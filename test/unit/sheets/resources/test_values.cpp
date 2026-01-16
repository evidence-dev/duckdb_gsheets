#include "catch.hpp"

#include "sheets/exception.hpp"
#include "sheets/resources/values.hpp"
#include "sheets/transport/mock_http_client.hpp"

// =============================================================================
// ValuesResource::Get Tests
// =============================================================================

TEST_CASE("ValuesResource::Get returns ValueRange on success", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({
		"range": "Sheet1!A1:B2",
		"majorDimension": "ROWS",
		"values": [["a", "b"], ["c", "d"]]
	})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	auto result = values.Get(duckdb::sheets::A1Range("Sheet1!A1:B2"));

	REQUIRE(result.range == "Sheet1!A1:B2");
	REQUIRE(result.majorDimension == duckdb::sheets::ROWS);
	REQUIRE(result.values.size() == 2);
	REQUIRE(result.values[0][0] == "a");
}

TEST_CASE("ValuesResource::Get builds correct URL", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"range": "", "values": []})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	values.Get(duckdb::sheets::A1Range("Sheet1!A1:B2"));

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].url == "https://sheets.googleapis.com/v4/spreadsheets/spreadsheet123/values/Sheet1!A1:B2");
	REQUIRE(requests[0].method == duckdb::sheets::HttpMethod::GET);
}

TEST_CASE("ValuesResource::Get throws SheetsApiException on HTTP error", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({404, {}, R"({"error": {"message": "Not found"}})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	REQUIRE_THROWS_AS(values.Get(duckdb::sheets::A1Range("Sheet1!A1")), duckdb::sheets::SheetsApiException);
}

TEST_CASE("ValuesResource::Get throws SheetsParseException on invalid JSON", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, "not valid json"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	REQUIRE_THROWS_AS(values.Get(duckdb::sheets::A1Range("Sheet1!A1")), duckdb::sheets::SheetsParseException);
}

// =============================================================================
// ValuesResource::Update Tests
// =============================================================================

TEST_CASE("ValuesResource::Update sends PUT with correct body", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({
		"spreadsheetId": "spreadsheet123",
		"updatedRange": "Sheet1!A1:B2",
		"updatedRows": 2,
		"updatedColumns": 2,
		"updatedCells": 4
	})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	duckdb::sheets::ValueRange input;
	input.range = "Sheet1!A1:B2";
	input.values = {{"x", "y"}, {"z", "w"}};

	auto result = values.Update(duckdb::sheets::A1Range("Sheet1!A1:B2"), input);

	REQUIRE(result.updatedCells == 4);

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].method == duckdb::sheets::HttpMethod::PUT);
	REQUIRE(requests[0].url.find("valueInputOption=USER_ENTERED") != std::string::npos);
	REQUIRE(requests[0].body.find("\"values\"") != std::string::npos);
}

// =============================================================================
// ValuesResource::Append Tests
// =============================================================================

TEST_CASE("ValuesResource::Append sends POST to :append endpoint", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({
		"spreadsheetId": "spreadsheet123",
		"tableRange": "Sheet1!A1:B2"
	})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	duckdb::sheets::ValueRange input;
	input.values = {{"new", "row"}};

	auto result = values.Append(duckdb::sheets::A1Range("Sheet1!A1"), input);

	REQUIRE(result.spreadsheetId == "spreadsheet123");

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].method == duckdb::sheets::HttpMethod::POST);
	REQUIRE(requests[0].url.find(":append") != std::string::npos);
	REQUIRE(requests[0].url.find("valueInputOption=USER_ENTERED") != std::string::npos);
}

// =============================================================================
// ValuesResource::Clear Tests
// =============================================================================

TEST_CASE("ValuesResource::Clear sends POST to :clear endpoint", "[values]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({
		"spreadsheetId": "spreadsheet123",
		"clearedRange": "Sheet1!A1:B2"
	})"});

	duckdb::sheets::HttpHeaders headers;
	duckdb::sheets::ValuesResource values(mockHttp, headers, "https://sheets.googleapis.com/v4", "spreadsheet123");

	auto result = values.Clear(duckdb::sheets::A1Range("Sheet1!A1:B2"));

	REQUIRE(result.spreadsheetId == "spreadsheet123");
	REQUIRE(result.clearedRange == "Sheet1!A1:B2");

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].method == duckdb::sheets::HttpMethod::POST);
	REQUIRE(requests[0].url.find(":clear") != std::string::npos);
	REQUIRE(requests[0].body == "{}");
}
