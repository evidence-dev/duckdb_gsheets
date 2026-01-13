#include "sheets/util/encoding.hpp"

namespace duckdb {
namespace sheets {

// Base64URL alphabet - 64 characters representing values 0-63
static const char BASE64URL_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// Convert a 6-bit value (0-63) to its Base64URL character
static char GetBase64UrlChar(unsigned char sixBits) {
	return BASE64URL_ALPHABET[sixBits];
}

std::string Base64UrlEncode(const unsigned char *data, size_t len) {
	std::string result;
	size_t i = 0;

	while (i < len) {
		unsigned char b0 = data[i];
		unsigned char b1 = (i + 1 < len) ? data[i + 1] : 0;
		unsigned char b2 = (i + 2 < len) ? data[i + 2] : 0;

		result += GetBase64UrlChar((b0 & MASK_TOP6) >> 2);

		result += GetBase64UrlChar(((b0 & MASK_BOT2) << 4) | ((b1 & MASK_TOP4) >> 4));

		if (i + 1 < len) {
			result += GetBase64UrlChar(((b1 & MASK_BOT4) << 2) | ((b2 & MASK_TOP2) >> 6));
		}

		if (i + 2 < len) {
			result += GetBase64UrlChar(b2 & MASK_BOT6);
		}

		i += 3;
	}

	return result;
}

std::string Base64UrlEncode(const std::string &input) {
	return Base64UrlEncode(reinterpret_cast<const unsigned char *>(input.c_str()), input.length());
}

} // namespace sheets
} // namespace duckdb
