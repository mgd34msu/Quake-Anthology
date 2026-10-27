/* Port of src/content/q3/base/game/save-state.ts. GPL-2.0-or-later.
 * Graph capture/restore over mirrors; actor registry mirrors unported. */
#ifndef QA_CONTENT_Q3_BASE_GAME_SAVE_STATE_H
#define QA_CONTENT_Q3_BASE_GAME_SAVE_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/save_values.h"
typedef struct qa_q3_saved_actor_ref { bool present; double slot, generation; } qa_q3_saved_actor_ref;
typedef struct qa_q3_entity_state {
  qa_q3_entity_values values; qa_q3_network_values network;
  qa_q3_game_trajectory_mirror pos, apos;
  double sv_flags, single_client, contents, owner_num, model_kind, model_index;
  bool has_client; double client; bool classname_is_client; char *classname; double classname_client;
  bool has_parent, has_next_train, has_prev_train, has_target_ent, has_chain, has_enemy, has_activator, has_teamchain, has_teammaster;
  double parent, next_train, prev_train, target_ent, chain, enemy, activator, teamchain, teammaster;
  bool has_item; double item; double nextthink;
  char *think, *reached, *blocked, *touch, *use, *pain, *die;
} qa_q3_entity_state;
typedef struct qa_q3_client_state {
  qa_q3_client_values values; qa_q3_player_values player; qa_q3_persistant_values persistant;
  qa_q3_game_user_command_mirror command; qa_q3_team_values team; qa_q3_session_values session;
  double *events, *event_parms, *persistant_slots, *powerups, *ammo_times;
  size_t event_count, event_parm_count, persistant_count, powerup_count, ammo_time_count;
  bool has_hook, has_persistant_powerup; double hook, persistant_powerup;
} qa_q3_client_state;
typedef struct qa_q3_graph_ownership { bool has_actor; qa_q3_saved_actor_ref actor; bool active, borrowed; } qa_q3_graph_ownership;
typedef struct qa_q3_graph_state {
  qa_q3_graph_ownership *ownership; size_t ownership_count;
  qa_q3_entity_state *entities; size_t entity_count;
  qa_q3_client_state *clients; size_t client_count;
  double num_entities, max_clients;
} qa_q3_graph_state;
void qa_q3_graph_state_destroy(qa_q3_graph_state *state);
/* Records mirror: pool supplies entities/clients; capture walks pool slots. */
qa_q3_graph_state qa_q3_capture_graph(qa_q3_entity_pool *pool);
void qa_q3_restore_graph(qa_q3_entity_pool *pool, qa_q3_graph_state *state);
#endif
