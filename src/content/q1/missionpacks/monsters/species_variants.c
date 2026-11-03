/* Port of src/content/q1/missionpacks/monsters/species-variants.ts: Rogue and
 * Hipnotic changes to id1 species (progs.dat knight, hknight, zombie, ogre,
 * tarbaby, fish). GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/species_variants.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "monsters_internal.h"
#include "qa/content/q1/base/frames.h"
#include "qa/content/q1/base/monster_actions.h"
#include "qa/content/q1/missionpacks/monsters/helpers.h"

enum { SPAWN_LYING = 2, SPAWN_AMBUSH = 4, SPAWN_SNEAKY = 8 };
static const qa_bounds LYING_BOUNDS = {{-16, -16, -24}, {16, 16, -16}};
static const char *const STATUE_GIBS[3] = {"statgib1", "statgib2", "statgib3"};
static const char *const STATUE_SOUNDS[6][2] = {
    {"knight/idle.wav", "statue/idle.wav"},   {"knight/khurt.wav", "statue/pain.wav"},
    {"knight/kdeath.wav", "statue/death.wav"}, {"hknight/idle.wav", "statue/idle.wav"},
    {"hknight/pain1.wav", "statue/pain.wav"}, {"hknight/death1.wav", "statue/death.wav"},
};

/* TS speciesDefinition defaults: pain/die run the shared bodies. */
static void default_pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; /* TS (monster, attacker, damage) => monster.painDefault(attacker, damage) */
  qa_q1mpm_monster_pain_default(monster, attacker, damage);
}
static void default_die(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker) {
  (void)ctx; /* TS (monster, attacker) => monster.dieDefault(attacker) */
  qa_q1mpm_monster_die_default(monster, attacker);
}
void qa_q1mpm_species_definition(const qa_q1_base_monster_species *spec, qa_q1mpm_pack_monster_definition *out) {
  memset(out, 0, sizeof *out);
  out->spec = *spec;
  out->has_base_behavior = true;
  out->base_behavior = true;
  out->pain = default_pain;
  out->die = default_die;
}
/* TS species(classname). */
static const qa_q1_base_monster_species *species(const char *classname) {
  for (size_t i = 0; i < QA_Q1_BASE_SPECIES_COUNT; i++)
    for (size_t j = 0; j < QA_Q1_BASE_SPECIES[i].classname_count; j++)
      if (strcmp(QA_Q1_BASE_SPECIES[i].classnames[j], classname) == 0) return &QA_Q1_BASE_SPECIES[i];
  qa_range_error("Missing base species for %s", classname);
}
/* TS withAction(frame, from, to): a shared frame whose action operation is
 * retargeted at a pack action of another name. Operations live in runtime storage. */
static qa_q1_monster_frame_entry with_action(qa_q1mpm_runtime *runtime, const char *frame, const char *from, const char *to) {
  const qa_q1_monster_frame *source = qa_q1_monster_frames_get(frame);
  if (source == NULL) qa_range_error("Missing Q1 source animation %s", frame);
  qa_q1_monster_operation *operations =
      qa_q1mpm_runtime_calloc(runtime, source->operation_count != 0 ? source->operation_count : 1, sizeof *operations);
  for (size_t i = 0; i < source->operation_count; i++) {
    operations[i] = source->operations[i];
    if (operations[i].kind == QA_Q1_OP_ACTION && q1mpm_str_eq(operations[i].name, from)) {
      memset(&operations[i], 0, sizeof operations[i]);
      operations[i].kind = QA_Q1_OP_ACTION;
      operations[i].name = to;
    }
  }
  qa_q1_monster_frame_entry entry = {frame, *source};
  entry.frame.operations = operations;
  return entry;
}
/* TS frameCopy(name, frame, next, operations). */
static qa_q1_monster_frame_entry frame_copy(const char *name, double frame, const char *next,
                                            const qa_q1_monster_operation *operations, size_t count) {
  qa_q1_monster_frame_entry entry = {name, {frame, next, operations, count}};
  return entry;
}
static double state(const qa_q1mpm_mission_monster *monster, const char *key) {
  return qa_q1_actor_number(monster->base.entity, key);
}
/* TS setState: entity.fields.set(key, String(value)). */
static void set_state(qa_q1mpm_mission_monster *monster, const char *key, double value) {
  q1mpm_set_number_text(monster->base.entity, key, value);
}
static bool has_flag(const qa_q1_actor *entity, int32_t flag) { return (qa_js_to_int32(entity->spawnflags) & flag) != 0; }
/* TS `entity.number("yaw_speed") || 20`. */
static double yaw_speed_or_default(const qa_q1_actor *entity) {
  const double value = qa_q1_actor_number(entity, "yaw_speed");
  return value != 0 && !isnan(value) ? value : 20;
}
static void set_health(qa_q1_services *game, qa_q1_actor *entity, double health) {
  qa_gameplay_authority_set_health(game->host.combat, entity->actor, health);
}
static void named_action(const char *name, qa_q1mpm_action_fn fn, void *ctx, qa_q1mpm_named_action *out) {
  out->name = name;
  out->action.fn = fn;
  out->action.ctx = ctx;
}

/* Closure state shared by one variant's hooks (TS captured spec and runtime). */
typedef struct variant_ctx {
  qa_q1mpm_runtime *runtime;
  const qa_q1_base_monster_species *spec;
  double frozen; /* statues */
} variant_ctx;
static variant_ctx *variant(qa_q1mpm_runtime *runtime, const qa_q1_base_monster_species *spec) {
  variant_ctx *ctx = qa_q1mpm_runtime_calloc(runtime, 1, sizeof *ctx);
  ctx->runtime = runtime;
  ctx->spec = spec;
  return ctx;
}

/* ---- knight.qc and hknight.qc: statues (spawnflag 2) hold a frozen frame,
 * take no damage and wake when triggered. ---- */
static void statue_pause(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_actor_set_damageable(monster->base.entity, false);
  q1mpm_set_next_frame(&monster->base, "rogue_statue_pause");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static const char *statue_redirect(void *ctx, qa_q1mpm_mission_monster *monster, const char *name) {
  const variant_ctx *v = ctx;
  return state(monster, "rogue_statue") != 0 &&
                 (q1mpm_str_eq(name, v->spec->stand) || q1mpm_str_eq(name, v->spec->walk) || q1mpm_str_eq(name, v->spec->run))
             ? "rogue_statue_pause"
             : name;
}
static void statue_spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  const variant_ctx *v = ctx;
  const qa_q1_base_monster_species *spec = v->spec;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!has_flag(entity, SPAWN_LYING)) { qa_q1mpm_monster_spawn_default(monster); return; }
  for (size_t i = 0; i < 3; i++) {
    char path[64];
    (void)snprintf(path, sizeof path, "progs/%s.mdl", STATUE_GIBS[i]);
    (void)qa_q1_services_precache_model(game, path);
  }
  static const char *const SOUNDS[4] = {"statue/see.wav", "statue/idle.wav", "statue/pain.wav", "statue/death.wav"};
  for (size_t i = 0; i < 4; i++) (void)qa_q1_services_precache_sound(game, SOUNDS[i]);
  if (entity->frame == 0) entity->frame = v->frozen;
  entity->skin = 1;
  if (entity->targetname == NULL || entity->targetname[0] == '\0') {
    /* The source returns here without starting the monster, so an untargeted statue is inert scenery. */
    char model[64];
    (void)snprintf(model, sizeof model, "progs/%s.mdl", spec->model);
    q1mpm_set_text(&entity->model, model);
    entity->solid = "slidebox";
    entity->movement = "step";
    entity->max_health = spec->health;
    set_health(game, entity, spec->health);
    qa_q1_set_bounds(game, entity, spec->bounds);
    return;
  }
  set_state(monster, "rogue_statue", 1);
  qa_q1mpm_monster_spawn_default(monster);
}
static void statue_use(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *activator) {
  (void)ctx;
  if (state(monster, "rogue_statue") == 0) { qa_q1mpm_monster_use_default(monster, activator); return; }
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_sound_defaults(game, entity->actor.id, "statue/see.wav");
  qa_q1_actor_set_damageable(entity, true);
  set_state(monster, "rogue_statue", 0);
  entity->use = (qa_q1_bound_use){0};
  qa_q1_base_monster_set_enemy(&monster->base, activator);
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "rogue:monster_found"));
}
static const char *statue_sfx(void *ctx, qa_q1mpm_mission_monster *monster, const char *path) {
  (void)ctx;
  if (!has_flag(monster->base.entity, SPAWN_LYING)) return path;
  for (size_t i = 0; i < 6; i++)
    if (strcmp(STATUE_SOUNDS[i][0], path) == 0) return STATUE_SOUNDS[i][1];
  return path;
}
static bool statue_gib_death(void *ctx, qa_q1mpm_mission_monster *monster) {
  const variant_ctx *v = ctx;
  const qa_q1_base_monster_species *spec = v->spec;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!has_flag(entity, SPAWN_LYING)) return qa_q1mpm_monster_gib_death_default(monster);
  if (qa_q1_health(game, entity->actor.id) >= spec->gib_health || spec->head == NULL) return false;
  qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
  qa_q1_throw_head(game, entity, spec->head);
  for (size_t i = 0; i < 3; i++) {
    const qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
    const double damage = qa_q1_health(game, entity->actor.id);
    (void)qa_q1_throw_gib(game, origin, STATUE_GIBS[i], damage);
  }
  return true;
}
static void statue(qa_q1mpm_runtime *runtime, const qa_q1_base_monster_species *spec, qa_q1mpm_pack_monster_definition *out) {
  variant_ctx *v = variant(runtime, spec);
  v->frozen = strcmp(spec->species, "knight") == 0 ? 44 : 73;
  qa_q1mpm_species_definition(spec, out);
  qa_q1mpm_named_action *actions = qa_q1mpm_runtime_calloc(runtime, 1, sizeof *actions);
  actions[0] = (qa_q1mpm_named_action){"rogue_statue_pause", {statue_pause, v}};
  out->actions = actions;
  out->action_count = 1;
  out->redirect = statue_redirect; out->redirect_ctx = v;
  out->spawn = (qa_q1mpm_action){statue_spawn, v};
  out->use = statue_use; out->use_ctx = v;
  out->sfx = statue_sfx; out->sfx_ctx = v;
  out->gib_death = statue_gib_death; out->gib_death_ctx = v;
}

/* ---- zombie.qc: lying (2), ambush (4) and sneaky (8) zombies. ---- */
#define ESCAPE_COUNT 16
static const double ESCAPE_WAITS[ESCAPE_COUNT] = {0.2, 0.2, 0.2, 0.2, 0.15, 0.15, 0.15, 0.15,
                                                  0.15, 0.15, 0.1, 0.1, 0.1, 0.15, 0.15, 0.1};
static const char *const ESCAPE_FRAMES[ESCAPE_COUNT] = {
    "zombie_escape1", "zombie_escape2", "zombie_escape3", "zombie_escape4", "zombie_escape5", "zombie_escape6",
    "zombie_escape7", "zombie_escape8", "zombie_escape9", "zombie_escape10", "zombie_escape11", "zombie_escape12",
    "zombie_escape13", "zombie_escape14", "zombie_escape15", "zombie_escape16"};
static const char *const ESCAPE_ACTIONS[ESCAPE_COUNT] = {
    "rogue_zombie_escape1", "rogue_zombie_escape2", "rogue_zombie_escape3", "rogue_zombie_escape4",
    "rogue_zombie_escape5", "rogue_zombie_escape6", "rogue_zombie_escape7", "rogue_zombie_escape8",
    "rogue_zombie_escape9", "rogue_zombie_escape10", "rogue_zombie_escape11", "rogue_zombie_escape12",
    "rogue_zombie_escape13", "rogue_zombie_escape14", "rogue_zombie_escape15", "rogue_zombie_escape16"};
static const qa_q1_monster_operation ESCAPE_OPS[ESCAPE_COUNT] = {
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape1"},  {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape2"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape3"},  {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape4"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape5"},  {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape6"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape7"},  {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape8"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape9"},  {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape10"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape11"}, {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape12"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape13"}, {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape14"},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape15"}, {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_escape16"}};
static const char *const CRUC_FRAMES[6] = {"rogue_cruc1", "rogue_cruc2", "rogue_cruc3", "rogue_cruc4", "rogue_cruc5", "rogue_cruc6"};
static const qa_q1_monster_operation CRUC_FIRST_OPS[2] = {
    {.kind = QA_Q1_OP_SOUND, .path = "zombie/idle_w2.wav", .channel = "voice", .attenuation = 3, .comparison = "less",
     .has_chance = true, .chance = 0.1},
    {.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_lurk"}};
static const qa_q1_monster_operation CRUC_OPS[1] = {{.kind = QA_Q1_OP_ACTION, .name = "rogue_zombie_lurk"}};
static const qa_q1_monster_operation SLEEP_OPS[1] = {{.kind = QA_Q1_OP_AI, .mode = QA_Q1_AI_STAND, .distance = 0}};

typedef struct escape_ctx { const variant_ctx *zombie; int index; } escape_ctx;
static void zombie_wake(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  set_state(monster, "rogue_zstate", 0);
  qa_q1_base_monster_play(&monster->base, "zombie_paine12");
}
static void zombie_lying_rise(void *ctx, qa_q1mpm_mission_monster *monster) {
  const variant_ctx *v = ctx;
  qa_q1_set_bounds(monster->base.game, monster->base.entity, v->spec->bounds);
  qa_q1_monster_action(&monster->base, "zombie_paine12");
}
static void zombie_escape(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (monster->base.state->pain_finished == 0) monster->base.state->pain_finished = (double)(float)(game->time + entity->delay);
  q1mpm_set_next_frame(&monster->base, "rogue_cruc1");
  qa_q1_base_monster_delay(&monster->base, 0.1);
}
static void zombie_lurk(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  const double pain_finished = monster->base.state->pain_finished;
  if (pain_finished == 0) {
    if (qa_q1_base_monster_find_target(&monster->base)) return;
  } else if (pain_finished < game->time) {
    q1mpm_set_next_frame(&monster->base, "zombie_escape1");
    qa_q1_base_monster_delay(&monster->base, 0.1);
    return;
  }
  qa_q1_base_monster_delay(&monster->base, 0.1 + qa_q1_host_random(game) * 0.1);
}
static void zombie_escape_step(void *ctx, qa_q1mpm_mission_monster *monster) {
  const escape_ctx *step = ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const int frame = step->index + 1;
  if (frame >= 14 && frame <= 15) {
    const qa_vec3 forward = qa_q1_make_vectors(game, qa_q1_get_body(game, entity).angles).forward;
    qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), qa_q1_vscale(forward, 8)));
  }
  if (frame == 16) {
    entity->solid = "slidebox";
    entity->movement = "step";
    qa_q1_actor_set_damageable(entity, true);
    entity->aimed_damage = true;
    set_state(monster, "rogue_zstate", 0);
    set_health(game, entity, 60);
    qa_q1_sound(game, entity->actor.id, "zombie/z_idle.wav", "voice", 2, 1);
    (void)qa_q1mpm_drop_to_floor(monster);
    qa_q1_link(game, entity);
    return;
  }
  qa_q1_base_monster_delay(&monster->base, ESCAPE_WAITS[step->index]);
}
static const char *zombie_redirect(void *ctx, qa_q1mpm_mission_monster *monster, const char *name) {
  const variant_ctx *v = ctx;
  const qa_q1_base_monster_species *spec = v->spec;
  const double mode = state(monster, "rogue_zstate");
  const bool moving = q1mpm_str_eq(name, spec->walk) || q1mpm_str_eq(name, spec->run) || q1mpm_str_eq(name, spec->missile);
  if (mode == 1) return q1mpm_str_eq(name, spec->stand) ? "zombie_sleep" : moving ? "rogue_zombie_wake" : name;
  if (mode == 2) return q1mpm_str_eq(name, spec->stand) ? "rogue_cruc1" : moving ? "rogue_zombie_escape" : name;
  return name;
}
static bool zombie_pre_pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage; /* TS (monster) */
  if (state(monster, "rogue_zstate") != 1) return false;
  set_state(monster, "rogue_zstate", 0);
  qa_q1_base_monster_play(&monster->base, "zombie_paine12");
  return true;
}
static void zombie_spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  const variant_ctx *v = ctx;
  const qa_q1_base_monster_species *spec = v->spec;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const int32_t flags = qa_js_to_int32(entity->spawnflags);
  if ((flags & 1) != 0) { qa_q1mpm_monster_spawn_default(monster); return; }
  if ((flags & SPAWN_LYING) != 0) {
    if ((flags & SPAWN_AMBUSH) != 0) entity->spawnflags = SPAWN_LYING;
    set_state(monster, "rogue_zstate", 1);
    qa_q1mpm_monster_spawn_default(monster);
    entity->solid = "none";
    qa_q1_set_bounds(game, entity, LYING_BOUNDS);
    return;
  }
  if ((flags & SPAWN_SNEAKY) != 0) {
    set_state(monster, "rogue_zstate", 2);
    game->total_monsters++;
    entity->max_health = spec->health;
    set_health(game, entity, spec->health);
    q1mpm_set_text(&entity->model, "progs/zombie.mdl");
    entity->solid = "none";
    entity->movement = "none";
    if (entity->delay == 0) entity->delay = 3;
    entity->yaw_speed = yaw_speed_or_default(entity);
    entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
    entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
    qa_q1_set_bounds(game, entity, spec->bounds);
    entity->pain = qa_q1_callback_registry_pain(game->named, entity, "rogue:monster_pain");
    entity->die = qa_q1_callback_registry_die(game->named, entity, "rogue:monster_die");
    entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:monster_use");
    qa_q1_base_monster_play(&monster->base, spec->stand);
    qa_q1_base_monster_delay(&monster->base, 0.1 + qa_q1_host_random(game) * 0.5);
    return;
  }
  if ((flags & SPAWN_AMBUSH) != 0) entity->spawnflags = SPAWN_LYING;
  qa_q1mpm_monster_spawn_default(monster);
}
static void zombie(qa_q1mpm_runtime *runtime, const qa_q1_base_monster_species *spec, qa_q1mpm_pack_monster_definition *out) {
  variant_ctx *v = variant(runtime, spec);
  qa_q1mpm_species_definition(spec, out);
  /* TS frames: zombie_sleep, zombie_paine12, rogue_cruc1..6, zombie_escape1..16. */
  const size_t frame_count = 2 + 6 + ESCAPE_COUNT;
  qa_q1_monster_frame_entry *frames = qa_q1mpm_runtime_calloc(runtime, frame_count, sizeof *frames);
  size_t f = 0;
  frames[f++] = frame_copy("zombie_sleep", 172, "zombie_sleep", SLEEP_OPS, 1);
  frames[f++] = with_action(runtime, "zombie_paine12", "zombie_paine12", "rogue_zombie_lying_rise");
  for (int index = 1; index <= 6; index++)
    frames[f++] = index == 1 ? frame_copy(CRUC_FRAMES[0], 192, CRUC_FRAMES[1], CRUC_FIRST_OPS, 2)
                             : frame_copy(CRUC_FRAMES[index - 1], 191 + index, CRUC_FRAMES[index % 6], CRUC_OPS, 1);
  for (int index = 0; index < ESCAPE_COUNT; index++) {
    const bool last = index == ESCAPE_COUNT - 1;
    frames[f++] = frame_copy(ESCAPE_FRAMES[index], 192 + index, last ? "zombie_run1" : ESCAPE_FRAMES[index + 1], &ESCAPE_OPS[index], 1);
  }
  out->frames = (qa_q1_monster_frame_table){frames, frame_count};
  /* TS actions record: wake, lying_rise, escape, lurk, then rogue_zombie_escape1..16. */
  const size_t action_count = 4 + ESCAPE_COUNT;
  qa_q1mpm_named_action *actions = qa_q1mpm_runtime_calloc(runtime, action_count, sizeof *actions);
  escape_ctx *steps = qa_q1mpm_runtime_calloc(runtime, ESCAPE_COUNT, sizeof *steps);
  named_action("rogue_zombie_wake", zombie_wake, v, &actions[0]);
  named_action("rogue_zombie_lying_rise", zombie_lying_rise, v, &actions[1]);
  named_action("rogue_zombie_escape", zombie_escape, v, &actions[2]);
  named_action("rogue_zombie_lurk", zombie_lurk, v, &actions[3]);
  for (int index = 0; index < ESCAPE_COUNT; index++) {
    steps[index] = (escape_ctx){v, index};
    named_action(ESCAPE_ACTIONS[index], zombie_escape_step, &steps[index], &actions[4 + index]);
  }
  out->actions = actions;
  out->action_count = action_count;
  out->redirect = zombie_redirect; out->redirect_ctx = v;
  out->pre_pain = zombie_pre_pain; out->pre_pain_ctx = v;
  out->spawn = (qa_q1mpm_action){zombie_spawn, v};
}

/* ---- ogre.qc: boss ogres (spawnflag 2) fire multi-grenades, wear skin 1
 * and drop multi-rockets. ---- */
static void ogre_fire(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (!has_flag(entity, 2)) { qa_q1_launch_ogre_grenade(&monster->base); return; }
  qa_vec3 target;
  if (!qa_q1_base_monster_target(&monster->base, &target)) return;
  qa_q1_effect(game, "muzzleflash", qa_q1_base_monster_origin(&monster->base), &entity->actor.id, 1);
  qa_q1_sound(game, entity->actor.id, "weapons/grenade.wav", "weapon", 1, 1);
  const qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
  const qa_vec3 direction = qa_q1_normalize(qa_q1_vsub(target, origin));
  const qa_vec3 velocity = {direction.x * 600, direction.y * 600, 200};
  qa_q1_actor *grenade = qa_q1mp_launch_rogue_multi_grenade(game, entity->actor.id, qa_q1_base_monster_origin(&monster->base),
                                                            velocity, qa_q1mp_velocity_angles(velocity));
  qa_q1_schedule(game, grenade, 2.5, qa_q1_callback_registry_action(game->named, grenade, "rogue:multi-grenade-split"));
}
static void ogre_drop(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  const bool boss = has_flag(monster->base.entity, 2);
  qa_q1_backpack_contents contents;
  memset(&contents, 0, sizeof contents);
  contents.weapon = NULL;
  contents.rockets = boss ? 0 : 2;
  static const qa_q1_backpack_extra EXTRA[1] = {{"rogue:ammo/multi-rockets", 2}};
  if (boss) { contents.extra = EXTRA; contents.extra_count = 1; contents.has_extra = true; }
  (void)qa_q1_drop_backpack(game, qa_q1_base_monster_origin(&monster->base), &contents, NULL);
}
static void ogre_spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (entity->spawnflags <= 1 && qa_q1_option_skill(game) > 0 && qa_q1_host_random(game) <= 0.25)
    entity->spawnflags = qa_js_bit_or(entity->spawnflags, 2);
  if (has_flag(entity, 2)) entity->skin = 1;
  qa_q1mpm_monster_spawn_default(monster);
}
static void ogre(qa_q1mpm_runtime *runtime, const qa_q1_base_monster_species *spec, qa_q1mpm_pack_monster_definition *out) {
  variant_ctx *v = variant(runtime, spec);
  qa_q1mpm_species_definition(spec, out);
  qa_q1_monster_frame_entry *frames = qa_q1mpm_runtime_calloc(runtime, 3, sizeof *frames);
  frames[0] = with_action(runtime, "ogre_nail4", "ogre_nail4", "rogue_ogre_fire");
  frames[1] = with_action(runtime, "ogre_die3", "ogre_die3", "rogue_ogre_drop");
  frames[2] = with_action(runtime, "ogre_bdie3", "ogre_bdie3", "rogue_ogre_drop");
  out->frames = (qa_q1_monster_frame_table){frames, 3};
  qa_q1mpm_named_action *actions = qa_q1mpm_runtime_calloc(runtime, 2, sizeof *actions);
  named_action("rogue_ogre_fire", ogre_fire, v, &actions[0]);
  named_action("rogue_ogre_drop", ogre_drop, v, &actions[1]);
  out->actions = actions;
  out->action_count = 2;
  out->spawn = (qa_q1mpm_action){ogre_spawn, v};
}

/* ---- tarbaby.qc: skins, health tiers and mitosis. ---- */
static const struct { double skin, health; } TIERS[3] = {{0, 80}, {1, 60}, {2, 100}};
/* TS tiers[index]?.health ?? 80 (and skin ?? 0): only an exact index selects an entry. */
static bool tier_index(double index, size_t *out) {
  if (!(index >= 0 && index <= 2) || floor(index) != index) return false;
  *out = (size_t)index;
  return true;
}
static void mitosis(const variant_ctx *v, qa_q1mpm_mission_monster *monster) {
  qa_q1mpm_runtime *runtime = v->runtime;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_host_random(game) >= 0.35) return;
  qa_q1_actor *child = qa_q1_create(game, entity->classname, NULL, NULL);
  child->movement = "step";
  child->has_owner = true; child->owner = entity->actor.id;
  q1mpm_set_text(&child->model, "progs/tarbaby.mdl");
  child->solid = "slidebox";
  child->skin = entity->skin;
  size_t tier;
  child->max_health = tier_index(entity->skin, &tier) ? TIERS[tier].health : 80;
  set_health(game, child, child->max_health);
  qa_q1_set_bounds(game, child, v->spec->bounds);
  qa_q1_set_origin(game, child, qa_q1_base_monster_origin(&monster->base));
  set_state(monster, "rogue_mitosis", 0);
  qa_q1mpm_mission_monster *controller = qa_q1mpm_mission_monster_create(game, child, monster->definition, runtime);
  qa_q1mpm_runtime_set_monster(runtime, child->actor, controller);
  child->pain = qa_q1_callback_registry_pain(game->named, child, "rogue:monster_pain");
  child->die = qa_q1_callback_registry_die(game->named, child, "rogue:monster_die");
  q1mpm_set_next_frame(&controller->base, "rogue_tbaby_checknew");
  qa_q1_base_monster_delay(&controller->base, 0.5);
  qa_q1_sound_defaults(game, child->actor.id, "blob/mytosis.wav");
}
static void tbaby_checknew(void *ctx, qa_q1mpm_mission_monster *monster) {
  const variant_ctx *v = ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  qa_q1_actor *parent = qa_q1_get_entity(game, entity->has_owner ? &entity->owner : NULL);
  if (!game->host.walk_move(game->host.ctx, entity->actor, 0, 0)) { qa_q1_remove(game, entity); return; }
  qa_q1_set_origin(game, entity, qa_q1_vadd(qa_q1_base_monster_origin(&monster->base), (qa_vec3){0, 0, 1}));
  (void)qa_q1mpm_drop_to_floor(monster);
  if (!game->host.walk_move(game->host.ctx, entity->actor, 0, 0)) {
    if (parent != NULL) set_state(qa_q1mpm_runtime_require(v->runtime, parent), "rogue_mitosis", 1);
    qa_q1_remove(game, entity);
    return;
  }
  qa_q1_actor_set_damageable(entity, true);
  entity->aimed_damage = true;
  entity->ideal_yaw = qa_q1_get_body(game, entity).angles.y;
  entity->yaw_speed = yaw_speed_or_default(entity);
  entity->use = qa_q1_callback_registry_use(game->named, entity, "rogue:monster_use");
  entity->movement_flags = qa_js_bit_or(entity->movement_flags, 32);
  monster->base.state->pause_until = (double)(float)99999999;
  qa_q1_base_monster_play(&monster->base, v->spec->stand);
  qa_q1_base_monster_delay(&monster->base, 0.1 + qa_q1_host_random(game) * 0.5);
  game->total_monsters++;
  qa_q1_event event;
  memset(&event, 0, sizeof event);
  event.kind = "monster-total";
  event.total = game->total_monsters;
  qa_q1_emit(game, event);
}
static bool tarbaby_pre_pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)attacker; (void)damage; /* TS (monster) */
  if (state(monster, "rogue_mitosis") == 0) return false;
  mitosis(ctx, monster);
  return true;
}
static void tarbaby_spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  const int32_t flags = qa_js_to_int32(entity->spawnflags);
  (void)qa_q1_services_precache_sound(game, "blob/mytosis.wav");
  size_t tier = 0;
  bool splits = true;
  if ((flags & SPAWN_SNEAKY) != 0) tier = 2;
  else if ((flags & SPAWN_AMBUSH) != 0) tier = 1;
  else if ((flags & SPAWN_LYING) != 0) splits = false;
  else {
    const double skill = qa_q1_option_skill(game);
    const double roll = (double)(float)((double)(float)(skill * 0.2) + qa_q1_host_random(game));
    if (roll > 0.8 && skill > 1) tier = 2;
    else if (roll > 0.5) tier = 1;
    else splits = false;
  }
  qa_q1mpm_monster_spawn_default(monster);
  entity->skin = TIERS[tier].skin;
  entity->max_health = TIERS[tier].health;
  set_health(game, entity, entity->max_health);
  set_state(monster, "rogue_mitosis", splits ? 1 : 0);
}
static void tarbaby(qa_q1mpm_runtime *runtime, const qa_q1_base_monster_species *spec, qa_q1mpm_pack_monster_definition *out) {
  variant_ctx *v = variant(runtime, spec);
  qa_q1mpm_species_definition(spec, out);
  qa_q1mpm_named_action *actions = qa_q1mpm_runtime_calloc(runtime, 1, sizeof *actions);
  named_action("rogue_tbaby_checknew", tbaby_checknew, v, &actions[0]);
  out->actions = actions;
  out->action_count = 1;
  out->pre_pain = tarbaby_pre_pain; out->pre_pain_ctx = v;
  out->spawn = (qa_q1mpm_action){tarbaby_spawn, v};
}

size_t qa_q1mpm_rogue_species_variants(qa_q1mpm_runtime *runtime, qa_q1mpm_pack_monster_definition *out, size_t capacity) {
  if (capacity < QA_Q1MPM_ROGUE_SPECIES_VARIANT_COUNT) qa_range_error("Rogue species variants need %d slots", QA_Q1MPM_ROGUE_SPECIES_VARIANT_COUNT);
  /* TS array literal order: each species() lookup runs before its builder. */
  statue(runtime, species("monster_knight"), &out[0]);
  statue(runtime, species("monster_hell_knight"), &out[1]);
  zombie(runtime, species("monster_zombie"), &out[2]);
  ogre(runtime, species("monster_ogre"), &out[3]);
  tarbaby(runtime, species("monster_tarbaby"), &out[4]);
  return QA_Q1MPM_ROGUE_SPECIES_VARIANT_COUNT;
}

/* ---- Hipnotic zombie.qc lying zombies (spawnflag 4) and rotfish.qc gibbing
 * below -20 health. ---- */
static void hip_zombie_stand(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  set_health(game, monster->base.entity, 60);
  set_state(monster, "hip_lay", 0);
  qa_q1_base_monster_delay(&monster->base, 0.2);
  if (qa_q1_base_monster_find_target(&monster->base)) qa_q1_base_monster_play(&monster->base, "zombie_lay12");
}
static const qa_q1_monster_operation HIP_LAY_OPS[1] = {{.kind = QA_Q1_OP_ACTION, .name = "hip_zombie_lay"}};
static const qa_q1_monster_frame_entry HIP_ZOMBIE_FRAMES[2] = {
    {"zombie_lay", {172, "zombie_lay", HIP_LAY_OPS, 1}},
    {"zombie_lay12", {173, "zombie_paine13", NULL, 0}},
};
static const qa_q1mpm_named_action HIP_ZOMBIE_ACTIONS[1] = {{"hip_zombie_lay", {hip_zombie_stand, NULL}}};
static const char *hip_zombie_redirect(void *ctx, qa_q1mpm_mission_monster *monster, const char *name) {
  (void)ctx;
  return state(monster, "hip_lay") != 0 && q1mpm_str_eq(name, monster->base.spec->stand) ? "zombie_lay" : name;
}
static bool hip_zombie_pre_pain(void *ctx, qa_q1mpm_mission_monster *monster, const qa_actor_id *attacker, double damage) {
  (void)ctx; (void)attacker; (void)damage; /* TS (monster) */
  if (strcmp(monster->base.current_frame, "zombie_lay") != 0) return false;
  qa_q1_base_monster_play(&monster->base, "zombie_lay12");
  return true;
}
static void hip_zombie_spawn(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  if ((qa_js_to_int32(monster->base.entity->spawnflags) & 5) == 4) set_state(monster, "hip_lay", 1);
  qa_q1mpm_monster_spawn_default(monster);
}
static bool hip_fish_gib_death(void *ctx, qa_q1mpm_mission_monster *monster) {
  (void)ctx;
  qa_q1_services *game = monster->base.game;
  qa_q1_actor *entity = monster->base.entity;
  if (qa_q1_health(game, entity->actor.id) >= -20) return false;
  qa_q1_sound_defaults(game, entity->actor.id, "player/udeath.wav");
  static const char *const GIBS[3] = {"gib1", "gib2", "gib3"};
  for (size_t i = 0; i < 3; i++) {
    const qa_vec3 origin = qa_q1_base_monster_origin(&monster->base);
    const double damage = qa_q1_health(game, entity->actor.id);
    (void)qa_q1_throw_gib(game, origin, GIBS[i], damage);
  }
  qa_q1_schedule(game, entity, 0.1, qa_q1_callback_registry_action(game->named, entity, "SUB_Remove"));
  return true;
}
bool qa_q1mpm_hipnotic_species_variant(const char *classname, qa_q1mpm_pack_monster_definition *out) {
  if (strcmp(classname, "monster_zombie") == 0) {
    out->frames = (qa_q1_monster_frame_table){HIP_ZOMBIE_FRAMES, 2};
    out->actions = HIP_ZOMBIE_ACTIONS;
    out->action_count = 1;
    out->redirect = hip_zombie_redirect;
    out->pre_pain = hip_zombie_pre_pain;
    out->spawn = (qa_q1mpm_action){hip_zombie_spawn, NULL};
    return true;
  }
  if (strcmp(classname, "monster_fish") == 0) {
    out->gib_death = hip_fish_gib_death;
    return true;
  }
  return false;
}
