#ifndef LOG_FILTERER_H
#define LOG_FILTERER_H

#include <analyzer/Filtering.h>
#include <model/LogEntry.h>

namespace LogAnalysis {

class LogFilterer {
public:
    LogFilterer(const FilterOptions& options) : options_(options) {}
    bool match(const LogEntry& entry) const { 
        if (options_.level != LogLevel::UNKNOWN && entry.level != options_.level) {
            return false;
        }
        return true; 
    }
private:
    FilterOptions options_;
};

} // namespace LogAnalysis

#endif // LOG_FILTERER_H
