/* Port of src/content/q1/addons/triggers.ts (quakec_mg1/triggers.qc and
 * quakec_mg3/mg3_triggers.qc, mg3_sacrifice_triggers.qc). Copyright (C)
 * 1996-2026 id Software LLC. GPL-2.0-or-later. */
#include "qa/content/q1/addons/triggers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/foundation/movers.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

/* `${program}:trigger:` names: static literals, since slots borrow the name. */
#define NAMED(context, name) \
  ((const char *const[]){"dopa:trigger:" name, "mg1:trigger:" name, "mg3:trigger:" name, \
                         "ctf:trigger:" name}[(context)->program])

static double fr(double value) { return (double)(float)value; }
static bool falsy(double value) { return value == 0 || isnan(value); }
static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
static bool same(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static double read_flags(qa_q1ad_context *context) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  return campaign.read_flags(campaign.ctx);
}
static void write_flags(qa_q1ad_context *context, double flags) {
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  campaign.write_flags(campaign.ctx, flags);
}
static void emit_addon(qa_q1ad_context *context, qa_q1ad_event event) {
  context->services.emit(context->services.ctx, &event);
}
static void schedule(qa_q1_services *game, qa_q1_actor *entity, const char *name, double delay) {
  qa_q1_schedule(game, entity, delay, qa_q1_callback_registry_action(game->named, entity, name));
}
static void call_action(qa_q1_services *game, qa_q1_actor *entity, const char *name) {
  qa_q1_call_action(game, entity, qa_q1_callback_registry_action(game->named, entity, name));
}
static const qa_actor_id *activator_of(const qa_q1_actor *entity) {
  return entity->has_activator ? &entity->activator : NULL;
}
static void set_activator(qa_q1_actor *entity, const qa_actor_id *activator) {
  entity->has_activator = activator != NULL;
  if (activator != NULL) entity->activator = *activator;
}
/* targets(entity, activator): entity.activator = activator; useTargets. */
static void targets(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *activator) {
  qa_actor_id held;
  if (activator != NULL) { held = *activator; activator = &held; } /* activator may alias entity->activator */
  set_activator(entity, activator);
  qa_q1_use_targets(game, entity, activator);
}
/* Snapshot of game.entities.values() for loops whose body removes or creates. */
static qa_q1_actor **entities_snapshot(qa_q1_services *game, size_t *count) {
  *count = game->entity_count;
  qa_q1_actor **copy = malloc((*count != 0 ? *count : 1) * sizeof *copy);
  if (copy == NULL) qa_range_error("out of memory");
  if (*count != 0) memcpy(copy, game->entities, *count * sizeof *copy);
  return copy;
}

static void targets_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  targets(game, entity, activator_of(entity));
}

/* spawnUse(classname, name, initialize?) */
typedef void (*initialize_fn)(qa_q1ad_context *context, qa_q1_actor *entity);
typedef struct spawn_use_row { qa_q1ad_context *context; const char *name; initialize_fn initialize; } spawn_use_row;
static void spawn_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  const spawn_use_row *row = ctx;
  if (qa_q1ad_removed_outside_coop(row->context, entity, true)) return;
  if (row->initialize != NULL) row->initialize(row->context, entity);
  if (qa_q1_live(game, entity)) entity->use = qa_q1_callback_registry_use(game->named, entity, row->name);
}

/* --- counters --- */
static void counter_message(qa_q1ad_context *context, qa_q1_actor *entity, const qa_actor_id *activator, bool sacrifice,
                            bool force) {
  if ((!qa_q1_is_player(context->game, activator) && (spawnflags(entity) & 8388608) == 0) ||
      (!force && (spawnflags(entity) & 1) != 0)) return;
  double count = entity->count;
  if (sacrifice) {
    if (count == 0) { qa_q1ad_broadcast(context, "$mg3_qc_sacricie_count_complete"); return; }
    if (count >= 1 && count <= 8) {
      char number[32], text[64];
      qa_net_js_number(count, number, sizeof number); /* `${count}` */
      (void)snprintf(text, sizeof text, "$mg3_qc_sacricie_count_%s_more", number);
      qa_q1ad_broadcast(context, text);
      return;
    }
    qa_q1ad_broadcast(context, "$mg3_qc_sacricie_count_more");
    return;
  }
  qa_q1ad_broadcast(context, count == 0 ? "$qc_sequence_completed" : count == 1 ? "$qc_one_more" : count == 2 ? "$qc_two_more"
                             : count == 3 ? "$qc_three_more" : "$qc_more_go");
}
static void counter_reset(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game;
  entity->count = qa_q1_actor_number(entity, "cnt");
}
static void counter_use(qa_q1ad_context *context, qa_q1_actor *entity, const qa_actor_id *activator, bool timed, bool sacrifice) {
  qa_q1_services *game = context->game;
  entity->count = fr(entity->count - 1);
  if (entity->count < 0) return;
  if (timed) schedule(game, entity, NAMED(context, "counter_reset"), entity->delay);
  counter_message(context, entity, activator, sacrifice, false);
  if (entity->count != 0) return;
  if (timed) { entity->delay = 0; qa_q1_cancel(game, entity); }
  else if (!sacrifice && (spawnflags(entity) & 2) != 0) entity->count = entity->wait;
  qa_q1_call_use(game, entity, qa_q1_callback_registry_use(game->named, entity, "multi_use"), NULL, activator);
  if (timed) qa_q1_remove(game, entity);
}
static void counter(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)game; (void)other;
  counter_use(ctx, entity, activator, false, false);
}
static void counter_timed(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)game; (void)other;
  counter_use(ctx, entity, activator, true, false);
}
static void sacrifice_counter(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                              const qa_actor_id *activator) {
  (void)game; (void)other;
  counter_use(ctx, entity, activator, false, true);
}
static void spawn_counter_timed(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  entity->wait = -1;
  if (falsy(entity->count)) entity->count = 2;
  if (falsy(entity->delay)) entity->delay = 2;
  qa_q1ad_set_number(context, entity, "cnt", entity->count);
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "counter_timed"));
  entity->think = qa_q1_callback_registry_action(game->named, entity, NAMED(context, "counter_reset"));
}
static void spawn_counter(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  if (falsy(entity->count)) entity->count = 2;
  if (falsy(entity->wait)) entity->wait = entity->count;
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "counter"));
}

/* --- repeater, multitouch, explosion, change target, cleanup --- */
static void repeater_schedule(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  double pausetime = qa_q1_actor_number(entity, "pausetime");
  schedule(game, entity, NAMED(context, "repeater_tick"), entity->wait + pausetime * qa_q1_host_random(game));
}
static void repeater_tick(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  targets(game, entity, activator_of(entity));
  if (!qa_q1_live(game, entity)) return;
  repeater_schedule(ctx, entity);
}
static void repeater_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)other;
  set_activator(entity, activator);
  entity->spawnflags = (double)(spawnflags(entity) ^ 1);
  if ((spawnflags(entity) & 1) == 0) qa_q1_cancel(game, entity);
  else repeater_schedule(ctx, entity);
}
static void init_repeater(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (falsy(entity->wait)) entity->wait = 1;
  if ((spawnflags(entity) & 1) != 0) repeater_schedule(context, entity);
}
static void multitouch_empty(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->wait = 0;
  if ((spawnflags(entity) & 32) == 0) targets(game, entity, activator_of(entity));
}
static void multitouch_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                             const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  if (entity->wait == 0) {
    entity->wait = 1;
    if ((spawnflags(entity) & 16) == 0) targets(game, entity, &other);
  }
  if (qa_q1_live(game, entity)) schedule(game, entity, NAMED(context, "multitouch_empty"), 0.2);
}
static void spawn_multitouch(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_init_trigger(context, entity);
  entity->wait = 0;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "multitouch_touch"));
}
static void explosion_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  entity->delay = 0;
  targets(game, entity, activator_of(entity));
  if (!qa_q1_live(game, entity)) return;
  qa_actor_id self = entity->actor.id;
  if ((spawnflags(entity) & 1) == 0) qa_q1_radius_damage(game, self, entity->has_owner ? &entity->owner : NULL, 120, &self, NULL, "");
  if (context->program == QA_Q1AD_MG3 && (spawnflags(entity) & 2) != 0)
    emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_COLORED_EXPLOSION, .origin = qa_q1_get_body(game, entity).origin,
                                        .color_start = 244, .color_length = 3});
  else
    qa_q1_effect(game, "explosion", qa_q1_get_body(game, entity).origin, NULL, 1);
  qa_q1_remove(game, entity);
}
static void explosion_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_context *context = ctx;
  set_activator(entity, activator);
  if (entity->delay > 0) schedule(game, entity, NAMED(context, "explosion"), entity->delay);
  else call_action(game, entity, NAMED(context, "explosion"));
}
static void change_target(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                          const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *target = game->entities[i];
    if (target != NULL && strcmp(target->target, entity->target) == 0) qa_q1ad_set_text(&target->target, entity->killtarget);
  }
}
static void init_change_target(qa_q1ad_context *context, qa_q1_actor *entity) {
  (void)context;
  if (entity->target[0] == '\0' || entity->killtarget[0] == '\0') qa_range_error("trigger_changetarget requires target and killtarget");
}
static void cleanup_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                        const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  size_t count = 0;
  qa_q1_actor **all = entities_snapshot(game, &count);
  for (size_t i = 0; i < count; i++)
    if (all[i] != NULL && all[i]->monster != NULL && qa_q1_health(game, all[i]->actor.id) <= 0) qa_q1_remove(game, all[i]);
  free(all);
  qa_q1_remove(game, entity);
}
static void spawn_cleanup(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (!game->options.coop) { qa_q1_remove(game, entity); return; }
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "cleanup"));
}

/* --- MG3 --- */
static void spawn_always(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (qa_q1ad_removed_outside_coop(context, entity, true)) return;
  schedule(game, entity, NAMED(context, "targets"), 0.1);
}
static void rune_relay(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_context *context = ctx;
  int32_t required = spawnflags(entity) & 15;
  if ((qa_js_to_int32(read_flags(context)) & required) != required) return;
  set_activator(entity, activator);
  schedule(game, entity, NAMED(context, "targets"), 0.1);
}
static void rune_counter(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)other;
  if (qa_q1ad_mg3_rune_count(read_flags(ctx)) >= entity->count) targets(game, entity, activator);
}
static void bn_relay(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  int32_t flags = qa_js_to_int32(read_flags(ctx));
  static const int32_t ROWS[3][3] = {{1, 8, QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE}, {2, 16, QA_Q1AD_BLOODY_NIGHTMARE_NEWGAME},
                                     {4, 32, QA_Q1AD_BLOODY_NIGHTMARE_DISCOVERED}};
  for (size_t i = 0; i < 3; i++) {
    int32_t yes = ROWS[i][0], no = ROWS[i][1], bit = ROWS[i][2];
    if (((spawnflags(entity) & yes) != 0 && (flags & bit) == 0) || ((spawnflags(entity) & no) != 0 && (flags & bit) != 0)) return;
  }
  targets(game, entity, activator);
}
static void init_rune_counter(qa_q1ad_context *context, qa_q1_actor *entity) {
  (void)context;
  if (falsy(entity->count)) entity->count = 2;
}
static void spawn_bn_relay(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED((qa_q1ad_context *)ctx, "bn_relay"));
}
static void init_sacrifice_counter(qa_q1ad_context *context, qa_q1_actor *entity) {
  (void)context;
  if (falsy(entity->count)) entity->count = 2;
  if (falsy(entity->wait)) entity->wait = entity->count;
}
static void sacrifice_empty(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx; (void)game;
  entity->wait = 0;
}
static void sacrifice_check(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                            const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &other) || qa_q1_health(game, other) <= 0) return;
  if (entity->wait == 0) {
    entity->wait = 1;
    qa_q1_actor *found[1];
    if (qa_q1_find(game, entity->target, found, 1) != 0 && found[0]->count > 0) counter_message(context, found[0], &other, true, true);
  }
  schedule(game, entity, NAMED(context, "sacrifice_empty"), 0.2);
}
static void spawn_check_sacrifices(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_init_trigger(context, entity);
  entity->wait = 0;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "sacrifice_check"));
}
static bool at_top(const qa_q1_actor *entity) { return same(entity->state, "top") || same(entity->state, "up"); }
static void door_relay(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other;
  qa_q1_actor *found[256];
  size_t count = qa_q1_find(game, entity->target, found, 256);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = found[i];
    bool top = at_top(target);
    if ((spawnflags(entity) & (top ? 1 : 2)) != 0) continue;
    if (strcmp(target->classname, "func_door") == 0) {
      if (top) qa_q1_door_down(game, target);
      else qa_q1_door_up(game, target);
    } else if (strcmp(target->classname, "func_button") == 0) {
      if (top) call_action(game, target, "button_return");
      else qa_q1_call_use(game, target, qa_q1_callback_registry_use(game->named, target, "button_use"), NULL, activator);
    }
  }
}
static void door_group(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *member = game->entities[i];
    if (member == NULL || strcmp(qa_q1_actor_text(member, "category"), qa_q1_actor_text(entity, "category")) != 0) continue;
    bool top = at_top(member);
    double goal = qa_q1_actor_number(member, "goal_state");
    if ((!top && goal == 0) || (top && goal == 1)) return;
  }
  targets(game, entity, activator);
}
static void init_door_relay(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (entity->target[0] == '\0') qa_q1_remove(context->game, entity);
}
static void init_door_group(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (entity->target[0] == '\0' || qa_q1_actor_text(entity, "category")[0] == '\0') qa_q1_remove(context->game, entity);
}
static void lore(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                 const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &other) || game->time <= qa_q1ad_player_number(context, other, "lore_active")) return;
  qa_q1ad_set_player_number(context, other, "lore_active", game->time + 0.5);
  qa_q1_message(game, &other, entity->message, true, NULL, 0);
}
static void spawn_lore(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  int32_t flags = qa_js_to_int32(read_flags(context));
  if (same(game->map_name, "hub"))
    for (int32_t bit = 1; bit <= 4; bit *= 2) {
      int number = bit == 1 ? 1 : bit == 2 ? 2 : 3;
      char hint[64];
      (void)snprintf(hint, sizeof hint, "$mg3_hub_rune%d_hint", number);
      if ((flags & bit) != 0 && strcmp(entity->message, hint) == 0) {
        char complete[80];
        (void)snprintf(complete, sizeof complete, "%s_complete", entity->message);
        qa_q1ad_set_text(&entity->message, complete);
      }
    }
  if (same(game->map_name, "map4") && strcmp(entity->message, "$mg3_hint_bloody_nightmare_new") == 0 && (flags & 448) != 0) {
    qa_q1_remove(game, entity);
    return;
  }
  qa_q1ad_init_trigger(context, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "lore"));
}
static void music(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  double track = qa_q1_actor_number(entity, "style");
  if (entity->sounds == track) return;
  emit_addon(ctx, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_MUSIC, .track = track, .loop_track = track});
  for (size_t i = 0; i < game->entity_count; i++)
    if (game->entities[i] != NULL && strcmp(game->entities[i]->classname, "trigger_music") == 0) game->entities[i]->sounds = track;
}
static void init_music(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  entity->sounds = game->world != NULL ? game->world->sounds : 0;
  if (entity->targetname[0] == '\0') { qa_q1_remove(game, entity); return; }
  if (qa_q1_actor_number(entity, "style") == 0) qa_q1ad_set_number(context, entity, "style", 3);
}
static void heal(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                 const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL || entity->attack_finished > game->time) return;
  double health = qa_q1_health(game, other);
  if (health > 0 && health < player->max_health)
    qa_gameplay_authority_set_health(game->host.combat, player->actor, qa_js_min(health + entity->damage, player->max_health));
  entity->attack_finished = fr(game->time + entity->wait);
}
static void spawn_heal(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (falsy(entity->damage)) entity->damage = 1;
  if (falsy(entity->wait)) entity->wait = 0.1;
  qa_q1ad_init_trigger(context, entity);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "heal"));
}
static void quad(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                 const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  qa_q1_player_state *player = qa_q1_player(game, other);
  if (player == NULL) return;
  if (qa_q1_player_powerup_get(player, "quad") <= game->time) qa_q1_sound(game, player->actor.id, "items/damage.wav", "item", 1, 1);
  double expires = fr(game->time + 0.1);
  qa_q1_player_powerup_set(player, "quad", expires);
  qa_q1ad_set_player_number(context, other, "super_time", 2);
  game->host.powerup(game->host.ctx, player->actor, "quad", expires);
  qa_q1_link(game, entity);
}
static void spawn_trigger_touch(qa_q1ad_context *context, qa_q1_actor *entity, const char *name) {
  qa_q1ad_init_trigger(context, entity);
  entity->touch = qa_q1_callback_registry_touch(context->game->named, entity, name);
}
static void spawn_quad(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  spawn_trigger_touch(ctx, entity, NAMED((qa_q1ad_context *)ctx, "quad"));
}
static void kill_monster(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)ctx; (void)other;
  qa_q1_actor *found[256];
  size_t count = qa_q1_find(game, entity->target, found, 256);
  for (size_t i = 0; i < count; i++) {
    qa_q1_actor *target = found[i];
    if (target->monster == NULL || qa_q1_health(game, target->actor.id) <= 0) continue;
    (void)qa_q1_damage(game, target->actor.id, activator != NULL ? *activator : entity->actor.id, activator,
                       qa_q1_health(game, target->actor.id) * 2, NULL, NULL, NULL, NULL);
  }
}
static void init_kill_monster(qa_q1ad_context *context, qa_q1_actor *entity) {
  if (entity->target[0] == '\0' || entity->targetname[0] == '\0') qa_q1_remove(context->game, entity);
}
static void silent_teleport(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                            const qa_touch_surface *surface) {
  (void)ctx; (void)normal; (void)surface;
  if (!qa_q1_is_player(game, &other)) return;
  qa_owned_actor player;
  bool owned = qa_session_actor_registry_resolve_owned(game->host.actors, other, &player);
  qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, other);
  if (!owned || !read.has_value) return;
  qa_body_state body = read.value;
  qa_actor_id ground;
  if (body.ground != NULL) { ground = *body.ground; body.ground = &ground; }
  double height = qa_q1_actor_number(entity, "height");
  qa_vec3 destination = qa_q1_vadd(body.origin, (qa_vec3){0, 0, height + (height < 0 ? 1 : 0)});
  qa_q1_actor *death = qa_q1_create(game, "teledeath", NULL, NULL);
  death->has_owner = true;
  death->owner = other;
  death->solid = "trigger";
  qa_q1_body_patch patch = {.has_origin = true, .origin = destination, .has_bounds = true,
                            .bounds = {qa_q1_vadd(body.bounds.min, (qa_vec3){-1, -1, -1}), qa_q1_vadd(body.bounds.max, (qa_vec3){1, 1, 1})}};
  qa_q1_set_body(game, death, &patch);
  death->touch = qa_q1_callback_registry_touch(game->named, death, "tdeath_touch");
  qa_q1_link(game, death);
  qa_bounds bounds = qa_q1_get_body(game, death).bounds;
  qa_bounds box = {qa_q1_vadd(destination, bounds.min), qa_q1_vadd(destination, bounds.max)};
  size_t count = 0;
  const qa_actor_observation *observed = qa_session_actor_registry_observations(game->host.actors, &count);
  qa_actor_id *victims = malloc((count != 0 ? count : 1) * sizeof *victims); /* the TS observations() array */
  if (victims == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i < count; i++) victims[i] = observed[i].id;
  for (size_t i = 0; i < count; i++) {
    qa_body_state_result state = qa_shared_body_table_read(game->host.bodies, victims[i]);
    if (state.has_value &&
        qa_q1_overlaps(box, (qa_bounds){qa_q1_vadd(state.value.origin, state.value.bounds.min),
                                        qa_q1_vadd(state.value.origin, state.value.bounds.max)}))
      (void)qa_actor_callback_table_touch(game->host.callbacks,
                                          (qa_touch_contact){.self = death->actor, .other = victims[i], .plane = NULL, .surface = NULL});
  }
  free(victims);
  qa_q1_schedule(game, death, 0.2, qa_q1_callback_registry_action(game->named, death, "SUB_Remove"));
  body.origin = destination;
  qa_shared_body_table_write(game->host.bodies, player, body);
  qa_shared_body_table_link(game->host.bodies, player, NULL);
}
static void spawn_silent_teleport(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_number(entity, "height") == 0) qa_q1ad_set_number(context, entity, "height", -2048);
  spawn_trigger_touch(context, entity, NAMED(context, "silent_teleport"));
}
static void cutscene(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal,
                     const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, &other)) return;
  entity->touch = (qa_q1_bound_touch){0};
  qa_q1_actor *camera = NULL;
  for (size_t i = 0; i < game->entity_count && camera == NULL; i++)
    if (game->entities[i] != NULL && strcmp(game->entities[i]->classname, "info_intermission") == 0) camera = game->entities[i];
  if (camera == NULL) qa_range_error("trigger_cutscene has no intermission camera");
  qa_vec3 position = qa_q1_get_body(game, camera).origin, angles = qa_q1_actor_vector(camera, "mangle");
  size_t count = 0;
  const qa_actor_id *players = game->host.players(game->host.ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id player_id = players[i];
    qa_owned_actor actor;
    bool owned = qa_session_actor_registry_resolve_owned(game->host.actors, player_id, &actor);
    qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, player_id);
    if (!owned || !read.has_value) continue;
    qa_body_state body = read.value;
    qa_actor_id ground;
    if (body.ground != NULL) { ground = *body.ground; body.ground = &ground; }
    body.origin = position;
    body.angles = angles;
    body.velocity = QA_Q1_ZERO;
    qa_shared_body_table_write(game->host.bodies, actor, body);
    qa_shared_body_table_link(game->host.bodies, actor, NULL);
    qa_q1_actor *source = qa_q1_get_entity(game, &player_id);
    if (source != NULL) {
      source->solid = "none";
      source->movement = "none";
      qa_q1ad_set_text(&source->model, "");
    }
  }
  emit_addon(context, (qa_q1ad_event){.kind = QA_Q1AD_EVENT_CUTSCENE, .camera = position, .angles = angles});
}
static void spawn_cutscene(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  spawn_trigger_touch(ctx, entity, NAMED((qa_q1ad_context *)ctx, "cutscene"));
}
static void spawn_axe_button(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->max_health = 1;
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, 1);
  qa_q1_spawn_button(game, entity);
}
static void set_skill(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other;
  qa_q1ad_context *context = ctx;
  if (!qa_q1_is_player(game, activator)) return;
  const char *message = entity->message;
  bool bloody = strcmp(message, "4") == 0;
  if (!bloody) write_flags(context, (double)(qa_js_to_int32(read_flags(context)) & ~QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE));
  int skill = strcmp(message, "0") == 0 ? 0 : strcmp(message, "1") == 0 ? 1 : strcmp(message, "2") == 0 ? 2
              : strcmp(message, "3") == 0 || bloody ? 3 : -1;
  if (skill < 0) return;
  if (bloody) write_flags(context, (double)(qa_js_to_int32(read_flags(context)) | QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE));
  static const char *const SELECTED[] = {"$mg3_hub_selected_easy", "$mg3_hub_selected_normal", "$mg3_hub_selected_hard",
                                         "$mg3_hub_selected_nightmare"};
  qa_q1ad_broadcast(context, bloody ? "$mg3_selected_bloody_nightmare" : SELECTED[skill]);
  qa_q1_campaign_binding campaign = qa_q1_base_campaign(context->base);
  campaign.set_skill(campaign.ctx, skill);
  static const char *const SKILLS[] = {"0", "1", "2", "3"};
  context->services.set_cvar(context->services.ctx, "skill", SKILLS[skill]);
}
static void spawn_set_skill(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED((qa_q1ad_context *)ctx, "set_skill"));
}
static void health_relay(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                         const qa_actor_id *activator) {
  (void)ctx; (void)other;
  targets(game, entity, activator);
}
static void init_health_relay(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  if (qa_q1_health(game, entity->actor.id) == 0) qa_gameplay_authority_set_health(game->host.combat, entity->actor, 0.5);
}
static void explode_repeatedly(qa_q1ad_context *context, qa_q1_actor *entity) {
  qa_q1_services *game = context->game;
  if (entity->delay > 0) {
    double delay = entity->delay;
    entity->delay = 0;
    schedule(game, entity, NAMED(context, "explosion_repeater"), delay);
    return;
  }
  qa_actor_id previous_id;
  qa_q1_actor *previous = qa_q1_actor_reference_get(entity, "explosion.child", &previous_id) ? qa_q1_get_entity(game, &previous_id) : NULL;
  if (previous != NULL && strcmp(previous->classname, "spawned_explosion") == 0) qa_q1_remove(game, previous);
  qa_q1_actor *child = qa_q1_create(game, "spawned_explosion", NULL, NULL);
  child->has_owner = true;
  child->owner = entity->actor.id;
  (void)qa_q1_actor_reference_set(entity, "explosion.child", &child->actor.id);
  qa_q1_set_origin(game, child, qa_q1_get_body(game, entity).origin);
  call_action(game, child, NAMED(context, "explosion"));
  if ((spawnflags(entity) & 4) != 0 && --entity->count == 0) { qa_q1_remove(game, entity); return; }
  double random = qa_q1_host_random(game);
  schedule(game, entity, NAMED(context, "explosion_repeater"), entity->wait + random * qa_q1_actor_number(entity, "pausetime"));
}
static void explosion_repeater_action(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)game;
  explode_repeatedly(ctx, entity);
}
static void explosion_repeater_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                                   const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator;
  explode_repeatedly(ctx, entity);
}
static void spawn_explosion_repeater(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  if (falsy(entity->wait)) entity->wait = 0.8;
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED((qa_q1ad_context *)ctx, "explosion_repeater"));
}

static void reg(qa_q1ad_context *context, const char *name, qa_q1_callback_handlers handlers) {
  handlers.ctx = context;
  qa_q1_callback_registry_register(context->game->named, name, handlers);
}
static void reg_spawn_use(qa_q1ad_context *context, spawn_use_row *row, const char *classname, const char *name,
                          initialize_fn initialize) {
  *row = (spawn_use_row){.context = context, .name = name, .initialize = initialize};
  qa_q1_register_spawn(context->game, classname, spawn_use, row);
}

void qa_q1ad_register_triggers(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  /* spawnUse closures: owned by the context (freed by qa_q1ad_context_destroy). */
  spawn_use_row *rows = calloc(11, sizeof *rows);
  if (rows == NULL) qa_range_error("out of memory");
  free(context->spawn_use_rows);
  context->spawn_use_rows = rows;
  reg(context, NAMED(context, "targets"), (qa_q1_callback_handlers){.action = targets_action});
  reg(context, NAMED(context, "counter_reset"), (qa_q1_callback_handlers){.action = counter_reset});
  reg(context, NAMED(context, "counter"), (qa_q1_callback_handlers){.use = counter});
  reg(context, NAMED(context, "counter_timed"), (qa_q1_callback_handlers){.use = counter_timed});
  reg(context, NAMED(context, "sacrifice_counter"), (qa_q1_callback_handlers){.use = sacrifice_counter});
  qa_q1_register_spawn(game, "trigger_counter_timed", spawn_counter_timed, context);
  qa_q1_register_spawn(game, "trigger_counter", spawn_counter, context);
  reg(context, NAMED(context, "repeater_tick"), (qa_q1_callback_handlers){.action = repeater_tick});
  reg(context, NAMED(context, "repeater_use"), (qa_q1_callback_handlers){.use = repeater_use});
  reg_spawn_use(context, &rows[0], "trigger_repeater", NAMED(context, "repeater_use"), init_repeater);
  reg(context, NAMED(context, "multitouch_empty"), (qa_q1_callback_handlers){.action = multitouch_empty});
  reg(context, NAMED(context, "multitouch_touch"), (qa_q1_callback_handlers){.touch = multitouch_touch});
  qa_q1_register_spawn(game, "trigger_multitouch", spawn_multitouch, context);
  reg(context, NAMED(context, "explosion"), (qa_q1_callback_handlers){.action = explosion_action, .use = explosion_use});
  reg_spawn_use(context, &rows[1], "trigger_explosion", NAMED(context, "explosion"), NULL);
  reg(context, NAMED(context, "change_target"), (qa_q1_callback_handlers){.use = change_target});
  reg_spawn_use(context, &rows[2], "trigger_changetarget", NAMED(context, "change_target"), init_change_target);
  reg(context, NAMED(context, "cleanup"), (qa_q1_callback_handlers){.use = cleanup_use});
  qa_q1_register_spawn(game, "trigger_cleanup_corpses", spawn_cleanup, context);
  if (context->program != QA_Q1AD_MG3) qa_q1_register_spawn(game, "mge2m2_cleanup_corpses", spawn_cleanup, context);
  if (context->program != QA_Q1AD_MG3) return;

  qa_q1_register_spawn(game, "trigger_always", spawn_always, context);
  reg(context, NAMED(context, "rune_relay"), (qa_q1_callback_handlers){.use = rune_relay});
  reg(context, NAMED(context, "rune_counter"), (qa_q1_callback_handlers){.use = rune_counter});
  reg(context, NAMED(context, "bn_relay"), (qa_q1_callback_handlers){.use = bn_relay});
  reg_spawn_use(context, &rows[3], "trigger_rune_relay", NAMED(context, "rune_relay"), NULL);
  reg_spawn_use(context, &rows[4], "trigger_rune_counter", NAMED(context, "rune_counter"), init_rune_counter);
  qa_q1_register_spawn(game, "trigger_bloodynightmare_relay", spawn_bn_relay, context);
  reg_spawn_use(context, &rows[5], "trigger_sacrifice_counter", NAMED(context, "sacrifice_counter"), init_sacrifice_counter);
  reg(context, NAMED(context, "sacrifice_empty"), (qa_q1_callback_handlers){.action = sacrifice_empty});
  reg(context, NAMED(context, "sacrifice_check"), (qa_q1_callback_handlers){.touch = sacrifice_check});
  qa_q1_register_spawn(game, "trigger_check_sacrifices", spawn_check_sacrifices, context);
  reg(context, NAMED(context, "door_relay"), (qa_q1_callback_handlers){.use = door_relay});
  reg(context, NAMED(context, "door_group"), (qa_q1_callback_handlers){.use = door_group});
  reg_spawn_use(context, &rows[6], "trigger_door_relay", NAMED(context, "door_relay"), init_door_relay);
  reg_spawn_use(context, &rows[7], "trigger_doorgroup_relay", NAMED(context, "door_group"), init_door_group);
  reg(context, NAMED(context, "lore"), (qa_q1_callback_handlers){.touch = lore});
  qa_q1_register_spawn(game, "trigger_lore", spawn_lore, context);
  reg(context, NAMED(context, "music"), (qa_q1_callback_handlers){.use = music});
  reg_spawn_use(context, &rows[8], "trigger_music", NAMED(context, "music"), init_music);
  reg(context, NAMED(context, "heal"), (qa_q1_callback_handlers){.touch = heal});
  qa_q1_register_spawn(game, "trigger_heal", spawn_heal, context);
  reg(context, NAMED(context, "quad"), (qa_q1_callback_handlers){.touch = quad});
  qa_q1_register_spawn(game, "trigger_quad", spawn_quad, context);
  reg(context, NAMED(context, "kill_monster"), (qa_q1_callback_handlers){.use = kill_monster});
  reg_spawn_use(context, &rows[9], "trigger_relay_killmonster", NAMED(context, "kill_monster"), init_kill_monster);
  reg(context, NAMED(context, "silent_teleport"), (qa_q1_callback_handlers){.touch = silent_teleport});
  qa_q1_register_spawn(game, "trigger_teleport_silent", spawn_silent_teleport, context);
  reg(context, NAMED(context, "cutscene"), (qa_q1_callback_handlers){.touch = cutscene});
  qa_q1_register_spawn(game, "trigger_cutscene", spawn_cutscene, context);
  qa_q1_register_spawn(game, "func_axe_button", spawn_axe_button, context);
  reg(context, NAMED(context, "set_skill"), (qa_q1_callback_handlers){.use = set_skill});
  qa_q1_register_spawn(game, "trigger_relay_setskill", spawn_set_skill, context);
  reg(context, NAMED(context, "health_relay"), (qa_q1_callback_handlers){.use = health_relay});
  reg_spawn_use(context, &rows[10], "trigger_health_relay", NAMED(context, "health_relay"), init_health_relay);
  reg(context, NAMED(context, "explosion_repeater"),
      (qa_q1_callback_handlers){.action = explosion_repeater_action, .use = explosion_repeater_use});
  qa_q1_register_spawn(game, "trigger_explosion_repeater", spawn_explosion_repeater, context);
}
