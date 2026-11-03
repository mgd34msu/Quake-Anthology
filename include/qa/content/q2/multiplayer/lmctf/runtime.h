/* Port of src/content/q2/multiplayer/lmctf/runtime.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_RUNTIME_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_RUNTIME_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/lmctf/flags.h"
#include "qa/content/q2/multiplayer/lmctf/grapple.h"
#include "qa/content/q2/multiplayer/lmctf/match.h"
#include "qa/content/q2/multiplayer/lmctf/runes.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/vote.h"
#include "qa/content/q2/multiplayer/lmctf/weapons.h"
#include "qa/persistence/value.h"
typedef struct qa_q2_lmctf qa_q2_lmctf;
/* TS capture() object: rules minus passwords plus module state. */
typedef struct qa_lmctf_rules_checkpoint {
  double time_limit_minutes, frag_limit;
  const char **map_list; size_t map_count;
  double ctf_flags, ref_flags, runes, skin_set;
  bool flag_init;
  double disabled_weapons;
  bool fast_switch, auto_lock;
  double countdown_seconds, quad_seconds;
} qa_lmctf_rules_checkpoint;
typedef struct qa_lmctf_player_checkpoint_entry {
  qa_saved_actor_id actor;
  bool plasma_mode;
  qa_lmctf_team team, observer_team;
  bool has_rune; qa_saved_actor_id rune;
  qa_lmctf_grapple_checkpoint grapple;
  double regen_frame, kill_carrier_time, hit_carrier_time, return_flag_time, defend_flag_time;
  double extra_flags, spawn_state;
  qa_lmctf_statistic *statistics; size_t statistic_count;
} qa_lmctf_player_checkpoint_entry;
typedef struct qa_lmctf_capture {
  qa_lmctf_rules_checkpoint rules;
  qa_lmctf_match_checkpoint match;
  bool has_vote_started; double vote_started_at;
  bool plasma_quad;
  qa_lmctf_flags_checkpoint flags;
  bool runes_forward;
  qa_lmctf_player_checkpoint_entry *players; size_t player_count;
} qa_lmctf_capture;
void qa_lmctf_capture_destroy(qa_lmctf_capture *capture);
/* rules NULL = TS default createLmctfRules() (copied). travel NULL =
 * TS undefined. shared NULL = TS null. */
qa_q2_lmctf *qa_q2_lmctf_create(qa_lmctf_hooks hooks, const qa_lmctf_rules *rules,
                                const qa_lmctf_travel *travel, qa_shared_grapple_control *shared_grapple);
void qa_q2_lmctf_destroy(qa_q2_lmctf *lmctf);
qa_q2_spawn_module qa_q2_lmctf_as_spawn(qa_q2_lmctf *lmctf);
qa_lmctf_context *qa_q2_lmctf_get_context(qa_q2_lmctf *lmctf);
qa_lmctf_rules *qa_q2_lmctf_get_rules(qa_q2_lmctf *lmctf);
qa_lmctf_match *qa_q2_lmctf_get_match(qa_q2_lmctf *lmctf);
qa_lmctf_vote *qa_q2_lmctf_get_vote(qa_q2_lmctf *lmctf);
qa_lmctf_weapons *qa_q2_lmctf_get_weapons(qa_q2_lmctf *lmctf);
qa_lmctf_flags *qa_q2_lmctf_get_flags(qa_q2_lmctf *lmctf);
qa_lmctf_runes *qa_q2_lmctf_get_runes(qa_q2_lmctf *lmctf);
qa_lmctf_grapple *qa_q2_lmctf_get_grapple(qa_q2_lmctf *lmctf);
qa_q2_callback_definitions qa_q2_lmctf_callbacks(qa_q2_lmctf *lmctf);
bool qa_q2_lmctf_spawn(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_lmctf_post_spawn(qa_q2_lmctf *lmctf, qa_q2_game_services *game);
void qa_q2_lmctf_capture_travel(qa_q2_lmctf *lmctf, qa_lmctf_travel *out);
void qa_q2_lmctf_capture(qa_q2_lmctf *lmctf, qa_lmctf_capture *out);
/* TS capture() as the tree encodeCheckpointValue encodes: same keys, same
 * order. Owned by the caller (qa_save_value_free). */
qa_save_value *qa_q2_lmctf_capture_value(qa_q2_lmctf *lmctf);
void qa_q2_lmctf_restore(qa_q2_lmctf *lmctf, const qa_save_reader *reader, qa_q2_game_services *game);
void qa_q2_lmctf_team_totals(qa_q2_lmctf *lmctf, double out[2]);
void qa_q2_lmctf_admitted(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_lmctf_player_spawned(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_lmctf_join(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game, qa_lmctf_playing_team team);
void qa_q2_lmctf_observer(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game, qa_lmctf_team team);
void qa_q2_lmctf_select_spawn(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_q2_lmctf_score_event(qa_q2_lmctf *lmctf, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *game, double change, double means, qa_q2_entity *recipient);
void qa_q2_lmctf_drop_inventory(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_lmctf_player_death(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_lmctf_disconnect(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_lmctf_player_frame(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_lmctf_can_move(qa_q2_lmctf *lmctf, qa_actor_id actor);
double qa_q2_lmctf_gravity_scale(qa_q2_lmctf *lmctf, qa_actor_id actor);
bool qa_q2_lmctf_command(qa_q2_lmctf *lmctf, qa_q2_entity *entity, qa_q2_game_services *game, const char *name, const char *const *args, size_t arg_count);
#endif
