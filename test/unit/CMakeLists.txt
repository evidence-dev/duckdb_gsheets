cmake_minimum_required(VERSION 3.5)
project(gsheets_unit_tests)

# Derive extension root from current location
get_filename_component(EXT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Find OpenSSL (required for ServiceAccountAuth)
find_package(OpenSSL REQUIRED)

# Include paths
include_directories(${EXT_ROOT}/duckdb/third_party/catch)
include_directories(${EXT_ROOT}/duckdb/third_party/httplib)
include_directories(${EXT_ROOT}/duckdb/src/include)
include_directories(${EXT_ROOT}/src/include)
include_directories(${EXT_ROOT}/third_party)

# Test sources
set(UNIT_TEST_SOURCES
    test_main.cpp
    # Stubs for DuckDB types (avoids linking full DuckDB library)
    stubs/duckdb_stubs.cpp
    # Util tests
    sheets/util/test_encoding.cpp
    ${EXT_ROOT}/src/sheets/util/encoding.cpp
    # Auth tests
    sheets/auth/test_auth.cpp
    ${EXT_ROOT}/src/sheets/auth/bearer_token_auth.cpp
    ${EXT_ROOT}/src/sheets/auth/oauth_auth.cpp
    ${EXT_ROOT}/src/sheets/auth/service_account_auth.cpp
    # Transport (needed for auth tests)
    ${EXT_ROOT}/src/sheets/transport/http_client.cpp
    ${EXT_ROOT}/src/sheets/transport/mock_http_client.cpp
)

add_executable(unit_tests ${UNIT_TEST_SOURCES})

# Link OpenSSL
target_link_libraries(unit_tests OpenSSL::SSL OpenSSL::Crypto)

# Enable testing
enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
