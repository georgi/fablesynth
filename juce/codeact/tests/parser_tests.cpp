#include "SseFramer.h"
#include "JsonSyntax.h"
#include <iostream>
#include <random>
#include <vector>

using namespace codeact;
static int checks = 0;
static void check(bool condition) {
    ++checks;
    if (!condition) throw std::runtime_error("Check failed: " + std::to_string(checks));
}
template<class F> void rejects(F fn) {
    bool threw = false;
    try { fn(); } catch (const std::exception&) { threw = true; }
    check(threw);
}
int main() {
    try {
        const std::string input = std::string("\xEF\xBB\xBF")
            + ": OPENROUTER PROCESSING\r\n"
              "event: message\r\nid: ignored\r\nretry: 1000\r\n"
              "data: {\"text\":\"caf\xC3\xA9 \xF0\x9F\x8E\xB5\"}\r\n\r\n"
              "data: first\ndata: second\n\n"
              "data:\r\r"
              "data: [DONE]\n\n";
        const std::vector<std::string> expected = {
            "{\"text\":\"caf\xC3\xA9 \xF0\x9F\x8E\xB5\"}", "first\nsecond", "", "[DONE]"
        };
        // Every possible constant chunk size, including UTF-8 and CRLF splits.
        for (std::size_t step = 1; step <= input.size(); ++step) {
            std::vector<std::string> events;
            SseFramer parser([&](const std::string& e) { events.push_back(e); });
            for (std::size_t pos = 0; pos < input.size(); pos += step)
                parser.feed(std::string_view(input).substr(pos, step));
            check(events == expected); check(!parser.hasUnfinishedEvent());
        }
        // Every two-chunk split position.
        for (std::size_t pos = 0; pos <= input.size(); ++pos) {
            std::vector<std::string> events;
            SseFramer parser([&](const std::string& e) { events.push_back(e); });
            parser.feed(std::string_view(input).substr(0, pos));
            parser.feed(std::string_view(input).substr(pos));
            check(events == expected);
        }
        // Deterministic randomized chunk boundaries.
        std::mt19937 rng(0xC0DEAC7);
        for (int trial = 0; trial < 1000; ++trial) {
            std::vector<std::string> events;
            SseFramer parser([&](const std::string& e) { events.push_back(e); });
            for (std::size_t pos = 0; pos < input.size();) {
                const auto size = std::min<std::size_t>(1 + rng() % 31, input.size() - pos);
                parser.feed(std::string_view(input).substr(pos, size)); pos += size;
            }
            check(events == expected);
        }
        {
            std::vector<std::string> events;
            SseFramer parser([&](const std::string& e) { events.push_back(e); });
            parser.feed(": comment\n\ndata: unfinished");
            check(events.empty()); check(parser.hasUnfinishedEvent());
            parser.feed("\n"); check(events.empty()); check(parser.hasUnfinishedEvent());
            parser.feed("\n"); check(events == std::vector<std::string>{"unfinished"});
        }
        rejects([] { SseFramer p([](const auto&) {}, 8); p.feed("data: excessive\n\n"); });
        rejects([] { SseFramer p([](const auto&) {}, 16); p.feed("data: 123456789\ndata: 123456789\n\n"); });
        rejects([] { SseFramer p([](const auto&) { throw std::runtime_error("consumer"); }); p.feed("data: x\n\n"); });

        for (const auto* valid : {"null", "true", "false", "0", "-0", "42", "-12.5e+2", "1e-324",
                "\"hello\\nworld\"", "[]", "{}", "[1,2,{\"x\":[null,true]}]",
                " { \"x\": \"\\uD834\\uDD1E\" } ", "{\"x\":\"braces[]{}\"}", "\"\\\\\\\"\""}) {
            JsonSyntax::validate(valid); check(true);
        }
        for (const auto* invalid : {"", " ", "undefined", "NaN", "Infinity", "{} junk", "{}{}", "null x",
                "[1,]", "{\"x\":1,}", "'single'", "\"bad\\a\"", "\"bad\nstring\"", "\"\\u123Z\"",
                "01", "-", "+1", ".2", "1.", "1e", "1e+", "--2", "{\"x\" 1}", "[",
                "1e999999999999999999999999", "1e-99999", "9999999999999999999", "tru", "falsee", "\"unterminated"})
            rejects([&] { JsonSyntax::validate(invalid); });
        rejects([] { JsonSyntax::validate(std::string(70, '[') + "0" + std::string(70, ']')); });
        std::cout << "PASS: " << checks << " dependency-free parser checks\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
