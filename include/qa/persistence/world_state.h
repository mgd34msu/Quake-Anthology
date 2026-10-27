/*
 * Port of src/persistence/world-state.ts: capture of shared body
 * checkpoints and restoration of shared bodies, combat and
 * inventories against copied/prebound/source-reconstructed storage.
 * Mirrored types (no C home yet; the world module owns them and its
 * port reconciles the names):
 *  - qa_shared_body_table / qa_body_link_state mirror SharedBodyTable
 *    and BodyLinkState from src/world/actors/body.ts.
 *  - qa_gameplay_authority mirrors GameplayAuthority from
 *    src/world/gameplay/authority.ts.
 * Only the methods world-state.ts (and readPrimaryProtection from
 * src/persistence/protection.ts, qa_read_primary_protection in
 * protection.h) call are mirrored. The registry/inventory mirrors
 * come from source_items.h.
 * TS overloads of restoreSharedWorldState split into
 * qa_restore_shared_world_state (no options; returns undefined) and
 * qa_restore_shared_world_state_deferred (deferProtection; yields
 * the completion object). OwnedActor Map/Set keys compare by
 * identity-token plus qa_same_actor, matching the canonical
 * registry objects the TS keys on. Malloc'd checkpoint arrays and
 * completion scratch are released by the *_free helpers.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_WORLD_STATE_H
#define QA_PERSISTENCE_WORLD_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/session.h"
#include "qa/contracts/world.h"
#include "qa/persistence/source_items.h"

/* Mirrored BodyLinkState (world/actors/body.ts): the linked member
 * carries only the fields persistence reads (state/absoluteBounds);
 * the live LinkedBody's actor/linkCount are the table's own. */
typedef struct qa_body_link_state {
  double link_count;
  bool has_linked;
  qa_body_state state;
  qa_bounds absolute_bounds;
} qa_body_link_state;

/* Mirrored SharedBodyTable (world/actors/body.ts). read/link_state/
 * attachment return false for the TS null; returned states borrow
 * their ground pointer from the table. attach() takes the contracts
 * BodyAttachment (anchor is an ActorId), as in TS. */
typedef struct qa_shared_body_table {
  void *ctx;
  bool (*read)(void *ctx, qa_actor_id actor, qa_body_state *out);
  bool (*link_state)(void *ctx, qa_actor_id actor, qa_body_link_state *out);
  bool (*attachment)(void *ctx, qa_actor_id actor, qa_body_attachment *out);
  void (*restore_link_state)(void *ctx, qa_owned_actor actor,
                             qa_body_link_state state);
  void (*create)(void *ctx, qa_owned_actor actor, qa_body_state state);
  void (*attach)(void *ctx, qa_owned_actor actor, qa_body_attachment attachment);
} qa_shared_body_table;

/* Mirrored GameplayAuthority (world/gameplay/authority.ts). read /
 * copied_primary_armor return false for the TS null;
 * protection_owner returns NULL for it. */
typedef struct qa_gameplay_authority {
  void *ctx;
  void (*assert_idle)(void *ctx);
  bool (*read)(void *ctx, qa_actor_id actor, qa_combat_state *out);
  void (*create)(void *ctx, qa_owned_actor actor, qa_combat_state state);
  qa_armor_state (*normalize_legacy_armor)(void *ctx, qa_owned_actor actor,
                                           qa_armor_state armor);
  qa_provider_id (*protection_owner)(void *ctx, qa_owned_actor actor,
                                     qa_protection_channel channel);
  const qa_item_id *(*protection_inventory_items)(void *ctx, qa_owned_actor actor,
                                                  qa_protection_channel channel,
                                                  size_t *out_count);
  bool (*copied_primary_armor)(void *ctx, qa_owned_actor actor,
                               qa_armor_state *out);
  void (*set_powered_protection)(void *ctx, qa_owned_actor actor,
                                 qa_powered_protection_state powered);
} qa_gameplay_authority;

/* TS storage(actor) result: "copied" | "prebound" |
 * "source-reconstructed". */
typedef enum qa_world_storage_kind {
  QA_WORLD_STORAGE_COPIED,
  QA_WORLD_STORAGE_PREBOUND,
  QA_WORLD_STORAGE_SOURCE_RECONSTRUCTED
} qa_world_storage_kind;
typedef qa_world_storage_kind (*qa_world_storage_fn)(void *ctx, qa_owned_actor actor);

/** TS SharedWorldRestoreHost. storage may be NULL only in the link
 * host below (the TS optional member). */
typedef struct qa_shared_world_restore_host {
  qa_session_actor_registry actors;
  qa_shared_body_table bodies;
  qa_gameplay_authority combat;
  qa_shared_inventory_table inventory;
  void *storage_ctx;
  qa_world_storage_fn storage;
} qa_shared_world_restore_host;

/* TS restoreSharedBodyLinks host: Pick<..., "actors"|"bodies"> plus
 * the optional storage member (NULL storage = absent). */
typedef struct qa_shared_body_link_host {
  qa_session_actor_registry actors;
  qa_shared_body_table bodies;
  void *storage_ctx;
  qa_world_storage_fn storage; /* nullable */
} qa_shared_body_link_host;

/** TS SharedWorldRestoreCompletion, plus the retained restore
 * context its closures capture. Free with
 * qa_shared_world_restore_completion_free after finish. */
typedef struct qa_shared_world_restore_completion qa_shared_world_restore_completion;

typedef void (*qa_hydrate_inventory_sources_fn)(void *ctx);

/* TS captureSharedBodies. Malloc'd array in *out (NULL when the
 * capture is empty); false only on OOM. */
bool qa_capture_shared_bodies(const qa_session_actor_registry *actors,
                              const qa_shared_body_table *bodies,
                              qa_body_checkpoint **out, size_t *out_count);
void qa_body_checkpoints_free(qa_body_checkpoint *checkpoints, size_t count);

/* TS restoreSharedBodyLinks over Pick<SaveImage,"bodies">: returns
 * undefined. */
void qa_restore_shared_body_links(const qa_save_image *save,
                                  const qa_shared_body_link_host *host);

/* TS restoreSharedWorldState(save, host): returns undefined. */
void qa_restore_shared_world_state(const qa_save_image *save,
                                   qa_shared_world_restore_host *host);
/* TS restoreSharedWorldState(save, host, {deferProtection:true,...}).
 * hydrate may be NULL (the TS optional member). NULL only on OOM. */
qa_shared_world_restore_completion *qa_restore_shared_world_state_deferred(
    const qa_save_image *save, qa_shared_world_restore_host *host,
    qa_hydrate_inventory_sources_fn hydrate, void *hydrate_ctx);
/* TS completion.finish() / assertComplete(): return undefined. */
void qa_shared_world_restore_finish(qa_shared_world_restore_completion *completion);
void qa_shared_world_restore_assert_complete(
    const qa_shared_world_restore_completion *completion);
void qa_shared_world_restore_completion_free(
    qa_shared_world_restore_completion *completion);

#endif
