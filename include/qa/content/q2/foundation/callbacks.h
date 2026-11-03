/* Port of src/content/q2/foundation/callbacks.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_FOUNDATION_CALLBACKS_H
#define QA_CONTENT_Q2_FOUNDATION_CALLBACKS_H
#include <stddef.h>
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/weapon_behavior.h"
extern const qa_q2_think QA_Q2_FREE_ENTITY;
void qa_q2_free_entity_fn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
/* TS freeQ2Entity assigned to die slots (registered as G_FreeEdict). */
extern const qa_q2_die QA_Q2_FREE_ENTITY_DIE;
void qa_q2_free_entity_die_fn(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
typedef struct qa_q2_trajectory_def { qa_q2_touch touch; void *ctx; void (*project)(void *ctx, qa_q2_entity *e, qa_weapon_trajectory_update u); } qa_q2_trajectory_def;
typedef struct qa_q2_named_think { const char *name; qa_q2_think cb; } qa_q2_named_think;
typedef struct qa_q2_named_use { const char *name; qa_q2_use cb; } qa_q2_named_use;
typedef struct qa_q2_named_touch { const char *name; qa_q2_touch cb; } qa_q2_named_touch;
typedef struct qa_q2_named_pain { const char *name; qa_q2_pain cb; } qa_q2_named_pain;
typedef struct qa_q2_named_die { const char *name; qa_q2_die cb; } qa_q2_named_die;
typedef struct qa_q2_named_blocked { const char *name; qa_q2_blocked cb; } qa_q2_named_blocked;
/** TS Q2CallbackDefinitions. */
typedef struct qa_q2_callback_definitions {
  const qa_q2_trajectory_def *trajectory; size_t trajectory_count;
  const qa_q2_named_think *think; size_t think_count;
  const qa_q2_named_use *use; size_t use_count;
  const qa_q2_named_touch *touch; size_t touch_count;
  const qa_q2_named_pain *pain; size_t pain_count;
  const qa_q2_named_die *die; size_t die_count;
  const qa_q2_named_blocked *blocked; size_t blocked_count;
} qa_q2_callback_definitions;
typedef struct qa_q2_source_callbacks qa_q2_source_callbacks;
qa_q2_source_callbacks *qa_q2_source_callbacks_create(void);
void qa_q2_source_callbacks_destroy(qa_q2_source_callbacks *c);
void qa_q2_source_callbacks_register(qa_q2_source_callbacks *c, qa_q2_callback_definitions defs);
void qa_q2_source_callbacks_project_trajectory(qa_q2_source_callbacks *c, qa_q2_entity *e, qa_weapon_trajectory_update u);
/* Per-kind name/resolve; NULL name input => TS null callback/name. */
const char *qa_q2_think_name(qa_q2_source_callbacks *c, qa_q2_think cb, bool has);
qa_q2_think qa_q2_think_resolve(qa_q2_source_callbacks *c, const char *name, bool *has);
const char *qa_q2_use_name(qa_q2_source_callbacks *c, qa_q2_use cb, bool has);
qa_q2_use qa_q2_use_resolve(qa_q2_source_callbacks *c, const char *name, bool *has);
const char *qa_q2_touch_name(qa_q2_source_callbacks *c, qa_q2_touch cb, bool has);
qa_q2_touch qa_q2_touch_resolve(qa_q2_source_callbacks *c, const char *name, bool *has);
const char *qa_q2_pain_name(qa_q2_source_callbacks *c, qa_q2_pain cb, bool has);
qa_q2_pain qa_q2_pain_resolve(qa_q2_source_callbacks *c, const char *name, bool *has);
const char *qa_q2_die_name(qa_q2_source_callbacks *c, qa_q2_die cb, bool has);
qa_q2_die qa_q2_die_resolve(qa_q2_source_callbacks *c, const char *name, bool *has);
const char *qa_q2_blocked_name(qa_q2_source_callbacks *c, qa_q2_blocked cb, bool has);
qa_q2_blocked qa_q2_blocked_resolve(qa_q2_source_callbacks *c, const char *name, bool *has);
/* TS [...<kind>.functions.keys()] (or the trajectories map keys): the
 * registered names in registration order. kind: "think"|"use"|"touch"|
 * "pain"|"die"|"blocked"|"trajectory". Writes up to capacity names into
 * out (borrowed) and returns the total count. */
size_t qa_q2_source_callbacks_names(const qa_q2_source_callbacks *c, const char *kind, const char **out, size_t capacity);
#endif
