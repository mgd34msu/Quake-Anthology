/* Port of src/content/q2/missionpacks/damage.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_DAMAGE_H
#define QA_CONTENT_Q2_MISSIONPACKS_DAMAGE_H
#include <stdbool.h>
#include "qa/contracts/gameplay.h"

/* TS Q2NativeCauseProfile union on edition. game used for classic
 * ("base"|"xatrix"|"rogue"|"ctf"); no_point_loss for rerelease. */
typedef struct qa_q2mp_native_cause_profile {
  bool rerelease;
  const char *game;
  bool no_point_loss;
} qa_q2mp_native_cause_profile;

/* TS q2CanonicalCause record as constants. */
enum { QA_Q2MP_CAUSE_GRAPPLE = 56, QA_Q2MP_CAUSE_TELEFRAG_SPAWN = 57, QA_Q2MP_CAUSE_BLUE_BLASTER = 58 };

/* False return is TS null. */
bool qa_q2mp_canonical_cause_from_native(qa_q2_native_cause native, double *out);
bool qa_q2mp_native_cause_from_canonical(qa_q2mp_native_cause_profile profile, double canonical,
                                         qa_q2_native_cause *out);
#endif
