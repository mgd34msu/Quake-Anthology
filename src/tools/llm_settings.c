#include "llm_internal.h"
#include "qa/text.h"
#include <curl/curl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool llm_provider_valid(qa_llm_provider p) { return p >= QA_LLM_SUBSCRIPTION && p <= QA_LLM_OTHER_API; }
const char *llm_provider_name(qa_llm_provider p) {
    return p == QA_LLM_SUBSCRIPTION ? "chatgpt-subscription" : p == QA_LLM_OPENAI_API ? "chatgpt-api" : "other-api";
}
char *llm_arena_text(qa_arena *arena, qa_bytes bytes, qa_error *error) {
    if ((!bytes.data && bytes.size) || bytes.size == SIZE_MAX) { llm_fail(error, "invalid LLM text span"); return NULL; }
    char *out = qa_arena_alloc(arena, bytes.size + 1, 1, error);
    if (out) { if (bytes.size) memcpy(out, bytes.data, bytes.size); out[bytes.size] = 0; } return out;
}
qa_bytes llm_trim(qa_bytes text) {
    size_t cursor = 0, start = 0, end = 0; uint32_t scalar; bool leading = true;
    while (qa_utf8_next(text, &cursor, &scalar)) {
        if (leading && qa_unicode_whitespace(scalar)) start = cursor;
        else { leading = false; if (!qa_unicode_whitespace(scalar)) end = cursor; }
    }
    return (qa_bytes){text.data ? text.data + start : NULL, end >= start ? end - start : 0};
}
static bool units_valid(qa_bytes text, size_t maximum, bool controls, qa_error *error) {
    if (!qa_utf8_valid(text)) return llm_fail(error, "invalid LLM UTF-8 text");
    size_t cursor = 0, units = 0; uint32_t scalar;
    while (qa_utf8_next(text, &cursor, &scalar)) {
        units += scalar > 0xffff ? 2 : 1;
        if (units > maximum || (controls && (scalar < 32 || scalar == 127))) return llm_fail(error, "invalid LLM text length or controls");
    }
    return true;
}
bool llm_model_text(qa_arena *arena, qa_bytes text, bool empty, const char **out, qa_error *error) {
    if (!units_valid(text, 256, true, error)) return false;
    text = llm_trim(text); if (!empty && !text.size) return llm_fail(error, "service returned invalid model metadata");
    *out = llm_arena_text(arena, text, error); return *out != NULL;
}
bool llm_effort_text(qa_arena *arena, qa_bytes text, const char **out, qa_error *error) {
    text = llm_trim(text);
    if (!text.size || text.size > 64 || text.data[0] < 'a' || text.data[0] > 'z') return llm_fail(error, "invalid reasoning effort");
    for (size_t i = 1; i < text.size; ++i) {
        uint8_t c = text.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return llm_fail(error, "invalid reasoning effort");
    }
    *out = llm_arena_text(arena, text, error); return *out != NULL;
}
bool llm_api_key_text(qa_arena *arena, qa_bytes text, const char **out, qa_error *error) {
    if (!units_valid(text, 16384, false, error)) return false;
    text = llm_trim(text); size_t cursor = 0; uint32_t scalar;
    if (!text.size) return llm_fail(error, "paste a nonempty API key without whitespace");
    while (qa_utf8_next(text, &cursor, &scalar)) if (qa_unicode_whitespace(scalar) || !scalar) return llm_fail(error, "paste a nonempty API key without whitespace");
    *out = llm_arena_text(arena, text, error); return *out != NULL;
}
static bool json_text(const qa_json_document *d, qa_json_id id, qa_arena *arena,
                       const char **out, bool nonempty, qa_error *error) {
    qa_buffer text = {0}; if (!qa_json_string(d, id, &text, error)) return llm_fail(error, "invalid LLM settings data");
    bool ok = !nonempty || llm_trim((qa_bytes){text.data, text.size}).size != 0;
    if (ok && !memchr(text.data, 0, text.size)) { *out = llm_arena_text(arena, (qa_bytes){text.data, text.size}, error); ok = *out != NULL; }
    else ok = llm_fail(error, "invalid LLM settings text");
    qa_buffer_free(&text); return ok;
}
static bool load_json_string(const qa_json_document *d, qa_json_id id, qa_arena *arena,
                              const char **out, bool effort, qa_error *error) {
    if (effort && (id == QA_JSON_NONE || qa_json_type(d, id) == QA_JSON_NULL)) { *out = NULL; return true; }
    qa_buffer text = {0}; if (!qa_json_string(d, id, &text, error)) return llm_fail(error, "invalid LLM settings data");
    bool ok = effort ? llm_effort_text(arena, (qa_bytes){text.data, text.size}, out, error) :
        llm_model_text(arena, (qa_bytes){text.data, text.size}, true, out, error);
    qa_buffer_free(&text); return ok;
}
static bool load_file(qa_llm *service, const char *name, qa_resource **out, bool *found, qa_error *error) {
    qa_error local = {0};
    if (qa_vfs_acquire_from(service->options.settings, service->options.private_mount, name, out, &local)) { *found = true; return true; }
    if (local.code == QA_ERROR_NOT_FOUND) { *found = false; return true; }
    qa_error_set(error, QA_ERROR_IO, 0, "could not read %s", name); return false;
}
static bool object(const qa_json_document *d, qa_json_id id, qa_error *error) {
    return qa_json_type(d, id) == QA_JSON_OBJECT || llm_fail(error, "invalid LLM settings data");
}
static bool version(const qa_json_document *d, qa_json_id root, qa_error *error) {
    double value;
    return object(d, root, error) && qa_json_number(d, qa_json_get(d, root, "version"), &value, error) &&
        (value == 1 || llm_fail(error, "unsupported LLM settings format"));
}
bool llm_preferences_load(qa_llm *service, llm_preferences *out, qa_error *error) {
    llm_preferences next = {.provider = QA_LLM_SUBSCRIPTION, .models = {"", ""}};
    qa_resource *file = NULL; bool found; qa_json_document *d = NULL; bool ok = false;
    if (!load_file(service, "llm.json", &file, &found, error)) goto done;
    if (!found) { ok = true; goto done; }
    if (!qa_json_parse(qa_resource_bytes(file), &d, error)) goto done;
    qa_json_id root = qa_json_root(d), models = qa_json_get(d, root, "models"), efforts = qa_json_get(d, root, "reasoningEfforts");
    if (!version(d, root, error) || !object(d, models, error)) goto done;
    qa_json_id provider = qa_json_get(d, root, "provider"); bool known = false;
    for (qa_llm_provider p = QA_LLM_SUBSCRIPTION; p <= QA_LLM_OTHER_API; ++p)
        if (qa_json_string_equal(d, provider, llm_provider_name(p))) { next.provider = p; known = true; break; }
    if (!known) { llm_fail(error, "invalid LLM provider setting"); goto done; }
    if (efforts != QA_JSON_NONE && qa_json_type(d, efforts) != QA_JSON_NULL && !object(d, efforts, error)) goto done;
    for (size_t i = 0; i < 3; ++i) {
        const char *key = llm_provider_name((qa_llm_provider)i);
        if (i < 2 && !load_json_string(d, qa_json_get(d, models, key), &next.storage, &next.models[i], false, error)) goto done;
        if (!load_json_string(d, qa_json_get(d, efforts, key), &next.storage, &next.efforts[i], true, error)) goto done;
    }
    ok = true;
done:
    qa_json_destroy(d); qa_resource_release(file);
    if (ok) *out = next; else qa_arena_destroy(&next.storage); return ok;
}
bool llm_subscription_parse(const qa_json_document *d, qa_json_id root, qa_arena *arena, llm_subscription *out, qa_error *error) {
    if (!object(d, root, error)) return false;
    llm_subscription value = {0};
    if (!json_text(d, qa_json_get(d, root, "accessToken"), arena, &value.access_token, true, error) ||
        !json_text(d, qa_json_get(d, root, "refreshToken"), arena, &value.refresh_token, true, error) ||
        !json_text(d, qa_json_get(d, root, "tokenType"), arena, &value.token_type, true, error) ||
        !qa_json_number(d, qa_json_get(d, root, "expiresAt"), &value.expires_at, error)) return false;
    if (!isfinite(value.expires_at)) return llm_fail(error, "invalid subscription credential expiry");
    qa_json_id scopes = qa_json_get(d, root, "scopes");
    if (qa_json_type(d, scopes) != QA_JSON_ARRAY) return llm_fail(error, "invalid subscription credential");
    value.scope_count = qa_json_size(d, scopes);
    if (value.scope_count > SIZE_MAX / sizeof(char *)) return llm_fail(error, "subscription scopes overflow");
    const char **items = value.scope_count ? qa_arena_alloc(arena, value.scope_count * sizeof(*items), _Alignof(char *), error) : NULL;
    if (value.scope_count && !items) return false;
    for (size_t i = 0; i < value.scope_count; ++i) if (!json_text(d, qa_json_at(d, scopes, i), arena, &items[i], true, error)) return false;
    value.scopes = items; *out = value; return true;
}
bool llm_credentials_load(qa_llm *service, llm_credentials *out, qa_error *error) {
    llm_credentials next = {0}; qa_resource *file = NULL; bool found; qa_json_document *d = NULL; bool ok = false;
    if (!load_file(service, "chatgpt.key", &file, &found, error)) goto done;
    if (!found) { ok = true; goto done; }
    qa_bytes bytes = llm_trim(qa_resource_bytes(file));
    if (!bytes.size || bytes.data[0] != '{') { ok = llm_api_key_text(&next.storage, qa_resource_bytes(file), &next.api_key, error); goto done; }
    if (!qa_json_parse(bytes, &d, error)) goto done;
    qa_json_id root = qa_json_root(d);
    if (!version(d, root, error)) goto done;
    qa_json_id key = qa_json_get(d, root, "apiKey"), subscription = qa_json_get(d, root, "subscription");
    if (key != QA_JSON_NONE && qa_json_type(d, key) != QA_JSON_NULL) {
        qa_buffer text = {0}; if (!qa_json_string(d, key, &text, error)) goto done;
        bool valid = llm_api_key_text(&next.storage, (qa_bytes){text.data, text.size}, &next.api_key, error); qa_buffer_free(&text); if (!valid) goto done;
    }
    if (subscription != QA_JSON_NONE && qa_json_type(d, subscription) != QA_JSON_NULL) {
        if (!llm_subscription_parse(d, subscription, &next.storage, &next.subscription, error)) goto done;
        next.subscribed = true;
    }
    ok = true;
done:
    qa_json_destroy(d); qa_resource_release(file);
    if (ok) *out = next; else qa_arena_destroy(&next.storage); return ok;
}
static bool base_url(qa_arena *arena, const char *source, const char **out, qa_error *error) {
    CURLU *url = curl_url(); char *scheme = NULL, *host = NULL, *user = NULL, *password = NULL, *query = NULL, *fragment = NULL, *normalized = NULL;
    if (!url) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating service address parser"); return false; }
    bool ok = curl_url_set(url, CURLUPART_URL, source, 0) == CURLUE_OK &&
        curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK && curl_url_get(url, CURLUPART_HOST, &host, 0) == CURLUE_OK;
    bool loopback = host && (!strcmp(host, "localhost") || !strcmp(host, "127.0.0.1") || !strcmp(host, "[::1]"));
    if (!scheme || (strcmp(scheme, "https") && !(loopback && !strcmp(scheme, "http")))) ok = false;
    if (curl_url_get(url, CURLUPART_USER, &user, 0) == CURLUE_OK || curl_url_get(url, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK ||
        curl_url_get(url, CURLUPART_QUERY, &query, 0) == CURLUE_OK || curl_url_get(url, CURLUPART_FRAGMENT, &fragment, 0) == CURLUE_OK) ok = false;
    if (ok) ok = curl_url_get(url, CURLUPART_URL, &normalized, 0) == CURLUE_OK;
    if (ok) {
        size_t n = strlen(normalized); if (n && normalized[n - 1] == '/') --n;
        *out = llm_arena_text(arena, (qa_bytes){(const uint8_t *)normalized, n}, error); ok = *out != NULL;
    } else llm_fail(error, "use HTTPS or loopback HTTP without URL credentials, query or fragment");
    curl_free(scheme); curl_free(host); curl_free(user); curl_free(password); curl_free(query); curl_free(fragment); curl_free(normalized); curl_url_cleanup(url); return ok;
}
bool llm_other_load(qa_llm *service, llm_other *out, qa_error *error) {
    llm_other next = {.base_url = "", .model = ""}; qa_resource *file = NULL; bool found; qa_json_document *d = NULL; bool ok = false;
    if (!load_file(service, "other.service", &file, &found, error)) goto done;
    if (!found) { ok = true; goto done; }
    if (!qa_json_parse(qa_resource_bytes(file), &d, error)) goto done;
    qa_json_id root = qa_json_root(d); const char *url;
    if (!object(d, root, error) || !qa_json_string_equal(d, qa_json_get(d, root, "transport"), "openai-chat-completions")) { llm_fail(error, "Other API requires an OpenAI Chat Completions compatible service"); goto done; }
    if (!json_text(d, qa_json_get(d, root, "baseUrl"), &next.storage, &url, false, error) || !base_url(&next.storage, url, &next.base_url, error) ||
        !load_json_string(d, qa_json_get(d, root, "model"), &next.storage, &next.model, false, error)) goto done;
    ok = true;
done:
    qa_json_destroy(d); qa_resource_release(file);
    if (ok) *out = next; else qa_arena_destroy(&next.storage); return ok;
}
bool llm_other_key_load(qa_llm *service, qa_buffer *out, qa_error *error) {
    qa_resource *file = NULL; bool found;
    if (!load_file(service, "other.key", &file, &found, error)) return false;
    if (!found) { *out = (qa_buffer){0}; return true; }
    qa_arena arena = {0}; const char *key = NULL;
    bool ok = llm_api_key_text(&arena, qa_resource_bytes(file), &key, error); qa_buffer next = {0};
    if (ok) {
        next.size = strlen(key); next.data = malloc(next.size + 1);
        if (next.data) memcpy(next.data, key, next.size + 1); else { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Other API credential"); ok = false; }
    }
    qa_arena_destroy(&arena); qa_resource_release(file); if (ok) *out = next; return ok;
}
static bool save(qa_llm *service, const char *file, qa_json_writer *writer, qa_error *error) {
    qa_buffer bytes = {0};
    bool ok = qa_json_writer_finish(writer, &bytes, error) && qa_vfs_write_private(service->options.settings, service->options.private_mount, file, (qa_bytes){bytes.data, bytes.size}, error);
    qa_json_writer_destroy(writer); qa_buffer_free(&bytes);
    if (!ok) qa_error_set(error, QA_ERROR_IO, 0, "could not save %s", file);
    return ok;
}
bool llm_preferences_save(qa_llm *s, const llm_preferences *p, qa_error *error) {
    qa_json_writer w = {0}; qa_json_writer_object(&w); qa_json_writer_key(&w, "version"); qa_json_writer_number(&w, 1);
    qa_json_writer_key(&w, "provider"); qa_json_writer_string(&w, llm_provider_name(p->provider));
    qa_json_writer_key(&w, "models"); qa_json_writer_object(&w);
    for (size_t i = 0; i < 2; ++i) { qa_json_writer_key(&w, llm_provider_name((qa_llm_provider)i)); qa_json_writer_string(&w, p->models[i]); }
    qa_json_writer_end(&w); qa_json_writer_key(&w, "reasoningEfforts"); qa_json_writer_object(&w);
    for (size_t i = 0; i < 3; ++i) { qa_json_writer_key(&w, llm_provider_name((qa_llm_provider)i)); if (p->efforts[i]) qa_json_writer_string(&w, p->efforts[i]); else qa_json_writer_null(&w); }
    qa_json_writer_end(&w); qa_json_writer_end(&w); return save(s, "llm.json", &w, error);
}
bool llm_credentials_save(qa_llm *s, const llm_credentials *c, qa_error *error) {
    qa_json_writer w = {0}; qa_json_writer_object(&w); qa_json_writer_key(&w, "version"); qa_json_writer_number(&w, 1);
    qa_json_writer_key(&w, "apiKey"); if (c->api_key) qa_json_writer_string(&w, c->api_key); else qa_json_writer_null(&w);
    qa_json_writer_key(&w, "subscription");
    if (!c->subscribed) qa_json_writer_null(&w);
    else {
        const llm_subscription *p = &c->subscription; qa_json_writer_object(&w);
        qa_json_writer_key(&w, "accessToken"); qa_json_writer_string(&w, p->access_token);
        qa_json_writer_key(&w, "refreshToken"); qa_json_writer_string(&w, p->refresh_token);
        qa_json_writer_key(&w, "tokenType"); qa_json_writer_string(&w, p->token_type);
        qa_json_writer_key(&w, "expiresAt"); qa_json_writer_number(&w, p->expires_at);
        qa_json_writer_key(&w, "scopes"); qa_json_writer_array(&w);
        for (size_t i = 0; i < p->scope_count; ++i) qa_json_writer_string(&w, p->scopes[i]);
        qa_json_writer_end(&w); qa_json_writer_end(&w);
    }
    qa_json_writer_end(&w); return save(s, "chatgpt.key", &w, error);
}
bool llm_other_save(qa_llm *s, const llm_other *other, qa_error *error) {
    qa_json_writer w = {0}; qa_json_writer_object(&w);
    qa_json_writer_key(&w, "baseUrl"); qa_json_writer_string(&w, other->base_url);
    qa_json_writer_key(&w, "model"); qa_json_writer_string(&w, other->model);
    qa_json_writer_key(&w, "transport"); qa_json_writer_string(&w, "openai-chat-completions"); qa_json_writer_end(&w);
    return save(s, "other.service", &w, error);
}
bool llm_other_prepare(const char *url, const char *model, llm_other *out, qa_error *error) {
    if (!url || !model || !out) return llm_fail(error, "invalid Other API connection");
    llm_other next = {0};
    if (!base_url(&next.storage, url, &next.base_url, error) ||
        !llm_model_text(&next.storage, (qa_bytes){(const uint8_t *)model, strlen(model)}, true, &next.model, error)) {
        qa_arena_destroy(&next.storage); return false;
    }
    *out = next; return true;
}
