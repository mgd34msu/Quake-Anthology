#include "llm_internal.h"
#include "qa/text.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
typedef SOCKET auth_socket;
#define AUTH_INVALID INVALID_SOCKET
#define auth_close closesocket
static bool blocked(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static bool nonblocking(auth_socket fd) { u_long value = 1; return ioctlsocket(fd, FIONBIO, &value) == 0; }
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/random.h>
#endif
typedef int auth_socket;
#define AUTH_INVALID (-1)
#define auth_close close
static bool blocked(void) { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
static bool nonblocking(auth_socket fd) {
    int flags = fcntl(fd, F_GETFL, 0), descriptor = fcntl(fd, F_GETFD, 0);
#ifdef SO_NOSIGPIPE
    int no_signal = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_signal, sizeof no_signal) != 0) return false;
#endif
    return flags >= 0 && descriptor >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, descriptor | FD_CLOEXEC) == 0;
}
#endif
static const char client_id[] = "app_EMoamEEZ73f0CkXaXp7hrann";
struct llm_auth {
    qa_llm *owner;
    qa_arena storage;
    llm_credentials previous;
    llm_text response;
    qa_http_request_id http_id;
    auth_socket listener, peer;
    char state[44], verifier[44], redirect[96], input[8193];
    size_t input_size, output_sent;
    const char *output;
    double deadline, peer_deadline;
    uint64_t generation;
    qa_error error;
    bool refresh, active, result, committed, winsock;
};
static bool entropy(uint8_t *bytes, size_t count, qa_error *error) {
#ifdef _WIN32
    if (count <= ULONG_MAX && BCryptGenRandom(NULL, bytes, (ULONG)count, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0) return true;
#elif defined(__linux__)
    size_t n = 0;
    while (n < count) { ssize_t got = getrandom(bytes + n, count - n, 0); if (got > 0) n += (size_t)got; else if (got < 0 && errno == EINTR) continue; else break; }
    if (n == count) return true;
#else
    int fd = open("/dev/urandom", O_RDONLY); size_t n = 0;
    if (fd >= 0) { while (n < count) { ssize_t got = read(fd, bytes + n, count - n); if (got > 0) n += (size_t)got; else if (got < 0 && errno == EINTR) continue; else break; } close(fd); }
    if (n == count) return true;
#endif
    return llm_fail(error, "secure operating-system randomness is unavailable");
}
static size_t encode64(const uint8_t *in, size_t n, char *out) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    size_t j = 0; unsigned value = 0, bits = 0;
    for (size_t i = 0; i < n; ++i) { value = (value << 8) | in[i]; bits += 8; while (bits >= 6) { bits -= 6; out[j++] = alphabet[(value >> bits) & 63]; } }
    if (bits) out[j++] = alphabet[(value << (6 - bits)) & 63];
    out[j] = 0; return j;
}
static uint32_t pkce_rotate(uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32 - shift));
}
/* The locally generated 32-byte verifier is always 43 base64url characters.
 * RFC 7636 S256 therefore requires exactly one padded SHA-256 block. */
static void pkce_challenge(const char verifier[43], char challenge[44]) {
    static const uint32_t rounds[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint8_t block[64] = {0}, digest[32];
    memcpy(block, verifier, 43);
    block[43] = 0x80;
    block[62] = (uint8_t)((43 * 8) >> 8);
    block[63] = (uint8_t)(43 * 8);
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) {
        const uint8_t *p = block + i * 4;
        words[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
            ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
    for (size_t i = 16; i < 64; ++i) {
        uint32_t x = words[i - 15], y = words[i - 2];
        uint32_t first = pkce_rotate(x, 7) ^ pkce_rotate(x, 18) ^ (x >> 3);
        uint32_t second = pkce_rotate(y, 17) ^ pkce_rotate(y, 19) ^ (y >> 10);
        words[i] = words[i - 16] + first + words[i - 7] + second;
    }
    uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (size_t i = 0; i < 64; ++i) {
        uint32_t first = h + (pkce_rotate(e, 6) ^ pkce_rotate(e, 11) ^ pkce_rotate(e, 25)) +
            ((e & f) ^ (~e & g)) + rounds[i] + words[i];
        uint32_t second = (pkce_rotate(a, 2) ^ pkce_rotate(a, 13) ^ pkce_rotate(a, 22)) +
            ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    for (size_t i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)state[i];
    }
    encode64(digest, sizeof digest, challenge);
}
bool llm_session_id(char out[37], qa_error *error) {
    uint8_t bytes[16]; if (!entropy(bytes, sizeof bytes, error)) return false;
    bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
    size_t pos = 0; static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) { if (i == 4 || i == 6 || i == 8 || i == 10) out[pos++] = '-'; out[pos++] = hex[bytes[i] >> 4]; out[pos++] = hex[bytes[i] & 15]; }
    out[pos] = 0; return true;
}
static int digit64(unsigned c) {
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a' + 26);
    if (c >= '0' && c <= '9') return (int)(c - '0' + 52);
    return c == '-' ? 62 : c == '_' ? 63 : -1;
}
bool llm_subscription_account(const char *token, qa_buffer *out, qa_error *error) {
    const char *start = strchr(token, '.'); if (!start) return llm_fail(error, "subscription access token has no account metadata");
    ++start; const char *end = strchr(start, '.'); if (!end || (size_t)(end - start) > 65536) return llm_fail(error, "subscription access token has invalid account metadata");
    llm_text decoded = {0}; unsigned value = 0, bits = 0; bool ok = false; qa_json_document *d = NULL;
    for (const char *p = start; p < end; ++p) {
        int x = digit64((unsigned char)*p); if (x < 0) goto done;
        value = (value << 6) | (unsigned)x; bits += 6;
        if (bits >= 8) { bits -= 8; uint8_t byte = (uint8_t)(value >> bits); if (!llm_text_add(&decoded, (qa_bytes){&byte, 1}, error)) goto done; }
    }
    if (!qa_json_parse((qa_bytes){decoded.buffer.data, decoded.buffer.size}, &d, error)) goto done;
    qa_json_id auth = qa_json_get(d, qa_json_root(d), "https://api.openai.com/auth");
    qa_buffer account = {0};
    if (!qa_json_string(d, qa_json_get(d, auth, "chatgpt_account_id"), &account, error)) goto done;
    if (!account.size || memchr(account.data, 0, account.size)) { qa_buffer_free(&account); goto done; }
    for (size_t i = 0; i < account.size; ++i) if (account.data[i] < 33 || account.data[i] > 126) { qa_buffer_free(&account); goto done; }
    *out = account; ok = true;
done:
    qa_json_destroy(d); qa_buffer_free(&decoded.buffer);
    return ok || llm_fail(error, "subscription access token has invalid account metadata");
}
static void close_peer(llm_auth *a) { if (a->peer != AUTH_INVALID) auth_close(a->peer); a->peer = AUTH_INVALID; a->input_size = a->output_sent = 0; a->output = NULL; }
static void close_listener(llm_auth *a) { if (a->listener != AUTH_INVALID) auth_close(a->listener); a->listener = AUTH_INVALID; }
void llm_auth_cancel(qa_llm *s) {
    llm_auth *a = s->auth; s->auth = NULL; s->signing_in = false;
    if (!a) return;
    if (a->active) qa_http_cancel(s->options.http, a->http_id);
    close_peer(a); close_listener(a);
#ifdef _WIN32
    if (a->winsock) WSACleanup();
#endif
    qa_arena_destroy(&a->storage); qa_arena_destroy(&a->previous.storage); qa_buffer_free(&a->response.buffer); free(a);
}
void qa_llm_cancel_sign_in(qa_llm *s) {
    if (!s || s->busy || s->pending_restore || !s->signing_in) return;
    llm_auth_cancel(s); llm_fail(&s->auth_error, "subscription sign-in canceled");
}
static bool form_value(llm_text *text, const char *key, const char *value, qa_error *error) {
    static const char hex[] = "0123456789ABCDEF";
    if ((text->buffer.size && !llm_text_string(text, "&", error)) || !llm_text_string(text, key, error) || !llm_text_string(text, "=", error)) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        unsigned c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            uint8_t byte = (uint8_t)c; if (!llm_text_add(text, (qa_bytes){&byte, 1}, error)) return false;
        } else { char escaped[4] = {'%', hex[c >> 4], hex[c & 15], 0}; if (!llm_text_string(text, escaped, error)) return false; }
    }
    return true;
}
static bool auth_headers(void *context, qa_http_request_id id, const qa_http_response *r, qa_error *error) {
    (void)context; (void)id;
    return (r->status >= 100 && r->status < 300) || llm_fail(error, "subscription token request failed; try signing in again");
}
static bool auth_body(void *context, qa_http_request_id id, const qa_http_response *r, qa_bytes bytes, qa_error *error) {
    (void)id; (void)r; llm_auth *a = context; return llm_text_add(&a->response, bytes, error);
}
static void auth_complete(void *context, qa_http_request_id id, const qa_http_response *r, const qa_error *error) {
    (void)id; (void)r; llm_auth *a = context; a->active = false; a->result = true;
    if (error) llm_fail(&a->error, "subscription token request failed; try signing in again");
}
static bool token_submit(llm_auth *a, const char *code, qa_error *error) {
    llm_text form = {0}; bool ok = form_value(&form, "grant_type", a->refresh ? "refresh_token" : "authorization_code", error) && form_value(&form, "client_id", client_id, error);
    if (ok && a->refresh) ok = form_value(&form, "refresh_token", a->previous.subscription.refresh_token, error);
    if (ok && !a->refresh) ok = form_value(&form, "redirect_uri", a->redirect, error) && form_value(&form, "code", code, error) && form_value(&form, "code_verifier", a->verifier, error);
    qa_http_header header = {"Content-Type", "application/x-www-form-urlencoded"};
    qa_http_request request = {.url = a->owner->options.token_url ? a->owner->options.token_url : "https://auth.openai.com/oauth/token", .method = "POST", .headers = &header, .header_count = 1,
        .body = {form.buffer.data, form.buffer.size}, .timeout_ms = 30000, .maximum_response_bytes = 262144,
        .callbacks = {.context = a, .headers = auth_headers, .body = auth_body, .complete = auth_complete}};
    if (ok) ok = qa_http_submit(a->owner->options.http, &request, &a->http_id, error);
    qa_buffer_free(&form.buffer);
    if (ok) { a->active = true; a->deadline = llm_wall_milliseconds(a->owner) + 30000; }
    return ok;
}
static llm_auth *auth_new(qa_llm *s, bool refresh, qa_error *error) {
    if (s->auth_generation == UINT64_MAX) { llm_fail(error, "subscription authorization generation exhausted"); return NULL; }
    llm_auth *a = calloc(1, sizeof *a);
    if (!a) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating subscription authorization"); return NULL; }
    a->owner = s; a->listener = a->peer = AUTH_INVALID; a->refresh = refresh; a->generation = ++s->auth_generation;
    s->auth = a; return a;
}
bool qa_llm_sign_in(qa_llm *s, qa_error *error) {
    if (!s || s->busy || s->pending_restore) return llm_fail(error, "subscription sign-in requires restored continuation and returned callbacks");
    llm_auth_cancel(s); s->auth_error = (qa_error){0};
    llm_auth *a = auth_new(s, false, error); if (!a) return false;
    uint8_t bytes[32]; char challenge[44]; llm_text query = {0}, url = {0}; bool ok = false;
    double now = llm_wall_milliseconds(s);
    if (!isfinite(now) || !isfinite(now + s->options.callback_timeout_ms)) { llm_fail(error, "invalid subscription clock"); goto done; }
    a->deadline = now + s->options.callback_timeout_ms;
    if (!entropy(bytes, sizeof bytes, error)) goto done;
    encode64(bytes, sizeof bytes, a->state);
    if (!entropy(bytes, sizeof bytes, error)) goto done;
    encode64(bytes, sizeof bytes, a->verifier);
    pkce_challenge(a->verifier, challenge);
#ifdef _WIN32
    WSADATA data; if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { llm_fail(error, "could not initialize subscription callback sockets"); goto done; } a->winsock = true;
#endif
    a->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    BOOL exclusive = TRUE;
    if (a->listener != AUTH_INVALID && setsockopt(a->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof exclusive) != 0) { llm_fail(error, "could not reserve local subscription callback socket"); goto done; }
#endif
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(s->options.callback_port);
    if (a->listener == AUTH_INVALID || !nonblocking(a->listener) || bind(a->listener, (struct sockaddr *)&address, sizeof address) != 0 || listen(a->listener, 4) != 0) { llm_fail(error, "could not open local subscription callback; close other sign-in windows and retry"); goto done; }
    (void)snprintf(a->redirect, sizeof a->redirect, "http://localhost:%u/auth/callback", (unsigned)s->options.callback_port);
    if (!form_value(&query, "response_type", "code", error) || !form_value(&query, "client_id", client_id, error) || !form_value(&query, "redirect_uri", a->redirect, error) ||
        !form_value(&query, "scope", "openid profile email offline_access", error) || !form_value(&query, "state", a->state, error) || !form_value(&query, "code_challenge", challenge, error) ||
        !form_value(&query, "code_challenge_method", "S256", error) || !form_value(&query, "id_token_add_organizations", "true", error) || !form_value(&query, "codex_cli_simplified_flow", "true", error) || !form_value(&query, "originator", "pi", error)) goto done;
    const char *base = s->options.authorization_url ? s->options.authorization_url : "https://auth.openai.com/oauth/authorize";
    if (!llm_text_string(&url, base, error) || !llm_text_string(&url, strchr(base, '?') ? "&" : "?", error) || !llm_text_add(&url, (qa_bytes){query.buffer.data, query.buffer.size}, error)) goto done;
    ++s->busy; ok = s->options.open_browser(s->options.context, (const char *)url.buffer.data, error); --s->busy;
done:
    qa_buffer_free(&query.buffer); qa_buffer_free(&url.buffer);
    if (!ok && error && error->code == QA_OK) llm_fail(error, "could not launch subscription sign-in browser");
    if (ok) s->signing_in = true;
    else { if (error) s->auth_error = *error; llm_auth_cancel(s); }
    return ok;
}
static int hex_value(unsigned c) { return c >= '0' && c <= '9' ? (int)(c - '0') : c >= 'a' && c <= 'f' ? (int)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (int)(c - 'A' + 10) : -1; }
static bool query_decode(char *text) {
    char *out = text;
    for (char *p = text; *p; ++p) {
        unsigned c = (unsigned char)*p;
        if (c == '%') { if (!p[1] || !p[2]) return false; int a = hex_value((unsigned char)p[1]), b = hex_value((unsigned char)p[2]); if (a < 0 || b < 0) return false; c = (unsigned)((a << 4) | b); p += 2; }
        else if (c == '+') c = ' ';
        if (c < 32 || c == 127) return false;
        *out++ = (char)c;
    }
    *out = 0; return true;
}
static const char reply_ok[] = "HTTP/1.0 200 OK\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\nAuthorization received. You can return to the game.\n";
static const char reply_bad[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 16\r\n\r\nSign-in failed.\n";
static const char reply_missing[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 10\r\n\r\nNot found\n";
static bool callback_request(llm_auth *a, qa_error *error) {
    char *end = strstr(a->input, "\r\n"); if (!end) return false; *end = 0;
    char *space = strchr(a->input, ' '); if (!space) { a->output = reply_bad; return true; } *space++ = 0;
    char *version = strchr(space, ' '); if (!version) { a->output = reply_bad; return true; } *version = 0;
    if (strcmp(a->input, "GET")) { a->output = reply_bad; return true; }
    char *query = strchr(space, '?'); if (query) *query++ = 0;
    if (strcmp(space, "/auth/callback")) { a->output = reply_missing; return true; }
    char *state = NULL, *code = NULL; bool denied = false, valid = query != NULL;
    while (query && *query) {
        char *next = strchr(query, '&'); if (next) *next++ = 0;
        char *value = strchr(query, '='); if (value) *value++ = 0;
        if (!query_decode(query) || (value && !query_decode(value))) valid = false;
        if (!strcmp(query, "state")) { if (state) valid = false; state = value; }
        else if (!strcmp(query, "code")) { if (code) valid = false; code = value; }
        else if (!strcmp(query, "error")) denied = true;
        query = next;
    }
    if (!valid || !state || strcmp(state, a->state) || !code || !*code || denied) {
        a->output = reply_bad; a->result = true; llm_fail(&a->error, "subscription sign-in was not authorized or its state did not match"); close_listener(a); return true;
    }
    close_listener(a); a->output = reply_ok;
    qa_error failure = {0};
    if (!token_submit(a, code, &failure)) { a->result = true; a->error = failure; if (error) *error = failure; }
    return true;
}
static bool token_text(const qa_json_document *d, qa_json_id root, const char *key, qa_arena *arena, const char **out, qa_error *error) {
    qa_buffer value = {0}; if (!qa_json_string(d, qa_json_get(d, root, key), &value, error)) return false;
    bool ok = llm_trim((qa_bytes){value.data, value.size}).size != 0 && !memchr(value.data, 0, value.size);
    if (ok) { *out = llm_arena_text(arena, (qa_bytes){value.data, value.size}, error); ok = *out != NULL; }
    qa_buffer_free(&value); return ok;
}
static bool commit_token(llm_auth *a, qa_error *error) {
    qa_llm *s = a->owner; qa_json_document *d = NULL; llm_credentials next = {0}; bool ok = false;
    if (!qa_json_parse((qa_bytes){a->response.buffer.data, a->response.buffer.size}, &d, error)) goto done;
    qa_json_id root = qa_json_root(d); double expires, now = llm_wall_milliseconds(s);
    if (qa_json_type(d, root) != QA_JSON_OBJECT || !qa_json_number(d, qa_json_get(d, root, "expires_in"), &expires, error) || !isfinite(expires) || expires <= 0 || !isfinite(now + expires * 1000)) goto done;
    if (!llm_credentials_load(s, &next, error)) goto done;
    /* Refresh must not overwrite a sign-out or a credential changed on disk. */
    if (a->refresh && !next.subscribed) { llm_fail(error, "subscription credential was removed while refreshing; sign in again"); goto done; }
    if (a->refresh && (strcmp(next.subscription.refresh_token, a->previous.subscription.refresh_token) ||
        strcmp(next.subscription.access_token, a->previous.subscription.access_token))) {
        qa_arena_destroy(&s->credentials.storage); s->credentials = next; next = (llm_credentials){0};
        s->settings_errors[1] = (qa_error){0}; ok = true; goto done;
    }
    llm_subscription credential = {.expires_at = now + expires * 1000};
    if (!token_text(d, root, "access_token", &next.storage, &credential.access_token, error) || !token_text(d, root, "refresh_token", &next.storage, &credential.refresh_token, error)) goto done;
    qa_json_id type = qa_json_get(d, root, "token_type");
    if (type == QA_JSON_NONE || qa_json_type(d, type) == QA_JSON_NULL) credential.token_type = "Bearer";
    else if (!token_text(d, root, "token_type", &next.storage, &credential.token_type, error)) goto done;
    qa_json_id scope = qa_json_get(d, root, "scope");
    if (qa_json_type(d, scope) == QA_JSON_STRING) {
        qa_buffer scopes = {0}; if (!qa_json_string(d, scope, &scopes, error)) goto done;
        char *text = llm_arena_text(&next.storage, (qa_bytes){scopes.data, scopes.size}, error); qa_buffer_free(&scopes); if (!text) goto done;
        size_t capacity = strlen(text) + 1;
        if (capacity > SIZE_MAX / sizeof(char *)) goto done;
        const char **items = qa_arena_alloc(&next.storage, capacity * sizeof(*items), _Alignof(char *), error); if (!items) goto done;
        size_t cursor = 0, begin = 0; uint32_t scalar;
        qa_bytes span = {(const uint8_t *)text, strlen(text)};
        while (cursor < span.size) { begin = cursor; if (!qa_utf8_next(span, &cursor, &scalar)) goto done; if (qa_unicode_whitespace(scalar)) { memset(text + begin, 0, cursor - begin); continue; }
            items[credential.scope_count++] = text + begin;
            while (cursor < span.size) { begin = cursor; if (!qa_utf8_next(span, &cursor, &scalar)) goto done; if (qa_unicode_whitespace(scalar)) { memset(text + begin, 0, cursor - begin); break; } }
        }
        credential.scopes = items;
    } else if (a->refresh) {
        credential.scope_count = a->previous.subscription.scope_count;
        const char **items = credential.scope_count ? qa_arena_alloc(&next.storage, credential.scope_count * sizeof(*items), _Alignof(char *), error) : NULL;
        if (credential.scope_count && !items) goto done;
        for (size_t i = 0; i < credential.scope_count; ++i) { items[i] = llm_arena_text(&next.storage, (qa_bytes){(const uint8_t *)a->previous.subscription.scopes[i], strlen(a->previous.subscription.scopes[i])}, error); if (!items[i]) goto done; }
        credential.scopes = items;
    }
    next.subscription = credential; next.subscribed = true;
    if (!llm_credentials_save(s, &next, error)) goto done;
    qa_arena_destroy(&s->credentials.storage); s->credentials = next; next = (llm_credentials){0}; s->settings_errors[1] = (qa_error){0}; ok = true;
done:
    qa_arena_destroy(&next.storage); qa_json_destroy(d); return ok || llm_fail(error, "subscription token request failed; try signing in again");
}
bool llm_auth_tick(qa_llm *s, qa_error *error) {
    llm_auth *a = s->auth; if (!a) return true;
    double now = llm_wall_milliseconds(s);
    if (!isfinite(now)) return llm_fail(error, "invalid subscription clock");
    if (!a->result && now >= a->deadline) { if (a->active) qa_http_cancel(s->options.http, a->http_id); a->active = false; a->result = true; llm_fail(&a->error, "subscription authorization timed out; try again"); close_listener(a); }
    if (a->peer == AUTH_INVALID && a->listener != AUTH_INVALID) {
        a->peer = accept(a->listener, NULL, NULL);
        if (a->peer != AUTH_INVALID) { if (!nonblocking(a->peer)) close_peer(a); else a->peer_deadline = now + 5000; }
        else if (!blocked()) { a->result = true; llm_fail(&a->error, "subscription callback listener failed"); close_listener(a); }
    }
    if (a->peer != AUTH_INVALID && now >= a->peer_deadline) close_peer(a);
    if (a->peer != AUTH_INVALID && !a->output) {
        size_t available = sizeof a->input - 1 - a->input_size;
#ifdef _WIN32
        int got = recv(a->peer, a->input + a->input_size, (int)available, 0);
#else
        ssize_t got = recv(a->peer, a->input + a->input_size, available, 0);
#endif
        if (got > 0) {
            a->input_size += (size_t)got; a->input[a->input_size] = 0;
            if (memchr(a->input, 0, a->input_size)) a->output = reply_bad;
            else if (strstr(a->input, "\r\n\r\n")) (void)callback_request(a, error);
            else if (a->input_size == sizeof a->input - 1) a->output = reply_bad;
        }
        else if (got == 0 || !blocked()) close_peer(a);
    }
    if (a->peer != AUTH_INVALID && a->output) {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        size_t length = strlen(a->output);
        size_t remaining = length - a->output_sent;
#ifdef _WIN32
        int sent = send(a->peer, a->output + a->output_sent, (int)remaining, flags);
#else
        ssize_t sent = send(a->peer, a->output + a->output_sent, remaining, flags);
#endif
        if (sent > 0) { a->output_sent += (size_t)sent; if (a->output_sent == length) close_peer(a); }
        else if (!blocked()) close_peer(a);
    }
    if (a->result && !a->committed) {
        a->committed = true;
        if (a->error.code == QA_OK) (void)commit_token(a, &a->error);
        s->auth_error = a->error; s->signing_in = false;
        if (!a->refresh && a->error.code == QA_OK) (void)llm_discover_start(s, QA_LLM_SUBSCRIPTION, &s->catalogs[QA_LLM_SUBSCRIPTION].error);
    }
    if (!a->refresh && a->committed && a->peer == AUTH_INVALID) llm_auth_cancel(s);
    return true;
}
bool llm_auth_credential(qa_llm *s, const char *rejected, uint64_t *ticket, qa_arena *arena, llm_subscription *out, bool *ready, qa_error *error) {
    llm_credentials current = {0}; bool ok = false; *ready = false;
    if (!llm_credentials_load(s, &current, error)) return false;
    if (!current.subscribed) { llm_fail(error, "sign in with ChatGPT in LLM options first"); goto done; }
    double now = llm_wall_milliseconds(s); if (!isfinite(now)) { llm_fail(error, "invalid subscription clock"); goto done; }
    bool stale = rejected ? !strcmp(rejected, current.subscription.access_token) : current.subscription.expires_at <= now + 60000;
    if (stale) {
        llm_auth *a = s->auth;
        if (a && !a->refresh) { llm_fail(error, "subscription sign-in is still running"); goto done; }
        if (a && *ticket == a->generation && a->committed) { if (a->error.code != QA_OK) { if (error) *error = a->error; goto done; } }
        else {
            if (a && a->committed) { llm_auth_cancel(s); a = NULL; }
            if (!a) {
                a = auth_new(s, true, error); if (!a) goto done;
                a->previous = current; current = (llm_credentials){0};
                if (!token_submit(a, NULL, error)) { llm_auth_cancel(s); goto done; }
            }
            *ticket = a->generation; ok = true; goto done;
        }
    }
    llm_subscription next = current.subscription;
    next.access_token = llm_arena_text(arena, (qa_bytes){(const uint8_t *)next.access_token, strlen(next.access_token)}, error);
    next.refresh_token = llm_arena_text(arena, (qa_bytes){(const uint8_t *)next.refresh_token, strlen(next.refresh_token)}, error);
    next.token_type = llm_arena_text(arena, (qa_bytes){(const uint8_t *)next.token_type, strlen(next.token_type)}, error);
    const char **scopes = next.scope_count ? qa_arena_alloc(arena, next.scope_count * sizeof(*scopes), _Alignof(char *), error) : NULL;
    if (!next.access_token || !next.refresh_token || !next.token_type || (next.scope_count && !scopes)) goto done;
    for (size_t i = 0; i < next.scope_count; ++i) { scopes[i] = llm_arena_text(arena, (qa_bytes){(const uint8_t *)next.scopes[i], strlen(next.scopes[i])}, error); if (!scopes[i]) goto done; }
    next.scopes = scopes; *out = next; *ready = true; ok = true;
done:
    qa_arena_destroy(&current.storage); return ok;
}
void llm_auth_refresh_users(qa_llm *s, size_t count) { if (s->auth && s->auth->refresh && !count) llm_auth_cancel(s); }
