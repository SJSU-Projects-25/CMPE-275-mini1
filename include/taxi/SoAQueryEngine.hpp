#pragma once

#include "taxi/TripDataSoA.hpp"
#include "taxi/QueryTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace taxi {

/**
 * @brief Query result returned by SoAQueryEngine.
 *
 * Because data is split across parallel typed arrays there is no per-record
 * pointer to return.  Instead we return the matching row indices; callers can
 * access any field via data_.fare_amount[result.indices[i]] etc.
 */
struct SoAQueryResult {
    std::vector<std::size_t> indices;   ///< matching row indices into the SoA arrays
    std::size_t              scanned = 0; ///< number of rows examined (for analysis)
};

/**
 * @brief Phase 3 query engine operating on Object-of-Arrays (SoA) layout.
 *
 * Implements the same 6 queries as QueryEngine but on TripDataSoA.
 *
 * Key performance advantages over AoS QueryEngine:
 *  - Q2 (distance scan): reads only data_.trip_distance[] — 8 doubles per
 *    64-byte cache line vs. 0.5 TripRecords (128 B struct) in AoS.
 *  - Q3 (fare scan): reads only data_.total_amount[].
 *  - Q4 (location scan): reads only data_.pu_location_id[] (4-byte ints).
 *  - Q5 (combined): each predicate array is accessed independently; the CPU
 *    prefetcher sees stride-1 access on typed arrays.
 *  - Q6 (aggregation): reduction over data_.fare_amount[] — fully vectorisable.
 *  - All scans parallelised with OpenMP (same as Phase 2 QueryEngine).
 */
class SoAQueryEngine {
public:
    explicit SoAQueryEngine(const TripDataSoA& data);

    /// Build the time-sorted index.  Must be called before queries.
    /// Returns build time in milliseconds.
    double build_indexes();

    // ---- Single-field range searches (Q1-Q4) ----
    SoAQueryResult search_by_time(const TimeRangeQuery& q) const;
    SoAQueryResult search_by_distance(const NumericRangeQuery& q) const;
    SoAQueryResult search_by_fare(const NumericRangeQuery& q) const;
    SoAQueryResult search_by_location(const IntRangeQuery& q) const;

    // ---- Multi-predicate combined search (Q5) ----
    SoAQueryResult search_combined(const CombinedQuery& q) const;

    // ---- Aggregation (Q6) ----
    AggregationResult aggregate_fare_by_time(const TimeRangeQuery& q) const;

    bool        indexes_built() const { return indexed_; }
    std::size_t size()          const { return data_.size(); }

private:
    const TripDataSoA&       data_;
    std::vector<std::size_t> time_sorted_idx_; ///< row indices sorted by pickup_timestamp
    bool                     indexed_ = false;

    /// Binary-search the sorted index; returns [lo, hi) position range.
    std::pair<std::size_t, std::size_t>
    time_lookup(std::int64_t start, std::int64_t end) const;
};

} // namespace taxi
