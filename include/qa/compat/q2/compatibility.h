/* Port of src/compat/q2/compatibility.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_COMPATIBILITY_H
#define QA_COMPAT_Q2_COMPATIBILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_primary.h"
#include "qa/contracts/content.h"
#include "qa/persistence/value.h"

/* Owned sub-profiles parsed from JSON (pointers in profile point
 * into this storage). */
typedef struct qa_compat_q2_native_compatibility_result {
  bool found; qa_compat_q2_native_primary_profile profile;
  qa_compat_q2_native_primary_weapon_profile weapons; qa_compat_q2_native_primary_player_profile player;
  qa_compat_q2_native_primary_command_profile commands; qa_compat_q2_native_primary_inventory_profile inventory;
  qa_compat_q2_native_primary_drop_profile drop; qa_compat_q2_native_pickup_profile pickups;
} qa_compat_q2_native_compatibility_result;
/* TS parseNativeCompatibility. World readers via prov fn+ctx reading
 * the "world" SaveReader (classic/rerelease world-profile slices). */
typedef struct qa_compat_q2_world_readers {
  void *ctx; const void *(*classic)(void *ctx, const qa_save_reader *r, qa_content_digest d);
  const void *(*rerelease)(void *ctx, const qa_save_reader *r, qa_content_digest d);
} qa_compat_q2_world_readers;
bool qa_compat_q2_parse_native_compatibility(const qa_save_value *value, const qa_resolved_execution_module *execution, qa_compat_q2_world_readers worlds, qa_compat_q2_native_compatibility_result *out);
#endif
