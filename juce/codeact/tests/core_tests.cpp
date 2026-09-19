#include "Agent.h"
#include <algorithm>
#include <iostream>
#include <vector>

using namespace codeact;
static int checks = 0;
static void check(bool value, const char* message = "check failed") {
    ++checks; if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F fn) {
    bool threw = false; try { fn(); } catch (const std::exception&) { threw = true; }
    check(threw, "expected an exception");
}
static Snapshot snapshot() {
    return {"Test", {{"gainDb", "Gain", "dB", -24, 6, 0, 0.25, {}},
                     {"wet", "Wet", "ratio", 0, 1, 1, 0, {}},
                     {"mode", "Mode", "", 0, 1, 0, 1, {"A", "B"}}}};
}
static Json call(const char* id, const char* code, const char* name = "execute_js") {
    return object({{"id", id}, {"type", "function"}, {"function", object({
        {"name", name}, {"arguments", jsonText(object({{"code", code}}))}})}});
}
static Completion tools(std::initializer_list<Json> calls) {
    return {object({{"role", "assistant"}, {"content", Json()}, {"tool_calls", array(calls)}}), "tool_calls"};
}
static Completion finalText(const char* text = "Proposal ready for review.") {
    return {object({{"role", "assistant"}, {"content", text}}), "stop"};
}
static void streamDelta(ChatStreamAssembler& s, const Json& delta, Json finish = {}) {
    s.feed(jsonText(object({{"choices", array({object({{"index", 0}, {"delta", delta}, {"finish_reason", finish}})})}})));
}
static void checkTranscript(const juce::Array<Json>& messages) {
    std::set<juce::String> pending;
    for (const auto& message : messages) {
        const auto role = get(message, "role").toString();
        if (role == "tool") check(pending.erase(get(message, "tool_call_id").toString()) == 1, "orphan tool result");
        else {
            check(pending.empty(), "unanswered tool call");
            const auto c = get(message, "tool_calls");
            if (c.isArray()) for (const auto& item : *c.getArray()) pending.insert(get(item, "id").toString());
        }
    }
    check(pending.empty(), "request contains unresolved calls");
}
struct ScriptTransport : ChatTransport {
    std::vector<Completion> script;
    int index = 0;
    std::function<void(int, const juce::Array<Json>&)> inspect;
    explicit ScriptTransport(std::vector<Completion> s) : script(std::move(s)) {}
    Completion complete(const Endpoint&, const juce::Array<Json>& messages, const Limits&,
                        const StopState& stop, const TextSink& sink) override {
        stop.check(); checkTranscript(messages);
        if (inspect) inspect(index, messages);
        auto response = script.at(static_cast<std::size_t>(index++));
        if (sink && get(response.message, "content").isString()) sink(get(response.message, "content").toString());
        return response;
    }
};
static void testRuntime() {
    Limits l; l.jsTimeoutMs = 60; l.maxPromiseJobs = 1000;
    StopState stop(60000); QuickJsRuntime js(l); const auto state = snapshot();
    auto run = [&](const char* source) { return js.execute(source, state, stop); };
    auto r = run("memory.answer = 41; return memory.answer;");
    check(r.ok && r.valueJson == "41", "numeric return");
    r = run("memory.answer++; print('value', memory.answer); return await Promise.resolve(memory.answer);");
    check(r.ok && r.valueJson == "42" && r.printed.contains("42"), "memory and await");
    r = run("const s = host.snapshot(); s.parameters[0].value = 999; return host.snapshot().parameters[0].value;");
    check(r.ok && r.valueJson == "0", "snapshot must be a copy");
    r = run("return [host.measureAudio(), host.readMeters()];");
    auto unavailableObservations = parseJson(r.valueJson);
    check(r.ok && !static_cast<bool>(get(unavailableObservations[0], "available"))
            && !static_cast<bool>(get(unavailableObservations[1], "available")),
          "default observations should be unavailable");
    auto observed = state;
    observed.audio = object({{"available", true}, {"capturedAt", 123},
                             {"window", object({{"seconds", 1.0}})},
                             {"rmsDbfs", -12.5}, {"units", "dBFS"}});
    observed.meters = object({{"available", true}, {"capturedAt", 123},
                              {"tracks", array({object({{"peakDbfs", -3.0}})})},
                              {"units", "dBFS"}});
    r = js.execute("const a=host.measureAudio(),m=host.readMeters();"
                   "a.rmsDbfs=99;m.tracks[0].peakDbfs=99;"
                   "return [host.measureAudio().rmsDbfs,host.readMeters().tracks[0].peakDbfs];",
                   observed, stop);
    const auto frozenObservations = parseJson(r.valueJson);
    check(r.ok && (double)frozenObservations[0] == -12.5 && (double)frozenObservations[1] == -3.0,
          "audio and meter observations must be immutable copies");
    r = run("const s=host.snapshot().parameters; return [s[0].step,s[2].choices[1]];");
    const auto metadata = parseJson(r.valueJson);
    check(r.ok && (double)metadata[0] == 0.25 && metadata[1].toString() == "B",
          "parameter metadata available to JS");
    r = run("host.proposeParameters({gainDb:-6, wet:0.5}); return {ready:true};");
    check(r.ok && r.changes.size() == 2, "validated proposal");
    check(r.changes[0].id == "gainDb" && r.changes[0].before == 0 && r.changes[0].after == -6);
    r = run("host.proposeParameters({gainDb:-3}); throw new Error('broken');");
    check(!r.ok && r.runtimeReset && r.changes.empty() && r.error.contains("broken"), "rollback on throw");
    r = run("return memory.answer === undefined;");
    check(r.ok && r.valueJson == "true", "VM reset after failure");
    for (const auto* code : {
        "host.proposeParameters({unknown:1});",
        "host.proposeParameters({gainDb:99});",
        "host.proposeParameters({gainDb:NaN});",
        "host.proposeParameters({gainDb:'-6'});",
        "host.proposeParameters({mode:0.5});",
        "host.measureAudio(1);",
        "host.readMeters('live');",
        "host.snapshot = () => 42;",
        "return await import('std');",
        "return await new Promise(() => {});",
        "Promise.resolve().then(() => { throw new Error('detached'); }); return 1;",
        "const a = {}; a.a = a; return a;",
        "while (true) {}",
        "function f() { Promise.resolve().then(f); } f(); return 1;"}) {
        r = run(code);
        check(!r.ok && r.runtimeReset && r.changes.empty(), "unsafe/failed execution did not fail");
    }
    r = run("return [typeof fetch, typeof require, typeof std, typeof os, typeof Atomics,"
            "typeof SharedArrayBuffer, typeof audio, typeof meters];");
    check(r.ok && parseJson(r.valueJson).size() == 8, "unavailable ambient APIs");
    const auto unavailable = parseJson(r.valueJson);
    for (const auto& v : *unavailable.getArray()) check(v.toString() == "undefined");
    // Explicit zero-argument print flood: still bounded by native output limits.
    Limits tiny = l; tiny.maxToolOutputBytes = 512;
    QuickJsRuntime small(tiny);
    r = small.execute("for(let i=0;i<10000;i++) print();", state, stop);
    check(!r.ok && r.printed.getNumBytesAsUTF8() <= 512, "native stdout cap");
    Limits heap = l; heap.jsHeapBytes = 2 * 1024 * 1024; heap.jsTimeoutMs = 2000;
    QuickJsRuntime limited(heap);
    r = limited.execute("memory.a=[]; for(;;) memory.a.push(new Array(10000).fill(1));", state, stop);
    check(!r.ok && r.runtimeReset, "allocation loop limit");
    StopState cancelled(10000); cancelled.cancelled = true;
    check(!js.execute("return 1;", state, cancelled).ok, "pre-cancelled execution");
}
static void testProtocol() {
    Limits l;
    check(parseJson("42").toString() == "42", "JUCE scalar JSON adapter");
    check(parseJson("null").isVoid());
    rejects([] { (void) parseJson("{}garbage"); });
    auto invalidObservation = snapshot();
    invalidObservation.audio = object({{"available", true},
                                       {"peak", std::numeric_limits<double>::infinity()}});
    rejects([&] { invalidObservation.validate(); });
    invalidObservation = snapshot();
    invalidObservation.meters = object({{"available", true}, {"payload", juce::String::repeatedString("x", 70 * 1024)}});
    rejects([&] { invalidObservation.validate(); });
    Endpoint e; e.model = "fixture";
    juce::Array<Json> history; history.add(object({{"role", "user"}, {"content", "test"}}));
    auto body = makeRequest(e, history, l);
    check(get(body, "tools").size() == 1);
    check(!has(body, "parallel_tool_calls"), "optional parallel field should be omitted by default");
    check(get(get(get(body, "tools")[0], "function"), "name").toString() == "execute_js");
    e.sendParallelToolCalls = true;
    body = makeRequest(e, history, l);
    check(has(body, "parallel_tool_calls") && !static_cast<bool>(get(body, "parallel_tool_calls")),
          "explicit sequential-provider hint missing");
    e.sendParallelToolCalls = false;
    e.extraBodyJson = R"({"tools":[]})";
    rejects([&] { (void) makeRequest(e, history, l); });
    e.extraBodyJson = "{}"; e.baseUrl = "http://example.org/v1";
    rejects([&] { (void) makeRequest(e, history, l); });
    e.baseUrl = "http://127.0.0.1:8080/v1"; e.allowLoopbackHttp = true;
    (void) makeRequest(e, history, l);

    ChatStreamAssembler s(l);
    streamDelta(s, object({{"tool_calls", array({object({{"index", 0}, {"id", "call_1"}, {"type", "function"},
        {"function", object({{"name", "exec"}, {"arguments", "{\"code\":\"ret"}})}})})}}));
    streamDelta(s, object({{"tool_calls", array({object({{"index", 0}, {"function", object({
        {"name", "ute_js"}, {"arguments", "urn 42;\"}"}})}})})}}));
    const auto details = array({object({{"type", "reasoning.encrypted"}, {"data", "opaque"}, {"index", 0}})});
    streamDelta(s, object({{"reasoning_details", details}}));
    streamDelta(s, object(), "tool_calls");
    streamDelta(s, object({{"content", ""}}), "tool_calls"); // OpenRouter's accounting frame.
    s.feed("[DONE]");
    const auto complete = s.finish();
    const auto fn = get(get(complete.message, "tool_calls")[0], "function");
    check(get(fn, "name").toString() == "execute_js");
    check(get(parseJson(get(fn, "arguments").toString()), "code").toString() == "return 42;");
    check(jsonText(get(complete.message, "reasoning_details")) == jsonText(details));
    rejects([&] { s.feed("[DONE]"); });
    {
        ChatStreamAssembler truncated(l);
        streamDelta(truncated, object({{"content", "complete-looking text"}}), "stop");
        rejects([&] { (void) truncated.finish(); });
    }
    {
        ChatStreamAssembler length(l);
        streamDelta(length, object({{"content", "partial"}}), "length"); length.feed("[DONE]");
        rejects([&] { (void) length.finish(); });
    }
    {
        ChatStreamAssembler error(l);
        rejects([&] { error.feed(R"({"error":{"message":"provider failed"},"choices":[]})"); });
    }
    {
        ChatStreamAssembler usage(l);
        streamDelta(usage, object({{"content", "done"}}), "stop");
        usage.feed(R"({"choices":[],"usage":{"total_tokens":42}})");
        usage.feed("[DONE]"); check(usage.finish().finishReason == "stop");
    }
    {
        auto invalid = tools({call("duplicate", "return 1"), call("duplicate", "return 2")});
        rejects([&] { validateCompletion(invalid, l); });
    }
}
static void testLoop() {
    Limits l; StopState stop(60000); Session session(l);
    Turn turn; turn.endpoint.model = "fixture"; turn.prompt = "Make a proposal"; turn.snapshot = snapshot();
    auto first = tools({call("a", "memory.amount=6; return host.snapshot();"),
                        call("b", "host.proposeParameters({gainDb:-memory.amount}); return 'staged';")});
    put(first.message, "reasoning_details", array({object({{"type", "reasoning.encrypted"}, {"data", "keep-me"}})}));
    ScriptTransport client({first, finalText()});
    client.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 1) {
            check(messages.size() == 5, "assistant plus both tool outputs");
            check(get(messages[2], "reasoning_details").size() == 1, "reasoning preserved in history");
            check(get(messages[3], "tool_call_id").toString() == "a");
            check(get(messages[4], "tool_call_id").toString() == "b");
            check(static_cast<bool>(get(parseJson(get(messages[4], "content").toString()), "ok")));
        }
    };
    const auto result = session.run(turn, client, stop);
    check(result.ok && result.modelCalls == 2 && result.changes.size() == 1);
    check(result.changes.front().after == -6 && turn.snapshot.parameters[0].value == 0, "staged, not applied");
    ScriptTransport unknown({tools({call("c", "return 1", "run_shell")}), finalText("Cannot run shell.")});
    unknown.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 1) {
            const auto observation = parseJson(get(messages.getLast(), "content").toString());
            check(!static_cast<bool>(get(observation, "ok")), "unknown tool rejected");
        }
    };
    check(session.run(turn, unknown, stop).changes.empty());
    // A provider failure after a successful JS call releases no proposal,
    // rolls back only the incomplete turn, and resets the VM.
    Turn failedTurn = turn; failedTurn.prompt = "This failed turn must not survive";
    ScriptTransport interrupted({tools({call("d", "memory.poison=123; host.proposeParameters({wet:0.5});")})});
    rejects([&] { (void) session.run(failedTurn, interrupted, stop); }); // exhausted fixture simulates disconnect
    Turn recoveredTurn = turn;
    recoveredTurn.prompt = "Continue after the failure";
    recoveredTurn.hostContext = "The earlier gain proposal was applied.";
    recoveredTurn.snapshot.parameters[0].value = 4;
    // Reusing d also proves failed-turn call IDs were rolled back.
    ScriptTransport recovered({tools({call("d", "return [memory.poison === undefined, memory.amount === undefined, host.snapshot().parameters[0].value];")}),
                               finalText("Continued conversation.")});
    recovered.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 0) {
            check(messages.size() == 13, "completed history was not retained transactionally");
            check(get(messages[1], "content").toString() == "Make a proposal", "earlier user turn missing");
            check(get(messages[5], "role").toString() == "assistant", "earlier assistant turn missing");
            for (const auto& message : messages)
                check(!get(message, "content").toString().contains("failed turn must not survive"),
                      "failed user message leaked into history");
            check(get(messages[10], "role").toString() == "system"
                    && get(messages[10], "content").toString().contains("JavaScript memory was reset"),
                  "runtime reset observation missing");
            check(get(messages[11], "role").toString() == "user"
                    && get(messages[11], "content").toString().contains("PLUGIN HOST OBSERVATION")
                    && get(messages[11], "content").toString().contains("proposal was applied"),
                  "host observation missing");
            check(get(messages[12], "content").toString() == recoveredTurn.prompt,
                  "follow-up user prompt missing");
        }
        if (step == 1) {
            const auto observation = parseJson(get(messages.getLast(), "content").toString());
            const auto values = get(observation, "value");
            check(static_cast<bool>(values[0]) && static_cast<bool>(values[1]), "VM reset too");
            check((int)values[2] == 4, "follow-up did not receive the fresh snapshot");
        }
    };
    check(session.run(recoveredTurn, recovered, stop).ok);

    Turn resetTurn = turn; resetTurn.prompt = "Explicit reset"; resetTurn.newSession = true;
    ScriptTransport resetClient({finalText("Reset.")});
    resetClient.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 0) check(messages.size() == 2, "newSession retained old history");
    };
    check(session.run(resetTurn, resetClient, stop).ok);

    Turn switched = turn; switched.prompt = "Different endpoint"; switched.endpoint.model = "other-fixture";
    ScriptTransport switchedClient({finalText("Switched.")});
    switchedClient.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 0) check(messages.size() == 2, "endpoint switch retained old history");
    };
    check(session.run(switched, switchedClient, stop).ok);
    Limits one = l; one.maxModelCalls = 2; Session bounded(one);
    ScriptTransport endless({tools({call("f", "memory.budget=1; host.proposeParameters({gainDb:-6});"),
                                    call("g", "host.proposeParameters({wet:0.25});")}),
                               tools({call("h", "host.proposeParameters({mode:1});")})});
    rejects([&] { (void) bounded.run(turn, endless, stop); });
    Turn afterBudget = turn; afterBudget.prompt = "Continue after budget exhaustion";
    ScriptTransport budgetRecovered({tools({call("f", "return memory.budget === undefined;")}),
                                     finalText("Recovered without partial changes.")});
    budgetRecovered.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 1) {
            const auto observation = parseJson(get(messages.getLast(), "content").toString());
            check(static_cast<bool>(get(observation, "value")), "budget-exhausted batch executed partially");
        }
    };
    check(bounded.run(afterBudget, budgetRecovered, stop).changes.empty(),
          "budget-exhausted turn released partial changes");
}
static void testMultipleToolCalls() {
    Limits limits; StopState stop(60000); Session session(limits);
    Turn turn; turn.endpoint.model = "fixture"; turn.prompt = "Adjust several parameters"; turn.snapshot = snapshot();
    turn.snapshot.audio = object({{"available", true}, {"rmsDbfs", -10.0}, {"units", "dBFS"}});
    ScriptTransport transport({
        tools({call("multi-a", "const s=host.snapshot();host.proposeParameters({gainDb:-6});return s.parameters.length;"),
               call("multi-b", "const a=host.measureAudio();host.proposeParameters({gainDb:-3,mode:1});return a;"),
               call("multi-c", "host.proposeParameters({wet:0.25}); return 'third';")}),
        tools({call("multi-d", "host.proposeParameters({gainDb:-2,wet:0.75}); return 'refined';")}),
        finalText("Combined proposal ready.")
    });
    transport.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 1) {
            check(messages.size() == 6, "same-response tool batch was not fully serialized");
            check(get(messages[3], "tool_call_id").toString() == "multi-a"
                    && get(messages[4], "tool_call_id").toString() == "multi-b"
                    && get(messages[5], "tool_call_id").toString() == "multi-c",
                  "same-response tool order changed");
            const auto measured = parseJson(get(messages[4], "content").toString());
            check(static_cast<bool>(get(get(measured, "value"), "available"))
                    && (double)get(get(measured, "value"), "rmsDbfs") == -10.0,
                  "measurement tool did not return frozen audio data");
        }
        if (step == 2)
            check(get(messages.getLast(), "tool_call_id").toString() == "multi-d",
                  "follow-up tool result missing");
    };
    std::vector<juce::String> activities;
    const auto result = session.run(turn, transport, stop, [&](const Progress& p) {
        if (p.activity.isNotEmpty()) activities.push_back(p.activity);
    });
    check(result.ok && result.modelCalls == 3 && result.changes.size() == 3,
          "multi-call turn did not complete with merged changes");
    std::map<juce::String, double> values;
    for (const auto& change : result.changes) values[change.id] = change.after;
    check(values["gainDb"] == -2 && values["wet"] == 0.75 && values["mode"] == 1,
          "latest successful proposal did not win across calls");
    check(std::any_of(activities.begin(), activities.end(), [](const auto& activity) {
              return activity.contains("model step 1, tool 1/3");
          }) && std::any_of(activities.begin(), activities.end(), [](const auto& activity) {
              return activity.contains("model step 1, tool 2/3");
          }) && std::any_of(activities.begin(), activities.end(), [](const auto& activity) {
              return activity.contains("model step 1, tool 3/3");
          }) && std::any_of(activities.begin(), activities.end(), [](const auto& activity) {
              return activity.contains("model step 2, tool 1/1");
          }), "multi-call progress did not identify model steps and tools");
}
static void testCompletedToolCallIdReuse() {
    Limits limits; Session session(limits);
    Turn turn; turn.endpoint.model = "fixture"; turn.prompt = "First turn"; turn.snapshot = snapshot();
    StopState firstStop(60000);
    ScriptTransport first({tools({call("provider-call-1", "return 'first';")}), finalText("First complete.")});
    check(session.run(turn, first, firstStop).ok, "first reused-ID turn failed");

    turn.prompt = "Second turn";
    StopState secondStop(60000);
    ScriptTransport second({tools({call("provider-call-1", "return 'second';")}), finalText("Second complete.")});
    check(session.run(turn, second, secondStop).ok,
          "completed tool-call ID could not be reused by a later response");
}
static void testCompletionGateRollback() {
    Limits limits; Session session(limits);
    Turn turn; turn.endpoint.model = "fixture"; turn.prompt = "Completed baseline"; turn.snapshot = snapshot();
    StopState baselineStop(60000);
    ScriptTransport baseline({finalText("Baseline complete.")});
    check(session.run(turn, baseline, baselineStop).ok, "completion-gate baseline failed");

    Turn cancelled = turn; cancelled.prompt = "Cancel at completion boundary";
    StopState cancelledStop(60000);
    ScriptTransport late({tools({call("late-call", "memory.late=1;host.proposeParameters({wet:0.5});")}),
                          finalText("Would have completed.")});
    bool gateReached = false;
    rejects([&] {
        (void) session.run(cancelled, late, cancelledStop, ProgressSink{}, [&] {
            gateReached = true;
            cancelledStop.cancelled.store(true, std::memory_order_relaxed);
            cancelledStop.check();
        });
    });
    check(gateReached, "completion gate was not reached");

    Turn recovered = turn; recovered.prompt = "Continue after boundary cancellation";
    StopState recoveredStop(60000);
    ScriptTransport recovery({tools({call("late-call", "return memory.late === undefined;")}),
                              finalText("Recovered.")});
    recovery.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 0) {
            check(messages.size() == 5, "completion-gate rollback lost completed history");
            check(get(messages[1], "content").toString() == "Completed baseline",
                  "completion-gate rollback lost the prior completed turn");
            check(get(messages[3], "role").toString() == "system"
                    && get(messages[3], "content").toString().contains("JavaScript memory was reset"),
                  "completion-gate rollback omitted the runtime-reset observation");
            for (const auto& message : messages)
                check(!get(message, "content").toString().contains("Cancel at completion boundary"),
                      "completion-gate rollback retained the cancelled prompt");
        }
        if (step == 1) {
            const auto observation = parseJson(get(messages.getLast(), "content").toString());
            check(static_cast<bool>(get(observation, "value")),
                  "completion-gate rollback retained cancelled JavaScript memory");
        }
    };
    const auto result = session.run(recovered, recovery, recoveredStop);
    check(result.ok && result.changes.empty(),
          "completion-gate rollback released a cancelled proposal");
}
struct CancelAfterToolTransport : ChatTransport {
    int calls = 0;
    Completion complete(const Endpoint&, const juce::Array<Json>&, const Limits&,
                        const StopState& stop, const TextSink&) override {
        if (calls++ == 0)
            return tools({call("cancelled-call", "memory.cancelledTurn=1; host.proposeParameters({wet:0.5});")});
        const_cast<StopState&>(stop).cancelled.store(true, std::memory_order_relaxed);
        stop.check();
        throw std::runtime_error("unreachable");
    }
};
static void testCancellationRollback() {
    Limits l; Session session(l);
    Turn turn; turn.endpoint.model = "fixture"; turn.prompt = "Completed before cancellation"; turn.snapshot = snapshot();
    StopState firstStop(60000);
    ScriptTransport first({finalText("Completed.")});
    check(session.run(turn, first, firstStop).ok);

    Turn cancelled = turn; cancelled.prompt = "Cancelled turn must not survive";
    StopState cancelledStop(60000); CancelAfterToolTransport cancelling;
    rejects([&] { (void) session.run(cancelled, cancelling, cancelledStop); });

    Turn followup = turn; followup.prompt = "Continue after cancellation";
    StopState followupStop(60000);
    ScriptTransport recovered({tools({call("cancelled-call", "return memory.cancelledTurn === undefined;")}),
                               finalText("Recovered.")});
    recovered.inspect = [&](int step, const juce::Array<Json>& messages) {
        if (step == 0) {
            check(messages.size() == 5, "cancel rollback lost completed history");
            check(get(messages[3], "role").toString() == "system"
                    && get(messages[3], "content").toString().contains("JavaScript memory was reset"),
                  "cancel reset observation missing");
            for (const auto& message : messages)
                check(!get(message, "content").toString().contains("Cancelled turn must not survive"),
                      "cancelled prompt leaked into history");
        }
        if (step == 1) {
            const auto observation = parseJson(get(messages.getLast(), "content").toString());
            check(static_cast<bool>(get(observation, "value")), "cancelled JS memory survived");
        }
    };
    const auto result = session.run(followup, recovered, followupStop);
    check(result.ok && result.changes.empty(), "cancelled turn released partial changes");
}
struct BlockingTransport : ChatTransport {
    juce::WaitableEvent entered;
    Completion complete(const Endpoint&, const juce::Array<Json>&, const Limits&,
                        const StopState& stop, const TextSink&) override {
        entered.signal();
        while (!stop.stopped()) juce::Thread::sleep(1);
        stop.check(); throw std::runtime_error("unreachable");
    }
};
static void testWorker() {
    auto fake = std::make_unique<BlockingTransport>(); auto* raw = fake.get();
    Agent worker({}, std::move(fake));
    Turn turn; turn.endpoint.model = "fixture"; turn.prompt = "wait"; turn.snapshot = snapshot();
    check(worker.submit(turn)); check(raw->entered.wait(2000));
    check(!worker.submit(turn), "busy worker accepted a second turn");
    worker.cancel();
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (worker.poll().busy && Clock::now() < deadline) juce::Thread::sleep(1);
    const auto state = worker.poll();
    check(!state.busy && state.result && !state.result->ok && state.result->changes.empty());
    worker.shutdown();
}
int main() {
    try {
        testProtocol(); testRuntime(); testLoop(); testMultipleToolCalls();
        testCompletedToolCallIdReuse(); testCompletionGateRollback();
        testCancellationRollback(); testWorker();
        std::cout << "PASS: " << checks << " JUCE/QuickJS core checks\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
