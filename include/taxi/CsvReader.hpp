#pragma once

#include "taxi/TripRecord.hpp"
#include <string>
#include <fstream>
#include <memory>
#include <vector>

namespace taxi {

/**
 * @brief Streaming CSV reader for TLC taxi trip data.
 * 
 * Reads CSV files line-by-line without loading entire file into memory.
 * Handles parsing, type conversion, and error handling.
 */
class CsvReader {
public:
    explicit CsvReader(const std::string& filepath);
    ~CsvReader();

    // Non-copyable, movable
    CsvReader(const CsvReader&) = delete;
    CsvReader& operator=(const CsvReader&) = delete;
    CsvReader(CsvReader&&) = default;
    CsvReader& operator=(CsvReader&&) = default;

    /**
     * @brief Read the next valid trip record from CSV.
     * @param record Output parameter for the parsed record.
     * @return true if a record was successfully read, false if EOF or error.
     */
    bool read_next(TripRecord& record);

    /**
     * @brief Check if the file is open and readable.
     */
    bool is_open() const;

    /**
     * @brief Get statistics about parsing.
     */
    struct Stats {
        std::size_t rows_read = 0;
        std::size_t rows_parsed_ok = 0;
        std::size_t rows_discarded = 0;
    };

    Stats get_stats() const { return stats_; }

private:
    std::ifstream file_;
    Stats stats_;
    bool header_read_;

    /**
     * @brief Parse a CSV line into a TripRecord.
     * @param line The CSV line to parse.
     * @param record Output parameter.
     * @return true if parsing succeeded, false otherwise.
     */
    bool parse_line(const std::string& line, TripRecord& record);

    /**
     * @brief Split a CSV line into tokens, handling quoted fields.
     */
    std::vector<std::string> split_csv_line(const std::string& line);

    /**
     * @brief Convert timestamp string to seconds since epoch.
     */
    std::int64_t parse_timestamp(const std::string& timestamp_str);
};

} // namespace taxi
