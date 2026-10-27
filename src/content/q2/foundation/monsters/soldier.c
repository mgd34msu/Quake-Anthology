/* Port of src/content/q2/foundation/monsters/soldier.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/soldier.h"

#include <math.h>

#include "monsters_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/frames.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

static void set_move(qa_q2m_context *c, const char *name) { c->set_move(c->impl_ctx, c, name, false, false); }
static bool alive_enemy(qa_q2m_context *c) {
  qa_body_state body;
  return qa_q2m_enemy_body(c, &body)
      && qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) > 0;
}
static bool needs_cock(qa_q2m_context *c) { return c->state->weapon == QA_Q2M_WEAPON_SHOTGUN && !c->state->cocked; }
static bool melee_range(qa_q2m_context *c) {
  return qa_q2m_target_distance(c) <= (!q2m_rr(c->game) ? 80 : 20);
}
static bool refire(qa_q2m_context *c, bool force) {
  if (!q2m_rr(c->game))
    return (c->game->options.skill == 3 && q2m_rnd(c->game) < 0.5) || melee_range(c);
  return ((force || q2m_rnd(c->game) < 0.5) && qa_q2m_visible_enemy(c)) || melee_range(c);
}
typedef struct frame_tables {
  const qa_q2m_classic_soldier_frames *classic;
  const qa_q2m_rerelease_soldier_frames *rerelease;
} frame_tables;
/* Field accessors dispatch on edition (same field names in both
 * generated structs; offsets may differ, so branch per use). */

void qa_q2m_soldier_stand_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  if (q2m_rr(game)) {
    if (((uint32_t)entity->spawnflags & 8u) != 0) { set_move(c, "soldier_move_blind"); return; }
    double r = q2m_rnd(game);
    set_move(c, strcmp(state->move->name, "soldier_move_stand1") != 0 || r < 0.6 ? "soldier_move_stand1"
               : r < 0.8 ? "soldier_move_stand2" : "soldier_move_stand3");
    return;
  }
  set_move(c, strcmp(state->move->name, "soldier_move_stand3") == 0 || q2m_rnd(game) < 0.8
             ? "soldier_move_stand1" : "soldier_move_stand3");
}
void qa_q2m_soldier_walk_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  set_move(c, q2m_rnd(c->game) < 0.5 ? "soldier_move_walk1" : "soldier_move_walk2");
}
void qa_q2m_soldier_run_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rr(c->game)) qa_q2m_finish_dodge(c);
  if (c->state->stand_ground) { set_move(c, "soldier_move_stand1"); return; }
  const char *move = c->state->move->name;
  set_move(c, strcmp(move, "soldier_move_walk1") == 0 || strcmp(move, "soldier_move_walk2") == 0
           || strcmp(move, "soldier_move_start_run") == 0
           || (q2m_rr(c->game) && strcmp(move, "soldier_move_run") == 0)
           ? "soldier_move_run" : "soldier_move_start_run");
}
void qa_q2m_soldier_attack_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  if (!q2m_rr(game)) {
    set_move(c, state->weapon == QA_Q2M_WEAPON_MACHINEGUN ? "soldier_move_attack4"
               : q2m_rnd(game) < 0.5 ? "soldier_move_attack1" : "soldier_move_attack2");
    return;
  }
  qa_q2m_finish_dodge(c);
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
    double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
    double r = q2m_rnd(game);
    state->blind_fire_delay += 4.1 + q2m_rnd(game) * 3;
    if (qa_hypot3(state->blind_fire_target) == 0 || r > chance) return;
    state->manual_steering = true;
    set_move(c, "soldier_move_attack1");
    state->attack_finished = q2m_now(game) + 1.5 + q2m_rnd(game);
    return;
  }
  double r = q2m_rnd(game);
  if (!state->stand_ground && r < 0.25 && state->weapon != QA_Q2M_WEAPON_MACHINEGUN
      && qa_q2m_target_distance(c) >= 220) { set_move(c, "soldier_move_attack6"); return; }
  const qa_q2m_muzzle_ids *ids = &qa_q2m_muzzle_ids_value;
  if (state->weapon != QA_Q2M_WEAPON_MACHINEGUN) {
    bool first = !(state->weapon == QA_Q2M_WEAPON_SHOTGUN && qa_q2m_target_distance(c) <= 286)
              && qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, ids->SOLDIER_BLASTER_1));
    bool second = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, ids->SOLDIER_BLASTER_2));
    if (first && (!second || q2m_rnd(game) < 0.5)) { set_move(c, "soldier_move_attack1"); return; }
    if (second) { set_move(c, "soldier_move_attack2"); return; }
  } else if (qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, ids->SOLDIER_MACHINEGUN_4))) {
    set_move(c, "soldier_move_attack4");
    return;
  }
}
void qa_q2m_soldier_sight_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  q2m_sound(game, c->entity, q2m_rnd(game) < 0.5 ? "soldier/solsght1.wav" : "soldier/solsrch1.wav",
            true, 2, false, 0, false, 0);
  if (!q2m_rr(game)) {
    if (game->options.skill > 0 && qa_q2m_target_distance(c) >= 500 && q2m_rnd(game) > 0.5)
      set_move(c, "soldier_move_attack6");
  } else if (qa_q2m_visible_enemy(c) && qa_q2m_target_distance(c) >= 440 && q2m_rnd(game) > 0.75) {
    if (c->state->weapon != QA_Q2M_WEAPON_MACHINEGUN) set_move(c, "soldier_move_attack6");
    else if (qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, qa_q2m_muzzle_ids_value.SOLDIER_MACHINEGUN_4)))
      set_move(c, "soldier_move_attack4");
  }
}
void qa_q2m_soldier_pain_handler(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  bool rr = q2m_rr(game);
  if (qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2)
    entity->skin = (double)((uint32_t)entity->skin | 1u);
  else if (rr) entity->skin = (double)((uint32_t)entity->skin & ~1u);
  if (rr) { qa_q2m_finish_dodge(c); state->charging = false; state->manual_steering = false; }
  qa_vec3 velocity = q2m_body(game, entity).velocity;
  const char *move = state->move->name;
  bool pain_move = strcmp(move, "soldier_move_pain1") == 0 || strcmp(move, "soldier_move_pain2") == 0
                || strcmp(move, "soldier_move_pain3") == 0;
  if (q2m_now(game) < state->pain_time) {
    if (velocity.z > 100 && pain_move) {
      if (rr) qa_q2m_set_duck(c, false);
      set_move(c, "soldier_move_pain4");
    }
    return;
  }
  state->pain_time = q2m_now(game) + 3;
  q2m_sound(game, entity, state->weapon == QA_Q2M_WEAPON_BLASTER ? "soldier/solpain2.wav"
            : state->weapon == QA_Q2M_WEAPON_SHOTGUN ? "soldier/solpain1.wav" : "soldier/solpain3.wav",
            true, 2, false, 0, false, 0);
  if (velocity.z > 100) { if (rr) qa_q2m_set_duck(c, false); set_move(c, "soldier_move_pain4"); return; }
  const qa_attack_cause *cause = entity->last_attack ? &entity->last_attack->cause : NULL;
  if ((rr && (state->ducked || state->combat_point))
      || (game->options.skill == 3 && !(rr && cause && cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 41)))
    return;
  double r = q2m_rnd(game);
  set_move(c, r < 0.33 ? "soldier_move_pain1" : r < 0.66 ? "soldier_move_pain2" : "soldier_move_pain3");
  if (rr) qa_q2m_set_duck(c, false);
}

static void fire_soldier(qa_q2m_context *c, double flash_number, bool angle_limited) {
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  bool rr = q2m_rr(game);
  const qa_q2m_muzzle_ids *ids = &qa_q2m_muzzle_ids_value;
  const double blaster_flash[9] = {39, 40, 83, 86, 89, 92, 95, 98, ids->SOLDIER_BLASTER_9};
  const double shotgun_flash[9] = {41, 42, 84, 87, 90, 93, 96, 99, ids->SOLDIER_SHOTGUN_9};
  const double machinegun_flash[9] = {43, 44, 85, 88, 91, 94, 97, 100, ids->SOLDIER_MACHINEGUN_9};
  const double *table = state->weapon == QA_Q2M_WEAPON_BLASTER ? blaster_flash
                      : state->weapon == QA_Q2M_WEAPON_SHOTGUN ? shotgun_flash : machinegun_flash;
  double flash = table[qa_q2m_record_at(9, flash_number)];
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 aim = qa_math_angle_vectors_rule(q2m_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  if (flash_number == 5 || flash_number == 6) {
    if (rr && ((uint32_t)entity->spawnflags & 65536u) != 0) return;
  } else {
    qa_vec3 enemy;
    if (!qa_q2m_enemy_eye(c, &enemy)) { state->hold_frame = false; return; }
    qa_vec3 end = enemy;
    if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
      qa_q2f_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
      end = state->blind_fire_target;
      end.z = state->blind_fire_target.z + (enemy_entity ? enemy_entity->view_height : 22);
    }
    qa_vec3 direction = qa_sub3_double(end, start);
    if (rr && angle_limited && qa_dot3_double(qa_normalize3_double_hypot(direction), aim) < 0.5) {
      state->hold_frame = q2m_now(game) < state->fire_wait;
      return;
    }
    qa_angle_vectors basis = qa_math_angle_vectors_rule(
        qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI), QA_ANGLE_RULE_NATIVE);
    double r = (q2m_rnd(game) * 2 - 1) * 1000, u = (q2m_rnd(game) * 2 - 1) * 500;
    aim = qa_normalize3_double_hypot(qa_add3_double(qa_scale3_double(basis.forward, 8192),
        qa_add3_double(qa_scale3_double(basis.right, r), qa_scale3_double(basis.up, u))));
  }
  if (state->weapon == QA_Q2M_WEAPON_BLASTER) {
    (void)c->weapons.fire_blaster(c->weapons.ctx, entity, game, start, aim, 5, 600, 8, false, false, 0);
  } else if (state->weapon == QA_Q2M_WEAPON_SHOTGUN) {
    c->weapons.fire_shotgun(c->weapons.ctx, entity, game, start, aim, 2, 1, rr ? 1500 : 1000, rr ? 750 : 500, rr ? 9 : 12, 0);
    if (rr) state->cocked = false;
  } else {
    if (!state->hold_frame) {
      if (rr) state->fire_wait = q2m_now(game) + 0.3 + q2m_rnd(game) * 0.8;
      else state->pause_time = q2m_now(game) + (3 + floor(q2m_rnd(game) * 8)) * 0.1;
    }
    c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, aim, 2, 4, 300, 500, 0);
    state->hold_frame = q2m_now(game) < (rr ? state->fire_wait : state->pause_time);
  }
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_MONSTER_MUZZLEFLASH; e.actor = &entity->actor.id; e.flash = flash;
  e.origin = start; e.direction = aim;
  game->host->emit(game->host->ctx, e);
}

static void death_shrink(void *ctx, qa_q2m_context *c);
void qa_q2m_soldier_die_handler(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  bool rr = q2m_rr(game);
  const qa_attack_cause *cause = entity->last_attack ? &entity->last_attack->cause : NULL;
  double damage = reaction.pain.damage;
  if (qa_q2m_health(game, &entity->actor.id) <= state->gib_health
      || (rr && state->dead && cause && cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 20)) {
    q2m_sound(game, entity, "misc/udeath.wav", true, 2, false, 0, false, 0);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options skinned = {false, false, true, false};
    if (rr) {
      entity->skin = trunc(entity->skin / 2);
      for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone2/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/soldier/gibs/arm.md2", damage, skinned);
      qa_q2m_gib_options gun = {false, false, true, true};
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/soldier/gibs/gun.md2", damage, gun);
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/soldier/gibs/chest.md2", damage, skinned);
      qa_q2m_gib_options head = {true, false, true, false};
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/soldier/gibs/head.md2", damage, head);
    } else {
      for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
      (void)qa_q2m_throw_head(entity, game, "models/objects/gibs/head2/tris.md2", damage);
    }
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  state->dead = true;
  qa_q2f_combat_traits traits;
  memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = true;
  game->host->combat.set_traits(game->host->combat.ctx, entity->actor, traits);
  q2m_sound(game, entity, state->weapon == QA_Q2M_WEAPON_BLASTER ? "soldier/soldeth2.wav"
            : state->weapon == QA_Q2M_WEAPON_SHOTGUN ? "soldier/soldeth1.wav" : "soldier/soldeth3.wav",
            true, 2, false, 0, false, 0);
  qa_body_state body = q2m_body(game, entity);
  if (fabs(body.origin.z + entity->view_height - reaction.point.z) <= 4 && (!rr || body.velocity.z < 65)) {
    set_move(c, "soldier_move_death3");
    return;
  }
  if (rr && (strcmp(state->move->name, "soldier_move_trip") == 0 || strcmp(state->move->name, "soldier_move_attack5") == 0)) {
    set_move(c, "soldier_move_death4");
    state->next_frame = qa_q2m_rerelease_soldier_frames_value.death413;
    death_shrink(NULL, c);
    return;
  }
  double n = floor(q2m_rnd(game) * (rr && body.velocity.z <= 65 && qa_hypot3(body.velocity) <= 150 ? 4 : 5));
  static const char *const deaths[5] = {"soldier_move_death1", "soldier_move_death2", "soldier_move_death4", "soldier_move_death5", "soldier_move_death6"};
  set_move(c, deaths[qa_q2m_record_at(5, n)]);
}
bool qa_q2m_soldier_sidestep(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const char *name = c->state->move->name;
  if (strcmp(name, "soldier_move_trip") == 0 || strcmp(name, "soldier_move_attack5") == 0
      || strcmp(name, "soldier_move_pain4") == 0) return false;
  if (c->state->weapon != QA_Q2M_WEAPON_MACHINEGUN) {
    if (strcmp(name, "soldier_move_attack6") != 0) set_move(c, "soldier_move_attack6");
  } else if (strcmp(name, "soldier_move_start_run") != 0 && strcmp(name, "soldier_move_run") != 0)
    set_move(c, "soldier_move_start_run");
  return true;
}
bool qa_q2m_soldier_duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  c->state->hold_frame = false;
  set_move(c, strcmp(c->state->move->name, "soldier_move_attack6") == 0 ? "soldier_move_trip"
             : needs_cock(c) || q2m_rnd(c->game) < 0.5 ? "soldier_move_duck" : "soldier_move_attack3");
  return true;
}

static void hyper_ripper(qa_q2m_context *c, double flash, bool limited) {
  if (c->entity->skin >= 6 && c->state->weapon != QA_Q2M_WEAPON_MACHINEGUN) fire_soldier(c, flash, limited);
}
static void laser_sound(qa_q2m_context *c, bool start) {
  if (c->entity->skin >= 6 && c->state->weapon == QA_Q2M_WEAPON_MACHINEGUN) {
    qa_q2f_presentation_event e;
    memset(&e, 0, sizeof e);
    e.kind = QA_Q2F_EVENT_SOUND; e.actor = &c->entity->actor.id;
    e.origin = q2m_body(c->game, c->entity).origin; e.path = "weapons/laser2.wav";
    e.channel = 1; e.volume = 1; e.attenuation = 1; e.loop = start ? "start" : "stop";
    c->game->host->emit(c->game->host->ctx, e);
  }
}

/* Frame-table field by edition (identical member names). */
#define SFIELD(c, field) (q2m_rr((c)->game) ? qa_q2m_rerelease_soldier_frames_value.field \
                                            : qa_q2m_classic_soldier_frames_value.field)
static void cb_idle(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rnd(c->game) > 0.8) q2m_sound(c->game, c->entity, "soldier/solidle1.wav", true, 2, true, 1, true, 2);
}
static void cb_cock(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2m_sound(c->game, c->entity, "infantry/infatck3.wav", true, 1, true, 1, true,
            c->entity->frame == SFIELD(c, stand322) ? 2 : 1);
  c->state->cocked = true;
}
static void cb_walk1_random(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rnd(c->game) > 0.1) c->state->next_frame = SFIELD(c, walk101);
}
static void cb_fire1(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 0, false); }
static void cb_fire2(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 1, false); }
static void duck_down(void *ctx, qa_q2m_context *c);
static void cb_fire3(void *ctx, qa_q2m_context *c) {
  if (!q2m_rr(c->game)) duck_down(ctx, c);
  fire_soldier(c, 2, false);
}
static void cb_fire4(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 3, false); }
static void cb_fire5(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 8, true); }
static void cb_fire6(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  fire_soldier(c, 5, false);
  if (q2m_rr(c->game) && needs_cock(c)) c->state->next_frame = SFIELD(c, death126);
}
static void cb_fire7(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 6, false); }
static void cb_fire8(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 7, true); }
static void cb_a1_refire1(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rr(c->game) && c->state->weapon == QA_Q2M_WEAPON_BLASTER) c->state->next_frame = SFIELD(c, attak110);
  if (c->state->manual_steering) { c->state->manual_steering = false; return; }
  if (c->state->weapon != QA_Q2M_WEAPON_BLASTER || !alive_enemy(c)) return;
  c->state->next_frame = refire(c, false) ? SFIELD(c, attak102) : SFIELD(c, attak110);
}
static void cb_a1_refire2(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->weapon != QA_Q2M_WEAPON_BLASTER && alive_enemy(c) && refire(c, c->state->force_refire)) {
    c->state->next_frame = SFIELD(c, attak102);
    c->state->force_refire = false;
  }
}
static void cb_a1_shotgun(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (needs_cock(c)) { c->state->next_frame = SFIELD(c, attak106); c->state->force_refire = true; }
}
static void cb_a2_refire1(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rr(c->game) && c->state->weapon == QA_Q2M_WEAPON_BLASTER) c->state->next_frame = SFIELD(c, attak216);
  if (c->state->weapon != QA_Q2M_WEAPON_BLASTER || !alive_enemy(c)) return;
  if (refire(c, false)) c->state->next_frame = SFIELD(c, attak204);
  else if (!q2m_rr(c->game)) c->state->next_frame = SFIELD(c, attak216);
}
static void cb_a2_refire2(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->weapon != QA_Q2M_WEAPON_BLASTER && alive_enemy(c) && refire(c, c->state->force_refire)) {
    c->state->next_frame = SFIELD(c, attak204);
    c->state->force_refire = false;
  }
}
static void cb_a2_shotgun(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (needs_cock(c)) { c->state->next_frame = SFIELD(c, attak210); c->state->force_refire = true; }
}
static void cb_a3_refire(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rr(c->game) && needs_cock(c)) c->state->hold_frame = q2m_now(c->game) < c->state->duck_wait;
  else if (q2m_now(c->game) + 0.4 < (!q2m_rr(c->game) ? c->state->pause_time : c->state->duck_wait))
    c->state->next_frame = SFIELD(c, attak303);
}
static void cb_a6_refire(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (alive_enemy(c) && qa_q2m_target_distance(c) >= 500 && c->game->options.skill == 3)
    c->state->next_frame = SFIELD(c, runs03);
}
static void cb_a6_refire1(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  c->state->charging = false;
  if (!c->entity->has_enemy || c->state->weapon != QA_Q2M_WEAPON_BLASTER) return;
  if (!alive_enemy(c) || qa_q2m_target_distance(c) < 440 || !qa_q2m_visible_enemy(c)) {
    qa_q2m_soldier_run_handler(NULL, c);
    return;
  }
  if (q2m_rnd(c->game) < 0.25) c->state->next_frame = SFIELD(c, runs03);
  else qa_q2m_soldier_run_handler(NULL, c);
}
static void cb_a6_refire2(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  c->state->charging = false;
  if (!c->entity->has_enemy || c->state->weapon == QA_Q2M_WEAPON_BLASTER || !alive_enemy(c)
      || (!c->state->force_refire && qa_q2m_target_distance(c) < 440) || !qa_q2m_visible_enemy(c)) return;
  if (c->state->force_refire || q2m_rnd(c->game) < 0.25) {
    c->state->next_frame = SFIELD(c, runs03);
    c->state->force_refire = false;
  }
}
static void cb_a6_shotgun(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (needs_cock(c)) { c->state->next_frame = SFIELD(c, runs09); c->state->force_refire = true; }
}
static void duck_down(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->ducked) return;
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2m_now(c->game) + 1;
}
static void duck_hold(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2m_now(c->game) < c->state->pause_time;
}
static void duck_up(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void start_charge(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->charging = true; }
static void blind_move(void *ctx, qa_q2m_context *c) { (void)ctx; set_move(c, "soldier_move_blind"); }
static void blind_check(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering)
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(c->state->blind_fire_target, q2m_body(c->game, c->entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static void stand_up(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->set_move(c->impl_ctx, c, "soldier_move_trip", true, false);
  c->state->next_frame = qa_q2m_rerelease_soldier_frames_value.runt08;
}
static void check_prone(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!needs_cock(c) && qa_q2m_prone_shot(c)) c->set_move(c->impl_ctx, c, "soldier_move_attack5", true, false);
}
static void dead_cb(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_corpse(c); }
static void death_shrink(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state body = q2m_body(c->game, c->entity);
  c->entity->server_flags = (double)((uint32_t)c->entity->server_flags | 2u);
  qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_bounds = true; changes.bounds = body.bounds; changes.bounds.max.z = 0;
  q2m_move(c->game, c->entity, changes, false, false);
}
static void laser_start(void *ctx, qa_q2m_context *c) { (void)ctx; laser_sound(c, true); }
static void laser_end(void *ctx, qa_q2m_context *c) { (void)ctx; laser_sound(c, false); }
static void ripper1(void *ctx, qa_q2m_context *c) { (void)ctx; fire_soldier(c, 0, false); }
static void ripper2(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->weapon != QA_Q2M_WEAPON_MACHINEGUN) fire_soldier(c, 1, false);
}
static void ripper3(void *ctx, qa_q2m_context *c) { (void)ctx; hyper_ripper(c, 2, false); }
static void ripper5(void *ctx, qa_q2m_context *c) { (void)ctx; hyper_ripper(c, 8, true); }
static void ripper8(void *ctx, qa_q2m_context *c) { (void)ctx; hyper_ripper(c, 7, true); }
static void hyper_refire1(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->weapon == QA_Q2M_WEAPON_SHOTGUN && q2m_rnd(c->game) < 0.7 && qa_q2m_visible_enemy(c))
    c->entity->frame = SFIELD(c, attak103);
}
static void hyper_refire2(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->weapon == QA_Q2M_WEAPON_SHOTGUN && q2m_rnd(c->game) < 0.7 && qa_q2m_visible_enemy(c))
    c->entity->frame = SFIELD(c, attak205);
}

#define CB(name, fn) {name, {NULL, fn}}
const qa_q2m_named_handler qa_q2m_soldier_callbacks[] = {
  CB("soldier_stand", qa_q2m_soldier_stand_handler),
  CB("soldier_run", qa_q2m_soldier_run_handler),
  CB("soldier_idle", cb_idle),
  CB("soldier_cock", cb_cock),
  CB("soldier_walk1_random", cb_walk1_random),
  CB("soldier_fire1", cb_fire1), CB("soldier_fire2", cb_fire2), CB("soldier_fire3", cb_fire3),
  CB("soldier_fire4", cb_fire4), CB("soldier_fire5", cb_fire5), CB("soldier_fire6", cb_fire6),
  CB("soldier_fire7", cb_fire7), CB("soldier_fire8", cb_fire8),
  CB("soldier_attack1_refire1", cb_a1_refire1), CB("soldier_attack1_refire2", cb_a1_refire2),
  CB("soldier_attack1_shotgun_check", cb_a1_shotgun),
  CB("soldier_attack2_refire1", cb_a2_refire1), CB("soldier_attack2_refire2", cb_a2_refire2),
  CB("soldier_attack2_shotgun_check", cb_a2_shotgun),
  CB("soldier_attack3_refire", cb_a3_refire),
  CB("soldier_attack6_refire", cb_a6_refire), CB("soldier_attack6_refire1", cb_a6_refire1),
  CB("soldier_attack6_refire2", cb_a6_refire2), CB("soldier_attack6_shotgun_check", cb_a6_shotgun),
  CB("soldier_duck_down", duck_down), CB("soldier_duck_hold", duck_hold), CB("soldier_duck_up", duck_up),
  CB("soldier_start_charge", start_charge),
  CB("soldier_blind", blind_move), CB("soldier_blind_check", blind_check),
  CB("soldier_stand_up", stand_up),
  CB("monster_check_prone", check_prone),
  CB("soldier_dead", dead_cb),
  CB("soldier_death_shrink", death_shrink),
  CB("soldierh_hyper_laser_sound_start", laser_start),
  CB("soldierh_hyper_laser_sound_end", laser_end),
  CB("soldierh_hyperripper1", ripper1), CB("soldierh_hyperripper2", ripper2),
  CB("soldierh_hyperripper3", ripper3), CB("soldierh_hyperripper5", ripper5),
  CB("soldierh_hyperripper8", ripper8),
  CB("soldierh_hyper_refire1", hyper_refire1), CB("soldierh_hyper_refire2", hyper_refire2),
};
const size_t qa_q2m_soldier_callback_count = sizeof qa_q2m_soldier_callbacks / sizeof qa_q2m_soldier_callbacks[0];
const qa_q2m_handler *qa_q2m_soldier_callback(const char *name) {
  for (size_t i = 0; i < qa_q2m_soldier_callback_count; i++)
    if (strcmp(name, qa_q2m_soldier_callbacks[i].name) == 0) return &qa_q2m_soldier_callbacks[i].handler;
  return NULL;
}
