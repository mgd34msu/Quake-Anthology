/* Port of src/app/bootstrap/network/unified-native-consumer.ts.
 * prepare/prepareUpdate return a heap commit token (TS closure);
 * run it with _commit (NULL from prepare is TS null).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_NATIVE_CONSUMER_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_NATIVE_CONSUMER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/network/unified_native_components.h"
#include "qa/world/session/mods.h"
typedef struct qa_app_unified_native_consumer_options {
  void *ctx;
  const qa_prepared_mod *prepared_mods; size_t prepared_mod_count;
  void (*assert_current)(void *ctx);
  bool (*viewer)(void *ctx, qa_actor_id *out);
  void (*admit_replicated_owner)(void *ctx, qa_presentation_owner owner, qa_content_id content);
  void (*retire_replicated_owner)(void *ctx, qa_presentation_owner owner);
  /* catalog.product(content).expectation family/edition (strings). */
  void (*product_expectation)(void *ctx, qa_content_id content,
      const char **family, const char **edition);
} qa_app_unified_native_consumer_options;
typedef struct qa_app_unified_native_consumers qa_app_unified_native_consumers;
typedef struct qa_app_unified_native_commit qa_app_unified_native_commit;
qa_app_unified_native_consumers *qa_app_unified_native_consumers_create(
    qa_app_unified_native_consumer_options options);
void qa_app_unified_native_consumers_destroy(qa_app_unified_native_consumers *consumers);
qa_app_unified_native_commit *qa_app_unified_native_consumers_prepare_update(
    qa_app_unified_native_consumers *consumers, const qa_app_unified_component_update *update);
/* legacy NULL is TS undefined. */
qa_app_unified_native_commit *qa_app_unified_native_consumers_prepare(
    qa_app_unified_native_consumers *consumers, const qa_app_unified_component_frames *frames,
    qa_actor_id viewer, const qa_app_unified_native_camera *legacy);
void qa_app_unified_native_commit_run(qa_app_unified_native_commit *commit);
void qa_app_unified_native_commit_free(qa_app_unified_native_commit *commit);
void qa_app_unified_native_consumers_retire(qa_app_unified_native_consumers *consumers,
    qa_presentation_owner owner);
const qa_active_mod_client_presentation *qa_app_unified_native_consumers_sources(
    qa_app_unified_native_consumers *consumers, size_t *out_count);
void qa_app_unified_native_consumers_close(qa_app_unified_native_consumers *consumers);
#endif
