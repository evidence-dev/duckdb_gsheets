import os
from google.oauth2 import service_account
from google.auth.transport.requests import Request

def get_token_from_user_file(user_file_path):
    SCOPES = ["https://www.googleapis.com/auth/spreadsheets"]

    credentials = service_account.Credentials.from_service_account_file(
        user_file_path,
        scopes=SCOPES
        )

    request = Request()
    credentials.refresh(request)
    return credentials.token

key_file_path = "credentials.json"
token = get_token_from_user_file(key_file_path)

env_file = os.getenv('GITHUB_ENV')

with open(env_file, "a") as myfile:
    # Set the token as an env var for some tests
    myfile.write(f"TOKEN={token}\n")
    # Set the key_file filepath as an env var for other tests
    myfile.write(f"KEY_FILE_PATH={key_file_path}")

print('It seems to have worked?')
