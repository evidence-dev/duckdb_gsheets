#pragma once

#include <string>

namespace duckdb {
namespace sheets {

class A1Range {
public:
	explicit A1Range(const std::string &range) : range(range) {
	}

	const std::string ToString() const {
		return range;
	}

	bool IsValid();

private:
	std::string range;
};

} // namespace sheets
} // namespace duckdb
