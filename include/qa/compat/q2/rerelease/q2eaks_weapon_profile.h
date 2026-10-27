/* Port of src/compat/q2/rerelease/q2eaks-weapon-profile.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_Q2EAKS_WEAPON_PROFILE_H
#define QA_COMPAT_Q2_RERELEASE_Q2EAKS_WEAPON_PROFILE_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/contracts/native_weapon_behavior.h"

extern const qa_content_digest qa_compat_q2_q2eaks_weapon_digest;
qa_compat_q2_guest_call_signature qa_compat_q2_q2eaks_weapon_think_signature(void);
qa_compat_q2_guest_call_signature qa_compat_q2_q2eaks_fire_rocket_signature(void);
extern const qa_guest_layout qa_compat_q2_q2eaks_projectile_layout;
extern const qa_guest_layout qa_compat_q2_q2eaks_weapon_client_layout;

typedef struct qa_compat_q2_q2eaks_weapon_entries {
  qa_guest_address weapon_run_think, rocket_launcher_fire, fire_rocket, spawn, freed, rocket_touch, level_time;
} qa_compat_q2_q2eaks_weapon_entries;
/* TS q2EaksWeaponEntries(module, imageBase): memory is module.memory. */
qa_compat_q2_q2eaks_weapon_entries qa_compat_q2_q2eaks_get_weapon_entries(const qa_compat_q2_memory *memory, qa_guest_address image_base);
/* False only on OOM. out owns malloc'd copies; release with
 * qa_compat_q2_native_weapon_declaration_free. */
bool qa_compat_q2_q2eaks_weapon_declaration(const char *artifact_path, qa_native_weapon_behavior_declaration *out);
#endif
