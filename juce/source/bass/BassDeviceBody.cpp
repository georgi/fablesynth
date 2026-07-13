#include "BassDeviceBody.h"

BassDeviceBody::BassDeviceBody(fui::BassUiModel& model)
    : osc(model), sub(model), filter(model), env(model), lfo(model), accent(model),
      keys(model), seq(model), fxRack(model) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &osc, &sub, &filter, &env, &lfo, &accent, &keys, &seq, &fxRack })
        addAndMakeVisible(*component);
}

void BassDeviceBody::resized() {
    osc.setBounds(18, 103, 464, 243);
    sub.setBounds(491, 103, 192, 243);
    filter.setBounds(692, 103, 355, 243);
    env.setBounds(1056, 103, 386, 243);
    lfo.setBounds(18, 355, 290, 140);
    accent.setBounds(317, 355, 250, 140);
    keys.setBounds(576, 355, 866, 140);
    seq.setBounds(18, 504, 1424, 276);
    fxRack.setBounds(18, 789, 1424, 120);
}
