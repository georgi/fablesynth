#include "QuickJsRuntime.h"
#include <quickjs.h>
#include <algorithm>
#include <cstring>
#include <utility>

namespace codeact {
namespace {
struct Value {
    JSContext* ctx;
    JSValue value;
    Value(JSContext* c, JSValue v) : ctx(c), value(v) {}
    ~Value() { JS_FreeValue(ctx, value); }
    Value(const Value&) = delete;
    Value& operator=(const Value&) = delete;
    JSValue release() { auto result = value; value = JS_UNDEFINED; return result; }
};
}
struct QuickJsRuntime::Impl {
    explicit Impl(Limits l) : limits(std::move(l)) { limits.validate(); }
    ~Impl() { destroy(); }
    Limits limits;
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    const StopState* stop = nullptr;
    const Snapshot* snapshot = nullptr;
    Clock::time_point deadline;
    bool interruptFired = false, trackerFailed = false;
    int jobs = 0;
    juce::String printed;
    std::map<juce::String, Change> staged;
    struct Rejection { JSValue promise, reason; };
    std::vector<Rejection> rejections;

    static Impl& self(JSContext* c) { return *static_cast<Impl*>(JS_GetContextOpaque(c)); }
    static int interrupt(JSRuntime*, void* opaque) noexcept {
        auto& s = *static_cast<Impl*>(opaque);
        if (s.stop && (s.stop->stopped() || Clock::now() >= s.deadline)) {
            s.interruptFired = true;
            return 1;
        }
        return 0;
    }
    static JSModuleDef* denyModule(JSContext* c, const char*, void*) {
        JS_ThrowReferenceError(c, "Module loading is disabled");
        return nullptr;
    }
    static void trackRejection(JSContext* c, JSValueConst promise, JSValueConst reason,
                               bool handled, void* opaque) noexcept {
        auto& s = *static_cast<Impl*>(opaque);
        try {
            const auto found = std::find_if(s.rejections.begin(), s.rejections.end(),
                [&](const Rejection& item) { return JS_IsStrictEqual(c, item.promise, promise); });
            if (handled) {
                if (found != s.rejections.end()) {
                    JS_FreeValue(c, found->promise);
                    JS_FreeValue(c, found->reason);
                    s.rejections.erase(found);
                }
            } else if (found == s.rejections.end()) {
                // Bound native bookkeeping as well as the JS heap.
                if (s.rejections.size() >= 128) { s.trackerFailed = true; return; }
                s.rejections.push_back({ JS_DupValue(c, promise), JS_DupValue(c, reason) });
            }
        } catch (...) { s.trackerFailed = true; } // Never unwind through QuickJS C frames.
    }
    void clearRejections() noexcept {
        for (const auto& r : rejections) {
            JS_FreeValue(ctx, r.promise); JS_FreeValue(ctx, r.reason);
        }
        rejections.clear();
    }
    void destroy() noexcept {
        stop = nullptr; snapshot = nullptr;
        if (rt) JS_SetHostPromiseRejectionTracker(rt, nullptr, nullptr);
        if (ctx) { clearRejections(); JS_FreeContext(ctx); ctx = nullptr; }
        if (rt) { JS_FreeRuntime(rt); rt = nullptr; }
    }
    void budget() const {
        if (stop) stop->check();
        if (stop && (interruptFired || Clock::now() >= deadline))
            fail("JavaScript execution deadline exceeded");
    }
    juce::String stringValue(JSValueConst v, std::size_t byteLimit) {
        std::size_t length = 0;
        const char* text = JS_ToCStringLen(ctx, &length, v);
        if (!text) fail("JavaScript string conversion failed");
        if (length > byteLimit) {
            JS_FreeCString(ctx, text);
            fail("JavaScript output byte limit exceeded");
        }
        auto result = juce::String::fromUTF8(text, static_cast<int>(length));
        JS_FreeCString(ctx, text);
        return result;
    }
    juce::String describe(JSValueConst error) {
        // Do not call an arbitrary error object's toString(). Even reading a
        // message getter is covered by the active QuickJS interrupt budget.
        if (JS_IsString(error)) return stringValue(error, 4096);
        Value message(ctx, JS_GetPropertyStr(ctx, error, "message"));
        if (JS_IsString(message.value)) return stringValue(message.value, 4096);
        if (JS_IsException(message.value)) {
            Value ignored(ctx, JS_GetException(ctx));
        }
        return "JavaScript exception";
    }
    [[noreturn]] void jsFailure() {
        Value error(ctx, JS_GetException(ctx));
        budget();
        fail(describe(error.value));
    }
    void checkValue(JSValueConst v) { if (JS_IsException(v)) jsFailure(); }
    void create() {
        if (rt) return;
        rt = JS_NewRuntime();
        if (!rt) fail("Cannot allocate QuickJS runtime");
        JS_SetMemoryLimit(rt, limits.jsHeapBytes);
        JS_SetMaxStackSize(rt, limits.jsStackBytes);
        JS_SetCanBlock(rt, false); // In particular, never permit Atomics.wait().
        JS_SetInterruptHandler(rt, interrupt, this);
        JS_SetModuleLoaderFunc(rt, nullptr, denyModule, nullptr);
        JS_SetHostPromiseRejectionTracker(rt, trackRejection, this);
        ctx = JS_NewContext(rt);
        if (!ctx) { destroy(); fail("Cannot allocate QuickJS context"); }
        JS_SetContextOpaque(ctx, this);
        rejections.reserve(128);
        {
            Value global(ctx, JS_GetGlobalObject(ctx));
            Value host(ctx, JS_NewObject(ctx));
            checkValue(host.value);
            if (JS_SetPropertyStr(ctx, host.value, "snapshot",
                                  JS_NewCFunction(ctx, snapshotFn, "snapshot", 0)) < 0
                || JS_SetPropertyStr(ctx, host.value, "measureAudio",
                                     JS_NewCFunction(ctx, measureAudioFn, "measureAudio", 0)) < 0
                || JS_SetPropertyStr(ctx, host.value, "readMeters",
                                     JS_NewCFunction(ctx, readMetersFn, "readMeters", 0)) < 0
                || JS_SetPropertyStr(ctx, host.value, "proposeParameters",
                                     JS_NewCFunction(ctx, proposeFn, "proposeParameters", 1)) < 0)
                jsFailure();
            if (JS_DefinePropertyValueStr(ctx, global.value, "host", host.release(), 0) < 0
                || JS_DefinePropertyValueStr(ctx, global.value, "print",
                    JS_NewCFunction(ctx, printFn, "print", 1), 0) < 0
                || JS_DefinePropertyValueStr(ctx, global.value, "memory", JS_NewObject(ctx), 0) < 0)
                jsFailure();
            // No quickjs-libc, std/os modules, filesystem, process, require,
            // sockets, fetch, timers, dynamic native loader, or bytecode input.
            constexpr auto bootstrap =
                "Object.freeze(host); Object.freeze(print);"
                "delete globalThis.SharedArrayBuffer; delete globalThis.Atomics;";
            Value result(ctx, JS_Eval(ctx, bootstrap, std::strlen(bootstrap),
                                     "<bootstrap>", JS_EVAL_TYPE_GLOBAL));
            checkValue(result.value);
        }
    }
    juce::String serialize(JSValueConst v) {
        if (JS_IsUndefined(v)) return "null";
        Value json(ctx, JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED));
        checkValue(json.value);
        if (!JS_IsString(json.value)) fail("Return a JSON-serializable value or undefined");
        auto text = stringValue(json.value, limits.maxToolOutputBytes);
        // Protect the downstream native JSON decoder too.
        (void) parseJson(text, limits.maxToolOutputBytes);
        budget();
        return text;
    }
    JSValue fromJson(const Json& value) {
        const auto text = jsonText(value);
        auto result = JS_ParseJSON(ctx, text.toRawUTF8(), text.getNumBytesAsUTF8(), "<host-data>");
        return result; // An exception sentinel propagates naturally to the JS caller.
    }
    void requireActive() {
        if (!stop || !snapshot) fail("Host capability called outside an execution");
        budget();
    }
    static JSValue snapshotFn(JSContext* c, JSValueConst, int argc, JSValueConst*) {
        try {
            auto& s = self(c); s.requireActive();
            if (argc != 0) return JS_ThrowTypeError(c, "host.snapshot takes no arguments");
            return s.fromJson(s.snapshot->toJson());
        } catch (const std::exception& e) { return JS_ThrowInternalError(c, "%s", e.what()); }
        catch (...) { return JS_ThrowInternalError(c, "Native snapshot failure"); }
    }
    static JSValue measureAudioFn(JSContext* c, JSValueConst, int argc, JSValueConst*) {
        try {
            auto& s = self(c); s.requireActive();
            if (argc != 0) return JS_ThrowTypeError(c, "host.measureAudio takes no arguments");
            return s.fromJson(s.snapshot->audio);
        } catch (const std::exception& e) { return JS_ThrowInternalError(c, "%s", e.what()); }
        catch (...) { return JS_ThrowInternalError(c, "Native audio measurement failure"); }
    }
    static JSValue readMetersFn(JSContext* c, JSValueConst, int argc, JSValueConst*) {
        try {
            auto& s = self(c); s.requireActive();
            if (argc != 0) return JS_ThrowTypeError(c, "host.readMeters takes no arguments");
            return s.fromJson(s.snapshot->meters);
        } catch (const std::exception& e) { return JS_ThrowInternalError(c, "%s", e.what()); }
        catch (...) { return JS_ThrowInternalError(c, "Native meter read failure"); }
    }
    static JSValue proposeFn(JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
        try {
            auto& s = self(c); s.requireActive();
            if (argc != 1) return JS_ThrowTypeError(c, "Pass one object mapping parameter IDs to values");
            const auto input = parseJson(s.serialize(argv[0]), s.limits.maxToolOutputBytes);
            const auto* obj = input.getDynamicObject();
            if (!obj || obj->getProperties().size() == 0)
                return JS_ThrowTypeError(c, "Expected a nonempty parameter object");
            if (obj->getProperties().size() > 64)
                return JS_ThrowRangeError(c, "Too many parameter changes");
            auto next = s.staged; // Validate the entire proposal before staging any part.
            const auto& props = obj->getProperties();
            for (int i = 0; i < props.size(); ++i) {
                const auto id = props.getName(i).toString();
                const auto value = props.getValueAt(i);
                const auto p = std::find_if(s.snapshot->parameters.begin(), s.snapshot->parameters.end(),
                    [&](const Parameter& parameter) { return parameter.id == id; });
                if (p == s.snapshot->parameters.end()) fail("Unknown parameter: " + id);
                if (!isNumber(value)) fail("Parameter must be a finite number: " + id);
                const auto number = static_cast<double>(value);
                if (!std::isfinite(number) || number < p->minimum || number > p->maximum)
                    fail("Parameter is out of range: " + id);
                if (p->step > 0) {
                    const auto steps = (number - p->minimum) / p->step;
                    if (std::abs(steps - std::round(steps)) > 1.0e-7)
                        fail("Parameter is not on an allowed step: " + id);
                }
                next[id] = Change{ id, p->value, number };
            }
            Value receipt(c, s.fromJson(object({{"status", "staged"}, {"applied", false},
                                               {"requiresUserApproval", true}})));
            if (JS_IsException(receipt.value)) return receipt.release();
            s.budget();
            s.staged = std::move(next);
            return receipt.release();
        } catch (const std::exception& e) { return JS_ThrowInternalError(c, "%s", e.what()); }
        catch (...) { return JS_ThrowInternalError(c, "Native proposal failure"); }
    }
    static JSValue printFn(JSContext* c, JSValueConst, int argc, JSValueConst* argv) {
        try {
            auto& s = self(c); s.requireActive();
            if (argc > 16) return JS_ThrowRangeError(c, "print accepts at most 16 arguments");
            juce::String line;
            for (int i = 0; i < argc; ++i) {
                if (i) line += " ";
                line += JS_IsString(argv[i]) ? s.stringValue(argv[i], s.limits.maxToolOutputBytes)
                                            : s.serialize(argv[i]);
                if (s.printed.getNumBytesAsUTF8() + line.getNumBytesAsUTF8() + 1 > s.limits.maxToolOutputBytes)
                    fail("print output byte limit exceeded");
            }
            if (s.printed.getNumBytesAsUTF8() + line.getNumBytesAsUTF8() + 1 > s.limits.maxToolOutputBytes)
                fail("print output byte limit exceeded");
            s.printed += line + "\n";
            return JS_UNDEFINED;
        } catch (const std::exception& e) { return JS_ThrowInternalError(c, "%s", e.what()); }
        catch (...) { return JS_ThrowInternalError(c, "Native print failure"); }
    }
    void drainJobs() {
        while (JS_IsJobPending(rt)) {
            budget();
            if (++jobs > limits.maxPromiseJobs) fail("Promise job limit exceeded");
            JSContext* jobContext = nullptr;
            const int status = JS_ExecutePendingJob(rt, &jobContext);
            if (status < 0) {
                // Only one context is ever created in this runtime.
                if (jobContext && jobContext != ctx) fail("Unexpected QuickJS job context");
                jsFailure();
            }
        }
        budget();
    }
    JsResult execute(const juce::String& code, const Snapshot& input, const StopState& token) {
        JsResult result;
        printed.clear(); staged.clear(); jobs = 0; interruptFired = false; trackerFailed = false;
        try {
            input.validate(); token.check();
            if (code.isEmpty() || code.getNumBytesAsUTF8() > limits.maxCodeBytes)
                fail("JavaScript source is empty or too large");
            create();
            clearRejections();
            stop = &token; snapshot = &input;
            deadline = std::min(token.deadline, Clock::now() + std::chrono::milliseconds(limits.jsTimeoutMs));
            {
                // Function-local let/const declarations can be reused on later calls.
                // Store persistent values explicitly on the immutable `memory` binding.
                const auto wrapped = "(async () => {\n\"use strict\";\n" + code + "\n})()";
                Value promise(ctx, JS_Eval(ctx, wrapped.toRawUTF8(), wrapped.getNumBytesAsUTF8(),
                                          "<execute_js>", JS_EVAL_TYPE_GLOBAL));
                checkValue(promise.value);
                if (!JS_IsPromise(promise.value)) fail("Execution must evaluate to the wrapper promise");
                drainJobs();
                const auto state = JS_PromiseState(ctx, promise.value);
                if (state == JS_PROMISE_PENDING)
                    fail("Promise cannot settle: no external event loop, timers, or I/O are exposed");
                Value value(ctx, JS_PromiseResult(ctx, promise.value));
                if (state == JS_PROMISE_REJECTED) fail(describe(value.value));
                result.valueJson = serialize(value.value);
                // toJSON/getters may have queued jobs. No runnable job leaks into
                // a later execute_js call with a different authorization context.
                drainJobs();
                if (trackerFailed) fail("Promise rejection tracking limit exceeded");
                if (!rejections.empty()) {
                    Value rejected(ctx, JS_DupValue(ctx, rejections.front().reason));
                    fail("Unhandled promise rejection: " + describe(rejected.value));
                }
                budget();
                for (const auto& item : staged) result.changes.push_back(item.second);
                result.printed = printed;
                result.ok = true;
                if (jsonText(result.toJson()).getNumBytesAsUTF8() > limits.maxToolOutputBytes)
                    fail("Combined tool result byte limit exceeded");
            } // Free ALL local JSValues before a possible reset.
        } catch (const std::exception& e) {
            result.ok = false; result.runtimeReset = true;
            result.error = juce::String::fromUTF8(e.what()).substring(0, 4096);
            result.printed = printed; result.valueJson = "null"; result.changes.clear();
            destroy(); // Failed execution cannot poison the next execution.
        } catch (...) {
            result.ok = false; result.runtimeReset = true;
            result.error = "Unexpected JavaScript runtime failure";
            result.changes.clear(); destroy();
        }
        stop = nullptr; snapshot = nullptr;
        return result;
    }
};
QuickJsRuntime::QuickJsRuntime(Limits limits) : impl(std::make_unique<Impl>(std::move(limits))) {}
QuickJsRuntime::~QuickJsRuntime() = default;
JsResult QuickJsRuntime::execute(const juce::String& code, const Snapshot& s, const StopState& stop) {
    return impl->execute(code, s, stop);
}
void QuickJsRuntime::reset() { impl->destroy(); }
} // namespace codeact
