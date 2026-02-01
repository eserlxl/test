#ifndef LOG_VALUE_H
#define LOG_VALUE_H

#include <string>
#include <string_view>
#include <chrono>
#include <compare>
#include <map>
#include <variant>
#include <vector>
#include <memory>
#include <optional>
#include <functional>
#include <initializer_list>
#include <ostream>
#include <sstream>
#include <set>

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
    std::vector<uint8_t>,
    std::chrono::nanoseconds,
    std::shared_ptr<LogList>,
    std::shared_ptr<LogObject>
>;

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

struct LogValue : LogValueBase {
    using LogValueBase::LogValueBase;
    
    LogValue(LogList list);
    LogValue(LogObject obj);
    LogValue(std::vector<uint8_t> data);

    LogValue(std::string_view s);
    LogValue(const char* s);
    LogValue(std::chrono::system_clock::time_point tp);
    template <typename Rep, typename Period>
    LogValue(std::chrono::duration<Rep, Period> d) : LogValueBase(std::chrono::duration_cast<std::chrono::nanoseconds>(d)) {}

    template<typename T>
    LogValue(std::optional<T> val) {
        if (val.has_value()) {
            *this = val.value();
        } else {
            *this = std::monostate{};
        }
    }

    LogValue(std::initializer_list<LogValue> init_list);
    LogValue(std::initializer_list<std::pair<const char*, LogValue>> init_list);

    ValueType type() const noexcept;
    bool is(ValueType t) const noexcept;
    template<typename T> bool is() const noexcept {
        return std::holds_alternative<T>(*this);
    }
    bool isNull() const noexcept;

    std::optional<const LogList*> asList() const;
    std::optional<const LogObject*> asObject() const;

    std::optional<bool> asBool() const;
    std::optional<int64_t> asInt64() const;
    std::optional<uint64_t> asUint64() const;
    std::optional<double> asDouble() const;
    std::optional<const std::string*> asString() const;
    std::optional<const std::vector<uint8_t>*> asBinary() const;
    std::optional<std::chrono::nanoseconds> asDuration() const;
    std::optional<std::string_view> asStringView() const;

    template<typename T> const T& to() const { return std::get<T>(static_cast<const LogValueBase&>(*this)); }
    template<typename T> T& to() { return std::get<T>(static_cast<LogValueBase&>(*this)); }

    template<typename T> T get_or_default(const T& default_value) const {
        if (auto p = get_if<T>()) {
            return *p;
        }
        return default_value;
    }

    LogValue& operator[](size_t index);
    const LogValue& operator[](size_t index) const;

    LogValue& operator[](std::string_view key);
    const LogValue& operator[](std::string_view key) const;
    template<typename T> const T* get_if() const noexcept { return std::get_if<T>(static_cast<const LogValueBase*>(this)); }
    template<typename T> T* get_if() noexcept { return std::get_if<T>(static_cast<LogValueBase*>(this)); }

    std::string toString(LogEntryJsonOptions::BinaryEncoding binary_encoding = LogEntryJsonOptions::BinaryEncoding::Hex) const;

    std::partial_ordering operator<=>(const LogValue& other) const;
    bool operator==(const LogValue& other) const;
};

std::ostream& operator<<(std::ostream& os, const LogValue& value);

namespace std {
    template<> struct hash<LogValue> {
        size_t operator()(const LogValue& lv) const noexcept;
    };
}

namespace LogEntryDetail {
    template<typename T>
    LogValue toLogValue(T&& arg) {
        using ArgumentType = std::decay_t<T>;
        if constexpr (std::is_convertible_v<ArgumentType, std::string_view>) {
            return LogValue(static_cast<std::string_view>(arg));
        } else if constexpr (std::is_integral_v<ArgumentType> && !std::is_same_v<bool, ArgumentType>) {
            if constexpr (std::is_signed_v<ArgumentType>) {
                return LogValue(static_cast<int64_t>(arg));
            } else {
                return LogValue(static_cast<uint64_t>(arg));
            }
        } else if constexpr (std::is_floating_point_v<ArgumentType>) {
            return LogValue(static_cast<double>(arg));
            } else if constexpr (std::is_same_v<bool, ArgumentType>) {
            return LogValue(static_cast<bool>(arg));
        } else {
            std::ostringstream oss;
            oss << arg;
            return LogValue(oss.str());
        }
    }

    inline LogValue toLogValue(LogList list) { return LogValue(std::move(list)); }
    inline LogValue toLogValue(LogObject obj) { return LogValue(std::move(obj)); }
}

#endif // LOG_VALUE_H
