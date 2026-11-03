/*
 * Port of src/settings/server/library.ts: lists the same named
 * files written by the server settings menu (JSON files under
 * the store root). A missing directory is an empty list; other
 * readdir failures abort as the TS throws. Sorting is
 * String.localeCompare (qa_js_locale_compare). Entries
 * are owned; free with qa_server_profile_entries_free.
 * GPL-2.0-or-later.
 */
#ifndef QA_SETTINGS_SERVER_LIBRARY_H
#define QA_SETTINGS_SERVER_LIBRARY_H

#include <stddef.h>

#include "qa/settings/config.h"

/* TS ServerProfileEntry. */
typedef struct qa_server_profile_entry {
  char *name;
  char *path;
} qa_server_profile_entry;

qa_server_profile_entry *qa_list_server_profiles(const qa_config_store *store,
                                                 size_t *out_count);
void qa_server_profile_entries_free(qa_server_profile_entry *entries, size_t count);

#endif
