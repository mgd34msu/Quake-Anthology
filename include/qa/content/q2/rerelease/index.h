/*
 * Port of src/content/q2/rerelease/index.ts (module class; the
 * re-export barrel is include/qa/content/q2/rerelease.h).
 * GPL-2.0-or-later. pickedUpBy slots are owned double arrays.
 * Checkpoint capture arrays are module scratch (valid until the
 * next capture/destroy).
 */
#ifndef QA_CONTENT_Q2_RERELEASE_INDEX_H
#define QA_CONTENT_Q2_RERELEASE_INDEX_H
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/rerelease/checkpoint.h"
#include "qa/content/q2/rerelease/entities.h"
#include "qa/content/q2/rerelease/goals.h"
#include "qa/content/q2/rerelease/lights.h"
#include "qa/content/q2/rerelease/q64/index.h"
#include "qa/content/q2/rerelease/triggers.h"
#include "qa/debug/shapes.h"

typedef struct qa_q2rr_pickup_entry { qa_actor_id actor; double *slots; size_t count, cap; } qa_q2rr_pickup_entry;
typedef struct qa_q2rr_msg_entry { qa_actor_id actor; char *message; } qa_q2rr_msg_entry;
typedef struct qa_q2_rerelease_module {
  qa_q2_rerelease_entities base; /* TS extends Q2RereleaseEntities */
  qa_q2rr_pickup_entry *picked_up_by; size_t picked_count, picked_cap;
  qa_q2rr_msg_entry *pickup_messages; size_t msg_count, msg_cap;
  qa_q2_rerelease_triggers triggers;
  qa_q2_rerelease_q64 q64;
  qa_q2_rerelease_lights lights;
  qa_q2_rerelease_goals goals;
  bool release_bound;
  qa_session_actor_registry *release_registry; int64_t release_subscription; /* on_release, undone by destroy */
  /* capture scratch */
  qa_q2rr_saved_healthbar *cp_bars; qa_q2rr_saved_pair *cp_ht;
  qa_q2rr_saved_slots *cp_picked; double **cp_slot_arrays;
  qa_q2rr_saved_time *cp_sounds; qa_q2rr_saved_light *cp_lights;
  qa_q2_rerelease_level_entry *cp_levels; const char **cp_visited;
  /* Storage behind qa_q2_rerelease_module_callbacks (per module). */
  qa_q2_named_think cb_thinks[64]; qa_q2_named_touch cb_touches[64]; qa_q2_named_use cb_uses[64];
} qa_q2_rerelease_module;
typedef struct qa_q2_rerelease_module_options {
  qa_q2_rerelease_players *players;
  qa_q2_rerelease_hooks hooks;
  qa_q2_rerelease_campaign_state *campaign; /* optional */
} qa_q2_rerelease_module_options;

bool qa_q2_create_rerelease_module(qa_q2_rerelease_module *m, qa_q2_rerelease_module_options o);
void qa_q2_rerelease_module_destroy(qa_q2_rerelease_module *m);
void qa_q2_rerelease_module_draw_debug_shape(qa_q2_rerelease_module *m, qa_debug_shape shape, qa_vec4 color,
                                             double lifetime_seconds, bool depth_test,
                                             qa_debug_line *lines, size_t capacity);
bool qa_q2_rerelease_module_use_powerup(qa_q2_rerelease_module *m, qa_owned_actor player, qa_item_id item, qa_q2_game_services *g);
bool qa_q2_rerelease_module_spawn(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g);
qa_q2_callback_definitions qa_q2_rerelease_module_callbacks(qa_q2_rerelease_module *m);
qa_q2_rerelease_module_checkpoint qa_q2_rerelease_module_capture(qa_q2_rerelease_module *m);
void qa_q2_rerelease_module_restore(qa_q2_rerelease_module *m, qa_q2_game_services *g, qa_q2_rerelease_module_checkpoint cp);
void qa_q2_rerelease_module_restore_campaign(qa_q2_rerelease_module *m, const qa_q2_rerelease_campaign_state *c);
void qa_q2_rerelease_module_admitted(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_module_after_player_frames(qa_q2_rerelease_module *m, qa_q2_game_services *g);
void qa_q2_rerelease_module_before_level_change(qa_q2_rerelease_module *m, qa_q2_game_services *g);
void qa_q2_rerelease_module_leave_unit(qa_q2_rerelease_module *m);
void qa_q2_rerelease_module_spawned(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_rerelease_module_instanced_coop(qa_q2_rerelease_module *m, qa_q2_game_services *g);
bool qa_q2_rerelease_module_can_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
bool qa_q2_rerelease_module_before_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
void qa_q2_rerelease_module_after_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player, bool taken);
void qa_q2_rerelease_module_before_targets(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player, bool taken);
bool qa_q2_rerelease_module_keep_after_pickup(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g, qa_actor_id player);
qa_q2_pickup_policy qa_q2_rerelease_module_as_pickup_policy(qa_q2_rerelease_module *m);
qa_q2_spawn_module qa_q2_rerelease_module_as_spawn(qa_q2_rerelease_module *m);
void qa_q2_rerelease_module_resume_presentation(qa_q2_rerelease_module *m, qa_q2_game_services *g, const qa_actor_id *only);
void qa_q2_rerelease_module_send_poi(qa_q2_rerelease_module *m, qa_actor_id a, qa_q2_game_services *g);
void qa_q2_rerelease_module_use_compass(qa_q2_rerelease_module *m, qa_actor_id a, qa_q2_game_services *g);
void qa_q2_rerelease_module_compass_update(qa_q2_rerelease_module *m, qa_actor_id a, qa_q2_game_services *g, bool first);
void qa_q2_rerelease_module_begin_player_frame(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_module_help(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_module_end_player_frame(qa_q2_rerelease_module *m, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_module_end_of_unit(qa_q2_rerelease_module *m, qa_q2_game_services *g);
#endif
