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

## Quick Start Guide

### For New Team Members

1. **Clone and build:**
   ```bash
   git clone <repo-url>
   cd CMPE-275-mini1
   bash scripts/validate_build.sh
   ```

2. **Test with sample data:**
   ```bash
   ./build/bin/taxi_bench data/test_sample.csv 3
   ```

3. **Review the code:**
   - Start with `include/taxi/TripRecord.hpp` - data structure
   - Then `include/taxi/CsvReader.hpp` - CSV parsing
   - Then `include/taxi/DatasetManager.hpp` - main API
   - Finally `src/benchmark_main.cpp` - benchmark harness

4. **Read handover document:**
   - See `HANDOVER.md` for detailed Phase 1 → Phase 2 transition guide
   - Includes API documentation, design decisions, and implementation suggestions

5. **Understand Phase 1 baseline:**
   - Current performance: ~21ms load for 10K records, ~14s for 6.4M records
   - Search operations: ~0.02ms for 10K records
   - This is your baseline for Phase 2 (parallelization) and Phase 3 (vectorization)

## API Documentation for Phase 2/3

### DatasetManager API

The `DatasetManager` class is the main interface for Phase 2 parallelization:

```cpp
// Load CSV file (serial - Phase 1)
void load_from_csv(const std::string& csv_path);

// Search APIs (currently serial - Phase 2 will parallelize these)
std::vector<const TripRecord*> search_by_fare(double min_fare, double max_fare) const;
std::vector<const TripRecord*> search_by_distance(double min_distance, double max_distance) const;
std::vector<const TripRecord*> search_by_passenger_count(int min_passengers, int max_passengers) const;

// Access loaded records
const std::vector<TripRecord>& records() const;
std::size_t size() const;
```

### Key Design Decisions

1. **Array-of-Objects Pattern (Phase 1):**
   - `std::vector<TripRecord>` stores records as objects
   - Phase 3 will convert to Object-of-Arrays for better cache performance

2. **Search APIs return pointers:**
   - Returns `std::vector<const TripRecord*>` to avoid copying
   - Phase 2 can parallelize the search loops easily

3. **No threading in Phase 1:**
   - All operations are serial as required
   - Phase 2 will add OpenMP/threading to search operations

## Handover Notes for Phase 2

### What's Complete (Phase 1)
- ✅ Complete CSV parsing pipeline
- ✅ Data loading into memory (Array-of-Objects)
- ✅ Three search APIs (fare, distance, passenger_count)
- ✅ Benchmark harness with timing
- ✅ Error handling and statistics tracking
- ✅ Test data for quick development

### What Needs to Be Done (Phase 2)

**Person B's Responsibilities:**
1. **Add parallelization to search operations:**
   - Use OpenMP or std::thread to parallelize search loops
   - Focus on `search_by_fare()`, `search_by_distance()`, `search_by_passenger_count()`
   - Keep `load_from_csv()` serial for now (or parallelize if time permits)

2. **Update CMakeLists.txt:**
   - Add OpenMP flags if using OpenMP
   - Ensure thread-safe compilation

3. **Benchmark and compare:**
   - Run same benchmarks as Phase 1
   - Compare performance improvements
   - Document speedup ratios

### Suggested Approach for Phase 2

1. **Start with one search function:**
   ```cpp
   // Example: Parallelize search_by_fare using OpenMP
   #pragma omp parallel for
   for (size_t i = 0; i < records_.size(); ++i) {
       if (records_[i].fare_amount >= min_fare && 
           records_[i].fare_amount <= max_fare) {
           #pragma omp critical
           results.push_back(&records_[i]);
       }
   }
   ```

2. **Measure performance:**
   - Use the existing benchmark harness
   - Compare Phase 1 vs Phase 2 results
   - Document findings

3. **Handle thread safety:**
   - Results vector needs thread-safe insertion
   - Consider pre-allocating or using thread-local storage

## Troubleshooting

### Build Issues

**CMake not found:**
```bash
# macOS
brew install cmake

# Linux
sudo apt-get install cmake  # Ubuntu/Debian
sudo yum install cmake       # CentOS/RHEL
```

**Compiler version issues:**
- Check version: `g++ --version` or `clang++ --version`
- Update if needed (GCC 13+ or Clang 16+ required)

### Runtime Issues

**"No records loaded":**
- Check CSV file format matches expected schema
- Verify file path is correct
- Check file permissions

**Benchmark takes too long:**
- Use `data/test_sample.csv` for quick testing (10K rows)
- Full dataset (6.4M rows) takes ~14s per iteration

**Parse success rate low:**
- Check CSV format (should have 17 columns)
- Verify timestamp format matches: "YYYY MMM DD HH:MM:SS AM/PM"
- Some rows may be invalid - this is normal (99.7% success rate is good)

## Team Responsibilities

- **Person A (Ashish)**: Data ingestion + parsing pipeline (core) ✅ COMPLETE
  - ✅ CSV reader (streaming, buffered)
  - ✅ Schema mapping (per-column parsing, type conversion)
  - ✅ Error handling strategy (bad rows, missing values)
  - ✅ Field normalization (dates, ints, floats)
  - ✅ TaxiTrip loader that outputs Phase 1 structure
  - ✅ Build system (CMakeLists.txt)
  - ✅ Benchmarking harness

- **Person B**: Phase 2 - Parallelization 🚧 TODO
  - Add OpenMP/threading to search operations
  - Benchmark and compare with Phase 1 baseline
  - Document performance improvements

- **Person C**: Phase 3 - Vectorization 🚧 TODO
  - Convert Array-of-Objects to Object-of-Arrays
  - Optimize memory layout for cache performance
  - Benchmark and document improvements
