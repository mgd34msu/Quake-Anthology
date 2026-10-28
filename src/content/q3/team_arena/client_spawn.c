/* Port of src/content/q3/team-arena/client-spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/client_spawn.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/ground.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/content/q3/base/game/save_module_values.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

enum { CONTENTS_BODY = 0x2000000, EF_DEAD = 1, EF_TELEPORT_BIT = 4, EF_KAMIKAZE = 0x200,
  EF_VOTED = 0x4000, EF_TEAMVOTED = 0x80000 };
#define CONTENTS_CORPSE 0x4000000
#define CONTENTS_NODROP_BITS (-2147483647 - 1) /* 0x80000000 as int32 bits */
static double s32(double v) { return (int32_t)v; }
static double imul(double a, double b) { return (int32_t)((int32_t)a * (int32_t)b); }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }
static void slot_set(qa_q3_game_player_slots_mirror *s, double i, double v) { qa_q3_game_slots_set_mirror(s, (size_t)i, v); }
static qa_vec3 player_mins(void) { return qa_math_vec3(-15, -15, -24); }
static qa_vec3 player_maxs(void) { return qa_math_vec3(15, 15, 32); }

void qa_q3_ta_spawn_deathmatch_point(qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  if (qa_q3_spawn_variables_int(variables, "nobots", "0").value)
    entity->flags = (double)((int32_t)entity->flags | QA_Q3_GAME_FLAG_NO_BOTS);
  if (qa_q3_spawn_variables_int(variables, "nohumans", "0").value)
    entity->flags = (double)((int32_t)entity->flags | QA_Q3_GAME_FLAG_NO_HUMANS);
}
void qa_q3_ta_spawn_player_start(qa_q3_game_entity *entity, const qa_q3_spawn_variables *variables) {
  qa_q3_game_entity_set_classname(entity, "info_player_deathmatch");
  qa_q3_ta_spawn_deathmatch_point(entity, variables);
}
static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Client spawning requires a client entity");
  return entity->client;
}
void qa_q3_ta_set_client_view_angle(qa_q3_game_entity *entity, qa_vec3 angles) {
  qa_q3_game_client *client = client_of(entity);
  double cmd[3] = {client->pers.cmd.angles.x, client->pers.cmd.angles.y, client->pers.cmd.angles.z};
  double src[3] = {angles.x, angles.y, angles.z};
  for (int i = 0; i < 3; i++) {
    double scaled = (float)((double)(float)(src[i] * 65536) / 360);
    double delta = s32((double)(qa_qvm_float_to_int(scaled) & 65535) - cmd[i]);
    slot_set(&client->ps.delta_angles, i, delta);
  }
  entity->s.angles = qa_math_vec3(angles.x, angles.y, angles.z);
  client->ps.viewangles = entity->s.angles;
}
static qa_q3_ta_spawn_point pose(qa_q3_game_entity *entity) {
  qa_q3_ta_spawn_point out = {qa_add3(entity->s.origin, qa_math_vec3(0, 0, 9)), entity->s.angles, entity};
  return out;
}
void qa_q3_ta_client_spawn_state_init(qa_q3_ta_client_spawn_state *s) { memset(s, 0, sizeof *s); }

void qa_q3_ta_client_spawn_runtime_init(qa_q3_ta_client_spawn_runtime *r,
    qa_q3_ta_client_spawn_host host, qa_q3_ta_client_spawn_state *state) {
  if (host.think_pool != host.pool || host.think_world != host.world)
    qa_range_error("Client spawning and thinking must share entity storage and world");
  r->host = host;
  qa_q3_ta_client_spawn_state_init(&r->owned_state);
  r->state = state ? state : &r->owned_state;
  qa_q3_ta_client_spawn_runtime_bind_save_callbacks(r);
}
qa_q3_ta_client_spawn_save_state qa_q3_ta_client_spawn_runtime_capture_save_state(qa_q3_ta_client_spawn_runtime *r) {
  qa_q3_ta_client_spawn_save_state out;
  memset(&out, 0, sizeof out);
  out.has_body_queue = r->state->has_body_queue;
  if (out.has_body_queue) {
    for (int i = 0; i < QA_Q3_TA_BODY_QUEUE_SIZE; i++) out.entity_slots[i] = r->state->body_queue_entities[i]->slot;
    out.index = r->state->body_queue_index;
  }
  return out;
}
void qa_q3_ta_client_spawn_runtime_restore_save_state(qa_q3_ta_client_spawn_runtime *r, const qa_save_value *value) {
  qa_save_reader reader, queue, entities, index_reader;
  if (!qa_save_reader_init(&reader, value, "q3.clientSpawn")) qa_range_error("out of memory");
  if (!qa_save_reader_field(&reader, "bodyQueue", &queue)) qa_range_error("out of memory");
  qa_q3_ta_client_spawn_state restored;
  qa_q3_ta_client_spawn_state_init(&restored);
  if (!qa_save_reader_is_null(&queue)) {
    if (!qa_save_reader_field(&queue, "entities", &entities)) qa_range_error("out of memory");
    size_t count = qa_save_reader_list_count(&entities);
    qa_q3_game_entity *list[QA_Q3_TA_BODY_QUEUE_SIZE];
    size_t stored = 0;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&entities, i, &item)) qa_range_error("out of memory");
      qa_q3_game_entity *entity = qa_q3_read_module_entity(&item, r->host.pool);
      qa_save_reader_destroy(&item);
      if (stored < QA_Q3_TA_BODY_QUEUE_SIZE) list[stored++] = entity;
    }
    qa_save_reader_destroy(&entities);
    if (!qa_save_reader_field(&queue, "index", &index_reader)) qa_range_error("out of memory");
    double index = qa_save_reader_integer_min(&index_reader, 0);
    qa_save_reader_destroy(&index_reader);
    bool distinct = true;
    for (size_t i = 0; i < stored && distinct; i++)
      for (size_t j = i + 1; j < stored; j++) if (list[i] == list[j]) distinct = false;
    if (count != QA_Q3_TA_BODY_QUEUE_SIZE || index >= (double)count || !distinct)
      qa_save_reader_fail(&reader, "invalid body queue");
    restored.has_body_queue = true;
    for (int i = 0; i < QA_Q3_TA_BODY_QUEUE_SIZE; i++) restored.body_queue_entities[i] = list[i];
    restored.body_queue_index = index;
  }
  qa_save_reader_destroy(&queue);
  qa_save_reader_destroy(&reader);
  *r->state = restored;
}
static qa_q3_game_client *owned_client(qa_q3_ta_client_spawn_runtime *r, qa_q3_game_entity *entity) {
  if (qa_q3_entity_pool_get(r->host.pool, entity->slot) != entity || entity->slot >= r->host.pool->max_clients ||
      entity->client != qa_q3_entity_pool_client_at(r->host.pool, entity->slot))
    qa_range_error("Client does not belong to the configured pool slot");
  return client_of(entity);
}
bool qa_q3_ta_client_spawn_runtime_spot_would_telefrag(qa_q3_ta_client_spawn_runtime *r, qa_q3_game_entity *spot) {
  qa_bounds bounds = {qa_add3(spot->s.origin, player_mins()), qa_add3(spot->s.origin, player_maxs())};
  size_t count = 0;
  const qa_actor_id *actors = r->host.spatial->area_actors(r->host.spatial->ctx, bounds, QA_Q3_MAX_GENTITIES, &count);
  for (size_t i = 0; i < count; i++) if (r->host.is_player(r->host.ctx, actors[i])) return true;
  return false;
}
qa_q3_game_entity *qa_q3_ta_client_spawn_runtime_select_nearest(qa_q3_ta_client_spawn_runtime *r, qa_vec3 from) {
  qa_q3_game_entity *spot = NULL, *nearest = NULL;
  double nearest_distance = 999999;
  while ((spot = qa_q3_find_entity(r->host.pool, spot, QA_Q3_FIELD_CLASSNAME, "info_player_deathmatch")) != NULL) {
    double distance = qa_length3(qa_sub3(spot->s.origin, from));
    if (distance < nearest_distance) { nearest_distance = distance; nearest = spot; }
  }
  return nearest;
}
qa_q3_game_entity *qa_q3_ta_client_spawn_runtime_select_random(qa_q3_ta_client_spawn_runtime *r) {
  qa_q3_game_entity *points[128];
  size_t count = 0;
  qa_q3_game_entity *spot = NULL;
  while ((spot = qa_q3_find_entity(r->host.pool, spot, QA_Q3_FIELD_CLASSNAME, "info_player_deathmatch")) != NULL) {
    if (qa_q3_ta_client_spawn_runtime_spot_would_telefrag(r, spot)) continue;
    if (count == 128) qa_range_error("SelectRandomDeathmatchSpawnPoint exceeds source 128-entry storage");
    points[count++] = spot;
  }
  if (count == 0) return qa_q3_find_entity(r->host.pool, NULL, QA_Q3_FIELD_CLASSNAME, "info_player_deathmatch");
  int32_t chosen = qa_game_random_rand(r->host.random) % (int32_t)count;
  if (chosen < 0) qa_range_error("Random spawn index invariant");
  return points[chosen];
}
qa_q3_ta_spawn_point qa_q3_ta_client_spawn_runtime_select_spawn_point(qa_q3_ta_client_spawn_runtime *r, qa_vec3 avoid) {
  struct { qa_q3_game_entity *entity; double distance; } points[64];
  size_t count = 0;
  qa_q3_game_entity *spot = NULL;
  while ((spot = qa_q3_find_entity(r->host.pool, spot, QA_Q3_FIELD_CLASSNAME, "info_player_deathmatch")) != NULL) {
    if (qa_q3_ta_client_spawn_runtime_spot_would_telefrag(r, spot)) continue;
    double distance = qa_length3(qa_sub3(spot->s.origin, avoid));
    size_t insertion = count;
    for (size_t i = 0; i < count; i++) if (distance > points[i].distance) { insertion = i; break; }
    if (insertion < count) {
      size_t new_count = count < 64 ? count + 1 : 64;
      for (size_t i = new_count - 1; i > insertion; i--) points[i] = points[i - 1];
      points[insertion].entity = spot; points[insertion].distance = distance;
      count = new_count;
    } else if (count < 64) {
      points[count].entity = spot; points[count].distance = distance; count++;
    }
  }
  if (count == 0) {
    qa_q3_game_entity *fallback = qa_q3_find_entity(r->host.pool, NULL, QA_Q3_FIELD_CLASSNAME, "info_player_deathmatch");
    if (fallback == NULL) qa_range_error("Couldn't find a spawn point");
    return pose(fallback);
  }
  double index = trunc((float)(qa_game_random_random(r->host.random) * trunc((double)count / 2)));
  if (index < 0 || index >= (double)count) qa_range_error("Farthest spawn index invariant");
  return pose(points[(size_t)index].entity);
}
qa_q3_ta_spawn_point qa_q3_ta_client_spawn_runtime_select_initial(qa_q3_ta_client_spawn_runtime *r) {
  qa_q3_game_entity *spot = NULL;
  while ((spot = qa_q3_find_entity(r->host.pool, spot, QA_Q3_FIELD_CLASSNAME, "info_player_deathmatch")) != NULL) {
    if (((int32_t)spot->spawnflags & 1) != 0) break;
  }
  return (spot == NULL || qa_q3_ta_client_spawn_runtime_spot_would_telefrag(r, spot))
      ? qa_q3_ta_client_spawn_runtime_select_spawn_point(r, qa_math_vec3(0, 0, 0)) : pose(spot);
}
qa_q3_ta_spawn_point qa_q3_ta_client_spawn_runtime_select_team(qa_q3_ta_client_spawn_runtime *r,
    qa_q3_base_team team, qa_q3_team_state state) {
  if (team != QA_Q3_BASE_TEAM_RED && team != QA_Q3_BASE_TEAM_BLUE)
    return qa_q3_ta_client_spawn_runtime_select_spawn_point(r, qa_math_vec3(0, 0, 0));
  char name[32];
  (void)snprintf(name, sizeof name, "team_CTF_%s%s", team == QA_Q3_BASE_TEAM_RED ? "red" : "blue",
      state == QA_Q3_TEAM_STATE_BEGIN ? "player" : "spawn");
  qa_q3_game_entity *points[32];
  size_t count = 0;
  qa_q3_game_entity *spot = NULL;
  while ((spot = qa_q3_find_entity(r->host.pool, spot, QA_Q3_FIELD_CLASSNAME, name)) != NULL) {
    if (qa_q3_ta_client_spawn_runtime_spot_would_telefrag(r, spot)) continue;
    points[count++] = spot;
    if (count == 32) break;
  }
  if (count == 0) {
    qa_q3_game_entity *fallback = qa_q3_find_entity(r->host.pool, NULL, QA_Q3_FIELD_CLASSNAME, name);
    return fallback == NULL ? qa_q3_ta_client_spawn_runtime_select_spawn_point(r, qa_math_vec3(0, 0, 0)) : pose(fallback);
  }
  int32_t chosen = qa_game_random_rand(r->host.random) % (int32_t)count;
  if (chosen < 0) qa_range_error("Team spawn index invariant");
  return pose(points[chosen]);
}
void qa_q3_ta_client_spawn_runtime_init_body_queue(qa_q3_ta_client_spawn_runtime *r) {
  if (r->state->has_body_queue) qa_range_error("Body queue is already initialized for this map");
  for (int i = 0; i < QA_Q3_TA_BODY_QUEUE_SIZE; i++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_spawn(r->host.pool);
    qa_q3_game_entity_set_classname(entity, "bodyque");
    entity->never_free = true;
    r->state->body_queue_entities[i] = entity;
  }
  r->state->has_body_queue = true;
  r->state->body_queue_index = 0;
}
void qa_q3_ta_client_spawn_runtime_body_sink(qa_q3_ta_client_spawn_runtime *r, qa_q3_game_entity *body) {
  double time = r->host.frame(r->host.ctx).time;
  if (s32(time - body->timestamp) > 6500) {
    r->host.world->unlink(r->host.world->ctx, body->slot);
    body->physics_object = false;
    return;
  }
  qa_q3_game_entity_set_nextthink(body, s32(time + 100));
  body->s.pos.base = qa_add3(body->s.pos.base, qa_math_vec3(0, 0, -1));
}

qa_q3_game_entity *qa_q3_ta_client_spawn_runtime_copy_to_body_queue(qa_q3_ta_client_spawn_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = owned_client(r, entity);
  double time = r->host.frame(r->host.ctx).time;
  r->host.world->unlink(r->host.world->ctx, entity->slot);
  if (((int32_t)r->host.world->point_contents(r->host.world->ctx, entity->s.origin, -1) & CONTENTS_NODROP_BITS) != 0) return NULL;
  if (!r->state->has_body_queue) qa_range_error("Body queue must be initialized before copying a corpse");
  qa_q3_game_entity *body = (r->state->body_queue_index >= 0 && r->state->body_queue_index < QA_Q3_TA_BODY_QUEUE_SIZE)
      ? r->state->body_queue_entities[(size_t)r->state->body_queue_index] : NULL;
  if (body == NULL) qa_range_error("Body queue index invariant");
  r->state->body_queue_index = (double)(((int32_t)r->state->body_queue_index + 1) % QA_Q3_TA_BODY_QUEUE_SIZE);
  r->host.world->unlink(r->host.world->ctx, body->slot);
  body->s = entity->s;
  qa_q3_write_ground(body, entity->binding.body.has_ground ? &entity->binding.body.ground : NULL, r->host.pool);
  body->s.e_flags = EF_DEAD;
  if (client->ps.product == QA_Q3_GAME_PRODUCT_MISSIONPACK && ((int32_t)entity->s.e_flags & EF_KAMIKAZE) != 0) {
    body->s.e_flags = (double)((int32_t)body->s.e_flags | EF_KAMIKAZE);
    for (int index = 0; index < QA_Q3_MAX_GENTITIES; index++) {
      qa_q3_game_entity *timer = qa_q3_entity_pool_at(r->host.pool, index);
      const char *cn = qa_q3_game_entity_classname(timer);
      if (qa_q3_game_entity_inuse(timer) && timer->activator == entity && cn && strcmp(cn, "kamikaze timer") == 0) {
        timer->activator = body; break; }
    }
  }
  body->s.powerups = 0;
  body->s.loop_sound = 0;
  body->s.number = body->slot;
  body->timestamp = time;
  body->physics_object = true;
  body->physics_bounce = 0;
  if (!body->binding.body.has_ground) {
    body->s.pos.type = QA_Q3_TR_GRAVITY;
    body->s.pos.time = time;
    body->s.pos.delta = client->ps.velocity;
  } else body->s.pos.type = QA_Q3_TR_STATIONARY;
  body->s.event = 0;
  /* TS legsAnim/torsoAnim copy onto s: no mirror entity-state fields. */
  body->r.sv_flags = entity->r.sv_flags;
  body->r.mins = entity->r.mins;
  body->r.maxs = entity->r.maxs;
  body->clipmask = 1 | 0x10000;
  body->r.contents = CONTENTS_CORPSE;
  body->r.owner_num = entity->s.number;
  qa_q3_game_entity_set_nextthink(body, s32(time + 5000));
  body->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.team-arena.client-spawn.copyToBodyQueue.think");
  body->die = qa_q3_callback_family_resolve(&r->host.pool->callbacks.die, "q3.death.body");
  qa_q3_game_entity_set_takedamage(body, qa_q3_game_entity_health(entity) > QA_Q3_BASE_GIB_HEALTH);
  body->r.current_origin = body->s.pos.base;
  r->host.world->link(r->host.world->ctx, body);
  return body;
}

static void slots_reset(qa_q3_game_player_slots_mirror *s, size_t length) {
  memset(s->values, 0, sizeof s->values); s->length = length;
}
/* TS fresh-GameClient assign in clientSpawn, preserving pers/sess/ps
 * identity plus accuracy and (by caller) persistant/eventSequence. */
static void fresh_client(qa_q3_game_client *client) {
  qa_q3_client_persistant pers = client->pers;
  qa_q3_client_session sess = client->sess;
  qa_q3_game_product product = client->ps.product;
  double hits = client->accuracy_hits, shots = client->accuracy_shots;
  double persistant[QA_Q3_GAME_SLOTS_CAP];
  size_t persistant_length = client->ps.persistant.length;
  memcpy(persistant, client->ps.persistant.values, sizeof persistant);
  double event_sequence = client->ps.generic;
  free(client->areabits);
  memset(client, 0, sizeof *client);
  client->pers = pers;
  client->sess = sess;
  client->accuracy_hits = hits;
  client->accuracy_shots = shots;
  client->ps.product = product;
  slots_reset(&client->ps.stats, 16); slots_reset(&client->ps.persistant, 16);
  slots_reset(&client->ps.powerups, 16); slots_reset(&client->ps.ammo, 16);
  slots_reset(&client->ps.events, 2); slots_reset(&client->ps.event_parms, 2);
  slots_reset(&client->ps.delta_angles, 3);
  slots_reset(&client->ammo_times, product == QA_Q3_GAME_PRODUCT_BASEQ3 ? 11 : 14);
  /* restored below to mirror the TS statement order */
  memcpy(client->ps.persistant.values, persistant, sizeof persistant);
  client->ps.persistant.length = persistant_length;
  client->ps.generic = event_sequence;
}

void qa_q3_ta_client_spawn_runtime_client_spawn(qa_q3_ta_client_spawn_runtime *r, qa_q3_game_entity *entity) {
  qa_q3_game_client *client = owned_client(r, entity);
  qa_q3_ta_client_spawn_frame frame = r->host.frame(r->host.ctx);
  qa_q3_ta_spawn_pose spawn = {qa_math_vec3(0, 0, 0), qa_math_vec3(0, 0, 0)};
  qa_q3_game_entity *spawn_entity = NULL;
  if (client->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) {
    spawn = r->host.find_intermission_point(r->host.ctx);
  } else if (frame.game_type >= QA_Q3_BASE_GT_CTF) {
    qa_q3_ta_spawn_point selected = qa_q3_ta_client_spawn_runtime_select_team(r,
        (qa_q3_base_team)client->sess.session_team, (qa_q3_team_state)client->pers.team_state.state);
    spawn.origin = selected.origin; spawn.angles = selected.angles; spawn_entity = selected.entity;
  } else {
    bool seen[65536];
    memset(seen, 0, sizeof seen);
    for (;;) {
      int32_t state = (r->host.random->seed & 0x7fff) | (client->pers.initial_spawn ? 0x8000 : 0);
      if (seen[state]) qa_range_error("No permitted spawn point in the complete game RNG cycle");
      seen[state] = true;
      qa_q3_ta_spawn_point selected;
      if (!client->pers.initial_spawn && client->pers.local_client) {
        client->pers.initial_spawn = true;
        selected = qa_q3_ta_client_spawn_runtime_select_initial(r);
      } else selected = qa_q3_ta_client_spawn_runtime_select_spawn_point(r, client->ps.origin);
      bool bot = ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) != 0;
      if (((int32_t)selected.entity->flags & (bot ? QA_Q3_GAME_FLAG_NO_BOTS : QA_Q3_GAME_FLAG_NO_HUMANS)) != 0) continue;
      spawn.origin = selected.origin; spawn.angles = selected.angles; spawn_entity = selected.entity;
      break;
    }
  }
  client->pers.team_state.state = QA_Q3_TEAM_STATE_ACTIVE;
  entity->s.e_flags = (double)((int32_t)entity->s.e_flags & ~EF_KAMIKAZE);
  double flags = (double)(((int32_t)client->ps.e_flags & (EF_TELEPORT_BIT | EF_VOTED | EF_TEAMVOTED)) ^ EF_TELEPORT_BIT);
  fresh_client(client);
  client->last_killed_client = -1;
  qa_q3_game_player_state_mirror *ps = &client->ps;
  slot_set(&ps->persistant, QA_Q3_BASE_PERS_SPAWN_COUNT,
      s32(slot_get(&ps->persistant, QA_Q3_BASE_PERS_SPAWN_COUNT) + 1));
  slot_set(&ps->persistant, QA_Q3_BASE_PERS_TEAM, client->sess.session_team);
  client->air_out_time = s32(frame.time + 12000);
  client->pers.max_health = qa_game_atoi(r->host.handicap(r->host.ctx, entity->slot));
  if (client->pers.max_health < 1 || client->pers.max_health > 100) client->pers.max_health = 100;
  const qa_q3_base_stat_schema *schema = qa_q3_base_stat_schema_for(
      ps->product == QA_Q3_GAME_PRODUCT_MISSIONPACK ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3);
  slot_set(&ps->stats, schema->max_health, client->pers.max_health);
  ps->e_flags = flags;
  qa_q3_write_ground(entity, NULL, r->host.pool);
  qa_q3_game_entity_set_takedamage(entity, true);
  (void)qa_q3_entity_pool_activate_client(r->host.pool, entity->slot);
  qa_q3_game_entity_set_classname(entity, "player");
  entity->r.contents = CONTENTS_BODY;
  entity->clipmask = 1 | 0x10000 | CONTENTS_BODY;
  entity->die = qa_q3_callback_family_resolve(&r->host.pool->callbacks.die, "q3.death.player");
  entity->waterlevel = 0;
  entity->watertype = 0;
  entity->flags = 0;
  bool has_selected = r->host.selected_player != NULL;
  if (!has_selected) { entity->r.mins = player_mins(); entity->r.maxs = player_maxs(); }
  ps->client_num = entity->slot;
  slot_set(&ps->ammo, QA_Q3_WP_GAUNTLET, -1);
  slot_set(&ps->ammo, QA_Q3_WP_GRAPPLING_HOOK, -1);
  if (!has_selected) {
    slot_set(&ps->stats, schema->weapons, (1 << QA_Q3_WP_MACHINEGUN) | (1 << QA_Q3_WP_GAUNTLET));
    slot_set(&ps->ammo, QA_Q3_WP_MACHINEGUN, frame.game_type == QA_Q3_BASE_GT_TEAM ? 50 : 100);
    double health = s32(client->pers.max_health + 25);
    qa_q3_game_entity_set_health(entity, health);
    ps->health = health;
  } else r->host.selected_player(r->host.ctx, entity, spawn);
  qa_q3_set_origin(entity, spawn.origin);
  ps->origin = spawn.origin;
  ps->pm_flags = (double)((int32_t)ps->pm_flags | QA_Q3_FLAG_RESPAWNED);
  qa_q3_base_user_command command = r->host.user_command(r->host.ctx, entity->slot);
  client->pers.cmd.server_time = command.server_time;
  client->pers.cmd.angles = command.angles;
  client->pers.cmd.buttons = command.buttons;
  client->pers.cmd.weapon = command.weapon;
  client->pers.cmd.forwardmove = command.forwardmove;
  client->pers.cmd.rightmove = command.rightmove;
  client->pers.cmd.upmove = command.upmove;
  qa_q3_ta_set_client_view_angle(entity, spawn.angles);
  if (client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR) {
    r->host.kill_box(r->host.ctx, entity);
    r->host.world->link(r->host.world->ctx, entity);
    if (!has_selected) entity->s.weapon = QA_Q3_WP_MACHINEGUN; /* TS ps.weapon */
    ps->weapon_state = QA_Q3_WEAPON_READY;
  }
  ps->pm_flags = (double)((int32_t)ps->pm_flags | QA_Q3_FLAG_TIME_KNOCKBACK);
  ps->pm_time = 100;
  client->respawn_time = frame.time;
  client->inactivity_time = s32(frame.time + imul(frame.inactivity_seconds, 1000));
  client->latched_buttons = 0;
  ps->torso_anim = QA_Q3_ANIM_TORSO_STAND;
  ps->legs_anim = QA_Q3_ANIM_LEGS_IDLE;
  if (frame.intermission_time != 0) r->host.move_to_intermission(r->host.ctx, entity);
  else {
    if (spawn_entity != NULL) {
      qa_q3_target_use_context targets = r->host.targets(r->host.ctx);
      qa_q3_use_participant activator = qa_q3_participant_entity(entity);
      qa_q3_use_targets(targets, spawn_entity, &activator);
    }
    if (!has_selected) {
      double weapon = QA_Q3_WP_GAUNTLET;
      double count = qa_q3_base_weapon_count(schema->product);
      for (double w = count - 1; w > 0; w--) {
        if (((int32_t)slot_get(&ps->stats, schema->weapons) & (1 << (int)w)) != 0) { weapon = w; break; }
      }
      entity->s.weapon = weapon; /* TS ps.weapon */
    }
  }
  /* TS ps.commandTime = frame.time - 100: no mirror field. */
  client->pers.cmd.server_time = frame.time;
  r->host.client_think(r->host.ctx, entity->slot, r->host.user_command(r->host.ctx, entity->slot));
  if (client->sess.session_team != QA_Q3_BASE_TEAM_SPECTATOR) {
    r->host.snapshot(r->host.ctx, client, entity);
    entity->r.current_origin = client->ps.origin;
    r->host.world->link(r->host.world->ctx, entity);
  }
  qa_q3_ta_client_effects_context effects = r->host.effects(r->host.ctx);
  qa_q3_ta_client_end_frame(&effects, entity);
  r->host.snapshot(r->host.ctx, client, entity);
}

void qa_q3_ta_client_spawn_runtime_respawn(qa_q3_ta_client_spawn_runtime *r, qa_q3_game_entity *entity) {
  (void)qa_q3_ta_client_spawn_runtime_copy_to_body_queue(r, entity);
  qa_q3_ta_client_spawn_runtime_client_spawn(r, entity);
  qa_q3_game_entity *temporary = qa_q3_entity_pool_temp_entity(r->host.pool, client_of(entity)->ps.origin, QA_Q3_EV_PLAYER_TELEPORT_IN);
  temporary->s.client_num = entity->s.client_num;
}

static void body_sink_think(void *ctx, qa_q3_game_entity *self) {
  qa_q3_ta_client_spawn_runtime_body_sink((qa_q3_ta_client_spawn_runtime *)ctx, self);
}
void qa_q3_ta_client_spawn_runtime_bind_save_callbacks(qa_q3_ta_client_spawn_runtime *r) {
  qa_q3_entity_callback think = {NULL, r};
  qa_q3_entity_think_fn fn = body_sink_think;
  memcpy(&think.fn, &fn, sizeof fn < sizeof think.fn ? sizeof fn : sizeof think.fn);
  (void)qa_q3_callback_family_intern(&r->host.pool->callbacks.think,
      "q3.team-arena.client-spawn.copyToBodyQueue.think", think);
  (void)qa_q3_callback_family_intern(&r->host.pool->callbacks.die, "q3.death.body", r->host.body_die);
  (void)qa_q3_callback_family_intern(&r->host.pool->callbacks.die, "q3.death.player", r->host.player_die);
}
