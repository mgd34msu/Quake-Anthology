/* Port of src/content/q1/missionpacks/world/hipnotic-hazards.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_hazards.h"

#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"

static bool falsy(double value) { return value == 0 || isnan(value); } /* JS `||=` on a number */
static bool can_take_damage(qa_q1_services *game, qa_actor_id actor) {
  const qa_combat_state_result state = qa_gameplay_authority_read(game->host.combat, actor);
  return state.has_value && state.value.can_take_damage;
}
static double powerup_of(qa_q1_services *game, qa_actor_id actor, qa_q1_powerup powerup) {
  qa_q1_player_state *player = qa_q1_player(game, actor);
  return player != NULL ? qa_q1_player_powerup_get(player, powerup) : 0;
}
static double movement_flags_of(qa_q1_actor *entity) { return entity != NULL ? entity->movement_flags : 0; }
static bool reference(const qa_q1_actor *entity, const char *key, qa_actor_id *out) { return qa_q1_actor_reference_get(entity, key, out); }
static void emit_beam(qa_q1_services *game, qa_actor_id actor, qa_vec3 start, qa_vec3 end) {
  qa_q1_event event = {0};
  event.kind = "beam"; event.beam_style = "lightning2"; event.has_actor = true; event.actor = actor;
  event.start = start; event.end = end;
  qa_q1_emit(game, event);
}
/* [...game.host.actors.observations()].reverse() ids; caller frees. */
static qa_actor_id *observations_reversed(qa_q1_services *game, size_t *out_count) {
  size_t count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(game->host.actors, &count);
  qa_actor_id *ids = malloc((count != 0 ? count : 1) * sizeof *ids);
  if (ids == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) ids[i] = observations[count - 1 - i].id;
  *out_count = count;
  return ids;
}

bool qa_q1mp_is_struck_by_mjolnir(qa_q1_services *game, qa_actor_id actor) {
  size_t count; qa_q1_actor **entities = qa_q1mp_entities(game, &count);
  bool struck = false;
  for (size_t i = 0; i < count && !struck; i++) {
    qa_q1_actor *entity = entities[i];
    if (!(qa_q1mp_streq(entity->classname, "hipnotic_mjolnir_lightning") || qa_q1mp_streq(entity->classname, "hipnotic_tesla_lightning")) || entity->count != 1) continue;
    qa_actor_id enemy;
    struck = reference(entity, "hipnotic:enemy", &enemy) && qa_same_actor(actor, enemy);
  }
  free(entities);
  return struck;
}
static bool visible(qa_q1_services *game, qa_q1_actor *entity, qa_actor_id actor) {
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
  if (!body.has_value) return false;
  qa_vec3 a = qa_q1_get_body(game, entity).origin;
  qa_vec3 b = qa_q1_actor_vector(entity, "view_ofs");
  const qa_vec3 start = qa_q1_vadd(a, b);
  qa_q1_actor *target = qa_q1_get_entity(game, &actor);
  const double z = qa_q1_is_player(game, &actor) ? 22 : target != NULL ? qa_q1_actor_vector(target, "view_ofs").z : 0;
  const qa_q1_trace trace = qa_q1mp_trace(game, start, qa_q1_vadd(body.value.origin, QA_Q1MP_V3(0, 0, z)), &entity->actor.id, false);
  return trace.fraction == 1 && !(trace.in_open && trace.in_water);
}
/* Returns an owned list; *out_count is its length. */
static qa_actor_id *scan(qa_q1_services *game, qa_q1_actor *entity, double radius, bool include_monsters, size_t *out_count) {
  const qa_vec3 origin = qa_q1_get_body(game, entity).origin;
  size_t count; qa_actor_id *ids = observations_reversed(game, &count);
  size_t found = 0;
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id actor = ids[i];
    qa_q1_actor *target = qa_q1_get_entity(game, &actor);
    const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
    if (!body.has_value || qa_js_bit_and(movement_flags_of(target), 128) != 0 ||
        (!qa_q1_is_player(game, &actor) && (!include_monsters || qa_js_bit_and(movement_flags_of(target), 32) == 0))) continue;
    const qa_vec3 center = qa_q1_vadd(body.value.origin, qa_q1_vscale(qa_q1_vadd(body.value.bounds.min, body.value.bounds.max), 0.5));
    if (qa_q1_length(qa_q1_vsub(center, origin)) <= radius && qa_q1_health(game, actor) > 0 && visible(game, entity, actor)) ids[found++] = actor;
  }
  *out_count = found;
  return ids;
}
static void lightning_damage(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 start, qa_vec3 end, const qa_actor_id *from, double amount) {
  qa_actor_id inflictor;
  if (from != NULL) inflictor = *from;
  else if (game->world != NULL) inflictor = game->world->actor.id;
  else qa_range_error("Lightning damage requires worldspawn");
  const qa_vec3 side = QA_Q1MP_V3(-(end.y - start.y) * 16, -(end.y - start.y) * 16, 0);
  const qa_vec3 offsets[3] = {QA_Q1_ZERO, side, qa_q1_vscale(side, -1)};
  qa_actor_id hit[3]; size_t hits = 0;
  for (size_t i = 0; i < 3; i++) {
    const qa_q1_trace trace = qa_q1mp_trace(game, qa_q1_vadd(start, offsets[i]), qa_q1_vadd(end, offsets[i]), &entity->actor.id, true);
    if (!trace.has_actor) continue;
    const qa_actor_id actor = trace.actor;
    bool seen = false;
    for (size_t j = 0; j < hits; j++) if (qa_same_actor(actor, hit[j])) seen = true;
    if (seen) continue;
    hit[hits++] = actor;
    if (!can_take_damage(game, actor) || powerup_of(game, actor, "hipnotic:wetsuit") != 0) continue;
    qa_q1_event event = {0};
    event.kind = "particles"; event.origin = trace.end; event.direction = QA_Q1MP_V3(0, 0, 100); event.color = 225; event.count = amount * 4;
    qa_q1_emit(game, event);
    (void)qa_q1_damage(game, actor, inflictor, from, amount, NULL, "direct", "electric", NULL);
  }
}
static void mine_explode(qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1_radius_damage(game, entity->actor.id, &entity->actor.id, 110, NULL, NULL, "");
  qa_q1_sound(game, entity->actor.id, "weapons/r_exp3.wav", "weapon", 1, 1);
  qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  qa_q1_sound(game, entity->actor.id, "misc/null.wav", "voice", 1, 1);
  qa_q1_body_patch patch = {0}; patch.has_velocity = true; patch.velocity = QA_Q1_ZERO; qa_q1_set_body(game, entity, &patch);
  entity->touch = (qa_q1_bound_touch){0};
  qa_q1mp_set_string(&entity->model, "progs/s_explod.spr"); entity->solid = "none"; entity->frame = 0;
  qa_q1_link(game, entity);
  qa_q1mp_world_later(game, entity, 0.1, "base:explosion_frame");
}
static void mine_home(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->frame = fmod(entity->frame + 1, 9);
  qa_q1mp_world_later(game, entity, 0.2, "hip:mine_home");
  if (qa_q1_actor_number(entity, "search_time") < game->time) {
    double distance = 2000; bool has_selected = false; qa_actor_id selected = {0};
    size_t count; qa_actor_id *found = scan(game, entity, 2000, false, &count);
    for (size_t i = 0; i < count; i++) {
      const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, found[i]);
      if (!body.has_value) continue;
      const double next = qa_q1_length(qa_q1_vsub(body.value.origin, qa_q1_get_body(game, entity).origin));
      if (next < distance) { distance = next; selected = found[i]; has_selected = true; }
    }
    free(found);
    if (has_selected) qa_q1_sound(game, entity->actor.id, "hipitems/spikmine.wav", "voice", 1, 1);
    if (!qa_q1_actor_reference_set(entity, "enemy", has_selected ? &selected : NULL)) qa_range_error("Out of memory");
    qa_q1mp_world_number(entity, "search_time", game->time + 1.3);
  }
  qa_actor_id enemy;
  const bool has_enemy = reference(entity, "enemy", &enemy);
  const qa_body_state_result target = has_enemy ? qa_shared_body_table_read(game->host.bodies, enemy) : (qa_body_state_result){0};
  qa_q1_body_patch patch = {0}; patch.has_velocity = true;
  if (!target.has_value) {
    qa_q1_sound(game, entity->actor.id, "misc/null.wav", "voice", 1, 1);
    patch.velocity = QA_Q1_ZERO; qa_q1_set_body(game, entity, &patch); return;
  }
  const qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(qa_q1_vadd(target.value.origin, QA_Q1MP_V3(0, 0, 10)), qa_q1_get_body(game, entity).origin));
  const qa_vec3 toward = qa_q1_normalize(qa_q1_vsub(target.value.origin, qa_q1_get_body(game, entity).origin));
  const bool in_front = qa_q1_dot(toward, qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward) > 0.3;
  patch.velocity = qa_q1_vscale(direction, qa_q1_option_skill(game) * 50 + (in_front ? 50 : 150));
  qa_q1_set_body(game, entity, &patch);
}
static void lightning_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (game->time > entity->delay) { qa_q1_remove(game, entity); return; }
  const qa_vec3 start = qa_q1_get_body(game, entity).origin, end = qa_q1_actor_vector(entity, "oldorigin");
  qa_actor_id client;
  if (game->host.check_client(game->host.ctx, entity->actor, &client)) emit_beam(game, entity->actor.id, start, end);
  qa_actor_id victim;
  const bool has_victim = reference(entity, "lastvictim", &victim);
  lightning_damage(game, entity, start, end, has_victim ? &victim : NULL, entity->damage);
  qa_q1mp_world_later(game, entity, 0.1, "hip:lightning_bolt");
}
static void lightning_use_impl(qa_q1_services *game, qa_q1_actor *entity) {
  if (game->time >= qa_q1_actor_number(entity, "pausetime")) {
    qa_q1_sound(game, entity->actor.id, qa_js_bit_and(entity->spawnflags, 2) != 0 ? "weapons/lstart.wav" : "weapons/lhit.wav", "voice", 1, 1);
    if (qa_q1mp_streq(entity->classname, "trap_lightning_triggered")) qa_q1mp_world_number(entity, "pausetime", game->time + 0.1);
  }
  qa_vec3 start = qa_q1_get_body(game, entity).origin, end;
  if (entity->target[0] != '\0') {
    qa_actor_id enemy_id;
    qa_q1_actor *enemy = reference(entity, "enemy", &enemy_id) ? qa_q1_get_entity(game, &enemy_id) : NULL;
    if (enemy == NULL) enemy = game->world;
    if (enemy == NULL) qa_range_error("Lightning requires worldspawn");
    end = qa_q1_get_body(game, enemy).origin;
  } else {
    entity->movedir = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward;
    end = qa_q1mp_trace(game, start, qa_q1_vadd(start, qa_q1_vscale(entity->movedir, 600)), &entity->actor.id, false).end;
  }
  const qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(end, start));
  const double distance = qa_q1_length(qa_q1_vsub(end, start)) / 30, remainder = distance - floor(distance);
  if (remainder > 0) {
    start = qa_q1_vadd(start, qa_q1_vscale(direction, (remainder - 1) * 15));
    end = qa_q1_vsub(end, qa_q1_vscale(direction, (remainder - 1) * 15));
  }
  if (qa_q1_actor_number(entity, "duration") > 0.1) {
    qa_q1_actor *bolt = qa_q1_create(game, "hipnotic_lightning", NULL, NULL);
    qa_q1_set_origin(game, bolt, start); qa_q1mp_world_vector(bolt, "oldorigin", end);
    if (!qa_q1_actor_reference_set(bolt, "lastvictim", &entity->actor.id)) qa_range_error("Out of memory");
    bolt->damage = entity->damage; bolt->delay = game->time + qa_q1_actor_number(entity, "duration");
    lightning_think(NULL, game, bolt); return;
  }
  qa_actor_id client;
  if (game->host.check_client(game->host.ctx, entity->actor, &client)) emit_beam(game, entity->actor.id, start, end);
  lightning_damage(game, entity, start, end, &entity->actor.id, entity->damage);
}
static void lightning_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator; lightning_use_impl(game, entity);
}
static qa_actor_id *tesla_scan(qa_q1_services *game, qa_q1_actor *entity, size_t *out_count) {
  size_t count; qa_actor_id *found = scan(game, entity, qa_q1_actor_number(entity, "distance"), qa_js_bit_and(entity->spawnflags, 1) != 0, &count);
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    if (qa_q1mp_is_struck_by_mjolnir(game, found[i])) continue;
    found[kept++] = found[i];
    if ((double)kept == entity->count) break;
  }
  *out_count = kept;
  return found;
}
static void tesla_bolt(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1_actor *owner = entity->has_owner ? qa_q1_get_entity(game, &entity->owner) : NULL;
  if (owner != NULL) qa_q1mp_world_number(owner, "attack_state", 2);
  qa_actor_id enemy;
  const bool has_enemy = reference(entity, "hipnotic:enemy", &enemy);
  const qa_body_state_result target = has_enemy ? qa_shared_body_table_read(game->host.bodies, enemy) : (qa_body_state_result){0};
  if (game->time > entity->delay || !target.has_value) { qa_q1_remove(game, entity); return; }
  const qa_vec3 start = qa_q1_get_body(game, entity).origin;
  const qa_q1_trace trace = qa_q1mp_trace(game, start, target.value.origin, &entity->actor.id, false);
  if (trace.fraction != 1 || !has_enemy || qa_q1_health(game, enemy) <= 0 ||
      qa_q1_length(qa_q1_vsub(start, target.value.origin)) > qa_q1_actor_number(entity, "distance") + 10) { qa_q1_remove(game, entity); return; }
  emit_beam(game, entity->actor.id, start, trace.end);
  qa_actor_id victim;
  const bool has_victim = reference(entity, "lastvictim", &victim);
  lightning_damage(game, entity, start, trace.end, has_victim ? &victim : NULL, entity->damage);
  qa_q1mp_world_later(game, entity, 0.1, "hip:tesla_bolt");
}
static void tesla_think(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (qa_q1_actor_number(entity, "hazard_state") == 0) { qa_q1mp_world_later(game, entity, 0.25, "hip:tesla_think"); return; }
  const double state = qa_q1_actor_number(entity, "attack_state");
  if (state == 0) {
    size_t count; qa_actor_id *targets = tesla_scan(game, entity, &count); free(targets);
    if (count > 0) {
      if (entity->wait > 0) qa_q1_sound(game, entity->actor.id, "misc/tesla.wav", "voice", 1, 1);
      qa_q1mp_world_number(entity, "attack_state", 1);
      qa_q1mp_world_later(game, entity, entity->wait, "hip:tesla_think"); return;
    }
    if (entity->delay > 0 && game->time > qa_q1_actor_number(entity, "search_time")) qa_q1mp_world_number(entity, "attack_state", 3);
    qa_q1mp_world_later(game, entity, 0.25, "hip:tesla_think"); return;
  }
  if (state == 1) {
    size_t count; qa_actor_id *targets = tesla_scan(game, entity, &count);
    for (size_t i = 0; i < count; i++) {
      qa_q1_sound(game, entity->actor.id, "hipweap/mjolhit.wav", "voice", 1, 1);
      qa_q1_actor *bolt = qa_q1_create(game, "hipnotic_tesla_lightning", NULL, NULL);
      bolt->count = 1; bolt->has_owner = true; bolt->owner = entity->actor.id;
      qa_q1mp_field_text(bolt, "deathtype", qa_q1_actor_text(entity, "deathtype"));
      if (!qa_q1_actor_reference_set(bolt, "hipnotic:enemy", &targets[i])) qa_range_error("Out of memory");
      qa_actor_id victim;
      const bool has_victim = reference(entity, "lastvictim", &victim);
      if (!qa_q1_actor_reference_set(bolt, "lastvictim", has_victim ? &victim : NULL)) qa_range_error("Out of memory");
      bolt->delay = game->time + (qa_q1_actor_number(entity, "duration") > 0 ? qa_q1_actor_number(entity, "duration") : 9999);
      bolt->damage = entity->damage;
      qa_q1mp_world_number(bolt, "distance", qa_q1_actor_number(entity, "distance"));
      qa_q1_set_origin(game, bolt, qa_q1_get_body(game, entity).origin);
      qa_q1mp_world_later(game, bolt, 0, "hip:tesla_bolt");
    }
    free(targets);
    qa_q1mp_world_number(entity, "attack_state", 2);
    qa_q1mp_world_later(game, entity, 1, "hip:tesla_think"); return;
  }
  if (state == 2) { qa_q1mp_world_number(entity, "attack_state", 3); qa_q1mp_world_later(game, entity, 0.2, "hip:tesla_think"); return; }
  qa_q1mp_world_number(entity, "attack_state", 0);
  if (qa_q1mp_streq(entity->classname, "trap_gods_wrath")) qa_q1_cancel(game, entity);
  else qa_q1mp_world_later(game, entity, 0.1, "hip:tesla_think");
}
static void mine_first(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_number(entity, "search_time", 0);
  qa_q1_actor_set_damageable(entity, true); entity->aimed_damage = true;
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:mine_use");
  qa_q1mp_world_later(game, entity, 0.1, "hip:mine_home");
}
static void mine_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other;
  if (activator != NULL && qa_q1_is_player(game, activator) && powerup_of(game, *activator, "invisibility") <= game->time) {
    if (!qa_q1_actor_reference_set(entity, "enemy", activator)) qa_range_error("Out of memory");
    qa_q1mp_world_later(game, entity, 0.1, "hip:mine_home");
  }
}
static void mine_die(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *attacker) {
  (void)ctx;
  qa_q1_actor_set_damageable(entity, false);
  game->killed_monsters++;
  qa_q1_event event = {0};
  event.kind = "monster-killed"; event.has_actor = true; event.actor = entity->actor.id;
  event.total = game->total_monsters; event.found = game->killed_monsters;
  qa_q1_emit(game, event);
  qa_q1_use_targets(game, entity, attacker);
  mine_explode(game, entity);
}
static void mine_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (qa_q1_health(game, entity->actor.id) > 0) {
    static const char *const IGNORED[] = {"trap_spike_mine", "missile", "grenade", "hiplaser", "proximity_grenade"};
    const char *classname = game->host.classname(game->host.ctx, other);
    for (size_t i = 0; i < sizeof IGNORED / sizeof IGNORED[0]; i++) if (qa_q1mp_streq(classname, IGNORED[i])) return;
    (void)qa_q1_damage(game, entity->actor.id, entity->actor.id, &entity->actor.id, qa_q1_health(game, entity->actor.id) + 10, NULL, "direct", "", NULL);
  }
  mine_explode(game, entity);
}
static void spike_mine_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_field_text(entity, "deathtype", "$qc_suicide_mine");
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  qa_q1mp_set_string(&entity->model, "progs/spikmine.mdl"); entity->solid = "bbox"; entity->movement = "flymissile";
  entity->angular_velocity = QA_Q1MP_V3(-50, 100, 150);
  entity->max_health = qa_q1_option_skill(game) <= 1 ? 200 : 400;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, entity->max_health);
  entity->frame = 0; entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
  game->total_monsters++;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "hip:mine_explode");
  entity->die = qa_q1_callback_registry_die(game->named, entity, "hip:mine_explode");
  qa_q1mp_world_later(game, entity, 0.2, "hip:mine_first");
}
static void hazard_switch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  qa_q1mp_world_number(entity, "hazard_state", 1 - qa_q1_actor_number(entity, "hazard_state"));
  if (qa_q1_actor_number(entity, "hazard_state") == 1 && entity->think.fn != NULL)
    qa_q1_schedule(game, entity, qa_q1_actor_number(entity, "huntingcharmer") - game->time, entity->think);
}
static void lightning_cycle(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (qa_q1_actor_number(entity, "hazard_state") != 0) lightning_use_impl(game, entity);
  if (qa_q1_actor_number(entity, "cnt") == 0) {
    double delay = qa_js_bit_and(entity->spawnflags, 1) != 0 ? entity->wait * game->host.random(game->host.ctx) : entity->wait;
    qa_q1mp_world_number(entity, "cnt", 1);
    qa_q1mp_world_number(entity, "t_length", game->time + qa_q1_actor_number(entity, "duration") - 0.1);
    qa_q1mp_world_number(entity, "pausetime", qa_js_max(game->time + qa_q1_actor_number(entity, "duration") - 0.1, game->time + 0.3));
    delay = qa_js_max(delay, qa_q1_actor_number(entity, "duration"));
    qa_q1mp_world_number(entity, "t_width", game->time + delay);
  }
  if (game->time >= qa_q1_actor_number(entity, "t_length")) {
    qa_q1mp_world_number(entity, "cnt", 0);
    qa_q1mp_world_later(game, entity, qa_q1_actor_number(entity, "t_width") - game->time, "hip:lightning_think"); return;
  }
  qa_q1mp_world_later(game, entity, 0.2, "hip:lightning_think");
}
static void lightning_first(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  if (entity->target[0] != '\0') {
    qa_q1_actor *first = qa_q1mp_find_first(game, entity->target);
    if (!qa_q1_actor_reference_set(entity, "enemy", first != NULL ? &first->actor.id : NULL)) qa_range_error("Out of memory");
  }
  if (qa_q1mp_streq(entity->classname, "trap_lightning_triggered")) { qa_q1_cancel(game, entity); return; }
  double left = qa_q1_actor_number(entity, "huntingcharmer") + entity->wait;
  double right = qa_q1_actor_number(entity, "ltime");
  qa_q1mp_world_later(game, entity, left + right - game->time,
                      "hip:lightning_think");
}
/* The TS spawn closures capture the registered classname. */
typedef struct hazard_class { const char *classname; } hazard_class;
static hazard_class LIGHTNING_CLASSES[3] = {{"trap_lightning"}, {"trap_lightning_triggered"}, {"trap_lightning_switched"}};
static hazard_class TESLA_CLASSES[2] = {{"trap_tesla_coil"}, {"trap_gods_wrath"}};
static void lightning_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const char *classname = ((const hazard_class *)ctx)->classname;
  qa_q1mp_field_text(entity, "deathtype", "$qc_suicide_electrocuted");
  if (falsy(entity->wait)) entity->wait = 1;
  if (falsy(entity->damage)) entity->damage = 30;
  if (qa_q1_actor_number(entity, "duration") == 0) qa_q1mp_world_number(entity, "duration", 0.1);
  qa_q1mp_world_number(entity, "cnt", 0);
  qa_q1mp_world_number(entity, "hazard_state", qa_q1mp_streq(classname, "trap_lightning") ? 1 : qa_q1_actor_number(entity, "state"));
  qa_q1mp_world_number(entity, "huntingcharmer", qa_q1_actor_number(entity, "nextthink"));
  entity->use = qa_q1_callback_registry_use(game->named, entity, qa_q1mp_streq(classname, "trap_lightning_switched") ? "hip:hazard_switch" : "hip:lightning_use");
  qa_q1mp_world_later(game, entity, 0.25, "hip:lightning_first");
}
static void wrath_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  if (qa_q1_actor_number(entity, "attack_state") != 0) return;
  qa_q1mp_world_number(entity, "search_time", game->time + entity->delay);
  if (!qa_q1_actor_reference_set(entity, "lastvictim", activator)) qa_range_error("Out of memory");
  tesla_think(ctx, game, entity);
}
static void tesla_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const bool wrath = qa_q1mp_streq(((const hazard_class *)ctx)->classname, "trap_gods_wrath");
  qa_q1mp_field_text(entity, "deathtype", wrath ? "$qc_suicide_wrath" : "$qc_suicide_electrocuted");
  if (falsy(entity->wait)) entity->wait = 2;
  if (falsy(entity->damage)) entity->damage = 2 + 5 * qa_q1_option_skill(game);
  if (qa_q1_actor_number(entity, "duration") == 0) qa_q1mp_world_number(entity, "duration", -1);
  if (qa_q1_actor_number(entity, "distance") == 0) qa_q1mp_world_number(entity, "distance", 600);
  if (falsy(entity->delay)) entity->delay = wrath ? 5 : -1;
  qa_q1mp_world_number(entity, "hazard_state", qa_q1_actor_number(entity, "state"));
  qa_q1mp_world_number(entity, "attack_state", 0);
  if (!qa_q1_actor_reference_set(entity, "lastvictim", NULL)) qa_range_error("Out of memory");
  entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:hazard_switch");
  qa_q1mp_world_later(game, entity, game->host.random(game->host.ctx), "hip:tesla_think");
  if (wrath) {
    entity->wait = 0; qa_q1mp_world_number(entity, "hazard_state", 1); qa_q1_cancel(game, entity);
    entity->use = qa_q1_callback_registry_use(game->named, entity, "hip:wrath_use");
  }
}
static void gravity_well(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  qa_q1mp_world_number(entity, "ltime", game->time);
  size_t count; qa_actor_id *targets = tesla_scan(game, entity, &count);
  for (size_t i = 0; i < count; i++) {
    const qa_actor_id actor = targets[i];
    qa_owned_actor owner;
    const bool has_owner = qa_session_actor_registry_resolve_owned(game->host.actors, actor, &owner);
    qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, actor);
    if (!has_owner || !body.has_value) continue;
    const double scale = qa_js_bit_and(entity->spawnflags, 2) != 0 && powerup_of(game, actor, "hipnotic:wetsuit") > game->time ? 0.6 : 1;
    const qa_vec3 pull = qa_q1_vscale(qa_q1_normalize(qa_q1_vsub(qa_q1_get_body(game, entity).origin, body.value.origin)), entity->speed * scale);
    body.value.velocity = qa_q1_vadd(body.value.velocity, pull);
    qa_shared_body_table_write(game->host.bodies, owner, body.value);
  }
  free(targets);
  qa_q1mp_world_later(game, entity, 0.1, "hip:gravity_well");
}
static void gravity_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (entity->attack_finished > game->time || !can_take_damage(game, other)) return;
  (void)qa_q1_damage(game, other, entity->actor.id, &entity->actor.id, entity->damage, NULL, "direct", "", NULL);
  entity->attack_finished = game->time + 0.2;
}
static void gravity_spawn(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->solid = "trigger"; entity->movement = "none";
  if (falsy(entity->damage)) entity->damage = 10000;
  if (falsy(entity->speed)) entity->speed = 210;
  if (qa_q1_actor_number(entity, "distance") == 0) qa_q1mp_world_number(entity, "distance", 600);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, "hip:gravity_well");
  qa_q1_set_bounds(game, entity, (qa_bounds){{-16, -16, -16}, {16, 16, 16}});
  qa_q1mp_world_number(entity, "ltime", game->time);
  qa_q1mp_world_later(game, entity, 0.1, "hip:gravity_well");
}
void qa_q1mp_register_hipnotic_hazards(qa_q1_services *game) {
  qa_q1_callback_registry *named = game->named;
  qa_q1_callback_registry_register(named, "hip:mine_home", (qa_q1_callback_handlers){.action = mine_home});
  qa_q1_callback_registry_register(named, "hip:mine_first", (qa_q1_callback_handlers){.action = mine_first});
  qa_q1_callback_registry_register(named, "hip:mine_use", (qa_q1_callback_handlers){.use = mine_use});
  qa_q1_callback_registry_register(named, "hip:mine_explode", (qa_q1_callback_handlers){.die = mine_die, .touch = mine_touch});
  qa_q1_register_spawn(game, "trap_spike_mine", spike_mine_spawn, NULL);
  qa_q1_callback_registry_register(named, "hip:lightning_bolt", (qa_q1_callback_handlers){.action = lightning_think});
  qa_q1_callback_registry_register(named, "hip:lightning_use", (qa_q1_callback_handlers){.use = lightning_use});
  qa_q1_callback_registry_register(named, "hip:hazard_switch", (qa_q1_callback_handlers){.use = hazard_switch});
  qa_q1_callback_registry_register(named, "hip:lightning_think", (qa_q1_callback_handlers){.action = lightning_cycle});
  qa_q1_callback_registry_register(named, "hip:lightning_first", (qa_q1_callback_handlers){.action = lightning_first});
  for (size_t i = 0; i < 3; i++) qa_q1_register_spawn(game, LIGHTNING_CLASSES[i].classname, lightning_spawn, &LIGHTNING_CLASSES[i]);
  qa_q1_callback_registry_register(named, "hip:tesla_bolt", (qa_q1_callback_handlers){.action = tesla_bolt});
  qa_q1_callback_registry_register(named, "hip:tesla_think", (qa_q1_callback_handlers){.action = tesla_think});
  qa_q1_callback_registry_register(named, "hip:wrath_use", (qa_q1_callback_handlers){.use = wrath_use});
  for (size_t i = 0; i < 2; i++) qa_q1_register_spawn(game, TESLA_CLASSES[i].classname, tesla_spawn, &TESLA_CLASSES[i]);
  qa_q1_callback_registry_register(named, "hip:gravity_well", (qa_q1_callback_handlers){.action = gravity_well, .touch = gravity_touch});
  qa_q1_register_spawn(game, "trap_gravity_well", gravity_spawn, NULL);
}
