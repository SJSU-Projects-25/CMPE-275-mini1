/**
 * unit_tests.cpp — Basic unit tests for CMPE-275 Mini 1.
 *
 * No external test framework — uses assert() and reports pass/fail counts.
 * Tests cover: TripRecord, CsvReader, TripDataSoA, BenchmarkRunner,
 *              QueryEngine, and SoAQueryEngine.
 *
 * Build:  cmake --build build --target unit_tests
 * Run:    ./build/bin/unit_tests
 */

#include "taxi/TripRecord.hpp"
#include "taxi/CsvReader.hpp"
#include "taxi/TripDataSoA.hpp"
#include "taxi/BenchmarkRunner.hpp"
#include "taxi/QueryEngine.hpp"
#include "taxi/SoAQueryEngine.hpp"
#include "taxi/QueryTypes.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

static int passed = 0;
static int failed = 0;

#define RUN_TEST(fn) do { \
    try { \
        fn(); \
        ++passed; \
        std::cout << "  PASS  " << #fn << "\n"; \
    } catch (const std::exception& e) { \
        ++failed; \
        std::cerr << "  FAIL  " << #fn << ": " << e.what() << "\n"; \
    } catch (...) { \
        ++failed; \
        std::cerr << "  FAIL  " << #fn << ": unknown exception\n"; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) throw std::runtime_error("assertion failed: " #cond); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) throw std::runtime_error( \
        "assertion failed: " #a " == " #b); \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (std::abs((a) - (b)) > (eps)) throw std::runtime_error( \
        "assertion failed: " #a " near " #b); \
} while(0)

// ── helpers ──────────────────────────────────────────────────────────────────

static taxi::TripRecord make_record(int vendor, int64_t pickup, int64_t dropoff,
                                     int passengers, double distance, double fare,
                                     int pu_loc, int do_loc, double total) {
    taxi::TripRecord r{};
    r.vendor_id = vendor;
    r.pickup_timestamp = pickup;
    r.dropoff_timestamp = dropoff;
    r.passenger_count = passengers;
    r.trip_distance = distance;
    r.rate_code_id = 1;
    r.store_and_fwd_flag = false;
    r.pu_location_id = pu_loc;
    r.do_location_id = do_loc;
    r.payment_type = 1;
    r.fare_amount = fare;
    r.extra = 0.0;
    r.mta_tax = 0.5;
    r.tip_amount = 2.0;
    r.tolls_amount = 0.0;
    r.improvement_surcharge = 0.3;
    r.total_amount = total;
    return r;
}

static std::string write_temp_csv(const std::string& content) {
    auto path = std::filesystem::temp_directory_path() / "taxi_unit_test.csv";
    std::ofstream f(path);
    f << content;
    f.close();
    return path.string();
}

// ── TripRecord tests ─────────────────────────────────────────────────────────

void test_valid_record() {
    auto r = make_record(1, 1000, 2000, 2, 3.5, 15.0, 100, 200, 20.0);
    ASSERT_TRUE(r.is_valid());
}

void test_invalid_record_negative_timestamp() {
    auto r = make_record(1, -1, 2000, 2, 3.5, 15.0, 100, 200, 20.0);
    ASSERT_TRUE(!r.is_valid());
}

void test_invalid_record_dropoff_before_pickup() {
    auto r = make_record(1, 2000, 1000, 2, 3.5, 15.0, 100, 200, 20.0);
    ASSERT_TRUE(!r.is_valid());
}

void test_invalid_record_negative_distance() {
    auto r = make_record(1, 1000, 2000, 2, -1.0, 15.0, 100, 200, 20.0);
    ASSERT_TRUE(!r.is_valid());
}

void test_invalid_record_negative_total() {
    auto r = make_record(1, 1000, 2000, 2, 3.5, 15.0, 100, 200, -5.0);
    ASSERT_TRUE(!r.is_valid());
}

void test_record_zero_passengers_valid() {
    auto r = make_record(1, 1000, 2000, 0, 3.5, 15.0, 100, 200, 20.0);
    ASSERT_TRUE(r.is_valid());
}

// ── CsvReader tests ──────────────────────────────────────────────────────────

void test_csv_reader_parse_valid_row() {
    // Use MM/DD/YYYY format (matches actual TLC data format)
    std::string csv =
        "VendorID,tpep_pickup_datetime,tpep_dropoff_datetime,passenger_count,"
        "trip_distance,RatecodeID,store_and_fwd_flag,PULocationID,DOLocationID,"
        "payment_type,fare_amount,extra,mta_tax,tip_amount,tolls_amount,"
        "improvement_surcharge,total_amount\n"
        "1,01/15/2021 10:30:00 AM,01/15/2021 10:45:00 AM,2,3.5,1,N,100,200,1,15.00,"
        "0.50,0.50,2.00,0.00,0.30,18.30\n";

    auto path = write_temp_csv(csv);
    taxi::CsvReader reader(path);
    ASSERT_TRUE(reader.is_open());

    taxi::TripRecord rec{};
    bool ok = reader.read_next(rec);
    ASSERT_TRUE(ok);
    ASSERT_EQ(rec.vendor_id, 1);
    ASSERT_EQ(rec.passenger_count, 2);
    ASSERT_NEAR(rec.trip_distance, 3.5, 0.001);
    ASSERT_NEAR(rec.fare_amount, 15.0, 0.001);
    ASSERT_EQ(rec.pu_location_id, 100);
    ASSERT_EQ(rec.do_location_id, 200);
    ASSERT_NEAR(rec.total_amount, 18.30, 0.001);

    std::filesystem::remove(path);
}

void test_csv_reader_eof() {
    std::string csv =
        "VendorID,tpep_pickup_datetime,tpep_dropoff_datetime,passenger_count,"
        "trip_distance,RatecodeID,store_and_fwd_flag,PULocationID,DOLocationID,"
        "payment_type,fare_amount,extra,mta_tax,tip_amount,tolls_amount,"
        "improvement_surcharge,total_amount\n"
        "1,01/15/2021 10:30:00 AM,01/15/2021 10:45:00 AM,2,3.5,1,N,100,200,1,15.00,"
        "0.50,0.50,2.00,0.00,0.30,18.30\n";

    auto path = write_temp_csv(csv);
    taxi::CsvReader reader(path);
    taxi::TripRecord rec{};
    reader.read_next(rec);  // read the one row
    bool more = reader.read_next(rec);
    ASSERT_TRUE(!more);  // should be EOF

    auto stats = reader.get_stats();
    ASSERT_EQ(stats.rows_parsed_ok, 1u);

    std::filesystem::remove(path);
}

void test_csv_reader_nonexistent_file() {
    // CsvReader throws on missing file
    bool threw = false;
    try {
        taxi::CsvReader reader("/tmp/this_file_does_not_exist_12345.csv");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    ASSERT_TRUE(threw);
}

void test_csv_reader_multiple_rows() {
    std::string csv =
        "VendorID,tpep_pickup_datetime,tpep_dropoff_datetime,passenger_count,"
        "trip_distance,RatecodeID,store_and_fwd_flag,PULocationID,DOLocationID,"
        "payment_type,fare_amount,extra,mta_tax,tip_amount,tolls_amount,"
        "improvement_surcharge,total_amount\n"
        "1,01/01/2021 08:00:00 AM,01/01/2021 08:15:00 AM,1,2.0,1,N,50,75,1,10.00,"
        "0.00,0.50,1.00,0.00,0.30,11.80\n"
        "2,06/15/2021 02:00:00 PM,06/15/2021 02:30:00 PM,3,5.0,1,N,150,250,2,25.00,"
        "1.00,0.50,0.00,0.00,0.30,26.80\n"
        "1,03/20/2022 10:00:00 PM,03/20/2022 10:20:00 PM,1,1.5,1,Y,130,180,1,8.00,"
        "0.50,0.50,2.50,0.00,0.30,11.80\n";

    auto path = write_temp_csv(csv);
    taxi::CsvReader reader(path);

    int count = 0;
    taxi::TripRecord rec{};
    while (reader.read_next(rec)) ++count;
    ASSERT_EQ(count, 3);

    auto stats = reader.get_stats();
    ASSERT_EQ(stats.rows_parsed_ok, 3u);

    std::filesystem::remove(path);
}

// ── TripDataSoA tests ────────────────────────────────────────────────────────

void test_soa_from_aos_size() {
    std::vector<taxi::TripRecord> records;
    records.push_back(make_record(1, 1000, 2000, 2, 3.5, 15.0, 100, 200, 20.0));
    records.push_back(make_record(2, 3000, 4000, 1, 5.0, 25.0, 150, 250, 30.0));

    auto soa = taxi::TripDataSoA::from_aos(records);
    ASSERT_EQ(soa.size(), 2u);
}

void test_soa_from_aos_field_values() {
    std::vector<taxi::TripRecord> records;
    records.push_back(make_record(1, 1000, 2000, 2, 3.5, 15.0, 100, 200, 20.0));
    records.push_back(make_record(2, 3000, 4000, 1, 5.0, 25.0, 150, 250, 30.0));

    auto soa = taxi::TripDataSoA::from_aos(records);

    ASSERT_EQ(soa.vendor_id[0], 1);
    ASSERT_EQ(soa.vendor_id[1], 2);
    ASSERT_NEAR(soa.trip_distance[0], 3.5, 0.001);
    ASSERT_NEAR(soa.trip_distance[1], 5.0, 0.001);
    ASSERT_NEAR(soa.fare_amount[0], 15.0, 0.001);
    ASSERT_NEAR(soa.fare_amount[1], 25.0, 0.001);
    ASSERT_EQ(soa.pu_location_id[0], 100);
    ASSERT_EQ(soa.pu_location_id[1], 150);
    ASSERT_EQ(soa.pickup_timestamp[0], 1000);
    ASSERT_EQ(soa.pickup_timestamp[1], 3000);
}

void test_soa_from_aos_empty() {
    std::vector<taxi::TripRecord> empty;
    auto soa = taxi::TripDataSoA::from_aos(empty);
    ASSERT_EQ(soa.size(), 0u);
}

void test_soa_from_csv() {
    std::string csv =
        "VendorID,tpep_pickup_datetime,tpep_dropoff_datetime,passenger_count,"
        "trip_distance,RatecodeID,store_and_fwd_flag,PULocationID,DOLocationID,"
        "payment_type,fare_amount,extra,mta_tax,tip_amount,tolls_amount,"
        "improvement_surcharge,total_amount\n"
        "1,01/15/2021 10:30:00 AM,01/15/2021 10:45:00 AM,2,3.5,1,N,100,200,1,15.00,"
        "0.50,0.50,2.00,0.00,0.30,18.30\n"
        "2,06/15/2021 02:00:00 PM,06/15/2021 02:30:00 PM,3,5.0,1,N,150,250,2,25.00,"
        "1.00,0.50,0.00,0.00,0.30,26.80\n";

    auto path = write_temp_csv(csv);
    auto soa = taxi::TripDataSoA::from_csv({path}, 0);
    ASSERT_EQ(soa.size(), 2u);
    ASSERT_NEAR(soa.trip_distance[0], 3.5, 0.001);
    ASSERT_NEAR(soa.trip_distance[1], 5.0, 0.001);

    std::filesystem::remove(path);
}

// ── BenchmarkRunner tests ────────────────────────────────────────────────────

void test_benchmark_runner_basic() {
    int counter = 0;
    auto stats = taxi::BenchmarkRunner::time_n([&]{ ++counter; }, 5);
    ASSERT_EQ(stats.runs, 5);
    ASSERT_EQ(counter, 5);
    ASSERT_TRUE(stats.avg_ms >= 0.0);
    ASSERT_TRUE(stats.min_ms <= stats.avg_ms);
    ASSERT_TRUE(stats.max_ms >= stats.avg_ms);
}

void test_benchmark_runner_single_run() {
    auto stats = taxi::BenchmarkRunner::time_n([]{ volatile int x = 42; (void)x; }, 1);
    ASSERT_EQ(stats.runs, 1);
    ASSERT_NEAR(stats.stddev_ms, 0.0, 0.001);
}

void test_benchmark_runner_zero_runs() {
    auto stats = taxi::BenchmarkRunner::time_n([]{ }, 0);
    ASSERT_EQ(stats.runs, 0);
}

// ── QueryEngine tests (AoS) ─────────────────────────────────────────────────

static std::vector<taxi::TripRecord> make_test_dataset() {
    // Timestamps: 2021-01-15 = 1610668800 epoch
    //             2021-01-16 = 1610755200 epoch
    //             2021-06-15 = 1623715200 epoch
    std::vector<taxi::TripRecord> data;
    data.push_back(make_record(1, 1610668800, 1610669700, 2, 3.5, 15.0, 100, 200, 20.0));
    data.push_back(make_record(2, 1610755200, 1610756100, 1, 1.0, 8.0,  150, 250, 10.0));
    data.push_back(make_record(1, 1623715200, 1623716100, 3, 7.0, 30.0, 180, 300, 35.0));
    data.push_back(make_record(2, 1610668800, 1610670600, 1, 2.5, 12.0, 120, 220, 15.0));
    data.push_back(make_record(1, 1610755200, 1610757000, 4, 4.0, 20.0, 200, 350, 25.0));
    return data;
}

void test_query_distance_range() {
    auto data = make_test_dataset();
    taxi::QueryEngine engine(data);
    engine.build_indexes();

    taxi::NumericRangeQuery q{1.0, 4.0};  // distance 1-4 miles
    auto result = engine.search_by_distance(q);
    // Records with distance: 3.5, 1.0, 2.5, 4.0 -> 4 matches (7.0 excluded)
    ASSERT_EQ(result.records.size(), 4u);
    ASSERT_EQ(result.scanned, 5u);
}

void test_query_fare_range() {
    auto data = make_test_dataset();
    taxi::QueryEngine engine(data);
    engine.build_indexes();

    taxi::NumericRangeQuery q{10.0, 20.0};  // fare $10-$20
    auto result = engine.search_by_fare(q);
    // Fares: 15, 8, 30, 12, 20 -> 15, 12, 20 match = 3
    ASSERT_EQ(result.records.size(), 3u);
}

void test_query_location_range() {
    auto data = make_test_dataset();
    taxi::QueryEngine engine(data);
    engine.build_indexes();

    taxi::IntRangeQuery q{100, 150};  // PU location 100-150
    auto result = engine.search_by_location(q);
    // PU locations: 100, 150, 180, 120, 200 -> 100, 150, 120 match = 3
    ASSERT_EQ(result.records.size(), 3u);
}

void test_query_aggregate_fare() {
    auto data = make_test_dataset();
    taxi::QueryEngine engine(data);
    engine.build_indexes();

    // Time range covering all records
    taxi::TimeRangeQuery q{1610000000, 1625000000};
    auto agg = engine.aggregate_fare_by_time(q);
    ASSERT_EQ(agg.count, 5u);
    double expected_avg = (15.0 + 8.0 + 30.0 + 12.0 + 20.0) / 5.0;
    ASSERT_NEAR(agg.avg, expected_avg, 0.001);
}

// ── SoAQueryEngine tests ────────────────────────────────────────────────────

void test_soa_query_distance_range() {
    auto data = make_test_dataset();
    auto soa = taxi::TripDataSoA::from_aos(data);
    taxi::SoAQueryEngine engine(soa);
    engine.build_indexes();

    taxi::NumericRangeQuery q{1.0, 4.0};
    auto result = engine.search_by_distance(q);
    ASSERT_EQ(result.indices.size(), 4u);
    ASSERT_EQ(result.scanned, 5u);
}

void test_soa_query_fare_range() {
    auto data = make_test_dataset();
    auto soa = taxi::TripDataSoA::from_aos(data);
    taxi::SoAQueryEngine engine(soa);
    engine.build_indexes();

    taxi::NumericRangeQuery q{10.0, 20.0};
    auto result = engine.search_by_fare(q);
    ASSERT_EQ(result.indices.size(), 3u);
}

void test_soa_query_location_range() {
    auto data = make_test_dataset();
    auto soa = taxi::TripDataSoA::from_aos(data);
    taxi::SoAQueryEngine engine(soa);
    engine.build_indexes();

    taxi::IntRangeQuery q{100, 150};
    auto result = engine.search_by_location(q);
    ASSERT_EQ(result.indices.size(), 3u);
}

void test_soa_query_aggregate_fare() {
    auto data = make_test_dataset();
    auto soa = taxi::TripDataSoA::from_aos(data);
    taxi::SoAQueryEngine engine(soa);
    engine.build_indexes();

    taxi::TimeRangeQuery q{1610000000, 1625000000};
    auto agg = engine.aggregate_fare_by_time(q);
    ASSERT_EQ(agg.count, 5u);
    double expected_avg = (15.0 + 8.0 + 30.0 + 12.0 + 20.0) / 5.0;
    ASSERT_NEAR(agg.avg, expected_avg, 0.001);
}

void test_aos_soa_query_consistency() {
    auto data = make_test_dataset();
    auto soa = taxi::TripDataSoA::from_aos(data);

    taxi::QueryEngine aos_engine(data);
    aos_engine.build_indexes();
    taxi::SoAQueryEngine soa_engine(soa);
    soa_engine.build_indexes();

    // Distance query should return same count
    taxi::NumericRangeQuery dq{1.0, 5.0};
    auto aos_res = aos_engine.search_by_distance(dq);
    auto soa_res = soa_engine.search_by_distance(dq);
    ASSERT_EQ(aos_res.records.size(), soa_res.indices.size());

    // Fare query should return same count
    taxi::NumericRangeQuery fq{10.0, 50.0};
    auto aos_fare = aos_engine.search_by_fare(fq);
    auto soa_fare = soa_engine.search_by_fare(fq);
    ASSERT_EQ(aos_fare.records.size(), soa_fare.indices.size());

    // Aggregation should match
    taxi::TimeRangeQuery tq{1610000000, 1625000000};
    auto aos_agg = aos_engine.aggregate_fare_by_time(tq);
    auto soa_agg = soa_engine.aggregate_fare_by_time(tq);
    ASSERT_EQ(aos_agg.count, soa_agg.count);
    ASSERT_NEAR(aos_agg.avg, soa_agg.avg, 0.001);
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n=== CMPE-275 Mini 1 Unit Tests ===\n\n";

    std::cout << "-- TripRecord --\n";
    RUN_TEST(test_valid_record);
    RUN_TEST(test_invalid_record_negative_timestamp);
    RUN_TEST(test_invalid_record_dropoff_before_pickup);
    RUN_TEST(test_invalid_record_negative_distance);
    RUN_TEST(test_invalid_record_negative_total);
    RUN_TEST(test_record_zero_passengers_valid);

    std::cout << "\n-- CsvReader --\n";
    RUN_TEST(test_csv_reader_parse_valid_row);
    RUN_TEST(test_csv_reader_eof);
    RUN_TEST(test_csv_reader_nonexistent_file);
    RUN_TEST(test_csv_reader_multiple_rows);

    std::cout << "\n-- TripDataSoA --\n";
    RUN_TEST(test_soa_from_aos_size);
    RUN_TEST(test_soa_from_aos_field_values);
    RUN_TEST(test_soa_from_aos_empty);
    RUN_TEST(test_soa_from_csv);

    std::cout << "\n-- BenchmarkRunner --\n";
    RUN_TEST(test_benchmark_runner_basic);
    RUN_TEST(test_benchmark_runner_single_run);
    RUN_TEST(test_benchmark_runner_zero_runs);

    std::cout << "\n-- QueryEngine (AoS) --\n";
    RUN_TEST(test_query_distance_range);
    RUN_TEST(test_query_fare_range);
    RUN_TEST(test_query_location_range);
    RUN_TEST(test_query_aggregate_fare);

    std::cout << "\n-- SoAQueryEngine --\n";
    RUN_TEST(test_soa_query_distance_range);
    RUN_TEST(test_soa_query_fare_range);
    RUN_TEST(test_soa_query_location_range);
    RUN_TEST(test_soa_query_aggregate_fare);
    RUN_TEST(test_aos_soa_query_consistency);

    std::cout << "\n=================================\n";
    std::cout << "  Total: " << (passed + failed)
              << "  Passed: " << passed
              << "  Failed: " << failed << "\n";
    std::cout << "=================================\n\n";

    return failed > 0 ? 1 : 0;
}
