#pragma once

// =============================================================================
// tcxArtnetSender.h - Art-Net DMX sender (public interface)
// =============================================================================
// Wraps TrussC's core UdpSocket to emit ArtDmx packets. Send modes:
//   - manual : set channels, then call send() once per frame
//   - auto   : startAutoSend(fps) spins a background thread that keeps resending
//              every active universe at ~fps Hz (DMX wants a continuous refresh
//              even when values don't change)
//
// Destinations default to the Art-Net broadcast address; add unicast targets
// with connect()/setup(). Channels are 1-based (1..512), matching how lighting
// consoles and fixture manuals address DMX.
//
// Implementation lives in tcxArtnetSender.cpp (so this header reads as the API
// surface and the addon builds once as a static lib via auto-collection).
// =============================================================================

#include "tcxArtnetConstants.h"
#include "tcxArtnetTypes.h"

#include "tcColor.h"
#include "tc/network/tcUdpSocket.h"

#include <array>
#include <atomic>
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
    ~ArtnetSender();

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

    // ------------------------------------------------------------------ destinations
    // Clear destinations and set a single one (default: broadcast).
    bool setup(const std::string& host = ARTNET_BROADCAST, int port = ARTNET_PORT);
    // Add a destination (creates the socket; auto-enables broadcast for ".255").
    bool connect(const std::string& host, int port = ARTNET_PORT);
    // Remove all destinations (socket stays open).
    void disconnect();
    // Stop the thread, close the socket, drop destinations.
    void close();

    const std::vector<Destination>& getDestinations() const { return destinations_; }
    bool isConnected() const { return !destinations_.empty(); }

    // Cap on the number of distinct universes held (DoS guard against an
    // unbounded universe index). Lowering it does not evict existing universes;
    // it only blocks new ones from being created beyond the limit.
    ArtnetSender& setMaxUniverses(size_t n);
    size_t getMaxUniverses() const;

    // ------------------------------------------------------------------ DMX data (channel is 1-based: 1..512)
    ArtnetSender& setChannel(int universe, int channel, uint8_t value);
    // Write a contiguous block from startChannel. If the whole block doesn't fit
    // in 1..512, nothing is written (a partial write would silently misalign).
    ArtnetSender& setChannels(int universe, int startChannel, const std::vector<uint8_t>& values);
    // Write an RGB fixture: startChannel=R, +1=G, +2=B (Color 0-1 -> 0-255). All
    // three must fit, otherwise nothing is written (a half-written color lies).
    ArtnetSender& setColor(int universe, int startChannel, const tc::Color& c);

    // Zero one universe but KEEP it active (blackout that still refreshes the
    // link). To stop sending it entirely, use removeUniverse().
    ArtnetSender& clear(int universe);
    // Zero every active universe (they stay active and keep being sent).
    ArtnetSender& clearAll();

    // Drop one / all universes from the active set: they stop being sent and
    // their buffers are freed. (clear() zeros but keeps sending; this removes.)
    ArtnetSender& removeUniverse(int universe);
    ArtnetSender& removeAllUniverses();

    size_t getUniverseCount() const;
    // Active universe numbers, ascending (a snapshot, safe while auto-send runs).
    std::vector<int> getUniverses() const;
    uint8_t getChannel(int universe, int channel) const;

    // ------------------------------------------------------------------ send
    bool send();                  // send every active universe once (ArtDmx only)
    bool sendUniverse(int universe);  // send one universe once (creates it zeroed)

    // Emit one ArtSync to all destinations: every node latches its buffered
    // ArtDmx simultaneously, so multiple universes update on the same frame.
    // Call it right after send() (manual sync). Note: sending ArtSync puts nodes
    // into synchronous mode; you must keep sending it (every frame, within ~4s)
    // or they freeze briefly then revert to async output. send()/sendUniverse()
    // never append it for you — sync is always explicit.
    bool sendSync();

    // ------------------------------------------------------------------ auto-send (background thread)
    // Keep resending all active universes at ~fps Hz on a background thread.
    // Calling either variant again while running just retunes (rate + sync mode)
    // without starting a second thread. fps is floored to 1, clamped to MAX_FPS.
    void startAutoSend(float fps = 30.0f);        // ArtDmx only
    void startAutoSendSynced(float fps = 30.0f);  // ArtDmx + ArtSync each tick
    void stopAutoSend();
    bool isAutoSending() const { return running_.load(); }

    // ------------------------------------------------------------------ discovery (ArtPoll)
    // Broadcast an ArtPoll to all destinations (nodes reply with ArtPollReply,
    // which an ArtnetReceiver collects). Set a broadcast destination to find all.
    bool sendPoll();
    // Send an ArtPollReply describing `id` to one host (used to answer an ArtPoll
    // when acting as a discoverable node). One reply per declared universe.
    bool sendPollReply(const NodeIdentity& id, const std::string& host, int port = ARTNET_PORT);

private:
    bool validUniverse(int universe) const;
    // Must hold dataMutex_. Returns the universe buffer, creating it if needed;
    // nullptr if it's new and we're at the cap.
    std::array<uint8_t, DMX_UNIVERSE_SIZE>* ensureUniverseLocked(int universe);
    void buildArtDmxPacket(int universe, uint8_t sequence,
                           const std::array<uint8_t, DMX_UNIVERSE_SIZE>& data,
                           std::vector<uint8_t>& out) const;
    void buildArtSyncPacket(std::vector<uint8_t>& out) const;
    void buildArtPollPacket(std::vector<uint8_t>& out) const;
    // Build one ArtPollReply. universe < 0 means "no ports" (node serves none).
    void buildArtPollReplyPacket(const NodeIdentity& id, int universe, std::vector<uint8_t>& out) const;
    bool sendUniverseLocked(int universe, const std::array<uint8_t, DMX_UNIVERSE_SIZE>& data);
    bool sendSyncLocked();  // must hold dataMutex_
    // Shared impl for both startAutoSend variants: retunes if already running so
    // a second call never spawns a second thread.
    void startAutoSendImpl(float fps, bool synchronous);
    void autoSendLoop();

    tc::UdpSocket socket_;
    std::vector<Destination> destinations_;
    std::map<int, std::array<uint8_t, DMX_UNIVERSE_SIZE>> universes_;
    std::map<int, uint8_t> sequence_;
    std::vector<uint8_t> packetScratch_;  // reused send buffer (under dataMutex_)
    size_t maxUniverses_ = ARTNET_DEFAULT_MAX_UNIVERSES;  // DoS cap on new universes
    mutable std::mutex dataMutex_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> synchronous_{false};  // auto-send appends ArtSync each tick
    std::atomic<float> fps_{30.0f};
};

} // namespace tcx
