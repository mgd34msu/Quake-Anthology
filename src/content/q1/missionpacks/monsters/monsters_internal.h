/*
 * Slice-internal helpers for the q1/missionpacks/monsters port:
 * foundation vector ops (owner src/content/q1/foundation/types.ts,
 * built on landed qa/core/math.h) and mirrored declarations for
 * unlanded cross-module functions. Mirrored functions keep their TS
 * names prefixed qa_q1mpm_ext_; link against their owners when landed:
 *  - projectiles: src/content/q1/base/projectiles.ts
 *  - fire_bullets: src/content/q1/foundation/weapons.ts
 *  - hipnotic weapons: src/content/q1/missionpacks/hipnotic-weapons.ts
 *  - rogue plasma: src/content/q1/missionpacks/rogue-weapons.ts
 *  - multi_explosion: src/content/q1/missionpacks/world/hipnotic-misc.ts
 *  - teleport fog/death: src/content/q1/foundation/spawns.ts
 *  - register_monster_callbacks: src/content/q1/base/monsters.ts
 * velocity_angles is implemented in runtime.c (owner
 * src/content/q1/missionpacks/types.ts, mirrored formula).
 * GPL-2.0-or-later.
 */
#ifndef QA_Q1MPM_MONSTERS_INTERNAL_H
#define QA_Q1MPM_MONSTERS_INTERNAL_H

#include <math.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/content/q1/missionpacks/monsters/runtime.h"

#define QA_Q1MPM_ZERO ((qa_vec3){0, 0, 0})
#define QA_Q1MPM_POINT ((qa_bounds){{0, 0, 0}, {0, 0, 0}})
#define QA_Q1MPM_V3(x, y, z) ((qa_vec3){(x), (y), (z)})
#define QA_Q1MPM_NONE ((qa_q1mpm_opt_actor){false, {{NULL, NULL}, 0, 0, NULL}})
#define QA_Q1MPM_SOME(id) ((qa_q1mpm_opt_actor){true, (id)})
#define QA_Q1MPM_CH(name) ((qa_q1mpm_channel){(name), 0})
#define QA_Q1MPM_CH_NUM(n) ((qa_q1mpm_channel){NULL, (n)})

static inline qa_vec3 qa_q1mpm_vadd(qa_vec3 a, qa_vec3 b) { return qa_add3_double(a, b); }
static inline qa_vec3 qa_q1mpm_vsub(qa_vec3 a, qa_vec3 b) { return qa_sub3_double(a, b); }
static inline qa_vec3 qa_q1mpm_vscale(qa_vec3 a, double s) { return qa_scale3_double(a, s); }
static inline double qa_q1mpm_dot(qa_vec3 a, qa_vec3 b) { return qa_dot3_double(a, b); }
static inline double qa_q1mpm_length(qa_vec3 a) { return qa_length3_double(a); }
static inline qa_vec3 qa_q1mpm_normalize(qa_vec3 a) {
  double magnitude = qa_length3_double(a);
  return magnitude == 0 ? QA_Q1MPM_ZERO : qa_scale3_double(a, 1 / magnitude);
}
static inline double qa_q1mpm_yaw_for(qa_vec3 direction) {
  double yaw = atan2(direction.y, direction.x) * 180 / 3.141592653589793;
  return yaw < 0 ? yaw + 360 : yaw;
}
static inline bool qa_q1mpm_opt_eq(qa_q1mpm_opt_actor a, qa_q1mpm_opt_actor b) {
  if (a.has != b.has) return false;
  return !a.has || qa_same_actor(a.id, b.id);
}
static inline bool qa_q1mpm_id_eq_opt(qa_actor_id a, qa_q1mpm_opt_actor b) {
  return b.has && qa_same_actor(a, b.id);
}
static inline bool qa_q1mpm_str_eq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
static inline qa_q1mpm_basis qa_q1mpm_vectors(qa_vec3 angles) {
  return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1);
}
qa_vec3 qa_q1mpm_velocity_angles(qa_vec3 velocity); /* runtime.c */
/* TS callbackName(): the bound name or NULL for a null callback. */
static inline const char *qa_q1mpm_callback_name(qa_q1mpm_callback callback) { return callback.name; }

/* TS BackpackContents (base/projectiles.ts) subset used here. */
typedef struct qa_q1mpm_ext_backpack_contents {
  const char *weapon; /* TS Q1Weapon | null */
  double shells; double nails; double rockets; double cells;
} qa_q1mpm_ext_backpack_contents;

qa_q1mpm_actor *qa_q1mpm_ext_create_missile(qa_q1mpm_game *game, qa_q1mpm_opt_actor owner,
                                            const char *classname, const char *model,
                                            qa_vec3 origin, qa_vec3 velocity);
qa_q1mpm_actor *qa_q1mpm_ext_throw_gib(qa_q1mpm_game *game, qa_vec3 origin, const char *model, double damage);
void qa_q1mpm_ext_throw_head(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *model);
void qa_q1mpm_ext_throw_head_damage(qa_q1mpm_game *game, qa_q1mpm_actor *entity, const char *model, double damage);
qa_q1mpm_actor *qa_q1mpm_ext_spawn_meat_spray(qa_q1mpm_game *game, qa_q1mpm_actor *owner,
                                              qa_vec3 origin, qa_vec3 velocity);
qa_q1mpm_actor *qa_q1mpm_ext_drop_backpack(qa_q1mpm_game *game, qa_vec3 origin,
                                           const qa_q1mpm_ext_backpack_contents *contents);
qa_q1mpm_actor *qa_q1mpm_ext_launch_spike(qa_q1mpm_game *game, qa_q1mpm_opt_actor owner,
                                          qa_vec3 origin, qa_vec3 velocity);
qa_q1mpm_actor *qa_q1mpm_ext_launch_laser(qa_q1mpm_game *game, qa_q1mpm_opt_actor owner,
                                          qa_vec3 origin, qa_vec3 direction);
void qa_q1mpm_ext_launch_ogre_grenade(qa_q1mpm_mission_monster *monster);
void qa_q1mpm_ext_fire_bullets(qa_q1mpm_game *game, qa_owned_actor shooter, qa_vec3 direction,
                               qa_vec3 view_angles, double count, double spread_x, double spread_y,
                               const char *weapon);
qa_q1mpm_actor *qa_q1mpm_ext_launch_hipnotic_laser(qa_q1mpm_game *game, qa_actor_id shooter,
                                                   qa_vec3 origin, qa_vec3 direction, bool light);
qa_q1mpm_actor *qa_q1mpm_ext_launch_hipnotic_proximity(qa_q1mpm_game *game, qa_actor_id owner,
                                                       qa_vec3 origin, qa_vec3 velocity);
qa_q1mpm_actor *qa_q1mpm_ext_launch_rogue_plasma(qa_q1mpm_game *game, qa_actor_id owner,
                                                 qa_vec3 origin, qa_vec3 direction);
qa_q1mpm_actor *qa_q1mpm_ext_multi_explosion(qa_q1mpm_game *game, qa_q1mpm_actor *source, qa_vec3 origin,
                                             double radius, double damage, double duration,
                                             double pause, double volume);
qa_q1mpm_actor *qa_q1mpm_ext_spawn_teleport_fog(qa_q1mpm_game *game, qa_vec3 origin);
qa_q1mpm_actor *qa_q1mpm_ext_spawn_teledeath(qa_q1mpm_game *game, qa_vec3 origin, qa_actor_id owner);
typedef qa_q1mpm_mission_monster *(*qa_q1mpm_ext_monster_lookup_fn)(void *ctx, qa_q1mpm_actor *entity);
void qa_q1mpm_ext_register_monster_callbacks(qa_q1mpm_game *game, const char *prefix,
                                             qa_q1mpm_ext_monster_lookup_fn lookup, void *lookup_ctx);

#endif
