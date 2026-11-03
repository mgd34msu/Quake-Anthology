/*
 * Port of src/bots/behavior/q3/ai-context.ts (per-game ownership of game/ai_main.c
 * and ai_dmq3.c globals). The context borrows game, library and host; deathmatch,
 * team, chat and command states are embedded as TS does.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_CONTEXT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_CONTEXT_H
#include "qa/bots/behavior/library/bsp_entities.h"
#include "qa/bots/behavior/q3/ai_chat.h"
#include "qa/bots/behavior/q3/ai_command.h"
#include "qa/bots/behavior/q3/ai_navigation.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/bots/behavior/q3/ai_team.h"
#include "qa/bots/behavior/q3/game_host.h"
#include "qa/bots/behavior/q3/library.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/behavior/q3/observations.h"
#include "qa/core/cvars/index.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TS GameAiHost: source server traps bound to real slots, ring and current map.
 * get_console_message NULL result is TS null; text borrowed until its next call. */
typedef struct qa_game_ai_host_bots_a {
  void *ctx;
  qa_bot_navigation_bots_a *navigation;
  qa_aas_bsp_entities *bsp_entities;
  int64_t (*point_contents)(void *ctx, qa_vec3 point);
  int64_t (*get_snapshot_entity)(void *ctx, int64_t client, int64_t sequence);
  const char *(*get_console_message)(void *ctx, int64_t client);
  void (*user_command)(void *ctx, int64_t client, const qa_q3_base_user_command *command);
  void (*insert_console_command)(void *ctx, const char *text);
  void (*check_bot_spawn)(void *ctx);
  int64_t (*load_map)(void *ctx, const char *name);
} qa_game_ai_host_bots_a;

/* TS BotSnapshotEntity. */
typedef struct qa_bot_snapshot_entity_bots_a {
  int64_t next_sequence;
  qa_q3_base_entity_state state;
} qa_bot_snapshot_entity_bots_a;

/* TS BotCvar: cached VM cell over a borrowed registry. */
typedef struct qa_bot_cvar_bots_a {
  qa_cvar_registry *registry;
  bool has_name;
  char name[256];
  char value[256];
  double numeric_value;
  int64_t integer_value;
  double modification_count;
} qa_bot_cvar_bots_a;
void qa_bot_cvar_bots_a_init(qa_bot_cvar_bots_a *cell, qa_cvar_registry *registry);
void qa_bot_cvar_register_bots_a(qa_bot_cvar_bots_a *cell, const char *name, const char *def,
                                 int32_t flags);
void qa_bot_cvar_update_bots_a(qa_bot_cvar_bots_a *cell);
void qa_bot_cvar_write_integer(qa_bot_cvar_bots_a *cell, double value);
/* Owned checkpoint tree. */
qa_save_value *qa_bot_cvar_capture_save_state(const qa_bot_cvar_bots_a *cell);
void qa_bot_cvar_restore_save_state(qa_bot_cvar_bots_a *cell, const qa_save_value *value);

typedef struct qa_game_ai_vm_cvar_bots_a {
  char *name;
  qa_bot_cvar_bots_a *cell; /* stable: VM cells are held by reference */
} qa_game_ai_vm_cvar_bots_a;

/* TS GameAiContext. Construction allocates source cells only. The struct must
 * not move (waypoint and observation identities). */
struct qa_game_ai_context_bots_a {
  qa_bot_entity_observations observations;
  qa_bot_state_store_bots_a states;
  qa_game_ai_deathmatch_state_bots_a deathmatch;
  qa_game_ai_team_state_bots_a team;
  qa_game_ai_chat_state_bots_a chat;
  qa_game_ai_command_state_bots_a command;
  char **node_switches; /* owned; TS nodeSwitches: string[], unbounded */
  size_t node_switch_count, node_switch_capacity;
  qa_game_ai_vm_cvar_bots_a *vm_cvars; /* TS Map insertion order */
  size_t vm_cvar_count, vm_cvar_capacity;
  char **restored_names; /* owned names of restored maxclients cells */
  size_t restored_name_count;
  double time, regular_update_time, num_bots, interbreed_match_count;
  bool interbreed;
  qa_source_bot_game_bots_a *game;
  qa_bot_library_bots_a *library;
  const qa_game_ai_host_bots_a *host;
};
/* game, library and host are borrowed. False only on OOM. */
bool qa_game_ai_context_bots_a_init(qa_game_ai_context_bots_a *c, qa_source_bot_game_bots_a *game,
                                    qa_bot_library_bots_a *library,
                                    const qa_game_ai_host_bots_a *host);
void qa_game_ai_context_bots_a_destroy(qa_game_ai_context_bots_a *c);
/* TS nodeSwitches.push(line): appends an owned line (the list grows without bound). */
void qa_bot_push_node_switch_bots_a(qa_game_ai_context_bots_a *c, char *line);
int64_t qa_game_ai_context_game_type(const qa_game_ai_context_bots_a *c);
int64_t qa_game_ai_context_max_clients(const qa_game_ai_context_bots_a *c);
qa_bot_navigation_bots_a *qa_game_ai_context_navigation(const qa_game_ai_context_bots_a *c);
double qa_game_ai_context_random(qa_game_ai_context_bots_a *c);
/* TS cvar(name): created unregistered on first use; the cell stays valid. */
qa_bot_cvar_bots_a *qa_game_ai_context_cvar(qa_game_ai_context_bots_a *c, const char *name);
void qa_game_ai_context_register_cvar(qa_game_ai_context_bots_a *c, const char *name,
                                      const char *def, int32_t flags);
/* False is TS null. On true *out is an owned copy: destroy it with
 * qa_q3_base_player_state_record_destroy. */
bool qa_game_ai_context_get_client_state(qa_game_ai_context_bots_a *c, int64_t client,
                                         qa_q3_base_player_state *out);
/* False is TS null. */
bool qa_game_ai_context_get_entity_state(qa_game_ai_context_bots_a *c, int64_t entity_num,
                                         qa_q3_base_entity_state *out);
qa_bot_snapshot_entity_bots_a qa_game_ai_context_get_snapshot_entity(qa_game_ai_context_bots_a *c,
                                                                     int64_t client,
                                                                     int64_t sequence);
void qa_game_ai_context_reset_state(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *state);
void qa_bot_reset_state_bots_a(qa_game_ai_context_bots_a *c, qa_bot_state_bots_a *state);
/* Owned checkpoint tree. */
qa_save_value *qa_game_ai_context_capture_save_state(qa_game_ai_context_bots_a *c);
void qa_game_ai_context_restore_save_state(qa_game_ai_context_bots_a *c, const qa_save_value *value,
                                           qa_bot_remap_observation_fn remap, void *remap_ctx);
#endif
