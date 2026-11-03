/* Port of src/app/bootstrap/simulation/arsenal/q2.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

typedef struct turn_entry { qa_actor_id actor; qa_app_q2_selected_weapon_turn_state turn; } turn_entry;
struct qa_app_q2_selected_arsenal {
  qa_app_q2_selected_arsenal_options options;
  const qa_q2w_definition **definitions;
  size_t definition_count;
  qa_item_id pickup_order[32];
  size_t pickup_order_count;
  turn_entry turns[64];
  size_t turn_count;
};

static bool item_eq(qa_item_id a, qa_item_id b) { return a && b && strcmp(a, b) == 0; }
static qa_q2w_state *state_of(qa_q2w_weapons *weapons, qa_actor_id actor) {
  for (size_t i = 0; i < weapons->base.state_count; i++)
    if (qa_same_actor(weapons->base.states[i].actor, actor)) return weapons->base.states[i].state;
  return NULL;
}
static qa_q2w_state *require_state(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q2w_state *state = state_of(arsenal->options.weapons, actor);
  if (!state) qa_range_error("Actor has no selected Q2 arsenal");
  return state;
}
static turn_entry *find_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  for (size_t i = 0; i < arsenal->turn_count; i++) if (qa_same_actor(arsenal->turns[i].actor, actor)) return &arsenal->turns[i];
  return NULL;
}
static qa_app_q2_selected_weapon_turn_state *require_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  turn_entry *entry = find_turn(arsenal, actor);
  if (!entry) qa_range_error("Selected Q2 arsenal has no command continuation");
  return &entry->turn;
}
static void delete_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  for (size_t i = 0; i < arsenal->turn_count; i++) {
    if (qa_same_actor(arsenal->turns[i].actor, actor)) {
      memmove(arsenal->turns + i, arsenal->turns + i + 1, (arsenal->turn_count - i - 1) * sizeof *arsenal->turns);
      arsenal->turn_count--;
      return;
    }
  }
}
static long order_index(qa_app_q2_selected_arsenal *arsenal, qa_item_id item) {
  for (size_t i = 0; i < arsenal->pickup_order_count; i++) if (item_eq(arsenal->pickup_order[i], item)) return (long)i;
  return -1;
}
static const qa_q2w_definition *find_definition(qa_app_q2_selected_arsenal *arsenal, qa_item_id item) {
  for (size_t i = 0; i < arsenal->definition_count; i++)
    if (item_eq(arsenal->definitions[i]->item, item)) return arsenal->definitions[i];
  return NULL;
}

qa_arsenal_state qa_app_project_q2_arsenal(qa_actor_id actor, qa_provider_id provider, qa_q2w_weapons *weapons,
    qa_app_inventory_access inventory) {
  qa_q2w_state *state = state_of(weapons, actor);
  if (!state) qa_range_error("Q2 player has no arsenal");
  qa_arsenal_state out; memset(&out, 0, sizeof out);
  out.provider = provider;
  out.active_weapon = state->weapon == NULL ? NULL : qa_q2w_weapons_definition(weapons, state->weapon)->item;
  size_t count = 0;
  out.ammo = inventory.entries(inventory.ctx, actor, &count);
  out.ammo_count = count;
  out.state.kind = "q2"; out.state.gun_frame = state->frame;
  out.state.state = state->phase == QA_Q2W_READY ? 0 : state->phase == QA_Q2W_ACTIVATING ? 1 : state->phase == QA_Q2W_DROPPING ? 2 : 3;
  out.state.pending_weapon = state->pending == NULL ? NULL : qa_q2w_weapons_definition(weapons, state->pending)->item;
  out.state.machinegun_shots = state->machinegun_shots;
  out.state.grenade_time.kind = QA_SOURCE_TIME_SECONDS; out.state.grenade_time.value = state->grenade_time;
  out.state.grenade_blew_up = state->grenade_blew_up;
  return out;
}

qa_app_q2_selected_arsenal *qa_app_q2_selected_arsenal_create(qa_app_q2_selected_arsenal_options options) {
  qa_app_q2_selected_arsenal *arsenal = calloc(1, sizeof *arsenal);
  if (!arsenal) return NULL;
  arsenal->options = options;
  size_t count = 0;
  const qa_q2w_definition *const *definitions = qa_q2w_weapons_registered_definitions(options.weapons, &count);
  arsenal->definitions = (const qa_q2w_definition **)definitions;
  arsenal->definition_count = count;
  if (options.pickup_order) {
    arsenal->pickup_order_count = options.pickup_order_count < 32 ? options.pickup_order_count : 32;
    memcpy(arsenal->pickup_order, options.pickup_order, arsenal->pickup_order_count * sizeof *arsenal->pickup_order);
  } else {
    arsenal->pickup_order_count = qa_q2w_base_weapon_count < 32 ? qa_q2w_base_weapon_count : 32;
    for (size_t i = 0; i < arsenal->pickup_order_count; i++) arsenal->pickup_order[i] = qa_q2w_base_weapons[i].item;
  }
  for (size_t i = 0; i < count; i++)
    if (order_index(arsenal, definitions[i]->item) < 0)
      qa_range_error("Selected Q2 arsenal requires an explicit pickup preference for every registered weapon");
  for (size_t i = 0; i < count; i++) {
    qa_item_id needed[2] = {definitions[i]->item, definitions[i]->ammo};
    size_t needed_count = definitions[i]->ammo ? 2 : 1;
    for (size_t j = 0; j < needed_count; j++) {
      bool found = false;
      for (size_t k = 0; k < options.inventory_definition_count; k++)
        if (item_eq(options.inventory_definitions[k].item, needed[j])) found = true;
      if (!found) qa_range_error("Missing source Q2 inventory definition %s", needed[j]);
    }
  }
  if (options.has_loadout) {
    if (!find_definition(arsenal, options.loadout_weapon))
      qa_range_error("Selected Q2 starter weapon is outside the registered arsenal");
    for (size_t i = 0; i < options.loadout_inventory_count; i++) {
      double amount = options.loadout_inventory[i].count;
      bool known = false;
      for (size_t j = 0; j < count; j++)
        if (item_eq(definitions[j]->item, options.loadout_inventory[i].item)
            || (definitions[j]->ammo && item_eq(definitions[j]->ammo, options.loadout_inventory[i].item))) known = true;
      if (!(isfinite(amount) && amount == trunc(amount)) || amount < 0 || !known)
        qa_range_error("Selected Q2 starter inventory is invalid");
    }
  }
  return arsenal;
}
void qa_app_q2_selected_arsenal_destroy(qa_app_q2_selected_arsenal *arsenal) { free(arsenal); }
void qa_app_q2_selected_arsenal_release_actor(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) { delete_turn(arsenal, actor); }
size_t qa_app_q2_selected_arsenal_catalog(qa_app_q2_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < arsenal->definition_count && count < capacity; i++) {
    out[count].item = arsenal->definitions[i]->item; out[count].ammo = arsenal->definitions[i]->ammo; out[count].drop = "supply"; count++;
  }
  return count;
}
bool qa_app_q2_selected_arsenal_has(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  return state_of(arsenal->options.weapons, actor) != NULL;
}
static bool in_items(qa_app_q2_selected_arsenal *arsenal, qa_item_id item) {
  for (size_t i = 0; i < arsenal->definition_count; i++) {
    if (item_eq(arsenal->definitions[i]->item, item)) return true;
    if (arsenal->definitions[i]->ammo && item_eq(arsenal->definitions[i]->ammo, item)) return true;
  }
  return false;
}
qa_arsenal_state qa_app_q2_selected_arsenal_admit(qa_app_q2_selected_arsenal *arsenal, qa_owned_actor actor, double max_health) {
  (void)max_health;
  if (qa_app_q2_selected_arsenal_has(arsenal, actor.id)) qa_range_error("Selected Q2 arsenal already admitted");
  qa_app_inventory_access inventory = arsenal->options.inventory;
  size_t entry_count = 0;
  const qa_inventory_entry *entries = inventory.entries(inventory.ctx, actor.id, &entry_count);
  for (size_t i = 0; i < entry_count; i++) {
    bool replaced = false;
    for (size_t j = 0; j < arsenal->options.replaced_item_count; j++)
      if (item_eq(entries[i].item, arsenal->options.replaced_items[j])) replaced = true;
    if (replaced && !in_items(arsenal, entries[i].item)) {
      qa_inventory_entry cleared = entries[i]; cleared.count = 0;
      inventory.configure(inventory.ctx, actor, cleared);
    }
  }
  for (size_t i = 0; i < arsenal->options.inventory_definition_count; i++) {
    qa_item_id item = arsenal->options.inventory_definitions[i].item;
    if (!in_items(arsenal, item)) continue;
    size_t current_count = 0;
    const qa_inventory_entry *current = inventory.entries(inventory.ctx, actor.id, &current_count);
    bool present = false;
    for (size_t j = 0; j < current_count; j++) if (item_eq(current[j].item, item)) present = true;
    if (!present) {
      qa_inventory_entry entry; memset(&entry, 0, sizeof entry);
      entry.item = item; entry.capacity = arsenal->options.inventory_definitions[i].capacity; entry.count = 0;
      inventory.configure(inventory.ctx, actor, entry);
    }
  }
  if (arsenal->options.has_loadout) {
    size_t count = 0;
    const qa_inventory_entry *current = inventory.entries(inventory.ctx, actor.id, &count);
    for (size_t i = 0; i < count; i++) {
      if (!in_items(arsenal, current[i].item)) continue;
      double amount = 0;
      for (size_t j = 0; j < arsenal->options.loadout_inventory_count; j++)
        if (item_eq(arsenal->options.loadout_inventory[j].item, current[i].item)) amount = arsenal->options.loadout_inventory[j].count;
      qa_inventory_entry entry = current[i];
      entry.count = amount < entry.capacity ? amount : entry.capacity;
      inventory.configure(inventory.ctx, actor, entry);
    }
  } else if (inventory.count(inventory.ctx, actor.id, "q2:weapon_blaster") == 0) {
    inventory.give(inventory.ctx, actor, "q2:weapon_blaster", 1);
  }
  qa_q2w_name weapon = "blaster";
  if (arsenal->options.has_loadout) {
    const qa_q2w_definition *definition = find_definition(arsenal, arsenal->options.loadout_weapon);
    weapon = definition ? definition->name : NULL;
  }
  if (!weapon || inventory.count(inventory.ctx, actor.id, qa_q2w_weapons_definition(arsenal->options.weapons, weapon)->item) < 1)
    qa_range_error("Selected Q2 starter weapon is not owned");
  qa_q2w_state *state = malloc(sizeof *state);
  if (!state) qa_range_error("Out of memory");
  qa_q2w_state_init(state, weapon);
  qa_q2w_weapons_bind(arsenal->options.weapons, actor, arsenal->options.game, state);
  if (arsenal->turn_count < 64) {
    turn_entry *entry = &arsenal->turns[arsenal->turn_count++];
    entry->actor = actor.id; memset(&entry->turn, 0, sizeof entry->turn);
  }
  return qa_app_q2_selected_arsenal_read(arsenal, actor.id);
}
qa_arsenal_state qa_app_q2_selected_arsenal_read(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_arsenal_state state = qa_app_project_q2_arsenal(actor, arsenal->options.provider, arsenal->options.weapons, arsenal->options.inventory);
  qa_inventory_entry *ammo = malloc((state.ammo_count ? state.ammo_count : 1) * sizeof *ammo);
  if (!ammo) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < state.ammo_count; i++) if (in_items(arsenal, state.ammo[i].item)) ammo[count++] = state.ammo[i];
  state.ammo = ammo; state.ammo_count = count;
  return state;
}
static void observe(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor, qa_q2w_owner *owner, qa_q2w_input *input) {
  arsenal->options.observe(arsenal->options.ctx, actor, owner, input);
}
bool qa_app_q2_selected_arsenal_select(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item) {
  const qa_q2w_definition *definition = find_definition(arsenal, item);
  if (!definition) return false;
  qa_q2w_owner owner; qa_q2w_input input;
  observe(arsenal, actor, &owner, &input);
  qa_q2w_selection result = qa_q2w_weapons_request_weapon(arsenal->options.weapons, owner, arsenal->options.game, definition->name, false);
  const char *name = qa_q2w_selection_name(result);
  return name && (strcmp(name, "selected") == 0 || strcmp(name, "current") == 0);
}
qa_item_id qa_app_q2_selected_arsenal_pending_weapon(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q2w_state *state = require_state(arsenal, actor);
  return state->pending == NULL ? NULL : qa_q2w_weapons_definition(arsenal->options.weapons, state->pending)->item;
}

typedef struct handoff_ctx { qa_app_q2_selected_arsenal *arsenal; qa_actor_id actor; } handoff_ctx;
static handoff_ctx HANDOFFS[16];
static size_t handoff_cursor;
static bool handoff_accepts(void *v, qa_item_id item) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  const qa_q2w_definition *definition = find_definition(ctx->arsenal, item);
  qa_app_inventory_access inventory = ctx->arsenal->options.inventory;
  return definition && inventory.count(inventory.ctx, ctx->actor, item) > 0
      && (definition->ammo == NULL || inventory.count(inventory.ctx, ctx->actor, definition->ammo) >= definition->quantity);
}
static bool handoff_select(void *v, qa_item_id item) {
  handoff_ctx *ctx = (handoff_ctx *)v; return qa_app_q2_selected_arsenal_select(ctx->arsenal, ctx->actor, item);
}
static void handoff_holster(void *v) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  qa_q2w_owner owner; qa_q2w_input input; observe(ctx->arsenal, ctx->actor, &owner, &input);
  qa_q2w_weapons_request_holster(ctx->arsenal->options.weapons, owner);
}
static bool handoff_holstered(void *v) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  qa_q2w_owner owner; qa_q2w_input input; observe(ctx->arsenal, ctx->actor, &owner, &input);
  return qa_q2w_weapons_is_holstered(ctx->arsenal->options.weapons, owner);
}
static bool handoff_resume(void *v, qa_item_id item) {
  handoff_ctx *ctx = (handoff_ctx *)v;
  qa_q2w_owner owner; qa_q2w_input input; observe(ctx->arsenal, ctx->actor, &owner, &input);
  const qa_q2w_definition *definition = item == NULL ? NULL : find_definition(ctx->arsenal, item);
  if (!definition) qa_range_error("Weapon does not belong to selected Q2 arsenal");
  qa_q2w_weapons_resume_primary(ctx->arsenal->options.weapons, owner, ctx->arsenal->options.game, input, definition->name);
  qa_arsenal_state state = qa_app_q2_selected_arsenal_read(ctx->arsenal, ctx->actor);
  bool result = item == NULL || item_eq(state.active_weapon, item);
  free((void *)state.ammo);
  return result;
}
qa_app_primary_weapon_handoff qa_app_q2_selected_arsenal_handoff(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  handoff_ctx *ctx = &HANDOFFS[handoff_cursor++ % 16];
  ctx->arsenal = arsenal; ctx->actor = actor;
  qa_app_primary_weapon_handoff handoff; memset(&handoff, 0, sizeof handoff);
  handoff.ctx = ctx; handoff.kind = "immediate"; handoff.provider = arsenal->options.provider;
  handoff.accepts = handoff_accepts; handoff.select = handoff_select; handoff.holster = handoff_holster;
  handoff.is_holstered = handoff_holstered; handoff.resume = handoff_resume;
  return handoff;
}
void qa_app_q2_selected_arsenal_pickup_ammo(qa_app_q2_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch) {
  bool grenades = false;
  for (size_t i = 0; i < grant_count; i++) if (item_eq(grants[i].item, "q2:ammo_grenades") && grants[i].before == 0) grenades = true;
  if (auto_switch && grenades) {
    qa_item_id items[1] = {"q2:ammo_grenades"};
    qa_app_q2_selected_arsenal_pickup_weapons(arsenal, actor, items, 1, QA_PICKUP_SELECTION_BETTER);
  }
}
void qa_app_q2_selected_arsenal_pickup_weapons(qa_app_q2_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *items, size_t item_count, qa_pickup_selection selection) {
  if (selection == QA_PICKUP_SELECTION_NEVER) return;
  qa_item_id best = NULL;
  for (size_t i = 0; i < item_count; i++)
    if (best == NULL || order_index(arsenal, items[i]) > order_index(arsenal, best)) best = items[i];
  if (!best) return;
  qa_item_id pending = qa_app_q2_selected_arsenal_pending_weapon(arsenal, actor.id);
  qa_item_id current = pending;
  if (!current) {
    qa_arsenal_state state = qa_app_q2_selected_arsenal_read(arsenal, actor.id);
    current = state.active_weapon;
    free((void *)state.ammo);
  }
  if (selection == QA_PICKUP_SELECTION_ALWAYS || order_index(arsenal, best) > (current == NULL ? -1 : order_index(arsenal, current)))
    qa_app_q2_selected_arsenal_select(arsenal, actor.id, best);
}

typedef struct tick_ctx {
  qa_app_q2_selected_arsenal *arsenal;
  qa_q2w_owner owner;
  qa_q2w_input input;
  qa_app_q2_selected_weapon_turn_state *turn;
} tick_ctx;
static void tick_cb(void *v, bool latched_attack) {
  tick_ctx *ctx = (tick_ctx *)v;
  qa_q2w_input input = ctx->input;
  input.latched_attack = latched_attack; input.weapon_thunk = ctx->turn->turn.weapon_thunk;
  qa_q2w_weapons_tick(ctx->arsenal->options.weapons, ctx->owner, ctx->arsenal->options.game, input);
}
qa_weapon_step_result qa_app_q2_selected_arsenal_step(qa_app_q2_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  if (intent && (!intent->provider || !arsenal->options.provider || strcmp(intent->provider, arsenal->options.provider) != 0))
    qa_range_error("Arsenal intent belongs to another provider");
  /* Peers choose this string; a weapon outside the arsenal is no request. */
  if (intent && intent->weapon && find_definition(arsenal, intent->weapon)) qa_app_q2_selected_arsenal_select(arsenal, input.actor.id, intent->weapon);
  qa_arsenal_state before = qa_app_q2_selected_arsenal_read(arsenal, input.actor.id);
  qa_q2w_owner owner; qa_q2w_input observed;
  observe(arsenal, input.actor.id, &owner, &observed);
  qa_app_q2_selected_weapon_turn_state *turn = require_turn(arsenal, input.actor.id);
  double buttons = input.command.kind == QA_USER_COMMAND_Q1 ? input.command.as.q1.buttons
      : input.command.kind == QA_USER_COMMAND_QW ? input.command.as.qw.buttons
      : input.command.kind == QA_USER_COMMAND_Q2 ? input.command.as.q2.buttons
      : input.command.kind == QA_USER_COMMAND_Q2_RERELEASE ? input.command.as.q2_rerelease.buttons
      : input.command.as.q3.buttons;
  qa_q2w_latch_buttons(&turn->turn, (double)(((qa_js_to_int32(buttons)) & ~1) | (observed.attack ? 1 : 0)));
  if (!observed.spectator) {
    tick_ctx ctx = {arsenal, owner, observed, turn};
    qa_q2w_turn_tick tick = {&ctx, tick_cb};
    qa_q2w_early_turn(&turn->turn, tick);
  }
  qa_q2w_state *state = state_of(arsenal->options.weapons, input.actor.id);
  if (turn->has_firing && (!state || (turn->firing_weapon != state->weapon
      && !(turn->firing_weapon && state->weapon && strcmp(turn->firing_weapon, state->weapon) == 0)))) turn->has_firing = false;
  qa_weapon_step_result result; memset(&result, 0, sizeof result);
  result.animation = input.animation;
  if (arsenal->options.is_live && arsenal->options.is_live(arsenal->options.ctx, input.actor.id)) {
    result.arsenal = qa_app_q2_selected_arsenal_read(arsenal, input.actor.id);
    free((void *)before.ammo);
  } else if (!arsenal->options.is_live) {
    result.arsenal = qa_app_q2_selected_arsenal_read(arsenal, input.actor.id);
    free((void *)before.ammo);
  } else result.arsenal = before;
  return result;
}
static double health_of(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  double health = 0;
  return arsenal->options.combat_health && arsenal->options.combat_health(arsenal->options.ctx, actor, &health) ? health : 0;
}
void qa_app_q2_selected_arsenal_frame(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_app_q2_selected_weapon_turn_state *turn = require_turn(arsenal, actor);
  qa_q2w_owner owner; qa_q2w_input observed;
  observe(arsenal, actor, &owner, &observed);
  qa_q2w_state *state = require_state(arsenal, actor);
  double elapsed = arsenal->options.frame_seconds(arsenal->options.ctx);
  if (turn->has_firing && !(turn->firing_weapon && state->weapon && strcmp(turn->firing_weapon, state->weapon) == 0)) turn->has_firing = false;
  bool classic = arsenal->options.edition && strcmp(arsenal->options.edition, "classic") == 0;
  if (classic && !observed.spectator && state->phase == QA_Q2W_FIRING && state->weapon != NULL && health_of(arsenal, actor) > 0) {
    double interval = qa_q2w_weapons_firing_interval(arsenal->options.weapons, actor, elapsed);
    if (interval != elapsed) {
      if (!turn->has_firing) { turn->has_firing = true; turn->firing_weapon = state->weapon; turn->firing_credit = 0; }
      turn->firing_credit += elapsed / interval - 1;
    }
  }
  {
    tick_ctx ctx = {arsenal, owner, observed, turn};
    qa_q2w_turn_tick tick = {&ctx, tick_cb};
    qa_q2w_begin_turn(&turn->turn, !observed.spectator, tick);
  }
  while (turn->has_firing && turn->firing_credit >= 1
      && (!arsenal->options.is_live || arsenal->options.is_live(arsenal->options.ctx, actor))
      && find_turn(arsenal, actor) && &find_turn(arsenal, actor)->turn == turn
      && state_of(arsenal->options.weapons, actor) == state
      && state->weapon && turn->firing_weapon && strcmp(state->weapon, turn->firing_weapon) == 0
      && state->phase == QA_Q2W_FIRING && health_of(arsenal, actor) > 0) {
    turn->firing_credit -= 1;
    qa_q2w_owner current_owner; qa_q2w_input current;
    observe(arsenal, actor, &current_owner, &current);
    if (current.spectator) break;
    current.latched_attack = false; current.weapon_thunk = false;
    qa_q2w_weapons_tick(arsenal->options.weapons, current_owner, arsenal->options.game, current);
  }
  if (turn->has_firing) {
    if (!(state->weapon && turn->firing_weapon && strcmp(state->weapon, turn->firing_weapon) == 0)
        || state_of(arsenal->options.weapons, actor) != state) turn->has_firing = false;
    else turn->firing_credit = fmod(turn->firing_credit, 1.0);
  }
  if (health_of(arsenal, actor) > 0) turn->turn.latched_buttons = 0;
}
qa_app_q2_selected_weapon_turn_state qa_app_q2_selected_arsenal_capture_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_app_q2_selected_weapon_turn_state *turn = require_turn(arsenal, actor);
  qa_q2w_state *state = require_state(arsenal, actor);
  qa_app_q2_selected_weapon_turn_state copy = *turn;
  if (!(copy.has_firing && copy.firing_weapon && state->weapon && strcmp(copy.firing_weapon, state->weapon) == 0)) copy.has_firing = false;
  return copy;
}
void qa_app_q2_selected_arsenal_restore_turn(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor,
    qa_app_q2_selected_weapon_turn_state turn) {
  qa_q2w_state *state = require_state(arsenal, actor);
  if (turn.has_firing && (!(turn.firing_weapon && state->weapon && strcmp(turn.firing_weapon, state->weapon) == 0)
      || !isfinite(turn.firing_credit) || turn.firing_credit < 0 || turn.firing_credit >= 1))
    qa_range_error("Invalid saved Q2 firing credit");
  turn_entry *entry = find_turn(arsenal, actor);
  if (!entry) {
    if (arsenal->turn_count >= 64) qa_range_error("Out of memory");
    entry = &arsenal->turns[arsenal->turn_count++]; entry->actor = actor;
  }
  entry->turn = turn;
}
void qa_app_q2_selected_arsenal_remove(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q2w_weapons *weapons = arsenal->options.weapons;
  qa_q2w_state *state = state_of(weapons, actor);
  if (state && (!arsenal->options.is_live || arsenal->options.is_live(arsenal->options.ctx, actor))) {
    qa_q2w_owner owner; qa_q2w_input input; observe(arsenal, actor, &owner, &input);
    qa_q2w_weapons_set_loop(weapons, owner, arsenal->options.game, state, "");
  }
  for (size_t i = 0; i < weapons->base.state_count; i++) {
    if (qa_same_actor(weapons->base.states[i].actor, actor)) {
      free(weapons->base.states[i].state);
      memmove(weapons->base.states + i, weapons->base.states + i + 1, (weapons->base.state_count - i - 1) * sizeof *weapons->base.states);
      weapons->base.state_count--; break;
    }
  }
  for (size_t i = 0; i < weapons->base.input_count; i++) {
    if (qa_same_actor(weapons->base.inputs[i].actor, actor)) {
      memmove(weapons->base.inputs + i, weapons->base.inputs + i + 1, (weapons->base.input_count - i - 1) * sizeof *weapons->base.inputs);
      weapons->base.input_count--; break;
    }
  }
  delete_turn(arsenal, actor);
}
static double ui_count(void *v, qa_item_id item) {
  struct { qa_app_inventory_access inventory; qa_actor_id actor; } *ctx = v;
  return ctx->inventory.count(ctx->inventory.ctx, ctx->actor, item);
}
qa_app_arsenal_ui qa_app_q2_selected_arsenal_ui(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity) {
  qa_q2w_state *state = require_state(arsenal, actor);
  qa_app_inventory_access inventory = arsenal->options.inventory;
  const qa_q2w_definition *active = state->weapon == NULL ? NULL : qa_q2w_weapons_definition(arsenal->options.weapons, state->weapon);
  qa_app_arsenal_ui ui; memset(&ui, 0, sizeof ui);
  struct { qa_app_inventory_access inventory; qa_actor_id actor; } ctx = {inventory, actor};
  char label[64] = {0};
  ui.has_weapon_status = qa_app_q2_weapon_status(active, ui_count, &ctx, source, label, sizeof label, &ui.weapon_status);
  if (ui.has_weapon_status && labels && label_capacity) { memcpy(labels[0], label, strlen(label) + 1); ui.weapon_status.label = labels[0]; }
  ui.arsenal_warning = "none";
  ui.active_weapon = active ? active->item : NULL;
  if (active && active->ammo) {
    ui.has_ammo = true; ui.ammo_item = active->ammo;
    ui.ammo_count = inventory.count(inventory.ctx, actor, active->ammo);
  }
  ui.items = items; ui.item_capacity = capacity;
  for (size_t i = 0; i < arsenal->definition_count && ui.item_count < capacity; i++) {
    const qa_q2w_definition *definition = arsenal->definitions[i];
    qa_app_player_ui_item *entry = &items[ui.item_count];
    memset(entry, 0, sizeof *entry);
    entry->id = definition->item; entry->kind = "weapon"; entry->source_ordinal = (double)(i + 1);
    if (labels && ui.item_count + 1 < label_capacity) {
      const char *name = qa_q2_base_weapon_display_name(definition->item);
      if (!name) name = qa_q2mp_mission_weapon_display_name(definition->name);
      if (!name) name = definition->name;
      strncpy(labels[ui.item_count + 1], name, 63);
      entry->label = labels[ui.item_count + 1];
    }
    entry->owned = inventory.count(inventory.ctx, actor, definition->item) > 0;
    entry->has_ammo = definition->ammo == NULL || inventory.count(inventory.ctx, actor, definition->ammo) >= definition->quantity;
    entry->has_count = definition->ammo != NULL;
    entry->count = definition->ammo ? inventory.count(inventory.ctx, actor, definition->ammo) : 0;
    entry->warning_count = definition->warning;
    ui.item_count++;
  }
  return ui;
}
qa_app_arsenal_view qa_app_q2_selected_arsenal_view(qa_app_q2_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q2w_state *state = require_state(arsenal, actor);
  qa_app_arsenal_view view; memset(&view, 0, sizeof view);
  if (state->weapon == NULL || state->primary_handoff == QA_Q2W_HANDOFF_HOLSTERED) return view;
  view.has_value = true;
  view.path = state->view_model ? state->view_model : qa_q2w_weapons_definition(arsenal->options.weapons, state->weapon)->view_model;
  view.frame = state->frame;
  return view;
}

static size_t iface_catalog(void *v, qa_selected_pickup_weapon *out, size_t capacity) { return qa_app_q2_selected_arsenal_catalog((qa_app_q2_selected_arsenal *)v, out, capacity); }
static bool iface_has(void *v, qa_actor_id actor) { return qa_app_q2_selected_arsenal_has((qa_app_q2_selected_arsenal *)v, actor); }
static qa_arsenal_state iface_admit(void *v, qa_owned_actor actor, double max_health, bool team_deathmatch) {
  (void)team_deathmatch; return qa_app_q2_selected_arsenal_admit((qa_app_q2_selected_arsenal *)v, actor, max_health);
}
static qa_arsenal_state iface_read(void *v, qa_actor_id actor) { return qa_app_q2_selected_arsenal_read((qa_app_q2_selected_arsenal *)v, actor); }
static bool iface_select(void *v, qa_actor_id actor, qa_item_id item) { return qa_app_q2_selected_arsenal_select((qa_app_q2_selected_arsenal *)v, actor, item); }
static void iface_pickup_ammo(void *v, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  qa_app_q2_selected_arsenal_pickup_ammo((qa_app_q2_selected_arsenal *)v, actor, grants, count, auto_switch);
}
static void iface_pickup_weapons(void *v, qa_owned_actor actor, const qa_item_id *items, size_t count, qa_pickup_selection selection) {
  qa_app_q2_selected_arsenal_pickup_weapons((qa_app_q2_selected_arsenal *)v, actor, items, count, selection);
}
static qa_item_id iface_pending(void *v, qa_actor_id actor) { return qa_app_q2_selected_arsenal_pending_weapon((qa_app_q2_selected_arsenal *)v, actor); }
static qa_app_primary_weapon_handoff iface_handoff(void *v, qa_actor_id actor) { return qa_app_q2_selected_arsenal_handoff((qa_app_q2_selected_arsenal *)v, actor); }
static qa_weapon_step_result iface_step(void *v, qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  return qa_app_q2_selected_arsenal_step((qa_app_q2_selected_arsenal *)v, input, intent);
}
static void iface_remove(void *v, qa_actor_id actor) { qa_app_q2_selected_arsenal_remove((qa_app_q2_selected_arsenal *)v, actor); }
static qa_app_arsenal_ui iface_ui(void *v, qa_actor_id actor, qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity) {
  return qa_app_q2_selected_arsenal_ui((qa_app_q2_selected_arsenal *)v, actor, source, items, capacity, NULL, 0);
}
static qa_app_arsenal_view iface_view(void *v, qa_actor_id actor) { return qa_app_q2_selected_arsenal_view((qa_app_q2_selected_arsenal *)v, actor); }
qa_app_selected_arsenal qa_app_q2_selected_arsenal_interface(qa_app_q2_selected_arsenal *arsenal) {
  qa_app_selected_arsenal iface; memset(&iface, 0, sizeof iface);
  iface.ctx = arsenal; iface.family = "q2"; iface.provider = arsenal->options.provider;
  iface.catalog = iface_catalog; iface.has = iface_has; iface.admit = iface_admit; iface.read = iface_read;
  iface.select = iface_select; iface.pickup_ammo = iface_pickup_ammo; iface.pickup_weapons = iface_pickup_weapons;
  iface.pending_weapon = iface_pending; iface.handoff = iface_handoff; iface.step = iface_step;
  iface.remove = iface_remove; iface.ui = iface_ui; iface.view = iface_view;
  return iface;
}
