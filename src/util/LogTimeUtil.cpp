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
    if (duration == duration_values<decltype(duration)>::zero()) {
        return "0s";
    }

    std::stringstream ss;
    auto d = duration;
    if (d < duration_values<decltype(duration)>::zero()) {
        ss << "-";
        d = -d;
    }

    using years = std::chrono::duration<long long, std::ratio<31556952>>;
    using days = std::chrono::duration<long long, std::ratio<86400>>;
    using double_seconds = std::chrono::duration<double>;

    auto y = duration_cast<years>(d);
    if (y.count() > 0) {
        ss << y.count() << (compact ? "y" : " years");
        d -= duration_cast<decltype(d)>(y);
    }

    auto dy = duration_cast<days>(d);
    if (dy.count() > 0) {
        if (ss.tellp() > 0 && ss.str() != "-") ss << " ";
        ss << dy.count() << (compact ? "d" : " days");
        d -= duration_cast<decltype(d)>(dy);
    }

    auto h = duration_cast<hours>(d);
    if (h.count() > 0) {
        if (ss.tellp() > 0 && ss.str() != "-") ss << " ";
        ss << h.count() << (compact ? "h" : " hours");
        d -= duration_cast<decltype(d)>(h);
    }

    auto m = duration_cast<minutes>(d);
    if (m.count() > 0) {
        if (ss.tellp() > 0 && ss.str() != "-") ss << " ";
        ss << m.count() << (compact ? "m" : " minutes");
        d -= duration_cast<decltype(d)>(m);
    }

    auto s = duration_cast<seconds>(d);
    if (s.count() > 0) {
        if (ss.tellp() > 0 && ss.str() != "-") ss << " ";
        ss << s.count();
        d -= duration_cast<decltype(d)>(s);
        if (d.count() > 0) {
            auto frac_s = duration_cast<double_seconds>(d);
            std::string frac_str = std::to_string(frac_s.count());
            ss << frac_str.substr(frac_str.find('.'));
        }
        ss << (compact ? "s" : " seconds");
    } else if (d.count() > 0) {
        if (ss.tellp() > 0 && ss.str() != "-") ss << " ";
        auto ms = duration_cast<milliseconds>(d);
        if (ms.count() > 0) {
            ss << ms.count() << (compact ? "ms" : " milliseconds");
        } else {
            auto us = duration_cast<microseconds>(d);
            if (us.count() > 0) {
                ss << us.count() << (compact ? "us" : " microseconds");
            } else {
                ss << duration_cast<nanoseconds>(d).count() << (compact ? "ns" : " nanoseconds");
            }
        }
    } else if (ss.tellp() > 0 && y.count() == 0 && dy.count() == 0) {
        // If we printed hours or minutes, but seconds is zero.
        ss << " 0" << (compact ? "s" : " seconds");
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

    if (opts.format == ::LogTimestampFormat::UnixMillis) {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    }

    const std::chrono::time_zone* tz_ptr = nullptr;
    try {
        if (opts.timezone == ::LogTimezone::Local) {
            tz_ptr = std::chrono::current_zone();
        } else {
            tz_ptr = std::chrono::locate_zone("UTC");
        }
    } catch (const std::exception& e) { // Catching std::exception is safer
        std::cerr << "Warning: Timezone error during formatting: " << e.what() << ". Falling back to UTC." << std::endl;
        tz_ptr = std::chrono::locate_zone("UTC");
    }
    
    std::string format_str;
    if (opts.custom_format.has_value() && !opts.custom_format->empty()) {
        format_str = "{:" + *opts.custom_format + "}";
    } else {
        format_str = "{:%Y-%m-%dT%H:%M:%S}";
    }
    
    std::chrono::zoned_time zt(tz_ptr, tp);
    std::string formatted_str = std::vformat(format_str, std::make_format_args(zt));
    
    formatted_str += getFractionalString(tp, opts.precision);
    
    if (!opts.custom_format.has_value() || opts.custom_format->empty()) {
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
    
    if (opts.format == ::LogTimestampFormat::UnixMillis) {
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
    }

    const std::chrono::time_zone* tz_ptr;
    try {
        tz_ptr = std::chrono::locate_zone(timezone_name);
    } catch (const std::exception& e) { // Catching std::exception is safer
        throw std::runtime_error("Invalid IANA timezone name: " + timezone_name + " - " + e.what());
    }

    std::string format_str;
    if (opts.custom_format.has_value() && !opts.custom_format->empty()) {
        format_str = "{:" + *opts.custom_format + "}";
    } else {
        format_str = "{:%Y-%m-%dT%H:%M:%S}";
    }
    
    std::chrono::zoned_time zt(tz_ptr, tp);
    std::string formatted_str = std::vformat(format_str, std::make_format_args(zt));
    
    formatted_str += getFractionalString(tp, opts.precision);

    if (!opts.custom_format.has_value() || opts.custom_format->empty()) {
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
    std::string s(timestamp_str);

    std::istringstream in(s);
    in.imbue(std::locale::classic());

    std::array<const char*, 4> formats_to_try = {
        "%Y-%m-%dT%H:%M:%S%Z",
        "%Y-%m-%d %H:%M:%S%Z",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M:%S"
    };

    bool parsed = false;
    for (const auto* fmt : formats_to_try) {
        in.clear();
        in.seekg(0);
        in >> std::chrono::parse(fmt, tp);
        if (!in.fail()) {
            if (opts.strict) {
                // Check for remaining unparsed characters
                std::string remainder;
                in >> remainder;
                if (remainder.empty()) {
                    parsed = true;
                    break;
                }
            } else {
                parsed = true;
                break;
            }
        }
    }

    if (!parsed) {
        return std::unexpected(ParseError::InvalidFormat);
    }
    
    return tp;
}

ParseResult parseTimestamp(
    std::string_view timestamp_str,
    ::LogTimestampFormat format_type,
    const ParseOptions& opts
) {
    if (format_type == ::LogTimestampFormat::ISO8601) {
        return parseIso8601WithOffset(timestamp_str, opts);
    } else if (format_type == ::LogTimestampFormat::UnixMillis) {
        try {
            std::string s{timestamp_str};
            size_t pos = 0;
            long long ms = std::stoll(s, &pos);
            if (pos != s.length() && opts.strict) {
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
    in.imbue(std::locale::classic());
    
    std::string format_string_copy{format_str};
    in >> std::chrono::parse(format_string_copy, tp);

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

} // namespace LogTimeUtil
