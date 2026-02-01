#include <model/LogEntrySerialization.h>
#include <model/LogEntry.h> // For LogEntry, LogValue, LogObject, LogList
#include <model/LogFormattingOptions.h> // For LogFormattingOptions
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <typeinfo>
#include <limits>
#include <cstdio>
#include <stdexcept> // For std::stoll errors

// --- JSON Parsing Helpers ---

// Helper to skip whitespace
static std::string_view::iterator skipWhitespace(std::string_view::iterator &it, std::string_view::iterator end)
{
    while (it != end && std::isspace(static_cast<unsigned char>(*it)))
    {
        ++it;
    }
    return it;
}

// Helper to parse a JSON string (e.g., "value")
static std::expected<std::string, std::string> parseJsonString(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '"')
    {
        return std::unexpected("Expected '\"' to start a string");
    }
    ++it; // Skip opening quote

    std::string value;
    while (it != end && *it != '"')
    {
        if (*it == '\\')
        { // Handle escape sequences
            ++it;
            if (it == end)
                return std::unexpected("Unexpected end of string after escape character");
            switch (*it)
            {
            case '"':
                value += '"';
                break;
            case '\\':
                value += '\\';
                break;
            case '/':
                value += '/';
                break;
            case 'b':
                value += '\b';
                break;
            case 'f':
                value += '\f';
                break;
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            case 'u': // Unicode escape (e.g., \u0000) - simplified
                if (std::distance(it, end) < 4)
                    return std::unexpected("Incomplete unicode escape sequence");
                value += '?'; // Placeholder for actual unicode processing
                it += 4;
                break;
            default:
                return std::unexpected(std::string("Unknown escape sequence: \\") + *it);
            }
        }
        else
        {
            value += *it;
        }
        ++it;
    }

    if (it == end)
    {
        return std::unexpected("Expected '\"' to end a string, but reached end of input");
    }
    ++it; // Skip closing quote
    return value;
}

// Helper to parse a JSON number (e.g., 123, 123.45)
static std::expected<LogValue, std::string> parseJsonNumber(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    auto start_num = it;
    if (it == end || (!std::isdigit(static_cast<unsigned char>(*it)) && *it != '-'))
    {
        return std::unexpected("Expected a number");
    }

    if (*it == '-')
        ++it;
    while (it != end && std::isdigit(static_cast<unsigned char>(*it)))
    {
        ++it;
    }

    bool is_double = false;
    if (it != end && *it == '.')
    {
        is_double = true;
        ++it;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it)))
        {
            ++it;
        }
    }

    if (it != end && (*it == 'e' || *it == 'E'))
    {
        is_double = true;
        ++it;
        if (it != end && (*it == '+' || *it == '-'))
            ++it;
        while (it != end && std::isdigit(static_cast<unsigned char>(*it)))
        {
            ++it;
        }
    }

    std::string num_str(start_num, it);
    try
    {
        if (is_double)
        {
            return static_cast<LogValue>(std::stod(num_str));
        }
        else
        {
            try
            {
                return static_cast<LogValue>(std::stoll(num_str));
            }
            catch (const std::out_of_range &)
            {
                return static_cast<LogValue>(std::stoull(num_str));
            }
        }
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string("Failed to parse number: ") + e.what());
    }
}

// Helper to parse a JSON boolean (true, false)
static std::expected<bool, std::string> parseJsonBoolean(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (std::distance(it, end) >= 4 && std::string_view(&*it, 4) == "true")
    {
        it += 4;
        return true;
    }
    if (std::distance(it, end) >= 5 && std::string_view(&*it, 5) == "false")
    {
        it += 5;
        return false;
    }
    return std::unexpected("Expected 'true' or 'false'");
}

// Forward declarations for recursive parsing
static std::expected<LogValue, std::string> parseLogValue(std::string_view::iterator &it, std::string_view::iterator end);
static std::expected<LogObject, std::string> parseJsonObject(std::string_view::iterator &it, std::string_view::iterator end);
static std::expected<LogList, std::string> parseJsonArray(std::string_view::iterator &it, std::string_view::iterator end);

// Helper to parse a LogValue (string, number, boolean, null, object, array)
static std::expected<LogValue, std::string> parseLogValue(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end)
    {
        return std::unexpected("Expected a value, but reached end of input");
    }

    switch (*it)
    {
    case '"':
        return parseJsonString(it, end);
    case '{':
    {
        auto obj_res = parseJsonObject(it, end);
        if (!obj_res)
            return std::unexpected(obj_res.error());
        return LogValue(std::move(*obj_res));
    }
    case '[':
    {
        auto list_res = parseJsonArray(it, end);
        if (!list_res)
            return std::unexpected(list_res.error());
        return LogValue(std::move(*list_res));
    }
    case 't': // true
    case 'f':
    { // false
        auto bool_res = parseJsonBoolean(it, end);
        if (bool_res)
            return static_cast<LogValue>(*bool_res);
        return std::unexpected(bool_res.error());
    }
    case 'n':
    { // null
        if (std::distance(it, end) >= 4 && std::string_view(&*it, 4) == "null")
        {
            it += 4;
            return std::monostate{};
        }
        return std::unexpected("Expected 'null'");
    }
    case '-': // negative number
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return parseJsonNumber(it, end);
    default:
        return std::unexpected(std::string("Unexpected character for value: ") + *it);
    }
}

// Helper to parse a JSON array (LogList)
static std::expected<LogList, std::string> parseJsonArray(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '[')
    {
        return std::unexpected("Expected '[' to start array");
    }
    ++it; // Skip '['

    LogList list;
    bool first_item = true;

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == ']')
        {
            ++it; // Skip ']'
            return list;
        }

        if (!first_item)
        {
            if (*it != ',')
            {
                return std::unexpected("Expected ',' or ']' in array");
            }
            ++it; // Skip ','
        }

        auto val_res = parseLogValue(it, end);
        if (!val_res)
            return std::unexpected(val_res.error());
        list.push_back(std::move(*val_res));
        first_item = false;
    }
    return std::unexpected("Expected ']' to end array, but reached end of input");
}

// Helper to parse a JSON object (LogObject)
static std::expected<LogObject, std::string> parseJsonObject(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '{')
    {
        return std::unexpected("Expected '{' to start object");
    }
    ++it; // Skip '{'

    LogObject obj;
    bool first_item = true;

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == '}')
        {
            ++it; // Skip '}'
            return obj;
        }

        if (!first_item)
        {
            if (*it != ',')
            {
                return std::unexpected("Expected ',' or '}' in object");
            }
            ++it; // Skip ','
        }

        auto key_res = parseJsonString(it, end);
        if (!key_res)
            return std::unexpected("Failed to parse key: " + key_res.error());

        it = skipWhitespace(it, end);
        if (it == end || *it != ':')
        {
            return std::unexpected("Expected ':' after key");
        }
        ++it; // Skip ':'

        auto value_res = parseLogValue(it, end);
        if (!value_res)
            return std::unexpected("Failed to parse value for key '" + *key_res + "': " + value_res.error());

        obj[*key_res] = std::move(*value_res);
        first_item = false;
    }
    return std::unexpected("Expected '}' to end object, but reached end of input");
}

// Helper to parse a JSON array of strings (e.g., ["tag1", "tag2"])
static std::expected<std::set<std::string, std::less<>>, std::string> parseJsonStringArray(std::string_view::iterator &it, std::string_view::iterator end)
{
    it = skipWhitespace(it, end);
    if (it == end || *it != '[')
    {
        return std::unexpected("Expected '[' to start array");
    }
    ++it; // Skip '['

    std::set<std::string, std::less<>> array_values;
    bool first_item = true;

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == ']')
        {
            ++it; // Skip ']'
            return array_values;
        }

        if (!first_item)
        {
            if (*it != ',')
            {
                return std::unexpected("Expected ',' or ']' in array");
            }
            ++it; // Skip ','
        }

        auto str_res = parseJsonString(it, end);
        if (!str_res)
            return std::unexpected(str_res.error());
        array_values.insert(*str_res);
        first_item = false;
    }
    return std::unexpected("Expected ']' to end array, but reached end of input");
}

// --- LogEntry Serialisation/Deserialization ---

LogEntry LogEntry::fromMap(const std::map<std::string, LogValue> &data)
{
    LogEntry entry;

    for (const auto &[key, value] : data)
    {
        if (key == "level")
        {
            if (auto s_ptr = value.asString()) {
                entry.level = LogEntry::parseLevel(**s_ptr);
            }
        }
        else if (key == "message")
        {
            if (auto s_ptr = value.asString()) {
                entry.message = **s_ptr;
            }
        }
        else if (key == "timestamp")
        {
            if (auto s_ptr = value.asString()) {
                entry.timestamp = **s_ptr;
                entry.parseTime();
            }
            else if (auto i = value.asInt64()) {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*i));
                entry.timestamp = entry.generatedTimestampString(LogEntry::getDefaultJsonOptions());
            } else if (auto u = value.asUint64()) {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*u));
                entry.timestamp = entry.generatedTimestampString(LogEntry::getDefaultJsonOptions());
            }
        }
        else if (key == "process_id")
        {
            if (auto u = value.asUint64())
                entry.process_id = *u;
            else if (auto i = value.asInt64())
                entry.process_id = static_cast<uint64_t>(*i);
        }
        else if (key == "host_name")
        {
            if (auto s_ptr = value.asString())
                entry.host_name = **s_ptr;
        }
        else if (key == "app_name")
        {
            if (auto s_ptr = value.asString())
                entry.app_name = **s_ptr;
        }
        else if (key == "event_id")
        {
            if (auto s_ptr = value.asString())
                entry.event_id = **s_ptr;
        }
        else if (key == "thread_id")
        {
            if (auto s_ptr = value.asString())
                entry.thread_id = **s_ptr;
        }
        else if (key == "thread_name")
        {
            if (auto s_ptr = value.asString())
                entry.thread_name = **s_ptr;
        }
        else if (key == "trace_id")
        {
            if (auto s_ptr = value.asString())
                entry.trace_id = **s_ptr;
        }
        else if (key == "span_id")
        {
            if (auto s_ptr = value.asString())
                entry.span_id = **s_ptr;
        }
        else if (key == "source")
        {
            if (auto obj_ptr = value.asObject()) {
                const auto& sourceObj = **obj_ptr;
                if (auto file_val = sourceObj.find("file"); file_val != sourceObj.end()) {
                    if (auto s_ptr = file_val->second.asString())
                        entry.source_file = **s_ptr;
                }
                if (auto func_val = sourceObj.find("function"); func_val != sourceObj.end()) {
                    if (auto s_ptr = func_val->second.asString())
                        entry.source_function = **s_ptr;
                }
                if (auto line_val = sourceObj.find("line"); line_val != sourceObj.end()) {
                    if (auto i = line_val->second.asInt64())
                        entry.source_line = static_cast<int>(*i);
                }
            }
        }
        else if (key == "tags")
        {
            if (auto list_ptr = value.asList()) {
                for (const auto &v : **list_ptr)
                {
                    if (auto s_ptr = v.asString())
                    {
                        entry.tags.insert(**s_ptr);
                    }
                }
            }
        }
        else if (key == "attributes")
        {
            if (auto obj_ptr = value.asObject()) {
                entry.attributes.clear();
                for (const auto& [attr_key, attr_val] : **obj_ptr) {
                    entry.attributes[attr_key] = attr_val;
                }
            }
        }
        else
        {
            entry.attributes[key] = value;
        }
    }
    return entry;
}

std::map<std::string, LogValue> LogEntry::toMap() const
{
    std::map<std::string, LogValue> m;

    m["timestamp"] = timestamp.empty() ? []() {
        LogEntry::JsonOptions opts_for_timestamp;
        opts_for_timestamp.precision = LogFormattingOptions::Precision::Millis;
        return generatedTimestampString(time_point, opts_for_timestamp);
    }() : LogValue(timestamp);
    m["level"] = std::string(LogEntry::levelToString(level));
    m["message"] = message;

    if (process_id != 0)
        m["process_id"] = process_id;
    if (!host_name.empty())
        m["host_name"] = host_name;
    if (!app_name.empty())
        m["app_name"] = app_name;
    if (!thread_id.empty())
        m["thread_id"] = thread_id;
    if (!thread_name.empty())
        m["thread_name"] = thread_name;
    if (!trace_id.empty())
        m["trace_id"] = trace_id;
    if (!span_id.empty())
        m["span_id"] = span_id;
    if (!event_id.empty())
        m["event_id"] = event_id;


    if (!source_file.empty() || !source_function.empty() || source_line != 0) {
        LogObject sourceObject;
        if (!source_file.empty())
            sourceObject["file"] = source_file;
        if (!source_function.empty())
            sourceObject["function"] = source_function;
        if (source_line != 0)
            sourceObject["line"] = static_cast<int64_t>(source_line);
        m["source"] = LogValue(std::move(sourceObject));
    }

    if (!tags.empty())
    {
        LogList tagList;
        for (const auto &tag : tags)
            tagList.push_back(tag);
        m["tags"] = LogValue(std::move(tagList));
    }

    if (!attributes.empty())
    {
        LogObject attrs_map_copy;
        for (const auto& [key, val] : attributes) {
            attrs_map_copy[key] = val;
        }
        m["attributes"] = LogValue(std::move(attrs_map_copy));
    }
    return m;
}

std::expected<LogEntry, std::string> LogEntry::fromJson(std::string_view json_str)
{
    LogEntry entry;
    auto it = json_str.begin();
    auto end = json_str.end();

    it = skipWhitespace(it, end);
    if (it == end || *it != '{')
    {
        return std::unexpected("JSON must start with '{'");
    }
    ++it; // Skip '{'

    while (it != end)
    {
        it = skipWhitespace(it, end);
        if (*it == '}')
        {
            ++it; // Skip '}'
            break;
        }

        auto key_res = parseJsonString(it, end);
        if (!key_res)
            return std::unexpected("Failed to parse key: " + key_res.error());
        std::string key = *key_res;

        it = skipWhitespace(it, end);
        if (it == end || *it != ':')
        {
            return std::unexpected("Expected ':' after key");
        }
        ++it; // Skip ':'

        if (key == "timestamp")
        {
            auto val_res = parseLogValue(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse timestamp: " + val_res.error());

            if (auto s_ptr = val_res->asString())
            {
                entry.timestamp = *(*s_ptr);
                entry.parseTime();
            }
            else if (auto i = val_res->asInt64())
            {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*i));
                entry.timestamp = entry.generatedTimestampString(LogEntry::getDefaultJsonOptions());
            }
            else if (auto u = val_res->asUint64())
            {
                entry.time_point = std::chrono::system_clock::time_point(std::chrono::milliseconds(*u));
                entry.timestamp = entry.generatedTimestampString(LogEntry::getDefaultJsonOptions());
            }
        }
        else if (key == "level")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse level: " + val_res.error());
            entry.level = LogEntry::parseLevel(*val_res);
        }
        else if (key == "message")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse message: " + val_res.error());
            entry.message = *val_res;
        }
        else if (key == "process_id")
        {
            auto val_res = parseJsonNumber(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse process_id: " + val_res.error());
            if (val_res->is<uint64_t>())
            {
                                 entry.process_id = val_res->to<uint64_t>().value_or(0ULL);            }
            else if (val_res->is<int64_t>())
            {
                                 entry.process_id = static_cast<uint64_t>(val_res->to<int64_t>().value_or(0LL));            }
            else
            {
                return std::unexpected("process_id must be a number");
            }
        }
        else if (key == "host_name")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse host_name: " + val_res.error());
            entry.host_name = *val_res;
        }
        else if (key == "app_name")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse app_name: " + val_res.error());
            entry.app_name = *val_res;
        }
        else if (key == "event_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse event_id: " + val_res.error());
            entry.event_id = *val_res;
        }
        else if (key == "source")
        {
            auto obj_res = parseJsonObject(it, end);
            if (!obj_res)
                return std::unexpected("Failed to parse source object: " + obj_res.error());

            if (auto file_val = obj_res->find("file"); file_val != obj_res->end()) {
                if (auto s_ptr = file_val->second.asString())
                    entry.source_file = *(*s_ptr);
            }
            if (auto func_val = obj_res->find("function"); func_val != obj_res->end()) {
                if (auto s_ptr = func_val->second.asString())
                    entry.source_function = *(*s_ptr);
            }
            if (auto line_val = obj_res->find("line"); line_val != obj_res->end()) {
                if (auto i = line_val->second.asInt64())
                    entry.source_line = static_cast<int>(*i);
            }
        }
        else if (key == "thread_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse thread_id: " + val_res.error());
            entry.thread_id = *val_res;
        }
        else if (key == "thread_name")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse thread_name: " + val_res.error());
            entry.thread_name = *val_res;
        }
        else if (key == "trace_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse trace_id: " + val_res.error());
            entry.trace_id = *val_res;
        }
        else if (key == "span_id")
        {
            auto val_res = parseJsonString(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse span_id: " + val_res.error());
            entry.span_id = *val_res;
        }
        else if (key == "tags")
        {
            auto val_res = parseJsonStringArray(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse tags: " + val_res.error());
            entry.tags = *val_res;
        }
        else if (key == "attributes")
        {
            auto val_res = parseJsonObject(it, end);
            if (!val_res)
                return std::unexpected("Failed to parse attributes: " + val_res.error());
            entry.attributes = std::move(*val_res);
        }
        else
        {
            auto dummy_val_res = parseLogValue(it, end);
            if (!dummy_val_res)
                return std::unexpected("Failed to skip unknown value for key '" + key + "': " + dummy_val_res.error());
        }

        it = skipWhitespace(it, end);
        if (it != end && *it == ',')
        {
            ++it;
        }
        else if (it != end && *it == '}')
        {
            // End of object, will break loop
        }
        else if (it == end)
        {
            return std::unexpected("Unexpected end of JSON string while parsing object");
        }
        else
        {
            return std::unexpected(std::string("Expected ',' or '}' but found: ") + *it);
        }
    }

    return entry;
}

// Helper to escape JSON strings
std::string escapeJson(const std::string &s)
{
    std::ostringstream o;
    for (auto c : s)
    {
        switch (c)
        {
        case '"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\b':
            o << "\\b";
            break;
        case '\f':
            o << "\\f";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if ((unsigned char)c <= '\x1f')
            {
                // Hexadecimal representation for control characters
                char buf[7]; // e.g., "\u0000"
                std::snprintf(buf, sizeof(buf), "\\u%04x", (int)(unsigned char)c);
                o << buf;
            }
            else
            {
                o << c;
            }
        }
    }
    return o.str();
}

struct JsonVisitor
{
    std::ostream &os;
    const LogFormattingOptions &options;
    int indent_level;

    void indent() const
    {
        if (options.pretty)
        {
            for (int i = 0; i < indent_level; ++i)
                os << "  ";
        }
    }

    void nl() const
    {
        if (options.pretty)
            os << "\n";
    }

    void operator()(std::monostate) const { os << "null"; }
    void operator()(bool b) const { os << (b ? "true" : "false"); }
    void operator()(int64_t i) const { os << i; }
    void operator()(uint64_t u) const { os << u; }
    void operator()(double d) const { os << d; }
    void operator()(const std::string &s) const { os << "\"" << escapeJson(s) << "\""; }

    void operator()(const std::vector<uint8_t>& binary) const {
        if (options.binary_encoding == LogFormattingOptions::BinaryEncoding::Hex) {
            os << "\"0x";
            auto flags = os.flags();
            os << std::hex << std::setfill('0');
            for (auto b : binary) {
                os << std::setw(2) << static_cast<int>(b);
            }
            os.flags(flags);
            os << "\"";
        } else {
            os << "\"" << LogEntryDetail::base64Encode(binary) << "\"";
        }
    }

    void operator()(std::chrono::nanoseconds duration) const {
        os << duration.count();
    }

    void operator()(const std::shared_ptr<LogList> &list) const
    {
        if (!list || list->empty())
        {
            os << "[]";
            return;
        }
        os << "[";
        nl();
        for (size_t i = 0; i < list->size(); ++i)
        {
            if (options.pretty)
            {
                for (int j = 0; j <= indent_level; ++j)
                    os << "  ";
            }
            std::visit(JsonVisitor{os, options, indent_level + 1}, (*list)[i]);
            if (i < list->size() - 1)
                os << ",";
            nl();
        }
        indent();
        os << "]";
    }

    void operator()(const std::shared_ptr<LogObject> &obj) const
    {
        if (!obj || obj->empty())
        {
            os << "{}";
            return;
        }
        os << "{";
        nl();
        size_t count = 0;
        for (const auto &[key, value] : *obj)
        {
            if (options.pretty)
            {
                for (int j = 0; j <= indent_level; ++j)
                    os << "  ";
            }
            os << "\"" << escapeJson(key) << "\": ";
            std::visit(JsonVisitor{os, options, indent_level + 1}, value);
            if (++count < obj->size())
                os << ",";
            nl();
        }
        indent();
        os << "}";
    }
};

std::string LogEntry::toJson(const JsonOptions &options) const
{
    std::ostringstream oss;
    std::string nl_char = options.pretty ? "\n" : "";
    std::string indent_prefix = options.pretty ? "  " : "";
    int current_indent_level = 0;

    auto should_include_field = [&](std::string_view field_name) {
        if (!options.include_fields.empty()) {
            return options.include_fields.count(std::string(field_name)) > 0;
        }
        if (!options.exclude_fields.empty()) {
            return options.exclude_fields.count(std::string(field_name)) == 0;
        }
        return true; // Include all if no filters specified
    };

    oss << "{" << nl_char;
    current_indent_level++;

    bool first_field = true;

    auto write_field_key = [&](std::string_view key) {
        if (!first_field) {
            oss << "," << nl_char;
        } else {
            first_field = false;
        }
        for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
        oss << "\"" << escapeJson(std::string(key)) << "\": ";
    };

    // Timestamp
    if (should_include_field("timestamp")) {
        write_field_key("timestamp");
        if (options.timestamp_format == LogFormattingOptions::TimestampFormat::UnixMillis)
        {
            oss << std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count();
        }
        else
        {
            TimestampFormatOptions formatOpts;
            formatOpts.precision = options.precision;
            formatOpts.timezone = options.timezone;
            formatOpts.custom_timestamp_format = options.custom_timestamp_format;
            oss << "\"" << LogEntry::formatTimestamp(time_point, options.timestamp_format, formatOpts) << "\"";
        }
    }

    // Level
    if (should_include_field("level")) {
        write_field_key("level");
        oss << "\"" << LogEntry::levelToString(level) << "\"";
    }
    
    // Message
    if (should_include_field("message")) {
        write_field_key("message");
        oss << "\"" << escapeJson(message) << "\"";
    }

    // Process ID
    if (should_include_field("process_id") && process_id != 0) {
        write_field_key("process_id");
        oss << process_id;
    }

    // Host Name
    if (should_include_field("host_name") && !host_name.empty()) {
        write_field_key("host_name");
        oss << "\"" << escapeJson(host_name) << "\"";
    }

    // App Name
    if (should_include_field("app_name") && !app_name.empty()) {
        write_field_key("app_name");
        oss << "\"" << escapeJson(app_name) << "\"";
    }

    // Event ID
    if (should_include_field("event_id") && !event_id.empty()) {
        write_field_key("event_id");
        oss << "\"" << escapeJson(event_id) << "\"";
    }

    // Source
    bool source_present = !source_file.empty() || !source_function.empty() || source_line != 0;
    if (should_include_field("source") && (source_present || !options.exclude_empty))
    {
        write_field_key("source");
        if (!source_present && options.exclude_empty) {
            oss << "{}";
        } else {
            oss << "{" << nl_char;
            current_indent_level++;
            bool first_source_field = true;
            auto write_source_field = [&](std::string_view key, const auto& value, bool is_string = true) {
                if (!first_source_field) {
                    oss << "," << nl_char;
                } else {
                    first_source_field = false;
                }
                for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
                oss << "\"" << escapeJson(std::string(key)) << "\": ";
                if (is_string) oss << "\"";
                oss << escapeJson(std::string(value));
                if (is_string) oss << "\"";
            };

            if (!source_file.empty()) write_source_field("file", source_file);
            if (!source_function.empty()) write_source_field("function", source_function);
            if (source_line != 0) write_source_field("line", std::to_string(source_line), false);

            current_indent_level--;
            oss << nl_char;
            for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix;
            oss << "}";
        }
    }

    // Thread ID & Name
    if (options.include_thread) {
        if (should_include_field("thread_id") && !thread_id.empty()) {
            write_field_key("thread_id");
            oss << "\"" << escapeJson(thread_id) << "\"";
        }
        if (should_include_field("thread_name") && !thread_name.empty()) {
            write_field_key("thread_name");
            oss << "\"" << escapeJson(thread_name) << "\"";
        }
    }

    // Tracing
    if (options.include_tracing) {
        if (should_include_field("trace_id") && !trace_id.empty()) {
            write_field_key("trace_id");
            oss << "\"" << escapeJson(trace_id) << "\"";
        }
        if (should_include_field("span_id") && !span_id.empty()) {
            write_field_key("span_id");
            oss << "\"" << escapeJson(span_id) << "\"";
        }
    }

    // Tags
    if (should_include_field("tags") && !tags.empty()) {
        write_field_key("tags");
        oss << "[";
        if (options.pretty && tags.size() > 1) { oss << nl_char; current_indent_level++; }
        bool first_tag = true;
        for (const auto& tag : tags) {
            if (!first_tag) { oss << ","; if (options.pretty) oss << nl_char; }
            if (options.pretty && tags.size() > 1) { for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix; }
            oss << "\"" << escapeJson(tag) << "\"";
            first_tag = false;
        }
        if (options.pretty && tags.size() > 1) { oss << nl_char; current_indent_level--; for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix; }
        oss << "]";
    }
    
    // Attributes
    if (should_include_field("attributes") && (!attributes.empty() || !options.exclude_empty)) {
        write_field_key("attributes");
        oss << "{";
        if (options.pretty && !attributes.empty()) { oss << nl_char; current_indent_level++; }
        bool first_attr = true;
        for (const auto& [key, value] : attributes) {
            if (!options.include_fields.empty() && options.include_fields.count(key) == 0) continue;
            if (!options.exclude_fields.empty() && options.exclude_fields.count(key) > 0) continue;

            if (!first_attr) { oss << ","; if (options.pretty) oss << nl_char; }
            if (options.pretty) { for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix; }
            std::string escaped_key = escapeJson(key);
            oss << "\"" << escaped_key << "\": ";
            std::visit(JsonVisitor{oss, options, current_indent_level}, value);
            if (++count < attributes.size()) { /* count needs to be declared outside if to properly track */ }
            first_attr = false;
        }
        if (options.pretty && !attributes.empty()) { oss << nl_char; current_indent_level--; for (int i = 0; i < current_indent_level; ++i) oss << indent_prefix; }
        oss << "}";
    }

    oss << nl_char << "}";
    return oss.str();
}

namespace LogEntryDetail {
    // Base64 helper, now in LogEntryDetail namespace
    std::string base64Encode(const std::vector<uint8_t>& data) {
        static const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string ret;
        int i = 0;
        int j = 0;
        uint8_t char_array_3[3];
        uint8_t char_array_4[4];

        for (auto b : data) {
            char_array_3[i++] = b;
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; (i < 4); i++) ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i) {
            for (j = i; j < 3; j++) char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];
            while ((i++ < 3)) ret += '=';
        }

        return ret;
    }
} // namespace LogEntryDetail
