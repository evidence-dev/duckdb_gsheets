#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

info() { echo "==> $*"; }

# Require key file path as argument or env var
KEY_FILE="${1:-${KEY_FILE_PATH:-}}"
if [ -z "$KEY_FILE" ]; then
    echo "Usage: $0 <path-to-keyfile.json>" >&2
    echo "  or set KEY_FILE_PATH env var" >&2
    exit 1
fi

# Set up venv and install dependencies
VENV_DIR="$PROJECT_DIR/.venv"
if [ ! -d "$VENV_DIR" ]; then
    info "Creating Python venv at $VENV_DIR"
    python3 -m venv "$VENV_DIR"
fi
info "Installing Python dependencies"
"$VENV_DIR/bin/pip" install -r "$SCRIPT_DIR/requirements.txt"

# Generate credentials
info "Generating access token from key file"
export KEY_FILE_PATH="$KEY_FILE"
export TOKEN
TOKEN="$("$VENV_DIR/bin/python" "$SCRIPT_DIR/generate_google_token.py" "$KEY_FILE")"
info "KEY_FILE_PATH=$KEY_FILE_PATH"
info "TOKEN generated (${#TOKEN} chars)"

# Build and run SQL tests
cd "$PROJECT_DIR"
if [ ! -f "./build/release/test/unittest" ]; then
    info "Building extension (make release)"
    make release
fi
info "Running SQL tests (make test)"
make test
