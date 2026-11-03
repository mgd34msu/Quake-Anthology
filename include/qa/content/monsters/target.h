/* Port of src/content/monsters/target.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_MONSTERS_TARGET_H
#define QA_CONTENT_MONSTERS_TARGET_H

#include <stdbool.h>

/* TS MonsterTargetObservation. light_level/hostile_until are
 * nullable in TS (has_* flags). */
typedef struct qa_monster_target_observation {
  double view_height;
  bool notarget;
  bool invisible;
  bool has_light_level;
  double light_level;
  bool has_hostile_until;
  double hostile_until;
  /* Rerelease invisibility: has_cloak_alpha false when not cloaked (TS
   * null/undefined), else the alpha a monster must roll under (0 once the
   * reveal fade has ended). */
  bool has_cloak_alpha;
  double cloak_alpha;
} qa_monster_target_observation;

/* `observation` NULL mirrors the TS null. */
bool qa_monster_target_eligible(double health,
                                const qa_monster_target_observation *observation);

#endif
