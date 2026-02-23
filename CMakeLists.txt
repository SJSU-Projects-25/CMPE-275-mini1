cmake_minimum_required(VERSION 3.20)
project(CMPE275_Mini1 VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard (C++20 for better features, but C++17 compatible)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
        message(FATAL_ERROR "GCC version 13.0 or newer is required. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "16.0")
        message(FATAL_ERROR "Clang version 16.0 or newer is required. Found: ${CMAKE_CXX_COMPILER_VERSION}")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

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
)

target_include_directories(taxi_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(OpenMP_CXX_FOUND)
    target_link_libraries(taxi_core PUBLIC OpenMP::OpenMP_CXX)
endif()

# Executable target: taxi_bench (benchmark harness)
add_executable(taxi_bench
    src/benchmark_main.cpp
)

target_link_libraries(taxi_bench PRIVATE taxi_core)

# Test executable: test_csv_reader (temporary, for Step 4 validation)
add_executable(test_csv_reader
    src/test_csv_reader.cpp
)

target_link_libraries(test_csv_reader PRIVATE taxi_core)

# Test executable: test_query_engine (Phase 2 Step 5 validation)
add_executable(test_query_engine
    src/test_query_engine.cpp
)

target_link_libraries(test_query_engine PRIVATE taxi_core)

# Set output directories
set_target_properties(taxi_core taxi_bench PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
