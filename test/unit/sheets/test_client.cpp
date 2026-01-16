#include "catch.hpp"

#include "sheets/auth/bearer_token_auth.hpp"
#include "sheets/client.hpp"
#include "sheets/transport/mock_http_client.hpp"

// =============================================================================
// GoogleSheetsClient Tests
// =============================================================================

TEST_CASE("GoogleSheetsClient sets Authorization header", "[client]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"spreadsheetId": "abc123", "properties": {}, "sheets": []})"});

	duckdb::sheets::BearerTokenAuth auth("my-secret-token");
	duckdb::sheets::GoogleSheetsClient client(mockHttp, auth);

	client.Spreadsheets("abc123").Get();

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].headers.at("Authorization") == "Bearer my-secret-token");
}

TEST_CASE("GoogleSheetsClient sets Content-Type and Accept headers", "[client]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"spreadsheetId": "abc123", "properties": {}, "sheets": []})"});

	duckdb::sheets::BearerTokenAuth auth("token");
	duckdb::sheets::GoogleSheetsClient client(mockHttp, auth);

	client.Spreadsheets("abc123").Get();

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests[0].headers.at("Content-Type") == "application/json");
	REQUIRE(requests[0].headers.at("Accept") == "application/json");
}

TEST_CASE("GoogleSheetsClient uses default base URL", "[client]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"spreadsheetId": "abc123", "properties": {}, "sheets": []})"});

	duckdb::sheets::BearerTokenAuth auth("token");
	duckdb::sheets::GoogleSheetsClient client(mockHttp, auth);

	client.Spreadsheets("abc123").Get();

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests[0].url.find("https://sheets.googleapis.com/v4") == 0);
}

TEST_CASE("GoogleSheetsClient allows custom base URL", "[client]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"spreadsheetId": "abc123", "properties": {}, "sheets": []})"});

	duckdb::sheets::BearerTokenAuth auth("token");
	duckdb::sheets::GoogleSheetsClient client(mockHttp, auth, "https://proxy.example.com/sheets/v4");

	client.Spreadsheets("abc123").Get();

	auto requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests[0].url.find("https://proxy.example.com/sheets/v4") == 0);
}

TEST_CASE("GoogleSheetsClient chains to Values resource", "[client]") {
	duckdb::sheets::MockHttpClient mockHttp;
	mockHttp.AddResponse({200, {}, R"({"range": "Sheet1!A1", "values": [["test"]]})"});

	duckdb::sheets::BearerTokenAuth auth("token");
	duckdb::sheets::GoogleSheetsClient client(mockHttp, auth);

	auto result = client.Spreadsheets("abc123").Values().Get(duckdb::sheets::A1Range("Sheet1!A1"));

	REQUIRE(result.values[0][0] == "test");
}
