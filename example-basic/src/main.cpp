#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(800, 600);
    settings.setTitle("tcxArtnet Example");

    return TC_RUN_APP(tcApp, settings);
}
