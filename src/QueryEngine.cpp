#include "taxi/QueryEngine.hpp"
#include <chrono>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace taxi {

QueryEngine::QueryEngine(const std::vector<TripRecord>& data)
    : data_(data) {}

double QueryEngine::build_indexes() {
    auto start = std::chrono::steady_clock::now();
    time_index_.build(data_);
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Query 1: Time range — uses TimeIndex for O(log N) lookup
QueryResult QueryEngine::search_by_time(const TimeRangeQuery& q) const {
    QueryResult result;

    if (time_index_.is_built()) {
        auto [lo, hi] = time_index_.lookup(data_, q.start_time, q.end_time);
        const auto& idx = time_index_.sorted_indices();
        result.scanned = hi - lo;
        result.records.reserve(hi - lo);

        for (std::size_t i = lo; i < hi; ++i) {
            result.records.push_back(&data_[idx[i]]);
        }
    } else {
        result.scanned = data_.size();
        result.records.reserve(data_.size() / 10);
        for (const auto& rec : data_) {
            if (rec.pickup_timestamp >= q.start_time &&
                rec.pickup_timestamp <= q.end_time) {
                result.records.push_back(&rec);
            }
        }
    }

    return result;
}

// Query 2: Distance range — linear scan
QueryResult QueryEngine::search_by_distance(const NumericRangeQuery& q) const {
    QueryResult result;
    result.scanned = data_.size();
    result.records.reserve(data_.size() / 10);

    for (const auto& rec : data_) {
        if (rec.trip_distance >= q.min_val &&
            rec.trip_distance <= q.max_val) {
            result.records.push_back(&rec);
        }
    }

    return result;
}

// Query 3: Fare range — linear scan
QueryResult QueryEngine::search_by_fare(const NumericRangeQuery& q) const {
    QueryResult result;
    result.scanned = data_.size();
    result.records.reserve(data_.size() / 10);

    for (const auto& rec : data_) {
        if (rec.total_amount >= q.min_val &&
            rec.total_amount <= q.max_val) {
            result.records.push_back(&rec);
        }
    }

    return result;
}

// Query 4: Location filter — linear scan on PULocationID
QueryResult QueryEngine::search_by_location(const IntRangeQuery& q) const {
    QueryResult result;
    result.scanned = data_.size();
    result.records.reserve(data_.size() / 10);

    for (const auto& rec : data_) {
        if (rec.pu_location_id >= q.min_val &&
            rec.pu_location_id <= q.max_val) {
            result.records.push_back(&rec);
        }
    }

    return result;
}

// Query 5: Combined — time + distance + passenger count
// Uses TimeIndex to narrow window, then filters further.
QueryResult QueryEngine::search_combined(const CombinedQuery& q) const {
    QueryResult result;

    if (time_index_.is_built()) {
        auto [lo, hi] = time_index_.lookup(
            data_, q.time_range.start_time, q.time_range.end_time);
        const auto& idx = time_index_.sorted_indices();
        result.scanned = hi - lo;
        result.records.reserve((hi - lo) / 10);

        for (std::size_t i = lo; i < hi; ++i) {
            const auto& rec = data_[idx[i]];
            if (rec.trip_distance >= q.distance_range.min_val &&
                rec.trip_distance <= q.distance_range.max_val &&
                rec.passenger_count >= q.passenger_range.min_val &&
                rec.passenger_count <= q.passenger_range.max_val) {
                result.records.push_back(&rec);
            }
        }
    } else {
        result.scanned = data_.size();
        result.records.reserve(data_.size() / 20);
        for (const auto& rec : data_) {
            if (rec.pickup_timestamp >= q.time_range.start_time &&
                rec.pickup_timestamp <= q.time_range.end_time &&
                rec.trip_distance >= q.distance_range.min_val &&
                rec.trip_distance <= q.distance_range.max_val &&
                rec.passenger_count >= q.passenger_range.min_val &&
                rec.passenger_count <= q.passenger_range.max_val) {
                result.records.push_back(&rec);
            }
        }
    }

    return result;
}

// Query 6: Aggregation — sum/avg of fare_amount over a time window
// Uses TimeIndex to narrow window, then accumulates.
AggregationResult QueryEngine::aggregate_fare_by_time(const TimeRangeQuery& q) const {
    AggregationResult result;

    if (time_index_.is_built()) {
        auto [lo, hi] = time_index_.lookup(data_, q.start_time, q.end_time);
        const auto& idx = time_index_.sorted_indices();

        for (std::size_t i = lo; i < hi; ++i) {
            result.sum += data_[idx[i]].fare_amount;
        }
        result.count = hi - lo;
    } else {
        for (const auto& rec : data_) {
            if (rec.pickup_timestamp >= q.start_time &&
                rec.pickup_timestamp <= q.end_time) {
                result.sum += rec.fare_amount;
                ++result.count;
            }
        }
    }

    if (result.count > 0) {
        result.avg = result.sum / static_cast<double>(result.count);
    }

    return result;
}

} // namespace taxi
