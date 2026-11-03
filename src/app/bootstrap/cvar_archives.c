/* Port of src/app/bootstrap/cvar-archives.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/cvar_archives.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
/* encodeURIComponent for one owner part. */
static char *encode_part(const char *part) {
  size_t cap = strlen(part) * 3 + 1, out = 0;
  char *encoded = malloc(cap);
  for (const unsigned char *p = (const unsigned char *)part; *p; p++) {
    unsigned char c = *p;
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')')
      encoded[out++] = (char)c;
    else out += (size_t)snprintf(encoded + out, cap - out, "%%%02X", c);
  }
  encoded[out] = '\0';
  return encoded;
}
static char *archive_path(const char *const *owner, size_t count) {
  size_t cap = 64;
  for (size_t i = 0; i < count; i++) cap += strlen(owner[i]) * 3 + 1;
  char *path = malloc(cap);
  size_t out = (size_t)snprintf(path, cap, "cvars/");
  for (size_t i = 0; i < count; i++) {
    char *part = encode_part(owner[i]);
    out += (size_t)snprintf(path + out, cap - out, "%s%s", i ? "/" : "", part);
    free(part);
  }
  snprintf(path + out, cap - out, ".json");
  return path;
}
static const char *dialect_text(qa_command_dialect dialect) {
  switch (dialect) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE: return "q1-netquake";
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD: return "q1-quakeworld";
    case QA_COMMAND_DIALECT_Q2_CLASSIC: return "q2-classic";
    case QA_COMMAND_DIALECT_Q2_RERELEASE: return "q2-rerelease";
    case QA_COMMAND_DIALECT_Q3: return "q3";
  }
  return "q3";
}
typedef struct archive_read { const qa_save_value *value; const char *dialect_name; qa_cvar_archive_entry *entries; size_t count; } archive_read;
static void free_partial_entries(void *ctx) {
  archive_read *read = ctx;
  qa_app_cvar_archive_entries_free(read->entries, read->count);
  read->entries = NULL; read->count = 0;
}
static void destroy_reader(void *reader) { qa_save_reader_destroy(reader); }
static void read_archive(void *ctx) {
  archive_read *read = ctx;
  qa_save_reader reader;
  qa_save_reader_init(&reader, read->value, "cvar archive");
  qa_cleanup reader_guard;
  qa_cleanup_push(&reader_guard, destroy_reader, &reader);
  qa_save_reader field_reader;
  qa_save_reader_field(&reader, "version", &field_reader);
  qa_save_reader_literal_number(&field_reader, 1);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&reader, "dialect", &field_reader);
  qa_save_reader_literal_string(&field_reader, read->dialect_name);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&reader, "entries", &field_reader);
  qa_cleanup field_guard;
  qa_cleanup_push(&field_guard, destroy_reader, &field_reader);
  size_t count = qa_save_reader_list_count(&field_reader);
  read->entries = calloc(count ? count : 1, sizeof *read->entries);
  if (read->entries == NULL) qa_range_error("Out of memory");
  qa_cleanup entries_guard;
  qa_cleanup_push(&entries_guard, free_partial_entries, read);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, name_reader, value_reader;
    qa_save_reader_item(&field_reader, i, &item);
    qa_save_reader_field(&item, "name", &name_reader);
    const char *name = qa_save_reader_string(&name_reader);
    qa_save_reader_field(&item, "value", &value_reader);
    const char *text = qa_save_reader_string(&value_reader);
    read->entries[i].name = dup(name);
    read->entries[i].value = dup(text);
    read->count = i + 1;
    qa_save_reader_destroy(&value_reader);
    qa_save_reader_destroy(&name_reader);
    qa_save_reader_destroy(&item);
  }
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (strcmp(read->entries[i].name, read->entries[j].name) == 0) qa_save_reader_fail(&reader, "duplicate archived cvar");
  qa_cleanup_pop(&entries_guard);
  qa_cleanup_pop(&field_guard);
  qa_save_reader_destroy(&field_reader);
  qa_cleanup_pop(&reader_guard);
  qa_save_reader_destroy(&reader);
}
qa_cvar_archive_entry *qa_app_load_cvar_archive(const qa_config_store *store, const char *const *owner,
                                                size_t owner_count, qa_command_dialect dialect, size_t *out_count) {
  *out_count = 0;
  char *path = archive_path(owner, owner_count);
  size_t length = 0;
  char *text = qa_config_store_load_text(store, path, &length);
  free(path);
  if (!text) return NULL;
  qa_save_value *value = qa_parse_json_value((const uint8_t *)text, length, true);
  free(text);
  if (!value) return NULL; /* non-JSON restores nothing */
  archive_read read = {value, dialect_text(dialect), NULL, 0};
  qa_caught_error caught;
  bool ok = qa_try(read_archive, &read, &caught);
  qa_save_value_free(value);
  /* A malformed archive restores nothing; the registry's own defaults apply. */
  if (!ok) return NULL;
  *out_count = read.count;
  return read.entries;
}
void qa_app_cvar_archive_entries_free(qa_cvar_archive_entry *entries, size_t count) {
  if (!entries) return;
  for (size_t i = 0; i < count; i++) { free(entries[i].name); free(entries[i].value); }
  free(entries);
}
/* Minimal JSON.stringify for strings (archive names/values). */
static void append_json_string(char **buf, size_t *len, size_t *cap, const char *text) {
  size_t need = *len + strlen(text) * 6 + 3;
  if (need > *cap) { *cap = need * 2; *buf = realloc(*buf, *cap); }
  (*buf)[(*len)++] = '"';
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') { (*buf)[(*len)++] = '\\'; (*buf)[(*len)++] = (char)c; }
    else if (c == '\n') { (*len) += (size_t)snprintf(*buf + *len, *cap - *len, "\\n"); }
    else if (c == '\r') { (*len) += (size_t)snprintf(*buf + *len, *cap - *len, "\\r"); }
    else if (c == '\t') { (*len) += (size_t)snprintf(*buf + *len, *cap - *len, "\\t"); }
    else if (c < 0x20) { (*len) += (size_t)snprintf(*buf + *len, *cap - *len, "\\u%04x", c); }
    else (*buf)[(*len)++] = (char)c;
  }
  (*buf)[(*len)++] = '"';
  (*buf)[*len] = '\0';
}
static void append_text(char **buf, size_t *len, size_t *cap, const char *text) {
  size_t need = *len + strlen(text) + 1;
  if (need > *cap) { *cap = need * 2; *buf = realloc(*buf, *cap); }
  memcpy(*buf + *len, text, strlen(text) + 1);
  *len += strlen(text);
}
void qa_app_save_cvar_archive(const qa_config_store *store, const char *const *owner, size_t owner_count,
                              qa_cvar_registry *registry, const qa_cvar_archive_entry *entries, size_t entry_count) {
  qa_cvar_archive_entry *owned = NULL;
  if (!entries) {
    owned = qa_cvar_registry_archive_entries(registry, NULL, NULL, &entry_count);
    entries = owned;
  }
  char *buf = malloc(256); size_t len = 0, cap = 256; buf[0] = '\0';
  append_text(&buf, &len, &cap, "{\"version\":1,\"dialect\":");
  append_json_string(&buf, &len, &cap, dialect_text(qa_cvar_registry_dialect(registry)));
  append_text(&buf, &len, &cap, ",\"entries\":[");
  for (size_t i = 0; i < entry_count; i++) {
    if (i) append_text(&buf, &len, &cap, ",");
    append_text(&buf, &len, &cap, "{\"name\":");
    append_json_string(&buf, &len, &cap, entries[i].name);
    append_text(&buf, &len, &cap, ",\"value\":");
    append_json_string(&buf, &len, &cap, entries[i].value);
    append_text(&buf, &len, &cap, "}");
  }
  append_text(&buf, &len, &cap, "]}\n");
  char *path = archive_path(owner, owner_count);
  qa_config_store_dump(store, path, buf);
  free(path);
  free(buf);
  if (owned) qa_app_cvar_archive_entries_free(owned, entry_count);
}
/* JS String.prototype.trim over [start, end): returns the trimmed bounds. */
static void js_trim_bounds(const char *start, const char *end, const char **out_start, const char **out_end) {
  size_t n;
  while (start < end && (n = qa_js_white_space_length(start)) != 0 && start + n <= end) start += n;
  for (;;) {
    const char *p = start, *last = NULL;
    /* find the last white-space character that ends exactly at `end` */
    while (p < end) {
      size_t w = qa_js_white_space_length(p);
      if (w != 0 && p + w == end) { last = p; break; }
      p += w != 0 ? w : 1;
    }
    if (last == NULL) break;
    end = last;
  }
  *out_start = start; *out_end = end;
}
static bool archive_command(const char *first, size_t length) {
  static const char *const names[] = {"seta", "set", "sets", "setu", "bind", "unbind", "unbindall"};
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    if (strlen(names[i]) != length) continue;
    bool same = true;
    for (size_t j = 0; j < length && same; j++) same = tolower((unsigned char)first[j]) == names[i][j];
    if (same) return true;
  }
  return false;
}
/* /^[^\s";]+ +"[^"]*"$/ over the trimmed line. */
static bool bare_variable_shape(const char *start, const char *end) {
  const char *p = start;
  while (p < end && *p != '"' && *p != ';' && qa_js_white_space_length(p) == 0) p++;
  if (p == start || p >= end || *p != ' ') return false;
  while (p < end && *p == ' ') p++;
  if (p >= end || *p != '"') return false;
  p++;
  while (p < end && *p != '"') p++;
  return p < end && p + 1 == end;
}
char *qa_app_archive_data_text(const char *text, bool (*is_command)(void *ctx, const char *name), void *ctx) {
  size_t cap = strlen(text) + 2, length = 0;
  char *out = malloc(cap);
  if (out == NULL) qa_range_error("Out of memory");
  out[0] = '\0';
  const char *line = text;
  for (;;) {
    const char *line_end = line;
    while (*line_end != '\0' && *line_end != '\r' && *line_end != '\n') line_end++;
    const char *start, *end;
    js_trim_bounds(line, line_end, &start, &end);
    if (start < end && !(end - start >= 2 && start[0] == '/' && start[1] == '/')) {
      bool quoted = false, chained = false;
      for (const char *p = start; p < end; p++) {
        if (*p == '"') quoted = !quoted;
        else if (*p == ';' && !quoted) chained = true;
      }
      const char *first_end = start;
      while (first_end < end && qa_js_white_space_length(first_end) == 0) first_end++;
      size_t first_length = (size_t)(first_end - start);
      bool keep = !quoted && !chained;
      if (keep && !archive_command(start, first_length)) {
        /* Quake I writes a bare `name "value"` line for an archived variable. */
        bool bare = bare_variable_shape(start, end);
        if (bare) {
          char *first = malloc(first_length + 1);
          if (first == NULL) qa_range_error("Out of memory");
          memcpy(first, start, first_length);
          first[first_length] = '\0';
          bare = !is_command(ctx, first);
          free(first);
        }
        keep = bare;
      }
      if (keep) {
        memcpy(out + length, start, (size_t)(end - start));
        length += (size_t)(end - start);
        out[length++] = '\n';
        out[length] = '\0';
      }
    }
    if (*line_end == '\0') break;
    line = line_end + (line_end[0] == '\r' && line_end[1] == '\n' ? 2 : 1);
  }
  return out;
}
