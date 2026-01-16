#include "sheets/range.hpp"
#include <cctype>

namespace duckdb {
namespace sheets {

bool A1Range::IsValid() {
	enum State {
		START,               // Initial state
		SHEET_NAME_QUOTED,   // Inside single-quoted sheet name
		SHEET_NAME_COMPLETE, // Quoted sheet name closed (saw closing ')
		SHEET_SEPARATOR,     // Just saw '!', expecting cell reference
		COL,                 // Parsing column letters
		ROW,                 // Parsing row digits
		RANGE_SEPARATOR,     // Just saw ':', expecting second cell reference
		ERROR,               // Invalid syntax
	};

	if (range.empty()) {
		return false;
	}

	State state = START;
	const char *ptr = range.c_str();
	bool seen_bang = false;   // Only one '!' allowed
	bool seen_colon = false;  // Only one ':' allowed

	while (*ptr) {
		char c = *ptr;
		switch (state) {

		case START:
			if (c == '\'') {
				state = SHEET_NAME_QUOTED;
			} else if (std::isalpha(c)) {
				state = COL;
			} else if (std::isdigit(c)) {
				state = ROW;
			} else {
				state = ERROR;
			}
			break;

		case SHEET_NAME_QUOTED:
			if (c == '\'') {
				// Check for escape sequence ''
				if (*(ptr + 1) == '\'') {
					ptr++;  // Skip the escaped quote
				} else {
					state = SHEET_NAME_COMPLETE;
				}
			}
			// Any other char: stay in SHEET_NAME_QUOTED
			break;

		case SHEET_NAME_COMPLETE:
			if (c == '!' && !seen_bang) {
				seen_bang = true;
				state = SHEET_SEPARATOR;
			} else {
				state = ERROR;
			}
			break;

		case SHEET_SEPARATOR:
			if (std::isalpha(c)) {
				state = COL;
			} else if (std::isdigit(c)) {
				state = ROW;
			} else {
				state = ERROR;
			}
			break;

		case COL:
			if (std::isalpha(c)) {
				// Stay in COL
			} else if (std::isdigit(c)) {
				state = ROW;
			} else if (c == '!' && !seen_bang) {
				// Unquoted sheet name (e.g., "Sheet1!")
				seen_bang = true;
				state = SHEET_SEPARATOR;
			} else if (c == ':' && !seen_colon) {
				seen_colon = true;
				state = RANGE_SEPARATOR;
			} else {
				state = ERROR;
			}
			break;

		case ROW:
			if (std::isdigit(c)) {
				// Stay in ROW
			} else if (c == ':' && !seen_colon) {
				seen_colon = true;
				state = RANGE_SEPARATOR;
			} else if (c == '!' && !seen_bang) {
				// Unquoted sheet name with digits (e.g., "Sheet1!")
				seen_bang = true;
				state = SHEET_SEPARATOR;
			} else {
				state = ERROR;
			}
			break;

		case RANGE_SEPARATOR:
			if (std::isalpha(c)) {
				state = COL;
			} else if (std::isdigit(c)) {
				state = ROW;
			} else {
				state = ERROR;
			}
			break;

		case ERROR:
			return false;
		}
		ptr++;
	}

	// Valid terminal states:
	// - COL: column-only like "A:A" ending, or sheet name like "Sheet1"
	// - ROW: cell reference complete like "A1" or "Sheet1!A1:B2"
	// - SHEET_NAME_COMPLETE: quoted sheet name only like "'My Sheet'"
	return state == COL || state == ROW || state == SHEET_NAME_COMPLETE;
}

} // namespace sheets
} // namespace duckdb
