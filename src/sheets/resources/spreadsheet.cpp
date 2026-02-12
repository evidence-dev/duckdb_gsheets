#include <string>

#include "sheets/resources/spreadsheet.hpp"
#include "sheets/exception.hpp"
#include "sheets/types.hpp"
#include "sheets/util/response.hpp"

namespace duckdb {
namespace sheets {

SpreadsheetMetadata SpreadsheetResource::Get() {
	std::string path = "/spreadsheets/" + spreadsheetId;
	return ParseResponse<SpreadsheetMetadata>(DoGet(path));
}

SheetMetadata SpreadsheetResource::GetSheetById(int sheetId) {
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

SheetMetadata SpreadsheetResource::GetSheetByIndex(int index) {
	auto meta = Get();
	for (const auto &sheet : meta.sheets) {
		if (sheet.properties.index == index) {
			return sheet;
		}
	}
	throw SheetNotFoundException(std::to_string(index));
}

ValuesResource SpreadsheetResource::Values() {
	return ValuesResource(http, headers, baseUrl, spreadsheetId);
}

} // namespace sheets
} // namespace duckdb
