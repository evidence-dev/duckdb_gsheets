// Taken with modifications from https://gist.github.com/niuk/6365b819a86a7e0b92d82328fcf87da5
#include <stdio.h>  
#include <string.h>
#include <stdlib.h>
#include <fstream>
#include "gsheets_requests.hpp"
#include "gsheets_utils.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/secret/secret_manager.hpp"
#include "gsheets_get_token.hpp"
#include "gsheets_auth.hpp"

#include <openssl/ssl.h>
#include <openssl/err.h>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <json.hpp>
using json = nlohmann::json;
namespace duckdb
{

    char get_base64_char(char byte) {
        if (byte < 26) {
            return 'A' + byte;
        } else if (byte < 52) {
            return 'a' + byte - 26;
        } else if (byte < 62) {
            return '0' + byte - 52;
        } else if (byte == 62) {
            return '-';
        } else if (byte == 63) {
            return '_';
        } else {
            fprintf(stderr, "BAD BYTE: %02x\n", byte);
            exit(1);
            return 0;
        }
    }

    // To execute C, please define "int main()"
    void base64encode(char *output, const char *input, size_t input_length) {
        size_t input_index = 0;
        size_t output_index = 0;
        for (; input_index < input_length; ++output_index) {
            switch (output_index % 4) {
            case 0:
                output[output_index] = get_base64_char((0xfc & input[input_index]) >> 2);
                break;
            case 1:
                output[output_index] = get_base64_char(((0x03 & input[input_index]) << 4) | ((0xf0 & input[input_index + 1]) >> 4));
                ++input_index;
                break;
            case 2:
                output[output_index] = get_base64_char(((0x0f & input[input_index]) << 2) | ((0xc0 & input[input_index + 1]) >> 6));
                ++input_index;
                break;
            case 3:
                output[output_index] = get_base64_char(0x3f & input[input_index]);
                ++input_index;
                break;
            default:
                exit(1);
            }
        }

        output[output_index] = '\0';
    }

    TokenDetails get_token(ClientContext &context, const KeyValueSecret* kv_secret) {
        const char *header = "{\"alg\":\"RS256\",\"typ\":\"JWT\"}";

        /* Create jwt claim set */
        json jwt_claim_set;
        std::time_t t = std::time(NULL);

        Value email_value;
        if (!kv_secret->TryGetValue("email", email_value)) {
            throw InvalidInputException("'email' not found in 'gsheet' secret");
        }
        std::string email_string = email_value.ToString();

        Value secret_value;
        if (!kv_secret->TryGetValue("secret", secret_value)) {
            throw InvalidInputException("'secret' (private_key) not found in 'gsheet' secret");
        }
        std::string secret_string = secret_value.ToString();

        jwt_claim_set["iss"] = email_string; /* service account email address */
        jwt_claim_set["scope"] = "https://www.googleapis.com/auth/spreadsheets" /* scope of requested access token */;
        jwt_claim_set["aud"] = "https://accounts.google.com/o/oauth2/token"; /* intended target of the assertion for an access token */
        jwt_claim_set["iat"] = std::to_string(t); /* issued time */
        // Max time that Google allows is 1 hour, so set to 30 minutes as a buffer
        // Set to t+5 for testing purposes
        std::string expiration_time = std::to_string(t+1800);
        jwt_claim_set["exp"] = expiration_time; /* expire time*/

        char header_64[1024];
        base64encode(header_64, header, strlen(header));

        char claim_set_64[1024];
        base64encode(claim_set_64, jwt_claim_set.dump().c_str(), strlen(jwt_claim_set.dump().c_str()));

        char input[1024];
        int input_length = sprintf(input, "%s.%s", header_64, claim_set_64);

        unsigned char *digest = SHA256((const unsigned char *)input, input_length, NULL);
        char digest_str[1024];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            sprintf(digest_str + i * 2, "%02x", digest[i]);
        }
        
        digest_str[SHA256_DIGEST_LENGTH * 2] = '\0';
        
        BIO* bio = BIO_new(BIO_s_mem());
        const void * private_key_pointer = secret_string.c_str();
        int private_key_length = std::strlen(secret_string.c_str());
        BIO_write(bio, private_key_pointer, private_key_length);
        EVP_PKEY* evp_key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
        RSA* rsa = EVP_PKEY_get1_RSA(evp_key);

        if (rsa != NULL) {
            unsigned char sigret[4096] = {};
            unsigned int siglen;
            if (RSA_sign(NID_sha256, digest, SHA256_DIGEST_LENGTH, sigret, &siglen, rsa)) {
                if (RSA_verify(NID_sha256, digest, SHA256_DIGEST_LENGTH, sigret, siglen, rsa)) {
                    char signature_64[1024];
                    base64encode(signature_64, (const char *)sigret, siglen);
                    
                    char jwt[1024];
                    sprintf(jwt, "%s.%s", input, signature_64);

                    std::string body = "grant_type=urn:ietf:params:oauth:grant-type:jwt-bearer&assertion=" + std::string(jwt);
                    std::string response = perform_https_request("oauth2.googleapis.com", "/token", "", 
                                               HttpMethod::POST, 
                                               body,
                                               "application/x-www-form-urlencoded");
                    json response_json = parseJson(response);
                    std::string token = response_json["access_token"].get<std::string>();
                    TokenDetails result = {token, expiration_time};
                    return result;
                } else {
                    printf("Could not verify RSA signature.");
                }
            } else {
                unsigned long err = ERR_get_error();
                printf("RSA_sign failed: %lu, %s\n", err, ERR_error_string(err, NULL));
            }

            RSA_free(rsa);
        }

        throw InvalidInputException("Conversion from private key to token failed. Check email, key format in JSON file (-----BEGIN PRIVATE KEY-----\\n ... -----END PRIVATE KEY-----\\n), and expiration date.");

    }

    std::string get_token_and_cache(ClientContext &context, CatalogTransaction &transaction, const KeyValueSecret* kv_secret) {
        
        // Check if the token exists and has not expired. If so, use it
        Value token_value;
        Value token_expiration_value;
        if (kv_secret->TryGetValue("token", token_value)) {
            std::string token_string = token_value.ToString();  
            
            if (kv_secret->TryGetValue("token_expiration", token_expiration_value)) {
                std::string token_expiration_string = token_expiration_value.ToString();
                // std::cout << "token_expiration_string: " << token_expiration_string << std::endl;

                std::time_t expiration_time = static_cast<std::time_t>(std::stod(token_expiration_string));
                std::time_t current_time = std::time(NULL);
                // std::cout << "expiration_time: " << expiration_time << " current_time: " << current_time << std::endl;
                if (expiration_time > current_time) {
                    return token_string;
                }
            }
        }

        // If we haven't returned yet, then there is no token or it is expired.
        // std::cout << "Token does not exist or is expired!" << std::endl;
        TokenDetails token_details = get_token(context, kv_secret);

        // Cache the token in a new secret
        auto &secret_manager = SecretManager::Get(context);
        auto secret_name = kv_secret->GetName();
        auto old_secret = secret_manager.GetSecretByName(transaction, secret_name);
        auto persist_type = old_secret->persist_type;
        auto storage_mode = old_secret->storage_mode;
        CreateSecretInfo create_secret_info = CreateSecretInfo(OnCreateConflict::REPLACE_ON_CONFLICT, persist_type);

        // Copy the old secret (to get metadata about the secret we want to maintain)
        auto new_secret = old_secret->secret->Clone().get();
        auto new_secret_kv = dynamic_cast<const KeyValueSecret &>(*old_secret->secret);
        
        // Add in the new token and expiration date
        case_insensitive_map_t<Value> new_options;
        new_options["token"] = Value(token_details.token);
        new_options["token_expiration"] = Value(token_details.expiration_time);
        // std::cout << "About to set new expiration_time: " << token_details.expiration_time << std::endl;

        // Create a new secret based on the old secret metadata, but with new token and expiration
        auto new_token_input = CreateSecretInput {
            new_secret_kv.GetType(),
            new_secret_kv.GetProvider(),
            "", // I don't know what storage_type to put, and it shouldn't be needed
            new_secret_kv.GetName(),
            new_secret_kv.GetScope(),
            new_options
            };
        auto successfully_set_token = new_secret_kv.TrySetValue("token", new_token_input);
        auto successfully_set_token_expiration = new_secret_kv.TrySetValue("token_expiration", new_token_input);
        // std::cout << "successfully_set_token: " << successfully_set_token << std::endl;
        // std::cout << "successfully_set_token_expiration: " << successfully_set_token_expiration << std::endl;

        // Then register the secret with the OnCreateConflict set to REPLACE_ON_CONFLICT
        secret_manager.RegisterSecret(
            transaction,
            make_uniq<const KeyValueSecret>(new_secret_kv),
            OnCreateConflict::REPLACE_ON_CONFLICT,
            persist_type,
            storage_mode
            );

        // To make sure saving the secret worked end to end, return the token from the new secret
        auto repulled_new_secret = secret_manager.GetSecretByName(transaction, secret_name);
        auto repulled_new_secret_kv = dynamic_cast<const KeyValueSecret &>(*repulled_new_secret->secret);
        Value repulled_token_value;
        if (!repulled_new_secret_kv.TryGetValue("token", repulled_token_value)) {
            throw InvalidInputException("'token' not found in repulled 'gsheet' secret after caching");
        }
        std::string token_string = repulled_token_value.ToString();
        return token_string;
    }
}
