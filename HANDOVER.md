# Phase 1 Handover Document

## Overview

Phase 1 is **COMPLETE** ✅. This document provides everything the next team member needs to understand the codebase and continue with Phase 2 (parallelization).

## What Was Built

### Core Components

1. **TripRecord** (`include/taxi/TripRecord.hpp`)
   - Data structure with primitive types only
   - 17 fields: vendor_id, timestamps, passenger_count, distances, monetary values
   - Validation method: `is_valid()`

2. **CsvReader** (`include/taxi/CsvReader.hpp`, `src/CsvReader.cpp`)
   - Streaming CSV parser (RFC 4180 compliant)
   - Handles quoted fields, escaped quotes, empty fields
   - Parses timestamps: "YYYY MMM DD HH:MM:SS AM/PM" → Unix epoch seconds
   - Error handling: discards invalid rows, normalizes missing values
   - Statistics tracking: rows read, parsed, discarded

3. **DatasetManager** (`include/taxi/DatasetManager.hpp`, `src/DatasetManager.cpp`)
   - Main library interface
   - Loads CSV files into `std::vector<TripRecord>` (Array-of-Objects pattern)
   - Three search APIs:
     - `search_by_fare(min, max)` - returns pointers to matching records
     - `search_by_distance(min, max)` - returns pointers to matching records
     - `search_by_passenger_count(min, max)` - returns pointers to matching records
   - All operations are **serial** (no threading) as required for Phase 1

4. **Benchmark Harness** (`src/benchmark_main.cpp`)
   - Times CSV loading and search operations
   - Runs multiple iterations and calculates averages
   - Reports performance metrics and statistics

## Phase 1 Baseline Performance

**Test Dataset (10K records):**
- Load time: ~21ms per iteration
- Search by fare: ~0.02ms
- Search by distance: ~0.02ms
- Parse success rate: 99.7%

**Full Dataset (6.4M records):**
- Load time: ~14 seconds per iteration
- Search by fare: ~58ms
- Search by distance: ~25ms
- Parse success rate: 99.8%

**These are your baseline metrics for Phase 2 and Phase 3 comparisons.**

## Code Structure

```
include/taxi/
├── TripRecord.hpp      # Data structure (primitive types only)
├── CsvReader.hpp       # CSV parsing interface
└── DatasetManager.hpp  # Main API (search operations)

src/
├── TripRecord.cpp           # Validation logic
├── CsvReader.cpp            # CSV parsing implementation
├── DatasetManager.cpp       # Search implementations (serial)
└── benchmark_main.cpp       # Benchmark harness

scripts/
├── download_tlc_data.sh     # Download full datasets
├── create_test_data.sh      # Create test samples
└── validate_build.sh        # Build validation
```

## Key Design Decisions

### 1. Array-of-Objects Pattern
- Current: `std::vector<TripRecord>` - each record is an object
- Phase 3 will convert to Object-of-Arrays for better cache performance

### 2. Search APIs Return Pointers
- Returns `std::vector<const TripRecord*>` to avoid copying
- Makes parallelization easier (can use thread-local vectors)

### 3. Error Handling Strategy
- **Critical fields** (timestamps): Invalid → discard row
- **Non-critical fields**: Missing → normalize to 0/0.0
- Statistics tracked for reporting

### 4. Timestamp Format
- CSV format: `"2018 Nov 04 12:32:24 PM"`
- Parsed to: Unix epoch seconds (int64_t)
- Handles both 12-hour format and 24-hour format

## How to Run

### Quick Test
```bash
# Build
cd build && cmake .. && cmake --build .

# Test with sample data (fast)
./bin/taxi_bench ../data/test_sample.csv 10

# Test with full dataset (slow, ~2+ minutes for 10 iterations)
./bin/taxi_bench ../data/2018_Yellow_Taxi_Trip_Data_20260216.csv 10
```

### Validate Build
```bash
bash scripts/validate_build.sh
```

## Next Steps for Phase 2

### Person B's Tasks

1. **Add Parallelization to Search Operations**
   - Target: `DatasetManager::search_by_fare()`, `search_by_distance()`, `search_by_passenger_count()`
   - Options: OpenMP (`#pragma omp parallel for`) or `std::thread`
   - Keep `load_from_csv()` serial for now (or parallelize if time permits)

2. **Update CMakeLists.txt**
   - Add OpenMP support if using OpenMP:
     ```cmake
     find_package(OpenMP)
     if(OpenMP_CXX_FOUND)
         target_link_libraries(taxi_core PUBLIC OpenMP::OpenMP_CXX)
     endif()
     ```

3. **Benchmark and Compare**
   - Run same benchmarks as Phase 1
   - Compare performance: Phase 1 vs Phase 2
   - Document speedup ratios and findings

### Suggested Implementation Approach

**Option 1: OpenMP (Easier)**
```cpp
std::vector<const TripRecord*> DatasetManager::search_by_fare(double min_fare, double max_fare) const {
    std::vector<const TripRecord*> results;
    results.reserve(records_.size() / 10);
    
    #pragma omp parallel
    {
        std::vector<const TripRecord*> local_results;
        local_results.reserve(records_.size() / 10);
        
        #pragma omp for nowait
        for (size_t i = 0; i < records_.size(); ++i) {
            if (records_[i].fare_amount >= min_fare && 
                records_[i].fare_amount <= max_fare) {
                local_results.push_back(&records_[i]);
            }
        }
        
        #pragma omp critical
        results.insert(results.end(), local_results.begin(), local_results.end());
    }
    
    return results;
}
```

**Option 2: std::thread**
- Split records into chunks
- Each thread processes a chunk
- Merge results at the end

### Testing Strategy

1. **Verify correctness:**
   - Compare Phase 2 results with Phase 1 (should be identical)
   - Test with `test_sample.csv` first (fast iteration)

2. **Measure performance:**
   - Use existing benchmark harness
   - Run with different thread counts
   - Document speedup ratios

3. **Handle edge cases:**
   - Empty datasets
   - Single-threaded execution
   - Very small result sets

## Files to Modify for Phase 2

1. **`src/DatasetManager.cpp`**
   - Add parallelization to search functions
   - Keep serial version commented for comparison

2. **`CMakeLists.txt`**
   - Add OpenMP/threading support
   - Add compiler flags if needed

3. **`src/benchmark_main.cpp`** (optional)
   - Add thread count reporting
   - Compare Phase 1 vs Phase 2 results side-by-side

## Common Issues & Solutions

### Issue: Results differ from Phase 1
- **Solution**: Check thread safety - ensure no race conditions
- Verify merge logic for combining thread-local results

### Issue: No speedup or slowdown
- **Solution**: Overhead might be too high for small datasets
- Try with full dataset (6.4M records)
- Check if data is cache-friendly

### Issue: Compilation errors with OpenMP
- **Solution**: Install OpenMP library
- macOS: `brew install libomp`
- Linux: Usually included with compiler

## Questions?

If you have questions about:
- **CSV parsing**: Check `src/CsvReader.cpp` - well commented
- **Data structure**: Check `include/taxi/TripRecord.hpp`
- **Search logic**: Check `src/DatasetManager.cpp` - simple loops
- **Benchmarking**: Check `src/benchmark_main.cpp` - uses `<chrono>`

## Baseline Metrics Summary

| Operation | Test Data (10K) | Full Data (6.4M) |
|-----------|----------------|-------------------|
| Load CSV  | ~21ms          | ~14s             |
| Search fare | ~0.02ms      | ~58ms            |
| Search distance | ~0.02ms | ~25ms        |

**Goal for Phase 2:** Achieve 2-4x speedup on search operations with parallelization.

Good luck! 🚀
