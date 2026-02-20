#include "taxi/DatasetManager.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace taxi;

/**
 * @brief Time a function call and return average milliseconds over multiple runs.
 * 
 * Uses steady_clock for accurate timing measurements.
 */
template<typename Func>
double time_function_ms(Func&& func, int num_runs) {
    if (num_runs <= 0) return 0.0;

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < num_runs; ++i) {
        func();
    }
    auto end = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    return duration.count() / 1000.0 / num_runs; // Convert to milliseconds, average
}

/**
 * @brief Check if a file exists and is readable.
 */
bool file_exists(const std::string& filepath) {
    std::ifstream file(filepath);
    return file.good();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file_path> [num_runs]\n";
        std::cerr << "\n";
        std::cerr << "Arguments:\n";
        std::cerr << "  csv_file_path: Path to the TLC taxi trip CSV file\n";
        std::cerr << "  num_runs:      Number of benchmark runs (default: 10)\n";
        std::cerr << "\n";
        std::cerr << "Example:\n";
        std::cerr << "  " << argv[0] << " data/yellow_taxi_2018.csv 10\n";
        return 1;
    }

    const std::string csv_path = argv[1];
    const int num_runs = (argc >= 3) ? std::atoi(argv[2]) : 10;

    // Validate arguments
    if (num_runs <= 0) {
        std::cerr << "ERROR: Number of runs must be positive (got: " << num_runs << ")\n";
        return 1;
    }

    if (!file_exists(csv_path)) {
        std::cerr << "ERROR: CSV file not found: " << csv_path << "\n";
        return 1;
    }

    std::cout << "===================================================================\n";
    std::cout << "CMPE 275 Mini1 - Phase 1 Benchmark Harness\n";
    std::cout << "===================================================================\n";
    std::cout << "CSV file:     " << csv_path << "\n";
    std::cout << "Number of runs: " << num_runs << "\n";
    std::cout << "===================================================================\n\n";

    try {
        // Benchmark: Load CSV
        std::cout << "[1/3] Benchmarking CSV load...\n";
        std::cout << "      Running " << num_runs << " iterations...\n";
        
        double load_avg_ms = time_function_ms([&csv_path]() {
            DatasetManager manager;
            manager.load_from_csv(csv_path);
        }, num_runs);

        // Load once for statistics and search benchmarks
        DatasetManager manager;
        manager.load_from_csv(csv_path);
        auto load_stats = manager.get_load_stats();

        if (manager.size() == 0) {
            std::cerr << "\nERROR: No records loaded from CSV file!\n";
            std::cerr << "       Check that the file format is correct.\n";
            return 1;
        }

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "      ✓ Average load time: " << load_avg_ms << " ms\n";
        std::cout << "      ✓ Records loaded: " << manager.size() << "\n";
        std::cout << "      ✓ Rows read: " << load_stats.total_rows_read << "\n";
        std::cout << "      ✓ Rows parsed OK: " << load_stats.total_rows_parsed << "\n";
        std::cout << "      ✓ Rows discarded: " << load_stats.total_rows_discarded << "\n";
        
        if (load_stats.total_rows_read > 0) {
            double success_rate = 100.0 * load_stats.total_rows_parsed / load_stats.total_rows_read;
            std::cout << "      ✓ Parse success rate: " << std::setprecision(1) 
                      << success_rate << "%\n";
        }
        std::cout << "\n";

        // Benchmark: Search by fare
        std::cout << "[2/3] Benchmarking search_by_fare(10.0, 50.0)...\n";
        std::cout << "      Running " << num_runs << " iterations...\n";
        
        std::vector<const TripRecord*> fare_results;
        double search_fare_avg_ms = time_function_ms([&manager, &fare_results]() {
            fare_results = manager.search_by_fare(10.0, 50.0);
        }, num_runs);
        
        std::cout << std::setprecision(2);
        std::cout << "      ✓ Average search time: " << search_fare_avg_ms << " ms\n";
        std::cout << "      ✓ Matches found: " << fare_results.size() << "\n";
        std::cout << "\n";

        // Benchmark: Search by distance
        std::cout << "[3/3] Benchmarking search_by_distance(1.0, 5.0)...\n";
        std::cout << "      Running " << num_runs << " iterations...\n";
        
        std::vector<const TripRecord*> distance_results;
        double search_distance_avg_ms = time_function_ms([&manager, &distance_results]() {
            distance_results = manager.search_by_distance(1.0, 5.0);
        }, num_runs);
        
        std::cout << "      ✓ Average search time: " << search_distance_avg_ms << " ms\n";
        std::cout << "      ✓ Matches found: " << distance_results.size() << "\n";
        std::cout << "\n";

        // Summary
        std::cout << "===================================================================\n";
        std::cout << "Benchmark Summary\n";
        std::cout << "===================================================================\n";
        std::cout << std::setprecision(2);
        std::cout << "Load Performance:\n";
        std::cout << "  Average time: " << load_avg_ms << " ms\n";
        std::cout << "  Records/sec:  " << (manager.size() / (load_avg_ms / 1000.0)) << "\n";
        std::cout << "\n";
        std::cout << "Search Performance:\n";
        std::cout << "  search_by_fare:      " << search_fare_avg_ms << " ms\n";
        std::cout << "  search_by_distance:  " << search_distance_avg_ms << " ms\n";
        std::cout << "\n";
        std::cout << "Dataset Statistics:\n";
        std::cout << "  Total records: " << manager.size() << "\n";
        std::cout << "  Parse success: " << std::setprecision(1)
                  << (load_stats.total_rows_read > 0 ? 
                      100.0 * load_stats.total_rows_parsed / load_stats.total_rows_read : 0.0)
                  << "%\n";
        std::cout << "===================================================================\n";
        std::cout << "Benchmark Complete ✓\n";
        std::cout << "===================================================================\n";

    } catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
