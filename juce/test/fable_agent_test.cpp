#include "../source/agent/FableAgent.h"

#include <chrono>
#include <iostream>
#include <limits>

namespace {
codeact::Json call(const char* id, const juce::String& code) {
    return codeact::object({{"id", id}, {"type", "function"}, {"function", codeact::object({
        {"name", "execute_js"},
        {"arguments", codeact::jsonText(codeact::object({{"code", code}}))}
    })}});
}
codeact::Completion tools(std::initializer_list<codeact::Json> calls) {
    return {codeact::object({{"role", "assistant"}, {"content", codeact::Json()},
                            {"tool_calls", codeact::array(calls)}}), "tool_calls"};
}
codeact::Completion finalText() {
    return {codeact::object({{"role", "assistant"}, {"content", "Ready for review."}}), "stop"};
}
struct ScriptTransport final : codeact::ChatTransport {
    std::vector<codeact::Completion> script;
    std::size_t next = 0;
    std::vector<juce::Array<codeact::Json>> requests;
    std::atomic<int> delivered { 0 };
    explicit ScriptTransport(std::vector<codeact::Completion> input) : script(std::move(input)) {}
    codeact::Completion complete(const codeact::Endpoint&, const juce::Array<codeact::Json>& messages,
                                 const codeact::Limits&, const codeact::StopState& stop,
                                 const codeact::TextSink& sink) override {
        stop.check();
        juce::Array<codeact::Json> request;
        for (const auto& message : messages) request.add(message.clone());
        requests.push_back(std::move(request));
        if (next >= script.size()) codeact::fail("Script exhausted");
        auto result = script[next++];
        const auto content = codeact::get(result.message, "content");
        delivered.fetch_add(1);
        if (content.toString() == "FAIL_FIXTURE") codeact::fail("Fixture transport failure");
        if (content.toString() == "FAIL_LONG") codeact::fail(juce::String::repeatedString("E", 12000));
        if (sink && content.isString()) sink(content.toString());
        return result;
    }
};
struct BlockingTransport final : codeact::ChatTransport {
    juce::WaitableEvent entered;
    codeact::Completion complete(const codeact::Endpoint&, const juce::Array<codeact::Json>&,
                                 const codeact::Limits&, const codeact::StopState& stop,
                                 const codeact::TextSink&) override {
        entered.signal();
        while (!stop.stopped()) juce::Thread::sleep(1);
        stop.check();
        codeact::fail("unreachable");
    }
};

int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    juce::ScopedJuceInitialiser_GUI initialiser;
    try {
        const std::vector<std::string> modes { "LOW", "MID", "HIGH" };
        const fable::ParamInfo descriptor {
            0, "filter.mode", "MODE", 0.0f, 2.0f, 0.0f,
            fable::Curve::Int, fable::Kind::Enum, &modes
        };
        const auto described = fable::agentParameter(descriptor, 1.0f, "track2.");
        check(described.id == "track2.filter.mode", "prefix metadata");
        check(described.minimum == 0.0 && described.maximum == 2.0 && described.step == 1.0,
              "physical range metadata");
        check(described.choices.size() == 3 && described.choices[2] == "HIGH",
              "choice metadata");

        codeact::Snapshot large;
        large.pluginName = "Catalog";
        for (int i = 0; i < 1200; ++i) {
            auto parameter = described;
            parameter.id = "p" + juce::String(i);
            large.parameters.push_back(std::move(parameter));
        }
        large.validate();
        check(large.parameters.size() == 1200, "large Fable catalog accepted");

        fable::FableAgent::CapturedState current;
        current.snapshot.pluginName = "Test";
        current.snapshot.parameters.push_back(described);
        current.document.append("A", 1);
        current.generation = 7;
        int commits = 0;
        std::vector<codeact::Change> committed;
        auto scripted = std::make_unique<ScriptTransport>(std::vector<codeact::Completion> {
            tools({call("proposal", "host.proposeParameters({'track2.filter.mode':2}); return 'staged';")}),
            finalText()
        });
        fable::FableAgent agent(
            [&] { return current; },
            [&](const std::vector<codeact::Change>& changes, juce::String&) {
                ++commits;
                committed = changes;
                return true;
            }, std::move(scripted));

        const auto before = agent.capture();
        juce::String error;
        check(agent.applyProposal(before, {{described.id, 1.0, 2.0}}, error),
              "valid proposal rejected");
        check(commits == 1 && committed.size() == 1 && committed[0].after == 2.0,
              "valid proposal did not commit once");

        check(!agent.applyProposal(before, {{described.id, 1.0, 0.5}}, error)
                  && error.containsIgnoreCase("discrete"),
              "off-step proposal accepted");
        check(commits == 1, "invalid proposal reached commit");

        check(!agent.applyProposal(before, {{"missing", 0.0, 1.0}}, error)
                  && error.containsIgnoreCase("unknown"),
              "unknown parameter accepted");
        check(commits == 1, "unknown proposal reached commit");

        current.snapshot.parameters[0].value = 0.0;
        check(!agent.applyProposal(before, {{described.id, 1.0, 2.0}}, error)
                  && error.containsIgnoreCase("changed"),
              "stale snapshot accepted");
        check(commits == 1, "stale proposal reached commit");

        current = before;
        current.document.append("B", 1);
        check(!agent.applyProposal(before, {{described.id, 1.0, 2.0}}, error),
              "stale document accepted");
        check(commits == 1, "stale document reached commit");

        current = before;
        ++current.generation;
        check(!agent.applyProposal(before, {{described.id, 1.0, 2.0}}, error),
              "stale generation accepted");
        check(commits == 1, "stale generation reached commit");

        current = before;
        check(!agent.applyProposal(before,
                  {{described.id, 1.0, std::numeric_limits<double>::infinity()}}, error),
              "non-finite proposal accepted");
        check(commits == 1, "non-finite proposal reached commit");

        check(!agent.applyProposal(before, {{described.id, 1.0, 3.0}}, error),
              "out-of-range proposal accepted");
        check(commits == 1, "out-of-range proposal reached commit");

        check(!agent.applyProposal(before,
                  {{described.id, 1.0, 2.0}, {described.id, 1.0, 0.0}}, error)
                  && error.containsIgnoreCase("duplicate"),
              "duplicate proposal accepted");
        check(commits == 1, "partially invalid proposal reached commit");

        codeact::Endpoint endpoint;
        endpoint.model = "fixture";
        check(agent.submit("Set the mode to HIGH", endpoint, true), "async submit rejected");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (agent.poll().busy && std::chrono::steady_clock::now() < deadline)
            juce::Thread::sleep(1);
        check(!agent.poll().busy && agent.canApply(), "async proposal was not published");
        check(agent.apply(error), "async proposal did not apply");
        check(commits == 2 && committed.size() == 1 && committed[0].after == 2.0,
              "async proposal committed incorrectly");
        check(!agent.apply(error) && commits == 2, "proposal applied more than once");

        int cancelCommits = 0;
        auto blocking = std::make_unique<BlockingTransport>();
        auto* blockingRaw = blocking.get();
        fable::FableAgent cancelledAgent(
            [&] { return current; },
            [&](const std::vector<codeact::Change>&, juce::String&) {
                ++cancelCommits;
                return true;
            }, std::move(blocking));
        check(cancelledAgent.submit("Wait", endpoint, true), "cancel test submit rejected");
        check(blockingRaw->entered.wait(2000), "cancel test transport never started");
        cancelledAgent.cancel();
        const auto cancelDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (cancelledAgent.poll().busy && std::chrono::steady_clock::now() < cancelDeadline)
            juce::Thread::sleep(1);
        check(!cancelledAgent.poll().busy, "cancelled turn stayed busy");
        check(!cancelledAgent.canApply() && cancelCommits == 0,
              "cancelled turn exposed or committed a proposal");

        // The processor retains conversation and model choice independently
        // of an editor, including the actual apply outcome on a follow-up.
        auto waitTurn = [&](fable::FableAgent& controller) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (controller.poll().busy && std::chrono::steady_clock::now() < until)
                juce::Thread::sleep(1);
            check(!controller.poll().busy, "conversation turn timed out");
        };
        auto requestText = [](const juce::Array<codeact::Json>& messages) {
            return codeact::jsonText(codeact::Json(messages));
        };
        auto historyTransport = std::make_unique<ScriptTransport>(std::vector<codeact::Completion> {
            tools({call("first", "host.proposeParameters({'track2.filter.mode':2}); return 'first';")}), finalText(),
            tools({call("second", "if (host.snapshot().parameters[0].value !== 2) throw Error('old snapshot'); host.proposeParameters({'track2.filter.mode':0}); return 'follow-up';")}), finalText(),
            finalText(), finalText()
        });
        auto* historyRaw = historyTransport.get();
        auto historyState = before;
        fable::FableAgent historyAgent([&] { return historyState; },
            [&](const auto& changes, juce::String&) {
                historyState.snapshot.parameters[0].value = changes[0].after;
                return true;
            }, std::move(historyTransport));
        historyAgent.setSelectedModel("fixture-model");
        endpoint.model = "fixture-model";
        check(historyAgent.submit("Raise the mode", endpoint), "conversation first submit");
        waitTurn(historyAgent);
        check(historyAgent.conversation().size() == 1 && historyAgent.conversation()[0].status == "pending",
              "pending proposal missing from conversation");
        check(historyAgent.apply(error), "conversation first apply");
        check(historyAgent.conversation()[0].status == "applied", "apply status not retained");
        check(historyAgent.submit("Make it low instead", endpoint), "follow-up submit");
        waitTurn(historyAgent);
        check(historyAgent.canApply(), "follow-up did not use refreshed snapshot");
        const auto followup = requestText(historyRaw->requests[2]);
        check(followup.contains("Raise the mode") && followup.contains("Ready for review.")
                  && followup.contains("Make it low instead") && followup.contains("applied"),
              "follow-up lost prior messages or apply outcome");
        int userMessages = 0, hostObservations = 0;
        for (const auto& message : historyRaw->requests[2]) {
            if (codeact::get(message, "role").toString() == "user") ++userMessages;
            const auto content = codeact::get(message, "content").toString();
            if (content.startsWith("PLUGIN HOST OBSERVATION")) {
                check(codeact::get(message, "role").toString() == "user", "host observation elevated to system authority");
                ++hostObservations;
            }
        }
        check(userMessages == 3 && hostObservations == 1,
              "host observation must remain separate from actual user prompts");
        historyState.document.append("external edit", 13);
        check(!historyAgent.apply(error) && historyAgent.conversation().back().status == "rejected"
                  && historyAgent.conversation().back().error.isNotEmpty(),
              "stale apply outcome missing from conversation");
        const auto previousRevision = historyAgent.conversationRevision();
        historyAgent.newConversation();
        check(historyAgent.conversation().empty() && !historyAgent.canApply()
                  && historyAgent.conversationRevision() > previousRevision,
              "new conversation did not clear transcript and consume proposal");
        check(historyAgent.selectedModel() == "fixture-model", "model selection lost on reset/reopen");
        check(historyAgent.submit("Fresh conversation", endpoint), "fresh conversation submit");
        waitTurn(historyAgent);
        const auto freshRequest = requestText(historyRaw->requests[4]);
        check(!freshRequest.contains("Raise the mode") && freshRequest.contains("Fresh conversation"),
              "new conversation did not reset worker context");

        historyAgent.setSelectedModel("another-model");
        check(historyAgent.conversation().size() == 1, "editing model selector cleared conversation before Send");
        endpoint.model = "another-model";
        check(historyAgent.submit("New model conversation", endpoint), "new model submit");
        waitTurn(historyAgent);
        check(historyAgent.conversation().size() == 1
                  && historyAgent.conversation()[0].prompt == "New model conversation",
              "model switch retained misleading prior displayed conversation");
        const auto switchedRequest = requestText(historyRaw->requests[5]);
        check(!switchedRequest.contains("Fresh conversation") && !switchedRequest.contains("Raise the mode"),
              "model switch retained previous model context");

        auto retryTransport = std::make_unique<ScriptTransport>(std::vector<codeact::Completion> {
            {codeact::object({{"role", "assistant"}, {"content", "FAIL_FIXTURE"}}), "stop"}, finalText()
        });
        auto* retryRaw = retryTransport.get();
        fable::FableAgent retryAgent([&] { return before; }, [](const auto&, auto&) { return true; },
                                    std::move(retryTransport));
        check(retryAgent.submit("Warm the high response", endpoint), "retry first submit");
        waitTurn(retryAgent);
        check(retryAgent.conversation().back().status == "failed", "failed turn missing from transcript");
        check(retryAgent.conversation().back().activityLog.contains("ERROR")
                  && retryAgent.conversation().back().activityLog.contains("Fixture transport failure"),
              "full provider failure is missing from the activity log");
        check(retryAgent.submit("Try again", endpoint), "retry follow-up submit");
        waitTurn(retryAgent);
        const auto retryRequest = requestText(retryRaw->requests[1]);
        check(retryRequest.contains("Warm the high response")
                  && retryRequest.contains("all persistent JavaScript memory was reset")
                  && !retryRequest.contains("javascriptMemoryReset")
                  && retryRequest.contains("Fixture transport failure"),
              "retry lost failed prompt or reset context");
        auto longErrorTransport = std::make_unique<ScriptTransport>(std::vector<codeact::Completion> {
            {codeact::object({{"role", "assistant"}, {"content", "FAIL_LONG"}}), "stop"}
        });
        fable::FableAgent longErrorAgent([&] { return before; }, [](const auto&, auto&) { return true; },
                                        std::move(longErrorTransport));
        check(longErrorAgent.submit("Show the complete provider error", endpoint), "long-error submit");
        waitTurn(longErrorAgent);
        const auto& longErrorTurn = longErrorAgent.conversation().back();
        check(longErrorTurn.error.getNumBytesAsUTF8() == 12000
                  && longErrorTurn.activityLog.contains(juce::String::repeatedString("E", 12000)),
              "provider error was truncated before reaching the activity log");
        check(cancelledAgent.conversation().back().status == "cancelled", "cancelled status missing");

        // Endpoint changes that reset the worker must also reset the visible
        // conversation, even when the model ID remains the same.
        auto identityTransport = std::make_unique<ScriptTransport>(std::vector<codeact::Completion>(6, finalText()));
        auto* identityRaw = identityTransport.get();
        fable::FableAgent identityAgent([&] { return before; }, [](const auto&, auto&) { return true; },
                                       std::move(identityTransport));
        auto identityEndpoint = endpoint;
        identityEndpoint.apiKey = "credential-fixture-a";
        for (int turn = 0; turn < 6; ++turn) {
            if (turn == 1) identityEndpoint.apiKey = "credential-fixture-b";
            if (turn == 2) identityEndpoint.headers.set("X-Test-Configuration", "changed");
            if (turn == 3) identityEndpoint.extraBodyJson = "{\"temperature\":0.2}";
            if (turn == 4) identityEndpoint.baseUrl = "https://example.invalid/v1";
            if (turn == 5) identityEndpoint.model = "third-fixture-model";
            const auto prompt = "Endpoint conversation " + juce::String(turn);
            check(identityAgent.submit(prompt, identityEndpoint), "endpoint identity submit");
            waitTurn(identityAgent);
            check(identityAgent.conversation().size() == 1 && identityAgent.conversation()[0].prompt == prompt,
                  "endpoint identity change retained stale display conversation");
            const auto sent = requestText(identityRaw->requests[(size_t)turn]);
            check(!sent.contains("credential-fixture-"), "credential leaked into model messages");
            if (turn > 0)
                check(!sent.contains("Endpoint conversation " + juce::String(turn - 1)),
                      "endpoint change retained old core conversation");
        }

        // A local capture failure never enters Session and must not imply that
        // its JavaScript memory was reset merely because display status failed.
        auto captureTransport = std::make_unique<ScriptTransport>(std::vector<codeact::Completion> {
            tools({call("remember-before-capture-error", "memory.fixtureMarker = 23; return memory.fixtureMarker;")}), finalText(),
            tools({call("read-after-capture-error", "if (memory.fixtureMarker !== 23) throw Error('memory unexpectedly lost'); host.proposeParameters({'track2.filter.mode':2}); return 'retained';")}), finalText()
        });
        auto* captureRaw = captureTransport.get();
        bool failCapture = false;
        fable::FableAgent captureAgent([&] {
            if (failCapture) codeact::fail("Fixture local capture error");
            return before;
        }, [](const auto&, auto&) { return true; }, std::move(captureTransport));
        check(captureAgent.submit("Remember this context", endpoint), "capture fixture first submit");
        waitTurn(captureAgent);
        failCapture = true;
        check(!captureAgent.submit("This snapshot cannot be captured", endpoint), "local capture failure was accepted");
        failCapture = false;
        check(captureAgent.submit("Try that again", endpoint), "capture failure retry submit");
        waitTurn(captureAgent);
        check(captureAgent.canApply(), "local capture failure discarded JavaScript context");
        const auto captureRetry = requestText(captureRaw->requests[2]);
        check(captureRetry.contains("This snapshot cannot be captured")
                  && !captureRetry.contains("javascriptMemoryReset")
                  && !captureRetry.contains("all persistent JavaScript memory was reset"),
              "controller falsely claimed a runtime reset for local capture failure");

        // Let a request finish with no controller/UI polling. Submitting the
        // next request must archive that result before the worker replaces it.
        std::vector<codeact::Completion> archiveScript;
        archiveScript.push_back(tools({call("unapplied", "host.proposeParameters({'track2.filter.mode':2}); return 'stage';")}));
        for (int i = 0; i < 36; ++i) archiveScript.push_back(finalText());
        auto archiveTransport = std::make_unique<ScriptTransport>(std::move(archiveScript));
        auto* archiveRaw = archiveTransport.get();
        fable::FableAgent archiveAgent([&] { return before; }, [](const auto&, auto&) { return true; },
                                      std::move(archiveTransport));
        check(archiveAgent.submit("Keep this while closed", endpoint), "archive submit");
        const auto archiveDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (archiveRaw->delivered.load() < 2 && std::chrono::steady_clock::now() < archiveDeadline)
            juce::Thread::sleep(1);
        juce::Thread::sleep(10);
        check(archiveAgent.submit("Follow up after reopening", endpoint), "archive follow-up submit");
        waitTurn(archiveAgent);
        check(archiveAgent.conversation().size() == 2
                  && archiveAgent.conversation()[0].assistant == "Ready for review."
                  && archiveAgent.conversation()[0].status == "not-applied",
              "unpolled result or superseded proposal status lost");
        for (int i = 0; i < 34; ++i) {
            check(archiveAgent.submit("Further follow-up " + juce::String(i), endpoint), "bounded history submit");
            waitTurn(archiveAgent);
        }
        check(archiveAgent.conversation().size() <= 32, "display conversation exceeds turn bound");

        // Read-only observations are detached from the parameter/document
        // stale check; activity alone must never invalidate an Apply proposal.
        auto observationState = before;
        observationState.snapshot.audio = codeact::object({{"available", false}});
        observationState.snapshot.meters = codeact::object({{"available", false}});
        int observationCommits = 0;
        fable::FableAgent observationAgent([&] { return observationState; },
            [&](const auto&, auto&) { ++observationCommits; return true; });
        const auto frozenObservation = observationAgent.capture();
        observationState.snapshot.audio = codeact::object({{"available", true}, {"serial", 99}});
        observationState.snapshot.meters = codeact::object({{"available", true}, {"level", 0.25}});
        check(observationAgent.applyProposal(frozenObservation, {{described.id, 1.0, 2.0}}, error)
                  && observationCommits == 1, "read-only telemetry drift staled parameter proposal");
        fable::AudioMeter testMeter;
        testMeter.prepare(48000, 1);
        const auto unavailable = fable::agentAudioMeasurements(testMeter.snapshot(), "fixture final output");
        check(!static_cast<bool>(codeact::get(unavailable, "available")), "empty measurement marked available");
        std::vector<float> monoSamples(4800, 0.25f);
        const float* monoChannels[] {monoSamples.data()};
        testMeter.process(monoChannels, 1, 4800);
        const auto observation = fable::agentAudioMeasurements(testMeter.snapshot(), "fixture final output");
        check(static_cast<bool>(codeact::get(observation, "available"))
                  && std::abs(static_cast<double>(codeact::get(codeact::get(observation, "combined"), "rmsLinear")) - 0.25) < 1e-6,
              "measurement adapter lost physical output level");
        check(codeact::get(observation, "audioWindowAgeMs").isVoid()
                  && codeact::get(observation, "snapshotCapturedAtMs").isInt64(),
              "measurement adapter confuses capture time with audio-window age");
        check(codeact::get(observation, "right").isVoid() && codeact::get(observation, "stereoCorrelation").isVoid(),
              "mono measurement claims a stereo observation");

        // Multiple JS calls across two model responses produce one reviewed
        // proposal and exactly one adapter commit when Apply is pressed.
        auto multiState = before;
        auto secondParameter = described;
        secondParameter.id = "track3.filter.mode";
        multiState.snapshot.parameters.push_back(secondParameter);
        int multiCommits = 0;
        std::vector<codeact::Change> multiChanges;
        auto multiTransport = std::make_unique<ScriptTransport>(std::vector<codeact::Completion> {
            tools({ call("inspect-many", "return host.snapshot().parameters.map(p=>p.id);"),
                    call("stage-one", "host.proposeParameters({'track2.filter.mode':2}); return 'first part';") }),
            tools({ call("stage-two", "host.proposeParameters({'track3.filter.mode':0}); return 'second part';") }),
            finalText()
        });
        fable::FableAgent multiAgent([&] { return multiState; },
            [&](const auto& changes, juce::String&) { ++multiCommits; multiChanges = changes; return true; },
            std::move(multiTransport));
        check(multiAgent.submit("Change both modes after inspecting them", endpoint), "multi-tool submit");
        waitTurn(multiAgent);
        check(multiAgent.poll().result && multiAgent.poll().result->modelCalls == 3
                  && multiAgent.poll().result->changes.size() == 2 && multiCommits == 0,
              "multiple tool calls did not stage one combined unapplied proposal");
        const auto& multiLog = multiAgent.conversation().back().activityLog;
        check(multiLog.contains("MODEL RESPONSE") && multiLog.contains("execute_js:")
                  && multiLog.contains("Tool result:") && multiLog.contains("Ready for review."),
              "full model-visible activity transcript is incomplete");
        check(multiAgent.apply(error) && multiCommits == 1 && multiChanges.size() == 2,
              "combined tool-call proposal did not apply in one commit");
        check(!multiAgent.apply(error) && multiCommits == 1, "combined proposal applied twice");

        std::cout << "PASS: " << checks << " Fable agent controller checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
