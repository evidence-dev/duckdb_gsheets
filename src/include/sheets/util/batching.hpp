#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "sheets/exception.hpp"

namespace duckdb {
namespace sheets {

//! Google recommends keeping a Sheets API request body under 2 MB
constexpr size_t DEFAULT_MAX_REQUEST_BYTES = 2 * 1024 * 1024;

//! Bytes a row takes in the "values" array of a request body: the row serialized as a JSON array of strings, plus
//! the comma that separates it from the next row
size_t JsonRowSize(const std::vector<std::string> &row);

//! Splits rows into consecutive [begin, end) ranges whose serialized size stays under max_bytes. A new range starts
//! only when adding the next row would go over the limit; a single row larger than the limit gets a range of its
//! own. No rows gives no ranges.
std::vector<std::pair<size_t, size_t>> PlanBatches(const std::vector<std::vector<std::string>> &rows,
                                                   size_t max_bytes = DEFAULT_MAX_REQUEST_BYTES);

struct RetryPolicy {
	//! Total attempts, including the first
	int max_attempts = 7;
	//! Wait before the first retry; doubled before each later one
	std::chrono::milliseconds initial_delay {1000};
	std::chrono::milliseconds max_delay {64000};
};

//! Runs request, and when it fails with HTTP 429 (rate limited) waits and runs it again with exponential backoff,
//! following https://developers.google.com/sheets/api/limits. A 429 means Google did not process the request, so
//! repeating a non-idempotent call such as an append is safe. Other errors, and a 429 on the last attempt, are
//! rethrown. sleep does the waiting, so callers can make it interruptible and tests can skip it.
template <typename F>
auto RetryOnRateLimit(F &&request, const RetryPolicy &policy,
                      const std::function<void(std::chrono::milliseconds)> &sleep) -> decltype(request()) {
	auto delay = policy.initial_delay;
	for (int attempt = 1;; attempt++) {
		try {
			return request();
		} catch (const SheetsApiException &e) {
			if (e.GetStatusCode() != 429 || attempt >= policy.max_attempts) {
				throw;
			}
		}
		sleep(delay);
		delay = std::min(delay * 2, policy.max_delay);
	}
}

} // namespace sheets
} // namespace duckdb
