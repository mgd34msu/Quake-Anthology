/* Port of src/content/q1/addons/ctf/drops.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/drops.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/foundation/pickups.h"
#include "qa/content/q1/foundation/weapons.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

typedef struct {
  qa_item_id item;
  double limit;
  const char *weapons[2];
  size_t weapon_count;
} ammo_type;
static const ammo_type AMMO_TYPES[] = {
  {"q1:ammo/shells", 20, {"q1:weapon/shotgun", "q1:weapon/supershotgun"}, 2},
  {"q1:ammo/nails", 20, {"q1:weapon/nailgun", "q1:weapon/supernailgun"}, 2},
  {"q1:ammo/rockets", 10, {"q1:weapon/grenadelauncher", "q1:weapon/rocketlauncher"}, 2},
  {"q1:ammo/cells", 20, {"q1:weapon/lightning", NULL}, 1},
};
typedef struct {
  qa_item_id item;
  const char *classname, *model, *name;
} weapon_def;
static const weapon_def WEAPONS[] = {
  {"q1:weapon/supershotgun", "weapon_supershotgun", "g_shot", "Double-barrelled Shotgun"},
  {"q1:weapon/nailgun", "weapon_nailgun", "g_nail", "nailgun"},
  {"q1:weapon/supernailgun", "weapon_supernailgun", "g_nail2", "Super Nailgun"},
  {"q1:weapon/grenadelauncher", "weapon_grenadelauncher", "g_rock", "Grenade Launcher"},
  {"q1:weapon/rocketlauncher", "weapon_rocketlauncher", "g_rock2", "Rocket Launcher"},
  {"q1:weapon/lightning", "weapon_lightning", "g_light", "Thunderbolt"},
};
static const weapon_def *find_weapon(const char *item) {
  for (size_t i = 0; i < sizeof WEAPONS / sizeof WEAPONS[0]; i++)
    if (item != NULL && strcmp(WEAPONS[i].item, item) == 0) return &WEAPONS[i];
  return NULL;
}

static void launch(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1_actor *item) {
  qa_q1_services *game = state->context->game;
  item->has_owner = true;
  item->owner = actor;
  item->solid = "trigger";
  item->movement = "bounce";
  item->movement_flags = qa_js_bit_or(item->movement_flags, 256);
  /* Object literal order: origin (body) before velocity (owner, input, aim). */
  qa_vec3 origin = qa_q1_vadd(qa_q1ad_ctf_body(state, actor).origin, (qa_vec3){0, 0, 16});
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  qa_vec3 forward = qa_q1_vectors(state->services.input(state->services.ctx, actor).view_angles).forward;
  qa_vec3 velocity = qa_q1_vscale(qa_q1_aim(game, owner, forward), 500);
  qa_q1_body_patch patch = {.has_origin = true, .origin = origin, .has_velocity = true, .velocity = velocity,
                            .has_bounds = true, .bounds = {{-16, -16, 0}, {16, 16, 56}}};
  qa_q1_set_body(game, item, &patch);
  qa_q1_link(game, item);
  qa_q1_schedule(game, item, 120, qa_q1_callback_registry_action(game->named, item, "SUB_Remove"));
}
void qa_q1ad_ctf_toss_ammo(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  qa_shared_inventory_table *inventory = game->host.inventory;
  qa_item_id selected = state->services.selected_weapon(state->services.ctx, actor);
  qa_item_id ammo = state->services.selected_ammo(state->services.ctx, actor);
  if (ammo == NULL || qa_shared_inventory_table_count(inventory, actor, ammo) <= 0) return;
  qa_q1_actor *item = qa_q1_create(game, "ctf_backpack", NULL, NULL);
  qa_q1ad_set_text(&item->model, "progs/backpack.mdl");
  for (size_t i = 0; i < sizeof AMMO_TYPES / sizeof AMMO_TYPES[0]; i++) {
    const ammo_type *type = &AMMO_TYPES[i];
    bool selected_matches = false, any_owned = false;
    for (size_t j = 0; j < type->weapon_count; j++)
      if (selected != NULL && strcmp(selected, type->weapons[j]) == 0) selected_matches = true;
    if (!selected_matches)
      for (size_t j = 0; j < type->weapon_count && !any_owned; j++)
        if (qa_shared_inventory_table_count(inventory, actor, type->weapons[j]) != 0) any_owned = true;
    if (!selected_matches && any_owned) continue;
    double count = qa_js_min(type->limit, qa_shared_inventory_table_count(inventory, actor, type->item));
    (void)qa_shared_inventory_table_consume(inventory, qa_q1ad_ctf_owner(state, actor), type->item, count);
    qa_q1ad_set_number(state->context, item, type->item, count);
  }
  item->touch = qa_q1_callback_registry_touch(game->named, item, "ctf:backpack_touch");
  launch(state, actor, item);
  state->services.weapon_changed(state->services.ctx, actor, NULL);
}
void qa_q1ad_ctf_toss_weapon(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  if (game->options.deathmatch != 1) return;
  const weapon_def *definition = find_weapon(state->services.selected_weapon(state->services.ctx, actor));
  if (definition == NULL) return;
  qa_q1_actor *item = qa_q1_create(game, definition->classname, NULL, NULL);
  char model[64];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", definition->model);
  qa_q1ad_set_text(&item->model, model);
  (void)qa_q1_actor_field_set(item, "ctf.weapon", definition->item);
  qa_q1ad_set_text(&item->message, definition->name);
  item->touch = qa_q1_callback_registry_touch(game->named, item, "ctf:weapon_touch");
  /* A failed consume means the weapon is not held, so nothing is thrown. */
  if (!qa_shared_inventory_table_consume(game->host.inventory, qa_q1ad_ctf_owner(state, actor), definition->item, 1)) {
    qa_q1_remove(game, item);
    return;
  }
  launch(state, actor, item);
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  (void)qa_q1_select_weapon(game, owner, qa_q1_choose_best(game, owner));
  state->services.weapon_changed(state->services.ctx, actor, NULL);
}
static void backpack_touch(void *ctx, qa_q1_services *game, qa_q1_actor *item, qa_actor_id actor, const qa_vec3 *normal,
                           const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS touch reads (item, actor) */
  qa_q1ad_ctf_state *state = ctx;
  if (!qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0 || state->services.observer(state->services.ctx, actor))
    return;
  /* Items.qc BackpackTouch: the thrower cannot take it back for the first two seconds. */
  if (item->has_owner && qa_same_actor(item->owner, actor) && item->next_think - game->time > 118) return;
  qa_q1_message(game, &actor, "$qc_backpack_got", false, NULL, 0);
  bool listed = false;
  for (size_t i = 0; i < sizeof AMMO_TYPES / sizeof AMMO_TYPES[0]; i++) {
    double count = qa_q1_actor_number(item, AMMO_TYPES[i].item);
    if (count == 0) continue;
    if (listed) qa_q1_message(game, &actor, ", ", false, NULL, 0);
    listed = true;
    char key[48];
    (void)snprintf(key, sizeof key, "$qc_backpack_%s", AMMO_TYPES[i].item + strlen("q1:ammo/"));
    qa_q1_msg_arg args[1] = {{.is_number = true, .number = count}};
    qa_q1_message(game, &actor, key, false, args, 1);
  }
  qa_q1_message(game, &actor, "\n", false, NULL, 0);
  for (size_t i = 0; i < sizeof AMMO_TYPES / sizeof AMMO_TYPES[0]; i++) {
    qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
    (void)qa_shared_inventory_table_give(game->host.inventory, owner, AMMO_TYPES[i].item,
                                         qa_q1_actor_number(item, AMMO_TYPES[i].item));
  }
  qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "weapons/lock4.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1ad_ctf_body(state, actor).origin, &actor, 1);
  state->services.weapon_changed(state->services.ctx, actor, NULL);
  qa_q1_remove(game, item);
}
static void weapon_touch(void *ctx, qa_q1_services *game, qa_q1_actor *item, qa_actor_id actor, const qa_vec3 *normal,
                         const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS touch reads (item, actor) */
  qa_q1ad_ctf_state *state = ctx;
  if (!qa_q1_is_player(game, &actor) || state->services.observer(state->services.ctx, actor)) return;
  if (item->has_owner && qa_same_actor(item->owner, actor) && item->next_think - game->time > 119) return;
  const weapon_def *definition = find_weapon(qa_q1_actor_text(item, "ctf.weapon"));
  if (definition == NULL) qa_range_error("CTF dropped weapon lost source kind");
  qa_q1ad_ctf_grant(state, actor, definition->item, 1, 1);
  qa_q1_msg_arg args[1] = {{.text = item->message}};
  qa_q1_message(game, &actor, "$qc_got_item", false, args, 1);
  qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "weapons/pkup.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1ad_ctf_body(state, actor).origin, &actor, 1);
  /* Deathmatch_Weapon: switch to the acquired weapon when it ranks above the current one. */
  qa_q1_base_weapon weapon = NULL;
  for (int i = 0; i < 8 && weapon == NULL; i++)
    if (strcmp(qa_q1_services_weapon_item(game, QA_Q1_WEAPONS[i]), definition->item) == 0) weapon = QA_Q1_WEAPONS[i];
  qa_q1_player_state *player = qa_q1_player(game, actor);
  if (weapon != NULL && player != NULL) qa_q1_weapon_pickup_selection(game, player, weapon, "better");
  state->services.weapon_changed(state->services.ctx, actor, definition->item);
  qa_q1_use_targets(game, item, &actor);
  qa_q1_remove(game, item);
}
void qa_q1ad_ctf_register_drops(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  qa_q1_callback_registry_register(game->named, "ctf:backpack_touch",
                                   (qa_q1_callback_handlers){.ctx = state, .touch = backpack_touch});
  qa_q1_callback_registry_register(game->named, "ctf:weapon_touch",
                                   (qa_q1_callback_handlers){.ctx = state, .touch = weapon_touch});
}
