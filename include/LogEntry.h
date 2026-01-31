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

#include <vector> // Required for std::vector<uint8_t>

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
    std::vector<uint8_t>,           // New: Binary data
    std::chrono::nanoseconds,        // New: High-precision duration
    std::shared_ptr<LogList>,
    std::shared_ptr<LogObject>
>;

struct LogValue : LogValueBase {
    using LogValueBase::LogValueBase;
    
    // Helper constructors
    LogValue(LogList list);
    LogValue(LogObject obj);

    // New: Convenience constructors for various types
    LogValue(std::string_view s);
    LogValue(const char* s);
    LogValue(std::chrono::system_clock::time_point tp);
    template <typename Rep, typename Period>
    LogValue(std::chrono::duration<Rep, Period> d) : LogValueBase(std::chrono::duration_cast<std::chrono::nanoseconds>(d)) {}

    // New: Type checking methods
    bool isMonostate() const noexcept;
    bool isBool() const noexcept;
    bool isInt64() const noexcept;
    bool isUint64() const noexcept;
    bool isDouble() const noexcept;
    bool isString() const noexcept;
    bool isBinary() const noexcept;
    bool isDuration() const noexcept;

    // Explicit conversion helpers (now returning std::optional)
    std::optional<const LogList&> asList() const;
    std::optional<const LogObject&> asObject() const;

    // New: Accessors for specific types (returning std::optional<T>)
    std::optional<bool> asBool() const;
    std::optional<int64_t> asInt64() const;
    std::optional<uint64_t> asUint64() const;
    std::optional<double> asDouble() const;
    std::optional<const std::string&> asString() const;
    std::optional<const std::vector<uint8_t>&> asBinary() const;
    std::optional<std::chrono::nanoseconds> asDuration() const;

    // New: Comparison operators
    std::strong_ordering operator<=>(const LogValue& other) const = default;
    bool operator==(const LogValue& other) const = default;
}; // Closing brace for LogValue

// Global function or friend method within LogValue
std::ostream& operator<<(std::ostream& os, const LogValue& value);

struct LogEntry {
    enum class TimestampFormat { Default, ISO8601, UnixMillis };

    struct JsonOptions {
        enum class Precision { Seconds, Millis, Micros, Nanos };
        enum class Timezone { Local, UTC }; // New
        enum class BinaryEncoding { Hex, Base64 }; // New

        bool pretty = false;
        bool include_source = true;
        bool include_thread = true;
        bool include_tracing = true;
        bool exclude_empty = false; // New: skip empty attributes/tags
        TimestampFormat timestamp_format = TimestampFormat::Default;
        Precision precision = Precision::Millis; // New: configurable precision
        Timezone timezone = Timezone::UTC; // New: Default to UTC for machine-readable logs
        BinaryEncoding binary_encoding = BinaryEncoding::Hex; // New: Default to Hex encoding
        std::optional<std::string> custom_timestamp_format = std::nullopt; // New: optional strftime string

        // New: Structured data pretty-printing control
        bool pretty_structured_data = false; // Apply pretty printing to LogList/LogObject values
        int indent_level = 2; // Indentation for pretty printing (global and for structured data)

        // New: Field inclusion/exclusion filters
        // A set of field names (e.g., "message", "level", "attributes.my_attr") to explicitly include.
        // If empty, all default fields are included (unless excluded by exclude_fields).
        std::set<std::string> include_fields; 
        // A set of field names to explicitly exclude. Exclusions override inclusions.
        std::set<std::string> exclude_fields; 

        // New: Options for sanitization (e.g., control characters in strings)
        bool sanitize_strings = true; // Replace non-printable characters or escape them
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
    std::string thread_name;

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
    LogEntry& withAttribute(std::string key, LogValue value); // Existing

    // New: withAttribute overloads for common types
    LogEntry& withAttribute(std::string_view key, bool value);
    LogEntry& withAttribute(std::string_view key, int64_t value);
    LogEntry& withAttribute(std::string_view key, uint64_t value);
    LogEntry& withAttribute(std::string_view key, double value);
    LogEntry& withAttribute(std::string_view key, std::string_view value);
    LogEntry& withAttribute(std::string_view key, const char* value);
    LogEntry& withAttribute(std::string_view key, std::chrono::system_clock::time_point value);
    template <typename Rep, typename Period>
    LogEntry& withAttribute(std::string_view key, std::chrono::duration<Rep, Period> value);
    LogEntry& withAttribute(std::string_view key, LogList value); // Takes ownership
    LogEntry& withAttribute(std::string_view key, LogObject value); // Takes ownership
    LogEntry& withAttributes(std::initializer_list<std::pair<const std::string, LogValue>> attrs); // Iteration 1
    LogEntry& withAttributes(const std::map<std::string, LogValue>& attrs); // Iteration 1
    LogEntry& withProcessId(uint64_t pid); // Iteration 1
    LogEntry& withHost(std::string_view host); // Iteration 1
    LogEntry& withApp(std::string_view app);   // Iteration 1
    LogEntry& withThreadId(std::string_view tid);
    LogEntry& withThreadId(std::thread::id tid);
    LogEntry& withThreadName(std::string_view name); // New
    LogEntry& withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional = true);
    LogEntry& withSource(std::source_location loc = std::source_location::current());
    LogEntry& withTag(std::string_view tag);
    LogEntry& withTags(std::initializer_list<std::string_view> tags);
    LogEntry& withException(const std::exception& e);
    LogEntry& withTraceContext(std::string_view tid, std::string_view sid);
    LogEntry& withSystemLoad();  // New: Captures system load averages
    LogEntry& withMemoryUsage(); // New: Captures current process RSS
    LogEntry& captureCallStack(std::string_view attribute_key = "call_stack",
                               size_t skip_frames = 0,
                               size_t max_frames = 64); // New

    // Attribute manipulation
    LogEntry& removeAttribute(const std::string& key); // Iteration 1
    LogEntry& clearAttributes(); // Iteration 1
    LogEntry& mergeAttributes(const LogEntry& other); // Iteration 1
    LogEntry& merge(const LogEntry& other, bool overwrite_attributes = true, bool merge_tags = true); // New

    LogEntry clonedWithTag(std::string_view tag) const;

    // Methods
    bool parseTime();
    std::string generatedTimestampString(const JsonOptions& options = defaultJsonOptions) const;
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

    // Returns value as double if it is int, uint, or double.
    std::optional<double> getAsDouble(const std::string& key) const;

    // Returns value as int64 if it is any numeric type (potential precision loss).
    std::optional<int64_t> getAsInt(const std::string& key) const;

    bool hasTag(std::string_view tag) const;
    bool isValid() const noexcept; // Iteration 1
    int getSeverityValue() const; // Iteration 1

    std::map<std::string, LogValue> toMap() const; // Iteration 1

    // Comparison (C++20)
    std::strong_ordering operator<=>(const LogEntry& other) const;
    bool operator==(const LogEntry& other) const;

    std::string toJson(const JsonOptions& options = defaultJsonOptions) const;

    // New: Flexible stream output
    void toStream(std::ostream& os, const JsonOptions& options = defaultJsonOptions) const;

    // Stream Support
    friend std::ostream& operator<<(std::ostream& os, const LogEntry& entry);
};

// New: std::hash specialization for LogEntry
namespace std {
    template <>
    struct hash<LogEntry> {
        size_t operator()(const LogEntry& entry) const noexcept;
    };
}

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
