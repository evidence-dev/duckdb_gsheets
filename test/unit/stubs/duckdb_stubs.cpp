// Minimal stubs for DuckDB types used in unit tests
// These avoid linking the full DuckDB library for fast unit test builds

#include "duckdb/common/exception.hpp"

namespace duckdb {

Exception::Exception(ExceptionType exception_type, const string &message)
    : std::runtime_error(message) {
}

Exception::Exception(ExceptionType exception_type, const string &message,
                     const unordered_map<string, string> &extra_info)
    : std::runtime_error(message) {
}

string Exception::ExceptionTypeToString(ExceptionType type) {
	return "UNKNOWN";
}

IOException::IOException(const string &msg) : Exception(ExceptionType::IO, msg) {
}

IOException::IOException(const string &msg, const unordered_map<string, string> &extra_info)
    : Exception(ExceptionType::IO, msg, extra_info) {
}

} // namespace duckdb
