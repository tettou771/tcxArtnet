#pragma once

// =============================================================================
// tcxArtnetConstants.h - Art-Net protocol constants
// =============================================================================
// Art-Net is a royalty-free protocol (by Artistic Licence) that transports
// DMX512 lighting data over UDP. A single "universe" carries up to 512 channels
// (0-255 each). This addon only needs the ArtDmx (OpOutput) opcode to send.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace tcx {

// UDP port every Art-Net node listens on.
inline constexpr int ARTNET_PORT = 6454;

// A DMX universe is 512 channels. We always send a full 512-byte frame for
// maximum compatibility (some nodes dislike short frames).
inline constexpr int DMX_UNIVERSE_SIZE = 512;

// Highest valid universe. Art-Net addresses a universe with a 15-bit port
// address (7-bit Net + 4-bit Sub-Net + 4-bit Universe), so the range is
// 0..32767. Values outside this are rejected rather than silently wrapped.
inline constexpr int ARTNET_MAX_UNIVERSE = 32767;

// Default cap on how many distinct universes one sender will hold. Generous
// enough for big LED matrices / pixel walls (2048 universes ~= 32 MB/s at 30 Hz,
// which is fine), while still bounding the per-frame send count so a buggy or
// hostile universe index can't grow the active set toward 32768 and saturate the
// send loop (a cheap DoS). New universes past the cap are rejected with a
// warning; raise it with setMaxUniverses() for genuinely huge installs.
inline constexpr size_t ARTNET_DEFAULT_MAX_UNIVERSES = 2048;

// OpOutput / ArtDmx. Goes on the wire little-endian (low byte first): 0x00 0x50.
inline constexpr uint16_t ARTNET_OPCODE_DMX = 0x5000;

// OpSync / ArtSync. Tells every node to latch its buffered ArtDmx data at once,
// so multiple universes update on the same frame (no inter-universe tearing).
// Little-endian on the wire: 0x00 0x52.
inline constexpr uint16_t ARTNET_OPCODE_SYNC = 0x5200;

// OpPoll (controller asks "who's there?") and OpPollReply (node describes itself).
// Used for node discovery. Little-endian on the wire: 0x00 0x20 / 0x00 0x21.
inline constexpr uint16_t ARTNET_OPCODE_POLL      = 0x2000;
inline constexpr uint16_t ARTNET_OPCODE_POLLREPLY = 0x2100;

// OEM code reported in ArtPollReply when you haven't registered one with
// Artistic Licence (the "unknown product" value).
inline constexpr uint16_t ARTNET_OEM_UNKNOWN = 0x00FF;

// Protocol version 14 (current). Sent big-endian: hi=0, lo=14.
inline constexpr uint8_t ARTNET_PROTOCOL_VER = 14;

// Primary broadcast address from the Art-Net spec (2.0.0.0/8). Nodes are
// typically configured in this range; ".255.255.255" makes setBroadcast kick in.
inline const std::string ARTNET_BROADCAST = "2.255.255.255";

// Max sensible refresh rate for DMX512 (~44 full frames/sec). Auto-send clamps
// to this so a runaway fps value can't flood the wire.
inline constexpr float ARTNET_MAX_FPS = 44.0f;

} // namespace tcx
