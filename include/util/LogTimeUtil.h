#ifndef LOG_TIME_UTIL_H
#define LOG_TIME_UTIL_H

#include <string>
#include <string_view>
#include <chrono>
#include <optional>
#include <vector>
#include <tuple>
#include <map>
#include <stdexcept>
#include <ctime> // Required for std::tm and mktime
#include <iomanip> // Required for std::get_time and std::put_time

#include <model/LogFormattingOptions.h> // Include the full definition, defining global enums

namespace LogTimeUtil {

    struct FormatOptions {
        // Use the global enums directly, with :: scope resolution
        ::LogTimestampFormat format = ::LogTimestampFormat::ISO8601;
        ::LogPrecision precision = ::LogPrecision::Milliseconds;
        ::LogTimezone timezone = ::LogTimezone::UTC;
        std::optional<std::string> custom_format = std::nullopt;
    };

    // Main function to format a timestamp
    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        ::LogTimestampFormat format_type = ::LogTimestampFormat::ISO8601, // Use global enum
        const FormatOptions& opts = {}
    );

    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        const FormatOptions& opts
    );

    // Main function to parse a timestamp
    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        ::LogTimestampFormat format_type // Use global enum
    );

    // New overload for custom format string
    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        std::string_view format_str // New overload
    );

    // Deprecated. Use the overload with FormatOptions struct directly.
    [[deprecated("Use generateLogEntryTimestampString(time_point, const LogFormattingOptions&) instead.")]]
    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        const LogFormattingOptions& options
    );

    // New API for internal LogEntry timestamp generation
    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        LogFormattingOptions options // Pass by value to allow modification
    );

    // Strict parse timestamp for Iteration 3
    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        ::LogTimestampFormat format_type, // Use global enum
        bool strict // This is the strictness parameter
    );

    // Wrapper for deprecated parseTimestamp(string_view) in LogEntry
    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str
    );

    // Helper for debugging/logging chrono durations
    std::string to_string(std::chrono::nanoseconds ns);

} // namespace LogTimeUtil

#endif // LOG_TIME_UTIL_H
