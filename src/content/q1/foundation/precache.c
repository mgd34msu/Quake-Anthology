/* Port of src/content/q1/foundation/precache.ts. GPL-2.0-or-later. */
#include "qa/content/q1/foundation/precache.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

struct qa_q1_precache_registry {
  bool frozen;
  char **models;
  size_t model_count, model_capacity;
  char **sounds;
  size_t sound_count, sound_capacity;
};

static char *dupstr(const char *text) {
  const size_t len = strlen(text) + 1;
  char *copy = malloc(len);
  if (copy) memcpy(copy, text, len);
  return copy;
}

static bool push(char ***names, size_t *count, size_t *capacity, const char *path) {
  if (*count == *capacity) {
    const size_t grown = *capacity ? *capacity * 2 : 16;
    char **next = realloc(*names, grown * sizeof *next);
    if (!next) return false;
    *names = next;
    *capacity = grown;
  }
  char *copy = dupstr(path);
  if (!copy) return false;
  (*names)[(*count)++] = copy;
  return true;
}

qa_q1_precache_registry *qa_q1_precache_registry_create(void) {
  qa_q1_precache_registry *registry = calloc(1, sizeof *registry);
  if (!registry) return NULL;
  if (!push(&registry->models, &registry->model_count, &registry->model_capacity, "") ||
      !push(&registry->sounds, &registry->sound_count, &registry->sound_capacity, "")) {
    qa_q1_precache_registry_destroy(registry);
    return NULL;
  }
  return registry;
}

void qa_q1_precache_registry_destroy(qa_q1_precache_registry *registry) {
  if (!registry) return;
  for (size_t i = 0; i < registry->model_count; i++) free(registry->models[i]);
  for (size_t i = 0; i < registry->sound_count; i++) free(registry->sounds[i]);
  free(registry->models);
  free(registry->sounds);
  free(registry);
}

const char *qa_q1_precache_phase(const qa_q1_precache_registry *registry) {
  return registry->frozen ? "frozen" : "loading";
}
const char *const *qa_q1_precache_models(const qa_q1_precache_registry *registry, size_t *out_count) {
  *out_count = registry->model_count;
  return (const char *const *)registry->models;
}
const char *const *qa_q1_precache_sounds(const qa_q1_precache_registry *registry, size_t *out_count) {
  *out_count = registry->sound_count;
  return (const char *const *)registry->sounds;
}

static const char *declare(qa_q1_precache_registry *registry, const char *path,
                           char ***names, size_t *count, size_t *capacity) {
  if (registry->frozen) qa_range_error("Q1 precache can only be done in spawn functions");
  if (path[0] == '\0' || (unsigned char)path[0] <= 32)
    qa_range_error("Invalid Q1 precache string");
  for (size_t i = 0; i < *count; i++)
    if (strcmp((*names)[i], path) == 0) return (*names)[i];
  if (!push(names, count, capacity, path)) return NULL; /* OOM only */
  return (*names)[*count - 1];
}

const char *qa_q1_precache_model(qa_q1_precache_registry *registry, const char *path) {
  return declare(registry, path, &registry->models, &registry->model_count, &registry->model_capacity);
}
const char *qa_q1_precache_sound(qa_q1_precache_registry *registry, const char *path) {
  return declare(registry, path, &registry->sounds, &registry->sound_count, &registry->sound_capacity);
}

void qa_q1_precache_begin_world(qa_q1_precache_registry *registry, const char *path, double inline_models) {
  if (registry->frozen || registry->model_count != 1 || registry->sound_count != 1)
    qa_range_error("Q1 world precaches must precede source spawn declarations");
  if (inline_models != floor(inline_models) || inline_models < 0)
    qa_range_error("Invalid Q1 inline model count");
  (void)qa_q1_precache_model(registry, path);
  for (double ordinal = 1; ordinal <= inline_models; ordinal += 1) {
    char name[32];
    (void)snprintf(name, sizeof name, "*%s", QA_JS_NUMBER(ordinal));
    (void)qa_q1_precache_model(registry, name);
  }
}

void qa_q1_precache_freeze(qa_q1_precache_registry *registry) { registry->frozen = true; }

static void validate(const char *const *names, size_t count) {
  if (count == 0 || strcmp(names[0], "") != 0)
    qa_range_error("Saved Q1 precache slot zero must be empty");
  for (size_t index = 0; index < count; index++) {
    bool duplicate = false;
    for (size_t earlier = 0; earlier < index; earlier++)
      if (strcmp(names[earlier], names[index]) == 0) duplicate = true;
    if (duplicate || (index != 0 && (names[index][0] == '\0' || (unsigned char)names[index][0] <= 32)))
      qa_range_error("Invalid saved Q1 precache declaration");
  }
}

void qa_q1_precache_restore(qa_q1_precache_registry *registry, qa_q1_precache_tables tables) {
  if (registry->frozen || registry->model_count != 1 || registry->sound_count != 1)
    qa_range_error("Restore Q1 precaches into a fresh registry");
  validate(tables.models, tables.model_count);
  validate(tables.sounds, tables.sound_count);
  for (size_t i = 0; i < registry->model_count; i++) free(registry->models[i]);
  for (size_t i = 0; i < registry->sound_count; i++) free(registry->sounds[i]);
  registry->model_count = 0;
  registry->sound_count = 0;
  for (size_t i = 0; i < tables.model_count; i++)
    (void)push(&registry->models, &registry->model_count, &registry->model_capacity, tables.models[i]);
  for (size_t i = 0; i < tables.sound_count; i++)
    (void)push(&registry->sounds, &registry->sound_count, &registry->sound_capacity, tables.sounds[i]);
  registry->frozen = tables.phase != NULL && strcmp(tables.phase, "frozen") == 0;
}
