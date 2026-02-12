#include "catch.hpp"

#include "sheets/range.hpp"

using duckdb::sheets::A1Range;

// =============================================================================
// Valid A1 Notation Tests
// =============================================================================

TEST_CASE("A1Range validates single cell reference", "[range]") {
	REQUIRE(A1Range("A1").IsValid());
	REQUIRE(A1Range("B2").IsValid());
	REQUIRE(A1Range("Z99").IsValid());
	REQUIRE(A1Range("AA100").IsValid());
	REQUIRE(A1Range("XFD1048576").IsValid()); // Max Excel range
}

TEST_CASE("A1Range validates cell range", "[range]") {
	REQUIRE(A1Range("A1:B2").IsValid());
	REQUIRE(A1Range("A1:Z99").IsValid());
	REQUIRE(A1Range("AA1:ZZ100").IsValid());
}

TEST_CASE("A1Range validates column-only range", "[range]") {
	REQUIRE(A1Range("A:A").IsValid());
	REQUIRE(A1Range("A:Z").IsValid());
	REQUIRE(A1Range("AA:ZZ").IsValid());
}

TEST_CASE("A1Range validates row-only range", "[range]") {
	REQUIRE(A1Range("1:1").IsValid());
	REQUIRE(A1Range("1:100").IsValid());
	REQUIRE(A1Range("5:10").IsValid());
}

TEST_CASE("A1Range validates mixed range (cell to column)", "[range]") {
	REQUIRE(A1Range("A5:A").IsValid());
	REQUIRE(A1Range("B10:B").IsValid());
}

TEST_CASE("A1Range validates sheet name with cell reference", "[range]") {
	REQUIRE(A1Range("Sheet1!A1").IsValid());
	REQUIRE(A1Range("Sheet1!A1:B2").IsValid());
	REQUIRE(A1Range("Data!A:A").IsValid());
	REQUIRE(A1Range("MySheet!1:5").IsValid());
}

TEST_CASE("A1Range validates unquoted sheet name only (whole sheet)", "[range]") {
	REQUIRE(A1Range("Sheet1").IsValid());
	REQUIRE(A1Range("Data").IsValid());
	REQUIRE(A1Range("MySheet2024").IsValid());
}

TEST_CASE("A1Range validates quoted sheet name", "[range]") {
	REQUIRE(A1Range("'My Sheet'!A1").IsValid());
	REQUIRE(A1Range("'My Sheet'!A1:B2").IsValid());
	REQUIRE(A1Range("'Sheet With Spaces'!A:A").IsValid());
}

TEST_CASE("A1Range validates quoted sheet name only (whole sheet)", "[range]") {
	REQUIRE(A1Range("'My Sheet'").IsValid());
	REQUIRE(A1Range("'Sheet With Spaces'").IsValid());
}

TEST_CASE("A1Range validates escaped apostrophe in sheet name", "[range]") {
	REQUIRE(A1Range("'Jon''s Data'!A1").IsValid());
	REQUIRE(A1Range("'It''s a sheet'!A1:B2").IsValid());
	REQUIRE(A1Range("'Multiple''quotes''here'!A1").IsValid());
}

// =============================================================================
// Absolute Reference Tests
// =============================================================================

TEST_CASE("A1Range validates absolute column reference", "[range]") {
	REQUIRE(A1Range("$A1").IsValid());
	REQUIRE(A1Range("$Z99").IsValid());
	REQUIRE(A1Range("$AA100").IsValid());
}

TEST_CASE("A1Range validates absolute row reference", "[range]") {
	REQUIRE(A1Range("A$1").IsValid());
	REQUIRE(A1Range("Z$99").IsValid());
	REQUIRE(A1Range("AA$100").IsValid());
}

TEST_CASE("A1Range validates fully absolute reference", "[range]") {
	REQUIRE(A1Range("$A$1").IsValid());
	REQUIRE(A1Range("$Z$99").IsValid());
	REQUIRE(A1Range("$AA$100").IsValid());
}

TEST_CASE("A1Range validates absolute references in ranges", "[range]") {
	REQUIRE(A1Range("$A$1:$B$2").IsValid());
	REQUIRE(A1Range("$A1:B$2").IsValid());
	REQUIRE(A1Range("A$1:$B2").IsValid());
	REQUIRE(A1Range("$A:$B").IsValid());       // Absolute column range
	REQUIRE(A1Range("$A$1:B2").IsValid());     // Mixed: first absolute, second relative
}

TEST_CASE("A1Range validates absolute references with sheet names", "[range]") {
	REQUIRE(A1Range("Sheet1!$A$1").IsValid());
	REQUIRE(A1Range("Sheet1!$A1:$B2").IsValid());
	REQUIRE(A1Range("'My Sheet'!$A$1:$B$2").IsValid());
}

TEST_CASE("A1Range rejects invalid absolute reference syntax", "[range]") {
	REQUIRE_FALSE(A1Range("$$A1").IsValid());    // Double dollar before column
	REQUIRE_FALSE(A1Range("A$$1").IsValid());    // Double dollar before row
	REQUIRE_FALSE(A1Range("$1").IsValid());      // Dollar with just row (no column)
	REQUIRE_FALSE(A1Range("$").IsValid());       // Just dollar
	REQUIRE_FALSE(A1Range("A1$").IsValid());     // Trailing dollar
	REQUIRE_FALSE(A1Range("$:A").IsValid());     // Dollar before colon
}

// =============================================================================
// Invalid A1 Notation Tests
// =============================================================================

TEST_CASE("A1Range rejects empty string", "[range]") {
	REQUIRE_FALSE(A1Range("").IsValid());
}

TEST_CASE("A1Range rejects invalid characters", "[range]") {
	REQUIRE_FALSE(A1Range("A1#B2").IsValid());
	REQUIRE_FALSE(A1Range("A1@").IsValid());
	REQUIRE_FALSE(A1Range("A1 B2").IsValid()); // Space without quotes
}

TEST_CASE("A1Range rejects unclosed quote", "[range]") {
	REQUIRE_FALSE(A1Range("'Unclosed").IsValid());
	REQUIRE_FALSE(A1Range("'Sheet!A1").IsValid());
}

TEST_CASE("A1Range rejects dangling colon", "[range]") {
	REQUIRE_FALSE(A1Range("A1:").IsValid());
	REQUIRE_FALSE(A1Range(":A1").IsValid());
	REQUIRE_FALSE(A1Range("A:").IsValid());
}

TEST_CASE("A1Range rejects dangling bang", "[range]") {
	REQUIRE_FALSE(A1Range("Sheet1!").IsValid());
	REQUIRE_FALSE(A1Range("!A1").IsValid());
}

TEST_CASE("A1Range rejects invalid quote placement", "[range]") {
	REQUIRE_FALSE(A1Range("Sheet'1!A1").IsValid());  // Quote in middle of unquoted name
	REQUIRE_FALSE(A1Range("'Sheet'1!A1").IsValid()); // Chars after closing quote before !
}

TEST_CASE("A1Range rejects double colon", "[range]") {
	REQUIRE_FALSE(A1Range("A1::B2").IsValid());
	REQUIRE_FALSE(A1Range("A1:B2:C3").IsValid());
}

TEST_CASE("A1Range rejects double bang", "[range]") {
	REQUIRE_FALSE(A1Range("Sheet1!!A1").IsValid());
	REQUIRE_FALSE(A1Range("Sheet1!Sheet2!A1").IsValid());
}
