/* Port of src/app/bootstrap/simulation/source-strategy.ts (see
 * source_strategy.h): LoadingStrategy, LoadedStrategy and the six source
 * strategies, and createSourceStrategy. Each `xxx_strategy()` constructor
 * starts from its parent class's table, as the TS subclasses inherit, and
 * overrides the members the TS class overrides.
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/content/q2/foundation/shadow_lights.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/settings/server/q2_owner.h"
#include "qa/foundation/support.h"
#include "qa/world/actors/body.h"

static double st_time_seconds(const qa_app_shared_simulation *sim) { return qa_ss_seconds(sim->source_frame.time); }
static bool st_live(qa_app_shared_simulation *sim, qa_actor_id actor) { return qa_session_actor_registry_is_live(sim->actors, actor); }
static double st_health(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  return combat.has_value ? combat.value.health : 0;
}
static qa_app_sim_movement_player *st_player_at(qa_app_shared_simulation *sim, size_t index) {
  return *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, index);
}
static double st_buttons(const qa_user_command *command) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: return command->as.q1.buttons;
    case QA_USER_COMMAND_QW: return command->as.qw.buttons;
    case QA_USER_COMMAND_Q2: return command->as.q2.buttons;
    case QA_USER_COMMAND_Q2_RERELEASE: return command->as.q2_rerelease.buttons;
    default: return command->as.q3.buttons;
  }
}
/* command.upMove > 0 for the commands that carry upMove. */
static bool st_up_positive(const qa_user_command *command) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: return command->as.q1.up_move > 0;
    case QA_USER_COMMAND_QW: return command->as.qw.up_move > 0;
    case QA_USER_COMMAND_Q2: return command->as.q2.up_move > 0;
    case QA_USER_COMMAND_Q3: return command->as.q3.up_move > 0;
    default: return false;
  }
}
/* "impulse" in command ? command.impulse : <absent>. */
static bool st_impulse(const qa_user_command *command, double *out) {
  switch (command->kind) {
    case QA_USER_COMMAND_Q1: *out = command->as.q1.impulse; return true;
    case QA_USER_COMMAND_QW: *out = command->as.qw.impulse; return true;
    case QA_USER_COMMAND_Q2: *out = command->as.q2.impulse; return true;
    default: return false;
  }
}

/* Scratch of the userinfo members (TS returns a fresh string): valid until the next call. */
static __thread char *st_userinfo_text;
static const char *st_keep_text(char *text) { free(st_userinfo_text); st_userinfo_text = text; return text; }
static char *st_dup(const char *text) {
  char *copy = strdup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static qa_q2_base_player_state *st_q2_state(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_q2_players_get_state(sim->source.product->players, actor);
}
static qa_q2_entity *st_q2_entity(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_game_services *game = sim->source.product->services;
  return game->entity(game->ctx, &actor);
}

/* Scratch arrays of the list members (TS returns fresh arrays): valid until the next call of the same member. */
static void *st_grow(void *storage, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return storage;
  void *grown = realloc(storage, need * size);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = need;
  return grown;
}
/* ---------------- saves and restore (shared helpers) ---------------- */
static qa_module_identity st_native_module(qa_app_shared_simulation *sim) {
  return sim->source.classic_world ? qa_app_sim_classic_guest_world_module(sim->source.classic_world)
    : qa_app_sim_rerelease_guest_world_module(sim->source.rerelease_world);
}
static void loading_require_loaded(qa_app_shared_simulation *sim, const char *message) { (void)sim; qa_raise("Error", 0, 0, "%s", message); }
static void loaded_require_loaded(qa_app_shared_simulation *sim, const char *message) { (void)sim; (void)message; }
static void loading_checkpoint_providers(qa_app_shared_simulation *sim, const qa_app_source_checkpoint_sink *sink) { (void)sim; (void)sink; }
static const qa_guest_checkpoint *loading_checkpoint_guests(qa_app_shared_simulation *sim, size_t *out_count) {
  (void)sim; *out_count = 0; return NULL;
}
static void loading_checkpoint_extras(qa_app_shared_simulation *sim, qa_save_value *record) { (void)sim; (void)record; }
static bool loading_native_clients(qa_app_shared_simulation *sim, qa_app_source_native_client **out, size_t *out_count) {
  (void)sim; *out = NULL; *out_count = 0; return false;
}
static qa_cvar_snapshot *loading_source_cvar_snapshots(qa_app_shared_simulation *sim, size_t *out_count) { (void)sim; *out_count = 0; return NULL; }
static qa_qvm_combat_bindings *loading_legacy_combat(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static void loading_require_inventory_projection(qa_app_shared_simulation *sim, const char *kind) {
  (void)sim;
  qa_raise("Error", 0, 0, "%s", strcmp(kind, "qvm") == 0 ? "Saved QVM inventory requires qualified source storage"
      : "Saved native inventory requires qualified source storage");
}
static bool loading_inventory_coverage(qa_app_shared_simulation *sim, bool native, bool qvm, qa_app_source_inventory_coverage *out) {
  (void)sim; (void)native; (void)qvm; (void)out; return false;
}
static bool loading_capture_guest_save(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx,
    qa_app_sim_rerelease_guest_save *out) {
  (void)sim; (void)next_frame; (void)frame_ctx; (void)out; return false;
}
static bool loading_original_save(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *guest_save, qa_persist_decoded *owner,
    qa_provider_checkpoint *out) {
  (void)sim; (void)guest_save; (void)owner; (void)out; return false;
}
static void loading_restore_loading(qa_app_shared_simulation *sim, const qa_save_image *save, qa_app_shared_sim_next_frame_fn next_frame,
    void *frame_ctx) {
  (void)sim; (void)save; (void)next_frame; (void)frame_ctx;
  qa_raise("Error", 0, 0, "Native original loading requires its source provider");
}
static void loading_restore_guest(qa_app_shared_simulation *sim, const qa_save_image *save, bool native_restored) {
  (void)sim; (void)save; (void)native_restored; /* only a native Q2 source restores its own original save */
}
static bool loading_monster_proxy(qa_app_shared_simulation *sim, qa_app_source_monster_proxy *out) { (void)sim; (void)out; return false; }
static void loading_save(qa_app_shared_simulation *sim, const qa_save_image *save) { (void)sim; (void)save; }
static void loading_restore_foundation(qa_app_shared_simulation *sim, const qa_save_image *save, const qa_save_reader *reader) {
  (void)sim; (void)save; (void)reader; /* the source has no foundation checkpoint */
}
static bool loading_ammo_timer(qa_app_shared_simulation *sim, qa_actor_id actor, double weapon, double *out) {
  (void)sim; (void)actor; (void)weapon; (void)out; return false;
}
/* ---------------- LoadingStrategy ---------------- */
static qa_ss_source_runtime *loading_as(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static qa_ss_source_runtime *own_source(qa_app_shared_simulation *sim) { return &sim->source; }
static qa_q2_lmctf *loading_lmctf(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static bool loading_false(qa_app_shared_simulation *sim) { (void)sim; return false; }
static bool loading_true(qa_app_shared_simulation *sim) { (void)sim; return true; }
static void loading_nothing(qa_app_shared_simulation *sim) { (void)sim; }
static void loading_begin_timed_frame(qa_app_shared_simulation *sim, double elapsed) { (void)sim; (void)elapsed; }
static void loading_frame(qa_app_shared_simulation *sim, qa_frame_context frame) { (void)sim; (void)frame; }
static void loading_request_intermission_exit(qa_app_shared_simulation *sim, qa_actor_id actor, double buttons) {
  (void)sim; (void)actor; (void)buttons;
}
static void loading_pre_frame(qa_app_shared_simulation *sim, const qa_app_source_pre_frame_input *input) { (void)sim; (void)input; }
static bool loading_pre_think(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_actor_command *command) {
  (void)sim; (void)player; (void)command; return true;
}
static bool loading_player_think(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_actor_command *command) {
  (void)sim; (void)player; (void)command; return false;
}
static double loading_gravity_scale(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return 1; }
static void loading_movement_applied(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_movement_result *moved) {
  (void)sim; (void)actor; (void)moved;
}
static void loading_after_player_command(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, bool paused) {
  (void)sim; (void)player; (void)paused;
}
static void loading_owned(qa_app_shared_simulation *sim, qa_owned_actor actor) { (void)sim; (void)actor; }
static bool loading_spectating(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return false; }
static bool loading_run_player_actor(qa_app_shared_simulation *sim, qa_owned_actor actor) { (void)sim; (void)actor; return false; }
static qa_app_source_step_route loading_step_route(qa_app_shared_simulation *sim) { (void)sim; return QA_APP_SOURCE_STEP_SHARED; }
static bool loading_settles(qa_app_shared_simulation *sim, const void *game) { (void)sim; (void)game; return false; }
static void loading_finish_frame(qa_app_shared_simulation *sim, bool run) { (void)sim; (void)run; }
static double loading_q1_powerup(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  (void)sim; (void)actor; (void)powerup; return 0;
}
static void loading_q1_character_frame(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_character_presentation *frame) {
  (void)sim; (void)actor; (void)frame;
}
static bool loading_admit_client(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel,
    const char *userinfo, qa_app_sim_player_admission *out) {
  (void)sim; (void)client; (void)travel; (void)userinfo; (void)out;
  qa_raise("Error", 0, 0, "Map spawn is incomplete");
}
static qa_owned_actor loading_prepare_bot(qa_app_shared_simulation *sim, qa_client_id client) {
  (void)sim; (void)client;
  qa_raise("Error", 0, 0, "Bot admission requires a supported source observation binding");
}
static qa_weapon_state loading_initial_arsenal_state(qa_app_shared_simulation *sim) {
  (void)sim;
  qa_raise("Error", 0, 0, "Player spawn requires a Q1 or Q2 source");
}
static qa_q2_entity *loading_attach_entity(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player) {
  (void)sim; (void)actor; (void)player; return NULL;
}
static void loading_configure_admitted(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client, qa_app_sim_movement_player *player,
    qa_q2_entity *entity, const char *userinfo, const qa_app_simulation_travel *travel) {
  /* only Q1 and Q2 sources configure a shared admission */
  (void)sim; (void)actor; (void)client; (void)player; (void)entity; (void)userinfo; (void)travel;
}
static void loading_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) { (void)sim; (void)player; }
static bool loading_admit_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q2_entity *entity, const qa_app_sim_travel_player *carried) {
  (void)sim; (void)actor; (void)entity; (void)carried; return false;
}
static void loading_enter_world(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player, bool fresh) {
  /* only Q1 and Q2 sources enter the world through the shared admission */
  (void)sim; (void)actor; (void)player; (void)fresh;
}
static bool loading_capture_entry_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_travel_state *out) {
  (void)sim; (void)actor; (void)out; return false;
}
static bool loading_actor_false(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return false; }
static bool loading_q3_client(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q3_game_client **out) {
  (void)sim; (void)actor; *out = NULL; return false;
}
static const char *loading_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; st_keep_text(NULL); return NULL; }
static void loading_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)sim; (void)actor; (void)client; (void)userinfo;
  qa_raise("Error", 0, 0, "Component userinfo requires an initialized source");
}
static void loading_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  (void)sim; (void)actor; (void)userinfo; (void)previous; /* no source yet: nothing stores userinfo */
}
static bool loading_pause(qa_app_shared_simulation *sim, qa_cvar_registry **cvars) { (void)sim; *cvars = NULL; return false; }
static bool loading_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  (void)sim; (void)actor; (void)name; (void)args; (void)arg_count; return false;
}
static void loading_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name,
    const char *const *args, size_t arg_count) {
  /* the source has no commands of its own after the shared ones */
  (void)sim; (void)actor; (void)player; (void)name; (void)args; (void)arg_count;
}
static qa_app_sim_player_ui loading_guest_ui(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor;
  qa_raise("Error", 0, 0, "Actor has no native source UI");
}
/* ---- monsters, policy and presentation members with per-source answers ---- */
static const char *loading_null_text(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static double loading_stop_speed(qa_app_shared_simulation *sim) { (void)sim; return 100; }
static bool loading_team_game(qa_app_shared_simulation *sim) {
  (void)sim;
  qa_raise("Error", 0, 0, "Team match policy requires an initialized source");
}
static const char *q1_legacy_q3_family(qa_app_shared_simulation *sim) { (void)sim; return "q1"; }
static const char *q2_legacy_q3_family(qa_app_shared_simulation *sim) { (void)sim; return "q2"; }
static bool q1_team_game(qa_app_shared_simulation *sim) {
  return sim->source.q1_composition->selection.program == QA_COMP_Q1_CTF || qa_cvar_registry_variable_value(sim->source.q1_cvars, "teamplay") != 0;
}
static bool quakec_team_game(qa_app_shared_simulation *sim) {
  return qa_cvar_registry_variable_value(qa_app_sim_quakec_source_cvars(sim->source.quakec), "teamplay") != 0;
}
static double q2_stop_speed(qa_app_shared_simulation *sim) {
  return sim->source.product->has_movement_stop_speed ? sim->source.product->movement_stop_speed : 100;
}
static bool q2_team_game(qa_app_shared_simulation *sim) {
  const qa_q2_product_match *selected = &sim->source.product->match;
  if (selected->source_kind == QA_Q2_MATCH_LMCTF) return (qa_js_to_int32(qa_q2_lmctf_get_rules(selected->lmctf)->ctf_flags) & 128) == 0;
  qa_q2_match_kind match = selected->selection.kind;
  const qa_q2_game_options *options = &sim->source.product->services->options;
  return match == QA_Q2_MATCH_CTF || match == QA_Q2_MATCH_LMCTF || match == QA_Q2_MATCH_DEATHBALL
    || (options->mode == QA_Q2_MODE_DEATHMATCH && (qa_js_to_int32(options->deathmatch_flags) & (64 | 128)) != 0);
}
static bool q2_native_team_game(qa_app_shared_simulation *sim) {
  return sim->q2_server_registry != NULL && (qa_cvar_registry_variable_value(sim->q2_server_registry, "ctf") != 0
    || (qa_js_to_int32(qa_cvar_registry_variable_value(sim->q2_server_registry, "dmflags")) & (64 | 128)) != 0);
}
static const char *q2_native_guest_edition(qa_app_shared_simulation *sim) { return sim->source.edition; }
static bool q3_team_game(qa_app_shared_simulation *sim) { return qa_app_q3_source_runtime_game_type(sim->source.q3) >= 3; }
static bool q3_curves_enabled(qa_app_shared_simulation *sim) {
  return qa_cvar_registry_variable_value(qa_app_q3_source_runtime_host(sim->source.q3)->cvars, "cm_noCurves") == 0;
}
static bool q3_qvm_team_game(qa_app_shared_simulation *sim) {
  return qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(sim->source.qvm)), "g_gametype") >= 3;
}

static double loading_knockback(qa_app_shared_simulation *sim) { (void)sim; return 1000; }
static double loading_zero(qa_app_shared_simulation *sim) { (void)sim; return 0; }
static double loading_one(qa_app_shared_simulation *sim) { (void)sim; return 1; }
static double loading_quad_factor(qa_app_shared_simulation *sim) { (void)sim; return 4; }
static double loading_proximity_timeout(qa_app_shared_simulation *sim) { (void)sim; return 20000; }
static double q1_intermission_queued(qa_app_shared_simulation *sim) { return sim->source.q1_game->has_intermission ? 1 : 0; }
static bool q2_friendly_fire(qa_app_shared_simulation *sim) {
  return (qa_js_to_int32(qa_q2_options_deathmatch_flags(&sim->source.product->services->options)) & 256) == 0;
}
static double q2_intermission_queued(qa_app_shared_simulation *sim) {
  return !qa_ss_is(qa_q2_players_intermission(sim->source.product->players).kind, "playing") ? 1 : 0;
}
static qa_cvar_registry *q3_host_cvars(qa_app_shared_simulation *sim) { return qa_app_q3_source_runtime_host(sim->source.q3)->cvars; }
static bool q3_player_curve_clip(qa_app_shared_simulation *sim) { return qa_cvar_registry_variable_value(q3_host_cvars(sim), "cm_playerCurveClip") != 0; }
static bool q3_friendly_fire(qa_app_shared_simulation *sim) { return qa_cvar_registry_variable_value(q3_host_cvars(sim), "g_friendlyFire") != 0; }
static double q3_knockback(qa_app_shared_simulation *sim) { return qa_cvar_registry_variable_value(q3_host_cvars(sim), "g_knockback"); }
static double q3_intermission_queued(qa_app_shared_simulation *sim) { return qa_app_q3_source_runtime_intermission_queued(sim->source.q3); }
static double q3_quad_factor(qa_app_shared_simulation *sim) { return qa_app_q3_source_runtime_quad_damage_factor(sim->source.q3); }
static double q3_proximity_timeout(qa_app_shared_simulation *sim) { return qa_cvar_registry_variable_value(q3_host_cvars(sim), "g_proxMineTimeout"); }

static bool loading_notarget(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) { (void)sim; (void)actor; (void)out; return false; }
static bool loading_cloak_alpha(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) { (void)sim; (void)actor; (void)out; return false; }
static bool loading_weapon_event(qa_app_shared_simulation *sim, const qa_q2w_event *event) { (void)sim; (void)event; return false; }
static bool loading_team_damage_enabled(qa_app_shared_simulation *sim, const char *match_provider) { (void)sim; (void)match_provider; return false; }
static qa_q2_damage_source_effects loading_match_damage_effects(qa_app_shared_simulation *sim, qa_q2_product_match_effects_binding *storage) {
  (void)sim; (void)storage;
  qa_raise("Error", 0, 0, "Q2 match damage requires its source runtime");
}
static void loading_required_monster_noise(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, bool secondary, const char *message) {
  (void)sim; (void)actor; (void)origin; (void)secondary;
  qa_raise("Error", 0, 0, "%s", message);
}
static void loading_require_q2(qa_app_shared_simulation *sim, const char *message) { (void)sim; qa_raise("Error", 0, 0, "%s", message); }
static void loading_pushed(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 velocity) {
  (void)sim; (void)actor; (void)velocity; /* only a Q2 source keeps the previous velocity */
}
static void loading_set_entity_gravity(qa_app_shared_simulation *sim, qa_actor_id actor, double gravity) {
  (void)sim; (void)actor; (void)gravity; /* only a Q2 source keeps entity gravity */
}
static bool loading_monsters_searching(qa_app_shared_simulation *sim, const qa_actor_id *player) { (void)sim; (void)player; return false; }
static qa_q1_services *loading_q1_game(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static bool loading_select_weapon(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_item_id item) { (void)sim; (void)actor; (void)item; return false; }
static const qa_client_id *loading_connected_clients(qa_app_shared_simulation *sim, size_t *out_count) { (void)sim; *out_count = 0; return NULL; }
static const qa_actor_id *loading_player_actors(qa_app_shared_simulation *sim, size_t *out_count) { (void)sim; *out_count = 0; return NULL; }
static bool loading_q3_game_type(qa_app_shared_simulation *sim, double *out) { (void)sim; (void)out; return false; }
static char *loading_q3_server_info(qa_app_shared_simulation *sim) { (void)sim; return st_dup(""); }
static double loading_equipment_damage_factor(qa_app_shared_simulation *sim, qa_actor_id actor) {
  double factor;
  if (!sim->strategy->weapon_damage_factor(sim, actor, true, &factor)) qa_raise("Error", 0, 0, "Primary Q3 equipment lost its source");
  return factor;
}
static double loading_equipment_firing_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent) {
  (void)sim; (void)actor; (void)milliseconds; (void)persistent;
  qa_raise("Error", 0, 0, "Primary Q3 equipment lost its source");
}
static void loading_check_hurt_carrier(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker) {
  (void)sim; (void)target; (void)attacker; /* only a Q3 source tracks carrier defense */
}
static bool loading_check_obelisk_attack(qa_app_shared_simulation *sim, qa_actor_id target, qa_actor_id attacker) {
  (void)sim; (void)target; (void)attacker; return false;
}
static void loading_return_pickup(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor;
  qa_raise("Error", 0, 0, "Borrowed persistent pickup has no original Q3 map owner");
}
static void loading_install_pickup_admission(qa_app_shared_simulation *sim, const qa_pickup_admission *admission) {
  (void)sim; (void)admission; /* the source admits its pickups itself */
}
static const char *loading_supply_family(qa_app_shared_simulation *sim) { (void)sim; return "q3"; }
static const qa_item_id *loading_extra_replaced_items(qa_app_shared_simulation *sim, size_t *out_count) { (void)sim; *out_count = 0; return NULL; }
static void loading_note_fired(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  (void)sim; (void)actor; (void)item; /* only a Q1 source records weapon use */
}
static void loading_weapon_noise(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, const char *kind) {
  (void)sim; (void)actor; (void)origin; (void)kind; /* only a Q2 source wakes monsters by weapon noise */
}
static bool loading_actor_true(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return true; }
static qa_cvar_registry *loading_cvars(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static double loading_q1_powerup_expires(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  (void)sim; (void)actor; (void)powerup; return 0;
}
static bool loading_damage_modifier(qa_app_shared_simulation *sim, qa_source_damage_modifier *out) { (void)sim; (void)out; return false; }
static void loading_monster_noise(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  (void)sim; (void)actor; (void)origin; (void)secondary; /* only a Q2 source has noise-driven monsters */
}
static bool loading_original_player(qa_app_shared_simulation *sim, qa_app_source_original_player *out) { (void)sim; (void)out; return false; }
static void loading_set_max_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  (void)sim; (void)actor; (void)health; /* the source has no max health of its own */
}
static qa_app_source_equipment_suppression loading_equipment_suppression(qa_app_shared_simulation *sim) {
  (void)sim; return (qa_app_source_equipment_suppression){false, false, false}; /* NO_SUPPRESSION */
}
static qa_app_source_equipment_suppression native_equipment_suppression(qa_app_shared_simulation *sim) {
  (void)sim; return (qa_app_source_equipment_suppression){true, true, true}; /* NATIVE_SUPPRESSION */
}
static bool loading_source_inventory(qa_app_shared_simulation *sim, qa_actor_id actor, qa_inventory_state_binding *out) {
  (void)sim; (void)actor; (void)out; return false;
}
static bool loading_player_available(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) { (void)sim; (void)actor; (void)out; return false; }
static bool loading_teleport_bits(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) { (void)sim; (void)actor; (void)out; return false; }
static qa_app_source_equipment_powerups loading_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor; return (qa_app_source_equipment_powerups){false, 0, 0};
}
static qa_q2w_input loading_refine_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2w_input input) {
  (void)sim; (void)actor; return input;
}
static bool loading_guest_weapon_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee) {
  (void)sim; (void)actor; (void)priority; (void)melee; return false;
}
static void loading_write_character_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_actor_animation_state *animation) {
  (void)sim; (void)actor; (void)animation; /* only a Q3 source mirrors the character animation into its entity */
}
static bool loading_character_pose(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_character_source_pose *out) {
  (void)sim; (void)actor; (void)out; return false;
}
static qa_app_source_weapon_facts loading_weapon_facts(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor; return (qa_app_source_weapon_facts){NULL, false, "right", 0, false};
}
static qa_arsenal_state loading_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  (void)sim; (void)player;
  qa_raise("Error", 0, 0, "Arsenal accessed before source admission");
}
static qa_comp_q1_source_client *loading_q1_client(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return NULL; }
static void loading_q1_impulse(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; /* only a Q1 source consumes impulses */ }
static bool loading_step_selected_holdable(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input) {
  (void)sim; (void)player; (void)input; return false;
}
static bool loading_primary_weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input,
    qa_weapon_step_result *out) {
  (void)sim; (void)player; (void)input; (void)out; return false;
}
static bool loading_owns_contacts(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return false; }
static bool loading_jump_voice(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player) {
  (void)sim; (void)actor; (void)player; return false;
}
static bool loading_animation(qa_app_shared_simulation *sim, const qa_animation_step_input *input, qa_animation_step_result *out) {
  (void)sim; (void)input; (void)out; return false;
}
static bool loading_attack_arsenal(qa_app_shared_simulation *sim, qa_actor_id actor, qa_arsenal_state *out) { (void)sim; (void)actor; (void)out; return false; }
static void loading_movement_bindings(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_clock_profile_kind selected_clock, void *player_host,
    qa_app_sim_player_movement_host *host) {
  (void)sim; (void)actor; (void)selected_clock; (void)player_host; (void)host; /* TS {}: no source bindings */
}
static qa_cvar_registry *loading_server_cvars(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static qa_cvar_registry *host_q2_server_cvars(qa_app_shared_simulation *sim) { return sim->q2_server_registry; }
static qa_server_settings_owner loading_server_settings_owner(qa_app_shared_simulation *sim, qa_cvar_server_owner *owner) {
  (void)sim; return owner->owner;
}
static int loading_client_of(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id *out) { (void)sim; (void)actor; (void)out; return -1; }
static bool loading_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)sim; (void)actor; (void)player; return true;
}
static void loading_respawn(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  (void)sim; (void)player;
  qa_raise("Error", 0, 0, "Respawn before source entry");
}
static void loading_cutscene_control(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_vec3 angles) {
  (void)sim; (void)player; (void)angles; /* only a Q1 source clears the held input */
}
static void loading_grant_quad(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; /* the source has no quad */ }
static qa_app_source_foreign_powerups loading_foreign_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor; return (qa_app_source_foreign_powerups){0, 0, 0};
}
static bool loading_guest_grounded(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) { (void)sim; (void)actor; (void)out; return false; }
static bool loading_guest_client_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 *out_view_offset, bool *out_crouched) {
  (void)sim; (void)actor; (void)out_view_offset; (void)out_crouched; return false;
}
static bool loading_outputs_suspended(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)sim; (void)actor;
  return player != NULL && (player->intermission || player->has_cutscene);
}
static void loading_reaction(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision) {
  (void)sim; (void)actor; (void)decision; /* the source has no reaction of its own here */
}
static const char *loading_canonical_team(qa_app_shared_simulation *sim, const char *original) { (void)sim; return original; }
static double loading_match_score(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor;
  qa_raise("Error", 0, 0, "The selected source declares no match score");
}
static void loading_set_match_score(qa_app_shared_simulation *sim, qa_actor_id actor, double value) {
  (void)sim; (void)actor; (void)value;
  qa_raise("Error", 0, 0, "The selected source declares no match score");
}
static void loading_set_match_team(qa_app_shared_simulation *sim, qa_actor_id actor, const char *requested) {
  (void)sim; (void)actor; (void)requested;
  qa_raise("Error", 0, 0, "This original source has no declared team-change command");
}
static void loading_set_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity) {
  (void)sim; (void)actor; (void)identity;
  qa_raise("Error", 0, 0, "Source identity updates require a native Q1 or Q2 player");
}
static bool loading_set_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode) {
  (void)sim; (void)actor; (void)field_of_view; (void)mode; return false;
}
static bool loading_angle_delta(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, double out[3]) {
  (void)sim; (void)actor; (void)client; (void)out; return false;
}
static bool loading_command_movement(qa_app_shared_simulation *sim, qa_client_id client, qa_movement_state *out) {
  (void)sim; (void)client; (void)out; return false;
}
static const qa_item_id *loading_guest_grant_exclusions(qa_app_shared_simulation *sim, size_t *out_count) { (void)sim; *out_count = 0; return NULL; }
static const qa_pickup_supply_profile *loading_grant_profile(qa_app_shared_simulation *sim) { (void)sim; return NULL; }
static bool loading_source_item(qa_app_shared_simulation *sim, const char *requested, qa_q2_inventory_item *out) {
  (void)sim; (void)requested; (void)out; return false;
}
static void loading_granted_weapon(qa_app_shared_simulation *sim, qa_owned_actor owner, const char *requested, const qa_item_id *destinations,
    size_t destination_count, const qa_pickup_supply_profile *profile) {
  (void)sim; (void)owner; (void)requested; (void)destinations; (void)destination_count; (void)profile; /* only Q2 weapons carry their own ammo grant */
}
static qa_actor_id loading_ui_subject(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; return actor; }
static bool loading_client_ui(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_quakec_client_ui *out) {
  (void)sim; (void)actor; (void)out; return false;
}
static size_t loading_ui_powerups(qa_app_shared_simulation *sim, qa_actor_id actor, qa_active_powerup_timer *out, size_t capacity) {
  (void)sim; (void)actor; (void)out; (void)capacity; return 0;
}
static bool loading_guest_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out) { (void)sim; (void)actor; (void)out; return false; }
static double loading_cutscene_fov(qa_app_shared_simulation *sim, bool has_fov, double fov) { (void)sim; (void)has_fov; (void)fov; return 90; }
static bool loading_override_view(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_view *view, bool intermission, double health,
    qa_app_sim_player_view *out) {
  (void)sim; (void)actor; (void)view; (void)intermission; (void)health; (void)out; return false;
}
static void loading_presentations(qa_app_shared_simulation *sim, double fraction, double back_lerp, void *list) {
  (void)sim; (void)fraction; (void)back_lerp; (void)list; /* the source draws nothing of its own */
}
static qa_q2_entity *loading_q2_player_entity(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return NULL; }
static qa_q1_player_state *loading_q1_player(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return NULL; }
static qa_comp_q1_source_client *loading_q1_colors(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return NULL; }
static bool loading_weapon_visible(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) { (void)sim; (void)actor; (void)out; return false; }
static double loading_guest_bob_cycle(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return 0; }
static bool loading_travelers(qa_app_shared_simulation *sim, qa_client_id *clients, qa_actor_id *actors, size_t capacity, size_t *out_count) {
  (void)sim; (void)clients; (void)actors; (void)capacity; *out_count = 0; return false;
}
static bool loading_guest_health(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) { (void)sim; (void)actor; (void)out; return false; }
static void loading_set_guest_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  (void)sim; (void)actor; (void)health; /* only a QVM guest keeps a health stat outside the shared combat view */
}
static size_t loading_guest_travel_players(qa_app_shared_simulation *sim, qa_app_source_guest_traveler *out, size_t capacity) {
  (void)sim; (void)out; (void)capacity; return 0;
}
static qa_app_source_strategy loading_strategy(void) {
  qa_app_source_strategy s; memset(&s, 0, sizeof s);
  s.kind = "loading";
  s.as_q1 = loading_as; s.as_q2 = loading_as; s.as_quakec = loading_as;
  s.as_q3 = loading_as; s.as_q3_qvm = loading_as; s.as_q2_native = loading_as;
  s.userinfo = loading_userinfo;
  s.store_userinfo = loading_store_userinfo;
  s.update_userinfo = loading_update_userinfo;
  s.pause = loading_pause;
  s.pause_is_spectator = loading_actor_false;
  s.admit_client = loading_admit_client;
  s.prepare_bot = loading_prepare_bot;
  s.initial_arsenal_state = loading_initial_arsenal_state;
  s.attach_entity = loading_attach_entity;
  s.configure_admitted = loading_configure_admitted;
  s.attach_foreign_character = loading_player;       /* only Q1 sources dress their players in a Q2 character */
  s.admit_carry = loading_admit_carry;
  s.enter_world = loading_enter_world;
  s.capture_entry_carry = loading_capture_entry_carry;
  s.q2_gibbed = loading_actor_false;
  s.q3_client = loading_q3_client;
  s.supplies_punch = loading_false;
  s.skips_trigger_touch = loading_actor_false;
  s.admits_guest_monsters = loading_false;
  s.projects_monsters = loading_false;
  s.uses_guest_monsters = loading_false;
  s.guest_edition = loading_null_text;
  s.handoff = qa_ss_loaded_handoff;
  s.weapon_damage_factor = qa_ss_loaded_weapon_damage_factor;
  s.weapon_delay = qa_ss_loaded_weapon_delay;
  s.equipment_delay = qa_ss_loaded_equipment_delay;
  s.curves_enabled = loading_true;
  s.player_curve_clip = loading_true;
  s.friendly_fire = loading_true;
  s.knockback = loading_knockback;
  s.intermission_queued = loading_zero;
  s.quad_factor = loading_quad_factor;
  s.proximity_timeout = loading_proximity_timeout;
  s.runs_q3_powerups = loading_false;
  s.steps_hand_grenades = loading_true;
  s.team_game = loading_team_game;
  s.keeps_angle_deltas = loading_false;
  s.stop_speed = loading_stop_speed;
  s.legacy_q3_family = loading_null_text;
  s.uses_q2_views = loading_false;
  s.renders_own_intermission = loading_false;
  s.has_guest_ui = loading_false;
  s.guest_ui = loading_guest_ui;
  s.forwards_commands = loading_false;
  s.pre_command = loading_pre_command;
  s.after_command = loading_after_command;
  s.lmctf = loading_lmctf;
  s.prepares_q1_clients = loading_false;              /* only Q1 clients are prepared */
  s.intermission_fading = loading_false;
  s.fade_intermission = loading_nothing;              /* only a Q2 rerelease source fades an intermission */
  s.begin_timed_frame = loading_begin_timed_frame;    /* only a Q1 source takes timed frames */
  s.begin_guest_frame = loading_frame;                /* only a QuakeC source takes guest frames */
  s.begin_actor_turns = loading_frame;                /* the source has no per-boundary actor setup */
  s.intermission_active = loading_false;
  s.request_intermission_exit = loading_request_intermission_exit; /* only a Q1 source has an intermission to leave */
  s.pre_frame = loading_pre_frame;                    /* only a Q1 source runs a client pre-frame */
  s.pre_think = loading_pre_think;
  s.player_think = loading_player_think;
  s.tracks_gravity = loading_false;
  s.gravity_scale = loading_gravity_scale;
  s.movement_applied = loading_movement_applied;      /* only a Q2 source reacts to movement impacts */
  s.after_player_command = loading_after_player_command; /* the source has no post-command client think */
  s.before_client_actor = loading_owned;              /* the source has no per-client actor setup */
  s.retouch = loading_owned;                          /* only a Q1 source forces retouches */
  s.spectating = loading_spectating;
  s.run_player_actor = loading_run_player_actor;
  s.watches_transitions = loading_false;
  s.step_route = loading_step_route;
  s.settles = loading_settles;
  s.sync_gravity = loading_nothing;                   /* only a Q2 source owns the world gravity setting */
  s.after_step = loading_nothing;                     /* the source has no post-step work */
  s.finish_frame = loading_finish_frame;              /* the source has no end-of-frame work */
  s.conclude_frame = loading_nothing;                 /* the source has no frame conclusion */
  s.q1_powerup = loading_q1_powerup;
  s.q1_character_frame = loading_q1_character_frame;  /* only a Q1 source records its character frames */
  s.notarget = loading_notarget;
  s.cloak_alpha = loading_cloak_alpha;
  s.god_mode = loading_actor_false;
  s.weapon_event = loading_weapon_event;
  s.has_enemy = loading_actor_false;
  s.team_damage_enabled = loading_team_damage_enabled;
  s.match_damage_effects = loading_match_damage_effects;
  s.required_monster_noise = loading_required_monster_noise;
  s.require_q2 = loading_require_q2;
  s.pushed = loading_pushed;
  s.set_entity_gravity = loading_set_entity_gravity;
  s.monsters_searching = loading_monsters_searching;
  s.q1_game = loading_q1_game;
  s.select_weapon = loading_select_weapon;
  s.connected_clients = loading_connected_clients;
  s.player_actors = loading_player_actors;
  s.selected_player = qa_ss_loading_selected_player;
  s.q3_game_type = loading_q3_game_type;
  s.q3_server_info = loading_q3_server_info;
  s.primary_equipment = loading_false;
  s.has_equipment_effects = loading_false;
  s.equipment_damage_factor = loading_equipment_damage_factor;
  s.equipment_firing_delay = loading_equipment_firing_delay;
  s.check_hurt_carrier = loading_check_hurt_carrier;
  s.check_obelisk_attack = loading_check_obelisk_attack;
  s.return_pickup = loading_return_pickup;
  s.install_pickup_admission = loading_install_pickup_admission;
  s.supply_family = loading_supply_family;
  s.owns_supply = loading_false;
  s.identity_supply = loading_false;
  s.extra_replaced_items = loading_extra_replaced_items;
  s.animates_weapon_fire = loading_false;
  s.note_fired = loading_note_fired;
  s.weapon_noise = loading_weapon_noise;
  s.allows_auto_switch = loading_actor_true;
  s.cvars = loading_cvars;
  s.q1_powerup_expires = loading_q1_powerup_expires;
  s.damage_modifier = loading_damage_modifier;
  s.monster_noise = loading_monster_noise;
  s.teleport_spawn = qa_ss_loading_teleport_spawn;
  s.drop_objectives = qa_ss_loading_drop_objectives;
  s.original_player = loading_original_player;
  s.set_max_health = loading_set_max_health;
  s.equipment_suppression = loading_equipment_suppression;
  s.owns_equipment_players = loading_false;
  s.source_inventory = loading_source_inventory;
  s.player_available = loading_player_available;
  s.teleport_bits = loading_teleport_bits;
  s.equipment_powerups = loading_equipment_powerups;
  s.refine_weapon_input = loading_refine_weapon_input;
  s.guest_weapon_animation = loading_guest_weapon_animation;
  s.write_character_animation = loading_write_character_animation;
  s.character_pose = loading_character_pose;
  s.weapon_facts = loading_weapon_facts;
  s.base_arsenal = loading_base_arsenal;
  s.q1_client = loading_q1_client;
  s.q1_impulse = loading_q1_impulse;
  s.weapons_paused = loading_false;
  s.step_selected_holdable = loading_step_selected_holdable;
  s.primary_weapon_step = loading_primary_weapon_step;
  s.player_weapon = loading_player;                   /* only Q1 and Q2 sources run a client weapon frame */
  s.drops_impulse = loading_false;
  s.requests_weapon_directly = loading_false;
  s.always_requests_weapon = loading_false;
  s.owns_contacts = loading_owns_contacts;
  s.quake_world_contacts = loading_false;
  s.jump_voice = loading_jump_voice;
  s.animation = loading_animation;
  s.attack_arsenal = loading_attack_arsenal;
  s.movement_bindings = loading_movement_bindings;
  s.server_cvars = loading_server_cvars;
  s.server_settings_owner = loading_server_settings_owner;
  s.client_of = loading_client_of;
  s.disconnect = loading_disconnect;
  s.drop_inventory = loading_player;                  /* only a Q1 source drops a backpack */
  s.respawn = loading_respawn;
  s.telefrag_cause_q1 = loading_false;
  s.cutscene_control = loading_cutscene_control;
  s.grant_quad = loading_grant_quad;
  s.foreign_powerups = loading_foreign_powerups;
  s.check_output_claim = qa_ss_loading_check_output_claim;
  s.guest_grounded = loading_guest_grounded;
  s.guest_client_view = loading_guest_client_view;
  s.outputs_suspended = loading_outputs_suspended;
  s.reaction_start = loading_reaction;
  s.reaction_end = loading_reaction;
  s.damage_committed = loading_reaction;
  s.canonical_team = loading_canonical_team;
  s.declares_match = loading_false;
  s.match_score = loading_match_score;
  s.set_match_score = loading_set_match_score;
  s.set_match_team = loading_set_match_team;
  s.set_identity = loading_set_identity;
  s.set_field_of_view = loading_set_field_of_view;
  s.angle_delta = loading_angle_delta;
  s.command_movement = loading_command_movement;
  s.guest_grant_exclusions = loading_guest_grant_exclusions;
  s.grant_profile = loading_grant_profile;
  s.source_item = loading_source_item;
  s.granted_weapon = loading_granted_weapon;
  s.ui_subject = loading_ui_subject;
  s.client_ui = loading_client_ui;
  s.ui_powerups = loading_ui_powerups;
  s.arsenal_ui = qa_ss_loading_arsenal_ui;
  s.guest_view = loading_guest_view;
  s.chasing = loading_actor_false;
  s.cutscene_fov = loading_cutscene_fov;
  s.override_view = loading_override_view;
  s.foreign_character_death = loading_false;
  s.rerelease_view_height = loading_false;
  s.presentations = loading_presentations;
  s.guest_weapon_views = loading_false;
  s.q2_player_entity = loading_q2_player_entity;
  s.q1_player = loading_q1_player;
  s.q1_colors = loading_q1_colors;
  s.weapon_visible = loading_weapon_visible;
  s.guest_bob_cycle = loading_guest_bob_cycle;
  s.travel_source = qa_ss_loading_travel_source;
  s.travelers = loading_travelers;
  s.travel_player_state = qa_ss_loading_travel_player_state;
  s.guest_health = loading_guest_health;
  s.set_guest_health = loading_set_guest_health;
  s.guest_travel_players = loading_guest_travel_players;
  s.require_loaded = loading_require_loaded;
  s.assert_save_ready = loading_nothing;
  s.reconstructs_actor = loading_actor_false;
  s.checkpoint_providers = loading_checkpoint_providers;
  s.checkpoint_guests = loading_checkpoint_guests;
  s.checkpoint_extras = loading_checkpoint_extras;
  s.native_clients = loading_native_clients;
  s.source_cvar_snapshots = loading_source_cvar_snapshots;
  s.expected_declaration = loading_null_text;
  s.legacy_combat = loading_legacy_combat;
  s.require_inventory_projection = loading_require_inventory_projection;
  s.inventory_coverage = loading_inventory_coverage;
  s.assert_savable = loading_nothing;
  s.capture_guest_save = loading_capture_guest_save;
  s.original_save = loading_original_save;
  s.restore_loading = loading_restore_loading;
  s.restore_guest = loading_restore_guest;
  s.monster_proxy = loading_monster_proxy;
  s.restore_continuation = loading_save;
  s.restore_foundation = loading_restore_foundation;
  s.restore_gravity = loading_save;
  s.ammo_timer = loading_ammo_timer;
  return s;
}

/* ---------------- LoadedStrategy ---------------- */
static qa_app_source_strategy loaded_strategy(void) {
  qa_app_source_strategy s = loading_strategy();
  s.require_loaded = loaded_require_loaded;
  return s;
}

/* ---------------- Q1Strategy ---------------- */
static void q1_begin_timed_frame(qa_app_shared_simulation *sim, double elapsed) {
  qa_q1_begin_frame(sim->source.q1_game, st_time_seconds(sim), elapsed);
}
static bool q1_intermission_active(qa_app_shared_simulation *sim) { return sim->source.q1_game->has_intermission; }
static void q1_request_intermission_exit(qa_app_shared_simulation *sim, qa_actor_id actor, double buttons) {
  qa_comp_q1_intermission_input exit = {actor, (qa_js_to_int32(buttons) & 1) != 0};
  qa_comp_q1_source_composition_request_intermission_exit(sim->source.q1_composition, buttons != 0, &exit);
}
static void q1_pre_frame(qa_app_shared_simulation *sim, const qa_app_source_pre_frame_input *input) {
  qa_app_shared_sim_set_world_gravity(sim, qa_cvar_registry_variable_value(sim->source.q1_cvars, "sv_gravity"));
  qa_comp_q1_source_composition_pre_frame(sim->source.q1_composition, input->elapsed);
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = st_player_at(sim, i);
    if (qa_ss_is(player->profile.kind, "q1-netquake")) continue;
    /* [...input.commands].reverse().find(...) */
    const qa_actor_command *received = NULL;
    for (size_t j = input->command_count; j-- > 0;)
      if (qa_same_actor(input->commands[j].actor, player->actor.id) && input->commands[j].sequence > player->last_sequence) {
        received = &input->commands[j]; break;
      }
    if (input->deferred && received && !player->has_cutscene && !player->intermission) {
      if (input->pending) qa_ss_map_set(input->pending, &player, &received->sequence);
      continue;
    }
    const qa_user_command *command = received ? &received->command : NULL;
    bool jump = command == NULL ? (qa_js_to_int32(player->buttons) & 2) != 0
        : command->kind == QA_USER_COMMAND_Q1 ? (qa_js_to_int32(command->as.q1.buttons) & 2) != 0
        : command->kind == QA_USER_COMMAND_Q2_RERELEASE ? (qa_js_to_int32(command->as.q2_rerelease.buttons) & 8) != 0
        : st_up_positive(command);
    double buttons = command ? st_buttons(command) : player->buttons;
    double impulse = 0;
    if (!(command && st_impulse(command, &impulse))) impulse = received && received->arsenal ? received->arsenal->impulse : 0;
    qa_comp_q1_source_input source_input = {!player->has_cutscene && (qa_js_to_int32(buttons) & 1) != 0, !player->has_cutscene && jump,
                                            (qa_js_to_int32(buttons) & 4) != 0, impulse};
    qa_comp_q1_source_composition_input(sim->source.q1_composition, player->actor.id, source_input);
    qa_comp_q1_source_composition_player_pre_think(sim->source.q1_composition, player->actor.id);
    qa_q1_player_frame(sim->source.q1_game, player->actor, st_time_seconds(sim), &player->water_level);
  }
}
static bool q1_pre_think(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_actor_command *command) {
  const qa_user_command *effective = &command->command;
  bool jump = effective->kind == QA_USER_COMMAND_Q1 || effective->kind == QA_USER_COMMAND_QW ? (qa_js_to_int32(st_buttons(effective)) & 2) != 0
      : effective->kind == QA_USER_COMMAND_Q2_RERELEASE ? (qa_js_to_int32(effective->as.q2_rerelease.buttons) & 8) != 0
      : st_up_positive(effective);
  /* command.arsenal?.impulse ?? ("impulse" in effective ? effective.impulse : 0) */
  double impulse = 0;
  if (command->arsenal) impulse = command->arsenal->impulse;
  else if (!st_impulse(effective, &impulse)) impulse = 0;
  qa_comp_q1_source_input input = {(qa_js_to_int32(st_buttons(effective)) & 1) != 0, jump, (qa_js_to_int32(st_buttons(effective)) & 4) != 0, impulse};
  qa_comp_q1_source_composition_input(sim->source.q1_composition, player->actor.id, input);
  qa_comp_q1_source_composition_player_pre_think(sim->source.q1_composition, player->actor.id);
  qa_q1_player_frame(sim->source.q1_game, player->actor, st_time_seconds(sim), &player->water_level);
  return st_live(sim, player->actor.id);
}
static void q1_after_player_command(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, bool paused) {
  (void)paused; /* TS Q1Strategy.afterPlayerCommand(player) ignores the paused flag */
  if (!st_live(sim, player->actor.id)) return;
  qa_q1_player_after_physics(sim->source.q1_game, player->actor, st_time_seconds(sim));
  qa_comp_q1_source_composition_player_post_think(sim->source.q1_composition, player->actor.id);
}
static void q1_retouch(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  /* host.relink(actor) */
  if (sim->source.q1_game->force_retouch > 0) {
    qa_shared_body_table_link(qa_app_sim_shared_physics_bodies(sim->physics), actor, NULL);
    qa_app_sim_shared_physics_touch_triggers(sim->physics, actor);
  }
}
static void q1_before_client_actor(qa_app_shared_simulation *sim, qa_owned_actor actor) { q1_retouch(sim, actor); }
static void q1_conclude_frame(qa_app_shared_simulation *sim) {
  if (sim->source.q1_game->force_retouch > 0) sim->source.q1_game->force_retouch--;
}
static double q1_powerup(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  qa_q1_player_state *state = qa_q1_player(sim->source.q1_game, actor);
  return state ? qa_q1_player_powerup_get(state, powerup) : 0;
}
static void q1_character_frame(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_character_presentation *frame) {
  qa_comp_q1_source_composition_character_frame(sim->source.q1_composition, actor, frame);
}
static bool shared_admission(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel,
    const char *userinfo, qa_app_sim_player_admission *out) {
  (void)sim; (void)client; (void)travel; (void)userinfo; (void)out; return false; /* TS admitClient(): null */
}
static qa_owned_actor q1_prepare_bot(qa_app_shared_simulation *sim, qa_client_id client) {
  return qa_app_shared_sim_require_player(sim, qa_app_shared_sim_admit_player(sim, client).actor)->actor;
}
static qa_weapon_state q1_initial_arsenal_state(qa_app_shared_simulation *sim) {
  (void)sim;
  qa_weapon_state state; memset(&state, 0, sizeof state);
  state.kind = "q1"; state.frame = 0; state.attack_finished_seconds = 0; state.source_weapon = 1;
  return state;
}
static void q1_configure_admitted(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client, qa_app_sim_movement_player *player,
    qa_q2_entity *entity, const char *userinfo, const qa_app_simulation_travel *travel) {
  (void)player; (void)entity; (void)travel;
  qa_q1_attach_player(sim->source.q1_game, actor, "shotgun", true, NULL);
  qa_comp_q1_client_admission admission = {client.slot, NULL, 0};
  qa_ss_info info = {0};
  if (!userinfo) {
    char text[64];
    snprintf(text, sizeof text, "Player %lld", (long long)client.slot + 1);
    qa_ss_info_set(&info, "name", text); qa_ss_info_set(&info, "topcolor", "0"); qa_ss_info_set(&info, "bottomcolor", "0");
  } else info = qa_ss_q2_userinfo(userinfo);
  qa_comp_userinfo_entry *entries = calloc(info.count + 1, sizeof *entries);
  if (!entries) qa_range_error("Out of memory");
  for (size_t i = 0; i < info.count; i++) entries[i] = (qa_comp_userinfo_entry){info.items[i].name, info.items[i].value};
  admission.userinfo = entries; admission.userinfo_count = info.count;
  qa_comp_q1_source_composition_attach(sim->source.q1_composition, actor, &admission);
  free(entries); qa_ss_info_free(&info);
}
static void q1_attach_foreign_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if (player->character == QA_GAME_FAMILY_Q2) qa_app_shared_sim_attach_q2_character(sim, player);
}
static bool q1_admit_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q2_entity *entity, const qa_app_sim_travel_player *carried) {
  (void)entity;
  if (carried == NULL || !qa_ss_is(carried->state_kind, "q1")) return false;
  qa_comp_q1_source_composition_admit_travel(sim->source.q1_composition, actor, carried->carry);
  return true;
}
static void q1_enter_world(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player, bool fresh) {
  (void)fresh;
  qa_app_shared_sim_spawn_q1_player(sim, actor, player);
}
static bool q1_capture_entry_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_travel_state *out) {
  *out = qa_comp_q1_source_composition_capture_travel(sim->source.q1_composition, actor);
  return true;
}
static const char *q1_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  const qa_comp_q1_source_client *record = qa_comp_q1_source_clients_require(&sim->source.q1_composition->clients, actor);
  qa_app_sim_cvar_name_value *items = calloc(record->userinfo_count + 1, sizeof *items);
  if (!items) qa_range_error("Out of memory");
  for (size_t i = 0; i < record->userinfo_count; i++) items[i] = (qa_app_sim_cvar_name_value){record->userinfo[i].key, record->userinfo[i].value};
  char *text = qa_ss_info_text(items, record->userinfo_count);
  free(items);
  return st_keep_text(text);
}
static void q1_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)client;
  qa_comp_q1_source_client *record = qa_comp_q1_source_clients_require(&sim->source.q1_composition->clients, actor);
  qa_ss_info info = qa_ss_q2_userinfo(userinfo);
  /* info.clear(); for (const [name, value] of q2Userinfo(userinfo)) info.set(name, value). */
  for (size_t i = 0; i < record->userinfo_count; i++) { free((void *)record->userinfo[i].key); free((void *)record->userinfo[i].value); }
  record->userinfo_count = 0;
  if (info.count > record->userinfo_capacity) {
    qa_comp_userinfo_entry *grown = realloc(record->userinfo, info.count * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    record->userinfo = grown; record->userinfo_capacity = info.count;
  }
  for (size_t i = 0; i < info.count; i++)
    record->userinfo[record->userinfo_count++] = (qa_comp_userinfo_entry){info.items[i].name, info.items[i].value};
  free(info.items); /* the strings moved into the record */
}
static void q1_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  const qa_comp_q1_source_client *record = qa_comp_q1_source_clients_require(&sim->source.q1_composition->clients, actor);
  qa_ss_info values = qa_ss_q2_userinfo(userinfo);
  qa_app_sim_cvar_name_value *current = calloc(record->userinfo_count + 1, sizeof *current);
  if (!current) qa_range_error("Out of memory");
  for (size_t i = 0; i < record->userinfo_count; i++) current[i] = (qa_app_sim_cvar_name_value){record->userinfo[i].key, record->userinfo[i].value};
  bool same = qa_ss_info_same(current, record->userinfo_count, &values);
  free(current);
  if (same) { qa_ss_info_free(&values); return; }
  qa_comp_userinfo_entry *entries = calloc(values.count + 1, sizeof *entries);
  if (!entries) qa_range_error("Out of memory");
  for (size_t i = 0; i < values.count; i++) entries[i] = (qa_comp_userinfo_entry){values.items[i].name, values.items[i].value};
  qa_comp_q1_source_composition_userinfo(sim->source.q1_composition, actor, entries, values.count);
  free(entries); qa_ss_info_free(&values);
  qa_app_shared_sim_userinfo_changed(sim, actor, previous);
}
static bool q1_pause(qa_app_shared_simulation *sim, qa_cvar_registry **cvars) { *cvars = sim->source.q1_cvars; return true; }
static bool q1_notarget(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  *out = qa_comp_q1_source_composition_no_target(sim->source.q1_composition, actor);
  return true;
}
static bool q1_god_mode(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_get(&sim->source.q1_composition->clients, actor);
  return client ? client->god_mode : false;
}
static qa_q1_services *q1_q1_game(qa_app_shared_simulation *sim) { return sim->source.q1_game; }
static bool q1_select_weapon(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_item_id item) {
  qa_q1_services *game = sim->source.q1_game;
  /* [...Q1_WEAPONS, ...game.registeredWeapons.keys()].find(value => game.weaponItem(value) === item) */
  qa_q1_weapon weapon = NULL;
  for (size_t i = 0; i < 8 && !weapon; i++) if (qa_ss_is(qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[i]), item)) weapon = QA_Q1_WEAPONS[i];
  for (size_t i = 0; i < game->registered_weapon_count && !weapon; i++)
    if (qa_ss_is(qa_q1_services_weapon_item(game, game->registered_weapons[i].def->id), item)) weapon = game->registered_weapons[i].def->id;
  return weapon != NULL && qa_q1_select_weapon(game, actor, weapon);
}
static void q1_install_pickup_admission(qa_app_shared_simulation *sim, const qa_pickup_admission *admission) {
  sim->source.q1_game->pickup_admission = admission;
}
static const char *q1_supply_family(qa_app_shared_simulation *sim) { (void)sim; return "q1"; }
static void q1_note_fired(qa_app_shared_simulation *sim, qa_actor_id actor, qa_item_id item) {
  qa_comp_q1_source_composition_fired(sim->source.q1_composition, actor, item);
}
static bool q1_allows_auto_switch(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  return !(player != NULL && qa_ss_is(player->auto_switch, "never"));
}
static qa_cvar_registry *q1_cvars(qa_app_shared_simulation *sim) { return sim->source.q1_cvars; }
static double q1_q1_powerup_expires(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  return player != NULL ? qa_q1_player_powerup_get(player, powerup) : 0;
}
static qa_app_source_equipment_powerups q1_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  return (qa_app_source_equipment_powerups){true, player != NULL ? qa_q1_player_powerup_get(player, "quad") : 0, 0};
}
static bool q1_character_pose(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q1_character_source_pose *out) {
  *out = qa_comp_q1_source_composition_character_pose(sim->source.q1_composition, actor);
  return true;
}
static qa_app_source_weapon_facts q1_weapon_facts(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  return (qa_app_source_weapon_facts){NULL, false, "right", player != NULL ? qa_q1_player_powerup_get(player, "quad") : 0, false};
}
static qa_comp_q1_source_client *q1_q1_client(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_comp_q1_source_clients_require(&sim->source.q1_composition->clients, actor);
}
static void q1_q1_impulse(qa_app_shared_simulation *sim, qa_actor_id actor) { qa_comp_q1_source_composition_impulse(sim->source.q1_composition, actor); }
static bool q1_primary_weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input,
    qa_weapon_step_result *out) {
  (void)input; (void)out; /* TS Q1Strategy.primaryWeaponStep(player) */
  qa_app_shared_sim_player_weapon(sim, player);
  return false;
}
static bool q1_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)player;
  qa_comp_q1_source_composition_disconnect(sim->source.q1_composition, actor);
  return false;
}
static void q1_drop_inventory(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_comp_q1_source_composition_drop_inventory(sim->source.q1_composition, player->actor);
}
static void q1_respawn(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  const qa_q1_travel_state *carry = qa_ss_map_get(&sim->entry_carry, &player->actor); /* host.entryCarry(actor) */
  qa_comp_q1_source_composition_request_respawn(sim->source.q1_composition, player->actor.id, carry);
}
static void q1_cutscene_control(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_vec3 angles) {
  qa_q1_services_player_input(sim->source.q1_game, player->actor, false, false, NULL);
  qa_q1_weapon_input(sim->source.q1_game, player->actor, false, angles, st_time_seconds(sim), player->water_level);
}
static void q1_grant_quad(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  if (player != NULL) qa_q1_player_powerup_set(player, "quad", sim->source.q1_game->time + 30);
}
static qa_app_source_foreign_powerups q1_foreign_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(sim->source.q1_game, actor);
  return (qa_app_source_foreign_powerups){player ? qa_q1_player_powerup_get(player, "quad") : 0, 0,
      player ? qa_q1_player_powerup_get(player, "invulnerability") : 0};
}
static void q1_reaction_end(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision) {
  qa_damage_decision copy = *decision;
  qa_comp_q1_source_composition_before_reaction(sim->source.q1_composition, actor, &copy);
}
static qa_q1_player_state *q1_q1_player(qa_app_shared_simulation *sim, qa_actor_id actor) { return qa_q1_player(sim->source.q1_game, actor); }
static qa_comp_q1_source_client *q1_q1_colors(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_comp_q1_source_clients_get(&sim->source.q1_composition->clients, actor);
}
static qa_cvar_snapshot *q1_source_cvar_snapshots(qa_app_shared_simulation *sim, size_t *out_count) {
  return qa_cvar_registry_snapshots(sim->source.q1_cvars, 0, out_count);
}
static qa_app_source_strategy q1_strategy(void) {
  qa_app_source_strategy s = loaded_strategy();
  s.kind = "q1";
  s.legacy_q3_family = q1_legacy_q3_family;
  s.handoff = qa_ss_q1_handoff;
  s.team_game = q1_team_game;
  s.intermission_queued = q1_intermission_queued;
  s.pre_command = qa_ss_q1_pre_command;
  s.after_command = qa_ss_q1_after_command;
  s.as_q1 = own_source;
  s.userinfo = q1_userinfo;
  s.store_userinfo = q1_store_userinfo;
  s.update_userinfo = q1_update_userinfo;
  s.pause = q1_pause;
  s.admit_client = shared_admission;
  s.prepare_bot = q1_prepare_bot;
  s.initial_arsenal_state = q1_initial_arsenal_state;
  s.configure_admitted = q1_configure_admitted;
  s.attach_foreign_character = q1_attach_foreign_character;
  s.admit_carry = q1_admit_carry;
  s.enter_world = q1_enter_world;
  s.capture_entry_carry = q1_capture_entry_carry;
  s.prepares_q1_clients = loading_true;
  s.begin_timed_frame = q1_begin_timed_frame;
  s.intermission_active = q1_intermission_active;
  s.request_intermission_exit = q1_request_intermission_exit;
  s.pre_frame = q1_pre_frame;
  s.pre_think = q1_pre_think;
  s.after_player_command = q1_after_player_command;
  s.before_client_actor = q1_before_client_actor;
  s.retouch = q1_retouch;
  s.conclude_frame = q1_conclude_frame;
  s.q1_powerup = q1_powerup;
  s.q1_character_frame = q1_character_frame;
  s.notarget = q1_notarget;
  s.god_mode = q1_god_mode;
  s.q1_game = q1_q1_game;
  s.select_weapon = q1_select_weapon;
  s.selected_player = qa_ss_q1_selected_player;
  s.install_pickup_admission = q1_install_pickup_admission;
  s.supply_family = q1_supply_family;
  s.note_fired = q1_note_fired;
  s.allows_auto_switch = q1_allows_auto_switch;
  s.cvars = q1_cvars;
  s.q1_powerup_expires = q1_q1_powerup_expires;
  s.teleport_spawn = qa_ss_q1_teleport_spawn;
  s.drop_objectives = qa_ss_q1_drop_objectives;
  s.set_max_health = qa_ss_q1_set_max_health;
  s.equipment_powerups = q1_equipment_powerups;
  s.character_pose = q1_character_pose;
  s.weapon_facts = q1_weapon_facts;
  s.base_arsenal = qa_ss_q1_base_arsenal;
  s.q1_client = q1_q1_client;
  s.q1_impulse = q1_q1_impulse;
  s.primary_weapon_step = q1_primary_weapon_step;
  s.player_weapon = qa_ss_q1_player_weapon;
  s.drops_impulse = loading_true;
  s.requests_weapon_directly = loading_true;
  s.movement_bindings = qa_ss_q1_movement_bindings;
  s.server_cvars = q1_cvars;
  s.disconnect = q1_disconnect;
  s.drop_inventory = q1_drop_inventory;
  s.respawn = q1_respawn;
  s.telefrag_cause_q1 = loading_true;
  s.cutscene_control = q1_cutscene_control;
  s.grant_quad = q1_grant_quad;
  s.foreign_powerups = q1_foreign_powerups;
  s.reaction_end = q1_reaction_end;
  s.canonical_team = qa_ss_q1_canonical_team;
  s.declares_match = loading_true;
  s.match_score = qa_ss_q1_match_score;
  s.set_match_score = qa_ss_q1_set_match_score;
  s.set_match_team = qa_ss_q1_set_match_team;
  s.set_identity = qa_ss_q1_set_identity;
  s.grant_profile = qa_ss_q1_grant_profile;
  s.ui_powerups = qa_ss_q1_ui_powerups;
  s.arsenal_ui = qa_ss_q1_arsenal_ui;
  s.q1_player = q1_q1_player;
  s.q1_colors = q1_q1_colors;
  s.travel_source = qa_ss_q1_travel_source;
  s.travel_player_state = qa_ss_q1_travel_player_state;
  s.checkpoint_providers = qa_ss_q1_checkpoint_providers;
  s.source_cvar_snapshots = q1_source_cvar_snapshots;
  s.restore_foundation = qa_ss_q1_restore_foundation;
  return s;
}

/* ---------------- QuakeCStrategy ---------------- */
static bool quakec_prepares_q1_clients(qa_app_shared_simulation *sim) {
  return qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "netquake");
}
static void quakec_begin_guest_frame(qa_app_shared_simulation *sim, qa_frame_context frame) {
  qa_app_sim_quakec_source_begin_frame(sim->source.quakec, frame);
}
static void quakec_before_client_actor(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_app_sim_quakec_source_before_actor(sim->source.quakec, actor);
}
static void quakec_after_player_command(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, bool paused) {
  (void)paused; /* TS QuakeCStrategy.afterPlayerCommand(player) ignores the paused flag */
  qa_app_sim_quakec_source *game = sim->source.quakec;
  if (!qa_ss_is(qa_app_sim_quakec_source_kind(game), "quakeworld") || !qa_ss_is(player->profile.kind, "q1-quakeworld")
      || !st_live(sim, player->actor.id)) return;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(game, player->actor.id);
  qa_app_sim_quakec_source_client_post_think(game, player->actor);
  qa_app_shared_sim_project_quake_c_attack(sim, player->actor, before);
  if (!qa_app_sim_quakec_source_is_spectator_client(game, player->actor.id)) qa_app_shared_sim_run_quake_world_new_missile(sim);
  /* player.arsenal = host.arsenal(player); a selected arsenal read is owned by the player (store_player_arsenal). */
  if (sim->has_selected_arsenal) qa_app_shared_sim_store_player_arsenal(sim, player, qa_app_shared_sim_arsenal(sim, player));
  else player->arsenal = qa_app_shared_sim_arsenal(sim, player);
  if (player->character == QA_GAME_FAMILY_Q1) player->animation = qa_app_sim_quakec_source_client_animation(game, player->actor.id);
  player->state = qa_app_sim_movement_player_read_state(player);
  qa_app_shared_sim_sync_quake_c_client_view(sim, player);
}
static void quakec_conclude_frame(qa_app_shared_simulation *sim) {
  qa_app_sim_quakec_source *game = sim->source.quakec;
  qa_app_sim_quakec_source_end_frame(game);
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = st_player_at(sim, i);
    bool intermission;
    if (qa_app_sim_quakec_source_local_client_intermission(game, player->actor.id, &intermission)) player->intermission = intermission;
  }
  if (qa_ss_is(qa_app_sim_quakec_source_kind(game), "quakeworld")) {
    qa_app_shared_sim_run_quake_world_commands(sim);
    qa_compat_qc_broadcast_messages_flush(qa_app_sim_quakec_source_messages(game));
  }
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = st_player_at(sim, i);
    if (player->character == QA_GAME_FAMILY_Q1 || player->intermission || player->has_cutscene) continue;
    qa_body_state body;
    if (!qa_app_shared_sim_body(sim, player->actor.id, &body)) continue;
    bool moving = qa_js_hypot2(body.velocity.x, body.velocity.y) != 0;
    const char *locomotion = player->water_level >= 2 ? "swim" : player->ground.kind == QA_TRACE_HIT_NONE ? "jump"
        : player->bounds.max.z < player->standing_bounds.max.z ? "crouch" : moving ? "run" : "idle";
    double yaw = player->view_angles.y * M_PI / 180;
    bool backwards = body.velocity.x * cos(yaw) + body.velocity.y * sin(yaw) < 0;
    qa_animation_step_input animation; memset(&animation, 0, sizeof animation);
    animation.actor = player->actor; animation.frame = sim->source_frame; animation.animation = player->animation;
    animation.locomotion = locomotion; animation.backwards = backwards; animation.force = false;
    player->animation = qa_app_shared_sim_quake_c_character_step(sim, animation).animation;
  }
}
static bool quakec_admit_client(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel,
    const char *userinfo, qa_app_sim_player_admission *out) {
  *out = qa_app_shared_sim_admit_quake_c_player(sim, client, travel, userinfo);
  return true;
}
static bool quakec_supplies_punch(qa_app_shared_simulation *sim) {
  return qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "netquake");
}
static bool quakec_skips_trigger_touch(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_quakec_source *game = sim->source.quakec;
  return qa_ss_is(qa_app_sim_quakec_source_kind(game), "quakeworld") && qa_app_sim_quakec_source_is_spectator_client(game, actor);
}
static const char *quakec_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) return st_keep_text(NULL);
  qa_app_sim_quakec_info info = qa_app_sim_quakec_source_client_info(sim->source.quakec, client);
  return st_keep_text(qa_ss_info_text(info.items, info.count));
}
static void quakec_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)actor;
  qa_ss_info info = qa_ss_q2_userinfo(userinfo);
  qa_app_sim_quakec_source_set_client_info_storage(sim->source.quakec, client, (qa_app_sim_quakec_info){info.items, info.count});
  qa_ss_info_free(&info);
}
static void quakec_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) return;
  qa_app_sim_quakec_info current = qa_app_sim_quakec_source_client_info(sim->source.quakec, client);
  qa_ss_info values = qa_ss_q2_userinfo(userinfo);
  if (qa_ss_info_same(current.items, current.count, &values)) { qa_ss_info_free(&values); return; }
  qa_app_sim_quakec_source_set_client_info(sim->source.quakec, client, (qa_app_sim_quakec_info){values.items, values.count});
  qa_ss_info_free(&values);
  qa_app_shared_sim_userinfo_changed(sim, actor, previous);
}
static bool quakec_pause(qa_app_shared_simulation *sim, qa_cvar_registry **cvars) {
  *cvars = qa_app_sim_quakec_source_cvars(sim->source.quakec); return true;
}
static bool quakec_notarget(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  return qa_app_sim_quakec_source_notarget(sim->source.quakec, actor, out);
}
static const qa_client_id *quakec_connected_clients(qa_app_shared_simulation *sim, size_t *out_count) {
  if (!qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "netquake")) { *out_count = 0; return NULL; }
  return qa_app_sim_quakec_source_connected_client_identities(sim->source.quakec, out_count);
}
static double quakec_equipment_firing_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent) {
  return qa_q3_weapon_delay(milliseconds, persistent, qa_app_shared_sim_lmctf_haste(sim, actor));
}
static qa_cvar_registry *quakec_cvars(qa_app_shared_simulation *sim) { return qa_app_sim_quakec_source_cvars(sim->source.quakec); }
static double quakec_q1_powerup_expires(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  if (qa_ss_is(powerup, "quad") || qa_ss_is(powerup, "invulnerability") || qa_ss_is(powerup, "invisibility") || qa_ss_is(powerup, "suit"))
    return qa_app_sim_quakec_source_client_powerup_expires(sim->source.quakec, actor, powerup);
  return qa_app_shared_sim_selected_q1_powerup(sim, actor, powerup);
}
static qa_app_source_equipment_suppression quakec_equipment_suppression(qa_app_shared_simulation *sim) {
  (void)sim; return (qa_app_source_equipment_suppression){true, false, false};
}
static qa_app_source_weapon_facts quakec_weapon_facts(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return (qa_app_source_weapon_facts){NULL, false, "right", qa_app_sim_quakec_source_client_powerup_expires(sim->source.quakec, actor, "quad"), false};
}
static qa_arsenal_state quakec_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  return qa_app_sim_quakec_source_client_arsenal(sim->source.quakec, player->actor.id);
}
static bool quakec_owns_contacts(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_app_sim_quakec_source_is_spectator_client(sim->source.quakec, actor);
}
static bool quakec_quake_world_contacts(qa_app_shared_simulation *sim) {
  return qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), "quakeworld");
}
static bool quakec_animation(qa_app_shared_simulation *sim, const qa_animation_step_input *input, qa_animation_step_result *out) {
  memset(out, 0, sizeof *out);
  out->animation = qa_ss_is(input->animation.state.kind, "q1") ? qa_app_sim_quakec_source_client_animation(sim->source.quakec, input->actor.id)
      : input->animation;
  return true;
}
static bool quakec_attack_arsenal(qa_app_shared_simulation *sim, qa_actor_id actor, qa_arsenal_state *out) {
  *out = qa_app_sim_quakec_source_client_arsenal(sim->source.quakec, actor);
  return true;
}
static bool quakec_client_ui(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_quakec_client_ui *out) {
  *out = qa_app_sim_quakec_source_client_ui(sim->source.quakec, actor);
  return true;
}
static const qa_guest_checkpoint *quakec_checkpoint_guests(qa_app_shared_simulation *sim, size_t *out_count) {
  *out_count = 1;
  return qa_app_sim_quakec_source_checkpoint(sim->source.quakec);
}
static qa_app_source_strategy quakec_strategy(void) {
  qa_app_source_strategy s = loaded_strategy();
  s.kind = "quakec";
  s.team_game = quakec_team_game;
  s.handoff = qa_ss_quakec_handoff;
  s.weapon_damage_factor = qa_ss_quakec_weapon_damage_factor;
  s.quad_factor = loading_one;
  s.uses_guest_monsters = loading_true;
  s.pre_command = qa_ss_quakec_pre_command;
  s.admits_guest_monsters = loading_true;
  s.as_quakec = own_source;
  s.userinfo = quakec_userinfo;
  s.store_userinfo = quakec_store_userinfo;
  s.update_userinfo = quakec_update_userinfo;
  s.pause = quakec_pause;
  s.pause_is_spectator = quakec_skips_trigger_touch; /* game.kind === "quakeworld" && game.isSpectatorClient(actor) */
  s.admit_client = quakec_admit_client;
  s.supplies_punch = quakec_supplies_punch;
  s.skips_trigger_touch = quakec_skips_trigger_touch;
  s.prepares_q1_clients = quakec_prepares_q1_clients;
  s.begin_guest_frame = quakec_begin_guest_frame;
  s.before_client_actor = quakec_before_client_actor;
  s.after_player_command = quakec_after_player_command;
  s.conclude_frame = quakec_conclude_frame;
  s.notarget = quakec_notarget;
  s.connected_clients = quakec_connected_clients;
  s.selected_player = qa_ss_quakec_selected_player;
  s.has_equipment_effects = loading_true;
  s.equipment_firing_delay = quakec_equipment_firing_delay;
  s.supply_family = q1_supply_family;
  s.owns_supply = loading_true;
  s.cvars = quakec_cvars;
  s.q1_powerup_expires = quakec_q1_powerup_expires;
  s.damage_modifier = qa_ss_quakec_damage_modifier;
  s.teleport_spawn = qa_ss_quakec_teleport_spawn;
  s.drop_objectives = qa_ss_quakec_drop_objectives;
  s.set_max_health = qa_ss_quakec_set_max_health;
  s.equipment_suppression = quakec_equipment_suppression;
  s.weapon_facts = quakec_weapon_facts;
  s.base_arsenal = quakec_base_arsenal;
  s.requests_weapon_directly = loading_true;
  s.owns_contacts = quakec_owns_contacts;
  s.quake_world_contacts = quakec_quake_world_contacts;
  s.animation = quakec_animation;
  s.attack_arsenal = quakec_attack_arsenal;
  s.movement_bindings = qa_ss_quakec_movement_bindings;
  s.server_cvars = quakec_cvars;
  s.disconnect = qa_ss_quakec_disconnect;
  s.check_output_claim = qa_ss_quakec_check_output_claim;
  s.outputs_suspended = qa_ss_quakec_outputs_suspended;
  s.damage_committed = qa_ss_quakec_damage_committed;
  s.canonical_team = qa_ss_quakec_canonical_team;
  s.declares_match = loading_true;
  s.match_score = qa_ss_quakec_match_score;
  s.set_match_score = qa_ss_quakec_set_match_score;
  s.set_match_team = qa_ss_quakec_set_match_team;
  s.client_ui = quakec_client_ui;
  s.ui_powerups = qa_ss_quakec_ui_powerups;
  s.override_view = qa_ss_quakec_override_view;
  s.travel_source = qa_ss_quakec_travel_source;
  s.travel_player_state = qa_ss_shared_travel_player_state;
  s.checkpoint_guests = quakec_checkpoint_guests;
  return s;
}

/* ---------------- Q2Strategy ---------------- */
static qa_q2_lmctf *q2_lmctf(qa_app_shared_simulation *sim) {
  qa_q2_product_match *match = &sim->source.product->match;
  return match->source_kind == QA_Q2_MATCH_LMCTF ? match->lmctf : NULL;
}
static bool q2_intermission_fading(qa_app_shared_simulation *sim) {
  return sim->source.product->rerelease != NULL && sim->source.product->rerelease->players.has_intermission_fade_until;
}
static void q2_fade_intermission(qa_app_shared_simulation *sim) {
  if (sim->source.product->rerelease != NULL)
    qa_q2_rerelease_players_fade_frame(&sim->source.product->rerelease->players, sim->source.product->services);
}
static void q2_begin_actor_turns(qa_app_shared_simulation *sim, qa_frame_context frame) {
  (void)frame;
  qa_q2m_monsters_begin_frame(&sim->source.product->monsters, sim->source.product->services);
}
static double q2_gravity_scale(qa_app_shared_simulation *sim, qa_actor_id actor) {
  double match_gravity = qa_q2_product_match_gravity_scale(&sim->source.product->match, actor);
  double flight_gravity = qa_q2_item_module_player_powerups(sim->source.product->items, actor).flight_until > st_time_seconds(sim) ? 0.1 : 1;
  return match_gravity * flight_gravity;
}
static void q2_movement_applied(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_movement_result *moved) {
  if (qa_ss_is(moved->kind, "q2-rerelease") && qa_ss_is(moved->status, "active"))
    qa_q2_product_runtime_movement_impact(sim->source.product, actor, moved->impact_delta,
        (qa_js_to_int32(moved->state.as.q2_rerelease.flags) & 128) != 0);
}
static void q2_after_player_command(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, bool paused) {
  if (paused || !st_live(sim, player->actor.id)) return;
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_entity *entity = game->entity(game->ctx, &player->actor.id);
  if (entity == NULL) return;
  if (!player->intermission) {
    qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, player->actor.id);
    entity->view_height = player->character == QA_GAME_FAMILY_Q2 && st_health(sim, player->actor.id) <= 0
        ? (state && state->gibbed ? 8 : -2) : player->view_height;
  }
  qa_q2_players_after_client_think(sim->source.product->players, entity, game);
}
static bool q2_spectating(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor);
  return state != NULL && state->spectator;
}
static void q2_sync_gravity(qa_app_shared_simulation *sim) {
  qa_cvar_registry *registry = sim->q2_server_registry;
  if (registry == NULL) return;
  double gravity = qa_cvar_registry_variable_value(registry, "sv_gravity");
  if (gravity != qa_app_sim_shared_physics_gravity(sim->physics)) qa_app_shared_sim_set_world_gravity(sim, gravity);
}
static void q2_after_step(qa_app_shared_simulation *sim) { qa_q2_emit_shadow_lights(sim->source.product->services); }
static void q2_finish_frame(qa_app_shared_simulation *sim, bool run) {
  qa_q2_game_services *game = sim->source.product->services;
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = st_player_at(sim, i);
    qa_q2_entity *entity = game->entity(game->ctx, &player->actor.id);
    if (!entity) continue;
    if (!player->intermission && player->character == QA_GAME_FAMILY_Q2 && st_health(sim, player->actor.id) <= 0) {
      qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, player->actor.id);
      entity->view_height = state && state->gibbed ? 8 : -2; player->view_height = entity->view_height;
    }
    qa_q2_players_end_frame(sim->source.product->players, entity, game);
    qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, player->actor.id);
    if (player->character == QA_GAME_FAMILY_Q2 && state) {
      player->animation.provider = sim->recipe.character.definition.provider;
      memset(&player->animation.state, 0, sizeof player->animation.state);
      player->animation.state.kind = "q2"; player->animation.state.frame = entity->frame;
      player->animation.state.end_frame = state->animation_end; player->animation.state.priority = state->animation_priority;
      player->animation.state.duck = state->animation_duck; player->animation.state.run = state->animation_run;
    }
  }
  if (!run) return;
  qa_q2_product_match *match = &sim->source.product->match;
  if (match->source_kind == QA_Q2_MATCH_LMCTF && match->lmctf) {
    qa_lmctf_match *state = qa_q2_lmctf_get_match(match->lmctf);
    if (qa_ss_is(qa_lmctf_match_phase(state), "countdown") && !qa_lmctf_match_paused(state) && qa_lmctf_match_remaining(state) <= 0
        && game->host->now(game->host->ctx) >= qa_lmctf_match_next_think(state) && sim->q2_server_registry) {
      size_t count = 0;
      free(qa_cvar_registry_apply_latched(sim->q2_server_registry, "timelimit", &count));
    }
  }
  qa_q2_product_runtime_after_player_frames(sim->source.product);
  qa_q2m_monsters_end_frame(&sim->source.product->monsters, sim->source.product->services);
  /* host.withQ2Rules(() => source.product.checkRules()) */
  sim->checking_q2_rules = true;
  qa_q2_product_runtime_check_rules(sim->source.product);
  sim->checking_q2_rules = false;
}
static qa_owned_actor q2_prepare_bot(qa_app_shared_simulation *sim, qa_client_id client) {
  qa_app_sim_player_admission admitted = qa_app_shared_sim_admit_player(sim, client);
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_entity *entity = game->entity(game->ctx, &admitted.actor);
  if (!entity) qa_raise("Error", 0, 0, "Bot admission lost its shared source entity");
  entity->server_flags = qa_js_bit_or(entity->server_flags, 16);
  return entity->actor;
}
static qa_weapon_state q2_initial_arsenal_state(qa_app_shared_simulation *sim) {
  (void)sim;
  qa_weapon_state state; memset(&state, 0, sizeof state);
  state.kind = "q2"; state.gun_frame = 0; state.state = 0; state.pending_weapon = NULL;
  state.machinegun_shots = 0; state.grenade_time = (qa_source_time){QA_SOURCE_TIME_SECONDS, 0}; state.grenade_blew_up = false;
  return state;
}
static qa_q2_entity *q2_attach_entity(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player) {
  qa_q2_entity *entity = qa_q2_entity_services_attach_player(sim->source.product->game, actor);
  if (entity) { entity->view_height = player->view_height; entity->solid = "box"; }
  return entity;
}
static void q2_configure_admitted(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client, qa_app_sim_movement_player *player,
    qa_q2_entity *entity, const char *userinfo, const qa_app_simulation_travel *travel) {
  qa_ss_source_runtime *source = &sim->source;
  qa_q2_item_module_configure_player(source->product->items, actor, source->product->services, true);
  if (entity == NULL) return;
  const char *appearance = player->character_appearance.provider, *slash = strrchr(appearance, '/');
  const char *model = player->character == QA_GAME_FAMILY_Q2 ? (slash ? slash + 1 : appearance) : "male";
  char text[256];
  if (!userinfo) snprintf(text, sizeof text, "\\name\\Player %lld\\skin\\%s/grunt\\fov\\90", (long long)client.slot + 1, model);
  qa_q2_player_admission admission; memset(&admission, 0, sizeof admission);
  admission.slot = client.slot; admission.userinfo = userinfo ? userinfo : text; admission.initialize_inventory = false;
  admission.has_use_q2_weapons = true; admission.use_q2_weapons = !sim->has_selected_arsenal; /* host.usesQ2Weapons() */
  qa_q2_landmark_carry landmark;
  bool has_landmark = travel && qa_ss_is(travel->source_kind, "q2") && travel->has_landmark && travel->landmark_client_slot == client.slot;
  if (has_landmark) {
    landmark.player = actor.id; landmark.name = travel->landmark_name; landmark.relative_origin = travel->landmark_relative_origin;
    landmark.relative_velocity = travel->landmark_relative_velocity; landmark.relative_view_angles = travel->landmark_relative_view_angles;
  }
  qa_q2_product_runtime_admit(source->product, actor, admission, has_landmark ? &landmark : NULL);
}
static bool q2_admit_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q2_entity *entity, const qa_app_sim_travel_player *carried) {
  (void)actor;
  if (carried == NULL || !qa_ss_is(carried->state_kind, "q2")) return false;
  if (entity != NULL) {
    qa_q2_base_player_carry carry = *(const qa_q2_base_player_carry *)carried->carry;
    qa_q2_players_restore_carry(sim->source.product->players, entity, sim->source.product->services, carry);
  }
  return true;
}
static void q2_enter_world(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player, bool fresh) {
  (void)player;
  if (fresh || qa_ss_map_has(&sim->pending_start_items, &actor.id)) qa_app_shared_sim_give_q2_start_items(sim, actor.id);
  qa_app_shared_sim_resume_q2_presentation(sim, &actor.id);
}
static bool q2_gibbed(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *state = qa_q2_players_get_state(sim->source.product->players, actor);
  return state != NULL && state->gibbed;
}
static const char *q2_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *state = st_q2_state(sim, actor);
  return st_keep_text(state ? st_dup(state->userinfo) : NULL);
}
static void q2_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)client;
  qa_q2_base_player_state *player = st_q2_state(sim, actor);
  if (!player) qa_raise("Error", 0, 0, "Component userinfo has no Q2 player");
  snprintf(player->userinfo, sizeof player->userinfo, "%s", userinfo);
}
static void q2_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  qa_q2_base_player_state *state = st_q2_state(sim, actor);
  qa_q2_entity *entity = st_q2_entity(sim, actor);
  if (!state || !entity || strcmp(state->userinfo, userinfo) == 0) return;
  double previous_fov = state->fov;
  qa_ss_info before = qa_ss_q2_userinfo(state->userinfo), after = qa_ss_q2_userinfo(userinfo);
  const char *before_fov = qa_ss_info_get(&before, "fov"), *after_fov = qa_ss_info_get(&after, "fov");
  bool same_request = before_fov == after_fov || (before_fov && after_fov && strcmp(before_fov, after_fov) == 0);
  qa_ss_info_free(&after);
  qa_ss_info_free(&before);
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_players_userinfo_changed(sim->source.product->players, entity, sim->source.product->services, userinfo);
  if (player->has_cutscene || state->has_chase_target || qa_js_bit_and(entity->flags, 0x4000) != 0 || same_request) state->fov = previous_fov;
  qa_q2_base_player_view *view = qa_ss_map_get(&sim->q2_views, &actor);
  if (view && view->fov == previous_fov) view->fov = state->fov;
  qa_app_shared_sim_userinfo_changed(sim, actor, previous);
}
static bool q2_cloak_alpha(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) {
  if (sim->source.product->rerelease == NULL) return false; /* players instanceof Q2RereleasePlayers */
  qa_q2_game_services *game = sim->source.product->services;
  return qa_q2_rerelease_players_cloak_alpha(&sim->source.product->rerelease->players, actor, game->host->now(game->host->ctx), out);
}
static bool q2_weapon_event(qa_app_shared_simulation *sim, const qa_q2w_event *event) {
  qa_q2_players_weapon_event(sim->source.product->players, *event);
  return true;
}
static bool q2_has_enemy(qa_app_shared_simulation *sim, qa_actor_id target) {
  qa_q2_entity *entity = qa_q2_game_entity_of(sim->source.product->services, &target);
  return entity != NULL && entity->has_enemy;
}
static bool q2_team_damage_enabled(qa_app_shared_simulation *sim, const char *match_provider) {
  return qa_ss_is(match_provider, "q2:ctf") || qa_ss_is(match_provider, "q2:lmctf")
    || (qa_js_to_int32(sim->source.product->services->options.deathmatch_flags) & (64 | 128)) != 0;
}
static qa_q2_damage_source_effects q2_match_damage_effects(qa_app_shared_simulation *sim, qa_q2_product_match_effects_binding *storage) {
  return qa_q2_product_match_source_effects(storage, &sim->source.product->match, sim->source.product->services);
}
static void q2_required_monster_noise(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, bool secondary, const char *message) {
  (void)message;
  qa_q2m_monsters_report_noise(&sim->source.product->monsters, actor, origin, secondary);
}
static void q2_require_q2(qa_app_shared_simulation *sim, const char *message) { (void)sim; (void)message; /* the source is Q2 */ }
static void q2_pushed(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 velocity) {
  qa_q2_base_player_state *state = st_q2_state(sim, actor);
  if (state) state->old_velocity = velocity;
}
static void q2_set_entity_gravity(qa_app_shared_simulation *sim, qa_actor_id actor, double gravity) {
  qa_q2_entity *entity = st_q2_entity(sim, actor);
  if (entity) entity->gravity = gravity;
}
static bool q2_monsters_searching(qa_app_shared_simulation *sim, const qa_actor_id *player) {
  qa_q2_game_services *game = sim->source.product->services;
  size_t count = 0; qa_q2_entity *const *entities = game->entities(game->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_q2_entity *entity = entities[i];
    if ((qa_js_to_int32(entity->server_flags) & 4) == 0 || st_health(sim, entity->actor.id) <= 0) continue;
    qa_q2m_context *context = qa_q2m_monsters_context(&sim->source.product->monsters, entity->actor.id);
    qa_q2m_state *state = context ? context->state : NULL;
    if (player == NULL ? !(entity->has_enemy && !game->host->is_player(game->host->ctx, entity->enemy))
                       : !(entity->has_enemy && qa_same_actor(entity->enemy, *player))) continue;
    if (state && state->lost_sight && st_time_seconds(sim) > state->trail_time + 5) continue;
    return true;
  }
  return false;
}
static void q2_install_pickup_admission(qa_app_shared_simulation *sim, const qa_pickup_admission *admission) {
  qa_q2_item_module_set_pickup_admission(sim->source.product->items, admission);
}
static const char *q2_supply_family(qa_app_shared_simulation *sim) { (void)sim; return "q2"; }
static const qa_item_id *q2_extra_replaced_items(qa_app_shared_simulation *sim, size_t *out_count) {
  static const qa_item_id items[1] = {"q2:weapon_blaster"};
  (void)sim; *out_count = 1; return items;
}
static void q2_weapon_noise(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, const char *kind) {
  qa_q2w_ballistics_player_noise_for_actor(&sim->source.q2_weapons->base, actor, sim->source.product->services, origin, kind);
}
static double q2_q1_powerup_expires(qa_app_shared_simulation *sim, qa_actor_id actor, const char *powerup) {
  qa_q2_player_powerups powers = qa_q2_item_module_player_powerups(sim->source.product->items, actor);
  return qa_ss_is(powerup, "quad") ? powers.quad_until : qa_ss_is(powerup, "invulnerability") ? powers.invulnerability_until
      : qa_ss_is(powerup, "suit") ? powers.enviro_until : 0;
}
static void q2_monster_noise(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, bool secondary) {
  qa_q2m_monsters_report_noise(&sim->source.product->monsters, actor, origin, secondary);
}
static qa_q2w_input q2_refine_weapon_input(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2w_input input) {
  return qa_q2_product_runtime_weapon_input(sim->source.product, actor, input);
}
static qa_app_source_weapon_facts q2_weapon_facts(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *state = st_q2_state(sim, actor);
  return (qa_app_source_weapon_facts){NULL, state != NULL && state->spectator, state != NULL && state->hand != NULL ? state->hand : "right",
      qa_q2_item_module_player_powerups(sim->source.product->items, actor).quad_until,
      (qa_js_to_int32(qa_q2_options_deathmatch_flags(&sim->source.product->services->options)) & 8192) != 0};
}
static bool q2_weapons_paused(qa_app_shared_simulation *sim) {
  return sim->source.product->match.source_kind == QA_Q2_MATCH_LMCTF && qa_lmctf_match_paused(qa_q2_lmctf_get_match(sim->source.product->match.lmctf));
}
static void q2_player_weapon(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_q2_entity *entity = st_q2_entity(sim, player->actor.id);
  if (entity != NULL) {
    qa_q2_product_match_before_player(&sim->source.product->match, entity, sim->source.product->services);
    qa_q2_players_begin_frame(sim->source.product->players, entity, sim->source.product->services);
  }
}
static bool q2_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)player;
  qa_q2_entity *entity = st_q2_entity(sim, actor);
  if (entity == NULL) return true;
  qa_q2_game_services *game = sim->source.product->services;
  qa_q2_players_disconnect(sim->source.product->players, entity, game);
  game->remove(game->ctx, entity);
  return st_live(sim, actor);
}
static void q2_respawn(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_q2_entity *entity = st_q2_entity(sim, player->actor.id);
  if (entity) qa_q2_players_respawn(sim->source.product->players, entity, sim->source.product->services);
}
static bool q2_source_item(qa_app_shared_simulation *sim, const char *requested, qa_q2_inventory_item *out) {
  return qa_q2_item_module_lookup(sim->source.product->items, requested, out);
}
static double q2_cutscene_fov(qa_app_shared_simulation *sim, bool has_fov, double fov) { (void)sim; return has_fov ? fov : 90; }
static bool q2_rerelease_view_height(qa_app_shared_simulation *sim) { return sim->source.product->rerelease != NULL; }
static qa_q2_entity *q2_q2_player_entity(qa_app_shared_simulation *sim, qa_actor_id actor) { return st_q2_entity(sim, actor); }
static void q2_restore_gravity(qa_app_shared_simulation *sim, const qa_save_image *save) {
  qa_cvar_registry *registry = sim->q2_server_registry;
  if (registry == NULL) return;
  qa_save_value *saved = qa_app_sim_saved_source_cvars(*save);
  /* The merge in restoreQ2ServerCvars preserves registered defaults, so
   * presence must be tested against the saved state itself. */
  if (saved == NULL || !qa_saved_q2_server_cvars_have(registry, saved, "sv_gravity")) {
    char gravity[64];
    qa_net_js_number(qa_app_sim_shared_physics_gravity(sim->physics), gravity, sizeof gravity);
    qa_cvar_read found; qa_cvar_snapshot snapshot;
    if (!qa_cvar_registry_find(registry, "sv_gravity", &found)) {
      if (qa_cvar_registry_register(registry, "sv_gravity", gravity, 0, &snapshot)) qa_cvar_read_free(&snapshot);
    } else {
      qa_cvar_read_free(&found);
      if (qa_cvar_registry_set(registry, "sv_gravity", gravity, true, &snapshot)) qa_cvar_read_free(&snapshot);
    }
  }
  qa_save_value_free(saved);
}
static qa_app_source_strategy q2_strategy(void) {
  qa_app_source_strategy s = loaded_strategy();
  s.kind = "q2";
  s.legacy_q3_family = q2_legacy_q3_family;
  s.handoff = qa_ss_q2_handoff;
  s.stop_speed = q2_stop_speed;
  s.team_game = q2_team_game;
  s.friendly_fire = q2_friendly_fire;
  s.intermission_queued = q2_intermission_queued;
  s.uses_q2_views = loading_true;
  s.pre_command = qa_ss_q2_pre_command;
  s.after_command = qa_ss_q2_after_command;
  s.as_q2 = own_source;
  s.userinfo = q2_userinfo;
  s.store_userinfo = q2_store_userinfo;
  s.update_userinfo = q2_update_userinfo;
  s.admit_client = shared_admission;
  s.prepare_bot = q2_prepare_bot;
  s.initial_arsenal_state = q2_initial_arsenal_state;
  s.attach_entity = q2_attach_entity;
  s.configure_admitted = q2_configure_admitted;
  s.admit_carry = q2_admit_carry;
  s.enter_world = q2_enter_world;
  s.q2_gibbed = q2_gibbed;
  s.lmctf = q2_lmctf;
  s.intermission_fading = q2_intermission_fading;
  s.fade_intermission = q2_fade_intermission;
  s.begin_actor_turns = q2_begin_actor_turns;
  s.tracks_gravity = loading_true;
  s.gravity_scale = q2_gravity_scale;
  s.movement_applied = q2_movement_applied;
  s.after_player_command = q2_after_player_command;
  s.spectating = q2_spectating;
  s.watches_transitions = loading_true;
  s.sync_gravity = q2_sync_gravity;
  s.after_step = q2_after_step;
  s.finish_frame = q2_finish_frame;
  s.cloak_alpha = q2_cloak_alpha;
  s.weapon_event = q2_weapon_event;
  s.has_enemy = q2_has_enemy;
  s.team_damage_enabled = q2_team_damage_enabled;
  s.match_damage_effects = q2_match_damage_effects;
  s.required_monster_noise = q2_required_monster_noise;
  s.require_q2 = q2_require_q2;
  s.pushed = q2_pushed;
  s.set_entity_gravity = q2_set_entity_gravity;
  s.monsters_searching = q2_monsters_searching;
  s.selected_player = qa_ss_q2_selected_player;
  s.install_pickup_admission = q2_install_pickup_admission;
  s.supply_family = q2_supply_family;
  s.extra_replaced_items = q2_extra_replaced_items;
  s.weapon_noise = q2_weapon_noise;
  s.q1_powerup_expires = q2_q1_powerup_expires;
  s.monster_noise = q2_monster_noise;
  s.teleport_spawn = qa_ss_q2_teleport_spawn;
  s.drop_objectives = qa_ss_q2_drop_objectives;
  s.set_max_health = qa_ss_q2_set_max_health;
  s.refine_weapon_input = q2_refine_weapon_input;
  s.weapon_facts = q2_weapon_facts;
  s.base_arsenal = qa_ss_q2_base_arsenal;
  s.weapons_paused = q2_weapons_paused;
  s.player_weapon = q2_player_weapon;
  s.requests_weapon_directly = loading_true;
  s.always_requests_weapon = loading_true;
  s.server_cvars = host_q2_server_cvars;
  s.disconnect = q2_disconnect;
  s.respawn = q2_respawn;
  s.outputs_suspended = qa_ss_q2_outputs_suspended;
  s.reaction_end = qa_ss_q2_reaction_end;
  s.damage_committed = qa_ss_q2_damage_committed;
  s.canonical_team = qa_ss_q2_canonical_team;
  s.declares_match = loading_true;
  s.match_score = qa_ss_q2_match_score;
  s.set_match_score = qa_ss_q2_set_match_score;
  s.set_match_team = qa_ss_q2_set_match_team;
  s.set_identity = qa_ss_q2_set_identity;
  s.set_field_of_view = qa_ss_q2_set_field_of_view;
  s.grant_profile = qa_ss_q2_grant_profile;
  s.source_item = q2_source_item;
  s.granted_weapon = qa_ss_q2_granted_weapon;
  s.ui_subject = qa_ss_q2_ui_subject;
  s.ui_powerups = qa_ss_q2_ui_powerups;
  s.arsenal_ui = qa_ss_q2_arsenal_ui;
  s.chasing = qa_ss_q2_chasing;
  s.cutscene_fov = q2_cutscene_fov;
  s.rerelease_view_height = q2_rerelease_view_height;
  s.q2_player_entity = q2_q2_player_entity;
  s.travel_source = qa_ss_q2_travel_source;
  s.travel_player_state = qa_ss_q2_travel_player_state;
  s.checkpoint_providers = qa_ss_q2_checkpoint_providers;
  s.restore_foundation = qa_ss_q2_restore_foundation;
  s.restore_gravity = q2_restore_gravity;
  return s;
}

/* ---------------- Q2NativeStrategy ---------------- */
static qa_app_source_step_route q2_native_step_route(qa_app_shared_simulation *sim) { (void)sim; return QA_APP_SOURCE_STEP_NATIVE; }
static bool q2_native_admit_client(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel,
    const char *userinfo, qa_app_sim_player_admission *out) {
  if (travel) qa_raise("Error", 0, 0, "Native Quake II travel requires its source file lifecycle");
  char text[256];
  snprintf(text, sizeof text, "\\name\\Player %lld\\skin\\male/grunt\\fov\\90", (long long)client.slot + 1);
  *out = qa_app_shared_sim_admit_q2_native_player(sim, client, userinfo ? userinfo : text, true);
  return true;
}
static const qa_app_sim_classic_guest_client *q2_native_clients(qa_app_shared_simulation *sim, size_t *count) {
  return qa_ss_is(sim->source.edition, "classic") ? qa_app_sim_classic_guest_world_clients(sim->source.classic_world, count)
      : qa_app_sim_rerelease_guest_world_clients(sim->source.rerelease_world, count);
}
static const char *q2_native_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  size_t count;
  const qa_app_sim_classic_guest_client *clients = q2_native_clients(sim, &count);
  for (size_t i = 0; i < count; i++) if (clients[i].slot == slot) return st_keep_text(st_dup(clients[i].userinfo));
  return st_keep_text(NULL);
}
static void q2_native_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)actor;
  if (qa_ss_is(sim->source.edition, "classic")) qa_app_sim_classic_guest_world_set_userinfo_storage(sim->source.classic_world, client.slot + 1, userinfo);
  else qa_app_sim_rerelease_guest_world_set_userinfo_storage(sim->source.rerelease_world, client.slot + 1, userinfo);
}
static void q2_native_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  size_t count;
  const qa_app_sim_classic_guest_client *clients = q2_native_clients(sim, &count);
  const char *current = NULL;
  for (size_t i = 0; i < count; i++) if (clients[i].slot == slot) { current = clients[i].userinfo; break; }
  if (!current || strcmp(current, userinfo) != 0) {
    if (qa_ss_is(sim->source.edition, "classic")) qa_app_sim_classic_guest_world_userinfo(sim->source.classic_world, slot, userinfo);
    else qa_app_sim_rerelease_guest_world_userinfo(sim->source.rerelease_world, slot, userinfo);
  }
  qa_app_shared_sim_userinfo_changed(sim, actor, previous);
}
static bool q2_native_notarget(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, actor);
  qa_module_identity module = sim->source.classic_world ? qa_app_sim_classic_guest_world_module(sim->source.classic_world)
    : qa_app_sim_rerelease_guest_world_module(sim->source.rerelease_world);
  if (!source.has_value || !qa_ss_is(source.provider, module.id)) return false;
  *out = sim->source.classic_world
    ? qa_app_sim_classic_guest_services_notarget(qa_app_sim_classic_guest_world_services(sim->source.classic_world), (double)source.slot)
    : qa_app_sim_rerelease_guest_services_notarget(qa_app_sim_rerelease_guest_world_services(sim->source.rerelease_world), (double)source.slot);
  return true;
}
static const qa_client_id *q2_native_connected_clients(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_client_id *storage; static __thread size_t capacity;
  storage = st_grow(storage, &capacity, sim->source.clients.count + 1, sizeof *storage);
  for (size_t i = 0; i < sim->source.clients.count; i++) storage[i] = qa_ss_map_key_at(&sim->source.clients, i)->client;
  *out_count = sim->source.clients.count;
  return storage;
}
static const qa_actor_id *q2_native_player_actors(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_actor_id *storage; static __thread size_t capacity;
  storage = st_grow(storage, &capacity, sim->source.clients.count + 1, sizeof *storage);
  for (size_t i = 0; i < sim->source.clients.count; i++) storage[i] = ((qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i))->id;
  *out_count = sim->source.clients.count;
  return storage;
}
static double q2_native_equipment_firing_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent) {
  return qa_q3_weapon_delay(qa_app_shared_sim_selected_weapon_delay(sim, actor, milliseconds / 1000) * 1000, persistent, false);
}
static qa_app_source_equipment_powerups zero_equipment_powerups(qa_app_shared_simulation *sim, qa_actor_id actor) {
  (void)sim; (void)actor; return (qa_app_source_equipment_powerups){true, 0, 0};
}
static bool q2_native_guest_weapon_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee) {
  (void)melee; /* TS Q2NativeStrategy.guestWeaponAnimation(actor, priority) */
  if (qa_ss_is(priority, "attack")) qa_compat_q2_native_primary_weapons_attack_animation(qa_app_shared_sim_require_native_weapons(sim), actor);
  return true;
}
static bool q2_native_primary_weapon_step(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_weapon_step_input *input,
    qa_weapon_step_result *out) {
  (void)sim; (void)player;
  memset(out, 0, sizeof *out);
  out->arsenal = input->arsenal; out->animation = input->animation;
  return true;
}
static int q2_native_client_of(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id *out) {
  for (size_t i = 0; i < sim->source.clients.count; i++)
    if (qa_same_actor(((qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i))->id, actor)) {
      *out = qa_ss_map_key_at(&sim->source.clients, i)->client;
      return 1;
    }
  return 0;
}
static const qa_pickup_supply_profile *q2_native_grant_profile(qa_app_shared_simulation *sim) { return sim->selected_original_supply; }
static bool q2_native_reconstructs_actor(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, actor);
  return source.has_value && qa_ss_is(source.provider, st_native_module(sim).id);
}
static bool q2_native_native_clients(qa_app_shared_simulation *sim, qa_app_source_native_client **out, size_t *out_count) {
  size_t count = 0;
  const qa_app_sim_classic_guest_client *clients = sim->source.classic_world != NULL
    ? qa_app_sim_classic_guest_world_clients(sim->source.classic_world, &count)
    : qa_app_sim_rerelease_guest_world_clients(sim->source.rerelease_world, &count);
  qa_app_source_native_client *list = calloc(count != 0 ? count : 1, sizeof *list);
  if (list == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) list[i] = (qa_app_source_native_client){clients[i].slot - 1, clients[i].phase, clients[i].userinfo};
  *out = list; *out_count = count;
  return true;
}
static void q2_native_require_inventory_projection(qa_app_shared_simulation *sim, const char *kind) {
  (void)kind;
  if (!qa_app_shared_sim_native_has_source_inventory(sim)) qa_raise("Error", 0, 0, "Saved native inventory requires qualified source storage");
}
static bool q2_native_inventory_coverage(qa_app_shared_simulation *sim, bool native, bool qvm, qa_app_source_inventory_coverage *out) {
  (void)qvm;
  if (!qa_app_shared_sim_native_has_source_inventory(sim)) return false;
  size_t count = sim->source.clients.count;
  qa_owned_actor *actors = qa_app_shared_sim_keep(sim, (count ? count : 1) * sizeof *actors);
  for (size_t i = 0; i < count; i++) actors[i] = *(const qa_owned_actor *)qa_ss_map_value_at(&sim->source.clients, i);
  *out = (qa_app_source_inventory_coverage){actors, count, !native};
  return true;
}
static void q2_native_assert_savable(qa_app_shared_simulation *sim) {
  qa_cvar_registry *registry = sim->q2_server_registry;
  if (qa_ss_is(sim->source.edition, "rerelease") && (registry == NULL || qa_cvar_registry_variable_value(registry, "deathmatch") != 0))
    qa_raise("Error", 0, 0, "Native Quake II original saves require a non-deathmatch source game");
}
static bool q2_native_capture_guest_save(qa_app_shared_simulation *sim, qa_app_shared_sim_next_frame_fn next_frame, void *frame_ctx,
    qa_app_sim_rerelease_guest_save *out) {
  if (!qa_ss_is(sim->source.edition, "rerelease")) return false;
  *out = qa_app_sim_rerelease_guest_world_write_save_loading(sim->source.rerelease_world, false, next_frame, frame_ctx);
  return true;
}
static bool q2_native_original_save(qa_app_shared_simulation *sim, const qa_app_sim_rerelease_guest_save *guest_save, qa_persist_decoded *owner,
    qa_provider_checkpoint *out) {
  qa_cvar_registry *cvars = sim->q2_server_registry;
  if (cvars == NULL || qa_cvar_registry_variable_value(cvars, "deathmatch") != 0)
    qa_raise("Error", 0, 0, "Native Quake II original saves require a non-deathmatch source game");
  qa_app_shared_sim_native_original_checkpoint(sim, guest_save, owner, out);
  return true;
}
static bool q2_native_proxy_allocate(qa_app_shared_simulation *sim, double *slot) {
  *slot = qa_app_sim_classic_guest_world_allocate_monster_proxy(sim->source.classic_world);
  return *slot >= 0;
}
static void q2_native_proxy_bind(qa_app_shared_simulation *sim, double slot, qa_owned_actor actor) {
  qa_app_sim_classic_guest_world_bind_monster_proxy(sim->source.classic_world, slot, actor);
}
static bool q2_native_monster_proxy(qa_app_shared_simulation *sim, qa_app_source_monster_proxy *out) {
  if (!qa_ss_is(sim->source.edition, "classic")) return false;
  *out = (qa_app_source_monster_proxy){q2_native_proxy_allocate, q2_native_proxy_bind};
  return true;
}
static qa_app_source_strategy q2_native_strategy(void) {
  qa_app_source_strategy s = loaded_strategy();
  s.kind = "q2-native";
  s.team_game = q2_native_team_game;
  s.handoff = qa_ss_native_handoff;
  s.weapon_damage_factor = qa_ss_q2_native_weapon_damage_factor;
  s.weapon_delay = qa_ss_q2_native_weapon_delay;
  s.equipment_delay = qa_ss_q2_native_equipment_delay;
  s.guest_edition = q2_native_guest_edition;
  s.renders_own_intermission = loading_false;
  s.steps_hand_grenades = loading_false;
  s.projects_monsters = loading_true;
  s.uses_guest_monsters = loading_true;
  s.has_guest_ui = loading_true;
  s.guest_ui = qa_ss_q2_native_guest_ui;
  s.pre_command = qa_ss_q2_native_pre_command;
  s.forwards_commands = loading_true;
  s.admits_guest_monsters = loading_true;
  s.as_q2_native = own_source;
  s.userinfo = q2_native_userinfo;
  s.store_userinfo = q2_native_store_userinfo;
  s.update_userinfo = q2_native_update_userinfo;
  s.admit_client = q2_native_admit_client;
  s.step_route = q2_native_step_route;
  s.notarget = q2_native_notarget;
  s.connected_clients = q2_native_connected_clients;
  s.player_actors = q2_native_player_actors;
  s.selected_player = qa_ss_q2_native_selected_player;
  s.has_equipment_effects = loading_true;
  s.equipment_firing_delay = q2_native_equipment_firing_delay;
  s.supply_family = q2_supply_family;
  s.animates_weapon_fire = loading_true;
  s.teleport_spawn = qa_ss_q2_native_teleport_spawn;
  s.drop_objectives = qa_ss_q2_native_drop_objectives;
  s.original_player = qa_ss_q2_native_original_player;
  s.equipment_suppression = native_equipment_suppression;
  s.owns_equipment_players = loading_true;
  s.source_inventory = qa_ss_q2_native_source_inventory;
  s.player_available = qa_ss_q2_native_player_available;
  s.teleport_bits = qa_ss_q2_native_teleport_bits;
  s.equipment_powerups = zero_equipment_powerups;
  s.guest_weapon_animation = q2_native_guest_weapon_animation;
  s.primary_weapon_step = q2_native_primary_weapon_step;
  s.server_cvars = host_q2_server_cvars;
  s.client_of = q2_native_client_of;
  s.disconnect = qa_ss_q2_native_disconnect;
  s.check_output_claim = qa_ss_q2_native_check_output_claim;
  s.guest_grounded = qa_ss_q2_native_guest_grounded;
  s.guest_client_view = qa_ss_q2_native_guest_client_view;
  s.outputs_suspended = qa_ss_q2_native_outputs_suspended;
  s.canonical_team = qa_ss_q2_native_canonical_team;
  s.declares_match = qa_ss_q2_native_declares_match;
  s.match_score = qa_ss_q2_native_match_score;
  s.set_match_score = qa_ss_q2_native_set_match_score;
  s.set_match_team = qa_ss_q2_native_set_match_team;
  s.set_field_of_view = qa_ss_q2_native_set_field_of_view;
  s.command_movement = qa_ss_q2_native_command_movement;
  s.grant_profile = q2_native_grant_profile;
  s.guest_view = qa_ss_q2_native_guest_view;
  s.presentations = qa_ss_q2_native_presentations;
  s.guest_weapon_views = loading_true;
  s.weapon_visible = qa_ss_q2_native_weapon_visible;
  s.reconstructs_actor = q2_native_reconstructs_actor;
  s.checkpoint_extras = qa_ss_q2_native_checkpoint_extras;
  s.native_clients = q2_native_native_clients;
  s.require_inventory_projection = q2_native_require_inventory_projection;
  s.inventory_coverage = q2_native_inventory_coverage;
  s.assert_savable = q2_native_assert_savable;
  s.capture_guest_save = q2_native_capture_guest_save;
  s.original_save = q2_native_original_save;
  s.restore_loading = qa_ss_q2_native_restore_loading;
  s.restore_guest = qa_ss_q2_native_restore_guest;
  s.monster_proxy = q2_native_monster_proxy;
  return s;
}

/* ---------------- Q3Strategy ---------------- */
static void q3_begin_actor_turns(qa_app_shared_simulation *sim, qa_frame_context frame) {
  qa_app_q3_source_runtime_begin_frame(sim->source.q3, frame);
}
static bool q3_player_think(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, const qa_actor_command *command) {
  /* host.setQ3Command(player.actor, command); try { game.playerThink(command) } finally { host.setQ3Command(player.actor, null) } */
  qa_ss_map_set(&sim->q3_commands, &player->actor, command);
  qa_app_q3_source_runtime_player_think(sim->source.q3, *command);
  qa_ss_map_delete(&sim->q3_commands, &player->actor);
  qa_app_shared_sim_sync_q3_player(sim, player);
  return true;
}
static bool q3_run_player_actor(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  if (!(qa_app_shared_sim_player_state(sim, actor) || qa_app_q3_source_runtime_record_by_actor(sim->source.q3, actor.id) != NULL)) return false;
  qa_app_q3_source_runtime_run_actor(sim->source.q3, actor);
  qa_app_sim_shared_physics_commit_attachments(sim->physics);
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (player) qa_app_shared_sim_sync_q3_player(sim, player);
  return true;
}
static bool q3_settles(qa_app_shared_simulation *sim, const void *game) { return sim->source.q3 == game; }
static void q3_finish_frame(qa_app_shared_simulation *sim, bool run) {
  (void)run; /* TS Q3Strategy.finishFrame() takes no arguments */
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = st_player_at(sim, i);
    if (!player->intermission && player->character == QA_GAME_FAMILY_Q3 && st_health(sim, player->actor.id) <= 0) {
      qa_q3_game_entity *entity = qa_app_q3_source_runtime_record_by_actor(sim->source.q3, player->actor.id);
      if (entity && entity->client) entity->client->ps.viewheight = -16;
    }
  }
  qa_app_q3_source_runtime_end_frame(sim->source.q3);
}
static bool q3_admit_client(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel,
    const char *userinfo, qa_app_sim_player_admission *out) {
  (void)travel; (void)userinfo;
  *out = qa_app_shared_sim_admit_q3_player(sim, client, sim->source.q3);
  return true;
}
static qa_owned_actor q3_prepare_bot(qa_app_shared_simulation *sim, qa_client_id client) {
  if (!qa_app_shared_sim_bot_slot_available(sim, client)) qa_raise("Error", 0, 0, "Bot client is not an available session slot");
  qa_owned_actor actor = qa_app_shared_sim_prepare_q3_client(sim, client, sim->source.q3, true);
  qa_app_q3_source_runtime_prepare_client(sim->source.q3, actor, client.slot);
  return actor;
}
static bool q3_client(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q3_game_client **out) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  *out = entity ? entity->client : NULL;
  return true;
}
static bool q3_skips_trigger_touch(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return true; }
static const char *q3_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) return st_keep_text(NULL);
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(sim->source.q3);
  return st_keep_text(host->engine.get_userinfo(host->engine.ctx, client.slot));
}
static void q3_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)actor;
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(sim->source.q3);
  host->engine.set_userinfo(host->engine.ctx, client.slot, userinfo);
}
static void q3_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) qa_raise("Error", 0, 0, "Q3 userinfo has no admitted client");
  qa_app_q3_source_host *host = qa_app_q3_source_runtime_host(sim->source.q3);
  host->engine.set_userinfo(host->engine.ctx, client.slot, userinfo);
  qa_app_q3_source_runtime_userinfo_changed(sim->source.q3, client.slot);
  qa_app_shared_sim_userinfo_changed(sim, actor, previous);
}
static bool q3_notarget(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  qa_q3_game_entity *record = qa_app_q3_source_runtime_record_by_actor(sim->source.q3, actor);
  *out = (qa_js_to_int32(record ? record->flags : 0) & 32) != 0;
  return true;
}
static bool q3_q3_game_type(qa_app_shared_simulation *sim, double *out) { *out = qa_app_q3_source_runtime_game_type(sim->source.q3); return true; }
static char *q3_q3_server_info(qa_app_shared_simulation *sim) {
  char *info = qa_app_q3_server_state_server_info(qa_app_q3_created_source_host_server_state(sim->source.q3_host));
  if (info == NULL) qa_range_error("Out of memory");
  return info;
}
static bool q3_primary_equipment(qa_app_shared_simulation *sim) {
  return qa_ss_is(qa_app_q3_source_runtime_options(sim->source.q3)->product, "missionpack");
}
static bool q3_has_equipment_effects(qa_app_shared_simulation *sim) { return sim->strategy->primary_equipment(sim); }
static qa_cvar_registry *q3_cvars(qa_app_shared_simulation *sim) { return q3_host_cvars(sim); }
static qa_app_source_equipment_suppression q3_equipment_suppression(qa_app_shared_simulation *sim) {
  (void)sim; return (qa_app_source_equipment_suppression){false, false, true};
}
static qa_arsenal_state q3_base_arsenal(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_arsenal_state state = player->arsenal;
  state.ammo = qa_shared_inventory_table_entries(sim->inventory, player->actor.id, &state.ammo_count);
  return state;
}
static bool q3_owns_contacts(qa_app_shared_simulation *sim, qa_actor_id actor) { (void)sim; (void)actor; return true; }
static bool q3_jump_voice(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player) {
  if (player->character != QA_GAME_FAMILY_Q3) return false;
  if (!qa_ss_is(player->profile.kind, "q3")) {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor.id);
    if (!entity || !entity->client) qa_raise("Error", 0, 0, "Q3 jump voice has no source client");
    qa_q3_base_player_state_record_add_event(&entity->client->ps, QA_Q3_EV_JUMP, 0);
  }
  return true;
}
static bool q3_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)player;
  qa_app_q3_source_runtime_disconnect_player(sim->source.q3, actor);
  return true;
}
static void q3_respawn(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, player->actor.id);
  if (entity) qa_app_q3_source_runtime_respawn(sim->source.q3, entity);
}
static void q3_reaction_start(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_damage_decision *decision) {
  qa_app_q3_source_runtime_before_reaction(sim->source.q3, actor, *decision);
}
static bool q3_angle_delta(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, double out[3]) {
  (void)client;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  if (!entity || !entity->client) return false;
  out[0] = entity->client->ps.delta_angles.x; out[1] = entity->client->ps.delta_angles.y; out[2] = entity->client->ps.delta_angles.z;
  return true;
}
static void q3_assert_save_ready(qa_app_shared_simulation *sim) {
  qa_app_sim_simulation_events_assert_output_consumed(sim->events);
  for (size_t i = 0; i < sim->player_states.count; i++) /* q3ArsenalsComplete() */
    if (!qa_ss_map_has(&sim->q3_arsenals, &qa_ss_map_key_at(&sim->player_states, i)->owned))
      qa_raise("Error", 0, 0, "Q3 save requires completed client admission");
}
static bool q3_ammo_timer(qa_app_shared_simulation *sim, qa_actor_id actor, double weapon, double *out) {
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, actor);
  if (native == NULL || native->client == NULL) return false;
  *out = qa_q3_base_player_state_slots_get(&native->client->ammo_times, weapon);
  return true;
}
static qa_app_source_strategy q3_strategy(void) {
  qa_app_source_strategy s = loaded_strategy();
  s.kind = "q3";
  s.team_game = q3_team_game;
  s.handoff = qa_ss_q3_handoff;
  s.weapon_damage_factor = qa_ss_q3_weapon_damage_factor;
  s.weapon_delay = qa_ss_q3_weapon_delay;
  s.keeps_angle_deltas = loading_true;
  s.renders_own_intermission = loading_true;
  s.curves_enabled = q3_curves_enabled;
  s.player_curve_clip = q3_player_curve_clip;
  s.friendly_fire = q3_friendly_fire;
  s.knockback = q3_knockback;
  s.intermission_queued = q3_intermission_queued;
  s.quad_factor = q3_quad_factor;
  s.proximity_timeout = q3_proximity_timeout;
  s.runs_q3_powerups = loading_true;
  s.pre_command = qa_ss_q3_pre_command;
  s.after_command = qa_ss_q3_after_command;
  s.admits_guest_monsters = loading_true;
  s.as_q3 = own_source;
  s.userinfo = q3_userinfo;
  s.store_userinfo = q3_store_userinfo;
  s.update_userinfo = q3_update_userinfo;
  s.admit_client = q3_admit_client;
  s.prepare_bot = q3_prepare_bot;
  s.q3_client = q3_client;
  s.skips_trigger_touch = q3_skips_trigger_touch;
  s.begin_actor_turns = q3_begin_actor_turns;
  s.player_think = q3_player_think;
  s.run_player_actor = q3_run_player_actor;
  s.settles = q3_settles;
  s.finish_frame = q3_finish_frame;
  s.notarget = q3_notarget;
  s.selected_player = qa_ss_q3_selected_player;
  s.q3_game_type = q3_q3_game_type;
  s.q3_server_info = q3_q3_server_info;
  s.primary_equipment = q3_primary_equipment;
  s.has_equipment_effects = q3_has_equipment_effects;
  s.equipment_damage_factor = qa_ss_q3_equipment_damage_factor;
  s.equipment_firing_delay = qa_ss_q3_equipment_firing_delay;
  s.check_hurt_carrier = qa_ss_q3_check_hurt_carrier;
  s.check_obelisk_attack = qa_ss_q3_check_obelisk_attack;
  s.return_pickup = qa_ss_q3_return_pickup;
  s.cvars = q3_cvars;
  s.q1_powerup_expires = qa_ss_q3_q1_powerup_expires;
  s.teleport_spawn = qa_ss_q3_teleport_spawn;
  s.drop_objectives = qa_ss_q3_drop_objectives;
  s.set_max_health = qa_ss_q3_set_max_health;
  s.equipment_suppression = q3_equipment_suppression;
  s.equipment_powerups = qa_ss_q3_equipment_powerups;
  s.write_character_animation = qa_ss_q3_write_character_animation;
  s.weapon_facts = qa_ss_q3_weapon_facts;
  s.base_arsenal = q3_base_arsenal;
  s.step_selected_holdable = qa_ss_q3_step_selected_holdable;
  s.primary_weapon_step = qa_ss_q3_primary_weapon_step;
  s.owns_contacts = q3_owns_contacts;
  s.jump_voice = q3_jump_voice;
  s.server_cvars = q3_cvars;
  s.server_settings_owner = qa_ss_q3_server_settings_owner;
  s.disconnect = q3_disconnect;
  s.respawn = q3_respawn;
  s.grant_quad = qa_ss_q3_grant_quad;
  s.foreign_powerups = qa_ss_q3_foreign_powerups;
  s.reaction_start = q3_reaction_start;
  s.canonical_team = qa_ss_q3_canonical_team;
  s.declares_match = loading_true;
  s.match_score = qa_ss_q3_match_score;
  s.set_match_score = qa_ss_q3_set_match_score;
  s.set_match_team = qa_ss_q3_set_match_team;
  s.angle_delta = q3_angle_delta;
  s.grant_profile = qa_ss_q3_grant_profile;
  s.ui_powerups = qa_ss_q3_ui_powerups;
  s.arsenal_ui = qa_ss_q3_arsenal_ui;
  s.foreign_character_death = loading_true;
  s.presentations = qa_ss_q3_presentations;
  s.travel_source = qa_ss_shared_travel_source;
  s.travel_player_state = qa_ss_shared_travel_player_state;
  s.assert_save_ready = q3_assert_save_ready;
  s.checkpoint_providers = qa_ss_q3_checkpoint_providers;
  s.restore_continuation = qa_ss_q3_restore_continuation;
  s.ammo_timer = q3_ammo_timer;
  return s;
}

/* ---------------- Q3QvmStrategy ---------------- */
static qa_app_source_step_route q3_qvm_step_route(qa_app_shared_simulation *sim) { (void)sim; return QA_APP_SOURCE_STEP_AWAITED; }
static bool q3_qvm_admit_client(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel,
    const char *userinfo, qa_app_sim_player_admission *out) {
  (void)sim; (void)client; (void)travel; (void)userinfo; (void)out;
  qa_raise("Error", 0, 0, "Q3 guest admission requires the awaited native network lifecycle");
}
static const char *q3_qvm_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) return st_keep_text(NULL);
  const char *value = qa_app_q3_server_state_get_userinfo(qa_app_q3_qvm_server_game_state(sim->source.qvm), client.slot);
  return st_keep_text(value ? st_dup(value) : NULL);
}
static void q3_qvm_store_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, const char *userinfo) {
  (void)actor;
  qa_app_q3_server_state_set_userinfo(qa_app_q3_qvm_server_game_state(sim->source.qvm), client.slot, userinfo);
}
static void q3_qvm_update_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo, const char *previous) {
  (void)previous;
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  const qa_app_q3_player *found = NULL;
  for (size_t i = 0; i < total; i++) if (qa_same_actor(players[i].actor, actor)) { found = &players[i]; break; }
  if (!found) qa_raise("Error", 0, 0, "Q3 guest userinfo has no admitted client");
  qa_app_q3_qvm_server_game_userinfo(sim->source.qvm, *found, userinfo);
}
static bool q3_qvm_notarget(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) {
  return sim->source.qvm_combat != NULL && qa_qvm_combat_bindings_notarget(sim->source.qvm_combat, actor, out);
}
static const qa_client_id *q3_qvm_connected_clients(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_client_id storage[64];
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  for (size_t i = 0; i < total; i++) storage[i] = players[i].client;
  *out_count = total;
  return storage;
}
static const qa_actor_id *q3_qvm_player_actors(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_actor_id storage[64];
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  for (size_t i = 0; i < total; i++) storage[i] = players[i].actor;
  *out_count = total;
  return storage;
}
static bool q3_qvm_q3_game_type(qa_app_shared_simulation *sim, double *out) {
  *out = qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(sim->source.qvm)), "g_gametype");
  return true;
}
static double q3_qvm_equipment_firing_delay(qa_app_shared_simulation *sim, qa_actor_id actor, double milliseconds, double persistent) {
  return persistent == QA_Q3_PW_SCOUT || persistent == QA_Q3_PW_AMMOREGEN ? qa_q3_weapon_delay(milliseconds, persistent, false)
      : qa_qvm_primary_weapons_weapon_delay(qa_app_shared_sim_require_qvm_weapons(sim), actor, milliseconds);
}
static qa_cvar_registry *q3_qvm_cvars(qa_app_shared_simulation *sim) { return qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(sim->source.qvm)); }
static bool q3_qvm_guest_weapon_animation(qa_app_shared_simulation *sim, qa_actor_id actor, const char *priority, bool melee) {
  if (qa_ss_is(priority, "attack")) qa_qvm_primary_weapons_attack_animation(qa_app_shared_sim_require_qvm_weapons(sim), actor, melee);
  return true;
}
static int q3_qvm_client_of(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id *out) {
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  for (size_t i = 0; i < total; i++) if (qa_same_actor(players[i].actor, actor)) { *out = players[i].client; return 1; }
  return 0;
}
static bool q3_qvm_angle_delta(qa_app_shared_simulation *sim, qa_actor_id actor, qa_client_id client, double out[3]) {
  (void)actor;
  qa_q3_player_state guest; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(sim->source.qvm), client.slot, &guest, stats, persistent, powerups, ammo);
  memcpy(out, guest.delta_angle_words.values, 3 * sizeof *out);
  return true;
}
static const qa_item_id *q3_qvm_guest_grant_exclusions(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_item_id *storage; static __thread size_t capacity;
  size_t count = 0, native_count = 0;
  const qa_q3_guest_weapon *weapons = qa_q3_guest_catalog_weapons(sim->source.qvm_catalog, &native_count);
  storage = st_grow(storage, &capacity, native_count * 2 + 1, sizeof *storage);
  for (size_t i = 0; i < native_count; i++) {
    storage[count++] = weapons[i].item;
    if (weapons[i].ammo != NULL) storage[count++] = weapons[i].ammo;
  }
  *out_count = count;
  return storage;
}
static bool q3_qvm_weapon_visible(qa_app_shared_simulation *sim, qa_actor_id actor, bool *out) { (void)sim; (void)actor; *out = true; return true; }
static void q3_qvm_assert_save_ready(qa_app_shared_simulation *sim) { qa_app_sim_simulation_events_assert_output_consumed(sim->events); }
static const qa_guest_checkpoint *q3_qvm_checkpoint_guests(qa_app_shared_simulation *sim, size_t *out_count) {
  *out_count = 1;
  return (const qa_guest_checkpoint *)qa_app_q3_qvm_server_game_checkpoint(sim->source.qvm);
}
static qa_qvm_combat_bindings *q3_qvm_legacy_combat(qa_app_shared_simulation *sim) { return sim->source.qvm_combat; }
static void q3_qvm_require_inventory_projection(qa_app_shared_simulation *sim, const char *kind) {
  (void)kind;
  if (sim->source.qvm_inventory == NULL) qa_raise("Error", 0, 0, "Saved QVM inventory requires qualified source storage");
}
static bool q3_qvm_inventory_coverage(qa_app_shared_simulation *sim, bool native, bool qvm, qa_app_source_inventory_coverage *out) {
  (void)native;
  if (qvm || sim->source.qvm_inventory == NULL) return false;
  size_t owned_count = 0;
  const qa_owned_actor *owned = qa_session_actor_registry_owned_by(sim->actors, sim->recipe.map.entities.provider, &owned_count);
  qa_owned_actor *actors = qa_app_shared_sim_keep(sim, (owned_count ? owned_count : 1) * sizeof *actors);
  size_t count = 0;
  qa_app_q3_guest_records *records = qa_app_q3_qvm_server_game_records(sim->source.qvm);
  double clients = qa_qvm_game_data_get_num_clients(qa_app_q3_guest_records_data(records));
  for (size_t i = 0; i < owned_count; i++) {
    double slot;
    if (qa_app_q3_guest_records_slot(records, owned[i].id, &slot) && slot < clients && qa_shared_inventory_table_has(sim->inventory, owned[i].id))
      actors[count++] = owned[i];
  }
  *out = (qa_app_source_inventory_coverage){actors, count, true};
  return true;
}
static bool q3_qvm_proxy_allocate(qa_app_shared_simulation *sim, double *slot) {
  if (sim->source.qvm_combat == NULL) return false;
  *slot = qa_qvm_combat_bindings_spawn_proxy_entity(sim->source.qvm_combat);
  return true;
}
static void q3_qvm_proxy_bind(qa_app_shared_simulation *sim, double slot, qa_owned_actor actor) {
  if (sim->source.qvm_combat == NULL) return;
  qa_app_q3_guest_records_register_foreign(qa_app_q3_qvm_server_game_records(sim->source.qvm), slot, actor);
  qa_qvm_combat_bindings_bind_proxy(sim->source.qvm_combat, slot, actor);
}
static bool q3_qvm_monster_proxy(qa_app_shared_simulation *sim, qa_app_source_monster_proxy *out) {
  (void)sim;
  *out = (qa_app_source_monster_proxy){q3_qvm_proxy_allocate, q3_qvm_proxy_bind};
  return true;
}
static qa_app_source_strategy q3_qvm_strategy(void) {
  qa_app_source_strategy s = loaded_strategy();
  s.kind = "q3-qvm";
  s.team_game = q3_qvm_team_game;
  s.handoff = qa_ss_native_handoff;
  s.weapon_damage_factor = qa_ss_q3_qvm_weapon_damage_factor;
  s.weapon_delay = qa_ss_q3_qvm_weapon_delay;
  s.equipment_delay = qa_ss_q3_qvm_equipment_delay;
  s.keeps_angle_deltas = loading_true;
  s.renders_own_intermission = loading_true;
  s.runs_q3_powerups = loading_true;
  s.projects_monsters = loading_true;
  s.uses_guest_monsters = loading_true;
  s.has_guest_ui = loading_true;
  s.guest_ui = qa_ss_q3_qvm_guest_ui;
  s.admits_guest_monsters = loading_true;
  s.as_q3_qvm = own_source;
  s.userinfo = q3_qvm_userinfo;
  s.store_userinfo = q3_qvm_store_userinfo;
  s.update_userinfo = q3_qvm_update_userinfo;
  s.admit_client = q3_qvm_admit_client;
  s.step_route = q3_qvm_step_route;
  s.notarget = q3_qvm_notarget;
  s.connected_clients = q3_qvm_connected_clients;
  s.player_actors = q3_qvm_player_actors;
  s.selected_player = qa_ss_q3_qvm_selected_player;
  s.q3_game_type = q3_qvm_q3_game_type;
  s.has_equipment_effects = loading_true;
  s.equipment_firing_delay = q3_qvm_equipment_firing_delay;
  s.owns_supply = loading_true;
  s.identity_supply = loading_true;
  s.animates_weapon_fire = loading_true;
  s.cvars = q3_qvm_cvars;
  s.teleport_spawn = qa_ss_q3_qvm_teleport_spawn;
  s.drop_objectives = qa_ss_q3_qvm_drop_objectives;
  s.original_player = qa_ss_q3_qvm_original_player;
  s.set_max_health = qa_ss_q3_qvm_set_max_health;
  s.equipment_suppression = native_equipment_suppression;
  s.owns_equipment_players = loading_true;
  s.player_available = qa_ss_q3_qvm_player_available;
  s.teleport_bits = qa_ss_q3_qvm_teleport_bits;
  s.equipment_powerups = qa_ss_q3_qvm_equipment_powerups;
  s.guest_weapon_animation = q3_qvm_guest_weapon_animation;
  s.server_cvars = q3_qvm_cvars;
  s.client_of = q3_qvm_client_of;
  s.check_output_claim = qa_ss_q3_qvm_check_output_claim;
  s.guest_grounded = qa_ss_q3_qvm_guest_grounded;
  s.guest_client_view = qa_ss_q3_qvm_guest_client_view;
  s.outputs_suspended = qa_ss_q3_qvm_outputs_suspended;
  s.canonical_team = qa_ss_q3_qvm_canonical_team;
  s.declares_match = qa_ss_q3_qvm_declares_match;
  s.match_score = qa_ss_q3_qvm_match_score;
  s.set_match_score = qa_ss_q3_qvm_set_match_score;
  s.set_match_team = qa_ss_q3_qvm_set_match_team;
  s.angle_delta = q3_qvm_angle_delta;
  s.guest_grant_exclusions = q3_qvm_guest_grant_exclusions;
  s.guest_view = qa_ss_q3_qvm_guest_view;
  s.guest_weapon_views = loading_true;
  s.weapon_visible = q3_qvm_weapon_visible;
  s.guest_bob_cycle = qa_ss_q3_qvm_guest_bob_cycle;
  s.travel_source = qa_ss_shared_travel_source;
  s.travelers = qa_ss_q3_qvm_travelers;
  s.travel_player_state = qa_ss_shared_travel_player_state;
  s.guest_health = qa_ss_q3_qvm_guest_health;
  s.set_guest_health = qa_ss_q3_qvm_set_guest_health;
  s.guest_travel_players = qa_ss_q3_qvm_guest_travel_players;
  s.assert_save_ready = q3_qvm_assert_save_ready;
  s.checkpoint_guests = q3_qvm_checkpoint_guests;
  s.checkpoint_extras = qa_ss_q3_qvm_checkpoint_extras;
  s.expected_declaration = qa_app_shared_sim_qvm_declaration;
  s.legacy_combat = q3_qvm_legacy_combat;
  s.require_inventory_projection = q3_qvm_require_inventory_projection;
  s.inventory_coverage = q3_qvm_inventory_coverage;
  s.monster_proxy = q3_qvm_monster_proxy;
  return s;
}

static qa_app_source_strategy st_tables[7];
static void st_build_tables(void) {
  st_tables[QA_SS_SOURCE_LOADING] = loading_strategy();
  st_tables[QA_SS_SOURCE_QUAKEC] = quakec_strategy();
  st_tables[QA_SS_SOURCE_Q1] = q1_strategy();
  st_tables[QA_SS_SOURCE_Q3] = q3_strategy();
  st_tables[QA_SS_SOURCE_Q3_QVM] = q3_qvm_strategy();
  st_tables[QA_SS_SOURCE_Q2_NATIVE] = q2_native_strategy();
  st_tables[QA_SS_SOURCE_Q2] = q2_strategy();
}
const qa_app_source_strategy *qa_app_create_source_strategy(const qa_app_shared_simulation *sim) {
  static pthread_once_t once = PTHREAD_ONCE_INIT;
  pthread_once(&once, st_build_tables);
  switch (sim->source.kind) {
    case QA_SS_SOURCE_LOADING: case QA_SS_SOURCE_QUAKEC: case QA_SS_SOURCE_Q1: case QA_SS_SOURCE_Q3:
    case QA_SS_SOURCE_Q3_QVM: case QA_SS_SOURCE_Q2_NATIVE: case QA_SS_SOURCE_Q2:
      return &st_tables[sim->source.kind];
  }
  qa_range_error("Unknown simulation source kind");
}
