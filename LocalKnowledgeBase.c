/*
 * LocalKnowledgeBase - Full C Implementation with Manticore Search Integration
 *
 * Features:
 * - HTTP Server (socket-based)
 * - HTTP Client (for Manticore Search)
 * - Template loading and substitution
 * - Manticore Search API integration
 * - Result transformation
 * - Query normalization
 * - JSON parsing and generation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <time.h>
#include <stdbool.h>
#include <signal.h>

#ifdef DEBUG
#define LOG_FILE "02_search.log"
#endif

#define BUFFER_SIZE 2097152  /* 2MB buffer for large responses */
#define MAX_QUERY_LEN 1024
#define MAX_QUERIES 10
#define MAX_RESULTS 50
#define MAX_CONFIG_LINE 512
#define MAX_SNIPPET_LEN 200
#define DEFAULT_PORT 7777
#define DEFAULT_SEARCH_COUNT 5

/* 설정 구조체 */
typedef struct {
    char listen[64];
    int port;
    char engine_type[32];
    char engine_url[256];
    char manticore_host[128];
    int manticore_port;
    char manticore_path[64];
    char index_name[128];
    char base_url[256];
    int search_count;
    int snippet_length;
} Config;

/* 검색 요청 구조체 */
typedef struct {
    char *query;
    char *queries[MAX_QUERIES];
    int queries_count;
    int count;
} SearchRequest;

/* 검색 결과 구조체 */
typedef struct {
    char *link;
    char *title;
    char *snippet;
} SearchResult;

/* 템플릿 캐시 */
typedef struct {
    char *content;
    bool loaded;
} TemplateCache;

/* 전역 변수 */
static Config g_config;
static TemplateCache g_template_cache = {NULL, false};
static volatile sig_atomic_t g_running = 1;
static int g_server_fd = -1;

/* ============================
 * 시그널 핸들러 및 정리 함수
 * ============================ */

void cleanup_resources() {
    printf("\n[Server] Cleaning up resources...\n");

    /* 템플릿 캐시 해제 */
    if (g_template_cache.content) {
        free(g_template_cache.content);
        g_template_cache.content = NULL;
        g_template_cache.loaded = false;
    }

    /* 서버 소켓 닫기 */
    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }

    printf("[Server] Cleanup complete\n");
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        g_running = 0;
        printf("\n[Server] Received signal %d, shutting down...\n", signum);

        /* accept() 블로킹 해제를 위해 서버 소켓 종료 */
        if (g_server_fd >= 0) {
            shutdown(g_server_fd, SHUT_RDWR);
        }
    }
}

/* ============================
 * 유틸리티 및 문자열 처리 함수
 * ============================ */

/* 안전한 문자열 복사 */
void safe_strncpy(char *dest, const char *src, size_t dest_size) {
    if (!dest || !src || dest_size == 0) return;
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

/* 메모리 할당 with 오류 검사 */
void* safe_malloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "[ERROR] Memory allocation failed: %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void* safe_realloc(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "[ERROR] Memory reallocation failed: %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return new_ptr;
}

char* safe_strdup(const char *str) {
    if (!str) return NULL;
    char *new_str = strdup(str);
    if (!new_str) {
        fprintf(stderr, "[ERROR] Memory allocation failed for strdup\n");
        exit(EXIT_FAILURE);
    }
    return new_str;
}

/* 문자열 trim */
char* trim_string(char *str) {
    if (!str) return NULL;

    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';

    return str;
}

/* URL 인코딩 (RFC 3986) */
char* url_encode(const char *str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    /* 최악의 경우 각 문자가 %XX로 인코딩 (3배) */
    char *encoded = safe_malloc(len * 3 + 1);
    char *p = encoded;

    for (const char *s = str; *s; s++) {
        unsigned char c = (unsigned char)*s;
        /* 안전한 문자: A-Z a-z 0-9 - _ . ~ */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            *p++ = c;
        }
        /* 공백은 _ 로 (MediaWiki 스타일) */
        else if (c == ' ') {
            *p++ = '_';
        }
        /* 나머지는 %XX로 인코딩 */
        else {
            sprintf(p, "%%%02X", c);
            p += 3;
        }
    }
    *p = '\0';

    return encoded;
}

#ifdef DEBUG
/* 디버그 로그 작성 함수 */
void write_debug_log(const char *section, const char *message) {
    FILE *log_file = fopen(LOG_FILE, "a");
    if (!log_file) {
        fprintf(stderr, "[DEBUG] Failed to open log file: %s\n", LOG_FILE);
        return;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(log_file, "[%s] [%s] %s\n", timestamp, section, message);
    fclose(log_file);
}
#endif

/* ============================
 * 설정 파일 파싱
 * ============================ */

/* YAML 값 추출 (간단한 key: value 형식) */
char* extract_yaml_value(const char *line) {
    const char *colon = strchr(line, ':');
    if (!colon) return NULL;

    const char *value = colon + 1;
    while (*value && isspace((unsigned char)*value)) value++;

    if (*value == '"') {
        value++;
        const char *end = strchr(value, '"');
        if (!end) return NULL;
        size_t len = end - value;
        char *result = safe_malloc(len + 1);
        strncpy(result, value, len);
        result[len] = '\0';
        return result;
    }

    char *result = safe_strdup(value);
    char *newline = strchr(result, '\n');
    if (newline) *newline = '\0';
    char *comment = strchr(result, '#');
    if (comment) *comment = '\0';

    return trim_string(result);
}

/* URL에서 host, port, path 파싱 */
void parse_url(const char *url, char *host, int host_size, int *port, char *path, int path_size) {
    const char *protocol = strstr(url, "://");
    const char *start = protocol ? protocol + 3 : url;

    const char *path_start = strchr(start, '/');
    const char *port_start = strchr(start, ':');

    if (port_start && (!path_start || port_start < path_start)) {
        size_t host_len = port_start - start;
        if (host_len >= (size_t)host_size) host_len = host_size - 1;
        strncpy(host, start, host_len);
        host[host_len] = '\0';

        *port = atoi(port_start + 1);

        if (path_start) {
            safe_strncpy(path, path_start, path_size);
        } else {
            safe_strncpy(path, "/", path_size);
        }
    } else {
        size_t host_len = path_start ? (size_t)(path_start - start) : strlen(start);
        if (host_len >= (size_t)host_size) host_len = host_size - 1;
        strncpy(host, start, host_len);
        host[host_len] = '\0';

        *port = 80;

        if (path_start) {
            safe_strncpy(path, path_start, path_size);
        } else {
            safe_strncpy(path, "/", path_size);
        }
    }
}

/* config.yaml 로드 */
bool load_config(const char *filename, Config *config) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "[Config] Warning: %s not found, using defaults\n", filename);
        return false;
    }

    /* 기본값 설정 */
    safe_strncpy(config->listen, "0.0.0.0", sizeof(config->listen));
    config->port = DEFAULT_PORT;
    safe_strncpy(config->engine_type, "manticore", sizeof(config->engine_type));
    safe_strncpy(config->engine_url, "http://127.0.0.1:29308/search", sizeof(config->engine_url));
    safe_strncpy(config->index_name, "wiki_main", sizeof(config->index_name));
    safe_strncpy(config->base_url, "http://localhost/mediawiki/index.php/", sizeof(config->base_url));
    config->search_count = DEFAULT_SEARCH_COUNT;
    config->snippet_length = MAX_SNIPPET_LEN;

    char line[MAX_CONFIG_LINE];
    char current_section[64] = "";

    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim_string(line);

        if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

        /* 섹션 감지 (들여쓰기 없는 키) */
        if (line[0] != ' ' && line[0] != '\t' && strchr(trimmed, ':')) {
            char *colon = strchr(trimmed, ':');
            size_t section_len = colon - trimmed;
            if (section_len < sizeof(current_section)) {
                strncpy(current_section, trimmed, section_len);
                current_section[section_len] = '\0';
            }
            continue;
        }

        /* lkb 섹션 */
        if (strcmp(current_section, "lkb") == 0) {
            if (strstr(trimmed, "listen:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    safe_strncpy(config->listen, value, sizeof(config->listen));
                    free(value);
                }
            } else if (strstr(trimmed, "port:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    config->port = atoi(value);
                    free(value);
                }
            }
        }
        /* engine 섹션 */
        else if (strcmp(current_section, "engine") == 0) {
            if (strstr(trimmed, "type:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    safe_strncpy(config->engine_type, value, sizeof(config->engine_type));
                    free(value);
                }
            } else if (strstr(trimmed, "url:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    safe_strncpy(config->engine_url, value, sizeof(config->engine_url));
                    free(value);
                }
            } else if (strstr(trimmed, "index_name:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    safe_strncpy(config->index_name, value, sizeof(config->index_name));
                    free(value);
                }
            } else if (strstr(trimmed, "replace_return_url:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    safe_strncpy(config->base_url, value, sizeof(config->base_url));
                    free(value);
                }
            } else if (strstr(trimmed, "search_count:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    config->search_count = atoi(value);
                    free(value);
                }
            } else if (strstr(trimmed, "snippet_length:")) {
                char *value = extract_yaml_value(trimmed);
                if (value) {
                    config->snippet_length = atoi(value);
                    free(value);
                }
            }
        }
    }

    fclose(f);

    /* engine_url에서 host, port, path 파싱 */
    parse_url(config->engine_url, config->manticore_host, sizeof(config->manticore_host),
              &config->manticore_port, config->manticore_path, sizeof(config->manticore_path));

    return true;
}

/* ============================
 * 문자열 처리 함수 (고급)
 * ============================ */

char* remove_think_tags(const char *input) {
    char *result = safe_strdup(input);
    char *src = result;
    char *dst = result;

    while (*src) {
        if (strncmp(src, "<think>", 7) == 0) {
            char *end = strstr(src, "</think>");
            if (end) {
                src = end + 8;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';

    return result;
}

/* ============================
 * JSON 파싱 함수
 * ============================ */

/* UTF-8 문자 경계를 찾는 함수 */
size_t utf8_safe_truncate(const char *str, size_t max_bytes) {
    size_t pos = 0;

    while (pos < max_bytes && str[pos] != '\0') {
        unsigned char c = (unsigned char)str[pos];

        /* ASCII 문자 (0xxxxxxx) */
        if (c < 0x80) {
            pos++;
        }
        /* 2바이트 UTF-8 (110xxxxx) */
        else if ((c & 0xE0) == 0xC0) {
            if (pos + 2 <= max_bytes) pos += 2;
            else break;
        }
        /* 3바이트 UTF-8 (1110xxxx) */
        else if ((c & 0xF0) == 0xE0) {
            if (pos + 3 <= max_bytes) pos += 3;
            else break;
        }
        /* 4바이트 UTF-8 (11110xxx) */
        else if ((c & 0xF8) == 0xF0) {
            if (pos + 4 <= max_bytes) pos += 4;
            else break;
        }
        /* 잘못된 UTF-8 시퀀스, 다음 문자로 */
        else {
            pos++;
        }
    }

    return pos;
}

/* 공통 escape 처리 함수 */
size_t unescape_json_string(const char *src, const char *end, char *dst, size_t dst_size) {
    size_t written = 0;
    while (src < end && written < dst_size - 1) {
        if (*src == '\\' && src + 1 < end) {
            src++;
            switch (*src) {
                case 'n':  dst[written++] = '\n'; break;
                case 'r':  dst[written++] = '\r'; break;
                case 't':  dst[written++] = '\t'; break;
                case '\\': dst[written++] = '\\'; break;
                case '"':  dst[written++] = '"'; break;
                default:   dst[written++] = *src; break;
            }
            src++;
        } else {
            dst[written++] = *src++;
        }
    }
    dst[written] = '\0';
    return written;
}

/* JSON에서 quote로 감싸진 문자열 찾기 (공통 함수) */
const char* find_quoted_string(const char *start, const char **out_start, const char **out_end) {
    const char *quote_start = strchr(start, '"');
    if (!quote_start) return NULL;
    quote_start++;

    const char *quote_end = quote_start;
    while (*quote_end && *quote_end != '"') {
        if (*quote_end == '\\') quote_end++;
        quote_end++;
    }

    if (!*quote_end) return NULL;

    *out_start = quote_start;
    *out_end = quote_end;
    return quote_start;
}

char* extract_json_string_value(const char *json, const char *key) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *key_pos = strstr(json, search_pattern);
    if (!key_pos) return NULL;

    const char *colon = strchr(key_pos, ':');
    if (!colon) return NULL;

    const char *quote_start, *quote_end;
    if (!find_quoted_string(colon, &quote_start, &quote_end)) {
        return NULL;
    }

    size_t len = quote_end - quote_start;
    char *value = safe_malloc(len + 1);
    unescape_json_string(quote_start, quote_end, value, len + 1);

    return value;
}

char* extract_first_array_string(const char *json, const char *key) {
    char search_pattern[128];
    snprintf(search_pattern, sizeof(search_pattern), "\"%s\"", key);

    const char *key_pos = strstr(json, search_pattern);
    if (!key_pos) return NULL;

    const char *bracket = strchr(key_pos, '[');
    if (!bracket) return NULL;

    const char *quote_start, *quote_end;
    if (!find_quoted_string(bracket, &quote_start, &quote_end)) {
        return NULL;
    }

    size_t len = quote_end - quote_start;
    char *value = safe_malloc(len + 1);
    unescape_json_string(quote_start, quote_end, value, len + 1);

    return value;
}

int parse_queries_array(const char *json, char **queries, int max_count) {
    const char *queries_start = strstr(json, "\"queries\"");
    if (!queries_start) return 0;

    const char *bracket_start = strchr(queries_start, '[');
    if (!bracket_start) return 0;

    const char *bracket_end = strchr(bracket_start, ']');
    if (!bracket_end) return 0;

    int count = 0;
    const char *p = bracket_start + 1;

    while (p < bracket_end && count < max_count) {
        const char *quote_start, *quote_end;

        /* find_quoted_string 사용 */
        if (!find_quoted_string(p, &quote_start, &quote_end) || quote_end > bracket_end) {
            break;
        }

        size_t len = quote_end - quote_start;
        queries[count] = safe_malloc(len + 1);
        unescape_json_string(quote_start, quote_end, queries[count], len + 1);
        count++;

        p = quote_end + 1;
    }

    return count;
}

char* normalize_search_query(const char *query, char **queries, int queries_count) {
    if (queries && queries_count > 0 && queries[0] && strlen(queries[0]) > 0) {
        char *result = safe_strdup(queries[0]);
        return trim_string(result);
    }

    if (!query || strlen(query) == 0) {
        return safe_strdup("");
    }

    char *cleaned = remove_think_tags(query);
    cleaned = trim_string(cleaned);

    if (strchr(cleaned, '{') && strstr(cleaned, "queries")) {
        char *nested_query = extract_first_array_string(cleaned, "queries");
        if (nested_query && strlen(nested_query) > 0) {
            free(cleaned);
            return nested_query;
        }
    }

    if (cleaned[0] == '[') {
        const char *quote = strchr(cleaned, '"');
        if (quote) {
            quote++;
            const char *quote_end = strchr(quote, '"');
            if (quote_end) {
                size_t len = quote_end - quote;
                char *result = malloc(len + 1);
                strncpy(result, quote, len);
                result[len] = '\0';
                free(cleaned);
                return result;
            }
        }
    }

    if (cleaned[0] == '"') {
        const char *end = strchr(cleaned + 1, '"');
        if (end) {
            size_t len = end - (cleaned + 1);
            char *result = malloc(len + 1);
            strncpy(result, cleaned + 1, len);
            result[len] = '\0';
            free(cleaned);
            return result;
        }
    }

    if (!strchr(cleaned, '{') && !strchr(cleaned, '[') && !strchr(cleaned, ':')) {
        char *space = strchr(cleaned, ' ');
        if (space) {
            *space = '\0';
        }
    }

    if (strlen(cleaned) > MAX_QUERY_LEN) {
        cleaned[MAX_QUERY_LEN] = '\0';
    }

    return cleaned;
}

int parse_search_request(const char *body, SearchRequest *req) {
    memset(req, 0, sizeof(SearchRequest));

    req->query = extract_json_string_value(body, "query");
    req->queries_count = parse_queries_array(body, req->queries, MAX_QUERIES);

    const char *count_str = strstr(body, "\"count\"");
    if (count_str) {
        const char *colon = strchr(count_str, ':');
        if (colon) {
            req->count = atoi(colon + 1);
        }
    }

    if (req->count <= 0) {
        req->count = g_config.search_count;
    }

    return 0;
}

void free_search_request(SearchRequest *req) {
    if (req->query) free(req->query);

    for (int i = 0; i < req->queries_count; i++) {
        if (req->queries[i]) free(req->queries[i]);
    }
}

/* ============================
 * 파일 I/O 함수
 * ============================ */

char* load_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[Template] Warning: %s not found\n", filename);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fprintf(stderr, "[Template] Error: ftell failed for %s\n", filename);
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = safe_malloc(size + 1);
    size_t bytes_read = fread(content, 1, size, f);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "[Template] Warning: partial read of %s (%zu/%ld bytes)\n",
                filename, bytes_read, size);
    }
    content[bytes_read] = '\0';
    fclose(f);

    return content;
}

/* 템플릿 파일 로드 (캐싱 포함) */
char* load_template(const char *filename) {
    if (g_template_cache.loaded && g_template_cache.content) {
        return g_template_cache.content;
    }

    char *content = load_file(filename);
    if (content) {
        g_template_cache.content = content;
        g_template_cache.loaded = true;
    }

    return content;
}

char* replace_template_vars(const char *template, const char *index_name,
                            const char *query, int count) {
    char *result = safe_malloc(BUFFER_SIZE);
    char *p = result;
    const char *s = template;
    size_t remaining = BUFFER_SIZE - 1;

    while (*s && remaining > 0) {
        if (strncmp(s, "{INDEX_NAME}", 12) == 0) {
            size_t len = strlen(index_name);
            if (len > remaining) len = remaining;
            memcpy(p, index_name, len);
            p += len;
            remaining -= len;
            s += 12;
        } else if (strncmp(s, "{SEARCH_QUERY}", 14) == 0) {
            size_t len = strlen(query);
            if (len > remaining) len = remaining;
            memcpy(p, query, len);
            p += len;
            remaining -= len;
            s += 14;
        } else if (strncmp(s, "{RESULT_LIMIT}", 14) == 0) {
            int written = snprintf(p, remaining, "%d", count);
            if (written > 0 && (size_t)written < remaining) {
                p += written;
                remaining -= written;
            }
            s += 14;
        } else {
            *p++ = *s++;
            remaining--;
        }
    }
    *p = '\0';

    return result;
}

/* ============================
 * HTTP 클라이언트 함수
 * ============================ */

char* http_post_request(const char *host, int port, const char *path, const char *body) {
    int sock;
    struct sockaddr_in server_addr;
    struct hostent *server;

    /* 소켓 생성 */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        return NULL;
    }

    /* 호스트 찾기 */
    server = gethostbyname(host);
    if (!server) {
        fprintf(stderr, "Error: No such host %s\n", host);
        close(sock);
        return NULL;
    }

    /* 서버 주소 설정 */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    server_addr.sin_port = htons(port);

    /* 연결 */
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("connection failed");
        close(sock);
        return NULL;
    }

    /* HTTP 요청 생성 */
    size_t body_len = strlen(body);
    size_t request_size = 256 + strlen(path) + strlen(host) + body_len;
    char *request = safe_malloc(request_size);
    snprintf(request, request_size,
             "POST %s HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             path, host, port, body_len, body);

    /* 요청 전송 */
    ssize_t written = write(sock, request, strlen(request));
    free(request);
    if (written < 0) {
        perror("write failed");
        close(sock);
        return NULL;
    }

    /* 응답 수신 */
    char *response = safe_malloc(BUFFER_SIZE);
    char buffer[4096];
    int total_read = 0;
    ssize_t bytes_read;

    while ((bytes_read = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
        if (total_read + bytes_read >= BUFFER_SIZE - 1) break;
        memcpy(response + total_read, buffer, bytes_read);
        total_read += bytes_read;
    }
    response[total_read] = '\0';

    close(sock);

    /* HTTP 바디 추출 */
    char *body_start = strstr(response, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        char *body_copy = safe_strdup(body_start);
        free(response);
        return body_copy;
    }

    return response;
}

/* ============================
 * Manticore Search 통합
 * ============================ */

/* Manticore 응답 파싱 */
int parse_manticore_response(const char *response, int max_results, SearchResult *results) {
    int result_count = 0;

    /* hits.hits 배열 찾기 */
    const char *hits = strstr(response, "\"hits\"");
    if (!hits) return 0;

    /* 두 번째 "hits" 찾기 (중첩 구조) */
    hits = strstr(hits + 6, "\"hits\"");
    if (!hits) return 0;

    /* [ 찾기 */
    const char *array_start = strchr(hits, '[');
    if (!array_start) return 0;

    /* 각 결과 파싱 - hits 배열의 각 요소를 순회 */
    const char *search_pos = array_start;

    while (result_count < max_results && result_count < MAX_RESULTS) {
        /* 다음 _source 찾기 */
        const char *source = strstr(search_pos, "\"_source\"");
        if (!source) break;

        /* _source 이후로 검색 위치 이동 (다음 반복을 위해) */
        search_pos = source + 9;

        /* page_title 추출 */
        const char *title_key = strstr(source, "\"page_title\"");
        char *title = NULL;
        if (title_key) {
            title = extract_json_string_value(title_key, "page_title");
        }
        if (!title) {
            title = safe_strdup("Unknown Document");
        }

        /* link 생성 - URL 인코딩 적용 */
        char *encoded_title = url_encode(title);
        size_t link_size = strlen(g_config.base_url) + strlen(encoded_title) + 1;
        char *link = safe_malloc(link_size);
        snprintf(link, link_size, "%s%s", g_config.base_url, encoded_title);
        free(encoded_title);

        /* old_text에서 snippet 추출 */
        const char *content_key = strstr(source, "\"old_text\"");
        char *snippet = NULL;
        if (content_key) {
            snippet = extract_json_string_value(content_key, "old_text");

            if (snippet) {
                /* 최대 snippet_length 제한 (UTF-8 안전) */
                if (strlen(snippet) > (size_t)g_config.snippet_length) {
                    size_t safe_len = utf8_safe_truncate(snippet, g_config.snippet_length);
                    snippet[safe_len] = '\0';
                    snippet = safe_realloc(snippet, safe_len + 4);
                    strcat(snippet, "...");
                }
            }
        }
        if (!snippet) {
            snippet = safe_strdup("No content available");
        }

        /* 결과 저장 */
        results[result_count].link = link;
        results[result_count].title = title;
        results[result_count].snippet = snippet;
        result_count++;
    }

    return result_count;
}

int search_manticore(const char *query, int count, SearchResult *results) {
    /* 템플릿 로드 (캐시 사용) */
    char *template = load_template("rule_manticore.txt");
    if (!template) {
        return 0;
    }

    /* 템플릿 변수 치환 */
    char *request_body = replace_template_vars(template, g_config.index_name, query, count);

    printf("[Manticore] Request: %s\n", request_body);

#ifdef DEBUG
    /* 디버그 로그: 검색 요청 */
    char log_msg[2048];
    snprintf(log_msg, sizeof(log_msg), "SEARCH_REQUEST | query=\"%s\" | count=%d | index=%s",
             query, count, g_config.index_name);
    write_debug_log("REQUEST", log_msg);

    snprintf(log_msg, sizeof(log_msg), "MANTICORE_QUERY | %s", request_body);
    write_debug_log("REQUEST", log_msg);
#endif

    /* Manticore Search API 호출 */
    char *response = http_post_request(g_config.manticore_host, g_config.manticore_port,
                                      g_config.manticore_path, request_body);
    free(request_body);

    if (!response) {
        printf("[Manticore] Error: No response\n");
#ifdef DEBUG
        write_debug_log("ERROR", "MANTICORE_NO_RESPONSE");
#endif
        return 0;
    }

    printf("[Manticore] Response: %s\n", response);

#ifdef DEBUG
    /* 디버그 로그: 검색 응답 (요약) */
    int response_len = strlen(response);
    if (response_len > 500) {
        char truncated[550];
        strncpy(truncated, response, 500);
        truncated[500] = '\0';
        snprintf(log_msg, sizeof(log_msg), "MANTICORE_RESPONSE | length=%d | data=%s...",
                 response_len, truncated);
    } else {
        snprintf(log_msg, sizeof(log_msg), "MANTICORE_RESPONSE | length=%d | data=%s",
                 response_len, response);
    }
    write_debug_log("RESPONSE", log_msg);
#endif

    /* 응답 파싱 */
    int result_count = parse_manticore_response(response, count, results);
    free(response);
    printf("[Manticore] Found %d results\n", result_count);

#ifdef DEBUG
    /* 디버그 로그: 검색 결과 요약 */
    snprintf(log_msg, sizeof(log_msg), "SEARCH_RESULT | found=%d results", result_count);
    write_debug_log("RESULT", log_msg);

    for (int i = 0; i < result_count; i++) {
        snprintf(log_msg, sizeof(log_msg), "RESULT_%d | title=\"%s\" | link=\"%s\"",
                 i + 1, results[i].title, results[i].link);
        write_debug_log("RESULT", log_msg);
    }
#endif

    return result_count;
}

void free_search_results(SearchResult *results, int count) {
    for (int i = 0; i < count; i++) {
        if (results[i].link) free(results[i].link);
        if (results[i].title) free(results[i].title);
        if (results[i].snippet) free(results[i].snippet);
    }
}

/* ============================
 * JSON 응답 생성
 * ============================ */

/* JSON 문자열 이스케이프 함수 */
char* json_escape_string(const char *str) {
    if (!str) return safe_strdup("");

    size_t len = strlen(str);
    char *escaped = safe_malloc(len * 2 + 1);  /* 최악의 경우 2배 크기 */
    char *dst = escaped;

    for (const char *src = str; *src; src++) {
        switch (*src) {
            case '\n': *dst++ = ' '; break;  /* 개행 → 공백 */
            case '\r': *dst++ = ' '; break;  /* 캐리지 리턴 → 공백 */
            case '\t': *dst++ = ' '; break;  /* 탭 → 공백 */
            case '"':  *dst++ = '\\'; *dst++ = '"'; break;   /* " → \" */
            case '\\': *dst++ = '\\'; *dst++ = '\\'; break;  /* \ → \\ */
            default:   *dst++ = *src; break;
        }
    }
    *dst = '\0';
    return escaped;
}

char* create_json_response(SearchResult *results, int count, int took_ms) {
    char *response = safe_malloc(BUFFER_SIZE);
    size_t offset = 0;
    size_t remaining = BUFFER_SIZE;

    int written = snprintf(response + offset, remaining, "{\n  \"results\": [\n");
    if (written > 0 && (size_t)written < remaining) {
        offset += written;
        remaining -= written;
    }

    for (int i = 0; i < count; i++) {
        char *escaped_link = json_escape_string(results[i].link);
        char *escaped_title = json_escape_string(results[i].title);
        char *escaped_snippet = json_escape_string(results[i].snippet);

        written = snprintf(response + offset, remaining,
                          "    {\n"
                          "      \"link\": \"%s\",\n"
                          "      \"title\": \"%s\",\n"
                          "      \"snippet\": \"%s\"\n"
                          "    }%s\n",
                          escaped_link, escaped_title, escaped_snippet,
                          (i < count - 1) ? "," : "");

        free(escaped_link);
        free(escaped_title);
        free(escaped_snippet);

        if (written > 0 && (size_t)written < remaining) {
            offset += written;
            remaining -= written;
        } else {
            break;  /* 버퍼 초과 */
        }
    }

    written = snprintf(response + offset, remaining,
                      "  ],\n"
                      "  \"took_ms\": %d,\n"
                      "  \"total\": %d,\n"
                      "  \"engine\": \"manticore\"\n"
                      "}",
                      took_ms, count);

    return response;
}

/* ============================
 * HTTP 서버 함수
 * ============================ */

void send_http_response(int client_fd, int status_code, const char *status_text,
                       const char *content_type, const char *body) {
    char header[2048];

    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, status_text, content_type, strlen(body));

    ssize_t header_written = write(client_fd, header, strlen(header));
    if (header_written < 0) {
        perror("write header failed");
        return;
    }

    ssize_t body_written = write(client_fd, body, strlen(body));
    if (body_written < 0) {
        perror("write body failed");
    }
}

void handle_search_request(int client_fd, const char *body) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    SearchRequest req;
    parse_search_request(body, &req);

    char *clean_query = normalize_search_query(req.query, req.queries, req.queries_count);

    printf("[Search] Query: \"%s\" | Count: %d | Engine: manticore\n", clean_query, req.count);

    SearchResult results[MAX_RESULTS];
    int result_count = 0;

    /* 빈 쿼리 처리 */
    if (strlen(clean_query) == 0) {
        printf("[Search] Warning: Empty query after normalization\n");
    } else {
        /* Manticore Search 호출 */
        result_count = search_manticore(clean_query, req.count, results);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    int took_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_nsec - start.tv_nsec) / 1000000;

    char *json_response = create_json_response(results, result_count, took_ms);
    send_http_response(client_fd, 200, "OK", "application/json", json_response);

    free(clean_query);
    free(json_response);
    free_search_request(&req);
    free_search_results(results, result_count);
}

void handle_root_request(int client_fd) {
    const char *body = "{\"status\": \"running\", \"service\": \"LocalKnowledgeBase\", \"version\": \"1.0\"}";
    send_http_response(client_fd, 200, "OK", "application/json", body);
}

void handle_not_found(int client_fd) {
    const char *body = "{\"error\": \"Not Found\"}";
    send_http_response(client_fd, 404, "Not Found", "application/json", body);
}

void handle_client(int client_fd) {
    char *buffer = safe_malloc(BUFFER_SIZE);
    ssize_t bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);

    if (bytes_read <= 0) {
        free(buffer);
        return;
    }

    buffer[bytes_read] = '\0';

    char method[16], path[256];
    int parsed = sscanf(buffer, "%15s %255s", method, path);
    if (parsed != 2) {
        fprintf(stderr, "[HTTP] Invalid request format\n");
        free(buffer);
        handle_not_found(client_fd);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/search") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (body) {
            body += 4;
            handle_search_request(client_fd, body);
        } else {
            handle_not_found(client_fd);
        }
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        handle_root_request(client_fd);
    } else {
        handle_not_found(client_fd);
    }

    free(buffer);
}

int main() {
    int client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    /* 시그널 핸들러 설정 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    atexit(cleanup_resources);

    /* 설정 파일 로드 */
    if (!load_config("config.yaml", &g_config)) {
        /* 기본값으로 초기화 */
        safe_strncpy(g_config.listen, "0.0.0.0", sizeof(g_config.listen));
        g_config.port = DEFAULT_PORT;
        safe_strncpy(g_config.manticore_host, "127.0.0.1", sizeof(g_config.manticore_host));
        g_config.manticore_port = 29308;
        safe_strncpy(g_config.manticore_path, "/search", sizeof(g_config.manticore_path));
        safe_strncpy(g_config.index_name, "wiki_main", sizeof(g_config.index_name));
        safe_strncpy(g_config.base_url, "http://localhost/mediawiki/index.php/", sizeof(g_config.base_url));
        g_config.search_count = DEFAULT_SEARCH_COUNT;
        g_config.snippet_length = MAX_SNIPPET_LEN;
    }

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;

    /* listen 주소 적용 */
    if (strcmp(g_config.listen, "0.0.0.0") == 0 || strcmp(g_config.listen, "*") == 0) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, g_config.listen, &server_addr.sin_addr) <= 0) {
            fprintf(stderr, "[Server] Invalid listen address: %s, using 0.0.0.0\n", g_config.listen);
            server_addr.sin_addr.s_addr = INADDR_ANY;
        }
    }

    server_addr.sin_port = htons(g_config.port);

    if (bind(g_server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        return 1;
    }

    if (listen(g_server_fd, 10) < 0) {
        perror("listen failed");
        return 1;
    }

    printf("LocalKnowledgeBase C Server\n");
    printf("✓ Server running on http://%s:%d\n", g_config.listen, g_config.port);
    printf("✓ Manticore Search integration enabled\n");
    printf("  - Host: %s:%d\n", g_config.manticore_host, g_config.manticore_port);
    printf("  - Index: %s\n", g_config.index_name);
    printf("  - Base URL: %s\n", g_config.base_url);
    printf("  - Default search count: %d\n", g_config.search_count);
    printf("  - Snippet length: %d\n", g_config.snippet_length);
    printf("\nPress Ctrl+C to stop\n\n");

    while (g_running) {
        client_fd = accept(g_server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (g_running) {
                perror("accept failed");
            }
            continue;
        }

        handle_client(client_fd);
        close(client_fd);
    }

    printf("[Server] Shutting down gracefully...\n");
    return 0;
}
