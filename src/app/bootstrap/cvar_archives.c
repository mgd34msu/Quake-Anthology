/* Port of src/app/bootstrap/cvar-archives.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/cvar_archives.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
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
  qa_save_reader reader;
  qa_save_reader_init(&reader, value, "cvar archive");
  qa_save_reader field_reader;
  qa_save_reader_field(&reader, "version", &field_reader);
  qa_save_reader_literal_number(&field_reader, 1);
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&reader, "dialect", &field_reader);
  qa_save_reader_literal_string(&field_reader, dialect_text(dialect));
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_field(&reader, "entries", &field_reader);
  size_t count = qa_save_reader_list_count(&field_reader);
  qa_cvar_archive_entry *entries = calloc(count ? count : 1, sizeof *entries);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item, name_reader, value_reader;
    qa_save_reader_item(&field_reader, i, &item);
    qa_save_reader_field(&item, "name", &name_reader);
    entries[i].name = dup(qa_save_reader_string(&name_reader));
    qa_save_reader_destroy(&name_reader);
    qa_save_reader_field(&item, "value", &value_reader);
    entries[i].value = dup(qa_save_reader_string(&value_reader));
    qa_save_reader_destroy(&value_reader);
    qa_save_reader_destroy(&item);
  }
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (strcmp(entries[i].name, entries[j].name) == 0) qa_save_reader_fail(&reader, "duplicate archived cvar");
  qa_save_reader_destroy(&field_reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(value);
  *out_count = count;
  return entries;
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
