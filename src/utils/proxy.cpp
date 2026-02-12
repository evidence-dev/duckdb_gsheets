#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"

#include "utils/proxy.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/secret/secret.hpp"
#include "utils/secret.hpp"
#include "sheets/transport/http_type.hpp"

namespace duckdb {
namespace sheets {

static void ParseHTTPProxyHost(string &proxy_value, string &hostname_out, uint16_t &port_out, uint16_t default_port) {
	auto sanitized_proxy_value = proxy_value;
	if (StringUtil::StartsWith(proxy_value, "http://")) {
		sanitized_proxy_value = proxy_value.substr(7);
	} else if (StringUtil::StartsWith(proxy_value, "https://")) {
		sanitized_proxy_value = proxy_value.substr(8);
	}
	auto proxy_split = StringUtil::Split(sanitized_proxy_value, ":");
	if (proxy_split.size() == 1) {
		hostname_out = proxy_split[0];
		port_out = default_port;
	} else if (proxy_split.size() == 2) {
		uint16_t port;
		try {
			auto val = std::stoul(proxy_split[1]);
			if (val > std::numeric_limits<uint16_t>::max()) {
				throw InvalidInputException("Failed to parse port from http_proxy '%s'", proxy_value);
			}
			port = static_cast<uint16_t>(val);
		} catch (const std::invalid_argument &e) {
			throw InvalidInputException("Failed to parse port from http_proxy '%s'", proxy_value);
		} catch (const std::out_of_range &e) {
			throw InvalidInputException("Failed to parse port from http_proxy '%s'", proxy_value);
		}
		hostname_out = proxy_split[0];
		port_out = port;
	} else {
		throw InvalidInputException("Failed to parse http_proxy '%s' into a host and port", proxy_value);
	}
}

HttpProxyConfig GetHttpProxyConfig(ClientContext &ctx) {
	HttpProxyConfig proxy_config;
	auto match = GetSecretMatch(ctx, "", "http");
	if (match.HasMatch()) {
		auto &secret = match.GetSecret();
		auto http_secret = dynamic_cast<const KeyValueSecret *>(&secret);

		Value http_proxy, http_proxy_username, http_proxy_password;

		http_secret->TryGetValue("http_proxy", http_proxy);
		http_secret->TryGetValue("http_proxy_username", http_proxy_username);
		http_secret->TryGetValue("http_proxy_password", http_proxy_password);

		auto proxy_value = http_proxy.ToString();

		if (!proxy_value.empty()) {
			ParseHTTPProxyHost(proxy_value, proxy_config.host, proxy_config.port, 80);
			proxy_config.username = http_proxy_username.ToString();
			proxy_config.password = http_proxy_password.ToString();
		}
	} else {
		// try falling back on configs
		auto &global_config = DBConfig::GetConfig(ctx);

		auto proxy_value = global_config.options.http_proxy;

		if (!proxy_value.empty()) {
			ParseHTTPProxyHost(proxy_value, proxy_config.host, proxy_config.port, 80);
			proxy_config.username = global_config.options.http_proxy_username;
			proxy_config.password = global_config.options.http_proxy_password;
		}
	}
	return proxy_config;
}

} // namespace sheets
} // namespace duckdb
