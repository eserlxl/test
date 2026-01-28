#include "LogAnalyzer.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>

// --- Filters Implementation ---

namespace Filters {
    class LevelPredicate : public LogPredicate {
        LogLevel level_;
    public:
        explicit LevelPredicate(LogLevel l) : level_(l) {}
        bool test(const LogEntry& entry) const override { return entry.level == level_; }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<LevelPredicate>(level_); }
    };

    class KeywordPredicate : public LogPredicate {
        std::string keyword_;
        bool case_sensitive_;
    public:
        KeywordPredicate(std::string k, bool cs) : keyword_(std::move(k)), case_sensitive_(cs) {}
        bool test(const LogEntry& entry) const override {
            if (case_sensitive_) {
                return entry.message.find(keyword_) != std::string::npos;
            } else {
                auto it = std::search(
                    entry.message.begin(), entry.message.end(),
                    keyword_.begin(), keyword_.end(),
                    [](char ch1, char ch2) { 
                        return std::toupper(static_cast<unsigned char>(ch1)) == 
                               std::toupper(static_cast<unsigned char>(ch2)); 
                    }
                );
                return it != entry.message.end();
            }
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<KeywordPredicate>(keyword_, case_sensitive_); }
    };

    class RegexPredicate : public LogPredicate {
        std::regex regex_;
        std::string pattern_;
    public:
        explicit RegexPredicate(std::string p) : regex_(p), pattern_(std::move(p)) {}
        bool test(const LogEntry& entry) const override {
            return std::regex_search(entry.message, regex_);
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<RegexPredicate>(pattern_); }
    };

    class AttributePredicate : public LogPredicate {
        std::string key_;
        LogValue value_;
    public:
        AttributePredicate(std::string k, LogValue v) : key_(std::move(k)), value_(std::move(v)) {}
        bool test(const LogEntry& entry) const override {
            auto it = entry.attributes.find(key_);
            return it != entry.attributes.end() && it->second == value_;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AttributePredicate>(key_, value_); }
    };

    class AndPredicate : public LogPredicate {
        std::unique_ptr<LogPredicate> a_, b_;
    public:
        AndPredicate(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b) 
            : a_(std::move(a)), b_(std::move(b)) {}
        bool test(const LogEntry& entry) const override { return a_->test(entry) && b_->test(entry); }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AndPredicate>(a_->clone(), b_->clone()); }
    };

    class OrPredicate : public LogPredicate {
        std::unique_ptr<LogPredicate> a_, b_;
    public:
        OrPredicate(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b) 
            : a_(std::move(a)), b_(std::move(b)) {}
        bool test(const LogEntry& entry) const override { return a_->test(entry) || b_->test(entry); }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<OrPredicate>(a_->clone(), b_->clone()); }
    };

    class NotPredicate : public LogPredicate {
        std::unique_ptr<LogPredicate> p_;
    public:
        explicit NotPredicate(std::unique_ptr<LogPredicate> p) : p_(std::move(p)) {}
        bool test(const LogEntry& entry) const override { return !p_->test(entry); }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<NotPredicate>(p_->clone()); }
    };

    class MinLevelPredicate : public LogPredicate {
        LogLevel min_level_;
    public:
        explicit MinLevelPredicate(LogLevel l) : min_level_(l) {}
        bool test(const LogEntry& entry) const override { return entry.level != LogLevel::UNKNOWN && entry.level >= min_level_; }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<MinLevelPredicate>(min_level_); }
    };

    class MultiLevelPredicate : public LogPredicate {
        std::vector<LogLevel> levels_;
    public:
        explicit MultiLevelPredicate(std::vector<LogLevel> l) : levels_(std::move(l)) {}
        bool test(const LogEntry& entry) const override {
            return std::find(levels_.begin(), levels_.end(), entry.level) != levels_.end();
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<MultiLevelPredicate>(levels_); }
    };

    class TimeRangePredicate : public LogPredicate {
        std::optional<std::chrono::system_clock::time_point> start_, end_;
        std::optional<std::string> start_s_, end_s_;
    public:
        TimeRangePredicate(std::optional<std::chrono::system_clock::time_point> s, std::optional<std::chrono::system_clock::time_point> e,
                           std::optional<std::string> ss = {}, std::optional<std::string> es = {})
            : start_(s), end_(e), start_s_(ss), end_s_(es) {}
        bool test(const LogEntry& entry) const override {
            if (start_ && entry.time_point < *start_) return false;
            if (end_ && entry.time_point > *end_) return false;
            if (start_s_ && entry.timestamp < *start_s_) return false;
            if (end_s_ && entry.timestamp > *end_s_) return false;
            return true;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<TimeRangePredicate>(start_, end_, start_s_, end_s_); }
    };

    class TagPredicate : public LogPredicate {
        std::set<std::string> tags_;
    public:
        explicit TagPredicate(std::set<std::string> t) : tags_(std::move(t)) {}
        bool test(const LogEntry& entry) const override {
            for (const auto& tag : tags_) if (!entry.hasTag(tag)) return false;
            return true;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<TagPredicate>(tags_); }
    };

    class MetadataPredicate : public LogPredicate {
        std::optional<std::string> file_, tid_;
    public:
        MetadataPredicate(std::optional<std::string> f, std::optional<std::string> t) : file_(f), tid_(t) {}
        bool test(const LogEntry& entry) const override {
            if (file_ && entry.source_file != *file_) return false;
            if (tid_ && entry.thread_id != *tid_) return false;
            return true;
        }
        std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<MetadataPredicate>(file_, tid_); }
    };

    std::unique_ptr<LogPredicate> Level(LogLevel l) { return std::make_unique<LevelPredicate>(l); }
    std::unique_ptr<LogPredicate> MinLevel(LogLevel l) { return std::make_unique<MinLevelPredicate>(l); }
    
    std::unique_ptr<LogPredicate> Keyword(std::string k, bool case_sensitive) { 
        return std::make_unique<KeywordPredicate>(std::move(k), case_sensitive); 
    }
    
    std::unique_ptr<LogPredicate> Regex(std::string pattern) { 
        return std::make_unique<RegexPredicate>(std::move(pattern)); 
    }
    
    std::unique_ptr<LogPredicate> Attribute(std::string key, LogValue val) { 
        return std::make_unique<AttributePredicate>(std::move(key), std::move(val)); 
    }
    
    std::unique_ptr<LogPredicate> And(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b) { 
        return std::make_unique<AndPredicate>(std::move(a), std::move(b)); 
    }
    
    std::unique_ptr<LogPredicate> Or(std::unique_ptr<LogPredicate> a, std::unique_ptr<LogPredicate> b) { 
        return std::make_unique<OrPredicate>(std::move(a), std::move(b)); 
    }
    
    std::unique_ptr<LogPredicate> Not(std::unique_ptr<LogPredicate> p) { 
        return std::make_unique<NotPredicate>(std::move(p)); 
    }
}

std::unique_ptr<LogPredicate> FilterOptions::toPredicate() const {
    std::unique_ptr<LogPredicate> root = nullptr;
    auto combine = [&](std::unique_ptr<LogPredicate> next) {
        if (!root) root = std::move(next);
        else root = Filters::And(std::move(root), std::move(next));
    };

    if (level) combine(std::make_unique<Filters::MinLevelPredicate>(*level));
    if (!levels.empty()) combine(std::make_unique<Filters::MultiLevelPredicate>(levels));
    if (keyword) combine(Filters::Keyword(*keyword, case_sensitive));
    if (message_regex_pattern) combine(Filters::Regex(*message_regex_pattern));
    if (start_tp || end_tp || start_time || end_time) combine(std::make_unique<Filters::TimeRangePredicate>(start_tp, end_tp, start_time, end_time));
    if (source_file || thread_id) combine(std::make_unique<Filters::MetadataPredicate>(source_file, thread_id));
    if (!required_tags.empty()) combine(std::make_unique<Filters::TagPredicate>(required_tags));
    
    for (const auto& [k, v] : attribute_matches) {
        combine(Filters::Attribute(k, v));
    }

    if (!root) {
        struct AllPredicate : LogPredicate {
            bool test(const LogEntry&) const override { return true; }
            std::unique_ptr<LogPredicate> clone() const override { return std::make_unique<AllPredicate>(); }
        };
        root = std::make_unique<AllPredicate>();
    }

    if (invert_match) root = Filters::Not(std::move(root));
    return root;
}


// --- Exporters Implementation ---

void JsonExporter::exportStats(const LogStatistics& stats) {
    out_ << "{\n";
    out_ << "  \"total_entries\": " << stats.total_entries << ",\n";
    out_ << "  \"duration_seconds\": " << stats.duration.count() << ",\n";
    out_ << "  \"level_counts\": {\n";
    bool first = true;
    for (const auto& [level, count] : stats.level_counts) {
        if (!first) out_ << ",\n";
        out_ << "    \"" << LogEntry::levelToString(level) << "\": " << count;
        first = false;
    }
    out_ << "\n  }\n}\n";
}

void JsonExporter::exportEntries(std::span<const LogEntry> entries) {
    out_ << "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        out_ << entries[i].toJson({.pretty = pretty_});
        if (i < entries.size() - 1) out_ << ",\n";
    }
    out_ << "\n]\n";
}

void CsvExporter::exportStats(const LogStatistics& stats) {
    out_ << "Metric,Value\n";
    out_ << "total_entries," << stats.total_entries << "\n";
    out_ << "duration_seconds," << stats.duration.count() << "\n";
}

void CsvExporter::exportEntries(std::span<const LogEntry> entries) {
    out_ << "Timestamp,Level,Message,ThreadId\n";
    for (const auto& entry : entries) {
        out_ << "\"" << entry.timestamp << "\",";
        out_ << "\"" << LogEntry::levelToString(entry.level) << "\",";
        // Escape quotes in message
        std::string msg = entry.message;
        size_t pos = 0;
        while ((pos = msg.find("\"", pos)) != std::string::npos) {
            msg.replace(pos, 1, "\"\"");
            pos += 2;
        }
        out_ << "\"" << msg << "\",";
        out_ << "\"" << entry.thread_id << "\"\n";
    }
}

// --- LogAnalyzer Implementation ---

LogAnalyzer::LogAnalyzer() {
    legacy_timestamp_regex_ = std::regex(R"(\[?(\d{4}-\d{2}-\d{2}[\sT]\d{2}:\d{2}:\d{2}(?:\.\d+)?(?:Z|[+-]\d{2}:\d{2})?)\]?)");
    legacy_level_regex_ = std::regex(R"(\[?(DEBUG|INFO|WARNING|WARN|ERROR|ERR|CRITICAL|CRIT|FATAL)\]?)");
}

void LogAnalyzer::setParsingConfig(const ParsingConfig& config) {
    config_ = config;
    std::string processed_pattern = config.line_pattern;
    named_group_indices_.clear();

    if (!processed_pattern.empty()) {
        std::string final_pattern;
        int current_group = 0;
        for (size_t i = 0; i < processed_pattern.size(); ++i) {
            if (processed_pattern[i] == '(') {
                if (i + 1 < processed_pattern.size() && processed_pattern[i+1] == '?') {
                    if (i + 2 < processed_pattern.size() && processed_pattern[i+2] == ':') {
                        final_pattern += "(?:";
                        i += 2;
                    } else if (i + 2 < processed_pattern.size() && processed_pattern[i+2] == '<') {
                        current_group++;
                        size_t end_bracket = processed_pattern.find('>', i + 3);
                        if (end_bracket != std::string::npos) {
                            std::string name = processed_pattern.substr(i + 3, end_bracket - (i + 3));
                            named_group_indices_[name] = current_group;
                            final_pattern += '(';
                            i = end_bracket;
                        } else {
                            final_pattern += '(';
                        }
                    } else {
                        current_group++;
                        final_pattern += '(';
                    }
                } else {
                    current_group++;
                    final_pattern += '(';
                }
            } else {
                final_pattern += processed_pattern[i];
            }
        }

        try {
            strict_regex_ = std::regex(final_pattern);
        } catch (const std::regex_error& e) {
            std::cerr << "Invalid regex pattern: " << e.what() << " (processed from " << config.line_pattern << ")" << std::endl;
        }
    }

    if (config_.entry_start_pattern) {
        try {
            entry_start_regex_ = std::regex(*config_.entry_start_pattern);
        } catch (const std::regex_error& e) {
            std::cerr << "Invalid entry start pattern: " << e.what() << std::endl;
        }
    }
}

void LogAnalyzer::setCustomPatterns(std::string_view timestamp_regex, std::string_view level_regex) {
    legacy_timestamp_regex_ = std::regex(std::string(timestamp_regex));
    legacy_level_regex_ = std::regex(std::string(level_regex));
    config_.line_pattern.clear();
    named_group_indices_.clear();
}

std::expected<LoadResult, std::string> LogAnalyzer::loadFileWithStats(
    const std::filesystem::path& filepath,
    ProgressCallback progress
) {
    entries_.clear();
    LoadResult result = {0, 0};
    
    try {
        std::ifstream file(filepath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            return std::unexpected("Could not open file: " + filepath.string());
        }
        
        size_t total_bytes = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::string line;
        size_t line_number = 0;
        size_t bytes_processed = 0;
        
        std::string current_entry_buffer;
        size_t entry_line_start = 1;

        auto process_buffer = [&]() {
            if (current_entry_buffer.empty()) return;
            LogEntry entry = parseLogLine(current_entry_buffer, entry_line_start);
            if (config_.strict_mode && entry.timestamp.empty() && entry.message.empty()) {
                result.error_count++;
            } else {
                applyEnrichers(entry);
                entries_.push_back(std::move(entry));
                result.loaded_count++;
            }
            current_entry_buffer.clear();
        };

        while (std::getline(file, line)) {
            line_number++;
            size_t line_bytes = line.size() + 1;
            bytes_processed += line_bytes;

            if (progress && (line_number % 100 == 0 || bytes_processed >= total_bytes)) {
                progress({bytes_processed, total_bytes, line_number});
            }

            if (line.empty()) continue;
            if (line.back() == '\r') line.pop_back();

            bool is_new_entry = true;
            if (entry_start_regex_) {
                is_new_entry = std::regex_search(line, *entry_start_regex_);
            }

            if (is_new_entry) {
                process_buffer();
                current_entry_buffer = line;
                entry_line_start = line_number;
            } else {
                if (!current_entry_buffer.empty()) {
                    current_entry_buffer += "\n" + line;
                } else {
                    current_entry_buffer = line;
                    entry_line_start = line_number;
                }
            }

            if (config_.max_errors > 0 && result.error_count >= config_.max_errors) {
                break;
            }
        }
        if (config_.max_errors == 0 || result.error_count < config_.max_errors) {
            process_buffer();
        }

        if (progress) progress({bytes_processed, total_bytes, line_number});
        
    } catch (const std::exception& e) {
        return std::unexpected(e.what());
    }
    
    return result;
}

std::expected<void, std::string> LogAnalyzer::loadFile(const std::filesystem::path& filepath) {
    auto result = loadFileWithStats(filepath);
    if (!result) return std::unexpected(result.error());
    return {};
}

std::future<LoadResult> LogAnalyzer::loadFileAsync(
    std::filesystem::path filepath, 
    ProgressCallback progress
) {
    return std::async(std::launch::async, [this, filepath, progress]() -> LoadResult {
        auto result = this->loadFileWithStats(filepath, progress);
        if (result) return *result;
        throw std::runtime_error(result.error());
    });
}

std::future<LoadResult> LogAnalyzer::loadParallel(std::filesystem::path path, ParallelConfig config) {
    return std::async(std::launch::async, [this, path, config]() -> LoadResult {
        {
            std::lock_guard lock(entries_mutex_);
            entries_.clear(); 
        }
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Could not open file");
        
        size_t total_size = file.tellg();
        size_t chunk_size = config.chunk_size_mb * 1024 * 1024;
        size_t num_chunks = (total_size + chunk_size - 1) / chunk_size;
        size_t actual_threads = std::min(num_chunks, config.thread_count);
        
        std::vector<std::future<std::vector<LogEntry>>> futures;
        std::atomic<size_t> total_errors = 0;

        for (size_t i = 0; i < actual_threads; ++i) {
            size_t start = i * (total_size / actual_threads);
            size_t end = (i == actual_threads - 1) ? total_size : (i + 1) * (total_size / actual_threads);
            
            futures.push_back(std::async(std::launch::async, [this, path, start, end, &total_errors]() {
                std::ifstream f(path, std::ios::binary);
                f.seekg(start);
                std::string line;
                if (start > 0) std::getline(f, line); // Skip partial line

                std::vector<LogEntry> chunk_entries;
                std::string buffer;
                while (f.tellg() < static_cast<std::streampos>(end) && std::getline(f, line)) {
                    bool is_new = true;
                    if (entry_start_regex_) is_new = std::regex_search(line, *entry_start_regex_);
                    
                    if (is_new && !buffer.empty()) {
                                            LogEntry entry = parseLogLine(buffer);
                                            if (!entry.timestamp.empty() || !entry.message.empty()) {
                                                // Note: applyEnrichers is not thread-safe if it modifies shared state, but here we assume enrichers are stateless or thread-safe.
                                                // Also applyEnrichers reads enrichers_ which is read-only here.
                                                applyEnrichers(entry);
                                                chunk_entries.push_back(std::move(entry));
                                            } else { // It's an invalid entry, count as error
                                                total_errors.fetch_add(1);
                                            }
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
                                        } else { // It's an invalid entry, count as error
                                            total_errors.fetch_add(1);
                                        }
                                    }
                        
                return chunk_entries;
            }));
        }

        LoadResult res = {0, 0};
        for (auto& f : futures) {
            auto entries = f.get();
            res.loaded_count += entries.size();
            std::lock_guard lock(entries_mutex_);
            entries_.insert(entries_.end(), std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end()));
        }
        res.error_count = total_errors.load();
        return res;
    });
}

bool LogAnalyzer::loadLogFile(const std::string& filepath) { return loadLogFile(std::filesystem::path(filepath)); }
bool LogAnalyzer::loadLogFile(const std::filesystem::path& filepath) {
    auto result = loadFile(filepath);
    if (!result) { std::cerr << "Error: " << result.error() << std::endl; return false; }
    return true;
}

std::generator<LogEntry> LogAnalyzer::streamEntries(std::filesystem::path filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) throw std::runtime_error("Could not open file: " + filepath.string());
    
    std::string line;
    std::string buffer;
    size_t line_number = 0;

    while (std::getline(file, line)) {
        line_number++;
        bool is_new = true;
        if (entry_start_regex_) is_new = std::regex_search(line, *entry_start_regex_);

        if (is_new && !buffer.empty()) {
            LogEntry entry = parseLogLine(buffer, line_number);
            applyEnrichers(entry);
            co_yield entry;
            buffer = line;
        } else {
            if (!buffer.empty()) buffer += "\n";
            buffer += line;
        }
    }
    if (!buffer.empty()) {
        LogEntry entry = parseLogLine(buffer, line_number);
        applyEnrichers(entry);
        co_yield entry;
    }
}

std::generator<LogEntry> LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, FilterOptions options) {
    auto pred = options.toPredicate();
    for (const auto& entry : streamEntries(filepath)) {
        if (pred->test(entry)) co_yield entry;
    }
}

std::generator<LogEntry> LogAnalyzer::streamFilteredEntries(std::filesystem::path filepath, const LogPredicate& predicate) {
    for (const auto& entry : streamEntries(filepath)) {
        if (predicate.test(entry)) co_yield entry;
    }
}

std::expected<LogStatistics, std::string> LogAnalyzer::analyzeStream(const std::filesystem::path& filepath) {
    LogStatistics stats;
    std::map<std::string, size_t> error_counts;
    try {
        for (const auto& entry : streamEntries(filepath)) {
            stats.total_entries++;
            stats.level_counts[entry.level]++;
            if (!stats.first_timestamp) stats.first_timestamp = entry.timestamp;
            stats.last_timestamp = entry.timestamp;
            if (entry.level == LogLevel::ERROR) error_counts[entry.message]++;
            if (!entry.thread_id.empty()) stats.thread_distribution[entry.thread_id]++;
            if (entry.time_point.time_since_epoch().count() > 0) {
                auto minute_tp = std::chrono::time_point_cast<std::chrono::minutes>(entry.time_point);
                stats.timeline_distribution[minute_tp]++;
            }
        }
    } catch (const std::exception& e) { return std::unexpected(e.what()); }
    
    for (const auto& [msg, count] : error_counts) stats.top_errors.push_back({msg, count});
    std::sort(stats.top_errors.begin(), stats.top_errors.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (stats.top_errors.size() > 5) stats.top_errors.resize(5);
    return stats;
}

std::span<const LogEntry> LogAnalyzer::getEntriesSpan() const { return entries_; }
const std::vector<LogEntry>& LogAnalyzer::getEntries() const { return entries_; }
void LogAnalyzer::addEntry(LogEntry entry) { applyEnrichers(entry); entries_.push_back(std::move(entry)); }

LogEntry LogAnalyzer::parseLogLine(const std::string& line, size_t line_number) {
    LogEntry entry;
    entry.raw_line = line;
    
    if (!config_.line_pattern.empty()) {
        std::smatch match;
        if (std::regex_match(line, match, strict_regex_)) {
            auto get_val = [&](const std::string& name) -> std::string {
                auto it = named_group_indices_.find(name);
                if (it != named_group_indices_.end() && static_cast<size_t>(it->second) < match.size()) {
                    return match[it->second].str();
                }
                return "";
            };

            // Named capture groups mapping
            for (const auto& [group_name, field_name] : config_.field_mapping) {
                std::string val = get_val(group_name);
                if (val.empty()) continue;
                if (field_name == "timestamp") entry.timestamp = val;
                else if (field_name == "level") entry.level = LogEntry::parseLevel(val);
                else if (field_name == "message") entry.message = val;
                else if (field_name == "thread_id") entry.thread_id = val;
                else if (field_name == "file") entry.source_file = val;
                else if (field_name == "line") try { entry.source_line = std::stoi(val); } catch(...) {}
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
                try { entry.source_line = std::stoi(match[config_.line_index].str()); } catch(...) {}

            if (!entry.timestamp.empty()) {
                std::istringstream ss(entry.timestamp);
                std::tm tm = {};
                ss >> std::get_time(&tm, config_.time_format.c_str());
                if (!ss.fail()) {
                    tm.tm_isdst = -1;
                    std::time_t tt = std::mktime(&tm);
                    if (tt != -1) entry.time_point = std::chrono::system_clock::from_time_t(tt);
                }
            }
            return entry;
        } else if (config_.strict_mode) {
            if (config_.error_callback) config_.error_callback({line_number, line, "Regex match failed"});
            return LogEntry();
        }
    }
    
    std::smatch match;
    if (std::regex_search(line, match, legacy_timestamp_regex_)) {
        entry.timestamp = match[1].str();
        entry.parseTime();
    }
    if (std::regex_search(line, match, legacy_level_regex_)) {
        entry.level = LogEntry::parseLevel(match[1].str());
    } else entry.level = LogLevel::UNKNOWN;
    
    if (entry.level != LogLevel::UNKNOWN && !match.empty()) {
        size_t message_start = match.position() + match.length();
        while (message_start < line.length() && (line[message_start] == ' ' || line[message_start] == ':' || line[message_start] == ']'))
            message_start++;
        entry.message = line.substr(message_start);
    } else entry.message = line;
    
    return entry;
}

bool LogAnalyzer::matchFilter(const LogEntry& entry, const FilterOptions& options) const {
    return options.toPredicate()->test(entry);
}

void LogAnalyzer::addEnricher(LogEnricher enricher) { enrichers_.push_back(std::move(enricher)); }
void LogAnalyzer::applyEnrichers(LogEntry& entry) { for (auto& e : enrichers_) e(entry); }

void LogAnalyzer::analyze() {}
LogStatistics LogAnalyzer::getStatistics() const {
    LogStatistics stats;
    stats.total_entries = entries_.size();
    if (entries_.empty()) return stats;
    std::map<std::string, size_t> error_counts;
    auto min_max = std::minmax_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) { return a.time_point < b.time_point; });
    if (min_max.first != entries_.end() && min_max.first->time_point.time_since_epoch().count() > 0) {
        stats.first_timestamp = min_max.first->timestamp;
        stats.last_timestamp = min_max.second->timestamp;
        stats.duration = std::chrono::duration_cast<std::chrono::seconds>(min_max.second->time_point - min_max.first->time_point);
        if (stats.duration.count() > 0) stats.entries_per_second = (double)stats.total_entries / stats.duration.count();
    }
    for (const auto& entry : entries_) {
        stats.level_counts[entry.level]++;
        if (entry.level == LogLevel::ERROR) error_counts[entry.message]++;
        if (!entry.thread_id.empty()) stats.thread_distribution[entry.thread_id]++;
        if (entry.time_point.time_since_epoch().count() > 0) {
            auto minute_tp = std::chrono::time_point_cast<std::chrono::minutes>(entry.time_point);
            stats.timeline_distribution[minute_tp]++;
        }
    }
    for (const auto& [msg, count] : error_counts) stats.top_errors.push_back({msg, count});
    std::sort(stats.top_errors.begin(), stats.top_errors.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (stats.top_errors.size() > 5) stats.top_errors.resize(5);
    return stats;
}

void LogAnalyzer::writeStatistics(std::ostream& out, bool as_json) const {
    if (as_json) {
        JsonExporter exporter(out);
        exporter.exportStats(getStatistics());
    } else {
        auto stats = getStatistics();
        out << "\n=== Log Analysis Statistics ===\nTotal entries: " << stats.total_entries << "\n";
    }
}

void LogAnalyzer::writeFilteredEntries(std::ostream& out, const FilterOptions& options, bool as_json) const {
    auto filtered = getFilteredEntries(options);
    if (as_json) { JsonExporter exporter(out); exporter.exportEntries(filtered); }
    else { for (const auto& entry : filtered) out << entry << "\n"; }
}

void LogAnalyzer::printStatistics() const { writeStatistics(std::cout, false); }
std::vector<LogEntry> LogAnalyzer::getFilteredEntries(const FilterOptions& options) const { return getFilteredEntries(*options.toPredicate()); }
std::vector<LogEntry> LogAnalyzer::getFilteredEntries(const LogPredicate& predicate) const {
    std::vector<LogEntry> result;
    for (const auto& entry : entries_) if (predicate.test(entry)) result.push_back(entry);
    return result;
}

std::string LogAnalyzer::levelToString(LogLevel level) const { return std::string(LogEntry::levelToString(level)); }

