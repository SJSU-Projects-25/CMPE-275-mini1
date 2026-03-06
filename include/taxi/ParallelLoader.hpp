#pragma once

#include "taxi/TripRecord.hpp"
#include "taxi/CsvReader.hpp"
#include <string>
#include <vector>
#include <cstddef>

namespace taxi {

/**
 * @brief Parallel CSV loader using std::thread.
 *
 * Splits the CSV file into N byte-range chunks and dispatches one
 * std::thread per chunk.  Each thread runs an independent CsvReader on
 * its slice; results are merged after all threads join.
 *
 * Parallel CSV parsing implementation (Phase 2).
 */
class ParallelLoader {
public:
    struct Result {
        std::vector<TripRecord> records;

        // Aggregate parse statistics across all threads
        std::size_t total_rows_read      = 0;
        std::size_t total_rows_parsed    = 0;
        std::size_t total_rows_discarded = 0;

        double load_time_ms = 0.0;   // wall-clock time for the parallel load
        int    threads_used = 0;
    };

    /**
     * @brief Load a CSV file using N parallel threads.
     *
     * @param path        Path to the TLC taxi CSV file.
     * @param num_threads Number of threads to use (clamped to >= 1).
     * @return Result struct with records, stats, and timing.
     */
    static Result load(const std::string& path, int num_threads);
};

} // namespace taxi
