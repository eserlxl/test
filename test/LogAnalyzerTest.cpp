#include "LogAnalyzer.h"
#include "LogEntry.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>

namespace fs = std::filesystem;

class LogAnalyzerTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir = fs::temp_directory_path() / "LogAnalyzerTestDir";
        fs::create_directories(tempDir);
        testLogFile = tempDir / "test.log";
        createStandardLogFile(testLogFile);
    }

    void TearDown() override {
        fs::remove_all(tempDir);
    }

    void createStandardLogFile(const fs::path& path) {
        std::ofstream file(path);
        file << "2023-10-27 10:00:00 [INFO] System started\n";
        file << "2023-10-27 10:00:05 [DEBUG] Debugging initialization\n";
        file << "2023-10-27 10:01:00 [ERROR] Connection failed\n";
        file << "2023-10-27 10:02:00 [WARNING] Retrying connection\n";
        file << "2023-10-27 10:03:00 [CRITICAL] System crash imminent\n";
    }

    void createCustomLogFile(const fs::path& path) {
        std::ofstream file(path);
        // Format: [THREAD_ID] {TIMESTAMP} <LEVEL> (SOURCE_FILE:LINE) Message
        file << "[T1] {2023-10-27 12:00:00} <INFO> (main.cpp:10) Started\n";
        file << "[T2] {2023-10-27 12:01:00} <ERROR> (network.cpp:42) Timeout\n";
    }

    fs::path tempDir;
    fs::path testLogFile;
};

// --- Suite 1: LogLoadingTest ---

TEST_F(LogAnalyzerTest, BasicLoad) {
    LogAnalyzer analyzer;
    auto result = analyzer.loadFile(testLogFile);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(analyzer.getEntries().size(), 5);
}

TEST_F(LogAnalyzerTest, LoadNonExistentFile) {
    LogAnalyzer analyzer;
    auto result = analyzer.loadFile("non_existent.log");
    EXPECT_FALSE(result.has_value());
}

TEST_F(LogAnalyzerTest, EmptyFile) {
    fs::path emptyFile = tempDir / "empty.log";
    { std::ofstream f(emptyFile); }
    
    LogAnalyzer analyzer;
    auto result = analyzer.loadFile(emptyFile);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(analyzer.getEntries().size(), 0);
}

TEST_F(LogAnalyzerTest, LoadWithStats) {
    LogAnalyzer analyzer;
    auto result = analyzer.loadFileWithStats(testLogFile);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->loaded_count, 5);
    EXPECT_EQ(result->error_count, 0);
}

TEST_F(LogAnalyzerTest, StrictModeParsing) {
    fs::path mixedFile = tempDir / "mixed.log";
    {
        std::ofstream f(mixedFile);
        f << "2023-10-27 10:00:00 [INFO] Valid\n";
        f << "INVALID LINE\n";
        f << "2023-10-27 10:00:05 [INFO] Also Valid\n";
    }

    LogAnalyzer analyzer;
    ParsingConfig config;
    config.strict_mode = true;
    config.line_pattern = R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) \[(\w+)\] (.*)$)";
    config.timestamp_index = 1;
    config.level_index = 2;
    config.message_index = 3;
    
    size_t error_count = 0;
    config.error_callback = [&](const ParseError& e) {
        error_count++;
        EXPECT_EQ(e.line_number, 2);
    };

    analyzer.setParsingConfig(config);
    auto result = analyzer.loadFileWithStats(mixedFile);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->loaded_count, 2);
    EXPECT_EQ(result->error_count, 1);
    EXPECT_EQ(error_count, 1);
}

TEST_F(LogAnalyzerTest, MaxErrorsLimit) {
    fs::path badFile = tempDir / "bad.log";
    {
        std::ofstream f(badFile);
        for(int i=0; i<10; ++i) f << "bad line " << i << "\n";
    }

    LogAnalyzer analyzer;
    ParsingConfig config;
    config.strict_mode = true;
    config.line_pattern = R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) \[(\w+)\] (.*)$)";
    config.max_errors = 3;
    
    analyzer.setParsingConfig(config);
    auto result = analyzer.loadFileWithStats(badFile);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->error_count, 3);
    EXPECT_EQ(result->loaded_count, 0);
}

TEST_F(LogAnalyzerTest, GetEntriesSpan) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    auto span = analyzer.getEntriesSpan();
    EXPECT_EQ(span.size(), 5);
    EXPECT_EQ(span[0].level, LogLevel::INFO);
}

// --- Suite 2: ParsingConfigTest ---

TEST_F(LogAnalyzerTest, CustomFormatParsing) {
    fs::path customFile = tempDir / "custom.log";
    createCustomLogFile(customFile);

    LogAnalyzer analyzer;
    ParsingConfig config;
    config.strict_mode = true;
    // Format: [THREAD_ID] {TIMESTAMP} <LEVEL> (SOURCE_FILE:LINE) Message
    config.line_pattern = R"(\[(.*?)\] \{(.*?)\} <(.*?)> \((.*?):(\d+)\) (.*))";
    config.thread_id_index = 1;
    config.timestamp_index = 2;
    config.level_index = 3;
    config.file_index = 4;
    config.line_index = 5;
    config.message_index = 6;

    analyzer.setParsingConfig(config);
    auto result = analyzer.loadFile(customFile);
    ASSERT_TRUE(result.has_value());
    
    const auto& entries = analyzer.getEntries();
    ASSERT_EQ(entries.size(), 2);
    
    EXPECT_EQ(entries[0].thread_id, "T1");
    EXPECT_EQ(entries[0].level, LogLevel::INFO);
    EXPECT_EQ(entries[0].source_file, "main.cpp");
    EXPECT_EQ(entries[0].source_line, 10);
    EXPECT_EQ(entries[0].message, "Started");

    EXPECT_EQ(entries[1].thread_id, "T2");
    EXPECT_EQ(entries[1].level, LogLevel::ERROR);
}

TEST_F(LogAnalyzerTest, CustomTimeFormat) {
    fs::path timeFile = tempDir / "time.log";
    {
        std::ofstream f(timeFile);
        f << "27/10/2023 10:00:00 [INFO] Date format test\n";
    }

    LogAnalyzer analyzer;
    ParsingConfig config;
    config.strict_mode = true;
    config.line_pattern = R"(^(\d{2}/\d{2}/\d{4} \d{2}:\d{2}:\d{2}) \[(\w+)\] (.*)$)";
    config.timestamp_index = 1;
    config.level_index = 2;
    config.message_index = 3;
    config.time_format = "%d/%m/%Y %H:%M:%S";

    analyzer.setParsingConfig(config);
    auto result = analyzer.loadFile(timeFile);
    ASSERT_TRUE(result.has_value());
    
    const auto& entries = analyzer.getEntries();
    ASSERT_EQ(entries.size(), 1);
    EXPECT_EQ(entries[0].timestamp, "27/10/2023 10:00:00");
    // Verify time_point is correctly parsed
    EXPECT_NE(entries[0].time_point, std::chrono::system_clock::time_point{});
}

// --- Suite 3: FilteringTest ---

TEST_F(LogAnalyzerTest, LevelRangeFilter) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.level = LogLevel::WARNING; // Should include WARNING, ERROR, CRITICAL
    
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 3);
}

TEST_F(LogAnalyzerTest, MultiLevelSelect) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.levels = {LogLevel::INFO, LogLevel::CRITICAL};
    
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 2);
    EXPECT_EQ(filtered[0].level, LogLevel::INFO);
    EXPECT_EQ(filtered[1].level, LogLevel::CRITICAL);
}

TEST_F(LogAnalyzerTest, KeywordSearch) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.keyword = "Connection";
    options.case_sensitive = true;
    EXPECT_EQ(analyzer.getFilteredEntries(options).size(), 1); // "Connection failed"
    
    options.case_sensitive = false;
    EXPECT_EQ(analyzer.getFilteredEntries(options).size(), 2); // "Connection failed", "Retrying connection"
}

TEST_F(LogAnalyzerTest, PreciseTimeFiltering) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    // Convert string to time_point for testing
    auto parseTime = [](const std::string& s) {
        std::tm tm = {};
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    };

    FilterOptions options;
    options.start_tp = parseTime("2023-10-27 10:01:00");
    options.end_tp = parseTime("2023-10-27 10:02:00");
    
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 2);
    EXPECT_EQ(filtered[0].timestamp, "2023-10-27 10:01:00");
    EXPECT_EQ(filtered[1].timestamp, "2023-10-27 10:02:00");
}

TEST_F(LogAnalyzerTest, MetadataFiltering) {
    fs::path customFile = tempDir / "custom_meta.log";
    createCustomLogFile(customFile);
    
    LogAnalyzer analyzer;
    ParsingConfig config;
    config.strict_mode = true;
    config.line_pattern = R"(\[(.*?)\] \{(.*?)\} <(.*?)> \((.*?):(\d+)\) (.*))";
    config.thread_id_index = 1;
    config.timestamp_index = 2;
    config.level_index = 3;
    config.file_index = 4;
    config.message_index = 6;
    analyzer.setParsingConfig(config);
    analyzer.loadFile(customFile);

    FilterOptions options;
    options.thread_id = "T2";
    EXPECT_EQ(analyzer.getFilteredEntries(options).size(), 1);
    
    options = FilterOptions();
    options.source_file = "main.cpp";
    EXPECT_EQ(analyzer.getFilteredEntries(options).size(), 1);
}

TEST_F(LogAnalyzerTest, RegexFiltering) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.message_regex_pattern = "failed|crash";
    EXPECT_EQ(analyzer.getFilteredEntries(options).size(), 2);
}

TEST_F(LogAnalyzerTest, InvertMatch) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.level = LogLevel::ERROR; // Matches ERROR, CRITICAL
    options.invert_match = true;    // Should match INFO, DEBUG, WARNING
    
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 3);
}

TEST_F(LogAnalyzerTest, ComplexFilter) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.level = LogLevel::WARNING; // WARNING, ERROR, CRITICAL
    options.keyword = "connection";    // "Connection failed", "Retrying connection"
    options.case_sensitive = false;
    
    // Intersection: "Connection failed" (ERROR), "Retrying connection" (WARNING)
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 2);
}

// --- Suite 4: StreamingTest ---

TEST_F(LogAnalyzerTest, StreamAllEntries) {
    LogAnalyzer analyzer;
    size_t count = 0;
    for (const auto& entry : analyzer.streamEntries(testLogFile)) {
        count++;
    }
    EXPECT_EQ(count, 5);
}

TEST_F(LogAnalyzerTest, StreamFilteredEntries) {
    LogAnalyzer analyzer;
    FilterOptions options;
    options.level = LogLevel::ERROR;
    
    size_t count = 0;
    for (const auto& entry : analyzer.streamFilteredEntries(testLogFile, options)) {
        count++;
        EXPECT_GE(entry.level, LogLevel::ERROR);
    }
    EXPECT_EQ(count, 2);
}

TEST_F(LogAnalyzerTest, AnalyzeStream) {
    LogAnalyzer analyzer;
    auto stats = analyzer.analyzeStream(testLogFile);
    ASSERT_TRUE(stats.has_value());
    EXPECT_EQ(stats->total_entries, 5);
    EXPECT_EQ(stats->level_counts.at(LogLevel::ERROR), 1);
}

// --- Suite 5: OutputTest ---

TEST_F(LogAnalyzerTest, StatisticsJSON) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    analyzer.analyze();
    
    std::stringstream ss;
    analyzer.writeStatistics(ss, true);
    std::string output = ss.str();
    
    EXPECT_NE(output.find("\"total_entries\": 5"), std::string::npos);
    EXPECT_NE(output.find("\"ERROR\": 1"), std::string::npos);
}

TEST_F(LogAnalyzerTest, FilteredEntriesJSON) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    
    FilterOptions options;
    options.level = LogLevel::CRITICAL;
    
    std::stringstream ss;
    analyzer.writeFilteredEntries(ss, options, true);
    std::string output = ss.str();
    
    EXPECT_NE(output.find("\"level\": \"CRITICAL\""), std::string::npos);
    EXPECT_NE(output.find("System crash imminent"), std::string::npos);
}

// --- Suite 6: LogEntryTest ---

TEST_F(LogAnalyzerTest, LogEntryFluentAPI) {
    auto tp = std::chrono::system_clock::now();
    LogEntry entry = LogEntry()
        .withLevel(LogLevel::WARNING)
        .withMessage("Test message")
        .withThreadId("1234")
        .withTimestamp(tp);
        
    EXPECT_EQ(entry.level, LogLevel::WARNING);
    EXPECT_EQ(entry.message, "Test message");
    EXPECT_EQ(entry.thread_id, "1234");
    EXPECT_EQ(entry.time_point, tp);
}

TEST_F(LogAnalyzerTest, LogEntryComparison) {
    LogEntry e1, e2;
    e1.timestamp = "2023-10-27 10:00:00";
    e2.timestamp = "2023-10-27 10:00:01";
    e1.parseTime();
    e2.parseTime();
    
    EXPECT_LT(e1, e2);
    EXPECT_EQ(e1, e1);
}

TEST_F(LogAnalyzerTest, AttributesAndTags) {
    LogEntry entry;
    entry.setAttribute("user", "admin");
    entry.setAttribute("retry_count", int64_t(3));
    entry.withTag("network").withTag("critical");
    
    EXPECT_EQ(entry.getAttributeAsString("user"), "admin");
    auto retry = entry.getAttributeAs<int64_t>("retry_count");
    ASSERT_TRUE(retry.has_value());
    EXPECT_EQ(*retry, 3);
    
    EXPECT_TRUE(entry.hasTag("network"));
    EXPECT_TRUE(entry.hasTag("critical"));
    EXPECT_FALSE(entry.hasTag("ui"));
}

// --- Suite 7: AsyncLoadingTest ---

TEST_F(LogAnalyzerTest, AsyncLoadSuccess) {
    LogAnalyzer analyzer;
    std::promise<void> progress_called;
    
    auto future = analyzer.loadFileAsync(testLogFile, [&](const ProgressInfo& info) {
        if (info.lines_processed > 0) {
            try {
                progress_called.set_value();
            } catch (const std::future_error&) {
                // Ignore multiple calls
            }
        }
    });
    
    auto result = future.get();
    EXPECT_EQ(result.loaded_count, 5);
    EXPECT_EQ(analyzer.getEntries().size(), 5);
    
    // Wait for progress callback (should be instant for small file)
    auto status = progress_called.get_future().wait_for(std::chrono::seconds(1));
    EXPECT_EQ(status, std::future_status::ready);
}

// --- Suite 8: StructuredDataTest ---

TEST_F(LogAnalyzerTest, AttributeFiltering) {
    LogAnalyzer analyzer;
    // Create entries with attributes
    LogEntry e1 = LogEntry().withMessage("M1").withAttribute("user", "alice");
    LogEntry e2 = LogEntry().withMessage("M2").withAttribute("user", "bob");
    LogEntry e3 = LogEntry().withMessage("M3").withAttribute("user", "alice").withAttribute("role", "admin");
    
    analyzer.addEntry(e1);
    analyzer.addEntry(e2);
    analyzer.addEntry(e3);
    
    FilterOptions options;
    options.attribute_matches["user"] = "alice";
    
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 2); // e1 and e3
    EXPECT_EQ(filtered[0].message, "M1");
    EXPECT_EQ(filtered[1].message, "M3");
    
    options.attribute_matches["role"] = "admin";
    filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 1); // e3
    EXPECT_EQ(filtered[0].message, "M3");
}

TEST_F(LogAnalyzerTest, TagFiltering) {
    LogAnalyzer analyzer;
    LogEntry e1 = LogEntry().withMessage("M1").withTag("network");
    LogEntry e2 = LogEntry().withMessage("M2").withTag("ui");
    LogEntry e3 = LogEntry().withMessage("M3").withTag("network").withTag("critical");
    
    analyzer.addEntry(e1);
    analyzer.addEntry(e2);
    analyzer.addEntry(e3);
    
    FilterOptions options;
    options.required_tags.insert("network");
    
    auto filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 2); // e1 and e3
    
    options.required_tags.insert("critical");
    filtered = analyzer.getFilteredEntries(options);
    EXPECT_EQ(filtered.size(), 1); // e3
}

// --- Suite 9: DeepStatisticsTest ---

TEST_F(LogAnalyzerTest, DeepStatisticsTest) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile);
    auto stats = analyzer.getStatistics();
    
    // Check timeline distribution (bucketed by minute)
    // 10:00:00 -> bucket 10:00
    // 10:00:05 -> bucket 10:00
    // 10:01:00 -> bucket 10:01
    // 10:02:00 -> bucket 10:02
    // 10:03:00 -> bucket 10:03
    
    EXPECT_EQ(stats.timeline_distribution.size(), 4);
    
    size_t sum = 0;
    for (auto& [tp, count] : stats.timeline_distribution) {
        sum += count;
    }
    EXPECT_EQ(sum, 5);
}

// --- Suite 10: LogEntryExtraTest ---

TEST_F(LogAnalyzerTest, LogEntryExtraTest) {
    auto loc = std::source_location::current();
    LogEntry entry = LogEntry::create(LogLevel::ERROR, "Test Error", loc);
    
    EXPECT_EQ(entry.level, LogLevel::ERROR);
    EXPECT_EQ(entry.message, "Test Error");
    EXPECT_FALSE(entry.source_file.empty());
    EXPECT_EQ(entry.source_line, loc.line());
    
    try {
        throw std::runtime_error("Failure");
    } catch (const std::exception& e) {
        entry.withException(e);
    }
    
    EXPECT_FALSE(entry.getAttributeAsString("exception_type").empty());
    EXPECT_EQ(entry.getAttributeAsString("exception_message"), "Failure");
    
    // Check JSON output
    std::string json = entry.toJson();
    EXPECT_NE(json.find("\"message\": \"Test Error\""), std::string::npos);
    EXPECT_NE(json.find("\"exception_message\": \"Failure\""), std::string::npos);
}

// --- Suite 11: ResilienceTests ---

TEST_F(LogAnalyzerTest, ResilienceTests) {
    // Test with invalid regex
    LogAnalyzer analyzer;
    ParsingConfig config;
    config.strict_mode = true;
    config.line_pattern = "[Invalid Regex"; // Missing closing bracket
    
    analyzer.setParsingConfig(config);
    
    EXPECT_NO_THROW(analyzer.loadFile(testLogFile));
}
// --- Iteration 1 Tests ---

TEST_F(LogAnalyzerTest, ParsingConfigValidation) {
    ParsingConfig validConfig;
    validConfig.line_pattern = R"(^(\S+) (.*)$)";
    validConfig.timestamp_index = 1;
    EXPECT_TRUE(validConfig.validate());

    ParsingConfig invalidConfig;
    invalidConfig.line_pattern = R"([Invalid Regex)";
    EXPECT_FALSE(invalidConfig.validate());

    ParsingConfig emptyConfig;
    emptyConfig.line_pattern = "";
    EXPECT_FALSE(emptyConfig.validate());
}

TEST_F(LogAnalyzerTest, ParsingConfigFromRegex) {
    std::string pattern = R"(^(?<timestamp>\S+) (?<level>\w+) (?<message>.*)$)";
    ParsingConfig config = ParsingConfig::fromRegex(pattern);
    
    EXPECT_EQ(config.line_pattern, pattern);
    EXPECT_EQ(config.field_mapping.size(), 3);
    EXPECT_EQ(config.field_mapping["timestamp"], "timestamp");
    EXPECT_EQ(config.field_mapping["level"], "level");
    EXPECT_EQ(config.field_mapping["message"], "message");
}

// Helper for attribute range tests
void createLogFileWithNumericAttributes(const fs::path& path) {
    std::ofstream file(path);
    file << "2023-10-27 10:00:00 [INFO] CPU=10%\n";
    file << "2023-10-27 10:00:01 [INFO] CPU=50%\n";
    file << "2023-10-27 10:00:02 [INFO] CPU=95%\n";
    file << "2023-10-27 10:00:03 [ERROR] CPU=100%\n";
    file << "2023-10-27 10:00:04 [INFO] Temp=25.5C\n";
    file.close();
}

TEST_F(LogAnalyzerTest, AttributeRangeFilter) {
    fs::path attrFile = tempDir / "attributes.log";
    createLogFileWithNumericAttributes(attrFile);

    LogAnalyzer analyzer;
    ParsingConfig config;
    config.line_pattern = R"(^(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) \[(\w+)\] (.*)$)";
    config.timestamp_index = 1;
    config.level_index = 2;
    config.message_index = 3;
    analyzer.setParsingConfig(config);
    // Custom enricher to parse CPU/Temp from message into attributes
    analyzer.addEnricher([](LogEntry& e) {
        std::regex cpu_regex("CPU=(\\d+)%");
        std::smatch match;
        if (std::regex_search(e.message, match, cpu_regex) && match.size() > 1) {
            try { e.withAttribute("cpu_usage", std::stoll(match[1].str())); } catch(...) {}
        }
        std::regex temp_regex("Temp=([0-9.]+)C");
        if (std::regex_search(e.message, match, temp_regex) && match.size() > 1) {
            try { e.withAttribute("temperature", std::stod(match[1].str())); } catch(...) {}
        }
    });

    analyzer.loadFile(attrFile);
    
    // Filter for CPU usage > 90
    auto predicate = Filters::AttributeRange("cpu_usage", LogValue(int64_t(90)), LogValue(int64_t(100)));
    auto filtered = analyzer.getFilteredEntries(*predicate);
    EXPECT_EQ(filtered.size(), 2); // 95% and 100%
    EXPECT_EQ(filtered[0].getAttributeAs<int64_t>("cpu_usage"), 95);
    EXPECT_EQ(filtered[1].getAttributeAs<int64_t>("cpu_usage"), 100);

    // Filter for temperature between 20 and 30
    auto pred_temp = Filters::AttributeRange("temperature", LogValue(20.0), LogValue(30.0));
    auto filtered_temp = analyzer.getFilteredEntries(*pred_temp);
    EXPECT_EQ(filtered_temp.size(), 1);
    EXPECT_EQ(filtered_temp[0].getAttributeAs<double>("temperature"), 25.5);
}

TEST_F(LogAnalyzerTest, SinceFilter) {
    LogAnalyzer analyzer;
    // This test is inherently brittle because `Filters::Since` uses `std::chrono::system_clock::now()`.
    // To make it robust, `SincePredicate` should accept a reference time, but the design specified it uses 'now'.
    // We will test it with a very large duration, expecting all entries from the test file to be captured.
    analyzer.loadFile(testLogFile);
    auto pred_since_24h = Filters::Since(std::chrono::hours(24));
    auto filtered_since = analyzer.getFilteredEntries(*pred_since_24h);
    EXPECT_EQ(filtered_since.size(), 5); // All entries should be within last 24h
}

TEST_F(LogAnalyzerTest, AnyKeywordFilter) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile); // Contains "System", "Connection", "Debugging", "Retrying", "crash"

    // Search for "System" or "Connection" (case-sensitive)
    auto predicate = Filters::AnyKeyword({"System", "Connection"}, true);
    auto filtered = analyzer.getFilteredEntries(*predicate);
    // "System started", "Connection failed", "System crash imminent"
    EXPECT_EQ(filtered.size(), 3); 

    // Search for "system" or "connection" (case-insensitive)
    auto predicate_ci = Filters::AnyKeyword({"system", "connection"}, false);
    auto filtered_ci = analyzer.getFilteredEntries(*predicate_ci);
    // "System started", "Connection failed", "Retrying connection", "System crash imminent"
    EXPECT_EQ(filtered_ci.size(), 4);
}

TEST_F(LogAnalyzerTest, GetAttributeFrequency) {
    fs::path attrFile = tempDir / "attributes_freq.log";
    std::ofstream file(attrFile);
    file << "2023-10-27 10:00:00 [INFO] user=alice action=login\n";
    file << "2023-10-27 10:00:01 [INFO] user=bob action=view\n";
    file << "2023-10-27 10:00:02 [WARN] user=alice action=failed_login\n";
    file << "2023-10-27 10:00:03 [INFO] user=alice action=view\n";
    file << "2023-10-27 10:00:04 [ERROR] user=charlie action=error\n";
    file.close();

    LogAnalyzer analyzer;
    ParsingConfig config;
    config.line_pattern = R"(^(\S+) \[(.*?)\] (.*)$)"; // Generic pattern
    config.timestamp_index = 1;
    config.level_index = 2;
    config.message_index = 3;
    analyzer.setParsingConfig(config);
    // Enricher to parse attributes from message
    analyzer.addEnricher([](LogEntry& e) {
        std::regex attr_regex("(\\w+)=(\\w+)");
        std::sregex_iterator next(e.message.begin(), e.message.end(), attr_regex);
        std::sregex_iterator end;
        for (; next != end; ++next) {
            e.withAttribute(next->str(1), next->str(2));
        }
    });

    analyzer.loadFile(attrFile);
    
    auto freq = analyzer.getAttributeFrequency("user");
    EXPECT_EQ(freq.size(), 3);
    EXPECT_EQ(freq[LogValue("alice")], 3);
    EXPECT_EQ(freq[LogValue("bob")], 1);
    EXPECT_EQ(freq[LogValue("charlie")], 1);

    auto action_freq = analyzer.getAttributeFrequency("action");
    EXPECT_EQ(action_freq.size(), 4);
    EXPECT_EQ(action_freq[LogValue("login")], 1);
    EXPECT_EQ(action_freq[LogValue("view")], 2); // Two 'view' actions
    EXPECT_EQ(action_freq[LogValue("failed_login")], 1);
    EXPECT_EQ(action_freq[LogValue("error")], 1);
}

TEST_F(LogAnalyzerTest, GetTimeline) {
    fs::path timelineFile = tempDir / "timeline.log";
    std::ofstream file(timelineFile);
    file << "2023-10-27 10:00:00 [INFO] 1\n";
    file << "2023-10-27 10:00:15 [INFO] 2\n";
    file << "2023-10-27 10:01:05 [INFO] 3\n";
    file << "2023-10-27 10:02:30 [INFO] 4\n";
    file << "2023-10-27 10:02:45 [INFO] 5\n";
    file.close();

    LogAnalyzer analyzer;
    analyzer.loadFile(timelineFile);
    
    // Bucket size 1 minute
    auto timeline = analyzer.getTimeline(std::chrono::minutes(1));
    EXPECT_EQ(timeline.size(), 3);

    // Expected buckets: 10:00 (2 entries), 10:01 (1 entry), 10:02 (2 entries)
    auto parseTime = [](const std::string& s) {
        std::tm tm = {};
        std::istringstream ss(s);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        return std::chrono::system_clock::from_time_t(std::mktime(&tm));
    };

    EXPECT_EQ(timeline[0].first, parseTime("2023-10-27 10:00:00"));
    EXPECT_EQ(timeline[0].second, 2);
    EXPECT_EQ(timeline[1].first, parseTime("2023-10-27 10:01:00"));
    EXPECT_EQ(timeline[1].second, 1);
    EXPECT_EQ(timeline[2].first, parseTime("2023-10-27 10:02:00"));
    EXPECT_EQ(timeline[2].second, 2);
}

TEST_F(LogAnalyzerTest, GetTrace) {
    LogAnalyzer analyzer;
    LogEntry e1 = LogEntry().withMessage("M1").withTraceContext("trace1", "spanA");
    LogEntry e2 = LogEntry().withMessage("M2").withTraceContext("trace2", "spanB");
    LogEntry e3 = LogEntry().withMessage("M3").withTraceContext("trace1", "spanC");
    
    analyzer.addEntry(e1);
    analyzer.addEntry(e2);
    analyzer.addEntry(e3);
    
    auto trace1_entries = analyzer.getTrace("trace1");
    EXPECT_EQ(trace1_entries.size(), 2);
    EXPECT_EQ(trace1_entries[0].message, "M1");
    EXPECT_EQ(trace1_entries[1].message, "M3");

    auto trace2_entries = analyzer.getTrace("trace2");
    EXPECT_EQ(trace2_entries.size(), 1);
    EXPECT_EQ(trace2_entries[0].message, "M2");

    auto trace_x_entries = analyzer.getTrace("traceX");
    EXPECT_EQ(trace_x_entries.size(), 0);
}

TEST_F(LogAnalyzerTest, MarkdownExporter) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile); // 5 entries
    analyzer.analyze(); // to populate stats

    std::stringstream ss_stats;
    MarkdownExporter md_exporter_stats(ss_stats);
    md_exporter_stats.exportStats(analyzer.getStatistics());
    std::string output_stats = ss_stats.str();
    EXPECT_NE(output_stats.find("# Log Analysis Statistics"), std::string::npos);
    EXPECT_NE(output_stats.find("| Total Entries | 5 |"), std::string::npos);
    EXPECT_NE(output_stats.find("| ERROR | 1 |"), std::string::npos);

    std::stringstream ss_entries;
    MarkdownExporter md_exporter_entries(ss_entries);
    md_exporter_entries.exportEntries(analyzer.getEntriesSpan());
    std::string output_entries = ss_entries.str();
    EXPECT_NE(output_entries.find("| Timestamp | Level | Message |"), std::string::npos);
    EXPECT_NE(output_entries.find("| 2023-10-27 10:00:00 | INFO | System started |"), std::string::npos);
}

TEST_F(LogAnalyzerTest, ConsoleExporter) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile); // 5 entries
    analyzer.analyze(); // to populate stats

    std::stringstream ss_stats;
    ConsoleExporter console_exporter_stats(ss_stats, false); // No color for testing output content
    console_exporter_stats.exportStats(analyzer.getStatistics());
    std::string output_stats = ss_stats.str();
    EXPECT_NE(output_stats.find("=== Log Analysis Statistics ==="), std::string::npos);
    EXPECT_NE(output_stats.find("Total entries: 5"), std::string::npos);
    EXPECT_NE(output_stats.find("ERROR     : 1"), std::string::npos); // Note spacing due to setw

    std::stringstream ss_entries;
    ConsoleExporter console_exporter_entries(ss_entries, false); // No color for testing output content
    console_exporter_entries.exportEntries(analyzer.getEntriesSpan());
    std::string output_entries = ss_entries.str();
    EXPECT_NE(output_entries.find("[2023-10-27 10:00:00] [   INFO] System started"), std::string::npos);
}

TEST_F(LogAnalyzerTest, ConsoleExporterWithColor) {
    LogAnalyzer analyzer;
    analyzer.loadFile(testLogFile); 
    
    std::stringstream ss_entries;
    ConsoleExporter console_exporter_entries(ss_entries, true); // With color
    console_exporter_entries.exportEntries(analyzer.getEntriesSpan());
    std::string output_entries = ss_entries.str();
    
    // Check for ANSI color codes
    EXPECT_NE(output_entries.find("\033[32m[   INFO]\033[0m"), std::string::npos); // Green for INFO
    EXPECT_NE(output_entries.find("\033[31m[  ERROR]\033[0m"), std::string::npos); // Red for ERROR
}
