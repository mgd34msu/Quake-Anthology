/* Port of src/app/bootstrap/server-master-list.ts. fetch/AbortSignal
 * -> synchronous host fetch returning status+body bytes. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_MASTER_LIST_H
#define QA_APP_BOOTSTRAP_SERVER_MASTER_LIST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_app_master_list_host {
  void *ctx;
  /* Fills status and malloc'd body; has_body false mirrors missing body;
   * is_byte_stream false mirrors a non-Uint8Array chunk stream. */
  bool (*fetch)(void *ctx, const char *url, long *out_status, uint8_t **out_body, size_t *out_length,
                bool *out_has_body, bool *out_is_byte_stream);
} qa_app_master_list_host;
/* Malloc'd string array; caller frees each + array. */
char **qa_app_fetch_server_master_list(qa_app_master_list_host host, const char *url, size_t *out_count);
/* Pure body parsing (split/trim/dedupe/validate), exposed for tests. */
char **qa_app_parse_server_master_list(const uint8_t *bytes, size_t length, size_t *out_count);
#endif
