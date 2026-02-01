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
#include <format> // For std::format in C++20
#include <ctime>  // For std::gmtime, std::localtime, std::strftime

namespace LogTimeUtil {

FormatOptions::FormatOptions(const ::LogFormattingOptions& model_opts)
    : format(model_opts.timestamp_format),
      precision(model_opts.precision),
      timezone(model_opts.timezone),
      custom_format(model_opts.custom_timestamp_format)
{}

template<class Rep, class Period>
std::string formatDuration(std::chrono::duration<Rep, Period> duration, bool compact) {
    using namespace std::chrono;
    if (duration == duration.zero()) {
        return "0s";
    }

    std::stringstream ss;
    auto d = duration;
    if (d < d.zero()) {
        ss << "-";
        d = -d;
    }

    // Using long long for duration_cast to avoid overflow for large values, then cast to original Rep
    long long total_years = duration_cast<std::chrono::duration<long long, std::ratio<31556952>>>(d).count();
    if (total_years > 0) {
        ss << total_years << (compact ? "y" : " years");
        d -= duration_cast<std::chrono::duration<Rep, Period>>(std::chrono::duration<long long, std::ratio<31556952>>(total_years));
    }

    long long total_days = duration_cast<std::chrono::duration<long long, std::ratio<86400>>>(d).count();
    if (total_days > 0) {
        if (!ss.str().empty()) ss << " "; 
        ss << total_days << (compact ? "d" : " days");
        d -= duration_cast<std::chrono::duration<Rep, Period>>(std::chrono::duration<long long, std::ratio<86400>>(total_days));
    }

    long long total_hours = duration_cast<hours>(d).count();
    if (total_hours > 0) {
        if (!ss.str().empty()) ss << " ";
        ss << total_hours << (compact ? "h" : " hours");
        d -= duration_cast<std::chrono::duration<Rep, Period>>(hours(total_hours));
    }

    long long total_minutes = duration_cast<minutes>(d).count();
    if (total_minutes > 0) {
        if (!ss.str().empty()) ss << " ";
        ss << total_minutes << (compact ? "m" : " minutes");
        d -= duration_cast<std::chrono::duration<Rep, Period>>(minutes(total_minutes));
    }

    long long total_seconds = duration_cast<seconds>(d).count();
    auto fractional_nanos_duration = d - seconds(total_seconds); 
    long long fractional_nanos_count = fractional_nanos_duration.count();

    bool printed_any_major_unit = (total_years > 0 || total_days > 0 || total_hours > 0 || total_minutes > 0);

    if (total_seconds > 0 || (!printed_any_major_unit && fractional_nanos_count == 0 && ss.str().empty())) { 
        if (printed_any_major_unit) ss << " ";
        ss << total_seconds;
        if (fractional_nanos_count > 0) {
            ss << ".";
            std::string nanos_str = std::to_string(fractional_nanos_count);
            nanos_str = std::string(9 - nanos_str.length(), '0') + nanos_str;
            nanos_str.erase(nanos_str.find_last_not_of('0') + 1, std::string::npos); // Trim trailing zeros
            ss << nanos_str;
        }
        ss << (compact ? "s" : " seconds");
    } else if (fractional_nanos_count > 0 && !printed_any_major_unit) { // Only fractional seconds if no major units and no whole seconds
        auto ms = duration_cast<milliseconds>(d);
        if (ms.count() > 0) {
            ss << ms.count() << (compact ? "ms" : " milliseconds");
        } else {
            auto us = duration_cast<microseconds>(d);
            if (us.count() > 0) {
                ss << us.count() << (compact ? "us" : " microseconds");
            } else {
                ss << fractional_nanos_count << (compact ? "ns" : " nanoseconds");
            }
        }
    }
    return ss.str();
}

template std::string formatDuration(std::chrono::nanoseconds duration, bool compact);
template std::string formatDuration(std::chrono::microseconds duration, bool compact);
template std::string formatDuration(std::chrono::milliseconds duration, bool compact);
template std::string formatDuration(std::chrono::seconds duration, bool compact);
template std::string formatDuration(std::chrono::minutes duration, bool compact);
template std::string formatDuration(std::chrono::hours duration, bool compact);

std::string getFractionalString(std::chrono::system_clock::time_point tp, ::LogPrecision precision) {
    if (precision == ::LogPrecision::Seconds) {
        return "";
    }
    
    auto since_epoch_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch());
    long long seconds_part_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::floor<std::chrono::seconds>(tp).time_since_epoch()).count();
    long long fractional_ns_val = since_epoch_ns.count() - seconds_part_ns;
    
    if (fractional_ns_val == 0) {
        return "";
    }

    std::stringstream ss;
    ss << ".";
    std::string nanos_str = std::to_string(fractional_ns_val);
    // Pad with leading zeros to 9 digits for nanoseconds
    if (nanos_str.length() < 9) {
        nanos_str.insert(0, 9 - nanos_str.length(), '0');
    }

    switch (precision) {
        case ::LogPrecision::Millis:      ss << nanos_str.substr(0, 3); break;
        case ::LogPrecision::Micros:     ss << nanos_str.substr(0, 6); break;
        case ::LogPrecision::Nanos:      ss << nanos_str.substr(0, 9); break;
        default: return ""; 
    }
    return ss.str();
}

std::string formatTimestamp(std::chrono::system_clock::time_point tp, const FormatOptions& opts) {
    if (tp.time_since_epoch().count() == 0) return "";

    if (opts.format == ::LogTimestampFormat::UnixMillis) {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    }

    const std::chrono::time_zone* tz_ptr = nullptr;
    try {
        const std::chrono::time_zone& current_zone_ref = std::chrono::current_zone();
        const std::chrono::time_zone& utc_zone_ref = std::chrono::locate_zone("UTC");
        tz_ptr = (opts.timezone == ::LogTimezone::Local) ? &current_zone_ref : &utc_zone_ref;
    } catch (const std::exception& e) { 
        std::cerr << "Warning: Timezone error during formatting: " << e.what() << ". Falling back to UTC." << std::endl;
        tz_ptr = &std::chrono::locate_zone("UTC");
    }
    
    std::string formatted_str;
    if (opts.custom_format.has_value()) {
        std::time_t tt = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf;
#if defined(_WIN32) || defined(_WIN64)
        if (opts.timezone == ::LogTimezone::UTC) {
            gmtime_s(&tm_buf, &tt);
        } else {
            localtime_s(&tm_buf, &tt);
        }
#else
        if (opts.timezone == ::LogTimezone::UTC) {
            gmtime_r(&tt, &tm_buf);
        } else {
            localtime_r(&tt, &tm_buf);
        }
#endif
        std::array<char, 128> buffer; 
        if (std::strftime(buffer.data(), buffer.size(), opts.custom_format->c_str(), &tm_buf) > 0) {
            formatted_str = buffer.data();
        } else {
            formatted_str = "Invalid custom format or buffer too small";
        }

    } else { // ISO8601 or RFC3339 default
        std::chrono::zoned_time zt_temp(tz_ptr, tp);
        formatted_str = std::format("{:%Y-%m-%dT%H:%M:%S}", zt_temp);
    }

    formatted_str += getFractionalString(tp, opts.precision);
    
    if (!opts.custom_format.has_value()) { 
        std::chrono::zoned_time zt_temp(tz_ptr, tp); 
        if (opts.timezone == ::LogTimezone::UTC) {
            formatted_str += "Z";
        } else {
             formatted_str += std::format("{:%Ez}", zt_temp);
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
    
    if (opts.format == ::LogTimestampFormat::UnixMillis) {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    }

    const std::chrono::time_zone* tz_ptr;
    try {
        tz_ptr = &std::chrono::locate_zone(timezone_name);
    } catch (const std::exception& e) { 
        throw std::runtime_error("Invalid IANA timezone name: " + timezone_name + " - " + e.what());
    }

    std::string formatted_str;
    if (opts.custom_format.has_value()) {
        std::time_t tt = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_buf;
        // The problem: strftime for a specific IANA timezone requires converting system_clock::time_point
        // to a local_time in that IANA timezone, then to std::tm. This is complex and problematic for strftime.
        // The simplest approach is to use std::format or document this limitation.
        // For custom formats in an IANA timezone, we'll try to use std::format with the custom pattern.
        // This relies on std::format handling the pattern, which it may not for all strftime patterns.
        // A more robust solution for full strftime custom patterns in arbitrary IANA timezones would be very complex.
        
        // As a best-effort, attempt to format via std::format. If the pattern is not compatible, it might fail.
        // This is a known limitation of std::format not supporting arbitrary strftime patterns.
        formatted_str = std::format("{:" + *opts.custom_format + "}", std::chrono::zoned_time(tz_ptr, tp));

    } else { // ISO8601 or RFC3339 default
        std::chrono::zoned_time zt_temp(tz_ptr, tp);
        formatted_str = std::format("{:%Y-%m-%dT%H:%M:%S}", zt_temp);
    }
    
    formatted_str += getFractionalString(tp, opts.precision);

    if (!opts.custom_format.has_value()) { 
        std::chrono::zoned_time zt_temp(tz_ptr, tp); 
        formatted_str += std::format("{:%Ez}", zt_temp);
    }
    
    return formatted_str;
}
    
std::vector<std::string> getAvailableTimezones() {
    std::vector<std::string> timezones;
    const auto& db = std::chrono::get_tzdb();
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
    std::chrono::sys_seconds tp_sec; 
    std::istringstream in{std::string(timestamp_str)};
    in.imbue(std::locale("C"));

    // Try parsing with 'T' separator, fractional seconds, and timezone offset/Z
    std::string format_full_T_offset = "%Y-%m-%dT%H:%M:%S%Z"; // %Z for Z or offset
    std::string format_full_T_no_offset = "%Y-%m-%dT%H:%M:%S";

    // Try parsing with ' ' separator, fractional seconds, and timezone offset/Z
    std::string format_full_space_offset = "%Y-%m-%d %H:%M:%S%Z";
    std::string format_full_space_no_offset = "%Y-%m-%d %H:%M:%S";

    std::array<std::string, 4> formats_to_try = {
        format_full_T_offset, format_full_T_no_offset,
        format_full_space_offset, format_full_space_no_offset
    };

    bool parsed_successfully = false;
    for (const auto& fmt_str : formats_to_try) {
        in.clear(); 
        in.seekg(0); 
        // std::chrono::from_stream is used internally by std::chrono::parse
        // parse into std::chrono::sys_seconds (seconds precision).
        std::chrono::from_stream(in, fmt_str.c_str(), tp_sec); // Pass c_str() here!

        if (!in.fail() && (in.eof() || !opts.strict)) { 
            parsed_successfully = true;
            break;
        }
    }

    if (!parsed_successfully) {
        return std::unexpected(ParseError::InvalidFormat);
    }
    
    std::chrono::system_clock::time_point final_tp = tp_sec;

    // Manually handle fractional seconds (if any remain) and apply to final_tp
    char next_char = in.peek();
    if (next_char == '.') {
        in.get(); // Consume '.'
        std::string fractional_str;
        while (std::isdigit(in.peek())) {
            fractional_str += static_cast<char>(in.get());
        }

        if (!fractional_str.empty()) {
            // Convert fractional string to nanoseconds.
            // Pad if less than 9 digits, truncate if more.
            long long frac_val = std::stoll(fractional_str);
            int digits = fractional_str.length();
            if (digits > 9) {
                frac_val = std::stoll(fractional_str.substr(0, 9));
            } else if (digits < 9) {
                frac_val *= static_cast<long long>(std::pow(10, 9 - digits));
            }
            final_tp += std::chrono::nanoseconds(frac_val);
        }
    }

    // Check for remaining characters for strictness
    if (opts.strict && !in.eof()) {
        return std::unexpected(ParseError::InvalidFormat);
    }

    return final_tp;
}

ParseResult parseTimestamp(
    std::string_view timestamp_str,
    ::LogTimestampFormat format_type,
    const ParseOptions& opts
) {
    if (format_type == ::LogTimestampFormat::ISO8601 || format_type == ::LogTimestampFormat::RFC3339) {
        return parseIso8601WithOffset(timestamp_str, opts);
    } else if (format_type == ::LogTimestampFormat::UnixMillis) {
        try {
            std::string s{timestamp_str};
            size_t pos = 0;
            long long ms = std::stoll(s, &pos);
            if (pos != s.length() && opts.strict) { // Check if entire string was consumed
                 return std::unexpected(ParseError::InvalidFormat);
            }
            return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
        } catch (const std::invalid_argument&) {
            return std::unexpected(ParseError::InvalidFormat);
        } catch (const std::out_of_range&) {
            return std::unexpected(ParseError::OutOfRange);
        }
    }
    return std::unexpected(ParseError::InvalidFormat);
}
    
ParseResult parseTimestamp(
    std::string_view timestamp_str,
    std::string_view format_str,
    const ParseOptions& opts
) {
    std::chrono::system_clock::time_point tp;
    std::istringstream in{std::string(timestamp_str)};
    in.imbue(std::locale("C"));
    
    std::string format_string_copy{format_str};
    in >> std::chrono::parse(format_string_copy.c_str(), tp); // Pass c_str() here!

    if (in.fail() || (opts.strict && !in.eof())) {
        return std::unexpected(ParseError::InvalidFormat);
    }
    return tp;
}
    
[[deprecated("Use an overload that specifies the format explicitly.")]]
ParseResult parseTimestamp(
    std::string_view timestamp_str
) {
    ParseOptions non_strict_opts { .strict = false };
    auto result = parseIso8601WithOffset(timestamp_str, non_strict_opts);
    if (result.has_value()) {
        return result;
    }

    return parseTimestamp(timestamp_str, ::LogTimestampFormat::UnixMillis, non_strict_opts);
}

std::string to_string(std::chrono::nanoseconds ns) {
    return formatDuration(ns, true);
}

} // namespace LogTimeUtil
