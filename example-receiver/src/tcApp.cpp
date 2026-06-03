#include "tcApp.h"

void tcApp::setup() {
    // Bind 6454 (receive) + default broadcast destination (for sendPoll).
    node_.setup();

    // Identify ourselves so a controller (e.g. QLC+) can discover this app when
    // it polls. Opt-in; harmless if nothing polls us.
    node_.setShortName("TrussC-rx").setLongName("tcxArtnet receiver example").setUniverses({universe_});
    node_.enablePollReply(true);

    logNotice("artnet") << "listening on port " << node_.receiver().getPort();
}

void tcApp::update() {}

void tcApp::draw() {
    clear(0.10f);

    auto& rx = node_.receiver();
    auto unis = rx.getUniverses();

    setColor(1.0f);
    drawBitmapString("tcxArtnet receiver   port " + to_string(rx.getPort()) +
                     "   active universes: " + to_string(unis.size()), 20, 24);
    drawBitmapString("[SPACE] send ArtPoll (discover nodes)   [<] [>] change universe", 20, 42);

    if (unis.empty()) {
        setColor(0.6f);
        drawBitmapString("waiting for Art-Net DMX on universe " + to_string(universe_) +
                         " ... point a controller / QLC+ at this machine.", 20, 80);
    } else {
        int u = rx.hasUniverse(universe_) ? universe_ : unis.front();
        setColor(0.85f);
        drawBitmapString("universe " + to_string(u) + "  (channels 1-64)", 20, 80);

        auto dmx = rx.getDmx(u);
        const int n = 64;
        const float x0 = 20, y0 = 100, barW = 11, gap = 1.5f, maxH = 380;
        for (int i = 0; i < n && i < (int)dmx.size(); ++i) {
            float v = dmx[i] / 255.0f;
            float x = x0 + i * (barW + gap);
            setColor(0.18f, 0.18f, 0.22f);          // track
            drawRect(x, y0, barW, maxH);
            setColor(v, 1.0f - v * 0.4f, 0.25f);    // value bar
            drawRect(x, y0 + (maxH - v * maxH), barW, v * maxH);
        }
        // a few channel values as text
        setColor(0.7f);
        drawBitmapString("ch1-8: " +
            to_string(dmx[0]) + " " + to_string(dmx[1]) + " " + to_string(dmx[2]) + " " +
            to_string(dmx[3]) + " " + to_string(dmx[4]) + " " + to_string(dmx[5]) + " " +
            to_string(dmx[6]) + " " + to_string(dmx[7]), 20, 500);
    }

    // discovered nodes (from ArtPollReply, after [SPACE])
    auto nodes = node_.getNodes();
    setColor(0.55f);
    drawBitmapString("discovered nodes: " + to_string(nodes.size()), 20, 524);
    float ny = 542;
    for (auto& nd : nodes) {
        string us;
        for (int u : nd.universes) us += to_string(u) + " ";
        drawBitmapString("  " + nd.ip + "  \"" + nd.shortName + "\"  uni: " + us, 20, ny);
        ny += 16;
    }
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        node_.clearNodes();
        node_.sendPoll();
    } else if (key == ',' || key == '<') {
        if (universe_ > 0) universe_--;
    } else if (key == '.' || key == '>') {
        universe_++;
    }
}
