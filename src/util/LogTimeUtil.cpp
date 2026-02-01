#include <util/LogTimeUtil.h>
#include <model/LogFormattingOptions.h> // For enums
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <cctype>
#include <locale> // Required for std::locale

namespace LogTimeUtil {

#if defined(_WIN32) || defined(_WIN64)
    inline time_t my_timegm(struct tm* tm) { return _mkgmtime(tm); }
#else
    inline time_t my_timegm(struct tm* tm) { return timegm(tm); }
#endif

    // Forward declare the helper function
    std::optional<std::chrono::system_clock::time_point> parseIso8601WithOffset(
        std::string_view timestamp_str
    );

    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        LogFormattingOptions::TimestampFormat format_type,
        const FormatOptions& opts
    ) {
        if (tp.time_since_epoch().count() == 0)
            return "";

        std::time_t tt = std::chrono::system_clock::to_time_t(tp);
        std::tm tm_struct = {};
        
        if (opts.timezone == LogFormattingOptions::Timezone::UTC) {
#if defined(_WIN32) || defined(_WIN64)
            gmtime_s(&tm_struct, &tt);
#else
            gmtime_r(&tt, &tm_struct);
#endif
        } else {
#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tm_struct, &tt);
#else
            localtime_r(&tt, &tm_struct);
#endif
        }

        std::ostringstream ss;
        if (format_type == LogFormattingOptions::TimestampFormat::Custom && opts.custom_format.has_value()) {
            char buffer[128];
            if (std::strftime(buffer, sizeof(buffer), opts.custom_format->c_str(), &tm_struct) > 0) {
                ss << buffer;
            }
        } else if (format_type == LogFormattingOptions::TimestampFormat::ISO8601 || format_type == LogFormattingOptions::TimestampFormat::RFC3339) {
            ss << std::put_time(&tm_struct, "%Y-%m-%dT%H:%M:%S");

            auto epoch = tp.time_since_epoch();
            if (opts.precision != LogFormattingOptions::Precision::Seconds) {
                ss << ".";
                long long fractional_part = 0;
                int width = 0;
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
            if (opts.timezone == LogFormattingOptions::Timezone::UTC) {
                ss << "Z";
            }
        } else if (format_type == LogFormattingOptions::TimestampFormat::UnixMillis) { // Assuming UnixMillis from another file. Changed to Unix
             ss << std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
        }
        return ss.str();
    }
    
    std::string formatTimestamp(
        std::chrono::system_clock::time_point tp,
        const FormatOptions& opts
    ) {
        return formatTimestamp(tp, opts.format, opts);
    }
    
    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        LogFormattingOptions options
    ) {
        FormatOptions opts;
        opts.format = options.timestamp_format;
        opts.precision = options.precision;
        opts.timezone = options.timezone;
        opts.custom_format = options.custom_timestamp_format;
        return formatTimestamp(tp, opts);
    }

    std::string generateLogEntryTimestampString(
        std::chrono::system_clock::time_point tp,
        const LogFormattingOptions& options
    ) {
        return generateLogEntryTimestampString(tp, options);
    }


    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        LogFormattingOptions::TimestampFormat format_type,
        bool strict
    ) {
        if (format_type == LogFormattingOptions::TimestampFormat::ISO8601 || format_type == LogFormattingOptions::TimestampFormat::RFC3339) {
            return parseIso8601WithOffset(timestamp_str);
        } else if (format_type == LogFormattingOptions::TimestampFormat::UnixMillis) {
            try {
                std::string s(timestamp_str);
                size_t pos;
                long long ms = std::stoll(s, &pos);
                if (pos == s.length()) {
                    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
                }
            } catch (...) {}
        }
        return std::nullopt;
    }

    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        LogFormattingOptions::TimestampFormat format_type
    ) {
        return parseTimestamp(timestamp_str, format_type, true);
    }

    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str,
        std::string_view format_str
    ) {
        std::chrono::system_clock::time_point tp;
        std::istringstream ss{std::string(timestamp_str)};
        ss.imbue(std::locale("C"));
        
        std::string format_string_copy(format_str);
        ss >> std::chrono::parse(format_string_copy, tp);

        if (ss.fail()) {
            return std::nullopt;
        }
        return tp;
    }
    
    std::optional<std::chrono::system_clock::time_point> parseTimestamp(
        std::string_view timestamp_str
    ) {
        return parseTimestamp(timestamp_str, LogFormattingOptions::TimestampFormat::ISO8601, true);
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

    std::string to_string(std::chrono::nanoseconds ns) {
        std::ostringstream oss;
        if (ns.count() == 0) {
            oss << "0ns";
        } else if (ns.count() % 1000000000 == 0) {
            oss << std::chrono::duration_cast<std::chrono::seconds>(ns).count() << "s";
        } else if (ns.count() % 1000000 == 0) {
            oss << std::chrono::duration_cast<std::chrono::milliseconds>(ns).count() << "ms";
        } else if (ns.count() % 1000 == 0) {
            oss << std::chrono::duration_cast<std::chrono::microseconds>(ns).count() << "us";
        } else {
            oss << ns.count() << "ns";
        }
        return oss.str();
    }

} // namespace LogTimeUtil
