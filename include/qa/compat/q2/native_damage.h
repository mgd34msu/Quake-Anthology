/* Port of src/compat/q2/native-damage.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_DAMAGE_H
#define QA_COMPAT_Q2_NATIVE_DAMAGE_H
#include <stdbool.h>
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/contracts/gameplay.h"
#include "qa/foundation/support.h"

/* TS RemovedNativeDamage message (Error subclass; C callers abort). */
#define QA_COMPAT_Q2_REMOVED_NATIVE_DAMAGE_MESSAGE "Native damage target was removed during armor protection"
typedef struct qa_compat_q2_native_damage_arguments_result { double damage_flags; qa_q2_native_cause native; } qa_compat_q2_native_damage_arguments_result;
qa_armor_damage_flags qa_compat_q2_native_armor_flags(double flags);
qa_compat_q2_native_damage_arguments_result qa_compat_q2_native_damage_arguments(qa_damage_request request, qa_q2mp_native_cause_profile profile);
#endif
