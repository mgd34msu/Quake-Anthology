#include "qa/q2_sound.h"
#include "internal.h"

static const char *const meat = "models/objects/gibs/sm_meat/tris.md2";
static const char *const metal = "models/objects/gibs/sm_metal/tris.md2";

static qa_vec3 project(const qa_body_state *body, qa_vec3 offset) {
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(body->angles, &forward, &right, &up);
  return qa_vec_add(body->origin,
      qa_vec_add(qa_vec_scale(forward, offset.x),
          qa_vec_add(qa_vec_scale(right, offset.y), qa_vec_scale(up, offset.z))));
}

static bool effect(qa_q2_game *game, qa_actor_id actor, qa_vec3 point,
                   const char *name, int count, qa_error *error) {
  if (!q2_actor_live(game, actor)) return true;
  return q2_projectile_event(game, actor, QA_BUILTIN_EFFECT, name, count,
                             point, point, error);
}

static bool release_failed(qa_q2_game *game, qa_actor_id actor, qa_error *error) {
  qa_error first = error ? *error : (qa_error){0}, cleanup = {0};
  if (q2_actor_live(game, actor))
    (void)qa_session_release(game->services.session, actor, &cleanup);
  if (error) *error = first;
  return false;
}

static qa_vec3 clip_velocity(qa_vec3 value) {
  return qa_v3(fmaxf(-300, fminf(300, value.x)),
                fmaxf(-300, fminf(300, value.y)),
                fmaxf(200, fminf(500, value.z)));
}

static bool gib(qa_q2_game *game, qa_actor_id source, const char *model,
                 float damage, bool organic, const qa_vec3 *point,
                 bool sized, bool hit_sound, bool fade, qa_error *error) {
  if (!q2_actor_live(game, source)) return true;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, source, &body, error)) return false;
  if (!q2_actor_live(game, source)) return true;
  qa_vec3 half = qa_vec_scale(qa_vec_sub(body.bounds.maxs, body.bounds.mins), .5f);
  qa_vec3 origin;
  if (point) origin = *point;
  else {
    qa_vec3 center = qa_vec_add(body.origin,
        qa_vec_add(body.bounds.mins, qa_vec_add(half, qa_v3(-1, -1, -1))));
    qa_vec3 offset;
    offset.x = q2_crandom(game) * half.x;
    offset.y = q2_crandom(game) * half.y;
    offset.z = q2_crandom(game) * half.z;
    origin = qa_vec_add(center, offset);
  }
  float lifetime = (fade ? sized ? 20 : 5 : sized ? 60 : 25) +
                   q2_random(game) * (sized ? 15 : 10);
  qa_vec3 impulse;
  impulse.x = damage * q2_crandom(game);
  impulse.y = damage * q2_crandom(game);
  impulse.z = damage * q2_crandom(game) + 200;
  qa_vec3 velocity = clip_velocity(qa_vec_add(body.velocity,
                              qa_vec_scale(impulse, organic ? .5f : 1)));
  qa_vec3 angular;
  angular.x = q2_random(game) * (sized ? 400 : 600);
  angular.y = q2_random(game) * (sized ? 400 : 600);
  angular.z = q2_random(game) * (sized ? game->options.edition == QA_Q2_RERELEASE ? 400 : 200 : 600);
  velocity.x *= 2; velocity.y *= 2;
  qa_bounds bounds = {0};
  if (sized) {
    velocity.z = fabsf(velocity.z);
    velocity = clip_velocity(velocity);
    velocity.z = fmaxf(350 + q2_random(game) * 100, velocity.z);
    float size = strcmp(model, "models/monsters/blackwidow2/gib2/tris.md2") == 0 ? 10 : 5;
    bounds = (qa_bounds){.mins = {-size, -size, 0}, .maxs = {size, size, size}};
  }
  qa_actor_definition definition;
  qa_string_id classname;
  if (!qa_builtin_resource(&game->services, "gib", &definition, error) ||
      !qa_builtin_resource(&game->services, "noclass", &classname, error)) return false;
  if (!q2_actor_live(game, source)) return true;
  qa_actor_reference owner_reference = qa_actor_reference_from_actor(qa_session_actors(game->services.session), game->options.owner, source);
  qa_combat_state combat = {.can_take_damage = true, .no_knockback = true};
  qa_actor_collision collision = {.family = QA_GAME_Q2, .shape = QA_SHAPE_BOX,
      .contents = qa_collision_q2_source_contents(2, 0,
          game->options.edition == QA_Q2_RERELEASE), .owner = owner_reference, .role = QA_COLLISION_SOLID};
  qa_builtin_spawn spawn = {.owner = game->options.owner, .definition = definition,
      .body = {.origin = origin, .velocity = velocity, .bounds = bounds}, .combat = &combat,
      .collision = sized ? &collision : NULL};
  qa_actor_id id;
  if (!qa_builtin_spawn_actor(&game->services, &spawn, &id, error)) return false;
  if (!q2_actor_live(game, id)) return true;
  if (!q2_actor_live(game, source))
    return qa_session_release(game->services.session, id, error);
  q2_actor *actor = q2_actor_get(game, id, true, error);
  if (!actor) return release_failed(game, id, error);
  actor->projectile = (q2_projectile){.kind = Q2_GIB,
      .owner = sized ? owner_reference : (qa_actor_reference){0}, .classname = classname,
      .effects = 2, .render_flags = 32768, .scale = game->options.edition == QA_Q2_RERELEASE ? 0 : 1, .alpha = 1,
      .armed = game->options.edition == QA_Q2_RERELEASE && !sized,
      .visible = true, .expire_ns = q2_deadline(game->now_ns, q2_duration(lifetime)),
      .gib_flags = Q2_GIB_WIDOW | (organic ? 0 : Q2_GIB_METALLIC) |
                   (sized ? Q2_GIB_WIDOW_SIZED : 0) |
                   (hit_sound ? Q2_GIB_WIDOW_HIT_SOUND : 0)};
  actor->physics_bound = true;
  actor->physics = qa_physics_properties_default(QA_GAME_Q2);
  actor->physics.q2_rerelease = game->options.edition == QA_Q2_RERELEASE;
  actor->physics.motion = organic ? QA_PHYSICS_TOSS : QA_PHYSICS_BOUNCE;
  actor->physics.solid = sized ? QA_PHYSICS_BOX : QA_PHYSICS_NOT_SOLID;
  actor->physics.angular_velocity = angular;
  actor->physics.gravity_scale = sized ? .25f : 1;
  actor->physics.clip_mask = 3;
  if (!qa_world_link(game->services.world, id, NULL, error))
    return release_failed(game, id, error);
  if (!q2_actor_live(game, id)) return true;
  if (!q2_projectile_event(game, id, QA_BUILTIN_ANIMATION, model, 0, origin,
                           qa_v3(0, 0, 0), error))
    return release_failed(game, id, error);
  return true;
}

static bool pieces(qa_q2_game *game, qa_actor_id source, qa_vec3 point,
                    bool more, qa_error *error) {
  bool small = !more || game->options.cooperative;
  unsigned meat_count = small ? 2 : 1, heavy_count = small ? 1 : 2,
           light_count = small ? 1 : 3;
  for (unsigned i = 0; i < meat_count; ++i)
    if (!gib(game, source, meat, 300, true, &point, false, false, false, error)) return false;
  for (unsigned i = 0; i < heavy_count; ++i)
    if (!gib(game, source, metal, 300, false, &point, false, false, false, error)) return false;
  for (unsigned i = 0; i < light_count; ++i)
    if (!gib(game, source, metal, 100, false, &point, false, false, false, error)) return false;
  return true;
}

static bool spawn_legs(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context)) return true;
  qa_actor_definition definition;
  if (!qa_builtin_resource(&context->game->services, "widowlegs", &definition, error)) return false;
  if (!q2m_alive(context)) return true;
  qa_builtin_spawn spawn = {.owner = context->game->options.owner, .definition = definition,
      .body = {.origin = context->body.origin, .angles = context->body.angles}};
  qa_actor_id id;
  if (!qa_builtin_spawn_actor(&context->game->services, &spawn, &id, error)) return false;
  if (!q2_actor_live(context->game, id)) return true;
  if (!q2m_alive(context)) return qa_session_release(context->game->services.session, id, error);
  q2_actor *actor = q2_actor_get(context->game, id, true, error);
  if (!actor) return release_failed(context->game, id, error);
  actor->projectile = (q2_projectile){.kind = Q2_GIB, .classname = definition,
      .render_flags = 32768,
      .scale = context->game->options.edition == QA_Q2_RERELEASE ? 0 : 1,
      .alpha = 1, .visible = true,
      .gib_flags = Q2_GIB_WIDOW_LEGS,
      .next_ns = q2_deadline(context->game->now_ns, 100 * Q2_MS), .expire_ns = UINT64_MAX};
  actor->physics_bound = true;
  actor->physics = qa_physics_properties_default(QA_GAME_Q2);
  actor->physics.motion = QA_PHYSICS_STATIONARY;
  actor->physics.solid = QA_PHYSICS_NOT_SOLID;
  actor->physics.clip_mask = 0;
  if (!qa_world_link(context->game->services.world, id, NULL, error))
    return release_failed(context->game, id, error);
  if (!q2_actor_live(context->game, id)) return true;
  if (!q2_projectile_event(context->game, id, QA_BUILTIN_ANIMATION,
      "models/monsters/legs/tris.md2", 0, spawn.body.origin, spawn.body.angles, error))
    return release_failed(context->game, id, error);
  return true;
}

bool q2_widow_legs_think(qa_q2_game *game, q2_actor *actor, qa_error *error) {
  qa_actor_id id = actor->id;
  if (!actor->projectile.next_ns || game->now_ns < actor->projectile.next_ns) return true;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, id, &body, error)) return false;
  if (!q2_actor_live(game, id)) return true;
  if (actor->projectile.frame == 17) {
    qa_vec3 point = project(&body, qa_v3(11.77f, -7.24f, 23.31f));
    if (!effect(game, id, point, "q2:explosion1", 1, error) ||
        !pieces(game, id, point, false, error)) return false;
    if (!q2_actor_live(game, id)) return true;
  }
  if (actor->projectile.frame < 23) {
    ++actor->projectile.frame;
    actor->projectile.next_ns = q2_deadline(game->now_ns, 100 * Q2_MS);
    qa_builtin_event event = {.kind = QA_BUILTIN_ANIMATION, .family = QA_GAME_Q2,
        .provider = game->options.owner, .actor = id, .time_ns = game->now_ns,
        .resource = actor->projectile.model, .frame = actor->projectile.frame,
        .origin = body.origin, .end = body.angles};
    return qa_builtin_emit(&game->services, &event, error);
  }
  bool rr = game->options.edition == QA_Q2_RERELEASE;
  if (actor->projectile.delay == 0)
    actor->projectile.delay = rr ?
        (float)(q2_deadline(game->now_ns, Q2_NS) / Q2_MS) / 1000.0f :
        (float)((double)game->now_ns / (double)Q2_NS) + 1.0f;
  float wait = actor->projectile.delay;
  float classic_time = (float)((double)game->now_ns / (double)Q2_NS);
  bool expired = rr ? game->now_ns / Q2_MS > (uint64_t)(wait * 1000.0f) :
      classic_time > wait;
  if (expired) {
    const qa_vec3 offsets[] = {{-65.6f, -8.44f, 28.59f}, {-1.04f, -51.18f, 7.04f}};
    const char *const models[] = {"models/monsters/blackwidow/gib1/tris.md2",
        "models/monsters/blackwidow/gib2/tris.md2", "models/monsters/blackwidow/gib3/tris.md2"};
    for (unsigned i = 0; i < 2; ++i) {
      qa_vec3 point = project(&body, offsets[i]);
      if (!effect(game, id, point, "q2:explosion1", 1, error) ||
          !pieces(game, id, point, false, error)) return false;
      if (!q2_actor_live(game, id)) return true;
      for (unsigned j = 0; j < i + 2; ++j) {
        float damage = 80 + floorf(q2_random(game) * 20);
        if (!gib(game, id, models[j], damage, false, &point, true, false, true, error)) return false;
        if (!q2_actor_live(game, id)) return true;
      }
    }
    return !q2_actor_live(game, id) || qa_session_release(game->services.session, id, error);
  }
  float warning = wait - .5f;
  bool warning_due = rr ? warning < 0 ||
      game->now_ns / Q2_MS > (uint64_t)(warning * 1000.0f) : classic_time > warning;
  if (!actor->projectile.phase && warning_due) {
    actor->projectile.phase = 1;
    if (!effect(game, id, project(&body, qa_v3(31, -88.7f, 10.96f)), "q2:explosion1", 1, error) ||
        !effect(game, id, project(&body, qa_v3(-12.67f, -4.39f, 15.68f)), "q2:explosion1", 1, error)) return false;
    if (!q2_actor_live(game, id)) return true;
  }
  actor->projectile.next_ns = q2_deadline(game->now_ns, 100 * Q2_MS);
  return true;
}

typedef struct gib_touch_call { qa_q2_game *game; } gib_touch_call;
static bool sized_touch(void *opaque, qa_actor_id id, qa_error *error) {
  gib_touch_call *call = opaque;
  qa_q2_game *game = call->game;
  q2_actor *actor = q2_actor_get(game, id, false, error);
  if (!actor) return false;
  if (actor->projectile.armed) return true;
  qa_body_state body;
  if (!qa_world_body_read(game->services.world, id, &body, error)) return false;
  if (!q2_actor_live(game, id)) return true;
  actor->projectile.armed = true;
  actor->physics.solid = QA_PHYSICS_NOT_SOLID;
  actor->physics.angular_velocity = qa_v3(0, 0, 0);
  bool sound = (actor->projectile.gib_flags & Q2_GIB_WIDOW_HIT_SOUND) != 0;
  body.angles.x = body.angles.z = 0;
  if (!qa_world_body_write(game->services.world, id, &body, error)) return false;
  if (!q2_actor_live(game, id)) return true;
  if (!qa_world_set_collision(game->services.world, id, NULL, error)) return false;
  if (!qa_world_link(game->services.world, id, NULL, error)) return false;
  return !q2_actor_live(game, id) || !sound ||
      q2_projectile_event(game, id, QA_BUILTIN_SOUND, QA_Q2_SOUND_MISC_FHIT3, 2, body.origin, body.origin, error);
}

bool q2_widow_gib_touch(qa_q2_game *game, const qa_touch_contact *contact, qa_error *error) {
  gib_touch_call call = {game};
  return qa_q2_run_actor(game, contact->self, sized_touch, &call, error);
}

bool q2m_widow_explode(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context)) return true;
  qa_q2_game *game = context->game;
  qa_actor_id id = context->actor->id;
  int count = context->monster->count;
  qa_vec3 origin = context->body.origin, point = origin;
  point.z += (float)(24 + ((uint32_t)((double)q2_random(game) * 2147483648.0) & 15u));
  if (count < 8)
    point.z += (float)(24 + ((uint32_t)((double)q2_random(game) * 2147483648.0) & 31u));
  static const qa_vec3 offsets[] = {{-24,-24,0},{24,24,0},{24,-24,0},{-24,24,0},
      {-48,-48,0},{48,48,0},{-48,48,0},{48,-48,0},
      {18,18,48},{-18,18,48},{18,-18,48},{-18,-18,48}};
  if (count < 0 || count > 12) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Widow explosion continuation");
    return false;
  }
  if (count == 12) {
    if (!q2_projectile_event(game, id, QA_BUILTIN_STOP_SOUND, NULL, 0, origin, origin, error)) return false;
    if (!q2m_alive(context)) return true;
    if (!gib(game, id, meat, 400, true, NULL, false, false, true, error)) return false;
    for (unsigned i = 0; i < 2; ++i)
      if (!gib(game, id, metal, 100, false, NULL, false, false, true, error)) return false;
    for (unsigned i = 0; i < 2; ++i)
      if (!gib(game, id, metal, 400, false, NULL, false, false, true, error)) return false;
    if (!q2m_alive(context)) return true;
    context->monster->death_ns = 0;
    context->monster->dead = true;
    context->monster->next_frame_ns = q2m_after(game->now_ns, .1);
    return q2m_set_move(context, Q2M_MOVE_widow2_move_dead, false, error);
  }
  point = qa_vec_add(count < 8 ? point : origin, offsets[count]);
  if ((count == 1 || count == 7) && !pieces(game, id, point, false, error)) return false;
  if ((count == 3 || count == 8) && !pieces(game, id, point, true, error)) return false;
  if (count == 5 || count == 6) {
    qa_vec3 arm = project(&context->body, qa_v3(65.76f, 17.52f, 7.56f));
    if (count == 5) {
      if (!effect(game, id, arm, "q2:explosion1_big", 1, error)) return false;
      for (unsigned i = 0; i < 2; ++i)
        if (!gib(game, id, metal, 100, false, &arm, false, false, false, error)) return false;
    } else if (!gib(game, id, "models/monsters/blackwidow2/gib4/tris.md2", 200,
                      false, &arm, true, true, false, error) ||
               !gib(game, id, meat, 300, true, &arm, false, false, false, error)) return false;
  }
  if (!q2m_alive(context)) return true;
  ++context->monster->count;
  context->monster->death_ns = q2m_after(game->now_ns, .1);
  const char *name = count >= 8 ? "q2:explosion1_big" :
                     (count & 1) == 0 ? "q2:explosion1" : "q2:explosion1_np";
  return effect(game, id, point, name, 1, error);
}

bool q2m_widow_death_action(q2m_context *context, q2m_callback_id name,
                            bool *handled, qa_error *error) {
  *handled = true;
  if (name == Q2M_CALLBACK_WidowExplode) return q2m_widow_explode(context, error);
  if ((name == Q2M_CALLBACK_spawn_out_start) || (name == Q2M_CALLBACK_spawn_out_do)) {
    bool start = (name == Q2M_CALLBACK_spawn_out_start);
    if (start) context->monster->pause_ns = q2m_after(context->game->now_ns, 2);
    const qa_vec3 offsets[] = {{12.58f,-43.71f,68.88f},{3.43f,58.72f,68.41f}};
    for (unsigned i = 0; i < 2; ++i) {
      if (!effect(context->game, context->actor->id, project(&context->body, offsets[i]),
          start ? "q2:widowbeamout" : "q2:widowsplash", start ? 20001 + (int)i : 1, error)) return false;
      if (!q2m_alive(context)) return true;
    }
    if (start) return q2m_sound(context, QA_Q2_SOUND_MISC_BWIDOWBEAMOUT, 2, 1, error);
    qa_vec3 point = context->body.origin; point.z += 36;
    if (!effect(context->game, context->actor->id, point, "q2:bosstport", 1, error) ||
        !spawn_legs(context, error)) return false;
    return !q2m_alive(context) || q2m_release(context, error);
  }
  static const qa_vec3 offsets[] = {{23.74f,-37.67f,76.96f},{-20.49f,36.92f,73.52f},
      {2.11f,.05f,92.20f},{-28.04f,-35.57f,-77.56f},{-20.11f,-1.11f,40.76f},
      {-20.11f,-1.11f,40.76f},{-20.11f,-1.11f,40.76f}};
  if (name >= Q2M_CALLBACK_WidowExplosion1 && name <= Q2M_CALLBACK_WidowExplosion7) {
    qa_vec3 point = project(&context->body, offsets[name - Q2M_CALLBACK_WidowExplosion1]);
    qa_actor_id id = context->actor->id;
    if (!effect(context->game, id, point, "q2:explosion1", 1, error) ||
        !gib(context->game, id, meat, 300, true, &point, false, false, false, error) ||
        !gib(context->game, id, metal, 100, false, &point, false, false, false, error)) return false;
    for (unsigned i = 0; i < 2; ++i)
      if (!gib(context->game, id, metal, 300, false, &point, false, false, false, error)) return false;
    return true;
  }
  if (name == Q2M_CALLBACK_WidowExplosionLeg) {
    const qa_vec3 leg_offsets[] = {{-31.89f,-47.86f,67.02f},{-44.9f,-82.14f,54.72f}};
    const char *const models[] = {"models/monsters/blackwidow2/gib2/tris.md2",
                                  "models/monsters/blackwidow2/gib1/tris.md2"};
    for (unsigned i = 0; i < 2; ++i) {
      qa_vec3 point = project(&context->body, leg_offsets[i]);
      qa_actor_id id = context->actor->id;
      if (!effect(context->game, id, point, i ? "q2:explosion1" : "q2:explosion1_big", 1, error) ||
          !gib(context->game, id, models[i], i ? 300 : 200, false, &point, true, true, false, error) ||
          !gib(context->game, id, meat, 300, true, &point, false, false, false, error) ||
          !gib(context->game, id, metal, 100, false, &point, false, false, false, error)) return false;
      if (!q2m_alive(context)) return true;
    }
    return true;
  }
  *handled = false;
  return true;
}

bool q2m_widow_death_gibs(q2m_context *context, float damage, qa_error *error) {
  static const struct { const char *model; unsigned count; bool organic, sized, sound; } recipe[] = {
      {"models/objects/gibs/bone/tris.md2",2,true,false,false},
      {"models/objects/gibs/sm_meat/tris.md2",3,true,false,false},
      {"models/monsters/blackwidow2/gib1/tris.md2",1,false,true,false},
      {"models/monsters/blackwidow2/gib2/tris.md2",1,false,true,true},
      {"models/monsters/blackwidow2/gib1/tris.md2",1,false,true,false},
      {"models/monsters/blackwidow2/gib2/tris.md2",1,false,true,true},
      {"models/monsters/blackwidow2/gib1/tris.md2",1,false,true,false},
      {"models/monsters/blackwidow2/gib2/tris.md2",1,false,true,true},
      {"models/monsters/blackwidow2/gib3/tris.md2",1,false,true,false},
      {"models/monsters/blackwidow/gib3/tris.md2",1,false,true,false},
      {"models/monsters/blackwidow2/gib3/tris.md2",1,false,true,false},
      {"models/monsters/blackwidow/gib3/tris.md2",1,false,true,false}};
  qa_actor_id id = context->actor->id;
  for (unsigned i = 0; i < sizeof(recipe) / sizeof(recipe[0]); ++i)
    for (unsigned j = 0; j < recipe[i].count; ++j)
      if (!gib(context->game, id, recipe[i].model, damage, recipe[i].organic,
                 NULL, recipe[i].sized, recipe[i].sound, false, error)) return false;
  if (!q2m_alive(context)) return true;
  if (!q2_spawn_gib(context->game, id, "models/objects/gibs/chest/tris.md2", damage,
                     0, 0, 1, error)) return false;
  return !q2m_alive(context) || q2_spawn_gib(context->game, id,
      "models/objects/gibs/head2/tris.md2", damage, Q2_GIB_HEAD, 0, 1, error);
}
