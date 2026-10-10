#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace fable {

// Lock-free analysis tap: one audio-thread writer, any number of message-thread
// readers. The writer stores samples into a power-of-two ring and then
// publishes its running frame count; it never allocates, locks or waits.
// Samples are relaxed atomics, so a reader racing the writer is well defined.
// A reader validates each copy against the frame count afterwards and treats
// any frame the writer may have overwritten meanwhile as lost.
//
// Frame numbers are absolute (frames written since construction or reset()).
// Readers keep their own cursor, so several consumers can pull independently.
template <int Channels, int Capacity>
class SampleTap {
    static_assert(Channels == 1 || Channels == 2, "mono or stereo");
    static_assert(Capacity >= 64 && (Capacity & (Capacity - 1)) == 0, "power of two");

  public:
    static constexpr int kChannels = Channels;
    static constexpr int kCapacity = Capacity;

    // Writer side (audio thread). R is ignored by a mono tap; a null R writes
    // L to both channels of a stereo tap.
    void write(const float* L, const float* R, int n) noexcept {
        if (n <= 0) return;
        uint64_t w = written_.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++w) {
            const size_t at = (size_t)(w & (uint64_t)(Capacity - 1));
            data_[0][at].store(L ? L[i] : 0.0f, std::memory_order_relaxed);
            if constexpr (Channels == 2)
                data_[1][at].store(R ? R[i] : (L ? L[i] : 0.0f), std::memory_order_relaxed);
        }
        written_.store(w, std::memory_order_release);
    }
    void writeSilence(int n) noexcept { write(nullptr, nullptr, n); }
    void push(float l, float r = 0.0f) noexcept {
        const uint64_t w = written_.load(std::memory_order_relaxed);
        const size_t at = (size_t)(w & (uint64_t)(Capacity - 1));
        data_[0][at].store(l, std::memory_order_relaxed);
        if constexpr (Channels == 2) data_[1][at].store(r, std::memory_order_relaxed);
        written_.store(w + 1, std::memory_order_release);
    }
    // Only while the writer is stopped (prepare / reset).
    void reset() noexcept {
        for (auto& channel : data_)
            for (auto& s : channel) s.store(0.0f, std::memory_order_relaxed);
        written_.store(0, std::memory_order_release);
    }

    // Reader side (any thread). Frames written so far.
    uint64_t frames() const noexcept { return written_.load(std::memory_order_acquire); }

    // Copies the newest n frames (n <= Capacity / 2). With a null R, a stereo
    // tap writes the mono mix (L + R) / 2 into L. Returns false while fewer
    // than n frames exist or when the writer lapped the copy twice in a row.
    // endFrame receives the absolute frame number after the last copied one.
    bool readLatest(float* L, float* R, int n, uint64_t* endFrame = nullptr) const noexcept {
        if (n <= 0 || n > Capacity / 2 || !L) return false;
        for (int attempt = 0; attempt < 2; ++attempt) {
            const uint64_t end = frames();
            if (end < (uint64_t)n) return false;
            copy(end - (uint64_t)n, L, R, n);
            if (frames() - (end - (uint64_t)n) <= (uint64_t)Capacity) {
                if (endFrame) *endFrame = end;
                return true;
            }
        }
        return false;
    }

    // Continuous pull from cursor (an absolute frame number the caller keeps;
    // start it at frames()). Copies up to max frames, advances cursor and
    // returns the count. Frames the writer overwrote before they were read
    // are skipped and counted in *dropped. A cursor ahead of the writer (a
    // reset tap) is pulled back to the current frame.
    int pull(uint64_t& cursor, float* L, float* R, int max, uint64_t* dropped = nullptr) const noexcept {
        if (dropped) *dropped = 0;
        if (max <= 0 || !L) return 0;
        const uint64_t end = frames();
        if (cursor > end) cursor = end;
        // Leave a margin the writer may fill while we copy.
        const uint64_t oldest = end > (uint64_t)Capacity / 2 ? end - (uint64_t)Capacity / 2 : 0;
        if (cursor < oldest) {
            if (dropped) *dropped = oldest - cursor;
            cursor = oldest;
        }
        const int n = (int)std::min<uint64_t>((uint64_t)max, end - cursor);
        if (n <= 0) return 0;
        copy(cursor, L, R, n);
        const uint64_t after = frames();
        if (after - cursor > (uint64_t)Capacity) {
            // Lapped mid-copy: the data is unreliable. Drop it and resync.
            const uint64_t lost = after - cursor;
            if (dropped) *dropped += lost;
            cursor = after;
            return 0;
        }
        cursor += (uint64_t)n;
        return n;
    }

  private:
    void copy(uint64_t start, float* L, float* R, int n) const noexcept {
        for (int i = 0; i < n; ++i) {
            const size_t at = (size_t)((start + (uint64_t)i) & (uint64_t)(Capacity - 1));
            const float l = data_[0][at].load(std::memory_order_relaxed);
            if constexpr (Channels == 2) {
                const float r = data_[1][at].load(std::memory_order_relaxed);
                if (R) { L[i] = l; R[i] = r; }
                else L[i] = 0.5f * (l + r);
            } else {
                L[i] = l;
                if (R) R[i] = l;
            }
        }
    }

    std::array<std::array<std::atomic<float>, (size_t)Capacity>, (size_t)Channels> data_{};
    std::atomic<uint64_t> written_{0};
};

// Compressor detector input (max |L|,|R| per sample, or the high-passed key
// while keyed). ~170 ms at 48 kHz; the web trace is bounded at 100 ms.
using DetectorTrace = SampleTap<1, 8192>;
// Mono post-FX DR-1 pad capture (KIT MIX); the web posts 4096-frame blocks.
using PadCaptureTap = SampleTap<1, 16384>;
// Stereo program taps: SQ-4 tracks / master, and each instrument output.
// Auto-EQ uses a 16384-point FFT, which needs Capacity >= 32768.
using ProgramTap = SampleTap<2, 32768>;

} // namespace fable
