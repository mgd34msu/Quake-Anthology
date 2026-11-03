/* Port of src/content/q2/missionpacks/modes/deathball.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MODES_DEATHBALL_H
#define QA_CONTENT_Q2_MISSIONPACKS_MODES_DEATHBALL_H
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/missionpacks/players.h"
#include "qa/contracts/session.h"

/* TS Q2DeathBallHooks settings() result. */
typedef struct qa_q2mp_deathball_settings {
  const char *team1_skin, *team2_skin; double goal_limit;
} qa_q2mp_deathball_settings;
/* TS Q2DeathBallHooks. */
typedef struct qa_q2mp_deathball_hooks {
  void *ctx;
  qa_q2mp_deathball_settings (*settings)(void *ctx);
  const char *(*skin)(void *ctx, qa_actor_id actor);
  void (*set_skin)(void *ctx, qa_actor_id actor, const char *skin);
  void (*add_score)(void *ctx, qa_actor_id actor, double amount);
  void (*end_level)(void *ctx);
  qa_q2mp_spawn_point (*select_spawn)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  double (*spawn_distance)(void *ctx, qa_q2_entity *spot, qa_q2_game_services *game);
} qa_q2mp_deathball_hooks;
/* TS Q2DeathBallCheckpoint. */
typedef struct qa_q2mp_deathball_checkpoint {
  bool has_ball; qa_saved_actor_id ball;
  double starts, team1_score, team2_score;
} qa_q2mp_deathball_checkpoint;
/* TS q2DeathBallRules result. */
typedef struct qa_q2mp_deathball_rules { double stop_speed, deathmatch_flags; } qa_q2mp_deathball_rules;
qa_q2mp_deathball_rules qa_q2mp_deathball_get_rules(double deathmatch_flags);

/* TS Q2DeathBall. */
typedef struct qa_q2mp_deathball {
  qa_q2mp_deathball_hooks hooks;
  bool has_ball; qa_actor_id ball;
  double starts, team1_score, team2_score;
  qa_q2_named_think cb_think[1];
  qa_q2_named_touch cb_touch[3];
  qa_q2_named_pain cb_pain[1];
  qa_q2_named_die cb_die[1];
  qa_q2_think respawn;
  qa_q2_touch ball_touch, goal_touch, speed_touch;
  qa_q2_pain pain;
  qa_q2_die die;
} qa_q2mp_deathball;

void qa_q2mp_deathball_init(qa_q2mp_deathball *ball, qa_q2mp_deathball_hooks hooks);
qa_q2_callback_definitions qa_q2mp_deathball_callbacks(qa_q2mp_deathball *ball);
qa_q2mp_deathball_checkpoint qa_q2mp_deathball_capture(qa_q2mp_deathball *ball);
void qa_q2mp_deathball_restore(qa_q2mp_deathball *ball, qa_q2_game_services *game, qa_q2mp_deathball_checkpoint saved);
/* False return is TS null. */
bool qa_q2mp_deathball_ball_actor(qa_q2mp_deathball *ball, qa_actor_id *out);
void qa_q2mp_deathball_scores(qa_q2mp_deathball *ball, double out[2]);
bool qa_q2mp_deathball_check_rules(qa_q2mp_deathball *ball, qa_q2_game_services *game);
void qa_q2mp_deathball_client_begin(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2mp_spawn_point qa_q2mp_deathball_select_spawn(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2mp_deathball_post_spawn(qa_q2mp_deathball *ball, qa_q2_game_services *game);
double qa_q2mp_deathball_change_damage(qa_q2mp_deathball *ball, qa_actor_id target, bool has_attacker,
                                       qa_actor_id attacker, double damage);
double qa_q2mp_deathball_change_knockback(qa_q2mp_deathball *ball, qa_actor_id target, double knockback,
                                          double means_of_death, qa_q2_game_services *game);
bool qa_q2mp_deathball_spawn(qa_q2mp_deathball *ball, qa_q2_entity *entity, qa_q2_game_services *game);
#endif
