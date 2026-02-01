#include <model/LogEntryTime.h>
#include <model/LogEntry.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>

// Helper to format a time_point into a string based on options
std::string LogEntry::formatTimestamp(
    std::chrono::system_clock::time_point tp,
    LogFormattingOptions::TimestampFormat format_type,
    const TimestampFormatOptions& opts
) {
    if (tp.time_since_epoch().count() == 0)
        return "";

    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    std::tm tm = {};
    
    if (opts.timezone == LogFormattingOptions::Timezone::UTC) {
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
    if (opts.custom_timestamp_format.has_value()) {
        char buffer[128]; // Sufficient buffer for common date/time formats
        std::strftime(buffer, sizeof(buffer), opts.custom_timestamp_format->c_str(), &tm);
        ss << buffer;
    } else if (format_type == LogFormattingOptions::TimestampFormat::ISO8601) {
        ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

        auto epoch = tp.time_since_epoch();
        if (opts.precision != LogFormattingOptions::Precision::Seconds)
        {
            ss << ".";
            long long fractional_part;
            int width;
            if (opts.precision == LogFormattingOptions::Precision::Millis) {
                fractional_part = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() % 1000;
                width = 3;
            } else if (opts.precision == LogFormattingOptions::Precision::Micros) {
                fractional_part = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count() % 1000000;
                width = 6;
            } else { // Nanos
                fractional_part = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch).count() % 1000000000;
                width = 9;
            }
            ss << std::setfill('0') << std::setw(width) << fractional_part;
        }
        ss << (opts.timezone == LogFormattingOptions::Timezone::UTC ? "Z" : "");
    } else { // Default format (YYYY-MM-DD HH:MM:SS.mmm)
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        if (opts.precision != LogFormattingOptions::Precision::Seconds) {
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
    opts.custom_timestamp_format = options.custom_timestamp_format;
    return LogEntry::formatTimestamp(time_point, options.timestamp_format, opts);
}

// Helper to parse ISO8601 timestamps
std::optional<std::chrono::system_clock::time_point> parseISO8601(std::string_view timestamp_str) {
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
std::optional<std::chrono::system_clock::time_point> parseDefaultFormat(std::string_view timestamp_str) {
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
    LogFormattingOptions::TimestampFormat format_type
) {
    if (format_type == LogFormattingOptions::TimestampFormat::ISO8601) {
        return parseISO8601(timestamp_str);
    } else if (format_type == LogFormattingOptions::TimestampFormat::Default) {
        return parseDefaultFormat(timestamp_str);
    } else if (format_type == LogFormattingOptions::TimestampFormat::UnixMillis) {
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
