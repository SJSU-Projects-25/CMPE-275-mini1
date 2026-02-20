#include "taxi/CsvReader.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

/**
 * Simple test program to verify CSV reader can open files and read lines.
 * Tests the streaming capability without loading entire file into memory.
 * 
 * This is a temporary test utility - CSV parsing will be fully tested in Step 5.
 */
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> [num_lines]\n";
        std::cerr << "  csv_file: Path to CSV file\n";
        std::cerr << "  num_lines: Number of lines to test (default: 10)\n";
        return 1;
    }

    const std::string csv_path = argv[1];
    const int num_lines = (argc >= 3) ? std::atoi(argv[2]) : 10;

    std::cout << "=== CSV Reader Streaming Test ===\n";
    std::cout << "File: " << csv_path << "\n";
    std::cout << "Testing streaming read of first " << num_lines << " data lines\n\n";

    try {
        // Test 1: Verify file can be opened
        taxi::CsvReader reader(csv_path);
        
        if (!reader.is_open()) {
            std::cerr << "ERROR: Failed to open file\n";
            return 1;
        }
        std::cout << "✓ File opened successfully\n";

        // Test 2: Verify we can read lines without loading entire file
        std::cout << "✓ Testing streaming read...\n\n";
        
        int lines_read = 0;
        taxi::TripRecord dummy_record;
        
        // Try to read a few records (parsing will fail for now, but reading should work)
        while (lines_read < num_lines && reader.read_next(dummy_record)) {
            lines_read++;
        }
        
        auto stats = reader.get_stats();
        
        std::cout << "Results:\n";
        std::cout << "  Lines attempted: " << stats.rows_read << "\n";
        std::cout << "  Lines successfully read: " << lines_read << "\n";
        std::cout << "\n✓ Streaming read test complete\n";
        std::cout << "  (Parsing will be implemented in Step 5)\n";

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
