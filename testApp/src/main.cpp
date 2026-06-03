// =============================================================================
// tcxArtnet testApp - headless behavioral test (no window).
//
// Built and run by CI (2bbb/trussc-actions build-addon.yml, test_mode: "test"):
// exit 0 = all pass, non-zero = failure -> CI fails. Console only, so it runs on
// headless macOS / Windows / Linux runners. The receiver uses TrussC's own
// tc::UdpSocket (not raw POSIX sockets) so it compiles on Windows too.
// =============================================================================

#include <tcxArtnet.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

using namespace tcx;

static void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

static int g_pass = 0, g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-52s %s\n", name, ok ? "PASS" : "FAIL");
    ok ? ++g_pass : ++g_fail;
}

int main() {
    // ----- packet format: send one ArtDmx to loopback and validate it -----
    tc::UdpSocket rx;
    rx.create();
    rx.setReuseAddress(true);
    bool bound = rx.bind(ARTNET_PORT, /*startReceiving=*/false);
    rx.setReceiveTimeout(200);
    check("receiver bound to 6454", bound);

    ArtnetSender tx;
    tx.setup("127.0.0.1");
    tx.setColor(0, 1, tc::Color(1.0f, 0.5f, 0.0f));  // R=255 G=128 B=0
    tx.setChannel(0, 10, 200);

    unsigned char buf[600];
    int n = -1;
    // Loopback is reliable, but retry a few times to absorb any scheduling jitter.
    for (int attempt = 0; attempt < 20 && n <= 0; ++attempt) {
        tx.send();
        n = rx.receive(buf, sizeof(buf));
    }
    check("received an ArtDmx packet", n > 0);
    if (n > 0) {
        check("packet size 530 (18 hdr + 512 dmx)", n == 18 + 512);
        check("id 'Art-Net\\0'", std::memcmp(buf, "Art-Net", 7) == 0 && buf[7] == 0);
        check("opcode 0x5000 little-endian", buf[8] == 0x00 && buf[9] == 0x50);
        check("protocol version 0.14", buf[10] == 0 && buf[11] == 14);
        check("length field == 512", ((buf[16] << 8) | buf[17]) == 512);
        check("ch1 R == 255", buf[18 + 0] == 255);
        check("ch2 G ~= 128", buf[18 + 1] >= 126 && buf[18 + 1] <= 130);
        check("ch3 B == 0", buf[18 + 2] == 0);
        check("ch10 == 200", buf[18 + 9] == 200);
    }
    // ----- ArtSync: send() is ArtDmx-only; sendSync() emits a 14-byte ArtSync -----
    // Drain whatever the retry loop above already sent, then send one clean frame.
    while (rx.receive(buf, sizeof(buf)) > 0) {}
    tx.send();       // ArtDmx (universe 0)
    tx.sendSync();   // ArtSync
    // Collect the two packets and classify them by opcode.
    int dmxCount = 0, syncCount = 0, syncLen = 0;
    for (int got = 0; got < 2; ++got) {
        int m = rx.receive(buf, sizeof(buf));
        if (m <= 0) break;
        uint16_t op = static_cast<uint16_t>(buf[8] | (buf[9] << 8));  // little-endian
        if (op == ARTNET_OPCODE_DMX) ++dmxCount;
        else if (op == ARTNET_OPCODE_SYNC) { ++syncCount; syncLen = m; }
    }
    check("send()+sendSync() -> 1 ArtDmx + 1 ArtSync", dmxCount == 1 && syncCount == 1);
    check("ArtSync packet is 14 bytes", syncLen == 14);
    if (syncCount == 1) {
        // Re-send sync alone to inspect its bytes deterministically.
        while (rx.receive(buf, sizeof(buf)) > 0) {}
        tx.sendSync();
        int m = rx.receive(buf, sizeof(buf));
        check("ArtSync id + opcode 0x5200 + protver",
              m == 14 && std::memcmp(buf, "Art-Net", 7) == 0 && buf[7] == 0 &&
              buf[8] == 0x00 && buf[9] == 0x52 && buf[10] == 0 && buf[11] == 14);
    }
    rx.close();

    // ----- state / edge cases (no network) -----
    ArtnetSender st;

    check("default max universes == 2048", st.getMaxUniverses() == 2048);

    // sparse universes: 0 and 2 only (not 1)
    st.setColor(0, 1, tc::Color(1, 0, 0));
    st.setColor(2, 1, tc::Color(0, 0, 1));
    check("sparse universes count == 2", st.getUniverseCount() == 2);
    check("getUniverses() == {0,2}", st.getUniverses() == std::vector<int>({0, 2}));

    // universe out of range: rejected, no entry created
    size_t before = st.getUniverseCount();
    st.setChannel(-1, 1, 255);
    st.setChannel(32768, 1, 255);
    check("invalid universe creates no entry", st.getUniverseCount() == before);

    // channel out of range: ignored, existing value untouched
    st.setChannel(0, 0, 9).setChannel(0, 513, 9).setChannel(0, -5, 9);
    check("channel out of range leaves ch1 R == 255", st.getChannel(0, 1) == 255);

    // setColor at the tail: 511..513 doesn't fit -> nothing written
    st.setChannel(0, 511, 7).setChannel(0, 512, 9);
    st.setColor(0, 511, tc::Color(1, 1, 1));
    check("setColor tail no-op keeps 511=7,512=9",
          st.getChannel(0, 511) == 7 && st.getChannel(0, 512) == 9);
    // 510..512 fits exactly
    st.setColor(0, 510, tc::Color(1, 1, 1));
    check("setColor at 510 writes 510..512 = 255",
          st.getChannel(0, 510) == 255 && st.getChannel(0, 511) == 255 && st.getChannel(0, 512) == 255);

    // setChannels overflow -> whole block rejected
    st.setChannels(0, 511, {1, 2, 3, 4});  // 511..514
    check("setChannels overflow writes nothing", st.getChannel(0, 511) == 255);
    st.setChannels(0, 5, {11, 22, 33});  // 5..7 fits
    check("setChannels in-range writes 5,6,7",
          st.getChannel(0, 5) == 11 && st.getChannel(0, 6) == 22 && st.getChannel(0, 7) == 33);

    // remove vs clear
    st.removeUniverse(2);
    check("removeUniverse(2) -> count 1, {0}",
          st.getUniverseCount() == 1 && st.getUniverses() == std::vector<int>({0}));
    st.clear(0);
    check("clear(0) zeros but keeps active",
          st.getUniverseCount() == 1 && st.getChannel(0, 1) == 0);
    st.removeAllUniverses();
    check("removeAllUniverses -> count 0", st.getUniverseCount() == 0);

    // universe cap
    ArtnetSender cap;
    cap.setMaxUniverses(3);
    for (int u = 0; u < 5; ++u) cap.setChannel(u, 1, 100);  // u=3,4 rejected
    check("cap blocks new universes beyond 3", cap.getUniverseCount() == 3);
    cap.setChannel(1, 5, 77);  // existing universe still writable at cap
    check("existing universe writable at cap", cap.getChannel(1, 5) == 77);
    cap.setMaxUniverses(0);  // floored to 1
    check("setMaxUniverses(0) floored to 1", cap.getMaxUniverses() == 1);

    // auto-send floor: start at 0 Hz (-> 1 Hz) then stop must not hang
    cap.setup("127.0.0.1");
    cap.startAutoSend(0);
    check("startAutoSend(0) -> running", cap.isAutoSending());
    cap.stopAutoSend();
    check("stopAutoSend returns (no hang)", !cap.isAutoSending());

    // synced variant + retune: re-calling either variant while running must keep
    // a single thread (no second spawn) and stop cleanly.
    cap.startAutoSendSynced(20);
    check("startAutoSendSynced -> running", cap.isAutoSending());
    cap.startAutoSend(10);  // retune same thread, switch sync off
    check("re-call while running stays running (single thread)", cap.isAutoSending());
    cap.stopAutoSend();
    check("stop after synced + retune returns", !cap.isAutoSending());

    // ----- Sender <-> Receiver loopback round-trip (no hardware) -----
    ArtnetReceiver rrx;
    bool rok = rrx.setup(ARTNET_PORT);
    check("receiver setup + listening", rok && rrx.isListening());

    std::atomic<int> dmxEvents{0}, syncEvents{0};
    std::atomic<int> lastUni{-1};
    auto lDmx  = rrx.onDmx.listen([&](DmxFrame& f) { dmxEvents++; lastUni.store(f.universe); });
    auto lSync = rrx.onSync.listen([&](int&) { syncEvents++; });

    ArtnetSender rtx;
    rtx.setup("127.0.0.1");
    rtx.setColor(5, 1, tc::Color(0, 1, 0));  // uni5 ch1=0 ch2=255 ch3=0
    rtx.setChannel(5, 100, 222);

    // Loopback is reliable; retry briefly to absorb scheduling jitter.
    for (int i = 0; i < 25 && dmxEvents.load() == 0; ++i) { rtx.send(); sleepMs(20); }
    sleepMs(50);
    check("receiver onDmx fired (universe 5)", dmxEvents.load() > 0 && lastUni.load() == 5);
    check("receiver state matches sent values",
          rrx.getChannel(5, 1) == 0 && rrx.getChannel(5, 2) == 255 &&
          rrx.getChannel(5, 3) == 0 && rrx.getChannel(5, 100) == 222);
    check("getDmx(5) is 512 bytes", rrx.getDmx(5).size() == 512);
    check("hasUniverse(5), not 6", rrx.hasUniverse(5) && !rrx.hasUniverse(6));
    check("getUniverses() == {5}", rrx.getUniverses() == std::vector<int>({5}));
    check("getChannel out-of-range/unseen -> 0",
          rrx.getChannel(5, 0) == 0 && rrx.getChannel(5, 513) == 0 && rrx.getChannel(9, 1) == 0);

    // hasNewData() latches then clears.
    bool firstNew = rrx.hasNewData();
    bool secondNew = rrx.hasNewData();
    check("hasNewData true once then clears", firstNew && !secondNew);

    // onSync fires on ArtSync.
    rtx.sendSync();
    for (int i = 0; i < 25 && syncEvents.load() == 0; ++i) { rtx.sendSync(); sleepMs(20); }
    sleepMs(50);
    check("receiver onSync fired", syncEvents.load() > 0);

    lDmx.disconnect();
    lSync.disconnect();
    rrx.close();
    check("getDmx after close is empty", rrx.getDmx(5).empty());

    std::printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
