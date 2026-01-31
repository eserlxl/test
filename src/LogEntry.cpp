#include <LogEntry.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <typeinfo>
#include <limits> // Moved here
#include <string> // Ensure std::string is available for LogEntry::toJson key filtering
#include <cstdio> // For std::snprintf if needed

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <psapi.h> // For PROCESS_MEMORY_COUNTERS_EX
#else
#include <unistd.h>
#include <sys/resource.h> // For getrusage on Unix
#include <sys/sysinfo.h> // For get_load_statistics on Linux
#include <fstream> // For reading /proc files
// #include <unordered_set> // Included in LogEntry.h
#endif

// Static mutable variable for default JsonOptions
namespace { // Anonymous namespace for internal linkage
    LogEntry::JsonOptions s_defaultJsonOptions;
    std::optional<LogEntry::HostNameProvider> s_hostNameProvider;
    std::optional<LogEntry::AppNameProvider> s_appNameProvider;
}

// Definition for the thread-local context stack
thread_local std::vector<ContextFrame> current_context_stack;

const LogEntry::JsonOptions LogEntry::defaultJsonOptions;

// Implement static methods for JsonOptions management
void LogEntry::setDefaultJsonOptions(const JsonOptions& opts) {
    s_defaultJsonOptions = opts;
}

const LogEntry::JsonOptions& LogEntry::getDefaultJsonOptions() {
    return s_defaultJsonOptions;
}

// Implement static methods for metadata providers
void LogEntry::setHostNameProvider(HostNameProvider provider) {
    s_hostNameProvider = provider;
}

void LogEntry::setAppNameProvider(AppNameProvider provider) {
    s_appNameProvider = provider;
}

void LogEntry::resetHostNameProvider() {
    s_hostNameProvider.reset();
}

void LogEntry::resetAppNameProvider() {
    s_appNameProvider.reset();
}

LogEntry::LogEntry() : level(LogLevel::UNKNOWN) {}

// LogContext::Scope implementations
namespace LogContext {
    Scope::Scope(std::map<std::string, LogValue> attributes,
                 std::unordered_set<std::string> tags) {
        current_context_stack.push_back({std::move(attributes), std::move(tags)});
    }

    Scope::~Scope() {
        if (!current_context_stack.empty()) {
            current_context_stack.pop_back();
        }
    }

    void apply(LogEntry& entry) {
        if (current_context_stack.empty()) {
            return;
        }

        // Apply attributes: inner scopes override outer ones
        for (const auto& frame : current_context_stack) {
            for (const auto& [key, value] : frame.attributes) {
                entry.withAttribute(key, value); // withAttribute will overwrite existing
            }
        }

        // Apply tags: cumulative
        for (const auto& frame : current_context_stack) {
            for (const auto& tag : frame.tags) {
                entry.withTag(tag); // withTag will add if not exists
            }
        }
    }
} // namespace LogContext

// LogValue helpers
LogValue::LogValue(LogList list) : LogValueBase(std::make_shared<LogList>(std::move(list))) {}
LogValue::LogValue(LogObject obj) : LogValueBase(std::make_shared<LogObject>(std::move(obj))) {}

ValueType LogValue::type() const noexcept {
    return std::visit([](auto&& arg) -> ValueType {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return ValueType::Monostate;
        } else if constexpr (std::is_same_v<T, bool>) {
            return ValueType::Bool;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return ValueType::Int64;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return ValueType::UInt64;
        } else if constexpr (std::is_same_v<T, double>) {
            return ValueType::Double;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return ValueType::String;
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return ValueType::Binary;
        } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
            return ValueType::Nanoseconds;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<LogList>>) {
            return ValueType::List;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<LogObject>>) {
            return ValueType::Object;
        } else {
            return ValueType::Monostate; // Fallback
        }
    }, static_cast<const LogValueBase&>(*this));
}

bool LogValue::is(ValueType t) const noexcept {
    return type() == t;
}

bool LogValue::isNull() const noexcept {
    return is(ValueType::Monostate);
}

// Convenience constructors for various types
LogValue::LogValue(std::string_view s) : LogValueBase(std::string(s)) {}
LogValue::LogValue(const char* s) : LogValueBase(std::string(s)) {}
LogValue::LogValue(std::chrono::system_clock::time_point tp) : LogValueBase(std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch())) {}

// Convenience constructors for LogList and LogObject from initializer lists
LogValue::LogValue(std::initializer_list<LogValue> init_list)
    : LogValueBase(std::make_shared<LogList>(init_list)) {}

LogValue::LogValue(std::initializer_list<std::pair<const char*, LogValue>> init_list) {
    auto obj_ptr = std::make_shared<LogObject>();
    for (const auto& pair : init_list) {
        (*obj_ptr)[pair.first] = pair.second;
    }
    *static_cast<LogValueBase*>(this) = obj_ptr;
}

// Explicit conversion helpers
std::optional<const LogList*> LogValue::asList() const {
    if (auto p = std::get_if<std::shared_ptr<LogList>>(this)) {
        return p->get(); // Return raw pointer from shared_ptr
    }
    return std::nullopt;
}

std::optional<const LogObject*> LogValue::asObject() const {
    if (auto p = std::get_if<std::shared_ptr<LogObject>>(this)) {
        return p->get(); // Return raw pointer from shared_ptr
    }
    return std::nullopt;
}

// Accessors for specific types
std::optional<bool> LogValue::asBool() const {
    if (auto p = std::get_if<bool>(this)) return *p;
    return std::nullopt;
}

std::optional<int64_t> LogValue::asInt64() const {
    if (auto p = std::get_if<int64_t>(this)) return *p;
    return std::nullopt;
}

std::optional<uint64_t> LogValue::asUint64() const {
    if (auto p = std::get_if<uint64_t>(this)) return *p;
    return std::nullopt;
}

std::optional<double> LogValue::asDouble() const {
    return std::visit([](auto&& arg) -> std::optional<double> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, double>) {
            return arg;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<double>(arg);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return static_cast<double>(arg);
        }
        return std::nullopt;
    }, static_cast<const LogValueBase&>(*this));
}

std::optional<const std::string*> LogValue::asString() const {
    if (auto p = std::get_if<std::string>(this)) return p;
    return std::nullopt;
}

std::optional<const std::vector<uint8_t>*> LogValue::asBinary() const {
    if (auto p = std::get_if<std::vector<uint8_t>>(this)) return p;
    return std::nullopt;
}

std::optional<std::chrono::nanoseconds> LogValue::asDuration() const {
    if (auto p = std::get_if<std::chrono::nanoseconds>(this)) return *p;
    return std::nullopt;
}

// Implement LogValue::operator[] for LogList (by index)
LogValue& LogValue::operator[](size_t index) {
    auto list_ptr = get_if<std::shared_ptr<LogList>>();
    if (!list_ptr || !*list_ptr) {
        throw std::bad_variant_access();
    }
    if (index >= (*list_ptr)->size()) {
        throw std::out_of_range("LogList index out of bounds.");
    }
    return (**list_ptr)[index];
}

const LogValue& LogValue::operator[](size_t index) const {
    auto list_ptr = get_if<std::shared_ptr<LogList>>();
    if (!list_ptr || !*list_ptr) {
        throw std::bad_variant_access();
    }
    if (index >= (*list_ptr)->size()) {
        throw std::out_of_range("LogList index out of bounds.");
    }
    return (**list_ptr)[index];
}

// Implement LogValue::operator[] for LogObject (by key)
LogValue& LogValue::operator[](std::string_view key) {
    auto obj_ptr = get_if<std::shared_ptr<LogObject>>();
    if (!obj_ptr || !*obj_ptr) {
        // If it's not an object, make it one.
        *this = LogValue(LogObject{});
        obj_ptr = get_if<std::shared_ptr<LogObject>>(); // Re-get the pointer after modification
    }
    return (**obj_ptr)[std::string(key)]; // std::map::operator[] inserts if key not found
}

const LogValue& LogValue::operator[](std::string_view key) const {
    auto obj_ptr = get_if<std::shared_ptr<LogObject>>();
    if (!obj_ptr || !*obj_ptr) {
        throw std::bad_variant_access();
    }
    // std::map::at() throws std::out_of_range if key not found
    return (**obj_ptr).at(std::string(key));
}

// Base64 helper
static std::string base64Encode(const std::vector<uint8_t>& data) {
    static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string ret;
    int i = 0;
    int j = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];

    for (auto b : data) {
        char_array_3[i++] = b;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++) ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++) char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
        while ((i++ < 3)) ret += '=';
    }

    return ret;
}

// Internal helper for LogValue::toString to recursively print lists/objects compactly
struct LogValueToStringVisitor {
    std::ostream& oss;
    LogEntryJsonOptions::BinaryEncoding binaryEncoding;

    void operator()(std::monostate) const { oss << "null"; }
    void operator()(bool b) const { oss << (b ? "true" : "false"); }
    void operator()(int64_t i) const { oss << i; }
    void operator()(uint64_t u) const { oss << u; }
    void operator()(double d) const { oss << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << d; }
    void operator()(const std::string& s) const { oss << "\"" << s << "\""; } // Simple quote, not full JSON escape for compact form

    void operator()(const std::vector<uint8_t>& binary) const {
        if (binaryEncoding == LogEntryJsonOptions::BinaryEncoding::Hex) {
            oss << "\"0x";
            auto flags = oss.flags();
            oss << std::hex << std::setfill('0');
            for (uint8_t b : binary) {
                oss << std::setw(2) << static_cast<int>(b);
            }
            oss.flags(flags);
            oss << "\"";
        } else { // Base64
            oss << "\"" << base64Encode(binary) << "\"";
        }
    }
    void operator()(std::chrono::nanoseconds duration) const {
        oss << duration.count() << "ns"; // Human-readable duration
    }

    void operator()(const std::shared_ptr<LogList>& list) const {
        if (!list) {
            oss << "null";
            return;
        }
        oss << "[";
        bool first = true;
        for (const auto& item : *list) {
            if (!first) oss << ", ";
            oss << item; // Use global operator<< for recursive formatting
            first = false;
        }
        oss << "]";
    }

    void operator()(const std::shared_ptr<LogObject>& obj) const {
        if (!obj) {
            oss << "null";
            return;
        }
        oss << "{";
        bool first = true;
        for (const auto& pair : *obj) {
            if (!first) oss << ", ";
            oss << "\"" << pair.first << "\": " << pair.second;
            first = false;
        }
        oss << "}";
    }
};

std::string LogValue::toString(LogEntryJsonOptions::BinaryEncoding binary_encoding) const {
    std::ostringstream oss_stream;
    std::visit(LogValueToStringVisitor{oss_stream, binary_encoding}, static_cast<const LogValueBase&>(*this));
    return oss_stream.str();
}

// Global function or friend method for LogValue stream insertion
std::ostream& operator<<(std::ostream& os, const LogValue& value) {
    std::visit(LogValueToStringVisitor{os, LogEntryJsonOptions::BinaryEncoding::Hex}, static_cast<const LogValueBase&>(value));
    return os;
}

// Explicit implementation for LogValue comparison operators
std::partial_ordering LogValue::operator<=>(const LogValue& other) const {
    if (this->index() != other.index()) {
        return this->index() <=> other.index();
    }

    return std::visit([&](auto&& arg) -> std::partial_ordering {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return std::partial_ordering::equivalent;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<LogList>>) {
            if (!arg && !std::get<std::shared_ptr<LogList>>(other)) return std::partial_ordering::equivalent;
            if (!arg) return std::strong_ordering::less;
            if (!std::get<std::shared_ptr<LogList>>(other)) return std::strong_ordering::greater;
            return *arg <=> *std::get<std::shared_ptr<LogList>>(other);
        } else if constexpr (std::is_same_v<T, std::shared_ptr<LogObject>>) {
            if (!arg && !std::get<std::shared_ptr<LogObject>>(other)) return std::partial_ordering::equivalent;
            if (!arg) return std::strong_ordering::less;
            if (!std::get<std::shared_ptr<LogObject>>(other)) return std::strong_ordering::greater;
            return *arg <=> *std::get<std::shared_ptr<LogObject>>(other);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return arg <=> std::get<std::vector<uint8_t>>(other);
        } else {
            return arg <=> std::get<T>(other);
        }
    }, static_cast<const LogValueBase&>(*this));
}

bool LogValue::operator==(const LogValue& other) const {
    return (*this <=> other) == 0;
}

// Helper function to combine hashes
template <class T>
inline void hash_combine(size_t& seed, const T& v) {
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

// Custom hash for shared_ptr to LogList/LogObject by hashing their contents
struct LogValueSharedPtrHasher {
    size_t operator()(const std::shared_ptr<LogList>& list_ptr) const {
        size_t seed = 0;
        if (list_ptr) {
            for (const auto& item : *list_ptr) {
                hash_combine(seed, std::hash<LogValue>{}(item));
            }
        }
        return seed;
    }

    size_t operator()(const std::shared_ptr<LogObject>& object_ptr) const {
        size_t seed = 0;
        if (object_ptr) {
            for (const auto& pair : *object_ptr) {
                hash_combine(seed, std::hash<std::string>{}(pair.first));
                hash_combine(seed, std::hash<LogValue>{}(pair.second));
            }
        }
        return seed;
    }
};

// std::hash specialization for LogValue
namespace std {
    size_t hash<LogValue>::operator()(const LogValue& lv) const noexcept {
        return std::visit([&](auto&& arg) -> size_t {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return std::hash<int>{}(0); // Hash of null
            } else if constexpr (std::is_same_v<T, bool>) {
                return std::hash<bool>{}(arg);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::hash<int64_t>{}(arg);
            } else if constexpr (std::is_same_v<T, uint64_t>) {
                return std::hash<uint64_t>{}(arg);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::hash<double>{}(arg);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return std::hash<std::string>{}(arg);
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                size_t seed = 0;
                for (uint8_t byte : arg) {
                    hash_combine(seed, byte);
                }
                return seed;
            } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
                return std::hash<long long>{}(arg.count());
            } else if constexpr (std::is_same_v<T, std::shared_ptr<LogList>>) {
                return LogValueSharedPtrHasher{}(arg);
            } else if constexpr (std::is_same_v<T, std::shared_ptr<LogObject>>) {
                return LogValueSharedPtrHasher{}(arg);
            }
            else {
                // Fallback for unexpected types, though all should be covered.
                // This might indicate an issue with the variant or the hash specialization.
                // For safety, hash the type index.
                return std::hash<size_t>{}(static_cast<const LogValueBase&>(lv).index());
            }
        }, static_cast<const LogValueBase&>(lv));
    }
} // namespace std

bool LogEntry::isAtLeast(LogLevel entryLevel, LogLevel minLevel) {
    if (entryLevel == LogLevel::UNKNOWN || minLevel == LogLevel::UNKNOWN) return false;
    return static_cast<int>(entryLevel) >= static_cast<int>(minLevel);
}

bool LogEntry::isError(LogLevel level) {
    return level == LogLevel::ERROR || level == LogLevel::CRITICAL;
}

LogLevel LogEntry::parseLevel(std::string_view level_str)
{
    std::string upper_level(level_str);
    std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(),
                   [](unsigned char c)
                   { return std::toupper(c); });

    if (upper_level == "DEBUG" || upper_level == "DBG")
    {
        return LogLevel::DEBUG;
    }
    else if (upper_level == "INFO" || upper_level == "INF")
    {
        return LogLevel::INFO;
    }
    else if (upper_level == "WARNING" || upper_level == "WARN")
    {
        return LogLevel::WARNING;
    }
    else if (upper_level == "ERROR" || upper_level == "ERR")
    {
        return LogLevel::ERROR;
    }
    else if (upper_level == "CRITICAL" || upper_level == "CRIT" || upper_level == "FATAL")
    {
        return LogLevel::CRITICAL;
    }

    return LogLevel::UNKNOWN;
}

std::string_view LogEntry::levelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::CRITICAL:
        return "CRITICAL";
    default:
        return "UNKNOWN";
    }
}

uint64_t LogEntry::currentProcessId()
{
#if defined(_WIN32) || defined(_WIN64)
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

std::string LogEntry::currentHostName()
{
    char hostname[256];
#if defined(_WIN32) || defined(_WIN64)
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size))
    {
        return hostname;
    }
#else
    if (gethostname(hostname, sizeof(hostname)) == 0)
    {
        return hostname;
    }
#endif
    return "unknown";
}

LogEntry LogEntry::create(LogLevel level, std::string_view message, std::source_location loc)
{
    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.withSource(loc);
    entry.withMetadata();
    LogContext::apply(entry); // Apply thread-local context
    return entry;
}

LogEntry &LogEntry::withLevel(LogLevel l)
{
    level = l;
    return *this;
}

LogEntry &LogEntry::withMessage(std::string_view msg)
{
    message = msg;
    return *this;
}

LogEntry &LogEntry::withMetadata()
{
    if (time_point.time_since_epoch().count() == 0)
    {
        time_point = std::chrono::system_clock::now();
        timestamp = generatedTimestampString(getDefaultJsonOptions()); // Use default options for timestamp string
    }

    if (process_id == 0)
    {
        process_id = currentProcessId();
    }

    if (host_name.empty())
    {
        if (s_hostNameProvider) {
            host_name = (*s_hostNameProvider)();
        } else {
            host_name = currentHostName();
        }
    }

    if (app_name.empty())
    {
        if (s_appNameProvider) {
            app_name = (*s_appNameProvider)();
        } else {
            app_name = ""; // Default empty if no provider
        }
    }

    if (thread_id.empty())
    {
        std::stringstream ss;
        ss << std::this_thread::get_id();
        thread_id = ss.str();
    }

    return *this;
}

LogEntry &LogEntry::withAttribute(std::string_view key, LogValue value)
{
    attributes[std::string(key)] = std::move(value);
    return *this;
}

LogEntry &LogEntry::withAttributes(std::initializer_list<std::pair<const std::string, LogValue>> attrs)
{
    for (const auto &[key, value] : attrs)
    {
        attributes[key] = value;
    }
    return *this;
}

LogEntry &LogEntry::withAttributes(const std::map<std::string, LogValue> &attrs)
{
    for (const auto &[key, value] : attrs)
    {
        attributes[key] = value;
    }
    return *this;
}

// New: withAttribute overloads for common types
LogEntry& LogEntry::withAttribute(std::string_view key, bool value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, int64_t value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, uint64_t value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, double value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, std::string_view value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, const char* value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, std::chrono::system_clock::time_point value) {
    attributes[std::string(key)] = LogValue(value);
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, LogList value) {
    attributes[std::string(key)] = LogValue(std::move(value));
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string_view key, LogObject value) {
    attributes[std::string(key)] = LogValue(std::move(value));
    return *this;
}

LogEntry& LogEntry::withThreadName(std::string_view name) {
    thread_name = name;
    return *this;
}

LogEntry& LogEntry::withEventId(std::string_view eventId) {
    event_id = eventId;
    return *this;
}

LogEntry& LogEntry::merge(const LogEntry& other, bool overwrite_attributes, bool merge_tags) {
    if (overwrite_attributes) {
        for (const auto& [key, val] : other.attributes) {
            attributes[key] = val;
        }
    } else {
        for (const auto& [key, val] : other.attributes) {
            if (attributes.find(key) == attributes.end()) {
                attributes[key] = val;
            }
        }
    }

    if (merge_tags) {
        tags.insert(other.tags.begin(), other.tags.end());
    }

    // Merge other primitive fields if they are empty in current entry and not empty in other
    if (level == LogLevel::UNKNOWN) level = other.level;
    if (message.empty()) message = other.message;
    if (timestamp.empty()) {
        timestamp = other.timestamp;
        time_point = other.time_point;
    }
    if (process_id == 0) process_id = other.process_id;
    if (host_name.empty()) host_name = other.host_name;
    if (app_name.empty()) app_name = other.app_name;
    if (source_file.empty()) source_file = other.source_file;
    if (source_function.empty()) source_function = other.source_function;
    if (source_line == 0) source_line = other.source_line;
    if (thread_id.empty()) thread_id = other.thread_id;
    if (thread_name.empty()) thread_name = other.thread_name;
    if (trace_id.empty()) trace_id = other.trace_id;
    if (span_id.empty()) span_id = other.span_id;

    return *this;
}

bool LogEntry::isValid() const noexcept {
    return !message.empty() && level != LogLevel::UNKNOWN && !timestamp.empty();
}

int LogEntry::getSeverityValue() const {
    return static_cast<int>(level);
}

// Callstack Capture (Placeholder, needs actual implementation)
LogEntry& LogEntry::captureCallStack(std::string_view attribute_key, [[maybe_unused]] size_t skip_frames, [[maybe_unused]] size_t max_frames) {
    // This is a placeholder. Actual implementation would involve platform-specific APIs
    // like DbgHelp on Windows or backtrace/execinfo on Linux.
    // For now, we just add a dummy attribute.
    LogList stack_frames;
    // Updated placeholder message as per design
    stack_frames.push_back("Stack capture not fully implemented yet (platform specific).");
    stack_frames.push_back(std::format("Requested skip_frames: {}, max_frames: {}", skip_frames, max_frames));
    withAttribute(attribute_key, std::move(stack_frames));
    return *this;
}

LogEntry &LogEntry::withProcessId(uint64_t pid)
{
    process_id = pid;
    return *this;
}

LogEntry &LogEntry::withHost(std::string_view host)
{
    host_name = host;
    return *this;
}

LogEntry &LogEntry::withApp(std::string_view app)
{
    app_name = app;
    return *this;
}

LogEntry &LogEntry::withThreadId(std::string_view tid)
{
    thread_id = tid;
    return *this;
}

LogEntry &LogEntry::withThreadId(std::thread::id tid)
{
    std::stringstream ss;
    ss << tid;
    thread_id = ss.str();
    return *this;
}

LogEntry &LogEntry::withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional)
{
    time_point = tp;
    JsonOptions opts = getDefaultJsonOptions(); // Start with default options
    if (!include_fractional) {
        opts.precision = JsonOptions::Precision::Seconds; // Override precision if fractional not desired
    }
    timestamp = generatedTimestampString(opts);
    return *this;
}

LogEntry &LogEntry::withSource(std::source_location loc)
{
    source_file = loc.file_name();
    source_function = loc.function_name();
    source_line = loc.line();
    return *this;
}

LogEntry &LogEntry::withTag(std::string_view tag)
{
    tags.emplace(tag);
    return *this;
}

LogEntry &LogEntry::withTags(std::initializer_list<std::string_view> tag_list)
{
    for (auto tag : tag_list)
    {
        tags.emplace(tag);
    }
    return *this;
}

// Tag manipulation
LogEntry& LogEntry::removeTag(std::string_view tag_name) {
    tags.erase(std::string(tag_name));
    return *this;
}

LogEntry& LogEntry::clearTags() {
    tags.clear();
    return *this;
}

bool LogEntry::hasAllTags(const std::initializer_list<std::string_view>& tag_names) const {
    for (const auto& tag_name : tag_names) {
        if (tags.find(std::string(tag_name)) == tags.end()) {
            return false;
        }
    }
    return true;
}

bool LogEntry::hasAnyTag(const std::initializer_list<std::string_view>& tag_names) const {
    for (const auto& tag_name : tag_names) {
        if (tags.count(std::string(tag_name)) > 0) {
            return true;
        }
    }
    return false;
}

const std::set<std::string, std::less<>>& LogEntry::getTags() const {
    return tags;
}

// Cloning with tag manipulation
LogEntry LogEntry::clonedWithoutTag(std::string_view tag_name) const {
    LogEntry clone = *this;
    clone.removeTag(tag_name);
    return clone;
}

LogEntry LogEntry::clonedWithoutTags() const {
    LogEntry clone = *this;
    clone.clearTags();
    return clone;
}

// Helper function to extract nested exception details
static void extractNestedExceptionDetails(const std::exception& ex, LogList& messages, LogList& types) {
    messages.push_back(LogValue(ex.what()));
    types.push_back(LogValue(typeid(ex).name()));

    const std::nested_exception* nested = dynamic_cast<const std::nested_exception*>(&ex);
    if (nested && nested->nested_ptr()) {
        try {
            std::rethrow_exception(nested->nested_ptr());
        } catch (const std::exception& inner_ex) {
            // Recursively call for the inner exception using the base overload
            extractNestedExceptionDetails(inner_ex, messages, types);
        } catch (...) {
            // Handle non-std::exception types or other unknown exceptions in the nested chain
            messages.push_back(LogValue("Unknown non-std::exception type in nested chain"));
            types.push_back(LogValue("unknown_nested"));
        }
    }
}

// Overload for std::nested_exception to directly handle the initial nested exception
static void extractNestedExceptionDetails(const std::nested_exception& ex, LogList& messages, LogList& types) {
    if (ex.nested_ptr()) {
        try {
            std::rethrow_exception(ex.nested_ptr());
        } catch (const std::exception& inner_ex) {
            extractNestedExceptionDetails(inner_ex, messages, types);
        } catch (...) {
            messages.push_back(LogValue("Unknown non-std::exception type in nested chain"));
            types.push_back(LogValue("unknown_nested"));
        }
    } else {
        messages.push_back(LogValue("Empty nested_exception"));
        types.push_back(LogValue("empty_nested_exception"));
    }
}

LogEntry &LogEntry::withException(const std::exception &e, bool include_stack_trace)
{
    withAttribute("exception.message", LogValue(std::string(e.what())));
    withAttribute("exception.type", LogValue(std::string(typeid(e).name())));

    if (include_stack_trace) {
        captureCallStack("exception.call_stack", 1); // Skip this frame
    }
    return *this;
}

// New overload for nested exceptions
LogEntry &LogEntry::withException(const std::nested_exception &e, bool include_stack_trace)
{
    LogList exception_messages;
    LogList exception_types;

    extractNestedExceptionDetails(e, exception_messages, exception_types);

    withAttribute("exception.messages", LogValue(std::move(exception_messages)));
    withAttribute("exception.types", LogValue(std::move(exception_types)));

    if (include_stack_trace) {
        captureCallStack("exception.call_stack", 1); // Skip this frame
    }
    return *this;
}

LogEntry &LogEntry::withTraceContext(std::string_view tid, std::string_view sid)
{
    trace_id = tid;
    span_id = sid;
    return *this;
}

LogEntry &LogEntry::withSystemLoad()
{
#if defined(__linux__)
    double load_avg[3];
    if (getloadavg(load_avg, 3) != -1)
    {
        withAttribute("system_load_1m", load_avg[0]);
        withAttribute("system_load_5m", load_avg[1]);
        withAttribute("system_load_15m", load_avg[2]);
    }
#elif !defined(_WIN32) && !defined(_WIN64)
    withAttribute("system_load", "unsupported");
#else
    withAttribute("system_load", "unsupported");
#endif
    return *this;
}

LogEntry &LogEntry::withMemoryUsage()
{
#if defined(_WIN32) || defined(_WIN64)
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&pmc, sizeof(pmc)))
    {
        withAttribute("memory_rss_bytes", static_cast<uint64_t>(pmc.WorkingSetSize));
        withAttribute("memory_peak_rss_bytes", static_cast<uint64_t>(pmc.PeakWorkingSetSize));
    }
#else
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open())
    {
        long long size, resident, share, text, lib, data, dt;
        statm >> size >> resident >> share >> text >> lib >> data >> dt;
        long page_size = sysconf(_SC_PAGESIZE);
        withAttribute("memory_rss_bytes", static_cast<uint64_t>(resident * page_size));
    }
#endif
    return *this;
}

LogEntry &LogEntry::removeAttribute(const std::string &key)
{
    attributes.erase(key);
    return *this;
}

LogEntry &LogEntry::clearAttributes()
{
    attributes.clear();
    return *this;
}

LogEntry &LogEntry::mergeAttributes(const LogEntry &other)
{
    for (const auto &[key, value] : other.attributes)
    {
        attributes[key] = value;
    }
    return *this;
}

LogEntry LogEntry::clonedWithLevel(LogLevel new_level) const
{
    LogEntry clone = *this;
    clone.level = new_level;
    return clone;
}

LogEntry LogEntry::clonedWithMessage(std::string_view new_message) const
{
    LogEntry clone = *this;
    clone.message = std::string(new_message);
    return clone;
}

LogEntry LogEntry::clonedWithAttribute(std::string_view key, LogValue value) const
{
    LogEntry clone = *this;
    clone.withAttribute(std::string(key), std::move(value));
    return clone;
}

LogEntry LogEntry::clonedWithAttributes(const std::map<std::string, LogValue>& attrs) const
{
    LogEntry clone = *this;
    clone.withAttributes(attrs);
    return clone;
}

LogEntry LogEntry::clonedWithoutAttribute(std::string_view key) const
{
    LogEntry clone = *this;
    clone.removeAttribute(std::string(key));
    return clone;
}

LogEntry LogEntry::clonedWithTag(std::string_view tag) const
{
    LogEntry clone = *this;
    clone.withTag(tag);
    return clone;
}

bool LogEntry::parseTime()
{
    auto parsed_tp = LogEntry::parseTimestamp(timestamp);
    if (parsed_tp) {
        time_point = *parsed_tp;
        return true;
    }
    time_point = std::chrono::system_clock::time_point(); // Reset on failure
    return false;
}

// Helper to format a time_point into a string based on options
std::string LogEntry::formatTimestamp(
    std::chrono::system_clock::time_point tp,
    LogEntry::JsonOptions::TimestampFormat format_type,
    const TimestampFormatOptions& opts
) {
    if (tp.time_since_epoch().count() == 0)
        return "";

    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    
    if (opts.timezone == JsonOptions::Timezone::UTC) {
#if defined(_WIN32) || defined(_WIN64)
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
    } else {
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
    }

    std::ostringstream ss;
    if (opts.custom_format.has_value()) {
        char buffer[128]; // Sufficient buffer for common date/time formats
        std::strftime(buffer, sizeof(buffer), opts.custom_format->c_str(), &tm);
        ss << buffer;
    } else if (format_type == JsonOptions::TimestampFormat::ISO8601) {
        ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

        auto epoch = tp.time_since_epoch();
        if (opts.precision != JsonOptions::Precision::Seconds)
        {
            ss << ".";
            long long fractional_part;
            int width;
            if (opts.precision == JsonOptions::Precision::Millis) {
                fractional_part = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() % 1000;
                width = 3;
            } else if (opts.precision == JsonOptions::Precision::Micros) {
                fractional_part = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count() % 1000000;
                width = 6;
            } else { // Nanos
                fractional_part = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch).count() % 1000000000;
                width = 9;
            }
            ss << std::setfill('0') << std::setw(width) << fractional_part;
        }
        ss << (opts.timezone == JsonOptions::Timezone::UTC ? "Z" : "");
    } else { // Default format (YYYY-MM-DD HH:MM:SS.mmm)
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (opts.precision != JsonOptions::Precision::Seconds) {
             auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          tp.time_since_epoch()) %
                      1000;
            ss << "." << std::setfill('0') << std::setw(3) << ms.count();
        }
    }
    return ss.str();
}

std::string LogEntry::generatedTimestampString(const JsonOptions& options) const
{
    TimestampFormatOptions opts;
    opts.precision = options.precision;
    opts.timezone = options.timezone;
    opts.custom_format = options.custom_timestamp_format;
    return LogEntry::formatTimestamp(time_point, options.timestamp_format, opts);
}

// Helper to parse ISO8601 timestamps
static std::optional<std::chrono::system_clock::time_point> parseISO8601(std::string_view timestamp_str) {
    std::tm tm = {};
    std::istringstream tss{std::string(timestamp_str)};

    int y, m, d, H, M, S;
    char sep1, sep2, T_char, colon1, colon2;
    if (!(tss >> y >> sep1 >> m >> sep2 >> d >> T_char >> H >> colon1 >> M >> colon2 >> S) ||
        sep1 != '-' || sep2 != '-' || T_char != 'T' || colon1 != ':' || colon2 != ':') {
        return std::nullopt;
    }

    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = H;
    tm.tm_min = M;
    tm.tm_sec = S;
    tm.tm_isdst = -1;

    std::time_t tt;
    char tz_char = tss.peek();
    bool is_utc = (tz_char == 'Z');

    if (is_utc) {
        tss.ignore();
#if defined(_WIN32) || defined(_WIN64)
        tt = _mkgmtime(&tm);
#else
        tt = timegm(&tm);
#endif
    } else {
        tt = std::mktime(&tm);
    }
    
    if (tt == -1) {
        return std::nullopt;
    }

    auto tp = std::chrono::system_clock::from_time_t(tt);

    if (tss.peek() == '.') {
        tss.ignore();
        std::string frac_str;
        while (tss.good() && std::isdigit(tss.peek())) {
            frac_str += static_cast<char>(tss.get());
        }
        if (!frac_str.empty()) {
            try {
                frac_str.resize(9, '0');
                long long nanos_val = std::stoll(frac_str);
                tp += std::chrono::nanoseconds(nanos_val);
            } catch (const std::out_of_range&) {
                // Ignore
            }
        }
    }

    return tp;
}

// Helper to parse default format timestamps (YYYY-MM-DD HH:MM:SS.mmm)
static std::optional<std::chrono::system_clock::time_point> parseDefaultFormat(std::string_view timestamp_str) {
    std::istringstream ss{std::string(timestamp_str)};
    std::tm tm = {};
    char dash1, dash2, colon1, colon2;
    int year, month, day, hour, min, sec;

    if (!(ss >> year >> dash1 >> month >> dash2 >> day >> hour >> colon1 >> min >> colon2 >> sec)) {
        return std::nullopt;
    }

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;

    std::time_t tt = std::mktime(&tm);
    if (tt == -1)
        return std::nullopt;

    auto tp = std::chrono::system_clock::from_time_t(tt);

    if (ss.peek() == '.') {
        ss.ignore();
        std::string frac_str;
        while (ss.good() && std::isdigit(ss.peek())) {
            frac_str += static_cast<char>(ss.get());
        }
        if (!frac_str.empty()) {
            try {
                frac_str.resize(9, '0');
                long long nanos_val = std::stoll(frac_str);
                tp += std::chrono::nanoseconds(nanos_val);
            } catch (const std::out_of_range&) {
                // Ignore
            }
        }
    }
    return tp;
}


std::optional<std::chrono::system_clock::time_point> LogEntry::parseTimestamp(
    std::string_view timestamp_str
) {
    if (auto tp = parseISO8601(timestamp_str)) {
        return tp;
    }
    if (auto tp = parseDefaultFormat(timestamp_str)) {
        return tp;
    }

    try {
        size_t pos;
        std::string s_timestamp_str(timestamp_str);
        long long millis = std::stoll(s_timestamp_str, &pos);
        if (pos == timestamp_str.length()) {
            return std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
        }
    } catch (const std::exception&) {
        // Not a Unix Millis timestamp
    }

    return std::nullopt;
}

std::optional<std::chrono::system_clock::time_point> LogEntry::parseTimestamp(
    std::string_view timestamp_str,
    LogEntry::JsonOptions::TimestampFormat format_type
) {
    if (format_type == JsonOptions::TimestampFormat::ISO8601) {
        return parseISO8601(timestamp_str);
    } else if (format_type == JsonOptions::TimestampFormat::Default) {
        return parseDefaultFormat(timestamp_str);
    } else if (format_type == JsonOptions::TimestampFormat::UnixMillis) {
         try {
            size_t pos;
            std::string s_timestamp_str(timestamp_str);
            long long millis = std::stoll(s_timestamp_str, &pos);
            if (pos == timestamp_str.length()) {
                return std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
            }
        } catch (const std::exception&) {
            // Not a Unix Millis timestamp
        }
    }
    return std::nullopt;
}

void LogEntry::setAttribute(const std::string &key, const std::string &value)
{
    attributes[key] = value;
}

void LogEntry::setAttribute(const std::string &key, const char *value)
{
    attributes[key] = std::string(value);
}

void LogEntry::setAttribute(const std::string &key, LogValue value)
{
    attributes[key] = std::move(value);
}

std::string LogEntry::getAttributeAsString(const std::string &key) const
{
    auto it = attributes.find(key);
    if (it == attributes.end())
        return "";

    return std::visit([](auto &&arg) -> std::string
                      {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
            return std::to_string(arg.count()) + "ns";
        }
        return ""; }, it->second);
}

bool LogEntry::hasAttribute(const std::string &key) const
{
    return attributes.contains(key);
}

std::optional<LogValue> LogEntry::getAttribute(const std::string &key) const
{
    auto it = attributes.find(key);
    if (it != attributes.end())
    {
        return it->second;
    }
    return std::nullopt;
}

bool LogEntry::hasTag(std::string_view tag) const
{
    return tags.contains(tag);
}

std::optional<double> LogEntry::getAsDouble(const std::string& key) const {
    auto it = attributes.find(key);
    if (it == attributes.end()) return std::nullopt;

    return std::visit([](auto&& arg) -> std::optional<double> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return static_cast<double>(arg);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return static_cast<double>(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            return arg;
        } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
            return std::chrono::duration_cast<std::chrono::duration<double>>(arg).count();
        }
        return std::nullopt;
    }, it->second);
}

std::optional<int64_t> LogEntry::getAsInt(const std::string& key) const {
    auto it = attributes.find(key);
    if (it == attributes.end()) return std::nullopt;

    return std::visit([](auto&& arg) -> std::optional<int64_t> {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, int64_t>) {
            return arg;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            if (arg > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return std::nullopt;
            }
            return static_cast<int64_t>(arg);
        } else if constexpr (std::is_same_v<T, double>) {
            if (arg > static_cast<double>(std::numeric_limits<int64_t>::max()) ||
                arg < static_cast<double>(std::numeric_limits<int64_t>::min())) {
                return std::nullopt;
            }
            return static_cast<int64_t>(arg);
        } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
            return arg.count();
        }
        return std::nullopt;
    }, it->second);
}

// Helper to skip whitespace
static std::string_view::iterator skipWhitespace(std::string_view::iterator &it, std::string_view::iterator end)
{
    while (it != end && std::isspace(static_cast<unsigned char>(*it)))
    {
        ++it;
    }
    return it;
}

// Helper to parse a JSON string (e.g., "value")
static std::expected<std::string, std::string> parseJsonString(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '"')
    {
        return std::unexpected("Expected '\"' to start a string");
    }
    ++it; // Skip opening quote

    std::string value;
    while (it != end && *it != '"')
    {
        if (*it == '\\')
        { // Handle escape sequences
            ++it;
            if (it == end)
                return std::unexpected("Unexpected end of string after escape character");
            switch (*it)
            {
            case '"':
                value += '"';
                break;
            case '\\':
                value += '\\';
                break;
            case '/':
                value += '/';
                break;
            case 'b':
                value += '\b';
                break;
            case 'f':
                value += '\f';
                break;
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            case 'u': // Unicode escape (e.g., \u0000) - simplified
                if (std::distance(it, end) < 4)
                    return std::unexpected("Incomplete unicode escape sequence");
                value += '?'; // Placeholder for actual unicode processing
                it += 4;
                break;
            default:
                return std::unexpected(std::string("Unknown escape sequence: \\") + *it);
            }
        }
        else
        {
            value += *it;
        }
        ++it;
    }

    if (it == end)
    {
        return std::unexpected("Expected '\"' to end a string, but reached end of input");
    }
    ++it; // Skip closing quote
    return value;
}

// Helper to parse a JSON number (e.g., 123, 123.45)
static std::expected<LogValue, std::string> parseJsonNumber(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    auto start_num = it;
    if (it == end || (!std::isdigit(static_cast<unsigned char>(*it)) && *it != '-'))
    {
        return std::unexpected("Expected a number");
    }

    if (*it == '-')
        ++it;
    while (it != end && std::isdigit(static_cast<unsigned char>(*it)))
    {
        ++it;
    }

    bool is_double = false;
    if (it != end && *it == '.')
    {
        is_double = true;
        ++it;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it)))
        {
            ++it;
        }
    }

    if (it != end && (*it == 'e' || *it == 'E'))
    {
        is_double = true;
        ++it;
        if (it != end && (*it == '+' || *it == '-'))
            ++it;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it)))
        {
            ++it;
        }
    }

    std::string num_str(start_num, it);
    try
    {
        if (is_double)
        {
            return static_cast<LogValue>(std::stod(num_str));
        }
        else
        {
            try
            {
                return static_cast<LogValue>(std::stoll(num_str));
            }
            catch (const std::out_of_range &)
            {
                return static_cast<LogValue>(std::stoull(num_str));
            }
        }
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string("Failed to parse number: ") + e.what());
    }
}

// Helper to parse a JSON boolean (true, false)
static std::expected<bool, std::string> parseJsonBoolean(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (std::distance(it, end) >= 4 && std::string_view(&*it, 4) == "true")
    {
        it += 4;
        return true;
    }
    if (std::distance(it, end) >= 5 && std::string_view(&*it, 5) == "false")
    {
        it += 5;
        return false;
    }
    return std::unexpected("Expected 'true' or 'false'");
}

// Forward declarations for recursive parsing
static std::expected<LogValue, std::string> parseLogValue(std::string_view::iterator &it, std::string_view::iterator end);
static std::expected<LogObject, std::string> parseJsonObject(std::string_view::iterator &it, std::string_view::iterator end);
static std::expected<LogList, std::string> parseJsonArray(std::string_view::iterator &it, std::string_view::iterator end);

// Helper to parse a LogValue (string, number, boolean, null, object, array)
static std::expected<LogValue, std::string> parseLogValue(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end)
    {
        return std::unexpected("Expected a value, but reached end of input");
    }

    switch (*it)
    {
    case '"':
        return parseJsonString(it, end);
    case '{':
    {
        auto obj_res = parseJsonObject(it, end);
        if (!obj_res)
            return std::unexpected(obj_res.error());
        return LogValue(std::move(*obj_res));
    }
    case '[':
    {
        auto list_res = parseJsonArray(it, end);
        if (!list_res)
            return std::unexpected(list_res.error());
        return LogValue(std::move(*list_res));
    }
    case 't': // true
    case 'f':
    { // false
        auto bool_res = parseJsonBoolean(it, end);
        if (bool_res)
            return static_cast<LogValue>(*bool_res);
        return std::unexpected(bool_res.error());
    }
    case 'n':
    { // null
        if (std::distance(it, end) >= 4 && std::string_view(&*it, 4) == "null")
        {
            it += 4;
            return std::monostate{};
        }
        return std::unexpected("Expected 'null'");
    }
    case '-': // negative number
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return parseJsonNumber(it, end);
    default:
        return std::unexpected(std::string("Unexpected character for value: ") + *it);
    }
}

// Helper to parse a JSON array (LogList)
static std::expected<LogList, std::string> parseJsonArray(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '[')
    {
        return std::unexpected("Expected '[' to start array");
    }
    ++it; // Skip '['

    LogList list;
    bool first_item = true;

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == ']')
        {
            ++it; // Skip ']'
            return list;
        }

        if (!first_item)
        {
            if (*it != ',')
            {
                return std::unexpected("Expected ',' or ']' in array");
            }
            ++it; // Skip ','
        }

        auto val_res = parseLogValue(it, end);
        if (!val_res)
            return std::unexpected(val_res.error());
        list.push_back(std::move(*val_res));
        first_item = false;
    }
    return std::unexpected("Expected ']' to end array, but reached end of input");
}

// Helper to parse a JSON object (LogObject)
static std::expected<LogObject, std::string> parseJsonObject(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '{')
    {
        return std::unexpected("Expected '{' to start object");
    }
    ++it; // Skip '{'

    LogObject obj;
    bool first_item = true;

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == '}')
        {
            ++it; // Skip '}'
            return obj;
        }

        if (!first_item)
        {
            if (*it != ',')
            {
                return std::unexpected("Expected ',' or '}' in object");
            }
            ++it; // Skip ','
        }

        auto key_res = parseJsonString(it, end);
        if (!key_res)
            return std::unexpected("Failed to parse key: " + key_res.error());

        it = skipWhitespace(it, end);
        if (it == end || *it != ':')
        {
            return std::unexpected("Expected ':' after key");
        }
        ++it; // Skip ':'

        auto value_res = parseLogValue(it, end);
        if (!value_res)
            return std::unexpected("Failed to parse value for key '" + *key_res + "': " + value_res.error());

        obj[*key_res] = std::move(*value_res);
        first_item = false;
    }
    return std::unexpected("Expected '}' to end object, but reached end of input");
}

// Helper to parse a JSON array of strings (e.g., ["tag1", "tag2"])
static std::expected<std::set<std::string, std::less<>>, std::string> parseJsonStringArray(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '[')
    {
        return std::unexpected("Expected '[' to start array");
    }
    ++it; // Skip '['

    std::set<std::string, std::less<>> array_values;
    bool first_item = true;

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == ']')
        {
            ++it; // Skip ']'
            return array_values;
        }

        if (!first_item)
        {
            if (*it != ',')
            {
                return std::unexpected("Expected ',' or ']' in array");
            }
            ++it; // Skip ','
        }

        auto str_res = parseJsonString(it, end);
        if (!str_res)
            return std::unexpected(str_res.error());
        array_values.insert(*str_res);
        first_item = false;
    }
    return std::unexpected("Expected ']' to end array, but reached end of input");
}

LogEntry LogEntry::fromMap(const std::map<std::string, LogValue> &data)
{
    LogEntry entry;

    for (const auto &[key, value] : data)
    {
        if (key == "level")
        {
            if (auto s_ptr = value.asString()) {
                entry.level = parseLevel(**s_ptr);
            }
        }
        else if (key == "message")
        {
            if (auto s_ptr = value.asString()) {
                entry.message = **s_ptr;
            }
        }
        else if (key == "timestamp")
        {
            if (auto s_ptr = value.asString()) {
                entry.timestamp = **s_ptr;
                entry.parseTime();
            }
            else if (auto i = value.asInt64()) {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*i));
                entry.timestamp = entry.generatedTimestampString(getDefaultJsonOptions());
            } else if (auto u = value.asUint64()) {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*u));
                entry.timestamp = entry.generatedTimestampString(getDefaultJsonOptions());
            }
        }
        else if (key == "process_id")
        {
            if (auto u = value.asUint64())
                entry.process_id = *u;
            else if (auto i = value.asInt64())
                entry.process_id = static_cast<uint64_t>(*i);
        }
        else if (key == "host_name")
        {
            if (auto s_ptr = value.asString())
                entry.host_name = **s_ptr;
        }
        else if (key == "app_name")
        {
            if (auto s_ptr = value.asString())
                entry.app_name = **s_ptr;
        }
        else if (key == "event_id")
        {
            if (auto s_ptr = value.asString())
                entry.event_id = **s_ptr;
        }
        else if (key == "thread_id")
        {
            if (auto s_ptr = value.asString())
                entry.thread_id = **s_ptr;
        }
        else if (key == "thread_name")
        {
            if (auto s_ptr = value.asString())
                entry.thread_name = **s_ptr;
        }
        else if (key == "trace_id")
        {
            if (auto s_ptr = value.asString())
                entry.trace_id = **s_ptr;
        }
        else if (key == "span_id")
        {
            if (auto s_ptr = value.asString())
                entry.span_id = **s_ptr;
        }
        else if (key == "source")
        {
            if (auto obj_ptr = value.asObject()) {
                const auto& sourceObj = **obj_ptr;
                if (auto file_val = sourceObj.find("file"); file_val != sourceObj.end()) {
                    if (auto s_ptr = file_val->second.asString())
                        entry.source_file = **s_ptr;
                }
                if (auto func_val = sourceObj.find("function"); func_val != sourceObj.end()) {
                    if (auto s_ptr = func_val->second.asString())
                        entry.source_function = **s_ptr;
                }
                if (auto line_val = sourceObj.find("line"); line_val != sourceObj.end()) {
                    if (auto i = line_val->second.asInt64())
                        entry.source_line = static_cast<int>(*i);
                }
            }
        }
        else if (key == "tags")
        {
            if (auto list_ptr = value.asList()) {
                for (const auto &v : **list_ptr)
                {
                    if (auto s_ptr = v.asString())
                    {
                        entry.tags.insert(**s_ptr);
                    }
                }
            }
        }
        else if (key == "attributes")
        {
            if (auto obj_ptr = value.asObject()) {
                entry.attributes.clear();
                for (const auto& [attr_key, attr_val] : **obj_ptr) {
                    entry.attributes[attr_key] = attr_val;
                }
            }
        }
        else
        {
            entry.attributes[key] = value;
        }
    }
    return entry;
}

std::map<std::string, LogValue> LogEntry::toMap() const
{
    std::map<std::string, LogValue> m;

    m["timestamp"] = timestamp.empty() ? generatedTimestampString(JsonOptions{.precision = JsonOptions::Precision::Millis}) : LogValue(timestamp);
    m["level"] = std::string(levelToString(level));
    m["message"] = message;

    if (process_id != 0)
        m["process_id"] = process_id;
    if (!host_name.empty())
        m["host_name"] = host_name;
    if (!app_name.empty())
        m["app_name"] = app_name;
    if (!thread_id.empty())
        m["thread_id"] = thread_id;
    if (!thread_name.empty())
        m["thread_name"] = thread_name;
    if (!trace_id.empty())
        m["trace_id"] = trace_id;
    if (!span_id.empty())
        m["span_id"] = span_id;
    if (!event_id.empty())
        m["event_id"] = event_id;


    if (!source_file.empty() || !source_function.empty() || source_line != 0) {
        LogObject sourceObject;
        if (!source_file.empty())
            sourceObject["file"] = source_file;
        if (!source_function.empty())
            sourceObject["function"] = source_function;
        if (source_line != 0)
            sourceObject["line"] = static_cast<int64_t>(source_line);
        m["source"] = LogValue(std::move(sourceObject));
    }

    if (!tags.empty())
    {
        LogList tagList;
        for (const auto &tag : tags)
            tagList.push_back(tag);
        m["tags"] = LogValue(std::move(tagList));
    }

    if (!attributes.empty())
    {
        LogObject attrs_map_copy;
        for (const auto& [key, val] : attributes) {
            attrs_map_copy[key] = val;
        }
        m["attributes"] = LogValue(std::move(attrs_map_copy));
    }
    return m;
}

std::expected<LogEntry, std::string> LogEntry::fromJson(std::string_view json_str)
{
    LogEntry entry;
    auto it = json_str.begin();
    auto end = json_str.end();

    it = skipWhitespace(it, end);
    if (it == end || *it != '{')
    {
        return std::unexpected("JSON must start with '{'");
    }
    ++it; // Skip '{'

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == '}')
        {
            ++it; // Skip '}'
            break;
        }

        auto key_res = parseJsonString(it, end);
        if (!key_res)
            return std::unexpected("Failed to parse key: " + key_res.error());
        std::string key = *key_res;

        it = skipWhitespace(it, end);
        if (it == end || *it != ':')
        {
            return std::unexpected("Expected ':' after key");
        }
        ++it; // Skip ':'

        if (key == "timestamp")
        {
            auto val_res = parseLogValue(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse timestamp: " + val_res.error());

            if (auto s_ptr = val_res->asString())
            {
                entry.timestamp = *(*s_ptr);
                entry.parseTime();
            }
            else if (auto i = val_res->asInt64())
            {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*i));
                entry.timestamp = entry.generatedTimestampString(getDefaultJsonOptions());
            }
            else if (auto u = val_res->asUint64())
            {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*u));
                entry.timestamp = entry.generatedTimestampString(getDefaultJsonOptions());
            }
        }
        else if (key == "level")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse level: " + val_res.error());
            entry.level = LogEntry::parseLevel(*val_res);
        }
        else if (key == "message")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse message: " + val_res.error());
            entry.message = *val_res;
        }
        else if (key == "process_id")
        {
            auto val_res = parseJsonNumber(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse process_id: " + val_res.error());
            if (val_res->is<uint64_t>())
            {
                                 entry.process_id = val_res->to<uint64_t>();            }
            else if (val_res->is<int64_t>())
            {
                                 entry.process_id = static_cast<uint64_t>(val_res->to<int64_t>());            }
            else
            {
                return std::unexpected("process_id must be a number");
            }
        }
        else if (key == "host_name")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse host_name: " + val_res.error());
            entry.host_name = *val_res;
        }
        else if (key == "app_name")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse app_name: " + val_res.error());
            entry.app_name = *val_res;
        }
        else if (key == "event_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse event_id: " + val_res.error());
            entry.event_id = *val_res;
        }
        else if (key == "source")
        {
            auto obj_res = parseJsonObject(it, end);
            if (!obj_res)
                return std::unexpected("Failed to parse source object: " + obj_res.error());

            if (auto file_val = obj_res->find("file"); file_val != obj_res->end()) {
                if (auto s_ptr = file_val->second.asString())
                    entry.source_file = *(*s_ptr);
            }
            if (auto func_val = obj_res->find("function"); func_val != obj_res->end()) {
                if (auto s_ptr = func_val->second.asString())
                    entry.source_function = *(*s_ptr);
            }
            if (auto line_val = obj_res->find("line"); line_val != obj_res->end()) {
                if (auto i = line_val->second.asInt64())
                    entry.source_line = static_cast<int>(*i);
            }
        }
        else if (key == "thread_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse thread_id: " + val_res.error());
            entry.thread_id = *val_res;
        }
        else if (key == "thread_name")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse thread_name: " + val_res.error());
            entry.thread_name = *val_res;
        }
        else if (key == "trace_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse trace_id: " + val_res.error());
            entry.trace_id = *val_res;
        }
        else if (key == "span_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse span_id: " + val_res.error());
            entry.span_id = *val_res;
        }
        else if (key == "tags")
        {
            auto val_res = parseJsonStringArray(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse tags: " + val_res.error());
            entry.tags = *val_res;
        }
        else if (key == "attributes")
        {
            auto val_res = parseJsonObject(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse attributes: " + val_res.error());
            entry.attributes = std::move(*val_res);
        }
        else
        {
            auto dummy_val_res = parseLogValue(it, end);
            if (!dummy_val_res)
                return std::unexpected("Failed to skip unknown value for key '" + key + "': " + dummy_val_res.error());
        }

        it = skipWhitespace(it, end);
        if (it != end && *it == ',')
        {
            ++it;
        }
        else if (it != end && *it == '}')
        {
            // End of object, will break loop
        }
        else if (it == end)
        {
            return std::unexpected("Unexpected end of JSON string while parsing object");
        }
        else
        {
            return std::unexpected(std::string("Expected ',' or '}' but found: ") + *it);
        }
    }

    return entry;
}

// Helper to escape JSON strings
static std::string escapeJson(const std::string &s)
{
    std::ostringstream o;
    for (auto c : s)
    {
        switch (c)
        {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\b':
            o << "\\b";
            break;
        case '\f':
            o << "\\f";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if ((unsigned char)c <= '\x1f')
            {
                // Hexadecimal representation for control characters
                char buf[7]; // e.g., "\u0000"
                std::snprintf(buf, sizeof(buf), "\\u%04x", (int)(unsigned char)c);
                o << buf;
            }
            else
            {
                o << c;
            }
        }
    }
    return o.str();
}

struct JsonVisitor
{
    std::ostream &os;
    const LogEntry::JsonOptions &options;
    int indent_level;

    void indent() const
    {
        if (options.pretty)
        {
            for (int i = 0; i < indent_level; ++i)
                os << "  ";
        }
    }

    void nl() const
    {
        if (options.pretty)
            os << "\n";
    }

    void operator()(std::monostate) const { os << "null"; }
    void operator()(bool b) const { os << (b ? "true" : "false"); }
    void operator()(int64_t i) const { os << i; }
    void operator()(uint64_t u) const { os << u; }
    void operator()(double d) const { os << d; }
    void operator()(const std::string &s) const { os << "\"" << escapeJson(s) << "\""; }

    void operator()(const std::vector<uint8_t>& binary) const {
        if (options.binary_encoding == LogEntry::JsonOptions::BinaryEncoding::Hex) {
            os << "\"0x";
            auto flags = os.flags();
            os << std::hex << std::setfill('0');
            for (auto b : binary) {
                os << std::setw(2) << static_cast<int>(b);
            }
            os.flags(flags);
            os << "\"";
        } else {
            os << "\"" << base64Encode(binary) << "\"";
        }
    }

    void operator()(std::chrono::nanoseconds duration) const {
        os << duration.count();
    }

    void operator()(const std::shared_ptr<LogList> &list) const
    {
        if (!list || list->empty())
        {
            os << "[]";
            return;
        }
        os << "[";
        nl();
        for (size_t i = 0; i < list->size(); ++i)
        {
            if (options.pretty)
            {
                for (int j = 0; j <= indent_level; ++j)
                    os << "  ";
            }
            std::visit(JsonVisitor{os, options, indent_level + 1}, (*list)[i]);
            if (i < list->size() - 1)
                os << ",";
            nl();
        }
        indent();
        os << "]";
    }

    void operator()(const std::shared_ptr<LogObject> &obj) const
    {
        if (!obj || obj->empty())
        {
            os << "{}";
            return;
        }
        os << "{";
        nl();
        size_t count = 0;
        for (const auto &[key, value] : *obj)
        {
            if (options.pretty)
            {
                for (int j = 0; j <= indent_level; ++j)
                    os << "  ";
            }
            os << "\"" << escapeJson(key) << "\": ";
            std::visit(JsonVisitor{os, options, indent_level + 1}, value);
            if (++count < obj->size())
                os << ",";
            nl();
        }
        indent();
        os << "}";
    }
};

std::string LogEntry::toJson(const JsonOptions &options) const
{
    std::ostringstream oss;
    std::string nl_char = options.pretty ? "\n" : "";
    std::string indent_prefix = options.pretty ? "  " : "";
    int current_indent_level = 0;

    auto should_include_field = [&](std::string_view field_name) {
        if (!options.include_fields.empty()) {
            return options.include_fields.count(std::string(field_name)) > 0;
        }
        if (!options.exclude_fields.empty()) {
            return options.exclude_fields.count(std::string(field_name)) == 0;
        }
        return true; // Include all if no filters specified
    };

    oss << "{" << nl_char;
    current_indent_level++;

    bool first_field = true;

    auto write_field_key = [&](std::string_view key) {
        if (!first_field) {
            oss << "," << nl_char;
        } else {
            first_field = false;
        }
        for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
        oss << "\"" << escapeJson(std::string(key)) << "\": ";
    };

    // Timestamp
    if (should_include_field("timestamp")) {
        write_field_key("timestamp");
        if (options.timestamp_format == TimestampFormat::UnixMillis)
        {
            oss << std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count();
        }
        else
        {
            TimestampFormatOptions formatOpts;
            formatOpts.precision = options.precision;
            formatOpts.timezone = options.timezone;
            formatOpts.custom_format = options.custom_timestamp_format;
            oss << "\"" << formatTimestamp(time_point, options.timestamp_format, formatOpts) << "\"";
        }
    }

    // Level
    if (should_include_field("level")) {
        write_field_key("level");
        oss << "\"" << levelToString(level) << "\"";
    }
    
    // Message
    if (should_include_field("message")) {
        write_field_key("message");
        oss << "\"" << escapeJson(message) << "\"";
    }

    // Process ID
    if (should_include_field("process_id") && process_id != 0) {
        write_field_key("process_id");
        oss << process_id;
    }

    // Host Name
    if (should_include_field("host_name") && !host_name.empty()) {
        write_field_key("host_name");
        oss << "\"" << escapeJson(host_name) << "\"";
    }

    // App Name
    if (should_include_field("app_name") && !app_name.empty()) {
        write_field_key("app_name");
        oss << "\"" << escapeJson(app_name) << "\"";
    }

    // Event ID
    if (should_include_field("event_id") && !event_id.empty()) {
        write_field_key("event_id");
        oss << "\"" << escapeJson(event_id) << "\"";
    }

    // Source
    bool source_present = !source_file.empty() || !source_function.empty() || source_line != 0;
    if (should_include_field("source") && (source_present || !options.exclude_empty))
    {
        write_field_key("source");
        if (!source_present && options.exclude_empty) {
            oss << "{}";
        } else {
            oss << "{" << nl_char;
            current_indent_level++;
            bool first_source_field = true;
            auto write_source_field = [&](std::string_view key, const auto& value, bool is_string = true) {
                if (!first_source_field) {
                    oss << "," << nl_char;
                } else {
                    first_source_field = false;
                }
                for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
                oss << "\"" << escapeJson(std::string(key)) << "\": ";
                if (is_string) oss << "\"";
                oss << value;
                if (is_string) oss << "\"";
            };

            if (!source_file.empty()) write_source_field("file", source_file);
            if (!source_function.empty()) write_source_field("function", source_function);
            if (source_line != 0) write_source_field("line", source_line, false);
            
            oss << nl_char;
            current_indent_level--;
            for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
            oss << "}";
        }
    }

    // Thread ID
    if (should_include_field("thread_id") && !thread_id.empty() && options.include_thread) {
        write_field_key("thread_id");
        oss << "\"" << escapeJson(thread_id) << "\"";
    }

    // Thread Name
    if (should_include_field("thread_name") && !thread_name.empty() && options.include_thread) {
        write_field_key("thread_name");
        oss << "\"" << escapeJson(thread_name) << "\"";
    }

    // Tracing
    if (options.include_tracing)
    {
        if (should_include_field("trace_id") && !trace_id.empty()) {
            write_field_key("trace_id");
            oss << "\"" << escapeJson(trace_id) << "\"";
        }
        if (should_include_field("span_id") && !span_id.empty()) {
            write_field_key("span_id");
            oss << "\"" << escapeJson(span_id) << "\"";
        }
    }

    // Tags list
    if (should_include_field("tags") && (!tags.empty() || !options.exclude_empty))
    {
        write_field_key("tags");
        if (tags.empty() && options.exclude_empty) {
            oss << "[]";
        } else {
            oss << "[";
            bool first_tag = true;
            if (options.pretty) current_indent_level++;
            for (const auto &tag : tags)
            {
                if (!first_tag) oss << ",";
                if (options.pretty) oss << nl_char;
                if (options.pretty) { for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix; }
                oss << "\"" << escapeJson(tag) << "\"";
                first_tag = false;
            }
            if (options.pretty) current_indent_level--;
            if (options.pretty && !tags.empty()) { oss << nl_char; for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix; }
            oss << "]";
        }
    }

    // Attributes object
    if (should_include_field("attributes") && (!attributes.empty() || !options.exclude_empty))
    {
        write_field_key("attributes");
        if (attributes.empty() && options.exclude_empty) {
            oss << "{}";
        } else {
            oss << "{" << nl_char;
            current_indent_level++;
            bool first_attr = true;
            for (const auto &[key, value] : attributes)
            {
                if (!first_attr) {
                    oss << "," << nl_char;
                } else {
                    first_attr = false;
                }
                for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
                oss << "\"" << escapeJson(key) << "\": ";
                std::visit(JsonVisitor{oss, options, current_indent_level}, value);
            }
            oss << nl_char;
            current_indent_level--;
            for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
            oss << "}";
        }
    }

    current_indent_level--;
    oss << nl_char;
    for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
    oss << "}";
    return oss.str();
}

void LogEntry::toStream(std::ostream& os, const JsonOptions& options) const {
    os << toJson(options);
}

// Explicit implementation for LogEntry comparison operators
std::strong_ordering LogEntry::operator<=>(const LogEntry &other) const
{
    bool has_tp = time_point.time_since_epoch().count() != 0;
    bool other_has_tp = other.time_point.time_since_epoch().count() != 0;

    if (has_tp && other_has_tp)
    {
        auto cmp = time_point <=> other.time_point;
        if (cmp != 0)
            return cmp;
    }
    else
    {
        auto cmp = timestamp <=> other.timestamp;
        if (cmp != 0)
            return cmp;
    }

    if (tags != other.tags)
    {
        return tags <=> other.tags;
    }

    if (trace_id != other.trace_id)
    {
        return trace_id <=> other.trace_id;
    }
    if (span_id != other.span_id)
    {
        return span_id <=> other.span_id;
    }

    auto msg_cmp = message <=> other.message;
    if (msg_cmp != 0) return msg_cmp;

    return event_id <=> other.event_id;
}

bool LogEntry::operator==(const LogEntry &other) const
{
    return (*this <=> other) == 0;
}

// Stream Support
std::ostream &operator<<(std::ostream &os, const LogEntry &entry)
{
    os << "[" << (entry.timestamp.empty() ? entry.generatedTimestampString() : entry.timestamp) << "] "
       << "[" << LogEntry::levelToString(entry.level) << "] "
       << entry.message;
    return os;
}

// std::hash specialization for LogEntry
namespace std {
    size_t hash<LogEntry>::operator()(const LogEntry& entry) const noexcept {
        size_t seed = 0;

        hash_combine(seed, static_cast<std::underlying_type_t<LogLevel>>(entry.level));
        hash_combine(seed, entry.message);
        hash_combine(seed, entry.process_id);
        hash_combine(seed, entry.host_name);
        hash_combine(seed, entry.app_name);
        hash_combine(seed, entry.source_file);
        hash_combine(seed, entry.source_function);
        hash_combine(seed, entry.source_line);
        hash_combine(seed, entry.thread_id);
        hash_combine(seed, entry.thread_name);
        hash_combine(seed, entry.trace_id);
        hash_combine(seed, entry.span_id);
        hash_combine(seed, entry.event_id);

        hash_combine(seed, entry.time_point.time_since_epoch().count());

        for (const auto& tag : entry.tags) {
            hash_combine(seed, tag);
        }

        for (const auto& pair : entry.attributes) {
            hash_combine(seed, pair.first);
            hash_combine(seed, pair.second);
        }

        return seed;
    }
} // namespace std
