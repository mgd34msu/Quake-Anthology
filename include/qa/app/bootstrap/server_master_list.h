/* Port of src/app/bootstrap/server-master-list.ts. The TS global fetch is a
 * synchronous libcurl GET (PORTING async rule), redirects followed as fetch does;
 * the AbortSignal is a deadline on the performance.now() clock
 * (qa_net_default_now) plus an aborted flag. Throws are qa_raise with the TS
 * class name and message. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_MASTER_LIST_H
#define QA_APP_BOOTSTRAP_SERVER_MASTER_LIST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* TS AbortSignal as the browser uses it: aborted by its 4 s timer (deadline_ms,
 * qa_net_default_now milliseconds; <= 0 means no deadline) or by the owner. */
typedef struct qa_app_master_list_signal { double deadline_ms; bool aborted; } qa_app_master_list_signal;
/* TS `skipped?: (count) => void`; NULL is the TS undefined. */
typedef void (*qa_app_master_list_skipped_fn)(void *ctx, double count);
/* TS fetchServerMasterList(url, signal, skipped): malloc'd array of malloc'd
 * addresses (free with qa_app_server_master_list_free). Raises "Master lists require
 * HTTP or HTTPS", "Master list returned HTTP <status>", "Master list exceeds 1 MiB",
 * "Master list exceeds 4096 servers", the TextDecoder TypeError for invalid UTF-8,
 * an AbortError when the signal fires, and the transfer error otherwise. */
char **qa_app_fetch_server_master_list(const char *url, const qa_app_master_list_signal *signal,
                                       qa_app_master_list_skipped_fn skipped, void *skipped_ctx, size_t *out_count);
/* The body half of fetchServerMasterList (decode, split, trim, dedupe, validate),
 * exposed for tests; raises as above. */
char **qa_app_parse_server_master_list(const uint8_t *bytes, size_t length, qa_app_master_list_skipped_fn skipped,
                                       void *skipped_ctx, size_t *out_count);
void qa_app_server_master_list_free(char **servers, size_t count);
/* JS String.prototype.trim over UTF-8 text (WhiteSpace and LineTerminator code
 * points); malloc'd, NULL on OOM. C-only helper shared with the browser. */
char *qa_app_js_trim(const char *text);
char *qa_app_js_trim_range(const char *text, size_t length);
/* Byte width of the JS whitespace code point at text[at] (0 when none). */
size_t qa_app_js_space_at(const char *text, size_t length, size_t at);
#endif
