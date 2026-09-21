#pragma once

#include <juce_core/juce_core.h>

#include <functional>

namespace fui {

// A bounded, user-initiated download used only by the DR-1 remote importer.
// It never runs on the audio/message threads and permits direct HTTPS media
// URLs only. Decoding remains in PadGrid's existing local-file import path.
class RemoteAudioDownload final : private juce::Thread {
public:
    struct Result {
        juce::File file;
        juce::String error;
        bool succeeded() const { return error.isEmpty() && file.existsAsFile(); }
    };

    using Completion = std::function<void(Result)>;

    RemoteAudioDownload(juce::URL url, Completion completion);
    ~RemoteAudioDownload() override;

    void begin();

private:
    void run() override;
    void finish(Result);

    juce::URL url_;
    Completion completion_;
    juce::File temporaryFile_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteAudioDownload)
};

} // namespace fui
