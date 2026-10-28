/* Port of src/app/bootstrap/simulation/arsenal/q1.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q1.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

struct qa_app_q1_selected_arsenal {
  qa_app_q1_selected_arsenal_options options;
  qa_q1_weapon weapons[32];
  size_t weapon_count;
};

static bool item_eq(qa_item_id a, qa_item_id b) { return a && b && strcmp(a, b) == 0; }
static bool weapon_in(qa_app_q1_selected_arsenal *arsenal, qa_q1_weapon weapon) {
  for (size_t i = 0; i < arsenal->weapon_count; i++) if (weapon && strcmp(arsenal->weapons[i], weapon) == 0) return true;
  return false;
}
static qa_q1_player_state *require_player(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = arsenal->options.game.player(arsenal->options.game.ctx, actor);
  if (!player) qa_range_error("Actor has no selected Q1 arsenal");
  return player;
}
static bool owns_entry(qa_app_q1_selected_arsenal *arsenal, qa_item_id item) {
  qa_app_q1_game_host *game = &arsenal->options.game;
  for (size_t i = 0; i < arsenal->weapon_count; i++) {
    if (item_eq(game->weapon_item(game->ctx, arsenal->weapons[i]), item)) return true;
    qa_item_id ammo = game->weapon_ammo(game->ctx, arsenal->weapons[i]);
    if (ammo && item_eq(ammo, item)) return true;
  }
  return false;
}

qa_app_q1_selected_arsenal *qa_app_q1_selected_arsenal_create(qa_app_q1_selected_arsenal_options options) {
  qa_app_q1_selected_arsenal *arsenal = calloc(1, sizeof *arsenal);
  if (!arsenal) return NULL;
  arsenal->options = options;
  for (size_t i = 0; i < 8; i++) arsenal->weapons[arsenal->weapon_count++] = QA_Q1_WEAPONS[i];
  size_t registered_count = 0;
  const qa_q1_weapon *registered = options.game.registered_weapons ? options.game.registered_weapons(options.game.ctx, &registered_count) : NULL;
  for (size_t i = 0; i < registered_count && arsenal->weapon_count < 32; i++)
    if (!weapon_in(arsenal, registered[i])) arsenal->weapons[arsenal->weapon_count++] = registered[i];
  return arsenal;
}
void qa_app_q1_selected_arsenal_destroy(qa_app_q1_selected_arsenal *arsenal) { free(arsenal); }
const qa_q1_weapon *qa_app_q1_selected_arsenal_weapons(const qa_app_q1_selected_arsenal *arsenal, size_t *out_count) {
  *out_count = arsenal->weapon_count; return arsenal->weapons;
}
bool qa_app_q1_selected_arsenal_has(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  return arsenal->options.game.player(arsenal->options.game.ctx, actor) != NULL;
}

qa_arsenal_state qa_app_q1_selected_arsenal_read(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_app_q1_game_host *game = &arsenal->options.game;
  if (!weapon_in(arsenal, player->weapon)) qa_range_error("Selected Q1 player has a weapon from another product");
  qa_arsenal_state state; memset(&state, 0, sizeof state);
  state.provider = game->provider;
  state.active_weapon = game->weapon_item(game->ctx, player->weapon);
  size_t entry_count = 0;
  const qa_inventory_entry *entries = game->inventory.entries(game->inventory.ctx, actor, &entry_count);
  qa_inventory_entry *ammo = malloc((entry_count ? entry_count : 1) * sizeof *ammo);
  if (!ammo) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < entry_count; i++) if (owns_entry(arsenal, entries[i].item)) ammo[count++] = entries[i];
  state.ammo = ammo; state.ammo_count = count;
  state.state.kind = "q1"; state.state.frame = player->weapon_frame;
  state.state.attack_finished_seconds = player->attack_finished;
  state.state.source_weapon = qa_q1_is_base_weapon(player->weapon) ? qa_q1_weapon_bit(player->weapon) : 0;
  return state;
}

qa_arsenal_state qa_app_q1_selected_arsenal_admit(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor, double max_health) {
  qa_app_q1_game_host *game = &arsenal->options.game;
  if (game->player(game->ctx, actor.id) != NULL) qa_range_error("Selected Q1 arsenal already admitted");
  size_t entry_count = 0;
  const qa_inventory_entry *entries = game->inventory.entries(game->inventory.ctx, actor.id, &entry_count);
  for (size_t i = 0; i < entry_count; i++) {
    for (size_t j = 0; j < arsenal->options.replaced_item_count; j++) {
      if (item_eq(entries[i].item, arsenal->options.replaced_items[j])) {
        qa_inventory_entry cleared = entries[i]; cleared.count = 0;
        game->inventory.configure(game->inventory.ctx, actor, cleared);
      }
    }
  }
  qa_app_q1_native_player native; memset(&native, 0, sizeof native);
  if (arsenal->options.native_player) native = arsenal->options.native_player(arsenal->options.ctx, actor.id);
  if (!native.has_value) game->initialize_weapon_inventory(game->ctx, actor);
  size_t registered_count = 0;
  const qa_q1_weapon *registered = game->registered_weapons ? game->registered_weapons(game->ctx, &registered_count) : NULL;
  for (size_t i = 0; i < registered_count; i++) {
    qa_item_id item = game->weapon_item(game->ctx, registered[i]);
    qa_inventory_entry entry; memset(&entry, 0, sizeof entry);
    entry.item = item; entry.count = game->inventory.count(game->inventory.ctx, actor.id, item); entry.capacity = 1;
    game->inventory.configure(game->inventory.ctx, actor, entry);
  }
  qa_q1_player_state *player = game->attach_player(game->ctx, actor, native.has_value, native.weapon,
      native.has_value ? native.max_health : max_health);
  if (native.has_value && native.has_auto_switch) player->auto_switch = native.auto_switch;
  return qa_app_q1_selected_arsenal_read(arsenal, actor.id);
}

void qa_app_q1_selected_arsenal_travel_destroy(qa_app_q1_selected_arsenal_travel *travel) {
  if (!travel) return;
  free((void *)travel->inventory);
  for (size_t i = 0; i < travel->extension_count; i++) free(travel->extensions[i].bytes);
  free((void *)travel->extensions);
  memset(travel, 0, sizeof *travel);
}

qa_app_q1_selected_arsenal_travel qa_app_q1_selected_arsenal_capture_travel(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_app_q1_game_host *game = &arsenal->options.game;
  qa_app_q1_selected_arsenal_travel travel; memset(&travel, 0, sizeof travel);
  travel.weapon = player->weapon;
  qa_arsenal_state state = qa_app_q1_selected_arsenal_read(arsenal, actor);
  travel.inventory = state.ammo; travel.inventory_count = state.ammo_count;
  size_t total = game->extension_count ? game->extension_count(game->ctx) : 0;
  qa_app_q1_travel_extension *extensions = malloc((total ? total : 1) * sizeof *extensions);
  if (!extensions) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < total; i++) {
    const uint8_t *bytes = NULL; size_t byte_count = 0;
    if (game->extension_capture && game->extension_capture(game->ctx, i, player, &bytes, &byte_count)) {
      extensions[count].id = game->extension_id(game->ctx, i);
      extensions[count].bytes = malloc(byte_count ? byte_count : 1);
      if (!extensions[count].bytes) qa_range_error("Out of memory");
      memcpy(extensions[count].bytes, bytes, byte_count);
      extensions[count].byte_count = byte_count;
      count++;
    }
  }
  travel.extensions = extensions; travel.extension_count = count;
  return travel;
}

qa_arsenal_state qa_app_q1_selected_arsenal_admit_travel(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    double max_health, qa_app_q1_selected_arsenal_travel travel) {
  qa_app_q1_game_host *game = &arsenal->options.game;
  if (game->player(game->ctx, actor.id) != NULL) qa_range_error("Selected Q1 arsenal already admitted");
  bool owned = false;
  for (size_t i = 0; i < travel.inventory_count; i++)
    if (item_eq(travel.inventory[i].item, game->weapon_item(game->ctx, travel.weapon)) && travel.inventory[i].count > 0) owned = true;
  if (!weapon_in(arsenal, travel.weapon) || !owned)
    qa_range_error("Selected Q1 travel weapon is not owned by this product");
  for (size_t i = 0; i < travel.inventory_count; i++)
    if (!owns_entry(arsenal, travel.inventory[i].item))
      qa_range_error("Selected Q1 travel contains another component's inventory");
  for (size_t i = 0; i < travel.extension_count; i++) {
    for (size_t j = 0; j < i; j++)
      if (strcmp(travel.extensions[i].id, travel.extensions[j].id) == 0)
        qa_range_error("Invalid selected Q1 travel extension: %s", travel.extensions[i].id);
  }
  qa_q1_player_state *player = game->attach_player(game->ctx, actor, true, travel.weapon, max_health);
  for (size_t i = 0; i < travel.inventory_count; i++) {
    qa_inventory_entry entry = travel.inventory[i];
    double capacity;
    if (game->inventory_capacity && game->inventory_capacity(game->ctx, actor.id, entry.item, &capacity)) entry.capacity = capacity;
    game->inventory.configure(game->inventory.ctx, actor, entry);
  }
  for (size_t i = 0; i < travel.extension_count; i++) {
    if (!game->extension_restore || !game->extension_restore(game->ctx, travel.extensions[i].id, player,
            travel.extensions[i].bytes, travel.extensions[i].byte_count))
      qa_range_error("Invalid selected Q1 travel extension: %s", travel.extensions[i].id);
  }
  return qa_app_q1_selected_arsenal_read(arsenal, actor.id);
}

bool qa_app_q1_selected_arsenal_select(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_app_q1_game_host *game = &arsenal->options.game;
  for (size_t i = 0; i < arsenal->weapon_count; i++)
    if (item_eq(game->weapon_item(game->ctx, arsenal->weapons[i]), item))
      return game->select_weapon(game->ctx, player->actor, arsenal->weapons[i]);
  return false;
}
qa_item_id qa_app_q1_selected_arsenal_pending_weapon(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  (void)require_player(arsenal, actor); return NULL;
}
bool qa_app_q1_selected_arsenal_impulse(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, double value) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  if (value == 0 || arsenal->options.game.time < player->attack_finished) return false;
  if (arsenal->options.has_impulse) return arsenal->options.impulse(arsenal->options.ctx, player, value);
  return arsenal->options.game.weapon_impulse(arsenal->options.game.ctx, player, value);
}
qa_app_primary_weapon_handoff qa_app_q1_selected_arsenal_handoff(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  return arsenal->options.game.primary_handoff(arsenal->options.game.ctx, player->actor);
}

typedef struct count_ctx { qa_app_q1_selected_arsenal *arsenal; qa_actor_id actor; const qa_pickup_ammo_receipt *grants; size_t grant_count; } count_ctx;
static double grant_count_fn(void *v, qa_item_id item) {
  count_ctx *ctx = (count_ctx *)v;
  for (size_t i = 0; i < ctx->grant_count; i++) if (item_eq(ctx->grants[i].item, item)) return ctx->grants[i].before;
  return ctx->arsenal->options.game.inventory.count(ctx->arsenal->options.game.inventory.ctx, ctx->actor, item);
}
static const char *native_auto_switch(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, qa_q1_player_state *player) {
  if (arsenal->options.native_player) {
    qa_app_q1_native_player native = arsenal->options.native_player(arsenal->options.ctx, actor);
    if (native.has_value && native.has_auto_switch) return native.auto_switch;
    if (native.has_value) return player->auto_switch;
  }
  return player->auto_switch;
}
void qa_app_q1_selected_arsenal_pickup_ammo(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch) {
  qa_q1_player_state *player = require_player(arsenal, actor.id);
  if (arsenal->options.prepare_pickup) arsenal->options.prepare_pickup(arsenal->options.ctx, player);
  count_ctx ctx = {arsenal, actor.id, grants, grant_count};
  qa_q1_weapon before = arsenal->options.game.choose_best(arsenal->options.game.ctx, actor, grant_count_fn, &ctx);
  const char *mode = native_auto_switch(arsenal, actor.id, player);
  arsenal->options.game.ammo_pickup_selection(arsenal->options.game.ctx, player, before,
      auto_switch && !(mode && strcmp(mode, "never") == 0));
}
void qa_app_q1_selected_arsenal_pickup_weapons(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection) {
  qa_q1_player_state *player = require_player(arsenal, actor.id);
  if (arsenal->options.prepare_pickup) arsenal->options.prepare_pickup(arsenal->options.ctx, player);
  const char *mode = native_auto_switch(arsenal, actor.id, player);
  const char *effective = mode && strcmp(mode, "never") == 0 ? "never"
      : selection == QA_PICKUP_SELECTION_NEVER ? "never" : selection == QA_PICKUP_SELECTION_ALWAYS ? "always" : "better";
  for (size_t i = 0; i < weapon_count; i++) {
    for (size_t j = 0; j < arsenal->weapon_count; j++) {
      if (item_eq(arsenal->options.game.weapon_item(arsenal->options.game.ctx, arsenal->weapons[j]), weapons[i]))
        arsenal->options.game.weapon_pickup_selection(arsenal->options.game.ctx, player, arsenal->weapons[j], effective);
    }
  }
}

qa_weapon_step_result qa_app_q1_selected_arsenal_step(qa_app_q1_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  qa_app_q1_game_host *game = &arsenal->options.game;
  if (intent && (!intent->provider || !game->provider || strcmp(intent->provider, game->provider) != 0))
    qa_range_error("Arsenal intent belongs to a different provider");
  if (intent && intent->weapon) {
    bool found = false;
    for (size_t i = 0; i < arsenal->weapon_count; i++)
      if (item_eq(game->weapon_item(game->ctx, arsenal->weapons[i]), intent->weapon)) found = true;
    if (!found) qa_range_error("Weapon does not belong to the selected Q1 product");
  }
  if (intent && intent->weapon
      && !item_eq(game->weapon_item(game->ctx, require_player(arsenal, input.actor.id)->weapon), intent->weapon))
    qa_app_q1_selected_arsenal_select(arsenal, input.actor.id, intent->weapon);
  qa_arsenal_state before = qa_app_q1_selected_arsenal_read(arsenal, input.actor.id);
  qa_vec3 view_angles = {0, 0, 0}; double water_level = 0;
  arsenal->options.observe(arsenal->options.ctx, input.actor.id, &view_angles, &water_level);
  double seconds = input.frame.time.kind == QA_SOURCE_TIME_SECONDS ? input.frame.time.value : input.frame.time.value / 1000;
  double buttons = input.command.kind == QA_USER_COMMAND_Q1 ? input.command.as.q1.buttons
      : input.command.kind == QA_USER_COMMAND_QW ? input.command.as.qw.buttons
      : input.command.kind == QA_USER_COMMAND_Q2 ? input.command.as.q2.buttons
      : input.command.kind == QA_USER_COMMAND_Q2_RERELEASE ? input.command.as.q2_rerelease.buttons
      : input.command.as.q3.buttons;
  bool pressed = (((long long)buttons) & 1) != 0
      && (input.command.kind != QA_USER_COMMAND_Q3 || (((long long)buttons) & QA_Q3_BUTTON_TALK) == 0);
  qa_q1_weapon weapon = require_player(arsenal, input.actor.id)->weapon;
  bool fired = game->weapon_input(game->ctx, input.actor, pressed, view_angles, seconds, water_level);
  bool live = game->is_live(game->ctx, input.actor.id);
  qa_weapon_step_result result; memset(&result, 0, sizeof result);
  result.animation = fired && live && arsenal->options.fired
      ? arsenal->options.fired(arsenal->options.ctx, input.actor.id, weapon, input.animation) : input.animation;
  if (live) {
    result.arsenal = qa_app_q1_selected_arsenal_read(arsenal, input.actor.id);
    free((void *)before.ammo);
  } else result.arsenal = before;
  return result;
}
void qa_app_q1_selected_arsenal_frame(qa_app_q1_selected_arsenal *arsenal, double seconds) {
  size_t count = 0;
  const qa_q1_player_state *const *players = arsenal->options.game.players(arsenal->options.game.ctx, &count);
  for (size_t i = 0; i < count; i++) arsenal->options.game.weapon_frame(arsenal->options.game.ctx, players[i]->actor, seconds);
}
void qa_app_q1_selected_arsenal_remove(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = arsenal->options.game.player(arsenal->options.game.ctx, actor);
  if (player) arsenal->options.game.delete_player(arsenal->options.game.ctx, player->actor);
}
static qa_item_id host_weapon_item(void *v, qa_q1_weapon weapon) {
  qa_app_q1_game_host *game = (qa_app_q1_game_host *)v; return game->weapon_item(game->ctx, weapon);
}
static qa_item_id host_weapon_ammo(void *v, qa_q1_weapon weapon) {
  qa_app_q1_game_host *game = (qa_app_q1_game_host *)v; return game->weapon_ammo(game->ctx, weapon);
}
static double host_count(void *v, qa_actor_id actor, qa_item_id item) {
  qa_app_q1_game_host *game = (qa_app_q1_game_host *)v;
  return game->inventory.count(game->inventory.ctx, actor, item);
}
static bool host_available(void *v, qa_q1_player_state *player, qa_q1_weapon weapon, const char *purpose) {
  (void)purpose;
  qa_app_q1_game_host *game = (qa_app_q1_game_host *)v; return game->weapon_available(game->ctx, player, weapon);
}
qa_app_arsenal_ui qa_app_q1_selected_arsenal_ui(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_app_q1_game_host *game = &arsenal->options.game;
  qa_app_arsenal_ui ui; memset(&ui, 0, sizeof ui);
  qa_app_q1_status_host host = {game, host_weapon_item, host_weapon_ammo, host_count, host_available};
  char label[64] = {0};
  ui.weapon_status = qa_app_q1_weapon_status(host, player, source, label, sizeof label);
  ui.has_weapon_status = true;
  if (labels && label_capacity) { memcpy(labels[0], label, strlen(label) + 1); ui.weapon_status.label = labels[0]; }
  ui.arsenal_warning = "none";
  ui.active_weapon = game->weapon_item(game->ctx, player->weapon);
  qa_item_id ammo = game->weapon_ammo(game->ctx, player->weapon);
  if (ammo) { ui.has_ammo = true; ui.ammo_item = ammo; ui.ammo_count = game->inventory.count(game->inventory.ctx, actor, ammo); }
  ui.items = items; ui.item_capacity = capacity;
  for (size_t i = 0; i < arsenal->weapon_count && ui.item_count < capacity; i++) {
    qa_q1_weapon weapon = arsenal->weapons[i];
    qa_item_id item = game->weapon_item(game->ctx, weapon);
    qa_item_id weapon_ammo = game->weapon_ammo(game->ctx, weapon);
    qa_app_player_ui_item *entry = &items[ui.item_count];
    memset(entry, 0, sizeof *entry);
    entry->id = item; entry->kind = "weapon"; entry->source_ordinal = (double)(i + 1);
    if (labels && ui.item_count + 1 < label_capacity) {
      qa_app_q1_weapon_display_name(weapon, labels[ui.item_count + 1], 64);
      entry->label = labels[ui.item_count + 1];
    }
    entry->owned = game->inventory.count(game->inventory.ctx, actor, item) > 0;
    entry->has_ammo = game->weapon_available(game->ctx, player, weapon);
    entry->has_count = weapon_ammo != NULL;
    entry->count = weapon_ammo ? game->inventory.count(game->inventory.ctx, actor, weapon_ammo) : 0;
    entry->warning_count = 0;
    ui.item_count++;
  }
  return ui;
}
qa_app_arsenal_view qa_app_q1_selected_arsenal_view(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_app_arsenal_view view; memset(&view, 0, sizeof view);
  view.has_value = true;
  view.path = arsenal->options.game.weapon_model(arsenal->options.game.ctx, player->weapon, player);
  view.frame = player->weapon_frame;
  return view;
}
size_t qa_app_q1_selected_arsenal_catalog(qa_app_q1_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < arsenal->weapon_count && count < capacity; i++) {
    qa_q1_weapon weapon = arsenal->weapons[i];
    out[count].item = arsenal->options.game.weapon_item(arsenal->options.game.ctx, weapon);
    out[count].ammo = arsenal->options.game.weapon_ammo(arsenal->options.game.ctx, weapon);
    out[count].drop = (strcmp(weapon, "rogue:grapple") == 0 || strcmp(weapon, "ctf:grapple") == 0) ? "none" : "supply";
    count++;
  }
  return count;
}

static size_t iface_catalog(void *v, qa_selected_pickup_weapon *out, size_t capacity) {
  return qa_app_q1_selected_arsenal_catalog((qa_app_q1_selected_arsenal *)v, out, capacity);
}
static bool iface_has(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_has((qa_app_q1_selected_arsenal *)v, actor); }
static qa_arsenal_state iface_admit(void *v, qa_owned_actor actor, double max_health, bool team_deathmatch) {
  (void)team_deathmatch; return qa_app_q1_selected_arsenal_admit((qa_app_q1_selected_arsenal *)v, actor, max_health);
}
static qa_arsenal_state iface_read(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_read((qa_app_q1_selected_arsenal *)v, actor); }
static bool iface_select(void *v, qa_actor_id actor, qa_item_id item) { return qa_app_q1_selected_arsenal_select((qa_app_q1_selected_arsenal *)v, actor, item); }
static void iface_pickup_ammo(void *v, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  qa_app_q1_selected_arsenal_pickup_ammo((qa_app_q1_selected_arsenal *)v, actor, grants, count, auto_switch);
}
static void iface_pickup_weapons(void *v, qa_owned_actor actor, const qa_item_id *weapons, size_t count, qa_pickup_selection selection) {
  qa_app_q1_selected_arsenal_pickup_weapons((qa_app_q1_selected_arsenal *)v, actor, weapons, count, selection);
}
static qa_item_id iface_pending(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_pending_weapon((qa_app_q1_selected_arsenal *)v, actor); }
static qa_app_primary_weapon_handoff iface_handoff(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_handoff((qa_app_q1_selected_arsenal *)v, actor); }
static qa_weapon_step_result iface_step(void *v, qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  return qa_app_q1_selected_arsenal_step((qa_app_q1_selected_arsenal *)v, input, intent);
}
static void iface_remove(void *v, qa_actor_id actor) { qa_app_q1_selected_arsenal_remove((qa_app_q1_selected_arsenal *)v, actor); }
static qa_app_arsenal_ui iface_ui(void *v, qa_actor_id actor, qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity) {
  return qa_app_q1_selected_arsenal_ui((qa_app_q1_selected_arsenal *)v, actor, source, items, capacity, NULL, 0);
}
static qa_app_arsenal_view iface_view(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_view((qa_app_q1_selected_arsenal *)v, actor); }
qa_app_selected_arsenal qa_app_q1_selected_arsenal_interface(qa_app_q1_selected_arsenal *arsenal) {
  qa_app_selected_arsenal iface; memset(&iface, 0, sizeof iface);
  iface.ctx = arsenal; iface.family = "q1"; iface.provider = arsenal->options.game.provider;
  iface.catalog = iface_catalog; iface.has = iface_has; iface.admit = iface_admit; iface.read = iface_read;
  iface.select = iface_select; iface.pickup_ammo = iface_pickup_ammo; iface.pickup_weapons = iface_pickup_weapons;
  iface.pending_weapon = iface_pending; iface.handoff = iface_handoff; iface.step = iface_step;
  iface.remove = iface_remove; iface.ui = iface_ui; iface.view = iface_view;
  return iface;
}
