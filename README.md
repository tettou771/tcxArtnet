# tcxArtnet

Art-Net sender for [TrussC](https://github.com/TrussC-org/TrussC). Drive DMX512
lighting fixtures and Art-Net nodes (moving heads, LED pars, dimmers, …) straight
from a TrussC app over UDP — no external library, it just wraps the core
`UdpSocket`.

> ⚠️ **Not yet tested against real hardware.** The packet format is verified by a
> loopback test (a well-formed ArtDmx frame with correct opcode / length / channel
> data, plus the ~30 Hz auto-send), but it hasn't been confirmed against an actual
> Art-Net node / DMX fixture yet. Use at your own risk and please report back if
> you try it on hardware.

> **Scope:** sending only (controller side). Receiving / node mode
> (`ArtnetReceiver`, ArtPoll discovery) is intentionally left out for now; the
> sender is a standalone class so a receiver can be added later without touching it.

## Features

- **Multi-universe** output (`setChannel(universe, channel, value)`).
- **Broadcast by default** (`2.255.255.255:6454`), or **unicast** to a specific node.
- **Two send modes**: manual `send()` per frame, or a background
  `startAutoSend(fps)` thread that keeps refreshing the output (DMX wants a
  continuous resend even when values don't change).
- **Color helper**: `setColor()` maps a TrussC `Color` (0–1 float) onto three
  consecutive RGB channels.

## Install

Add it to your project's `addons.make`:

```
tcxArtnet
```

…then `trusscli update`. There's no build config to write — it's header-only and
TrussC auto-collects `src/`.

## Quick start

```cpp
#include <TrussC.h>
#include <tcxArtnet.h>
using namespace tc;
using namespace tcx;

class tcApp : public App {
    ArtnetSender artnet_;

    void setup() override {
        artnet_.setup();             // default: broadcast to 2.255.255.255:6454
        // artnet_.setup("192.168.1.50");   // ...or unicast to one node
        artnet_.startAutoSend(30);   // keep refreshing at ~30 Hz in the background
    }

    void update() override {
        float hue = fmodf(getElapsedTimef() * 0.1f, 1.0f);
        artnet_.setColor(0, 1, ColorHSB(hue, 1, 1).toRGB());  // universe 0, ch 1-3 = RGB
    }
};
```

Set values whenever you like; the auto-send thread takes care of pushing frames.
If you'd rather drive the timing yourself, skip `startAutoSend()` and call
`artnet_.send()` once per frame in `update()`.

## API

```cpp
// Destinations (broadcast detected automatically for a ".255" address)
bool setup(host = "2.255.255.255", port = 6454);  // clear + set one destination
bool connect(host, port = 6454);                  // add a destination
void disconnect();                                // clear destinations
void close();                                     // stop thread + close socket

// DMX data — channels are 1-based (1..512), matching console/fixture addressing
ArtnetSender& setChannel(universe, channel, value);         // value: 0-255
ArtnetSender& setChannels(universe, startChannel, values);  // vector<uint8_t>
ArtnetSender& setColor(universe, startChannel, Color);      // RGB -> 3 channels
ArtnetSender& clear(universe);                              // zero one universe
ArtnetSender& clearAll();                                   // blackout
uint8_t getChannel(universe, channel) const;

// Send
bool send();                  // send every active universe once
bool sendUniverse(universe);  // send one universe once

// Background auto-refresh
void startAutoSend(fps = 30);  // clamped to <= 44 Hz; call again to retune fps
void stopAutoSend();
bool isAutoSending() const;
```

### Notes

- **Channels are 1-based.** Channel 1 is the first DMX slot — same numbering as
  every lighting desk and fixture manual.
- **Universes** use the 15-bit Art-Net port address (`Net` + `SubUni`); pass the
  plain universe number (e.g. `0`, `1`, …, up to `32767`).
- Each universe always goes out as a full **512-byte** frame for broad node
  compatibility.
- The auto-send thread and your `update()` thread share the channel buffers under
  a mutex, so it's safe to set channels from `update()` while auto-send runs.

## Example

`example-basic/` sweeps an RGB fixture through the hue wheel on universe 0,
channels 1–3, with auto-send running. `[SPACE]` blacks out, `[A]` toggles
auto-send.

```bash
cd example-basic
trusscli update
trusscli run
```

To verify without hardware, point it at loopback (`artnet_.setup("127.0.0.1")`)
and watch the packets:

```bash
nc -u -l 6454 | xxd | head      # first 8 bytes read "Art-Net", opcode 00 50
```

## License

MIT — see [LICENSES.md](LICENSES.md). "Art-Net™" is a trademark of Artistic
Licence Engineering Ltd; the protocol is royalty-free and implemented here
independently.
