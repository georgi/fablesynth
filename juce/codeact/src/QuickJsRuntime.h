#pragma once
#include "Types.h"
namespace codeact {
// Single-owner-thread only. No JSValue, context, or runtime crosses a thread.
// A failed execution discards that execution's proposals and destroys the VM.
class QuickJsRuntime {
public:
    explicit QuickJsRuntime(Limits limits = {});
    ~QuickJsRuntime();
    QuickJsRuntime(const QuickJsRuntime&) = delete;
    QuickJsRuntime& operator=(const QuickJsRuntime&) = delete;
    JsResult execute(const juce::String& code, const Snapshot&, const StopState&);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace codeact
