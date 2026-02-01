#include <util/LogTimeUtil.h>
#include <model/LogEntry.h>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <cctype>

namespace LogTimeUtil {

#if defined(_WIN32) || defined(_WIN64)
    inline time_t my_timegm(struct tm* tm) { return _mkgmtime(tm); }
#else
    inline time_t my_timegm(struct tm* tm) { return timegm(tm); }
#endif

    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        LogTimestampFormat format_type,
        const FormatOptions& opts
    ) {
        if (tp.time_since_epoch().count() == 0)
            return "";

        std::time_t tt = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = {};
        
        if (opts.timezone == LogTimezone::UTC) {
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
        if (opts.custom_format.has_value()) {
            char buffer[128];
            if (std::strftime(buffer, sizeof(buffer), opts.custom_format->c_str(), &tm) > 0) {
                ss << buffer;
            }
        } else if (format_type == LogTimestampFormat::ISO8601) {
            ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");

            auto epoch = tp.time_since_epoch();
            if (opts.precision != LogPrecision::Seconds) {
                ss << ".";
                long long fractional_part = 0;
                int width = 0;
                if (opts.precision == LogPrecision::Millis) {
                    fractional_part = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count() % 1000;
                    width = 3;
                } else if (opts.precision == LogPrecision::Micros) {
                    fractional_part = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count() % 1000000;
                    width = 6;
                } else { // Nanos
                    fractional_part = std::chrono::duration_cast<std::chrono::nanoseconds>(epoch).count() % 1000000000;
                    width = 9;
                }
                ss << std::setfill('0') << std::setw(width) << fractional_part;
            }
            if (opts.timezone == LogTimezone::UTC) {
                ss << "Z";
            }
        } else if (format_type == LogTimestampFormat::UnixMillis) {
            ss << std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
        } else { // Default format (YYYY-MM-DD HH:MM:SS.mmm)
            ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
            if (opts.precision != LogPrecision::Seconds) {
                 auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              tp.time_since_epoch()) %
                          1000;
                ss << "." << std::setfill('0') << std::setw(3) << ms.count();
            }
        }
        return ss.str();
    }

    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        const LogFormattingOptions& options
    ) {
        FormatOptions opts;
        opts.precision = options.precision;
        opts.timezone = options.timezone;
        opts.custom_format = options.custom_timestamp_format;
        return formatTimestamp(tp, options.timestamp_format, opts);
    }

    std::optional<std::chrono::system_clock::time_point> parseIso8601WithOffset(
        std::string_view timestamp_str
    ) {
        std::tm tm = {};
        std::istringstream tss{std::string(timestamp_str)};

        int y, m, d, H, M, S;
        char sep1, sep2, T_char, colon1, colon2;
        if (!(tss >> y >> sep1 >> m >> sep2 >> d >> T_char >> H >> colon1 >> M >> colon2 >> S) ||
            sep1 != '-' || sep2 != '-' || (T_char != 'T' && T_char != ' ') || colon1 != ':' || colon2 != ':') {
            return std::nullopt;
        }

        tm.tm_year = y - 1900;
        tm.tm_mon = m - 1;
        tm.tm_mday = d;
        tm.tm_hour = H;
        tm.tm_min = M;
        tm.tm_sec = S;
        tm.tm_isdst = -1;

        long long nanos = 0;
        if (tss.peek() == '.') {
            tss.ignore();
            std::string frac_str;
            while (tss.good() && std::isdigit(tss.peek())) {
                frac_str += static_cast<char>(tss.get());
            }
            if (!frac_str.empty()) {
                try {
                    std::string full_frac = frac_str;
                    if (full_frac.size() > 9) full_frac = full_frac.substr(0, 9);
                    else full_frac.resize(9, '0');
                    nanos = std::stoll(full_frac);
                } catch (...) {}
            }
        }

        std::time_t tt = -1;
        char tz_char = static_cast<char>(tss.peek());
        if (tz_char == 'Z') {
            tss.ignore();
            tt = my_timegm(&tm);
        } else if (tz_char == '+' || tz_char == '-') {
            char sign = static_cast<char>(tss.get());
            int oh = 0, om = 0;
            bool parsed_offset = false;
            
            // Try reading HH:MM or HHMM
            std::string offset_remaining;
            tss >> offset_remaining;
            if (!offset_remaining.empty()) {
                size_t colon_pos = offset_remaining.find(':');
                try {
                    if (colon_pos != std::string::npos) {
                        oh = std::stoi(offset_remaining.substr(0, colon_pos));
                        om = std::stoi(offset_remaining.substr(colon_pos + 1));
                        parsed_offset = true;
                    } else if (offset_remaining.length() >= 2) {
                        oh = std::stoi(offset_remaining.substr(0, 2));
                        if (offset_remaining.length() >= 4) {
                            om = std::stoi(offset_remaining.substr(2, 2));
                        }
                        parsed_offset = true;
                    }
                } catch (...) {}
            }
            
            if (parsed_offset) {
                tt = my_timegm(&tm);
                if (tt != -1) {
                    int offset_seconds = oh * 3600 + om * 60;
                    if (sign == '+') tt -= offset_seconds;
                    else tt += offset_seconds;
                }
            } else {
                tt = std::mktime(&tm);
            }
        } else {
            tt = std::mktime(&tm);
        }

        if (tt == -1) return std::nullopt;
        return std::chrono::system_clock::from_time_t(tt) + std::chrono::nanoseconds(nanos);
    }

    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        LogTimestampFormat format_type
    ) {
        if (format_type == LogTimestampFormat::ISO8601) {
            return parseIso8601WithOffset(timestamp_str);
        } else if (format_type == LogTimestampFormat::UnixMillis) {
            try {
                std::string s(timestamp_str);
                size_t pos;
                long long ms = std::stoll(s, &pos);
                if (pos == s.length()) {
                    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
                }
            } catch (...) {}
        }
        return std::nullopt; // Explicitly return nullopt on failure, no fallback
    }

    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str
    ) {
        if (auto tp = parseIso8601WithOffset(timestamp_str)) return tp;
        
        // Try default format YYYY-MM-DD HH:MM:SS.mmm
        std::tm tm = {};
        std::istringstream ss{std::string(timestamp_str)};
        int y, mon, d, H, M, S;
        char d1, d2, c1, c2;
        if (ss >> y >> d1 >> mon >> d2 >> d >> H >> c1 >> M >> c2 >> S) {
            tm.tm_year = y - 1900;
            tm.tm_mon = mon - 1;
            tm.tm_mday = d;
            tm.tm_hour = H;
            tm.tm_min = M;
            tm.tm_sec = S;
            tm.tm_isdst = -1;
            std::time_t tt = std::mktime(&tm);
            if (tt != -1) {
                auto tp = std::chrono::system_clock::from_time_t(tt);
                if (ss.peek() == '.') {
                    ss.ignore();
                    std::string frac_str;
                    while (ss.good() && std::isdigit(ss.peek())) frac_str += static_cast<char>(ss.get());
                    if (!frac_str.empty()) {
                        frac_str.resize(9, '0');
                        tp += std::chrono::nanoseconds(std::stoll(frac_str));
                    }
                }
                return tp;
            }
        }

        // Try Unix Millis
        try {
            std::string s(timestamp_str);
            size_t pos;
            long long ms = std::stoll(s, &pos);
            if (pos == s.length()) {
                return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
            }
        } catch (...) {}

        return std::nullopt;
    }

    std::chrono::system_clock::duration timeDifference(
        std::chrono::system_clock::time_point tp1,
        std::chrono::system_clock::time_point tp2
    ) {
        return tp1 - tp2;
    }

    std::chrono::system_clock::duration timeDifference(
        const LogEntry& entry1,
        const LogEntry& entry2
    ) {
        return entry1.time_point - entry2.time_point;
    }

} // namespace LogTimeUtil
