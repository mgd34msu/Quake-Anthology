/* Port of src/app/bootstrap/server-browser-cache.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/server_browser_cache.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/server_browser_addresses.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/persistence/value.h"
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static const qa_save_value *record(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_OBJECT) qa_range_error("Invalid browser cache object");
  return value;
}
static const qa_save_value *array_of(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_ARRAY) qa_range_error("Invalid browser cache array");
  return value;
}
static double integer_of(const qa_save_value *value, double min, double max) {
  if (value == NULL || value->kind != QA_SAVE_NUMBER || value->number != trunc(value->number)
      || value->number > 9007199254740991.0 || value->number < -9007199254740991.0
      || value->number < min || value->number > max)
    qa_range_error("Invalid browser cache integer");
  return value->number;
}
static const char *text_of(const qa_save_value *value, size_t max) {
  if (value == NULL || value->kind != QA_SAVE_STRING || strlen(value->text) > max || strchr(value->text, '\0') != value->text + strlen(value->text))
    qa_range_error("Invalid browser cache text");
  return value->text;
}
static qa_net_address address_of(const qa_save_value *value) {
  qa_net_address result;
  qa_app_parse_network_address_value(value, &result);
  if (result.kind != QA_NET_IPV4 || result.port == 0) qa_range_error("Browser cache requires an IPv4 endpoint");
  return result;
}
static double finite_of(const qa_save_value *value) {
  if (value == NULL || value->kind != QA_SAVE_NUMBER || !isfinite(value->number) || value->number < 0)
    qa_range_error("Invalid browser cache time");
  return value->number;
}
void qa_app_browser_entry_dispose(qa_browser_entry *entry) {
  qa_net_address_destroy(&entry->address);
  free(entry->sources);
  if (entry->has_status) qa_server_status_destroy(&entry->status);
  memset(entry, 0, sizeof *entry);
}
static bool status_of(const qa_save_value *value, qa_server_status *out) {
  memset(out, 0, sizeof *out);
  if (value != NULL && value->kind == QA_SAVE_NULL) return false;
  const qa_save_value *source = record(value);
  const qa_save_value *protocol = qa_save_value_get(source, "protocol");
  if (protocol == NULL || protocol->kind != QA_SAVE_NUMBER || protocol->number != 68)
    qa_range_error("Browser cache status requires Q3 protocol 68");
  const qa_save_value *pairs = array_of(qa_save_value_get(source, "rules"));
  if (pairs->item_count > 1024) qa_range_error("Too many browser cache rules");
  qa_qw_info_init(&out->rules);
  for (size_t i = 0; i < pairs->item_count; i++) {
    const qa_save_value *fields = array_of(pairs->items[i]);
    if (fields->item_count != 2) qa_range_error("Invalid browser cache rule");
    qa_qw_info_set(&out->rules, text_of(fields->items[0], 1024), text_of(fields->items[1], 8192));
  }
  const qa_save_value *players = array_of(qa_save_value_get(source, "playerDetails"));
  if (players->item_count > 1024) qa_range_error("Too many browser cache players");
  out->name = xdup(text_of(qa_save_value_get(source, "name"), 1024));
  out->map = xdup(text_of(qa_save_value_get(source, "map"), 1024));
  out->players = integer_of(qa_save_value_get(source, "players"), 0, 1024);
  out->max_players = integer_of(qa_save_value_get(source, "maxPlayers"), 0, 1024);
  out->player_details = calloc(players->item_count ? players->item_count : 1, sizeof *out->player_details);
  if (!out->player_details) qa_range_error("Out of memory");
  out->player_count = players->item_count;
  for (size_t i = 0; i < players->item_count; i++) {
    const qa_save_value *player = record(players->items[i]);
    out->player_details[i].name = xdup(text_of(qa_save_value_get(player, "name"), 1024));
    out->player_details[i].score = integer_of(qa_save_value_get(player, "score"), -2147483648.0, 2147483647.0);
    out->player_details[i].ping = integer_of(qa_save_value_get(player, "ping"), -2147483648.0, 2147483647.0);
  }
  out->wire.is_unified = false;
  return true;
}
static qa_q3_mirror_address mirror_of(const qa_net_address *a) {
  qa_q3_mirror_address m; memset(&m, 0, sizeof m);
  m.kind = QA_Q3_ADDR_IPV4;
  memcpy(m.host, a->ipv4, 4);
  m.port = a->port;
  return m;
}
qa_app_q3_browser_cache qa_app_read_q3_browser_cache(const char *serialized) {
  if (strlen(serialized) > (8u << 20)) qa_range_error("Browser cache is too large");
  qa_save_value *parsed = qa_parse_json_value((const uint8_t *)serialized, strlen(serialized), false);
  if (parsed == NULL) qa_range_error("Out of memory");
  const qa_save_value *root = record(parsed);
  const qa_save_value *version = qa_save_value_get(root, "version");
  const qa_save_value *protocol = qa_save_value_get(root, "protocol");
  if (version == NULL || version->kind != QA_SAVE_NUMBER || version->number != 1
      || protocol == NULL || protocol->kind != QA_SAVE_STRING || strcmp(protocol->text, "q3") != 0)
    qa_range_error("Unsupported browser cache version or protocol");
  const qa_save_value *values = array_of(qa_save_value_get(root, "entries"));
  if (values->item_count > 16384) qa_range_error("Too many browser cache entries");
  qa_app_q3_browser_cache cache; memset(&cache, 0, sizeof cache);
  cache.entries = calloc(values->item_count ? values->item_count : 1, sizeof *cache.entries);
  if (!cache.entries) qa_range_error("Out of memory");
  cache.entry_count = values->item_count;
  char **keys = calloc(values->item_count ? values->item_count : 1, sizeof *keys);
  if (!keys) qa_range_error("Out of memory");
  size_t master_count = 0, secondary_count = 0;
  for (size_t i = 0; i < values->item_count; i++) {
    const qa_save_value *entry = record(values->items[i]);
    qa_browser_entry *out = &cache.entries[i];
    out->address = address_of(qa_save_value_get(entry, "address"));
    char *key = qa_net_address_key(&out->address, true);
    if (!key) qa_range_error("Out of memory");
    for (size_t j = 0; j < i; j++) if (strcmp(keys[j], key) == 0) qa_range_error("Duplicate browser cache endpoint");
    keys[i] = key;
    const qa_save_value *sources = array_of(qa_save_value_get(entry, "sources"));
    out->sources = calloc(sources->item_count ? sources->item_count : 1, sizeof *out->sources);
    if (!out->sources) qa_range_error("Out of memory");
    out->source_count = sources->item_count;
    for (size_t j = 0; j < sources->item_count; j++) {
      const qa_save_value *sv = sources->items[j];
      if (sv->kind != QA_SAVE_STRING || (strcmp(sv->text, "master") != 0 && strcmp(sv->text, "secondary-master") != 0 && strcmp(sv->text, "favorite") != 0))
        qa_range_error("Invalid cached browser source");
      out->sources[j] = strcmp(sv->text, "master") == 0 ? "master" : strcmp(sv->text, "secondary-master") == 0 ? "secondary-master" : "favorite";
      for (size_t k = 0; k < j; k++) if (out->sources[k] == out->sources[j]) qa_range_error("Invalid cached browser memberships");
      if (strcmp(sv->text, "master") == 0) master_count++;
      if (strcmp(sv->text, "secondary-master") == 0) secondary_count++;
    }
    if (out->source_count == 0) qa_range_error("Invalid cached browser memberships");
    out->has_status = status_of(qa_save_value_get(entry, "status"), &out->status);
    const qa_save_value *ping = qa_save_value_get(entry, "pingMilliseconds");
    out->has_ping = !(ping != NULL && ping->kind == QA_SAVE_NULL);
    out->ping_ms = out->has_ping ? finite_of(ping) : 0;
    out->updated_at = finite_of(qa_save_value_get(entry, "updatedAt"));
  }
  if (master_count > 8192 || secondary_count > 128) qa_range_error("Too many cached master entries");
  const qa_save_value *view_value = qa_save_value_get(root, "view");
  if (view_value != NULL && view_value->kind != QA_SAVE_NULL) {
    const qa_save_value *lists = array_of(qa_save_value_get(record(view_value), "lists"));
    if (lists->item_count != 3) qa_range_error("Browser cache requires three UI lists");
    cache.has_view = true;
    cache.view.lists = calloc(3, sizeof *cache.view.lists);
    if (!cache.view.lists) qa_range_error("Out of memory");
    cache.view.list_count = 3;
    bool seen[4] = {false, false, false, false};
    for (size_t i = 0; i < 3; i++) {
      const qa_save_value *list = record(lists->items[i]);
      const qa_save_value *source_value = qa_save_value_get(list, "source");
      double source = source_value && source_value->kind == QA_SAVE_NUMBER ? source_value->number : -1;
      if ((source != 1 && source != 2 && source != 3) || seen[(int)source]) qa_range_error("Invalid browser cache list source");
      seen[(int)source] = true;
      const qa_save_value *rows = array_of(qa_save_value_get(list, "rows"));
      if (rows->item_count > (source == 2 ? 4096u : 128u)) qa_range_error("Too many browser cache UI rows");
      const char *membership = source == 1 ? "secondary-master" : source == 2 ? "master" : "favorite";
      qa_q3_browser_cache_list *out_list = &cache.view.lists[i];
      out_list->source = (int64_t)source;
      out_list->rows = calloc(rows->item_count ? rows->item_count : 1, sizeof *out_list->rows);
      if (!out_list->rows) qa_range_error("Out of memory");
      out_list->row_count = rows->item_count;
      char **row_keys = calloc(rows->item_count ? rows->item_count : 1, sizeof *row_keys);
      if (!row_keys) qa_range_error("Out of memory");
      for (size_t j = 0; j < rows->item_count; j++) {
        const qa_save_value *row = record(rows->items[j]);
        const char *name = text_of(qa_save_value_get(row, "name"), 31);
        /* Bytes are inherently <= 255 (TS charCode check). */
        qa_net_address endpoint = address_of(qa_save_value_get(row, "address"));
        char *key = qa_net_address_key(&endpoint, true);
        if (!key) qa_range_error("Out of memory");
        bool dup = false, member = false;
        for (size_t k = 0; k < j; k++) if (strcmp(row_keys[k], key) == 0) dup = true;
        for (size_t k = 0; k < cache.entry_count; k++) {
          if (strcmp(keys[k], key) != 0) continue;
          for (size_t s2 = 0; s2 < cache.entries[k].source_count; s2++)
            if (strcmp(cache.entries[k].sources[s2], membership) == 0) member = true;
        }
        if (dup || !member) qa_range_error("Browser cache UI row has duplicate or mismatched membership");
        row_keys[j] = key;
        out_list->rows[j].address = mirror_of(&endpoint);
        out_list->rows[j].name = xdup(name);
        out_list->rows[j].visible = (int64_t)integer_of(qa_save_value_get(row, "visible"), -2147483648.0, 2147483647.0);
        out_list->rows[j].ping = (int64_t)integer_of(qa_save_value_get(row, "ping"), -2147483648.0, 2147483647.0);
        qa_net_address_destroy(&endpoint);
      }
      for (size_t j = 0; j < rows->item_count; j++) free(row_keys[j]);
      free(row_keys);
    }
  }
  for (size_t i = 0; i < cache.entry_count; i++) free(keys[i]);
  free(keys);
  qa_save_value_free(parsed);
  return cache;
}
void qa_app_q3_browser_cache_free(qa_app_q3_browser_cache *cache) {
  for (size_t i = 0; i < cache->entry_count; i++) qa_app_browser_entry_dispose(&cache->entries[i]);
  free(cache->entries);
  if (cache->has_view) qa_q3_browser_cache_view_destroy(&cache->view);
  memset(cache, 0, sizeof *cache);
}
static void append_str(char **buf, const char *add) {
  size_t a = strlen(*buf), b = strlen(add);
  char *n = realloc(*buf, a + b + 1);
  if (!n) qa_range_error("Out of memory");
  memcpy(n + a, add, b + 1); *buf = n;
}
static void append_json_string(char **buf, const char *value) {
  append_str(buf, "\"");
  for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
    char tmp[8];
    switch (*p) {
      case '"': append_str(buf, "\\\""); break;
      case '\\': append_str(buf, "\\\\"); break;
      case '\n': append_str(buf, "\\n"); break;
      case '\r': append_str(buf, "\\r"); break;
      case '\t': append_str(buf, "\\t"); break;
      default:
        if (*p < 0x20) { snprintf(tmp, sizeof tmp, "\\u%04x", *p); append_str(buf, tmp); }
        else { tmp[0] = (char)*p; tmp[1] = '\0'; append_str(buf, tmp); }
    }
  }
  append_str(buf, "\"");
}
static void append_num(char **buf, double v) { char *n = qa_input_js_number(v); if (!n) qa_range_error("Out of memory"); append_str(buf, n); free(n); }
static void append_address(char **buf, const qa_net_address *a) {
  char tmp[128];
  if (a->kind == QA_NET_IPV4) {
    snprintf(tmp, sizeof tmp, "{\"kind\":\"ipv4\",\"host\":[%u,%u,%u,%u],\"port\":%lld}", a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3], (long long)a->port);
    append_str(buf, tmp);
  } else if (a->kind == QA_NET_IPV6) {
    append_str(buf, "{\"kind\":\"ipv6\",\"host\":");
    append_json_string(buf, a->host ? a->host : "");
    snprintf(tmp, sizeof tmp, ",\"port\":%lld}", (long long)a->port);
    append_str(buf, tmp);
  } else if (a->kind == QA_NET_IPX) {
    snprintf(tmp, sizeof tmp, "{\"kind\":\"ipx\",\"network\":%u,\"node\":[%u,%u,%u,%u,%u,%u],\"port\":%lld}",
             a->network, a->node[0], a->node[1], a->node[2], a->node[3], a->node[4], a->node[5], (long long)a->port);
    append_str(buf, tmp);
  } else {
    append_str(buf, "{\"kind\":\"loopback\",\"id\":");
    append_json_string(buf, a->host ? a->host : "");
    append_str(buf, "}");
  }
}
char *qa_app_write_q3_browser_cache(const qa_browser_entry *entries, size_t entry_count, const qa_q3_browser_cache_view *view) {
  char *text = xdup("{\"version\":1,\"protocol\":\"q3\",\"entries\":[");
  bool first_entry = true;
  for (size_t i = 0; i < entry_count; i++) {
    const qa_browser_entry *entry = &entries[i];
    bool keep = false;
    for (size_t j = 0; j < entry->source_count; j++)
      if (strcmp(entry->sources[j], "master") == 0 || strcmp(entry->sources[j], "secondary-master") == 0 || strcmp(entry->sources[j], "favorite") == 0) keep = true;
    if (!keep) continue;
    if (!first_entry) append_str(&text, ",");
    first_entry = false;
    append_str(&text, "{\"address\":");
    append_address(&text, &entry->address);
    append_str(&text, ",\"sources\":[");
    bool first_source = true;
    for (size_t j = 0; j < entry->source_count; j++) {
      const char *s = entry->sources[j];
      if (strcmp(s, "master") != 0 && strcmp(s, "secondary-master") != 0 && strcmp(s, "favorite") != 0) continue;
      if (!first_source) append_str(&text, ",");
      first_source = false;
      append_json_string(&text, s);
    }
    append_str(&text, "],\"status\":");
    if (!entry->has_status) append_str(&text, "null");
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
      char tmp[64];
      snprintf(tmp, sizeof tmp, "{\"source\":%lld,\"rows\":[", (long long)view->lists[i].source);
      append_str(&text, tmp);
      for (size_t j = 0; j < view->lists[i].row_count; j++) {
        if (j) append_str(&text, ",");
        const qa_q3_browser_cache_row *row = &view->lists[i].rows[j];
        qa_net_address a; memset(&a, 0, sizeof a);
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
  qa_app_q3_browser_cache check = qa_app_read_q3_browser_cache(text);
  qa_app_q3_browser_cache_free(&check);
  return text;
}
