/* Port of src/app/bootstrap/simulation/arsenal/q1.ts over the real
 * Q1EntityServices (qa_q1_services). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q1.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal/weapon_status.h"
#include "qa/content/composition/q1/commands.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"

/* TS primaryWeaponHandoff(actor)'s closure state (one record per actor, kept until destroy). */
typedef struct q1_handoff { qa_q1_services *game; qa_owned_actor actor; } q1_handoff;
struct qa_app_q1_selected_arsenal {
  qa_app_q1_selected_arsenal_options options;
  qa_q1_weapon *weapons; size_t weapon_count;
  qa_item_id *replaced; size_t replaced_count;
  q1_handoff **handoffs; size_t handoff_count;
};

static noreturn void q1_fail(const char *format, ...) {
  char message[512];
  va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static bool item_eq(qa_item_id a, qa_item_id b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool weapon_in(const qa_app_q1_selected_arsenal *arsenal, qa_q1_weapon weapon) {
  for (size_t i = 0; i < arsenal->weapon_count; i++) if (weapon != NULL && strcmp(arsenal->weapons[i], weapon) == 0) return true;
  return false;
}
static qa_shared_inventory_table *inventory_of(qa_app_q1_selected_arsenal *arsenal) { return arsenal->options.game->host.inventory; }
static qa_q1_player_state *require_player(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = qa_q1_player(arsenal->options.game, actor);
  if (player == NULL) q1_fail("Actor has no selected Q1 arsenal");
  return player;
}
/* this.weapons.some(weapon => weaponItem(weapon) === item || weaponAmmo(weapon) === item) */
static bool owns_entry(qa_app_q1_selected_arsenal *arsenal, qa_item_id item) {
  qa_q1_services *game = arsenal->options.game;
  for (size_t i = 0; i < arsenal->weapon_count; i++)
    if (item_eq(qa_q1_services_weapon_item(game, arsenal->weapons[i]), item) || item_eq(qa_q1_services_weapon_ammo(game, arsenal->weapons[i]), item)) return true;
  return false;
}

qa_app_q1_selected_arsenal *qa_app_q1_selected_arsenal_create(qa_app_q1_selected_arsenal_options options) {
  qa_app_q1_selected_arsenal *arsenal = calloc(1, sizeof *arsenal);
  if (arsenal == NULL) return NULL;
  arsenal->options = options;
  qa_q1_services *game = options.game;
  /* [...new Set([...WEAPONS, ...game.registeredWeapons.keys()])] */
  arsenal->weapons = calloc(8 + game->registered_weapon_count, sizeof *arsenal->weapons);
  arsenal->replaced = calloc(options.replaced_item_count + 1, sizeof *arsenal->replaced);
  if (arsenal->weapons == NULL || arsenal->replaced == NULL) { qa_app_q1_selected_arsenal_destroy(arsenal); return NULL; }
  for (size_t i = 0; i < 8 + game->registered_weapon_count; i++) {
    qa_q1_weapon weapon = i < 8 ? QA_Q1_WEAPONS[i] : game->registered_weapons[i - 8].def->id;
    if (!weapon_in(arsenal, weapon)) arsenal->weapons[arsenal->weapon_count++] = weapon;
  }
  /* The replaced item list is copied (TS keeps the readonly array). */
  for (size_t i = 0; i < options.replaced_item_count; i++) arsenal->replaced[i] = options.replaced_items[i];
  arsenal->replaced_count = options.replaced_item_count;
  arsenal->options.replaced_items = arsenal->replaced;
  return arsenal;
}
void qa_app_q1_selected_arsenal_destroy(qa_app_q1_selected_arsenal *arsenal) {
  if (arsenal == NULL) return;
  for (size_t i = 0; i < arsenal->handoff_count; i++) free(arsenal->handoffs[i]);
  free(arsenal->handoffs); free(arsenal->weapons); free(arsenal->replaced); free(arsenal);
}
qa_q1_services *qa_app_q1_selected_arsenal_game(const qa_app_q1_selected_arsenal *arsenal) { return arsenal->options.game; }
const qa_q1_weapon *qa_app_q1_selected_arsenal_weapons(const qa_app_q1_selected_arsenal *arsenal, size_t *out_count) {
  *out_count = arsenal->weapon_count; return arsenal->weapons;
}
bool qa_app_q1_selected_arsenal_has(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  return qa_q1_player(arsenal->options.game, actor) != NULL;
}

/* read(actor): the ammo array is malloc'd (caller frees). */
qa_arsenal_state qa_app_q1_selected_arsenal_read(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_q1_services *game = arsenal->options.game;
  if (!weapon_in(arsenal, player->weapon)) q1_fail("Selected Q1 player has a weapon from another product");
  qa_arsenal_state state; memset(&state, 0, sizeof state);
  state.provider = qa_q1_services_provider(game);
  state.active_weapon = qa_q1_services_weapon_item(game, player->weapon);
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(inventory_of(arsenal), actor, &entry_count);
  qa_inventory_entry *ammo = malloc((entry_count ? entry_count : 1) * sizeof *ammo);
  if (ammo == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < entry_count; i++) if (owns_entry(arsenal, entries[i].item)) ammo[count++] = entries[i];
  state.ammo = ammo; state.ammo_count = count;
  state.state.kind = "q1"; state.state.frame = player->weapon_frame;
  state.state.attack_finished_seconds = player->attack_finished;
  state.state.source_weapon = qa_q1_is_base_weapon(player->weapon) ? qa_q1_weapon_bit(player->weapon) : 0;
  return state;
}

qa_arsenal_state qa_app_q1_selected_arsenal_admit(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor, double max_health) {
  qa_q1_services *game = arsenal->options.game;
  qa_shared_inventory_table *inventory = inventory_of(arsenal);
  if (qa_q1_player(game, actor.id) != NULL) q1_fail("Selected Q1 arsenal already admitted");
  size_t entry_count = 0;
  const qa_inventory_entry *entries = qa_shared_inventory_table_entries(inventory, actor.id, &entry_count);
  /* configure may rebuild the entry list: iterate a copy (TS iterates the returned array). */
  qa_inventory_entry *snapshot = malloc((entry_count ? entry_count : 1) * sizeof *snapshot);
  if (snapshot == NULL) qa_range_error("Out of memory");
  memcpy(snapshot, entries, entry_count * sizeof *snapshot);
  for (size_t i = 0; i < entry_count; i++)
    for (size_t j = 0; j < arsenal->replaced_count; j++)
      if (item_eq(snapshot[i].item, arsenal->replaced[j])) {
        qa_inventory_entry cleared = snapshot[i]; cleared.count = 0;
        qa_shared_inventory_table_configure(inventory, actor, cleared, NULL, NULL);
        break;
      }
  free(snapshot);
  qa_app_q1_native_player native; memset(&native, 0, sizeof native);
  if (arsenal->options.native_player != NULL) native = arsenal->options.native_player(arsenal->options.ctx, actor.id);
  if (!native.has_value) qa_q1_initialize_weapon_inventory(game, actor);
  for (size_t i = 0; i < game->registered_weapon_count; i++) {
    qa_item_id item = qa_q1_services_weapon_item(game, game->registered_weapons[i].def->id);
    qa_inventory_entry entry; memset(&entry, 0, sizeof entry);
    entry.item = item; entry.count = qa_shared_inventory_table_count(inventory, actor.id, item); entry.capacity = 1;
    qa_shared_inventory_table_configure(inventory, actor, entry, NULL, NULL);
  }
  double health = native.has_value ? native.max_health : max_health;
  qa_q1_player_state *player = qa_q1_attach_player(game, actor, native.has_value ? native.weapon : NULL, false, &health);
  if (native.has_value && native.has_auto_switch) player->auto_switch = native.auto_switch;
  return qa_app_q1_selected_arsenal_read(arsenal, actor.id);
}

void qa_app_q1_selected_arsenal_travel_destroy(qa_app_q1_selected_arsenal_travel *travel) {
  if (travel == NULL) return;
  free((void *)travel->inventory);
  for (size_t i = 0; i < travel->extension_count; i++) free(travel->extensions[i].bytes);
  free((void *)travel->extensions);
  memset(travel, 0, sizeof *travel);
}

qa_app_q1_selected_arsenal_travel qa_app_q1_selected_arsenal_capture_travel(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_q1_services *game = arsenal->options.game;
  qa_app_q1_selected_arsenal_travel travel; memset(&travel, 0, sizeof travel);
  qa_app_q1_travel_extension *extensions = calloc(game->player_extension_count + 1, sizeof *extensions);
  if (extensions == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < game->player_extension_count; i++) {
    const qa_q1_player_extension *extension = game->player_extensions[i];
    uint8_t *bytes = NULL; size_t byte_count = 0;
    if (extension->capture_travel == NULL || !extension->capture_travel(extension->ctx, game, player, &bytes, &byte_count)) continue;
    extensions[count].id = extension->id;
    extensions[count].bytes = malloc(byte_count ? byte_count : 1); /* bytes.slice() */
    if (extensions[count].bytes == NULL) qa_range_error("Out of memory");
    memcpy(extensions[count].bytes, bytes, byte_count);
    extensions[count].byte_count = byte_count;
    count++;
  }
  travel.weapon = player->weapon;
  qa_arsenal_state state = qa_app_q1_selected_arsenal_read(arsenal, actor);
  travel.inventory = state.ammo; travel.inventory_count = state.ammo_count;
  travel.extensions = extensions; travel.extension_count = count;
  return travel;
}

qa_arsenal_state qa_app_q1_selected_arsenal_admit_travel(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    double max_health, qa_app_q1_selected_arsenal_travel travel) {
  qa_q1_services *game = arsenal->options.game;
  qa_shared_inventory_table *inventory = inventory_of(arsenal);
  if (qa_q1_player(game, actor.id) != NULL) q1_fail("Selected Q1 arsenal already admitted");
  bool owned = false;
  for (size_t i = 0; i < travel.inventory_count && !owned; i++)
    owned = item_eq(travel.inventory[i].item, qa_q1_services_weapon_item(game, travel.weapon)) && travel.inventory[i].count > 0;
  if (!weapon_in(arsenal, travel.weapon) || !owned) q1_fail("Selected Q1 travel weapon is not owned by this product");
  for (size_t i = 0; i < travel.inventory_count; i++)
    if (!owns_entry(arsenal, travel.inventory[i].item)) q1_fail("Selected Q1 travel contains another component's inventory");
  const qa_q1_player_extension **restores = calloc(travel.extension_count + 1, sizeof *restores);
  if (restores == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < travel.extension_count; i++) {
    const qa_q1_player_extension *extension = NULL;
    for (size_t j = 0; j < game->player_extension_count && extension == NULL; j++)
      if (strcmp(game->player_extensions[j]->id, travel.extensions[i].id) == 0) extension = game->player_extensions[j];
    bool restored = false;
    for (size_t j = 0; j < i && !restored; j++) restored = strcmp(travel.extensions[j].id, travel.extensions[i].id) == 0;
    if (extension == NULL || extension->restore_travel == NULL || restored) {
      free(restores); q1_fail("Invalid selected Q1 travel extension: %s", travel.extensions[i].id);
    }
    restores[i] = extension;
  }
  qa_q1_player_state *player = qa_q1_attach_player(game, actor, travel.weapon, false, &max_health);
  for (size_t i = 0; i < travel.inventory_count; i++) {
    qa_inventory_entry entry = travel.inventory[i];
    double capacity;
    if (qa_q1_inventory_capacity(game, actor.id, entry.item, &capacity)) entry.capacity = capacity;
    qa_shared_inventory_table_configure(inventory, actor, entry, NULL, NULL);
  }
  for (size_t i = 0; i < travel.extension_count; i++)
    restores[i]->restore_travel(restores[i]->ctx, game, player, travel.extensions[i].bytes, travel.extensions[i].byte_count);
  free(restores);
  return qa_app_q1_selected_arsenal_read(arsenal, actor.id);
}

bool qa_app_q1_selected_arsenal_select(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, qa_item_id item) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_q1_services *game = arsenal->options.game;
  for (size_t i = 0; i < arsenal->weapon_count; i++)
    if (item_eq(qa_q1_services_weapon_item(game, arsenal->weapons[i]), item)) return qa_q1_select_weapon(game, player->actor, arsenal->weapons[i]);
  return false;
}
qa_item_id qa_app_q1_selected_arsenal_pending_weapon(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  require_player(arsenal, actor); return NULL;
}
bool qa_app_q1_selected_arsenal_impulse(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, double value) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_q1_services *game = arsenal->options.game;
  if (value == 0 || game->time < player->attack_finished) return false;
  if (arsenal->options.impulse != NULL) return arsenal->options.impulse(arsenal->options.ctx, player, value);
  return qa_comp_q1_weapon_impulse(game, player, value);
}

/* ---- Q1EntityServices.primaryWeaponHandoff (content/q1/foundation/entity-services.ts 737-764) ---- */
static qa_q1_player_state *handoff_player(q1_handoff *handoff) {
  qa_q1_player_state *player = qa_q1_player(handoff->game, handoff->actor.id);
  if (player == NULL) q1_fail("Player has no Q1 weapon state");
  return player;
}
static bool handoff_resolve(q1_handoff *handoff, qa_item_id item, qa_q1_weapon *out) {
  qa_q1_services *game = handoff->game;
  for (size_t i = 0; i < 8 + game->registered_weapon_count; i++) {
    qa_q1_weapon candidate = i < 8 ? QA_Q1_WEAPONS[i] : game->registered_weapons[i - 8].def->id;
    if (!item_eq(qa_q1_services_weapon_item(game, candidate), item)) continue;
    if (!qa_q1_weapon_available(game, handoff_player(handoff), candidate, "best")) return false;
    *out = candidate;
    return true;
  }
  return false;
}
static bool handoff_accepts(void *ctx, qa_item_id item) { qa_q1_weapon weapon; return handoff_resolve(ctx, item, &weapon); }
static bool handoff_select(void *ctx, qa_item_id item) {
  q1_handoff *handoff = ctx; qa_q1_weapon weapon;
  return handoff_resolve(handoff, item, &weapon) && qa_q1_select_weapon(handoff->game, handoff->actor, weapon);
}
static void handoff_holster(void *ctx) {
  qa_q1_player_state *player = handoff_player(ctx);
  if (player->primary_holstered) return;
  player->primary_holstered = true;
  player->weapon_frame = 0; player->continuous_firing = false; player->weapon_animation_at = -1;
}
static bool handoff_is_holstered(void *ctx) { return handoff_player(ctx)->primary_holstered; }
static bool handoff_resume(void *ctx, qa_item_id item) {
  q1_handoff *handoff = ctx;
  qa_q1_player_state *player = handoff_player(handoff);
  qa_q1_weapon requested = NULL;
  if (item == NULL) requested = player->weapon;
  else if (!handoff_resolve(handoff, item, &requested)) requested = NULL;
  qa_q1_weapon weapon = requested != NULL && qa_q1_weapon_available(handoff->game, player, requested, "best")
      ? requested : qa_q1_choose_best(handoff->game, handoff->actor);
  qa_q1_select_weapon(handoff->game, handoff->actor, weapon);
  player->primary_holstered = false;
  return item == NULL || item_eq(qa_q1_services_weapon_item(handoff->game, player->weapon), item);
}
qa_app_primary_weapon_handoff qa_app_q1_selected_arsenal_handoff(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  q1_handoff *handoff = NULL;
  for (size_t i = 0; i < arsenal->handoff_count && handoff == NULL; i++)
    if (qa_same_owned_actor(arsenal->handoffs[i]->actor, player->actor)) handoff = arsenal->handoffs[i];
  if (handoff == NULL) {
    q1_handoff **grown = realloc(arsenal->handoffs, (arsenal->handoff_count + 1) * sizeof *grown);
    handoff = calloc(1, sizeof *handoff);
    if (grown == NULL || handoff == NULL) qa_range_error("Out of memory");
    arsenal->handoffs = grown; arsenal->handoffs[arsenal->handoff_count++] = handoff;
    handoff->game = arsenal->options.game; handoff->actor = player->actor;
  }
  handoff_player(handoff); /* "Player has no Q1 weapon state" */
  qa_app_primary_weapon_handoff result = {handoff, "immediate", qa_q1_services_provider(arsenal->options.game), handoff_accepts,
      handoff_select, handoff_holster, handoff_is_holstered, handoff_resume};
  return result;
}

typedef struct count_ctx { qa_app_q1_selected_arsenal *arsenal; qa_actor_id actor; const qa_pickup_ammo_receipt *grants; size_t grant_count; } count_ctx;
static double grant_count_fn(void *v, qa_item_id item) {
  count_ctx *ctx = v;
  for (size_t i = 0; i < ctx->grant_count; i++) if (item_eq(ctx->grants[i].item, item)) return ctx->grants[i].before;
  return qa_shared_inventory_table_count(inventory_of(ctx->arsenal), ctx->actor, item);
}
/* this.options.nativePlayer?.(actor).autoSwitch ?? player.autoSwitch */
static const char *auto_switch_of(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor, qa_q1_player_state *player) {
  if (arsenal->options.native_player != NULL) {
    qa_app_q1_native_player native = arsenal->options.native_player(arsenal->options.ctx, actor);
    if (native.has_value && native.has_auto_switch && native.auto_switch != NULL) return native.auto_switch;
  }
  return player->auto_switch;
}
void qa_app_q1_selected_arsenal_pickup_ammo(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_pickup_ammo_receipt *grants, size_t grant_count, bool auto_switch) {
  qa_q1_services *game = arsenal->options.game;
  qa_q1_player_state *player = require_player(arsenal, actor.id);
  if (arsenal->options.prepare_pickup != NULL) arsenal->options.prepare_pickup(arsenal->options.ctx, player);
  count_ctx ctx = {arsenal, actor.id, grants, grant_count};
  qa_q1_weapon before = qa_q1_choose_best_counted(game, actor, grant_count_fn, &ctx);
  bool switching = auto_switch && !item_eq(auto_switch_of(arsenal, actor.id, player), "never");
  qa_q1_ammo_pickup_selection(game, player, before, switching);
}
void qa_app_q1_selected_arsenal_pickup_weapons(qa_app_q1_selected_arsenal *arsenal, qa_owned_actor actor,
    const qa_item_id *weapons, size_t weapon_count, qa_pickup_selection selection) {
  qa_q1_services *game = arsenal->options.game;
  qa_q1_player_state *player = require_player(arsenal, actor.id);
  if (arsenal->options.prepare_pickup != NULL) arsenal->options.prepare_pickup(arsenal->options.ctx, player);
  for (size_t i = 0; i < weapon_count; i++) {
    qa_q1_weapon weapon = NULL;
    for (size_t j = 0; j < arsenal->weapon_count && weapon == NULL; j++)
      if (item_eq(qa_q1_services_weapon_item(game, arsenal->weapons[j]), weapons[i])) weapon = arsenal->weapons[j];
    if (weapon == NULL) continue;
    const char *mode = item_eq(auto_switch_of(arsenal, actor.id, player), "never") ? "never"
        : selection == QA_PICKUP_SELECTION_NEVER ? "never" : selection == QA_PICKUP_SELECTION_ALWAYS ? "always" : "better";
    qa_q1_weapon_pickup_selection(game, player, weapon, mode);
  }
}

static double command_buttons(const qa_user_command *command) {
  switch (command->kind) {
  case QA_USER_COMMAND_Q1: return command->as.q1.buttons;
  case QA_USER_COMMAND_QW: return command->as.qw.buttons;
  case QA_USER_COMMAND_Q2: return command->as.q2.buttons;
  case QA_USER_COMMAND_Q2_RERELEASE: return command->as.q2_rerelease.buttons;
  default: return command->as.q3.buttons;
  }
}
qa_weapon_step_result qa_app_q1_selected_arsenal_step(qa_app_q1_selected_arsenal *arsenal,
    qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  qa_q1_services *game = arsenal->options.game;
  if (intent != NULL && !item_eq(intent->provider, qa_q1_services_provider(game))) q1_fail("Arsenal intent belongs to a different provider");
  /* Peers choose this string; a weapon outside the product is no request. */
  if (intent != NULL && intent->weapon != NULL) {
    bool known = false;
    for (size_t i = 0; i < arsenal->weapon_count && !known; i++) known = item_eq(qa_q1_services_weapon_item(game, arsenal->weapons[i]), intent->weapon);
    if (known && !item_eq(qa_q1_services_weapon_item(game, require_player(arsenal, input.actor.id)->weapon), intent->weapon))
      qa_app_q1_selected_arsenal_select(arsenal, input.actor.id, intent->weapon);
  }
  qa_arsenal_state before = qa_app_q1_selected_arsenal_read(arsenal, input.actor.id);
  qa_vec3 view_angles = {0, 0, 0}; double water_level = 0;
  arsenal->options.observe(arsenal->options.ctx, input.actor.id, &view_angles, &water_level);
  double seconds = input.frame.time.kind == QA_SOURCE_TIME_SECONDS ? input.frame.time.value : input.frame.time.value / 1000;
  double buttons = command_buttons(&input.command);
  bool pressed = (qa_js_to_int32(buttons) & 1) != 0 && (input.command.kind != QA_USER_COMMAND_Q3 || (qa_js_to_int32(buttons) & QA_Q3_BUTTON_TALK) == 0);
  qa_q1_weapon weapon = require_player(arsenal, input.actor.id)->weapon;
  bool fired = qa_q1_weapon_input(game, input.actor, pressed, view_angles, seconds, water_level);
  bool live = qa_session_actor_registry_is_live(game->host.actors, input.actor.id);
  qa_weapon_step_result result; memset(&result, 0, sizeof result);
  result.animation = fired && live && arsenal->options.fired != NULL
      ? arsenal->options.fired(arsenal->options.ctx, input.actor.id, weapon, input.animation) : input.animation;
  if (live) { result.arsenal = qa_app_q1_selected_arsenal_read(arsenal, input.actor.id); free((void *)before.ammo); }
  else result.arsenal = before;
  return result;
}
void qa_app_q1_selected_arsenal_frame(qa_app_q1_selected_arsenal *arsenal, double seconds) {
  qa_q1_services *game = arsenal->options.game;
  /* players.values(): a weapon frame can release players, so iterate a snapshot of the actors. */
  size_t count = game->player_count;
  qa_owned_actor *actors = malloc((count ? count : 1) * sizeof *actors);
  if (actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) actors[i] = game->players[i]->actor;
  for (size_t i = 0; i < count; i++) qa_q1_weapon_frame(game, actors[i], seconds);
  free(actors);
}
/* this.options.game.players.delete(player.actor): the player record stays
 * allocated with the game's released players (as its onRelease does). */
void qa_app_q1_selected_arsenal_remove(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_services *game = arsenal->options.game;
  for (size_t i = 0; i < game->player_count; i++) {
    if (!qa_same_actor(game->players[i]->actor.id, actor)) continue;
    qa_q1_player_state *player = game->players[i];
    memmove(&game->players[i], &game->players[i + 1], (game->player_count - i - 1) * sizeof *game->players);
    game->player_count--;
    if (game->released_player_count == game->released_player_capacity) {
      size_t capacity = game->released_player_capacity ? game->released_player_capacity * 2 : 8;
      qa_q1_player_state **grown = realloc(game->released_players, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      game->released_players = grown; game->released_player_capacity = capacity;
    }
    game->released_players[game->released_player_count++] = player;
    return;
  }
}
static qa_item_id status_weapon_item(void *ctx, qa_q1_weapon weapon) { return qa_q1_services_weapon_item(ctx, weapon); }
static qa_item_id status_weapon_ammo(void *ctx, qa_q1_weapon weapon) { return qa_q1_services_weapon_ammo(ctx, weapon); }
static double status_count(void *ctx, qa_actor_id actor, qa_item_id item) {
  return qa_shared_inventory_table_count(((qa_q1_services *)ctx)->host.inventory, actor, item);
}
static bool status_available(void *ctx, qa_q1_player_state *player, qa_q1_weapon weapon, const char *purpose) {
  return qa_q1_weapon_available(ctx, player, weapon, purpose);
}
qa_app_arsenal_ui qa_app_q1_selected_arsenal_ui(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor,
    qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity, char (*labels)[64], size_t label_capacity) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_q1_services *game = arsenal->options.game;
  qa_shared_inventory_table *inventory = inventory_of(arsenal);
  qa_app_arsenal_ui ui; memset(&ui, 0, sizeof ui);
  qa_app_q1_status_host host = {game, status_weapon_item, status_weapon_ammo, status_count, status_available};
  char label[64] = {0};
  ui.weapon_status = qa_app_q1_weapon_status(host, player, source, label, sizeof label);
  ui.has_weapon_status = true;
  if (labels != NULL && label_capacity != 0) { memcpy(labels[0], label, strlen(label) + 1); ui.weapon_status.label = labels[0]; }
  ui.arsenal_warning = "none";
  ui.active_weapon = qa_q1_services_weapon_item(game, player->weapon);
  qa_item_id ammo = qa_q1_services_weapon_ammo(game, player->weapon);
  if (ammo != NULL) { ui.has_ammo = true; ui.ammo_item = ammo; ui.ammo_count = qa_shared_inventory_table_count(inventory, actor, ammo); }
  ui.items = items; ui.item_capacity = capacity;
  if (arsenal->weapon_count > capacity) q1_fail("Selected Q1 UI has more weapons than its storage holds");
  for (size_t i = 0; i < arsenal->weapon_count; i++) {
    qa_q1_weapon weapon = arsenal->weapons[i];
    qa_item_id item = qa_q1_services_weapon_item(game, weapon), weapon_ammo = qa_q1_services_weapon_ammo(game, weapon);
    qa_app_player_ui_item *entry = &items[ui.item_count];
    memset(entry, 0, sizeof *entry);
    entry->id = item; entry->kind = "weapon"; entry->source_ordinal = (double)(i + 1);
    if (labels != NULL && ui.item_count + 1 < label_capacity) {
      qa_app_q1_weapon_display_name(weapon, labels[ui.item_count + 1], 64);
      entry->label = labels[ui.item_count + 1];
    }
    entry->owned = qa_shared_inventory_table_count(inventory, actor, item) > 0;
    entry->has_ammo = qa_q1_weapon_available(game, player, weapon, "best");
    entry->has_count = weapon_ammo != NULL;
    entry->count = weapon_ammo != NULL ? qa_shared_inventory_table_count(inventory, actor, weapon_ammo) : 0;
    entry->warning_count = 0;
    ui.item_count++;
  }
  return ui;
}
qa_app_arsenal_view qa_app_q1_selected_arsenal_view(qa_app_q1_selected_arsenal *arsenal, qa_actor_id actor) {
  qa_q1_player_state *player = require_player(arsenal, actor);
  qa_app_arsenal_view view; memset(&view, 0, sizeof view);
  view.has_value = true;
  view.path = qa_q1_services_weapon_model(arsenal->options.game, player->weapon, player);
  view.frame = player->weapon_frame;
  return view;
}
size_t qa_app_q1_selected_arsenal_catalog(qa_app_q1_selected_arsenal *arsenal, qa_selected_pickup_weapon *out, size_t capacity) {
  if (arsenal->weapon_count > capacity) q1_fail("Selected Q1 catalog exceeds its storage");
  for (size_t i = 0; i < arsenal->weapon_count; i++) {
    qa_q1_weapon weapon = arsenal->weapons[i];
    out[i].item = qa_q1_services_weapon_item(arsenal->options.game, weapon);
    out[i].ammo = qa_q1_services_weapon_ammo(arsenal->options.game, weapon);
    out[i].drop = strcmp(weapon, "rogue:grapple") == 0 || strcmp(weapon, "ctf:grapple") == 0 ? "none" : "supply";
  }
  return arsenal->weapon_count;
}

static size_t iface_catalog(void *v, qa_selected_pickup_weapon *out, size_t capacity) { return qa_app_q1_selected_arsenal_catalog(v, out, capacity); }
static bool iface_has(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_has(v, actor); }
static qa_arsenal_state iface_admit(void *v, qa_owned_actor actor, double max_health, bool team_deathmatch) {
  (void)team_deathmatch; /* the Q1 admit takes no team argument */
  return qa_app_q1_selected_arsenal_admit(v, actor, max_health);
}
static qa_arsenal_state iface_read(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_read(v, actor); }
static bool iface_select(void *v, qa_actor_id actor, qa_item_id item) { return qa_app_q1_selected_arsenal_select(v, actor, item); }
static void iface_pickup_ammo(void *v, qa_owned_actor actor, const qa_pickup_ammo_receipt *grants, size_t count, bool auto_switch) {
  qa_app_q1_selected_arsenal_pickup_ammo(v, actor, grants, count, auto_switch);
}
static void iface_pickup_weapons(void *v, qa_owned_actor actor, const qa_item_id *weapons, size_t count, qa_pickup_selection selection) {
  qa_app_q1_selected_arsenal_pickup_weapons(v, actor, weapons, count, selection);
}
static qa_item_id iface_pending(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_pending_weapon(v, actor); }
static qa_app_primary_weapon_handoff iface_handoff(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_handoff(v, actor); }
static qa_weapon_step_result iface_step(void *v, qa_weapon_step_input input, const qa_arsenal_intent *intent) {
  return qa_app_q1_selected_arsenal_step(v, input, intent);
}
static void iface_remove(void *v, qa_actor_id actor) { qa_app_q1_selected_arsenal_remove(v, actor); }
static qa_app_arsenal_ui iface_ui(void *v, qa_actor_id actor, qa_provider_reference source, qa_app_player_ui_item *items, size_t capacity) {
  return qa_app_q1_selected_arsenal_ui(v, actor, source, items, capacity, NULL, 0);
}
static qa_app_arsenal_view iface_view(void *v, qa_actor_id actor) { return qa_app_q1_selected_arsenal_view(v, actor); }
qa_app_selected_arsenal qa_app_q1_selected_arsenal_interface(qa_app_q1_selected_arsenal *arsenal) {
  qa_app_selected_arsenal iface; memset(&iface, 0, sizeof iface);
  iface.ctx = arsenal; iface.family = "q1"; iface.provider = qa_q1_services_provider(arsenal->options.game);
  iface.catalog = iface_catalog; iface.has = iface_has; iface.admit = iface_admit; iface.read = iface_read;
  iface.select = iface_select; iface.pickup_ammo = iface_pickup_ammo; iface.pickup_weapons = iface_pickup_weapons;
  iface.pending_weapon = iface_pending; iface.handoff = iface_handoff; iface.step = iface_step;
  iface.remove = iface_remove; iface.ui = iface_ui; iface.view = iface_view;
  return iface;
}
