#pragma once

#include "taxi/TripRecord.hpp"
#include <string>
#include <vector>
#include <memory>

namespace taxi {

/**
 * @brief Manages a collection of taxi trip records.
 * 
 * Provides APIs for loading CSV data and performing range searches.
 * This is the main library interface for Phase 1.
 */
class DatasetManager {
public:
    DatasetManager();
    ~DatasetManager() = default;

    // Non-copyable, movable
    DatasetManager(const DatasetManager&) = delete;
    DatasetManager& operator=(const DatasetManager&) = delete;
    DatasetManager(DatasetManager&&) = default;
    DatasetManager& operator=(DatasetManager&&) = default;

    /**
     * @brief Load trip records from a CSV file.
     * @param csv_path Path to the CSV file.
     * @throws std::runtime_error if file cannot be opened or read.
     */
    void load_from_csv(const std::string& csv_path);

    /**
     * @brief Get all loaded records (const reference).
     */
    const std::vector<TripRecord>& records() const { return records_; }

    /**
     * @brief Get the number of loaded records.
     */
    std::size_t size() const { return records_.size(); }

    /**
     * @brief Clear all loaded records.
     */
    void clear();

    // Search APIs (Phase 1: serial, no threading)

    /**
     * @brief Search for trips within a fare amount range.
     * @param min_fare Minimum fare amount (inclusive).
     * @param max_fare Maximum fare amount (inclusive).
     * @return Vector of pointers to matching records.
     */
    std::vector<const TripRecord*> search_by_fare(double min_fare, double max_fare) const;

    /**
     * @brief Search for trips within a distance range.
     * @param min_distance Minimum trip distance in miles (inclusive).
     * @param max_distance Maximum trip distance in miles (inclusive).
     * @return Vector of pointers to matching records.
     */
    std::vector<const TripRecord*> search_by_distance(double min_distance, double max_distance) const;

    /**
     * @brief Search for trips within a passenger count range.
     * @param min_passengers Minimum passenger count (inclusive).
     * @param max_passengers Maximum passenger count (inclusive).
     * @return Vector of pointers to matching records.
     */
    std::vector<const TripRecord*> search_by_passenger_count(int min_passengers, int max_passengers) const;

    /**
     * @brief Get loading statistics.
     */
    struct LoadStats {
        std::size_t total_rows_read = 0;
        std::size_t total_rows_parsed = 0;
        std::size_t total_rows_discarded = 0;
    };

    LoadStats get_load_stats() const { return load_stats_; }

private:
    std::vector<TripRecord> records_;
    LoadStats load_stats_;

    /**
     * @brief Reserve memory if we can estimate size (for performance).
     */
    void reserve_if_needed(std::size_t estimated_size);
};

} // namespace taxi
