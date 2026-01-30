"""Generate a Google OAuth2 access token from a service account key file.

Usage:
    python scripts/generate_google_token.py <path-to-keyfile.json>

Prints the access token to stdout.
"""

import sys

from google.auth.transport.requests import Request
from google.oauth2 import service_account

SCOPES = ["https://www.googleapis.com/auth/spreadsheets"]


def get_token(key_file_path: str) -> str:
    credentials = service_account.Credentials.from_service_account_file(
        key_file_path, scopes=SCOPES
    )
    credentials.refresh(Request())
    return credentials.token


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <key-file-path>", file=sys.stderr)
        sys.exit(1)
    print(get_token(sys.argv[1]))
