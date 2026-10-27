/* Port of src/content/q1/addons/monsters/bosses/registry.ts.
 * The TS generic map becomes one registry object whose create
 * callback builds the concrete boss (fn+ctx closure mirror).
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_REGISTRY_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_REGISTRY_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_boss_registry qa_q1am_boss_registry;
typedef struct qa_q1am_boss_create {
  void *ctx;
  qa_q1am_monster *(*fn)(void *ctx, qa_q1am_actor *entity);
  void (*destroy)(void *ctx, qa_q1am_monster *monster);
} qa_q1am_boss_create;

qa_q1am_boss_registry *qa_q1am_register_boss_controllers(qa_q1am_context *context, const char *prefix,
                                                         const char *classname, qa_q1am_boss_create create);
qa_q1am_monster *qa_q1am_require_boss(qa_q1am_boss_registry *registry, qa_q1am_actor *entity);
bool qa_q1am_boss_registry_has(qa_q1am_boss_registry *registry, qa_owned_actor actor);

/* Builds the per-class action map the boss modules derive from their
 * frame tables (instanceof check -> dispatch by operation name).
 * extra (nullable) appends/overrides entries like the TS Map.set. */
const qa_q1am_source *qa_q1am_boss_build_source(
    const char *prefix, const qa_q1am_frame_table *frames, qa_q1am_kind kind,
    const char *kind_error, void (*dispatch)(qa_q1am_monster *monster, const char *name),
    const qa_q1am_action_entry *extra, size_t extra_count);

#endif
