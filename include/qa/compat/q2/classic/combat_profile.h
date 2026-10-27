/* Port of src/compat/q2/classic/combat-profile.ts. GPL-2.0-or-later.
 * Combat call argument storage is embedded (stock calls, max 10). */
#ifndef QA_COMPAT_Q2_CLASSIC_COMBAT_PROFILE_H
#define QA_COMPAT_Q2_CLASSIC_COMBAT_PROFILE_H
#include <stddef.h>
#include "qa/compat/q2/classic/layout.h"
#include "qa/compat/q2/native_combat_call.h"

typedef struct qa_compat_q2_classic_combat_calls {
  qa_compat_q2_native_combat_call pain, death, damage, regular_armor, power_armor;
  qa_compat_q2_native_combat_argument storage[5][10];
} qa_compat_q2_classic_combat_calls;
typedef struct qa_compat_q2_classic_combat_profile {
  qa_compat_q2_classic_combat_calls calls;
  qa_content_digest digest;
  const char *game; /* "base"|"xatrix"|"rogue"|"ctf" */
  double entity_bytes;
  struct { double health, damageable, flags, mass, velocity, pain, die; } fields;
  struct { double inventory, inventory_count, max_grenades, invincible_frame, userinfo, userinfo_bytes, view_angles; } client;
  struct { double damage, power_armor, regular_armor, spawn, free; } entries;
  struct { double level_frame, item_list, item_bytes; } globals;
  struct { double jacket, combat, body, screen, shield, cells, grenades; } items;
  struct { double class_name, armor_info; } item_fields;
  struct { double normal_protection, energy_protection; } armor_info;
  struct { const double *regular; size_t regular_count; double empty; } armor;
  struct { double invulnerable, notarget, no_knockback, power_armor; } flags;
  struct { double model, skin; } teams;
} qa_compat_q2_classic_combat_profile;

extern const qa_compat_q2_classic_combat_profile qa_compat_q2_xatrix_combat_profile;
extern const qa_compat_q2_classic_combat_profile qa_compat_q2_xatrix1998_combat_profile;
void qa_compat_q2_validate_classic_combat_profile(const qa_compat_q2_classic_combat_profile *profile);
/* TS classicCombatProfile(digest); NULL is the TS null. */
const qa_compat_q2_classic_combat_profile *qa_compat_q2_classic_get_combat_profile(qa_content_digest digest);
#endif
