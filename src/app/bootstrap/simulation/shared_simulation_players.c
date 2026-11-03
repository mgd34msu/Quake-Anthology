/* Port of src/app/bootstrap/simulation/runtime.ts, SharedSimulation phase
 * B3 region: players, admission, characters, views, userinfo, commands,
 * travel, checkpoint, restore and mods (docs/runtime_layout.md, group B3).
 * Every method keeps its TS name in a comment and every branch in TS order.
 * TS throws are qa_raise("Error", ...) with the TS message.
 *
 * Ownership: arrays a TS method returns are thread-local scratch that stays
 * valid until the next call of the same method (the pattern of players()).
 * TS userinfo Maps (q2Userinfo) are ss_info lists freed by their user.
 * GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/app/bootstrap/simulation/q3/guest_records.h"
#include "qa/content/q1/base/map_entities.h"
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_player.h"
#include "qa/app/bootstrap/simulation/quakec_character_animation.h"
#include "qa/content/q2/base/player/view.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q1/quakeworld.h"
#include "qa/movement/q3/animation.h"
#include "qa/app/bootstrap/simulation/player_input_application.h"
#include "qa/app/bootstrap/simulation/player_movement.h"
#include "qa/app/bootstrap/simulation/quakec_client_adapter.h"
#include "qa/content/q3/team_arena/client_effects.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/content/composition/q1/give.h"
#include "qa/content/q2/base/player/commands.h"
#include "qa/contracts/source_items.h"
#include "qa/app/bootstrap/simulation/q3/player_state.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/app/bootstrap/simulation/q3/command_policy.h"
#include "qa/network/common/js_number.h"
#include "qa/app/bootstrap/simulation/player_jump.h"

static noreturn void ss_fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *ss_grow(void *ptr, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return ptr;
  size_t next = *capacity ? *capacity * 2 : 8;
  while (next < need) next *= 2;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
static char *ss_dup(const char *text) {
  char *copy = qa_ss_strdup(text);
  if (!copy) qa_range_error("Out of memory");
  return copy;
}
static qa_shared_body_table *ss_bodies(qa_app_shared_simulation *sim) { return qa_app_sim_shared_physics_bodies(sim->physics); }
static double ss_time_seconds(const qa_app_shared_simulation *sim) { return qa_ss_seconds(sim->source_frame.time); }
static bool ss_live(qa_app_shared_simulation *sim, qa_actor_id actor) { return qa_session_actor_registry_is_live(sim->actors, actor); }
/* TS this.combat.read(actor)?.health ?? fallback. */
static double ss_health_or(qa_app_shared_simulation *sim, qa_actor_id actor, double fallback) {
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  return combat.has_value ? combat.value.health : fallback;
}
static bool ss_quakec_kind(qa_app_shared_simulation *sim, const char *kind) {
  return (sim->strategy->as_quakec(sim) != NULL) && qa_ss_is(qa_app_sim_quakec_source_kind(sim->source.quakec), kind);
}
static qa_q2_entity *ss_q2_entity(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_game_services *game = sim->source.product->services;
  return game->entity(game->ctx, &actor);
}
static qa_q2_base_player_state *ss_q2_state(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return qa_q2_players_get_state(sim->source.product->players, actor);
}
static void ss_release(qa_cleanup *node, void *owned) { qa_cleanup_pop(node); free(owned); }
static void ss_cvar_set(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}

/* ---- TS q2Userinfo(source): ordered Map, first key wins. ---- */
typedef qa_ss_info ss_info;
static void ss_info_free(ss_info *info) {
  for (size_t i = 0; i < info->count; i++) { free((char *)info->items[i].name); free((char *)info->items[i].value); }
  free(info->items); memset(info, 0, sizeof *info);
}
static const char *ss_info_get(const ss_info *info, const char *name) {
  for (size_t i = 0; i < info->count; i++) if (strcmp(info->items[i].name, name) == 0) return info->items[i].value;
  return NULL;
}
/* Map.set: replaces in place or appends. */
static void ss_info_set(ss_info *info, const char *name, const char *value) {
  for (size_t i = 0; i < info->count; i++)
    if (strcmp(info->items[i].name, name) == 0) { free((char *)info->items[i].value); info->items[i].value = ss_dup(value); return; }
  info->items = ss_grow(info->items, &info->capacity, info->count + 1, sizeof *info->items);
  info->items[info->count++] = (qa_app_sim_cvar_name_value){ss_dup(name), ss_dup(value)};
}
static ss_info ss_q2_userinfo(const char *source) {
  ss_info info = {0};
  const char *text = source[0] == '\\' ? source + 1 : source;
  /* split("\\"): tokens alternate key, value; a trailing odd key is dropped. */
  size_t token_count = 1;
  for (const char *c = text; *c; c++) if (*c == '\\') token_count++;
  const char *cursor = text;
  for (size_t index = 0; index + 1 < token_count; index += 2) {
    const char *key_end = strchr(cursor, '\\');
    const char *value = key_end + 1, *value_end = strchr(value, '\\');
    size_t key_length = (size_t)(key_end - cursor), value_length = value_end ? (size_t)(value_end - value) : strlen(value);
    char *key = malloc(key_length + 1), *copy = malloc(value_length + 1);
    if (!key || !copy) qa_range_error("Out of memory");
    memcpy(key, cursor, key_length); key[key_length] = '\0';
    memcpy(copy, value, value_length); copy[value_length] = '\0';
    if (!ss_info_get(&info, key)) {
      info.items = ss_grow(info.items, &info.capacity, info.count + 1, sizeof *info.items);
      info.items[info.count++] = (qa_app_sim_cvar_name_value){key, copy};
    } else { free(key); free(copy); }
    if (!value_end) break;
    cursor = value_end + 1;
  }
  return info;
}
qa_ss_info qa_ss_q2_userinfo(const char *source) { return ss_q2_userinfo(source); }
void qa_ss_info_set(qa_ss_info *info, const char *name, const char *value) { ss_info_set(info, name, value); }
void qa_ss_info_free(qa_ss_info *info) { ss_info_free(info); }
/* [...info].map(([key, value]) => `\\${key}\\${value}`).join(""), malloc'd. */
static char *ss_info_text(const qa_app_sim_cvar_name_value *items, size_t count) {
  size_t length = 1;
  for (size_t i = 0; i < count; i++) length += strlen(items[i].name) + strlen(items[i].value) + 2;
  char *text = malloc(length);
  if (!text) qa_range_error("Out of memory");
  char *cursor = text;
  for (size_t i = 0; i < count; i++) cursor += sprintf(cursor, "\\%s\\%s", items[i].name, items[i].value);
  *cursor = '\0';
  return text;
}
/* previous.size === values.size && [...values].every(([n, v]) => previous.get(n) === v). */
static bool ss_info_same(const qa_app_sim_cvar_name_value *previous, size_t previous_count, const ss_info *values) {
  if (previous_count != values->count) return false;
  for (size_t i = 0; i < values->count; i++) {
    const char *found = NULL;
    for (size_t j = 0; j < previous_count; j++) if (strcmp(previous[j].name, values->items[i].name) == 0) { found = previous[j].value; break; }
    if (!found || strcmp(found, values->items[i].value) != 0) return false;
  }
  return true;
}

char *qa_ss_info_text(const qa_app_sim_cvar_name_value *items, size_t count) { return ss_info_text(items, count); }
bool qa_ss_info_same(const qa_app_sim_cvar_name_value *previous, size_t previous_count, const qa_ss_info *values) {
  return ss_info_same(previous, previous_count, values);
}
const char *qa_ss_info_get(const qa_ss_info *info, const char *name) { return ss_info_get(info, name); }
/* TS host.userinfoChanged(actor, previous): notify when the stored userinfo moved away from `previous` (NULL = TS null). */
void qa_app_shared_sim_userinfo_changed(qa_app_shared_simulation *sim, qa_actor_id actor, const char *previous) {
  const char *current = qa_app_shared_sim_source_player_userinfo(sim, actor);
  bool changed = (current == NULL) != (previous == NULL) || (current && strcmp(current, previous) != 0);
  if (changed) qa_app_shared_sim_notify_client_event(sim, "userinfo", actor);
}

/* ---- modClientPresentationSources / modPresentationSources (481-482) ---- */
const qa_active_mod_client_presentation *qa_app_shared_sim_mod_client_presentation_sources(qa_app_shared_simulation *sim, size_t *out_count) {
  if (!sim->mod_owner) { *out_count = 0; return NULL; }
  return qa_session_mods_client_presentation_sources(sim->mod_owner, out_count);
}
const qa_active_mod_presentation *qa_app_shared_sim_mod_presentation_sources(qa_app_shared_simulation *sim, size_t *out_count) {
  if (!sim->mod_owner) { *out_count = 0; return NULL; }
  return qa_session_mods_presentation_sources(sim->mod_owner, out_count);
}

/* ---- botEntity (4091-4097) ---- */
bool qa_app_shared_sim_bot_entity(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_bot_entity *out) {
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &actor);
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1) {
    const qa_q1_actor *entity = entry->q1_entity;
    *out = (qa_app_sim_bot_entity){entity->classname, entity->model, entity->targetname, ss_health_or(sim, actor, 0),
        entity->spawnflags, entity->model[0] == '\0'};
    return true;
  }
  if (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q2) {
    const qa_q2_entity *entity = entry->q2_entity;
    *out = (qa_app_sim_bot_entity){entity->classname, entity->model, entity->targetname, ss_health_or(sim, actor, 0),
        entity->spawnflags, !entity->visible};
    return true;
  }
  return false;
}

/* ---- takeModClientDrops (6537): drainAll; the array is the caller's (free()). ---- */
qa_ss_mod_client_drop *qa_app_shared_sim_take_mod_client_drops(qa_app_shared_simulation *sim, size_t *out_count) {
  return qa_event_ring_drain_all(&sim->mod_client_drops, out_count);
}
/* ---- rememberModClientCommand (6538) ---- */
void qa_app_shared_sim_remember_mod_client_command(qa_app_shared_simulation *sim, const qa_actor_command *input) {
  qa_mod_client_command_receipt receipt = {*input, sim->source_frame.time};
  qa_ss_map_set(&sim->mod_client_commands, &input->actor, &receipt);
}
/* ---- requireModClient (6539-6544) ---- */
qa_actor_id qa_app_shared_sim_require_mod_client(qa_app_shared_simulation *sim, qa_client_id client) {
  qa_app_shared_sim_assert_open(sim);
  qa_actor_id actor;
  if (!sim->mod_clients.actor(sim->mod_clients.ctx, client, &actor)) ss_fail("Component client is no longer admitted");
  return actor;
}
/* ---- notifyClientEvent (6545-6556) ---- */
void qa_app_shared_sim_notify_client_event(qa_app_shared_simulation *sim, const char *kind, qa_actor_id actor) {
  qa_client_id client;
  if (!ss_live(sim, actor) || !qa_app_shared_sim_player_client(sim, actor, &client)) return;
  bool admitted = qa_ss_is(kind, "admitted");
  if (admitted) {
    if (qa_ss_map_has(&sim->mod_client_admissions, &actor)) return;
    qa_ss_map_set(&sim->mod_client_admissions, &actor, NULL);
  }
  qa_mod_client_event event = {kind, {client, actor}};
  /* for (const listener of [...this.modClientListeners]): a snapshot. */
  size_t count = sim->mod_client_listener_count;
  qa_ss_mod_client_listener *listeners = malloc((count ? count : 1) * sizeof *listeners);
  if (!listeners) qa_range_error("Out of memory");
  if (count) memcpy(listeners, sim->mod_client_listeners, count * sizeof *listeners);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, listeners);
  for (size_t i = 0; i < count; i++) listeners[i].listener(listeners[i].ctx, event);
  qa_cleanup_pop(&cleanup); free(listeners);
  if (admitted && ss_live(sim, actor)) qa_app_shared_sim_restore_native_inventory_cursors(sim, &actor);
  if (qa_ss_is(kind, "disconnecting")) {
    qa_ss_map_delete(&sim->mod_client_admissions, &actor);
    qa_ss_map_delete(&sim->mod_client_commands, &actor);
  }
}
/* ---- observeClientCommand (6557-6583) ---- */
static double ss_word(double value, double delta) { return (double)((qa_js_to_int32(value) + qa_js_to_int32(delta)) & 65535); }
void qa_app_shared_sim_observe_client_command(qa_app_shared_simulation *sim, const qa_actor_command *input) {
  qa_client_id client;
  bool has_client = qa_app_shared_sim_player_client(sim, input->actor, &client);
  if (!has_client || !ss_live(sim, input->actor)
      || (input->source.kind != QA_COMMAND_SOURCE_BOT
          && (!qa_app_shared_sim_owns_client(sim, input->source.client) || !qa_same_client(client, input->source.client))))
    ss_fail("Component input does not belong to an admitted client");
  const qa_mod_client_command_receipt *previous = qa_ss_map_get(&sim->mod_client_commands, &input->actor);
  if (previous && input->sequence < previous->input.sequence) return;
  qa_user_command command = input->command;
  qa_actor_command remembered = *input;
  remembered.angle_space = "absolute";
  if ((input->angle_space && qa_ss_is(input->angle_space, "absolute"))
      || (input->source.kind == QA_COMMAND_SOURCE_LOCAL_SEAT && command.kind != QA_USER_COMMAND_Q3)
      || command.kind == QA_USER_COMMAND_Q1 || command.kind == QA_USER_COMMAND_QW) {
    qa_app_shared_sim_remember_mod_client_command(sim, &remembered); return;
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, input->actor);
  qa_movement_state state; memset(&state, 0, sizeof state);
  bool has_state = sim->strategy->command_movement(sim, client, &state);
  if (!has_state && player) { has_state = true; state = qa_app_sim_movement_player_read_state(player); }
  if (command.kind == QA_USER_COMMAND_Q2 && has_state && state.kind == QA_MOVEMENT_STATE_Q2) {
    const double *delta = state.as.q2.delta_angle_shorts.values;
    double *shorts = remembered.command.as.q2.angle_shorts.values;
    for (int i = 0; i < 3; i++) shorts[i] = ss_word(command.as.q2.angle_shorts.values[i], delta[i]);
    qa_app_shared_sim_remember_mod_client_command(sim, &remembered);
  } else if (command.kind == QA_USER_COMMAND_Q2_RERELEASE && has_state && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    remembered.command.as.q2_rerelease.angles = qa_ss_add(command.as.q2_rerelease.angles, state.as.q2_rerelease.delta_angles);
    qa_app_shared_sim_remember_mod_client_command(sim, &remembered);
  } else if (command.kind == QA_USER_COMMAND_Q3) {
    double delta[3] = {0, 0, 0};
    /* this.strategy.angleDelta(actor, client) ?? (state?.kind === "q3" ? state.deltaAngleWords : undefined) */
    bool has_delta = sim->strategy->angle_delta(sim, input->actor, client, delta);
    if (!has_delta && has_state && state.kind == QA_MOVEMENT_STATE_Q3) { has_delta = true; memcpy(delta, state.as.q3.delta_angle_words.values, sizeof delta); }
    if (!has_delta) ss_fail("Q3 component input has no authoritative angle delta");
    double *words = remembered.command.as.q3.angle_words.values;
    for (int i = 0; i < 3; i++) words[i] = ss_word(command.as.q3.angle_words.values[i], delta[i]);
    qa_app_shared_sim_remember_mod_client_command(sim, &remembered);
  } else ss_fail("Component input has no matching source movement state");
}

/* ---- storePlayerUserinfo (6664-6678) ---- */
void qa_app_shared_sim_store_player_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *value) {
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(sim, actor, &client)) ss_fail("Component userinfo has no admitted client");
  sim->strategy->store_userinfo(sim, actor, client, value);
}
/* ---- sourcePlayerName (6679) ---- */
const char *qa_app_shared_sim_source_player_name(qa_app_shared_simulation *sim, qa_actor_id actor) {
  static __thread char name[256];
  const char *userinfo = qa_app_shared_sim_source_player_userinfo(sim, actor);
  ss_info info = ss_q2_userinfo(userinfo ? userinfo : "");
  const char *found = ss_info_get(&info, "name");
  snprintf(name, sizeof name, "%s", found ? found : "unconnected");
  ss_info_free(&info);
  return name;
}
/* ---- get q1Paused (6680) ---- */
bool qa_app_shared_sim_q1_paused(qa_app_shared_simulation *sim) { return sim->q1_pause_state; }
/* ---- toggleQ1Pause (6681-6691): actor NULL = TS null. ---- */
const char *qa_app_shared_sim_toggle_q1_pause(qa_app_shared_simulation *sim, const qa_actor_id *actor) {
  static __thread char message[320];
  qa_cvar_registry *cvars = NULL;
  if (!sim->strategy->pause(sim, &cvars)) ss_fail("Pause requires a Quake source server");
  if (actor) {
    qa_cvar_read pausable;
    if (qa_cvar_registry_find(cvars, "pausable", &pausable)) {
      bool zero = pausable.numeric_value == 0;
      qa_cvar_read_free(&pausable);
      if (zero) return "Pause not allowed.\n";
    }
  }
  if (actor && sim->strategy->pause_is_spectator(sim, *actor)) return "Spectators can not pause.\n";
  sim->q1_pause_state = !sim->q1_pause_state;
  const char *name = actor ? qa_app_shared_sim_source_player_name(sim, *actor) : "Server";
  snprintf(message, sizeof message, "%s %s the game\n", name, sim->q1_pause_state ? "paused" : "unpaused");
  return message;
}
/* ---- sourcePlayerUserinfo (6692-6706): NULL = TS null; scratch until the next call. ---- */
const char *qa_app_shared_sim_source_player_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor) {
  return sim->strategy->userinfo(sim, actor);
}
/* ---- updatePlayerUserinfo. The Q3 QVM strategy returns the guest's userinfo completion (synchronous in C). ---- */
void qa_app_shared_sim_update_player_userinfo(qa_app_shared_simulation *sim, qa_actor_id actor, const char *userinfo) {
  const char *previous_text = qa_app_shared_sim_source_player_userinfo(sim, actor);
  char *previous_userinfo = previous_text ? ss_dup(previous_text) : NULL;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, previous_userinfo);
  if (!(previous_userinfo && strcmp(previous_userinfo, userinfo) == 0)) sim->strategy->update_userinfo(sim, actor, userinfo, previous_userinfo);
  ss_release(&cleanup, previous_userinfo);
}

/* ---- movementPlayer (7427) ---- */
qa_app_sim_movement_player *qa_app_shared_sim_movement_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) return NULL;
  return qa_app_shared_sim_movement_player_of(sim, owned);
}
/* ---- q2PlayerView (7431) ---- */
bool qa_app_shared_sim_q2_player_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2_base_player_view *out) {
  qa_q2_base_player_view *view = qa_ss_map_get(&sim->q2_views, &actor);
  if (!view) return false;
  *out = *view;
  return true;
}
/* ---- setWorldGravity (7448-7456) ---- */
void qa_app_shared_sim_set_world_gravity(qa_app_shared_simulation *sim, double gravity) {
  qa_app_sim_shared_physics_set_world_gravity(sim->physics, gravity);
  if (sim->q2_server_registry) { char text[32]; ss_cvar_set(sim->q2_server_registry, "sv_gravity", qa_ss_number_text(gravity, text)); }
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    player->world_gravity = gravity;
    double value = trunc(gravity * player->gravity_multiplier);
    if (player->state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.gravity = value;
    else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.gravity = value;
    else if (player->state.kind == QA_MOVEMENT_STATE_Q3) player->state.as.q3.gravity = value;
  }
}

/* ---- shared helpers of the movement members ---- */
static qa_body_state ss_require_body(qa_app_shared_simulation *sim, qa_actor_id actor, const char *message) {
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor, &body)) ss_fail("%s", message);
  return body;
}
static void ss_set_traits(qa_app_shared_simulation *sim, qa_owned_actor actor, bool has_can_take_damage, bool can_take_damage,
    bool has_invulnerable, bool invulnerable) {
  qa_combat_trait_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_can_take_damage = has_can_take_damage; changes.can_take_damage = can_take_damage;
  changes.has_invulnerable = has_invulnerable; changes.invulnerable = invulnerable;
  qa_gameplay_authority_set_traits(sim->combat, actor, changes);
}
/* this.events.emit(this.recipe.map.entities.content, { kind: "view-reset", reason, actor, angles }). */
static void ss_view_reset(qa_app_shared_simulation *sim, const char *reason, qa_actor_id actor, qa_vec3 angles) {
  qa_app_sim_source_event_input event; memset(&event, 0, sizeof event);
  event.kind = "view-reset";
  event.view_reset = (qa_app_sim_view_reset_event){reason, actor, angles};
  qa_app_shared_sim_emit(sim, sim->recipe.map.entities.content, event, NULL, NULL);
}
static qa_q3_character_actor *ss_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &actor);
  return character ? *character : NULL;
}
static double ss_trunc8(double value) { return trunc(value * 8); }

/* ---- setPlayerMovement (4842-4893) ---- */
void qa_app_shared_sim_set_player_movement_link(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2_player_movement_change change, bool link) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_body_state body = ss_require_body(sim, actor, "Player has no body");
  qa_movement_state state = qa_app_sim_movement_player_read_state(player);
  bool spawn = qa_ss_is(change.kind, "spawn"), freeze = qa_ss_is(change.kind, "freeze");
  if (qa_ss_is(change.kind, "teleport")) {
    if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
    if (sim->selected_q3_source) qa_app_q3_selected_source_release_hook(sim->selected_q3_source, actor);
  }
  if (qa_ss_is(change.kind, "noclip")) {
    qa_app_sim_movement_player_set_flight(player, false);
    player->state = state;
    if (state.kind == QA_MOVEMENT_STATE_Q1) player->state.as.q1.move_type = change.enabled ? 8 : 3;
    else if (state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.type = change.enabled ? 1 : 0;
    else if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.type = change.enabled ? 1 : 0;
    else if (state.kind == QA_MOVEMENT_STATE_Q3) player->state.as.q3.movement_type = change.enabled ? 1 : 0;
    else player->state.as.qw.spectator = change.enabled ? 1 : 0;
    return;
  }
  player->view_angles = change.angles;
  if (spawn) {
    qa_app_sim_movement_player_set_flight(player, false); player->has_cutscene = false;
    player->bounds = player->standing_bounds; player->has_body_shape_base = false;
    player->view_height = player->character == QA_GAME_FAMILY_Q3 ? 26 : 22;
  }
  player->intermission = freeze;
  if (freeze) ss_set_traits(sim, player->actor, true, false, false, false);
  qa_vec3 velocity = freeze ? qa_ss_zero : change.velocity;
  qa_body_state written = body;
  written.origin = change.origin; written.velocity = velocity; written.angles = change.angles; written.ground = NULL;
  qa_shared_body_table_write(ss_bodies(sim), player->actor, written);
  memset(&player->ground, 0, sizeof player->ground); player->ground.kind = QA_TRACE_HIT_NONE;
  qa_movement_state next = state;
  if (state.kind == QA_MOVEMENT_STATE_Q1) {
    qa_q1_movement_state *q1 = &next.as.q1;
    q1->origin = change.origin; q1->old_origin = change.origin; q1->velocity = velocity; q1->view_angles = change.angles;
    q1->angles = change.angles; q1->flags = qa_js_bit_and(state.as.q1.flags, ~512);
    q1->move_type = spawn ? 3 : freeze ? 0 : state.as.q1.move_type;
    memset(&q1->ground, 0, sizeof q1->ground); q1->ground.kind = QA_TRACE_HIT_NONE; q1->fix_angle = false;
    q1->teleport_time_seconds = ss_time_seconds(sim) + (freeze ? 0 : change.hold_milliseconds / 1000);
  } else if (state.kind == QA_MOVEMENT_STATE_Q2) {
    qa_q2_movement_state *q2 = &next.as.q2;
    q2->origin_eighths = (qa_integer_triple){{ss_trunc8(change.origin.x), ss_trunc8(change.origin.y), ss_trunc8(change.origin.z)}};
    q2->velocity_eighths = (qa_integer_triple){{ss_trunc8(velocity.x), ss_trunc8(velocity.y), ss_trunc8(velocity.z)}};
    q2->flags = freeze ? state.as.q2.flags
        : qa_js_bit_or(spawn ? 0 : qa_js_bit_and(state.as.q2.flags, ~56), change.hold_milliseconds > 0 ? 32 : 0);
    q2->time_eight_milliseconds = freeze ? 0 : trunc(change.hold_milliseconds / 8);
    q2->delta_angle_shorts = (qa_angle_words){{0, 0, 0}}; q2->type = freeze ? 4 : 0;
  } else if (state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    qa_q2_rerelease_movement_state *q2 = &next.as.q2_rerelease;
    q2->origin = change.origin; q2->velocity = velocity;
    q2->flags = freeze ? state.as.q2_rerelease.flags
        : qa_js_bit_or(spawn ? 0 : qa_js_bit_and(state.as.q2_rerelease.flags, ~56), change.hold_milliseconds > 0 ? 32 : 0);
    q2->time_milliseconds = freeze ? 0 : change.hold_milliseconds; q2->delta_angles = qa_ss_zero; q2->type = freeze ? 5 : 0;
  } else if (state.kind == QA_MOVEMENT_STATE_Q3) {
    qa_q3_movement_state *q3 = &next.as.q3;
    q3->origin = change.origin; q3->velocity = velocity; q3->view_angles = change.angles;
    if (!sim->strategy->keeps_angle_deltas(sim)) q3->delta_angle_words = (qa_angle_words){{0, 0, 0}};
    q3->movement_type = freeze ? 4 : 0;
    memset(&q3->ground, 0, sizeof q3->ground); q3->ground.kind = QA_TRACE_HIT_NONE;
    q3->movement_flags = freeze ? state.as.q3.movement_flags
        : qa_js_bit_or(qa_js_bit_and(state.as.q3.movement_flags, ~(QA_Q3_FLAG_TIME_LAND | QA_Q3_FLAG_TIME_KNOCKBACK | QA_Q3_FLAG_TIME_WATERJUMP)),
            change.hold_milliseconds > 0 ? QA_Q3_FLAG_TIME_KNOCKBACK : 0);
    q3->movement_time_milliseconds = freeze ? 0 : change.hold_milliseconds;
  } else {
    qa_qw_movement_state *qw = &next.as.qw;
    qw->origin = change.origin; qw->velocity = velocity; qw->angles = change.angles;
    memset(&qw->ground, 0, sizeof qw->ground); qw->ground.kind = QA_TRACE_HIT_NONE;
  }
  player->state = next;
  qa_q3_character_actor *character = ss_q3_character(sim, player->actor);
  if (spawn && character && qa_js_bit_and(qa_q3_character_actor_get_source_flags(character), 1) != 0) {
    qa_body_state current = ss_require_body(sim, actor, "Respawning character has no body");
    current.bounds = player->standing_bounds;
    size_t count;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &count);
    qa_q3_character_actor_spawn(character, (qa_q3_character_spawn){current, qa_q3_initial_combat("100", NULL), entries, count});
    player->bounds = player->standing_bounds; player->has_body_shape_base = false; player->view_height = 26;
    player->animation = qa_q3_character_actor_get_animation(character);
  }
  if (link) qa_shared_body_table_link(ss_bodies(sim), player->actor, NULL);
  ss_view_reset(sim, change.kind, actor, change.angles);
}
void qa_app_shared_sim_set_player_movement(qa_app_shared_simulation *sim, qa_actor_id actor, qa_q2_player_movement_change change) {
  qa_app_shared_sim_set_player_movement_link(sim, actor, change, true);
}

/* ---- killBox (4712-4727) ---- */
void qa_app_shared_sim_kill_box(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_body_state body = ss_require_body(sim, actor.id, "Spawned player has no body");
  size_t count;
  const qa_actor_observation *observed = qa_session_actor_registry_observations(sim->actors, &count);
  qa_actor_observation *candidates = malloc((count ? count : 1) * sizeof *candidates);
  if (!candidates) qa_range_error("Out of memory");
  if (count) memcpy(candidates, observed, count * sizeof *candidates);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, candidates);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id candidate = candidates[i].id;
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, candidate);
    if (qa_same_actor(candidate, actor.id) || !(combat.has_value && combat.value.can_take_damage)) continue;
    qa_body_state other;
    if (!qa_app_shared_sim_body(sim, candidate, &other)
        || body.origin.x + body.bounds.min.x > other.origin.x + other.bounds.max.x || body.origin.x + body.bounds.max.x < other.origin.x + other.bounds.min.x
        || body.origin.y + body.bounds.min.y > other.origin.y + other.bounds.max.y || body.origin.y + body.bounds.max.y < other.origin.y + other.bounds.min.y
        || body.origin.z + body.bounds.min.z > other.origin.z + other.bounds.max.z || body.origin.z + body.bounds.max.z < other.origin.z + other.bounds.min.z) continue;
    qa_damage_request request; memset(&request, 0, sizeof request);
    request.attack.sequence = sim->attack_sequence++; request.attack.time = sim->source_frame.time;
    request.attack.attacker = &actor.id; request.attack.inflictor = &actor.id; request.attack.weapon = NULL;
    request.attack.weapon_provider = sim->weapon_provider.provider; request.attack.combat_provider = sim->recipe.combat.provider;
    request.attack.inventory_provider = sim->recipe.inventory.provider; request.attack.movement_provider = sim->recipe.movement.provider;
    if (sim->strategy->telefrag_cause_q1(sim)) { request.attack.cause.kind = QA_ATTACK_CAUSE_Q1; request.attack.cause.death_type = "telefrag"; }
    else { request.attack.cause.kind = QA_ATTACK_CAUSE_Q2; request.attack.cause.means_of_death = 21; request.attack.cause.damage_flags = 32; }
    request.target = candidate; request.amount = 100000; request.knockback = 0; request.direction = qa_ss_zero;
    request.point = body.origin; request.normal = qa_ss_zero; request.delivery = "direct";
    qa_gameplay_authority_apply(sim->combat, request, NULL, NULL);
  }
  qa_cleanup_pop(&cleanup); free(candidates);
}

/* ---- controlPlayer (7432-7447): kind "cutscene". ---- */
void qa_app_shared_sim_control_player(qa_app_shared_simulation *sim, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_body_state body = ss_require_body(sim, actor, "Controlled player has no body");
  player->has_cutscene = true; player->cutscene_origin = origin; player->cutscene_angles = angles; player->cutscene_view_offset = view_offset;
  player->view_angles = angles; player->view_height = view_offset.z;
  memset(&player->ground, 0, sizeof player->ground); player->ground.kind = QA_TRACE_HIT_NONE;
  qa_movement_state *state = &player->state;
  if (state->kind == QA_MOVEMENT_STATE_Q1) {
    qa_q1_movement_state *q1 = &state->as.q1;
    q1->origin = origin; q1->velocity = qa_ss_zero; q1->angles = angles; q1->view_angles = angles; q1->move_type = 0;
    memset(&q1->ground, 0, sizeof q1->ground); q1->ground.kind = QA_TRACE_HIT_NONE;
  } else if (state->kind == QA_MOVEMENT_STATE_Q2) {
    qa_q2_movement_state *q2 = &state->as.q2;
    q2->type = 4; q2->origin_eighths = (qa_integer_triple){{ss_trunc8(origin.x), ss_trunc8(origin.y), ss_trunc8(origin.z)}};
    q2->velocity_eighths = (qa_integer_triple){{0, 0, 0}};
  } else if (state->kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    state->as.q2_rerelease.type = 5; state->as.q2_rerelease.origin = origin; state->as.q2_rerelease.velocity = qa_ss_zero;
  } else if (state->kind == QA_MOVEMENT_STATE_Q3) {
    qa_q3_movement_state *q3 = &state->as.q3;
    q3->movement_type = 4; q3->origin = origin; q3->velocity = qa_ss_zero; q3->view_angles = angles;
    memset(&q3->ground, 0, sizeof q3->ground); q3->ground.kind = QA_TRACE_HIT_NONE;
  } else { state->as.qw.origin = origin; state->as.qw.velocity = qa_ss_zero; state->as.qw.angles = angles; }
  qa_body_state written = body;
  written.origin = origin; written.angles = angles; written.velocity = qa_ss_zero; written.ground = NULL;
  qa_shared_body_table_write(ss_bodies(sim), player->actor, written);
  ss_set_traits(sim, player->actor, true, false, false, false);
  qa_shared_body_table_link(ss_bodies(sim), player->actor, NULL);
  sim->strategy->cutscene_control(sim, player, angles);
}

/* ---- takeTransitions (8114-8119): scratch until the next call. ---- */
const qa_transition_intent *qa_app_shared_sim_take_transitions(qa_app_shared_simulation *sim, size_t *out_count) {
  static __thread qa_transition_intent *taken;
  static __thread qa_mission_gate **gate_lists; static __thread size_t gate_list_count;
  for (size_t i = 0; i < gate_list_count; i++) free(gate_lists[i]);
  free(gate_lists); gate_lists = NULL; gate_list_count = 0;
  free(taken); taken = NULL;
  size_t gate_count;
  const qa_mission_gate *gates = qa_mod_match_state_gates(sim->source_match, &gate_count);
  size_t count;
  taken = qa_event_ring_take(&sim->transitions, &sim->transitions_cursor, &count);
  qa_event_ring_retire(&sim->transitions);
  gate_lists = calloc(count ? count : 1, sizeof *gate_lists);
  if (!gate_lists) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_transition_intent *intent = &taken[i];
    if (!qa_ss_is(intent->kind, "campaign-level") && !qa_ss_is(intent->kind, "campaign-complete")) continue;
    /* { ...intent, gates: [...intent.gates, ...gates] } */
    qa_mission_gate *merged = malloc((intent->gate_count + gate_count + 1) * sizeof *merged);
    if (!merged) qa_range_error("Out of memory");
    if (intent->gate_count) memcpy(merged, intent->gates, intent->gate_count * sizeof *merged);
    if (gate_count) memcpy(merged + intent->gate_count, gates, gate_count * sizeof *merged);
    intent->gates = merged; intent->gate_count += gate_count;
    gate_lists[gate_list_count++] = merged;
  }
  *out_count = count;
  return taken;
}
/* ---- takeLevelChange (8120): false = TS null; map is the caller's (free()). ---- */
bool qa_app_shared_sim_take_level_change(qa_app_shared_simulation *sim, qa_ss_level_change *out) {
  bool had = sim->has_level_change;
  if (had) {
    out->map = sim->level_change_map; out->server_flags = sim->level_change_server_flags;
    out->has_landmark = sim->has_level_change_landmark; out->landmark = sim->level_change_landmark;
  }
  sim->has_level_change = false; sim->level_change_map = NULL; sim->has_level_change_landmark = false;
  return had;
}
/* ---- assertBotRestoreReady (8121-8124) ---- */
void qa_app_shared_sim_assert_bot_restore_ready(qa_app_shared_simulation *sim) {
  const qa_save_image *restore = sim->options.restore;
  bool saved_bots = false;
  if (restore) for (size_t i = 0; i < restore->provider_count; i++) if (qa_ss_is(restore->providers[i].schema, "world:bots")) saved_bots = true;
  if (saved_bots && qa_app_sim_bot_services_configuration(&sim->bot_services) == NULL)
    ss_fail("Saved bot services must be restored before simulation advances or saves");
}
/* ---- assertCheckpointReady (8125-8140) ---- */
void qa_app_shared_sim_assert_checkpoint_ready(qa_app_shared_simulation *sim) {
  if (sim->pending_shared_restore) qa_shared_world_restore_assert_complete(sim->pending_shared_restore);
  qa_gameplay_authority_assert_idle(sim->combat);
  qa_shared_original_pickup_admission_assert_idle(sim->original_pickups);
  qa_app_shared_sim_assert_open(sim);
  qa_app_shared_sim_assert_bot_restore_ready(sim);
  if (qa_ss_is(sim->source_round_settlement.kind, "active") || qa_ss_is(sim->source_round_settlement.kind, "failed"))
    ss_fail("Save requires completed source round settlement");
  if (sim->stepping || sim->checkpoint_in_progress || qa_event_ring_pending(&sim->transitions, &sim->transitions_cursor) != 0
      || sim->has_level_change) ss_fail("Save requires a completed frame without pending world travel");
  sim->strategy->require_loaded(sim, "The selected source world does not yet expose a complete saved-game checkpoint");
  if (qa_app_shared_sim_foreign_tag(sim) || qa_app_shared_sim_foreign_lmctf(sim) || qa_app_shared_sim_foreign_deathball(sim))
    ss_fail("Saved games do not yet carry the runtime Tag, LMCTF or DeathBall match state");
  if (qa_event_ring_pending(&sim->quake_world_commands, &sim->quake_world_cursor) != 0 || sim->q3_commands.count != 0
      || sim->primary_command_blocks.count != 0) ss_fail("Save requires completed source commands");
  qa_app_presentation_state_assert_local_media_consumed(sim->presentation);
  sim->strategy->assert_save_ready(sim);
}

/* ---- travel helpers ---- */
static const qa_app_sim_travel_player *ss_travel_find(const qa_app_simulation_travel *travel, qa_client_id client) {
  if (!travel) return NULL;
  for (size_t i = 0; i < travel->player_count; i++) if (qa_same_client(travel->players[i].client, client)) return &travel->players[i];
  return NULL;
}
/* ---- sharedTravelState (7967-7979): false = TS null; entries borrow the inventory table. ---- */
bool qa_app_shared_sim_shared_travel_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_ss_shared_carry *out) {
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  double guest_health = 0;
  bool has_guest_health = !combat.has_value && sim->strategy->guest_health(sim, actor, &guest_health);
  if (!combat.has_value && !has_guest_health) return false;
  out->health = combat.has_value ? combat.value.health : has_guest_health ? guest_health : 100;
  if (combat.has_value) out->armor = combat.value.armor;
  else { memset(&out->armor, 0, sizeof out->armor); out->armor.regular.kind = QA_REGULAR_ARMOR_NONE; out->armor.powered.kind = "none"; }
  out->inventory = NULL; out->inventory_count = 0;
  if (qa_shared_inventory_table_has(sim->inventory, actor))
    out->inventory = qa_shared_inventory_table_entries(sim->inventory, actor, &out->inventory_count);
  return true;
}
/* ---- sharedCarryOf (7981-7985): false = TS null. ---- */
bool qa_app_shared_sim_shared_carry_of(const qa_app_sim_travel_player *carried, qa_ss_shared_carry *out) {
  if (!carried || !carried->state_kind || qa_ss_is(carried->state_kind, "quakec")) return false;
  if (qa_ss_is(carried->state_kind, "shared")) {
    *out = (qa_ss_shared_carry){carried->health, carried->armor, carried->inventory, carried->inventory_count};
    return true;
  }
  if (qa_ss_is(carried->state_kind, "q1")) {
    const qa_q1_travel_state *carry = carried->carry;
    *out = (qa_ss_shared_carry){carry->health, carry->armor, carry->inventory, carry->inventory_count};
    return true;
  }
  const qa_q2_base_player_carry *carry = carried->carry;
  *out = (qa_ss_shared_carry){carry->health, carry->armor, carry->inventory, carry->inventory_count};
  return true;
}
/* ---- applySharedTravelCarry (7990-8013) ---- */
void qa_app_shared_sim_apply_shared_travel_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, const qa_app_sim_travel_player *carried) {
  qa_ss_shared_carry shared;
  if (!qa_app_shared_sim_shared_carry_of(carried, &shared)) return;
  if (!qa_shared_inventory_table_has(sim->inventory, actor.id))
    qa_shared_inventory_table_create_entries(sim->inventory, actor, shared.inventory, shared.inventory_count);
  else {
    size_t count;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor.id, &count);
    qa_inventory_entry *current = malloc((count ? count : 1) * sizeof *current);
    if (!current) qa_range_error("Out of memory");
    if (count) memcpy(current, entries, count * sizeof *current);
    qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, current);
    for (size_t i = 0; i < count; i++) {
      /* new Map(shared.inventory.map(entry => [entry.item, entry])): the last entry of an item wins. */
      const qa_inventory_entry *target = NULL;
      for (size_t j = 0; j < shared.inventory_count; j++) if (qa_ss_is(shared.inventory[j].item, current[i].item)) target = &shared.inventory[j];
      qa_inventory_entry next = current[i];
      next.count = target ? qa_js_min(target->count, current[i].capacity) : 0;
      qa_shared_inventory_table_configure(sim->inventory, actor, next, NULL, NULL);
    }
    qa_cleanup_pop(&cleanup); free(current);
  }
  if (qa_gameplay_authority_read(sim->combat, actor.id).has_value) {
    qa_gameplay_authority_set_health(sim->combat, actor, qa_js_max(1, trunc(shared.health)));
    qa_gameplay_authority_set_armor(sim->combat, actor, shared.armor);
  } else sim->strategy->set_guest_health(sim, actor.id, qa_js_max(1, trunc(shared.health))); /* Unqualified guests expose no shared combat view */
}
/* Q3QvmStrategy.guestHealth / setGuestHealth / guestTravelPlayers */
bool qa_ss_q3_qvm_guest_health(qa_app_shared_simulation *sim, qa_actor_id actor, double *out) {
  qa_app_q3_guest_records *records = qa_app_q3_qvm_server_game_records(sim->source.qvm);
  double slot;
  if (!qa_app_q3_guest_records_slot(records, actor, &slot)) return false;
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(records, slot, &state, stats, persistent, powerups, ammo);
  *out = stats[0];
  return true;
}
void qa_ss_q3_qvm_set_guest_health(qa_app_shared_simulation *sim, qa_actor_id actor, double health) {
  qa_app_q3_guest_records *records = qa_app_q3_qvm_server_game_records(sim->source.qvm);
  double slot;
  if (!qa_app_q3_guest_records_slot(records, actor, &slot)) return;
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(records, slot, &state, stats, persistent, powerups, ammo);
  stats[0] = health;
  qa_qvm_game_data_write_player_state(qa_app_q3_guest_records_data(records), slot, &state);
}
size_t qa_ss_q3_qvm_guest_travel_players(qa_app_shared_simulation *sim, qa_app_source_guest_traveler *out, size_t capacity) {
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64), count = 0;
  qa_app_q3_guest_records *records = qa_app_q3_qvm_server_game_records(sim->source.qvm);
  for (size_t i = 0; i < total && count < capacity; i++) {
    qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
    qa_app_q3_guest_records_player(records, players[i].source_entity, &state, stats, persistent, powerups, ammo);
    out[count++] = (qa_app_source_guest_traveler){players[i].actor, players[i].client, stats[0] > 0};
  }
  return count;
}
/* ---- applySharedTravelOnce (8017-8028) ---- */
void qa_app_shared_sim_apply_shared_travel_once(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client) {
  if (qa_ss_map_has(&sim->shared_travel_applied, &actor.id)) return;
  const qa_app_simulation_travel *travel = sim->options.has_travel ? &sim->options.travel : NULL;
  const char *kind = travel ? travel->source_kind : NULL;
  if (kind && (qa_ss_is(kind, "netquake") || qa_ss_is(kind, "quakeworld"))) return;
  const qa_app_sim_travel_player *carried = ss_travel_find(travel, client);
  qa_ss_shared_carry shared;
  if (!carried || !qa_app_shared_sim_shared_carry_of(carried, &shared)) return;
  qa_ss_map_set(&sim->shared_travel_applied, &actor.id, NULL);
  qa_app_shared_sim_apply_shared_travel_carry(sim, actor, carried);
}
/* ---- applyGuestTravelCarries (8031-8041) ---- */
void qa_app_shared_sim_apply_guest_travel_carries(qa_app_shared_simulation *sim) {
  if (!sim->options.has_travel) return;
  qa_app_source_guest_traveler players[64];
  size_t total = sim->strategy->guest_travel_players(sim, players, 64);
  for (size_t i = 0; i < total; i++) {
    if (qa_ss_map_has(&sim->shared_travel_applied, &players[i].actor) || !players[i].alive) continue;
    qa_owned_actor owned;
    if (qa_session_actor_registry_resolve_owned(sim->actors, players[i].actor, &owned))
      qa_app_shared_sim_apply_shared_travel_once(sim, owned, players[i].client);
  }
}

/* physics.setSolid(actor, solid, null, "q2"). */
static qa_app_sim_shared_solid ss_q2_solid(const char *solid) {
  qa_app_sim_shared_solid value; memset(&value, 0, sizeof value);
  value.solid = solid; value.has_model = false; value.family = "q2"; value.owner = NULL;
  return value;
}

/* ---- restoreSelectedTravel (4430-4441) ---- */
void qa_app_shared_sim_restore_selected_travel(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player,
    const qa_app_sim_travel_player *carried_player) {
  qa_owned_actor actor = player->actor;
  if (sim->selected_q3_source) qa_app_q3_selected_source_admit(sim->selected_q3_source, actor);
  if (sim->has_selected_arsenal) {
    const qa_ss_selected_arsenal_travel *carry = carried_player ? carried_player->selected_arsenal : NULL;
    if ((sim->strategy->as_q1(sim) != NULL) && qa_ss_is(sim->selected_arsenal.family, "q1")
        && (qa_app_shared_sim_selected_q1_game(sim)->registered_weapon_count == 0 || carry == NULL))
      qa_app_shared_sim_selected_arsenal_admit(sim, actor, 100, false);
    else if (carry) qa_app_shared_sim_restore_selected_arsenal_travel(sim, actor, 100, carry);
    else qa_app_shared_sim_selected_arsenal_admit(sim, actor, 100, false);
    player->arsenal = qa_app_shared_sim_arsenal(sim, player);
  } else if (carried_player && carried_player->selected_arsenal)
    ss_fail("Campaign travel selected arsenal differs from the destination recipe");
  qa_app_shared_sim_sync_selected_q1_health_limit(sim, player->actor.id);
}

/* ---- quakeCClientSpawned (4556-4570) ---- */
void qa_app_shared_sim_quake_c_client_spawned(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_player_state(sim, actor);
  if (!player) return;
  if ((sim->strategy->as_quakec(sim) == NULL)) ss_fail("QC client spawn has no source owner");
  if (sim->has_selected_arsenal && qa_app_shared_sim_selected_arsenal_has(sim, actor.id)) {
    qa_app_shared_sim_selected_arsenal_remove(sim, actor.id);
    player->arsenal = qa_app_shared_sim_selected_arsenal_admit(sim, actor,
        qa_app_sim_quakec_source_client_equipment(sim->source.quakec, actor.id).max_health, false);
  }
  qa_app_shared_sim_sync_selected_q1_health_limit(sim, actor.id);
  qa_app_shared_sim_apply_shared_travel_once(sim, actor, player->client);
  if (sim->selected_q3_source) qa_app_q3_selected_source_respawn(sim->selected_q3_source, actor.id);
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, actor.id);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor.id);
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &actor);
  if (q1) qa_q1_character_actor_respawn(*q1, NULL);
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &actor);
  if (q2) qa_q2_character_actor_respawned(*q2);
  player->state = qa_app_sim_movement_player_read_state(player);
  qa_app_shared_sim_sync_quake_c_client_view(sim, player);
}
/* ---- syncQuakeCClientView (4572-4580) ---- */
void qa_app_shared_sim_sync_quake_c_client_view(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if ((sim->strategy->as_quakec(sim) == NULL)) return;
  qa_vec3 angles;
  if (qa_app_sim_quakec_source_consume_client_view_reset(sim->source.quakec, player->actor.id, &angles)) {
    player->view_angles = angles;
    ss_view_reset(sim, "source", player->actor.id, angles);
  }
}
/* ---- resumeQ2Presentation (4582-4593): actor NULL = TS null. ---- */
static void ss_emit_q2_rerelease(qa_app_shared_simulation *sim, qa_content_id content, const qa_q2_rerelease_event *event) {
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2-rerelease"; source.q2_rerelease = event;
  qa_app_shared_sim_emit(sim, content, source, NULL, NULL);
}
void qa_app_shared_sim_resume_q2_presentation(qa_app_shared_simulation *sim, const qa_actor_id *actor) {
  if ((sim->strategy->as_q2(sim) == NULL) || !sim->source.product->rerelease) return;
  qa_q2_product_rerelease *source = sim->source.product->rerelease;
  qa_content_id content = sim->recipe.map.entities.content;
  /* { kind: "sky", ...source.entities.sky } */
  qa_q2_rerelease_event sky; memset(&sky, 0, sizeof sky);
  sky.kind = "sky"; sky.sky_name = source->entities.base.sky.name; sky.rotation = source->entities.base.sky.rotation;
  sky.auto_rotate = source->entities.base.sky.auto_rotate; sky.axis = source->entities.base.sky.axis;
  ss_emit_q2_rerelease(sim, content, &sky);
  qa_q2_rerelease_event story; memset(&story, 0, sizeof story);
  story.kind = "story"; story.text = source->entities.base.story;
  ss_emit_q2_rerelease(sim, content, &story);
  for (size_t i = 0; i < source->players.extra_count; i++) {
    qa_actor_id player = source->players.rerelease_states[i].actor;
    const qa_q2rr_player_extra_state *state = source->players.rerelease_states[i].state;
    if (actor && !qa_same_actor(*actor, player)) continue;
    qa_q2_rerelease_event fog; memset(&fog, 0, sizeof fog);
    fog.kind = "fog"; fog.has_actor = true; fog.actor = player; fog.fog_value = state->fog; fog.transition_milliseconds = 0;
    ss_emit_q2_rerelease(sim, content, &fog);
    qa_q2_rerelease_players_emit_flashlight(&source->players, player, sim->source.product->services);
  }
  qa_q2_rerelease_module_resume_presentation(&source->entities, sim->source.product->services, actor);
}

/* ---- q1CharacterSource (4607-4628) ---- */
typedef struct ss_character_runtime { qa_app_shared_simulation *sim; } ss_character_runtime;
static double ss_character_now(void *ctx) { return ss_time_seconds(((ss_character_runtime *)ctx)->sim); }
static double ss_character_frame_seconds(void *ctx) { return qa_ss_seconds(((ss_character_runtime *)ctx)->sim->source_frame.elapsed); }
static void ss_character_schedule(void *ctx, qa_owned_actor actor, const double *due) {
  qa_app_shared_simulation *sim = ((ss_character_runtime *)ctx)->sim;
  if (due == NULL) qa_frame_scheduler_cancel(sim->scheduler, actor);
  else qa_app_shared_sim_schedule(sim, actor, *due);
}
qa_q1_services *qa_app_shared_sim_q1_character_source(qa_app_shared_simulation *sim) {
  if ((sim->strategy->as_q1(sim) != NULL)) return sim->source.q1_game;
  qa_q1_services *selected = sim->has_selected_weapon_source && qa_ss_is(sim->selected_weapon_source.kind, "q1")
      ? sim->selected_weapon_source.q1_game : NULL;
  if (selected) {
    if (!qa_ss_map_has(&sim->q1_character_adjuncts, &selected)) {
      qa_q1_register_map_callbacks(selected); qa_q1_register_character_callbacks(selected);
      qa_ss_map_set(&sim->q1_character_adjuncts, &selected, NULL);
    }
    return selected;
  }
  if (sim->q1_character_foundation) return sim->q1_character_foundation;
  qa_provider_reference reference = sim->recipe.character.definition;
  qa_provider_timing timing = qa_app_sim_provider_timing(sim->recipe, reference.provider);
  ss_character_runtime *ctx = qa_app_shared_sim_keep(sim, sizeof *ctx);
  ctx->sim = sim;
  qa_app_sim_actor_host_runtime runtime = {timing.numeric, sim->random, ctx, ss_character_now, ss_character_frame_seconds, ss_character_schedule};
  qa_q1_host host = qa_app_shared_sim_q1_actor_host(sim, reference, runtime, NULL);
  qa_q1_options options; memset(&options, 0, sizeof options);
  options.provider = reference.provider;
  options.edition = strstr(reference.content, ":rerelease:") ? "rerelease" : "classic";
  options.skill = sim->options.skill; options.physics_edition = qa_app_shared_sim_q1_physics_edition(sim);
  options.deathmatch = qa_ss_is(sim->options.mode, "deathmatch") ? 1 : 0; options.coop = qa_ss_is(sim->options.mode, "coop");
  options.max_clients = sim->options.max_clients; options.has_max_clients = true;
  options.campaign = sim->recipe.map.entities.provider; options.combat_provider = sim->recipe.combat.provider;
  options.inventory_provider = sim->recipe.inventory.provider; options.movement_provider = sim->recipe.movement.provider;
  options.gravity = qa_app_sim_shared_physics_gravity(sim->physics);
  qa_q1_services *game = calloc(1, sizeof *game);
  if (!game || !qa_q1_services_init(game, host, options)) qa_range_error("Out of memory");
  qa_q1_register_map_callbacks(game); qa_q1_register_character_callbacks(game);
  sim->q1_character_foundation = game;
  return game;
}

/* ---- attachQ1Character (4630-4641) ---- */
typedef struct ss_q1_character_ctx { qa_app_shared_simulation *sim; qa_app_sim_movement_player *player; qa_q1_services *game; qa_q1_character_actor *character; } ss_q1_character_ctx;
static void ss_q1_drop_inventory(void *ctx) { ss_q1_character_ctx *c = ctx; qa_app_shared_sim_drop_player_inventory(c->sim, c->player); }
static void ss_q1_request_respawn(void *ctx) { ss_q1_character_ctx *c = ctx; qa_app_shared_sim_respawn_player(c->sim, c->player); }
static bool ss_q1_source_pose(void *ctx, qa_q1_character_source_pose *out) {
  ss_q1_character_ctx *c = ctx;
  *out = qa_app_shared_sim_q1_character_pose(c->sim, c->player->actor.id);
  return true;
}
static bool ss_q1_fall_damage_allowed(void *ctx) {
  ss_q1_character_ctx *c = ctx;
  return (c->sim->strategy->as_q1(c->sim) == NULL) || qa_comp_q1_source_composition_fall_damage_allowed(c->sim->source.q1_composition, c->player->actor.id);
}
static void ss_q1_pain(void *ctx, qa_pain_reaction reaction) {
  ss_q1_character_ctx *c = ctx;
  qa_q1_character_actor_pain(c->character, reaction.attacker, reaction.damage, false);
}
static void ss_q1_die(void *ctx, qa_death_reaction reaction) {
  ss_q1_character_ctx *c = ctx;
  c->game->time = ss_time_seconds(c->sim);
  qa_q1_character_actor_die(c->character, reaction.pain.attacker);
  if (c->player->state.kind == QA_MOVEMENT_STATE_Q1)
    c->player->state.as.q1.move_type = qa_ss_is(qa_q1_character_actor_presentation(c->character).movement, "bounce") ? 10 : 6;
}
qa_q1_character_actor *qa_app_shared_sim_attach_q1_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_q1_services *game = qa_app_shared_sim_q1_character_source(sim);
  qa_owned_actor actor = player->actor;
  ss_q1_character_ctx *ctx = qa_app_shared_sim_keep(sim, sizeof *ctx);
  ctx->sim = sim; ctx->player = player; ctx->game = game;
  qa_q1_character_options options; memset(&options, 0, sizeof options);
  options.ctx = ctx; options.request_respawn = ss_q1_request_respawn; options.drop_inventory = ss_q1_drop_inventory;
  options.source_pose = ss_q1_source_pose; options.has_fall_damage_allowed = true; options.fall_damage_allowed = ss_q1_fall_damage_allowed;
  qa_q1_character_actor *character = qa_q1_character_actor_create(game, actor, &options);
  if (!character) qa_range_error("Out of memory");
  ctx->character = character;
  qa_q1_character_actor **previous = qa_ss_map_get(&sim->q1_characters, &actor);
  if (previous) qa_q1_character_actor_destroy(*previous);
  qa_ss_map_set(&sim->q1_characters, &actor, &character);
  qa_actor_callbacks callbacks = {ctx, NULL, NULL, NULL, ss_q1_pain, ss_q1_die};
  qa_actor_callback_table_bind(sim->callbacks, actor, callbacks);
  return character;
}

/* ---- attachQ2Character (4643-4673) ---- */
typedef struct ss_q2_character_ctx { qa_app_shared_simulation *sim; qa_app_sim_movement_player *player; qa_content_id content; qa_q2_character_actor *character; } ss_q2_character_ctx;
static double ss_q2_now(void *ctx) { return ss_time_seconds(((ss_q2_character_ctx *)ctx)->sim); }
static double ss_q2_random(void *ctx) { return qa_app_sim_source_random_next_unit(((ss_q2_character_ctx *)ctx)->sim->random); }
static double ss_q2_water_type(double type) { return type < 0 ? type == -3 ? 32 : type == -4 ? 16 : type == -5 ? 8 : 0 : type; }
static qa_q2_player_movement ss_q2_movement(void *ctx, qa_actor_id actor) {
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(((ss_q2_character_ctx *)ctx)->sim, actor);
  qa_q2_player_movement movement;
  movement.view_angles = player->view_angles; movement.command_angles = player->command_angles;
  movement.water_level = player->water_level; movement.water_type = ss_q2_water_type(player->water_type);
  movement.grounded = player->ground.kind != QA_TRACE_HIT_NONE; movement.ducked = player->bounds.max.z < player->standing_bounds.max.z;
  movement.buttons = player->buttons; movement.standing_bounds = player->standing_bounds; movement.animate_q2 = true;
  movement.up_move = player->up_move;
  return movement;
}
static double ss_q2_point_contents(void *ctx, qa_vec3 point) { return qa_app_shared_sim_contents(((ss_q2_character_ctx *)ctx)->sim, point, "q2"); }
static qa_q2_player_powerups ss_q2_powerups(void *ctx, qa_actor_id actor) {
  qa_app_shared_simulation *sim = ((ss_q2_character_ctx *)ctx)->sim;
  /* TS const powers = this.strategy.q1Powerups(actor); powers?.get(name) ?? 0 */
#define SS_POWER(name) sim->strategy->q1_powerup(sim, actor, name)
  qa_q2_player_powerups result = {SS_POWER("quad"), SS_POWER("invulnerability"), SS_POWER("suit"), SS_POWER("suit"),
      SS_POWER("q3:haste"), SS_POWER("q3:regen"), SS_POWER("q3:flight"), SS_POWER("invisibility")};
#undef SS_POWER
  return result;
}
static bool ss_q2_weapon(void *ctx, qa_actor_id actor, qa_q2_character_weapon *out) {
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(((ss_q2_character_ctx *)ctx)->sim, actor);
  out->q2_name = NULL; out->ammo = ui.has_ammo ? ui.ammo_item : NULL;
  out->kick_angles = qa_ss_zero; out->kick_origin = qa_ss_zero; out->loop_sound = "";
  return true;
}
static void ss_q2_emit(void *ctx, qa_q2_presentation_event event) {
  ss_q2_character_ctx *c = ctx;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q2"; source.q2 = &event;
  qa_app_shared_sim_emit(c->sim, c->content, source, NULL, NULL);
}
static void ss_q2_view(void *ctx, qa_actor_id actor, qa_q2_base_player_view view) {
  qa_ss_map_set(&((ss_q2_character_ctx *)ctx)->sim->q2_views, &actor, &view);
}
static void ss_q2_noise(void *ctx, qa_actor_id actor, qa_vec3 origin) {
  qa_app_shared_simulation *sim = ((ss_q2_character_ctx *)ctx)->sim;
  if ((sim->strategy->as_q2(sim) != NULL)) qa_q2m_monsters_report_noise(&sim->source.product->monsters, actor, origin, false);
}
static void ss_q2_environment_damage(void *ctx, qa_owned_actor actor, double amount, double means, double flags) {
  qa_app_shared_simulation *sim = ((ss_q2_character_ctx *)ctx)->sim;
  qa_body_state body;
  if (!qa_app_shared_sim_body(sim, actor.id, &body)) return;
  qa_actor_id world; bool has_world = qa_app_shared_sim_world_actor(sim, &world);
  qa_damage_request request; memset(&request, 0, sizeof request);
  request.target = actor.id; request.amount = amount; request.knockback = 0; request.direction = qa_ss_zero;
  request.point = body.origin; request.normal = qa_ss_zero; request.delivery = "direct";
  request.attack.sequence = sim->attack_sequence++; request.attack.time = sim->source_frame.time;
  request.attack.attacker = has_world ? &world : NULL; request.attack.inflictor = has_world ? &world : NULL; request.attack.weapon = NULL;
  request.attack.weapon_provider = sim->weapon_provider.provider; request.attack.combat_provider = sim->recipe.combat.provider;
  request.attack.inventory_provider = sim->recipe.inventory.provider; request.attack.movement_provider = sim->recipe.movement.provider;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2; request.attack.cause.means_of_death = means; request.attack.cause.damage_flags = flags;
  qa_gameplay_authority_apply(sim->combat, request, NULL, NULL);
}
static void ss_q2_died(void *ctx, qa_owned_actor actor, qa_death_reaction reaction) {
  (void)actor; (void)reaction; /* TS died: () => this.dropPlayerInventory(player) takes no arguments */
  ss_q2_character_ctx *c = ctx;
  qa_app_shared_sim_drop_player_inventory(c->sim, c->player);
}
static void ss_q2_request_respawn(void *ctx, qa_owned_actor actor) {
  (void)actor; /* TS requestRespawn: () => this.respawnPlayer(player) */
  ss_q2_character_ctx *c = ctx;
  qa_app_shared_sim_respawn_player(c->sim, c->player);
}
static void ss_q2_motion(void *ctx, qa_owned_actor actor, const char *kind, const char *solid) {
  ss_q2_character_ctx *c = ctx;
  qa_q2_character_actor **character = qa_ss_map_get(&c->sim->q2_characters, &actor);
  if (character) { (*character)->entity.motion = kind; (*character)->entity.solid = solid; }
  if (c->player->state.kind == QA_MOVEMENT_STATE_Q1) c->player->state.as.q1.move_type = qa_ss_is(kind, "bounce") ? 10 : 6;
  qa_app_sim_shared_physics_set_solid(c->sim->physics, actor, ss_q2_solid(solid));
}
static void ss_q2_spawn_gib(void *ctx, qa_q2_character_gib gib) {
  ss_q2_character_ctx *c = ctx;
  qa_app_shared_sim_spawn_character_gib(c->sim, gib, c->player);
}
static void ss_q2_pain(void *ctx, qa_pain_reaction reaction) { qa_q2_character_actor_pain(((ss_q2_character_ctx *)ctx)->character, reaction); }
static void ss_q2_die(void *ctx, qa_death_reaction reaction) { qa_q2_character_actor_die(((ss_q2_character_ctx *)ctx)->character, reaction); }
void qa_app_shared_sim_attach_q2_character(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  qa_content_id content = player->character_definition.content;
  ss_q2_character_ctx *ctx = qa_app_shared_sim_keep(sim, sizeof *ctx);
  ctx->sim = sim; ctx->player = player; ctx->content = content;
  qa_q2_character_host host = {ss_bodies(sim), sim->combat, sim->inventory, ctx, ss_q2_now, ss_q2_random, ss_q2_movement,
      ss_q2_point_contents, ss_q2_powerups, ss_q2_weapon, ss_q2_emit, ss_q2_view, ss_q2_noise, ss_q2_environment_damage,
      ss_q2_died, ss_q2_request_respawn, ss_q2_motion, ss_q2_spawn_gib};
  /* `players/${appearance.provider.split("/").at(-1) ?? "male"}/tris.md2` */
  const char *appearance = player->character_appearance.provider, *slash = strrchr(appearance, '/');
  char model[256]; snprintf(model, sizeof model, "players/%s/tris.md2", slash ? slash + 1 : appearance);
  qa_q2_character_options options; memset(&options, 0, sizeof options);
  options.model = qa_app_shared_sim_intern(sim, model); options.skin = 0; options.slot = (double)player->client.slot;
  options.mode = qa_ss_is(sim->options.mode, "deathmatch") ? QA_Q2_MODE_DEATHMATCH : qa_ss_is(sim->options.mode, "coop") ? QA_Q2_MODE_COOP : QA_Q2_MODE_SINGLEPLAYER;
  options.deathmatch_flags = 0; options.environment = false;
  options.has_edition = true; options.edition = strstr(content, ":rerelease:") ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC;
  qa_q2_character_actor *character = qa_q2_character_actor_create(player->actor, host, options);
  if (!character) qa_range_error("Out of memory");
  ctx->character = character;
  qa_q2_character_actor **previous = qa_ss_map_get(&sim->q2_characters, &player->actor);
  if (previous) qa_q2_character_actor_destroy(*previous);
  qa_ss_map_set(&sim->q2_characters, &player->actor, &character);
  qa_actor_callbacks callbacks = {ctx, NULL, NULL, NULL, ss_q2_pain, ss_q2_die};
  qa_actor_callback_table_bind(sim->callbacks, player->actor, callbacks);
}

/* ---- spawnCharacterGib (4675-4683): player NULL = TS undefined. ---- */
typedef struct ss_gib_ctx { qa_app_shared_simulation *sim; } ss_gib_ctx;
static void ss_release_self(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)frame; /* TS think: self => this.actors.release(self) */
  qa_session_actor_registry_release(((ss_gib_ctx *)ctx)->sim->actors, self);
}
void qa_app_shared_sim_spawn_character_gib(qa_app_shared_simulation *sim, qa_q2_character_gib gib, qa_app_sim_movement_player *player) {
  qa_owned_actor actor = qa_session_actor_registry_allocate(sim->actors,
      player ? player->character_definition.provider : sim->recipe.character.definition.provider, "q2:character-gib");
  qa_body_state body; memset(&body, 0, sizeof body);
  body.origin = gib.origin; body.velocity = gib.velocity; body.angles = qa_ss_zero; body.ground = NULL;
  qa_shared_body_table_create_body(ss_bodies(sim), actor, body);
  qa_app_sim_shared_physics_set_solid(sim->physics, actor, ss_q2_solid("none"));
  qa_q2_motion motion; memset(&motion, 0, sizeof motion);
  motion.actor = actor; motion.velocity = gib.velocity; motion.angular_velocity = gib.angular_velocity; motion.kind = "toss";
  motion.gravity = 1; motion.gravity_vector = (qa_vec3){0, 0, -1}; motion.clip_mask = 3; motion.has_owner = false;
  qa_app_sim_shared_physics_set_motion(sim->physics, actor, motion);
  ss_gib_ctx *ctx = qa_app_shared_sim_keep(sim, sizeof *ctx);
  ctx->sim = sim;
  qa_actor_callbacks callbacks = {ctx, ss_release_self, NULL, NULL, NULL, NULL};
  qa_actor_callback_table_bind(sim->callbacks, actor, callbacks);
  qa_ss_detached_model model; memset(&model, 0, sizeof model);
  model.content = player ? player->character_definition.content : sim->recipe.character.definition.content;
  model.path = qa_app_shared_sim_intern(sim, gib.model);
  qa_ss_map_set(&sim->detached_models, &actor, &model);
  qa_app_shared_sim_schedule(sim, actor, gib.expires_at);
  qa_shared_body_table_link(ss_bodies(sim), actor, NULL);
}
/* ---- dropPlayerInventory (4685-4688) ---- */
void qa_app_shared_sim_drop_player_inventory(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if (!qa_ss_is(sim->options.mode, "singleplayer")) sim->strategy->drop_inventory(sim, player);
}
/* ---- respawnPlayer (4690-4696) ---- */
void qa_app_shared_sim_respawn_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if (qa_ss_is(sim->options.mode, "singleplayer")) return;
  sim->strategy->respawn(sim, player);
}
/* ---- placeQ1Player (4698-4710) ---- */
void qa_app_shared_sim_place_q1_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_q1_actor *start,
    const qa_q1_travel_state *travel) {
  if ((sim->strategy->as_q1(sim) == NULL)) ss_fail("Q1 placement before source entry");
  qa_body_state body;
  bool has_body = qa_app_shared_sim_body(sim, player->actor.id, &body);
  qa_q1_body spot = qa_q1_get_body(sim->source.q1_game, start);
  if (!has_body) ss_fail("Respawn player body missing");
  body.bounds = player->standing_bounds;
  qa_shared_body_table_write(ss_bodies(sim), player->actor, body);
  player->bounds = player->standing_bounds; player->has_body_shape_base = false; player->view_height = player->character == QA_GAME_FAMILY_Q3 ? 26 : 22;
  qa_comp_q1_source_composition_admit_travel(sim->source.q1_composition, player->actor, travel);
  if (sim->has_selected_arsenal) {
    qa_app_shared_sim_selected_arsenal_remove(sim, player->actor.id);
    player->arsenal = qa_app_shared_sim_selected_arsenal_admit(sim, player->actor, 100, false);
  }
  ss_set_traits(sim, player->actor, true, true, true, false);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "spawn"; change.origin = qa_ss_add(spot.origin, (qa_vec3){0, 0, 1}); change.velocity = qa_ss_zero;
  change.angles = spot.angles; change.command_angles = player->command_angles; change.hold_milliseconds = 0; change.spectator = false;
  qa_app_shared_sim_set_player_movement(sim, player->actor.id, change);
  if (sim->selected_q3_source) qa_app_q3_selected_source_respawn(sim->selected_q3_source, player->actor.id);
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, player->actor.id);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, player->actor.id);
  qa_app_shared_sim_admit_grapple(sim, player->actor.id);
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &player->actor);
  if (q2) qa_q2_character_actor_respawned(*q2);
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &player->actor);
  if (q1) qa_q1_character_actor_respawn(*q1, NULL);
  qa_app_shared_sim_kill_box(sim, player->actor);
}

/* ---- playerView (6743-6753) ---- */
qa_app_sim_player_view qa_app_shared_sim_player_view(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_sim_player_view view;
  if (!qa_app_shared_sim_source_player_view(sim, actor, &view)) ss_fail("Actor has no player view");
  qa_mod_client_movement_outputs output;
  bool has_output = qa_app_shared_sim_active_client_outputs(sim, actor, &output);
  qa_body_state body;
  bool has_body = has_output && output.view_offset && qa_app_shared_sim_body(sim, actor, &body);
  if (has_output && output.view_offset && has_body
      && (!output.has_stance || output.stance || !qa_app_shared_sim_source_client_view(sim, actor).crouched)) {
    qa_vec3 offset = *output.view_offset;
    qa_vec3 origin = qa_ss_add(body.origin, (qa_vec3){offset.x, offset.y, 0});
    qa_vec3 delta = {origin.x - view.origin.x, origin.y - view.origin.y, origin.z + offset.z - view.origin.z - view.view_height};
    view.origin = origin; view.view_height = offset.z;
    view.has_client_view_offset_delta = true; view.client_view_offset_delta = delta;
  }
  view.kick_angles = qa_ss_add(view.has_kick_angles ? view.kick_angles : qa_ss_zero, qa_app_sim_q1_player_punch_read(sim->q1_punch, actor));
  view.has_kick_angles = true;
  return view;
}
/* ---- sourcePlayerView (6754-6794). Always a view (true); the bool is the header's TS-null form. ---- */
static qa_app_sim_player_view ss_view_only(qa_vec3 origin, qa_vec3 angles, double view_height) {
  qa_app_sim_player_view view; memset(&view, 0, sizeof view);
  view.origin = origin; view.angles = angles; view.view_height = view_height;
  return view;
}
/* SourceStrategy.guestView (Q2Native, Q3Qvm). */
bool qa_ss_q2_native_guest_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out) {
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  *out = qa_ss_is(sim->source.edition, "classic")
      ? qa_app_sim_classic_guest_player_view(qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot))
      : qa_app_sim_rerelease_guest_player_view(qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot));
  return true;
}
bool qa_ss_q3_qvm_guest_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out) {
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64);
  const qa_app_q3_player *found = NULL;
  for (size_t i = 0; i < total; i++) if (qa_same_actor(players[i].actor, actor)) { found = &players[i]; break; }
  if (!found) ss_fail("Actor has no Q3 guest client view");
  qa_q3_player_state state; double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(sim->source.qvm), found->source_entity, &state, stats, persistent, powerups, ammo);
  *out = ss_view_only(state.origin, state.view_angles, state.view_height);
  return true;
}
/* Q2Strategy.chasing */
bool qa_ss_q2_chasing(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q2_base_player_state *observer = ss_q2_state(sim, actor);
  return observer && observer->spectator && observer->has_chase_target;
}
/* QuakeCStrategy.overrideView */
bool qa_ss_quakec_override_view(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_view *view, bool intermission, double health,
    qa_app_sim_player_view *out) {
  if (qa_app_sim_quakec_source_local_client_view(sim->source.quakec, actor, out)) return true;
  if (intermission) return false;
  qa_vec3 offset = qa_app_sim_quakec_source_client_view_offset(sim->source.quakec, actor);
  *out = *view;
  out->origin = qa_ss_add(view->origin, (qa_vec3){offset.x, offset.y, 0}); out->view_height = offset.z;
  if (health <= 0) out->angles.z = 80;
  return true;
}
bool qa_app_shared_sim_source_player_view(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_player_view *out) {
  if (sim->strategy->guest_view(sim, actor, out)) return true;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  const qa_q2_base_player_view *source = qa_ss_map_get(&sim->q2_views, &actor);
  qa_movement_state state = player->state;
  qa_app_sim_player_view view = qa_app_sim_movement_player_view(player);
  double health = ss_health_or(sim, actor, 0);
  if (state.kind == QA_MOVEMENT_STATE_Q1 || state.kind == QA_MOVEMENT_STATE_QW) {
    bool q1 = state.kind == QA_MOVEMENT_STATE_Q1;
    view.has_pitch_drift = true;
    view.pitch_drift_grounded = (q1 ? state.as.q1.ground.kind : state.as.qw.ground.kind) != QA_TRACE_HIT_NONE;
    view.pitch_drift_ideal_pitch = q1 ? state.as.q1.ideal_pitch : 0;
    view.pitch_drift_disabled = player->intermission || player->has_cutscene || health <= 0
        || (q1 ? state.as.q1.move_type != 3 : state.as.qw.spectator != 0);
  }
  double source_fov = source ? source->fov : 90;
  if (sim->strategy->chasing(sim, actor)) {
    *out = ss_view_only(view.origin, view.angles, 0);
    out->has_field_of_view = true; out->field_of_view = source_fov;
    return true;
  }
  if (player->has_cutscene) {
    *out = ss_view_only(qa_ss_add(player->cutscene_origin, (qa_vec3){player->cutscene_view_offset.x, player->cutscene_view_offset.y, 0}),
        player->cutscene_angles, player->cutscene_view_offset.z);
    out->has_field_of_view = true; out->field_of_view = sim->strategy->cutscene_fov(sim, source != NULL, source ? source->fov : 0);
    return true;
  }
  if (sim->strategy->override_view(sim, actor, &view, player->intermission, health, out)) return true;
  if (!player->intermission && health <= 0) {
    qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &player->actor);
    if (q1 && !qa_ss_is(qa_q1_character_actor_life(*q1), "alive")) {
      qa_vec3 offset = qa_q1_character_actor_view_offset(*q1);
      *out = view;
      out->origin = qa_ss_add(view.origin, (qa_vec3){offset.x, offset.y, 0}); out->view_height = offset.z; out->angles.z = 80;
      out->has_field_of_view = true; out->field_of_view = source_fov;
      if (sim->strategy->foreign_character_death(sim)) out->foreign_character_death = true;
      return true;
    }
    if (player->character == QA_GAME_FAMILY_Q3 && qa_ss_map_has(&sim->characters, &player->actor)) {
      const qa_attack_provenance *attack = qa_ss_map_get(&sim->last_attack, &player->actor);
      qa_body_state killer;
      bool has_killer = attack && attack->attacker && qa_app_shared_sim_body(sim, *attack->attacker, &killer);
      double yaw = !has_killer || qa_same_actor(*attack->attacker, actor) ? view.angles.y
          : atan2(killer.origin.y - view.origin.y, killer.origin.x - view.origin.x) * 180 / M_PI;
      *out = view;
      out->view_height = -16; out->angles = (qa_vec3){-15, yaw, 40};
      out->has_field_of_view = true; out->field_of_view = source_fov;
      return true;
    }
  }
  /* Classic viewoffset includes eye height; rerelease sends it separately in pmove.viewheight. */
  if (player->character != QA_GAME_FAMILY_Q2 || !source) {
    *out = view;
    if (source) { out->has_field_of_view = true; out->field_of_view = source->fov; }
    return true;
  }
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &player->actor);
  qa_app_sim_player_view result; memset(&result, 0, sizeof result);
  result.origin = qa_ss_add(view.origin, (qa_vec3){source->offset.x, source->offset.y, 0});
  result.angles = qa_ss_add(!player->intermission && health > 0 ? view.angles : source->angles, source->kick_angles);
  result.foreign_character_death = sim->strategy->foreign_character_death(sim) && q2 && (*q2)->state.dead && !player->intermission;
  result.has_field_of_view = true; result.field_of_view = source->fov;
  result.view_height = source->offset.z + (sim->strategy->rerelease_view_height(sim) && !player->intermission ? view.view_height : 0);
  *out = result;
  return true;
}

/* ---- quakeCCharacterStep (4895-4913) ---- */
qa_animation_step_result qa_app_shared_sim_quake_c_character_step(qa_app_shared_simulation *sim, qa_animation_step_input input) {
  qa_animation_step_result result; memset(&result, 0, sizeof result);
  if (qa_ss_is(input.animation.state.kind, "q1")) { result.animation = input.animation; return result; }
  double health = ss_health_or(sim, input.actor.id, 0);
  if (qa_ss_is(input.animation.state.kind, "q3"))
    return qa_q3_step_character_animation(input, strstr(sim->recipe.character.definition.content, "missionpack") ? "missionpack" : "baseq3",
        health <= 0, 0);
  qa_app_sim_movement_player *qc_player = qa_app_shared_sim_require_player(sim, input.actor.id);
  qa_actor_animation_state selected = qa_app_sim_quakec_character_animation(input.animation, health <= 0 ? "death" : "alive",
      qc_player->bounds.max.z < qc_player->standing_bounds.max.z);
  qa_animation_state animation = selected.state;
  if (!qa_ss_is(animation.kind, "q2")) ss_fail("Q2 character animation changed family");
  qa_q2_entity entity; memset(&entity, 0, sizeof entity);
  entity.frame = animation.frame;
  qa_q2_base_player_state state; memset(&state, 0, sizeof state);
  state.animation_priority = animation.priority; state.animation_end = animation.end_frame;
  state.animation_duck = animation.duck; state.animation_run = animation.run;
  double now = qa_ss_seconds(input.frame.time), elapsed = qa_ss_seconds(input.frame.elapsed);
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, input.actor.id);
  qa_body_state body;
  bool run = qa_app_shared_sim_body(sim, input.actor.id, &body) && qa_js_hypot2(body.velocity.x, body.velocity.y) != 0;
  for (double tick = floor((now - elapsed) * 10); tick < floor(now * 10); tick++)
    qa_q2_advance_player_animation(&state, &entity, player->ground.kind != QA_TRACE_HIT_NONE,
        player->bounds.max.z < player->standing_bounds.max.z, run);
  result.animation = input.animation;
  result.animation.state = animation;
  result.animation.state.frame = entity.frame; result.animation.state.end_frame = state.animation_end;
  result.animation.state.priority = state.animation_priority; result.animation.state.duck = state.animation_duck;
  result.animation.state.run = state.animation_run;
  return result;
}
/* ---- animationStep (4915-4923) ---- */
qa_animation_step_result qa_app_shared_sim_animation_step(qa_app_shared_simulation *sim, qa_animation_step_input input) {
  qa_animation_step_result result; memset(&result, 0, sizeof result);
  if (sim->strategy->animation(sim, &input, &result)) return result;
  qa_q3_character_actor *character = ss_q3_character(sim, input.actor);
  if (character) {
    qa_animation_step_input step = input;
    step.animation = qa_q3_character_actor_get_animation(character);
    result = qa_q3_step_character_animation(step, "baseq3", ss_health_or(sim, input.actor.id, 0) <= 0,
        qa_q3_character_actor_get_event_sequence(character));
    qa_q3_character_actor_commit_animation(character, result.animation);
    return result;
  }
  result.animation = input.animation;
  return result;
}
/* ---- touch (4925-4955) ---- */
static bool ss_owner_of(qa_app_shared_simulation *sim, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(sim->actors, actor, out);
}
qa_movement_continuation qa_app_shared_sim_touch(qa_app_shared_simulation *sim, qa_movement_touch_contact contact, qa_movement_state state) {
  qa_movement_continuation removed; memset(&removed, 0, sizeof removed); removed.is_continue = false;
  qa_movement_continuation keep; memset(&keep, 0, sizeof keep); keep.is_continue = true; keep.state = state;
  qa_trace_hit touched = contact.other;
  if (sim->has_native_slice_contacts) {
    qa_app_sim_native_slice_contact *entry = calloc(1, sizeof *entry);
    qa_app_sim_native_slice_contact **grown = realloc(sim->native_slice_contacts, (sim->native_slice_contact_count + 1) * sizeof *grown);
    if (!entry || !grown) qa_range_error("Out of memory");
    entry->has_actor = touched.kind == QA_TRACE_HIT_ACTOR; entry->actor = touched.actor;
    entry->has_plane = contact.plane != NULL; if (contact.plane) entry->plane = *contact.plane;
    sim->native_slice_contacts = grown; sim->native_slice_contacts[sim->native_slice_contact_count++] = entry;
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player_of(sim, contact.self);
  if (!player) return removed;
  if (sim->strategy->owns_contacts(sim, contact.self.id)) return keep;
  qa_app_sim_movement_player_commit(player, state, false, false);
  qa_actor_id other; bool has_other;
  if (contact.other.kind == QA_TRACE_HIT_ACTOR) { other = contact.other.actor; has_other = true; }
  else has_other = qa_app_shared_sim_world_actor(sim, &other);
  if (has_other) {
    qa_touch_contact shared = {contact.self, other, contact.plane, contact.surface, NULL};
    bool source_qw = sim->strategy->quake_world_contacts(sim);
    if (source_qw) {
      double position[2]; qa_app_shared_sim_source_position(sim, other, position);
      double slot = position[1];
      if (sim->has_quake_world_touched && qa_ss_map_has(&sim->quake_world_touched, &slot)) {
        keep.state = qa_app_sim_movement_player_read_state(player); return keep;
      }
      qa_owned_actor owner;
      if (ss_owner_of(sim, other, &owner)) {
        qa_touch_contact reverse = shared; reverse.self = owner; reverse.other = contact.self.id;
        qa_actor_callback_table_touch(sim->callbacks, reverse);
      }
      if (sim->has_quake_world_touched) qa_ss_map_set(&sim->quake_world_touched, &slot, NULL);
    } else if (state.kind == QA_MOVEMENT_STATE_Q2 || state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
      /* Q2 ClientThink invokes only the touched entity's callback. */
      qa_owned_actor owner;
      if (ss_owner_of(sim, other, &owner)) {
        qa_touch_contact reverse = shared; reverse.self = owner; reverse.other = contact.self.id;
        qa_touch_source_trace trace;
        if (contact.has_source_trace) {
          trace.trace = contact.source_trace_trace; trace.ent = other; trace.inverted = contact.source_trace_inverted;
          reverse.source_trace = &trace;
        }
        qa_actor_callback_table_touch(sim->callbacks, reverse);
      }
    } else {
      qa_actor_callback_table_touch(sim->callbacks, shared);
      qa_owned_actor owner;
      if (ss_owner_of(sim, other, &owner) && ss_live(sim, contact.self.id)) {
        qa_touch_contact reverse = shared; reverse.self = owner; reverse.other = contact.self.id;
        qa_actor_callback_table_touch(sim->callbacks, reverse);
      }
    }
  }
  if (!ss_live(sim, contact.self.id)) return removed;
  keep.state = qa_app_sim_movement_player_read_state(player);
  return keep;
}
/* ---- jump (4957-4975) ---- */
void qa_app_shared_sim_jump(qa_app_shared_simulation *sim, qa_owned_actor actor, const char *action) {
  if (qa_ss_map_has(&sim->native_movement_players, &actor)) return;
  if (qa_ss_is(action, "jump")) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor.id);
    if (sim->strategy->jump_voice(sim, actor, player)) return;
    qa_q3_character_actor *character = ss_q3_character(sim, actor);
    if (character) { qa_q3_character_actor_jump(character); return; }
    qa_app_sim_movement_player *q2 = qa_app_shared_sim_player(sim, actor.id);
    if (q2 && q2->character == QA_GAME_FAMILY_Q2) {
      qa_q2_presentation_event event; memset(&event, 0, sizeof event);
      event.kind = "sound"; event.has_actor = true; event.actor = actor.id;
      event.origin = qa_app_sim_movement_player_view(qa_app_shared_sim_require_player(sim, actor.id)).origin;
      event.path = "*jump1.wav"; event.channel = 2; event.volume = 1; event.attenuation = 1; event.reliable = false; event.loop = "once";
      qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
      source.kind = "q2"; source.q2 = &event;
      qa_app_shared_sim_emit(sim, sim->recipe.character.definition.content, source, NULL, NULL);
      return;
    }
  }
  bool jump = qa_ss_is(action, "jump");
  qa_q1_event event; memset(&event, 0, sizeof event);
  event.kind = "sound"; event.has_actor = true; event.actor = actor.id;
  event.path = jump ? "player/plyrjmp8.wav" : "misc/water1.wav"; event.sound_channel = "body"; event.volume = 1; event.attenuation = 1;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  qa_app_shared_sim_emit(sim, jump ? sim->recipe.character.definition.content : sim->recipe.movement.content, source, NULL, NULL);
}

/* ---- setSourcePlayerIdentity (6478-6499): absent members are NULL / has_* false. ---- */
void qa_app_shared_sim_set_source_player_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity) {
  qa_app_shared_sim_require_player(sim, actor);
  sim->strategy->set_identity(sim, actor, identity);
}
/* Q1Strategy.setIdentity / Q2Strategy.setIdentity */
void qa_ss_q1_set_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity) {
  qa_ss_source_runtime *source = &sim->source;
  qa_comp_q1_source_clients *clients = &source->q1_composition->clients;
  qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(clients, actor);
  ss_info info = {0};
  for (size_t i = 0; i < client->userinfo_count; i++) ss_info_set(&info, client->userinfo[i].key, client->userinfo[i].value);
  if (identity->name) ss_info_set(&info, "name", identity->name);
  qa_comp_userinfo_entry *entries = calloc(info.count + 1, sizeof *entries);
  if (!entries) qa_range_error("Out of memory");
  for (size_t i = 0; i < info.count; i++) entries[i] = (qa_comp_userinfo_entry){info.items[i].name, info.items[i].value};
  qa_comp_q1_source_clients_update(clients, actor, entries, info.count);
  free(entries); ss_info_free(&info);
  if (identity->has_shirt || identity->has_pants) {
    client = qa_comp_q1_source_clients_require(clients, actor); /* TS reads client.shirt/pants after the update */
    qa_comp_q1_source_clients_colors(clients, actor, identity->has_shirt ? identity->shirt : (double)qa_comp_q1_source_client_shirt(client),
        identity->has_pants ? identity->pants : (double)qa_comp_q1_source_client_pants(client));
  }
}
void qa_ss_q2_set_identity(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_player_identity *identity) {
  qa_ss_source_runtime *source = &sim->source;
  qa_q2_entity *entity = ss_q2_entity(sim, actor);
  qa_q2_base_player_state *state = ss_q2_state(sim, actor);
  if (!entity || !state) ss_fail("Q2 source identity requires an admitted player");
  ss_info info = ss_q2_userinfo(state->userinfo);
  if (identity->name) ss_info_set(&info, "name", identity->name);
  if (identity->skin) ss_info_set(&info, "skin", identity->skin);
  char *text = ss_info_text(info.items, info.count);
  ss_info_free(&info);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, text);
  qa_q2_players_userinfo_changed(source->product->players, entity, source->product->services, text);
  qa_cleanup_pop(&cleanup); free(text);
}
/* ---- setPlayerFieldOfView (6500-6536): mode "change" | "restore". ---- */
/* updateView(previous, current): `source_intermission` is the Q2 strategy's own
 * source.players.intermission test (runtime.ts's updateView has none). */
static void ss_update_view(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player, qa_actor_id actor, bool source_intermission,
    double previous, double current) {
  if (player->intermission || source_intermission) return;
  qa_q2_base_player_view *view = qa_ss_map_get(&sim->q2_views, &actor);
  if (view && view->fov == previous) view->fov = current;
}
void qa_app_shared_sim_set_player_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode) {
  if (sim->strategy->set_field_of_view(sim, actor, field_of_view, mode)) return;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &player->actor);
  if (character) { ss_update_view(sim, player, actor, false, (*character)->state.fov, field_of_view); (*character)->state.fov = field_of_view; }
}
/* Q2NativeStrategy.commandMovement */
bool qa_ss_q2_native_command_movement(qa_app_shared_simulation *sim, qa_client_id client, qa_movement_state *out) {
  double slot = client.slot + 1;
  memset(out, 0, sizeof *out);
  if (qa_ss_is(sim->source.edition, "classic")) {
    out->kind = QA_MOVEMENT_STATE_Q2;
    out->as.q2 = qa_app_sim_classic_guest_world_player_state(sim->source.classic_world, slot).movement;
  } else {
    out->kind = QA_MOVEMENT_STATE_Q2_RERELEASE;
    out->as.q2_rerelease = qa_app_sim_rerelease_guest_world_player_state(sim->source.rerelease_world, slot).movement;
  }
  return true;
}
/* Q2NativeStrategy.setFieldOfView */
bool qa_ss_q2_native_set_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode) {
  char number[32];
  double slot = qa_app_shared_sim_native_q2_client(sim, actor).slot + 1;
  bool classic = qa_ss_is(sim->source.edition, "classic");
  size_t count;
  const qa_app_sim_classic_guest_client *clients = classic ? qa_app_sim_classic_guest_world_clients(sim->source.classic_world, &count)
      : qa_app_sim_rerelease_guest_world_clients(sim->source.rerelease_world, &count);
  const qa_app_sim_classic_guest_client *client = NULL;
  for (size_t i = 0; i < count; i++) if (clients[i].slot == slot) { client = &clients[i]; break; }
  if (!client) ss_fail("Native player has no source userinfo");
  if (qa_ss_is(mode, "restore")) return true;
  ss_info info = ss_q2_userinfo(client->userinfo);
  ss_info_set(&info, "fov", qa_ss_number_text(field_of_view, number));
  char *text = ss_info_text(info.items, info.count);
  ss_info_free(&info);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, text);
  if (classic) qa_app_sim_classic_guest_world_userinfo(sim->source.classic_world, slot, text);
  else qa_app_sim_rerelease_guest_world_userinfo(sim->source.rerelease_world, slot, text);
  qa_cleanup_pop(&cleanup); free(text);
  return true;
}
/* Q2Strategy.setFieldOfView */
bool qa_ss_q2_set_field_of_view(qa_app_shared_simulation *sim, qa_actor_id actor, double field_of_view, const char *mode) {
  char number[32];
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q2_product_runtime *source = sim->source.product;
  qa_q2_entity *entity = ss_q2_entity(sim, actor);
  qa_q2_base_player_state *state = ss_q2_state(sim, actor);
  if (!entity || !state) return true;
  ss_info info = ss_q2_userinfo(state->userinfo);
  double previous = state->fov;
  /* Saved userinfo holds the ordinary request; source cameras can change only the effective FOV. */
  const char *requested_text = ss_info_get(&info, "fov");
  double parsed = qa_js_parse_int(requested_text ? requested_text : "0", 10);
  double requested = parsed != parsed || parsed == 0 ? 0 : parsed; /* || 0 */
  qa_q2_game_options *options = &source->services->options;
  bool fixed = options->edition != QA_Q2_EDITION_RERELEASE && options->mode == QA_Q2_MODE_DEATHMATCH
      && qa_js_bit_and(options->deathmatch_flags, 32768) != 0;
  double ordinary = fixed ? 90 : options->edition == QA_Q2_EDITION_RERELEASE ? qa_js_max(1, qa_js_min(160, requested))
      : requested < 1 ? 90 : qa_js_min(160, requested);
  bool preserve = !fixed && (player->has_cutscene || state->has_chase_target || qa_js_bit_and(entity->flags, 0x4000) != 0
      || (qa_ss_is(mode, "restore") && previous != ordinary));
  ss_info_set(&info, "fov", qa_ss_number_text(field_of_view, number));
  char *text = ss_info_text(info.items, info.count);
  ss_info_free(&info);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, text);
  qa_q2_players_userinfo_changed(source->players, entity, source->services, text);
  qa_cleanup_pop(&cleanup); free(text);
  if (preserve) state->fov = previous;
  ss_update_view(sim, player, actor, qa_ss_is(qa_q2_players_intermission(sim->source.product->players).kind, "intermission"), previous, state->fov);
  return true;
}

/* ---- disconnectPlayer (7457-7476) ---- */
void qa_app_shared_sim_disconnect_player(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_app_shared_sim_notify_client_event(sim, "disconnecting", actor);
  if (sim->strategy->as_q2_native(sim) != NULL) {
    sim->strategy->disconnect(sim, actor, NULL);
    return;
  }
  qa_app_sim_movement_player *player = qa_app_shared_sim_player(sim, actor);
  if (!player) return;
  qa_owned_actor owned = player->actor;
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor);
  qa_app_shared_sim_step_hand_grenade(sim, owned.id, "removing");
  if (sim->strategy->disconnect(sim, actor, player)) qa_session_actor_registry_release(sim->actors, owned);
}
/* Q2NativeStrategy.disconnect */
bool qa_ss_q2_native_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)player;
  qa_client_id client = qa_app_shared_sim_native_q2_client(sim, actor);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, actor); /* host.releaseGrapple */
  qa_app_shared_sim_step_hand_grenade(sim, actor, "removing");               /* host.removeHandGrenade */
  if (qa_ss_is(sim->source.edition, "classic")) qa_app_sim_classic_guest_world_disconnect(sim->source.classic_world, client.slot + 1);
  else qa_app_sim_rerelease_guest_world_disconnect(sim->source.rerelease_world, client.slot + 1);
  qa_ss_map_delete(&sim->source.clients, &client);
  return false;
}
/* QuakeCStrategy.disconnect */
bool qa_ss_quakec_disconnect(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player) {
  (void)actor;
  if (player == NULL) return false;
  qa_owned_actor owned = player->actor;
  qa_app_sim_quakec_source_disconnect_client(sim->source.quakec, owned);
  /* The reserved QC slot keeps its actor, so the release handler may never clear the adjunct bindings (host.releasePickupBinding). */
  qa_ss_dispose *binding = qa_ss_map_get(&sim->selected_pickup_bindings, &owned.id);
  if (binding) binding->dispose(binding->ctx);
  qa_ss_map_delete(&sim->selected_pickup_bindings, &owned.id);
  if (sim->has_selected_arsenal) qa_app_shared_sim_selected_arsenal_remove(sim, owned.id);
  qa_app_sim_movement_player_destroy(player); /* host.forgetPlayer */
  qa_ss_map_delete(&sim->player_states, &owned);
  return false;
}

/* ---- queueQuakeWorldCommands (5024-5034) ---- */
void qa_app_shared_sim_queue_quake_world_commands(qa_app_shared_simulation *sim, qa_client_id client, const qa_qw_user_command *commands,
    size_t count, double sequence) {
  qa_app_shared_sim_assert_open(sim);
  if (!ss_quakec_kind(sim, "quakeworld") || !qa_app_shared_sim_owns_client(sim, client)
      || !(fabs(sequence) <= 9007199254740991.0 && trunc(sequence) == sequence) || sequence < 0 || count < 1 || count > 20)
    ss_fail("Invalid native QuakeWorld command group");
  bool begun = false;
  for (size_t i = 0; i < sim->player_states.count; i++)
    if (qa_same_client((*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i))->client, client)) begun = true;
  if (!begun) ss_fail("QuakeWorld command client has not begun");
  for (size_t i = 0; i < count; i++) {
    double ms = commands[i].milliseconds;
    if (!isfinite(ms) || trunc(ms) != ms || ms < 0 || ms > 255) ss_fail("Invalid QuakeWorld command duration");
  }
  qa_ss_quakeworld_command group; memset(&group, 0, sizeof group);
  group.action = false; group.client = client; group.sequence = sequence;
  group.commands = malloc(count * sizeof *group.commands);
  if (!group.commands) qa_range_error("Out of memory");
  memcpy(group.commands, commands, count * sizeof *group.commands); /* structuredClone */
  group.command_count = count;
  qa_event_ring_admit(&sim->quake_world_commands, &group);
}
/* ---- queueQuakeWorldAction (5035-5040) ---- */
void qa_app_shared_sim_queue_quake_world_action(qa_app_shared_simulation *sim, qa_client_id client, void (*action)(void *ctx), void *action_ctx) {
  qa_app_shared_sim_assert_open(sim);
  if (!ss_quakec_kind(sim, "quakeworld") || !qa_app_shared_sim_owns_client(sim, client)
      || !qa_app_sim_quakec_source_has_client(sim->source.quakec, client)) ss_fail("QuakeWorld action requires a connected client");
  qa_ss_quakeworld_command group; memset(&group, 0, sizeof group);
  group.action = true; group.client = client; group.run = action; group.action_ctx = action_ctx;
  qa_event_ring_admit(&sim->quake_world_commands, &group);
}
/* ---- runQuakeWorldCommands (5051-5102) ---- */
static qa_app_sim_movement_player *ss_player_for_client(qa_app_shared_simulation *sim, qa_client_id client) {
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    if (qa_same_client(player->client, client)) return player;
  }
  return NULL;
}
static void ss_refresh_quakec_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  player->arsenal = qa_app_shared_sim_arsenal(sim, player);
  if (player->character == QA_GAME_FAMILY_Q1) player->animation = qa_app_sim_quakec_source_client_animation(sim->source.quakec, player->actor.id);
  player->state = qa_app_sim_movement_player_read_state(player);
  qa_app_shared_sim_sync_quake_c_client_view(sim, player);
}
typedef struct ss_qw_slices { qa_app_shared_simulation *sim; qa_app_sim_movement_player *player; qa_actor_command input; double maximum; } ss_qw_slices;
static void ss_qw_slice_operation(void *ctx, qa_user_command effective, const qa_arsenal_intent *arsenal) {
  (void)arsenal; /* TS effective => ...: the arsenal intent is not read */
  ss_qw_slices *run = ctx;
  qa_app_shared_simulation *sim = run->sim; qa_app_sim_movement_player *player = run->player;
  if (effective.kind != QA_USER_COMMAND_QW) ss_fail("Input output changed source command dialect");
  size_t count;
  qa_qw_user_command *slices = qa_quakeworld_command_slices(effective.as.qw, run->maximum, &count);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, slices);
  for (size_t i = 0; i < count; i++) {
    if (!ss_live(sim, player->actor.id)) break;
    qa_qw_user_command slice = slices[i];
    qa_movement_state state = qa_app_sim_movement_player_read_state(player);
    double milliseconds = (state.kind == QA_MOVEMENT_STATE_Q3 ? state.as.q3.command_time_milliseconds : ss_time_seconds(sim) * 1000) + slice.milliseconds;
    qa_qw_user_command physical = slice;
    physical.up_move = qa_js_bit_and(slice.buttons, 2) != 0 ? 320 : slice.up_move;
    qa_actor_command shaped = run->input;
    shaped.command.kind = QA_USER_COMMAND_QW; shaped.command.as.qw = physical;
    qa_app_sim_q3_command_player view = {player->arsenal, sim->recipe.map.entities.content, player->profile};
    qa_user_command selected = qa_app_sim_selected_q3_command(qa_app_sim_q3_source_command(shaped, view, milliseconds, 0), player->profile, slice.milliseconds);
    qa_app_sim_relative_movement_input relative = qa_app_sim_relative_movement_command((qa_app_sim_relative_movement_input){selected, "absolute"}, state);
    qa_actor_command moved = run->input;
    moved.command = relative.command; moved.angle_space = relative.angle_space;
    qa_frame_context frame = sim->source_frame;
    frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND; frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, slice.milliseconds};
    qa_app_sim_movement_player_move_command(player, moved, frame, &slice);
  }
  qa_cleanup_pop(&cleanup); free(slices);
}
static void ss_qw_after(void *ctx) { qa_app_shared_simulation *sim = ctx; sim->has_quake_world_touched = false; qa_ss_map_clear(&sim->quake_world_touched); }
void qa_app_shared_sim_run_quake_world_commands(qa_app_shared_simulation *sim) {
  if (!ss_quakec_kind(sim, "quakeworld")) return;
  qa_app_sim_quakec_source *source = sim->source.quakec;
  size_t group_count;
  qa_ss_quakeworld_command *groups = qa_event_ring_take(&sim->quake_world_commands, &sim->quake_world_cursor, &group_count);
  qa_event_ring_retire(&sim->quake_world_commands);
  qa_cleanup groups_cleanup; qa_cleanup_push(&groups_cleanup, free, groups);
  for (size_t g = 0; g < group_count; g++) {
    qa_ss_quakeworld_command *group = &groups[g];
    qa_app_sim_movement_player *player = ss_player_for_client(sim, group->client);
    if (group->action) {
      if (!qa_app_sim_quakec_source_has_client(source, group->client)) continue;
      group->run(group->action_ctx);
      if (!player || !qa_app_sim_quakec_source_is_active_client(source, player->actor.id)) continue;
      ss_refresh_quakec_player(sim, player);
      continue;
    }
    qa_cleanup commands_cleanup; qa_cleanup_push(&commands_cleanup, free, group->commands);
    if (!player || group->sequence <= player->last_sequence) { qa_cleanup_pop(&commands_cleanup); free(group->commands); continue; }
    qa_ss_map_clear(&sim->quake_world_touched); sim->has_quake_world_touched = true;
    qa_cleanup touched_cleanup; qa_cleanup_push(&touched_cleanup, ss_qw_after, sim); /* finally */
    qa_clock_profile clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.map.entities.provider).clock;
    if (clock.kind != QA_CLOCK_Q1_QUAKEWORLD) ss_fail("Native QW source requires its command clock");
    for (size_t i = 0; i < group->command_count; i++) {
      qa_qw_user_command command = group->commands[i];
      if (!ss_live(sim, player->actor.id)) break;
      qa_actor_command input; memset(&input, 0, sizeof input);
      input.actor = player->actor.id; input.source.kind = QA_COMMAND_SOURCE_REMOTE_CLIENT; input.source.client = group->client;
      input.command.kind = QA_USER_COMMAND_QW; input.command.as.qw = command; input.sequence = group->sequence;
      qa_app_shared_sim_observe_client_command(sim, &input);
      qa_frame_context frame = sim->source_frame;
      frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND;
      if (qa_ss_is(player->profile.kind, "q1-quakeworld")) {
        frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, command.milliseconds};
        qa_app_sim_movement_player_move(player, input, frame, NULL);
      } else {
        double applied = command.milliseconds;
        if (qa_mod_client_applications_active(sim->mod_client_applications)) {
          applied = 0;
          size_t count;
          qa_qw_user_command *slices = qa_quakeworld_command_slices(command, clock.maximum_command_milliseconds, &count);
          for (size_t j = 0; j < count; j++) applied += slices[j].milliseconds;
          free(slices);
        }
        frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, applied};
        ss_qw_slices run = {sim, player, input, clock.maximum_command_milliseconds};
        qa_user_command wrapped; wrapped.kind = QA_USER_COMMAND_QW; wrapped.as.qw = command;
        qa_app_sim_movement_player_with_input_command(player, wrapped, frame, ss_qw_slice_operation, &run, NULL);
      }
    }
    if (ss_live(sim, player->actor.id)) {
      qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(source, player->actor.id);
      qa_app_sim_quakec_source_client_post_think(source, player->actor);
      qa_app_shared_sim_project_quake_c_attack(sim, player->actor, before);
      if (!qa_app_sim_quakec_source_is_spectator_client(source, player->actor.id)) qa_app_shared_sim_run_quake_world_new_missile(sim);
      ss_refresh_quakec_player(sim, player);
    }
    qa_cleanup_pop(&touched_cleanup); ss_qw_after(sim);
    qa_cleanup_pop(&commands_cleanup); free(group->commands);
  }
  qa_cleanup_pop(&groups_cleanup); free(groups);
}

/* ---- createPlayer (4118-4205) ----
 * The MovementPlayer host closures of TS capture (this, actor, source); they
 * live in one record kept until the simulation is destroyed. The
 * ModClientApplications begin/finish of players.ts beginInputApplication runs
 * here (players.c delegates it to its host). */
typedef struct ss_player_host {
  qa_app_shared_simulation *sim;
  qa_owned_actor actor;
  qa_app_sim_quakec_source *qc; /* source.kind "quakec" captured by TS */
  qa_app_sim_netquake_client_binding netquake;
  qa_mod_client_application *applications[16]; size_t depth; /* sliceApplications / applicationParent */
  qa_app_sim_movement_player *player;
  qa_fixed_movement_pose fixed_pose;
  qa_actor_id jump_pad; /* storage behind a read Q3 movement state's jumpPad */
  qa_arsenal_intent q3_arsenal_intent, q3_policy_arsenal; /* storage behind moveQ3Client's command arsenal intents */
  qa_actor_id *q3_contacts; size_t q3_contact_capacity;  /* moveQ3Client's borrowed contact list */
  qa_inventory_entry q3_spawn_ammo[32];                   /* spawnQ3Player's loadout ammo (QA_Q3_WEAPON_ITEM_COUNT * 2 bound) */
} ss_player_host;
static qa_mod_client_movement_outputs ss_host_client_outputs(void *ctx) {
  ss_player_host *h = ctx;
  qa_mod_client_movement_outputs out; memset(&out, 0, sizeof out);
  qa_app_shared_sim_active_client_outputs(h->sim, h->actor.id, &out);
  return out;
}
typedef struct ss_aim_ctx { qa_app_sim_movement_player *player; } ss_aim_ctx;
static double ss_word_of(double angle) { return trunc(angle * 65536 / 360); }
static qa_user_command ss_encode_aim(void *ctx, qa_vec3 aim, qa_user_command current) {
  qa_movement_state state = qa_app_sim_movement_player_read_state(((ss_aim_ctx *)ctx)->player);
  if (current.kind == QA_USER_COMMAND_Q1) { current.as.q1.view_angles = aim; return current; }
  if (current.kind == QA_USER_COMMAND_QW) { current.as.qw.angles = aim; return current; }
  if (current.kind == QA_USER_COMMAND_Q2_RERELEASE && state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) {
    qa_vec3 delta = state.as.q2_rerelease.delta_angles;
    current.as.q2_rerelease.angles = (qa_vec3){aim.x - delta.x, aim.y - delta.y, aim.z - delta.z};
    return current;
  }
  if (current.kind == QA_USER_COMMAND_Q2 && state.kind == QA_MOVEMENT_STATE_Q2) {
    const double *delta = state.as.q2.delta_angle_shorts.values;
    current.as.q2.angle_shorts = (qa_angle_words){{ss_word_of(aim.x) - delta[0], ss_word_of(aim.y) - delta[1], ss_word_of(aim.z) - delta[2]}};
    return current;
  }
  if (current.kind == QA_USER_COMMAND_Q3 && state.kind == QA_MOVEMENT_STATE_Q3) {
    const double *delta = state.as.q3.delta_angle_words.values;
    current.as.q3.angle_words = (qa_angle_words){{ss_word_of(aim.x) - delta[0], ss_word_of(aim.y) - delta[1], ss_word_of(aim.z) - delta[2]}};
    return current;
  }
  ss_fail("Input aim output changed movement dialect");
}
static bool ss_host_input_begin(void *ctx, qa_app_sim_movement_player *player, const char *scope, qa_user_command command,
    qa_frame_context frame, qa_movement_state state, const qa_arsenal_intent *arsenal,
    qa_user_command *out_command, const qa_arsenal_intent **out_arsenal) {
  ss_player_host *h = ctx;
  qa_mod_client_applications *owner = h->sim->mod_client_applications;
  if (!qa_mod_client_applications_active(owner)) return false;
  qa_mod_client_application_input input; memset(&input, 0, sizeof input);
  input.identity = (qa_mod_client_identity){player->client, player->actor.id};
  input.scope = scope; input.command = command;
  input.angle_space = command.kind == QA_USER_COMMAND_Q1 || command.kind == QA_USER_COMMAND_QW ? "absolute" : "source-relative";
  input.absolute_aim = qa_app_sim_movement_application_aim(command, state, ss_health_or(h->sim, player->actor.id, 0), player->services.numeric);
  input.frame = frame;
  input.accepted = qa_ss_map_get(&h->sim->mod_client_commands, &player->actor.id);
  input.has_arsenal = arsenal != NULL; input.arsenal = arsenal;
  if (h->depth > 0) { input.has_parent_invocation = true; input.parent_invocation = h->applications[h->depth - 1]->invocation; }
  ss_aim_ctx aim = {player};
  qa_mod_client_application *application = qa_mod_client_applications_begin(owner, input, ss_encode_aim, &aim);
  if (h->depth == sizeof h->applications / sizeof *h->applications) qa_range_error("Input application nesting exceeds its bound");
  h->applications[h->depth++] = application;
  *out_command = application ? application->command : command;
  *out_arsenal = application ? (application->has_arsenal ? &application->arsenal : NULL) : arsenal;
  return true;
}
static void ss_host_input_finish(void *ctx, bool failed) {
  ss_player_host *h = ctx;
  qa_mod_client_application *application = h->depth > 0 ? h->applications[--h->depth] : NULL;
  qa_mod_client_applications_finish(h->sim->mod_client_applications, application, failed);
}
static void ss_host_project(ss_player_host *h, qa_arsenal_state before) { qa_app_shared_sim_project_quake_c_attack(h->sim, h->actor, before); }
static qa_movement_state ss_host_project_state(void *ctx, qa_movement_state state) {
  ss_player_host *h = ctx;
  return qa_app_sim_quakec_source_is_spectator_client(h->qc, h->actor.id) ? qa_app_sim_quakec_free_movement(state) : state;
}
static qa_app_sim_quakec_client_movement ss_host_before_movement(void *ctx, qa_actor_command input, qa_frame_context frame,
    const qa_qw_user_command *source_command) {
  ss_player_host *h = ctx;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(h->qc, h->actor.id);
  qa_app_sim_quakec_client_movement result = source_command == NULL
      ? qa_app_sim_quakec_source_mixed_client_pre_think(h->qc, h->actor, input.command, frame)
      : qa_app_sim_quakec_source_mixed_quakeworld_pre_think(h->qc, h->actor, *source_command, input.command, frame);
  ss_host_project(h, before);
  return result;
}
static void ss_host_after_movement(void *ctx, qa_app_sim_movement_player *player, qa_frame_context frame, const qa_qw_user_command *source_command) {
  (void)frame; /* TS (player, _frame, sourceCommand?) */
  ss_player_host *h = ctx;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(h->qc, h->actor.id);
  qa_app_sim_quakec_source_mixed_client_post_think(h->qc, h->actor, player->ground, player->water_level, player->water_type,
      player->profile.kind, source_command == NULL ? "immediate" : "deferred");
  ss_host_project(h, before);
  player->arsenal = qa_app_shared_sim_arsenal(h->sim, player);
}
static bool ss_host_q2_config(void *ctx, qa_app_sim_q2_movement_config *out) { return qa_app_shared_sim_q2_movement_config(((ss_player_host *)ctx)->sim, out); }
/* NetQuake binding of a native QuakeC source. */
static qa_q1_movement_state ss_qc_read(void *ctx, qa_q1_movement_state state) {
  ss_player_host *h = ctx; return qa_app_sim_quakec_source_read_client_state(h->qc, h->actor.id, state);
}
static void ss_qc_write(void *ctx, qa_q1_movement_state state) { ss_player_host *h = ctx; qa_app_sim_quakec_source_write_client_state(h->qc, h->actor.id, state); }
static void ss_qc_input(void *ctx, qa_q1_user_command command) { ss_player_host *h = ctx; qa_app_sim_quakec_source_client_input(h->qc, h->actor.id, command); }
static void ss_qc_before(void *ctx, qa_frame_context frame) {
  (void)frame; /* TS beforePhysics: () => ... */
  ss_player_host *h = ctx;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(h->qc, h->actor.id);
  qa_app_sim_quakec_source_client_pre_think(h->qc, h->actor); ss_host_project(h, before);
}
static void ss_qc_think(void *ctx, qa_frame_context frame) {
  ss_player_host *h = ctx;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(h->qc, h->actor.id);
  qa_app_sim_quakec_source_run_think(h->qc, h->actor, frame); ss_host_project(h, before);
}
static void ss_qc_after(void *ctx, qa_frame_context frame) {
  (void)frame; /* TS afterPhysics: () => ... */
  ss_player_host *h = ctx;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(h->qc, h->actor.id);
  qa_app_sim_quakec_source_client_post_think(h->qc, h->actor); ss_host_project(h, before);
}
/* NetQuake binding of a TS Q1 source. */
static void ss_q1_input(void *ctx, qa_q1_user_command command) {
  ss_player_host *h = ctx;
  qa_comp_q1_source_input input; memset(&input, 0, sizeof input);
  input.attack = qa_js_bit_and(command.buttons, 1) != 0; input.jump = qa_js_bit_and(command.buttons, 2) != 0;
  input.use = qa_js_bit_and(command.buttons, 4) != 0; input.impulse = command.impulse;
  qa_comp_q1_source_composition_input(h->sim->source.q1_composition, h->actor.id, input);
}
static void ss_q1_before(void *ctx, qa_frame_context frame) {
  ss_player_host *h = ctx; qa_app_shared_simulation *sim = h->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, h->actor.id);
  qa_comp_q1_source_composition_player_pre_think(sim->source.q1_composition, h->actor.id);
  double water = player->water_level;
  qa_q1_player_frame(sim->source.q1_game, h->actor, qa_ss_seconds(frame.time), &water);
}
static void ss_q1_think(void *ctx, qa_frame_context frame) {
  ss_player_host *h = ctx;
  frame.phase = QA_FRAME_PHASE_ENTITY_THINK;
  qa_frame_scheduler_run(h->sim->scheduler, h->actor.id, frame, QA_THINK_DURING_PHYSICS);
}
static void ss_q1_after(void *ctx, qa_frame_context frame) {
  ss_player_host *h = ctx; qa_app_shared_simulation *sim = h->sim;
  qa_q1_player_after_physics(sim->source.q1_game, h->actor, qa_ss_seconds(frame.time));
  qa_comp_q1_source_composition_player_post_think(sim->source.q1_composition, h->actor.id);
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &h->actor);
  if (q2) qa_q2_character_actor_after_client_think(*q2);
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &h->actor);
  if (q1) qa_q1_character_actor_post_move(*q1);
}
/* QuakeWorld binding. */
static qa_qw_movement_state ss_qw_read(void *ctx, qa_qw_movement_state state) { ss_player_host *h = ctx; return qa_app_sim_quakec_source_read_quakeworld_state(h->qc, h->actor.id, state); }
static void ss_qw_write(void *ctx, qa_qw_movement_state state) { ss_player_host *h = ctx; qa_app_sim_quakec_source_write_quakeworld_state(h->qc, h->actor.id, state); }
static void ss_qw_before(void *ctx, qa_qw_user_command command, qa_frame_context frame) {
  ss_player_host *h = ctx;
  qa_arsenal_state before = qa_app_sim_quakec_source_client_arsenal(h->qc, h->actor.id);
  qa_app_sim_quakec_source_quakeworld_pre_think(h->qc, h->actor, command, frame); ss_host_project(h, before);
}
static void ss_qw_water(void *ctx, double level, double type) { ss_player_host *h = ctx; qa_app_sim_quakec_source_quakeworld_water(h->qc, h->actor.id, level, type); }
static qa_qw_movement_profile ss_qw_profile(void *ctx, qa_qw_movement_profile profile) { ss_player_host *h = ctx; return qa_app_sim_quakec_source_quakeworld_profile(h->qc, h->actor.id, profile); }
static void ss_host_touch_triggers(void *ctx, qa_owned_actor owned) {
  ss_player_host *h = ctx; qa_app_shared_simulation *sim = h->sim;
  if (sim->strategy->skips_trigger_touch(sim, owned.id)) return;
  qa_app_sim_shared_physics_touch_triggers(sim->physics, owned);
}
static bool ss_host_is_brush(void *ctx, qa_actor_id id) {
  qa_app_shared_simulation *sim = ((ss_player_host *)ctx)->sim;
  qa_app_sim_actor_execution *entry = qa_ss_map_get(&sim->actor_executions, &id);
  return qa_app_sim_shared_physics_is_brush(sim->physics, id)
      || (entry && entry->kind == QA_APP_SIM_ACTOR_EXECUTION_Q1 && qa_ss_is_q1_barrel(entry->q1_entity->classname));
}
static bool ss_host_world_actor(void *ctx, qa_actor_id *out) { return qa_app_shared_sim_world_actor(((ss_player_host *)ctx)->sim, out); }
static double ss_host_speed_multiplier(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ((ss_player_host *)ctx)->sim;
  qa_q3_game_client *client = NULL;
  bool q3 = sim->strategy->q3_client(sim, actor.id, &client);
  if (q3 && qa_ss_is(qa_app_shared_sim_require_player(sim, actor.id)->profile.kind, "q3")) return 1;
  if (sim->selected_q3_source && qa_app_q3_selected_source_owns_equipment(sim->selected_q3_source))
    return qa_app_q3_selected_source_speed_multiplier(sim->selected_q3_source, actor.id);
  return client ? qa_q3_ta_client_speed_multiplier(&client->ps) : 1;
}
static const qa_fixed_movement_pose *ss_host_fixed_pose(void *ctx, qa_owned_actor actor) {
  ss_player_host *h = ctx; qa_app_shared_simulation *sim = h->sim;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor.id);
  if (!sim->selected_q3_source) return NULL;
  return qa_app_q3_selected_source_fixed_pose(sim->selected_q3_source, actor,
      qa_app_sim_player_postures(player->character, player->standing_bounds), &h->fixed_pose) ? &h->fixed_pose : NULL;
}
static bool ss_host_source_punch(void *ctx, qa_actor_id actor, qa_vec3 *out) {
  qa_app_shared_simulation *sim = ((ss_player_host *)ctx)->sim;
  if (sim->strategy->supplies_punch(sim)) return false;
  *out = qa_app_sim_q1_player_punch_read(sim->q1_punch, actor);
  return true;
}
static bool ss_host_gibbed(void *ctx) {
  ss_player_host *h = ctx; qa_app_shared_simulation *sim = h->sim;
  qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &h->actor);
  if (character) return (*character)->state.gibbed;
  return sim->strategy->q2_gibbed(sim, h->actor.id);
}
static void ss_host_jump(void *ctx, qa_owned_actor actor, const char *action) { qa_app_shared_sim_jump(((ss_player_host *)ctx)->sim, actor, action); }
static qa_movement_continuation ss_host_touch(void *ctx, qa_movement_touch_contact contact, qa_movement_state state) {
  return qa_app_shared_sim_touch(((ss_player_host *)ctx)->sim, contact, state);
}
static qa_weapon_step_result ss_host_weapon_step(void *ctx, qa_weapon_step_input input, qa_movement_state state) {
  (void)state; /* TS weaponStep: input => this.weaponStep(input) */
  return qa_app_shared_sim_weapon_step(((ss_player_host *)ctx)->sim, input);
}
static qa_animation_step_result ss_host_animation_step(void *ctx, qa_animation_step_input input) {
  return qa_app_shared_sim_animation_step(((ss_player_host *)ctx)->sim, input);
}
static bool ss_q3_firing(void *ctx, qa_q3_hook_context context) {
  (void)ctx; return qa_js_bit_and(context.command.buttons, 1) != 0 && context.motion->health > 0;
}
static qa_animation_step_result ss_q3_animation(void *ctx, qa_q3_animation_request request, qa_q3_hook_context context) {
  (void)ctx;
  if (qa_ss_is(context.animation.state.kind, "q3")) return qa_q3_source_animation(request, &context);
  qa_animation_step_result result; memset(&result, 0, sizeof result); result.animation = context.animation; return result;
}
static qa_animation_step_result ss_q3_torso(void *ctx, qa_q3_hook_context context) {
  (void)ctx;
  if (qa_ss_is(context.animation.state.kind, "q3")) return qa_q3_source_torso(11, &context, true);
  qa_animation_step_result result; memset(&result, 0, sizeof result); result.animation = context.animation; return result;
}
static qa_q3_weapon_phase_result ss_q3_weapon(void *ctx, qa_q3_hook_context context) {
  qa_app_shared_simulation *sim = ((ss_player_host *)ctx)->sim;
  qa_app_sim_movement_player *mover = qa_app_shared_sim_player_state(sim, context.input.actor);
  qa_weapon_step_input input; memset(&input, 0, sizeof input);
  input.actor = context.input.actor; input.command = context.input.command; input.frame = context.frame;
  input.arsenal = context.arsenal; input.animation = context.animation; input.environment = context.input.environment;
  input.gauntlet_hit = mover && mover->has_source_movement ? mover->source_movement.gauntlet_hit : false;
  qa_movement_state state; memset(&state, 0, sizeof state);
  state.kind = QA_MOVEMENT_STATE_Q3; state.as.q3 = context.state;
  qa_q3_weapon_phase_result result; memset(&result, 0, sizeof result);
  result.base = context.services.weapon_step(context.services.ctx, input, state);
  /* The weapon step's effects stay with the player that stepped; the hook hands the move a copy it frees. */
  if (result.base.effect_count > 0) {
    qa_movement_effect *copy = malloc(result.base.effect_count * sizeof *copy);
    if (copy == NULL) ss_fail("Out of memory");
    memcpy(copy, result.base.effects, result.base.effect_count * sizeof *copy);
    result.base.effects = copy;
  } else result.base.effects = NULL;
  const qa_q3_arsenal_runtime_state *runtime = qa_ss_map_get(&sim->q3_arsenals, &context.input.actor);
  double mask = QA_Q3_FLAG_RESPAWNED | QA_Q3_FLAG_USE_ITEM_HELD, flags = context.motion->pm_flags;
  if (sim->has_selected_arsenal && (sim->strategy->as_q3(sim) != NULL)) {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_native_by_actor(qa_app_q3_source_runtime_records(sim->source.q3), true, context.input.actor.id);
    if (!entity || !entity->client) ss_fail("Selected Q3-map movement has no source client");
    result.movement_flags = qa_js_bit_or(qa_js_bit_and(flags, qa_js_bit_not(mask)), qa_js_bit_and(entity->client->ps.pm_flags, mask));
    return result;
  }
  result.movement_flags = !runtime ? flags
      : qa_js_bit_or(qa_js_bit_or(qa_js_bit_and(flags, qa_js_bit_not(mask)), runtime->respawned ? QA_Q3_FLAG_RESPAWNED : 0),
          runtime->use_item_held ? QA_Q3_FLAG_USE_ITEM_HELD : 0);
  return result;
}
/* Q1Strategy.movementBindings: the NetQuake binding over the Q1 composition. */
void qa_ss_q1_movement_bindings(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_clock_profile_kind selected_clock, void *player_host,
    qa_app_sim_player_movement_host *host) {
  (void)sim; (void)actor; (void)selected_clock; /* TS Q1Strategy.movementBindings(actor); the host record holds the actor */
  ss_player_host *h = player_host;
  h->netquake = (qa_app_sim_netquake_client_binding){h, false, NULL, NULL, "selected-movement", ss_q1_input, ss_q1_before, ss_q1_think, ss_q1_after};
  host->netquake = &h->netquake;
}
/* QuakeCStrategy.movementBindings: NetQuake, mixed source-client or QuakeWorld bindings of the QC game. */
void qa_ss_quakec_movement_bindings(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_clock_profile_kind selected_clock, void *player_host,
    qa_app_sim_player_movement_host *host) {
  (void)actor; /* the host record holds the actor */
  ss_player_host *h = player_host;
  qa_app_sim_quakec_source *game = sim->source.quakec;
  bool quakeworld = qa_ss_is(qa_app_sim_quakec_source_kind(game), "quakeworld");
  bool mixed_qc = selected_clock != (quakeworld ? QA_CLOCK_Q1_QUAKEWORLD : QA_CLOCK_Q1_NETQUAKE);
  h->qc = game;
  if (qa_ss_is(qa_app_sim_quakec_source_kind(game), "netquake") && !mixed_qc) {
    h->netquake = (qa_app_sim_netquake_client_binding){h, true, ss_qc_read, ss_qc_write, "source-gamecode", ss_qc_input, ss_qc_before, ss_qc_think, ss_qc_after};
    host->netquake = &h->netquake;
    return;
  }
  if (mixed_qc) {
    host->has_source_client = true;
    host->project_state = ss_host_project_state; host->before_movement = ss_host_before_movement; host->after_movement = ss_host_after_movement;
    return;
  }
  if (!quakeworld) return;
  host->has_quakeworld = true;
  host->qw_read = ss_qw_read; host->qw_write = ss_qw_write; host->qw_before_physics = ss_qw_before;
  host->qw_water = ss_qw_water; host->qw_profile = ss_qw_profile;
}
qa_app_sim_movement_player *qa_app_shared_sim_create_player_with(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client,
    qa_vec3 origin, qa_vec3 angles, qa_arsenal_state arsenal, const qa_provider_reference *character_definition,
    const qa_provider_reference *character_appearance) {
  qa_clock_profile_kind selected_clock = qa_app_sim_provider_timing(sim->recipe, sim->recipe.movement.provider).clock.kind;
  ss_player_host *h = qa_app_shared_sim_keep(sim, sizeof *h);
  h->sim = sim; h->actor = actor; h->qc = NULL;
  qa_app_sim_player_movement_host host; memset(&host, 0, sizeof host);
  host.ctx = h;
  host.client_outputs = ss_host_client_outputs;
  host.input_applications_active = sim->mod_client_applications != NULL;
  host.input_begin = ss_host_input_begin; host.input_finish = ss_host_input_finish;
  sim->strategy->movement_bindings(sim, actor, selected_clock, h, &host);
  host.actors = sim->actors; host.bodies = ss_bodies(sim); host.combat = sim->combat;
  host.scene = qa_shared_as_scene_queries(sim->scene);
  host.rerelease_movement = qa_app_sim_shared_physics_rerelease_movement(sim->physics);
  host.q2_movement_config = ss_host_q2_config;
  host.gibbed = ss_host_gibbed; host.speed_multiplier = ss_host_speed_multiplier; host.fixed_pose = ss_host_fixed_pose;
  memset(&host.services, 0, sizeof host.services);
  host.services.ctx = h; host.services.weapon_step = ss_host_weapon_step; host.services.animation_step = ss_host_animation_step;
  host.services.touch = ss_host_touch;
  host.source_punch = ss_host_source_punch; host.world_actor = ss_host_world_actor; host.touch_triggers = ss_host_touch_triggers;
  host.is_brush = ss_host_is_brush; host.jump = ss_host_jump;
  host.q3_hooks = (qa_q3_movement_hooks){h, ss_q3_firing, ss_q3_animation, ss_q3_weapon, ss_q3_torso};
  qa_app_sim_movement_player *player = qa_app_sim_movement_player_create(actor, client, sim->recipe, host, origin, angles, arsenal,
      character_definition, character_appearance);
  h->player = player;
  double gravity = qa_app_sim_shared_physics_gravity(sim->physics);
  player->world_gravity = gravity;
  if (player->state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.gravity = gravity;
  else if (player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE) player->state.as.q2_rerelease.gravity = gravity;
  else if (player->state.kind == QA_MOVEMENT_STATE_Q3) player->state.as.q3.gravity = gravity;
  return player;
}
qa_app_sim_movement_player *qa_app_shared_sim_create_player(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_client_id client,
    qa_vec3 origin, qa_vec3 angles, qa_arsenal_state arsenal) {
  return qa_app_shared_sim_create_player_with(sim, actor, client, origin, angles, arsenal, NULL, NULL);
}

/* ---- shared Q3CharacterActor construction (spawnQ3Player 4258-4268, admitPlayer 4497-4504, restore q3Characters) ---- */
typedef struct ss_q3_character_ctx { qa_app_shared_simulation *sim; qa_content_id content; } ss_q3_character_ctx;
static double ss_q3c_time(void *ctx) { return trunc(ss_time_seconds(((ss_q3_character_ctx *)ctx)->sim) * 1000); }
static void ss_q3c_emit(void *ctx, qa_q3_character_event event) {
  ss_q3_character_ctx *c = ctx;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q3-character";
  source.q3_character = (qa_app_sim_q3_character_presentation_event){event.actor.id, event.sequence, event.time_milliseconds, event.event, event.parameter};
  qa_app_shared_sim_emit(c->sim, c->content, source, NULL, NULL);
}
static qa_q3_character_death_context ss_q3c_death(void *ctx, qa_owned_actor actor) {
  qa_app_shared_simulation *sim = ((ss_q3_character_ctx *)ctx)->sim;
  const qa_attack_provenance *attack = qa_ss_map_get(&sim->last_attack, &actor);
  qa_q3_character_death_context death = {true, false, false, 1022};
  death.suicide = attack && attack->cause.kind == QA_ATTACK_CAUSE_Q3 && attack->cause.means_of_death == 20;
  if (attack && attack->attacker) {
    qa_actor_source_result source = qa_session_actor_registry_source_of(sim->actors, *attack->attacker);
    death.killer_source_slot = source.has_value ? (double)source.slot : 1022;
  }
  return death;
}
static qa_q3_character_actor *ss_new_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_provider_id provider,
    const char *product, qa_content_id content) {
  ss_q3_character_ctx *ctx = qa_app_shared_sim_keep(sim, sizeof *ctx);
  ctx->sim = sim; ctx->content = content;
  qa_q3_character_services services; memset(&services, 0, sizeof services);
  services.bodies = ss_bodies(sim); services.callbacks = sim->callbacks; services.combat = sim->combat; services.inventory = sim->inventory;
  services.ctx = ctx; services.time_milliseconds = ss_q3c_time; services.emit = ss_q3c_emit; services.death_context = ss_q3c_death;
  services.placement = "source-game";
  qa_q3_character_actor *character = calloc(1, sizeof *character);
  if (!character) qa_range_error("Out of memory");
  qa_q3_character_actor_init(character, actor, provider, qa_app_shared_sim_intern(sim, product), services, &sim->death_animations);
  return character;
}
static void ss_set_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q3_character_actor *character) {
  qa_q3_character_actor **previous = qa_ss_map_get(&sim->characters, &actor);
  if (previous && *previous != character) free(*previous);
  qa_ss_map_set(&sim->characters, &actor, &character);
}
/* The restore q3Characters arm (shared_simulation_checkpoint.c) builds the same actor. */
qa_q3_character_actor *qa_app_shared_sim_new_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_provider_id provider,
    const char *product, qa_content_id content) {
  return ss_new_q3_character(sim, actor, provider, product, content);
}
void qa_app_shared_sim_set_q3_character(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q3_character_actor *character) {
  ss_set_q3_character(sim, actor, character);
}

/* ---- reserveNetQuakeClient (4450-4459): userinfo NULL is TS undefined. ---- */
qa_actor_id qa_app_shared_sim_reserve_net_quake_client_with(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_sim_quakec_info *userinfo) {
  qa_app_shared_sim_assert_open(sim);
  if (!qa_app_shared_sim_owns_client(sim, client) || client.slot >= sim->options.max_clients)
    ss_fail("Client does not belong to an available session slot");
  if (!ss_quakec_kind(sim, "netquake")) ss_fail("NetQuake reservation requires its native QuakeC source");
  qa_actor_id existing;
  if (qa_app_sim_quakec_source_client_actor(sim->source.quakec, client, &existing) && qa_app_sim_quakec_source_is_active_client(sim->source.quakec, existing))
    ss_fail("NetQuake client is already active");
  if (userinfo) qa_app_sim_quakec_source_set_client_info(sim->source.quakec, client, *userinfo);
  return qa_app_sim_quakec_source_reserved_client(sim->source.quakec, client).id;
}
qa_actor_id qa_app_shared_sim_reserve_net_quake_client(qa_app_shared_simulation *sim, qa_client_id client) {
  return qa_app_shared_sim_reserve_net_quake_client_with(sim, client, NULL);
}

/* ---- prepareQ3Client (4220-4238) ---- */
qa_owned_actor qa_app_shared_sim_prepare_q3_client(qa_app_shared_simulation *sim, qa_client_id client, qa_app_q3_source_runtime *source, bool bot) {
  /* The builder's character choice belongs to human seats. Q3 is a multiplayer game even in single player:
   * arena bots wear the Enemies selection (map-native Q3 by default), never the local character. */
  qa_provider_reference native_character = {"q3:character", sim->recipe.map.entities.content};
  qa_provider_reference character = bot ? (sim->recipe.enemies.kind == QA_ENEMY_SELECTION_PLAYERS ? sim->recipe.enemies.character : native_character)
      : sim->recipe.character.definition;
  qa_game_family family = qa_app_sim_provider_family(character.provider);
  qa_provider_reference appearance = sim->recipe.character.appearance;
  if (bot) {
    const char *name = family == QA_GAME_FAMILY_Q2 ? "q2" : family == QA_GAME_FAMILY_Q1 ? "q1" : "q3";
    const char *model = family == QA_GAME_FAMILY_Q2 ? "male" : family == QA_GAME_FAMILY_Q1 ? "player" : "sarge";
    char provider[64]; snprintf(provider, sizeof provider, "%s:model/%s", name, model);
    appearance = (qa_provider_reference){qa_app_shared_sim_intern(sim, provider), character.content};
  }
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(sim->actors, sim->recipe.map.entities.provider, client.slot, character.provider);
  qa_arsenal_state arsenal = qa_q3_spawn_loadout(sim->weapon_provider.provider, qa_app_q3_source_runtime_options(source)->product,
      qa_app_q3_source_runtime_game_type(source) == 3);
  qa_app_sim_movement_player *player = qa_app_shared_sim_create_player_with(sim, actor, client, qa_ss_zero, qa_ss_zero, arsenal, &character, &appearance);
  player->owned_arsenal_ammo = arsenal.ammo; /* the spawn loadout's malloc'd ammo, freed when the player's next arsenal replaces it or with the player */
  qa_ss_map_set(&sim->player_states, &actor, &player);
  qa_body_state body; memset(&body, 0, sizeof body);
  body.bounds = player->standing_bounds; body.ground = NULL;
  qa_shared_body_table_create_body(ss_bodies(sim), actor, body);
  qa_gameplay_authority_create_state(sim->combat, actor, qa_q3_initial_combat("100", NULL), NULL, NULL);
  qa_shared_inventory_table_create_entries(sim->inventory, actor, arsenal.ammo, arsenal.ammo_count);
  return actor;
}
/* ---- admitQ3Player (4240-4250) ---- */
/* takeAdmissionSettleMilliseconds: source time that admission ran on its own; the host's clock must account for it so
 * player commands stay on the level clock. */
double qa_app_shared_sim_take_admission_settle_milliseconds(qa_app_shared_simulation *sim) {
  double settled = sim->admission_settle_milliseconds;
  sim->admission_settle_milliseconds = 0;
  return settled;
}
qa_app_sim_player_admission qa_app_shared_sim_admit_q3_player(qa_app_shared_simulation *sim, qa_client_id client, qa_app_q3_source_runtime *source) {
  /* sv_init.c runs three frames before any client connects so spawn-time items (FinishSpawningItem) have published. */
  if (!qa_ss_map_has(&sim->settled_q3_sources, &source) && qa_app_q3_source_runtime_frame_num(source) == 0 && !sim->stepping
      && qa_ss_is(sim->source_round_settlement.kind, "none")) {
    qa_ss_map_set(&sim->settled_q3_sources, &source, NULL);
    for (int frame = 0; frame < 3; frame++) qa_app_shared_sim_step(sim, (qa_input_batch){100, NULL, 0});
    sim->admission_settle_milliseconds += 300;
  }
  qa_owned_actor actor = qa_app_shared_sim_prepare_q3_client(sim, client, source, false);
  qa_app_q3_source_runtime_admit_player(source, actor, client.slot);
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_admit(sim->hand_grenades, actor.id, NULL);
  qa_app_shared_sim_admit_grapple(sim, actor.id);
  qa_app_shared_sim_notify_client_event(sim, "admitted", actor.id);
  return (qa_app_sim_player_admission){actor.id, qa_app_shared_sim_require_player(sim, actor.id)->view_height};
}

/* entryCarry.set(actor, state): the map owns its travel states. */
static void ss_set_entry_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_travel_state state) {
  qa_q1_travel_state *previous = qa_ss_map_get(&sim->entry_carry, &actor);
  if (previous) qa_q1_travel_state_destroy(previous);
  qa_ss_map_set(&sim->entry_carry, &actor, &state);
}
/* ---- admitPlayer (4461-4554): travel NULL is TS undefined (the public form passes options.travel), userinfo NULL likewise. ---- */
static void ss_holster_into_slot(qa_app_shared_simulation *sim, qa_actor_id actor, const qa_app_sim_travel_player *carried) {
  if (!carried || !carried->has_weapon_slot || !qa_ss_map_has(&sim->weapon_slots, &actor)) return;
  qa_app_primary_weapon_handoff handoff = qa_app_shared_sim_primary_handoff(sim, actor);
  handoff.holster(handoff.ctx);
  qa_app_sim_weapon_slot_restore_state state; memset(&state, 0, sizeof state);
  state.kind = "holstering-primary"; state.next = (qa_weapon_reference){carried->weapon_provider, carried->weapon_item};
  qa_app_shared_sim_bind_weapon_slot_state(sim, actor, &state);
  qa_app_sim_weapon_slot **slot = qa_ss_map_get(&sim->weapon_slots, &actor);
  if (slot) qa_app_sim_weapon_slot_reconcile(*slot);
}
/* TS admitQuakeCPlayer: the QuakeC program connects the client through its own parms. */
qa_app_sim_player_admission qa_app_shared_sim_admit_quake_c_player(qa_app_shared_simulation *sim, qa_client_id client,
    const qa_app_simulation_travel *travel, const char *userinfo) {
  qa_ss_source_runtime *source = &sim->source;
  /* Cross-program travel (TS Q1/Q2/Q3/QVM into a QC world) connects fresh through the program's own parms, then
   * lands its shared carry after spawn; only the QW/NQ ABI split stays exclusive. */
  const char *game_kind = qa_app_sim_quakec_source_kind(source->quakec);
  if ((travel && (qa_ss_is(travel->source_kind, "netquake") || qa_ss_is(travel->source_kind, "quakeworld")) && !qa_ss_is(travel->source_kind, game_kind))
      || (sim->options.dedicated && qa_app_sim_provider_family(sim->recipe.character.definition.provider) != QA_GAME_FAMILY_Q1))
    ss_fail("QC clients require matching source travel and native wire servers require Q1 character presentation");
  if (userinfo) {
    ss_info info = ss_q2_userinfo(userinfo);
    qa_app_sim_quakec_source_set_client_info(source->quakec, client, (qa_app_sim_quakec_info){info.items, info.count});
    ss_info_free(&info);
  }
  qa_owned_actor actor = qa_app_sim_quakec_source_admit_client(source->quakec, client);
  qa_body_state body = ss_require_body(sim, actor.id, "QC reserved client has no shared body");
  qa_app_sim_movement_player *player = qa_app_shared_sim_create_player(sim, actor, client, body.origin, body.angles,
      qa_app_sim_quakec_source_client_arsenal(source->quakec, actor.id));
  qa_ss_map_set(&sim->player_states, &actor, &player);
  player->state = qa_app_sim_movement_player_read_state(player);
  if (player->character == QA_GAME_FAMILY_Q1) player->animation = qa_app_sim_quakec_source_client_animation(source->quakec, actor.id);
  player->bounds = body.bounds; player->view_angles = body.angles; player->command_angles = body.angles;
  qa_app_shared_sim_sync_quake_c_client_view(sim, player);
  qa_app_shared_sim_restore_selected_travel(sim, player, ss_travel_find(travel, client));
  qa_app_shared_sim_bind_selected_original_pickups(sim, actor);
  qa_app_shared_sim_notify_client_event(sim, "admitted", actor.id);
  return (qa_app_sim_player_admission){actor.id, player->view_height};
}
/* TS spawnQ1Player(source, actor, player). */
void qa_app_shared_sim_spawn_q1_player(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_app_sim_movement_player *player) {
  qa_ss_source_runtime *source = &sim->source;
  qa_q1_actor *selected = qa_comp_q1_source_composition_select_spawn(source->q1_composition, &actor.id, false);
  if (!selected) ss_fail("Source player spawn is deferred");
  qa_q1_body spot = qa_q1_get_body(source->q1_game, selected);
  qa_ss_optional_actor start = {true, selected->actor.id};
  qa_ss_map_set(&sim->character_starts, &actor, &start);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "spawn"; change.origin = qa_ss_add(spot.origin, (qa_vec3){0, 0, 1}); change.velocity = qa_ss_zero;
  change.angles = spot.angles; change.command_angles = player->command_angles; change.hold_milliseconds = 0; change.spectator = false;
  qa_app_shared_sim_set_player_movement(sim, actor.id, change);
  qa_app_shared_sim_kill_box(sim, actor); qa_shared_body_table_link(ss_bodies(sim), actor, NULL);
  qa_q1_use_targets(source->q1_game, selected, &actor.id);
  qa_comp_q1_source_composition_spawned(source->q1_composition, actor.id, true, selected);
  ss_set_entry_carry(sim, actor, qa_comp_q1_source_composition_capture_travel(source->q1_composition, actor));
}
void qa_app_shared_sim_set_entry_carry(qa_app_shared_simulation *sim, qa_owned_actor actor, qa_q1_travel_state state) {
  ss_set_entry_carry(sim, actor, state);
}
qa_app_sim_player_admission qa_app_shared_sim_admit_player_with(qa_app_shared_simulation *sim, qa_client_id client,
    const qa_app_simulation_travel *travel, const char *userinfo) {
  qa_app_shared_sim_assert_open(sim);
  if (!qa_app_shared_sim_owns_client(sim, client) || client.slot >= sim->options.max_clients)
    ss_fail("Client does not belong to an available session slot");
  for (size_t i = 0; i < sim->player_states.count; i++)
    if ((*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i))->client.slot == client.slot) ss_fail("Client already has a player");
  qa_app_sim_player_admission admitted;
  if (sim->strategy->admit_client(sim, client, travel, userinfo, &admitted)) return admitted;
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(sim->actors, sim->recipe.map.entities.provider, client.slot + 1,
      sim->recipe.character.definition.provider);
  qa_arsenal_state arsenal; memset(&arsenal, 0, sizeof arsenal);
  arsenal.provider = sim->weapon_provider.provider; arsenal.active_weapon = NULL; arsenal.ammo = NULL; arsenal.ammo_count = 0;
  arsenal.state = sim->strategy->initial_arsenal_state(sim);
  qa_app_sim_movement_player *player = qa_app_shared_sim_create_player(sim, actor, client, qa_ss_zero, qa_ss_zero, arsenal);
  qa_ss_map_set(&sim->player_states, &actor, &player);
  qa_body_state body; memset(&body, 0, sizeof body);
  body.origin = qa_app_sim_movement_origin(player->state); body.bounds = player->bounds; body.ground = NULL;
  qa_shared_body_table_create_body(ss_bodies(sim), actor, body);
  qa_q2_entity *entity = sim->strategy->attach_entity(sim, actor, player);
  if (player->character == QA_GAME_FAMILY_Q3) {
    qa_ss_optional_actor none; memset(&none, 0, sizeof none);
    qa_ss_map_set(&sim->character_starts, &actor, &none);
    qa_q3_character_actor *character = ss_new_q3_character(sim, actor, sim->recipe.character.definition.provider, "baseq3",
        sim->recipe.character.definition.content);
    ss_set_q3_character(sim, actor, character);
    qa_q3_character_actor_spawn(character, (qa_q3_character_spawn){body, qa_q3_initial_combat("100", NULL), NULL, 0});
    player->animation = qa_q3_character_actor_get_animation(character);
  } else {
    qa_combat_state combat; memset(&combat, 0, sizeof combat);
    combat.health = 100; combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE; combat.armor.powered.kind = "none";
    combat.mass = 200; combat.can_take_damage = true; combat.invulnerable = false; combat.team = NULL;
    qa_gameplay_authority_create_state(sim->combat, actor, combat, NULL, NULL);
    qa_shared_inventory_table_create_entries(sim->inventory, actor, NULL, 0);
  }
  sim->strategy->configure_admitted(sim, actor, client, player, entity, userinfo, travel);
  if (player->character == QA_GAME_FAMILY_Q1) qa_app_shared_sim_attach_q1_character(sim, player);
  sim->strategy->attach_foreign_character(sim, player);
  const qa_app_sim_travel_player *carried_player = NULL;
  if (travel) for (size_t i = 0; i < travel->player_count; i++)
    if (travel->players[i].client.slot == client.slot && travel->players[i].client.generation == client.generation) { carried_player = &travel->players[i]; break; }
  bool native_carry = sim->strategy->admit_carry(sim, actor, entity, carried_player);
  player->state = qa_app_sim_movement_player_read_state(player);
  qa_app_shared_sim_restore_selected_travel(sim, player, carried_player);
  player->arsenal = qa_app_shared_sim_arsenal(sim, player);
  sim->strategy->enter_world(sim, actor, player, carried_player == NULL);
  /* Cross-program travel lands after the destination's own spawn so its loadout reset cannot discard the carried health and inventory. */
  if (!native_carry && carried_player) {
    qa_app_shared_sim_apply_shared_travel_carry(sim, actor, carried_player);
    player->state = qa_app_sim_movement_player_read_state(player); player->arsenal = qa_app_shared_sim_arsenal(sim, player);
    qa_q1_travel_state entry_carry;
    if (sim->strategy->capture_entry_carry(sim, actor, &entry_carry)) ss_set_entry_carry(sim, actor, entry_carry);
  }
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_admit(sim->hand_grenades, actor.id, carried_player ? carried_player->hand_grenades : NULL);
  qa_app_shared_sim_admit_grapple(sim, actor.id);
  ss_holster_into_slot(sim, actor.id, carried_player);
  qa_app_shared_sim_notify_client_event(sim, "admitted", actor.id);
  return (qa_app_sim_player_admission){actor.id, player->view_height};
}
qa_app_sim_player_admission qa_app_shared_sim_admit_player(qa_app_shared_simulation *sim, qa_client_id client) {
  return qa_app_shared_sim_admit_player_with(sim, client, sim->options.has_travel ? &sim->options.travel : NULL, NULL);
}
/* ---- admitTravel (8042) ---- */
qa_app_sim_player_admission qa_app_shared_sim_admit_travel(qa_app_shared_simulation *sim, qa_client_id client, const qa_app_simulation_travel *travel) {
  return qa_app_shared_sim_admit_player_with(sim, client, travel, NULL);
}
/* ---- prepareBotClient (4207-4218) ---- */
qa_owned_actor qa_app_shared_sim_prepare_bot_client(qa_app_shared_simulation *sim, qa_client_id client) {
  qa_app_shared_sim_assert_open(sim);
  return sim->strategy->prepare_bot(sim, client);
}
/* TS botSlotAvailable(client): an owned, free session slot. */
bool qa_app_shared_sim_bot_slot_available(qa_app_shared_simulation *sim, qa_client_id client) {
  bool taken = false;
  for (size_t i = 0; i < sim->player_states.count; i++)
    if ((*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i))->client.slot == client.slot) taken = true;
  return qa_app_shared_sim_owns_client(sim, client) && client.slot < sim->options.max_clients && !taken;
}

/* ---- captureTravel (7924-7965) ----
 * The returned SimulationTravel outlives the simulation (it seeds the next
 * world), so every record is a deep copy held by a thread-local arena that
 * the next captureTravel call releases. */
typedef struct ss_travel_block { void *block; void (*release)(void *block); } ss_travel_block;
static __thread ss_travel_block *ss_travel_blocks; static __thread size_t ss_travel_block_count, ss_travel_block_capacity;
static void *ss_travel_keep(void *block, void (*release)(void *block)) {
  if (!block) qa_range_error("Out of memory");
  ss_travel_blocks = ss_grow(ss_travel_blocks, &ss_travel_block_capacity, ss_travel_block_count + 1, sizeof *ss_travel_blocks);
  ss_travel_blocks[ss_travel_block_count++] = (ss_travel_block){block, release};
  return block;
}
static void *ss_travel_alloc(size_t size) { return ss_travel_keep(calloc(1, size ? size : 1), free); }
static const char *ss_travel_text(const char *text) { return text ? ss_travel_keep(ss_dup(text), free) : NULL; }
static void ss_travel_release_all(void) {
  for (size_t i = ss_travel_block_count; i-- > 0;) ss_travel_blocks[i].release(ss_travel_blocks[i].block);
  ss_travel_block_count = 0;
}
static void ss_release_q1_travel(void *block) { qa_q1_travel_state_destroy(block); free(block); }
static void ss_release_q2_carry(void *block) { qa_q2_base_player_carry_clear(block); free(block); }
static void ss_release_arsenal_travel(void *block) { qa_ss_selected_arsenal_travel_free(block); free(block); }
static void ss_release_lmctf_travel(void *block) { qa_lmctf_travel_destroy(block); free(block); }
static void ss_release_campaign(void *block) { qa_q2_rerelease_campaign_destroy(block); free(block); }
static const qa_inventory_entry *ss_travel_inventory(const qa_inventory_entry *entries, size_t count) {
  qa_inventory_entry *copy = ss_travel_alloc(count * sizeof *copy);
  for (size_t i = 0; i < count; i++) { copy[i] = entries[i]; copy[i].item = ss_travel_text(entries[i].item); }
  return copy;
}
static const qa_app_sim_quakec_source_travel *ss_travel_quakec(qa_app_sim_quakec_source_travel source) {
  qa_app_sim_quakec_source_travel *copy = ss_travel_alloc(sizeof *copy);
  copy->kind = ss_travel_text(source.kind); copy->server_flags = source.server_flags;
  qa_app_sim_cvar_name_value *cvars = ss_travel_alloc(source.cvar_count * sizeof *cvars);
  for (size_t i = 0; i < source.cvar_count; i++) cvars[i] = (qa_app_sim_cvar_name_value){ss_travel_text(source.cvars[i].name), ss_travel_text(source.cvars[i].value)};
  copy->cvars = cvars; copy->cvar_count = source.cvar_count;
  qa_app_sim_quakec_client_travel *clients = ss_travel_alloc(source.client_count * sizeof *clients);
  for (size_t i = 0; i < source.client_count; i++) {
    const qa_app_sim_quakec_client_travel *client = &source.clients[i];
    clients[i].client = client->client; clients[i].role = ss_travel_text(client->role);
    double *parameters = ss_travel_alloc(client->parameter_count * sizeof *parameters);
    if (client->parameter_count) memcpy(parameters, client->parameters, client->parameter_count * sizeof *parameters);
    clients[i].parameters = parameters; clients[i].parameter_count = client->parameter_count;
    qa_app_sim_cvar_name_value *info = ss_travel_alloc(client->userinfo_count * sizeof *info);
    for (size_t j = 0; j < client->userinfo_count; j++) info[j] = (qa_app_sim_cvar_name_value){ss_travel_text(client->userinfo[j].name), ss_travel_text(client->userinfo[j].value)};
    clients[i].userinfo = info; clients[i].userinfo_count = client->userinfo_count;
  }
  copy->clients = clients; copy->client_count = source.client_count;
  return copy;
}
/* SourceStrategy.travelSource per class: the travel's source record (C: the source fields of the travel). */
void qa_ss_loading_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel) {
  (void)sim; (void)context; (void)travel;
  ss_fail("Source campaign travel requires its native source file lifecycle");
}
void qa_ss_q1_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel) {
  (void)context;
  travel->source_kind = "q1"; travel->flags = sim->q1_campaign.flags; travel->skill = sim->q1_campaign.skill;
}
void qa_ss_quakec_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel) {
  (void)context;
  travel->quakec = ss_travel_quakec(qa_app_sim_quakec_source_capture_travel(sim->source.quakec));
  travel->source_kind = travel->quakec->kind; travel->server_flags = travel->quakec->server_flags;
}
void qa_ss_shared_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel) {
  (void)sim; (void)context;
  travel->source_kind = "shared";
}
void qa_ss_q2_travel_source(qa_app_shared_simulation *sim, const qa_app_source_travel_context *context, qa_app_simulation_travel *travel) {
  qa_q2_product_runtime *product = sim->source.product;
  travel->source_kind = "q2";
  travel->server_flags = context->has_server_flags ? context->server_flags : product->services->counters->server_flags;
  if (product->match.source_kind == QA_Q2_MATCH_LMCTF && product->match.lmctf) {
    qa_lmctf_travel *lmctf = ss_travel_keep(calloc(1, sizeof *lmctf), ss_release_lmctf_travel);
    qa_q2_lmctf_capture_travel(product->match.lmctf, lmctf);
    travel->lmctf = lmctf;
  }
  if (product->rerelease) {
    qa_q2_rerelease_campaign_state *campaign = ss_travel_keep(calloc(1, sizeof *campaign), ss_release_campaign);
    if (!qa_q2_rerelease_campaign_clone(campaign, &product->rerelease->entities.base.campaign)) qa_range_error("Out of memory");
    travel->rerelease = campaign;
  }
  if (context->has_landmark) {
    travel->has_landmark = true; travel->landmark_client_slot = context->landmark_client_slot;
    travel->landmark_name = ss_travel_text(sim->level_change_landmark.name);
    travel->landmark_relative_origin = sim->level_change_landmark.relative_origin;
    travel->landmark_relative_velocity = sim->level_change_landmark.relative_velocity;
    travel->landmark_relative_view_angles = sim->level_change_landmark.relative_view_angles;
  }
}
/* Q3QvmStrategy.travelers */
bool qa_ss_q3_qvm_travelers(qa_app_shared_simulation *sim, qa_client_id *clients, qa_actor_id *actors, size_t capacity, size_t *out_count) {
  qa_app_q3_player players[64];
  size_t total = qa_app_q3_qvm_server_game_players(sim->source.qvm, players, 64), count = 0;
  for (size_t i = 0; i < total && count < capacity; i++) { clients[count] = players[i].client; actors[count++] = players[i].actor; }
  *out_count = count;
  return true;
}
/* SourceStrategy.travelPlayerState per class. */
void qa_ss_loading_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry) {
  (void)sim; (void)actor; (void)entry;
  ss_fail("Source campaign travel requires its native source file lifecycle");
}
/* this.host.sharedTravelState(actor) ?? { kind: "quakec" } */
void qa_ss_shared_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry) {
  qa_ss_shared_carry shared;
  if (qa_app_shared_sim_shared_travel_state(sim, actor, &shared)) {
    entry->state_kind = "shared"; entry->health = shared.health; entry->armor = shared.armor;
    entry->inventory = ss_travel_inventory(shared.inventory, shared.inventory_count); entry->inventory_count = shared.inventory_count;
  } else entry->state_kind = "quakec";
}
static qa_owned_actor ss_travel_require_owned(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(sim->actors, actor, &owned)) ss_fail("Travel player actor is missing");
  return owned;
}
void qa_ss_q1_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry) {
  qa_comp_q1_source_composition *composition = sim->source.q1_composition;
  qa_q1_travel_state *carry = ss_travel_keep(calloc(1, sizeof *carry), ss_release_q1_travel);
  *carry = sim->q1_restart ? qa_comp_q1_source_composition_new_travel(composition)
      : qa_comp_q1_source_composition_capture_travel(composition, ss_travel_require_owned(sim, actor));
  entry->state_kind = "q1"; entry->carry = carry;
}
void qa_ss_q2_travel_player_state(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_travel_player *entry) {
  qa_q2_product_runtime *product = sim->source.product;
  ss_travel_require_owned(sim, actor);
  qa_q2_entity *entity = ss_q2_entity(sim, actor);
  if (!entity) ss_fail("Q2 travel player entity is missing");
  qa_q2_base_player_carry *carry = ss_travel_keep(calloc(1, sizeof *carry), ss_release_q2_carry);
  *carry = qa_q2_players_save_carry(product->players, entity, product->services);
  if (product->match.source_kind == QA_Q2_MATCH_LMCTF) carry->score = 0;
  entry->state_kind = "q2"; entry->carry = carry;
}
qa_app_simulation_travel qa_app_shared_sim_capture_travel(qa_app_shared_simulation *sim, const char *spawn_point) {
  qa_app_shared_sim_assert_open(sim);
  ss_travel_release_all();
  for (size_t i = 0; i < sim->player_states.count; i++)
    if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, (*(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i))->actor.id);
  bool has_landmark = sim->has_level_change && sim->has_level_change_landmark;
  qa_app_sim_movement_player *landmark_player = has_landmark ? qa_app_shared_sim_player(sim, sim->level_change_landmark.player) : NULL;
  qa_app_simulation_travel travel; memset(&travel, 0, sizeof travel);
  travel.spawn_point = ss_travel_text(spawn_point ? spawn_point : "");
  qa_app_source_travel_context context; memset(&context, 0, sizeof context);
  context.has_server_flags = sim->has_level_change; context.server_flags = sim->level_change_server_flags;
  context.has_landmark = has_landmark && landmark_player != NULL;
  if (context.has_landmark) context.landmark_client_slot = (double)landmark_player->client.slot;
  sim->strategy->travel_source(sim, &context, &travel);
  /* Native QuakeWorld travel carries its clients in the source record; shared movement players must not be duplicated alongside them. */
  if (qa_ss_is(travel.source_kind, "quakeworld")) return travel;
  size_t count = 0;
  qa_client_id clients[256]; qa_actor_id actors[256];
  if (!sim->strategy->travelers(sim, clients, actors, 256, &count)) for (size_t i = 0; i < sim->player_states.count && count < 256; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    clients[count] = player->client; actors[count++] = player->actor.id;
  }
  qa_app_sim_travel_player *players = ss_travel_alloc(count * sizeof *players);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    qa_app_sim_travel_player *entry = &players[i];
    entry->client = clients[i];
    if (!sim->q1_restart && sim->hand_grenades) {
      qa_app_sim_hand_grenade_travel grenades;
      if (qa_app_sim_hand_grenade_runtime_travel(sim->hand_grenades, actor, &grenades)) {
        qa_app_sim_hand_grenade_travel *copy = ss_travel_alloc(sizeof *copy);
        *copy = grenades; copy->ammo.item = ss_travel_text(grenades.ammo.item);
        entry->hand_grenades = copy;
      }
    }
    if (qa_ss_map_has(&sim->weapon_slots, &actor) && !sim->q1_restart) {
      qa_app_sim_weapon_slot_projection projection = qa_app_shared_sim_slot_projection(sim, actor, NULL);
      const qa_weapon_reference *target = projection.pending ? projection.pending : projection.active;
      if (target) { entry->has_weapon_slot = true; entry->weapon_provider = ss_travel_text(target->provider); entry->weapon_item = ss_travel_text(target->item); }
    }
    if (!sim->q1_restart) {
      qa_ss_selected_arsenal_travel *selected = ss_travel_keep(calloc(1, sizeof *selected), ss_release_arsenal_travel);
      if (qa_app_shared_sim_capture_selected_arsenal_travel(sim, actor, selected)) { entry->selected_arsenal = selected; entry->selected_arsenal_kind = selected->kind; }
    }
    sim->strategy->travel_player_state(sim, actor, entry);
  }
  travel.players = players; travel.player_count = count;
  return travel;
}

/* ---- restoreCampaignTravel (8044-8078) ---- */
void qa_app_shared_sim_restore_campaign_travel(qa_app_shared_simulation *sim, const qa_app_simulation_travel *travel) {
  qa_ss_source_runtime *source = &sim->source;
  if ((sim->strategy->as_q2(sim) == NULL) || !qa_ss_is(travel->source_kind, "q2")) {
    /* Other programs have no revisit carry of their own; the restored save is authoritative and only the shared travel snapshot lands. */
    for (size_t i = 0; i < sim->player_states.count; i++) {
      qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
      qa_app_shared_sim_apply_shared_travel_once(sim, player->actor, player->client);
    }
    return;
  }
  qa_q2_product_runtime *product = source->product;
  product->services->counters->server_flags = travel->server_flags;
  if (travel->rerelease) {
    if (!product->rerelease) ss_fail("Rerelease campaign carry requires matching source state");
    qa_q2_rerelease_module_restore_campaign(&product->rerelease->entities, travel->rerelease);
  }
  qa_q2_intermission playing; memset(&playing, 0, sizeof playing); playing.kind = "playing";
  qa_q2_players_set_intermission(product->players, playing);
  free(sim->level_change_map); sim->level_change_map = NULL; sim->has_level_change = false; sim->has_level_change_landmark = false;
  qa_event_ring_reset(&sim->transitions);
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    const qa_app_sim_travel_player *carried = ss_travel_find(travel, player->client);
    qa_q2_entity *entity = ss_q2_entity(sim, player->actor.id);
    qa_q2_base_player_state *state = ss_q2_state(sim, player->actor.id);
    if (!carried || !qa_ss_is(carried->state_kind, "q2") || !entity || !state)
      ss_fail("Campaign revisit requires current carry for every restored player");
    const qa_q2_base_player_carry *carry = carried->carry;
    qa_q2_players_restore_carry(product->players, entity, product->services, *carry);
    if (product->services->options.mode == QA_Q2_MODE_COOP) {
      state->has_coop_respawn = true; qa_q2_base_player_carry_set(&state->coop_respawn, *carry);
    }
    player->intermission = false; player->has_cutscene = false;
    qa_q2_landmark_carry landmark;
    bool has_landmark = travel->has_landmark && travel->landmark_client_slot == player->client.slot;
    if (has_landmark) {
      landmark.player = player->actor.id; landmark.name = travel->landmark_name; landmark.relative_origin = travel->landmark_relative_origin;
      landmark.relative_velocity = travel->landmark_relative_velocity; landmark.relative_view_angles = travel->landmark_relative_view_angles;
    }
    qa_q2_players_put_in_server(product->players, entity, product->services, false, has_landmark ? &landmark : NULL);
    qa_app_shared_sim_restore_selected_travel(sim, player, carried);
    player->arsenal = qa_app_shared_sim_arsenal(sim, player); player->state = qa_app_sim_movement_player_read_state(player);
    if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_admit(sim->hand_grenades, player->actor.id, carried->hand_grenades);
    ss_holster_into_slot(sim, player->actor.id, carried);
  }
  qa_app_shared_sim_resume_q2_presentation(sim, NULL);
}

/* ---- captureOriginalSave (8080-8096): format 5 | 6 (TS Q1SaveData["format"]). ---- */
static bool ss_source_owned(const qa_executable_recipe *recipe, qa_provider_reference reference) {
  return qa_ss_is(reference.content, recipe->map.entities.content) && qa_ss_is(reference.provider, recipe->map.entities.provider);
}
static bool ss_starts(const char *text, const char *prefix) { return text && strncmp(text, prefix, strlen(prefix)) == 0; }
bool qa_app_shared_sim_capture_original_save(qa_app_shared_simulation *sim, int format_version, const char *game_directories,
    const char *comment, qa_q1_save_data *out) {
  qa_app_shared_sim_assert_checkpoint_ready(sim);
  if ((sim->strategy->as_quakec(sim) == NULL)) ss_fail("Original Quake export requires an idle NetQuake source");
  const qa_executable_recipe *recipe = &sim->recipe;
  bool weapons_owned = true;
  for (size_t i = 0; i < recipe->weapons.count; i++) if (!ss_source_owned(recipe, recipe->weapons.values[i])) weapons_owned = false;
  qa_provider_reference owned[] = {recipe->combat, recipe->inventory, recipe->engine_behavior, recipe->transition,
      recipe->presentation.hud, recipe->presentation.effects, recipe->presentation.audio};
  bool all_owned = true;
  for (size_t i = 0; i < sizeof owned / sizeof *owned; i++) if (!ss_source_owned(recipe, owned[i])) all_owned = false;
  if (sim->player_states.count != 1 || !qa_ss_is(recipe->map.geometry_content, recipe->map.entities.content)
      || !qa_ss_is(recipe->movement.provider, "q1:movement") || !ss_starts(recipe->movement.content, "q1:")
      || !qa_ss_is(recipe->character.definition.provider, "q1:character") || !ss_starts(recipe->character.definition.content, "q1:")
      || !qa_ss_is(recipe->character.appearance.provider, "q1:model/player") || !ss_starts(recipe->character.appearance.content, "q1:")
      || recipe->weapons.count != 1 || !weapons_owned || !all_owned
      || !qa_ss_is(recipe->presentation.assets, recipe->map.entities.content)
      || recipe->enemies.kind != QA_ENEMY_SELECTION_MAP_DEFINED || recipe->weapon_behavior_count != 0
      || recipe->equipment.grapple.is_enabled || recipe->equipment.hand_grenades.is_enabled)
    ss_fail("Original Quake formats cannot preserve this mixed composition; use a shared save");
  return qa_app_sim_quakec_source_capture_original_save(sim->source.quakec, format_version, game_directories, comment, out);
}
/* ---- restoreOriginalSave (8098-8111) ---- */
void qa_app_shared_sim_restore_original_save(qa_app_shared_simulation *sim, const qa_q1_save_data *save, qa_q1_unknown_save_fields *out_unknown) {
  qa_app_shared_sim_assert_open(sim);
  if (!sim->options.original_save_candidate || (sim->strategy->as_quakec(sim) == NULL) || sim->stepping || ss_time_seconds(sim) != save->time)
    ss_fail("Original save restoration requires a staged QuakeC world at the saved source time");
  qa_app_sim_quakec_source_restore_original_save(sim->source.quakec, save, out_unknown);
  for (size_t i = 0; i < sim->player_states.count; i++) {
    qa_app_sim_movement_player *player = *(qa_app_sim_movement_player **)qa_ss_map_value_at(&sim->player_states, i);
    player->arsenal = qa_app_sim_quakec_source_client_arsenal(sim->source.quakec, player->actor.id);
    if (player->character == QA_GAME_FAMILY_Q1) player->animation = qa_app_sim_quakec_source_client_animation(sim->source.quakec, player->actor.id);
    player->state = qa_app_sim_movement_player_read_state(player);
    qa_app_shared_sim_sync_quake_c_client_view(sim, player);
  }
}
/* ---- checkpointModsForTravel (8142-8148): false = TS undefined; the entries are the owner's (free the array). ---- */
typedef struct ss_mod_travel { qa_app_shared_simulation *sim; qa_mod_travel_entry **mods; size_t *count; } ss_mod_travel;
static void ss_checkpoint_for_travel(void *ctx) { ss_mod_travel *t = ctx; qa_session_mods_checkpoint_for_travel(t->sim->mod_owner, t->mods, t->count); }
bool qa_app_shared_sim_checkpoint_mods_for_travel(qa_app_shared_simulation *sim, qa_mod_travel_entry **out_mods, size_t *out_count) {
  qa_app_shared_sim_assert_open(sim);
  if (sim->stepping || sim->checkpoint_in_progress) ss_fail("Mod checkpoint requires a completed frame");
  if (!sim->mod_owner) return false;
  sim->checkpoint_in_progress = true;
  ss_mod_travel travel = {sim, out_mods, out_count};
  qa_caught_error caught;
  bool ok = qa_try(ss_checkpoint_for_travel, &travel, &caught);
  sim->checkpoint_in_progress = false; /* finally */
  if (!ok) qa_rethrow(&caught);
  return true;
}

/* ---- playerCommand (7805-7922) ---- */
typedef struct ss_native_command { qa_app_shared_simulation *sim; double slot; const char *const *argv; size_t argc; const char *args; } ss_native_command;
static void ss_native_command_run(void *ctx) {
  ss_native_command *c = ctx;
  if (qa_ss_is(c->sim->source.edition, "classic")) qa_app_sim_classic_guest_world_command(c->sim->source.classic_world, c->slot, c->argv, c->argc, c->args);
  else qa_app_sim_rerelease_guest_world_command(c->sim->source.rerelease_world, c->slot, c->argv, c->argc, c->args);
}
/* args.join(separator), malloc'd. */
static char *ss_join(const char *const *args, size_t count, const char *separator) {
  size_t length = 1;
  for (size_t i = 0; i < count; i++) length += strlen(args[i]) + strlen(separator);
  char *text = malloc(length);
  if (!text) qa_range_error("Out of memory");
  text[0] = '\0';
  for (size_t i = 0; i < count; i++) { if (i) strcat(text, separator); strcat(text, args[i]); }
  return text;
}
/* args.join("").toLowerCase().replaceAll(" ", "") (ASCII lowering, see source_items.h). */
static char *ss_requested(const char *const *args, size_t count) {
  char *joined = ss_join(args, count, ""), *out = joined;
  for (const char *c = joined; *c; c++) if (*c != ' ') *out++ = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
  *out = '\0';
  return joined;
}
static void ss_q1_message(qa_app_shared_simulation *sim, qa_actor_id actor, const char *text) { qa_q1_message(sim->source.q1_game, &actor, text, false, NULL, 0); }
/* (index + (next ? 1 : length - 1)) % length over an owned list; -1 when the list is empty. */
static long ss_cycle(long index, bool next, size_t length) {
  if (length == 0) return -1;
  return (long)(((double)index + (next ? 1 : (double)length - 1)) - floor(((double)index + (next ? 1 : (double)length - 1)) / (double)length) * (double)length);
}
void qa_app_shared_sim_player_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  if (qa_app_shared_sim_source_item_command(sim, actor, name, args, arg_count)
      || qa_app_shared_sim_selected_weapon_command(sim, actor, name, args, arg_count)) return;
  if (sim->strategy->pre_command(sim, actor, name, args, arg_count)) return;
  if (qa_ss_is(name, "giveall")) { const char *all[] = {"all"}; qa_app_shared_sim_player_command(sim, actor, "give", all, 1); return; }
  if (qa_ss_is(name, "suicide")) { qa_app_shared_sim_player_command(sim, actor, "kill", args, arg_count); return; }
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  bool cycle = qa_ss_is(name, "weapnext") || qa_ss_is(name, "weapprev"), use = qa_ss_is(name, "use");
  if (qa_ss_map_has(&sim->weapon_slots, &actor) && (cycle || use)) {
    qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(sim, actor);
    qa_source_item_named_entry *rows = malloc((ui.item_count + 1) * sizeof *rows);
    const qa_app_sim_player_ui_item **owned = malloc((ui.item_count + 1) * sizeof *owned);
    if (!rows || !owned) qa_range_error("Out of memory");
    size_t count = 0;
    for (size_t i = 0; i < ui.item_count; i++)
      if (qa_ss_is(ui.items[i].kind, "weapon") && ui.items[i].owned) { owned[count] = &ui.items[i]; rows[count++] = (qa_source_item_named_entry){ui.items[i].id, ui.items[i].label}; }
    const qa_app_sim_player_ui_item *selected = NULL;
    if (use) {
      char *text = ss_join(args, arg_count, " ");
      qa_source_item_named_result choice = qa_source_item_named(rows, count, text, NULL, NULL, 0);
      if (choice.kind == QA_SOURCE_ITEM_NAMED_NONE) {
        char *joined = ss_join(args, arg_count, ""), *weapon = malloc(strlen(joined) + 11);
        if (!weapon) qa_range_error("Out of memory");
        sprintf(weapon, "q1:weapon/%s", joined); free(joined);
        choice = qa_source_item_named(rows, count, weapon, NULL, NULL, 0);
        free(weapon);
      }
      free(text);
      if (choice.kind == QA_SOURCE_ITEM_NAMED_AMBIGUOUS) {
        size_t *indices = malloc((choice.match_count + 1) * sizeof *indices);
        if (!indices) qa_range_error("Out of memory");
        char *again = ss_join(args, arg_count, " ");
        qa_source_item_named_result listed = qa_source_item_named(rows, count, again, NULL, indices, choice.match_count);
        if (listed.kind != QA_SOURCE_ITEM_NAMED_AMBIGUOUS) {
          char *joined = ss_join(args, arg_count, ""), *weapon = malloc(strlen(joined) + 11);
          if (!weapon) qa_range_error("Out of memory");
          sprintf(weapon, "q1:weapon/%s", joined); free(joined);
          qa_source_item_named(rows, count, weapon, NULL, indices, choice.match_count); free(weapon);
        }
        free(again);
        const char **items = malloc((choice.match_count + 1) * sizeof *items);
        if (!items) qa_range_error("Out of memory");
        for (size_t i = 0; i < choice.match_count; i++) items[i] = rows[indices[i]].item;
        char *list = ss_join(items, choice.match_count, ", "), *message = malloc(strlen(list) + 32);
        if (!message) qa_range_error("Out of memory");
        sprintf(message, "Ambiguous weapon; use %s\n", list);
        qa_app_shared_sim_print(sim, message, &actor);
        free(message); free(list); free(items); free(indices); free(rows); free(owned);
        return;
      }
      if (choice.kind == QA_SOURCE_ITEM_NAMED_MATCH) selected = owned[choice.item_index];
    } else {
      long index = -1;
      for (size_t i = 0; i < count; i++) if (ui.active_weapon && qa_ss_is(owned[i]->id, ui.active_weapon)) { index = (long)i; break; }
      long next = ss_cycle(index, qa_ss_is(name, "weapnext"), count);
      if (next >= 0) selected = owned[next];
    }
    if (selected) {
      qa_provider_id provider;
      if (!qa_shared_inventory_table_item_owner(sim->inventory, actor, selected->id, &provider)) {
        qa_weapon_reference equipment; bool has_equipment = sim->grapple != NULL;
        if (has_equipment) equipment = qa_app_sim_grapple_runtime_weapon(sim->grapple);
        provider = has_equipment && qa_ss_is(equipment.item, selected->id) ? equipment.provider : sim->weapon_provider.provider;
      }
      qa_app_shared_sim_request_weapon(sim, actor, (qa_weapon_reference){provider, selected->id});
      free(rows); free(owned);
      return;
    }
    free(rows); free(owned);
    if (!use) return;
  }
  if (sim->has_selected_arsenal && (cycle || use)) {
    qa_app_sim_player_ui ui = qa_app_shared_sim_selected_arsenal_ui(sim, actor);
    char *requested = ss_requested(args, arg_count);
    const qa_app_sim_player_ui_item *weapon = NULL;
    const qa_app_sim_player_ui_item **owned = malloc((ui.item_count + 1) * sizeof *owned);
    if (!owned) qa_range_error("Out of memory");
    size_t count = 0;
    for (size_t i = 0; i < ui.item_count; i++) if (qa_ss_is(ui.items[i].kind, "weapon") && ui.items[i].owned) owned[count++] = &ui.items[i];
    if (use) {
      for (size_t i = 0; i < count && !weapon; i++) {
        char prefixed[256]; snprintf(prefixed, sizeof prefixed, "q1:weapon/%s", requested);
        char label[256]; size_t n = 0;
        for (const char *c = owned[i]->label; *c && n + 1 < sizeof label; c++) if (*c != ' ') label[n++] = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        label[n] = '\0';
        if (qa_ss_is(owned[i]->id, requested) || qa_ss_is(owned[i]->id, prefixed) || qa_ss_is(label, requested)) weapon = owned[i];
      }
    } else {
      long index = -1;
      for (size_t i = 0; i < count; i++) if (ui.active_weapon && qa_ss_is(owned[i]->id, ui.active_weapon)) { index = (long)i; break; }
      long next = ss_cycle(index, qa_ss_is(name, "weapnext"), count);
      if (next >= 0) weapon = owned[next];
    }
    free(requested);
    if (weapon) { qa_app_shared_sim_selected_arsenal_select(sim, actor, weapon->id); free(owned); return; }
    free(owned);
    if (!use) return;
  }
  sim->strategy->after_command(sim, actor, player, name, args, arg_count);
}

/* ---- Source command members of the strategy (TS SourceStrategy preCommand/afterCommand). They live beside the
 * command helpers of this file; source_strategy.c installs them in the source tables. ---- */
bool qa_ss_q2_native_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source; (void)source;
  const char **argv = malloc((arg_count + 1) * sizeof *argv);
  if (!argv) qa_range_error("Out of memory");
  argv[0] = name; for (size_t i = 0; i < arg_count; i++) argv[i + 1] = args[i];
  char *joined = ss_join(args, arg_count, " ");
  ss_native_command command = {sim, qa_app_shared_sim_native_q2_client(sim, actor).slot + 1, argv, arg_count + 1, joined};
  qa_cleanup a, b; qa_cleanup_push(&a, free, argv); qa_cleanup_push(&b, free, joined);
  qa_app_q2_game_callback(ss_native_command_run, &command);
  qa_cleanup_pop(&b); qa_cleanup_pop(&a); free(joined); free(argv);

  return true;
}

bool qa_ss_quakec_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source;
  if (qa_ss_is(name, "kill")) { qa_app_sim_quakec_source_client_kill(source->quakec, actor); return true; }
  if ((qa_ss_is(name, "god") || qa_ss_is(name, "notarget") || qa_ss_is(name, "noclip")
      || qa_ss_is(name, "fly") || qa_ss_is(name, "give"))) {
    qa_app_sim_quakec_source_host_cheat(source->quakec, actor, name, args, arg_count);
    if (qa_ss_is(name, "fly") || qa_ss_is(name, "noclip")) {
      double move_type; bool has = qa_app_sim_quakec_source_read_move_type(source->quakec, actor, &move_type);
      qa_app_sim_movement_player_set_flight(qa_app_shared_sim_require_player(sim, actor), has && move_type == 5);
    }
    return true;
  }
  return false;
}

bool qa_ss_q2_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  (void)args; (void)arg_count;
  qa_ss_source_runtime *source = &sim->source;
  if (qa_ss_is(name, "fly")) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
    qa_q2_entity *entity = ss_q2_entity(sim, actor);
    if (!entity) ss_fail("Q2 player entity missing");
    if (!qa_ss_is(qa_q2_players_intermission(source->product->players).kind, "playing")) return true;
    qa_q2_player_context context = qa_q2_players_context(source->product->players, entity, source->product->services);
    if (!qa_q2_cheats_allowed(&context)) return true;
    bool enabled = qa_app_sim_movement_player_set_flight(player, !player->flight);
    if (enabled) context.state->noclip = false;
    qa_q2_player_event event; memset(&event, 0, sizeof event);
    event.kind = "print"; event.has_target = true; event.target = actor; event.level = "high"; event.text = enabled ? "fly ON\n" : "fly OFF\n";
    context.hooks->emit(context.hooks->ctx, event);
    return true;
  }
  return false;
}

bool qa_ss_q3_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source;
  if (qa_ss_is(name, "fly")) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
    qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(source->q3), true, actor);
    if (!entity || !entity->client) ss_fail("Q3 player entity missing");
    if (qa_app_q3_source_runtime_intermission_time(source->q3) != 0) { qa_app_q3_source_runtime_player_command(source->q3, actor, name, args, arg_count); return true; }
    if (!qa_app_q3_source_runtime_cheats_ok(source->q3, entity)) return true;
    bool enabled = qa_app_sim_movement_player_set_flight(player, !player->flight);
    if (enabled) entity->client->noclip = false;
    qa_app_shared_sim_print(sim, enabled ? "fly ON\n" : "fly OFF\n", &actor);
    return true;
  }
  return false;
}

bool qa_ss_q1_pre_command(qa_app_shared_simulation *sim, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source;
  if ((qa_ss_is(name, "say") || qa_ss_is(name, "say_team"))) {
    if (arg_count == 0) return true;
    qa_comp_q1_source_clients *clients = &source->q1_composition->clients;
    qa_comp_q1_source_client *sender = qa_comp_q1_source_clients_require(clients, actor);
    char prefix[300]; snprintf(prefix, sizeof prefix, "\x01%s: ", qa_comp_q1_source_client_name(sender));
    char *joined = ss_join(args, arg_count, " ");
    size_t keep = (size_t)qa_js_max(0, 62 - (double)strlen(prefix));
    if (strlen(joined) > keep) joined[keep] = '\0'; /* .slice(0, max(0, 62 - prefix.length)) (UTF-16 units: ASCII) */
    char *text = malloc(strlen(prefix) + strlen(joined) + 2);
    if (!text) qa_range_error("Out of memory");
    sprintf(text, "%s%s\n", prefix, joined); free(joined);
    qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, text);
    double sender_team = sender->team;
    for (size_t i = 0; i < clients->record_count; i++) {
      qa_comp_q1_source_client *recipient = &clients->records[i];
      if (qa_ss_is(name, "say_team") && qa_cvar_registry_variable_value(source->q1_cvars, "teamplay") != 0 && recipient->team != sender_team) continue;
      ss_q1_message(sim, recipient->actor.id, text);
    }
    qa_cleanup_pop(&cleanup); free(text);
    return true;
  }
  if (qa_ss_is(name, "kill")) {
    if (qa_q1_health(source->q1_game, actor) <= 0) { ss_q1_message(sim, actor, "Can't suicide -- already dead!\n"); return true; }
    qa_comp_q1_source_composition_suicide(source->q1_composition, actor);
    return true;
  }
  bool cheats_disabled = source->q1_game->options.deathmatch != 0
      && (qa_ss_is(source->q1_game->options.edition, "classic") || qa_cvar_registry_variable_value(source->q1_cvars, "sv_cheats") == 0);
  if (qa_ss_is(name, "give")) {
    if (cheats_disabled) { ss_q1_message(sim, actor, "Cheats are disabled on this server.\n"); return true; }
    qa_comp_q1_give_source give = {source->q1_game, source->q1_composition->services};
    qa_comp_give_q1(&give, actor, args, arg_count);
    return true;
  }
  if ((qa_ss_is(name, "god") || qa_ss_is(name, "notarget") || qa_ss_is(name, "noclip") || qa_ss_is(name, "fly"))) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
    qa_comp_q1_source_client *client = qa_comp_q1_source_clients_require(&source->q1_composition->clients, actor);
    if (cheats_disabled) { ss_q1_message(sim, actor, "Cheats are disabled on this server.\n"); return true; }
    bool enabled;
    if (qa_ss_is(name, "god")) {
      client->god_mode = !client->god_mode; enabled = client->god_mode;
      qa_q1_player_state *state = qa_q1_player(source->q1_game, actor);
      ss_set_traits(sim, player->actor, false, false, true,
          enabled || (state ? qa_q1_player_powerup_get(state, "invulnerability") : 0) > ss_time_seconds(sim));
    } else if (qa_ss_is(name, "notarget")) {
      enabled = !qa_comp_q1_source_composition_no_target(source->q1_composition, actor);
      qa_comp_q1_source_composition_set_no_target(source->q1_composition, actor, enabled);
    } else if (qa_ss_is(name, "fly")) {
      enabled = qa_app_sim_movement_player_set_flight(player, !player->flight);
    } else {
      qa_movement_state state = qa_app_sim_movement_player_read_state(player);
      enabled = !(state.kind == QA_MOVEMENT_STATE_Q1 ? state.as.q1.move_type == 8 : state.kind == QA_MOVEMENT_STATE_QW ? state.as.qw.spectator != 0
          : state.kind == QA_MOVEMENT_STATE_Q3 ? state.as.q3.movement_type == 1
          : state.kind == QA_MOVEMENT_STATE_Q2 ? state.as.q2.type == 1 : state.as.q2_rerelease.type == 1);
      qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
      change.kind = "noclip"; change.enabled = enabled;
      qa_app_shared_sim_set_player_movement(sim, actor, change);
    }
    char message[64]; snprintf(message, sizeof message, "%s %s\n", qa_ss_is(name, "god") ? "godmode" : name, enabled ? "ON" : "OFF");
    ss_q1_message(sim, actor, message);
    return true;
  }
  return false;
}

void qa_ss_q3_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source;
  qa_app_q3_source_runtime_player_command(source->q3, actor, name, args, arg_count);
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(qa_app_q3_source_runtime_records(source->q3), true, actor);
  if (qa_ss_is(name, "noclip") && entity && entity->client && entity->client->noclip) qa_app_sim_movement_player_set_flight(player, false);

}

void qa_ss_q1_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source;
  bool cycle = qa_ss_is(name, "weapnext") || qa_ss_is(name, "weapprev");
  qa_q1_player_state *state = qa_q1_player(source->q1_game, actor);
  const char *active = state ? state->weapon : NULL;
  const char *owned[8]; size_t count = 0;
  for (size_t i = 0; i < 8; i++) {
    char item[64]; snprintf(item, sizeof item, "q1:weapon/%s", QA_Q1_WEAPONS[i]);
    if (qa_shared_inventory_table_count(sim->inventory, actor, item) > 0) owned[count++] = QA_Q1_WEAPONS[i];
  }
  char *requested = ss_requested(args, arg_count);
  const char *weapon = NULL;
  if (cycle) {
    long index = -1;
    for (size_t i = 0; i < count; i++) if (active && qa_ss_is(owned[i], active)) { index = (long)i; break; }
    long next = ss_cycle(index, qa_ss_is(name, "weapnext"), count);
    if (next >= 0) weapon = owned[next];
  } else for (size_t i = 0; i < 8 && !weapon; i++) {
    char prefixed[64]; snprintf(prefixed, sizeof prefixed, "q1:weapon/%s", QA_Q1_WEAPONS[i]);
    if (qa_ss_is(QA_Q1_WEAPONS[i], requested) || qa_ss_is(prefixed, requested)) weapon = QA_Q1_WEAPONS[i];
  }
  free(requested);
  if (weapon && !sim->has_selected_arsenal) qa_q1_select_weapon(source->q1_game, player->actor, weapon);
}

void qa_ss_q2_after_command(qa_app_shared_simulation *sim, qa_actor_id actor, qa_app_sim_movement_player *player, const char *name, const char *const *args, size_t arg_count) {
  qa_ss_source_runtime *source = &sim->source;
  bool cycle = qa_ss_is(name, "weapnext") || qa_ss_is(name, "weapprev"), use = qa_ss_is(name, "use");
  qa_q2_product_runtime *product = source->product;
  qa_q2_entity *entity = ss_q2_entity(sim, actor);
  if (!entity) ss_fail("Q2 player entity missing");
  if (!cycle && !use) { qa_q2_players_client_command(product->players, entity, product->services, name, args, arg_count); return; }
  if (product->match.source_kind == QA_Q2_MATCH_LMCTF && product->match.lmctf && !qa_q2_lmctf_can_move(product->match.lmctf, actor)) return;
  qa_q2w_state *state = qa_q2w_ballistics_state(&product->weapons->base, actor);
  const char *active = state ? state->weapon : NULL;
  size_t total;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(product->weapons, &total);
  const qa_q2w_definition **owned = malloc((total + 1) * sizeof *owned);
  if (!owned) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < total; i++) if (qa_shared_inventory_table_count(sim->inventory, actor, definitions[i]->item) > 0) owned[count++] = definitions[i];
  char *requested = ss_requested(args, arg_count);
  const qa_q2w_definition *weapon = NULL;
  if (cycle) {
    long index = -1;
    for (size_t i = 0; i < count; i++) if (active && qa_ss_is(owned[i]->name, active)) { index = (long)i; break; }
    long next = ss_cycle(index, qa_ss_is(name, "weapnext"), count);
    if (next >= 0) weapon = owned[next];
  } else for (size_t i = 0; i < total && !weapon; i++)
    if (qa_ss_is(definitions[i]->name, requested) || qa_ss_is(definitions[i]->item, requested)) weapon = definitions[i];
  free(owned);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, requested);
  if (weapon) {
    if (!sim->has_selected_arsenal)
      qa_q2w_weapons_request_weapon(product->weapons, (qa_q2w_owner){entity->actor, entity->view_height}, product->services, weapon->name, false);
  } else if (use) {
    size_t entry_count;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, actor, &entry_count);
    char prefixed[256]; snprintf(prefixed, sizeof prefixed, "q2:item_%s", requested);
    for (size_t i = 0; i < entry_count; i++)
      if (qa_ss_is(entries[i].item, requested) || qa_ss_is(entries[i].item, prefixed)) {
        qa_item_id item = entries[i].item;
        qa_q2_item_module_use(product->items, player->actor, item, product->services, 30);
        break;
      }
  }
  qa_cleanup_pop(&cleanup); free(requested);
}

/* ---- C-only owner release (the TS GC) ---- */
void qa_app_shared_sim_players_release(qa_app_shared_simulation *sim, qa_owned_actor actor) {
  qa_q1_character_actor **q1 = qa_ss_map_get(&sim->q1_characters, &actor);
  if (q1) qa_q1_character_actor_destroy(*q1);
  qa_q2_character_actor **q2 = qa_ss_map_get(&sim->q2_characters, &actor);
  if (q2) qa_q2_character_actor_destroy(*q2);
  qa_q3_character_actor **q3 = qa_ss_map_get(&sim->characters, &actor);
  if (q3) free(*q3);
  qa_q1_travel_state *carry = qa_ss_map_get(&sim->entry_carry, &actor);
  if (carry) qa_q1_travel_state_destroy(carry);
  qa_ss_map_delete(&sim->q1_characters, &actor); qa_ss_map_delete(&sim->q2_characters, &actor);
  qa_ss_map_delete(&sim->characters, &actor); qa_ss_map_delete(&sim->entry_carry, &actor);
}
void qa_app_shared_sim_players_destroy(qa_app_shared_simulation *sim) {
  for (size_t i = 0; i < sim->q1_characters.count; i++) qa_q1_character_actor_destroy(*(qa_q1_character_actor **)qa_ss_map_value_at(&sim->q1_characters, i));
  for (size_t i = 0; i < sim->q2_characters.count; i++) qa_q2_character_actor_destroy(*(qa_q2_character_actor **)qa_ss_map_value_at(&sim->q2_characters, i));
  for (size_t i = 0; i < sim->characters.count; i++) free(*(qa_q3_character_actor **)qa_ss_map_value_at(&sim->characters, i));
  for (size_t i = 0; i < sim->entry_carry.count; i++) qa_q1_travel_state_destroy(qa_ss_map_value_at(&sim->entry_carry, i));
  qa_ss_map_clear(&sim->q1_characters); qa_ss_map_clear(&sim->q2_characters); qa_ss_map_clear(&sim->characters); qa_ss_map_clear(&sim->entry_carry);
  if (sim->q1_character_foundation) { qa_q1_services_free(sim->q1_character_foundation); free(sim->q1_character_foundation); sim->q1_character_foundation = NULL; }
  for (size_t i = 0; i < sim->native_slice_contact_count; i++) free(sim->native_slice_contacts[i]);
  free(sim->native_slice_contacts); sim->native_slice_contacts = NULL; sim->native_slice_contact_count = 0;
}

/* ---- syncQ3Player (4384-4400) ---- */
void qa_app_shared_sim_sync_q3_player(qa_app_shared_simulation *sim, qa_app_sim_movement_player *player) {
  if ((sim->strategy->as_q3(sim) == NULL)) return;
  qa_q3_base_entity_records *records = qa_app_q3_source_runtime_records(sim->source.q3);
  qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(records, true, player->actor.id);
  if (!entity || !entity->client) return;
  qa_q3_base_player_state *ps = &entity->client->ps;
  if (sim->grapple) qa_app_sim_grapple_runtime_observe_teleport(sim->grapple, player->actor.id, qa_js_bit_and(ps->e_flags, 4));
  qa_vec3 previous = player->view_angles;
  player->view_angles = ps->viewangles;
  if (player->state.kind == QA_MOVEMENT_STATE_Q3) {
    ss_player_host *h = player->host.ctx;
    player->state.as.q3 = qa_app_read_q3_movement_state(entity, records, &h->jump_pad);
  } else {
    qa_movement_state state = qa_app_sim_movement_player_read_state(player);
    if (state.kind == QA_MOVEMENT_STATE_Q1) {
      state.as.q1.view_angles = player->view_angles;
      if (qa_js_bit_and(ps->pm_flags, QA_Q3_FLAG_TIME_KNOCKBACK) != 0) state.as.q1.teleport_time_seconds = ss_time_seconds(sim) + ps->pm_time / 1000;
    }
    player->state = state;
  }
  if (previous.x != player->view_angles.x || previous.y != player->view_angles.y || previous.z != player->view_angles.z)
    ss_view_reset(sim, "source", player->actor.id, player->view_angles);
}

/* ---- SimulationEvents capture()/restore() (events.ts 87-92) over the PresentationState super, for
 * checkpointFromSource's `events` and restore's `this.events.restore(...)`. ---- */
qa_save_value *qa_app_shared_sim_events_capture(qa_app_shared_simulation *sim) {
  /* { ...super.capture(), sequence: this.sequence } */
  qa_save_value *value = qa_app_presentation_state_capture(sim->presentation);
  if (!value || !qa_save_value_set(value, "sequence", qa_save_value_number(qa_app_sim_simulation_events_capture_sequence(sim->events))))
    qa_range_error("Out of memory");
  return value;
}
void qa_app_shared_sim_events_restore(qa_app_shared_simulation *sim, const qa_save_reader *reader,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx) {
  /* this.sequence = reader.field("sequence").integer(0); this.emitted.reset(); super.restore(reader, reference) */
  qa_app_sim_simulation_events_restore(sim->events, reader);
  qa_app_presentation_state_restore(sim->presentation, reader, reference, reference_ctx);
}

/* ---- spawnQ3Player (runtime.ts 4193-4238): the Q3SourceRuntime host spawnPlayer. ---- */
void qa_app_shared_sim_spawn_q3_player(qa_app_shared_simulation *sim, qa_q3_game_entity *entity, qa_q3_ta_spawn_pose pose) {
  if ((sim->strategy->as_q3(sim) == NULL)) ss_fail("Q3 spawn before source attachment");
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, qa_q3_game_entity_actor(entity).id);
  const char *product = qa_app_q3_source_runtime_options(sim->source.q3)->product;
  /* The loadout's ammo list lives with the player host (the TS array is the player's own). */
  qa_arsenal_state loadout = qa_q3_spawn_loadout(sim->weapon_provider.provider, product, qa_app_q3_source_runtime_game_type(sim->source.q3) == 3);
  ss_player_host *loadout_host = player->host.ctx;
  if (loadout.ammo_count > sizeof loadout_host->q3_spawn_ammo / sizeof *loadout_host->q3_spawn_ammo) ss_fail("Q3 spawn loadout exceeds its weapon table");
  memcpy(loadout_host->q3_spawn_ammo, loadout.ammo, loadout.ammo_count * sizeof *loadout.ammo);
  free((void *)loadout.ammo);
  loadout.ammo = loadout_host->q3_spawn_ammo;
  player->arsenal = loadout;
  qa_q3_arsenal_runtime_state runtime = qa_q3_spawn_arsenal_runtime(product, 100, 0);
  qa_ss_map_set(&sim->q3_arsenals, &player->actor, &runtime);
  player->bounds = player->standing_bounds; player->has_body_shape_base = false;
  player->view_height = player->character == QA_GAME_FAMILY_Q3 ? 26 : 22;
  qa_body_state body; memset(&body, 0, sizeof body);
  body.origin = pose.origin; body.angles = pose.angles; body.velocity = qa_ss_zero; body.bounds = player->standing_bounds; body.ground = NULL;
  if (player->character == QA_GAME_FAMILY_Q3) {
    qa_q3_character_actor **existing = qa_ss_map_get(&sim->characters, &player->actor);
    qa_q3_character_actor *character = existing ? *existing : NULL;
    if (character == NULL) {
      character = ss_new_q3_character(sim, player->actor, player->character_definition.provider, product, player->character_definition.content);
      ss_set_q3_character(sim, player->actor, character);
    }
    char handicap[32];
    snprintf(handicap, sizeof handicap, "%s", QA_JS_NUMBER(entity->client ? entity->client->pers.max_health : 100));
    qa_q3_character_spawn spawn; memset(&spawn, 0, sizeof spawn);
    spawn.body = body; spawn.combat = qa_q3_initial_combat(handicap, NULL);
    spawn.inventory = player->arsenal.ammo; spawn.inventory_count = player->arsenal.ammo_count;
    qa_q3_character_actor_spawn(character, spawn);
    player->animation = qa_q3_character_actor_get_animation(character);
  } else {
    qa_shared_body_table_write(ss_bodies(sim), player->actor, body);
    qa_gameplay_authority_set_health(sim->combat, player->actor, 100);
    qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
    traits.has_can_take_damage = true; traits.can_take_damage = true; traits.has_invulnerable = true; traits.invulnerable = false;
    qa_gameplay_authority_set_traits(sim->combat, player->actor, traits);
    size_t count = 0;
    const qa_inventory_entry *entries = qa_shared_inventory_table_entries(sim->inventory, player->actor.id, &count);
    qa_inventory_entry *copy = calloc(count + 1, sizeof *copy);
    if (!copy) ss_fail("Out of memory");
    memcpy(copy, entries, count * sizeof *copy);
    for (size_t i = 0; i < count; i++) { qa_inventory_entry zeroed = copy[i]; zeroed.count = 0; qa_shared_inventory_table_configure(sim->inventory, player->actor, zeroed, NULL, NULL); }
    free(copy);
    for (size_t i = 0; i < player->arsenal.ammo_count; i++) qa_shared_inventory_table_configure(sim->inventory, player->actor, player->arsenal.ammo[i], NULL, NULL);
    if (player->character == QA_GAME_FAMILY_Q2) {
      qa_q2_character_actor **character = qa_ss_map_get(&sim->q2_characters, &player->actor);
      if (character == NULL) qa_app_shared_sim_attach_q2_character(sim, player); else qa_q2_character_actor_respawned(*character);
    } else {
      qa_q1_character_actor **character = qa_ss_map_get(&sim->q1_characters, &player->actor);
      if (character == NULL) (void)qa_app_shared_sim_attach_q1_character(sim, player); else qa_q1_character_actor_respawn(*character, NULL);
    }
  }
  if (sim->has_selected_arsenal) {
    qa_app_shared_sim_selected_arsenal_remove(sim, player->actor.id);
    player->arsenal = qa_app_shared_sim_selected_arsenal_admit(sim, player->actor, 100, false);
  }
  if (sim->hand_grenades) qa_app_sim_hand_grenade_runtime_respawn(sim->hand_grenades, player->actor.id);
  if (sim->grapple) qa_app_sim_grapple_runtime_release(sim->grapple, player->actor.id);
  qa_app_shared_sim_admit_grapple(sim, player->actor.id);
  qa_app_shared_sim_apply_shared_travel_once(sim, player->actor, player->client);
  qa_q2_player_movement_change change; memset(&change, 0, sizeof change);
  change.kind = "spawn"; change.origin = pose.origin; change.angles = pose.angles; change.velocity = qa_ss_zero;
  change.command_angles = player->command_angles; change.hold_milliseconds = 100; change.spectator = false;
  qa_app_shared_sim_set_player_movement_link(sim, player->actor.id, change, false);
  qa_q3_game_client *client = entity->client;
  if (client != NULL) {
    client->ps.viewheight = player->view_height; client->ps.viewangles = pose.angles;
    if (player->arsenal.state.kind && qa_ss_is(player->arsenal.state.kind, "q3")) {
      client->ps.weapon = player->arsenal.state.source_weapon; client->ps.weapon_state = player->arsenal.state.state;
      client->ps.weapon_time = player->arsenal.state.time_milliseconds;
    }
  }
  qa_app_shared_sim_notify_client_event(sim, "admitted", player->actor.id);
}

/* ---- selectQ3SpawnWeapon (runtime.ts 4241-4255): g_client.c's highest held weapon after the targets fire. ---- */
void qa_app_shared_sim_select_q3_spawn_weapon(qa_app_shared_simulation *sim, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = entity->client;
  if (client == NULL || (sim->strategy->as_q3(sim) == NULL)) return;
  const char *product_name = qa_app_q3_source_runtime_options(sim->source.q3)->product;
  qa_q3_base_product product = product_name && strcmp(product_name, "missionpack") == 0 ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  qa_actor_id actor = qa_q3_game_entity_actor(entity).id;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  int32_t held = qa_js_to_int32(qa_q3_base_player_state_slots_get(&client->ps.stats, (size_t)qa_q3_base_stat_schema_for(product)->weapons));
  double weapon = 1;
  for (double candidate = qa_q3_base_weapon_count(product) - 1; candidate > 0; candidate--)
    if ((held & (1 << (int)candidate)) != 0) { weapon = candidate; break; }
  if (sim->has_selected_arsenal && qa_ss_is(sim->selected_arsenal.family, "q3")) {
    qa_app_q3_selected_arsenal_set_spawn_weapon(sim->selected_arsenal.q3, actor, weapon);
    player->arsenal = qa_app_q3_selected_arsenal_read(sim->selected_arsenal.q3, actor);
  } else if (player->arsenal.state.kind && qa_ss_is(player->arsenal.state.kind, "q3")) {
    const qa_q3_weapon_item *item = qa_q3_get_weapon_item(weapon);
    player->arsenal.active_weapon = item ? item->item : player->arsenal.active_weapon;
    player->arsenal.state.source_weapon = weapon;
  }
  client->ps.weapon = weapon;
}

/* ---- moveQ3Client (runtime.ts 4257-4324): the Q3SourceRuntime host moveClient runs the selected provider. ---- */
qa_q3_ta_client_movement_result qa_app_shared_sim_move_q3_client(qa_app_shared_simulation *sim, qa_q3_game_entity *entity,
    qa_q3_base_user_command command, qa_q3_ta_client_movement_options options) {
  qa_actor_id actor = qa_q3_game_entity_actor(entity).id;
  qa_app_sim_movement_player *player = qa_app_shared_sim_require_player(sim, actor);
  qa_q3_game_client *client = entity->client;
  if (client == NULL) ss_fail("Q3 movement has no client record");
  if ((sim->strategy->as_q3(sim) == NULL)) ss_fail("Q3 source movement owner is missing");
  qa_q3_base_entity_records *records = qa_app_q3_source_runtime_records(sim->source.q3);
  if (player->state.kind == QA_MOVEMENT_STATE_Q3) {
    ss_player_host *h = player->host.ctx;
    player->state.as.q3 = qa_app_read_q3_movement_state(entity, records, &h->jump_pad);
  }
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, actor);
  qa_movement_environment base; memset(&base, 0, sizeof base);
  base.health = combat.has_value ? combat.value.health : 0; base.flight = false; base.haste = false;
  base.invulnerable = combat.has_value && combat.value.invulnerable; base.gravity_multiplier = player->gravity_multiplier;
  player->has_source_environment = true;
  player->source_environment = qa_app_read_q3_movement_environment(entity, base);
  double grapple_gravity = sim->grapple ? qa_app_sim_grapple_runtime_gravity_scale(sim->grapple, player->actor.id) : 1;
  if (sim->grapple && (player->state.kind == QA_MOVEMENT_STATE_Q2 || player->state.kind == QA_MOVEMENT_STATE_Q2_RERELEASE)) {
    double gravity = trunc(qa_app_sim_shared_physics_gravity(sim->physics) * player->gravity_multiplier * grapple_gravity);
    if (player->state.kind == QA_MOVEMENT_STATE_Q2) player->state.as.q2.gravity = gravity; else player->state.as.q2_rerelease.gravity = gravity;
  }
  if (grapple_gravity == 0) {
    if (player->state.kind == QA_MOVEMENT_STATE_Q3) player->state.as.q3.gravity = 0;
    player->source_environment.gravity_multiplier = 0;
  }
  double elapsed = qa_js_max(0, qa_js_min(200, command.server_time - client->ps.command_time));
  const qa_actor_command *pending = qa_ss_map_get(&sim->q3_commands, &player->actor);
  qa_q3_base_user_command for_selection = command;
  if (!qa_ss_is(player->profile.kind, "q3")) for_selection.angles = qa_ss_add(command.angles, client->ps.delta_angles);
  qa_user_command converted = qa_app_sim_selected_q3_command(for_selection, player->profile, elapsed);
  qa_actor_command input; memset(&input, 0, sizeof input);
  /* The arsenal intent the command points to lives with the player host (TS object literal). */
  ss_player_host *intent_host = player->host.ctx;
  if (pending == NULL) {
    input.actor = player->actor.id; input.sequence = player->last_sequence + 1;
    input.source.kind = QA_COMMAND_SOURCE_BOT; input.source.provider = sim->recipe.map.entities.provider;
    input.command = converted;
    if (sim->has_selected_arsenal) {
      qa_arsenal_intent *intent = &intent_host->q3_arsenal_intent;
      memset(intent, 0, sizeof *intent);
      intent->provider = sim->weapon_provider.provider; intent->weapon = NULL;
      intent->use_holdable = (qa_js_to_int32(command.buttons) & QA_Q3_BUTTON_USE_HOLDABLE) != 0;
      input.arsenal = intent;
    }
  } else {
    qa_app_sim_q3_command_player command_player = {player->arsenal, sim->recipe.map.entities.content, player->profile};
    qa_q3_base_user_command before = qa_app_sim_q3_source_command(*pending, command_player, sim->source_scheduling_milliseconds, client->ps.weapon);
    qa_q3_base_user_command source_before = qa_app_sim_relative_q3_source_command(pending->source,
        pending->command.kind == QA_USER_COMMAND_Q3 ? "q3" : "foreign", before, client->ps.delta_angles, pending->angle_space);
    double pm_type = client->ps.pm_type;
    input = qa_app_apply_q3_command_policy(*pending, source_before, command, converted,
        pm_type == QA_Q3_PM_FREEZE || pm_type == QA_Q3_PM_INTERMISSION || pm_type == QA_Q3_PM_SPINTERMISSION, &intent_host->q3_policy_arsenal);
  }
  if (input.angle_space && qa_ss_is(input.angle_space, "absolute") && input.command.kind == QA_USER_COMMAND_Q3 && converted.kind == QA_USER_COMMAND_Q3) {
    input.angle_space = "source-relative"; input.command.as.q3.angle_words = converted.as.q3.angle_words;
  }
  qa_app_sim_relative_movement_input relative = {input.command, input.angle_space};
  relative = qa_app_sim_relative_movement_command(relative, qa_app_sim_movement_player_read_state(player));
  input.command = relative.command; input.angle_space = relative.angle_space;
  /* player.sourceMovement = options (the moveClient literal, its key order included) */
  player->has_source_movement = true; memset(&player->source_movement, 0, sizeof player->source_movement);
  player->source_movement.has_trace_mask = true; player->source_movement.trace_mask = options.trace_mask;
  player->source_movement.has_fixed_msec = options.has_fixed_msec; player->source_movement.fixed_msec = options.fixed_msec;
  player->source_movement.no_footsteps = options.no_footsteps; player->source_movement.gauntlet_hit = options.gauntlet_hit;
  player->source_movement.debug_level = options.debug_level; player->source_movement.ladders = options.ladders;
  player->source_movement.order = options.literal == QA_Q3_TA_MOVEMENT_LITERAL_SPECTATOR ? QA_APP_SIM_MOVEMENT_ORDER_SPECTATOR
                                                                                        : QA_APP_SIM_MOVEMENT_ORDER_THINK;
  double last_sequence = player->last_sequence;
  qa_frame_context frame = sim->source_frame;
  frame.phase = QA_FRAME_PHASE_CLIENT_COMMAND; frame.elapsed = (qa_source_time){QA_SOURCE_TIME_MILLISECONDS, elapsed};
  qa_movement_result result = pending != NULL && sim->mod_client_applications && qa_mod_client_applications_active(sim->mod_client_applications)
      ? qa_app_sim_movement_player_move_command(player, input, frame, NULL) : qa_app_sim_movement_player_move(player, input, frame, NULL);
  if (pending == NULL) player->last_sequence = last_sequence;
  qa_q3_ta_client_movement_result out; memset(&out, 0, sizeof out);
  if (!qa_ss_is(result.status, "active")) {
    out.bounds = player->bounds; out.waterlevel = player->water_level; out.watertype = player->water_type; out.xyspeed = 0;
    return out;
  }
  const char *selected_provider = sim->selected_q3_source ? sim->selected_q3_source->host.provider : NULL;
  if (qa_ss_is(player->profile.kind, "q2-classic") || qa_ss_is(player->profile.kind, "q2-rerelease")) {
    qa_weapon_step_input step; memset(&step, 0, sizeof step);
    step.actor = player->actor; step.command = input.command; step.frame = frame;
    step.arsenal = qa_app_shared_sim_arsenal(sim, player); step.animation = player->animation;
    step.environment = player->source_environment; step.gauntlet_hit = options.gauntlet_hit;
    qa_weapon_step_result weapon = qa_app_shared_sim_weapon_step(sim, step);
    player->arsenal = weapon.arsenal; player->animation = weapon.animation;
    for (size_t i = 0; i < weapon.effect_count; i++) {
      const qa_movement_effect *effect = &weapon.effects[i];
      if (effect->kind == QA_MOVEMENT_EFFECT_EVENT && qa_app_sim_provider_family(effect->value.provider) == QA_GAME_FAMILY_Q3
          && !(selected_provider && qa_ss_is(effect->value.provider, selected_provider)))
        qa_q3_base_player_state_record_add_event(&client->ps, effect->value.event, effect->value.parameter);
    }
  }
  client->ps.command_time = command.server_time; client->ps.viewangles = player->view_angles; client->ps.viewheight = player->view_height;
  if (player->state.kind != QA_MOVEMENT_STATE_Q3) {
    /* Native Q3 pmove ages pm_time and clears the TIME_* flags; a foreign provider never writes them back. */
    client->ps.pm_time -= elapsed;
    if (client->ps.pm_time <= 0) {
      client->ps.pm_time = 0;
      client->ps.pm_flags = (double)(qa_js_to_int32(client->ps.pm_flags) & ~(QA_Q3_FLAG_TIME_LAND | QA_Q3_FLAG_TIME_KNOCKBACK | QA_Q3_FLAG_TIME_WATERJUMP));
    }
  }
  if (player->ground.kind == QA_TRACE_HIT_WORLD) client->ps.ground_entity_num = 1022;
  else if (player->ground.kind == QA_TRACE_HIT_ACTOR) {
    qa_q3_game_entity *ground = qa_q3_base_entity_records_native_by_actor(records, true, player->ground.actor);
    client->ps.ground_entity_num = ground ? ground->slot : 1023;
  } else client->ps.ground_entity_num = 1023;
  if (player->state.kind == QA_MOVEMENT_STATE_Q3) qa_app_write_q3_movement_state(entity, player->state.as.q3, records);
  if (player->animation.state.kind && qa_ss_is(player->animation.state.kind, "q3")) {
    client->ps.legs_anim = player->animation.state.legs; client->ps.torso_anim = player->animation.state.torso;
    client->ps.legs_timer = player->animation.state.legs_timer_milliseconds; client->ps.torso_timer = player->animation.state.torso_timer_milliseconds;
    qa_q3_character_actor **character = qa_ss_map_get(&sim->characters, &player->actor);
    if (character) qa_q3_character_actor_commit_animation(*character, player->animation);
  }
  qa_app_write_q3_character_animation(entity, player->animation);
  for (size_t i = 0; i < result.effect_count; i++) {
    const qa_movement_effect *effect = &result.effects[i].effect;
    if (effect->kind == QA_MOVEMENT_EFFECT_EVENT && qa_app_sim_provider_family(effect->value.provider) == QA_GAME_FAMILY_Q3
        && !(selected_provider && qa_ss_is(effect->value.provider, selected_provider))
        && qa_app_sim_publish_q3_character_movement_event(player->character, effect->value.event))
      qa_q3_base_player_state_record_add_event(&client->ps, effect->value.event, effect->value.parameter);
  }
  qa_actor_id world_actor = qa_q3_game_entity_actor(qa_q3_entity_pool_at(qa_app_q3_source_runtime_pool(sim->source.q3), 1022)).id;
  /* The contacts are borrowed by the caller until the next move: the player host keeps a buffer
   * that grows to the largest count seen (simulation-lifetime storage). */
  if (result.contact_count > intent_host->q3_contact_capacity) {
    intent_host->q3_contacts = qa_app_shared_sim_keep(sim, result.contact_count * sizeof *intent_host->q3_contacts);
    intent_host->q3_contact_capacity = result.contact_count;
  }
  qa_actor_id *contacts = intent_host->q3_contacts;
  size_t contact_count = 0;
  for (size_t i = 0; i < result.contact_count; i++) {
    if (result.contacts[i].target.kind == QA_TRACE_HIT_ACTOR) contacts[contact_count++] = result.contacts[i].target.actor;
    else if (result.contacts[i].target.kind == QA_TRACE_HIT_WORLD) contacts[contact_count++] = world_actor;
  }
  out.contacts = contacts; out.contact_count = contact_count;
  out.bounds = player->bounds; out.waterlevel = player->water_level;
  out.watertype = player->water_type < 0 ? (player->water_type == -3 ? 32 : player->water_type == -4 ? 16 : player->water_type == -5 ? 8 : 0) : player->water_type;
  double vx = result.state.kind == QA_MOVEMENT_STATE_Q2 ? result.state.as.q2.velocity_eighths.values[0] / 8 : qa_app_sim_movement_velocity(result.state).x;
  double vy = result.state.kind == QA_MOVEMENT_STATE_Q2 ? result.state.as.q2.velocity_eighths.values[1] / 8 : qa_app_sim_movement_velocity(result.state).y;
  out.xyspeed = qa_js_hypot2(vx, vy);
  return out;
}
