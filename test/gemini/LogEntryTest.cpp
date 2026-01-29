#include <gemini/LogEntry.h>
#include <cassert>
#include <iostream>
#include <format>
#include <sstream>

void testLevelParsing() {
    assert(LogEntry::parseLevel("DEBUG") == LogLevel::DEBUG);
    assert(LogEntry::parseLevel("dbg") == LogLevel::DEBUG);
    assert(LogEntry::parseLevel("INFO") == LogLevel::INFO);
    assert(LogEntry::parseLevel("error") == LogLevel::ERROR);
    assert(LogEntry::parseLevel("UNKNOWN_JUNK") == LogLevel::UNKNOWN);
    
    // Test backward compatibility
    assert(parseLogLevel("DEBUG") == LogLevel::DEBUG);
    
    std::cout << "testLevelParsing passed" << std::endl;
}

void testLevelToString() {
    assert(LogEntry::levelToString(LogLevel::DEBUG) == "DEBUG");
    assert(LogEntry::levelToString(LogLevel::INFO) == "INFO");
    assert(LogEntry::levelToString(LogLevel::UNKNOWN) == "UNKNOWN");
    std::cout << "testLevelToString passed" << std::endl;
}

void testTimeParsing() {
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00";
    assert(entry.parseTime());
    // Checking exact value might be tricky due to timezone, but we can check round trip if we assume local time.
    std::string generated = entry.generatedTimestampString(false); // No fractional
    assert(generated == "2023-10-27 10:00:00");

    // Test with fractional
    entry.timestamp = "2023-10-27 10:00:00.123";
    assert(entry.parseTime());
    generated = entry.generatedTimestampString(true);
    if (generated != "2023-10-27 10:00:00.123") {
        std::cout << "Generated: '" << generated << "', Expected: '2023-10-27 10:00:00.123'" << std::endl;
    }
    assert(generated == "2023-10-27 10:00:00.123");
    
    // Test invalid time
    entry.timestamp = "invalid";
    assert(!entry.parseTime());
    
    std::cout << "testTimeParsing passed" << std::endl;
}

void testComparison() {
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

void testFormatting() {
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00.000";
    entry.level = LogLevel::INFO;
    entry.message = "System started";
    
    std::string formatted = std::format("{}", entry);
    // Expected: [2023-10-27 10:00:00.000] [INFO] System started
    assert(formatted == "[2023-10-27 10:00:00.000] [INFO] System started");
    std::cout << "testFormatting passed" << std::endl;
}

void testStructuredLogging() {
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
    entry.setAttribute("score", 95.5); // double
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
    try {
        throw std::runtime_error("test exception");
    } catch (const std::exception& e) {
        e_entry.withException(e);
    }
    assert(e_entry.getAttributeAs<std::string>("exception_message") == "test exception");
    
    std::cout << "testStructuredLogging passed" << std::endl;
}

void testFluentApi() {
    auto entry = LogEntry::create(LogLevel::WARNING, "Something happened")
        .withAttribute("error_code", 404LL)
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

void testStreamOperator() {
    LogEntry entry;
    entry.timestamp = "2023-10-27 10:00:00";
    entry.level = LogLevel::ERROR;
    entry.message = "Stream test";
    
    std::ostringstream oss;
    oss << entry;
    assert(oss.str() == "[2023-10-27 10:00:00] [ERROR] Stream test");
    
        std::cout << "testStreamOperator passed" << std::endl;
    
    }
    
    
    
    void testSeverityChecks() {
    
        assert(isAtLeast(LogLevel::ERROR, LogLevel::WARNING));
    
        assert(isAtLeast(LogLevel::WARNING, LogLevel::WARNING));
    
        assert(!isAtLeast(LogLevel::INFO, LogLevel::WARNING));
    
        
    
        assert(isError(LogLevel::ERROR));
    
        assert(isError(LogLevel::CRITICAL));
    
        assert(!isError(LogLevel::WARNING));
    
        assert(!isError(LogLevel::INFO));
    
        
    
        std::cout << "testSeverityChecks passed" << std::endl;
    
    }
    
    
    
    void testTracing() {
    
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
    
    
    
    void testJsonDeserialization() {
    
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
    
        if (!result) {
    
            std::cout << "JSON parsing failed: " << result.error() << std::endl;
    
        }
    
        assert(result.has_value());
    
        
    
        const LogEntry& entry = *result;
    
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
    
    
    
    void testCloning() {
        LogEntry entry = LogEntry::create(LogLevel::INFO, "Original");
        entry.withTag("original_tag");
        
        LogEntry clone = entry.clonedWithTag("new_tag");
        assert(clone.hasTag("original_tag"));
        assert(clone.hasTag("new_tag"));
        assert(!entry.hasTag("new_tag")); // Original should be unmodified
        
        std::cout << "testCloning passed" << std::endl;
    }
    
    void testIteration1Features() {
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
        entry.withAttributes({
            {"attr1", "val1"},
            {"attr2", 100LL}
        });
        std::map<std::string, LogValue> more_attrs = {
            {"attr3", true},
            {"attr4", std::monostate{}} // Null value
        };
        entry.withAttributes(more_attrs);

        assert(entry.getAttributeAsString("attr1") == "val1");
        assert(entry.getAttributeAs<int64_t>("attr2") == 100);
        assert(entry.getAttributeAs<bool>("attr3") == true);
        assert(entry.hasAttribute("attr4"));
        assert(!entry.getAttributeAs<std::string>("attr4").has_value()); // Should be empty/nullopt

        entry.removeAttribute("attr1");
        assert(!entry.hasAttribute("attr1"));

        LogEntry otherEntry;
        otherEntry.withAttribute("attr5", 5.5);
        entry.mergeAttributes(otherEntry);
        assert(entry.getAttributeAs<double>("attr5") == 5.5);

        // JSON Null check
        std::string nullJson = entry.toJson();
        assert(nullJson.find("\"attr4\": null") != std::string::npos);

        // 3. JSON Timestamp Formats
        entry.withTimestamp(std::chrono::system_clock::from_time_t(1698400800)); // 2023-10-27 10:00:00 UTC approximately
        
        LogEntry::JsonOptions opts;
        opts.timestamp_format = LogEntry::TimestampFormat::ISO8601;
        std::string isoJson = entry.toJson(opts);
        // Expect roughly "2023-10-27T...Z"
        assert(isoJson.find("T") != std::string::npos); 
        assert(isoJson.find("Z") != std::string::npos);

        opts.timestamp_format = LogEntry::TimestampFormat::UnixMillis;
        std::string millisJson = entry.toJson(opts);
        // Expect a number, not a quote-wrapped string
        assert(millisJson.find("\"timestamp\": 16984") != std::string::npos); 

        // 4. Factory fromMap
        std::map<std::string, LogValue> mapData = {
            {"level", "ERROR"},
            {"message", "Map created"},
            {"process_id", 12345LL},
            {"custom_attr", "custom_val"}
        };
        auto mapEntry = LogEntry::fromMap(mapData);
        assert(mapEntry.level == LogLevel::ERROR);
        assert(mapEntry.message == "Map created");
        assert(mapEntry.process_id == 12345);
        assert(mapEntry.getAttributeAsString("custom_attr") == "custom_val");

        std::cout << "testIteration1Features passed" << std::endl;
    }

    int main() {
    
        testLevelParsing();
    
        testLevelToString();
    
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

        testIteration1Features();
    
        std::cout << "All LogEntry tests passed!" << std::endl;
    
        return 0;
    
    }
    
    