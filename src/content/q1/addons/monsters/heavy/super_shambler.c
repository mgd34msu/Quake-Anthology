/* Port of src/content/q1/addons/monsters/heavy/super-shambler.ts.
 * quakec_mg3/monsters/mg3_super_shambler.qc. GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/heavy/super_shambler.h"

#include <math.h>
#include <string.h>

#include "qa/content/q1/addons/campaign.h"
#include "qa/content/q1/addons/monsters/heavy/projectiles.h"
#include "qa/content/q1/addons/monsters/heavy/tables/mg3_super_shambler.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/missionpacks/types.h"
#include "../support.h"

#define FLAGS QA_Q1AM_FLAGS

static qa_q1_base_monster *base_of(qa_q1am_heavy_monster *monster) { return &monster->base.base; }
static qa_vec3 target_or_zero(qa_q1_base_monster *monster) {
  qa_vec3 target = QA_Q1_ZERO;
  (void)qa_q1_base_monster_target(monster, &target);
  return target;
}
static double spread(qa_q1_services *game) { return qa_q1am_random(game) * 2 - 1; } /* TS random() */
/* TS game.entity(entity.references.get("child") ?? null) when it is this monster's lightning_child. */
static qa_q1_actor *own_child(qa_q1_services *game, qa_q1_actor *entity) {
  qa_actor_id id;
  qa_q1_actor *child = qa_q1_actor_reference_get(entity, "child", &id) ? qa_q1_get_entity(game, &id) : NULL;
  return child != NULL && child->has_owner && qa_same_actor(child->owner, entity->actor.id) &&
                 strcmp(child->classname, "lightning_child") == 0
             ? child : NULL;
}
static void remove_child(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_actor *child = own_child(monster->game, monster->entity);
  if (child != NULL) qa_q1_remove(monster->game, child);
  qa_q1am_reference(monster->entity, "child", NULL);
}
static void child_frame(qa_q1am_heavy_monster *heavy, double frame) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_actor *entity = monster->entity;
  entity->effects = FLAGS(entity->effects) | 2;
  qa_q1_actor *child = own_child(monster->game, entity);
  if (child != NULL) child->frame = frame;
}
static void lightning_child(qa_q1am_heavy_monster *heavy, bool fast) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_face(monster);
  qa_q1_base_monster_delay(monster, entity->next_think - game->time + 0.2);
  entity->effects = FLAGS(entity->effects) | 2;
  qa_q1_base_monster_face(monster);
  qa_q1_actor *child = qa_q1_create(game, fast ? "" : "lightning_child", NULL, NULL);
  qa_q1am_reference(entity, "child", &child->actor.id);
  if (!fast) { child->has_owner = true; child->owner = entity->actor.id; }
  qa_q1ad_set_text(&child->model, "progs/s_light.mdl");
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_origin = true;
  patch.origin = qa_q1_base_monster_origin(monster);
  patch.has_angles = true;
  patch.angles = qa_q1_get_body(game, entity).angles;
  qa_q1_set_body(game, child, &patch);
  qa_q1_link(game, child);
  qa_q1_schedule(game, child, fast ? 0.7 : 1.4, qa_q1_callback_registry_action(game->named, child, "SUB_Remove"));
}
static void cast_lightning(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  entity->effects = FLAGS(entity->effects) | 2;
  qa_q1_base_monster_face(monster);
  const qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster), (qa_vec3){0, 0, 40});
  const qa_vec3 delta = qa_sub3(qa_add3(target_or_zero(monster), (qa_vec3){0, 0, 16}), origin);
  const double range = qa_js_max(600, qa_js_min(1000, qa_length3(delta)));
  const qa_vec3 direction = qa_q1_normalize(delta);
  const qa_q1_trace trace = qa_q1am_trace(game, origin, qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(direction, range)),
                                          QA_Q1_POINT, &entity->actor.id, false);
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "beam";
  event.beam_style = "lightning1";
  event.has_actor = true;
  event.actor = entity->actor.id;
  event.start = origin;
  event.end = trace.end;
  qa_q1_emit(game, event);
  qa_q1am_heavy_monster_number(heavy, "frags", qa_q1_actor_number(entity, "frags") + 1);
  qa_q1am_heavy_lightning_damage(game, entity->actor.id, origin, trace.end, 10);
}
static bool attack_check(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  double cap = 3 + floor(qa_q1am_random(monster->game) * 2 + 0.5);
  const qa_q1_campaign_binding campaign = qa_q1_base_campaign(heavy->runtime->context->base);
  if ((qa_js_to_int32(campaign.read_flags(campaign.ctx)) & QA_Q1AD_BLOODY_NIGHTMARE_ACTIVE) != 0) cap--;
  monster->entity->count++;
  return monster->entity->count > cap;
}
static double z_offset(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_vec3 a = target_or_zero(monster);
  qa_vec3 b = qa_q1_base_monster_origin(monster);
  const qa_vec3 delta = qa_sub3(a, b);
  if (delta.z > 30) return 100 + spread(monster->game) * 50;
  if (delta.z < -30) return -100 + spread(monster->game) * 50;
  const double range = qa_length3(delta);
  return range > 300 ? 50 + spread(monster->game) * 25 : range < 150 ? -50 + spread(monster->game) * 25 : 0;
}
static void shot(qa_q1am_heavy_monster *heavy, double offset_y, double offset_z, qa_vec3 offset, bool hands) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const qa_q1_body body = qa_q1_get_body(game, entity);
  qa_vec3 a = target_or_zero(monster);
  qa_vec3 b = qa_q1_base_monster_origin(monster);
  qa_vec3 angles = qa_q1mp_velocity_angles(qa_sub3(a, b));
  if (hands) {
    angles.y = angles.y + offset_y * (12 + spread(game) * 4);
    angles.x = angles.x + offset_z * (12 + spread(game) * 4);
  } else {
    angles.y = angles.y + offset_y * 5;
  }
  const qa_vec3 forward = qa_q1_make_vectors(game, angles).forward;
  const qa_vec3 origin = qa_add3(qa_add3(qa_add3(body.origin, qa_scale3(qa_add3(body.bounds.min, body.bounds.max), 0.5)),
                                         qa_scale3(forward, 20)), offset);
  qa_vec3 direction = qa_q1_normalize(forward);
  if (!hands) direction.z = -direction.z + (qa_q1am_random(game) - 0.5) * 0.1;
  qa_q1_actor *missile = qa_q1am_heavy_spike(game, entity->actor.id, origin, qa_scale3(direction, 1000));
  qa_q1ad_set_text(&missile->model, "progs/rogue/plasma.mdl");
  qa_vec3 angular;
  angular.x = 300 * spread(game);
  angular.y = 300 * spread(game);
  angular.z = 300 * spread(game);
  missile->angular_velocity = angular;
  missile->movement = "toss";
  qa_q1_set_bounds(game, missile, QA_Q1_POINT);
  qa_vec3 velocity = qa_scale3(direction, spread(game) * 100 + (hands ? 500 : 400));
  velocity.z = (hands ? 100 : 125) + spread(game) * 50;
  velocity.z = velocity.z + z_offset(heavy);
  qa_q1am_set_velocity_body(game, missile, velocity);
  missile->effects = FLAGS(missile->effects) | 64;
  qa_q1_link(game, missile);
}
static void blast(qa_q1am_heavy_monster *heavy, qa_vec3 offset, bool hands) {
  if (hands) {
    for (int x = -1; x < 2; x++)
      for (int y = -1; y < 2; y++) shot(heavy, x, y, offset, true);
  } else {
    for (int i = -5; i <= 5; i++) shot(heavy, i, 0, offset, false);
    qa_q1_sound(base_of(heavy)->game, base_of(heavy)->entity->actor.id, "zombie/z_shot1.wav", "weapon", 1, 1);
  }
}
static void claw(qa_q1am_heavy_monster *heavy, double side) {
  qa_q1_base_monster *monster = base_of(heavy);
  if (!monster->state->has_enemy) return;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  if (qa_q1_base_monster_distance(monster) > 100) return;
  (void)qa_q1_base_monster_melee(monster, 100, 20, 3, false);
  qa_q1_sound_defaults(game, entity->actor.id, "shambler/smack.wav");
  if (side != 0) {
    const qa_q1_basis basis = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles);
    (void)qa_q1_spawn_meat_spray(game, entity, qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, 16)),
                                 qa_scale3(basis.right, side));
  }
}
static void smash(qa_q1am_heavy_monster *heavy) {
  blast(heavy, QA_Q1_ZERO, false);
  qa_q1_base_monster *monster = base_of(heavy);
  if (!monster->state->has_enemy) return;
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 0);
  if (qa_q1_base_monster_distance(monster) > 100 || !monster->state->has_enemy ||
      !qa_q1_can_damage(game, monster->state->enemy, entity->actor.id))
    return;
  (void)qa_q1_base_monster_melee(monster, 100, 40, 3, false);
  qa_q1_sound_defaults(game, entity->actor.id, "shambler/smack.wav");
  for (int i = 0; i < 2; i++) {
    const qa_q1_basis basis = game->basis;
    const qa_vec3 origin = qa_add3(qa_q1_base_monster_origin(monster), qa_scale3(basis.forward, 16));
    (void)qa_q1_spawn_meat_spray(game, entity, origin, qa_scale3(basis.right, spread(game) * 100));
  }
}
static void melee(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  qa_vec3 a = qa_q1_base_monster_origin(monster);
  qa_vec3 b = target_or_zero(monster);
  const qa_vec3 delta = qa_sub3(a, b);
  const double range = qa_length3(delta);
  if (range < 110) {
    const char *frame;
    if (qa_q1am_random(game) < 0.8) frame = "supsham_smash1";
    else frame = qa_q1am_random(game) < 0.5 ? "supsham_swingl1" : "supsham_swingr1";
    qa_q1_base_monster_play(monster, frame);
    return;
  }
  if (range < 200 || entity->wait > game->time) {
    const double chance = qa_dot3(qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).right, qa_q1_normalize(delta));
    qa_q1_base_monster_play(monster, chance < 0.45 ? "supsham_swingr1" : chance < 0.9 ? "supsham_swingl1" : "supsham_smash1");
    return;
  }
  entity->count = 0;
  if (qa_q1am_random(game) > 0.4) {
    qa_q1_base_monster_play(monster, "supsham_magic1");
    entity->wait = game->time + 5;
  } else {
    qa_q1_base_monster_play(monster, "supsham_magic_b1");
    entity->wait = game->time + 3;
  }
}
static void missile(qa_q1am_heavy_monster *heavy) {
  const double r = qa_q1am_random(base_of(heavy)->game);
  qa_q1_base_monster_play(base_of(heavy), r < 0.36 ? "supsham_magic1" : r < 0.66 ? "supsham_swingr1" : "supsham_swingl1");
}
static void cleanup_orbs(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  for (size_t i = 0; i < game->entity_count; i++) {
    qa_q1_actor *entity = game->entities[i];
    if (strcmp(entity->classname, "monster_super_shambler") == 0 && entity->has_owner &&
        qa_same_actor(entity->owner, monster->entity->actor.id))
      qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, QA_Q1AM_HEAVY_PREFIX ":source_die"));
  }
}
static void set_next(qa_q1_base_monster *monster, const char *frame) {
  qa_q1am_copy_name(monster->next_frame, sizeof monster->next_frame, frame);
}
static void smash12(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster_ai(base_of(heavy), QA_Q1_AI_CHARGE, 4);
  if (attack_check(heavy)) set_next(base_of(heavy), "supsham_magic1");
}
static void swingl7(qa_q1am_heavy_monster *heavy) {
  qa_q1_services *game = base_of(heavy)->game;
  blast(heavy, qa_add3(qa_scale3(game->basis.right, 16), (qa_vec3){0, 0, 32}), true);
  qa_q1_base_monster_ai(base_of(heavy), QA_Q1_AI_CHARGE, 5);
  claw(heavy, 250);
}
static void swingr7(qa_q1am_heavy_monster *heavy) {
  qa_q1_services *game = base_of(heavy)->game;
  blast(heavy, qa_add3(qa_scale3(game->basis.right, -16), (qa_vec3){0, 0, 32}), true);
  qa_q1_base_monster_ai(base_of(heavy), QA_Q1_AI_CHARGE, 6);
  claw(heavy, -250);
}
static void swingl9(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 8);
  (void)qa_q1_make_vectors(monster->game, qa_q1_get_body(monster->game, monster->entity).angles);
  if (attack_check(heavy)) set_next(monster, "supsham_magic1");
  else if (qa_q1am_random(monster->game) < 0.5) set_next(monster, "supsham_swingr1");
}
static void swingr9(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 1);
  qa_q1_base_monster_ai(monster, QA_Q1_AI_CHARGE, 10);
  (void)qa_q1_make_vectors(monster->game, qa_q1_get_body(monster->game, monster->entity).angles);
  if (attack_check(heavy)) set_next(monster, "supsham_magic1");
  else if (qa_q1am_random(monster->game) < 0.5) set_next(monster, "supsham_swingl1");
}
static void magic1(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_base_monster_face(monster);
  qa_q1_sound(monster->game, monster->entity->actor.id, "shambler/sattck1.wav", "weapon", 1, 1);
  monster->entity->count = 0;
}
static void magic3(qa_q1am_heavy_monster *heavy) { lightning_child(heavy, false); }
static void magic_b3(qa_q1am_heavy_monster *heavy) { lightning_child(heavy, true); }
static void magic4(qa_q1am_heavy_monster *heavy) { child_frame(heavy, 1); }
static void magic5(qa_q1am_heavy_monster *heavy) { child_frame(heavy, 2); }
static void magic4b(qa_q1am_heavy_monster *heavy) { child_frame(heavy, 3); }
static const qa_q1am_heavy_action_entry actions[] = {
  {"supsham_removechild", remove_child}, {"SupCastLightning", cast_lightning}, {"supsham_melee", melee},
  {"supsham_missile", missile}, {"cleanup_orbs", cleanup_orbs},
  {"mg3_super_shambler:supsham_smash10", smash}, {"mg3_super_shambler:supsham_smash12", smash12},
  {"mg3_super_shambler:supsham_swingl7", swingl7}, {"mg3_super_shambler:supsham_swingr7", swingr7},
  {"mg3_super_shambler:supsham_swingl9", swingl9}, {"mg3_super_shambler:supsham_swingr9", swingr9},
  {"mg3_super_shambler:supsham_magic1", magic1}, {"mg3_super_shambler:supsham_magic3", magic3},
  {"mg3_super_shambler:supsham_magic_b3", magic_b3}, {"mg3_super_shambler:supsham_magic4", magic4},
  {"mg3_super_shambler:supsham_magic5", magic5}, {"mg3_super_shambler:supsham_magic4b", magic4b},
};
static void spawn(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_gameplay_authority_set_health(monster->game->host.combat, monster->entity->actor, 2000);
  qa_q1am_heavy_monster_number(heavy, "allowPathFind", 1);
  qa_q1am_heavy_monster_number(heavy, "combat_style", 3);
  qa_q1am_heavy_monster_number(heavy, "frags", 0);
  qa_q1am_heavy_monster_initialize(heavy, 2);
}
static void achievement(qa_q1_services *game, qa_actor_id player, const char *id) {
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "achievement";
  event.has_player = true;
  event.player = player;
  event.id = id;
  qa_q1_emit(game, event);
}
static void pain(qa_q1am_heavy_monster *heavy, const qa_actor_id *attacker, double damage) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  const double health = qa_q1_health(game, entity->actor.id);
  qa_q1_sound_defaults(game, entity->actor.id, "shambler/shurt2.wav");
  if (damage >= health && attacker != NULL && qa_q1_is_player(game, attacker)) {
    const qa_q1_player_state *player = qa_q1_player(game, *attacker);
    if (player != NULL && player->weapon != NULL && strcmp(player->weapon, "axe") == 0) achievement(game, *attacker, "ACH_CLOSE_SHAVE");
    if (qa_q1_actor_number(entity, "frags") == 0) achievement(game, *attacker, "ACH_SHAMBLER_DANCE");
  }
  if (health <= 0 || 25 + qa_q1am_random(game) * 400 > damage || monster->state->pain_finished > game->time) return;
  monster->state->pain_finished = game->time + 5;
  remove_child(heavy);
  qa_q1_base_monster_play(monster, "supsham_pain1");
}
static void die(qa_q1am_heavy_monster *heavy) {
  qa_q1_base_monster *monster = base_of(heavy);
  qa_q1_services *game = monster->game;
  qa_q1_actor *entity = monster->entity;
  remove_child(heavy);
  const double health = qa_q1_health(game, entity->actor.id);
  if (health < -60) {
    qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
    qa_q1_throw_head_damage(game, entity, "h_shams", health);
    static const char *const models[] = {"gib1", "gib2", "gib3"};
    for (size_t i = 0; i < 3; i++) (void)qa_q1_throw_gib(game, qa_q1_base_monster_origin(monster), models[i], health);
    return;
  }
  qa_q1_sound_defaults(game, entity->actor.id, "shambler/sdeath.wav");
  qa_q1_base_monster_play(monster, "supsham_death1");
}
static const char *const classnames[] = {"monster_super_shambler"};
static const char *const gibs[] = {"gib1", "gib2", "gib3"};
const qa_q1am_heavy_definition qa_q1am_super_shambler_definition = {
  .spec = {.species = "shambler", .classnames = classnames, .classname_count = 1, .model = "shambler_blood",
           .head = "h_shams", .health = 2000, .gib_health = -60, .gibs = gibs, .gib_count = 3,
           .bounds = {{-32, -32, -24}, {32, 32, 64}}, .stand = "supsham_stand1", .walk = "supsham_walk1",
           .run = "supsham_run1", .sight = "", .melee = true, .missile = "supsham_melee", .movement = "walk"},
  .frames = &qa_q1am_mg3_super_shambler_frames,
  .actions = actions, .action_count = sizeof actions / sizeof actions[0],
  .spawn = spawn, .melee = melee, .pain = pain, .die = die,
};
