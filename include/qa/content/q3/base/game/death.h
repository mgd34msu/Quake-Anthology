/* Port of src/content/q3/base/game/death.ts. GPL-2.0-or-later.
 * Items are ../shared/items.ts; death animations
 * use the landed foundation/character sequence; stat schema and
 * persistent indexes use landed shared/definitions.h. */
#ifndef QA_CONTENT_Q3_BASE_GAME_DEATH_H
#define QA_CONTENT_Q3_BASE_GAME_DEATH_H
#include <stdbool.h>
#include "qa/content/q3/base/game/item_motion.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/foundation/character.h"
void qa_q3_return_persistent_powerup(qa_q3_game_entity *powerup, qa_q3_base_server_world world);
typedef void (*qa_q3_return_item_fn)(void *ctx, qa_q3_game_entity *powerup);
void qa_q3_toss_client_persistent_powerup(qa_q3_game_entity *entity, qa_q3_return_item_fn return_item, void *return_ctx);
/* TS DeathFrame. */
typedef struct qa_q3_death_frame {
  double time, game_type, warmup_time, intermission_time; bool blood;
} qa_q3_death_frame;
/* TS DeathHost (missionpack extras used only for missionpack). */
typedef struct qa_q3_death_host {
  void *ctx; qa_q3_base_product product; bool character_death_selected;
  qa_q3_death_animation_sequence *death_animations;
  qa_q3_entity_pool *pool; qa_q3_base_server_world world;
  double (*point_contents)(void *ctx, qa_vec3 point, double pass);
  double (*random_value)(void *ctx); double (*crandom)(void *ctx);
  qa_q3_base_player_state_slots *team_scores; /* length must be 4 */
  void (*hook_free)(void *ctx, qa_q3_game_entity *hook);
  qa_q3_death_frame (*frame)(void *ctx);
  void (*calculate_ranks)(void *ctx);
  void (*send_scoreboard)(void *ctx, qa_q3_game_entity *entity);
  void (*log)(void *ctx, const char *message);
  void (*team_frag_bonuses)(void *ctx, qa_q3_game_entity *victim, qa_q3_game_entity *attacker);
  void (*return_flag)(void *ctx, double team);
  /* TS host.items: Pick<LaunchItemContext, touchItem|droppedFlagThink|checkDroppedTeamItem>. */
  qa_q3_entity_callback touch_item, dropped_flag_think, check_dropped_team_item;
  qa_q3_game_entity *(*neutral_obelisk)(void *ctx);
  double (*cube_timeout_seconds)(void *ctx);
  void (*start_kamikaze)(void *ctx, qa_q3_game_entity *timer);
} qa_q3_death_host;
typedef struct qa_q3_death_runtime { qa_q3_death_host host; } qa_q3_death_runtime;
void qa_q3_death_runtime_init(qa_q3_death_runtime *runtime, qa_q3_death_host host);
void qa_q3_death_bind_save_callbacks(qa_q3_death_runtime *runtime);
void qa_q3_death_score_plum(qa_q3_death_runtime *runtime, qa_q3_game_entity *entity, qa_vec3 origin, double score);
void qa_q3_death_add_score(qa_q3_death_runtime *runtime, qa_q3_game_entity *entity, qa_vec3 origin, double score);
void qa_q3_death_toss_client_items(qa_q3_death_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_death_toss_client_cubes(qa_q3_death_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_death_toss_client_persistant_powerups(qa_q3_death_runtime *runtime, qa_q3_game_entity *entity);
/* inflictor/attacker NULL is TS null. */
void qa_q3_death_look_at_killer(qa_q3_death_runtime *runtime, qa_q3_game_entity *self,
    const qa_q3_damage_participant *inflictor, const qa_q3_damage_participant *attacker);
void qa_q3_death_gib_entity(qa_q3_death_runtime *runtime, qa_q3_game_entity *self, double killer);
void qa_q3_death_body_die(qa_q3_death_runtime *runtime, qa_q3_game_entity *self);
void qa_q3_death_player_die(qa_q3_death_runtime *runtime, qa_q3_game_entity *self,
    const qa_q3_damage_participant *inflictor, const qa_q3_damage_participant *attacker,
    double damage, double means_of_death);
#endif
