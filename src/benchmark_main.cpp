#include "taxi/DatasetManager.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <cstdlib>

using namespace taxi;

/**
 * @brief Time a function call and return average milliseconds over multiple runs.
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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file_path> [num_runs]\n";
        std::cerr << "  csv_file_path: Path to the TLC taxi trip CSV file\n";
        std::cerr << "  num_runs: Number of benchmark runs (default: 10)\n";
        return 1;
    }

    const std::string csv_path = argv[1];
    const int num_runs = (argc >= 3) ? std::atoi(argv[2]) : 10;

    std::cout << "=== CMPE 275 Mini1 Benchmark Harness ===\n";
    std::cout << "CSV file: " << csv_path << "\n";
    std::cout << "Number of runs: " << num_runs << "\n\n";

    // Benchmark: Load CSV
    std::cout << "Benchmarking CSV load...\n";
    double load_avg_ms = time_function_ms([&csv_path]() {
        DatasetManager manager;
        manager.load_from_csv(csv_path);
    }, num_runs);

    DatasetManager manager;
    manager.load_from_csv(csv_path);
    auto load_stats = manager.get_load_stats();

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Average load time: " << load_avg_ms << " ms\n";
    std::cout << "  Records loaded: " << manager.size() << "\n";
    std::cout << "  Rows read: " << load_stats.total_rows_read << "\n";
    std::cout << "  Rows parsed OK: " << load_stats.total_rows_parsed << "\n";
    std::cout << "  Rows discarded: " << load_stats.total_rows_discarded << "\n\n";

    // Benchmark: Search by fare
    std::cout << "Benchmarking search_by_fare(10.0, 50.0)...\n";
    std::vector<const TripRecord*> fare_results;
    double search_fare_avg_ms = time_function_ms([&manager, &fare_results]() {
        fare_results = manager.search_by_fare(10.0, 50.0);
    }, num_runs);
    std::cout << "  Average search time: " << search_fare_avg_ms << " ms\n";
    std::cout << "  Matches found: " << fare_results.size() << "\n\n";

    // Benchmark: Search by distance
    std::cout << "Benchmarking search_by_distance(1.0, 5.0)...\n";
    std::vector<const TripRecord*> distance_results;
    double search_distance_avg_ms = time_function_ms([&manager, &distance_results]() {
        distance_results = manager.search_by_distance(1.0, 5.0);
    }, num_runs);
    std::cout << "  Average search time: " << search_distance_avg_ms << " ms\n";
    std::cout << "  Matches found: " << distance_results.size() << "\n\n";

    std::cout << "=== Benchmark Complete ===\n";
    return 0;
}
