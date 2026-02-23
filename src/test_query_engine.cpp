#include "taxi/DatasetManager.hpp"
#include "taxi/QueryEngine.hpp"
#include "taxi/QueryTypes.hpp"
#include <iostream>
#include <iomanip>
#include <cstdlib>

using namespace taxi;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file>\n";
        return 1;
    }

    DatasetManager mgr;
    mgr.load_from_csv(argv[1]);
    std::cout << "Loaded " << mgr.size() << " records\n\n";

    if (mgr.size() == 0) {
        std::cerr << "No records loaded.\n";
        return 1;
    }

    QueryEngine engine(mgr.records());
    double build_ms = engine.build_indexes();
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Index build: " << build_ms << " ms\n\n";

    // Use wide ranges to guarantee matches on any dataset
    const auto& recs = mgr.records();
    std::int64_t min_ts = recs[0].pickup_timestamp;
    std::int64_t max_ts = recs[0].pickup_timestamp;
    for (const auto& r : recs) {
        if (r.pickup_timestamp < min_ts) min_ts = r.pickup_timestamp;
        if (r.pickup_timestamp > max_ts) max_ts = r.pickup_timestamp;
    }
    std::int64_t mid_ts = min_ts + (max_ts - min_ts) / 2;

    int pass = 0;
    int fail = 0;

    auto check = [&](const char* name, bool ok, std::size_t matches, std::size_t scanned) {
        std::cout << (ok ? "PASS" : "FAIL") << "  " << name
                  << "  matches=" << matches << "  scanned=" << scanned << "\n";
        if (ok) ++pass; else ++fail;
    };

    // Q1: Time range — first half of the dataset's time span
    {
        TimeRangeQuery q{min_ts, mid_ts};
        auto r = engine.search_by_time(q);
        check("Q1 search_by_time", r.records.size() > 0, r.records.size(), r.scanned);
    }

    // Q2: Distance [1.0, 5.0]
    {
        NumericRangeQuery q{1.0, 5.0};
        auto r = engine.search_by_distance(q);
        check("Q2 search_by_distance", r.records.size() > 0, r.records.size(), r.scanned);
    }

    // Q3: Fare [10.0, 50.0]
    {
        NumericRangeQuery q{10.0, 50.0};
        auto r = engine.search_by_fare(q);
        check("Q3 search_by_fare", r.records.size() > 0, r.records.size(), r.scanned);
    }

    // Q4: Location [100, 200]
    {
        IntRangeQuery q{100, 200};
        auto r = engine.search_by_location(q);
        check("Q4 search_by_location", r.records.size() > 0, r.records.size(), r.scanned);
    }

    // Q5: Combined — first half time + distance [0, 100] + passengers [1, 6]
    {
        CombinedQuery q{{min_ts, mid_ts}, {0.0, 100.0}, {1, 6}};
        auto r = engine.search_combined(q);
        check("Q5 search_combined", r.records.size() > 0, r.records.size(), r.scanned);
    }

    // Q6: Aggregation — full time range
    {
        TimeRangeQuery q{min_ts, max_ts};
        auto r = engine.aggregate_fare_by_time(q);
        bool ok = r.count > 0 && r.sum > 0.0 && r.avg > 0.0;
        std::cout << (ok ? "PASS" : "FAIL")
                  << "  Q6 aggregate_fare_by_time"
                  << "  count=" << r.count
                  << "  sum=" << r.sum
                  << "  avg=" << r.avg << "\n";
        if (ok) ++pass; else ++fail;
    }

    std::cout << "\n" << pass << " passed, " << fail << " failed\n";
    return fail > 0 ? 1 : 0;
}
