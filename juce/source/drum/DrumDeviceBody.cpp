#include "DrumDeviceBody.h"

DrumDeviceBody::DrumDeviceBody(fui::DrumUiModel& model)
    : pads(model), padStrip(model), oscA(model, 0), oscB(model, 1), noise(model),
      pitchEnv(model), ampEnv(model), filter(model), mod(model), selBar(model),
      stepSeq(model), fxRack(model) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &pads, &padStrip, &oscA, &oscB, &noise, &pitchEnv, &ampEnv,
             &filter, &mod, &selBar, &stepSeq, &fxRack })
        addAndMakeVisible(*component);
}

void DrumDeviceBody::resized() {
    pads.setBounds(18, 103, 352, 369);
    padStrip.setBounds(18, 481, 352, 119);
    selBar.setBounds(379, 103, 1063, 31);
    oscA.setBounds(379, 143, 424, 243);
    oscB.setBounds(812, 143, 425, 243);
    noise.setBounds(1246, 143, 196, 243);
    pitchEnv.setBounds(379, 395, 225, 209);
    ampEnv.setBounds(613, 395, 259, 209);
    filter.setBounds(881, 395, 259, 209);
    mod.setBounds(1149, 395, 293, 209);
    fxRack.setBounds(18, 613, 1424, 131);
    stepSeq.setBounds(18, 753, 1424, 105);
}
