/* Port of src/content/q2/missionpacks/spheres.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_SPHERES_H
#define QA_CONTENT_Q2_MISSIONPACKS_SPHERES_H
#include "qa/content/q2/missionpacks/projectiles/trap.h"
#include "qa/contracts/gameplay.h"

/* TS Q2SphereKind: "defender" | "hunter" | "vengeance". */
typedef enum qa_q2mp_sphere_kind { QA_Q2MP_SPHERE_DEFENDER, QA_Q2MP_SPHERE_HUNTER, QA_Q2MP_SPHERE_VENGEANCE } qa_q2mp_sphere_kind;
const char *qa_q2mp_sphere_kind_name(qa_q2mp_sphere_kind kind);

/* TS Q2SphereHooks. */
typedef struct qa_q2mp_sphere_hooks {
  bool hunter_camera;
  void *ctx;
  bool (*intermission)(void *ctx);
  void (*player_effect)(void *ctx, qa_q2mp_player_effect effect);
} qa_q2mp_sphere_hooks;

/* TS Q2MissionPackSpheres. */
typedef struct qa_q2mp_spheres {
  qa_q2mp_projectiles *projectiles;
  qa_q2mp_sphere_hooks hooks;
  qa_q2_named_think cb_think[4];
  qa_q2_named_pain cb_pain[3];
  qa_q2_named_die cb_die[2];
  qa_q2_named_touch cb_touch[2];
  qa_q2_think expire, defender_think, hunter_think, vengeance_think;
  qa_q2_pain defender_pain, hunter_pain, vengeance_pain;
  qa_q2_die die, idle_die;
  qa_q2_touch hunter_touch, vengeance_touch;
} qa_q2mp_spheres;

void qa_q2mp_spheres_init(qa_q2mp_spheres *spheres, qa_q2mp_projectiles *projectiles, qa_q2mp_sphere_hooks hooks);
qa_q2_callback_definitions qa_q2mp_spheres_callbacks(qa_q2mp_spheres *spheres);
/* NULL return is TS null. */
qa_q2_entity *qa_q2mp_spheres_owned_sphere(qa_q2mp_spheres *s, qa_actor_id actor, qa_q2_game_services *game);
void qa_q2mp_spheres_owner_damaged(qa_q2mp_spheres *s, qa_actor_id actor, const qa_attack_provenance *attack,
                                   qa_q2_game_services *game);
void qa_q2mp_spheres_owner_died(qa_q2mp_spheres *s, qa_actor_id actor, qa_q2_game_services *game,
                                const qa_attack_provenance *attack);
void qa_q2mp_spheres_disconnect(qa_q2mp_spheres *s, qa_actor_id actor, qa_q2_game_services *game);
qa_q2_entity *qa_q2mp_spheres_launch(qa_q2mp_spheres *s, qa_q2_entity *owner, qa_q2_game_services *game,
                                      qa_q2mp_sphere_kind kind, bool dopple);
#endif
