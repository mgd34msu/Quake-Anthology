/*
 * Port of src/world/actors/body.ts: the shared body table.
 * TS frozen snapshots become returned values (structural-sharing
 * identity is unobservable under C value semantics). qa_body_state's
 * nullable ground pointer is retargeted to table-owned storage on
 * every store/read. Implements contracts qa_body_table through the
 * adapter. TS Error throws abort identically; RangeError uses
 * qa_range_error. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_ACTORS_BODY_H
#define QA_WORLD_ACTORS_BODY_H

#include <stdbool.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/registry.h"

/** TS BodyStateBinding. linked may be NULL. */
typedef struct qa_body_state_binding {
  void *ctx;
  qa_body_state (*read)(void *ctx);
  void (*write)(void *ctx, qa_body_state state);
  void (*linked)(void *ctx, qa_linked_body body);
} qa_body_state_binding;

/** TS BodyLinkHooks. */
typedef struct qa_body_link_hooks {
  void *ctx;
  qa_bounds (*absolute_bounds)(void *ctx, qa_owned_actor actor, qa_body_state state);
  void (*on_link)(void *ctx, qa_linked_body body);
  void (*on_unlink)(void *ctx, qa_actor_id actor);
} qa_body_link_hooks;

/** TS BodyLinkState. linked is NULL when absent. */
typedef struct qa_body_link_state {
  double link_count;
  bool has_linked;
  qa_linked_body linked;
} qa_body_link_state;

qa_vec3 qa_copy_vector(qa_vec3 vector);
qa_bounds qa_copy_bounds(qa_bounds bounds);
qa_body_state qa_copy_body(qa_body_state state);
qa_bounds qa_translated_body_bounds(qa_owned_actor actor, qa_body_state state);

typedef struct qa_shared_body_table qa_shared_body_table;

/* NULL only on allocation failure. The registry and hooks are borrowed. */
qa_shared_body_table *qa_shared_body_table_create(qa_session_actor_registry *actors,
                                                  qa_body_link_hooks hooks);
void qa_shared_body_table_destroy(qa_shared_body_table *table);

void qa_shared_body_table_bind(qa_shared_body_table *table, qa_owned_actor actor,
                               qa_body_state_binding binding);
void qa_shared_body_table_rebind(qa_shared_body_table *table, qa_owned_actor actor,
                                 qa_body_state_binding binding);
void qa_shared_body_table_create_body(qa_shared_body_table *table, qa_owned_actor actor,
                                      qa_body_state initial);
qa_body_state_result qa_shared_body_table_read(qa_shared_body_table *table, qa_actor_id actor);
void qa_shared_body_table_write(qa_shared_body_table *table, qa_owned_actor actor,
                                qa_body_state state);
void qa_shared_body_table_attach(qa_shared_body_table *table, qa_owned_actor actor,
                                 qa_body_attachment attachment);
void qa_shared_body_table_detach(qa_shared_body_table *table, qa_owned_actor actor);
qa_body_attachment_result qa_shared_body_table_attachment(qa_shared_body_table *table,
                                                          qa_actor_id actor);
void qa_shared_body_table_transport_attachments(qa_shared_body_table *table,
                                                qa_numeric_operations numeric);
qa_linked_body_result qa_shared_body_table_linked(qa_shared_body_table *table,
                                                  qa_actor_id actor);
bool qa_shared_body_table_link_state(qa_shared_body_table *table, qa_actor_id actor,
                                     qa_body_link_state *out);
void qa_shared_body_table_restore_link_state(qa_shared_body_table *table,
                                             qa_owned_actor actor, qa_body_link_state saved);
/* origin may be NULL (TS optional). */
void qa_shared_body_table_link(qa_shared_body_table *table, qa_owned_actor actor,
                               const qa_vec3 *origin);
void qa_shared_body_table_unlink(qa_shared_body_table *table, qa_owned_actor actor);

qa_body_table qa_shared_body_table_as_contract(qa_shared_body_table *table);

#endif
