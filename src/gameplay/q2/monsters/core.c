#include "internal.h"
#include "reinforcements.h"
#include "medic.h"

static bool actor_current(const q2m_context *context) {
  qa_actor_id id = context->actor->id;
  return q2_actor_live(context->game, id) &&
         id.slot < context->game->capacity &&
         context->game->actors[id.slot] == context->actor &&
         context->actor->monster == context->monster;
}

uint64_t q2m_after(uint64_t now, double seconds) {
  if (!(seconds > 0.0))
    return now;
  long double interval = seconds * (long double)Q2M_SECOND;
  if (interval >= (long double)(UINT64_MAX - now))
    return UINT64_MAX;
  return now + (uint64_t)llroundl(interval);
}

bool q2m_alive(const q2m_context *context) {
  return context != NULL && context->game != NULL && context->actor != NULL &&
         context->monster != NULL && actor_current(context);
}

bool qa_q2_monster_holds_healthbar(const qa_q2_game *game, qa_actor_id id) {
  if (game == NULL || id.registry == 0 || id.slot >= game->capacity ||
      !q2_actor_live((qa_q2_game *)game, id))
    return false;
  const q2_actor *actor = game->actors[id.slot];
  return actor != NULL && qa_actor_id_equal(actor->id, id) &&
         actor->projectile.kind == Q2_PROJECTILE_NONE &&
         actor->monster != NULL && actor->monster->definition != NULL &&
         strcmp(actor->monster->definition->classname, "monster_jorg") == 0;
}

bool q2m_refresh(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return false;
  if (!qa_world_body_read(context->game->services.world, context->actor->id,
                          &context->body, error) || !q2m_alive(context) ||
      !qa_combat_read(context->game->services.combat, context->actor->id,
                      &context->combat, error) || !q2m_alive(context))
    return false;
  context->elapsed = (float)((double)context->game->frame_ns / 1e9);
  return true;
}

bool q2m_write_body(q2m_context *context, bool link, qa_error *error) {
  if (!q2m_alive(context) ||
      !qa_world_body_write(context->game->services.world, context->actor->id,
                           &context->body, error))
    return false;
  if (!link)
    return true;
  return q2m_link(context, error);
}

bool q2m_damageable(q2m_context *context, bool enabled, qa_error *error) {
  qa_combat_state traits;
  if (!qa_combat_read_traits(context->game->services.combat, context->actor->id,
                             &traits, error))
    return false;
  if (!q2m_alive(context))
    return true;
  traits.can_take_damage = enabled;
  if (!qa_combat_set_traits(context->game->services.combat, context->actor->id,
                            &traits, error))
    return false;
  if (!q2m_alive(context))
    return true;
  context->combat.can_take_damage = enabled;
  context->monster->can_take_damage = enabled;
  return true;
}

bool q2m_link(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context) || !qa_world_link(context->game->services.world,
                                            context->actor->id, NULL, error))
    return false;
  if (!q2m_alive(context))
    return true;
  qa_linked_body linked;
  if (qa_world_linked(context->game->services.world, context->actor->id,
                      &linked))
    context->monster->last_link_count = linked.link_count;
  return true;
}

bool q2m_emit(q2m_context *context, qa_builtin_event_kind kind,
              const char *resource, int code, qa_vec3 origin, qa_vec3 end,
              float value, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  qa_builtin_event event = {
      .kind = kind,
      .family = QA_GAME_Q2,
      .provider = context->game->options.owner,
      .actor = context->actor->id,
      .other = context->monster->enemy,
      .time_ns = context->game->now_ns,
      .origin = origin,
      .end = end,
      .direction = qa_vec_normalize(qa_vec_sub(end, origin)),
      .volume = 1.0f,
      .attenuation = 1.0f,
      .value = value,
      .code = code,
      .frame = context->monster->frame,
  };
  if (resource != NULL &&
      !qa_builtin_resource(&context->game->services, resource, &event.resource,
                           error))
    return false;
  return qa_builtin_emit(&context->game->services, &event, error);
}

bool q2m_sound(q2m_context *context, const char *path, int channel,
               float attenuation, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  qa_builtin_event event = {
      .kind = QA_BUILTIN_SOUND,
      .family = QA_GAME_Q2,
      .provider = context->game->options.owner,
      .actor = context->actor->id,
      .other = context->monster->enemy,
      .time_ns = context->game->now_ns,
      .origin = context->body.origin,
      .volume = 1.0f,
      .attenuation = attenuation,
      .channel = channel,
      .frame = context->monster->frame,
  };
  if (!qa_builtin_resource(&context->game->services, path, &event.resource,
                           error))
    return false;
  return qa_builtin_emit(&context->game->services, &event, error);
}

const q2m_frame *q2m_frame_at(const struct qa_q2_monster *monster, const q2m_move *move,
                              int frame, qa_error *error) {
  const q2m_move_set *set = monster->move_set;
  size_t ordinal = (size_t)(move - set->moves);
  size_t limit = ordinal + 1 < set->move_count ? set->moves[ordinal + 1].frame_first
                                               : set->frame_count;
  if (limit < move->frame_first || limit > set->frame_count ||
      frame < move->first_frame || frame > move->last_frame ||
      (size_t)(frame - move->first_frame) >= limit - move->frame_first) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s move %s has no source frame %d",
                  monster->definition->classname, move->name, frame);
    return NULL;
  }
  const q2m_frame *entry = &set->frames[move->frame_first + (size_t)(frame - move->first_frame)];
  if ((size_t)entry->action_first + entry->action_count > set->action_count) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s move %s has an invalid action table",
                  monster->definition->classname, move->name);
    return NULL;
  }
  return entry;
}

bool q2m_animation(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;

  struct qa_q2_monster *monster = context->monster;
  const bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  const q2m_move *move = monster->move;
  bool run_frame =
      !rerelease || monster->next_frame_ns <= context->game->now_ns;

  if (run_frame && monster->next_move != NULL &&
      monster->next_move != monster->move) {
    monster->move = monster->next_move;
    monster->next_move = NULL;
    move = monster->move;
  }
  if (!run_frame)
    run_frame =
        monster->frame < move->first_frame || monster->frame > move->last_frame;

  if (run_frame) {
    bool explicit_frame = false;
    if (monster->next_frame != 0 && monster->next_frame >= move->first_frame &&
        monster->next_frame <= move->last_frame) {
      monster->frame = monster->next_frame;
      monster->next_frame = 0;
    } else {
      if (monster->frame == move->last_frame && move->end != NULL) {
        if (!q2m_dispatch(context, move->end, error))
          return false;
        if (!q2m_alive(context))
          return true;
        monster = context->monster;
        if (rerelease && monster->next_move != NULL) {
          monster->move = monster->next_move;
          monster->next_move = NULL;
          if (monster->next_frame != 0) {
            monster->frame = monster->next_frame;
            monster->next_frame = 0;
            explicit_frame = true;
          }
        }
        move = monster->move;
        if (monster->corpse || monster->gibbed)
          return true;
      }
      if (monster->frame < move->first_frame ||
          monster->frame > move->last_frame) {
        monster->hold_frame = false;
        monster->frame = move->first_frame;
      } else if (!explicit_frame && !monster->hold_frame) {
        if (++monster->frame > move->last_frame)
          monster->frame = move->first_frame;
      }
    }
    monster->next_frame_ns = q2m_after(context->game->now_ns, 0.1);
    if (rerelease && monster->next_frame != 0 &&
        (monster->next_frame < move->first_frame ||
         monster->next_frame > move->last_frame))
      monster->next_frame = 0;
  }

  const q2m_frame *source_frame = q2m_frame_at(monster, move, monster->frame, error);
  if (!source_frame)
    return false;
  const q2m_frame frame = *source_frame;
  float distance = monster->hold_frame
                       ? 0.0f
                       : frame.distance * monster->animation_scale *
                             (rerelease ? context->elapsed * 10.0f : 1.0f);
  if (!q2m_run_ai(context, frame.ai, frame.source_ai, distance, error))
    return false;
  if (!q2m_alive(context))
    return true;

  if (run_frame) {
    for (uint16_t i = 0; i < frame.action_count; ++i) {
      const q2m_frame_action *action =
          &monster->move_set->actions[frame.action_first + i];
      if (action->callback != NULL) {
        if (!q2m_dispatch(context, action->callback, error))
          return false;
      } else if (action->next_frame == INT_MAX) {
        monster->next_frame = monster->frame + 1;
      } else if (action->next_frame != INT_MIN) {
        monster->next_frame = action->next_frame;
      }
      if (!q2m_alive(context))
        return true;
    }
  }
  if (rerelease && frame.lerp_frame != -1) {
    monster->render_flags |= UINT32_C(1) << 22;
    monster->old_frame = frame.lerp_frame;
  }
  return true;
}

bool q2m_set_move(q2m_context *context, const char *name, bool immediate,
                  qa_error *error) {
  const q2m_move *move = q2m_move_named(context->monster, name);
  if (move == NULL) {
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "%s references missing monster move %s",
                 context->monster->definition->classname,
                 name == NULL ? "(null)" : name);
    return false;
  }
  if (context->game->options.edition == QA_Q2_RERELEASE && !immediate) {
    context->monster->next_move = move;
  } else {
    context->monster->move = move;
    context->monster->next_move = NULL;
  }
  return true;
}

static bool species_is_soldier(q2m_species species) {
  return species == Q2M_SOLDIER_LIGHT || species == Q2M_SOLDIER ||
         species == Q2M_SOLDIER_SS;
}

static bool species_is_soldierh(q2m_species species) {
  return species == Q2M_SOLDIER_RIPPER || species == Q2M_SOLDIER_HYPER ||
         species == Q2M_SOLDIER_LASER;
}

static bool move_is(const struct qa_q2_monster *monster, const char *name) {
  return monster->move != NULL && monster->move->name != NULL &&
         strcmp(monster->move->name, name) == 0;
}

static void finish_dodge(struct qa_q2_monster *monster) {
  monster->dodging = false;
  if (monster->attack_state == Q2M_SLIDING)
    monster->attack_state = Q2M_STRAIGHT;
}

static bool set_duck_bounds(q2m_context *context, bool down, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (!down && !monster->ducked)
    return true;
  monster->ducked = down;
  monster->can_take_damage = true;
  context->body.bounds.maxs.z = monster->normal_height - (down ? 32.0f : 0.0f);
  return q2m_damageable(context, true, error) &&
         q2m_write_body(context, true, error);
}

static void dodge_capabilities(const q2m_context *context, bool *duck,
                               bool *sidestep) {
  const q2m_species species = context->monster->definition->species;
  const bool rerelease = context->game->options.edition == QA_Q2_RERELEASE;
  const bool rogue = context->game->options.product == QA_Q2_ROGUE;

  *duck = false;
  *sidestep = false;
  if (rerelease) {
    if (context->game->options.product == QA_Q2_XATRIX &&
        (species == Q2M_GEKK || species_is_soldierh(species))) {
      *duck = true;
      return;
    }
    if (context->game->options.product == QA_Q2_ROGUE &&
        species == Q2M_STALKER) {
      *sidestep = true;
      return;
    }
    if (species == Q2M_INFANTRY || species_is_soldier(species)) {
      *duck = true;
      *sidestep = true;
    } else if (species == Q2M_BERSERK) {
      *duck = true;
      *sidestep = true;
    } else if (species == Q2M_BRAIN) {
      *duck = true;
    } else if (species == Q2M_CHICK || species == Q2M_CHICK_HEAT ||
               species == Q2M_GUNNER || species == Q2M_MEDIC ||
               species == Q2M_MEDIC_COMMANDER || species == Q2M_GUN_COMMANDER) {
      *duck = true;
      *sidestep = species != Q2M_BRAIN;
    }
    return;
  }
  if (rogue) {
    if (species == Q2M_INFANTRY || species_is_soldier(species) ||
        species == Q2M_GUNNER || species == Q2M_CHICK ||
        species == Q2M_CHICK_HEAT || species == Q2M_MEDIC ||
        species == Q2M_MEDIC_COMMANDER) {
      *duck = true;
      *sidestep = true;
    } else if (species == Q2M_BRAIN) {
      *duck = true;
    } else if (species == Q2M_BERSERK) {
      *sidestep = true;
    }
  }
}

static bool dodge_duck(q2m_context *context, float eta_seconds, bool rogue,
                       bool *accepted, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const q2m_species species = monster->definition->species;
  const char *move = NULL;
  *accepted = false;

  if (context->body.ground.registry == 0 &&
      (species == Q2M_INFANTRY || species == Q2M_GUNNER))
    return true;
  if (species == Q2M_INFANTRY) {
    if (!rogue &&
        (move_is(monster, "infantry_move_attack2") || monster->frame == 186 ||
         monster->frame == 227 || monster->frame == 255))
      return set_duck_bounds(context, false, error);
    move = "infantry_move_duck";
  } else if (species_is_soldier(species)) {
    if (!rogue && (move_is(monster, "soldier_move_trip") ||
                   move_is(monster, "soldier_move_attack5") ||
                   move_is(monster, "soldier_move_pain4")))
      return true;
    monster->hold_frame = false;
    if (!rogue && move_is(monster, "soldier_move_attack6"))
      move = "soldier_move_trip";
    else if (!rogue && q2m_random(context->game) >= 0.5f)
      move = "soldier_move_attack3";
    else
      move = "soldier_move_duck";
  } else if (species == Q2M_BERSERK) {
    if (rogue || context->body.ground.registry == 0 ||
        move_is(monster, "berserk_move_jump") ||
        move_is(monster, "berserk_move_jump2") ||
        q2m_random(context->game) >= 0.05f)
      return true;
    move = "berserk_move_duck2";
  } else if (species == Q2M_BRAIN) {
    move = "brain_move_duck";
  } else if (species == Q2M_CHICK || species == Q2M_CHICK_HEAT) {
    if (!rogue && (move_is(monster, "chick_move_start_attack1") ||
                   move_is(monster, "chick_move_attack1")))
      return set_duck_bounds(context, false, error);
    move = "chick_move_duck";
  } else if (species == Q2M_GUNNER) {
    if (move_is(monster, "gunner_move_attack_chain") ||
        move_is(monster, "gunner_move_fire_chain") ||
        move_is(monster, "gunner_move_attack_grenade"))
      return set_duck_bounds(context, false, error);
    if (!rogue && q2m_random(context->game) > 0.5f &&
        !q2m_attack(context, Q2M_ATTACK_GRENADE,
                    monster->definition->secondary_damage, error))
      return false;
    if (!q2m_alive(context))
      return true;
    move = "gunner_move_duck";
  } else if (species == Q2M_MEDIC || species == Q2M_MEDIC_COMMANDER) {
    if (monster->medic || move_is(monster, "medic_move_attackBlaster") ||
        move_is(monster, "medic_move_attackHyperBlaster") ||
        move_is(monster, "medic_move_attackCable") ||
        move_is(monster, "medic_move_callReinforcements"))
      return set_duck_bounds(context, false, error);
    move = "medic_move_duck";
  } else if (species == Q2M_GUN_COMMANDER) {
    if (move_is(monster, "guncmdr_move_jump") ||
        move_is(monster, "guncmdr_move_jump2"))
      return true;
    if (strstr(monster->move->name, "_dodge") != NULL)
      return set_duck_bounds(context, false, error);
    move = "guncmdr_move_duck_attack";
  }
  if (move == NULL || q2m_move_named(monster, move) == NULL)
    return true;

  if (rogue) {
    double extra = context->game->options.skill == 0
                       ? 1.0
                       : 0.1 * (3 - context->game->options.skill);
    monster->duck_ns =
        q2m_after(context->game->now_ns, fmax(0.0, eta_seconds) + extra);
    monster->next_frame = q2m_move_named(monster, move)->first_frame;
  }
  if (!q2m_set_move(context, move, true, error))
    return false;
  if (!q2m_alive(context))
    return true;
  *accepted = true;
  return set_duck_bounds(context, true, error);
}

static bool dodge_sidestep(q2m_context *context, bool rogue, bool *accepted,
                           qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  const q2m_species species = monster->definition->species;
  const char *move = NULL;
  bool immediate = true;
  *accepted = false;

  if ((species == Q2M_INFANTRY || species == Q2M_GUNNER ||
       species == Q2M_BERSERK) &&
      context->body.ground.registry == 0)
    return true;
  if (species == Q2M_INFANTRY) {
    if (!rogue && !move_is(monster, "infantry_move_run") &&
        !move_is(monster, "infantry_move_attack4") && !monster->cocked &&
        (monster->frame == 186 || monster->frame == 227 ||
         monster->frame == 255)) {
      monster->fire_ns = q2m_after(monster->fire_ns > context->game->now_ns
                                       ? monster->fire_ns
                                       : context->game->now_ns,
                                   0.3 + q2m_random(context->game) * 0.3);
      move = "infantry_move_attack4";
      immediate = false;
    } else {
      move = "infantry_move_run";
    }
  } else if (species_is_soldier(species)) {
    if (!rogue && (move_is(monster, "soldier_move_trip") ||
                   move_is(monster, "soldier_move_attack5") ||
                   move_is(monster, "soldier_move_pain4")))
      return true;
    move =
        monster->skin <= 3 ? "soldier_move_attack6" : "soldier_move_start_run";
  } else if (species == Q2M_BERSERK) {
    if (move_is(monster, "berserk_move_jump") ||
        move_is(monster, "berserk_move_jump2") ||
        move_is(monster, "berserk_move_pain2"))
      return true;
    move = "berserk_move_run1";
  } else if (species == Q2M_GUNNER) {
    if (move_is(monster, "gunner_move_attack_chain") ||
        move_is(monster, "gunner_move_fire_chain") ||
        move_is(monster, "gunner_move_attack_grenade") ||
        move_is(monster, "gunner_move_pain1"))
      return true;
    move = "gunner_move_run";
  } else if (species == Q2M_CHICK || species == Q2M_CHICK_HEAT) {
    if (move_is(monster, "chick_move_start_attack1") ||
        move_is(monster, "chick_move_attack1") ||
        move_is(monster, "chick_move_pain3"))
      return true;
    move = "chick_move_run";
  } else if (species == Q2M_MEDIC || species == Q2M_MEDIC_COMMANDER) {
    if (monster->medic || move_is(monster, "medic_move_attackBlaster") ||
        move_is(monster, "medic_move_attackHyperBlaster") ||
        move_is(monster, "medic_move_attackCable") ||
        move_is(monster, "medic_move_callReinforcements"))
      return true;
    move = "medic_move_run";
  } else if (species == Q2M_GUN_COMMANDER) {
    if (move_is(monster, "guncmdr_move_fire_chain") ||
        move_is(monster, "guncmdr_move_fire_chain_run"))
      move = monster->lefty ? "guncmdr_move_fire_chain_dodge_left"
                            : "guncmdr_move_fire_chain_dodge_right";
    else if (move_is(monster, "guncmdr_move_attack_grenade_back")) {
      monster->count = monster->frame;
      move = monster->lefty ? "guncmdr_move_attack_grenade_back_dodge_left"
                            : "guncmdr_move_attack_grenade_back_dodge_right";
    } else if (move_is(monster, "guncmdr_move_attack_mortar")) {
      monster->count = monster->frame;
      move = "guncmdr_move_attack_mortar_dodge";
    } else if (move_is(monster, "guncmdr_move_run")) {
      move = "guncmdr_move_run";
    } else {
      return true;
    }
    immediate = false;
  }
  if (move == NULL || q2m_move_named(monster, move) == NULL)
    return true;
  if (!move_is(monster, move) && !q2m_set_move(context, move, immediate, error))
    return false;
  *accepted = q2m_alive(context);
  return true;
}

static bool classic_dodge(q2m_context *context, qa_actor_id attacker,
                          float eta_seconds, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  q2m_species species = monster->definition->species;
  if (q2m_random(context->game) > 0.25f)
    return true;
  if (monster->enemy.registry == 0) {
    monster->enemy = attacker;
    context->actor->physics.enemy = attacker;
  }

  if (context->game->options.product == QA_Q2_XATRIX && species == Q2M_GEKK) {
    if (context->actor->physics.water_level > 0)
      return q2m_set_move(context, "gekk_move_attack", true, error);
    if (context->game->options.skill == 0)
      return q2m_set_move(context,
                          q2m_random(context->game) > 0.5f ? "gekk_move_lduck"
                                                           : "gekk_move_rduck",
                          true, error);
    monster->pause_ns = q2m_after(context->game->now_ns, eta_seconds + 0.3f);
    float choice = q2m_random(context->game);
    if (context->game->options.skill < 3 &&
        choice > (context->game->options.skill == 1 ? 0.33f : 0.66f))
      return q2m_set_move(context,
                          q2m_random(context->game) > 0.5f ? "gekk_move_lduck"
                                                           : "gekk_move_rduck",
                          true, error);
    return q2m_set_move(context,
                        q2m_random(context->game) > 0.66f ? "gekk_move_attack1"
                                                          : "gekk_move_attack2",
                        true, error);
  }
  if (species == Q2M_INFANTRY)
    return q2m_set_move(context, "infantry_move_duck", true, error);
  if (species == Q2M_BRAIN) {
    monster->pause_ns = q2m_after(context->game->now_ns, eta_seconds + 0.5f);
    return q2m_set_move(context, "brain_move_duck", true, error);
  }
  if (species == Q2M_CHICK || species == Q2M_CHICK_HEAT)
    return q2m_set_move(context, "chick_move_duck", true, error);
  if (species == Q2M_GUNNER)
    return q2m_set_move(context, "gunner_move_duck", true, error);
  if (species == Q2M_MEDIC)
    return q2m_set_move(context, "medic_move_duck", true, error);
  if (!species_is_soldier(species) && !species_is_soldierh(species))
    return true;

  const char *duck =
      species_is_soldierh(species) ? "soldierh_move_duck" : "soldier_move_duck";
  const char *attack = species_is_soldierh(species) ? "soldierh_move_attack3"
                                                    : "soldier_move_attack3";
  if (context->game->options.skill == 0)
    return q2m_set_move(context, duck, true, error);
  monster->pause_ns = q2m_after(context->game->now_ns, eta_seconds + 0.3f);
  return q2m_set_move(
      context,
      q2m_random(context->game) >
              (context->game->options.skill == 1 ? 0.33f : 0.66f)
          ? duck
          : attack,
      true, error);
}

bool q2_monster_dodge(qa_q2_game *game, qa_actor_id target,
                      qa_actor_id attacker, float eta_seconds,
                      const qa_trace_result *trace, bool gravity,
                      qa_error *error) {
  if (game == NULL || target.slot >= game->capacity)
    return true;
  q2_actor *actor = game->actors[target.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, target) ||
      actor->monster == NULL || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      !q2_actor_live(game, target) || actor->monster->definition == NULL ||
      actor->monster->controller_kind != Q2M_CONTROLLER_NONE)
    return true;
  q2m_context context = {
      .game = game, .actor = actor, .monster = actor->monster};
  if (!q2m_refresh(&context, error))
    return false;
  struct qa_q2_monster *monster = context.monster;
  if (monster->dead || context.combat.health < 1.0f ||
      game->now_ns < monster->dodge_ns)
    return true;

  if ((game->options.edition == QA_Q2_CLASSIC &&
       game->options.product != QA_Q2_ROGUE) ||
      (game->options.product == QA_Q2_XATRIX &&
       (monster->definition->species == Q2M_GEKK ||
        species_is_soldierh(monster->definition->species))))
    return classic_dodge(&context, attacker, eta_seconds, error);

  if (game->options.product == QA_Q2_ROGUE &&
      monster->definition->species == Q2M_STALKER) {
    if (context.body.ground.registry == 0)
      return true;
    if (monster->enemy.registry == 0) {
      monster->enemy = attacker;
      return q2m_found_target(&context, attacker, error);
    }
    if (eta_seconds < 0.1f || eta_seconds > 5.0f)
      return true;
    return q2m_set_move(&context, "stalker_move_jump_straightup", true, error);
  }

  bool duck, sidestep;
  dodge_capabilities(&context, &duck, &sidestep);
  if (game->options.edition == QA_Q2_RERELEASE && gravity)
    duck = false;
  if (monster->stand_ground)
    sidestep = false;
  if (!duck && !sidestep)
    return true;

  const bool rogue = game->options.edition == QA_Q2_CLASSIC &&
                     game->options.product == QA_Q2_ROGUE;
  const float admission = q2m_random(game);
  if (monster->enemy.registry == 0) {
    monster->enemy = attacker;
    if (!q2m_found_target(&context, attacker, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    if (!q2m_refresh(&context, error))
      return false;
    monster = context.monster;
  }
  if (rogue) {
    if (eta_seconds < 0.1f || eta_seconds > 5.0f || trace == NULL ||
        admission > 0.25f * (float)(game->options.skill + 1))
      return true;
  } else if (eta_seconds < context.elapsed || eta_seconds > 2.5f ||
             admission > 0.5f) {
    return true;
  }

  float height = context.body.origin.z + context.body.bounds.maxs.z;
  if (duck && trace != NULL)
    height -= rogue ? 33.0f : 32.0f;
  if (duck && trace != NULL && !sidestep &&
      (trace->end.z <= height || monster->ducked))
    return true;

  if (sidestep && !monster->dodging &&
      (!duck || trace == NULL || trace->end.z <= height || monster->ducked)) {
    if (!rogue && game->options.skill < 2 &&
        q2m_random(game) >= (game->options.skill == 0 ? 0.25f : 0.5f)) {
      monster->dodge_ns = q2m_after(game->now_ns, 0.8 + q2m_random(game) * 0.6);
      return true;
    }
    if (trace == NULL) {
      monster->lefty = q2m_random(game) < 0.5f;
    } else {
      qa_vec3 right;
      qa_builtin_angle_vectors(context.body.angles, NULL, &right, NULL);
      monster->lefty =
          qa_vec_dot(right, qa_vec_sub(trace->end, context.body.origin)) >=
          0.0f;
    }
    bool accepted;
    if (!dodge_sidestep(&context, rogue, &accepted, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    if (accepted) {
      if (duck && monster->ducked && !set_duck_bounds(&context, false, error))
        return false;
      if (!q2m_alive(&context))
        return true;
      monster->dodging = true;
      monster->attack_state = Q2M_SLIDING;
      if (!rogue)
        monster->dodge_ns =
            q2m_after(game->now_ns, 0.4 + q2m_random(game) * 1.6);
    }
    return true;
  }

  if (duck && trace != NULL && (rogue || eta_seconds < 0.5f) &&
      monster->next_duck_ns <= game->now_ns) {
    finish_dodge(monster);
    bool accepted;
    if (!dodge_duck(&context, eta_seconds, rogue, &accepted, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    if (accepted && !rogue) {
      uint64_t arrival = q2m_after(game->now_ns, eta_seconds);
      if (monster->duck_ns < arrival)
        monster->duck_ns = arrival;
      monster->next_duck_ns = q2m_after(game->now_ns, 5.0);
      if (game->options.skill == 0)
        monster->duck_ns =
            q2m_after(monster->duck_ns, 0.5 + q2m_random(game) * 0.5);
      else if (game->options.skill == 1)
        monster->duck_ns =
            q2m_after(monster->duck_ns, 0.1 + q2m_random(game) * 0.25);
    }
    if (!rogue)
      monster->dodge_ns = q2m_after(game->now_ns, 0.2 + q2m_random(game) * 0.5);
  }
  return true;
}

bool qa_q2_monster_action(qa_q2_game *game, qa_actor_id id,
                          qa_q2_monster_action_kind action, qa_actor_id other,
                          float value, qa_error *error) {
  if (game == NULL || id.slot >= game->capacity ||
      game->actors[id.slot] == NULL ||
      !qa_actor_id_equal(game->actors[id.slot]->id, id) ||
      game->actors[id.slot]->monster == NULL ||
      game->actors[id.slot]->projectile.kind != Q2_PROJECTILE_NONE ||
      game->actors[id.slot]->monster->definition == NULL ||
      game->actors[id.slot]->monster->controller_kind != Q2M_CONTROLLER_NONE ||
      !q2_actor_live(game, id)) {
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0,
                 "Q2 actor has no native monster state");
    return false;
  }
  q2_actor *actor = game->actors[id.slot];
  q2m_context context = {
      .game = game, .actor = actor, .monster = actor->monster};
  if (!q2m_refresh(&context, error))
    return false;
  struct qa_q2_monster *monster = context.monster;
  switch (action) {
  case QA_Q2_MONSTER_USE:
    return q2m_lifecycle_use(&context, other, error);
  case QA_Q2_MONSTER_TOUCH:
    if ((monster->definition->flags & Q2M_TOUCH_ATTACK) == 0)
      return true;
    return q2m_melee(&context, 80.0f, monster->definition->secondary_damage,
                     100.0f, error);
  case QA_Q2_MONSTER_BLOCKED:
    if (monster->definition->flags & Q2M_JUMPS) {
      monster->jump_ns = q2m_after(game->now_ns, 3.0);
      const char *move =
          value > 0.0f ? "infantry_move_jump2" : "infantry_move_jump";
      if (monster->definition->species != Q2M_INFANTRY)
        move = monster->definition->run_move;
      return q2m_set_move(&context, move, true, error);
    }
    return true;
  case QA_Q2_MONSTER_DODGE:
    return q2_monster_dodge(game, id, other, value, NULL, false, error);
  case QA_Q2_MONSTER_SET_ENEMY:
    monster->enemy = other;
    return true;
  case QA_Q2_MONSTER_FOUND_TARGET:
    return q2m_found_target(&context, other, error);
  }
  qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown Q2 monster action");
  return false;
}

static bool public_monster_context(qa_q2_game *game, qa_actor_id id,
                                   q2m_context *out, qa_error *error) {
  if (game == NULL || out == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 monster action requires game and output");
    return false;
  }
  *out = (q2m_context){0};
  if (id.slot >= game->capacity)
    return true;
  q2_actor *actor = game->actors[id.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, id) ||
      actor->monster == NULL || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      !q2_actor_live(game, id) || actor->monster->definition == NULL ||
      actor->monster->controller_kind != Q2M_CONTROLLER_NONE)
    return true;
  *out = (q2m_context){.game = game, .actor = actor, .monster = actor->monster};
  return q2m_refresh(out, error);
}

bool qa_q2_monster_turret_admit(qa_q2_game *game, qa_actor_id id,
                                qa_error *error) {
  if (game == NULL || id.registry == 0 || id.slot >= game->capacity ||
      !q2_actor_live(game, id)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Q2 turret-driver admission");
    return false;
  }

  q2_actor *actor = game->actors[id.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, id)) {
    qa_error_set(error, QA_ERROR_NOT_FOUND, id.slot,
                 "Q2 turret driver actor is unavailable");
    return false;
  }
  if (actor->monster == NULL) {
    qa_q2_monster_spawn_options options = {
        .classname = "turret_driver",
        .scale = 1.0f,
        .health_multiplier = 1.0f,
    };
    if (!qa_q2_monster_spawn(game, id, &options, error))
      return false;
    if (!q2_actor_live(game, id))
      return true;
  }

  q2m_context context;
  if (!public_monster_context(game, id, &context, error))
    return false;
  if (context.actor == NULL ||
      context.monster->definition->species != Q2M_TURRET_DRIVER) {
    qa_error_set(error, QA_ERROR_ARGUMENT, id.slot,
                 "Q2 turret driver requires infantry driver state");
    return false;
  }

  struct qa_q2_monster *monster = context.monster;
  monster->gib_health = 0.0f;
  monster->view_height = 24.0f;
  monster->stand_ground = true;
  monster->ducked = true;
  monster->turret_attached = true;
  monster->lost_sight = false;
  monster->trail_ns = 0;
  monster->attack_ns = 0;
  monster->enemy = (qa_actor_id){0};
  monster->goal = (qa_actor_id){0};
  context.actor->physics.enemy = (qa_actor_id){0};
  context.actor->physics.goal = (qa_actor_id){0};
  context.actor->physics.motion = QA_PHYSICS_PUSH;
  context.actor->physics.solid = QA_PHYSICS_BOX;
  qa_combat_state traits;
  if (!qa_combat_read_traits(game->services.combat, id, &traits, error))
    return false;
  traits.no_knockback = true;
  if (!qa_combat_set_traits(game->services.combat, id, &traits, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  if (!q2m_set_move(&context, monster->definition->stand_move, true, error))
    return false;
  monster->frame = monster->move->first_frame;
  monster->next_frame = 0;
  monster->next_frame_ns = UINT64_MAX;
  return present_animation(&context, error);
}

bool qa_q2_monster_turret_aim(qa_q2_game *game, qa_actor_id id,
                              qa_actor_id *enemy, bool *fire_ready,
                              qa_error *error) {
  if (enemy == NULL || fire_ready == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q2 turret aim requires outputs");
    return false;
  }
  *enemy = (qa_actor_id){0};
  *fire_ready = false;

  q2m_context context;
  if (!public_monster_context(game, id, &context, error))
    return false;
  if (context.actor == NULL ||
      context.monster->definition->species != Q2M_TURRET_DRIVER ||
      !context.monster->turret_attached) {
    qa_error_set(error, QA_ERROR_NOT_FOUND, id.slot,
                 "Q2 actor is not an attached turret driver");
    return false;
  }

  struct qa_q2_monster *monster = context.monster;
  if (monster->enemy.registry != 0) {
    qa_combat_state combat;
    qa_error ignored = {0};
    if (qa_actors_get(qa_session_actors(game->services.session),
                      monster->enemy) == NULL ||
        !qa_combat_read(game->services.combat, monster->enemy, &combat,
                        &ignored) ||
        combat.health <= 0.0f) {
      monster->enemy = (qa_actor_id){0};
      monster->goal = (qa_actor_id){0};
      context.actor->physics.enemy = (qa_actor_id){0};
      context.actor->physics.goal = (qa_actor_id){0};
    }
  }

  if (monster->enemy.registry == 0) {
    bool found;
    if (!q2m_find_target(&context, &found, error))
      return false;
    if (!q2m_alive(&context) || context.monster->enemy.registry == 0)
      return true;
    monster = context.monster;
    monster->trail_ns = game->now_ns;
    monster->lost_sight = false;
  } else {
    bool visible;
    if (!q2m_visible(&context, monster->enemy, &visible, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    monster = context.monster;
    if (!visible) {
      monster->lost_sight = true;
      return true;
    }
    if (monster->lost_sight) {
      monster->trail_ns = game->now_ns;
      monster->lost_sight = false;
    }
  }

  *enemy = monster->enemy;
  if (game->now_ns < monster->attack_ns)
    return true;
  double reaction = fmax(0.0, 3.0 - (double)game->options.skill);
  if (game->now_ns < q2m_after(monster->trail_ns, reaction))
    return true;
  monster->attack_ns = q2m_after(game->now_ns, reaction + 1.0);
  *fire_ready = true;
  return true;
}

bool qa_q2_monster_turret_release(qa_q2_game *game, qa_actor_id id,
                                  qa_error *error) {
  q2m_context context;
  if (!public_monster_context(game, id, &context, error))
    return false;
  if (context.actor == NULL)
    return true;
  if (context.monster->definition->species != Q2M_TURRET_DRIVER) {
    qa_error_set(error, QA_ERROR_ARGUMENT, id.slot,
                 "Q2 turret release requires driver state");
    return false;
  }
  if (!context.monster->turret_attached)
    return true;

  context.monster->turret_attached = false;
  context.monster->ducked = false;
  context.monster->stand_ground = false;
  context.monster->next_frame_ns = game->now_ns;
  context.actor->physics.motion = QA_PHYSICS_STEP;
  if (context.monster->dead || context.monster->gibbed)
    return true;
  if (!q2m_set_move(&context, context.monster->definition->stand_move, true,
                    error))
    return false;
  return true;
}

bool qa_q2_monster_route_contact(const qa_q2_game *game, qa_actor_id id,
                                 qa_actor_id corner, bool combat_point,
                                 bool *eligible) {
  if (eligible == NULL)
    return false;
  *eligible = false;
  if (game == NULL || corner.registry == 0 || id.slot >= game->capacity)
    return game != NULL && corner.registry != 0;
  const q2_actor *actor = game->actors[id.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, id) ||
      actor->monster == NULL || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      !q2_actor_live((qa_q2_game *)game, id))
    return true;
  *eligible = qa_actor_id_equal(actor->monster->move_target, corner) &&
              (combat_point || actor->monster->enemy.registry == 0);
  return true;
}

bool qa_q2_monster_touch_path_corner(qa_q2_game *game, qa_actor_id id,
                                     const qa_q2_monster_path_corner *step,
                                     bool *accepted, qa_error *error) {
  if (step == NULL || accepted == NULL || step->corner.registry == 0) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Q2 monster path-corner action");
    return false;
  }
  *accepted = false;
  q2m_context context;
  if (!public_monster_context(game, id, &context, error))
    return false;
  if (context.actor == NULL ||
      !qa_actor_id_equal(context.monster->move_target, step->corner) ||
      context.monster->enemy.registry != 0)
    return true;

  context.monster->goal = step->next;
  context.monster->move_target = step->next;
  if (step->pause_until_ns != 0) {
    context.monster->pause_ns = step->pause_until_ns;
    if (context.monster->definition->stand_move != NULL &&
        !q2m_set_move(&context, context.monster->definition->stand_move, false,
                      error))
      return false;
  } else if (step->next.registry != 0) {
    qa_body_state target;
    if (!qa_world_body_read(game->services.world, step->next, &target, error))
      return false;
    qa_vec3 direction = qa_vec_sub(target.origin, context.body.origin);
    context.monster->ideal_yaw =
        atan2f(direction.y, direction.x) * 57.29577951308232f;
  }
  context.actor->physics.goal = context.monster->goal;
  *accepted = true;
  return true;
}

bool qa_q2_monster_touch_combat_point(qa_q2_game *game, qa_actor_id id,
                                      const qa_q2_monster_combat_point *step,
                                      bool *accepted, bool *finished,
                                      qa_error *error) {
  if (step == NULL || accepted == NULL || finished == NULL ||
      step->corner.registry == 0) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Q2 monster combat-point action");
    return false;
  }
  *accepted = false;
  *finished = false;
  q2m_context context;
  if (!public_monster_context(game, id, &context, error))
    return false;
  if (context.actor == NULL ||
      !qa_actor_id_equal(context.monster->move_target, step->corner))
    return true;

  if (step->has_target) {
    context.monster->goal = step->next;
    context.monster->move_target =
        step->next.registry != 0 ? step->next : step->corner;
  } else if (step->hold_if_walking && context.monster->move != NULL &&
             strstr(context.monster->move->name, "walk") != NULL) {
    context.monster->pause_ns = q2m_after(game->now_ns, 100000000.0);
    context.monster->stand_ground = true;
    if (context.monster->definition->stand_move != NULL &&
        !q2m_set_move(&context, context.monster->definition->stand_move, false,
                      error))
      return false;
  }
  if (qa_actor_id_equal(context.monster->move_target, step->corner)) {
    context.monster->move_target = (qa_actor_id){0};
    context.monster->goal = context.monster->enemy;
    context.monster->combat_point = false;
    *finished = true;
  }
  context.actor->physics.goal = context.monster->goal;
  *accepted = true;
  return true;
}

bool qa_q2_monster_path_activator(const qa_q2_game *game, qa_actor_id id,
                                  qa_actor_id *activator) {
  if (game == NULL || activator == NULL)
    return false;
  *activator = id;
  if (id.slot >= game->capacity)
    return true;
  const q2_actor *actor = game->actors[id.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, id) ||
      actor->monster == NULL || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      qa_actors_get(qa_session_actors(game->services.session), id) == NULL)
    return true;
  const qa_actor_id candidates[] = {actor->monster->enemy,
                                    actor->monster->old_enemy,
                                    actor->monster->activator};
  for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]);
       ++index) {
    qa_actor_id candidate = candidates[index];
    if (candidate.registry == 0 ||
        qa_actors_get(qa_session_actors(game->services.session), candidate) ==
            NULL)
      continue;
    qa_builtin_actor_traits traits = {0};
    if (game->services.actor_traits != NULL &&
        game->services.actor_traits(game->services.context, candidate,
                                    &traits) &&
        traits.player) {
      *activator = candidate;
      break;
    }
  }
  return true;
}

bool qa_q2_monster_target_anger(qa_q2_game *game, qa_actor_id id,
                                qa_actor_id target, qa_error *error) {
  if (target.registry == 0) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 target anger requires a target");
    return false;
  }
  q2m_context context;
  if (!public_monster_context(game, id, &context, error))
    return false;
  if (context.actor == NULL ||
      qa_actors_get(qa_session_actors(game->services.session), target) == NULL)
    return true;

  if (target.slot < game->capacity) {
    q2_actor *target_actor = game->actors[target.slot];
    if (target_actor != NULL && qa_actor_id_equal(target_actor->id, target) &&
        target_actor->monster != NULL &&
        target_actor->projectile.kind == Q2_PROJECTILE_NONE)
      target_actor->monster->good_guy = true;
  }
  context.monster->enemy = target;
  context.monster->target_anger = true;
  return q2m_found_target(&context, target, error);
}

static bool initialize_body(qa_q2_game *game, q2_actor *actor,
                            struct qa_q2_monster *monster, qa_error *error) {
  q2m_context context = {.game = game, .actor = actor, .monster = monster};
  qa_body_state body = {0};
  bool has_body = qa_world_body_storage_serial(game->services.world, actor->id) != 0;
  if (has_body && !qa_world_body_read(game->services.world, actor->id, &body, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  float scale = monster->entity_scale;
  body.bounds.mins = qa_vec_scale(monster->definition->bounds.mins, scale);
  body.bounds.maxs = qa_vec_scale(monster->definition->bounds.maxs, scale);
  if (game->options.edition == QA_Q2_RERELEASE) {
    if (monster->definition->species == Q2M_FLIPPER) {
      body.bounds.mins.z = -8.0f * scale;
      body.bounds.maxs.z = 20.0f * scale;
    } else if (monster->definition->species == Q2M_FLOATER) {
      body.bounds.maxs.z = 48.0f * scale;
    } else if (monster->definition->species == Q2M_FLYER) {
      body.bounds.maxs.z = 16.0f * scale;
    } else if (monster->definition->species == Q2M_TANK_STAND) {
      body.bounds.maxs.z = 72.0f * scale;
    }
  }
  monster->normal_height = body.bounds.maxs.z;
  monster->view_height = monster->definition->view_height;
  if (monster->view_height == 0.0f)
    monster->view_height = game->options.edition == QA_Q2_RERELEASE
                               ? truncf(monster->normal_height - 8.0f)
                           : monster->definition->locomotion == Q2M_SWIM
                               ? 10.0f
                               : 25.0f;
  if (has_body) {
    if (!qa_world_body_write(game->services.world, actor->id, &body, error))
      return false;
  } else if (!qa_world_body_create(game->services.world, actor->id, &body,
                                   error)) {
    return false;
  }
  if (!q2m_alive(&context))
    return true;
  actor->physics = qa_physics_properties_default(QA_COLLISION_Q2);
  actor->physics.q2_rerelease = game->options.edition == QA_Q2_RERELEASE;
  actor->physics.motion = monster->definition->locomotion == Q2M_STATIONARY
                              ? QA_PHYSICS_STATIONARY
                              : QA_PHYSICS_STEP;
  actor->physics.solid = QA_PHYSICS_BOX;
  actor->physics.flags = QA_PHYSICS_MONSTER;
  if ((game->options.product == QA_Q2_ROGUE || game->options.edition == QA_Q2_RERELEASE) &&
      (monster->definition->species == Q2M_WIDOW || monster->definition->species == Q2M_WIDOW2))
    actor->physics.flags |= QA_PHYSICS_KEEP_MOVE_WHILE_TURNING;
  if (monster->definition->locomotion == Q2M_FLY)
    actor->physics.flags |= QA_PHYSICS_FLYING;
  if (monster->definition->locomotion == Q2M_SWIM)
    actor->physics.flags |= QA_PHYSICS_SWIMMING;
  actor->physics.clip_mask =
      Q2M_MONSTER_MASK |
      (game->options.edition == QA_Q2_RERELEASE ? Q2_PLAYER_CONTENTS : 0);
  actor->physics.gravity_direction = qa_v3(0, 0, -1);
  actor->physics.gravity_scale = 1.0f;
  actor->physics.ideal_yaw = body.angles.y;
  actor->physics.yaw_speed = monster->yaw_speed;
  actor->physics.enemy = monster->enemy;
  actor->physics.goal = monster->goal;
  actor->physics_bound = true;
  monster->ideal_yaw = body.angles.y;
  return true;
}

static bool set_power_cells(q2m_context *context, qa_power_kind kind, float cells,
                            qa_error *error) {
  qa_q2_game *game = context->game;
  qa_actor_id id = context->actor->id;
  qa_inventory *inventory = game->services.inventory;
  qa_item_id item = 0;
  bool bound = qa_combat_power_inventory(game->services.combat, id, &inventory, &item);
  if (!bound) {
    inventory = game->services.inventory;
    if (kind == QA_POWER_NONE && !qa_inventory_has(inventory, id))
      return true;
    if (!qa_builtin_resource(&game->services, "q2:monster-power", &item, error))
      return false;
  }
  qa_inventory_entry entry = {.item = item, .count = cells, .capacity = cells};
  qa_inventory_admission *admission;
  if (!qa_inventory_prepare_entries(inventory, id, &entry, 1, &admission, error))
    return false;
  if (!q2m_alive(context)) {
    qa_inventory_admission_abort(admission);
    return true;
  }
  if (!qa_inventory_admission_validate(admission, error) || !q2m_alive(context)) {
    qa_inventory_admission_abort(admission);
    return !q2m_alive(context);
  }
  if (!qa_inventory_admission_commit(admission, error)) {
    qa_inventory_admission_abort(admission);
    return false;
  }
  if (!qa_inventory_configure(inventory, id, &entry, NULL, NULL, error))
    return false;
  if (!q2m_alive(context))
    return true;
  return bound || kind == QA_POWER_NONE ||
         qa_combat_bind_power_inventory(game->services.combat, id, inventory, item, error);
}

static bool initialize_combat(qa_q2_game *game, q2_actor *actor,
                              struct qa_q2_monster *monster, qa_error *error) {
  q2m_context context = {.game = game, .actor = actor, .monster = monster};
  qa_combat_state combat = {
      .health = monster->base_health,
      .mass = monster->definition->mass * monster->entity_scale,
      .can_take_damage = true,
  };
  if (monster->definition->flags & Q2M_POWER_SCREEN) {
    combat.armor.powered.kind = QA_POWER_SCREEN;
    combat.armor.powered.cells = 100.0f;
  } else if (monster->definition->flags & Q2M_POWER_SHIELD) {
    combat.armor.powered.kind = QA_POWER_SHIELD;
    combat.armor.powered.cells =
        monster->definition->flags & Q2M_BOSS ? 400.0f : 200.0f;
  }
  if (qa_combat_storage_serial(game->services.combat, actor->id)) {
    if (!qa_combat_set_health(game->services.combat, actor->id, combat.health, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    if (!qa_combat_set_armor(game->services.combat, actor->id, &combat.armor, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    qa_combat_state traits;
    if (!qa_combat_read_traits(game->services.combat, actor->id, &traits, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    traits.mass = combat.mass;
    traits.can_take_damage = true;
    traits.invulnerable = false;
    if (!qa_combat_set_traits(game->services.combat, actor->id, &traits, error))
      return false;
  } else if (!qa_combat_create_actor(game->services.combat, actor->id, &combat,
                                     error)) {
    return false;
  }
  return true;
}

static bool monster_admit(qa_q2_game *game, qa_actor_id id,
                          const qa_q2_monster_spawn_options *options,
                          struct qa_q2_monster *previous, qa_error *error) {
  if (game == NULL || options == NULL || options->classname == NULL ||
      options->health_multiplier < 0.0f || options->scale < 0.0f ||
      !isfinite(options->health_multiplier) || !isfinite(options->scale)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid native Q2 monster spawn");
    return false;
  }
  if (game->options.deathmatch)
    return !q2_actor_live(game, id) ||
           qa_session_release(game->services.session, id, error);
  if (game->services.physics == NULL) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Native Q2 monsters require shared physics");
    return false;
  }
  const q2m_definition *definition =
      q2m_definition_for(game, options->classname);
  if (definition == NULL) {
    qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Unknown native Q2 monster %s",
                 options->classname);
    return false;
  }
  const q2m_move_set *move_set = q2m_move_set_for(game, definition);
  if (move_set == NULL) {
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Native Q2 monster %s has no move table", options->classname);
    return false;
  }
  q2_actor *actor = q2_actor_get(game, id, true, error);
  if (actor == NULL)
    return false;
  if (actor->monster != previous) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Q2 actor already has native monster state");
    return false;
  }
  struct qa_q2_monster *monster = calloc(1, sizeof(*monster));
  if (monster == NULL) {
    qa_error_set(error, QA_ERROR_MEMORY, 0,
                 "Allocating native Q2 monster state");
    return false;
  }
  monster->definition = definition;
  monster->move_set = move_set;
  monster->move = q2m_move_named(monster, definition->initial_move);
  if (monster->move == NULL) {
    q2m_free_monster(monster);
    qa_error_set(error, QA_ERROR_FORMAT, 0,
                 "Native Q2 monster %s lacks initial move %s",
                 options->classname, definition->initial_move);
    return false;
  }
  monster->spawnflags = options->spawnflags;
  monster->old_frame = -1;
  monster->render_flags = game->options.edition == QA_Q2_CLASSIC ? 64u : 32768u;
  monster->entity_scale = options->scale > 0.0f ? options->scale : 1.0f;
  if (definition->species == Q2M_GUN_COMMANDER && options->scale == 0.0f)
    monster->entity_scale = 1.25f;
  if (definition->species == Q2M_TANK_STAND && options->scale == 0.0f)
    monster->entity_scale = 1.5f;
  monster->animation_scale = definition->scale * monster->entity_scale;
  monster->health_scaling =
      options->health_multiplier > 0.0f ? options->health_multiplier : 1.0f;
  monster->base_health = definition->health * monster->health_scaling;
  monster->max_health = monster->base_health;
  if (definition->species == Q2M_CARRIER || definition->species == Q2M_WIDOW ||
      definition->species == Q2M_WIDOW2)
    monster->base_health +=
        (float)(game->options.cooperative ? 500 * game->options.skill : 0);
  if (definition->species == Q2M_CARRIER)
    monster->base_health =
        fmaxf(2000.0f, 2000.0f + 1000.0f * (game->options.skill - 1)) +
        (game->options.cooperative ? 500.0f * game->options.skill : 0.0f);
  if (definition->species == Q2M_WIDOW)
    monster->base_health =
        2000.0f + 1000.0f * game->options.skill +
        (game->options.cooperative ? 500.0f * game->options.skill : 0.0f);
  if (definition->species == Q2M_WIDOW2)
    monster->base_health =
        2800.0f + 1000.0f * game->options.skill +
        (game->options.cooperative ? 500.0f * game->options.skill : 0.0f);
  monster->max_health = monster->base_health;
  monster->gib_health = definition->species == Q2M_INFANTRY &&
                                game->options.edition == QA_Q2_RERELEASE
                            ? -65.0f
                            : definition->gib_health;
  monster->yaw_speed = definition->yaw_speed > 0.0f ? definition->yaw_speed
                       : definition->locomotion == Q2M_WALK ? 20.0f
                                                            : 10.0f;
  monster->enemy = options->enemy;
  monster->commander = options->commander;
  monster->summoned = options->summoned;
  monster->triggered = options->triggered || (options->spawnflags & 2u) != 0;
  monster->do_not_count =
      options->summoned || (definition->flags & Q2M_DO_NOT_COUNT) != 0;
  monster->good_guy = (definition->flags & Q2M_GOOD_GUY) != 0 ||
                      (game->options.edition == QA_Q2_RERELEASE &&
                       (options->spawnflags & UINT32_C(524288)) != 0);
  monster->ignore_shots = definition->species == Q2M_CARRIER ||
                          definition->species == Q2M_MEDIC_COMMANDER ||
                          definition->species == Q2M_TANK_COMMANDER;
  if (game->options.edition == QA_Q2_RERELEASE &&
      (definition->species == Q2M_JORG || definition->species == Q2M_MAKRON))
    monster->ignore_shots = true;
  monster->can_take_damage = true;
  monster->visible = true;
  if (options->triggered)
    monster->spawnflags |= 2u;
  monster->air_ns =
      q2m_after(game->now_ns, definition->locomotion == Q2M_SWIM ? 9.0 : 12.0);
  monster->pause_ns = UINT64_MAX;
  monster->attack_state = Q2M_STRAIGHT;
  monster->spawned_by =
      options->commander.registry == 0 ? Q2M_SPAWN_NONE
      : definition->species == Q2M_FLYER || definition->species == Q2M_KAMIKAZE
          ? Q2M_SPAWN_CARRIER
          : Q2M_SPAWN_MEDIC;
  monster->monster_slots = definition->species == Q2M_CARRIER
                               ? game->options.skill == 0   ? 3
                                 : game->options.skill == 3 ? 9
                                                            : 6
                           : definition->species == Q2M_MEDIC_COMMANDER ? 3
                                                                        : 0;
  monster->fly_min_distance = 100.0f;
  monster->fly_max_distance = 500.0f;
  monster->fly_acceleration = 5.0f;
  monster->fly_speed = 150.0f;
  monster->skin = definition->species == Q2M_SOLDIER_LIGHT ? 0
                  : definition->species == Q2M_SOLDIER     ? 2
                  : definition->species == Q2M_SOLDIER_SS  ? 4
                                                           : 0;
  if (definition->species == Q2M_TANK_COMMANDER ||
      definition->species == Q2M_GUN_COMMANDER)
    monster->skin = 2;
  if (definition->species == Q2M_SOLDIER_HYPER)
    monster->skin = 2;
  else if (definition->species == Q2M_SOLDIER_LASER)
    monster->skin = 4;
  else if (definition->species == Q2M_DAEDALUS)
    monster->skin = 2;
  else if (definition->species == Q2M_BOSS5)
    monster->skin = 2;
  else if (definition->species == Q2M_CHICK_HEAT)
    monster->skin = game->options.edition == QA_Q2_RERELEASE ? 2 : 3;
  else if (definition->species == Q2M_INSANE && (options->spawnflags & 8u) == 0)
    monster->skin = (int)floorf(q2m_random(game) * 3.0f);
  if (definition->species == Q2M_TANK_COMMANDER &&
      game->options.edition == QA_Q2_RERELEASE)
    monster->count = 1;
  if (game->options.edition == QA_Q2_RERELEASE) {
    switch (definition->species) {
    case Q2M_PARASITE:
      monster->yaw_speed = 30;
      break;
    case Q2M_FLIPPER:
      monster->alternate_fly = true;
      monster->fly_acceleration = 30.0f;
      monster->fly_speed = 110.0f;
      monster->fly_min_distance = 10.0f;
      monster->fly_max_distance = 10.0f;
      break;
    case Q2M_FLOATER:
      monster->alternate_fly = true;
      monster->fly_acceleration = 10.0f;
      monster->fly_speed = 100.0f;
      monster->fly_min_distance = 20.0f;
      monster->fly_max_distance = 200.0f;
      break;
    case Q2M_FLYER:
      monster->alternate_fly = true;
      monster->fly_buzzard = true;
      monster->fly_acceleration = 15.0f;
      monster->fly_speed = 165.0f;
      monster->fly_min_distance = 45.0f;
      monster->fly_max_distance = 200.0f;
      break;
    case Q2M_HOVER:
      monster->alternate_fly = true;
      monster->fly_acceleration = 20.0f;
      monster->fly_speed = 120.0f;
      monster->fly_min_distance = 150.0f;
      monster->fly_max_distance = 350.0f;
      monster->yaw_speed = 18.0f;
      break;
    default:
      break;
    }
  }
  if (previous) {
    monster->healer = previous->healer;
    memcpy(monster->bad_medic, previous->bad_medic, sizeof(monster->bad_medic));
    monster->medic_tries = previous->medic_tries;
    monster->ignore_shots = previous->ignore_shots || monster->ignore_shots;
    monster->do_not_count = previous->do_not_count;
    monster->spawned_by = previous->spawned_by;
    monster->commander = previous->commander;
    monster->monster_slots = previous->monster_slots;
    monster->monster_used = previous->monster_used;
    q2m_retire_monster(game, previous);
  }
  actor->monster = monster;
  q2m_context context = {.game = game, .actor = actor, .monster = monster};
  if (!qa_builtin_resource(&game->services, options->classname,
                           &monster->classname, error) ||
      !qa_builtin_resource(&game->services, definition->model, &monster->model,
                           error) ||
      !initialize_body(game, actor, monster, error)) {
    if (q2m_alive(&context)) {
      actor->monster = NULL;
      actor->physics_bound = false;
      q2m_retire_monster(game, monster);
    }
    return false;
  }
  if (!q2m_alive(&context))
    return true;
  if (!initialize_combat(game, actor, monster, error)) {
    if (q2m_alive(&context)) {
      actor->monster = NULL;
      actor->physics_bound = false;
      q2m_retire_monster(game, monster);
    }
    return false;
  }
  if (!q2m_alive(&context))
    return true;
  qa_actor_collision collision = {
      .family = QA_COLLISION_Q2,
      .shape = QA_SHAPE_BOX,
      .contents = (int32_t)UINT32_C(0x02000000),
      .role = QA_COLLISION_SOLID,
      .monster = true,
  };
  if (!qa_world_set_collision(game->services.world, id, &collision,
                                     error)) {
    actor->monster = NULL;
    actor->physics_bound = false;
    q2m_retire_monster(game, monster);
    return false;
  }
  if (!q2m_refresh(&context, error)) {
    if (q2m_alive(&context)) {
      actor->monster = NULL;
      actor->physics_bound = false;
      q2m_retire_monster(game, monster);
    }
    return false;
  }
  monster->initialized = true;
  if (!q2m_summon_initialize(&context, error)) {
    if (q2m_alive(&context)) {
      actor->monster = NULL;
      actor->physics_bound = false;
      q2m_retire_monster(game, monster);
    }
    return false;
  }
  if (!q2m_alive(&context))
    return true;
  monster->initial_power_armor = context.combat.armor.powered.kind;
  monster->max_power_armor = context.combat.armor.powered.cells;
  if (monster->initial_power_armor != QA_POWER_NONE) {
    if (!set_power_cells(&context, monster->initial_power_armor, monster->max_power_armor, error))
      return false;
    if (!q2m_alive(&context))
      return true;
  } else if (qa_inventory_has(game->services.inventory, id)) {
    qa_inventory *inventory = game->services.inventory;
    qa_item_id item;
    if (!qa_combat_power_inventory(game->services.combat, id, &inventory, &item) &&
        !qa_builtin_resource(&game->services, "q2:monster-power", &item, error))
      return false;
    qa_inventory_entry fuel;
    qa_error local = {0};
    bool found = qa_inventory_entry_read(inventory, id, item, &fuel, &local);
    if (!q2m_alive(&context))
      return true;
    if (!found && local.code != QA_OK && local.code != QA_ERROR_NOT_FOUND) {
      if (error)
        *error = local;
      return false;
    }
    if (found) {
      if (!isfinite(fuel.count) || fuel.count < 0 || fuel.count > FLT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, id.slot,
                     "Q2 monster power reserve is outside float range");
        return false;
      }
      monster->max_power_armor = (float)fuel.count;
    }
  }
  if (previous && game->options.edition == QA_Q2_RERELEASE) {
    monster->max_health = previous->max_health;
    monster->base_health = previous->base_health;
    monster->health_scaling = previous->health_scaling;
    monster->gib_health = truncf(previous->gib_health * .5f);
    monster->monster_slots = previous->monster_slots;
    monster->monster_used = previous->monster_used;
    monster->spawned_by = previous->spawned_by;
    monster->commander = previous->commander;
    monster->initial_power_armor = previous->initial_power_armor;
    monster->max_power_armor = previous->max_power_armor;
    qa_armor armor = {.powered = {.kind = monster->initial_power_armor,
                                  .cells = monster->max_power_armor}};
    if (!qa_combat_set_health(game->services.combat, id, monster->max_health, error) ||
        !q2m_alive(&context))
      return !q2m_alive(&context);
    if (!qa_combat_set_armor(game->services.combat, id, &armor, error) ||
        !q2m_alive(&context))
      return !q2m_alive(&context);
    if (!set_power_cells(&context, monster->initial_power_armor, monster->max_power_armor, error))
      return false;
    if (!q2m_alive(&context))
      return true;
    if (!q2m_refresh(&context, error))
      return !q2m_alive(&context);
  }
  bool automatic = !(definition->species == Q2M_TURRET && (monster->spawnflags & 128u));
  if (!q2m_lifecycle_admitted(&context, automatic, previous != NULL, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  size_t frame_count =
      (size_t)(monster->move->last_frame - monster->move->first_frame + 1);
  monster->frame = monster->move->first_frame;
  if (automatic)
    monster->frame += (int)fminf((float)(frame_count - 1),
                                floorf(q2m_random(game) * (float)frame_count));
  monster->next_frame_ns = q2m_after(game->now_ns, 0.1);
  if (!q2m_link(&context, error)) {
    if (q2m_alive(&context)) {
      actor->monster = NULL;
      actor->physics_bound = false;
      q2m_retire_monster(game, monster);
    }
    return false;
  }
  return q2m_alive(&context) ? q2m_show(&context, error) : true;
}

bool qa_q2_monster_spawn(qa_q2_game *game, qa_actor_id id,
                         const qa_q2_monster_spawn_options *options, qa_error *error) {
  return monster_admit(game, id, options, NULL, error);
}

bool q2m_revive(q2m_context *context, qa_error *error) {
  if (!q2m_alive(context))
    return true;
  struct qa_q2_monster *previous = context->monster;
  qa_actor_id id = context->actor->id;
  qa_q2_monster_spawn_options options = {
      .classname = previous->definition->classname,
      .scale = previous->entity_scale,
      .health_multiplier = previous->health_scaling,
      .enemy = previous->enemy,
      .commander = previous->commander,
  };
  bool ok = monster_admit(context->game, id, &options, previous, error);
  if (!q2_actor_live(context->game, id) || context->game->actors[id.slot] != context->actor)
    return ok;
  context->monster = context->actor->monster;
  return ok && (!q2m_alive(context) || q2m_refresh(context, error));
}

void q2_monster_release_state(q2_actor *actor) {
  if (actor == NULL)
    return;
  q2m_free_monster(actor->monster);
  actor->monster = NULL;
}

void q2m_free_monster(struct qa_q2_monster *monster) {
  if (monster) {
    q2m_summon_clear(monster->summons);
    free(monster->summons);
    free(monster);
  }
}

bool q2_monster_traits(qa_q2_game *game, qa_actor_id id,
                       qa_builtin_actor_traits *out) {
  if (out == NULL)
    return false;
  *out = (qa_builtin_actor_traits){0};
  if (game == NULL || id.slot >= game->capacity ||
      game->actors[id.slot] == NULL ||
      !qa_actor_id_equal(game->actors[id.slot]->id, id) ||
      game->actors[id.slot]->monster == NULL ||
      game->actors[id.slot]->projectile.kind != Q2_PROJECTILE_NONE ||
      !q2_actor_live(game, id))
    return false;
  const struct qa_q2_monster *monster = game->actors[id.slot]->monster;
  if (monster->definition == NULL ||
      monster->controller_kind != Q2M_CONTROLLER_NONE)
    return false;
  out->classname = monster->classname;
  out->monster = true;
  out->invisible = !monster->visible;
  out->aimed_damage = true;
  out->laser_immune = monster->definition->species == Q2M_CARRIER ||
                      monster->definition->species == Q2M_WIDOW ||
                      monster->definition->species == Q2M_WIDOW2;
  out->damageable_target = monster->can_take_damage;
  out->no_source_friendly_fire = monster->good_guy;
  out->grounded = (game->actors[id.slot]->physics.flags & QA_PHYSICS_ONGROUND) != 0;
  out->view_height = monster->view_height;
  out->gib_health = monster->gib_health;
  out->max_health = monster->max_health;
  out->hostile_until_ns = monster->hostile_ns;
  return true;
}

static bool process_pending(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  if (!monster->pending_pain && !monster->pending_death)
    return true;
  bool death = monster->pending_death || context->combat.health <= 0.0f;
  monster->pending_pain = false;
  monster->pending_death = false;
  bool result = death ? q2m_die(context, error) : q2m_pain(context, error);
  monster->pending_damage = 0.0f;
  monster->pending_kick = 0.0f;
  if (!result || !q2m_alive(context))
    return result;
  if (context->game->options.edition == QA_Q2_RERELEASE) {
    if (!q2m_refresh(context, error))
      return false;
    if (death && context->combat.health > monster->gib_health &&
        monster->frame == monster->move->last_frame) {
      monster->frame -= 1 + (int)floorf(q2m_random(context->game) * 2.0f);
      if ((context->actor->physics.flags & QA_PHYSICS_ONGROUND) &&
          context->actor->physics.motion == QA_PHYSICS_TOSS &&
          monster->definition->locomotion != Q2M_STATIONARY) {
        context->body.angles.y += q2m_random(context->game) < 0.5f ? 4.5f : -4.5f;
        if (!q2m_write_body(context, true, error))
          return false;
        if (!q2m_alive(context))
          return true;
      }
    }
    if (!q2m_health_target(context, error))
      return false;
  }
  if (q2m_alive(context))
    return q2m_show(context, error);
  return result;
}

static bool finish_actor_damage(void *context, qa_actor_id id, qa_error *error) {
  qa_q2_game *game = context;
  q2_actor *actor = q2_actor_get(game, id, false, NULL);
  if (!actor || !actor->monster || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      actor->monster->controller_kind != Q2M_CONTROLLER_NONE ||
      !actor->monster->definition ||
      (!actor->monster->pending_pain && !actor->monster->pending_death) ||
      actor->monster->pending_damage == 0)
    return true;
  q2m_context call = {.game = game, .actor = actor, .monster = actor->monster};
  return q2m_refresh(&call, error) && process_pending(&call, error);
}

bool qa_q2_monsters_end_frame(qa_q2_game *game, qa_error *error) {
  if (!game) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q2 monster frame owner");
    return false;
  }
  if (game->options.edition != QA_Q2_RERELEASE)
    return true;
  q2_trace_frame *snapshot = q2_scratch_acquire(game, error);
  if (!snapshot)
    return false;
  bool ok = qa_builtin_observations(&game->services, &snapshot->snapshot, error);
  if (ok) {
    size_t count = 0;
    for (size_t i = 0; i < snapshot->snapshot.count; i++) {
      qa_actor_id id = snapshot->snapshot.ids[i];
      q2_actor *actor = q2_actor_get(game, id, false, NULL);
      if (actor && actor->monster && actor->projectile.kind == Q2_PROJECTILE_NONE &&
          actor->monster->controller_kind == Q2M_CONTROLLER_NONE && actor->monster->definition)
        snapshot->snapshot.ids[count++] = id;
    }
    snapshot->snapshot.count = count;
    qa_builtin_sort_observations(&game->services, &snapshot->snapshot);
    for (size_t i = 0; i < snapshot->snapshot.count; i++) {
      qa_actor_id id = snapshot->snapshot.ids[i];
      if (q2_actor_live(game, id) &&
          !qa_q2_run_actor(game, id, finish_actor_damage, game, error)) {
        ok = false;
        break;
      }
    }
  }
  snapshot->active = false;
  return ok;
}

static bool check_dodge_projectiles(q2m_context *context, qa_error *error) {
  struct qa_q2_monster *monster = context->monster;
  bool can_duck, can_sidestep;
  dodge_capabilities(context, &can_duck, &can_sidestep);
  if (monster->dead || context->combat.health < 1.0f ||
      context->game->now_ns < monster->dodge_ns || (!can_duck && !can_sidestep))
    return true;

  qa_vec3 forward;
  qa_builtin_angle_vectors(context->body.angles, &forward, NULL, NULL);
  qa_vec3 minimum =
      qa_vec_add(qa_vec_add(context->body.origin, context->body.bounds.mins),
                 qa_v3(-512.0f, -512.0f, -512.0f));
  qa_vec3 maximum =
      qa_vec_add(qa_vec_add(context->body.origin, context->body.bounds.maxs),
                 qa_v3(512.0f, 512.0f, 512.0f));

  for (q2_actor *shot_actor = context->game->first_actor; shot_actor != NULL;
       shot_actor = shot_actor->live_next) {
    q2_projectile *projectile = &shot_actor->projectile;
    if (projectile->kind == Q2_PROJECTILE_NONE || !projectile->dodgeable ||
        !shot_actor->physics_bound ||
        shot_actor->physics.solid == QA_PHYSICS_NOT_SOLID ||
        projectile->owner.registry == 0 ||
        !q2_actor_live(context->game, projectile->owner))
      continue;

    qa_body_state shot;
    if (!qa_world_body_read(context->game->services.world, shot_actor->id,
                            &shot, error))
      return false;
    float speed = qa_vec_length(shot.velocity);
    qa_vec3 shot_min = qa_vec_add(shot.origin, shot.bounds.mins);
    qa_vec3 shot_max = qa_vec_add(shot.origin, shot.bounds.maxs);
    if (speed < 4.0f || shot_min.x > maximum.x || shot_min.y > maximum.y ||
        shot_min.z > maximum.z || shot_max.x < minimum.x ||
        shot_max.y < minimum.y || shot_max.z < minimum.z ||
        qa_vec_dot(
            qa_vec_normalize(qa_vec_sub(shot.origin, context->body.origin)),
            forward) <= 0.35f)
      continue;

    qa_trace_query query = {
        .start = shot.origin,
        .end = qa_vec_add(shot.origin, shot.velocity),
        .shape = {.kind = QA_SHAPE_BOX, .bounds = shot.bounds},
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
        .pass_actor = shot_actor->id,
    };
    query.policy.contents_mask = shot_actor->physics.clip_mask;
    qa_trace_result trace;
    if (!qa_world_trace(context->game->services.world, &query, &trace, error))
      return false;
    if (!q2m_alive(context))
      return true;
    if (trace.hit != QA_TRACE_HIT_ACTOR ||
        !qa_actor_id_equal(trace.actor, context->actor->id))
      continue;

    bool gravity = shot_actor->physics.motion == QA_PHYSICS_BOUNCE ||
                   shot_actor->physics.motion == QA_PHYSICS_TOSS;
    float eta = qa_vec_length(qa_vec_sub(trace.end, shot.origin)) / speed;
    return q2_monster_dodge(context->game, context->actor->id,
                            projectile->owner, eta, &trace, gravity, error);
  }
  return true;
}

bool q2_monster_tick(qa_q2_game *game, q2_actor *actor, qa_error *error) {
  if (game == NULL || actor == NULL || actor->monster == NULL ||
      actor->projectile.kind != Q2_PROJECTILE_NONE)
    return true;
  if (!q2m_perception_begin(game, error))
    return false;
  if (!q2_actor_live(game, actor->id) || actor->monster == NULL)
    return true;
  if (actor->monster->controller_kind != Q2M_CONTROLLER_NONE)
    return q2m_controller_tick(game, actor, error);
  q2m_context context = {
      .game = game, .actor = actor, .monster = actor->monster};
  if (!q2m_refresh(&context, error))
    return false;
  bool handled;
  if (!q2m_lifecycle_tick(&context, &handled, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  if (handled)
    return true;
  if (game->options.edition == QA_Q2_RERELEASE) {
    context.monster->render_flags &= ~((UINT32_C(1) << 22) | (UINT32_C(1) << 26));
    context.monster->old_frame = -1;
  }
  if (game->options.edition == QA_Q2_RERELEASE &&
      !process_pending(&context, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  if (!q2m_boss_explosion_tick(&context, error))
    return false;
  if (!q2m_alive(&context) || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      context.monster->gibbed)
    return true;
  if (context.monster->turret_attached)
    return true;
  if (game->options.edition == QA_Q2_RERELEASE &&
      !check_dodge_projectiles(&context, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  if (!q2m_refresh(&context, error))
    return false;
  if (context.monster->enemy.registry) {
    bool routed;
    if (!q2m_lifecycle_route(&context, false, &routed, error))
      return false;
    if (!q2m_alive(&context))
      return true;
  }
  if (!q2m_animation(&context, error))
    return false;
  if (!q2m_alive(&context) || context.monster->gibbed)
    return true;
  qa_linked_body linked;
  if (qa_world_linked(game->services.world, actor->id, &linked) &&
      linked.link_count != context.monster->last_link_count) {
    context.monster->last_link_count = linked.link_count;
    if (!qa_physics_check_ground(game->services.physics, actor->id, error))
      return false;
  }
  if (!qa_physics_categorize_water(game->services.physics, actor->id, error) ||
      !q2m_world_effects(&context, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  context.monster->water_level = actor->physics.water_level;
  context.monster->water_type = actor->physics.water_type;
  actor->extra_effects &= ~UINT64_C(256);
  context.monster->render_flags &= ~(1024u | 2048u | 4096u);
  if (context.monster->resurrecting) {
    actor->extra_effects |= 256u;
    context.monster->render_flags |= 1024u;
  }
  return q2m_show(&context, error);
}

bool q2_monster_reaction(qa_q2_game *game, const qa_damage_outcome *outcome,
                         qa_error *error) {
  if (game == NULL || outcome == NULL || outcome->stale)
    return true;
  qa_actor_id id = outcome->request.target;
  if (id.slot >= game->capacity || game->actors[id.slot] == NULL ||
      !qa_actor_id_equal(game->actors[id.slot]->id, id) ||
      game->actors[id.slot]->monster == NULL)
    return true;
  q2_actor *actor = game->actors[id.slot];
  if (actor->projectile.kind != Q2_PROJECTILE_NONE)
    return true;
  if (actor->monster->controller_kind != Q2M_CONTROLLER_NONE ||
      actor->monster->definition == NULL)
    return true;
  q2m_context context = {
      .game = game, .actor = actor, .monster = actor->monster};
  if (!q2m_refresh(&context, error))
    return false;
  struct qa_q2_monster *monster = context.monster;
  monster->last_attack = outcome->request.attack;
  monster->last_damage_point = outcome->request.point;
  monster->pending_damage += outcome->result.applied_damage;
  monster->pending_kick += outcome->request.knockback;
  qa_actor_id attacker = outcome->request.attack.attacker;
  bool dying = outcome->result.reaction == QA_REACTION_DEATH ||
               context.combat.health <= 0.0f;
  if (!dying && outcome->result.applied_damage > 0.0f &&
      !q2m_react_to_damage(&context, attacker, error))
    return false;
  if (!q2m_alive(&context))
    return true;
  monster = context.monster;
  if (dying) {
    if (game->options.edition == QA_Q2_RERELEASE) {
      if (context.combat.health < -999.0f &&
          !qa_combat_set_health(game->services.combat, id, -999.0f, error))
        return false;
      monster->enemy = attacker;
      monster->goal = attacker;
      actor->physics.enemy = attacker;
      actor->physics.goal = attacker;
    }
    monster->pending_death = true;
  } else if (outcome->result.reaction == QA_REACTION_PAIN &&
             outcome->result.applied_damage > 0.0f) {
    monster->pending_pain = true;
  }
  if (game->options.edition == QA_Q2_RERELEASE)
    return true;
  return process_pending(&context, error);
}

bool q2_monster_touch(qa_q2_game *game, const qa_touch_contact *contact,
                      qa_error *error) {
  if (game == NULL || contact == NULL || contact->self.slot >= game->capacity ||
      game->actors[contact->self.slot] == NULL ||
      !qa_actor_id_equal(game->actors[contact->self.slot]->id, contact->self) ||
      game->actors[contact->self.slot]->monster == NULL)
    return true;
  q2_actor *actor = game->actors[contact->self.slot];
  if (actor->projectile.kind != Q2_PROJECTILE_NONE)
    return true;
  if (actor->monster->controller_kind != Q2M_CONTROLLER_NONE ||
      actor->monster->definition == NULL)
    return true;
  q2m_context context = {
      .game = game, .actor = actor, .monster = actor->monster};
  return q2m_refresh(&context, error) && q2m_touch(&context, contact, error);
}

bool q2_monster_pain_advance(qa_q2_game *game, qa_actor_id id,
                             uint64_t amount_ns) {
  if (game == NULL || amount_ns == 0 || id.slot >= game->capacity)
    return false;
  q2_actor *actor = game->actors[id.slot];
  if (actor == NULL || !qa_actor_id_equal(actor->id, id) ||
      actor->monster == NULL || actor->projectile.kind != Q2_PROJECTILE_NONE ||
      !q2_actor_live(game, id) || actor->monster->definition == NULL ||
      actor->monster->controller_kind != Q2M_CONTROLLER_NONE ||
      actor->monster->pain_ns == 0)
    return false;
  actor->monster->pain_ns = actor->monster->pain_ns > amount_ns
                                ? actor->monster->pain_ns - amount_ns
                                : 0;
  return true;
}
