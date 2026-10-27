/*
 * Port of src/world/actors/callbacks.ts: the actor callback table.
 * The five ModOperations carry the concrete request structs below;
 * nullable ActorId arguments use has_ flags, converted back to the
 * contracts pointer form at invocation. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_ACTORS_CALLBACKS_H
#define QA_WORLD_ACTORS_CALLBACKS_H

#include <stdbool.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/time.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/mod_composition.h"

/** TS ActorInvocation. parent is borrowed (the enclosing invocation). */
typedef struct qa_actor_invocation qa_actor_invocation;
struct qa_actor_invocation {
  qa_owned_actor self;
  const char *kind; /* "think" | "touch" | "use" | "pain" | "die" */
  const qa_actor_invocation *parent; /* nullable */
};

/* ModOperation request payloads (TS Parameters<> tuples). */
typedef struct qa_actor_think_request { qa_owned_actor self; qa_frame_context frame; } qa_actor_think_request;
typedef struct qa_actor_touch_request { qa_touch_contact contact; } qa_actor_touch_request;
typedef struct qa_actor_use_request {
  qa_owned_actor self;
  bool has_other; qa_actor_id other;
  bool has_activator; qa_actor_id activator;
} qa_actor_use_request;
typedef struct qa_actor_pain_request { qa_pain_reaction reaction; } qa_actor_pain_request;
typedef struct qa_actor_die_request { qa_death_reaction reaction; } qa_actor_die_request;

typedef struct qa_actor_callback_table qa_actor_callback_table;
/* NULL only on allocation failure. */
qa_actor_callback_table *qa_actor_callback_table_create(qa_session_actor_registry *actors);
void qa_actor_callback_table_destroy(qa_actor_callback_table *table);
/* TS operations: borrowed pointers to the table's ModOperations. */
qa_mod_operation *qa_actor_callback_table_think_operation(qa_actor_callback_table *table);
qa_mod_operation *qa_actor_callback_table_touch_operation(qa_actor_callback_table *table);
qa_mod_operation *qa_actor_callback_table_use_operation(qa_actor_callback_table *table);
qa_mod_operation *qa_actor_callback_table_pain_operation(qa_actor_callback_table *table);
qa_mod_operation *qa_actor_callback_table_die_operation(qa_actor_callback_table *table);
const qa_actor_invocation *qa_actor_callback_table_current(qa_actor_callback_table *table);

void qa_actor_callback_table_bind(qa_actor_callback_table *table, qa_owned_actor actor,
                                  qa_actor_callbacks callbacks);
bool qa_actor_callback_table_think(qa_actor_callback_table *table, qa_owned_actor self,
                                   qa_frame_context frame);
bool qa_actor_callback_table_touch(qa_actor_callback_table *table, qa_touch_contact contact);
bool qa_actor_callback_table_use(qa_actor_callback_table *table, qa_owned_actor self,
                                 const qa_actor_id *other, const qa_actor_id *activator);
bool qa_actor_callback_table_pain(qa_actor_callback_table *table, qa_pain_reaction reaction);
bool qa_actor_callback_table_die(qa_actor_callback_table *table, qa_death_reaction reaction);

/* TS source* execute closures: fn ptr + ctx. */
bool qa_actor_callback_table_source_pain(qa_actor_callback_table *table,
                                         qa_pain_reaction reaction,
                                         void (*execute)(void *ctx, qa_pain_reaction reaction),
                                         void *ctx);
bool qa_actor_callback_table_source_die(qa_actor_callback_table *table,
                                        qa_death_reaction reaction,
                                        void (*execute)(void *ctx, qa_death_reaction reaction),
                                        void *ctx);
bool qa_actor_callback_table_source_use(qa_actor_callback_table *table, qa_owned_actor self,
                                        const qa_actor_id *other, const qa_actor_id *activator,
                                        void (*execute)(void *ctx, qa_owned_actor self,
                                                        const qa_actor_id *other,
                                                        const qa_actor_id *activator),
                                        void *ctx);
bool qa_actor_callback_table_source_touch(qa_actor_callback_table *table,
                                          qa_touch_contact contact,
                                          void (*execute)(void *ctx, qa_touch_contact contact),
                                          void *ctx);

#endif
