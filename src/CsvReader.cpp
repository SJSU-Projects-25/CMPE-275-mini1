#include "taxi/CsvReader.hpp"
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <ctime>
#include <cctype>
#include <iomanip>

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
    /**
     * Parse CSV line into TripRecord
     * 
     * Expected CSV column order (17 fields):
     * 0: VendorID
     * 1: tpep_pickup_datetime
     * 2: tpep_dropoff_datetime
     * 3: passenger_count
     * 4: trip_distance
     * 5: RatecodeID
     * 6: store_and_fwd_flag
     * 7: PULocationID
     * 8: DOLocationID
     * 9: payment_type
     * 10: fare_amount
     * 11: extra
     * 12: mta_tax
     * 13: tip_amount
     * 14: tolls_amount
     * 15: improvement_surcharge
     * 16: total_amount
     */
    
    try {
        auto tokens = split_csv_line(line);
        
        // Validate we have the expected number of fields
        constexpr std::size_t EXPECTED_FIELDS = 17;
        if (tokens.size() != EXPECTED_FIELDS) {
            return false; // Wrong number of fields - discard row
        }

        // Helper lambda to safely parse integer with default
        auto parse_int = [](const std::string& str, int default_val = 0) -> int {
            if (str.empty() || str.find_first_not_of(" \t") == std::string::npos) {
                return default_val;
            }
            try {
                return std::stoi(str);
            } catch (...) {
                return default_val;
            }
        };

        // Helper lambda to safely parse double with default
        auto parse_double = [](const std::string& str, double default_val = 0.0) -> double {
            if (str.empty() || str.find_first_not_of(" \t") == std::string::npos) {
                return default_val;
            }
            try {
                return std::stod(str);
            } catch (...) {
                return default_val;
            }
        };

        // Parse critical fields - if these fail, discard the row
        std::int64_t pickup_ts = parse_timestamp(tokens[1]);
        std::int64_t dropoff_ts = parse_timestamp(tokens[2]);
        
        if (pickup_ts <= 0 || dropoff_ts <= pickup_ts) {
            return false; // Invalid timestamps - discard row
        }

        // Parse all fields
        record.vendor_id = parse_int(tokens[0], 0);
        record.pickup_timestamp = pickup_ts;
        record.dropoff_timestamp = dropoff_ts;
        record.passenger_count = parse_int(tokens[3], 0);
        record.trip_distance = parse_double(tokens[4], 0.0);
        record.rate_code_id = parse_int(tokens[5], 0);
        
        // Parse store_and_fwd_flag (Y/N -> true/false)
        std::string flag = tokens[6];
        std::transform(flag.begin(), flag.end(), flag.begin(), ::toupper);
        record.store_and_fwd_flag = (flag == "Y" || flag == "YES" || flag == "TRUE" || flag == "1");
        
        record.pu_location_id = parse_int(tokens[7], 0);
        record.do_location_id = parse_int(tokens[8], 0);
        record.payment_type = parse_int(tokens[9], 0);
        
        // Parse monetary fields (normalize empty/missing to 0.0)
        record.fare_amount = parse_double(tokens[10], 0.0);
        record.extra = parse_double(tokens[11], 0.0);
        record.mta_tax = parse_double(tokens[12], 0.0);
        record.tip_amount = parse_double(tokens[13], 0.0);
        record.tolls_amount = parse_double(tokens[14], 0.0);
        record.improvement_surcharge = parse_double(tokens[15], 0.0);
        record.total_amount = parse_double(tokens[16], 0.0);

        // Validate record meets minimum requirements
        if (!record.is_valid()) {
            return false;
        }

        return true;
        
    } catch (const std::exception&) {
        // Any parsing error - discard row
        return false;
    }
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
    /**
     * Parse TLC timestamp format: "YYYY-MM-DD HH:MM:SS"
     * Returns seconds since Unix epoch (UTC)
     * 
     * Example: "2018-01-01 00:15:30" -> 1514760930
     * 
     * Note: TLC data timestamps are in EST/EDT, but for Phase 1 we'll
     * parse them as UTC-equivalent for simplicity. This is acceptable
     * for relative comparisons and range queries.
     */
    
    if (timestamp_str.empty()) {
        return 0;
    }

    try {
        // TLC format: "YYYY-MM-DD HH:MM:SS"
        std::tm tm = {};
        std::istringstream ss(timestamp_str);
        
        // Parse date and time
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        
        if (ss.fail()) {
            return 0; // Parsing failed
        }
        
        // Calculate seconds since Unix epoch (1970-01-01 00:00:00 UTC)
        // We'll do this manually to avoid timezone issues with mktime
        
        int year = tm.tm_year + 1900;
        int month = tm.tm_mon + 1;
        int day = tm.tm_mday;
        
        // Calculate days since epoch
        std::int64_t days_since_epoch = 0;
        
        // Days from 1970 to (year-1)
        for (int y = 1970; y < year; ++y) {
            bool is_leap = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0));
            days_since_epoch += is_leap ? 366 : 365;
        }
        
        // Days in months before current month in current year
        int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        bool is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
        if (is_leap) days_in_month[1] = 29;
        
        for (int m = 0; m < month - 1; ++m) {
            days_since_epoch += days_in_month[m];
        }
        
        // Add days in current month (day is 1-indexed)
        days_since_epoch += day - 1;
        
        // Convert to seconds
        std::int64_t seconds = days_since_epoch * 86400LL; // 86400 seconds per day
        seconds += static_cast<std::int64_t>(tm.tm_hour) * 3600LL;
        seconds += static_cast<std::int64_t>(tm.tm_min) * 60LL;
        seconds += static_cast<std::int64_t>(tm.tm_sec);
        
        return seconds;
        
    } catch (const std::exception&) {
        return 0; // Parsing failed
    }
}

} // namespace taxi
