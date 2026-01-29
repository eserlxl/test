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
#include <numeric>

// --- ParsingConfig Implementation ---

bool ParsingConfig::validate() const
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

ParsingConfig ParsingConfig::fromRegex(std::string pattern)
{
    ParsingConfig config;
    config.line_pattern = pattern;
    // Use the new helper for named groups
    config.field_mapping = ParsingConfig::fromRegexWithNamedGroups(pattern).field_mapping;
    return config;
}

// Improved static helper to parse named groups
ParsingConfig ParsingConfig::fromRegexWithNamedGroups(std::string pattern)
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

// --- LogQuery Implementation ---

namespace LogQuery
{
    // Token types
    enum class TokenType {
        Identifier, String, Number, 
        And, Or, Not, 
        Eq, Neq, Gt, Lt, Gte, Lte,
        LParen, RParen, End
    };

    struct Token {
        TokenType type;
        std::string value;
    };

    // Internal Predicate for LQL that handles core fields and operators
    class FieldPredicate : public LogPredicate {
        std::string key_;
        TokenType op_;
        LogValue val_;

    public:
        FieldPredicate(std::string key, TokenType op, LogValue val) 
            : key_(std::move(key)), op_(op), val_(std::move(val)) {}

        bool test(const LogEntry& entry) const override {
            LogValue entry_val;
            bool found = false;

            // map core fields
            if (key_ == "message") { entry_val = entry.message; found = true; }
            else if (key_ == "timestamp") { entry_val = entry.timestamp; found = true; }
            else if (key_ == "thread_id") { entry_val = entry.thread_id; found = true; }
            else if (key_ == "level") { entry_val = std::string(LogEntry::levelToString(entry.level)); found = true; }
            else if (key_ == "source_file") { entry_val = entry.source_file; found = true; }
            else {
                // Check attributes
                if (auto it = entry.attributes.find(key_); it != entry.attributes.end()) {
                    entry_val = it->second;
                    found = true;
                }
            }

            if (!found) return false;

            // Perform comparison
            // Simplified comparison logic. In a real system, this would be more robust (type coercion).
            
            // Helper for typed comparison
            auto compare = [&](auto a, auto b) -> int {
                if (a < b) return -1;
                if (a > b) return 1;
                return 0;
            };

            // String comparison
            if (std::holds_alternative<std::string>(entry_val) && std::holds_alternative<std::string>(val_)) {
                int cmp = std::get<std::string>(entry_val).compare(std::get<std::string>(val_));
                return compareOp(cmp);
            }
            
            // Numeric comparison
            auto getDouble = [](const LogValue& v) -> std::optional<double> {
                if (std::holds_alternative<double>(v)) return std::get<double>(v);
                if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
                if (std::holds_alternative<uint64_t>(v)) return static_cast<double>(std::get<uint64_t>(v));
                return std::nullopt;
            };

            auto d1 = getDouble(entry_val);
            auto d2 = getDouble(val_);
            if (d1 && d2) {
                int cmp = 0;
                if (*d1 < *d2) cmp = -1;
                else if (*d1 > *d2) cmp = 1;
                return compareOp(cmp);
            }

            // Boolean
            if (std::holds_alternative<bool>(entry_val) && std::holds_alternative<bool>(val_)) {
                int cmp = 0;
                if (std::get<bool>(entry_val) == std::get<bool>(val_)) cmp = 0;
                else cmp = (std::get<bool>(entry_val) < std::get<bool>(val_)) ? -1 : 1;
                return compareOp(cmp);
            }

            // Fallback for equality if types mismatch or other types
            if (op_ == TokenType::Eq) return entry_val == val_;
            if (op_ == TokenType::Neq) return entry_val != val_;

            return false;
        }
        
        bool compareOp(int cmp) const {
            switch (op_) {
                case TokenType::Eq: return cmp == 0;
                case TokenType::Neq: return cmp != 0;
                case TokenType::Gt: return cmp > 0;
                case TokenType::Lt: return cmp < 0;
                case TokenType::Gte: return cmp >= 0;
                case TokenType::Lte: return cmp <= 0;
                default: return false;
            }
        }

        std::unique_ptr<LogPredicate> clone() const override {
            return std::make_unique<FieldPredicate>(key_, op_, val_);
        }
    };

    class Lexer {
        std::string_view input_;
        size_t pos_ = 0;

    public:
        explicit Lexer(std::string_view input) : input_(input) {}

        Token next() {
            while (pos_ < input_.size() && std::isspace(input_[pos_])) pos_++;
            
            if (pos_ >= input_.size()) return {TokenType::End, ""};

            char c = input_[pos_];

            if (std::isalpha(c) || c == '_') {
                size_t start = pos_;
                while (pos_ < input_.size() && (std::isalnum(input_[pos_]) || input_[pos_] == '_' || input_[pos_] == '.')) {
                    pos_++;
                }
                std::string word(input_.substr(start, pos_ - start));
                if (word == "AND" || word == "and") return {TokenType::And, word};
                if (word == "OR" || word == "or") return {TokenType::Or, word};
                if (word == "NOT" || word == "not") return {TokenType::Not, word};
                return {TokenType::Identifier, word};
            }

            if (std::isdigit(c) || c == '-') { // Basic number support
                size_t start = pos_;
                if (c == '-') pos_++;
                while (pos_ < input_.size() && (std::isdigit(input_[pos_]) || input_[pos_] == '.')) {
                    pos_++;
                }
                return {TokenType::Number, std::string(input_.substr(start, pos_ - start))};
            }

            if (c == '"' || c == '\'') {
                char quote = c;
                pos_++;
                size_t start = pos_;
                while (pos_ < input_.size() && input_[pos_] != quote) {
                    if (input_[pos_] == '\\' && pos_ + 1 < input_.size()) pos_++;
                    pos_++;
                }
                if (pos_ >= input_.size()) throw std::runtime_error("Unterminated string");
                std::string val(input_.substr(start, pos_ - start));
                pos_++;
                return {TokenType::String, val};
            }

            // Operators
            if (c == '(') { pos_++; return {TokenType::LParen, "("}; }
            if (c == ')') { pos_++; return {TokenType::RParen, ")"}; }
            
            if (c == '=') {
                pos_++;
                if (pos_ < input_.size() && input_[pos_] == '=') { pos_++; return {TokenType::Eq, "=="}; }
                return {TokenType::Eq, "=="}; // Allow single = as ==
            }
            if (c == '!') {
                pos_++;
                if (pos_ < input_.size() && input_[pos_] == '=') { pos_++; return {TokenType::Neq, "!="}; }
                throw std::runtime_error("Unexpected char '!'");
            }
            if (c == '>') {
                pos_++;
                if (pos_ < input_.size() && input_[pos_] == '=') { pos_++; return {TokenType::Gte, ">="}; }
                return {TokenType::Gt, ">"};
            }
            if (c == '<') {
                pos_++;
                if (pos_ < input_.size() && input_[pos_] == '=') { pos_++; return {TokenType::Lte, "<="}; }
                return {TokenType::Lt, "<"};
            }

            throw std::runtime_error(std::string("Unexpected character: ") + c);
        }
    };

    class Parser {
        Lexer lexer_;
        Token current_;

        void advance() { current_ = lexer_.next(); }

        std::unique_ptr<LogPredicate> parseFactor() {
            if (current_.type == TokenType::Not) {
                advance();
                return Filters::Not(parseFactor());
            }
            if (current_.type == TokenType::LParen) {
                advance();
                auto expr = parseExpression();
                if (current_.type != TokenType::RParen) throw std::runtime_error("Expected ')'");
                advance();
                return expr;
            }
            
            // Comparison: Identifier Op Value
            if (current_.type == TokenType::Identifier) {
                std::string key = current_.value;
                advance();

                // Check for operators
                if (current_.type == TokenType::Eq || current_.type == TokenType::Neq ||
                    current_.type == TokenType::Gt || current_.type == TokenType::Lt ||
                    current_.type == TokenType::Gte || current_.type == TokenType::Lte) {
                    
                    TokenType op = current_.type;
                    advance();
                    
                    LogValue val;
                    if (current_.type == TokenType::String) val = current_.value;
                    else if (current_.type == TokenType::Number) {
                         // Try parse as double
                         try { val = std::stod(current_.value); } catch(...) { val = 0.0; }
                    }
                    else if (current_.type == TokenType::Identifier) {
                        if (current_.value == "true") val = true;
                        else if (current_.value == "false") val = false;
                        else val = current_.value; // Treat as string
                    } else {
                        throw std::runtime_error("Expected value");
                    }
                    advance();

                    // Special handling for core fields
                    if (key == "level") {
                         // Convert string value to standard level string for comparison if possible, or keep as is.
                         // FieldPredicate handles level by converting entry.level to string.
                         // So we should pass the string representation of level if the user passed a string.
                         // If user passed "ERROR", val is "ERROR".
                         // If user passed identifier ERROR, it might be parsed as string "ERROR" by lexer if we don't handle it.
                         // My lexer parses identifiers as strings basically.
                    }

                    return std::make_unique<FieldPredicate>(key, op, val);
                }
            }
            
            throw std::runtime_error("Unexpected token in factor");
        }

        std::unique_ptr<LogPredicate> parseTerm() {
            auto left = parseFactor();
            while (current_.type == TokenType::And) {
                advance();
                auto right = parseFactor();
                left = Filters::And(std::move(left), std::move(right));
            }
            return left;
        }

        std::unique_ptr<LogPredicate> parseExpression() {
            auto left = parseTerm();
            while (current_.type == TokenType::Or) {
                advance();
                auto right = parseTerm();
                left = Filters::Or(std::move(left), std::move(right));
            }
            return left;
        }

    public:
        Parser(std::string_view input) : lexer_(input) { advance(); }
        std::unique_ptr<LogPredicate> parse() { return parseExpression(); }
    };

    std::expected<std::unique_ptr<LogPredicate>, std::string> compile(std::string_view query) {
        try {
            Parser parser(query);
            return parser.parse();
        } catch (const std::exception& e) {
            return std::unexpected(e.what());
        }
    }
}

// --- Filters Implementation ---

namespace Filters
{
    class LevelPredicate : public LogPredicate
    {
        LogLevel level_;

    public:
        explicit LevelPredicate(LogLevel l) : level_(l) {}
        bool test(const LogEntry &entry) const override { return entry.level == level_; }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<LevelPredicate>(level_); }
    };

    class KeywordPredicate : public LogPredicate
    {
        std::string keyword_;
        bool case_sensitive_;

    public:
        KeywordPredicate(std::string k, bool cs) : keyword_(std::move(k)), case_sensitive_(cs) {}
        bool test(const LogEntry &entry) const override
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
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<KeywordPredicate>(keyword_, case_sensitive_); }
    };

    class RegexPredicate : public LogPredicate
    {
        std::regex regex_;
        std::string pattern_;

    public:
        explicit RegexPredicate(std::string p) : regex_(p), pattern_(std::move(p)) {}
        bool test(const LogEntry &entry) const override
        {
            return std::regex_search(entry.message, regex_);
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<RegexPredicate>(pattern_); }
    };

    class AttributePredicate : public LogPredicate
    {
        std::string key_;
        LogValue value_;

    public:
        AttributePredicate(std::string k, LogValue v) : key_(std::move(k)), value_(std::move(v)) {}
        bool test(const LogEntry &entry) const override
        {
            auto it = entry.attributes.find(key_);
            return it != entry.attributes.end() && it->second == value_;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AttributePredicate>(key_, value_); }
    };

    class AttributeRangePredicate : public LogPredicate
    {
        std::string key_;
        LogValue min_, max_;

    public:
        AttributeRangePredicate(std::string k, LogValue min, LogValue max)
            : key_(std::move(k)), min_(std::move(min)), max_(std::move(max)) {}

        bool test(const LogEntry &entry) const override
        {
            auto it = entry.attributes.find(key_);
            if (it == entry.attributes.end())
                return false;

            const auto &val = it->second;

            if (val.index() == min_.index() && val.index() == max_.index())
            {
                return val >= min_ && val <= max_;
            }

            auto to_double = [](const LogValue &v) -> std::optional<double>
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
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AttributeRangePredicate>(key_, min_, max_); }
    };

    class SincePredicate : public LogPredicate
    {
        std::chrono::system_clock::duration duration_;
        mutable std::optional<std::chrono::system_clock::time_point> reference_time_;

    public:
        explicit SincePredicate(std::chrono::system_clock::duration d) : duration_(d) {}
        bool test(const LogEntry &entry) const override
        {
            if (!reference_time_)
            {
                reference_time_ = std::chrono::system_clock::now();
            }
            return entry.time_point >= (*reference_time_ - duration_);
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<SincePredicate>(duration_); }
    };

    class AnyKeywordPredicate : public LogPredicate
    {
        std::vector<std::string> keywords_;
        bool case_sensitive_;

    public:
        AnyKeywordPredicate(std::vector<std::string> kw, bool cs) : keywords_(std::move(kw)), case_sensitive_(cs) {}
        bool test(const LogEntry &entry) const override
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
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AnyKeywordPredicate>(keywords_, case_sensitive_); }
    };

    class AndPredicate : public LogPredicate
    {
        std::unique_ptr<LogPredicate> a_, b_;

    public:
        AndPredicate(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b)
            : a_(std::move(a)), b_(std::move(b)) {}
        bool test(const LogEntry &entry) const override { return a_->test(entry) && b_->test(entry); }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AndPredicate>(a_->clone(), b_->clone()); }
    };

    class OrPredicate : public LogPredicate
    {
        std::unique_ptr<LogPredicate> a_, b_;

    public:
        OrPredicate(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b)
            : a_(std::move(a)), b_(std::move(b)) {}
        bool test(const LogEntry &entry) const override { return a_->test(entry) || b_->test(entry); }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<OrPredicate>(a_->clone(), b_->clone()); }
    };

    class NotPredicate : public LogPredicate
    {
        std::unique_ptr<LogPredicate> p_;

    public:
        explicit NotPredicate(std::unique_ptr<LogPredicate> p) : p_(std::move(p)) {}
        bool test(const LogEntry &entry) const override { return !p_->test(entry); }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<NotPredicate>(p_->clone()); }
    };

    class MinLevelPredicate : public LogPredicate
    {
        LogLevel min_level_;

    public:
        explicit MinLevelPredicate(LogLevel l) : min_level_(l) {}
        bool test(const LogEntry &entry) const override { return entry.level != LogLevel::UNKNOWN && entry.level >= min_level_; }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<MinLevelPredicate>(min_level_); }
    };

    class MultiLevelPredicate : public LogPredicate
    {
        std::vector<LogLevel> levels_;

    public:
        explicit MultiLevelPredicate(std::vector<LogLevel> l) : levels_(std::move(l)) {}
        bool test(const LogEntry &entry) const override
        {
            return std::find(levels_.begin(), levels_.end(), entry.level) != levels_.end();
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<MultiLevelPredicate>(levels_); }
    };

    class TimeRangePredicate : public LogPredicate
    {
        std::optional<std::chrono::system_clock::time_point> start_, end_;
        std::optional<std::string> start_s_, end_s_;

    public:
        TimeRangePredicate(std::optional<std::chrono::system_clock::time_point> s, std::optional<std::chrono::system_clock::time_point> e,
                           std::optional<std::string> ss = {}, std::optional<std::string> es = {})
            : start_(s), end_(e), start_s_(ss), end_s_(es) {}
        bool test(const LogEntry &entry) const override
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
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<TimeRangePredicate>(start_, end_, start_s_, end_s_); }
    };

    class TagPredicate : public LogPredicate
    {
        std::set<std::string> tags_;

    public:
        explicit TagPredicate(std::set<std::string> t) : tags_(std::move(t)) {}
        bool test(const LogEntry &entry) const override
        {
            for (const auto &tag : tags_)
                if (!entry.hasTag(tag))
                    return false;
            return true;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<TagPredicate>(tags_); }
    };

    class MetadataPredicate : public LogPredicate
    {
        std::optional<std::string> file_, tid_;

    public:
        MetadataPredicate(std::optional<std::string> f, std::optional<std::string> t) : file_(f), tid_(t) {}
        bool test(const LogEntry &entry) const override
        {
            if (file_ && entry.source_file != *file_)
                return false;
            if (tid_ && entry.thread_id != *tid_)
                return false;
            return true;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<MetadataPredicate>(file_, tid_); }
    };

    std::unique_ptr<LogPredicate> Level(LogLevel l) { return std::make_unique<LevelPredicate>(l); }
    std::unique_ptr<LogPredicate> MinLevel(LogLevel l) { return std::make_unique<MinLevelPredicate>(l); }
    std::unique_ptr<LogPredicate> MultiLevel(std::vector<LogLevel> levels) { return std::make_unique<MultiLevelPredicate>(std::move(levels)); }

    std::unique_ptr<LogPredicate> TimeRange(std::optional<std::chrono::system_clock::time_point> start, std::optional<std::chrono::system_clock::time_point> end)
    {
        return std::make_unique<TimeRangePredicate>(start, end);
    }

    std::unique_ptr<LogPredicate> Tag(std::string tag) { return std::make_unique<TagPredicate>(std::set<std::string>{std::move(tag)}); }
    std::unique_ptr<LogPredicate> ThreadId(std::string tid) { return std::make_unique<MetadataPredicate>(std::nullopt, std::move(tid)); }
    std::unique_ptr<LogPredicate> SourceFile(std::string file) { return std::make_unique<MetadataPredicate>(std::move(file), std::nullopt); }

    std::unique_ptr<LogPredicate> Keyword(std::string k, bool case_sensitive)
    {
        return std::make_unique<KeywordPredicate>(std::move(k), case_sensitive);
    }

    std::unique_ptr<LogPredicate> Regex(std::string pattern)
    {
        return std::make_unique<RegexPredicate>(std::move(pattern));
    }

    std::unique_ptr<LogPredicate> Attribute(std::string key, LogValue val)
    {
        return std::make_unique<AttributePredicate>(std::move(key), std::move(val));
    }

    std::unique_ptr<LogPredicate> AttributeRange(std::string key, LogValue min, LogValue max)
    {
        return std::make_unique<AttributeRangePredicate>(std::move(key), std::move(min), std::move(max));
    }

    std::unique_ptr<LogPredicate> Since(std::chrono::system_clock::duration d)
    {
        return std::make_unique<SincePredicate>(d);
    }

    std::unique_ptr<LogPredicate> AnyKeyword(std::vector<std::string> keywords, bool case_sensitive)
    {
        return std::make_unique<AnyKeywordPredicate>(std::move(keywords), case_sensitive);
    }

    std::unique_ptr<LogPredicate> And(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b)
    {
        return std::make_unique<AndPredicate>(std::move(a), std::move(b));
    }

    std::unique_ptr<LogPredicate> Or(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b)
    {
        return std::make_unique<OrPredicate>(std::move(a), std::move(b));
    }

    std::unique_ptr<LogPredicate> Not(std::unique_ptr<LogPredicate> p)
    {
        return std::make_unique<NotPredicate>(std::move(p));
    }
}

std::unique_ptr<LogPredicate> FilterOptions::toPredicate() const
{
    std::unique_ptr<LogPredicate> root = nullptr;
    auto combine = [&](std::unique_ptr<LogPredicate> next)
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

    for (const auto &[k, v] : attribute_matches)
    {
        combine(Filters::Attribute(k, v));
    }

    if (!root)
    {
        struct AllPredicate : LogPredicate
        {
            bool test(const LogEntry &) const override { return true; }
            std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AllPredicate>(); }
        };
        root = std::make_unique<AllPredicate>();
    }

    if (invert_match)
        root = Filters::Not(std::move(root));
    return root;
}

std::vector<LogEntry> LogAnalyzer::query(std::string_view query_str) const
{
    auto pred_res = LogQuery::compile(query_str);
    if (!pred_res) {
        std::cerr << "Query compilation failed: " << pred_res.error() << std::endl;
        return {};
    }
    return getFilteredEntries(**pred_res);
}

// --- Exporters Implementation ---

void JsonExporter::exportStats(const LogStatistics &stats)
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
        out_ << "    \"" << LogEntry::levelToString(level) << "\": " << count;
        first = false;
    }
    out_ << "\n  }\n}\n";
}

void JsonExporter::exportEntries(std::span<const LogEntry> entries)
{
    out_ << "[\n";
    for (size_t i = 0; i < entries.size(); ++i)
    {
        out_ << entries[i].toJson({.pretty = pretty_});
        if (i < entries.size() - 1)
            out_ << ",\n";
    }
    out_ << "\n]\n";
}

void CsvExporter::exportStats(const LogStatistics &stats)
{
    out_ << "Metric,Value\n";
    out_ << "total_entries," << stats.total_entries << "\n";
    out_ << "duration_seconds," << stats.duration.count() << "\n";
}

void CsvExporter::exportEntries(std::span<const LogEntry> entries)
{
    out_ << "Timestamp,Level,Message,ThreadId\n";
    for (const auto &entry : entries)
    {
        out_ << "\"" << entry.timestamp << "\",";
        out_ << "\"" << LogEntry::levelToString(entry.level) << "\",";
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

void MarkdownExporter::exportStats(const LogStatistics &stats)
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
        out_ << "| " << LogEntry::levelToString(level) << " | " << count << " |\n";
    }
}

void MarkdownExporter::exportEntries(std::span<const LogEntry> entries)
{
    out_ << "| Timestamp | Level | Message |\n";
    out_ << "| :--- | :--- | :--- |\n";
    for (const auto &entry : entries)
    {
        out_ << "| " << entry.timestamp << " | " << LogEntry::levelToString(entry.level) << " | " << entry.message << " |\n";
    }
}

void ConsoleExporter::exportStats(const LogStatistics &stats)
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
        out_ << "  " << std::left << std::setw(10) << LogEntry::levelToString(level) << ": " << count << "\n";
    }
}

void ConsoleExporter::exportEntries(std::span<const LogEntry> entries)
{
    for (const auto &entry : entries)
    {
        if (use_color_)
        {
            std::string color = "";
            switch (entry.level)
            {
            case LogLevel::ERROR:
            case LogLevel::CRITICAL:
                color = "\033[31m";
                break; // Red
            case LogLevel::WARNING:
                color = "\033[33m";
                break; // Yellow
            case LogLevel::INFO:
                color = "\033[32m";
                break; // Green
            case LogLevel::DEBUG:
                color = "\033[34m";
                break; // Blue
            default:
                break;
            }
            out_ << "\033[90m[" << entry.timestamp << "]\033[0m " << color << "[" << std::setw(7) << LogEntry::levelToString(entry.level) << "]\033[0m " << entry.message << "\n";
        }
        else
        {
            out_ << "[" << entry.timestamp << "] [" << std::setw(7) << LogEntry::levelToString(entry.level) << "] " << entry.message << "\n";
        }
    }
}

// --- LogAnalyzer Implementation ---

void LogAnalyzer::createIndex(IndexType type, const std::string& attribute_name) {
    std::unique_lock lock(rw_mutex_);
    if (type == IndexType::Timestamp) {
        timestamp_index_.resize(entries_.size());
        std::iota(timestamp_index_.begin(), timestamp_index_.end(), 0);
        std::sort(timestamp_index_.begin(), timestamp_index_.end(), [this](size_t a, size_t b) {
            return entries_[a].time_point < entries_[b].time_point;
        });
    } else if (type == IndexType::Level) {
        level_index_.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            level_index_[entries_[i].level].push_back(i);
        }
    } else if (type == IndexType::Attribute && !attribute_name.empty()) {
        auto& idx = attribute_indices_[attribute_name];
        idx.clear();
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (auto val = entries_[i].getAttribute(attribute_name)) {
                idx[*val].push_back(i);
            }
        }
    }
}

LogAnalyzer::LogAnalyzer() : rw_mutex_()
{
    legacy_timestamp_regex_ = std::regex(R"(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?)\]?)");
    legacy_level_regex_ = std::regex(R"(\[?(DEBUG|INFO|WARNING|WARN|ERROR|ERR|CRITICAL|CRIT|FATAL)\]?)");
}

void LogAnalyzer::setParsingConfig(const ParsingConfig &config)
{
    config_ = config;
    std::string processed_pattern = config.line_pattern;
    named_group_indices_.clear();

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
                            named_group_indices_[name] = current_group;
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
            strict_regex_ = std::regex(final_pattern);
        }
        catch (const std::regex_error &e)
        {
            std::cerr << "Invalid regex pattern: " << e.what() << " (processed from " << config.line_pattern << ")" << std::endl;
        }
    }

    if (config_.entry_start_pattern)
    {
        try
        {
            entry_start_regex_ = std::regex(*config_.entry_start_pattern);
        }
        catch (const std::regex_error &e)
        {
            std::cerr << "Invalid entry start pattern: " << e.what() << std::endl;
        }
    }
}

void LogAnalyzer::setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex)
{
    legacy_timestamp_regex_ = std::regex(std::string(timestamp_regex));
    legacy_level_regex_ = std::regex(std::string(level_regex));
    config_.line_pattern.clear();
    named_group_indices_.clear();
}

std::expected<LoadResult, std::string> LogAnalyzer::loadFileWithStats(
    const std::filesystem::path &filepath,
    ProgressCallback progress)
{
    LoadResult result = {0, 0};
    std::vector<LogEntry> new_entries;

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
            LogEntry entry = parseLogLine(current_entry_buffer, entry_line_start);
            if (config_.strict_mode && entry.timestamp.empty() && entry.message.empty())
            {
                result.error_count++;
            }
            else
            {
                applyEnrichers(entry);
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
            if (entry_start_regex_)
            {
                is_new_entry = std::regex_search(line, *entry_start_regex_);
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
                    (config_.max_continuation_lines > 0 &&
                     static_cast<size_t>(std::count(current_entry_buffer.begin(), current_entry_buffer.end(), '\n')) >= config_.max_continuation_lines))
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

            if (config_.max_errors > 0 && result.error_count >= config_.max_errors)
            {
                break;
            }
        }
        if (config_.max_errors == 0 || result.error_count < config_.max_errors)
        {
            process_buffer();
        }

        if (progress)
            progress({bytes_processed, total_bytes, line_number});

        {
            std::unique_lock lock(rw_mutex_);
            entries_ = std::move(new_entries);
            cached_stats_.reset();
        }
    }
    catch (const std::exception &e)
    {
        return std::unexpected(e.what());
    }

    return result;
}

std::expected<void, std::string> LogAnalyzer::loadFile(const std::filesystem::path &filepath)
{
    auto result = loadFileWithStats(filepath);
    if (!result)
        return std::unexpected(result.error());
    return {};
}

std::future<LoadResult> LogAnalyzer::loadFileAsync(
    std::filesystem::path filepath,
    ProgressCallback progress)
{
    return std::async(std::launch::async, [this, filepath, progress]() -> LoadResult
                      {
        auto result = this->loadFileWithStats(filepath, progress);
        if (result) return *result;
        throw std::runtime_error(result.error()); });
}

std::future<LoadResult> LogAnalyzer::loadParallel(std::filesystem::path path, ParallelConfig config)
{
    return std::async(std::launch::async, [this, path, config]() -> LoadResult
                      {
        {
            std::unique_lock lock(rw_mutex_);
            entries_.clear();
            cached_stats_.reset();
        }

        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Could not open file: " + path.string());
        
        size_t total_size = file.tellg();
        size_t chunk_size = config.chunk_size_mb * 1024 * 1024;
        
        std::vector<std::future<std::pair<std::vector<LogEntry>, size_t>>> futures;
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
                std::vector<LogEntry> chunk_entries;
                std::string buffer;
                size_t lines_read = 0;

                while (f.tellg() < static_cast<std::streampos>(end) && std::getline(f, line)) {
                    lines_read++;
                    bool is_new = true;
                    if (entry_start_regex_) is_new = std::regex_search(line, *entry_start_regex_);
                    
                    if (is_new && !buffer.empty()) {
                        LogEntry entry = parseLogLine(buffer);
                        if (!entry.timestamp.empty() || !entry.message.empty()) {
                            applyEnrichers(entry);
                            chunk_entries.push_back(std::move(entry));
                        } else total_errors.fetch_add(1);
                        buffer = line;
                    } else {
                        if (!buffer.empty()) buffer += "\n";
                        buffer += line;
                    }
                }
                if (!buffer.empty()) {
                    LogEntry entry = parseLogLine(buffer);
                    if (!entry.timestamp.empty() || !entry.message.empty()) {
                        applyEnrichers(entry);
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
            std::unique_lock lock(rw_mutex_);
            entries_.insert(entries_.end(), std::make_move_iterator(chunk_entries.begin()), std::make_move_iterator(chunk_entries.end()));
        }
        res.error_count = total_errors.load();
        if (config.progress) config.progress({total_size, total_size, total_lines});
        return res; });
}

bool LogAnalyzer::loadLogFile(const std::string &filepath) { return loadLogFile(std::filesystem::path(filepath)); }
bool LogAnalyzer::loadLogFile(const std::filesystem::path &filepath)
{
    auto result = loadFile(filepath);
    if (!result)
    {
        std::cerr << "Error: " << result.error() << std::endl;
        return false;
    }
    return true;
}

std::generator<LogEntry> LogAnalyzer::streamEntries(std::filesystem::path filepath)
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
        if (entry_start_regex_)
            is_new = std::regex_search(line, *entry_start_regex_);

        if (is_new && !buffer.empty())
        {
            LogEntry entry = parseLogLine(buffer, line_number);
            applyEnrichers(entry);
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
        LogEntry entry = parseLogLine(buffer, line_number);
        applyEnrichers(entry);
        co_yield entry;
    }
}

std::generator<LogEntry> LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, FilterOptions options)
{
    auto pred = options.toPredicate();
    for (const auto &entry : streamEntries(filepath))
    {
        if (pred->test(entry))
            co_yield entry;
    }
}

std::generator<LogEntry> LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, const LogPredicate &predicate)
{
    for (const auto &entry : streamEntries(filepath))
    {
        if (predicate.test(entry))
            co_yield entry;
    }
}

std::expected<LogStatistics, std::string> LogAnalyzer::analyzeStream(const std::filesystem::path &filepath)
{
    LogStatistics stats;
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
            if (entry.level == LogLevel::ERROR)
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

std::map<LogValue, size_t> LogAnalyzer::getAttributeFrequency(std::string_view attr_key) const
{
    std::map<LogValue, size_t> frequency;
    std::string key(attr_key);
    std::shared_lock lock(rw_mutex_);
    for (const auto &entry : entries_)
    {
        auto it = entry.attributes.find(key);
        if (it != entry.attributes.end())
        {
            frequency[it->second]++;
        }
    }
    return frequency;
}

std::vector<std::pair<std::chrono::system_clock::time_point, size_t>> LogAnalyzer::getTimeline(std::chrono::system_clock::duration bucket_size) const
{
    if (bucket_size.count() <= 0)
        return {};
    std::map<std::chrono::system_clock::time_point, size_t> buckets;
    for (const auto &entry : entries_)
    {
        if (entry.time_point.time_since_epoch().count() == 0)
            continue;
        auto bucket_start = std::chrono::time_point<std::chrono::system_clock>(
            entry.time_point.time_since_epoch() - (entry.time_point.time_since_epoch() % bucket_size));
        buckets[bucket_start]++;
    }
    return {buckets.begin(), buckets.end()};
}

std::vector<LogEntry> LogAnalyzer::getTrace(std::string_view trace_id) const
{
    std::vector<LogEntry> result;
    std::shared_lock lock(rw_mutex_);
    for (const auto &entry : entries_)
    {
        if (entry.trace_id == trace_id)
        {
            result.push_back(entry);
        }
    }
    return result;
}

std::vector<LogEntry> LogAnalyzer::getEntriesSpan() const
{
    std::shared_lock lock(rw_mutex_);
    return entries_;
}
std::vector<LogEntry> LogAnalyzer::getEntries() const
{
    std::shared_lock lock(rw_mutex_);
    return entries_;
}
void LogAnalyzer::addEntry(LogEntry entry)
{
    applyEnrichers(entry);
    std::unique_lock lock(rw_mutex_);
    entries_.push_back(std::move(entry));
    cached_stats_.reset();
}

LogEntry LogAnalyzer::parseLogLine(const std::string &line, size_t line_number)
{
    LogEntry entry;
    entry.raw_line = line;

    if (!config_.line_pattern.empty())
    {
        std::smatch match;
        if (std::regex_match(line, match, strict_regex_))
        {
            auto get_val = [&](const std::string &name) -> std::string
            {
                auto it = named_group_indices_.find(name);
                if (it != named_group_indices_.end() && static_cast<size_t>(it->second) < match.size())
                {
                    return match[it->second].str();
                }
                return "";
            };

            // Named capture groups mapping
            for (const auto &[group_name, field_name] : config_.field_mapping)
            {
                std::string val = get_val(group_name);
                if (val.empty())
                    continue;
                if (field_name == "timestamp")
                    entry.timestamp = val;
                else if (field_name == "level")
                    entry.level = LogEntry::parseLevel(val);
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
            if (entry.timestamp.empty() && config_.timestamp_index > 0 && (size_t)config_.timestamp_index < match.size())
                entry.timestamp = match[config_.timestamp_index].str();

            if (entry.level == LogLevel::UNKNOWN && config_.level_index > 0 && (size_t)config_.level_index < match.size())
                entry.level = LogEntry::parseLevel(match[config_.level_index].str());

            if (entry.message.empty() && config_.message_index > 0 && (size_t)config_.message_index < match.size())
                entry.message = match[config_.message_index].str();

            if (entry.thread_id.empty() && config_.thread_id_index > 0 && (size_t)config_.thread_id_index < match.size())
                entry.thread_id = match[config_.thread_id_index].str();

            if (entry.source_file.empty() && config_.file_index > 0 && (size_t)config_.file_index < match.size())
                entry.source_file = match[config_.file_index].str();

            if (entry.source_line == 0 && config_.line_index > 0 && (size_t)config_.line_index < match.size())
                try
                {
                    entry.source_line = std::stoi(match[config_.line_index].str());
                }
                catch (...)
                {
                }

            if (!entry.timestamp.empty())
            {
                std::istringstream ss(entry.timestamp);
                std::tm tm = {};
                ss >> std::get_time(&tm, config_.time_format.c_str());
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
        else if (config_.strict_mode)
        {
            if (config_.error_callback)
                config_.error_callback({line_number, line, "Regex match failed"});
            return LogEntry();
        }
    }

    std::smatch match;
    if (std::regex_search(line, match, legacy_timestamp_regex_))
    {
        entry.timestamp = match[1].str();
        entry.parseTime();
    }
    if (std::regex_search(line, match, legacy_level_regex_))
    {
        entry.level = LogEntry::parseLevel(match[1].str());
    }
    else
        entry.level = LogLevel::UNKNOWN;

    if (entry.level != LogLevel::UNKNOWN && !match.empty())
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

bool LogAnalyzer::matchFilter(const LogEntry &entry, const FilterOptions &options) const
{
    return options.toPredicate()->test(entry);
}

void LogAnalyzer::addEnricher(LogEnricher enricher) { enrichers_.push_back(std::move(enricher)); }
void LogAnalyzer::applyEnrichers(LogEntry &entry)
{
    for (auto &e : enrichers_)
        e(entry);
}

// Implement `analyze()` and update `getStatistics()` for caching
void LogAnalyzer::analyze()
{
    LogStatistics stats;
    std::shared_lock lock(rw_mutex_); // Acquire read lock to safely access entries_
    stats.total_entries = entries_.size();
    if (entries_.empty())
    {
        cached_stats_ = stats; // Cache empty stats
        return;
    }

    std::map<std::string, size_t> error_counts;
    auto min_max_it = std::minmax_element(entries_.begin(), entries_.end(),
                                          [](const LogEntry &a, const LogEntry &b)
                                          {
                                              return a.time_point < b.time_point;
                                          });

    if (min_max_it.first != entries_.end() && min_max_it.first->time_point.time_since_epoch().count() > 0)
    {
        stats.first_timestamp = min_max_it.first->timestamp;
        stats.last_timestamp = min_max_it.second->timestamp;
        stats.duration = std::chrono::duration_cast<std::chrono::seconds>(min_max_it.second->time_point - min_max_it.first->time_point);
        if (stats.duration.count() > 0)
        {
            stats.entries_per_second = static_cast<double>(stats.total_entries) / stats.duration.count();
        }
    }

    for (const auto &entry : entries_)
    {
        stats.level_counts[entry.level]++;
        if (entry.level == LogLevel::ERROR || entry.level == LogLevel::CRITICAL)
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

    cached_stats_ = std::move(stats);
}

LogStatistics LogAnalyzer::getStatistics() const
{
    std::shared_lock lock(rw_mutex_);
    if (cached_stats_)
    {
        return *cached_stats_;
    }
    lock.unlock();

    const_cast<LogAnalyzer *>(this)->analyze();

    lock.lock();
    return *cached_stats_;
}

// --- New aggregation methods ---
std::map<LogValue, size_t> LogAnalyzer::getFrequencyMap(std::string_view attribute_key) const
{
    std::map<LogValue, size_t> frequency;
    std::string key(attribute_key);
    std::shared_lock lock(rw_mutex_);
    for (const auto &entry : entries_)
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
void LogAnalyzer::sort(std::function<bool(const LogEntry &, const LogEntry &)> cmp)
{
    std::unique_lock lock(rw_mutex_); // Unique lock for modifying entries_
    std::sort(entries_.begin(), entries_.end(), cmp);
    cached_stats_.reset(); // Invalidate cache
}

// In-place removal
void LogAnalyzer::removeIf(const LogPredicate &predicate)
{
    std::unique_lock lock(rw_mutex_); // Unique lock for modifying entries_
    auto it = std::remove_if(entries_.begin(), entries_.end(),
                             [&](const LogEntry &entry)
                             {
                                 return predicate.test(entry);
                             });
    entries_.erase(it, entries_.end());
    cached_stats_.reset(); // Invalidate cache
}

// Bulk transformation
void LogAnalyzer::transform(std::function<void(LogEntry &)> transformer)
{
    std::unique_lock lock(rw_mutex_); // Unique lock for modifying entries_
    for (auto &entry : entries_)
    {
        transformer(entry); // Apply transformer to each entry
    }
    cached_stats_.reset(); // Invalidate cache
}

// New generic export methods
void LogAnalyzer::exportTo(LogExporter &exporter) const
{
    std::shared_lock lock(rw_mutex_); // Read lock for entries_
    exporter.exportEntries({entries_.data(), entries_.size()});
}

void LogAnalyzer::exportFilteredTo(LogExporter &exporter, const LogPredicate &predicate) const
{
    std::vector<LogEntry> filtered_entries;
    {
        std::shared_lock lock(rw_mutex_); // Read lock for entries_
        for (const auto &entry : entries_)
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

void LogAnalyzer::exportStatistics(LogExporter &exporter) const
{
    // getStatistics() will ensure analyze() is called and cache is populated
    LogStatistics stats = getStatistics();
    exporter.exportStats(stats);
}

void LogAnalyzer::writeStatistics(std::ostream &out, bool as_json) const
{
    if (as_json)
    {
        JsonExporter exporter(out, true); // Assuming pretty JSON for direct JSON output
        exportStatistics(exporter);
    }
    else
    {
        ConsoleExporter exporter(out);
        exportStatistics(exporter);
    }
}

void LogAnalyzer::writeFilteredEntries(std::ostream &out, const FilterOptions &options, bool as_json) const
{
    auto pred = options.toPredicate(); // Create predicate from options
    if (as_json)
    {
        JsonExporter exporter(out, true); // Assuming pretty JSON
        exportFilteredTo(exporter, *pred);
    }
    else
    {
        ConsoleExporter exporter(out);
        exportFilteredTo(exporter, *pred);
    }
}

void LogAnalyzer::printStatistics() const
{
    ConsoleExporter exporter(std::cout);
    exportStatistics(exporter);
}
std::vector<LogEntry> LogAnalyzer::getFilteredEntries(const FilterOptions &options) const { return getFilteredEntries(*options.toPredicate()); }
std::vector<LogEntry> LogAnalyzer::getFilteredEntries(const LogPredicate &predicate) const
{
    std::vector<LogEntry> result;
    std::shared_lock lock(rw_mutex_);
    for (const auto &entry : entries_)
        if (predicate.test(entry))
            result.push_back(entry);
    return result;
}

std::string LogAnalyzer::levelToString(LogLevel level) const { return std::string(LogEntry::levelToString(level)); }

std::expected<NumericStats, std::string> LogAnalyzer::analyzeMetric(
    std::string_view attribute_key, 
    const std::vector<int>& percentiles
) const {
    std::shared_lock lock(rw_mutex_);
    std::vector<double> values;
    std::string key(attribute_key);
    
    for (const auto& entry : entries_) {
        if (auto val = entry.getAttribute(key)) {
             if (std::holds_alternative<double>(*val)) values.push_back(std::get<double>(*val));
             else if (std::holds_alternative<int64_t>(*val)) values.push_back(static_cast<double>(std::get<int64_t>(*val)));
             else if (std::holds_alternative<uint64_t>(*val)) values.push_back(static_cast<double>(std::get<uint64_t>(*val)));
        }
    }

    if (values.empty()) return std::unexpected("No numeric values found for attribute");

    std::sort(values.begin(), values.end());
    
    NumericStats stats;
    stats.min = values.front();
    stats.max = values.back();
    double sum = 0;
    for(double v : values) sum += v;
    stats.avg = sum / values.size();
    
    double sq_sum = 0;
    for(double v : values) sq_sum += (v - stats.avg) * (v - stats.avg);
    stats.std_dev = std::sqrt(sq_sum / values.size());

    for (int p : percentiles) {
        if (p < 0 || p > 100) continue;
        size_t idx = (size_t)std::ceil((p / 100.0) * values.size()) - 1;
        if (idx >= values.size()) idx = values.size() - 1;
        stats.percentiles[p] = values[idx];
    }
    
    return stats;
}

std::generator<LogEntry> LogAnalyzer::tailFile(
    std::filesystem::path filepath, 
    std::chrono::milliseconds polling_interval
) {
    std::ifstream file(filepath, std::ios::ate); // Start at end
    if (!file.is_open()) throw std::runtime_error("Could not open file for tailing");
    
    auto last_pos = file.tellg();
    std::string buffer;
    
    while (true) {
        file.clear(); // Clear EOF flags
        file.seekg(last_pos);
        
        std::string line;
        while (std::getline(file, line)) {
            bool is_new = true;
            if (entry_start_regex_) is_new = std::regex_search(line, *entry_start_regex_);
            
            if (is_new && !buffer.empty()) {
                LogEntry entry = parseLogLine(buffer);
                applyEnrichers(entry);
                co_yield entry;
                buffer = line;
            } else {
                if (!buffer.empty()) buffer += "\n";
                buffer += line;
            }
        }
        last_pos = file.tellg();
        
        std::this_thread::sleep_for(polling_interval);
        // Note: Generator cancellation handles the loop break via destructor
    }
}

std::expected<void, std::string> LogAnalyzer::saveState(const std::filesystem::path& path) const {
    std::shared_lock lock(rw_mutex_);
    try {
        std::ofstream out(path, std::ios::binary);
        if (!out) return std::unexpected("Could not open file for writing");
        
        // Header "LOGA" + Version 1
        out.write("LOGA", 4);
        uint32_t version = 1;
        out.write(reinterpret_cast<char*>(&version), sizeof(version));
        
        uint64_t count = entries_.size();
        out.write(reinterpret_cast<char*>(&count), sizeof(count));
        
        for (const auto& entry : entries_) {
            // Very simple serialization for demo
            // Serialize Map
            auto map = entry.toMap();
            std::string json = entry.toJson(); // Use JSON as intermediate binary payload for simplicity in this iteration
            uint32_t size = json.size();
            out.write(reinterpret_cast<char*>(&size), sizeof(size));
            out.write(json.data(), size);
        }
    } catch (const std::exception& e) {
        return std::unexpected(e.what());
    }
    return {};
}

std::expected<void, std::string> LogAnalyzer::loadState(const std::filesystem::path& path) {
    std::unique_lock lock(rw_mutex_);
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::unexpected("Could not open file for reading");
        
        char magic[4];
        in.read(magic, 4);
        if (strncmp(magic, "LOGA", 4) != 0) return std::unexpected("Invalid file format");
        
        uint32_t version;
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        
        uint64_t count;
        in.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        entries_.clear();
        entries_.reserve(count);
        
        for (uint64_t i = 0; i < count; ++i) {
            uint32_t size;
            in.read(reinterpret_cast<char*>(&size), sizeof(size));
            std::string json(size, '\0');
            in.read(json.data(), size);
            
            auto res = LogEntry::fromJson(json);
            if (res) entries_.push_back(std::move(*res));
        }
        cached_stats_.reset();
    } catch (const std::exception& e) {
        return std::unexpected(e.what());
    }
    return {};
}

void LogAnalyzer::rebuildIndices() {
    // Rebuild all declared indices
    // Not implemented fully as we don't track which indices were created.
    // In a real system, we'd store the configured indices.
}
