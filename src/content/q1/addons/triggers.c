/* Port of src/content/q1/addons/triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/triggers.h"

#include <stdio.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/foundation/support.h"

static void prefix_of(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:trigger:", qa_q1ad_program_name(context->program));
}
static void schedule(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name, double delay) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_schedule_named(context, entity, delay, prefix, name);
}
static void targets(qa_q1ad_context *context, qa_q1ad_actor *entity, const qa_actor_id *activator) {
  if (activator != NULL) { entity->activator = *activator; entity->has_activator = true; }
  else entity->has_activator = false;
  qa_q1ad_use_targets(context, entity, activator);
}
static void targets_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  targets(context, entity, entity->has_activator ? &entity->activator : NULL);
}
static double read_flags(qa_q1ad_context *context) {
  return context->game.campaign_read_flags != NULL
      ? context->game.campaign_read_flags(context->game.ctx) : 0;
}
static void counter_message(qa_q1ad_context *context, qa_q1ad_actor *entity,
                            const qa_actor_id *activator, bool sacrifice, bool force) {
  if (!qa_q1ad_is_player(context, activator) || (!force && ((int32_t)entity->spawnflags & 1) != 0)) return;
  double count = entity->count;
  char buffer[64];
  const char *text;
  if (sacrifice) {
    if (count == 0) text = "$mg3_qc_sacricie_count_complete";
    else if (count >= 1 && count <= 8) {
      (void)snprintf(buffer, sizeof buffer, "$mg3_qc_sacricie_count_%d_more", (int)count);
      text = buffer;
    } else text = "$mg3_qc_sacricie_count_more";
  } else
    text = count == 0 ? "$qc_sequence_completed" : count == 1 ? "$qc_one_more"
        : count == 2 ? "$qc_two_more" : count == 3 ? "$qc_three_more" : "$qc_more_go";
  qa_q1ad_broadcast(context, text);
}
static void counter_reset(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)context;
  entity->count = qa_q1ad_actor_number(entity, "cnt");
}
static void counter_use_impl(qa_q1ad_context *context, qa_q1ad_actor *entity,
                             const qa_actor_id *activator, bool timed, bool sacrifice) {
  entity->count = (double)(float)(entity->count - 1);
  if (entity->count < 0) return;
  if (timed) schedule(context, entity, "counter_reset", entity->delay);
  counter_message(context, entity, activator, sacrifice, false);
  if (entity->count != 0) return;
  if (timed) { entity->delay = 0; qa_q1ad_cancel(context, entity); }
  else if (!sacrifice && ((int32_t)entity->spawnflags & 2) != 0) entity->count = entity->wait;
  qa_q1ad_do_use(context, entity, "multi_use", NULL, activator);
  if (timed) qa_q1ad_remove(context, entity);
}
static void counter_use(qa_q1ad_context *c, qa_q1ad_actor *e, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; counter_use_impl(c, e, a, false, false);
}
static void counter_timed_use(qa_q1ad_context *c, qa_q1ad_actor *e, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; counter_use_impl(c, e, a, true, false);
}
static void sacrifice_counter_use(qa_q1ad_context *c, qa_q1ad_actor *e, const qa_actor_id *o, const qa_actor_id *a) {
  (void)o; counter_use_impl(c, e, a, false, true);
}
static void spawn_counter_timed(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  entity->wait = -1;
  if (entity->count == 0) entity->count = 2;
  if (entity->delay == 0) entity->delay = 2;
  qa_q1ad_set_number(context, entity, "cnt", entity->count);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "counter_timed");
  qa_q1ad_bind_prefix(entity->think, prefix, "counter_reset");
}
static void spawn_counter(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->count == 0) entity->count = 2;
  if (entity->wait == 0) entity->wait = entity->count;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "counter");
}
static void repeater_tick(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  targets(context, entity, entity->has_activator ? &entity->activator : NULL);
  if (!qa_q1ad_live(context, entity)) return;
  schedule(context, entity, "repeater_tick",
           entity->wait + qa_q1ad_actor_number(entity, "pausetime") * qa_q1ad_random(context));
}
static void repeater_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  if (activator != NULL) { entity->activator = *activator; entity->has_activator = true; }
  else entity->has_activator = false;
  entity->spawnflags = (double)((int32_t)entity->spawnflags ^ 1);
  if (((int32_t)entity->spawnflags & 1) == 0) qa_q1ad_cancel(context, entity);
  else schedule(context, entity, "repeater_tick",
                entity->wait + qa_q1ad_actor_number(entity, "pausetime") * qa_q1ad_random(context));
}
static void multitouch_empty(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->wait = 0;
  if (((int32_t)entity->spawnflags & 32) == 0)
    targets(context, entity, entity->has_activator ? &entity->activator : NULL);
}
static void multitouch_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                             const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0) return;
  if (entity->wait == 0) {
    entity->wait = 1;
    if (((int32_t)entity->spawnflags & 16) == 0) targets(context, entity, other);
  }
  if (qa_q1ad_live(context, entity)) schedule(context, entity, "multitouch_empty", 0.2);
}
static void spawn_multitouch(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_init_trigger(context, entity);
  entity->wait = 0;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "multitouch_touch");
}
static void explosion_action(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->delay = 0;
  targets(context, entity, entity->has_activator ? &entity->activator : NULL);
  if (!qa_q1ad_live(context, entity)) return;
  qa_actor_id self = entity->actor.id;
  if (((int32_t)entity->spawnflags & 1) == 0 && context->game.radius_damage != NULL)
    context->game.radius_damage(context->game.ctx, self,
        entity->has_owner ? &entity->owner : NULL, 120, &self, NULL);
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  if (context->program == QA_Q1AD_MG3 && ((int32_t)entity->spawnflags & 2) != 0) {
    qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_COLORED_EXPLOSION, .origin = body.origin,
                           .color_start = 244, .color_length = 3};
    qa_q1ad_emit(context, &event);
  } else qa_q1ad_effect(context, "explosion", body.origin, NULL, 0);
  qa_q1ad_remove(context, entity);
}
static void explosion_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  if (activator != NULL) { entity->activator = *activator; entity->has_activator = true; }
  else entity->has_activator = false;
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_named_name(full, prefix, "explosion");
  if (entity->delay > 0) qa_q1ad_schedule(context, entity, entity->delay, full);
  else qa_q1ad_do_action(context, entity, full);
}
static void change_target(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  const char *target = entity->target != NULL ? entity->target : "";
  for (size_t i = 0; i < count; i++)
    if (entities[i]->target != NULL && strcmp(entities[i]->target, target) == 0)
      entities[i]->target = entity->killtarget;
}
static void cleanup_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                        const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->has_monster && qa_q1ad_health(context, entities[i]->actor.id) <= 0)
      qa_q1ad_remove(context, entities[i]);
  qa_q1ad_remove(context, entity);
}
static void spawn_cleanup(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (!context->game.coop) { qa_q1ad_remove(context, entity); return; }
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "cleanup");
}
/* spawnUse helper: removedOutsideCoop gate, optional init, bind use. */
static void spawn_use_common(qa_q1ad_context *context, qa_q1ad_actor *entity, const char *name) {
  if (qa_q1ad_live(context, entity)) {
    char prefix[QA_Q1AD_NAME];
    prefix_of(context, prefix);
    qa_q1ad_bind_prefix(entity->use, prefix, name);
  }
}
static void spawn_repeater(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->wait == 0) entity->wait = 1;
  if (((int32_t)entity->spawnflags & 1) != 0)
    schedule(context, entity, "repeater_tick",
             entity->wait + qa_q1ad_actor_number(entity, "pausetime") * qa_q1ad_random(context));
  spawn_use_common(context, entity, "repeater_use");
}
static void spawn_explosion(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  spawn_use_common(context, entity, "explosion");
}
static void spawn_changetarget(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  const char *killtarget = entity->killtarget != NULL ? entity->killtarget : "";
  if (entity->target == NULL || entity->target[0] == '\0' || killtarget[0] == '\0')
    qa_range_error("trigger_changetarget requires target and killtarget");
  spawn_use_common(context, entity, "change_target");
}

/* --- mg3-only triggers --- */
static void spawn_always(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (!qa_q1ad_removed_outside_coop(context, entity, true)) schedule(context, entity, "targets", 0.1);
}
static void rune_relay(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  int32_t required = (int32_t)entity->spawnflags & 15;
  if (((int32_t)read_flags(context) & required) != required) return;
  if (activator != NULL) { entity->activator = *activator; entity->has_activator = true; }
  else entity->has_activator = false;
  schedule(context, entity, "targets", 0.1);
}
static void rune_counter(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  if (qa_q1ad_mg3_rune_count(read_flags(context)) >= entity->count) targets(context, entity, activator);
}
static void bn_relay(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  int32_t flags = (int32_t)read_flags(context);
  static const int CHECKS[3][3] = {
    {1, 8, QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE}, {2, 16, QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME},
    {4, 32, QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED}};
  int32_t spawnflags = (int32_t)entity->spawnflags;
  for (int i = 0; i < 3; i++)
    if (((spawnflags & CHECKS[i][0]) != 0 && (flags & CHECKS[i][2]) == 0) ||
        ((spawnflags & CHECKS[i][1]) != 0 && (flags & CHECKS[i][2]) != 0)) return;
  targets(context, entity, activator);
}
static void sacrifice_empty(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  (void)context;
  entity->wait = 0;
}
static void sacrifice_check(qa_q1ad_context *context, qa_q1ad_actor *entity,
                            const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other) || qa_q1ad_health(context, *other) <= 0) return;
  if (entity->wait == 0) {
    entity->wait = 1;
    qa_q1ad_actor *found[8];
    size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", found, 8);
    if (count > 0 && found[0]->count > 0) counter_message(context, found[0], other, true, true);
  }
  schedule(context, entity, "sacrifice_empty", 0.2);
}
static void spawn_check_sacrifices(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_init_trigger(context, entity);
  entity->wait = 0;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "sacrifice_check");
}
static bool is_top(const qa_q1ad_actor *entity) {
  return entity->state != NULL && (strcmp(entity->state, "top") == 0 || strcmp(entity->state, "up") == 0);
}
static void door_relay(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_actor *found[64];
  size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", found, 64);
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *target = found[i];
    bool top = is_top(target);
    if (((int32_t)entity->spawnflags & (top ? 1 : 2)) != 0) continue;
    if (target->classname != NULL && strcmp(target->classname, "func_door") == 0) {
      if (top) { if (context->game.door_down != NULL) context->game.door_down(context->game.ctx, target); }
      else if (context->game.door_up != NULL) context->game.door_up(context->game.ctx, target);
    } else if (target->classname != NULL && strcmp(target->classname, "func_button") == 0) {
      if (top) qa_q1ad_do_action(context, target, "button_return");
      else qa_q1ad_do_use(context, target, "button_use", NULL, activator);
    }
  }
}
static void door_group(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  const char *category = qa_q1ad_actor_text(entity, "category");
  for (size_t i = 0; i < count; i++)
    if (strcmp(qa_q1ad_actor_text(entities[i], "category"), category) == 0) {
      bool top = is_top(entities[i]);
      double goal = qa_q1ad_actor_number(entities[i], "goal_state");
      if ((!top && goal == 0) || (top && goal == 1)) return;
    }
  targets(context, entity, activator);
}
static void lore_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other) ||
      context->game.time <= qa_q1ad_player_number(context, *other, "lore_active")) return;
  qa_q1ad_set_player_number(context, *other, "lore_active", context->game.time + 0.5);
  qa_q1ad_message(context, *other, entity->message != NULL ? entity->message : "", false, NULL, 0);
}
static void spawn_lore(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  int32_t flags = (int32_t)read_flags(context);
  const char *map = context->game.map_name != NULL ? context->game.map_name : "";
  const char *message = entity->message != NULL ? entity->message : "";
  if (strcmp(map, "hub") == 0) {
    static const struct { int bit; const char *from, *to; } HINTS[] = {
      {1, "$mg3_hub_rune1_hint", "$mg3_hub_rune1_hint_complete"},
      {2, "$mg3_hub_rune2_hint", "$mg3_hub_rune2_hint_complete"},
      {4, "$mg3_hub_rune3_hint", "$mg3_hub_rune3_hint_complete"}};
    for (int i = 0; i < 3; i++)
      if ((flags & HINTS[i].bit) != 0 && strcmp(message, HINTS[i].from) == 0) entity->message = HINTS[i].to;
  }
  if (strcmp(map, "map4") == 0 && strcmp(message, "$mg3_hint_bloody_nightmare_new") == 0 &&
      (flags & 448) != 0) { qa_q1ad_remove(context, entity); return; }
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "lore");
}
static void music_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                      const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  double track = qa_q1ad_actor_number(entity, "style");
  if (entity->sounds == track) return;
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_MUSIC, .track = track, .loop_track = track};
  qa_q1ad_emit(context, &event);
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "trigger_music") == 0)
      entities[i]->sounds = track;
}
static void heal_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL || entity->attack_finished > context->game.time) return;
  double health = qa_q1ad_health(context, *other);
  if (health > 0 && health < player->max_health && context->game.combat_set_health != NULL) {
    double next = health + entity->damage;
    context->game.combat_set_health(context->game.ctx, player->actor,
        next < player->max_health ? next : player->max_health);
  }
  entity->attack_finished = (double)(float)(context->game.time + entity->wait);
}
static void spawn_heal(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->damage == 0) entity->damage = 1;
  if (entity->wait == 0) entity->wait = 0.1;
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "heal");
}
static void quad_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_q1ad_player *player = qa_q1ad_player_state(context, *other);
  if (player == NULL) return;
  if (qa_q1ad_player_powerup(player, "quad") <= context->game.time)
    qa_q1ad_sound_actor(context, player->actor.id, "items/damage.wav", "item");
  double expires = (double)(float)(context->game.time + 0.1);
  qa_q1ad_player_set_powerup(player, "quad", expires);
  qa_q1ad_set_player_number(context, *other, "super_time", 2);
  if (context->game.powerup != NULL) context->game.powerup(context->game.ctx, player->actor, "quad", expires);
  qa_q1ad_link(context, entity);
}
static void spawn_quad(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "quad");
}
static void kill_monster(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_actor *found[64];
  size_t count = qa_q1ad_find(context, entity->target != NULL ? entity->target : "", found, 64);
  qa_actor_id self = entity->actor.id;
  for (size_t i = 0; i < count; i++) {
    qa_q1ad_actor *target = found[i];
    double health = qa_q1ad_health(context, target->actor.id);
    if (target->has_monster && health > 0) {
      const qa_actor_id *attacker = activator != NULL ? activator : &self;
      qa_q1ad_damage(context, target->actor.id, attacker, activator, health * 2, NULL, NULL, NULL);
    }
  }
}
static void silent_teleport(qa_q1ad_context *context, qa_q1ad_actor *entity,
                            const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other)) return;
  qa_owned_actor player;
  qa_q1ad_body body;
  if (!qa_q1ad_resolve_owned(context, *other, &player) || !qa_q1ad_body_by_id(context, *other, &body)) return;
  double height = qa_q1ad_actor_number(entity, "height");
  qa_vec3 destination = qa_add3(body.origin, (qa_vec3){0, 0, height + (height < 0 ? 1 : 0)});
  if (context->game.create == NULL) return;
  qa_q1ad_actor *death = context->game.create(context->game.ctx, "teledeath");
  if (death == NULL) return;
  death->owner = *other;
  death->has_owner = true;
  death->solid = "trigger";
  qa_bounds bounds = {qa_add3(body.bounds.min, (qa_vec3){-1, -1, -1}),
                      qa_add3(body.bounds.max, (qa_vec3){1, 1, 1})};
  qa_q1ad_body_patch patch = {.has_origin = true, .origin = destination,
                              .has_bounds = true, .bounds = bounds};
  qa_q1ad_set_body(context, death, &patch);
  qa_q1ad_bind(death->touch, "tdeath_touch");
  qa_q1ad_link(context, death);
  qa_q1ad_body death_body = qa_q1ad_body_of(context, death);
  qa_bounds box = {qa_add3(destination, death_body.bounds.min), qa_add3(destination, death_body.bounds.max)};
  qa_actor_id observed[256];
  size_t observed_count = context->game.observations != NULL
      ? context->game.observations(context->game.ctx, observed, 256) : 0;
  for (size_t i = 0; i < observed_count; i++) {
    qa_q1ad_body state;
    if (!qa_q1ad_body_by_id(context, observed[i], &state)) continue;
    qa_bounds other_box = {qa_add3(state.origin, state.bounds.min), qa_add3(state.origin, state.bounds.max)};
    if (qa_math_bounds_overlap(box, other_box) && context->game.host_touch != NULL)
      context->game.host_touch(context->game.ctx, death->actor, observed[i]);
  }
  qa_q1ad_schedule(context, death, 0.2, "SUB_Remove");
  body.origin = destination;
  if (context->game.write_body != NULL) context->game.write_body(context->game.ctx, player, &body);
  if (context->game.link_body != NULL) context->game.link_body(context->game.ctx, player);
}
static void spawn_silent_teleport(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_actor_number(entity, "height") == 0) qa_q1ad_set_number(context, entity, "height", -2048);
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "silent_teleport");
}
static void cutscene_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                           const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (!qa_q1ad_is_player(context, other)) return;
  entity->touch[0] = '\0';
  qa_q1ad_actor *entities[512];
  size_t count = qa_q1ad_entities(context, entities, 512);
  qa_q1ad_actor *camera = NULL;
  for (size_t i = 0; i < count; i++)
    if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "info_intermission") == 0) { camera = entities[i]; break; }
  if (camera == NULL) qa_range_error("trigger_cutscene has no intermission camera");
  qa_vec3 position = qa_q1ad_body_of(context, camera).origin;
  qa_vec3 angles = qa_q1ad_actor_vector(camera, "mangle");
  qa_actor_id players[32];
  size_t player_count = qa_q1ad_players(context, players, 32);
  for (size_t i = 0; i < player_count; i++) {
    qa_owned_actor actor;
    qa_q1ad_body body;
    if (!qa_q1ad_resolve_owned(context, players[i], &actor) ||
        !qa_q1ad_body_by_id(context, players[i], &body)) continue;
    body.origin = position;
    body.angles = angles;
    body.velocity = (qa_vec3){0, 0, 0};
    if (context->game.write_body != NULL) context->game.write_body(context->game.ctx, actor, &body);
    if (context->game.link_body != NULL) context->game.link_body(context->game.ctx, actor);
    qa_q1ad_actor *source = qa_q1ad_entity(context, &players[i]);
    if (source != NULL) { source->solid = "none"; source->movement = "none"; source->model = ""; }
  }
  qa_q1ad_event event = {.kind = QA_Q1AD_EVENT_CUTSCENE, .camera = position, .angles = angles};
  qa_q1ad_emit(context, &event);
}
static void spawn_cutscene(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_init_trigger(context, entity);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "cutscene");
}
static void spawn_axe_button(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->max_health = 1;
  if (context->game.combat_set_health != NULL)
    context->game.combat_set_health(context->game.ctx, entity->actor, 1);
  if (context->game.spawn_button != NULL) context->game.spawn_button(context->game.ctx, entity);
}
static void set_skill_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  if (!qa_q1ad_is_player(context, activator)) return;
  const char *message = entity->message != NULL ? entity->message : "";
  int32_t flags = (int32_t)read_flags(context);
  if (strcmp(message, "4") != 0 && context->game.campaign_write_flags != NULL)
    context->game.campaign_write_flags(context->game.ctx, (double)(flags & ~QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE));
  int skill;
  if (strcmp(message, "0") == 0) skill = 0;
  else if (strcmp(message, "1") == 0) skill = 1;
  else if (strcmp(message, "2") == 0) skill = 2;
  else if (strcmp(message, "3") == 0 || strcmp(message, "4") == 0) skill = 3;
  else return;
  if (strcmp(message, "4") == 0 && context->game.campaign_write_flags != NULL)
    context->game.campaign_write_flags(context->game.ctx,
        (double)(int32_t)((int32_t)read_flags(context) | QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE));
  const char *names[4] = {"easy", "normal", "hard", "nightmare"};
  char buffer[64];
  const char *text = buffer;
  if (strcmp(message, "4") == 0) text = "$mg3_selected_bloody_nightmare";
  else (void)snprintf(buffer, sizeof buffer, "$mg3_hub_selected_%s", names[skill]);
  qa_q1ad_broadcast(context, text);
  if (context->game.campaign_set_skill != NULL) context->game.campaign_set_skill(context->game.ctx, skill);
  char value[8];
  (void)snprintf(value, sizeof value, "%d", skill);
  if (context->services.set_cvar != NULL) context->services.set_cvar(context->services.ctx, "skill", value);
}
static void spawn_setskill(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "set_skill");
}
static void health_relay(qa_q1ad_context *context, qa_q1ad_actor *entity,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  targets(context, entity, activator);
}
static void explode_repeatedly(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->delay > 0) {
    double delay = entity->delay;
    entity->delay = 0;
    schedule(context, entity, "explosion_repeater", delay);
    return;
  }
  qa_actor_id child_id;
  qa_q1ad_actor *previous = qa_q1ad_actor_ref(entity, "explosion.child", &child_id)
      ? qa_q1ad_entity(context, &child_id) : NULL;
  if (previous != NULL && previous->classname != NULL &&
      strcmp(previous->classname, "spawned_explosion") == 0) qa_q1ad_remove(context, previous);
  if (context->game.create == NULL) return;
  qa_q1ad_actor *child = context->game.create(context->game.ctx, "spawned_explosion");
  if (child == NULL) return;
  child->owner = entity->actor.id;
  child->has_owner = true;
  qa_q1ad_actor_set_ref(entity, "explosion.child", &child->actor.id);
  qa_q1ad_set_origin(context, child, qa_q1ad_body_of(context, entity).origin);
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_named_name(full, prefix, "explosion");
  qa_q1ad_do_action(context, child, full);
  if (((int32_t)entity->spawnflags & 4) != 0 && --entity->count == 0) { qa_q1ad_remove(context, entity); return; }
  schedule(context, entity, "explosion_repeater",
           entity->wait + qa_q1ad_random(context) * qa_q1ad_actor_number(entity, "pausetime"));
}
static void explosion_repeater_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                                   const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  explode_repeatedly(context, entity);
}
static void spawn_explosion_repeater(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (entity->wait == 0) entity->wait = 0.8;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "explosion_repeater");
}
static void spawn_rune_counter(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->count == 0) entity->count = 2;
  spawn_use_common(context, entity, "rune_counter");
}
static void spawn_sacrifice_counter(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->count == 0) entity->count = 2;
  if (entity->wait == 0) entity->wait = entity->count;
  spawn_use_common(context, entity, "sacrifice_counter");
}
static void spawn_bn_relay(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->use, prefix, "bn_relay");
}
static void spawn_door_relay(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->target == NULL || entity->target[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  spawn_use_common(context, entity, "door_relay");
}
static void spawn_door_group(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->target == NULL || entity->target[0] == '\0' ||
      qa_q1ad_actor_text(entity, "category")[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  spawn_use_common(context, entity, "door_group");
}
static void spawn_music(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  qa_q1ad_actor *world = qa_q1ad_world(context);
  entity->sounds = world != NULL ? world->sounds : 0;
  if (entity->targetname == NULL || entity->targetname[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  if (qa_q1ad_actor_number(entity, "style") == 0) qa_q1ad_set_number(context, entity, "style", 3);
  spawn_use_common(context, entity, "music");
}
static void spawn_kill_monster(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (entity->target == NULL || entity->target[0] == '\0' ||
      entity->targetname == NULL || entity->targetname[0] == '\0') { qa_q1ad_remove(context, entity); return; }
  spawn_use_common(context, entity, "kill_monster");
}
static void spawn_rune_relay(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  spawn_use_common(context, entity, "rune_relay");
}
static void spawn_health_relay(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (qa_q1ad_health(context, entity->actor.id) == 0 && context->game.combat_set_health != NULL)
    context->game.combat_set_health(context->game.ctx, entity->actor, 0.5);
  spawn_use_common(context, entity, "health_relay");
}

void qa_q1ad_register_triggers(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("targets", ((qa_q1ad_handlers){.action = targets_action}));
  REG("counter_reset", ((qa_q1ad_handlers){.action = counter_reset}));
  REG("counter", ((qa_q1ad_handlers){.use = counter_use}));
  REG("counter_timed", ((qa_q1ad_handlers){.use = counter_timed_use}));
  REG("sacrifice_counter", ((qa_q1ad_handlers){.use = sacrifice_counter_use}));
  REG("repeater_tick", ((qa_q1ad_handlers){.action = repeater_tick}));
  REG("repeater_use", ((qa_q1ad_handlers){.use = repeater_use}));
  REG("multitouch_empty", ((qa_q1ad_handlers){.action = multitouch_empty}));
  REG("multitouch_touch", ((qa_q1ad_handlers){.touch = multitouch_touch}));
  REG("explosion", ((qa_q1ad_handlers){.action = explosion_action, .use = explosion_use}));
  REG("change_target", ((qa_q1ad_handlers){.use = change_target}));
  REG("cleanup", ((qa_q1ad_handlers){.use = cleanup_use}));
  qa_q1ad_register_spawn(context, "trigger_counter_timed", false, spawn_counter_timed);
  qa_q1ad_register_spawn(context, "trigger_counter", false, spawn_counter);
  qa_q1ad_register_spawn(context, "trigger_repeater", false, spawn_repeater);
  qa_q1ad_register_spawn(context, "trigger_multitouch", false, spawn_multitouch);
  qa_q1ad_register_spawn(context, "trigger_explosion", false, spawn_explosion);
  qa_q1ad_register_spawn(context, "trigger_changetarget", false, spawn_changetarget);
  qa_q1ad_register_spawn(context, "trigger_cleanup_corpses", false, spawn_cleanup);
  if (context->program != QA_Q1AD_MG3) {
    qa_q1ad_register_spawn(context, "mge2m2_cleanup_corpses", false, spawn_cleanup);
    return;
  }
  REG("rune_relay", ((qa_q1ad_handlers){.use = rune_relay}));
  REG("rune_counter", ((qa_q1ad_handlers){.use = rune_counter}));
  REG("bn_relay", ((qa_q1ad_handlers){.use = bn_relay}));
  REG("sacrifice_empty", ((qa_q1ad_handlers){.action = sacrifice_empty}));
  REG("sacrifice_check", ((qa_q1ad_handlers){.touch = sacrifice_check}));
  REG("door_relay", ((qa_q1ad_handlers){.use = door_relay}));
  REG("door_group", ((qa_q1ad_handlers){.use = door_group}));
  REG("lore", ((qa_q1ad_handlers){.touch = lore_touch}));
  REG("music", ((qa_q1ad_handlers){.use = music_use}));
  REG("heal", ((qa_q1ad_handlers){.touch = heal_touch}));
  REG("quad", ((qa_q1ad_handlers){.touch = quad_touch}));
  REG("kill_monster", ((qa_q1ad_handlers){.use = kill_monster}));
  REG("silent_teleport", ((qa_q1ad_handlers){.touch = silent_teleport}));
  REG("cutscene", ((qa_q1ad_handlers){.touch = cutscene_touch}));
  REG("set_skill", ((qa_q1ad_handlers){.use = set_skill_use}));
  REG("health_relay", ((qa_q1ad_handlers){.use = health_relay}));
  REG("explosion_repeater", ((qa_q1ad_handlers){.action = explode_repeatedly, .use = explosion_repeater_use}));
#undef REG
  qa_q1ad_register_spawn(context, "trigger_always", false, spawn_always);
  qa_q1ad_register_spawn(context, "trigger_rune_relay", false, spawn_rune_relay);
  qa_q1ad_register_spawn(context, "trigger_rune_counter", false, spawn_rune_counter);
  qa_q1ad_register_spawn(context, "trigger_bloodynightmare_relay", false, spawn_bn_relay);
  qa_q1ad_register_spawn(context, "trigger_sacrifice_counter", false, spawn_sacrifice_counter);
  qa_q1ad_register_spawn(context, "trigger_check_sacrifices", false, spawn_check_sacrifices);
  qa_q1ad_register_spawn(context, "trigger_door_relay", false, spawn_door_relay);
  qa_q1ad_register_spawn(context, "trigger_doorgroup_relay", false, spawn_door_group);
  qa_q1ad_register_spawn(context, "trigger_lore", false, spawn_lore);
  qa_q1ad_register_spawn(context, "trigger_music", false, spawn_music);
  qa_q1ad_register_spawn(context, "trigger_heal", false, spawn_heal);
  qa_q1ad_register_spawn(context, "trigger_quad", false, spawn_quad);
  qa_q1ad_register_spawn(context, "trigger_relay_killmonster", false, spawn_kill_monster);
  qa_q1ad_register_spawn(context, "trigger_teleport_silent", false, spawn_silent_teleport);
  qa_q1ad_register_spawn(context, "trigger_cutscene", false, spawn_cutscene);
  qa_q1ad_register_spawn(context, "func_axe_button", false, spawn_axe_button);
  qa_q1ad_register_spawn(context, "trigger_relay_setskill", false, spawn_setskill);
  qa_q1ad_register_spawn(context, "trigger_health_relay", false, spawn_health_relay);
  qa_q1ad_register_spawn(context, "trigger_explosion_repeater", false, spawn_explosion_repeater);
}

