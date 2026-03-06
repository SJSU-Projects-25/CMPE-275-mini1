/**
 * SoAQueryEngine.cpp — Phase 3: Object-of-Arrays query engine.
 *
 * Each query scans one (or a few) contiguous typed arrays instead of
 * iterating over 128-byte TripRecord structs.  This gives 8× better cache
 * utilisation for double fields (8 doubles vs. ~0.5 structs per 64-byte cache
 * line) and enables the compiler to auto-vectorise loops with SIMD.
 *
 * All scans are also parallelised with OpenMP, matching Phase 2 behaviour.
 */

#include "taxi/SoAQueryEngine.hpp"
#include "taxi/TripDataSoA.hpp"
#include "taxi/CsvReader.hpp"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

#if defined(_OPENMP)
#include <omp.h>
#endif

namespace taxi {

// ============================================================================
// TripDataSoA::from_aos — AoS → SoA conversion
// ============================================================================

TripDataSoA TripDataSoA::from_aos(const std::vector<TripRecord>& records)
{
    TripDataSoA soa;
    const std::size_t n = records.size();

    // Pre-allocate all parallel arrays in one pass to avoid reallocations.
    soa.vendor_id.reserve(n);
    soa.pickup_timestamp.reserve(n);
    soa.dropoff_timestamp.reserve(n);
    soa.passenger_count.reserve(n);
    soa.trip_distance.reserve(n);
    soa.rate_code_id.reserve(n);
    soa.store_and_fwd_flag.reserve(n);
    soa.pu_location_id.reserve(n);
    soa.do_location_id.reserve(n);
    soa.payment_type.reserve(n);
    soa.fare_amount.reserve(n);
    soa.extra.reserve(n);
    soa.mta_tax.reserve(n);
    soa.tip_amount.reserve(n);
    soa.tolls_amount.reserve(n);
    soa.improvement_surcharge.reserve(n);
    soa.total_amount.reserve(n);

    for (const auto& r : records) {
        soa.vendor_id.push_back(r.vendor_id);
        soa.pickup_timestamp.push_back(r.pickup_timestamp);
        soa.dropoff_timestamp.push_back(r.dropoff_timestamp);
        soa.passenger_count.push_back(r.passenger_count);
        soa.trip_distance.push_back(r.trip_distance);
        soa.rate_code_id.push_back(r.rate_code_id);
        soa.store_and_fwd_flag.push_back(r.store_and_fwd_flag);
        soa.pu_location_id.push_back(r.pu_location_id);
        soa.do_location_id.push_back(r.do_location_id);
        soa.payment_type.push_back(r.payment_type);
        soa.fare_amount.push_back(r.fare_amount);
        soa.extra.push_back(r.extra);
        soa.mta_tax.push_back(r.mta_tax);
        soa.tip_amount.push_back(r.tip_amount);
        soa.tolls_amount.push_back(r.tolls_amount);
        soa.improvement_surcharge.push_back(r.improvement_surcharge);
        soa.total_amount.push_back(r.total_amount);
    }

    return soa;
}

// ============================================================================
// TripDataSoA::from_csv — direct CSV → SoA (no intermediate AoS)
// ============================================================================

TripDataSoA TripDataSoA::from_csv(const std::vector<std::string>& paths,
                                   std::size_t reserve_count)
{
    TripDataSoA soa;

    if (reserve_count > 0) {
        soa.vendor_id.reserve(reserve_count);
        soa.pickup_timestamp.reserve(reserve_count);
        soa.dropoff_timestamp.reserve(reserve_count);
        soa.passenger_count.reserve(reserve_count);
        soa.trip_distance.reserve(reserve_count);
        soa.rate_code_id.reserve(reserve_count);
        soa.store_and_fwd_flag.reserve(reserve_count);
        soa.pu_location_id.reserve(reserve_count);
        soa.do_location_id.reserve(reserve_count);
        soa.payment_type.reserve(reserve_count);
        soa.fare_amount.reserve(reserve_count);
        soa.extra.reserve(reserve_count);
        soa.mta_tax.reserve(reserve_count);
        soa.tip_amount.reserve(reserve_count);
        soa.tolls_amount.reserve(reserve_count);
        soa.improvement_surcharge.reserve(reserve_count);
        soa.total_amount.reserve(reserve_count);
    }

    for (const auto& path : paths) {
        CsvReader reader(path);
        if (!reader.is_open())
            throw std::runtime_error("from_csv: cannot open " + path);
        TripRecord r;
        while (reader.read_next(r)) {
            soa.vendor_id.push_back(r.vendor_id);
            soa.pickup_timestamp.push_back(r.pickup_timestamp);
            soa.dropoff_timestamp.push_back(r.dropoff_timestamp);
            soa.passenger_count.push_back(r.passenger_count);
            soa.trip_distance.push_back(r.trip_distance);
            soa.rate_code_id.push_back(r.rate_code_id);
            soa.store_and_fwd_flag.push_back(r.store_and_fwd_flag);
            soa.pu_location_id.push_back(r.pu_location_id);
            soa.do_location_id.push_back(r.do_location_id);
            soa.payment_type.push_back(r.payment_type);
            soa.fare_amount.push_back(r.fare_amount);
            soa.extra.push_back(r.extra);
            soa.mta_tax.push_back(r.mta_tax);
            soa.tip_amount.push_back(r.tip_amount);
            soa.tolls_amount.push_back(r.tolls_amount);
            soa.improvement_surcharge.push_back(r.improvement_surcharge);
            soa.total_amount.push_back(r.total_amount);
        }
    }

    return soa;
}

// ============================================================================
// SoAQueryEngine — construction, index build, time lookup
// ============================================================================

SoAQueryEngine::SoAQueryEngine(const TripDataSoA& data)
    : data_(data) {}

double SoAQueryEngine::build_indexes()
{
    auto t0 = std::chrono::steady_clock::now();

    const std::size_t n = data_.size();
    time_sorted_idx_.resize(n);
    std::iota(time_sorted_idx_.begin(), time_sorted_idx_.end(), 0);

    // Sort by pickup_timestamp — accesses only the int64 array (cache-friendly).
    const auto* ts = data_.pickup_timestamp.data();
    std::sort(time_sorted_idx_.begin(), time_sorted_idx_.end(),
              [ts](std::size_t a, std::size_t b) {
                  return ts[a] < ts[b];
              });

    indexed_ = true;
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// Binary search over the sorted index: returns [lo, hi) range of positions.
std::pair<std::size_t, std::size_t>
SoAQueryEngine::time_lookup(std::int64_t start, std::int64_t end) const
{
    const auto* ts  = data_.pickup_timestamp.data();
    const auto* idx = time_sorted_idx_.data();
    const std::size_t n = time_sorted_idx_.size();

    // Lower bound: first position i where ts[idx[i]] >= start
    std::size_t lo = 0, hi_b = n;
    while (lo < hi_b) {
        std::size_t mid = lo + (hi_b - lo) / 2;
        if (ts[idx[mid]] < start) lo = mid + 1;
        else                      hi_b = mid;
    }
    const std::size_t range_lo = lo;

    // Upper bound: first position i where ts[idx[i]] > end
    std::size_t hi = n;
    lo = range_lo;
    while (lo < hi) {
        std::size_t mid = lo + (hi - lo) / 2;
        if (ts[idx[mid]] <= end) lo = mid + 1;
        else                     hi = mid;
    }

    return {range_lo, lo};
}

// ============================================================================
// Query 1: Time range — O(log N) via sorted index, then parallel gather
// ============================================================================

SoAQueryResult SoAQueryEngine::search_by_time(const TimeRangeQuery& q) const
{
    SoAQueryResult result;

    if (indexed_) {
        auto [lo, hi] = time_lookup(q.start_time, q.end_time);
        result.scanned = hi - lo;

        const auto* idx = time_sorted_idx_.data();

        if (hi - lo < 10000) {
            result.indices.reserve(hi - lo);
            for (std::size_t i = lo; i < hi; ++i)
                result.indices.push_back(idx[i]);
        } else {
            #pragma omp parallel
            {
                std::vector<std::size_t> local;
#if defined(_OPENMP)
                local.reserve((hi - lo) / omp_get_num_threads());
#else
                local.reserve(hi - lo);
#endif
                #pragma omp for nowait schedule(static)
                for (std::size_t i = lo; i < hi; ++i)
                    local.push_back(idx[i]);

                #pragma omp critical
                result.indices.insert(result.indices.end(),
                                      local.begin(), local.end());
            }
        }
    } else {
        // Fallback: full linear scan over the pickup_timestamp array only.
        const std::size_t n = data_.size();
        result.scanned = n;
        const auto* ts = data_.pickup_timestamp.data();

        #pragma omp parallel
        {
            std::vector<std::size_t> local;
#if defined(_OPENMP)
            local.reserve(n / (10 * omp_get_num_threads()));
#else
            local.reserve(n / 10);
#endif
            #pragma omp for nowait schedule(static)
            for (std::size_t i = 0; i < n; ++i) {
                if (ts[i] >= q.start_time && ts[i] <= q.end_time)
                    local.push_back(i);
            }

            #pragma omp critical
            result.indices.insert(result.indices.end(),
                                  local.begin(), local.end());
        }
    }

    return result;
}

// ============================================================================
// Query 2: Distance range — contiguous double[] scan, fully vectorisable
// ============================================================================

SoAQueryResult SoAQueryEngine::search_by_distance(const NumericRangeQuery& q) const
{
    SoAQueryResult result;
    const std::size_t n  = data_.size();
    result.scanned       = n;

    // Pointer to contiguous double array — compiler can use SIMD (AVX2/AVX-512).
    const double* dist = data_.trip_distance.data();
    const double  lo   = q.min_val;
    const double  hi   = q.max_val;

    #pragma omp parallel
    {
        std::vector<std::size_t> local;
#if defined(_OPENMP)
        local.reserve(n / (10 * omp_get_num_threads()));
#else
        local.reserve(n / 10);
#endif
        #pragma omp for nowait schedule(static)
        for (std::size_t i = 0; i < n; ++i) {
            if (dist[i] >= lo && dist[i] <= hi)
                local.push_back(i);
        }

        #pragma omp critical
        result.indices.insert(result.indices.end(), local.begin(), local.end());
    }

    return result;
}

// ============================================================================
// Query 3: Fare (total_amount) range — contiguous double[] scan
// ============================================================================

SoAQueryResult SoAQueryEngine::search_by_fare(const NumericRangeQuery& q) const
{
    SoAQueryResult result;
    const std::size_t n  = data_.size();
    result.scanned       = n;

    const double* amt = data_.total_amount.data();
    const double  lo  = q.min_val;
    const double  hi  = q.max_val;

    #pragma omp parallel
    {
        std::vector<std::size_t> local;
#if defined(_OPENMP)
        local.reserve(n / (10 * omp_get_num_threads()));
#else
        local.reserve(n / 10);
#endif
        #pragma omp for nowait schedule(static)
        for (std::size_t i = 0; i < n; ++i) {
            if (amt[i] >= lo && amt[i] <= hi)
                local.push_back(i);
        }

        #pragma omp critical
        result.indices.insert(result.indices.end(), local.begin(), local.end());
    }

    return result;
}

// ============================================================================
// Query 4: Location (PULocationID) range — contiguous int[] scan
// 16 ints per 64-byte cache line (vs. 0.5 TripRecords in AoS)
// ============================================================================

SoAQueryResult SoAQueryEngine::search_by_location(const IntRangeQuery& q) const
{
    SoAQueryResult result;
    const std::size_t n  = data_.size();
    result.scanned       = n;

    const int* loc = data_.pu_location_id.data();
    const int  lo  = q.min_val;
    const int  hi  = q.max_val;

    #pragma omp parallel
    {
        std::vector<std::size_t> local;
#if defined(_OPENMP)
        local.reserve(n / (10 * omp_get_num_threads()));
#else
        local.reserve(n / 10);
#endif
        #pragma omp for nowait schedule(static)
        for (std::size_t i = 0; i < n; ++i) {
            if (loc[i] >= lo && loc[i] <= hi)
                local.push_back(i);
        }

        #pragma omp critical
        result.indices.insert(result.indices.end(), local.begin(), local.end());
    }

    return result;
}

// ============================================================================
// Query 5: Combined — time + distance + passenger_count
// Uses time index to narrow the candidate window, then filters on two more arrays.
// ============================================================================

SoAQueryResult SoAQueryEngine::search_combined(const CombinedQuery& q) const
{
    SoAQueryResult result;

    const double* dist = data_.trip_distance.data();
    const int*    pax  = data_.passenger_count.data();
    const auto*   idx  = time_sorted_idx_.data();

    const double  dist_lo = q.distance_range.min_val;
    const double  dist_hi = q.distance_range.max_val;
    const int     pax_lo  = q.passenger_range.min_val;
    const int     pax_hi  = q.passenger_range.max_val;

    if (indexed_) {
        auto [lo, hi] = time_lookup(q.time_range.start_time, q.time_range.end_time);
        result.scanned = hi - lo;

        #pragma omp parallel
        {
            std::vector<std::size_t> local;
#if defined(_OPENMP)
            local.reserve((hi - lo) / (10 * omp_get_num_threads()));
#else
            local.reserve((hi - lo) / 10);
#endif
            #pragma omp for nowait schedule(static)
            for (std::size_t i = lo; i < hi; ++i) {
                std::size_t row = idx[i];
                if (dist[row] >= dist_lo && dist[row] <= dist_hi &&
                    pax[row]  >= pax_lo  && pax[row]  <= pax_hi)
                    local.push_back(row);
            }

            #pragma omp critical
            result.indices.insert(result.indices.end(),
                                  local.begin(), local.end());
        }
    } else {
        const std::size_t n = data_.size();
        result.scanned      = n;
        const auto* ts      = data_.pickup_timestamp.data();

        #pragma omp parallel
        {
            std::vector<std::size_t> local;
#if defined(_OPENMP)
            local.reserve(n / (20 * omp_get_num_threads()));
#else
            local.reserve(n / 20);
#endif
            #pragma omp for nowait schedule(static)
            for (std::size_t i = 0; i < n; ++i) {
                if (ts[i]   >= q.time_range.start_time &&
                    ts[i]   <= q.time_range.end_time   &&
                    dist[i] >= dist_lo && dist[i] <= dist_hi &&
                    pax[i]  >= pax_lo  && pax[i]  <= pax_hi)
                    local.push_back(i);
            }

            #pragma omp critical
            result.indices.insert(result.indices.end(),
                                  local.begin(), local.end());
        }
    }

    return result;
}

// ============================================================================
// Query 6: Aggregation — sum/avg of fare_amount over a time window
// Reduction over a contiguous double[] — the compiler can use SIMD reduction.
// ============================================================================

AggregationResult SoAQueryEngine::aggregate_fare_by_time(const TimeRangeQuery& q) const
{
    AggregationResult result;
    const double* fare = data_.fare_amount.data();
    const auto*   idx  = time_sorted_idx_.data();

    if (indexed_) {
        auto [lo, hi] = time_lookup(q.start_time, q.end_time);
        result.count = hi - lo;

        double local_sum = 0.0;
        // Reduction over fare_amount values accessed via sorted index.
        // The SIMD unit sees a gather pattern here; still parallelises well.
        #pragma omp parallel for reduction(+:local_sum) schedule(static)
        for (std::size_t i = lo; i < hi; ++i)
            local_sum += fare[idx[i]];

        result.sum = local_sum;
    } else {
        const std::size_t n = data_.size();
        const auto* ts      = data_.pickup_timestamp.data();

        double      local_sum   = 0.0;
        std::size_t local_count = 0;

        #pragma omp parallel for reduction(+:local_sum,local_count) schedule(static)
        for (std::size_t i = 0; i < n; ++i) {
            if (ts[i] >= q.start_time && ts[i] <= q.end_time) {
                // Access fare_amount[] — contiguous typed array.
                local_sum += fare[i];
                ++local_count;
            }
        }
        result.sum   = local_sum;
        result.count = local_count;
    }

    if (result.count > 0)
        result.avg = result.sum / static_cast<double>(result.count);

    return result;
}

} // namespace taxi
