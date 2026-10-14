#include "qa/http.h"
#include "qa/http_save.h"
#include "save_internal.h"
#include <curl/curl.h>
#include <limits.h>
#include <stdatomic.h>
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
    qa_error failure, transport_failure;
    uint64_t maximum_bytes, response_body_start, admitted_bytes, terminal_order;
    CURLcode result;
    uint32_t timeout_ms, connect_timeout_ms, maximum_redirects, intake_status;
    bool same_origin_redirects;
    bool canceled, callbacks_bound, in_multi, paused, terminal, completion_queued, rejected;
} http_transfer;
struct qa_http {
    CURLM *multi;
    http_transfer *transfers;
    qa_platform_events *events;
    uint64_t next_id, source_id, time_ns, terminal_sequence;
    bool pumping, pending_restore, detached, owns_events;
};
static atomic_uint_fast64_t next_source_id = 1;
bool qa_http_callbacks_idle(const qa_http *owner) { return !owner || !owner->pumping; }
static bool fail(qa_error *error, qa_status status, const char *text) {
    qa_error_set(error, status, 0, "%s", text); return false;
}
bool qa_http_checkpoint_ready(const qa_http *owner, qa_error *error) {
    return (owner && !owner->pumping && !owner->pending_restore) ||
        fail(error, QA_ERROR_ARGUMENT, "HTTP continuation requires returned callbacks and restored records");
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
    if (t->in_multi) (void)curl_multi_remove_handle(owner->multi, t->easy);
    transfer_free(t);
}
static uint64_t source_id_take(qa_error *error) {
    uint_fast64_t id = atomic_load_explicit(&next_source_id, memory_order_relaxed);
    while (id != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_source_id, &id, id + 1,
            memory_order_relaxed, memory_order_relaxed)) return (uint64_t)id;
    }
    fail(error, QA_ERROR_CAPACITY, "HTTP source IDs exhausted"); return 0;
}
bool qa_http_create(qa_platform_events *events, qa_http **out, qa_error *error) {
    if (!out) return fail(error, QA_ERROR_ARGUMENT, "missing HTTP owner output");
    /* libcurl global initialization is reference counted internally. Each
     * owner balances its own initialization through teardown. */
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return fail(error, QA_ERROR_IO, "initializing native HTTP transport");
    qa_http *owner = calloc(1, sizeof *owner);
    if (!owner) { curl_global_cleanup(); return fail(error, QA_ERROR_MEMORY, "allocating HTTP owner"); }
    owner->multi = curl_multi_init(); owner->next_id = 1;
    if (!owner->multi) { free(owner); curl_global_cleanup(); return fail(error, QA_ERROR_MEMORY, "allocating HTTP transfer queue"); }
    owner->events = events; owner->owns_events = !events;
    if (!events) owner->events = qa_platform_events_create(error);
    if (!owner->events || !(owner->source_id = source_id_take(error))) {
        if (owner->owns_events) qa_platform_events_destroy(owner->events);
        curl_multi_cleanup(owner->multi); free(owner); curl_global_cleanup(); return false;
    }
    *out = owner; return true;
}
bool qa_http_create_empty(qa_platform_events *events, qa_http **out, qa_error *error) {
    if (!out || *out) return fail(error, QA_ERROR_ARGUMENT, "empty HTTP restoration requires an empty output");
    if (!qa_http_create(events, out, error)) return false;
    (*out)->pending_restore = true; return true;
}
bool qa_http_destroy(qa_http *owner, qa_error *error) {
    if (!owner) return true;
    if (owner->pumping) return fail(error, QA_ERROR_ARGUMENT, "HTTP teardown requires callbacks to return");
    while (owner->transfers) remove_transfer(owner, &owner->transfers);
    if (owner->owns_events) qa_platform_events_destroy(owner->events);
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
static unsigned status_code(const char *bytes, size_t n) {
    const char *space = memchr(bytes, ' ', n);
    if (space && bytes + n - space >= 4 && space[1] >= '0' && space[1] <= '9' &&
        space[2] >= '0' && space[2] <= '9' && space[3] >= '0' && space[3] <= '9')
        return (unsigned)(space[1] - '0') * 100 + (unsigned)(space[2] - '0') * 10 + (unsigned)(space[3] - '0');
    return 0;
}
static bool redirect_origin(http_transfer *t, const char *bytes, size_t n) {
    size_t begin = 9, end = n;
    while (begin < end && (bytes[begin] == ' ' || bytes[begin] == '\t')) ++begin;
    while (end > begin && (bytes[end-1] == '\r' || bytes[end-1] == '\n' || bytes[end-1] == ' ' || bytes[end-1] == '\t')) --end;
    char *location = malloc(end - begin + 1);
    CURLU *origin = curl_url(), *target = curl_url();
    char *effective = NULL, *left[3] = {0}, *right[3] = {0}, *user = NULL, *password = NULL;
    bool okay = location && origin && target && !memchr(bytes+begin, 0, end-begin);
    if (okay) {
        memcpy(location, bytes+begin, end-begin); location[end-begin] = 0;
        okay = curl_easy_getinfo(t->easy, CURLINFO_EFFECTIVE_URL, &effective) == CURLE_OK && effective &&
            curl_url_set(origin, CURLUPART_URL, t->url, 0) == CURLUE_OK &&
            curl_url_set(target, CURLUPART_URL, effective, 0) == CURLUE_OK &&
            curl_url_set(target, CURLUPART_URL, location, 0) == CURLUE_OK;
    }
    const CURLUPart parts[] = {CURLUPART_SCHEME, CURLUPART_HOST, CURLUPART_PORT};
    for (unsigned i=0; okay && i<3; ++i) {
        unsigned flags = i==2 ? CURLU_DEFAULT_PORT : 0;
        okay = curl_url_get(origin, parts[i], &left[i], flags) == CURLUE_OK &&
            curl_url_get(target, parts[i], &right[i], flags) == CURLUE_OK &&
            curl_strequal(left[i], right[i]);
    }
    if (okay && (curl_url_get(target, CURLUPART_USER, &user, 0) == CURLUE_OK ||
        curl_url_get(target, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK)) okay = false;
    for (unsigned i=0; i<3; ++i) { curl_free(left[i]); curl_free(right[i]); }
    curl_free(user); curl_free(password); curl_url_cleanup(origin); curl_url_cleanup(target); free(location);
    return okay || fail(&t->transport_failure, QA_ERROR_FORMAT, "HTTP redirect leaves the requested origin");
}
static bool header_receive(http_transfer *t, qa_bytes data) {
    const char *bytes = (const char *)data.data; size_t n = data.size;
    if (n >= 5 && !memcmp(bytes, "HTTP/", 5)) {
        t->response_body_start = t->response.bytes;
        free(t->content_type); t->content_type = NULL;
        t->response.content_type = NULL; t->response.status = 0;
        t->response.has_range = false;
        t->response.range_first = t->response.range_last = t->response.range_total = 0;
        t->response.status = status_code(bytes, n);
    } else if (n >= 14 && prefix_equal(bytes, "content-range:", 14)) {
        if (!content_range(t, bytes, n)) return fail(&t->failure, QA_ERROR_FORMAT, "HTTP response returned an invalid content range");
    } else if (n >= 13 && prefix_equal(bytes, "content-type:", 13)) {
        size_t begin = 13, end = n;
        while (begin < end && (bytes[begin] == ' ' || bytes[begin] == '\t')) ++begin;
        while (end > begin && (bytes[end - 1] == '\r' || bytes[end - 1] == '\n' || bytes[end - 1] == ' ' || bytes[end - 1] == '\t')) --end;
        char *value = malloc(end - begin + 1);
        if (!value) return fail(&t->failure, QA_ERROR_MEMORY, "allocating HTTP response metadata");
        memcpy(value, bytes + begin, end - begin); value[end - begin] = 0;
        free(t->content_type); t->content_type = value; t->response.content_type = value;
    } else if ((n == 2 && bytes[0] == '\r' && bytes[1] == '\n') || (n == 1 && bytes[0] == '\n')) {
        if (t->callbacks.headers && !t->callbacks.headers(t->callbacks.context, t->id, &t->response, &t->failure)) {
            if (t->failure.code == QA_OK) fail(&t->failure, QA_ERROR_IO, "HTTP response rejected by consumer");
            return false;
        }
    }
    return true;
}
static size_t intake_data(char *bytes, size_t size, size_t count, void *context,
    qa_sys_http_action action) {
    http_transfer *t = context;
    if (size && count > SIZE_MAX / size) {
        fail(&t->transport_failure, QA_ERROR_MEMORY,
            action == QA_SYS_HTTP_HEADER ? "HTTP header size overflow" : "HTTP body size overflow"); return 0;
    }
    size_t n = size * count;
    if (t->canceled || t->transport_failure.code != QA_OK) return 0;
    if (action == QA_SYS_HTTP_HEADER && t->same_origin_redirects) {
        if (n>=5 && !memcmp(bytes, "HTTP/", 5)) t->intake_status = status_code(bytes, n);
        else if (t->intake_status>=300 && t->intake_status<400 && n>=9 &&
            prefix_equal(bytes, "location:", 9) && !redirect_origin(t, bytes, n)) return 0;
    }
    if (action == QA_SYS_HTTP_BODY && ((uint64_t)n > UINT64_MAX - t->admitted_bytes ||
        (t->maximum_bytes && ((uint64_t)n > t->maximum_bytes ||
            t->admitted_bytes > t->maximum_bytes - (uint64_t)n)))) {
        fail(&t->transport_failure, QA_ERROR_FORMAT, "HTTP response exceeds admitted byte limit"); return 0;
    }
    qa_http *owner = t->owner;
    qa_platform_event_result admitted = qa_platform_events_admit(owner->events,
        QA_PLATFORM_EVENT_HTTP, n);
    /* No offered bytes have been accepted on pause. libcurl replays this exact
     * callback, including headers, when collect resumes the handle. */
    if (admitted == QA_PLATFORM_EVENT_FULL) { t->paused = true; return CURL_WRITEFUNC_PAUSE; }
    if (admitted == QA_PLATFORM_EVENT_OVERSIZE) {
        fail(&t->transport_failure, QA_ERROR_CAPACITY, "HTTP callback exceeds platform event backing"); return 0;
    }
    qa_sys_event event = {.kind = QA_PLATFORM_EVENT_HTTP, .time_ns = owner->time_ns,
        .data.http = {.source_id = owner->source_id, .request_id = t->id, .action = action}};
    admitted = qa_platform_events_push(owner->events, &event,
        (qa_bytes){(const uint8_t *)bytes, n}, (qa_bytes){0});
    if (admitted != QA_PLATFORM_EVENT_ACCEPTED) {
        t->paused = true; return CURL_WRITEFUNC_PAUSE;
    }
    if (action == QA_SYS_HTTP_BODY) t->admitted_bytes += (uint64_t)n;
    return n;
}
static size_t header_data(char *bytes, size_t size, size_t count, void *context) {
    return intake_data(bytes, size, count, context, QA_SYS_HTTP_HEADER);
}
static size_t body_data(char *bytes, size_t size, size_t count, void *context) {
    return intake_data(bytes, size, count, context, QA_SYS_HTTP_BODY);
}
static void canceled_remove(qa_http *owner) {
    for (http_transfer **link = &owner->transfers; *link;) {
        if ((*link)->canceled) remove_transfer(owner, link); else link = &(*link)->next;
    }
}
static void terminal_set(http_transfer *t, CURLcode result) {
    if (t->terminal) return;
    if (t->in_multi) (void)curl_multi_remove_handle(t->owner->multi, t->easy);
    t->in_multi = t->paused = false; t->terminal = true; t->result = result;
    t->terminal_order = ++t->owner->terminal_sequence;
}
static bool terminals_publish(qa_http *owner) {
    for (;;) {
        http_transfer *first = NULL;
        for (http_transfer *t = owner->transfers; t; t = t->next)
            if (t->terminal && !t->completion_queued && !t->canceled &&
                (!first || t->terminal_order < first->terminal_order)) first = t;
        if (!first) { owner->terminal_sequence = 0; return true; }
        if (qa_platform_events_admit(owner->events, QA_PLATFORM_EVENT_HTTP,
            sizeof(first->transport_failure)) != QA_PLATFORM_EVENT_ACCEPTED) return false;
        qa_sys_event event = {.kind = QA_PLATFORM_EVENT_HTTP, .time_ns = owner->time_ns,
            .data.http = {.source_id = owner->source_id, .request_id = first->id,
                .action = QA_SYS_HTTP_COMPLETE, .result = (uint32_t)first->result}};
        if (qa_platform_events_push(owner->events, &event,
            (qa_bytes){(const uint8_t *)&first->transport_failure, sizeof(first->transport_failure)},
            (qa_bytes){0}) != QA_PLATFORM_EVENT_ACCEPTED) return false;
        first->completion_queued = true;
    }
}
bool qa_http_event(qa_http *owner, const qa_sys_event *event, qa_bytes bytes, qa_error *error) {
    if (!owner || event->kind != QA_PLATFORM_EVENT_HTTP ||
        event->data.http.source_id != owner->source_id) return true;
    if (owner->pumping || owner->pending_restore || owner->detached)
        return fail(error, QA_ERROR_ARGUMENT, "HTTP delivery requires its published native continuation and returned callbacks");
    http_transfer **link = &owner->transfers;
    while (*link && (*link)->id != event->data.http.request_id) link = &(*link)->next;
    if (!*link) return true;
    http_transfer *t = *link;
    owner->pumping = true;
    if (!t->canceled && event->data.http.action == QA_SYS_HTTP_COMPLETE) {
        qa_error failure = t->failure;
        if (failure.code == QA_OK) memcpy(&failure, bytes.data, sizeof(failure));
        if (failure.code == QA_OK && event->data.http.result != (uint32_t)CURLE_OK)
            qa_error_set(&failure, QA_ERROR_IO, 0, "HTTP transport failed (%u)", (unsigned)event->data.http.result);
        *link = t->next;
        if (t->callbacks.complete)
            t->callbacks.complete(t->callbacks.context, t->id, &t->response,
                failure.code == QA_OK ? NULL : &failure);
        transfer_free(t);
    } else if (!t->canceled && !t->rejected) {
        bool accepted = true;
        if (event->data.http.action == QA_SYS_HTTP_HEADER) accepted = header_receive(t, bytes);
        else if (event->data.http.action == QA_SYS_HTTP_BODY) {
            t->response.bytes += (uint64_t)bytes.size;
            if (t->callbacks.body) accepted = t->callbacks.body(t->callbacks.context,
                t->id, &t->response, bytes, &t->failure);
        }
        if (!accepted && !t->canceled) {
            if (t->failure.code == QA_OK) fail(&t->failure, QA_ERROR_IO, "HTTP response rejected by consumer");
            t->rejected = true;
            terminal_set(t, CURLE_WRITE_ERROR);
        }
    }
    canceled_remove(owner);
    owner->pumping = false;
    return true;
}
bool qa_http_collect(qa_http *owner, uint64_t time_ns, qa_error *error) {
    if (!owner || owner->pumping || owner->pending_restore || owner->detached)
        return fail(error, QA_ERROR_ARGUMENT, "HTTP progress requires its published native continuation and returned callbacks");
    owner->pumping = true; owner->time_ns = time_ns;
    canceled_remove(owner);
    if (!terminals_publish(owner)) { owner->pumping = false; return true; }
    for (http_transfer *t = owner->transfers; t; t = t->next) if (t->in_multi && t->paused) {
        /* Unpausing can synchronously offer buffered bytes. Owner custody and
         * the admission timestamp remain set throughout that call. */
        t->paused = false;
        CURLcode resumed = curl_easy_pause(t->easy, CURLPAUSE_CONT);
        if (resumed != CURLE_OK) terminal_set(t, resumed);
    }
    if (!terminals_publish(owner)) { owner->pumping = false; return true; }
    int running = 0;
    CURLMcode result = curl_multi_perform(owner->multi, &running);
    int queued = 0;
    while (qa_platform_events_admit(owner->events, QA_PLATFORM_EVENT_HTTP,
        sizeof(qa_error)) == QA_PLATFORM_EVENT_ACCEPTED) {
        /* info_read removes the native completion; retain it only after its
         * common record has room, and copy result before remove_handle. */
        CURLMsg *message = curl_multi_info_read(owner->multi, &queued);
        if (!message) break;
        if (message->msg != CURLMSG_DONE) continue;
        http_transfer *t = NULL;
        (void)curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &t);
        if (!t) continue;
        CURLcode completed_result = message->data.result;
        terminal_set(t, completed_result);
        if (!terminals_publish(owner)) break;
    }
    owner->pumping = false;
    return result == CURLM_OK || fail(error, QA_ERROR_IO, "HTTP transfer queue failed");
}
bool qa_http_submit(qa_http *owner, const qa_http_request *r, qa_http_request_id *out, qa_error *error) {
    if (!owner || !r || !out || owner->pumping || owner->pending_restore || owner->detached || !r->url || !header_name(r->method) ||
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
    t->timeout_ms = r->timeout_ms; t->connect_timeout_ms = r->connect_timeout_ms;
    t->maximum_redirects = r->maximum_redirects; t->same_origin_redirects = r->same_origin_redirects; t->callbacks_bound = true;
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
    t->in_multi = true;
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
    if (!qa_http_collect(owner, 0, error)) return false;
    if (!owner->owns_events) return true;
    for (;;) {
        qa_platform_event_cursor cursor = {0}; qa_sys_event event; qa_bytes bytes;
        bool delivered = false;
        while (qa_platform_events_read(owner->events, &cursor, &event, &bytes)) {
            if (!qa_http_event(owner, &event, bytes, error)) return false;
            qa_platform_events_consume(owner->events, &cursor); delivered = true;
        }
        if (!delivered) return true;
        (void)terminals_publish(owner);
    }
}
size_t qa_http_pending(const qa_http *owner) {
    size_t count = 0; if (owner) for (const http_transfer *t = owner->transfers; t; t = t->next) if (!t->canceled) ++count;
    return count;
}
bool qa_http_next_timeout(const qa_http *owner, uint32_t *milliseconds, bool *pending, qa_error *error) {
    if (!owner || owner->pending_restore || owner->detached || !milliseconds || !pending) return fail(error, QA_ERROR_ARGUMENT, "invalid or detached HTTP wakeup query");
    long timeout = -1;
    if (curl_multi_timeout(owner->multi, &timeout) != CURLM_OK) return fail(error, QA_ERROR_IO, "HTTP wakeup query failed");
    *pending = qa_http_pending(owner) != 0;
    *milliseconds = timeout < 0 ? 1000 : (uint32_t)(timeout > UINT32_MAX ? UINT32_MAX : timeout);
    return true;
}

static bool redirect_policy_fields(qa_source_save_io *io, http_transfer *t) {
    uint32_t policy = t->maximum_redirects | (t->same_origin_redirects ? UINT32_C(0x80000000) : 0);
    if (!qa_source_save_u32(io, &policy)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        t->maximum_redirects = policy & UINT32_C(0x7fffffff);
        t->same_origin_redirects = (policy & UINT32_C(0x80000000)) != 0;
    }
    return true;
}
static bool transfer_fields(qa_source_save_io *io, http_transfer *t) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t status = t->response.status;
    size_t count = 0;
    if (!reading) for (const struct curl_slist *h = t->headers; h; h = h->next) ++count;
    if (!qa_source_save_u64(io, &t->id) || !t->id ||
        !tool_save_text(io, &t->url) || !t->url || !*t->url ||
        !tool_save_text(io, &t->method) || !header_name(t->method) ||
        !qa_source_save_u32(io, &t->timeout_ms) || !t->timeout_ms || t->timeout_ms > INT32_MAX ||
        !qa_source_save_u32(io, &t->connect_timeout_ms) || t->connect_timeout_ms > INT32_MAX ||
        !redirect_policy_fields(io, t) ||
        !qa_source_save_u64(io, &t->maximum_bytes) || !tool_save_blob(io, &t->body, false) ||
        t->body.size > (size_t)PTRDIFF_MAX ||
        !qa_source_save_count(io, &count, reading ? io->input.size - io->offset : SIZE_MAX))
        return tool_save_fail(io, "Invalid HTTP request continuation");
    const struct curl_slist *h = t->headers;
    for (size_t i = 0; i < count; ++i) {
        char *line = reading ? NULL : h->data;
        bool ok = tool_save_text(io, &line) && safe_value(line) && strchr(line, ':');
        if (ok && reading) {
            struct curl_slist *next = curl_slist_append(t->headers, line);
            if (!next) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Retaining HTTP request headers"); io->failed = true; ok = false; }
            else t->headers = next;
        }
        if (reading) free(line); else h = h->next;
        if (!ok) return tool_save_fail(io, "Invalid HTTP request header continuation");
    }
    if (!tool_save_text(io, &t->content_type) ||
        !qa_source_save_u32(io, &status) || status > 999 ||
        !qa_source_save_u64(io, &t->response.bytes) ||
        !qa_source_save_u64(io, &t->response_body_start) ||
        t->response_body_start > t->response.bytes ||
        (t->maximum_bytes && t->response.bytes > t->maximum_bytes) ||
        !qa_source_save_bool(io, &t->response.has_range) ||
        !qa_source_save_u64(io, &t->response.range_first) ||
        !qa_source_save_u64(io, &t->response.range_last) ||
        !qa_source_save_u64(io, &t->response.range_total) ||
        !tool_save_error(io, &t->failure) || !qa_source_save_bool(io, &t->canceled))
        return tool_save_fail(io, "Invalid HTTP response continuation");
    if ((t->response.has_range && (t->response.range_first > t->response.range_last ||
         t->response.range_last >= t->response.range_total)) ||
        (!t->response.has_range && (t->response.range_first || t->response.range_last || t->response.range_total)))
        return tool_save_fail(io, "Invalid HTTP response range continuation");
    if (reading) {
        t->response.status = status; t->response.content_type = t->content_type;
        t->admitted_bytes = t->response.bytes; t->callbacks_bound = t->canceled;
    }
    return true;
}

bool qa_http_checkpoint(const qa_http *owner, qa_buffer *out, qa_error *error) {
    if (!out || !qa_http_checkpoint_ready(owner, error)) return false;
    qa_source_save_io io = {0}; uint64_t next = owner->next_id;
    size_t count = 0;
    for (const http_transfer *t = owner->transfers; t; t = t->next) ++count;
    if (!qa_source_save_writer(&io, NULL, error)) return false;
    bool ok = qa_source_save_u64(&io, &next) &&
        qa_source_save_count(&io, &count, SIZE_MAX);
    for (http_transfer *t = owner->transfers; ok && t; t = t->next) ok = transfer_fields(&io, t);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return ok;
}

bool qa_http_restore(qa_http *owner, qa_bytes bytes, qa_error *error) {
    if (!owner || owner->pumping || owner->transfers)
        return fail(error, QA_ERROR_ARGUMENT, "HTTP restoration requires an empty native transfer owner");
    qa_source_save_io io = {0}; uint64_t next = 0; size_t count = 0;
    if (!qa_source_save_reader(&io, NULL, bytes, error)) return false;
    bool ok = qa_source_save_u64(&io, &next) && qa_source_save_count(&io, &count, bytes.size / 96);
    http_transfer *head = NULL, **tail = &head;
    for (size_t i = 0; ok && i < count; ++i) {
        http_transfer *t = calloc(1, sizeof(*t));
        if (!t) { fail(error, QA_ERROR_MEMORY, "Retaining detached HTTP transfer"); ok = false; break; }
        *tail = t; tail = &t->next; t->owner = owner;
        ok = transfer_fields(&io, t) && (!next || t->id < next);
        for (const http_transfer *previous = head; ok && previous != t; previous = previous->next)
            if (previous->id == t->id) ok = false;
    }
    if (ok) ok = qa_source_save_finish(&io, NULL);
    if (ok) { owner->transfers = head; owner->next_id = next; owner->pending_restore = false; owner->detached = head != NULL; }
    else {
        while (head) { http_transfer *t = head; head = t->next; transfer_free(t); }
        if (!io.failed && (!error || error->code == QA_OK)) fail(error, QA_ERROR_FORMAT, "Invalid HTTP continuation or request identities");
    }
    qa_source_save_dispose(&io); return ok;
}

bool qa_http_restore_callbacks(qa_http *owner, qa_http_request_id id,
    const qa_http_callbacks *callbacks, qa_error *error) {
    if (!owner || !owner->detached || owner->pumping || !id || !callbacks)
        return fail(error, QA_ERROR_ARGUMENT, "HTTP consumer binding requires its detached request");
    for (http_transfer *t = owner->transfers; t; t = t->next) if (t->id == id) {
        if (t->callbacks_bound || t->canceled)
            return fail(error, QA_ERROR_ARGUMENT, "HTTP request already has its continuation consumer");
        t->callbacks = *callbacks; t->callbacks_bound = true; return true;
    }
    return fail(error, QA_ERROR_NOT_FOUND, "HTTP consumer has no saved request identity");
}

static const http_transfer *continuation_at(const qa_http *owner, qa_http_request_id id) {
    if (owner && id && !owner->pending_restore)
        for (const http_transfer *t = owner->transfers; t; t = t->next) if (t->id == id) return t;
    return NULL;
}

bool qa_http_continuation_read(const qa_http *owner, qa_http_request_id id,
    qa_http_continuation_view *out, qa_error *error) {
    const http_transfer *t = continuation_at(owner, id);
    if (!t || !out) return fail(error, QA_ERROR_NOT_FOUND, "Missing actual HTTP request continuation");
    size_t count = 0;
    for (const struct curl_slist *h = t->headers; h; h = h->next) ++count;
    *out = (qa_http_continuation_view){.url = t->url, .method = t->method,
        .request_body = {t->body.data, t->body.size}, .header_count = count,
        .timeout_ms = t->timeout_ms, .connect_timeout_ms = t->connect_timeout_ms,
        .maximum_redirects = t->maximum_redirects, .maximum_response_bytes = t->maximum_bytes,
        .response_body_start = t->response_body_start, .response = t->response,
        .failure = t->failure, .canceled = t->canceled, .same_origin_redirects = t->same_origin_redirects};
    return true;
}

bool qa_http_continuation_header(const qa_http *owner, qa_http_request_id id, size_t index,
    const char **out, qa_error *error) {
    const http_transfer *t = continuation_at(owner, id);
    if (!t || !out) return fail(error, QA_ERROR_NOT_FOUND, "Missing actual HTTP request headers");
    const struct curl_slist *h = t->headers;
    for (size_t i = 0; h && i < index; ++i) h = h->next;
    if (!h) return fail(error, QA_ERROR_NOT_FOUND, "Missing actual HTTP request header ordinal");
    *out = h->data; return true;
}

bool qa_http_handoff_ready(const qa_http *active, const qa_http *candidate, qa_error *error) {
    if (!active || !candidate || active == candidate || active->detached ||
        !qa_http_checkpoint_ready(active, error) || !qa_http_checkpoint_ready(candidate, error))
        return fail(error, QA_ERROR_ARGUMENT, "HTTP handoff requires two idle independent owners");
    if (!active->transfers && !candidate->transfers) return true;
    for (const http_transfer *t = candidate->transfers; t; t = t->next)
        if (!t->callbacks_bound || t->easy)
            return fail(error, QA_ERROR_ARGUMENT, "HTTP handoff lacks a detached consumer binding");
    qa_buffer old = {0}, restored = {0};
    bool ok = qa_http_checkpoint(active, &old, error) && qa_http_checkpoint(candidate, &restored, error);
    if (ok && (old.size != restored.size || memcmp(old.data, restored.data, old.size)))
        ok = fail(error, QA_ERROR_ARGUMENT, "HTTP native continuation has advanced beyond the saved request cut");
    qa_buffer_free(&old); qa_buffer_free(&restored); return ok;
}

void qa_http_handoff_publish(qa_http *active, qa_http *candidate) {
    for (http_transfer *t = active->transfers; t; t = t->next) {
        for (const http_transfer *restored = candidate->transfers; restored; restored = restored->next)
            if (restored->id == t->id) { t->callbacks = restored->callbacks; break; }
        t->owner = candidate;
    }
    while (candidate->transfers) {
        http_transfer *t = candidate->transfers; candidate->transfers = t->next; transfer_free(t);
    }
    CURLM *empty = candidate->multi; candidate->multi = active->multi; active->multi = empty;
    qa_platform_events *events = candidate->events; candidate->events = active->events; active->events = events;
    bool owns_events = candidate->owns_events; candidate->owns_events = active->owns_events; active->owns_events = owns_events;
    uint64_t source_id = candidate->source_id; candidate->source_id = active->source_id; active->source_id = source_id;
    candidate->time_ns = active->time_ns;
    candidate->terminal_sequence = active->terminal_sequence; active->terminal_sequence = 0;
    candidate->transfers = active->transfers; active->transfers = NULL;
    candidate->detached = false;
}
