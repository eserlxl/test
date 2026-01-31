#include <LogEntry.h>
#include <cassert>
#include <iostream>
#include <format>
#include <sstream>
#include <cstring>
#include <unordered_set>

void testLevelParsing()
{
    assert(LogEntry::parseLevel("DEBUG") == LogLevel::DEBUG);
    assert(LogEntry::parseLevel("dbg") == LogLevel::DEBUG);
    assert(LogEntry::parseLevel("INFO") == LogLevel::INFO);
    assert(LogEntry::parseLevel("error") == LogLevel::ERROR);
    assert(LogEntry::parseLevel("UNKNOWN_JUNK") == LogLevel::UNKNOWN);

    // Test backward compatibility
    assert(parseLogLevel("DEBUG") == LogLevel::DEBUG);

    std::cout << "testLevelParsing passed" << std::endl;
}

void testLevelToString()
{
    assert(LogEntry::levelToString(LogLevel::DEBUG) == "DEBUG");
    assert(LogEntry::levelToString(LogLevel::INFO) == "INFO");
    assert(LogEntry::levelToString(LogLevel::UNKNOWN) == "UNKNOWN");
    std::cout << "testLevelToString passed" << std::endl;
}

void testTimeParsing()
{
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00";
    assert(entry.parseTime());
    // Checking exact value might be tricky due to timezone, but we can check round trip if we assume local time.
    std::string generated = entry.generatedTimestampString(LogEntry::JsonOptions{.precision = LogEntry::JsonOptions::Precision::Seconds, .include_fields = {}, .exclude_fields = {}});
    assert(generated == "2023-10-27 10:00:00");

    // Test with fractional
    entry.timestamp = "2023-10-27 10:00:00.123";
    assert(entry.parseTime());
    generated = entry.generatedTimestampString(LogEntry::JsonOptions{.precision = LogEntry::JsonOptions::Precision::Millis, .include_fields = {}, .exclude_fields = {}});
    if (generated != "2023-10-27 10:00:00.123")
    {
        std::cout << "Generated: '" << generated << "', Expected: '2023-10-27 10:00:00.123'" << std::endl;
    }
    assert(generated == "2023-10-27 10:00:00.123");

    // Test invalid time
    entry.timestamp = "invalid";
    assert(!entry.parseTime());

    std::cout << "testTimeParsing passed" << std::endl;
}

void testComparison()
{
    LogEntry e1;
    e1.timestamp = "2023-10-27 10:00:00.000";
    e1.parseTime();

    LogEntry e2;
    e2.timestamp = "2023-10-27 10:00:00.001";
    e2.parseTime();

    assert(e1 < e2);
    assert(e2 > e1);
    assert(e1 != e2);

    LogEntry e3;
    e3.timestamp = "2023-10-27 10:00:00.000";
    e3.parseTime();
    e3.message = "A"; // Default message is empty
    e1.message = "A";
    assert(e1 == e3);

    // Test comparison with strings only (no time_point)
    LogEntry e4;
    e4.timestamp = "2023-10-27 10:00:00.000";
    e4.message = "A";
    assert(e1 == e4);

    // Test tie-breaker (tags)
    LogEntry e5 = e1;
    e5.withTag("test");
    assert(e1 < e5);

    // Test tie-breaker (message)
    LogEntry e6 = e5;
    e6.message = "B";
    assert(e5 < e6);

    std::cout << "testComparison passed" << std::endl;
}

void testFormatting()
{
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00.000";
    entry.level = LogLevel::INFO;
    entry.message = "System started";

    std::string formatted = std::format("{}", entry);
    // Expected: [2023-10-27 10:00:00.000] [INFO] System started
    assert(formatted == "[2023-10-27 10:00:00.000] [INFO] System started");
    std::cout << "testFormatting passed" << std::endl;
}

void testStructuredLogging()
{
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00.000";
    entry.level = LogLevel::INFO;
    entry.message = "User login";

    // Test Attributes
    entry.setAttribute("user_id", "12345");
    entry.setAttribute("ip_address", "192.168.1.1");

    // Use getAttributeAsString for checking
    assert(entry.getAttributeAsString("user_id") == "12345");
    assert(entry.getAttributeAsString("ip_address") == "192.168.1.1");

    // Test typed attributes
    entry.setAttribute("retries", int64_t{3}); // int -> int64_t
    entry.setAttribute("u_val", uint64_t{100});
    entry.setAttribute("score", 95.5);    // double
    entry.setAttribute("is_admin", true); // bool

    // Check variant content
    assert(std::get<int64_t>(entry.attributes["retries"]) == 3);
    assert(std::get<uint64_t>(entry.attributes["u_val"]) == 100);
    assert(std::get<double>(entry.attributes["score"]) == 95.5);
    assert(std::get<bool>(entry.attributes["is_admin"]) == true);

    // Test new accessors
    assert(entry.hasAttribute("retries"));
    assert(!entry.hasAttribute("missing"));
    assert(entry.getAttributeAs<int64_t>("retries") == 3);
    assert(entry.getAttributeAs<uint64_t>("u_val") == 100);
    assert(!entry.getAttributeAs<int64_t>("u_val").has_value()); // Wrong type

    // Test Tags
    entry.withTags({"auth", "security"});
    assert(entry.hasTag("auth"));
    assert(entry.hasTag("security"));
    assert(!entry.hasTag("performance"));

    // Test Source Info
    entry.source_file = "auth.cpp";
    entry.source_function = "login";
    entry.source_line = 42;
    entry.thread_id = "0x123";

    // Test JSON Serialization
    std::string json = entry.toJson();
    // Simple checks for JSON structure
    assert(json.find("\"message\": \"User login\"") != std::string::npos);
    assert(json.find("\"user_id\": \"12345\"") != std::string::npos);
    assert(json.find("\"tags\": [\"auth\", \"security\"]") != std::string::npos);

    // Check typed values in JSON (not quoted)
    assert(json.find("\"retries\": 3") != std::string::npos);
    assert(json.find("\"u_val\": 100") != std::string::npos);
    assert(json.find("\"score\": 95.5") != std::string::npos);
    assert(json.find("\"is_admin\": true") != std::string::npos);

    assert(json.find("\"file\": \"auth.cpp\"") != std::string::npos);
    assert(json.find("\"thread_id\": \"0x123\"") != std::string::npos);
    assert(json.find("\"line\": 42") != std::string::npos);

    // Test JSON Options
    LogEntry::JsonOptions opts;
    opts.pretty = true;
    opts.include_source = false;
    std::string pretty_json = entry.toJson(opts);
    assert(pretty_json.find("\n") != std::string::npos);
    assert(pretty_json.find("\"source\"") == std::string::npos);

    // Test Exception
    LogEntry e_entry;
    try
    {
        throw std::runtime_error("test exception");
    }
    catch (const std::exception &e)
    {
        e_entry.withException(e);
    }
    assert(e_entry.getAttributeAs<std::string>("exception_message") == "test exception");

    std::cout << "testStructuredLogging passed" << std::endl;
}

void testFluentApi()
{
    auto entry = LogEntry::create(LogLevel::WARNING, "Something happened")
                     .withAttribute("error_code", int64_t{404})
                     .withAttribute("retry", false)
                     .withThreadId("thread-1");

    assert(entry.level == LogLevel::WARNING);
    assert(entry.message == "Something happened");
    assert(std::get<int64_t>(entry.attributes["error_code"]) == 404);
    assert(std::get<bool>(entry.attributes["retry"]) == false);
    assert(entry.thread_id == "thread-1");
    assert(!entry.source_file.empty()); // Should be captured by create()

    std::cout << "testFluentApi passed" << std::endl;
}

void testStreamOperator()
{
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00";
    entry.level = LogLevel::ERROR;
    entry.message = "Stream test";

    std::ostringstream oss;
    oss << entry;
    assert(oss.str() == "[2023-10-27 10:00:00] [ERROR] Stream test");

    std::cout << "testStreamOperator passed" << std::endl;
}

void testSeverityChecks()
{

    assert(LogEntry::isAtLeast(LogLevel::ERROR, LogLevel::WARNING));

    assert(LogEntry::isAtLeast(LogLevel::WARNING, LogLevel::WARNING));

    assert(!LogEntry::isAtLeast(LogLevel::INFO, LogLevel::WARNING));

    assert(LogEntry::isError(LogLevel::ERROR));

    assert(LogEntry::isError(LogLevel::CRITICAL));

    assert(!LogEntry::isError(LogLevel::WARNING));

    assert(!LogEntry::isError(LogLevel::INFO));

    std::cout << "testSeverityChecks passed" << std::endl;
}

void testTracing()
{

    LogEntry entry = LogEntry::create(LogLevel::INFO, "Tracing test")

                         .withTraceContext("trace-123", "span-456");

    assert(entry.trace_id == "trace-123");

    assert(entry.span_id == "span-456");

    std::string json = entry.toJson();

    assert(json.find("\"trace_id\": \"trace-123\"") != std::string::npos);

    assert(json.find("\"span_id\": \"span-456\"") != std::string::npos);

    // Test comparison with tracing

    LogEntry e2 = entry;

    e2.trace_id = "trace-124";

    assert(entry < e2);

    std::cout << "testTracing passed" << std::endl;
}

void testJsonDeserialization()
{

    std::string json = R"({
    
            "timestamp": "2023-10-27 10:00:00.123",
    
            "level": "ERROR",
    
            "message": "Test deserialization",
    
            "thread_id": "thread-1",
    
            "trace_id": "trace-abc",
    
            "span_id": "span-def",
    
            "tags": ["tag1", "tag2"],
    
            "attributes": {
    
                "key1": "value1",
    
                "key2": 123,
    
                "key3": true,
    
                "key4": 123.456
    
            },
    
            "source": {
    
                "file": "test.cpp",
    
                "function": "main",
    
                "line": 100
    
            }
    
        })";

    auto result = LogEntry::fromJson(json);

    if (!result)
    {

        std::cout << "JSON parsing failed: " << result.error() << std::endl;
    }

    assert(result.has_value());

    const LogEntry &entry = *result;

    assert(entry.level == LogLevel::ERROR);

    assert(entry.message == "Test deserialization");

    assert(entry.timestamp == "2023-10-27 10:00:00.123");

    assert(entry.thread_id == "thread-1");

    assert(entry.trace_id == "trace-abc");

    assert(entry.span_id == "span-def");

    assert(entry.hasTag("tag1"));

    assert(entry.hasTag("tag2"));

    assert(entry.getAttributeAsString("key1") == "value1");

    assert(std::get<int64_t>(entry.attributes.at("key2")) == 123);

    assert(std::get<bool>(entry.attributes.at("key3")) == true);

    // Double comparison

    assert(std::abs(std::get<double>(entry.attributes.at("key4")) - 123.456) < 0.0001);

    assert(entry.source_file == "test.cpp");

    assert(entry.source_function == "main");

    assert(entry.source_line == 100);

    // Test Round Trip

    std::string serialized = entry.toJson();

    auto deserialized = LogEntry::fromJson(serialized);

    assert(deserialized.has_value());

    assert(deserialized->message == entry.message);

    assert(deserialized->tags == entry.tags);

    // Note: attributes might have slightly different string representations for doubles if not carefully handled, but basic types should match.

    std::cout << "testJsonDeserialization passed" << std::endl;
}

void testCloning()
{
    std::cout << "Starting testCloning..." << std::endl;
    LogEntry original = LogEntry::create(LogLevel::INFO, "Original Message");
    original.withTag("original_tag")
            .withAttribute("initial_attr", "value1")
            .withAttribute("attr_to_remove", static_cast<int64_t>(123LL));

    // Test clonedWithTag (already exists)
    LogEntry clone_with_tag = original.clonedWithTag("new_tag");
    assert(clone_with_tag.hasTag("original_tag"));
    assert(clone_with_tag.hasTag("new_tag"));
    assert(!original.hasTag("new_tag")); // Original unmodified

    // Test clonedWithLevel
    LogEntry clone_with_level = original.clonedWithLevel(LogLevel::ERROR);
    assert(clone_with_level.level == LogLevel::ERROR);
    assert(original.level == LogLevel::INFO); // Original unmodified

    // Test clonedWithMessage
    LogEntry clone_with_message = original.clonedWithMessage("Modified Message");
    assert(clone_with_message.message == "Modified Message");
    assert(original.message == "Original Message"); // Original unmodified

    // Test clonedWithAttribute
    LogEntry clone_with_new_attr = original.clonedWithAttribute("new_attr", LogValue("new_value"));
    assert(clone_with_new_attr.hasAttribute("new_attr"));
    assert(clone_with_new_attr.getAttributeAs<std::string>("new_attr") == "new_value");
    assert(!original.hasAttribute("new_attr")); // Original unmodified

    LogEntry clone_with_updated_attr = original.clonedWithAttribute("initial_attr", LogValue("updated_value"));
    assert(clone_with_updated_attr.getAttributeAs<std::string>("initial_attr") == "updated_value");
    assert(original.getAttributeAs<std::string>("initial_attr") == "value1"); // Original unmodified

    // Test clonedWithAttributes
    std::map<std::string, LogValue> new_attrs = {
        {"batch_attr1", 100LL},
        {"batch_attr2", true}
    };
    LogEntry clone_with_batch_attrs = original.clonedWithAttributes(new_attrs);
    assert(clone_with_batch_attrs.hasAttribute("batch_attr1"));
    assert(clone_with_batch_attrs.hasAttribute("batch_attr2"));
    assert(clone_with_batch_attrs.hasAttribute("initial_attr")); // Existing attributes should be preserved
    assert(!original.hasAttribute("batch_attr1")); // Original unmodified

    // Test clonedWithoutAttribute
    LogEntry clone_without_attr = original.clonedWithoutAttribute("attr_to_remove");
    assert(!clone_without_attr.hasAttribute("attr_to_remove"));
    assert(original.hasAttribute("attr_to_remove")); // Original unmodified

    std::cout << "testCloning passed" << std::endl;
}

void testTagManagement() {
    std::cout << "Starting testTagManagement..." << std::endl;

    LogEntry entry = LogEntry::create(LogLevel::INFO, "Tag management test");
    entry.withTag("tag1").withTag("tag2").withTag("tag3");

    // Test getTags
    const auto& tags = entry.getTags();
    assert(tags.size() == 3);
    assert(tags.count("tag1") == 1);
    assert(tags.count("tag2") == 1);
    assert(tags.count("tag3") == 1);
    assert(tags.count("non_existent") == 0);

    // Test hasAllTags
    assert(entry.hasAllTags({"tag1", "tag2"}));
    assert(!entry.hasAllTags({"tag1", "non_existent"}));
    assert(entry.hasAllTags({})); // Always true for empty list

    // Test hasAnyTag
    assert(entry.hasAnyTag({"tag1", "non_existent"}));
    assert(!entry.hasAnyTag({"non_existent1", "non_existent2"}));
    assert(!entry.hasAnyTag({})); // Always false for empty list

    // Test removeTag
    entry.removeTag("tag2");
    assert(entry.getTags().size() == 2);
    assert(!entry.hasTag("tag2"));
    assert(entry.hasTag("tag1"));

    // Remove non-existent tag (should do nothing)
    entry.removeTag("non_existent");
    assert(entry.getTags().size() == 2);

    // Test clearTags
    entry.clearTags();
    assert(entry.getTags().empty());

    // Re-add tags for cloned tests
    entry.withTag("clone_tag1").withTag("clone_tag2");

    // Test clonedWithoutTag
    LogEntry clone_without_one = entry.clonedWithoutTag("clone_tag1");
    assert(!clone_without_one.hasTag("clone_tag1"));
    assert(clone_without_one.hasTag("clone_tag2"));
    assert(entry.hasTag("clone_tag1")); // Original untouched

    // Test clonedWithoutTags
    LogEntry clone_without_all = entry.clonedWithoutTags();
    assert(clone_without_all.getTags().empty());
    assert(!entry.getTags().empty()); // Original untouched

    std::cout << "testTagManagement passed" << std::endl;
}

void testToMapRoundTrip()
{
    std::cout << "Starting testToMapRoundTrip..." << std::endl;
    LogEntry original = LogEntry::create(LogLevel::INFO, "Map Round Trip Test");
    original.withAttribute("int_attr", int64_t{123})
        .withAttribute("double_attr", 45.67)
        .withAttribute("bool_attr", true)
        .withTag("map_test")
        .withTag("another_tag")
        .withProcessId(54321)
        .withHost("test-host")
        .withApp("test-app")
        .withThreadName("main-thread") // New field
        .withTraceContext("trace-xyz", "span-abc")
        .withSource(); // Populates source_file, func, line

    // Ensure timestamp is generated
    original.withMetadata();

    std::map<std::string, LogValue> map_repr = original.toMap();
    LogEntry from_map = LogEntry::fromMap(map_repr);

    // Basic fields
    assert(from_map.level == original.level);
    assert(from_map.message == original.message);
    assert(from_map.process_id == original.process_id);
    assert(from_map.host_name == original.host_name);
    assert(from_map.app_name == original.app_name);
    assert(from_map.thread_name == original.thread_name); // Check new thread_name
    assert(from_map.trace_id == original.trace_id);
    assert(from_map.span_id == original.span_id);
    
    // Timestamp
    // toMap stores timestamp as string, fromMap parses it back to time_point
    assert(!from_map.timestamp.empty());
    assert(from_map.time_point == original.time_point);

    // Tags
    assert(from_map.tags == original.tags); // Now this should pass

    // Nested Source
    assert(from_map.source_file == original.source_file);
    assert(from_map.source_function == original.source_function);
    assert(from_map.source_line == original.source_line);

    // Nested Attributes
    // Since attributes are nested under "attributes" key in the map,
    // from_map.attributes should now contain a single LogValue with LogObject type
    // or should be correctly populated based on fromMap parsing.
    // My fromMap implementation parses attributes explicitly if the key is "attributes",
    // otherwise it adds them directly to the entry.attributes.
    // Since toMap() now nests everything under "attributes", from_map should have:
    // from_map.attributes should be a direct copy of original.attributes.
    
    // Check specific attribute values
    assert(from_map.attributes.at("int_attr").get<int64_t>() == original.attributes.at("int_attr").get<int64_t>());
    assert(std::abs(from_map.attributes.at("double_attr").get<double>() - original.attributes.at("double_attr").get<double>()) < 1e-9);
    assert(from_map.attributes.at("bool_attr").get<bool>() == original.attributes.at("bool_attr").get<bool>());

    std::cout << "testToMapRoundTrip passed" << std::endl;
}

void testJsonFormatDetection()
{
    // Test ISO8601
    std::string iso_json = R"({"timestamp": "2023-10-27T10:00:00.123Z", "level": "INFO", "message": "ISO test"})";
    auto iso_entry_res = LogEntry::fromJson(iso_json);
    assert(iso_entry_res.has_value());
    assert(iso_entry_res->level == LogLevel::INFO);
    assert(iso_entry_res->message == "ISO test");
    // Verify time_point is set
    assert(iso_entry_res->time_point.time_since_epoch().count() != 0);

    // Test Unix Millis
    // 2023-10-27 10:00:00.123 UTC is 1698400800123 milliseconds
    std::string unix_json = R"({"timestamp": 1698400800123, "level": "DEBUG", "message": "UnixMillis test"})";
    auto unix_entry_res = LogEntry::fromJson(unix_json);
    assert(unix_entry_res.has_value());
    assert(unix_entry_res->level == LogLevel::DEBUG);
    assert(unix_entry_res->message == "UnixMillis test");
    assert(unix_entry_res->time_point.time_since_epoch().count() != 0);

    // Test default format (like current generatedTimestampString)
    std::string default_json = R"({"timestamp": "2023-10-27 10:00:00.456", "level": "WARNING", "message": "Default format test"})";
    auto default_entry_res = LogEntry::fromJson(default_json);
    assert(default_entry_res.has_value());
    assert(default_entry_res->level == LogLevel::WARNING);
    assert(default_entry_res->message == "Default format test");
    assert(default_entry_res->time_point.time_since_epoch().count() != 0);

    std::cout << "testJsonFormatDetection passed" << std::endl;
}

void testTimestampUtilities() {
    std::cout << "Starting testTimestampUtilities..." << std::endl;

    // A known time_point (UTC to avoid local timezone issues for comparison)
    std::chrono::system_clock::time_point tp;
    std::tm t = {};
    t.tm_year = 2023 - 1900;
    t.tm_mon = 10 - 1; // October (tm_mon is 0-11)
    t.tm_mday = 15;
    t.tm_hour = 14;
    t.tm_min = 30;
    t.tm_sec = 45;
    t.tm_isdst = -1; // Not set by system

#if defined(_WIN32) || defined(_WIN64)
    std::time_t tt = _mkgmtime(&t); // Windows equivalent of timegm
#else
    std::time_t tt = timegm(&t); // For UTC time
#endif
    tp = std::chrono::system_clock::from_time_t(tt);
    tp += std::chrono::milliseconds(500); // Add 500ms

    // Test formatTimestamp - ISO8601, Millis, UTC
    LogEntry::TimestampFormatOptions opts_iso_millis_utc;
    opts_iso_millis_utc.precision = LogEntry::JsonOptions::Precision::Millis;
    opts_iso_millis_utc.timezone = LogEntry::JsonOptions::Timezone::UTC;
    std::string formatted_iso_millis_utc = LogEntry::formatTimestamp(tp, LogEntry::JsonOptions::TimestampFormat::ISO8601, opts_iso_millis_utc);
    assert(formatted_iso_millis_utc == "2023-10-15T14:30:45.500Z");

    // Test formatTimestamp - ISO8601, Micros, UTC
    LogEntry::TimestampFormatOptions opts_iso_micros_utc;
    opts_iso_micros_utc.precision = LogEntry::JsonOptions::Precision::Micros;
    opts_iso_micros_utc.timezone = LogEntry::JsonOptions::Timezone::UTC;
    std::string formatted_iso_micros_utc = LogEntry::formatTimestamp(tp, LogEntry::JsonOptions::TimestampFormat::ISO8601, opts_iso_micros_utc);
    assert(formatted_iso_micros_utc.substr(0, 20) == "2023-10-15T14:30:45."); // Check prefix
    // The exact microseconds might vary slightly based on system_clock resolution, verify the length and part
    assert(formatted_iso_micros_utc.length() == strlen("YYYY-MM-DDTHH:MM:SS.xxxxxxZ")); // 27 chars
    assert(formatted_iso_micros_utc.at(26) == 'Z'); // Ends with Z

    // Test formatTimestamp - ISO8601, Nanos, UTC
    LogEntry::TimestampFormatOptions opts_iso_nanos_utc;
    opts_iso_nanos_utc.precision = LogEntry::JsonOptions::Precision::Nanos;
    opts_iso_nanos_utc.timezone = LogEntry::JsonOptions::Timezone::UTC;
    std::string formatted_iso_nanos_utc = LogEntry::formatTimestamp(tp, LogEntry::JsonOptions::TimestampFormat::ISO8601, opts_iso_nanos_utc);
    assert(formatted_iso_nanos_utc.substr(0, 20) == "2023-10-15T14:30:45."); // Check prefix
    assert(formatted_iso_nanos_utc.length() == strlen("YYYY-MM-DDTHH:MM:SS.xxxxxxxxxZ")); // 30 chars
    assert(formatted_iso_nanos_utc.at(29) == 'Z'); // Ends with Z

    // Test formatTimestamp - Default, Millis, Local (approximate check due to local time)
    LogEntry::TimestampFormatOptions opts_default_millis_local;
    opts_default_millis_local.precision = LogEntry::JsonOptions::Precision::Millis;
    opts_default_millis_local.timezone = LogEntry::JsonOptions::Timezone::Local;
    std::string formatted_default_millis_local = LogEntry::formatTimestamp(tp, LogEntry::JsonOptions::TimestampFormat::Default, opts_default_millis_local);
    // Cannot assert exact string due to local timezone, check format
    assert(formatted_default_millis_local.find("2023-10-15") != std::string::npos);
    assert(formatted_default_millis_local.find(":") != std::string::npos);
    assert(formatted_default_millis_local.find(".") != std::string::npos);

    // Test formatTimestamp - Custom format
    LogEntry::TimestampFormatOptions opts_custom;
    opts_custom.custom_format = "%Y/%m/%d %H:%M:%S - Custom";
    std::string formatted_custom = LogEntry::formatTimestamp(tp, LogEntry::JsonOptions::TimestampFormat::Default, opts_custom);
    // Exact match is hard due to potential fractional part from LogEntry side, just check structure
    assert(formatted_custom.find("2023/10/15 14:30:45 - Custom") != std::string::npos); // Adjusted expected custom format string


    // Test parseTimestamp (auto-detection)
    auto parsed_iso = LogEntry::parseTimestamp("2023-10-15T14:30:45.500Z");
    assert(parsed_iso.has_value());
    assert(parsed_iso.value() == tp); // Should match original tp

    auto parsed_default = LogEntry::parseTimestamp("2023-10-15 14:30:45.500");
    // This will depend on the system's local time setting if parseDefaultFormat is local.
    // Assuming `mktime` (local time) is used for default parsing, create a local time_point for comparison
    std::tm t_local = {};
    t_local.tm_year = 2023 - 1900;
    t_local.tm_mon = 10 - 1; // October
    t_local.tm_mday = 15;
    t_local.tm_hour = 14;
    t_local.tm_min = 30;
    t_local.tm_sec = 45;
    t_local.tm_isdst = -1;
    std::time_t tt_local = std::mktime(&t_local);
    std::chrono::system_clock::time_point tp_local = std::chrono::system_clock::from_time_t(tt_local);
    tp_local += std::chrono::milliseconds(500); // Add 500ms

    assert(parsed_default.has_value());
    assert(parsed_default.value() == tp_local); // Should match local tp


    auto parsed_unix = LogEntry::parseTimestamp("1697380245500"); // 2023-10-15 14:30:45.500 UTC
    assert(parsed_unix.has_value());
    assert(parsed_unix.value() == tp);


    // Test parseTimestamp (specific format)
    auto parsed_iso_specific = LogEntry::parseTimestamp("2023-10-15T14:30:45.500Z", LogEntry::JsonOptions::TimestampFormat::ISO8601);
    assert(parsed_iso_specific.has_value());
    assert(parsed_iso_specific.value() == tp);

    auto parsed_default_specific = LogEntry::parseTimestamp("2023-10-15 14:30:45.500", LogEntry::JsonOptions::TimestampFormat::Default);
    assert(parsed_default_specific.has_value());
    assert(parsed_default_specific.value() == tp_local);

    auto parsed_unix_specific = LogEntry::parseTimestamp("1697380245500", LogEntry::JsonOptions::TimestampFormat::UnixMillis);
    assert(parsed_unix_specific.has_value());
    assert(parsed_unix_specific.value() == tp);

    // Test invalid parsing
    assert(!LogEntry::parseTimestamp("invalid").has_value());
    assert(!LogEntry::parseTimestamp("2023-XX-YY").has_value());
    assert(!LogEntry::parseTimestamp("12345ab").has_value()); // Invalid UnixMillis

    std::cout << "testTimestampUtilities passed" << std::endl;
}

void testValidation()
{
    std::cout << "Starting testValidation..." << std::endl;
    LogEntry entry;
    assert(!entry.isValid()); // No level, no message

    entry.level = LogLevel::INFO;
    assert(!entry.isValid()); // No message

    entry.message = "Hello";
    assert(entry.isValid()); // Has level and message

    entry.level = LogLevel::UNKNOWN;
    assert(!entry.isValid()); // Level is UNKNOWN
    entry.level = LogLevel::INFO; // Reset level for further tests

    // Test: Invalid timestamp string format
    entry.timestamp = "invalid-time-format";
    assert(!entry.isValid());
    entry.timestamp = ""; // Reset

    // Test: Inconsistent timestamp string and time_point
    entry.message = "Inconsistent Time";
    entry.timestamp = "2023-01-01 12:00:00.000";
    // Manually set time_point to a different time
    entry.time_point = std::chrono::system_clock::time_point(std::chrono::seconds(100)); // A very different time
    assert(!entry.isValid());
    entry.time_point = std::chrono::system_clock::time_point(); // Reset time_point
    entry.timestamp = ""; // Reset timestamp

    // Test: Source info with invalid line number
    entry.message = "Source test";
    entry.source_file = "test.cpp";
    entry.source_function = "testFunc";
    entry.source_line = 0; // Invalid line number
    assert(!entry.isValid());
    entry.source_line = -5; // Also invalid
    assert(!entry.isValid());

    entry.source_line = 100; // Valid line number
    assert(entry.isValid());

    std::cout << "testValidation passed" << std::endl;
}

void testEnvironmentMetadata()
{
    LogEntry entry = LogEntry::create(LogLevel::INFO, "Env Test")
                         .withHost("my-server")
                         .withApp("backend-service");

    assert(entry.host_name == "my-server");
    assert(entry.app_name == "backend-service");

    std::string json = entry.toJson();
    assert(json.find("\"host_name\": \"my-server\"") != std::string::npos);
    assert(json.find("\"app_name\": \"backend-service\"") != std::string::npos);

    auto deserialized_res = LogEntry::fromJson(json);
    assert(deserialized_res.has_value());
    const LogEntry &deserialized = *deserialized_res;

    assert(deserialized.host_name == "my-server");
    assert(deserialized.app_name == "backend-service");

    std::cout << "testEnvironmentMetadata passed" << std::endl;
}

void testSeverityValue()
{
    assert(LogEntry().withLevel(LogLevel::DEBUG).getSeverityValue() == 7);
    assert(LogEntry().withLevel(LogLevel::INFO).getSeverityValue() == 6);
    assert(LogEntry().withLevel(LogLevel::WARNING).getSeverityValue() == 4);
    assert(LogEntry().withLevel(LogLevel::ERROR).getSeverityValue() == 3);
    assert(LogEntry().withLevel(LogLevel::CRITICAL).getSeverityValue() == 2);
    assert(LogEntry().withLevel(LogLevel::UNKNOWN).getSeverityValue() == 0);
    std::cout << "testSeverityValue passed" << std::endl;
}

void testHasAttributeValue()
{
    LogEntry entry;
    entry.withAttribute("str_key", "hello");
    entry.withAttribute("int_key", static_cast<int64_t>(123LL));
    entry.withAttribute("bool_key", true);

    assert(entry.hasAttributeValue("str_key", std::string("hello")));
    assert(!entry.hasAttributeValue("str_key", std::string("world")));
    assert(entry.hasAttributeValue("int_key", int64_t{123}));
    assert(!entry.hasAttributeValue("int_key", int64_t{456}));
    assert(entry.hasAttributeValue("bool_key", true));
    assert(!entry.hasAttributeValue("bool_key", false));
    assert(!entry.hasAttributeValue("missing_key", std::string("any")));

    std::cout << "testHasAttributeValue passed" << std::endl;
}

void testJsonOptionsFieldFiltering() {
    std::cout << "Starting testJsonOptionsFieldFiltering..." << std::endl;

    LogEntry entry = LogEntry::create(LogLevel::INFO, "Filtered message")
                         .withAttribute("user", LogValue("alice"))
                         .withTag("security")
                         .withEventId("LOGIN_SUCCESS")
                         .withSource(); // Populate source fields

    // Scenario 1: Include Only `message` and `level`
    LogEntry::JsonOptions opts_include_message_level;
    opts_include_message_level.include_fields = {"message", "level"};
    std::string json_message_level = entry.toJson(opts_include_message_level);
    assert(json_message_level.find("\"message\"") != std::string::npos);
    assert(json_message_level.find("\"level\"") != std::string::npos);
    assert(json_message_level.find("\"timestamp\"") == std::string::npos);
    assert(json_message_level.find("\"user\"") == std::string::npos);
    assert(json_message_level.find("\"tags\"") == std::string::npos);
    assert(json_message_level.find("\"event_id\"") == std::string::npos);
    assert(json_message_level.find("\"source\"") == std::string::npos);

    // Scenario 2: Exclude `attributes` and `tags`
    LogEntry::JsonOptions opts_exclude_tags_attrs;
    opts_exclude_tags_attrs.exclude_fields = {"attributes", "tags"};
    std::string json_no_tags_attrs = entry.toJson(opts_exclude_tags_attrs);
    assert(json_no_tags_attrs.find("\"message\"") != std::string::npos);
    assert(json_no_tags_attrs.find("\"level\"") != std::string::npos);
    assert(json_no_tags_attrs.find("\"user\"") == std::string::npos); // user is an attribute
    assert(json_no_tags_attrs.find("\"tags\"") == std::string::npos);
    assert(json_no_tags_attrs.find("\"event_id\"") != std::string::npos);
    assert(json_no_tags_attrs.find("\"source\"") != std::string::npos);

    // Scenario 3: Precedence - include `tags`, but also exclude `tags`. Exclude should win.
    LogEntry::JsonOptions opts_precedence;
    opts_precedence.include_fields = {"message", "level", "tags"};
    opts_precedence.exclude_fields = {"tags"};
    std::string json_precedence = entry.toJson(opts_precedence);
    assert(json_precedence.find("\"message\"") != std::string::npos);
    assert(json_precedence.find("\"level\"") != std::string::npos);
    assert(json_precedence.find("\"tags\"") == std::string::npos); // Exclude wins

    // Scenario 4: Test with specific standard fields and new event_id
    LogEntry::JsonOptions opts_specific_fields;
    opts_specific_fields.include_fields = {"timestamp", "level", "message", "source", "event_id"};
    std::string json_specific = entry.toJson(opts_specific_fields);
    assert(json_specific.find("\"timestamp\"") != std::string::npos);
    assert(json_specific.find("\"level\"") != std::string::npos);
    assert(json_specific.find("\"message\"") != std::string::npos);
    assert(json_specific.find("\"source\"") != std::string::npos);
    assert(json_specific.find("\"event_id\"") != std::string::npos);
    assert(json_specific.find("\"user\"") == std::string::npos);
    assert(json_specific.find("\"tags\"") == std::string::npos);

    // Scenario 5: Exclude a non-existent field, verify no impact
    LogEntry::JsonOptions opts_exclude_nonexistent;
    opts_exclude_nonexistent.exclude_fields = {"nonExistentField"};
    std::string json_full = entry.toJson(opts_exclude_nonexistent); // Should be full JSON
    assert(json_full.find("\"message\"") != std::string::npos);
    assert(json_full.find("\"user\"") != std::string::npos);

    // Scenario 6: Test interaction with `exclude_empty = true`
    LogEntry empty_entry = LogEntry::create(LogLevel::INFO, "Empty entry");
    LogEntry::JsonOptions opts_exclude_empty;
    opts_exclude_empty.exclude_empty = true;
    std::string json_empty_excluded = empty_entry.toJson(opts_exclude_empty);
    assert(json_empty_excluded.find("\"attributes\"") == std::string::npos); // Should be excluded
    assert(json_empty_excluded.find("\"tags\"") == std::string::npos);       // Should be excluded

    std::cout << "testJsonOptionsFieldFiltering passed" << std::endl;
}

void testEventId() {
    std::cout << "Starting testEventId..." << std::endl;

    // Scenario 1: `withEventId` fluent method.
    LogEntry entry = LogEntry::create(LogLevel::INFO, "User login attempt")
                         .withEventId("USER_LOGIN_ATTEMPT");
    assert(entry.event_id == "USER_LOGIN_ATTEMPT");

    // Scenario 2: JSON serialization.
    std::string json_with_event_id = entry.toJson();
    assert(json_with_event_id.find("\"event_id\": \"USER_LOGIN_ATTEMPT\"") != std::string::npos);

    LogEntry entry_no_event_id = LogEntry::create(LogLevel::INFO, "Simple message");
    std::string json_no_event_id = entry_no_event_id.toJson();
    assert(json_no_event_id.find("\"event_id\"") == std::string::npos);

    // Scenario 3: JSON deserialization.
    std::string json_str_with_event = R"({"level":"INFO","message":"Test event","event_id":"TEST_EVENT_CODE"})";
    auto result_with_event = LogEntry::fromJson(json_str_with_event);
    assert(result_with_event.has_value());
    assert(result_with_event->event_id == "TEST_EVENT_CODE");

    std::string json_str_no_event = R"({"level":"INFO","message":"No event"})";
    auto result_no_event = LogEntry::fromJson(json_str_no_event);
    assert(result_no_event.has_value());
    assert(result_no_event->event_id.empty());

    // Scenario 4: `toMap` and `fromMap` round-trip.
    LogEntry original_entry = LogEntry::create(LogLevel::ERROR, "DB Error")
                                  .withEventId("DB_CONNECTION_FAILURE")
                                  .withAttribute("reason", "timeout");
    std::map<std::string, LogValue> map_repr = original_entry.toMap();
    LogEntry from_map_entry = LogEntry::fromMap(map_repr);
    assert(from_map_entry.event_id == "DB_CONNECTION_FAILURE");
    assert(from_map_entry.message == "DB Error");
    assert(from_map_entry.attributes.at("reason") == LogValue("timeout"));


    // Scenario 5: `operator==` and `operator<` behavior.
    LogEntry e1 = LogEntry::create(LogLevel::INFO, "Message");
    LogEntry e2 = LogEntry::create(LogLevel::INFO, "Message");
    assert(e1 == e2);

    e1.withEventId("EVENT_A");
    assert(e1 != e2);
    assert(e2 < e1); // Assuming "EVENT_A" comes after "" (empty string) in lexicographical order

    e2.withEventId("EVENT_B");
    assert(e1 != e2);
    assert(e1 < e2); // Assuming "EVENT_A" < "EVENT_B"

    LogEntry e3 = e1;
    assert(e1 == e3);

    // Scenario 6: `std::hash<LogEntry>` behavior.
    std::unordered_set<LogEntry> entry_set;
    entry_set.insert(e1);
    entry_set.insert(e2);
    assert(entry_set.size() == 2); // e1 and e2 should be distinct due to event_id

    LogEntry e4 = LogEntry::create(LogLevel::INFO, "Message").withEventId("EVENT_A");
    entry_set.insert(e4);
    assert(entry_set.size() == 2); // e4 is a duplicate of e1

    std::cout << "testEventId passed" << std::endl;
}

// Dummy global LogEntry to ensure LogContext::Scope is instantiated and compiled,
// as the primary creation path is via LogEntry::create().
// This helps catch compilation issues related to LogContext in isolation.
[[maybe_unused]] static LogContext::Scope globalDummyScope(
    {{"globalAttr", LogValue("globalVal")}}, {"globalTag"});


void testLogContextScope() {
    std::cout << "Starting testLogContextScope..." << std::endl;

    // Scenario 1: Basic context application.
    {
        LogContext::Scope scope1({{"req_id", LogValue("abc-123")}}, {"web"});
        LogEntry entry = LogEntry::create(LogLevel::INFO, "Request received");
        assert(entry.hasAttribute("req_id"));
        assert(entry.getAttributeAs<std::string>("req_id") == "abc-123");
        assert(entry.hasTag("web"));
    }
    // After scope1 exits, its context should be gone.
    LogEntry entry_after_scope1 = LogEntry::create(LogLevel::INFO, "Outside scope");
    assert(!entry_after_scope1.hasAttribute("req_id"));
    assert(!entry_after_scope1.hasTag("web"));

    // Scenario 2: Nested scopes - attribute overriding.
    {
        LogContext::Scope outer_scope({{"trace_level", LogValue(1LL)}, {"operation", LogValue("outer")}});
        LogEntry entry_outer = LogEntry::create(LogLevel::INFO, "Outer operation");
        assert(entry_outer.getAttributeAs<int64_t>("trace_level") == 1LL);
        assert(entry_outer.getAttributeAs<std::string>("operation") == "outer");

        {
            LogContext::Scope inner_scope({{"trace_level", LogValue(2LL)}, {"component", LogValue("inner")}});
            LogEntry entry_inner = LogEntry::create(LogLevel::INFO, "Inner operation");
            assert(entry_inner.getAttributeAs<int64_t>("trace_level") == 2LL); // Inner overrides outer
            assert(entry_inner.getAttributeAs<std::string>("operation") == "outer"); // Outer attribute still present
            assert(entry_inner.getAttributeAs<std::string>("component") == "inner");
        } // inner_scope ends

        LogEntry entry_after_inner = LogEntry::create(LogLevel::INFO, "Back in outer scope");
        assert(entry_after_inner.getAttributeAs<int64_t>("trace_level") == 1LL); // Outer restored
        assert(entry_after_inner.getAttributeAs<std::string>("operation") == "outer");
        assert(!entry_after_inner.hasAttribute("component")); // Inner attribute gone
    } // outer_scope ends

    // Scenario 3: Nested scopes - cumulative tags.
    {
        LogContext::Scope outer_scope_tags({}, {"database"});
        LogEntry entry_outer_tags = LogEntry::create(LogLevel::INFO, "DB access");
        assert(entry_outer_tags.hasTag("database"));
        assert(entry_outer_tags.getTags().size() == 1);

        {
            LogContext::Scope inner_scope_tags({}, {"query", "performance"});
            LogEntry entry_inner_tags = LogEntry::create(LogLevel::INFO, "Executing query");
            assert(entry_inner_tags.hasTag("database"));
            assert(entry_inner_tags.hasTag("query"));
            assert(entry_inner_tags.hasTag("performance"));
            assert(entry_inner_tags.getTags().size() == 3);
        } // inner_scope_tags ends

        LogEntry entry_after_inner_tags = LogEntry::create(LogLevel::INFO, "After query");
        assert(entry_after_inner_tags.hasTag("database"));
        assert(!entry_after_inner_tags.hasTag("query"));
        assert(!entry_after_inner_tags.hasTag("performance"));
        assert(entry_after_inner_tags.getTags().size() == 1);
    }

    // Scenario 4: Scope without attributes/tags.
    {
        LogContext::Scope empty_scope;
        LogEntry entry = LogEntry::create(LogLevel::INFO, "Empty scope test");
        assert(entry.attributes.empty());
        assert(entry.tags.empty());
    }

    // Scenario 5: `LogEntry` created outside any active scope.
    LogEntry global_entry = LogEntry::create(LogLevel::INFO, "Global entry test");
    assert(global_entry.attributes.empty());
    assert(global_entry.tags.empty());

    // Scenario 6: Thread-locality.
    // This requires a separate thread for testing, which complicates the assert flow.
    // We'll simulate by ensuring that a context set in one block doesn't leak.
    // The previous tests already implicitly cover this by showing context disappears after scope.
    // For a more robust thread-locality test, one would launch actual threads.
    // For now, we rely on the thread_local keyword and scope exit.

    std::cout << "testLogContextScope passed" << std::endl;
}

void testNestedData()
{
    LogEntry entry = LogEntry::create(LogLevel::INFO, "Nested data test");

    LogList list = {"a", 1LL, true};
    LogObject obj = {{"k1", "v1"}, {"k2", 2.2}};

    entry.withAttribute("list", list);
    entry.withAttribute("obj", obj);

    assert(entry.attributes["list"].isList());
    assert(entry.attributes["obj"].isObject());

    auto opt_list_ptr = entry.attributes["list"].asList();
    assert(opt_list_ptr.has_value());
    const LogList &l = *opt_list_ptr.value();
    assert(l.size() == 3);
    assert(std::get<std::string>(l[0]) == "a");

    auto opt_obj_ptr = entry.attributes["obj"].asObject();
    assert(opt_obj_ptr.has_value());
    const LogObject &o = *opt_obj_ptr.value();
    assert(o.at("k1") == LogValue("v1"));

    // JSON Round Trip for nested data
    std::string json = entry.toJson();
    auto result = LogEntry::fromJson(json);
    assert(result.has_value());
    assert(result->attributes["list"].isList());
    assert(result->attributes["obj"].isObject());
    assert(result->attributes["list"].asList().size() == 3);
    assert(result->attributes["obj"].asObject().at("k1") == LogValue("v1"));

    std::cout << "testNestedData passed" << std::endl;
}

void testWithMetadata()
{
    LogEntry entry;
    entry.level = LogLevel::INFO;
    entry.message = "Metadata test";

    assert(entry.process_id == 0);
    assert(entry.host_name.empty());

    entry.withMetadata();

    assert(entry.process_id != 0);
    assert(!entry.host_name.empty());
    assert(!entry.thread_id.empty());
    assert(!entry.timestamp.empty());

    std::cout << "testWithMetadata passed" << std::endl;
}

void testJsonOptionsIteration1()
{
    std::cout << "Starting testJsonOptionsIteration1..." << std::endl;
    LogEntry entry = LogEntry::create(LogLevel::INFO, "Options test");
    entry.withAttribute("a", static_cast<int64_t>(1LL));
    entry.withTag("t1");

    // Existing tests for exclude_empty and precision
    LogEntry::JsonOptions opts;
    opts.exclude_empty = true;

    // Test with content (should be there)
    std::string json = entry.toJson(opts);
    assert(json.find("\"attributes\"") != std::string::npos);
    assert(json.find("\"tags\"") != std::string::npos);

    // Test with empty (should be missing)
    entry.clearAttributes();
    entry.tags.clear();
    json = entry.toJson(opts);
    assert(json.find("\"attributes\"") == std::string::npos);
    assert(json.find("\"tags\"") == std::string::npos);

    // Test Precision for ISO8601
    entry.time_point = std::chrono::system_clock::now(); // Use current time for more robust testing
    opts.timestamp_format = LogEntry::TimestampFormat::ISO8601;

        opts.precision = LogEntry::JsonOptions::Precision::Seconds;

        json = entry.toJson(opts);

        std::cout << "DEBUG: JSON for Precision::Seconds: " << json << std::endl; // Added debug print

    

        // Extract the timestamp value from the JSON

        size_t ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        size_t ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        size_t ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        std::string timestamp_str = json.substr(ts_start, ts_end - ts_start);

    

        std::cout << "DEBUG: Extracted timestamp_str: " << timestamp_str << std::endl;

        std::cout << "DEBUG: timestamp_str.find(\".\") result: " << timestamp_str.find(".") << std::endl;

    

        assert(timestamp_str.find("T") != std::string::npos && 

               timestamp_str.find("Z") != std::string::npos && 

               timestamp_str.find(".") == std::string::npos);

    

        opts.precision = LogEntry::JsonOptions::Precision::Millis;

        json = entry.toJson(opts);

        // Extract timestamp string for millis precision

        ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        timestamp_str = json.substr(ts_start, ts_end - ts_start);

        

        assert(timestamp_str.find("T") != std::string::npos && timestamp_str.find("Z") != std::string::npos);

        // Check for 3 digits after the dot (before Z)

        auto dot_pos_millis = timestamp_str.find('.');

        auto Z_pos_millis = timestamp_str.find('Z');

        assert(Z_pos_millis > dot_pos_millis);

        assert((Z_pos_millis - dot_pos_millis - 1) == 3);

    

        opts.precision = LogEntry::JsonOptions::Precision::Micros;

        json = entry.toJson(opts);

        // Extract timestamp string for micros precision

        ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        timestamp_str = json.substr(ts_start, ts_end - ts_start);

    

        assert(timestamp_str.find("T") != std::string::npos && timestamp_str.find("Z") != std::string::npos);

        // Check for 6 digits after the dot (before Z)

        auto dot_pos_micros = timestamp_str.find('.');

        auto Z_pos_micros = timestamp_str.find('Z');

        assert(Z_pos_micros > dot_pos_micros);

        assert((Z_pos_micros - dot_pos_micros - 1) == 6);

        

        opts.precision = LogEntry::JsonOptions::Precision::Nanos;

        json = entry.toJson(opts);

        // Extract timestamp string for nanos precision

        ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        timestamp_str = json.substr(ts_start, ts_end - ts_start);

    

        assert(timestamp_str.find("T") != std::string::npos && timestamp_str.find("Z") != std::string::npos);

        // Check for 9 digits after the dot (before Z)

        auto dot_pos_nanos = timestamp_str.find('.');

        auto Z_pos_nanos = timestamp_str.find('Z');

        assert(Z_pos_nanos > dot_pos_nanos);

        assert((Z_pos_nanos - dot_pos_nanos - 1) == 9);

    

    

        // Test Timezone

        entry.time_point = std::chrono::system_clock::now(); // Use current time for more robust testing

        opts.timestamp_format = LogEntry::TimestampFormat::Default; // Use default string format

        opts.precision = LogEntry::JsonOptions::Precision::Millis; // Default precision for default format string

    

        opts.timezone = LogEntry::JsonOptions::Timezone::UTC;

        json = entry.toJson(opts);

        // Extract timestamp string for UTC default format

        ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        timestamp_str = json.substr(ts_start, ts_end - ts_start);

    

        // Check for standard format with millis, and that it's not local

        // (This is a weak check, as system could be UTC)

        // For now, simply verify the format structure.

        assert(timestamp_str.find(":") != std::string::npos && timestamp_str.find(".") != std::string::npos);

        // std::cout << "UTC NOW: " << json << std::endl;

    

        opts.timezone = LogEntry::JsonOptions::Timezone::Local;

        json = entry.toJson(opts);

        // Extract timestamp string for Local default format

        ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        timestamp_str = json.substr(ts_start, ts_end - ts_start);

    

        // Check for standard format with millis, and that it's not UTC (weak check)

        assert(timestamp_str.find(":") != std::string::npos && timestamp_str.find(".") != std::string::npos);

        // std::cout << "LOCAL NOW: " << json << std::endl;

    

        // Test custom_timestamp_format

        opts.timezone = LogEntry::JsonOptions::Timezone::UTC; // Set to UTC for deterministic format for custom

        opts.custom_timestamp_format = "%Y/%m/%d %H:%M:%S - Custom";

        json = entry.toJson(opts);

        // Extract timestamp string for custom format

        ts_key_pos = json.find("\"timestamp\": \"");

        assert(ts_key_pos != std::string::npos);

        ts_start = ts_key_pos + strlen("\"timestamp\": \"");

        ts_end = json.find("\"", ts_start);

        assert(ts_end != std::string::npos);

        timestamp_str = json.substr(ts_start, ts_end - ts_start);

        

        // The specific time will change, but the format should be consistent

        assert(timestamp_str.find("- Custom") != std::string::npos);

        assert(timestamp_str.find("/") != std::string::npos); // Check for / as per custom format

        opts.custom_timestamp_format = std::nullopt; // Reset for next test

    std::cout << "testJsonOptionsIteration1 passed" << std::endl;
}

void testGlobalConfigAndProviders() {
    std::cout << "Starting testGlobalConfigAndProviders..." << std::endl;

    // Test default JsonOptions
    LogEntry entry = LogEntry::create(LogLevel::INFO, "Global config test");
    std::string default_json = entry.toJson(); // Should use default options
    assert(default_json.find("\"timestamp\":") != std::string::npos);
    assert(default_json.find("0x") != std::string::npos); // Default binary encoding is Hex

    // Change default JsonOptions
    LogEntry::JsonOptions custom_default_opts;
    custom_default_opts.pretty = true;
    custom_default_opts.binary_encoding = LogEntry::JsonOptions::BinaryEncoding::Base64;
    custom_default_opts.timestamp_format = LogEntry::JsonOptions::TimestampFormat::ISO8601;
    custom_default_opts.precision = LogEntry::JsonOptions::Precision::Nanos;
    custom_default_opts.timezone = LogEntry::JsonOptions::Timezone::UTC;

    LogEntry::setDefaultJsonOptions(custom_default_opts);
    assert(LogEntry::getDefaultJsonOptions().pretty == true);
    assert(LogEntry::getDefaultJsonOptions().binary_encoding == LogEntry::JsonOptions::BinaryEncoding::Base64);

    // Verify toJson() without args uses new defaults
    std::string custom_default_json = entry.toJson();
    assert(custom_default_json.find("\n") != std::string::npos); // Pretty printing
    assert(custom_default_json.find("Z\"") != std::string::npos); // ISO8601 UTC
    assert(custom_default_json.find("0x") == std::string::npos); // No Hex
    // Need a binary attribute to properly test base64
    std::vector<uint8_t> binary_data = {0x01, 0x02, 0x03};
    entry.withAttribute("binary_test", binary_data);
    custom_default_json = entry.toJson();
    assert(custom_default_json.find("\"binary_test\": \"AQID\"") != std::string::npos); // Base64 encoding

    // Test metadata providers
    std::string test_host = "custom-host";
    std::string test_app = "custom-app";

    LogEntry::setHostNameProvider([&]() { return test_host; });
    LogEntry::setAppNameProvider([&]() { return test_app; });

    LogEntry provider_entry;
    provider_entry.withMetadata(); // Should use providers

    assert(provider_entry.host_name == test_host);
    assert(provider_entry.app_name == test_app);

    // Reset providers
    LogEntry::resetHostNameProvider();
    LogEntry::resetAppNameProvider();

    LogEntry reset_entry;
    reset_entry.withMetadata(); // Should revert to default behavior
    assert(reset_entry.host_name == LogEntry::currentHostName()); // Default hostname
    assert(reset_entry.app_name.empty()); // Default empty app name

    std::cout << "testGlobalConfigAndProviders passed" << std::endl;
}

void testNumericConversion()
{
    LogEntry entry;
    entry.withAttribute("int_val", static_cast<int64_t>(42LL));
    entry.withAttribute("uint_val", static_cast<uint64_t>(100ULL));

    // Exact type
    assert(entry.getAttributeAs<int64_t>("int_val") == 42);

    // Conversion to double
    auto d_val = entry.getAttributeAs<double>("int_val");
    assert(d_val.has_value());
    assert(*d_val == 42.0);

    auto d_val2 = entry.getAttributeAs<double>("uint_val");
    assert(d_val2.has_value());
    assert(*d_val2 == 100.0);

    std::cout << "testNumericConversion passed" << std::endl;
}

void testNewLogValueTypes()
{
    std::cout << "Starting testNewLogValueTypes..." << std::endl;
    LogEntry entry = LogEntry::create(LogLevel::INFO, "New types test");

    // Test std::vector<uint8_t>
    std::vector<uint8_t> binary_data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67};
    entry.withAttribute("binary_payload", binary_data);

    // Test std::chrono::nanoseconds
    std::chrono::nanoseconds duration = std::chrono::nanoseconds(1234567890123); // 1.234567890123 seconds
    entry.withAttribute("duration_ns", duration);

    // Verify retrieval
    auto retrieved_binary = entry.getAttributeAs<std::vector<uint8_t>>("binary_payload");
    assert(retrieved_binary.has_value());
    assert(*retrieved_binary == binary_data);

    auto retrieved_duration = entry.getAttributeAs<std::chrono::nanoseconds>("duration_ns");
    assert(retrieved_duration.has_value());
    assert(*retrieved_duration == duration);

    // Test JSON serialization (default Hex)
    std::string json_default = entry.toJson();
    assert(json_default.find("\"binary_payload\": \"0xdeadbeef01234567\"") != std::string::npos);
    assert(json_default.find("\"duration_ns\": 1234567890123") != std::string::npos);

    // Test JSON serialization (Base64)
    LogEntry::JsonOptions base64_opts;
    base64_opts.binary_encoding = LogEntry::JsonOptions::BinaryEncoding::Base64;
    std::string json_base64 = entry.toJson(base64_opts);
    std::cout << "DEBUG: JSON Base64: " << json_base64 << std::endl;
    assert(json_base64.find("\"binary_payload\": \"3q2+7wEjRWc=\"") != std::string::npos); // Base64 for DEAD BEEF 01 23 45 67
    assert(json_base64.find("\"duration_ns\": 1234567890123") != std::string::npos);


    std::cout << "testNewLogValueTypes passed" << std::endl;
}

void testLogValueEnhancements() {
    std::cout << "Starting testLogValueEnhancements..." << std::endl;

    LogValue val_monostate;
    LogValue val_bool(true);
    LogValue val_int64(123LL);
    LogValue val_uint64(456ULL);
    LogValue val_double(7.89);
    LogValue val_string("hello");
    std::vector<uint8_t> binary_data = {0x01, 0x02, 0x03};
    LogValue val_binary(binary_data);
    LogValue val_duration(std::chrono::nanoseconds(100));
    LogList list_val = {val_int64, val_string};
    LogValue val_list(list_val);
    LogObject obj_val = {{"key", val_double}};
    LogValue val_object(obj_val);

    // Test type() and is(ValueType)
    assert(val_monostate.type() == ValueType::Monostate);
    assert(val_monostate.is(ValueType::Monostate));
    assert(val_monostate.isNull());

    assert(val_bool.type() == ValueType::Bool);
    assert(val_bool.is(ValueType::Bool));
    assert(!val_bool.isNull());

    assert(val_int64.type() == ValueType::Int64);
    assert(val_int64.is(ValueType::Int64));

    assert(val_uint64.type() == ValueType::UInt64);
    assert(val_uint64.is(ValueType::UInt64));

    assert(val_double.type() == ValueType::Double);
    assert(val_double.is(ValueType::Double));

    assert(val_string.type() == ValueType::String);
    assert(val_string.is(ValueType::String));

    assert(val_binary.type() == ValueType::Binary);
    assert(val_binary.is(ValueType::Binary));

    assert(val_duration.type() == ValueType::Nanoseconds);
    assert(val_duration.is(ValueType::Nanoseconds));

    assert(val_list.type() == ValueType::List);
    assert(val_list.is(ValueType::List));

    assert(val_object.type() == ValueType::Object);
    assert(val_object.is(ValueType::Object));

    // Test is<T>()
    assert(val_bool.is<bool>());
    assert(val_int64.is<int64_t>());
    assert(!val_int64.is<uint64_t>()); // Should not be both

    // Test get<T>()
    assert(val_bool.get<bool>() == true);
    assert(val_int64.get<int64_t>() == 123LL);
    assert(val_string.get<std::string>() == "hello");

    // Test get_if<T>()
    assert(*val_double.get_if<double>() == 7.89);
    assert(val_binary.get_if<std::vector<uint8_t>>() != nullptr);
    assert(val_monostate.get_if<bool>() == nullptr);
    assert(val_list.get_if<std::shared_ptr<LogList>>() != nullptr);
    assert(val_object.get_if<std::shared_ptr<LogObject>>() != nullptr);


    // Test toString()
    assert(val_monostate.toString() == "null");
    assert(val_bool.toString() == "true");
    assert(val_int64.toString() == "123");
    
    // For doubles, due to precision output, comparing with a small epsilon or specific format
    std::stringstream ss_double;
    ss_double << std::fixed << std::setprecision(std::numeric_limits<double>::max_digits10) << 7.89;
    assert(val_double.toString() == ss_double.str());

    assert(val_string.toString() == "\"hello\"");
    assert(val_binary.toString(LogEntry::JsonOptions::BinaryEncoding::Hex) == "\"0x010203\"");
    assert(val_binary.toString(LogEntry::JsonOptions::BinaryEncoding::Base64) == "\"AQID\"");
    assert(val_duration.toString() == "100ns");
    assert(val_list.toString() == "[123,\"hello\"]");
    
    // Object string representation order might not be guaranteed by map, but key/value should be there
    std::string obj_str = val_object.toString();
    assert(obj_str.find("\"key\":") != std::string::npos);
    assert(obj_str.find(ss_double.str()) != std::string::npos); // Value of "key"

    std::cout << "testLogValueEnhancements passed" << std::endl;
}

void testErgonomicGetters()
{
    std::cout << "Starting testErgonomicGetters..." << std::endl;

    LogEntry entry;

    entry.withAttribute("i64_val", int64_t{100});
    entry.withAttribute("u64_val", uint64_t{200});
    entry.withAttribute("double_val", 300.5);
    entry.withAttribute("duration_val", std::chrono::nanoseconds(5000000000LL)); // 5 seconds
    entry.withAttribute("bool_val", true);
    entry.withAttribute("string_val", "hello");

    // Test getAsDouble
    assert(entry.getAsDouble("i64_val").value_or(0.0) == 100.0);
    assert(entry.getAsDouble("u64_val").value_or(0.0) == 200.0);
    assert(entry.getAsDouble("double_val").value_or(0.0) == 300.5);
    assert(entry.getAsDouble("duration_val").value_or(0.0) == 5.0); // 5 billion ns = 5s
    assert(!entry.getAsDouble("bool_val").has_value());
    assert(!entry.getAsDouble("string_val").has_value());
    assert(!entry.getAsDouble("non_existent").has_value());

    // Test getAsInt
    assert(entry.getAsInt("i64_val").value_or(0LL) == 100LL);
    assert(entry.getAsInt("u64_val").value_or(0LL) == 200LL);
    assert(entry.getAsInt("double_val").value_or(0LL) == 300LL); // Precision loss
    assert(entry.getAsInt("duration_val").value_or(0LL) == 5000000000LL); // ns count
    assert(!entry.getAsInt("bool_val").has_value());
    assert(!entry.getAsInt("string_val").has_value());
    assert(!entry.getAsInt("non_existent").has_value());

    // Test getAsInt edge cases for uint64_t and double
    entry.withAttribute("large_u64", std::numeric_limits<uint64_t>::max());
    assert(!entry.getAsInt("large_u64").has_value()); // Too large for int64_t

    entry.withAttribute("large_double", static_cast<double>(std::numeric_limits<int64_t>::max()) + 1.0e10); // Make it clearly larger
    assert(!entry.getAsInt("large_double").has_value()); // Too large for int64_t

    entry.withAttribute("small_double", static_cast<double>(std::numeric_limits<int64_t>::min()) - 1.0e10); // Make it clearly smaller
    assert(!entry.getAsInt("small_double").has_value()); // Too small for int64_t

    std::cout << "testErgonomicGetters passed" << std::endl;
}

void testMetadataExtensions()
{
    std::cout << "Starting testMetadataExtensions..." << std::endl;
    LogEntry entry = LogEntry::create(LogLevel::INFO, "System/Memory test");
    entry.withSystemLoad();
    entry.withMemoryUsage();

    // Check for presence, exact values are system-dependent
#if defined(__linux__)
    assert(entry.hasAttribute("system_load_1m"));
    assert(entry.hasAttribute("system_load_5m"));
    assert(entry.hasAttribute("system_load_15m"));
    assert(entry.hasAttribute("memory_rss_bytes"));
#elif defined(_WIN32) || defined(_WIN64)
    assert(entry.hasAttribute("system_load")); // Should be "unsupported"
    assert(entry.getAttributeAs<std::string>("system_load").value_or("") == "unsupported");
    assert(entry.hasAttribute("memory_rss_bytes"));
    assert(entry.hasAttribute("memory_peak_rss_bytes"));
#else
    assert(entry.hasAttribute("system_load")); // Should be "unsupported"
    assert(entry.getAttributeAs<std::string>("system_load").value_or("") == "unsupported");
#endif

    std::string json = entry.toJson();
    std::cout << "Metadata Extensions JSON: " << json << std::endl; // For visual inspection

    std::cout << "testMetadataExtensions passed" << std::endl;
}


void testIteration1Features()
{
    std::cout << "Starting testIteration1Features..." << std::endl;
    // 1. Process ID
    uint64_t pid = LogEntry::currentProcessId();
    assert(pid != 0);

    auto entry = LogEntry::create(LogLevel::INFO, "PID Test");
    assert(entry.process_id == pid);

    entry.withProcessId(9999);
    assert(entry.process_id == 9999);

    std::string json = entry.toJson();
    assert(json.find("\"process_id\": 9999") != std::string::npos);

    auto deserialized = LogEntry::fromJson(json);
    assert(deserialized.has_value());
    assert(deserialized->process_id == 9999);

    // 2. Attribute Management
    entry.clearAttributes();
    entry.withAttributes({{"attr1", "val1"},
                          {"attr2", 100LL}});
    std::map<std::string, LogValue> more_attrs = {
        {"attr3", true},
        {"attr4", std::monostate{}} // Null value
    };
    entry.withAttributes(more_attrs);

    assert(entry.getAttributeAsString("attr1") == "val1");
    assert(entry.getAttributeAs<int64_t>("attr2") == 100);
    assert(entry.getAttributeAs<bool>("attr3") == true);
    assert(entry.hasAttribute("attr4"));
    // Note: getAttributeAs<std::string>("attr4") returns nullopt for std::monostate
    assert(!entry.getAttributeAs<std::string>("attr4").has_value());

    entry.removeAttribute("attr1");
    assert(!entry.hasAttribute("attr1"));

    LogEntry otherEntry;
    otherEntry.withAttribute("attr5", 5.5);
    entry.mergeAttributes(otherEntry);
    assert(entry.getAttributeAs<double>("attr5") == 5.5);

    // JSON Null check
    std::string nullJson = entry.toJson();
    assert(nullJson.find("\"attr4\": null") != std::string::npos);

    std::cout << "testIteration1Features passed" << std::endl;
}

void testHashability() {
    std::cout << "Starting testHashability..." << std::endl;

    // Test LogValue hashability
    std::unordered_set<LogValue> value_set;
    LogValue lv1("hello");
    LogValue lv2(123LL);
    LogValue lv3("hello"); // Same as lv1
    LogValue lv4(std::chrono::nanoseconds(100));

    value_set.insert(lv1);
    value_set.insert(lv2);
    value_set.insert(lv3); // Should not insert a duplicate
    value_set.insert(lv4);

    assert(value_set.size() == 3);
    assert(value_set.count(lv1) == 1);
    assert(value_set.count(lv2) == 1);
    assert(value_set.count(lv3) == 1); // Found because it's equal to lv1
    assert(value_set.count(lv4) == 1);
    assert(value_set.count(LogValue(124LL)) == 0);

    // Test LogEntry hashability
    std::unordered_set<LogEntry> entry_set;
    LogEntry e1 = LogEntry::create(LogLevel::INFO, "message1");
    e1.withTag("tagA").withAttribute("attr1", "val1");

    LogEntry e2 = LogEntry::create(LogLevel::INFO, "message2");
    e2.withTag("tagB").withAttribute("attr2", static_cast<int64_t>(123LL));

    LogEntry e3 = e1; // Should be equal to e1

    entry_set.insert(e1);
    entry_set.insert(e2);
    entry_set.insert(e3); // Should not insert a duplicate

    assert(entry_set.size() == 2);
    assert(entry_set.count(e1) == 1);
    assert(entry_set.count(e2) == 1);
    assert(entry_set.count(e3) == 1); // Found because it's equal to e1

    // Test for different hash for different content
    LogEntry e4 = LogEntry::create(LogLevel::INFO, "message1");
    e4.withTag("tagA").withAttribute("attr1", "val2"); // Different attribute value
    assert(entry_set.count(e4) == 0); // Should not find e4 as it's different

    // Test with unordered_map
    std::unordered_map<LogEntry, int> entry_map;
    entry_map[e1] = 1;
    entry_map[e2] = 2;
    entry_map[e3] = 3; // Updates value for e1

    assert(entry_map.size() == 2);
    assert(entry_map.at(e1) == 3); // Value for e1 updated by e3
    assert(entry_map.at(e2) == 2);

    std::cout << "testHashability passed" << std::endl;
}

int main()
{

    testLevelParsing();    testLevelToString();
    testTimeParsing();
    testComparison();
    testFormatting();
    testStructuredLogging();
    testFluentApi();
    testStreamOperator();
    testSeverityChecks();
    testTracing();
    testJsonDeserialization();
    testCloning();
    testTagManagement(); // New test
    testToMapRoundTrip();
    testJsonFormatDetection();
    testTimestampUtilities(); // New test
    testValidation();
    testEnvironmentMetadata();
    testSeverityValue();
    testHasAttributeValue();
    testJsonOptionsFieldFiltering(); // New test
    testEventId(); // New test

    testIteration1Features();
    testNestedData();
    testWithMetadata();
    testJsonOptionsIteration1();
    testGlobalConfigAndProviders(); // New test
    testNumericConversion();

    // New tests for Iteration 1
    testNewLogValueTypes();
    testLogValueEnhancements(); // New test
    testErgonomicGetters();
    testMetadataExtensions();
    testHashability(); // New test
    testLogContextScope(); // New test

    std::cout << "All LogEntry tests passed!" << std::endl;

    return 0;
}
