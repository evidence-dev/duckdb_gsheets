#include <random>
#include <regex>
#include <sstream>

#include "duckdb/common/exception.hpp"

#include "gsheets_utils.hpp"

namespace duckdb {

std::string extract_spreadsheet_id(const std::string &input) {
	// Check if the input is already a sheet ID (no slashes)
	if (input.find('/') == std::string::npos) {
		return input;
	}

	// Regular expression to match the spreadsheet ID in a Google Sheets URL
	if (input.find("docs.google.com/spreadsheets/d/") != std::string::npos) {
		std::regex spreadsheet_id_regex("/d/([a-zA-Z0-9-_]+)");
		std::smatch match;

		if (std::regex_search(input, match, spreadsheet_id_regex) && match.size() > 1) {
			return match.str(1);
		}
	}

	throw duckdb::InvalidInputException("Invalid Google Sheets URL or ID");
}

std::string extract_sheet_id(const std::string &input) {
	if (input.find("docs.google.com/spreadsheets/d/") != std::string::npos && input.find("gid=") != std::string::npos) {
		std::regex sheet_id_regex("gid=([0-9]+)");
		std::smatch match;
		if (std::regex_search(input, match, sheet_id_regex) && match.size() > 1) {
			return match.str(1);
		}
	}
	return "";
}

std::string extract_sheet_range(const std::string &input) {
	if (input.find("docs.google.com/spreadsheets/d/") != std::string::npos &&
	    input.find("range=") != std::string::npos) {
		std::regex sheet_range_regex("range=([^&]+)");
		std::smatch match;
		if (std::regex_search(input, match, sheet_range_regex) && match.size() > 1) {
			return match.str(1);
		}
	}
	return "";
}

std::string generate_random_string(size_t length) {
	static const char charset[] = "0123456789"
	                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                              "abcdefghijklmnopqrstuvwxyz";

	std::random_device rd;
	std::mt19937 generator(rd());
	std::uniform_int_distribution<> distribution(0, sizeof(charset) - 2);

	std::string result;
	result.reserve(length);
	for (size_t i = 0; i < length; ++i) {
		result.push_back(charset[distribution(generator)]);
	}
	return result;
}

std::string url_encode(const std::string &str) {
	std::string encoded;
	for (char c : str) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			encoded += c;
		} else {
			std::stringstream ss;
			ss << std::hex << std::uppercase << static_cast<int>(static_cast<unsigned char>(c));
			encoded += '%' + ss.str();
		}
	}
	return encoded;
}

} // namespace duckdb
