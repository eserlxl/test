cmake_minimum_required(VERSION 3.15...3.27)
project(gemini-cli
    VERSION 1.0.0
    DESCRIPTION "A command-line tool for analyzing log files"
    LANGUAGES CXX)

set(GEMINI_CLI_CXX_STANDARD 23 CACHE STRING "C++ standard to use for building (e.g., 20, 23, 26)")
set(CMAKE_CXX_STANDARD ${GEMINI_CLI_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type if not specified
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No CMAKE_BUILD_TYPE set, defaulting to RelWithDebInfo")
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel" FORCE)
    # Set property for VS IDE
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Common compiler options
# Note: Warnings are now conditionally applied based on build type or global option
# Debug-specific flags
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g) # Add debug symbols
    add_compile_options(-O0) # No optimization
    # Perhaps more aggressive warnings for Debug
elseif (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -DNDEBUG) # Full optimization, no asserts
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-O2 -g -DNDEBUG) # Optimized with debug info
elseif (CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
    add_compile_options(-Os -DNDEBUG) # Optimize for size
endif()

# Enable compile commands for IDE integration (e.g., VS Code, CLion)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options for building
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_DEVELOPER_WARNINGS "Enable strict developer warning flags (-Wall -Wextra -Wpedantic)" ON)
option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_CODE_COVERAGE "Enable code coverage instrumentation (gcov/lcov)" OFF)
option(BUILD_SHARED_LIBS "Build gemini-core as a shared library instead of static" OFF)
option(GEMINI_CLI_FETCH_GTEST "Use FetchContent to download and build GTest if not found" OFF)

# --- Core Library Component ---
# Defines the reusable logic as a library
if (BUILD_SHARED_LIBS)
    add_library(gemini-core SHARED
        src/LogAnalyzer.cpp
        src/LogEntry.cpp
    )
else()
    add_library(gemini-core STATIC
        src/LogAnalyzer.cpp
        src/LogEntry.cpp
    )
endif()

target_include_directories(gemini-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/gemini-cli>
)

# Conditional warning flags
if (ENABLE_DEVELOPER_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gemini-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Sanitizers
if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Enabling AddressSanitizer and UndefinedBehaviorSanitizer")
    target_compile_options(gemini-core PRIVATE -fsanitize=address,undefined)
    target_link_options(gemini-core PRIVATE -fsanitize=address,undefined)
endif()

# Code Coverage
if (ENABLE_CODE_COVERAGE AND BUILD_TESTS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Enabling code coverage instrumentation")
    target_compile_options(gemini-core PRIVATE --coverage)
    target_link_options(gemini-core PRIVATE --coverage)
endif()

# --- Main Executable ---
add_executable(gemini-cli src/main.cpp)
target_link_libraries(gemini-cli PRIVATE gemini-core CLI11::CLI11)

# Apply options to the main executable as well
if (ENABLE_DEVELOPER_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gemini-cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(gemini-cli PRIVATE -fsanitize=address,undefined)
    target_link_options(gemini-cli PRIVATE -fsanitize=address,undefined)
endif()

# --- Testing Setup ---

if (BUILD_TESTS)
    find_package(GTest CONFIG QUIET) # Try to find system GTest first

    if (NOT GTest_FOUND AND GEMINI_CLI_FETCH_GTEST)
        message(STATUS "GTest not found, attempting to fetch with FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG        release-1.12.1 # Pin to a specific version
        )
        FetchContent_Populate(googletest)
        add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
        set(GTest_FOUND TRUE) # Manually set GTest_FOUND after FetchContent
    elseif (NOT GTest_FOUND AND NOT GEMINI_CLI_FETCH_GTEST)
        message(FATAL_ERROR "GTest not found. Please install GTest or enable GEMINI_CLI_FETCH_GTEST option.")
    endif()

    if (GTest_FOUND)
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
    endif()
endif()

# FetchContent for CLI11
include(FetchContent)
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2 # Pin to a specific version
)
FetchContent_MakeAvailable(CLI11)

# --- Installation ---
# Install the gemini-cli executable
install(TARGETS gemini-cli
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# Install the gemini-core library
install(TARGETS gemini-core
    EXPORT gemini-cli-targets # Export for find_package later
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" # For static library
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" # For shared library
    FRAMEWORK DESTINATION "${CMAKE_INSTALL_LIBDIR}" # For macOS frameworks
)

# Install public headers
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/gemini-cli"
    FILES_MATCHING PATTERN "*.h"
)

# Generate a package configuration file for other projects to find gemini-cli
# This will create gemini-cli-config.cmake and gemini-cli-targets.cmake
install(EXPORT gemini-cli-targets
    FILE gemini-cli-targets.cmake
    NAMESPACE gemini-cli::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/gemini-cli"
)
