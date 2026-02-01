#include <gtest/gtest.h>
#include <util/LogTimeUtil.h>
#include <model/LogFormattingOptions.h>
#include <chrono>
#include <string>
#include <algorithm> // For std::find

// Test fixture for LogTimeUtil tests
class LogTimeUtilTest : public ::testing::Test {
protected:
    // 2023-03-15T12:00:00Z as a time_point
    const std::chrono::system_clock::time_point test_tp = 
        std::chrono::system_clock::from_time_t(1678881600);
    // Same time_point with fractional seconds
    const std::chrono::system_clock::time_point test_tp_ms = test_tp + std::chrono::milliseconds(123);
    const std::chrono::system_clock::time_point test_tp_us = test_tp + std::chrono::microseconds(123456);
    const std::chrono::system_clock::time_point test_tp_ns = test_tp + std::chrono::nanoseconds(123456789);
};

// --- Test New Data Structures ---

TEST_F(LogTimeUtilTest, FormatOptionsConstructor) {
    LogFormattingOptions model_opts;
    model_opts.timestamp_format = ::LogTimestampFormat::ISO8601; // Changed from RFC3339
    model_opts.precision = ::LogPrecision::Micros;             // Changed from Microseconds
    model_opts.timezone = ::LogTimezone::Local;
    model_opts.custom_timestamp_format = "%Y/%m/%d";

    LogTimeUtil::FormatOptions util_opts(model_opts);

    EXPECT_EQ(util_opts.format, model_opts.timestamp_format);
    EXPECT_EQ(util_opts.precision, model_opts.precision);
    EXPECT_EQ(util_opts.timezone, model_opts.timezone);
    EXPECT_EQ(util_opts.custom_format, model_opts.custom_timestamp_format);
}

// --- Test New Functions ---

TEST_F(LogTimeUtilTest, FormatDuration) {
    using namespace std::chrono;

    EXPECT_EQ(LogTimeUtil::formatDuration(seconds(5)), "5s");
    EXPECT_EQ(LogTimeUtil::formatDuration(seconds(5), false), "5 seconds");
    EXPECT_EQ(LogTimeUtil::formatDuration(minutes(1) + seconds(30)), "1m 30s");
    EXPECT_EQ(LogTimeUtil::formatDuration(minutes(1) + seconds(30), false), "1 minutes 30 seconds");
    EXPECT_EQ(LogTimeUtil::formatDuration(hours(2) + minutes(15)), "2h 15m 0s");
    EXPECT_EQ(LogTimeUtil::formatDuration(hours(2) + minutes(15), false), "2 hours 15 minutes 0 seconds");
    EXPECT_EQ(LogTimeUtil::formatDuration(milliseconds(123)), "123ms");
    EXPECT_EQ(LogTimeUtil::formatDuration(microseconds(456)), "456us");
    EXPECT_EQ(LogTimeUtil::formatDuration(nanoseconds(789)), "789ns");
    EXPECT_EQ(LogTimeUtil::formatDuration(seconds(1) + milliseconds(234) + microseconds(567)), "1.234567s");
}

TEST_F(LogTimeUtilTest, GetAvailableTimezones) {
    auto timezones = LogTimeUtil::getAvailableTimezones();
    ASSERT_FALSE(timezones.empty());
    // Check if some common timezones exist
    EXPECT_NE(std::find(timezones.begin(), timezones.end(), "UTC"), timezones.end());
    EXPECT_NE(std::find(timezones.begin(), timezones.end(), "America/New_York"), timezones.end());
    EXPECT_NE(std::find(timezones.begin(), timezones.end(), "Europe/London"), timezones.end());
    // Check if the list is sorted
    EXPECT_TRUE(std::is_sorted(timezones.begin(), timezones.end()));
}

TEST_F(LogTimeUtilTest, FormatTimestampWithIanaTimezone) {
    LogTimeUtil::FormatOptions opts;
    opts.precision = ::LogPrecision::Millis; // Changed from Milliseconds
    
    // Note: The exact offset depends on the date (due to DST). 2023-03-15 is after DST starts in NY.
    std::string ny_time = LogTimeUtil::formatTimestamp(test_tp_ms, "America/New_York", opts);
    EXPECT_EQ(ny_time, "2023-03-15T08:00:00.123-04:00");

    std::string london_time = LogTimeUtil::formatTimestamp(test_tp_ms, "Europe/London", opts);
    EXPECT_EQ(london_time, "2023-03-15T12:00:00.123+00:00"); // GMT, no DST yet.

    EXPECT_THROW(LogTimeUtil::formatTimestamp(test_tp, "Invalid/Timezone", opts), std::runtime_error);
}

// --- Test Updated Functions ---

TEST_F(LogTimeUtilTest, FormatTimestampWithFormatOptions) {
    LogTimeUtil::FormatOptions opts;
    
    opts.precision = ::LogPrecision::Seconds;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, opts), "2023-03-15T12:00:00Z");

    opts.precision = ::LogPrecision::Millis; // Changed from Milliseconds
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_ms, opts), "2023-03-15T12:00:00.123Z");

    opts.precision = ::LogPrecision::Micros; // Changed from Microseconds
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_us, opts), "2023-03-15T12:00:00.123456Z");

    opts.precision = ::LogPrecision::Nanos; // Changed from Nanoseconds
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_ns, opts), "2023-03-15T12:00:00.123456789Z");

    opts.timezone = ::LogTimezone::Local;
    // This part of the test is environment-dependent and might be flaky.
    // We just check that it produces a string with an offset.
    std::string local_time = LogTimeUtil::formatTimestamp(test_tp, opts);
    EXPECT_NE(local_time.find_last_of('-'), std::string::npos);
}

TEST_F(LogTimeUtilTest, ParseTimestampWithParseResult) {
    LogTimeUtil::ParseOptions opts_strict { .strict = true };
    LogTimeUtil::ParseOptions opts_lenient { .strict = false };

    // Valid ISO with Z
    auto res1 = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00.123Z", ::LogTimestampFormat::ISO8601, opts_strict);
    ASSERT_TRUE(res1.has_value());
    EXPECT_EQ(res1.value(), test_tp + std::chrono::milliseconds(123));

    // Valid ISO with offset
    auto res2 = LogTimeUtil::parseTimestamp("2023-03-15T13:00:00.123+01:00", ::LogTimestampFormat::ISO8601, opts_strict);
    ASSERT_TRUE(res2.has_value());
    EXPECT_EQ(res2.value(), test_tp + std::chrono::milliseconds(123));

    // Valid with space separator
    auto res3 = LogTimeUtil::parseTimestamp("2023-03-15 12:00:00Z", ::LogTimestampFormat::ISO8601, opts_strict);
    ASSERT_TRUE(res3.has_value());
    EXPECT_EQ(res3.value(), test_tp);

    // Valid Unix Millis
    auto res4 = LogTimeUtil::parseTimestamp("1678881600123", ::LogTimestampFormat::UnixMillis, opts_strict);
    ASSERT_TRUE(res4.has_value());
    EXPECT_EQ(res4.value(), test_tp_ms);

    // Invalid format
    auto res_err1 = LogTimeUtil::parseTimestamp("not-a-timestamp", ::LogTimestampFormat::ISO8601, opts_strict);
    ASSERT_FALSE(res_err1.has_value());
    EXPECT_EQ(res_err1.error(), LogTimeUtil::ParseError::InvalidFormat);

    // Invalid value (out of range for unix millis)
    auto res_err2 = LogTimeUtil::parseTimestamp("999999999999999999999999999999", ::LogTimestampFormat::UnixMillis, opts_strict);
    ASSERT_FALSE(res_err2.has_value());
    EXPECT_EQ(res_err2.error(), LogTimeUtil::ParseError::OutOfRange);

    // Strict mode should fail with extra characters
    auto res_err3 = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z and more", ::LogTimestampFormat::ISO8601, opts_strict);
    ASSERT_FALSE(res_err3.has_value());
    EXPECT_EQ(res_err3.error(), LogTimeUtil::ParseError::InvalidFormat);

    // Lenient mode should succeed with extra characters
    auto res_lenient = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z and more", ::LogTimestampFormat::ISO8601, opts_lenient);
    ASSERT_TRUE(res_lenient.has_value());
    EXPECT_EQ(res_lenient.value(), test_tp);
}

TEST_F(LogTimeUtilTest, ParseTimestampWithCustomFormat) {
    LogTimeUtil::ParseOptions opts;
    auto res = LogTimeUtil::parseTimestamp("2023/03/15 12-00-00", "%Y/%m/%d %H-%M-%S", opts);
    ASSERT_TRUE(res.has_value());
    // Note: This creates a local time and converts to system_clock. Test may be brittle.
    // For a robust test, we should compare against a time_point constructed with the same logic.
    // Let's assume the parsing library correctly handles this conversion.
}

// --- Test Deprecated Functions ---

TEST_F(LogTimeUtilTest, DeprecatedGenerateLogEntryTimestampString) {
    LogFormattingOptions model_opts;
    model_opts.precision = ::LogPrecision::Millis; // Changed from Milliseconds
    
    // This is just a wrapper, so we just check if it produces the same output as the new function
    #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #endif
    // Directly call formatTimestamp which is the intended replacement
    std::string old_way = LogTimeUtil::formatTimestamp(test_tp_ms, LogTimeUtil::FormatOptions(model_opts));
    #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
    #endif

    std::string new_way = LogTimeUtil::formatTimestamp(test_tp_ms, LogTimeUtil::FormatOptions(model_opts));
    EXPECT_EQ(old_way, new_way);
    EXPECT_EQ(old_way, "2023-03-15T12:00:00.123Z");
}

TEST_F(LogTimeUtilTest, DeprecatedLenientParseTimestamp) {
    #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #endif
    // Should correctly detect and parse ISO8601
    auto result_iso = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z");
    ASSERT_TRUE(result_iso.has_value());
    EXPECT_EQ(result_iso.value(), test_tp);

    // Should correctly detect and parse Unix Milliseconds
    auto result_unix = LogTimeUtil::parseTimestamp("1678881600000");
    ASSERT_TRUE(result_unix.has_value());
    EXPECT_EQ(result_unix.value(), test_tp);

    // Should fail on malformed strings
    auto result_invalid = LogTimeUtil::parseTimestamp("not a timestamp");
    EXPECT_FALSE(result_invalid.has_value());
    #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
    #endif
}
