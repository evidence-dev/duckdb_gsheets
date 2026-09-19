// Minimal stubs for DuckDB types used in unit tests
// These avoid linking the full DuckDB library for fast unit test builds

#include "duckdb/common/exception.hpp"

namespace duckdb {

Exception::Exception(ExceptionType exception_type, const string &message)
    : std::runtime_error(message) {
}

Exception::Exception(const unordered_map<string, string> &extra_info, ExceptionType exception_type,
                     const string &message)
    : std::runtime_error(message) {
}

string Exception::ExceptionTypeToString(ExceptionType type) {
	return "UNKNOWN";
}

IOException::IOException(const string &msg) : Exception(ExceptionType::IO, msg) {
}

IOException::IOException(const unordered_map<string, string> &extra_info, const string &msg)
    : Exception(extra_info, ExceptionType::IO, msg) {
}

InterruptException::InterruptException() : Exception(ExceptionType::INTERRUPT, INTERRUPT_MESSAGE) {
}

InvalidInputException::InvalidInputException(const string &msg) : Exception(ExceptionType::INVALID_INPUT, msg) {
}

InvalidInputException::InvalidInputException(const unordered_map<string, string> &extra_info, const string &msg)
    : Exception(extra_info, ExceptionType::INVALID_INPUT, msg) {
}

} // namespace duckdb
