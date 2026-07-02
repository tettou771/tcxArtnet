#pragma once

// =============================================================================
// tcxArtnetTypes.h - shared structs for the ArtPoll / discovery layer
// =============================================================================

#include "tcxArtnetConstants.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tcx::artnet {

// What this app reports about itself in an ArtPollReply, i.e. only when it acts
// as a discoverable node. IP / MAC / firmware version / port flags are derived
// automatically, so they're not here — set just what identifies you.
struct NodeIdentity {
    std::string shortName = "TrussC";            // <= 17 chars on the wire
    std::string longName  = "TrussC tcxArtnet";  // <= 63 chars on the wire
    std::vector<int> universes;                  // universes this node serves
    uint16_t oem  = ARTNET_OEM_UNKNOWN;          // product code (default: unknown)
    uint16_t esta = 0x0000;                      // ESTA manufacturer (vendor) code
};

// A remote Art-Net node discovered via ArtPollReply.
struct ArtnetNodeInfo {
    std::string ip;              // source IP of the reply (the reliable address)
    std::string shortName;
    std::string longName;
    std::vector<int> universes;  // output universes the node advertises
    uint16_t oem  = 0;
    uint16_t esta = 0;
};

} // namespace tcx::artnet

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::artnet`. These
// silent aliases keep older code compiling: flat `tcx::NodeIdentity` and legacy
// `tc::NodeIdentity` / `trussc::NodeIdentity`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxArtnet README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using artnet::NodeIdentity; }        // deprecated: remove at v1.0.0
namespace tcx    { using artnet::ArtnetNodeInfo; }      // deprecated: remove at v1.0.0
namespace trussc { using tcx::artnet::NodeIdentity; }   // deprecated: remove at v1.0.0
namespace trussc { using tcx::artnet::ArtnetNodeInfo; } // deprecated: remove at v1.0.0
