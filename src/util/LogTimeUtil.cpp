#include "util/LogTimeUtil.h"
#include "model/LogFormattingOptions.h"
#include <iomanip>
#include <sstream>
#include <chrono>
#include <cctype>
#include <locale>
#include <vector>
#include <iostream>
#include <string_view>
#include <cmath>
#include <algorithm>
#include <array>
#include <format> // For std::format in C++20 and std::format_error
#include <ctime>  // For std::gmtime, std::localtime, std::strftime
#include <chrono> // Required for std::chrono::current_zone, locate_zone, get_tzdb
#include <limits> // For numeric_limits

namespace LogTimeUtil {

FormatOptions::FormatOptions(const ::LogFormattingOptions& model_opts)
    : format(model_opts.timestamp_format),
      precision(model_opts.precision),
      timezone(model_opts.timezone),
      custom_format(model_opts.custom_timestamp_format)
{}

template<class Rep, class Period>
std::string formatDuration(std::chrono::duration<Rep, Period> duration_param, bool compact) {
    using namespace std::chrono;
    std::string result;
    bool is_negative = (duration_param < duration_param.zero());
    if (is_negative) {
        result += "-";
        duration_param = -duration_param;
    }

    if (duration_param == duration_param.zero()) {
        return result + "0s";
    }

    nanoseconds total_remaining_ns = duration_cast<nanoseconds>(duration_param);
    if (total_remaining_ns == nanoseconds::zero() && duration_param != duration_param.zero()) {
        return result + "0s"; // Duration was non-zero but too small for ns
    }

    std::vector<std::string> parts;

    auto append_part = [&](long long value, const std::string& unit_compact, const std::string& unit_full) {
        if (value > 0) {
            parts.push_back(std::to_string(value) + (compact ? unit_compact : unit_full));
        }
    };

    // Years (approximate, using 365.2425 days per year for chrono::years compatibility)
    auto d_years = duration_cast<years>(total_remaining_ns);
    if (d_years.count() > 0) {
        append_part(d_years.count(), "y", " years");
        total_remaining_ns -= d_years;
    }

    auto d_days = duration_cast<days>(total_remaining_ns);
    if (d_days.count() > 0) {
        append_part(d_days.count(), "d", " days");
        total_remaining_ns -= d_days;
    }

    auto d_hours = duration_cast<hours>(total_remaining_ns);
    if (d_hours.count() > 0) {
        append_part(d_hours.count(), "h", " hours");
        total_remaining_ns -= d_hours;
    }

    auto d_minutes = duration_cast<minutes>(total_remaining_ns);
    if (d_minutes.count() > 0) {
        append_part(d_minutes.count(), "m", " minutes");
        total_remaining_ns -= d_minutes;
    }

    // Handle seconds and fractional seconds
    auto d_seconds = duration_cast<seconds>(total_remaining_ns);
    if (d_seconds.count() > 0 || total_remaining_ns > nanoseconds::zero()) { // If there are any seconds or fractional seconds
        std::string sec_str = std::to_string(d_seconds.count());
        total_remaining_ns -= d_seconds;

        if (total_remaining_ns > nanoseconds::zero()) {
            // Format fractional part
            long long fractional_ns = total_remaining_ns.count();
            std::string frac_part_str = std::to_string(fractional_ns);
            // Pad with leading zeros up to 9 digits (nanoseconds)
            if (frac_part_str.length() < 9) {
                frac_part_str.insert(0, 9 - frac_part_str.length(), '0');
            }
            // Trim trailing zeros
            frac_part_str.erase(frac_part_str.find_last_not_of('0') + 1, std::string::npos);
            if (!frac_part_str.empty()) {
                sec_str += "." + frac_part_str;
            }
        }
        parts.push_back(sec_str + (compact ? "s" : " seconds"));
    } else if (parts.empty() && total_remaining_ns == nanoseconds::zero()) {
        // If the original duration was smaller than seconds and became zero after other units
        // (e.g. 0.0001ms became 0ns) and nothing was appended.
        return result + "0s";
    }

    if (parts.empty()) { // Case like duration is < 1s but non-zero, and no larger units
        if (total_remaining_ns > nanoseconds::zero()) {
            auto ms = duration_cast<milliseconds>(total_remaining_ns);
            if (ms.count() > 0) {
                append_part(ms.count(), "ms", " milliseconds");
            } else {
                auto us = duration_cast<microseconds>(total_remaining_ns);
                if (us.count() > 0) {
                    append_part(us.count(), "us", " microseconds");
                } else {
                    append_part(total_remaining_ns.count(), "ns", " nanoseconds");
                }
            }
        }
    }


    bool first_part = true;
    for (const auto& part_str : parts) {
        if (!first_part) {
            result += " ";
        }
        result += part_str;
        first_part = false;
    }

    return result;
}

template std::string formatDuration(std::chrono::nanoseconds duration, bool compact);
template std::string formatDuration(std::chrono::microseconds duration, bool compact);
template std::string formatDuration(std::chrono::milliseconds duration, bool compact);
template std::string formatDuration(std::chrono::seconds duration, bool compact);
template std::string formatDuration(std::chrono::minutes duration, bool compact);
template std::string formatDuration(std::chrono::hours duration, bool compact);
template std::string formatDuration(std::chrono::days duration, bool compact); // Added days
template std::string formatDuration(std::chrono::weeks duration, bool compact); // Added weeks
template std::string formatDuration(std::chrono::years duration, bool compact); // Added years
template std::string formatDuration(std::chrono::months duration, bool compact); // Added months

// --- New `getFractionalString` helper for `formatTimestamp` ---
std::string getFractionalString(std::chrono::system_clock::time_point tp, ::LogPrecision precision) {
    if (precision == ::LogPrecision::Seconds) {
        return "";
    }
    
    auto since_epoch = tp.time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(since_epoch);
    auto fractional = since_epoch - seconds;
    long long fractional_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(fractional).count();

    if (fractional_ns == 0) {
        return "";
    }

    std::stringstream ss;
    ss << ".";
    std::string nanos_str = std::to_string(fractional_ns);
    // Pad with leading zeros up to 9 digits if necessary
    if (nanos_str.length() < 9) {
        nanos_str.insert(0, 9 - nanos_str.length(), '0');
    }
    
    // Trim trailing zeros
    nanos_str.erase(nanos_str.find_last_not_of('0') + 1, std::string::npos);

    switch (precision) {
        case ::LogPrecision::Millis: ss << nanos_str.substr(0, std::min((size_t)3, nanos_str.length())); break;
        case ::LogPrecision::Micros: ss << nanos_str.substr(0, std::min((size_t)6, nanos_str.length())); break;
        case ::LogPrecision::Nanos:  ss << nanos_str.substr(0, std::min((size_t)9, nanos_str.length())); break;
        default: return ""; 
    }
    return ss.str();
}

std::string formatTimestamp(std::chrono::system_clock::time_point tp, const FormatOptions& opts) {
    if (tp.time_since_epoch().count() == 0) return "";

    std::string formatted_str;
    std::string custom_format_str_val = opts.custom_format.value_or("");

    // Special handling for Unix timestamps
    if (opts.format == ::LogTimestampFormat::UnixMillis) {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    } else if (opts.format == ::LogTimestampFormat::UnixSeconds) {
        return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count());
    } else if (opts.format == ::LogTimestampFormat::UnixNanos) {
        return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
    }

    // Determine target timezone
    const std::chrono::time_zone* tz_ptr = nullptr;
    try {
        if (opts.timezone == ::LogTimezone::Local) {
            tz_ptr = std::chrono::current_zone();
        } else {
            tz_ptr = std::chrono::locate_zone("UTC");
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: Timezone error during formatting: " << e.what() << ". Falling back to UTC." << std::endl;
        tz_ptr = std::chrono::locate_zone("UTC");
    }
    
    std::chrono::zoned_time zt(tz_ptr, tp);

    std::string chrono_format_specifier;
    if (custom_format_str_val.empty()) {
        switch (opts.format) {
            case ::LogTimestampFormat::ISO8601:
                chrono_format_specifier = "%Y-%m-%dT%H:%M:%S"; // Timezone info handled separately
                break;
            case ::LogTimestampFormat::RFC1123:
                chrono_format_specifier = "%a, %d %b %Y %H:%M:%S %Z"; // Example: "Tue, 03 Jun 2008 11:05:30 GMT"
                break;
            case ::LogTimestampFormat::CommonLogFormat:
                chrono_format_specifier = "%d/%b/%Y:%H:%M:%S %z"; // Example: "10/Oct/2000:13:55:36 -0700"
                break;
            case ::LogTimestampFormat::Default: // Fallback for Default to ISO8601 if no custom format
            case ::LogTimestampFormat::UnixMillis: // Handled above
            case ::LogTimestampFormat::UnixSeconds: // Handled above
            case ::LogTimestampFormat::UnixNanos: // Handled above
                chrono_format_specifier = "%Y-%m-%dT%H:%M:%S";
                break;
        }
    } else {
        chrono_format_specifier = custom_format_str_val;
    }

    formatted_str = std::vformat("{:" + chrono_format_specifier + "}", std::make_format_args(zt));
    
    // Add fractional seconds if applicable and not explicitly handled by custom format
    if (custom_format_str_val.empty() || 
        (custom_format_str_val.find("%f") == std::string::npos && 
         custom_format_str_val.find("%F") == std::string::npos && 
         custom_format_str_val.find("%Ez") == std::string::npos)) { // Only add if not already in custom format
        formatted_str += getFractionalString(tp, opts.precision);
    }

    // Add timezone designator for ISO8601 if not handled by custom format
    if (opts.format == ::LogTimestampFormat::ISO8601 && custom_format_str_val.empty()) {
        if (opts.timezone == ::LogTimezone::UTC) {
            formatted_str += "Z";
        } else {
             formatted_str += std::format("{:%Ez}", zt);
        }
    }
    
    return formatted_str;
}

std::string formatTimestamp(
    std::chrono::system_clock::time_point tp,
    ::LogTimestampFormat format_type,
    const FormatOptions& opts
) {
    FormatOptions new_opts = opts;
    new_opts.format = format_type;
    return formatTimestamp(tp, new_opts);
}

std::string formatTimestamp(
    std::chrono::system_clock::time_point tp,
    const std::string& timezone_name,
    const FormatOptions& opts
) {
    if (tp.time_since_epoch().count() == 0) return "";
    
    std::string formatted_str;
    std::string custom_format_str_val = opts.custom_format.value_or("");

    // Special handling for Unix timestamps
    if (opts.format == ::LogTimestampFormat::UnixMillis) {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    } else if (opts.format == ::LogTimestampFormat::UnixSeconds) {
        return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count());
    } else if (opts.format == ::LogTimestampFormat::UnixNanos) {
        return std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count());
    }

    const std::chrono::time_zone* tz_ptr;
    try {
        tz_ptr = std::chrono::locate_zone(timezone_name);
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid IANA timezone name: " + timezone_name + " - " + e.what());
    }

    std::chrono::zoned_time zt(tz_ptr, tp);

    std::string chrono_format_specifier;
    if (custom_format_str_val.empty()) {
        switch (opts.format) {
            case ::LogTimestampFormat::ISO8601:
                chrono_format_specifier = "%Y-%m-%dT%H:%M:%S"; // Timezone info handled separately
                break;
            case ::LogTimestampFormat::RFC1123:
                chrono_format_specifier = "%a, %d %b %Y %H:%M:%S %Z"; // Example: "Tue, 03 Jun 2008 11:05:30 GMT"
                break;
            case ::LogTimestampFormat::CommonLogFormat:
                chrono_format_specifier = "%d/%b/%Y:%H:%M:%S %z"; // Example: "10/Oct/2000:13:55:36 -0700"
                break;
            case ::LogTimestampFormat::Default: // Fallback for Default to ISO8601 if no custom format
            case ::LogTimestampFormat::UnixMillis: // Handled above
            case ::LogTimestampFormat::UnixSeconds: // Handled above
            case ::LogTimestampFormat::UnixNanos: // Handled above
                chrono_format_specifier = "%Y-%m-%dT%H:%M:%S";
                break;
        }
    } else {
        chrono_format_specifier = custom_format_str_val;
    }

    formatted_str = std::vformat("{:" + chrono_format_specifier + "}", std::make_format_args(zt));
    
    // Add fractional seconds if applicable and not explicitly handled by custom format
    if (custom_format_str_val.empty() || 
        (custom_format_str_val.find("%f") == std::string::npos && 
         custom_format_str_val.find("%F") == std::string::npos && 
         custom_format_str_val.find("%Ez") == std::string::npos)) { // Only add if not already in custom format
        formatted_str += getFractionalString(tp, opts.precision);
    }

    // Add timezone designator for ISO8601 if not handled by custom format
    if (opts.format == ::LogTimestampFormat::ISO8601 && custom_format_str_val.empty()) {
        formatted_str += std::format("{:%Ez}", zt);
    }
    
    return formatted_str;
}
    
std::vector<std::string> getAvailableTimezones() {
    std::vector<std::string> timezones;
    const auto& db = std::chrono::get_tzdb();
    if (db.zones.empty()) {
        // Fallback for environments where the TZ database is not available
        return {"UTC", "GMT", "Etc/UTC"}; 
    }
    for (const auto& zone : db.zones) {
        timezones.push_back(std::string(zone.name()));
    }
    std::sort(timezones.begin(), timezones.end());
    return timezones;
}

[[deprecated("Use formatTimestamp with FormatOptions(logFormattingOptions) instead.")]]
std::string generateLogEntryTimestampString(
    std::chrono::system_clock::time_point tp,
    LogFormattingOptions options
) {
    return formatTimestamp(tp, FormatOptions(options));
}

[[deprecated("Use formatTimestamp with FormatOptions(logFormattingOptions) instead.")]]
std::string generateLogEntryTimestampString(
    std::chrono::system_clock::time_point tp,
    const LogFormattingOptions& options
) {
    return formatTimestamp(tp, FormatOptions(options));
}

ParseResult parseIso8601WithOffset(std::string_view timestamp_str, const ParseOptions& opts) {
    std::chrono::system_clock::time_point tp;
    std::string s(timestamp_str); // Create a copy for std::istringstream

    // Define format strings that std::chrono::parse can handle for ISO8601,
    // including optional fractional seconds and 'Z' or +/- offset.
    // std::chrono::parse implicitly handles 'Z' and +/-HHMM if not explicitly included in format.
    // For ISO8601, common formats are:
    // YYYY-MM-DDTHH:MM:SS
    // YYYY-MM-DDTHH:MM:SSZ
    // YYYY-MM-DDTHH:MM:SS+HH:MM
    // YYYY-MM-DD HH:MM:SS (space instead of T)

    std::array<const char*, 8> formats_to_try = {
        "%Y-%m-%dT%H:%M:%S",            // Basic ISO 8601
        "%Y-%m-%dT%H:%M:%S%Ez",         // ISO 8601 with Z or offset
        "%Y-%m-%dT%H:%M:%S%F",          // ISO 8601 with fractional seconds
        "%Y-%m-%dT%H:%M:%S%F%Ez",       // ISO 8601 with fractional seconds and Z or offset
        "%Y-%m-%d %H:%M:%S",            // Common variant with space
        "%Y-%m-%d %H:%M:%S%Ez",         // Common variant with space and Z or offset
        "%Y-%m-%d %H:%M:%S%F",          // Common variant with space and fractional seconds
        "%Y-%m-%d %H:%M:%S%F%Ez"        // Common variant with space, fractional seconds and Z or offset
    };

    bool parsed = false;
    std::string_view unparsed_suffix; // To capture remaining unparsed string

    for (const auto* fmt_str : formats_to_try) {
        std::istringstream in(s);
        // Using std::locale::classic() for consistent parsing behavior across locales
        in.imbue(std::locale::classic());

        in >> std::chrono::parse(fmt_str, tp);

        if (!in.fail()) {
            // Check for remaining characters if strict mode is enabled
            if (opts.strict) {
                // Get remaining characters
                std::string remaining_chars;
                in >> remaining_chars;
                if (remaining_chars.empty()) { // Successfully parsed the entire string
                    parsed = true;
                    break;
                }
            } else { // Not strict, accept partial match
                parsed = true;
                break;
            }
        }
    }

    if (!parsed) {
        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
            "Timestamp string '" + s + "' does not match any known ISO8601 format."));
    }
    
    return tp;
}

ParseResult parseTimestamp(
    std::string_view timestamp_str,
    ::LogTimestampFormat format_type,
    const ParseOptions& opts
) {
    std::string s(timestamp_str); // Convert to string for parsing and error messages

    switch (format_type) {
        case ::LogTimestampFormat::ISO8601:
            return parseIso8601WithOffset(timestamp_str, opts);

        case ::LogTimestampFormat::UnixMillis: {
            try {
                size_t pos = 0;
                long long ms = std::stoll(s, &pos);
                if (pos != s.length()) {
                    if (opts.strict) {
                        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                            "Extra characters found after Unix Milliseconds timestamp: '" + s.substr(pos) + "'"));
                    }
                }
                return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
            } catch (const std::invalid_argument&) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Invalid Unix Milliseconds timestamp format: '" + s + "'"));
            } catch (const std::out_of_range&) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::OutOfRange,
                    "Unix Milliseconds timestamp value out of range: '" + s + "'"));
            }
        }
        case ::LogTimestampFormat::UnixSeconds: {
            try {
                size_t pos = 0;
                long long sec = std::stoll(s, &pos);
                if (pos != s.length()) {
                    if (opts.strict) {
                        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                            "Extra characters found after Unix Seconds timestamp: '" + s.substr(pos) + "'"));
                    }
                }
                return std::chrono::system_clock::time_point(std::chrono::seconds(sec));
            } catch (const std::invalid_argument&) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Invalid Unix Seconds timestamp format: '" + s + "'"));
            } catch (const std::out_of_range&) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::OutOfRange,
                    "Unix Seconds timestamp value out of range: '" + s + "'"));
            }
        }
        case ::LogTimestampFormat::UnixNanos: {
            try {
                size_t pos = 0;
                long long ns = std::stoll(s, &pos);
                if (pos != s.length()) {
                    if (opts.strict) {
                        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                            "Extra characters found after Unix Nanoseconds timestamp: '" + s.substr(pos) + "'"));
                    }
                }
                return std::chrono::system_clock::time_point(std::chrono::nanoseconds(ns));
            } catch (const std::invalid_argument&) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Invalid Unix Nanoseconds timestamp format: '" + s + "'"));
            } catch (const std::out_of_range&) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::OutOfRange,
                    "Unix Nanoseconds timestamp value out of range: '" + s + "'"));
            }
        }
        case ::LogTimestampFormat::RFC1123: {
            std::chrono::system_clock::time_point tp;
            std::istringstream in(s);
            std::string format_str = "%a, %d %b %Y %H:%M:%S %Z"; // Example: "Tue, 03 Jun 2008 11:05:30 GMT"
            
            // Use provided locale hint if available, otherwise classic
            if (opts.localeName) {
                try {
                    in.imbue(std::locale(*opts.localeName));
                } catch (const std::runtime_error& e) {
                    return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                        "Failed to imbue locale '" + *opts.localeName + "': " + e.what()));
                }
            } else {
                in.imbue(std::locale::classic());
            }

            in >> std::chrono::parse(format_str, tp);

            if (in.fail()) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Timestamp string '" + s + "' does not match RFC1123 format."));
            }
            if (opts.strict && !in.eof()) {
                std::string remainder;
                in >> remainder;
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Extra characters found after RFC1123 timestamp: '" + remainder + "'"));
            }
            return tp;
        }
        case ::LogTimestampFormat::CommonLogFormat: {
            std::chrono::system_clock::time_point tp;
            std::istringstream in(s);
            // Example: "10/Oct/2000:13:55:36 -0700"
            std::string format_str = "%d/%b/%Y:%H:%M:%S %z"; 
            
            // Use provided locale hint if available, otherwise classic
            if (opts.localeName) {
                try {
                    in.imbue(std::locale(*opts.localeName));
                } catch (const std::runtime_error& e) {
                    return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                        "Failed to imbue locale '" + *opts.localeName + "': " + e.what()));
                }
            } else {
                in.imbue(std::locale::classic());
            }

            in >> std::chrono::parse(format_str, tp);

            if (in.fail()) {
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Timestamp string '" + s + "' does not match Common Log Format."));
            }
            if (opts.strict && !in.eof()) {
                std::string remainder;
                in >> remainder;
                return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                    "Extra characters found after Common Log Format timestamp: '" + remainder + "'"));
            }
            return tp;
        }
        case ::LogTimestampFormat::Default: // Default will be handled by auto-detection if no format specified
        default:
            return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                "Unsupported or Default LogTimestampFormat provided explicitly."));
    }
}
    
ParseResult parseTimestamp(
    std::string_view timestamp_str,
    std::string_view format_str,
    const ParseOptions& opts
) {
    std::chrono::system_clock::time_point tp;
    std::istringstream in{std::string(timestamp_str)};
    
    // Use provided locale hint if available, otherwise classic
    if (opts.localeName) {
        try {
            in.imbue(std::locale(*opts.localeName));
        } catch (const std::runtime_error& e) {
            return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
                "Failed to imbue locale '" + *opts.localeName + "': " + e.what()));
        }
    } else {
        in.imbue(std::locale::classic());
    }
    
    std::string format_string_copy{format_str}; // chrono::parse needs a non-const char*
    in >> std::chrono::parse(format_string_copy, tp);

    if (in.fail()) {
        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
            "Timestamp string '" + std::string(timestamp_str) + "' does not match custom format string '" + format_string_copy + "'."));
    }
    if (opts.strict && !in.eof()) {
        std::string remainder;
        in >> remainder;
        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
            "Extra characters found after parsing with custom format string: '" + remainder + "'"));
    }
    return tp;
}


// --- NEW: Auto-detection parsing function implementation ---
ParseResult parseTimestamp(
    std::string_view timestamp_str,
    const ParseOptions& opts
) {
    // Ordered list of formats to try for auto-detection
    // ISO8601 is generally the most common and robust, so it's first.
    // RFC1123 and CommonLogFormat are also widely used.
    // Unix timestamps are simple but distinct, so they follow.
    const std::vector<::LogTimestampFormat> formats_to_try = {
        ::LogTimestampFormat::ISO8601,
        ::LogTimestampFormat::RFC1123,
        ::LogTimestampFormat::CommonLogFormat,
        ::LogTimestampFormat::UnixMillis,
        ::LogTimestampFormat::UnixSeconds,
        ::LogTimestampFormat::UnixNanos
    };

    std::string_view trimmed_str = timestamp_str;
    // Trim leading/trailing whitespace which can interfere with parsing
    size_t first = trimmed_str.find_first_not_of(" \t\n\r");
    if (std::string_view::npos == first) {
        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat, "Timestamp string is empty or only whitespace."));
    }
    size_t last = trimmed_str.find_last_not_of(" \t\n\r");
    trimmed_str = trimmed_str.substr(first, (last - first + 1));

    if (trimmed_str.empty()) {
        return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat, "Timestamp string is empty after trimming."));
    }


    for (const auto format_type : formats_to_try) {
        // Create a temporary ParseOptions to override strictness for initial auto-detection attempts.
        // If we are auto-detecting, we might want to be more lenient initially,
        // then apply strictness if a format seems plausible.
        // However, the ParseOptions already has 'strict', so we pass it through.
        ParseResult result = parseTimestamp(trimmed_str, format_type, opts);
        if (result.has_value()) {
            return result;
        }
    }

    return std::unexpected(ParseErrorInfo::fromCode(ParseErrorType::InvalidFormat,
        "Timestamp string '" + std::string(timestamp_str) + "' could not be auto-detected by any common format."));
}

// --- NEW: Timezone and local time conversions implementation ---
std::chrono::zoned_time<std::chrono::system_clock::duration> toZonedTime(
    std::chrono::system_clock::time_point tp,
    const std::string& timezoneName
) {
    try {
        const auto& tz = std::chrono::locate_zone(timezoneName);
        return std::chrono::zoned_time<std::chrono::system_clock::duration>(tz, tp);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Invalid IANA timezone name for toZonedTime: " + timezoneName + " - " + e.what());
    }
}

std::chrono::local_time<std::chrono::system_clock::duration> toLocal(
    std::chrono::system_clock::time_point tp
) {
    try {
        const auto& tz = std::chrono::current_zone();
        return tz->to_local(tp);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("Error converting to local time: " + std::string(e.what()));
    }
}

std::chrono::system_clock::time_point toUtc(
    std::chrono::local_time<std::chrono::system_clock::duration> lt,
    const std::string& timezoneName
) {
    try {
        const auto& tz = std::chrono::locate_zone(timezoneName);
        return tz->to_sys(lt);
    } catch (const std::runtime_error& e) {
        // Handle all chrono-related runtime errors here, including zone_error, ambiguous_local_time, nonexistent_local_time
        // The original error message from the chrono library will be preserved in e.what().
        throw std::runtime_error("Error during local time to UTC conversion for timezone '" + timezoneName + "': " + std::string(e.what()));
    }
}

// --- NEW: Custom format string validation ---
bool isValidChronoFormatString(std::string_view formatStr) {
    // Attempt to format a dummy time_point. If it throws std::format_error, the format is invalid.
    try {
        std::chrono::system_clock::time_point dummy_tp;
        std::chrono::zoned_time zt(std::chrono::locate_zone("UTC"), dummy_tp);
        
        // Try formatting with the given format string.
        // If the format string is syntactically incorrect for std::format, it will throw std::format_error.
        (void)std::vformat("{:" + std::string(formatStr) + "}", std::make_format_args(zt));
        
        return true;
    } catch (const std::format_error&) {
        return false;
    } catch (const std::runtime_error&) { // Catch potential errors from locate_zone if called with an invalid zone (e.g., from an invalid timezone specifier in formatStr)
        return false;
    } catch (...) { // Catch any other unexpected exceptions
        return false;
    }
}

// Helper for debugging/logging chrono durations (kept for now, but formatDuration is preferred)
std::string to_string(std::chrono::nanoseconds ns) {
    std::stringstream ss;
    ss << ns.count() << "ns";
    return ss.str();
}
} // namespace LogTimeUtil
