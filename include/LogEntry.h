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
struct LogValue;
using LogList = std::vector<LogValue>;
using LogObject = std::map<std::string, LogValue>;

using LogValueBase = std::variant<
    std::monostate,
    bool,
    int64_t,
    uint64_t,
    double,
    std::string,
    std::shared_ptr<LogList>,
    std::shared_ptr<LogObject>
>;

struct LogValue : LogValueBase {
    using LogValueBase::LogValueBase;
    
    // Helper constructors
    LogValue(LogList list);
    LogValue(LogObject obj);
    
    // Explicit conversion helpers
    bool isList() const;
    bool isObject() const;
    const LogList& asList() const;
    const LogObject& asObject() const;
};

struct LogEntry {
    enum class TimestampFormat { Default, ISO8601, UnixMillis };

    struct JsonOptions {
        enum class Precision { Seconds, Millis, Micros, Nanos };

        bool pretty = false;
        bool include_source = true;
        bool include_thread = true;
        bool include_tracing = true;
        bool exclude_empty = false; // New: skip empty attributes/tags
        TimestampFormat timestamp_format = TimestampFormat::Default;
        Precision precision = Precision::Millis; // New: configurable precision
    };

    static const JsonOptions defaultJsonOptions; // New: Default options for JSON serialization

    // Existing fields (Public API Compat)
    std::string timestamp;
    LogLevel level = LogLevel::UNKNOWN;
    std::string message;
    std::string raw_line;

    // New fields
    std::chrono::system_clock::time_point time_point;

    // Context info
    uint64_t process_id = 0; // Iteration 1
    std::string host_name;   // New: Host name
    std::string app_name;    // New: Application/Service name
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
    static uint64_t currentProcessId(); // Iteration 1
    static std::string currentHostName(); // Iteration 1

    // Factory methods
    static LogEntry create(LogLevel level, std::string_view message, 
                          std::source_location loc = std::source_location::current());
    static LogEntry fromMap(const std::map<std::string, LogValue>& data); // Iteration 1
    
    // JSON Deserialization
    static std::expected<LogEntry, std::string> fromJson(std::string_view json_str);

    // Fluent API
    LogEntry& withLevel(LogLevel l);
    LogEntry& withMessage(std::string_view msg);
    LogEntry& withMetadata(); // Captures PID, Host, App, Thread, and Time if not set
    LogEntry& withAttribute(std::string key, LogValue value);
    LogEntry& withAttributes(std::initializer_list<std::pair<const std::string, LogValue>> attrs); // Iteration 1
    LogEntry& withAttributes(const std::map<std::string, LogValue>& attrs); // Iteration 1
    LogEntry& withProcessId(uint64_t pid); // Iteration 1
    LogEntry& withHost(std::string_view host); // Iteration 1
    LogEntry& withApp(std::string_view app);   // Iteration 1
    LogEntry& withThreadId(std::string_view tid);
    LogEntry& withThreadId(std::thread::id tid);
    LogEntry& withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional = true);
    LogEntry& withSource(std::source_location loc = std::source_location::current());
    LogEntry& withTag(std::string_view tag);
    LogEntry& withTags(std::initializer_list<std::string_view> tags);
    LogEntry& withException(const std::exception& e);
    LogEntry& withTraceContext(std::string_view tid, std::string_view sid);

    // Attribute manipulation
    LogEntry& removeAttribute(const std::string& key); // Iteration 1
    LogEntry& clearAttributes(); // Iteration 1
    LogEntry& mergeAttributes(const LogEntry& other); // Iteration 1

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
        if (it == attributes.end()) return std::nullopt;
        
        // Iteration 1: Handle std::monostate (null)
        if (std::holds_alternative<std::monostate>(it->second)) {
            return std::nullopt;
        }

        // If T is double, and value is int64_t/uint64_t, allow conversion
        if constexpr (std::is_floating_point_v<T>) {
            if (std::holds_alternative<int64_t>(it->second)) 
                return static_cast<T>(std::get<int64_t>(it->second));
            if (std::holds_alternative<uint64_t>(it->second)) 
                return static_cast<T>(std::get<uint64_t>(it->second));
        }
        
        // Standard variant check
        if (std::holds_alternative<T>(it->second)) {
            return std::get<T>(it->second);
        }
        return std::nullopt;
    }

    template<typename T>
    bool hasAttributeValue(const std::string& key, const T& value) const {
        auto opt_val = getAttributeAs<T>(key);
        return opt_val.has_value() && *opt_val == value;
    }

    bool hasTag(std::string_view tag) const;
    bool isValid() const noexcept; // Iteration 1
    int getSeverityValue() const; // Iteration 1

    std::map<std::string, LogValue> toMap() const; // Iteration 1

    // Comparison (C++20)
    std::strong_ordering operator<=>(const LogEntry& other) const;
    bool operator==(const LogEntry& other) const;

    std::string toJson(const JsonOptions& options = defaultJsonOptions) const;

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
