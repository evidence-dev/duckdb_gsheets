cmake_minimum_required(VERSION 3.5)

# Set extension name here
set(TARGET_NAME gsheets)

# Find OpenSSL package
find_package(OpenSSL REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include)
include_directories(third_party)
include_directories(duckdb/third_party/httplib)

# Extension sources
set(EXTENSION_SOURCES
    src/gsheets_extension.cpp
    src/gsheets_auth.cpp
    src/gsheets_copy.cpp
    src/gsheets_read.cpp
    src/gsheets_utils.cpp
    src/sheets/auth/bearer_token_auth.cpp
    src/sheets/auth/oauth_auth.cpp
    src/sheets/auth/service_account_auth.cpp
    src/sheets/resources/base.cpp
    src/sheets/resources/values.cpp
    src/sheets/resources/spreadsheet.cpp
    src/sheets/transport/http_client.cpp
    src/sheets/transport/httplib_client.cpp
    src/sheets/transport/duckdb_http_client.cpp
    src/sheets/transport/mock_http_client.cpp
    src/sheets/transport/client_factory.cpp
    src/sheets/util/encoding.cpp
    src/sheets/range.cpp
    src/sheets/auth_factory.cpp
    src/utils/secret.cpp
    src/utils/proxy.cpp
    src/utils/version.cpp)

# Warn on unused/dead code (GCC/Clang only; MSVC uses different flag syntax)
add_compile_options($<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wunused-function>)
add_compile_options($<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wunreachable-code>)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link OpenSSL in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)
target_link_libraries(${LOADABLE_EXTENSION_NAME} OpenSSL::SSL OpenSSL::Crypto)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
