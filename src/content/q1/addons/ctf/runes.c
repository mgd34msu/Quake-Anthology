/* Port of src/content/q1/addons/ctf/runes.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/runes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

bool qa_q1ad_ctf_haste_interval(qa_item_id item, double *out) {
  static const struct { const char *item; double interval; } TABLE[] = {
    {"q1:weapon/axe", 0.3}, {"q1:weapon/shotgun", 0.3}, {"q1:weapon/supershotgun", 0.4},
    {"q1:weapon/grenadelauncher", 0.3}, {"q1:weapon/rocketlauncher", 0.4}};
  for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
    if (strcmp(item, TABLE[i].item) == 0) { *out = TABLE[i].interval; return true; }
  return false;
}
static int rune_ordinal(const char *rune) {
  for (int i = 0; i < 4; i++) if (strcmp(QA_Q1AD_CTF_RUNES[i], rune) == 0) return i;
  return -1;
}
static qa_q1_actor *next_rune_spawn(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  size_t spot_count = 0;
  qa_q1_actor **spots = malloc((game->entity_count != 0 ? game->entity_count : 1) * sizeof *spots);
  if (spots == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, "info_player_deathmatch") == 0) spots[spot_count++] = game->entities[i];
  qa_q1_actor *world = qa_q1ad_ctf_world(state);
  qa_actor_id previous;
  bool has_previous = qa_q1_actor_reference_get(world, "ctf.runeSpawn", &previous);
  size_t ordinal = (size_t)-1;
  for (size_t i = 0; i < spot_count && ordinal == (size_t)-1; i++)
    if (has_previous && qa_same_actor(spots[i]->actor.id, previous)) ordinal = i;
  if (spot_count == 0) { free(spots); qa_range_error("CTF has no info_player_deathmatch to spawn a rune"); }
  qa_q1_actor *spot = spots[(ordinal + 1) % spot_count];
  free(spots);
  (void)qa_q1_actor_reference_set(world, "ctf.runeSpawn", &spot->actor.id);
  return spot;
}
static qa_q1_actor *dropped_rune(qa_q1ad_ctf_state *state, const char *rune, qa_vec3 origin) {
  qa_q1_services *game = state->context->game;
  char classname[64], model[64];
  (void)snprintf(classname, sizeof classname, "item_rune_%s", rune);
  qa_q1_actor *item = qa_q1_create(game, classname, NULL, NULL);
  (void)snprintf(model, sizeof model, "progs/end%d.mdl", rune_ordinal(rune) + 1);
  qa_q1ad_set_text(&item->model, model);
  item->solid = "trigger";
  item->movement = "toss";
  item->movement_flags = qa_js_bit_or(item->movement_flags, 256);
  (void)qa_q1_actor_field_set(item, "ctf.rune", rune);
  /* Object literal order: x's random before y's. */
  double x = -500 + qa_q1_host_random(game) * 1000;
  double y = -500 + qa_q1_host_random(game) * 1000;
  qa_q1_body_patch patch = {
      .has_origin = true, .origin = qa_q1_vadd(origin, (qa_vec3){0, 0, -24}),
      .has_velocity = true, .velocity = {x, y, 400},
      .has_bounds = true, .bounds = {{-16, -16, 0}, {16, 16, 56}}};
  qa_q1_set_body(game, item, &patch);
  item->touch = qa_q1_callback_registry_touch(game->named, item, "ctf:rune_touch");
  qa_q1_link(game, item);
  qa_q1_schedule(game, item, 120, qa_q1_callback_registry_action(game->named, item, "ctf:rune_respawn"));
  return item;
}
static const char *rune_kind(const qa_q1_actor *item) {
  const char *text = qa_q1_actor_text(item, "ctf.rune");
  for (int i = 0; i < 4; i++) if (strcmp(QA_Q1AD_CTF_RUNES[i], text) == 0) return QA_Q1AD_CTF_RUNES[i];
  qa_range_error("CTF rune lost its source kind");
}
void qa_q1ad_ctf_drop_rune(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  const char *rune = qa_q1ad_ctf_rune(state, actor);
  if (rune == NULL) return;
  (void)dropped_rune(state, rune, qa_q1ad_ctf_body(state, actor).origin);
  qa_q1ad_ctf_grant(state, actor, qa_q1ad_ctf_rune_item(rune), 0, 1);
  state->services.haste(state->services.ctx, actor, false);
  qa_q1ad_ctf_update(state, &actor);
}
void qa_q1ad_ctf_regenerate(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1_services *game = state->context->game;
  const char *rune = qa_q1ad_ctf_rune(state, actor);
  state->services.haste(state->services.ctx, actor, rune != NULL && strcmp(rune, "haste") == 0);
  if (rune == NULL || strcmp(rune, "regeneration") != 0 || qa_q1ad_ctf_number(state, actor, "regenTime") >= game->time ||
      qa_q1_health(game, actor) <= 0)
    return;
  double delay = 0;
  if (qa_q1_health(game, actor) < 150) {
    qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
    qa_gameplay_authority_set_health(game->host.combat, owner, qa_js_min(150, qa_q1_health(game, actor) + 5));
    delay += 0.5;
  }
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, actor);
  if (combat.has_value) {
    qa_regular_armor_state armor = combat.value.armor.regular;
    if (armor.kind != QA_REGULAR_ARMOR_NONE && armor.points < 150 &&
        (armor.kind != QA_REGULAR_ARMOR_Q1 || armor.absorption > 0)) {
      qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
      qa_gameplay_authority_set_regular_points(game->host.combat, owner, qa_js_min(150, armor.points + 5), NULL);
      delay += 0.5;
    }
  }
  qa_q1ad_ctf_set(state, actor, "regenTime", game->time + delay);
  if (delay > 0 && qa_q1ad_ctf_number(state, actor, "regenSound") < game->time) {
    qa_q1ad_ctf_set(state, actor, "regenSound", game->time + 1);
    qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "rune/rune4.wav", "body", 1, 1);
  }
}
void qa_q1ad_ctf_start_runes(qa_q1ad_ctf_state *state) {
  if (qa_q1ad_ctf_start_map(state) || qa_q1_actor_number(qa_q1ad_ctf_world(state), "ctf.runesSpawned") != 0) return;
  qa_q1ad_set_number(state->context, qa_q1ad_ctf_world(state), "ctf.runesSpawned", 1);
  qa_q1_services *game = state->context->game;
  qa_q1_actor *timer = qa_q1_create(game, "ctf_rune_spawn", NULL, NULL);
  qa_q1_schedule(game, timer, 0.1, qa_q1_callback_registry_action(game->named, timer, "ctf:rune_spawn"));
}
static void rune_touch(void *ctx, qa_q1_services *game, qa_q1_actor *item, qa_actor_id actor, const qa_vec3 *normal,
                       const qa_touch_surface *surface) {
  (void)normal; (void)surface; /* TS touch reads only (item, actor) */
  qa_q1ad_ctf_state *state = ctx;
  if (!qa_q1_is_player(game, &actor) || qa_q1_health(game, actor) <= 0 || state->services.observer(state->services.ctx, actor))
    return;
  if (qa_q1ad_ctf_rune(state, actor) != NULL) {
    if (qa_q1ad_ctf_number(state, actor, "runeNotice") < game->time) {
      qa_q1_message(game, &actor, "$qc_already_have_rune", true, NULL, 0);
      qa_q1ad_ctf_set(state, actor, "runeNotice", game->time + 5);
    }
    return;
  }
  const char *rune = rune_kind(item);
  int ordinal = rune_ordinal(rune);
  qa_q1ad_ctf_grant(state, actor, qa_q1ad_ctf_rune_item(rune), 1, 1);
  char hud[48], got[48];
  (void)snprintf(hud, sizeof hud, "$qc_rune%d_hud", ordinal + 1);
  qa_q1_message(game, &actor, hud, true, NULL, 0);
  qa_q1_sound(game, qa_q1ad_ctf_owner(state, actor).id, "weapons/lock4.wav", "item", 1, 1);
  qa_q1_effect(game, "pickup", qa_q1ad_ctf_body(state, actor).origin, &actor, 1);
  bool regeneration = strcmp(rune, "regeneration") == 0;
  if ((qa_q1ad_ctf_teamplay(state) == 0 && !regeneration) || (qa_q1ad_ctf_teamplay(state) == 2147483648.0 && regeneration)) {
    (void)snprintf(got, sizeof got, "$qc_got_rune%d", ordinal + 1);
    qa_q1ad_ctf_announce(state, got, &actor, "");
  }
  qa_q1_remove(game, item);
  qa_q1ad_ctf_update(state, &actor);
}
static void rune_respawn(void *ctx, qa_q1_services *game, qa_q1_actor *item) {
  qa_q1ad_ctf_state *state = ctx;
  const char *rune = rune_kind(item);
  (void)dropped_rune(state, rune, qa_q1_get_body(game, next_rune_spawn(state)).origin);
  qa_q1_remove(game, item);
}
static void rune_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *timer) {
  qa_q1ad_ctf_state *state = ctx;
  for (double count = qa_q1_host_random(game) * 10; count > 0; count--) (void)next_rune_spawn(state);
  for (int i = 0; i < 4; i++)
    (void)dropped_rune(state, QA_Q1AD_CTF_RUNES[i], qa_q1_get_body(game, next_rune_spawn(state)).origin);
  qa_q1_remove(game, timer);
}
/* registerDamageSourceEffects("ctf:runes").afterQuad. */
static bool runes_after_quad(void *ctx, qa_damage_request request, double initial, qa_combat_state target,
                             const qa_combat_state *attacker_state, qa_damage_preparation *out) {
  (void)target; (void)attacker_state; /* TS afterQuad reads (request, initial) only */
  qa_q1ad_ctf_state *state = ctx;
  qa_q1_services *game = state->context->game;
  const qa_actor_id *actor = request.attack.attacker;
  double amount = initial;
  if (actor != NULL) {
    const char *rune = qa_q1ad_ctf_rune(state, *actor);
    if (rune != NULL && strcmp(rune, "strength") == 0) amount = (double)(float)(amount * 2);
  }
  const char *target_rune = qa_q1ad_ctf_rune(state, request.target);
  if (target_rune != NULL && strcmp(target_rune, "resistance") == 0) {
    amount = (double)(float)(amount / 2);
    if (qa_q1ad_ctf_number(state, request.target, "resistanceSound") < game->time) {
      qa_q1ad_ctf_set(state, request.target, "resistanceSound", game->time + 1);
      qa_q1_sound(game, qa_q1ad_ctf_owner(state, request.target).id, "rune/rune1.wav", "body", 1, 1);
    }
  }
  if (actor != NULL && qa_q1_is_player(game, actor) && qa_q1ad_ctf_carried(state, request.target) != NULL) {
    const char *attacker_team = qa_q1ad_ctf_last_team(state, *actor);
    const char *target_team = qa_q1ad_ctf_last_team(state, request.target);
    if (attacker_team != target_team && target_team != NULL) qa_q1ad_ctf_set(state, *actor, "lastHurtCarrier", game->time);
  }
  *out = (qa_damage_preparation){.is_continue = true, .amount = amount};
  return true;
}
void qa_q1ad_ctf_register_runes(qa_q1ad_ctf_state *state) {
  qa_q1_services *game = state->context->game;
  qa_q1_callback_registry_register(game->named, "ctf:rune_touch", (qa_q1_callback_handlers){.ctx = state, .touch = rune_touch});
  qa_q1_callback_registry_register(game->named, "ctf:rune_respawn",
                                   (qa_q1_callback_handlers){.ctx = state, .action = rune_respawn});
  qa_q1_callback_registry_register(game->named, "ctf:rune_spawn", (qa_q1_callback_handlers){.ctx = state, .action = rune_spawn});
  qa_q1_register_damage_source_effects(game, "ctf:runes", (qa_q1_damage_source_effects){.ctx = state, .after_quad = runes_after_quad});
}
