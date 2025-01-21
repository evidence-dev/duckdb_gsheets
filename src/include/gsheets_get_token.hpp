#pragma once

#include <string>
#include <stdlib.h>
#include "duckdb/main/secret/secret_manager.hpp"

namespace duckdb {

    char get_base64_char(char byte);

    void base64encode(char *output, const char *input, size_t input_length) ;

    std::string get_token(const KeyValueSecret* kv_secret) ;

}