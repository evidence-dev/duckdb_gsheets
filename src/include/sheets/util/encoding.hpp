#pragma once

#include <string>

namespace duckdb {
namespace sheets {

constexpr unsigned char MASK_TOP6 = 0xFC; // 11111100 - bits 7-2 (char 0 from byte 0)
constexpr unsigned char MASK_BOT2 = 0x03; // 00000011 - bits 1-0 (start of char 1)
constexpr unsigned char MASK_TOP4 = 0xF0; // 11110000 - bits 7-4 (end of char 1)
constexpr unsigned char MASK_BOT4 = 0x0F; // 00001111 - bits 3-0 (start of char 2)
constexpr unsigned char MASK_TOP2 = 0xC0; // 11000000 - bits 7-6 (end of char 2)
constexpr unsigned char MASK_BOT6 = 0x3F; // 00111111 - bits 5-0 (char 3 from byte 2)

std::string Base64UrlEncode(const unsigned char *data, size_t len);

std::string Base64UrlEncode(const std::string &input);

std::string NormalizePemKey(const std::string &key);

} // namespace sheets
} // namespace duckdb
