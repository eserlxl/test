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
#include <expected> // For C++23 std::expected
#include <chrono> // For C++20 chrono features, including time zones

#include <model/LogFormattingOptions.h> // Include the full definition, defining global enums

namespace LogTimeUtil {

    // Error enum for detailed parsing failures
    enum class ParseErrorType {
        InvalidFormat,          // The string does not match the expected format.
        OutOfRange,             // A component (e.g., day, month) is invalid.
        UnsupportedTimezone,    // The timezone specifier is not supported or ambiguous.
        AmbiguousFormat,        // Multiple formats matched, but none uniquely identifiable.
        TimezoneConversionError // Error during timezone conversion.
    };

    struct ParseErrorInfo {
        ParseErrorType code;
        std::string message;

        // Helper for creating ParseResult with an error
        static ParseErrorInfo fromCode(ParseErrorType c, const std::string& msg = "") {
            return {c, msg};
        }
    };

    // Type alias for parse results for readability
    using ParseResult = std::expected<std::chrono::system_clock::time_point, ParseErrorInfo>;

    // New options struct for parsing functions
    struct ParseOptions {
        bool strict = true; // Default to strict parsing
        std::optional<std::string> defaultTimezoneName; // Hint for parsing ambiguous timestamps (e.g., "PST")
        std::optional<std::string> localeName;          // Hint for locale-specific parsing (e.g., month names)
    };

    struct FormatOptions {
        // Use the global enums directly, with :: scope resolution
        ::LogTimestampFormat format = ::LogTimestampFormat::ISO8601;
        ::LogPrecision precision = ::LogPrecision::Millis;
        ::LogTimezone timezone = ::LogTimezone::UTC;
        std::optional<std::string> custom_format = std::nullopt;

        // Add constructor for interoperability with model layer
        FormatOptions(const ::LogFormattingOptions& model_opts);

        // Add default constructor back since a custom constructor was defined
        FormatOptions() = default;
    };

    // --- NEW: Human-readable duration formatting ---
    /**
     * @brief Formats a duration into a human-readable string (e.g., "1h 5m 10.123s").
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param duration The duration to format.
     * @param compact If true, uses abbreviated units (e.g., "m", "s"); otherwise, uses full names ("minutes", "seconds").
     * @return A formatted string representation of the duration.
     */
    template<class Rep, class Period>
    std::string formatDuration(std::chrono::duration<Rep, Period> duration, bool compact = true);

    // Main function to format a timestamp
    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        ::LogTimestampFormat format_type, // Use global enum
        const FormatOptions& opts
    );

    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        const FormatOptions& opts = {}
    );

    // --- NEW: Timezone-aware formatting ---
    /**
     * @brief Formats a time_point into a string for a specific IANA timezone.
     * This function uses the C++20 timezone library.
     * @param tp The time_point to format.
     * @param timezone_name The IANA timezone name (e.g., "America/New_York", "Europe/London").
     * @param opts Formatting options.
     * @return Formatted timestamp string. Throws std::runtime_error if timezone is not found.
     */
    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        const std::string& timezone_name,
        const FormatOptions& opts = {}
    );

    // --- NEW: Timezone enumeration ---
    /**
     * @brief Retrieves the list of available IANA timezone names from the system's database.
     * @return A vector of strings, where each string is a valid IANA timezone name.
     */
    std::vector<std::string> getAvailableTimezones();


    // --- UPDATED: All parsing functions now return ParseResult ---
    ParseResult parseTimestamp(
        std::string_view timestamp_str,
        std::string_view format_str,
        const ParseOptions& opts = {}
    );

    ParseResult parseTimestamp(
        std::string_view timestamp_str,
        ::LogTimestampFormat format_type,
        const ParseOptions& opts = {}
    );

    // --- NEW: Auto-detection parsing function, replaces the deprecated one ---
    ParseResult parseTimestamp(
        std::string_view timestamp_str,
        const ParseOptions& opts = {}
    );

    // --- DEPRECATED: generateLogEntryTimestampString will be removed in a future iteration ---
    [[deprecated("Use formatTimestamp with FormatOptions(logFormattingOptions) instead.")]]
    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        LogFormattingOptions options
    );

    // Old generateLogEntryTimestampString overload, now also deprecated
    [[deprecated("Use formatTimestamp with FormatOptions(logFormattingOptions) instead.")]]
    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        const LogFormattingOptions& options
    );

    // --- NEW: Timezone and local time conversions ---
    /**
     * @brief Converts a system_clock::time_point to a zoned_time in the specified IANA timezone.
     * @param tp The system_clock::time_point to convert (assumed UTC).
     * @param timezoneName The IANA timezone name (e.g., "America/New_York").
     * @return A std::chrono::zoned_time representing the time in the target timezone.
     * @throws std::runtime_error if the timezoneName is invalid or not found.
     */
    std::chrono::zoned_time<std::chrono::system_clock::duration> toZonedTime(
        std::chrono::system_clock::time_point tp,
        const std::string& timezoneName
    );

    /**
     * @brief Converts a system_clock::time_point (UTC) to a local_time using the system's current timezone.
     * @param tp The system_clock::time_point to convert.
     * @return A std::chrono::local_time representing the time in the local timezone.
     */
    std::chrono::local_time<std::chrono::system_clock::duration> toLocal(
        std::chrono::system_clock::time_point tp
    );

    /**
     * @brief Converts a local_time in a specified timezone to a system_clock::time_point (UTC).
     * @param lt The std::chrono::local_time to convert.
     * @param timezoneName The IANA timezone name of the local_time.
     * @return A std::chrono::system_clock::time_point in UTC.
     * @throws std::runtime_error if the timezoneName is invalid or not found.
     */
    std::chrono::system_clock::time_point toUtc(
        std::chrono::local_time<std::chrono::system_clock::duration> lt,
        const std::string& timezoneName
    );

    // --- NEW: Custom format string validation ---
    /**
     * @brief Validates if a given format string is a valid chrono format string.
     * This function attempts to parse a dummy time_point using the format string
     * and catches any exceptions, indicating an invalid format.
     * @param formatStr The format string to validate.
     * @return True if the format string is valid, false otherwise.
     */
    bool isValidChronoFormatString(std::string_view formatStr);

    // Helper for debugging/logging chrono durations (kept for now, but formatDuration is preferred)
    std::string to_string(std::chrono::nanoseconds ns);

} // namespace LogTimeUtil

#endif // LOG_TIME_UTIL_H
