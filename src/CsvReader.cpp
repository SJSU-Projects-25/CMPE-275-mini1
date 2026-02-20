#include "taxi/CsvReader.hpp"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <ctime>
#include <cctype>

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

    // Skip empty lines
    if (line.empty() || (line.find_first_not_of(" \t\r\n") == std::string::npos)) {
        stats_.rows_read++;
        stats_.rows_discarded++;
        return false;
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
    /**
     * CSV Line Parser
     * 
     * Handles RFC 4180-compliant CSV format:
     * - Fields may be quoted with double quotes
     * - Quoted fields may contain commas
     * - Escaped quotes within quoted fields are represented as ""
     * - Empty fields are allowed
     * 
     * Example: "field1","field,with,commas","field""with""quotes",field4
     * Results: ["field1", "field,with,commas", "field\"with\"quotes", "field4"]
     */
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_quotes = false;
    
    for (std::size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        
        if (c == '"') {
            if (in_quotes && i + 1 < line.length() && line[i + 1] == '"') {
                // Escaped quote (double quote) - add single quote to token
                current_token += '"';
                ++i; // Skip next quote
            } else {
                // Toggle quote state
                in_quotes = !in_quotes;
                // Note: We don't add the quote character itself to the token
            }
        } else if (c == ',' && !in_quotes) {
            // Field separator - save current token and start new one
            tokens.push_back(current_token);
            current_token.clear();
        } else {
            // Regular character - add to current token
            current_token += c;
        }
    }
    
    // Add the last token (after final comma or end of line)
    tokens.push_back(current_token);
    
    return tokens;
}

std::int64_t CsvReader::parse_timestamp(const std::string& timestamp_str) {
    // TODO: Implement timestamp parsing in Step 5
    (void)timestamp_str;
    return 0;
}

} // namespace taxi
