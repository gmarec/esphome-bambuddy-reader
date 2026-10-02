#!/bin/sh
# Regenere les tables dans bambu_nfc puis les recopie ici avec le bon namespace.
set -e
cd "$(dirname "$0")/../bambu_nfc" && python3 generate_bambu_db.py
for f in bambu_colors.h bambu_densities.h; do
  sed 's/namespace bambu_nfc/namespace bambu_rc522/' "$f" > "../bambu_rc522/$f"
done
echo "Tables synchronisees dans bambu_rc522"
