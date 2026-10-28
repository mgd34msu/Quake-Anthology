/* Port of src/content/q3/team-arena/arenas.ts. GPL-2.0-or-later. */
#include "qa/content/q3/team_arena/arenas.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/game/ground.h"
#include "qa/content/q3/base/game/save_callbacks.h"
#include "qa/content/q3/base/game/save_module_values.h"
#include "qa/content/q3/base/shared/entity_shared.h"
#include "qa/content/q3/base/shared/trajectory.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"

enum { CONTENTS_SOLID = 1, CONTENTS_PLAYERCLIP = 0x10000, CONTENTS_BODY = 0x2000000,
  RANK_TIED_FLAG = 0x4000, MAX_STRING_CHARS = 1024, TIMER_GESTURE = 34 * 66 + 50 };
static double s32(double v) { return (int32_t)v; }
static double imul(double a, double b) { return (int32_t)((int32_t)a * (int32_t)b); }
static double slot_get(qa_q3_game_player_slots_mirror *s, double i) { return qa_q3_game_slots_get_mirror(s, (size_t)i); }
static qa_q3_ta_match_state *level(qa_q3_ta_arena_runtime *r) { return r->host.match->host.state; }
static qa_q3_entity_pool *pool_of(qa_q3_ta_arena_runtime *r) { return r->host.match->host.pool; }
static double sorted_at(qa_q3_ta_arena_runtime *r, double index) {
  if (index != trunc(index) || index < 0 || index >= QA_Q3_MAX_CLIENTS)
    qa_range_error("Arena rank index has no source storage");
  return level(r)->sorted_clients[(size_t)index];
}
static qa_q3_game_client *client_of(qa_q3_game_entity *entity) {
  if (entity->client == NULL) qa_range_error("Arena entity has no source client pointer");
  return entity->client;
}
static double score_of(qa_q3_ta_arena_runtime *r, double number) {
  return slot_get(&qa_q3_entity_pool_client_at(pool_of(r), number)->ps.persistant, QA_Q3_BASE_PERS_SCORE);
}
static double cvar_integer(qa_q3_ta_arena_runtime *r, const char *name) {
  qa_cvar_snapshot snap;
  if (!qa_cvar_registry_get(r->host.cvars, name, &snap))
    qa_range_error("Arena requires registered cvar %s", name);
  double value = snap.integer_value;
  qa_cvar_read_free(&snap);
  return value;
}
static qa_vec3 offset_first(void) { return qa_math_vec3(0, 0, 74); }
static qa_vec3 offset_second(void) { return qa_math_vec3(-10, 60, 54); }
static qa_vec3 offset_third(void) { return qa_math_vec3(-19, -60, 45); }

void qa_q3_ta_arena_runtime_init(qa_q3_ta_arena_runtime *r, qa_q3_ta_arena_host host) {
  r->host = host;
  r->podium1 = r->podium2 = r->podium3 = NULL;
  qa_q3_ta_arena_runtime_bind_save_callbacks(r);
}
qa_q3_ta_arena_save_state qa_q3_ta_arena_runtime_capture_save_state(qa_q3_ta_arena_runtime *r) {
  qa_q3_ta_arena_save_state out = {r->podium1 != NULL, r->podium2 != NULL, r->podium3 != NULL,
      r->podium1 ? r->podium1->slot : 0, r->podium2 ? r->podium2->slot : 0, r->podium3 ? r->podium3->slot : 0};
  return out;
}
static qa_q3_game_entity *restore_podium(qa_q3_ta_arena_runtime *r, qa_save_reader *reader, const char *name) {
  qa_save_reader field;
  if (!qa_save_reader_field(reader, name, &field)) qa_range_error("out of memory");
  qa_q3_game_entity *entity = qa_save_reader_is_null(&field) ? NULL : qa_q3_read_module_entity(&field, pool_of(r));
  qa_save_reader_destroy(&field);
  return entity;
}
void qa_q3_ta_arena_runtime_restore_save_state(qa_q3_ta_arena_runtime *r, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "q3.arenas")) qa_range_error("out of memory");
  qa_q3_game_entity *first = restore_podium(r, &reader, "podium1");
  qa_q3_game_entity *second = restore_podium(r, &reader, "podium2");
  qa_q3_game_entity *third = restore_podium(r, &reader, "podium3");
  qa_save_reader_destroy(&reader);
  r->podium1 = first; r->podium2 = second; r->podium3 = third;
}
void qa_q3_ta_arena_runtime_reset_podium_players(qa_q3_ta_arena_runtime *r) {
  r->podium1 = r->podium2 = r->podium3 = NULL;
}

void qa_q3_ta_arena_runtime_update_tournament_info(qa_q3_ta_arena_runtime *r) {
  qa_q3_game_entity *player = NULL;
  double player_client_num = 0;
  for (; player_client_num < pool_of(r)->max_clients; player_client_num++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool_of(r), player_client_num);
    if (qa_q3_game_entity_inuse(entity) && ((int32_t)entity->r.sv_flags & QA_Q3_BASE_SVF_BOT) == 0) {
      player = entity; break; }
  }
  if (player == NULL) return;
  qa_q3_ta_match_runtime_calculate_ranks(r->host.match);
  qa_q3_ta_match_host *match = &r->host.match->host;
  qa_q3_game_client *ranked = qa_q3_entity_pool_client_at(pool_of(r), player_client_num);
  char *message;
  if (ranked->sess.session_team == QA_Q3_BASE_TEAM_SPECTATOR) {
    qa_q3_game_format_arg a[] = {qa_q3_game_format_number(level(r)->num_non_spectator_clients),
        qa_q3_game_format_number(player_client_num)};
    message = qa_q3_game_format(match->product == QA_Q3_BASE_PRODUCT_MISSIONPACK
        ? "postgame %i %i 0 0 0 0 0 0 0 0 0 0 0" : "postgame %i %i 0 0 0 0 0 0", a, 2, MAX_STRING_CHARS);
  } else {
    qa_q3_game_client *client = client_of(player);
    qa_q3_game_player_slots_mirror *persistant = &client->ps.persistant;
    double accuracy = client->accuracy_shots != 0
        ? s32(trunc(imul(client->accuracy_hits, 100) / client->accuracy_shots)) : 0;
    if (match->product == QA_Q3_BASE_PRODUCT_MISSIONPACK) {
      bool won = false;
      double score1, score2;
      if (match->settings(match->ctx).game_type >= QA_Q3_BASE_GT_CTF) {
        score1 = slot_get(match->team_scores, QA_Q3_BASE_TEAM_RED);
        score2 = slot_get(match->team_scores, QA_Q3_BASE_TEAM_BLUE);
        won = ranked->sess.session_team == QA_Q3_BASE_TEAM_RED ? score1 > score2 : score2 > score1;
      } else if (ranked == qa_q3_entity_pool_client_at(pool_of(r), sorted_at(r, 0))) {
        won = true;
        score1 = score_of(r, sorted_at(r, 0)); score2 = score_of(r, sorted_at(r, 1));
      } else {
        score2 = score_of(r, sorted_at(r, 0)); score1 = score_of(r, sorted_at(r, 1));
      }
      double perfect = (won && slot_get(persistant, QA_Q3_BASE_PERS_KILLED) == 0) ? 1 : 0;
      qa_q3_game_format_arg a[] = {
        qa_q3_game_format_number(level(r)->num_non_spectator_clients),
        qa_q3_game_format_number(player_client_num), qa_q3_game_format_number(accuracy),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_EXCELLENT_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_DEFEND_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_ASSIST_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_SCORE)),
        qa_q3_game_format_number(perfect), qa_q3_game_format_number(score1),
        qa_q3_game_format_number(score2), qa_q3_game_format_number(level(r)->time),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_CAPTURES))};
      message = qa_q3_game_format("postgame %i %i %i %i %i %i %i %i %i %i %i %i %i %i", a, 14, MAX_STRING_CHARS);
    } else {
      double perfect = (slot_get(&ranked->ps.persistant, QA_Q3_BASE_PERS_RANK) == 0 &&
          slot_get(persistant, QA_Q3_BASE_PERS_KILLED) == 0) ? 1 : 0;
      qa_q3_game_format_arg a[] = {
        qa_q3_game_format_number(level(r)->num_non_spectator_clients),
        qa_q3_game_format_number(player_client_num), qa_q3_game_format_number(accuracy),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_IMPRESSIVE_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_EXCELLENT_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_GAUNTLET_FRAG_COUNT)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_SCORE)),
        qa_q3_game_format_number(perfect)};
      message = qa_q3_game_format("postgame %i %i %i %i %i %i %i %i", a, 8, MAX_STRING_CHARS);
    }
  }
  size_t message_length = strlen(message);
  for (double index = 0; index < level(r)->num_non_spectator_clients; index++) {
    double number = sorted_at(r, index);
    qa_q3_game_player_slots_mirror *persistant = &qa_q3_entity_pool_client_at(pool_of(r), number)->ps.persistant;
    qa_q3_game_format_arg a[] = {qa_q3_game_format_number(number),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_RANK)),
        qa_q3_game_format_number(slot_get(persistant, QA_Q3_BASE_PERS_SCORE))};
    char *row = qa_q3_game_format(" %i %i %i", a, 3, 32);
    size_t row_length = strlen(row);
    if (message_length + row_length + 1 >= MAX_STRING_CHARS) { free(row); break; }
    if (strlen(message) + row_length >= MAX_STRING_CHARS) {
      free(row); free(message);
      qa_range_error("Postgame strcat exceeds the source 1024-byte message buffer");
    }
    size_t current = strlen(message);
    char *grown = realloc(message, current + row_length + 1);
    if (!grown) { free(row); free(message); qa_range_error("out of memory"); }
    message = grown;
    memcpy(message + current, row, row_length + 1);
    free(row);
  }
  match->append_console_command(match->ctx, message);
  free(message);
}

static void place_model(qa_q3_ta_arena_runtime *r, qa_q3_game_entity *body, qa_q3_game_entity *podium, qa_vec3 offset) {
  qa_vec3 angles = qa_vector_to_angles(qa_sub3(level(r)->intermission_origin, podium->r.current_origin));
  body->s.apos.base = qa_math_vec3(0, angles.y, 0);
  qa_angle_vectors axis = qa_qvm_angle_vectors(body->s.apos.base);
  qa_vec3 origin = qa_add3(podium->r.current_origin, qa_scale3(axis.forward, offset.x));
  origin = qa_add3(origin, qa_scale3(axis.right, offset.y));
  origin = qa_add3(origin, qa_scale3(axis.up, offset.z));
  qa_q3_set_origin(body, origin);
}
static qa_q3_game_entity *spawn_model_on_pad(qa_q3_ta_arena_runtime *r, qa_q3_game_entity *pad,
    qa_vec3 offset, qa_q3_game_entity *entity, double place) {
  qa_q3_game_entity *body = qa_q3_entity_pool_spawn(pool_of(r));
  qa_q3_game_client *client = client_of(entity);
  qa_q3_game_entity_bind_client_name(body, client);
  body->client = client;
  body->s = entity->s;
  body->s.e_type = QA_Q3_BASE_ET_PLAYER;
  body->s.e_flags = body->s.powerups = body->s.loop_sound = 0;
  body->s.number = body->slot;
  body->timestamp = level(r)->time;
  body->physics_object = true;
  body->physics_bounce = 0;
  body->s.event = 0;
  body->s.pos.type = QA_Q3_TR_STATIONARY;
  qa_actor_id world_actor = qa_q3_game_entity_actor(qa_q3_entity_pool_at(pool_of(r), QA_Q3_GAME_ENTITYNUM_WORLD)).id;
  qa_q3_write_ground(body, &world_actor, pool_of(r));
  /* TS legsAnim/torsoAnim stores: no mirror fields. */
  if (body->s.weapon == QA_Q3_WP_NONE) body->s.weapon = QA_Q3_WP_MACHINEGUN;
  body->s.event = 0;
  body->r.sv_flags = entity->r.sv_flags;
  body->r.model_kind = entity->r.model_kind == QA_Q3_BASE_MODEL_CAPSULE ? QA_Q3_BASE_MODEL_CAPSULE : QA_Q3_BASE_MODEL_BOX;
  body->r.mins = entity->r.mins;
  body->r.maxs = entity->r.maxs;
  body->clipmask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP;
  body->r.contents = CONTENTS_BODY;
  body->r.owner_num = entity->r.owner_num;
  qa_q3_game_entity_set_takedamage(body, false);
  place_model(r, body, pad, offset);
  r->host.world->link(r->host.world->ctx, body);
  body->count = place;
  return body;
}
static void celebrate_stop(qa_q3_game_entity *player) { (void)player; /* torsoAnim: no mirror field */ }
static void celebrate_start(qa_q3_ta_arena_runtime *r, qa_q3_game_entity *player) {
  qa_q3_game_entity_set_nextthink(player, s32(level(r)->time + TIMER_GESTURE));
  player->think = qa_q3_callback_family_resolve(&pool_of(r)->callbacks.think, "q3.team-arena.arenas.celebrateStart.think");
  qa_q3_entity_pool_add_event(pool_of(r), player, QA_Q3_EV_TAUNT, 0);
}
static qa_vec3 podium_origin(qa_q3_ta_arena_runtime *r) {
  qa_vec3 forward = qa_qvm_angle_vectors(level(r)->intermission_angle).forward;
  qa_vec3 view = level(r)->intermission_origin;
  qa_vec3 origin = qa_math_vec3(
      view.x + (float)(forward.x * (float)cvar_integer(r, "g_podiumDist")),
      view.y + (float)(forward.y * (float)cvar_integer(r, "g_podiumDist")),
      view.z + (float)(forward.z * (float)cvar_integer(r, "g_podiumDist")));
  origin = qa_math_vec3(origin.x, origin.y, origin.z - (float)cvar_integer(r, "g_podiumDrop"));
  return origin;
}
static void podium_placement_think(qa_q3_ta_arena_runtime *r, qa_q3_game_entity *podium) {
  qa_q3_game_entity_set_nextthink(podium, s32(level(r)->time + 100));
  qa_q3_set_origin(podium, podium_origin(r));
  if (r->podium1 != NULL) place_model(r, r->podium1, podium, offset_first());
  if (r->podium2 != NULL) place_model(r, r->podium2, podium, offset_second());
  if (r->podium3 != NULL) place_model(r, r->podium3, podium, offset_third());
}
static qa_q3_game_entity *spawn_podium(qa_q3_ta_arena_runtime *r) {
  qa_q3_game_entity *podium = qa_q3_entity_pool_spawn(pool_of(r));
  qa_q3_game_entity_set_classname(podium, "podium");
  podium->s.e_type = QA_Q3_BASE_ET_GENERAL;
  podium->s.number = podium->slot;
  podium->clipmask = podium->r.contents = CONTENTS_SOLID;
  podium->s.modelindex = qa_q3_config_string_registry_model_index(r->host.config, "models/mapobjects/podium/podium4.md3");
  qa_q3_set_origin(podium, podium_origin(r));
  double yaw = qa_vector_to_angles(qa_sub3(level(r)->intermission_origin, podium->r.current_origin)).y;
  podium->s.apos.base = qa_math_vec3(podium->s.apos.base.x, yaw, podium->s.apos.base.z);
  r->host.world->link(r->host.world->ctx, podium);
  podium->think = qa_q3_callback_family_resolve(&pool_of(r)->callbacks.think, "q3.team-arena.arenas.spawnPodium.think");
  qa_q3_game_entity_set_nextthink(podium, s32(level(r)->time + 100));
  return podium;
}
void qa_q3_ta_arena_runtime_spawn_models_on_victory_pads(qa_q3_ta_arena_runtime *r) {
  qa_q3_ta_arena_runtime_reset_podium_players(r);
  qa_q3_game_entity *podium = spawn_podium(r);
  double number = sorted_at(r, 0);
  qa_q3_game_entity *player = spawn_model_on_pad(r, podium, offset_first(), qa_q3_entity_pool_at(pool_of(r), number),
      (double)((int32_t)slot_get(&qa_q3_entity_pool_client_at(pool_of(r), number)->ps.persistant, QA_Q3_BASE_PERS_RANK) & ~RANK_TIED_FLAG));
  qa_q3_game_entity_set_nextthink(player, s32(level(r)->time + 2000));
  player->think = qa_q3_callback_family_resolve(&pool_of(r)->callbacks.think, "q3.team-arena.arenas.spawnModelsOnVictoryPads.think");
  r->podium1 = player;
  number = sorted_at(r, 1);
  player = spawn_model_on_pad(r, podium, offset_second(), qa_q3_entity_pool_at(pool_of(r), number),
      (double)((int32_t)slot_get(&qa_q3_entity_pool_client_at(pool_of(r), number)->ps.persistant, QA_Q3_BASE_PERS_RANK) & ~RANK_TIED_FLAG));
  r->podium2 = player;
  if (level(r)->num_non_spectator_clients > 2) {
    number = sorted_at(r, 2);
    player = spawn_model_on_pad(r, podium, offset_third(), qa_q3_entity_pool_at(pool_of(r), number),
        (double)((int32_t)slot_get(&qa_q3_entity_pool_client_at(pool_of(r), number)->ps.persistant, QA_Q3_BASE_PERS_RANK) & ~RANK_TIED_FLAG));
    r->podium3 = player;
  }
}
void qa_q3_ta_arena_runtime_abort_podium(qa_q3_ta_arena_runtime *r) {
  if (r->host.match->host.settings(r->host.match->host.ctx).game_type != QA_Q3_BASE_GT_SINGLE_PLAYER) return;
  if (r->podium1 != NULL) {
    qa_q3_game_entity_set_nextthink(r->podium1, level(r)->time);
    r->podium1->think = qa_q3_callback_family_resolve(&pool_of(r)->callbacks.think, "q3.team-arena.arenas.celebrateStart.think");
  }
}
static void celebrate_stop_think(void *ctx, qa_q3_game_entity *entity) { (void)ctx; celebrate_stop(entity); }
static void podium_think(void *ctx, qa_q3_game_entity *entity) { podium_placement_think((qa_q3_ta_arena_runtime *)ctx, entity); }
static void celebrate_start_think(void *ctx, qa_q3_game_entity *entity) { celebrate_start((qa_q3_ta_arena_runtime *)ctx, entity); }
static qa_q3_entity_callback think_cb(qa_q3_entity_think_fn fn, void *ctx) {
  qa_q3_entity_callback cb = {NULL, ctx};
  memcpy(&cb.fn, &fn, sizeof fn < sizeof cb.fn ? sizeof fn : sizeof cb.fn);
  return cb;
}
void qa_q3_ta_arena_runtime_bind_save_callbacks(qa_q3_ta_arena_runtime *r) {
  qa_q3_callback_family *think = &pool_of(r)->callbacks.think;
  (void)qa_q3_callback_family_intern(think, "q3.team-arena.arenas.celebrateStart.think", think_cb(celebrate_stop_think, r));
  (void)qa_q3_callback_family_intern(think, "q3.team-arena.arenas.spawnPodium.think", think_cb(podium_think, r));
  (void)qa_q3_callback_family_intern(think, "q3.team-arena.arenas.spawnModelsOnVictoryPads.think", think_cb(celebrate_start_think, r));
}
