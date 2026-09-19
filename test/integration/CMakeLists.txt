# Catch2 tests for code that can't link into the standalone, no-full-DuckDB
# test/unit target (test/unit/CMakeLists.txt): currently just
# oauth_listener.cpp, which is built on httplib::Server and therefore needs
# DuckDB's re2 regex wrapper and Exception-formatting machinery. Unlike
# test/unit, this target is NOT standalone - it links duckdb_static, the same
# target the full extension build already produces the same way the extension
# library itself does (see build_static_extension in
# duckdb/extension/extension_build_tools.cmake) - so it only builds as part of
# the full build (`GEN=ninja make`), not on its own. See
# docs/pages/development.md.
#
# This directory is only ever processed via add_subdirectory() from the
# extension's root CMakeLists.txt, itself pulled into DuckDB's own CMake project
# (see extension_config.cmake) - so include paths set there (src/include,
# third_party, duckdb/third_party/httplib) and targets like
# duckdb_static/OpenSSL::SSL are already available here.

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../duckdb/third_party/catch)

add_executable(
  oauth_listener_tests
  ${CMAKE_CURRENT_SOURCE_DIR}/../unit/test_main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../unit/sheets/auth/test_oauth_listener.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/sheets/auth/oauth_listener.cpp)

# link_extension_libraries (duckdb/extension/extension_build_tools.cmake) is how
# DuckDB's own unittest/shell binaries pull in duckdb_generated_extension_loader
# (needed for ExtensionHelper::LoadAllExtensions, referenced by DuckDB::DuckDB's
# constructor even though this binary never calls it) - reusing it here instead
# of hand-picking symbols keeps this in sync with however that dependency
# evolves upstream.
link_extension_libraries(oauth_listener_tests "")
target_link_libraries(oauth_listener_tests duckdb_static OpenSSL::SSL
                      OpenSSL::Crypto)

# Winsock (required by httplib on Windows; #pragma comment covers MSVC, but this
# makes it explicit for other Windows toolchains/generators), matching the main
# extension target's own ws2_32 linking.
if(WIN32)
  target_link_libraries(oauth_listener_tests ws2_32)
endif()
