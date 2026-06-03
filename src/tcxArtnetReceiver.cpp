// =============================================================================
// tcxArtnetReceiver.cpp - Art-Net DMX receiver (implementation)
// =============================================================================

#include "tcxArtnetReceiver.h"

#include <cstring>

using namespace std;

namespace tcx {

ArtnetReceiver::~ArtnetReceiver() {
    close();
}

bool ArtnetReceiver::setup(int port) {
    port_ = port;
    receiveListener_ = socket_.onReceive.listen(
        [this](tc::UdpReceiveEventArgs& args) { handleReceive(args); });
    socket_.create();
    socket_.setReuseAddress(true);   // allow other receivers on the same port
    return socket_.bind(port, /*startReceiving=*/true);
}

void ArtnetReceiver::close() {
    socket_.close();               // stops the receive thread
    receiveListener_.disconnect();
    port_ = 0;
    lock_guard<mutex> lock(dataMutex_);
    universes_.clear();
    newData_ = false;
}

bool ArtnetReceiver::isListening() const {
    return socket_.isReceiving();
}

// ----------------------------------------------------------------------------- receive
void ArtnetReceiver::handleReceive(tc::UdpReceiveEventArgs& args) {
    if (args.data.empty()) return;
    parsePacket(reinterpret_cast<const uint8_t*>(args.data.data()), args.data.size());
}

void ArtnetReceiver::parsePacket(const uint8_t* p, size_t size) {
    // Common header: "Art-Net\0" (8) + OpCode (2). 12 bytes covers up to protver.
    if (size < 12) return;
    if (memcmp(p, "Art-Net", 7) != 0 || p[7] != 0) return;  // not an Art-Net packet
    uint16_t opcode = static_cast<uint16_t>(p[8] | (p[9] << 8));  // little-endian

    if (opcode == ARTNET_OPCODE_SYNC) {
        int reserved = 0;
        onSync.notify(reserved);
        return;
    }
    if (opcode != ARTNET_OPCODE_DMX) return;  // ignore ArtPoll etc.
    if (size < 18) return;                    // ArtDmx needs the full 18-byte header

    uint8_t sequence = p[12];
    int subUni = p[14];
    int net = p[15];
    int universe = (net << 8) | subUni;       // 15-bit port address, always 0..32767
    uint16_t length = static_cast<uint16_t>((p[16] << 8) | p[17]);  // big-endian

    // Trust neither the length field nor the datagram: clamp to what's present
    // and to a universe size.
    size_t avail = size - 18;
    if (length > avail) length = static_cast<uint16_t>(avail);
    if (length > DMX_UNIVERSE_SIZE) length = DMX_UNIVERSE_SIZE;

    DmxFrame frame;
    frame.universe = universe;
    frame.sequence = sequence;
    frame.length = length;
    {
        lock_guard<mutex> lock(dataMutex_);
        // Persistent 512-channel buffer per universe: merge the first `length`
        // channels, leave the rest at their previous values (a short ArtDmx says
        // nothing about channels beyond its length).
        auto& buf = universes_[universe];
        for (uint16_t i = 0; i < length; ++i) buf[i] = p[18 + i];
        newData_ = true;
        frame.data.assign(buf.begin(), buf.end());  // full 512 snapshot
    }
    // Notify outside the lock so a handler can call getChannel()/getDmx() without
    // deadlocking on the (non-recursive) mutex.
    onDmx.notify(frame);
}

// ----------------------------------------------------------------------------- state read
uint8_t ArtnetReceiver::getChannel(int universe, int channel) const {
    if (channel < 1 || channel > DMX_UNIVERSE_SIZE) return 0;
    lock_guard<mutex> lock(dataMutex_);
    auto it = universes_.find(universe);
    if (it == universes_.end()) return 0;
    return it->second[channel - 1];
}

vector<uint8_t> ArtnetReceiver::getDmx(int universe) const {
    lock_guard<mutex> lock(dataMutex_);
    auto it = universes_.find(universe);
    if (it == universes_.end()) return {};
    return vector<uint8_t>(it->second.begin(), it->second.end());
}

vector<int> ArtnetReceiver::getUniverses() const {
    lock_guard<mutex> lock(dataMutex_);
    vector<int> result;
    result.reserve(universes_.size());
    for (auto& [uni, buf] : universes_) result.push_back(uni);
    return result;
}

bool ArtnetReceiver::hasUniverse(int universe) const {
    lock_guard<mutex> lock(dataMutex_);
    return universes_.find(universe) != universes_.end();
}

bool ArtnetReceiver::hasNewData() {
    lock_guard<mutex> lock(dataMutex_);
    bool n = newData_;
    newData_ = false;
    return n;
}

} // namespace tcx
