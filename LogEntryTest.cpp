#include "LogEntry.h"
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

int main() {
    testLevelParsing();
    testLevelToString();
    testTimeParsing();
    testComparison();
    testFormatting();
    testStructuredLogging();
    testFluentApi();
    testStreamOperator();
    std::cout << "All LogEntry tests passed!" << std::endl;
    return 0;
}