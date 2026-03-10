cmake_minimum_required(VERSION 3.20)
project(CMPE275_Mini1 VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard (C++20 for better features, but C++17 compatible)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler guard ─────────────────────────────────────────────────────────────
# REQUIREMENT: GCC >= 13  OR  non-Apple Clang >= 16.
# Apple's Xcode Clang (AppleClang) is explicitly banned because it ships without
# libomp and produces different OpenMP behaviour than upstream Clang.
# On macOS use GCC from Homebrew:  brew install gcc
#   cmake .. -DCMAKE_CXX_COMPILER=g++-15 -DCMAKE_C_COMPILER=gcc-15
if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    message(FATAL_ERROR
        "\n*** Apple's Xcode Clang (AppleClang) is not allowed by the project requirements.\n"
        "    Use GCC >= 13 or upstream Clang >= 16 (e.g. from Homebrew).\n"
        "    Quick fix (macOS):\n"
        "      brew install gcc\n"
        "      cmake .. -DCMAKE_CXX_COMPILER=g++-15 -DCMAKE_C_COMPILER=gcc-15\n")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "GCC >= 13.0 required. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
        message(FATAL_ERROR "Clang >= 16.0 required. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
else()
    message(WARNING "Unknown compiler '${CMAKE_CXX_COMPILER_ID}'. Proceeding, but GCC>=13 or Clang>=16 is required.")
endif()

# ── OpenMP ─────────────────────────────────────────────────────────────────────
# GCC ships with full libgomp (no extra flags needed).
# Upstream Clang (from Homebrew llvm) also ships with libomp.
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: version ${OpenMP_CXX_VERSION}")
else()
    message(WARNING "OpenMP not found — building serial-only version")
endif()

# Build type defaults to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Include directories
include_directories(include)

# Library target: taxi_core
add_library(taxi_core
    src/TripRecord.cpp
    src/CsvReader.cpp
    src/DatasetManager.cpp
    src/TimeIndex.cpp
    src/QueryEngine.cpp
    src/SoAQueryEngine.cpp
    src/MetricsRecorder.cpp
    src/ParallelLoader.cpp
)

target_include_directories(taxi_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(taxi_core PUBLIC OpenMP::OpenMP_CXX)
endif()

# Executable target: taxi_bench_full (full benchmark CLI — all phases)
add_executable(taxi_bench_full
    src/benchmark_main.cpp
)
target_link_libraries(taxi_bench_full PRIVATE taxi_core)

# Test executable: test_csv_reader
add_executable(test_csv_reader
    src/test_csv_reader.cpp
)
target_link_libraries(test_csv_reader PRIVATE taxi_core)

# Test executable: test_query_engine
add_executable(test_query_engine
    src/test_query_engine.cpp
)
target_link_libraries(test_query_engine PRIVATE taxi_core)

# Unit tests
add_executable(unit_tests
    src/unit_tests.cpp
)
target_link_libraries(unit_tests PRIVATE taxi_core)

# Set output directories for all targets
set_target_properties(
    taxi_core taxi_bench_full test_csv_reader test_query_engine unit_tests
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
