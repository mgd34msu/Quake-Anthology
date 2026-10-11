#include "qa/q2_sound.h"
#include "internal.h"
#include "reinforcements.h"
#include "medic.h"
#include "qa/game_q2_entities.h"
#include "qa/game_q2_combat.h"
#include "../entities/internal.h"

typedef struct q2m_transition {
  q2m_move_id move;
} q2m_transition;

static const q2m_transition transitions[Q2M_CALLBACK_COUNT] = {
    [Q2M_CALLBACK_boss2_attack_mg]={ Q2M_MOVE_boss2_move_attack_mg},
    [Q2M_CALLBACK_carrier_attack_mg]={ Q2M_MOVE_carrier_move_attack_mg},
    [Q2M_CALLBACK_chick_attack1]={ Q2M_MOVE_chick_move_attack1},
    [Q2M_CALLBACK_chick_slash]={ Q2M_MOVE_chick_move_slash},
    [Q2M_CALLBACK_fixbot_attack]={ Q2M_MOVE_fixbot_move_attack1},
    [Q2M_CALLBACK_flipper_run_loop]={ Q2M_MOVE_flipper_move_run_loop},
    [Q2M_CALLBACK_flyer_check_melee]={ Q2M_MOVE_flyer_move_start_melee},
    [Q2M_CALLBACK_flyer_kamikaze]={ Q2M_MOVE_flyer_move_kamikaze},
    [Q2M_CALLBACK_flyer_loop_melee]={ Q2M_MOVE_flyer_move_loop_melee},
    [Q2M_CALLBACK_gekk_chant]={ Q2M_MOVE_gekk_move_chant},
    [Q2M_CALLBACK_gekk_face]={ Q2M_MOVE_gekk_move_attack},
    [Q2M_CALLBACK_gekk_run_start]={ Q2M_MOVE_gekk_move_run_start},
    [Q2M_CALLBACK_gekk_swim_loop]={ Q2M_MOVE_gekk_move_swim_loop},
    [Q2M_CALLBACK_guardian_atk1_finish]={ Q2M_MOVE_guardian_atk1_out},
    [Q2M_CALLBACK_guardian_atk2]={ Q2M_MOVE_guardian_move_atk2_fire},
    [Q2M_CALLBACK_guardian_atk2_out]={ Q2M_MOVE_guardian_move_atk2_out},
    [Q2M_CALLBACK_guncmdr_fire_chain]={ Q2M_MOVE_guncmdr_move_fire_chain},
    [Q2M_CALLBACK_guncmdr_grenade_back_dodge_resume]={ Q2M_MOVE_guncmdr_move_attack_grenade_back},
    [Q2M_CALLBACK_guncmdr_grenade_mortar_resume]={ Q2M_MOVE_guncmdr_move_attack_mortar},
    [Q2M_CALLBACK_gunner_fire_chain]={ Q2M_MOVE_gunner_move_fire_chain},
    [Q2M_CALLBACK_hover_attack]={ Q2M_MOVE_hover_move_attack1},
    [Q2M_CALLBACK_jorg_attack1]={ Q2M_MOVE_jorg_move_attack1},
    [Q2M_CALLBACK_parasite_do_fidget]={ Q2M_MOVE_parasite_move_fidget},
    [Q2M_CALLBACK_parasite_refidget]={ Q2M_MOVE_parasite_move_start_fidget},
    [Q2M_CALLBACK_parasite_start_run]={ Q2M_MOVE_parasite_move_run},
    [Q2M_CALLBACK_soldier_blind]={ Q2M_MOVE_soldier_move_blind},
    [Q2M_CALLBACK_stalker_false_death]={ Q2M_MOVE_stalker_move_false_death},
    [Q2M_CALLBACK_tank_doattack_rocket]={ Q2M_MOVE_tank_move_attack_fire_rocket},
    [Q2M_CALLBACK_tank_poststrike]={ Q2M_MOVE_tank_move_run},
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




static bool set_existing_move(q2m_context *context, q2m_move_id name,
                              bool immediate, bool *found, qa_error *error) {
  *found = q2m_move_find(context->monster, name) != NULL;
  return !*found || q2m_set_move(context, name, immediate, error);
}

static bool set_definition_move(q2m_context *context, q2m_move_id name,
                                qa_error *error) {
  return name == Q2M_MOVE_NONE || q2m_set_move(context, name, false, error);
}

static bool enemy_alive(q2m_context *context) {
  qa_combat_state combat;
  qa_error ignored = {0};
  return context->monster->enemy.registry != 0 &&
         qa_combat_read(context->game->services.combat, context->monster->enemy,
                        &combat, &ignored) &&
         combat.health > 0.0f;
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
  qa_actor_id child;
  if (!q2m_spawn_makron_entity(context, &child, error))
    return false;
  if (child.registry == 0)
    return true;

  qa_q2_monster_spawn_options options = {
      .classname = "monster_makron",
      .scale = context->game->options.edition == QA_Q2_RERELEASE ? 0 : 1,
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
    resumed.body.ground = (qa_actor_reference){0};
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
  if (!q2m_set_move(&resumed, Q2M_MOVE_makron_move_sight, true, error))
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

static bool duck_action(q2m_context *context, q2m_callback_id callback,
                        qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  bool shared = (callback == Q2M_CALLBACK_monster_duck_down) ||
      (callback == Q2M_CALLBACK_monster_duck_hold) || (callback == Q2M_CALLBACK_monster_duck_up);
  bool down = (callback == Q2M_CALLBACK_brain_duck_down) || (callback == Q2M_CALLBACK_chick_duck_down) ||
      (callback == Q2M_CALLBACK_medic_duck_down) || (callback == Q2M_CALLBACK_monster_duck_down);
  bool hold = (callback == Q2M_CALLBACK_brain_duck_hold) || (callback == Q2M_CALLBACK_chick_duck_hold) ||
      (callback == Q2M_CALLBACK_medic_duck_hold) || (callback == Q2M_CALLBACK_monster_duck_hold);
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (down) {
    if (!shared && m->ducked) return true;
    if (shared) {
      if (rerelease) m->next_duck_ns = q2m_after(context->game->now_ns, 5);
      else if (m->duck_ns < context->game->now_ns)
        m->duck_ns = q2m_after(context->game->now_ns, 1);
    } else if ((callback != (Q2M_CALLBACK_brain_duck_down)))
      m->pause_ns = q2m_after(context->game->now_ns, 1);
    return set_duck(context, true, error);
  }
  if (hold) {
    m->hold_frame = context->game->now_ns < (shared ? m->duck_ns : m->pause_ns);
    return true;
  }
  if (shared) {
    if (rerelease && !m->ducked) return true;
    if (!rerelease) m->next_duck_ns = q2m_after(context->game->now_ns, 5);
    else if (m->next_duck_ns > context->game->now_ns)
      m->next_duck_ns = context->game->now_ns + (m->next_duck_ns - context->game->now_ns) / 2;
  }
  return set_duck(context, false, error);
}

static bool jump_action(q2m_context *context, q2m_callback_id callback,
                        qa_error *error) {
  static const struct { q2m_callback_id name; float forward, up; } jumps[Q2M_CALLBACK_COUNT] = {
      [Q2M_CALLBACK_berserk_jump_now]={Q2M_CALLBACK_berserk_jump_now, 100, 300}, [Q2M_CALLBACK_berserk_jump2_now]={Q2M_CALLBACK_berserk_jump2_now, 150, 400},
      [Q2M_CALLBACK_guncmdr_jump_now]={Q2M_CALLBACK_guncmdr_jump_now, 100, 300}, [Q2M_CALLBACK_guncmdr_jump2_now]={Q2M_CALLBACK_guncmdr_jump2_now, 150, 400},
      [Q2M_CALLBACK_gunner_jump_now]={Q2M_CALLBACK_gunner_jump_now, 100, 300}, [Q2M_CALLBACK_gunner_jump2_now]={Q2M_CALLBACK_gunner_jump2_now, 150, 400},
      [Q2M_CALLBACK_infantry_jump_now]={Q2M_CALLBACK_infantry_jump_now, 100, 300}, [Q2M_CALLBACK_infantry_jump2_now]={Q2M_CALLBACK_infantry_jump2_now, 150, 400},
      [Q2M_CALLBACK_mutant_jump_down]={Q2M_CALLBACK_mutant_jump_down, 100, 100}, [Q2M_CALLBACK_mutant_jump_up]={Q2M_CALLBACK_mutant_jump_up, 200, 450},
      [Q2M_CALLBACK_parasite_jump_down]={Q2M_CALLBACK_parasite_jump_down, 100, 100}, [Q2M_CALLBACK_parasite_jump_up]={Q2M_CALLBACK_parasite_jump_up, 200, 450},
  };
  bool wait = callback == Q2M_CALLBACK_berserk_jump_wait_land ||
      callback == Q2M_CALLBACK_guncmdr_jump_wait_land || callback == Q2M_CALLBACK_gunner_jump_wait_land ||
      callback == Q2M_CALLBACK_infantry_jump_wait_land || callback == Q2M_CALLBACK_mutant_jump_wait_land ||
      callback == Q2M_CALLBACK_parasite_jump_wait_land;
  if (wait) {
    if (!qa_actor_reference_present(context->body.ground) &&
        context->game->options.edition == QA_Q2_RERELEASE) {
      qa_vec3 forward;
      qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
      qa_vec3 projected = qa_v3(context->body.velocity.x * forward.x,
                               context->body.velocity.y * forward.y,
                               context->body.velocity.z * forward.z);
      if (qa_vec_length(projected) < 150) {
        float z = context->body.velocity.z;
        context->body.velocity = qa_vec_scale(forward, 150);
        context->body.velocity.z = z;
        if (!q2m_write_body(context, false, error))
          return false;
        if (!q2m_alive(context))
          return true;
      }
    }
    if (qa_actor_reference_present(context->body.ground) ||
        context->game->now_ns > context->monster->jump_ns)
      context->monster->next_frame = context->monster->frame + 1;
    else
      context->monster->next_frame = context->monster->frame;
    return true;
  }
  qa_vec3 forward, up;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, &up);
  if (jumps[callback].name != callback) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Unknown authored Q2 jump callback %s", q2m_callbacks[callback].name);
    return false;
  }
  context->body.velocity = qa_vec_add(
      context->body.velocity, qa_vec_add(qa_vec_scale(forward, jumps[callback].forward),
                                         qa_vec_scale(up, jumps[callback].up)));
  context->body.ground = (qa_actor_reference){0};
  context->actor->physics.motion = QA_PHYSICS_STEP;
  return q2m_write_body(context, true, error);
}

static bool shrink(q2m_context *context, qa_error *error) {
  q2m_species species = context->monster->definition->species;
  float height = species == Q2M_BOSS2 ? 50 : (species == Q2M_CHICK || species == Q2M_CHICK_HEAT) ? 12
                 : species == Q2M_GUNNER ? -4
                 : species == Q2M_GUN_COMMANDER ? -4 * context->monster->entity_scale : 0;
  context->body.bounds.maxs.z = height;
  if (species != Q2M_BOSS2)
    context->actor->physics.flags |= QA_PHYSICS_DEAD;
  return q2m_write_body(context, true, error);
}

typedef struct source_sound {
  q2_runtime_name path;
  int channel;
  float attenuation, volume;
} source_sound;

static const source_sound source_sounds[Q2M_CALLBACK_COUNT] = {
    [Q2M_CALLBACK_TankStrike]={ Q2_NAME_RESOURCE_TANK_TNKATCK5_WAV, 1, 1, 1},
    [Q2M_CALLBACK_TreadSound2]={ Q2_NAME_RESOURCE_BOSSTANK_BTKENGN1_WAV, 2, 1, 1},
    [Q2M_CALLBACK_arachnid_footstep]={ Q2_NAME_RESOURCE_INSANE_INSANE11_WAV, 4, 2, .5f},
    [Q2M_CALLBACK_guardian_footstep]={ Q2_NAME_RESOURCE_ZORTEMP_STEP_WAV, 4, 1, 1},
    [Q2M_CALLBACK_guncmdr_idlesound]={ Q2_NAME_RESOURCE_GUNCMDR_GCDRIDLE1_WAV, 2, 2, 1},
    [Q2M_CALLBACK_guncmdr_opengun]={ Q2_NAME_RESOURCE_GUNCMDR_GCDRATCK1_WAV, 2, 2, 1},
    [Q2M_CALLBACK_insane_fist]={ Q2_NAME_RESOURCE_INSANE_INSANE11_WAV, 2, 2, 1},
    [Q2M_CALLBACK_jorg_idle]={ Q2_NAME_RESOURCE_BOSS3_BS3IDLE1_WAV, 2, 1, 1},
    [Q2M_CALLBACK_jorg_step_left]={ Q2_NAME_RESOURCE_BOSS3_STEP1_WAV, 4, 1, 1},
    [Q2M_CALLBACK_jorg_step_right]={ Q2_NAME_RESOURCE_BOSS3_STEP2_WAV, 4, 1, 1},
    [Q2M_CALLBACK_makron_hit]={ Q2_NAME_RESOURCE_MAKRON_BHIT_WAV, 0, 0, 1},
    [Q2M_CALLBACK_makron_popup]={ Q2_NAME_RESOURCE_MAKRON_POPUP_WAV, 4, 0, 1},
    [Q2M_CALLBACK_makron_step_left]={ Q2_NAME_RESOURCE_MAKRON_STEP1_WAV, 4, 1, 1},
    [Q2M_CALLBACK_makron_step_right]={ Q2_NAME_RESOURCE_MAKRON_STEP2_WAV, 4, 1, 1},
    [Q2M_CALLBACK_makron_brainsplorch]={ Q2_NAME_RESOURCE_MAKRON_BRAIN1_WAV, 2, 1, 1},
    [Q2M_CALLBACK_makron_prerailgun]={ Q2_NAME_RESOURCE_MAKRON_RAIL_UP_WAV, 1, 1, 1},
    [Q2M_CALLBACK_shambler_melee1]={ Q2_NAME_RESOURCE_SHAMBLER_MELEE1_WAV, 1, 1, 1},
    [Q2M_CALLBACK_shambler_melee2]={ Q2_NAME_RESOURCE_SHAMBLER_MELEE2_WAV, 1, 1, 1},
    [Q2M_CALLBACK_tank_footstep]={ Q2_NAME_RESOURCE_TANK_STEP_WAV, 4, 1, 1},
    [Q2M_CALLBACK_tank_thud]={ Q2_NAME_RESOURCE_TANK_TNKDETH2_WAV, 4, 1, 1},
    [Q2M_CALLBACK_tank_windup]={ Q2_NAME_RESOURCE_TANK_TNKATCK4_WAV, 1, 1, 1},
};

static bool source_sound_callback(q2m_context *context, q2m_callback_id callback,
                                   bool *handled, qa_error *error) {
  *handled = true;
  const source_sound *sound = source_sounds + callback;
  if (sound->path)
    return q2m_sound_volume(context, sound->path, sound->channel,
                           sound->attenuation, sound->volume, error);
  if (callback == Q2M_CALLBACK_monster_footstep)
    return !qa_actor_reference_present(context->body.ground) ||
           q2m_emit(context, QA_BUILTIN_Q2_ENTITY_EVENT, Q2_NAME_NONE, 8,
                     context->body.origin, context->body.origin, 0, error);
  if (callback == Q2M_CALLBACK_makron_taunt) {
    float choice = q2m_random(context->game);
    q2_runtime_name path = choice <= .3f ? Q2_NAME_RESOURCE_MAKRON_VOICE4_WAV
                       : choice <= .6f ? Q2_NAME_RESOURCE_MAKRON_VOICE3_WAV
                                         : Q2_NAME_RESOURCE_MAKRON_VOICE_WAV;
    return q2m_sound(context, path, 0, 0, error);
  }
  if ((callback == Q2M_CALLBACK_insane_shake) ||
      (callback == Q2M_CALLBACK_insane_moan) ||
      (callback == Q2M_CALLBACK_insane_scream)) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool shake = (callback == Q2M_CALLBACK_insane_shake);
    if (rerelease && ((context->monster->spawnflags & 64u) ||
        (!shake && context->monster->attack_ns >= context->game->now_ns)))
      return true;
    static const q2_runtime_name screams[] = {
        Q2_NAME_RESOURCE_INSANE_INSANE1_WAV, Q2_NAME_RESOURCE_INSANE_INSANE2_WAV, Q2_NAME_RESOURCE_INSANE_INSANE3_WAV,
        Q2_NAME_RESOURCE_INSANE_INSANE4_WAV, Q2_NAME_RESOURCE_INSANE_INSANE6_WAV, Q2_NAME_RESOURCE_INSANE_INSANE8_WAV,
        Q2_NAME_RESOURCE_INSANE_INSANE9_WAV, Q2_NAME_RESOURCE_INSANE_INSANE10_WAV};
    q2_runtime_name path = shake ? Q2_NAME_RESOURCE_INSANE_INSANE5_WAV
                       : (callback == Q2M_CALLBACK_insane_moan)
                           ? Q2_NAME_RESOURCE_INSANE_INSANE7_WAV
                           : screams[q2_random_bounded(context->game, 8)];
    if (!q2m_sound(context, path, 2, 2, error))
      return false;
    if (rerelease && !shake && q2m_alive(context))
      context->monster->attack_ns = q2m_after(context->game->now_ns,
          q2_rerelease_float(context->game, 1, 3));
    return true;
  }
  *handled = false;
  return true;
}

bool q2m_weapon_sound(q2m_context *context, q2_runtime_name path, qa_error *error) {
  qa_string_id resource = context->monster->weapon_sound;
  if (path) resource=context->game->runtime_names[path];
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
      !q2m_sound(context, Q2_NAME_RESOURCE_BOSS3_BS3ATCK1_END_WAV, 1, 1, error)) return false;
  return !q2m_alive(context) || q2m_weapon_sound(context, Q2_NAME_NONE, error);
}

bool q2m_soldier_sound_end(q2m_context *context, qa_error *error) {
  if (context->game->options.edition != QA_Q2_RERELEASE ||
      !context->monster->weapon_sound)
    return true;
  if (context->monster->count >= 2 && context->monster->count < 4 &&
      !q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBD1A_WAV, 0, 1.0f, error))
    return false;
  return !q2m_alive(context) || q2m_weapon_sound(context, Q2_NAME_NONE, error);
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
  return q2m_weapon_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBL1A_WAV, error);
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
  float scale = context->game->options.edition == QA_Q2_RERELEASE &&
                        context->monster->entity_scale != 0
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
        .policy = qa_collision_default_policy(QA_GAME_Q2),
    };
    query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x06000003), QA_GAME_Q2);
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
    if (context->game->options.edition == QA_Q2_CLASSIC &&
        q2m_random(context->game) > 0.8f &&
        !q2m_sound(context, Q2_NAME_RESOURCE_MISC_LASFLY_WAV, 0, 3.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!q2m_soldier_laser_beam(context, flash, error))
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
    q2m_move_id move = context->monster->stand_ground ? Q2M_MOVE_soldier_move_stand1
        : context->monster->move &&
          ((context->monster->move->id == Q2M_MOVE_soldier_move_walk1) ||
           (context->monster->move->id == Q2M_MOVE_soldier_move_walk2) ||
           (context->monster->move->id == Q2M_MOVE_soldier_move_start_run) ||
           (context->monster->move->id == Q2M_MOVE_soldier_move_run))
            ? Q2M_MOVE_soldier_move_run : Q2M_MOVE_soldier_move_start_run;
    return q2m_set_move(context, move, true, error);
  }
  context->monster->charging = false;
  context->monster->hold_frame = false;
  q2m_move_id move = context->monster->stand_ground
                         ? context->monster->definition->stand_move
                         : context->monster->definition->run_move;
  return set_definition_move(context, move, error);
}

static bool soldier_callbacks(q2m_context *context, q2m_callback_id callback,
                              bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool base = (q2m_callbacks[callback].flags & Q2M_CALLBACK_BASE_SOLDIER) != 0;
  bool heavy = (q2m_callbacks[callback].flags & Q2M_CALLBACK_HEAVY_SOLDIER) != 0;
  *handled = base || heavy;
  if (!*handled)
    return true;

  if ((callback == Q2M_CALLBACK_soldier_attack1_shotgun_check) ||
      (callback == Q2M_CALLBACK_soldier_attack2_shotgun_check) ||
      (callback == Q2M_CALLBACK_soldier_attack6_shotgun_check)) {
    if (!soldier_shotgun(monster) || monster->cocked)
      return true;
    monster->next_frame = (q2m_callbacks[callback].flags & Q2M_CALLBACK_ATTACK1) != 0   ? 5
                          : (q2m_callbacks[callback].flags & Q2M_CALLBACK_ATTACK2) != 0 ? 21
                                                                : 117;
    monster->force_refire = true;
    return true;
  }

  if (callback == Q2M_CALLBACK_soldier_attack1_refire1) {
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
  if ((callback == Q2M_CALLBACK_soldier_attack1_refire2) ||
      (callback == Q2M_CALLBACK_soldier_attack2_refire2)) {
    if (soldier_blaster(monster) || !enemy_alive(context))
      return true;
    bool refire;
    if (!soldier_refire(context, monster->force_refire, &refire, error))
      return false;
    if (refire) {
      monster->next_frame = callback == Q2M_CALLBACK_soldier_attack1_refire2 ? 1 : 15;
      monster->force_refire = false;
    }
    return true;
  }
  if (callback == Q2M_CALLBACK_soldier_attack2_refire1) {
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
  if (callback == Q2M_CALLBACK_soldier_attack3_refire) {
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
  if (callback == Q2M_CALLBACK_soldier_attack6_refire) {
    if (enemy_alive(context) &&
        q2m_distance(context, monster->enemy) >=
            (context->game->options.product == QA_Q2_ROGUE ? 80.0f : 500.0f) &&
        (context->game->options.skill == 3 ||
         (context->game->options.product == QA_Q2_ROGUE &&
          q2m_random(context->game) < 0.25f * (float)context->game->options.skill)))
      monster->next_frame = 111;
    monster->dodging = false;
    monster->charging = false;
    return true;
  }
  if ((callback == Q2M_CALLBACK_soldier_attack6_refire1) ||
      (callback == Q2M_CALLBACK_soldier_attack6_refire2)) {
    bool first = (q2m_callbacks[callback].flags & Q2M_CALLBACK_REFIRE1) != 0;
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

  if ((callback == Q2M_CALLBACK_soldierh_attack1_refire1) ||
      (callback == Q2M_CALLBACK_soldierh_attack1_refire2) ||
      (callback == Q2M_CALLBACK_soldierh_attack2_refire1) ||
      (callback == Q2M_CALLBACK_soldierh_attack2_refire2)) {
    if (!enemy_alive(context))
      return true;
    bool attack1 = (q2m_callbacks[callback].flags & Q2M_CALLBACK_ATTACK1) != 0;
    bool first = (q2m_callbacks[callback].flags & Q2M_CALLBACK_REFIRE1) != 0;
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
  if (callback == Q2M_CALLBACK_soldierh_attack3_refire) {
    if (q2m_after(context->game->now_ns, 0.4) < monster->pause_ns)
      monster->next_frame = 32;
    return true;
  }
  if (callback == Q2M_CALLBACK_soldierh_attack6_refire) {
    if (enemy_alive(context) &&
        q2m_distance(context, monster->enemy) >= 500.0f &&
        context->game->options.skill == 3)
      monster->next_frame = 111;
    return true;
  }
  if ((callback == Q2M_CALLBACK_soldierh_hyper_refire1) ||
      (callback == Q2M_CALLBACK_soldierh_hyper_refire2)) {
    if (!soldierh_hyper(monster))
      return true;
    if (q2m_random(context->game) < 0.7f) {
      bool visible;
      if (!visible_enemy(context, &visible, error))
        return false;
      if (visible)
        monster->frame = (q2m_callbacks[callback].flags & Q2M_CALLBACK_REFIRE1) != 0 ? 2 : 16;
    } else {
      return q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBD1A_WAV, 0, 1.0f, error);
    }
    return true;
  }
  if (callback == Q2M_CALLBACK_soldierh_hyper_sound)
    return soldierh_hyper(monster)
               ? q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBL1A_WAV, 0, 1.0f, error)
               : true;

  if ((callback == Q2M_CALLBACK_soldier_idle) ||
      (callback == Q2M_CALLBACK_soldierh_idle))
    return q2m_random(context->game) > 0.8f
               ? q2m_sound(context, Q2_NAME_RESOURCE_SOLDIER_SOLIDLE1_WAV, 2, 2.0f, error)
               : true;
  if ((callback == Q2M_CALLBACK_soldier_cock) ||
      (callback == Q2M_CALLBACK_soldierh_cock)) {
    if (base)
      monster->cocked = true;
    return q2m_sound(context, Q2_NAME_RESOURCE_INFANTRY_INFATCK3_WAV, 1, 1.0f, error);
  }
  if ((callback == Q2M_CALLBACK_soldier_walk1_random) ||
      (callback == Q2M_CALLBACK_soldierh_walk1_random)) {
    if (q2m_random(context->game) > 0.1f)
      monster->next_frame = monster->move->first_frame;
    return true;
  }
  if ((callback == Q2M_CALLBACK_soldier_duck_down) ||
      (callback == Q2M_CALLBACK_soldierh_duck_down)) {
    if (monster->ducked)
      return true;
    monster->pause_ns = q2m_after(context->game->now_ns, 1.0);
    return set_duck(context, true, error);
  }
  if ((callback == Q2M_CALLBACK_soldier_duck_hold) ||
      (callback == Q2M_CALLBACK_soldierh_duck_hold)) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  if ((callback == Q2M_CALLBACK_soldier_duck_up) ||
      (callback == Q2M_CALLBACK_soldierh_duck_up))
    return set_duck(context, false, error);
  if (callback == Q2M_CALLBACK_soldier_start_charge) {
    monster->charging = true;
    return true;
  }
  if (callback == Q2M_CALLBACK_soldier_stop_charge) {
    monster->charging = false;
    return true;
  }
  if (callback == Q2M_CALLBACK_soldier_blind_check) {
    if (monster->manual_steering) {
      qa_vec3 direction =
          qa_vec_sub(monster->blind_fire_target, context->body.origin);
      monster->ideal_yaw =
          atan2f(direction.y, direction.x) * 57.29577951308232f;
    }
    return true;
  }
  if (callback == Q2M_CALLBACK_soldier_death_shrink)
    return shrink(context, error);

  static const struct { q2m_callback_id callback; unsigned index; bool limited, extra; } shots[Q2M_CALLBACK_COUNT] = {
      [Q2M_CALLBACK_soldier_fire1]={Q2M_CALLBACK_soldier_fire1,0,false,false}, [Q2M_CALLBACK_soldier_fire2]={Q2M_CALLBACK_soldier_fire2,1,false,false},
      [Q2M_CALLBACK_soldier_fire3]={Q2M_CALLBACK_soldier_fire3,2,false,false}, [Q2M_CALLBACK_soldier_fire4]={Q2M_CALLBACK_soldier_fire4,3,false,false},
      [Q2M_CALLBACK_soldier_fire5]={Q2M_CALLBACK_soldier_fire5,8,true,false}, [Q2M_CALLBACK_soldier_fire6]={Q2M_CALLBACK_soldier_fire6,5,false,false},
      [Q2M_CALLBACK_soldier_fire7]={Q2M_CALLBACK_soldier_fire7,6,false,false}, [Q2M_CALLBACK_soldier_fire8]={Q2M_CALLBACK_soldier_fire8,7,true,false},
      [Q2M_CALLBACK_soldierh_fire1]={Q2M_CALLBACK_soldierh_fire1,0,false,false}, [Q2M_CALLBACK_soldierh_fire2]={Q2M_CALLBACK_soldierh_fire2,1,false,false},
      [Q2M_CALLBACK_soldierh_fire3]={Q2M_CALLBACK_soldierh_fire3,2,false,false}, [Q2M_CALLBACK_soldierh_fire4]={Q2M_CALLBACK_soldierh_fire4,3,false,false},
      [Q2M_CALLBACK_soldierh_fire6]={Q2M_CALLBACK_soldierh_fire6,5,false,false}, [Q2M_CALLBACK_soldierh_fire7]={Q2M_CALLBACK_soldierh_fire7,6,false,false},
      [Q2M_CALLBACK_soldierh_fire8]={Q2M_CALLBACK_soldierh_fire8,7,false,false},
      [Q2M_CALLBACK_soldierh_ripper1]={Q2M_CALLBACK_soldierh_ripper1,0,false,true}, [Q2M_CALLBACK_soldierh_ripper2]={Q2M_CALLBACK_soldierh_ripper2,1,false,true},
      [Q2M_CALLBACK_soldierh_hyperripper1]={Q2M_CALLBACK_soldierh_hyperripper1,0,false,true}, [Q2M_CALLBACK_soldierh_hyperripper2]={Q2M_CALLBACK_soldierh_hyperripper2,1,false,true},
      [Q2M_CALLBACK_soldierh_hyperripper3]={Q2M_CALLBACK_soldierh_hyperripper3,2,false,true}, [Q2M_CALLBACK_soldierh_hyperripper5]={Q2M_CALLBACK_soldierh_hyperripper5,8,true,true},
      [Q2M_CALLBACK_soldierh_hyperripper8]={Q2M_CALLBACK_soldierh_hyperripper8,7,true,true},
  };
  if (shots[callback].callback == callback) {
    if (shots[callback].extra) {
      if (context->game->options.edition == QA_Q2_RERELEASE) {
        if (monster->count >= 4 ||
            ((shots[callback].index == 2 || shots[callback].index == 7) && monster->skin < 6) ||
            (shots[callback].index == 8 && !monster->style)) return true;
      } else if (soldierh_laser(monster)) return true;
    }
    if (base && shots[callback].index == 2 && context->game->options.edition == QA_Q2_CLASSIC) {
      monster->pause_ns = q2m_after(context->game->now_ns, 1);
      if (!set_duck(context, true, error)) return false;
    }
    if (!soldier_fire_exact(context, shots[callback].index, shots[callback].limited, error)) return false;
    if (q2m_alive(context) && (callback == Q2M_CALLBACK_soldier_fire6) &&
        context->game->options.edition == QA_Q2_RERELEASE &&
        soldier_shotgun(monster) && !monster->cocked)
      monster->next_frame = 297;
    return true;

  }

  if (callback == Q2M_CALLBACK_soldierh_hyper_laser_sound_start)
    return soldier_laser_sound(context, true, error);
  if (callback == Q2M_CALLBACK_soldierh_hyper_laser_sound_end)
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
          .policy = qa_collision_default_policy(QA_GAME_Q2),
      };
      eye_query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x46004003), QA_GAME_Q2);
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
          .policy = qa_collision_default_policy(QA_GAME_Q2),
      };
      lead_query.policy.contents_mask = qa_collision_contents_mask(3u, QA_GAME_Q2);
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
  if ((monster->move->id == Q2M_MOVE_infantry_move_attack4)) {
    if (context->game->now_ns >= monster->fire_ns) {
      finish_dodge_action(monster);
      if (!q2m_set_move(context, Q2M_MOVE_infantry_move_attack1, false, error))
        return false;
      monster->next_frame = 197;
    } else {
      bool moved;
      if (!can_walk_forward(context, 8.0f, &moved, error))
        return false;
      if (!moved) {
        if (!q2m_set_move(context, Q2M_MOVE_infantry_move_attack1, false, error))
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

static bool infantry_callbacks(q2m_context *context, q2m_callback_id callback,
                               bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = (q2m_callbacks[callback].flags & Q2M_CALLBACK_INFANTRY) != 0 ||
             (callback == Q2M_CALLBACK_InfantryMachineGun);
  if (!*handled)
    return true;

  if (callback == Q2M_CALLBACK_InfantryMachineGun)
    return infantry_machinegun(context, error);
  if (callback == Q2M_CALLBACK_infantry_fire)
    return infantry_fire(context, error);
  if (callback == Q2M_CALLBACK_infantry_cock_gun) {
    if (context->game->options.edition == QA_Q2_CLASSIC) {
      unsigned tenths = 10u + (unsigned)(q2m_random(context->game) * 16.0f);
      monster->pause_ns =
          q2m_after(context->game->now_ns, (double)tenths * 0.1);
    } else {
      monster->cocked = true;
    }
    return q2m_sound(context, Q2_NAME_RESOURCE_INFANTRY_INFATCK3_WAV, 1, 1.0f, error);
  }
  if (callback == Q2M_CALLBACK_infantry_set_firetime) {
    monster->fire_ns =
        q2m_after(context->game->now_ns, 0.7 + q2m_random(context->game) * 1.3);
    if (!monster->stand_ground && monster->enemy.registry != 0 &&
        q2m_distance(context, monster->enemy) >= 330.0f) {
      bool moved;
      if (!can_walk_forward(context, 8.0f, &moved, error))
        return false;
      if (moved)
        return q2m_set_move(context, Q2M_MOVE_infantry_move_attack4, false, error);
    }
    return true;
  }
  if (callback == Q2M_CALLBACK_infantry_attack4_refire) {
    if (context->game->now_ns >= monster->fire_ns) {
      finish_dodge_action(monster);
      if (!q2m_set_move(context, Q2M_MOVE_infantry_move_attack1, false, error))
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
        if (!q2m_set_move(context, Q2M_MOVE_infantry_move_attack1, false, error))
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
  if (callback == Q2M_CALLBACK_infantry_swing)
    return q2m_sound(context, Q2_NAME_RESOURCE_INFANTRY_INFATCK2_WAV, 1, 1.0f, error);
  if (callback == Q2M_CALLBACK_infantry_duck_down) {
    if (monster->ducked)
      return true;
    monster->pause_ns = q2m_after(context->game->now_ns, 1.0);
    return set_duck(context, true, error);
  }
  if (callback == Q2M_CALLBACK_infantry_duck_hold) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  if (callback == Q2M_CALLBACK_infantry_duck_up)
    return set_duck(context, false, error);
  if (callback == Q2M_CALLBACK_infantry_shrink)
    return shrink(context, error);
  if ((callback == Q2M_CALLBACK_infantry_jump_now) ||
      (callback == Q2M_CALLBACK_infantry_jump2_now) ||
      (callback == Q2M_CALLBACK_infantry_jump_wait_land))
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
  qa_builtin_snapshot_frame *players = q2_player_roster(context->game, error);
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
        .policy = qa_collision_default_policy(QA_GAME_Q2),
    };
    query.policy.contents_mask = qa_collision_contents_mask(3u, QA_GAME_Q2);
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
  qa_builtin_snapshot_release(players);
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
  if (!q2m_sound(context, Q2_NAME_RESOURCE_MEDIC_COMMANDER_MONSTERSPAWN1_WAV, 4, 0.0f, error))
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
      return q2m_set_move(&spawned, Q2M_MOVE_flyer_move_attack3, false, error);
    }
    if (phase == 2) {
      spawned.monster->lefty = false;
      spawned.monster->attack_state = Q2M_STRAIGHT;
      if (!q2m_set_move(&spawned, Q2M_MOVE_flyer_move_kamikaze, false, error))
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

static bool carrier_spawn_callbacks(q2m_context *context, q2m_callback_id callback,
                                    bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = true;
  if (callback == Q2M_CALLBACK_carrier_prep_spawn) {
    monster->manual_steering = true;
    monster->timestamp_ns = context->game->now_ns;
    monster->yaw_speed = 10.0f;
    return carrier_machineguns(context, error);
  }
  if (callback == Q2M_CALLBACK_carrier_start_spawn) {
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
  if (callback == Q2M_CALLBACK_carrier_ready_spawn) {
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
  if (callback == Q2M_CALLBACK_carrier_spawn_check) {
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

static bool stop_loop_sound(q2m_context *context, q2_runtime_name path,
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
  event.resource=context->game->runtime_names[path];
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
    q2m_move_id move = angle >= 105.0f ? Q2M_MOVE_widow_move_attack_post_blaster_r
                                       : Q2M_MOVE_widow_move_attack_post_blaster_l;
    if (!q2m_set_move(context, move, false, error))
      return -1;
    context->monster->manual_steering = false;
    *turned = true;
    return 0;
  }
  for (int index = 0; index < 17; ++index)
    if (angle >= 95.0f - (float)index * 10.0f)
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
    float aim = 100.0f - 10.0f * (float)(flash - 165);
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
  q2m_move_id move = context->monster->move->id;
  int flash = (move == Q2M_MOVE_widow_move_attack_rail_l) ? 154
              : (move == Q2M_MOVE_widow_move_attack_rail_r) ? 155 : 150;
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
    angles.y -= -40.0f + (float)index * 8.0f;
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
      .policy = qa_collision_default_policy(QA_GAME_Q2)};
  query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x06000003), QA_GAME_Q2);
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id) ||
      trace.hit != QA_TRACE_HIT_ACTOR || !qa_actor_id_equal(trace.actor, enemy_id))
    return true;
  if (!q2m_sound(context, Q2_NAME_RESOURCE_BRAIN_BRNATCK3_WAV, 1, 1.0f, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id))
    return true;
  if (!q2m_emit(context, QA_BUILTIN_BEAM, Q2_NAME_RESOURCE_Q2_PARASITE, 0,
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
        ? Q2M_MOVE_widow2_move_stand : Q2M_MOVE_widow2_move_run, false, error);
  qa_vec3 start;
  if (!widow2_tongue_point(context, &start) || !widow2_tongue_reaches(start, enemy.origin))
    return true;
  if (qa_actor_reference_present(enemy.ground))
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
  enemy.ground = (qa_actor_reference){0};
  if (!qa_world_body_write(context->game->services.world, enemy_id, &enemy, error))
    return false;
  if (!q2m_alive(context) || !q2_actor_live(context->game, enemy_id))
    return true;
  return !context->game->services.motion_changed ||
      context->game->services.motion_changed(context->game->services.context, enemy_id,
          &(qa_builtin_motion_change){.reason = QA_BUILTIN_MOTION_LAUNCH,
                                      .body = enemy}, error);
}

static bool conditional_transition(q2m_context *context, q2m_callback_id callback,
                                   bool *handled, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = true;

  if (callback == Q2M_CALLBACK_carrier_attack_gren) {
    monster->timestamp_ns = context->game->now_ns;
    return q2m_set_move(context, Q2M_MOVE_carrier_move_attack_gren, false, error);
  }

  if (callback == Q2M_CALLBACK_guardian_atk1) {
    monster->timestamp_ns = q2_deadline(context->game->now_ns,
        (UINT64_C(650) + (uint64_t)q2_rerelease_time_ms(context->game, 0, 1500)) * Q2_MS);
    return q2m_set_move(context, Q2M_MOVE_guardian_move_atk1_spin, false, error);
  }

  if (callback == Q2M_CALLBACK_guardian_atk1_finish) {
    if (!q2m_set_move(context, Q2M_MOVE_guardian_atk1_out, true, error))
      return false;
    monster->weapon_sound = 0;
    if (!stop_loop_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBL1A_WAV, error))
      return false;
    return true;
  }

  if ((callback == Q2M_CALLBACK_widow_start_rail) ||
      (callback == Q2M_CALLBACK_widow_done_spawn) ||
      (callback == Q2M_CALLBACK_widow_rail_done)) {
    monster->manual_steering = (callback == Q2M_CALLBACK_widow_start_rail);
    return true;
  }

  if ((callback == Q2M_CALLBACK_widow_start_run_5) ||
      (callback == Q2M_CALLBACK_widow_start_run_10) ||
      (callback == Q2M_CALLBACK_widow_start_run_12)) {
    int frame = (callback == Q2M_CALLBACK_widow_start_run_5)    ? 15
                : (callback == Q2M_CALLBACK_widow_start_run_10) ? 20
                                                              : 22;
    if (!q2m_set_move(context, Q2M_MOVE_widow_move_run, false, error))
      return false;
    monster->next_frame = frame;
    return true;
  }

  if (callback == Q2M_CALLBACK_widow_attack_blaster) {
    monster->pause_ns =
        q2m_after(context->game->now_ns, 1.0 + 2.0 * q2m_random(context->game));
    if (!q2m_set_move(context, Q2M_MOVE_widow_move_attack_blaster, false, error))
      return false;
    bool turned;
    int frame = widow_torso_frame(context, &turned, error);
    if (frame < 0)
      return false;
    if (!turned)
      monster->next_frame = frame;
    return true;
  }

  if (callback == Q2M_CALLBACK_brain_laserbeam_reattack) {
    if (q2m_random(context->game) < 0.5f) {
      bool visible;
      if (!q2m_visible(context, monster->enemy, &visible, error))
        return false;
      if (visible && enemy_alive(context))
        monster->frame = monster->move->first_frame;
    }
    return true;
  }

  if (callback == Q2M_CALLBACK_chick_rerocket) {
    if (monster->manual_steering) {
      monster->manual_steering = false;
      return q2m_set_move(context, Q2M_MOVE_chick_move_end_attack1, false, error);
    }
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    float chance = context->game->options.edition == QA_Q2_RERELEASE ? 0.7f
                   : context->game->options.product == QA_Q2_ROGUE
                       ? 0.6f + 0.05f * (float)context->game->options.skill
                       : 0.6f;
    bool repeat = visible && q2m_distance(context, monster->enemy) >= 80.0f &&
                  q2m_random(context->game) <= chance;
    return q2m_set_move(
        context, repeat ? Q2M_MOVE_chick_move_attack1 : Q2M_MOVE_chick_move_end_attack1,
        false, error);
  }

  if (callback == Q2M_CALLBACK_chick_reslash) {
    bool repeat = enemy_alive(context) &&
                  q2m_distance(context, monster->enemy) < 80.0f &&
                  q2m_random(context->game) <= 0.9f;
    return q2m_set_move(context,
                        repeat ? Q2M_MOVE_chick_move_slash : Q2M_MOVE_chick_move_end_slash,
                        false, error);
  }

  if ((callback == Q2M_CALLBACK_gunner_refire_chain) ||
      (callback == Q2M_CALLBACK_guncmdr_refire_chain)) {
    bool commander = (callback == Q2M_CALLBACK_guncmdr_refire_chain);
    if (commander) {
      monster->dodging = false;
      monster->attack_state = Q2M_STRAIGHT;
    }
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    bool repeat = visible && q2m_random(context->game) <= 0.5f;
    q2m_move_id move;
    if (!repeat) {
      move = commander ? Q2M_MOVE_guncmdr_move_endfire_chain
                       : Q2M_MOVE_gunner_move_endfire_chain;
    } else if (commander && !monster->stand_ground &&
               q2m_distance(context, monster->enemy) > 400.0f) {
      move = Q2M_MOVE_guncmdr_move_fire_chain_run;
    } else {
      move = commander ? Q2M_MOVE_guncmdr_move_fire_chain : Q2M_MOVE_gunner_move_fire_chain;
    }
    return q2m_set_move(context, move, false, error);
  }

  if (callback == Q2M_CALLBACK_hover_reattack) {
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    q2m_move_id move = Q2M_MOVE_hover_move_end_attack;
    if (visible && q2m_random(context->game) <= 0.6f) {
      if (monster->attack_state == Q2M_SLIDING &&
          q2m_move_find(monster, Q2M_MOVE_hover_move_attack2) != NULL)
        move = Q2M_MOVE_hover_move_attack2;
      else if (monster->attack_state == Q2M_STRAIGHT)
        move = Q2M_MOVE_hover_move_attack1;
    }
    return q2m_set_move(context, move, false, error);
  }

  if (callback == Q2M_CALLBACK_jorg_reattack1) {
    bool visible;
    if (!q2m_visible(context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context)) return true;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    if (visible && (rerelease ? q2_rerelease_float(context->game, 0, 1) :
                              q2m_random(context->game)) < 0.9f)
      return q2m_set_move(context, Q2M_MOVE_jorg_move_attack1, rerelease, error);
    if (rerelease && !q2m_set_move(context, Q2M_MOVE_jorg_move_end_attack1, true, error))
      return false;
    if (!q2m_jorg_sound_end(context, error))
      return false;
    return !q2m_alive(context) || rerelease ||
           q2m_set_move(context, Q2M_MOVE_jorg_move_end_attack1, false, error);
  }

  if ((callback == Q2M_CALLBACK_boss5_reattack1) ||
      (callback == Q2M_CALLBACK_supertank_reattack1)) {
    bool supertank = callback == Q2M_CALLBACK_supertank_reattack1;
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
    q2m_move_id move = supertank ? repeat ? Q2M_MOVE_supertank_move_attack1
                                          : Q2M_MOVE_supertank_move_end_attack1
                       : repeat  ? Q2M_MOVE_boss5_move_attack1
                                 : Q2M_MOVE_boss5_move_end_attack1;
    return q2m_set_move(context, move, false, error);
  }

  if (callback == Q2M_CALLBACK_boss2_reattack_mg) {
    bool repeat = monster->enemy.registry != 0 && enemy_in_front(context) &&
                  q2m_random(context->game) <= 0.7f;
    return q2m_set_move(
        context, repeat ? Q2M_MOVE_boss2_move_attack_mg : Q2M_MOVE_boss2_move_attack_post_mg,
        false, error);
  }

  if ((callback == Q2M_CALLBACK_tank_reattack_blaster) ||
      (callback == Q2M_CALLBACK_tank_refire_rocket)) {
    bool rocket = (callback == Q2M_CALLBACK_tank_refire_rocket);
    if (monster->manual_steering) {
      monster->manual_steering = false;
      return q2m_set_move(context,
                          rocket ? Q2M_MOVE_tank_move_attack_post_rocket
                                 : Q2M_MOVE_tank_move_attack_post_blast,
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
                        rocket   ? repeat ? Q2M_MOVE_tank_move_attack_fire_rocket
                                          : Q2M_MOVE_tank_move_attack_post_rocket
                        : repeat ? Q2M_MOVE_tank_move_reattack_blast
                                 : Q2M_MOVE_tank_move_attack_post_blast,
                        false, error);
  }

  if (callback == Q2M_CALLBACK_carrier_reattack_gren) {
    bool repeat = monster->enemy.registry != 0 && enemy_in_front(context) &&
                  q2m_after(monster->timestamp_ns, 1.3) > context->game->now_ns;
    return q2m_set_move(context,
                        repeat ? Q2M_MOVE_carrier_move_attack_gren
                               : Q2M_MOVE_carrier_move_attack_post_gren,
                        false, error);
  }

  if (callback == Q2M_CALLBACK_carrier_reattack_mg) {
    q2m_move_id move = Q2M_MOVE_carrier_move_attack_post_mg;
    if (monster->enemy.registry != 0 && enemy_in_front(context) &&
        q2m_random(context->game) <= 0.5f)
      move = q2m_random(context->game) < 0.7f || monster->monster_slots <= 2
                 ? Q2M_MOVE_carrier_move_attack_mg
                 : Q2M_MOVE_carrier_move_spawn;
    return q2m_set_move(context, move, false, error);
  }

  if (callback == Q2M_CALLBACK_widow_reattack_blaster) {
    if (!widow_blaster(context, error))
      return false;
    if (!q2m_alive(context) || monster->pause_ns >= context->game->now_ns ||
        (monster->move->id == Q2M_MOVE_widow_move_attack_post_blaster_r) ||
        (monster->move->id == Q2M_MOVE_widow_move_attack_post_blaster_l))
      return true;
    monster->manual_steering = false;
    return q2m_set_move(context, Q2M_MOVE_widow_move_attack_post_blaster, false,
                        error);
  }

  if (callback == Q2M_CALLBACK_widow2_reattack_beam) {
    monster->manual_steering = false;
    bool repeat = monster->enemy.registry != 0 && enemy_in_front(context) &&
                  q2m_random(context->game) <= 0.5f;
    q2m_move_id move = Q2M_MOVE_widow2_move_attack_post_beam;
    if (repeat)
      move = q2m_random(context->game) < 0.7f ||
                     !q2m_summon_has_slots(monster, 2)
                 ? Q2M_MOVE_widow2_move_attack_beam
                 : Q2M_MOVE_widow2_move_spawn;
    return q2m_set_move(context, move, false, error);
  }

  if (callback == Q2M_CALLBACK_widow2_disrupt_reattack) {
    if (q2m_random(context->game) <
        0.25f + 0.15f * (float)context->game->options.skill)
      monster->next_frame = 28;
    return true;
  }

  *handled = false;
  return true;
}

static bool turret_ready_gun(q2m_context *context, qa_error *error) {
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (rerelease && context->monster->move &&
      context->monster->move->id == Q2M_MOVE_turret_move_ready_gun)
    return true;
  if (!q2m_set_move(context, Q2M_MOVE_turret_move_ready_gun, false, error))
    return false;
  return !rerelease || !q2m_alive(context) ||
      q2m_weapon_sound(context, Q2_NAME_RESOURCE_TURRET_MOVING_WAV, error);
}

static bool callback_turret_run(q2m_context *context, q2m_callback_id callback, qa_error *error) {
  (void)callback;
  const q2m_move *run = q2m_move_find(context->monster, Q2M_MOVE_turret_move_run);
  if (!run) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Turret has no Source run animation");
    return false;
  }
  if (context->monster->frame < run->first_frame)
    return turret_ready_gun(context, error);
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  if (rerelease) context->monster->high_tick_rate = true;
  if (!q2m_set_move(context, Q2M_MOVE_turret_move_run, false, error))
    return false;
  if (rerelease && q2m_alive(context) && context->monster->weapon_sound) {
    if (!q2m_weapon_sound(context, Q2_NAME_NONE, error)) return false;
    return !q2m_alive(context) ||
        q2m_sound(context, Q2_NAME_RESOURCE_TURRET_MOVED_WAV, 1, 1, error);
  }
  return true;
}

static bool end_transition(q2m_context *context, q2m_callback_id callback,
                           bool *handled, qa_error *error) {
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      (context->monster->definition->species == Q2M_MEDIC ||
       context->monster->definition->species == Q2M_MEDIC_COMMANDER) &&
      ((callback == Q2M_CALLBACK_medic_stand) || (callback == Q2M_CALLBACK_medic_walk))) {
    *handled = true;
    return q2m_set_move(context, (callback == Q2M_CALLBACK_medic_stand)
                                   ? Q2M_MOVE_medic_move_stand : Q2M_MOVE_medic_move_walk,
                        true, error);
  }
  if (context->monster->definition->species == Q2M_ACTOR &&
      ((callback == Q2M_CALLBACK_actor_run) || (callback == Q2M_CALLBACK_actor_stand) ||
       (callback == Q2M_CALLBACK_actor_walk))) {
    *handled = true;
    q2m_move_id move = Q2M_MOVE_actor_move_run;
    if (callback == Q2M_CALLBACK_actor_stand)
      move = Q2M_MOVE_actor_move_stand;
    else if (callback == Q2M_CALLBACK_actor_walk)
      move = Q2M_MOVE_actor_move_walk;
    else if (context->game->now_ns < context->monster->pain_ns &&
             !context->monster->enemy.registry)
      move = context->monster->move_target.registry ? Q2M_MOVE_actor_move_walk : Q2M_MOVE_actor_move_stand;
    else if (context->monster->stand_ground)
      move = Q2M_MOVE_actor_move_stand;
    if (!q2m_set_move(context, move, true, error))
      return false;
    if (q2m_alive(context) &&
        (context->monster->move->id == Q2M_MOVE_actor_move_stand) &&
        context->game->now_ns < Q2M_SECOND) {
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
      ((callback == Q2M_CALLBACK_jorg_stand) || (callback == Q2M_CALLBACK_jorg_run))) {
    *handled = true;
    bool stand = (callback == Q2M_CALLBACK_jorg_stand) || context->monster->stand_ground;
    if (!q2m_set_move(context, stand ? Q2M_MOVE_jorg_move_stand : Q2M_MOVE_jorg_move_run, true, error))
      return false;
    return q2m_jorg_sound_end(context, error);
  }
  if (context->game->options.edition == QA_Q2_RERELEASE &&
      ((callback == Q2M_CALLBACK_soldier_stand) ||
       (callback == Q2M_CALLBACK_soldier_run))) {
    *handled = true;
    if (callback == Q2M_CALLBACK_soldier_run)
      return soldier_run(context, error);
    float draw = q2m_random(context->game);
    q2m_move_id move = !context->monster->move ||
                      (context->monster->move->id != Q2M_MOVE_soldier_move_stand1) ||
                      draw < .6f ? Q2M_MOVE_soldier_move_stand1
                        : draw < .8f ? Q2M_MOVE_soldier_move_stand2 : Q2M_MOVE_soldier_move_stand3;
    if (!q2m_set_move(context, move, true, error)) return false;
    return q2m_soldier_sound_end(context, error);
  }
  if (callback == Q2M_CALLBACK_soldier_stand_up) {
    *handled = true;
    if (!q2m_set_move(context, Q2M_MOVE_soldier_move_trip, false, error))
      return false;
    context->monster->next_frame = 134;
    return true;
  }
  if (callback == Q2M_CALLBACK_guncmdr_kick_finished) {
    *handled = true;
    context->monster->melee_ns = q2m_after(context->game->now_ns, 3.0);
    return set_definition_move(context, context->monster->definition->run_move,
                               error);
  }
  if ((callback == Q2M_CALLBACK_supertank_dead) ||
      (callback == Q2M_CALLBACK_boss5_dead) ||
      (callback == Q2M_CALLBACK_boss2_dead) ||
      (callback == Q2M_CALLBACK_jorg_dead) ||
      (callback == Q2M_CALLBACK_guardian_dead)) {
    *handled = true;
    return q2m_finish_boss_death(context, error);
  }
  if (transitions[callback].move != Q2M_MOVE_NONE) {
    *handled = true;
    bool found;
    if (!set_existing_move(context, transitions[callback].move, false, &found, error))
      return false;
    if (found)
      return true;
    return set_definition_move(context, context->monster->definition->run_move,
                               error);

  }
  if ((q2m_callbacks[callback].flags & Q2M_CALLBACK_DEAD) != 0 || (callback == Q2M_CALLBACK_soldier_dead2) ||
      (callback == Q2M_CALLBACK_monster_dead) ||
      (callback == Q2M_CALLBACK_widow2_finaldeath)) {
    *handled = true;
    return q2m_corpse_callback(context, callback, error);
  }
  if ((q2m_callbacks[callback].flags & Q2M_CALLBACK_RUN) != 0 || (q2m_callbacks[callback].flags & Q2M_CALLBACK_RUN_LOOP) != 0 ||
      (callback == Q2M_CALLBACK_mutant_walk_loop)) {
    *handled = true;
    if (callback == Q2M_CALLBACK_guncmdr_run) {
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
  if ((q2m_callbacks[callback].flags & Q2M_CALLBACK_STAND) != 0 ||
      (callback == Q2M_CALLBACK_insane_onground) ||
      (callback == Q2M_CALLBACK_insane_cross)) {
    *handled = true;
    return set_definition_move(context,
                               context->monster->definition->stand_move, error);
  }
  if ((q2m_callbacks[callback].flags & Q2M_CALLBACK_WALK) != 0) {
    *handled = true;
    return set_definition_move(context, context->monster->definition->walk_move,
                               error);
  }
  return true;
}

static bool foundational_species_callback(q2m_context *context,
                                          q2m_callback_id callback, bool *handled,
                                          qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  *handled = true;

  if (callback == Q2M_CALLBACK_berserk_run_swing) {
    if (!q2m_sound(context, Q2_NAME_RESOURCE_BERSERK_ATTACK_WAV, 1, 1, error)) return false;
    if (!q2m_alive(context)) return true;
    monster->melee_ns = q2m_after(context->game->now_ns, .6);
    if (monster->attack_state == Q2M_SLIDING) {
      monster->attack_state = Q2M_STRAIGHT;
      monster->dodging = false;
    }
    return true;
  }
  if (callback == Q2M_CALLBACK_berserk_swing)
    return q2m_sound(context, Q2_NAME_RESOURCE_BERSERK_ATTACK_WAV, 1, 1.0f, error);
  if (callback == Q2M_CALLBACK_berserk_strike)
    return true;
  if (callback == Q2M_CALLBACK_berserk_high_gravity) {
    context->actor->physics.gravity_scale =
        context->body.velocity.z < 0.0f ? 2.25f : 5.25f;
    return true;
  }
  if (callback == Q2M_CALLBACK_berserk_check_landing) {
    context->actor->physics.gravity_scale =
        context->body.velocity.z < 0.0f ? 2.25f : 5.25f;
    if (qa_actor_reference_present(context->body.ground)) {
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

  if (callback == Q2M_CALLBACK_brain_swing_right)
    return q2m_sound(context, Q2_NAME_RESOURCE_BRAIN_MELEE1_WAV, 4, 1.0f, error);
  if (callback == Q2M_CALLBACK_brain_swing_left)
    return q2m_sound(context, Q2_NAME_RESOURCE_BRAIN_MELEE2_WAV, 4, 1.0f, error);
  if (callback == Q2M_CALLBACK_brain_chest_open) {
    if (context->game->options.edition == QA_Q2_RERELEASE)
      monster->count = 0;
    else
      monster->spawnflags &= ~UINT32_C(65536);
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
           q2m_sound(context, Q2_NAME_RESOURCE_BRAIN_BRNATCK1_WAV, 4, 1.0f, error);
  }
  if (callback == Q2M_CALLBACK_brain_chest_closed) {
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
    return q2m_set_move(context, Q2M_MOVE_brain_move_attack1, false, error);
  }

  if (callback == Q2M_CALLBACK_ChickMoan)
    return q2m_sound(context,
                     q2m_random(context->game) < 0.5f ? Q2_NAME_RESOURCE_CHICK_CHKIDLE1_WAV
                                                      : Q2_NAME_RESOURCE_CHICK_CHKIDLE2_WAV,
                     2, 2.0f, error);
  if (callback == Q2M_CALLBACK_Chick_PreAttack1)
    return q2m_sound(context, Q2_NAME_RESOURCE_CHICK_CHKATCK1_WAV, 2, 1.0f, error);
  if (callback == Q2M_CALLBACK_ChickReload)
    return q2m_sound(context, Q2_NAME_RESOURCE_CHICK_CHKATCK5_WAV, 2, 1.0f, error);
  if (callback == Q2M_CALLBACK_ChickRocket) {
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
                            : 500.0f + 100.0f * (float)context->game->options.skill;
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


  if (callback == Q2M_CALLBACK_floater_fire_blaster) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool effect = rerelease ? monster->frame % 4 == 0
                            : monster->frame == 34 || monster->frame == 37;
    return fire_source_exact(context, Q2M_ATTACK_BLASTER, 1.0f, 82, 0.0f,
                             1000.0f, true, effect ? 64u : 0u, error);
  }
  if (callback == Q2M_CALLBACK_floater_zap) {
    if (!q2m_sound(context, Q2_NAME_RESOURCE_FLOATER_FLTATCK2_WAV, 1, 1.0f, error))
      return false;
    return !q2m_alive(context) ||
           q2m_damage_enemy(context, FLT_MAX, 4, 0,
                            5.0f + floorf(q2m_random(context->game) * 6.0f),
                            -10.0f, NULL, error);
  }

  if (callback == Q2M_CALLBACK_flyer_pop_blades)
    return q2m_sound(context, Q2_NAME_RESOURCE_FLYER_FLYATCK1_WAV, 2, 1.0f, error);
  if ((callback == Q2M_CALLBACK_flyer_fireleft) ||
      (callback == Q2M_CALLBACK_flyer_fireright)) {
    int flash = (callback == Q2M_CALLBACK_flyer_fireleft) ? 58 : 59;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    bool effect = rerelease ? monster->frame % 4 == 0
                            : monster->frame == 82 || monster->frame == 85 ||
                                  monster->frame == 88;
    return fire_source_exact(context, Q2M_ATTACK_BLASTER, 1.0f, flash, 0.0f,
                             1000.0f, true, effect ? 64u : 0u, error);
  }

  if ((callback == Q2M_CALLBACK_gladiator_cleaver_swing) ||
      (callback == Q2M_CALLBACK_gladb_cleaver_swing))
    return q2m_sound(context, Q2_NAME_RESOURCE_GLADIATOR_MELEE1_WAV, 1, 1.0f, error);
  if (callback == Q2M_CALLBACK_GladiatorGun) {
    qa_vec3 start;
    if (!q2m_project_flash(context, 61, &start, error))
      return false;
    qa_vec3 direction = qa_vec_normalize(
        qa_vec_sub(monster->blind_fire_target, start));
    q2m_fire_spec spec = q2m_fire_default(
        context, Q2M_ATTACK_RAIL, 50.0f, 61, start, direction);
    return q2m_fire(context, &spec, error);
  }

  if (callback == Q2M_CALLBACK_gunner_idlesound)
    return q2m_sound(context, Q2_NAME_RESOURCE_GUNNER_GUNIDLE1_WAV, 2, 2.0f, error);
  if (callback == Q2M_CALLBACK_gunner_opengun)
    return q2m_sound(context, Q2_NAME_RESOURCE_GUNNER_GUNATCK1_WAV, 2, 2.0f, error);
  if (callback == Q2M_CALLBACK_GunnerGrenade) {
    return gunner_grenade(context, error);
  }
  if (callback == Q2M_CALLBACK_GunnerFire) {
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
  if (callback == Q2M_CALLBACK_gunner_duck_down) {
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
  if (callback == Q2M_CALLBACK_gunner_duck_hold) {
    monster->hold_frame = context->game->now_ns < monster->pause_ns;
    return true;
  }
  if (callback == Q2M_CALLBACK_gunner_duck_up)
    return set_duck(context, false, error);

  if (callback == Q2M_CALLBACK_hover_fire_blaster) {
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

  if (callback == Q2M_CALLBACK_medic_fire_blaster) {
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
    bool tesla = rogue && enemy_traits.classname ==
        context->game->runtime_names[rerelease ? Q2_NAME_TESLA_MINE : Q2_NAME_TESLA];
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

  if ((callback == Q2M_CALLBACK_mutant_step) ||
      (callback == Q2M_CALLBACK_gekk_step)) {
    unsigned variant =
        ((unsigned)(q2m_random(context->game) * 3.0f) + 1u) % 3u + 1u;
    static const q2_runtime_name steps[2][3] = {
        {Q2_NAME_RESOURCE_MUTANT_STEP1_WAV, Q2_NAME_RESOURCE_MUTANT_STEP2_WAV, Q2_NAME_RESOURCE_MUTANT_STEP3_WAV},
        {Q2_NAME_RESOURCE_GEK_GK_STEP1_WAV, Q2_NAME_RESOURCE_GEK_GK_STEP2_WAV, Q2_NAME_RESOURCE_GEK_GK_STEP3_WAV}
    };
    q2_runtime_name path=steps[callback==Q2M_CALLBACK_gekk_step][variant-1];
    return q2m_sound(context, path, 2, 1.0f, error);
  }
  if (callback == Q2M_CALLBACK_mutant_jump_takeoff) {
    qa_vec3 forward;
    qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
    if (!q2m_sound(context, Q2_NAME_RESOURCE_MUTANT_MUTSGHT1_WAV, 2, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    context->body.origin.z += 1.0f;
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    context->body.velocity = qa_vec_scale(forward, rerelease ? 425.0f : 600.0f);
    context->body.velocity.z = rerelease ? 160.0f : 250.0f;
    context->body.ground = (qa_actor_reference){0};
    monster->ducked = true;
    monster->touch_active = true;
    if (rerelease)
      monster->style = 1;
    monster->attack_ns = q2m_after(context->game->now_ns, 3.0);
    return q2m_write_body(context, true, error);
  }
  if (callback == Q2M_CALLBACK_mutant_check_landing) {
    if (qa_actor_reference_present(context->body.ground)) {
      monster->attack_ns = 0;
      monster->ducked = false;
      monster->touch_active = false;
      return q2m_sound(context, Q2_NAME_RESOURCE_MUTANT_THUD1_WAV, 1, 1.0f, error);
    }
    monster->next_frame = context->game->now_ns > monster->attack_ns ? 1 : 4;
    return true;
  }

  if ((callback == Q2M_CALLBACK_gekk_jump_takeoff) ||
      (callback == Q2M_CALLBACK_gekk_jump_takeoff2)) {
    bool from_water = (callback == Q2M_CALLBACK_gekk_jump_takeoff2);
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
    if (!q2m_sound(context, Q2_NAME_RESOURCE_GEK_GK_SGHT1_WAV, 2, 1.0f, error))
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
    context->body.ground = (qa_actor_reference){0};
    monster->ducked = true;
    monster->touch_active = true;
    monster->jump_ns = q2m_after(context->game->now_ns, 3.0);
    return q2m_write_body(context, true, error);
  }
  if (callback == Q2M_CALLBACK_gekk_check_landing) {
    if (qa_actor_reference_present(context->body.ground)) {
      monster->jump_ns = 0;
      monster->ducked = false;
      monster->touch_active = false;
      context->body.velocity = qa_v3(0, 0, 0);
      return q2m_write_body(context, false, error) &&
             q2m_sound(context, Q2_NAME_RESOURCE_MUTANT_THUD1_WAV, 1, 1.0f, error);
    }
    monster->next_frame = context->game->now_ns > monster->jump_ns ? 81 : 82;
    return true;
  }

  if (callback == Q2M_CALLBACK_berserk_jump_takeoff) {
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
    context->body.ground = (qa_actor_reference){0};
    context->actor->physics.gravity_scale = 5.25f;
    monster->ducked = true;
    monster->touch_active = true;
    monster->jump_ns = q2m_after(context->game->now_ns, 3.0);
    return q2m_write_body(context, true, error);
  }

  if (callback == Q2M_CALLBACK_parasite_launch)
    return q2m_sound(context, Q2_NAME_RESOURCE_PARASITE_PARATCK1_WAV, 1, 1.0f, error);
  if (callback == Q2M_CALLBACK_parasite_reel_in)
    return q2m_sound(context, Q2_NAME_RESOURCE_PARASITE_PARATCK4_WAV, 1, 1.0f, error);
  if (callback == Q2M_CALLBACK_parasite_tap)
    return q2m_sound(context, Q2_NAME_RESOURCE_PARASITE_PARIDLE1_WAV, 1, 2.0f, error);
  if (callback == Q2M_CALLBACK_parasite_scratch)
    return q2m_sound(context, Q2_NAME_RESOURCE_PARASITE_PARIDLE2_WAV, 1, 2.0f, error);
  if (callback == Q2M_CALLBACK_parasite_drain_attack) {
    bool first = monster->frame == 41;
    if (first && !q2m_sound(context, Q2_NAME_RESOURCE_PARASITE_PARATCK2_WAV, 0, 1.0f, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (monster->frame == 42 &&
        !q2m_sound(context, Q2_NAME_RESOURCE_PARASITE_PARATCK3_WAV, 1, 1.0f, error))
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
            qa_vec_scale(enemy.velocity, travel - 0.3f + (float)index * 0.15f));
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
  float scale = context->monster->entity_scale != 0 ? context->monster->entity_scale : 1;
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
        .policy = qa_collision_default_policy(QA_GAME_Q2),
    };
    query.policy.contents_mask = qa_collision_contents_mask(mask, QA_GAME_Q2);
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
      .policy = qa_collision_default_policy(QA_GAME_Q2),
  };
  query.policy.contents_mask = qa_collision_contents_mask(mask, QA_GAME_Q2);
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
          .policy = qa_collision_default_policy(QA_GAME_Q2),
      };
      query.policy.contents_mask = qa_collision_contents_mask(UINT32_C(0x46000003), QA_GAME_Q2);
      qa_trace_result trace;
      if (!qa_world_trace(context->game->services.world, &query, &trace,
                          error))
        return false;
      if (!q2m_alive(context))
        return true;
      origin = trace.end;
      if (trace.fraction >= 1.0f)
        continue;
      if (trace.has_surface && (qa_collision_surface_export(trace.surface_flags, QA_GAME_Q2) & 4) != 0)
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
      if (((uint32_t)qa_collision_contents_export(trace.contents, QA_GAME_Q2, trace.q1_opaque_token) & UINT32_C(0x46000000)) != 0)
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

static bool guncmdr_grenade(q2m_context *context, qa_error *error) {
  static const struct { int frame, flash; float spread; } shots[] = {
      {381, 242, -.1f}, {384, 243, 0}, {387, 244, .1f},
      {401, 245, -.1f}, {404, 246, 0}, {407, 247, .1f},
      {748, 248, .25f}, {749, 249, 0}, {750, 250, -.25f}};
  size_t shot;
  for (shot = 0; shot < sizeof(shots) / sizeof(*shots); ++shot)
    if (shots[shot].frame == context->monster->frame)
      break;
  if (shot == sizeof(shots) / sizeof(*shots)) {
    qa_error_set(error, QA_ERROR_FORMAT, context->actor->id.slot,
                  "Gunner commander grenade has no authored muzzle frame");
    return false;
  }
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available) || !available ||
      !q2m_alive(context))
    return true;
  qa_vec3 target = enemy.origin;
  if (context->monster->manual_steering) {
    bool visible;
    if (!q2m_visible(context, context->monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (!visible) {
      target = context->monster->blind_fire_target;
      if (qa_vec_length(target) == 0)
        return true;
    }
  }
  int flash = shots[shot].flash;
  bool mortar = flash <= 244, crouch = flash >= 248;
  qa_vec3 start, forward, right, up, aim;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  qa_builtin_angle_vectors(context->body.angles, &forward, &right, &up);
  if (crouch) {
    if (!q2m_predict_from(context, start, 800, false, 0, NULL, &aim,
                          &available, error))
      return false;
    if (!available || !q2m_alive(context))
      return true;
    aim = qa_vec_normalize(qa_vec_add(aim, qa_vec_scale(right, shots[shot].spread)));
    for (int i = 0; i < 3; ++i) {
      q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_ION, 15, 0,
          start, qa_vec_add(aim, qa_vec_scale(right, -.25f + .125f * (float)(i + 1))));
      spec.speed = 800;
      spec.has_projectile_effects = true;
      spec.projectile_effects = UINT64_C(0x00100000);
      if (!q2m_fire(context, &spec, error))
        return false;
      if (!q2m_alive(context))
        return true;
    }
    return q2m_emit(context, QA_BUILTIN_MUZZLE, Q2_NAME_RESOURCE_Q2_MONSTER_MUZZLE, flash,
                     start, qa_vec_add(start, aim), 1, error);
  }
  qa_vec3 delta = qa_vec_sub(target, context->body.origin);
  float distance = qa_vec_length(delta);
  if (distance > 512 && delta.z > -64 && delta.z < 64)
    delta.z += distance - 512;
  float pitch = fmaxf(-.5f, fminf(.4f, qa_vec_normalize(delta).z));
  if (mortar && enemy.origin.z + enemy.bounds.mins.z -
                   context->body.origin.z - context->body.bounds.maxs.z > 16)
    pitch += .5f;
  if (!mortar)
    pitch -= .05f;
  aim = qa_vec_normalize(qa_vec_add(qa_vec_add(forward,
      qa_vec_scale(right, shots[shot].spread)), qa_vec_scale(up, pitch)));
  float speed = mortar ? 850 : 600;
  float gravity = context->game->services.physics
                      ? context->game->services.physics->gravity : 800;
  qa_vec3 direction;
  bool predicted;
  if (!mortar_direction(context, target, start, aim, speed, gravity, mortar,
                        &direction, &predicted, error))
    return false;
  if (!q2m_alive(context))
    return true;
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_GRENADE, 50,
      flash, start, predicted ? direction : aim);
  spec.speed = speed;
  spec.fuse = 2.5f;
  spec.radius = 90;
  spec.radius_damage = 50;
  spec.has_grenade_impulse = true;
  spec.grenade_right = q2m_crandom(context->game) * 10;
  spec.grenade_up = predicted ? q2m_random(context->game) * 10
                              : 200 + q2m_crandom(context->game) * 10;
  spec.grenade_gravity = gravity;
  return q2m_fire(context, &spec, error);
}

static bool arachnid_rail(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int flash = frame == 7 ? 229 : frame == 121 ? 230
                               : frame == 125 ? 231 : 228;
  qa_vec3 start;
  if (!q2m_project_flash(context, flash, &start, error))
    return false;
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_RAIL, 35,
      flash, start, qa_vec_normalize(qa_vec_sub(
          context->monster->saved_attack_position, start)));
  spec.kick = 100;
  return q2m_fire(context, &spec, error);
}

static bool gladb_gun(q2m_context *context, qa_error *error) {
  qa_vec3 start;
  if (!q2m_project_flash(context, 61, &start, error))
    return false;
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  float damage = 35, radius = 45;
  if (rerelease && context->monster->frame > 48) {
    damage = 17;
    radius = 22;
  }
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_PLASMA, damage, 0,
      start, qa_vec_normalize(qa_vec_sub(context->monster->blind_fire_target, start)));
  exact_projectile_speed(&spec, 725);
  spec.radius = spec.radius_damage = radius;
  if (!q2m_fire(context, &spec, error))
    return false;
  if (rerelease && q2m_alive(context))
    return makron_save_location(context);
  return true;
}

static bool actor_fire(q2m_context *context, qa_error *error) {
  qa_vec3 start, direction;
  if (!q2m_project_flash(context, 63, &start, error))
    return false;
  qa_builtin_angle_vectors(context->body.angles, &direction, NULL, NULL);
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available))
    return false;
  if (!q2m_alive(context))
    return true;
  if (available) {
    qa_vec3 point;
    if (enemy_alive(context))
      point = qa_vec_add(qa_vec_add(enemy.origin, qa_vec_scale(enemy.velocity, -.2f)),
                         qa_v3(0, 0, traits.view_height));
    else {
      point = qa_vec_add(enemy.origin, enemy.bounds.mins);
      point.z += (enemy.bounds.maxs.z - enemy.bounds.mins.z) * .5f + 1;
    }
    direction = qa_vec_normalize(qa_vec_sub(point, start));
  }
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_BULLET, 3, 63,
                                       start, direction);
  spec.kick = 4;
  if (!q2m_fire(context, &spec, error))
    return false;
  if (q2m_alive(context))
    context->monster->hold_frame = context->game->now_ns <
        (context->game->options.edition == QA_Q2_RERELEASE
             ? context->monster->fire_ns : context->monster->pause_ns);
  return true;
}

static bool guncmdr_fire(q2m_context *context, qa_error *error) {
  int frame = context->monster->frame;
  int flash = frame >= 419 && frame <= 428 ? 241 : 240;
  qa_vec3 start, direction;
  bool available;
  if (!q2m_project_flash(context, flash, &start, error) ||
      !q2m_predict_from(context, start, 800, false,
          q2m_random(context->game) * .3f, NULL, &direction, &available, error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  direction.x += q2m_crandom(context->game) * .025f;
  direction.y += q2m_crandom(context->game) * .025f;
  direction.z += q2m_crandom(context->game) * .025f;
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_FLECHETTE, 4,
                                       flash, start, direction);
  spec.speed = 800;
  return q2m_fire(context, &spec, error);
}

static bool fixbot_blaster(q2m_context *context, qa_error *error) {
  bool visible;
  if (!visible_enemy(context, &visible, error))
    return false;
  if (!q2m_alive(context))
    return true;
  if (!visible && !q2m_set_move(context, Q2M_MOVE_fixbot_move_run, false, error))
    return false;
  if (!q2m_alive(context))
    return true;
  return fire_source_exact(context, Q2M_ATTACK_BLASTER, 15, 62, 0, 1000,
                            true, 8, error);
}

static bool fixbot_welder(q2m_context *context, qa_error *error) {
  if (!q2_actor_live(context->game, context->monster->enemy))
    return true;
  qa_builtin_event event = {
      .kind = QA_BUILTIN_EFFECT, .family = QA_GAME_Q2,
      .provider = context->game->options.owner, .actor = context->actor->id,
      .time_ns = context->game->now_ns,
      .origin = q2m_project_offset(context, qa_v3(24, -.8f, -10)),
      .count = 10, .code = 0xe0 + (int32_t)q2_random_bounded(context->game, 8),
  };
  event.resource=context->game->runtime_names[Q2_NAME_WELDING_SPARKS];
  if (!qa_builtin_emit(&context->game->services, &event, error))
    return false;
  if (!q2m_alive(context) || q2m_random(context->game) <= .8f)
    return true;
  float choice = q2m_random(context->game);
  return q2m_sound(context, choice < .33f ? Q2_NAME_RESOURCE_MISC_WELDER1_WAV
                            : choice < .66f ? Q2_NAME_RESOURCE_MISC_WELDER2_WAV
                                            : Q2_NAME_RESOURCE_MISC_WELDER3_WAV, 2, 2, error);
}

static bool shambler_lightning(q2m_context *context, qa_error *error) {
  if (!q2_actor_live(context->game, context->monster->enemy))
    return true;
  qa_vec3 offset = qa_v3(0, 0, 48);
  bool clear;
  for (unsigned i = 0; i < 8; ++i, offset.z -= 4) {
    if (!q2m_clear_shot(context, offset, &clear, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (clear)
      break;
  }
  if (!clear)
    offset.z = 48;
  qa_vec3 start = q2m_project_offset(context, offset), direction;
  bool available;
  if (!q2m_predict_from(context, start, 0, false,
      context->monster->spawnflags & 1u ? 0 : .1f,
      NULL, &direction, &available, error))
    return false;
  if (!available || !q2m_alive(context))
    return true;
  qa_trace_query query = {
      .start = start, .end = qa_vec_add(start, qa_vec_scale(direction, 8192)),
      .pass_actor = context->actor->id,
      .policy = qa_collision_default_policy(QA_GAME_Q2),
  };
  query.policy.contents_mask = qa_collision_contents_mask(Q2M_ATTACK_MASK, QA_GAME_Q2);
  if (context->game->options.edition == QA_Q2_RERELEASE)
    query.policy.contents_mask = qa_collision_bits_union(query.policy.contents_mask, qa_collision_contents_mask(UINT32_C(0x40000000), QA_GAME_Q2));
  qa_trace_result trace;
  if (!qa_world_trace(context->game->services.world, &query, &trace, error))
    return false;
  if (!q2m_alive(context))
    return true;
  qa_builtin_event event = {
      .kind = QA_BUILTIN_BEAM, .family = QA_GAME_Q2,
      .provider = context->game->options.owner, .actor = context->actor->id,
      .other = context->game->services.physics->world_actor,
      .time_ns = context->game->now_ns, .origin = start, .end = trace.end,
      .q2_multicast = {QA_BUILTIN_Q2_MULTICAST_PVS,start},
  };
  event.resource=context->game->runtime_names[Q2_NAME_RESOURCE_Q2_LIGHTNING];
  if (!qa_builtin_emit(&context->game->services, &event, error))
    return false;
  if (!q2m_alive(context))
    return true;
  q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_BULLET,
      8 + (float)q2_random_bounded(context->game, 4), 0, start, direction);
  spec.kick = 15;
  spec.horizontal_spread = spec.vertical_spread = 0;
  spec.direct_mod = 45;
  return q2m_fire(context, &spec, error);
}

static bool turret_aim(q2m_context *context, qa_error *error) {
  if (!q2_actor_live(context->game, context->monster->enemy)) {
    bool found;
    if (!q2m_find_target(context, &found, error))
      return false;
    if (!q2m_alive(context) || !found)
      return true;
  }
  if (context->monster->frame < 2) {
    return turret_ready_gun(context, error);
  }
  if (context->monster->frame < 8)
    return true;
  qa_body_state enemy;
  qa_builtin_actor_traits traits;
  bool available;
  if (!target_body(context, &enemy, &traits, &available))
    return false;
  if (!q2m_alive(context) || !available)
    return true;
  qa_vec3 point = enemy.origin;
  if (context->monster->move &&
      (context->monster->move->id == Q2M_MOVE_turret_move_fire_blind)) {
    point = context->monster->blind_fire_target;
    point.z += enemy.origin.z < point.z ? traits.view_height + 10
                                         : enemy.bounds.mins.z - 10;
  } else if (traits.player)
    point.z += traits.view_height;
  qa_vec3 ideal = q2m_vector_angles(qa_vec_sub(point, context->body.origin));
  switch (context->monster->turret_orientation) {
  case -1:
    if (ideal.x < -90) ideal.x += 360;
    ideal.x = fminf(ideal.x, -5);
    break;
  case -2:
    if (ideal.x > -90) ideal.x -= 360;
    ideal.x = fminf(-185, fmaxf(-355, ideal.x));
    break;
  case 0:
    if (ideal.x < -180) ideal.x += 360;
    ideal.x = fminf(85, fmaxf(-85, ideal.x));
    if (ideal.y > 180) ideal.y -= 360;
    ideal.y = fminf(85, fmaxf(-85, ideal.y));
    break;
  case 90:
    if (ideal.x < -180) ideal.x += 360;
    ideal.x = fminf(85, fmaxf(-85, ideal.x));
    if (ideal.y > 270) ideal.y -= 360;
    ideal.y = fminf(175, fmaxf(5, ideal.y));
    break;
  case 180:
    if (ideal.x < -180) ideal.x += 360;
    ideal.x = fminf(85, fmaxf(-85, ideal.x));
    ideal.y = fminf(265, fmaxf(95, ideal.y));
    break;
  case 270:
    if (ideal.x < -180) ideal.x += 360;
    ideal.x = fminf(85, fmaxf(-85, ideal.x));
    if (ideal.y < 90) ideal.y += 360;
    ideal.y = fminf(355, fmaxf(185, ideal.y));
    break;
  }
  float speed = context->monster->yaw_speed *
      (context->game->options.edition == QA_Q2_RERELEASE ? context->elapsed * 10 : 1);
  float move = ideal.x - context->body.angles.x;
  while (move >= 360) move -= 360;
  if (move >= 90) move -= 360;
  while (move <= -360) move += 360;
  if (move <= -90) move += 360;
  context->body.angles.x = qa_builtin_angle_mod(context->body.angles.x +
                                               fminf(speed, fmaxf(-speed, move)));
  move = ideal.y - context->body.angles.y;
  if (move >= 180) move -= 360;
  if (move <= -180) move += 360;
  context->body.angles.y = qa_builtin_angle_mod(context->body.angles.y +
                                               fminf(speed, fmaxf(-speed, move)));
  return q2m_write_body(context, false, error) &&
      (!q2m_alive(context) || q2m_turret_lasersight(context, error));
}

static bool turret_fire(q2m_context *context, bool blind, qa_error *error) {
  if (!turret_aim(context, error))
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
  bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  uint32_t flags = context->monster->spawnflags;
  qa_vec3 point = blind || (rerelease && context->monster->lost_sight)
                     ? context->monster->blind_fire_target : enemy.origin;
  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  if (qa_vec_dot(qa_vec_normalize(qa_vec_sub(point, context->body.origin)), forward) < .98f)
    return true;
  float speed = flags & 32u ? 650 : flags & 8u ? 800 : 0;
  if (!rerelease) {
    if (flags & 32u) {
      speed = 550;
      if (context->game->options.skill == 2)
        speed += 200 * q2m_random(context->game);
      else if (context->game->options.skill == 3)
        speed += 100 + 200 * q2m_random(context->game);
    } else if (flags & 8u)
      speed = blind ? 1000 : context->game->options.skill == 0 ? 600
                            : context->game->options.skill == 1 ? 800 : 1000;
  }
  if (!blind) {
    bool visible;
    if (!visible_enemy(context, &visible, error))
      return false;
    if (!q2m_alive(context) || (!visible && !(rerelease && (flags & 16u))))
      return true;
    if (!(rerelease && context->monster->lost_sight))
      point.z += traits.player ? traits.view_height : 22;
  } else
    point.z += enemy.origin.z < point.z ? traits.view_height + 10
                                         : enemy.bounds.mins.z - 10;
  qa_vec3 start = context->body.origin, direction = qa_vec_sub(point, start);
  float distance = qa_vec_length(direction);
  if (!blind) {
    if (rerelease && !context->monster->lost_sight) {
      if ((flags & 16u) || q2m_random(context->game) < (float)context->game->options.skill / 5) {
        float offset = flags & 16u ? .3f
            : q2m_random(context->game) * (3 - (float)context->game->options.skill) / 3 -
              q2m_random(context->game) * .05f * (3 - (float)context->game->options.skill);
        if (!q2m_predict_from(context, start, speed, true, offset,
                              NULL, &direction, &available, error))
          return false;
        if (!q2m_alive(context) || !available)
          return true;
      }
    } else if (!rerelease && !(flags & 0x50u) && distance < 512 &&
        q2m_random(context->game) + (3 - (float)context->game->options.skill) * .1f < .8f)
      direction = qa_vec_sub(qa_vec_add(point, qa_vec_scale(enemy.velocity, distance / 1000)), start);
    qa_trace_query query = {.start = start, .end = point, .pass_actor = context->actor->id,
                           .policy = qa_collision_default_policy(QA_GAME_Q2)};
    query.policy.contents_mask = qa_collision_contents_mask(Q2M_ATTACK_MASK, QA_GAME_Q2);
    if (rerelease) query.policy.contents_mask = qa_collision_bits_union(query.policy.contents_mask, qa_collision_contents_mask(UINT32_C(0x40000000), QA_GAME_Q2));
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context) || (trace.hit == QA_TRACE_HIT_ACTOR &&
        !qa_actor_id_equal(trace.actor, context->monster->enemy) &&
        !qa_actor_id_equal(trace.actor, context->game->services.physics->world_actor)))
      return true;
    if ((flags & 32u) && distance * trace.fraction <= 72)
      return true;
  }
  q2m_attack_kind kind = flags & 8u ? Q2M_ATTACK_BLASTER
                         : flags & 16u ? Q2M_ATTACK_BULLET : Q2M_ATTACK_ROCKET;
  if (blind && kind == Q2M_ATTACK_BULLET)
    return true;
  if (rerelease && kind == Q2M_ATTACK_BULLET) {
    if (!context->monster->hold_frame) {
      context->monster->hold_frame = true;
      context->monster->duck_ns = q2m_after(context->game->now_ns,
          2 + q2m_random(context->game) * (float)context->game->options.skill);
      context->monster->next_duck_ns = q2m_after(context->game->now_ns, 1);
      return q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_CHNGNU1A_WAV, 2, 1, error);
    }
    if (context->monster->duck_ns < context->game->now_ns)
      context->monster->hold_frame = false;
    if (context->monster->next_duck_ns >= context->game->now_ns ||
        context->monster->melee_ns > context->game->now_ns)
      return true;
    context->monster->melee_ns = q2m_after(context->game->now_ns, .1);
  }
  float damage = kind == Q2M_ATTACK_BLASTER ? rerelease ? 8 : 20
                   : kind == Q2M_ATTACK_BULLET ? rerelease ? 2 : 4
                                              : rerelease ? 40 : 50;
  int flash = kind == Q2M_ATTACK_BLASTER ? 143 : kind == Q2M_ATTACK_BULLET ? 141 : 142;
  q2m_fire_spec spec = q2m_fire_default(context, kind, damage, flash,
                                       start, qa_vec_normalize(direction));
  spec.kick = 0;
  if (kind != Q2M_ATTACK_BULLET) {
    spec.speed = speed;
    if (kind == Q2M_ATTACK_BLASTER) {
      spec.has_projectile_effects = true;
      spec.projectile_effects = 8;
    }
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

static bool heavy_weapon_callback(q2m_context *context, q2m_callback_id callback,
                                  bool *handled, qa_error *error) {
  *handled = true;
  if (callback == Q2M_CALLBACK_Boss2MachineGun)
    return boss2_bullet(context, 73, error) &&
           (!q2m_alive(context) || boss2_bullet(context, 133, error));
  if (callback == Q2M_CALLBACK_boss2_firebullet_left)
    return boss2_bullet(context, 73, error);
  if (callback == Q2M_CALLBACK_boss2_firebullet_right)
    return boss2_bullet(context, 133, error);
  if (callback == Q2M_CALLBACK_Boss2Rocket)
    return boss2_rockets(context, false, error);
  if (callback == Q2M_CALLBACK_Boss2PredictiveRocket)
    return boss2_rockets(context, true, error);
  if (callback == Q2M_CALLBACK_Boss2Rocket64)
    return boss2_rocket64(context, error);
  if (callback == Q2M_CALLBACK_Boss2HyperBlaster)
    return boss2_hyperblaster(context, error);
  if (callback == Q2M_CALLBACK_jorg_firebullet)
    return jorg_bullets(context, -1, error);
  if (callback == Q2M_CALLBACK_jorg_firebullet_left)
    return jorg_bullets(context, 120, error);
  if (callback == Q2M_CALLBACK_jorg_firebullet_right)
    return jorg_bullets(context, 126, error);
  if (callback == Q2M_CALLBACK_jorgBFG) {
    bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
    if (!q2m_sound(context,
                   rerelease ? Q2_NAME_RESOURCE_MAKRON_BFG_FIRE_WAV : Q2_NAME_RESOURCE_BOSS3_BS3ATCK2_WAV,
                   rerelease ? 1 : 2, 1.0f, error))
      return false;
    return !q2m_alive(context) || fire_bfg_exact(context, 132, 200.0f, error);
  }
  if (callback == Q2M_CALLBACK_makronBFG) {
    if (!q2m_sound(context, Q2_NAME_RESOURCE_MAKRON_BFG_FIRE_WAV, 2, 1.0f, error))
      return false;
    return !q2m_alive(context) || fire_bfg_exact(context, 101, 300.0f, error);
  }
  if (callback == Q2M_CALLBACK_MakronSaveloc)
    return makron_save_location(context);
  if (callback == Q2M_CALLBACK_MakronRailgun) {
    qa_vec3 start;
    if (!q2m_project_flash(context, 119, &start, error))
      return false;
    qa_vec3 direction = qa_vec_normalize(
        qa_vec_sub(context->monster->blind_fire_target, start));
    q2m_fire_spec spec = q2m_fire_default(
        context, Q2M_ATTACK_RAIL, 50.0f, 119, start, direction);
    return q2m_fire(context, &spec, error);
  }
  if (callback == Q2M_CALLBACK_MakronHyperblaster)
    return makron_hyperblaster(context, error);
  if ((callback == Q2M_CALLBACK_supertankMachineGun) ||
      (callback == Q2M_CALLBACK_boss5MachineGun))
    return supertank_machinegun(context, error);
  if ((callback == Q2M_CALLBACK_supertankRocket) ||
      (callback == Q2M_CALLBACK_boss5Rocket))
    return supertank_rocket(context, error);
  if (callback == Q2M_CALLBACK_supertankGrenade)
    return supertank_grenade(context, error);
  if (callback == Q2M_CALLBACK_TankBlaster)
    return tank_blaster(context, error);
  if (callback == Q2M_CALLBACK_TankRocket)
    return tank_rocket(context, error);
  if (callback == Q2M_CALLBACK_TankMachineGun)
    return tank_machinegun(context, error);
  if ((callback == Q2M_CALLBACK_CarrierMachineGun) ||
      (callback == Q2M_CALLBACK_CarrierMachineGunHold))
    return carrier_machineguns(context, error);
  if (callback == Q2M_CALLBACK_CarrierRocket)
    return carrier_rocket(context, error);
  if (callback == Q2M_CALLBACK_CarrierGrenade)
    return carrier_grenade(context, error);
  if (callback == Q2M_CALLBACK_CarrierRail)
    return carrier_rail(context, error);
  if (callback == Q2M_CALLBACK_CarrierSaveLoc)
    return carrier_coop_check(context, error) &&
           (!q2m_alive(context) || carrier_save_location(context));
  *handled = false;
  return true;
}

static bool fixbot_goal_create(q2m_context *context, int vertical, qa_error *error) {
  qa_vec3 point = {0};
  qa_bounds bounds = {{-32,-32,-24},{32,32,24}};
  qa_trace_query query = {.start = context->body.origin, .pass_actor = context->actor->id,
    .policy = qa_collision_default_policy(QA_GAME_Q2)};
  if (vertical) {
    qa_vec3 up;
    qa_builtin_angle_vectors(context->body.angles, NULL, NULL, &up);
    query.end = qa_vec_add(context->body.origin, qa_vec_scale(up, vertical < 0 ? -8096 : 128));
    query.shape = (qa_trace_shape){.kind = QA_SHAPE_BOX, .bounds = bounds};
    query.policy.contents_mask = qa_collision_contents_mask(Q2M_MONSTER_MASK, QA_GAME_Q2);
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    point = trace.end;
  } else {
    float longest = 0;
    query.policy.contents_mask = qa_collision_contents_mask(Q2M_ATTACK_MASK |
        (context->game->options.edition == QA_Q2_RERELEASE ? Q2_PLAYER_CONTENTS : 0), QA_GAME_Q2);
    for (int i = 0; i < 12; ++i) {
      qa_vec3 angles = context->body.angles, forward;
      angles.y += (float)(i < 6 ? 30 * i : -30 * (i - 6));
      qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
      query.end = qa_vec_add(context->body.origin, qa_vec_scale(forward, 8192));
      qa_trace_result trace;
      if (!qa_world_trace(context->game->services.world, &query, &trace, error))
        return false;
      if (!q2m_alive(context)) return true;
      float length = qa_vec_length(qa_vec_sub(context->body.origin, trace.end));
      if (length > longest) { longest = length; point = trace.end; }
    }
  }
  if (!q2m_alive(context)) return true;
  qa_actor_id goal;
  if (!q2m_fixbot_goal(context, point, vertical ? &bounds : NULL, &goal, error))
    return false;
  if (!q2m_alive(context) || !goal.registry) return true;
  context->monster->goal = context->monster->enemy = goal;
  return q2m_set_move(context, vertical < 0 ? Q2M_MOVE_fixbot_move_landing :
      vertical > 0 ? Q2M_MOVE_fixbot_move_takeoff : Q2M_MOVE_fixbot_move_turn, false, error);
}

static bool fixbot_roam(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  bool acquired = false;
  if (!(context->game->options.edition == QA_Q2_RERELEASE ? m->enemy.registry : m->goal.registry) &&
      !q2m_medic_acquire(context, true, &acquired, error))
    return false;
  if (!q2m_alive(context) || acquired) return true;
  if (!q2m_set_move(context, Q2M_MOVE_fixbot_move_roamgoal, false, error)) return false;
  if (m->spawnflags & 1u) {
    if (!fixbot_goal_create(context, -1, error)) return false;
    if (!q2m_alive(context)) return true;
    m->spawnflags = 8;
  }
  if (m->spawnflags & 2u) {
    if (!fixbot_goal_create(context, 1, error)) return false;
    if (!q2m_alive(context)) return true;
    m->spawnflags = 8;
  }
  if (m->spawnflags & 4u) {
    if (!q2m_set_move(context, Q2M_MOVE_fixbot_move_roamgoal, false, error)) return false;
    if (!q2m_alive(context)) return true;
    m->spawnflags = 8;
  }
  return m->spawnflags || q2m_set_move(context, Q2M_MOVE_fixbot_move_stand2, false, error);
}

static bool fixbot_finish_goal(q2m_context *context, bool clear, qa_error *error) {
  q2m_fixbot_goal_retire(context, context->monster->goal);
  if (clear) context->monster->goal = context->monster->enemy = (qa_actor_id){0};
  return q2m_set_move(context, Q2M_MOVE_fixbot_move_stand, false, error);
}

static bool fixbot_scanner(q2m_context *context, qa_error *error) {
  qa_q2_game *game = context->game;
  qa_builtin_snapshot_frame *nearby = q2_nearby(game, context->body.origin, 1024, error);
  if (!nearby) return false;
  qa_actor_id repair = {0};
  bool result = true;
  for (size_t i = 0; i < nearby->snapshot.count; ++i) {
    qa_actor_id id = nearby->snapshot.ids[i];
    q2_actor *a = q2_actor_get(game, id, false, NULL);
    qa_combat_state combat;
    if (!a || !a->entity || a->entity->scenery != Q2S_REPAIR ||
        !qa_combat_read(game->services.combat, id, &combat, NULL) || combat.health < 100)
      continue;
    bool visible;
    if (!q2m_visible(context, id, &visible, error)) { result = false; break; }
    if (!q2m_alive(context)) break;
    if (visible) { repair = id; break; }
  }
  qa_builtin_snapshot_release(nearby);
  if (!result || !q2m_alive(context)) return result;
  if (repair.registry) {
    q2m_fixbot_goal_retire(context, context->monster->goal);
    context->monster->goal = context->monster->enemy = repair;
    q2m_fixbot_flight(context, false, true);
    return q2m_distance(context, repair) >= 32 ||
        q2m_set_move(context, Q2M_MOVE_fixbot_move_weld_start, false, error);
  }
  qa_actor_id goal_id = context->monster->goal;
  q2_actor *goal = q2_actor_get(game, goal_id, false, NULL);
  if (!goal) return q2m_set_move(context, Q2M_MOVE_fixbot_move_stand, false, error);
  bool is_repair = goal->entity && goal->entity->scenery == Q2S_REPAIR;
  float distance = q2m_distance(context, goal_id);
  if (distance < 32)
    return is_repair ? q2m_set_move(context, Q2M_MOVE_fixbot_move_weld_start, false, error)
                     : fixbot_finish_goal(context, true, error);
  if (game->wire_frame) {
    const qa_q2_wire_origin *old = context->actor->wire_lifetime.origins + ((game->wire_frame - 1) & 7u);
    if (old->present && old->source_frame == game->wire_frame - 1 &&
        qa_vec_length(qa_vec_sub(context->body.origin, old->origin)) == 0)
      return is_repair ? q2m_set_move(context, Q2M_MOVE_fixbot_move_stand, false, error)
                       : fixbot_finish_goal(context, true, error);
  }
  return true;
}

static bool fixbot_vertical(q2m_context *context, bool approach, qa_error *error) {
  qa_actor_id goal = context->monster->goal;
  qa_body_state body;
  if (!q2_actor_live(context->game, goal)) return true;
  if (!qa_world_body_read(context->game->services.world, goal, &body, error))
    return !q2_actor_live(context->game, goal);
  qa_vec3 difference = qa_vec_sub(body.origin, context->body.origin);
  context->monster->ideal_yaw = q2m_vector_angles(difference).y;
  if (!q2m_change_yaw(context, error)) return false;
  if (!q2m_alive(context)) return true;
  int frame = context->monster->frame;
  if ((approach ? qa_vec_length(difference) < 32 : frame == 88 || frame == 120) &&
      !fixbot_finish_goal(context, true, error)) return false;
  if (!q2m_alive(context) || approach) return true;
  qa_vec3 angles = context->body.angles, forward;
  angles.x += 90;
  qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
  q2m_fire_spec shot = {.kind = Q2M_ATTACK_SHOTGUN, .flash = -1,
    .start = context->body.origin, .direction = forward, .damage = 2, .kick = 1,
    .horizontal_spread = (float)(1000 + frame - 105),
    .vertical_spread = (float)(500 + frame - 105), .pellets = 10, .direct_mod = 37};
  return q2m_fire(context, &shot, error);
}

static bool fixbot_weld_state(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *m = context->monster;
  if (m->frame == 197) return q2m_set_move(context, Q2M_MOVE_fixbot_move_weld, false, error);
  if (m->goal.registry && m->frame == 204) {
    qa_combat_state combat;
    if (!qa_combat_read(context->game->services.combat, m->goal, &combat, error))
      return !q2_actor_live(context->game, m->goal);
    if (!q2m_alive(context)) return true;
    if (context->game->options.edition == QA_Q2_RERELEASE ? combat.health <= 0 : combat.health < 0) {
      q2_actor *enemy = q2_actor_get(context->game, m->enemy, false, NULL);
      if (enemy && enemy->entity) enemy->entity->owner = (qa_actor_id){0};
      return q2m_set_move(context, Q2M_MOVE_fixbot_move_weld_end, false, error);
    }
    return qa_combat_set_health(context->game->services.combat, m->goal, combat.health - 10, error);
  }
  m->goal = m->enemy = (qa_actor_id){0};
  return q2m_set_move(context, Q2M_MOVE_fixbot_move_stand, false, error);
}

static bool callback_GunnerCmdrFire_0(q2m_context *context, q2m_callback_id callback,
                                      qa_error *error) {
    (void)callback;
    return guncmdr_fire(context, error);
}

static bool callback_GunnerCmdrGrenade_1(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    return guncmdr_grenade(context, error);
}

static bool callback_arachnid_rail_2(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    return arachnid_rail(context, error);
}

static bool callback_gladbGun_3(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return gladb_gun(context, error);
}

static bool callback_actor_fire_4(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return actor_fire(context, error);
}

static bool callback_fixbot_fire_blaster_5(q2m_context *context, q2m_callback_id callback,
                                           qa_error *error) {
    (void)callback;
    return fixbot_blaster(context, error);
}

static bool callback_fixbot_fire_welder_6(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    return fixbot_welder(context, error);
}

static bool callback_fixbot_fire_laser_7(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    return q2m_fixbot_repair(context, error);
}

static bool callback_fixbot_attack_8(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    return q2m_fixbot_attack(context, error);
}

static bool callback_brain_laserbeam_9(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    return q2m_brain_laser_beam(context, error);
}

static bool callback_ShamblerCastLightning_10(q2m_context *context, q2m_callback_id callback,
                                              qa_error *error) {
    (void)callback;
    return shambler_lightning(context, error);
}

static bool callback_sham_swingl9_11(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {

    struct qa_q2_monster *monster = context->monster;
    bool left = (callback == Q2M_CALLBACK_sham_swingl9);
    if (!q2m_run_ai(context, Q2M_AI_CHARGE, left ? 8 : 1, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (!left && !q2m_run_ai(context, Q2M_AI_CHARGE, 10, error))
        return false;
    if (!q2m_alive(context))
        return true;
    if (q2m_random(context->game) < .5f && monster->enemy.registry &&
        q2m_distance(context, monster->enemy) < 80)
        return q2m_set_move(
            context, left ? Q2M_MOVE_shambler_attack_swingr : Q2M_MOVE_shambler_attack_swingl,
            false, error);
    return true;
}

static bool callback_flipper_run_12(q2m_context *context, q2m_callback_id callback,
                                    qa_error *error) {
    (void)callback;
    return q2m_set_move(context, Q2M_MOVE_flipper_move_run, false, error);
}

static bool callback_widow2_attack_beam_13(q2m_context *context, q2m_callback_id callback,
                                           qa_error *error) {
    (void)callback;
    return q2m_set_move(context, Q2M_MOVE_widow2_move_attack_beam, false, error) &&
           (!q2m_alive(context) || q2m_sound(context, Q2_NAME_RESOURCE_WIDOW_BWSTEP1_WAV, 4, 1, error));
}

static bool callback_widow_attack_rail_14(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
        return false;
    if (!q2m_alive(context) || !available)
        return true;
    float angle = context->body.angles.y -
                  q2m_vector_angles(qa_vec_sub(context->body.origin, enemy.origin)).y;
    if (angle < 0)
        angle += 360;
    angle -= 180;
    return q2m_set_move(context,
                        angle < -15  ? Q2M_MOVE_widow_move_attack_rail_l
                        : angle > 15 ? Q2M_MOVE_widow_move_attack_rail_r
                                     : Q2M_MOVE_widow_move_attack_rail,
                        false, error);
}

static bool callback_TurretAim_15(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return turret_aim(context, error);
}

static bool callback_TurretFire_16(q2m_context *context, q2m_callback_id callback,
                                   qa_error *error) {

    return turret_fire(context, (callback == Q2M_CALLBACK_TurretFireBlind), error);
}

static bool callback_loogie_17(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
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
    qa_vec3 start =
        qa_vec_add(q2m_project_offset(context, qa_v3(-18, -.8f, 24)), qa_vec_scale(up, 2));
    qa_vec3 eye = enemy.origin;
    eye.z += height;
    return !q2m_alive(context) || !enemy_alive(context) ||
           q2_fire_actor_loogie(context->game, context->actor->id, start, qa_vec_sub(eye, start),
                                error);
}

static bool callback_WidowBlaster_18(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    return widow_blaster(context, error);
}

static bool callback_Widow2Beam_19(q2m_context *context, q2m_callback_id callback,
                                   qa_error *error) {
    (void)callback;
    return widow2_beam(context, error);
}

static bool callback_Widow2Tongue_20(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    return widow2_tongue(context, error);
}

static bool callback_Widow2TonguePull_21(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    return widow2_pull(context, error);
}

static bool callback_Widow2Crunch_22(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
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
    float kick = monster->frame != 53 ? 0.0f : qa_actor_reference_present(enemy.ground) ? 500.0f : 250.0f;
    bool hit;
    return q2m_hit(context, qa_v3(150, 0, 4), 20.0f + floorf(q2m_random(context->game) * 6.0f),
                   kick, &hit, error);
}

static bool callback_widow_attack_kick_23(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
        return false;
    if (!q2m_alive(context) || !available)
        return true;
    bool hit;
    return q2m_hit(context, qa_v3(100, 0, 4), 50.0f + floorf(q2m_random(context->game) * 6.0f),
                   qa_actor_reference_present(enemy.ground) ? 500.0f : 250.0f, &hit, error);
}

static bool callback_Widow2SaveBeamTarget_24(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    (void)error;
    return widow2_save_beam(context);
}

static bool callback_Widow2BeamTargetRemove_25(q2m_context *context, q2m_callback_id callback,
                                               qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    monster->saved_attack_position = monster->widow_previous_target = qa_v3(0, 0, 0);
    return true;
}

static bool callback_WidowRail_26(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return widow_rail(context, error);
}

static bool callback_WidowSaveLoc_27(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
        return false;
    if (q2m_alive(context) && available)
        monster->saved_attack_position = qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height));
    return true;
}

static bool callback_brain_duck_down_28(q2m_context *context, q2m_callback_id callback,
                                        qa_error *error) {

    return duck_action(context, callback, error);
}

static bool callback_berserk_jump2_now_29(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {

    return jump_action(context, callback, error);
}

static bool callback_berserk_shrink_30(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    return shrink(context, error);
}

static bool callback_Widow2SaveDisruptLoc_31(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    (void)error;
    return save_widow_disrupt_location(context);
}

static bool callback_WidowDisrupt_32(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    return q2m_widow_disrupt(context, error);
}

static bool callback_ShamblerSaveLoc_33(q2m_context *context, q2m_callback_id callback,
                                        qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
        return false;
    if (!q2m_alive(context) || !available)
        return true;
    monster->saved_attack_position = qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height));
    monster->next_frame = 73;
    return q2m_sound(context, Q2_NAME_RESOURCE_SHAMBLER_SBOOM_WAV, 1, 1, error) &&
           (!q2m_alive(context) || q2m_shambler_lightning(context, false, error));
}

static bool callback_shambler_lightning_update_34(q2m_context *context, q2m_callback_id callback,
                                                  qa_error *error) {

    return q2m_shambler_lightning(context, (callback == Q2M_CALLBACK_shambler_windup), error);
}

static bool callback_gekk_face_35(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return q2m_face_enemy(context, error);
}

static bool callback_monster_done_dodge_36(q2m_context *context, q2m_callback_id callback,
                                           qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    monster->dodging = false;
    if (monster->attack_state == Q2M_SLIDING && context->game->options.edition == QA_Q2_RERELEASE)
        monster->attack_state = Q2M_STRAIGHT;
    return true;
}

static bool callback_monster_check_prone_37(q2m_context *context, q2m_callback_id callback,
                                            qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if (!enemy_alive(context))
        monster->next_frame = monster->frame + 1;
    return true;
}

static bool callback_brain_chest_open_38(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (context->game->options.edition == QA_Q2_RERELEASE)
        monster->count = 0;
    else
        monster->spawnflags &= ~UINT32_C(65536);
    context->combat.armor.powered.kind = QA_POWER_NONE;
    qa_q2_combat_power_armor_source(context->game, &context->combat.armor.powered);
    if (!qa_combat_set_armor(context->game->services.combat, context->actor->id,
                             &context->combat.armor, error))
        return false;
    return !q2m_alive(context) || q2m_sound(context, Q2_NAME_RESOURCE_BRAIN_BRNATCK1_WAV, 4, 1, error);
}

static bool callback_brain_chest_closed_39(q2m_context *context, q2m_callback_id callback,
                                           qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    context->combat.armor.powered.kind = QA_POWER_SCREEN;
    qa_q2_combat_power_armor_source(context->game, &context->combat.armor.powered);
    if (!qa_combat_set_armor(context->game->services.combat, context->actor->id,
                             &context->combat.armor, error))
        return false;
    if (!q2m_alive(context))
        return true;
    bool follow = context->game->options.edition == QA_Q2_RERELEASE
                      ? monster->count != 0
                      : (monster->spawnflags & UINT32_C(65536)) != 0;
    if (!follow)
        return true;
    if (context->game->options.edition == QA_Q2_RERELEASE)
        monster->count = 0;
    else
        monster->spawnflags &= ~UINT32_C(65536);
    return q2m_set_move(context, Q2M_MOVE_brain_move_attack1, false, error);
}

static bool callback_change_to_roam_40(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    return fixbot_roam(context, error);
}

static bool callback_roam_goal_41(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return fixbot_goal_create(context, 0, error);
}

static bool callback_fly_vertical_42(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {

    return fixbot_vertical(context, (callback == Q2M_CALLBACK_fly_vertical2), error);
}

static bool callback_berserk_high_gravity_43(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    (void)error;
    context->actor->physics.gravity_scale = 2.0f;
    return true;
}

static bool callback_gekk_check_underwater_44(q2m_context *context, q2m_callback_id callback,
                                              qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    q2m_move_id move = context->actor->physics.water_level > 1 ? Q2M_MOVE_gekk_move_swim_start
                                                               : monster->definition->run_move;
    bool found;
    return set_existing_move(context, move, false, &found, error);
}

static bool callback_gekk_stop_skid_45(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    context->body.velocity = qa_v3(0, 0, 0);
    return q2m_write_body(context, true, error);
}

static bool callback_MakronToss_46(q2m_context *context, q2m_callback_id callback,
                                   qa_error *error) {
    (void)callback;
    return toss_makron(context, error);
}

static bool callback_Widow2Toss_47(q2m_context *context, q2m_callback_id callback,
                                   qa_error *error) {
    (void)callback;
    context->body.velocity.z += 200.0f;
    return q2m_write_body(context, true, error);
}

static bool callback_use_scanner_48(q2m_context *context, q2m_callback_id callback,
                                    qa_error *error) {
    (void)callback;
    return fixbot_scanner(context, error);
}

static bool callback_weldstate_49(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return fixbot_weld_state(context, error);
}

static bool callback_CarrierCoopCheck_50(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    return carrier_coop_check(context, error);
}

static bool callback_gunner_blind_check_51(q2m_context *context, q2m_callback_id callback,
                                           qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (monster->attack_state == Q2M_BLIND) {
        qa_vec3 direction = qa_vec_sub(monster->blind_fire_target, context->body.origin);
        monster->ideal_yaw = atan2f(direction.y, direction.x) * 57.29577951308232f;
        return q2m_change_yaw(context, error);
    }
    return true;
}

static bool callback_parasite_drain_attack_52(q2m_context *context, q2m_callback_id callback,
                                              qa_error *error) {

    return q2m_melee(context, 256.0f,
                     (callback == Q2M_CALLBACK_parasite_drain_attack) ? 5.0f : 2.0f, 0.0f, error);
}

static bool callback_soldier_walk1_random_53(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if (q2m_random(context->game) > 0.1f)
        monster->next_frame = monster->move->first_frame;
    return true;
}

static bool callback_soldier_cock_54(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    monster->cocked = true;
    return q2m_sound(context, Q2_NAME_RESOURCE_INFANTRY_INFATCK3_WAV, 1, monster->frame == 197 ? 2.0f : 1.0f,
                     error);
}

static bool callback_soldierh_hyper_laser_sound_start_55(q2m_context *context,
                                                         q2m_callback_id callback,
                                                         qa_error *error) {
    (void)callback;
    return soldier_laser_sound(context, true, error);
}

static bool callback_soldierh_hyper_laser_sound_end_56(q2m_context *context,
                                                       q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return soldier_laser_sound(context, false, error);
}

static bool callback_soldierh_hyper_sound_57(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    return monster->definition->species == Q2M_SOLDIER_HYPER
               ? q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBL1A_WAV, 0, 1.0f, error)
               : true;
}

static bool callback_flyer_kamikaze_check_58(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (!enemy_alive(context) || q2m_distance(context, monster->enemy) < 90.0f)
        return q2m_kamikaze(context, error);
    monster->goal = monster->enemy;
    return true;
}

static bool callback_gladbGun_check_59(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    return context->game->options.skill == 3 ? gladb_gun(context, error) : true;
}

static bool callback_mutant_check_refire_60(q2m_context *context, q2m_callback_id callback,
                                            qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if (enemy_alive(context) &&
        ((context->game->options.skill == 3 && q2m_random(context->game) < 0.5f) ||
         q2m_distance(context, monster->enemy) < 80.0f))
        monster->next_frame = 8;
    return true;
}

static bool callback_gekk_check_refire_61(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (!enemy_alive(context) ||
        q2m_random(context->game) >= (float)context->game->options.skill * 0.1f ||
        q2m_distance(context, monster->enemy) >= 80.0f)
        return true;
    q2m_move_id move =
        monster->frame == 53 ? Q2M_MOVE_gekk_move_attack2 : Q2M_MOVE_gekk_move_attack1;
    bool found;
    return set_existing_move(context, move, false, &found, error);
}

static bool callback_insane_checkdown_62(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (!(monster->spawnflags & 32u) && q2m_random(context->game) < .3f)
        return q2m_set_move(context,
                            q2m_random(context->game) < .5f ? Q2M_MOVE_insane_move_uptodown
                                                            : Q2M_MOVE_insane_move_jumpdown,
                            false, error);
    return true;
}

static bool callback_insane_checkup_63(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if ((monster->spawnflags & 20u) != 20u && q2m_random(context->game) < .5f)
        return q2m_set_move(context, Q2M_MOVE_insane_move_downtoup, false, error);
    return true;
}

static bool callback_reloogie_64(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    bool found;
    if (q2m_random(context->game) > 0.8f && context->combat.health < monster->base_health)
        return set_existing_move(context, Q2M_MOVE_gekk_move_idle2, false, &found, error);
    float distance = q2m_distance(context, monster->enemy);
    if (enemy_alive(context) && q2m_random(context->game) > 0.7f && distance >= 80.0f &&
        distance < 500.0f)
        return set_existing_move(context, Q2M_MOVE_gekk_move_spit, false, &found, error);
    return true;
}

static bool callback_widow_step_65(q2m_context *context, q2m_callback_id callback,
                                   qa_error *error) {
    (void)callback;
    return q2m_sound(context, Q2_NAME_RESOURCE_WIDOW_BWSTEP3_WAV, 4, 1.0f, error);
}

static bool callback_widow_stepshoot_66(q2m_context *context, q2m_callback_id callback,
                                        qa_error *error) {
    (void)callback;
    return q2m_sound(context, Q2_NAME_RESOURCE_WIDOW_BWSTEP3_WAV, 4, 1.0f, error) &&
           (!q2m_alive(context) || widow_blaster(context, error));
}

static bool callback_arachnid_charge_rail_67(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    qa_body_state enemy;
    qa_builtin_actor_traits traits;
    bool available;
    if (!target_body(context, &enemy, &traits, &available))
        return false;
    if (!q2m_alive(context) || !available)
        return true;
    monster->saved_attack_position = qa_vec_add(enemy.origin, qa_v3(0, 0, traits.view_height));
    return q2m_sound(context, Q2_NAME_RESOURCE_GLADIATOR_RAILGUN_WAV, 1, 1.0f, error);
}

static bool callback_berserk_run_attack_speed_68(q2m_context *context, q2m_callback_id callback,
                                                 qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if (enemy_alive(context) && q2m_distance(context, monster->enemy) < 80.0f) {
        monster->next_frame = monster->frame + 6;
        monster->dodging = false;
    }
    return true;
}

static bool callback_arachnid_melee_charge_69(q2m_context *context, q2m_callback_id callback,
                                              qa_error *error) {
    (void)callback;
    return q2m_sound(context, Q2_NAME_RESOURCE_GLADIATOR_MELEE3_WAV, 1, 1.0f, error);
}

static bool callback_flipper_preattack_70(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    return q2m_sound(context, Q2_NAME_RESOURCE_FLIPPER_FLPATCK1_WAV, 1, 1.0f, error);
}

static bool callback_gekk_preattack_71(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)context;
    (void)callback;
    (void)error;
    return true;
}

static bool callback_mutant_idle_loop_72(q2m_context *context, q2m_callback_id callback,
                                         qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if (q2m_random(context->game) < 0.75f)
        monster->next_frame = 116;
    return true;
}

static bool callback_gekk_idle_loop_73(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if (q2m_random(context->game) > 0.75f && context->combat.health < monster->base_health)
        monster->next_frame = 297;
    return true;
}

static bool callback_shambler_maybe_idle_74(q2m_context *context, q2m_callback_id callback,
                                            qa_error *error) {
    (void)callback;
    return q2m_random(context->game) > 0.8f
               ? q2m_sound(context, Q2_NAME_RESOURCE_SHAMBLER_SIDLE_WAV, 2, 2.0f, error)
               : true;
}

static bool callback_berserk_fidget_75(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (monster->stand_ground || monster->enemy.registry != 0 || q2m_random(context->game) > 0.15f)
        return true;
    return q2m_set_move(context, Q2M_MOVE_berserk_move_stand_fidget, false, error) &&
           (!q2m_alive(context) || q2m_sound(context, Q2_NAME_RESOURCE_BERSERK_BERIDLE1_WAV, 1, 2.0f, error));
}

static bool callback_chick_fidget_76(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (!monster->stand_ground && q2m_random(context->game) <= 0.3f)
        return q2m_set_move(context, Q2M_MOVE_chick_move_fidget, false, error);
    return true;
}

static bool callback_guncmdr_fidget_77(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {

    struct qa_q2_monster *monster = context->monster;
    bool commander = (callback == Q2M_CALLBACK_guncmdr_fidget);
    if (!monster->stand_ground && (!commander || monster->enemy.registry == 0) &&
        q2m_random(context->game) <= 0.05f)
        return q2m_set_move(context,
                            commander ? Q2M_MOVE_guncmdr_move_fidget : Q2M_MOVE_gunner_move_fidget,
                            false, error);
    return true;
}

static bool callback_guardian_atk1_charge_78(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    if (!q2m_weapon_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBL1A_WAV, error))
        return false;
    return !q2m_alive(context) || q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_HYPRBU1A_WAV, 1, 1.0f, error);
}

static bool callback_guardian_fire_blaster_79(q2m_context *context, q2m_callback_id callback,
                                              qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    qa_body_state enemy;
    if (!qa_world_body_read(context->game->services.world, monster->enemy, &enemy, error))
        return false;
    if (!q2m_alive(context))
        return true;
    qa_vec3 start;
    float height;
    if (!q2m_project_flash(context, 227, &start, error))
        return false;
    if (!enemy_view_height(context, &height))
        return true;
    qa_vec3 target = enemy.origin;
    target.z += height;
    float low = nextafterf(-1.0f, 0.0f), high = nextafterf(1.0f, 0.0f);
    target.x += fminf(high, q2_rerelease_float(context->game, low, 1.0f)) * 5.0f;
    target.y += fminf(high, q2_rerelease_float(context->game, low, 1.0f)) * 5.0f;
    target.z += fminf(high, q2_rerelease_float(context->game, low, 1.0f)) * 5.0f;
    q2m_fire_spec spec = q2m_fire_default(context, Q2M_ATTACK_BLASTER, 2.0f, 227, start,
                                          qa_vec_normalize(qa_vec_sub(target, start)));
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

static bool callback_guardian_laser_fire_80(q2m_context *context, q2m_callback_id callback,
                                            qa_error *error) {
    (void)callback;
    return q2m_sound(context, Q2_NAME_RESOURCE_WEAPONS_LASER2_WAV, 1, 1.0f, error) &&
           (!q2m_alive(context) || q2m_guardian_beam(context, error));
}

static bool callback_infantry_fire_prep_81(q2m_context *context, q2m_callback_id callback,
                                           qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    monster->fire_ns =
        q2m_after(context->game->now_ns,
                  ((qa_builtin_random_integer(&context->game->random) & 15u) + 4u) * .1);
    monster->pause_ns = monster->fire_ns;
    return true;
}

static bool callback_soldier_start_charge_82(q2m_context *context, q2m_callback_id callback,
                                             qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    monster->charging = true;
    return true;
}

static bool callback_GunnerCmdrCounter_83(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    ++monster->count;
    return true;
}

static bool callback_guncmdr_pain5_to_death1_84(q2m_context *context, q2m_callback_id callback,
                                                qa_error *error) {

    if (context->combat.health < 0 &&
        ((callback != (Q2M_CALLBACK_guncmdr_pain5_to_death2)) || q2m_random(context->game) < .5f))
        return q2m_set_move(
            context,
            (callback == Q2M_CALLBACK_guncmdr_pain5_to_death1)   ? Q2M_MOVE_guncmdr_move_death1
            : (callback == Q2M_CALLBACK_guncmdr_pain5_to_death2) ? Q2M_MOVE_guncmdr_move_death2
                                                                 : Q2M_MOVE_guncmdr_move_death6,
            false, error);
    return true;
}

static bool callback_flyer_nextmove_85(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    bool found;
    return set_existing_move(context,
                             q2m_random(context->game) < 0.5f ? Q2M_MOVE_flyer_move_rollleft
                                                              : Q2M_MOVE_flyer_move_rollright,
                             false, &found, error);
}

static bool callback_gekk_gibfest_86(q2m_context *context, q2m_callback_id callback,
                                     qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    return context->combat.health <= monster->gib_health ? q2m_die(context, error) : true;
}

static bool callback_BossExplode_87(q2m_context *context, q2m_callback_id callback,
                                    qa_error *error) {
    (void)callback;
    return q2m_start_boss_explosion(context, error);
}

static bool callback_hover_dying_88(q2m_context *context, q2m_callback_id callback,
                                    qa_error *error) {
    (void)callback;
    return q2m_hover_dying(context, error);
}

static bool callback_jorg_death_hit_89(q2m_context *context, q2m_callback_id callback,
                                       qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    ++monster->count;
    return q2m_emit(context, QA_BUILTIN_EXPLOSION, Q2_NAME_RESOURCE_Q2_EXPLOSION1, monster->count,
                    context->body.origin, context->body.origin, 1.0f, error);
}

static bool callback_BossLoop_90(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    if ((monster->spawnflags & 16u) == 0)
        return true;
    if (monster->count != 0)
        --monster->count;
    else
        monster->spawnflags &= ~16u;
    monster->next_frame = monster->move->first_frame + 18;
    return true;
}

static bool callback_widow2_start_searching_91(q2m_context *context, q2m_callback_id callback,
                                               qa_error *error) {
    (void)callback;
    (void)error;
    struct qa_q2_monster *monster = context->monster;
    monster->count = 0;
    return true;
}

static bool callback_widow2_keep_searching_92(q2m_context *context, q2m_callback_id callback,
                                              qa_error *error) {
    (void)callback;
    struct qa_q2_monster *monster = context->monster;
    if (monster->count > 2)
        return q2m_set_move(context, Q2M_MOVE_widow2_move_really_dead, false, error);
    if (!q2m_set_move(context, Q2M_MOVE_widow2_move_dead, false, error))
        return false;
    monster->frame = 104;
    ++monster->count;
    return true;
}

static bool callback_widow2_finaldeath_93(q2m_context *context, q2m_callback_id callback,
                                          qa_error *error) {
    (void)callback;
    return q2m_corpse(context, error);
}

static bool callback_gekk_search_94(q2m_context *context, q2m_callback_id callback,
                                    qa_error *error) {
    (void)context;
    (void)callback;
    (void)error;
    return true;
}

static bool callback_TreadSound(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    (void)callback;
    return q2m_sound(context, Q2_NAME_RESOURCE_BOSSTANK_BTKENGN1_WAV,
                     context->game->options.edition == QA_Q2_RERELEASE ? 4 : 2, 1, error);
}

static bool callback_stalker_idle_noise(q2m_context *context, q2m_callback_id callback,
                                        qa_error *error) {
    (void)callback;
    return q2m_sound_volume(context, Q2_NAME_RESOURCE_STALKER_IDLE_WAV,
                            context->game->options.edition == QA_Q2_RERELEASE ? 2 : 1, 2, .5f,
                            error);
}

static bool callback_unsupported(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s references unsupported monster callback %s",
                 context->monster->definition->classname, q2m_callbacks[callback].name);
    return false;
}

static bool callback_sequence_1(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!carrier_spawn_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_2(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!conditional_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_3(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!conditional_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_4(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_5(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!infantry_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_6(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_7(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_gekk_face_35(context, callback, error);
}

static bool callback_sequence_8(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_gekk_search_94(context, callback, error);
}

static bool callback_sequence_9(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_widow2_finaldeath_93(context, callback, error);
}

static bool callback_sequence_10(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_11(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_berserk_high_gravity_43(context, callback, error);
}

static bool callback_sequence_12(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_brain_chest_closed_39(context, callback, error);
}

static bool callback_sequence_13(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_brain_chest_open_38(context, callback, error);
}

static bool callback_sequence_14(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_gekk_search_94(context, callback, error);
}

static bool callback_sequence_15(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_parasite_drain_attack_52(context, callback, error);
}

static bool callback_sequence_16(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!heavy_weapon_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_17(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!infantry_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_18(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!infantry_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_infantry_fire_prep_81(context, callback, error);
}

static bool callback_sequence_19(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_medic_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_20(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_medic_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_21(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_parasite_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_22(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_parasite_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!end_transition(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_23(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_parasite_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!foundational_species_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_24(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_species_melee(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_25(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_species_melee(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    if (!infantry_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_26(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_stalker_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_27(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_summon_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_28(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!q2m_widow_death_action(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_29(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

static bool callback_sequence_30(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_gunner_blind_check_51(context, callback, error);
}

static bool callback_sequence_31(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_soldier_cock_54(context, callback, error);
}

static bool callback_sequence_32(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_soldier_start_charge_82(context, callback, error);
}

static bool callback_sequence_33(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_soldier_walk1_random_53(context, callback, error);
}

static bool callback_sequence_34(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_soldierh_hyper_laser_sound_end_56(context, callback, error);
}

static bool callback_sequence_35(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_soldierh_hyper_laser_sound_start_55(context, callback, error);
}

static bool callback_sequence_36(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!soldier_callbacks(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_soldierh_hyper_sound_57(context, callback, error);
}

static bool callback_sequence_37(q2m_context *context, q2m_callback_id callback, qa_error *error) {
    bool handled = false;
    if (!source_sound_callback(context, callback, &handled, error))
        return false;
    if (handled)
        return true;
    return callback_unsupported(context, callback, error);
}

const q2m_callback q2m_callbacks[Q2M_CALLBACK_COUNT] = {

    [Q2M_CALLBACK_Boss2HyperBlaster] = {callback_sequence_16, Q2M_CALLBACK_Boss2HyperBlaster,
                                        "Boss2HyperBlaster", 0},

    [Q2M_CALLBACK_Boss2MachineGun] = {callback_sequence_16, Q2M_CALLBACK_Boss2MachineGun,
                                      "Boss2MachineGun", 0},

    [Q2M_CALLBACK_Boss2PredictiveRocket] = {callback_sequence_16,
                                            Q2M_CALLBACK_Boss2PredictiveRocket,
                                            "Boss2PredictiveRocket", 0},

    [Q2M_CALLBACK_Boss2Rocket] = {callback_sequence_16, Q2M_CALLBACK_Boss2Rocket, "Boss2Rocket", 0},

    [Q2M_CALLBACK_Boss2Rocket64] = {callback_sequence_16, Q2M_CALLBACK_Boss2Rocket64,
                                    "Boss2Rocket64", 0},

    [Q2M_CALLBACK_BossExplode] = {callback_BossExplode_87, Q2M_CALLBACK_BossExplode, "BossExplode",
                                  0},

    [Q2M_CALLBACK_BossExplode2] = {callback_BossExplode_87, Q2M_CALLBACK_BossExplode2,
                                   "BossExplode2", 0},

    [Q2M_CALLBACK_BossLoop] = {callback_BossLoop_90, Q2M_CALLBACK_BossLoop, "BossLoop", 0},

    [Q2M_CALLBACK_CarrierCoopCheck] = {callback_CarrierCoopCheck_50, Q2M_CALLBACK_CarrierCoopCheck,
                                       "CarrierCoopCheck", 0},

    [Q2M_CALLBACK_CarrierGrenade] = {callback_sequence_16, Q2M_CALLBACK_CarrierGrenade,
                                     "CarrierGrenade", 0},

    [Q2M_CALLBACK_CarrierMachineGun] = {callback_sequence_16, Q2M_CALLBACK_CarrierMachineGun,
                                        "CarrierMachineGun", 0},

    [Q2M_CALLBACK_CarrierMachineGunHold] = {callback_sequence_16,
                                            Q2M_CALLBACK_CarrierMachineGunHold,
                                            "CarrierMachineGunHold", 0},

    [Q2M_CALLBACK_CarrierRail] = {callback_sequence_16, Q2M_CALLBACK_CarrierRail, "CarrierRail", 0},

    [Q2M_CALLBACK_CarrierRocket] = {callback_sequence_16, Q2M_CALLBACK_CarrierRocket,
                                    "CarrierRocket", 0},

    [Q2M_CALLBACK_CarrierSaveLoc] = {callback_sequence_16, Q2M_CALLBACK_CarrierSaveLoc,
                                     "CarrierSaveLoc", 0},

    [Q2M_CALLBACK_ChickMoan] = {callback_sequence_10, Q2M_CALLBACK_ChickMoan, "ChickMoan", 0},

    [Q2M_CALLBACK_ChickReload] = {callback_sequence_10, Q2M_CALLBACK_ChickReload, "ChickReload", 0},

    [Q2M_CALLBACK_ChickRocket] = {callback_sequence_10, Q2M_CALLBACK_ChickRocket, "ChickRocket", 0},

    [Q2M_CALLBACK_ChickSlash] = {callback_sequence_24, Q2M_CALLBACK_ChickSlash, "ChickSlash", 0},

    [Q2M_CALLBACK_Chick_PreAttack1] = {callback_sequence_10, Q2M_CALLBACK_Chick_PreAttack1,
                                       "Chick_PreAttack1", 0},

    [Q2M_CALLBACK_GaldiatorMelee] = {callback_sequence_24, Q2M_CALLBACK_GaldiatorMelee,
                                     "GaldiatorMelee", 0},

    [Q2M_CALLBACK_GladbMelee] = {callback_sequence_24, Q2M_CALLBACK_GladbMelee, "GladbMelee", 0},

    [Q2M_CALLBACK_GladiatorGun] = {callback_sequence_10, Q2M_CALLBACK_GladiatorGun, "GladiatorGun",
                                   0},

    [Q2M_CALLBACK_GladiatorMelee] = {callback_sequence_24, Q2M_CALLBACK_GladiatorMelee,
                                     "GladiatorMelee", 0},

    [Q2M_CALLBACK_GunnerCmdrCounter] = {callback_GunnerCmdrCounter_83,
                                        Q2M_CALLBACK_GunnerCmdrCounter, "GunnerCmdrCounter", 0},

    [Q2M_CALLBACK_GunnerCmdrFire] = {callback_GunnerCmdrFire_0, Q2M_CALLBACK_GunnerCmdrFire,
                                     "GunnerCmdrFire", 0},

    [Q2M_CALLBACK_GunnerCmdrGrenade] = {callback_GunnerCmdrGrenade_1,
                                        Q2M_CALLBACK_GunnerCmdrGrenade, "GunnerCmdrGrenade", 0},

    [Q2M_CALLBACK_GunnerFire] = {callback_sequence_10, Q2M_CALLBACK_GunnerFire, "GunnerFire", 0},

    [Q2M_CALLBACK_GunnerGrenade] = {callback_sequence_10, Q2M_CALLBACK_GunnerGrenade,
                                    "GunnerGrenade", 0},

    [Q2M_CALLBACK_InfantryMachineGun] = {callback_sequence_17, Q2M_CALLBACK_InfantryMachineGun,
                                         "InfantryMachineGun", 0},

    [Q2M_CALLBACK_MakronHyperblaster] = {callback_sequence_16, Q2M_CALLBACK_MakronHyperblaster,
                                         "MakronHyperblaster", 0},

    [Q2M_CALLBACK_MakronRailgun] = {callback_sequence_16, Q2M_CALLBACK_MakronRailgun,
                                    "MakronRailgun", 0},

    [Q2M_CALLBACK_MakronSaveloc] = {callback_sequence_16, Q2M_CALLBACK_MakronSaveloc,
                                    "MakronSaveloc", 0},

    [Q2M_CALLBACK_MakronToss] = {callback_MakronToss_46, Q2M_CALLBACK_MakronToss, "MakronToss", 0},

    [Q2M_CALLBACK_ShamClaw] = {callback_sequence_24, Q2M_CALLBACK_ShamClaw, "ShamClaw", 0},

    [Q2M_CALLBACK_ShamblerCastLightning] = {callback_ShamblerCastLightning_10,
                                            Q2M_CALLBACK_ShamblerCastLightning,
                                            "ShamblerCastLightning", 0},

    [Q2M_CALLBACK_ShamblerSaveLoc] = {callback_ShamblerSaveLoc_33, Q2M_CALLBACK_ShamblerSaveLoc,
                                      "ShamblerSaveLoc", 0},

    [Q2M_CALLBACK_TankBlaster] = {callback_sequence_16, Q2M_CALLBACK_TankBlaster, "TankBlaster", 0},

    [Q2M_CALLBACK_TankMachineGun] = {callback_sequence_16, Q2M_CALLBACK_TankMachineGun,
                                     "TankMachineGun", 0},

    [Q2M_CALLBACK_TankRocket] = {callback_sequence_16, Q2M_CALLBACK_TankRocket, "TankRocket", 0},

    [Q2M_CALLBACK_TankStrike] = {callback_sequence_37, Q2M_CALLBACK_TankStrike, "TankStrike", 0},

    [Q2M_CALLBACK_TreadSound] = {callback_TreadSound, Q2M_CALLBACK_TreadSound, "TreadSound", 0},

    [Q2M_CALLBACK_TreadSound2] = {callback_sequence_37, Q2M_CALLBACK_TreadSound2, "TreadSound2", 0},

    [Q2M_CALLBACK_TurretAim] = {callback_TurretAim_15, Q2M_CALLBACK_TurretAim, "TurretAim", 0},

    [Q2M_CALLBACK_TurretFire] = {callback_TurretFire_16, Q2M_CALLBACK_TurretFire, "TurretFire", 0},

    [Q2M_CALLBACK_TurretFireBlind] = {callback_TurretFire_16, Q2M_CALLBACK_TurretFireBlind,
                                      "TurretFireBlind", 0},

    [Q2M_CALLBACK_Widow2Beam] = {callback_Widow2Beam_19, Q2M_CALLBACK_Widow2Beam, "Widow2Beam", 0},

    [Q2M_CALLBACK_Widow2BeamTargetRemove] = {callback_Widow2BeamTargetRemove_25,
                                             Q2M_CALLBACK_Widow2BeamTargetRemove,
                                             "Widow2BeamTargetRemove", 0},

    [Q2M_CALLBACK_Widow2Crunch] = {callback_Widow2Crunch_22, Q2M_CALLBACK_Widow2Crunch,
                                   "Widow2Crunch", 0},

    [Q2M_CALLBACK_Widow2SaveBeamTarget] = {callback_Widow2SaveBeamTarget_24,
                                           Q2M_CALLBACK_Widow2SaveBeamTarget,
                                           "Widow2SaveBeamTarget", 0},

    [Q2M_CALLBACK_Widow2SaveDisruptLoc] = {callback_Widow2SaveDisruptLoc_31,
                                           Q2M_CALLBACK_Widow2SaveDisruptLoc,
                                           "Widow2SaveDisruptLoc", 0},

    [Q2M_CALLBACK_Widow2StartSweep] = {callback_Widow2SaveBeamTarget_24,
                                       Q2M_CALLBACK_Widow2StartSweep, "Widow2StartSweep", 0},

    [Q2M_CALLBACK_Widow2Tongue] = {callback_Widow2Tongue_20, Q2M_CALLBACK_Widow2Tongue,
                                   "Widow2Tongue", 0},

    [Q2M_CALLBACK_Widow2TonguePull] = {callback_Widow2TonguePull_21, Q2M_CALLBACK_Widow2TonguePull,
                                       "Widow2TonguePull", 0},

    [Q2M_CALLBACK_Widow2Toss] = {callback_Widow2Toss_47, Q2M_CALLBACK_Widow2Toss, "Widow2Toss", 0},

    [Q2M_CALLBACK_WidowBlaster] = {callback_WidowBlaster_18, Q2M_CALLBACK_WidowBlaster,
                                   "WidowBlaster", 0},

    [Q2M_CALLBACK_WidowDisrupt] = {callback_WidowDisrupt_32, Q2M_CALLBACK_WidowDisrupt,
                                   "WidowDisrupt", 0},

    [Q2M_CALLBACK_WidowExplode] = {callback_sequence_28, Q2M_CALLBACK_WidowExplode, "WidowExplode",
                                   0},

    [Q2M_CALLBACK_WidowExplosion1] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion1,
                                      "WidowExplosion1", 0},

    [Q2M_CALLBACK_WidowExplosion2] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion2,
                                      "WidowExplosion2", 0},

    [Q2M_CALLBACK_WidowExplosion3] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion3,
                                      "WidowExplosion3", 0},

    [Q2M_CALLBACK_WidowExplosion4] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion4,
                                      "WidowExplosion4", 0},

    [Q2M_CALLBACK_WidowExplosion5] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion5,
                                      "WidowExplosion5", 0},

    [Q2M_CALLBACK_WidowExplosion6] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion6,
                                      "WidowExplosion6", 0},

    [Q2M_CALLBACK_WidowExplosion7] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosion7,
                                      "WidowExplosion7", 0},

    [Q2M_CALLBACK_WidowExplosionLeg] = {callback_sequence_28, Q2M_CALLBACK_WidowExplosionLeg,
                                        "WidowExplosionLeg", 0},

    [Q2M_CALLBACK_WidowRail] = {callback_WidowRail_26, Q2M_CALLBACK_WidowRail, "WidowRail", 0},

    [Q2M_CALLBACK_WidowSaveLoc] = {callback_WidowSaveLoc_27, Q2M_CALLBACK_WidowSaveLoc,
                                   "WidowSaveLoc", 0},

    [Q2M_CALLBACK_actor_dead] = {callback_sequence_4, Q2M_CALLBACK_actor_dead, "actor_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_actor_fire] = {callback_actor_fire_4, Q2M_CALLBACK_actor_fire, "actor_fire", 0},

    [Q2M_CALLBACK_actor_run] = {callback_sequence_4, Q2M_CALLBACK_actor_run, "actor_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_actor_stand] = {callback_sequence_4, Q2M_CALLBACK_actor_stand, "actor_stand",
                                  Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_actor_walk] = {callback_sequence_4, Q2M_CALLBACK_actor_walk, "actor_walk",
                                 Q2M_CALLBACK_WALK},

    [Q2M_CALLBACK_arachnid_charge_rail] = {callback_arachnid_charge_rail_67,
                                           Q2M_CALLBACK_arachnid_charge_rail,
                                           "arachnid_charge_rail", 0},

    [Q2M_CALLBACK_arachnid_dead] = {callback_sequence_4, Q2M_CALLBACK_arachnid_dead,
                                    "arachnid_dead", Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_arachnid_footstep] = {callback_sequence_37, Q2M_CALLBACK_arachnid_footstep,
                                        "arachnid_footstep", 0},

    [Q2M_CALLBACK_arachnid_melee_charge] = {callback_arachnid_melee_charge_69,
                                            Q2M_CALLBACK_arachnid_melee_charge,
                                            "arachnid_melee_charge", 0},

    [Q2M_CALLBACK_arachnid_melee_hit] = {callback_sequence_24, Q2M_CALLBACK_arachnid_melee_hit,
                                         "arachnid_melee_hit", 0},

    [Q2M_CALLBACK_arachnid_rail] = {callback_arachnid_rail_2, Q2M_CALLBACK_arachnid_rail,
                                    "arachnid_rail", 0},

    [Q2M_CALLBACK_arachnid_run] = {callback_sequence_4, Q2M_CALLBACK_arachnid_run, "arachnid_run",
                                   Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_berserk_attack_club] = {callback_sequence_24, Q2M_CALLBACK_berserk_attack_club,
                                          "berserk_attack_club", 0},

    [Q2M_CALLBACK_berserk_attack_spike] = {callback_sequence_24, Q2M_CALLBACK_berserk_attack_spike,
                                           "berserk_attack_spike", 0},

    [Q2M_CALLBACK_berserk_check_landing] = {callback_sequence_10,
                                            Q2M_CALLBACK_berserk_check_landing,
                                            "berserk_check_landing", 0},

    [Q2M_CALLBACK_berserk_dead] = {callback_sequence_4, Q2M_CALLBACK_berserk_dead, "berserk_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_berserk_fidget] = {callback_berserk_fidget_75, Q2M_CALLBACK_berserk_fidget,
                                     "berserk_fidget", 0},

    [Q2M_CALLBACK_berserk_high_gravity] = {callback_sequence_11, Q2M_CALLBACK_berserk_high_gravity,
                                           "berserk_high_gravity", 0},

    [Q2M_CALLBACK_berserk_jump2_now] = {callback_berserk_jump2_now_29,
                                        Q2M_CALLBACK_berserk_jump2_now, "berserk_jump2_now", 0},

    [Q2M_CALLBACK_berserk_jump_now] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_berserk_jump_now,
                                       "berserk_jump_now", 0},

    [Q2M_CALLBACK_berserk_jump_takeoff] = {callback_sequence_10, Q2M_CALLBACK_berserk_jump_takeoff,
                                           "berserk_jump_takeoff", 0},

    [Q2M_CALLBACK_berserk_jump_wait_land] = {callback_berserk_jump2_now_29,
                                             Q2M_CALLBACK_berserk_jump_wait_land,
                                             "berserk_jump_wait_land", 0},

    [Q2M_CALLBACK_berserk_run] = {callback_sequence_4, Q2M_CALLBACK_berserk_run, "berserk_run",
                                  Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_berserk_run_attack_speed] = {callback_berserk_run_attack_speed_68,
                                               Q2M_CALLBACK_berserk_run_attack_speed,
                                               "berserk_run_attack_speed", 0},

    [Q2M_CALLBACK_berserk_run_swing] = {callback_sequence_10, Q2M_CALLBACK_berserk_run_swing,
                                        "berserk_run_swing", 0},

    [Q2M_CALLBACK_berserk_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_berserk_shrink,
                                     "berserk_shrink", 0},

    [Q2M_CALLBACK_berserk_stand] = {callback_sequence_4, Q2M_CALLBACK_berserk_stand,
                                    "berserk_stand", Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_berserk_strike] = {callback_sequence_10, Q2M_CALLBACK_berserk_strike,
                                     "berserk_strike", 0},

    [Q2M_CALLBACK_berserk_swing] = {callback_sequence_10, Q2M_CALLBACK_berserk_swing,
                                    "berserk_swing", 0},

    [Q2M_CALLBACK_boss2_attack_mg] = {callback_sequence_4, Q2M_CALLBACK_boss2_attack_mg,
                                      "boss2_attack_mg", 0},

    [Q2M_CALLBACK_boss2_dead] = {callback_sequence_4, Q2M_CALLBACK_boss2_dead, "boss2_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_boss2_firebullet_left] = {callback_sequence_16,
                                            Q2M_CALLBACK_boss2_firebullet_left,
                                            "boss2_firebullet_left", 0},

    [Q2M_CALLBACK_boss2_firebullet_right] = {callback_sequence_16,
                                             Q2M_CALLBACK_boss2_firebullet_right,
                                             "boss2_firebullet_right", 0},

    [Q2M_CALLBACK_boss2_reattack_mg] = {callback_sequence_2, Q2M_CALLBACK_boss2_reattack_mg,
                                        "boss2_reattack_mg", 0},

    [Q2M_CALLBACK_boss2_run] = {callback_sequence_4, Q2M_CALLBACK_boss2_run, "boss2_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_boss2_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_boss2_shrink,
                                   "boss2_shrink", 0},

    [Q2M_CALLBACK_boss5MachineGun] = {callback_sequence_16, Q2M_CALLBACK_boss5MachineGun,
                                      "boss5MachineGun", 0},

    [Q2M_CALLBACK_boss5Rocket] = {callback_sequence_16, Q2M_CALLBACK_boss5Rocket, "boss5Rocket", 0},

    [Q2M_CALLBACK_boss5_dead] = {callback_sequence_4, Q2M_CALLBACK_boss5_dead, "boss5_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_boss5_reattack1] = {callback_sequence_2, Q2M_CALLBACK_boss5_reattack1,
                                      "boss5_reattack1", Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_boss5_run] = {callback_sequence_4, Q2M_CALLBACK_boss5_run, "boss5_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_brain_chest_closed] = {callback_sequence_12, Q2M_CALLBACK_brain_chest_closed,
                                         "brain_chest_closed", 0},

    [Q2M_CALLBACK_brain_chest_open] = {callback_sequence_13, Q2M_CALLBACK_brain_chest_open,
                                       "brain_chest_open", 0},

    [Q2M_CALLBACK_brain_dead] = {callback_sequence_4, Q2M_CALLBACK_brain_dead, "brain_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_brain_duck_down] = {callback_brain_duck_down_28, Q2M_CALLBACK_brain_duck_down,
                                      "brain_duck_down", 0},

    [Q2M_CALLBACK_brain_duck_hold] = {callback_brain_duck_down_28, Q2M_CALLBACK_brain_duck_hold,
                                      "brain_duck_hold", 0},

    [Q2M_CALLBACK_brain_duck_up] = {callback_brain_duck_down_28, Q2M_CALLBACK_brain_duck_up,
                                    "brain_duck_up", 0},

    [Q2M_CALLBACK_brain_hit_left] = {callback_sequence_24, Q2M_CALLBACK_brain_hit_left,
                                     "brain_hit_left", 0},

    [Q2M_CALLBACK_brain_hit_right] = {callback_sequence_24, Q2M_CALLBACK_brain_hit_right,
                                      "brain_hit_right", 0},

    [Q2M_CALLBACK_brain_laserbeam] = {callback_brain_laserbeam_9, Q2M_CALLBACK_brain_laserbeam,
                                      "brain_laserbeam", 0},

    [Q2M_CALLBACK_brain_laserbeam_reattack] = {callback_sequence_2,
                                               Q2M_CALLBACK_brain_laserbeam_reattack,
                                               "brain_laserbeam_reattack", 0},

    [Q2M_CALLBACK_brain_run] = {callback_sequence_4, Q2M_CALLBACK_brain_run, "brain_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_brain_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_brain_shrink,
                                   "brain_shrink", 0},

    [Q2M_CALLBACK_brain_stand] = {callback_sequence_4, Q2M_CALLBACK_brain_stand, "brain_stand",
                                  Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_brain_swing_left] = {callback_sequence_10, Q2M_CALLBACK_brain_swing_left,
                                       "brain_swing_left", 0},

    [Q2M_CALLBACK_brain_swing_right] = {callback_sequence_10, Q2M_CALLBACK_brain_swing_right,
                                        "brain_swing_right", 0},

    [Q2M_CALLBACK_brain_tentacle_attack] = {callback_sequence_24,
                                            Q2M_CALLBACK_brain_tentacle_attack,
                                            "brain_tentacle_attack", 0},

    [Q2M_CALLBACK_brain_tounge_attack] = {callback_sequence_24, Q2M_CALLBACK_brain_tounge_attack,
                                          "brain_tounge_attack", 0},

    [Q2M_CALLBACK_carrier_attack_gren] = {callback_sequence_2, Q2M_CALLBACK_carrier_attack_gren,
                                          "carrier_attack_gren", 0},

    [Q2M_CALLBACK_carrier_attack_mg] = {callback_sequence_4, Q2M_CALLBACK_carrier_attack_mg,
                                        "carrier_attack_mg", 0},

    [Q2M_CALLBACK_carrier_dead] = {callback_sequence_4, Q2M_CALLBACK_carrier_dead, "carrier_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_carrier_prep_spawn] = {callback_sequence_1, Q2M_CALLBACK_carrier_prep_spawn,
                                         "carrier_prep_spawn", 0},

    [Q2M_CALLBACK_carrier_ready_spawn] = {callback_sequence_1, Q2M_CALLBACK_carrier_ready_spawn,
                                          "carrier_ready_spawn", 0},

    [Q2M_CALLBACK_carrier_reattack_gren] = {callback_sequence_2, Q2M_CALLBACK_carrier_reattack_gren,
                                            "carrier_reattack_gren", 0},

    [Q2M_CALLBACK_carrier_reattack_mg] = {callback_sequence_2, Q2M_CALLBACK_carrier_reattack_mg,
                                          "carrier_reattack_mg", 0},

    [Q2M_CALLBACK_carrier_run] = {callback_sequence_4, Q2M_CALLBACK_carrier_run, "carrier_run",
                                  Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_carrier_spawn_check] = {callback_sequence_1, Q2M_CALLBACK_carrier_spawn_check,
                                          "carrier_spawn_check", 0},

    [Q2M_CALLBACK_carrier_start_spawn] = {callback_sequence_1, Q2M_CALLBACK_carrier_start_spawn,
                                          "carrier_start_spawn", 0},

    [Q2M_CALLBACK_change_to_roam] = {callback_change_to_roam_40, Q2M_CALLBACK_change_to_roam,
                                     "change_to_roam", 0},

    [Q2M_CALLBACK_chick_attack1] = {callback_sequence_4, Q2M_CALLBACK_chick_attack1,
                                    "chick_attack1", Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_chick_dead] = {callback_sequence_4, Q2M_CALLBACK_chick_dead, "chick_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_chick_duck_down] = {callback_brain_duck_down_28, Q2M_CALLBACK_chick_duck_down,
                                      "chick_duck_down", 0},

    [Q2M_CALLBACK_chick_duck_hold] = {callback_brain_duck_down_28, Q2M_CALLBACK_chick_duck_hold,
                                      "chick_duck_hold", 0},

    [Q2M_CALLBACK_chick_duck_up] = {callback_brain_duck_down_28, Q2M_CALLBACK_chick_duck_up,
                                    "chick_duck_up", 0},

    [Q2M_CALLBACK_chick_fidget] = {callback_chick_fidget_76, Q2M_CALLBACK_chick_fidget,
                                   "chick_fidget", 0},

    [Q2M_CALLBACK_chick_rerocket] = {callback_sequence_2, Q2M_CALLBACK_chick_rerocket,
                                     "chick_rerocket", 0},

    [Q2M_CALLBACK_chick_reslash] = {callback_sequence_2, Q2M_CALLBACK_chick_reslash,
                                    "chick_reslash", 0},

    [Q2M_CALLBACK_chick_run] = {callback_sequence_4, Q2M_CALLBACK_chick_run, "chick_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_chick_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_chick_shrink,
                                   "chick_shrink", 0},

    [Q2M_CALLBACK_chick_slash] = {callback_sequence_4, Q2M_CALLBACK_chick_slash, "chick_slash", 0},

    [Q2M_CALLBACK_chick_stand] = {callback_sequence_4, Q2M_CALLBACK_chick_stand, "chick_stand",
                                  Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_fixbot_attack] = {callback_fixbot_attack_8, Q2M_CALLBACK_fixbot_attack,
                                    "fixbot_attack", 0},

    [Q2M_CALLBACK_fixbot_dead] = {callback_sequence_4, Q2M_CALLBACK_fixbot_dead, "fixbot_dead",
                                  Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_fixbot_fire_blaster] = {callback_fixbot_fire_blaster_5,
                                          Q2M_CALLBACK_fixbot_fire_blaster, "fixbot_fire_blaster",
                                          0},

    [Q2M_CALLBACK_fixbot_fire_laser] = {callback_fixbot_fire_laser_7,
                                        Q2M_CALLBACK_fixbot_fire_laser, "fixbot_fire_laser", 0},

    [Q2M_CALLBACK_fixbot_fire_welder] = {callback_fixbot_fire_welder_6,
                                         Q2M_CALLBACK_fixbot_fire_welder, "fixbot_fire_welder", 0},

    [Q2M_CALLBACK_fixbot_run] = {callback_sequence_4, Q2M_CALLBACK_fixbot_run, "fixbot_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_flipper_bite] = {callback_sequence_24, Q2M_CALLBACK_flipper_bite, "flipper_bite",
                                   0},

    [Q2M_CALLBACK_flipper_dead] = {callback_sequence_4, Q2M_CALLBACK_flipper_dead, "flipper_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_flipper_preattack] = {callback_flipper_preattack_70,
                                        Q2M_CALLBACK_flipper_preattack, "flipper_preattack", 0},

    [Q2M_CALLBACK_flipper_run] = {callback_flipper_run_12, Q2M_CALLBACK_flipper_run, "flipper_run",
                                  Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_flipper_run_loop] = {callback_sequence_4, Q2M_CALLBACK_flipper_run_loop,
                                       "flipper_run_loop", Q2M_CALLBACK_RUN_LOOP},

    [Q2M_CALLBACK_floater_dead] = {callback_sequence_4, Q2M_CALLBACK_floater_dead, "floater_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_floater_fire_blaster] = {callback_sequence_10, Q2M_CALLBACK_floater_fire_blaster,
                                           "floater_fire_blaster", 0},

    [Q2M_CALLBACK_floater_run] = {callback_sequence_4, Q2M_CALLBACK_floater_run, "floater_run",
                                  Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_floater_wham] = {callback_sequence_24, Q2M_CALLBACK_floater_wham, "floater_wham",
                                   0},

    [Q2M_CALLBACK_floater_zap] = {callback_sequence_10, Q2M_CALLBACK_floater_zap, "floater_zap", 0},

    [Q2M_CALLBACK_fly_vertical] = {callback_fly_vertical_42, Q2M_CALLBACK_fly_vertical,
                                   "fly_vertical", 0},

    [Q2M_CALLBACK_fly_vertical2] = {callback_fly_vertical_42, Q2M_CALLBACK_fly_vertical2,
                                    "fly_vertical2", 0},

    [Q2M_CALLBACK_flyer_check_melee] = {callback_sequence_4, Q2M_CALLBACK_flyer_check_melee,
                                        "flyer_check_melee", 0},

    [Q2M_CALLBACK_flyer_fireleft] = {callback_sequence_10, Q2M_CALLBACK_flyer_fireleft,
                                     "flyer_fireleft", 0},

    [Q2M_CALLBACK_flyer_fireright] = {callback_sequence_10, Q2M_CALLBACK_flyer_fireright,
                                      "flyer_fireright", 0},

    [Q2M_CALLBACK_flyer_kamikaze] = {callback_sequence_4, Q2M_CALLBACK_flyer_kamikaze,
                                     "flyer_kamikaze", 0},

    [Q2M_CALLBACK_flyer_kamikaze_check] = {callback_flyer_kamikaze_check_58,
                                           Q2M_CALLBACK_flyer_kamikaze_check,
                                           "flyer_kamikaze_check", 0},

    [Q2M_CALLBACK_flyer_loop_melee] = {callback_sequence_4, Q2M_CALLBACK_flyer_loop_melee,
                                       "flyer_loop_melee", 0},

    [Q2M_CALLBACK_flyer_nextmove] = {callback_flyer_nextmove_85, Q2M_CALLBACK_flyer_nextmove,
                                     "flyer_nextmove", 0},

    [Q2M_CALLBACK_flyer_pop_blades] = {callback_sequence_10, Q2M_CALLBACK_flyer_pop_blades,
                                       "flyer_pop_blades", 0},

    [Q2M_CALLBACK_flyer_run] = {callback_sequence_4, Q2M_CALLBACK_flyer_run, "flyer_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_flyer_slash_left] = {callback_sequence_24, Q2M_CALLBACK_flyer_slash_left,
                                       "flyer_slash_left", 0},

    [Q2M_CALLBACK_flyer_slash_right] = {callback_sequence_24, Q2M_CALLBACK_flyer_slash_right,
                                        "flyer_slash_right", 0},

    [Q2M_CALLBACK_gekk_bite] = {callback_sequence_24, Q2M_CALLBACK_gekk_bite, "gekk_bite", 0},

    [Q2M_CALLBACK_gekk_chant] = {callback_sequence_4, Q2M_CALLBACK_gekk_chant, "gekk_chant", 0},

    [Q2M_CALLBACK_gekk_check_landing] = {callback_sequence_10, Q2M_CALLBACK_gekk_check_landing,
                                         "gekk_check_landing", 0},

    [Q2M_CALLBACK_gekk_check_refire] = {callback_gekk_check_refire_61,
                                        Q2M_CALLBACK_gekk_check_refire, "gekk_check_refire", 0},

    [Q2M_CALLBACK_gekk_check_underwater] = {callback_gekk_check_underwater_44,
                                            Q2M_CALLBACK_gekk_check_underwater,
                                            "gekk_check_underwater", 0},

    [Q2M_CALLBACK_gekk_dead] = {callback_sequence_4, Q2M_CALLBACK_gekk_dead, "gekk_dead",
                                Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_gekk_face] = {callback_sequence_7, Q2M_CALLBACK_gekk_face, "gekk_face", 0},

    [Q2M_CALLBACK_gekk_gibfest] = {callback_gekk_gibfest_86, Q2M_CALLBACK_gekk_gibfest,
                                   "gekk_gibfest", 0},

    [Q2M_CALLBACK_gekk_hit_left] = {callback_sequence_24, Q2M_CALLBACK_gekk_hit_left,
                                    "gekk_hit_left", 0},

    [Q2M_CALLBACK_gekk_hit_right] = {callback_sequence_24, Q2M_CALLBACK_gekk_hit_right,
                                     "gekk_hit_right", 0},

    [Q2M_CALLBACK_gekk_idle_loop] = {callback_gekk_idle_loop_73, Q2M_CALLBACK_gekk_idle_loop,
                                     "gekk_idle_loop", 0},

    [Q2M_CALLBACK_gekk_jump_takeoff] = {callback_sequence_10, Q2M_CALLBACK_gekk_jump_takeoff,
                                        "gekk_jump_takeoff", 0},

    [Q2M_CALLBACK_gekk_jump_takeoff2] = {callback_sequence_10, Q2M_CALLBACK_gekk_jump_takeoff2,
                                         "gekk_jump_takeoff2", 0},

    [Q2M_CALLBACK_gekk_preattack] = {callback_gekk_preattack_71, Q2M_CALLBACK_gekk_preattack,
                                     "gekk_preattack", 0},

    [Q2M_CALLBACK_gekk_run] = {callback_sequence_4, Q2M_CALLBACK_gekk_run, "gekk_run",
                               Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_gekk_run_start] = {callback_sequence_4, Q2M_CALLBACK_gekk_run_start,
                                     "gekk_run_start", 0},

    [Q2M_CALLBACK_gekk_search] = {callback_gekk_search_94, Q2M_CALLBACK_gekk_search, "gekk_search",
                                  0},

    [Q2M_CALLBACK_gekk_stand] = {callback_sequence_4, Q2M_CALLBACK_gekk_stand, "gekk_stand",
                                 Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_gekk_step] = {callback_sequence_10, Q2M_CALLBACK_gekk_step, "gekk_step", 0},

    [Q2M_CALLBACK_gekk_stop_skid] = {callback_gekk_stop_skid_45, Q2M_CALLBACK_gekk_stop_skid,
                                     "gekk_stop_skid", 0},

    [Q2M_CALLBACK_gekk_swim] = {callback_gekk_search_94, Q2M_CALLBACK_gekk_swim, "gekk_swim", 0},

    [Q2M_CALLBACK_gekk_swim_loop] = {callback_sequence_4, Q2M_CALLBACK_gekk_swim_loop,
                                     "gekk_swim_loop", 0},

    [Q2M_CALLBACK_gladbGun] = {callback_gladbGun_3, Q2M_CALLBACK_gladbGun, "gladbGun", 0},

    [Q2M_CALLBACK_gladbGun_check] = {callback_gladbGun_check_59, Q2M_CALLBACK_gladbGun_check,
                                     "gladbGun_check", 0},

    [Q2M_CALLBACK_gladb_cleaver_swing] = {callback_sequence_10, Q2M_CALLBACK_gladb_cleaver_swing,
                                          "gladb_cleaver_swing", 0},

    [Q2M_CALLBACK_gladb_dead] = {callback_sequence_4, Q2M_CALLBACK_gladb_dead, "gladb_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_gladb_run] = {callback_sequence_4, Q2M_CALLBACK_gladb_run, "gladb_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_gladiator_cleaver_swing] = {callback_sequence_10,
                                              Q2M_CALLBACK_gladiator_cleaver_swing,
                                              "gladiator_cleaver_swing", 0},

    [Q2M_CALLBACK_gladiator_dead] = {callback_sequence_4, Q2M_CALLBACK_gladiator_dead,
                                     "gladiator_dead", Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_gladiator_run] = {callback_sequence_4, Q2M_CALLBACK_gladiator_run,
                                    "gladiator_run", Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_gladiator_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_gladiator_shrink,
                                       "gladiator_shrink", 0},

    [Q2M_CALLBACK_guardian_atk1] = {callback_sequence_2, Q2M_CALLBACK_guardian_atk1,
                                    "guardian_atk1", 0},

    [Q2M_CALLBACK_guardian_atk1_charge] = {callback_guardian_atk1_charge_78,
                                           Q2M_CALLBACK_guardian_atk1_charge,
                                           "guardian_atk1_charge", 0},

    [Q2M_CALLBACK_guardian_atk1_finish] = {callback_sequence_3, Q2M_CALLBACK_guardian_atk1_finish,
                                           "guardian_atk1_finish", 0},

    [Q2M_CALLBACK_guardian_atk2] = {callback_sequence_4, Q2M_CALLBACK_guardian_atk2,
                                    "guardian_atk2", 0},

    [Q2M_CALLBACK_guardian_atk2_out] = {callback_sequence_4, Q2M_CALLBACK_guardian_atk2_out,
                                        "guardian_atk2_out", 0},

    [Q2M_CALLBACK_guardian_dead] = {callback_sequence_4, Q2M_CALLBACK_guardian_dead,
                                    "guardian_dead", Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_guardian_fire_blaster] = {callback_guardian_fire_blaster_79,
                                            Q2M_CALLBACK_guardian_fire_blaster,
                                            "guardian_fire_blaster", 0},

    [Q2M_CALLBACK_guardian_footstep] = {callback_sequence_37, Q2M_CALLBACK_guardian_footstep,
                                        "guardian_footstep", 0},

    [Q2M_CALLBACK_guardian_kick] = {callback_sequence_24, Q2M_CALLBACK_guardian_kick,
                                    "guardian_kick", 0},

    [Q2M_CALLBACK_guardian_laser_fire] = {callback_guardian_laser_fire_80,
                                          Q2M_CALLBACK_guardian_laser_fire, "guardian_laser_fire",
                                          0},

    [Q2M_CALLBACK_guardian_run] = {callback_sequence_4, Q2M_CALLBACK_guardian_run, "guardian_run",
                                   Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_guncmdr_dead] = {callback_sequence_4, Q2M_CALLBACK_guncmdr_dead, "guncmdr_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_guncmdr_fidget] = {callback_guncmdr_fidget_77, Q2M_CALLBACK_guncmdr_fidget,
                                     "guncmdr_fidget", 0},

    [Q2M_CALLBACK_guncmdr_fire_chain] = {callback_sequence_4, Q2M_CALLBACK_guncmdr_fire_chain,
                                         "guncmdr_fire_chain", 0},

    [Q2M_CALLBACK_guncmdr_grenade_back_dodge_resume] =
        {callback_sequence_4, Q2M_CALLBACK_guncmdr_grenade_back_dodge_resume,
         "guncmdr_grenade_back_dodge_resume", 0},

    [Q2M_CALLBACK_guncmdr_grenade_mortar_resume] = {callback_sequence_4,
                                                    Q2M_CALLBACK_guncmdr_grenade_mortar_resume,
                                                    "guncmdr_grenade_mortar_resume", 0},

    [Q2M_CALLBACK_guncmdr_idlesound] = {callback_sequence_37, Q2M_CALLBACK_guncmdr_idlesound,
                                        "guncmdr_idlesound", 0},

    [Q2M_CALLBACK_guncmdr_jump2_now] = {callback_berserk_jump2_now_29,
                                        Q2M_CALLBACK_guncmdr_jump2_now, "guncmdr_jump2_now", 0},

    [Q2M_CALLBACK_guncmdr_jump_now] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_guncmdr_jump_now,
                                       "guncmdr_jump_now", 0},

    [Q2M_CALLBACK_guncmdr_jump_wait_land] = {callback_berserk_jump2_now_29,
                                             Q2M_CALLBACK_guncmdr_jump_wait_land,
                                             "guncmdr_jump_wait_land", 0},

    [Q2M_CALLBACK_guncmdr_kick] = {callback_sequence_24, Q2M_CALLBACK_guncmdr_kick, "guncmdr_kick",
                                   0},

    [Q2M_CALLBACK_guncmdr_kick_finished] = {callback_sequence_4, Q2M_CALLBACK_guncmdr_kick_finished,
                                            "guncmdr_kick_finished", 0},

    [Q2M_CALLBACK_guncmdr_opengun] = {callback_sequence_37, Q2M_CALLBACK_guncmdr_opengun,
                                      "guncmdr_opengun", 0},

    [Q2M_CALLBACK_guncmdr_pain5_to_death1] = {callback_guncmdr_pain5_to_death1_84,
                                              Q2M_CALLBACK_guncmdr_pain5_to_death1,
                                              "guncmdr_pain5_to_death1", 0},

    [Q2M_CALLBACK_guncmdr_pain5_to_death2] = {callback_guncmdr_pain5_to_death1_84,
                                              Q2M_CALLBACK_guncmdr_pain5_to_death2,
                                              "guncmdr_pain5_to_death2", 0},

    [Q2M_CALLBACK_guncmdr_pain6_to_death6] = {callback_guncmdr_pain5_to_death1_84,
                                              Q2M_CALLBACK_guncmdr_pain6_to_death6,
                                              "guncmdr_pain6_to_death6", 0},

    [Q2M_CALLBACK_guncmdr_refire_chain] = {callback_sequence_2, Q2M_CALLBACK_guncmdr_refire_chain,
                                           "guncmdr_refire_chain", 0},

    [Q2M_CALLBACK_guncmdr_run] = {callback_sequence_4, Q2M_CALLBACK_guncmdr_run, "guncmdr_run",
                                  Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_guncmdr_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_guncmdr_shrink,
                                     "guncmdr_shrink", 0},

    [Q2M_CALLBACK_guncmdr_stand] = {callback_sequence_4, Q2M_CALLBACK_guncmdr_stand,
                                    "guncmdr_stand", Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_gunner_blind_check] = {callback_gunner_blind_check_51,
                                         Q2M_CALLBACK_gunner_blind_check, "gunner_blind_check", 0},

    [Q2M_CALLBACK_gunner_dead] = {callback_sequence_4, Q2M_CALLBACK_gunner_dead, "gunner_dead",
                                  Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_gunner_duck_down] = {callback_sequence_10, Q2M_CALLBACK_gunner_duck_down,
                                       "gunner_duck_down", 0},

    [Q2M_CALLBACK_gunner_duck_hold] = {callback_sequence_10, Q2M_CALLBACK_gunner_duck_hold,
                                       "gunner_duck_hold", 0},

    [Q2M_CALLBACK_gunner_duck_up] = {callback_sequence_10, Q2M_CALLBACK_gunner_duck_up,
                                     "gunner_duck_up", 0},

    [Q2M_CALLBACK_gunner_fidget] = {callback_guncmdr_fidget_77, Q2M_CALLBACK_gunner_fidget,
                                    "gunner_fidget", 0},

    [Q2M_CALLBACK_gunner_fire_chain] = {callback_sequence_4, Q2M_CALLBACK_gunner_fire_chain,
                                        "gunner_fire_chain", 0},

    [Q2M_CALLBACK_gunner_idlesound] = {callback_sequence_10, Q2M_CALLBACK_gunner_idlesound,
                                       "gunner_idlesound", 0},

    [Q2M_CALLBACK_gunner_jump2_now] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_gunner_jump2_now,
                                       "gunner_jump2_now", 0},

    [Q2M_CALLBACK_gunner_jump_now] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_gunner_jump_now,
                                      "gunner_jump_now", 0},

    [Q2M_CALLBACK_gunner_jump_wait_land] = {callback_berserk_jump2_now_29,
                                            Q2M_CALLBACK_gunner_jump_wait_land,
                                            "gunner_jump_wait_land", 0},

    [Q2M_CALLBACK_gunner_opengun] = {callback_sequence_10, Q2M_CALLBACK_gunner_opengun,
                                     "gunner_opengun", 0},

    [Q2M_CALLBACK_gunner_refire_chain] = {callback_sequence_2, Q2M_CALLBACK_gunner_refire_chain,
                                          "gunner_refire_chain", 0},

    [Q2M_CALLBACK_gunner_run] = {callback_sequence_4, Q2M_CALLBACK_gunner_run, "gunner_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_gunner_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_gunner_shrink,
                                    "gunner_shrink", 0},

    [Q2M_CALLBACK_gunner_stand] = {callback_sequence_4, Q2M_CALLBACK_gunner_stand, "gunner_stand",
                                   Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_hover_attack] = {callback_sequence_4, Q2M_CALLBACK_hover_attack, "hover_attack",
                                   0},

    [Q2M_CALLBACK_hover_dead] = {callback_sequence_4, Q2M_CALLBACK_hover_dead, "hover_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_hover_dying] = {callback_hover_dying_88, Q2M_CALLBACK_hover_dying, "hover_dying",
                                  0},

    [Q2M_CALLBACK_hover_fire_blaster] = {callback_sequence_10, Q2M_CALLBACK_hover_fire_blaster,
                                         "hover_fire_blaster", 0},

    [Q2M_CALLBACK_hover_reattack] = {callback_sequence_2, Q2M_CALLBACK_hover_reattack,
                                     "hover_reattack", 0},

    [Q2M_CALLBACK_hover_run] = {callback_sequence_4, Q2M_CALLBACK_hover_run, "hover_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_infantry_attack4_refire] = {callback_sequence_17,
                                              Q2M_CALLBACK_infantry_attack4_refire,
                                              "infantry_attack4_refire", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_cock_gun] = {callback_sequence_17, Q2M_CALLBACK_infantry_cock_gun,
                                        "infantry_cock_gun", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_dead] = {callback_sequence_5, Q2M_CALLBACK_infantry_dead,
                                    "infantry_dead", Q2M_CALLBACK_INFANTRY | Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_infantry_duck_down] = {callback_sequence_17, Q2M_CALLBACK_infantry_duck_down,
                                         "infantry_duck_down", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_duck_hold] = {callback_sequence_17, Q2M_CALLBACK_infantry_duck_hold,
                                         "infantry_duck_hold", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_duck_up] = {callback_sequence_17, Q2M_CALLBACK_infantry_duck_up,
                                       "infantry_duck_up", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_fire] = {callback_sequence_17, Q2M_CALLBACK_infantry_fire,
                                    "infantry_fire", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_fire_prep] = {callback_sequence_18, Q2M_CALLBACK_infantry_fire_prep,
                                         "infantry_fire_prep", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_jump2_now] = {callback_sequence_17, Q2M_CALLBACK_infantry_jump2_now,
                                         "infantry_jump2_now", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_jump_now] = {callback_sequence_17, Q2M_CALLBACK_infantry_jump_now,
                                        "infantry_jump_now", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_jump_wait_land] = {callback_sequence_17,
                                              Q2M_CALLBACK_infantry_jump_wait_land,
                                              "infantry_jump_wait_land", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_run] = {callback_sequence_5, Q2M_CALLBACK_infantry_run, "infantry_run",
                                   Q2M_CALLBACK_INFANTRY | Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_infantry_set_firetime] = {callback_sequence_17,
                                            Q2M_CALLBACK_infantry_set_firetime,
                                            "infantry_set_firetime", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_shrink] = {callback_sequence_17, Q2M_CALLBACK_infantry_shrink,
                                      "infantry_shrink", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_smack] = {callback_sequence_25, Q2M_CALLBACK_infantry_smack,
                                     "infantry_smack", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_infantry_stand] = {callback_sequence_5, Q2M_CALLBACK_infantry_stand,
                                     "infantry_stand", Q2M_CALLBACK_INFANTRY | Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_infantry_swing] = {callback_sequence_17, Q2M_CALLBACK_infantry_swing,
                                     "infantry_swing", Q2M_CALLBACK_INFANTRY},

    [Q2M_CALLBACK_insane_checkdown] = {callback_insane_checkdown_62, Q2M_CALLBACK_insane_checkdown,
                                       "insane_checkdown", 0},

    [Q2M_CALLBACK_insane_checkup] = {callback_insane_checkup_63, Q2M_CALLBACK_insane_checkup,
                                     "insane_checkup", 0},

    [Q2M_CALLBACK_insane_cross] = {callback_sequence_4, Q2M_CALLBACK_insane_cross, "insane_cross",
                                   0},

    [Q2M_CALLBACK_insane_dead] = {callback_sequence_4, Q2M_CALLBACK_insane_dead, "insane_dead",
                                  Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_insane_fist] = {callback_sequence_37, Q2M_CALLBACK_insane_fist, "insane_fist", 0},

    [Q2M_CALLBACK_insane_moan] = {callback_sequence_37, Q2M_CALLBACK_insane_moan, "insane_moan", 0},

    [Q2M_CALLBACK_insane_onground] = {callback_sequence_4, Q2M_CALLBACK_insane_onground,
                                      "insane_onground", 0},

    [Q2M_CALLBACK_insane_run] = {callback_sequence_4, Q2M_CALLBACK_insane_run, "insane_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_insane_scream] = {callback_sequence_37, Q2M_CALLBACK_insane_scream,
                                    "insane_scream", 0},

    [Q2M_CALLBACK_insane_shake] = {callback_sequence_37, Q2M_CALLBACK_insane_shake, "insane_shake",
                                   0},

    [Q2M_CALLBACK_insane_stand] = {callback_sequence_4, Q2M_CALLBACK_insane_stand, "insane_stand",
                                   Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_insane_walk] = {callback_sequence_4, Q2M_CALLBACK_insane_walk, "insane_walk",
                                  Q2M_CALLBACK_WALK},

    [Q2M_CALLBACK_isgibfest] = {callback_gekk_gibfest_86, Q2M_CALLBACK_isgibfest, "isgibfest", 0},

    [Q2M_CALLBACK_jorgBFG] = {callback_sequence_16, Q2M_CALLBACK_jorgBFG, "jorgBFG", 0},

    [Q2M_CALLBACK_jorg_attack1] = {callback_sequence_4, Q2M_CALLBACK_jorg_attack1, "jorg_attack1",
                                   Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_jorg_dead] = {callback_sequence_4, Q2M_CALLBACK_jorg_dead, "jorg_dead",
                                Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_jorg_death_hit] = {callback_jorg_death_hit_89, Q2M_CALLBACK_jorg_death_hit,
                                     "jorg_death_hit", 0},

    [Q2M_CALLBACK_jorg_firebullet] = {callback_sequence_16, Q2M_CALLBACK_jorg_firebullet,
                                      "jorg_firebullet", 0},

    [Q2M_CALLBACK_jorg_firebullet_left] = {callback_sequence_16, Q2M_CALLBACK_jorg_firebullet_left,
                                           "jorg_firebullet_left", 0},

    [Q2M_CALLBACK_jorg_firebullet_right] = {callback_sequence_16,
                                            Q2M_CALLBACK_jorg_firebullet_right,
                                            "jorg_firebullet_right", 0},

    [Q2M_CALLBACK_jorg_idle] = {callback_sequence_37, Q2M_CALLBACK_jorg_idle, "jorg_idle", 0},

    [Q2M_CALLBACK_jorg_reattack1] = {callback_sequence_2, Q2M_CALLBACK_jorg_reattack1,
                                     "jorg_reattack1", Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_jorg_run] = {callback_sequence_4, Q2M_CALLBACK_jorg_run, "jorg_run",
                               Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_jorg_stand] = {callback_sequence_4, Q2M_CALLBACK_jorg_stand, "jorg_stand",
                                 Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_jorg_step_left] = {callback_sequence_37, Q2M_CALLBACK_jorg_step_left,
                                     "jorg_step_left", 0},

    [Q2M_CALLBACK_jorg_step_right] = {callback_sequence_37, Q2M_CALLBACK_jorg_step_right,
                                      "jorg_step_right", 0},

    [Q2M_CALLBACK_loogie] = {callback_loogie_17, Q2M_CALLBACK_loogie, "loogie", 0},

    [Q2M_CALLBACK_makronBFG] = {callback_sequence_16, Q2M_CALLBACK_makronBFG, "makronBFG", 0},

    [Q2M_CALLBACK_makron_brainsplorch] = {callback_sequence_37, Q2M_CALLBACK_makron_brainsplorch,
                                          "makron_brainsplorch", 0},

    [Q2M_CALLBACK_makron_dead] = {callback_sequence_4, Q2M_CALLBACK_makron_dead, "makron_dead",
                                  Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_makron_hit] = {callback_sequence_37, Q2M_CALLBACK_makron_hit, "makron_hit", 0},

    [Q2M_CALLBACK_makron_popup] = {callback_sequence_37, Q2M_CALLBACK_makron_popup, "makron_popup",
                                   0},

    [Q2M_CALLBACK_makron_prerailgun] = {callback_sequence_37, Q2M_CALLBACK_makron_prerailgun,
                                        "makron_prerailgun", 0},

    [Q2M_CALLBACK_makron_run] = {callback_sequence_4, Q2M_CALLBACK_makron_run, "makron_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_makron_step_left] = {callback_sequence_37, Q2M_CALLBACK_makron_step_left,
                                       "makron_step_left", 0},

    [Q2M_CALLBACK_makron_step_right] = {callback_sequence_37, Q2M_CALLBACK_makron_step_right,
                                        "makron_step_right", 0},

    [Q2M_CALLBACK_makron_taunt] = {callback_sequence_37, Q2M_CALLBACK_makron_taunt, "makron_taunt",
                                   0},

    [Q2M_CALLBACK_medic_cable_attack] = {callback_sequence_19, Q2M_CALLBACK_medic_cable_attack,
                                         "medic_cable_attack", 0},

    [Q2M_CALLBACK_medic_continue] = {callback_sequence_19, Q2M_CALLBACK_medic_continue,
                                     "medic_continue", 0},

    [Q2M_CALLBACK_medic_dead] = {callback_sequence_4, Q2M_CALLBACK_medic_dead, "medic_dead",
                                 Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_medic_determine_spawn] = {callback_sequence_27,
                                            Q2M_CALLBACK_medic_determine_spawn,
                                            "medic_determine_spawn", 0},

    [Q2M_CALLBACK_medic_duck_down] = {callback_brain_duck_down_28, Q2M_CALLBACK_medic_duck_down,
                                      "medic_duck_down", 0},

    [Q2M_CALLBACK_medic_duck_hold] = {callback_brain_duck_down_28, Q2M_CALLBACK_medic_duck_hold,
                                      "medic_duck_hold", 0},

    [Q2M_CALLBACK_medic_duck_up] = {callback_brain_duck_down_28, Q2M_CALLBACK_medic_duck_up,
                                    "medic_duck_up", 0},

    [Q2M_CALLBACK_medic_finish_spawn] = {callback_sequence_27, Q2M_CALLBACK_medic_finish_spawn,
                                         "medic_finish_spawn", 0},

    [Q2M_CALLBACK_medic_fire_blaster] = {callback_sequence_10, Q2M_CALLBACK_medic_fire_blaster,
                                         "medic_fire_blaster", 0},

    [Q2M_CALLBACK_medic_hook_launch] = {callback_sequence_19, Q2M_CALLBACK_medic_hook_launch,
                                        "medic_hook_launch", 0},

    [Q2M_CALLBACK_medic_hook_retract] = {callback_sequence_19, Q2M_CALLBACK_medic_hook_retract,
                                         "medic_hook_retract", 0},

    [Q2M_CALLBACK_medic_idle] = {callback_sequence_19, Q2M_CALLBACK_medic_idle, "medic_idle", 0},

    [Q2M_CALLBACK_medic_quick_attack] = {callback_sequence_19, Q2M_CALLBACK_medic_quick_attack,
                                         "medic_quick_attack", 0},

    [Q2M_CALLBACK_medic_run] = {callback_sequence_20, Q2M_CALLBACK_medic_run, "medic_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_medic_search] = {callback_sequence_19, Q2M_CALLBACK_medic_search, "medic_search",
                                   0},

    [Q2M_CALLBACK_medic_shrink] = {callback_sequence_19, Q2M_CALLBACK_medic_shrink, "medic_shrink",
                                   0},

    [Q2M_CALLBACK_medic_spawngrows] = {callback_sequence_27, Q2M_CALLBACK_medic_spawngrows,
                                       "medic_spawngrows", 0},

    [Q2M_CALLBACK_medic_stand] = {callback_sequence_4, Q2M_CALLBACK_medic_stand, "medic_stand",
                                  Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_medic_start_spawn] = {callback_sequence_27, Q2M_CALLBACK_medic_start_spawn,
                                        "medic_start_spawn", 0},

    [Q2M_CALLBACK_medic_walk] = {callback_sequence_4, Q2M_CALLBACK_medic_walk, "medic_walk",
                                 Q2M_CALLBACK_WALK},

    [Q2M_CALLBACK_monster_check_prone] = {callback_monster_check_prone_37,
                                          Q2M_CALLBACK_monster_check_prone, "monster_check_prone",
                                          0},

    [Q2M_CALLBACK_monster_dead] = {callback_sequence_4, Q2M_CALLBACK_monster_dead, "monster_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_monster_done_dodge] = {callback_monster_done_dodge_36,
                                         Q2M_CALLBACK_monster_done_dodge, "monster_done_dodge", 0},

    [Q2M_CALLBACK_monster_duck_down] = {callback_brain_duck_down_28, Q2M_CALLBACK_monster_duck_down,
                                        "monster_duck_down", 0},

    [Q2M_CALLBACK_monster_duck_hold] = {callback_brain_duck_down_28, Q2M_CALLBACK_monster_duck_hold,
                                        "monster_duck_hold", 0},

    [Q2M_CALLBACK_monster_duck_up] = {callback_brain_duck_down_28, Q2M_CALLBACK_monster_duck_up,
                                      "monster_duck_up", 0},

    [Q2M_CALLBACK_monster_footstep] = {callback_sequence_37, Q2M_CALLBACK_monster_footstep,
                                       "monster_footstep", 0},

    [Q2M_CALLBACK_mutant_check_landing] = {callback_sequence_10, Q2M_CALLBACK_mutant_check_landing,
                                           "mutant_check_landing", 0},

    [Q2M_CALLBACK_mutant_check_refire] = {callback_mutant_check_refire_60,
                                          Q2M_CALLBACK_mutant_check_refire, "mutant_check_refire",
                                          0},

    [Q2M_CALLBACK_mutant_dead] = {callback_sequence_4, Q2M_CALLBACK_mutant_dead, "mutant_dead",
                                  Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_mutant_hit_left] = {callback_sequence_24, Q2M_CALLBACK_mutant_hit_left,
                                      "mutant_hit_left", 0},

    [Q2M_CALLBACK_mutant_hit_right] = {callback_sequence_24, Q2M_CALLBACK_mutant_hit_right,
                                       "mutant_hit_right", 0},

    [Q2M_CALLBACK_mutant_idle_loop] = {callback_mutant_idle_loop_72, Q2M_CALLBACK_mutant_idle_loop,
                                       "mutant_idle_loop", 0},

    [Q2M_CALLBACK_mutant_jump_down] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_mutant_jump_down,
                                       "mutant_jump_down", 0},

    [Q2M_CALLBACK_mutant_jump_takeoff] = {callback_sequence_10, Q2M_CALLBACK_mutant_jump_takeoff,
                                          "mutant_jump_takeoff", 0},

    [Q2M_CALLBACK_mutant_jump_up] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_mutant_jump_up,
                                     "mutant_jump_up", 0},

    [Q2M_CALLBACK_mutant_jump_wait_land] = {callback_berserk_jump2_now_29,
                                            Q2M_CALLBACK_mutant_jump_wait_land,
                                            "mutant_jump_wait_land", 0},

    [Q2M_CALLBACK_mutant_run] = {callback_sequence_4, Q2M_CALLBACK_mutant_run, "mutant_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_mutant_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_mutant_shrink,
                                    "mutant_shrink", 0},

    [Q2M_CALLBACK_mutant_stand] = {callback_sequence_4, Q2M_CALLBACK_mutant_stand, "mutant_stand",
                                   Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_mutant_step] = {callback_sequence_10, Q2M_CALLBACK_mutant_step, "mutant_step", 0},

    [Q2M_CALLBACK_mutant_walk_loop] = {callback_sequence_4, Q2M_CALLBACK_mutant_walk_loop,
                                       "mutant_walk_loop", 0},

    [Q2M_CALLBACK_parasite_break_noise] = {callback_sequence_21, Q2M_CALLBACK_parasite_break_noise,
                                           "parasite_break_noise", 0},

    [Q2M_CALLBACK_parasite_break_retract] = {callback_sequence_21,
                                             Q2M_CALLBACK_parasite_break_retract,
                                             "parasite_break_retract", 0},

    [Q2M_CALLBACK_parasite_break_sound] = {callback_sequence_21, Q2M_CALLBACK_parasite_break_sound,
                                           "parasite_break_sound", 0},

    [Q2M_CALLBACK_parasite_break_wait] = {callback_sequence_21, Q2M_CALLBACK_parasite_break_wait,
                                          "parasite_break_wait", 0},

    [Q2M_CALLBACK_parasite_dead] = {callback_sequence_4, Q2M_CALLBACK_parasite_dead,
                                    "parasite_dead", Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_parasite_do_fidget] = {callback_sequence_4, Q2M_CALLBACK_parasite_do_fidget,
                                         "parasite_do_fidget", 0},

    [Q2M_CALLBACK_parasite_drain_attack] = {callback_sequence_15,
                                            Q2M_CALLBACK_parasite_drain_attack,
                                            "parasite_drain_attack", 0},

    [Q2M_CALLBACK_parasite_fire_proboscis] = {callback_sequence_21,
                                              Q2M_CALLBACK_parasite_fire_proboscis,
                                              "parasite_fire_proboscis", 0},

    [Q2M_CALLBACK_parasite_jump_down] = {callback_berserk_jump2_now_29,
                                         Q2M_CALLBACK_parasite_jump_down, "parasite_jump_down", 0},

    [Q2M_CALLBACK_parasite_jump_up] = {callback_berserk_jump2_now_29, Q2M_CALLBACK_parasite_jump_up,
                                       "parasite_jump_up", 0},

    [Q2M_CALLBACK_parasite_jump_wait_land] = {callback_berserk_jump2_now_29,
                                              Q2M_CALLBACK_parasite_jump_wait_land,
                                              "parasite_jump_wait_land", 0},

    [Q2M_CALLBACK_parasite_launch] = {callback_sequence_15, Q2M_CALLBACK_parasite_launch,
                                      "parasite_launch", 0},

    [Q2M_CALLBACK_parasite_proboscis_pull_wait] = {callback_sequence_21,
                                                   Q2M_CALLBACK_parasite_proboscis_pull_wait,
                                                   "parasite_proboscis_pull_wait", 0},

    [Q2M_CALLBACK_parasite_proboscis_wait] = {callback_sequence_21,
                                              Q2M_CALLBACK_parasite_proboscis_wait,
                                              "parasite_proboscis_wait", 0},

    [Q2M_CALLBACK_parasite_reel_in] = {callback_sequence_14, Q2M_CALLBACK_parasite_reel_in,
                                       "parasite_reel_in", 0},

    [Q2M_CALLBACK_parasite_refidget] = {callback_sequence_4, Q2M_CALLBACK_parasite_refidget,
                                        "parasite_refidget", 0},

    [Q2M_CALLBACK_parasite_run] = {callback_sequence_22, Q2M_CALLBACK_parasite_run, "parasite_run",
                                   Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_parasite_scratch] = {callback_sequence_23, Q2M_CALLBACK_parasite_scratch,
                                       "parasite_scratch", 0},

    [Q2M_CALLBACK_parasite_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_parasite_shrink,
                                      "parasite_shrink", 0},

    [Q2M_CALLBACK_parasite_stand] = {callback_sequence_4, Q2M_CALLBACK_parasite_stand,
                                     "parasite_stand", Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_parasite_start_run] = {callback_sequence_22, Q2M_CALLBACK_parasite_start_run,
                                         "parasite_start_run", Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_parasite_tap] = {callback_sequence_23, Q2M_CALLBACK_parasite_tap, "parasite_tap",
                                   0},

    [Q2M_CALLBACK_parasite_walk] = {callback_sequence_8, Q2M_CALLBACK_parasite_walk,
                                    "parasite_walk", Q2M_CALLBACK_WALK},

    [Q2M_CALLBACK_reloogie] = {callback_reloogie_64, Q2M_CALLBACK_reloogie, "reloogie", 0},

    [Q2M_CALLBACK_roam_goal] = {callback_roam_goal_41, Q2M_CALLBACK_roam_goal, "roam_goal", 0},

    [Q2M_CALLBACK_sham_smash10] = {callback_sequence_24, Q2M_CALLBACK_sham_smash10, "sham_smash10",
                                   0},

    [Q2M_CALLBACK_sham_swingl9] = {callback_sham_swingl9_11, Q2M_CALLBACK_sham_swingl9,
                                   "sham_swingl9", 0},

    [Q2M_CALLBACK_sham_swingr9] = {callback_sham_swingl9_11, Q2M_CALLBACK_sham_swingr9,
                                   "sham_swingr9", 0},

    [Q2M_CALLBACK_shambler_dead] = {callback_sequence_4, Q2M_CALLBACK_shambler_dead,
                                    "shambler_dead", Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_shambler_lightning_update] = {callback_shambler_lightning_update_34,
                                                Q2M_CALLBACK_shambler_lightning_update,
                                                "shambler_lightning_update", 0},

    [Q2M_CALLBACK_shambler_maybe_idle] = {callback_shambler_maybe_idle_74,
                                          Q2M_CALLBACK_shambler_maybe_idle, "shambler_maybe_idle",
                                          0},

    [Q2M_CALLBACK_shambler_melee1] = {callback_sequence_37, Q2M_CALLBACK_shambler_melee1,
                                      "shambler_melee1", 0},

    [Q2M_CALLBACK_shambler_melee2] = {callback_sequence_37, Q2M_CALLBACK_shambler_melee2,
                                      "shambler_melee2", 0},

    [Q2M_CALLBACK_shambler_run] = {callback_sequence_4, Q2M_CALLBACK_shambler_run, "shambler_run",
                                   Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_shambler_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_shambler_shrink,
                                      "shambler_shrink", 0},

    [Q2M_CALLBACK_shambler_windup] = {callback_shambler_lightning_update_34,
                                      Q2M_CALLBACK_shambler_windup, "shambler_windup", 0},

    [Q2M_CALLBACK_soldier_attack1_refire1] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldier_attack1_refire1,
                                              "soldier_attack1_refire1",
                                              Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_ATTACK1 |
                                                  Q2M_CALLBACK_REFIRE1},

    [Q2M_CALLBACK_soldier_attack1_refire2] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldier_attack1_refire2,
                                              "soldier_attack1_refire2",
                                              Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_soldier_attack1_shotgun_check] = {callback_sequence_29,
                                                    Q2M_CALLBACK_soldier_attack1_shotgun_check,
                                                    "soldier_attack1_shotgun_check",
                                                    Q2M_CALLBACK_BASE_SOLDIER |
                                                        Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_soldier_attack2_refire1] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldier_attack2_refire1,
                                              "soldier_attack2_refire1",
                                              Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_ATTACK2 |
                                                  Q2M_CALLBACK_REFIRE1},

    [Q2M_CALLBACK_soldier_attack2_refire2] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldier_attack2_refire2,
                                              "soldier_attack2_refire2",
                                              Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_ATTACK2},

    [Q2M_CALLBACK_soldier_attack2_shotgun_check] = {callback_sequence_29,
                                                    Q2M_CALLBACK_soldier_attack2_shotgun_check,
                                                    "soldier_attack2_shotgun_check",
                                                    Q2M_CALLBACK_BASE_SOLDIER |
                                                        Q2M_CALLBACK_ATTACK2},

    [Q2M_CALLBACK_soldier_attack3_refire] = {callback_sequence_29,
                                             Q2M_CALLBACK_soldier_attack3_refire,
                                             "soldier_attack3_refire", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_attack6_refire] = {callback_sequence_29,
                                             Q2M_CALLBACK_soldier_attack6_refire,
                                             "soldier_attack6_refire", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_attack6_refire1] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldier_attack6_refire1,
                                              "soldier_attack6_refire1",
                                              Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_REFIRE1},

    [Q2M_CALLBACK_soldier_attack6_refire2] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldier_attack6_refire2,
                                              "soldier_attack6_refire2", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_attack6_shotgun_check] = {callback_sequence_29,
                                                    Q2M_CALLBACK_soldier_attack6_shotgun_check,
                                                    "soldier_attack6_shotgun_check",
                                                    Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_blind] = {callback_sequence_6, Q2M_CALLBACK_soldier_blind,
                                    "soldier_blind", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_blind_check] = {callback_sequence_30, Q2M_CALLBACK_soldier_blind_check,
                                          "soldier_blind_check", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_cock] = {callback_sequence_31, Q2M_CALLBACK_soldier_cock, "soldier_cock",
                                   Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_dead] = {callback_sequence_6, Q2M_CALLBACK_soldier_dead, "soldier_dead",
                                   Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_soldier_dead2] = {callback_sequence_6, Q2M_CALLBACK_soldier_dead2,
                                    "soldier_dead2", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_death_shrink] = {callback_sequence_29, Q2M_CALLBACK_soldier_death_shrink,
                                           "soldier_death_shrink", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_duck_down] = {callback_sequence_29, Q2M_CALLBACK_soldier_duck_down,
                                        "soldier_duck_down", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_duck_hold] = {callback_sequence_29, Q2M_CALLBACK_soldier_duck_hold,
                                        "soldier_duck_hold", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_duck_up] = {callback_sequence_29, Q2M_CALLBACK_soldier_duck_up,
                                      "soldier_duck_up", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire1] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire1,
                                    "soldier_fire1", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire2] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire2,
                                    "soldier_fire2", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire3] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire3,
                                    "soldier_fire3", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire4] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire4,
                                    "soldier_fire4", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire5] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire5,
                                    "soldier_fire5", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire6] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire6,
                                    "soldier_fire6", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire7] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire7,
                                    "soldier_fire7", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_fire8] = {callback_sequence_29, Q2M_CALLBACK_soldier_fire8,
                                    "soldier_fire8", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_idle] = {callback_sequence_29, Q2M_CALLBACK_soldier_idle, "soldier_idle",
                                   Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_run] = {callback_sequence_6, Q2M_CALLBACK_soldier_run, "soldier_run",
                                  Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_soldier_stand] = {callback_sequence_6, Q2M_CALLBACK_soldier_stand,
                                    "soldier_stand",
                                    Q2M_CALLBACK_BASE_SOLDIER | Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_soldier_stand_up] = {callback_sequence_6, Q2M_CALLBACK_soldier_stand_up,
                                       "soldier_stand_up", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_start_charge] = {callback_sequence_32, Q2M_CALLBACK_soldier_start_charge,
                                           "soldier_start_charge", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_stop_charge] = {callback_sequence_29, Q2M_CALLBACK_soldier_stop_charge,
                                          "soldier_stop_charge", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldier_walk1_random] = {callback_sequence_33, Q2M_CALLBACK_soldier_walk1_random,
                                           "soldier_walk1_random", Q2M_CALLBACK_BASE_SOLDIER},

    [Q2M_CALLBACK_soldierh_attack1_refire1] = {callback_sequence_29,
                                               Q2M_CALLBACK_soldierh_attack1_refire1,
                                               "soldierh_attack1_refire1",
                                               Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_ATTACK1 |
                                                   Q2M_CALLBACK_REFIRE1},

    [Q2M_CALLBACK_soldierh_attack1_refire2] = {callback_sequence_29,
                                               Q2M_CALLBACK_soldierh_attack1_refire2,
                                               "soldierh_attack1_refire2",
                                               Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_soldierh_attack2_refire1] = {callback_sequence_29,
                                               Q2M_CALLBACK_soldierh_attack2_refire1,
                                               "soldierh_attack2_refire1",
                                               Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_ATTACK2 |
                                                   Q2M_CALLBACK_REFIRE1},

    [Q2M_CALLBACK_soldierh_attack2_refire2] = {callback_sequence_29,
                                               Q2M_CALLBACK_soldierh_attack2_refire2,
                                               "soldierh_attack2_refire2",
                                               Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_ATTACK2},

    [Q2M_CALLBACK_soldierh_attack3_refire] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldierh_attack3_refire,
                                              "soldierh_attack3_refire",
                                              Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_attack6_refire] = {callback_sequence_29,
                                              Q2M_CALLBACK_soldierh_attack6_refire,
                                              "soldierh_attack6_refire",
                                              Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_cock] = {callback_sequence_29, Q2M_CALLBACK_soldierh_cock,
                                    "soldierh_cock", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_dead] = {callback_sequence_6, Q2M_CALLBACK_soldierh_dead,
                                    "soldierh_dead",
                                    Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_soldierh_duck_down] = {callback_sequence_29, Q2M_CALLBACK_soldierh_duck_down,
                                         "soldierh_duck_down", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_duck_hold] = {callback_sequence_29, Q2M_CALLBACK_soldierh_duck_hold,
                                         "soldierh_duck_hold", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_duck_up] = {callback_sequence_29, Q2M_CALLBACK_soldierh_duck_up,
                                       "soldierh_duck_up", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire1] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire1,
                                     "soldierh_fire1", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire2] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire2,
                                     "soldierh_fire2", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire3] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire3,
                                     "soldierh_fire3", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire4] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire4,
                                     "soldierh_fire4", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire6] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire6,
                                     "soldierh_fire6", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire7] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire7,
                                     "soldierh_fire7", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_fire8] = {callback_sequence_29, Q2M_CALLBACK_soldierh_fire8,
                                     "soldierh_fire8", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyper_laser_sound_end] = {callback_sequence_34,
                                                     Q2M_CALLBACK_soldierh_hyper_laser_sound_end,
                                                     "soldierh_hyper_laser_sound_end",
                                                     Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyper_laser_sound_start] =
        {callback_sequence_35, Q2M_CALLBACK_soldierh_hyper_laser_sound_start,
         "soldierh_hyper_laser_sound_start", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyper_refire1] = {callback_sequence_29,
                                             Q2M_CALLBACK_soldierh_hyper_refire1,
                                             "soldierh_hyper_refire1",
                                             Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_REFIRE1},

    [Q2M_CALLBACK_soldierh_hyper_refire2] = {callback_sequence_29,
                                             Q2M_CALLBACK_soldierh_hyper_refire2,
                                             "soldierh_hyper_refire2", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyper_sound] = {callback_sequence_36, Q2M_CALLBACK_soldierh_hyper_sound,
                                           "soldierh_hyper_sound", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyperripper1] = {callback_sequence_29,
                                            Q2M_CALLBACK_soldierh_hyperripper1,
                                            "soldierh_hyperripper1", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyperripper2] = {callback_sequence_29,
                                            Q2M_CALLBACK_soldierh_hyperripper2,
                                            "soldierh_hyperripper2", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyperripper3] = {callback_sequence_29,
                                            Q2M_CALLBACK_soldierh_hyperripper3,
                                            "soldierh_hyperripper3", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyperripper5] = {callback_sequence_29,
                                            Q2M_CALLBACK_soldierh_hyperripper5,
                                            "soldierh_hyperripper5", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_hyperripper8] = {callback_sequence_29,
                                            Q2M_CALLBACK_soldierh_hyperripper8,
                                            "soldierh_hyperripper8", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_idle] = {callback_sequence_29, Q2M_CALLBACK_soldierh_idle,
                                    "soldierh_idle", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_ripper1] = {callback_sequence_29, Q2M_CALLBACK_soldierh_ripper1,
                                       "soldierh_ripper1", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_ripper2] = {callback_sequence_29, Q2M_CALLBACK_soldierh_ripper2,
                                       "soldierh_ripper2", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_soldierh_run] = {callback_sequence_6, Q2M_CALLBACK_soldierh_run, "soldierh_run",
                                   Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_soldierh_stand] = {callback_sequence_6, Q2M_CALLBACK_soldierh_stand,
                                     "soldierh_stand",
                                     Q2M_CALLBACK_HEAVY_SOLDIER | Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_soldierh_walk1_random] = {callback_sequence_33,
                                            Q2M_CALLBACK_soldierh_walk1_random,
                                            "soldierh_walk1_random", Q2M_CALLBACK_HEAVY_SOLDIER},

    [Q2M_CALLBACK_spawn_out_do] = {callback_sequence_28, Q2M_CALLBACK_spawn_out_do, "spawn_out_do",
                                   0},

    [Q2M_CALLBACK_spawn_out_start] = {callback_sequence_28, Q2M_CALLBACK_spawn_out_start,
                                      "spawn_out_start", 0},

    [Q2M_CALLBACK_stalker_dead] = {callback_sequence_4, Q2M_CALLBACK_stalker_dead, "stalker_dead",
                                   Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_stalker_false_death] = {callback_sequence_4, Q2M_CALLBACK_stalker_false_death,
                                          "stalker_false_death", 0},

    [Q2M_CALLBACK_stalker_footstep] = {callback_sequence_26, Q2M_CALLBACK_stalker_footstep,
                                       "stalker_footstep", 0},

    [Q2M_CALLBACK_stalker_heal] = {callback_sequence_26, Q2M_CALLBACK_stalker_heal, "stalker_heal",
                                   0},

    [Q2M_CALLBACK_stalker_idle_noise] = {callback_stalker_idle_noise,
                                         Q2M_CALLBACK_stalker_idle_noise, "stalker_idle_noise", 0},

    [Q2M_CALLBACK_stalker_jump_down] = {callback_sequence_26, Q2M_CALLBACK_stalker_jump_down,
                                        "stalker_jump_down", 0},

    [Q2M_CALLBACK_stalker_jump_straightup] = {callback_sequence_26,
                                              Q2M_CALLBACK_stalker_jump_straightup,
                                              "stalker_jump_straightup", 0},

    [Q2M_CALLBACK_stalker_jump_up] = {callback_sequence_26, Q2M_CALLBACK_stalker_jump_up,
                                      "stalker_jump_up", 0},

    [Q2M_CALLBACK_stalker_jump_wait_land] = {callback_sequence_26,
                                             Q2M_CALLBACK_stalker_jump_wait_land,
                                             "stalker_jump_wait_land", 0},

    [Q2M_CALLBACK_stalker_run] = {callback_sequence_4, Q2M_CALLBACK_stalker_run, "stalker_run",
                                  Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_stalker_shoot_attack] = {callback_sequence_26, Q2M_CALLBACK_stalker_shoot_attack,
                                           "stalker_shoot_attack", 0},

    [Q2M_CALLBACK_stalker_shoot_attack2] = {callback_sequence_26,
                                            Q2M_CALLBACK_stalker_shoot_attack2,
                                            "stalker_shoot_attack2", Q2M_CALLBACK_ATTACK2},

    [Q2M_CALLBACK_stalker_stand] = {callback_sequence_4, Q2M_CALLBACK_stalker_stand,
                                    "stalker_stand", Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_stalker_swing_attack] = {callback_sequence_24, Q2M_CALLBACK_stalker_swing_attack,
                                           "stalker_swing_attack", 0},

    [Q2M_CALLBACK_stalker_walk] = {callback_sequence_4, Q2M_CALLBACK_stalker_walk, "stalker_walk",
                                   Q2M_CALLBACK_WALK},

    [Q2M_CALLBACK_supertankGrenade] = {callback_sequence_16, Q2M_CALLBACK_supertankGrenade,
                                       "supertankGrenade", 0},

    [Q2M_CALLBACK_supertankMachineGun] = {callback_sequence_16, Q2M_CALLBACK_supertankMachineGun,
                                          "supertankMachineGun", 0},

    [Q2M_CALLBACK_supertankRocket] = {callback_sequence_16, Q2M_CALLBACK_supertankRocket,
                                      "supertankRocket", 0},

    [Q2M_CALLBACK_supertank_dead] = {callback_sequence_4, Q2M_CALLBACK_supertank_dead,
                                     "supertank_dead", Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_supertank_reattack1] = {callback_sequence_2, Q2M_CALLBACK_supertank_reattack1,
                                          "supertank_reattack1", Q2M_CALLBACK_ATTACK1},

    [Q2M_CALLBACK_supertank_run] = {callback_sequence_4, Q2M_CALLBACK_supertank_run,
                                    "supertank_run", Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_tank_blind_check] = {callback_gunner_blind_check_51,
                                       Q2M_CALLBACK_tank_blind_check, "tank_blind_check", 0},

    [Q2M_CALLBACK_tank_dead] = {callback_sequence_4, Q2M_CALLBACK_tank_dead, "tank_dead",
                                Q2M_CALLBACK_DEAD},

    [Q2M_CALLBACK_tank_doattack_rocket] = {callback_sequence_4, Q2M_CALLBACK_tank_doattack_rocket,
                                           "tank_doattack_rocket", 0},

    [Q2M_CALLBACK_tank_footstep] = {callback_sequence_37, Q2M_CALLBACK_tank_footstep,
                                    "tank_footstep", 0},

    [Q2M_CALLBACK_tank_poststrike] = {callback_sequence_4, Q2M_CALLBACK_tank_poststrike,
                                      "tank_poststrike", 0},

    [Q2M_CALLBACK_tank_reattack_blaster] = {callback_sequence_2, Q2M_CALLBACK_tank_reattack_blaster,
                                            "tank_reattack_blaster", 0},

    [Q2M_CALLBACK_tank_refire_rocket] = {callback_sequence_2, Q2M_CALLBACK_tank_refire_rocket,
                                         "tank_refire_rocket", 0},

    [Q2M_CALLBACK_tank_run] = {callback_sequence_4, Q2M_CALLBACK_tank_run, "tank_run",
                               Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_tank_shrink] = {callback_berserk_shrink_30, Q2M_CALLBACK_tank_shrink,
                                  "tank_shrink", 0},

    [Q2M_CALLBACK_tank_stand] = {callback_sequence_4, Q2M_CALLBACK_tank_stand, "tank_stand",
                                 Q2M_CALLBACK_STAND},

    [Q2M_CALLBACK_tank_thud] = {callback_sequence_37, Q2M_CALLBACK_tank_thud, "tank_thud", 0},

    [Q2M_CALLBACK_tank_walk] = {callback_sequence_4, Q2M_CALLBACK_tank_walk, "tank_walk",
                                Q2M_CALLBACK_WALK},

    [Q2M_CALLBACK_tank_windup] = {callback_sequence_37, Q2M_CALLBACK_tank_windup, "tank_windup", 0},

    [Q2M_CALLBACK_turret_run] = {callback_turret_run, Q2M_CALLBACK_turret_run, "turret_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_use_scanner] = {callback_use_scanner_48, Q2M_CALLBACK_use_scanner, "use_scanner",
                                  0},

    [Q2M_CALLBACK_weldstate] = {callback_weldstate_49, Q2M_CALLBACK_weldstate, "weldstate", 0},

    [Q2M_CALLBACK_widow2_attack_beam] = {callback_widow2_attack_beam_13,
                                         Q2M_CALLBACK_widow2_attack_beam, "widow2_attack_beam", 0},

    [Q2M_CALLBACK_widow2_disrupt_reattack] = {callback_sequence_2,
                                              Q2M_CALLBACK_widow2_disrupt_reattack,
                                              "widow2_disrupt_reattack", 0},

    [Q2M_CALLBACK_widow2_finaldeath] = {callback_sequence_9, Q2M_CALLBACK_widow2_finaldeath,
                                        "widow2_finaldeath", 0},

    [Q2M_CALLBACK_widow2_keep_searching] = {callback_widow2_keep_searching_92,
                                            Q2M_CALLBACK_widow2_keep_searching,
                                            "widow2_keep_searching", 0},

    [Q2M_CALLBACK_widow2_ready_spawn] = {callback_sequence_27, Q2M_CALLBACK_widow2_ready_spawn,
                                         "widow2_ready_spawn", 0},

    [Q2M_CALLBACK_widow2_reattack_beam] = {callback_sequence_2, Q2M_CALLBACK_widow2_reattack_beam,
                                           "widow2_reattack_beam", 0},

    [Q2M_CALLBACK_widow2_run] = {callback_sequence_4, Q2M_CALLBACK_widow2_run, "widow2_run",
                                 Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_widow2_spawn_check] = {callback_sequence_27, Q2M_CALLBACK_widow2_spawn_check,
                                         "widow2_spawn_check", 0},

    [Q2M_CALLBACK_widow2_start_searching] = {callback_widow2_start_searching_91,
                                             Q2M_CALLBACK_widow2_start_searching,
                                             "widow2_start_searching", 0},

    [Q2M_CALLBACK_widow_attack_blaster] = {callback_sequence_2, Q2M_CALLBACK_widow_attack_blaster,
                                           "widow_attack_blaster", 0},

    [Q2M_CALLBACK_widow_attack_kick] = {callback_widow_attack_kick_23,
                                        Q2M_CALLBACK_widow_attack_kick, "widow_attack_kick", 0},

    [Q2M_CALLBACK_widow_attack_rail] = {callback_widow_attack_rail_14,
                                        Q2M_CALLBACK_widow_attack_rail, "widow_attack_rail", 0},

    [Q2M_CALLBACK_widow_done_spawn] = {callback_sequence_2, Q2M_CALLBACK_widow_done_spawn,
                                       "widow_done_spawn", 0},

    [Q2M_CALLBACK_widow_rail_done] = {callback_sequence_2, Q2M_CALLBACK_widow_rail_done,
                                      "widow_rail_done", 0},

    [Q2M_CALLBACK_widow_ready_spawn] = {callback_sequence_27, Q2M_CALLBACK_widow_ready_spawn,
                                        "widow_ready_spawn", 0},

    [Q2M_CALLBACK_widow_reattack_blaster] = {callback_sequence_2,
                                             Q2M_CALLBACK_widow_reattack_blaster,
                                             "widow_reattack_blaster", 0},

    [Q2M_CALLBACK_widow_run] = {callback_sequence_4, Q2M_CALLBACK_widow_run, "widow_run",
                                Q2M_CALLBACK_RUN},

    [Q2M_CALLBACK_widow_spawn_check] = {callback_sequence_27, Q2M_CALLBACK_widow_spawn_check,
                                        "widow_spawn_check", 0},

    [Q2M_CALLBACK_widow_start_rail] = {callback_sequence_2, Q2M_CALLBACK_widow_start_rail,
                                       "widow_start_rail", 0},

    [Q2M_CALLBACK_widow_start_run_10] = {callback_sequence_2, Q2M_CALLBACK_widow_start_run_10,
                                         "widow_start_run_10", 0},

    [Q2M_CALLBACK_widow_start_run_12] = {callback_sequence_2, Q2M_CALLBACK_widow_start_run_12,
                                         "widow_start_run_12", 0},

    [Q2M_CALLBACK_widow_start_run_5] = {callback_sequence_2, Q2M_CALLBACK_widow_start_run_5,
                                        "widow_start_run_5", 0},

    [Q2M_CALLBACK_widow_start_spawn] = {callback_sequence_27, Q2M_CALLBACK_widow_start_spawn,
                                        "widow_start_spawn", 0},

    [Q2M_CALLBACK_widow_step] = {callback_widow_step_65, Q2M_CALLBACK_widow_step, "widow_step", 0},

    [Q2M_CALLBACK_widow_stepshoot] = {callback_widow_stepshoot_66, Q2M_CALLBACK_widow_stepshoot,
                                      "widow_stepshoot", 0},

};

bool q2m_callback_call(q2m_context *context, const q2m_callback *callback, qa_error *error) {
    return !q2m_alive(context) || !callback || callback->invoke(context, callback->id, error);
}
bool q2m_callback_run(q2m_context *context, q2m_callback_id id, qa_error *error) {
    return q2m_callback_call(context, id == Q2M_CALLBACK_NONE ? NULL : q2m_callbacks + id, error);
}
