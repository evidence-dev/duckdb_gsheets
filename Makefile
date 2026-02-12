PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=gsheets
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Custom test targets
.PHONY: test_unit test_unit_build test_sql test_all

# Build unit tests (standalone, doesn't require full DuckDB build)
test_unit_build:
	mkdir -p build/unit_tests
	cmake -G "Ninja" -S test/unit -B build/unit_tests
	cmake --build build/unit_tests

# Run unit tests (builds if needed)
test_unit: test_unit_build
	./build/unit_tests/unit_tests

# SQL tests (SQLLogicTests via DuckDB test runner)
test_sql: test_release

test_sql_debug: test_debug

# Run all tests (unit + SQL)
test_all: test_unit test_sql

test_all_debug: test_unit test_sql_debug

# Clean unit test build
clean_unit_tests:
	rm -rf build/unit_tests
