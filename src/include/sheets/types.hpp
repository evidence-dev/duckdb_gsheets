#pragma once

#include <string>
#include <vector>

#include "json.hpp"

namespace duckdb {
namespace sheets {

enum SheetType { SHEET_TYPE_UNSPECIFIED, GRID, OBJECT, DATA_SOURCE };

NLOHMANN_JSON_SERIALIZE_ENUM(SheetType, {
                                            {SHEET_TYPE_UNSPECIFIED, "SHEET_TYPE_UNSPECIFIED"},
                                            {GRID, "GRID"},
                                            {OBJECT, "OBJECT"},
                                            {DATA_SOURCE, "DATA_SOURCE"},
                                        })

struct SheetMetadataProperties {
	int sheetId = 0;
	std::string title = "";
	int index = 0;
	SheetType sheetType = SHEET_TYPE_UNSPECIFIED;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SheetMetadataProperties, sheetId, title, index, sheetType)

struct SheetMetadata {
	SheetMetadataProperties properties = {};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SheetMetadata, properties)

struct SpreadsheetMetadataProperties {
	std::string title = "";
	std::string locale = "";
	std::string timeZone = "";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpreadsheetMetadataProperties, title, locale, timeZone)

struct SpreadsheetMetadata {
	std::string spreadsheetId = "";
	SpreadsheetMetadataProperties properties = {};
	std::vector<SheetMetadata> sheets = {};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpreadsheetMetadata, spreadsheetId, properties, sheets)

enum MajorDimension { DIMENSION_UNSPECIFIED, ROWS, COLUMNS };

NLOHMANN_JSON_SERIALIZE_ENUM(MajorDimension, {
                                                 {DIMENSION_UNSPECIFIED, "DIMENSION_UNSPECIFIED"},
                                                 {ROWS, "ROWS"},
                                                 {COLUMNS, "COLUMNS"},
                                             })

struct ValueRange {
	std::string range = "";
	MajorDimension majorDimension = ROWS;
	std::vector<std::vector<std::string>> values = {};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ValueRange, range, majorDimension, values)

struct UpdateValuesResponse {
	std::string spreadsheetId = "";
	std::string updatedRange = "";
	int updatedRows = 0;
	int updatedColumns = 0;
	int updatedCells = 0;
	ValueRange updatedData = {};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UpdateValuesResponse, spreadsheetId, updatedRange, updatedRows,
                                                updatedColumns, updatedCells, updatedData)

struct AppendValuesResponse {
	std::string spreadsheetId = "";
	std::string tableRange = "";
	UpdateValuesResponse updates = {};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppendValuesResponse, spreadsheetId, tableRange, updates)

struct ClearValuesResponse {
	std::string spreadsheetId;
	std::string clearedRange;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClearValuesResponse, spreadsheetId, clearedRange)

} // namespace sheets
} // namespace duckdb
