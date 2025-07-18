---
title: DuckDB GSheets
description: A DuckDB extension for reading and writing Google Sheets with SQL.
hide_title: true
---

<h1 class="markdown flex items-center gap-2"><img src="icon-512.png" style="height: 1em;"/> DuckDB GSheets</h1>

<Alert status="warning">

**🚧 Experimental 🚧** Here be dragons
 
</Alert>


A DuckDB extension for reading and writing Google Sheets with SQL.

_Note: This project is not affliated with Google or DuckDB, it is a community extension maintained by [Evidence](https://evidence.dev)._

## Install

```sql
INSTALL gsheets FROM community;
LOAD gsheets;
```

The latest version of [DuckDB](https://duckdb.org/docs/installation) (currently 1.3.2) is supported.

## Usage 

### Authenticate

```sql
-- Authenticate with Google Account in the browser (default)
CREATE SECRET (TYPE gsheet);


-- OR create a secret with your Google API access token (boring, see below guide)
CREATE SECRET (
    TYPE gsheet, 
    PROVIDER access_token, 
    TOKEN '<your_token>'
);


-- OR create a non-expiring JSON secret with your Google API private key 
-- (This enables use in non-interactive workflows like data pipelines)
-- (see "Getting a Google API Access Private Key" below)
CREATE SECRET (
    TYPE gsheet, 
    PROVIDER key_file, 
    FILEPATH '<path_to_JSON_file_with_private_key>'
);
```

### Read

```sql
-- Read a spreadsheet by full URL
FROM read_gsheet('https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit');

-- Read a spreadsheet by full URL, implicitly
FROM 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit';

-- Read a spreadsheet by spreadsheet id
FROM read_gsheet('11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8');

-- Read a spreadsheet with no header row
SELECT * FROM read_gsheet('11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8', header=false);

-- Read all values in as varchar, skipping type inference
SELECT * FROM read_gsheet('11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8', all_varchar=true);

-- Read a sheet other than the first sheet using the sheet name
SELECT * FROM read_gsheet('11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8', sheet='Sheet2');

-- Read a spreadsheet using a specific range
SELECT * FROM read_gsheet('11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8', sheet='Sheet1', range='B1:C7');
    -- or using A1 notation
SELECT * FROM read_gsheet('11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8', sheet='Sheet1!B1:C7');
    -- or from range in URL
SELECT * FROM read_gsheet('https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit?gid=0#gid=0&range=B1:C7');

-- Read a sheet other than the first sheet using the sheet id in the URL
SELECT * FROM read_gsheet('https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit?gid=644613997#gid=644613997');
```

### Write

```sql
-- Write a spreadsheet from a table by spreadsheet id
COPY <table_name> TO '11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8' (FORMAT gsheet);

-- Write a spreadsheet from a table by full URL
COPY <table_name> TO 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit?usp=sharing' (FORMAT gsheet);

-- Write a spreadsheet to a specific sheet using the sheet id in the URL
COPY <table_name> TO 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit?gid=1295634987#gid=1295634987' (FORMAT gsheet);

-- Write a spreadsheet to a specific range using the range in the URL
copy <table_name> to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit?gid=1385451074#gid=1385451074&range=C6:E10' (format gsheet);

-- Write a spreadsheet to a specific sheet with the sheet parameter
-- NOTE: A sheet parameter will take precedence over the query string
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, sheet 'Woot');

-- Write a spreadsheet to a specific range with the range parameter
-- NOTE: A range parameter will take precedence over the query string
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, sheet 'Woot', range 'B2:C10000');

-- Only overwrite the range that is being written to
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, sheet 'Woot', range 'B2:C10000', overwrite_range TRUE);

-- Overwrite the entire sheet (this is the default)
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, sheet 'Woot', range 'B2:C10000', overwrite_sheet TRUE);

-- Append below existing data
-- (by default, no header will be included)
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, sheet 'Woot', range 'B2:C10000', overwrite_sheet FALSE, overwrite_range FALSE);

-- Append below existing data, but force the output of a header
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, sheet 'Woot', range 'B2:C10000', overwrite_sheet FALSE, overwrite_range FALSE, header TRUE);

-- Write with no header
copy <table_name> 
to 'https://docs.google.com/spreadsheets/d/11QdEasMWbETbFVxry-SsD8jVcdYIT1zBQszcF84MdE8/edit' 
(format gsheet, header FALSE);
```

## Getting a Google API Access Token

To connect DuckDB to Google Sheets via an access token, you’ll need to create a Service Account through the Google API, and use it to generate an access token:

1. Navigate to the [Google API Console](https://console.developers.google.com/apis/library).
2. Create a new project.
3. Search for the Google Sheets API and enable it.
4. In the left-hand navigation, go to the **Credentials** tab.
5. Click **+ Create Credentials** and select **Service Account**.
6. Name the Service Account and assign it the **Owner** role for your project. Click **Done** to save.
7. From the **Service Accounts** page, click on the Service Account you just created.
8. Go to the **Keys** tab, then click **Add Key** > **Create New Key**.
9. Choose **JSON**, then click **Create**. The JSON file will download automatically.
10. Download and install the [gcloud CLI](https://cloud.google.com/sdk/docs/install).
11. Run the following command to login to the gcloud CLI with the Service Account using the newly created JSON file
    ```bash
    gcloud auth activate-service-account --key-file /path/to/key/file
    ```
12. Run the following command to generate an access token:
    ```bash
    gcloud auth print-access-token --scopes=https://www.googleapis.com/auth/spreadsheets
    ```
13. Open your Google Sheet and share it with the Service Account email.
14. Run DuckDB and load the extension

This token will periodically expire - you can re-run the above command again to generate a new one.

## Getting a Google API Access Private Key

Follow steps 1-9 above to get a JSON file with your private key inside.

Include the path to the file as the `FILEPATH` parameter when creating a secret.
Ex: `CREATE SECRET (TYPE gsheet, PROVIDER key_file, FILEPATH '<path_to_JSON_file_with_private_key>');`

You can skip steps 10, 11, and 12 since this extension will convert from your JSON file to a token on your behalf!
The contents of the JSON file will be stored in the secret, as will the temporary token.

Follow steps 13 and 14.

This private key by default will not expire. Use caution with it. 

This will also require an additional API request approximately every 30 minutes.

## Limitations / Known Issues

- DuckDB WASM is not (yet) supported.
- Google Sheets has a limit of 10,000,000 cells per spreadsheet.
- Sheets must already exist to COPY TO them.

## Support 

If you are having problems, find a bug, or have an idea for an improvement, please [file an issue on GitHub](https://github.com/evidence-dev/duckdb_gsheets).
