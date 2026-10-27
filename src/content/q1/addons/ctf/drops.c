/* Port of src/content/q1/addons/ctf/drops.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/drops.h"

#include <stdio.h>
#include <string.h>

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
static qa_q1ad_ctf_state *state_of(qa_q1ad_context *context) { return context->slice; }
static double inventory_count(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_item_id item) {
  return state->context->game.inventory_count != NULL
      ? state->context->game.inventory_count(state->context->game.ctx, actor, item) : 0;
}
static void launch(qa_q1ad_ctf_state *state, qa_actor_id actor, qa_q1ad_actor *item) {
  qa_q1ad_context *context = state->context;
  item->owner = actor;
  item->has_owner = true;
  item->solid = "trigger";
  item->movement = "bounce";
  item->movement_flags |= 256;
  qa_q1ad_ctf_input input = state->services.input != NULL
      ? state->services.input(state->services.ctx, actor) : (qa_q1ad_ctf_input){0};
  qa_angle_vectors basis = qa_math_angle_vectors_rule(input.view_angles, QA_ANGLE_RULE_Q1);
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  qa_vec3 direction = context->game.aim != NULL
      ? context->game.aim(context->game.ctx, owner, basis.forward) : basis.forward;
  qa_q1ad_body_patch patch = {
      .has_origin = true, .origin = qa_add3(qa_q1ad_ctf_body(state, actor).origin, (qa_vec3){0, 0, 16}),
      .has_velocity = true, .velocity = qa_scale3(direction, 500),
      .has_bounds = true, .bounds = {{-16, -16, 0}, {16, 16, 56}}};
  qa_q1ad_set_body(context, item, &patch);
  qa_q1ad_link(context, item);
  qa_q1ad_schedule(context, item, 120, "SUB_Remove");
}
void qa_q1ad_ctf_toss_ammo(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  const char *selected = state->services.selected_weapon != NULL
      ? state->services.selected_weapon(state->services.ctx, actor) : NULL;
  const char *ammo = state->services.selected_ammo != NULL
      ? state->services.selected_ammo(state->services.ctx, actor) : NULL;
  if (ammo == NULL || inventory_count(state, actor, ammo) <= 0) return;
  if (context->game.create == NULL) return;
  qa_q1ad_actor *item = context->game.create(context->game.ctx, "ctf_backpack");
  if (item == NULL) return;
  item->model = "progs/backpack.mdl";
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  for (size_t i = 0; i < sizeof AMMO_TYPES / sizeof AMMO_TYPES[0]; i++) {
    const ammo_type *type = &AMMO_TYPES[i];
    bool selected_matches = false, any_owned = false;
    for (size_t j = 0; j < type->weapon_count; j++) {
      if (selected != NULL && strcmp(selected, type->weapons[j]) == 0) selected_matches = true;
      if (inventory_count(state, actor, type->weapons[j]) != 0) any_owned = true;
    }
    if (!selected_matches && any_owned) continue;
    double held = inventory_count(state, actor, type->item);
    double count = held < type->limit ? held : type->limit;
    if (context->game.inventory_consume != NULL)
      (void)context->game.inventory_consume(context->game.ctx, owner, type->item, count);
    qa_q1ad_set_number(context, item, type->item, count);
  }
  qa_q1ad_bind(item->touch, "ctf:backpack_touch");
  launch(state, actor, item);
  if (state->services.weapon_changed != NULL) state->services.weapon_changed(state->services.ctx, actor, NULL);
}
void qa_q1ad_ctf_toss_weapon(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  if (context->game.deathmatch != 1) return;
  const char *selected = state->services.selected_weapon != NULL
      ? state->services.selected_weapon(state->services.ctx, actor) : NULL;
  const weapon_def *definition = NULL;
  for (size_t i = 0; i < sizeof WEAPONS / sizeof WEAPONS[0]; i++)
    if (selected != NULL && strcmp(WEAPONS[i].item, selected) == 0) definition = &WEAPONS[i];
  if (definition == NULL || context->game.create == NULL) return;
  qa_q1ad_actor *item = context->game.create(context->game.ctx, definition->classname);
  if (item == NULL) return;
  char model[64];
  (void)snprintf(model, sizeof model, "progs/%s.mdl", definition->model);
  qa_q1ad_actor_set(item, "drop.model", model);
  item->model = qa_q1ad_actor_text(item, "drop.model");
  qa_q1ad_actor_set(item, "ctf.weapon", definition->item);
  item->message = definition->name;
  qa_q1ad_bind(item->touch, "ctf:weapon_touch");
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  if (context->game.inventory_consume != NULL)
    (void)context->game.inventory_consume(context->game.ctx, owner, definition->item, 1);
  launch(state, actor, item);
  if (state->services.weapon_changed != NULL) state->services.weapon_changed(state->services.ctx, actor, NULL);
}
static void backpack_touch(qa_q1ad_context *context, qa_q1ad_actor *item,
                           const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  qa_q1ad_ctf_state *state = state_of(context);
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0 ||
      (state->services.observer != NULL && state->services.observer(state->services.ctx, *other))) return;
  qa_actor_id actor = *other;
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  for (size_t i = 0; i < sizeof AMMO_TYPES / sizeof AMMO_TYPES[0]; i++)
    if (context->game.inventory_give != NULL)
      context->game.inventory_give(context->game.ctx, owner, AMMO_TYPES[i].item,
                                   qa_q1ad_actor_number(item, AMMO_TYPES[i].item));
  qa_q1ad_sound_actor(context, owner.id, "weapons/lock4.wav", "item");
  qa_q1ad_effect(context, "pickup", qa_q1ad_ctf_body(state, actor).origin, &actor, 0);
  if (state->services.weapon_changed != NULL) state->services.weapon_changed(state->services.ctx, actor, NULL);
  qa_q1ad_remove(context, item);
}
static void weapon_touch(qa_q1ad_context *context, qa_q1ad_actor *item,
                         const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  qa_q1ad_ctf_state *state = state_of(context);
  if (!qa_q1ad_is_player(context, other) ||
      (state->services.observer != NULL && state->services.observer(state->services.ctx, *other))) return;
  qa_actor_id actor = *other;
  if (item->has_owner && qa_same_actor(item->owner, actor) && item->next_think - context->game.time > 119) return;
  const weapon_def *definition = NULL;
  for (size_t i = 0; i < sizeof WEAPONS / sizeof WEAPONS[0]; i++)
    if (strcmp(WEAPONS[i].item, qa_q1ad_actor_text(item, "ctf.weapon")) == 0) definition = &WEAPONS[i];
  if (definition == NULL) qa_range_error("CTF dropped weapon lost source kind");
  qa_q1ad_ctf_grant(state, actor, definition->item, 1, 1);
  qa_q1ad_msg_arg args[] = {{.text = item->message != NULL ? item->message : ""}};
  qa_q1ad_message(context, actor, "$qc_got_item", false, args, 1);
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  qa_q1ad_sound_actor(context, owner.id, "weapons/pkup.wav", "item");
  qa_q1ad_effect(context, "pickup", qa_q1ad_ctf_body(state, actor).origin, &actor, 0);
  if (state->services.weapon_changed != NULL)
    state->services.weapon_changed(state->services.ctx, actor, definition->item);
  qa_q1ad_use_targets(context, item, &actor);
  qa_q1ad_remove(context, item);
}
void qa_q1ad_ctf_register_drops(qa_q1ad_ctf_state *state) {
  qa_q1ad_register_named(state->context, "ctf:backpack_touch", (qa_q1ad_handlers){.touch = backpack_touch});
  qa_q1ad_register_named(state->context, "ctf:weapon_touch", (qa_q1ad_handlers){.touch = weapon_touch});
}
