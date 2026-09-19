#pragma once
#include "OpenAIClient.h"
#include "QuickJsRuntime.h"

namespace codeact {
// Synchronous, single-owner core. Agent runs it exclusively on its worker.
class Session {
public:
    explicit Session(Limits limits = {});
    TurnResult run(const Turn&, ChatTransport&, const StopState&, const ProgressSink& = {},
                   const CompletionGate& = {});
    void reset();
private:
    TurnResult runImpl(const Turn&, ChatTransport&, const StopState&, const ProgressSink&);
    Limits limits;
    juce::Array<Json> history;
    std::unique_ptr<QuickJsRuntime> js;
    std::optional<Endpoint> previousEndpoint;
    bool runtimeResetPending = false;
};
struct View {
    std::uint64_t revision = 0;
    bool busy = false;
    juce::String activity, text, diagnostics;
    // Immutable, model-visible trace chunks. Shared so polling the editor
    // never duplicates a potentially large completed transcript.
    std::shared_ptr<const std::vector<juce::String>> trace;
    std::shared_ptr<const TurnResult> result;
};
// Own this in the AudioProcessor, not its editor. ALL public methods are
// non-realtime. The editor polls the mailbox; no callback captures an editor.
class Agent final : private juce::Thread {
public:
    explicit Agent(Limits limits = {}, std::unique_ptr<ChatTransport> transport = {});
    ~Agent() override;
    bool submit(Turn); // false if busy or shutting down; never queues another turn
    void cancel();
    View poll() const;
    void shutdown(); // cooperative join; NEVER call from processBlock/releaseResources
private:
    struct Pending { Turn turn; std::shared_ptr<StopState> stop; };
    void run() override;
    void publish(const Progress&);
    void appendTrace(const juce::String&);
    Limits limits;
    std::unique_ptr<ChatTransport> transport;
    mutable juce::CriticalSection mutex;
    std::optional<Pending> pending;
    std::shared_ptr<StopState> activeStop;
    View view;
    std::vector<juce::String> trace;
    std::size_t traceBytes = 0;
    bool shuttingDown = false;
};
} // namespace codeact
