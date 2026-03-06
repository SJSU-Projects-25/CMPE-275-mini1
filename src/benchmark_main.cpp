/**
 * benchmark_main.cpp — comprehensive benchmark CLI
 *
 * Runs all 6 QueryEngine queries (or a user-selected subset) over N iterations,
 * computes avg ± stddev, and optionally writes a CSV for plotting.
 * Also benchmarks serial vs parallel CSV loading (ParallelLoader).
 *
 * Usage:
 *   taxi_bench_full <csv_file> [<csv_file2> ...] [options]
 *
 * Options:
 *   --runs N           Number of timed iterations per query (default: 10)
 *   --queries <list>   Comma-separated query IDs: Q1,Q2,Q3,Q4,Q5,Q6 or "all" (default)
 *   --output <file>    Write metrics CSV to this path (e.g. results/bench.csv)
 *   --serial           Phase 1 baseline: 1 thread for load + queries
 *   --threads N        Phase 2 parallel: N threads for load + OMP queries
 *
 * Multiple CSV files are concatenated into one dataset before querying.
 * Example (all 4 years):
 *   taxi_bench_full data/2020.csv data/2021.csv data/2022.csv data/2023.csv --serial
 */

#include "taxi/DatasetManager.hpp"
#include "taxi/ParallelLoader.hpp"
#include "taxi/QueryEngine.hpp"
#include "taxi/SoAQueryEngine.hpp"
#include "taxi/TripDataSoA.hpp"
#include "taxi/QueryTypes.hpp"
#include "taxi/BenchmarkRunner.hpp"
#include "taxi/MetricsRecorder.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace taxi;

// ---- helpers ----------------------------------------------------------------

static bool file_exists(const std::string& p) {
    std::ifstream f(p);
    return f.good();
}

static std::set<std::string> parse_queries(const std::string& s) {
    std::set<std::string> result;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        result.insert(token);
    }
    return result;
}

static int omp_thread_count() {
#if defined(_OPENMP)
    return omp_get_max_threads();
#else
    return 1;
#endif
}

static void set_omp_threads(int n) {
#if defined(_OPENMP)
    omp_set_num_threads(n);
#else
    (void)n;
#endif
}

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <csv_file> [<csv_file2> ...] [options]\n\n"
              << "Options:\n"
              << "  --runs N          Timed iterations per query (default: 10)\n"
              << "  --queries <list>  Q1,Q2,Q3,Q4,Q5,Q6 or all (default: all)\n"
              << "  --output <file>   Write CSV to this file\n"
              << "  --serial          Phase 1: 1 thread for load + queries\n"
              << "  --threads N       Phase 2: N threads for load + OMP queries\n"
              << "  --soa             Phase 3: run queries on Object-of-Arrays layout\n"
              << "\nMultiple CSV files are concatenated before querying:\n"
              << "  " << prog << " data/2020.csv data/2021.csv data/2022.csv data/2023.csv --serial\n";
}

// ---- main -------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }

    // ---- Parse arguments ----
    std::vector<std::string> csv_paths;
    int         num_runs     = 10;
    std::string query_spec   = "all";
    std::string output_path;
    bool        serial_mode     = false;
    bool        soa_mode        = false;
    bool        soa_direct_mode = false;
    int         num_threads  = -1;   // -1 = not set by user

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--runs" && i + 1 < argc) {
            num_runs = std::atoi(argv[++i]);
        } else if (arg == "--queries" && i + 1 < argc) {
            query_spec = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--serial") {
            serial_mode = true;
            num_threads = 1;
        } else if (arg == "--soa") {
            soa_mode = true;
        } else if (arg == "--soa-direct") {
            soa_direct_mode = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::atoi(argv[++i]);
            if (num_threads < 1) num_threads = 1;
        } else if (arg[0] != '-') {
            csv_paths.push_back(arg);   // positional: one or more CSV files
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (csv_paths.empty()) { print_usage(argv[0]); return 1; }
    for (const auto& p : csv_paths) {
        if (!file_exists(p)) {
            std::cerr << "ERROR: file not found: " << p << "\n";
            return 1;
        }
    }
    if (num_runs <= 0) {
        std::cerr << "ERROR: --runs must be > 0\n";
        return 1;
    }

    // Determine parallelism level
    // --serial / --threads 1 → Phase 1 serial
    // --threads N (N>1)      → Phase 2 parallel
    // (neither)              → Phase 2 with all available OMP threads, serial load
    const bool use_parallel_load = (num_threads > 1);
    if (num_threads > 0) {
        set_omp_threads(num_threads);
    }
    if (serial_mode) {
        set_omp_threads(1);
    }

    const int   omp_threads = omp_thread_count();
    const int   load_threads = use_parallel_load ? num_threads : 1;
    const std::string base_phase = (load_threads == 1 && omp_threads == 1)
                                       ? "Phase1_serial"
                                       : "Phase2_parallel";
    const std::string phase = soa_direct_mode ? "Phase3_soa_direct"
                            : soa_mode        ? "Phase3_soa"
                            : base_phase;

    // ---- Resolve which queries to run ----
    const std::vector<std::string> ALL_QUERIES = {"Q1","Q2","Q3","Q4","Q5","Q6"};
    std::vector<std::string> active_queries;

    if (query_spec == "all") {
        active_queries = ALL_QUERIES;
    } else {
        auto requested = parse_queries(query_spec);
        for (const auto& q : ALL_QUERIES) {
            if (requested.count(q)) active_queries.push_back(q);
        }
        if (active_queries.empty()) {
            std::cerr << "ERROR: no valid query IDs in --queries \""
                      << query_spec << "\"\n"
                      << "       Valid IDs: Q1 Q2 Q3 Q4 Q5 Q6\n";
            return 1;
        }
    }

    // ---- Print header ----
    std::cout << "================================================================\n"
              << "CMPE 275 Mini1 — Full Benchmark CLI  (" << phase << ")\n"
              << "================================================================\n";
    if (csv_paths.size() == 1) {
        std::cout << "CSV file      : " << csv_paths[0] << "\n";
    } else {
        std::cout << "CSV files     : " << csv_paths[0] << "\n";
        for (std::size_t i = 1; i < csv_paths.size(); ++i)
            std::cout << "               " << csv_paths[i] << "\n";
    }
    std::cout << "Runs/query    : " << num_runs     << "\n"
              << "Queries       : " << query_spec   << "\n"
              << "Load threads  : " << load_threads << "\n"
              << "Query threads : " << omp_threads  << "\n"
              << "Layout        : " << (soa_direct_mode ? "SoA direct from CSV"
                                    : soa_mode        ? "Object-of-Arrays (SoA from AoS)"
                                    :                   "Array-of-Structs (AoS)") << "\n";
    if (!output_path.empty())
        std::cout << "Output        : " << output_path << "\n";
    std::cout << "================================================================\n\n";

    try {
        MetricsRecorder recorder;
        // Wall-clock for the ENTIRE phase (load + all queries + overhead).
        // Recorded as TOTAL_PHASE in the output CSV so you can compare
        // across runs without re-timing manually.
        const auto phase_wall_start = std::chrono::steady_clock::now();

        // ================================================================
        // Phase 3b: Direct CSV → SoA (no intermediate AoS)
        // ================================================================
        if (soa_direct_mode) {
            std::cout << "[Load] Direct CSV → SoA load (serial, "
                      << csv_paths.size() << " file(s))...\n";

            TripDataSoA soa;
            RunStats direct_timing = BenchmarkRunner::time_n([&]() {
                soa = TripDataSoA::from_csv(csv_paths, 95000000);
            }, num_runs);

            if (soa.size() == 0) {
                std::cerr << "ERROR: no records loaded (soa-direct).\n";
                return 1;
            }

            std::cout << std::fixed << std::setprecision(2)
                      << "  Records loaded : " << soa.size() << "\n"
                      << "  avg " << direct_timing.avg_ms
                      << " ms  ±" << direct_timing.stddev_ms
                      << "  min " << direct_timing.min_ms
                      << "  max " << direct_timing.max_ms << " ms\n\n";

            recorder.record({phase, "LOAD", soa.size(), 1,
                             direct_timing, soa.size(), 0.0});

            // Compute time range directly from SoA pickup_timestamp array
            std::int64_t min_ts = soa.pickup_timestamp[0];
            std::int64_t max_ts = soa.pickup_timestamp[0];
            for (auto t : soa.pickup_timestamp) {
                if (t < min_ts) min_ts = t;
                if (t > max_ts) max_ts = t;
            }
            const std::int64_t mid_ts = min_ts + (max_ts - min_ts) / 2;
            const std::size_t  dataset_size = soa.size();

            std::cout << "[Index] Building SoA time index...\n";
            SoAQueryEngine soa_engine(soa);
            double idx_ms = soa_engine.build_indexes();
            std::cout << std::fixed << std::setprecision(2)
                      << "  Index build time: " << idx_ms << " ms\n\n";

            for (const auto& qid : active_queries) {
                std::cout << "[" << qid << "] Running " << num_runs
                          << " iterations (SoA direct)...\n";

                RunStats    timing;
                std::size_t matches = 0;
                double      extra   = 0.0;

                if (qid == "Q1") {
                    TimeRangeQuery q{min_ts, mid_ts};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_time(q);
                    }, num_runs);
                    matches = last.indices.size();
                } else if (qid == "Q2") {
                    NumericRangeQuery q{1.0, 5.0};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_distance(q);
                    }, num_runs);
                    matches = last.indices.size();
                } else if (qid == "Q3") {
                    NumericRangeQuery q{10.0, 50.0};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_fare(q);
                    }, num_runs);
                    matches = last.indices.size();
                } else if (qid == "Q4") {
                    IntRangeQuery q{100, 200};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_location(q);
                    }, num_runs);
                    matches = last.indices.size();
                } else if (qid == "Q5") {
                    CombinedQuery q{{min_ts, mid_ts}, {0.0, 100.0}, {1, 6}};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_combined(q);
                    }, num_runs);
                    matches = last.indices.size();
                } else if (qid == "Q6") {
                    TimeRangeQuery q{min_ts, max_ts};
                    AggregationResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.aggregate_fare_by_time(q);
                    }, num_runs);
                    matches = last.count;
                    extra   = last.avg;
                }

                std::cout << std::fixed << std::setprecision(3)
                          << "  avg " << timing.avg_ms
                          << " ms  ±" << timing.stddev_ms
                          << "  min " << timing.min_ms
                          << "  max " << timing.max_ms
                          << "  matches " << matches;
                if (qid == "Q6")
                    std::cout << "  avg_fare $" << std::setprecision(2) << extra;
                std::cout << "\n\n";

                recorder.record({phase, qid, dataset_size,
                                 omp_threads, timing, matches, extra});
            }

            const auto   phase_wall_end  = std::chrono::steady_clock::now();
            const double phase_total_ms  = std::chrono::duration<double, std::milli>(
                phase_wall_end - phase_wall_start).count();
            {
                RunStats ps;
                ps.avg_ms = ps.min_ms = ps.max_ms = phase_total_ms;
                ps.stddev_ms = 0.0; ps.runs = 1;
                recorder.record({phase, "TOTAL_PHASE",
                                 dataset_size, omp_threads, ps, 0, 0.0});
            }
            std::cout << std::fixed << std::setprecision(1)
                      << "\n=== Total Phase Wall-Clock Time ===\n"
                      << "  " << phase_total_ms << " ms"
                      << "  (" << (phase_total_ms / 60000.0) << " min)\n";
            std::cout << "\n=== Summary ===\n";
            recorder.print_summary();
            if (!output_path.empty()) {
                recorder.write_csv(output_path);
                std::cout << "\nMetrics written to: " << output_path << "\n";
            }
            std::cout << "\nDone.\n";
            return 0;
        }

        std::vector<TripRecord> records;

        // ================================================================
        // LOAD PHASE
        // ================================================================
        if (use_parallel_load) {
            // ---- Phase 2: parallel load ----
            std::cout << "[Load] Parallel CSV load (" << load_threads
                      << " threads, " << csv_paths.size() << " file(s))...\n";

            // Time loading all CSV files over num_runs iterations.
            // Each run clears and reloads all files so timing is consistent.
            // Pre-reserve to avoid reallocation OOM spikes (e.g. 140M × 128 B ≈ 17.9 GB).
            // macOS treats this as virtual memory — physical pages are faulted in lazily,
            // allowing NVMe-backed swap to absorb datasets larger than physical RAM.
            ParallelLoader::Result par_result;
            par_result.records.reserve(95000000);
            RunStats par_timing = BenchmarkRunner::time_n([&]() {
                par_result.records.clear();
                par_result.total_rows_read = par_result.total_rows_parsed
                                           = par_result.total_rows_discarded = 0;
                for (const auto& p : csv_paths) {
                    auto chunk = ParallelLoader::load(p, load_threads);
                    par_result.total_rows_read      += chunk.total_rows_read;
                    par_result.total_rows_parsed    += chunk.total_rows_parsed;
                    par_result.total_rows_discarded += chunk.total_rows_discarded;
                    par_result.records.insert(par_result.records.end(),
                        std::make_move_iterator(chunk.records.begin()),
                        std::make_move_iterator(chunk.records.end()));
                }
            }, num_runs);

            if (par_result.records.empty()) {
                std::cerr << "ERROR: no records loaded (parallel).\n";
                return 1;
            }
            records = std::move(par_result.records);

            double success_rate = par_result.total_rows_read > 0
                ? 100.0 * par_result.total_rows_parsed / par_result.total_rows_read
                : 0.0;

            std::cout << std::fixed << std::setprecision(2)
                      << "  Records loaded : " << records.size() << "\n"
                      << "  Rows read      : " << par_result.total_rows_read << "\n"
                      << "  Parse success  : " << success_rate << "%\n"
                      << "  avg " << par_timing.avg_ms
                      << " ms  ±" << par_timing.stddev_ms
                      << "  min " << par_timing.min_ms
                      << "  max " << par_timing.max_ms << " ms\n\n";

            recorder.record({"Phase2_parallel", "LOAD",
                             records.size(), load_threads,
                             par_timing, records.size(), 0.0});

        } else {
            // ---- Phase 1: serial load (baseline) ----
            std::cout << "[Load] Serial CSV load (1 thread, "
                      << csv_paths.size() << " file(s))...\n";

            DatasetManager mgr;
            // Pre-reserve to avoid reallocation OOM spikes on multi-file loads.
            mgr.reserve_if_needed(95000000);
            RunStats ser_timing = BenchmarkRunner::time_n([&]() {
                mgr.clear();
                for (const auto& p : csv_paths)
                    mgr.load_from_csv(p);   // DatasetManager appends on each call
            }, num_runs);

            if (mgr.size() == 0) {
                std::cerr << "ERROR: no records loaded.\n";
                return 1;
            }
            records = mgr.take_records();   // move (not copy) to avoid doubling ~17 GB

            auto ls = mgr.get_load_stats();
            double success_rate = ls.total_rows_read > 0
                ? 100.0 * ls.total_rows_parsed / ls.total_rows_read
                : 0.0;

            std::cout << std::fixed << std::setprecision(2)
                      << "  Records loaded : " << records.size() << "\n"
                      << "  Rows read      : " << ls.total_rows_read << "\n"
                      << "  Parse success  : " << success_rate << "%\n"
                      << "  avg " << ser_timing.avg_ms
                      << " ms  ±" << ser_timing.stddev_ms
                      << "  min " << ser_timing.min_ms
                      << "  max " << ser_timing.max_ms << " ms\n\n";

            recorder.record({"Phase1_serial", "LOAD",
                             records.size(), 1,
                             ser_timing, records.size(), 0.0});
        }

        // ================================================================
        // QUERY PHASE
        // ================================================================

        // Derive time range parameters from actual data
        std::int64_t min_ts = records[0].pickup_timestamp;
        std::int64_t max_ts = records[0].pickup_timestamp;
        for (const auto& r : records) {
            if (r.pickup_timestamp < min_ts) min_ts = r.pickup_timestamp;
            if (r.pickup_timestamp > max_ts) max_ts = r.pickup_timestamp;
        }
        std::int64_t mid_ts = min_ts + (max_ts - min_ts) / 2;
        // Save dataset size here — in SoA mode we free records after conversion
        // to reclaim ~4.8 GB before queries run.
        const std::size_t dataset_size = records.size();

        if (soa_mode) {
            // ================================================================
            // Phase 3: Object-of-Arrays (SoA) queries
            // ================================================================
            std::cout << "[SoA] Converting AoS -> SoA layout...\n";
            double conv_start_ms = []() {
                return std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            }();
            TripDataSoA soa = TripDataSoA::from_aos(records);
            // Free the AoS vector immediately — it has been fully converted to SoA.
            // Without this, AoS (~4.8 GB) and SoA (~4.2 GB) coexist, causing OOM.
            { std::vector<TripRecord>().swap(records); }
            double conv_end_ms = []() {
                return std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
            }();
            std::cout << std::fixed << std::setprecision(2)
                      << "  SoA arrays populated: " << soa.size() << " rows  ("
                      << (conv_end_ms - conv_start_ms) << " ms)\n\n";

            std::cout << "[Index] Building SoA time index...\n";
            SoAQueryEngine soa_engine(soa);
            double idx_ms = soa_engine.build_indexes();
            std::cout << std::fixed << std::setprecision(2)
                      << "  Index build time: " << idx_ms << " ms\n\n";

            for (const auto& qid : active_queries) {
                std::cout << "[" << qid << "] Running " << num_runs
                          << " iterations (SoA)...\n";

                RunStats    timing;
                std::size_t matches = 0;
                double      extra   = 0.0;

                if (qid == "Q1") {
                    TimeRangeQuery q{min_ts, mid_ts};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_time(q);
                    }, num_runs);
                    matches = last.indices.size();

                } else if (qid == "Q2") {
                    NumericRangeQuery q{1.0, 5.0};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_distance(q);
                    }, num_runs);
                    matches = last.indices.size();

                } else if (qid == "Q3") {
                    NumericRangeQuery q{10.0, 50.0};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_fare(q);
                    }, num_runs);
                    matches = last.indices.size();

                } else if (qid == "Q4") {
                    IntRangeQuery q{100, 200};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_by_location(q);
                    }, num_runs);
                    matches = last.indices.size();

                } else if (qid == "Q5") {
                    CombinedQuery q{{min_ts, mid_ts}, {0.0, 100.0}, {1, 6}};
                    SoAQueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.search_combined(q);
                    }, num_runs);
                    matches = last.indices.size();

                } else if (qid == "Q6") {
                    TimeRangeQuery q{min_ts, max_ts};
                    AggregationResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = soa_engine.aggregate_fare_by_time(q);
                    }, num_runs);
                    matches = last.count;
                    extra   = last.avg;
                }

                std::cout << std::fixed << std::setprecision(3)
                          << "  avg " << timing.avg_ms
                          << " ms  ±" << timing.stddev_ms
                          << "  min " << timing.min_ms
                          << "  max " << timing.max_ms
                          << "  matches " << matches;
                if (qid == "Q6")
                    std::cout << "  avg_fare $" << std::setprecision(2) << extra;
                std::cout << "\n\n";

                recorder.record({phase, qid, soa.size(),
                                 omp_threads, timing, matches, extra});
            }

        } else {
            // ================================================================
            // Phase 1/2: Array-of-Structs (AoS) queries via QueryEngine
            // ================================================================
            std::cout << "[Index] Building time index...\n";
            QueryEngine engine(records);
            double idx_ms = engine.build_indexes();
            std::cout << std::fixed << std::setprecision(2)
                      << "  Index build time: " << idx_ms << " ms\n\n";

            for (const auto& qid : active_queries) {
                std::cout << "[" << qid << "] Running " << num_runs
                          << " iterations...\n";

                RunStats    timing;
                std::size_t matches = 0;
                double      extra   = 0.0;

                if (qid == "Q1") {
                    TimeRangeQuery q{min_ts, mid_ts};
                    QueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = engine.search_by_time(q);
                    }, num_runs);
                    matches = last.records.size();

                } else if (qid == "Q2") {
                    NumericRangeQuery q{1.0, 5.0};
                    QueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = engine.search_by_distance(q);
                    }, num_runs);
                    matches = last.records.size();

                } else if (qid == "Q3") {
                    NumericRangeQuery q{10.0, 50.0};
                    QueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = engine.search_by_fare(q);
                    }, num_runs);
                    matches = last.records.size();

                } else if (qid == "Q4") {
                    IntRangeQuery q{100, 200};
                    QueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = engine.search_by_location(q);
                    }, num_runs);
                    matches = last.records.size();

                } else if (qid == "Q5") {
                    CombinedQuery q{{min_ts, mid_ts}, {0.0, 100.0}, {1, 6}};
                    QueryResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = engine.search_combined(q);
                    }, num_runs);
                    matches = last.records.size();

                } else if (qid == "Q6") {
                    TimeRangeQuery q{min_ts, max_ts};
                    AggregationResult last;
                    timing = BenchmarkRunner::time_n([&]() {
                        last = engine.aggregate_fare_by_time(q);
                    }, num_runs);
                    matches = last.count;
                    extra   = last.avg;
                }

                std::cout << std::fixed << std::setprecision(3)
                          << "  avg " << timing.avg_ms
                          << " ms  ±" << timing.stddev_ms
                          << "  min " << timing.min_ms
                          << "  max " << timing.max_ms
                          << "  matches " << matches;
                if (qid == "Q6")
                    std::cout << "  avg_fare $" << std::setprecision(2) << extra;
                std::cout << "\n\n";

                recorder.record({phase, qid, records.size(),
                                 omp_threads, timing, matches, extra});
            }
        }

        // ---- Total phase wall-clock time ----
        const auto phase_wall_end = std::chrono::steady_clock::now();
        const double phase_total_ms = std::chrono::duration<double, std::milli>(
            phase_wall_end - phase_wall_start).count();

        // Record as TOTAL_PHASE row so it appears in the output CSV.
        // avg_ms = stddev_ms = min_ms = max_ms = total elapsed (runs=1).
        {
            RunStats ps;
            ps.avg_ms = ps.min_ms = ps.max_ms = phase_total_ms;
            ps.stddev_ms = 0.0;
            ps.runs = 1;
            recorder.record({phase, "TOTAL_PHASE",
                             dataset_size, omp_threads,
                             ps, 0, 0.0});
        }

        std::cout << std::fixed << std::setprecision(1)
                  << "\n=== Total Phase Wall-Clock Time ===\n"
                  << "  " << phase_total_ms << " ms"
                  << "  (" << (phase_total_ms / 60000.0) << " min)\n";

        // ---- Summary table ----
        std::cout << "\n=== Summary ===\n";
        recorder.print_summary();

        // ---- Write CSV ----
        if (!output_path.empty()) {
            recorder.write_csv(output_path);
            std::cout << "\nMetrics written to: " << output_path << "\n";
        }

        std::cout << "\nDone.\n";

    } catch (const std::exception& e) {
        std::cerr << "\nERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
