/* Port of src/content/q2/multiplayer/ctf/match.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_MATCH_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_MATCH_H
#include "qa/content/q2/multiplayer/ctf/types.h"
typedef struct qa_q2_ctf_match_actions {
  void *ctx;
  void (*reset_players)(void *ctx, qa_q2_game_services *game);
  void (*reset_grapple)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  /* ghost: TS optional trailing; pass false. */
  bool (*join)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_ctf_playing_team team, bool ghost);
} qa_q2_ctf_match_actions;
typedef struct qa_q2_ctf_match qa_q2_ctf_match;
qa_q2_ctf_match *qa_q2_ctf_match_create(qa_q2_ctf_context *context, qa_q2_ctf_match_actions actions);
void qa_q2_ctf_match_destroy(qa_q2_ctf_match *match);
void qa_q2_ctf_match_after_spawn(qa_q2_ctf_match *match, qa_q2_game_services *game);
void qa_q2_ctf_match_setup(qa_q2_ctf_match *match, qa_q2_game_services *game);
void qa_q2_ctf_match_assign_ghost(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_ctf_match_sync_ghost(qa_q2_ctf_match *match, qa_actor_id actor);
bool qa_q2_ctf_match_restore_ghost(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, double code);
bool qa_q2_ctf_match_ready(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, bool ready);
void qa_q2_ctf_match_start(qa_q2_ctf_match *match, qa_q2_game_services *game);
void qa_q2_ctf_match_totals(qa_q2_ctf_match *match, qa_q2_game_services *game, double out[2]);
void qa_q2_ctf_match_end(qa_q2_ctf_match *match, qa_q2_game_services *game);
/* map NULL = TS default "". */
bool qa_q2_ctf_match_begin_election(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, const char *kind, const char *map);
bool qa_q2_ctf_match_vote(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, bool yes);
bool qa_q2_ctf_match_check_rules(qa_q2_ctf_match *match, qa_q2_game_services *game);
/* Malloc'd status string. */
char *qa_q2_ctf_match_status(qa_q2_ctf_match *match, qa_q2_game_services *game);
bool qa_q2_ctf_match_admin(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, const char *password);
bool qa_q2_ctf_match_configure(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_ctf_admin_settings values);
void qa_q2_ctf_match_settings_menu(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_ctf_match_warp(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, const char *requested);
bool qa_q2_ctf_match_boot(qa_q2_ctf_match *match, qa_q2_entity *entity, qa_q2_game_services *game, const char *number);
#endif
