#include "taxi/DatasetManager.hpp"
#include "taxi/CsvReader.hpp"
#include <stdexcept>
#include <algorithm>

namespace taxi {

DatasetManager::DatasetManager() : records_(), load_stats_() {
    // Reserve some initial capacity to reduce reallocations
    records_.reserve(1000000); // Reserve for ~1M records initially
}

void DatasetManager::load_from_csv(const std::string& csv_path) {
    clear();
    
    try {
        CsvReader reader(csv_path);
        if (!reader.is_open()) {
            throw std::runtime_error("Failed to open CSV file: " + csv_path);
        }

        TripRecord record;
        while (reader.read_next(record)) {
            records_.push_back(record);
        }

        // Update statistics
        auto csv_stats = reader.get_stats();
        load_stats_.total_rows_read = csv_stats.rows_read;
        load_stats_.total_rows_parsed = csv_stats.rows_parsed_ok;
        load_stats_.total_rows_discarded = csv_stats.rows_discarded;
        
        // Warn if no records were loaded
        if (records_.empty() && csv_stats.rows_read > 0) {
            // This is a warning, not an error - file might be empty or all rows invalid
            // We'll let the caller decide what to do
        }
        
    } catch (const std::runtime_error&) {
        // Re-throw runtime errors (file not found, etc.)
        throw;
    } catch (const std::exception& e) {
        // Wrap other exceptions
        throw std::runtime_error("Error reading CSV file: " + std::string(e.what()));
    }
}

void DatasetManager::clear() {
    records_.clear();
    load_stats_ = LoadStats();
    query_engine_.reset();
}

QueryEngine& DatasetManager::query_engine() {
    if (!query_engine_) {
        query_engine_ = std::make_unique<QueryEngine>(records_);
        query_engine_->build_indexes();
    }
    return *query_engine_;
}

std::vector<const TripRecord*> DatasetManager::search_by_fare(double min_fare, double max_fare) const {
    std::vector<const TripRecord*> results;
    results.reserve(records_.size() / 10); // Heuristic: reserve 10% capacity

    for (const auto& record : records_) {
        if (record.fare_amount >= min_fare && record.fare_amount <= max_fare) {
            results.push_back(&record);
        }
    }

    return results;
}

std::vector<const TripRecord*> DatasetManager::search_by_distance(double min_distance, double max_distance) const {
    std::vector<const TripRecord*> results;
    results.reserve(records_.size() / 10);

    for (const auto& record : records_) {
        if (record.trip_distance >= min_distance && record.trip_distance <= max_distance) {
            results.push_back(&record);
        }
    }

    return results;
}

std::vector<const TripRecord*> DatasetManager::search_by_passenger_count(int min_passengers, int max_passengers) const {
    std::vector<const TripRecord*> results;
    results.reserve(records_.size() / 10);

    for (const auto& record : records_) {
        if (record.passenger_count >= min_passengers && record.passenger_count <= max_passengers) {
            results.push_back(&record);
        }
    }

    return results;
}

void DatasetManager::reserve_if_needed(std::size_t estimated_size) {
    if (estimated_size > records_.capacity()) {
        records_.reserve(estimated_size);
    }
}

} // namespace taxi
