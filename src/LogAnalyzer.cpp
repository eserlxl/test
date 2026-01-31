#include <LogAnalyzer.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <shared_mutex>
#include <condition_variable>

namespace LogAnalysis {

// --- ParsingConfig Implementation ---

bool LogAnalysis::ParsingConfig::validate() const
{
    if (line_pattern.empty())
        return false;
    try
    {
        std::regex r(line_pattern);
    }
    catch (...)
    {
        return false;
    }
    return true;
}

LogAnalysis::ParsingConfig LogAnalysis::ParsingConfig::fromRegex(std::string pattern)
{
    ParsingConfig config;
    config.line_pattern = pattern;
    // Use the new helper for named groups
    config.field_mapping = ParsingConfig::fromRegexWithNamedGroups(pattern).field_mapping;
    return config;
}

// Improved static helper to parse named groups
LogAnalysis::ParsingConfig LogAnalysis::ParsingConfig::fromRegexWithNamedGroups(std::string pattern)
{
    ParsingConfig config;
    config.line_pattern = pattern;
    std::regex named_group_regex("\\(\\?<([a-zA-Z_][a-zA-Z0-9_]*)>");
    auto words_begin = std::sregex_iterator(pattern.begin(), pattern.end(), named_group_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i)
    {
        std::smatch match = *i;
        std::string name = match[1].str(); // The captured group name
        config.field_mapping[name] = name; // Assume group name maps to field name
    }
    return config;
}

// New: Factory method for JSON log format
ParsingConfig ParsingConfig::jsonLogFormat() {
    ParsingConfig config;
    // Regex to capture the entire line.
    config.line_pattern = "(.*)";
    // Map the captured group (the entire line) to a special "json_message" field
    // which will then be processed by a custom parser.
    // The "1" refers to the first (and only) capture group in the regex (.*).
    config.field_mapping["1"] = "json_message"; 
    
    // Custom parser for "json_message" field
    // This parser takes the string (the entire log line) and attempts to parse it as JSON.
    // If successful, it extracts attributes from the top-level JSON object.
    config.custom_parsers["json_message"] = [](std::string_view sv) -> LogValue {
        // LogEntry::fromJson already exists and can convert JSON string to LogEntry.
        // We need to extract the attributes from that LogEntry into a LogObject.
        auto expected_entry = LogEntry::fromJson(sv);
        if (expected_entry) {
            LogObject obj;
            // Iterate over the attributes already parsed by LogEntry::fromJson
            for (const auto& [key, value] : expected_entry->attributes) {
                obj[key] = value;
            }
            // Add standard LogEntry fields if they were parsed and are not empty/unknown
            if (!expected_entry->timestamp.empty() && expected_entry->time_point.time_since_epoch().count() != 0) {
                 obj["timestamp"] = LogValue(expected_entry->timestamp);
            }
            if (!expected_entry->message.empty()) {
                obj["message"] = LogValue(expected_entry->message);
            }
            if (expected_entry->level != LogLevel::UNKNOWN) {
                obj["level"] = LogValue(LogEntry::levelToString(expected_entry->level));
            }
            if (!expected_entry->thread_id.empty()) {
                obj["thread_id"] = LogValue(expected_entry->thread_id);
            }
            if (!expected_entry->source_file.empty()) {
                obj["source_file"] = LogValue(expected_entry->source_file);
            }
            if (expected_entry->source_line != 0) {
                obj["source_line"] = LogValue((int64_t)expected_entry->source_line);
            }
            
            return LogValue(obj);
        }
        // If JSON parsing fails, return a string LogValue of the original content.
        return LogValue(sv);
    };
    
    // Set strict mode to false for JSON logs, so that lines not matching JSON are not discarded
    config.strict_mode = false;
    
    // The parseLogLine method will need to be enhanced to check for this "json_message"
    // and correctly extract the attributes from the returned LogObject into the LogEntry.
    return config;
}

// --- Filters Implementation ---

namespace Filters
{
    class LevelPredicate : public LogAnalysis::LogPredicate
    {
        ::LogLevel level_;

    public:
        explicit LevelPredicate(::LogLevel l) : level_(l) {}
        bool test(const ::LogEntry &entry) const override { return entry.level == level_; }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<LevelPredicate>(level_); }
    };

    class KeywordPredicate : public LogAnalysis::LogPredicate
    {
        std::string keyword_;
        bool case_sensitive_;

    public:
        KeywordPredicate(std::string k, bool cs) : keyword_(std::move(k)), case_sensitive_(cs) {}
        bool test(const ::LogEntry &entry) const override
        {
            if (case_sensitive_)
            {
                return entry.message.find(keyword_) != std::string::npos;
            }
            else
            {
                auto it = std::search(
                    entry.message.begin(), entry.message.end(),
                    keyword_.begin(), keyword_.end(),
                    [](char ch1, char ch2)
                    {
                        return std::toupper(static_cast<unsigned char>(ch1)) ==
                               std::toupper(static_cast<unsigned char>(ch2));
                    });
                return it != entry.message.end();
            }
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<KeywordPredicate>(keyword_, case_sensitive_); }
    };

    class RegexPredicate : public LogAnalysis::LogPredicate
    {
        std::regex regex_;
        std::string pattern_;

    public:
        explicit RegexPredicate(std::string p) : regex_(p), pattern_(std::move(p)) {}
        bool test(const ::LogEntry &entry) const override
        {
            return std::regex_search(entry.message, regex_);
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<RegexPredicate>(pattern_); }
    };

    class AttributePredicate : public LogAnalysis::LogPredicate
    {
        std::string key_;
        ::LogValue value_;

    public:
        AttributePredicate(std::string k, ::LogValue v) : key_(std::move(k)), value_(std::move(v)) {}
        bool test(const ::LogEntry &entry) const override
        {
            auto it = entry.attributes.find(key_);
            return it != entry.attributes.end() && it->second == value_;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<AttributePredicate>(key_, value_); }
    };

    class AttributeRangePredicate : public LogAnalysis::LogPredicate
    {
        std::string key_;
        ::LogValue min_, max_;

    public:
        AttributeRangePredicate(std::string k, ::LogValue min, ::LogValue max)
            : key_(std::move(k)), min_(std::move(min)), max_(std::move(max)) {}

        bool test(const ::LogEntry &entry) const override
        {
            auto it = entry.attributes.find(key_);
            if (it == entry.attributes.end())
                return false;

            const auto &val = it->second;

            if (val.index() == min_.index() && val.index() == max_.index())
            {
                return val >= min_ && val <= max_;
            }

            auto to_double = [](const ::LogValue &v) -> std::optional<double>
            {
                return std::visit([](auto &&arg) -> std::optional<double>
                                  {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_arithmetic_v<T>) return static_cast<double>(arg);
                    else return std::nullopt; }, v);
            };

            auto v_d = to_double(val);
            auto min_d = to_double(min_);
            auto max_d = to_double(max_);

            if (v_d && min_d && max_d)
            {
                return *v_d >= *min_d && *v_d <= *max_d;
            }
            return false;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<AttributeRangePredicate>(key_, min_, max_); }
    };

    class SincePredicate : public LogAnalysis::LogPredicate
    {
        std::chrono::system_clock::duration duration_;
        mutable std::optional<std::chrono::system_clock::time_point> reference_time_;

    public:
        explicit SincePredicate(std::chrono::system_clock::duration d) : duration_(d) {}
        bool test(const ::LogEntry &entry) const override
        {
            if (!reference_time_)
            {
                reference_time_ = std::chrono::system_clock::now();
            }
            return entry.time_point >= (*reference_time_ - duration_);
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<SincePredicate>(duration_); }
    };

    class AnyKeywordPredicate : public LogAnalysis::LogPredicate
    {
        std::vector<std::string> keywords_;
        bool case_sensitive_;

    public:
        AnyKeywordPredicate(std::vector<std::string> kw, bool cs) : keywords_(std::move(kw)), case_sensitive_(cs) {}
        bool test(const ::LogEntry &entry) const override
        {
            for (const auto &kw : keywords_)
            {
                if (case_sensitive_)
                {
                    if (entry.message.find(kw) != std::string::npos)
                        return true;
                }
                else
                {
                    auto it = std::search(
                        entry.message.begin(), entry.message.end(),
                        kw.begin(), kw.end(),
                        [](char ch1, char ch2)
                        {
                            return std::toupper(static_cast<unsigned char>(ch1)) ==
                                   std::toupper(static_cast<unsigned char>(ch2));
                        });
                    if (it != entry.message.end())
                        return true;
                }
            }
            return false;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<AnyKeywordPredicate>(keywords_, case_sensitive_); }
    };

    class AndPredicate : public LogAnalysis::LogPredicate
    {
        std::unique_ptr<LogAnalysis::LogPredicate> a_, b_;

    public:
        AndPredicate(std::unique_ptr<LogAnalysis::LogPredicate> a, std::unique_ptr<LogAnalysis::LogPredicate> b)
            : a_(std::move(a)), b_(std::move(b)) {}
        bool test(const ::LogEntry &entry) const override { return a_->test(entry) && b_->test(entry); }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<AndPredicate>(a_->clone(), b_->clone()); }
    };

    class OrPredicate : public LogAnalysis::LogPredicate
    {
        std::unique_ptr<LogAnalysis::LogPredicate> a_, b_;

    public:
        OrPredicate(std::unique_ptr<LogAnalysis::LogPredicate> a, std::unique_ptr<LogAnalysis::LogPredicate> b)
            : a_(std::move(a)), b_(std::move(b)) {}
        bool test(const ::LogEntry &entry) const override { return a_->test(entry) || b_->test(entry); }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<OrPredicate>(a_->clone(), b_->clone()); }
    };

    class NotPredicate : public LogAnalysis::LogPredicate
    {
        std::unique_ptr<LogAnalysis::LogPredicate> p_;

    public:
        explicit NotPredicate(std::unique_ptr<LogAnalysis::LogPredicate> p) : p_(std::move(p)) {}
        bool test(const ::LogEntry &entry) const override { return !p_->test(entry); }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<NotPredicate>(p_->clone()); }
    };

    class MinLevelPredicate : public LogAnalysis::LogPredicate
    {
        ::LogLevel min_level_;

    public:
        explicit MinLevelPredicate(::LogLevel l) : min_level_(l) {}
        bool test(const ::LogEntry &entry) const override { return entry.level != ::LogLevel::UNKNOWN && entry.level >= min_level_; }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<MinLevelPredicate>(min_level_); }
    };

    class MultiLevelPredicate : public LogAnalysis::LogPredicate
    {
        std::vector<::LogLevel> levels_;

    public:
        explicit MultiLevelPredicate(std::vector<::LogLevel> l) : levels_(std::move(l)) {}
        bool test(const ::LogEntry &entry) const override
        {
            return std::find(levels_.begin(), levels_.end(), entry.level) != levels_.end();
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<MultiLevelPredicate>(levels_); }
    };

    class TimeRangePredicate : public LogAnalysis::LogPredicate
    {
        std::optional<std::chrono::system_clock::time_point> start_, end_;
        std::optional<std::string> start_s_, end_s_;

    public:
        TimeRangePredicate(std::optional<std::chrono::system_clock::time_point> s, std::optional<std::chrono::system_clock::time_point> e,
                           std::optional<std::string> ss = {}, std::optional<std::string> es = {})
            : start_(s), end_(e), start_s_(ss), end_s_(es) {}
        bool test(const ::LogEntry &entry) const override
        {
            if (start_ && entry.time_point < *start_)
                return false;
            if (end_ && entry.time_point > *end_)
                return false;
            if (start_s_ && entry.timestamp < *start_s_)
                return false;
            if (end_s_ && entry.timestamp > *end_s_)
                return false;
            return true;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<TimeRangePredicate>(start_, end_, start_s_, end_s_); }
    };

    class TagPredicate : public LogAnalysis::LogPredicate
    {
        std::set<std::string> tags_;

    public:
        explicit TagPredicate(std::set<std::string> t) : tags_(std::move(t)) {}
        bool test(const ::LogEntry &entry) const override
        {
            for (const auto &tag : tags_)
                if (!entry.hasTag(tag))
                    return false;
            return true;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<TagPredicate>(tags_); }
    };

    class MetadataPredicate : public LogAnalysis::LogPredicate
    {
        std::optional<std::string> file_, tid_;

    public:
        MetadataPredicate(std::optional<std::string> f, std::optional<std::string> t) : file_(f), tid_(t) {}
        bool test(const ::LogEntry &entry) const override
        {
            if (file_ && entry.source_file != *file_)
                return false;
            if (tid_ && entry.thread_id != *tid_)
                return false;
            return true;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<MetadataPredicate>(file_, tid_); }
    };

    std::unique_ptr<LogAnalysis::LogPredicate> Level(::LogLevel l) { return std::make_unique<LevelPredicate>(l); }
    std::unique_ptr<LogAnalysis::LogPredicate> MinLevel(::LogLevel l) { return std::make_unique<MinLevelPredicate>(l); }
    std::unique_ptr<LogAnalysis::LogPredicate> MultiLevel(std::vector<::LogLevel> levels) { return std::make_unique<MultiLevelPredicate>(std::move(levels)); }

    std::unique_ptr<LogAnalysis::LogPredicate> TimeRange(std::optional<std::chrono::system_clock::time_point> start, std::optional<std::chrono::system_clock::time_point> end)
    {
        return std::make_unique<TimeRangePredicate>(start, end);
    }

    std::unique_ptr<LogAnalysis::LogPredicate> Tag(std::string tag) { return std::make_unique<TagPredicate>(std::set<std::string>{std::move(tag)}); }
    std::unique_ptr<LogAnalysis::LogPredicate> ThreadId(std::string tid) { return std::make_unique<MetadataPredicate>(std::nullopt, std::move(tid)); }
    std::unique_ptr<LogAnalysis::LogPredicate> SourceFile(std::string file) { return std::make_unique<MetadataPredicate>(std::move(file), std::nullopt); }

    std::unique_ptr<LogAnalysis::LogPredicate> Keyword(std::string k, bool case_sensitive)
    {
        return std::make_unique<KeywordPredicate>(std::move(k), case_sensitive);
    }

    std::unique_ptr<LogAnalysis::LogPredicate> Regex(std::string pattern)
    {
        return std::make_unique<RegexPredicate>(std::move(pattern));
    }

    std::unique_ptr<LogAnalysis::LogPredicate> Attribute(std::string key, ::LogValue val)
    {
        return std::make_unique<AttributePredicate>(std::move(key), std::move(val));
    }

    std::unique_ptr<LogAnalysis::LogPredicate> AttributeRange(std::string key, ::LogValue min, ::LogValue max)
    {
        return std::make_unique<AttributeRangePredicate>(std::move(key), std::move(min), std::move(max));
    }

    std::unique_ptr<LogAnalysis::LogPredicate> Since(std::chrono::system_clock::duration d)
    {
        return std::make_unique<SincePredicate>(d);
    }

    std::unique_ptr<LogAnalysis::LogPredicate> AnyKeyword(std::vector<std::string> keywords, bool case_sensitive)
    {
        return std::make_unique<AnyKeywordPredicate>(std::move(keywords), case_sensitive);
    }

    std::unique_ptr<LogAnalysis::LogPredicate> And(std::unique_ptr<LogAnalysis::LogPredicate> a, std::unique_ptr<LogAnalysis::LogPredicate> b)
    {
        return std::make_unique<AndPredicate>(std::move(a), std::move(b));
    }

    std::unique_ptr<LogAnalysis::LogPredicate> Or(std::unique_ptr<LogAnalysis::LogPredicate> a, std::unique_ptr<LogAnalysis::LogPredicate> b)
    {
        return std::make_unique<OrPredicate>(std::move(a), std::move(b));
    }

    std::unique_ptr<LogAnalysis::LogPredicate> Not(std::unique_ptr<LogAnalysis::LogPredicate> p)
    {
        return std::make_unique<NotPredicate>(std::move(p));
    }

    // Variadic Or predicate implementation
    class OrManyPredicate : public LogAnalysis::LogPredicate {
        std::vector<std::unique_ptr<LogAnalysis::LogPredicate>> predicates_;
    public:
        explicit OrManyPredicate(std::vector<std::unique_ptr<LogAnalysis::LogPredicate>> preds) : predicates_(std::move(preds)) {}
        bool test(const ::LogEntry& entry) const override {
            for (const auto& p : predicates_) {
                if (p->test(entry)) return true;
            }
            return false;
        }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override {
            std::vector<std::unique_ptr<LogAnalysis::LogPredicate>> cloned_preds;
            for (const auto& p : predicates_) {
                cloned_preds.push_back(p->clone());
            }
            return std::make_unique<OrManyPredicate>(std::move(cloned_preds));
        }
    };

    template<typename... Predicates>
    std::unique_ptr<LogAnalysis::LogPredicate> Or(Predicates&&... preds) {
        std::vector<std::unique_ptr<LogAnalysis::LogPredicate>> all_preds;
        (all_preds.push_back(std::forward<Predicates>(preds)), ...);
        return std::make_unique<OrManyPredicate>(std::move(all_preds));
    }


    class HasAttributePredicate : public LogAnalysis::LogPredicate
    {
        std::string attribute_name_;
    public:
        explicit HasAttributePredicate(std::string name) : attribute_name_(std::move(name)) {}
        bool test(const ::LogEntry &entry) const override { return entry.attributes.count(attribute_name_) > 0; }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<HasAttributePredicate>(attribute_name_); }
    };

    class NotHasAttributePredicate : public LogAnalysis::LogPredicate
    {
        std::string attribute_name_;
    public:
        explicit NotHasAttributePredicate(std::string name) : attribute_name_(std::move(name)) {}
        bool test(const ::LogEntry &entry) const override { return entry.attributes.count(attribute_name_) == 0; }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<NotHasAttributePredicate>(attribute_name_); }
    };

    class NotHasTagPredicate : public LogAnalysis::LogPredicate
    {
        std::string tag_;
    public:
        explicit NotHasTagPredicate(std::string tag) : tag_(std::move(tag)) {}
        bool test(const ::LogEntry &entry) const override { return !entry.hasTag(tag_); }
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<NotHasTagPredicate>(tag_); }
    };

    class AttributeConditionPredicate : public LogAnalysis::LogPredicate
    {
        std::string attribute_name_;
        LogAnalysis::AttributeFilterCondition condition_;
        bool case_sensitive_; // NEW: Member to store case sensitivity

        bool compareStrings(std::string_view entry_str, std::string_view condition_str, StringComparisonOp op, bool current_case_sensitive) const { // Renamed parameter
            // Normalize to lower case if not case-sensitive
            std::string normalized_entry_str(entry_str);
            std::string normalized_condition_str(condition_str);

            if (!current_case_sensitive) {
                // If it's a REGEX, and it's explicitly case-sensitive, don't lower case.
                // Otherwise, lower case.
                if (op != StringComparisonOp::REGEX || (op == StringComparisonOp::REGEX && !current_case_sensitive)) {
                     std::transform(normalized_entry_str.begin(), normalized_entry_str.end(), normalized_entry_str.begin(), ::tolower);
                     std::transform(normalized_condition_str.begin(), normalized_condition_str.end(), normalized_condition_str.begin(), ::tolower);
                }
            }

            switch (op) {
                case StringComparisonOp::EQUALS:
                    return normalized_entry_str == normalized_condition_str;
                case StringComparisonOp::CONTAINS:
                    return normalized_entry_str.find(normalized_condition_str) != std::string_view::npos;
                case StringComparisonOp::STARTS_WITH:
                    return normalized_entry_str.rfind(normalized_condition_str, 0) == 0;
                case StringComparisonOp::ENDS_WITH:
                    return normalized_entry_str.length() >= normalized_condition_str.length() && normalized_entry_str.compare(normalized_entry_str.length() - normalized_condition_str.length(), normalized_condition_str.length(), normalized_condition_str) == 0;
                case StringComparisonOp::REGEX: {
                    try {
                        std::regex r(std::string(condition_str), current_case_sensitive ? std::regex_constants::ECMAScript : std::regex_constants::ECMAScript | std::regex_constants::icase);
                        return std::regex_search(std::string(entry_str), r);
                    } catch (const std::regex_error& e) {
                        std::cerr << "Regex error in AttributeConditionPredicate: " << e.what() << std::endl;
                        return false;
                    }
                }
            }
            return false;
        }

        bool compareNumerics(double entry_val, double condition_val, NumericComparisonOp op) const {
            switch (op) {
                case NumericComparisonOp::EQUALS:
                    return entry_val == condition_val;
                case NumericComparisonOp::NOT_EQUALS:
                    return entry_val != condition_val;
                case NumericComparisonOp::GT:
                    return entry_val > condition_val;
                case NumericComparisonOp::LT:
                    return entry_val < condition_val;
                case NumericComparisonOp::GTE:
                    return entry_val >= condition_val;
                case NumericComparisonOp::LTE:
                    return entry_val <= condition_val;
            }
            return false;
        }

    public:
        // MODIFIED: Constructor now takes case_sensitive
        AttributeConditionPredicate(std::string name, LogAnalysis::AttributeFilterCondition condition, bool cs)
            : attribute_name_(std::move(name)), condition_(std::move(condition)), case_sensitive_(cs) {}

        bool test(const ::LogEntry &entry) const override {
            auto it = entry.attributes.find(attribute_name_);
            if (it == entry.attributes.end()) {
                return false;
            }

            const auto& entry_value = it->second;
            const auto& condition_value = condition_.value;

            // Handle string comparisons
            if (condition_.string_op) {
                // Attempt to get string view from LogValue
                std::optional<std::string> temp_entry_str; // For holding converted numeric to string
                std::optional<std::string_view> entry_str_view;
                if (auto p_str_optional = entry_value.asString(); p_str_optional.has_value()) { // p_str_optional is optional<const string*>
                    entry_str_view.emplace(**p_str_optional); // **p_str_optional is const string&
                } else if (entry_value.is(ValueType::Int64)) {
                    temp_entry_str = std::to_string(entry_value.get<int64_t>());
                    entry_str_view.emplace(temp_entry_str.value());
                } else if (entry_value.is(ValueType::UInt64)) {
                    temp_entry_str = std::to_string(entry_value.get<uint64_t>());
                    entry_str_view.emplace(temp_entry_str.value());
                } else if (entry_value.is(ValueType::Double)) {
                    temp_entry_str = std::to_string(entry_value.get<double>());
                    entry_str_view.emplace(temp_entry_str.value());
                }

                std::optional<std::string> temp_condition_str; // For holding converted numeric to string
                std::optional<std::string_view> condition_str_view;
                if (auto p_str_optional = condition_value.asString(); p_str_optional.has_value()) {
                    condition_str_view.emplace(**p_str_optional);
                } else if (condition_value.is(ValueType::Int64)) {
                    temp_condition_str = std::to_string(condition_value.get<int64_t>());
                    condition_str_view.emplace(temp_condition_str.value());
                } else if (condition_value.is(ValueType::UInt64)) {
                    temp_condition_str = std::to_string(condition_value.get<uint64_t>());
                    condition_str_view.emplace(temp_condition_str.value());
                } else if (condition_value.is(ValueType::Double)) {
                    temp_condition_str = std::to_string(condition_value.get<double>());
                    condition_str_view.emplace(temp_condition_str.value());
                }


                if (entry_str_view && condition_str_view) {
                    return compareStrings(*entry_str_view, *condition_str_view, *condition_.string_op, case_sensitive_); // Use case_sensitive_
                }
            } else if (condition_.numeric_op) {
                auto entry_double_opt = entry_value.asDouble();
                auto condition_double_opt = condition_value.asDouble();

                if (entry_double_opt && condition_double_opt) {
                    return compareNumerics(*entry_double_opt, *condition_double_opt, *condition_.numeric_op);
                }
            }
            // Fallback for exact match if no specific operator is provided, or type mismatch, or no conversion possible.
            // This also handles cases where condition_.string_op or condition_.numeric_op are not set,
            // meaning a direct LogValue comparison is intended.
            return entry_value == condition_value;
        }
        // MODIFIED: clone() also passes case_sensitive_
        std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<AttributeConditionPredicate>(attribute_name_, condition_, case_sensitive_); }
    };


    std::unique_ptr<LogAnalysis::LogPredicate> HasAttribute(std::string attribute_name) { return std::make_unique<HasAttributePredicate>(std::move(attribute_name)); }
    std::unique_ptr<LogAnalysis::LogPredicate> NotHasAttribute(std::string attribute_name) { return std::make_unique<NotHasAttributePredicate>(std::move(attribute_name)); }
    std::unique_ptr<LogAnalysis::LogPredicate> Attribute(std::string attribute_name, AttributeFilterCondition condition, bool case_sensitive) { // Modified factory function
        return std::make_unique<AttributeConditionPredicate>(std::move(attribute_name), std::move(condition), case_sensitive);
    }
    std::unique_ptr<LogAnalysis::LogPredicate> NotHasTag(std::string tag) { return std::make_unique<NotHasTagPredicate>(std::move(tag)); }

    struct Token
    {
        enum Type
        {
            FIELD,
            OPERATOR,
            LITERAL,
            LOGICAL,
            LPAREN,
            RPAREN,
            END
        };
        Type type;
        std::string value;
    };


    std::vector<Token> tokenize(std::string_view query)
    {
        std::vector<Token> tokens;
        for (size_t i = 0; i < query.size(); ++i)
        {
            if (std::isspace(query[i]))
                continue;
            if (query[i] == '(')
                tokens.push_back({Token::LPAREN, "("});
            else if (query[i] == ')')
                tokens.push_back({Token::RPAREN, ")"});
            else if (query[i] == '=')
                tokens.push_back({Token::OPERATOR, "="});
            else if (query[i] == '\'')
            {
                std::string s;
                i++;
                while (i < query.size() && query[i] != '\'')
                    s += query[i++];
                tokens.push_back({Token::LITERAL, s});
            }
            else
            {
                std::string s;
                while (i < query.size() && !std::isspace(query[i]) && query[i] != '(' && query[i] != ')' && query[i] != '=')
                    s += query[i++];
                if (s == "AND" || s == "OR" || s == "NOT")
                    tokens.push_back({Token::LOGICAL, s});
                else if (s == "CONTAINS" || s == "MATCHES")
                    tokens.push_back({Token::OPERATOR, s});
                else if (s == "level" || s == "message" || s == "thread_id" || s == "file")
                    tokens.push_back({Token::FIELD, s});
                else
                    tokens.push_back({Token::LITERAL, s});
                i--;
            }
        }
        tokens.push_back({Token::END, ""});
        return tokens;
    }

    struct Parser
    {
        std::vector<Token> tokens;
        size_t pos = 0;

        Token peek() { return tokens[pos]; }
        Token consume() { return tokens[pos++]; }

        std::expected<std::unique_ptr<LogAnalysis::LogPredicate>, std::string> parseExpr() { return parseOr(); }

        std::expected<std::unique_ptr<LogAnalysis::LogPredicate>, std::string> parseOr()
        {
            auto left_res = parseAnd();
            if (!left_res)
                return left_res;
            auto left = std::move(*left_res);
            while (peek().type == Token::LOGICAL && peek().value == "OR")
            {
                consume();
                auto right_res = parseAnd();
                if (!right_res)
                    return right_res;
                left = LogAnalysis::Filters::Or(std::move(left), std::move(*right_res));
            }
            return left;
        }

        std::expected<std::unique_ptr<LogAnalysis::LogPredicate>, std::string> parseAnd()
        {
            auto left_res = parseUnary();
            if (!left_res)
                return left_res;
            auto left = std::move(*left_res);
            while (peek().type == Token::LOGICAL && peek().value == "AND")
            {
                consume();
                auto right_res = parseUnary();
                if (!right_res)
                    return right_res;
                left = LogAnalysis::Filters::And(std::move(left), std::move(*right_res));
            }
            return left;
        }

        std::expected<std::unique_ptr<LogAnalysis::LogPredicate>, std::string> parseUnary()
        {
            if (peek().type == Token::LOGICAL && peek().value == "NOT")
            {
                consume();
                auto p_res = parseUnary();
                if (!p_res)
                    return p_res;
                return LogAnalysis::Filters::Not(std::move(*p_res));
            }
            return parsePrimary();
        }

        std::expected<std::unique_ptr<LogAnalysis::LogPredicate>, std::string> parsePrimary()
        {
            if (peek().type == Token::LPAREN)
            {
                consume();
                auto p_res = parseExpr();
                if (!p_res)
                    return p_res;
                if (consume().type != Token::RPAREN)
                    return std::unexpected("Expected )");
                return p_res;
            }
            if (peek().type == Token::FIELD)
            {
                auto field = consume().value;
                if (peek().type != Token::OPERATOR)
                    return std::unexpected("Expected operator after field: " + field);
                auto op = consume().value;
                if (peek().type != Token::LITERAL)
                    return std::unexpected("Expected literal after operator: " + op);
                auto val = consume().value;

                if (field == "level")
                    return LogAnalysis::Filters::Level(::LogEntry::parseLevel(val));
                if (field == "message")
                {
                    if (op == "=" || op == "CONTAINS")
                        return LogAnalysis::Filters::Keyword(val);
                    if (op == "MATCHES")
                        return LogAnalysis::Filters::Regex(val);
                }
                if (field == "thread_id")
                    return LogAnalysis::Filters::ThreadId(val);
                if (field == "file")
                    return LogAnalysis::Filters::SourceFile(val);

                return LogAnalysis::Filters::Attribute(field, val);
            }
            return std::unexpected("Unexpected token: " + peek().value);
        }
    };

    std::expected<std::unique_ptr<LogAnalysis::LogPredicate>, std::string> fromQuery(std::string_view query_string)
    {
        auto tokens = tokenize(query_string);
        Parser p{std::move(tokens)};
        return p.parseExpr();
    }
}

std::unique_ptr<LogAnalysis::LogPredicate> LogAnalysis::FilterOptions::toPredicate() const
{
    std::unique_ptr<LogAnalysis::LogPredicate> root = nullptr;
    auto combine = [&](std::unique_ptr<LogAnalysis::LogPredicate> next)
    {
        if (!root)
            root = std::move(next);
        else
            root = Filters::And(std::move(root), std::move(next));
    };

    // Use public filter factory functions
    if (level)
        combine(Filters::MinLevel(*level));
    if (!levels.empty())
        combine(Filters::MultiLevel(levels));
    if (keyword)
        combine(Filters::Keyword(*keyword, case_sensitive));
    if (message_regex_pattern)
        combine(Filters::Regex(*message_regex_pattern));
    // When using TimeRange, pass time_points. start_time/end_time are for legacy string comparison in predicate.
    if (start_tp || end_tp || start_time || end_time)
        combine(Filters::TimeRange(start_tp, end_tp));
    if (source_file)
        combine(Filters::SourceFile(*source_file));
    if (thread_id)
        combine(Filters::ThreadId(*thread_id));
    if (!required_tags.empty())
    {
        for (const auto &tag : required_tags)
            combine(Filters::Tag(tag));
    }
    if (since)
        combine(Filters::Since(*since));
    if (!any_keywords.empty())
        combine(Filters::AnyKeyword(any_keywords, case_sensitive));

    // Handle existing attribute_matches as specific AttributeFilterConditions with EQUALS operator
    for (const auto &[k, v] : attribute_matches)
    {
        if (v.is(ValueType::String)) {
            combine(Filters::Attribute(k, AttributeFilterCondition(v, StringComparisonOp::EQUALS), case_sensitive));
        } else if (v.is(ValueType::Int64) || v.is(ValueType::UInt64) || v.is(ValueType::Double)) {
            combine(Filters::Attribute(k, AttributeFilterCondition(v, NumericComparisonOp::EQUALS), case_sensitive));
        } else {
            combine(Filters::Attribute(k, AttributeFilterCondition(v, NumericComparisonOp::EQUALS), case_sensitive)); // Default to numeric equals for others
        }
    }

    // NEW: Handle attribute_filter_conditions
    for (const auto& [attr_name, conditions] : attribute_filter_conditions) {
        if (!conditions.empty()) {
            std::vector<std::unique_ptr<LogPredicate>> or_group_preds;
            for (const auto& condition : conditions) {
                or_group_preds.push_back(Filters::Attribute(attr_name, condition, case_sensitive));
            }
            if (or_group_preds.size() == 1) {
                combine(std::move(or_group_preds[0]));
            } else if (or_group_preds.size() > 1) {
                combine(std::make_unique<LogAnalysis::Filters::OrManyPredicate>(std::move(or_group_preds)));
            }
        }
    }

    // NEW: Handle excluded_tags
    for (const auto& tag : excluded_tags) {
        combine(Filters::NotHasTag(tag));
    }

    // NEW: Handle attribute_or_matches
    if (!attribute_or_matches.empty()) {
        std::vector<std::unique_ptr<LogPredicate>> top_level_or_preds;
        for (const auto& and_group_map : attribute_or_matches) {
            std::unique_ptr<LogPredicate> and_group_root = nullptr;
            auto combine_and_group = [&](std::unique_ptr<LogPredicate> next) {
                if (!and_group_root) {
                    and_group_root = std::move(next);
                } else {
                    and_group_root = Filters::And(std::move(and_group_root), std::move(next));
                }
            };

            for (const auto &[k, v] : and_group_map)
            {
                if (v.is(ValueType::String)) {
                    combine_and_group(Filters::Attribute(k, AttributeFilterCondition(v, StringComparisonOp::EQUALS), case_sensitive));
                } else if (v.is(ValueType::Int64) || v.is(ValueType::UInt64) || v.is(ValueType::Double)) {
                    combine_and_group(Filters::Attribute(k, AttributeFilterCondition(v, NumericComparisonOp::EQUALS), case_sensitive));
                } else {
                    combine_and_group(Filters::Attribute(k, AttributeFilterCondition(v, NumericComparisonOp::EQUALS), case_sensitive)); // Default to numeric equals for others
                }
            }
            if (and_group_root) {
                top_level_or_preds.push_back(std::move(and_group_root));
            }
        }
        if (top_level_or_preds.size() == 1) {
            combine(std::move(top_level_or_preds[0]));
        } else if (top_level_or_preds.size() > 1) {
            combine(std::make_unique<LogAnalysis::Filters::OrManyPredicate>(std::move(top_level_or_preds)));
        }
    }


    if (!root)
    {
        struct AllPredicate : LogAnalysis::LogPredicate
        {
            bool test(const ::LogEntry &) const override { return true; }
            std::unique_ptr<LogAnalysis::LogPredicate> clone() const override { return std::make_unique<AllPredicate>(); }
        };
        root = std::make_unique<AllPredicate>();
    }

    if (invert_match)
        root = Filters::Not(std::move(root));
    return root;
}

// --- LogSource Implementation ---
LogAnalysis::LogSource::LogSource(const std::string& path, SourceType type, bool recursive)
    : path_(path), type_(type), recursive_(recursive) {
    if (type_ == SourceType::DIRECTORY) {
        resolveFilePaths();
    } else { // SourceType::FILE or SourceType::STD_IN
        resolved_file_paths_.push_back(path_);
    }
}

std::vector<std::string> LogAnalysis::LogSource::getFilePaths() const {
    if (type_ == SourceType::DIRECTORY && resolved_file_paths_.empty()) {
        const_cast<LogSource*>(this)->resolveFilePaths(); // Resolve if not already done
    }
    return resolved_file_paths_;
}

void LogAnalysis::LogSource::resolveFilePaths() const {
    resolved_file_paths_.clear(); // Clear previous paths
    if (type_ == SourceType::FILE || type_ == SourceType::STD_IN) {
        resolved_file_paths_.push_back(path_);
        return;
    }

    if (!std::filesystem::exists(path_) || !std::filesystem::is_directory(path_)) {
        // Handle error or throw exception for invalid directory
        std::cerr << "Warning: Directory not found or not a directory: " << path_ << std::endl;
        return;
    }

    if (recursive_) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(path_)) {
            if (std::filesystem::is_regular_file(entry.status())) {
                resolved_file_paths_.push_back(entry.path().string());
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(path_)) {
            if (std::filesystem::is_regular_file(entry.status())) {
                resolved_file_paths_.push_back(entry.path().string());
            }
        }
    }
}
// --- Exporters Implementation ---

void LogAnalysis::JsonExporter::exportStats(const LogStatistics &stats)
{
    out_ << "{\n";
    out_ << "  \"total_entries\": " << stats.total_entries << ",\n";
    out_ << "  \"duration_seconds\": " << stats.duration.count() << ",\n";
    out_ << "  \"level_counts\": {\n";
    bool first = true;
    for (const auto &[level, count] : stats.level_counts)
    {
        if (!first)
            out_ << ",\n";
        out_ << "    \"" << ::LogEntry::levelToString(level) << "\": " << count;
        first = false;
    }
    out_ << "\n  }\n}\n";
}

void LogAnalysis::JsonExporter::exportEntries(std::span<const ::LogEntry> entries)
{
    out_ << "[\n";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        out_ << entries[i].toJson({.pretty = pretty_, .include_fields = {}, .exclude_fields = {}});
        if (i < entries.size() - 1)
            out_ << ",\n";
    }
    out_ << "\n]\n";
}

void LogAnalysis::CsvExporter::exportStats(const LogStatistics &stats)
{
    out_ << "Metric,Value\n";
    out_ << "total_entries," << stats.total_entries << "\n";
    out_ << "duration_seconds," << stats.duration.count() << "\n";
}

void LogAnalysis::CsvExporter::exportEntries(std::span<const ::LogEntry> entries)
{
    out_ << "Timestamp,Level,Message,ThreadId\n";
    for (const auto &entry : entries)
    {
        out_ << "\"" << entry.timestamp << "\",";
        out_ << "\"" << ::LogEntry::levelToString(entry.level) << "\",";
        // Escape quotes in message
        std::string msg = entry.message;
        size_t pos = 0;
        while ((pos = msg.find("\"", pos)) != std::string::npos)
        {
            msg.replace(pos, 1, "\"\"");
            pos += 2;
        }
        out_ << "\"" << msg << "\",";
        out_ << "\"" << entry.thread_id << "\"\n";
    }
}

void LogAnalysis::MarkdownExporter::exportStats(const LogStatistics &stats)
{
    out_ << "# Log Analysis Statistics\n\n";
    out_ << "| Metric | Value |\n";
    out_ << "| :--- | :--- |\n";
    out_ << "| Total Entries | " << stats.total_entries << " |\n";
    out_ << "| Duration | " << stats.duration.count() << "s |\n";
    out_ << "| Entries/sec | " << std::fixed << std::setprecision(2) << stats.entries_per_second << " |\n";
    out_ << "\n## Log Levels\n\n";
    out_ << "| Level | Count |\n";
    out_ << "| :--- | :--- |\n";
    for (const auto &[level, count] : stats.level_counts)
    {
        out_ << "| " << ::LogEntry::levelToString(level) << " | " << count << " |\n";
    }
}

void LogAnalysis::MarkdownExporter::exportEntries(std::span<const ::LogEntry> entries)
{
    out_ << "| Timestamp | Level | Message |\n";
    out_ << "| :--- | :--- | :--- |\n";
    for (const auto &entry : entries)
    {
        out_ << "| " << entry.timestamp << " | " << ::LogEntry::levelToString(entry.level) << " | " << entry.message << " |\n";
    }
}

void LogAnalysis::ConsoleExporter::exportStats(const LogStatistics &stats)
{
    auto bold = use_color_ ? "\033[1m" : "";
    auto reset = use_color_ ? "\033[0m" : "";
    auto cyan = use_color_ ? "\033[36m" : "";

    out_ << bold << cyan << "=== Log Analysis Statistics ===" << reset << "\n";
    out_ << "Total entries: " << stats.total_entries << "\n";
    out_ << "Duration:      " << stats.duration.count() << "s\n";
    out_ << "Entries/sec:   " << std::fixed << std::setprecision(2) << stats.entries_per_second << "\n";
    out_ << "\n"
         << bold << "Log Level Distribution:" << reset << "\n";
    for (const auto &[level, count] : stats.level_counts)
    {
        out_ << "  " << std::left << std::setw(10) << ::LogEntry::levelToString(level) << ": " << count << "\n";
    }
}

void LogAnalysis::ConsoleExporter::exportEntries(std::span<const ::LogEntry> entries)
{
    for (const auto &entry : entries)
    {
        if (use_color_)
        {
            std::string color = "";
            switch (entry.level)
            {
            case ::LogLevel::ERROR:
            case ::LogLevel::CRITICAL:
                color = "\033[31m";
                break; // Red
            case ::LogLevel::WARNING:
                color = "\033[33m";
                break; // Yellow
            case ::LogLevel::INFO:
                color = "\033[32m";
                break; // Green
            case ::LogLevel::DEBUG:
                color = "\033[34m";
                break; // Blue
            default:
                break;
            }
            out_ << "\033[90m[" << entry.timestamp << "]\033[0m " << color << "[" << std::setw(7) << ::LogEntry::levelToString(entry.level) << "]\033[0m " << entry.message << "\n";
        }
        else
        {
            out_ << "[" << entry.timestamp << "] [" << std::setw(7) << ::LogEntry::levelToString(entry.level) << "] " << entry.message << "\n";
        }
    }
}

// --- LogAnonymizer Implementation ---

LogAnalysis::RegexAnonymizer::RegexAnonymizer(std::string_view pattern, std::string_view replacement, std::set<std::string> fields_to_anonymize)
    : pattern_(std::string(pattern)), replacement_(std::string(replacement)), fields_to_anonymize_(std::move(fields_to_anonymize)) {}

bool LogAnalysis::RegexAnonymizer::anonymize(::LogEntry &entry) const
{
    bool modified = false;
    auto anonymize_str = [&](std::string &s)
    {
        std::string new_s = std::regex_replace(s, pattern_, replacement_);
        if (new_s != s)
        {
            s = std::move(new_s);
            modified = true;
        }
    };

    if (fields_to_anonymize_.empty())
    {
        anonymize_str(entry.message);
        anonymize_str(entry.source_file);
        for (auto &[key, value] : entry.attributes)
        {
            if (auto p_str = value.get_if<std::string>())
            {
                anonymize_str(*p_str);
            }
        }
    }
    else
    {
        for (const auto &field : fields_to_anonymize_)
        {
            if (field == "message")
                anonymize_str(entry.message);
            else if (field == "thread_id")
                anonymize_str(entry.thread_id);
            else if (field == "source_file" || field == "file")
                anonymize_str(entry.source_file);
            else
            {
                auto it = entry.attributes.find(field);
                if (it != entry.attributes.end())
                {
                    if (auto p_str = it->second.get_if<std::string>())
                    {
                        anonymize_str(*p_str);
                    }
                }
            }
        }
    }
    return modified;
}

// --- LogAnalyzer Implementation ---

LogAnalysis::LogAnalyzer::LogAnalyzer() 
    : rw_mutex_ptr_(std::make_unique<std::shared_mutex>()), 
      legacy_timestamp_regex_(R"(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?)\]?)"),
      legacy_level_regex_(R"(\[?(DEBUG|INFO|WARNING|WARN|ERROR|ERR|CRITICAL|CRIT|FATAL)\]?)")
{
    // stop_tailing_ptr_ is already initialized with make_unique in header
}

// Static map to store registered parsing profiles
static std::map<std::string, LogAnalysis::ParsingConfig> s_parsingProfiles;

void LogAnalysis::LogAnalyzer::registerParsingProfile(const std::string& name, ParsingConfig config) {
    s_parsingProfiles[name] = std::move(config);
}

void LogAnalysis::LogAnalyzer::loadParsingProfile(const std::string& profileName) {
    auto it = s_parsingProfiles.find(profileName);
    if (it != s_parsingProfiles.end()) {
        setParsingConfig(it->second);
    } else {
        throw std::runtime_error("Unknown parsing profile: " + profileName);
    }
}

void LogAnalysis::LogAnalyzer::setParsingConfig(ParsingConfig config, bool reparseExisting)
{
    // Need to protect access to entries_ and config_
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->config_ = std::move(config);
    std::string processed_pattern = this->config_.line_pattern; // Use new config
    this->named_group_indices_.clear();

    if (!processed_pattern.empty())
    {
        std::string final_pattern;
        int current_group = 0;
        for (size_t i = 0; i < processed_pattern.size(); ++i)
        {
            if (processed_pattern[i] == '(')
            {
                if (i + 1 < processed_pattern.size() && processed_pattern[i + 1] == '?')
                {
                    if (i + 2 < processed_pattern.size() && processed_pattern[i + 2] == ':')
                    {
                        final_pattern += "(?:";
                        i += 2;
                    }
                    else if (i + 2 < processed_pattern.size() && processed_pattern[i + 2] == '<')
                    {
                        current_group++;
                        size_t end_bracket = processed_pattern.find('>', i + 3);
                        if (end_bracket != std::string::npos)
                        {
                            std::string name = processed_pattern.substr(i + 3, end_bracket - (i + 3));
                            this->named_group_indices_[name] = current_group;
                            final_pattern += '(';
                            i = end_bracket;
                        }
                        else
                        {
                            final_pattern += '(';
                        }
                    }
                    else
                    {
                        current_group++;
                        final_pattern += '(';
                    }
                }
                else
                {
                    current_group++;
                    final_pattern += '(';
                }
            }
            else
            {
                final_pattern += processed_pattern[i];
            }
        }

        try
        {
            this->strict_regex_ = std::regex(final_pattern);
        }
        catch (const std::regex_error &e)
        {
            std::cerr << "Invalid regex pattern: " << e.what() << " (processed from " << this->config_.line_pattern << ")" << std::endl;
        }
    }

    if (this->config_.entry_start_pattern)
    {
        try
        {
            this->entry_start_regex_ = std::regex(*this->config_.entry_start_pattern);
        }
        catch (const std::regex_error &e)
        {
            std::cerr << "Invalid entry start pattern: " << e.what() << std::endl;
        }
    }

    if (reparseExisting) {
        std::vector<LogEntry> reprocessed_entries;
        reprocessed_entries.reserve(this->entries_.size());
        for (const auto& entry : this->entries_) {
            reprocessed_entries.push_back(parseLogLine(entry.raw_line, 0)); // Re-parse from raw_line
        }
        this->entries_ = std::move(reprocessed_entries);
        // Re-apply enrichers and anonymizers after re-parsing
        for (auto& entry : this->entries_) {
            applyEnrichers(entry);
            applyAnonymizers(entry);
        }
    }
    this->cached_stats_.reset(); // Invalidate cache regardless of reparseExisting
}

void LogAnalysis::LogAnalyzer::setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex)
{
    this->legacy_timestamp_regex_ = std::regex(std::string(timestamp_regex));
    this->legacy_level_regex_ = std::regex(std::string(level_regex));
    this->config_.line_pattern.clear();
    this->named_group_indices_.clear();
}

void LogAnalysis::LogAnalyzer::setAnalysisConfig(AnalysisConfig config, bool reanalyzeExisting)
{
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->currentAnalysisConfig_ = std::move(config);
    if (reanalyzeExisting) {
        this->cached_stats_.reset(); // Invalidate cache to force re-analysis
    }
}

void LogAnalysis::LogAnalyzer::clearAnalysisConfig()
{
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->currentAnalysisConfig_.reset();
    this->cached_stats_.reset();
}

std::expected<std::pair<LoadResult, std::vector<::LogEntry>>, std::string> LogAnalyzer::loadFileWithStats(
    const std::filesystem::path &filepath,
    LogAnalysis::ProgressCallback progress)
{
    LoadResult result = {0, 0};
    std::vector<::LogEntry> new_entries;

    try
    {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            return std::unexpected("Could not open file: " + filepath.string());
        }

        size_t total_bytes = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string line;
        size_t line_number = 0;
        size_t bytes_processed = 0;

        std::string current_entry_buffer;
        size_t entry_line_start = 1;

        auto process_buffer = [&]()
        {
            if (current_entry_buffer.empty())
                return;
            ::LogEntry entry = parseLogLine(current_entry_buffer, entry_line_start);
            if (this->config_.strict_mode && entry.timestamp.empty() && entry.message.empty())
            {
                result.error_count++;
            }
            else
            {
                this->applyEnrichers(entry);
                this->applyAnonymizers(entry);
                new_entries.push_back(std::move(entry));
                result.loaded_count++;
            }
            current_entry_buffer.clear();
        };

        while (std::getline(file, line))
        {
            line_number++;
            size_t line_bytes = line.size() + 1;
            bytes_processed += line_bytes;

            if (progress && (line_number % 100 == 0 || bytes_processed >= total_bytes))
            {
                progress({bytes_processed, total_bytes, line_number});
            }

            if (line.empty())
                continue;
            if (line.back() == '\r')
                line.pop_back();

            bool is_new_entry = true;
            if (this->entry_start_regex_)
            {
                is_new_entry = std::regex_search(line, *this->entry_start_regex_);
            }

            if (is_new_entry)
            {
                process_buffer();
                current_entry_buffer = line;
                entry_line_start = line_number;
            }
            else
            { // Not a new entry
                // Apply max_continuation_lines limit
                if (current_entry_buffer.empty() ||
                    (this->config_.max_continuation_lines > 0 &&
                     static_cast<size_t>(std::count(current_entry_buffer.begin(), current_entry_buffer.end(), '\n')) >= this->config_.max_continuation_lines))
                {
                    // If buffer is empty or limit reached, treat current line as start of a new entry
                    process_buffer(); // Process the accumulated buffer as a full entry
                    current_entry_buffer = line;
                    entry_line_start = line_number;
                }
                else
                {
                    current_entry_buffer += "\n" + line;
                }
            }

            if (this->config_.max_errors > 0 && result.error_count >= this->config_.max_errors)
            {
                break;
            }
        }
        if (this->config_.max_errors == 0 || result.error_count < this->config_.max_errors)
        {
            process_buffer();
        }

        if (progress)
            progress({bytes_processed, total_bytes, line_number});

    }
    catch (const std::exception &e)
    {
        return std::unexpected(e.what());
    }

    return std::make_pair(result, std::move(new_entries));
}

std::expected<void, std::string> LogAnalysis::LogAnalyzer::loadFile(const std::filesystem::path &filepath)
{
    auto result_pair = loadFileWithStats(filepath);
    if (!result_pair)
        return std::unexpected(result_pair.error());
    
    // After successful load, move the entries into `this->entries_`
    {
        std::unique_lock lock(*this->rw_mutex_ptr_);
        this->entries_.insert(this->entries_.end(), std::make_move_iterator(result_pair->second.begin()), std::make_move_iterator(result_pair->second.end()));
        this->cached_stats_.reset();
    }
    return {};
}

std::future<LogAnalysis::LoadResult> LogAnalysis::LogAnalyzer::loadFileAsync(
    std::filesystem::path filepath,
    LogAnalysis::ProgressCallback progress)
{
    return std::async(std::launch::async, [this, filepath, progress]() -> LoadResult
                      {
        auto result_pair = this->loadFileWithStats(filepath, progress);
        if (result_pair) {
            // Aggregate entries from this single file load into the main this->entries_ vector
            std::unique_lock lock(*this->rw_mutex_ptr_);
            this->entries_.insert(this->entries_.end(), std::make_move_iterator(result_pair->second.begin()), std::make_move_iterator(result_pair->second.end()));
            this->cached_stats_.reset();
            return result_pair->first; // Return only the LoadResult part
        }
        throw std::runtime_error(result_pair.error()); });
}

std::future<LogAnalysis::LoadResult> LogAnalysis::LogAnalyzer::loadParallel(std::filesystem::path path, LogAnalysis::ParallelConfig config)
{
    return std::async(std::launch::async, [this, path, config]() -> LoadResult
                      {
        {
            std::unique_lock lock(*this->rw_mutex_ptr_);
            this->entries_.clear();
            this->cached_stats_.reset();
        }

        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Could not open file: " + path.string());
        
        size_t total_size = file.tellg();
        size_t chunk_size = config.chunk_size_mb * 1024 * 1024;
        
        std::vector<std::future<std::pair<std::vector<::LogEntry>, size_t>>> futures;
        std::atomic<size_t> total_errors = 0;

        size_t current_pos = 0;
        while (current_pos < total_size) {
            size_t start = current_pos;
            size_t end = std::min(start + chunk_size, total_size);

            if (end < total_size) {
                file.seekg(end);
                std::string temp;
                std::getline(file, temp);
                end = file.tellg();
                if (end == (size_t)-1) end = total_size;
            }

            futures.push_back(std::async(std::launch::async, [this, path, start, end, &total_errors]() {
                std::ifstream f(path, std::ios::binary);
                f.seekg(start);
                std::string line;
                std::vector<::LogEntry> chunk_entries;
                std::string buffer;
                size_t lines_read = 0;

                while (f.tellg() < static_cast<std::streampos>(end) && std::getline(f, line)) {
                    lines_read++;
                    bool is_new = true;
                    if (this->entry_start_regex_) is_new = std::regex_search(line, *this->entry_start_regex_);
                    
                    if (is_new && !buffer.empty()) {
                        ::LogEntry entry = this->parseLogLine(buffer);
                        if (!entry.timestamp.empty() || !entry.message.empty()) {
                            this->applyEnrichers(entry);
                            this->applyAnonymizers(entry);
                            chunk_entries.push_back(std::move(entry));
                        } else total_errors.fetch_add(1);
                        buffer = line;
                    } else {
                        if (!buffer.empty()) buffer += "\n";
                        buffer += line;
                    }
                }
                if (!buffer.empty()) {
                    ::LogEntry entry = this->parseLogLine(buffer);
                    if (!entry.timestamp.empty() || !entry.message.empty()) {
                        this->applyEnrichers(entry);
                        this->applyAnonymizers(entry);
                        chunk_entries.push_back(std::move(entry));
                    } else total_errors.fetch_add(1);
                }
                return std::make_pair(std::move(chunk_entries), lines_read);
            }));
            current_pos = end;
        }

        LoadResult res = {0, 0};
        size_t total_lines = 0;
        for (auto& f : futures) {
            auto [chunk_entries, lines] = f.get();
            res.loaded_count += chunk_entries.size();
            total_lines += lines;
            std::unique_lock lock(*this->rw_mutex_ptr_);
            this->entries_.insert(this->entries_.end(), std::make_move_iterator(chunk_entries.begin()), std::make_move_iterator(chunk_entries.end()));
        }
        res.error_count = total_errors.load();
        if (config.progress) config.progress({total_size, total_size, total_lines});
        return res;
    });
}

std::expected<LogAnalysis::LoadResult, std::string> LogAnalysis::LogAnalyzer::loadLogSources(const std::vector<LogAnalysis::LogSource>& sources, LogAnalysis::ProgressCallback progress) {
    LogAnalysis::LoadResult total_result = {0, 0};
    std::vector<::LogEntry> collected_entries; // Temporarily store entries from all sources

    for (const auto& source : sources) {
        if (source.getType() == LogAnalysis::LogSource::SourceType::STD_IN) {
            std::cerr << "Warning: STDIN source type not fully supported yet in loadLogSources. Skipping." << std::endl;
            total_result.error_count++;
            continue;
        }

        auto file_paths = source.getFilePaths();
        for (const auto& filepath_str : file_paths) {
            std::filesystem::path filepath(filepath_str);
            auto result_pair = loadFileWithStats(filepath, progress); // Use refactored loadFileWithStats
            if (result_pair) {
                total_result.loaded_count += result_pair->first.loaded_count;
                total_result.error_count += result_pair->first.error_count;
                // Move entries from the result of loadFileWithStats to collected_entries
                collected_entries.insert(collected_entries.end(), 
                                         std::make_move_iterator(result_pair->second.begin()), 
                                         std::make_move_iterator(result_pair->second.end()));
            } else {
                return std::unexpected(result_pair.error());
            }
        }
    }

    // Replace the analyzer's this->entries_ with the collected ones
    {
        std::unique_lock lock(*this->rw_mutex_ptr_);
        this->entries_ = std::move(collected_entries);
        this->cached_stats_.reset();
    }
    return total_result;
}

bool LogAnalysis::LogAnalyzer::loadLogFile(const std::string &filepath) { return LogAnalysis::LogAnalyzer::loadFile(std::filesystem::path(filepath)).has_value(); }
bool LogAnalysis::LogAnalyzer::loadLogFile(const std::filesystem::path &filepath)
{
    auto result = LogAnalysis::LogAnalyzer::loadFile(filepath);
    if (!result.has_value())
    {
        std::cerr << "Error: " << result.error() << std::endl;
        return false;
    }
    return true;
}

std::generator<::LogEntry> LogAnalysis::LogAnalyzer::streamEntries(std::filesystem::path filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        throw std::runtime_error("Could not open file: " + filepath.string());

    std::string line;
    std::string buffer;
    size_t line_number = 0;

    while (std::getline(file, line))
    {
        line_number++;
        bool is_new = true;
        if (this->entry_start_regex_)
            is_new = std::regex_search(line, *this->entry_start_regex_);

        if (is_new && !buffer.empty())
        {
            ::LogEntry entry = this->parseLogLine(buffer, line_number);
            this->applyEnrichers(entry);
            this->applyAnonymizers(entry);
            co_yield entry;
            buffer = line;
        }
        else
        {
            if (!buffer.empty())
                buffer += "\n";
            buffer += line;
        }
    }
    if (!buffer.empty())
    {
        ::LogEntry entry = this->parseLogLine(buffer, line_number);
        this->applyEnrichers(entry);
        this->applyAnonymizers(entry);
        co_yield entry;
    }
}

std::generator<::LogEntry> LogAnalysis::LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, FilterOptions options)
{
    auto pred = options.toPredicate();
    for (const auto &entry : streamEntries(filepath))
    {
        if (pred->test(entry))
            co_yield entry;
    }
}

std::generator<::LogEntry> LogAnalysis::LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, const LogAnalysis::LogPredicate &predicate)
{
    for (const auto &entry : streamEntries(filepath))
    {
        if (predicate.test(entry))
            co_yield entry;
    }
}

std::future<void> LogAnalysis::LogAnalyzer::tailFile(
    const std::filesystem::path &filepath,
    std::function<void(::LogEntry)> entry_callback,
    std::function<void(const ParseError &)> error_callback,
    std::function<bool()> stop_predicate)
{
    return std::async(std::launch::async, [this, filepath, entry_callback, error_callback, stop_predicate]()
                      {
        std::ifstream file(filepath);
        if (!file.is_open()) return;

        file.seekg(0, std::ios::end);
        auto last_pos = file.tellg();
        std::string entry_buffer;
        size_t line_count = 0;

        while (true) {
            if (stop_predicate && stop_predicate()) break;

            if (!std::filesystem::exists(filepath)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }

            file.clear();
            auto current_size = std::filesystem::file_size(filepath);
            if (current_size > static_cast<size_t>(last_pos)) {
                file.seekg(last_pos);
                std::string line;
                while (std::getline(file, line)) {
                    line_count++;
                    bool is_new = true;
                    if (this->entry_start_regex_) is_new = std::regex_search(line, *this->entry_start_regex_);

                    if (is_new && !entry_buffer.empty()) {
                        ::LogEntry entry = this->parseLogLine(entry_buffer, line_count);
                        this->applyEnrichers(entry);
                        this->applyAnonymizers(entry);
                        entry_callback(std::move(entry));
                        entry_buffer = line;
                    } else {
                        if (!entry_buffer.empty()) entry_buffer += "\n";
                        entry_buffer += line;
                    }
                }
                last_pos = file.tellg();
            } else if (current_size < static_cast<size_t>(last_pos)) {
                last_pos = 0;
                file.seekg(0);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!entry_buffer.empty()) {
            ::LogEntry entry = this->parseLogLine(entry_buffer, line_count);
            this->applyEnrichers(entry);
            this->applyAnonymizers(entry);
            entry_callback(std::move(entry));
        } });
}

std::generator<::LogEntry> LogAnalysis::LogAnalyzer::tailFileStream(
    const std::filesystem::path &filepath,
    std::function<void(const ParseError &)> error_callback)
{
    std::ifstream file(filepath);
    if (!file.is_open())
        co_return;

    file.seekg(0, std::ios::end);
    auto last_pos = file.tellg();
    std::string entry_buffer;
    size_t line_count = 0;

    while (true)
    {
        if (!std::filesystem::exists(filepath))
            break;
        file.clear();
        auto current_size = std::filesystem::file_size(filepath);

        if (current_size > static_cast<size_t>(last_pos))
        {
            file.seekg(last_pos);
            std::string line;
            while (std::getline(file, line))
            {
                line_count++;
                bool is_new = true;
                if (this->entry_start_regex_)
                    is_new = std::regex_search(line, *this->entry_start_regex_);

                if (is_new && !entry_buffer.empty())
                {
                    ::LogEntry entry = this->parseLogLine(entry_buffer, line_count);
                    this->applyEnrichers(entry);
                    this->applyAnonymizers(entry);
                    co_yield entry;
                    entry_buffer = line;
                }
                else
                {
                    if (!entry_buffer.empty())
                        entry_buffer += "\n";
                    entry_buffer += line;
                }
            }
            last_pos = file.tellg();
        }
        else if (current_size < static_cast<size_t>(last_pos))
        {
            last_pos = 0;
            file.seekg(0);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!entry_buffer.empty())
    {
        ::LogEntry entry = this->parseLogLine(entry_buffer, line_count);
        this->applyEnrichers(entry);
        this->applyAnonymizers(entry);
        co_yield entry;
    }
}

/**
 * @brief Starts tailing a log file, periodically checking for new entries and processing them.
 *        Processed entries are added to the analyzer's internal store.
 *        This operation can be stopped via `stopTailing()`.
 * @param path The path to the log file to tail.
 * @param interval The interval at which to check for new file content.
 * @return A future that completes when the tailing process is stopped.
 * @note This method typically runs in a separate thread.
 */
std::future<void> LogAnalysis::LogAnalyzer::startTailing(const std::filesystem::path& path, std::chrono::milliseconds interval) {
    // If a tailing thread is already running, stop it first
    if (tailing_thread_.joinable()) {
        stopTailing();
    }

    current_tail_path_ = path;
    tail_interval_ = interval;
    *this->stop_tailing_ptr_ = false;

    // Use a promise to return a future that completes when tailing stops
    auto promise = std::make_shared<std::promise<void>>();
    std::future<void> future = promise->get_future();

    tailing_thread_ = std::thread([this, promise]() {
        std::ifstream file(current_tail_path_);
        if (!file.is_open()) {
            // Signal an error or complete the promise exceptionally
            promise->set_exception(std::make_exception_ptr(std::runtime_error("Could not open file for tailing: " + current_tail_path_.string())));
            return;
        }

        file.seekg(0, std::ios::end); // Start at the end of the file
        auto last_pos = file.tellg();
        std::string entry_buffer;
        size_t line_count = 0; // Approximate line count for error reporting

        while (!*this->stop_tailing_ptr_) {
            std::this_thread::sleep_for(tail_interval_);

            if (!std::filesystem::exists(current_tail_path_)) {
                // File might have been deleted or moved. Try to re-open.
                std::cerr << "Tailing: File not found: " << current_tail_path_ << ". Retrying..." << std::endl;
                file.close();
                std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait a bit before trying to re-open
                file.open(current_tail_path_);
                if (!file.is_open()) {
                    continue; // Keep trying
                }
                file.seekg(0, std::ios::end);
                last_pos = file.tellg();
                continue;
            }

            file.clear(); // Clear any error flags
            auto current_size = std::filesystem::file_size(current_tail_path_);

            if (current_size < static_cast<size_t>(last_pos)) {
                // File was truncated or reset (e.g., log rotation)
                std::cerr << "Tailing: File truncated or rotated. Re-reading from beginning." << std::endl;
                file.seekg(0, std::ios::beg);
                last_pos = 0;
                entry_buffer.clear(); // Clear buffer to avoid parsing old partial entries
            } else if (current_size > static_cast<size_t>(last_pos)) {
                // New content available
                file.seekg(last_pos);
                std::string line;
                while (std::getline(file, line)) {
                    line_count++;
                    bool is_new = true;
                    if (this->entry_start_regex_)
                        is_new = std::regex_search(line, *this->entry_start_regex_);

                    if (is_new && !entry_buffer.empty()) {
                        ::LogEntry entry = this->parseLogLine(entry_buffer, line_count);
                        this->applyEnrichers(entry);
                        this->applyAnonymizers(entry);
                        this->addEntry(std::move(entry)); // Add to analyzer's entries
                        entry_buffer = line;
                    } else {
                        if (!entry_buffer.empty()) entry_buffer += "\n";
                        entry_buffer += line;
                    }
                }
                last_pos = file.tellg();
            }
        }
        // Process any remaining buffer after tailing stops
        if (!entry_buffer.empty()) {
            ::LogEntry entry = this->parseLogLine(entry_buffer, line_count);
            this->applyEnrichers(entry);
            this->applyAnonymizers(entry);
            this->addEntry(std::move(entry));
        }
        promise->set_value(); // Signal completion
    });

    return future;
}

/**
 * @brief Stops any active tailing operations started by `startTailing()`.
 */
void LogAnalysis::LogAnalyzer::stopTailing() {
    *this->stop_tailing_ptr_ = true;
    if (tailing_thread_.joinable()) {
        tailing_thread_.join();
    }
}

std::expected<LogAnalysis::LogStatistics, std::string> LogAnalysis::LogAnalyzer::analyzeStream(const std::filesystem::path &filepath)
{
    LogAnalysis::LogStatistics stats;
    std::map<std::string, size_t> error_counts;
    try
    {
        for (const auto &entry : streamEntries(filepath))
        {
            stats.total_entries++;
            stats.level_counts[entry.level]++;
            if (!stats.first_timestamp)
                stats.first_timestamp = entry.timestamp;
            stats.last_timestamp = entry.timestamp;
            if (entry.level == ::LogLevel::ERROR)
                error_counts[entry.message]++;
            if (!entry.thread_id.empty())
                stats.thread_distribution[entry.thread_id]++;
            if (entry.time_point.time_since_epoch().count() > 0)
            {
                auto minute_tp = std::chrono::time_point_cast<std::chrono::minutes>(entry.time_point);
                stats.timeline_distribution[minute_tp]++;
            }
        }
    }
    catch (const std::exception &e)
    {
        return std::unexpected(e.what());
    }

    for (const auto &[msg, count] : error_counts)
        stats.top_errors.push_back({msg, count});
    std::sort(stats.top_errors.begin(), stats.top_errors.end(), [](const auto &a, const auto &b)
              { return a.second > b.second; });
    if (stats.top_errors.size() > 5)
        stats.top_errors.resize(5);
    return stats;
}

std::map<::LogValue, size_t> LogAnalysis::LogAnalyzer::getAttributeFrequency(std::string_view attr_key) const
{
    std::map<::LogValue, size_t> frequency;
    std::string key(attr_key);
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto &entry : this->entries_)
    {
        auto it = entry.attributes.find(key);
        if (it != entry.attributes.end())
        {
            frequency[it->second]++;
        }
    }
    return frequency;
}

std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> LogAnalysis::LogAnalyzer::getTimeline(std::chrono::system_clock::duration bucket_size) const
{
    if (bucket_size.count() <= 0)
        return {};
    std::map<std::chrono::system_clock::time_point, size_t> buckets;
    for (const auto &entry : this->entries_)
    {
        if (entry.time_point.time_since_epoch().count() == 0)
            continue;
        auto bucket_start = std::chrono::time_point<std::chrono::system_clock>(
            entry.time_point.time_since_epoch() - (entry.time_point.time_since_epoch() % bucket_size));
        buckets[bucket_start]++;
    }
    return {buckets.begin(), buckets.end()};
}

std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getTrace(std::string_view trace_id) const
{
    std::vector<::LogEntry> result;
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto &entry : this->entries_)
    {
        if (entry.trace_id == trace_id)
        {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getEntriesSpan() const
{
    std::shared_lock lock(*this->rw_mutex_ptr_);
    return this->entries_;
}
std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getEntries() const
{
    std::shared_lock lock(*this->rw_mutex_ptr_);
    return this->entries_;
}
void LogAnalysis::LogAnalyzer::addEntry(::LogEntry entry)
{
    this->applyEnrichers(entry);
    this->applyAnonymizers(entry);
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->entries_.push_back(std::move(entry));
    this->cached_stats_.reset();
}

std::string LogAnalysis::LogAnalyzer::generateMessageTemplate(std::string_view message) const
{
    std::string result(message);
    // Replace digits with {}
    static const std::regex digit_regex(R"(\d+)");
    result = std::regex_replace(result, digit_regex, "{}");
    // Replace hex-like strings (UUIDs, etc)
    static const std::regex hex_regex(R"([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}|0x[0-9a-fA-F]+)");
    result = std::regex_replace(result, hex_regex, "{}");
    return result;
}

::LogEntry LogAnalysis::LogAnalyzer::parseLogLine(const std::string &line, size_t line_number)
{
    ::LogEntry entry;
    entry.raw_line = line;

    if (!this->config_.line_pattern.empty())
    {
        std::smatch match;
        if (std::regex_match(line, match, this->strict_regex_))
        {
            auto get_val = [&](const std::string &name) -> std::string
            {
                auto it = this->named_group_indices_.find(name);
                if (it != this->named_group_indices_.end() && static_cast<size_t>(it->second) < match.size())
                {
                    return match[it->second].str();
                }
                return "";
            };

            // Named capture groups mapping
            for (const auto &[group_name, field_name] : this->config_.field_mapping)
            {
                std::string val = get_val(group_name);
                if (val.empty())
                    continue;
                if (field_name == "timestamp")
                    entry.timestamp = val;
                else if (field_name == "level")
                    entry.level = ::LogEntry::parseLevel(val);
                else if (field_name == "message")
                    entry.message = val;
                else if (field_name == "thread_id")
                    entry.thread_id = val;
                else if (field_name == "file")
                    entry.source_file = val;
                else if (field_name == "line")
                    try
                    {
                        entry.source_line = std::stoi(val);
                    }
                    catch (...)
                    {
                    }
            }

            // Fallback to indices if field_mapping didn't cover them
            if (entry.timestamp.empty() && this->config_.timestamp_index > 0 && (size_t)this->config_.timestamp_index < match.size())
                entry.timestamp = match[this->config_.timestamp_index].str();

            if (entry.level == ::LogLevel::UNKNOWN && this->config_.level_index > 0 && (size_t)this->config_.level_index < match.size())
                entry.level = ::LogEntry::parseLevel(match[this->config_.level_index].str());

            if (entry.message.empty() && this->config_.message_index > 0 && (size_t)this->config_.message_index < match.size())
                entry.message = match[this->config_.message_index].str();

            if (entry.thread_id.empty() && this->config_.thread_id_index > 0 && (size_t)this->config_.thread_id_index < match.size())
                entry.thread_id = match[this->config_.thread_id_index].str();

            if (entry.source_file.empty() && this->config_.file_index > 0 && (size_t)this->config_.file_index < match.size())
                entry.source_file = match[this->config_.file_index].str();

            if (entry.source_line == 0 && this->config_.line_index > 0 && (size_t)this->config_.line_index < match.size())
                try
                {
                    entry.source_line = std::stoi(match[this->config_.line_index].str());
                }
                catch (...)
                {
                }

            if (!entry.timestamp.empty())
            {
                std::istringstream ss(entry.timestamp);
                std::tm tm = {};
                ss >> std::get_time(&tm, this->config_.time_format.c_str());
                if (!ss.fail())
                {
                    tm.tm_isdst = -1;
                    std::time_t tt = std::mktime(&tm);
                    if (tt != -1)
                        entry.time_point = std::chrono::system_clock::from_time_t(tt);
                }
            }
            return entry;
        }
        else if (this->config_.strict_mode)
        {
            if (this->config_.error_callback)
                this->config_.error_callback({line_number, line, "Regex match failed"});
            return ::LogEntry();
        }
    }

    std::smatch match;
    if (std::regex_search(line, match, this->legacy_timestamp_regex_))
    {
        entry.timestamp = match[1].str();
        entry.parseTime();
    }
    if (std::regex_search(line, match, this->legacy_level_regex_))
    {
        entry.level = ::LogEntry::parseLevel(match[1].str());
    }
    else
        entry.level = ::LogLevel::UNKNOWN;

    if (entry.level != ::LogLevel::UNKNOWN && !match.empty())
    {
        size_t message_start = match.position() + match.length();
        while (message_start < line.length() && (line[message_start] == ' ' || line[message_start] == ':' || line[message_start] == ']'))
            message_start++;
        entry.message = line.substr(message_start);
    }
    else
        entry.message = line;

    return entry;
}

bool LogAnalysis::LogAnalyzer::matchFilter(const ::LogEntry &entry, const LogAnalysis::FilterOptions &options) const
{
    return options.toPredicate()->test(entry);
}

void LogAnalysis::LogAnalyzer::addEnricher(LogAnalysis::LogEnricher enricher) { this->enrichers_.push_back(std::move(enricher)); }
void LogAnalysis::LogAnalyzer::applyEnrichers(::LogEntry &entry)
{
    for (auto &e : this->enrichers_)
        e(entry);
}

void LogAnalysis::LogAnalyzer::addAnonymizer(std::unique_ptr<LogAnonymizer> anonymizer)
{
    this->anonymizers_.push_back(std::move(anonymizer));
}

void LogAnalysis::LogAnalyzer::clearAnonymizers()
{
    this->anonymizers_.clear();
}

void LogAnalysis::LogAnalyzer::applyAnonymizers(::LogEntry &entry)
{
    for (const auto &a : this->anonymizers_)
        a->anonymize(entry);
}

void LogAnalysis::LogAnalyzer::setFilterOptions(const LogAnalysis::FilterOptions& options) {
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->currentFilterOptions_ = options;
    this->currentFilterPredicate_.reset();
    this->cached_stats_.reset(); // Invalidate cache as filter changes
}

void LogAnalysis::LogAnalyzer::clearFilterOptions() {
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->currentFilterOptions_.reset();
    this->currentFilterPredicate_.reset();
    this->cached_stats_.reset(); // Invalidate cache
}

void LogAnalysis::LogAnalyzer::setFilterQuery(std::string_view query_string) {
    auto res = Filters::fromQuery(query_string);
    if (!res) {
        throw std::invalid_argument(res.error());
    }
    std::unique_lock lock(*this->rw_mutex_ptr_);
    this->currentFilterPredicate_ = std::move(*res);
    this->currentFilterOptions_.reset();
    this->cached_stats_.reset();
}

std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getFilteredEntriesInternal() const {
    std::shared_lock lock(*this->rw_mutex_ptr_);
    if (!this->currentFilterOptions_ && !this->currentFilterPredicate_) {
        return this->entries_; // No filter applied, return all entries
    }
    std::vector<::LogEntry> filtered;
    std::unique_ptr<LogAnalysis::LogPredicate> predicate;
    if (this->currentFilterPredicate_) {
        predicate = this->currentFilterPredicate_->clone();
    } else {
        predicate = this->currentFilterOptions_->toPredicate();
    }
    for (const auto& entry : this->entries_) {
        if (predicate->test(entry)) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

LogAnalysis::LogStatistics LogAnalysis::LogAnalyzer::analyzeAndGetResults()
{
    std::shared_lock lock(*this->rw_mutex_ptr_);
    // If cached stats exist AND no filter is applied, return cached stats.
    // If a filter IS applied, force recalculation as cached_stats_ only holds unfiltered stats.
    if (cached_stats_ && !this->currentFilterOptions_ && !this->currentFilterPredicate_) {
        return *cached_stats_;
    }
    lock.unlock(); // Release read lock to allow analyze() to take unique lock if needed

    // Compute statistics on filtered entries
    LogAnalysis::LogStatistics stats = calculateStatistics(getFilteredEntriesInternal());

    // Cache results only if no filter is applied
    if (!this->currentFilterOptions_ && !this->currentFilterPredicate_) {
        std::unique_lock write_lock(*this->rw_mutex_ptr_);
        this->cached_stats_ = std::move(stats);
        return *cached_stats_;
    }
    return stats;
}

// Implement `analyze()` and update `getStatistics()` for caching
void LogAnalysis::LogAnalyzer::analyze()
{
    // Simply call analyzeAndGetResults; its side effect is to cache if no filter is applied
    analyzeAndGetResults();
}

LogAnalysis::LogStatistics LogAnalysis::LogAnalyzer::getStatistics() const
{
    // Call analyzeAndGetResults to ensure stats are calculated, respecting filters and caching if no filter.
    return const_cast<LogAnalysis::LogAnalyzer *>(this)->analyzeAndGetResults();
}

// NEW: Helper to calculate statistics on a given set of entries
LogAnalysis::LogStatistics LogAnalysis::LogAnalyzer::calculateStatistics(const std::vector<::LogEntry>& entries_to_analyze) const {
    LogAnalysis::LogStatistics stats;
    stats.total_entries = entries_to_analyze.size();

    if (entries_to_analyze.empty()) {
        return stats;
    }

    std::map<std::string, size_t> error_counts;
    std::map<std::string, std::map<std::string, size_t>> top_attr_counts;

    auto min_max_it = std::minmax_element(entries_to_analyze.begin(), entries_to_analyze.end(),
                                          [](const ::LogEntry &a, const ::LogEntry &b)
                                          {
                                              return a.time_point < b.time_point;
                                          });

    if (min_max_it.first != entries_to_analyze.end() && min_max_it.first->time_point.time_since_epoch().count() > 0)
    {
        stats.first_timestamp = min_max_it.first->timestamp;
        stats.last_timestamp = min_max_it.second->timestamp;
        stats.duration = std::chrono::duration_cast<std::chrono::seconds>(min_max_it.second->time_point - min_max_it.first->time_point);
        if (stats.duration.count() > 0)
        {
            stats.entries_per_second = static_cast<double>(stats.total_entries) / static_cast<double>(stats.duration.count());
        }
    }

    for (const auto &entry : entries_to_analyze)
    {
        stats.level_counts[entry.level]++;
        if (entry.level == ::LogLevel::ERROR || entry.level == ::LogLevel::CRITICAL)
        {
            error_counts[entry.message]++;
        }
        if (!entry.thread_id.empty())
        {
            stats.thread_distribution[entry.thread_id]++;
        }
        if (entry.time_point.time_since_epoch().count() > 0)
        {
            auto bucket_start = std::chrono::time_point_cast<std::chrono::minutes>(entry.time_point);
            stats.timeline_distribution[bucket_start]++;
        }

        if (currentAnalysisConfig_)
        {
            if (currentAnalysisConfig_->enable_message_template_counts)
            {
                stats.message_template_counts[generateMessageTemplate(entry.message)]++;
            }

            for (const auto &attr : currentAnalysisConfig_->attributes_for_distribution)
            {
                auto it = entry.attributes.find(attr);
                if (it != entry.attributes.end())
                {
                    stats.attribute_value_distributions[attr][it->second]++;
                }
            }

            for (const auto &[attr, n] : currentAnalysisConfig_->top_N_string_attributes)
            {
                auto it = entry.attributes.find(attr);
                if (it != entry.attributes.end())
                {
                    if (auto p_str = it->second.get_if<std::string>())
                    {
                        top_attr_counts[attr][*p_str]++;
                    }
                }
            }
        }
    }

    if (currentAnalysisConfig_)
    {
        for (const auto &[attr, n] : currentAnalysisConfig_->top_N_string_attributes)
        {
            auto &counts = top_attr_counts[attr];
            std::vector<std::pair<std::string, size_t>> top_n(counts.begin(), counts.end());
            std::sort(top_n.begin(), top_n.end(), [](const auto &a, const auto &b)
                      { return a.second > b.second; });
            if (top_n.size() > n)
                top_n.resize(n);
            stats.top_string_attribute_occurrences[attr] = std::move(top_n);
        }
    }

    stats.top_errors.reserve(error_counts.size());
    for (const auto &[msg, count] : error_counts)
    {
        stats.top_errors.push_back({msg, count});
    }
    std::sort(stats.top_errors.begin(), stats.top_errors.end(), [](const auto &a, const auto &b)
              { return a.second > b.second; });
    if (stats.top_errors.size() > 5)
    {
        stats.top_errors.resize(5);
    }
    return stats;
}


// NEW: getFilteredStatistics overloads
LogAnalysis::LogStatistics LogAnalysis::LogAnalyzer::getFilteredStatistics(const FilterOptions& options) const {
    std::vector<::LogEntry> filtered_entries;
    {
        std::shared_lock lock(*this->rw_mutex_ptr_);
        auto predicate = options.toPredicate();
        for (const auto& entry : this->entries_) {
            if (predicate->test(entry)) {
                filtered_entries.push_back(entry);
            }
        }
    }
    return calculateStatistics(filtered_entries);
}

LogAnalysis::LogStatistics LogAnalysis::LogAnalyzer::getFilteredStatistics(const LogPredicate& predicate) const {
    std::vector<::LogEntry> filtered_entries;
    {
        std::shared_lock lock(*this->rw_mutex_ptr_);
        for (const auto& entry : this->entries_) {
            if (predicate.test(entry)) {
                filtered_entries.push_back(entry);
            }
        }
    }
    return calculateStatistics(filtered_entries);
}

std::vector<std::pair<std::string, size_t>> LogAnalysis::LogAnalyzer::getTopNMessages(size_t n, bool useTemplates) const {
    std::map<std::string, size_t> counts;
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto& entry : this->entries_) {
        if (useTemplates) {
            counts[generateMessageTemplate(entry.message)]++;
        } else {
            counts[entry.message]++;
        }
    }
    std::vector<std::pair<std::string, size_t>> sorted_counts(counts.begin(), counts.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sorted_counts.size() > n) {
        sorted_counts.resize(n);
    }
    return sorted_counts;
}

std::vector<std::pair<LogValue, size_t>> LogAnalysis::LogAnalyzer::getTopNAttributeValues(const std::string& attributeName, size_t n) const {
    std::map<LogValue, size_t> counts;
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto& entry : this->entries_) {
        auto it = entry.attributes.find(attributeName);
        if (it != entry.attributes.end()) {
            counts[it->second]++;
        }
    }
    std::vector<std::pair<LogValue, size_t>> sorted_counts(counts.begin(), counts.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sorted_counts.size() > n) {
        sorted_counts.resize(n);
    }
    return sorted_counts;
}

std::vector<std::pair<std::string, size_t>> LogAnalysis::LogAnalyzer::getTopNThreadIds(size_t n) const {
    std::map<std::string, size_t> counts;
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto& entry : this->entries_) {
        if (!entry.thread_id.empty()) {
            counts[entry.thread_id]++;
        }
    }
    std::vector<std::pair<std::string, size_t>> sorted_counts(counts.begin(), counts.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sorted_counts.size() > n) {
        sorted_counts.resize(n);
    }
    return sorted_counts;
}

std::vector<std::pair<std::string, size_t>> LogAnalysis::LogAnalyzer::getTopNSourceFiles(size_t n) const {
    std::map<std::string, size_t> counts;
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto& entry : this->entries_) {
        if (!entry.source_file.empty()) {
            counts[entry.source_file]++;
        }
    }
    std::vector<std::pair<std::string, size_t>> sorted_counts(counts.begin(), counts.end());
    std::sort(sorted_counts.begin(), sorted_counts.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });
    if (sorted_counts.size() > n) {
        sorted_counts.resize(n);
    }
    return sorted_counts;
}


// --- New aggregation methods ---
std::map<::LogValue, size_t> LogAnalysis::LogAnalyzer::getFrequencyMap(std::string_view attribute_key) const
{
    std::map<::LogValue, size_t> frequency;
    std::string key(attribute_key);
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto &entry : this->entries_)
    {
        auto it = entry.attributes.find(key);
        if (it != entry.attributes.end())
        {
            frequency[it->second]++;
        }
    }
    return frequency;
}

// In-place sort
void LogAnalysis::LogAnalyzer::sort(std::function<bool(const ::LogEntry &, const ::LogEntry &)> cmp)
{
    std::unique_lock lock(*this->rw_mutex_ptr_); // Unique lock for modifying this->entries_
    std::sort(this->entries_.begin(), this->entries_.end(), cmp);
    this->cached_stats_.reset(); // Invalidate cache
}

// In-place removal
void LogAnalysis::LogAnalyzer::removeIf(const LogAnalysis::LogPredicate &predicate)
{
    std::unique_lock lock(*this->rw_mutex_ptr_); // Unique lock for modifying this->entries_
    auto it = std::remove_if(this->entries_.begin(), this->entries_.end(),
                             [&](const ::LogEntry &entry)
                             {
                                 return predicate.test(entry);
                             });
    this->entries_.erase(it, this->entries_.end());
    this->cached_stats_.reset(); // Invalidate cache
}

// Bulk transformation
void LogAnalysis::LogAnalyzer::transform(std::function<void(::LogEntry &)> transformer)
{
    std::unique_lock lock(*this->rw_mutex_ptr_); // Unique lock for modifying this->entries_
    for (auto &entry : this->entries_)
    {
        transformer(entry); // Apply transformer to each entry
    }
    this->cached_stats_.reset(); // Invalidate cache
}

// New generic export methods
void LogAnalysis::LogAnalyzer::exportTo(LogAnalysis::LogExporter &exporter) const
{
    std::shared_lock lock(*this->rw_mutex_ptr_); // Read lock for this->entries_
    exporter.exportEntries({this->entries_.data(), this->entries_.size()});
}

void LogAnalysis::LogAnalyzer::exportFilteredTo(LogAnalysis::LogExporter &exporter, const LogAnalysis::LogPredicate &predicate) const
{
    std::vector<::LogEntry> filtered_entries;
    {
        std::shared_lock lock(*this->rw_mutex_ptr_); // Read lock for this->entries_
        for (const auto &entry : this->entries_)
        {
            if (predicate.test(entry))
            {
                filtered_entries.push_back(entry);
            }
        }
    }
    // Note: Exporters are not expected to be thread-safe, so we copy filtered entries
    // and then export them outside the lock, or export one by one from inside lock
    // if exporter supports streaming. For now, collect all and export.
    exporter.exportEntries({filtered_entries.data(), filtered_entries.size()});
}

void LogAnalysis::LogAnalyzer::exportStatistics(LogAnalysis::LogExporter &exporter) const
{
    // getStatistics() will ensure analyze() is called and cache is populated
    LogAnalysis::LogStatistics stats = getStatistics();
    exporter.exportStats(stats);
}

void LogAnalysis::LogAnalyzer::printResults(std::ostream& os, LogAnalysis::OutputFormat format) const {
    LogAnalysis::LogStatistics stats = const_cast<LogAnalysis::LogAnalyzer*>(this)->analyzeAndGetResults(); // Get stats based on current filters

    switch (format) {
        case LogAnalysis::OutputFormat::TEXT: {
            LogAnalysis::ConsoleExporter exporter(os);
            exporter.exportStats(stats);
            exporter.exportEntries(getFilteredEntriesInternal()); // Export filtered entries
            break;
        }
        case LogAnalysis::OutputFormat::JSON: {
            LogAnalysis::JsonExporter exporter(os, true); // Pretty JSON
            exporter.exportStats(stats);
            exporter.exportEntries(getFilteredEntriesInternal()); // Export filtered entries
            break;
        }
        case LogAnalysis::OutputFormat::CSV: {
            LogAnalysis::CsvExporter exporter(os);
            exporter.exportStats(stats);
            exporter.exportEntries(getFilteredEntriesInternal()); // Export filtered entries
            break;
        }
        case LogAnalysis::OutputFormat::MARKDOWN: {
            LogAnalysis::MarkdownExporter exporter(os);
            exporter.exportStats(stats);
            exporter.exportEntries(getFilteredEntriesInternal()); // Export filtered entries
            break;
        }
    }
}

void LogAnalysis::LogAnalyzer::writeStatistics(std::ostream &out, bool as_json) const
{
    if (as_json)
    {
        LogAnalysis::JsonExporter exporter(out, true); // Assuming pretty JSON for direct JSON output
        exportStatistics(exporter);
    }
    else
    {
        LogAnalysis::ConsoleExporter exporter(out);
        exportStatistics(exporter);
    }
}

void LogAnalysis::LogAnalyzer::writeFilteredEntries(std::ostream &out, const LogAnalysis::FilterOptions &options, bool as_json) const
{
    auto pred = options.toPredicate(); // Create predicate from options
    if (as_json)
    {
        LogAnalysis::JsonExporter exporter(out, true); // Assuming pretty JSON
        exportFilteredTo(exporter, *pred);
    }
    else
    {
        LogAnalysis::ConsoleExporter exporter(out);
        exportFilteredTo(exporter, *pred);
    }
}

std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getFilteredEntries() const { return getFilteredEntriesInternal(); }

void LogAnalysis::LogAnalyzer::printStatistics() const
{
    printResults(std::cout, LogAnalysis::OutputFormat::TEXT);
}
std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getFilteredEntries(const LogAnalysis::FilterOptions &options) const { return getFilteredEntries(*options.toPredicate()); }
std::vector<::LogEntry> LogAnalysis::LogAnalyzer::getFilteredEntries(const LogAnalysis::LogPredicate &predicate) const
{
    std::vector<::LogEntry> result;
    std::shared_lock lock(*this->rw_mutex_ptr_);
    for (const auto &entry : this->entries_)
        if (predicate.test(entry))
            result.push_back(entry);
    return result;
}

std::string LogAnalysis::LogAnalyzer::levelToString(::LogLevel level) const { return std::string(::LogEntry::levelToString(level)); }

std::string LogAnalysis::ParsingConfig::toJson() const
{
    std::ostringstream oss;
    oss << "{"
        << "\"strict_mode\":" << (strict_mode ? "true" : "false") << ","
        << "\"line_pattern\":\"" << line_pattern << "\","
        << "\"timestamp_index\":" << timestamp_index << ","
        << "\"level_index\":" << level_index << ","
        << "\"message_index\":" << message_index << ","
        << "\"thread_id_index\":" << thread_id_index << ","
        << "\"file_index\":" << file_index << ","
        << "\"line_index\":" << line_index << ","
        << "\"time_format\":\"" << time_format << "\","
        << "\"max_errors\":" << static_cast<unsigned long long>(max_errors) << ","
        << "\"max_continuation_lines\":" << static_cast<unsigned long long>(max_continuation_lines);
    if (entry_start_pattern)
    {
        oss << ",\"entry_start_pattern\":\"" << *entry_start_pattern << "\"";
    }
    oss << "}";
    return oss.str();
}

std::expected<LogAnalysis::ParsingConfig, std::string> LogAnalysis::ParsingConfig::fromJson(std::string_view json_str)
{
    ParsingConfig config;
    auto find_val = [&](std::string_view key) -> std::optional<std::string_view>
    {
        std::string pattern = "\"" + std::string(key) + "\"";
        size_t pos = json_str.find(pattern);
        if (pos == std::string_view::npos)
            return std::nullopt;
        pos = json_str.find(':', pos + pattern.size());
        if (pos == std::string_view::npos)
            return std::nullopt;
        pos++;
        while (pos < json_str.size() && std::isspace(json_str[pos]))
            pos++;
        if (pos >= json_str.size())
            return std::nullopt;

        size_t end;
        if (json_str[pos] == '\"')
        {
            pos++;
            end = json_str.find('\"', pos);
        }
        else
        {
            end = pos;
            while (end < json_str.size() && json_str[end] != ',' && json_str[end] != '}' && !std::isspace(json_str[end]))
                end++;
        }
        if (end == std::string_view::npos)
            return std::nullopt;
        return json_str.substr(pos, end - pos);
    };

    try
    {
        if (auto s = find_val("strict_mode"))
            config.strict_mode = (*s == "true");
        if (auto s = find_val("line_pattern"))
            config.line_pattern = std::string(*s);
        if (auto s = find_val("timestamp_index"))
            config.timestamp_index = std::stoi(std::string(*s));
        if (auto s = find_val("level_index"))
            config.level_index = std::stoi(std::string(*s));
        if (auto s = find_val("message_index"))
            config.message_index = std::stoi(std::string(*s));
        if (auto s = find_val("thread_id_index"))
            config.thread_id_index = std::stoi(std::string(*s));
        if (auto s = find_val("file_index"))
            config.file_index = std::stoi(std::string(*s));
        if (auto s = find_val("line_index"))
            config.line_index = std::stoi(std::string(*s));
        if (auto s = find_val("time_format"))
            config.time_format = std::string(*s);
        if (auto s = find_val("max_errors"))
            config.max_errors = std::stoull(std::string(*s));
        if (auto s = find_val("max_continuation_lines"))
            config.max_continuation_lines = std::stoull(std::string(*s));
        if (auto s = find_val("entry_start_pattern"))
            config.entry_start_pattern = std::string(*s);
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string("Parse error: ") + e.what());
    }

    return config;
}

std::string LogAnalysis::FilterOptions::toJson() const
{
    std::ostringstream oss;
    oss << "{"
        << "\"case_sensitive\":" << (case_sensitive ? "true" : "false") << ","
        << "\"invert_match\":" << (invert_match ? "true" : "false");
    if (keyword)
        oss << ",\"keyword\":\"" << *keyword << "\"";
    if (level)
        oss << ",\"level\":" << static_cast<int>(*level);
    if (source_file)
        oss << ",\"source_file\":\"" << *source_file << "\"";
    if (thread_id)
        oss << ",\"thread_id\":\"" << *thread_id << "\"";
    oss << "}";
    return oss.str();
}

std::expected<LogAnalysis::FilterOptions, std::string> LogAnalysis::FilterOptions::fromJson(std::string_view json_str)
{
    FilterOptions options;
    auto find_val = [&](std::string_view key) -> std::optional<std::string_view>
    {
        std::string pattern = "\"" + std::string(key) + "\"";
        size_t pos = json_str.find(pattern);
        if (pos == std::string_view::npos)
            return std::nullopt;
        pos = json_str.find(':', pos + pattern.size());
        if (pos == std::string_view::npos)
            return std::nullopt;
        pos++;
        while (pos < json_str.size() && std::isspace(json_str[pos]))
            pos++;
        if (pos >= json_str.size())
            return std::nullopt;

        size_t end;
        if (json_str[pos] == '\"')
        {
            pos++;
            end = json_str.find('\"', pos);
        }
        else
        {
            end = pos;
            while (end < json_str.size() && json_str[end] != ',' && json_str[end] != '}' && !std::isspace(json_str[end]))
                end++;
        }
        if (end == std::string_view::npos)
            return std::nullopt;
        return json_str.substr(pos, end - pos);
    };

    try
    {
        if (auto s = find_val("case_sensitive"))
            options.case_sensitive = (*s == "true");
        if (auto s = find_val("invert_match"))
            options.invert_match = (*s == "true");
        if (auto s = find_val("keyword"))
            options.keyword = std::string(*s);
        if (auto s = find_val("level"))
            options.level = static_cast<::LogLevel>(std::stoi(std::string(*s)));
        if (auto s = find_val("source_file"))
            options.source_file = std::string(*s);
        if (auto s = find_val("thread_id"))
            options.thread_id = std::string(*s);
    }
    catch (const std::exception &e)
    {
        return std::unexpected(std::string("Parse error: ") + e.what());
    }
    return options;
}

} // namespace LogAnalysis

namespace LogAnalysis {

// --- LogAnalyzerBuilder Implementation ---

LogAnalyzerBuilder::LogAnalyzerBuilder() {
    // Default configurations
    current_parsing_config_ = ParsingConfig();
    current_analysis_config_ = AnalysisConfig();
}

LogAnalyzerBuilder& LogAnalyzerBuilder::withParsingConfig(ParsingConfig config) {
    current_parsing_config_ = std::move(config);
    return *this;
}

LogAnalyzerBuilder& LogAnalyzerBuilder::withParsingProfile(const std::string& profileName) {
    auto it = s_parsingProfiles.find(profileName);
    if (it != s_parsingProfiles.end()) {
        current_parsing_config_ = it->second;
    } else {
        throw std::runtime_error("Unknown parsing profile: " + profileName);
    }
    return *this;
}

LogAnalyzerBuilder& LogAnalyzerBuilder::withAnalysisConfig(AnalysisConfig config) {
    current_analysis_config_ = std::move(config);
    return *this;
}

LogAnalyzerBuilder& LogAnalyzerBuilder::withInitialFilterOptions(FilterOptions options) {
    initial_filter_options_ = std::move(options);
    initial_filter_query_.reset(); // Clear query if options are set
    return *this;
}

LogAnalyzerBuilder& LogAnalyzerBuilder::withFilterQuery(const std::string& query) {
    initial_filter_query_ = query;
    initial_filter_options_.reset(); // Clear options if query is set
    return *this;
}

LogAnalyzerBuilder& LogAnalyzerBuilder::addEnricher(LogEnricher enricher) {
    enrichers_.push_back(std::move(enricher));
    return *this;
}

LogAnalyzerBuilder& LogAnalyzerBuilder::addAnonymizer(std::unique_ptr<LogAnonymizer> anonymizer) {
    anonymizers_.push_back(std::move(anonymizer));
    return *this;
}

std::unique_ptr<LogAnalyzer> LogAnalyzerBuilder::build() {
    auto analyzer = std::make_unique<LogAnalyzer>();
    analyzer->setParsingConfig(current_parsing_config_);
    analyzer->setAnalysisConfig(current_analysis_config_);

    if (initial_filter_options_) {
        analyzer->setFilterOptions(*initial_filter_options_);
    } else if (initial_filter_query_) {
        analyzer->setFilterQuery(*initial_filter_query_);
    }

    for (auto& enricher : enrichers_) {
        analyzer->addEnricher(std::move(enricher));
    }
    for (auto& anonymizer : anonymizers_) {
        analyzer->addAnonymizer(std::move(anonymizer));
    }

    return analyzer;
}

} // namespace LogAnalysis
