#!/bin/bash

###############################################################################
# TLC Yellow Taxi Trip Data Download Script
# 
# This script downloads TLC (Taxi and Limousine Commission) Yellow Taxi Trip
# Data from NYC OpenData. The datasets are large (>12 GB total required),
# so ensure you have sufficient disk space and bandwidth.
#
# Usage:
#   bash scripts/download_tlc_data.sh [output_dir]
#
#   output_dir: Directory to save CSV files (default: ./data)
#
# Requirements:
#   - curl (for downloading)
#   - Sufficient disk space (>15 GB recommended)
#   - Stable internet connection
###############################################################################

set -e  # Exit on error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${1:-$PROJECT_ROOT/data}"

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

# TLC Yellow Taxi Trip Data URLs
# These are direct download links to CSV files from NYC OpenData
# You may need to update these URLs based on the exact datasets you want
# Check: https://data.cityofnewyork.us/browse?Data-Collection_Data-Collection=TLC%20Trip%20Data

declare -a DATASET_URLS=(
    # Example URLs - UPDATE THESE with your actual dataset URLs
    # Format: "URL|FILENAME"
    # "https://data.cityofnewyork.us/api/views/.../rows.csv?accessType=DOWNLOAD|yellow_taxi_2018_01.csv"
    # "https://data.cityofnewyork.us/api/views/.../rows.csv?accessType=DOWNLOAD|yellow_taxi_2018_02.csv"
    
    # Placeholder: You need to replace these with actual TLC dataset URLs
    # The assignment requires >12 GB and >2 million records
    # You may need multiple monthly/yearly datasets to meet this requirement
)

# If no URLs configured, print instructions
if [ ${#DATASET_URLS[@]} -eq 0 ]; then
    echo "==================================================================="
    echo "WARNING: No dataset URLs configured!"
    echo "==================================================================="
    echo ""
    echo "Please edit this script and add your TLC dataset URLs."
    echo ""
    echo "To find datasets:"
    echo "  1. Visit: https://data.cityofnewyork.us/browse"
    echo "  2. Filter by: Data Collection = 'TLC Trip Data'"
    echo "  3. Select 'Yellow Taxi Trip Data'"
    echo "  4. For each dataset, click 'Export' -> 'CSV'"
    echo "  5. Copy the download URL and add it to DATASET_URLS array"
    echo ""
    echo "Format: \"URL|filename.csv\""
    echo ""
    echo "Example:"
    echo "  DATASET_URLS=("
    echo "    \"https://data.cityofnewyork.us/api/views/abc123/rows.csv?accessType=DOWNLOAD|yellow_2018_01.csv\""
    echo "    \"https://data.cityofnewyork.us/api/views/def456/rows.csv?accessType=DOWNLOAD|yellow_2018_02.csv\""
    echo "  )"
    echo ""
    echo "Output directory: $OUTPUT_DIR"
    echo ""
    exit 1
fi

echo "==================================================================="
echo "TLC Yellow Taxi Trip Data Downloader"
echo "==================================================================="
echo "Output directory: $OUTPUT_DIR"
echo "Number of datasets: ${#DATASET_URLS[@]}"
echo ""

# Download each dataset
for entry in "${DATASET_URLS[@]}"; do
    IFS='|' read -r url filename <<< "$entry"
    
    if [ -z "$url" ] || [ -z "$filename" ]; then
        echo "ERROR: Invalid entry format: $entry"
        echo "Expected format: \"URL|filename.csv\""
        continue
    fi
    
    output_path="$OUTPUT_DIR/$filename"
    
    # Check if file already exists
    if [ -f "$output_path" ]; then
        echo "[SKIP] $filename already exists at $output_path"
        echo "       Delete it first if you want to re-download."
        continue
    fi
    
    echo "[DOWNLOAD] $filename"
    echo "  URL: $url"
    echo "  Saving to: $output_path"
    
    # Download with curl
    # -L: Follow redirects
    # -C -: Resume if interrupted
    # -f: Fail silently on HTTP errors
    # --progress-bar: Show progress bar
    if curl -L -C - -f --progress-bar -o "$output_path" "$url"; then
        file_size=$(du -h "$output_path" | cut -f1)
        echo "  ✓ Downloaded successfully ($file_size)"
    else
        echo "  ✗ Download failed!"
        echo "  Check the URL and your internet connection."
        # Don't exit - continue with other downloads
    fi
    
    echo ""
done

echo "==================================================================="
echo "Download Summary"
echo "==================================================================="
echo "Total files in $OUTPUT_DIR:"
ls -lh "$OUTPUT_DIR"/*.csv 2>/dev/null | wc -l || echo "0"
echo ""
echo "Total size:"
du -sh "$OUTPUT_DIR" 2>/dev/null || echo "N/A"
echo ""
echo "Done!"
