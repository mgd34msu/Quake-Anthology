#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/models.h. GPL-2.0-or-later. */
#include "qa/llm/models.h"

#include "qa/llm/codex.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

const char *qa_llm_reasoning_source_name(qa_llm_reasoning_source source) {
  switch (source) {
    case QA_LLM_REASONING_PROVIDER: return "provider";
    case QA_LLM_REASONING_REFERENCE: return "reference";
    case QA_LLM_REASONING_UNKNOWN: return "unknown";
  }
  return "unknown";
}

const char *qa_llm_catalog_status_name(qa_llm_catalog_status status) {
  switch (status) {
    case QA_LLM_CATALOG_IDLE: return "idle";
    case QA_LLM_CATALOG_LOADING: return "loading";
    case QA_LLM_CATALOG_READY: return "ready";
    case QA_LLM_CATALOG_ERROR: return "error";
  }
  return "idle";
}

void qa_llm_model_clear(qa_llm_model *model) {
  free(model->id);
  free(model->name);
  for (size_t i = 0; i < model->reasoning_effort_count; i++) free(model->reasoning_efforts[i]);
  free(model->reasoning_efforts);
  free(model->default_reasoning_effort);
  memset(model, 0, sizeof *model);
}

void qa_llm_model_list_free(qa_llm_model_list *list) {
  for (size_t i = 0; i < list->count; i++) qa_llm_model_clear(&list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}

static void model_list_push(qa_llm_model_list *list, qa_llm_model *model) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    list->items = realloc(list->items, list->capacity * sizeof *list->items);
    if (list->items == NULL) qa_llm_oom();
  }
  list->items[list->count++] = *model;
  memset(model, 0, sizeof *model);
}

void qa_llm_model_copy(const qa_llm_model *source, qa_llm_model *out) {
  memset(out, 0, sizeof *out);
  out->id = qa_llm_strdup(source->id);
  out->name = qa_llm_strdup(source->name);
  if (source->reasoning_effort_count > 0) {
    out->reasoning_efforts = calloc(source->reasoning_effort_count, sizeof *out->reasoning_efforts);
    if (out->reasoning_efforts == NULL) qa_llm_oom();
    for (size_t i = 0; i < source->reasoning_effort_count; i++) out->reasoning_efforts[i] = qa_llm_strdup(source->reasoning_efforts[i]);
    out->reasoning_effort_count = source->reasoning_effort_count;
  }
  out->default_reasoning_effort = source->default_reasoning_effort != NULL ? qa_llm_strdup(source->default_reasoning_effort) : NULL;
  out->reasoning_source = source->reasoning_source;
  out->recommended = source->recommended;
}

void qa_llm_model_list_copy(const qa_llm_model_list *source, qa_llm_model_list *out) {
  memset(out, 0, sizeof *out);
  for (size_t i = 0; i < source->count; i++) {
    qa_llm_model copy;
    qa_llm_model_copy(&source->items[i], &copy);
    model_list_push(out, &copy);
  }
}

bool qa_llm_model_has_effort(const qa_llm_model *model, const char *effort) {
  if (model == NULL || effort == NULL) return false;
  for (size_t i = 0; i < model->reasoning_effort_count; i++) {
    if (strcmp(model->reasoning_efforts[i], effort) == 0) return true;
  }
  return false;
}

void qa_llm_model_catalog_free(qa_llm_model_catalog *catalog) {
  qa_llm_model_list_free(&catalog->models);
  free(catalog->message);
  memset(catalog, 0, sizeof *catalog);
}

void qa_llm_model_catalog_copy(const qa_llm_model_catalog *source, qa_llm_model_catalog *out) {
  memset(out, 0, sizeof *out);
  out->status = source->status;
  qa_llm_model_list_copy(&source->models, &out->models);
  out->message = source->message != NULL ? qa_llm_strdup(source->message) : NULL;
}

/* models.ts string(): JS length > 256 counts UTF-16 units. */
static bool model_string(const qa_llm_json *value, char **out, qa_llm_error *error) {
  bool bad = value == NULL || value->kind != QA_LLM_JSON_STRING || qa_llm_trim_is_empty(value->string) || qa_llm_utf16_length(value->string) > 256;
  if (!bad) {
    for (const uint8_t *p = (const uint8_t *)value->string; *p != '\0'; p++) {
      if (*p <= 0x1f || *p == 0x7f) bad = true;
    }
  }
  /* \x00 cannot appear in a C string; TS rejects it via the same class. */
  if (bad) {
    qa_llm_error_set(error, "The service returned invalid model metadata.");
    return false;
  }
  *out = qa_llm_trim_dup(value->string);
  return true;
}

static bool effort_from_string(const char *value, char **out_effort, qa_llm_error *error) {
  /* string(value) then /^[a-z][a-z0-9_-]{0,63}$/ on the trimmed text. */
  qa_llm_json wrapped;
  memset(&wrapped, 0, sizeof wrapped);
  wrapped.kind = QA_LLM_JSON_STRING;
  wrapped.string = (char *)value;
  char *effort = NULL;
  if (!model_string(&wrapped, &effort, error)) return false;
  size_t length = strlen(effort);
  bool valid = length >= 1 && length <= 64 && effort[0] >= 'a' && effort[0] <= 'z';
  for (size_t i = 1; valid && i < length; i++) {
    char c = effort[i];
    valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  }
  if (!valid) {
    free(effort);
    qa_llm_error_set(error, "Invalid reasoning effort.");
    return false;
  }
  *out_effort = effort;
  return true;
}

bool qa_llm_parse_reasoning_effort(const qa_llm_json *value, char **out_effort, qa_llm_error *error) {
  *out_effort = NULL;
  if (value == NULL || value->kind == QA_LLM_JSON_NULL) return true;
  if (value->kind != QA_LLM_JSON_STRING) {
    /* string(value) throws the metadata message for non-strings. */
    char *ignored = NULL;
    return model_string(value, &ignored, error);
  }
  return effort_from_string(value->string, out_effort, error);
}

bool qa_llm_parse_reasoning_effort_string(const char *value, char **out_effort, qa_llm_error *error) {
  *out_effort = NULL;
  if (value == NULL) return true;
  return effort_from_string(value, out_effort, error);
}

typedef struct api_reasoning_entry {
  const char *id;
  const char *const *efforts;
  size_t count;
} api_reasoning_entry;

static const char *const efforts_gpt5[] = { "minimal", "low", "medium", "high" };
static const char *const efforts_high[] = { "high" };
static const char *const efforts_med_high_xhigh[] = { "medium", "high", "xhigh" };
static const char *const efforts_none_low_med_high_xhigh[] = { "none", "low", "medium", "high", "xhigh" };
static const char *const efforts_none_low_med_high_xhigh_max[] = { "none", "low", "medium", "high", "xhigh", "max" };
static const char *const efforts_none_low_med_high[] = { "none", "low", "medium", "high" };
static const char *const efforts_low_med_high_xhigh_max[] = { "low", "medium", "high", "xhigh", "max" };

static const api_reasoning_entry API_REASONING[] = {
  { "gpt-5", efforts_gpt5, 4 },
  { "gpt-5-pro", efforts_high, 1 },
  { "gpt-5.2-pro", efforts_med_high_xhigh, 3 },
  { "gpt-5.4", efforts_none_low_med_high_xhigh, 5 },
  { "gpt-5.4-pro", efforts_med_high_xhigh, 3 },
  { "gpt-5.4-mini", efforts_none_low_med_high_xhigh, 5 },
  { "gpt-5.4-nano", efforts_none_low_med_high_xhigh, 5 },
  { "gpt-5.6-sol", efforts_none_low_med_high_xhigh_max, 6 },
  { "gpt-5.1", efforts_none_low_med_high, 4 },
  { "gpt-5.2", efforts_none_low_med_high_xhigh, 5 },
  { "gpt-5.5", efforts_none_low_med_high_xhigh, 5 },
  { "gpt-5.5-pro", efforts_med_high_xhigh, 3 },
  { "gpt-6-astra", efforts_low_med_high_xhigh_max, 5 },
  { "gpt-5.6-terra", efforts_none_low_med_high_xhigh_max, 6 },
  { "gpt-5.6-luna", efforts_none_low_med_high_xhigh_max, 6 },
};

const char *qa_llm_fastest_reasoning_effort(const qa_llm_model *model) {
  if (model == NULL || model->reasoning_effort_count == 0) return NULL;
  static const char *const preferred[] = { "none", "minimal", "low" };
  for (size_t i = 0; i < 3; i++) {
    if (qa_llm_model_has_effort(model, preferred[i])) return preferred[i];
  }
  return model->reasoning_efforts[0];
}

void qa_llm_api_model(const char *id, qa_llm_model *out) {
  memset(out, 0, sizeof *out);
  out->id = qa_llm_strdup(id);
  out->name = qa_llm_strdup(id);
  /* id.replace(/-\d{4}-\d{2}-\d{2}$/, "") */
  char *lookup = qa_llm_strdup(id);
  size_t length = strlen(lookup);
  if (length >= 11) {
    const char *tail = lookup + length - 11;
    bool date = tail[0] == '-' && tail[5] == '-' && tail[8] == '-';
    for (int i = 1; date && i < 11; i++) {
      if (i == 5 || i == 8) continue;
      if (tail[i] < '0' || tail[i] > '9') date = false;
    }
    if (date) lookup[length - 11] = '\0';
  }
  const api_reasoning_entry *found = NULL;
  for (size_t i = 0; i < sizeof API_REASONING / sizeof API_REASONING[0]; i++) {
    if (strcmp(API_REASONING[i].id, lookup) == 0) found = &API_REASONING[i];
  }
  free(lookup);
  if (found != NULL) {
    out->reasoning_efforts = calloc(found->count, sizeof *out->reasoning_efforts);
    if (out->reasoning_efforts == NULL) qa_llm_oom();
    for (size_t i = 0; i < found->count; i++) out->reasoning_efforts[i] = qa_llm_strdup(found->efforts[i]);
    out->reasoning_effort_count = found->count;
    out->reasoning_source = QA_LLM_REASONING_REFERENCE;
  } else {
    out->reasoning_source = QA_LLM_REASONING_UNKNOWN;
  }
  out->default_reasoning_effort = NULL;
  out->recommended = false;
}

/* NON_CHAT, case-insensitive substring/alternation test. */
static bool contains_case_insensitive(const char *haystack, const char *needle) {
  size_t needle_len = strlen(needle);
  for (const char *p = haystack; *p != '\0'; p++) {
    size_t i = 0;
    while (i < needle_len && p[i] != '\0') {
      char a = p[i], b = needle[i];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) break;
      i++;
    }
    if (i == needle_len) return true;
  }
  return false;
}

static bool is_non_chat(const char *id) {
  static const char *const needles[] = {
    "embedding", "whisper", "tts", "dall-e", "davinci", "babbage", "moderation",
    "text-search", "similarity", "transcribe", "speech", "realtime", "image",
  };
  for (size_t i = 0; i < sizeof needles / sizeof needles[0]; i++) {
    if (contains_case_insensitive(id, needles[i])) return true;
  }
  /* ^ada */
  char first[4] = { id[0], id[0] != '\0' ? id[1] : '\0', id[0] != '\0' && id[1] != '\0' ? id[2] : '\0', '\0' };
  for (int i = 0; i < 3; i++) {
    if (first[i] >= 'A' && first[i] <= 'Z') first[i] = (char)(first[i] - 'A' + 'a');
  }
  return strcmp(first, "ada") == 0;
}

static int compare_model_name(const void *a, const void *b) {
  /* TS a.name.localeCompare(b.name). */
  return qa_js_locale_compare(((const qa_llm_model *)a)->name, ((const qa_llm_model *)b)->name);
}

bool qa_llm_parse_api_models(const qa_llm_json *value, bool openai, qa_llm_model_list *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  if (!qa_llm_record(value, error)) return false;
  const qa_llm_json *data = qa_llm_json_get(value, "data");
  if (!qa_llm_json_is_array(data)) {
    qa_llm_error_set(error, "The service returned an invalid model list.");
    return false;
  }
  for (size_t i = 0; i < data->count; i++) {
    if (!qa_llm_record(data->items[i], error)) { qa_llm_model_list_free(out); return false; }
    char *id = NULL;
    if (!model_string(qa_llm_json_get(data->items[i], "id"), &id, error)) { qa_llm_model_list_free(out); return false; }
    if (openai && is_non_chat(id)) { free(id); continue; }
    qa_llm_model model;
    if (openai) {
      qa_llm_api_model(id, &model);
    } else {
      memset(&model, 0, sizeof model);
      model.id = qa_llm_strdup(id);
      model.name = qa_llm_strdup(id);
      model.reasoning_source = QA_LLM_REASONING_UNKNOWN;
    }
    free(id);
    /* Map.set: a later duplicate replaces in original position. */
    bool replaced = false;
    for (size_t j = 0; j < out->count; j++) {
      if (strcmp(out->items[j].id, model.id) == 0) {
        qa_llm_model_clear(&out->items[j]);
        out->items[j] = model;
        replaced = true;
        break;
      }
    }
    if (!replaced) model_list_push(out, &model);
  }
  if (out->count > 1) qa_js_sort(out->items, out->count, sizeof *out->items, compare_model_name);
  return true;
}

typedef struct prioritized_model {
  qa_llm_model model;
  double priority;
  size_t order;
} prioritized_model;

static int compare_priority(const void *a, const void *b) {
  const prioritized_model *left = a;
  const prioritized_model *right = b;
  double diff = left->priority - right->priority; /* JS sort comparator */
  if (diff < 0) return -1;
  if (diff > 0) return 1;
  return left->order < right->order ? -1 : 1; /* stable, like JS sort */
}

bool qa_llm_parse_codex_models(const qa_llm_json *value, qa_llm_model_list *out, qa_llm_error *error) {
  memset(out, 0, sizeof *out);
  if (!qa_llm_record(value, error)) return false;
  const qa_llm_json *list = qa_llm_json_get(value, "models");
  if (!qa_llm_json_is_array(list)) {
    qa_llm_error_set(error, "The subscription service returned an invalid model list.");
    return false;
  }
  prioritized_model *models = NULL;
  size_t count = 0, capacity = 0;
  bool ok = true;
  for (size_t i = 0; ok && i < list->count; i++) {
    const qa_llm_json *item = list->items[i];
    if (!qa_llm_record(item, error)) { ok = false; break; }
    const qa_llm_json *visibility = qa_llm_json_get(item, "visibility");
    if (!(visibility != NULL && visibility->kind == QA_LLM_JSON_STRING && strcmp(visibility->string, "list") == 0)) continue;
    qa_llm_model model;
    memset(&model, 0, sizeof model);
    if (!model_string(qa_llm_json_get(item, "slug"), &model.id, error)) { ok = false; break; }
    model.name = qa_llm_strdup(model.id); /* replaced below */
    const qa_llm_json *levels = qa_llm_json_get(item, "supported_reasoning_levels");
    if (!qa_llm_json_is_array(levels)) {
      qa_llm_error_set(error, "The subscription service returned invalid reasoning choices.");
      qa_llm_model_clear(&model);
      ok = false;
      break;
    }
    for (size_t j = 0; j < levels->count; j++) {
      if (!qa_llm_record(levels->items[j], error)) { ok = false; break; }
      char *effort = NULL;
      if (!qa_llm_parse_reasoning_effort(qa_llm_json_get(levels->items[j], "effort"), &effort, error)) { ok = false; break; }
      if (effort == NULL) {
        qa_llm_error_set(error, "The subscription service returned invalid reasoning choices.");
        ok = false;
        break;
      }
      if (!qa_llm_model_has_effort(&model, effort)) {
        model.reasoning_efforts = realloc(model.reasoning_efforts, (model.reasoning_effort_count + 1) * sizeof *model.reasoning_efforts);
        if (model.reasoning_efforts == NULL) qa_llm_oom();
        model.reasoning_efforts[model.reasoning_effort_count++] = effort;
      } else free(effort);
    }
    if (!ok) { qa_llm_model_clear(&model); break; }
    char *default_effort = NULL;
    if (!qa_llm_parse_reasoning_effort(qa_llm_json_get(item, "default_reasoning_level"), &default_effort, error)) {
      qa_llm_model_clear(&model);
      ok = false;
      break;
    }
    const qa_llm_json *priority = qa_llm_json_get(item, "priority");
    if (priority == NULL || priority->kind != QA_LLM_JSON_NUMBER || !isfinite(priority->number)) {
      qa_llm_error_set(error, "The subscription service returned invalid model priority metadata.");
      free(default_effort);
      qa_llm_model_clear(&model);
      ok = false;
      break;
    }
    const qa_llm_json *display = qa_llm_json_get(item, "display_name");
    qa_llm_json id_json;
    memset(&id_json, 0, sizeof id_json);
    id_json.kind = QA_LLM_JSON_STRING;
    id_json.string = model.id;
    free(model.name);
    model.name = NULL;
    /* item["display_name"] ?? id: a JSON null display name falls back too. */
    bool nullish = display == NULL || display->kind == QA_LLM_JSON_NULL;
    if (!model_string(nullish ? &id_json : display, &model.name, error)) {
      free(default_effort);
      qa_llm_model_clear(&model);
      ok = false;
      break;
    }
    model.default_reasoning_effort = default_effort != NULL && qa_llm_model_has_effort(&model, default_effort) ? default_effort : NULL;
    if (model.default_reasoning_effort == NULL) free(default_effort);
    model.reasoning_source = QA_LLM_REASONING_PROVIDER;
    const qa_llm_json *is_default = qa_llm_json_get(item, "is_default");
    model.recommended = is_default != NULL && is_default->kind == QA_LLM_JSON_BOOL && is_default->boolean;
    if (count == capacity) {
      capacity = capacity == 0 ? 8 : capacity * 2;
      models = realloc(models, capacity * sizeof *models);
      if (models == NULL) qa_llm_oom();
    }
    models[count].model = model;
    models[count].priority = priority->number;
    models[count].order = count;
    count++;
  }
  if (!ok) {
    for (size_t i = 0; i < count; i++) qa_llm_model_clear(&models[i].model);
    free(models);
    return false;
  }
  if (count > 1) qa_js_sort(models, count, sizeof *models, compare_priority);
  /* [...new Map(models.map(...)).values()]: dedupe by id, first wins in
   * insertion position but the LAST value (Map.set semantics on an
   * entries-built Map keeps the later value at the first position). */
  for (size_t i = 0; i < count; i++) {
    bool replaced = false;
    for (size_t j = 0; j < out->count; j++) {
      if (strcmp(out->items[j].id, models[i].model.id) == 0) {
        qa_llm_model_clear(&out->items[j]);
        out->items[j] = models[i].model;
        memset(&models[i].model, 0, sizeof models[i].model);
        replaced = true;
        break;
      }
    }
    if (!replaced) model_list_push(out, &models[i].model);
  }
  free(models);
  for (size_t i = 0; i < out->count; i++) out->items[i].recommended = i == 0;
  return true;
}

/* modelJson() */
static bool model_json(qa_llm_http_response *response, qa_llm_abort_signal *signal, qa_llm_json **out, qa_llm_error *error) {
  if (!qa_llm_response_ok(response)) {
    int status = response->status;
    qa_llm_cancel_response_body(response);
    qa_llm_error_http(error, status);
    return false;
  }
  bool has_body = false;
  if (!qa_llm_response_body(response, &has_body, error)) return false;
  if (!has_body) {
    qa_llm_error_set(error, "The service returned an empty model list response.");
    return false;
  }
  qa_llm_bytes raw;
  qa_llm_bytes_init(&raw);
  qa_llm_strbuf text;
  qa_llm_strbuf_init(&text);
  bool ok = true;
  bool internal_error = false; /* LlmSettingsError raised inside: rethrow */
  size_t size = 0;
  uint8_t chunk[8192];
  for (;;) {
    if (!qa_llm_check_request_abort(signal, error)) { ok = false; internal_error = true; break; }
    size_t length = 0;
    qa_llm_read_result result = response->read(response->body_ctx, chunk, sizeof chunk, &length);
    if (result == QA_LLM_READ_DONE) break;
    if (result == QA_LLM_READ_ERROR) { ok = false; break; }
    size += length;
    if (size > 4194304) {
      qa_llm_error_set(error, "The service model list exceeded the size limit.");
      ok = false;
      internal_error = true;
      break;
    }
    qa_llm_bytes_append(&raw, chunk, length);
    size_t valid = 0;
    bool valid_ok = qa_llm_utf8_valid(raw.data, raw.length, &valid);
    if (valid_ok) valid = raw.length;
    else {
      /* The streaming fatal decoder throws unless the tail can still complete. */
      bool partial = valid < raw.length && qa_llm_utf8_incomplete_tail(raw.data + valid, raw.length - valid);
      if (!partial) { ok = false; break; } /* decoder fatal: generic */
    }
    if (valid > 0) {
      qa_llm_strbuf_append_n(&text, raw.data, valid);
      memmove(raw.data, raw.data + valid, raw.length - valid);
      raw.length -= valid;
    }
  }
  if (ok && raw.length > 0) ok = false; /* final decode flush is fatal */
  if (ok && !qa_llm_json_parse(qa_llm_strbuf_text(&text), text.length, out)) ok = false;
  qa_llm_cancel_response_body(response);
  qa_llm_bytes_free(&raw);
  qa_llm_strbuf_free(&text);
  if (!ok && !internal_error) {
    qa_llm_error abort_error;
    if (!qa_llm_check_request_abort(signal, &abort_error)) *error = abort_error;
    else qa_llm_error_set(error, "The service returned an invalid model list response.");
  }
  return ok;
}

bool qa_llm_discover_api_models(const char *base_url, const char *api_key, bool openai, qa_llm_fetch_ref fetcher,
                                qa_llm_abort_signal *signal, qa_llm_model_list *out, qa_llm_error *error) {
  char *base = qa_llm_strip_trailing_slashes(base_url);
  qa_llm_strbuf url;
  qa_llm_strbuf_init(&url);
  qa_llm_strbuf_appendf(&url, "%s/models", base);
  free(base);
  qa_llm_strbuf authorization;
  qa_llm_strbuf_init(&authorization);
  qa_llm_strbuf_appendf(&authorization, "Bearer %s", api_key);
  qa_llm_header headers[] = {
    { "authorization", authorization.data },
    { "accept", "application/json" },
  };
  qa_llm_http_init init;
  memset(&init, 0, sizeof init);
  init.method = "GET";
  init.headers = headers;
  init.header_count = 2;
  qa_llm_http_response response;
  bool ok = qa_llm_fetch_llm_response(qa_llm_strbuf_text(&url), &init, fetcher, signal, &response, error);
  qa_llm_json *value = NULL;
  if (ok) {
    ok = model_json(&response, signal, &value, error);
    qa_llm_response_dispose(&response);
  }
  if (ok) ok = qa_llm_parse_api_models(value, openai, out, error);
  qa_llm_json_free(value);
  qa_llm_strbuf_free(&authorization);
  qa_llm_strbuf_free(&url);
  return ok;
}

bool qa_llm_discover_codex_models(const qa_llm_subscription_credential *credential, qa_llm_fetch_ref fetcher,
                                  qa_llm_abort_signal *signal, qa_llm_model_list *out, qa_llm_error *error) {
  char *account = NULL;
  qa_llm_strbuf authorization;
  qa_llm_strbuf_init(&authorization);
  qa_llm_strbuf_appendf(&authorization, "Bearer %s", credential->access_token);
  /* subscriptionAccountId runs while building headers in TS too. */
  if (!qa_llm_subscription_account_id(credential->access_token, &account, error)) {
    qa_llm_strbuf_free(&authorization);
    return false;
  }
  qa_llm_header headers[] = {
    { "authorization", authorization.data },
    { "chatgpt-account-id", account },
    { "originator", "pi" },
    { "accept", "application/json" },
  };
  qa_llm_http_init init;
  memset(&init, 0, sizeof init);
  init.method = "GET";
  init.headers = headers;
  init.header_count = 4;
  qa_llm_http_response response;
  bool ok = qa_llm_fetch_llm_response("https://chatgpt.com/backend-api/codex/models?client_version=0.162.1",
                                      &init, fetcher, signal, &response, error);
  qa_llm_json *value = NULL;
  if (ok) {
    ok = model_json(&response, signal, &value, error);
    qa_llm_response_dispose(&response);
  }
  if (ok) ok = qa_llm_parse_codex_models(value, out, error);
  qa_llm_json_free(value);
  qa_llm_strbuf_free(&authorization);
  free(account);
  return ok;
}
