#include "catch.hpp"

#include "duckdb/common/exception.hpp"
#include "sheets/auth/bearer_token_auth.hpp"
#include "sheets/auth/oauth_auth.hpp"
#include "sheets/auth/service_account_auth.hpp"
#include "sheets/transport/mock_http_client.hpp"

// =============================================================================
// BearerTokenAuth Tests
// =============================================================================

TEST_CASE("BearerTokenAuth returns correct header", "[auth]") {
	duckdb::sheets::BearerTokenAuth auth("my-test-token");
	REQUIRE(auth.GetAuthorizationHeader() == "Bearer my-test-token");
}

TEST_CASE("BearerTokenAuth handles empty token", "[auth]") {
	duckdb::sheets::BearerTokenAuth auth("");
	REQUIRE(auth.GetAuthorizationHeader() == "Bearer ");
}

// =============================================================================
// OAuthAuth Tests
// =============================================================================

TEST_CASE("OAuthAuth returns correct header", "[auth]") {
	duckdb::sheets::OAuthAuth auth("oauth-access-token");
	REQUIRE(auth.GetAuthorizationHeader() == "Bearer oauth-access-token");
}

// =============================================================================
// ServiceAccountAuth Tests
// =============================================================================

// Test RSA private key (for testing only - not a real key)
static const char TEST_PRIVATE_KEY[] = R"(-----BEGIN RSA PRIVATE KEY-----
MIIEpAIBAAKCAQEA2a2rwplBQLF29amygykEMmYz0+Kcj3bKBp7draBHAFnGKzab
mJGBQmhYltrVFxmePN2Cd0JitEYi5nKgLhF8gWBYcLBcihBNYlRWYgGkD1jLpHXa
bFn2X3nXFMrPvUkDzFWjjxrVMeo1rVFVrXjG+UsghZs0TO4HyGkMsJkaxkIjLENR
WJqEqnPJBJdqdGs0l80dZL0gCu9V0KcdyfJVnAo1rV1avVrSej4w7JSYnpqJuByT
a3sVQ+6btOPbMR7fGYJPJ0zmHzJJfY6PplJmRjD+4rHbFd8mfkxlqGKXaKgZWi0y
yls8OJnpkPUDKZJFnqBGzpAXXt6SYyFBGYBjbwIDAQABAoIBAC1J4j+v0lmzPYvH
al0YYb8jUJfPO9HIJa9YCLFKrce5xb9prVoJjrkDgbRviWwfLmHchJrCBbG5ukLu
w0aJX1GXWP9LBWaFaVUuSl3QOAMLVK4dL6O+kJL9E3hZLLz/0BEIP1g3Y8bS7xJC
t1gsz5DPNTfsfPzsbBnZriUgnagvKpR9E4arG2s3peLGH/D28EByJT4juoGytvJu
D74+4IQ4aSa2FW8BCZJ0RNUqJuTcaBnl3+R1Ab+sitPVVbF0QaltScYjpWdsSNGP
z+VjZGrnNkMzGDV6scz6c8iolmVAn9JT9VSE9qdZ94s0trKvSmJrpkIf2zpNaQ87
kaJA9UECgYEA7lXftjsAv4RHhqjcpZ7IzgjGvj6JfzShqtJPjakLrJONjjmE3lRi
HstwfyLy18BvvGBFrV3hHUdm1sCqGgeM2l/9dYpJv5E1bpGpLkQL3gvzbSc8wYuX
5PGK02pTNrvl3dOu3WjtmDn0+sKMpcHrNA0VoErrBgYdftJt5EN12DECgYEA6RpZ
rV3KKRQF+zwJvhGnH5hWJn84+xRe6payy44qOn3b1kKw+FLEA8HdmgDRicpQrVKO
0sT3t60PL8RXPc9SEGZ8j5D8JaNy71FYP3X+Bj6UfYsFig7Q42y8Ihf6mfD8HQMJ
B6UXqdGrXePReqsNkvM7XwQE4MwkK0xbHTCkhj8CgYBmMF4sLGJdFT4S/dVHQAuu
vjywvqZLfJzkT+GDz5ol4Kv3k53fuvlPMlqAmVbJtKNcIPi4VrXqvMT0LwMBtpsH
KAGDJxdZCo3cfEKqEbWCWN9JM7poUhIZrM3rku4TviolBHVPsYPPZ7uxs8VUFCpF
0ddT1OVKP+VaW7PRpfqzUQKBgQCnplspiPck8Ap3HAR8o7T0BFqkLsT0cyCgSYLo
FVbvE7Lv384cFRwuWmGL1bXNz+6EL9VWBGX9cCjGCWXc8hD3HFMdF4Ys6mKbecbH
bkkeYYdPsKfQGJG0kOUM4UvaFrQ/s2r0L5k4Li5QnA0fJbcRZfq5LNe7ewsy4byR
Yd2o3wKBgQCa8gRI1FhIc52wgxJr4VpkWqjF37b13UjPKLkA4NVJ2H4FmB3ZMMXO
vMjnEczCpVTMq6grdFNDq5BQ+hCYmpJjTfFSqnIq0NBD4j7f2cJGFr0ifyAp6Z9h
bJLmkFstvHhZln4DQeDvR7ADLqufsif6Ytu6dDes1qP2+3GxQP1a8w==
-----END RSA PRIVATE KEY-----
)";

TEST_CASE("ServiceAccountAuth creates valid JWT structure", "[auth]") {
	duckdb::sheets::MockHttpClient mockHttp;

	// Queue a successful token response
	duckdb::sheets::HttpResponse tokenResponse;
	tokenResponse.statusCode = 200;
	tokenResponse.body = R"({"access_token": "ya29.test-token", "expires_in": 3600})";
	mockHttp.AddResponse(tokenResponse);

	duckdb::sheets::ServiceAccountAuth auth(mockHttp, "test@project.iam.gserviceaccount.com", TEST_PRIVATE_KEY);

	std::string header = auth.GetAuthorizationHeader();

	// Should return Bearer token
	REQUIRE(header == "Bearer ya29.test-token");

	// Verify the HTTP request was made correctly
	auto &requests = mockHttp.GetRecordedRequests();
	REQUIRE(requests.size() == 1);
	REQUIRE(requests[0].method == duckdb::sheets::HttpMethod::POST);
	REQUIRE(requests[0].url == "https://oauth2.googleapis.com/token");
	REQUIRE(requests[0].headers.at("Content-Type") == "application/x-www-form-urlencoded");

	// Body should contain JWT assertion
	REQUIRE(requests[0].body.find("grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer") != std::string::npos);
	REQUIRE(requests[0].body.find("assertion=") != std::string::npos);
}

TEST_CASE("ServiceAccountAuth JWT has correct structure", "[auth]") {
	duckdb::sheets::MockHttpClient mockHttp;

	duckdb::sheets::HttpResponse tokenResponse;
	tokenResponse.statusCode = 200;
	tokenResponse.body = R"({"access_token": "test-token", "expires_in": 3600})";
	mockHttp.AddResponse(tokenResponse);

	duckdb::sheets::ServiceAccountAuth auth(mockHttp, "test@example.com", TEST_PRIVATE_KEY);
	auth.GetAuthorizationHeader();

	auto &requests = mockHttp.GetRecordedRequests();
	std::string body = requests[0].body;

	// Extract JWT from assertion=<jwt>
	size_t pos = body.find("assertion=");
	REQUIRE(pos != std::string::npos);
	std::string jwt = body.substr(pos + 10);

	// JWT should have 3 parts separated by dots
	int dotCount = 0;
	for (char c : jwt) {
		if (c == '.') {
			dotCount++;
		}
	}
	REQUIRE(dotCount == 2);
}

TEST_CASE("ServiceAccountAuth caches token", "[auth]") {
	duckdb::sheets::MockHttpClient mockHttp;

	duckdb::sheets::HttpResponse tokenResponse;
	tokenResponse.statusCode = 200;
	tokenResponse.body = R"({"access_token": "cached-token", "expires_in": 3600})";
	mockHttp.AddResponse(tokenResponse);

	duckdb::sheets::ServiceAccountAuth auth(mockHttp, "test@example.com", TEST_PRIVATE_KEY);

	// First call should make HTTP request
	std::string header1 = auth.GetAuthorizationHeader();
	REQUIRE(header1 == "Bearer cached-token");
	REQUIRE(mockHttp.GetRecordedRequests().size() == 1);

	// Second call should use cached token (no new request)
	std::string header2 = auth.GetAuthorizationHeader();
	REQUIRE(header2 == "Bearer cached-token");
	REQUIRE(mockHttp.GetRecordedRequests().size() == 1); // Still 1, not 2
}

TEST_CASE("ServiceAccountAuth throws on HTTP error", "[auth]") {
	duckdb::sheets::MockHttpClient mockHttp;

	duckdb::sheets::HttpResponse errorResponse;
	errorResponse.statusCode = 401;
	errorResponse.body = R"({"error": "invalid_client"})";
	mockHttp.AddResponse(errorResponse);

	duckdb::sheets::ServiceAccountAuth auth(mockHttp, "test@example.com", TEST_PRIVATE_KEY);

	REQUIRE_THROWS_AS(auth.GetAuthorizationHeader(), duckdb::IOException);
}

TEST_CASE("ServiceAccountAuth throws on missing access_token", "[auth]") {
	duckdb::sheets::MockHttpClient mockHttp;

	duckdb::sheets::HttpResponse badResponse;
	badResponse.statusCode = 200;
	badResponse.body = R"({"token_type": "Bearer"})"; // Missing access_token
	mockHttp.AddResponse(badResponse);

	duckdb::sheets::ServiceAccountAuth auth(mockHttp, "test@example.com", TEST_PRIVATE_KEY);

	REQUIRE_THROWS_AS(auth.GetAuthorizationHeader(), duckdb::IOException);
}

TEST_CASE("ServiceAccountAuth throws on invalid JSON", "[auth]") {
	duckdb::sheets::MockHttpClient mockHttp;

	duckdb::sheets::HttpResponse badResponse;
	badResponse.statusCode = 200;
	badResponse.body = "not valid json";
	mockHttp.AddResponse(badResponse);

	duckdb::sheets::ServiceAccountAuth auth(mockHttp, "test@example.com", TEST_PRIVATE_KEY);

	REQUIRE_THROWS_AS(auth.GetAuthorizationHeader(), duckdb::IOException);
}
