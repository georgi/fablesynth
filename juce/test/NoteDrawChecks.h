#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Exercise real mouse routing, including preview-only edits and cancellation.
template <typename View, typename Processor>
bool checkNoteDrawing(View& view, Processor& proc) {
    using Step = decltype(proc.getSeqStep(0, 0));
    std::array<Step, 16> saved;
    for (int i = 0; i < 16; ++i) {
        saved[i] = proc.getSeqStep(0, i);
        proc.setSeqStep(0, i, Step{});
    }
    view.clearSelection();
    const auto start = view.cellBounds(2, 5).getCentre().toFloat();
    auto event = [&](juce::Point<float> pos) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),
            pos, juce::ModifierKeys::leftButtonModifier, 1, 0, 0, 0, 0,
            &view, &view, juce::Time::getCurrentTime(), start,
            juce::Time::getCurrentTime(), 1, pos != start);
    };
    view.mouseDown(event(start));
    view.mouseDrag(event(view.cellBounds(7, 8).getCentre().toFloat()));
    bool ok = !proc.getSeqStep(0, 2).on; // preview has not changed the score
    view.mouseUp(event(view.cellBounds(7, 8).getCentre().toFloat()));
    auto note = proc.getSeqStep(0, 2);
    ok &= note.on && note.note == 5 && note.duration == 6;
    for (int i = 3; i <= 7; ++i) ok &= !proc.getSeqStep(0, i).on;
    view.undoEdit();
    ok &= !proc.getSeqStep(0, 2).on;
    view.redoEdit();
    ok &= proc.getSeqStep(0, 2).duration == 6;
    view.undoEdit();
    view.mouseDown(event(start));
    view.mouseDrag(event(view.cellBounds(9, 5).getCentre().toFloat()));
    view.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    view.mouseUp(event(view.cellBounds(9, 5).getCentre().toFloat()));
    ok &= !proc.getSeqStep(0, 2).on;
    view.mouseDown(event(start));
    view.mouseUp(event(start));
    ok &= proc.getSeqStep(0, 2).on && proc.getSeqStep(0, 2).duration == 1;
    view.undoEdit();
    view.mouseDown(event(start));
    view.mouseUp(event({ view.cellBounds(15, 5).getRight() + 100.0f, start.y }));
    ok &= proc.getSeqStep(0, 2).duration == 14;
    view.undoEdit();
    for (int i = 0; i < 16; ++i) proc.setSeqStep(0, i, saved[i]);
    return ok;
}
