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
#include <expected>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4,
    UNKNOWN = 5
};

inline bool isAtLeast(LogLevel level, LogLevel minimum) noexcept {
    if (level == LogLevel::UNKNOWN || minimum == LogLevel::UNKNOWN) return false;
    return static_cast<int>(level) >= static_cast<int>(minimum);
}

inline bool isError(LogLevel level) noexcept {
    return level == LogLevel::ERROR || level == LogLevel::CRITICAL;
}

// Supported types for structured data
using LogValue = std::variant<std::string, int64_t, uint64_t, double, bool>;

struct LogEntryJsonOptions {
    bool pretty = false;
    bool include_source = true;
    bool include_thread = true;
    bool include_tracing = true;
};

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

    // Tracing context
    std::string trace_id;
    std::string span_id;

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
    
    // JSON Deserialization
    static std::expected<LogEntry, std::string> fromJson(std::string_view json_str);

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
    LogEntry& withTraceContext(std::string_view tid, std::string_view sid);

    LogEntry clonedWithTag(std::string_view tag) const;

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

    // Comparison (C++20)
    std::strong_ordering operator<=>(const LogEntry& other) const;
    bool operator==(const LogEntry& other) const;

    using JsonOptions = LogEntryJsonOptions;
    std::string toJson(const JsonOptions& options = {}) const;

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
