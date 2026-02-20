# CMPE-275-mini1

This is mini project 1 for Distributed Application Development - Memory Overload

Focus: Memory utilization and concurrent processing

## Project Structure

```
.
├── CMakeLists.txt          # CMake build configuration
├── include/
│   └── taxi/              # Library headers
│       ├── TripRecord.hpp
│       ├── CsvReader.hpp
│       └── DatasetManager.hpp
├── src/                   # Library implementation
│   ├── TripRecord.cpp
│   ├── CsvReader.cpp
│   ├── DatasetManager.cpp
│   └── benchmark_main.cpp # Benchmark harness executable
├── scripts/
│   └── download_tlc_data.sh  # Data download automation
└── data/                  # CSV data files (gitignored)
```

## Building the Project

### Prerequisites

- **CMake** (version 3.20 or newer)
- **C++ Compiler**:
  - GCC/G++ v13+ OR
  - Clang v16+ (not Apple's Xcode)
- **C++ Standard**: C++20

### Build Instructions

```bash
# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build the project
cmake --build .

# Or use make (on Unix systems)
make
```

The build will create:
- **Library**: `build/lib/libtaxi_core.a` (or `.so` on Linux)
- **Executable**: `build/bin/taxi_bench` (benchmark harness)

### Running the Benchmark

```bash
# From the build directory
./bin/taxi_bench <path_to_csv_file> [num_runs]

# Example with test data (fast, ~20ms per iteration):
./bin/taxi_bench ../data/test_sample.csv 10

# Example with full dataset (slow, ~14s per iteration):
./bin/taxi_bench ../data/2018_Yellow_Taxi_Trip_Data_20260216.csv 10
```

**Note**: For quick testing, use `data/test_sample.csv` (10K rows, ~1.2MB). For final benchmarks, use the full dataset.

## Test Data

A small test dataset (`data/test_sample.csv`) is included for quick testing and development:
- **Size**: ~1.3 MB
- **Records**: 10,000 rows
- **Purpose**: Fast testing, can be committed to GitHub
- **Usage**: `./build/bin/taxi_bench data/test_sample.csv 10` (~20ms per iteration)

To create your own test sample from a larger dataset:
```bash
bash scripts/create_test_data.sh [input_file] [num_rows] [output_file]

# Example: Extract 5000 rows
bash scripts/create_test_data.sh data/large_file.csv 5000 data/my_test.csv
```

## Downloading Full Data

The `scripts/download_tlc_data.sh` script automates downloading TLC Yellow Taxi Trip Data from NYC OpenData.

**Before using**, edit the script and add your dataset URLs to the `DATASET_URLS` array:

```bash
# Edit the script
vim scripts/download_tlc_data.sh

# Add your URLs in this format:
DATASET_URLS=(
    "https://data.cityofnewyork.us/api/views/.../rows.csv?accessType=DOWNLOAD|filename.csv"
    # ... more URLs
)
```

Then run:

```bash
bash scripts/download_tlc_data.sh
```

The script will:
- Download datasets to `data/` directory
- Skip files that already exist
- Show progress and summary

**Note**: The assignment requires datasets totaling >12 GB and >2 million records. You may need multiple monthly/yearly datasets. Large data files are gitignored - only `test_sample.csv` is committed.

## Testing

### Test CSV Reader

```bash
# Build test utility
cd build
make test_csv_reader

# Test parsing on a sample file
./bin/test_csv_reader ../data/your_file.csv 10
```

### Run Full Benchmark

```bash
# Build benchmark harness
cd build
make taxi_bench

# Run benchmark (10 iterations by default)
./bin/taxi_bench ../data/2018_Yellow_Taxi_Trip_Data_20260216.csv 10
```

## Phase 1 Status

✅ Build system configured  
✅ Project structure created  
✅ Data automation script ready  
✅ CSV reader (streaming, RFC 4180 compliant)  
✅ Schema mapping and type conversion  
✅ Error handling and normalization  
✅ DatasetManager with search APIs  
✅ Benchmark harness with timing

## Architecture

### Phase 1 Design (Current)

- **TripRecord**: Data structure with primitive types only (int, double, bool, int64_t)
- **CsvReader**: Streaming CSV parser with RFC 4180 compliance
  - Handles quoted fields, escaped quotes, empty fields
  - Type conversion with error handling
  - Timestamp parsing (YYYY-MM-DD HH:MM:SS → Unix epoch seconds)
- **DatasetManager**: Main library interface
  - Loads CSV files into memory (Array-of-Objects pattern)
  - Provides search APIs: `search_by_fare()`, `search_by_distance()`, `search_by_passenger_count()`
  - Tracks loading statistics
- **Benchmark Harness**: Performance measurement tool
  - Times CSV loading and search operations
  - Runs multiple iterations and calculates averages
  - Reports statistics and success rates

### Error Handling Strategy

- **Critical fields** (timestamps): Invalid values cause row to be discarded
- **Non-critical fields**: Missing/invalid values normalized to defaults (0 or 0.0)
- **Statistics**: Tracks rows read, parsed successfully, and discarded
- **Validation**: Records must pass `TripRecord::is_valid()` check

## Team Responsibilities

- **Ashish**: Data ingestion + parsing pipeline (core) ✅ COMPLETE
  - ✅ CSV reader (streaming, buffered)
  - ✅ Schema mapping (per-column parsing, type conversion)
  - ✅ Error handling strategy (bad rows, missing values)
  - ✅ Field normalization (dates, ints, floats)
  - ✅ TaxiTrip loader that outputs Phase 1 structure
  - ✅ Build system (CMakeLists.txt)
  - ✅ Benchmarking harness
