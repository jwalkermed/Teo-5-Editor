#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright (C) 2026 The TEO-5 Editor project
# Builds the user's guide PDF: renders the screenshots with TEO5Snapshot (example programs, not your library),
# expands docs/manual/content.html and prints it with headless Google Chrome (in a throwaway profile).
# Usage: packaging/make-manual.sh        -> build/manual/TEO-5-Editor-Users-Guide.pdf
set -e
cd "$(dirname "$0")/.."

CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
[ -x "$CHROME" ] || { echo "Google Chrome is needed to print the manual"; exit 1; }

cmake --build build --config Release --target TEO5Snapshot
SNAP=build/TEO5Snapshot_artefacts/Release/TEO5Snapshot
VERSION=$(grep -m1 -oE 'project\(TEO5Editor VERSION [0-9.]+' CMakeLists.txt | awk '{print $3}')
OUT="$PWD/build/manual"
rm -rf "$OUT" && mkdir -p "$OUT/img"
export TEO5_LAYOUT_FILE="$OUT/layout.json"

# name, then the snapshot mode arguments
shot() { local name=$1; shift; "$SNAP" "$OUT/img/$name.png" 2 "$@" >/dev/null; }
shot panel
for m in sequencer modmatrix drawer global program guide; do shot $m $m; done
shot modmatrix-assign modmatrix assign
shot library-write library write          # WRITE: the library as a picker with the Save pop-up
TEO5_SNAPSHOT_CLICKS="37,167;483,167" shot library library   # opens TEO-5 User 1 and My Programs
for f in "$OUT"/img/*.png; do
  sips -s format jpeg -s formatOptions 85 "$f" --out "${f%.png}.jpg" >/dev/null
done

python3 docs/manual/build.py "$VERSION" "$OUT"
"$CHROME" --headless=new --user-data-dir="$OUT/chrome-profile" --no-first-run --disable-gpu --no-pdf-header-footer \
  --print-to-pdf="$OUT/TEO-5-Editor-Users-Guide.pdf" "file://$OUT/manual.html" 2>/dev/null
rm -rf "$OUT/chrome-profile"
[ -s "$OUT/TEO-5-Editor-Users-Guide.pdf" ] || { echo "Chrome didn't print the manual"; exit 1; }
echo "Created build/manual/TEO-5-Editor-Users-Guide.pdf"
