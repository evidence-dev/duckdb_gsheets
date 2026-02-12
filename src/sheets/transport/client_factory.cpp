#include "duckdb/common/helper.hpp"
#include "duckdb/main/client_context.hpp"

#include "sheets/transport/http_client.hpp"
#include "sheets/transport/httplib_client.hpp"
#include "utils/proxy.hpp"

namespace duckdb {
namespace sheets {

std::unique_ptr<IHttpClient> CreateHttpClient(ClientContext &ctx) {
	auto proxy_config = GetHttpProxyConfig(ctx);
	return make_uniq<HttpLibClient>(proxy_config);
}

} // namespace sheets
} // namespace duckdb
