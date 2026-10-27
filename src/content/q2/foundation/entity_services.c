/* Port of src/content/q2/foundation/entity-services.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/entity_services.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/foundation/support.h"
#include "qa/world/gameplay/damage_modifier.h"

#define MASK_SOLID 3

typedef struct slot_entry { qa_actor_id id; double slot; } slot_entry;
typedef struct freed_entry { double slot, time; } freed_entry;

struct qa_q2_entity_services {
  qa_q2_game_services game;
  qa_q2_counters counters;
  qa_q2_source_callbacks *callbacks;
  const qa_q2_spawn_module *modules;
  size_t module_count;
  qa_q2_entity **entities;
  size_t entity_count, entity_cap;
  qa_authored_target **authored;
  size_t authored_count, authored_cap;
  slot_entry *slots;
  size_t slot_count, slot_cap;
  freed_entry *freed;
  size_t freed_count, freed_cap;
  qa_q2_entity **unsupported;
  size_t unsupported_count, unsupported_cap;
  double next_source_slot, sequence;
  /* scratch */
  qa_owned_actor *team_scratch; size_t team_cap;
  qa_q2_entity **target_scratch; size_t target_cap;
  /* capture storage */
  qa_q2_entity_checkpoint *cp_entities; size_t cp_count;
  qa_q2_freed_slot *cp_freed;
  /* restored lastAttack storage */
  qa_attack_provenance *attacks; qa_actor_id *attack_ids; size_t attack_count, attack_cap;
  /* definition strings for allocateActor */
  char **def_strings; size_t def_count, def_cap;
  char *diag_buf; char *self_buf; char *count_buf;
};

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) qa_range_error("out of memory");
  return q;
}
static char *fmt(char **buf, const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  va_list cp;
  va_copy(cp, ap);
  int n = vsnprintf(NULL, 0, format, cp);
  va_end(cp);
  char *next = realloc(*buf, (size_t)n + 1);
  if (!next) { va_end(ap); qa_range_error("out of memory"); }
  *buf = next;
  vsnprintf(*buf, (size_t)n + 1, format, ap);
  va_end(ap);
  return *buf;
}

static bool same_id(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }

static qa_q2_entity *find_entity(qa_q2_entity_services *s, qa_actor_id id) {
  for (size_t i = 0; i < s->entity_count; i++)
    if (same_id(s->entities[i]->actor.id, id)) return s->entities[i];
  return NULL;
}
static void push_entity(qa_q2_entity_services *s, qa_q2_entity *e) {
  if (s->entity_count == s->entity_cap) { s->entity_cap = s->entity_cap ? s->entity_cap * 2 : 16; s->entities = xrealloc(s->entities, s->entity_cap * sizeof *s->entities); }
  s->entities[s->entity_count++] = e;
}
static void remove_entity_ptr(qa_q2_entity_services *s, qa_q2_entity *e) {
  for (size_t i = 0; i < s->entity_count; i++)
    if (s->entities[i] == e) { memmove(&s->entities[i], &s->entities[i + 1], (s->entity_count - i - 1) * sizeof *s->entities); s->entity_count--; return; }
}
static bool slot_get(qa_q2_entity_services *s, qa_actor_id id, double *out) {
  for (size_t i = 0; i < s->slot_count; i++) if (same_id(s->slots[i].id, id)) { *out = s->slots[i].slot; return true; }
  return false;
}
static void slot_set(qa_q2_entity_services *s, qa_actor_id id, double slot) {
  for (size_t i = 0; i < s->slot_count; i++) if (same_id(s->slots[i].id, id)) { s->slots[i].slot = slot; return; }
  if (s->slot_count == s->slot_cap) { s->slot_cap = s->slot_cap ? s->slot_cap * 2 : 16; s->slots = xrealloc(s->slots, s->slot_cap * sizeof *s->slots); }
  s->slots[s->slot_count++] = (slot_entry){id, slot};
}
static void slot_delete(qa_q2_entity_services *s, qa_actor_id id) {
  for (size_t i = 0; i < s->slot_count; i++)
    if (same_id(s->slots[i].id, id)) { memmove(&s->slots[i], &s->slots[i + 1], (s->slot_count - i - 1) * sizeof *s->slots); s->slot_count--; return; }
}
static double source_slot_of(qa_q2_entity_services *s, qa_actor_id id) {
  qa_actor_source_result r = qa_session_actor_registry_source_of(s->game.host->actors, id);
  return r.has_value ? (double)r.slot : (double)id.slot;
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_q2_entity_services *s = ctx;
  qa_q2_entity *e = find_entity(s, actor.id);
  if (e) {
    for (size_t i = 0; i < s->unsupported_count; i++)
      if (s->unsupported[i] == e) { memmove(&s->unsupported[i], &s->unsupported[i + 1], (s->unsupported_count - i - 1) * sizeof *s->unsupported); s->unsupported_count--; break; }
  }
  double slot;
  if (slot_get(s, actor.id, &slot) && slot > s->game.options.max_clients) {
    bool found = false;
    for (size_t i = 0; i < s->freed_count; i++) if (s->freed[i].slot == slot) { s->freed[i].time = s->game.host->now(s->game.host->ctx); found = true; }
    if (!found) {
      if (s->freed_count == s->freed_cap) { s->freed_cap = s->freed_cap ? s->freed_cap * 2 : 16; s->freed = xrealloc(s->freed, s->freed_cap * sizeof *s->freed); }
      s->freed[s->freed_count++] = (freed_entry){slot, s->game.host->now(s->game.host->ctx)};
    }
  }
  slot_delete(s, actor.id);
  if (e) { remove_entity_ptr(s, e); qa_q2_entity_destroy(e); free(e); }
  for (size_t i = 0; i < s->authored_count; i++)
    if (same_id(s->authored[i]->actor.id, actor.id)) { memmove(&s->authored[i], &s->authored[i + 1], (s->authored_count - i - 1) * sizeof *s->authored); s->authored_count--; break; }
}

/* ---- game service implementations ---- */
static qa_q2_entity *svc_entity(void *ctx, const qa_actor_id *actor);
static void svc_remove(void *ctx, qa_q2_entity *e);
static void svc_schedule(void *ctx, qa_q2_entity *e, double delay, qa_q2_think think);
static void svc_cancel(void *ctx, qa_q2_entity *e);
static qa_body_state svc_body(void *ctx, qa_owned_actor actor);
static qa_attack_provenance svc_attack(void *ctx, qa_q2_entity *inf_e, qa_actor_id inf_id, const qa_actor_id *attacker, double mod, double flags, qa_item_id weapon);

static qa_q2_entity *const *svc_entities(void *ctx, size_t *n) {
  qa_q2_entity_services *s = ctx;
  *n = s->entity_count;
  return s->entities;
}

static void invoke(qa_q2_entity_services *s, qa_actor_id actor, void (*cb)(void *), void *cbctx) {
  bool prev_has = s->game.has_current_actor;
  qa_actor_id prev = s->game.current_actor;
  s->game.has_current_actor = true; s->game.current_actor = actor;
  cb(cbctx);
  s->game.has_current_actor = prev_has; s->game.current_actor = prev;
}

typedef struct think_ctx { qa_q2_entity_services *s; qa_q2_entity *e; } think_ctx;
static void run_think(void *c) {
  think_ctx *t = c;
  qa_q2_think cb = t->e->think;
  t->e->has_next_think = false;
  if (cb.fn) cb.fn(cb.ctx, t->e, &t->s->game);
}
/* ActorCallbacks need entity+services; store a per-entity binding record. */
typedef struct binding { qa_q2_entity_services *s; qa_q2_entity *e; } binding;
static binding *binding_new(qa_q2_entity_services *s, qa_q2_entity *e) {
  binding *b = malloc(sizeof *b);
  if (!b) qa_range_error("out of memory");
  b->s = s; b->e = e;
  return b;
}
static void b_think(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  (void)self; (void)frame;
  binding *b = ctx;
  think_ctx t = {b->s, b->e};
  invoke(b->s, b->e->actor.id, run_think, &t);
}
static void b_use(void *ctx, qa_owned_actor self, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)self;
  binding *b = ctx;
  if (b->e->use.fn) b->e->use.fn(b->e->use.ctx, b->e, &b->s->game, other, activator);
}
static void b_touch(void *ctx, qa_touch_contact contact) {
  binding *b = ctx;
  if (b->e->touch.fn) b->e->touch.fn(b->e->touch.ctx, b->e, &b->s->game, contact);
}
static void b_pain(void *ctx, qa_pain_reaction r) {
  binding *b = ctx;
  if (b->e->pain.fn) b->e->pain.fn(b->e->pain.ctx, b->e, &b->s->game, r);
}
static void b_die(void *ctx, qa_death_reaction r) {
  binding *b = ctx;
  if (b->e->die.fn) b->e->die.fn(b->e->die.ctx, b->e, &b->s->game, r);
}

static void bind_callbacks(qa_q2_entity_services *s, qa_q2_entity *e) {
  binding *b = binding_new(s, e);
  qa_actor_callbacks cbs = {.ctx = b, .think = b_think, .touch = b_touch, .use = b_use, .pain = b_pain, .die = b_die};
  qa_actor_callback_table_bind(s->game.host->callbacks, e->actor, cbs);
}

qa_owned_actor qa_q2_entity_services_allocate_actor(qa_q2_entity_services *s, const qa_q2_spawn_fields *f, const char *definition) {
  /* reusable: lowest freed slot with freed<2 or now-freed>0.5 */
  double best = -1;
  for (size_t i = 0; i < s->freed_count; i++)
    if ((s->freed[i].time < 2 || s->game.host->now(s->game.host->ctx) - s->freed[i].time > 0.5) && (best < 0 || s->freed[i].slot < best)) best = s->freed[i].slot;
  double slot = strcmp(f->classname, "worldspawn") == 0 ? 0 : best >= 0 ? best : s->next_source_slot++;
  for (size_t i = 0; i < s->freed_count; i++)
    if (s->freed[i].slot == slot) { memmove(&s->freed[i], &s->freed[i + 1], (s->freed_count - i - 1) * sizeof *s->freed); s->freed_count--; break; }
  const char *def = definition;
  if (!def) {
    if (s->def_count == s->def_cap) { s->def_cap = s->def_cap ? s->def_cap * 2 : 16; s->def_strings = xrealloc(s->def_strings, s->def_cap * sizeof *s->def_strings); }
    char *buf = NULL;
    fmt(&buf, "q2:%s", f->classname);
    s->def_strings[s->def_count++] = buf;
    def = buf;
  }
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(s->game.host->actors, s->game.options.provider, (int64_t)slot, def);
  slot_set(s, actor.id, slot);
  qa_vec3 angles = qa_q2_spawn_has(f, "angles") ? qa_q2_vector_field(f, "angles") : (qa_vec3){0, qa_q2_number_field(f, "angle", 0), 0};
  qa_body_state body = {.origin = qa_q2_vector_field(f, "origin"), .angles = angles, .velocity = QA_Q2_ZERO, .bounds = {QA_Q2_ZERO, QA_Q2_ZERO}, .ground = NULL};
  qa_shared_body_table_create_body(s->game.host->bodies, actor, body);
  return actor;
}

qa_q2_entity *qa_q2_entity_services_attach(qa_q2_entity_services *s, qa_owned_actor actor, qa_q2_spawn_fields f) {
  qa_session_actor_registry_assert_owned(s->game.host->actors, actor);
  if (find_entity(s, actor.id)) qa_range_error("Q2 actor already has an entity continuation");
  if (!qa_shared_body_table_read(s->game.host->bodies, actor.id).has_value) qa_range_error("Q2 entity attachment requires an existing shared body");
  qa_q2_entity *e = calloc(1, sizeof *e);
  if (!e) qa_range_error("out of memory");
  qa_q2_entity_init(e, actor, f);
  int64_t mask = s->game.options.edition == QA_Q2_EDITION_RERELEASE ? 0xff00 : 0x1f00;
  e->spawnflags = (double)((int64_t)qa_q2_integer_field(&f, "spawnflags", 0) & ~mask);
  e->delay = qa_q2_number_field(&f, "delay", 0);
  e->wait = qa_q2_number_field(&f, "wait", 0);
  e->speed = qa_q2_number_field(&f, "speed", 0);
  e->accel = qa_q2_number_field(&f, "accel", 0);
  e->decel = qa_q2_number_field(&f, "decel", 0);
  e->damage = qa_q2_integer_field(&f, "dmg", 0);
  e->count = qa_q2_integer_field(&f, "count", 0);
  e->max_health = qa_q2_integer_field(&f, "health", 0);
  const char *noise = qa_q2_spawn_get(&f, "noise");
  e->noise = noise ? noise : "";
  const char *map = qa_q2_spawn_get(&f, "map");
  e->map = map ? map : "";
  e->volume = qa_q2_number_field(&f, "volume", 0);
  e->attenuation = qa_q2_number_field(&f, "attenuation", 0);
  e->random = qa_q2_number_field(&f, "random", 0);
  e->style = qa_q2_integer_field(&f, "style", 0);
  push_entity(s, e);
  bind_callbacks(s, e);
  if (s->game.host->register_entity) s->game.host->register_entity(s->game.host->ctx, e, s);
  return e;
}

static qa_q2_entity *allocate(qa_q2_entity_services *s, qa_q2_spawn_fields f) {
  qa_owned_actor actor = qa_q2_entity_services_allocate_actor(s, &f, NULL);
  return qa_q2_entity_services_attach(s, actor, f);
}

qa_q2_entity *qa_q2_entity_services_spawn_entity(qa_q2_entity_services *s, qa_q2_entity *e) {
  for (size_t i = 0; i < s->module_count; i++)
    if (s->modules[i].spawn(s->modules[i].ctx, e, &s->game)) return e;
  if (s->unsupported_count == s->unsupported_cap) { s->unsupported_cap = s->unsupported_cap ? s->unsupported_cap * 2 : 16; s->unsupported = xrealloc(s->unsupported, s->unsupported_cap * sizeof *s->unsupported); }
  s->unsupported[s->unsupported_count++] = e;
  s->game.host->diagnostic(s->game.host->ctx, fmt(&s->diag_buf, "Q2 spawn handler not yet imported: %s at authored entity %.17g", e->classname, e->spawn.ordinal));
  return e;
}

qa_q2_entity *qa_q2_entity_services_spawn(qa_q2_entity_services *s, qa_q2_spawn_fields f) {
  return qa_q2_entity_services_spawn_entity(s, allocate(s, f));
}

qa_q2_entity *qa_q2_entity_services_create_entity(qa_q2_entity_services *s, const char *classname, const qa_q2_field_pair *v, size_t n) {
  qa_q2_spawn_fields f = {.ordinal = -1, .classname = classname};
  if (!qa_q2_spawn_set(&f, "classname", classname)) qa_range_error("out of memory");
  for (size_t i = 0; i < n; i++) if (!qa_q2_spawn_set(&f, v[i].key, v[i].value)) qa_range_error("out of memory");
  f.classname = qa_q2_spawn_get(&f, "classname");
  return allocate(s, f);
}

qa_q2_entity *qa_q2_entity_services_attach_player(qa_q2_entity_services *s, qa_owned_actor actor) {
  qa_session_actor_registry_assert_owned(s->game.host->actors, actor);
  qa_q2_entity *existing = find_entity(s, actor.id);
  if (existing) return existing;
  qa_q2_spawn_fields f = {.ordinal = -1, .classname = "player"};
  if (!qa_q2_spawn_set(&f, "classname", "player")) qa_range_error("out of memory");
  qa_q2_entity *e = qa_q2_entity_services_attach(s, actor, f);
  e->max_health = 100;
  e->view_height = 22;
  return e;
}

static qa_q2_entity *svc_entity(void *ctx, const qa_actor_id *actor) {
  qa_q2_entity_services *s = ctx;
  if (!actor) return NULL;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(s->game.host->actors, *actor, &owner)) return NULL;
  return find_entity(s, owner.id);
}

static void delayed_use_fn(void *c, qa_q2_entity *e, qa_q2_game_services *g) {
  (void)c;
  qa_actor_id act = e->activator;
  qa_q2_game_use_targets_entity(g, e, e->has_activator ? &act : NULL, false);
  g->remove(g->ctx, e);
}

static qa_q2_entity *svc_spawn(void *ctx, qa_q2_spawn_fields f) { return qa_q2_entity_services_spawn(ctx, f); }
static qa_q2_entity *svc_create(void *ctx, const char *cn, const qa_q2_field_pair *v, size_t n) { return qa_q2_entity_services_create_entity(ctx, cn, v, n); }

static void svc_remove(void *ctx, qa_q2_entity *e) {
  qa_q2_entity_services *s = ctx;
  if (!qa_session_actor_registry_is_live(s->game.host->actors, e->actor.id)) return;
  qa_shared_body_table_unlink(s->game.host->bodies, e->actor);
  qa_actor_source_result src = qa_session_actor_registry_source_of(s->game.host->actors, e->actor.id);
  if (strcmp(e->classname, "bodyque") == 0 || (src.has_value && strcmp(src.provider, s->game.options.provider) == 0 && src.slot <= s->game.options.max_clients)) return;
  svc_cancel(s, e);
  qa_q2_presentation_event ev = {0};
  ev.kind = "visibility"; ev.has_actor = true; ev.actor = e->actor.id; ev.visible = false;
  s->game.host->emit(s->game.host->ctx, ev);
  qa_session_actor_registry_release(s->game.host->actors, e->actor);
}

static qa_q2_weapon_target_result svc_weapon_target(void *ctx, qa_actor_id actor) {
  qa_q2_entity_services *s = ctx;
  qa_q2_weapon_target_result out = {0};
  if (!qa_session_actor_registry_is_live(s->game.host->actors, actor)) return out;
  if (s->game.host->weapon_target) return s->game.host->weapon_target(s->game.host->ctx, actor);
  qa_q2_entity *e = svc_entity(s, &actor);
  if (!e) return out;
  out.has_value = true;
  out.value = (qa_q2_weapon_target){e->solid, e->laser_immune, e->damageable_target, strcmp(e->classname, "misc_explobox") == 0};
  return out;
}

static const char *svc_item_name(void *ctx, const char *classname) {
  qa_q2_entity_services *s = ctx;
  for (size_t i = 0; i < s->module_count; i++) {
    if (!s->modules[i].item_name) continue;
    const char *name = s->modules[i].item_name(s->modules[i].ctx, classname);
    if (name) return name;
  }
  return NULL;
}

static const qa_owned_actor *svc_push_team(void *ctx, qa_actor_id actor, size_t *n) {
  qa_q2_entity_services *s = ctx;
  *n = 0;
  qa_q2_entity *e = svc_entity(s, &actor);
  if (!e) return NULL;
  qa_q2_entity *master = e->has_team_master ? svc_entity(s, &e->team_master) : NULL;
  if (!master) master = e;
  size_t count = 0;
  for (qa_q2_entity *m = master; m; m = m->has_team_chain ? svc_entity(s, &m->team_chain) : NULL) {
    if (count == s->team_cap) { s->team_cap = s->team_cap ? s->team_cap * 2 : 8; s->team_scratch = xrealloc(s->team_scratch, s->team_cap * sizeof *s->team_scratch); }
    s->team_scratch[count++] = m->actor;
  }
  *n = count;
  return s->team_scratch;
}

static qa_body_state svc_body(void *ctx, qa_owned_actor actor) {
  qa_q2_entity_services *s = ctx;
  qa_body_state_result r = qa_shared_body_table_read(s->game.host->bodies, actor.id);
  if (!r.has_value) qa_range_error("Q2 callback used a released body");
  return r.value;
}

static void svc_move(void *ctx, qa_q2_entity *e, qa_q2_body_changes c, bool link) {
  qa_q2_entity_services *s = ctx;
  qa_body_state body = svc_body(s, e->actor);
  qa_actor_id ground = c.ground;
  if (c.has_origin) body.origin = c.origin;
  if (c.has_angles) body.angles = c.angles;
  if (c.has_velocity) body.velocity = c.velocity;
  if (c.has_bounds) body.bounds = c.bounds;
  if (c.has_ground) body.ground = c.ground_null ? NULL : &ground;
  qa_shared_body_table_write(s->game.host->bodies, e->actor, body);
  if (link) qa_shared_body_table_link(s->game.host->bodies, e->actor, NULL);
}

static void svc_link(void *ctx, qa_q2_entity *e) {
  qa_q2_entity_services *s = ctx;
  qa_shared_body_table_link(s->game.host->bodies, e->actor, NULL);
}

static void svc_solid(void *ctx, qa_q2_entity *e, const char *solid) {
  qa_q2_entity_services *s = ctx;
  e->solid = solid;
  bool has_model = e->model[0] == '*';
  double model = has_model ? strtod(e->model + 1, NULL) : 0;
  if (has_model) {
    qa_q2_body_changes c = {0};
    c.has_bounds = true;
    c.bounds = s->game.host->inline_model_bounds(s->game.host->ctx, model);
    svc_move(s, e, c, false);
  }
  s->game.host->set_solid(s->game.host->ctx, e->actor, solid, has_model ? &model : NULL);
  svc_link(s, e);
}

static void svc_motion(void *ctx, qa_q2_entity *e, const char *kind) {
  qa_q2_entity_services *s = ctx;
  e->motion = kind;
  qa_q2_motion m = {0};
  m.actor = e->actor; m.kind = kind; m.velocity = svc_body(s, e->actor).velocity;
  m.angular_velocity = e->angular_velocity; m.gravity = e->gravity; m.gravity_vector = e->gravity_vector;
  m.clip_mask = e->clip_mask; m.has_owner = e->has_owner; m.owner = e->owner;
  s->game.host->set_motion(s->game.host->ctx, m);
}

static void svc_schedule(void *ctx, qa_q2_entity *e, double delay, qa_q2_think think) {
  qa_q2_entity_services *s = ctx;
  e->think = think;
  double now = s->game.host->now(s->game.host->ctx);
  e->has_next_think = true;
  e->next_think = s->game.options.edition == QA_Q2_EDITION_RERELEASE
      ? (round(now * 1000) + round(delay * 1000)) / 1000 : now + delay;
  s->game.host->schedule(s->game.host->ctx, e->actor, &e->next_think);
}

static void svc_cancel(void *ctx, qa_q2_entity *e) {
  qa_q2_entity_services *s = ctx;
  e->has_next_think = false;
  e->think = (qa_q2_think){0};
  s->game.host->schedule(s->game.host->ctx, e->actor, NULL);
}

static int cmp_entity_slot(const void *a, const void *b, void *ctx) {
  qa_q2_entity_services *s = ctx;
  qa_q2_entity *ea = *(qa_q2_entity *const *)a, *eb = *(qa_q2_entity *const *)b;
  double d = source_slot_of(s, ea->actor.id) - source_slot_of(s, eb->actor.id);
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static qa_q2_entity *const *svc_targets(void *ctx, const char *name, size_t *n) {
  qa_q2_entity_services *s = ctx;
  *n = 0;
  if (!name || name[0] == '\0') return NULL;
  size_t count = 0;
  for (size_t i = 0; i < s->entity_count; i++)
    if (strcmp(s->entities[i]->targetname, name) == 0) {
      if (count == s->target_cap) { s->target_cap = s->target_cap ? s->target_cap * 2 : 8; s->target_scratch = xrealloc(s->target_scratch, s->target_cap * sizeof *s->target_scratch); }
      s->target_scratch[count++] = s->entities[i];
    }
  /* stable sort by slot (insertion sort preserves TS Map order ties) */
  for (size_t i = 1; i < count; i++) {
    qa_q2_entity *key = s->target_scratch[i];
    size_t j = i;
    while (j > 0 && cmp_entity_slot(&s->target_scratch[j - 1], &key, s) > 0) { s->target_scratch[j] = s->target_scratch[j - 1]; j--; }
    s->target_scratch[j] = key;
  }
  *n = count;
  return s->target_scratch;
}

static qa_q2_entity *svc_pick_target(void *ctx, const char *name) {
  qa_q2_entity_services *s = ctx;
  size_t n = 0;
  qa_q2_entity *const *t = svc_targets(s, name, &n);
  if (n > 8) n = 8;
  if (n == 0) return NULL;
  size_t idx = (size_t)floor(s->game.host->random(s->game.host->ctx) * (double)n);
  return idx < n ? t[idx] : NULL;
}

/* targetActors: entities + authored, as authored views sorted by slot. */
typedef struct target_view { qa_authored_target view; qa_q2_entity *entity; } target_view;
static size_t target_actors(qa_q2_entity_services *s, const char *name, target_view **out) {
  if (!name || name[0] == '\0') { *out = NULL; return 0; }
  size_t count = 0, cap = 0;
  target_view *list = NULL;
  for (size_t i = 0; i < s->entity_count; i++) {
    qa_q2_entity *e = s->entities[i];
    if (strcmp(e->targetname, name) == 0 && qa_session_actor_registry_is_live(s->game.host->actors, e->actor.id)) {
      if (count == cap) { cap = cap ? cap * 2 : 8; list = xrealloc(list, cap * sizeof *list); }
      list[count++] = (target_view){qa_q2_entity_authored_target(e), e};
    }
  }
  for (size_t i = 0; i < s->authored_count; i++) {
    qa_authored_target *a = s->authored[i];
    if (a->targetname && strcmp(a->targetname, name) == 0 && qa_session_actor_registry_is_live(s->game.host->actors, a->actor.id)) {
      if (count == cap) { cap = cap ? cap * 2 : 8; list = xrealloc(list, cap * sizeof *list); }
      list[count++] = (target_view){*a, NULL};
    }
  }
  for (size_t i = 1; i < count; i++) {
    target_view key = list[i];
    size_t j = i;
    while (j > 0 && source_slot_of(s, list[j - 1].view.actor.id) > source_slot_of(s, key.view.actor.id)) { list[j] = list[j - 1]; j--; }
    list[j] = key;
  }
  *out = list;
  return count;
}

static void svc_use_targets(void *ctx, qa_authored_target entity, const qa_actor_id *activator, bool ignore_delay) {
  qa_q2_entity_services *s = ctx;
  if (entity.delay != 0 && !ignore_delay) {
    qa_q2_entity *delayed = svc_create(s, "DelayedUse", NULL, 0);
    delayed->has_activator = activator != NULL;
    if (activator) delayed->activator = *activator;
    delayed->message = entity.message ? entity.message : "";
    delayed->target = entity.target ? entity.target : "";
    delayed->killtarget = entity.killtarget ? entity.killtarget : "";
    svc_schedule(s, delayed, entity.delay, (qa_q2_think){NULL, delayed_use_fn});
    return;
  }
  const char *message = entity.message ? entity.message : "";
  if (message[0] && activator && !s->game.host->is_monster(s->game.host->ctx, *activator)) {
    qa_q2_presentation_event ev = {0};
    ev.kind = "centerprint"; ev.has_actor = true; ev.actor = *activator; ev.text = message;
    s->game.host->emit(s->game.host->ctx, ev);
    qa_body_state_result body = qa_shared_body_table_read(s->game.host->bodies, *activator);
    if (body.has_value) {
      qa_q2_presentation_event sev = {0};
      sev.kind = "sound"; sev.has_actor = true; sev.actor = *activator; sev.origin = body.value.origin;
      sev.path = "misc/talk1.wav"; sev.channel = 0; sev.volume = 1; sev.attenuation = 1; sev.loop = "once";
      s->game.host->emit(s->game.host->ctx, sev);
    }
  }
  target_view *kills = NULL;
  size_t kill_count = target_actors(s, entity.killtarget ? entity.killtarget : "", &kills);
  for (size_t i = 0; i < kill_count; i++) {
    qa_q2_entity *native = svc_entity(s, &kills[i].view.actor.id);
    if (!native) {
      if (qa_session_actor_registry_is_live(s->game.host->actors, kills[i].view.actor.id))
        qa_session_actor_registry_release(s->game.host->actors, kills[i].view.actor);
    } else svc_remove(s, native);
    if (!qa_session_actor_registry_is_live(s->game.host->actors, entity.actor.id)) { free(kills); return; }
  }
  free(kills);
  double after = -1;
  for (;;) {
    target_view *list = NULL;
    size_t n = target_actors(s, entity.target ? entity.target : "", &list);
    target_view *target = NULL;
    for (size_t i = 0; i < n; i++)
      if (source_slot_of(s, list[i].view.actor.id) > after) { target = &list[i]; break; }
    if (!target) { free(list); break; }
    target_view chosen = *target;
    after = source_slot_of(s, chosen.view.actor.id);
    free(list);
    if (same_id(chosen.view.actor.id, entity.actor.id))
      s->game.host->diagnostic(s->game.host->ctx, fmt(&s->self_buf, "Q2 %s targets itself", entity.classname ? entity.classname : ""));
    else if (!(chosen.view.classname && strcmp(chosen.view.classname, "func_areaportal") == 0 &&
               entity.classname && (strcmp(entity.classname, "func_door") == 0 || strcmp(entity.classname, "func_door_rotating") == 0))) {
      qa_actor_id other = entity.actor.id;
      qa_actor_callback_table_use(s->game.host->callbacks, chosen.view.actor, &other, activator);
    }
    if (!qa_session_actor_registry_is_live(s->game.host->actors, entity.actor.id)) break;
  }
}

static qa_q2_monster_target_result svc_monster_target(void *ctx, const qa_actor_id *actor) {
  qa_q2_entity_services *s = ctx;
  qa_q2_monster_target_result out = {0};
  if (!actor) return out;
  if (s->game.host->monster_target) return s->game.host->monster_target(s->game.host->ctx, *actor);
  qa_q2_entity *e = svc_entity(s, actor);
  if (!e) return out;
  out.has_value = true;
  out.value.view_height = e->view_height;
  out.value.notarget = ((int64_t)e->flags & (32 | (s->game.options.edition == QA_Q2_EDITION_RERELEASE ? 0x1008000 : 0))) != 0;
  out.value.invisible = false;
  out.value.has_light_level = true; out.value.light_level = e->light_level;
  out.value.has_hostile_until = false;
  return out;
}

static void svc_show(void *ctx, qa_q2_entity *e) {
  qa_q2_entity_services *s = ctx;
  qa_q2_presentation_event ev = {0};
  ev.kind = "model"; ev.has_actor = true; ev.actor = e->actor.id; ev.path = e->model;
  ev.attached_models[0] = e->model2; ev.attached_models[1] = e->model3; ev.attached_models[2] = e->model4;
  ev.frame = e->frame; ev.old_frame = e->old_frame; ev.scale = e->scale; ev.alpha = e->alpha;
  ev.skin = e->skin; ev.effects = e->effects; ev.render_flags = e->render_flags;
  s->game.host->emit(s->game.host->ctx, ev);
  qa_q2_presentation_event vev = {0};
  vev.kind = "visibility"; vev.has_actor = true; vev.actor = e->actor.id; vev.visible = e->visible;
  s->game.host->emit(s->game.host->ctx, vev);
}

static void svc_sound(void *ctx, qa_owned_actor actor, const char *path, double channel, double volume, double attenuation) {
  qa_q2_entity_services *s = ctx;
  qa_q2_presentation_event ev = {0};
  ev.kind = "sound"; ev.has_actor = true; ev.actor = actor.id; ev.origin = svc_body(s, actor).origin;
  ev.path = path; ev.channel = channel; ev.volume = volume; ev.attenuation = attenuation; ev.loop = "once";
  s->game.host->emit(s->game.host->ctx, ev);
}

static qa_attack_provenance svc_attack(void *ctx, qa_q2_entity *inf_e, qa_actor_id inf_id, const qa_actor_id *attacker, double mod, double flags, qa_item_id weapon) {
  qa_q2_entity_services *s = ctx;
  qa_attack_provenance a = {0};
  a.sequence = s->sequence++;
  a.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, s->game.host->now(s->game.host->ctx)};
  a.attacker = attacker;
  /* inflictor pointer: use retained scratch in services */
  qa_actor_id inf = inf_e ? inf_e->actor.id : inf_id;
  if (s->attack_count == s->attack_cap) { /* small ring of scratch ids/attacks, retained */
    s->attack_cap = s->attack_cap ? s->attack_cap * 2 : 16;
    s->attack_ids = xrealloc(s->attack_ids, s->attack_cap * 2 * sizeof *s->attack_ids);
    s->attacks = xrealloc(s->attacks, s->attack_cap * sizeof *s->attacks);
  }
  size_t idx = s->attack_count++;
  s->attack_ids[idx * 2] = inf;
  a.inflictor = &s->attack_ids[idx * 2];
  a.weapon = weapon;
  a.weapon_provider = s->game.options.provider;
  a.combat_provider = s->game.options.combat_provider;
  a.inventory_provider = s->game.options.inventory_provider;
  a.movement_provider = s->game.options.movement_provider;
  a.has_damage_powerup_owner = true;
  a.damage_powerup_owner = s->game.options.has_damage_powerup_owner ? s->game.options.damage_powerup_owner : s->game.options.provider;
  a.cause = (qa_attack_cause){.kind = QA_ATTACK_CAUSE_Q2, .means_of_death = mod, .damage_flags = flags};
  s->attacks[idx] = a;
  return a;
}

static bool is_live_cb(void *ctx, qa_actor_id actor) {
  return qa_session_actor_registry_is_live(((qa_q2_entity_services *)ctx)->game.host->actors, actor);
}

static qa_damage_outcome svc_damage(void *ctx, qa_actor_id target, qa_q2_entity *inf_e, qa_actor_id inf_id, const qa_actor_id *attacker,
    double amount, double knockback, qa_vec3 dir, qa_vec3 point, qa_vec3 normal, double mod, double flags, qa_item_id weapon) {
  qa_q2_entity_services *s = ctx;
  qa_damage_request req = {0};
  req.attack = svc_attack(s, inf_e, inf_id, attacker, mod, flags, weapon);
  req.target = target; req.amount = amount; req.knockback = knockback;
  req.direction = dir; req.point = point; req.normal = normal; req.delivery = "direct";
  return qa_gameplay_authority_apply(s->game.host->combat,
      qa_apply_source_damage_modifier(req, s->game.options.source_damage_modifier, is_live_cb, s), NULL, NULL);
}

static bool svc_can_damage(void *ctx, qa_actor_id target, qa_owned_actor inflictor) {
  qa_q2_entity_services *s = ctx;
  qa_body_state_result br = qa_shared_body_table_read(s->game.host->bodies, target);
  if (!br.has_value) return false;
  qa_vec3 source = svc_body(s, inflictor).origin;
  qa_q2_entity *entity = svc_entity(s, &target);
  bool push = entity && strcmp(entity->motion, "push") == 0;
  qa_vec3 dest = push ? qa_q2_add(br.value.origin, qa_q2_scale(qa_q2_add(br.value.bounds.min, br.value.bounds.max), 0.5)) : br.value.origin;
  qa_q2_trace_request tr = {.start = source, .end = dest, .bounds = NULL, .ignore = &inflictor.id, .mask = MASK_SOLID};
  qa_trace_result trace = s->game.host->trace(s->game.host->ctx, tr);
  if (trace.fraction == 1 || (trace.hit.kind == QA_TRACE_HIT_ACTOR && same_id(trace.hit.actor, target))) return true;
  if (push) return false;
  static const qa_vec3 offs[] = {{15, 15, 0}, {15, -15, 0}, {-15, 15, 0}, {-15, -15, 0}};
  for (size_t i = 0; i < 4; i++) {
    tr.end = qa_q2_add(dest, offs[i]);
    if (s->game.host->trace(s->game.host->ctx, tr).fraction == 1) return true;
  }
  return false;
}

static void svc_radius_damage(void *ctx, qa_q2_entity *inflictor, const qa_actor_id *attacker, double damage,
    const qa_actor_id *ignore, double radius, double mod, double damage_flags, qa_item_id weapon) {
  qa_q2_entity_services *s = ctx;
  qa_vec3 origin = svc_body(s, inflictor->actor).origin;
  size_t n = 0;
  const qa_actor_id *near = s->game.host->nearby(s->game.host->ctx, origin, radius, &n);
  /* copy: callbacks may invalidate host scratch */
  qa_actor_id *targets = NULL;
  if (n) { targets = xrealloc(NULL, n * sizeof *targets); memcpy(targets, near, n * sizeof *targets); }
  for (size_t i = 0; i < n; i++) {
    qa_actor_id target = targets[i];
    if (ignore && same_id(*ignore, target)) continue;
    qa_combat_state_result state = qa_gameplay_authority_read(s->game.host->combat, target);
    qa_body_state_result body = qa_shared_body_table_read(s->game.host->bodies, target);
    if (!state.has_value || !state.value.can_take_damage || !body.has_value) continue;
    qa_vec3 center = qa_q2_add(body.value.origin, qa_q2_scale(qa_q2_add(body.value.bounds.min, body.value.bounds.max), 0.5));
    double points = damage - 0.5 * qa_q2_length(qa_q2_subtract(center, origin));
    if (attacker && same_id(target, *attacker)) points *= 0.5;
    if (points <= 0 || !svc_can_damage(s, target, inflictor->actor)) continue;
    qa_damage_request req = {0};
    req.attack = svc_attack(s, inflictor, inflictor->actor.id, attacker, mod, (double)((int64_t)damage_flags | 1), weapon);
    req.target = target; req.amount = trunc(points); req.knockback = trunc(points);
    req.direction = qa_q2_subtract(body.value.origin, origin); req.point = origin; req.normal = QA_Q2_ZERO; req.delivery = "radius";
    qa_gameplay_authority_apply(s->game.host->combat,
        qa_apply_source_damage_modifier(req, s->game.options.source_damage_modifier, is_live_cb, s), NULL, NULL);
  }
  free(targets);
}

static void svc_project_trajectory(void *ctx, qa_q2_entity *e, qa_weapon_trajectory_update u) {
  qa_q2_entity_services *s = ctx;
  qa_q2_source_callbacks_project_trajectory(s->callbacks, e, u);
  qa_q2_body_changes c = {0};
  c.has_origin = true; c.origin = u.origin;
  c.has_velocity = true; c.velocity = u.velocity;
  c.has_angles = true; c.angles = u.angles;
  svc_move(s, e, c, true);
  svc_motion(s, e, e->motion);
}

void qa_q2_entity_services_apply_projectile_behavior(qa_q2_entity_services *s, qa_actor_id actor, double seconds) {
  qa_q2_entity *e = svc_entity(s, &actor);
  if (!e || qa_shared_body_table_attachment(s->game.host->bodies, actor).has_value) return;
  if (!s->game.host->weapon_behavior) return;
  const qa_weapon_behavior_projectile_port *port = s->game.host->weapon_behavior;
  qa_weapon_trajectory_update_result update = port->step(port->ctx, e->actor, svc_body(s, e->actor), seconds);
  if (update.has_value) svc_project_trajectory(s, e, update.value);
}

typedef struct pre_ctx { qa_q2_entity *e; qa_q2_game_services *g; qa_q2_think cb; } pre_ctx;
static void run_pre(void *c) { pre_ctx *p = c; if (p->cb.fn) p->cb.fn(p->cb.ctx, p->e, p->g); }
void qa_q2_entity_services_pre_physics(qa_q2_entity_services *s, qa_actor_id actor) {
  qa_q2_entity *e = svc_entity(s, &actor);
  if (!e) return;
  pre_ctx p = {e, &s->game, e->prethink};
  invoke(s, actor, run_pre, &p);
}
void qa_q2_entity_services_post_physics(qa_q2_entity_services *s, qa_actor_id actor) {
  qa_q2_entity *e = svc_entity(s, &actor);
  if (!e) return;
  pre_ctx p = {e, &s->game, e->postthink};
  invoke(s, actor, run_pre, &p);
}

void qa_q2_entity_services_run_actor(qa_q2_entity_services *s, qa_actor_id actor, void (*cb)(void *), void *ctx) {
  invoke(s, actor, cb, ctx);
}

/* ---- capture / restore ---- */
static qa_q2_saved_link save_link(bool has, qa_actor_id id) {
  qa_q2_saved_link out = {0};
  if (has) out.has = qa_q2_save_actor(&id, &out.v);
  return out;
}

qa_q2_foundation_checkpoint qa_q2_entity_services_capture(qa_q2_entity_services *s) {
  if (s->game.has_current_actor) qa_range_error("Q2 source saves require a completed callback boundary");
  for (size_t i = 0; i < s->cp_count; i++) { qa_q2_spawn_fields_free(&s->cp_entities[i].spawn); qa_q2_entity_destroy(&s->cp_entities[i].values); }
  free(s->cp_entities); free(s->cp_freed);
  s->cp_entities = NULL; s->cp_count = 0;
  s->cp_entities = xrealloc(NULL, (s->entity_count ? s->entity_count : 1) * sizeof *s->cp_entities);
  for (size_t i = 0; i < s->entity_count; i++) {
    qa_q2_entity *e = s->entities[i];
    qa_q2_entity_checkpoint *cp = &s->cp_entities[s->cp_count];
    *cp = (qa_q2_entity_checkpoint){0};
    cp->actor = (qa_saved_actor_id){(double)e->actor.id.slot, (double)e->actor.id.generation};
    cp->has_source_slot = slot_get(s, e->actor.id, &cp->source_slot);
    if (!qa_q2_spawn_fields_clone(&cp->spawn, &e->spawn)) qa_range_error("out of memory");
    /* values: shallow copy minus spawn (strings borrowed from live entity) */
    cp->values = *e;
    cp->values.spawn = (qa_q2_spawn_fields){0};
    cp->activator = save_link(e->has_activator, e->activator);
    cp->enemy = save_link(e->has_enemy, e->enemy);
    cp->owner = save_link(e->has_owner, e->owner);
    cp->goal = save_link(e->has_goal, e->goal);
    cp->team_master = save_link(e->has_team_master, e->team_master);
    cp->team_chain = save_link(e->has_team_chain, e->team_chain);
    cp->chain = save_link(e->has_chain, e->chain);
    cp->beam = save_link(e->has_beam, e->beam);
    cp->beam2 = save_link(e->has_beam2, e->beam2);
    cp->proboscus = save_link(e->has_proboscus, e->proboscus);
    cp->has_last_attack = e->last_attack != NULL;
    if (e->last_attack) cp->last_attack = qa_q2_save_attack(*e->last_attack);
    cp->think = qa_q2_think_name(s->callbacks, e->think, e->think.fn != NULL);
    cp->prethink = qa_q2_think_name(s->callbacks, e->prethink, e->prethink.fn != NULL);
    cp->postthink = qa_q2_think_name(s->callbacks, e->postthink, e->postthink.fn != NULL);
    cp->use = qa_q2_use_name(s->callbacks, e->use, e->use.fn != NULL);
    cp->touch = qa_q2_touch_name(s->callbacks, e->touch, e->touch.fn != NULL);
    cp->pain = qa_q2_pain_name(s->callbacks, e->pain, e->pain.fn != NULL);
    cp->die = qa_q2_die_name(s->callbacks, e->die, e->die.fn != NULL);
    cp->blocked = qa_q2_blocked_name(s->callbacks, e->blocked, e->blocked.fn != NULL);
    s->cp_count++;
  }
  s->cp_freed = xrealloc(NULL, (s->freed_count ? s->freed_count : 1) * sizeof *s->cp_freed);
  for (size_t i = 0; i < s->freed_count; i++) s->cp_freed[i] = (qa_q2_freed_slot){s->freed[i].slot, s->freed[i].time};
  return (qa_q2_foundation_checkpoint){.version = 1, .next_source_slot = s->next_source_slot, .sequence = s->sequence,
      .freed_slots = s->cp_freed, .freed_slot_count = s->freed_count, .counters = s->counters,
      .entities = s->cp_entities, .entity_count = s->cp_count};
}

typedef struct ref_ctx { qa_q2_entity_services *s; } ref_ctx;
static qa_actor_id ref_saved(void *c, qa_saved_actor_id saved) {
  ref_ctx *r = c;
  return qa_session_actor_registry_reference_saved(r->s->game.host->actors, saved, "checkpoint");
}
static bool link_ref(qa_q2_entity_services *s, qa_q2_saved_link link, qa_actor_id *out) {
  if (!link.has) return false;
  *out = qa_session_actor_registry_reference_saved(s->game.host->actors, link.v, "checkpoint");
  return true;
}

void qa_q2_entity_services_restore(qa_q2_entity_services *s, qa_q2_foundation_checkpoint cp) {
  if (s->game.has_current_actor) qa_range_error("Q2 source restore requires a completed callback boundary");
  for (size_t i = 0; i < s->entity_count; i++) { qa_q2_entity_destroy(s->entities[i]); free(s->entities[i]); }
  s->entity_count = 0; s->slot_count = 0; s->freed_count = 0; s->unsupported_count = 0;
  s->next_source_slot = cp.next_source_slot; s->sequence = cp.sequence;
  s->counters = cp.counters;
  for (size_t i = 0; i < cp.freed_slot_count; i++) {
    if (s->freed_count == s->freed_cap) { s->freed_cap = s->freed_cap ? s->freed_cap * 2 : 16; s->freed = xrealloc(s->freed, s->freed_cap * sizeof *s->freed); }
    s->freed[s->freed_count++] = (freed_entry){cp.freed_slots[i].slot, cp.freed_slots[i].time};
  }
  for (size_t i = 0; i < cp.entity_count; i++) {
    const qa_q2_entity_checkpoint *saved = &cp.entities[i];
    qa_owned_actor actor = qa_q2_restore_actor(&s->game, saved->actor);
    if (saved->has_source_slot) {
      qa_actor_source_result src = qa_session_actor_registry_source_of(s->game.host->actors, actor.id);
      if (!src.has_value || strcmp(src.provider, s->game.options.provider) != 0 || (double)src.slot != saved->source_slot)
        qa_range_error("Q2 checkpoint source slot disagrees with the shared registry");
    }
    qa_q2_spawn_fields fields;
    if (!qa_q2_spawn_fields_clone(&fields, &saved->spawn)) qa_range_error("out of memory");
    qa_q2_entity *e = calloc(1, sizeof *e);
    if (!e) qa_range_error("out of memory");
    qa_q2_entity_init(e, actor, fields);
    /* Object.assign(entity, values): overwrite value fields, keep actor/spawn. */
    qa_q2_spawn_fields keep_spawn = e->spawn;
    qa_owned_actor keep_actor = e->actor;
    *e = saved->values;
    e->spawn = keep_spawn; e->actor = keep_actor;
    e->has_activator = link_ref(s, saved->activator, &e->activator);
    e->has_enemy = link_ref(s, saved->enemy, &e->enemy);
    e->has_owner = link_ref(s, saved->owner, &e->owner);
    e->has_goal = link_ref(s, saved->goal, &e->goal);
    e->has_team_master = link_ref(s, saved->team_master, &e->team_master);
    e->has_team_chain = link_ref(s, saved->team_chain, &e->team_chain);
    e->has_chain = link_ref(s, saved->chain, &e->chain);
    e->has_beam = link_ref(s, saved->beam, &e->beam);
    e->has_beam2 = link_ref(s, saved->beam2, &e->beam2);
    e->has_proboscus = link_ref(s, saved->proboscus, &e->proboscus);
    if (saved->has_last_attack) {
      if (s->attack_count == s->attack_cap) {
        s->attack_cap = s->attack_cap ? s->attack_cap * 2 : 16;
        s->attack_ids = xrealloc(s->attack_ids, s->attack_cap * 2 * sizeof *s->attack_ids);
        s->attacks = xrealloc(s->attacks, s->attack_cap * sizeof *s->attacks);
      }
      size_t idx = s->attack_count++;
      ref_ctx rc = {s};
      s->attacks[idx] = qa_q2_restore_attack(saved->last_attack, ref_saved, &rc, &s->attack_ids[idx * 2], &s->attack_ids[idx * 2 + 1]);
      e->last_attack = &s->attacks[idx];
    } else e->last_attack = NULL;
    bool has;
    e->think = qa_q2_think_resolve(s->callbacks, saved->think, &has);
    e->prethink = qa_q2_think_resolve(s->callbacks, saved->prethink, &has);
    e->postthink = qa_q2_think_resolve(s->callbacks, saved->postthink, &has);
    e->use = qa_q2_use_resolve(s->callbacks, saved->use, &has);
    e->touch = qa_q2_touch_resolve(s->callbacks, saved->touch, &has);
    e->pain = qa_q2_pain_resolve(s->callbacks, saved->pain, &has);
    e->die = qa_q2_die_resolve(s->callbacks, saved->die, &has);
    e->blocked = qa_q2_blocked_resolve(s->callbacks, saved->blocked, &has);
    push_entity(s, e);
    if (saved->has_source_slot) slot_set(s, actor.id, saved->source_slot);
    bind_callbacks(s, e);
    if (s->game.host->register_entity) s->game.host->register_entity(s->game.host->ctx, e, s);
  }
}

/* runtime support (runtime.c uses these) */
qa_q2_entity *const *qa_q2_entity_services_entities(qa_q2_entity_services *s, size_t *n) { return svc_entities(s, n); }
qa_authored_target *const *qa_q2_entity_services_authored_targets(qa_q2_entity_services *s, size_t *n) { *n = s->authored_count; return s->authored; }
void qa_q2_entity_services_add_authored_target(qa_q2_entity_services *s, qa_authored_target *t) {
  if (s->authored_count == s->authored_cap) { s->authored_cap = s->authored_cap ? s->authored_cap * 2 : 8; s->authored = xrealloc(s->authored, s->authored_cap * sizeof *s->authored); }
  s->authored[s->authored_count++] = t;
}
qa_q2_source_callbacks *qa_q2_entity_services_callbacks(qa_q2_entity_services *s) { return s->callbacks; }
qa_q2_counters *qa_q2_entity_services_counters(qa_q2_entity_services *s) { return &s->counters; }
double qa_q2_entity_services_source_slot(qa_q2_entity_services *s, qa_actor_id actor, bool *has) {
  double v = 0;
  *has = slot_get(s, actor, &v);
  return v;
}
qa_q2_entity *const *qa_q2_entity_services_unsupported(qa_q2_entity_services *s, size_t *n) { *n = s->unsupported_count; return s->unsupported; }
qa_q2_game_services *qa_q2_entity_services_game(qa_q2_entity_services *s) { return &s->game; }

qa_q2_entity_services *qa_q2_entity_services_create(qa_q2_foundation_host *host, qa_q2_game_options options, const qa_q2_spawn_module *modules, size_t module_count) {
  qa_q2_entity_services *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->callbacks = qa_q2_source_callbacks_create();
  if (!s->callbacks) { free(s); return NULL; }
  s->modules = modules; s->module_count = module_count;
  s->next_source_slot = options.max_clients + 1;
  s->game = (qa_q2_game_services){
    .ctx = s, .host = host, .source_callbacks = s->callbacks, .options = options, .counters = &s->counters,
    .entities = svc_entities, .spawn = svc_spawn, .create = svc_create, .remove = svc_remove,
    .entity = svc_entity, .weapon_target = svc_weapon_target, .item_name = svc_item_name,
    .push_team = svc_push_team, .body = svc_body, .move = svc_move, .project_trajectory = svc_project_trajectory,
    .link = svc_link, .solid = svc_solid, .motion = svc_motion, .schedule = svc_schedule, .cancel = svc_cancel,
    .use_targets = svc_use_targets, .monster_target = svc_monster_target, .targets = svc_targets,
    .pick_target = svc_pick_target, .show = svc_show, .sound = svc_sound, .damage = svc_damage,
    .radius_damage = svc_radius_damage, .can_damage = svc_can_damage, .attack = svc_attack};
  qa_q2_named_think thinks[] = {{"Think_Delay", {NULL, delayed_use_fn}}, {"G_FreeEdict", QA_Q2_FREE_ENTITY}};
  qa_q2_named_die dies[] = {{"G_FreeEdict", QA_Q2_FREE_ENTITY_DIE}};
  qa_q2_callback_definitions base = {0};
  base.think = thinks; base.think_count = 2; base.die = dies; base.die_count = 1;
  qa_q2_source_callbacks_register(s->callbacks, base);
  for (size_t i = 0; i < module_count; i++)
    if (modules[i].callbacks) qa_q2_source_callbacks_register(s->callbacks, *modules[i].callbacks);
  qa_session_actor_registry_on_release(host->actors, on_release, s);
  return s;
}

void qa_q2_entity_services_destroy(qa_q2_entity_services *s) {
  if (!s) return;
  for (size_t i = 0; i < s->entity_count; i++) { qa_q2_entity_destroy(s->entities[i]); free(s->entities[i]); }
  for (size_t i = 0; i < s->cp_count; i++) { qa_q2_spawn_fields_free(&s->cp_entities[i].spawn); qa_q2_entity_destroy(&s->cp_entities[i].values); }
  for (size_t i = 0; i < s->def_count; i++) free(s->def_strings[i]);
  free(s->entities); free(s->authored); free(s->slots); free(s->freed); free(s->unsupported);
  free(s->team_scratch); free(s->target_scratch); free(s->cp_entities); free(s->cp_freed);
  free(s->attacks); free(s->attack_ids); free(s->def_strings);
  free(s->diag_buf); free(s->self_buf); free(s->count_buf);
  qa_q2_source_callbacks_destroy(s->callbacks);
  free(s);
}
