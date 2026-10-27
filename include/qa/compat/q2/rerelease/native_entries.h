/* Port of src/compat/q2/rerelease/native-entries.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_NATIVE_ENTRIES_H
#define QA_COMPAT_Q2_RERELEASE_NATIVE_ENTRIES_H
#include "qa/compat/q2/native_combat_call.h"
#include "qa/compat/q2/rerelease/world_profile.h"

/* TS re-export rereleaseModLayout = nativeRereleaseModLayout. */
#define qa_compat_q2_rerelease_mod_layout qa_compat_q2_native_rerelease_mod_layout
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_spawn_signature(void);
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_free_signature(void);
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_damage_signature(qa_guest_value_layout *storage);
qa_compat_q2_guest_call_signature qa_compat_q2_rerelease_get_power_armor_signature(qa_guest_value_layout *storage);
typedef struct qa_compat_q2_rerelease_native_entries {
  qa_guest_address spawn, freed, damage, power_armor;
  struct { qa_guest_address entry, join; } regular_armor;
  qa_guest_address armor_info_table, process_pain, time;
} qa_compat_q2_rerelease_native_entries;
qa_compat_q2_rerelease_native_entries qa_compat_q2_rerelease_entries(const qa_compat_q2_memory *memory, qa_guest_address image_base,
                                                                     const qa_compat_q2_rerelease_primary_world_profile *profile);
#endif
