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
    ArtnetNode node_;        // receiver + sender, so we can also poll for nodes
    int universe_ = 0;       // which universe to visualise
};
