/* Port of src/app/bootstrap/q1-localization.ts. ApplicationAssets
 * content accessors (assets.ts / content.ts, other slices) are the
 * host below. Async catalog loads become synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_LOCALIZATION_H
#define QA_APP_BOOTSTRAP_Q1_LOCALIZATION_H
#include <stddef.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
typedef struct qa_app_q1_localization_assets {
  void *ctx;
  const qa_content_catalog *catalog; /* borrowed */
  /* TS content.forContent(content): borrowed mounts. */
  qa_content_mounted *(*for_content)(void *ctx, qa_content_id content);
} qa_app_q1_localization_assets;
typedef const char *(*qa_app_q1_language_fn)(void *ctx);
typedef struct qa_app_q1_message_localization qa_app_q1_message_localization;
/* language may be NULL (TS default "english"). */
qa_app_q1_message_localization *qa_app_q1_message_localization_create(qa_seat_id seat,
    qa_app_q1_localization_assets assets, qa_app_q1_language_fn language, void *language_ctx);
void qa_app_q1_message_localization_destroy(qa_app_q1_message_localization *localization);
/* Malloc'd resolved text; caller frees. */
char *qa_app_q1_message_localization_resolve(qa_app_q1_message_localization *localization,
    qa_content_id content, const char *text, const qa_q1_msg_arg *args, size_t arg_count,
    const qa_q1_message_part *parts, size_t part_count);
#endif
