/* Port of src/foundation/strings.ts. GPL-2.0-or-later. */
#include "qa/foundation/strings.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static int starts_with(const char *text, const char *prefix) {
  return strncmp(text, prefix, strlen(prefix)) == 0;
}

uint32_t qa_quake_name_flags(const char *text) {
  uint32_t flags = 0;
  size_t length = strlen(text);
  if (starts_with(text, "*")) flags |= QA_NAME_SEXED_SOUND;
  if (starts_with(text, "monster_")) flags |= QA_NAME_MONSTER;
  if (starts_with(text, "info_player_")) flags |= QA_NAME_PLAYER_START;
  if (starts_with(text, "item_flag_")) flags |= QA_NAME_ITEM_FLAG;
  if (starts_with(text, "info_flag")) flags |= QA_NAME_INFO_FLAG;
  if (starts_with(text, "func")) flags |= QA_NAME_FUNC;
  if (starts_with(text, "q1:ammo/")) flags |= QA_NAME_Q1_AMMO;
  if (length > 2 && text[0] == '*' && (text[length - 1] == 'p' || text[length - 1] == 'P')) {
    flags |= QA_NAME_BRUSH_PATH;
  }
  if (strcmp(text, "red") == 0 || strcmp(text, "team:red") == 0) {
    flags |= QA_NAME_TEAM_RED_ALIAS | QA_NAME_TEAM_RED_EXACT;
  } else if (strcmp(text, "blue") == 0 || strcmp(text, "team:blue") == 0) {
    flags |= QA_NAME_TEAM_BLUE_ALIAS | QA_NAME_TEAM_BLUE_EXACT;
  } else if (strcmp(text, "5") == 0 || strcmp(text, "q2:1") == 0 || strcmp(text, "q3:1") == 0) {
    flags |= QA_NAME_TEAM_RED_ALIAS;
  } else if (strcmp(text, "14") == 0 || strcmp(text, "q2:2") == 0 || strcmp(text, "q3:2") == 0) {
    flags |= QA_NAME_TEAM_BLUE_ALIAS;
  } else if (strcmp(text, "q2") == 0 || strcmp(text, "q3") == 0) {
    flags |= QA_NAME_TEAM_COLOR_NAMESPACE;
  }
  return flags;
}

struct qa_string_table {
  qa_string_classify_fn classify;
  void *classify_ctx;
  char **texts;       /* texts[0] == ""; ids are indices */
  size_t texts_count; /* includes the reserved slot 0 */
  size_t texts_capacity;
  uint32_t *flags;
  size_t flags_capacity;
  uint32_t *buckets;
  size_t bucket_count;
};

/* Bucket 0 stores id+1 so QA_STRING_NONE (0) marks an empty bucket. */
static uint32_t hash_text(const char *text) {
  uint32_t hash = 2166136261u;
  for (const uint8_t *p = (const uint8_t *)text; *p != 0; p++) {
    hash ^= *p;
    hash *= 16777619u;
  }
  return hash;
}

static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = (char *)malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static int buckets_rehash(qa_string_table *table, size_t new_count) {
  uint32_t *buckets = (uint32_t *)calloc(new_count, sizeof(uint32_t));
  if (buckets == NULL) return -1;
  for (size_t i = 0; i < table->bucket_count; i++) {
    uint32_t stored = table->buckets[i];
    if (stored == 0) continue;
    uint32_t id = stored - 1;
    size_t at = hash_text(table->texts[id]) & (new_count - 1);
    while (buckets[at] != 0) at = (at + 1) & (new_count - 1);
    buckets[at] = stored;
  }
  free(table->buckets);
  table->buckets = buckets;
  table->bucket_count = new_count;
  return 0;
}

qa_string_table *qa_string_table_create(qa_string_classify_fn classify, void *classify_ctx) {
  qa_string_table *table = (qa_string_table *)calloc(1, sizeof *table);
  if (table == NULL) return NULL;
  table->classify = classify;
  table->classify_ctx = classify_ctx;
  table->texts_capacity = 64;
  table->texts = (char **)malloc(table->texts_capacity * sizeof(char *));
  table->flags_capacity = 64;
  table->flags = (uint32_t *)calloc(table->flags_capacity, sizeof(uint32_t));
  table->bucket_count = 128;
  table->buckets = (uint32_t *)calloc(table->bucket_count, sizeof(uint32_t));
  char *empty = dup_text("");
  if (table->texts == NULL || table->flags == NULL || table->buckets == NULL || empty == NULL) {
    free(table->texts);
    free(table->flags);
    free(table->buckets);
    free(empty);
    free(table);
    return NULL;
  }
  table->texts[0] = empty;
  table->texts_count = 1;
  return table;
}

void qa_string_table_destroy(qa_string_table *table) {
  if (table == NULL) return;
  for (size_t i = 0; i < table->texts_count; i++) free(table->texts[i]);
  free(table->texts);
  free(table->flags);
  free(table->buckets);
  free(table);
}

size_t qa_string_table_count(const qa_string_table *table) {
  return table->texts_count - 1;
}

uint32_t qa_string_table_find(const qa_string_table *table, const char *text) {
  size_t at = hash_text(text) & (table->bucket_count - 1);
  for (;;) {
    uint32_t stored = table->buckets[at];
    if (stored == 0) return QA_STRING_NONE;
    uint32_t id = stored - 1;
    if (strcmp(table->texts[id], text) == 0) return id;
    at = (at + 1) & (table->bucket_count - 1);
  }
}

uint32_t qa_string_table_intern(qa_string_table *table, const char *text) {
  uint32_t existing = qa_string_table_find(table, text);
  if (existing != QA_STRING_NONE) return existing;
  if (table->texts_count > 0xffffffffu) {
    qa_range_error("String identity space exhausted");
  }
  uint32_t id = (uint32_t)table->texts_count;
  if (table->texts_count == table->texts_capacity) {
    size_t grown = table->texts_capacity * 2;
    char **texts = (char **)realloc(table->texts, grown * sizeof(char *));
    if (texts == NULL) qa_range_error("String identity space exhausted");
    table->texts = texts;
    table->texts_capacity = grown;
  }
  char *copy = dup_text(text);
  if (copy == NULL) qa_range_error("String identity space exhausted");
  table->texts[table->texts_count++] = copy;
  if (id >= table->flags_capacity) {
    size_t grown = table->flags_capacity * 2;
    uint32_t *flags = (uint32_t *)realloc(table->flags, grown * sizeof(uint32_t));
    if (flags == NULL) qa_range_error("String identity space exhausted");
    memset(flags + table->flags_capacity, 0,
           (grown - table->flags_capacity) * sizeof(uint32_t));
    table->flags = flags;
    table->flags_capacity = grown;
  }
  table->flags[id] = table->classify != NULL
                         ? table->classify(text, table->classify_ctx)
                         : qa_quake_name_flags(text);
  /* Keep load factor below 3/4 (TypeScript Map growth is unobservable). */
  if ((table->texts_count - 1) * 4 >= table->bucket_count * 3) {
    if (buckets_rehash(table, table->bucket_count * 2) != 0) {
      qa_range_error("String identity space exhausted");
    }
  }
  size_t at = hash_text(text) & (table->bucket_count - 1);
  while (table->buckets[at] != 0) at = (at + 1) & (table->bucket_count - 1);
  table->buckets[at] = id + 1;
  return id;
}

void qa_string_table_bind_names(qa_string_table *table, const char *const *names,
                                size_t count, uint32_t *out_ids) {
  for (size_t i = 0; i < count; i++) out_ids[i] = qa_string_table_intern(table, names[i]);
}

const char *qa_string_table_text(const qa_string_table *table, uint32_t id) {
  if (id >= table->texts_count) {
    qa_range_error("String id is outside this table: %u", id);
  }
  return table->texts[id];
}

uint32_t qa_string_table_name_flags(const qa_string_table *table, uint32_t id) {
  if (id >= table->flags_capacity) return 0;
  return table->flags[id];
}
