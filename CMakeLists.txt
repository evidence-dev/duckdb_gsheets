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
    src/sheets/auth/oauth_listener.cpp
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
    src/utils/options.cpp
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

# duckdb's vendored httplib now loads TLS roots from the macOS Keychain by
# default (CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN), which needs Security.framework.
if(APPLE)
  target_link_libraries(${EXTENSION_NAME} "-framework Security" "-framework CoreFoundation")
  target_link_libraries(${LOADABLE_EXTENSION_NAME} "-framework Security" "-framework CoreFoundation")
endif()

# The local OAuth listener (src/sheets/auth/oauth_listener.cpp) uses the
# vendored httplib::Server, which uses Winsock on Windows. httplib.hpp's
# #pragma comment(lib, "ws2_32.lib") only takes effect for MSVC, so link
# ws2_32 explicitly here for other toolchains (e.g. MinGW/Ninja generators),
# matching test/integration/CMakeLists.txt.
if(WIN32)
  target_link_libraries(${EXTENSION_NAME} ws2_32)
  target_link_libraries(${LOADABLE_EXTENSION_NAME} ws2_32)
endif()

# oauth_listener.cpp's Catch2 tests (test/unit/sheets/auth/test_oauth_listener.cpp)
# can't link into the standalone test/unit target (see its CMakeLists.txt),
# so they're built here instead, as part of the full extension build - same
# condition DuckDB itself uses to decide whether to build its own unittest
# runner.
if(${BUILD_UNITTESTS})
  add_subdirectory(test/integration)
endif()

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
