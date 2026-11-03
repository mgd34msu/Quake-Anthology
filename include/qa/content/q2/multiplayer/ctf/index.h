/* Port of src/content/q2/multiplayer/ctf/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_INDEX_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_INDEX_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/multiplayer/ctf/checkpoint.h"
#include "qa/content/q2/multiplayer/ctf/flags.h"
#include "qa/content/q2/multiplayer/ctf/grapple.h"
#include "qa/content/q2/multiplayer/ctf/match.h"
#include "qa/content/q2/multiplayer/ctf/presentation.h"
#include "qa/content/q2/multiplayer/ctf/techs.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
typedef struct qa_q2_ctf qa_q2_ctf;
/* rules/changes NULL = defaults; mask per QA_Q2_CTF_RULES_*. */
qa_q2_ctf *qa_q2_ctf_create(qa_q2_ctf_hooks hooks, const qa_q2_ctf_rules *changes, unsigned mask, qa_shared_grapple_control *shared_grapple);
void qa_q2_ctf_destroy(qa_q2_ctf *ctf);
qa_q2_spawn_module qa_q2_ctf_as_spawn(qa_q2_ctf *ctf);
qa_q2_ctf_context *qa_q2_ctf_get_context(qa_q2_ctf *ctf);
qa_q2_ctf_flags *qa_q2_ctf_get_flags(qa_q2_ctf *ctf);
qa_q2_ctf_match *qa_q2_ctf_get_match(qa_q2_ctf *ctf);
qa_q2_ctf_presentation *qa_q2_ctf_get_presentation(qa_q2_ctf *ctf);
qa_q2_ctf_grapple *qa_q2_ctf_get_grapple(qa_q2_ctf *ctf);
qa_q2_ctf_techs *qa_q2_ctf_get_techs(qa_q2_ctf *ctf);
qa_q2_callback_definitions qa_q2_ctf_callbacks(qa_q2_ctf *ctf);
const char *qa_q2_ctf_item_name(qa_q2_ctf *ctf, const char *classname);
bool qa_q2_ctf_spawn(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_after_spawn(qa_q2_ctf *ctf, qa_q2_game_services *game);
void qa_q2_ctf_admitted(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_assign_skin(qa_q2_ctf *ctf, qa_q2_entity *entity);
bool qa_q2_ctf_join(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_ctf_playing_team team, bool ghost);
void qa_q2_ctf_team_command(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game, const char *name);
void qa_q2_ctf_observer(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_ctf_select_spawn(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 *out_origin, qa_vec3 *out_angles);
void qa_q2_ctf_score_event(qa_q2_ctf *ctf, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *game, double change, double means, qa_q2_entity *recipient);
bool qa_q2_ctf_same_team(qa_q2_ctf *ctf, const qa_actor_id *one, qa_actor_id two);
void qa_q2_ctf_drop_inventory(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_death(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_disconnect(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_before_player(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2w_input qa_q2_ctf_weapon_input(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2w_input input);
void qa_q2_ctf_after_player(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_after_player_frames(qa_q2_ctf *ctf, qa_q2_game_services *game);
bool qa_q2_ctf_check_rules(qa_q2_ctf *ctf, qa_q2_game_services *game);
bool qa_q2_ctf_match_setup_phase(qa_q2_ctf *ctf);
bool qa_q2_ctf_pickups_allowed(qa_q2_ctf *ctf);
void qa_q2_ctf_reset_players(qa_q2_ctf *ctf, qa_q2_game_services *game);
bool qa_q2_ctf_command(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game, const char *command, const char *const *args, size_t arg_count);
void qa_q2_ctf_menu_action(qa_q2_ctf *ctf, qa_q2_entity *entity, qa_q2_game_services *game, const char *action);
void qa_q2_ctf_capture(qa_q2_ctf *ctf, qa_q2_ctf_checkpoint *out);
void qa_q2_ctf_restore(qa_q2_ctf *ctf, const qa_q2_ctf_checkpoint *checkpoint, qa_q2_game_services *game);
#endif
