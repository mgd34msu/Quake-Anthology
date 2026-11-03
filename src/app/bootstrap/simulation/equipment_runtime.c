/* Port of src/app/bootstrap/simulation/equipment-runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/equipment_runtime.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/world/gameplay/inventory.h"

void qa_app_sim_hand_grenade_runtime_checkpoint_destroy(qa_app_sim_hand_grenade_runtime_checkpoint *c) {
  free(c->controller_actors); c->controller_actors = NULL;
  c->controller.actors = NULL; c->controller.actor_count = 0;
  free(c->controls); c->controls = NULL; c->control_count = 0;
  qa_persist_decoded_free(&c->entities_owner);
}

/* TS HandControl keyed by ActorId (Map insertion order). */
typedef struct control_entry { qa_actor_id actor; bool held, pressed, released; } control_entry;
struct qa_app_sim_hand_grenade_runtime {
  qa_hand_grenade_selection selection;
  qa_q2_hand_grenade_equipment *controller;
  qa_q2_entity_services *independent_game;
  qa_app_sim_source_random *independent_random;
  control_entry *controls; size_t control_count, control_capacity;
  int64_t release_subscription;
};

static control_entry *find_control(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->control_count; i++) if (qa_same_actor(r->controls[i].actor, actor)) return &r->controls[i];
  return NULL;
}
static void delete_control(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor) {
  for (size_t i = 0; i < r->control_count; i++)
    if (qa_same_actor(r->controls[i].actor, actor)) {
      memmove(&r->controls[i], &r->controls[i + 1], (r->control_count - i - 1) * sizeof *r->controls);
      r->control_count--;
      return;
    }
}
/* Map.set: an existing key keeps its position. */
static void set_control(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor, bool held, bool pressed, bool released) {
  control_entry *entry = find_control(r, actor);
  if (entry == NULL) {
    if (r->control_count == r->control_capacity) {
      size_t capacity = r->control_capacity ? r->control_capacity * 2 : 4;
      control_entry *grown = realloc(r->controls, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      r->controls = grown; r->control_capacity = capacity;
    }
    entry = &r->controls[r->control_count++];
    entry->actor = actor;
  }
  entry->held = held; entry->pressed = pressed; entry->released = released;
}
static void on_release(void *ctx, qa_owned_actor actor) { delete_control(ctx, actor.id); }
static qa_q2_game_services *game_of(const qa_app_sim_hand_grenade_runtime *r) {
  return qa_q2_hand_grenade_equipment_game(r->controller);
}

qa_app_sim_hand_grenade_runtime *qa_app_sim_hand_grenade_runtime_create(qa_hand_grenade_selection selection,
    qa_q2_hand_grenade_equipment *controller, qa_q2_entity_services *independent_game,
    qa_app_sim_source_random *independent_random) {
  qa_app_sim_hand_grenade_runtime *r = calloc(1, sizeof *r);
  if (r == NULL) qa_range_error("Out of memory");
  r->selection = selection;
  r->controller = controller;
  r->independent_game = independent_game;
  r->independent_random = independent_random;
  r->release_subscription = qa_session_actor_registry_on_release(game_of(r)->host->actors, on_release, r);
  return r;
}
void qa_app_sim_hand_grenade_runtime_destroy(qa_app_sim_hand_grenade_runtime *r) {
  if (r == NULL) return;
  qa_session_actor_registry_off_release(game_of(r)->host->actors, r->release_subscription);
  free(r->controls);
  free(r);
}
const qa_hand_grenade_selection *qa_app_sim_hand_grenade_runtime_selection(const qa_app_sim_hand_grenade_runtime *r) {
  return &r->selection;
}
/* TS readonly independent.game. */
qa_q2_entity_services *qa_app_sim_hand_grenade_runtime_independent_game(const qa_app_sim_hand_grenade_runtime *r) {
  return r->independent_game;
}
qa_q2_hand_grenade_equipment *qa_app_sim_hand_grenade_runtime_controller(const qa_app_sim_hand_grenade_runtime *r) {
  return r->controller;
}

/* TS rebase(action, delta). */
static qa_q2w_hand_action rebase(qa_q2w_hand_action action, double delta) {
  switch (action.kind) {
    case QA_Q2W_HA_IDLE: case QA_Q2W_HA_DISARMED: return action;
    case QA_Q2W_HA_PREPARING: action.next_at = action.next_at + delta; return action;
    case QA_Q2W_HA_COOKING: action.expires_at = action.expires_at + delta; return action;
    case QA_Q2W_HA_RELEASING:
      action.expires_at = action.expires_at + delta; action.throw_at = action.throw_at + delta; return action;
    case QA_Q2W_HA_RECOVERING: action.ready_at = action.ready_at + delta; return action;
  }
  return action;
}
static bool find_ammo(qa_q2_game_services *game, qa_actor_id actor, qa_inventory_entry *out) {
  size_t count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(game->host->inventory, actor, &count);
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].item, QA_HAND_GRENADE_AMMO) == 0) { *out = entries[i]; return true; }
  return false;
}
/* controller.restore({...checkpoint, actors: map(entry => same actor ? replace(entry) : entry)}). */
typedef struct rewrite_scratch { qa_hand_grenade_checkpoint_entry *actors; } rewrite_scratch;
static void rewrite_free(void *arg) { free(((rewrite_scratch *)arg)->actors); }
static void restore_rewritten(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                              const qa_app_sim_hand_grenade_travel *carry) {
  qa_hand_grenades_checkpoint checkpoint = qa_q2_hand_grenade_equipment_capture(r->controller);
  rewrite_scratch scratch = {malloc((checkpoint.actor_count ? checkpoint.actor_count : 1) * sizeof *scratch.actors)};
  if (scratch.actors == NULL) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, rewrite_free, &scratch);
  for (size_t i = 0; i < checkpoint.actor_count; i++) {
    scratch.actors[i] = checkpoint.actors[i];
    if (checkpoint.actors[i].actor.slot != (double)actor.slot || checkpoint.actors[i].actor.generation != (double)actor.generation)
      continue;
    if (carry != NULL) {
      qa_q2_game_services *game = game_of(r);
      scratch.actors[i].state = carry->state;
      scratch.actors[i].state.action = rebase(carry->state.action, game->host->now(game->host->ctx) - carry->seconds);
    } else {
      memset(&scratch.actors[i].state.action, 0, sizeof scratch.actors[i].state.action);
      scratch.actors[i].state.action.kind = QA_Q2W_HA_IDLE;
    }
  }
  checkpoint.actors = scratch.actors;
  qa_q2_hand_grenade_equipment_restore(r->controller, checkpoint);
  qa_cleanup_pop(&cleanup);
  free(scratch.actors);
}

static void grant_initial(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor) {
  qa_q2_game_services *game = game_of(r);
  qa_owned_actor owner;
  bool has_owner = qa_session_actor_registry_resolve_owned(game->host->actors, actor, &owner);
  qa_inventory_entry ammo;
  bool has_ammo = find_ammo(game, actor, &ammo);
  if (!has_owner || !has_ammo) qa_raise("Error", 0, 0, "Equipment allowance requires shared inventory");
  ammo.count = qa_js_max(ammo.count, r->selection.initial_ammo);
  ammo.capacity = qa_js_max(ammo.capacity, r->selection.capacity);
  qa_shared_inventory_table_configure(game->host->inventory, owner, ammo, NULL, NULL);
}

void qa_app_sim_hand_grenade_runtime_admit(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                                           const qa_app_sim_hand_grenade_travel *carry) {
  qa_q2_game_services *game = game_of(r);
  qa_hand_grenade_equipment_state ignored;
  bool first = !qa_q2_hand_grenade_equipment_state_of(r->controller, actor, &ignored);
  qa_hand_grenade_loadout loadout = {true, r->selection.initial_ammo, r->selection.capacity, false};
  qa_q2_hand_grenade_equipment_configure(r->controller, actor, loadout);
  set_control(r, actor, false, false, false);
  if (first && carry == NULL) grant_initial(r, actor);
  if (carry != NULL) {
    qa_owned_actor owner;
    if (!qa_session_actor_registry_resolve_owned(game->host->actors, actor, &owner))
      qa_raise("Error", 0, 0, "Equipment travel requires a live actor");
    qa_shared_inventory_table_configure(game->host->inventory, owner, carry->ammo, NULL, NULL);
    restore_rewritten(r, actor, carry);
  }
}

void qa_app_sim_hand_grenade_runtime_respawn(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor) {
  qa_app_sim_hand_grenade_runtime_admit(r, actor, NULL);
  grant_initial(r, actor);
  restore_rewritten(r, actor, NULL);
}

void qa_app_sim_hand_grenade_runtime_input(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor, bool held) {
  control_entry *previous = find_control(r, actor);
  if (previous == NULL) qa_raise("Error", 0, 0, "Actor has no selected hand grenade equipment");
  bool pressed = previous->pressed || (held && !previous->held);
  bool released = previous->released || (!held && previous->held);
  set_control(r, actor, held, pressed, released);
}

void qa_app_sim_hand_grenade_runtime_step(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                                          qa_hand_grenade_equipment_input input, bool enabled) {
  control_entry *found = find_control(r, actor);
  if (found == NULL) return;
  control_entry controls = *found;
  qa_hand_grenade_equipment_state state;
  if (qa_q2_hand_grenade_equipment_state_of(r->controller, actor, &state) && state.config.enabled != enabled) {
    qa_hand_grenade_loadout config = state.config;
    config.enabled = enabled;
    qa_q2_hand_grenade_equipment_configure(r->controller, actor, config);
  }
  set_control(r, actor, controls.held, false, false);
  input.pressed = controls.pressed;
  input.held = controls.held;
  input.released = controls.released;
  qa_q2_hand_grenade_equipment_step(r->controller, actor, input);
}

bool qa_app_sim_hand_grenade_runtime_travel(qa_app_sim_hand_grenade_runtime *r, qa_actor_id actor,
                                            qa_app_sim_hand_grenade_travel *out) {
  qa_hand_grenade_equipment_state state;
  if (!qa_q2_hand_grenade_equipment_state_of(r->controller, actor, &state)) return false;
  qa_q2_game_services *game = game_of(r);
  qa_inventory_entry ammo;
  if (!find_ammo(game, actor, &ammo)) qa_raise("Error", 0, 0, "Equipment travel lost its canonical ammunition");
  out->state = state;
  out->ammo = ammo;
  out->seconds = game->host->now(game->host->ctx);
  return true;
}

qa_app_sim_hand_grenade_runtime_checkpoint qa_app_sim_hand_grenade_runtime_capture(qa_app_sim_hand_grenade_runtime *r) {
  qa_app_sim_hand_grenade_runtime_checkpoint out;
  memset(&out, 0, sizeof out);
  out.version = 1;
  out.controller = qa_q2_hand_grenade_equipment_capture(r->controller);
  out.controller_actors = malloc((out.controller.actor_count ? out.controller.actor_count : 1) * sizeof *out.controller_actors);
  if (out.controller_actors == NULL) qa_range_error("Out of memory");
  memcpy(out.controller_actors, out.controller.actors, out.controller.actor_count * sizeof *out.controller_actors);
  out.controller.actors = out.controller_actors;
  out.controls = malloc((r->control_count ? r->control_count : 1) * sizeof *out.controls);
  if (out.controls == NULL) qa_range_error("Out of memory");
  out.control_count = r->control_count;
  for (size_t i = 0; i < r->control_count; i++) {
    out.controls[i].actor = qa_saved_actor_id_of(r->controls[i].actor);
    out.controls[i].held = r->controls[i].held;
    out.controls[i].pressed = r->controls[i].pressed;
    out.controls[i].released = r->controls[i].released;
  }
  out.entities = qa_q2_entity_services_capture(r->independent_game);
  out.random = qa_app_sim_source_random_checkpoint(r->independent_random);
  return out;
}

void qa_app_sim_hand_grenade_runtime_restore(qa_app_sim_hand_grenade_runtime *r,
                                             const qa_app_sim_hand_grenade_runtime_checkpoint *checkpoint) {
  qa_app_sim_source_random_restore(r->independent_random, checkpoint->random);
  qa_q2_entity_services_restore(r->independent_game, checkpoint->entities);
  qa_q2_hand_grenade_equipment_restore(r->controller, checkpoint->controller);
  r->control_count = 0;
  qa_q2_game_services *game = game_of(r);
  for (size_t i = 0; i < checkpoint->control_count; i++) {
    const qa_app_sim_hand_grenade_control *entry = &checkpoint->controls[i];
    qa_owned_actor actor;
    qa_hand_grenade_equipment_state state;
    if (!qa_session_actor_registry_resolve_saved(game->host->actors, entry->actor, &actor)
        || !qa_q2_hand_grenade_equipment_state_of(r->controller, actor.id, &state) || find_control(r, actor.id) != NULL)
      qa_raise("Error", 0, 0, "Invalid equipment input owner");
    set_control(r, actor.id, entry->held, entry->pressed, entry->released);
  }
  if (r->control_count != checkpoint->controller.actor_count)
    qa_raise("Error", 0, 0, "Equipment checkpoint is missing an action's input state");
}
