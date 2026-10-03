#!/bin/sh
# Regenerates the tables in bambu_nfc, then copies them here with the right namespace.
set -e
cd "$(dirname "$0")/../bambu_nfc" && python3 generate_bambu_db.py
for f in bambu_colors.h bambu_densities.h; do
  sed 's/namespace bambu_nfc/namespace bambu_rc522/' "$f" > "../bambu_rc522/$f"
done
echo "Tables synced into bambu_rc522"
