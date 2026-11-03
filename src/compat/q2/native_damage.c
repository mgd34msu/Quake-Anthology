/* Port of src/compat/q2/native-damage.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_damage.h"
#include <string.h>
#include "qa/world/gameplay/armor.h"
#include "qa/foundation/js.h"
qa_armor_damage_flags qa_compat_q2_native_armor_flags(double flags) {
  int32_t f = qa_js_to_int32(flags); /* flags & bit: ToInt32 */ qa_armor_damage_flags o; memset(&o, 0, sizeof o);
  o.no_armor = (f & 2) != 0; o.no_power_armor = (f & 0x100) != 0; o.no_regular_armor = (f & 0x80) != 0; o.energy = (f & 4) != 0; return o;
}
qa_compat_q2_native_damage_arguments_result qa_compat_q2_native_damage_arguments(qa_damage_request request, qa_q2mp_native_cause_profile profile) {
  qa_attack_cause cause = request.attack.cause; qa_attack_damage_flags fl = qa_attack_damage_flags_of(request);
  double damage_flags;
  if (cause.kind == QA_ATTACK_CAUSE_Q2) damage_flags = cause.damage_flags;
  else damage_flags = (double)((request.delivery && strcmp(request.delivery, "radius") == 0 ? 1 : 0) | (fl.flags.no_armor ? 2 : 0) | (fl.flags.energy ? 4 : 0) | (fl.no_knockback ? 8 : 0) | (fl.no_protection ? 32 : 0));
  qa_q2_native_cause native; bool have = false;
  if (cause.kind == QA_ATTACK_CAUSE_Q2 && cause.has_native) {
    const char *ed = profile.rerelease ? "rerelease" : "classic";
    bool same = cause.native.edition && strcmp(cause.native.edition, ed) == 0 && (profile.rerelease || (cause.native.game && profile.game && strcmp(cause.native.game, profile.game) == 0));
    double canon = 0;
    if (same && qa_q2mp_canonical_cause_from_native(cause.native, &canon) && canon == cause.means_of_death) { native = cause.native; have = true; }
  }
  if (!have) {
    double want = cause.kind == QA_ATTACK_CAUSE_Q2 ? cause.means_of_death : 0;
    if (cause.kind == QA_ATTACK_CAUSE_Q2) have = qa_q2mp_native_cause_from_canonical(profile, want, &native);
    if (!have) { /* MOD_UNKNOWN fallback */ qa_q2mp_native_cause_from_canonical(profile, 0, &native); }
  }
  qa_compat_q2_native_damage_arguments_result o = {damage_flags, native}; return o;
}
