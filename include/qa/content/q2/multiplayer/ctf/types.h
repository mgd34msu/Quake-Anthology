/*
 * Port of src/content/q2/multiplayer/ctf/types.ts. Q2CtfHooks carries the
 * one Q2Weapons (qa_q2w_weapons) and Q2PlayerState
 * (qa_q2_base_player_state) the composition owns. Plain TS Errors raise
 * qa_raise("Error", ...). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_CTF_TYPES_H
#define QA_CONTENT_Q2_MULTIPLAYER_CTF_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/equipment/grapple_weapon.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/contracts/equipment.h"
#include "qa/contracts/session.h"

/* ---- ctf/types.ts ---- */
typedef double qa_q2_ctf_team;         /* 0 | 1 | 2 */
typedef double qa_q2_ctf_playing_team; /* 1 | 2 */
typedef struct qa_q2_ctf_score_row {
  double slot; const char *name; double score, ping;
  bool has_carried_flag; qa_q2_ctf_playing_team carried_flag;
} qa_q2_ctf_score_row;
typedef struct qa_q2_ctf_menu_entry {
  char *label;          /* owned */
  const char *action;   /* Q2CtfMenuAction string or NULL */
} qa_q2_ctf_menu_entry;
typedef struct qa_q2_ctf_admin_settings {
  double match_minutes, setup_minutes, start_seconds;
  bool weapons_stay, instant_items, quad_drop, instant_weapons, match_lock;
} qa_q2_ctf_admin_settings;
/* TS Q2CtfEvent union on kind (exact strings). Row/entry arrays are
 * borrowed for the emit call; layout/title/text borrowed. */
typedef struct qa_q2_ctf_event {
  const char *kind;
  qa_actor_id actor;
  const qa_q2_ctf_score_row *red, *blue, *spectators;
  size_t red_count, blue_count, spectator_count;
  double captures[2], totals[2];
  const char *layout, *title, *text, *match;
  const qa_q2_ctf_menu_entry *entries; size_t entry_count;
  const char *flag_states[2]; /* "base"|"dropped"|"taken" */
  qa_q2_ctf_team team;
  bool has_carried_flag; qa_q2_ctf_playing_team carried_flag;
  const char *tech; /* nullable Q2CtfTech classname */
  bool has_id_target; qa_actor_id id_target;
  bool has_blink_team; qa_q2_ctf_playing_team blink_team;
  qa_q2_ctf_admin_settings settings;
  qa_vec3 start, end, offset; /* grapple-cable */
} qa_q2_ctf_event;

typedef struct qa_q2_ctf_rules {
  const char *force_join; /* "" | "red" | "blue" (borrowed) */
  double competition;
  bool match_lock;
  double election_percentage, match_minutes, setup_minutes, start_seconds;
  char *admin_password; /* owned */
  const char **warp_list; size_t warp_count; /* borrowed strings, owned array */
  double capture_limit;
  bool instant_weapons;
} qa_q2_ctf_rules;
/* TS createQ2CtfRules(changes?): apply changes via the _with form
 * (non-NULL fields of `changes` per the mask bits below). */
enum {
  QA_Q2_CTF_RULES_FORCE_JOIN = 1u << 0, QA_Q2_CTF_RULES_COMPETITION = 1u << 1,
  QA_Q2_CTF_RULES_MATCH_LOCK = 1u << 2, QA_Q2_CTF_RULES_ELECTION_PERCENTAGE = 1u << 3,
  QA_Q2_CTF_RULES_MATCH_MINUTES = 1u << 4, QA_Q2_CTF_RULES_SETUP_MINUTES = 1u << 5,
  QA_Q2_CTF_RULES_START_SECONDS = 1u << 6, QA_Q2_CTF_RULES_ADMIN_PASSWORD = 1u << 7,
  QA_Q2_CTF_RULES_WARP_LIST = 1u << 8, QA_Q2_CTF_RULES_CAPTURE_LIMIT = 1u << 9,
  QA_Q2_CTF_RULES_INSTANT_WEAPONS = 1u << 10
};
void qa_create_q2_ctf_rules(qa_q2_ctf_rules *out);
void qa_create_q2_ctf_rules_with(qa_q2_ctf_rules *out, const qa_q2_ctf_rules *changes, unsigned mask);
void qa_q2_ctf_rules_destroy(qa_q2_ctf_rules *rules);

typedef struct qa_q2_ctf_player_state {
  qa_q2_ctf_team team;
  double spawn_state;
  bool has_last_hurt_carrier; double last_hurt_carrier;
  bool has_last_returned_flag; double last_returned_flag;
  bool has_last_fragged_carrier; double last_fragged_carrier;
  double flag_since;
  bool voted, ready, admin, id_view;
  bool has_ghost_code; double ghost_code;
  double regen_time, tech_sound_time, last_tech_message;
  bool has_match_respawn_at; double match_respawn_at;
} qa_q2_ctf_player_state;
void qa_q2_ctf_player_state_init(qa_q2_ctf_player_state *state);

typedef struct qa_q2_ctf_ghost {
  double code; qa_q2_ctf_playing_team team; char *name; /* owned */
  bool has_actor; qa_actor_id actor;
  double score, deaths, kills, captures, base_defense, carrier_defense;
} qa_q2_ctf_ghost;
typedef struct qa_q2_ctf_election {
  const char *kind; /* "match"|"admin"|"map" */
  qa_actor_id target;
  char *map, *message; /* owned */
  double votes, needed, expires;
} qa_q2_ctf_election;
typedef struct qa_q2_ctf_match_state {
  double team1, team2, total1, total2;
  bool has_last_flag_capture; double last_flag_capture;
  bool has_last_capture_team; qa_q2_ctf_playing_team last_capture_team;
  const char *phase; /* "none"|"setup"|"pregame"|"game"|"post" */
  double match_time, last_time;
  qa_q2_ctf_election *election; /* owned or NULL */
  qa_q2_ctf_ghost *ghosts; size_t ghost_count, ghost_cap; /* insertion order */
} qa_q2_ctf_match_state;
void qa_q2_ctf_match_state_init(qa_q2_ctf_match_state *state);
void qa_q2_ctf_match_state_destroy(qa_q2_ctf_match_state *state);
qa_q2_ctf_ghost *qa_q2_ctf_match_state_ghost(qa_q2_ctf_match_state *state, double code);
void qa_q2_ctf_match_state_set_ghost(qa_q2_ctf_match_state *state, qa_q2_ctf_ghost ghost); /* takes name */
void qa_q2_ctf_match_state_delete_ghost(qa_q2_ctf_match_state *state, double code);
void qa_q2_ctf_match_state_clear_ghosts(qa_q2_ctf_match_state *state);
void qa_q2_ctf_match_state_set_election(qa_q2_ctf_match_state *state, qa_q2_ctf_election *election); /* takes ownership; NULL clears */

typedef struct qa_q2_ctf_hooks {
  void *ctx;
  qa_q2_item_module *items;
  qa_q2w_weapons *weapons; /* borrowed */
  qa_q2_base_player_state *(*player)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  void (*set_skin)(void *ctx, qa_actor_id actor, const char *skin);
  void (*spawn_player)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*observer)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*teleport)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity);
  void (*chase)(void *ctx, qa_actor_id actor);
  void (*set_grapple_prediction)(void *ctx, qa_actor_id actor, bool suppressed);
  double (*gravity)(void *ctx);
  void (*emit)(void *ctx, qa_q2_ctf_event event);
  void (*end_level)(void *ctx, qa_q2_game_services *game, const char *map); /* map NULL = TS null */
  void (*kick)(void *ctx, qa_actor_id actor);
  void (*set_deathmatch_flags)(void *ctx, double flags);
  bool (*chat_allowed)(void *ctx, qa_actor_id actor, qa_q2_game_services *game);
} qa_q2_ctf_hooks;

typedef struct qa_q2_ctf_state_entry {
  qa_actor_id actor;
  qa_q2_ctf_player_state state;
} qa_q2_ctf_state_entry;
typedef struct qa_q2_ctf_context {
  qa_q2_ctf_hooks hooks;
  qa_q2_ctf_rules rules;
  qa_q2_ctf_state_entry *states; size_t state_count, state_cap; /* insertion order */
  qa_q2_ctf_match_state match;
} qa_q2_ctf_context;
qa_q2_ctf_player_state *qa_q2_ctf_context_get(qa_q2_ctf_context *context, qa_actor_id actor);
qa_q2_ctf_player_state *qa_q2_ctf_context_set(qa_q2_ctf_context *context, qa_actor_id actor, qa_q2_ctf_player_state state);
void qa_q2_ctf_context_delete(qa_q2_ctf_context *context, qa_actor_id actor);
void qa_q2_ctf_context_clear(qa_q2_ctf_context *context);

typedef struct qa_q2_ctf_flag_def {
  const char *classname; qa_item_id item; const char *model, *icon; double effect;
} qa_q2_ctf_flag_def;
extern const qa_q2_ctf_flag_def QA_CTF_FLAGS[3]; /* index by playing team (1,2) */

qa_q2_ctf_playing_team qa_other_ctf_team(qa_q2_ctf_playing_team team);
const char *qa_ctf_team_name(qa_q2_ctf_team team);
/* TS ctfPlayer: aborts when not admitted. */
qa_q2_ctf_player_state *qa_ctf_player(qa_q2_ctf_context *context, qa_actor_id actor);
/* level NULL = "high"; actor NULL = broadcast. text is formatted here. */
void qa_ctf_print(qa_q2_game_services *game, const qa_actor_id *actor, const char *level, const char *format, ...);
const char *qa_ctf_name(qa_q2_ctf_context *context, qa_actor_id actor);
void qa_ctf_score(qa_q2_ctf_context *context, qa_actor_id actor, double amount);
qa_saved_actor_id qa_save_ctf_actor(qa_actor_id actor);
bool qa_ctf_carried_flag(qa_q2_game_services *game, qa_actor_id actor, qa_q2_ctf_playing_team *out); /* false = null */
#endif
