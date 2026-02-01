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
#include <span>
#include <format>
#include "LogFormattingOptions.h"

using LogEntryJsonOptions = LogFormattingOptions;

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

struct LogValue : LogValueBase {
    using LogValueBase::LogValueBase;
    
    LogValue(LogList list);
    LogValue(LogObject obj);
    LogValue(std::vector<uint8_t> data);
    LogValue(std::span<const uint8_t> data);

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

    // New coercive conversion methods
    std::optional<bool> toBool() const;
    std::optional<int64_t> toInt64() const;
    std::optional<uint64_t> toUint64() const;
    std::optional<std::vector<uint8_t>> toBinary() const;

    // Generic conversion template with coercion
    template<typename T>
    std::optional<T> to() const {
        if constexpr (std::is_same_v<T, bool>) {
            return toBool();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return toInt64();
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return toUint64();
        } else if constexpr (std::is_same_v<T, double>) {
            return asDouble(); // asDouble already has coercion
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Note: toString() returns a std::string, which can be implicitly converted to std::optional<std::string>.
            // If the LogValue is not convertible to string, toString() will return a representation like "null",
            // "[]", "{}" etc., so this will always return a value.
            return toString();
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return toBinary();
        } else if constexpr (std::is_same_v<T, std::chrono::nanoseconds>) {
            return asDuration();
        }
        // Direct conversion for other types if they match exactly
        if (auto p = std::get_if<T>(static_cast<const LogValueBase*>(this))) {
            return *p;
        }
        return std::nullopt;
    }

    // Path-based access for nested data
    std::optional<LogValue*> at_path(std::string_view path);
    std::optional<const LogValue*> at_path(std::string_view path) const;

    // Member visit functions for improved ergonomics
    template<typename Visitor>
    decltype(auto) visit(Visitor&& visitor) {
        return std::visit(std::forward<Visitor>(visitor), static_cast<LogValueBase&>(*this));
    }

    template<typename Visitor>
    decltype(auto) visit(Visitor&& visitor) const {
        return std::visit(std::forward<Visitor>(visitor), static_cast<const LogValueBase&>(*this));
    }

    // Direct typed access (unsafe if wrong type) - renamed from old `to<T>()`
    template<typename T> const T& get() const { return std::get<T>(static_cast<const LogValueBase&>(*this)); }
    template<typename T> T& get() { return std::get<T>(static_cast<LogValueBase&>(*this)); }

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

    std::string toString(LogFormattingOptions::BinaryEncoding binary_encoding = LogFormattingOptions::BinaryEncoding::Hex) const;

    std::partial_ordering operator<=>(const LogValue& other) const;
    bool operator==(const LogValue& other) const;
};

// Helper function to combine hashes
template <class T>
inline void hash_combine(size_t& seed, const T& v) {
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

std::ostream& operator<<(std::ostream& os, const LogValue& value);

namespace std {
    template<> struct hash<LogValue> {
        size_t operator()(const LogValue& lv) const noexcept;
    };

    template <>
    struct formatter<LogValue> {
        LogBinaryEncoding binary_encoding = LogBinaryEncoding::Hex;

        constexpr auto parse(std::format_parse_context& ctx) {
            auto it = ctx.begin();
            if (it != ctx.end() && *it == ':') {
                ++it;
                if (it != ctx.end()) {
                    if (*it == 'h' || *it == 'H') { // Hex encoding
                        binary_encoding = LogBinaryEncoding::Hex;
                        ++it;
                    } else if (*it == 'b' || *it == 'B') { // Base64 encoding
                        binary_encoding = LogBinaryEncoding::Base64;
                        ++it;
                    }
                }
            }
            return it;
        }

        auto format(const LogValue& value, std::format_context& ctx) const {
            return std::format_to(ctx.out(), "{}", value.toString(binary_encoding));
        }
    };
}

namespace LogEntryDetail {
    std::string base64Encode(const std::vector<uint8_t>& data);

    template<typename T>
    LogValue toLogValue(T&& arg) {
        using ArgumentType = std::decay_t<T>;
        if constexpr (std::is_convertible_v<ArgumentType, std::string_view> && !std::is_arithmetic_v<ArgumentType>) {
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
        } else if constexpr (std::is_same_v<ArgumentType, std::monostate>) {
            return LogValue(std::monostate{});
        } else {
            // Fallback for types that can be streamed to an ostringstream
            if constexpr (requires { std::declval<std::ostringstream>() << arg; }) {
                std::ostringstream oss;
                oss << arg;
                return LogValue(oss.str());
            } else {
                // If it can't be streamed, return monostate for unknown types.
                return LogValue(std::monostate{});
            }
        }
    }

    inline LogValue toLogValue(LogList list) { return LogValue(std::move(list)); }
    inline LogValue toLogValue(LogObject obj) { return LogValue(std::move(obj)); }
    inline LogValue toLogValue(const std::vector<uint8_t>& data) { return LogValue(data); }
    inline LogValue toLogValue(std::span<const uint8_t> data) { return LogValue(data); }

    // Handle std::optional explicitly
    template<typename T>
    LogValue toLogValue(std::optional<T> val) {
        if (val.has_value()) {
            return toLogValue(std::move(val.value()));
        } else {
            return LogValue(std::monostate{});
        }
    }
}
#endif // LOG_VALUE_H
