/* Port of src/content/q2/base/player/spawns.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/player/spawns.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"

static const char *const FIXED_COOP_MAPS[] = {
    "jail2", "jail4", "mine1", "mine2", "mine3", "mine4", "lab",
    "boss1", "fact3", "biggun", "space", "command", "power2", "strike"};

/* TS a.toLowerCase() === b.toLowerCase() over entity targetnames (Latin-1 byte strings). */
static bool equals_lower(const char *a, const char *b) { return qa_js_latin1_equals_ignore_case(a, b); }

static int64_t source_slot_of(qa_q2_game_services *game, qa_q2_entity *entity) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(game->host->actors, entity->actor.id);
  return source.has_value ? source.slot : entity->actor.id.slot;
}

qa_q2_entity **qa_q2_entities_named(qa_q2_game_services *game, const char *classname, size_t *out_count) {
  size_t total = 0;
  qa_q2_entity *const *entities = game->entities(game->ctx, &total);
  qa_q2_entity **out = NULL;
  size_t count = 0, cap = 0;
  for (size_t i = 0; i < total; i++) {
    if (strcmp(entities[i]->classname, classname) != 0) continue;
    if (count == cap) { cap = cap ? cap * 2 : 8; out = realloc(out, cap * sizeof *out); if (!out) qa_range_error("Out of memory"); }
    out[count++] = entities[i];
  }
  /* Stable insertion sort by source slot (JS Array.sort is stable). */
  for (size_t i = 1; i < count; i++) {
    qa_q2_entity *key = out[i];
    int64_t key_slot = source_slot_of(game, key);
    size_t j = i;
    while (j > 0 && source_slot_of(game, out[j - 1]) > key_slot) { out[j] = out[j - 1]; j--; }
    out[j] = key;
  }
  *out_count = count;
  return out;
}

static void security_spots_fn(void *c, qa_q2_entity *self, qa_q2_game_services *active) {
  (void)c; (void)self;
  static const double xs[] = {124, 252, 316};
  for (size_t i = 0; i < sizeof xs / sizeof xs[0]; i++) {
    qa_q2_entity *spot = active->create(active->ctx, "info_player_coop", NULL, 0);
    spot->targetname = "jail3";
    qa_q2_body_changes changes = {0};
    changes.has_origin = true; changes.origin = (qa_vec3){xs[i], -164, 80};
    changes.has_angles = true; changes.angles = (qa_vec3){0, 90, 0};
    active->move(active->ctx, spot, changes, true);
  }
}

static void coop_fix_fn(void *c, qa_q2_entity *self, qa_q2_game_services *active) {
  (void)c;
  size_t count = 0;
  qa_q2_entity **starts = qa_q2_entities_named(active, "info_player_start", &count);
  for (size_t i = 0; i < count; i++) {
    qa_vec3 a;
    qa_vec3 b;
    qa_q2_entity *start = starts[i];
    if (start->targetname[0] != '\0' &&
        qa_q2_length((a = qa_q2_game_body(active, start).origin, b = qa_q2_game_body(active, self).origin, qa_q2_subtract(a, b))) < 384) {
      if (!equals_lower(self->targetname, start->targetname)) self->targetname = start->targetname;
      break;
    }
  }
  free(starts);
}

/* TS q2PlayerSpawnCallbacks: the coop spot thinks are named source callbacks. */
static const qa_q2_named_think PLAYER_SPAWN_THINKS[] = {
  {"SP_CreateCoopSpots", {NULL, security_spots_fn}},
  {"SP_FixCoopSpots", {NULL, coop_fix_fn}},
};
static const qa_q2_callback_definitions PLAYER_SPAWN_CALLBACKS = {.think = PLAYER_SPAWN_THINKS, .think_count = 2};
const qa_q2_callback_definitions *qa_q2_player_spawn_callbacks(void) { return &PLAYER_SPAWN_CALLBACKS; }

bool qa_q2_player_spawns_spawn(qa_q2_entity *entity, qa_q2_game_services *game) {
  const char *classname = entity->classname;
  if (strcmp(classname, "info_player_start") == 0) {
    if (game->options.mode == QA_Q2_MODE_COOP && equals_lower(game->options.map_name, "security"))
      game->schedule(game->ctx, entity, 0.1, (qa_q2_think){NULL, security_spots_fn});
    return true;
  }
  if (strcmp(classname, "info_player_deathmatch") == 0) {
    if (game->options.mode != QA_Q2_MODE_DEATHMATCH) { game->remove(game->ctx, entity); return true; }
    entity->model = "models/objects/dmspot/tris.md2";
    entity->skin = 1;
    qa_q2_body_changes changes = {0};
    changes.has_bounds = true;
    changes.bounds = (qa_bounds){{-32, -32, -24}, {32, 32, -16}};
    game->move(game->ctx, entity, changes, false);
    game->solid(game->ctx, entity, "box");
    game->show(game->ctx, entity);
    return true;
  }
  if (strcmp(classname, "info_player_coop") == 0) {
    if (game->options.mode != QA_Q2_MODE_COOP) { game->remove(game->ctx, entity); return true; }
    bool fixed = false;
    for (size_t i = 0; i < sizeof FIXED_COOP_MAPS / sizeof FIXED_COOP_MAPS[0]; i++)
      if (equals_lower(game->options.map_name, FIXED_COOP_MAPS[i])) fixed = true;
    if (fixed) game->schedule(game->ctx, entity, 0.1, (qa_q2_think){NULL, coop_fix_fn});
    return true;
  }
  if (strcmp(classname, "info_player_intermission") == 0) return true;
  return false;
}

static bool spawn_module_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  return qa_q2_player_spawns_spawn(entity, game);
}
qa_q2_spawn_module qa_q2_get_player_spawns(void) {
  return (qa_q2_spawn_module){NULL, &PLAYER_SPAWN_CALLBACKS, NULL, spawn_module_fn};
}

double qa_q2_players_range(qa_q2_game_services *game, qa_q2_entity *spot) {
  double closest = 9999999;
  size_t count = 0;
  const qa_actor_id *players = game->host->players(game->host->ctx, &count);
  for (size_t i = 0; i < count; i++) {
    qa_combat_state_result combat = qa_gameplay_authority_read(game->host->combat, players[i]);
    if ((combat.has_value ? combat.value.health : 0) <= 0) continue;
    qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, players[i]);
    if (body.has_value)
      closest = qa_js_min(closest, qa_q2_length(qa_q2_subtract(body.value.origin, qa_q2_game_body(game, spot).origin)));
  }
  return closest;
}

qa_q2_entity *qa_q2_select_spawn(qa_q2_game_services *game, qa_q2_base_player_state *state, const char *spawn_point) {
  qa_q2_entity *spot = NULL;
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH) {
    size_t count = 0;
    qa_q2_entity **spots = qa_q2_entities_named(game, "info_player_deathmatch", &count);
    if ((qa_js_to_int32(qa_q2_options_deathmatch_flags(&game->options)) & 512) != 0) {
      double best = 0;
      for (size_t i = 0; i < count; i++) {
        double distance = qa_q2_players_range(game, spots[i]);
        if (distance > best) { best = distance; spot = spots[i]; }
      }
      if (!spot && count > 0) spot = spots[0];
    } else {
      qa_q2_entity *first = NULL, *second = NULL;
      double first_range = 99999, second_range = 99999;
      for (size_t i = 0; i < count; i++) {
        double range = qa_q2_players_range(game, spots[i]);
        /* Preserve source selection, including its non-shifting
         * first/second closest slots. */
        if (range < first_range) { first_range = range; first = spots[i]; }
        else if (range < second_range) { second_range = range; second = spots[i]; }
      }
      double selectable = count <= 2 ? (double)count : (double)count - 2;
      if (selectable > 0) {
        double selection = floor(game->host->random(game->host->ctx) * selectable);
        for (size_t i = 0; i < count; i++) {
          if (count > 2 && (spots[i] == first || spots[i] == second)) selection++;
          if (selection-- == 0) { spot = spots[i]; break; }
        }
      }
    }
    free(spots);
  } else if (game->options.mode == QA_Q2_MODE_COOP && state->slot != 0) {
    size_t count = 0;
    qa_q2_entity **spots = qa_q2_entities_named(game, "info_player_coop", &count);
    size_t wanted = 0;
    for (size_t i = 0; i < count; i++) {
      if (!equals_lower(spots[i]->targetname, spawn_point)) continue;
      if (wanted == (size_t)(state->slot - 1)) { spot = spots[i]; break; }
      wanted++;
    }
    free(spots);
  }
  if (!spot) {
    size_t count = 0;
    qa_q2_entity **starts = qa_q2_entities_named(game, "info_player_start", &count);
    for (size_t i = 0; i < count; i++) {
      bool match = spawn_point[0] == '\0' ? starts[i]->targetname[0] == '\0' : equals_lower(starts[i]->targetname, spawn_point);
      if (match) { spot = starts[i]; break; }
    }
    if (!spot && spawn_point[0] == '\0' && count > 0) spot = starts[0];
    free(starts);
  }
  if (!spot) qa_range_error("No Q2 player spawn for '%s' on %s", spawn_point, game->options.map_name);
  return spot;
}

bool qa_q2_player_kill_box(qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_body_state body = qa_q2_game_body(game, entity);
  for (;;) {
    qa_q2_trace_request request = {0};
    request.start = body.origin; request.end = body.origin; request.bounds = &body.bounds;
    request.ignore = &entity->actor.id; request.mask = 0x2010003;
    qa_trace_result trace = game->host->trace(game->host->ctx, request);
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR) return !trace.start_solid;
    qa_actor_id target = trace.hit.actor;
    qa_actor_id self_id = entity->actor.id;
    game->damage(game->ctx, target, entity, entity->actor.id, &self_id, 100000, 0, QA_Q2_ZERO, body.origin, QA_Q2_ZERO, 21, 32, NULL);
    if (qa_session_actor_registry_is_live(game->host->actors, target)) {
      qa_q2_entity *other = game->entity(game->ctx, &target);
      if (!other || strcmp(other->solid, "none") != 0) return false;
    }
  }
}

qa_vec3 qa_q2_spawn_origin(qa_q2_entity *entity, qa_q2_game_services *game) {
  return qa_q2_add(qa_q2_game_body(game, entity).origin, (qa_vec3){0, 0, 10});
}
