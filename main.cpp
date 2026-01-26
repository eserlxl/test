#include <iostream>
#include <string>
#include "LogAnalyzer.h"

void printUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <log_file>" << std::endl;
    std::cout << "Analyzes a log file and provides statistics." << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        printUsage(argv[0]);
        return 1;
    }
    
    std::string log_file = argv[1];
    
    LogAnalyzer analyzer;
    
    if (!analyzer.loadLogFile(log_file)) {
        return 1;
    }
    
    analyzer.analyze();
    analyzer.printStatistics();
    
    return 0;
}
