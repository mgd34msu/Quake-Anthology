/* Port of src/content/q1/addons/field-triggers.ts. GPL-2.0-or-later. */
#include "qa/content/q1/addons/field_triggers.h"

#include <stdio.h>
#include <string.h>

static void prefix_of(qa_q1ad_context *context, char out[QA_Q1AD_NAME]) {
  (void)snprintf(out, QA_Q1AD_NAME, "%s:field:", qa_q1ad_program_name(context->program));
}
static void hurt_on(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  entity->solid = "trigger";
  qa_q1ad_cancel(context, entity);
}
static void hurt_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)other; (void)activator;
  qa_q1ad_set_number(context, entity, "state", 1 - qa_q1ad_actor_number(entity, "state"));
}
static void hurt_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  bool can_take = false;
  bool known = context->game.combat_can_take_damage != NULL &&
      context->game.combat_can_take_damage(context->game.ctx, *other, &can_take);
  bool monster_only = ((int32_t)entity->spawnflags & 8) != 0 &&
      !(context->services.is_monster != NULL && context->services.is_monster(context->services.ctx, *other));
  if (qa_q1ad_actor_number(entity, "state") != 0 || !known || !can_take || monster_only) return;
  entity->solid = "none";
  qa_actor_id self = entity->actor.id;
  qa_q1ad_damage(context, *other, &self, &self, entity->damage, NULL, NULL, NULL);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_schedule_named(context, entity, entity->wait, prefix, "hurt_on");
}
static void spawn_hurt(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1ad_live(context, entity)) return;
  qa_q1ad_actor_set(entity, "netname", "trigger_hurt");
  if (entity->damage == 0) entity->damage = 5;
  if (entity->wait == 0) entity->wait = 1;
  if (((int32_t)entity->spawnflags & 1) != 0) qa_q1ad_set_number(context, entity, "state", 1);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "hurt");
  qa_q1ad_bind_prefix(entity->use, prefix, "hurt");
}
static void push_use(qa_q1ad_context *context, qa_q1ad_actor *entity,
                     const qa_actor_id *other, const qa_actor_id *activator) {
  (void)context; (void)other; (void)activator;
  if (entity->solid != NULL && strcmp(entity->solid, "trigger") == 0) entity->solid = "none";
  else { entity->solid = "trigger"; context->game.force_retouch = 1; }
}
static void write_velocity(qa_q1ad_context *context, qa_owned_actor actor, qa_q1ad_body body,
                           qa_vec3 velocity, bool clear_ground) {
  body.velocity = velocity;
  if (clear_ground) body.has_ground = false;
  if (context->game.write_body != NULL) context->game.write_body(context->game.ctx, actor, &body);
}
static void push_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                       const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  qa_owned_actor actor;
  qa_q1ad_body body;
  if (!qa_q1ad_resolve_owned(context, *other, &actor) || !qa_q1ad_body_by_id(context, *other, &body)) return;
  qa_q1ad_actor *source = qa_q1ad_entity(context, other);
  const char *classname = context->game.classname_of != NULL
      ? context->game.classname_of(context->game.ctx, *other) : "";
  if (classname == NULL) classname = "";
  bool player = qa_q1ad_is_player(context, other);
  qa_vec3 delta = qa_scale3(entity->movedir, entity->speed * 10);
  qa_vec3 velocity = ((int32_t)entity->spawnflags & 2) != 0
      ? qa_add3(body.velocity, qa_scale3(delta, context->frame_time)) : delta;
  if (context->program != QA_Q1AD_MG3) {
    qa_q1ad_actor *entities[256];
    size_t count = qa_q1ad_entities(context, entities, 256);
    bool horde = false;
    for (size_t i = 0; i < count; i++)
      if (entities[i]->classname != NULL && strcmp(entities[i]->classname, "horde_manager") == 0) horde = true;
    if (horde) {
      if (source != NULL) source->spawnflags = (double)((int32_t)source->spawnflags & ~4);
      if (!player) {
        if (strcmp(classname, "item_artifact_invulnerability") == 0 ||
            strcmp(classname, "item_artifact_super_damage") == 0)
          write_velocity(context, actor, body, velocity, false);
        return;
      }
    }
  }
  if (qa_q1ad_health(context, *other) > 0 || strcmp(classname, "grenade") == 0) {
    bool sheltered = source == NULL ? qa_q1ad_player_number(context, *other, "in_shelter") != 0
        : (source->movement_flags & 32768) != 0;
    if (sheltered) return;
    bool jump = context->program == QA_Q1AD_MG3 && ((int32_t)entity->spawnflags & 16) != 0;
    if (jump && !body.has_ground) return;
    bool monster = context->services.is_monster != NULL &&
        context->services.is_monster(context->services.ctx, *other);
    write_velocity(context, actor, body, velocity, monster);
    if (monster && source != NULL) source->movement_flags &= ~INT64_C(512);
    if (player && qa_q1ad_player_number(context, *other, "fly_sound") < context->game.time) {
      qa_q1ad_set_player_number(context, *other, "fly_sound", context->game.time + (jump ? 0.5 : 1.5));
      const char *sound = jump ? "weapons/sgun1.wav"
          : context->program == QA_Q1AD_MG3 && ((int32_t)entity->spawnflags & 8) != 0
              ? "player/inh2o.wav" : "ambience/windfly.wav";
      qa_q1ad_sound_actor(context, actor.id, sound, "auto");
    }
  }
  if (((int32_t)entity->spawnflags & 1) != 0) qa_q1ad_remove(context, entity);
}
static void spawn_push(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  if (context->program == QA_Q1AD_MG3) {
    qa_q1ad_body body = qa_q1ad_body_of(context, entity);
    qa_vec3 direction = qa_q1ad_actor_vector(entity, "movedir");
    if (qa_length3(body.angles) == 0 && qa_length3(direction) == 0)
      qa_q1ad_set_vector(context, entity, "movedir", (qa_vec3){1, 0, 0});
    else if (qa_length3(direction) != 0)
      qa_q1ad_set_vector(context, entity, "movedir", qa_normalize3_or_zero(direction));
  }
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1ad_live(context, entity)) return;
  if (context->program == QA_Q1AD_MG3 && qa_q1ad_actor_has(entity, "movedir"))
    entity->movedir = qa_q1ad_actor_vector(entity, "movedir");
  qa_q1ad_actor_set(entity, "netname", "trigger_push");
  if (entity->speed == 0) entity->speed = 1000;
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "push");
  qa_q1ad_bind_prefix(entity->use, prefix, "push");
  if (((int32_t)entity->spawnflags & 4) != 0) entity->solid = "none";
}
static void shelter_touch(qa_q1ad_context *context, qa_q1ad_actor *entity,
                          const qa_actor_id *other, const qa_vec3 *normal) {
  (void)normal;
  if (other == NULL) return;
  const char *classname = context->game.classname_of != NULL
      ? context->game.classname_of(context->game.ctx, *other) : "";
  if (qa_q1ad_health(context, *other) <= 0 && (classname == NULL || strcmp(classname, "grenade") != 0)) return;
  qa_q1ad_body body;
  if (!qa_q1ad_body_by_id(context, *other, &body)) return;
  bool sheltered = qa_dot3(qa_sub3(body.origin, entity->pos1), entity->pos2) >= 0;
  qa_q1ad_actor *source = qa_q1ad_entity(context, other);
  if (source != NULL)
    source->movement_flags = sheltered ? source->movement_flags | 32768 : source->movement_flags & ~INT64_C(32768);
  else qa_q1ad_set_player_number(context, *other, "in_shelter", sheltered ? 1 : 0);
}
static void spawn_shelter(qa_q1ad_context *context, qa_q1ad_actor *entity) {
  qa_q1ad_init_trigger(context, entity);
  if (!qa_q1ad_live(context, entity)) return;
  qa_q1ad_body body = qa_q1ad_body_of(context, entity);
  qa_vec3 size = qa_sub3(body.bounds.max, body.bounds.min);
  entity->pos1 = qa_add3(body.bounds.min, qa_scale3(size, 0.5));
  entity->pos2 = size.x < size.y ? size.x < size.z ? (qa_vec3){1, 0, 0} : (qa_vec3){0, 0, 1}
      : size.y < size.z ? (qa_vec3){0, 1, 0} : (qa_vec3){0, 0, 1};
  if (((int32_t)entity->spawnflags & 1) != 0) entity->pos2 = qa_scale3(entity->pos2, -1);
  char prefix[QA_Q1AD_NAME];
  prefix_of(context, prefix);
  qa_q1ad_bind_prefix(entity->touch, prefix, "shelter");
}

void qa_q1ad_register_field_triggers(qa_q1ad_context *context) {
  char prefix[QA_Q1AD_NAME], full[QA_Q1AD_NAME];
  prefix_of(context, prefix);
#define REG(name, handlers) do { qa_q1ad_named_name(full, prefix, name); \
  qa_q1ad_register_named(context, full, handlers); } while (0)
  REG("hurt_on", ((qa_q1ad_handlers){.action = hurt_on}));
  REG("hurt", ((qa_q1ad_handlers){.use = hurt_use, .touch = hurt_touch}));
  REG("push", ((qa_q1ad_handlers){.use = push_use, .touch = push_touch}));
  REG("shelter", ((qa_q1ad_handlers){.touch = shelter_touch}));
#undef REG
  qa_q1ad_register_spawn(context, "trigger_hurt", false, spawn_hurt);
  qa_q1ad_register_spawn(context, "trigger_push", false, spawn_push);
  qa_q1ad_register_spawn(context, "trigger_shelter_portal", false, spawn_shelter);
}
