#include "gsheets_requests.hpp"
#include "duckdb/common/exception.hpp"
#include <chrono>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <thread>

namespace duckdb {

// Parse HTTP status code from response (returns -1 if parsing fails)
static int parse_http_status_code(const std::string &response) {
	// HTTP/1.0 200 OK  or  HTTP/1.1 429 Too Many Requests
	size_t http_pos = response.find("HTTP/");
	if (http_pos == std::string::npos) {
		return -1;
	}

	size_t space_pos = response.find(' ', http_pos);
	if (space_pos == std::string::npos) {
		return -1;
	}

	try {
		return std::stoi(response.substr(space_pos + 1, 3));
	} catch (...) {
		return -1;
	}
}

// Check if HTTP status code is a transient error that should be retried
static bool is_retryable_status_code(int status_code) {
	switch (status_code) {
	case 429: // Too Many Requests (rate limit)
	case 500: // Internal Server Error
	case 502: // Bad Gateway
	case 503: // Service Unavailable
	case 504: // Gateway Timeout
		return true;
	default:
		return false;
	}
}

// Get error message for a specific HTTP status code
static std::string get_error_message_for_status(int status_code) {
	switch (status_code) {
	case 429:
		return "Google Sheets API rate limit exceeded";
	case 500:
		return "Google Sheets API internal server error";
	case 502:
		return "Google Sheets API bad gateway error";
	case 503:
		return "Google Sheets API service unavailable";
	case 504:
		return "Google Sheets API gateway timeout";
	default:
		return "Google Sheets API request failed with status " + std::to_string(status_code);
	}
}

std::string perform_https_request(const std::string &host, const std::string &path, const std::string &token,
                                  HttpMethod method, const std::string &body, const std::string &content_type) {
	// NOTE: implements an exponential backoff retry strategy as per https://developers.google.com/sheets/api/limits
	const int MAX_RETRIES = 10;
	int retry_count = 0;
	int backoff_s = 1;
	int last_status_code = -1;
	bool connection_failed = false;

	while (retry_count < MAX_RETRIES) {
		SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
		if (!ctx) {
			throw duckdb::IOException("Failed to create SSL context");
		}

		BIO *bio = BIO_new_ssl_connect(ctx);
		SSL *ssl;
		BIO_get_ssl(bio, &ssl);
		SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

		BIO_set_conn_hostname(bio, (host + ":443").c_str());

		if (BIO_do_connect(bio) <= 0) {
			BIO_free_all(bio);
			SSL_CTX_free(ctx);
			// Retry connection failures with exponential backoff
			std::this_thread::sleep_for(std::chrono::seconds(backoff_s));
			backoff_s *= 2;
			retry_count++;
			connection_failed = true;
			continue;
		}
		connection_failed = false;

		std::string method_str;
		switch (method) {
		case HttpMethod::GET:
			method_str = "GET";
			break;
		case HttpMethod::POST:
			method_str = "POST";
			break;
		case HttpMethod::PUT:
			method_str = "PUT";
			break;
		}

		std::string request = method_str + " " + path + " HTTP/1.0\r\n";
		request += "Host: " + host + "\r\n";
		request += "Authorization: Bearer " + token + "\r\n";
		request += "Connection: close\r\n";

		if (!body.empty()) {
			request += "Content-Type: " + content_type + "\r\n";
			request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
		}

		request += "\r\n";

		if (!body.empty()) {
			request += body;
		}

		if (BIO_write(bio, request.c_str(), request.length()) <= 0) {
			BIO_free_all(bio);
			SSL_CTX_free(ctx);
			throw duckdb::IOException("Failed to write request");
		}

		std::string response;
		char buffer[1024];
		int len;
		while ((len = BIO_read(bio, buffer, sizeof(buffer))) > 0) {
			response.append(buffer, len);
		}

		BIO_free_all(bio);
		SSL_CTX_free(ctx);

		// Check for retryable HTTP status codes (429, 500, 502, 503, 504)
		int status_code = parse_http_status_code(response);
		if (is_retryable_status_code(status_code)) {
			std::this_thread::sleep_for(std::chrono::seconds(backoff_s));
			backoff_s *= 2;
			retry_count++;
			last_status_code = status_code;
			continue;
		}

		// Extract body from response
		size_t body_start = response.find("\r\n\r\n");
		if (body_start != std::string::npos) {
			return response.substr(body_start + 4);
		}

		return response;
	}

	// Generate appropriate error message based on failure type
	if (connection_failed) {
		throw duckdb::IOException("Failed to connect to " + host + " after " + std::to_string(MAX_RETRIES) + " retries");
	}
	throw duckdb::IOException(get_error_message_for_status(last_status_code) + " after " +
	                          std::to_string(MAX_RETRIES) + " retries");
}

std::string call_sheets_api(const std::string &spreadsheet_id, const std::string &token, const std::string &sheet_name,
                            const std::string &sheet_range, HttpMethod method, const std::string &body) {
	std::string host = "sheets.googleapis.com";
	std::string path = "/v4/spreadsheets/" + spreadsheet_id + "/values/" + sheet_name;

	if (!sheet_range.empty()) {
		path += "!" + sheet_range;
	}

	if (method == HttpMethod::POST) {
		path += ":append";
		path += "?valueInputOption=USER_ENTERED";
	}

	return perform_https_request(host, path, token, method, body);
}

std::string delete_sheet_data(const std::string &spreadsheet_id, const std::string &token,
                              const std::string &sheet_name, const std::string &sheet_range) {
	std::string host = "sheets.googleapis.com";
	std::string sheet_and_range = sheet_range.empty() ? sheet_name : sheet_name + "!" + sheet_range;
	std::string path = "/v4/spreadsheets/" + spreadsheet_id + "/values/" + sheet_and_range + ":clear";

	return perform_https_request(host, path, token, HttpMethod::POST, "{}");
}

std::string get_spreadsheet_metadata(const std::string &spreadsheet_id, const std::string &token) {
	std::string host = "sheets.googleapis.com";
	std::string path = "/v4/spreadsheets/" + spreadsheet_id + "?&fields=sheets.properties";
	return perform_https_request(host, path, token, HttpMethod::GET, "");
}
} // namespace duckdb
