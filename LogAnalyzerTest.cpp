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
    EXPECT_NE(json.find("\"message\":\"Test Error\""), std::string::npos);
    EXPECT_NE(json.find("\"exception_message\":\"Failure\""), std::string::npos);
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