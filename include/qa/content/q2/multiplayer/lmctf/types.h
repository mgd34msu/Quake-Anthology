/* Port of src/content/q2/multiplayer/lmctf/types.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_LMCTF_TYPES_H
#define QA_CONTENT_Q2_MULTIPLAYER_LMCTF_TYPES_H
#include "qa/content/q2/multiplayer/ctf/types.h"
typedef qa_q2_ctf_team qa_lmctf_team;
typedef qa_q2_ctf_playing_team qa_lmctf_playing_team;
/* TS LmctfRune is the exact string. */
typedef const char *qa_lmctf_rune;
typedef struct qa_lmctf_rune_definition {
  const char *kind; double bit; const char *classname; qa_item_id item; const char *model, *icon, *name;
} qa_lmctf_rune_definition;
extern const qa_lmctf_rune_definition QA_LMCTF_RUNES[5];
#define QA_LMCTF_RUNE_COUNT 5

typedef struct qa_lmctf_rules {
  double ctf_flags, ref_flags, runes, skin_set;
  bool flag_init;
  double disabled_weapons, time_limit_minutes, frag_limit;
  const char **map_list; size_t map_count; /* borrowed strings, owned array */
  char *rcon_password, *ref_password; /* owned */
  bool fast_switch, auto_lock;
  double countdown_seconds, quad_seconds;
} qa_lmctf_rules;
enum {
  QA_LMCTF_RULES_CTF_FLAGS = 1u << 0, QA_LMCTF_RULES_REF_FLAGS = 1u << 1,
  QA_LMCTF_RULES_RUNES = 1u << 2, QA_LMCTF_RULES_SKIN_SET = 1u << 3,
  QA_LMCTF_RULES_FLAG_INIT = 1u << 4, QA_LMCTF_RULES_DISABLED_WEAPONS = 1u << 5,
  QA_LMCTF_RULES_TIME_LIMIT = 1u << 6, QA_LMCTF_RULES_FRAG_LIMIT = 1u << 7,
  QA_LMCTF_RULES_MAP_LIST = 1u << 8, QA_LMCTF_RULES_RCON_PASSWORD = 1u << 9,
  QA_LMCTF_RULES_FAST_SWITCH = 1u << 10, QA_LMCTF_RULES_REF_PASSWORD = 1u << 11,
  QA_LMCTF_RULES_AUTO_LOCK = 1u << 12, QA_LMCTF_RULES_COUNTDOWN = 1u << 13,
  QA_LMCTF_RULES_QUAD_SECONDS = 1u << 14
};
void qa_create_lmctf_rules(qa_lmctf_rules *out);
void qa_create_lmctf_rules_with(qa_lmctf_rules *out, const qa_lmctf_rules *changes, unsigned mask);
void qa_lmctf_rules_destroy(qa_lmctf_rules *rules);
void qa_lmctf_rules_copy(qa_lmctf_rules *out, const qa_lmctf_rules *in);

typedef struct qa_lmctf_score_row {
  qa_actor_id actor; double slot; const char *name; qa_lmctf_team team; double score, ping;
} qa_lmctf_score_row;
typedef struct qa_lmctf_menu_entry {
  const char *label;   /* borrowed (static in this slice) */
  const char *command; /* nullable */
} qa_lmctf_menu_entry;
/* TS LmctfEvent union on kind (exact strings). Arrays borrowed. */
typedef struct qa_lmctf_event {
  const char *kind;
  qa_actor_id actor;
  qa_vec3 start, end, offset; /* grapple-cable */
  const char *title; const qa_lmctf_menu_entry *entries; size_t entry_count; /* menu */
  const qa_lmctf_score_row *rows; size_t row_count; const char *layout; /* scoreboard/hud */
  qa_lmctf_team team; bool carried_flag; const char *rune; /* hud */
  bool has_victim; qa_actor_id victim; const char *name; double amount, seconds; /* score-log */
} qa_lmctf_event;

typedef struct qa_lmctf_hooks {
  void *ctx;
  qa_q2_item_module *items;
  qa_q2mp_weapons *weapons;
  qa_q2mp_player_state *(*player)(void *ctx, qa_actor_id actor);
  void (*set_skin)(void *ctx, qa_actor_id actor, const char *skin);
  void (*spawn_player)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*observer)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*teleport)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity);
  void (*chase)(void *ctx, qa_actor_id actor);
  void (*set_grapple_prediction)(void *ctx, qa_actor_id actor, bool suppressed);
  double (*gravity)(void *ctx);
  void (*emit)(void *ctx, qa_lmctf_event event);
  void (*end_level)(void *ctx, qa_q2_game_services *game, const char *map);
  void (*kick)(void *ctx, qa_actor_id actor);
  void (*set_deathmatch_flags)(void *ctx, double flags);
  bool (*chat_allowed)(void *ctx, qa_actor_id actor, qa_q2_game_services *game);
} qa_lmctf_hooks;

typedef struct qa_lmctf_statistic { char *key; double count; } qa_lmctf_statistic;
typedef struct qa_lmctf_player_state {
  bool plasma_mode;
  qa_lmctf_team team, observer_team;
  bool has_rune; qa_actor_id rune;
  double regen_frame, kill_carrier_time, hit_carrier_time, return_flag_time, defend_flag_time;
  double extra_flags, spawn_state;
  qa_lmctf_statistic *statistics; size_t statistic_count, statistic_cap; /* insertion order */
} qa_lmctf_player_state;
void qa_lmctf_player_state_init(qa_lmctf_player_state *state);
void qa_lmctf_player_state_destroy(qa_lmctf_player_state *state);
double qa_lmctf_player_state_stat(const qa_lmctf_player_state *state, const char *name, double fallback);
void qa_lmctf_player_state_set_stat(qa_lmctf_player_state *state, const char *name, double count);
void qa_lmctf_player_state_clear_stats(qa_lmctf_player_state *state);

typedef struct qa_lmctf_state_entry {
  qa_actor_id actor;
  qa_lmctf_player_state state;
} qa_lmctf_state_entry;
typedef struct qa_lmctf_context {
  bool plasma_quad;
  qa_lmctf_hooks hooks;
  qa_lmctf_rules *rules; /* borrowed (TS LmctfContext.rules) */
  qa_lmctf_state_entry *states; size_t state_count, state_cap;
  void *rule_ctx;
  bool (*can_score)(void *ctx);
  bool (*flags_touchable)(void *ctx);
} qa_lmctf_context;
qa_lmctf_player_state *qa_lmctf_context_get(qa_lmctf_context *context, qa_actor_id actor);
qa_lmctf_player_state *qa_lmctf_context_set(qa_lmctf_context *context, qa_actor_id actor, qa_lmctf_player_state state); /* takes statistics */
void qa_lmctf_context_delete(qa_lmctf_context *context, qa_actor_id actor);
void qa_lmctf_context_clear(qa_lmctf_context *context);
bool qa_lmctf_context_can_score(qa_lmctf_context *context);
bool qa_lmctf_context_flags_touchable(qa_lmctf_context *context);

qa_lmctf_player_state *qa_lmctf_player(qa_lmctf_context *context, qa_actor_id actor);
const char *qa_lmctf_name(qa_lmctf_context *context, qa_actor_id actor);
void qa_lmctf_print(qa_q2_game_services *game, const qa_actor_id *actor, const char *format, ...);
void qa_lmctf_stat(qa_lmctf_context *context, qa_actor_id actor, const char *name, double amount);
/* victim NULL = TS null default. */
void qa_lmctf_score(qa_lmctf_context *context, qa_q2_game_services *game, qa_actor_id actor, double amount, const char *name, const qa_actor_id *victim);
bool qa_lmctf_active(qa_lmctf_context *context, qa_q2_game_services *game, qa_actor_id actor);
void qa_lmctf_toss(qa_q2_entity *entity, qa_q2_entity *player, qa_q2_game_services *game, qa_vec3 forward);

typedef struct qa_lmctf_map_change { char *map; bool countdown; } qa_lmctf_map_change;
typedef struct qa_lmctf_travel_player {
  double slot; qa_lmctf_team team, observer_team; double extra_flags;
} qa_lmctf_travel_player;
typedef struct qa_lmctf_travel {
  qa_lmctf_rules rules;
  bool countdown, paused;
  qa_lmctf_travel_player *players; size_t player_count;
} qa_lmctf_travel;
void qa_lmctf_travel_destroy(qa_lmctf_travel *travel);
#endif
