#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(820, 560);
    settings.setTitle("tcxArtnet Receiver");

    return TC_RUN_APP(tcApp, settings);
}
