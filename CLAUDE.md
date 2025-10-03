# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

LKB (Local Knowledge Base) is a middleware service that bridges Open WebUI with external search engines, specifically Manticore Search. It acts as a translation layer between Open WebUI's search request format and Manticore Search's query format.

## Architecture

### Request Flow

```
Open WebUI → LKB (FastAPI) → Manticore Search
         ↓                  ↓
    sample_from_outer.txt   sample_manticore.txt
```

1. **Input (from Open WebUI)**: POST request to `http://localhost:8080/search`
   - Format: `{"query": "search_keyword", "count": 3}`
   - See `sample_from_outer.txt` for exact format

2. **Output (to Manticore Search)**: POST request to `http://127.0.0.1:29308/search`
   - Format: `{"index":"wiki_main","query":{"match":{"*":"search_keyword"}},"limit":5}`
   - See `sample_manticore.txt` for exact format

3. **Response (back to Open WebUI)**: Array of search results
   - Format: `[{"link": "...", "title": "...", "snippet": "..."}]`

### Core Components

- **LimitedKnowledgeBase.py**: FastAPI-based middleware service (currently returns dummy data)
  - Receives search requests from Open WebUI
  - Should translate queries to Manticore Search format
  - Should transform Manticore results back to Open WebUI format
  - Runs on port 7777

- **example_SphinxSearch/**: Reference MediaWiki extension for Sphinx/Manticore integration
  - Contains Sphinx/Manticore configuration examples
  - `sphinx.conf`: Database source configuration and indexing settings
  - Compatible with both SphinxSearch and ManticoreSearch

## Development Commands

### Running the Service

```bash
./start.sh                    # Start the middleware service
# OR
python3 LimitedKnowledgeBase.py      # Run directly
```

The service will be available at `http://0.0.0.0:7777`

### Testing

Test Open WebUI-style requests:
```bash
curl -X POST "http://localhost:7777/search" \
  -H "Content-Type: application/json" \
  -d '{"query": "sample_keyword", "count": 3}'
```

Test Manticore Search directly:
```bash
curl -s 'http://127.0.0.1:29308/search' \
  -H 'Content-Type: application/json' \
  -d '{"index":"wiki_main","query":{"match":{"*":"search_keyword"}},"limit":5}'
```

## Key Implementation Notes

- The middleware currently returns dummy data and needs actual Manticore Search integration
- Default Manticore index name is `wiki_main`
- Manticore Search default port is 29308 (HTTP API)
- Open WebUI expects results with `link`, `title`, and `snippet` fields
- The service should handle query translation and result transformation between the two systems
