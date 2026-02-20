#include "taxi/CsvReader.hpp"
#include <iostream>
#include <iomanip>

/**
 * Test program to verify CSV reader parsing functionality.
 * Tests schema mapping and type conversion from Step 5.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> [num_lines]\n";
        std::cerr << "  csv_file: Path to CSV file\n";
        std::cerr << "  num_lines: Number of records to test (default: 10)\n";
        return 1;
    }

    const std::string csv_path = argv[1];
    const int num_lines = (argc >= 3) ? std::atoi(argv[2]) : 10;

    std::cout << "=== CSV Reader Parsing Test ===\n";
    std::cout << "File: " << csv_path << "\n";
    std::cout << "Testing parsing of first " << num_lines << " records\n\n";

    try {
        taxi::CsvReader reader(csv_path);
        
        if (!reader.is_open()) {
            std::cerr << "ERROR: Failed to open file\n";
            return 1;
        }

        std::cout << "✓ File opened successfully\n";
        std::cout << "✓ Testing record parsing...\n\n";
        
        int records_parsed = 0;
        taxi::TripRecord record;
        
        while (records_parsed < num_lines && reader.read_next(record)) {
            records_parsed++;
            
            if (records_parsed <= 3) {
                // Show details of first 3 records
                std::cout << "Record " << records_parsed << ":\n";
                std::cout << "  Vendor ID: " << record.vendor_id << "\n";
                std::cout << "  Pickup TS: " << record.pickup_timestamp << "\n";
                std::cout << "  Dropoff TS: " << record.dropoff_timestamp << "\n";
                std::cout << "  Passengers: " << record.passenger_count << "\n";
                std::cout << "  Distance: " << std::fixed << std::setprecision(2) 
                          << record.trip_distance << " miles\n";
                std::cout << "  Fare: $" << std::fixed << std::setprecision(2) 
                          << record.fare_amount << "\n";
                std::cout << "  Total: $" << std::fixed << std::setprecision(2) 
                          << record.total_amount << "\n";
                std::cout << "  Valid: " << (record.is_valid() ? "Yes" : "No") << "\n\n";
            }
        }
        
        auto stats = reader.get_stats();
        
        std::cout << "=== Test Results ===\n";
        std::cout << "  Rows read: " << stats.rows_read << "\n";
        std::cout << "  Records parsed successfully: " << stats.rows_parsed_ok << "\n";
        std::cout << "  Rows discarded: " << stats.rows_discarded << "\n";
        std::cout << "  Success rate: " << std::fixed << std::setprecision(1)
                  << (stats.rows_read > 0 ? 
                      (100.0 * stats.rows_parsed_ok / stats.rows_read) : 0.0) 
                  << "%\n\n";
        
        if (records_parsed > 0) {
            std::cout << "✓ Parsing test successful!\n";
        } else {
            std::cout << "⚠ No records parsed - check CSV format\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
