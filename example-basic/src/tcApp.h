#pragma once

#include <TrussC.h>
#include <tcxArtnet.h>

using namespace tc;
using namespace tcx;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    ArtnetSender artnet_;
    int universe_ = 0;       // Art-Net universe to drive
    int startChannel_ = 1;   // RGB fixture base address (R, R+1=G, R+2=B)
};
