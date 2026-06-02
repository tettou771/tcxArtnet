// =============================================================================
// tcxArtnet testApp - headless behavioral test (no window).
//
// Built and run by CI (2bbb/trussc-actions build-addon.yml, test_mode: "test"):
// exit 0 = all pass, non-zero = failure -> CI fails. Console only, so it runs on
// headless macOS / Windows / Linux runners. The receiver uses TrussC's own
// tc::UdpSocket (not raw POSIX sockets) so it compiles on Windows too.
// =============================================================================

#include <tcxArtnet.h>

#include <cstdio>
#include <cstring>
#include <vector>

using namespace tcx;

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

    std::printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
