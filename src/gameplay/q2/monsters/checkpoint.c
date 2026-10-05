#include "internal.h"
#include "reinforcements.h"
#include "../entities/internal.h"

static bool copy_name(char *out, size_t capacity, const char *value,
                      qa_error *error) {
  size_t length = strlen(value);
  if (length == 0 || length >= capacity) {
    qa_error_set(error, QA_ERROR_FORMAT, length,
                 "Q2 monster checkpoint name is too long");
    return false;
  }
  memcpy(out, value, length + 1);
  return true;
}

static bool valid_name(const char *value, size_t capacity, bool allow_empty) {
  const char *end = memchr(value, '\0', capacity);
  return end != NULL && (allow_empty || end != value);
}

static bool reinforcement_bounds(qa_bounds bounds) {
  return qa_vec_finite(bounds.mins) && qa_vec_finite(bounds.maxs) &&
         bounds.mins.x <= bounds.maxs.x && bounds.mins.y <= bounds.maxs.y &&
         bounds.mins.z <= bounds.maxs.z;
}

static bool save_reinforcement(const q2m_reinforcement *entry,
                               qa_q2_reinforcement_checkpoint *out,
                               qa_error *error) {
  if (!copy_name(out->classname, sizeof(out->classname),
                  entry->definition->classname, error))
    return false;
  out->strength = entry->strength;
  out->bounds = entry->bounds;
  return true;
}

static bool restore_reinforcement(qa_q2_game *game,
                                  const qa_q2_reinforcement_checkpoint *saved,
                                  q2m_reinforcement *out, qa_error *error) {
  const q2m_definition *definition =
      valid_name(saved->classname, sizeof(saved->classname), false)
          ? q2m_definition_for(game, saved->classname) : NULL;
  if (!definition || !reinforcement_bounds(saved->bounds)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid saved medic reinforcement");
    return false;
  }
  *out = (q2m_reinforcement){definition, saved->strength, saved->bounds};
  return true;
}

static bool finite_checkpoint(const qa_q2_monster_checkpoint *state) {
  const float *values[] = {
      &state->entity_scale,     &state->animation_scale,
      &state->base_health,      &state->health_scaling,
      &state->max_health,
      &state->gib_health,       &state->normal_height,
      &state->view_height,      &state->ideal_yaw,
      &state->yaw_speed,        &state->blind_fire_delay,
      &state->fly_min_distance, &state->fly_max_distance,
      &state->fly_acceleration, &state->fly_speed,
      &state->pending_damage,   &state->pending_kick,
      &state->controller_damage,
  };
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    if (!isfinite(*values[i]))
      return false;
  return isfinite(state->max_power_armor) && qa_vec_finite(state->last_sighting) &&
         qa_vec_finite(state->saved_goal) &&
         qa_vec_finite(state->blind_fire_target) &&
         qa_vec_finite(state->fly_ideal_position) &&
         qa_vec_finite(state->fly_recovery_direction) &&
         qa_vec_finite(state->last_damage_point) &&
         qa_vec_finite(state->saved_attack_position) &&
         qa_vec_finite(state->widow_previous_target) &&
         qa_vec_finite(state->controller_direction) &&
         qa_vec_finite(state->sound_target.origin);
}

bool qa_q2_monster_read(const qa_q2_game *game, qa_actor_id id,
                        qa_q2_monster_view *out) {
  if (game == NULL || out == NULL || id.slot >= game->capacity)
    return false;
  const q2_actor *actor = game->actors[id.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, id) ||
      actor->monster == NULL || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      !q2_actor_live((qa_q2_game *)game, id))
    return false;
  const struct qa_q2_monster *monster = actor->monster;
  if (monster->definition == NULL ||
      monster->controller_kind != Q2M_CONTROLLER_NONE)
    return false;
  *out = (qa_q2_monster_view){
      .classname = monster->classname,
      .model = monster->model,
      .enemy = monster->enemy,
      .commander = monster->commander,
      .effects = actor->extra_effects,
      .frame = monster->frame,
      .old_frame = monster->old_frame,
      .render_flags = monster->render_flags,
      .skin = monster->skin,
      .scale = monster->entity_scale,
      .view_height = monster->view_height,
      .ideal_yaw = monster->ideal_yaw,
      .can_take_damage = monster->can_take_damage,
      .dead = monster->dead,
      .corpse = monster->corpse,
      .gibbed = monster->gibbed,
      .triggered = monster->triggered,
      .summoned = monster->summoned,
      .visible = monster->visible,
  };
  return true;
}

bool qa_q2_monster_capture(qa_q2_game *game, qa_actor_id id,
                           qa_q2_monster_checkpoint *out, qa_error *error) {
  if (game == NULL || out == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 monster capture requires game and output");
    return false;
  }
  if (!q2_checkpoint_idle(game, error))
    return false;
  q2_actor *actor = q2_actor_get(game, id, false, error);
  if (actor == NULL || actor->monster == NULL ||
      actor->projectile.kind != Q2_PROJECTILE_NONE) {
    qa_error_set(error, QA_ERROR_FORMAT, id.slot,
                 "Actor has no active Q2 monster state");
    return false;
  }
  const struct qa_q2_monster *monster = actor->monster;
  qa_q2_monster_checkpoint saved = {
      .start_phase = (uint32_t)monster->start_phase,
      .combat_target = monster->combat_target,
      .weapon_sound = monster->weapon_sound,
      .start_due_ns = monster->start_due_ns,
      .death_notified = monster->death_notified,
      .spawnflags = monster->spawnflags,
      .attack_state = (uint32_t)monster->attack_state,
      .spawned_by = (uint32_t)monster->spawned_by,
      .controller_kind = (uint32_t)monster->controller_kind,
      .frame = monster->frame,
      .next_frame = monster->next_frame,
      .old_frame = monster->old_frame,
      .render_flags = monster->render_flags,
      .skin = monster->skin,
      .style = monster->style,
      .turret_orientation = monster->turret_orientation,
      .count = monster->count,
      .entity_scale = monster->entity_scale,
      .animation_scale = monster->animation_scale,
      .base_health = monster->base_health,
      .health_scaling = monster->health_scaling,
      .max_health = monster->max_health,
      .max_power_armor = monster->max_power_armor,
      .initial_power_armor = (uint32_t)monster->initial_power_armor,
      .medic_tries = monster->medic_tries,
      .corpse_phase = (uint32_t)monster->corpse_phase,
      .corpse_due_ns = monster->corpse_due_ns,
      .corpse_end_ns = monster->corpse_end_ns,
      .gib_health = monster->gib_health,
      .normal_height = monster->normal_height,
      .view_height = monster->view_height,
      .ideal_yaw = monster->ideal_yaw,
      .yaw_speed = monster->yaw_speed,
      .blind_fire_delay = monster->blind_fire_delay,
      .fly_min_distance = monster->fly_min_distance,
      .fly_max_distance = monster->fly_max_distance,
      .fly_acceleration = monster->fly_acceleration,
      .fly_speed = monster->fly_speed,
      .next_frame_ns = monster->next_frame_ns,
      .pause_ns = monster->pause_ns,
      .idle_ns = monster->idle_ns,
      .pain_ns = monster->pain_ns,
      .fire_ns = monster->fire_ns,
      .duck_ns = monster->duck_ns,
      .next_duck_ns = monster->next_duck_ns,
      .dodge_ns = monster->dodge_ns,
      .attack_ns = monster->attack_ns,
      .check_attack_ns = monster->check_attack_ns,
      .strafe_ns = monster->strafe_ns,
      .melee_ns = monster->melee_ns,
      .search_ns = monster->search_ns,
      .trail_ns = monster->trail_ns,
      .hostile_ns = monster->hostile_ns,
      .air_ns = monster->air_ns,
      .environment_ns = monster->environment_ns,
      .jump_ns = monster->jump_ns,
      .flies_ns = monster->flies_ns,
      .fly_position_ns = monster->fly_position_ns,
      .recovery_ns = monster->recovery_ns,
      .death_ns = monster->death_ns,
      .spawn_ns = monster->spawn_ns,
      .timestamp_ns = monster->timestamp_ns,
      .coop_check_ns = monster->coop_check_ns,
      .react_ns = monster->react_ns,
      .widow_powers = monster->widow_powers,
      .sound_target = {.origin = monster->sound_target.origin,
                       .time_ns = monster->sound_target.time_ns,
                       .present = monster->sound_target.present},
      .last_sighting = monster->last_sighting,
      .saved_goal = monster->saved_goal,
      .blind_fire_target = monster->blind_fire_target,
      .fly_ideal_position = monster->fly_ideal_position,
      .fly_recovery_direction = monster->fly_recovery_direction,
      .last_damage_point = monster->last_damage_point,
      .saved_attack_position = monster->saved_attack_position,
      .widow_previous_target = monster->widow_previous_target,
      .controller_direction = monster->controller_direction,
      .last_attack = monster->last_attack,
      .pending_damage = monster->pending_damage,
      .pending_kick = monster->pending_kick,
      .controller_damage = monster->controller_damage,
      .controller_ns = monster->controller_ns,
      .monster_slots = monster->monster_slots,
      .monster_used = monster->monster_used,
      .water_level = monster->water_level,
      .water_type = monster->water_type,
      .last_link_count = monster->last_link_count,
      .has_saved_goal = monster->has_saved_goal,
      .good_guy = monster->good_guy,
      .target_anger = monster->target_anger,
      .ignore_shots = monster->ignore_shots,
      .do_not_count = monster->do_not_count,
      .brutal = monster->brutal,
      .medic = monster->medic,
      .resurrecting = monster->resurrecting,
      .can_take_damage = monster->can_take_damage,
      .dead = monster->dead,
      .corpse = monster->corpse,
      .gibbed = monster->gibbed,
      .stand_ground = monster->stand_ground,
      .temporary_stand_ground = monster->temporary_stand_ground,
      .hold_frame = monster->hold_frame,
      .source_blocked = monster->source_blocked,
      .ducked = monster->ducked,
      .dodging = monster->dodging,
      .charging = monster->charging,
      .manual_steering = monster->manual_steering,
      .combat_point = monster->combat_point,
      .lefty = monster->lefty,
      .had_visibility = monster->had_visibility,
      .close_sight_tripped = monster->close_sight_tripped,
      .lost_sight = monster->lost_sight,
      .pursue_next = monster->pursue_next,
      .pursue_temporary = monster->pursue_temporary,
      .pursuit_last_seen = monster->pursuit_last_seen,
      .cocked = monster->cocked,
      .force_refire = monster->force_refire,
      .triggered = monster->triggered,
      .visible = monster->visible,
      .pending_pain = monster->pending_pain,
      .pending_death = monster->pending_death,
      .alternate_fly = monster->alternate_fly,
      .fly_buzzard = monster->fly_buzzard,
      .fly_above = monster->fly_above,
      .fly_pinned = monster->fly_pinned,
      .fly_thrusters = monster->fly_thrusters,
      .hint_path = monster->hint_path,
      .summoned = monster->summoned,
      .touch_active = monster->touch_active,
      .turret_attached = monster->turret_attached,
      .initialized = monster->initialized,
      .controller_medic = monster->controller_medic,
      .controller_fired = monster->controller_fired,
  };
  if (monster->controller_kind != Q2M_CONTROLLER_NONE) {
    if (!q2_save_reference(game, monster->controller_owner,
                        &saved.controller_owner, error) ||
        !q2_save_reference(game, monster->controller_target,
                        &saved.controller_target, error))
      return false;
    *out = saved;
    return true;
  }
  if (!copy_name(saved.definition, sizeof(saved.definition),
                 monster->definition->classname, error) ||
      !copy_name(saved.move, sizeof(saved.move), q2m_move_name(monster->move), error))
    return false;
  if (monster->next_move != NULL &&
      !copy_name(saved.next_move, sizeof(saved.next_move),
                 q2m_move_name(monster->next_move), error))
    return false;
  if (monster->summons) {
    const q2m_summon_state *summons = monster->summons;
    saved.has_summons = true;
    saved.summon_strength = summons->classic_strength;
    saved.summon_count = (uint32_t)summons->chosen_count;
    saved.reinforcement_source = summons->authored;
    saved.reinforcements_configured = summons->configured;
    for (size_t i = 0; i < summons->chosen_count; ++i) {
      if (!save_reinforcement(summons->chosen + i, saved.summons + i, error))
        return false;
    }
  }
  if (!q2_save_reference(game, monster->enemy, &saved.enemy, error) ||
      !q2_save_reference(game, monster->old_enemy, &saved.old_enemy, error) ||
      !q2_save_reference(game, monster->last_player_enemy,
                      &saved.last_player_enemy, error) ||
      !q2_save_reference(game, monster->goal, &saved.goal, error) ||
      !q2_save_reference(game, monster->move_target, &saved.move_target, error) ||
      !q2_save_reference(game, monster->commander, &saved.commander, error) ||
      !q2_save_reference(game, monster->activator, &saved.activator, error) ||
      !q2_save_reference(game, monster->resurrect_target, &saved.resurrect_target,
                      error) ||
      !q2_save_reference(game, monster->hazard, &saved.hazard, error) ||
      !q2_save_reference(game, monster->proboscis, &saved.proboscis, error) ||
      !q2_save_reference(game, monster->healer, &saved.healer, error) ||
      !q2_save_reference(game, monster->bad_medic[0], &saved.bad_medic[0], error) ||
      !q2_save_reference(game, monster->bad_medic[1], &saved.bad_medic[1], error) ||
      !q2_save_reference(game, monster->sound_target.actor,
                      &saved.sound_target.actor, error) ||
      !q2_save_reference(game, monster->sound_target.owner,
                      &saved.sound_target.owner, error) ||
      !q2_save_reference(game, monster->last_attack.attacker,
                      &saved.attack_attacker, error) ||
      !q2_save_reference(game, monster->last_attack.inflictor,
                      &saved.attack_inflictor, error) ||
      !q2_save_reference(game, monster->last_attack.projectile,
                      &saved.attack_projectile, error))
    return false;
  saved.last_attack.attacker = (qa_actor_id){0};
  saved.last_attack.inflictor = (qa_actor_id){0};
  saved.last_attack.projectile = (qa_actor_id){0};
  if (monster->summons && monster->summons->entry_count) {
    const q2m_summon_state *summons = monster->summons;
    if (summons->entry_count > UINT32_MAX ||
        summons->entry_count > SIZE_MAX / sizeof(*saved.reinforcements)) {
      qa_error_set(error, QA_ERROR_MEMORY, 0, "Medic reinforcement catalog is too large");
      return false;
    }
    saved.reinforcements = calloc(summons->entry_count, sizeof(*saved.reinforcements));
    if (!saved.reinforcements) {
      qa_error_set(error, QA_ERROR_MEMORY, 0, "Capturing medic reinforcement catalog");
      return false;
    }
    saved.reinforcement_count = summons->entry_count;
    for (size_t i = 0; i < summons->entry_count; ++i)
      if (!save_reinforcement(summons->entries + i, saved.reinforcements + i, error)) {
        qa_q2_monster_checkpoint_free(&saved);
        return false;
      }
  }
  *out = saved;
  return true;
}

void qa_q2_monster_checkpoint_free(qa_q2_monster_checkpoint *state) {
  if (state) {
    free(state->reinforcements);
    *state = (qa_q2_monster_checkpoint){0};
  }
}

static bool resolve_all(qa_q2_game *game, const qa_q2_monster_checkpoint *saved,
                        struct qa_q2_monster *monster, qa_error *error) {
  return q2_resolve_reference(game, saved->enemy, &monster->enemy, error) &&
         q2_resolve_reference(game, saved->old_enemy, &monster->old_enemy,
                           error) &&
         q2_resolve_reference(game, saved->last_player_enemy,
                           &monster->last_player_enemy, error) &&
         q2_resolve_reference(game, saved->goal, &monster->goal, error) &&
         q2_resolve_reference(game, saved->move_target, &monster->move_target,
                           error) &&
         q2_resolve_reference(game, saved->commander, &monster->commander,
                           error) &&
         q2_resolve_reference(game, saved->activator, &monster->activator,
                           error) &&
         q2_resolve_reference(game, saved->resurrect_target,
                           &monster->resurrect_target, error) &&
         q2_resolve_reference(game, saved->hazard, &monster->hazard, error) &&
         q2_resolve_reference(game, saved->proboscis, &monster->proboscis, error) &&
         q2_resolve_reference(game, saved->healer, &monster->healer, error) &&
         q2_resolve_reference(game, saved->bad_medic[0], &monster->bad_medic[0], error) &&
         q2_resolve_reference(game, saved->bad_medic[1], &monster->bad_medic[1], error) &&
         q2_resolve_reference(game, saved->controller_owner,
                           &monster->controller_owner, error) &&
         q2_resolve_reference(game, saved->controller_target,
                           &monster->controller_target, error) &&
         q2_resolve_reference(game, saved->sound_target.actor,
                           &monster->sound_target.actor, error) &&
         q2_resolve_reference(game, saved->sound_target.owner,
                           &monster->sound_target.owner, error) &&
         q2_resolve_reference(game, saved->attack_attacker,
                           &monster->last_attack.attacker, error) &&
         q2_resolve_reference(game, saved->attack_inflictor,
                           &monster->last_attack.inflictor, error) &&
         q2_resolve_reference(game, saved->attack_projectile,
                           &monster->last_attack.projectile, error);
}

bool qa_q2_monster_restore(qa_q2_game *game, qa_actor_id id,
                           const qa_q2_monster_checkpoint *saved,
                           qa_error *error) {
  if (game == NULL || saved == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 monster restore requires game and checkpoint");
    return false;
  }
  if (!q2_checkpoint_idle(game, error))
    return false;
  bool controller = saved->controller_kind != Q2M_CONTROLLER_NONE;
  if (saved->start_phase > Q2M_START_MANUAL ||
      saved->reinforcement_count > UINT32_MAX ||
      saved->reinforcement_count > SIZE_MAX / sizeof(q2m_reinforcement) ||
      (saved->reinforcement_count && !saved->reinforcements) ||
      (saved->reinforcement_source &&
       !qa_strings_cstr(qa_session_strings(game->services.session),
                         saved->reinforcement_source)) ||
      ((!saved->has_summons || !saved->reinforcements_configured) &&
       (saved->reinforcement_source || saved->reinforcement_count)) ||
      (!saved->has_summons && saved->reinforcements_configured) ||
      (game->options.edition == QA_Q2_CLASSIC &&
       (saved->reinforcements_configured || saved->reinforcement_count)) ||
      (saved->weapon_sound && !qa_strings_text(qa_session_strings(game->services.session),
                                               saved->weapon_sound).size) ||
      (controller && saved->weapon_sound) ||
      saved->corpse_phase > Q2M_CORPSE_HOVER ||
      (saved->corpse_phase != Q2M_CORPSE_IDLE && !saved->corpse) ||
      saved->initial_power_armor > QA_POWER_SHIELD || saved->max_power_armor < 0 ||
      saved->medic_tries > 2 ||
      saved->controller_kind > Q2M_CONTROLLER_BOT_GOAL ||
      ((saved->controller_kind == Q2M_CONTROLLER_VISUAL_CHILD ||
        saved->controller_kind == Q2M_CONTROLLER_BOT_GOAL) &&
       (!saved->controller_owner.present || saved->count < 0 ||
        saved->count > (saved->controller_kind == Q2M_CONTROLLER_VISUAL_CHILD ? 1 : 0))) ||
      (saved->controller_kind == Q2M_CONTROLLER_GUARDIAN_BEAM &&
       (game->options.edition != QA_Q2_RERELEASE || saved->count < 0 || saved->count > 1 ||
        !saved->controller_owner.present || saved->controller_damage != 25.0f)) ||
      !valid_name(saved->definition, sizeof(saved->definition), controller) ||
      !valid_name(saved->move, sizeof(saved->move), controller) ||
      !valid_name(saved->next_move, sizeof(saved->next_move), true) ||
      saved->attack_state > Q2M_BLIND || saved->spawned_by > Q2M_SPAWN_WIDOW ||
      (!controller &&
       (saved->entity_scale <= 0.0f || saved->animation_scale <= 0.0f ||
        saved->health_scaling <= 0.0f || saved->normal_height < 0.0f ||
        saved->fly_min_distance < 0.0f ||
        saved->fly_max_distance < saved->fly_min_distance ||
        saved->fly_acceleration < 0.0f || saved->fly_speed < 0.0f ||
        saved->water_level < 0 || saved->water_level > 3)) ||
      !saved->initialized ||
      saved->last_attack.attacker.registry != 0 ||
      saved->last_attack.inflictor.registry != 0 ||
      saved->last_attack.projectile.registry != 0 ||
      !finite_checkpoint(saved)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q2 monster checkpoint");
    return false;
  }
  if (controller &&
      (saved->definition[0] != '\0' || saved->move[0] != '\0' ||
       saved->next_move[0] != '\0' || saved->has_summons || saved->summon_count ||
       saved->summon_strength || saved->corpse_phase != Q2M_CORPSE_IDLE)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Invalid Q2 monster controller checkpoint");
    return false;
  }
  if ((saved->controller_kind == Q2M_CONTROLLER_VISUAL_CHILD ||
       saved->controller_kind == Q2M_CONTROLLER_BOT_GOAL) &&
      !q2_ent(game, id)) {
    qa_error_set(error, QA_ERROR_FORMAT, id.slot, "Q2 Source child lost its native entity continuation");
    return false;
  }
  if (controller) {
    struct qa_q2_monster *monster = calloc(1, sizeof(*monster));
    if (monster == NULL) {
      qa_error_set(error, QA_ERROR_MEMORY, 0,
                   "Q2 monster controller checkpoint allocation failed");
      return false;
    }
    monster->controller_kind = (q2m_controller_kind)saved->controller_kind;
    monster->count = saved->count;
    monster->style = saved->style;
    monster->controller_direction = saved->controller_direction;
    monster->controller_damage = saved->controller_damage;
    monster->controller_ns = saved->controller_ns;
    monster->controller_medic = saved->controller_medic;
    monster->controller_fired = saved->controller_fired;
    monster->initialized = true;
    if (!q2_resolve_reference(game, saved->controller_owner,
                           &monster->controller_owner, error) ||
        !q2_resolve_reference(game, saved->controller_target,
                           &monster->controller_target, error)) {
      q2m_free_monster(monster);
      return false;
    }
    q2_actor *actor = q2_actor_get(game, id, true, error);
    if (actor == NULL) {
      q2m_free_monster(monster);
      return false;
    }
    if (actor->projectile.kind != Q2_PROJECTILE_NONE) {
      q2m_free_monster(monster);
      qa_error_set(error, QA_ERROR_FORMAT, id.slot,
                   "Projectile actor cannot restore monster controller state");
      return false;
    }
    struct qa_q2_monster *previous = actor->monster;
    actor->monster = monster;
    actor->physics_bound = true;
    actor->physics = qa_physics_properties_default(QA_COLLISION_Q2);
    actor->physics.motion = QA_PHYSICS_STATIONARY;
    actor->physics.solid = QA_PHYSICS_NOT_SOLID;
    actor->physics.clip_mask = 0;
    if (previous)
      q2m_retire_monster(game, previous);
    return true;
  }
  const q2m_definition *definition =
      q2m_definition_for(game, saved->definition);
  const q2m_move_set *move_set = q2m_move_set_for(game, definition);
  if (definition == NULL || move_set == NULL) {
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Q2 monster checkpoint definition is unavailable");
    return false;
  }
  if (definition->species == Q2M_WIDOW2 && saved->death_ns != 0 &&
      (!saved->dead || saved->count < 0 || saved->count > 12)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Widow explosion checkpoint");
    return false;
  }
  if (!q2m_corpse_phase_valid(game, definition->species,
                              (q2m_corpse_phase)saved->corpse_phase)) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Q2 corpse continuation differs from its source species");
    return false;
  }
  struct qa_q2_monster *monster = calloc(1, sizeof(*monster));
  if (monster == NULL) {
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Q2 monster checkpoint allocation failed");
    return false;
  }
  monster->definition = definition;
  monster->move_set = move_set;
  monster->move = q2m_move_named(monster, saved->move);
  monster->next_move = saved->next_move[0] == '\0'
                           ? NULL
                           : q2m_move_named(monster, saved->next_move);
  /* AI can switch moves after this frame ran. The next animation tick
   * rebases that retained frame against the selected move. */
  if (monster->move == NULL ||
      (saved->next_move[0] != '\0' && monster->next_move == NULL) ||
      saved->frame < 0 || saved->next_frame < 0) {
    q2m_free_monster(monster);
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Q2 monster checkpoint move is invalid");
    return false;
  }
#define Q2M_RESTORE(field) monster->field = saved->field
  Q2M_RESTORE(spawnflags);
  Q2M_RESTORE(frame);
  Q2M_RESTORE(next_frame);
  Q2M_RESTORE(skin);
  Q2M_RESTORE(style);
  Q2M_RESTORE(turret_orientation);
  Q2M_RESTORE(count);
  Q2M_RESTORE(weapon_sound);
  Q2M_RESTORE(entity_scale);
  Q2M_RESTORE(animation_scale);
  Q2M_RESTORE(base_health);
  Q2M_RESTORE(health_scaling);
  Q2M_RESTORE(max_health);
  Q2M_RESTORE(max_power_armor);
  monster->initial_power_armor = (qa_power_kind)saved->initial_power_armor;
  Q2M_RESTORE(medic_tries);
  monster->corpse_phase = (q2m_corpse_phase)saved->corpse_phase;
  Q2M_RESTORE(corpse_due_ns);
  Q2M_RESTORE(corpse_end_ns);
  Q2M_RESTORE(gib_health);
  Q2M_RESTORE(normal_height);
  Q2M_RESTORE(view_height);
  Q2M_RESTORE(ideal_yaw);
  Q2M_RESTORE(yaw_speed);
  Q2M_RESTORE(blind_fire_delay);
  Q2M_RESTORE(fly_min_distance);
  Q2M_RESTORE(fly_max_distance);
  Q2M_RESTORE(fly_acceleration);
  Q2M_RESTORE(fly_speed);
  Q2M_RESTORE(next_frame_ns);
  Q2M_RESTORE(pause_ns);
  Q2M_RESTORE(idle_ns);
  Q2M_RESTORE(pain_ns);
  Q2M_RESTORE(fire_ns);
  Q2M_RESTORE(duck_ns);
  Q2M_RESTORE(next_duck_ns);
  Q2M_RESTORE(dodge_ns);
  Q2M_RESTORE(attack_ns);
  Q2M_RESTORE(check_attack_ns);
  Q2M_RESTORE(strafe_ns);
  Q2M_RESTORE(melee_ns);
  Q2M_RESTORE(search_ns);
  Q2M_RESTORE(trail_ns);
  Q2M_RESTORE(hostile_ns);
  Q2M_RESTORE(air_ns);
  Q2M_RESTORE(environment_ns);
  Q2M_RESTORE(jump_ns);
  Q2M_RESTORE(flies_ns);
  Q2M_RESTORE(fly_position_ns);
  Q2M_RESTORE(recovery_ns);
  Q2M_RESTORE(death_ns);
  Q2M_RESTORE(spawn_ns);
  Q2M_RESTORE(timestamp_ns);
  Q2M_RESTORE(coop_check_ns);
  Q2M_RESTORE(react_ns);
  Q2M_RESTORE(widow_powers);
  Q2M_RESTORE(old_frame);
  Q2M_RESTORE(render_flags);
  monster->start_phase = (q2m_start_phase)saved->start_phase;
  Q2M_RESTORE(combat_target);
  Q2M_RESTORE(start_due_ns);
  Q2M_RESTORE(death_notified);
  Q2M_RESTORE(last_sighting);
  Q2M_RESTORE(saved_goal);
  Q2M_RESTORE(blind_fire_target);
  Q2M_RESTORE(fly_ideal_position);
  Q2M_RESTORE(fly_recovery_direction);
  Q2M_RESTORE(last_damage_point);
  Q2M_RESTORE(saved_attack_position);
  Q2M_RESTORE(widow_previous_target);
  Q2M_RESTORE(last_attack);
  Q2M_RESTORE(pending_damage);
  Q2M_RESTORE(pending_kick);
  Q2M_RESTORE(monster_slots);
  Q2M_RESTORE(monster_used);
  Q2M_RESTORE(water_level);
  Q2M_RESTORE(water_type);
  Q2M_RESTORE(last_link_count);
  Q2M_RESTORE(has_saved_goal);
  Q2M_RESTORE(good_guy);
  Q2M_RESTORE(target_anger);
  Q2M_RESTORE(ignore_shots);
  Q2M_RESTORE(do_not_count);
  Q2M_RESTORE(brutal);
  Q2M_RESTORE(medic);
  Q2M_RESTORE(resurrecting);
  Q2M_RESTORE(can_take_damage);
  Q2M_RESTORE(dead);
  Q2M_RESTORE(corpse);
  Q2M_RESTORE(gibbed);
  Q2M_RESTORE(stand_ground);
  Q2M_RESTORE(temporary_stand_ground);
  Q2M_RESTORE(hold_frame);
  Q2M_RESTORE(source_blocked);
  Q2M_RESTORE(ducked);
  Q2M_RESTORE(dodging);
  Q2M_RESTORE(charging);
  Q2M_RESTORE(manual_steering);
  Q2M_RESTORE(combat_point);
  Q2M_RESTORE(lefty);
  Q2M_RESTORE(had_visibility);
  Q2M_RESTORE(close_sight_tripped);
  Q2M_RESTORE(lost_sight);
  Q2M_RESTORE(pursue_next);
  Q2M_RESTORE(pursue_temporary);
  Q2M_RESTORE(pursuit_last_seen);
  Q2M_RESTORE(cocked);
  Q2M_RESTORE(force_refire);
  Q2M_RESTORE(triggered);
  Q2M_RESTORE(visible);
  Q2M_RESTORE(pending_pain);
  Q2M_RESTORE(pending_death);
  Q2M_RESTORE(alternate_fly);
  Q2M_RESTORE(fly_buzzard);
  Q2M_RESTORE(fly_above);
  Q2M_RESTORE(fly_pinned);
  Q2M_RESTORE(fly_thrusters);
  Q2M_RESTORE(hint_path);
  Q2M_RESTORE(summoned);
  Q2M_RESTORE(touch_active);
  Q2M_RESTORE(turret_attached);
  Q2M_RESTORE(initialized);
#undef Q2M_RESTORE
  monster->attack_state = (q2m_attack_state)saved->attack_state;
  monster->spawned_by = (q2m_spawned_by)saved->spawned_by;
  monster->sound_target.origin = saved->sound_target.origin;
  monster->sound_target.time_ns = saved->sound_target.time_ns;
  monster->sound_target.present = saved->sound_target.present;
  bool source_medic = definition->species == Q2M_MEDIC_COMMANDER ||
      (definition->species == Q2M_MEDIC && (game->options.edition == QA_Q2_RERELEASE ||
                                           game->options.product == QA_Q2_ROGUE));
  if (saved->summon_count > 5 || saved->summon_strength < 0 || saved->summon_strength > 6 ||
      saved->has_summons != source_medic ||
      (!saved->has_summons && (saved->summon_count || saved->summon_strength))) {
    q2m_free_monster(monster);
    qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid retained medic reinforcement state");
    return false;
  }
  if (saved->has_summons) {
    monster->summons = calloc(1, sizeof(*monster->summons));
    if (!monster->summons) {
      q2m_free_monster(monster);
      qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring medic reinforcement choices");
      return false;
    }
    monster->summons->classic_strength = saved->summon_strength;
    monster->summons->chosen_count = saved->summon_count;
    monster->summons->authored = saved->reinforcement_source;
    monster->summons->configured = saved->reinforcements_configured;
    if (saved->reinforcement_count) {
      monster->summons->entries =
          calloc(saved->reinforcement_count, sizeof(*monster->summons->entries));
      if (!monster->summons->entries) {
        q2m_free_monster(monster);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring medic reinforcement catalog");
        return false;
      }
      monster->summons->entry_count = saved->reinforcement_count;
      for (size_t i = 0; i < saved->reinforcement_count; ++i)
        if (!restore_reinforcement(game, saved->reinforcements + i,
                                    monster->summons->entries + i, error)) {
          q2m_free_monster(monster);
          return false;
        }
    }
    for (size_t i = 0; i < saved->summon_count; ++i) {
      const qa_q2_reinforcement_checkpoint *choice = &saved->summons[i];
      if (!restore_reinforcement(game, choice, monster->summons->chosen + i, error)) {
        q2m_free_monster(monster);
        return false;
      }
    }
  }
  if (!resolve_all(game, saved, monster, error) ||
      !qa_builtin_resource(&game->services, definition->classname,
                           &monster->classname, error) ||
      !qa_builtin_resource(&game->services, definition->model, &monster->model,
                           error)) {
    q2m_free_monster(monster);
    return false;
  }
  q2_actor *actor = q2_actor_get(game, id, true, error);
  if (actor == NULL) {
    q2m_free_monster(monster);
    return false;
  }
  if (actor->projectile.kind != Q2_PROJECTILE_NONE) {
    q2m_free_monster(monster);
    qa_error_set(error, QA_ERROR_FORMAT, id.slot,
                 "Projectile actor cannot restore active monster state");
    return false;
  }
  struct qa_q2_monster *previous = actor->monster;
  actor->monster = monster;
  actor->physics_bound = true;
  if (previous)
    q2m_retire_monster(game, previous);
  return true;
}
