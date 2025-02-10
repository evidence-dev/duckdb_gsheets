#pragma once

#include <string>
#include <stdlib.h>
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

    char get_base64_char(char byte);

    void base64encode(char *output, const char *input, size_t input_length) ;

    struct TokenDetails {
        std::string token;
        std::string expiration_time;
    };

    TokenDetails get_token(ClientContext &context, const KeyValueSecret* kv_secret) ;
    std::string get_token_and_cache(ClientContext &context, CatalogTransaction &transaction, const KeyValueSecret* kv_secret) ;

    
}