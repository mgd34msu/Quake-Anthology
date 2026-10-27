/*
 * Port of src/persistence/source-items.ts: the "world:source-items"
 * provider checkpoint that records, per actor, the primary inventory
 * rows hidden under component source-item admissions.
 * Mirrored types (no C home yet; the world module owns them and its
 * port reconciles the names):
 *  - qa_session_actor_registry mirrors SessionActorRegistry from
 *    src/world/actors/registry.ts, with only the methods this module
 *    (and world-state.ts, which shares the mirror) call.
 *    reference_saved is TS referenceSaved with its default
 *    "checkpoint" domain, the only domain the persistence callers use.
 *  - qa_shared_inventory_table / qa_source_item_snapshot mirror
 *    SharedInventoryTable and its sourceItems() result from
 *    src/world/gameplay/inventory.ts.
 * Only these persistence-facing methods are mirrored, so a world
 * table that lacks the rest of the TS class surface still binds.
 * Ownership: qa_source_items_result and qa_source_item_restore own
 * the decoded checkpoint tree and every array/struct built from it;
 * strings borrow from that tree except resource paths normalized by
 * the mirrored content readers, which are malloc'd and freed with
 * the result. GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_SOURCE_ITEMS_H
#define QA_PERSISTENCE_SOURCE_ITEMS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/contracts/source_items.h"
#include "qa/contracts/world.h"
#include "qa/persistence/value.h"

/* --- Mirrored from src/world/actors/registry.ts (see header note).
 * resolve_* return false for the TS null. --- */
typedef struct qa_session_actor_registry {
  void *ctx;
  const qa_actor_observation *(*observations)(void *ctx, size_t *out_count);
  bool (*resolve_owned)(void *ctx, qa_actor_id actor, qa_owned_actor *out);
  bool (*resolve_saved)(void *ctx, qa_saved_actor_id saved, qa_owned_actor *out);
  qa_actor_id (*reference_saved)(void *ctx, qa_saved_actor_id saved);
} qa_session_actor_registry;

/* --- Mirrored from src/world/gameplay/inventory.ts (see header
 * note). qa_source_item_snapshot is the TS sourceItems() result:
 * {primary, groups:[{owner, items}]}. --- */
typedef struct qa_source_item_group {
  qa_provider_id owner;
  const qa_source_item_admission *items;
  size_t item_count;
} qa_source_item_group;
typedef struct qa_source_item_snapshot {
  const qa_inventory_entry *primary;
  size_t primary_count;
  const qa_source_item_group *groups;
  size_t group_count;
} qa_source_item_snapshot;
typedef struct qa_shared_inventory_table {
  void *ctx;
  /* TS sourceItems(actor); false is the TS null. */
  bool (*source_items)(void *ctx, qa_owned_actor actor, qa_source_item_snapshot *out);
  const qa_inventory_entry *(*entries)(void *ctx, qa_actor_id actor, size_t *out_count);
  bool (*has)(void *ctx, qa_actor_id actor);
  void (*create)(void *ctx, qa_owned_actor actor,
                 const qa_inventory_entry *entries, size_t count);
} qa_shared_inventory_table;

/* TS interface SourceItems (one decoded checkpoint record). Item
 * arrays are mutable COPIES of the contracts shapes so their
 * pointer fields can own decoded allocations. */
typedef struct qa_source_items_group {
  qa_provider_id owner;
  qa_source_item_admission *items;
  size_t item_count;
} qa_source_items_group;
typedef struct qa_source_items_record {
  qa_saved_actor_id actor;
  qa_provider_id owner;
  qa_inventory_entry *primary;
  size_t primary_count;
  qa_source_items_group *groups;
  size_t group_count;
} qa_source_items_record;
typedef struct qa_source_items_result {
  qa_source_items_record *records;
  size_t count;
  qa_save_value *root;      /* owned decoded tree; NULL when empty */
  char **owned_strings;     /* normalized resource paths */
  size_t owned_string_count;
  size_t owned_string_capacity;
} qa_source_items_result;

/* TS captureSourceItems. False only on OOM; out->bytes.bytes is
 * malloc'd (caller frees), provider/schema are string literals. */
bool qa_capture_source_items(const qa_session_actor_registry *actors,
                             const qa_shared_inventory_table *inventory,
                             qa_provider_checkpoint *out);
/* TS readSourceItems over Pick<SaveImage,"providers"|"inventories">.
 * False only on OOM; malformed checkpoints abort as the TS throws
 * SaveFormatError. */
bool qa_read_source_items(const qa_save_image *save, qa_source_items_result *out);
void qa_source_items_result_free(qa_source_items_result *result);

/* TS prepareSourceItemRestore result. `primary` is a shallow copy of
 * the save whose inventories array is owned here (or borrowed from
 * the save when no source-item records exist). */
typedef struct qa_source_item_restore {
  const qa_save_image *save; /* borrowed */
  qa_source_items_result records;
  qa_save_image primary;
  qa_inventory_checkpoint *primary_inventories; /* owned; may be NULL */
  qa_inventory_checkpoint *effective;           /* owned scratch */
  size_t effective_count;
} qa_source_item_restore;

/* False only on OOM. */
bool qa_prepare_source_item_restore(const qa_save_image *save,
                                    qa_source_item_restore *out);
void qa_source_item_restore_free(qa_source_item_restore *restore);
/* TS effective(inventories): returns the input array unchanged when
 * no records exist, else a scratch array owned by `restore` (valid
 * until the next call or free) with saved-effective rows restored.
 * NULL only on OOM. */
const qa_inventory_checkpoint *qa_source_item_restore_effective(
    qa_source_item_restore *restore, const qa_inventory_checkpoint *inventories,
    size_t count, size_t *out_count);
/* TS finish(actors, inventory): returns undefined. */
void qa_source_item_restore_finish(const qa_source_item_restore *restore,
                                   const qa_session_actor_registry *actors,
                                   const qa_shared_inventory_table *inventory);

#endif
