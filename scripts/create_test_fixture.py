#!/usr/bin/env python3
"""Recreate the duckdb_gsheets SQL test fixture in a spreadsheet you own.

Usage:
    TOKEN=<oauth access token> python3 scripts/create_test_fixture.py <spreadsheet-id>

The tabs and data are reverse-engineered from test/sql/*.test. The script is
idempotent: it creates or renames the tabs it manages (with the exact gids the
tests use), clears them and rewrites their contents. Tabs it does not manage are
left alone. The token is read from the environment and never printed.
"""

import json
import os
import sys
import urllib.error
import urllib.request

API = "https://sheets.googleapis.com/v4/spreadsheets"

SHEET1_ROWS = [
    ["name", "age", "city"],
    ["Alice", 30, "Toronto"],
    ["Bob", 25, "New York"],
    ["Charlie", 45, "Chicago"],
    ["Drake"],
    [],
    ["Archie", 99],
]

# (gid or None to let Google pick, title, rows). Order = tab order; Sheet1 must be first.
TABS = [
    (0, "Sheet1", SHEET1_ROWS),
    (None, "Sheet1!", SHEET1_ROWS),
    (None, "Sheet2", [
        ["code", "x", "y", "city", "region"],
        ["AGA", 57.5, 27, "Agana GU", "Pacific"],
        ["ALB", 49, 21.5, "Albany NY", "Northeast"],
        ["ABQ", 30, 15.5, "Albuquerque NM", "Southwest"],
        ["ANC", 26.5, 26, "Anchorage AK", "Pacific"],
        ["ATL", 53, 14.5, "Atlanta GA", "Southeast"],
        ["BAL", 19, 11, "Baltimore MD", "Mid-Atlantic"],
        ["BOI", 19.5, 15, "Boise ID", "Northwest"],
        ["BOS", 35.5, 23, "Boston MA", "Northeast"],
        ["BNY", 56, 24.5, "Brooklyn NY", "Northeast"],
        ["BUF", 32.5, 23, "Buffalo NY", "Northeast"],
    ]),
    # Issue 34: empty numeric cell
    (732080485, "34-empty-strings", [
        ["id", "value", "note"],
        [1, "value1", "blabla1"],
        [2, "value2", "blabla2"],
        [3, "value3", "blabla3"],
        ["", "value4", "blabla4"],
    ]),
    # Issue 47: blanks in the first data row
    (1746330494, "47-blank-first-row", [
        ["a", "b", "c", "d"],
        ["woot", "blah"],
        ["more wooting", "more blah", "", "should get this!"],
    ]),
    # Issue 47: missing cells, booleans and doubles
    (1961167280, "47-missing-cells", [
        ["a", "b", "c", "d", "e", "f", "g"],
        ["woot", "blah", "", "", "TRUE", 123, "should get this!"],
        ["more wooting", "more blah", "should handle blank to the right"],
        ["more wooting", "more blah", "", "", "FALSE", 456.789, "should get this!"],
    ]),
    # Issue 47: missing column headers
    (1108445818, "47-missing-headers", [
        ["a", "b"],
        ["woot", "blah", "", "should get this!"],
        ["more wooting", "more blah", "", "should get this!"],
    ]),
    (None, "62-header_only", [["a", "b"]]),
    (None, "62-empty", []),
    (528658050, "key-file-read", [
        ["name", "value"],
        ["woot", 0],
        ["woot", 9001],
    ]),
    # Write targets (the tests fill these themselves)
    (295324039, "copy-to", []),
    (1385451074, "copy-to-ranged", []),
    (1134341320, "copy-multiple-vectors", []),
    (1295634987, "types", []),
    (341836654, "overwrite-range", "OVERWRITE_RANGE"),
    (768509192, "Ranged Write Overwrite Sheet", []),
    (1902090053, "append", []),
]


def a1(title, cell="A1"):
    return "'" + title.replace("'", "''") + "'!" + cell


def call(token, method, url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header("Authorization", "Bearer " + token)
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req) as resp:
            return json.load(resp)
    except urllib.error.HTTPError as e:
        sys.exit(f"{method} {url.split('?')[0]} failed: HTTP {e.code}\n{e.read().decode()}")


def main():
    if len(sys.argv) != 2 or not os.environ.get("TOKEN"):
        sys.exit(__doc__)
    token, sid = os.environ["TOKEN"], sys.argv[1]
    base = f"{API}/{sid}"

    meta = call(token, "GET", base + "?fields=sheets.properties(sheetId,title,index)")
    existing = {s["properties"]["sheetId"]: s["properties"]["title"] for s in meta["sheets"]}
    by_title = {title: gid for gid, title in existing.items()}

    # Resolve each managed tab to a gid; plan renames and additions.
    requests, resolved = [], []
    for index, (gid, title, rows) in enumerate(TABS):
        if gid is None:
            gid = by_title.get(title)
        if gid is not None and title in by_title and by_title[title] != gid:
            sys.exit(f"Tab '{title}' exists with gid {by_title[title]}, but the tests need gid {gid}. "
                     f"Rename or delete it and re-run.")
        if gid is not None and gid in existing:
            props = {"sheetId": gid, "title": title, "index": index}
            requests.append({"updateSheetProperties": {"properties": props, "fields": "title,index"}})
        else:
            props = {"title": title, "index": index}
            if gid is not None:
                props["sheetId"] = gid
            requests.append({"addSheet": {"properties": props}})
        resolved.append((title, rows))

    call(token, "POST", base + ":batchUpdate", {"requests": requests})

    call(token, "POST", base + "/values:batchClear", {"ranges": [a1(t, "A:ZZ") for t, _ in resolved]})

    data = []
    for title, rows in resolved:
        if rows == "OVERWRITE_RANGE":
            data.append({"range": a1(title, "A2"), "values": [["leave this cell alone"]]})
            data.append({"range": a1(title, "H14"), "values": [["More leaving alone"]]})
        elif rows:
            data.append({"range": a1(title), "values": rows})
    call(token, "POST", base + "/values:batchUpdate", {"valueInputOption": "USER_ENTERED", "data": data})

    print(f"Fixture ready: {len(resolved)} tabs set up in spreadsheet {sid}")


if __name__ == "__main__":
    main()
