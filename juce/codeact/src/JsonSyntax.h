#pragma once
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace codeact {
// Strict framing/syntax validation before JUCE's deliberately permissive JSON
// parser. Also prevents pathological depth and overflowing integer lexemes.
class JsonSyntax {
public:
    static void validate(std::string_view text) {
        JsonSyntax parser(text);
        parser.value(0); parser.whitespace();
        if (parser.pos != text.size()) bad("Trailing data after JSON value");
    }
private:
    explicit JsonSyntax(std::string_view s) : text(s) {}
    std::string_view text;
    std::size_t pos = 0;
    [[noreturn]] static void bad(const char* message) { throw std::runtime_error(message); }
    char peek() const { return pos < text.size() ? text[pos] : '\0'; }
    void whitespace() { while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') ++pos; }
    bool take(char c) { if (peek() == c) { ++pos; return true; } return false; }
    void expect(char c) { if (!take(c)) bad("Invalid JSON syntax"); }
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    void string() {
        expect('"');
        for (;;) {
            if (pos >= text.size()) bad("Unterminated JSON string");
            const auto c = static_cast<unsigned char>(text[pos++]);
            if (c == '"') return;
            if (c < 0x20) bad("Unescaped control character in JSON");
            if (c == '\\') {
                if (pos >= text.size()) bad("Incomplete JSON escape");
                const char e = text[pos++];
                if (e == 'u') {
                    for (int i = 0; i < 4; ++i) {
                        const char h = peek();
                        if (!digit(h) && !(h >= 'a' && h <= 'f') && !(h >= 'A' && h <= 'F'))
                            bad("Invalid JSON Unicode escape");
                        ++pos;
                    }
                } else if (e != '"' && e != '\\' && e != '/' && e != 'b' && e != 'f'
                           && e != 'n' && e != 'r' && e != 't') bad("Invalid JSON escape");
            }
        }
    }
    void number() {
        take('-');
        const auto first = pos;
        if (!take('0')) { if (!digit(peek())) bad("Invalid JSON number"); while (digit(peek())) ++pos; }
        // JUCE 8's integer accumulator is int64_t. Larger exact values should
        // cross this boundary as strings, not as JSON integer lexemes.
        if (pos - first > 18) bad("JSON integer part exceeds 18 digits; encode it as a string");
        if (take('.')) { if (!digit(peek())) bad("Invalid JSON fraction"); while (digit(peek())) ++pos; }
        if (take('e') || take('E')) {
            if (!take('+')) take('-');
            if (!digit(peek())) bad("Invalid JSON exponent");
            const auto exponentStart = pos;
            while (digit(peek())) ++pos;
            if (pos - exponentStart > 4) bad("JSON exponent exceeds four digits");
        }
    }
    void literal(std::string_view word) {
        if (text.substr(pos, word.size()) != word) bad("Invalid JSON literal");
        pos += word.size();
    }
    void value(int depth) {
        if (depth > 64) bad("JSON nesting limit exceeded");
        whitespace();
        if (take('{')) {
            whitespace(); if (take('}')) return;
            do { whitespace(); string(); whitespace(); expect(':'); value(depth + 1); whitespace(); }
            while (take(','));
            expect('}');
        } else if (take('[')) {
            whitespace(); if (take(']')) return;
            do { value(depth + 1); whitespace(); } while (take(','));
            expect(']');
        } else if (peek() == '"') string();
        else if (peek() == 't') literal("true");
        else if (peek() == 'f') literal("false");
        else if (peek() == 'n') literal("null");
        else if (peek() == '-' || digit(peek())) number();
        else bad("Expected a JSON value");
    }
};
} // namespace codeact
