#include <string>

#include "json.hpp"

#include "sheets/resources/spreadsheet.hpp"
#include "sheets/exception.hpp"
#include "sheets/resources/values.hpp"
#include "sheets/types.hpp"
#include "sheets/util/response.hpp"

using json = nlohmann::json;

namespace duckdb {
namespace sheets {

SpreadsheetMetadata SpreadsheetResource::Get() {
	std::string path = "/spreadsheets/" + spreadsheetId;
	return ParseResponse<SpreadsheetMetadata>(DoGet(path));
}

SheetMetadata SpreadsheetResource::GetSheetById(const int sheetId) {
	auto meta = Get();
	for (const auto &sheet : meta.sheets) {
		if (sheet.properties.sheetId == sheetId) {
			return sheet;
		}
	}
	throw SheetNotFoundException(std::to_string(sheetId));
}

SheetMetadata SpreadsheetResource::GetSheetById(const std::string &sheetId) {
	int id = std::stoi(sheetId);
	return GetSheetById(id);
}

SheetMetadata SpreadsheetResource::GetSheetByName(const std::string &name) {
	auto meta = Get();
	for (const auto &sheet : meta.sheets) {
		if (sheet.properties.title == name) {
			return sheet;
		}
	}
	throw SheetNotFoundException(name);
}

SheetMetadata SpreadsheetResource::GetSheetByIndex(const int index) {
	auto meta = Get();
	for (const auto &sheet : meta.sheets) {
		if (sheet.properties.index == index) {
			return sheet;
		}
	}
	throw SheetNotFoundException(std::to_string(index));
}

SheetMetadata SpreadsheetResource::CreateSheet(const std::string &name) {
	SpreadsheetUpdateRequest update;
	update.addSheet.properties.title = name;

	SpreadsheetBatchUpdateRequest req;
	req.requests.push_back(update);

	SpreadsheetBatchUpdateResponse res = BatchUpdate(req);
	if (res.replies.empty()) {
		throw SheetNotCreatedException(name);
	}
	auto reply = res.replies.front();
	return reply.addSheet;
}

SpreadsheetBatchUpdateResponse SpreadsheetResource::BatchUpdate(const SpreadsheetBatchUpdateRequest &req) {
	std::string path = "/spreadsheets/" + spreadsheetId + ":batchUpdate";
	std::string body = json(req).dump();
	return ParseResponse<SpreadsheetBatchUpdateResponse>(DoPost(path, body));
}

ValuesResource SpreadsheetResource::Values() {
	return ValuesResource(http, headers, baseUrl, spreadsheetId);
}

} // namespace sheets
} // namespace duckdb
