#pragma once

// =============================================================================
// tcxArtnetConstants.h - Art-Net protocol constants
// =============================================================================
// Art-Net is a royalty-free protocol (by Artistic Licence) that transports
// DMX512 lighting data over UDP. A single "universe" carries up to 512 channels
// (0-255 each). This addon only needs the ArtDmx (OpOutput) opcode to send.
// =============================================================================

#include <cstdint>
#include <string>

namespace tcx {

// UDP port every Art-Net node listens on.
inline constexpr int ARTNET_PORT = 6454;

// A DMX universe is 512 channels. We always send a full 512-byte frame for
// maximum compatibility (some nodes dislike short frames).
inline constexpr int DMX_UNIVERSE_SIZE = 512;

// OpOutput / ArtDmx. Goes on the wire little-endian (low byte first): 0x00 0x50.
inline constexpr uint16_t ARTNET_OPCODE_DMX = 0x5000;

// Protocol version 14 (current). Sent big-endian: hi=0, lo=14.
inline constexpr uint8_t ARTNET_PROTOCOL_VER = 14;

// Primary broadcast address from the Art-Net spec (2.0.0.0/8). Nodes are
// typically configured in this range; ".255.255.255" makes setBroadcast kick in.
inline const std::string ARTNET_BROADCAST = "2.255.255.255";

// Max sensible refresh rate for DMX512 (~44 full frames/sec). Auto-send clamps
// to this so a runaway fps value can't flood the wire.
inline constexpr float ARTNET_MAX_FPS = 44.0f;

} // namespace tcx
