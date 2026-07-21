#include "DrumDeviceBody.h"

#include <initializer_list>
#include <utility>

DrumDeviceBody::DrumDeviceBody(fui::DrumUiModel& model)
    : pads(model), padStrip(model), oscA(model, 0), oscB(model, 1), noise(model),
      pitchEnv(model), ampEnv(model), filter(model), mod(model), selBar(model),
      stepSeq(model), fxRack(model) {
    for (auto* component : std::initializer_list<juce::Component*>{
             &pads, &padStrip, &oscA, &oscB, &noise, &pitchEnv, &ampEnv,
             &filter, &mod, &selBar, &stepSeq, &fxRack })
        addAndMakeVisible(*component);
}

// Row heights are fixed; the column table is derived from the body's own
// width. The standalone rack is 1460 wide and reproduces the original layout
// exactly, while SQ-4 hosts the same body on a wider canvas
// (DeviceFocusView::kDrumWidth) because its focus slot is much wider relative
// to its height -- at 1460 the body was height-bound and letterboxed with
// ~80px of dead space down each side (DeviceFocusView::layoutBody scales
// uniformly). Widening the canvas rather than compressing the rows keeps
// every panel's internal layout untouched.
void DrumDeviceBody::resized() {
    constexpr int gap = 9, rightX = 379, baseRight = 1063;
    const int w = getWidth() > 0 ? getWidth() : 1460;
    const int rightW = juce::jmax(baseRight / 2, w - 18 - rightX);
    const int fullW = juce::jmax(1, w - 36);

    // Spread a row of panels across the right column, scaling each panel's
    // base width by the column's actual width. The last panel closes the row
    // so rounding can never leave a seam at the right margin.
    auto layRow = [&](std::initializer_list<std::pair<juce::Component*, int>> items,
                      int y, int h) {
        const int n = (int)items.size();
        const int gaps = gap * (n - 1);
        const int avail = rightW - gaps, baseAvail = baseRight - gaps;
        int x = rightX, i = 0;
        for (auto& [component, baseW] : items) {
            const int cw = (++i == n) ? (rightX + rightW - x)
                                      : juce::roundToInt((double)baseW * avail / baseAvail);
            component->setBounds(x, y, cw, h);
            x += cw + gap;
        }
    };

    pads.setBounds(18, 103, 352, 369);
    padStrip.setBounds(18, 481, 352, 119);
    selBar.setBounds(rightX, 103, rightW, 31);
    layRow({ { &oscA, 424 }, { &oscB, 425 }, { &noise, 196 } }, 143, 243);
    layRow({ { &pitchEnv, 225 }, { &ampEnv, 259 }, { &filter, 259 }, { &mod, 293 } }, 395, 209);
    fxRack.setBounds(18, 613, fullW, 131);
    stepSeq.setBounds(18, 753, fullW, 399);   // 16 lanes x 21px + head + padding
}
