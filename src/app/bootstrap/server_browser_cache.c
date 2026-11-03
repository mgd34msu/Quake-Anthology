/* Port of src/app/bootstrap/server-browser-cache.ts. JSON via qa_parse_json_value;
 * the view uses the browser-view cache types. Throws are qa_raise ("Error" as the
 * TS `new Error`); every partial result is owned by a cleanup scope, so a catching
 * caller (StartupServerBrowser.open, loadQ3Cache) leaks nothing. C strings end at
 * NUL, so the TS `includes("\0")` text check has nothing to find (string model).
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_browser_cache.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/network/q3/adapters.h"
#include "qa/persistence/value.h"

static noreturn void cache_error(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static char *xdup(const char *s) {
  size_t n = strlen(s);
  char *r = malloc(n + 1);
  if (!r) qa_range_error("Out of memory");
  memcpy(r, s, n + 1);
  return r;
}
/* UTF-16 code units of UTF-8 text. */
static size_t utf16_length(const char *text) {
  size_t units = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++)
    if ((*p & 0xc0) != 0x80) units += *p >= 0xf0 ? 2 : 1;
  return units;
}
static const qa_save_value *record(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_OBJECT) cache_error("Invalid browser cache object");
  return value;
}
static const qa_save_value *array_of(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_ARRAY) cache_error("Invalid browser cache array");
  return value;
}
static bool safe_integer_in(double value, double minimum, double maximum) {
  return value == trunc(value) && fabs(value) <= 9007199254740991.0 && value >= minimum && value <= maximum;
}
static double integer_of(const qa_save_value *value, double minimum, double maximum) {
  if (value == NULL || value->kind != QA_SAVE_NUMBER || !safe_integer_in(value->number, minimum, maximum))
    cache_error("Invalid browser cache integer");
  return value->number;
}
static const char *text_of(const qa_save_value *value, size_t maximum) {
  if (value == NULL || value->kind != QA_SAVE_STRING || utf16_length(value->text) > maximum) cache_error("Invalid browser cache text");
  return value->text;
}
static void address_of(const qa_save_value *value, qa_net_address *out) {
  qa_net_parse_address_value(value, out);
  if (out->kind != QA_NET_IPV4 || out->port == 0) {
    qa_net_address_destroy(out);
    cache_error("Browser cache requires an IPv4 endpoint");
  }
}
static double finite_of(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_NUMBER || !isfinite(value->number) || value->number < 0)
    cache_error("Invalid browser cache time");
  return value->number;
}
void qa_app_browser_entry_dispose(qa_browser_entry *entry) {
  qa_net_address_destroy(&entry->address);
  free(entry->sources);
  if (entry->has_status) qa_server_status_destroy(&entry->status);
  memset(entry, 0, sizeof *entry);
}
/* status(value): fills *out (owned by the caller's scope as it grows); false is TS null. */
static bool status_of(const qa_save_value *value, qa_server_status *out) {
  memset(out, 0, sizeof *out);
  qa_qw_info_init(&out->rules);
  if (value != NULL && value->kind == QA_SAVE_NULL) return false;
  const qa_save_value *source = record(value);
  const qa_save_value *protocol = qa_save_value_get(source, "protocol");
  if (protocol == NULL || protocol->kind != QA_SAVE_NUMBER || protocol->number != 68)
    cache_error("Browser cache status requires Q3 protocol 68");
  const qa_save_value *pairs = array_of(qa_save_value_get(source, "rules"));
  if (pairs->item_count > 1024) cache_error("Too many browser cache rules");
  for (size_t i = 0; i < pairs->item_count; i++) {
    const qa_save_value *fields = array_of(pairs->items[i]);
    if (fields->item_count != 2) cache_error("Invalid browser cache rule");
    const char *key = text_of(fields->items[0], 1024);
    qa_qw_info_set(&out->rules, key, text_of(fields->items[1], 8192));
  }
  const qa_save_value *players = array_of(qa_save_value_get(source, "playerDetails"));
  if (players->item_count > 1024) cache_error("Too many browser cache players");
  out->name = xdup(text_of(qa_save_value_get(source, "name"), 1024));
  out->map = xdup(text_of(qa_save_value_get(source, "map"), 1024));
  out->players = integer_of(qa_save_value_get(source, "players"), 0, 1024);
  out->max_players = integer_of(qa_save_value_get(source, "maxPlayers"), 0, 1024);
  out->player_details = calloc(players->item_count ? players->item_count : 1, sizeof *out->player_details);
  if (!out->player_details) qa_range_error("Out of memory");
  for (size_t i = 0; i < players->item_count; i++) {
    const qa_save_value *player = record(players->items[i]);
    qa_player_detail *detail = &out->player_details[out->player_count];
    detail->name = xdup(text_of(qa_save_value_get(player, "name"), 1024));
    out->player_count++;
    detail->score = integer_of(qa_save_value_get(player, "score"), -2147483648.0, 2147483647.0);
    detail->ping = integer_of(qa_save_value_get(player, "ping"), -2147483648.0, 2147483647.0);
  }
  out->wire.is_unified = false;
  out->wire.protocol = qa_q3_protocol;
  return true;
}
static const char *source_literal(const char *text) {
  return strcmp(text, "master") == 0 ? "master" : strcmp(text, "secondary-master") == 0 ? "secondary-master"
       : strcmp(text, "favorite") == 0 ? "favorite" : NULL;
}

/* Everything one read owns until it returns. */
typedef struct read_scope {
  qa_save_value *parsed;
  qa_app_q3_browser_cache cache; /* entry_count counts initialized entries */
  bool status_live; size_t status_at;
  char **keys; size_t key_count;
  char **row_keys; size_t row_key_count;
} read_scope;
static void free_keys(char **keys, size_t count) {
  for (size_t i = 0; i < count; i++) free(keys[i]);
  free(keys);
}
static void read_scope_release(void *arg) {
  read_scope *scope = arg;
  if (scope->parsed != NULL) qa_save_value_free(scope->parsed);
  scope->parsed = NULL;
  if (scope->status_live) qa_server_status_destroy(&scope->cache.entries[scope->status_at].status);
  scope->status_live = false;
  qa_app_q3_browser_cache_free(&scope->cache);
  free_keys(scope->keys, scope->key_count); scope->keys = NULL; scope->key_count = 0;
  free_keys(scope->row_keys, scope->row_key_count); scope->row_keys = NULL; scope->row_key_count = 0;
}

qa_app_q3_browser_cache qa_app_read_q3_browser_cache(const char *serialized) {
  if (utf16_length(serialized) > (8u << 20)) cache_error("Browser cache is too large");
  read_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup guard;
  qa_cleanup_push(&guard, read_scope_release, &scope);
  scope.parsed = qa_parse_json_value((const uint8_t *)serialized, strlen(serialized), false);
  if (scope.parsed == NULL) qa_range_error("Out of memory");
  const qa_save_value *root = record(scope.parsed);
  const qa_save_value *version = qa_save_value_get(root, "version");
  const qa_save_value *protocol = qa_save_value_get(root, "protocol");
  if (version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1
      || protocol == NULL || protocol->kind != QA_SAVE_STRING || strcmp(protocol->text, "q3") != 0)
    cache_error("Unsupported browser cache version or protocol");
  const qa_save_value *values = array_of(qa_save_value_get(root, "entries"));
  if (values->item_count > 16384) cache_error("Too many browser cache entries");
  qa_app_q3_browser_cache *cache = &scope.cache;
  cache->entries = calloc(values->item_count ? values->item_count : 1, sizeof *cache->entries);
  scope.keys = calloc(values->item_count ? values->item_count : 1, sizeof *scope.keys);
  if (!cache->entries || !scope.keys) qa_range_error("Out of memory");
  size_t master_count = 0, secondary_count = 0;
  for (size_t i = 0; i < values->item_count; i++) {
    const qa_save_value *entry = record(values->items[i]);
    qa_browser_entry *out = &cache->entries[i];
    qa_net_address endpoint;
    address_of(qa_save_value_get(entry, "address"), &endpoint);
    out->address = endpoint;
    cache->entry_count++;
    char *key = qa_net_address_key(&out->address, true);
    if (!key) qa_range_error("Out of memory");
    scope.keys[scope.key_count++] = key;
    for (size_t j = 0; j + 1 < scope.key_count; j++) if (strcmp(scope.keys[j], key) == 0) cache_error("Duplicate browser cache endpoint");
    const qa_save_value *sources = array_of(qa_save_value_get(entry, "sources"));
    out->sources = calloc(sources->item_count ? sources->item_count : 1, sizeof *out->sources);
    if (!out->sources) qa_range_error("Out of memory");
    for (size_t j = 0; j < sources->item_count; j++) {
      const qa_save_value *value = sources->items[j];
      const char *literal = value->kind == QA_SAVE_STRING ? source_literal(value->text) : NULL;
      if (literal == NULL) cache_error("Invalid cached browser source");
      out->sources[out->source_count++] = literal;
    }
    bool unique = out->source_count != 0;
    for (size_t j = 0; j < out->source_count; j++)
      for (size_t k = 0; k < j; k++) if (strcmp(out->sources[k], out->sources[j]) == 0) unique = false;
    if (!unique) cache_error("Invalid cached browser memberships");
    scope.status_live = true; scope.status_at = i;
    out->has_status = status_of(qa_save_value_get(entry, "status"), &out->status);
    scope.status_live = false;
    if (!out->has_status) qa_server_status_destroy(&out->status);
    const qa_save_value *ping = qa_save_value_get(entry, "pingMilliseconds");
    out->has_ping = !(ping != NULL && ping->kind == QA_SAVE_NULL);
    out->ping_ms = out->has_ping ? finite_of(ping) : 0;
    out->updated_at = finite_of(qa_save_value_get(entry, "updatedAt"));
    for (size_t j = 0; j < out->source_count; j++) {
      if (strcmp(out->sources[j], "master") == 0) master_count++;
      if (strcmp(out->sources[j], "secondary-master") == 0) secondary_count++;
    }
  }
  if (master_count > 8192 || secondary_count > 128) cache_error("Too many cached master entries");
  const qa_save_value *view_value = qa_save_value_get(root, "view");
  /* root["view"] === null; an absent view (undefined) still reaches record(). */
  if (view_value == NULL || view_value->kind != QA_SAVE_NULL) {
    const qa_save_value *lists = array_of(qa_save_value_get(record(view_value), "lists"));
    if (lists->item_count != 3) cache_error("Browser cache requires three UI lists");
    cache->has_view = true;
    cache->view.lists = calloc(3, sizeof *cache->view.lists);
    if (!cache->view.lists) qa_range_error("Out of memory");
    bool seen[4] = {false, false, false, false};
    for (size_t i = 0; i < 3; i++) {
      const qa_save_value *list = record(lists->items[i]);
      const qa_save_value *source_value = qa_save_value_get(list, "source");
      double source = source_value && source_value->kind == QA_SAVE_NUMBER ? source_value->number : -1;
      if ((source != 1 && source != 2 && source != 3) || seen[(int)source]) cache_error("Invalid browser cache list source");
      seen[(int)source] = true;
      const qa_save_value *rows = array_of(qa_save_value_get(list, "rows"));
      if (rows->item_count > (source == 2 ? 4096u : 128u)) cache_error("Too many browser cache UI rows");
      const char *membership = source == 1 ? "secondary-master" : source == 2 ? "master" : "favorite";
      qa_q3_browser_cache_list *out_list = &cache->view.lists[i];
      cache->view.list_count = i + 1;
      out_list->source = (int64_t)source;
      out_list->rows = calloc(rows->item_count ? rows->item_count : 1, sizeof *out_list->rows);
      scope.row_keys = calloc(rows->item_count ? rows->item_count : 1, sizeof *scope.row_keys);
      if (!out_list->rows || !scope.row_keys) qa_range_error("Out of memory");
      for (size_t j = 0; j < rows->item_count; j++) {
        const qa_save_value *row = record(rows->items[j]);
        const char *name = text_of(qa_save_value_get(row, "name"), 31);
        /* [...name].some(character => character.charCodeAt(0) > 255): U+0100 and above
         * start with a UTF-8 lead byte of 0xC4 or more. */
        for (const unsigned char *p = (const unsigned char *)name; *p; p++)
          if (*p >= 0xc4) cache_error("Browser cache UI name must contain source bytes");
        qa_net_address endpoint;
        address_of(qa_save_value_get(row, "address"), &endpoint);
        char *key = qa_net_address_key(&endpoint, true);
        uint8_t host[4];
        memcpy(host, endpoint.ipv4, 4);
        int64_t port = endpoint.port;
        qa_net_address_destroy(&endpoint);
        if (!key) qa_range_error("Out of memory");
        scope.row_keys[scope.row_key_count++] = key;
        bool duplicate = false, member = false;
        for (size_t k = 0; k + 1 < scope.row_key_count; k++) if (strcmp(scope.row_keys[k], key) == 0) duplicate = true;
        for (size_t k = 0; k < scope.key_count; k++) {
          if (strcmp(scope.keys[k], key) != 0) continue;
          for (size_t s = 0; s < cache->entries[k].source_count; s++)
            if (strcmp(cache->entries[k].sources[s], membership) == 0) member = true;
        }
        if (duplicate || !member) cache_error("Browser cache UI row has duplicate or mismatched membership");
        qa_q3_browser_cache_row *out_row = &out_list->rows[out_list->row_count];
        out_row->address.kind = QA_Q3_ADDR_IPV4;
        memcpy(out_row->address.host, host, 4);
        out_row->address.port = port;
        out_row->name = xdup(name);
        out_list->row_count++;
        out_row->visible = (int64_t)integer_of(qa_save_value_get(row, "visible"), -2147483648.0, 2147483647.0);
        out_row->ping = (int64_t)integer_of(qa_save_value_get(row, "ping"), -2147483648.0, 2147483647.0);
      }
      free_keys(scope.row_keys, scope.row_key_count);
      scope.row_keys = NULL; scope.row_key_count = 0;
    }
  }
  qa_app_q3_browser_cache result = scope.cache;
  memset(&scope.cache, 0, sizeof scope.cache);
  qa_cleanup_pop(&guard);
  read_scope_release(&scope);
  return result;
}
void qa_app_q3_browser_cache_free(qa_app_q3_browser_cache *cache) {
  for (size_t i = 0; i < cache->entry_count; i++) qa_app_browser_entry_dispose(&cache->entries[i]);
  free(cache->entries);
  if (cache->has_view || cache->view.lists != NULL) qa_q3_browser_cache_view_destroy(&cache->view);
  memset(cache, 0, sizeof *cache);
}

/* JSON.stringify output into a growing buffer owned by the write's cleanup scope. */
typedef struct json_text { char *data; size_t length, capacity; } json_text;
static void json_text_release(void *arg) {
  json_text *text = arg;
  free(text->data);
  text->data = NULL;
}
static void append_bytes(json_text *text, const char *add, size_t n) {
  if (text->length + n + 1 > text->capacity) {
    size_t capacity = text->capacity ? text->capacity : 1024;
    while (capacity < text->length + n + 1) capacity *= 2;
    char *grown = realloc(text->data, capacity);
    if (!grown) qa_range_error("Out of memory");
    text->data = grown; text->capacity = capacity;
  }
  memcpy(text->data + text->length, add, n);
  text->length += n;
  text->data[text->length] = '\0';
}
static void append_str(json_text *text, const char *add) { append_bytes(text, add, strlen(add)); }
static void append_json_string(json_text *text, const char *value) {
  append_str(text, "\"");
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    char tmp[8];
    switch (*p) {
      case '"': append_str(text, "\\\""); break;
      case '\\': append_str(text, "\\\\"); break;
      case '\b': append_str(text, "\\b"); break;
      case '\f': append_str(text, "\\f"); break;
      case '\n': append_str(text, "\\n"); break;
      case '\r': append_str(text, "\\r"); break;
      case '\t': append_str(text, "\\t"); break;
      default:
        if (*p < 0x20) { snprintf(tmp, sizeof tmp, "\\u%04x", *p); append_str(text, tmp); }
        else append_bytes(text, (const char *)p, 1);
    }
  }
  append_str(text, "\"");
}
/* JSON.stringify(number): non-finite numbers are null. */
static void append_num(json_text *text, double value) {
  if (!isfinite(value)) { append_str(text, "null"); return; }
  char *n = qa_input_js_number(value);
  if (!n) qa_range_error("Out of memory");
  append_str(text, n);
  free(n);
}
static void append_address(json_text *text, const qa_net_address *a) {
  char tmp[160];
  if (a->kind == QA_NET_IPV4) {
    snprintf(tmp, sizeof tmp, "{\"kind\":\"ipv4\",\"host\":[%u,%u,%u,%u],\"port\":%lld}", a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3], (long long)a->port);
    append_str(text, tmp);
  } else if (a->kind == QA_NET_IPV6) {
    append_str(text, "{\"kind\":\"ipv6\",\"host\":");
    append_json_string(text, a->host ? a->host : "");
    snprintf(tmp, sizeof tmp, ",\"port\":%lld}", (long long)a->port);
    append_str(text, tmp);
  } else if (a->kind == QA_NET_IPX) {
    snprintf(tmp, sizeof tmp, "{\"kind\":\"ipx\",\"network\":%u,\"node\":[%u,%u,%u,%u,%u,%u],\"port\":%lld}",
             a->network, a->node[0], a->node[1], a->node[2], a->node[3], a->node[4], a->node[5], (long long)a->port);
    append_str(text, tmp);
  } else {
    append_str(text, "{\"kind\":\"loopback\",\"id\":");
    append_json_string(text, a->host ? a->host : "");
    append_str(text, "}");
  }
}
/* `status(JSON.parse(JSON.stringify(cached)))` succeeds: the checks status() applies,
 * on the values the round trip would carry (non-finite numbers become null). */
static bool storable_status(const qa_server_status *status) {
  if (status->rules.count > 1024 || status->player_count > 1024) return false;
  for (size_t i = 0; i < status->rules.count; i++)
    if (utf16_length(status->rules.entries[i].key) > 1024 || utf16_length(status->rules.entries[i].value) > 8192) return false;
  if (utf16_length(status->name ? status->name : "") > 1024 || utf16_length(status->map ? status->map : "") > 1024) return false;
  if (!safe_integer_in(status->players, 0, 1024) || !safe_integer_in(status->max_players, 0, 1024)) return false;
  for (size_t i = 0; i < status->player_count; i++) {
    const qa_player_detail *player = &status->player_details[i];
    if (utf16_length(player->name ? player->name : "") > 1024) return false;
    if (!safe_integer_in(player->score, -2147483648.0, 2147483647.0) || !safe_integer_in(player->ping, -2147483648.0, 2147483647.0))
      return false;
  }
  return true;
}
static bool cached_source(const char *source) {
  return strcmp(source, "master") == 0 || strcmp(source, "secondary-master") == 0 || strcmp(source, "favorite") == 0;
}
char *qa_app_write_q3_browser_cache(const qa_browser_entry *entries, size_t entry_count, const qa_q3_browser_cache_view *view) {
  json_text text = {NULL, 0, 0};
  qa_cleanup guard;
  qa_cleanup_push(&guard, json_text_release, &text);
  append_str(&text, "{\"version\":1,\"protocol\":\"q3\",\"entries\":[");
  bool first_entry = true;
  for (size_t i = 0; i < entry_count; i++) {
    const qa_browser_entry *entry = &entries[i];
    bool keep = false;
    for (size_t j = 0; j < entry->source_count; j++) if (cached_source(entry->sources[j])) keep = true;
    if (!keep) continue;
    if (!first_entry) append_str(&text, ",");
    first_entry = false;
    append_str(&text, "{\"address\":");
    append_address(&text, &entry->address);
    append_str(&text, ",\"sources\":[");
    bool first_source = true;
    for (size_t j = 0; j < entry->source_count; j++) {
      if (!cached_source(entry->sources[j])) continue;
      if (!first_source) append_str(&text, ",");
      first_source = false;
      append_json_string(&text, entry->sources[j]);
    }
    append_str(&text, "],\"status\":");
    /* A status the loader would refuse is dropped, so one odd server cannot stop the whole cache from saving. */
    if (!entry->has_status || !storable_status(&entry->status)) append_str(&text, "null");
    else {
      append_str(&text, "{\"name\":"); append_json_string(&text, entry->status.name ? entry->status.name : "");
      append_str(&text, ",\"map\":"); append_json_string(&text, entry->status.map ? entry->status.map : "");
      append_str(&text, ",\"players\":"); append_num(&text, entry->status.players);
      append_str(&text, ",\"maxPlayers\":"); append_num(&text, entry->status.max_players);
      append_str(&text, ",\"rules\":[");
      for (size_t j = 0; j < entry->status.rules.count; j++) {
        if (j) append_str(&text, ",");
        append_str(&text, "["); append_json_string(&text, entry->status.rules.entries[j].key);
        append_str(&text, ","); append_json_string(&text, entry->status.rules.entries[j].value); append_str(&text, "]");
      }
      append_str(&text, "],\"playerDetails\":[");
      for (size_t j = 0; j < entry->status.player_count; j++) {
        if (j) append_str(&text, ",");
        append_str(&text, "{\"name\":"); append_json_string(&text, entry->status.player_details[j].name ? entry->status.player_details[j].name : "");
        append_str(&text, ",\"score\":"); append_num(&text, entry->status.player_details[j].score);
        append_str(&text, ",\"ping\":"); append_num(&text, entry->status.player_details[j].ping);
        append_str(&text, "}");
      }
      append_str(&text, "],\"protocol\":68}");
    }
    append_str(&text, ",\"pingMilliseconds\":");
    if (entry->has_ping) append_num(&text, entry->ping_ms); else append_str(&text, "null");
    append_str(&text, ",\"updatedAt\":"); append_num(&text, entry->updated_at);
    append_str(&text, "}");
  }
  append_str(&text, "],\"view\":");
  if (view == NULL) append_str(&text, "null");
  else {
    append_str(&text, "{\"lists\":[");
    for (size_t i = 0; i < view->list_count; i++) {
      if (i) append_str(&text, ",");
      char tmp[96];
      snprintf(tmp, sizeof tmp, "{\"source\":%lld,\"rows\":[", (long long)view->lists[i].source);
      append_str(&text, tmp);
      for (size_t j = 0; j < view->lists[i].row_count; j++) {
        if (j) append_str(&text, ",");
        const qa_q3_browser_cache_row *row = &view->lists[i].rows[j];
        qa_net_address a;
        memset(&a, 0, sizeof a);
        a.kind = QA_NET_IPV4; memcpy(a.ipv4, row->address.host, 4); a.port = row->address.port;
        append_str(&text, "{\"address\":");
        append_address(&text, &a);
        append_str(&text, ",\"name\":"); append_json_string(&text, row->name ? row->name : "");
        snprintf(tmp, sizeof tmp, ",\"visible\":%lld,\"ping\":%lld}", (long long)row->visible, (long long)row->ping);
        append_str(&text, tmp);
      }
      append_str(&text, "]}");
    }
    append_str(&text, "]}");
  }
  append_str(&text, "}");
  qa_app_q3_browser_cache check = qa_app_read_q3_browser_cache(text.data);
  qa_app_q3_browser_cache_free(&check);
  qa_cleanup_pop(&guard);
  return text.data;
}
