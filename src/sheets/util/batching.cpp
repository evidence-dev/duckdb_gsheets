#include "sheets/util/batching.hpp"

namespace duckdb {
namespace sheets {

namespace {

// Bytes nlohmann::json::dump() writes for s as a JSON string, quotes included. It leaves UTF-8 as is, escapes '"'
// and '\\' and the named control characters with two bytes, and the other control characters as \u00XX.
size_t JsonStringSize(const std::string &s) {
	size_t size = 2;
	for (unsigned char c : s) {
		switch (c) {
		case '"':
		case '\\':
		case '\b':
		case '\f':
		case '\n':
		case '\r':
		case '\t':
			size += 2;
			break;
		default:
			size += c < 0x20 ? 6 : 1;
		}
	}
	return size;
}

} // namespace

size_t JsonRowSize(const std::vector<std::string> &row) {
	// "[" + "]" + the comma after the row, and a comma between cells
	size_t size = 3 + (row.empty() ? 0 : row.size() - 1);
	for (auto &cell : row) {
		size += JsonStringSize(cell);
	}
	return size;
}

std::vector<std::pair<size_t, size_t>> PlanBatches(const std::vector<std::vector<std::string>> &rows,
                                                   size_t max_bytes) {
	// Room for the rest of the body: {"range":"...","majorDimension":"ROWS","values":[...]}
	constexpr size_t ENVELOPE_BYTES = 1024;
	size_t budget = max_bytes > ENVELOPE_BYTES ? max_bytes - ENVELOPE_BYTES : 0;

	std::vector<std::pair<size_t, size_t>> batches;
	size_t begin = 0;
	size_t batch_bytes = 0;
	for (size_t i = 0; i < rows.size(); i++) {
		size_t row_bytes = JsonRowSize(rows[i]);
		if (i > begin && batch_bytes + row_bytes > budget) {
			batches.emplace_back(begin, i);
			begin = i;
			batch_bytes = 0;
		}
		batch_bytes += row_bytes;
	}
	if (begin < rows.size()) {
		batches.emplace_back(begin, rows.size());
	}
	return batches;
}

} // namespace sheets
} // namespace duckdb
