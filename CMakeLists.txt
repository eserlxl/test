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

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ENABLE_CODE_COVERAGE "Enable code coverage instrumentation (gcov/lcov)" OFF)
option(BUILD_SHARED_LIBS "Build gemini-core as a shared library instead of static" OFF)
option(GEMINI_CLI_FETCH_GTEST "Use FetchContent to download and build GTest if not found" OFF)
option(GEMINI_CLI_FETCH_CLI11 "Use FetchContent to download and build CLI11 if not found" ON)
option(BUILD_BENCHMARKS "Build benchmarking executables using Google Benchmark" OFF)
option(GEMINI_CLI_FETCH_BENCHMARK "Use FetchContent to download and build Google Benchmark if not found" ON)

set(GEMINI_CLI_EXTRA_COMPILE_OPTIONS "" CACHE STRING "Additional compile options to be applied to all targets")
set(GEMINI_CLI_EXTRA_LINK_OPTIONS "" CACHE STRING "Additional link options to be applied to all linkable targets")
set(GEMINI_CLI_CXX26_EXPERIMENTAL_FLAGS "" CACHE STRING "Compiler flags specifically for enabling experimental C++26 features (if CXX_STANDARD >= 26)")

# Determine warning flags based on GEMINI_CLI_WARNING_LEVEL
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(_default_warnings -Wall -Wextra -Wpedantic)
    set(_strict_warnings ${_default_warnings} -Werror -Wconversion -Wshadow -Wformat=2 -Wnull-dereference -Wdouble-promotion -Wold-style-cast -Wsuggest-override -Wno-unused-parameter)

    set(_current_warnings "")
    # Ensure GEMINI_CLI_WARNING_LEVEL has a default if not explicitly set
    if (NOT DEFINED GEMINI_CLI_WARNING_LEVEL)
        set(GEMINI_CLI_WARNING_LEVEL "DEFAULT" CACHE STRING "Controls the level of compiler warnings. Options: NONE, DEFAULT (-Wall -Wextra -Wpedantic), STRICT (-Werror, etc.), CUSTOM" FORCE)
        set_property(CACHE GEMINI_CLI_WARNING_LEVEL PROPERTY STRINGS "NONE" "DEFAULT" "STRICT" "CUSTOM")
    endif()

    if (GEMINI_CLI_WARNING_LEVEL STREQUAL "DEFAULT")
        set(_current_warnings "${_default_warnings}")
    elseif (GEMINI_CLI_WARNING_LEVEL STREQUAL "STRICT")
        set(_current_warnings "${_strict_warnings}")
    elseif (GEMINI_CLI_WARNING_LEVEL STREQUAL "CUSTOM")
        # Start with default warnings, then add/remove based on user input
        set(_custom_base_warnings "${_default_warnings}") # Or empty, depending on desired custom base behavior
        list(APPEND _custom_base_warnings ${GEMINI_CLI_ADDITIONAL_WARNINGS})
        
        set(_final_custom_warnings "")
        foreach (_warning IN LISTS _custom_base_warnings)
            list(FIND GEMINI_CLI_REMOVED_WARNINGS "${_warning}" _idx)
            if (_idx EQUAL -1) # Warning not in removed list
                list(APPEND _final_custom_warnings "${_warning}")
            endif()
        endforeach()
        set(_current_warnings "${_final_custom_warnings}")
    elseif (GEMINI_CLI_WARNING_LEVEL STREQUAL "NONE")
        set(_current_warnings "")
    else()
        message(WARNING "Unknown GEMINI_CLI_WARNING_LEVEL: ${GEMINI_CLI_WARNING_LEVEL}. No custom warnings will be applied.")
    endif()

    if (NOT "${_current_warnings}" STREQUAL "")
        # Temporarily remove problematic std::generator warning for GCC/Clang C++23
        # This is often a toolchain bug (GCC 13/14 std::generator with mismatched new/delete)
        list(REMOVE_ITEM _current_warnings "-Wmismatched-new-delete")
        message(STATUS "Applying compiler warnings: ${_current_warnings}")
        add_compile_options(${_current_warnings})
    endif()
endif()

# Apply experimental C++26 flags if CXX_STANDARD is 26 or greater
if (GEMINI_CLI_CXX_STANDARD GREATER_EQUAL 26)
    if (NOT "${GEMINI_CLI_CXX26_EXPERIMENTAL_FLAGS}" STREQUAL "")
        message(STATUS "Applying experimental C++26 flags: ${GEMINI_CLI_CXX26_EXPERIMENTAL_FLAGS}")
        add_compile_options(${GEMINI_CLI_CXX26_EXPERIMENTAL_FLAGS})
    endif()
endif()



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

if (NOT "${GEMINI_CLI_EXTRA_COMPILE_OPTIONS}" STREQUAL "")
    target_compile_options(gemini-core PRIVATE ${GEMINI_CLI_EXTRA_COMPILE_OPTIONS})
endif()
if (NOT "${GEMINI_CLI_EXTRA_LINK_OPTIONS}" STREQUAL "")
    target_link_options(gemini-core PRIVATE ${GEMINI_CLI_EXTRA_LINK_OPTIONS})
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

if (NOT "${GEMINI_CLI_EXTRA_COMPILE_OPTIONS}" STREQUAL "")
    target_compile_options(gemini-cli PRIVATE ${GEMINI_CLI_EXTRA_COMPILE_OPTIONS})
endif()
if (NOT "${GEMINI_CLI_EXTRA_LINK_OPTIONS}" STREQUAL "")
    target_link_options(gemini-cli PRIVATE ${GEMINI_CLI_EXTRA_LINK_OPTIONS})
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
        if (NOT "${GEMINI_CLI_EXTRA_COMPILE_OPTIONS}" STREQUAL "")
            target_compile_options(LogAnalyzerTest PRIVATE ${GEMINI_CLI_EXTRA_COMPILE_OPTIONS})
        endif()
        if (NOT "${GEMINI_CLI_EXTRA_LINK_OPTIONS}" STREQUAL "")
            target_link_options(LogAnalyzerTest PRIVATE ${GEMINI_CLI_EXTRA_LINK_OPTIONS})
        endif()
        add_test(NAME LogAnalyzerTest COMMAND LogAnalyzerTest)

        # LogEntry Tests
        add_executable(LogEntryTest test/LogEntryTest.cpp)
        target_link_libraries(LogEntryTest PRIVATE
            GTest::gtest_main
            gemini-core
        )
        if (NOT "${GEMINI_CLI_EXTRA_COMPILE_OPTIONS}" STREQUAL "")
            target_compile_options(LogEntryTest PRIVATE ${GEMINI_CLI_EXTRA_COMPILE_OPTIONS})
        endif()
        if (NOT "${GEMINI_CLI_EXTRA_LINK_OPTIONS}" STREQUAL "")
            target_link_options(LogEntryTest PRIVATE ${GEMINI_CLI_EXTRA_LINK_OPTIONS})
        endif()
        add_test(NAME LogEntryTest COMMAND LogEntryTest)
    endif()
endif()

# --- Benchmarking Setup ---
if (BUILD_BENCHMARKS)
    message(STATUS "Benchmarking enabled. Looking for benchmarks in bench/")

    find_package(Benchmark CONFIG QUIET) # Try to find system Google Benchmark first

    if (NOT Benchmark_FOUND AND GEMINI_CLI_FETCH_BENCHMARK)
        message(STATUS "Google Benchmark not found, attempting to fetch with FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            google_benchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG        v1.8.3 # Pin to a specific version
        )
        FetchContent_Populate(google_benchmark)
        add_subdirectory(${google_benchmark_SOURCE_DIR} ${google_benchmark_BINARY_DIR})
        set(Benchmark_FOUND TRUE) # Manually set Benchmark_FOUND after FetchContent
    elseif (NOT Benchmark_FOUND AND NOT GEMINI_CLI_FETCH_BENCHMARK)
        message(FATAL_ERROR "Google Benchmark not found. Please install Google Benchmark or enable GEMINI_CLI_FETCH_BENCHMARK option.")
    endif()

    if (Benchmark_FOUND)
        # Add benchmark subdirectory
        # Assuming benchmark files are directly in 'bench/' and end with .cpp
        file(GLOB_RECURSE BENCHMARK_SOURCES "bench/*.cpp")
        if (BENCHMARK_SOURCES)
            foreach(bench_src ${BENCHMARK_SOURCES})
                get_filename_component(bench_name ${bench_src} NAME_WE)
                add_executable(${bench_name} ${bench_src})
                target_link_libraries(${bench_name} PRIVATE
                    Benchmark::benchmark_main
                    gemini-core
                )
                # Apply extra options to benchmarks
                if (NOT "${GEMINI_CLI_EXTRA_COMPILE_OPTIONS}" STREQUAL "")
                    target_compile_options(${bench_name} PRIVATE ${GEMINI_CLI_EXTRA_COMPILE_OPTIONS})
                endif()
                if (NOT "${GEMINI_CLI_EXTRA_LINK_OPTIONS}" STREQUAL "")
                    target_link_options(${bench_name} PRIVATE ${GEMINI_CLI_EXTRA_LINK_OPTIONS})
                endif()
                add_test(NAME ${bench_name} COMMAND ${bench_name}) # Add as a test for ctest integration
            endforeach()
        else()
            message(STATUS "No benchmark source files found in bench/.")
        endif()
    endif()
endif()

# FetchContent for CLI11 or find_package
if (GEMINI_CLI_FETCH_CLI11)
    message(STATUS "CLI11 not found, attempting to fetch with FetchContent...")
    include(FetchContent)
    FetchContent_Declare(
        CLI11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG        v2.4.2 # Pin to a specific version
    )
    FetchContent_MakeAvailable(CLI11)
else()
    find_package(CLI11 CONFIG QUIET)
    if (NOT CLI11_FOUND)
        message(FATAL_ERROR "CLI11 not found. Please install CLI11 or enable GEMINI_CLI_FETCH_CLI11 option.")
    endif()
endif()

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
