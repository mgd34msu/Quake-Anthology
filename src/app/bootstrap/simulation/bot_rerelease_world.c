/* Port of src/app/bootstrap/simulation/bot-rerelease-world.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_rerelease_world.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool starts(const char *a, const char *prefix) { return a != NULL && strncmp(a, prefix, strlen(prefix)) == 0; }
static noreturn void fail(const char *message) { qa_raise("Error", 0, 0, "%s", message); }

/* name.replace(/^.*:/, "").replace(/^.*weapon[/_]/, "").replaceAll("_", "").replaceAll("/", "") */
static void normalized(const char *name, char *out, size_t capacity) {
  const char *s = name != NULL ? name : "";
  const char *colon = strrchr(s, ':');
  if (colon != NULL) s = colon + 1;
  const char *cut = NULL;
  for (const char *p = s; (p = strstr(p, "weapon")) != NULL; p++)
    if (p[6] == '/' || p[6] == '_') cut = p + 7;
  if (cut != NULL) s = cut;
  size_t n = 0;
  for (; *s != '\0' && n + 1 < capacity; s++) if (*s != '_' && *s != '/') out[n++] = *s;
  out[n] = '\0';
}
static bool same_normalized(const char *a, const char *b) {
  char left[256], right[256];
  normalized(a, left, sizeof left); normalized(b, right, sizeof right);
  return strcmp(left, right) == 0;
}
static const qa_bot_weapon_rec *weapon_list(const qa_bot_knowledge *k, size_t *out_count) {
  if (k->replaced != NULL) { *out_count = k->replaced_count; return k->replaced; }
  *out_count = k->weapon_count; return k->weapons;
}
qa_item_id qa_app_sim_native_weapon_item(const qa_app_sim_bot_simulation *simulation, qa_actor_id actor,
    const qa_bot_knowledge *knowledge, double number) {
  const qa_bot_weapon_rec *weapon = qa_bot_knowledge_weapon_by_number(knowledge, number);
  if (weapon == NULL) return NULL;
  qa_app_sim_player_ui ui = simulation->player_ui(simulation->ctx, actor);
  for (size_t i = 0; i < ui.item_count; i++)
    if (is(ui.items[i].kind, "weapon") && same_normalized(ui.items[i].id, weapon->entry->name)) return ui.items[i].id;
  return NULL;
}

static qa_app_sim_movement_player *player(qa_app_sim_rerelease_bot_world *w) {
  const qa_app_sim_bot_simulation *sim = w->options.simulation;
  qa_app_sim_movement_player *value = sim->movement_player(sim->ctx, w->actor);
  if (value == NULL) fail("Native bot lost shared player");
  return value;
}
static bool nav_ground(void *ctx, qa_actor_id *out) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  qa_body_state_result body = qa_shared_body_table_read(w->options.simulation->bodies, w->actor);
  if (!body.has_value || body.value.ground == NULL) return false;
  *out = *body.value.ground; return true;
}
static double world_time(void *ctx) { qa_app_sim_rerelease_bot_world *w = ctx; return w->options.simulation->time_seconds(w->options.simulation->ctx); }
static double world_frame_time(void *ctx) { qa_app_sim_rerelease_bot_world *w = ctx; return w->options.elapsed(w->options.ctx) / 1000; }
static void ammo_set(qa_app_sim_rerelease_bot_world *w, const char *name, double value) {
  for (size_t i = 0; i < w->ammo_count; i++) if (is(w->ammo[i].name, name)) { w->ammo[i].value = value; return; }
  if (w->ammo_count == w->ammo_capacity) {
    w->ammo_capacity = w->ammo_capacity != 0 ? w->ammo_capacity * 2 : 16;
    qa_bot_ammo_entry *grown = realloc(w->ammo, w->ammo_capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    w->ammo = grown;
  }
  w->ammo[w->ammo_count].name = name; w->ammo[w->ammo_count].value = value; w->ammo_count++;
}
static qa_bot_self self_of(void *ctx) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  const qa_app_sim_bot_simulation *sim = w->options.simulation;
  qa_app_sim_movement_player *movement = player(w);
  qa_body_state_result body = qa_shared_body_table_read(sim->bodies, w->actor);
  qa_app_sim_player_ui ui = sim->player_ui(sim->ctx, w->actor);
  if (!body.has_value) fail("Native bot lost shared body");
  const char *ground_class = NULL;
  qa_app_sim_bot_entity ground_entity;
  if (body.value.ground != NULL && sim->bot_entity(sim->ctx, *body.value.ground, &ground_entity)) ground_class = ground_entity.classname;
  int32_t items = 0;
  int64_t current_weapon = 0;
  w->ammo_count = 0;
  size_t weapon_count = 0;
  const qa_bot_weapon_rec *weapons = weapon_list(w->options.knowledge, &weapon_count);
  for (size_t i = 0; i < weapon_count; i++) {
    const qa_weapon_entry *weapon = weapons[i].entry;
    qa_item_id item = qa_app_sim_native_weapon_item(sim, w->actor, w->options.knowledge, weapon->number);
    if (item != NULL && qa_shared_inventory_table_count(sim->inventory, w->actor, item) > 0) items = qa_js_to_int32((double)items) | qa_js_to_int32(weapon->number);
    /* item === ui.activeWeapon (null === null included) */
    if ((item == NULL && ui.active_weapon == NULL) || is(item, ui.active_weapon)) current_weapon = (int64_t)weapon->number;
    const char *ammo_name = weapon->ammo_name != NULL ? weapon->ammo_name : "";
    /* Selected arsenals carry ammo on the weapon entry (and in the shared
     * inventory under foreign ids), not in the source inventory projection,
     * which lists the map game's own empty ammo slots. */
    double match = 0;
    for (size_t e = 0; e < ui.inventory_count; e++)
      if (same_normalized(ui.inventory[e].item, ammo_name)) { match = ui.inventory[e].count; break; }
    double carried = 0;
    if (item != NULL)
      for (size_t e = 0; e < ui.item_count; e++)
        if (is(ui.items[e].id, item)) { carried = ui.items[e].has_count ? ui.items[e].count : 0; break; }
    if (ammo_name[0] != '\0') ammo_set(w, ammo_name, qa_js_max(match, carried));
  }
  qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, w->actor);
  qa_bot_self self;
  memset(&self, 0, sizeof self);
  self.id = w->options.identify(w->options.ctx, w->actor);
  self.origin = body.value.origin; self.velocity = body.value.velocity; self.view_angles = movement->view_angles;
  self.eye = body.value.origin; self.eye.z = body.value.origin.z + movement->view_height;
  self.health = ui.health;
  self.armor = ui.armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : ui.armor.regular.points;
  self.items = items; self.ammo = w->ammo; self.ammo_count = w->ammo_count; self.current_weapon = current_weapon;
  self.on_ground = body.value.ground != NULL;
  self.has_on_lift = true; self.on_lift = is(ground_class, "func_plat") || is(ground_class, "func_plat2") || is(ground_class, "func_train");
  self.water_level = (int64_t)movement->water_level;
  self.team = w->options.objectives->team(w->options.objectives->ctx, w->actor);
  self.dead = ui.health <= 0;
  self.has_protection = combat.has_value ? combat.value.invulnerable : false;
  self.has_carrying_objective = true;
  self.carrying_objective = w->options.objectives->carrying(w->options.objectives->ctx, w->actor);
  return self;
}
static qa_bot_trace trace(qa_app_sim_rerelease_bot_world *w, qa_vec3 start, qa_vec3 end, qa_trace_shape shape) {
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start; query.end = end; query.shape = shape;
  query.policy.kind = "q3"; query.policy.contents_mask = -1; query.policy.curves = true; query.policy.player_curve_clip = true;
  query.numeric = w->numeric; query.pass_actor = &w->actor;
  qa_trace_result result = qa_shared_trace(w->options.simulation->scene, query);
  qa_bot_trace out;
  out.fraction = result.fraction; out.endpos = result.end; out.startsolid = result.start_solid || result.all_solid;
  out.hit_id = result.hit.kind == QA_TRACE_HIT_ACTOR ? w->options.identify(w->options.ctx, result.hit.actor) : -1;
  return out;
}
static qa_bot_trace world_trace_line(void *ctx, qa_bot_vec3 start, qa_bot_vec3 end) {
  qa_trace_shape shape; memset(&shape, 0, sizeof shape); shape.kind = "point";
  return trace(ctx, start, end, shape);
}
static qa_bot_trace world_trace_box(void *ctx, qa_bot_vec3 start, qa_bot_vec3 mins, qa_bot_vec3 maxs, qa_bot_vec3 end) {
  qa_trace_shape shape; shape.kind = "box"; shape.bounds.min = mins; shape.bounds.max = maxs;
  return trace(ctx, start, end, shape);
}
static int64_t world_point_contents(void *ctx, qa_bot_vec3 point) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = point; /* target: world */
  query.policy.kind = "q3"; query.policy.contents_mask = -1; query.policy.curves = true; query.policy.player_curve_clip = true;
  query.numeric = w->numeric; query.pass_actor = &w->actor;
  qa_point_contents_result result = qa_shared_point_contents(w->options.simulation->scene, query);
  if (!is(result.kind, "q3")) fail("Native bot contents projection lost its selected policy");
  int32_t contents = qa_js_to_int32(result.contents);
  return (contents & 8) != 0 ? QA_BOT_CONTENTS_LAVA : (contents & 16) != 0 ? QA_BOT_CONTENTS_SLIME
      : (contents & 32) != 0 ? QA_BOT_CONTENTS_WATER : (contents & 1) != 0 ? QA_BOT_CONTENTS_SOLID : QA_BOT_CONTENTS_EMPTY;
}
static const char *keep_classname(qa_app_sim_rerelease_bot_world *w, const char *classname) {
  if (w->classname_count == w->classname_capacity) {
    w->classname_capacity = w->classname_capacity != 0 ? w->classname_capacity * 2 : 32;
    char **grown = realloc(w->classnames, w->classname_capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    w->classnames = grown;
  }
  char *copy = strdup(classname);
  if (copy == NULL) qa_range_error("Out of memory");
  w->classnames[w->classname_count++] = copy;
  return copy;
}
static const qa_bot_entity *build_entities(void *ctx, size_t *out_count) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  const qa_app_sim_bot_simulation *sim = w->options.simulation;
  for (size_t i = 0; i < w->classname_count; i++) free(w->classnames[i]);
  w->classname_count = 0; w->entity_count = 0;
  size_t observation_count = 0;
  const qa_actor_observation *observations = qa_session_actor_registry_observations(sim->actors, &observation_count);
  qa_actor_id *targets = calloc(observation_count != 0 ? observation_count : 1, sizeof *targets);
  if (targets == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < observation_count; i++) targets[i] = observations[i].id;
  for (size_t i = 0; i < observation_count; i++) {
    qa_actor_id target = targets[i];
    qa_body_state_result body = qa_shared_body_table_read(sim->bodies, target);
    if (!body.has_value || !qa_shared_body_table_linked(sim->bodies, target).has_value) continue;
    qa_app_sim_movement_player *movement = sim->movement_player(sim->ctx, target);
    qa_combat_state_result combat = qa_gameplay_authority_read(sim->combat, target);
    qa_app_sim_bot_entity entity;
    bool has_entity = sim->bot_entity(sim->ctx, target, &entity);
    if ((has_entity && entity.hidden) || (!has_entity && movement == NULL)) continue;
    const char *classname = movement != NULL ? "player" : has_entity && entity.classname != NULL ? entity.classname : "";
    int64_t kind = movement != NULL ? QA_BOT_ENTITY_PLAYER : starts(classname, "monster_") ? QA_BOT_ENTITY_MONSTER
        : qa_bot_knowledge_get_item(w->options.knowledge, classname) != NULL ? QA_BOT_ENTITY_ITEM : QA_BOT_ENTITY_INTERACTABLE;
    if (w->entity_count == w->entity_capacity) {
      w->entity_capacity = w->entity_capacity != 0 ? w->entity_capacity * 2 : 64;
      qa_bot_entity *grown = realloc(w->entities, w->entity_capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      w->entities = grown;
    }
    qa_bot_entity *e = &w->entities[w->entity_count++];
    memset(e, 0, sizeof *e);
    qa_body_state b = body.value;
    double health = combat.has_value ? combat.value.health : has_entity ? entity.health : 0;
    e->id = w->options.identify(w->options.ctx, target); e->kind = kind; e->classname = keep_classname(w, classname);
    e->origin = b.origin; e->velocity = b.velocity;
    e->center = (qa_vec3){b.origin.x + (b.bounds.min.x + b.bounds.max.x) / 2, b.origin.y + (b.bounds.min.y + b.bounds.max.y) / 2,
        b.origin.z + (b.bounds.min.z + b.bounds.max.z) / 2};
    e->head = b.origin; e->head.z = b.origin.z + b.bounds.max.z;
    e->feet = b.origin; e->feet.z = b.origin.z + b.bounds.min.z;
    e->health = health; e->team = w->options.objectives->team(w->options.objectives->ctx, target);
    e->dead = combat.has_value && combat.value.health <= 0; e->invisible = false;
    e->water_level = movement != NULL ? (int64_t)movement->water_level : 0;
    e->is_bot = w->options.is_bot(w->options.ctx, target);
    e->spawnflags = has_entity ? (int64_t)entity.spawnflags : 0;
    e->has_health = health > 0;
    e->has_targetname = has_entity && entity.targetname != NULL && entity.targetname[0] != '\0';
    e->has_carrying_objective = true;
    e->carrying_objective = w->options.objectives->carrying(w->options.objectives->ctx, target);
  }
  free(targets);
  *out_count = w->entity_count;
  return w->entities;
}
void qa_app_sim_rerelease_frame_snapshot_begin(qa_app_sim_rerelease_frame_snapshot *snapshot) {
  snapshot->open = true; snapshot->serial++; snapshot->has_list = false; snapshot->list = NULL; snapshot->list_count = 0;
}
void qa_app_sim_rerelease_frame_snapshot_end(qa_app_sim_rerelease_frame_snapshot *snapshot) {
  snapshot->open = false; snapshot->has_list = false; snapshot->list = NULL; snapshot->list_count = 0;
}
static qa_bot_self world_self(void *ctx) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  const qa_app_sim_rerelease_frame_snapshot *snapshot = w->options.snapshot;
  if (!snapshot->open) return self_of(w);
  if (w->has_remembered && w->remembered_frame == snapshot->serial) return w->remembered;
  qa_bot_self value = self_of(w);
  w->has_remembered = true; w->remembered_frame = snapshot->serial; w->remembered = value;
  return value;
}
static const qa_bot_entity *world_entities(void *ctx, size_t *out_count) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  qa_app_sim_rerelease_frame_snapshot *snapshot = w->options.snapshot;
  if (!snapshot->open) return build_entities(w, out_count);
  if (!snapshot->has_list) {
    snapshot->list = build_entities(w, &snapshot->list_count);
    snapshot->has_list = true;
  }
  *out_count = snapshot->list_count;
  return snapshot->list;
}
static const qa_bot_sound *world_hearing(void *ctx, size_t *out_count) {
  qa_app_sim_rerelease_bot_world *w = ctx;
  return w->options.sounds(w->options.ctx, out_count);
}
static qa_bot_navigation_iface *world_nav(void *ctx) { qa_app_sim_rerelease_bot_world *w = ctx; return &w->nav; }

qa_app_sim_rerelease_bot_world *qa_app_sim_create_rerelease_bot_world(const qa_app_sim_rerelease_world_options *options, qa_actor_id actor) {
  qa_app_sim_rerelease_bot_world *w = calloc(1, sizeof *w);
  if (w == NULL) return NULL;
  w->options = *options; w->actor = actor;
  if (!qa_app_sim_bot_numeric(options->simulation, &w->numeric)) { free(w); fail("Native bot world requires source numeric policy"); }
  qa_navigation_runtime *runtime = options->navigation->selected.for_client(options->navigation->selected.ctx, (int64_t)player(w)->client.slot);
  qa_source_rerelease_navigation_init(&w->nav_source, runtime, w, nav_ground);
  w->nav = qa_source_rerelease_navigation_as_iface(&w->nav_source);
  w->world.ctx = w;
  w->world.time = world_time; w->world.frame_time = world_frame_time; w->world.get_self = world_self;
  w->world.trace_line = world_trace_line; w->world.trace_box = world_trace_box;
  w->world.point_contents = world_point_contents; w->world.entities = world_entities;
  w->world.hearing = world_hearing; w->world.nav = world_nav;
  return w;
}
void qa_app_sim_rerelease_bot_world_destroy(qa_app_sim_rerelease_bot_world *w) {
  if (w == NULL) return;
  for (size_t i = 0; i < w->classname_count; i++) free(w->classnames[i]);
  free(w->classnames); free(w->entities); free(w->ammo); free(w);
}
