#include "tcApp.h"

void tcApp::setup() {
    // Default destination is the Art-Net broadcast address (2.255.255.255:6454).
    // For a specific node, use e.g. artnet_.setup("192.168.1.50");
    artnet_.setup();

    // Keep refreshing the output ~30 times/sec on a background thread, so the
    // fixture holds its value even when nothing changes (proper DMX behaviour).
    artnet_.startAutoSend(30.0f);

    for (auto& d : artnet_.getDestinations())
        logNotice("artnet") << "sending to " << d.host << ":" << d.port;
}

void tcApp::update() {
    // Sweep an RGB fixture through the hue wheel on channels 1-3 of universe 0.
    float hue = fmodf(getElapsedTimef() * 0.1f, 1.0f);
    Color c = ColorHSB(hue, 1.0f, 1.0f).toRGB();
    artnet_.setColor(universe_, startChannel_, c);
}

void tcApp::draw() {
    clear(0.12f);

    setColor(1.0f);
    drawBitmapString("tcxArtnet - basic   [SPACE] blackout   [A] toggle auto-send", 20, 28);

    string dst = artnet_.isConnected() ? artnet_.getDestinations().front().host : string("(none)");
    drawBitmapString("dest      : " + dst + ":" + to_string(ARTNET_PORT), 20, 56);
    drawBitmapString("universe  : " + to_string(universe_), 20, 74);
    drawBitmapString("auto-send : " + string(artnet_.isAutoSending() ? "on (~30Hz)" : "off"), 20, 92);

    // Show the live RGB channel values + a swatch.
    int r = artnet_.getChannel(universe_, startChannel_);
    int g = artnet_.getChannel(universe_, startChannel_ + 1);
    int b = artnet_.getChannel(universe_, startChannel_ + 2);
    drawBitmapString("ch" + to_string(startChannel_) + "-" + to_string(startChannel_ + 2) +
                     " (RGB): " + to_string(r) + ", " + to_string(g) + ", " + to_string(b), 20, 120);

    setColor(r / 255.0f, g / 255.0f, b / 255.0f);
    drawRect(20, 140, 200, 200);
}

void tcApp::keyPressed(int key) {
    if (key == ' ') {
        artnet_.clearAll();  // blackout (auto-send keeps pushing the zeros out)
    } else if (key == 'a' || key == 'A') {
        if (artnet_.isAutoSending()) artnet_.stopAutoSend();
        else artnet_.startAutoSend(30.0f);
    }
}
