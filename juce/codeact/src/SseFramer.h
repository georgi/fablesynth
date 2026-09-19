#pragma once
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace codeact {
// Byte-oriented: UTF-8 is decoded only after a complete SSE event. Never decode
// arbitrary network chunks independently, since a code point may span chunks.
class SseFramer {
public:
    using Consumer = std::function<void(const std::string&)>;
    explicit SseFramer(Consumer consumer, std::size_t limit = 256 * 1024)
        : consume(std::move(consumer)), maxBytes(limit) {}

    void feed(std::string_view bytes) {
        for (const char ch : bytes) {
            if (skipLF) { skipLF = false; if (ch == '\n') continue; }
            if (ch == '\r' || ch == '\n') {
                finishLine();
                skipLF = (ch == '\r');
            } else {
                if (line.size() >= maxBytes) throw std::runtime_error("SSE line too large");
                line.push_back(ch);
            }
        }
    }
    // The SSE specification does not dispatch an unfinished event at EOF.
    // The protocol layer separately requires [DONE] and a valid finish reason.
    bool hasUnfinishedEvent() const noexcept { return !line.empty() || hasData; }
private:
    void finishLine() {
        if (firstLine) {
            firstLine = false;
            if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        }
        if (line.empty()) {
            if (hasData) {
                data.pop_back(); // Remove the final newline appended below.
                auto event = std::move(data);
                data.clear(); hasData = false;
                consume(event);
            }
        } else if (line.front() != ':') {
            const auto colon = line.find(':');
            const auto field = line.substr(0, colon);
            if (field == "data") {
                auto value = colon == std::string::npos ? std::string{} : line.substr(colon + 1);
                if (!value.empty() && value.front() == ' ') value.erase(0, 1);
                if (data.size() + value.size() + 1 > maxBytes)
                    throw std::runtime_error("SSE event too large");
                data += value; data += '\n'; hasData = true;
            }
        }
        line.clear();
    }
    Consumer consume;
    std::size_t maxBytes;
    std::string line, data;
    bool firstLine = true, skipLF = false, hasData = false;
};
} // namespace codeact
