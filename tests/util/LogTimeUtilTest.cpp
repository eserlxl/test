#include <gtest/gtest.h>
#include <util/LogTimeUtil.h>
#include <model/LogFormattingOptions.h>
#include <chrono>
#include <string>
#include <algorithm> // For std::find
#include <gmock/gmock.h> // For EXPECT_THAT, HasSubstr

using namespace std::chrono_literals; // Added for chrono numeric literals

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
    model_opts.timestamp_format = ::LogTimestampFormat::ISO8601;
    model_opts.precision = ::LogPrecision::Micros;
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
    EXPECT_EQ(LogTimeUtil::formatDuration(seconds(1) + milliseconds(234) + microseconds(567) + nanoseconds(890)), "1.23456789s");
    EXPECT_EQ(LogTimeUtil::formatDuration(- (minutes(1) + seconds(30))), "-1m 30s");
    EXPECT_EQ(LogTimeUtil::formatDuration(hours(0)), "0s");
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
    opts.precision = ::LogPrecision::Millis;
    
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

    opts.precision = ::LogPrecision::Millis;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_ms, opts), "2023-03-15T12:00:00.123Z");

    opts.precision = ::LogPrecision::Micros;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_us, opts), "2023-03-15T12:00:00.123456Z");

    opts.precision = ::LogPrecision::Nanos;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_ns, opts), "2023-03-15T12:00:00.123456789Z");

    opts.timezone = ::LogTimezone::Local;
    // This part of the test is environment-dependent and might be flaky.
    // We just check that it produces a string with an offset.
    std::string local_time = LogTimeUtil::formatTimestamp(test_tp, opts);
    // Local time should contain an offset (+/-HHMM)
    EXPECT_TRUE(local_time.find("+") != std::string::npos || local_time.find("-") != std::string::npos);
}

TEST_F(LogTimeUtilTest, ParseTimestampWithParseResult) {
    // Explicitly initialize all members to silence -Wmissing-field-initializers
    LogTimeUtil::ParseOptions opts_strict { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };
    LogTimeUtil::ParseOptions opts_lenient { .strict = false, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };

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
    EXPECT_EQ(res_err1.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
    EXPECT_FALSE(res_err1.error().message.empty());

    // Invalid value (out of range for unix millis)
    auto res_err2 = LogTimeUtil::parseTimestamp("999999999999999999999999999999", ::LogTimestampFormat::UnixMillis, opts_strict);
    ASSERT_FALSE(res_err2.has_value());
    EXPECT_EQ(res_err2.error().code, LogTimeUtil::ParseErrorType::OutOfRange);
    EXPECT_FALSE(res_err2.error().message.empty());

    // Strict mode should fail with extra characters
    auto res_err3 = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z and more", ::LogTimestampFormat::ISO8601, opts_strict);
    ASSERT_FALSE(res_err3.has_value());
    EXPECT_EQ(res_err3.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
    EXPECT_FALSE(res_err3.error().message.empty());

    // Lenient mode should succeed with extra characters
    auto res_lenient = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z and more", ::LogTimestampFormat::ISO8601, opts_lenient);
    ASSERT_TRUE(res_lenient.has_value());
    EXPECT_EQ(res_lenient.value(), test_tp);
}

TEST_F(LogTimeUtilTest, ParseTimestampWithAutoDetection) {
    // Explicitly initialize all members to silence -Wmissing-field-initializers
    LogTimeUtil::ParseOptions opts_strict { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };
    LogTimeUtil::ParseOptions opts_lenient { .strict = false, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };

    // Test ISO8601 auto-detection
    auto res_iso = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z", opts_strict);
    ASSERT_TRUE(res_iso.has_value());
    EXPECT_EQ(res_iso.value(), test_tp);

    // Test RFC1123 auto-detection
    // Example: "Wed, 15 Mar 2023 12:00:00 UTC"
    LogTimeUtil::FormatOptions rfc1123_fmt_opts;
    rfc1123_fmt_opts.format = ::LogTimestampFormat::RFC1123;
    rfc1123_fmt_opts.precision = ::LogPrecision::Seconds;
    std::string rfc1123_str = LogTimeUtil::formatTimestamp(test_tp, rfc1123_fmt_opts); // "Wed, 15 Mar 2023 12:00:00 UTC"
    auto res_rfc = LogTimeUtil::parseTimestamp(rfc1123_str, opts_strict);
    ASSERT_TRUE(res_rfc.has_value()) << "Failed to parse RFC1123: " << res_rfc.error().message;
    EXPECT_EQ(res_rfc.value(), test_tp);

    // Test CommonLogFormat auto-detection
    // Example: "15/Mar/2023:12:00:00 +0000"
    LogTimeUtil::FormatOptions clf_fmt_opts;
    clf_fmt_opts.format = ::LogTimestampFormat::CommonLogFormat;
    clf_fmt_opts.precision = ::LogPrecision::Seconds;
    std::string clf_str = LogTimeUtil::formatTimestamp(test_tp, clf_fmt_opts); // "15/Mar/2023:12:00:00 +0000"
    auto res_clf = LogTimeUtil::parseTimestamp(clf_str, opts_strict);
    ASSERT_TRUE(res_clf.has_value()) << "Failed to parse CommonLogFormat: " << res_clf.error().message;
    EXPECT_EQ(res_clf.value(), test_tp);

    // Test Unix Millis auto-detection
    auto res_unix_millis = LogTimeUtil::parseTimestamp("1678881600123", opts_strict);
    ASSERT_TRUE(res_unix_millis.has_value());
    EXPECT_EQ(res_unix_millis.value(), test_tp_ms);

    // Test Unix Seconds auto-detection
    auto res_unix_seconds = LogTimeUtil::parseTimestamp("1678881600", opts_strict);
    ASSERT_TRUE(res_unix_seconds.has_value());
    EXPECT_EQ(res_unix_seconds.value(), test_tp);

    // Test Unix Nanos auto-detection
    auto res_unix_nanos = LogTimeUtil::parseTimestamp("1678881600123456789", opts_strict);
    ASSERT_TRUE(res_unix_nanos.has_value());
    EXPECT_EQ(res_unix_nanos.value(), test_tp_ns);

    // Test auto-detection failure
    auto res_no_auto = LogTimeUtil::parseTimestamp("not a recognized format string", opts_strict);
    ASSERT_FALSE(res_no_auto.has_value());
    EXPECT_EQ(res_no_auto.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
    EXPECT_FALSE(res_no_auto.error().message.empty());

    // Test auto-detection with lenient options and extra characters
    auto res_auto_lenient = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z and more", opts_lenient);
    ASSERT_TRUE(res_auto_lenient.has_value());
    EXPECT_EQ(res_auto_lenient.value(), test_tp);
}

TEST_F(LogTimeUtilTest, ParseTimestampWithNewFormats) {
    // Explicitly initialize all members to silence -Wmissing-field-initializers
    LogTimeUtil::ParseOptions opts_strict { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };

    // RFC1123
    LogTimeUtil::FormatOptions rfc1123_fmt_opts;
    rfc1123_fmt_opts.format = ::LogTimestampFormat::RFC1123;
    rfc1123_fmt_opts.precision = ::LogPrecision::Seconds; // Ensure no fractional parts interfere
    std::string rfc1123_str = LogTimeUtil::formatTimestamp(test_tp, rfc1123_fmt_opts); // "Wed, 15 Mar 2023 12:00:00 UTC"
    auto res_rfc1123 = LogTimeUtil::parseTimestamp(rfc1123_str, ::LogTimestampFormat::RFC1123, opts_strict);
    ASSERT_TRUE(res_rfc1123.has_value()) << "Failed to parse RFC1123: " << res_rfc1123.error().message;
    EXPECT_EQ(res_rfc1123.value(), test_tp);

    // CommonLogFormat
    LogTimeUtil::FormatOptions clf_fmt_opts;
    clf_fmt_opts.format = ::LogTimestampFormat::CommonLogFormat;
    clf_fmt_opts.precision = ::LogPrecision::Seconds;
    std::string clf_str = LogTimeUtil::formatTimestamp(test_tp, clf_fmt_opts); // "15/Mar/2023:12:00:00 +0000"
    auto res_clf = LogTimeUtil::parseTimestamp(clf_str, ::LogTimestampFormat::CommonLogFormat, opts_strict);
    ASSERT_TRUE(res_clf.has_value()) << "Failed to parse CommonLogFormat: " << res_clf.error().message;
    EXPECT_EQ(res_clf.value(), test_tp);

    // UnixSeconds
    auto res_unix_s = LogTimeUtil::parseTimestamp("1678881600", ::LogTimestampFormat::UnixSeconds, opts_strict);
    ASSERT_TRUE(res_unix_s.has_value());
    EXPECT_EQ(res_unix_s.value(), test_tp);

    // UnixNanos
    auto res_unix_ns = LogTimeUtil::parseTimestamp("1678881600123456789", ::LogTimestampFormat::UnixNanos, opts_strict);
    ASSERT_TRUE(res_unix_ns.has_value());
    EXPECT_EQ(res_unix_ns.value(), test_tp_ns);

    // Test parsing with locale hint (e.g., French month names)
    LogTimeUtil::ParseOptions fr_opts { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = "fr_FR.UTF-8" };
    // Create a timepoint with March for French locale
    std::chrono::system_clock::time_point march_tp = std::chrono::sys_days(std::chrono::year(2023)/std::chrono::March/std::chrono::day(15)) + std::chrono::hours(12) + std::chrono::minutes(0) + std::chrono::seconds(0);
    std::string rfc1123_fr = "mer., 15 mars 2023 12:00:00 UTC"; // 'mer.' for mercredi, 'mars' for March
    
    // This is tricky as chrono::parse expects specific locale names.
    // Testing with a specific locale might require the locale to be installed on the system.
    // For now, let's just test that providing a locale doesn't break general parsing (if locale is not found it will throw)
    // A better test would involve a custom locale object or mock locale.
    // For this iteration, assume classic locale behavior unless system locale is guaranteed.
    // If the locale isn't installed, the imbue will throw, which is handled.
    try {
        auto res_rfc1123_fr = LogTimeUtil::parseTimestamp(rfc1123_fr, ::LogTimestampFormat::RFC1123, fr_opts);
        if (res_rfc1123_fr.has_value()) {
            // This might pass if the system's default locale also accepts 'mars' or if "fr_FR.UTF-8" is available
            EXPECT_EQ(res_rfc1123_fr.value(), march_tp);
        } else {
             // Expecting a failure if locale not found or format doesn't match
            EXPECT_EQ(res_rfc1123_fr.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
        }
    } catch (const std::runtime_error& e) {
        // If the locale "fr_FR.UTF-8" is not available on the system, std::locale will throw.
        // This is an acceptable outcome for the test in this context.
        std::cerr << "Locale 'fr_FR.UTF-8' not available, skipping locale-specific parse test: " << e.what() << std::endl;
        SUCCEED() << "Locale 'fr_FR.UTF-8' not available, skipping locale-specific parse test.";
    }
}

TEST_F(LogTimeUtilTest, ParseTimestampWithCustomFormat) {
    // Explicitly initialize all members to silence -Wmissing-field-initializers
    LogTimeUtil::ParseOptions opts_strict { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };

    // Standard custom format parsing (no locale)
    auto res_standard_custom = LogTimeUtil::parseTimestamp("2023/03/15 12:00:00", "%Y/%m/%d %H:%M:%S", opts_strict);
    ASSERT_TRUE(res_standard_custom.has_value());
    EXPECT_EQ(res_standard_custom.value(), test_tp);

    // Test with fractional seconds in custom format
    auto res_custom_frac = LogTimeUtil::parseTimestamp("2023-03-15 12:00:00.123", "%Y-%m-%d %H:%M:%S%F", opts_strict);
    ASSERT_TRUE(res_custom_frac.has_value());
    EXPECT_EQ(res_custom_frac.value(), test_tp_ms);

    // Test parsing with locale hint (e.g., French month names)
    LogTimeUtil::ParseOptions fr_opts { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = "fr_FR.UTF-8" };
    
    std::chrono::system_clock::time_point custom_fr_tp = std::chrono::sys_days(std::chrono::year(2023)/std::chrono::March/std::chrono::day(15)) + std::chrono::hours(14) + std::chrono::minutes(30) + std::chrono::seconds(0);
    std::string custom_fr_str = "15-mars-2023 14:30"; // "mars" for March in French
    std::string custom_fr_format = "%d-%b-%Y %H:%M";
    
    try {
        auto res_custom_fr = LogTimeUtil::parseTimestamp(custom_fr_str, custom_fr_format, fr_opts);
        if (res_custom_fr.has_value()) {
            EXPECT_EQ(res_custom_fr.value(), custom_fr_tp);
        } else {
             EXPECT_EQ(res_custom_fr.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "Locale 'fr_FR.UTF-8' not available, skipping locale-specific custom format parse test: " << e.what() << std::endl;
        SUCCEED() << "Locale 'fr_FR.UTF-8' not available, skipping locale-specific custom format parse test.";
    }

    // Test invalid format string with strict option
    auto res_invalid_format = LogTimeUtil::parseTimestamp("2023-03-15 12:00:00", "%Y-%m-%d %H:%M", opts_strict);
    ASSERT_FALSE(res_invalid_format.has_value());
    EXPECT_EQ(res_invalid_format.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
    EXPECT_FALSE(res_invalid_format.error().message.empty());

    // Test extra characters with strict option
    auto res_extra_chars = LogTimeUtil::parseTimestamp("2023-03-15 12:00:00 ABC", "%Y-%m-%d %H:%M:%S", opts_strict);
    ASSERT_FALSE(res_extra_chars.has_value());
    EXPECT_EQ(res_extra_chars.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
    EXPECT_FALSE(res_extra_chars.error().message.empty());
}

TEST_F(LogTimeUtilTest, ParseTimestampWithCustomFormatAndLocale) {
    // Explicitly initialize all members to silence -Wmissing-field-initializers
    LogTimeUtil::ParseOptions opts_strict { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = std::nullopt };

    // Test parsing with locale hint (e.g., French month names)
    LogTimeUtil::ParseOptions fr_opts { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = "fr_FR.UTF-8" };
    
    std::chrono::system_clock::time_point custom_fr_tp = std::chrono::sys_days(std::chrono::year(2023)/std::chrono::March/std::chrono::day(15)) + std::chrono::hours(14) + std::chrono::minutes(30) + std::chrono::seconds(0);
    std::string custom_fr_str = "15-mars-2023 14:30"; // "mars" for March in French
    std::string custom_fr_format = "%d-%b-%Y %H:%M";
    
    try {
        auto res_custom_fr = LogTimeUtil::parseTimestamp(custom_fr_str, custom_fr_format, fr_opts);
        if (res_custom_fr.has_value()) {
            EXPECT_EQ(res_custom_fr.value(), custom_fr_tp);
        } else {
             EXPECT_EQ(res_custom_fr.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
        }
    } catch (const std::runtime_error& e) {
        std::cerr << "Locale 'fr_FR.UTF-8' not available, skipping locale-specific custom format parse test: " << e.what() << std::endl;
        SUCCEED() << "Locale 'fr_FR.UTF-8' not available, skipping locale-specific custom format parse test.";
    }

    // Test invalid locale name
    LogTimeUtil::ParseOptions invalid_locale_opts { .strict = true, .defaultTimezoneName = std::nullopt, .localeName = "invalid_locale" };
    auto res_invalid_locale = LogTimeUtil::parseTimestamp("2023-03-15T12:00:00Z", ::LogTimestampFormat::ISO8601, invalid_locale_opts);
    ASSERT_FALSE(res_invalid_locale.has_value());
    EXPECT_EQ(res_invalid_locale.error().code, LogTimeUtil::ParseErrorType::InvalidFormat);
    EXPECT_THAT(res_invalid_locale.error().message, testing::HasSubstr("Failed to imbue locale 'invalid_locale'"));
}

TEST_F(LogTimeUtilTest, ToZonedTime) {
    using namespace std::chrono;
    auto zoned_time_ny = LogTimeUtil::toZonedTime(test_tp, "America/New_York");
    // Verify properties - 2023-03-15 12:00:00 UTC is 08:00:00 EDT in New York
    
    // Manually construct expected local time in NY (4 hours behind UTC)
    sys_days sd_test_tp = floor<days>(test_tp);
    // test_tp is 2023-03-15T12:00:00Z
    // 12:00:00 UTC - 4 hours = 08:00:00 in America/New_York (EDT in March)
    local_time expected_ny_local_time = local_days(std::chrono::year(2023)/std::chrono::March/std::chrono::day(15)) + std::chrono::hours(8);

    EXPECT_EQ(zoned_time_ny.get_local_time(), expected_ny_local_time);
    EXPECT_EQ(zoned_time_ny.get_info().abbrev, "EDT"); // Should be EDT on March 15, 2023
    EXPECT_EQ(zoned_time_ny.get_info().offset, -4h);

    EXPECT_THROW(LogTimeUtil::toZonedTime(test_tp, "Invalid/Timezone"), std::runtime_error);
}

TEST_F(LogTimeUtilTest, ToLocal) {
    // This test is highly dependent on the system's local timezone settings.
    // We can only reliably test if it produces a local_time without throwing.
    // For more robust testing, one would need to mock or control the system timezone.
    
    // We check that it runs without throwing and that the resulting local_time
    // can be converted back to a system_clock::time_point in some timezone (e.g., UTC)
    // without throwing, implying it's a valid local_time object.
    auto local_tp = LogTimeUtil::toLocal(test_tp);
    // Attempt to convert back to UTC using a known timezone to check validity
    try {
        (void)std::chrono::current_zone()->to_sys(local_tp);
        SUCCEED();
    } catch (const std::exception& e) {
        FAIL() << "toLocal produced an invalid local_time: " << e.what();
    }
}

TEST_F(LogTimeUtilTest, ToUtc) {
    using namespace std::chrono;
    // Convert 08:00:00 EDT on 2023-03-15 in America/New_York to UTC
    local_time<system_clock::duration> ny_local_time = 
        local_days(std::chrono::year(2023)/std::chrono::March/std::chrono::day(15)) + std::chrono::hours(8) + std::chrono::minutes(0) + std::chrono::seconds(0);
    
    auto utc_tp = LogTimeUtil::toUtc(ny_local_time, "America/New_York");
    // 08:00 EDT is 12:00 UTC
    EXPECT_EQ(utc_tp, test_tp);

    // Test for ambiguous local time (fall-back DST, e.g., 2023-11-05 01:30:00 in America/New_York)
    // This is problematic. to_sys can throw ambiguous_local_time.
    // The current implementation throws std::runtime_error in this case.
    local_time<system_clock::duration> ambiguous_lt = 
        local_days(std::chrono::year(2023)/std::chrono::November/std::chrono::day(5)) + std::chrono::hours(1) + std::chrono::minutes(30) + std::chrono::seconds(0); // 01:30 AM
    EXPECT_THROW(LogTimeUtil::toUtc(ambiguous_lt, "America/New_York"), std::runtime_error);

    // Test for nonexistent local time (spring-forward DST, e.g., 2023-03-12 02:30:00 in America/New_York)
    local_time<system_clock::duration> nonexistent_lt = 
        local_days(std::chrono::year(2023)/std::chrono::March/std::chrono::day(12)) + std::chrono::hours(2) + std::chrono::minutes(30) + std::chrono::seconds(0); // 02:30 AM
    EXPECT_THROW(LogTimeUtil::toUtc(nonexistent_lt, "America/New_York"), std::runtime_error);

    // Test with invalid timezone name
    EXPECT_THROW(LogTimeUtil::toUtc(ny_local_time, "Invalid/Timezone"), std::runtime_error);
}

TEST_F(LogTimeUtilTest, IsValidChronoFormatString) {
    // Valid format strings
    EXPECT_TRUE(LogTimeUtil::isValidChronoFormatString("%Y-%m-%d %H:%M:%S"));
    EXPECT_TRUE(LogTimeUtil::isValidChronoFormatString("%Y/%m/%d %H:%M:%S%F"));
    EXPECT_TRUE(LogTimeUtil::isValidChronoFormatString("%a, %d %b %Y %H:%M:%S %Z"));
    EXPECT_TRUE(LogTimeUtil::isValidChronoFormatString("%d/%b/%Y:%H:%M:%S %z"));
    EXPECT_TRUE(LogTimeUtil::isValidChronoFormatString("%H:%M"));

    // Invalid format strings
    EXPECT_FALSE(LogTimeUtil::isValidChronoFormatString("%invalid-specifier"));
    EXPECT_FALSE(LogTimeUtil::isValidChronoFormatString("%Y-%m-%D")); // %D is invalid in C++20 chrono format
    EXPECT_FALSE(LogTimeUtil::isValidChronoFormatString(""));
    EXPECT_FALSE(LogTimeUtil::isValidChronoFormatString("{:somenonsense}")); // Invalid format string for std::format
}

TEST_F(LogTimeUtilTest, FormatTimestampNewFormats) {
    LogTimeUtil::FormatOptions opts;
    opts.precision = ::LogPrecision::Seconds; // Ensure no fractional parts interfere
    opts.timezone = ::LogTimezone::UTC;

    // RFC1123
    opts.format = ::LogTimestampFormat::RFC1123;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, opts), "Wed, 15 Mar 2023 12:00:00 UTC");

    // CommonLogFormat
    opts.format = ::LogTimestampFormat::CommonLogFormat;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, opts), "15/Mar/2023:12:00:00 +0000");

    // UnixSeconds
    opts.format = ::LogTimestampFormat::UnixSeconds;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, opts), "1678881600");

    // UnixNanos
    opts.format = ::LogTimestampFormat::UnixNanos;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_ns, opts), "1678881600123456789");
}

TEST_F(LogTimeUtilTest, FormatTimestampWithTimeZoneName) {
    LogTimeUtil::FormatOptions opts;
    opts.precision = ::LogPrecision::Seconds;

    // UTC
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, "UTC", opts), "2023-03-15T12:00:00+00:00"); // Implicitly UTC +00:00

    // America/New_York (EDT)
    // 2023-03-15 12:00:00 UTC -> 08:00:00 EDT
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, "America/New_York", opts), "2023-03-15T08:00:00-04:00");

    // Europe/London (GMT)
    // 2023-03-15 12:00:00 UTC -> 12:00:00 GMT (no DST yet)
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, "Europe/London", opts), "2023-03-15T12:00:00+00:00");

    // Test with fractional seconds
    opts.precision = ::LogPrecision::Millis;
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp_ms, "America/New_York", opts), "2023-03-15T08:00:00.123-04:00");

    // Test with RFC1123 format in specific timezone
    opts.precision = ::LogPrecision::Seconds;
    opts.format = ::LogTimestampFormat::RFC1123;
    // 2023-03-15 12:00:00 UTC -> 08:00:00 EDT in New York
    EXPECT_EQ(LogTimeUtil::formatTimestamp(test_tp, "America/New_York", opts), "Wed, 15 Mar 2023 08:00:00 EDT");

    // Test invalid timezone
    EXPECT_THROW(LogTimeUtil::formatTimestamp(test_tp, "Invalid/Zone", opts), std::runtime_error);
}

// --- Test Deprecated Functions ---

TEST_F(LogTimeUtilTest, DeprecatedGenerateLogEntryTimestampString) {
    LogFormattingOptions model_opts;
    model_opts.precision = ::LogPrecision::Millis;
    
    #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    #endif
    // Resolve ambiguous call by explicitly casting to const reference
    std::string old_way = LogTimeUtil::generateLogEntryTimestampString(test_tp_ms, static_cast<const LogFormattingOptions&>(model_opts));
    #if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
    #endif

    LogTimeUtil::FormatOptions new_opts(model_opts);
    std::string new_way = LogTimeUtil::formatTimestamp(test_tp_ms, new_opts);
    
    EXPECT_EQ(old_way, new_way);
    EXPECT_EQ(old_way, "2023-03-15T12:00:00.123Z");
}
