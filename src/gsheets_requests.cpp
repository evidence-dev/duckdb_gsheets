#include "gsheets_requests.hpp"
#include "duckdb/common/exception.hpp"

#include <chrono>
#include <thread>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

// POSIX sockets for proxy CONNECT path
#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif
#include <cstdlib>
#include <optional>
#include <vector>
#include <cctype>

namespace duckdb {

// -------------------- Minimal env-based proxy support --------------------

static std::string Base64Encode(const std::string &input) {
	BIO *bio, *b64;
	BUF_MEM *bufferPtr;

	b64 = BIO_new(BIO_f_base64());
	bio = BIO_new(BIO_s_mem());
	bio = BIO_push(b64, bio);

	BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
	BIO_write(bio, input.data(), static_cast<int>(input.size()));
	BIO_flush(bio);
	BIO_get_mem_ptr(bio, &bufferPtr);

	std::string result(bufferPtr->data, bufferPtr->length);
	BIO_free_all(bio);

	return result;
}

static int ParseHttpStatusCode(const std::string &response) {
	size_t start = response.find("HTTP/");
	if (start == std::string::npos) {
		return -1;
	}
	size_t space = response.find(' ', start);
	if (space == std::string::npos) {
		return -1;
	}
	size_t end = response.find_first_of(" \r\n", space + 1);
	if (end == std::string::npos) {
		end = response.size();
	}
	try {
		std::string code_str = response.substr(space + 1, end - space - 1);
		return std::stoi(code_str);
	} catch (...) {
		return -1;
	}
}

struct GSheetsProxySettings {
	bool enabled = false;
	std::string scheme; // http/https (we only use http proxies for CONNECT)
	std::string host;
	int port = 0;
	std::optional<std::string> username;
	std::optional<std::string> password;
	std::vector<std::string> no_proxy; // raw patterns from NO_PROXY
};

static std::string GetEnvCI(const char *upper, const char *lower) {
	const char *v = std::getenv(upper);
	if (!v || !*v) v = std::getenv(lower);
	return v ? std::string(v) : std::string();
}

static std::vector<std::string> SplitCommaList(const std::string &s) {
	std::vector<std::string> out;
	size_t i = 0;
	while (i < s.size()) {
		size_t j = s.find(',', i);
		if (j == std::string::npos)
			j = s.size();
		auto tok = s.substr(i, j - i);
		// trim
		size_t l = 0, r = tok.size();
		while (l < r && std::isspace(static_cast<unsigned char>(tok[l]))) l++;
		while (r > l && std::isspace(static_cast<unsigned char>(tok[r - 1]))) r--;
		if (r > l)
			out.push_back(tok.substr(l, r - l));
		i = j + 1;
	}
	return out;
}

static bool HostMatchesNoProxy(const std::string &host, const std::vector<std::string> &no_proxy) {
	if (host.empty()) return false;
	// strip port from host (if any)
	auto h = host;
	auto colon = h.rfind(':');
	if (colon != std::string::npos) h = h.substr(0, colon);
	for (auto &raw : no_proxy) {
		if (raw.empty()) continue;
		if (raw == "*") return true;
		// exact match
		if (strcasecmp(h.c_str(), raw.c_str()) == 0) return true;
		// domain suffix match when pattern starts with '.'
		if (raw[0] == '.' && h.size() >= raw.size()) {
			auto tail = h.substr(h.size() - raw.size());
			if (strcasecmp(tail.c_str(), raw.c_str()) == 0) return true;
		}
	}
	return false;
}

// Parse proxy URL like: http://user:pass@proxy.host:8080
static bool ParseProxyURL(const std::string &url, GSheetsProxySettings &out) {
	auto scheme_end = url.find("://");
	if (scheme_end == std::string::npos) return false;
	out.scheme = url.substr(0, scheme_end);
	auto rest = url.substr(scheme_end + 3);

	std::string auth, hostport;
	auto at = rest.find('@');
	if (at != std::string::npos) {
		auth = rest.substr(0, at);
		hostport = rest.substr(at + 1);
		auto colon = auth.find(':');
		if (colon != std::string::npos) {
			out.username = auth.substr(0, colon);
			out.password = auth.substr(colon + 1);
		} else if (!auth.empty()) {
			out.username = auth;
		}
	} else {
		hostport = rest;
	}

	auto colon = hostport.rfind(':');
	if (colon == std::string::npos) return false;
	out.host = hostport.substr(0, colon);
	try {
		out.port = std::stoi(hostport.substr(colon + 1));
	} catch (...) {
		return false;
	}
	return true;
}

static GSheetsProxySettings ResolveProxyFromEnv(const std::string &target_host) {
	GSheetsProxySettings cfg;
#ifdef _WIN32
	// Minimal implementation: disable proxy on Windows in this patch; follow-up can add WinSock CONNECT
	return cfg; // disabled
#else
	auto no_proxy_raw = GetEnvCI("NO_PROXY", "no_proxy");
	if (!no_proxy_raw.empty()) cfg.no_proxy = SplitCommaList(no_proxy_raw);
	if (HostMatchesNoProxy(target_host, cfg.no_proxy)) return cfg; // disabled

	auto https_proxy = GetEnvCI("HTTPS_PROXY", "https_proxy");
	auto http_proxy = GetEnvCI("HTTP_PROXY", "http_proxy");
	std::string proxy_url = !https_proxy.empty() ? https_proxy : http_proxy;
	if (proxy_url.empty()) return cfg; // disabled
	if (!ParseProxyURL(proxy_url, cfg)) return cfg; // invalid
	if (cfg.scheme != "http" && cfg.scheme != "https") return cfg; // unsupported
	cfg.enabled = true;
	return cfg;
#endif
}

static int ConnectTCPSocket(const std::string &host, int port) {
#ifdef _WIN32
	throw duckdb::IOException("Proxy CONNECT not supported on Windows in this build");
#else
	struct addrinfo hints { };
	struct addrinfo *res = nullptr;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	auto port_str = std::to_string(port);
	int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
	if (rc != 0) {
		throw duckdb::IOException("DNS resolution failed for host '" + host + "': " + gai_strerror(rc));
	}
	int fd = -1;
	for (auto p = res; p != nullptr; p = p->ai_next) {
		fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (fd < 0) continue;
		// TODO: Add configurable timeout using non-blocking connect + select/poll
		if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		throw duckdb::IOException("Failed to connect to '" + host + ":" + std::to_string(port) + "'");
	}
	return fd;
#endif
}

static void WriteAll(int fd, const std::string &data) {
#ifdef _WIN32
	throw duckdb::IOException("Proxy CONNECT not supported on Windows in this build");
#else
	size_t total = 0;
	while (total < data.size()) {
		ssize_t n = send(fd, data.data() + total, data.size() - total, 0);
		if (n <= 0) throw duckdb::IOException("Failed to write to socket");
		total += static_cast<size_t>(n);
	}
#endif
}

static std::string ReadUntilDelimiter(int fd, const std::string &delim) {
#ifdef _WIN32
	throw duckdb::IOException("Proxy CONNECT not supported on Windows in this build");
#else
	std::string out;
	char buf[1024];
	for (;;) {
		ssize_t n = recv(fd, buf, sizeof(buf), MSG_PEEK);
		if (n <= 0) throw duckdb::IOException("Failed to read from socket");
		std::string peek(buf, buf + n);
		auto pos = peek.find(delim);
		if (pos == std::string::npos) {
			// consume and continue
			recv(fd, buf, n, 0);
			out.append(buf, buf + n);
			continue;
		}
		// consume up to and including delimiter
		recv(fd, buf, pos + delim.size(), 0);
		out.append(buf, buf + pos + delim.size());
		break;
	}
	return out;
#endif
}

static void CloseSocket(int fd) {
#ifdef _WIN32
	if (fd >= 0) closesocket(fd);
#else
	if (fd >= 0) close(fd);
#endif
}

std::string perform_https_request(const std::string &host, const std::string &path, const std::string &token,
                                  HttpMethod method, const std::string &body, const std::string &content_type) {
	// NOTE: implements an exponential backoff retry strategy as per https://developers.google.com/sheets/api/limits
	const int MAX_RETRIES = 10;
	int retry_count = 0;
	int backoff_s = 1;
	while (retry_count < MAX_RETRIES) {
		SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
		if (!ctx) {
			throw duckdb::IOException("Failed to create SSL context");
		}

		// Build the HTTP request payload
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
		if (!token.empty()) {
			request += "Authorization: Bearer " + token + "\r\n";
		}
		request += "Connection: close\r\n";
		if (!body.empty()) {
			request += "Content-Type: " + content_type + "\r\n";
			request += "Content-Length: " + std::to_string(body.length()) + "\r\n";
		}
		request += "\r\n";
		if (!body.empty()) {
			request += body;
		}

		std::string response;

		// Decide proxy vs direct
		auto proxy = ResolveProxyFromEnv(host);
		if (!proxy.enabled) {
			// Direct path: use existing BIO connect
			BIO *bio = BIO_new_ssl_connect(ctx);
			SSL *ssl;
			BIO_get_ssl(bio, &ssl);
			SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
			BIO_set_conn_hostname(bio, (host + ":443").c_str());
			if (BIO_do_connect(bio) <= 0) {
				BIO_free_all(bio);
				SSL_CTX_free(ctx);
				throw duckdb::IOException("Failed to connect");
			}
			if (BIO_write(bio, request.c_str(), request.length()) <= 0) {
				BIO_free_all(bio);
				SSL_CTX_free(ctx);
				throw duckdb::IOException("Failed to write request");
			}
			char buffer[1024];
			int len;
			while ((len = BIO_read(bio, buffer, sizeof(buffer))) > 0) {
				response.append(buffer, len);
			}
			BIO_free_all(bio);
			SSL_CTX_free(ctx);
		} else {
			// Proxy path: connect TCP to proxy, issue CONNECT, then TLS to target
			int fd = -1;
			SSL *ssl = nullptr;
			try {
				fd = ConnectTCPSocket(proxy.host, proxy.port);
				std::string connect_req = "CONNECT " + host + ":443 HTTP/1.1\r\n";
				connect_req += "Host: " + host + ":443\r\n";
				connect_req += "Proxy-Connection: Keep-Alive\r\n";
				if (proxy.username && proxy.password) {
					auto creds = *proxy.username + ":" + *proxy.password;
					auto b64 = Base64Encode(creds);
					connect_req += "Proxy-Authorization: Basic " + b64 + "\r\n";
				}
				connect_req += "\r\n";
				WriteAll(fd, connect_req);
				// Read headers from proxy response
				auto hdrs = ReadUntilDelimiter(fd, "\r\n\r\n");
				// Parse and check HTTP status code
				int status_code = ParseHttpStatusCode(hdrs);
				if (status_code != 200) {
					CloseSocket(fd);
					SSL_CTX_free(ctx);
					std::string error_msg = "HTTP proxy CONNECT failed with status " + std::to_string(status_code);
					throw duckdb::IOException(error_msg);
				}
				// Start TLS over the tunnel
				ssl = SSL_new(ctx);
				SSL_set_tlsext_host_name(ssl, host.c_str());
				SSL_set_fd(ssl, fd);
				if (SSL_connect(ssl) <= 0) {
					SSL_free(ssl);
					CloseSocket(fd);
					SSL_CTX_free(ctx);
					throw duckdb::IOException("TLS handshake via proxy failed");
				}
				// Send HTTPS request
				size_t sent = 0;
				while (sent < request.size()) {
					int n = SSL_write(ssl, request.data() + sent, static_cast<int>(request.size() - sent));
					if (n <= 0) {
						SSL_free(ssl);
						CloseSocket(fd);
						SSL_CTX_free(ctx);
						throw duckdb::IOException("Failed to write HTTPS request via proxy");
					}
					sent += static_cast<size_t>(n);
				}
				// Read full response
				char buf[2048];
				for (;;) {
					int n = SSL_read(ssl, buf, sizeof(buf));
					if (n <= 0) break;
					response.append(buf, buf + n);
				}
				SSL_shutdown(ssl);
				SSL_free(ssl);
				CloseSocket(fd);
				SSL_CTX_free(ctx);
			} catch (...) {
				if (ssl) SSL_free(ssl);
				if (fd >= 0) CloseSocket(fd);
				SSL_CTX_free(ctx);
				throw;
			}
		}

		// Check for rate limit exceeded
		if (response.find("429 Too Many Requests") != std::string::npos) {
			std::this_thread::sleep_for(std::chrono::seconds(backoff_s));
			backoff_s *= 2;
			retry_count++;
			continue;
		}

		// Extract body from response
		size_t body_start = response.find("\r\n\r\n");
		if (body_start != std::string::npos) {
			return response.substr(body_start + 4);
		}

		return response;
	}
	// TODO: refactor HTTPS to catch more than just rate limit exceeded or else
	//       this could give false positives.
	throw duckdb::IOException("Google Sheets API rate limit exceeded");
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
