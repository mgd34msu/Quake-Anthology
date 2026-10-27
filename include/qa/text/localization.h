/*
 * Port of src/text/localization.ts: Quake rerelease localization
 * grammar and substitutions (LocalizationTable, LocalizationCatalog,
 * Loc_LanguageFromLocale).
 * C accommodations:
 *  - Strings are NUL-terminated UTF-8 bytes. TS indices/counts are
 *    UTF-16 code units; the C port slices by byte, which is internally
 *    coherent (same visible result) but truncation limits near the
 *    64/1024 caps can differ for non-ASCII input. Invalid UTF-8 in
 *    loaded files is kept as bytes instead of TextDecoder replacement.
 *  - LibLog becomes warn/info function pointers plus a void *ctx;
 *    info may be NULL (TS info is optional). A NULL log is no log.
 *  - TS `profile` string literals become qa_loc_profile; the string
 *    form is available from qa_localization_table_profile_name().
 *  - Lookup/localize results are malloc'd; the caller frees. A NULL
 *    return from lookup is TS null; NULL from localize* is OOM only.
 *  - The TS init(bytes) method (a reload alias) is
 *    qa_localization_table_load(); qa_localization_table_init() is
 *    the C constructor.
 * GPL-2.0-or-later.
 */
#ifndef QA_TEXT_LOCALIZATION_H
#define QA_TEXT_LOCALIZATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h" /* qa_byte_view */

#define QA_LOC_MAX_ARGS 8

typedef struct qa_lib_log {
  void (*warn)(void *ctx, const char *text);
  void (*info)(void *ctx, const char *text); /* may be NULL */
  void *ctx;
} qa_lib_log;

typedef enum qa_loc_profile {
  QA_LOC_Q1_RERELEASE = 0,
  QA_LOC_Q2_RERELEASE = 1
} qa_loc_profile;

typedef enum qa_loc_duplicate_keys {
  QA_LOC_DUPLICATES_UNSPECIFIED = 0, /* TS: option absent */
  QA_LOC_DUPLICATES_FIRST = 1,
  QA_LOC_DUPLICATES_LAST = 2
} qa_loc_duplicate_keys;

/* TS LocReloadOptions. `platform` NULL is TS undefined. */
typedef struct qa_loc_reload_options {
  const char *platform;
  const qa_lib_log *log;
  qa_loc_duplicate_keys duplicate_keys;
} qa_loc_reload_options;

/* One parsed `{n}` argument; start/end are byte offsets (see above). */
typedef struct qa_loc_arg {
  int arg_index;
  size_t start;
  size_t end;
} qa_loc_arg;

typedef struct qa_loc_string {
  char *format; /* owned by the table */
  qa_loc_arg arguments[QA_LOC_MAX_ARGS];
  size_t argument_count;
} qa_loc_string;

typedef struct qa_loc_entry {
  char *key; /* owned */
  qa_loc_string value;
} qa_loc_entry;

typedef struct qa_localization_table {
  qa_loc_profile profile;
  qa_loc_entry *entries; /* owned; TS Map, insertion order kept */
  size_t entry_count;
  size_t entry_capacity;
} qa_localization_table;

/* TS LocLoadTier. `has_base` false is a TS null base. */
typedef struct qa_loc_load_tier {
  bool has_base;
  qa_byte_view base;
  const qa_byte_view *mods;
  size_t mod_count;
} qa_loc_load_tier;

extern const char *const qa_loc_known_languages[6];
#define QA_LOC_KNOWN_LANGUAGE_COUNT 6

/* TS Loc_LanguageFromLocale; `tag` may be NULL. Static result. */
const char *qa_loc_language_from_locale(const char *tag);

void qa_localization_table_init(qa_localization_table *table, qa_loc_profile profile);
void qa_localization_table_destroy(qa_localization_table *table);
const char *qa_localization_table_profile_name(const qa_localization_table *table);

/* Borrowed entry, or NULL (TS undefined). */
const qa_loc_string *qa_localization_table_find(const qa_localization_table *table,
                                                const char *base);
size_t qa_localization_table_size(const qa_localization_table *table);
void qa_localization_table_clear(qa_localization_table *table);

/* Malloc'd result, or NULL for TS null (lookup) / OOM (both). */
char *qa_localization_table_lookup(qa_localization_table *table, const char *key,
                                   const char *const *args, size_t arg_count);
/* TS localizeSource. `args` NULL or an args[i] NULL is a TS null arg. */
char *qa_localization_table_localize_source(qa_localization_table *table, const char *base,
                                            bool allow_in_place,
                                            const char *const *args, int64_t num_args,
                                            int64_t output_length,
                                            const qa_lib_log *log);
/* TS localize. A negative or non-safe output_bytes aborts (RangeError). */
char *qa_localization_table_localize(qa_localization_table *table, const char *base,
                                     const char *const *args, size_t arg_count,
                                     bool allow_in_place, int64_t output_bytes,
                                     const qa_lib_log *log);
/* TS localizeBytes: malloc'd bytes (may cut a UTF-8 sequence);
 * *out_length is the byte count. The buffer is NUL-terminated after
 * the returned bytes for convenience. NULL only on OOM. */
uint8_t *qa_localization_table_localize_bytes(qa_localization_table *table,
                                              const char *base,
                                              const char *const *args, size_t arg_count,
                                              bool allow_in_place, int64_t output_bytes,
                                              size_t *out_length);

/* `bytes` NULL data is a TS null file. A NULL options pointer is {}. */
int64_t qa_localization_table_reload(qa_localization_table *table, qa_byte_view bytes,
                                     const qa_loc_reload_options *options);
int64_t qa_localization_table_merge(qa_localization_table *table, qa_byte_view bytes,
                                    const qa_loc_reload_options *options);
int64_t qa_localization_table_load_ordered(qa_localization_table *table,
                                           qa_loc_load_tier primary,
                                           qa_loc_load_tier fallback,
                                           const qa_loc_reload_options *options);
/* TS init(bytes): identical to reload. */
int64_t qa_localization_table_load(qa_localization_table *table, qa_byte_view bytes,
                                   const qa_loc_reload_options *options);

/* TS LocalizationCatalog: a table bound to its viewing seat. */
typedef struct qa_localization_catalog {
  qa_localization_table table;
  qa_seat_id seat;
} qa_localization_catalog;

void qa_localization_catalog_init(qa_localization_catalog *catalog, qa_seat_id seat,
                                  qa_loc_profile profile);
void qa_localization_catalog_destroy(qa_localization_catalog *catalog);

#endif
