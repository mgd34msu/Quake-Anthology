/* Port of src/content/q1/addons/monsters/ai/controller.ts; also the
 * mirrored BaseMonster/actor helpers declared in controller.h
 * (TS owners: src/content/q1/base/monsters.ts,
 * src/content/q1/foundation/entity.ts). GPL-2.0-or-later. */
#include "qa/content/q1/addons/monsters/ai/controller.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/addons/monsters/ai/path.h"
#include "qa/foundation/support.h"

#define FR(x) ((double)(float)(x))
#define FLAGS(x) ((int32_t)(x))

const qa_vec3 QA_Q1AM_ZERO = {0, 0, 0};
const qa_bounds QA_Q1AM_POINT = {{0, 0, 0}, {0, 0, 0}};

qa_vec3 qa_q1am_normalize(qa_vec3 a) {
  double magnitude = qa_length3(a);
  return magnitude == 0 ? QA_Q1AM_ZERO : qa_scale3(a, 1 / magnitude);
}
double qa_q1am_yaw_for(qa_vec3 direction) {
  double yaw = atan2(direction.y, direction.x) * 180 / M_PI;
  return yaw < 0 ? yaw + 360 : yaw;
}
qa_vec3 qa_q1am_velocity_angles(qa_vec3 velocity) {
  double yaw = velocity.x == 0 && velocity.y == 0 ? 0 : atan2(velocity.y, velocity.x) * 180 / M_PI;
  double pitch = velocity.x == 0 && velocity.y == 0
                     ? (velocity.z > 0 ? 90 : 270)
                     : atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / M_PI;
  qa_vec3 out = {FR(pitch < 0 ? pitch + 360 : pitch), FR(yaw < 0 ? yaw + 360 : yaw), 0};
  return out;
}

const char *qa_q1am_ai_name(qa_q1am_ai mode) {
  switch (mode) {
    case QA_Q1AM_AI_STAND: return "stand";
    case QA_Q1AM_AI_WALK: return "walk";
    case QA_Q1AM_AI_RUN: return "run";
    case QA_Q1AM_AI_CHARGE_SIDE: return "charge_side";
    case QA_Q1AM_AI_MELEE_SIDE: return "melee_side";
    case QA_Q1AM_AI_CHARGE: return "charge";
    case QA_Q1AM_AI_MELEE: return "melee";
    case QA_Q1AM_AI_PAINFORWARD: return "painforward";
    case QA_Q1AM_AI_TURN: return "turn";
    case QA_Q1AM_AI_FACE: return "face";
    case QA_Q1AM_AI_PAIN: return "pain";
    case QA_Q1AM_AI_FORWARD: return "forward";
  }
  return "stand";
}
const qa_q1am_frame *qa_q1am_frame_table_get(const qa_q1am_frame_table *table, const char *name) {
  if (table == NULL) return NULL;
  for (size_t i = 0; i < table->count; i++)
    if (strcmp(table->frames[i].name, name) == 0) return &table->frames[i];
  return NULL;
}

static void copy_str(char *dst, size_t cap, const char *src) {
  if (cap == 0) return;
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}
void qa_q1am_monster_set_frame(char *dst, size_t cap, const char *name) { copy_str(dst, cap, name); }

void qa_q1am_actor_init(qa_q1am_actor *actor, qa_owned_actor owned, const char *classname) {
  memset(actor, 0, sizeof *actor);
  actor->actor = owned;
  copy_str(actor->classname, sizeof actor->classname, classname);
  actor->solid = "none";
  actor->movement = "none";
  actor->next_think = -1;
  actor->yaw_speed = 20;
  copy_str(actor->attack_state, sizeof actor->attack_state, "straight");
}
void qa_q1am_actor_destroy(qa_q1am_actor *actor) {
  free(actor->fields);
  free(actor->refs);
  actor->fields = NULL;
  actor->refs = NULL;
  actor->field_count = actor->field_cap = actor->ref_count = actor->ref_cap = 0;
}
const char *qa_q1am_actor_text(const qa_q1am_actor *actor, const char *key) {
  for (size_t i = 0; i < actor->field_count; i++)
    if (strcmp(actor->fields[i].key, key) == 0) return actor->fields[i].value;
  return "";
}
bool qa_q1am_actor_has_field(const qa_q1am_actor *actor, const char *key) {
  for (size_t i = 0; i < actor->field_count; i++)
    if (strcmp(actor->fields[i].key, key) == 0) return true;
  return false;
}
void qa_q1am_actor_set_field(qa_q1am_actor *actor, const char *key, const char *value) {
  for (size_t i = 0; i < actor->field_count; i++)
    if (strcmp(actor->fields[i].key, key) == 0) {
      copy_str(actor->fields[i].value, sizeof actor->fields[i].value, value);
      return;
    }
  if (actor->field_count == actor->field_cap) {
    size_t cap = actor->field_cap ? actor->field_cap * 2 : 8;
    qa_q1am_field *next = realloc(actor->fields, cap * sizeof *next);
    if (next == NULL) qa_range_error("Q1 addon monster field allocation failed");
    actor->fields = next;
    actor->field_cap = cap;
  }
  qa_q1am_field *field = &actor->fields[actor->field_count++];
  copy_str(field->key, sizeof field->key, key);
  copy_str(field->value, sizeof field->value, value);
}
double qa_q1am_actor_number_or(const qa_q1am_actor *actor, const char *key, double fallback) {
  const char *text = qa_q1am_actor_text(actor, key);
  if (text[0] == '\0') return fallback;
  double value = strtod(text, NULL);
  return isfinite(value) ? FR(value) : fallback;
}
double qa_q1am_actor_number(const qa_q1am_actor *actor, const char *key) {
  return qa_q1am_actor_number_or(actor, key, 0);
}
qa_vec3 qa_q1am_actor_vector(const qa_q1am_actor *actor, const char *key) {
  const char *text = qa_q1am_actor_text(actor, key);
  double values[3] = {0, 0, 0};
  const char *p = text;
  for (int i = 0; i < 3 && *p; i++) {
    char *end = NULL;
    values[i] = strtod(p, &end);
    if (end == p) break;
    p = end;
  }
  qa_vec3 out = {isfinite(values[0]) ? FR(values[0]) : 0, isfinite(values[1]) ? FR(values[1]) : 0,
                 isfinite(values[2]) ? FR(values[2]) : 0};
  return out;
}
bool qa_q1am_actor_ref(const qa_q1am_actor *actor, const char *key, qa_actor_id *out) {
  for (size_t i = 0; i < actor->ref_count; i++)
    if (strcmp(actor->refs[i].key, key) == 0) {
      if (actor->refs[i].has && out) *out = actor->refs[i].id;
      return actor->refs[i].has;
    }
  return false;
}
void qa_q1am_actor_set_ref(qa_q1am_actor *actor, const char *key, bool has, qa_actor_id id) {
  for (size_t i = 0; i < actor->ref_count; i++)
    if (strcmp(actor->refs[i].key, key) == 0) {
      actor->refs[i].has = has;
      actor->refs[i].id = id;
      return;
    }
  if (actor->ref_count == actor->ref_cap) {
    size_t cap = actor->ref_cap ? actor->ref_cap * 2 : 8;
    qa_q1am_ref *next = realloc(actor->refs, cap * sizeof *next);
    if (next == NULL) qa_range_error("Q1 addon monster reference allocation failed");
    actor->refs = next;
    actor->ref_cap = cap;
  }
  qa_q1am_ref *ref = &actor->refs[actor->ref_count++];
  copy_str(ref->key, sizeof ref->key, key);
  ref->has = has;
  ref->id = id;
}
void qa_q1am_format_fround(char *out, size_t cap, double value) {
  (void)snprintf(out, cap, "%.9g", FR(value));
}
void qa_q1am_actor_set_field_number(qa_q1am_actor *actor, const char *key, double value) {
  char text[64];
  qa_q1am_format_fround(text, sizeof text, value);
  qa_q1am_actor_set_field(actor, key, text);
}
bool qa_q1am_same_owned(qa_owned_actor a, qa_owned_actor b) {
  return qa_same_actor(a.id, b.id) && a.owner == b.owner && a.token == b.token;
}
bool qa_q1am_same_opt(bool has_a, qa_actor_id a, bool has_b, qa_actor_id b) {
  return !has_a || !has_b ? has_a == has_b : qa_same_actor(a, b);
}
double qa_q1am_player_powerup(const qa_q1am_player *player, const char *name) {
  for (size_t i = 0; i < player->powerup_count; i++)
    if (strcmp(player->powerups[i].name, name) == 0) return player->powerups[i].expires;
  return 0;
}

/* Mirrored baseSpecies (TS owner: src/content/q1/base/species.ts). */
static const char *const bs_knight[] = {"monster_knight"};
static const char *const bs_enforcer[] = {"monster_enforcer"};
static const char *const bs_demon[] = {"monster_demon1"};
static const char *const bs_ogre[] = {"monster_ogre", "monster_ogre_marksman"};
static const char *const bs_hellknight[] = {"monster_hell_knight"};
static const char *const bs_shambler[] = {"monster_shambler"};
static const char *const bs_wizard[] = {"monster_wizard"};
static const char *const bs_shalrath[] = {"monster_shalrath"};
static const char *const bs_tarbaby[] = {"monster_tarbaby"};
static const char *const bs_fish[] = {"monster_fish"};
static const char *const bs_zombie[] = {"monster_zombie"};
static const char *const bs_boss[] = {"monster_boss"};
static const char *const bs_oldone[] = {"monster_oldone"};
static const char *const bs_gibs[] = {"gib1", "gib2", "gib3"};
static const char *const bs_gibs1[] = {"gib1", "gib1", "gib1"};
static const char *const bs_gibs2[] = {"gib2", "gib2", "gib2"};
static const char *const bs_gibs3[] = {"gib3", "gib3", "gib3"};
#define BS_HUMAN {{-16, -16, -24}, {16, 16, 40}}
#define BS_LARGE {{-32, -32, -24}, {32, 32, 64}}
/* -Infinity gibHealth as -INFINITY via designated fallback below. */
const qa_q1am_species qa_q1am_base_species[] = {
  {"knight", "$qc_ks_knight", "knight", "h_knight", "knight_stand1", "knight_walk1", "knight_run1", "knight/ksight.wav", NULL, "walk", bs_knight, 1, bs_gibs, 3, 75, -40, BS_HUMAN, true},
  {"enforcer", "$qc_ks_enforcer", "enforcer", "h_mega", "enf_stand1", "enf_walk1", "enf_run1", "enforcer/sight1.wav", "enf_atk1", "walk", bs_enforcer, 1, bs_gibs, 3, 80, -35, BS_HUMAN, false},
  {"demon", "$qc_ks_fiend", "demon", "h_demon", "demon1_stand1", "demon1_walk1", "demon1_run1", "demon/sight2.wav", "demon1_jump1", "walk", bs_demon, 1, bs_gibs1, 3, 300, -80, BS_LARGE, true},
  {"ogre", "$qc_ks_ogre", "ogre", "h_ogre", "ogre_stand1", "ogre_walk1", "ogre_run1", "ogre/ogwake.wav", "ogre_nail1", "walk", bs_ogre, 2, bs_gibs3, 3, 200, -80, BS_LARGE, true},
  {"hellknight", "$qc_ks_deathknight", "hknight", "h_hellkn", "hknight_stand1", "hknight_walk1", "hknight_run1", "hknight/sight1.wav", "hknight_magicc1", "walk", bs_hellknight, 1, bs_gibs, 3, 250, -40, BS_HUMAN, true},
  {"shambler", "$qc_ks_shambler", "shambler", "h_shams", "sham_stand1", "sham_walk1", "sham_run1", "shambler/ssight.wav", "sham_magic1", "walk", bs_shambler, 1, bs_gibs, 3, 600, -60, BS_LARGE, true},
  {"wizard", "$qc_ks_scrag", "wizard", "h_wizard", "wiz_stand1", "wiz_walk1", "wiz_run1", "wizard/wsight.wav", "wiz_fast1", "fly", bs_wizard, 1, bs_gibs2, 3, 80, -40, BS_HUMAN, false},
  {"shalrath", "$qc_ks_vore", "shalrath", "h_shal", "shal_stand", "shal_walk1", "shal_run1", "shalrath/sight.wav", "shal_attack1", "walk", bs_shalrath, 1, bs_gibs, 3, 400, -90, BS_LARGE, false},
  {"tarbaby", "$qc_ks_spawn", "tarbaby", NULL, "tbaby_stand1", "tbaby_walk1", "tbaby_run1", "blob/sight1.wav", "tbaby_jump1", "walk", bs_tarbaby, 1, NULL, 0, 80, -INFINITY, BS_HUMAN, true},
  {"fish", "$qc_ks_rotfish", "fish", NULL, "f_stand1", "f_walk1", "f_run1", "fish/idle.wav", NULL, "swim", bs_fish, 1, NULL, 0, 25, -INFINITY, {{-16, -16, -24}, {16, 16, 24}}, true},
  {"zombie", "$qc_ks_zombie", "zombie", "h_zombie", "zombie_stand1", "zombie_walk1", "zombie_run1", "zombie/z_idle.wav", "zombie_atta1", "walk", bs_zombie, 1, bs_gibs, 3, 60, 0, BS_HUMAN, false},
  {"boss", "$qc_ks_chthon", "boss", NULL, "boss_idle1", "boss_idle1", "boss_missile1", "boss1/sight1.wav", "boss_missile1", "boss", bs_boss, 1, NULL, 0, 3, -INFINITY, {{-128, -128, -24}, {128, 128, 256}}, false},
  {"oldone", "$qc_ks_shub", "oldone", NULL, "old_idle1", "old_idle1", "old_idle1", "boss2/sight.wav", NULL, "boss", bs_oldone, 1, NULL, 0, 40000, -INFINITY, {{-160, -128, -24}, {160, 128, 256}}, false},
};
const size_t qa_q1am_base_species_count = sizeof qa_q1am_base_species / sizeof qa_q1am_base_species[0];

qa_q1am_basis qa_q1am_game_make_vectors(qa_q1am_game *game, qa_vec3 angles) {
  game->basis = game->make_vectors_raw(game->ctx, angles);
  return game->basis;
}
bool qa_q1am_actor_damageable(const qa_q1am_game *game, const qa_q1am_actor *entity) {
  return game->can_take_damage(game->ctx, entity->actor.id);
}
void qa_q1am_actor_set_damageable(qa_q1am_game *game, qa_q1am_actor *entity, bool value) {
  game->combat_set_can_take_damage(game->ctx, entity->actor, value);
}

void qa_q1am_monster_init(qa_q1am_monster *m, qa_q1am_game *game, qa_q1am_actor *entity,
                          const qa_q1am_species *spec, qa_q1am_base *services,
                          const qa_q1am_source *source) {
  memset(m, 0, sizeof *m);
  m->game = game;
  m->entity = entity;
  m->spec = *spec;
  m->services = services;
  m->source = source;
  m->ops = services->base_ops;
  m->kind = QA_Q1AM_KIND_BASE;
  m->owner = m;
  copy_str(m->current_frame, sizeof m->current_frame, spec->stand);
  copy_str(m->next_frame, sizeof m->next_frame, spec->stand);
  if (!entity->has_monster) {
    memset(&entity->monster, 0, sizeof entity->monster);
    entity->monster.species = spec->species;
    copy_str(entity->monster.mode, sizeof entity->monster.mode, "stand");
    copy_str(entity->monster.path, sizeof entity->monster.path, entity->target);
    entity->has_monster = true;
  }
  m->state = &entity->monster;
  qa_q1am_actor_set_field(entity, "source.monsterCallbackPrefix",
                          source ? source->callback_prefix : "base");
}
qa_save_value *qa_q1am_monster_capture(const qa_q1am_monster *m) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  qa_save_value_set(object, "currentFrame", qa_save_value_string(m->current_frame));
  qa_save_value_set(object, "nextFrame", qa_save_value_string(m->next_frame));
  qa_save_value_set(object, "inPain", qa_save_value_number(m->in_pain));
  qa_save_value_set(object, "counter", qa_save_value_number(m->counter));
  qa_save_value_set(object, "idleUntil", qa_save_value_number(m->idle_until));
  qa_save_value_set(object, "lefty", qa_save_value_boolean(m->lefty));
  qa_save_value_set(object, "sliding", qa_save_value_boolean(m->sliding));
  qa_save_value_set(object, "lightningCount", qa_save_value_number(m->lightning_count));
  qa_save_value_set(object, "countedDeath", qa_save_value_boolean(m->counted_death));
  return object;
}
static bool monster_known(const qa_q1am_monster *m, const char *name) {
  if (m->game->base_frame(m->game->ctx, name) != NULL) return true;
  if (m->source && qa_q1am_frame_table_get(m->source->frames, name) != NULL) return true;
  if (m->source)
    for (size_t i = 0; i < m->source->action_count; i++)
      if (strcmp(m->source->actions[i].name, name) == 0) return true;
  return false;
}
static double reader_number_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(reader, name, &field);
  double value = qa_save_reader_number(&field);
  qa_save_reader_destroy(&field);
  return value;
}
static bool reader_bool_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  qa_save_reader_field(reader, name, &field);
  bool value = qa_save_reader_boolean(&field);
  qa_save_reader_destroy(&field);
  return value;
}
void qa_q1am_monster_restore(qa_q1am_monster *m, const qa_save_reader *reader) {
  qa_save_reader field;
  qa_save_reader_field(reader, "currentFrame", &field);
  copy_str(m->current_frame, sizeof m->current_frame, qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  qa_save_reader_field(reader, "nextFrame", &field);
  copy_str(m->next_frame, sizeof m->next_frame, qa_save_reader_string(&field));
  qa_save_reader_destroy(&field);
  if (!monster_known(m, m->current_frame) || !monster_known(m, m->next_frame))
    qa_save_reader_fail(reader, "unknown source monster continuation");
  m->in_pain = reader_number_field(reader, "inPain");
  m->counter = reader_number_field(reader, "counter");
  m->idle_until = reader_number_field(reader, "idleUntil");
  m->lefty = reader_bool_field(reader, "lefty");
  m->sliding = reader_bool_field(reader, "sliding");
  m->lightning_count = reader_number_field(reader, "lightningCount");
  m->counted_death = reader_bool_field(reader, "countedDeath");
}
qa_vec3 qa_q1am_monster_origin(const qa_q1am_monster *m) {
  return m->game->body(m->game->ctx, m->entity).origin;
}
bool qa_q1am_monster_target(const qa_q1am_monster *m, qa_vec3 *out) {
  if (!m->state->has_enemy) return false;
  qa_q1am_body body;
  if (!m->game->body_read(m->game->ctx, m->state->enemy, &body)) return false;
  *out = body.origin;
  return true;
}
double qa_q1am_monster_distance(const qa_q1am_monster *m) {
  qa_vec3 target;
  return qa_q1am_monster_target(m, &target)
             ? qa_length3(qa_sub3(target, qa_q1am_monster_origin(m))) : INFINITY;
}
bool qa_q1am_monster_eye(const qa_q1am_monster *m, qa_actor_id target, qa_vec3 *out) {
  qa_q1am_body body;
  if (!m->game->body_read(m->game->ctx, target, &body)) return false;
  qa_q1am_actor *entity = m->game->entity(m->game->ctx, true, target);
  qa_vec3 offset;
  if (entity && qa_q1am_actor_has_field(entity, "view_ofs")) {
    offset = qa_q1am_actor_vector(entity, "view_ofs");
  } else {
    double z = 25;
    if (entity && entity->has_monster && strcmp(entity->monster.species, "fish") == 0) z = 10;
    else {
      qa_monster_target_observation observation;
      if (m->game->monster_target(m->game->ctx, target, &observation)) z = observation.view_height;
    }
    offset = (qa_vec3){0, 0, z};
  }
  *out = qa_add3(body.origin, offset);
  return true;
}
double qa_q1am_monster_range_distance(const qa_q1am_monster *m, bool has, qa_actor_id target) {
  qa_vec3 start, end;
  if (!qa_q1am_monster_eye(m, m->entity->actor.id, &start)) return INFINITY;
  if (!has) {
    if (!m->state->has_enemy) return INFINITY;
    target = m->state->enemy;
  }
  if (!qa_q1am_monster_eye(m, target, &end)) return INFINITY;
  return qa_length3(qa_sub3(end, start));
}
bool qa_q1am_monster_visible(const qa_q1am_monster *m, bool has, qa_actor_id target) {
  if (!has) {
    if (!m->state->has_enemy) return false;
    target = m->state->enemy;
  }
  qa_vec3 start, end;
  if (!qa_q1am_monster_eye(m, m->entity->actor.id, &start) || !qa_q1am_monster_eye(m, target, &end)) return false;
  qa_q1am_trace_request request = {start, end, QA_Q1AM_POINT, true, m->entity->actor.id, false};
  qa_q1am_trace trace = m->game->trace(m->game->ctx, &request);
  return trace.fraction == 1 && !(trace.in_open && trace.in_water);
}
void qa_q1am_monster_delay(qa_q1am_monster *m, double seconds) {
  char name[200];
  (void)snprintf(name, sizeof name, "%s:monster_frame", m->source ? m->source->callback_prefix : "base");
  m->game->schedule(m->game->ctx, m->entity, seconds, m->game->named_action(m->game->ctx, m->entity, name));
}
void qa_q1am_monster_change_yaw(qa_q1am_monster *m) {
  m->game->change_yaw(m->game->ctx, m->entity->actor);
}
void qa_q1am_monster_face(qa_q1am_monster *m) {
  qa_vec3 target;
  if (!qa_q1am_monster_target(m, &target)) return;
  qa_q1am_body body = m->game->body(m->game->ctx, m->entity);
  m->entity->ideal_yaw = qa_q1am_yaw_for(qa_sub3(target, body.origin));
  qa_q1am_monster_change_yaw(m);
}
double qa_q1am_monster_melee(qa_q1am_monster *m, double range, double scale, double rolls, bool sight) {
  if (!m->state->has_enemy || qa_q1am_monster_distance(m) > range ||
      (sight && !m->game->can_damage(m->game->ctx, m->state->enemy, m->entity->actor.id)))
    return 0;
  double damage = 0;
  for (double i = 0; i < rolls; i++) damage = FR(damage + m->game->random(m->game->ctx));
  damage = FR(damage * scale);
  m->game->damage(m->game->ctx, m->state->enemy, m->entity->actor.id, true, m->entity->actor.id, damage);
  return damage;
}
void qa_q1am_monster_retaliate(qa_q1am_monster *m, bool has, qa_actor_id attacker) {
  if (!has || qa_same_actor(attacker, m->entity->actor.id) ||
      strcmp(m->game->classname_of(m->game->ctx, attacker), m->entity->classname) == 0)
    return;
  if (m->game->world && qa_same_actor(attacker, m->game->world->actor.id)) return;
  if (m->state->has_enemy && qa_same_actor(attacker, m->state->enemy)) return;
  if (m->state->has_enemy && m->game->is_player(m->game->ctx, true, m->state->enemy)) {
    m->state->has_old_enemy = true;
    m->state->old_enemy = m->state->enemy;
  }
  m->ops.found(m, attacker);
}
void qa_q1am_monster_count_kill(qa_q1am_monster *m) {
  if (m->counted_death) return;
  m->counted_death = true;
  qa_q1am_game *game = m->game;
  const qa_monster_mission *mission = game->mission(game->ctx, m->entity->actor.id);
  if (mission) {
    mission->killed(mission->ctx, m->state->has_enemy ? &m->state->enemy : NULL);
  } else if (m->services->count_monster_kill(m->services->ctx, m)) {
    game->killed_monsters++;
    qa_q1am_event event = {0};
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = m->entity->actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    game->emit(game->ctx, &event);
  }
  if (game->options.edition && strcmp(game->options.edition, "rerelease") == 0 &&
      (FLAGS(m->entity->movement_flags) & 32) != 0 && m->state->has_enemy &&
      !qa_same_actor(m->state->enemy, m->entity->actor.id)) {
    qa_q1am_actor *enemy = game->entity(game->ctx, true, m->state->enemy);
    if (((enemy ? FLAGS(enemy->movement_flags) : 0) & 32) != 0) {
      qa_q1am_event event = {0};
      event.kind = "achievement";
      event.id = "ACH_FRIENDLY_FIRE";
      game->emit(game->ctx, &event);
    }
  }
  m->entity->movement_flags = (double)(FLAGS(m->entity->movement_flags) & ~3);
  if (!mission) game->use_targets(game->ctx, m->entity, m->state->has_enemy, m->state->enemy);
}
bool qa_q1am_monster_route(const qa_q1am_monster *m, qa_actor_id *out) {
  const qa_monster_mission *mission = m->game->mission(m->game->ctx, m->entity->actor.id);
  if (mission) {
    const qa_actor_id *route = mission->route(mission->ctx);
    if (route) *out = *route;
    return route != NULL;
  }
  if (m->state->path[0] == '\0') return false;
  size_t count = 0;
  qa_q1am_actor **found = m->game->find(m->game->ctx, m->state->path, &count);
  bool has = count > 0;
  if (has) *out = found[0]->actor.id;
  free(found);
  return has;
}
void qa_q1am_monster_update_route(qa_q1am_monster *m) {
  qa_actor_id goal;
  if (!qa_q1am_monster_route(m, &goal) || m->state->pause_until > m->game->time) {
    copy_str(m->state->mode, sizeof m->state->mode, "stand");
    m->ops.play(m, m->spec.stand);
    return;
  }
  if (strcmp(m->state->mode, "stand") == 0) {
    copy_str(m->state->mode, sizeof m->state->mode, "walk");
    m->ops.play(m, m->spec.walk);
  }
}
void qa_q1am_monster_move_to_enemy(qa_q1am_monster *m, double distance) {
  if (m->state->has_enemy) m->game->move_to_goal(m->game->ctx, m->entity->actor, m->state->enemy, distance);
}
bool qa_q1am_monster_search_for_coop_target(qa_q1am_monster *m) {
  return m->game->options.coop && m->state->search_until < m->game->time && m->ops.find_target(m);
}

/* ---- Mg3Monster ---- */
static qa_q1am_basis mg3_make_vectors(qa_q1am_monster *m) {
  return qa_q1am_game_make_vectors(m->game, m->game->body(m->game->ctx, m->entity).angles);
}
static void mg3_attack_finished(qa_q1am_monster *m, double seconds) {
  m->state->refired = false;
  m->state->attack_finished = FR(m->game->time + seconds);
}
static void mg3_sight_sound(qa_q1am_monster *m) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  const char *sound = m->spec.sight;
  char computed[64];
  if (strcmp(entity->classname, "monster_ogre") == 0 && qa_q1am_actor_number(entity, "aflag") != 0)
    sound = "armagon/sight.wav";
  else if (strcmp(entity->classname, "monster_hell_knight") == 0 && strcmp(entity->solid, "none") == 0 &&
           (FLAGS(entity->spawnflags) & (65536 | 8388608)) != 0)
    return;
  else if (strcmp(entity->classname, "monster_enforcer") == 0) {
    double choice = floor(game->random(game->ctx) * 3 + 0.5);
    (void)snprintf(computed, sizeof computed, "enforcer/sight%d.wav",
                   choice == 1 ? 1 : choice == 2 ? 2 : choice == 0 ? 3 : 4);
    sound = computed;
  }
  if (sound[0] != '\0') game->sound(game->ctx, entity, sound, "voice", 1);
}
void qa_q1am_mg3_hunt_target(qa_q1am_monster *m) {
  qa_vec3 target;
  if (!qa_q1am_monster_target(m, &target)) return;
  copy_str(m->state->mode, sizeof m->state->mode, "run");
  m->entity->ideal_yaw = qa_q1am_yaw_for(qa_sub3(target, qa_q1am_monster_origin(m)));
  copy_str(m->next_frame, sizeof m->next_frame, m->spec.run);
  qa_q1am_monster_delay(m, 0.1);
  m->ops.attack_finished(m, 1);
}
static void mg3_found(qa_q1am_monster *m, qa_actor_id target) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  m->state->has_enemy = true;
  m->state->enemy = target;
  if (game->is_player(game->ctx, true, target)) {
    game->sight_entity = entity;
    game->sight_time = game->time;
  }
  if (strcmp(entity->classname, "monster_hell_knight") == 0 &&
      (FLAGS(entity->spawnflags) & (65536 | 8388608)) != 0 && m->state->pain_finished > game->time)
    return;
  qa_q1am_actor_set_field_number(entity, "show_hostile", game->time + 1);
  m->ops.sight_sound(m);
  qa_q1am_mg3_hunt_target(m);
}
static bool mg3_check_attack(qa_q1am_monster *m) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  bool has_enemy = m->state->has_enemy;
  qa_vec3 start, end;
  bool has_start = qa_q1am_monster_eye(m, entity->actor.id, &start);
  bool has_end = has_enemy && qa_q1am_monster_eye(m, m->state->enemy, &end);
  if (!has_enemy || !has_start || !has_end) return false;
  qa_q1am_trace_request request = {start, end, QA_Q1AM_POINT, true, entity->actor.id, true};
  qa_q1am_trace trace = game->trace(game->ctx, &request);
  if (!trace.has_actor || !qa_same_actor(trace.actor, m->state->enemy) || (trace.in_open && trace.in_water)) return false;
  double range = game->world ? qa_q1am_actor_number(game->world, "enemy_range") : 0;
  if (range == 0 && m->spec.melee) {
    m->ops.melee_attack(m);
    return true;
  }
  if (m->spec.missile == NULL || game->time < m->state->attack_finished || range == 3) return false;
  if (range == 0) m->state->attack_finished = 0;
  double chance = range == 0 ? 0.9 : range == 1 ? (m->spec.melee ? 0.2 : 0.4) : range == 2 ? (m->spec.melee ? 0.05 : 0.1) : 0;
  if (game->random(game->ctx) >= chance) return false;
  m->ops.play(m, m->spec.missile);
  m->ops.attack_finished(m, 2 * game->random(game->ctx));
  return true;
}
void qa_q1am_mg3_source_word(qa_q1am_monster *m, const char *name, double value) {
  if (m->game->world == NULL) qa_range_error("MG3 AI requires the source world globals");
  qa_q1am_actor_set_field_number(m->game->world, name, value);
}
double qa_q1am_mg3_range_category(qa_q1am_monster *m, bool has, qa_actor_id target) {
  double distance = qa_q1am_monster_range_distance(m, has, target);
  bool near_sighted = (FLAGS(m->entity->spawnflags) & 8192) != 0;
  double melee, near, mid;
  if (near_sighted) {
    /* TS branches on this.enemy, not the target parameter. */
    if (!m->state->has_enemy) { melee = 120; near = 300; mid = 340; }
    else { melee = 96; near = 400; mid = 800; }
  } else { melee = 120; near = 500; mid = 1000; }
  return distance < melee ? 0 : distance < near ? 1 : distance < mid ? 2 : 3;
}
bool qa_q1am_mg3_in_front(qa_q1am_monster *m, qa_actor_id target) {
  qa_q1am_body body;
  if (!m->game->body_read(m->game->ctx, target, &body)) return false;
  qa_q1am_basis basis = qa_q1am_game_make_vectors(m->game, m->game->body(m->game->ctx, m->entity).angles);
  double facing = qa_dot3(qa_q1am_normalize(qa_sub3(body.origin, qa_q1am_monster_origin(m))), basis.forward);
  double threshold = (FLAGS(m->entity->spawnflags) & 8192) != 0 ? FR(0.866) : FR(0.3);
  return facing > threshold || (strcmp(m->entity->classname, "monster_orb") == 0 && -facing > threshold);
}
static bool mg3_find_target(qa_q1am_monster *m) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if ((FLAGS(entity->spawnflags) & 32) != 0) {
    size_t count = 0, damageable = 0;
    qa_q1am_actor **targets = game->find(game->ctx, entity->target, &count);
    for (size_t i = 0; i < count; i++)
      if (qa_q1am_actor_damageable(game, targets[i])) damageable++;
    if (damageable > 0) {
      size_t wanted = (size_t)floor((double)damageable * game->random(game->ctx));
      qa_q1am_actor *target = NULL;
      for (size_t i = 0, seen = 0; i < count; i++)
        if (qa_q1am_actor_damageable(game, targets[i]) && seen++ == wanted) target = targets[i];
      if (target == NULL) {
        free(targets);
        qa_range_error("Source random target index exceeds candidates");
      }
      qa_actor_id id = target->actor.id;
      free(targets);
      m->state->has_enemy = true;
      m->state->enemy = id;
      m->ops.found(m, id);
      return true;
    }
    free(targets);
  }
  bool has_candidate = false;
  qa_actor_id candidate = {0};
  if (game->sight_time >= game->time - 0.1 && (FLAGS(entity->spawnflags) & 3) == 0) {
    qa_q1am_actor *sight = game->sight_entity;
    bool has_sight_enemy = sight && sight->has_monster && sight->monster.has_enemy;
    qa_actor_id sight_enemy = has_sight_enemy ? sight->monster.enemy : (qa_actor_id){0};
    if (qa_q1am_same_opt(has_sight_enemy, sight_enemy, m->state->has_enemy, m->state->enemy)) return false;
    has_candidate = sight != NULL;
    if (has_candidate) candidate = sight->actor.id;
  } else {
    has_candidate = game->check_client(game->ctx, entity->actor, &candidate);
  }
  if (!has_candidate || qa_q1am_same_opt(true, candidate, m->state->has_enemy, m->state->enemy)) return false;
  qa_q1am_actor *actor = game->entity(game->ctx, true, candidate);
  qa_q1am_player *player = game->player(game->ctx, candidate);
  if (((actor ? FLAGS(actor->movement_flags) : 0) & 128) != 0 ||
      (player ? qa_q1am_player_powerup(player, "invisibility") : 0) > game->time)
    return false;
  double range = qa_q1am_mg3_range_category(m, true, candidate);
  if (range == 3 || !qa_q1am_monster_visible(m, true, candidate)) return false;
  double hostile = player ? player->hostile_until : actor ? qa_q1am_actor_number(actor, "show_hostile") : 0;
  if ((range == 1 && hostile < game->time && !qa_q1am_mg3_in_front(m, candidate)) ||
      (range == 2 && !qa_q1am_mg3_in_front(m, candidate)))
    return false;
  m->state->has_enemy = true;
  m->state->enemy = candidate;
  if (!game->is_player(game->ctx, true, candidate)) {
    bool has = actor && actor->has_monster && actor->monster.has_enemy;
    if (has) m->state->enemy = actor->monster.enemy;
    m->state->has_enemy = has;
    if (!has || !game->is_player(game->ctx, true, m->state->enemy)) {
      m->state->has_enemy = false;
      return false;
    }
  }
  m->ops.found(m, m->state->enemy);
  return true;
}
bool qa_q1am_mg3_check_contents_damage(qa_q1am_monster *m) {
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  if (entity->water_level == 0 || game->health(game->ctx, entity->actor.id) <= 0 ||
      game->time < qa_q1am_actor_number(entity, "dmgtime") || (FLAGS(entity->spawnflags) & 16384) != 0)
    return false;
  if (entity->water_type != -4 && entity->water_type != -5) return false;
  if (game->world == NULL) qa_range_error("MG3 contents damage requires the world actor");
  bool lava = entity->water_type == -5;
  qa_q1am_actor_set_field_number(entity, "dmgtime", game->time + (lava ? 0.2 : 1));
  game->damage(game->ctx, entity->actor.id, game->world->actor.id, true, game->world->actor.id,
               lava ? (strcmp(entity->classname, "monster_zombie") == 0 ? 120 : 30 * entity->water_level)
                    : 4 * entity->water_level);
  return (lava && strcmp(entity->classname, "monster_zombie") == 0) ||
         game->health(game->ctx, entity->actor.id) <= 0;
}
static void mg3_ai(qa_q1am_monster *m, qa_q1am_ai mode, double distance) {
  qa_q1am_game *game = m->game;
  switch (mode) {
    case QA_Q1AM_AI_STAND:
      if (qa_q1am_mg3_check_contents_damage(m) || m->ops.find_target(m)) return;
      if (game->time > m->state->pause_until) m->ops.play(m, m->spec.walk);
      return;
    case QA_Q1AM_AI_WALK: {
      if (qa_q1am_mg3_check_contents_damage(m)) return;
      qa_q1am_mg3_source_word(m, "movedist", distance);
      if (m->ops.find_target(m)) return;
      size_t count = 0;
      qa_q1am_actor **found = game->find(game->ctx, m->state->path, &count);
      qa_q1am_actor *goal = count > 0 ? found[0] : game->world;
      if (goal) game->move_to_goal(game->ctx, m->entity->actor, goal->actor.id, distance);
      free(found);
      return;
    }
    case QA_Q1AM_AI_TURN:
      if (!m->ops.find_target(m)) qa_q1am_monster_change_yaw(m);
      return;
    case QA_Q1AM_AI_PAINFORWARD:
      game->walk_move(game->ctx, m->entity->actor, m->entity->ideal_yaw, distance);
      return;
    default:
      m->mg3_super.ai(m, mode, distance);
      return;
  }
}
static void mg3_update_run_knowledge(qa_q1am_monster *m) {
  qa_q1am_mg3_source_word(m, "enemy_range", qa_q1am_mg3_range_category(m, false, (qa_actor_id){0}));
  qa_vec3 target;
  if (qa_q1am_monster_target(m, &target))
    qa_q1am_mg3_source_word(m, "enemy_yaw", qa_q1am_yaw_for(qa_sub3(target, qa_q1am_monster_origin(m))));
}
static void mg3_run(qa_q1am_monster *m, double distance) {
  if (qa_q1am_mg3_check_contents_damage(m)) return;
  qa_q1am_game *game = m->game;
  qa_q1am_actor *entity = m->entity;
  qa_q1am_mg3_source_word(m, "movedist", distance);
  if (!m->state->has_enemy || game->health(game->ctx, m->state->enemy) <= 0) {
    m->state->has_enemy = false;
    if (m->state->has_old_enemy && game->health(game->ctx, m->state->old_enemy) > 0) {
      m->state->has_enemy = true;
      m->state->enemy = m->state->old_enemy;
      qa_q1am_mg3_hunt_target(m);
    } else {
      m->ops.play(m, m->state->path[0] == '\0' ? m->spec.stand : m->spec.walk);
      return;
    }
  }
  qa_q1am_actor_set_field_number(entity, "show_hostile", game->time + 1);
  bool seen = qa_q1am_monster_visible(m, false, (qa_actor_id){0});
  qa_q1am_mg3_source_word(m, "enemy_visible", seen ? 1 : 0);
  if (seen) m->state->search_until = FR(game->time + 5);
  if (qa_q1am_monster_search_for_coop_target(m)) return;
  m->ops.update_run_knowledge(m);
  if (strcmp(entity->attack_state, "missile") == 0 || strcmp(entity->attack_state, "melee") == 0) {
    entity->ideal_yaw = game->world ? qa_q1am_actor_number(game->world, "enemy_yaw") : 0;
    qa_q1am_monster_change_yaw(m);
    double delta = fmod(fmod(game->body(game->ctx, entity).angles.y - entity->ideal_yaw, 360) + 360, 360);
    if (delta <= 45 || delta >= 315) {
      if (strcmp(entity->attack_state, "melee") == 0) m->ops.melee_attack(m);
      else if (m->spec.missile) m->ops.play(m, m->spec.missile);
      copy_str(entity->attack_state, sizeof entity->attack_state, "straight");
    }
    return;
  }
  if (seen && m->ops.try_attack(m)) return;
  if (m->sliding) {
    entity->ideal_yaw = game->world ? qa_q1am_actor_number(game->world, "enemy_yaw") : 0;
    qa_q1am_monster_change_yaw(m);
    double offset = m->lefty ? 90 : -90;
    if (game->walk_move(game->ctx, entity->actor, entity->ideal_yaw + offset, distance)) return;
    m->lefty = !m->lefty;
    if (strcmp(entity->classname, "monster_orb") == 0) {
      m->sliding = false;
      copy_str(entity->attack_state, sizeof entity->attack_state, "straight");
      copy_str(m->next_frame, sizeof m->next_frame, m->spec.run);
      return;
    }
    game->walk_move(game->ctx, entity->actor, entity->ideal_yaw - offset, distance);
    return;
  }
  if ((game->world ? qa_q1am_actor_number(game->world, "isHordeMode") : 0) != 0)
    qa_q1am_mg3_path_to_goal(m, distance);
  else
    qa_q1am_monster_move_to_enemy(m, distance);
}
void qa_q1am_mg3_path_to_goal(qa_q1am_monster *m, double distance) {
  qa_vec3 target;
  if (qa_q1am_actor_number(m->entity, "allowPathFind") == 0 || !qa_q1am_monster_target(m, &target)) {
    qa_q1am_monster_move_to_enemy(m, distance);
    return;
  }
  qa_q1am_game *game = m->game;
  bool seen = (game->world ? qa_q1am_actor_number(game->world, "enemy_visible") : 0) != 0;
  double range = game->world ? qa_q1am_actor_number(game->world, "enemy_range") : 0;
  double style = qa_q1am_actor_number(m->entity, "combat_style");
  if (!seen || (style == 2 && range > 1) || (style == 3 && range > 2)) {
    if (qa_q1am_walk_mg3_path_to_goal(m, distance, target) == QA_Q1AM_PATH_IN_PROGRESS) return;
  }
  qa_q1am_monster_move_to_enemy(m, distance);
}
void qa_q1am_mg3_install(qa_q1am_monster *m) {
  m->mg3_super = m->ops;
  m->ops.make_vectors = mg3_make_vectors;
  m->ops.attack_finished = mg3_attack_finished;
  m->ops.sight_sound = mg3_sight_sound;
  m->ops.found = mg3_found;
  m->ops.find_target = mg3_find_target;
  m->ops.check_attack = mg3_check_attack;
  m->ops.ai = mg3_ai;
  m->ops.run_ai = mg3_run;
  m->ops.update_run_knowledge = mg3_update_run_knowledge;
  if (m->kind == QA_Q1AM_KIND_BASE) m->kind = QA_Q1AM_KIND_MG3;
}
