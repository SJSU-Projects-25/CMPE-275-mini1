#include "taxi/ParallelLoader.hpp"
#include "taxi/BenchmarkRunner.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace taxi {

ParallelLoader::Result ParallelLoader::load(const std::string& path,
                                             int num_threads)
{
    if (num_threads < 1) num_threads = 1;

    // ---- 1. Measure total file size ----------------------------------------
    std::ifstream probe(path, std::ios::in | std::ios::ate);
    if (!probe.is_open()) {
        throw std::runtime_error("ParallelLoader: cannot open file: " + path);
    }
    const std::int64_t file_size = static_cast<std::int64_t>(probe.tellg());
    probe.close();

    if (file_size <= 0) {
        throw std::runtime_error("ParallelLoader: empty or unreadable file: " + path);
    }

    // Clamp threads so we never spawn more threads than bytes (degenerate files)
    num_threads = static_cast<int>(
        std::min(static_cast<std::int64_t>(num_threads), file_size));

    // ---- 2. Compute byte ranges --------------------------------------------
    // Each chunk gets an equal slice of the file.  Actual line boundaries are
    // handled inside CsvReader::load_chunk: when byte_start > 0 it discards
    // the partial line at the start of the chunk (which the previous chunk
    // already read in full).
    std::vector<std::int64_t> starts(num_threads);
    std::vector<std::int64_t> ends(num_threads);

    const std::int64_t chunk_size = file_size / num_threads;
    for (int i = 0; i < num_threads; ++i) {
        starts[i] = static_cast<std::int64_t>(i) * chunk_size;
        ends[i]   = (i == num_threads - 1)
                        ? file_size - 1
                        : starts[i] + chunk_size - 1;
    }

    // ---- 3. Spawn threads --------------------------------------------------
    std::vector<std::thread>                threads(num_threads);
    std::vector<std::vector<TripRecord>>    partial_records(num_threads);
    std::vector<CsvReader::Stats>           partial_stats(num_threads);

    auto wall_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        threads[i] = std::thread([&, i]() {
            partial_records[i] = CsvReader::load_chunk(
                path, starts[i], ends[i], partial_stats[i]);
        });
    }

    // ---- 4. Join and merge -------------------------------------------------
    for (auto& t : threads) t.join();

    auto wall_end = std::chrono::steady_clock::now();

    Result result;
    result.threads_used  = num_threads;
    result.load_time_ms  =
        std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    // Pre-size the output vector to avoid repeated reallocation during merge
    std::size_t total = 0;
    for (int i = 0; i < num_threads; ++i) total += partial_records[i].size();
    result.records.reserve(total);

    for (int i = 0; i < num_threads; ++i) {
        result.records.insert(result.records.end(),
                              partial_records[i].begin(),
                              partial_records[i].end());

        result.total_rows_read      += partial_stats[i].rows_read;
        result.total_rows_parsed    += partial_stats[i].rows_parsed_ok;
        result.total_rows_discarded += partial_stats[i].rows_discarded;
    }

    return result;
}

} // namespace taxi
