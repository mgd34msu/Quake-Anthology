/*
 * Port of src/world/gameplay/original-pickups.ts. Captured rules own
 * malloc'd offered/writes copies (free with
 * qa_free_captured_original_pickup_rules). The generic runSource is
 * synchronous (no Promise counterpart). TS Error throws abort
 * identically; RangeError uses qa_range_error. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_ORIGINAL_PICKUPS_H
#define QA_WORLD_GAMEPLAY_ORIGINAL_PICKUPS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/original_pickups.h"
#include "qa/world/actors/registry.h"

/** TS CapturedOriginalPickupRule (extends OriginalPickupRule). */
typedef struct qa_captured_original_pickup_rule {
  qa_original_pickup_rule operation; /* the original rule */
  const char *id;
  const qa_item_id *offered; /* owned copy */
  size_t offered_count;
  qa_pickup_writes writes;   /* owned copy */
  void *take_ctx;
  qa_original_pickup_decision (*take)(void *ctx, qa_original_pickup_offer offer,
                                      qa_original_pickup_execution execution);
} qa_captured_original_pickup_rule;

/* Writes "protection:<channel>" or "inventory:<item>" into buffer. */
void qa_pickup_write_key(qa_pickup_write write, char *buffer, size_t buffer_size);
/* Malloc'd array (NULL on OOM); *out_count set. */
qa_captured_original_pickup_rule *qa_capture_original_pickup_rules(
    const qa_original_pickup_rule *rules, size_t rule_count, size_t *out_count);
void qa_free_captured_original_pickup_rules(qa_captured_original_pickup_rule *rules,
                                            size_t rule_count);

typedef struct qa_shared_original_pickup_admission qa_shared_original_pickup_admission;
typedef struct qa_gameplay_authority qa_gameplay_authority;
typedef struct qa_shared_inventory_table qa_shared_inventory_table;

/* eligible may be NULL. NULL return only on OOM. Borrowed deps. */
qa_shared_original_pickup_admission *qa_shared_original_pickup_admission_create(
    qa_session_actor_registry *actors, qa_gameplay_authority *combat,
    qa_shared_inventory_table *inventory,
    bool (*eligible)(void *ctx, qa_original_pickup_offer offer), void *eligible_ctx);
void qa_shared_original_pickup_admission_destroy(qa_shared_original_pickup_admission *admission);
void qa_shared_original_pickup_admission_assert_idle(qa_shared_original_pickup_admission *admission);
void *qa_shared_original_pickup_admission_run_source(
    qa_shared_original_pickup_admission *admission, qa_original_pickup_offer offer,
    void *(*execute)(void *ctx, qa_source_pickup_selection selection,
                     qa_source_pickup_lifetime lifetime),
    void *execute_ctx);
qa_original_pickup_outcome qa_shared_original_pickup_admission_touch(
    qa_shared_original_pickup_admission *admission, qa_original_pickup_offer offer,
    qa_original_pickup_continuation continuation);
qa_original_pickup_admission qa_shared_original_pickup_admission_as_contract(
    qa_shared_original_pickup_admission *admission);

#endif
