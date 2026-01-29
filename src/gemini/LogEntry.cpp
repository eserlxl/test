#include <gemini/LogEntry.h>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <typeinfo>

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#endif

const LogEntry::JsonOptions LogEntry::defaultJsonOptions; // Initializes with all defaults

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

uint64_t LogEntry::currentProcessId() {
#if defined(_WIN32) || defined(_WIN64)
    return static_cast<uint64_t>(GetCurrentProcessId());
#else
    return static_cast<uint64_t>(getpid());
#endif
}

std::string LogEntry::currentHostName() {
    char hostname[256];
#if defined(_WIN32) || defined(_WIN64)
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        return hostname;
    }
#else
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return hostname;
    }
#endif
    return "unknown";
}

LogEntry LogEntry::create(LogLevel level, std::string_view message, std::source_location loc) {
    LogEntry entry;
    entry.level = level;
    entry.message = message;
    entry.time_point = std::chrono::system_clock::now();
    entry.timestamp = entry.generatedTimestampString();
    entry.withSource(loc);
    entry.process_id = currentProcessId();
    entry.host_name = currentHostName();
    
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

LogEntry& LogEntry::withAttributes(std::initializer_list<std::pair<const std::string, LogValue>> attrs) {
    for (const auto& [key, value] : attrs) {
        attributes[key] = value;
    }
    return *this;
}

LogEntry& LogEntry::withAttributes(const std::map<std::string, LogValue>& attrs) {
    for (const auto& [key, value] : attrs) {
        attributes[key] = value;
    }
    return *this;
}

LogEntry& LogEntry::withProcessId(uint64_t pid) {
    process_id = pid;
    return *this;
}

LogEntry& LogEntry::withHost(std::string_view host) {
    host_name = host;
    return *this;
}

LogEntry& LogEntry::withApp(std::string_view app) {
    app_name = app;
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

LogEntry& LogEntry::withTraceContext(std::string_view tid, std::string_view sid) {
    trace_id = tid;
    span_id = sid;
    return *this;
}

LogEntry& LogEntry::removeAttribute(const std::string& key) {
    attributes.erase(key);
    return *this;
}

LogEntry& LogEntry::clearAttributes() {
    attributes.clear();
    return *this;
}

LogEntry& LogEntry::mergeAttributes(const LogEntry& other) {
    for (const auto& [key, value] : other.attributes) {
        attributes[key] = value;
    }
    return *this;
}

LogEntry LogEntry::clonedWithTag(std::string_view tag) const {
    LogEntry clone = *this;
    clone.withTag(tag);
    return clone;
}

bool LogEntry::parseTime() {
    std::istringstream ss(timestamp);
    std::tm tm = {};
    // Format: 2023-10-27 10:00:00.000
    char dash1, dash2, colon1, colon2;
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
        ss.ignore(); // skip '.'
        std::string frac_str;
        while (std::isdigit(ss.peek())) {
            frac_str += static_cast<char>(ss.get());
        }
        
        if (!frac_str.empty()) {
            try {
                double val = std::stod("0." + frac_str);
                auto nanos = std::chrono::nanoseconds(static_cast<long long>(val * 1'000'000'000));
                tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(nanos);
            } catch (...) {}
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

// Helper to skip whitespace
static std::string_view::iterator skipWhitespace(std::string_view::iterator it, std::string_view::iterator end) {
    while (it != end && std::isspace(static_cast<unsigned char>(*it))) {
        ++it;
    }
    return it;
}

// Helper to parse a JSON string (e.g., "value")
static std::expected<std::string, std::string> parseJsonString(std::string_view::iterator& it, std::string_view::iterator end) {
    it = skipWhitespace(it, end);
    if (it == end || *it != '"') {
        return std::unexpected("Expected '\"' to start a string");
    }
    ++it; // Skip opening quote

    std::string value;
    while (it != end && *it != '"') {
        if (*it == '\\') { // Handle escape sequences
            ++it;
            if (it == end) return std::unexpected("Unexpected end of string after escape character");
            switch (*it) {
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case 'u': // Unicode escape (e.g., \u0000) - simplified
                    if (std::distance(it, end) < 4) return std::unexpected("Incomplete unicode escape sequence");
                    value += '?'; 
                    it += 4;
                    break;
                default:
                    return std::unexpected(std::string("Unknown escape sequence: \\") + *it);
            }
        } else {
            value += *it;
        }
        ++it;
    }

    if (it == end) {
        return std::unexpected("Expected '\"' to end a string, but reached end of input");
    }
    ++it; // Skip closing quote
    return value;
}

// Helper to parse a JSON number (e.g., 123, 123.45)
static std::expected<LogValue, std::string> parseJsonNumber(std::string_view::iterator& it, std::string_view::iterator end) {
    it = skipWhitespace(it, end);
    auto start_num = it;
    if (it == end || (!std::isdigit(static_cast<unsigned char>(*it)) && *it != '-')) {
        return std::unexpected("Expected a number");
    }

    if (*it == '-') ++it;
    while (it != end && std::isdigit(static_cast<unsigned char>(*it))) {
        ++it;
    }

    bool is_double = false;
    if (it != end && *it == '.') {
        is_double = true;
        ++it;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it))) {
            ++it;
        }
    }

    if (it != end && (*it == 'e' || *it == 'E')) {
        is_double = true;
        ++it;
        if (it != end && (*it == '+' || *it == '-')) ++it;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it))) {
            ++it;
        }
    }
    
    std::string num_str(start_num, it);
    try {
        if (is_double) {
            return static_cast<LogValue>(std::stod(num_str));
        } else {
            try {
                return static_cast<LogValue>(std::stoll(num_str));
            } catch (const std::out_of_range&) {
                return static_cast<LogValue>(std::stoull(num_str));
            }
        }
    } catch (const std::exception& e) {
        return std::unexpected(std::string("Failed to parse number: ") + e.what());
    }
}

// Helper to parse a JSON boolean (true, false)
static std::expected<bool, std::string> parseJsonBoolean(std::string_view::iterator& it, std::string_view::iterator end) {
    it = skipWhitespace(it, end);
    if (std::distance(it, end) >= 4 && std::string_view(&*it, 4) == "true") {
        it += 4;
        return true;
    }
    if (std::distance(it, end) >= 5 && std::string_view(&*it, 5) == "false") {
        it += 5;
        return false;
    }
    return std::unexpected("Expected 'true' or 'false'");
}

// Helper to parse a LogValue (string, number, boolean, null)
static std::expected<LogValue, std::string> parseLogValue(std::string_view::iterator& it, std::string_view::iterator end) {
    it = skipWhitespace(it, end);
    if (it == end) {
        return std::unexpected("Expected a value, but reached end of input");
    }

    switch (*it) {
        case '"': return parseJsonString(it, end);
        case 't': // true
        case 'f': { // false
            auto bool_res = parseJsonBoolean(it, end);
            if (bool_res) return static_cast<LogValue>(*bool_res);
            return std::unexpected(bool_res.error());
        }
        case 'n': { // null
            if (std::distance(it, end) >= 4 && std::string_view(&*it, 4) == "null") {
                it += 4;
                return std::monostate{};
            }
            return std::unexpected("Expected 'null'");
        }
        case '-': // negative number
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return parseJsonNumber(it, end);
        default:
            return std::unexpected(std::string("Unexpected character for value: ") + *it);
    }
}


// Helper to parse a JSON array of strings (e.g., ["tag1", "tag2"])
static std::expected<std::set<std::string, std::less<>>, std::string> parseJsonStringArray(std::string_view::iterator& it, std::string_view::iterator end) {
    it = skipWhitespace(it, end);
    if (it == end || *it != '[') {
        return std::unexpected("Expected '[' to start array");
    }
    ++it; // Skip '['

    std::set<std::string, std::less<>> array_values;
    bool first_item = true;

    while (it != end) {
        it = skipWhitespace(it, end);
        if (*it == ']') {
            ++it; // Skip ']'
            return array_values;
        }

        if (!first_item) {
            if (*it != ',') {
                return std::unexpected("Expected ',' or ']' in array");
            }
            ++it; // Skip ','
        }

        auto str_res = parseJsonString(it, end);
        if (!str_res) return std::unexpected(str_res.error());
        array_values.insert(*str_res);
        first_item = false;
    }
    return std::unexpected("Expected ']' to end array, but reached end of input");
}

// Helper to parse a JSON object for attributes
static std::expected<std::map<std::string, LogValue>, std::string> parseJsonAttributes(std::string_view::iterator& it, std::string_view::iterator end) {
    it = skipWhitespace(it, end);
    if (it == end || *it != '{') {
        return std::unexpected("Expected '{' to start object");
    }
    ++it; // Skip '{'

    std::map<std::string, LogValue> attributes;
    bool first_item = true;

    while (it != end) {
        it = skipWhitespace(it, end);
        if (*it == '}') {
            ++it; // Skip '}'
            return attributes;
        }

        if (!first_item) {
            if (*it != ',') {
                return std::unexpected("Expected ',' or '}' in object");
            }
            ++it; // Skip ','
        }

        auto key_res = parseJsonString(it, end);
        if (!key_res) return std::unexpected("Failed to parse attribute key: " + key_res.error());

        it = skipWhitespace(it, end);
        if (it == end || *it != ':') {
            return std::unexpected("Expected ':' after attribute key");
        }
        ++it; // Skip ':'

        auto value_res = parseLogValue(it, end);
        if (!value_res) return std::unexpected("Failed to parse attribute value for key '" + *key_res + "': " + value_res.error());

        attributes[*key_res] = *value_res;
        first_item = false;
    }
    return std::unexpected("Expected '}' to end object, but reached end of input");
}


// The main fromJson method
LogEntry LogEntry::fromMap(const std::map<std::string, LogValue>& data) {
    LogEntry entry;
    // Auto-populate defaults
    entry.process_id = currentProcessId();
    entry.host_name = currentHostName();
    entry.time_point = std::chrono::system_clock::now();
    entry.timestamp = entry.generatedTimestampString();
    
    std::stringstream ss;
    ss << std::this_thread::get_id();
    entry.thread_id = ss.str();

    for (const auto& [key, value] : data) {
        if (key == "level" && std::holds_alternative<std::string>(value)) {
            entry.level = parseLevel(std::get<std::string>(value));
        } else if (key == "message" && std::holds_alternative<std::string>(value)) {
            entry.message = std::get<std::string>(value);
        } else if (key == "timestamp" && std::holds_alternative<std::string>(value)) {
            entry.timestamp = std::get<std::string>(value);
            entry.parseTime();
        } else if (key == "process_id") {
            if (std::holds_alternative<uint64_t>(value)) entry.process_id = std::get<uint64_t>(value);
            else if (std::holds_alternative<int64_t>(value)) entry.process_id = static_cast<uint64_t>(std::get<int64_t>(value));
        } else if (key == "host_name" && std::holds_alternative<std::string>(value)) {
            entry.host_name = std::get<std::string>(value);
        } else if (key == "app_name" && std::holds_alternative<std::string>(value)) {
            entry.app_name = std::get<std::string>(value);
        } else if (key == "thread_id" && std::holds_alternative<std::string>(value)) {
            entry.thread_id = std::get<std::string>(value);
        } else if (key == "trace_id" && std::holds_alternative<std::string>(value)) {
            entry.trace_id = std::get<std::string>(value);
        } else if (key == "span_id" && std::holds_alternative<std::string>(value)) {
            entry.span_id = std::get<std::string>(value);
        } else {
            // Treat everything else as an attribute
            entry.attributes[key] = value;
        }
    }
    return entry;
}

std::map<std::string, LogValue> LogEntry::toMap() const {
    std::map<std::string, LogValue> m;
    m["timestamp"] = timestamp.empty() ? generatedTimestampString() : timestamp;
    m["level"] = std::string(levelToString(level));
    m["message"] = message;
    if (process_id != 0) m["process_id"] = process_id;
    if (!host_name.empty()) m["host_name"] = host_name;
    if (!app_name.empty()) m["app_name"] = app_name;
    if (!thread_id.empty()) m["thread_id"] = thread_id;
    if (!trace_id.empty()) m["trace_id"] = trace_id;
    if (!span_id.empty()) m["span_id"] = span_id;
    
    for (const auto& [key, value] : attributes) {
        m[key] = value;
    }
    return m;
}

std::expected<LogEntry, std::string> LogEntry::fromJson(std::string_view json_str) {
    LogEntry entry;
    auto it = json_str.begin();
    auto end = json_str.end();

    it = skipWhitespace(it, end);
    if (it == end || *it != '{') {
        return std::unexpected("JSON must start with '{'");
    }
    ++it; // Skip '{'

    while (it != end) {
        it = skipWhitespace(it, end);
        if (*it == '}') {
            ++it; // Skip '}'
            break;
        }

        // Parse key
        auto key_res = parseJsonString(it, end);
        if (!key_res) return std::unexpected("Failed to parse key: " + key_res.error());
        std::string key = *key_res;

        it = skipWhitespace(it, end);
        if (it == end || *it != ':') {
            return std::unexpected("Expected ':' after key");
        }
        ++it; // Skip ':'

        // Parse value based on key
        if (key == "timestamp") {
            auto val_res = parseLogValue(it, end);
            if (!val_res) return std::unexpected("Failed to parse timestamp: " + val_res.error());
            
            if (std::holds_alternative<std::string>(*val_res)) {
                entry.timestamp = std::get<std::string>(*val_res);
                // ISO8601 detection
                if (entry.timestamp.find('T') != std::string::npos && entry.timestamp.find('Z') != std::string::npos) {
                    std::tm tm = {};
                    std::istringstream tss(entry.timestamp);
                    int y, m, d, h, min, s;
                    char t, dash1, dash2, c1, c2;
                    if (tss >> y >> dash1 >> m >> dash2 >> d >> t >> h >> c1 >> min >> c2 >> s) {
                        tm.tm_year = y - 1900;
                        tm.tm_mon = m - 1;
                        tm.tm_mday = d;
                        tm.tm_hour = h;
                        tm.tm_min = min;
                        tm.tm_sec = s;
                        
                        std::time_t tt;
                        #if defined(_WIN32) || defined(_WIN64)
                            tt = _mkgmtime(&tm);
                        #else
                            tt = timegm(&tm);
                        #endif
                        
                        if (tt != -1) {
                            auto tp = std::chrono::system_clock::from_time_t(tt);
                            if (tss.peek() == '.') {
                                tss.ignore();
                                std::string ms_str;
                                while (std::isdigit(tss.peek())) ms_str += static_cast<char>(tss.get());
                                if (!ms_str.empty()) {
                                    double val = std::stod("0." + ms_str);
                                    tp += std::chrono::duration_cast<std::chrono::system_clock::duration>(std::chrono::nanoseconds(static_cast<long long>(val * 1'000'000'000)));
                                }
                            }
                            entry.time_point = tp;
                        }
                    }
                } else {
                    entry.parseTime(); // Attempt to parse into time_point
                }
            } else if (std::holds_alternative<int64_t>(*val_res)) {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(std::get<int64_t>(*val_res)));
                entry.timestamp = entry.generatedTimestampString();
            } else if (std::holds_alternative<uint64_t>(*val_res)) {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(std::get<uint64_t>(*val_res)));
                entry.timestamp = entry.generatedTimestampString();
            }
        } else if (key == "level") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse level: " + val_res.error());
            entry.level = LogEntry::parseLevel(*val_res);
        } else if (key == "message") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse message: " + val_res.error());
            entry.message = *val_res;
        } else if (key == "process_id") { // Iteration 1: Handle process_id
            auto val_res = parseJsonNumber(it, end);
            if (!val_res) return std::unexpected("Failed to parse process_id: " + val_res.error());
            if (std::holds_alternative<uint64_t>(*val_res)) {
                entry.process_id = std::get<uint64_t>(*val_res);
            } else if (std::holds_alternative<int64_t>(*val_res)) {
                entry.process_id = static_cast<uint64_t>(std::get<int64_t>(*val_res));
            } else {
                return std::unexpected("process_id must be a number");
            }
        } else if (key == "host_name") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse host_name: " + val_res.error());
            entry.host_name = *val_res;
        } else if (key == "app_name") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse app_name: " + val_res.error());
            entry.app_name = *val_res;
        } else if (key == "source") {
            it = skipWhitespace(it, end);
            if (it == end || *it != '{') {
                return std::unexpected("Expected '{' to start source object");
            }
            ++it; // Skip '{'
            while (it != end) {
                it = skipWhitespace(it, end);
                if (*it == '}') {
                    ++it; // Skip '}'
                    break;
                }
                auto source_key_res = parseJsonString(it, end);
                if (!source_key_res) return std::unexpected("Failed to parse source key: " + source_key_res.error());
                std::string source_key = *source_key_res;
                it = skipWhitespace(it, end);
                if (it == end || *it != ':') return std::unexpected("Expected ':' after source key");
                ++it; // Skip ':'
                if (source_key == "file") {
                    auto file_res = parseJsonString(it, end);
                    if (!file_res) return std::unexpected("Failed to parse source file: " + file_res.error());
                    entry.source_file = *file_res;
                } else if (source_key == "function") {
                    auto func_res = parseJsonString(it, end);
                    if (!func_res) return std::unexpected("Failed to parse source function: " + func_res.error());
                    entry.source_function = *func_res;
                } else if (source_key == "line") {
                    auto line_res = parseJsonNumber(it, end);
                    if (!line_res || !std::holds_alternative<int64_t>(*line_res)) return std::unexpected("Failed to parse source line (expected integer)");
                    entry.source_line = static_cast<int>(std::get<int64_t>(*line_res));
                } else {
                    // Skip unknown source field value
                    auto dummy_val_res = parseLogValue(it, end); 
                    if (!dummy_val_res) return std::unexpected("Failed to skip unknown source value: " + dummy_val_res.error());
                }
                it = skipWhitespace(it, end);
                if (it != end && *it == ',') ++it;
                else if (it != end && *it != '}') {
                    it = skipWhitespace(it, end);
                    if (it != end && *it != '}') return std::unexpected("Expected ',' or '}' in source object");
                }
            }
        } else if (key == "thread_id") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse thread_id: " + val_res.error());
            entry.thread_id = *val_res;
        } else if (key == "trace_id") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse trace_id: " + val_res.error());
            entry.trace_id = *val_res;
        } else if (key == "span_id") {
            auto val_res = parseJsonString(it, end);
            if (!val_res) return std::unexpected("Failed to parse span_id: " + val_res.error());
            entry.span_id = *val_res;
        } else if (key == "tags") {
            auto val_res = parseJsonStringArray(it, end);
            if (!val_res) return std::unexpected("Failed to parse tags: " + val_res.error());
            entry.tags = *val_res;
        } else if (key == "attributes") {
            auto val_res = parseJsonAttributes(it, end);
            if (!val_res) return std::unexpected("Failed to parse attributes: " + val_res.error());
            entry.attributes = *val_res;
        } else {
            // Skip unknown key's value
            auto dummy_val_res = parseLogValue(it, end);
            if (!dummy_val_res) return std::unexpected("Failed to skip unknown value for key '" + key + "': " + dummy_val_res.error());
        }
        
        it = skipWhitespace(it, end);
        if (it != end && *it == ',') {
            ++it; // Skip comma and continue for next field
        } else if (it != end && *it == '}') {
            // End of object, will break loop
        } else if (it == end) {
            return std::unexpected("Unexpected end of JSON string while parsing object");
        } else {
            return std::unexpected(std::string("Expected ',' or '}' but found: ") + *it);
        }
    }

    return entry;
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

    // Timestamp
    if (options.timestamp_format == TimestampFormat::UnixMillis) {
        oss << indent << "\"timestamp\": " << std::chrono::duration_cast<std::chrono::milliseconds>(
               time_point.time_since_epoch()).count();
    } else {
        oss << indent << "\"timestamp\": \"";
        if (options.timestamp_format == TimestampFormat::ISO8601) {
            // Format to ISO 8601 (e.g., 2023-10-27T10:00:00.000Z)
            // Note: C++20 std::chrono::format is still a bit experimental or not universally available
            // Using a manual approach for robustness.
            std::time_t tt = std::chrono::system_clock::to_time_t(time_point);
            std::tm tm = {};
            #if defined(_WIN32) || defined(_WIN64)
                gmtime_s(&tm, &tt); // Use gmtime for UTC
            #else
                gmtime_r(&tt, &tm); // Use gmtime for UTC
            #endif
            
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                time_point.time_since_epoch()) % 1000;
            
            // Example: 2023-10-27T10:00:00.123Z
            oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << "." 
                << std::setfill('0') << std::setw(3) << ms.count() << "Z";

        } else { // Default or unknown, use generatedTimestampString
            oss << escapeJson(timestamp.empty() ? generatedTimestampString() : timestamp);
        }
        oss << "\"";
    }
    oss << "," << nl;

    oss << indent << "\"level\": \"" << levelToString(level) << "\"," << nl;
    oss << indent << "\"message\": \"" << escapeJson(message) << "\"";

    // Process ID
    if (process_id != 0) {
        oss << "," << nl << indent << "\"process_id\": " << process_id;
    }

    if (!host_name.empty()) {
        oss << "," << nl << indent << "\"host_name\": \"" << escapeJson(host_name) << "\"";
    }

    if (!app_name.empty()) {
        oss << "," << nl << indent << "\"app_name\": \"" << escapeJson(app_name) << "\"";
    }

    if (options.include_source && (!source_file.empty() || !source_function.empty() || source_line != 0)) {
        oss << "," << nl << indent << "\"source\": {" << nl;
        oss << indent << indent << "\"file\": \"" << escapeJson(source_file) << "\"," << nl;
        oss << indent << indent << "\"function\": \"" << escapeJson(source_function) << "\"," << nl;
        oss << indent << indent << "\"line\": " << source_line << nl;
        oss << indent << "}";
    }

    if (options.include_thread && !thread_id.empty()) {
        oss << "," << nl << indent << "\"thread_id\": \"" << escapeJson(thread_id) << "\"";
    }

    if (options.include_tracing) {
        if (!trace_id.empty()) {
            oss << "," << nl << indent << "\"trace_id\": \"" << escapeJson(trace_id) << "\"";
        }
        if (!span_id.empty()) {
            oss << "," << nl << indent << "\"span_id\": \"" << escapeJson(span_id) << "\"";
        }
    }

    if (!tags.empty()) {
        oss << "," << nl << indent << "\"tags\": [";
        bool first = true;
        for (const auto& tag : tags) {
            if (!first) oss << ", ";
            oss << "\"" << escapeJson(tag) << "\"";
            first = false;
        }
        oss << "]";
    }

    if (!attributes.empty()) {
        oss << "," << nl << indent << "\"attributes\": {" << nl;
        bool first = true;
        for (const auto& [key, value] : attributes) {
            if (!first) oss << "," << nl;
            oss << indent << indent << "\"" << escapeJson(key) << "\": ";
            
            std::visit([&oss](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    oss << "\"" << escapeJson(arg) << "\"";
                } else if constexpr (std::is_same_v<T, bool>) {
                    oss << (arg ? "true" : "false");
                } else if constexpr (std::is_same_v<T, std::monostate>) {
                    oss << "null";
                } else { // Numeric types
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

    // Tracing tie-breaker
    if (trace_id != other.trace_id) {
        return trace_id <=> other.trace_id;
    }
    if (span_id != other.span_id) {
        return span_id <=> other.span_id;
    }

    // Tie-breaker: message
    return message <=> other.message;
}

bool LogEntry::operator==(const LogEntry& other) const {
    return (*this <=> other) == 0;
}

bool LogEntry::isValid() const noexcept {
    return level != LogLevel::UNKNOWN && !message.empty();
}

int LogEntry::getSeverityValue() const {
    switch (level) {
        case LogLevel::DEBUG: return 7;
        case LogLevel::INFO: return 6;
        case LogLevel::WARNING: return 4;
        case LogLevel::ERROR: return 3;
        case LogLevel::CRITICAL: return 2;
        default: return 0; // Emergency/Unknown? treating UNKNOWN as 0 might be misleading but fits the return type.
                           // Actually, RFC 5424: 0 is Emergency. 
                           // Let's assume Unknown is not standard.
    }
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