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

# Example:
./bin/taxi_bench ../data/2018_Yellow_Taxi_Trip_Data_20260216.csv 10
```

## Downloading Data

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

**Note**: The assignment requires datasets totaling >12 GB and >2 million records. You may need multiple monthly/yearly datasets.

## Phase 1 Status

✅ Build system configured  
✅ Project structure created  
✅ Data automation script ready  
🚧 CSV parsing (in progress)  
🚧 Benchmark harness (in progress)

## Team Responsibilities

- **Person A**: Data ingestion + parsing pipeline (core)
  - CSV reader (streaming, buffered)
  - Schema mapping (per-column parsing, type conversion)
  - Error handling strategy (bad rows, missing values)
  - Field normalization (dates, ints, floats)
  - TaxiTrip loader that outputs Phase 1 structure
  - Build system (CMakeLists.txt)
  - Benchmarking harness
