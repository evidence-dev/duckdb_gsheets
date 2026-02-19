#include "json.hpp"
#include "duckdb/common/exception.hpp"

#include "sheets/auth/service_account_auth.hpp"
#include "sheets/transport/http_type.hpp"
#include "sheets/util/encoding.hpp"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

using json = nlohmann::json;

namespace duckdb {
namespace sheets {

// RAII deleters for OpenSSL types
struct BIODeleter {
	void operator()(BIO *b) const {
		if (b) {
			BIO_free(b);
		}
	}
};
struct EVPPKEYDeleter {
	void operator()(EVP_PKEY *p) const {
		if (p) {
			EVP_PKEY_free(p);
		}
	}
};
struct EVPMDCTXDeleter {
	void operator()(EVP_MD_CTX *m) const {
		if (m) {
			EVP_MD_CTX_free(m);
		}
	}
};

using BIOPtr = std::unique_ptr<BIO, BIODeleter>;
using EVPPKEYPtr = std::unique_ptr<EVP_PKEY, EVPPKEYDeleter>;
using EVPMDCTXPtr = std::unique_ptr<EVP_MD_CTX, EVPMDCTXDeleter>;

constexpr int TOKEN_TTL = 1800;
constexpr const char *TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token";

std::string ServiceAccountAuth::GetAuthorizationHeader() {
	if (IsExpired()) {
		Refresh();
	}
	return "Bearer " + cachedToken;
}

std::string ServiceAccountAuth::CreateJwt() {
	const char *header = R"({"alg":"RS256","typ":"JWT"})";

	json claimSet;
	std::time_t now = std::time(nullptr);
	claimSet["iss"] = email;                                            // service account email
	claimSet["scope"] = "https://www.googleapis.com/auth/spreadsheets"; // API scope
	claimSet["aud"] = TOKEN_ENDPOINT;                                   // token endpoint
	claimSet["iat"] = now;                                              // issued at
	claimSet["exp"] = now + TOKEN_TTL;                                  // expires in 30 min

	std::string headerB64 = Base64UrlEncode(header);
	std::string claimsB64 = Base64UrlEncode(claimSet.dump());
	std::string signInput = headerB64 + "." + claimsB64;

	// Replace literal "\n" with actual newlines
	std::string pem = privateKey;
	size_t pos = 0;
	while ((pos = pem.find("\\n", pos)) != std::string::npos) {
		pem.replace(pos, 2, "\n");
		pos += 1;
	}

	// Parse PEM private key into EVP_PKEY (RAII handles cleanup)
	BIOPtr bio(BIO_new_mem_buf(pem.c_str(), -1));
	if (!bio) {
		throw duckdb::IOException("Failed to create BIO for private key");
	}

	EVPPKEYPtr pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
	if (!pkey) {
		throw duckdb::IOException("Failed to parse private key");
	}

	// Create and initialize signing context
	EVPMDCTXPtr mdctx(EVP_MD_CTX_new());
	if (!mdctx) {
		throw duckdb::IOException("Failed to create EVP_MD_CTX");
	}

	if (EVP_DigestSignInit(mdctx.get(), nullptr, EVP_sha256(), nullptr, pkey.get()) != 1) {
		throw duckdb::IOException("Failed to initialize signing context");
	}

	// Provide data to sign
	if (EVP_DigestSignUpdate(mdctx.get(), signInput.c_str(), signInput.length()) != 1) {
		throw duckdb::IOException("Failed to update signing context");
	}

	// Get signature length, then sign
	size_t sigLen = 0;
	if (EVP_DigestSignFinal(mdctx.get(), nullptr, &sigLen) != 1) {
		throw duckdb::IOException("Failed to get signature length");
	}

	std::vector<unsigned char> signature(sigLen);
	if (EVP_DigestSignFinal(mdctx.get(), signature.data(), &sigLen) != 1) {
		throw duckdb::IOException("Failed to sign JWT");
	}

	return signInput + "." + Base64UrlEncode(signature.data(), sigLen);
}

void ServiceAccountAuth::ExchangeJwtForToken() {
	std::string jwt = CreateJwt();

	std::string body = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + jwt;

	HttpHeaders headers;
	headers["Content-Type"] = "application/x-www-form-urlencoded";
	HttpResponse response = http.Post(TOKEN_ENDPOINT, headers, body);

	if (response.statusCode != 200) {
		throw duckdb::IOException("Token exchange failed: " + response.body);
	}

	json responseJson;
	try {
		responseJson = json::parse(response.body);
	} catch (const json::exception &) {
		throw duckdb::IOException("Failed to parse token response: " + response.body);
	}

	if (!responseJson.contains("access_token")) {
		throw duckdb::IOException("Token response missing 'access_token': " + response.body);
	}
	cachedToken = responseJson["access_token"].get<std::string>();

	int expiresIn = responseJson.value("expires_in", TOKEN_TTL);
	expirationTime = std::time(nullptr) + expiresIn - 60; // refresh 1 min early
}

bool ServiceAccountAuth::IsExpired() {
	if (cachedToken.empty()) {
		return true;
	}
	std::time_t now = std::time(nullptr);
	return now >= expirationTime;
}

void ServiceAccountAuth::Refresh() {
	ExchangeJwtForToken();
}

} // namespace sheets
} // namespace duckdb
