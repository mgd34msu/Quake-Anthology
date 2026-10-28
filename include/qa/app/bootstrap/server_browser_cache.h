/* Port of src/app/bootstrap/server-browser-cache.ts. JSON via landed
 * qa_parse_json_value; view uses the landed browser-view cache types.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_BROWSER_CACHE_H
#define QA_APP_BOOTSTRAP_SERVER_BROWSER_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/network/q3/browser_view.h"
#include "qa/network/services/discovery.h"
typedef struct qa_app_q3_browser_cache {
  qa_browser_entry *entries; size_t entry_count;
  bool has_view; qa_q3_browser_cache_view view;
} qa_app_q3_browser_cache;
qa_app_q3_browser_cache qa_app_read_q3_browser_cache(const char *serialized);
void qa_app_q3_browser_cache_free(qa_app_q3_browser_cache *cache);
void qa_app_browser_entry_dispose(qa_browser_entry *entry);
/* Malloc'd JSON; validates by re-reading, as TS. view NULL = TS null. */
char *qa_app_write_q3_browser_cache(const qa_browser_entry *entries, size_t entry_count, const qa_q3_browser_cache_view *view);
#endif
