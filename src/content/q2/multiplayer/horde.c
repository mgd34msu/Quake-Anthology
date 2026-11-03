/* Port of src/content/q2/multiplayer/horde.ts. GPL-2.0-or-later. */
#include "qa/content/q2/multiplayer/horde.h"
#include "multiplayer_internal.h"

static const char *const WAVE_POOL_0[] = {"monster_soldier", "monster_soldier", "monster_gunner"};
static const char *const WAVE_POOL_1[] = {"monster_soldier", "monster_infantry", "monster_gunner", "monster_berserk"};
static const char *const WAVE_POOL_2[] = {"monster_gunner", "monster_infantry", "monster_berserk", "monster_parasite", "monster_flyer"};
static const char *const WAVE_POOL_3[] = {"monster_infantry", "monster_berserk", "monster_gladiator", "monster_hover", "monster_mutant"};
static const char *const WAVE_POOL_4[] = {"monster_gladiator", "monster_tank", "monster_medic", "monster_brain", "monster_mutant"};
static const char *const *const WAVE_POOLS[] = {WAVE_POOL_0, WAVE_POOL_1, WAVE_POOL_2, WAVE_POOL_3, WAVE_POOL_4};
static const size_t WAVE_POOL_SIZES[] = {3, 4, 5, 5, 5};
#define TOTAL_WAVES 15

struct qa_q2_horde {
  qa_q2_entity *manager;
  double wave;
  qa_q2_entity **points;
  size_t point_count, point_cap;
  double spawned_this_wave;
  double intermission_ticks;
  qa_actor_id *wave_monsters;
  size_t monster_count, monster_cap;
  qa_q2_named_think think_entry;
  qa_q2_callback_definitions callbacks;
};

static bool is_point(const char *classname) {
  return strcmp(classname, "info_monster_start") == 0 || strcmp(classname, "info_monster_start_flying") == 0 ||
         strcmp(classname, "info_monster_start_ranged") == 0 || strcmp(classname, "info_monster_start_boss") == 0;
}

static void add_monster(qa_q2_horde *h, qa_actor_id id) {
  for (size_t i = 0; i < h->monster_count; i++)
    if (qa_same_actor(h->wave_monsters[i], id)) return;
  if (h->monster_count == h->monster_cap) {
    h->monster_cap = h->monster_cap == 0 ? 8 : h->monster_cap * 2;
    h->wave_monsters = qa_q2mp_xalloc(realloc(h->wave_monsters, h->monster_cap * sizeof *h->wave_monsters));
  }
  h->wave_monsters[h->monster_count++] = id;
}

static bool players_alive(qa_q2_horde *h, qa_q2_game_services *g) {
  (void)h;
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  for (size_t i = 0; i < n; i++)
    if (strcmp(all[i]->classname, "player") == 0 && qa_q2mp_health_or(g, all[i]->actor.id, 0) > 0) return true;
  return false;
}

static double alive_count(qa_q2_horde *h, qa_q2_game_services *g) {
  double alive = 0;
  size_t i = 0;
  while (i < h->monster_count) {
    qa_combat_state_result r = qa_q2mp_combat_read(g, h->wave_monsters[i]);
    if (!r.has_value || r.value.health <= 0) {
      h->wave_monsters[i] = h->wave_monsters[--h->monster_count];
    } else {
      alive++;
      i++;
    }
  }
  return alive;
}

static double wave_quota(qa_q2_horde *h) {
  double q = 4 + h->wave * 2;
  return q < 24 ? q : 24;
}

static void tick(qa_q2_horde *h, qa_q2_game_services *g) {
  if (h->manager == NULL || h->point_count == 0 || h->wave > TOTAL_WAVES) return;
  if (!players_alive(h, g)) return;
  double alive = alive_count(h, g);
  if (h->spawned_this_wave >= wave_quota(h)) {
    if (alive > 0) return;
    if (++h->intermission_ticks < 10) return;
    h->intermission_ticks = 0;
    h->wave++;
    h->spawned_this_wave = 0;
    if (h->wave > TOTAL_WAVES) {
      qa_q2_presentation_event done = {0};
      done.kind = "print"; done.level = "high"; done.text = "Horde complete: all waves cleared.\n";
      g->host->emit(g->host->ctx, done);
    }
    return;
  }
  if (h->wave == 0) h->wave = 1;
  double tier = trunc((h->wave - 1) / 3);
  size_t pool_index = tier > 4 ? 4 : (size_t)tier;
  const char *const *pool = WAVE_POOLS[pool_index];
  size_t pool_size = WAVE_POOL_SIZES[pool_index];
  double room = 12 - alive;
  if (room < 0) room = 0;
  double batch = 4;
  if (wave_quota(h) - h->spawned_this_wave < batch) batch = wave_quota(h) - h->spawned_this_wave;
  if (room < batch) batch = room;
  double spawned = 0;
  for (double index = 0; index < batch; index++) {
    size_t pi = (size_t)h->spawned_this_wave + (size_t)index;
    qa_q2_entity *point = h->points[pi % h->point_count];
    const char *classname = pool[pi % pool_size];
    qa_vec3 origin = qa_q2_game_body(g, point).origin;
    /* A monster or player still standing on the point blocks the rest of this batch. */
    static const qa_bounds monster_bounds = {{-16, -16, -24}, {16, 16, 32}};
    qa_q2_trace_request occupied_request = {0};
    occupied_request.start = origin; occupied_request.end = origin; occupied_request.bounds = &monster_bounds;
    occupied_request.ignore = &point->actor.id; occupied_request.mask = 0x2020003;
    qa_trace_result occupied = g->host->trace(g->host->ctx, occupied_request);
    if (occupied.start_solid || occupied.hit.kind == QA_TRACE_HIT_ACTOR) break;
    char ox[64], oy[64], oz[64], origin_text[200];
    qa_q2mp_format_number(ox, sizeof ox, origin.x);
    qa_q2mp_format_number(oy, sizeof oy, origin.y);
    qa_q2mp_format_number(oz, sizeof oz, origin.z);
    snprintf(origin_text, sizeof origin_text, "%s %s %s", ox, oy, oz);
    qa_q2_spawn_fields fields = {0};
    fields.ordinal = -1;
    fields.classname = classname;
    qa_q2_spawn_set(&fields, "classname", classname);
    qa_q2_spawn_set(&fields, "origin", origin_text);
    qa_q2_spawn_set(&fields, "angle", "0");
    qa_q2_entity *monster = g->spawn(g->ctx, fields);
    add_monster(h, monster->actor.id);
    spawned++;
  }
  h->spawned_this_wave += spawned;
}

static void think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_horde *h = ctx;
  tick(h, g);
  if (h->wave <= TOTAL_WAVES) {
    qa_q2_think self = {h, think_fn};
    g->schedule(g->ctx, entity, 0.5, self);
  }
}

qa_q2_horde *qa_q2_horde_create(void) {
  qa_q2_horde *h = calloc(1, sizeof *h);
  if (h == NULL) return NULL;
  h->think_entry.name = "q2:horde/manager";
  h->think_entry.cb = (qa_q2_think){h, think_fn};
  h->callbacks.think = &h->think_entry;
  h->callbacks.think_count = 1;
  return h;
}

void qa_q2_horde_destroy(qa_q2_horde *h) {
  if (h == NULL) return;
  free(h->points);
  free(h->wave_monsters);
  free(h);
}

static bool spawn_cb(void *ctx, qa_q2_entity *entity, qa_q2_game_services *g) {
  return qa_q2_horde_spawn(ctx, entity, g);
}

qa_q2_spawn_module qa_q2_horde_as_spawn(qa_q2_horde *h) {
  qa_q2_spawn_module m = {0};
  m.ctx = h;
  m.callbacks = &h->callbacks;
  m.spawn = spawn_cb;
  return m;
}

qa_q2_callback_definitions qa_q2_horde_callbacks(qa_q2_horde *h) { return h->callbacks; }
qa_q2_entity *qa_q2_horde_manager(qa_q2_horde *h) { return h->manager; }
double qa_q2_horde_wave(qa_q2_horde *h) { return h->wave; }

bool qa_q2_horde_spawn(qa_q2_horde *h, qa_q2_entity *entity, qa_q2_game_services *g) {
  qa_q2_think think = {h, think_fn};
  if (strcmp(entity->classname, "horde_manager") == 0) {
    h->manager = entity;
    g->solid(g->ctx, entity, "none");
    g->schedule(g->ctx, entity, 1, think);
    return true;
  }
  if (is_point(entity->classname)) {
    if (h->point_count == h->point_cap) {
      h->point_cap = h->point_cap == 0 ? 8 : h->point_cap * 2;
      h->points = qa_q2mp_xalloc(realloc(h->points, h->point_cap * sizeof *h->points));
    }
    h->points[h->point_count++] = entity;
    g->solid(g->ctx, entity, "none");
    return true;
  }
  return false;
}

bool qa_q2_horde_capture(qa_q2_horde *h, qa_q2_horde_checkpoint *out) {
  out->wave = h->wave;
  out->spawned = h->spawned_this_wave;
  out->intermission = h->intermission_ticks;
  out->monster_count = h->monster_count;
  out->monsters = malloc((h->monster_count == 0 ? 1 : h->monster_count) * sizeof *out->monsters);
  if (out->monsters == NULL) return false;
  for (size_t i = 0; i < h->monster_count; i++) {
    out->monsters[i].slot = (double)h->wave_monsters[i].slot;
    out->monsters[i].generation = (double)h->wave_monsters[i].generation;
  }
  return true;
}

void qa_q2_horde_restore(qa_q2_horde *h, qa_q2_game_services *g, qa_q2_horde_checkpoint saved) {
  h->wave = saved.wave;
  h->spawned_this_wave = saved.spawned;
  h->intermission_ticks = saved.intermission;
  h->monster_count = 0;
  for (size_t i = 0; i < saved.monster_count; i++) {
    qa_owned_actor owned;
    qa_actor_id id;
    if (qa_session_actor_registry_resolve_saved(g->host->actors, saved.monsters[i], &owned)) {
      id = owned.id;
    } else {
      id = qa_session_actor_registry_reference_saved(g->host->actors, saved.monsters[i], "checkpoint");
    }
    add_monster(h, id);
  }
  size_t n = 0;
  qa_q2_entity *const *all = g->entities(g->ctx, &n);
  qa_q2_think think = {h, think_fn};
  for (size_t i = 0; i < n; i++) {
    if (strcmp(all[i]->classname, "horde_manager") == 0) {
      h->manager = all[i];
      g->schedule(g->ctx, all[i], 1, think);
    } else if (is_point(all[i]->classname)) {
      bool known = false;
      for (size_t j = 0; j < h->point_count; j++)
        if (h->points[j] == all[i]) known = true;
      if (!known) {
        if (h->point_count == h->point_cap) {
          h->point_cap = h->point_cap == 0 ? 8 : h->point_cap * 2;
          h->points = qa_q2mp_xalloc(realloc(h->points, h->point_cap * sizeof *h->points));
        }
        h->points[h->point_count++] = all[i];
      }
    }
  }
}
