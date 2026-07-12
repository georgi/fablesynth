#include "../source/seq/dsp/SeqProtocol.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void testProtocol() {
    using namespace fable;
    CHECK(sqBytesPerBar(Machine::DR1) == 256);
    CHECK(sqBytesPerBar(Machine::BL1) == 48);
    CHECK(sqBytesPerBar(Machine::WT1) == 48);
    CHECK(sqDr1Idx(1, 2, 3) == (1 * 16 + 2) * 16 + 3);
    CHECK(sqNoteIdx(2, 5) == (2 * 16 + 5) * 3);

    auto dr = sqEmptyClip(Machine::DR1, 2);
    CHECK(dr.size() == 512);
    for (auto b : dr) CHECK(b == 0);
    auto bl = sqEmptyClip(Machine::BL1, 1);
    CHECK(bl.size() == 48);
    for (int i = 0; i < 48; i++) CHECK(bl[(size_t)i] == (i % 3 == 2 ? 1 : 0));

    const double sr = 48000, bpm = 122;
    const double spb = sr * 60 / bpm;
    CHECK(std::abs(sqSamplesPerBeat(bpm, sr) - spb) < 1e-9);
    CHECK(std::abs(sqSamplesPerStep(bpm, sr) - spb / 4) < 1e-9);
    CHECK(std::abs(sqBarFrames(bpm, sr) - spb * 4) < 1e-9);

    // OFF -> 0 ("this block"); 1/4 -> next beat; 1 BAR -> next bar; pure fn.
    CHECK(sqBoundaryFrame(Quant::Off, 99999, 256, bpm, sr) == 0.0);
    double b1 = sqBoundaryFrame(Quant::Quarter, 256 + spb * 1.5, 256, bpm, sr);
    CHECK(std::abs(b1 - (256 + spb * 2)) < 1e-6);
    double b2 = sqBoundaryFrame(Quant::Bar, 256 + spb * 1.5, 256, bpm, sr);
    CHECK(std::abs(b2 - (256 + spb * 4)) < 1e-6);
    // at-or-after: now exactly on a boundary returns now
    CHECK(std::abs(sqBoundaryFrame(Quant::Bar, 256 + spb * 4, 256, bpm, sr) - (256 + spb * 4)) < 1e-6);
    // now before anchor clamps to anchor
    CHECK(std::abs(sqBoundaryFrame(Quant::Bar, 0, 256, bpm, sr) - 256) < 1e-6);

    auto p = sqSongPosition(256 + spb * 5.2, 256, bpm, sr);
    CHECK(p.beat == 1 && p.bar == 2);
}

int main() {
    testProtocol();
    if (failures) { std::printf("%d FAILURES\n", failures); return 1; }
    std::printf("ALL PASS\n");
    return 0;
}
