#include "catch.hpp"

#include "json.hpp"

#include "sheets/util/batching.hpp"

using namespace duckdb::sheets;
using Rows = std::vector<std::vector<std::string>>;

namespace {

Rows MakeRows(size_t count, const std::string &cell) {
	return Rows(count, std::vector<std::string> {cell, cell, cell});
}

size_t SerializedSize(const Rows &rows, size_t begin, size_t end) {
	Rows slice(rows.begin() + begin, rows.begin() + end);
	return nlohmann::json(slice).dump().size();
}

} // namespace

TEST_CASE("JsonRowSize matches what nlohmann::json writes", "[batching]") {
	std::vector<std::vector<std::string>> rows = {
	    {},
	    {""},
	    {"abc", "de"},
	    {"quote \" and backslash \\", "tab\tnewline\n"},
	    {std::string("\x01\x1f", 2), "caf\xc3\xa9"},
	};
	for (auto &row : rows) {
		// The serialized row plus the comma that follows it in the values array
		REQUIRE(JsonRowSize(row) == nlohmann::json(row).dump().size() + 1);
	}
}

TEST_CASE("PlanBatches", "[batching]") {
	SECTION("no rows gives no batches") {
		REQUIRE(PlanBatches({}).empty());
	}

	SECTION("rows that fit go in one batch") {
		auto rows = MakeRows(1000, "value");
		auto batches = PlanBatches(rows);
		REQUIRE(batches.size() == 1);
		REQUIRE(batches[0] == std::make_pair<size_t, size_t>(0, 1000));
	}

	SECTION("batches are contiguous, in order, cover every row and stay under the limit") {
		auto rows = MakeRows(50000, std::string(40, 'x'));
		const size_t max_bytes = 256 * 1024;
		auto batches = PlanBatches(rows, max_bytes);
		REQUIRE(batches.size() > 1);
		size_t next = 0;
		for (auto &batch : batches) {
			REQUIRE(batch.first == next);
			REQUIRE(batch.second > batch.first);
			REQUIRE(SerializedSize(rows, batch.first, batch.second) < max_bytes);
			next = batch.second;
		}
		REQUIRE(next == rows.size());
		// Splits only when needed: no two neighbouring batches would have fit together
		for (size_t i = 0; i + 1 < batches.size(); i++) {
			REQUIRE(SerializedSize(rows, batches[i].first, batches[i + 1].second) > max_bytes - 1024);
		}
	}

	SECTION("a row larger than the limit is sent on its own") {
		Rows rows = {{"a"}, {std::string(10000, 'x')}, {"b"}};
		auto batches = PlanBatches(rows, 2048);
		REQUIRE(batches == std::vector<std::pair<size_t, size_t>> {{0, 1}, {1, 2}, {2, 3}});
	}
}

TEST_CASE("RetryOnRateLimit", "[batching]") {
	std::vector<std::chrono::milliseconds> waits;
	auto sleep = [&waits](std::chrono::milliseconds duration) {
		waits.push_back(duration);
	};
	RetryPolicy policy;
	policy.max_attempts = 4;
	policy.initial_delay = std::chrono::milliseconds(100);
	policy.max_delay = std::chrono::milliseconds(250);

	SECTION("a request that succeeds runs once") {
		int calls = 0;
		REQUIRE(RetryOnRateLimit(
		            [&]() {
			            calls++;
			            return 42;
		            },
		            policy, sleep) == 42);
		REQUIRE(calls == 1);
		REQUIRE(waits.empty());
	}

	SECTION("a 429 is retried with growing waits until it succeeds") {
		int calls = 0;
		auto result = RetryOnRateLimit(
		    [&]() {
			    if (++calls < 4) {
				    throw SheetsApiException(429, "rate limited");
			    }
			    return calls;
		    },
		    policy, sleep);
		REQUIRE(result == 4);
		REQUIRE(waits == std::vector<std::chrono::milliseconds> {std::chrono::milliseconds(100),
		                                                         std::chrono::milliseconds(200),
		                                                         std::chrono::milliseconds(250)});
	}

	SECTION("a 429 on the last attempt is rethrown") {
		int calls = 0;
		REQUIRE_THROWS_AS(RetryOnRateLimit(
		                      [&]() -> int {
			                      calls++;
			                      throw SheetsApiException(429, "rate limited");
		                      },
		                      policy, sleep),
		                  SheetsApiException);
		REQUIRE(calls == 4);
		REQUIRE(waits.size() == 3);
	}

	SECTION("other errors are not retried") {
		int calls = 0;
		REQUIRE_THROWS_AS(RetryOnRateLimit(
		                      [&]() -> int {
			                      calls++;
			                      throw SheetsApiException(500, "backend error");
		                      },
		                      policy, sleep),
		                  SheetsApiException);
		REQUIRE(calls == 1);
		REQUIRE(waits.empty());
	}
}
