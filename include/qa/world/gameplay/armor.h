/*
 * Port of src/world/gameplay/armor.ts. Math.fround staging is
 * reproduced with double->float casts at the TS points.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_ARMOR_H
#define QA_WORLD_GAMEPLAY_ARMOR_H

#include <stdbool.h>

#include "qa/contracts/gameplay.h"

/** TS VictimArmorContext. arithmetic: "binary32" | "binary64". */
typedef struct qa_victim_armor_context {
  double screen_facing_dot;
  const char *arithmetic;
  bool has_q2;
  const char *q2_product; /* "classic" | "rerelease" */
  bool q2_ctf;
  bool q2_alive;
} qa_victim_armor_context;

/** TS VictimArmorPolicy. */
typedef struct qa_victim_armor_policy {
  void *ctx;
  qa_armor_result (*call)(void *ctx, qa_damage_request request, qa_armor_state armor,
                          double damage, qa_armor_damage_flags flags);
} qa_victim_armor_policy;

qa_armor_result qa_absorb_native_armor(qa_armor_state armor, double damage,
                                       qa_armor_damage_flags flags,
                                       qa_victim_armor_context context);

/** TS attackDamageFlags return (ArmorDamageFlags & extras). */
typedef struct qa_attack_damage_flags {
  qa_armor_damage_flags flags;
  bool no_knockback;
  bool no_protection;
  bool no_team_protection;
  bool destroy_armor;
} qa_attack_damage_flags;
qa_attack_damage_flags qa_attack_damage_flags_of(qa_damage_request request);

#endif
