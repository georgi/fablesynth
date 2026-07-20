// Headless verification harness for StepEditOps.h (JUCE-free) — mirrors
// src/shared/seqEdit.test.ts. Exercises copyRange/pasteRange/clearRange/
// shiftRange/copyPattern/pastePattern over both the WT-1/BL-1 note layout
// (source/dsp/NoteSeq.h) and the DR-1 pad-row layout (DrumParams.h's
// DR_NPADS/DR_NPATTERNS/DR_STEPS), plus the bounded StepEditHistory template.
//
// Exits non-zero if any check fails.
#include "../source/ui/StepEditOps.h"
#include "../source/dsp/NoteSeq.h"
#include "../source/drum/dsp/DrumParams.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace fable;

static int g_fail = 0;
static void check(bool cond, const std::string& name, const std::string& detail = "") {
    printf("  [%s] %s%s\n", cond ? "PASS" : "FAIL", name.c_str(),
           detail.empty() ? "" : ("  -> " + detail).c_str());
    if (!cond) g_fail++;
}

// ---- WT-1/BL-1 layout: stride 3, 16 steps, 48 bytes/pattern ----
static const StepLayout kNoteLayout { SEQ_STEP_STRIDE, SEQ_STEPS, SEQ_STEPS * SEQ_STEP_STRIDE, 0 };
static StepBytes emptyStepBytes() {
    NoteSeqStep s; // duration 1, oct 0, off
    StepBytes b(SEQ_STEP_STRIDE, 0);
    setNoteSeqStep(b.data(), 0, 0, s);
    return b;
}

// ---- DR-1 layout: a pad's row is a lane, stride 1, whole patterns span all pads ----
static StepLayout drumLane(int padI) {
    StepLayout l;
    l.stride = 1;
    l.stepsPerPattern = DR_STEPS;
    l.patternSize = DR_NPADS * DR_STEPS;
    l.laneOffset = padI * DR_STEPS;
    return l;
}
static int drumIdx(int pat, int pad, int step) { return (pat * DR_NPADS + pad) * DR_STEPS + step; }

int main() {
    printf("=== StepEditOps headless test ===\n\n");

    // -- copyRange / pasteRange (note layout) --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 7; a.acc = true; a.duration = 3;
        setNoteSeqStep(p.data(), 0, 4, a);
        NoteSeqStep b; b.on = true; b.note = 2; b.oct = 1;
        setNoteSeqStep(p.data(), 0, 5, b);

        StepBytes data = copyRange(p, kNoteLayout, 0, 4, 5);
        check(data.size() == 2 * (size_t)SEQ_STEP_STRIDE, "copyRange returns 2 steps' worth of bytes");

        StepBytes next = pasteRange(p, kNoteLayout, 1, 10, data);
        check(next != p, "pasteRange returns a new buffer");
        NoteSeqStep r10 = getNoteSeqStep(next.data(), 1, 10);
        check(r10.on && r10.note == 7 && r10.acc && r10.duration == 3, "pasted step 10 matches source step 4");
        NoteSeqStep r11 = getNoteSeqStep(next.data(), 1, 11);
        check(r11.on && r11.note == 2 && r11.oct == 1, "pasted step 11 matches source step 5");
        NoteSeqStep src4 = getNoteSeqStep(next.data(), 0, 4);
        check(src4.on && src4.note == 7, "source pattern untouched by paste into another pattern");
        NoteSeqStep origDest = getNoteSeqStep(p.data(), 1, 10);
        check(!origDest.on, "input buffer never mutated by pasteRange");
    }

    // -- copyRange normalizes reversed ranges and clamps out-of-bounds steps --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep s; s.on = true; s.note = 9;
        setNoteSeqStep(p.data(), 0, 15, s);

        StepBytes reversed = copyRange(p, kNoteLayout, 0, 15, 12);
        check(reversed.size() == 4 * (size_t)SEQ_STEP_STRIDE, "copyRange normalizes a reversed [15,12] to 4 steps");

        StepBytes clamped = copyRange(p, kNoteLayout, 0, 14, 99);
        check(clamped.size() == 2 * (size_t)SEQ_STEP_STRIDE, "copyRange clamps an out-of-range hi to pattern end");

        StepBytes neg = copyRange(p, kNoteLayout, 0, -5, 0);
        check(neg.size() == 1 * (size_t)SEQ_STEP_STRIDE, "copyRange clamps a negative lo to step 0");
    }

    // -- pasteRange clamps at the pattern end (drops overflow, never wraps) --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep n1; n1.on = true; n1.note = 1; setNoteSeqStep(p.data(), 0, 0, n1);
        NoteSeqStep n2; n2.on = true; n2.note = 2; setNoteSeqStep(p.data(), 0, 1, n2);
        NoteSeqStep n3; n3.on = true; n3.note = 3; setNoteSeqStep(p.data(), 0, 2, n3);

        StepBytes data = copyRange(p, kNoteLayout, 0, 0, 2);
        StepBytes next = pasteRange(p, kNoteLayout, 1, 14, data);
        check(getNoteSeqStep(next.data(), 1, 14).note == 1, "overflow paste: step 14 = source step 0");
        check(getNoteSeqStep(next.data(), 1, 15).note == 2, "overflow paste: step 15 = source step 1");
        check(!getNoteSeqStep(next.data(), 2, 0).on, "overflow paste drops the third step (no wrap into pattern 2)");
    }

    // -- CRITICAL: pasteRange anchor clamping / past-end no-op semantics --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep s; s.on = true; s.note = 5; setNoteSeqStep(p.data(), 0, 3, s);
        StepBytes data = copyRange(p, kNoteLayout, 0, 3, 3);

        StepBytes negAnchor = pasteRange(p, kNoteLayout, 1, -4, data);
        check(getNoteSeqStep(negAnchor.data(), 1, 0).note == 5, "pasteRange clamps a negative anchor to step 0");

        // Anchor AT the pattern end (== stepsPerPattern): must be a total no-op,
        // never clamped back onto the last valid step.
        StepBytes droppedAtEnd = pasteRange(p, kNoteLayout, 1, SEQ_STEPS, data);
        check(droppedAtEnd == p, "pasteRange at exactly the pattern end is a no-op (anchor never clamped back in)");

        // Anchor well past the end: same no-op guarantee.
        StepBytes droppedFar = pasteRange(p, kNoteLayout, 1, 99, data);
        check(droppedFar == p, "pasteRange far past the pattern end is a no-op");
    }

    // -- clearRange --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 4; a.acc = true; a.duration = 5; a.oct = 1;
        setNoteSeqStep(p.data(), 0, 2, a);
        NoteSeqStep b; b.on = true; b.note = 6; setNoteSeqStep(p.data(), 0, 3, b);
        NoteSeqStep c; c.on = true; c.note = 8; setNoteSeqStep(p.data(), 0, 4, c);

        StepBytes empty = emptyStepBytes();
        StepBytes next = clearRange(p, kNoteLayout, 0, 2, 3, empty);
        check(next != p, "clearRange returns a new buffer");
        NoteSeqStep r2 = getNoteSeqStep(next.data(), 0, 2);
        check(!r2.on && !r2.acc && r2.note == 0 && r2.oct == 0 && r2.duration == 1,
              "clearRange restores the empty-step template");
        check(!getNoteSeqStep(next.data(), 0, 3).on, "clearRange clears step 3 too (inclusive range)");
        check(getNoteSeqStep(next.data(), 0, 4).on && getNoteSeqStep(next.data(), 0, 4).note == 8,
              "clearRange leaves the neighbor step 4 untouched");
        check(getNoteSeqStep(p.data(), 0, 2).on, "clearRange does not mutate its input");
    }

    // -- clearRange defaults to all-zero bytes (DR-1 lane) --
    {
        StepBytes p(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        p[(size_t)drumIdx(0, 3, 5)] = 2;
        p[(size_t)drumIdx(0, 3, 6)] = 1;
        StepBytes next = clearRange(p, drumLane(3), 0, 5, 6);
        check(next[(size_t)drumIdx(0, 3, 5)] == 0, "clearRange default-zeros a DR-1 lane step (hit)");
        check(next[(size_t)drumIdx(0, 3, 6)] == 0, "clearRange default-zeros a DR-1 lane step (accent)");
        check(p[(size_t)drumIdx(0, 3, 5)] == 2, "clearRange does not mutate the DR-1 input buffer");
    }

    // -- shiftRange: move clears the vacated source with the template --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 3; setNoteSeqStep(p.data(), 0, 0, a);
        NoteSeqStep b; b.on = true; b.note = 4; setNoteSeqStep(p.data(), 0, 1, b);

        ShiftOpts opts; opts.emptyStep = emptyStepBytes();
        StepBytes next = shiftRange(p, kNoteLayout, 0, 0, 1, 8, opts);
        check(getNoteSeqStep(next.data(), 0, 8).note == 3, "shiftRange moves step 0 to dest 8");
        check(getNoteSeqStep(next.data(), 0, 9).note == 4, "shiftRange moves step 1 to dest 9");
        NoteSeqStep vac0 = getNoteSeqStep(next.data(), 0, 0);
        check(!vac0.on && vac0.duration == 1 && vac0.oct == 0, "shiftRange clears the vacated source with the template");
        check(!getNoteSeqStep(next.data(), 0, 1).on, "shiftRange clears both vacated source steps");
    }

    // -- shiftRange: copy keeps the source; overlapping move keeps captured bytes --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 3; setNoteSeqStep(p.data(), 0, 0, a);
        ShiftOpts copyOpts; copyOpts.copy = true;
        StepBytes copied = shiftRange(p, kNoteLayout, 0, 0, 0, 4, copyOpts);
        check(getNoteSeqStep(copied.data(), 0, 0).on, "shiftRange copy keeps the source step");
        check(getNoteSeqStep(copied.data(), 0, 4).note == 3, "shiftRange copy writes the dest step");

        StepBytes q = makeEmptySeqPatterns();
        NoteSeqStep q2; q2.on = true; q2.note = 1; setNoteSeqStep(q.data(), 0, 2, q2);
        NoteSeqStep q3; q3.on = true; q3.note = 2; setNoteSeqStep(q.data(), 0, 3, q3);
        ShiftOpts moveOpts; moveOpts.emptyStep = emptyStepBytes();
        StepBytes moved = shiftRange(q, kNoteLayout, 0, 2, 3, 3, moveOpts);
        check(getNoteSeqStep(moved.data(), 0, 3).note == 1, "overlapping shiftRange: dest 3 gets captured step 2");
        check(getNoteSeqStep(moved.data(), 0, 4).note == 2, "overlapping shiftRange: dest 4 gets captured step 3");
        check(!getNoteSeqStep(moved.data(), 0, 2).on, "overlapping shiftRange vacates the untouched source step");
    }

    // -- shiftRange drops steps shifted past the pattern end (no bleed) --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 1; setNoteSeqStep(p.data(), 0, 0, a);
        NoteSeqStep b; b.on = true; b.note = 2; setNoteSeqStep(p.data(), 0, 1, b);
        ShiftOpts opts; opts.emptyStep = emptyStepBytes();
        StepBytes next = shiftRange(p, kNoteLayout, 0, 0, 1, 15, opts);
        check(getNoteSeqStep(next.data(), 0, 15).note == 1, "shiftRange past-end: last valid step gets the first byte");
        check(!getNoteSeqStep(next.data(), 1, 0).on, "shiftRange past-end drops overflow, no bleed into next pattern");
    }

    // -- DR-1 lane layout: range ops stay inside the pad row --
    {
        StepBytes p(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        p[(size_t)drumIdx(0, 5, 0)] = 1;
        p[(size_t)drumIdx(0, 5, 1)] = 2;
        p[(size_t)drumIdx(0, 6, 0)] = 2; // neighbor pad, must not travel

        StepBytes data = copyRange(p, drumLane(5), 0, 0, 1);
        check(data.size() == 2 && data[0] == 1 && data[1] == 2, "DR-1 copyRange reads only the target pad's lane");

        StepBytes next = pasteRange(StepBytes(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0), drumLane(2), 1, 14, data);
        check(next[(size_t)drumIdx(1, 2, 14)] == 1, "DR-1 pasteRange writes lane step 14");
        check(next[(size_t)drumIdx(1, 2, 15)] == 2, "DR-1 pasteRange writes lane step 15");
        check(next[(size_t)drumIdx(1, 3, 0)] == 0, "DR-1 pasteRange clamps at the lane end, no bleed into next pad");
    }

    // -- DR-1 lane layout: shiftRange moves hits along the pad row --
    {
        StepBytes p(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        p[(size_t)drumIdx(0, 0, 0)] = 2;
        StepBytes next = shiftRange(p, drumLane(0), 0, 0, 0, 8);
        check(next[(size_t)drumIdx(0, 0, 0)] == 0, "DR-1 shiftRange vacates the source step");
        check(next[(size_t)drumIdx(0, 0, 8)] == 2, "DR-1 shiftRange writes the dest step");
    }

    // -- shiftRange dest clamping: content and selection agree --
    // A dest at/past the pattern end must behave exactly like pasteRange's
    // no-op-at-end rule: the moved content never wraps or bleeds, and a
    // dest of stepsPerPattern-1 is the last slot content can land in.
    {
        StepBytes p(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        p[(size_t)drumIdx(0, 0, 0)] = 1;
        p[(size_t)drumIdx(0, 0, 1)] = 2;
        StepBytes atEnd = shiftRange(p, drumLane(0), 0, 0, 1, DR_STEPS); // dest == stepsPerPattern: no-op paste
        check(atEnd[(size_t)drumIdx(0, 0, 0)] == 0, "shiftRange dest-at-end still vacates the source (paste side no-ops)");
        check(atEnd[(size_t)drumIdx(1, 0, 0)] == 0, "shiftRange dest-at-end does not bleed into the next pattern");
    }

    // -- copyPattern / pastePattern (note layout) --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 11; a.acc = true;
        setNoteSeqStep(p.data(), 2, 7, a);

        StepBytes data = copyPattern(p, kNoteLayout, 2);
        check((int)data.size() == kNoteLayout.patternSize, "copyPattern returns a full pattern block");

        StepBytes next = pastePattern(p, kNoteLayout, 0, data);
        check(next != p, "pastePattern returns a new buffer");
        NoteSeqStep r = getNoteSeqStep(next.data(), 0, 7);
        check(r.on && r.note == 11 && r.acc, "pastePattern copies the whole bar into pattern 0");
        check(getNoteSeqStep(next.data(), 2, 7).on, "pastePattern keeps the source pattern");
        check(!getNoteSeqStep(p.data(), 0, 7).on, "pastePattern does not mutate its input");
    }

    // -- copyPattern / pastePattern (DR-1: whole block spans all pads, laneOffset ignored) --
    {
        StepBytes p(DR_NPATTERNS * DR_NPADS * DR_STEPS, 0);
        p[(size_t)drumIdx(1, 0, 0)] = 1;
        p[(size_t)drumIdx(1, 9, 15)] = 2;

        StepBytes data = copyPattern(p, drumLane(4), 1); // laneOffset on drumLane(4) must be ignored
        check((int)data.size() == DR_NPADS * DR_STEPS, "DR-1 copyPattern spans the full pad*step block");

        StepBytes next = pastePattern(p, drumLane(4), 3, data);
        check(next[(size_t)drumIdx(3, 0, 0)] == 1, "DR-1 pastePattern copies pad 0's hit");
        check(next[(size_t)drumIdx(3, 9, 15)] == 2, "DR-1 pastePattern copies pad 9's accent");
        check((int)next.size() == DR_NPATTERNS * DR_NPADS * DR_STEPS, "DR-1 pastePattern preserves total buffer size");
    }

    // ============ rectangle (step × note-lane) selection ============
    // Mirrors src/shared/seqEdit.test.ts's rect cases. maxNote = SEQ_NOTE_LANES-1.
    const int kMaxNote = SEQ_NOTE_LANES - 1;
    const StepBytes kEmpty = emptyStepBytes();

    // -- copyRect captures only lit cells inside the pitch band --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 5; a.acc = true; a.duration = 2; setNoteSeqStep(p.data(), 0, 2, a);
        NoteSeqStep b; b.on = true; b.note = 8;                              setNoteSeqStep(p.data(), 0, 3, b);
        NoteSeqStep c; c.on = true; c.note = 1;                              setNoteSeqStep(p.data(), 0, 4, c); // below band
        RectSel rect { 2, 4, 4, 9 }; // steps 2..4, notes 4..9
        RectCells cap = copyRect(p, kNoteLayout, 0, rect);
        check(cap.wSteps == 3 && cap.noteLo == 4 && cap.noteHi == 9, "copyRect records width and pitch band");
        check(cap.cells.size() == 2, "copyRect keeps only the two in-band lit cells");
        check(cap.cells[0].dStep == 0 && (cap.cells[0].bytes[1] & 0x7f) == 5, "captured cell 0 = step2/note5");
        check(cap.cells[1].dStep == 1 && (cap.cells[1].bytes[1] & 0x7f) == 8, "captured cell 1 = step3/note8");
    }

    // -- clearRect blanks in-band lit cells only, leaves out-of-band alone --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 5; setNoteSeqStep(p.data(), 0, 2, a);
        NoteSeqStep c; c.on = true; c.note = 1; setNoteSeqStep(p.data(), 0, 2, c); // overwrite: step2 note1 (below band)
        NoteSeqStep d; d.on = true; d.note = 7; setNoteSeqStep(p.data(), 0, 3, d);
        RectSel rect { 2, 3, 4, 9 };
        StepBytes next = clearRect(p, kNoteLayout, 0, rect, kEmpty);
        check(!getNoteSeqStep(next.data(), 0, 3).on, "clearRect blanks the in-band cell");
        check(getNoteSeqStep(next.data(), 0, 2).on && getNoteSeqStep(next.data(), 0, 2).note == 1,
              "clearRect leaves the out-of-band cell (note 1) untouched");
        check(getNoteSeqStep(p.data(), 0, 3).on, "clearRect never mutates its input");
    }

    // -- pasteRect transposes by dNote and drops cells outside pattern / lane range --
    {
        StepBytes src = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 3; a.duration = 2; setNoteSeqStep(src.data(), 0, 0, a);
        NoteSeqStep b; b.on = true; b.note = 6;                 setNoteSeqStep(src.data(), 0, 1, b);
        RectCells cap = copyRect(src, kNoteLayout, 0, RectSel{ 0, 1, 0, kMaxNote });
        StepBytes p = makeEmptySeqPatterns();
        StepBytes next = pasteRect(p, kNoteLayout, 0, 10, +2, cap, kMaxNote); // atStep 10, +2 semitones
        check(getNoteSeqStep(next.data(), 0, 10).note == 5 && getNoteSeqStep(next.data(), 0, 10).duration == 2,
              "pasteRect lands transposed cell and preserves duration");
        check(getNoteSeqStep(next.data(), 0, 11).note == 8, "pasteRect lands the second transposed cell");
        // dStep 1 from atStep 15 would land on step 16 (past end) -> dropped
        StepBytes edge = pasteRect(p, kNoteLayout, 0, 15, 0, cap, kMaxNote);
        check(getNoteSeqStep(edge.data(), 0, 15).on && !getNoteSeqStep(edge.data(), 0, 0).on,
              "pasteRect drops the cell that would fall past the pattern end (no wrap)");
        // transpose that pushes a note above maxNote drops just that cell
        StepBytes hi = pasteRect(p, kNoteLayout, 0, 0, +kMaxNote, cap, kMaxNote);
        check(!getNoteSeqStep(hi.data(), 0, 1).on, "pasteRect drops the cell whose transposed note exceeds maxNote");
    }

    // -- pasteRect preserves the BL-1 slide flag (byte1 bit7) --
    {
        StepBytes src = makeEmptySeqPatterns();
        // hand-set a lit cell with slide flag + note 4
        int o = seqStepOff(0, 0);
        src[(size_t)o] = 1; src[(size_t)(o + 1)] = (uint8_t)(0x80 | 4); src[(size_t)(o + 2)] = 1;
        RectCells cap = copyRect(src, kNoteLayout, 0, RectSel{ 0, 0, 0, kMaxNote });
        StepBytes p = makeEmptySeqPatterns();
        StepBytes next = pasteRect(p, kNoteLayout, 0, 5, +1, cap, kMaxNote);
        int no = seqStepOff(0, 5);
        check((next[(size_t)(no + 1)] & 0x80) != 0, "pasteRect keeps the slide flag through a transpose");
        check((next[(size_t)(no + 1)] & 0x7f) == 5, "pasteRect transposes the note under the slide flag");
    }

    // -- moveRect: move transposes and clears the source in one pass --
    {
        StepBytes p = makeEmptySeqPatterns();
        NoteSeqStep a; a.on = true; a.note = 4; setNoteSeqStep(p.data(), 0, 2, a);
        NoteSeqStep b; b.on = true; b.note = 7; setNoteSeqStep(p.data(), 0, 3, b);
        RectSel rect { 2, 3, 0, kMaxNote };
        StepBytes moved = moveRect(p, kNoteLayout, 0, rect, +5, +1, RectMoveOpts{ false, kEmpty, kMaxNote });
        check(!getNoteSeqStep(moved.data(), 0, 2).on && !getNoteSeqStep(moved.data(), 0, 3).on,
              "moveRect clears the source cells");
        check(getNoteSeqStep(moved.data(), 0, 7).note == 5 && getNoteSeqStep(moved.data(), 0, 8).note == 8,
              "moveRect lands the block shifted +5 steps / +1 semitone");
        // copy variant keeps the source
        StepBytes copied = moveRect(p, kNoteLayout, 0, rect, +5, 0, RectMoveOpts{ true, kEmpty, kMaxNote });
        check(getNoteSeqStep(copied.data(), 0, 2).on && getNoteSeqStep(copied.data(), 0, 7).on,
              "moveRect copy=true keeps the source and writes the destination");
    }

    // -- chain-aware rect spans bar boundaries (absolute steps into chain[bar]) --
    {
        StepBytes p = makeEmptySeqPatterns();
        std::vector<int> chain { 0, 1 }; // two bars: abs 0..31
        NoteSeqStep a; a.on = true; a.note = 4; setNoteSeqStep(p.data(), 0, 15, a); // last step of bar 0
        NoteSeqStep b; b.on = true; b.note = 6; setNoteSeqStep(p.data(), 1, 0, b);  // first step of bar 1
        RectSel rect { 15, 16, 0, kMaxNote }; // abs steps 15 (bar0/15) .. 16 (bar1/0)
        RectCells cap = copyRectChain(p, kNoteLayout, chain, rect);
        check(cap.cells.size() == 2 && cap.cells[0].dStep == 0 && cap.cells[1].dStep == 1,
              "copyRectChain captures cells across the bar boundary");
        // paste onto abs 0 (bar0/0) and abs 1 (bar0/1)
        StepBytes next = pasteRectChain(p, kNoteLayout, chain, 0, 0, cap, kMaxNote);
        check(getNoteSeqStep(next.data(), 0, 0).note == 4 && getNoteSeqStep(next.data(), 0, 1).note == 6,
              "pasteRectChain drops the captured cells at the new absolute anchor");
        // chainOffset rejects steps past the timeline
        check(chainOffset(kNoteLayout, chain, 32) < 0 && chainOffset(kNoteLayout, chain, -1) < 0,
              "chainOffset returns -1 outside the timeline");
    }

    // -- moveRectChain across bars clears source and lands destination --
    {
        StepBytes p = makeEmptySeqPatterns();
        std::vector<int> chain { 0, 1 };
        NoteSeqStep a; a.on = true; a.note = 3; setNoteSeqStep(p.data(), 0, 14, a);
        NoteSeqStep b; b.on = true; b.note = 5; setNoteSeqStep(p.data(), 1, 2, b); // abs 18
        RectSel rect { 14, 18, 0, kMaxNote };
        StepBytes moved = moveRectChain(p, kNoteLayout, chain, rect, +1, 0, RectMoveOpts{ false, kEmpty, kMaxNote });
        check(!getNoteSeqStep(moved.data(), 0, 14).on && !getNoteSeqStep(moved.data(), 1, 2).on,
              "moveRectChain clears both source cells across bars");
        check(getNoteSeqStep(moved.data(), 0, 15).note == 3 && getNoteSeqStep(moved.data(), 1, 3).note == 5,
              "moveRectChain lands the block one step later across the bar boundary");
    }

    // -- StepEditHistory: undo/redo shuffle snapshots and report availability --
    {
        StepEditHistory<int> h;
        check(!h.canUndo(), "fresh history has nothing to undo");
        int dummy;
        check(!h.undo(0, dummy), "undo on an empty history returns false");

        h.push(1); // state before mutating 1 -> 2
        h.push(2); // state before mutating 2 -> 3
        check(h.canUndo(), "history has entries after two pushes");

        int restored = -1;
        check(h.undo(3, restored) && restored == 2, "undo(3) restores 2");
        check(h.undo(2, restored) && restored == 1, "undo(2) restores 1");
        check(!h.canUndo(), "history exhausted after two undos");
        check(h.canRedo(), "redo available after undoing");

        check(h.redo(1, restored) && restored == 2, "redo(1) restores 2");
        check(h.redo(2, restored) && restored == 3, "redo(2) restores 3");
        check(!h.redo(3, restored), "redo exhausted returns false");
    }

    // -- StepEditHistory: a fresh push clears the redo stack --
    {
        StepEditHistory<int> h;
        h.push(1);
        int restored = -1;
        check(h.undo(2, restored) && restored == 1, "undo(2) restores 1");
        h.push(1); // new edit branch
        check(!h.canRedo(), "a fresh push after undo clears the redo stack");
        check(!h.redo(5, restored), "redo is unavailable after the branch push");
    }

    // -- StepEditHistory: bounded — oldest snapshots fall off past the limit --
    {
        StepEditHistory<int> h(3);
        for (int i = 0; i < 10; i++) h.push(i);
        int restored = -1;
        check(h.undo(10, restored) && restored == 9, "bounded history: first undo restores 9");
        check(h.undo(9, restored) && restored == 8, "bounded history: second undo restores 8");
        check(h.undo(8, restored) && restored == 7, "bounded history: third undo restores 7 (limit=3)");
        check(!h.undo(7, restored), "bounded history: fourth undo is exhausted (oldest entries fell off)");
    }

    // -- StepEditHistory: clear empties both stacks --
    {
        StepEditHistory<int> h;
        h.push(1);
        int restored = -1;
        h.undo(2, restored);
        h.clear();
        check(!h.canUndo(), "clear() empties the undo stack");
        check(!h.canRedo(), "clear() empties the redo stack");
    }

    printf("\n%s\n", g_fail == 0 ? "ALL CHECKS PASSED" : (std::to_string(g_fail) + " CHECK(S) FAILED").c_str());
    return g_fail == 0 ? 0 : 1;
}
