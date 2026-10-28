/* Port of src/app/bootstrap/effects/q2-muzzle.ts
 * (CL_ParseMuzzleFlash2 color/particle admissions, Q2 cl_fx.c).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_EFFECTS_Q2_MUZZLE_H
#define QA_APP_BOOTSTRAP_EFFECTS_Q2_MUZZLE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
/* TS MonsterMuzzle. */
typedef struct qa_app_monster_muzzle {
  qa_vec3 color;
  bool particles;
  bool smoke;
  double radius;
  double mask;
} qa_app_monster_muzzle;
/* One TS q2MonsterMuzzles map entry. */
typedef struct qa_app_q2_monster_muzzle_entry {
  double flash;
  qa_app_monster_muzzle muzzle;
} qa_app_q2_monster_muzzle_entry;
/* TS q2MonsterMuzzles (insertion order preserved). */
extern const qa_app_q2_monster_muzzle_entry qa_app_q2_monster_muzzles[];
extern const size_t qa_app_q2_monster_muzzle_count;
/* TS q2MonsterMuzzle: false is the TS undefined. */
bool qa_app_q2_monster_muzzle(double flash, bool rerelease, qa_app_monster_muzzle *out);
#endif
