cmake_minimum_required(VERSION 3.10)
project(LogAnalyzer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(log_analyzer
    main.cpp
    LogEntry.cpp
    LogAnalyzer.cpp
)
