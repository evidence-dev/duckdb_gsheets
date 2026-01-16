#pragma once

#include <string>
#include <vector>

#include "json.hpp"

namespace duckdb {
namespace sheets {

enum SheetType { SHEET_TYPE_UNSPECIFIED, GRID, OBJECT, DATA_SOURCE };

struct SheetMetadataProperties {
	int sheetId = 0;
	std::string title = "";
	int index = 0;
	SheetType sheetType = SHEET_TYPE_UNSPECIFIED;
};

struct SheetMetadata {
	SheetMetadataProperties properties = {};
};

struct SpreadsheetMetadataProperties {
	std::string title = "";
	std::string locale = "";
	std::string timeZone = "";
};

struct SpreadsheetMetadata {
	std::string spreadsheetId = "";
	SpreadsheetMetadataProperties properties = {};
	std::vector<SheetMetadata> sheets = {};
};

enum MajorDimension { DIMENSION_UNSPECIFIED, ROWS, COLUMNS };

struct ValueRange {
	std::string range = "";
	MajorDimension majorDimension = ROWS;
	std::vector<std::vector<std::string>> values = {};
};

struct UpdateResponse {
	std::string spreadsheetId = "";
	std::string updatedRange = "";
	int updatedRows = 0;
	int updatedColumns = 0;
	int updatedCells = 0;
};

struct AppendResponse {
	std::string spreadsheetId = "";
	std::string tableRange = "";
	int updatedRows = 0;
	int updatedColumns = 0;
	int updatedCells = 0;
};

struct ClearResponse {
	std::string spreadsheetId = "";
	std::string clearedRange = "";
};

NLOHMANN_JSON_SERIALIZE_ENUM(SheetType, {
                                            {SHEET_TYPE_UNSPECIFIED, "SHEET_TYPE_UNSPECIFIED"},
                                            {GRID, "GRID"},
                                            {OBJECT, "OBJECT"},
                                            {DATA_SOURCE, "DATA_SOURCE"},
                                        })

NLOHMANN_JSON_SERIALIZE_ENUM(MajorDimension, {
                                                 {DIMENSION_UNSPECIFIED, "DIMENSION_UNSPECIFIED"},
                                                 {ROWS, "ROWS"},
                                                 {COLUMNS, "COLUMNS"},
                                             })

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SheetMetadataProperties, sheetId, title, index, sheetType)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SheetMetadata, properties)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpreadsheetMetadataProperties, title, locale, timeZone)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SpreadsheetMetadata, spreadsheetId, properties, sheets)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ValueRange, range, majorDimension, values)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UpdateResponse, spreadsheetId, updatedRange, updatedRows, updatedColumns, updatedCells)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AppendResponse, spreadsheetId, tableRange, updatedRows, updatedColumns, updatedCells)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClearResponse, spreadsheetId, clearedRange)
} // namespace sheets
} // namespace duckdb
