#include "catch.hpp"

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "gsheets_auth.hpp"
#include "sheets/auth/oauth_token_exchange.hpp"
#include "sheets/auth_factory.hpp"
#include "sheets/transport/mock_http_client.hpp"

using namespace duckdb;

// Builds the auth provider from a real `gsheet` secret, so the secret's stored keys and CreateAuthFromSecret's
// handling of them are tested together. The token endpoint is a MockHttpClient: no credentials or network access
// needed.

namespace {

void RunOk(Connection &con, const string &query) {
	auto result = con.Query(query);
	if (result->HasError()) {
		FAIL(result->GetError());
	}
}

sheets::HttpResponse InvalidGrantResponse() {
	sheets::HttpResponse response;
	response.statusCode = 400;
	response.body = R"({"error": "invalid_grant", "error_description": "Token has been expired or revoked."})";
	return response;
}

} // namespace

TEST_CASE("CreateAuthFromSecret with a passed-in refresh_token", "[auth_factory]") {
	DuckDB db(nullptr);
	ExtensionLoader loader(*db.instance, "gsheets_auth_factory_test");
	CreateGsheetSecretFunctions::Register(loader);

	Connection con(db);
	RunOk(con, "CREATE SECRET (TYPE gsheet, PROVIDER oauth, CLIENT_ID 'my-client-id', "
	           "CLIENT_SECRET 'my-client-secret', REFRESH_TOKEN 'my-refresh-token')");

	sheets::MockHttpClient mockHttp;
	// The secret lookup needs an active transaction, as it has inside a query's bind.
	con.BeginTransaction();

	SECTION("mints the first access token from the refresh_token") {
		sheets::HttpResponse tokenResponse;
		tokenResponse.statusCode = 200;
		tokenResponse.body = R"({"access_token": "minted-token", "expires_in": 3600})";
		mockHttp.AddResponse(tokenResponse);

		auto auth = sheets::CreateAuthFromSecret(*con.context, mockHttp, /*allow_interactive_reauth=*/true);
		REQUIRE(auth);
		REQUIRE(auth->GetAuthorizationHeader() == "Bearer minted-token");

		auto &requests = mockHttp.GetRecordedRequests();
		REQUIRE(requests.size() == 1);
		REQUIRE(requests[0].body.find("refresh_token=my-refresh-token") != std::string::npos);
		REQUIRE(requests[0].body.find("client_id=my-client-id") != std::string::npos);
		REQUIRE(requests[0].body.find("client_secret=my-client-secret") != std::string::npos);
	}

	SECTION("a revoked refresh_token throws instead of starting the browser login") {
		mockHttp.AddResponse(InvalidGrantResponse());

		// Even with the caller allowing reauth, the secret's own interactive_reauth = false wins: a login here
		// would open the local listener and wait minutes for a browser.
		auto auth = sheets::CreateAuthFromSecret(*con.context, mockHttp, /*allow_interactive_reauth=*/true);
		REQUIRE(auth);
		REQUIRE_THROWS_AS(auth->GetAuthorizationHeader(), sheets::OAuthInvalidGrantException);
		REQUIRE(mockHttp.GetRecordedRequests().size() == 1);
	}

	con.Rollback();
}
