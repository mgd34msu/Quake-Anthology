/* Port of src/app/bootstrap/network/unified-component-consumer.ts.
 * Prepared mods, presentation events, per-mod file mounts and
 * installed content come through the options host (app content
 * owner). Async update -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_COMPONENT_CONSUMER_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_COMPONENT_CONSUMER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/world/session/mod_client_presentation.h"
#include "qa/world/session/mods.h"
/* qa_active_mod_presentation lives in world/session/mod_presentations.h,
 * whose qa_qvm_scene_command mirror collides with the checkpoint
 * header unified_components.h uses; forward-declared here (the .c
 * renames per landed mod_provider.c precedent). */
struct qa_active_mod_presentation;
typedef struct qa_app_unified_component_consumer_options {
  void *ctx;
  const qa_prepared_mod *prepared_mods; size_t prepared_mod_count;
  void (*assert_current)(void *ctx);
  bool (*viewer)(void *ctx, qa_actor_id *out); /* false = TS null */
  void (*command)(void *ctx, qa_presentation_owner owner, double generation,
      const char *const *args, size_t count);
  void (*admit_replicated_owner)(void *ctx, qa_presentation_owner owner, qa_content_id content);
  void (*retire_replicated_owner)(void *ctx, qa_presentation_owner owner);
  /* content.forContent(content) -> borrowed mounts handle. */
  const void *(*installed_mounts)(void *ctx, qa_content_id content);
  /* borrowModFileMounts(selection, content, installed, writable);
   * returns mounts handle; close only when != installed handle. */
  const void *(*borrow_mounts)(void *ctx, const char *selection, qa_content_id content,
      const void *installed, const void **out_writable);
  void (*close_mounts)(void *ctx, const void *mounts);
} qa_app_unified_component_consumer_options;
typedef struct qa_app_unified_component_consumers qa_app_unified_component_consumers;
qa_app_unified_component_consumers *qa_app_unified_component_consumers_create(
    qa_app_unified_component_consumer_options options);
void qa_app_unified_component_consumers_destroy(qa_app_unified_component_consumers *consumers);
void qa_app_unified_component_consumers_update(qa_app_unified_component_consumers *consumers,
    const qa_app_unified_component_update *update);
bool qa_app_unified_component_consumers_accept(qa_app_unified_component_consumers *consumers,
    const qa_app_unified_component_frames *frames, qa_actor_id viewer);
/* Borrowed arrays owned by the consumer; count via out param. */
const struct qa_active_mod_presentation *qa_app_unified_component_consumers_sources(
    qa_app_unified_component_consumers *consumers, size_t *out_count);
const qa_active_mod_client_presentation *qa_app_unified_component_consumers_client_sources(
    qa_app_unified_component_consumers *consumers, size_t *out_count);
void qa_app_unified_component_consumers_close(qa_app_unified_component_consumers *consumers);
#endif
