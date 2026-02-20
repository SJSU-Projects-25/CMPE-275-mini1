#include "taxi/CsvReader.hpp"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <ctime>

namespace taxi {

CsvReader::CsvReader(const std::string& filepath)
    : file_(filepath), stats_(), header_read_(false) {
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open CSV file: " + filepath);
    }
    // Skip header line
    std::string header;
    if (std::getline(file_, header)) {
        header_read_ = true;
    }
}

CsvReader::~CsvReader() {
    if (file_.is_open()) {
        file_.close();
    }
}

bool CsvReader::is_open() const {
    return file_.is_open() && file_.good();
}

bool CsvReader::read_next(TripRecord& record) {
    std::string line;
    if (!std::getline(file_, line)) {
        return false; // EOF
    }

    stats_.rows_read++;
    
    if (parse_line(line, record)) {
        stats_.rows_parsed_ok++;
        return true;
    } else {
        stats_.rows_discarded++;
        return false;
    }
}

bool CsvReader::parse_line(const std::string& line, TripRecord& record) {
    // TODO: Implement full parsing logic in Step 5
    // For now, return false to indicate not implemented
    (void)line;
    (void)record;
    return false;
}

std::vector<std::string> CsvReader::split_csv_line(const std::string& line) {
    // TODO: Implement CSV tokenization in Step 4
    return {};
}

std::int64_t CsvReader::parse_timestamp(const std::string& timestamp_str) {
    // TODO: Implement timestamp parsing in Step 5
    (void)timestamp_str;
    return 0;
}

} // namespace taxi
