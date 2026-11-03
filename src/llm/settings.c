#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/settings.h. GPL-2.0-or-later. */
#include "qa/llm/settings.h"
#include "qa/foundation/js.h"

#include "qa/llm/api.h"
#include "qa/llm/codex.h"
#include "qa/llm/session.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *qa_llm_provider_name(qa_llm_provider provider) {
  switch (provider) {
    case QA_LLM_PROVIDER_SUBSCRIPTION: return "chatgpt-subscription";
    case QA_LLM_PROVIDER_CHATGPT_API: return "chatgpt-api";
    case QA_LLM_PROVIDER_OTHER_API: return "other-api";
  }
  return "chatgpt-subscription";
}

bool qa_llm_provider_parse(const char *text, qa_llm_provider *out) {
  for (int i = 0; i < QA_LLM_PROVIDER_COUNT; i++) {
    if (strcmp(text, qa_llm_provider_name((qa_llm_provider)i)) == 0) {
      *out = (qa_llm_provider)i;
      return true;
    }
  }
  return false;
}

void qa_llm_other_service_clear(qa_llm_other_service *service) {
  free(service->base_url);
  free(service->model);
  memset(service, 0, sizeof *service);
}

const char *qa_llm_subscription_auth_status_name(qa_llm_subscription_auth_status status) {
  switch (status) {
    case QA_LLM_SUBSCRIPTION_AUTH_IDLE: return "idle";
    case QA_LLM_SUBSCRIPTION_AUTH_PENDING: return "pending";
    case QA_LLM_SUBSCRIPTION_AUTH_ERROR: return "error";
  }
  return "idle";
}

/* ---- persisted shapes ---- */

typedef struct preferences {
  qa_llm_provider provider;
  char *models[2]; /* chatgpt-subscription, chatgpt-api */
  char *efforts[QA_LLM_PROVIDER_COUNT]; /* NULL mirrors null */
} preferences;

typedef struct chatgpt_credentials {
  char *api_key; /* NULL mirrors null */
  bool has_subscription;
  qa_llm_subscription_credential subscription;
} chatgpt_credentials;

typedef struct file_error_entry {
  char *file;
  char *message;
} file_error_entry;

struct qa_llm_settings_service {
  char *base_directory;
  qa_llm_subscription_auth_options auth;
  qa_llm_request_options request_options;
  preferences preferences;
  chatgpt_credentials chatgpt;
  char *other_key;
  qa_llm_other_service other;
  qa_llm_subscription_auth_status auth_status;
  char *auth_message;
  qa_llm_abort_signal *active_controller; /* authenticate(), borrowed */
  qa_llm_abort_signal *request_controllers[16]; /* runRequest registry */
  size_t request_controller_count;
  bool closed;
  file_error_entry *errors;
  size_t error_count;
  size_t error_capacity;
  qa_llm_model_catalog catalogs[QA_LLM_PROVIDER_COUNT];
  int64_t catalog_generations[QA_LLM_PROVIDER_COUNT];
  bool has_catalog_generation[QA_LLM_PROVIDER_COUNT];
  qa_llm_session *session;
  char *fingerprint_storage;
};

static void preferences_clear(preferences *prefs) {
  for (int i = 0; i < 2; i++) free(prefs->models[i]);
  for (int i = 0; i < QA_LLM_PROVIDER_COUNT; i++) free(prefs->efforts[i]);
  memset(prefs, 0, sizeof *prefs);
}

static void chatgpt_clear(chatgpt_credentials *credentials) {
  free(credentials->api_key);
  if (credentials->has_subscription) qa_llm_subscription_credential_free(&credentials->subscription);
  memset(credentials, 0, sizeof *credentials);
}

static void errors_set(qa_llm_settings_service *service, const char *file, const char *message) {
  for (size_t i = 0; i < service->error_count; i++) {
    if (strcmp(service->errors[i].file, file) == 0) {
      free(service->errors[i].message);
      service->errors[i].message = qa_llm_strdup(message);
      return;
    }
  }
  if (service->error_count == service->error_capacity) {
    service->error_capacity = service->error_capacity == 0 ? 4 : service->error_capacity * 2;
    service->errors = realloc(service->errors, service->error_capacity * sizeof *service->errors);
    if (service->errors == NULL) qa_llm_oom();
  }
  service->errors[service->error_count].file = qa_llm_strdup(file);
  service->errors[service->error_count].message = qa_llm_strdup(message);
  service->error_count++;
}

static void errors_delete(qa_llm_settings_service *service, const char *file) {
  for (size_t i = 0; i < service->error_count; i++) {
    if (strcmp(service->errors[i].file, file) == 0) {
      free(service->errors[i].file);
      free(service->errors[i].message);
      memmove(service->errors + i, service->errors + i + 1, (service->error_count - i - 1) * sizeof *service->errors);
      service->error_count--;
      return;
    }
  }
}

/* ---- validation helpers (provider/model/apiKey/otherService) ---- */

static bool settings_provider(const qa_llm_json *value, qa_llm_provider *out, qa_llm_error *error) {
  if (value != NULL && value->kind == QA_LLM_JSON_STRING && qa_llm_provider_parse(value->string, out)) return true;
  qa_llm_error_set(error, "Invalid LLM provider setting.");
  return false;
}

static bool settings_model_text(const char *value, char **out, qa_llm_error *error) {
  bool bad = value == NULL || qa_llm_utf16_length(value) > 256; /* JS length: UTF-16 units */
  if (!bad) {
    for (const uint8_t *p = (const uint8_t *)value; *p != '\0'; p++) {
      if (*p <= 0x1f || *p == 0x7f) bad = true;
    }
  }
  if (bad) {
    qa_llm_error_set(error, "Invalid model name.");
    return false;
  }
  *out = qa_llm_trim_dup(value);
  return true;
}

static bool settings_model(const qa_llm_json *value, char **out, qa_llm_error *error) {
  if (value == NULL || value->kind != QA_LLM_JSON_STRING) {
    qa_llm_error_set(error, "Invalid model name.");
    return false;
  }
  return settings_model_text(value->string, out, error);
}

static bool settings_api_key_text(const char *value, char **out, qa_llm_error *error) {
  char *trimmed = value != NULL ? qa_llm_trim_dup(value) : NULL;
  bool bad = value == NULL || trimmed[0] == '\0' || qa_llm_utf16_length(value) > 16384;
  if (!bad) {
    /* /\s/.test(value.trim()): any JS WhiteSpace or LineTerminator code point. */
    for (const char *p = trimmed; *p != '\0'; p++) {
      if (qa_js_white_space_length(p) != 0) bad = true;
    }
  }
  if (bad) {
    free(trimmed);
    qa_llm_error_set(error, "Paste a nonempty API key without whitespace.");
    return false;
  }
  *out = trimmed;
  return true;
}

static bool settings_api_key(const qa_llm_json *value, char **out, qa_llm_error *error) {
  if (value == NULL || value->kind != QA_LLM_JSON_STRING) {
    qa_llm_error_set(error, "Paste a nonempty API key without whitespace.");
    return false;
  }
  return settings_api_key_text(value->string, out, error);
}

/* Minimal URL equivalent for otherService: enough of WHATWG URL to
 * reproduce protocol/hostname/username/search/hash and toString for
 * the http(s) forms this setting admits. */
typedef struct parsed_url {
  char *protocol; /* with trailing ':', lowercased */
  char *hostname; /* lowercased, brackets kept for IPv6 */
  char *port; /* "" when absent */
  char *path;
  bool has_username_or_password;
  bool has_search;
  bool has_hash;
} parsed_url;

static void parsed_url_clear(parsed_url *url) {
  free(url->protocol);
  free(url->hostname);
  free(url->port);
  free(url->path);
  memset(url, 0, sizeof *url);
}

static char *lower_dup_n(const char *text, size_t length) {
  char *out = qa_llm_strndup(text, length);
  for (size_t i = 0; i < length; i++) {
    if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
  }
  return out;
}

static bool parse_url(const char *text, parsed_url *out) {
  memset(out, 0, sizeof *out);
  const char *scheme_end = strstr(text, "://");
  if (scheme_end == NULL || scheme_end == text) return false;
  for (const char *p = text; p < scheme_end; p++) {
    bool valid = isalpha((uint8_t)*p) || (p > text && (isdigit((uint8_t)*p) || *p == '+' || *p == '-' || *p == '.'));
    if (!valid) return false;
  }
  if (!isalpha((uint8_t)text[0])) return false;
  out->protocol = lower_dup_n(text, (size_t)(scheme_end - text) + 1); /* includes ':' */
  const char *rest = scheme_end + 3;
  const char *authority_end = rest;
  while (*authority_end != '\0' && *authority_end != '/' && *authority_end != '?' && *authority_end != '#') authority_end++;
  const char *host_begin = rest;
  const char *at = NULL;
  for (const char *p = rest; p < authority_end; p++) {
    if (*p == '@') at = p;
  }
  if (at != NULL) {
    /* username[:password]@ — truthy when either side is nonempty. */
    out->has_username_or_password = at > rest;
    host_begin = at + 1;
  }
  if (host_begin >= authority_end) { parsed_url_clear(out); return false; }
  const char *port_begin = NULL;
  const char *host_end = authority_end;
  if (*host_begin == '[') {
    const char *close_bracket = memchr(host_begin, ']', (size_t)(authority_end - host_begin));
    if (close_bracket == NULL) { parsed_url_clear(out); return false; }
    host_end = close_bracket + 1;
    if (host_end < authority_end) {
      if (*host_end != ':') { parsed_url_clear(out); return false; }
      port_begin = host_end + 1;
    }
  } else {
    for (const char *p = host_begin; p < authority_end; p++) {
      if (*p == ':') { host_end = p; port_begin = p + 1; break; }
    }
  }
  if (host_end == host_begin) { parsed_url_clear(out); return false; }
  out->hostname = lower_dup_n(host_begin, (size_t)(host_end - host_begin));
  out->port = qa_llm_strdup("");
  if (port_begin != NULL && port_begin < authority_end) {
    for (const char *p = port_begin; p < authority_end; p++) {
      if (!isdigit((uint8_t)*p)) { parsed_url_clear(out); return false; }
    }
    char *port = qa_llm_strndup(port_begin, (size_t)(authority_end - port_begin));
    long value = strtol(port, NULL, 10);
    free(port);
    if (value > 65535) { parsed_url_clear(out); return false; }
    /* Default ports serialize to "" in WHATWG URL. */
    bool is_default = (strcmp(out->protocol, "https:") == 0 && value == 443) ||
                      (strcmp(out->protocol, "http:") == 0 && value == 80);
    if (!is_default) {
      free(out->port);
      qa_llm_strbuf buffer;
      qa_llm_strbuf_init(&buffer);
      qa_llm_strbuf_appendf(&buffer, "%ld", value);
      out->port = qa_llm_strbuf_steal(&buffer);
    }
  }
  const char *path_begin = authority_end;
  const char *path_end = path_begin;
  while (*path_end != '\0' && *path_end != '?' && *path_end != '#') path_end++;
  out->path = qa_llm_strndup(path_begin, (size_t)(path_end - path_begin));
  if (*path_end == '?') {
    const char *query_end = path_end + 1;
    while (*query_end != '\0' && *query_end != '#') query_end++;
    out->has_search = query_end > path_end + 1; /* url.search w/o bare '?' */
    path_end = query_end;
  }
  if (*path_end == '#') out->has_hash = path_end[1] != '\0';
  return true;
}

static char *url_to_string(const parsed_url *url) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_appendf(&out, "%s//%s", url->protocol, url->hostname);
  if (url->port[0] != '\0') qa_llm_strbuf_appendf(&out, ":%s", url->port);
  qa_llm_strbuf_append(&out, url->path[0] != '\0' ? url->path : "/");
  return qa_llm_strbuf_steal(&out);
}

static bool other_service_validate(const char *base_url, const char *model_value, const char *transport,
                                   qa_llm_other_service *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  if (transport == NULL || strcmp(transport, "openai-chat-completions") != 0 || base_url == NULL) {
    qa_llm_error_set(error, "Other API requires an OpenAI Chat Completions compatible service.");
    return false;
  }
  parsed_url url;
  if (!parse_url(base_url, &url)) {
    qa_llm_error_set(error, "Enter a valid Other API base URL.");
    return false;
  }
  bool loopback = strcmp(url.hostname, "localhost") == 0 || strcmp(url.hostname, "127.0.0.1") == 0 ||
                  strcmp(url.hostname, "[::1]") == 0;
  bool protocol_ok = strcmp(url.protocol, "https:") == 0 || (strcmp(url.protocol, "http:") == 0 && loopback);
  if (!protocol_ok || url.has_username_or_password || url.has_search || url.has_hash) {
    parsed_url_clear(&url);
    qa_llm_error_set(error, "Use HTTPS or loopback HTTP, without URL credentials, query, or fragment.");
    return false;
  }
  char *serialized = url_to_string(&url);
  parsed_url_clear(&url);
  size_t length = strlen(serialized);
  if (length > 0 && serialized[length - 1] == '/') serialized[length - 1] = '\0'; /* replace(/\/$/, "") */
  out->base_url = serialized;
  if (!settings_model_text(model_value, &out->model, error)) {
    qa_llm_other_service_clear(out);
    return false;
  }
  return true;
}

static bool other_service_from_json(const qa_llm_json *value, qa_llm_other_service *out, qa_llm_error *error) {
  if (!qa_llm_record(value, error)) return false;
  const qa_llm_json *transport = qa_llm_json_get(value, "transport");
  const qa_llm_json *base_url = qa_llm_json_get(value, "baseUrl");
  const qa_llm_json *model = qa_llm_json_get(value, "model");
  const char *transport_text = transport != NULL && transport->kind == QA_LLM_JSON_STRING ? transport->string : NULL;
  const char *base_text = base_url != NULL && base_url->kind == QA_LLM_JSON_STRING ? base_url->string : NULL;
  /* model() runs last in TS, after the URL checks. */
  qa_llm_other_service partial;
  if (!other_service_validate(base_text, "", transport_text, &partial, error)) return false;
  free(partial.model);
  partial.model = NULL;
  if (!settings_model(model, &partial.model, error)) {
    qa_llm_other_service_clear(&partial);
    return false;
  }
  *out = partial;
  return true;
}

static bool parse_json_text(const char *text, qa_llm_json **out, qa_llm_error *error) {
  if (!qa_llm_json_parse(text, strlen(text), out)) {
    qa_llm_error_set(error, "Invalid LLM settings file. Check its format before saving.");
    return false;
  }
  return true;
}

static const char *trim_start_ptr(const char *text) {
  size_t length; /* String.prototype.trimStart */
  while ((length = qa_js_white_space_length(text)) != 0) text += length;
  return text;
}

static bool parse_chatgpt(const char *text, chatgpt_credentials *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  if (text == NULL) return true;
  if (trim_start_ptr(text)[0] != '{') {
    return settings_api_key_text(text, &out->api_key, error);
  }
  qa_llm_json *value = NULL;
  if (!parse_json_text(text, &value, error)) return false;
  bool ok = qa_llm_record(value, error);
  if (ok) {
    const qa_llm_json *version = qa_llm_json_get(value, "version");
    if (version == NULL || version->kind != QA_LLM_JSON_NUMBER || version->number != 1) {
      qa_llm_error_set(error, "Unsupported chatgpt.key format.");
      ok = false;
    }
  }
  if (ok) {
    const qa_llm_json *api_key = qa_llm_json_get(value, "apiKey");
    if (api_key != NULL && api_key->kind != QA_LLM_JSON_NULL) ok = settings_api_key(api_key, &out->api_key, error);
  }
  if (ok) {
    const qa_llm_json *subscription = qa_llm_json_get(value, "subscription");
    if (subscription != NULL && subscription->kind != QA_LLM_JSON_NULL) {
      ok = qa_llm_parse_subscription(subscription, &out->subscription, error);
      out->has_subscription = ok;
    }
  }
  if (!ok) chatgpt_clear(out);
  qa_llm_json_free(value);
  return ok;
}

static bool parse_preferences(const char *text, preferences *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  out->provider = QA_LLM_PROVIDER_SUBSCRIPTION;
  if (text == NULL) {
    out->models[0] = qa_llm_strdup("");
    out->models[1] = qa_llm_strdup("");
    return true;
  }
  qa_llm_json *value = NULL;
  if (!parse_json_text(text, &value, error)) return false;
  bool ok = qa_llm_record(value, error);
  const qa_llm_json *models = NULL;
  const qa_llm_json *efforts = NULL;
  qa_llm_json *empty = NULL;
  if (ok) {
    models = qa_llm_json_get(value, "models");
    ok = qa_llm_record(models, error);
  }
  if (ok) {
    efforts = qa_llm_json_get(value, "reasoningEfforts");
    if (efforts == NULL || efforts->kind == QA_LLM_JSON_NULL) {
      empty = qa_llm_json_object();
      efforts = empty;
    }
    ok = qa_llm_record(efforts, error);
  }
  if (ok) {
    const qa_llm_json *version = qa_llm_json_get(value, "version");
    if (version == NULL || version->kind != QA_LLM_JSON_NUMBER || version->number != 1) {
      qa_llm_error_set(error, "Unsupported llm.json format.");
      ok = false;
    }
  }
  if (ok) ok = settings_provider(qa_llm_json_get(value, "provider"), &out->provider, error);
  if (ok) ok = settings_model(qa_llm_json_get(models, "chatgpt-subscription"), &out->models[0], error);
  if (ok) ok = settings_model(qa_llm_json_get(models, "chatgpt-api"), &out->models[1], error);
  for (int i = 0; ok && i < QA_LLM_PROVIDER_COUNT; i++) {
    ok = qa_llm_parse_reasoning_effort(qa_llm_json_get(efforts, qa_llm_provider_name((qa_llm_provider)i)), &out->efforts[i], error);
  }
  if (!ok) preferences_clear(out);
  qa_llm_json_free(empty);
  qa_llm_json_free(value);
  return ok;
}

static qa_llm_json *preferences_to_json(const preferences *prefs) {
  qa_llm_json *object = qa_llm_json_object();
  qa_llm_json_object_set(object, "version", qa_llm_json_number(1));
  qa_llm_json_object_set(object, "provider", qa_llm_json_string(qa_llm_provider_name(prefs->provider)));
  qa_llm_json *models = qa_llm_json_object();
  qa_llm_json_object_set(models, "chatgpt-subscription", qa_llm_json_string(prefs->models[0]));
  qa_llm_json_object_set(models, "chatgpt-api", qa_llm_json_string(prefs->models[1]));
  qa_llm_json_object_set(object, "models", models);
  qa_llm_json *efforts = qa_llm_json_object();
  for (int i = 0; i < QA_LLM_PROVIDER_COUNT; i++) {
    qa_llm_json_object_set(efforts, qa_llm_provider_name((qa_llm_provider)i),
                           prefs->efforts[i] != NULL ? qa_llm_json_string(prefs->efforts[i]) : qa_llm_json_null());
  }
  qa_llm_json_object_set(object, "reasoningEfforts", efforts);
  return object;
}

static qa_llm_json *chatgpt_to_json(const chatgpt_credentials *credentials) {
  qa_llm_json *object = qa_llm_json_object();
  qa_llm_json_object_set(object, "version", qa_llm_json_number(1));
  qa_llm_json_object_set(object, "apiKey",
                         credentials->api_key != NULL ? qa_llm_json_string(credentials->api_key) : qa_llm_json_null());
  qa_llm_json_object_set(object, "subscription",
                         credentials->has_subscription ? qa_llm_subscription_to_json(&credentials->subscription) : qa_llm_json_null());
  return object;
}

static qa_llm_json *other_to_json(const qa_llm_other_service *service) {
  qa_llm_json *object = qa_llm_json_object();
  qa_llm_json_object_set(object, "baseUrl", qa_llm_json_string(service->base_url));
  qa_llm_json_object_set(object, "model", qa_llm_json_string(service->model));
  qa_llm_json_object_set(object, "transport", qa_llm_json_string("openai-chat-completions"));
  return object;
}

static char *json_line(qa_llm_json *value) {
  char *text = qa_llm_json_stringify(value);
  qa_llm_json_free(value);
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_append(&out, text);
  qa_llm_strbuf_append_char(&out, '\n');
  free(text);
  return qa_llm_strbuf_steal(&out);
}

/* ---- file IO (load/write) ---- */

static char *resolve_path(const char *base, const char *name) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  qa_llm_strbuf_append(&out, base);
  if (out.length == 0 || out.data[out.length - 1] != '/') qa_llm_strbuf_append_char(&out, '/');
  qa_llm_strbuf_append(&out, name);
  return qa_llm_strbuf_steal(&out);
}

/* node:path resolve(): absolute path, dot segments normalized. */
static char *resolve_directory(const char *directory) {
  char *absolute;
  if (directory[0] == '/') {
    absolute = qa_llm_strdup(directory);
  } else {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd) == NULL) return qa_llm_strdup(directory);
    qa_llm_strbuf joined;
    qa_llm_strbuf_init(&joined);
    qa_llm_strbuf_appendf(&joined, "%s/%s", cwd, directory);
    absolute = qa_llm_strbuf_steal(&joined);
  }
  /* Normalize "." / ".." components in place. */
  char **parts = NULL;
  size_t count = 0, capacity = 0;
  char *copy = qa_llm_strdup(absolute);
  char *cursor = copy;
  while (*cursor != '\0') {
    while (*cursor == '/') cursor++;
    if (*cursor == '\0') break;
    char *begin = cursor;
    while (*cursor != '\0' && *cursor != '/') cursor++;
    size_t length = (size_t)(cursor - begin);
    if (length == 1 && begin[0] == '.') continue;
    if (length == 2 && begin[0] == '.' && begin[1] == '.') {
      if (count > 0) free(parts[--count]);
      continue;
    }
    if (count == capacity) {
      capacity = capacity == 0 ? 8 : capacity * 2;
      parts = realloc(parts, capacity * sizeof *parts);
      if (parts == NULL) qa_llm_oom();
    }
    parts[count++] = qa_llm_strndup(begin, length);
  }
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  for (size_t i = 0; i < count; i++) {
    qa_llm_strbuf_append_char(&out, '/');
    qa_llm_strbuf_append(&out, parts[i]);
    free(parts[i]);
  }
  free(parts);
  free(copy);
  free(absolute);
  if (out.length == 0) qa_llm_strbuf_append_char(&out, '/');
  return qa_llm_strbuf_steal(&out);
}

/* load(): false with error; *out_text NULL mirrors a missing file. */
static bool service_load(qa_llm_settings_service *service, const char *name, char **out_text, qa_llm_error *error) {
  *out_text = NULL;
  char *path = resolve_path(service->base_directory, name);
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    int saved = errno;
    free(path);
    if (saved == ENOENT) return true;
    qa_llm_error_setf(error, "Could not read %s.", name);
    return false;
  }
  qa_llm_bytes bytes;
  qa_llm_bytes_init(&bytes);
  uint8_t chunk[8192];
  size_t got;
  bool read_ok = true;
  while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) qa_llm_bytes_append(&bytes, chunk, got);
  if (ferror(file)) read_ok = false;
  fclose(file);
  if (!read_ok) {
    free(path);
    qa_llm_bytes_free(&bytes);
    qa_llm_error_setf(error, "Could not read %s.", name);
    return false;
  }
  /* Credentials copied in by older versions may be group- or world-readable. */
  (void)chmod(path, 0600);
  free(path);
  /* readFile utf8: invalid sequences become U+FFFD in Node; settings
   * files are ASCII JSON here, so bytes pass through as-is. */
  qa_llm_bytes_append(&bytes, "", 1);
  *out_text = (char *)bytes.data;
  return true;
}

/* editablePreferences(): a load failure propagates; a malformed llm.json is
 * replaced by the defaults for explicit preference changes. */
static bool editable_preferences(qa_llm_settings_service *service, preferences *out, qa_llm_error *error) {
  char *text = NULL;
  if (!service_load(service, "llm.json", &text, error)) return false;
  qa_llm_error ignored;
  bool parsed = parse_preferences(text, out, &ignored);
  free(text);
  if (!parsed) parse_preferences(NULL, out, &ignored);
  return true;
}

/* editableCredentials(): as editable_preferences, for chatgpt.key. */
static bool editable_credentials(qa_llm_settings_service *service, chatgpt_credentials *out, qa_llm_error *error) {
  char *text = NULL;
  if (!service_load(service, "chatgpt.key", &text, error)) return false;
  qa_llm_error ignored;
  bool parsed = parse_chatgpt(text, out, &ignored);
  free(text);
  if (!parsed) parse_chatgpt(NULL, out, &ignored);
  return true;
}

static bool mkdir_recursive(const char *path) {
  char *copy = qa_llm_strdup(path);
  bool ok = true;
  for (char *p = copy + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(copy, 0777) != 0 && errno != EEXIST) ok = false;
      *p = '/';
      if (!ok) break;
    }
  }
  if (ok && mkdir(copy, 0777) != 0 && errno != EEXIST) ok = false;
  free(copy);
  return ok;
}

/* write(): every failure becomes "Could not save <name>.". */
static bool service_write(qa_llm_settings_service *service, const char *name, const char *text,
                          qa_llm_abort_signal *signal, qa_llm_error *error) {
  char *target = resolve_path(service->base_directory, name);
  char uuid[37];
  qa_llm_random_uuid(uuid);
  qa_llm_strbuf temporary;
  qa_llm_strbuf_init(&temporary);
  qa_llm_strbuf_appendf(&temporary, "%s.%s.tmp", target, uuid);
  bool ok = false;
  int fd = -1;
  if (!mkdir_recursive(service->base_directory)) goto done;
  fd = open(temporary.data, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) goto done;
  {
    const char *data = text;
    size_t remaining = strlen(text);
    ok = true;
    while (remaining > 0) {
      ssize_t written = write(fd, data, remaining);
      if (written <= 0) { ok = false; break; }
      data += written;
      remaining -= (size_t)written;
    }
    if (close(fd) != 0) ok = false;
    fd = -1;
    if (!ok) goto done;
  }
  if (signal != NULL && qa_llm_signal_is_aborted(signal)) { ok = false; goto done; }
  if (rename(temporary.data, target) != 0) { ok = false; goto done; }
  errors_delete(service, name);
  ok = true;
done:
  if (fd >= 0) close(fd);
  unlink(temporary.data); /* rm(force) in finally */
  qa_llm_strbuf_free(&temporary);
  free(target);
  if (!ok) qa_llm_error_setf(error, "Could not save %s.", name);
  return ok;
}

/* mutate(): synchronous, so the queue is the call order itself. */
static bool check_mutable(qa_llm_settings_service *service, qa_llm_error *error) {
  if (service->closed) {
    qa_llm_error_set(error, "LLM settings are closed.");
    return false;
  }
  return true;
}

/* ---- construction / snapshot ---- */

static bool session_transport(void *ctx, const qa_llm_request_input *input, char **out_text, qa_llm_error *error);
static const char *session_fingerprint(void *ctx);
static bool request_direct(qa_llm_settings_service *service, const qa_llm_request_input *input,
                           char **out_text, qa_llm_error *error);

static bool provider_configured(const qa_llm_settings_service *service, qa_llm_provider provider) {
  switch (provider) {
    case QA_LLM_PROVIDER_SUBSCRIPTION: return service->chatgpt.has_subscription;
    case QA_LLM_PROVIDER_CHATGPT_API: return service->chatgpt.api_key != NULL;
    case QA_LLM_PROVIDER_OTHER_API: return service->other_key != NULL;
  }
  return false;
}

static const char *provider_model(const qa_llm_settings_service *service, qa_llm_provider provider) {
  switch (provider) {
    case QA_LLM_PROVIDER_SUBSCRIPTION: return service->preferences.models[0];
    case QA_LLM_PROVIDER_CHATGPT_API: return service->preferences.models[1];
    case QA_LLM_PROVIDER_OTHER_API: return service->other.model;
  }
  return "";
}

qa_llm_settings_service *qa_llm_settings_open(const qa_llm_settings_options *options) {
  qa_llm_settings_service *service = calloc(1, sizeof *service);
  if (service == NULL) qa_llm_oom();
  service->base_directory = resolve_directory(options->base_directory);
  service->auth = options->auth;
  service->request_options = options->request;
  qa_llm_error ignored;
  qa_llm_error_clear(&ignored);
  parse_preferences(NULL, &service->preferences, &ignored);
  parse_chatgpt(NULL, &service->chatgpt, &ignored);
  service->other.base_url = qa_llm_strdup("");
  service->other.model = qa_llm_strdup("");
  service->session = qa_llm_session_create(session_transport, service, session_fingerprint, service);
  static const char *const files[] = { "llm.json", "chatgpt.key", "other.key", "other.service" };
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
    const char *file = files[i];
    char *text = NULL;
    qa_llm_error error;
    qa_llm_error_clear(&error);
    bool ok = service_load(service, file, &text, &error);
    if (ok) {
      switch (i) {
        case 0: {
          preferences parsed;
          ok = parse_preferences(text, &parsed, &error);
          if (ok) { preferences_clear(&service->preferences); service->preferences = parsed; }
          break;
        }
        case 1: {
          chatgpt_credentials parsed;
          ok = parse_chatgpt(text, &parsed, &error);
          if (ok) { chatgpt_clear(&service->chatgpt); service->chatgpt = parsed; }
          break;
        }
        case 2:
          if (text != NULL) ok = settings_api_key_text(text, &service->other_key, &error);
          break;
        case 3:
          if (text != NULL) {
            qa_llm_json *value = NULL;
            ok = parse_json_text(text, &value, &error);
            if (ok) {
              qa_llm_other_service parsed;
              ok = other_service_from_json(value, &parsed, &error);
              if (ok) { qa_llm_other_service_clear(&service->other); service->other = parsed; }
            }
            qa_llm_json_free(value);
          }
          break;
      }
    }
    free(text);
    if (!ok) {
      qa_llm_strbuf message;
      qa_llm_strbuf_init(&message);
      qa_llm_strbuf_appendf(&message, "Could not load %s. Check its format and permissions.", file);
      errors_set(service, file, message.data);
      qa_llm_strbuf_free(&message);
    }
  }
  return service;
}

void qa_llm_settings_read(const qa_llm_settings_service *service, qa_llm_settings_snapshot *out) {
  memset(out, 0, sizeof *out);
  out->provider = service->preferences.provider;
  out->model = qa_llm_strdup(provider_model(service, out->provider));
  out->reasoning_effort = service->preferences.efforts[out->provider] != NULL
      ? qa_llm_strdup(service->preferences.efforts[out->provider]) : NULL;
  for (int i = 0; i < QA_LLM_PROVIDER_COUNT; i++) {
    out->reasoning_efforts[i] = service->preferences.efforts[i] != NULL ? qa_llm_strdup(service->preferences.efforts[i]) : NULL;
    qa_llm_model_catalog_copy(&service->catalogs[i], &out->catalogs[i]);
  }
  out->providers.subscription.configured = service->chatgpt.has_subscription;
  out->providers.subscription.model = qa_llm_strdup(service->preferences.models[0]);
  out->providers.subscription.has_expires_at = service->chatgpt.has_subscription;
  out->providers.subscription.expires_at = service->chatgpt.has_subscription ? service->chatgpt.subscription.expires_at : 0;
  out->providers.chatgpt_api.configured = service->chatgpt.api_key != NULL;
  out->providers.chatgpt_api.model = qa_llm_strdup(service->preferences.models[1]);
  out->providers.other_api.base_url = qa_llm_strdup(service->other.base_url);
  out->providers.other_api.model = qa_llm_strdup(service->other.model);
  out->providers.other_api.configured = service->other_key != NULL;
  out->subscription_auth = service->auth_status;
  out->subscription_auth_message = service->auth_message != NULL ? qa_llm_strdup(service->auth_message) : NULL;
  if (service->error_count > 0) {
    out->errors = calloc(service->error_count, sizeof *out->errors);
    if (out->errors == NULL) qa_llm_oom();
    for (size_t i = 0; i < service->error_count; i++) {
      out->errors[i].file = qa_llm_strdup(service->errors[i].file);
      out->errors[i].message = qa_llm_strdup(service->errors[i].message);
    }
    out->error_count = service->error_count;
  }
}

void qa_llm_settings_snapshot_free(qa_llm_settings_snapshot *snapshot) {
  free(snapshot->model);
  free(snapshot->reasoning_effort);
  for (int i = 0; i < QA_LLM_PROVIDER_COUNT; i++) {
    free(snapshot->reasoning_efforts[i]);
    qa_llm_model_catalog_free(&snapshot->catalogs[i]);
  }
  free(snapshot->providers.subscription.model);
  free(snapshot->providers.chatgpt_api.model);
  free(snapshot->providers.other_api.base_url);
  free(snapshot->providers.other_api.model);
  free(snapshot->subscription_auth_message);
  for (size_t i = 0; i < snapshot->error_count; i++) {
    free(snapshot->errors[i].file);
    free(snapshot->errors[i].message);
  }
  free(snapshot->errors);
  memset(snapshot, 0, sizeof *snapshot);
}

/* ---- mutations ---- */

static void invalidate_catalog(qa_llm_settings_service *service, qa_llm_provider selected) {
  service->catalog_generations[selected] = (service->has_catalog_generation[selected] ? service->catalog_generations[selected] : 0) + 1;
  service->has_catalog_generation[selected] = true;
  qa_llm_model_catalog_free(&service->catalogs[selected]);
  service->catalogs[selected].status = QA_LLM_CATALOG_IDLE;
}

/* modelMetadata(): owned copy, or false-found when undefined. */
static bool model_metadata(qa_llm_settings_service *service, qa_llm_provider selected, const char *name, qa_llm_model *out) {
  const qa_llm_model_list *models = &service->catalogs[selected].models;
  for (size_t i = 0; i < models->count; i++) {
    if (strcmp(models->items[i].id, name) == 0) {
      qa_llm_model_copy(&models->items[i], out);
      return true;
    }
  }
  if (selected == QA_LLM_PROVIDER_CHATGPT_API) {
    qa_llm_api_model(name, out);
    return true;
  }
  return false;
}

bool qa_llm_settings_select_provider(qa_llm_settings_service *service, qa_llm_provider selected, qa_llm_error *error) {
  if (selected != QA_LLM_PROVIDER_SUBSCRIPTION) qa_llm_settings_cancel_sign_in(service);
  if (!check_mutable(service, error)) return false;
  preferences current;
  if (!editable_preferences(service, &current, error)) return false;
  bool ok;
  qa_llm_provider previous = current.provider;
  current.provider = selected;
  char *line = json_line(preferences_to_json(&current));
  ok = service_write(service, "llm.json", line, NULL, error);
  free(line);
  if (!ok) { preferences_clear(&current); return false; }
  preferences_clear(&service->preferences);
  service->preferences = current;
  if (previous != selected) qa_llm_session_invalidate(service->session);
  return true;
}

bool qa_llm_settings_set_model(qa_llm_settings_service *service, qa_llm_provider selected,
                               const char *value, qa_llm_error *error) {
  if (!check_mutable(service, error)) return false;
  char *name = NULL;
  if (!settings_model_text(value, &name, error)) return false;
  bool ok = false;
  preferences current;
  memset(&current, 0, sizeof current);
  char *text = NULL;
  ok = editable_preferences(service, &current, error);
  if (!ok) goto done;
  if (service->catalogs[selected].status == QA_LLM_CATALOG_READY) {
    bool listed = false;
    const qa_llm_model_list *models = &service->catalogs[selected].models;
    for (size_t i = 0; i < models->count; i++) {
      if (strcmp(models->items[i].id, name) == 0) listed = true;
    }
    if (!listed) {
      qa_llm_error_set(error, "Choose a model from the loaded provider list.");
      ok = false;
      goto done;
    }
  }
  {
    qa_llm_model metadata;
    bool has_metadata = model_metadata(service, selected, name, &metadata);
    const char *effort = current.efforts[selected];
    char *next_effort = effort != NULL && has_metadata && qa_llm_model_has_effort(&metadata, effort) ? qa_llm_strdup(effort) : NULL;
    if (has_metadata) qa_llm_model_clear(&metadata);
    free(current.efforts[selected]);
    current.efforts[selected] = next_effort;
  }
  if (selected != QA_LLM_PROVIDER_OTHER_API) {
    int index = selected == QA_LLM_PROVIDER_SUBSCRIPTION ? 0 : 1;
    free(current.models[index]);
    current.models[index] = qa_llm_strdup(name);
  }
  if (selected == QA_LLM_PROVIDER_OTHER_API) {
    if (!service_load(service, "other.service", &text, error)) { ok = false; goto done; }
    qa_llm_other_service other;
    if (text == NULL) {
      memset(&other, 0, sizeof other);
      other.base_url = qa_llm_strdup(service->other.base_url);
      other.model = qa_llm_strdup(name);
      qa_llm_other_service validated;
      ok = other_service_validate(other.base_url, name, "openai-chat-completions", &validated, error);
      qa_llm_other_service_clear(&other);
      if (!ok) goto done;
      other = validated;
    } else {
      qa_llm_json *parsed = NULL;
      ok = parse_json_text(text, &parsed, error);
      free(text);
      text = NULL;
      if (!ok) { qa_llm_json_free(parsed); goto done; }
      qa_llm_other_service loaded;
      ok = other_service_from_json(parsed, &loaded, error);
      qa_llm_json_free(parsed);
      if (!ok) goto done;
      qa_llm_other_service validated;
      ok = other_service_validate(loaded.base_url, name, "openai-chat-completions", &validated, error);
      qa_llm_other_service_clear(&loaded);
      if (!ok) goto done;
      other = validated;
    }
    char *line = json_line(other_to_json(&other));
    ok = service_write(service, "other.service", line, NULL, error);
    free(line);
    if (!ok) { qa_llm_other_service_clear(&other); goto done; }
    qa_llm_other_service_clear(&service->other);
    service->other = other;
  }
  {
    char *line = json_line(preferences_to_json(&current));
    ok = service_write(service, "llm.json", line, NULL, error);
    free(line);
    if (!ok) goto done;
  }
  preferences_clear(&service->preferences);
  service->preferences = current;
  memset(&current, 0, sizeof current);
  if (selected == service->preferences.provider) qa_llm_session_invalidate(service->session);
  ok = true;
done:
  free(text);
  preferences_clear(&current);
  free(name);
  return ok;
}

bool qa_llm_settings_set_reasoning_effort(qa_llm_settings_service *service, qa_llm_provider selected,
                                          const char *value, qa_llm_error *error) {
  if (!check_mutable(service, error)) return false;
  char *effort = NULL;
  if (!qa_llm_parse_reasoning_effort_string(value, &effort, error)) return false;
  bool ok = false;
  preferences current;
  memset(&current, 0, sizeof current);
  char *text = NULL;
  ok = editable_preferences(service, &current, error);
  if (!ok) goto done;
  if (effort != NULL) {
    const char *name = selected == QA_LLM_PROVIDER_OTHER_API ? service->other.model
        : current.models[selected == QA_LLM_PROVIDER_SUBSCRIPTION ? 0 : 1];
    qa_llm_model metadata;
    bool has_metadata = model_metadata(service, selected, name, &metadata);
    bool supported = has_metadata && qa_llm_model_has_effort(&metadata, effort);
    if (has_metadata) qa_llm_model_clear(&metadata);
    if (!supported) {
      qa_llm_error_set(error, "Choose a reasoning effort supported by the selected model.");
      ok = false;
      goto done;
    }
  }
  {
    char *previous = current.efforts[selected];
    bool changed = (previous == NULL) != (effort == NULL) || (previous != NULL && effort != NULL && strcmp(previous, effort) != 0);
    current.efforts[selected] = effort;
    effort = NULL;
    char *line = json_line(preferences_to_json(&current));
    ok = service_write(service, "llm.json", line, NULL, error);
    free(line);
    free(previous);
    if (!ok) goto done;
    qa_llm_provider provider_after = current.provider;
    preferences_clear(&service->preferences);
    service->preferences = current;
    memset(&current, 0, sizeof current);
    if (selected == provider_after && changed) qa_llm_session_invalidate(service->session);
  }
  ok = true;
done:
  free(text);
  preferences_clear(&current);
  free(effort);
  return ok;
}

bool qa_llm_settings_save_other_service(qa_llm_settings_service *service, const char *base_url,
                                        const char *model, const char *transport, qa_llm_error *error) {
  if (!check_mutable(service, error)) return false;
  qa_llm_other_service next;
  if (!other_service_validate(base_url, model, transport, &next, error)) return false;
  bool connection_changed = strcmp(next.base_url, service->other.base_url) != 0;
  bool model_changed = strcmp(next.model, service->other.model) != 0;
  bool ok = true;
  if (!connection_changed && model_changed && service->catalogs[QA_LLM_PROVIDER_OTHER_API].status == QA_LLM_CATALOG_READY) {
    bool listed = false;
    const qa_llm_model_list *models = &service->catalogs[QA_LLM_PROVIDER_OTHER_API].models;
    for (size_t i = 0; i < models->count; i++) {
      if (strcmp(models->items[i].id, next.model) == 0) listed = true;
    }
    if (!listed) {
      qa_llm_error_set(error, "Choose a model from the loaded provider list.");
      qa_llm_other_service_clear(&next);
      return false;
    }
  }
  {
    char *line = json_line(other_to_json(&next));
    ok = service_write(service, "other.service", line, NULL, error);
    free(line);
  }
  if (!ok) { qa_llm_other_service_clear(&next); return false; }
  qa_llm_other_service_clear(&service->other);
  service->other = next;
  if (connection_changed) invalidate_catalog(service, QA_LLM_PROVIDER_OTHER_API);
  if (connection_changed || model_changed) {
    preferences current;
    if (!editable_preferences(service, &current, error)) return false;
    qa_llm_model metadata;
    bool has_metadata = model_metadata(service, QA_LLM_PROVIDER_OTHER_API, service->other.model, &metadata);
    const char *previous_effort = current.efforts[QA_LLM_PROVIDER_OTHER_API];
    char *effort = previous_effort != NULL && has_metadata && qa_llm_model_has_effort(&metadata, previous_effort)
        ? qa_llm_strdup(previous_effort) : NULL;
    if (has_metadata) qa_llm_model_clear(&metadata);
    free(current.efforts[QA_LLM_PROVIDER_OTHER_API]);
    current.efforts[QA_LLM_PROVIDER_OTHER_API] = effort;
    char *line = json_line(preferences_to_json(&current));
    ok = service_write(service, "llm.json", line, NULL, error);
    free(line);
    if (!ok) { preferences_clear(&current); return false; }
    preferences_clear(&service->preferences);
    service->preferences = current;
  }
  if (service->preferences.provider == QA_LLM_PROVIDER_OTHER_API && (connection_changed || model_changed)) {
    qa_llm_session_invalidate(service->session);
  }
  return true;
}

bool qa_llm_settings_save_api_key(qa_llm_settings_service *service, qa_llm_provider selected,
                                  const char *value, qa_llm_error *error) {
  if (!check_mutable(service, error)) return false;
  char *key = NULL;
  if (!settings_api_key_text(value, &key, error)) return false;
  bool ok = false;
  if (selected == QA_LLM_PROVIDER_OTHER_API) {
    qa_llm_strbuf line;
    qa_llm_strbuf_init(&line);
    qa_llm_strbuf_append(&line, key);
    qa_llm_strbuf_append_char(&line, '\n');
    ok = service_write(service, "other.key", line.data, NULL, error);
    qa_llm_strbuf_free(&line);
    if (ok) {
      free(service->other_key);
      service->other_key = key;
      key = NULL;
    }
  } else {
    char *text = NULL;
    if (service_load(service, "chatgpt.key", &text, error)) {
      chatgpt_credentials current;
      ok = parse_chatgpt(text, &current, error);
      free(text);
      if (ok) {
        free(current.api_key);
        current.api_key = key;
        key = NULL;
        char *line = json_line(chatgpt_to_json(&current));
        ok = service_write(service, "chatgpt.key", line, NULL, error);
        free(line);
        if (ok) {
          chatgpt_clear(&service->chatgpt);
          service->chatgpt = current;
        } else chatgpt_clear(&current);
      }
    }
  }
  free(key);
  if (!ok) return false;
  invalidate_catalog(service, selected);
  if (selected == service->preferences.provider) qa_llm_session_invalidate(service->session);
  return true;
}

bool qa_llm_settings_remove_credential(qa_llm_settings_service *service, qa_llm_provider selected, qa_llm_error *error) {
  if (selected == QA_LLM_PROVIDER_SUBSCRIPTION) qa_llm_settings_cancel_sign_in(service);
  if (!check_mutable(service, error)) return false;
  bool ok = false;
  if (selected == QA_LLM_PROVIDER_OTHER_API) {
    char *path = resolve_path(service->base_directory, "other.key");
    if (unlink(path) != 0 && errno != ENOENT) {
      qa_llm_error_set(error, "Could not remove other.key.");
      free(path);
      return false;
    }
    free(path);
    free(service->other_key);
    service->other_key = NULL;
    errors_delete(service, "other.key");
    ok = true;
  } else {
    chatgpt_credentials current;
    if (editable_credentials(service, &current, error)) {
      ok = true;
      {
        if (selected == QA_LLM_PROVIDER_CHATGPT_API) {
          free(current.api_key);
          current.api_key = NULL;
        } else if (current.has_subscription) {
          qa_llm_subscription_credential_free(&current.subscription);
          current.has_subscription = false;
        }
        char *line = json_line(chatgpt_to_json(&current));
        ok = service_write(service, "chatgpt.key", line, NULL, error);
        free(line);
        if (ok) {
          chatgpt_clear(&service->chatgpt);
          service->chatgpt = current;
        } else chatgpt_clear(&current);
      }
    }
  }
  if (!ok) return false;
  invalidate_catalog(service, selected);
  if (selected == service->preferences.provider) qa_llm_session_invalidate(service->session);
  return true;
}

/* ---- request runner, subscription freshness ---- */

/* runRequest scaffolding: registers the controller for close(). */
static bool run_request_begin(qa_llm_settings_service *service, qa_llm_abort_signal *caller,
                              double timeout_ms, qa_llm_abort_signal *controller, qa_llm_error *error) {
  qa_llm_signal_init(controller);
  if (qa_llm_signal_is_aborted(caller)) {
    qa_llm_error_set(error, "LLM request cancelled.");
    return false;
  }
  if (service->closed) {
    qa_llm_error_set(error, "LLM settings are closed.");
    return false;
  }
  controller->parent = caller;
  qa_llm_signal_set_timeout(controller, timeout_ms);
  if (qa_llm_signal_is_aborted(caller)) controller->aborted = true;
  if (service->request_controller_count < 16) {
    service->request_controllers[service->request_controller_count++] = controller;
  }
  return qa_llm_check_request_abort(controller, error);
}

static void run_request_end(qa_llm_settings_service *service, qa_llm_abort_signal *controller) {
  for (size_t i = 0; i < service->request_controller_count; i++) {
    if (service->request_controllers[i] == controller) {
      memmove(service->request_controllers + i, service->request_controllers + i + 1,
              (service->request_controller_count - i - 1) * sizeof *service->request_controllers);
      service->request_controller_count--;
      return;
    }
  }
}

/* runRequest's catch: timeout wins, then cancellation, then the
 * LlmSettingsError rethrows (every error here), else the generic. */
static void run_request_map_error(qa_llm_abort_signal *controller, qa_llm_error *error) {
  (void)qa_llm_signal_is_aborted(controller);
  if (controller->timed_out) {
    qa_llm_error_set(error, "LLM request timed out. Try a shorter request or check the service.");
    return;
  }
  qa_llm_error abort_error;
  if (!qa_llm_check_request_abort(controller, &abort_error)) {
    *error = abort_error;
    return;
  }
  if (error->message[0] == '\0') qa_llm_error_set(error, "LLM request failed. Check the selected service and try again.");
}

static bool load_subscription(qa_llm_settings_service *service, qa_llm_subscription_credential *out, qa_llm_error *error) {
  char *text = NULL;
  if (!service_load(service, "chatgpt.key", &text, error)) return false;
  chatgpt_credentials parsed;
  bool ok = parse_chatgpt(text, &parsed, error);
  free(text);
  if (!ok) return false;
  if (!parsed.has_subscription) {
    chatgpt_clear(&parsed);
    qa_llm_error_set(error, "Sign in to ChatGPT Subscription in LLM options first.");
    return false;
  }
  *out = parsed.subscription;
  parsed.has_subscription = false;
  chatgpt_clear(&parsed);
  return true;
}

static bool refresh_exchange(qa_llm_settings_service *service, qa_llm_abort_signal *signal,
                             const char *rejected_access_token, qa_llm_subscription_credential *out, qa_llm_error *error);

static bool fresh_subscription(qa_llm_settings_service *service, qa_llm_abort_signal *caller,
                               const char *rejected_access_token, qa_llm_subscription_credential *out, qa_llm_error *error) {
  if (!qa_llm_check_request_abort(caller, error)) return false;
  qa_llm_subscription_credential credential;
  if (!load_subscription(service, &credential, error)) return false;
  double now = (double)qa_llm_now_ms();
  if ((rejected_access_token != NULL && strcmp(credential.access_token, rejected_access_token) != 0) ||
      (rejected_access_token == NULL && credential.expires_at > now + 60000)) {
    *out = credential;
    return true;
  }
  qa_llm_subscription_credential_free(&credential);
  /* The token exchange outlives caller cancellation: the provider may already
   * have rotated the refresh token, so the result must be saved. It runs on its
   * own controller (close() aborts it) with the request timeout; the caller
   * only stops waiting (withRequestAbort). */
  qa_llm_abort_signal controller;
  qa_llm_signal_init(&controller);
  qa_llm_signal_set_timeout(&controller, service->request_options.has_timeout
                                             ? service->request_options.timeout_ms
                                             : 120000);
  bool registered = service->request_controller_count < 16;
  if (registered) service->request_controllers[service->request_controller_count++] = &controller;
  qa_llm_subscription_credential result;
  qa_llm_error exchange_error;
  memset(&exchange_error, 0, sizeof exchange_error);
  bool ok = refresh_exchange(service, &controller, rejected_access_token, &result, &exchange_error);
  if (registered) run_request_end(service, &controller);
  if (qa_llm_signal_is_aborted(caller)) {
    if (ok) qa_llm_subscription_credential_free(&result);
    qa_llm_error_set(error, "LLM request cancelled.");
    return false;
  }
  if (!ok) {
    *error = exchange_error;
    return false;
  }
  *out = result;
  return true;
}

/* The shared refresh promise body: with synchronous callers the re-check
 * below is what makes concurrent turns share one refresh. */
static bool refresh_exchange(qa_llm_settings_service *service, qa_llm_abort_signal *signal,
                             const char *rejected_access_token, qa_llm_subscription_credential *out, qa_llm_error *error) {
  double now;
  qa_llm_subscription_credential before;
  if (!load_subscription(service, &before, error)) return false;
  if (!qa_llm_check_request_abort(signal, error)) { qa_llm_subscription_credential_free(&before); return false; }
  now = (double)qa_llm_now_ms();
  if ((rejected_access_token != NULL && strcmp(before.access_token, rejected_access_token) != 0) ||
      (rejected_access_token == NULL && before.expires_at > now + 60000)) {
    *out = before;
    return true;
  }
  if (service->closed) {
    /* mutate() after close rejects; the refresh itself runs first in
     * TS only while a request holds it. Cancellation covers the rest. */
  }
  qa_llm_subscription_credential refreshed;
  bool ok = qa_llm_refresh_subscription(&service->auth, &before, signal, &refreshed, error);
  if (!ok) { qa_llm_subscription_credential_free(&before); return false; }
  if (!qa_llm_check_request_abort(signal, error)) {
    qa_llm_subscription_credential_free(&before);
    qa_llm_subscription_credential_free(&refreshed);
    return false;
  }
  /* mutate(): persist unless the stored credential moved on. */
  if (!check_mutable(service, error)) {
    qa_llm_subscription_credential_free(&before);
    qa_llm_subscription_credential_free(&refreshed);
    return false;
  }
  char *text = NULL;
  bool done = false;
  if (service_load(service, "chatgpt.key", &text, error)) {
    chatgpt_credentials latest;
    bool parsed_ok = parse_chatgpt(text, &latest, error);
    free(text);
    if (parsed_ok) {
      if (!latest.has_subscription) {
        qa_llm_error_set(error, "Subscription credential was removed. Sign in again.");
        chatgpt_clear(&latest);
      } else if (strcmp(latest.subscription.refresh_token, before.refresh_token) != 0 ||
                 strcmp(latest.subscription.access_token, before.access_token) != 0) {
        *out = latest.subscription;
        latest.has_subscription = false;
        qa_llm_subscription_credential_free(&refreshed);
        chatgpt_clear(&latest);
        done = true;
      } else {
        qa_llm_subscription_credential_free(&latest.subscription);
        latest.subscription = refreshed;
        latest.has_subscription = true;
        memset(&refreshed, 0, sizeof refreshed);
        char *line = json_line(chatgpt_to_json(&latest));
        bool wrote = service_write(service, "chatgpt.key", line, signal, error);
        free(line);
        if (wrote) {
          qa_llm_subscription_credential result = latest.subscription;
          latest.has_subscription = false;
          chatgpt_clear(&service->chatgpt);
          service->chatgpt = latest;
          service->chatgpt.has_subscription = true;
          service->chatgpt.subscription = result;
          /* Hand the caller its own copy by re-parsing ownership:
           * service keeps one clone, caller takes the original. */
          qa_llm_subscription_credential clone;
          memset(&clone, 0, sizeof clone);
          clone.access_token = qa_llm_strdup(result.access_token);
          clone.refresh_token = qa_llm_strdup(result.refresh_token);
          clone.token_type = qa_llm_strdup(result.token_type);
          clone.expires_at = result.expires_at;
          if (result.scope_count > 0) {
            clone.scopes = calloc(result.scope_count, sizeof *clone.scopes);
            if (clone.scopes == NULL) qa_llm_oom();
            for (size_t i = 0; i < result.scope_count; i++) clone.scopes[i] = qa_llm_strdup(result.scopes[i]);
            clone.scope_count = result.scope_count;
          }
          service->chatgpt.subscription = clone;
          *out = result;
          done = true;
        } else {
          chatgpt_clear(&latest);
        }
      }
    }
  }
  qa_llm_subscription_credential_free(&before);
  qa_llm_subscription_credential_free(&refreshed);
  return done;
}

bool qa_llm_settings_refresh_subscription(qa_llm_settings_service *service, qa_llm_error *error) {
  qa_llm_abort_signal controller;
  double timeout = service->request_options.has_timeout ? service->request_options.timeout_ms : 120000;
  if (!run_request_begin(service, NULL, timeout, &controller, error)) {
    run_request_map_error(&controller, error);
    return false;
  }
  qa_llm_subscription_credential current;
  bool ok = load_subscription(service, &current, error);
  if (ok) {
    qa_llm_subscription_credential fresh;
    ok = fresh_subscription(service, &controller, current.access_token, &fresh, error);
    if (ok) qa_llm_subscription_credential_free(&fresh);
    qa_llm_subscription_credential_free(&current);
  }
  run_request_end(service, &controller);
  if (!ok) run_request_map_error(&controller, error);
  return ok;
}

/* ---- model refresh ---- */

bool qa_llm_settings_refresh_models(qa_llm_settings_service *service, qa_llm_provider selected,
                                    qa_llm_abort_signal *caller, qa_llm_model_list *out_models, qa_llm_error *error) {
  memset(out_models, 0, sizeof *out_models);
  int64_t generation = (service->has_catalog_generation[selected] ? service->catalog_generations[selected] : 0) + 1;
  service->catalog_generations[selected] = generation;
  service->has_catalog_generation[selected] = true;
  /* catalogs[selected] = { status: "loading", models: previous } */
  service->catalogs[selected].status = QA_LLM_CATALOG_LOADING;
  free(service->catalogs[selected].message);
  service->catalogs[selected].message = NULL;

  qa_llm_abort_signal controller;
  double requested_timeout = service->request_options.has_timeout ? service->request_options.timeout_ms : 30000;
  double timeout = requested_timeout < 30000 ? requested_timeout : 30000;
  qa_llm_model_list models;
  memset(&models, 0, sizeof models);
  bool ok = run_request_begin(service, caller, timeout, &controller, error);
  if (ok) {
    qa_llm_fetch_ref fetcher = service->request_options.fetch;
    if (selected == QA_LLM_PROVIDER_SUBSCRIPTION) {
      qa_llm_subscription_credential credential;
      ok = fresh_subscription(service, &controller, NULL, &credential, error);
      if (ok) {
        ok = qa_llm_discover_codex_models(&credential, fetcher, &controller, &models, error);
        if (!ok && (qa_llm_error_is_http(error, 401) || qa_llm_error_is_http(error, 403))) {
          qa_llm_subscription_credential refreshed;
          ok = fresh_subscription(service, &controller, credential.access_token, &refreshed, error);
          if (ok) {
            ok = qa_llm_discover_codex_models(&refreshed, fetcher, &controller, &models, error);
            qa_llm_subscription_credential_free(&refreshed);
          }
        }
        qa_llm_subscription_credential_free(&credential);
      }
    } else {
      char *key = NULL;
      if (selected == QA_LLM_PROVIDER_CHATGPT_API) {
        char *text = NULL;
        if (service_load(service, "chatgpt.key", &text, error)) {
          chatgpt_credentials parsed;
          ok = parse_chatgpt(text, &parsed, error);
          free(text);
          if (ok) {
            key = parsed.api_key;
            parsed.api_key = NULL;
            chatgpt_clear(&parsed);
          }
        } else ok = false;
      } else {
        ok = service_load(service, "other.key", &key, error);
        if (ok && key != NULL) {
          /* The raw file text goes through apiKey() below, as in TS. */
        }
      }
      if (ok && key == NULL) {
        qa_llm_error_set(error, "Paste an API key before loading models.");
        ok = false;
      }
      if (ok) {
        qa_llm_other_service other;
        memset(&other, 0, sizeof other);
        bool has_other = false;
        if (selected == QA_LLM_PROVIDER_OTHER_API) {
          char *text = NULL;
          if (service_load(service, "other.service", &text, error)) {
            const char *json_text = text != NULL ? text : "{}";
            qa_llm_json *value = NULL;
            ok = parse_json_text(json_text, &value, error);
            if (ok) {
              ok = other_service_from_json(value, &other, error);
              has_other = ok;
            }
            qa_llm_json_free(value);
          } else ok = false;
          free(text);
        }
        if (ok) {
          char *validated_key = NULL;
          ok = settings_api_key_text(key, &validated_key, error);
          if (ok) {
            const char *base_url = has_other ? other.base_url : "https://api.openai.com/v1";
            ok = qa_llm_discover_api_models(base_url, validated_key, selected == QA_LLM_PROVIDER_CHATGPT_API,
                                            fetcher, &controller, &models, error);
            free(validated_key);
          }
        }
        if (has_other) qa_llm_other_service_clear(&other);
      }
      free(key);
    }
    if (ok) ok = qa_llm_check_request_abort(&controller, error);
    if (ok) {
      if (!service->has_catalog_generation[selected] || service->catalog_generations[selected] != generation) {
        /* Superseded: return models without touching the catalog. */
        run_request_end(service, &controller);
        *out_models = models;
        return true;
      }
      qa_llm_model_list_free(&service->catalogs[selected].models);
      qa_llm_model_list_copy(&models, &service->catalogs[selected].models);
      service->catalogs[selected].status = QA_LLM_CATALOG_READY;
      /* mutate(): persist the recommended default / drop stale effort. */
      if (check_mutable(service, error) &&
          service->has_catalog_generation[selected] && service->catalog_generations[selected] == generation &&
          qa_llm_check_request_abort(&controller, error)) {
        preferences current;
        if (editable_preferences(service, &current, error)) {
          {
            const char *name = selected == QA_LLM_PROVIDER_OTHER_API ? service->other.model
                : current.models[selected == QA_LLM_PROVIDER_SUBSCRIPTION ? 0 : 1];
            const qa_llm_model *selected_model = NULL;
            const qa_llm_model *default_model = NULL;
            for (size_t i = 0; i < models.count; i++) {
              if (strcmp(models.items[i].id, name) == 0) selected_model = &models.items[i];
            }
            if (selected_model == NULL && name[0] == '\0') {
              for (size_t i = 0; i < models.count; i++) {
                if (models.items[i].recommended) selected_model = &models.items[i];
              }
            }
            if (name[0] == '\0') default_model = selected_model;
            const char *old_effort = current.efforts[selected];
            const char *effort = old_effort != NULL && (selected_model == NULL || !qa_llm_model_has_effort(selected_model, old_effort))
                ? NULL : old_effort;
            bool effort_changed = effort != old_effort;
            if (default_model != NULL || effort_changed) {
              if (effort_changed) {
                free(current.efforts[selected]);
                current.efforts[selected] = NULL;
              }
              if (default_model != NULL && selected != QA_LLM_PROVIDER_OTHER_API) {
                int index = selected == QA_LLM_PROVIDER_SUBSCRIPTION ? 0 : 1;
                free(current.models[index]);
                current.models[index] = qa_llm_strdup(default_model->id);
              }
              bool persisted = true;
              if (default_model != NULL && selected == QA_LLM_PROVIDER_OTHER_API) {
                qa_llm_other_service other;
                memset(&other, 0, sizeof other);
                other.base_url = qa_llm_strdup(service->other.base_url);
                other.model = qa_llm_strdup(default_model->id);
                char *line = json_line(other_to_json(&other));
                persisted = service_write(service, "other.service", line, &controller, error);
                free(line);
                if (persisted) {
                  qa_llm_other_service_clear(&service->other);
                  service->other = other;
                } else qa_llm_other_service_clear(&other);
              }
              if (persisted) {
                char *line = json_line(preferences_to_json(&current));
                persisted = service_write(service, "llm.json", line, &controller, error);
                free(line);
                if (persisted) {
                  preferences_clear(&service->preferences);
                  service->preferences = current;
                  memset(&current, 0, sizeof current);
                }
              }
              if (!persisted) ok = false;
            }
            preferences_clear(&current);
          }
        } else ok = false;
      }
    }
  }
  run_request_end(service, &controller);
  if (!ok) {
    qa_llm_model_list_free(&models);
    run_request_map_error(&controller, error);
    if (service->has_catalog_generation[selected] && service->catalog_generations[selected] == generation) {
      if (qa_llm_signal_is_aborted(caller) || service->closed) {
        service->catalogs[selected].status = QA_LLM_CATALOG_IDLE;
      } else {
        service->catalogs[selected].status = QA_LLM_CATALOG_ERROR;
        free(service->catalogs[selected].message);
        service->catalogs[selected].message = qa_llm_strdup(error->message[0] != '\0' ? error->message : "Could not load models. Try Refresh models.");
      }
    }
    return false;
  }
  qa_llm_model_list_copy(&service->catalogs[selected].models, out_models);
  qa_llm_model_list_free(&models);
  return true;
}

/* ---- subscription sign-in (authenticate) ---- */

bool qa_llm_settings_sign_in_subscription(qa_llm_settings_service *service, qa_llm_error *error) {
  if (service->closed) {
    qa_llm_error_set(error, "LLM settings are closed.");
    return false;
  }
  if (service->active_controller != NULL && !qa_llm_signal_is_aborted(service->active_controller)) {
    /* TS returns the in-flight promise; synchronous C cannot join it
     * from a reentrant callback, so the shared attempt reports true. */
    return true;
  }
  qa_llm_abort_signal controller;
  qa_llm_signal_init(&controller);
  service->active_controller = &controller;
  service->auth_status = QA_LLM_SUBSCRIPTION_AUTH_PENDING;
  free(service->auth_message);
  service->auth_message = NULL;

  qa_llm_subscription_credential credential;
  qa_llm_error login_error;
  qa_llm_error_clear(&login_error);
  bool ok = qa_llm_check_abort(&controller, &login_error) &&
            qa_llm_login_subscription(&service->auth, &controller, &credential, &login_error);
  bool saved = false;
  qa_llm_error save_error;
  qa_llm_error_clear(&save_error);
  if (ok) {
    ok = qa_llm_check_abort(&controller, &save_error);
    if (ok && check_mutable(service, &save_error)) {
      ok = qa_llm_check_abort(&controller, &save_error);
      char *text = NULL;
      if (ok && service_load(service, "chatgpt.key", &text, &save_error)) {
        chatgpt_credentials current;
        ok = parse_chatgpt(text, &current, &save_error);
        free(text);
        if (ok) {
          if (current.has_subscription) qa_llm_subscription_credential_free(&current.subscription);
          current.subscription = credential;
          current.has_subscription = true;
          memset(&credential, 0, sizeof credential);
          char *line = json_line(chatgpt_to_json(&current));
          ok = service_write(service, "chatgpt.key", line, &controller, &save_error);
          free(line);
          if (ok) {
            chatgpt_clear(&service->chatgpt);
            service->chatgpt = current;
            saved = true;
            invalidate_catalog(service, QA_LLM_PROVIDER_SUBSCRIPTION);
            if (service->preferences.provider == QA_LLM_PROVIDER_SUBSCRIPTION) qa_llm_session_invalidate(service->session);
          } else chatgpt_clear(&current);
        }
      } else ok = false;
    }
    qa_llm_subscription_credential_free(&credential);
    if (!saved && ok) ok = false;
    if (!ok && save_error.message[0] != '\0') login_error = save_error;
  }
  bool aborted = qa_llm_signal_is_aborted(&controller);
  bool result;
  if (ok && saved) {
    if (service->active_controller == &controller) {
      service->auth_status = QA_LLM_SUBSCRIPTION_AUTH_IDLE;
    }
    result = true;
  } else if (aborted) {
    result = true; /* authenticate() resolves on cancellation */
  } else {
    const char *message = login_error.message[0] != '\0' ? login_error.message : "Subscription sign-in failed. Try again.";
    service->auth_status = QA_LLM_SUBSCRIPTION_AUTH_ERROR;
    free(service->auth_message);
    service->auth_message = qa_llm_strdup(message);
    qa_llm_error_set(error, message);
    result = false;
  }
  if (service->active_controller == &controller) service->active_controller = NULL;
  return result;
}

void qa_llm_settings_cancel_sign_in(qa_llm_settings_service *service) {
  if (service->active_controller != NULL) qa_llm_signal_abort(service->active_controller);
  service->auth_status = QA_LLM_SUBSCRIPTION_AUTH_IDLE;
  free(service->auth_message);
  service->auth_message = NULL;
}

/* ---- session wiring, requests ---- */

static const char *session_fingerprint(void *ctx) {
  qa_llm_settings_service *service = ctx;
  /* JSON.stringify([provider, model, effort, configured, other base]) */
  qa_llm_provider provider = service->preferences.provider;
  qa_llm_json *array = qa_llm_json_array();
  qa_llm_json_array_push(array, qa_llm_json_string(qa_llm_provider_name(provider)));
  qa_llm_json_array_push(array, qa_llm_json_string(provider_model(service, provider)));
  const char *effort = service->preferences.efforts[provider];
  qa_llm_json_array_push(array, effort != NULL ? qa_llm_json_string(effort) : qa_llm_json_null());
  qa_llm_json_array_push(array, qa_llm_json_bool(provider_configured(service, provider)));
  qa_llm_json_array_push(array, qa_llm_json_string(provider == QA_LLM_PROVIDER_OTHER_API ? service->other.base_url : ""));
  char *text = qa_llm_json_stringify(array);
  qa_llm_json_free(array);
  free(service->fingerprint_storage);
  service->fingerprint_storage = text;
  return text;
}

static bool session_transport(void *ctx, const qa_llm_request_input *input, char **out_text, qa_llm_error *error) {
  return request_direct((qa_llm_settings_service *)ctx, input, out_text, error);
}

const char *qa_llm_settings_session_id(const qa_llm_settings_service *service) {
  return qa_llm_session_id(service->session);
}

bool qa_llm_settings_prime(qa_llm_settings_service *service, const char *instructions, qa_llm_error *error) {
  qa_llm_provider provider = service->preferences.provider;
  const char *model = provider_model(service, provider);
  if (service->closed || !provider_configured(service, provider) || qa_llm_trim_is_empty(model)) return true;
  return qa_llm_session_prime(service->session, instructions, error);
}

bool qa_llm_settings_request(qa_llm_settings_service *service, const qa_llm_request_input *input,
                             char **out_text, qa_llm_error *error) {
  if (input->session_instructions != NULL || qa_llm_session_active(service->session)) {
    return qa_llm_session_request(service->session, input, out_text, error);
  }
  return request_direct(service, input, out_text, error);
}

static bool request_direct(qa_llm_settings_service *service, const qa_llm_request_input *input,
                           char **out_text, qa_llm_error *error) {
  qa_llm_abort_signal controller;
  double timeout = service->request_options.has_timeout ? service->request_options.timeout_ms : 120000;
  qa_llm_abort_signal *caller = input->signal;
  bool ok = run_request_begin(service, caller, timeout, &controller, error);
  qa_llm_other_service other;
  memset(&other, 0, sizeof other);
  bool has_other = false;
  preferences prefs;
  memset(&prefs, 0, sizeof prefs);
  bool has_prefs = false;
  if (ok) {
    char *text = NULL;
    ok = service_load(service, "llm.json", &text, error);
    if (ok) {
      ok = parse_preferences(text, &prefs, error);
      has_prefs = ok;
      free(text);
    }
  }
  if (ok && qa_llm_trim_is_empty(input->prompt)) {
    qa_llm_error_set(error, "Enter a question or command request.");
    ok = false;
  }
  qa_llm_provider selected = has_prefs ? prefs.provider : QA_LLM_PROVIDER_SUBSCRIPTION;
  if (ok && selected == QA_LLM_PROVIDER_OTHER_API) {
    char *text = NULL;
    ok = service_load(service, "other.service", &text, error);
    if (ok) {
      qa_llm_json *value = NULL;
      ok = parse_json_text(text != NULL ? text : "{}", &value, error);
      if (ok) {
        ok = other_service_from_json(value, &other, error);
        has_other = ok;
      }
      qa_llm_json_free(value);
      free(text);
    }
  }
  const char *selected_model = "";
  if (ok) {
    selected_model = selected == QA_LLM_PROVIDER_OTHER_API ? other.model
        : prefs.models[selected == QA_LLM_PROVIDER_SUBSCRIPTION ? 0 : 1];
    if (selected_model[0] == '\0') {
      qa_llm_error_set(error, "Choose a Model in LLM options before sending a request.");
      ok = false;
    }
  }
  char *selected_model_copy = NULL;
  const char *selected_effort = NULL;
  char *fastest_copy = NULL;
  if (ok) {
    selected_model_copy = qa_llm_strdup(selected_model);
    selected_effort = prefs.efforts[selected];
    if (selected_effort != NULL) {
      qa_llm_model metadata;
      bool has_metadata = model_metadata(service, selected, selected_model_copy, &metadata);
      if (has_metadata) qa_llm_model_clear(&metadata);
      if (!has_metadata) {
        qa_llm_model_list refreshed;
        ok = qa_llm_settings_refresh_models(service, selected, &controller, &refreshed, error);
        qa_llm_model_list_free(&refreshed);
      }
    }
  }
  if (ok) {
    qa_llm_model metadata;
    bool has_metadata = model_metadata(service, selected, selected_model_copy, &metadata);
    const char *effort = selected_effort;
    if (selected_effort != NULL && !(has_metadata && qa_llm_model_has_effort(&metadata, selected_effort))) {
      qa_llm_error_set(error, "The selected reasoning effort is not supported by this model. Choose a supported effort or Model default in LLM options.");
      ok = false;
    }
    if (ok && effort == NULL && has_metadata) {
      const char *fastest = qa_llm_fastest_reasoning_effort(&metadata);
      if (fastest != NULL) fastest_copy = qa_llm_strdup(fastest);
    }
    if (has_metadata) qa_llm_model_clear(&metadata);
    if (ok) {
      qa_llm_transport_request request;
      memset(&request, 0, sizeof request);
      request.input.prompt = input->prompt;
      request.input.instructions = input->instructions;
      request.input.session_id = input->session_id;
      request.input.signal = &controller;
      request.input.on_text = input->on_text;
      request.input.on_text_ctx = input->on_text_ctx;
      request.model = selected_model_copy;
      request.reasoning_effort = selected_effort != NULL ? selected_effort : fastest_copy;
      qa_llm_fetch_ref fetcher = service->request_options.fetch;
      if (selected == QA_LLM_PROVIDER_SUBSCRIPTION) {
        qa_llm_subscription_credential credential;
        ok = fresh_subscription(service, &controller, NULL, &credential, error);
        if (ok) {
          ok = qa_llm_request_codex(&request, &credential, fetcher, out_text, error);
          if (!ok && (qa_llm_error_is_http(error, 401) || qa_llm_error_is_http(error, 403))) {
            qa_llm_subscription_credential refreshed;
            ok = fresh_subscription(service, &controller, credential.access_token, &refreshed, error);
            if (ok) {
              ok = qa_llm_request_codex(&request, &refreshed, fetcher, out_text, error);
              qa_llm_subscription_credential_free(&refreshed);
            }
          }
          qa_llm_subscription_credential_free(&credential);
        }
      } else {
        char *key = NULL;
        if (selected == QA_LLM_PROVIDER_CHATGPT_API) {
          char *text = NULL;
          if (service_load(service, "chatgpt.key", &text, error)) {
            chatgpt_credentials parsed;
            ok = parse_chatgpt(text, &parsed, error);
            free(text);
            if (ok) {
              key = parsed.api_key;
              parsed.api_key = NULL;
              chatgpt_clear(&parsed);
            }
          } else ok = false;
        } else {
          ok = service_load(service, "other.key", &key, error);
        }
        if (ok && key == NULL) {
          qa_llm_error_set(error, "Paste an API key for the selected provider in LLM options first.");
          ok = false;
        }
        if (ok) {
          char *validated_key = NULL;
          if (settings_api_key_text(key, &validated_key, error)) {
            if (selected == QA_LLM_PROVIDER_CHATGPT_API) {
              ok = qa_llm_request_open_ai_responses(&request, validated_key, fetcher, out_text, error);
            } else if (!has_other) {
              qa_llm_error_set(error, "Configure Other API before sending a request.");
              ok = false;
            } else {
              qa_llm_api_credential credential = { validated_key, other.base_url };
              ok = qa_llm_request_chat_completions(&request, &credential, fetcher, out_text, error);
            }
            free(validated_key);
          } else ok = false;
        }
        free(key);
      }
    }
  }
  free(fastest_copy);
  free(selected_model_copy);
  if (has_prefs) preferences_clear(&prefs);
  if (has_other) qa_llm_other_service_clear(&other);
  run_request_end(service, &controller);
  if (!ok) run_request_map_error(&controller, error);
  return ok;
}

void qa_llm_settings_close(qa_llm_settings_service *service) {
  service->closed = true;
  qa_llm_session_close(service->session);
  qa_llm_settings_cancel_sign_in(service);
  for (size_t i = 0; i < service->request_controller_count; i++) {
    qa_llm_signal_abort(service->request_controllers[i]);
  }
}

void qa_llm_settings_destroy(qa_llm_settings_service *service) {
  if (service == NULL) return;
  qa_llm_session_destroy(service->session);
  free(service->base_directory);
  preferences_clear(&service->preferences);
  chatgpt_clear(&service->chatgpt);
  free(service->other_key);
  qa_llm_other_service_clear(&service->other);
  free(service->auth_message);
  for (size_t i = 0; i < service->error_count; i++) {
    free(service->errors[i].file);
    free(service->errors[i].message);
  }
  free(service->errors);
  for (int i = 0; i < QA_LLM_PROVIDER_COUNT; i++) qa_llm_model_catalog_free(&service->catalogs[i]);
  free(service->fingerprint_storage);
  free(service);
}
