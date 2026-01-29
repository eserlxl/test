cmake_minimum_required(VERSION 3.15...3.27)
project(gemini-cli LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable compile commands for IDE integration (e.g., VS Code, CLion)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Core Library Component ---
# Defines the reusable logic as a static library
add_library(gemini-core STATIC
    src/LogAnalyzer.cpp
    src/LogEntry.cpp
)

target_include_directories(gemini-core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# Apply common warning flags
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gemini-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Main Executable ---
add_executable(gemini-cli src/main.cpp)
target_link_libraries(gemini-cli PRIVATE gemini-core)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gemini-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Testing Setup ---
find_package(GTest CONFIG REQUIRED)
enable_testing()

# LogAnalyzer Tests
add_executable(LogAnalyzerTest test/LogAnalyzerTest.cpp)
target_link_libraries(LogAnalyzerTest PRIVATE
    GTest::gtest_main
    gemini-core
)
add_test(NAME LogAnalyzerTest COMMAND LogAnalyzerTest)

# LogEntry Tests
add_executable(LogEntryTest test/LogEntryTest.cpp)
target_link_libraries(LogEntryTest PRIVATE
    GTest::gtest_main
    gemini-core
)
add_test(NAME LogEntryTest COMMAND LogEntryTest)
