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
#include <shared_mutex>

#if __has_include(<stacktrace>)
#include <stacktrace>
#define HAS_STACKTRACE 1
#endif

enum class SeverityLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4,
    FATAL = 5,
    UNKNOWN = 6
};

inline bool isAtLeast(SeverityLevel level, SeverityLevel minimum) noexcept {
    if (level == SeverityLevel::UNKNOWN || minimum == SeverityLevel::UNKNOWN) return false;
    return static_cast<int>(level) >= static_cast<int>(minimum);
}

inline bool isError(SeverityLevel level) noexcept {
    return level == SeverityLevel::ERROR || level == SeverityLevel::FATAL;
}

#include <cstddef> // for std::byte

// Supported types for structured data
struct LogValue;
using LogList = std::vector<LogValue>;
using LogObject = std::map<std::string, LogValue>;
using LogBinary = std::vector<std::byte>;

using LogValueBase = std::variant<
    std::monostate,
    bool,
    int64_t,
    uint64_t,
    double,
    std::string,
    LogBinary,
    std::shared_ptr<LogList>,
    std::shared_ptr<LogObject>
>;

namespace LogUtils {
    std::string base64Encode(const std::vector<std::byte>& data);
    std::vector<std::byte> base64Decode(std::string_view encoded);
}

struct LogValue : LogValueBase {
    using LogValueBase::LogValueBase;
    
    // Helper constructors
    LogValue(LogList list);
    LogValue(LogObject obj);
    LogValue(LogBinary bin);
    
    // Explicit conversion helpers
    bool isList() const;
    bool isObject() const;
    bool isBinary() const;
    const LogList& asList() const;
    const LogObject& asObject() const;
    const LogBinary& asBinary() const;

    template<typename T>
    std::optional<T> as() const {
        if (std::holds_alternative<T>(*this)) {
            return std::get<T>(*this);
        }
        // Handle numeric conversions for floating point types
        if constexpr (std::is_floating_point_v<T>) {
            if (std::holds_alternative<int64_t>(*this)) 
                return static_cast<T>(std::get<int64_t>(*this));
            if (std::holds_alternative<uint64_t>(*this)) 
                return static_cast<T>(std::get<uint64_t>(*this));
        }
        return std::nullopt;
    }

    // Navigation (Iteration 1)
    std::optional<LogValue> find(std::string_view path) const;
    LogValue& operator[](std::string_view key);
    LogValue& operator[](size_t index);

    // Deep cloning (Iteration 1)
    LogValue deepClone() const;
};

struct LogEntry {
    enum class TimestampFormat { Default, ISO8601, UnixMillis };

    struct JsonOptions {
        enum class Precision { Seconds, Millis, Micros, Nanos };

        bool pretty = false;
        bool include_source = true;
        bool include_thread = true;
        bool include_tracing = true;
        bool include_resources = true; // New: include resource attributes
        bool exclude_empty = false; 
        TimestampFormat timestamp_format = TimestampFormat::Default;
        Precision precision = Precision::Millis; 
    };

    struct FormatSpecifier {
        std::string pattern;
    };

    struct FilterCriteria {
        std::optional<SeverityLevel> min_level;
        std::optional<SeverityLevel> max_level;
        std::optional<std::string> message_contains;
        std::optional<std::set<std::string>> tags_in;
    };

    static const JsonOptions defaultJsonOptions; 

    // Existing fields (Public API Compat)
    std::string timestamp;
    SeverityLevel level = SeverityLevel::UNKNOWN;
    std::string message;
    std::string raw_line;

    // New fields
    std::chrono::system_clock::time_point time_point;

    // Context info
    uint64_t process_id = 0; 
    std::string host_name;   
    std::string app_name;    
    std::string source_file;
    std::string source_function;
    int source_line = 0;
    std::string thread_id;
    std::string stacktrace; // New (Iteration 1)

    // Tracing context
    std::string trace_id;
    std::string span_id;

    // Structured data
    std::map<std::string, LogValue> attributes;
    std::map<std::string, LogValue> resources; // New: Environmental attributes
    std::set<std::string, std::less<>> tags; 

    // Constructors
    LogEntry();

    // Static helpers
    static SeverityLevel parseLevel(std::string_view level_str);
    static std::string_view levelToString(SeverityLevel level);
    static uint64_t currentProcessId(); 
    static std::string currentHostName(); 

    // Global resources management (Iteration 1)
    static void setGlobalResource(std::string key, LogValue value);
    static void clearGlobalResources();

    // Factory methods
    static LogEntry create(SeverityLevel level, std::string_view message, 
                          std::source_location loc = std::source_location::current());
    static LogEntry fromMap(const std::map<std::string, LogValue>& data); 
    
    // JSON Deserialization
    static std::expected<LogEntry, std::string> fromJson(std::string_view json_str);

    // Fluent API
    LogEntry& withLevel(SeverityLevel l);
    LogEntry& withMessage(std::string_view msg);
    LogEntry& withMetadata(); 
    LogEntry& withAttribute(std::string key, LogValue value);
    LogEntry& withAttributes(std::initializer_list<std::pair<const std::string, LogValue>> attrs); 
    LogEntry& withAttributes(const std::map<std::string, LogValue>& attrs); 
    LogEntry& withResource(std::string key, LogValue value); // New
    LogEntry& withResources(const std::map<std::string, LogValue>& res); // New
    LogEntry& withEnvironment(); // New
    LogEntry& withSystemInfo(); // New
    LogEntry& withMemoryInfo(); // New (Iteration 1)
    LogEntry& withNetworkInfo(); // New (Iteration 1)
    LogEntry& withStacktrace(size_t skip = 1, size_t max_depth = 20); // New (Iteration 1)
    LogEntry& withProcessId(uint64_t pid); 
    LogEntry& withHost(std::string_view host); 
    LogEntry& withApp(std::string_view app);   
    LogEntry& withThreadId(std::string_view tid);
    LogEntry& withThreadId(std::thread::id tid);
    LogEntry& withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional = true);
    LogEntry& withSource(std::source_location loc = std::source_location::current());
    LogEntry& withTag(std::string_view tag);
    LogEntry& withTags(std::initializer_list<std::string_view> tags);
    LogEntry& withException(const std::exception& e);
    LogEntry& withTraceContext(std::string_view tid, std::string_view sid);

    // Attribute manipulation
    LogEntry& removeAttribute(const std::string& key); 
    LogEntry& clearAttributes(); 
    LogEntry& mergeAttributes(const LogEntry& other); 

    LogEntry clonedWithTag(std::string_view tag) const;

    // Methods
    bool parseTime();
    std::string generatedTimestampString(bool include_fractional = true) const;
    void setAttribute(const std::string& key, const std::string& value); 
    void setAttribute(const std::string& key, const char* value); 
    void setAttribute(const std::string& key, LogValue value); 
    std::string getAttributeAsString(const std::string& key) const; 
    
    bool hasAttribute(const std::string& key) const;
    std::optional<LogValue> getAttribute(const std::string& key) const;

    std::string format(const FormatSpecifier& specifier) const;
    bool matches(const FilterCriteria& criteria) const;

    template<typename Archive>
    void serialize(Archive& ar);
    
    std::string summary() const; // New
    
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
    std::map<std::string, std::string> flattenedAttributes(std::string_view separator = ".") const; // New (Iteration 1)

    // Comparison (C++20)
    std::strong_ordering operator<=>(const LogEntry& other) const;
    bool operator==(const LogEntry& other) const;

    std::string toJson(const JsonOptions& options = defaultJsonOptions) const;
    std::string toKvp() const; // New (Iteration 1)

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
SeverityLevel parseLogLevel(const std::string& level_str);

#endif // LOG_ENTRY_H
