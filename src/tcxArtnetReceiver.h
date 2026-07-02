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
#include "tcxArtnetTypes.h"

#include "tc/network/tcUdpSocket.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace tcx::artnet {

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
    tc::Event<DmxFrame> onDmx;          // each received ArtDmx (state updated)
    tc::Event<int> onSync;             // an ArtSync arrived (arg reserved, 0)
    tc::Event<std::string> onPoll;     // an ArtPoll arrived (arg: poller's IP)
    tc::Event<ArtnetNodeInfo> onNode;  // an ArtPollReply arrived (discovered node)

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

    // Nodes discovered from ArtPollReply (a controller sends ArtPoll, e.g. via
    // ArtnetSender::sendPoll(), and the replies accumulate here).
    std::vector<ArtnetNodeInfo> getNodes() const;
    void clearNodes();

private:
    void handleReceive(tc::UdpReceiveEventArgs& args);
    void parsePacket(const uint8_t* data, size_t size, const std::string& remoteHost);
    void parsePollReply(const uint8_t* p, size_t size, const std::string& remoteHost);

    tc::UdpSocket socket_;
    tc::EventListener receiveListener_;
    int port_ = 0;

    std::map<int, std::array<uint8_t, DMX_UNIVERSE_SIZE>> universes_;
    std::map<std::string, ArtnetNodeInfo> nodes_;  // discovered nodes, keyed by IP
    mutable std::mutex dataMutex_;
    bool newData_ = false;
};

} // namespace tcx::artnet

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::artnet`. These
// silent aliases keep older code compiling: flat `tcx::ArtnetReceiver` and legacy
// `tc::ArtnetReceiver` / `trussc::ArtnetReceiver`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxArtnet README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using artnet::DmxFrame; }            // deprecated: remove at v1.0.0
namespace tcx    { using artnet::ArtnetReceiver; }      // deprecated: remove at v1.0.0
namespace trussc { using tcx::artnet::DmxFrame; }       // deprecated: remove at v1.0.0
namespace trussc { using tcx::artnet::ArtnetReceiver; } // deprecated: remove at v1.0.0
