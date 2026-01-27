#include "LogEntry.h"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <typeinfo>

LogEntry::LogEntry() : level(LogLevel::UNKNOWN) {}

LogLevel LogEntry::parseLevel(std::string_view level_str) {
    std::string upper_level(level_str);
    std::transform(upper_level.begin(), upper_level.end(), upper_level.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    
    if (upper_level == "DEBUG" || upper_level == "DBG") {
        return LogLevel::DEBUG;
    } else if (upper_level == "INFO" || upper_level == "INF") {
        return LogLevel::INFO;
    } else if (upper_level == "WARNING" || upper_level == "WARN") {
        return LogLevel::WARNING;
    } else if (upper_level == "ERROR" || upper_level == "ERR") {
        return LogLevel::ERROR;
    } else if (upper_level == "CRITICAL" || upper_level == "CRIT" || upper_level == "FATAL") {
        return LogLevel::CRITICAL;
    }
    
    return LogLevel::UNKNOWN;
}

std::string_view LogEntry::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARNING: return "WARNING";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

LogEntry LogEntry::create(LogLevel level, std::string_view message, std::source_location loc) {
    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.time_point = std::chrono::system_clock::now();
    entry.timestamp = entry.generatedTimestampString();
    entry.withSource(loc);
    
    std::stringstream ss;
    ss << std::this_thread::get_id();
    entry.thread_id = ss.str();
    
    return entry;
}

LogEntry& LogEntry::withLevel(LogLevel l) {
    level = l;
    return *this;
}

LogEntry& LogEntry::withMessage(std::string_view msg) {
    message = msg;
    return *this;
}

LogEntry& LogEntry::withAttribute(std::string key, LogValue value) {
    attributes[std::move(key)] = std::move(value);
    return *this;
}

LogEntry& LogEntry::withThreadId(std::string_view tid) {
    thread_id = tid;
    return *this;
}

LogEntry& LogEntry::withThreadId(std::thread::id tid) {
    std::stringstream ss;
    ss << tid;
    thread_id = ss.str();
    return *this;
}

LogEntry& LogEntry::withTimestamp(std::chrono::system_clock::time_point tp, bool include_fractional) {
    time_point = tp;
    timestamp = generatedTimestampString(include_fractional);
    return *this;
}

LogEntry& LogEntry::withSource(std::source_location loc) {
    source_file = loc.file_name();
    source_function = loc.function_name();
    source_line = loc.line();
    return *this;
}

LogEntry& LogEntry::withTag(std::string_view tag) {
    tags.emplace(tag);
    return *this;
}

LogEntry& LogEntry::withTags(std::initializer_list<std::string_view> tag_list) {
    for (auto tag : tag_list) {
        tags.emplace(tag);
    }
    return *this;
}

LogEntry& LogEntry::withException(const std::exception& e) {
    withAttribute("exception_type", std::string(typeid(e).name()));
    withAttribute("exception_message", std::string(e.what()));
    return *this;
}

bool LogEntry::parseTime() {
    std::istringstream ss(timestamp);
    std::tm tm = {};
    // Format: 2023-10-27 10:00:00.000
    char dash1, dash2, space, colon1, colon2;
    int year, month, day, hour, min, sec;
    
    if (!(ss >> year >> dash1 >> month >> dash2 >> day >> hour >> colon1 >> min >> colon2 >> sec)) {
        return false;
    }
    
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;
    
    std::time_t tt = std::mktime(&tm);
    if (tt == -1) return false;
    
    auto tp = std::chrono::system_clock::from_time_t(tt);
    
    // Check for fractional seconds
    if (ss.peek() == '.') {
        char dot;
        double fractional_sec;
        ss >> dot >> fractional_sec;
        if (!ss.fail()) {
            // fractional_sec is something like 0.123 if string was .123?
            // Wait, ss >> fractional_sec after '.' will read the numbers.
            // If it's .123, ss >> fractional_sec will read 123 if it's an int.
            // Let's do it better.
            std::string frac_str;
            ss >> frac_str;
            if (!frac_str.empty()) {
                try {
                    double val = std::stod("0." + frac_str);
                    auto nanos = std::chrono::nanoseconds(static_cast<long long>(val * 1'000'000'000));
                    tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(nanos);
                } catch (...) {}
            }
        }
    }
    
    time_point = tp;
    return true;
}

std::string LogEntry::generatedTimestampString(bool include_fractional) const {
    if (time_point.time_since_epoch().count() == 0) return "";
    
    std::time_t tt = std::chrono::system_clock::to_time_t(time_point);
    std::tm tm = {};
    #if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm, &tt);
    #else
        localtime_r(&tt, &tm);
    #endif
    
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    
    if (include_fractional) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            time_point.time_since_epoch()) % 1000;
        ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    }
    
    return ss.str();
}

void LogEntry::setAttribute(const std::string& key, const std::string& value) {
    attributes[key] = value;
}

void LogEntry::setAttribute(const std::string& key, const char* value) {
    attributes[key] = std::string(value);
}

void LogEntry::setAttribute(const std::string& key, LogValue value) {
    attributes[key] = std::move(value);
}

std::string LogEntry::getAttributeAsString(const std::string& key) const {
    auto it = attributes.find(key);
    if (it == attributes.end()) return "";
    
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(arg);
        }
        return "";
    }, it->second);
}

bool LogEntry::hasAttribute(const std::string& key) const {
    return attributes.contains(key);
}

std::optional<LogValue> LogEntry::getAttribute(const std::string& key) const {
    auto it = attributes.find(key);
    if (it != attributes.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool LogEntry::hasTag(std::string_view tag) const {
    return tags.contains(tag);
}

// Helper to escape JSON strings
static std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (auto c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= (unsigned char)c && (unsigned char)c <= '\x1f') {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)(unsigned char)c;
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

std::string LogEntry::toJson(const JsonOptions& options) const {
    std::ostringstream oss;
    std::string indent = options.pretty ? "  " : "";
    std::string nl = options.pretty ? "\n" : "";
    
    oss << "{" << nl;
    oss << indent << "\"timestamp\":\"" << escapeJson(timestamp.empty() ? generatedTimestampString() : timestamp) << "\"," << nl;
    oss << indent << "\"level\":\"" << levelToString(level) << "\"," << nl;
    oss << indent << "\"message\":\"" << escapeJson(message) << "\"";

    if (options.include_source && !source_file.empty()) {
        oss << "," << nl << indent << "\"source\":{" << nl;
        oss << indent << indent << "\"file\":\"" << escapeJson(source_file) << "\"," << nl;
        oss << indent << indent << "\"function\":\"" << escapeJson(source_function) << "\"," << nl;
        oss << indent << indent << "\"line\":" << source_line << nl;
        oss << indent << "}";
    }

    if (options.include_thread && !thread_id.empty()) {
        oss << "," << nl << indent << "\"thread_id\":\"" << escapeJson(thread_id) << "\"";
    }

    if (!tags.empty()) {
        oss << "," << nl << indent << "\"tags\":[";
        bool first = true;
        for (const auto& tag : tags) {
            if (!first) oss << ",";
            oss << "\"" << escapeJson(tag) << "\"";
            first = false;
        }
        oss << "]";
    }

    if (!attributes.empty()) {
        oss << "," << nl << indent << "\"attributes\":{" << nl;
        bool first = true;
        for (const auto& [key, value] : attributes) {
            if (!first) oss << "," << nl;
            oss << indent << indent << "\"" << escapeJson(key) << "\":";
            
            std::visit([&oss](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    oss << "\"" << escapeJson(arg) << "\"";
                } else if constexpr (std::is_same_v<T, bool>) {
                    oss << (arg ? "true" : "false");
                } else {
                    oss << arg;
                }
            }, value);
            
            first = false;
        }
        oss << nl << indent << "}";
    }

    oss << nl << "}";
    return oss.str();
}

std::strong_ordering LogEntry::operator<=>(const LogEntry& other) const {
    // If both have valid time_points (non-zero), use them.
    bool has_tp = time_point.time_since_epoch().count() != 0;
    bool other_has_tp = other.time_point.time_since_epoch().count() != 0;

    if (has_tp && other_has_tp) {
        auto cmp = time_point <=> other.time_point;
        if (cmp != 0) return cmp;
    } else {
        // Fallback to string timestamp comparison
        auto cmp = timestamp <=> other.timestamp;
        if (cmp != 0) return cmp;
    }
    
    // Compare tags
    if (tags != other.tags) {
        return tags <=> other.tags;
    }

    // Tie-breaker: message
    return message <=> other.message;
}

bool LogEntry::operator==(const LogEntry& other) const {
    return (*this <=> other) == 0;
}

std::ostream& operator<<(std::ostream& os, const LogEntry& entry) {
    os << "[" << (entry.timestamp.empty() ? entry.generatedTimestampString() : entry.timestamp) << "] "
       << "[" << LogEntry::levelToString(entry.level) << "] "
       << entry.message;
    return os;
}

LogLevel parseLogLevel(const std::string& level_str) {
    return LogEntry::parseLevel(level_str);
}