#!/usr/bin/env bash
# Run the SQL tests against your own spreadsheet instead of the default test sheet.
# Prepare the spreadsheet first with scripts/create_test_fixture.py.
#
# Usage: TOKEN=<token> ./scripts/test_sql_own_sheet.sh <spreadsheet-id> [unittest args...]
#        (set KEY_FILE_PATH as well to run the key file tests)
#
# Google limits reads to 60 per minute per user, which a full run exceeds. Set
# PAUSE_SECONDS (e.g. 70) to run the test files one at a time with a pause between them.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DEFAULT_SHEET_ID=11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8

if [ $# -lt 1 ]; then
    echo "Usage: $0 <spreadsheet-id> [unittest args...]" >&2
    exit 1
fi
SHEET_ID="$1"
shift

# The test runner only discovers files under test/, so the rewritten copies live
# in test/sql_own_sheet/ (gitignored).
OUT_DIR="$PROJECT_DIR/test/sql_own_sheet"
rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"
for f in "$PROJECT_DIR"/test/sql/*.test; do
    sed "s/$DEFAULT_SHEET_ID/$SHEET_ID/g" "$f" > "$OUT_DIR/$(basename "$f")"
done

cd "$PROJECT_DIR"
if [ ! -f "./build/release/test/unittest" ]; then
    echo "==> Building extension (make release)"
    make release
fi
PAUSE_SECONDS="${PAUSE_SECONDS:-0}"
if [ "$PAUSE_SECONDS" -eq 0 ]; then
    ./build/release/test/unittest "test/sql_own_sheet/*" "$@"
    exit
fi

status=0
first=1
for f in test/sql_own_sheet/*.test; do
    if [ "$first" -eq 0 ]; then
        echo "==> Waiting ${PAUSE_SECONDS}s for the Sheets API read quota"
        sleep "$PAUSE_SECONDS"
    fi
    first=0
    echo "==> $f"
    ./build/release/test/unittest "$f" "$@" || status=1
done
exit $status
