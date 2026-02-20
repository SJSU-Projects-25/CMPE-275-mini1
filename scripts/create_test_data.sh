#!/bin/bash

###############################################################################
# Create Test CSV Data
# 
# Extracts a sample from the full dataset to create a smaller test file.
# This test file can be committed to GitHub for team members to use.
#
# Usage:
#   bash scripts/create_test_data.sh [input_file] [num_rows] [output_file]
#
# Arguments:
#   input_file:  Path to full CSV file (default: data/2018_Yellow_Taxi_Trip_Data_20260216.csv)
#   num_rows:    Number of data rows to extract (default: 10000)
#   output_file: Output file path (default: data/test_sample.csv)
###############################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

INPUT_FILE="${1:-$PROJECT_ROOT/data/2018_Yellow_Taxi_Trip_Data_20260216.csv}"
NUM_ROWS="${2:-10000}"
OUTPUT_FILE="${3:-$PROJECT_ROOT/data/test_sample.csv}"

echo "==================================================================="
echo "Create Test CSV Data"
echo "==================================================================="
echo "Input file:  $INPUT_FILE"
echo "Output file: $OUTPUT_FILE"
echo "Rows to extract: $NUM_ROWS (plus header)"
echo ""

# Check if input file exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "ERROR: Input file not found: $INPUT_FILE"
    exit 1
fi

# Create data directory if it doesn't exist
mkdir -p "$(dirname "$OUTPUT_FILE")"

echo "Extracting sample data..."
echo "  Reading header and first $NUM_ROWS rows..."

# Extract header + specified number of rows
head -n $((NUM_ROWS + 1)) "$INPUT_FILE" > "$OUTPUT_FILE"

# Get file size
FILE_SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
LINE_COUNT=$(wc -l < "$OUTPUT_FILE" | tr -d ' ')

echo ""
echo "==================================================================="
echo "Test Data Created Successfully ✓"
echo "==================================================================="
echo "Output file: $OUTPUT_FILE"
echo "File size:   $FILE_SIZE"
echo "Total lines: $LINE_COUNT (1 header + $NUM_ROWS data rows)"
echo ""
echo "You can now use this file for testing:"
echo "  ./build/bin/taxi_bench $OUTPUT_FILE 10"
echo ""
echo "This file is small enough to commit to GitHub for team reference."
echo ""
