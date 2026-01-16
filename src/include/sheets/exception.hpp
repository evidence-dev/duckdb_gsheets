#pragma once

#include <stdexcept>
#include <string>

namespace duckdb {
namespace sheets {

class SheetsException : public std::runtime_error {
public:
	explicit SheetsException(const std::string &message) : std::runtime_error(message) {
	}
};

class SheetsApiException : public SheetsException {
public:
	SheetsApiException(int statusCode, const std::string &apiMessage)
	    : SheetsException("Google Sheets API error (" + std::to_string(statusCode) + "): " + apiMessage),
	      statusCode(statusCode),
	      apiMessage(apiMessage) {
	}

	int GetStatusCode() const {
		return statusCode;
	}

	const std::string &GetApiMessage() const {
		return apiMessage;
	}

private:
	int statusCode;
	std::string apiMessage;
};

class SheetsParseException : public SheetsException {
public:
	explicit SheetsParseException(const std::string &message) : SheetsException(message) {
	}
};

} // namespace sheets
} // namespace duckdb
