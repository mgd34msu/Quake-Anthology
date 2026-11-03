/*
 * Port of src/content/q1/foundation/runtime.ts. GPL-2.0-or-later.
 * Q1Foundation is qa_q1_services plus these functions (see header).
 * Monster admission has no field on qa_q1_services, so it is kept in
 * a per-game side table here. Foreign-world admission catches
 * UnknownQ1SpawnError (a qa_raise by that name) as TS does.
 * Monster replacements allocate directly in the shared actor, body and
 * combat tables (TS allocateAtSource/bodies.create/combat.create); the
 * registry borrows their `provider/classname` definition strings, which
 * are interned process-wide.
 */
#include "qa/content/q1/foundation/runtime.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/movers.h"
#include "qa/content/q1/foundation/precache.h"
#include "qa/content/q1/foundation/spawns.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

/* `${provider}/${classname}`, interned: the actor registry borrows it. */
static const char *replacement_definition(const char *provider, const char *classname) {
  static char **interned = NULL;
  static size_t count = 0, capacity = 0;
  const size_t provider_length = strlen(provider), length = provider_length + strlen(classname) + 2;
  for (size_t i = 0; i < count; i++)
    if (!strncmp(interned[i], provider, provider_length) && interned[i][provider_length] == '/' &&
        !strcmp(interned[i] + provider_length + 1, classname))
      return interned[i];
  if (count == capacity) {
    const size_t grown = capacity ? capacity * 2 : 8;
    char **next = realloc(interned, grown * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    interned = next;
    capacity = grown;
  }
  char *definition = malloc(length);
  if (definition == NULL) qa_range_error("Out of memory");
  (void)snprintf(definition, length, "%s/%s", provider, classname);
  interned[count++] = definition;
  return definition;
}

typedef struct admission_entry {
  qa_q1_services *game;
  qa_q1_monster_admission value;
  bool has;
} admission_entry;
static admission_entry *admissions;
static size_t admission_count, admission_capacity;

void qa_q1_foundation_set_monster_admission(qa_q1_services *game, const qa_q1_monster_admission *admission) {
  for (size_t i = 0; i < admission_count; i++)
    if (admissions[i].game == game) {
      admissions[i].has = admission != NULL;
      if (admission) admissions[i].value = *admission;
      return;
    }
  if (admission == NULL) return;
  if (admission_count == admission_capacity) {
    const size_t grown = admission_capacity ? admission_capacity * 2 : 8;
    admission_entry *next = realloc(admissions, grown * sizeof *next);
    if (!next) return;
    admissions = next;
    admission_capacity = grown;
  }
  admissions[admission_count].game = game;
  admissions[admission_count].value = *admission;
  admissions[admission_count].has = true;
  admission_count++;
}

const qa_q1_monster_admission *qa_q1_foundation_monster_admission(qa_q1_services *game) {
  for (size_t i = 0; i < admission_count; i++)
    if (admissions[i].game == game && admissions[i].has) return &admissions[i].value;
  return NULL;
}

typedef struct report_storage {
  char **strings;
  size_t string_count, string_capacity;
} report_storage;

static const char *storage_dup(report_storage *storage, const char *text) {
  if (text == NULL) text = "";
  const size_t len = strlen(text) + 1;
  char *copy = malloc(len);
  if (!copy) return NULL;
  memcpy(copy, text, len);
  if (storage->string_count == storage->string_capacity) {
    const size_t grown = storage->string_capacity ? storage->string_capacity * 2 : 16;
    char **next = realloc(storage->strings, grown * sizeof *next);
    if (!next) {
      free(copy);
      return NULL;
    }
    storage->strings = next;
    storage->string_capacity = grown;
  }
  storage->strings[storage->string_count++] = copy;
  return copy;
}

void qa_q1_spawn_report_free(qa_q1_spawn_report *report) {
  if (report == NULL) return;
  report_storage *storage = report->storage;
  if (storage != NULL) {
    for (size_t i = 0; i < storage->string_count; i++) free(storage->strings[i]);
    free(storage->strings);
    free(storage);
  }
  free(report->spawned);
  free(report->inhibited);
  free(report->compiler_only);
  free(report->foreign);
  memset(report, 0, sizeof *report);
}

static void set_string(char **slot, const char *value) {
  if (value == NULL) value = "";
  if (*slot != NULL && strcmp(*slot, value) == 0) return;
  const size_t len = strlen(value) + 1;
  char *copy = malloc(len);
  if (!copy) return;
  memcpy(copy, value, len);
  free(*slot);
  *slot = copy;
}

static const char *inhibit(qa_q1_services *game, const char *classname, double flags, char *buffer, size_t size) {
  if (game->options.deathmatch != 0 &&
      ((qa_js_to_int32(flags) & 2048) != 0 || strncmp(classname, "monster_", 8) == 0))
    return "deathmatch";
  if (game->options.deathmatch == 0) {
    const double skill = qa_q1_option_skill(game);
    const int32_t bit = skill == 0 ? 256 : skill == 1 ? 512 : 1024;
    if ((qa_js_to_int32(flags) & bit) != 0) {
      (void)snprintf(buffer, size, "skill-%s", QA_JS_NUMBER(skill));
      return buffer;
    }
  }
  return NULL;
}

static bool kept(const double *keep, size_t keep_count, size_t ordinal) {
  if (keep == NULL || ordinal == 0) return true;
  for (size_t i = 0; i < keep_count; i++)
    if (keep[i] == (double)ordinal) return true;
  return false;
}

static bool push_note(qa_q1_spawn_note **notes, size_t *count, size_t *capacity, qa_q1_spawn_note note) {
  if (*count == *capacity) {
    const size_t grown = *capacity ? *capacity * 2 : 8;
    qa_q1_spawn_note *next = realloc(*notes, grown * sizeof *next);
    if (!next) return false;
    *notes = next;
    *capacity = grown;
  }
  (*notes)[(*count)++] = note;
  return true;
}

static void free_report_arg(void *report) { qa_q1_spawn_report_free(report); }
typedef struct spawn_attempt { qa_q1_services *game; qa_q1_actor *actor; } spawn_attempt;
static void spawn_attempt_run(void *ctx) {
  const spawn_attempt *attempt = ctx;
  qa_q1_spawn_entity(attempt->game, attempt->actor);
}

bool qa_q1_foundation_spawn_entities(qa_q1_services *game, const qa_q1_entity *entity_list, size_t entity_count,
                                     const qa_bounds *model_bounds, size_t model_count, const char *map_name,
                                     const double *keep, size_t keep_count, qa_q1_spawn_report *out) {
  memset(out, 0, sizeof *out);
  if (game->entity_count > 0) qa_range_error("Q1 map entities already spawned"); /* TS Error throw */
  report_storage *storage = calloc(1, sizeof *storage);
  if (!storage) return false;
  out->storage = storage;
  const qa_q1_monster_admission *admission = qa_q1_foundation_monster_admission(game);
  typedef struct replacement {
    bool has;
    const char *provider, *classname;
  } replacement;
  replacement *replacements = calloc(entity_count ? entity_count : 1, sizeof *replacements);
  if (!replacements) {
    qa_q1_spawn_report_free(out);
    return false;
  }
  /* A spawn function or a bad inline model raises out of the map load (TS
   * throws to the map command); release what this call allocated. */
  qa_cleanup report_cleanup, replacements_cleanup;
  qa_cleanup_push(&report_cleanup, free_report_arg, out);
  qa_cleanup_push(&replacements_cleanup, free, replacements);
  if (admission != NULL && admission->resolve != NULL) {
    for (size_t ordinal = 0; ordinal < entity_count; ordinal++) {
      if (!kept(keep, keep_count, ordinal)) continue;
      const qa_q1_entity *source = &entity_list[ordinal];
      const char *classname = qa_q1_entity_value(source, "classname");
      if (classname == NULL) classname = "";
      const char *flags_text = qa_q1_entity_value(source, "spawnflags");
      char reason_buffer[32];
      if (inhibit(game, classname, flags_text ? qa_js_string_to_number(flags_text) : 0, reason_buffer, sizeof reason_buffer))
        continue;
      const char *provider = NULL, *def_classname = NULL;
      if (admission->resolve(admission->ctx, classname, source, &provider, &def_classname)) {
        replacements[ordinal].has = true;
        replacements[ordinal].provider = provider;
        replacements[ordinal].classname = def_classname;
      }
    }
  }
  set_string(&game->map_name, map_name ? map_name : "");
  {
    char world_path[512];
    (void)snprintf(world_path, sizeof world_path, "maps/%s.bsp", game->map_name);
    qa_q1_precache_begin_world(game->precaches, world_path,
                               model_count > 0 ? (double)(model_count - 1) : 0);
  }
  game->next_dynamic_slot = (game->options.has_max_clients ? game->options.max_clients : 0) + (double)entity_count;
  size_t inhibited_cap = 0, compiler_cap = 0, foreign_cap = 0;
  bool oom = false;
  for (size_t ordinal = 0; ordinal < entity_count && !oom; ordinal++) {
    if (!kept(keep, keep_count, ordinal)) continue;
    const qa_q1_entity *source = &entity_list[ordinal];
    const char *classname = qa_q1_entity_value(source, "classname");
    if (classname == NULL) classname = "";
    const char *flags_text = qa_q1_entity_value(source, "spawnflags");
    const double flags = flags_text ? qa_js_string_to_number(flags_text) : 0; /* Number(spawnflags ?? 0) */
    char reason_buffer[32];
    const char *reason = inhibit(game, classname, flags, reason_buffer, sizeof reason_buffer);
    if (reason != NULL) {
      const char * classname2 = storage_dup(storage, classname);
      const char * reason2 = storage_dup(storage, reason);
      const qa_q1_spawn_note note = {(double)ordinal, classname2, reason2};
      oom = !push_note(&out->inhibited, &out->inhibited_count, &inhibited_cap, note);
      continue;
    }
    if (!strcmp(classname, "info_null")) {
      const qa_q1_spawn_note note = {(double)ordinal, storage_dup(storage, classname), NULL};
      oom = !push_note(&out->compiler_only, &out->compiler_only_count, &compiler_cap, note);
      continue;
    }
    const double ordinal_d = (double)ordinal;
    if (replacements[ordinal].has && admission != NULL) {
      const double slot = ordinal == 0 ? 0 : ordinal_d + (game->options.has_max_clients ? game->options.max_clients : 0);
      const qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(
          game->host.actors, qa_q1_services_provider(game), (int64_t)slot,
          replacement_definition(replacements[ordinal].provider, replacements[ordinal].classname));
      qa_body_state body;
      memset(&body, 0, sizeof body);
      const char *origin_text = qa_q1_entity_value(source, "origin");
      body.origin = qa_q1_parse_vector(origin_text ? origin_text : "");
      body.angles = qa_q1_source_angles(source);
      body.velocity = QA_Q1_ZERO;
      body.bounds = QA_Q1_POINT;
      body.ground = NULL;
      qa_shared_body_table_create_body(game->host.bodies, actor, body);
      qa_combat_state combat;
      memset(&combat, 0, sizeof combat);
      combat.health = 0;
      combat.armor.regular.kind = QA_REGULAR_ARMOR_NONE;
      combat.armor.powered.kind = "none";
      combat.mass = 100;
      combat.team = NULL;
      qa_gameplay_authority_create_state(game->host.combat, actor, combat, NULL, NULL);
      admission->spawn(admission->ctx, actor, source, ordinal_d, replacements[ordinal].provider,
                       replacements[ordinal].classname);
      continue;
    }
    qa_q1_actor *actor = qa_q1_create(game, classname, source, &ordinal_d);
    if (actor == NULL) {
      oom = true;
      break;
    }
    const qa_q1_body body = qa_q1_get_body(game, actor);
    if (actor->model != NULL && actor->model[0] == '*') {
      /* modelBounds[Number(model.slice(1))]: "*" is index 0; "*1.5", "*1x" are undefined. */
      const double index = qa_js_string_to_number(actor->model + 1);
      if (!(index >= 0) || index != trunc(index) || index >= (double)model_count || model_bounds == NULL)
        qa_range_error("Missing inline model %s", actor->model);
      const qa_bounds bounds = model_bounds[(size_t)index];
      qa_q1_body patch = body;
      patch.bounds.min = qa_q1_vsub(bounds.min, (qa_vec3){1, 1, 1});
      patch.bounds.max = qa_q1_vadd(bounds.max, (qa_vec3){1, 1, 1});
      qa_q1_set_body_patch(game, actor, &patch, false, false, false, true, false);
    }
    if (!game->options.foreign_world) {
      qa_q1_spawn_entity(game, actor);
      continue;
    }
    /* try { spawnEntity } catch (error) { if (!(error instanceof
     * UnknownQ1SpawnError)) throw error; foreign.push(...); remove(actor) } */
    spawn_attempt attempt = {game, actor};
    qa_caught_error caught;
    if (qa_try(spawn_attempt_run, &attempt, &caught)) continue;
    if (caught.name == NULL || strcmp(caught.name, "UnknownQ1SpawnError") != 0) qa_rethrow(&caught);
    const char *unknown = qa_q1_last_unknown_spawn();
    const qa_q1_spawn_note note = {(double)ordinal, storage_dup(storage, unknown != NULL ? unknown : classname), NULL};
    oom = !push_note(&out->foreign, &out->foreign_count, &foreign_cap, note);
    qa_q1_remove(game, actor);
  }
  qa_cleanup_pop(&replacements_cleanup);
  free(replacements);
  if (oom) {
    qa_cleanup_pop(&report_cleanup);
    qa_q1_spawn_report_free(out);
    return false;
  }
  qa_q1_link_doors(game);
  qa_q1_precache_freeze(game->precaches);
  qa_cleanup_pop(&report_cleanup);
  const size_t spawned_count = qa_q1_presentations(game, NULL, 0);
  if (spawned_count > 0) {
    out->spawned = malloc(spawned_count * sizeof *out->spawned);
    if (!out->spawned) {
      qa_q1_spawn_report_free(out);
      return false;
    }
    out->spawned_count = qa_q1_presentations(game, out->spawned, spawned_count);
  }
  return true;
}

bool qa_q1_foundation_spawn_map(qa_q1_services *game, const qa_q1_map *map, qa_q1_spawn_report *out) {
  qa_bounds *bounds = NULL;
  if (map->world.model_count > 0) {
    bounds = malloc(map->world.model_count * sizeof *bounds);
    if (!bounds) {
      memset(out, 0, sizeof *out);
      return false;
    }
    for (size_t i = 0; i < map->world.model_count; i++) bounds[i] = map->world.models[i].bounds;
  }
  const char *source = map->source ? map->source : "";
  const char *base = source;
  for (const char *p = source; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  char name[512];
  (void)snprintf(name, sizeof name, "%s", base);
  const size_t len = strlen(name);
  if (len >= 4 && !strcmp(name + len - 4, ".bsp")) name[len - 4] = '\0';
  qa_cleanup bounds_cleanup;
  qa_cleanup_push(&bounds_cleanup, free, bounds);
  const bool ok = qa_q1_foundation_spawn_entities(game, map->entity_list, map->entity_count, bounds,
                                                  map->world.model_count, name, NULL, 0, out);
  qa_cleanup_pop(&bounds_cleanup);
  free(bounds);
  return ok;
}
