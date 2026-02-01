#ifndef LOG_LOADER_H
#define LOG_LOADER_H

#include <analyzer/LogAnalyzer.h>
#include <filesystem>
#include <future>
#include <generator>
#include <string>
#include <vector>
#include <thread>   // For std::thread
#include <atomic>   // For std::atomic<bool>
#include <chrono>   // For std::chrono::milliseconds
#include <memory>   // For std::unique_ptr

namespace LogAnalysis {

class LogLoader {
public:
    explicit LogLoader(LogAnalyzer& analyzer);

    std::expected<void, std::string> loadFile(const std::filesystem::path& filepath);
    std::expected<std::pair<LoadResult, std::vector<LogEntry>>, std::string> loadFileWithStats(const std::filesystem::path& filepath, ProgressCallback progress = nullptr);
    std::future<LoadResult> loadFileAsync(std::filesystem::path filepath, ProgressCallback progress = nullptr);
    std::future<LoadResult> loadParallel(std::filesystem::path path, ParallelConfig config = {});
    std::expected<LoadResult, std::string> loadLogSources(const std::vector<LogAnalysis::LogSource>& sources, bool recursive_global_flag = false, ProgressCallback progress = nullptr);

    bool loadLogFile(const std::string& filepath);
    bool loadLogFile(const std::filesystem::path& filepath);

    std::generator<LogEntry> streamEntries(std::filesystem::path filepath);
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, FilterOptions options);
    std::generator<LogEntry> streamFilteredEntries(std::filesystem::path filepath, const LogPredicate& predicate);

    std::future<void> tailFile(const std::filesystem::path& filepath, std::function<void(LogEntry)> entry_callback, std::function<void(const ParseError&)> error_callback = nullptr, std::function<bool() > stop_predicate = nullptr);
    void tailFileStream(const LogSource& source, const FilterOptions& filter, const RetrievalOptions& retrieval, LogExporter& exporter, std::function<void(const ParseError&)> error_callback = nullptr);
    std::future<void> startTailing(const std::filesystem::path& path, std::chrono::milliseconds interval = std::chrono::seconds(1));
    void stopTailing();

private:
    LogAnalyzer& analyzer_;
    std::thread tailing_thread_;
    std::unique_ptr<std::atomic<bool>> stop_tailing_ptr_ = std::make_unique<std::atomic<bool>>(false);
    std::filesystem::path current_tail_path_;
    std::chrono::milliseconds tail_interval_ = std::chrono::seconds(1);
};

} // namespace LogAnalysis

#endif // LOG_LOADER_H
