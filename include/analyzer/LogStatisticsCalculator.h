#ifndef LOG_STATISTICS_CALCULATOR_H
#define LOG_STATISTICS_CALCULATOR_H

#include <analyzer/AnalysisConfig.h>
#include <vector>
#include <model/LogEntry.h>

namespace LogAnalysis {

class LogStatisticsCalculator {
public:
    LogStatisticsCalculator(const AnalysisConfig& config) : config_(config) {}
    LogStatistics calculate(const std::vector<LogEntry>& entries) const {
        LogStatistics stats;
        stats.total_entries = entries.size();
        for(const auto& entry : entries) {
            stats.level_counts[entry.level]++;
            if (!stats.first_timestamp.has_value()) {
                stats.first_timestamp = entry.timestamp;
            }
            stats.last_timestamp = entry.timestamp;
        }
        return stats;
    }
private:
    AnalysisConfig config_;
};

} // namespace LogAnalysis

#endif // LOG_STATISTICS_CALCULATOR_H
