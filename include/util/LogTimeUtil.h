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
    enum class ParseError {
        InvalidFormat,      // The string does not match the expected format.
        OutOfRange,         // A component (e.g., day, month) is invalid.
        UnsupportedTimezone // The timezone specifier is not supported or ambiguous.
    };

    // Type alias for parse results for readability
    using ParseResult = std::expected<std::chrono::system_clock::time_point, ParseError>;

    // New options struct for parsing functions
    struct ParseOptions {
        bool strict = true; // Default to strict parsing
        // Future extensions: timezone hint, locale, etc.
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

    // --- DEPRECATED: Ambiguous "magic" parser ---
    /**
     * @brief Attempts to parse a timestamp by trying a series of common formats.
     * @deprecated Scheduled for removal. Use an overload that explicitly specifies the
     *             expected format for deterministic behavior.
     */
    [[deprecated("Use an overload that specifies the format explicitly.")]]
    ParseResult parseTimestamp(std::string_view timestamp_str);

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

    // Helper for debugging/logging chrono durations (kept for now, but formatDuration is preferred)
    std::string to_string(std::chrono::nanoseconds ns);

} // namespace LogTimeUtil

#endif // LOG_TIME_UTIL_H
