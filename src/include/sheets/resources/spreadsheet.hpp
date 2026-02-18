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

	SheetMetadata GetSheetById(const int sheetId);
	SheetMetadata GetSheetById(const std::string &sheetId);
	SheetMetadata GetSheetByName(const std::string &name);
	SheetMetadata GetSheetByIndex(const int index);

	SheetMetadata CreateSheet(const std::string &name);

	ValuesResource Values();

private:
	std::string spreadsheetId;

	SpreadsheetBatchUpdateResponse BatchUpdate(const SpreadsheetBatchUpdateRequest &req);
};

} // namespace sheets
} // namespace duckdb
