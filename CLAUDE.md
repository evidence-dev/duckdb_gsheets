# CLAUDE.md

DuckDB extension for reading and writing Google Sheets.

## Quick Reference

- **Build:** `GEN=ninja make`
- **Unit tests:** `make test_unit`
- **SQL tests:** `TOKEN=your-token make test` or `./scripts/test_sql.sh path/to/keyfile.json`
- **Format:** `make format`

## Architecture

The extension has three layers: **Transport** (HTTP client abstraction), **Auth** (bearer token, OAuth, service account), and **Service** (Google Sheets API client with spreadsheet/values resources). See `src/include/sheets/` for the interfaces.

## Contributing

See [docs/pages/development.md](docs/pages/development.md) for build setup, testing, repo layout, and CI pipeline details.
