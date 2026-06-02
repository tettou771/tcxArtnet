#pragma once

// =============================================================================
// tcxArtnetSender.h - Art-Net DMX sender
// =============================================================================
// Wraps TrussC's core UdpSocket to emit ArtDmx packets. Send modes:
//   - manual    : set channels, then call send() once per frame
//   - auto       : startAutoSend(fps) spins a background thread that keeps
//                  resending every active universe at ~fps Hz (DMX wants a
//                  continuous refresh even when values don't change)
//
// Destinations default to the Art-Net broadcast address; add unicast targets
// with connect()/setup(). Channels are 1-based (1..512), matching how lighting
// consoles and fixture manuals address DMX.
// =============================================================================

#include "tcxArtnetConstants.h"

#include "tcColor.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/utils/tcLog.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tcx {

// =============================================================================
// ArtnetSender
// =============================================================================
class ArtnetSender {
public:
    ArtnetSender() = default;
    ~ArtnetSender() {
        stopAutoSend();
        close();
    }

    // Non-copyable (owns a socket and a thread).
    ArtnetSender(const ArtnetSender&) = delete;
    ArtnetSender& operator=(const ArtnetSender&) = delete;

    struct Destination {
        std::string host;
        int port = ARTNET_PORT;
        bool operator==(const Destination& o) const {
            return host == o.host && port == o.port;
        }
    };

    // -------------------------------------------------------------------------
    // Destination management
    // -------------------------------------------------------------------------

    // Clear destinations and set a single one (default: broadcast).
    bool setup(const std::string& host = ARTNET_BROADCAST, int port = ARTNET_PORT) {
        disconnect();
        return connect(host, port);
    }

    // Add a destination. Creates the socket on first use and enables broadcast
    // automatically for a ".255" address (same convention as tcxOsc).
    bool connect(const std::string& host, int port = ARTNET_PORT) {
        if (!socket_.isValid() && !socket_.create()) {
            tc::logError("tcxArtnet") << "failed to create UDP socket";
            return false;
        }
        if (host.size() >= 4 && host.substr(host.size() - 4) == ".255") {
            socket_.setBroadcast(true);
        }
        Destination d{host, port};
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (auto& existing : destinations_) {
            if (existing == d) return true;  // avoid duplicates
        }
        destinations_.push_back(d);
        return true;
    }

    // Remove all destinations (socket stays open).
    void disconnect() {
        std::lock_guard<std::mutex> lock(dataMutex_);
        destinations_.clear();
    }

    // Stop the thread, close the socket, drop destinations.
    void close() {
        stopAutoSend();
        socket_.close();
        std::lock_guard<std::mutex> lock(dataMutex_);
        destinations_.clear();
    }

    const std::vector<Destination>& getDestinations() const { return destinations_; }
    bool isConnected() const { return !destinations_.empty(); }

    // Cap on the number of distinct universes held (DoS guard against an
    // unbounded universe index). Lowering it does not evict existing universes;
    // it only blocks new ones from being created beyond the limit.
    ArtnetSender& setMaxUniverses(size_t n) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        maxUniverses_ = (n == 0) ? 1 : n;
        return *this;
    }
    size_t getMaxUniverses() const {
        std::lock_guard<std::mutex> lock(dataMutex_);
        return maxUniverses_;
    }

    // -------------------------------------------------------------------------
    // DMX data (channel is 1-based: 1..512)
    // -------------------------------------------------------------------------

    ArtnetSender& setChannel(int universe, int channel, uint8_t value) {
        if (!validUniverse(universe)) return *this;
        if (channel < 1 || channel > DMX_UNIVERSE_SIZE) {
            tc::logWarning("tcxArtnet") << "channel " << channel << " out of range (1.." << DMX_UNIVERSE_SIZE << ")";
            return *this;
        }
        std::lock_guard<std::mutex> lock(dataMutex_);
        auto* buf = ensureUniverseLocked(universe);
        if (!buf) return *this;
        (*buf)[channel - 1] = value;
        return *this;
    }

    // Write a contiguous block starting at startChannel (1-based). If the whole
    // block doesn't fit in 1..512, nothing is written (a partial write would
    // silently misalign the data, which is worse than a no-op + warning).
    ArtnetSender& setChannels(int universe, int startChannel, const std::vector<uint8_t>& values) {
        if (!validUniverse(universe)) return *this;
        if (values.empty()) return *this;
        int endChannel = startChannel + static_cast<int>(values.size()) - 1;
        if (startChannel < 1 || endChannel > DMX_UNIVERSE_SIZE) {
            tc::logWarning("tcxArtnet") << "setChannels range " << startChannel << ".." << endChannel
                << " out of bounds (1.." << DMX_UNIVERSE_SIZE << "); nothing written";
            return *this;
        }
        std::lock_guard<std::mutex> lock(dataMutex_);
        auto* buf = ensureUniverseLocked(universe);
        if (!buf) return *this;
        for (size_t i = 0; i < values.size(); ++i) {
            (*buf)[startChannel - 1 + static_cast<int>(i)] = values[i];
        }
        return *this;
    }

    // Write an RGB fixture: startChannel=R, +1=G, +2=B. Color is 0-1 -> 0-255.
    // All three channels must fit in 1..512, otherwise nothing is written (a
    // half-written color would show a wrong hue silently).
    ArtnetSender& setColor(int universe, int startChannel, const tc::Color& c) {
        if (!validUniverse(universe)) return *this;
        if (startChannel < 1 || startChannel + 2 > DMX_UNIVERSE_SIZE) {
            tc::logWarning("tcxArtnet") << "setColor needs channels " << startChannel << ".." << (startChannel + 2)
                << ", out of bounds (1.." << DMX_UNIVERSE_SIZE << "); nothing written";
            return *this;
        }
        auto to8 = [](float v) -> uint8_t {
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            return static_cast<uint8_t>(v * 255.0f + 0.5f);
        };
        std::lock_guard<std::mutex> lock(dataMutex_);
        auto* buf = ensureUniverseLocked(universe);
        if (!buf) return *this;
        (*buf)[startChannel - 1]     = to8(c.r);
        (*buf)[startChannel - 1 + 1] = to8(c.g);
        (*buf)[startChannel - 1 + 2] = to8(c.b);
        return *this;
    }

    // Zero one universe but KEEP it active, so it keeps being sent (a blackout
    // that still refreshes the link). To stop sending it, use removeUniverse().
    ArtnetSender& clear(int universe) {
        if (!validUniverse(universe)) return *this;
        std::lock_guard<std::mutex> lock(dataMutex_);
        auto* buf = ensureUniverseLocked(universe);
        if (buf) buf->fill(0);
        return *this;
    }

    // Zero every active universe (they stay active and keep being sent).
    ArtnetSender& clearAll() {
        std::lock_guard<std::mutex> lock(dataMutex_);
        for (auto& [uni, buf] : universes_) buf.fill(0);
        return *this;
    }

    // Drop one universe from the active set entirely: it stops being sent and
    // its buffer is freed. (clear() zeros but keeps sending; this removes it.)
    ArtnetSender& removeUniverse(int universe) {
        std::lock_guard<std::mutex> lock(dataMutex_);
        universes_.erase(universe);
        sequence_.erase(universe);
        return *this;
    }

    // Drop every universe: send() / auto-send then transmit nothing until you
    // set channels again. Frees all per-universe buffers.
    ArtnetSender& removeAllUniverses() {
        std::lock_guard<std::mutex> lock(dataMutex_);
        universes_.clear();
        sequence_.clear();
        return *this;
    }

    // Number of universes currently in the active set (what send() transmits).
    size_t getUniverseCount() const {
        std::lock_guard<std::mutex> lock(dataMutex_);
        return universes_.size();
    }

    // Active universe numbers, ascending (the keys send() iterates). Returned by
    // value as a snapshot, so it's safe to read while auto-send runs.
    std::vector<int> getUniverses() const {
        std::lock_guard<std::mutex> lock(dataMutex_);
        std::vector<int> result;
        result.reserve(universes_.size());
        for (auto& [uni, buf] : universes_) result.push_back(uni);
        return result;
    }

    uint8_t getChannel(int universe, int channel) const {
        if (!validUniverse(universe)) return 0;
        if (channel < 1 || channel > DMX_UNIVERSE_SIZE) return 0;
        std::lock_guard<std::mutex> lock(dataMutex_);
        auto it = universes_.find(universe);
        if (it == universes_.end()) return 0;
        return it->second[channel - 1];
    }

    // -------------------------------------------------------------------------
    // Manual send
    // -------------------------------------------------------------------------

    // Send every active universe once.
    bool send() {
        std::lock_guard<std::mutex> lock(dataMutex_);
        bool ok = true;
        for (auto& [uni, buf] : universes_) {
            if (!sendUniverseLocked(uni, buf)) ok = false;
        }
        return ok;
    }

    // Send one universe once (creates it zeroed if it doesn't exist yet).
    bool sendUniverse(int universe) {
        if (!validUniverse(universe)) return false;
        std::lock_guard<std::mutex> lock(dataMutex_);
        auto* buf = ensureUniverseLocked(universe);
        if (!buf) return false;
        return sendUniverseLocked(universe, *buf);
    }

    // -------------------------------------------------------------------------
    // Auto-send (background refresh thread)
    // -------------------------------------------------------------------------

    // Keep resending all active universes at ~fps Hz. Calling again while
    // running just updates the rate. fps is clamped to (0, ARTNET_MAX_FPS].
    void startAutoSend(float fps = 30.0f) {
        // Floor at 1 Hz: keeps the sleep <= 1s (so stopAutoSend()'s join always
        // returns within ~1s) and still far exceeds Art-Net's ~0.25 Hz (every
        // 4s) keep-alive minimum. Also guards the 1000/fps division below.
        if (fps <= 0.0f) fps = 1.0f;
        if (fps > ARTNET_MAX_FPS) fps = ARTNET_MAX_FPS;
        fps_.store(fps);
        if (running_.exchange(true)) return;  // already running -> just retuned fps
        thread_ = std::thread([this]() { autoSendLoop(); });
    }

    void stopAutoSend() {
        if (!running_.exchange(false)) return;
        if (thread_.joinable()) thread_.join();
    }

    bool isAutoSending() const { return running_.load(); }

private:
    // Reject out-of-range universes (0..32767) instead of letting them silently
    // wrap on the wire or spawn stray 512-byte buffers from a bad/negative index.
    bool validUniverse(int universe) const {
        if (universe < 0 || universe > ARTNET_MAX_UNIVERSE) {
            tc::logWarning("tcxArtnet") << "universe " << universe << " out of range (0.." << ARTNET_MAX_UNIVERSE << ")";
            return false;
        }
        return true;
    }

    // Must hold dataMutex_. Returns the universe's buffer, creating it if needed.
    // Returns nullptr if it doesn't exist yet and we're at the universe cap, so
    // a runaway/hostile universe index can't grow the active set without bound.
    std::array<uint8_t, DMX_UNIVERSE_SIZE>* ensureUniverseLocked(int universe) {
        auto it = universes_.find(universe);
        if (it != universes_.end()) return &it->second;
        if (universes_.size() >= maxUniverses_) {
            tc::logWarning("tcxArtnet") << "universe limit reached (" << maxUniverses_
                << "); ignoring new universe " << universe;
            return nullptr;
        }
        return &universes_[universe];  // inserts a zeroed buffer
    }

    // Build an 18-byte ArtDmx header followed by the 512-byte DMX frame.
    void buildArtDmxPacket(int universe, uint8_t sequence,
                           const std::array<uint8_t, DMX_UNIVERSE_SIZE>& data,
                           std::vector<uint8_t>& out) const {
        out.clear();
        out.reserve(18 + DMX_UNIVERSE_SIZE);
        // ID[8] "Art-Net\0"
        const char* id = "Art-Net";
        for (int i = 0; i < 7; ++i) out.push_back(static_cast<uint8_t>(id[i]));
        out.push_back(0);
        // OpCode (little-endian)
        out.push_back(static_cast<uint8_t>(ARTNET_OPCODE_DMX & 0xFF));
        out.push_back(static_cast<uint8_t>((ARTNET_OPCODE_DMX >> 8) & 0xFF));
        // Protocol version (big-endian)
        out.push_back(0);
        out.push_back(ARTNET_PROTOCOL_VER);
        // Sequence, Physical
        out.push_back(sequence);
        out.push_back(0);
        // SubUni (low byte) + Net (high 7 bits) of the 15-bit port address
        out.push_back(static_cast<uint8_t>(universe & 0xFF));
        out.push_back(static_cast<uint8_t>((universe >> 8) & 0x7F));
        // Length (big-endian), always full 512
        out.push_back(static_cast<uint8_t>((DMX_UNIVERSE_SIZE >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(DMX_UNIVERSE_SIZE & 0xFF));
        // DMX data
        out.insert(out.end(), data.begin(), data.end());
    }

    // Caller must hold dataMutex_.
    bool sendUniverseLocked(int universe, const std::array<uint8_t, DMX_UNIVERSE_SIZE>& data) {
        if (destinations_.empty()) return false;
        // Sequence 1..255 looping (0 means "disabled" in the spec).
        uint8_t& seq = sequence_[universe];
        seq = (seq >= 255) ? 1 : static_cast<uint8_t>(seq + 1);
        buildArtDmxPacket(universe, seq, data, packetScratch_);
        bool ok = true;
        for (auto& d : destinations_) {
            if (!socket_.sendTo(d.host, d.port, packetScratch_.data(), packetScratch_.size())) {
                ok = false;
            }
        }
        return ok;
    }

    void autoSendLoop() {
        while (running_.load()) {
            {
                std::lock_guard<std::mutex> lock(dataMutex_);
                for (auto& [uni, buf] : universes_) sendUniverseLocked(uni, buf);
            }
            float fps = fps_.load();
            int ms = static_cast<int>(1000.0f / fps);
            if (ms < 1) ms = 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
    }

    tc::UdpSocket socket_;
    std::vector<Destination> destinations_;
    std::map<int, std::array<uint8_t, DMX_UNIVERSE_SIZE>> universes_;
    std::map<int, uint8_t> sequence_;
    std::vector<uint8_t> packetScratch_;  // reused send buffer (under dataMutex_)
    size_t maxUniverses_ = ARTNET_DEFAULT_MAX_UNIVERSES;  // DoS cap on new universes
    mutable std::mutex dataMutex_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<float> fps_{30.0f};
};

} // namespace tcx
