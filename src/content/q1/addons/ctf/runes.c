/* Port of src/content/q1/addons/ctf/runes.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/ctf/runes.h"

#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

bool qa_q1ad_ctf_haste_interval(qa_item_id item, double *out) {
  static const struct { const char *item; double interval; } TABLE[] = {
    {"q1:weapon/axe", 0.3}, {"q1:weapon/shotgun", 0.3}, {"q1:weapon/supershotgun", 0.4},
    {"q1:weapon/grenadelauncher", 0.3}, {"q1:weapon/rocketlauncher", 0.4}};
  for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
    if (strcmp(item, TABLE[i].item) == 0) { *out = TABLE[i].interval; return true; }
  return false;
}
static qa_q1ad_ctf_state *state_of(qa_q1ad_context *context) { return context->slice; }
static int rune_ordinal(const char *rune) {
  for (int i = 0; i < 4; i++) if (strcmp(QA_Q1AD_CTF_RUNES[i], rune) == 0) return i;
  return -1;
}
static qa_q1ad_actor *next_rune_spawn(qa_q1ad_ctf_state *state) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_actor *entities[512], *spots[128];
  size_t count = qa_q1ad_entities(context, entities, 512), spot_count = 0;
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_player_deathmatch") == 0 &&
        spot_count < 128) spots[spot_count++] = entities[i];
  qa_q1ad_actor *world = qa_q1ad_ctf_world(state);
  qa_actor_id previous;
  bool has_previous = qa_q1ad_actor_ref(world, "ctf.runeSpawn", &previous);
  int ordinal = -1;
  if (has_previous)
    for (size_t i = 0; i < spot_count; i++)
      if (qa_same_actor(spots[i]->actor.id, previous)) ordinal = (int)i;
  if (spot_count == 0) qa_range_error("CTF has no info_player_deathmatch to spawn a rune");
  qa_q1ad_actor *spot = spots[(size_t)(ordinal + 1) % spot_count];
  qa_q1ad_actor_set_ref(world, "ctf.runeSpawn", &spot->actor.id);
  return spot;
}
static qa_q1ad_actor *dropped_rune(qa_q1ad_ctf_state *state, const char *rune, qa_vec3 origin) {
  qa_q1ad_context *context = state->context;
  char classname[64], model[64];
  (void)snprintf(classname, sizeof classname, "item_rune_%s", rune);
  if (context->game.create == NULL) return NULL;
  qa_q1ad_actor *item = context->game.create(context->game.ctx, classname);
  if (item == NULL) return NULL;
  (void)snprintf(model, sizeof model, "progs/end%d.mdl", rune_ordinal(rune) + 1);
  qa_q1ad_actor_set(item, "rune.model", model);
  item->model = qa_q1ad_actor_text(item, "rune.model");
  item->solid = "trigger";
  item->movement = "toss";
  item->movement_flags |= 256;
  qa_q1ad_actor_set(item, "ctf.rune", rune);
  qa_q1ad_body_patch patch = {
      .has_origin = true, .origin = qa_add3(origin, (qa_vec3){0, 0, -24}),
      .has_velocity = true,
      .velocity = {-500 + qa_q1ad_random(context) * 1000, -500 + qa_q1ad_random(context) * 1000, 400},
      .has_bounds = true, .bounds = {{-16, -16, 0}, {16, 16, 56}}};
  qa_q1ad_set_body(context, item, &patch);
  qa_q1ad_bind(item->touch, "ctf:rune_touch");
  qa_q1ad_link(context, item);
  qa_q1ad_schedule(context, item, 120, "ctf:rune_respawn");
  return item;
}
static const char *rune_kind(qa_q1ad_actor *item) {
  const char *text = qa_q1ad_actor_text(item, "ctf.rune");
  for (int i = 0; i < 4; i++) if (strcmp(QA_Q1AD_CTF_RUNES[i], text) == 0) return QA_Q1AD_CTF_RUNES[i];
  qa_range_error("CTF rune lost its source kind");
}
void qa_q1ad_ctf_drop_rune(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  const char *rune = qa_q1ad_ctf_rune(state, actor);
  if (rune == NULL) return;
  char item[64];
  (void)dropped_rune(state, rune, qa_q1ad_ctf_body(state, actor).origin);
  qa_q1ad_ctf_grant(state, actor, qa_q1ad_ctf_rune_item(rune, item), 0, 1);
  if (state->services.haste != NULL) state->services.haste(state->services.ctx, actor, false);
  qa_q1ad_ctf_update(state, &actor);
}
void qa_q1ad_ctf_regenerate(qa_q1ad_ctf_state *state, qa_actor_id actor) {
  qa_q1ad_context *context = state->context;
  const char *rune = qa_q1ad_ctf_rune(state, actor);
  if (state->services.haste != NULL)
    state->services.haste(state->services.ctx, actor, rune != NULL && strcmp(rune, "haste") == 0);
  if (rune == NULL || strcmp(rune, "regeneration") != 0 ||
      qa_q1ad_ctf_number(state, actor, "regenTime") >= context->game.time ||
      qa_q1ad_health(context, actor) <= 0) return;
  double delay = 0;
  if (qa_q1ad_health(context, actor) < 150 && context->game.combat_set_health != NULL) {
    double next = qa_q1ad_health(context, actor) + 5;
    context->game.combat_set_health(context->game.ctx, qa_q1ad_ctf_owner(state, actor),
        next < 150 ? next : 150);
    delay += 0.5;
  }
  qa_q1ad_armor armor = {0};
  bool known = context->game.combat_armor != NULL &&
      context->game.combat_armor(context->game.ctx, actor, &armor);
  const char *kind = known && armor.kind != NULL ? armor.kind : "none";
  if (known && strcmp(kind, "none") != 0 && armor.points < 150 &&
      (strcmp(kind, "q1") != 0 || armor.absorption > 0) &&
      context->game.combat_set_regular_points != NULL) {
    double next = armor.points + 5;
    context->game.combat_set_regular_points(context->game.ctx, qa_q1ad_ctf_owner(state, actor),
        next < 150 ? next : 150);
    delay += 0.5;
  }
  qa_q1ad_ctf_set(state, actor, "regenTime", context->game.time + delay);
  if (delay > 0 && qa_q1ad_ctf_number(state, actor, "regenSound") < context->game.time) {
    qa_q1ad_ctf_set(state, actor, "regenSound", context->game.time + 1);
    qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
    qa_q1ad_sound_actor(context, owner.id, "rune/rune4.wav", "body");
  }
}
void qa_q1ad_ctf_start_runes(qa_q1ad_ctf_state *state) {
  if (qa_q1ad_ctf_start_map(state)) return;
  qa_q1ad_actor *world = qa_q1ad_ctf_world(state);
  if (qa_q1ad_actor_number(world, "ctf.runesSpawned") != 0) return;
  qa_q1ad_set_number(state->context, world, "ctf.runesSpawned", 1);
  if (state->context->game.create == NULL) return;
  qa_q1ad_actor *timer = state->context->game.create(state->context->game.ctx, "ctf_rune_spawn");
  if (timer != NULL) qa_q1ad_schedule(state->context, timer, 0.1, "ctf:rune_spawn");
}
static void rune_touch(qa_q1ad_context *context, qa_q1ad_actor *item,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  qa_q1ad_ctf_state *state = state_of(context);
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0 ||
      (state->services.observer != NULL && state->services.observer(state->services.ctx, *other))) return;
  qa_actor_id actor = *other;
  if (qa_q1ad_ctf_rune(state, actor) != NULL) {
    if (qa_q1ad_ctf_number(state, actor, "runeNotice") < context->game.time) {
      qa_q1ad_message(context, actor, "$qc_already_have_rune", false, NULL, 0);
      qa_q1ad_ctf_set(state, actor, "runeNotice", context->game.time + 5);
    }
    return;
  }
  const char *rune = rune_kind(item);
  int ordinal = rune_ordinal(rune);
  char item_id[64], hud[48], got[48];
  qa_q1ad_ctf_grant(state, actor, qa_q1ad_ctf_rune_item(rune, item_id), 1, 1);
  (void)snprintf(hud, sizeof hud, "$qc_rune%d_hud", ordinal + 1);
  qa_q1ad_message(context, actor, hud, false, NULL, 0);
  qa_owned_actor owner = qa_q1ad_ctf_owner(state, actor);
  qa_q1ad_sound_actor(context, owner.id, "weapons/lock4.wav", "item");
  qa_q1ad_effect(context, "pickup", qa_q1ad_ctf_body(state, actor).origin, &actor, 0);
  double teamplay = qa_q1ad_ctf_teamplay(state);
  if ((teamplay == 0 && strcmp(rune, "regeneration") != 0) ||
      (teamplay == 2147483648.0 && strcmp(rune, "regeneration") == 0)) {
    (void)snprintf(got, sizeof got, "$qc_got_rune%d", ordinal + 1);
    qa_q1ad_ctf_announce(state, got, &actor, "");
  }
  qa_q1ad_remove(context, item);
  qa_q1ad_ctf_update(state, &actor);
}
static void rune_respawn(qa_q1ad_context *context, qa_q1ad_actor *item) {
  qa_q1ad_ctf_state *state = state_of(context);
  (void)dropped_rune(state, rune_kind(item),
      qa_q1ad_body_of(context, next_rune_spawn(state)).origin);
  qa_q1ad_remove(context, item);
}
static void rune_spawn(qa_q1ad_context *context, qa_q1ad_actor *timer) {
  qa_q1ad_ctf_state *state = state_of(context);
  for (double count = qa_q1ad_random(context) * 10; count > 0; count--) (void)next_rune_spawn(state);
  for (int i = 0; i < 4; i++)
    (void)dropped_rune(state, QA_Q1AD_CTF_RUNES[i],
        qa_q1ad_body_of(context, next_rune_spawn(state)).origin);
  qa_q1ad_remove(context, timer);
}
double qa_q1ad_ctf_rune_damage(qa_q1ad_ctf_state *state, const qa_actor_id *attacker,
                               qa_actor_id target, double amount) {
  qa_q1ad_context *context = state->context;
  if (attacker != NULL) {
    const char *rune = qa_q1ad_ctf_rune(state, *attacker);
    if (rune != NULL && strcmp(rune, "strength") == 0) amount = (double)(float)(amount * 2);
  }
  const char *target_rune = qa_q1ad_ctf_rune(state, target);
  if (target_rune != NULL && strcmp(target_rune, "resistance") == 0) {
    amount = (double)(float)(amount / 2);
    if (qa_q1ad_ctf_number(state, target, "resistanceSound") < context->game.time) {
      qa_q1ad_ctf_set(state, target, "resistanceSound", context->game.time + 1);
      qa_owned_actor owner = qa_q1ad_ctf_owner(state, target);
      qa_q1ad_sound_actor(context, owner.id, "rune/rune1.wav", "body");
    }
  }
  if (attacker != NULL && qa_q1ad_is_player(context, attacker) &&
      qa_q1ad_ctf_carried(state, target) != NULL) {
    const char *attacker_team = qa_q1ad_ctf_last_team(state, *attacker);
    const char *target_team = qa_q1ad_ctf_last_team(state, target);
    bool differ = attacker_team == NULL || target_team == NULL || strcmp(attacker_team, target_team) != 0;
    if (differ && target_team != NULL)
      qa_q1ad_ctf_set(state, *attacker, "lastHurtCarrier", context->game.time);
  }
  return amount;
}
void qa_q1ad_ctf_register_runes(qa_q1ad_ctf_state *state) {
  qa_q1ad_context *context = state->context;
  qa_q1ad_register_named(context, "ctf:rune_touch", (qa_q1ad_handlers){.touch = rune_touch});
  qa_q1ad_register_named(context, "ctf:rune_respawn", (qa_q1ad_handlers){.action = rune_respawn});
  qa_q1ad_register_named(context, "ctf:rune_spawn", (qa_q1ad_handlers){.action = rune_spawn});
  if (context->game.register_rule != NULL) context->game.register_rule(context->game.ctx, "ctf:runes");
}
