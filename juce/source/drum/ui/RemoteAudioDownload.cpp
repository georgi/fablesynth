#include "RemoteAudioDownload.h"

#include <juce_events/juce_events.h>

#include <array>

namespace fui {
namespace {
constexpr int kConnectTimeoutMs = 12000;
constexpr int64_t kMaxDownloadBytes = 32 * 1024 * 1024;
constexpr int kChunkBytes = 32768;
}

RemoteAudioDownload::RemoteAudioDownload(juce::URL url, Completion completion)
    : juce::Thread("FableSynth DR-1 remote audio import"),
      url_(std::move(url)), completion_(std::move(completion)) {}

RemoteAudioDownload::~RemoteAudioDownload() {
    signalThreadShouldExit();
    waitForThreadToExit(kConnectTimeoutMs + 1000);
    if (temporaryFile_.existsAsFile()) temporaryFile_.deleteFile();
}

void RemoteAudioDownload::begin() { startThread(); }

void RemoteAudioDownload::finish(Result downloadResult) {
    juce::MessageManager::callAsync([completion = completion_, delivered = std::move(downloadResult)]() mutable {
        if (completion) completion(std::move(delivered));
    });
}

void RemoteAudioDownload::run() {
    int status = 0;
    const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(kConnectTimeoutMs)
        .withNumRedirectsToFollow(0)
        .withHttpRequestCmd("GET")
        .withExtraHeaders("Accept: audio/wav, audio/aiff, audio/flac, audio/mpeg, audio/*\r\n")
        .withStatusCode(&status);
    auto input = url_.createInputStream(options);
    if (input == nullptr) {
        finish({ {}, "Could not download that URL. Use a direct HTTPS audio-file link." });
        return;
    }
    if (status < 200 || status >= 300) {
        finish({ {}, "The source returned HTTP " + juce::String(status) + ". Use the final direct audio-file URL." });
        return;
    }

    temporaryFile_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("fablesynth-dr1-import", ".wav", false);
    juce::FileOutputStream output(temporaryFile_);
    if (!output.openedOk()) {
        finish({ {}, "Could not create a temporary file for the import." });
        return;
    }

    std::array<char, kChunkBytes> buffer {};
    int64_t total = 0;
    while (!threadShouldExit()) {
        const int read = input->read(buffer.data(), (int) buffer.size());
        if (read <= 0) break;
        total += read;
        if (total > kMaxDownloadBytes) {
            finish({ {}, "Remote audio is over the 32 MB import limit." });
            return;
        }
        if (!output.write(buffer.data(), (size_t) read)) {
            finish({ {}, "Could not write the downloaded audio." });
            return;
        }
    }
    output.flush();
    if (threadShouldExit()) { temporaryFile_.deleteFile(); return; }
    if (total == 0) { temporaryFile_.deleteFile(); finish({ {}, "The remote file was empty." }); return; }
    finish({ temporaryFile_, {} });
}

} // namespace fui
