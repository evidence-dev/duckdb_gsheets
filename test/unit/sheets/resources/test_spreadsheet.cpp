#include "catch.hpp"

#include "sheets/exception.hpp"
#include "sheets/resources/spreadsheet.hpp"
#include "sheets/transport/mock_http_client.hpp"

// =============================================================================
// SpreadsheetResource::Get Tests
// =============================================================================

TEST_CASE("SpreadsheetResource::Get returns SpreadsheetMetadata on success", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({
		"spreadsheetId": "abc123",
		"properties": {
			"title": "My Spreadsheet",
			"locale": "en_US",
			"timeZone": "America/New_York"
		},
		"sheets": [
			{
				"properties": {
					"sheetId": 0,
					"title": "Sheet1",
					"index": 0,
					"sheetType": "GRID"
				}
			},
			{
				"properties": {
					"sheetId": 1,
					"title": "Sheet2",
					"index": 1,
					"sheetType": "GRID"
				}
			}
		]
	})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	auto result = spreadsheet.Get();

	REQUIRE(result.spreadsheetId == "abc123");
	REQUIRE(result.properties.title == "My Spreadsheet");
	REQUIRE(result.properties.locale == "en_US");
	REQUIRE(result.properties.timeZone == "America/New_York");
	REQUIRE(result.sheets.size() == 2);
	REQUIRE(result.sheets[0].properties.title == "Sheet1");
	REQUIRE(result.sheets[0].properties.sheetType == duckdb::sheets::GRID);
	REQUIRE(result.sheets[1].properties.title == "Sheet2");
}

TEST_CASE("SpreadsheetResource::Get builds correct URL", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"spreadsheetId": "abc123", "properties": {}, "sheets": []})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	spreadsheet.Get();

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].url == "https://sheets.googleapis.com/v4/spreadsheets/abc123");
	REQUIRE(requests[0].method == duckdb::sheets::HttpMethod::GET);
}

TEST_CASE("SpreadsheetResource::Get throws SheetsApiException on HTTP error", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({404, {}, R"({"error": {"message": "Spreadsheet not found"}})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	REQUIRE_THROWS_AS(spreadsheet.Get(), duckdb::sheets::SheetsApiException);
}

TEST_CASE("SpreadsheetResource::Get throws SheetsParseException on invalid JSON", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, "not valid json"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	REQUIRE_THROWS_AS(spreadsheet.Get(), duckdb::sheets::SheetsParseException);
}

// =============================================================================
// SpreadsheetResource::Values Tests
// =============================================================================

TEST_CASE("SpreadsheetResource::Values returns working ValuesResource", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	// Response for Values().Get()
	mockHttp.AddResponse({200, {}, R"({
		"range": "Sheet1!A1:B2",
		"majorDimension": "ROWS",
		"values": [["hello", "world"]]
	})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	auto valuesResult = spreadsheet.Values().Get(duckdb::sheets::A1Range("Sheet1!A1:B2"));

	REQUIRE(valuesResult.range == "Sheet1!A1:B2");
	REQUIRE(valuesResult.values[0][0] == "hello");

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].url == "https://sheets.googleapis.com/v4/spreadsheets/abc123/values/Sheet1!A1:B2");
}

// =============================================================================
// SpreadsheetResource::GetSheetById Tests
// =============================================================================

// Helper: common spreadsheet response with multiple sheets
static const char *const MULTI_SHEET_RESPONSE = R"({
	"spreadsheetId": "abc123",
	"properties": {"title": "Test Spreadsheet"},
	"sheets": [
		{"properties": {"sheetId": 0, "title": "First", "index": 0, "sheetType": "GRID"}},
		{"properties": {"sheetId": 42, "title": "Second", "index": 1, "sheetType": "GRID"}},
		{"properties": {"sheetId": 99, "title": "Third", "index": 2, "sheetType": "OBJECT"}}
	]
})";

TEST_CASE("SpreadsheetResource::GetSheetById returns correct sheet", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, MULTI_SHEET_RESPONSE});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	auto sheet = spreadsheet.GetSheetById(42);

	REQUIRE(sheet.properties.sheetId == 42);
	REQUIRE(sheet.properties.title == "Second");
}

TEST_CASE("SpreadsheetResource::GetSheetById throws when not found", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, MULTI_SHEET_RESPONSE});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	REQUIRE_THROWS_AS(spreadsheet.GetSheetById(999), duckdb::sheets::SheetNotFoundException);
}

// =============================================================================
// SpreadsheetResource::GetSheetByName Tests
// =============================================================================

TEST_CASE("SpreadsheetResource::GetSheetByName returns correct sheet", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, MULTI_SHEET_RESPONSE});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	auto sheet = spreadsheet.GetSheetByName("Third");

	REQUIRE(sheet.properties.title == "Third");
	REQUIRE(sheet.properties.sheetId == 99);
	REQUIRE(sheet.properties.sheetType == duckdb::sheets::OBJECT);
}

TEST_CASE("SpreadsheetResource::GetSheetByName throws when not found", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, MULTI_SHEET_RESPONSE});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	REQUIRE_THROWS_AS(spreadsheet.GetSheetByName("NonExistent"), duckdb::sheets::SheetNotFoundException);
}

// =============================================================================
// SpreadsheetResource::GetSheetByIndex Tests
// =============================================================================

TEST_CASE("SpreadsheetResource::GetSheetByIndex returns correct sheet", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, MULTI_SHEET_RESPONSE});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	auto sheet = spreadsheet.GetSheetByIndex(1);

	REQUIRE(sheet.properties.index == 1);
	REQUIRE(sheet.properties.title == "Second");
}

TEST_CASE("SpreadsheetResource::GetSheetByIndex throws when not found", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, MULTI_SHEET_RESPONSE});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	REQUIRE_THROWS_AS(spreadsheet.GetSheetByIndex(100), duckdb::sheets::SheetNotFoundException);
}

// =============================================================================
// SpreadsheetResource::GetSheetByIndex Tests
// =============================================================================

TEST_CASE("SpreadsheetResource::CreateSheet returns new sheet", "[spreadsheet]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({
	                     	"replies": [
	                     		{"addSheet": {"properties": {"title": "test1"}}}
	                     	]
	                     })"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::SpreadsheetResource spreadsheet(mockHttp, headers, "https://sheets.googleapis.com/v4", "abc123");

	auto sheet = spreadsheet.CreateSheet("test1");

	REQUIRE(sheet.properties.title == "test1");
}
