# tcxArtnet

[![CI](https://github.com/tettou771/tcxArtnet/actions/workflows/ci.yml/badge.svg)](https://github.com/tettou771/tcxArtnet/actions/workflows/ci.yml)

Art-Net sender for [TrussC](https://github.com/TrussC-org/TrussC). Drive DMX512
lighting fixtures and Art-Net nodes (moving heads, LED pars, dimmers, …) straight
from a TrussC app over UDP — no external library, it just wraps the core
`UdpSocket`.

> ⚠️ **Not yet tested against real hardware.** The packet format is verified by a
> loopback test (a well-formed ArtDmx frame with correct opcode / length / channel
> data, plus the ~30 Hz auto-send), but it hasn't been confirmed against an actual
> Art-Net node / DMX fixture yet. Use at your own risk and please report back if
> you try it on hardware.

> **Scope:** send (`ArtnetSender`), receive (`ArtnetReceiver`), and node discovery
> via ArtPoll (`ArtnetNode`).

## Features

- **Send + receive** — `ArtnetSender` and `ArtnetReceiver`, both state-oriented
  (set/read channels), event + polling on the receive side.
- **Discovery** — `ArtnetNode` bundles a sender + receiver for ArtPoll node
  discovery and (opt-in) answering ArtPoll as a discoverable node.
- **Multi-universe** (`setChannel(universe, channel, value)`).
- **Broadcast by default** (`2.255.255.255:6454`), or **unicast** to a specific node.
- **Two send modes**: manual `send()` per frame, or a background
  `startAutoSend(fps)` thread that keeps refreshing the output (DMX wants a
  continuous resend even when values don't change).
- **ArtSync** for tear-free multi-universe output (`sendSync()` / `startAutoSendSynced()`).
- **Color helper**: `setColor()` maps a TrussC `Color` (0–1 float) onto three
  consecutive RGB channels.

## Install

Add it to your project's `addons.make`:

```
tcxArtnet
```

…then `trusscli update`. There's no build config to write — TrussC auto-collects
`src/` and builds the addon as a static library.

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
ArtnetSender& clear(universe);                              // zero, keep sending
ArtnetSender& clearAll();                                   // blackout (all kept)
uint8_t getChannel(universe, channel) const;

// Active universe set
ArtnetSender& removeUniverse(universe);     // stop sending one universe, free it
ArtnetSender& removeAllUniverses();         // stop sending everything
size_t getUniverseCount() const;
std::vector<int> getUniverses() const;      // active universe numbers, ascending

// Send
bool send();                  // send every active universe once (ArtDmx only)
bool sendUniverse(universe);  // send one universe once
bool sendSync();              // emit one ArtSync (latch all universes at once)

// Background auto-refresh
void startAutoSend(fps = 30);        // ArtDmx only
void startAutoSendSynced(fps = 30);  // ArtDmx + ArtSync each tick
void stopAutoSend();                 // floored to >=1 Hz, clamped to <=44 Hz; re-tunes if running
bool isAutoSending() const;

// Limits
ArtnetSender& setMaxUniverses(n);   // cap on distinct universes (default 2048)
size_t getMaxUniverses() const;
```

### Notes

- **Channels are 1-based.** Channel 1 is the first DMX slot — same numbering as
  every lighting desk and fixture manual.
- **Universes** use the 15-bit Art-Net port address (`Net` + `SubUni`); pass the
  plain universe number `0..32767`. Out-of-range values are rejected with a warning.
- **ArtSync (multi-universe sync):** spanning a fixture/wall across several
  universes? Sending each `ArtDmx` separately lets nodes output them at slightly
  different times (tearing). `sendSync()` (or `startAutoSendSynced()`) emits an
  `ArtSync` after the `ArtDmx` batch so every node latches the whole frame at
  once. Sync is **always explicit** — `send()` never appends it. Caveat: once a
  node receives `ArtSync` it enters synchronous mode and **waits** for the next
  one; if you stop sending `ArtSync` for ~4 s it reverts to immediate output. So
  pair sync with a per-frame loop — i.e. `send(); sendSync();` every frame, or
  just `startAutoSendSynced()`. (Irrelevant for a single universe.)
- **`clear` vs `remove`:** `clear(u)` zeros a universe but keeps sending it (a
  blackout that still refreshes the DMX link); `removeUniverse(u)` drops it from
  the active set entirely so it's no longer transmitted.
- **Range-safe writes:** if a `setChannels` / `setColor` block doesn't fully fit
  in 1..512, nothing is written (and you get a warning) — a partial write would
  silently misalign the data.
- **Universe cap:** a sender holds at most `getMaxUniverses()` distinct universes
  (default **2048**). New universes past the cap are rejected with a warning;
  existing ones keep working. This bounds the per-frame send count so an
  attacker-influenced universe index can't grow the active set toward 32768 and
  saturate the send loop. Raise it with `setMaxUniverses()` for huge LED rigs.
- Each universe always goes out as a full **512-byte** frame for broad node
  compatibility.
- The auto-send thread and your `update()` thread share the channel buffers under
  a mutex, so it's safe to set channels from `update()` while auto-send runs.

## Receiving

`ArtnetReceiver` binds a port and parses incoming ArtDmx into per-universe state.
Like the sender, it's **state-oriented** (DMX is a 512-channel framebuffer, not a
message stream), so polling reads the current value — there's no queue to drain.

```cpp
#include <tcxArtnet.h>
using namespace tc;
using namespace tcx;

ArtnetReceiver artnet_;

void setup() override {
    artnet_.setup();                      // bind 6454, start receive thread
    // async, lowest latency (fires on the receive thread — guard shared state):
    artnet_.onDmx.listen([](DmxFrame& f) {
        // f.universe, f.sequence, f.length, f.data (512 bytes)
    });
}

void update() override {
    // or poll the latest state on the main thread, anytime:
    uint8_t r = artnet_.getChannel(0, 1);   // universe 0, channel 1 (1-based)
    if (artnet_.hasNewData()) { /* something changed since last frame */ }
}
```

```cpp
bool setup(port = 6454);   void close();   bool isListening() const;
tc::Event<DmxFrame> onDmx; // each received ArtDmx (state already updated)
tc::Event<int>      onSync; // an ArtSync arrived (reserved arg)

uint8_t getChannel(universe, channel) const;   // 1-based, 0 if unseen
std::vector<uint8_t> getDmx(universe) const;    // latest 512, empty if unseen
std::vector<int> getUniverses() const;          // universes seen
bool hasUniverse(universe) const;
bool hasNewData();   // any universe updated since the last call (clears on read)
```

- A short ArtDmx (length < 512) updates only the channels it carries; the rest of
  the universe keeps its previous value. `getChannel` / `getDmx` always expose a
  full 512.
- **ArtSync is applied immediately**: each ArtDmx updates the visible state the
  moment it arrives. `onSync` just notifies you one was received (use it if you
  want to read all universes together as a coherent frame); there is no
  receive-side sync staging.

## Discovery (ArtPoll)

`ArtnetNode` bundles a sender + receiver and adds ArtPoll. The sender and receiver
stay single-responsibility (one direction each); discovery just spans both, so the
node wires them. Reach the underlying objects with `sender()` / `receiver()` for
normal DMX I/O.

```cpp
ArtnetNode node;
node.setup();                 // broadcast send + bind 6454

// --- as a controller: find nodes on the network ---
node.sendPoll();              // broadcast ArtPoll (every ~3s to stay fresh)
node.receiver().onNode.listen([](ArtnetNodeInfo& n) {
    // n.ip, n.shortName, n.longName, n.universes, n.oem, n.esta
});
for (auto& n : node.getNodes()) { /* polling: the discovered set so far */ }

// --- as a discoverable node: answer other controllers' ArtPoll ---
node.setShortName("Stage Left").setLongName("TrussC rig").setUniverses({0, 1});
node.enablePollReply(true);   // opt-in; off by default
```

- **Discovery** = `sendPoll()` (outbound) + `getNodes()` / `onNode` (inbound). Set
  a broadcast destination (the default) so all nodes hear the poll.
- **Being discoverable** is **opt-in** (`enablePollReply(true)`) — a plain receiver
  isn't obliged to announce itself. When on, the node answers each ArtPoll with an
  ArtPollReply built from the identity you set (`setShortName` / `setLongName` /
  `setUniverses` / `setVendor` (ESTA) / `setOem`). IP / MAC / version are auto.
- A node serving several universes sends one reply per universe; the receiver
  **merges** them per source IP, so `getNodes()` shows the full universe set.

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

## Tests

`testApp/` is a headless console test: it sends an ArtDmx frame to loopback and
asserts the packet format, plus the channel/universe edge cases and the universe
cap. CI builds it on macOS / Windows / Linux and runs it (`test_mode: test`), so
a non-zero exit fails the build. Run it locally with `trusscli run -p testApp`.

## License

MIT — see [LICENSES.md](LICENSES.md). "Art-Net™" is a trademark of Artistic
Licence Engineering Ltd; the protocol is royalty-free and implemented here
independently.
