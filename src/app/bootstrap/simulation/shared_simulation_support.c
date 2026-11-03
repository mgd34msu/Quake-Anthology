/* Port of src/app/bootstrap/simulation/runtime.ts support: the TS
 * collections the class uses (Map/Set/RevisionMap as qa_ss_map), the free
 * helpers of runtime.ts (270-280) and the modClientListeners set. Kept apart
 * from the method files so it links on its own. GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"

static void *ss_grow(void *ptr, size_t *capacity, size_t need, size_t size) {
  if (need <= *capacity) return ptr;
  size_t next = *capacity ? *capacity * 2 : 8;
  while (next < need) next *= 2;
  void *grown = realloc(ptr, next * size);
  if (!grown) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}

/* ---- qa_ss_map ---- */
void qa_ss_map_init(qa_ss_map *map, qa_ss_key_kind kind, size_t value_size) {
  memset(map, 0, sizeof *map); map->key_kind = kind; map->value_size = value_size;
}
static void ss_key_free(qa_ss_map *map, qa_ss_key *key) { if (map->key_kind == QA_SS_KEY_STRING) free(key->string); }
void qa_ss_map_free(qa_ss_map *map) {
  for (size_t i = 0; i < map->count; i++) ss_key_free(map, &map->keys[i]);
  free(map->keys); free(map->values);
  map->keys = NULL; map->values = NULL; map->count = map->capacity = 0;
}
static bool ss_key_equal(qa_ss_key_kind kind, const qa_ss_key *stored, const void *key) {
  switch (kind) {
  case QA_SS_KEY_ACTOR: return qa_same_actor(stored->actor, *(const qa_actor_id *)key);
  case QA_SS_KEY_OWNED: return qa_same_owned_actor(stored->owned, *(const qa_owned_actor *)key);
  case QA_SS_KEY_CLIENT: return qa_same_client(stored->client, *(const qa_client_id *)key);
  case QA_SS_KEY_NUMBER: { double a = stored->number, b = *(const double *)key; return a == b || (isnan(a) && isnan(b)); }
  case QA_SS_KEY_STRING: return strcmp(stored->string, *(const char *const *)key) == 0;
  case QA_SS_KEY_POINTER: return stored->pointer == *(const void *const *)key;
  }
  return false;
}
static size_t ss_find(const qa_ss_map *map, const void *key) {
  for (size_t i = 0; i < map->count; i++) if (ss_key_equal(map->key_kind, &map->keys[i], key)) return i;
  return (size_t)-1;
}
void *qa_ss_map_get(const qa_ss_map *map, const void *key) {
  size_t index = ss_find(map, key);
  if (index == (size_t)-1) return NULL;
  return map->value_size ? map->values + index * map->value_size : (void *)&map->keys[index];
}
bool qa_ss_map_has(const qa_ss_map *map, const void *key) { return ss_find(map, key) != (size_t)-1; }
void *qa_ss_map_set(qa_ss_map *map, const void *key, const void *value) {
  map->revision++;
  size_t index = ss_find(map, key);
  if (index == (size_t)-1) {
    size_t capacity = map->capacity;
    map->keys = ss_grow(map->keys, &capacity, map->count + 1, sizeof *map->keys);
    if (map->value_size) {
      size_t value_capacity = map->capacity;
      map->values = ss_grow(map->values, &value_capacity, map->count + 1, map->value_size);
    }
    map->capacity = capacity;
    index = map->count++;
    qa_ss_key *stored = &map->keys[index];
    memset(stored, 0, sizeof *stored);
    switch (map->key_kind) {
    case QA_SS_KEY_ACTOR: stored->actor = *(const qa_actor_id *)key; break;
    case QA_SS_KEY_OWNED: stored->owned = *(const qa_owned_actor *)key; break;
    case QA_SS_KEY_CLIENT: stored->client = *(const qa_client_id *)key; break;
    case QA_SS_KEY_NUMBER: stored->number = *(const double *)key; break;
    case QA_SS_KEY_STRING: stored->string = qa_ss_strdup(*(const char *const *)key); break;
    case QA_SS_KEY_POINTER: stored->pointer = *(const void *const *)key; break;
    }
  }
  if (!map->value_size) return &map->keys[index];
  void *slot = map->values + index * map->value_size;
  if (value) memmove(slot, value, map->value_size); else memset(slot, 0, map->value_size);
  return slot;
}
bool qa_ss_map_delete(qa_ss_map *map, const void *key) {
  map->revision++;
  size_t index = ss_find(map, key);
  if (index == (size_t)-1) return false;
  ss_key_free(map, &map->keys[index]);
  memmove(&map->keys[index], &map->keys[index + 1], (map->count - index - 1) * sizeof *map->keys);
  if (map->value_size)
    memmove(map->values + index * map->value_size, map->values + (index + 1) * map->value_size,
            (map->count - index - 1) * map->value_size);
  map->count--;
  return true;
}
void qa_ss_map_clear(qa_ss_map *map) {
  map->revision++;
  for (size_t i = 0; i < map->count; i++) ss_key_free(map, &map->keys[i]);
  map->count = 0;
}

/* ---- TS free helpers ---- */
qa_vec3 qa_ss_add(qa_vec3 a, qa_vec3 b) {
  return (qa_vec3){(float)(a.x + b.x), (float)(a.y + b.y), (float)(a.z + b.z)};
}
qa_vec3 qa_ss_subtract(qa_vec3 a, qa_vec3 b) { return (qa_vec3){a.x - b.x, a.y - b.y, a.z - b.z}; }
double qa_ss_seconds(qa_source_time time) {
  return time.kind == QA_SOURCE_TIME_SECONDS ? time.value : time.value / 1000;
}
bool qa_ss_is_q1_barrel(const char *classname) {
  return qa_ss_is(classname, "misc_explobox") || qa_ss_is(classname, "misc_explobox2");
}
bool qa_ss_source_model(const char *path, double *out) {
  /* /^\*[0-9]+$/.test(path) ? Number(path.slice(1)) : null */
  if (!path || path[0] != '*' || path[1] == '\0') return false;
  for (const char *c = path + 1; *c; c++) if (*c < '0' || *c > '9') return false;
  *out = qa_js_string_to_number(path + 1);
  return true;
}
char *qa_ss_map_name(const char *requested_path) {
  const char *start = requested_path ? requested_path : "";
  if (strncmp(start, "maps/", 5) == 0) start += 5;
  size_t length = strlen(start);
  if (length >= 4 && strcmp(start + length - 4, ".bsp") == 0) length -= 4;
  char *name = malloc(length + 1);
  if (!name) qa_range_error("Out of memory");
  memcpy(name, start, length); name[length] = '\0';
  return name;
}
bool qa_ss_is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
char *qa_ss_strdup(const char *text) {
  if (!text) return NULL;
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
const char *qa_ss_number_text(double value, char buffer[32]) {
  qa_net_js_number(value, buffer, 32);
  return buffer;
}

/* modClientListeners: the TS Set of listeners and its delete closure. */
typedef struct ss_listener_unsubscribe { qa_app_shared_simulation *sim; qa_mod_client_listener_fn listener; void *ctx; } ss_listener_unsubscribe;
void qa_app_shared_sim_unsubscribe_listener(void *ctx) {
  ss_listener_unsubscribe *entry = ctx;
  qa_app_shared_simulation *sim = entry->sim;
  for (size_t i = 0; i < sim->mod_client_listener_count; i++)
    if (sim->mod_client_listeners[i].listener == entry->listener && sim->mod_client_listeners[i].ctx == entry->ctx) {
      memmove(&sim->mod_client_listeners[i], &sim->mod_client_listeners[i + 1],
              (sim->mod_client_listener_count - i - 1) * sizeof *sim->mod_client_listeners);
      sim->mod_client_listener_count--;
      break;
    }
  free(entry);
}
void qa_app_shared_sim_add_mod_client_listener(qa_app_shared_simulation *sim, qa_mod_client_listener_fn listener, void *ctx) {
  /* TS Set.add: a listener already present is not added twice. */
  for (size_t i = 0; i < sim->mod_client_listener_count; i++)
    if (sim->mod_client_listeners[i].listener == listener && sim->mod_client_listeners[i].ctx == ctx) return;
  sim->mod_client_listeners = ss_grow(sim->mod_client_listeners, &sim->mod_client_listener_capacity,
      sim->mod_client_listener_count + 1, sizeof *sim->mod_client_listeners);
  sim->mod_client_listeners[sim->mod_client_listener_count++] = (qa_ss_mod_client_listener){listener, ctx};
}
qa_mod_client_unsubscribe qa_app_shared_sim_subscribe_listener(qa_app_shared_simulation *sim, qa_mod_client_listener_fn listener, void *ctx) {
  qa_app_shared_sim_add_mod_client_listener(sim, listener, ctx);
  ss_listener_unsubscribe *entry = malloc(sizeof *entry);
  if (!entry) qa_range_error("Out of memory");
  *entry = (ss_listener_unsubscribe){sim, listener, ctx};
  return (qa_mod_client_unsubscribe){entry, qa_app_shared_sim_unsubscribe_listener};
}
