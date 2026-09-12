"""
Downloads the Project Gutenberg Top 100 corpus (last 30 days) from a local
HTML export of https://www.gutenberg.org/browse/scores/top#books-last30

Each file is saved as:
    <title_in_snake_case>_(read_count)-<book_id>.txt

Example:
    pride_and_prejudice_by_jane_austen_(177409)-1342.txt

Usage on Windows 10:
    1. Install Python 3 if needed: https://www.python.org/downloads/
    2. Place this script and the_project_gutenberg-top_100_ebooks_last_30_days.html in the same folder.
    3. Run:
         py download_gutenberg_corpus.py

Requires only the Python standard library (no pip install needed).
"""

import os
import re
import time
import urllib.request
import urllib.error

# --- Configuration -----------------------------------------------------------
# Constants are uppercase because they represent fixed configuration values,
# not data that changes while the script runs.

HTML_FILE_PATH = "the_project_gutenberg-top_100_ebooks_last_30_days.html"
OUTPUT_DIRECTORY = "gutenberg_corpus"
REQUEST_DELAY_SECONDS = 1.0   # be polite to Gutenberg's servers between requests
REQUEST_TIMEOUT_SECONDS = 30

# Gutenberg rejects requests without a browser-like User-Agent.
HTTP_HEADERS = {
    "User-Agent": (
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        "AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/120.0 Safari/537.36"
    )
}

BOOK_LINK_PATTERN = re.compile(
    r'<a href="(https://www\.gutenberg\.org/ebooks/(\d+)\.txt\.utf-8)">(.*?)</a>'
)


# --- Core logic ----------------------------------------------------------------

def convertTitleToSnakeCase(title):
    """
    Converts a book title into snake_case while preserving the trailing
    "(read count)" so the resulting filename still carries that metadata.
    """
    normalizedTitle = title.lower().strip().replace(" ", "_")
    # Strip anything that is not a letter, digit, underscore, hyphen or
    # parenthesis, since those are the only characters we want in a filename.
    normalizedTitle = re.sub(r"[^a-z0-9_\-\(\)]", "", normalizedTitle)
    normalizedTitle = re.sub(r"_+", "_", normalizedTitle)
    return normalizedTitle.strip("_")


def parseBookEntries(htmlFilePath):
    """Extracts (bookId, title, downloadUrl) for every book listed in the HTML."""
    with open(htmlFilePath, "r", encoding="utf-8") as htmlFile:
        htmlContent = htmlFile.read()

    return [
        (bookId, title.strip(), downloadUrl)
        for downloadUrl, bookId, title in BOOK_LINK_PATTERN.findall(htmlContent)
    ]


def downloadBookFile(downloadUrl, destinationPath):
    """Downloads a single plain-text file. Returns True on success."""
    request = urllib.request.Request(downloadUrl, headers=HTTP_HEADERS)
    try:
        with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
            fileContent = response.read()
        with open(destinationPath, "wb") as outputFile:
            outputFile.write(fileContent)
        return True
    except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError) as error:
        print(f"    [ERROR] {error}")
        return False


def buildFilename(bookId, title):
    """Builds the '<snake_case_title>-<bookId>.txt' filename used for storage."""
    snakeCaseTitle = convertTitleToSnakeCase(title)
    return f"{snakeCaseTitle}-{bookId}.txt"


# --- Entry point ---------------------------------------------------------------

def main():
    if not os.path.isfile(HTML_FILE_PATH):
        print(f"Could not find '{HTML_FILE_PATH}' in the current folder.")
        print("Place the_project_gutenberg-top_100_ebooks_last_30_days.html next to this script and try again.")
        return

    os.makedirs(OUTPUT_DIRECTORY, exist_ok=True)

    bookEntries = parseBookEntries(HTML_FILE_PATH)
    totalBooks = len(bookEntries)
    print(f"Found {totalBooks} books in '{HTML_FILE_PATH}'.\n")

    successCount = 0
    failedDownloads = []

    for index, (bookId, title, downloadUrl) in enumerate(bookEntries, start=1):
        filename = buildFilename(bookId, title)
        destinationPath = os.path.join(OUTPUT_DIRECTORY, filename)

        # Skipping existing files makes the script safe to re-run after a
        # partial failure, without re-downloading what already succeeded.
        if os.path.exists(destinationPath):
            print(f"[{index}/{totalBooks}] Already exists, skipping: {filename}")
            successCount += 1
            continue

        print(f"[{index}/{totalBooks}] Downloading: {filename}")
        wasSuccessful = downloadBookFile(downloadUrl, destinationPath)

        if wasSuccessful:
            successCount += 1
        else:
            failedDownloads.append((bookId, title, downloadUrl))

        time.sleep(REQUEST_DELAY_SECONDS)

    print("\n--- Summary ---")
    print(f"Downloaded/OK: {successCount}/{totalBooks}")
    print(f"Failed: {len(failedDownloads)}")

    if failedDownloads:
        print("\nBooks that failed:")
        for bookId, title, downloadUrl in failedDownloads:
            print(f"  - [{bookId}] {title} -> {downloadUrl}")


if __name__ == "__main__":
    main()
