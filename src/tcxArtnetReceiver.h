#pragma once

// =============================================================================
// tcxArtnetReceiver.h - Art-Net DMX receiver (public interface)
// =============================================================================
// Binds a UDP port and parses incoming ArtDmx into per-universe state. Two ways
// to consume, mirroring tcxOsc/tcxMidi:
//   - async  : listen to onDmx (fires on the receive thread, lowest latency;
//              guard your own shared state)
//   - polling: read the latest state from the main thread anytime with
//              getChannel()/getDmx() (DMX is a 512-channel framebuffer, not a
//              message stream, so polling reads current state — there is no
//              queue to drain)
//
// ArtSync is surfaced as onSync but applied immediately: each ArtDmx updates the
// visible state the moment it arrives (no receive-side sync staging). Channels
// are 1-based (1..512). Implementation lives in tcxArtnetReceiver.cpp.
// =============================================================================

#include "tcxArtnetConstants.h"

#include "tc/network/tcUdpSocket.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace tcx {

// One received ArtDmx frame. data is always the full 512-channel universe state
// (merged) at receive time; length is how many channels the packet actually
// carried (usually 512), kept for debugging short/partial frames.
struct DmxFrame {
    int universe = 0;
    uint8_t sequence = 0;
    uint16_t length = 0;
    std::vector<uint8_t> data;  // 512 bytes
};

// =============================================================================
// ArtnetReceiver
// =============================================================================
class ArtnetReceiver {
public:
    ArtnetReceiver() = default;
    ~ArtnetReceiver();

    // Non-copyable (owns a socket and a receive thread).
    ArtnetReceiver(const ArtnetReceiver&) = delete;
    ArtnetReceiver& operator=(const ArtnetReceiver&) = delete;

    // Events fire on the receive thread — guard any shared state you touch.
    tc::Event<DmxFrame> onDmx;   // each received ArtDmx (state already updated)
    tc::Event<int> onSync;       // an ArtSync arrived (arg reserved, always 0)

    bool setup(int port = ARTNET_PORT);  // bind + start receive thread
    void close();
    bool isListening() const;
    int getPort() const { return port_; }

    // State read (poll from the main thread; thread-safe snapshots).
    uint8_t getChannel(int universe, int channel) const;   // 1-based, 0 if unseen
    std::vector<uint8_t> getDmx(int universe) const;        // latest 512, empty if unseen
    std::vector<int> getUniverses() const;                 // universes seen, ascending
    bool hasUniverse(int universe) const;
    bool hasNewData();   // true if any universe updated since the last call (clears)

private:
    void handleReceive(tc::UdpReceiveEventArgs& args);
    void parsePacket(const uint8_t* data, size_t size);

    tc::UdpSocket socket_;
    tc::EventListener receiveListener_;
    int port_ = 0;

    std::map<int, std::array<uint8_t, DMX_UNIVERSE_SIZE>> universes_;
    mutable std::mutex dataMutex_;
    bool newData_ = false;
};

} // namespace tcx
