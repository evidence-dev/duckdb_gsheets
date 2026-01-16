#pragma once

#include "sheets/resources/base.hpp"
#include "sheets/resources/values.hpp"
#include "sheets/types.hpp"

namespace duckdb {
namespace sheets {

class SpreadsheetResource : protected BaseResource {
public:
	SpreadsheetResource(IHttpClient &http, const HttpHeaders &headers, const std::string &baseUrl,
	                    const std::string &spreadsheetId)
	    : BaseResource(http, headers, baseUrl), spreadsheetId(spreadsheetId) {};

	SpreadsheetMetadata Get();

	SheetMetadata GetSheetById(int sheetId);
	SheetMetadata GetSheetByName(const std::string &name);
	SheetMetadata GetSheetByIndex(int index);

	ValuesResource Values();

private:
	std::string spreadsheetId;
};

} // namespace sheets
} // namespace duckdb
