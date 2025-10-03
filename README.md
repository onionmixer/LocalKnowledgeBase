# LKB (Local Knowledge Base)

A high-performance C-based middleware service that bridges Open WebUI with Manticore Search, translating search requests and transforming results between the two systems.

## Features

- **Pure C Implementation**: Lightweight, fast, and minimal dependencies
- **Manticore Search Integration**: Full support for Manticore Search HTTP API
- **Query Translation**: Automatic conversion between Open WebUI and Manticore formats
- **Template-Based Queries**: Flexible query customization via templates
- **UTF-8 Safe**: Proper UTF-8 handling for multilingual content
- **Memory Safe**: Comprehensive memory management with safe allocation functions
- **Graceful Shutdown**: Signal handling for clean resource cleanup
- **Configurable**: YAML-based configuration with section hierarchy support
- **URL Encoding**: RFC 3986 compliant URL encoding for special characters

## Architecture

```
Open WebUI → LKB (C Server) → Manticore Search
    ↓            ↓                    ↓
  POST         Template            HTTP API
/search      rule_manticore.txt   /search
```

### Request Flow

1. **Input (from Open WebUI)**
   - Endpoint: `POST http://localhost:7777/search`
   - Format: `{"query": "search_keyword", "count": 3}`

2. **Processing (LKB)**
   - Query normalization and cleaning
   - Template variable substitution
   - HTTP request to Manticore Search

3. **Output (to Manticore Search)**
   - Endpoint: `POST http://127.0.0.1:29308/search`
   - Format: `{"index":"wiki_main","query":{"match":{"*":"search_keyword"}},"limit":5}`

4. **Response (back to Open WebUI)**
   - Format: `{"results": [{"link": "...", "title": "...", "snippet": "..."}], "took_ms": 10, "total": 5, "engine": "manticore"}`

## Installation

### Prerequisites

- GCC compiler
- Linux/Unix system (tested on Linux 6.8.0)
- Manticore Search instance (optional, for full functionality)

### Building

```bash
# Compile the server
gcc -o LocalKnowledgeBase LocalKnowledgeBase.c -Wall -Wextra

# Or use the provided compilation script
chmod +x start.sh
```

## Configuration

Edit `config.yaml` to customize server settings:

```yaml
# LKB Server Settings
lkb:
  listen: "0.0.0.0"  # Bind address
  port: 7777          # Server port

# Search Engine Settings
engine:
  type: "manticore"
  url: "http://127.0.0.1:29308/search"
  index_name: "wiki_main"
  replace_return_url: "http://localhost/mediawiki/index.php/"
  search_count: 5       # Default result count
  snippet_length: 200   # Max snippet length (bytes)
```

### Template Customization

Edit `rule_manticore.txt` to customize Manticore Search queries:

```json
{
  "index": "{INDEX_NAME}",
  "query": {
    "match": {
      "*": "{SEARCH_QUERY}"
    }
  },
  "limit": {RESULT_LIMIT}
}
```

**Available variables:**
- `{INDEX_NAME}`: From config.yaml `engine.index_name`
- `{SEARCH_QUERY}`: User's search query
- `{RESULT_LIMIT}`: From request `count` or config default

## Usage

### Starting the Server

```bash
# Start the server
./LocalKnowledgeBase

# Or run in background
./LocalKnowledgeBase > /var/log/lkb.log 2>&1 &
```

Expected output:
```
LocalKnowledgeBase C Server
✓ Server running on http://0.0.0.0:7777
✓ Manticore Search integration enabled
  - Host: 127.0.0.1:29308
  - Index: wiki_main
  - Base URL: http://localhost/mediawiki/index.php/
  - Default search count: 5
  - Snippet length: 200

Press Ctrl+C to stop
```

### Stopping the Server

```bash
# Graceful shutdown (recommended)
kill -INT <pid>

# Or press Ctrl+C if running in foreground
```

### Testing

**Test search endpoint:**
```bash
curl -X POST "http://localhost:7777/search" \
  -H "Content-Type: application/json" \
  -d '{"query": "hello world", "count": 3}'
```

**Test health check:**
```bash
curl http://localhost:7777/
```

**Test Manticore Search directly:**
```bash
curl -s 'http://127.0.0.1:29308/search' \
  -H 'Content-Type: application/json' \
  -d '{"index":"wiki_main","query":{"match":{"*":"test"}},"limit":5}'
```

## API Reference

### POST /search

Search for documents via Manticore Search.

**Request:**
```json
{
  "query": "search term",
  "count": 5
}
```

**Response:**
```json
{
  "results": [
    {
      "link": "http://localhost/mediawiki/index.php/Article_Title",
      "title": "Article Title",
      "snippet": "Preview text from the document..."
    }
  ],
  "took_ms": 15,
  "total": 1,
  "engine": "manticore"
}
```

### GET /

Health check endpoint.

**Response:**
```json
{
  "status": "running",
  "service": "LocalKnowledgeBase",
  "version": "1.0"
}
```

## Advanced Features

### Query Normalization

LKB automatically normalizes search queries:
- Removes `<think>` tags
- Trims whitespace
- Extracts queries from JSON/array formats
- Handles nested query structures
- Limits query length to prevent abuse

### URL Encoding

Automatic RFC 3986 compliant URL encoding:
- Safe characters: `A-Z a-z 0-9 - _ . ~`
- Spaces converted to `_` (MediaWiki style)
- Special characters encoded as `%XX`

### UTF-8 Support

- Safe UTF-8 truncation for snippets
- Preserves multi-byte character boundaries
- Prevents broken characters in responses

### Memory Management

- Safe allocation functions with error checking
- Automatic cleanup on exit
- No memory leaks (validated)
- Stack overflow prevention (heap allocation for large buffers)

### Signal Handling

- `SIGINT` (Ctrl+C): Graceful shutdown
- `SIGTERM`: Graceful shutdown
- Resource cleanup on exit
- Template cache cleanup
- Socket cleanup

## Development

### Debug Mode

Compile with debug flag to enable detailed logging:

```bash
gcc -o LocalKnowledgeBase LocalKnowledgeBase.c -Wall -Wextra -DDEBUG
```

Debug logs will be written to `02_search.log`.

### Project Structure

```
LKB/
├── LocalKnowledgeBase.c      # Main server implementation
├── config.yaml                # Configuration file
├── rule_manticore.txt         # Manticore query template
├── CLAUDE.md                  # Development guide
├── README.md                  # This file
└── legacy/
    └── LocalKnowledgeBase.py  # Original Python implementation
```

### Code Organization

- **Signal Handlers & Cleanup** (82-110)
- **Utility Functions** (112-195)
- **Config Parser** (197-387)
- **String Processing** (389-410)
- **JSON Parsing** (412-568)
- **File I/O & Templates** (570-650)
- **HTTP Client** (652-743)
- **Manticore Integration** (745-900)
- **JSON Response Generation** (902-1023)
- **HTTP Server** (1025-1116)
- **Main** (1118-1201)

## Performance

- **Memory Usage**: ~2-4 MB base + request buffers
- **Response Time**: <10ms (excluding Manticore latency)
- **Concurrency**: Single-threaded, sequential request handling
- **Max Request Size**: 2MB (configurable via `BUFFER_SIZE`)

## Troubleshooting

### Server won't start

```
bind failed: Address already in use
```
**Solution**: Port 7777 is already in use. Kill existing process or change port in `config.yaml`.

### No results returned

```
[Manticore] Error: No response
```
**Solution**:
1. Check Manticore Search is running: `curl http://127.0.0.1:29308/`
2. Verify `config.yaml` engine.url is correct
3. Check `rule_manticore.txt` exists

### Connection refused

```
connection failed: Connection refused
```
**Solution**: Manticore Search is not running or wrong host/port configured.

### Invalid request format

```
[HTTP] Invalid request format
```
**Solution**: Ensure requests include both method and path in HTTP header.

## License

This project is provided as-is for integration with Open WebUI and Manticore Search.

## Contributing

When contributing, please:
1. Follow the existing code style
2. Test with `gcc -Wall -Wextra`
3. Ensure no memory leaks (`valgrind` recommended)
4. Update documentation for new features

## Related Projects

- [Open WebUI](https://github.com/open-webui/open-webui)
- [Manticore Search](https://manticoresearch.com/)
- [MediaWiki](https://www.mediawiki.org/)

## Contact

For issues or questions, please refer to the project repository.
