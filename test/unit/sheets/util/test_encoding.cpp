#include "catch.hpp"

#include "sheets/util/encoding.hpp"

TEST_CASE("Base64UrlEncode empty input", "[encoding]") {
	REQUIRE(duckdb::sheets::Base64UrlEncode("") == "");
}

TEST_CASE("Base64UrlEncode single character", "[encoding]") {
	// 'a' = 0x61 = 01100001
	// Split into: [011000] [01xxxx] -> 24, 16 (padded)
	// But without padding: just 2 chars
	REQUIRE(duckdb::sheets::Base64UrlEncode("a") == "YQ");
}

TEST_CASE("Base64UrlEncode two characters", "[encoding]") {
	// 'ab' = 0x61 0x62 = 01100001 01100010
	// Split into: [011000] [010110] [0010xx] -> 24, 22, 8
	// Without padding: 3 chars
	REQUIRE(duckdb::sheets::Base64UrlEncode("ab") == "YWI");
}

TEST_CASE("Base64UrlEncode three characters (exact group)", "[encoding]") {
	// 'abc' = 0x61 0x62 0x63
	// Exactly 3 bytes -> 4 chars, no edge case
	REQUIRE(duckdb::sheets::Base64UrlEncode("abc") == "YWJj");
}

TEST_CASE("Base64UrlEncode Hello", "[encoding]") {
	// 'Hello' should encode to 'SGVsbG8'
	// This is the example we walked through earlier
	REQUIRE(duckdb::sheets::Base64UrlEncode("Hello") == "SGVsbG8");
}

TEST_CASE("Base64UrlEncode Hello World", "[encoding]") {
	REQUIRE(duckdb::sheets::Base64UrlEncode("Hello World") == "SGVsbG8gV29ybGQ");
}

TEST_CASE("Base64UrlEncode binary data with high bytes", "[encoding]") {
	// Test bytes > 127 (high bit set)
	unsigned char data[] = {0xFF, 0x00, 0xFF};
	REQUIRE(duckdb::sheets::Base64UrlEncode(data, 3) == "_wD_");
}

TEST_CASE("Base64UrlEncode uses URL-safe alphabet", "[encoding]") {
	// Standard Base64 would produce + and /
	// Base64URL should produce - and _ instead
	// The bytes 0xFB, 0xEF, 0xBE produce '++' in standard Base64
	unsigned char data[] = {0xFB, 0xEF, 0xBE};
	std::string result = duckdb::sheets::Base64UrlEncode(data, 3);
	// Should not contain + or /
	REQUIRE(result.find('+') == std::string::npos);
	REQUIRE(result.find('/') == std::string::npos);
}

TEST_CASE("Base64UrlEncode no padding", "[encoding]") {
	// JWT Base64URL should NOT have '=' padding
	std::string result = duckdb::sheets::Base64UrlEncode("a");
	REQUIRE(result.find('=') == std::string::npos);

	result = duckdb::sheets::Base64UrlEncode("ab");
	REQUIRE(result.find('=') == std::string::npos);
}

TEST_CASE("Base64UrlEncode JWT header", "[encoding]") {
	// Real-world test: JWT header
	std::string header = R"({"alg":"RS256","typ":"JWT"})";
	REQUIRE(duckdb::sheets::Base64UrlEncode(header) == "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9");
}
