/* Port of src/bots/behavior/library/weapons.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_WEAPONS_H
#define QA_BOTS_BEHAVIOR_LIBRARY_WEAPONS_H
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_MAX_WEAPON_STATES 64
enum {
  QA_PROJECTILE_WINDOW_DAMAGE = 1,
  QA_PROJECTILE_RETURN = 2,
  QA_WEAPON_FIRE_RELEASED_BOTS_A = 1,
  QA_DAMAGE_TYPE_IMPACT = 1,
  QA_DAMAGE_TYPE_RADIAL = 2,
  QA_DAMAGE_TYPE_VISIBLE = 4
};
typedef enum qa_weapon_load_result {
  QA_WEAPON_LOAD_OK = 0,
  QA_WEAPON_LOAD_FAILED = 1
} qa_weapon_load_result;
typedef struct qa_projectile_info_bots_a {
  char name[80];
  double speed, gravity;
  int64_t damage, splash_damage, splash_radius, damage_type, flags;
} qa_projectile_info_bots_a;
typedef struct qa_weapon_info_bots_a {
  bool valid;
  int64_t number;
  char name[80];
  double speed;
  qa_projectile_info_bots_a projectile;
  int64_t ammo_index, fire_rate_ms;
} qa_weapon_info_bots_a;
typedef struct qa_weapon_config_bots_a {
  qa_weapon_info_bots_a *weapons;
  size_t weapon_count;
  qa_projectile_info_bots_a *projectiles;
  size_t projectile_count;
} qa_weapon_config_bots_a;
typedef struct qa_weapon_ai_bots_a {
  qa_weapon_config_bots_a config;
  bool has_config;
  int64_t states[QA_MAX_WEAPON_STATES + 1];
} qa_weapon_ai_bots_a;
void qa_weapon_ai_bots_a_init(qa_weapon_ai_bots_a *w);
void qa_weapon_ai_bots_a_destroy(qa_weapon_ai_bots_a *w);
int64_t qa_weapon_ai_allocate(qa_weapon_ai_bots_a *w);
void qa_weapon_ai_free(qa_weapon_ai_bots_a *w, int64_t handle);
const qa_weapon_info_bots_a *qa_weapon_ai_get_weapon_info(qa_weapon_ai_bots_a *w, int64_t handle,
                                                          int64_t weapon);
bool qa_weapon_ai_set_config(qa_weapon_ai_bots_a *w, const qa_weapon_info_bots_a *weapons,
                             size_t count);
#endif
