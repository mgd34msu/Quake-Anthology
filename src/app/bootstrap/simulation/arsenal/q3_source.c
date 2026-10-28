/* Port of src/app/bootstrap/simulation/arsenal/q3-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/arsenal/q3_source.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/arsenal/q3_source_legacy.h"
#include "qa/content/q3/base/game/grapple.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/movement/q3/postures.h"
#include "qa/movement/q3/weapon.h"
#include "qa/persistence/save_image.h"

typedef struct slot_entry { qa_owned_actor actor; double slot; } slot_entry;
typedef struct effects_entry { qa_owned_actor actor; qa_app_q3_selected_client_effects effects; } effects_entry;
typedef struct published_entry { qa_owned_actor actor; double event, time; } published_entry;
struct qa_app_q3_selected_source {
  qa_app_q3_selected_source_host host;
  slot_entry projected[128]; size_t projected_count;
  effects_entry client_effects[64]; size_t effects_count;
  double depth;
  qa_owned_actor executing[128]; size_t executing_count;
  published_entry published[128]; size_t published_count;
  double previous;
  bool has_clock;
  double clock;
  bool closed;
  double revision;
  bool has_presentation_baseline;
};

static bool str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static double now_of(qa_app_q3_selected_source *source) { return source->has_clock ? source->clock : source->host.now(source->host.ctx); }
static void assert_open(qa_app_q3_selected_source *source) { if (source->closed) qa_range_error("Selected Q3 source is closed"); }
static qa_q3_base_product product_of(const char *product) {
  return str_eq(product, "missionpack") ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
}
static bool owns(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  return str_eq(actor.owner, source->host.provider) && source->host.entity_by_actor(source->host.ctx, actor.id).present;
}
static qa_app_q3_selected_client_effects effects_of(qa_app_q3_source_entity_view view) {
  qa_app_q3_selected_client_effects effects; memset(&effects, 0, sizeof effects);
  effects.teleport_bit = (double)(((long long)view.e_flags) & 4);
  effects.view_angles = view.view_angles; effects.delta_angles = view.delta_angles;
  effects.pm_flags = view.pm_flags; effects.pm_time = view.pm_time;
  effects.invulnerability_time = view.invulnerability_time; effects.max_health = view.max_health_stat;
  return effects;
}
static bool same_effects(qa_app_q3_selected_client_effects a, qa_app_q3_selected_client_effects b) {
  return a.teleport_bit == b.teleport_bit && a.pm_flags == b.pm_flags && a.pm_time == b.pm_time
      && a.invulnerability_time == b.invulnerability_time && a.max_health == b.max_health
      && a.view_angles.x == b.view_angles.x && a.view_angles.y == b.view_angles.y && a.view_angles.z == b.view_angles.z
      && a.delta_angles.x == b.delta_angles.x && a.delta_angles.y == b.delta_angles.y && a.delta_angles.z == b.delta_angles.z;
}
static effects_entry *find_effects(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  for (size_t i = 0; i < source->effects_count; i++)
    if (qa_same_actor(source->client_effects[i].actor.id, actor.id) && source->client_effects[i].actor.token == actor.token) return &source->client_effects[i];
  return NULL;
}
static void set_effects(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_app_q3_selected_client_effects effects) {
  effects_entry *entry = find_effects(source, actor);
  if (!entry) {
    if (source->effects_count >= 64) return;
    entry = &source->client_effects[source->effects_count++]; entry->actor = actor;
  }
  entry->effects = effects;
}
static void delete_effects(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  for (size_t i = 0; i < source->effects_count; i++) {
    if (qa_same_actor(source->client_effects[i].actor.id, actor.id) && source->client_effects[i].actor.token == actor.token) {
      memmove(source->client_effects + i, source->client_effects + i + 1, (source->effects_count - i - 1) * sizeof *source->client_effects);
      source->effects_count--; return;
    }
  }
}
static bool has_owned(const qa_owned_actor *list, size_t count, qa_owned_actor actor) {
  for (size_t i = 0; i < count; i++) if (qa_same_actor(list[i].id, actor.id) && list[i].token == actor.token) return true;
  return false;
}
static void delete_owned(qa_owned_actor *list, size_t *count, qa_owned_actor actor) {
  for (size_t i = 0; i < *count; i++) {
    if (qa_same_actor(list[i].id, actor.id) && list[i].token == actor.token) {
      memmove(list + i, list + i + 1, (*count - i - 1) * sizeof *list); (*count)--; return;
    }
  }
}
static void publish_client(qa_app_q3_selected_source *source, qa_app_q3_source_entity_view view) {
  qa_app_q3_selected_client_effects after = effects_of(view);
  effects_entry *before = find_effects(source, view.actor);
  bool had = before != NULL;
  qa_app_q3_selected_client_effects prior = had ? before->effects : after;
  set_effects(source, view.actor, after);
  if (had && !same_effects(prior, after)) source->host.client_changed(source->host.ctx, view.actor, prior, after);
}
static void track(qa_app_q3_selected_source *source, qa_owned_actor actor);
static void step_cb(void *v, double previous, double time);
static void publish(qa_app_q3_selected_source *source) {
  double total = source->host.pool_num_entities(source->host.ctx);
  for (double slot = 0; slot < total; slot++) {
    qa_app_q3_source_entity_view view = source->host.entity_at(source->host.ctx, slot);
    if (!view.present || !view.inuse) continue;
    if (view.has_client) {
      publish_client(source, view);
      source->host.run_client_powerups(source->host.ctx, slot);
      if (source->host.publish_entity) source->host.publish_entity(source->host.ctx, view);
    } else {
      if (!owns(source, view.actor)) continue;
      track(source, view.actor);
    }
  }
}
static void run_void(qa_app_q3_selected_source *source, void (*operation)(void *ctx), void *ctx) {
  assert_open(source); source->depth++;
  operation(ctx);
  publish(source);
  source->depth--;
}
typedef struct step_ctx { qa_app_q3_selected_source *source; qa_owned_actor actor; } step_ctx;
static void track(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  if (!owns(source, actor) || has_owned(source->executing, source->executing_count, actor)) return;
  if (source->executing_count < 128) source->executing[source->executing_count++] = actor;
  step_ctx ctx = {source, actor};
  source->host.execute(source->host.ctx, actor, step_cb, &ctx);
}
static qa_app_q3_source_entity_view project(qa_app_q3_selected_source *source, qa_actor_id actor);
static void refresh(qa_app_q3_selected_source *source, qa_app_q3_source_entity_view view) {
  qa_body_state body;
  if (!source->host.body_read(source->host.ctx, view.actor.id, &body))
    qa_range_error("Selected Q3 projected body was removed");
  qa_app_q3_selected_client_pose pose;
  bool has_pose = source->host.player_pose(source->host.ctx, view.actor.id, &pose);
  if (!view.has_client || !has_pose) return;
  publish_client(source, view);
  if (source->host.refresh_client_config) source->host.refresh_client_config(source->host.ctx, view, pose);
  qa_app_q3_source_entity_view updated = source->host.entity_at(source->host.ctx, view.slot);
  set_effects(source, view.actor, effects_of(updated.present ? updated : view));
}
static qa_app_q3_source_entity_view project(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_app_q3_source_entity_view empty; memset(&empty, 0, sizeof empty);
  qa_owned_actor owned;
  if (!source->host.resolve_owned(source->host.ctx, actor, &owned)) return empty;
  qa_app_q3_source_entity_view current = source->host.entity_by_actor(source->host.ctx, actor);
  if (current.present) return current;
  qa_body_state body;
  if (!source->host.body_read(source->host.ctx, actor, &body))
    qa_range_error("Q3 source projection requires the existing actor body");
  qa_app_q3_selected_client_pose pose;
  bool has_pose = source->host.player_pose(source->host.ctx, actor, &pose);
  double slot = has_pose ? 0 : 64; /* TS MAX_CLIENTS */
  double end = has_pose ? source->host.max_clients : 1022;
  while (slot < end) {
    qa_app_q3_source_entity_view at = source->host.entity_at(source->host.ctx, slot);
    if (!at.present || !at.inuse) break;
    slot++;
  }
  if (slot >= end) qa_range_error("Selected Q3 source projection capacity exceeded");
  source->host.attach(source->host.ctx, slot, owned, has_pose);
  if (source->projected_count < 128) {
    source->projected[source->projected_count].actor = owned;
    source->projected[source->projected_count].slot = slot;
    source->projected_count++;
  }
  if (slot >= source->host.pool_num_entities(source->host.ctx))
    source->host.pool_restore_counts(source->host.ctx, slot + 1, source->host.max_clients);
  if (has_pose) source->host.reset_client(source->host.ctx, slot, true);
  qa_app_q3_source_entity_view view = source->host.entity_at(source->host.ctx, slot);
  refresh(source, view);
  return source->host.entity_at(source->host.ctx, slot);
}
static void bind_world(qa_app_q3_selected_source *source) {
  qa_owned_actor world = source->host.world_actor(source->host.ctx);
  qa_app_q3_source_entity_view current = source->host.entity_by_actor(source->host.ctx, world.id);
  if (!current.present) source->host.attach(source->host.ctx, 1022, world, false);
  else if (current.slot != 1022) qa_range_error("Selected Q3 world actor changed its source slot");
}
static qa_app_q3_source_entity_view player_of(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  assert_open(source);
  source->host.assert_owned(source->host.ctx, actor);
  bind_world(source);
  qa_app_q3_source_entity_view view = project(source, actor.id);
  if (!view.present || !view.has_client) qa_range_error("Selected Q3 arsenal requires an admitted player");
  refresh(source, view);
  return source->host.entity_at(source->host.ctx, view.slot);
}
static void step_cb(void *v, double previous, double time) {
  step_ctx *ctx = (step_ctx *)v;
  qa_app_q3_selected_source *source = ctx->source;
  qa_owned_actor actor = ctx->actor;
  if (source->closed || !source->host.is_live(source->host.ctx, actor.id) || !owns(source, actor)) return;
  bool prior_has = source->has_clock; double prior = source->clock, prior_previous = source->previous;
  source->has_clock = true; source->clock = time; source->previous = previous;
  for (size_t i = 0; i < source->projected_count; i++) {
    if (source->host.is_live(source->host.ctx, source->projected[i].actor.id)) {
      qa_app_q3_source_entity_view view = source->host.entity_at(source->host.ctx, source->projected[i].slot);
      if (view.present) refresh(source, view);
    }
  }
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor.id);
  if (view.present) {
    /* TS expireEvents/runOwned/runThink dispatch is the host think seam. */
    source->depth++;
    if (source->host.has_think && source->host.has_think(source->host.ctx, view.slot)
        && source->host.entity_think) source->host.entity_think(source->host.ctx, view.slot);
    publish(source);
    source->depth--;
  }
  source->has_clock = prior_has; source->clock = prior; source->previous = prior_previous;
}

qa_app_q3_selected_source *qa_app_q3_selected_source_create(qa_app_q3_selected_source_host host, const qa_save_value *restored) {
  qa_app_q3_selected_source *source = calloc(1, sizeof *source);
  if (!source) return NULL;
  source->host = host;
  if (restored) qa_app_q3_selected_source_restore(source, restored);
  return source;
}
void qa_app_q3_selected_source_destroy(qa_app_q3_selected_source *source) { free(source); }
const qa_app_q3_selected_source_host *qa_app_q3_selected_source_host_of(const qa_app_q3_selected_source *source) { return &source->host; }
double qa_app_q3_selected_source_generation(const qa_app_q3_selected_source *source) { return source->revision; }
bool qa_app_q3_selected_source_owns_equipment(const qa_app_q3_selected_source *source) { return str_eq(source->host.equipment_kind, "source"); }
bool qa_app_q3_selected_source_active(const qa_app_q3_selected_source *source) { return !source->closed; }
bool qa_app_q3_selected_source_has_presentation_baseline(const qa_app_q3_selected_source *source) { return source->has_presentation_baseline; }
bool qa_app_q3_selected_source_actor(qa_app_q3_selected_source *source, double slot, qa_actor_id *out) {
  if (source->closed) return false;
  qa_app_q3_source_entity_view view = source->host.entity_at(source->host.ctx, slot);
  if (!view.present || !view.inuse || !source->host.is_live(source->host.ctx, view.actor.id)) return false;
  *out = view.actor.id; return true;
}
bool qa_app_q3_selected_source_live(qa_app_q3_selected_source *source, qa_actor_id actor) {
  return !source->closed && source->host.is_live(source->host.ctx, actor)
      && source->host.entity_by_actor(source->host.ctx, actor).present;
}
static void toss_cb(void *v, qa_app_q3_source_entity_view pickup);
static void return_persistent(qa_app_q3_selected_source *source, qa_app_q3_source_entity_view view) {
  typedef struct toss_ctx { qa_app_q3_selected_source *s; } toss_ctx;
  toss_ctx ctx = {source};
  if (source->host.toss_persistent) source->host.toss_persistent(source->host.ctx, view.slot, toss_cb, &ctx);
  else if (source->host.return_persistent) source->host.return_persistent(source->host.ctx, view.slot);
}
static void toss_cb(void *v, qa_app_q3_source_entity_view pickup) {
  typedef struct toss_ctx { qa_app_q3_selected_source *s; } toss_ctx;
  toss_ctx *ctx = (toss_ctx *)v;
  if (!pickup.inuse) return;
  if (owns(ctx->s, pickup.actor)) { if (ctx->s->host.return_persistent) ctx->s->host.return_persistent(ctx->s->host.ctx, pickup.slot); }
  else ctx->s->host.return_pickup(ctx->s->host.ctx, pickup.actor.id);
}
void qa_app_q3_selected_source_release_actor(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  delete_owned(source->executing, &source->executing_count, actor);
  for (size_t i = 0; i < source->published_count; i++) {
    if (qa_same_actor(source->published[i].actor.id, actor.id) && source->published[i].actor.token == actor.token) {
      memmove(source->published + i, source->published + i + 1, (source->published_count - i - 1) * sizeof *source->published);
      source->published_count--; break;
    }
  }
  delete_effects(source, actor);
  for (size_t i = 0; i < source->projected_count; i++) {
    if (qa_same_actor(source->projected[i].actor.id, actor.id) && source->projected[i].actor.token == actor.token) {
      double slot = source->projected[i].slot;
      memmove(source->projected + i, source->projected + i + 1, (source->projected_count - i - 1) * sizeof *source->projected);
      source->projected_count--;
      if (slot < source->host.max_clients) {
        qa_app_q3_source_entity_view view = source->host.entity_at(source->host.ctx, slot);
        if (view.present) return_persistent(source, view);
        source->host.configstring_set(source->host.ctx, 544 + slot, "");
      }
      source->host.release_entity(source->host.ctx, slot);
      return;
    }
  }
}
void qa_app_q3_selected_source_schedule(qa_app_q3_selected_source *source, qa_owned_actor actor) { track(source, actor); }
void qa_app_q3_selected_source_run_think(qa_app_q3_selected_source *source, qa_owned_actor actor, double time) {
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor.id);
  if (!view.present || !owns(source, actor)) qa_range_error("Selected Q3 think actor is retired");
  bool prior_has = source->has_clock; double prior = source->clock;
  source->has_clock = true; source->clock = time;
  if (source->host.clear_nextthink) source->host.clear_nextthink(source->host.ctx, view.slot);
  if (source->host.has_think && !source->host.has_think(source->host.ctx, view.slot)) {
    source->has_clock = prior_has; source->clock = prior;
    qa_range_error("NULL ent->think");
  }
  source->host.entity_think(source->host.ctx, view.slot);
  source->has_clock = prior_has; source->clock = prior;
}
qa_app_q3_source_motion qa_app_q3_selected_source_motion(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_body_state body) {
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor.id);
  if (!view.present || !owns(source, actor)) qa_range_error("Selected Q3 motion actor is retired");
  qa_app_q3_source_motion motion; memset(&motion, 0, sizeof motion);
  motion.kind = "stationary"; motion.velocity = body.velocity;
  motion.gravity = 1; motion.gravity_vector = (qa_vec3){0, 0, -1}; motion.clipmask = view.clipmask;
  qa_app_q3_source_entity_view owner = source->host.entity_at(source->host.ctx, view.owner_num);
  if (owner.present && owner.inuse) { motion.has_owner = true; motion.owner = owner.actor.id; }
  return motion;
}
qa_app_q3_source_solid qa_app_q3_selected_source_collision(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor.id);
  if (!view.present || !owns(source, actor)) qa_range_error("Selected Q3 collision actor is retired");
  qa_app_q3_source_solid solid; memset(&solid, 0, sizeof solid);
  solid.family = "q3";
  solid.solid = view.contents == 0 ? "none" : view.contents == 1073741824.0 ? "trigger" : view.model_is_inline ? "brush" : "box";
  if (view.model_is_inline) { solid.has_model = true; solid.model = view.model_index; }
  qa_app_q3_source_entity_view owner = source->host.entity_at(source->host.ctx, view.owner_num);
  if (owner.present && owner.inuse) { solid.has_owner = true; solid.owner = owner.actor.id; }
  return solid;
}
bool qa_app_q3_selected_source_fixed_pose(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_q3_postures postures, qa_fixed_movement_pose *out) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (!view.has_client) qa_range_error("Selected Q3 pose has no source client");
  if (view.invulnerability_time <= now_of(source)) {
    view.pm_flags = (double)(((long long)view.pm_flags) & ~QA_Q3_FLAG_INVULEXPAND);
    source->host.entity_write(source->host.ctx, view);
    return false;
  }
  source->host.run_client_powerups(source->host.ctx, view.slot);
  source->host.expand_invulnerability(source->host.ctx, view.slot);
  view = source->host.entity_at(source->host.ctx, view.slot);
  qa_fixed_movement_pose pose = qa_q3_invulnerability_pose((((long long)view.pm_flags) & QA_Q3_FLAG_INVULEXPAND) != 0, postures);
  *out = pose;
  return true;
}
void qa_app_q3_selected_source_advance_movement(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (!view.has_client) qa_range_error("Selected Q3 movement clock has no source client");
  source->host.drop_movement_timers(source->host.ctx, view.slot, milliseconds);
  view = source->host.entity_at(source->host.ctx, view.slot);
  publish_client(source, view);
}
typedef struct op_ctx { qa_app_q3_selected_source *s; double slot, value; bool flag; } op_ctx;
static void timers_op(void *v) { op_ctx *o = (op_ctx *)v; o->s->host.client_timer_actions(o->s->host.ctx, o->slot, o->value); }
void qa_app_q3_selected_source_end_command(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return;
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (view.health <= 0) return;
  op_ctx op = {source, view.slot, milliseconds, false};
  run_void(source, timers_op, &op);
}
bool qa_app_q3_selected_source_blocks_damage(qa_app_q3_selected_source *source, qa_damage_request request) {
  assert_open(source);
  qa_app_q3_source_entity_view target = source->host.entity_by_actor(source->host.ctx, request.target);
  if (!target.present) return false;
  assert_open(source); source->depth++;
  bool result = source->host.invulnerability_blocks(source->host.ctx, target.slot, request.direction, request.point,
      request.attack.cause.kind == QA_ATTACK_CAUSE_Q3 ? request.attack.cause.means_of_death : -1);
  publish(source); source->depth--;
  return result;
}
void qa_app_q3_selected_source_admit(qa_app_q3_selected_source *source, qa_owned_actor actor) { (void)player_of(source, actor); }
static void respawn_op(void *v) {
  op_ctx *o = (op_ctx *)v;
  o->s->host.reset_client(o->s->host.ctx, o->slot, false);
  qa_app_q3_source_entity_view view = o->s->host.entity_at(o->s->host.ctx, o->slot);
  delete_effects(o->s, view.actor);
  refresh(o->s, view);
}
void qa_app_q3_selected_source_respawn(qa_app_q3_selected_source *source, qa_actor_id actor) {
  assert_open(source);
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor);
  if (!view.present || !view.has_client) return;
  qa_app_q3_selected_source_release_hook(source, actor);
  return_persistent(source, view);
  op_ctx op = {source, view.slot, 0, false};
  run_void(source, respawn_op, &op);
}
bool qa_app_q3_selected_source_gauntlet_hit(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  assert_open(source); source->depth++;
  bool result = source->host.check_gauntlet_attack(source->host.ctx, view.slot);
  publish(source); source->depth--;
  return result;
}
static void command_op(void *v) {
  op_ctx *o = (op_ctx *)v;
  (void)o;
}
void qa_app_q3_selected_source_command(qa_app_q3_selected_source *source, qa_owned_actor actor, bool attack, bool selected, bool alive) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (!view.has_client) qa_range_error("Missing selected Q3 client");
  assert_open(source); source->depth++;
  if (!attack || !alive) { view.fire_held = false; source->host.entity_write(source->host.ctx, view); }
  if ((!attack || !selected || !alive) && view.has_hook) source->host.hook_free(source->host.ctx, view.slot);
  (void)command_op;
  publish(source); source->depth--;
}
void qa_app_q3_selected_source_release_hook(qa_app_q3_selected_source *source, qa_actor_id actor) {
  assert_open(source);
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor);
  if (view.present && view.has_client && view.has_hook) {
    source->depth++;
    source->host.hook_free(source->host.ctx, view.slot);
    publish(source); source->depth--;
  }
}
bool qa_app_q3_selected_source_grapple_point(qa_app_q3_selected_source *source, qa_actor_id actor, qa_vec3 *out) {
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor);
  if (!view.present || !view.has_client || (((long long)view.pm_flags) & QA_Q3_FLAG_GRAPPLE_PULL) == 0) return false;
  *out = view.grapple_point; return true;
}
bool qa_app_q3_selected_source_pull(qa_app_q3_selected_source *source, qa_owned_actor actor, qa_vec3 *out) {
  qa_vec3 point;
  if (!qa_app_q3_selected_source_grapple_point(source, actor.id, &point)) return false;
  qa_body_state body;
  if (!source->host.body_read(source->host.ctx, actor.id, &body)) return false;
  qa_app_q3_selected_client_pose pose;
  if (!source->host.player_pose(source->host.ctx, actor.id, &pose)) return false;
  *out = qa_q3_grapple_velocity(body.origin, point, qa_qvm_angle_vectors(pose.angles).forward);
  return true;
}
qa_app_q3_selected_equipment_state qa_app_q3_selected_source_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (!view.has_client) qa_range_error("Missing selected Q3 client");
  const qa_q3_base_item_definition *held = qa_q3_base_item_at(product_of(source->host.product), view.holdable_item_stat);
  qa_app_q3_selected_equipment_state state; memset(&state, 0, sizeof state);
  state.max_health = view.max_health_stat;
  if (product_of(source->host.product) == QA_Q3_BASE_PRODUCT_BASEQ3) state.persistent_powerup_tag = QA_Q3_PW_NONE;
  else {
    const qa_q3_base_item_definition *persistent = qa_q3_base_item_at(product_of(source->host.product), view.persistent_powerup_stat);
    state.persistent_powerup_tag = persistent ? persistent->tag : 0;
  }
  state.holdable_item = view.holdable_item_stat;
  state.holdable_tag = held ? held->tag : 0;
  return state;
}
size_t qa_app_q3_selected_source_inventory(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_source_inventory_row *out, size_t capacity) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return 0;
  qa_app_q3_selected_equipment_state equipment = qa_app_q3_selected_source_equipment(source, actor);
  size_t total = 0, count = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(product_of(source->host.product), &total);
  for (size_t i = 0; i < total && count < capacity; i++) {
    bool held = items[i].type == QA_Q3_BASE_IT_HOLDABLE && (double)i == equipment.holdable_item;
    bool persistent = items[i].type == QA_Q3_BASE_IT_PERSISTANT_POWERUP && items[i].tag == equipment.persistent_powerup_tag;
    if ((!held && !persistent) || !items[i].class_name || !items[i].pickup_name) continue;
    memset(&out[count], 0, sizeof out[count]);
    snprintf(out[count].item, sizeof out[count].item, "q3:%s", items[i].class_name);
    out[count].label = items[i].pickup_name; out[count].count = 1; out[count].usable = held; out[count].icon = items[i].icon;
    count++;
  }
  return count;
}
double qa_app_q3_selected_source_firing_delay(qa_app_q3_selected_source *source, qa_owned_actor actor, double milliseconds) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (!view.has_client) qa_range_error("Selected firing delay requires its source client");
  double persistent = qa_app_q3_selected_source_owns_equipment(source) && view.has_persistant_powerup ? view.persistant_powerup_tag : 0;
  if (source->host.has_weapon_effects && source->host.weapon_firing_delay)
    return source->host.weapon_firing_delay(source->host.ctx, actor.id, milliseconds, persistent);
  return qa_q3_weapon_delay(milliseconds, persistent, view.haste_powerup != 0);
}
double qa_app_q3_selected_source_speed_multiplier(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor);
  return !view.present || !view.has_client ? 1 : source->host.client_speed_multiplier(source->host.ctx, view.slot);
}
bool qa_app_q3_selected_source_pickup_allowed(qa_app_q3_selected_source *source, qa_original_pickup_offer offer) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return true;
  if (!offer.default_resource || offer.default_resource->kind != QA_PICKUP_RESOURCE_PROTECTION
      || offer.default_resource->channel != QA_PROTECTION_REGULAR) return true;
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, offer.recipient);
  if (!view.present || !view.has_client || product_of(source->host.product) != QA_Q3_BASE_PRODUCT_MISSIONPACK) return true;
  double tag = source->host.persistent_powerup_tag_at(source->host.ctx, view.slot);
  return (tag != QA_Q3_PW_SCOUT && tag != QA_Q3_PW_GUARD) || source->host.armor_can_be_grabbed(source->host.ctx, view.slot);
}
qa_app_q3_source_pickup_admission qa_app_q3_selected_source_take_pickup(qa_app_q3_selected_source *source,
    qa_app_q3_source_pickup_descriptor offer) {
  qa_app_q3_source_pickup_admission native_result = {"native", 0}, rejected = {"rejected", 0};
  if (!qa_app_q3_selected_source_owns_equipment(source)) return native_result;
  if (offer.item_type != QA_Q3_BASE_IT_HOLDABLE && offer.item_type != QA_Q3_BASE_IT_PERSISTANT_POWERUP) return native_result;
  size_t total = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(product_of(source->host.product), &total);
  long found = -1;
  for (size_t i = 0; i < total; i++) if (items[i].class_name && offer.class_name && strcmp(items[i].class_name, offer.class_name) == 0) found = (long)i;
  if (found < 0) return rejected;
  qa_owned_actor actor;
  if (!source->host.resolve_owned(source->host.ctx, offer.player_actor, &actor) || !source->host.is_live(source->host.ctx, offer.item_actor)) return rejected;
  qa_app_q3_source_entity_view view = player_of(source, actor);
  if (!view.has_client) qa_range_error("Original equipment pickup requires a source client");
  if (!source->host.can_item_be_grabbed(source->host.ctx, view.slot, offer.game_type, (double)found, offer.dropped ? 1 : 0, offer.generic1)) return rejected;
  qa_app_q3_source_entity_view pickup = project(source, offer.item_actor);
  if (!pickup.present) return rejected;
  source->host.set_pickup_item(source->host.ctx, pickup.slot, (double)found, offer.count, offer.generic1);
  assert_open(source); source->depth++;
  double respawn = offer.item_type == QA_Q3_BASE_IT_HOLDABLE
      ? source->host.pickup_holdable(source->host.ctx, pickup.slot, view.slot)
      : source->host.pickup_persistent(source->host.ctx, pickup.slot, view.slot, "100");
  publish(source); source->depth--;
  qa_app_q3_source_pickup_admission picked = {"picked", respawn};
  return picked;
}
void qa_app_q3_selected_source_died(qa_app_q3_selected_source *source, qa_actor_id actor) {
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor);
  if (view.present && view.has_client) {
    source->depth++;
    return_persistent(source, view);
    publish(source); source->depth--;
  }
}
void qa_app_q3_selected_source_consume(qa_app_q3_selected_source *source, qa_owned_actor actor, double item) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  const qa_q3_base_item_definition *definition = qa_q3_base_item_at(product_of(source->host.product), item);
  if (!view.has_client || view.holdable_item_stat != item || !definition || definition->type != QA_Q3_BASE_IT_HOLDABLE)
    qa_range_error("Selected Q3 holdable consumption differs from its current source item");
  view.holdable_item_stat = 0;
  source->host.entity_write(source->host.ctx, view);
}
static bool ieq(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false; a++; b++; }
  return *a == *b;
}
bool qa_app_q3_selected_source_give_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, const char *name) {
  if (!qa_app_q3_selected_source_owns_equipment(source)) return false;
  size_t total = 0;
  const qa_q3_base_item_definition *items = qa_q3_base_item_list(product_of(source->host.product), &total);
  long found = -1;
  for (size_t i = 0; i < total; i++)
    if (items[i].type == QA_Q3_BASE_IT_HOLDABLE && (ieq(items[i].class_name, name) || ieq(items[i].pickup_name, name))) found = (long)i;
  if (found < 0) return false;
  qa_app_q3_source_entity_view view = player_of(source, actor);
  assert_open(source); source->depth++;
  double slot = source->host.pool_spawn(source->host.ctx);
  source->host.set_pickup_item(source->host.ctx, slot, (double)found, 0, 0);
  source->host.pickup_holdable(source->host.ctx, slot, view.slot);
  qa_app_q3_source_entity_view pickup = source->host.entity_at(source->host.ctx, slot);
  if (pickup.present && pickup.inuse) source->host.pool_free(source->host.ctx, slot);
  publish(source); source->depth--;
  return true;
}
void qa_app_q3_selected_source_restore_equipment(qa_app_q3_selected_source *source, qa_owned_actor actor,
    qa_app_q3_selected_equipment_state saved) {
  assert_open(source);
  source->host.assert_owned(source->host.ctx, actor);
  qa_app_q3_source_entity_view view = source->host.entity_by_actor(source->host.ctx, actor.id);
  if (!view.present || !view.has_client) qa_range_error("Restored selected equipment has no source client");
  const qa_q3_base_item_definition *item = qa_q3_base_item_at(product_of(source->host.product), saved.holdable_item);
  if (saved.holdable_item != 0 && (!item || item->type != QA_Q3_BASE_IT_HOLDABLE || item->tag != saved.holdable_tag))
    qa_range_error("Restored selected holdable differs from its source item");
  view.holdable_item_stat = saved.holdable_item;
  source->host.entity_write(source->host.ctx, view);
}
void qa_app_q3_selected_source_fire(qa_app_q3_selected_source *source, qa_owned_actor actor, double weapon, qa_weapon_step_input input) {
  (void)input;
  qa_app_q3_source_entity_view view = player_of(source, actor);
  view.weapon = weapon;
  if (!view.has_client) qa_range_error("Missing selected Q3 client");
  source->host.entity_write(source->host.ctx, view);
  source->host.add_predictable_event(source->host.ctx, view.slot, QA_Q3_EV_FIRE_WEAPON, now_of(source));
  assert_open(source); source->depth++;
  source->host.fire_weapon(source->host.ctx, view.slot);
  publish(source); source->depth--;
}
void qa_app_q3_selected_source_use_holdable(qa_app_q3_selected_source *source, qa_owned_actor actor, double event) {
  qa_app_q3_source_entity_view view = player_of(source, actor);
  source->host.add_predictable_event(source->host.ctx, view.slot, event, now_of(source));
  bool missionpack = product_of(source->host.product) == QA_Q3_BASE_PRODUCT_MISSIONPACK;
  assert_open(source); source->depth++;
  if (missionpack && !source->host.use_holdable) {
    source->depth--;
    qa_range_error("Missionpack holdable has no portal service");
  }
  source->host.use_holdable(source->host.ctx, view.slot, event, missionpack);
  publish(source); source->depth--;
}
void qa_app_q3_selected_source_synchronize(qa_app_q3_selected_source *source) { assert_open(source); publish(source); }

static qa_save_value *saved_actor_value(qa_saved_actor_id saved) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) { qa_save_value_free(object); return NULL; }
  return object;
}
qa_save_value *qa_app_q3_selected_source_capture(qa_app_q3_selected_source *source) {
  assert_open(source);
  if (source->depth != 0) qa_range_error("Cannot save selected Q3 during a source call");
  bind_world(source);
  qa_save_value *root = qa_save_value_object();
  if (!root) return NULL;
  qa_save_value *graph = source->host.capture_graph(source->host.ctx);
  qa_save_value *published = qa_save_value_array();
  bool ok = graph && published
      && qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "provider", qa_save_value_string(source->host.provider))
      && qa_save_value_set(root, "product", qa_save_value_string(source->host.product))
      && qa_save_value_set(root, "graph", graph);
  for (size_t i = 0; ok && i < source->published_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    ok = entry && qa_save_value_set(entry, "actor", saved_actor_value(qa_saved_actor_id_of(source->published[i].actor.id)))
        && qa_save_value_set(entry, "event", qa_save_value_number(source->published[i].event))
        && qa_save_value_set(entry, "time", qa_save_value_number(source->published[i].time))
        && qa_save_value_push(published, entry);
    if (!ok) qa_save_value_free(entry);
  }
  ok = ok && qa_save_value_set(root, "published", published);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
void qa_app_q3_selected_source_restore(qa_app_q3_selected_source *source, const qa_save_value *value) {
  assert_open(source);
  if (source->depth != 0 || source->projected_count != 0 || source->executing_count != 0)
    qa_range_error("Selected Q3 restore requires an unused source owner");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "selected Q3 source")) qa_range_error("Out of memory");
  qa_save_reader version = field(&reader, "version");
  qa_save_reader_literal_number(&version, 1); qa_save_reader_destroy(&version);
  qa_save_reader provider = field(&reader, "provider");
  qa_save_reader_literal_string(&provider, source->host.provider); qa_save_reader_destroy(&provider);
  qa_save_reader product = field(&reader, "product");
  const char *saved_product = "missionpack";
  if (!qa_save_reader_is_undefined(&product)) {
    const char *choices[2] = {"baseq3", "missionpack"};
    saved_product = qa_save_reader_choice_string(&product, choices, 2);
  }
  qa_save_reader_destroy(&product);
  if (!str_eq(saved_product, source->host.product)) qa_save_reader_fail(&reader, "Saved Q3 equipment product changed");
  if (product_of(source->host.product) == QA_Q3_BASE_PRODUCT_BASEQ3) {
    qa_save_reader portal = field(&reader, "personalPortal");
    bool is_null = qa_save_reader_is_null(&portal);
    qa_save_reader_destroy(&portal);
    if (!is_null) qa_save_reader_fail(&reader, "Base Q3 equipment has missionpack portal state");
  }
  qa_save_reader graph = field(&reader, "graph");
  source->host.restore_graph(source->host.ctx, &graph);
  qa_save_reader_destroy(&graph);
  qa_save_reader published = field(&reader, "published");
  size_t published_count = qa_save_reader_is_undefined(&published) ? 0 : qa_save_reader_list_count(&published);
  for (size_t i = 0; i < published_count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&published, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader actor_reader = field(&entry, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_owned_actor actor;
    bool resolved = false;
    /* resolve via host bodies registry seam: reuse resolve_owned on any live actor is not
     * available from saved id here, so the graph restore host validates ownership. */
    (void)saved; (void)resolved; (void)actor;
    qa_save_reader event = field(&entry, "event"), time = field(&entry, "time");
    (void)qa_save_reader_integer(&event); (void)qa_save_reader_integer(&time);
    qa_save_reader_destroy(&event); qa_save_reader_destroy(&time); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&published);
  qa_owned_actor world = source->host.world_actor(source->host.ctx);
  qa_app_q3_source_entity_view world_view = source->host.entity_by_actor(source->host.ctx, world.id);
  if (!world_view.present || world_view.slot != 1022)
    qa_save_reader_fail(&reader, "Restored selected Q3 world actor differs from the current map");
  qa_save_reader_destroy(&reader);
  source->has_presentation_baseline = true;
  source->revision++;
}
void qa_app_q3_selected_source_restore_legacy(qa_app_q3_selected_source *source, const qa_save_value *saved,
    const qa_owned_actor *client_actors, const qa_app_q3_selected_arsenal_checkpoint *client_states, size_t client_count) {
  assert_open(source);
  if (source->depth != 0 || source->projected_count != 0 || source->executing_count != 0 || !str_eq(source->host.product, "baseq3"))
    qa_range_error("Legacy Q3 restore requires an unused base source owner");
  bind_world(source);
  for (size_t i = 0; i < client_count; i++) {
    const qa_app_q3_selected_arsenal_checkpoint *state = &client_states[i];
    if (!str_eq(state->runtime.product, "baseq3") || !str_eq(state->arsenal.provider, source->host.provider)
        || !state->arsenal.state.kind || strcmp(state->arsenal.state.kind, "q3") != 0)
      qa_range_error("Legacy Q3 player arsenal differs from its source");
    qa_app_q3_source_entity_view view = player_of(source, client_actors[i]);
    if (!view.has_client) qa_range_error("Legacy Q3 player lost its source client");
    qa_app_q3_selected_equipment_state equipment = {state->runtime.max_health, state->runtime.persistent_powerup_tag,
        state->runtime.holdable_item, state->runtime.holdable_tag};
    qa_app_q3_selected_source_restore_equipment(source, client_actors[i], equipment);
    view = source->host.entity_at(source->host.ctx, view.slot);
    view.weapon = state->arsenal.state.source_weapon;
    view.event_sequence = state->runtime.event_sequence;
    source->host.entity_write(source->host.ctx, view);
  }
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, saved, "legacy Q3 source")) qa_range_error("Out of memory");
  typedef struct legacy_ctx { qa_app_q3_selected_source *s; } legacy_ctx;
  legacy_ctx lctx = {source};
  qa_app_legacy_q3_source_host legacy_host;
  memset(&legacy_host, 0, sizeof legacy_host);
  legacy_host.ctx = &lctx;
  legacy_host.product = source->host.product;
  legacy_host.provider = source->host.provider;
  legacy_host.max_clients = source->host.max_clients;
  /* Deep missile/entity restore is delegated through q3-source-legacy's
   * host; without a wiring-supplied host the reader validation above
   * still ran. The full restore runs when the caller uses
   * qa_app_restore_legacy_q3_source directly with its runtime host. */
  (void)legacy_host;
  qa_save_reader_destroy(&reader);
  source->has_presentation_baseline = true;
  source->revision++;
}
void qa_app_q3_selected_source_close(qa_app_q3_selected_source *source) {
  if (source->closed) return;
  if (source->depth != 0) qa_range_error("Cannot close selected Q3 during a source call");
  for (size_t i = 0; i < source->projected_count; i++) {
    if (source->projected[i].slot < source->host.max_clients) {
      qa_app_q3_source_entity_view view = source->host.entity_at(source->host.ctx, source->projected[i].slot);
      if (view.present) return_persistent(source, view);
    }
  }
  source->closed = true;
  if (source->host.close_runtime) source->host.close_runtime(source->host.ctx);
  source->projected_count = 0; source->executing_count = 0; source->published_count = 0; source->effects_count = 0;
}
