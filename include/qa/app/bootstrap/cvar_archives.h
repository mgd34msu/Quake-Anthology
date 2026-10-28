/* Port of src/app/bootstrap/cvar-archives.ts. Async store I/O is
 * synchronous against the landed qa_config_store. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CVAR_ARCHIVES_H
#define QA_APP_BOOTSTRAP_CVAR_ARCHIVES_H
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/settings/config.h"
/* TS CvarArchiveOwner tuple: owner[0] is source|client|input|movement|fallback. */
/* Malloc'd entries (name/value owned); free with qa_app_cvar_archive_entries_free. */
qa_cvar_archive_entry *qa_app_load_cvar_archive(const qa_config_store *store, const char *const *owner,
                                                size_t owner_count, qa_command_dialect dialect, size_t *out_count);
void qa_app_cvar_archive_entries_free(qa_cvar_archive_entry *entries, size_t count);
/* entries NULL uses registry archive entries (TS default argument). */
void qa_app_save_cvar_archive(const qa_config_store *store, const char *const *owner, size_t owner_count,
                              qa_cvar_registry *registry, const qa_cvar_archive_entry *entries, size_t entry_count);
#endif
