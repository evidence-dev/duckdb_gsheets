PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=gsheets
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Custom test targets
.PHONY: test_unit test_unit_build test_oauth_listener test_oauth_listener_debug test_sql test_all test_all_debug

# Build unit tests (standalone, doesn't require full DuckDB build)
test_unit_build:
	mkdir -p build/unit_tests
	cmake -G "Ninja" -S test/unit -B build/unit_tests
	cmake --build build/unit_tests

# Run unit tests (builds if needed)
test_unit: test_unit_build
	./build/unit_tests/unit_tests

# Run the OAuth listener's Catch2 tests (RunLocalOAuthListener integration
# tests, CSRF/paste-fallback unit tests, etc.). Unlike test_unit, these can't
# be standalone: oauth_listener.cpp is built on httplib::Server, which needs
# DuckDB's re2 regex wrapper and Exception-formatting machinery - see
# test/integration/CMakeLists.txt - so this builds (and links) as part of the
# full extension build.
test_oauth_listener: release
	./build/release/extension/gsheets/test/integration/oauth_listener_tests

test_oauth_listener_debug: debug
	./build/debug/extension/gsheets/test/integration/oauth_listener_tests

# SQL tests (SQLLogicTests via DuckDB test runner)
test_sql: test_release

test_sql_debug: test_debug

# Run all tests (unit + OAuth listener + SQL)
test_all: test_unit test_oauth_listener test_sql

test_all_debug: test_unit test_oauth_listener_debug test_sql_debug

# Clean unit test build
clean_unit_tests:
	rm -rf build/unit_tests
