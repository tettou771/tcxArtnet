// =============================================================================
// tcxArtnetSender.cpp - Art-Net DMX sender (implementation)
// =============================================================================

#include "tcxArtnetSender.h"

#include "tc/utils/tcLog.h"

#include <chrono>

using namespace std;

namespace tcx {

// ----------------------------------------------------------------------------- lifecycle
ArtnetSender::~ArtnetSender() {
    stopAutoSend();
    close();
}

// ----------------------------------------------------------------------------- destinations
bool ArtnetSender::setup(const string& host, int port) {
    disconnect();
    return connect(host, port);
}

bool ArtnetSender::connect(const string& host, int port) {
    if (!socket_.isValid() && !socket_.create()) {
        tc::logError("tcxArtnet") << "failed to create UDP socket";
        return false;
    }
    // Auto-enable broadcast for a ".255" address (same convention as tcxOsc).
    if (host.size() >= 4 && host.substr(host.size() - 4) == ".255") {
        socket_.setBroadcast(true);
    }
    Destination d{host, port};
    lock_guard<mutex> lock(dataMutex_);
    for (auto& existing : destinations_) {
        if (existing == d) return true;  // avoid duplicates
    }
    destinations_.push_back(d);
    return true;
}

void ArtnetSender::disconnect() {
    lock_guard<mutex> lock(dataMutex_);
    destinations_.clear();
}

void ArtnetSender::close() {
    stopAutoSend();
    socket_.close();
    lock_guard<mutex> lock(dataMutex_);
    destinations_.clear();
}

// ----------------------------------------------------------------------------- limits
ArtnetSender& ArtnetSender::setMaxUniverses(size_t n) {
    lock_guard<mutex> lock(dataMutex_);
    maxUniverses_ = (n == 0) ? 1 : n;
    return *this;
}

size_t ArtnetSender::getMaxUniverses() const {
    lock_guard<mutex> lock(dataMutex_);
    return maxUniverses_;
}

// ----------------------------------------------------------------------------- DMX data
ArtnetSender& ArtnetSender::setChannel(int universe, int channel, uint8_t value) {
    if (!validUniverse(universe)) return *this;
    if (channel < 1 || channel > DMX_UNIVERSE_SIZE) {
        tc::logWarning("tcxArtnet") << "channel " << channel << " out of range (1.." << DMX_UNIVERSE_SIZE << ")";
        return *this;
    }
    lock_guard<mutex> lock(dataMutex_);
    auto* buf = ensureUniverseLocked(universe);
    if (!buf) return *this;
    (*buf)[channel - 1] = value;
    return *this;
}

ArtnetSender& ArtnetSender::setChannels(int universe, int startChannel, const vector<uint8_t>& values) {
    if (!validUniverse(universe)) return *this;
    if (values.empty()) return *this;
    int endChannel = startChannel + static_cast<int>(values.size()) - 1;
    if (startChannel < 1 || endChannel > DMX_UNIVERSE_SIZE) {
        tc::logWarning("tcxArtnet") << "setChannels range " << startChannel << ".." << endChannel
            << " out of bounds (1.." << DMX_UNIVERSE_SIZE << "); nothing written";
        return *this;
    }
    lock_guard<mutex> lock(dataMutex_);
    auto* buf = ensureUniverseLocked(universe);
    if (!buf) return *this;
    for (size_t i = 0; i < values.size(); ++i) {
        (*buf)[startChannel - 1 + static_cast<int>(i)] = values[i];
    }
    return *this;
}

ArtnetSender& ArtnetSender::setColor(int universe, int startChannel, const tc::Color& c) {
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
    lock_guard<mutex> lock(dataMutex_);
    auto* buf = ensureUniverseLocked(universe);
    if (!buf) return *this;
    (*buf)[startChannel - 1]     = to8(c.r);
    (*buf)[startChannel - 1 + 1] = to8(c.g);
    (*buf)[startChannel - 1 + 2] = to8(c.b);
    return *this;
}

ArtnetSender& ArtnetSender::clear(int universe) {
    if (!validUniverse(universe)) return *this;
    lock_guard<mutex> lock(dataMutex_);
    auto* buf = ensureUniverseLocked(universe);
    if (buf) buf->fill(0);
    return *this;
}

ArtnetSender& ArtnetSender::clearAll() {
    lock_guard<mutex> lock(dataMutex_);
    for (auto& [uni, buf] : universes_) buf.fill(0);
    return *this;
}

ArtnetSender& ArtnetSender::removeUniverse(int universe) {
    lock_guard<mutex> lock(dataMutex_);
    universes_.erase(universe);
    sequence_.erase(universe);
    return *this;
}

ArtnetSender& ArtnetSender::removeAllUniverses() {
    lock_guard<mutex> lock(dataMutex_);
    universes_.clear();
    sequence_.clear();
    return *this;
}

size_t ArtnetSender::getUniverseCount() const {
    lock_guard<mutex> lock(dataMutex_);
    return universes_.size();
}

vector<int> ArtnetSender::getUniverses() const {
    lock_guard<mutex> lock(dataMutex_);
    vector<int> result;
    result.reserve(universes_.size());
    for (auto& [uni, buf] : universes_) result.push_back(uni);
    return result;
}

uint8_t ArtnetSender::getChannel(int universe, int channel) const {
    if (!validUniverse(universe)) return 0;
    if (channel < 1 || channel > DMX_UNIVERSE_SIZE) return 0;
    lock_guard<mutex> lock(dataMutex_);
    auto it = universes_.find(universe);
    if (it == universes_.end()) return 0;
    return it->second[channel - 1];
}

// ----------------------------------------------------------------------------- send
bool ArtnetSender::send() {
    lock_guard<mutex> lock(dataMutex_);
    bool ok = true;
    for (auto& [uni, buf] : universes_) {
        if (!sendUniverseLocked(uni, buf)) ok = false;
    }
    return ok;
}

bool ArtnetSender::sendUniverse(int universe) {
    if (!validUniverse(universe)) return false;
    lock_guard<mutex> lock(dataMutex_);
    auto* buf = ensureUniverseLocked(universe);
    if (!buf) return false;
    return sendUniverseLocked(universe, *buf);
}

// ----------------------------------------------------------------------------- auto-send
void ArtnetSender::startAutoSend(float fps) {
    // Floor at 1 Hz: keeps the sleep <= 1s (so stopAutoSend()'s join always
    // returns within ~1s) and still far exceeds Art-Net's ~0.25 Hz (every 4s)
    // keep-alive minimum. Also guards the 1000/fps division in autoSendLoop().
    if (fps <= 0.0f) fps = 1.0f;
    if (fps > ARTNET_MAX_FPS) fps = ARTNET_MAX_FPS;
    fps_.store(fps);
    if (running_.exchange(true)) return;  // already running -> just retuned fps
    thread_ = thread([this]() { autoSendLoop(); });
}

void ArtnetSender::stopAutoSend() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

// ----------------------------------------------------------------------------- internals
bool ArtnetSender::validUniverse(int universe) const {
    if (universe < 0 || universe > ARTNET_MAX_UNIVERSE) {
        tc::logWarning("tcxArtnet") << "universe " << universe << " out of range (0.." << ARTNET_MAX_UNIVERSE << ")";
        return false;
    }
    return true;
}

array<uint8_t, DMX_UNIVERSE_SIZE>* ArtnetSender::ensureUniverseLocked(int universe) {
    auto it = universes_.find(universe);
    if (it != universes_.end()) return &it->second;
    if (universes_.size() >= maxUniverses_) {
        tc::logWarning("tcxArtnet") << "universe limit reached (" << maxUniverses_
            << "); ignoring new universe " << universe;
        return nullptr;
    }
    return &universes_[universe];  // inserts a zeroed buffer
}

void ArtnetSender::buildArtDmxPacket(int universe, uint8_t sequence,
                                     const array<uint8_t, DMX_UNIVERSE_SIZE>& data,
                                     vector<uint8_t>& out) const {
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

bool ArtnetSender::sendUniverseLocked(int universe, const array<uint8_t, DMX_UNIVERSE_SIZE>& data) {
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

void ArtnetSender::autoSendLoop() {
    while (running_.load()) {
        {
            lock_guard<mutex> lock(dataMutex_);
            for (auto& [uni, buf] : universes_) sendUniverseLocked(uni, buf);
        }
        float fps = fps_.load();
        int ms = static_cast<int>(1000.0f / fps);
        if (ms < 1) ms = 1;
        this_thread::sleep_for(chrono::milliseconds(ms));
    }
}

} // namespace tcx
