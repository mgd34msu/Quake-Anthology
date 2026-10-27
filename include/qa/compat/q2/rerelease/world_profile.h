/* Port of src/compat/q2/rerelease/world-profile.ts. GPL-2.0-or-later.
 * Combat call argument storage is embedded (stock calls, max 10);
 * profiles built by the reader hold malloc'd call arguments. */
#ifndef QA_COMPAT_Q2_RERELEASE_WORLD_PROFILE_H
#define QA_COMPAT_Q2_RERELEASE_WORLD_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/q2/native_combat_call.h"
#include "qa/compat/q2/rerelease/api.h"
#include "qa/compat/q2/rerelease/client_profile.h"
#include "qa/contracts/gameplay.h"
#include "qa/persistence/value.h"

/** TS RereleaseWorldLocation. register used when is_register. */
typedef struct qa_compat_q2_rerelease_world_location {
  bool is_register;
  const char *reg; /* register kind */
  double offset; /* stack kind */
  const char *storage; /* "pointer"|"int32"|"uint32" */
} qa_compat_q2_rerelease_world_location;
typedef struct qa_compat_q2_rerelease_armor_repair {
  qa_compat_q2_rerelease_world_location source, target;
} qa_compat_q2_rerelease_armor_repair;
typedef struct qa_compat_q2_rerelease_inventory_row {
  qa_item_id item;
  const char *source_kind; /* classname|index|remaining */
  const char *source_name;
  double source_index;
  bool capacity_is_ammo;
  double capacity_source_index;
  double capacity_count;
} qa_compat_q2_rerelease_inventory_row;
typedef struct qa_compat_q2_rerelease_speed_load { double next, reg; } qa_compat_q2_rerelease_speed_load;
typedef struct qa_compat_q2_rerelease_primary_world_profile {
  struct {
    qa_compat_q2_native_combat_call pain, death, process_pain, damage, power_armor;
    qa_compat_q2_native_combat_argument storage[5][10];
  } calls;
  qa_content_digest digest;
  qa_guest_layout edict;
  qa_compat_q2_rerelease_client_profile client;
  struct {
    double spawn, free_, damage, power_armor, process_pain, time;
    struct { double entry, join; } regular_armor;
  } entries;
  struct {
    qa_compat_q2_rerelease_world_location target, amount, point, normal, flags, result;
    const qa_compat_q2_rerelease_armor_repair *repair; size_t repair_count;
  } regular_armor;
  struct { double attacker, inflictor, blood, knockback, point, mod, invincible_time; } monster;
  const qa_compat_q2_rerelease_inventory_row *inventory; size_t inventory_count;
  struct {
    double table, stride, normal, energy;
    const qa_item_id *regular; size_t regular_count;
    qa_item_id empty, screen, shield, cells; double cells_index;
  } armor;
  struct { double godmode, notarget, no_knockback, power_armor; } flags;
  struct {
    bool has_body; struct { double dimensions, trace, movement_global; } body;
    double game_api, pmove;
    const qa_compat_q2_rerelease_speed_load *speed_loads; size_t speed_load_count;
  } movement;
} qa_compat_q2_rerelease_primary_world_profile;

/* TS rereleasePrimaryWorldProfile; NULL is the TS null. The retail
 * profile is built on first use. */
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_get_primary_world_profile(qa_content_digest digest);
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_get_retail_world_profile(void);
void qa_compat_q2_validate_rerelease_primary_world_profile(const qa_compat_q2_rerelease_primary_world_profile *profile, qa_content_digest digest);
/* Malloc'd lists inside out; release with _free. False only on OOM. */
bool qa_compat_q2_read_rerelease_primary_world_profile(const qa_save_reader *reader, qa_content_digest digest,
                                                       qa_compat_q2_rerelease_primary_world_profile *out);
void qa_compat_q2_rerelease_world_profile_free(qa_compat_q2_rerelease_primary_world_profile *profile);
#endif
