/* Port of src/content/q2/rerelease/monsters/base-variants/medic.ts.
 * Rerelease m_medic.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/medic.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/content/q2/missionpacks/monsters/combat.h"
#include "qa/content/q2/missionpacks/monsters/medic.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/spawn_placement.h"
#include "qa/content/q2/rerelease/monsters/tables/medic.h"
#include "../rrm_internal.h"
#include "qa/foundation/js.h"

static const qa_vec3 cable_offsets[10] = {
  {45, -9.2, 15.5}, {48.4, -9.7, 15.2}, {47.8, -9.8, 15.8},
  {47.3, -9.3, 14.3}, {45.4, -10.1, 13.1}, {41.9, -12.7, 12},
  {37.8, -15.8, 11.2}, {34.3, -18.4, 10.7}, {32.7, -19.7, 10.4}, {32.7, -19.7, 10.4},
};
static const qa_vec3 reinforcement_positions[5] = {
  {80, 0, 0}, {40, 60, 0}, {40, -60, 0}, {0, 80, 0}, {0, -80, 0}};
static const char default_reinforcements[] =
  "monster_soldier_light 1;monster_soldier 2;monster_soldier_ss 2;monster_infantry 3;monster_gunner 4;monster_medic 5;monster_gladiator 6";

typedef struct reinforcement {
  char *classname;
  double strength;
  qa_bounds bounds;
} reinforcement;

typedef struct medic_ctx {
  qa_q2m_monsters *monsters;
  qa_q2mpm_weapons weapons;
  qa_q2mpm_monster_state *source;
} medic_ctx;

static char *dup_range(const char *start, size_t length) {
  char *copy = malloc(length + 1);
  if (!copy) qa_raise("Error", 0, 0, "Out of memory parsing medic reinforcements");
  memcpy(copy, start, length);
  copy[length] = '\0';
  return copy;
}
size_t qa_q2rm_rerelease_medic_reinforcements(const qa_q2_spawn_fields *fields,
                                              qa_q2rm_medic_reinforcement_choice **out) {
  const char *value = qa_q2_spawn_get(fields, "reinforcements");
  if (!value) value = default_reinforcements;
  *out = NULL;
  if (strcmp(value, "") == 0) return 0;
  size_t count = 1;
  for (const char *p = value; *p; p++) if (*p == ';') count++;
  qa_q2rm_medic_reinforcement_choice *entries = malloc(count * sizeof *entries);
  if (!entries) qa_raise("Error", 0, 0, "Out of memory parsing medic reinforcements");
  size_t index = 0;
  const char *entry = value;
  for (;;) {
    const char *end = strchr(entry, ';');
    size_t length = end ? (size_t)(end - entry) : strlen(entry);
    /* trim, then split on whitespace runs (TS split(/\s+/)). */
    const char *s = entry, *e = entry + length;
    while (s < e && isspace((unsigned char)*s)) s++;
    while (e > s && isspace((unsigned char)e[-1])) e--;
    const char *word_end = s;
    while (word_end < e && !isspace((unsigned char)*word_end)) word_end++;
    entries[index].classname = dup_range(s, (size_t)(word_end - s));
    const char *num = word_end;
    while (num < e && isspace((unsigned char)*num)) num++;
    const char *num_end = num;
    while (num_end < e && !isspace((unsigned char)*num_end)) num_end++;
    /* JS Number.parseInt(parts[1]): missing or non-numeric is NaN. */
    /* strtod read "12e3" as 12000 and "0x10" as 16; parseInt stops at the first non-digit. */
    entries[index].strength = num < num_end ? qa_js_parse_int(num, 10) : NAN;
    index++;
    if (!end) break;
    entry = end + 1;
  }
  *out = entries;
  return index;
}
void qa_q2rm_medic_reinforcements_free(qa_q2rm_medic_reinforcement_choice *entries, size_t count) {
  for (size_t i = 0; i < count; i++) free(entries[i].classname);
  free(entries);
}

static void emit_beam_event(qa_q2_entity *beam, qa_q2_game_services *game, bool visible) {
  qa_q2_presentation_event event;
  memset(&event, 0, sizeof event);
  event.has_actor = true; event.kind = "beam"; event.has_actor = true; event.actor = beam->actor.id;
  event.start = q2rm_body(game, beam).origin;
  event.end = beam->pos2;
  event.width = 1;
  event.color = beam->skin;
  event.visible = visible;
  game->host->emit(game->host->ctx, event);
}
static void spawn_grow_laser_think_fn(void *ctx, qa_q2_entity *beam, qa_q2_game_services *game) {
  (void)ctx;
  qa_q2_entity *owner = game->entity(game->ctx, beam->has_owner ? &beam->owner : NULL);
  if (!owner) { game->remove(game->ctx, beam); return; }
  double theta = q2rm_rnd(game) * 2 * acos(-1.0);
  double phi = acos(q2rm_rnd(game) * 2 - 1);
  qa_vec3 direction = {sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi)};
  beam->pos2 = qa_add3_double(q2rm_body(game, beam).origin, qa_scale3_double(direction, owner->scale * 9));
  game->link(game->ctx, beam);
  emit_beam_event(beam, game, true);
  qa_q2_think self = {NULL, spawn_grow_laser_think_fn};
  game->schedule(game->ctx, beam, 0.001, self);
}
static void spawn_grow_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  if (q2rm_now(game) >= entity->timestamp) {
    qa_q2_entity *beam = game->entity(game->ctx, entity->has_beam ? &entity->beam : NULL);
    if (beam) {
      emit_beam_event(beam, game, false);
      game->remove(game->ctx, beam);
    }
    game->remove(game->ctx, entity);
    return;
  }
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true;
  qa_vec3 a = q2rm_body(game, entity).angles;
  qa_vec3 b = qa_scale3_double(entity->angular_velocity, game->host->frame_seconds(game->host->ctx));
  change.angles = qa_add3_double(a, b);
  q2rm_move_body(game, entity, change, false, false);
  double t = 1 - (q2rm_now(game) - (entity->timestamp - entity->wait)) / entity->wait;
  double scale = (entity->decel + t * (entity->accel - entity->decel)) / 16;
  entity->scale = qa_js_max(0.001, qa_js_min(16, scale));
  entity->alpha = t * t;
  game->show(game->ctx, entity);
  qa_q2_think self = {NULL, spawn_grow_think_fn};
  game->schedule(game->ctx, entity, 0.1, self);
}
static void spawn_grow(qa_q2m_context *context, qa_vec3 origin, double radius) {
  qa_q2_game_services *game = context->game;
  qa_q2_rerelease_random *random = qa_q2rm_rerelease_random(context);
  qa_q2_entity *entity = game->create(game->ctx, "spawngro", NULL, 0);
  double x2 = (double)qa_q2_rerelease_random_integer_max(random, 360);
  double y2 = (double)qa_q2_rerelease_random_integer_max(random, 360);
  double z2 = (double)qa_q2_rerelease_random_integer_max(random, 360);
  qa_vec3 angles = {x2, y2, z2};
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_angles = true; change.angles = angles;
  q2rm_move_body(game, entity, change, true, false);
  double x = qa_q2_rerelease_random_float_range(random, 280, 360) * 2.0;
  double y = qa_q2_rerelease_random_float_range(random, 280, 360) * 2.0;
  double z = qa_q2_rerelease_random_float_range(random, 280, 360) * 2.0;
  entity->angular_velocity = q2rm_v3(x, y, z);
  entity->model = "models/items/spawngro3/tris.md2";
  entity->render_flags = 32768;
  entity->skin = 1;
  entity->accel = radius;
  entity->decel = radius * 2;
  entity->scale = qa_js_max(0.001, qa_js_min(8, radius / 16));
  entity->wait = 1;
  entity->timestamp = q2rm_now(game) + 1;
  game->solid(game->ctx, entity, "none");
  game->motion(game->ctx, entity, "stationary");
  qa_q2_think think = {NULL, spawn_grow_think_fn};
  game->schedule(game->ctx, entity, 0.1, think);
  game->link(game->ctx, entity);
  game->show(game->ctx, entity);
  qa_q2_entity *beam = game->create(game->ctx, "spawngro_beam", NULL, 0);
  entity->beam = beam->actor.id; entity->has_beam = true;
  beam->owner = entity->actor.id; beam->has_owner = true;
  beam->frame = 1;
  beam->skin = (double)0x30303030u;
  beam->render_flags = 128 | 512 | 64;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  q2rm_move_body(game, beam, change, true, false);
  spawn_grow_laser_think_fn(NULL, beam, game);
}

static void medic_sound(qa_q2m_context *c, const char *normal, const char *commander, double channel, double attenuation) {
  char path[96];
  if (qa_q2mpm_monster_mass(c) == 400) (void)snprintf(path, sizeof path, "medic/%s.wav", normal);
  else (void)snprintf(path, sizeof path, "medic_commander/%s.wav", commander);
  q2rm_sound(c->game, c->entity, path, true, channel, true, 1, true, attenuation);
}
static bool restore_enemy(medic_ctx *box, qa_q2m_context *c) {
  qa_q2m_state *state = c->state;
  if (state->has_old_enemy && qa_q2m_health(c->game, &state->old_enemy) > 0) {
    c->entity->enemy = state->old_enemy;
    c->entity->has_enemy = true;
    qa_q2m_monsters_hunt_target(box->monsters, c);
  } else {
    c->entity->has_enemy = false;
    c->entity->has_goal = false;
    state->has_old_enemy = false;
    if (!c->find_target(c->impl_ctx, c)) {
      state->pause_time = 100000000;
      c->stand(c->impl_ctx, c);
      return false;
    }
  }
  return true;
}
static void cleanup(medic_ctx *box, qa_q2m_context *c, bool change_frame) {
  qa_q2_entity *target = c->game->entity(c->game->ctx, c->entity->has_enemy ? &c->entity->enemy : NULL);
  if (target) qa_q2mpm_cleanup_rogue_heal_target(box->monsters, box->source, target, c->game);
  if (!restore_enemy(box, c)) return;
  if (change_frame) c->state->next_frame = qa_q2rm_medic_frames_value.attack52;
}
static void abort_heal(medic_ctx *box, qa_q2m_context *c, bool change_frame, bool gib, bool mark) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2_entity *target = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
  if (target) qa_q2mpm_cleanup_rogue_heal_target(box->monsters, box->source, target, game);
  if (target && mark) {
    qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(box->source, target);
    qa_q2_entity *previous = patient->has_bad_medic1 ? game->entity(game->ctx, &patient->bad_medic1) : NULL;
    if (previous && strncmp(previous->classname, "monster_medic", 13) == 0) {
      patient->bad_medic2 = entity->actor.id; patient->has_bad_medic2 = true;
    } else {
      patient->bad_medic1 = entity->actor.id; patient->has_bad_medic1 = true;
    }
  }
  if (target && gib) {
    qa_q2m_context *patient = qa_q2m_monsters_context(box->monsters, target->actor.id);
    double threshold = patient ? patient->state->gib_health : 0;
    (void)q2rm_damage(game, target->actor.id, entity, &entity->actor.id, threshold == 0 ? 500 : -threshold, 0,
                      q2rm_zero(), q2rm_body(game, target).origin, q2rm_v3(0, 0, 1), 0, false, 0);
  }
  cleanup(box, c, change_frame);
  state->medic = false;
  qa_q2mpm_state_get(box->source, entity)->medic_tries = 0;
}
static qa_q2_entity *find_dead(medic_ctx *box, qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  double left = qa_q2mpm_state_get(box->source, entity)->react_to_damage_time;
  double right = q2rm_now(game);
  if (left > right) return NULL;
  qa_q2_entity *best = NULL;
  size_t count = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, q2rm_body(game, entity).origin,
                                                 state->stand_ground ? 400 : 1024, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    qa_q2_entity *target = game->entity(game->ctx, &actor);
    qa_q2m_context *candidate = qa_q2m_monsters_context(box->monsters, actor);
    if (!target || target == entity || !q2rm_has(target->server_flags, 4)
        || (candidate && candidate->state->good_guy) || strncmp(target->classname, "player", 6) == 0) continue;
    qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(box->source, target);
    qa_q2_entity *healer = patient->has_healer ? game->entity(game->ctx, &patient->healer) : NULL;
    if ((patient->has_bad_medic1 && qa_same_actor(patient->bad_medic1, entity->actor.id))
        || (patient->has_bad_medic2 && qa_same_actor(patient->bad_medic2, entity->actor.id))
        || (healer && qa_q2m_health(game, &healer->actor.id) > 0 && q2rm_has(healer->server_flags, 4)
            && qa_q2m_monsters_context(box->monsters, healer->actor.id)
            && qa_q2m_monsters_context(box->monsters, healer->actor.id)->state->medic)) continue;
    bool think_ok = true;
    if (target->has_next_think) {
      bool has_resolved = false; qa_q2_think resolved = qa_q2_think_resolve(game->source_callbacks, "monster_dead_think", &has_resolved);
      think_ok = has_resolved && qa_q2_think_eq(target->think, resolved);
    }
    if (qa_q2m_health(game, &actor) > 0 || !think_ok || !qa_q2m_visible(c, &actor)) continue;
    qa_vec3 a = q2rm_body(game, entity).origin;
    qa_vec3 b = q2rm_body(game, target).origin;
    if (qa_hypot3(qa_sub3_double(a, b)) <= 32) continue;
    if (!best || target->max_health > best->max_health) best = target;
  }
  if (best) entity->timestamp = q2rm_now(game) + 10;
  return best;
}
static bool acquire(medic_ctx *box, qa_q2m_context *c) {
  qa_q2_entity *target = find_dead(box, c);
  if (!target) return false;
  if (c->entity->has_enemy) {
    c->state->old_enemy = c->entity->enemy;
    c->state->has_old_enemy = true;
  } else c->state->has_old_enemy = false;
  c->entity->enemy = target->actor.id;
  c->entity->has_enemy = true;
  qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(box->source, target);
  patient->healer = c->entity->actor.id;
  patient->has_healer = true;
  c->state->medic = true;
  qa_q2m_monsters_found_target(box->monsters, c);
  return true;
}
static void run_impl(medic_ctx *box, qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  if (!c->state->medic && acquire(box, c)) return;
  q2rm_set_move(c, c->state->stand_ground ? "medic_move_stand" : "medic_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { run_impl(ctx, c); }
static void idle_impl(medic_ctx *box, qa_q2m_context *c) {
  medic_sound(c, "idle", "medidle", 2, 2);
  if (!c->state->has_old_enemy) (void)acquire(box, c);
}
static void idle_fn(void *ctx, qa_q2m_context *c) { idle_impl(ctx, c); }
static void attack_impl(medic_ctx *box, qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  bool melee_range = qa_q2m_target_distance(c) < 80;
  qa_q2mpm_rogue_state *private_state = qa_q2mpm_state_get(box->source, entity);
  if (private_state->blocked) {
    q2rm_set_move(c, "medic_move_callReinforcements");
    private_state->blocked = false;
  }
  double random = q2rm_rnd(game);
  bool commander = qa_q2mpm_monster_mass(c) > 400;
  if (state->medic) {
    q2rm_set_move(c, commander && random > 0.8 && state->monster_slots > state->monster_used
        ? "medic_move_callReinforcements" : "medic_move_attackCable");
    return;
  }
  q2rm_set_move(c, state->attack_state == QA_Q2M_ATTACK_BLIND
      || (commander && random > 0.2 && !melee_range && state->monster_slots > state->monster_used)
      ? "medic_move_callReinforcements" : "medic_move_attackBlaster");
}
static void attack_fn(void *ctx, qa_q2m_context *c) { attack_impl(ctx, c); }
static bool attacking(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  return strcmp(name, "medic_move_attackHyperBlaster") == 0 || strcmp(name, "medic_move_attackCable") == 0
      || strcmp(name, "medic_move_attackBlaster") == 0 || strcmp(name, "medic_move_callReinforcements") == 0;
}
static void cable(medic_ctx *box, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2_entity *target = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
  if (!target || q2rm_has(target->effects, 2)) { abort_heal(box, c, false, false, false); return; }
  if (game->host->is_player(game->host->ctx, target->actor.id)) return;
  if (qa_q2m_health(game, &target->actor.id) > 0) { abort_heal(box, c, false, false, false); return; }
  qa_vec3 start = qa_q2m_project_flash_default(c,
      q2rm_at_v(cable_offsets, 10, entity->frame - qa_q2rm_medic_frames_value.attack42));
  qa_body_state target_body = q2rm_body(game, target);
  if (qa_hypot3(qa_sub3_double(start, target_body.origin)) < 32) { abort_heal(box, c, true, true, false); return; }
  qa_trace_result trace = q2rm_trace(game, start, target_body.origin, NULL, &entity->actor.id, 3);
  if (trace.fraction != 1 && (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, target->actor.id))) {
    if (qa_q2mpm_source_trace_world(game, trace)) {
      if (qa_q2mpm_state_get(box->source, entity)->medic_tries > 1) { abort_heal(box, c, true, false, true); return; }
      qa_q2mpm_state_get(box->source, entity)->medic_tries++;
      cleanup(box, c, true);
      return;
    }
    abort_heal(box, c, true, false, false);
    return;
  }
  const qa_q2rm_medic_frames *frame = &qa_q2rm_medic_frames_value;
  if (entity->frame == frame->attack43) {
    q2rm_sound(game, target, qa_q2mpm_monster_mass(c) == 400 ? "medic/medatck3.wav" : "medic_commander/medatck3a.wav",
               true, 0, false, 0, false, 0);
    qa_q2m_context *patient = qa_q2m_monsters_context(box->monsters, target->actor.id);
    if (patient) {
      patient->state->resurrecting = true;
      patient->state->can_take_damage = false;
      qa_q2mpm_rogue_heal_effects(patient);
    }
    q2rm_set_damageable(game, target, false);
  } else if (entity->frame == frame->attack50) {
    target->spawnflags = 0;
    target->target = "";
    target->targetname = "";
    target->combat_target = "";
    target->death_target = "";
    target->health_target = "";
    target->item_target = "";
    qa_q2m_context *previous = qa_q2m_monsters_context(box->monsters, target->actor.id);
    if (previous) {
      qa_q2m_state *flags = previous->state;
      flags->ignore_shots = false; flags->do_not_count = false; flags->good_guy = false; flags->target_anger = false;
      flags->brutal = false; flags->medic = false; flags->resurrecting = false; flags->stand_ground = false;
      flags->temporary_stand_ground = false;
      flags->hold_frame = false; flags->ducked = false; flags->dodging = false; flags->charging = false;
      flags->manual_steering = false;
      flags->combat_point = false; flags->lost_sight = false; flags->pursue_next = false;
      flags->pursue_temporary = false; flags->pursuit_last_seen = false; flags->has_sound_target = false;
    }
    qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(box->source, target);
    patient->healer = entity->actor.id;
    patient->has_healer = true;
    qa_bounds raised_bounds = target_body.bounds;
    raised_bounds.max.z += 48;
    qa_trace_result clear = q2rm_trace(game, target_body.origin, target_body.origin, &raised_bounds,
                                       &target->actor.id, qa_q2m_monster_solid_mask(game));
    if (clear.start_solid || clear.all_solid || !qa_q2mpm_source_trace_world(game, clear)) {
      abort_heal(box, c, true, true, false);
      return;
    }
    if (previous) previous->state->do_not_count = true;
    double max_health = target->max_health;
    double gib_health = previous ? previous->state->gib_health : 0;
    double slots = previous ? previous->state->monster_slots : 0;
    double used = previous ? previous->state->monster_used : 0;
    qa_q2m_spawned_by spawned_by = previous ? previous->state->spawned_by : QA_Q2M_SPAWNED_NONE;
    bool has_commander = previous && previous->state->has_commander;
    qa_actor_id commander;
    memset(&commander, 0, sizeof commander);
    if (has_commander) commander = previous->state->commander;
    qa_q2m_power_armor armor_type = previous ? previous->state->initial_power_armor_type : QA_Q2M_ARMOR_NONE;
    double max_power = previous ? previous->state->max_power_armor_power : 0;
    double base_health = previous ? previous->state->base_health : max_health;
    double scaling = previous ? previous->state->health_scaling : 1;
    qa_q2m_context *revived = qa_q2m_monsters_respawn(box->monsters, target, game);
    if (armor_type == QA_Q2M_ARMOR_NONE) {
      qa_armor_state armor;
      memset(&armor, 0, sizeof armor);
      armor.regular.kind = QA_REGULAR_ARMOR_NONE;
      armor.powered.kind = "none";
      qa_gameplay_authority_set_armor(game->host->combat, target->actor, armor);
      size_t inventory_count = 0;
      (void)qa_shared_inventory_table_entries(game->host->inventory, target->actor.id, &inventory_count);
      bool has_inventory = false;
      /* TS inventory.has: the qa_q2f mirror entries() count stands in. */
      has_inventory = inventory_count > 0 || qa_shared_inventory_table_count(game->host->inventory, target->actor.id, "q2:monster-power") != 0;
      if (has_inventory) {
        qa_inventory_entry entry;
        memset(&entry, 0, sizeof entry);
        entry.item = "q2:monster-power";
        entry.count = max_power;
        entry.capacity = max_power;
        qa_shared_inventory_table_configure(game->host->inventory, target->actor, entry, NULL, NULL);
      }
    } else qa_q2mpm_monster_power_armor(revived, armor_type == QA_Q2M_ARMOR_SHIELD ? "shield" : "screen", max_power);
    revived->state->initial_power_armor_type = armor_type;
    revived->state->max_power_armor_power = max_power;
    revived->state->base_health = base_health;
    revived->state->health_scaling = scaling;
    target->max_health = max_health;
    qa_gameplay_authority_set_health(game->host->combat, target->actor, max_health);
    revived->state->gib_health = trunc(gib_health / 2);
    revived->state->monster_slots = slots;
    revived->state->monster_used = used;
    revived->state->spawned_by = spawned_by;
    revived->state->has_commander = has_commander;
    if (has_commander) revived->state->commander = commander;
    if (target->think.fn) {
      target->has_next_think = true;
      target->next_think = q2rm_now(game);
      target->think.fn(target->think.ctx, target, game);
    }
    revived->state->resurrecting = false;
    revived->state->ignore_shots = true;
    revived->state->do_not_count = true;
    target->effects = q2rm_and(target->effects, ~0x4000);
    qa_q2mpm_state_get(box->source, target)->has_healer = false;
    if (state->has_old_enemy && q2rm_live(game, state->old_enemy) && qa_q2m_health(game, &state->old_enemy) > 0) {
      target->enemy = state->old_enemy;
      target->has_enemy = true;
      qa_q2m_monsters_found_target(box->monsters, revived);
    } else {
      target->has_enemy = false;
      if (!revived->find_target(revived->impl_ctx, revived)) {
        revived->state->pause_time = q2rm_now(game) + 100000000;
        revived->stand(revived->impl_ctx, revived);
      }
      entity->has_enemy = false;
      state->has_old_enemy = false;
      if (!c->find_target(c->impl_ctx, c)) {
        state->pause_time = q2rm_now(game) + 100000000;
        c->stand(c->impl_ctx, c);
        return;
      }
    }
    cleanup(box, c, false);
    return;
  } else if (entity->frame == frame->attack44) medic_sound(c, "medatck4", "medatck4a", 1, 1);
  qa_q2_entity *current_target = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
  if (!current_target) return;
  qa_body_state body = q2rm_body(game, current_target);
  qa_vec3 beam_start = qa_add3_double(start,
      qa_scale3_double(qa_math_angle_vectors_rule(q2rm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward, 8));
  qa_vec3 beam_end = body.origin;
  beam_end.z += (body.bounds.min.z + body.bounds.max.z) / 2;
  qa_q2_presentation_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam";
  beam.effect = "medic";
  beam.has_actor = true; beam.kind = "beam"; beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = beam_start;
  beam.end = beam_end;
  game->host->emit(game->host->ctx, beam);
}
static size_t reinforcement_list(medic_ctx *box, qa_q2m_context *c, reinforcement **out) {
  qa_q2rm_medic_reinforcement_choice *choices = NULL;
  size_t count = qa_q2rm_rerelease_medic_reinforcements(&c->entity->spawn, &choices);
  reinforcement *list = NULL;
  if (count) {
    list = malloc(count * sizeof *list);
    if (!list) qa_raise("Error", 0, 0, "Out of memory building medic reinforcement list");
  }
  for (size_t i = 0; i < count; i++) {
    const qa_q2m_definition *definition = qa_q2m_monsters_definition(box->monsters, choices[i].classname, c->game);
    if (!definition) qa_raise("Error", 0, 0, "Unknown medic reinforcement %s", choices[i].classname);
    list[i].classname = choices[i].classname;
    list[i].strength = choices[i].strength;
    list[i].bounds = definition->bounds;
  }
  free(choices);
  *out = list;
  return count;
}
static void free_reinforcement_list(reinforcement *list, size_t count) {
  for (size_t i = 0; i < count; i++) free(list[i].classname);
  free(list);
}
typedef bool (*spawn_visitor)(void *ctx, qa_vec3 point, const reinforcement *entry);
static void each_spawn(medic_ctx *box, qa_q2m_context *c, bool behind, void *visit_ctx, spawn_visitor visit, bool determine) {
  reinforcement *list = NULL;
  size_t list_count = reinforcement_list(box, c, &list);
  qa_q2mpm_rogue_state *private_state = qa_q2mpm_state_get(box->source, c->entity);
  for (size_t i = 0; i < private_state->reinforcement_count; i++) {
    const reinforcement *entry = &list[qa_q2m_record_at(list_count,
        private_state->chosen_reinforcements[qa_q2m_record_at(private_state->reinforcement_count, (double)i)])];
    qa_vec3 position = q2rm_at_v(reinforcement_positions, 5, (double)i);
    if (determine) position = qa_scale3_double(position, c->entity->scale);
    if (behind) { position.x = -position.x; position.y = -position.y; }
    qa_vec3 point = qa_q2m_project_flash_default(c, position);
    qa_vec3 raised = point;
    raised.z += behind ? 10 : 10 * c->entity->scale;
    qa_vec3 spawn;
    if (qa_q2rm_find_rerelease_spawn_point_default(c->game, raised, entry->bounds, 32, &spawn)
        && visit(visit_ctx, spawn, entry)) break;
  }
  free_reinforcement_list(list, list_count);
}
/* pickRogueCoopTarget (TS owner: src/content/q2/missionpacks/
 * monsters/medic.ts) is qa_q2mpm_pick_rogue_coop_target. */

static void dead_cb_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -24);
  change.bounds.max = q2rm_v3(16, 16, -8);
  q2rm_move_body(c->game, c->entity, change, false, false);
  qa_q2m_corpse(c);
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  bounds.max.z = -2;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void quick_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_rnd(c->game) < 0.5) {
    q2rm_set_move_imm(c, "medic_move_attackHyperBlaster", false);
    c->state->next_frame = qa_q2rm_medic_frames_value.attack16;
  }
}
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void hook_launch_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  medic_sound(c, "medatck2", "medatck2c", 1, 1);
}
static void hook_retract_fn(void *ctx, qa_q2m_context *c) {
  q2rm_snd(c, "medic/medatck5.wav", 1);
  c->state->medic = false;
  (void)restore_enemy(ctx, c);
}
static void cable_fn(void *ctx, qa_q2m_context *c) { cable(ctx, c); }
static void continue_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) <= 0.95)
    q2rm_set_move_imm(c, "medic_move_attackHyperBlaster", false);
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  medic_ctx *box = ctx;
  double frame = c->entity->frame;
  bool commander = qa_q2mpm_monster_mass(c) > 400;
  const qa_q2rm_medic_frames *f = &qa_q2rm_medic_frames_value;
  bool blaster = frame == f->attack9 || frame == f->attack12;
  double flash = blaster ? (commander ? 146 : 60) : (commander ? 277 : 265) + frame - f->attack19;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  double effects = blaster ? 8 : fmod(frame, 4) == 0 ? 64 : 0;
  qa_q2_entity *enemy_entity = c->game->entity(c->game->ctx, c->entity->has_enemy ? &c->entity->enemy : NULL);
  double damage = enemy_entity && strcmp(enemy_entity->classname, "tesla_mine") == 0 ? 3 : blaster ? 6 : 2;
  if (commander)
    (void)box->weapons.fire_blaster2(box->weapons.ctx, c->entity, c->game, aim.start, aim.direction, damage, 1000, effects);
  else
    (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, damage, 1000, effects, false, false, 0);
  qa_q2rm_monster_flash(c, flash, aim.start, aim.direction);
}
static void start_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "medic_commander/monsterspawn1.wav", 1);
  c->state->next_frame = qa_q2rm_medic_frames_value.attack48;
}
typedef struct spawn_check { medic_ctx *box; qa_q2m_context *context; bool success; } spawn_check;
static bool check_visit(void *ctx, qa_vec3 point, const reinforcement *entry) {
  spawn_check *check = ctx;
  check->success = qa_q2rm_check_rerelease_ground_spawn_point(check->context->game, point, entry->bounds, 256, -1);
  return check->success;
}
static bool grow_visit(void *ctx, qa_vec3 point, const reinforcement *entry) {
  spawn_check *check = ctx;
  qa_q2m_context *c = check->context;
  if (qa_q2rm_check_rerelease_ground_spawn_point(c->game, point, entry->bounds, 256, -1)) {
    check->success = true;
    qa_vec3 origin = qa_add3_double(point, qa_add3_double(entry->bounds.min, entry->bounds.max));
    double radius = qa_hypot3(qa_sub3_double(entry->bounds.max, entry->bounds.min)) * 0.5;
    spawn_grow(c, origin, radius);
  }
  return false;
}
typedef struct finish_ctx { medic_ctx *box; qa_q2m_context *context; } finish_ctx;
static bool finish_visit(void *ctx, qa_vec3 point, const reinforcement *entry) {
  finish_ctx *finish = ctx;
  medic_ctx *box = finish->box;
  qa_q2m_context *c = finish->context;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_bounds bounds = entry->bounds;
  if (!qa_q2mpm_check_rogue_spawn_point(game, point, bounds)) return false;
  qa_q2_entity *child = qa_q2rm_check_rerelease_ground_spawn_point(game, point, bounds, 256, -1)
      ? qa_q2mpm_create_rogue_monster(box->monsters, game, point, q2rm_body(game, entity).angles, entry->classname)
      : NULL;
  if (!child) return false;
  if (child->think.fn) {
    child->has_next_think = true;
    child->next_think = q2rm_now(game);
    child->think.fn(child->think.ctx, child, game);
  }
  qa_q2m_context *child_context = qa_q2m_monsters_context(box->monsters, child->actor.id);
  if (!child_context) qa_raise("Error", 0, 0, "Medic reinforcement has no shared controller");
  child_context->state->ignore_shots = true;
  child_context->state->do_not_count = true;
  child_context->state->spawned_by = QA_Q2M_SPAWNED_MEDIC;
  child_context->state->has_commander = true;
  child_context->state->commander = entity->actor.id;
  child_context->state->monster_slots = entry->strength;
  state->monster_used += entry->strength;
  bool has_enemy;
  qa_actor_id enemy;
  memset(&enemy, 0, sizeof enemy);
  if (state->medic) { has_enemy = state->has_old_enemy; if (has_enemy) enemy = state->old_enemy; }
  else { has_enemy = entity->has_enemy; if (has_enemy) enemy = entity->enemy; }
  if (game->options.mode == QA_Q2_MODE_COOP) {
    has_enemy = qa_q2mpm_pick_rogue_coop_target(child_context, &enemy);
    if (has_enemy && entity->has_enemy && qa_same_actor(enemy, entity->enemy))
      has_enemy = qa_q2mpm_pick_rogue_coop_target(child_context, &enemy);
    if (!has_enemy && entity->has_enemy) { enemy = entity->enemy; has_enemy = true; }
  }
  if (has_enemy && q2rm_live(game, enemy) && qa_q2m_health(game, &enemy) > 0) {
    child->enemy = enemy;
    child->has_enemy = true;
    qa_q2m_monsters_found_target(box->monsters, child_context);
  } else {
    child->has_enemy = false;
    child_context->stand(child_context->impl_ctx, child_context);
  }
  return false;
}
static void determine_spawn_fn(void *ctx, qa_q2m_context *c) {
  medic_ctx *box = ctx;
  reinforcement *list = NULL;
  size_t list_count = reinforcement_list(box, c, &list);
  double count = qa_js_max(1, trunc(log2(q2rm_rnd(c->game) * 32)));
  double *chosen = NULL;
  size_t chosen_count = 0, chosen_cap = 0;
  double remaining = c->state->monster_slots - c->state->monster_used;
  for (double i = 0; i < count && remaining != 0; i++) {
    size_t available[64];
    size_t available_count = 0;
    for (size_t j = 0; j < list_count; j++)
      if (list[j].strength <= remaining && available_count < 64) available[available_count++] = j;
    if (available_count == 0) break;
    int32_t picked = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), (int64_t)available_count);
    size_t index = available[qa_q2m_record_at(available_count, (double)picked)];
    if (chosen_count == chosen_cap) {
      chosen_cap = chosen_cap ? chosen_cap * 2 : 4;
      chosen = realloc(chosen, chosen_cap * sizeof *chosen);
      if (!chosen) qa_raise("Error", 0, 0, "Out of memory choosing medic reinforcements");
    }
    chosen[chosen_count++] = (double)index;
    remaining -= list[index].strength;
  }
  qa_q2mpm_rogue_state *private_state = qa_q2mpm_state_get(box->source, c->entity);
  free(private_state->chosen_reinforcements);
  private_state->chosen_reinforcements = chosen;
  private_state->reinforcement_count = chosen_count;
  private_state->reinforcement_cap = chosen_cap;
  free_reinforcement_list(list, list_count);
  spawn_check check = {box, c, false};
  each_spawn(box, c, false, &check, check_visit, true);
  if (!check.success) {
    each_spawn(box, c, true, &check, check_visit, true);
    if (check.success) {
      c->state->manual_steering = true;
      c->state->ideal_yaw = qa_angle_mod_scaled(q2rm_body(c->game, c->entity).angles.y) + 180;
      if (c->state->ideal_yaw > 360) c->state->ideal_yaw -= 360;
    }
  }
  if (!check.success) c->state->next_frame = qa_q2rm_medic_frames_value.attack53;
}
static void spawngrows_fn(void *ctx, qa_q2m_context *c) {
  medic_ctx *box = ctx;
  if (c->state->manual_steering) {
    if (fabs(qa_angle_mod_scaled(q2rm_body(c->game, c->entity).angles.y) - c->state->ideal_yaw) > 0.1) {
      c->state->hold_frame = true;
      return;
    }
    c->state->hold_frame = false;
    c->state->manual_steering = false;
  }
  spawn_check check = {box, c, false};
  each_spawn(box, c, false, &check, grow_visit, false);
  if (!check.success) c->state->next_frame = qa_q2rm_medic_frames_value.attack53;
}
static void finish_spawn_fn(void *ctx, qa_q2m_context *c) {
  medic_ctx *box = ctx;
  finish_ctx finish = {box, c};
  each_spawn(box, c, false, &finish, finish_visit, false);
}
static void sight_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  medic_sound(c, "medsght1", "medsght", 2, 1);
}
static void search_fn(void *ctx, qa_q2m_context *c) {
  medic_sound(c, "medsrch1", "medsrch", 2, 2);
  if (!c->state->has_old_enemy) (void)acquire(ctx, c);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  medic_ctx *box = ctx;
  c->state->ignore_shots = true;
  if (qa_q2mpm_monster_mass(c) > 400) {
    c->entity->skin = 2;
    double slots = qa_q2_number_field(&c->entity->spawn, "monster_slots", 3);
    c->state->monster_slots = slots;
    reinforcement *list = NULL;
    size_t list_count = reinforcement_list(box, c, &list);
    if (slots != 0 && list_count != 0)
      c->state->monster_slots += floor(slots * c->game->options.skill / 2);
    free_reinforcement_list(list, list_count);
  }
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  medic_ctx *box = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  bool commander = qa_q2mpm_monster_mass(c) > 400;
  qa_q2m_finish_dodge(c);
  entity->skin = q2rm_or(q2rm_and(entity->skin, ~1),
                         qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0);
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  double random = q2rm_rnd(game);
  if (commander) {
    if (reaction.damage < 35) {
      medic_sound(c, "medpain1", "medpain1", 2, 1);
      if (!qa_q2rm_chainfist(c)) return;
    }
    medic_sound(c, "medpain2", "medpain2", 2, 1);
  } else medic_sound(c, random < 0.5 ? "medpain1" : "medpain2", "medpain2", 2, 1);
  if (!qa_q2rm_reacts_to_pain(c) || (!qa_q2rm_chainfist(c) && state->medic)) return;
  if (commander) {
    state->manual_steering = false;
    state->hold_frame = false;
  }
  q2rm_set_move(c, commander
      ? (random < qa_js_min(reaction.damage * 0.005, 0.5) ? "medic_move_pain2" : "medic_move_pain1")
      : (random < 0.5 ? "medic_move_pain1" : "medic_move_pain2"));
  if (state->ducked) qa_q2m_set_duck(c, false);
  abort_heal(box, c, false, false, false);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  medic_ctx *box = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (state->medic) {
    qa_q2_entity *target = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
    if (target) qa_q2mpm_cleanup_rogue_heal_target(box->monsters, box->source, target, game);
    state->medic = false;
  }
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = trunc(entity->skin / 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options metal = {false, true, false, false};
    qa_q2m_gib_options skinned = {false, false, true, false};
    qa_q2m_gib_options skin_up = {false, false, true, true};
    qa_q2m_gib_options head = {true, false, true, false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", damage, metal);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/medic/gibs/chest.md2", damage, skinned);
    static const char *const parts[4] = {
      "models/monsters/medic/gibs/leg.md2", "models/monsters/medic/gibs/leg.md2",
      "models/monsters/medic/gibs/hook.md2", "models/monsters/medic/gibs/gun.md2"};
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, parts[i], damage, skin_up);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/medic/gibs/head.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  medic_sound(c, "meddeth1", "meddeth", 2, 1);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "medic_move_death");
}
static bool duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  if (c->state->medic) return false;
  if (attacking(c)) { qa_q2m_set_duck(c, false); return false; }
  q2rm_set_move(c, "medic_move_duck");
  return true;
}
static bool sidestep_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (attacking(c)) return false;
  if (strcmp(c->state->move->name, "medic_move_run") != 0) q2rm_set_move(c, "medic_move_run");
  return true;
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  medic_ctx *box = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (state->medic) {
    if (!entity->has_enemy || !q2rm_live(game, entity->enemy)) { abort_heal(box, c, true, false, false); return false; }
    if (entity->timestamp < q2rm_now(game)) {
      abort_heal(box, c, true, false, true);
      entity->timestamp = 0;
      return false;
    }
    if (qa_q2m_target_distance(c) < 410) { attack_impl(box, c); return true; }
    state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    return false;
  }
  if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy) && !qa_q2m_visible_enemy(c)
      && state->monster_slots > state->monster_used) {
    state->attack_state = QA_Q2M_ATTACK_BLIND;
    return true;
  }
  if (state->monster_slots != 0 && q2rm_rnd(game) < 0.8
      && state->monster_slots - state->monster_used > state->monster_slots * 0.8
      && qa_q2m_target_distance(c) > 150) {
    qa_q2mpm_state_get(box->source, entity)->blocked = true;
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  if (state->stand_ground) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  return qa_q2m_default_check_attack(c, NULL);
}

size_t qa_q2rm_create_rerelease_medic_definitions(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                  qa_q2mpm_monster_state *source, qa_q2m_definition out[2]) {
  medic_ctx *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  qa_q2m_named_handler *callbacks = malloc(14 * sizeof *callbacks);
  qa_q2_named_think *think_defs = qa_q2mpm_arena_alloc(arena, 2 * sizeof *think_defs);
  if (!box || !callbacks || !think_defs) qa_raise("Error", 0, 0, "Out of memory creating rerelease medic definitions");
  box->monsters = monsters;
  box->weapons = weapons;
  box->source = source;
  callbacks[0] = (qa_q2m_named_handler){"medic_idle", {box, idle_fn}};
  callbacks[1] = (qa_q2m_named_handler){"medic_run", {box, run_fn}};
  callbacks[2] = (qa_q2m_named_handler){"medic_dead", {NULL, dead_cb_fn}};
  callbacks[3] = (qa_q2m_named_handler){"medic_shrink", {NULL, shrink_fn}};
  callbacks[4] = (qa_q2m_named_handler){"medic_quick_attack", {NULL, quick_attack_fn}};
  callbacks[5] = (qa_q2m_named_handler){"monster_done_dodge", {NULL, done_dodge_fn}};
  callbacks[6] = (qa_q2m_named_handler){"medic_hook_launch", {NULL, hook_launch_fn}};
  callbacks[7] = (qa_q2m_named_handler){"medic_hook_retract", {box, hook_retract_fn}};
  callbacks[8] = (qa_q2m_named_handler){"medic_cable_attack", {box, cable_fn}};
  callbacks[9] = (qa_q2m_named_handler){"medic_continue", {NULL, continue_fn}};
  callbacks[10] = (qa_q2m_named_handler){"medic_fire_blaster", {box, fire_blaster_fn}};
  callbacks[11] = (qa_q2m_named_handler){"medic_start_spawn", {NULL, start_spawn_fn}};
  callbacks[12] = (qa_q2m_named_handler){"medic_determine_spawn", {box, determine_spawn_fn}};
  callbacks[13] = (qa_q2m_named_handler){"medic_spawngrows", {box, spawngrows_fn}};
  const qa_q2m_named_handler extra[1] = {{"medic_finish_spawn", {box, finish_spawn_fn}}};
  size_t callback_count = 0;
  qa_q2m_named_handler *merged = qa_q2rm_merge_callbacks(arena, callbacks, 14, extra, 1, &callback_count);
  free(callbacks);
  think_defs[0].name = "rerelease.medic.spawngrow_think";
  think_defs[0].cb = (qa_q2_think){NULL, spawn_grow_think_fn};
  think_defs[1].name = "rerelease.medic.SpawnGro_laser_think";
  think_defs[1].cb = (qa_q2_think){NULL, spawn_grow_laser_think_fn};
  qa_q2m_definition base;
  memset(&base, 0, sizeof base);
  base.classname = "monster_medic";
  base.kind = "medic";
  base.model = "models/monsters/medic/tris.md2";
  base.health = 300; base.gib_health = -130; base.mass = 400;
  base.bounds.min = q2rm_v3(-24, -24, -24);
  base.bounds.max = q2rm_v3(24, 24, 32);
  base.scale = 1;
  base.initial_move = "medic_move_stand";
  base.moves = qa_q2rm_medic_moves;
  base.move_count = qa_q2rm_medic_move_count;
  base.stand = qa_q2bm_move_handler("medic_move_stand");
  base.walk = qa_q2bm_move_handler("medic_move_walk");
  base.run = (qa_q2m_handler){box, run_fn};
  base.attack = (qa_q2m_handler){box, attack_fn};
  base.idle = (qa_q2m_handler){box, idle_fn};
  base.callbacks = merged;
  base.callback_count = callback_count;
  base.has_source_callbacks = true;
  base.source_callbacks.think = think_defs;
  base.source_callbacks.think_count = 2;
  base.sight = (qa_q2m_handler){NULL, sight_fn};
  base.search = (qa_q2m_handler){box, search_fn};
  base.initialize = (qa_q2m_handler){box, init_fn};
  base.reactions.ctx = box;
  base.reactions.pain = pain_fn;
  base.reactions.die = die_fn;
  base.duck_ctx = NULL; base.duck = duck_fn;
  base.sidestep_ctx = NULL; base.sidestep = sidestep_fn;
  base.blocked_ctx = NULL; base.blocked = qa_q2rm_blocked_check_platform_handler;
  base.check_attack_ctx = box; base.check_attack = check_attack_fn;
  out[0] = base;
  out[1] = base;
  out[1].classname = "monster_medic_commander";
  out[1].kind = "medic_commander";
  out[1].health = 600;
  out[1].mass = 600;
  out[1].has_yaw_speed = true;
  out[1].yaw_speed = 40;
  return 2;
}
