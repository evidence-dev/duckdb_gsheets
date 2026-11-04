#include "catch.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "gsheets_copy.hpp"

using namespace duckdb;

// Runs COPY through DuckDB's real execution paths with the gsheet_write callback set, so a callback that the
// engine requires but the copy function leaves unset fails here instead of on a user's query. Sink is the real one;
// only the two callbacks that call the Sheets API are swapped for stubs, and the Finalize stub records the rows
// Sink collected instead of sending them.

namespace {

std::mutex sunk_mutex;
std::vector<int64_t> sunk_values;
int finalize_calls = 0;

constexpr const char *TEST_FORMAT = "gsheet_write_test";

unique_ptr<GlobalFunctionData> StubInitializeGlobal(ClientContext &context, FunctionData &, const string &) {
	return make_uniq<GSheetCopyGlobalState>(context, nullptr, nullptr, "spreadsheet-id", "Sheet1");
}

void StubFinalize(ClientContext &, FunctionData &, GlobalFunctionData &gstate_p) {
	auto &gstate = gstate_p.Cast<GSheetCopyGlobalState>();
	std::lock_guard<std::mutex> guard(sunk_mutex);
	finalize_calls++;
	for (auto &row : gstate.rows) {
		sunk_values.push_back(std::stoll(row.at(0)));
	}
}

CopyFunction MakeTestCopyFunction() {
	CopyFunction function = GSheetCopyFunction();
	function.name = TEST_FORMAT;
	function.copy_to_initialize_global = StubInitializeGlobal;
	function.copy_to_finalize = StubFinalize;
	return function;
}

void RunCopy(Connection &con, const string &query) {
	{
		std::lock_guard<std::mutex> guard(sunk_mutex);
		sunk_values.clear();
		finalize_calls = 0;
	}
	auto result = con.Query("COPY (" + query + ") TO 'spreadsheet-id' (FORMAT " + TEST_FORMAT + ", sheet 'Sheet1')");
	if (result->HasError()) {
		FAIL(result->GetError());
	}
}

std::vector<int64_t> Range(int64_t start, int64_t end, int64_t step) {
	std::vector<int64_t> values;
	for (int64_t value = start; step > 0 ? value < end : value > end; value += step) {
		values.push_back(value);
	}
	return values;
}

} // namespace

TEST_CASE("gsheet_write runs through DuckDB's COPY execution paths", "[copy_function]") {
	DuckDB db(nullptr);
	ExtensionLoader loader(*db.instance, "gsheets_copy_test");
	loader.RegisterFunction(MakeTestCopyFunction());

	Connection con(db);
	REQUIRE_FALSE(con.Query("SET threads = 4")->HasError());

	SECTION("ORDER BY keeps row order through the batched copy path") {
		RunCopy(con, "SELECT i FROM range(10000) t(i) ORDER BY i DESC");
		REQUIRE(sunk_values == Range(9999, -1, -1));
	}

	SECTION("insertion order is kept for a plain multi-vector query") {
		RunCopy(con, "SELECT i FROM range(10000) t(i)");
		REQUIRE(sunk_values == Range(0, 10000, 1));
		REQUIRE(finalize_calls == 1);
	}

	SECTION("every row arrives when insertion order is not preserved") {
		REQUIRE_FALSE(con.Query("SET preserve_insertion_order = false")->HasError());
		RunCopy(con, "SELECT i FROM range(10000) t(i)");
		std::sort(sunk_values.begin(), sunk_values.end());
		REQUIRE(sunk_values == Range(0, 10000, 1));
	}

	SECTION("an empty result writes nothing") {
		RunCopy(con, "SELECT i FROM range(0) t(i) ORDER BY i");
		REQUIRE(sunk_values.empty());
	}
}
