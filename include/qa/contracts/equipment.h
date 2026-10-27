/*
 * Port of src/contracts/equipment.ts (types only): the shared grapple
 * control interface as a function table + ctx. GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_EQUIPMENT_H
#define QA_CONTRACTS_EQUIPMENT_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"

/** TS SharedGrappleControl. native_slot's argument is the enabled
 * selection's mechanic (exact TS string, see content.h). */
typedef struct qa_shared_grapple_control {
  void *ctx;
  qa_grapple_selection selection;
  bool (*native_slot)(void *ctx, const char *mechanic);
  void (*input)(void *ctx, qa_actor_id actor, bool held);
  void (*release)(void *ctx, qa_actor_id actor);
  bool (*pulling)(void *ctx, qa_actor_id actor);
  int64_t (*gravity_scale)(void *ctx, qa_actor_id actor); /* TS returns 0 | 1 */
} qa_shared_grapple_control;

#endif
