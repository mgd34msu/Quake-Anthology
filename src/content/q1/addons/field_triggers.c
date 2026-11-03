/* Port of src/content/q1/addons/field-triggers.ts (quakec_mg1/triggers.qc and
 * quakec_mg3/triggers.qc). GPL-2.0-or-later. */
#include "qa/content/q1/addons/field_triggers.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/js.h"

/* `${program}:field:` names: static literals, since slots borrow the name. */
#define NAMED(context, name) \
  ((const char *const[]){"dopa:field:" name, "mg1:field:" name, "mg3:field:" name, \
                         "ctf:field:" name}[(context)->program])

static bool falsy(double value) { return value == 0 || isnan(value); }
static int32_t spawnflags(const qa_q1_actor *entity) { return qa_js_to_int32(entity->spawnflags); }
static bool is_monster(qa_q1ad_context *context, qa_actor_id actor) {
  return context->services.is_monster(context->services.ctx, actor);
}
static const char *host_classname(qa_q1_services *game, qa_actor_id actor) {
  const char *classname = game->host.classname(game->host.ctx, actor);
  return classname != NULL ? classname : "";
}
/* game.host.bodies.write(actor, { ...body, velocity [, ground] }). */
static void write_velocity(qa_q1_services *game, qa_owned_actor actor, qa_body_state body, qa_vec3 velocity,
                           bool clear_ground) {
  qa_actor_id ground;
  body.velocity = velocity;
  if (clear_ground) body.ground = NULL;
  else if (body.ground != NULL) { ground = *body.ground; body.ground = &ground; }
  qa_shared_body_table_write(game->host.bodies, actor, body);
}

static void hurt_on(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  (void)ctx;
  entity->solid = "trigger";
  qa_q1_cancel(game, entity);
}
static void hurt_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)game; (void)other; (void)activator;
  qa_q1ad_set_number(ctx, entity, "state", 1 - qa_q1_actor_number(entity, "state"));
}
static void hurt_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (qa_q1_actor_number(entity, "state") != 0) return;
  qa_combat_state_result combat = qa_gameplay_authority_read(game->host.combat, other);
  if (!combat.has_value || !combat.value.can_take_damage) return;
  if ((spawnflags(entity) & 8) != 0 && !is_monster(context, other)) return;
  entity->solid = "none";
  qa_actor_id self = entity->actor.id;
  (void)qa_q1_damage(game, other, self, &self, entity->damage, NULL, NULL, NULL, NULL);
  qa_q1_schedule(game, entity, entity->wait, qa_q1_callback_registry_action(game->named, entity, NAMED(context, "hurt_on")));
}
static void spawn_hurt(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1_live(game, entity)) return;
  (void)qa_q1_actor_field_set(entity, "netname", "trigger_hurt");
  if (falsy(entity->damage)) entity->damage = 5;
  if (falsy(entity->wait)) entity->wait = 1;
  if ((spawnflags(entity) & 1) != 0) qa_q1ad_set_number(context, entity, "state", 1);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "hurt"));
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "hurt"));
}

static void push_use(void *ctx, qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *other,
                     const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  if (strcmp(entity->solid, "trigger") == 0) entity->solid = "none";
  else { entity->solid = "trigger"; game->force_retouch = 1; }
}
static void push_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                       const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  qa_owned_actor actor;
  bool owned = qa_session_actor_registry_resolve_owned(game->host.actors, other, &actor);
  qa_body_state_result read = qa_shared_body_table_read(game->host.bodies, other);
  if (!owned || !read.has_value) return;
  qa_body_state body = read.value;
  qa_q1_actor *source = qa_q1_get_entity(game, &other);
  const char *classname = host_classname(game, other);
  bool player = qa_q1_is_player(game, &other);
  qa_vec3 delta = qa_q1_vscale(entity->movedir, entity->speed * 10);
  qa_vec3 velocity = (spawnflags(entity) & 2) != 0 ? qa_q1_vadd(body.velocity, qa_q1_vscale(delta, context->frame_time)) : delta;
  if (context->program != QA_Q1AD_MG3) {
    bool horde = false;
    for (size_t i = 0; i < game->entity_count && !horde; i++)
      horde = game->entities[i] != NULL && strcmp(game->entities[i]->classname, "horde_manager") == 0;
    if (horde) {
      if (source != NULL) source->spawnflags = (double)(spawnflags(source) & ~4);
      if (!player) {
        if (strcmp(classname, "item_artifact_invulnerability") == 0 || strcmp(classname, "item_artifact_super_damage") == 0)
          write_velocity(game, actor, body, velocity, false);
        return;
      }
    }
  }
  if (qa_q1_health(game, other) > 0 || strcmp(classname, "grenade") == 0) {
    if (source == NULL ? qa_q1ad_player_number(context, other, "in_shelter") != 0
                       : (qa_js_to_int32(source->movement_flags) & 32768) != 0) return;
    bool jump = context->program == QA_Q1AD_MG3 && (spawnflags(entity) & 16) != 0;
    if (jump && body.ground == NULL) return;
    bool monster = is_monster(context, other);
    write_velocity(game, actor, body, velocity, monster);
    if (monster && source != NULL) source->movement_flags = (double)(qa_js_to_int32(source->movement_flags) & ~512);
    if (player && qa_q1ad_player_number(context, other, "fly_sound") < game->time) {
      qa_q1ad_set_player_number(context, other, "fly_sound", game->time + (jump ? 0.5 : 1.5));
      const char *sound = jump ? "weapons/sgun1.wav"
                          : context->program == QA_Q1AD_MG3 && (spawnflags(entity) & 8) != 0 ? "player/inh2o.wav"
                                                                                            : "ambience/windfly.wav";
      qa_q1_sound(game, actor.id, sound, "auto", 1, 1);
    }
  }
  if ((spawnflags(entity) & 1) != 0) qa_q1_remove(game, entity);
}
static void spawn_push(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  if (context->program == QA_Q1AD_MG3) {
    qa_vec3 angles = qa_q1_get_body(game, entity).angles, direction = qa_q1_actor_vector(entity, "movedir");
    if (qa_q1_length(angles) == 0 && qa_q1_length(direction) == 0) qa_q1ad_set_vector(context, entity, "movedir", (qa_vec3){1, 0, 0});
    else if (qa_q1_length(direction) != 0) qa_q1ad_set_vector(context, entity, "movedir", qa_q1_normalize(direction));
  }
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1_live(game, entity)) return;
  if (context->program == QA_Q1AD_MG3 && qa_q1_actor_field_get(entity, "movedir") != NULL)
    entity->movedir = qa_q1_actor_vector(entity, "movedir");
  (void)qa_q1_actor_field_set(entity, "netname", "trigger_push");
  if (falsy(entity->speed)) entity->speed = 1000;
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "push"));
  entity->use = qa_q1_callback_registry_use(game->named, entity, NAMED(context, "push"));
  if ((spawnflags(entity) & 4) != 0) entity->solid = "none";
}

static void shelter_touch(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other,
                          const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)normal; (void)surface;
  qa_q1ad_context *context = ctx;
  if (qa_q1_health(game, other) <= 0 && strcmp(host_classname(game, other), "grenade") != 0) return;
  qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, other);
  if (!body.has_value) return;
  bool sheltered = qa_q1_dot(qa_q1_vsub(body.value.origin, entity->pos1), entity->pos2) >= 0;
  qa_q1_actor *source = qa_q1_get_entity(game, &other);
  if (source != NULL) {
    int32_t flags = qa_js_to_int32(source->movement_flags);
    source->movement_flags = (double)(sheltered ? flags | 32768 : flags & ~32768);
  } else {
    qa_q1ad_set_player_number(context, other, "in_shelter", sheltered ? 1 : 0);
  }
}
static void spawn_shelter(void *ctx, qa_q1_services *game, qa_q1_actor *entity) {
  qa_q1ad_context *context = ctx;
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1_live(game, entity)) return;
  qa_bounds bounds = qa_q1_get_body(game, entity).bounds;
  qa_vec3 size = qa_q1_vsub(bounds.max, bounds.min);
  entity->pos1 = qa_q1_vadd(bounds.min, qa_q1_vscale(size, 0.5));
  entity->pos2 = size.x < size.y ? size.x < size.z ? (qa_vec3){1, 0, 0} : (qa_vec3){0, 0, 1}
                 : size.y < size.z ? (qa_vec3){0, 1, 0} : (qa_vec3){0, 0, 1};
  if ((spawnflags(entity) & 1) != 0) entity->pos2 = qa_q1_vscale(entity->pos2, -1);
  entity->touch = qa_q1_callback_registry_touch(game->named, entity, NAMED(context, "shelter"));
}

void qa_q1ad_register_field_triggers(qa_q1ad_context *context) {
  qa_q1_services *game = context->game;
  qa_q1_callback_registry_register(game->named, NAMED(context, "hurt_on"), (qa_q1_callback_handlers){.ctx = context, .action = hurt_on});
  qa_q1_callback_registry_register(game->named, NAMED(context, "hurt"),
                                   (qa_q1_callback_handlers){.ctx = context, .use = hurt_use, .touch = hurt_touch});
  qa_q1_register_spawn(game, "trigger_hurt", spawn_hurt, context);
  qa_q1_callback_registry_register(game->named, NAMED(context, "push"),
                                   (qa_q1_callback_handlers){.ctx = context, .use = push_use, .touch = push_touch});
  qa_q1_register_spawn(game, "trigger_push", spawn_push, context);
  qa_q1_callback_registry_register(game->named, NAMED(context, "shelter"), (qa_q1_callback_handlers){.ctx = context, .touch = shelter_touch});
  qa_q1_register_spawn(game, "trigger_shelter_portal", spawn_shelter, context);
}
