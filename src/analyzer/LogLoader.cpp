#include <analyzer/LogLoader.h>
#include <analyzer/LogAnalyzer.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <regex>
#include <expected>
#include <future>
#include <generator>
#include <functional>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include <iterator>
#include <vector>

namespace LogAnalysis {

LogLoader::LogLoader(LogAnalyzer& analyzer) : analyzer_(analyzer) {}

std::expected<std::pair<LoadResult, std::vector<::LogEntry>>, std::string> LogLoader::loadFileWithStats(
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
            ::LogEntry entry = analyzer_.parseLogLine(current_entry_buffer, entry_line_start);
            if (analyzer_.getParsingConfig().strict_mode && entry.timestamp.empty() && entry.message.empty())
            {
                result.error_count++;
            }
            else
            {
                analyzer_.applyEnrichers(entry);
                analyzer_.applyAnonymizers(entry);
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
            if (analyzer_.entry_start_regex_)
            {
                is_new_entry = std::regex_search(line, *analyzer_.entry_start_regex_);
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
                    (analyzer_.getParsingConfig().max_continuation_lines > 0 &&
                     static_cast<size_t>(std::count(current_entry_buffer.begin(), current_entry_buffer.end(), '\n')) >= analyzer_.getParsingConfig().max_continuation_lines))
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

            if (analyzer_.getParsingConfig().max_errors > 0 && result.error_count >= analyzer_.getParsingConfig().max_errors)
            {
                break;
            }
        }
        if (analyzer_.getParsingConfig().max_errors == 0 || result.error_count < analyzer_.getParsingConfig().max_errors)
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

std::expected<void, std::string> LogLoader::loadFile(const std::filesystem::path &filepath)
{
    auto result_pair = loadFileWithStats(filepath);
    if (!result_pair)
        return std::unexpected(result_pair.error());
    
    // After successful load, move the entries into `analyzer_.entries_`
    {
        std::unique_lock lock(*analyzer_.rw_mutex_ptr_);
        analyzer_.entries_.insert(analyzer_.entries_.end(), std::make_move_iterator(result_pair->second.begin()), std::make_move_iterator(result_pair->second.end()));
        analyzer_.cached_stats_.reset();
    }
    return {};
}

std::future<LoadResult> LogLoader::loadFileAsync(
    std::filesystem::path filepath,
    LogAnalysis::ProgressCallback progress)
{
    return std::async(std::launch::async, [this, filepath, progress]() -> LoadResult
                      {
        auto result_pair = this->loadFileWithStats(filepath, progress);
        if (result_pair) {
            // Aggregate entries from this single file load into the main analyzer_.entries_ vector
            std::unique_lock lock(*analyzer_.rw_mutex_ptr_);
            analyzer_.entries_.insert(analyzer_.entries_.end(), std::make_move_iterator(result_pair->second.begin()), std::make_move_iterator(result_pair->second.end()));
            analyzer_.cached_stats_.reset();
            return result_pair->first; // Return only the LoadResult part
        }
        throw std::runtime_error(result_pair.error()); });
}

std::future<LoadResult> LogLoader::loadParallel(std::filesystem::path path, LogAnalysis::ParallelConfig config)
{
    return std::async(std::launch::async, [this, path, config]() -> LoadResult
                      {
        {
            std::unique_lock lock(*analyzer_.rw_mutex_ptr_);
            analyzer_.entries_.clear();
            analyzer_.cached_stats_.reset();
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
                    if (analyzer_.entry_start_regex_) is_new = std::regex_search(line, *analyzer_.entry_start_regex_);
                    
                    if (is_new && !buffer.empty()) {
                        ::LogEntry entry = analyzer_.parseLogLine(buffer);
                        if (!entry.timestamp.empty() || !entry.message.empty()) {
                            analyzer_.applyEnrichers(entry);
                            analyzer_.applyAnonymizers(entry);
                            chunk_entries.push_back(std::move(entry));
                        } else total_errors.fetch_add(1);
                        buffer = line;
                    } else {
                        if (!buffer.empty()) buffer += "\n";
                        buffer += line;
                    }
                }
                if (!buffer.empty()) {
                    ::LogEntry entry = analyzer_.parseLogLine(buffer);
                    if (!entry.timestamp.empty() || !entry.message.empty()) {
                        analyzer_.applyEnrichers(entry);
                        analyzer_.applyAnonymizers(entry);
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
            std::unique_lock lock(*analyzer_.rw_mutex_ptr_);
            analyzer_.entries_.insert(analyzer_.entries_.end(), std::make_move_iterator(chunk_entries.begin()), std::make_move_iterator(chunk_entries.end()));
        }
        res.error_count = total_errors.load();
        if (config.progress) config.progress({total_size, total_size, total_lines});
        return res;
    });
}

std::expected<LoadResult, std::string> LogLoader::loadLogSources(const std::vector<LogAnalysis::LogSource>& sources, bool recursive_global_flag, LogAnalysis::ProgressCallback progress) {
    LogAnalysis::LoadResult total_result = {0, 0};
    std::vector<::LogEntry> collected_entries; // Temporarily store entries from all sources

    for (const auto& source : sources) {
        std::vector<std::string> file_paths_to_load;
        if (source.getType() == LogAnalysis::LogSource::SourceType::DIRECTORY) {
            std::filesystem::path dir_path(source.getPath());
            if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
                return std::unexpected("Directory not found or not a directory: " + source.getPath());
            }

            if (recursive_global_flag) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
                    if (std::filesystem::is_regular_file(entry.status())) {
                        file_paths_to_load.push_back(entry.path().string());
                    }
                }
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                    if (std::filesystem::is_regular_file(entry.status())) {
                        file_paths_to_load.push_back(entry.path().string());
                    }
                }
            }
        } else if (source.getType() == LogAnalysis::LogSource::SourceType::FILE) {
            file_paths_to_load.push_back(source.getPath());
        } else if (source.getType() == LogAnalysis::LogSource::SourceType::URL || source.getType() == LogAnalysis::LogSource::SourceType::S3) {
            std::cerr << "Warning: Remote source type (" << (source.getType() == LogAnalysis::LogSource::SourceType::URL ? "URL" : "S3") << ") not fully implemented. Skipping: " << source.getUrl() << std::endl;
            total_result.error_count++;
            continue;
        } else if (source.getType() == LogAnalysis::LogSource::SourceType::STD_IN) {
             // STDIN is handled via a dedicated stream in main.cpp, not file_paths_to_load.
             // For now, if passed here, we'll treat it as a file path if available, or skip.
             // This method primarily focuses on file-based sources.
             if (source.getPath().empty()) { // Path is empty for pure stdin
                 std::cerr << "Warning: STDIN source type needs dedicated stream handling. Skipping for now." << std::endl;
                 total_result.error_count++;
                 continue;
             } else {
                file_paths_to_load.push_back(source.getPath()); // Fallback if STDIN has a path (e.g., /dev/stdin)
             }
        }

        for (const auto& filepath_str : file_paths_to_load) {
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

    // Replace the analyzer's analyzer_.entries_ with the collected ones
    {
        std::unique_lock lock(*analyzer_.rw_mutex_ptr_);
        analyzer_.entries_ = std::move(collected_entries);
        analyzer_.cached_stats_.reset();
    }
    return total_result;
}

bool LogLoader::loadLogFile(const std::string &filepath) { return loadFile(std::filesystem::path(filepath)).has_value(); }
bool LogLoader::loadLogFile(const std::filesystem::path &filepath)
{
    auto result = loadFile(filepath);
    if (!result.has_value())
    {
        std::cerr << "Error: " << result.error() << std::endl;
        return false;
    }
    return true;
}

std::generator<::LogEntry> LogLoader::streamEntries(std::filesystem::path filepath)
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
        if (analyzer_.entry_start_regex_)
            is_new = std::regex_search(line, *analyzer_.entry_start_regex_);

        if (is_new && !buffer.empty())
        {
            ::LogEntry entry = analyzer_.parseLogLine(buffer, line_number);
            analyzer_.applyEnrichers(entry);
            analyzer_.applyAnonymizers(entry);
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
        ::LogEntry entry = analyzer_.parseLogLine(buffer, line_number);
        analyzer_.applyEnrichers(entry);
        analyzer_.applyAnonymizers(entry);
        co_yield entry;
    }
}

std::generator<::LogEntry> LogLoader::streamFilteredEntries(std::filesystem::path filepath, FilterOptions options)
{
    auto pred = options.toPredicate();
    for (const auto &entry : streamEntries(filepath))
    {
        if (pred->test(entry))
            co_yield entry;
    }
}

std::generator<::LogEntry> LogLoader::streamFilteredEntries(std::filesystem::path filepath, const LogAnalysis::LogPredicate &predicate)
{
    for (const auto &entry : streamEntries(filepath))
    {
        if (predicate.test(entry))
            co_yield entry;
    }
}

std::future<void> LogLoader::tailFile(
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
                    if (analyzer_.entry_start_regex_) is_new = std::regex_search(line, *analyzer_.entry_start_regex_);

                    if (is_new && !entry_buffer.empty()) {
                        ::LogEntry entry = analyzer_.parseLogLine(entry_buffer, line_count);
                        analyzer_.applyEnrichers(entry);
                        analyzer_.applyAnonymizers(entry);
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
            ::LogEntry entry = analyzer_.parseLogLine(entry_buffer, line_count);
            analyzer_.applyEnrichers(entry);
            analyzer_.applyAnonymizers(entry);
            entry_callback(std::move(entry));
        } });
}

void LogLoader::tailFileStream(
    const LogSource& source,
    const FilterOptions& filter,
    const RetrievalOptions& retrieval,
    LogExporter& exporter,
    std::function<void(const ParseError&)> error_callback)
{
    if (source.getType() != LogSource::SourceType::FILE) {
        if (error_callback) error_callback({0, "", "Tail command requires a file source."});
        return;
    }

    std::filesystem::path filepath(source.getPath());
    std::ifstream file;
    std::string entry_buffer;
    size_t line_count = 0;
    long long last_pos = 0;
    std::optional<std::filesystem::file_time_type> last_write_time;
    std::optional<std::filesystem::file_status> last_status;

    auto open_file = [&]() {
        file.close(); // Close existing stream if any
        file.open(filepath);
        if (!file.is_open()) {
            if (error_callback) error_callback({0, "", "Could not open file for tailing: " + filepath.string()});
            return false;
        }
        file.seekg(0, std::ios::end);
        last_pos = file.tellg();
        last_write_time = std::filesystem::last_write_time(filepath);
        last_status = std::filesystem::status(filepath);
        entry_buffer.clear(); // Clear buffer on re-open
        return true;
    };

    if (!open_file()) {
        return;
    }

    auto predicate = filter.toPredicate();
    std::optional<std::regex> tail_grep_re;
    if (!retrieval.tail_grep_regex.empty()) {
        try {
            tail_grep_re.emplace(retrieval.tail_grep_regex);
        } catch (const std::regex_error& e) {
            if (error_callback) error_callback({0, retrieval.tail_grep_regex, "Invalid tail-grep regex: " + std::string(e.what())});
            return;
        }
    }
    
    // Initial read for --lines N functionality (read last N lines and exit)
    if (retrieval.tail_count > 0 && !retrieval.follow) {
        file.seekg(0, std::ios::end);
        long long current_pos = file.tellg();
        
        std::vector<std::string> last_n_lines;
        std::string current_line_read;

        while(current_pos > 0 && last_n_lines.size() <= retrieval.tail_count) {
            current_pos--;
            file.seekg(current_pos);
            char c;
            file.get(c);
            if (c == '\n') {
                last_n_lines.push_back(current_line_read);
                current_line_read.clear();
            } else {
                current_line_read.insert(0, 1, c);
            }
        }
        if (!current_line_read.empty()) { // Add the first line if it wasn't terminated by newline
            last_n_lines.push_back(current_line_read);
        }
        std::reverse(last_n_lines.begin(), last_n_lines.end()); // Reverse to get in correct order

        // Process only up to tail_count lines and apply filters
        std::vector<LogEntry> filtered_initial_entries;
        for (const auto& l : last_n_lines) {
            if (filtered_initial_entries.size() >= retrieval.tail_count) break;

            if (tail_grep_re && !std::regex_search(l, *tail_grep_re)) {
                continue; // Skip if it doesn't match --tail-grep-regex
            }

            // Simulate parsing with entry_start_regex logic
            bool is_new = true;
            if (analyzer_.entry_start_regex_) is_new = std::regex_search(l, *analyzer_.entry_start_regex_);

            if (is_new && !entry_buffer.empty()) {
                LogEntry entry = analyzer_.parseLogLine(entry_buffer, ++line_count);
                analyzer_.applyEnrichers(entry);
                analyzer_.applyAnonymizers(entry);
                if (predicate->test(entry)) {
                    filtered_initial_entries.push_back(std::move(entry));
                }
                entry_buffer = l;
            } else {
                if (!entry_buffer.empty()) entry_buffer += "\n";
                entry_buffer += l;
            }
        }
        if (!entry_buffer.empty()) { // Process any remaining buffer
            LogEntry entry = analyzer_.parseLogLine(entry_buffer, ++line_count);
            analyzer_.applyEnrichers(entry);
            analyzer_.applyAnonymizers(entry);
            if (predicate->test(entry)) {
                filtered_initial_entries.push_back(std::move(entry));
            }
        }
        // Export collected entries for --lines N
        exporter.exportEntries({filtered_initial_entries.data(), filtered_initial_entries.size()}, retrieval.fields_to_export, retrieval.highlight_regex);
        return; // Exit after printing N lines
    }

    // Continuous tailing (follow mode)
    while (!stop_tailing_ptr_->load()) { // Loop until stop_tailing_ptr_ is set to true
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Polling interval

        file.clear(); // Clear any error flags

        std::filesystem::file_status current_status;
        std::error_code ec;
        current_status = std::filesystem::status(filepath, ec);

        if (ec) {
            if (error_callback) error_callback({0, "", "Tailing: Failed to get file status: " + ec.message() + ". Retrying..."});
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Longer pause on error
            continue;
        }

        if (!std::filesystem::exists(filepath)) {
            if (error_callback) error_callback({0, "", "Tailing: File not found: " + filepath.string() + ". Retrying..."});
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Longer pause
            file.close(); // Close stream as file is gone
            // Wait for file to reappear, then re-open
            while (!stop_tailing_ptr_->load() && !std::filesystem::exists(filepath)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (stop_tailing_ptr_->load()) break; // Stop if requested while waiting
            if (!open_file()) { // Re-open if file reappeared
                std::this_thread::sleep_for(std::chrono::seconds(1)); // Pause before retry
                continue;
            }
            last_pos = 0; // Reset last_pos after file disappears and reappears
            continue;
        }

        // Handle log rotation by name (-F)
        if (retrieval.follow_by_name) {
            if (last_status && std::filesystem::equivalent(filepath, filepath, ec)) {
                // If the file is still the same, check write time for changes
                 auto current_write_time = std::filesystem::last_write_time(filepath, ec);
                 if (!ec && last_write_time && current_write_time < *last_write_time) {
                    // File was truncated/replaced and has an older write time, re-open
                    if (error_callback) error_callback({0, "", "Tailing: File rotated/truncated (older write time). Re-opening."});
                    if (!open_file()) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                        continue;
                    }
                    continue;
                 }
            } else {
                 // File is not equivalent (likely rotated and new file created with same name)
                 if (error_callback) error_callback({0, "", "Tailing: File rotated (different inode). Re-opening."});
                 if (!open_file()) {
                     std::this_thread::sleep_for(std::chrono::seconds(1));
                     continue;
                 }
                 continue;
            }
        }
        
        auto current_size = std::filesystem::file_size(filepath, ec);
        if (ec) {
             if (error_callback) error_callback({0, "", "Tailing: Failed to get file size: " + ec.message()});
             continue;
        }

        if (current_size < static_cast<size_t>(last_pos)) {
            // File was truncated or reset (e.g., log rotation).
            // Re-read from beginning.
            if (error_callback) error_callback({0, "", "Tailing: File truncated or rotated. Re-reading from beginning."});
            file.seekg(0, std::ios::beg);
            last_pos = 0;
            entry_buffer.clear(); // Clear buffer to avoid parsing old partial entries
        } else if (current_size > static_cast<size_t>(last_pos)) {
            // New content available
            file.seekg(last_pos);
            std::string current_line;
            std::vector<LogEntry> entries_to_export;

            while (std::getline(file, current_line)) {
                if (stop_tailing_ptr_->load()) break; // Check stop signal inside inner loop

                // Apply tail-grep-regex
                if (tail_grep_re && !std::regex_search(current_line, *tail_grep_re)) {
                    continue; // Skip this line if it doesn't match the grep regex
                }

                // Simulate parsing with entry_start_regex logic
                bool is_new = true;
                if (analyzer_.entry_start_regex_) is_new = std::regex_search(current_line, *analyzer_.entry_start_regex_);

                if (is_new && !entry_buffer.empty()) {
                    LogEntry entry = analyzer_.parseLogLine(entry_buffer, ++line_count);
                    analyzer_.applyEnrichers(entry);
                    analyzer_.applyAnonymizers(entry);
                    if (predicate->test(entry)) {
                        entries_to_export.push_back(std::move(entry));
                    }
                    entry_buffer = current_line;
                } else {
                    if (!entry_buffer.empty()) entry_buffer += "\n";
                    entry_buffer += current_line;
                }
            }
            if (stop_tailing_ptr_->load()) break; // Check stop signal after inner loop

            // Export any accumulated entries
            if (!entries_to_export.empty()) {
                exporter.exportEntries({entries_to_export.data(), entries_to_export.size()}, retrieval.fields_to_export, retrieval.highlight_regex);
            }
            last_pos = file.tellg();
        }
    }
    // Process any remaining buffer upon stopping
    if (!entry_buffer.empty()) {
        LogEntry entry = analyzer_.parseLogLine(entry_buffer, ++line_count);
        analyzer_.applyEnrichers(entry);
        analyzer_.applyAnonymizers(entry);
        if (predicate->test(entry)) {
            // Need to create a single-element vector for exportEntries
            std::vector<LogEntry> final_entry_vec;
            final_entry_vec.push_back(std::move(entry));
            exporter.exportEntries({final_entry_vec.data(), final_entry_vec.size()}, retrieval.fields_to_export, retrieval.highlight_regex);
        }
    }
}

std::future<void> LogLoader::startTailing(const std::filesystem::path& path, std::chrono::milliseconds interval) {
    // If a tailing thread is already running, stop it first
    if (tailing_thread_.joinable()) {
        stopTailing();
    }

    current_tail_path_ = path;
    tail_interval_ = interval;
    *stop_tailing_ptr_ = false;

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

        while (!*stop_tailing_ptr_) {
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
                    if (analyzer_.entry_start_regex_)
                        is_new = std::regex_search(line, *analyzer_.entry_start_regex_);

                    if (is_new && !entry_buffer.empty()) {
                        ::LogEntry entry = analyzer_.parseLogLine(entry_buffer, line_count);
                        analyzer_.applyEnrichers(entry);
                        analyzer_.applyAnonymizers(entry);
                        analyzer_.addEntry(std::move(entry)); // Add to analyzer's entries
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
            ::LogEntry entry = analyzer_.parseLogLine(entry_buffer, line_count);
            analyzer_.applyEnrichers(entry);
            analyzer_.applyAnonymizers(entry);
            analyzer_.addEntry(std::move(entry));
        }
        promise->set_value(); // Signal completion
    });

    return future;
}

void LogLoader::stopTailing() {
    *stop_tailing_ptr_ = true;
    if (tailing_thread_.joinable()) {
        tailing_thread_.join();
    }
}

} // namespace LogAnalysis
