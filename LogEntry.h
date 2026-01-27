#ifndef LOG_ENTRY_H
#define LOG_ENTRY_H

#include <string>
#include <string_view>
#include <chrono>
#include <compare>
#include <format>
#include <map>
#include <variant>
#include <source_location>
#include <cstdint>
#include <ostream>
#include <thread>
#include <type_traits>
#include <set>
#include <optional>
#include <functional>
#include <initializer_list>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    CRITICAL,
    UNKNOWN
};

// Supported types for structured data
using LogValue = std::variant<std::string, int64_t, uint64_t, double, bool>;

struct LogEntry {
    // Existing fields (Public API Compat)
    std::string timestamp;
    LogLevel level = LogLevel::UNKNOWN;
    std::string message;
    std::string raw_line;

    // New fields
    std::chrono::system_clock::time_point time_point;

    // Context info
    std::string source_file;
    std::string source_function;
    int source_line = 0;
    std::string thread_id;

    // Structured data
    // BREAKING CHANGE: attributes now stores variants
    std::map<std::string, LogValue> attributes;
    std::set<std::string, std::less<>> tags; // Use transparent comparator

    // Constructors
    LogEntry();

    // Static helpers
    static LogLevel parseLevel(std::string_view level_str);
    static std::string_view levelToString(LogLevel level);

    // Factory method
    static LogEntry create(LogLevel level, std::string_view message, 
                          std::source_location loc = std::source_location::current());

    // Fluent API
    LogEntry& withLevel(LogLevel l);
    LogEntry& withMessage(std::string_view msg);
    LogEntry& withAttribute(std::string key, LogValue value);
    LogEntry& withThreadId(std::string_view tid);
    LogEntry& withThreadId(std::thread::id tid);
    LogEntry& withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional = true);
    LogEntry& withSource(std::source_location loc = std::source_location::current());
    LogEntry& withTag(std::string_view tag);
    LogEntry& withTags(std::initializer_list<std::string_view> tags);
    LogEntry& withException(const std::exception& e);

    // Methods
    bool parseTime();
    std::string generatedTimestampString(bool include_fractional = true) const;
    void setAttribute(const std::string& key, const std::string& value); // Compat shim
    void setAttribute(const std::string& key, const char* value); // Ambiguity resolver
    void setAttribute(const std::string& key, LogValue value); // New overload
    std::string getAttributeAsString(const std::string& key) const; // Helper
    
    bool hasAttribute(const std::string& key) const;
    std::optional<LogValue> getAttribute(const std::string& key) const;
    
    template<typename T>
    std::optional<T> getAttributeAs(const std::string& key) const {
        auto it = attributes.find(key);
        if (it != attributes.end() && std::holds_alternative<T>(it->second)) {
            return std::get<T>(it->second);
        }
        return std::nullopt;
    }

    bool hasTag(std::string_view tag) const;

    struct JsonOptions {
        bool pretty = false;
        bool include_source = true;
        bool include_thread = true;
    };
    std::string toJson(const JsonOptions& options = {}) const;

    // Comparison (C++20)
    std::strong_ordering operator<=>(const LogEntry& other) const;
    bool operator==(const LogEntry& other) const;

    // Stream Support
    friend std::ostream& operator<<(std::ostream& os, const LogEntry& entry);
};

// Formatter specialization
template <>
struct std::formatter<LogEntry> {
    constexpr auto parse(std::format_parse_context& ctx) {
        return ctx.begin();
    }

    auto format(const LogEntry& entry, std::format_context& ctx) const {
        return std::format_to(ctx.out(), "[{}] [{}] {}", 
            entry.timestamp.empty() ? entry.generatedTimestampString() : entry.timestamp, 
            LogEntry::levelToString(entry.level), 
            entry.message);
    }
};

// Backward compatibility
LogLevel parseLogLevel(const std::string& level_str);

#endif // LOG_ENTRY_H
