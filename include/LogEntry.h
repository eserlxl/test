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
#include <unordered_set>
#include <optional>
#include <functional>
#include <initializer_list>
#include <expected>

#include <vector> // Required for std::vector<uint8_t>

enum class ValueType {
    Monostate, String, Int64, UInt64, Double, Bool,
    Binary, Nanoseconds, List, Object
};

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    CRITICAL = 4,
    UNKNOWN = 5
};

// Forward declaration for LogEntry to allow nested types to be defined globally
struct LogEntry;

// Define JsonOptions outside LogEntry to resolve circular dependencies and allow
// LogValue to reference its enums.
struct LogEntryJsonOptions {
    enum class TimestampFormat { Default, ISO8601, UnixMillis };
    enum class Precision { Seconds, Millis, Micros, Nanos };
    enum class Timezone { Local, UTC };
    enum class BinaryEncoding { Hex, Base64 };

    bool pretty = false;
    bool include_source = true;
    bool include_thread = true;
    bool include_tracing = true;
    bool exclude_empty = false;
    TimestampFormat timestamp_format = TimestampFormat::Default;
    Precision precision = Precision::Millis;
    Timezone timezone = Timezone::UTC;
    BinaryEncoding binary_encoding = BinaryEncoding::Hex;
    std::optional<std::string> custom_timestamp_format = std::nullopt;

    bool pretty_structured_data = false;
    int indent_level = 2;

    std::set<std::string> include_fields;
    std::set<std::string> exclude_fields;

    bool sanitize_strings = true;
};

// Define TimestampFormatOptions outside LogEntry
struct LogEntryTimestampFormatOptions {
    // Reference to LogEntryJsonOptions::Precision and Timezone
    LogEntryJsonOptions::Precision precision = LogEntryJsonOptions::Precision::Millis;
    LogEntryJsonOptions::Timezone timezone = LogEntryJsonOptions::Timezone::Local;
    std::optional<std::string> custom_format = std::nullopt;
};

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

    // New: Type inspection methods
    ValueType type() const noexcept;
    bool is(ValueType t) const noexcept;
    template<typename T> bool is() const noexcept {
        return std::holds_alternative<T>(*this);
    }
    bool isNull() const noexcept;

    // Explicit conversion helpers (now returning std::optional<T*>, not T&)
    std::optional<const LogList*> asList() const;
    std::optional<const LogObject*> asObject() const;

    // New: Accessors for specific types (returning std::optional<T>)
    std::optional<bool> asBool() const;
    std::optional<int64_t> asInt64() const;
    std::optional<uint64_t> asUint64() const;
    std::optional<double> asDouble() const;
    std::optional<const std::string*> asString() const; // Changed to pointer
    std::optional<const std::vector<uint8_t>*> asBinary() const; // Changed to pointer
    std::optional<std::chrono::nanoseconds> asDuration() const;

    // New: Direct Access (throws on mismatch, like std::get)
    template<typename T> const T& get() const { return std::get<T>(static_cast<const LogValueBase&>(*this)); }
    template<typename T> T& get() { return std::get<T>(static_cast<LogValueBase&>(*this)); }

    // New: Optional Access (returns nullptr on mismatch, like std::get_if)
    template<typename T> const T* get_if() const noexcept { return std::get_if<T>(static_cast<const LogValueBase*>(this)); }
    template<typename T> T* get_if() noexcept { return std::get_if<T>(static_cast<LogValueBase*>(this)); }

    // Converts the stored value to its string representation.
    // binary_encoding applies only if the stored type is std::vector<uint8_t>.
    std::string toString(LogEntryJsonOptions::BinaryEncoding binary_encoding = LogEntryJsonOptions::BinaryEncoding::Hex) const;

    // New: Comparison operators
    std::partial_ordering operator<=>(const LogValue& other) const; // Remove default to implement manually
    bool operator==(const LogValue& other) const; // Remove default to implement manually
}; // Closing brace for LogValue

// Global function or friend method within LogValue
std::ostream& operator<<(std::ostream& os, const LogValue& value);

// New: std::hash specialization for LogValue
namespace std {
    template<> struct hash<LogValue> {
        size_t operator()(const LogValue& lv) const noexcept; // Added noexcept
    };
} // namespace std

// Forward declarations for internal implementation details of LogContext.
struct ContextFrame {
    std::map<std::string, LogValue> attributes;
    std::unordered_set<std::string> tags; // Use unordered_set for faster lookups during merging
};

// Thread-local stack to manage nested contexts.
extern thread_local std::vector<ContextFrame> current_context_stack;

namespace LogContext {
    /**
     * @brief RAII scope guard for applying contextual attributes and tags to LogEntry objects.
     *
     * When a LogContext::Scope object is constructed, it pushes its provided attributes and tags
     * onto a thread-local context stack. These attributes and tags are then automatically
     * applied to any LogEntry created via LogEntry::create() within this scope.
     *
     * If an attribute key already exists in an outer scope, the attribute provided by
     * this inner scope will temporarily override it. Tags are cumulative.
     *
     * When the LogContext::Scope object is destroyed (e.g., when it goes out of scope),
     * its associated attributes and tags are popped from the stack, restoring the previous context.
     *
     * This mechanism is strictly thread-local; context set in one thread does not affect others.
     */
    class Scope {
    public:
        /**
         * @brief Constructs a new LogContext::Scope and pushes context onto the stack.
         * @param attributes A map of attributes to apply within this scope.
         * @param tags A set of tags to apply within this scope.
         */
        Scope(std::map<std::string, LogValue> attributes = {},
              std::unordered_set<std::string> tags = {});

        /**
         * @brief Destroys the LogContext::Scope and pops its context from the stack.
         */
        ~Scope();

        // Disallow copying and moving to ensure correct RAII management of the context stack.
        // Copying or moving a Scope object would lead to incorrect stack behavior and potential memory issues.
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;
    };

    /**
     * @brief Applies all active attributes and tags from the thread-local context stack to the given LogEntry.
     *
     * This function is intended for internal use, primarily called by LogEntry::create().
     * It iterates through the current_context_stack and merges all attributes and tags
     * into the provided LogEntry object, respecting attribute overrides from inner scopes.
     *
     * @param entry The LogEntry object to apply context to.
     */
    void apply(LogEntry& entry);

} // namespace LogContext

struct LogEntry {
    // Using declarations to make external structs accessible via LogEntry::
    using JsonOptions = LogEntryJsonOptions;
    using TimestampFormat = JsonOptions::TimestampFormat;
    using TimestampFormatOptions = LogEntryTimestampFormatOptions;

    static const JsonOptions defaultJsonOptions;

    // Existing fields (Public API Compat)
    std::string timestamp;
    LogLevel level = LogLevel::UNKNOWN;
    std::string message;
    std::string raw_line;
    std::string event_id; // New: Dedicated event identifier

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

    // New: Static Timestamp Converters
    static std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        JsonOptions::TimestampFormat format_type, // Use JsonOptions directly
        const TimestampFormatOptions& opts = {}
    );

    static std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str
    );
    static std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        JsonOptions::TimestampFormat format_type // Use JsonOptions directly
    );


    // Static helpers
    static bool isAtLeast(LogLevel entryLevel, LogLevel minLevel);
    static bool isError(LogLevel level);
    static LogLevel parseLevel(std::string_view level_str);
    static std::string_view levelToString(LogLevel level);
    static uint64_t currentProcessId(); // Iteration 1
    static std::string currentHostName(); // Iteration 1

    // New: Global Default Metadata Providers
    using HostNameProvider = std::function<std::string()>;
    using AppNameProvider = std::function<std::string()>;

    static void setHostNameProvider(HostNameProvider provider);
    static void setAppNameProvider(AppNameProvider provider);
    static void resetHostNameProvider();
    static void resetAppNameProvider();

    // Factory methods
    static LogEntry create(LogLevel level, std::string_view message, 
                          std::source_location loc = std::source_location::current());
    static LogEntry fromMap(const std::map<std::string, LogValue>& data); // Iteration 1
    
    // JSON Deserialization
    static std::expected<LogEntry, std::string> fromJson(std::string_view json_str);

    // Fluent API
    LogEntry& withLevel(LogLevel l);
    LogEntry& withMessage(std::string_view msg);
    /**
     * @brief Sets the message using a structured format string, automatically extracting arguments as attributes.
     *        Similar to C++20's std::format, where {} placeholders can become attributes.
     *        Example: entry.withStructuredMessage("User {} logged in from IP {}", "alice", "192.168.1.1");
     *        This would set message to "User alice logged in from IP 192.168.1.1" and
     *        add attributes like "arg0"="alice", "arg1"="192.168.1.1".
     *        More advanced: entry.withStructuredMessage("User {user} logged in from IP {ip}", "alice", "192.168.1.1");
     *        This would add attributes "user"="alice", "ip"="192.168.1.1".
     * @param format The format string with placeholders.
     * @param args The arguments to format and potentially extract as attributes.
     * @return Reference to the LogEntry for chaining.
     */
    template <typename... Args>
    LogEntry& withStructuredMessage(std::string_view format, Args&&... args) {
        // Format the message
        this->message = std::vformat(format, std::make_format_args(args...));

        // Extract positional arguments as attributes (arg0, arg1, ...)
        int arg_idx = 0;
        // This fold expression will execute the lambda for each argument
        ( (this->attributes["arg" + std::to_string(arg_idx++)] = [](auto&& arg) -> LogValue {
            // Attempt to convert argument to LogValue.
            // This is a simplified conversion; a real implementation might need more specific handling
            // for different types (e.g., custom types, enums).
            if constexpr (std::is_convertible_v<decltype(arg), std::string_view>) {
                return LogValue(static_cast<std::string_view>(arg));
            } else if constexpr (std::is_integral_v<decltype(arg)> && !std::is_same_v<bool, std::decay_t<decltype(arg)>>) {
                if constexpr (std::is_signed_v<decltype(arg)>) {
                    return LogValue(static_cast<int64_t>(arg));
                } else {
                    return LogValue(static_cast<uint64_t>(arg));
                }
            } else if constexpr (std::is_floating_point_v<decltype(arg)>) {
                return LogValue(static_cast<double>(arg));
            } else if constexpr (std::is_same_v<bool, std::decay_t<decltype(arg)>>) {
                return LogValue(static_cast<bool>(arg));
            } else {
                // Fallback for types not directly convertible: use stringstream
                std::ostringstream oss;
                oss << arg;
                return LogValue(oss.str());
            }
        }(std::forward<Args>(args))), ...);

        return *this;
    }
    LogEntry& withMetadata(); // Captures PID, Host, App, Thread, and Time if not set
    LogEntry& withAttribute(std::string_view key, LogValue value); // Existing

    // New: withAttribute overloads for common types
    LogEntry& withAttribute(std::string_view key, bool value);
    LogEntry& withAttribute(std::string_view key, int64_t value);
    LogEntry& withAttribute(std::string_view key, uint64_t value);
    LogEntry& withAttribute(std::string_view key, double value);
    LogEntry& withAttribute(std::string_view key, std::string_view value);
    LogEntry& withAttribute(std::string_view key, const char* value);
    LogEntry& withAttribute(std::string_view key, std::chrono::system_clock::time_point value);
    template <typename Rep, typename Period>
    LogEntry& withAttribute(std::string_view key, std::chrono::duration<Rep, Period> value) {
        attributes[std::string(key)] = LogValue(value);
        return *this;
    }
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
    LogEntry& withEventId(std::string_view eventId); // New: Fluent method to set event_id
    LogEntry& withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional = true);
    LogEntry& withSource(std::source_location loc = std::source_location::current());
    LogEntry& withTag(std::string_view tag);
    LogEntry& withTags(std::initializer_list<std::string_view> tags);
    // New: Tag manipulation
    LogEntry& removeTag(std::string_view tag_name);
    LogEntry& clearTags();
    bool hasAllTags(const std::initializer_list<std::string_view>& tag_names) const;
    bool hasAnyTag(const std::initializer_list<std::string_view>& tag_names) const;
    const std::set<std::string, std::less<>>& getTags() const; // Read-only access to all tags

    // New: Cloning with tag manipulation
    LogEntry clonedWithoutTag(std::string_view tag_name) const;
    LogEntry clonedWithoutTags() const;

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

    // New: Cloning with Field Overrides
    LogEntry clonedWithLevel(LogLevel new_level) const;
    LogEntry clonedWithMessage(std::string_view new_message) const;
    LogEntry clonedWithAttribute(std::string_view key, LogValue value) const;
    LogEntry clonedWithAttributes(const std::map<std::string, LogValue>& attrs) const;
    LogEntry clonedWithoutAttribute(std::string_view key) const;
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

    static void setDefaultJsonOptions(const JsonOptions& opts);
    static const JsonOptions& getDefaultJsonOptions();


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



#endif // LOG_ENTRY_H