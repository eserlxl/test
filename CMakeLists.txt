cmake_minimum_required(VERSION 3.15...3.27)
project(gemini-cli
    VERSION 1.0.0
    DESCRIPTION "A command-line tool for analyzing log files"
    LANGUAGES CXX)

set(GEMINI_CLI_CXX_STANDARD 23 CACHE STRING "C++ standard to use for building (e.g., 20, 23, 26)")
set(CMAKE_CXX_STANDARD ${GEMINI_CLI_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable experimental C++ Module CMake API if modules are enabled
if (GEMINI_CLI_ENABLE_MODULES)
    set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API TRUE)
    message(STATUS "C++ Modules support enabled via CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API.")
endif()

# Vcpkg Integration
if (GEMINI_CLI_USE_VCPKG)
    message(STATUS "Vcpkg integration enabled.")
    # CMAKE_TOOLCHAIN_FILE must be set when CMake is invoked.
    # We can check if it's already set and if it points to vcpkg.
    if (NOT CMAKE_TOOLCHAIN_FILE)
        message(WARNING "GEMINI_CLI_USE_VCPKG is ON, but CMAKE_TOOLCHAIN_FILE is not set. "
                        "Please configure CMake with -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake")
    else()
        if (CMAKE_TOOLCHAIN_FILE MATCHES ".*vcpkg.cmake")
            message(STATUS "Vcpkg toolchain file found: ${CMAKE_TOOLCHAIN_FILE}")
            if (NOT "${GEMINI_CLI_VCPKG_TRIPLET}" STREQUAL "")
                set(VCPKG_TARGET_TRIPLET "${GEMINI_CLI_VCPKG_TRIPLET}")
                message(STATUS "Vcpkg target triplet set to: ${VCPKG_TARGET_TRIPLET}")
            endif()
        else()
            message(WARNING "GEMINI_CLI_USE_VCPKG is ON, but CMAKE_TOOLCHAIN_FILE points to a non-vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
        endif()
    endif()
endif()

# Clang-Format Integration
if (GEMINI_CLI_ENABLE_CLANG_FORMAT)
    find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format-15 clang-format-14 clang-format-13) # Add more versions as needed
    if (CLANG_FORMAT_EXECUTABLE)
        message(STATUS "clang-format found: ${CLANG_FORMAT_EXECUTABLE}")
        
        # Define a custom target for applying code formatting
        add_custom_target(clangFormat
            COMMAND ${CLANG_FORMAT_EXECUTABLE} -i -style=file ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h ${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp
            COMMENT "Running clang-format on C++ source and header files"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
        message(STATUS "Custom target 'clangFormat' added to apply clang-format.")
    else()
        message(WARNING "clang-format not found. Please install it or disable GEMINI_CLI_ENABLE_CLANG_FORMAT.")
    endif()
endif()

# Clang-Tidy Integration
if (GEMINI_CLI_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy-15 clang-tidy-14 clang-tidy-13) # Add more versions as needed
    if (CLANG_TIDY_EXECUTABLE)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_EXECUTABLE}")
        set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXECUTABLE} -p=${CMAKE_BINARY_DIR})
        message(STATUS "clang-tidy enabled via CMAKE_CXX_CLANG_TIDY.")
    else()
        message(WARNING "clang-tidy not found. Please install it or disable GEMINI_CLI_ENABLE_CLANG_TIDY.")
    endif()
endif()

# Conan Integration
if (GEMINI_CLI_USE_CONAN)
    message(STATUS "Conan integration enabled.")
    find_program(CONAN_EXECUTABLE NAMES conan)
    if (NOT CONAN_EXECUTABLE)
        message(FATAL_ERROR "Conan executable not found. Please install Conan or disable GEMINI_CLI_USE_CONAN.")
    endif()

    # Try to find conan.cmake from Conan installation
    # On some systems, it might be in a different path.
    # A more robust way is to use FetchContent to get conan.cmake,
    # but for simplicity, let's assume it's findable or already in CMAKE_MODULE_PATH.
    find_file(CONAN_CMAKE_TOOLCHAIN_FILE conan.cmake
              PATHS ${CONAN_CMAKE_PATH}
              ENV CONAN_CMAKE_PATH
              NO_CMAKE_FIND_ROOT_PATH)

    if (NOT CONAN_CMAKE_TOOLCHAIN_FILE)
        # Fallback to a common location or FetchContent (more involved)
        find_file(CONAN_CMAKE_TOOLCHAIN_FILE conan.cmake
                  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/cmake
                        /usr/local/share/cmake/conan
                        /opt/conan/share/cmake/conan
                  NO_CMAKE_FIND_ROOT_PATH)
    endif()

    if (CONAN_CMAKE_TOOLCHAIN_FILE)
        message(STATUS "Including Conan toolchain from: ${CONAN_CMAKE_TOOLCHAIN_FILE}")
        include(${CONAN_CMAKE_TOOLCHAIN_FILE})

        conan_cmake_run(
            REQUIRES
                # List your project's dependencies here
                # Example:
                # boost/1.76.0
                # fmt/8.0.1
            BUILD
                missing
            PROFILE
                ${GEMINI_CLI_CONAN_PROFILE}
            BASIC_SETUP
            CMAKE_TARGETS # Integrate Conan packages as CMake targets
        )
    else()
        message(FATAL_ERROR "Could not find conan.cmake toolchain file. Please ensure Conan is installed and conan.cmake is findable, or disable GEMINI_CLI_USE_CONAN.")
    endif()
endif()

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

# Iteration 22 Features: New Options and Variables
option(GEMINI_CLI_ENABLE_MODULES "Enable C++ Modules support (requires compatible compiler & CMake)" OFF)
option(GEMINI_CLI_ENABLE_PCH "Enable Precompiled Headers (PCH) for faster compilation" OFF)
option(GEMINI_CLI_ENABLE_CLANG_FORMAT "Enable clang-format check as a custom target" OFF)
option(GEMINI_CLI_ENABLE_CLANG_TIDY "Enable clang-tidy analysis during compilation" OFF)
option(GEMINI_CLI_USE_CONAN "Use Conan for dependency management" OFF)
option(GEMINI_CLI_USE_VCPKG "Use vcpkg for dependency management" OFF)
option(GEMINI_CLI_BUILD_DOCS "Build project documentation using Doxygen" OFF)
option(GEMINI_CLI_GENERATE_TEST_REPORTS "Generate CTest XML reports for tests" OFF)

set(GEMINI_CLI_MSVC_WARNING_LEVEL "W3" CACHE STRING "MSVC warning level (e.g., W3, W4, WX)")
set_property(CACHE GEMINI_CLI_MSVC_WARNING_LEVEL PROPERTY STRINGS "W0" "W1" "W2" "W3" "W4" "Wall" "WX")

set(GEMINI_CLI_CONAN_PROFILE "" CACHE STRING "Conan profile to use (e.g., default, clang12, vs2019)")
set(GEMINI_CLI_VCPKG_TRIPLET "" CACHE STRING "vcpkg triplet to use (e.g., x64-windows, x64-linux)")
set(GEMINI_CLI_PCH_HEADER "" CACHE STRING "Path to the precompiled header file (e.g., stdafx.h or PCH.h)")

set(GEMINI_CLI_EXTRA_COMPILE_OPTIONS "" CACHE STRING "Additional compile options to be applied to all targets")
set(GEMINI_CLI_EXTRA_LINK_OPTIONS "" CACHE STRING "Additional link options to be applied to all linkable targets")
set(GEMINI_CLI_CXX26_EXPERIMENTAL_FLAGS "" CACHE STRING "Compiler flags specifically for enabling experimental C++26 features (if CXX_STANDARD >= 26)")

# Define a utility function to apply global options to targets
function(gemini_cli_apply_global_options TARGET_NAME)
    # Apply extra compile options
    if (NOT "${GEMINI_CLI_EXTRA_COMPILE_OPTIONS}" STREQUAL "")
        target_compile_options(${TARGET_NAME} PRIVATE ${GEMINI_CLI_EXTRA_COMPILE_OPTIONS})
    endif()

    # Apply extra link options
    if (NOT "${GEMINI_CLI_EXTRA_LINK_OPTIONS}" STREQUAL "")
        target_link_options(${TARGET_NAME} PRIVATE ${GEMINI_CLI_EXTRA_LINK_OPTIONS})
    endif()

    # Apply code coverage instrumentation if enabled
    if (ENABLE_CODE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${TARGET_NAME} PRIVATE --coverage)
        target_link_options(${TARGET_NAME} PRIVATE --coverage)
    endif()

    # Apply C++ Modules flags if enabled
    if (GEMINI_CLI_ENABLE_MODULES)
        if (CMAKE_VERSION VERSION_LESS "3.19")
            message(WARNING "C++ Modules support requires CMake 3.19 or higher. Skipping for target ${TARGET_NAME}.")
        else()
            # General C++ Modules feature
            if (TARGET_NAME) # Ensure target exists before querying compile features
                cmake_check_compile_features(cxx_modules CXX_MODULES_SUPPORTED)
                if (CXX_MODULES_SUPPORTED)
                    target_compile_features(${TARGET_NAME} PRIVATE cxx_modules)
                    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
                        # MSVC specific flags for C++20 Modules
                        target_compile_options(${TARGET_NAME} PRIVATE /experimental:module /Zc:preprocessor)
                    elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                        # Clang often needs -fmodules but target_compile_features handles it.
                        # Explicitly adding it might sometimes be necessary depending on Clang version and setup.
                        # For now, rely on target_compile_features.
                        # target_compile_options(${TARGET_NAME} PRIVATE -fmodules) # Optional, often not needed with cxx_modules feature
                    elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
                        # GCC's C++ modules support is still evolving.
                        # CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API is the primary mechanism for CMake integration.
                        # Specific flags like -fmodules-ts or similar might be needed for older GCC versions
                        # or specific module configurations, but CMake's feature should ideally manage this.
                    endif()
                    message(STATUS "C++ Modules enabled for target ${TARGET_NAME}.")
                else()
                    message(WARNING "C++ Modules requested but not supported by the current compiler/CMake for target ${TARGET_NAME}. Skipping.")
                endif()
            else()
                 message(WARNING "Cannot apply C++ Modules to an undefined target: ${TARGET_NAME}.")
            endif()
        endif()
    endif()

    # Apply Precompiled Headers (PCH) if enabled
    if (GEMINI_CLI_ENABLE_PCH)
        if (GEMINI_CLI_PCH_HEADER)
            if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${GEMINI_CLI_PCH_HEADER}")
                target_precompile_headers(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/${GEMINI_CLI_PCH_HEADER}")
                message(STATUS "Precompiled Headers enabled for target ${TARGET_NAME} using ${GEMINI_CLI_PCH_HEADER}.")
            else()
                message(WARNING "GEMINI_CLI_PCH_HEADER '${GEMINI_CLI_PCH_HEADER}' not found at '${CMAKE_CURRENT_SOURCE_DIR}/${GEMINI_CLI_PCH_HEADER}'. PCH not applied to target ${TARGET_NAME}.")
            endif()
        else()
            message(STATUS "PCH enabled for target ${TARGET_NAME}, but GEMINI_CLI_PCH_HEADER is not specified. Skipping PCH for this target.")
        endif()
    endif()

    # Apply Clang-Tidy if enabled
    if (GEMINI_CLI_ENABLE_CLANG_TIDY)
        # CMAKE_CXX_CLANG_TIDY is a global property, but can be set per-target too.
        # For simplicity, we assume it's applied globally if enabled.
        # If per-target setup is needed, this function can be extended.
    endif()

    # Apply MSVC warnings if the compiler is MSVC
    if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        if (NOT "${GEMINI_CLI_MSVC_WARNING_LEVEL}" STREQUAL "")
            # Clear existing warning flags first for MSVC to avoid conflicts
            target_compile_options(${TARGET_NAME} PRIVATE /W0 /W1 /W2 /W3 /W4 /Wall /WX)
            target_compile_options(${TARGET_NAME} PRIVATE "/${GEMINI_CLI_MSVC_WARNING_LEVEL}")
            message(STATUS "Applying MSVC warning level /${GEMINI_CLI_MSVC_WARNING_LEVEL} to target ${TARGET_NAME}")
        endif()
    endif()

endfunction()
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
        src/ConfigManager.cpp # Add ConfigManager.cpp to the core library
    )
else()
    add_library(gemini-core STATIC
        src/analyzer/AnalysisConfig.cpp
        src/analyzer/Exporters.cpp
        src/analyzer/Filtering.cpp
        src/analyzer/LogAnalyzer.cpp
        src/analyzer/ParsingConfig.cpp
        src/model/LogEntry.cpp
        src/model/LogValue.cpp
        src/config/ConfigManager.cpp
    )
endif()

target_include_directories(gemini-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/gemini-cli>
)

target_link_libraries(gemini-core PRIVATE
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json # Or just nlohmann_json if target is not namespaced
)

gemini_cli_apply_global_options(gemini-core)

# --- Main Executable ---
add_executable(gemini-cli src/main.cpp)
target_link_libraries(gemini-cli PRIVATE
    gemini-core
    CLI11::CLI11
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json # Link to executable as well
)

gemini_cli_apply_global_options(gemini-cli)

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
        if (GEMINI_CLI_GENERATE_TEST_REPORTS)
            set(CTEST_USE_LAUNCHERS ON) # Ensures CTest wrappers are used
            set(CTEST_OUTPUT_ON_FAILURE ON) # Show output for failed tests

            # Define a custom target to generate JUnit XML test reports
            add_custom_target(generateTestReports
                COMMAND ${CMAKE_CTEST_COMMAND} --output-junit "${CMAKE_BINARY_DIR}/TestResults.xml"
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Generating CTest JUnit XML test reports"
            )
            message(STATUS "Custom target 'generateTestReports' added for CTest XML reports.")
        endif()
        # LogAnalyzer Tests
        add_executable(LogAnalyzerTest tests/LogAnalyzerTest.cpp)
        target_link_libraries(LogAnalyzerTest PRIVATE
            GTest::gtest_main
            gemini-core
        )
        gemini_cli_apply_global_options(LogAnalyzerTest)
        add_test(NAME LogAnalyzerTest COMMAND LogAnalyzerTest)

        # LogEntry Tests
        add_executable(LogEntryTest tests/LogEntryTest.cpp)
        target_link_libraries(LogEntryTest PRIVATE
            GTest::gtest_main
            gemini-core
        )
        gemini_cli_apply_global_options(LogEntryTest)
        add_test(NAME LogEntryTest COMMAND LogEntryTest)

        # LogValue Tests
        add_executable(LogValueTest tests/LogValueTest.cpp)
        target_link_libraries(LogValueTest PRIVATE
            GTest::gtest_main
            gemini-core
        )
        gemini_cli_apply_global_options(LogValueTest)
        add_test(NAME LogValueTest COMMAND LogValueTest)
    endif()
endif()

# Find yaml-cpp and nlohmann/json for configuration management
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED) # Assumes nlohmann_json provides a CMake config


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
                gemini_cli_apply_global_options(${bench_name})
                add_test(NAME ${bench_name} COMMAND ${bench_name}) # Add as a test for ctest integration
            endforeach()
        else()
            message(STATUS "No benchmark source files found in bench/.")
        endif()
    endif()
endif()

# Documentation Generation with Doxygen
if (GEMINI_CLI_BUILD_DOCS)
    find_package(Doxygen QUIET)
    if (Doxygen_FOUND)
        message(STATUS "Doxygen found, documentation generation enabled.")
        
        # This will create a 'doc' target that builds the documentation
        # by generating a temporary Doxyfile and running Doxygen.
        doxygen_add_docs(
            generateDocs  # Name of the custom target
            "${CMAKE_CURRENT_SOURCE_DIR}/include"  # Source directories to scan
            # You can add more source directories here if needed, e.g., "${CMAKE_CURRENT_SOURCE_DIR}/src"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" # Where the Doxyfile will be generated
            # OPTIONAL_BUILD_GROUP  # Group docs into a 'Documentation' section in Visual Studio
            # COMMENT "Generating API documentation with Doxygen" # This is usually part of the generated target
        )
        
        message(STATUS "Custom target 'generateDocs' added for Doxygen documentation.")
    else()
        message(WARNING "Doxygen not found. Documentation will not be generated. Please install Doxygen or disable GEMINI_CLI_BUILD_DOCS.")
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
