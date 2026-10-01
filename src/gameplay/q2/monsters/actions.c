#include "internal.h"
#include "reinforcements.h"
#include "medic.h"
#include "qa/game_q2_entities.h"
#include "qa/game_q2_combat.h"

typedef struct q2m_transition {
  const char *callback;
  const char *move;
} q2m_transition;

static const q2m_transition transitions[] = {
    {"boss2_attack_mg", "boss2_move_attack_mg"},
    {"carrier_attack_mg", "carrier_move_attack_mg"},
    {"chick_attack1", "chick_move_attack1"},
    {"chick_slash", "chick_move_slash"},
    {"fixbot_attack", "fixbot_move_attack1"},
    {"flipper_run_loop", "flipper_move_run_loop"},
    {"flyer_check_melee", "flyer_move_start_melee"},
    {"flyer_kamikaze", "flyer_move_kamikaze"},
    {"flyer_loop_melee", "flyer_move_loop_melee"},
    {"gekk_chant", "gekk_move_chant"},
    {"gekk_face", "gekk_move_attack"},
    {"gekk_run_start", "gekk_move_run_start"},
    {"gekk_swim_loop", "gekk_move_swim_loop"},
    {"guardian_atk1_finish", "guardian_atk1_out"},
    {"guardian_atk2", "guardian_move_atk2_fire"},
    {"guardian_atk2_out", "guardian_move_atk2_out"},
    {"guncmdr_fire_chain", "guncmdr_move_fire_chain"},
    {"guncmdr_grenade_back_dodge_resume", "guncmdr_move_attack_grenade_back"},
    {"guncmdr_grenade_mortar_resume", "guncmdr_move_attack_mortar"},
    {"gunner_fire_chain", "gunner_move_fire_chain"},
    {"hover_attack", "hover_move_attack1"},
    {"jorg_attack1", "jorg_move_attack1"},
    {"parasite_do_fidget", "parasite_move_fidget"},
    {"parasite_refidget", "parasite_move_start_fidget"},
    {"parasite_start_run", "parasite_move_run"},
    {"soldier_blind", "soldier_move_blind"},
    {"stalker_false_death", "stalker_move_false_death"},
    {"tank_doattack_rocket", "tank_move_attack_fire_rocket"},
    {"tank_poststrike", "tank_move_run"},
};

static bool trace_point(q2m_context *, qa_vec3, qa_vec3, uint32_t,
                        qa_trace_result *, qa_error *);
static bool blind_direction(q2m_context *, qa_vec3, qa_vec3, float, uint32_t,
                            qa_vec3 *, bool *, qa_error *);
static bool carrier_coop_check(q2m_context *, qa_error *);
static bool gunner_grenade(q2m_context *, qa_error *);
static bool mortar_direction(q2m_context *, qa_vec3, qa_vec3, qa_vec3, float,
                             float, bool, qa_vec3 *, bool *, qa_error *);

static bool trace_hit_brush(q2m_context *context,
                            const qa_trace_result *trace) {
  if (trace->hit != QA_TRACE_HIT_ACTOR ||
      context->game->services.physics == NULL ||
      context->game->services.physics->services.read == NULL)
    return false;
  qa_physics_properties properties;
  return context->game->services.physics->services.read(
             context->game->services.physics->services.context, trace->actor,
             &properties) &&
         properties.solid == QA_PHYSICS_BRUSH;
}

static bool has(const char *value, const char *part) {
  return strstr(value, part) != NULL;
}

static bool ends_with(const char *value, const char *suffix) {
  size_t length = strlen(value), tail = strlen(suffix);
  return tail <= length && strcmp(value + length - tail, suffix) == 0;
}

static bool set_existing_move(q2m_context *context, const char *name,
                              bool immediate, bool *found, qa_error *error) {
  *found = q2m_move_named(context->monster, name) != NULL;
  return !*found || q2m_set_move(context, name, immediate, error);
}

static bool set_definition_move(q2m_context *context, const char *name,
                                qa_error *error) {
  return name == NULL || q2m_set_move(context, name, false, error);
}

static bool enemy_alive(q2m_context *context) {
  qa_combat_state combat;
  qa_error ignored = {0};
  return context->monster->enemy.registry != 0 &&
         qa_combat_read(context->game->services.combat, context->monster->enemy,
                        &combat, &ignored) &&
         combat.health > 0.0f;
}

static bool save_enemy_location(q2m_context *context) {
  qa_body_state enemy;
  qa_error ignored = {0};
  if (context->monster->enemy.registry != 0 &&
      qa_world_body_read(context->game->services.world, context->monster->enemy,
                         &enemy, &ignored)) {
    context->monster->last_sighting = enemy.origin;
    context->monster->saved_goal = enemy.origin;
    context->monster->has_saved_goal = true;
    context->monster->blind_fire_target = enemy.origin;
  }
  return true;
}

static bool save_widow_disrupt_location(q2m_context *context) {
  struct qa_q2_monster *monster = context->monster;
  monster->saved_attack_position = qa_v3(0, 0, 0);
  if (monster->enemy.registry == 0)
    return true;

  qa_body_state enemy;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world, monster->enemy, &enemy,
                          &ignored))
    return true;

  qa_builtin_actor_traits traits = {.view_height = 22.0f};
  if (context->game->services.actor_traits != NULL) {
    qa_builtin_actor_traits shared = {0};
    if (context->game->services.actor_traits(context->game->services.context,
                                             monster->enemy, &shared))
      traits = shared;
  }
  if (!q2m_alive(context))
    return true;
  monster->saved_attack_position =
      qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height));
  return true;
}

static bool toss_makron(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  if (context->game->options.edition == QA_Q2_CLASSIC)
    return q2m_schedule_makron_spawn(context, error);

  const qa_actor_id jorg = context->actor->id;
  const qa_actor_id enemy = context->monster->enemy;
  qa_actor_definition definition;
  if (!qa_builtin_resource(&context->game->services, "monster_makron",
                           &definition, error))
    return false;

  qa_builtin_spawn spawn = {
      .owner = context->game->options.owner,
      .definition = definition,
      .body = {.origin = context->body.origin, .angles = context->body.angles},
      .link = false,
  };
  qa_actor_id child;
  if (!qa_builtin_spawn_actor(&context->game->services, &spawn, &child, error))
    return false;

  qa_q2_monster_spawn_options options = {
      .classname = "monster_makron",
      .scale = 1.0f,
      .health_multiplier = 1.0f,
      .enemy = enemy,
  };
  if (!qa_q2_monster_spawn(context->game, child, &options, error)) {
    qa_error original = error != NULL ? *error : (qa_error){0};
    qa_error ignored = {0};
    qa_session_release(context->game->services.session, child, &ignored);
    if (error != NULL)
      *error = original;
    return false;
  }

  if (!q2_actor_live(context->game, child))
    return true;
  q2_actor *child_actor = context->game->actors[child.slot];
  if (child_actor == NULL || !qa_actor_id_equal(child_actor->id, child) ||
      child_actor->monster == NULL)
    return true;

  q2m_context resumed = {
      .game = context->game,
      .actor = child_actor,
      .monster = child_actor->monster,
  };
  if (!q2m_refresh(&resumed, error))
    return false;

  qa_body_state target;
  qa_combat_state target_combat;
  qa_error ignored = {0};
  qa_actor_id selected = enemy;
  bool has_target = selected.registry != 0 &&
                    qa_world_body_read(context->game->services.world, selected,
                                       &target, &ignored) &&
                    qa_combat_read(context->game->services.combat, selected,
                                   &target_combat, &ignored) &&
                    target_combat.health > 0.0f;
  if (!has_target) {
    selected = q2m_current_sight_client(context->game);
    has_target = selected.registry != 0 &&
                 qa_world_body_read(context->game->services.world, selected,
                                    &target, &ignored) &&
                 qa_combat_read(context->game->services.combat, selected,
                                &target_combat, &ignored) &&
                 target_combat.health > 0.0f;
  }
  if (has_target) {
    qa_vec3 difference = qa_vec_sub(target.origin, resumed.body.origin);
    qa_vec3 direction = qa_vec_normalize(difference);
    resumed.body.angles.y = qa_builtin_angle_mod(
        atan2f(difference.y, difference.x) * 57.29577951308232f);
    resumed.body.velocity = qa_vec_scale(direction, 400.0f);
    resumed.body.velocity.z = 200.0f;
    resumed.body.ground = (qa_actor_id){0};
    if (!q2m_write_body(&resumed, true, error))
      return false;
    if (!q2m_alive(&resumed))
      return true;
    resumed.monster->enemy = selected;
    if (!q2m_found_target(&resumed, selected, error))
      return false;
  }
  if (!q2m_alive(&resumed))
    return true;
  if (!q2m_set_move(&resumed, "makron_move_sight", true, error))
    return false;
  if (q2m_alive(&resumed)) {
    resumed.monster->frame = resumed.monster->move->first_frame;
    resumed.monster->next_frame = resumed.monster->frame;
  }

  return context->game->options.edition != QA_Q2_RERELEASE ||
         qa_q2_healthbar_transfer(context->game, jorg, child, error);
}

static bool publish_duck(q2m_context *context, bool down, qa_error *error) {
  context->monster->ducked = down;
  context->body.bounds.maxs.z = context->monster->normal_height - (down ? 32.0f : 0.0f);
  if (!q2m_damageable(context, true, error))
    return false;
  return !q2m_alive(context) || q2m_write_body(context, true, error);
}

static bool set_duck(q2m_context *context, bool down, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (!down && !monster->ducked &&
      !(context->game->options.edition == QA_Q2_CLASSIC &&
        monster->definition->species == Q2M_GUN_COMMANDER))
    return true;
  return publish_duck(context, down, error);
}

static bool duck_action(q2m_context *context, const char *callback,
                        qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  bool commander = m->definition->species == Q2M_GUN_COMMANDER;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (has(callback, "duck_down")) {
    if (!rerelease && (commander || !strcmp(callback, "monster_duck_down"))) {
      if (m->duck_ns < context->game->now_ns)
        m->duck_ns = q2m_after(context->game->now_ns, 1);
    } else {
      m->next_duck_ns = q2m_after(context->game->now_ns, 5.0);
    }
    return set_duck(context, true, error);
  }
  if (has(callback, "duck_hold")) {
    context->monster->hold_frame =
        context->game->now_ns < context->monster->duck_ns;
    return true;
  }
  bool source_up = !strcmp(callback, "monster_duck_up");
  if ((commander || source_up) && (!rerelease || m->ducked)) {
    if (!rerelease) m->next_duck_ns = q2m_after(context->game->now_ns, 5);
    else if (m->next_duck_ns > context->game->now_ns)
      m->next_duck_ns = context->game->now_ns + (m->next_duck_ns - context->game->now_ns) / 2;
  }
  if (source_up && !rerelease)
    return publish_duck(context, false, error);
  return set_duck(context, false, error);
}

static bool jump_action(q2m_context *context, const char *callback,
                        qa_error *error) {
  if (has(callback, "wait_land") || has(callback, "check_landing")) {
    if (context->body.ground.registry != 0 ||
        context->game->now_ns >= context->monster->jump_ns)
      context->monster->next_frame = context->monster->frame + 1;
    else
      context->monster->next_frame = context->monster->frame;
    return true;
  }
  qa_vec3 forward, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, &up);
  bool high = has(callback, "jump2") || has(callback, "straightup") ||
              has(callback, "jump_up");
  float forward_speed = high ? 150.0f : 100.0f;
  float upward_speed = high ? 400.0f : 300.0f;
  if (has(callback, "jump_down"))
    upward_speed = 100.0f;
  context->body.velocity = qa_vec_add(
      context->body.velocity, qa_vec_add(qa_vec_scale(forward, forward_speed),
                                         qa_vec_scale(up, upward_speed)));
  context->body.ground = (qa_actor_id){0};
  context->monster->jump_ns = q2m_after(context->game->now_ns, 3.0);
  context->actor->physics.motion = QA_PHYSICS_STEP;
  return q2m_write_body(context, true, error);
}

static bool shrink(q2m_context *context, qa_error *error) {
  context->body.bounds.maxs.z = fminf(context->body.bounds.maxs.z,
                                      -4.0f * context->monster->entity_scale);
  return q2m_write_body(context, true, error);
}

static bool footstep(q2m_context *context, const char *callback,
                     qa_error *error) {
  if (context->body.ground.registry == 0)
    return true;
  const char *path = has(callback, "arachnid") ? "insane/insane11.wav"
                     : has(callback, "Tread")  ? "tank/step.wav"
                                               : "player/step1.wav";
  return q2m_sound(context, path, 4, 1.0f, error);
}

static bool simple_sound(q2m_context *context, const char *callback,
                         qa_error *error) {
  const char *path = "misc/talk.wav";
  int channel = 2;
  float attenuation = 1.0f;
  if (has(callback, "cock") || has(callback, "Reload") ||
      has(callback, "opengun")) {
    path = has(callback, "infantry") ? "infantry/infatck3.wav"
                                     : "weapons/shotgr1b.wav";
    channel = 1;
  } else if (has(callback, "swing") || has(callback, "charge") ||
             has(callback, "windup") || has(callback, "PreAttack")) {
    path = context->monster->definition->species == Q2M_BERSERK
               ? "berserk/attack.wav"
           : context->monster->definition->species == Q2M_SHAMBLER
               ? "shambler/sattck1.wav"
               : "gladiator/melee3.wav";
    channel = 1;
  } else if (has(callback, "idle") || has(callback, "fidget") ||
             has(callback, "Moan") || has(callback, "moan")) {
    path = context->monster->definition->species == Q2M_INFANTRY
               ? "infantry/infidle1.wav"
           : context->monster->definition->species == Q2M_GUNNER
               ? "gunner/gunidle1.wav"
           : context->monster->definition->species == Q2M_GEKK
               ? "gek/gk_idle1.wav"
               : "insane/insane11.wav";
    attenuation = 2.0f;
  } else if (has(callback, "scream")) {
    path = "insane/insane5.wav";
  } else if (has(callback, "tap") || has(callback, "scratch") ||
             has(callback, "break_noise") || has(callback, "break_sound")) {
    path = "parasite/paridle1.wav";
  } else if (has(callback, "thud")) {
    path = "tank/thud.wav";
  }
  return q2m_sound(context, path, channel, attenuation, error);
}

bool q2m_weapon_sound(q2m_context *context, const char *path, qa_error *error) {
  qa_string_id resource = context->monster->weapon_sound;
  if (path && !qa_builtin_resource(&context->game->services, path, &resource, error))
    return false;
  if (!path && !resource)
    return true;
  context->monster->weapon_sound = path ? resource : 0;
  qa_builtin_event event = {
      .kind = path ? QA_BUILTIN_SOUND : QA_BUILTIN_STOP_SOUND,
      .family = QA_GAME_Q2,
      .provider = context->game->options.owner,
      .actor = context->actor->id,
      .other = context->monster->enemy,
      .time_ns = context->game->now_ns,
      .origin = context->body.origin,
      .volume = 1.0f,
      .attenuation = 1.0f,
      .channel = 0,
      .resource = resource,
      .flags = path ? 1u : 0u,
      .frame = context->monster->frame,
  };
  return qa_builtin_emit(&context->game->services, &event, error);
}

bool q2m_jorg_sound_end(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context) || !context->monster->weapon_sound) return true;
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      !q2m_sound(context, "boss3/bs3atck1_end.wav", 1, 1, error)) return false;
  return !q2m_alive(context) || q2m_weapon_sound(context, NULL, error);
}

bool q2m_soldier_sound_end(q2m_context *context, qa_error *error) {
  if (context->game->options.edition != QA_Q2_RERELEASE ||
      !context->monster->weapon_sound)
    return true;
  if (context->monster->count >= 2 && context->monster->count < 4 &&
      !q2m_sound(context, "weapons/hyprbd1a.wav", 0, 1.0f, error))
    return false;
  return !q2m_alive(context) || q2m_weapon_sound(context, NULL, error);
}

static bool soldier_laser_sound(q2m_context *context, bool start,
                                qa_error *error) {
  if (context->game->options.edition != QA_Q2_RERELEASE)
    return true;
  if (!start)
    return q2m_soldier_sound_end(context, error);
  if (context->monster->style != 1 || context->monster->count < 2 ||
      context->monster->count >= 4)
    return true;
  return q2m_weapon_sound(context, "weapons/hyprbl1a.wav", error);
}

static q2m_attack_kind attack_kind(q2m_context *context, const char *callback) {
  if (strcmp(callback, "Boss2HyperBlaster") == 0 ||
      strcmp(callback, "MakronHyperblaster") == 0)
    return Q2M_ATTACK_BLASTER;
  if (strcmp(callback, "GunnerCmdrFire") == 0)
    return Q2M_ATTACK_FLECHETTE;
  if (has(callback, "Disrupt") || has(callback, "disrupt"))
    return Q2M_ATTACK_TRACKER;
  if (has(callback, "BFG"))
    return Q2M_ATTACK_BFG;
  if (has(callback, "Rail") || has(callback, "rail"))
    return Q2M_ATTACK_RAIL;
  if (has(callback, "Rocket") || has(callback, "rocket"))
    return Q2M_ATTACK_ROCKET;
  if (has(callback, "Grenade") || has(callback, "Gren") ||
      has(callback, "gren"))
    return Q2M_ATTACK_GRENADE;
  if (has(callback, "Beam") || has(callback, "beam") ||
      has(callback, "laser") || has(callback, "Laser") ||
      has(callback, "Lightning") || has(callback, "lightning") ||
      has(callback, "zap") || has(callback, "welder"))
    return Q2M_ATTACK_BEAM;
  if (has(callback, "Melee") || has(callback, "melee_hit") ||
      has(callback, "_hit") || has(callback, "_bite") ||
      has(callback, "_wham") || has(callback, "_slash") ||
      has(callback, "_smack") || has(callback, "_strike") ||
      has(callback, "_kick") || has(callback, "Claw") ||
      has(callback, "smash") || has(callback, "tentacle") ||
      has(callback, "tounge") || has(callback, "_fist") ||
      has(callback, "Crunch") || has(callback, "Tongue") ||
      has(callback, "melee") || has(callback, "Strike") ||
      has(callback, "Slash") || has(callback, "attack_club") ||
      has(callback, "attack_spike") || has(callback, "swing_attack"))
    return Q2M_ATTACK_HIT;
  if (has(callback, "Shotgun"))
    return Q2M_ATTACK_SHOTGUN;
  if (has(callback, "MachineGun") || has(callback, "firebullet"))
    return Q2M_ATTACK_BULLET;
  if (has(callback, "Blaster") || has(callback, "blaster") ||
      has(callback, "Hyper") || has(callback, "hyper") ||
      has(callback, "loogie") || has(callback, "fireleft") ||
      has(callback, "fireright") || has(callback, "shoot_attack") ||
      strcmp(callback, "actor_fire") == 0)
    return context->monster->definition->primary != Q2M_ATTACK_NONE
               ? context->monster->definition->primary
               : Q2M_ATTACK_BLASTER;
  if (has(callback, "GunnerFire") || has(callback, "TurretFire") ||
      has(callback, "infantry_fire") || has(callback, "soldier_fire") ||
      has(callback, "soldierh_fire") || has(callback, "ripper"))
    return context->monster->definition->primary;
  if (ends_with(callback, "Gun"))
    return context->monster->definition->primary;
  return Q2M_ATTACK_NONE;
}

static float attack_damage(q2m_context *context, q2m_attack_kind kind) {
  const q2m_definition *definition = context->monster->definition;
  if (kind == definition->primary && definition->primary_damage > 0.0f)
    return definition->primary_damage;
  if (kind == definition->secondary && definition->secondary_damage > 0.0f)
    return definition->secondary_damage;
  switch (kind) {
  case Q2M_ATTACK_HIT:
    return 20.0f;
  case Q2M_ATTACK_BULLET:
    return 3.0f;
  case Q2M_ATTACK_SHOTGUN:
    return 4.0f;
  case Q2M_ATTACK_ROCKET:
  case Q2M_ATTACK_GRENADE:
  case Q2M_ATTACK_RAIL:
  case Q2M_ATTACK_BFG:
    return 50.0f;
  case Q2M_ATTACK_TRACKER:
    return 20.0f;
  case Q2M_ATTACK_ION:
  case Q2M_ATTACK_BLUE_BOLT:
  case Q2M_ATTACK_GREEN_BOLT:
  case Q2M_ATTACK_PLASMA:
  case Q2M_ATTACK_FLECHETTE:
  case Q2M_ATTACK_BEAM:
  case Q2M_ATTACK_HEAT:
    return 15.0f;
  default:
    return 5.0f;
  }
}

static bool visible_enemy(q2m_context *context, bool *visible,
                          qa_error *error) {
  *visible = false;
  if (!enemy_alive(context))
    return true;
  return q2m_visible(context, context->monster->enemy, visible, error);
}

qa_vec3 q2m_vector_angles(qa_vec3 direction) {
  float yaw = direction.x == 0.0f && direction.y == 0.0f
                  ? 0.0f
                  : atan2f(direction.y, direction.x) * 57.29577951308232f;
  if (yaw < 0.0f)
    yaw += 360.0f;
  return qa_v3(-atan2f(direction.z, hypotf(direction.x, direction.y)) *
                   57.29577951308232f,
               yaw, 0.0f);
}

static qa_vec3 spread_direction(q2m_context *context, qa_vec3 direct,
                                float horizontal, float vertical) {
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(q2m_vector_angles(direct), &forward, &right, &up);
  float r = q2m_crandom(context->game) * horizontal;
  float u = q2m_crandom(context->game) * vertical;
  return qa_vec_normalize(
      qa_vec_add(qa_vec_scale(forward, 8192.0f),
                 qa_vec_add(qa_vec_scale(right, r), qa_vec_scale(up, u))));
}

static bool enemy_view_height(q2m_context *context, float *height) {
  *height = 22.0f;
  if (context->monster->enemy.registry == 0 ||
      context->game->services.actor_traits == NULL)
    return true;
  qa_builtin_actor_traits traits = {0};
  if (context->game->services.actor_traits(context->game->services.context,
                                           context->monster->enemy, &traits))
    *height = traits.view_height;
  return q2m_alive(context);
}

static void exact_projectile_speed(q2m_fire_spec *spec, float speed) {
  spec->speed = speed;
  if (spec->kind == Q2M_ATTACK_ROCKET || spec->kind == Q2M_ATTACK_HEAT ||
      spec->kind == Q2M_ATTACK_PLASMA || spec->kind == Q2M_ATTACK_FLECHETTE)
    spec->fuse = speed > 0.0f ? 8000.0f / speed : 0.0f;
}

static bool fire_source_exact(q2m_context *context, q2m_attack_kind kind,
                              float damage, int flash, float lead, float speed,
                              bool has_effects, uint64_t effects,
                              qa_error *error) {
  qa_vec3 start, direction;
  bool available;
  if (!q2m_source_shot(context, flash, lead, &start, &direction, &available,
                       error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  q2m_fire_spec spec =
      q2m_fire_default(context, kind, damage, flash, start, direction);
  if (speed > 0.0f)
    exact_projectile_speed(&spec, speed);
  spec.has_projectile_effects = has_effects;
  spec.projectile_effects = effects;
  return q2m_fire(context, &spec, error);
}

static bool fire_predict_exact(q2m_context *context, q2m_attack_kind kind,
                               float damage, int flash, float speed, bool eye,
                               float offset, qa_error *error) {
  qa_vec3 start, direction;
  bool available;
  if (!q2m_predict_shot(context, flash, speed, eye, offset, &start, &direction,
                        &available, error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  q2m_fire_spec spec =
      q2m_fire_default(context, kind, damage, flash, start, direction);
  exact_projectile_speed(&spec, speed);
  return q2m_fire(context, &spec, error);
}

static bool fire_forward_exact(q2m_context *context, q2m_attack_kind kind,
                               float damage, int flash, float speed,
                               qa_error *error) {
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_vec3 direction;
  qa_builtin_angle_vectors(context->body.angles, &direction, NULL, NULL);
  q2m_fire_spec spec =
      q2m_fire_default(context, kind, damage, flash, start, direction);
  if (speed > 0.0f)
    exact_projectile_speed(&spec, speed);
  return q2m_fire(context, &spec, error);
}

static bool target_body(q2m_context *context, qa_body_state *body,
                        qa_builtin_actor_traits *traits, bool *available) {
  *available = false;
  *traits = (qa_builtin_actor_traits){.view_height = 22.0f};
  if (context->monster->enemy.registry == 0)
    return true;
  qa_error ignored = {0};
  if (!qa_world_body_read(context->game->services.world,
                          context->monster->enemy, body, &ignored))
    return true;
  if (context->game->services.actor_traits != NULL) {
    qa_builtin_actor_traits shared = {0};
    if (context->game->services.actor_traits(context->game->services.context,
                                             context->monster->enemy, &shared))
      *traits = shared;
  }
  if (!q2m_alive(context))
    return true;
  *available = true;
  return true;
}

static bool project_flash_angles(q2m_context *context, int flash,
                                 qa_vec3 angles, qa_vec3 *start,
                                 qa_error *error) {
  qa_vec3 offset;
  if (!q2m_muzzle_offset(context, flash, &offset, error))
    return false;
  float scale = context->game->options.edition == QA_Q2_RERELEASE
                    ? context->monster->entity_scale
                    : 1.0f;
  qa_vec3 forward, right;
  qa_builtin_angle_vectors(angles, &forward, &right, NULL);
  *start = qa_vec_add(
      context->body.origin,
      qa_vec_add(qa_vec_scale(forward, offset.x * scale),
                 qa_vec_add(qa_vec_scale(right, offset.y * scale),
                            qa_v3(0.0f, 0.0f, offset.z * scale))));
  return true;
}

static bool fire_bullet_exact(q2m_context *context, int flash, qa_vec3 start,
                              qa_vec3 direction, float damage,
                              float horizontal_spread, float vertical_spread,
                              qa_error *error) {
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_BULLET, damage, flash, start, direction);
  spec.horizontal_spread = horizontal_spread;
  spec.vertical_spread = vertical_spread;
  return q2m_fire(context, &spec, error);
}

static bool fire_rocket_exact(q2m_context *context, q2m_attack_kind kind,
                              int flash, qa_vec3 start, qa_vec3 direction,
                              float damage, float speed, float radius,
                              float radius_damage, qa_error *error) {
  q2m_fire_spec spec =
      q2m_fire_default(context, kind, damage, flash, start, direction);
  exact_projectile_speed(&spec, speed);
  spec.radius = radius;
  spec.radius_damage = radius_damage;
  return q2m_fire(context, &spec, error);
}

static bool fire_bfg_exact(q2m_context *context, int flash, float radius,
                           qa_error *error) {
  qa_vec3 start, direction;
  bool available;
  if (!q2m_source_shot(context, flash, 0.0f, &start, &direction, &available,
                       error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_BFG, 50.0f, flash, start, direction);
  spec.speed = 300.0f;
  spec.radius = radius;
  spec.radius_damage = 50.0f;
  return q2m_fire(context, &spec, error);
}

static bool soldier_aim(q2m_context *context, int flash, unsigned index,
                        bool angle_limited, bool rogue, bool heavy,
                        qa_vec3 *start, qa_vec3 *direction, bool *available,
                        qa_error *error) {
  *available = false;
  if (!q2m_project_flash(context, flash, start, error))
    return false;
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  if (index == 5 || index == 6) {
    if (!heavy && !rogue &&
        context->game->options.edition == QA_Q2_RERELEASE &&
        (context->monster->spawnflags & UINT32_C(65536)) != 0)
      return true;
    *direction = forward;
    *available = true;
    return true;
  }

  qa_vec3 direct;
  bool target;
  if (!q2m_source_shot(context, flash, 0.0f, start, &direct, &target, error))
    return false;
  if (!target || !q2m_alive(context))
    return true;
  if (context->monster->attack_state == Q2M_BLIND) {
    float height;
    if (!enemy_view_height(context, &height) || !q2m_alive(context))
      return true;
    qa_vec3 point = context->monster->blind_fire_target;
    point.z += height;
    direct = qa_vec_normalize(qa_vec_sub(point, *start));
  }
  float minimum_dot = rogue ? 0.9f : 0.5f;
  if (angle_limited && qa_vec_dot(direct, forward) < minimum_dot) {
    if (!rogue)
      context->monster->hold_frame =
          context->game->now_ns < context->monster->fire_ns;
    return true;
  }
  float horizontal = heavy ? 100.0f
                     : rogue && context->game->options.skill >= 2 ? 500.0f
                                                                  : 1000.0f;
  *direction = spread_direction(context, direct, horizontal, horizontal * 0.5f);
  if (rogue) {
    qa_body_state enemy;
    qa_error ignored = {0};
    if (!qa_world_body_read(context->game->services.world,
                            context->monster->enemy, &enemy, &ignored))
      return true;
    float height;
    if (!enemy_view_height(context, &height) || !q2m_alive(context))
      return true;
    qa_trace_query query = {
        .start = *start,
        .end = qa_vec_add(enemy.origin, qa_v3(0.0f, 0.0f, height)),
        .pass_actor = context->actor->id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    query.policy.contents_mask = UINT32_C(0x06000003);
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (trace.hit == QA_TRACE_HIT_ACTOR &&
        !qa_actor_id_equal(trace.actor, context->monster->enemy))
      return true;
  }
  *available = true;
  return true;
}

static bool soldier_fire_exact(q2m_context *context, unsigned index,
                               bool angle_limited, qa_error *error) {
  static const int blaster_flashes[] = {39, 40, 83, 86, 89,
                                        92, 95, 98, 251};
  static const int shotgun_flashes[] = {41, 42, 84, 87, 90,
                                        93, 96, 99, 252};
  static const int machinegun_flashes[] = {43, 44, 85, 88, 91,
                                           94, 97, 100, 253};
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  bool heavy = species == Q2M_SOLDIER_RIPPER ||
               species == Q2M_SOLDIER_HYPER || species == Q2M_SOLDIER_LASER;
  bool rogue = !heavy && context->game->options.product == QA_Q2_ROGUE;
  if (index >= sizeof(blaster_flashes) / sizeof(blaster_flashes[0]) ||
      (heavy && index >= 8))
    return true;
  const int *flashes = species == Q2M_SOLDIER_LIGHT ||
                               species == Q2M_SOLDIER_RIPPER ||
                               species == Q2M_SOLDIER_HYPER
                           ? blaster_flashes
                       : species == Q2M_SOLDIER ? shotgun_flashes
                                                : machinegun_flashes;
  int flash = flashes[index];
  qa_vec3 start, direction;
  bool available;
  if (!soldier_aim(context, flash, index, angle_limited, rogue, heavy, &start,
                   &direction, &available, error))
    return false;
  if (!available || !q2m_alive(context))
    return true;

  if (species == Q2M_SOLDIER_LASER) {
    if (!monster->hold_frame) {
      unsigned tenths =
          3u + (unsigned)floorf(q2m_random(context->game) * 8.0f);
      monster->pause_ns =
          q2m_after(context->game->now_ns, (double)tenths * 0.1);
    }
    if (q2m_random(context->game) > 0.8f &&
        !q2m_sound(context, "misc/lasfly.wav", 0, 3.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_body_state enemy;
    qa_error ignored = {0};
    if (monster->enemy.registry == 0 ||
        !qa_world_body_read(context->game->services.world, monster->enemy,
                            &enemy, &ignored))
      return true;
    qa_vec3 aim_forward, aim_right, aim_up;
    qa_builtin_angle_vectors(
        q2m_vector_angles(qa_vec_sub(enemy.origin, context->body.origin)),
        &aim_forward, &aim_right, &aim_up);
    qa_vec3 muzzle_offset;
    if (!q2m_muzzle_offset(context, flash, &muzzle_offset, error))
      return false;
    qa_vec3 origin = qa_vec_add(
        context->body.origin,
        qa_vec_add(
            qa_vec_scale(aim_right,
                         muzzle_offset.x + (flash == 85 ? -14.0f : 2.0f)),
            qa_vec_add(qa_vec_scale(aim_up, muzzle_offset.z + 8.0f),
                       qa_vec_scale(aim_forward, muzzle_offset.y))));
    if (!q2m_spawn_monster_beam(context, monster->enemy, origin, aim_forward,
                                1.0f, false, error))
      return false;
    if (q2m_alive(context))
      monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }

  q2m_fire_spec spec = {
      .flash = flash,
      .start = start,
      .direction = direction,
      .pellets = 1,
  };
  if (species == Q2M_SOLDIER_RIPPER) {
    spec.kind = Q2M_ATTACK_ION;
    spec.damage = 5.0f;
    spec.kick = 1.0f;
    spec.speed = 600.0f;
    spec.radius = 100.0f;
    spec.fuse = 3.0f;
    spec.direct_mod = 34;
    spec.has_projectile_effects = true;
    spec.projectile_effects = UINT64_C(0x100000);
  } else if (species == Q2M_SOLDIER_HYPER) {
    spec.kind = Q2M_ATTACK_BLUE_BOLT;
    spec.flash = 17;
    spec.damage = 1.0f;
    spec.kick = 1.0f;
    spec.speed = 600.0f;
    spec.fuse = 2.0f;
    spec.direct_mod = context->game->options.edition == QA_Q2_RERELEASE ? 58
                                                                         : 1;
    spec.has_projectile_effects = true;
    spec.projectile_effects = UINT64_C(0x400000);
  } else if (species == Q2M_SOLDIER_LIGHT) {
    spec.kind = Q2M_ATTACK_BLASTER;
    spec.damage = 5.0f;
    spec.kick = 1.0f;
    spec.speed = 600.0f;
    spec.fuse = 2.0f;
    spec.direct_mod = Q2M_MOD_BLASTER;
    spec.has_projectile_effects = true;
    spec.projectile_effects = 8;
  } else if (species == Q2M_SOLDIER) {
    spec.kind = Q2M_ATTACK_SHOTGUN;
    spec.damage = 2.0f;
    spec.kick = 1.0f;
    spec.horizontal_spread =
        context->game->options.edition == QA_Q2_RERELEASE && !rogue ? 1500.0f
                                                                    : 1000.0f;
    spec.vertical_spread =
        context->game->options.edition == QA_Q2_RERELEASE && !rogue ? 750.0f
                                                                    : 500.0f;
    spec.pellets =
        context->game->options.edition == QA_Q2_RERELEASE && !rogue ? 9 : 12;
    spec.direct_mod = Q2M_MOD_SHOTGUN;
    monster->cocked = false;
  } else {
    if (!monster->hold_frame) {
      if (context->game->options.edition == QA_Q2_RERELEASE && !rogue) {
        monster->fire_ns = q2m_after(
            context->game->now_ns, 0.3 + q2m_random(context->game) * 0.8);
      } else if (rogue) {
        uint32_t word =
            (uint32_t)(q2m_random(context->game) * 32768.0f) % 8u;
        monster->pause_ns =
            q2m_after(context->game->now_ns, (3.0 + word) * 0.1);
      } else {
        unsigned tenths =
            3u + (unsigned)floorf(q2m_random(context->game) * 8.0f);
        monster->pause_ns =
            q2m_after(context->game->now_ns, (double)tenths * 0.1);
      }
    }
    spec.kind = Q2M_ATTACK_BULLET;
    spec.damage = 2.0f;
    spec.kick = 4.0f;
    spec.horizontal_spread = 300.0f;
    spec.vertical_spread = 500.0f;
    spec.direct_mod = Q2M_MOD_MACHINEGUN;
  }
  if (!q2m_fire(context, &spec, error))
    return false;
  if (q2m_alive(context) && species == Q2M_SOLDIER_SS)
    monster->hold_frame = context->game->now_ns <
                          (context->game->options.edition == QA_Q2_RERELEASE &&
                                   !rogue
                               ? monster->fire_ns
                               : monster->pause_ns);
  return true;
}

static bool enemy_in_front(q2m_context *context) {
  qa_body_state target;
  qa_error ignored = {0};
  if (context->monster->enemy.registry == 0 ||
      !qa_world_body_read(context->game->services.world,
                          context->monster->enemy, &target, &ignored))
    return false;
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  return qa_vec_dot(forward, qa_vec_normalize(qa_vec_sub(
                                 target.origin, context->body.origin))) > 0.3f;
}

static bool soldier_blaster(const struct qa_q2_monster *monster) {
  return monster->definition->species == Q2M_SOLDIER_LIGHT;
}

static bool soldier_shotgun(const struct qa_q2_monster *monster) {
  return monster->definition->species == Q2M_SOLDIER;
}

static bool soldierh_ripper(const struct qa_q2_monster *monster) {
  return monster->definition->species == Q2M_SOLDIER_RIPPER;
}

static bool soldierh_hyper(const struct qa_q2_monster *monster) {
  return monster->definition->species == Q2M_SOLDIER_HYPER;
}

static bool soldierh_laser(const struct qa_q2_monster *monster) {
  return monster->definition->species == Q2M_SOLDIER_LASER;
}

static int soldier_callback_index(const char *callback) {
  size_t length = strlen(callback);
  if (length == 0 || callback[length - 1] < '1' || callback[length - 1] > '8')
    return -1;
  switch (callback[length - 1]) {
  case '1':
    return 0;
  case '2':
    return 1;
  case '3':
    return 2;
  case '4':
    return 3;
  case '5':
    return 8;
  case '6':
    return 5;
  case '7':
    return 6;
  case '8':
    return 7;
  default:
    return -1;
  }
}

static bool soldier_refire(q2m_context *context, bool force, bool *result,
                           qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  float distance = q2m_distance(context, monster->enemy);
  if (context->game->options.edition == QA_Q2_CLASSIC) {
    *result = (context->game->options.skill == 3 &&
               q2m_random(context->game) < 0.5f) ||
              distance <= 80.0f;
    return true;
  }
  bool visible = false;
  if ((force || distance > 20.0f) && !visible_enemy(context, &visible, error))
    return false;
  *result = ((force || q2m_random(context->game) < 0.5f) && visible) ||
            distance <= 20.0f;
  return true;
}

static bool soldier_run(q2m_context *context, qa_error *error) {
  context->monster->dodging = false;
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    if (context->monster->attack_state == Q2M_SLIDING)
      context->monster->attack_state = Q2M_STRAIGHT;
    if (!q2m_soldier_sound_end(context, error)) return false;
    if (!q2m_alive(context)) return true;
    const char *move = context->monster->stand_ground ? "soldier_move_stand1"
        : context->monster->move &&
          (strcmp(context->monster->move->name, "soldier_move_walk1") == 0 ||
           strcmp(context->monster->move->name, "soldier_move_walk2") == 0 ||
           strcmp(context->monster->move->name, "soldier_move_start_run") == 0 ||
           strcmp(context->monster->move->name, "soldier_move_run") == 0)
            ? "soldier_move_run" : "soldier_move_start_run";
    return q2m_set_move(context, move, true, error);
  }
  context->monster->charging = false;
  context->monster->hold_frame = false;
  const char *move = context->monster->stand_ground
                         ? context->monster->definition->stand_move
                         : context->monster->definition->run_move;
  return set_definition_move(context, move, error);
}

static bool soldier_callbacks(q2m_context *context, const char *callback,
                              bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool base = strncmp(callback, "soldier_", 8) == 0;
  bool heavy = strncmp(callback, "soldierh_", 9) == 0;
  *handled = base || heavy;
  if (!*handled)
    return true;

  if (strcmp(callback, "soldier_attack1_shotgun_check") == 0 ||
      strcmp(callback, "soldier_attack2_shotgun_check") == 0 ||
      strcmp(callback, "soldier_attack6_shotgun_check") == 0) {
    if (!soldier_shotgun(monster) || monster->cocked)
      return true;
    monster->next_frame = strstr(callback, "attack1") != NULL   ? 5
                          : strstr(callback, "attack2") != NULL ? 21
                                                                : 117;
    monster->force_refire = true;
    return true;
  }

  if (strcmp(callback, "soldier_attack1_refire1") == 0) {
    if (context->game->options.edition == QA_Q2_RERELEASE &&
        soldier_blaster(monster))
      monster->next_frame = 9;
    if (monster->manual_steering) {
      monster->manual_steering = false;
      return true;
    }
    if (!soldier_blaster(monster) || !enemy_alive(context))
      return true;
    bool refire;
    if (!soldier_refire(context, false, &refire, error))
      return false;
    monster->next_frame = refire ? 1 : 9;
    return true;
  }
  if (strcmp(callback, "soldier_attack1_refire2") == 0 ||
      strcmp(callback, "soldier_attack2_refire2") == 0) {
    if (soldier_blaster(monster) || !enemy_alive(context))
      return true;
    bool refire;
    if (!soldier_refire(context, monster->force_refire, &refire, error))
      return false;
    if (refire) {
      monster->next_frame = callback[14] == '1' ? 1 : 15;
      monster->force_refire = false;
    }
    return true;
  }
  if (strcmp(callback, "soldier_attack2_refire1") == 0) {
    if (context->game->options.edition == QA_Q2_RERELEASE &&
        soldier_blaster(monster))
      monster->next_frame = 27;
    if (!soldier_blaster(monster) || !enemy_alive(context))
      return true;
    bool refire;
    if (!soldier_refire(context, false, &refire, error))
      return false;
    if (refire)
      monster->next_frame = 15;
    else if (context->game->options.edition == QA_Q2_CLASSIC)
      monster->next_frame = 27;
    return true;
  }
  if (strcmp(callback, "soldier_attack3_refire") == 0) {
    if (context->game->options.edition == QA_Q2_RERELEASE &&
        soldier_shotgun(monster) && !monster->cocked)
      monster->hold_frame = context->game->now_ns < monster->duck_ns;
    else {
      uint64_t until = context->game->options.edition == QA_Q2_CLASSIC
                           ? monster->pause_ns
                           : monster->duck_ns;
      if (q2m_after(context->game->now_ns, 0.4) < until)
        monster->next_frame = 32;
    }
    return true;
  }
  if (strcmp(callback, "soldier_attack6_refire") == 0) {
    if (enemy_alive(context) &&
        q2m_distance(context, monster->enemy) >=
            (context->game->options.product == QA_Q2_ROGUE ? 80.0f : 500.0f) &&
        (context->game->options.skill == 3 ||
         (context->game->options.product == QA_Q2_ROGUE &&
          q2m_random(context->game) < 0.25f * context->game->options.skill)))
      monster->next_frame = 111;
    monster->dodging = false;
    monster->charging = false;
    return true;
  }
  if (strcmp(callback, "soldier_attack6_refire1") == 0 ||
      strcmp(callback, "soldier_attack6_refire2") == 0) {
    bool first = ends_with(callback, "refire1");
    monster->dodging = false;
    monster->charging = false;
    if (monster->enemy.registry == 0 ||
        (first ? !soldier_blaster(monster) : soldier_blaster(monster)))
      return true;
    bool visible = false;
    if (!enemy_alive(context) ||
        q2m_distance(context, monster->enemy) < 440.0f ||
        !visible_enemy(context, &visible, error))
      return first ? soldier_run(context, error) : true;
    if (!visible)
      return first ? soldier_run(context, error) : true;
    if (monster->force_refire || q2m_random(context->game) < 0.25f) {
      monster->next_frame = 111;
      monster->force_refire = false;
    } else if (first) {
      return soldier_run(context, error);
    }
    return true;
  }

  if (strcmp(callback, "soldierh_attack1_refire1") == 0 ||
      strcmp(callback, "soldierh_attack1_refire2") == 0 ||
      strcmp(callback, "soldierh_attack2_refire1") == 0 ||
      strcmp(callback, "soldierh_attack2_refire2") == 0) {
    if (!enemy_alive(context))
      return true;
    bool attack1 = strstr(callback, "attack1") != NULL;
    bool first = ends_with(callback, "refire1");
    float distance = q2m_distance(context, monster->enemy);
    if (first && soldierh_ripper(monster)) {
      bool refire = (context->game->options.skill == 3 &&
                     q2m_random(context->game) < 0.5f) ||
                    distance < 80.0f;
      monster->next_frame = refire ? (attack1 ? 1 : 15) : (attack1 ? 9 : 27);
    } else if (!first && !soldierh_ripper(monster) &&
               ((context->game->options.skill == 3 &&
                 q2m_random(context->game) < 0.5f) ||
                (distance < 80.0f && !soldierh_laser(monster)))) {
      monster->next_frame = attack1 ? 1 : 15;
    }
    return true;
  }
  if (strcmp(callback, "soldierh_attack3_refire") == 0) {
    if (q2m_after(context->game->now_ns, 0.4) < monster->pause_ns)
      monster->next_frame = 32;
    return true;
  }
  if (strcmp(callback, "soldierh_attack6_refire") == 0) {
    if (enemy_alive(context) &&
        q2m_distance(context, monster->enemy) >= 500.0f &&
        context->game->options.skill == 3)
      monster->next_frame = 111;
    return true;
  }
  if (strcmp(callback, "soldierh_hyper_refire1") == 0 ||
      strcmp(callback, "soldierh_hyper_refire2") == 0) {
    if (!soldierh_hyper(monster))
      return true;
    if (q2m_random(context->game) < 0.7f) {
      bool visible;
      if (!visible_enemy(context, &visible, error))
        return false;
      if (visible)
        monster->frame = ends_with(callback, "refire1") ? 2 : 16;
    } else {
      return q2m_sound(context, "weapons/hyprbd1a.wav", 0, 1.0f, error);
    }
    return true;
  }
  if (strcmp(callback, "soldierh_hyper_sound") == 0)
    return soldierh_hyper(monster)
               ? q2m_sound(context, "weapons/hyprbl1a.wav", 0, 1.0f, error)
               : true;

  if (strcmp(callback, "soldier_idle") == 0 ||
      strcmp(callback, "soldierh_idle") == 0)
    return q2m_random(context->game) > 0.8f
               ? q2m_sound(context, "soldier/solidle1.wav", 2, 2.0f, error)
               : true;
  if (strcmp(callback, "soldier_cock") == 0 ||
      strcmp(callback, "soldierh_cock") == 0) {
    if (base)
      monster->cocked = true;
    return q2m_sound(context, "infantry/infatck3.wav", 1, 1.0f, error);
  }
  if (strcmp(callback, "soldier_walk1_random") == 0 ||
      strcmp(callback, "soldierh_walk1_random") == 0) {
    if (q2m_random(context->game) > 0.1f)
      monster->next_frame = monster->move->first_frame;
    return true;
  }
  if (strcmp(callback, "soldier_duck_down") == 0 ||
      strcmp(callback, "soldierh_duck_down") == 0) {
    if (monster->ducked)
      return true;
    monster->pause_ns = q2m_after(context->game->now_ns, 1.0);
    return set_duck(context, true, error);
  }
  if (strcmp(callback, "soldier_duck_hold") == 0 ||
      strcmp(callback, "soldierh_duck_hold") == 0) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  if (strcmp(callback, "soldier_duck_up") == 0 ||
      strcmp(callback, "soldierh_duck_up") == 0)
    return set_duck(context, false, error);
  if (strcmp(callback, "soldier_start_charge") == 0) {
    monster->charging = true;
    return true;
  }
  if (strcmp(callback, "soldier_stop_charge") == 0) {
    monster->charging = false;
    return true;
  }
  if (strcmp(callback, "soldier_blind_check") == 0) {
    if (monster->manual_steering) {
      qa_vec3 direction =
          qa_vec_sub(monster->blind_fire_target, context->body.origin);
      monster->ideal_yaw =
          atan2f(direction.y, direction.x) * 57.29577951308232f;
    }
    return true;
  }
  if (strcmp(callback, "soldier_death_shrink") == 0)
    return shrink(context, error);

  if ((base && strncmp(callback, "soldier_fire", 12) == 0) ||
      (heavy && (strncmp(callback, "soldierh_fire", 13) == 0 ||
                 strncmp(callback, "soldierh_ripper", 15) == 0 ||
                 strncmp(callback, "soldierh_hyperripper", 20) == 0))) {
    bool ripper_callback = strncmp(callback, "soldierh_ripper", 15) == 0;
    bool hyper_callback =
        strncmp(callback, "soldierh_hyperripper", 20) == 0;
    if (heavy && soldierh_laser(monster) &&
        (ripper_callback || hyper_callback))
      return true;
    if (!heavy && hyper_callback && monster->skin < 6)
      return true;
    int index = soldier_callback_index(callback);
    if (index < 0)
      return true;
    if (base && strcmp(callback, "soldier_fire3") == 0 &&
        context->game->options.edition == QA_Q2_CLASSIC) {
      monster->pause_ns = q2m_after(context->game->now_ns, 1.0);
      if (!set_duck(context, true, error))
        return false;
    }
    bool limited = strcmp(callback, "soldier_fire5") == 0 ||
                   strcmp(callback, "soldier_fire8") == 0 ||
                   strcmp(callback, "soldierh_hyperripper5") == 0 ||
                   strcmp(callback, "soldierh_hyperripper8") == 0;
    return soldier_fire_exact(context, (unsigned)index, limited, error);
  }

  if (strcmp(callback, "soldierh_hyper_laser_sound_start") == 0)
    return soldier_laser_sound(context, true, error);
  if (strcmp(callback, "soldierh_hyper_laser_sound_end") == 0)
    return soldier_laser_sound(context, false, error);

  *handled = false;
  return true;
}

static void finish_dodge_action(struct qa_q2_monster *monster) {
  monster->dodging = false;
  if (monster->attack_state == Q2M_SLIDING)
    monster->attack_state = Q2M_STRAIGHT;
}

static const qa_vec3 infantry_death_aim[] = {
    {0, 5, 0},   {10, 15, 0}, {20, 25, 0}, {25, 35, 0},
    {30, 40, 0}, {30, 45, 0}, {25, 50, 0}, {20, 40, 0},
    {15, 35, 0}, {40, 35, 0}, {70, 35, 0}, {90, 35, 0},
};

static bool infantry_machinegun(q2m_context *context, qa_error *error) {
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  int frame = context->monster->frame;
  bool running = rerelease && frame >= 232 && frame <= 239;
  bool normal = rerelease ? frame == 186 || frame == 227 || frame == 255 ||
                                running
                          : frame == 194;
  if (!normal && (frame < 155 || frame >= 155 +
                                           (int)(sizeof(infantry_death_aim) /
                                                 sizeof(infantry_death_aim[0]))))
    return true;
  int flash = normal ? running ? frame : rerelease && frame == 255 ? 260 : 26
                     : 27 + frame - 155;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_vec3 direction;
  qa_builtin_angle_vectors(context->body.angles, &direction, NULL, NULL);

  qa_body_state enemy;
  qa_error ignored = {0};
  bool has_enemy = context->monster->enemy.registry != 0 &&
                   qa_world_body_read(context->game->services.world,
                                      context->monster->enemy, &enemy, &ignored);
  if (normal && rerelease && !has_enemy)
    return true;
  if (normal && has_enemy) {
    float height;
    if (!enemy_view_height(context, &height) || !q2m_alive(context))
      return true;
    if (!rerelease) {
      qa_vec3 target = qa_vec_add(
          qa_vec_add(enemy.origin, qa_v3(0, 0, height)),
          qa_vec_scale(enemy.velocity, -0.2f));
      direction = qa_vec_normalize(qa_vec_sub(target, start));
    } else {
      qa_vec3 eye = qa_vec_add(enemy.origin, qa_v3(0, 0, height));
      qa_trace_query eye_query = {
          .start = start,
          .end = eye,
          .pass_actor = context->actor->id,
          .policy = qa_collision_default_policy(QA_COLLISION_Q2),
      };
      eye_query.policy.contents_mask = UINT32_C(0x46004003);
      qa_trace_result trace;
      if (!qa_world_trace(context->game->services.world, &eye_query, &trace,
                          error))
        return false;
      if (!q2m_alive(context))
        return true;
      bool use_eye = trace.hit == QA_TRACE_HIT_ACTOR &&
                     qa_actor_id_equal(trace.actor, context->monster->enemy);
      qa_vec3 observed = use_eye ? eye : enemy.origin;
      qa_vec3 predicted =
          qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, 0.2f));
      qa_trace_query lead_query = {
          .start = start,
          .end = predicted,
          .policy = qa_collision_default_policy(QA_COLLISION_Q2),
      };
      lead_query.policy.contents_mask = 3u;
      if (!qa_world_trace(context->game->services.world, &lead_query, &trace,
                          error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (qa_vec_dot(qa_vec_normalize(qa_vec_sub(observed, start)),
                     qa_vec_normalize(qa_vec_sub(predicted, start))) < 0.0f ||
          trace.fraction < 0.9f)
        predicted = enemy.origin;
      if (use_eye)
        predicted.z += height;
      direction = qa_vec_normalize(qa_vec_sub(predicted, start));
    }
  } else if (!normal) {
    qa_vec3 angles = qa_vec_sub(context->body.angles,
                                infantry_death_aim[frame - 155]);
    qa_builtin_angle_vectors(angles, &direction, NULL, NULL);
  }
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_BULLET, 3.0f,
                                        flash, start, direction);
  return q2m_fire(context, &spec, error);
}

static bool can_walk_forward(q2m_context *context, float distance, bool *moved,
                             qa_error *error) {
  return qa_physics_walk_move(context->game->services.physics,
                              context->actor->id, context->body.angles.y,
                              distance, context->elapsed, false, false, moved,
                              error);
}

static bool infantry_fire(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (!infantry_machinegun(context, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (context->game->options.edition == QA_Q2_CLASSIC) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  monster->cocked = false;
  if (strcmp(monster->move->name, "infantry_move_attack4") == 0) {
    if (context->game->now_ns >= monster->fire_ns) {
      finish_dodge_action(monster);
      if (!q2m_set_move(context, "infantry_move_attack1", false, error))
        return false;
      monster->next_frame = 197;
    } else {
      bool moved;
      if (!can_walk_forward(context, 8.0f, &moved, error))
        return false;
      if (!moved) {
        if (!q2m_set_move(context, "infantry_move_attack1", false, error))
          return false;
        monster->next_frame = 186;
        finish_dodge_action(monster);
        monster->attack_state = Q2M_STRAIGHT;
      }
    }
    return true;
  }
  bool attacking = (monster->frame >= 184 && monster->frame <= 198) ||
                   (monster->frame >= 217 && monster->frame <= 231) ||
                   (monster->frame >= 240 && monster->frame <= 263);
  if (attacking) {
    monster->hold_frame = context->game->now_ns < monster->fire_ns;
    if (!monster->hold_frame && monster->frame == 255)
      monster->next_frame = 259;
  }
  return true;
}

static bool infantry_callbacks(q2m_context *context, const char *callback,
                               bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = strncmp(callback, "infantry_", 9) == 0 ||
             strcmp(callback, "InfantryMachineGun") == 0;
  if (!*handled)
    return true;

  if (strcmp(callback, "InfantryMachineGun") == 0)
    return infantry_machinegun(context, error);
  if (strcmp(callback, "infantry_fire") == 0)
    return infantry_fire(context, error);
  if (strcmp(callback, "infantry_cock_gun") == 0) {
    if (context->game->options.edition == QA_Q2_CLASSIC) {
      unsigned tenths = 10u + (unsigned)(q2m_random(context->game) * 16.0f);
      monster->pause_ns =
          q2m_after(context->game->now_ns, (double)tenths * 0.1);
    } else {
      monster->cocked = true;
    }
    return q2m_sound(context, "infantry/infatck3.wav", 1, 1.0f, error);
  }
  if (strcmp(callback, "infantry_set_firetime") == 0) {
    monster->fire_ns =
        q2m_after(context->game->now_ns, 0.7 + q2m_random(context->game) * 1.3);
    if (!monster->stand_ground && monster->enemy.registry != 0 &&
        q2m_distance(context, monster->enemy) >= 330.0f) {
      bool moved;
      if (!can_walk_forward(context, 8.0f, &moved, error))
        return false;
      if (moved)
        return q2m_set_move(context, "infantry_move_attack4", false, error);
    }
    return true;
  }
  if (strcmp(callback, "infantry_attack4_refire") == 0) {
    if (context->game->now_ns >= monster->fire_ns) {
      finish_dodge_action(monster);
      if (!q2m_set_move(context, "infantry_move_attack1", false, error))
        return false;
      monster->next_frame = 197;
    } else {
      bool moved = false;
      if (!monster->stand_ground && monster->enemy.registry != 0 &&
          q2m_distance(context, monster->enemy) >= 330.0f &&
          !can_walk_forward(context, 8.0f, &moved, error))
        return false;
      if (monster->stand_ground || monster->enemy.registry == 0 ||
          q2m_distance(context, monster->enemy) < 330.0f || !moved) {
        if (!q2m_set_move(context, "infantry_move_attack1", false, error))
          return false;
        monster->next_frame = 186;
        finish_dodge_action(monster);
        monster->attack_state = Q2M_STRAIGHT;
      } else {
        monster->next_frame = 232;
      }
    }
    return infantry_fire(context, error);
  }
  if (strcmp(callback, "infantry_swing") == 0)
    return q2m_sound(context, "infantry/infatck2.wav", 1, 1.0f, error);
  if (strcmp(callback, "infantry_duck_down") == 0) {
    if (monster->ducked)
      return true;
    monster->pause_ns = q2m_after(context->game->now_ns, 1.0);
    return set_duck(context, true, error);
  }
  if (strcmp(callback, "infantry_duck_hold") == 0) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  if (strcmp(callback, "infantry_duck_up") == 0)
    return set_duck(context, false, error);
  if (strcmp(callback, "infantry_shrink") == 0)
    return shrink(context, error);
  if (strcmp(callback, "infantry_jump_now") == 0 ||
      strcmp(callback, "infantry_jump2_now") == 0 ||
      strcmp(callback, "infantry_jump_wait_land") == 0)
    return jump_action(context, callback, error);

  *handled = false;
  return true;
}

static qa_vec3 carrier_spawn_start(const q2m_context *context) {
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  return qa_vec_add(context->body.origin,
                    qa_vec_add(qa_vec_scale(forward, 105.0f),
                               qa_vec_add(qa_vec_scale(right, 0.0f),
                                          qa_vec_scale(up, -58.0f))));
}

static bool carrier_rocket(q2m_context *context, qa_error *error) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  bool predictive = traits.player && q2m_random(context->game) < 0.5f;
  qa_vec3 right;
  qa_builtin_angle_vectors(context->body.angles, NULL, &right, NULL);
  static const float spreads[] = {0.4f, 0.025f, -0.025f, -0.4f};
  for (unsigned index = 0; index < 4; ++index) {
    int flash = 191 + (int)index;
    qa_vec3 start;
    if (!q2m_project_flash(context, flash, &start, error))
      return false;
    qa_vec3 direction;
    if (predictive) {
      if (!q2m_predict_from(context, start, 750.0f, false,
                            -0.3f + (float)index * 0.15f, NULL, &direction,
                            &available, error))
        return false;
      if (!available || !q2m_alive(context))
        return true;
    } else {
      qa_vec3 target = enemy.origin;
      if (index == 0 || index == 3)
        target.z -= 15.0f;
      qa_vec3 direct = qa_vec_normalize(qa_vec_sub(target, start));
      direction = qa_vec_normalize(
          qa_vec_add(direct, qa_vec_scale(right, spreads[index])));
    }
    if (!fire_rocket_exact(context, Q2M_ATTACK_ROCKET, flash, start, direction,
                           50.0f, predictive ? 750.0f : 500.0f, 70.0f,
                           50.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  return true;
}

static bool carrier_machinegun(q2m_context *context, bool right_gun,
                               qa_error *error) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  int flash = (context->monster->manual_steering ? 152 : 138) +
              (right_gun ? 1 : 0);
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_vec3 eye = enemy.origin;
  eye.z += traits.view_height;
  eye = qa_vec_add(
      eye, qa_vec_scale(enemy.velocity, right_gun ? 0.2f : -0.2f));
  qa_vec3 direction = qa_vec_normalize(qa_vec_sub(eye, start));
  return fire_bullet_exact(context, flash, start, direction, 6.0f, 900.0f,
                           500.0f, error);
}

static bool carrier_grenade(q2m_context *context, qa_error *error) {
  if (!carrier_coop_check(context, error) || !q2m_alive(context))
    return !q2m_alive(context);
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  (void)traits;
  float side = q2m_random(context->game) < 0.5f ? -1.0f : 1.0f;
  uint64_t elapsed =
      context->game->now_ns >= context->monster->timestamp_ns
          ? context->game->now_ns - context->monster->timestamp_ns
          : 0;
  unsigned phase = (unsigned)(elapsed / (4 * Q2M_TENTH));
  float right_spread = phase == 0 ? 0.15f * side
                       : phase == 2 ? -0.15f * side
                                    : 0.0f;
  float up_spread = phase == 0   ? 0.1f - 0.1f * side
                    : phase == 2 ? 0.1f + 0.1f * side
                    : phase == 1 || phase == 3 ? 0.1f
                                               : 0.0f;
  qa_vec3 start;
  if (!q2m_project_flash(context, 140, &start, error))
    return false;
  qa_vec3 right, up;
  qa_builtin_angle_vectors(context->body.angles, NULL, &right, &up);
  qa_vec3 direction = qa_vec_add(
      qa_vec_add(qa_vec_normalize(qa_vec_sub(enemy.origin, start)),
                 qa_vec_scale(right, right_spread)),
      qa_vec_scale(up, up_spread));
  direction.z = fmaxf(-0.5f, fminf(0.15f, direction.z));
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_GRENADE, 50.0f, 53, start, direction);
  spec.speed = 600.0f;
  spec.fuse = 2.5f;
  spec.radius = 90.0f;
  spec.radius_damage = 50.0f;
  return q2m_fire(context, &spec, error);
}

static bool carrier_save_location(q2m_context *context) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  context->monster->saved_attack_position = enemy.origin;
  context->monster->saved_attack_position.z += traits.view_height;
  return true;
}

static bool carrier_rail(q2m_context *context, qa_error *error) {
  if (!carrier_coop_check(context, error) || !q2m_alive(context))
    return !q2m_alive(context);
  qa_vec3 start;
  if (!q2m_project_flash(context, 147, &start, error))
    return false;
  qa_vec3 direction = qa_vec_normalize(
      qa_vec_sub(context->monster->saved_attack_position, start));
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_RAIL, 50.0f, 147, start, direction);
  if (!q2m_fire(context, &spec, error))
    return false;
  if (q2m_alive(context))
    context->monster->attack_ns = q2m_after(context->game->now_ns, 3.0);
  return true;
}

static bool carrier_coop_check(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (!context->game->options.cooperative ||
      context->game->now_ns < monster->coop_check_ns)
    return true;
  q2_trace_frame *players = q2_player_roster(context->game, error);
  if (players == NULL)
    return false;
  bool result = true;
  size_t eligible = 0;
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  for (size_t index = 0; index < players->snapshot.count; ++index) {
    qa_actor_id target = players->snapshot.ids[index];
    qa_body_state body;
    qa_error ignored = {0};
    qa_builtin_actor_traits traits = {0};
    if (!qa_world_body_read(context->game->services.world, target, &body,
                            &ignored))
      continue;
    if (context->game->services.actor_traits != NULL)
      context->game->services.actor_traits(context->game->services.context,
                                           target, &traits);
    if (!q2m_alive(context))
      break;
    if (!traits.player)
      continue;
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(body.origin, context->body.origin));
    float facing = qa_vec_dot(direction, forward);
    if (facing >= -0.3f && -direction.z <= 0.95f)
      continue;
    qa_trace_query query = {
        .start = context->body.origin,
        .end = body.origin,
        .pass_actor = context->actor->id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    query.policy.contents_mask = 3u;
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error)) {
      result = false;
      break;
    }
    if (trace.fraction == 1.0f)
      players->snapshot.ids[eligible++] = target;
  }
  if (result && eligible != 0) {
    uint32_t selected =
        (uint32_t)floorf(q2m_random(context->game) * (float)eligible);
    if (selected >= eligible)
      selected = (uint32_t)eligible - 1u;
    qa_actor_id previous = monster->enemy;
    monster->enemy = players->snapshot.ids[selected];
    monster->coop_check_ns = q2m_after(context->game->now_ns, 2.0);
    if (!carrier_rocket(context, error))
      result = false;
    if (q2m_alive(context))
      monster->enemy = previous;
  }
  players->active = false;
  return result;
}

static bool carrier_machineguns(q2m_context *context, qa_error *error) {
  return carrier_coop_check(context, error) &&
         (!q2m_alive(context) ||
          carrier_machinegun(context, false, error)) &&
         (!q2m_alive(context) ||
          carrier_machinegun(context, true, error));
}

static bool carrier_spawn_child(q2m_context *context, qa_error *error) {
  qa_bounds flyer = {.mins = {-16, -16, -24}, .maxs = {16, 16, 16}};
  bool found;
  qa_vec3 point;
  if (!qa_q2_rogue_find_spawn_point(context->game, carrier_spawn_start(context),
                                    flyer, 32.0f, &found, &point, error))
    return false;
  if (!found || !q2m_alive(context))
    return true;
  uint64_t elapsed =
      context->game->now_ns >= context->monster->timestamp_ns
          ? context->game->now_ns - context->monster->timestamp_ns
          : 0;
  unsigned phase = (unsigned)((elapsed + Q2M_TENTH) / (5 * Q2M_TENTH));
  const char *classname = phase == 2 ? "monster_kamikaze" : "monster_flyer";
  qa_actor_id child;
  if (!q2m_create_reinforcement(context, classname, point, &child, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!q2m_sound(context, "medic_commander/monsterspawn1.wav", 4, 0.0f, error))
    return false;
  if (!q2m_alive(context) || child.registry == 0 ||
      child.slot >= context->game->capacity)
    return true;
  if (!q2m_summon_subtract(&context->monster->monster_slots, 1, error))
    return false;
  q2_actor *actor = context->game->actors[child.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, child) ||
      actor->monster == NULL)
    return true;
  q2m_context spawned = {.game = context->game,
                         .actor = actor,
                         .monster = actor->monster,
                         .elapsed = context->elapsed};
  if (!q2m_refresh(&spawned, error))
    return !q2m_alive(&spawned);
  spawned.monster->start_due_ns = context->game->now_ns;
  bool handled;
  if (!q2m_lifecycle_tick(&spawned, &handled, error))
    return false;
  if (!q2m_alive(context) || !q2m_alive(&spawned))
    return true;
  spawned.monster->spawned_by = Q2M_SPAWN_CARRIER;
  spawned.monster->do_not_count = spawned.monster->ignore_shots = true;
  spawned.monster->commander = context->actor->id;
  if (context->monster->enemy.registry != 0 && enemy_alive(context)) {
    if (!q2m_alive(context) || !q2m_alive(&spawned))
      return true;
    if (!q2m_found_target(&spawned, context->monster->enemy, error))
      return false;
    if (!q2m_alive(&spawned))
      return true;
    if (phase == 1 || phase == 3) {
      spawned.monster->lefty = phase == 3;
      spawned.monster->attack_state = Q2M_SLIDING;
      return q2m_set_move(&spawned, "flyer_move_attack3", false, error);
    }
    if (phase == 2) {
      spawned.monster->lefty = false;
      spawned.monster->attack_state = Q2M_STRAIGHT;
      if (!q2m_set_move(&spawned, "flyer_move_kamikaze", false, error))
        return false;
      if (!q2m_alive(&spawned))
        return true;
      qa_combat_state traits;
      if (!qa_combat_read_traits(context->game->services.combat, child, &traits, error))
        return false;
      if (!q2m_alive(&spawned))
        return true;
      traits.mass = 100.0f;
      if (!qa_combat_set_traits(context->game->services.combat, child,
                                &traits, error))
        return false;
      if (q2m_alive(&spawned))
        spawned.monster->charging = true;
      return true;
    }
  }
  return true;
}

static bool carrier_spawn_callbacks(q2m_context *context, const char *callback,
                                    bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = true;
  if (strcmp(callback, "carrier_prep_spawn") == 0) {
    monster->manual_steering = true;
    monster->timestamp_ns = context->game->now_ns;
    monster->yaw_speed = 10.0f;
    return carrier_machineguns(context, error);
  }
  if (strcmp(callback, "carrier_start_spawn") == 0) {
    qa_body_state enemy;
    qa_error ignored = {0};
    if (monster->enemy.registry != 0 &&
        qa_world_body_read(context->game->services.world, monster->enemy,
                           &enemy, &ignored)) {
      uint64_t elapsed = context->game->now_ns >= monster->timestamp_ns
                             ? context->game->now_ns - monster->timestamp_ns
                             : 0;
      int phase = (int)(elapsed / (5 * Q2M_TENTH));
      if (phase >= 0 && phase <= 2) {
        qa_vec3 direction = qa_vec_sub(enemy.origin, context->body.origin);
        monster->ideal_yaw = qa_builtin_angle_mod(
            atan2f(direction.y, direction.x) * 57.29577951308232f +
            (float)(phase - 1) * 30.0f);
      }
    }
    return carrier_machineguns(context, error);
  }
  if (strcmp(callback, "carrier_ready_spawn") == 0) {
    if (!carrier_machineguns(context, error))
      return false;
    if (!q2m_alive(context))
      return true;
    float current = qa_builtin_angle_mod(context->body.angles.y);
    float delta = fabsf(current - monster->ideal_yaw);
    if (delta > 180.0f)
      delta = 360.0f - delta;
    if (delta > 0.1f) {
      monster->hold_frame = true;
      monster->timestamp_ns += Q2M_TENTH;
      return true;
    }
    monster->hold_frame = false;
    qa_bounds flyer = {.mins = {-16, -16, -24}, .maxs = {16, 16, 16}};
    bool found;
    qa_vec3 point;
    if (!qa_q2_rogue_find_spawn_point(context->game,
                                      carrier_spawn_start(context), flyer,
                                      32.0f, &found, &point, error))
      return false;
    return !found || q2_spawn_growth(context->game, point, 0, error);
  }
  if (strcmp(callback, "carrier_spawn_check") == 0) {
    if (!carrier_machineguns(context, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!carrier_spawn_child(context, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (context->game->now_ns > q2m_after(monster->timestamp_ns, 1.1)) {
      monster->manual_steering = false;
      monster->yaw_speed = 15.0f;
    } else {
      monster->next_frame = 51;
    }
    return true;
  }
  *handled = false;
  return true;
}

static bool stop_loop_sound(q2m_context *context, const char *path,
                            qa_error *error) {
  qa_builtin_event event = {
      .kind = QA_BUILTIN_STOP_SOUND,
      .family = QA_GAME_Q2,
      .provider = context->game->options.owner,
      .actor = context->actor->id,
      .time_ns = context->game->now_ns,
      .origin = context->body.origin,
      .volume = 1.0f,
      .attenuation = 1.0f,
      .channel = 0,
  };
  if (!qa_builtin_resource(&context->game->services, path, &event.resource,
                           error))
    return false;
  return qa_builtin_emit(&context->game->services, &event, error);
}

static int widow_torso_frame(q2m_context *context, bool *turned,
                             qa_error *error) {
  *turned = false;
  qa_body_state enemy;
  qa_error ignored = {0};
  if (context->monster->enemy.registry == 0 ||
      !qa_world_body_read(context->game->services.world,
                          context->monster->enemy, &enemy, &ignored))
    return 79;
  qa_vec3 to_self = qa_vec_sub(context->body.origin, enemy.origin);
  float target_yaw = atan2f(to_self.y, to_self.x) * 57.29577951308232f;
  float angle = context->body.angles.y - target_yaw;
  while (angle < 0.0f)
    angle += 360.0f;
  while (angle >= 360.0f)
    angle -= 360.0f;
  angle -= 180.0f;
  if (angle >= 105.0f || angle <= -75.0f) {
    const char *move = angle >= 105.0f ? "widow_move_attack_post_blaster_r"
                                       : "widow_move_attack_post_blaster_l";
    if (!q2m_set_move(context, move, false, error))
      return -1;
    context->monster->manual_steering = false;
    *turned = true;
    return 0;
  }
  for (int index = 0; index < 17; ++index)
    if (angle >= 95.0f - index * 10.0f)
      return 62 + index;
  return 79;
}

static bool widow_blaster(q2m_context *context, qa_error *error) {
  static const float sweep[] = {32, 26, 20, 10, 0, -6.5f, -13, -27, -41};
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) ||
      !q2m_alive(context) || !available)
    return true;
  qa_q2_game *game = context->game;
  game->widow_shot_phase = (uint8_t)((game->widow_shot_phase + 1u) & 3u);
  uint64_t effect = game->widow_shot_phase == 0 ? 8u : 0u;
  struct qa_q2_monster *monster = context->monster;
  int frame = monster->frame, flash;
  qa_vec3 start, direction;
  if (frame >= 86 && frame <= 94) {
    int index = frame - 86;
    flash = 156 + index;
    if (!q2m_project_flash(context, flash, &start, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_vec3 angles = context->body.angles;
    angles.x += q2m_vector_angles(qa_vec_sub(enemy.origin, start)).x;
    angles.y -= sweep[index];
    qa_builtin_angle_vectors(angles, &direction, NULL, NULL);
  } else if (frame >= 61 && frame <= 79) {
    monster->manual_steering = true;
    bool turned;
    int next = widow_torso_frame(context, &turned, error);
    if (next < 0)
      return false;
    if (!q2m_alive(context))
      return true;
    monster->next_frame = next ? next : frame;
    flash = frame == 61 ? 175 : 165 + frame - 62;
    if (!q2m_project_flash(context, flash, &start, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!q2m_predict_from(context, start, 1000.0f, true,
                          q2m_random(game) * .1f - .05f, NULL, &direction,
                          &available, error))
      return false;
    if (!q2m_alive(context) || !available)
      return true;
    qa_vec3 angles = q2m_vector_angles(direction);
    float yaw = context->body.angles.y;
    float aim = 100.0f - 10.0f * (flash - 165);
    if (aim <= 0.0f)
      aim += 360.0f;
    float target = yaw - angles.y;
    if (target <= 0.0f)
      target += 360.0f;
    float deviation = aim - target;
    if (deviation > 15.0f)
      angles.y = yaw - aim + 15.0f;
    else if (deviation < -15.0f)
      angles.y = yaw - aim - 15.0f;
    qa_builtin_angle_vectors(angles, &direction, NULL, NULL);
  } else if (frame >= 24 && frame <= 31) {
    flash = 183 + frame - 24;
    if (!q2m_project_flash(context, flash, &start, error))
      return false;
    if (!q2m_alive(context))
      return true;
    direction = qa_vec_sub(qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height)), start);
  } else {
    return true;
  }
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_GREEN_BOLT,
      10.0f * game->widow_damage_multiplier, flash, start, direction);
  spec.speed = 1000.0f;
  spec.has_projectile_effects = true;
  spec.projectile_effects = effect;
  return q2m_fire(context, &spec, error);
}

static bool widow_rail(q2m_context *context, qa_error *error) {
  const char *move = context->monster->move->name;
  int flash = !strcmp(move, "widow_move_attack_rail_l") ? 154
              : !strcmp(move, "widow_move_attack_rail_r") ? 155 : 150;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  if (!q2m_alive(context))
    return true;
  qa_vec3 direction = qa_vec_normalize(
      qa_vec_sub(context->monster->saved_attack_position, start));
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_RAIL,
      50.0f * context->game->widow_damage_multiplier, flash, start, direction);
  if (!q2m_fire(context, &spec, error))
    return false;
  if (q2m_alive(context))
    context->monster->timestamp_ns = q2m_after(context->game->now_ns, 3.0);
  return true;
}

static bool widow2_save_beam(q2m_context *context) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available))
    return false;
  if (q2m_alive(context)) {
    context->monster->widow_previous_target = available
        ? context->monster->saved_attack_position : qa_v3(0, 0, 0);
    context->monster->saved_attack_position = available ? enemy.origin : qa_v3(0, 0, 0);
  }
  return true;
}

static bool widow2_beam(q2m_context *context, qa_error *error) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) ||
      !q2m_alive(context) || !available)
    return true;
  struct qa_q2_monster *monster = context->monster;
  int frame = monster->frame, flash;
  qa_vec3 start, direction;
  bool sweep = frame >= 13 && frame <= 23;
  bool firing = frame >= 39 && frame <= 43;
  if (sweep) {
    int index = frame - 13;
    flash = 200 + index;
    if (!q2m_project_flash(context, flash, &start, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_vec3 angles = context->body.angles;
    angles.x += q2m_vector_angles(qa_vec_sub(enemy.origin, start)).x;
    angles.y -= -40.0f + index * 8.0f;
    qa_builtin_angle_vectors(angles, &direction, NULL, NULL);
  } else {
    monster->widow_previous_target = monster->saved_attack_position;
    monster->saved_attack_position = enemy.origin;
    flash = firing ? 195 + frame - 39 : 195;
    if (!q2m_project_flash(context, flash, &start, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_vec3 target = monster->widow_previous_target;
    target.z += traits.view_height - 10.0f;
    direction = qa_vec_normalize(qa_vec_sub(target, start));
  }
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_BEAM,
      10.0f, sweep || firing ? flash : -1, start, direction);
  spec.kick = 50.0f;
  return q2m_fire(context, &spec, error);
}

static bool widow2_tongue_point(q2m_context *context, qa_vec3 *start) {
  static const qa_vec3 offsets[] = {
      {17.48f, .10f, 68.92f}, {17.47f, .29f, 68.91f},
      {17.45f, .53f, 68.87f}, {17.42f, .78f, 68.81f},
      {17.39f, 1.02f, 68.75f}, {17.37f, 1.20f, 68.70f},
      {17.36f, 1.24f, 68.71f}, {17.37f, 1.21f, 68.72f}};
  int index = context->monster->frame - 47;
  if (index < 0 || (size_t)index >= sizeof(offsets) / sizeof(offsets[0]))
    return false;
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  qa_vec3 offset = offsets[index];
  *start = qa_vec_add(context->body.origin,
      qa_vec_add(qa_vec_scale(forward, offset.x),
          qa_vec_add(qa_vec_scale(right, offset.y), qa_vec_scale(up, offset.z))));
  return true;
}

static bool widow2_tongue_reaches(qa_vec3 start, qa_vec3 end) {
  qa_vec3 delta = qa_vec_sub(start, end);
  return qa_vec_length(delta) <= 256.0f &&
         fabsf(q2m_vector_angles(delta).x) <= 30.0f;
}

static bool widow2_tongue(q2m_context *context, qa_error *error) {
  qa_actor_id enemy_id = context->monster->enemy;
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) ||
      !q2m_alive(context) || !available || !q2_actor_live(context->game, enemy_id))
    return true;
  qa_vec3 start;
  if (!widow2_tongue_point(context, &start))
    return true;
  if (!widow2_tongue_reaches(start, enemy.origin) &&
      !widow2_tongue_reaches(start,
          qa_vec_add(enemy.origin, qa_v3(0, 0, enemy.bounds.maxs.z - 8.0f))) &&
      !widow2_tongue_reaches(start,
          qa_vec_add(enemy.origin, qa_v3(0, 0, enemy.bounds.mins.z + 8.0f))))
    return true;
  qa_trace_query query = {.start = start, .end = enemy.origin,
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2)};
  query.policy.contents_mask = UINT32_C(0x06000003);
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id) ||
      trace.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(trace.actor, enemy_id))
    return true;
  if (!q2m_sound(context, "brain/brnatck3.wav", 1, 1.0f, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id))
    return true;
  if (!q2m_emit(context, QA_BUILTIN_BEAM, "q2:parasite", 0,
                 start, enemy.origin, 1.0f, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id))
    return true;
  qa_attack attack = {.attacker = context->actor->id, .inflictor = context->actor->id,
      .combat_provider = context->game->options.owner,
      .cause = qa_q2_damage_cause(context->game->options.edition,
                                  context->game->options.product, 0, 8u)};
  return q2_damage(context->game, &attack, enemy_id, 2.0f, 0.0f,
                   qa_vec_sub(start, enemy.origin), enemy.origin, qa_v3(0, 0, 0),
                   false, error);
}

static bool widow2_pull(q2m_context *context, qa_error *error) {
  qa_actor_id enemy_id = context->monster->enemy;
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!available || !q2_actor_live(context->game, enemy_id))
    return q2m_set_move(context, context->monster->stand_ground
        ? "widow2_move_stand" : "widow2_move_run", false, error);
  qa_vec3 start;
  if (!widow2_tongue_point(context, &start) || !widow2_tongue_reaches(start, enemy.origin))
    return true;
  if (enemy.ground.registry)
    enemy.origin.z += 1.0f;
  qa_vec3 delta = qa_vec_sub(context->body.origin, enemy.origin);
  if (traits.player) {
    enemy.velocity = qa_vec_add(enemy.velocity, qa_vec_scale(qa_vec_normalize(delta), 1000.0f));
  } else {
    q2_actor *other = q2_actor_get(context->game, enemy_id, false, NULL);
    if (other && other->monster && other->monster->definition) {
      q2m_context target = {.game = context->game, .actor = other, .monster = other->monster};
      if (!q2m_refresh(&target, error))
        return !q2m_alive(context) || !q2m_alive(&target);
      if (!q2m_alive(context))
        return true;
      target.monster->ideal_yaw = q2m_vector_angles(delta).y;
      if (!q2m_change_yaw(&target, error))
        return false;
      if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id))
        return true;
      enemy.angles = target.body.angles;
    }
    qa_vec3 forward;
    qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
    enemy.velocity = qa_vec_scale(forward, 1000.0f);
  }
  enemy.ground = (qa_actor_id){0};
  if (!qa_world_body_write(context->game->services.world, enemy_id, &enemy, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id))
    return true;
  return !context->game->services.motion_changed ||
      context->game->services.motion_changed(context->game->services.context, enemy_id,
          &(qa_builtin_motion_change){.reason = QA_BUILTIN_MOTION_LAUNCH,
                                      .body = enemy}, error);
}

static bool conditional_transition(q2m_context *context, const char *callback,
                                   bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = true;

  if (strcmp(callback, "carrier_attack_gren") == 0) {
    monster->timestamp_ns = context->game->now_ns;
    return q2m_set_move(context, "carrier_move_attack_gren", false, error);
  }

  if (strcmp(callback, "guardian_atk1") == 0) {
    monster->timestamp_ns = q2_deadline(context->game->now_ns,
        (UINT64_C(650) + (uint64_t)q2_rerelease_time_ms(context->game, 0, 1500)) * Q2_MS);
    return q2m_set_move(context, "guardian_move_atk1_spin", false, error);
  }

  if (strcmp(callback, "guardian_atk1_finish") == 0) {
    if (!q2m_set_move(context, "guardian_atk1_out", true, error))
      return false;
    monster->weapon_sound = 0;
    if (!stop_loop_sound(context, "weapons/hyprbl1a.wav", error))
      return false;
    return true;
  }

  if (strcmp(callback, "widow_start_rail") == 0 ||
      strcmp(callback, "widow_done_spawn") == 0 ||
      strcmp(callback, "widow_rail_done") == 0) {
    monster->manual_steering = strcmp(callback, "widow_start_rail") == 0;
    return true;
  }

  if (strcmp(callback, "widow_start_run_5") == 0 ||
      strcmp(callback, "widow_start_run_10") == 0 ||
      strcmp(callback, "widow_start_run_12") == 0) {
    int frame = strcmp(callback, "widow_start_run_5") == 0    ? 15
                : strcmp(callback, "widow_start_run_10") == 0 ? 20
                                                              : 22;
    if (!q2m_set_move(context, "widow_move_run", false, error))
      return false;
    monster->next_frame = frame;
    return true;
  }

  if (strcmp(callback, "widow_attack_blaster") == 0) {
    monster->pause_ns =
        q2m_after(context->game->now_ns, 1.0 + 2.0 * q2m_random(context->game));
    if (!q2m_set_move(context, "widow_move_attack_blaster", false, error))
      return false;
    bool turned;
    int frame = widow_torso_frame(context, &turned, error);
    if (frame < 0)
      return false;
    if (!turned)
      monster->next_frame = frame;
    return true;
  }

  if (strcmp(callback, "brain_laserbeam_reattack") == 0) {
    if (q2m_random(context->game) < 0.5f) {
      bool visible;
      if (!q2m_visible(context, monster->enemy, &visible, error))
        return false;
      if (visible && enemy_alive(context))
        monster->frame = monster->move->first_frame;
    }
    return true;
  }

  if (strcmp(callback, "chick_rerocket") == 0) {
    if (monster->manual_steering) {
      monster->manual_steering = false;
      return q2m_set_move(context, "chick_move_end_attack1", false, error);
    }
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    float chance = context->game->options.edition == QA_Q2_RERELEASE ? 0.7f
                   : context->game->options.product == QA_Q2_ROGUE
                       ? 0.6f + 0.05f * context->game->options.skill
                       : 0.6f;
    bool repeat = visible && q2m_distance(context, monster->enemy) >= 80.0f &&
                  q2m_random(context->game) <= chance;
    return q2m_set_move(
        context, repeat ? "chick_move_attack1" : "chick_move_end_attack1",
        false, error);
  }

  if (strcmp(callback, "chick_reslash") == 0) {
    bool repeat = enemy_alive(context) &&
                  q2m_distance(context, monster->enemy) < 80.0f &&
                  q2m_random(context->game) <= 0.9f;
    return q2m_set_move(context,
                        repeat ? "chick_move_slash" : "chick_move_end_slash",
                        false, error);
  }

  if (strcmp(callback, "gunner_refire_chain") == 0 ||
      strcmp(callback, "guncmdr_refire_chain") == 0) {
    bool commander = strcmp(callback, "guncmdr_refire_chain") == 0;
    if (commander) {
      monster->dodging = false;
      monster->attack_state = Q2M_STRAIGHT;
    }
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    bool repeat = visible && q2m_random(context->game) <= 0.5f;
    const char *move;
    if (!repeat) {
      move = commander ? "guncmdr_move_endfire_chain"
                       : "gunner_move_endfire_chain";
    } else if (commander && !monster->stand_ground &&
               q2m_distance(context, monster->enemy) > 400.0f) {
      move = "guncmdr_move_fire_chain_run";
    } else {
      move = commander ? "guncmdr_move_fire_chain" : "gunner_move_fire_chain";
    }
    return q2m_set_move(context, move, false, error);
  }

  if (strcmp(callback, "hover_reattack") == 0) {
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    const char *move = "hover_move_end_attack";
    if (visible && q2m_random(context->game) <= 0.6f) {
      if (monster->attack_state == Q2M_SLIDING &&
          q2m_move_named(monster, "hover_move_attack2") != NULL)
        move = "hover_move_attack2";
      else if (monster->attack_state == Q2M_STRAIGHT)
        move = "hover_move_attack1";
    }
    return q2m_set_move(context, move, false, error);
  }

  if (strcmp(callback, "jorg_reattack1") == 0) {
    bool visible;
    if (!q2m_visible(context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context)) return true;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    if (visible && (rerelease ? q2_rerelease_float(context->game, 0, 1) :
                              q2m_random(context->game)) < 0.9f)
      return q2m_set_move(context, "jorg_move_attack1", rerelease, error);
    if (rerelease && !q2m_set_move(context, "jorg_move_end_attack1", true, error))
      return false;
    if (!q2m_jorg_sound_end(context, error))
      return false;
    return !q2m_alive(context) || rerelease ||
           q2m_set_move(context, "jorg_move_end_attack1", false, error);
  }

  if (strcmp(callback, "boss5_reattack1") == 0 ||
      strcmp(callback, "supertank_reattack1") == 0) {
    bool supertank = callback[0] == 's';
    bool visible;
    if (!q2m_visible(context, monster->enemy, &visible, error))
      return false;
    float chance =
        supertank && context->game->options.edition == QA_Q2_RERELEASE ? 0.3f
                                                                       : 0.9f;
    bool repeat =
        visible &&
        ((supertank && monster->timestamp_ns >= context->game->now_ns) ||
         q2m_random(context->game) < chance);
    const char *move = supertank ? repeat ? "supertank_move_attack1"
                                          : "supertank_move_end_attack1"
                       : repeat  ? "boss5_move_attack1"
                                 : "boss5_move_end_attack1";
    return q2m_set_move(context, move, false, error);
  }

  if (strcmp(callback, "boss2_reattack_mg") == 0) {
    bool repeat = monster->enemy.registry != 0 && enemy_in_front(context) &&
                  q2m_random(context->game) <= 0.7f;
    return q2m_set_move(
        context, repeat ? "boss2_move_attack_mg" : "boss2_move_attack_post_mg",
        false, error);
  }

  if (strcmp(callback, "tank_reattack_blaster") == 0 ||
      strcmp(callback, "tank_refire_rocket") == 0) {
    bool rocket = strcmp(callback, "tank_refire_rocket") == 0;
    if (monster->manual_steering) {
      monster->manual_steering = false;
      return q2m_set_move(context,
                          rocket ? "tank_move_attack_post_rocket"
                                 : "tank_move_attack_post_blast",
                          false, error);
    }
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    bool skill_allows = context->game->options.edition == QA_Q2_RERELEASE ||
                        context->game->options.skill >= 2;
    bool repeat = skill_allows && visible &&
                  q2m_random(context->game) <= (rocket ? 0.4f : 0.6f);
    return q2m_set_move(context,
                        rocket   ? repeat ? "tank_move_attack_fire_rocket"
                                          : "tank_move_attack_post_rocket"
                        : repeat ? "tank_move_reattack_blast"
                                 : "tank_move_attack_post_blast",
                        false, error);
  }

  if (strcmp(callback, "carrier_reattack_gren") == 0) {
    bool repeat = monster->enemy.registry != 0 && enemy_in_front(context) &&
                  q2m_after(monster->timestamp_ns, 1.3) > context->game->now_ns;
    return q2m_set_move(context,
                        repeat ? "carrier_move_attack_gren"
                               : "carrier_move_attack_post_gren",
                        false, error);
  }

  if (strcmp(callback, "carrier_reattack_mg") == 0) {
    const char *move = "carrier_move_attack_post_mg";
    if (monster->enemy.registry != 0 && enemy_in_front(context) &&
        q2m_random(context->game) <= 0.5f)
      move = q2m_random(context->game) < 0.7f || monster->monster_slots <= 2
                 ? "carrier_move_attack_mg"
                 : "carrier_move_spawn";
    return q2m_set_move(context, move, false, error);
  }

  if (strcmp(callback, "widow_reattack_blaster") == 0) {
    if (!widow_blaster(context, error))
      return false;
    if (!q2m_alive(context) || monster->pause_ns >= context->game->now_ns ||
        !strcmp(monster->move->name, "widow_move_attack_post_blaster_r") ||
        !strcmp(monster->move->name, "widow_move_attack_post_blaster_l"))
      return true;
    monster->manual_steering = false;
    return q2m_set_move(context, "widow_move_attack_post_blaster", false,
                        error);
  }

  if (strcmp(callback, "widow2_reattack_beam") == 0) {
    monster->manual_steering = false;
    bool repeat = monster->enemy.registry != 0 && enemy_in_front(context) &&
                  q2m_random(context->game) <= 0.5f;
    const char *move = "widow2_move_attack_post_beam";
    if (repeat)
      move = q2m_random(context->game) < 0.7f ||
                     !q2m_summon_has_slots(monster, 2)
                 ? "widow2_move_attack_beam"
                 : "widow2_move_spawn";
    return q2m_set_move(context, move, false, error);
  }

  if (strcmp(callback, "widow2_disrupt_reattack") == 0) {
    if (q2m_random(context->game) <
        0.25f + 0.15f * context->game->options.skill)
      monster->next_frame = 28;
    return true;
  }

  *handled = false;
  return true;
}

static bool reattack(q2m_context *context, const char *callback,
                     qa_error *error) {
  if (enemy_alive(context) && q2m_random(context->game) < 0.6f) {
    context->monster->next_frame = context->monster->move->first_frame;
    return true;
  }
  context->monster->hold_frame = false;
  context->monster->manual_steering = false;
  if (has(callback, "chick")) {
    bool found;
    return set_existing_move(context, "chick_move_end_attack1", false, &found,
                             error);
  }
  if (has(callback, "gunner") || has(callback, "guncmdr")) {
    bool found;
    const char *move =
        context->monster->definition->species == Q2M_GUN_COMMANDER
            ? "guncmdr_move_endfire_chain"
            : "gunner_move_endfire_chain";
    return set_existing_move(context, move, false, &found, error);
  }
  return set_definition_move(context, context->monster->definition->run_move,
                             error);
}

static bool end_transition(q2m_context *context, const char *callback,
                           bool *handled, qa_error *error) {
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      (context->monster->definition->species == Q2M_MEDIC ||
       context->monster->definition->species == Q2M_MEDIC_COMMANDER) &&
      (!strcmp(callback, "medic_stand") || !strcmp(callback, "medic_walk"))) {
    *handled = true;
    return q2m_set_move(context, !strcmp(callback, "medic_stand")
                                   ? "medic_move_stand" : "medic_move_walk",
                        true, error);
  }
  if (context->monster->definition->species == Q2M_ACTOR &&
      (!strcmp(callback, "actor_run") || !strcmp(callback, "actor_stand") ||
       !strcmp(callback, "actor_walk"))) {
    *handled = true;
    const char *move = "actor_move_run";
    if (!strcmp(callback, "actor_stand"))
      move = "actor_move_stand";
    else if (!strcmp(callback, "actor_walk"))
      move = "actor_move_walk";
    else if (context->game->now_ns < context->monster->pain_ns &&
             !context->monster->enemy.registry)
      move = context->monster->move_target.registry ? "actor_move_walk" : "actor_move_stand";
    else if (context->monster->stand_ground)
      move = "actor_move_stand";
    if (!q2m_set_move(context, move, true, error))
      return false;
    if (!strcmp(move, "actor_move_stand") && context->game->now_ns < Q2M_SECOND) {
      unsigned frames = (unsigned)(context->monster->move->last_frame -
                                   context->monster->move->first_frame + 1);
      unsigned choice = context->game->options.edition == QA_Q2_RERELEASE
                            ? q2_random_bounded(context->game, frames)
                            : qa_builtin_random_integer(&context->game->random) % frames;
      context->monster->frame = context->monster->move->first_frame + (int)choice;
    }
    return true;
  }
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      (!strcmp(callback, "jorg_stand") || !strcmp(callback, "jorg_run"))) {
    *handled = true;
    bool stand = !strcmp(callback, "jorg_stand") || context->monster->stand_ground;
    if (!q2m_set_move(context, stand ? "jorg_move_stand" : "jorg_move_run", true, error))
      return false;
    return q2m_jorg_sound_end(context, error);
  }
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      (strcmp(callback, "soldier_stand") == 0 ||
       strcmp(callback, "soldier_run") == 0)) {
    *handled = true;
    if (strcmp(callback, "soldier_run") == 0)
      return soldier_run(context, error);
    float draw = q2m_random(context->game);
    const char *move = !context->monster->move ||
                      strcmp(context->monster->move->name, "soldier_move_stand1") != 0 ||
                      draw < .6f ? "soldier_move_stand1"
                        : draw < .8f ? "soldier_move_stand2" : "soldier_move_stand3";
    if (!q2m_set_move(context, move, true, error)) return false;
    return q2m_soldier_sound_end(context, error);
  }
  if (strcmp(callback, "soldier_stand_up") == 0) {
    *handled = true;
    if (!q2m_set_move(context, "soldier_move_trip", false, error))
      return false;
    context->monster->next_frame = 134;
    return true;
  }
  if (strcmp(callback, "guncmdr_kick_finished") == 0) {
    *handled = true;
    context->monster->melee_ns = q2m_after(context->game->now_ns, 3.0);
    return set_definition_move(context, context->monster->definition->run_move,
                               error);
  }
  if (strcmp(callback, "supertank_dead") == 0 ||
      strcmp(callback, "boss5_dead") == 0 ||
      strcmp(callback, "boss2_dead") == 0 ||
      strcmp(callback, "jorg_dead") == 0 ||
      strcmp(callback, "guardian_dead") == 0) {
    *handled = true;
    return q2m_finish_boss_death(context, error);
  }
  for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); ++i) {
    if (strcmp(callback, transitions[i].callback) != 0)
      continue;
    *handled = true;
    bool found;
    if (!set_existing_move(context, transitions[i].move, false, &found, error))
      return false;
    if (found)
      return true;
    return set_definition_move(context, context->monster->definition->run_move,
                               error);
  }
  if (ends_with(callback, "_dead") || strcmp(callback, "soldier_dead2") == 0 ||
      strcmp(callback, "monster_dead") == 0 ||
      strcmp(callback, "widow2_finaldeath") == 0) {
    *handled = true;
    return q2m_corpse_callback(context, callback, error);
  }
  if (ends_with(callback, "_run") || ends_with(callback, "_run_loop") ||
      strcmp(callback, "mutant_walk_loop") == 0) {
    *handled = true;
    if (!strcmp(callback, "guncmdr_run")) {
      context->monster->dodging = false;
      if (context->game->options.edition == QA_Q2_RERELEASE &&
          context->monster->attack_state == Q2M_SLIDING)
        context->monster->attack_state = Q2M_STRAIGHT;
    }
    return set_definition_move(context,
                               context->monster->stand_ground
                                   ? context->monster->definition->stand_move
                                   : context->monster->definition->run_move,
                               error);
  }
  if (ends_with(callback, "_stand") ||
      strcmp(callback, "insane_onground") == 0 ||
      strcmp(callback, "insane_cross") == 0) {
    *handled = true;
    return set_definition_move(context,
                               context->monster->definition->stand_move, error);
  }
  if (ends_with(callback, "_walk")) {
    *handled = true;
    return set_definition_move(context, context->monster->definition->walk_move,
                               error);
  }
  return true;
}

static bool foundational_species_callback(q2m_context *context,
                                          const char *callback, bool *handled,
                                          qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = true;

  if (strcmp(callback, "berserk_swing") == 0)
    return q2m_sound(context, "berserk/attack.wav", 1, 1.0f, error);
  if (strcmp(callback, "berserk_strike") == 0)
    return true;
  if (strcmp(callback, "berserk_high_gravity") == 0) {
    context->actor->physics.gravity_scale =
        context->body.velocity.z < 0.0f ? 2.25f : 5.25f;
    return true;
  }
  if (strcmp(callback, "berserk_check_landing") == 0) {
    context->actor->physics.gravity_scale =
        context->body.velocity.z < 0.0f ? 2.25f : 5.25f;
    if (context->body.ground.registry != 0) {
      if (monster->touch_active)
        return q2m_berserk_land(context, error);
      monster->jump_ns = 0;
      monster->ducked = false;
      context->actor->physics.gravity_scale = 1.0f;
      monster->frame = 163;
      return true;
    }
    monster->next_frame = context->game->now_ns > monster->jump_ns ? 148 : 150;
    return true;
  }

  if (strcmp(callback, "brain_swing_right") == 0)
    return q2m_sound(context, "brain/melee1.wav", 4, 1.0f, error);
  if (strcmp(callback, "brain_swing_left") == 0)
    return q2m_sound(context, "brain/melee2.wav", 4, 1.0f, error);
  if (strcmp(callback, "brain_chest_open") == 0) {
    if (context->game->options.edition == QA_Q2_RERELEASE)
      monster->count = 0;
    else
      monster->spawnflags &= ~UINT32_C(65536);
    context->combat.armor.powered.kind = QA_POWER_NONE;
    qa_q2_combat_power_armor_source(context->game, &context->combat.armor.powered);
    if (!qa_combat_set_armor(context->game->services.combat, context->actor->id,
                             &context->combat.armor, error))
      return false;
    return !q2m_alive(context) ||
           q2m_sound(context, "brain/brnatck1.wav", 4, 1.0f, error);
  }
  if (strcmp(callback, "brain_chest_closed") == 0) {
    context->combat.armor.powered.kind = QA_POWER_SCREEN;
    qa_q2_combat_power_armor_source(context->game, &context->combat.armor.powered);
    if (!qa_combat_set_armor(context->game->services.combat, context->actor->id,
                             &context->combat.armor, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (context->game->options.edition == QA_Q2_RERELEASE) {
      if (!monster->count)
        return true;
      monster->count = 0;
    } else {
      if (!(monster->spawnflags & UINT32_C(65536)))
        return true;
      monster->spawnflags &= ~UINT32_C(65536);
    }
    return q2m_set_move(context, "brain_move_attack1", false, error);
  }

  if (strcmp(callback, "ChickMoan") == 0)
    return q2m_sound(context,
                     q2m_random(context->game) < 0.5f ? "chick/chkidle1.wav"
                                                      : "chick/chkidle2.wav",
                     2, 2.0f, error);
  if (strcmp(callback, "Chick_PreAttack1") == 0)
    return q2m_sound(context, "chick/chkatck1.wav", 2, 1.0f, error);
  if (strcmp(callback, "ChickReload") == 0)
    return q2m_sound(context, "chick/chkatck5.wav", 2, 1.0f, error);
  if (strcmp(callback, "ChickRocket") == 0) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool rogue = !rerelease && context->game->options.product == QA_Q2_ROGUE;
    bool heat = monster->definition->species == Q2M_CHICK_HEAT &&
                monster->skin > 1;
    if (!rerelease && !rogue)
      return fire_source_exact(context,
                               heat ? Q2M_ATTACK_HEAT : Q2M_ATTACK_ROCKET,
                               50.0f, 57, 0.0f, 500.0f, false, 0, error);

    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available) || !available ||
        !q2m_alive(context))
      return true;
    qa_vec3 start;
    if (!q2m_project_flash(context, 57, &start, error))
      return false;
    bool blind = monster->manual_steering;
    float speed = rerelease ? (heat ? 500.0f : 650.0f)
                            : 500.0f + 100.0f * context->game->options.skill;
    qa_vec3 point = blind ? monster->blind_fire_target : enemy.origin;
    if (!blind) {
      bool head = q2m_random(context->game) < 0.33f ||
                  start.z < enemy.origin.z + enemy.bounds.mins.z;
      point.z = head ? enemy.origin.z + traits.view_height
                     : enemy.origin.z + enemy.bounds.mins.z +
                           (rerelease ? 1.0f : 0.0f);
      float prediction_chance =
          rerelease ? 0.35f
                    : 0.2f +
                          (3.0f - (float)context->game->options.skill) * 0.15f;
      if (q2m_random(context->game) < prediction_chance) {
        if (rerelease) {
          qa_vec3 predicted_direction;
          if (!q2m_predict_from(context, start, speed, false, 0.0f, &point,
                                &predicted_direction, &available, error))
            return false;
          if (!available || !q2m_alive(context))
            return true;
        } else {
          float travel = qa_vec_length(qa_vec_sub(point, start)) / speed;
          point = qa_vec_add(point, qa_vec_scale(enemy.velocity, travel));
        }
      }
    }
    qa_vec3 direction;
    uint32_t mask = rerelease ? UINT32_C(0x46004003)
                              : UINT32_C(0x06000003);
    if (blind) {
      if (!blind_direction(context, start, point, 10.0f, mask, &direction,
                           &available, error))
        return false;
      if (!available || !q2m_alive(context))
        return true;
    } else {
      qa_trace_result trace;
      if (!trace_point(context, start, point, mask, &trace, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (rerelease) {
        if (trace.fraction <= 0.5f &&
            (trace.hit == QA_TRACE_HIT_WORLD ||
             trace_hit_brush(context, &trace)))
          return true;
      } else {
        qa_trace_result second;
        if (!trace_point(context, start, point, mask, &second, error))
          return false;
        if (!q2m_alive(context))
          return true;
        bool hit_enemy = second.hit == QA_TRACE_HIT_ACTOR &&
                         qa_actor_id_equal(second.actor, monster->enemy);
        bool hit_world = second.hit == QA_TRACE_HIT_NONE ||
                         second.hit == QA_TRACE_HIT_WORLD ||
                         (second.hit == QA_TRACE_HIT_ACTOR &&
                          context->game->services.physics != NULL &&
                          qa_actor_id_equal(
                              second.actor,
                              context->game->services.physics->world_actor));
        bool hit_player = false;
        if (second.hit == QA_TRACE_HIT_ACTOR &&
            context->game->services.actor_traits != NULL) {
          qa_builtin_actor_traits hit_traits = {0};
          if (context->game->services.actor_traits(
                  context->game->services.context, second.actor, &hit_traits))
            hit_player = hit_traits.player;
        }
        if (!q2m_alive(context))
          return true;
        if ((!hit_enemy && !hit_world) ||
            (second.fraction <= 0.5f && !hit_player))
          return true;
      }
      direction = qa_vec_normalize(qa_vec_sub(point, start));
    }
    q2m_fire_spec spec = q2m_fire_default(
        context, heat ? Q2M_ATTACK_HEAT : Q2M_ATTACK_ROCKET, 50.0f, 57, start,
        direction);
    exact_projectile_speed(&spec, speed);
    spec.radius = 70.0f;
    spec.radius_damage = 50.0f;
    if (rerelease && heat) {
      spec.has_turn_fraction = true;
      spec.turn_fraction = blind ? 0.075f : 0.15f;
    }
    return q2m_fire(context, &spec, error);
  }


  if (strcmp(callback, "floater_fire_blaster") == 0) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool effect = rerelease ? monster->frame % 4 == 0
                            : monster->frame == 34 || monster->frame == 37;
    return fire_source_exact(context, Q2M_ATTACK_BLASTER, 1.0f, 82, 0.0f,
                             1000.0f, true, effect ? 64u : 0u, error);
  }
  if (strcmp(callback, "floater_zap") == 0) {
    if (!q2m_sound(context, "floater/fltatck2.wav", 1, 1.0f, error))
      return false;
    return !q2m_alive(context) ||
           q2m_damage_enemy(context, FLT_MAX, 4, 0,
                            5.0f + floorf(q2m_random(context->game) * 6.0f),
                            -10.0f, NULL, error);
  }

  if (strcmp(callback, "flyer_pop_blades") == 0)
    return q2m_sound(context, "flyer/flyatck1.wav", 2, 1.0f, error);
  if (strcmp(callback, "flyer_fireleft") == 0 ||
      strcmp(callback, "flyer_fireright") == 0) {
    int flash = strcmp(callback, "flyer_fireleft") == 0 ? 58 : 59;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool effect = rerelease ? monster->frame % 4 == 0
                            : monster->frame == 82 || monster->frame == 85 ||
                                  monster->frame == 88;
    return fire_source_exact(context, Q2M_ATTACK_BLASTER, 1.0f, flash, 0.0f,
                             1000.0f, true, effect ? 64u : 0u, error);
  }

  if (strcmp(callback, "gladiator_cleaver_swing") == 0 ||
      strcmp(callback, "gladb_cleaver_swing") == 0)
    return q2m_sound(context, "gladiator/melee1.wav", 1, 1.0f, error);
  if (strcmp(callback, "GladiatorGun") == 0) {
    qa_vec3 start;
    if (!q2m_project_flash(context, 61, &start, error))
      return false;
    qa_vec3 direction = qa_vec_normalize(
        qa_vec_sub(monster->blind_fire_target, start));
    q2m_fire_spec spec = q2m_fire_default(
        context, Q2M_ATTACK_RAIL, 50.0f, 61, start, direction);
    return q2m_fire(context, &spec, error);
  }

  if (strcmp(callback, "gunner_idlesound") == 0)
    return q2m_sound(context, "gunner/gunidle1.wav", 2, 2.0f, error);
  if (strcmp(callback, "gunner_opengun") == 0)
    return q2m_sound(context, "gunner/gunatck1.wav", 2, 2.0f, error);
  if (strcmp(callback, "GunnerGrenade") == 0) {
    return gunner_grenade(context, error);
  }
  if (strcmp(callback, "GunnerFire") == 0) {
    int flash = 45 + monster->frame - 144;
    qa_vec3 start, direction;
    bool available;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool aimed = rerelease
                     ? q2m_predict_shot(context, flash, 0.0f, true, -0.2f,
                                        &start, &direction, &available, error)
                     : q2m_source_shot(context, flash, -0.2f, &start,
                                       &direction, &available, error);
    if (!aimed)
      return false;
    return !available || !q2m_alive(context) ||
           fire_bullet_exact(context, flash, start, direction, 3.0f, 300.0f,
                             500.0f, error);
  }
  if (strcmp(callback, "gunner_duck_down") == 0) {
    if (monster->ducked)
      return true;
    if (context->game->options.skill >= 2 && q2m_random(context->game) > 0.5f &&
        !gunner_grenade(context, error))
      return false;
    if (!q2m_alive(context))
      return true;
    monster->pause_ns = q2m_after(context->game->now_ns, 1.0);
    return set_duck(context, true, error);
  }
  if (strcmp(callback, "gunner_duck_hold") == 0) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  if (strcmp(callback, "gunner_duck_up") == 0)
    return set_duck(context, false, error);

  if (strcmp(callback, "hover_fire_blaster") == 0) {
    bool daedalus = monster->definition->species == Q2M_DAEDALUS;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    int source_flash = rerelease && !daedalus && (monster->frame & 1) != 0
                           ? 263
                           : 62;
    qa_vec3 start, direction;
    bool available;
    if (!q2m_source_shot(context, source_flash, 0.0f, &start, &direction,
                         &available, error))
      return false;
    if (!available || !q2m_alive(context))
      return true;
    q2m_attack_kind kind =
        daedalus ? Q2M_ATTACK_GREEN_BOLT : Q2M_ATTACK_BLASTER;
    int emitted_flash = daedalus ? 145 : source_flash;
    q2m_fire_spec spec = q2m_fire_default(
        context, kind, 1.0f, emitted_flash, start, direction);
    spec.speed = 1000.0f;
    spec.fuse = 2.0f;
    spec.has_projectile_effects = true;
    spec.projectile_effects = daedalus
                                  ? 8u
                                  : rerelease ? monster->frame % 4 == 0 ? 64u
                                                                            : 0u
                                              : monster->frame == 200 ? 64u
                                                                       : 0u;
    return q2m_fire(context, &spec, error);
  }

  if (strcmp(callback, "medic_fire_blaster") == 0) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool rogue = rerelease || context->game->options.product == QA_Q2_ROGUE ||
                 monster->definition->species == Q2M_MEDIC_COMMANDER;
    bool commander = rogue && context->combat.mass > 400;
    bool blaster = monster->frame == 185 || monster->frame == 188;
    int flash;
    if (rerelease)
      flash = blaster ? commander ? 146 : 60
                      : (commander ? 277 : 265) + monster->frame - 195;
    else
      flash = 60;
    qa_vec3 start, direction;
    bool available;
    if (!q2m_source_shot(context, flash, 0.0f, &start, &direction, &available,
                         error))
      return false;
    if (!available || !q2m_alive(context))
      return true;

    qa_builtin_actor_traits enemy_traits = {0};
    if (context->game->services.actor_traits != NULL)
      context->game->services.actor_traits(context->game->services.context,
                                           monster->enemy, &enemy_traits);
    if (!q2m_alive(context))
      return true;
    const char *enemy_classname = qa_strings_cstr(
        qa_session_strings(context->game->services.session),
        enemy_traits.classname);
    const char *tesla_name = rerelease ? "tesla_mine" : "tesla";
    bool tesla = rogue && enemy_classname != NULL &&
                 strcmp(enemy_classname, tesla_name) == 0;
    float damage = tesla ? 3.0f : rerelease && blaster ? 6.0f : 2.0f;
    q2m_attack_kind kind =
        commander ? Q2M_ATTACK_GREEN_BOLT : Q2M_ATTACK_BLASTER;
    int emitted_flash = !rerelease && commander ? 146 : flash;
    q2m_fire_spec spec = q2m_fire_default(
        context, kind, damage, emitted_flash, start, direction);
    spec.speed = 1000.0f;
    spec.fuse = 2.0f;
    spec.has_projectile_effects = true;
    spec.projectile_effects = blaster
                                  ? 8u
                                  : rerelease ? monster->frame % 4 == 0 ? 64u
                                                                            : 0u
                                              : monster->frame == 195 ||
                                                        monster->frame == 198 ||
                                                        monster->frame == 201 ||
                                                        monster->frame == 204
                                                    ? 64u
                                                    : 0u;
    return q2m_fire(context, &spec, error);
  }

  if (strcmp(callback, "mutant_step") == 0 ||
      strcmp(callback, "gekk_step") == 0) {
    unsigned variant =
        ((unsigned)(q2m_random(context->game) * 3.0f) + 1u) % 3u + 1u;
    char path[32];
    snprintf(path, sizeof(path),
             strcmp(callback, "mutant_step") == 0 ? "mutant/step%u.wav"
                                                  : "gek/gk_step%u.wav",
             variant);
    return q2m_sound(context, path, 2, 1.0f, error);
  }
  if (strcmp(callback, "mutant_jump_takeoff") == 0) {
    qa_vec3 forward;
    qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
    if (!q2m_sound(context, "mutant/mutsght1.wav", 2, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    context->body.origin.z += 1.0f;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    context->body.velocity = qa_vec_scale(forward, rerelease ? 425.0f : 600.0f);
    context->body.velocity.z = rerelease ? 160.0f : 250.0f;
    context->body.ground = (qa_actor_id){0};
    monster->ducked = true;
    monster->touch_active = true;
    if (rerelease)
      monster->style = 1;
    monster->attack_ns = q2m_after(context->game->now_ns, 3.0);
    return q2m_write_body(context, true, error);
  }
  if (strcmp(callback, "mutant_check_landing") == 0) {
    if (context->body.ground.registry != 0) {
      monster->attack_ns = 0;
      monster->ducked = false;
      monster->touch_active = false;
      return q2m_sound(context, "mutant/thud1.wav", 1, 1.0f, error);
    }
    monster->next_frame = context->game->now_ns > monster->attack_ns ? 1 : 4;
    return true;
  }

  if (strcmp(callback, "gekk_jump_takeoff") == 0 ||
      strcmp(callback, "gekk_jump_takeoff2") == 0) {
    bool from_water = strcmp(callback, "gekk_jump_takeoff2") == 0;
    qa_body_state enemy;
    qa_error ignored = {0};
    bool has_enemy = monster->enemy.registry != 0 &&
                     qa_world_body_read(context->game->services.world,
                                        monster->enemy, &enemy, &ignored);
    bool long_jump = false;
    if (has_enemy) {
      float minimum = enemy.origin.z + enemy.bounds.mins.z;
      float height = enemy.bounds.maxs.z - enemy.bounds.mins.z;
      bool overlaps = context->body.origin.z + context->body.bounds.mins.z <=
                          minimum + 0.75f * height &&
                      context->body.origin.z + context->body.bounds.maxs.z >=
                          minimum + 0.25f * height;
      float dx = context->body.origin.x - enemy.origin.x;
      float dy = context->body.origin.y - enemy.origin.y;
      float distance = hypotf(dx, dy);
      long_jump = overlaps && distance >= 100.0f &&
                  (distance == 100.0f || q2m_random(context->game) >= 0.9f);
    }
    if (!q2m_sound(context, "gek/gk_sght1.wav", 2, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
    if (from_water && has_enemy)
      context->body.origin.z = enemy.origin.z;
    else
      context->body.origin.z += 1.0f;
    float forward_speed = from_water  ? long_jump ? 300.0f : 150.0f
                          : long_jump ? 700.0f
                                      : 250.0f;
    context->body.velocity = qa_vec_scale(forward, forward_speed);
    context->body.velocity.z = from_water  ? long_jump ? 250.0f : 300.0f
                               : long_jump ? 250.0f
                                           : 400.0f;
    context->body.ground = (qa_actor_id){0};
    monster->ducked = true;
    monster->touch_active = true;
    monster->jump_ns = q2m_after(context->game->now_ns, 3.0);
    return q2m_write_body(context, true, error);
  }
  if (strcmp(callback, "gekk_check_landing") == 0) {
    if (context->body.ground.registry != 0) {
      monster->jump_ns = 0;
      monster->ducked = false;
      monster->touch_active = false;
      context->body.velocity = qa_v3(0, 0, 0);
      return q2m_write_body(context, false, error) &&
             q2m_sound(context, "mutant/thud1.wav", 1, 1.0f, error);
    }
    monster->next_frame = context->game->now_ns > monster->jump_ns ? 81 : 82;
    return true;
  }

  if (strcmp(callback, "berserk_jump_takeoff") == 0) {
    qa_body_state enemy;
    qa_error ignored = {0};
    if (monster->enemy.registry == 0 ||
        !qa_world_body_read(context->game->services.world, monster->enemy,
                            &enemy, &ignored))
      return true;
    float speed =
        qa_vec_length(qa_vec_sub(context->body.origin, enemy.origin)) * 1.95f;
    if (speed <= 0.0f)
      return true;
    float travel =
        qa_vec_length(qa_vec_sub(enemy.origin, context->body.origin)) / speed;
    qa_vec3 target =
        qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, travel));
    qa_vec3 direction =
        qa_vec_normalize(qa_vec_sub(target, context->body.origin));
    context->body.angles.y =
        atan2f(direction.y, direction.x) * 57.29577951308232f;
    qa_vec3 forward;
    qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
    context->body.origin.z += 1.0f;
    context->body.velocity = qa_vec_scale(forward, speed);
    context->body.velocity.z = 450.0f;
    context->body.ground = (qa_actor_id){0};
    context->actor->physics.gravity_scale = 5.25f;
    monster->ducked = true;
    monster->touch_active = true;
    monster->jump_ns = q2m_after(context->game->now_ns, 3.0);
    return q2m_write_body(context, true, error);
  }

  if (strcmp(callback, "parasite_launch") == 0)
    return q2m_sound(context, "parasite/paratck1.wav", 1, 1.0f, error);
  if (strcmp(callback, "parasite_reel_in") == 0)
    return q2m_sound(context, "parasite/paratck4.wav", 1, 1.0f, error);
  if (strcmp(callback, "parasite_tap") == 0)
    return q2m_sound(context, "parasite/paridle1.wav", 1, 2.0f, error);
  if (strcmp(callback, "parasite_scratch") == 0)
    return q2m_sound(context, "parasite/paridle2.wav", 1, 2.0f, error);
  if (strcmp(callback, "parasite_drain_attack") == 0) {
    bool first = monster->frame == 41;
    if (first && !q2m_sound(context, "parasite/paratck2.wav", 0, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (monster->frame == 42 &&
        !q2m_sound(context, "parasite/paratck3.wav", 1, 1.0f, error))
      return false;
    return !q2m_alive(context) ||
           q2m_damage_enemy(context, 256.0f, 8, 0, first ? 5.0f : 2.0f, 0.0f,
                            NULL, error);
  }

  *handled = false;
  return true;
}

static bool boss2_bullet(q2m_context *context, int flash, qa_error *error) {
  qa_vec3 start, direction;
  bool available;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  bool rogue = !rerelease && context->game->options.product == QA_Q2_ROGUE;
  float lead = rogue && flash == 133 ? 0.2f : -0.2f;
  bool aimed = rerelease
                   ? q2m_predict_shot(context, flash, 0.0f, true, -0.2f,
                                      &start, &direction, &available, error)
                   : q2m_source_shot(context, flash, lead, &start, &direction,
                                     &available, error);
  if (!aimed)
    return false;
  if (!available || !q2m_alive(context))
    return true;
  float horizontal = rerelease || rogue ? 900.0f : 300.0f;
  return fire_bullet_exact(context, flash, start, direction, 6.0f, horizontal,
                           500.0f, error);
}

static bool boss2_rockets(q2m_context *context, bool force_predictive,
                          qa_error *error) {
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  bool rogue = !rerelease && context->game->options.product == QA_Q2_ROGUE;
  if (!rerelease && !rogue) {
    for (int flash = 78; flash <= 81; ++flash) {
      if (!fire_source_exact(context, Q2M_ATTACK_ROCKET, 50.0f, flash, 0.0f,
                             500.0f, false, 0, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    return true;
  }

  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  bool predictive = force_predictive;
  if (!predictive && traits.player)
    predictive = q2m_random(context->game) < 0.9f;
  if (!q2m_alive(context))
    return true;

  static const float spread[4] = {0.4f, 0.025f, -0.025f, -0.4f};
  static const float rerelease_lead[4] = {-0.1f, -0.05f, 0.05f, 0.1f};
  qa_vec3 right;
  qa_builtin_angle_vectors(context->body.angles, NULL, &right, NULL);
  for (unsigned index = 0; index < 4; ++index) {
    int flash = 78 + (int)index;
    qa_vec3 start, direction;
    if (predictive) {
      if (rerelease) {
        if (!q2m_predict_shot(context, flash, 750.0f, false,
                              rerelease_lead[index], &start, &direction,
                              &available, error))
          return false;
        if (!available || !q2m_alive(context))
          return true;
      } else {
        if (!q2m_project_flash(context, flash, &start, error))
          return false;
        float travel = qa_vec_length(qa_vec_sub(enemy.origin, start)) / 750.0f;
        qa_vec3 point = qa_vec_add(
            enemy.origin,
            qa_vec_scale(enemy.velocity, travel - 0.3f + index * 0.15f));
        direction = qa_vec_normalize(qa_vec_sub(point, start));
      }
    } else {
      if (!q2m_project_flash(context, flash, &start, error))
        return false;
      qa_vec3 point = enemy.origin;
      if (index == 0 || index == 3)
        point.z -= 15.0f;
      direction = qa_vec_normalize(
          qa_vec_add(qa_vec_normalize(qa_vec_sub(point, start)),
                     qa_vec_scale(right, spread[index])));
    }
    if (!fire_rocket_exact(context, Q2M_ATTACK_ROCKET, flash, start, direction,
                           50.0f, predictive ? 750.0f : 500.0f, 70.0f,
                           50.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  return true;
}

static bool boss2_rocket64(q2m_context *context, qa_error *error) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  qa_vec3 start;
  if (!q2m_project_flash(context, 78, &start, error))
    return false;
  qa_vec3 right;
  qa_builtin_angle_vectors(context->body.angles, NULL, &right, NULL);
  float scale = context->monster->entity_scale;
  unsigned barrel = (unsigned)context->monster->count++ % 4u;
  start.z += 10.0f * scale;
  start = qa_vec_sub(
      start, qa_vec_scale(right, (2.0f + (float)barrel * 8.0f) * scale));
  bool predictive = traits.player && q2m_random(context->game) < 0.9f;
  if (!q2m_alive(context))
    return true;
  qa_vec3 point;
  if (predictive) {
    float travel = qa_vec_length(qa_vec_sub(enemy.origin, start)) / 750.0f;
    point = qa_vec_add(enemy.origin,
                       qa_vec_scale(enemy.velocity, travel - 0.3f));
  } else {
    point = enemy.origin;
    point.z -= 15.0f;
  }
  qa_vec3 direction = qa_vec_normalize(qa_vec_sub(point, start));
  return fire_rocket_exact(context, Q2M_ATTACK_ROCKET, 78, start, direction,
                           35.0f, 750.0f, 55.0f, 35.0f, error);
}

static bool boss2_hyperblaster(q2m_context *context, qa_error *error) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  int flash = (context->monster->frame & 1) != 0 ? 74 : 134;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_vec3 target = enemy.origin;
  target.z += traits.view_height;
  qa_vec3 direction = qa_vec_normalize(qa_vec_sub(target, start));
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_BLASTER, 2.0f, flash, start, direction);
  spec.speed = 1000.0f;
  spec.has_projectile_effects = true;
  spec.projectile_effects = context->monster->frame % 4 == 0 ? 64u : 0u;
  return q2m_fire(context, &spec, error);
}

static bool jorg_bullets(q2m_context *context, int only_flash,
                         qa_error *error) {
  static const int flashes[2] = {120, 126};
  for (unsigned index = 0; index < 2; ++index) {
    int flash = flashes[index];
    if (only_flash >= 0 && flash != only_flash)
      continue;
    qa_vec3 start, direction;
    bool available;
    bool aimed = context->game->options.edition == QA_Q2_RERELEASE
                     ? q2m_predict_shot(context, flash, 0.0f, false,
                                        flash == 120 ? 0.2f : -0.2f, &start,
                                        &direction, &available, error)
                     : q2m_source_shot(context, flash, -0.2f, &start,
                                       &direction, &available, error);
    if (!aimed)
      return false;
    if (!available || !q2m_alive(context))
      return true;
    if (!fire_bullet_exact(context, flash, start, direction, 6.0f, 300.0f,
                           500.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
  }
  return true;
}

static bool makron_save_location(q2m_context *context) {
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  context->monster->blind_fire_target = enemy.origin;
  context->monster->blind_fire_target.z += traits.view_height;
  return true;
}

static bool makron_hyperblaster(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int source_flash = 102 + frame - 213;
  qa_vec3 start;
  if (!q2m_project_flash(context, source_flash, &start, error))
    return false;
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !q2m_alive(context))
    return true;
  float pitch = 0.0f;
  if (available) {
    qa_vec3 eye = enemy.origin;
    eye.z += traits.view_height;
    pitch = q2m_vector_angles(qa_vec_sub(eye, start)).x;
  }
  float yaw = context->body.angles.y;
  yaw += frame <= 221 ? -10.0f * (float)(frame - 221)
                      : 10.0f * (float)(frame - 229);
  qa_vec3 direction;
  qa_builtin_angle_vectors(qa_v3(pitch, yaw, 0.0f), &direction, NULL, NULL);
  int emitted_flash =
      context->game->options.edition == QA_Q2_RERELEASE ? source_flash : 102;
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_BLASTER, 15.0f, emitted_flash, start, direction);
  spec.speed = 1000.0f;
  spec.has_projectile_effects = true;
  spec.projectile_effects = 8u;
  return q2m_fire(context, &spec, error);
}

static bool supertank_machinegun(q2m_context *context, qa_error *error) {
  int flash = 64 + context->monster->frame;
  qa_vec3 angles = context->body.angles;
  angles.x = 0.0f;
  angles.z = 0.0f;
  qa_vec3 start;
  if (!project_flash_angles(context, flash, angles, &start, error))
    return false;
  qa_vec3 direction;
  bool available;
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    if (!q2m_predict_from(context, start, 0.0f, true, -0.1f, NULL,
                          &direction, &available, error))
      return false;
    if (!available || !q2m_alive(context))
      return true;
    return fire_bullet_exact(context, flash, start, direction, 6.0f, 900.0f,
                             1500.0f, error);
  }
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  if (!target_body(context, &enemy, &traits, &available) || !q2m_alive(context))
    return true;
  qa_builtin_angle_vectors(angles, &direction, NULL, NULL);
  if (available) {
    qa_vec3 eye = enemy.origin;
    eye.z += traits.view_height;
    direction = qa_vec_normalize(qa_vec_sub(eye, start));
  }
  return fire_bullet_exact(context, flash, start, direction, 6.0f, 300.0f,
                           500.0f, error);
}

static bool supertank_rocket(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int flash = frame == 27 ? 70 : frame == 30 ? 71 : 72;
  if (context->game->options.edition != QA_Q2_RERELEASE)
    return fire_source_exact(context, Q2M_ATTACK_ROCKET, 50.0f, flash, 0.0f,
                             500.0f, false, 0, error);

  bool heat = (context->monster->spawnflags & UINT32_C(8)) != 0;
  qa_vec3 start, direction;
  bool available;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  if (heat) {
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    if (!target_body(context, &enemy, &traits, &available) || !available ||
        !q2m_alive(context))
      return true;
    qa_vec3 eye = enemy.origin;
    eye.z += traits.view_height;
    direction = qa_vec_normalize(qa_vec_sub(eye, start));
    return fire_rocket_exact(context, Q2M_ATTACK_HEAT, flash, start, direction,
                             40.0f, 500.0f, 60.0f, 40.0f, error);
  }
  if (!q2m_predict_from(context, start, 750.0f, false, 0.0f, NULL,
                        &direction, &available, error))
    return false;
  return !available || !q2m_alive(context) ||
         fire_rocket_exact(context, Q2M_ATTACK_ROCKET, flash, start, direction,
                           50.0f, 750.0f, 70.0f, 50.0f, error);
}

static bool blind_direction(q2m_context *context, qa_vec3 start,
                            qa_vec3 target, float side, uint32_t mask,
                            qa_vec3 *direction, bool *available,
                            qa_error *error) {
  qa_vec3 right;
  qa_builtin_angle_vectors(context->body.angles, NULL, &right, NULL);
  static const float factors[3] = {0.0f, -1.0f, 1.0f};
  *available = false;
  for (unsigned index = 0; index < 3; ++index) {
    qa_vec3 point =
        qa_vec_add(target, qa_vec_scale(right, factors[index] * side));
    qa_trace_query query = {
        .start = start,
        .end = point,
        .pass_actor = context->actor->id,
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    query.policy.contents_mask = mask;
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!trace.start_solid && !trace.all_solid && trace.fraction >= 0.5f) {
      *direction = qa_vec_normalize(qa_vec_sub(point, start));
      *available = true;
      return true;
    }
  }
  return true;
}

static bool tank_blaster(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int flash = frame == 64 ? 1 : frame == 67 ? 2 : 3;
  qa_vec3 start, direction;
  bool available;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      context->monster->manual_steering) {
    if (!blind_direction(context, start, context->monster->blind_fire_target,
                         20.0f, UINT32_C(0x46004003), &direction, &available,
                         error))
      return false;
  } else if (context->game->options.edition == QA_Q2_RERELEASE) {
    if (!q2m_predict_from(context, start, 0.0f, false, 0.0f, NULL,
                          &direction, &available, error))
      return false;
  } else {
    if (!q2m_source_shot(context, flash, 0.0f, &start, &direction, &available,
                         error))
      return false;
  }
  if (!available || !q2m_alive(context))
    return true;
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_BLASTER, 30.0f, flash, start, direction);
  spec.speed = 800.0f;
  spec.has_projectile_effects = true;
  spec.projectile_effects = 8u;
  return q2m_fire(context, &spec, error);
}

static bool tank_machinegun(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int flash = 4 + frame - 173;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !q2m_alive(context))
    return true;
  if (!available && context->game->options.edition == QA_Q2_RERELEASE)
    return true;
  float pitch = 0.0f;
  if (available) {
    qa_vec3 eye = enemy.origin;
    eye.z += traits.view_height;
    pitch = q2m_vector_angles(qa_vec_sub(eye, start)).x;
  }
  float yaw = context->body.angles.y;
  yaw += frame <= 182 ? -8.0f * (float)(frame - 178)
                      : 8.0f * (float)(frame - 186);
  qa_vec3 direction;
  qa_builtin_angle_vectors(qa_v3(pitch, yaw, 0.0f), &direction, NULL, NULL);
  return fire_bullet_exact(context, flash, start, direction, 20.0f, 300.0f,
                           500.0f, error);
}

static bool trace_point(q2m_context *context, qa_vec3 start, qa_vec3 end,
                        uint32_t mask, qa_trace_result *trace,
                        qa_error *error) {
  qa_trace_query query = {
      .start = start,
      .end = end,
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  query.policy.contents_mask = mask;
  return qa_world_trace(context->game->services.world, &query, trace, error);
}

static bool mortar_direction(q2m_context *context, qa_vec3 target,
                             qa_vec3 start, qa_vec3 aim, float speed,
                             float gravity, bool mortar, qa_vec3 *direction,
                             bool *available, qa_error *error) {
  static const float pitches[] = {-80.0f, -70.0f, -60.0f, -50.0f, -40.0f,
                                  -30.0f, -20.0f, -10.0f, -5.0f};
  qa_vec3 angles = q2m_vector_angles(aim);
  float best_pitch = 0.0f;
  float best_distance = FLT_MAX;
  *available = false;
  for (size_t pitch_index = 0;
       pitch_index < sizeof(pitches) / sizeof(pitches[0]); ++pitch_index) {
    float pitch = pitches[pitch_index];
    if (mortar && pitch >= -30.0f)
      break;
    qa_vec3 velocity;
    qa_builtin_angle_vectors(qa_v3(pitch, angles.y, angles.z), &velocity, NULL,
                             NULL);
    velocity = qa_vec_scale(velocity, speed);
    qa_vec3 origin = start;
    for (unsigned step = 0; step < 25; ++step) {
      velocity.z -= gravity * 0.1f;
      qa_trace_query query = {
          .start = origin,
          .end = qa_vec_add(origin, qa_vec_scale(velocity, 0.1f)),
          .policy = qa_collision_default_policy(QA_COLLISION_Q2),
      };
      query.policy.contents_mask = UINT32_C(0x46000003);
      qa_trace_result trace;
      if (!qa_world_trace(context->game->services.world, &query, &trace,
                          error))
        return false;
      if (!q2m_alive(context))
        return true;
      origin = trace.end;
      if (trace.fraction >= 1.0f)
        continue;
      if (trace.has_surface && (trace.surface_flags & 4) != 0)
        break;
      qa_vec3 normal =
          trace.contact ? trace.contact_plane.normal : qa_v3(0.0f, 0.0f, 0.0f);
      origin = qa_vec_add(origin, normal);
      velocity = qa_vec_sub(
          velocity,
          qa_vec_scale(normal, qa_vec_dot(velocity, normal) * 1.6f));
      qa_vec3 delta = qa_vec_sub(origin, target);
      float distance = qa_vec_dot(delta, delta);
      bool actor_target = false;
      if (trace.hit == QA_TRACE_HIT_ACTOR) {
        actor_target =
            qa_actor_id_equal(trace.actor, context->monster->enemy);
        if (!actor_target && context->game->services.actor_traits != NULL) {
          qa_builtin_actor_traits traits = {0};
          if (context->game->services.actor_traits(
                  context->game->services.context, trace.actor, &traits))
            actor_target = traits.player;
          if (!q2m_alive(context))
            return true;
        }
      }
      if (actor_target ||
          (normal.z >= 0.7f && distance < 128.0f * 128.0f &&
           distance < best_distance)) {
        best_pitch = pitch;
        best_distance = distance;
      }
      if (((uint32_t)trace.contents & UINT32_C(0x46000000)) != 0)
        break;
    }
  }
  if (best_distance == FLT_MAX)
    return true;
  qa_builtin_angle_vectors(qa_v3(best_pitch, angles.y, angles.z), direction,
                           NULL, NULL);
  *available = true;
  return true;
}

static bool gunner_grenade(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  bool rogue = !rerelease && context->game->options.product == QA_Q2_ROGUE;
  int frame = monster->frame;
  if (!rerelease && !rogue) {
    int flash = frame == 112 ? 53 : frame == 115 ? 54 : frame == 118 ? 55 : 56;
    return fire_forward_exact(context, Q2M_ATTACK_GRENADE, 50.0f, flash,
                              600.0f, error);
  }

  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  (void)traits;
  unsigned index = frame == 112 || frame == 233 ? 0
                   : frame == 115 || frame == 236 ? 1
                   : frame == 118 || frame == 239 ? 2
                                                  : 3;
  bool blind = monster->manual_steering;
  if (index == 3)
    monster->manual_steering = false;
  bool blind_target = false;
  if (blind) {
    bool visible;
    if (!q2m_visible(context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context))
      return true;
    blind_target = !visible;
  }
  qa_vec3 target = blind_target ? monster->blind_fire_target
                                : rerelease ? enemy.origin
                                            : context->body.origin;
  if (blind_target && qa_vec_length(target) == 0.0f)
    return true;

  int flash = 53 + (int)index;
  float spread;
  if (rerelease) {
    static const float spreads[] = {-0.1f, -0.05f, 0.05f, 0.1f};
    spread = spreads[index];
    if (frame >= 229 && frame <= 248)
      flash = 256 + 56 - flash;
  } else {
    spread = 0.02f + (float)index * 0.03f;
  }
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_vec3 forward, right, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  qa_vec3 delta = qa_vec_sub(target, context->body.origin);
  float distance = qa_vec_length(delta);
  if (distance > 512.0f && delta.z < 64.0f && delta.z > -64.0f)
    delta.z += distance - 512.0f;
  float pitch = fmaxf(-0.5f, fminf(0.4f, qa_vec_normalize(delta).z));
  qa_vec3 aim = qa_vec_add(
      qa_vec_add(forward, qa_vec_scale(right, spread)), qa_vec_scale(up, pitch));
  qa_vec3 direction = aim;
  bool predicted = false;
  float gravity = context->game->services.physics != NULL
                      ? context->game->services.physics->gravity
                      : 800.0f;
  if (rerelease) {
    if (!mortar_direction(context, target, start, aim, 600.0f, gravity, false,
                          &direction, &predicted, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!predicted)
      direction = aim;
  }
  q2m_fire_spec spec = q2m_fire_default(
      context, Q2M_ATTACK_GRENADE, 50.0f, flash, start, direction);
  spec.speed = 600.0f;
  spec.fuse = 2.5f;
  spec.radius = 90.0f;
  spec.radius_damage = 50.0f;
  if (rerelease) {
    spec.has_grenade_impulse = true;
    spec.grenade_right = q2m_crandom(context->game) * 10.0f;
    spec.grenade_up = predicted ? q2m_random(context->game) * 10.0f
                                : 200.0f + q2m_crandom(context->game) * 10.0f;
    spec.grenade_gravity = gravity;
  }
  return q2m_fire(context, &spec, error);
}

static bool supertank_grenade(q2m_context *context, qa_error *error) {
  if (context->game->options.edition != QA_Q2_RERELEASE)
    return true;
  int flash = context->monster->frame == 74 ? 261 : 262;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_vec3 target, aim;
  bool available;
  float offset = q2m_crandom(context->game) * 0.1f;
  if (!q2m_predict_from(context, start, 0.0f, false, offset, &target, &aim,
                        &available, error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  float gravity = context->game->services.physics != NULL
                      ? context->game->services.physics->gravity
                      : 800.0f;
  qa_vec3 direction;
  for (float speed = 500.0f; speed < 1000.0f; speed += 100.0f) {
    if (!mortar_direction(context, target, start, aim, speed, gravity, true,
                          &direction, &available, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!available)
      continue;
    q2m_fire_spec spec = q2m_fire_default(
        context, Q2M_ATTACK_GRENADE, 50.0f, flash, start, direction);
    spec.speed = speed;
    spec.fuse = 2.5f;
    spec.radius = 90.0f;
    spec.radius_damage = 50.0f;
    spec.has_grenade_impulse = true;
    spec.grenade_right = 0.0f;
    spec.grenade_up = 0.0f;
    spec.grenade_gravity = gravity;
    return q2m_fire(context, &spec, error);
  }
  return true;
}

static bool tank_rocket(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int flash = frame == 138 ? 23 : frame == 141 ? 24 : 25;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  bool rogue = !rerelease && context->game->options.product == QA_Q2_ROGUE;
  if (!rerelease && !rogue)
    return fire_source_exact(context, Q2M_ATTACK_ROCKET, 50.0f, flash, 0.0f,
                             550.0f, false, 0, error);

  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;

  bool blind = context->monster->manual_steering;
  if (rogue) {
    float speed = 500.0f + 100.0f * (float)context->game->options.skill;
    qa_vec3 point = blind ? context->monster->blind_fire_target : enemy.origin;
    if (!blind) {
      bool head = q2m_random(context->game) < 0.66f ||
                  start.z < enemy.origin.z + enemy.bounds.mins.z;
      point.z = head ? enemy.origin.z + traits.view_height
                     : enemy.origin.z + enemy.bounds.mins.z;
      if (q2m_random(context->game) <
          0.2f + (3.0f - (float)context->game->options.skill) * 0.15f) {
        float travel = qa_vec_length(qa_vec_sub(point, start)) / speed;
        point = qa_vec_add(point, qa_vec_scale(enemy.velocity, travel));
      }
    }
    if (!q2m_alive(context))
      return true;
    qa_vec3 direction;
    if (blind) {
      if (!blind_direction(context, start, point, 20.0f,
                           UINT32_C(0x06000003), &direction, &available,
                           error))
        return false;
      if (!available || !q2m_alive(context))
        return true;
    } else {
      qa_trace_result discarded, trace;
      if (!trace_point(context, start, point, UINT32_C(0x06000003),
                       &discarded, error))
        return false;
      if (!q2m_alive(context))
        return true;
      if (!trace_point(context, start, point, UINT32_C(0x06000003), &trace,
                       error))
        return false;
      if (!q2m_alive(context))
        return true;
      bool hit_enemy = trace.hit == QA_TRACE_HIT_ACTOR &&
                       qa_actor_id_equal(trace.actor, context->monster->enemy);
      bool hit_world = trace.hit == QA_TRACE_HIT_NONE ||
                       trace.hit == QA_TRACE_HIT_WORLD ||
                       (trace.hit == QA_TRACE_HIT_ACTOR &&
                        context->game->services.physics != NULL &&
                        qa_actor_id_equal(
                            trace.actor,
                            context->game->services.physics->world_actor));
      bool hit_player = false;
      if (trace.hit == QA_TRACE_HIT_ACTOR &&
          context->game->services.actor_traits != NULL) {
        qa_builtin_actor_traits hit_traits = {0};
        if (context->game->services.actor_traits(
                context->game->services.context, trace.actor, &hit_traits))
          hit_player = hit_traits.player;
      }
      if (!q2m_alive(context))
        return true;
      if ((!hit_enemy && !hit_world) ||
          (trace.fraction <= 0.5f && !hit_player))
        return true;
      direction = qa_vec_normalize(qa_vec_sub(point, start));
    }
    int emitted_flash = blind ? flash : 57;
    return fire_rocket_exact(context, Q2M_ATTACK_ROCKET, emitted_flash, start,
                             direction, 50.0f, speed, 70.0f, 50.0f, error);
  }

  bool heat = (context->monster->spawnflags & UINT32_C(16)) != 0;
  float speed = heat ? 500.0f : 650.0f;
  qa_vec3 point = context->monster->blind_fire_target;
  if (!blind) {
    bool eye = q2m_random(context->game) < 0.66f ||
               start.z < enemy.origin.z + enemy.bounds.mins.z;
    point = enemy.origin;
    point.z += eye ? traits.view_height : enemy.bounds.mins.z + 1.0f;
    if (q2m_random(context->game) <
        0.2f + (3.0f - (float)context->game->options.skill) * 0.15f) {
      qa_vec3 predicted_direction;
      if (!q2m_predict_from(context, start, speed, false, 0.0f, &point,
                            &predicted_direction, &available, error))
        return false;
      if (!available || !q2m_alive(context))
        return true;
    }
  }
  qa_vec3 direction;
  if (blind) {
    if (!blind_direction(context, start, point, 20.0f,
                         UINT32_C(0x46004003), &direction, &available,
                         error))
      return false;
    if (!available || !q2m_alive(context))
      return true;
  } else {
    direction = qa_vec_normalize(qa_vec_sub(point, start));
    qa_trace_result trace;
    if (!trace_point(context, start, point, UINT32_C(0x46004003), &trace,
                     error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (trace.fraction <= 0.5f &&
        (trace.hit == QA_TRACE_HIT_WORLD ||
         trace_hit_brush(context, &trace)))
      return true;
  }
  return fire_rocket_exact(context,
                           heat ? Q2M_ATTACK_HEAT : Q2M_ATTACK_ROCKET, flash,
                           start, direction, 50.0f, speed, 70.0f, 50.0f,
                           error);
}

static bool heavy_weapon_callback(q2m_context *context, const char *callback,
                                  bool *handled, qa_error *error) {
  *handled = true;
  if (strcmp(callback, "Boss2MachineGun") == 0)
    return boss2_bullet(context, 73, error) &&
           (!q2m_alive(context) || boss2_bullet(context, 133, error));
  if (strcmp(callback, "boss2_firebullet_left") == 0)
    return boss2_bullet(context, 73, error);
  if (strcmp(callback, "boss2_firebullet_right") == 0)
    return boss2_bullet(context, 133, error);
  if (strcmp(callback, "Boss2Rocket") == 0)
    return boss2_rockets(context, false, error);
  if (strcmp(callback, "Boss2PredictiveRocket") == 0)
    return boss2_rockets(context, true, error);
  if (strcmp(callback, "Boss2Rocket64") == 0)
    return boss2_rocket64(context, error);
  if (strcmp(callback, "Boss2HyperBlaster") == 0)
    return boss2_hyperblaster(context, error);
  if (strcmp(callback, "jorg_firebullet") == 0)
    return jorg_bullets(context, -1, error);
  if (strcmp(callback, "jorg_firebullet_left") == 0)
    return jorg_bullets(context, 120, error);
  if (strcmp(callback, "jorg_firebullet_right") == 0)
    return jorg_bullets(context, 126, error);
  if (strcmp(callback, "jorgBFG") == 0) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    if (!q2m_sound(context,
                   rerelease ? "makron/bfg_fire.wav" : "boss3/bs3atck2.wav",
                   rerelease ? 1 : 2, 1.0f, error))
      return false;
    return !q2m_alive(context) || fire_bfg_exact(context, 132, 200.0f, error);
  }
  if (strcmp(callback, "makronBFG") == 0) {
    if (!q2m_sound(context, "makron/bfg_fire.wav", 2, 1.0f, error))
      return false;
    return !q2m_alive(context) || fire_bfg_exact(context, 101, 300.0f, error);
  }
  if (strcmp(callback, "MakronSaveloc") == 0)
    return makron_save_location(context);
  if (strcmp(callback, "MakronRailgun") == 0) {
    qa_vec3 start;
    if (!q2m_project_flash(context, 119, &start, error))
      return false;
    qa_vec3 direction = qa_vec_normalize(
        qa_vec_sub(context->monster->blind_fire_target, start));
    q2m_fire_spec spec = q2m_fire_default(
        context, Q2M_ATTACK_RAIL, 50.0f, 119, start, direction);
    return q2m_fire(context, &spec, error);
  }
  if (strcmp(callback, "MakronHyperblaster") == 0)
    return makron_hyperblaster(context, error);
  if (strcmp(callback, "supertankMachineGun") == 0 ||
      strcmp(callback, "boss5MachineGun") == 0)
    return supertank_machinegun(context, error);
  if (strcmp(callback, "supertankRocket") == 0 ||
      strcmp(callback, "boss5Rocket") == 0)
    return supertank_rocket(context, error);
  if (strcmp(callback, "supertankGrenade") == 0)
    return supertank_grenade(context, error);
  if (strcmp(callback, "TankBlaster") == 0)
    return tank_blaster(context, error);
  if (strcmp(callback, "TankRocket") == 0)
    return tank_rocket(context, error);
  if (strcmp(callback, "TankMachineGun") == 0)
    return tank_machinegun(context, error);
  if (strcmp(callback, "CarrierMachineGun") == 0 ||
      strcmp(callback, "CarrierMachineGunHold") == 0)
    return carrier_machineguns(context, error);
  if (strcmp(callback, "CarrierRocket") == 0)
    return carrier_rocket(context, error);
  if (strcmp(callback, "CarrierGrenade") == 0)
    return carrier_grenade(context, error);
  if (strcmp(callback, "CarrierRail") == 0)
    return carrier_rail(context, error);
  if (strcmp(callback, "CarrierSaveLoc") == 0)
    return carrier_coop_check(context, error) &&
           (!q2m_alive(context) || carrier_save_location(context));
  *handled = false;
  return true;
}

bool q2m_dispatch(q2m_context *context, const char *callback, qa_error *error) {
  if (!q2m_alive(context) || callback == NULL)
    return true;
  struct qa_q2_monster *monster = context->monster;
  bool handled = false;
  if (!q2m_species_melee(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!q2m_stalker_callback(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (strcmp(callback, "loogie") == 0) {
    if (!enemy_alive(context))
      return true;
    qa_body_state enemy;
    qa_actor_id enemy_id = monster->enemy;
    if (!qa_world_body_read(context->game->services.world, enemy_id, &enemy, error))
      return false;
    float height;
    if (!enemy_view_height(context, &height))
      return true;
    qa_vec3 up;
    qa_builtin_angle_vectors(context->body.angles, NULL, NULL, &up);
    qa_vec3 start = qa_vec_add(q2m_project_offset(context, qa_v3(-18, -.8f, 24)),
                                qa_vec_scale(up, 2));
    qa_vec3 eye = enemy.origin;
    eye.z += height;
    return !q2m_alive(context) || !enemy_alive(context) ||
           q2_fire_actor_loogie(context->game, context->actor->id, start,
                                 qa_vec_sub(eye, start), error);
  }
  if (!q2m_medic_callback(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!q2m_parasite_callback(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!q2m_summon_callback(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!conditional_transition(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!end_transition(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!soldier_callbacks(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!infantry_callbacks(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!carrier_spawn_callbacks(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!foundational_species_callback(context, callback, &handled, error))
    return false;
  if (handled)
    return true;
  if (!heavy_weapon_callback(context, callback, &handled, error))
    return false;
  if (handled)
    return true;

  if (!strcmp(callback, "WidowBlaster"))
    return widow_blaster(context, error);
  if (!strcmp(callback, "Widow2Beam"))
    return widow2_beam(context, error);
  if (!strcmp(callback, "Widow2Tongue"))
    return widow2_tongue(context, error);
  if (!strcmp(callback, "Widow2TonguePull"))
    return widow2_pull(context, error);
  if (!strcmp(callback, "Widow2Crunch")) {
    if (!widow2_pull(context, error))
      return false;
    if (!q2m_alive(context))
      return true;
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
      return false;
    if (!q2m_alive(context) || !available)
      return true;
    float kick = monster->frame != 53 ? 0.0f : enemy.ground.registry ? 500.0f : 250.0f;
    bool hit;
    return q2m_hit(context, qa_v3(150, 0, 4),
        20.0f + floorf(q2m_random(context->game) * 6.0f), kick, &hit, error);
  }
  if (!strcmp(callback, "widow_attack_kick")) {
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
      return false;
    if (!q2m_alive(context) || !available)
      return true;
    bool hit;
    return q2m_hit(context, qa_v3(100, 0, 4),
        50.0f + floorf(q2m_random(context->game) * 6.0f),
        enemy.ground.registry ? 500.0f : 250.0f, &hit, error);
  }
  if (!strcmp(callback, "Widow2SaveBeamTarget") || !strcmp(callback, "Widow2StartSweep"))
    return widow2_save_beam(context);
  if (!strcmp(callback, "Widow2BeamTargetRemove")) {
    monster->saved_attack_position = monster->widow_previous_target = qa_v3(0, 0, 0);
    return true;
  }
  if (!strcmp(callback, "WidowRail"))
    return widow_rail(context, error);
  if (!strcmp(callback, "WidowSaveLoc")) {
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
      return false;
    if (q2m_alive(context) && available)
      monster->saved_attack_position = qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height));
    return true;
  }
  if (has(callback, "duck_down") || has(callback, "duck_hold") ||
      has(callback, "duck_up"))
    return duck_action(context, callback, error);
  if ((has(callback, "jump") || has(callback, "check_landing")) &&
      (has(callback, "now") || has(callback, "takeoff") ||
       has(callback, "wait_land") || has(callback, "check_landing") ||
       has(callback, "jump_up") || has(callback, "jump_down")))
    return jump_action(context, callback, error);
  if (has(callback, "shrink") || strcmp(callback, "soldier_death_shrink") == 0)
    return shrink(context, error);
  if (has(callback, "footstep") || has(callback, "_step") ||
      has(callback, "TreadSound") || has(callback, "step_left") ||
      has(callback, "step_right"))
    return footstep(context, callback, error);

  if (strcmp(callback, "Widow2SaveDisruptLoc") == 0)
    return save_widow_disrupt_location(context);
  if (strcmp(callback, "WidowDisrupt") == 0)
    return q2m_widow_disrupt(context, error);
  if (has(callback, "SaveLoc") || has(callback, "Saveloc") ||
      has(callback, "save_loc"))
    return save_enemy_location(context);
  if (strcmp(callback, "TurretAim") == 0 || strcmp(callback, "gekk_face") == 0)
    return q2m_face_enemy(context, error);
  if (strcmp(callback, "monster_done_dodge") == 0) {
    monster->dodging = false;
    if (monster->attack_state == Q2M_SLIDING &&
        context->game->options.edition == QA_Q2_RERELEASE)
      monster->attack_state = Q2M_STRAIGHT;
    return true;
  }
  if (strcmp(callback, "monster_check_prone") == 0) {
    if (!enemy_alive(context))
      monster->next_frame = monster->frame + 1;
    return true;
  }
  if (strcmp(callback, "brain_chest_open") == 0) {
    context->combat.armor.powered.kind = QA_POWER_NONE;
    qa_q2_combat_power_armor_source(context->game, &context->combat.armor.powered);
    return qa_combat_set_armor(context->game->services.combat,
                               context->actor->id, &context->combat.armor,
                               error);
  }
  if (strcmp(callback, "brain_chest_closed") == 0) {
    context->combat.armor.powered.kind = QA_POWER_SCREEN;
    if (context->combat.armor.powered.cells <= 0.0f)
      context->combat.armor.powered.cells = 100.0f;
    qa_q2_combat_power_armor_source(context->game, &context->combat.armor.powered);
    return qa_combat_set_armor(context->game->services.combat,
                               context->actor->id, &context->combat.armor,
                               error);
  }
  if (strcmp(callback, "change_to_roam") == 0 ||
      strcmp(callback, "roam_goal") == 0) {
    monster->enemy = (qa_actor_id){0};
    monster->goal = monster->move_target;
    return set_definition_move(context, monster->definition->walk_move, error);
  }
  if (strcmp(callback, "fly_vertical") == 0 ||
      strcmp(callback, "fly_vertical2") == 0) {
    context->body.velocity.z +=
        strcmp(callback, "fly_vertical2") == 0 ? -80.0f : 80.0f;
    return q2m_write_body(context, true, error);
  }
  if (strcmp(callback, "berserk_high_gravity") == 0) {
    context->actor->physics.gravity_scale = 2.0f;
    return true;
  }
  if (strcmp(callback, "gekk_check_underwater") == 0) {
    const char *move = context->actor->physics.water_level > 1
                           ? "gekk_move_swim_start"
                           : monster->definition->run_move;
    bool found;
    return set_existing_move(context, move, false, &found, error);
  }
  if (strcmp(callback, "gekk_stop_skid") == 0) {
    context->body.velocity = qa_v3(0, 0, 0);
    return q2m_write_body(context, true, error);
  }
  if (strcmp(callback, "MakronToss") == 0)
    return toss_makron(context, error);
  if (strcmp(callback, "Widow2Toss") == 0) {
    context->body.velocity.z += 200.0f;
    return q2m_write_body(context, true, error);
  }
  if (strcmp(callback, "use_scanner") == 0) {
    return q2m_emit(context, QA_BUILTIN_LIGHT, "q2:scanner", 0,
                    context->body.origin, context->body.origin, 1.0f, error);
  }
  if (strcmp(callback, "weldstate") == 0) {
    ++monster->count;
    return true;
  }
  if (strcmp(callback, "CarrierCoopCheck") == 0)
    return carrier_coop_check(context, error);

  if (strcmp(callback, "gunner_blind_check") == 0 ||
      strcmp(callback, "soldier_blind_check") == 0 ||
      strcmp(callback, "tank_blind_check") == 0) {
    if (monster->attack_state == Q2M_BLIND) {
      qa_vec3 direction =
          qa_vec_sub(monster->blind_fire_target, context->body.origin);
      monster->ideal_yaw =
          atan2f(direction.y, direction.x) * 57.29577951308232f;
      return q2m_change_yaw(context, error);
    }
    return true;
  }

  bool widow_death_handled;
  if (!q2m_widow_death_action(context, callback, &widow_death_handled, error))
    return false;
  if (widow_death_handled)
    return true;

  if (strcmp(callback, "parasite_drain_attack") == 0 ||
      strcmp(callback, "parasite_launch") == 0)
    return q2m_melee(context, 256.0f,
                     strcmp(callback, "parasite_drain_attack") == 0 ? 5.0f
                                                                    : 2.0f,
                     0.0f, error);

  if (strcmp(callback, "soldier_walk1_random") == 0 ||
      strcmp(callback, "soldierh_walk1_random") == 0) {
    if (q2m_random(context->game) > 0.1f)
      monster->next_frame = monster->move->first_frame;
    return true;
  }

  if (strcmp(callback, "soldier_cock") == 0) {
    monster->cocked = true;
    return simple_sound(context, callback, error);
  }

  if (strcmp(callback, "soldierh_hyper_laser_sound_start") == 0)
    return soldier_laser_sound(context, true, error);
  if (strcmp(callback, "soldierh_hyper_laser_sound_end") == 0)
    return soldier_laser_sound(context, false, error);
  if (strcmp(callback, "soldierh_hyper_sound") == 0)
    return monster->definition->species == Q2M_SOLDIER_HYPER
               ? q2m_sound(context, "weapons/hyprbl1a.wav", 0, 1.0f, error)
               : true;

  if (strcmp(callback, "flyer_kamikaze_check") == 0) {
    if (!enemy_alive(context) || q2m_distance(context, monster->enemy) < 90.0f)
      return q2m_kamikaze(context, error);
    monster->goal = monster->enemy;
    return true;
  }

  if (strcmp(callback, "gladbGun_check") == 0)
    return context->game->options.skill == 3
               ? q2m_attack(context, monster->definition->primary,
                            monster->definition->primary_damage, error)
               : true;

  if (strcmp(callback, "mutant_check_refire") == 0) {
    if (enemy_alive(context) && ((context->game->options.skill == 3 &&
                                  q2m_random(context->game) < 0.5f) ||
                                 q2m_distance(context, monster->enemy) < 80.0f))
      monster->next_frame = 8;
    return true;
  }

  if (strcmp(callback, "gekk_check_refire") == 0) {
    if (!enemy_alive(context) ||
        q2m_random(context->game) >=
            (float)context->game->options.skill * 0.1f ||
        q2m_distance(context, monster->enemy) >= 80.0f)
      return true;
    const char *move =
        monster->frame == 53 ? "gekk_move_attack2" : "gekk_move_attack1";
    bool found;
    return set_existing_move(context, move, false, &found, error);
  }

  if (has(callback, "soldier_attack") && has(callback, "shotgun_check")) {
    if (monster->definition->species == Q2M_SOLDIER && !monster->cocked) {
      monster->next_frame = has(callback, "attack1")   ? 5
                            : has(callback, "attack2") ? 21
                                                       : 117;
      monster->force_refire = true;
    }
    return true;
  }

  if (strcmp(callback, "insane_checkdown") == 0 ||
      strcmp(callback, "insane_checkup") == 0)
    return true;

  if (strcmp(callback, "reloogie") == 0) {
    bool found;
    if (q2m_random(context->game) > 0.8f &&
        context->combat.health < monster->base_health)
      return set_existing_move(context, "gekk_move_idle2", false, &found,
                               error);
    float distance = q2m_distance(context, monster->enemy);
    if (enemy_alive(context) && q2m_random(context->game) > 0.7f &&
        distance >= 80.0f && distance < 500.0f)
      return set_existing_move(context, "gekk_move_spit", false, &found, error);
    return true;
  }

  if (strcmp(callback, "widow_step") == 0)
    return q2m_sound(context, "widow/bwstep3.wav", 4, 1.0f, error);
  if (strcmp(callback, "widow_stepshoot") == 0)
    return q2m_sound(context, "widow/bwstep3.wav", 4, 1.0f, error) &&
           (!q2m_alive(context) ||
            widow_blaster(context, error));

  if (strcmp(callback, "arachnid_charge_rail") == 0) {
    save_enemy_location(context);
    return q2m_sound(context, "gladiator/railgun.wav", 1, 1.0f, error);
  }

  if (strcmp(callback, "berserk_run_attack_speed") == 0) {
    if (enemy_alive(context) && q2m_distance(context, monster->enemy) < 80.0f) {
      monster->next_frame = monster->frame + 6;
      monster->dodging = false;
    }
    return true;
  }

  if (strcmp(callback, "arachnid_melee_charge") == 0)
    return q2m_sound(context, "gladiator/melee3.wav", 1, 1.0f, error);
  if (strcmp(callback, "flipper_preattack") == 0)
    return q2m_sound(context, "flipper/flpatck1.wav", 1, 1.0f, error);
  if (strcmp(callback, "gekk_preattack") == 0)
    return true;

  if (strcmp(callback, "mutant_idle_loop") == 0) {
    if (q2m_random(context->game) < 0.75f)
      monster->next_frame = 116;
    return true;
  }
  if (strcmp(callback, "gekk_idle_loop") == 0) {
    if (q2m_random(context->game) > 0.75f &&
        context->combat.health < monster->base_health)
      monster->next_frame = 203;
    return true;
  }
  if (strcmp(callback, "shambler_maybe_idle") == 0)
    return q2m_random(context->game) > 0.8f
               ? q2m_sound(context, "shambler/sidle.wav", 2, 2.0f, error)
               : true;

  if (strcmp(callback, "berserk_fidget") == 0) {
    if (monster->stand_ground || monster->enemy.registry != 0 ||
        q2m_random(context->game) > 0.15f)
      return true;
    return q2m_set_move(context, "berserk_move_stand_fidget", false, error) &&
           (!q2m_alive(context) ||
            q2m_sound(context, "berserk/beridle1.wav", 1, 2.0f, error));
  }
  if (strcmp(callback, "chick_fidget") == 0) {
    if (!monster->stand_ground && q2m_random(context->game) <= 0.3f)
      return q2m_set_move(context, "chick_move_fidget", false, error);
    return true;
  }
  if (strcmp(callback, "gunner_fidget") == 0 ||
      strcmp(callback, "guncmdr_fidget") == 0) {
    bool commander = callback[3] == 'c';
    if (!monster->stand_ground &&
        (!commander || monster->enemy.registry == 0) &&
        q2m_random(context->game) <= 0.05f)
      return q2m_set_move(
          context, commander ? "guncmdr_move_fidget" : "gunner_move_fidget",
          false, error);
    return true;
  }

  if (strcmp(callback, "guardian_atk1_charge") == 0) {
    if (!q2m_weapon_sound(context, "weapons/hyprbl1a.wav", error))
      return false;
    return !q2m_alive(context) ||
           q2m_sound(context, "weapons/hyprbu1a.wav", 1, 1.0f, error);
  }
  if (strcmp(callback, "guardian_fire_blaster") == 0) {
    qa_body_state enemy;
    if (!qa_world_body_read(context->game->services.world, monster->enemy, &enemy, error))
      return false;
    if (!q2m_alive(context)) return true;
    qa_vec3 start;
    float height;
    if (!q2m_project_flash(context, 227, &start, error)) return false;
    if (!enemy_view_height(context, &height)) return true;
    qa_vec3 target = enemy.origin;
    target.z += height;
    float low = nextafterf(-1.0f, 0.0f), high = nextafterf(1.0f, 0.0f);
    target.x += fminf(high, q2_rerelease_float(context->game, low, 1.0f)) * 5.0f;
    target.y += fminf(high, q2_rerelease_float(context->game, low, 1.0f)) * 5.0f;
    target.z += fminf(high, q2_rerelease_float(context->game, low, 1.0f)) * 5.0f;
    q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_BLASTER, 2.0f, 227,
        start, qa_vec_normalize(qa_vec_sub(target, start)));
    spec.speed = 1000.0f;
    spec.has_projectile_effects = true;
    spec.projectile_effects = monster->frame % 4 == 0 ? UINT64_C(16) : 0;
    if (!q2m_fire(context, &spec, error))
      return false;
    if (!q2m_alive(context) || monster->frame != 173 ||
        monster->timestamp_ns <= context->game->now_ns || !enemy_alive(context))
      return true;
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    if (visible)
      monster->next_frame = 166;
    return true;
  }
  if (strcmp(callback, "guardian_laser_fire") == 0)
    return q2m_sound(context, "weapons/laser2.wav", 1, 1.0f, error) &&
           (!q2m_alive(context) ||
            q2m_guardian_beam(context, error));

  if (has(callback, "refire") || has(callback, "reattack"))
    return reattack(context, callback, error);

  if (has(callback, "firetime") || has(callback, "fire_prep")) {
    monster->fire_ns =
        q2m_after(context->game->now_ns, 0.7 + q2m_random(context->game) * 1.3);
    monster->pause_ns = monster->fire_ns;
    return true;
  }
  if (strcmp(callback, "soldier_start_charge") == 0) {
    monster->charging = true;
    return true;
  }
  if (strcmp(callback, "GunnerCmdrCounter") == 0) {
    ++monster->count;
    return true;
  }
  if (has(callback, "pain5_to_death") || has(callback, "pain6_to_death")) {
    if (context->combat.health < 0.0f)
      return q2m_die(context, error);
    return true;
  }
  if (strcmp(callback, "flyer_nextmove") == 0) {
    bool found;
    return set_existing_move(context,
                             q2m_random(context->game) < 0.5f
                                 ? "flyer_move_rollleft"
                                 : "flyer_move_rollright",
                             false, &found, error);
  }
  if (strcmp(callback, "gekk_gibfest") == 0 ||
      strcmp(callback, "isgibfest") == 0)
    return context->combat.health <= monster->gib_health
               ? q2m_die(context, error)
               : true;
  if (strcmp(callback, "BossExplode") == 0 ||
      strcmp(callback, "BossExplode2") == 0)
    return q2m_start_boss_explosion(context, error);
  if (strcmp(callback, "hover_dying") == 0)
    return q2m_hover_dying(context, error);
  if (strcmp(callback, "jorg_death_hit") == 0) {
    ++monster->count;
    return q2m_emit(context, QA_BUILTIN_EXPLOSION, "q2:explosion1",
                    monster->count, context->body.origin, context->body.origin,
                    1.0f, error);
  }
  if (strcmp(callback, "BossLoop") == 0) {
    if ((monster->spawnflags & 16u) == 0)
      return true;
    if (monster->count != 0)
      --monster->count;
    else
      monster->spawnflags &= ~16u;
    monster->next_frame = monster->move->first_frame + 18;
    return true;
  }
  if (strcmp(callback, "widow2_start_searching") == 0) {
    monster->count = 0;
    return true;
  }
  if (strcmp(callback, "widow2_keep_searching") == 0) {
    if (monster->count > 2)
      return q2m_set_move(context, "widow2_move_really_dead", false, error);
    if (!q2m_set_move(context, "widow2_move_dead", false, error))
      return false;
    monster->frame = 104;
    ++monster->count;
    return true;
  }
  if (strcmp(callback, "widow2_finaldeath") == 0)
    return q2m_corpse(context, error);

  q2m_attack_kind kind = attack_kind(context, callback);
  if (kind != Q2M_ATTACK_NONE) {
    float damage = attack_damage(context, kind);
    if (strcmp(callback, "Boss2HyperBlaster") == 0)
      damage = 2.0f;
    if (strcmp(callback, "MakronHyperblaster") == 0)
      damage = 15.0f;
    if (strcmp(callback, "Boss2Rocket64") == 0)
      damage = 35.0f;
    return q2m_attack(context, kind, damage, error);
  }

  if (has(callback, "swing") || has(callback, "idle") ||
      has(callback, "fidget") || has(callback, "Moan") ||
      has(callback, "moan") || has(callback, "scream") ||
      has(callback, "shake") || has(callback, "cock") ||
      has(callback, "Reload") || has(callback, "opengun") ||
      has(callback, "tap") || has(callback, "scratch") ||
      has(callback, "break_") || has(callback, "preattack") ||
      has(callback, "PreAttack") || has(callback, "windup") ||
      has(callback, "taunt") || has(callback, "brainsplorch") ||
      has(callback, "popup") || has(callback, "prerailgun") ||
      has(callback, "pop_blades") || has(callback, "shambler_melee") ||
      has(callback, "thud") || has(callback, "charge"))
    return simple_sound(context, callback, error);

  if (strcmp(callback, "parasite_reel_in") == 0 ||
      strcmp(callback, "parasite_walk") == 0 ||
      strcmp(callback, "gekk_swim") == 0 ||
      strcmp(callback, "gekk_search") == 0 || strcmp(callback, "loogie") == 0)
    return true;

  qa_error_set(error, QA_ERROR_FORMAT, 0,
               "%s references unsupported monster callback %s",
               monster->definition->classname, callback);
  return false;
}
