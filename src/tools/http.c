#include "qa/http.h"
#include <curl/curl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct http_transfer {
    struct http_transfer *next;
    qa_http *owner;
    qa_http_request_id id;
    CURL *easy;
    struct curl_slist *headers;
    char *url, *method, *content_type;
    qa_buffer body;
    qa_http_callbacks callbacks;
    qa_http_response response;
    qa_error failure;
    uint64_t maximum_bytes;
    bool canceled;
} http_transfer;
struct qa_http { CURLM *multi; http_transfer *transfers; uint64_t next_id; bool pumping; };
bool qa_http_callbacks_idle(const qa_http *owner) { return !owner || !owner->pumping; }
static bool fail(qa_error *error, qa_status status, const char *text) {
    qa_error_set(error, status, 0, "%s", text); return false;
}
static char *copy_text(const char *text) {
    size_t n = strlen(text); if (n == SIZE_MAX) return NULL;
    char *copy = malloc(n + 1); if (copy) memcpy(copy, text, n + 1); return copy;
}
static void transfer_free(http_transfer *t) {
    if (t->easy) curl_easy_cleanup(t->easy);
    curl_slist_free_all(t->headers); free(t->url); free(t->method); free(t->content_type);
    qa_buffer_free(&t->body); free(t);
}
static void remove_transfer(qa_http *owner, http_transfer **link) {
    http_transfer *t = *link; *link = t->next;
    (void)curl_multi_remove_handle(owner->multi, t->easy); transfer_free(t);
}
bool qa_http_create(qa_http **out, qa_error *error) {
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "missing HTTP owner output");
    /* libcurl global initialization is reference counted internally. Each
     * owner balances its own initialization through teardown. */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return fail(error, QA_ERROR_IO, "initializing native HTTP transport");
    qa_http *owner = calloc(1, sizeof *owner);
    if (!owner) { curl_global_cleanup(); return fail(error, QA_ERROR_MEMORY, "allocating HTTP owner"); }
    owner->multi = curl_multi_init(); owner->next_id = 1;
    if (!owner->multi) { free(owner); curl_global_cleanup(); return fail(error, QA_ERROR_MEMORY, "allocating HTTP transfer queue"); }
    *out = owner; return true;
}
bool qa_http_destroy(qa_http *owner, qa_error *error) {
    if (!owner) return true;
    if (owner->pumping) return fail(error, QA_ERROR_ARGUMENT, "HTTP teardown requires callbacks to return");
    while (owner->transfers) remove_transfer(owner, &owner->transfers);
    curl_multi_cleanup(owner->multi); free(owner); curl_global_cleanup(); return true;
}
static bool header_name(const char *name) {
    if (!name || !*name) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("!#$%&'*+-.^_`|~", *p))) return false;
    return true;
}
static bool safe_value(const char *value) {
    if (!value) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) if (*p < 32 || *p == 127) return false;
    return true;
}
static bool prefix_equal(const char *text, const char *prefix, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        unsigned c = (unsigned char)text[i]; if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != (unsigned char)prefix[i]) return false;
    }
    return true;
}
static bool range_number(const char *text, size_t n, size_t *cursor, uint64_t *out) {
    size_t start = *cursor; uint64_t value = 0;
    while (*cursor < n && text[*cursor] >= '0' && text[*cursor] <= '9') {
        unsigned digit = (unsigned)(text[(*cursor)++] - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (*cursor == start) return false;
    *out = value; return true;
}
static bool content_range(http_transfer *t, const char *bytes, size_t n) {
    size_t cursor = 14;
    while (cursor < n && (bytes[cursor] == ' ' || bytes[cursor] == '\t')) ++cursor;
    if (n - cursor < 6 || !prefix_equal(bytes + cursor, "bytes ", 6)) return false;
    cursor += 6;
    uint64_t first, last, total;
    if (!range_number(bytes, n, &cursor, &first) || cursor >= n || bytes[cursor++] != '-' ||
        !range_number(bytes, n, &cursor, &last) || cursor >= n || bytes[cursor++] != '/' ||
        !range_number(bytes, n, &cursor, &total) || first > last || last >= total) return false;
    while (cursor < n && (bytes[cursor] == ' ' || bytes[cursor] == '\t' || bytes[cursor] == '\r' || bytes[cursor] == '\n')) ++cursor;
    if (cursor != n || t->response.has_range) return false;
    t->response.range_first = first; t->response.range_last = last; t->response.range_total = total; t->response.has_range = true; return true;
}
static size_t header_data(char *bytes, size_t size, size_t count, void *context) {
    http_transfer *t = context;
    if (size && count > SIZE_MAX / size) { fail(&t->failure, QA_ERROR_MEMORY, "HTTP header size overflow"); return 0; }
    size_t n = size * count;
    if (t->canceled) return 0;
    if (n >= 5 && !memcmp(bytes, "HTTP/", 5)) {
        free(t->content_type); t->content_type = NULL;
        t->response.content_type = NULL; t->response.status = 0;
        t->response.has_range = false;
        t->response.range_first = t->response.range_last = t->response.range_total = 0;
        const char *space = memchr(bytes, ' ', n);
        if (space && bytes + n - space >= 4 && space[1] >= '0' && space[1] <= '9' &&
            space[2] >= '0' && space[2] <= '9' && space[3] >= '0' && space[3] <= '9')
            t->response.status = (unsigned)(space[1] - '0') * 100 + (unsigned)(space[2] - '0') * 10 + (unsigned)(space[3] - '0');
    } else if (n >= 14 && prefix_equal(bytes, "content-range:", 14)) {
        if (!content_range(t, bytes, n)) { fail(&t->failure, QA_ERROR_FORMAT, "HTTP response returned an invalid content range"); return 0; }
    } else if (n >= 13 && prefix_equal(bytes, "content-type:", 13)) {
        size_t begin = 13, end = n;
        while (begin < end && (bytes[begin] == ' ' || bytes[begin] == '\t')) ++begin;
        while (end > begin && (bytes[end - 1] == '\r' || bytes[end - 1] == '\n' || bytes[end - 1] == ' ' || bytes[end - 1] == '\t')) --end;
        char *value = malloc(end - begin + 1);
        if (!value) { fail(&t->failure, QA_ERROR_MEMORY, "allocating HTTP response metadata"); return 0; }
        memcpy(value, bytes + begin, end - begin); value[end - begin] = 0;
        free(t->content_type); t->content_type = value; t->response.content_type = value;
    } else if ((n == 2 && bytes[0] == '\r' && bytes[1] == '\n') || (n == 1 && bytes[0] == '\n')) {
        if (t->callbacks.headers && !t->callbacks.headers(t->callbacks.context, t->id, &t->response, &t->failure)) {
            if (t->failure.code == QA_OK) fail(&t->failure, QA_ERROR_IO, "HTTP response rejected by consumer");
            return 0;
        }
    }
    return t->canceled ? 0 : n;
}
static size_t body_data(char *bytes, size_t size, size_t count, void *context) {
    http_transfer *t = context;
    if (size && count > SIZE_MAX / size) { fail(&t->failure, QA_ERROR_MEMORY, "HTTP body size overflow"); return 0; }
    size_t n = size * count;
    if (t->canceled) return 0;
    if ((uint64_t)n > UINT64_MAX - t->response.bytes ||
        (t->maximum_bytes && ((uint64_t)n > t->maximum_bytes || t->response.bytes > t->maximum_bytes - (uint64_t)n))) {
        fail(&t->failure, QA_ERROR_FORMAT, "HTTP response exceeds admitted byte limit"); return 0;
    }
    t->response.bytes += (uint64_t)n;
    if (t->callbacks.body && !t->callbacks.body(t->callbacks.context, t->id, &t->response, (qa_bytes){(const uint8_t *)bytes, n}, &t->failure)) {
        if (t->failure.code == QA_OK) fail(&t->failure, QA_ERROR_IO, "HTTP response rejected by consumer");
        return 0;
    }
    return t->canceled ? 0 : n;
}
bool qa_http_submit(qa_http *owner, const qa_http_request *r, qa_http_request_id *out, qa_error *error) {
    if (!owner || !r || !out || owner->pumping || !r->url || !header_name(r->method) ||
        (r->header_count && !r->headers) || (r->body.size && !r->body.data) || r->body.size > (size_t)PTRDIFF_MAX ||
        !owner->next_id || !r->timeout_ms || r->timeout_ms > INT32_MAX ||
        r->connect_timeout_ms > INT32_MAX || r->maximum_redirects > INT32_MAX)
        return fail(error, QA_ERROR_ARGUMENT, "invalid HTTP request admission");
    CURLU *url = curl_url(); char *scheme = NULL, *user = NULL, *password = NULL;
    if (!url) return fail(error, QA_ERROR_MEMORY, "allocating HTTP address parser");
    bool valid = curl_url_set(url, CURLUPART_URL, r->url, 0) == CURLUE_OK &&
        curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK && (!strcmp(scheme, "https") || !strcmp(scheme, "http"));
    if (curl_url_get(url, CURLUPART_USER, &user, 0) == CURLUE_OK || curl_url_get(url, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK) valid = false;
    curl_free(scheme); curl_free(user); curl_free(password); curl_url_cleanup(url);
    if (!valid) return fail(error, QA_ERROR_ARGUMENT, "HTTP address must use HTTP(S) without URL credentials");
    http_transfer *t = calloc(1, sizeof *t);
    if (!t) return fail(error, QA_ERROR_MEMORY, "allocating HTTP transfer");
    t->owner = owner; t->id = owner->next_id; t->callbacks = r->callbacks; t->maximum_bytes = r->maximum_response_bytes;
    t->easy = curl_easy_init(); t->url = copy_text(r->url); t->method = copy_text(r->method);
    if (!t->easy || !t->url || !t->method) goto memory;
    if (r->body.size) {
        t->body.data = malloc(r->body.size);
        if (!t->body.data) goto memory;
        memcpy(t->body.data, r->body.data, r->body.size); t->body.size = r->body.size;
    }
    for (size_t i = 0; i < r->header_count; ++i) {
        const qa_http_header *h = &r->headers[i];
        if (!header_name(h->name) || !safe_value(h->value)) { fail(error, QA_ERROR_ARGUMENT, "invalid HTTP header"); goto failed; }
        size_t a = strlen(h->name), b = strlen(h->value);
        if (a > SIZE_MAX - b - 3) goto memory;
        char *line = malloc(a + b + 3); if (!line) goto memory;
        memcpy(line, h->name, a); line[a] = ':'; line[a + 1] = ' '; memcpy(line + a + 2, h->value, b + 1);
        struct curl_slist *headers = curl_slist_append(t->headers, line); free(line);
        if (!headers) goto memory;
        t->headers = headers;
    }
#define OPTION(name, value) do { if (curl_easy_setopt(t->easy, name, value) != CURLE_OK) { fail(error, QA_ERROR_IO, "configuring HTTP transfer"); goto failed; } } while (0)
    OPTION(CURLOPT_URL, t->url); OPTION(CURLOPT_CUSTOMREQUEST, t->method);
    OPTION(CURLOPT_PROTOCOLS_STR, "http,https"); OPTION(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    OPTION(CURLOPT_SSL_VERIFYPEER, 1L); OPTION(CURLOPT_SSL_VERIFYHOST, 2L);
    OPTION(CURLOPT_NOSIGNAL, 1L); OPTION(CURLOPT_TIMEOUT_MS, (long)r->timeout_ms);
    OPTION(CURLOPT_CONNECTTIMEOUT_MS, (long)(r->connect_timeout_ms ? r->connect_timeout_ms : r->timeout_ms));
    OPTION(CURLOPT_FOLLOWLOCATION, r->maximum_redirects ? 1L : 0L); OPTION(CURLOPT_MAXREDIRS, (long)r->maximum_redirects);
    OPTION(CURLOPT_UNRESTRICTED_AUTH, 0L); OPTION(CURLOPT_HTTPHEADER, t->headers);
    OPTION(CURLOPT_ACCEPT_ENCODING, "");
    OPTION(CURLOPT_HEADERFUNCTION, header_data); OPTION(CURLOPT_HEADERDATA, t);
    OPTION(CURLOPT_WRITEFUNCTION, body_data); OPTION(CURLOPT_WRITEDATA, t); OPTION(CURLOPT_PRIVATE, t);
    if (r->body.size || !strcmp(r->method, "POST") || !strcmp(r->method, "PUT") || !strcmp(r->method, "PATCH")) {
        OPTION(CURLOPT_POSTFIELDS, t->body.data ? (char *)t->body.data : "");
        OPTION(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)t->body.size);
    }
#undef OPTION
    if (curl_multi_add_handle(owner->multi, t->easy) != CURLM_OK) { fail(error, QA_ERROR_IO, "admitting HTTP transfer"); goto failed; }
    t->next = owner->transfers; owner->transfers = t; ++owner->next_id; *out = t->id; return true;
memory:
    fail(error, QA_ERROR_MEMORY, "allocating HTTP request data");
failed:
    transfer_free(t); return false;
}
void qa_http_cancel(qa_http *owner, qa_http_request_id id) {
    if (!owner || !id) return;
    for (http_transfer **link = &owner->transfers; *link; link = &(*link)->next) if ((*link)->id == id) {
        (*link)->canceled = true;
        if (!owner->pumping) remove_transfer(owner, link);
        return;
    }
}
bool qa_http_pump(qa_http *owner, qa_error *error) {
    if (!owner || owner->pumping) return fail(error, QA_ERROR_ARGUMENT, "HTTP progress cannot reenter callbacks");
    owner->pumping = true; int running = 0;
    CURLMcode result = curl_multi_perform(owner->multi, &running);
    int queued = 0; CURLMsg *message;
    while ((message = curl_multi_info_read(owner->multi, &queued)) != NULL) {
        if (message->msg != CURLMSG_DONE) continue;
        http_transfer *t = NULL;
        (void)curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &t);
        if (!t) continue;
        http_transfer **link = &owner->transfers;
        while (*link && *link != t) link = &(*link)->next;
        if (!*link) continue;
        CURLcode completed_result = message->data.result;
        *link = t->next; (void)curl_multi_remove_handle(owner->multi, t->easy);
        if (!t->canceled && t->callbacks.complete) {
            qa_error failure = t->failure;
            if (failure.code == QA_OK && completed_result != CURLE_OK)
                qa_error_set(&failure, QA_ERROR_IO, 0, "HTTP transport failed (%u)", (unsigned)completed_result);
            t->callbacks.complete(t->callbacks.context, t->id, &t->response, failure.code == QA_OK ? NULL : &failure);
        }
        transfer_free(t);
    }
    for (http_transfer **link = &owner->transfers; *link;) {
        if ((*link)->canceled) remove_transfer(owner, link); else link = &(*link)->next;
    }
    owner->pumping = false;
    return result == CURLM_OK || fail(error, QA_ERROR_IO, "HTTP transfer queue failed");
}
size_t qa_http_pending(const qa_http *owner) {
    size_t count = 0; if (owner) for (const http_transfer *t = owner->transfers; t; t = t->next) if (!t->canceled) ++count;
    return count;
}
bool qa_http_next_timeout(const qa_http *owner, uint32_t *milliseconds, bool *pending, qa_error *error) {
    if (!owner || !milliseconds || !pending) return fail(error, QA_ERROR_ARGUMENT, "invalid HTTP wakeup query");
    long timeout = -1;
    if (curl_multi_timeout(owner->multi, &timeout) != CURLM_OK) return fail(error, QA_ERROR_IO, "HTTP wakeup query failed");
    *pending = qa_http_pending(owner) != 0;
    *milliseconds = timeout < 0 ? 1000 : (uint32_t)(timeout > UINT32_MAX ? UINT32_MAX : timeout);
    return true;
}
