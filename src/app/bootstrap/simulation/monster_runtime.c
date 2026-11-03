/* Port of src/app/bootstrap/simulation/monster-runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/foundation/entity_services.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/save_image.h"
#include "qa/world/gameplay/authority.h"

const char *const QA_APP_SIM_Q1_ORDINARY_MONSTERS[] = {
  "monster_army_infected", "monster_knight_infected", "monster_enforcer_infected", "monster_hell_knight_infected",
  "monster_demodog", "monster_ranged_knight", "monster_ogre_rocket", "monster_army", "monster_dog", "monster_knight",
  "monster_enforcer", "monster_demon1", "monster_ogre", "monster_ogre_marksman", "monster_hell_knight", "monster_shambler",
  "monster_wizard", "monster_shalrath", "monster_tarbaby", "monster_fish", "monster_zombie", "monster_scourge",
  "monster_gremlin", "monster_eel", "monster_sword", "monster_wrath", "monster_mummy", "monster_lava_man"};
const size_t QA_APP_SIM_Q1_ORDINARY_MONSTER_COUNT = sizeof QA_APP_SIM_Q1_ORDINARY_MONSTERS / sizeof *QA_APP_SIM_Q1_ORDINARY_MONSTERS;
const char *const QA_APP_SIM_Q2_ORDINARY_MONSTERS[] = {
  "monster_soldier", "monster_soldier_light", "monster_soldier_ss", "monster_infantry", "monster_berserk",
  "monster_gladiator", "monster_gunner", "monster_parasite", "monster_flyer", "monster_floater", "monster_hover",
  "monster_mutant", "monster_chick", "monster_tank", "monster_tank_commander", "monster_flipper", "monster_brain",
  "monster_gekk", "monster_chick_heat", "monster_soldier_ripper", "monster_soldier_hypergun", "monster_soldier_lasergun",
  "monster_stalker", "monster_daedalus"};
const size_t QA_APP_SIM_Q2_ORDINARY_MONSTER_COUNT = sizeof QA_APP_SIM_Q2_ORDINARY_MONSTERS / sizeof *QA_APP_SIM_Q2_ORDINARY_MONSTERS;

static bool same(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool in_set(const char *const *set, size_t count, const char *value) {
  for (size_t i = 0; i < count; i++) if (same(set[i], value)) return true;
  return false;
}
bool qa_app_sim_q1_ordinary_monster(const char *classname) {
  return in_set(QA_APP_SIM_Q1_ORDINARY_MONSTERS, QA_APP_SIM_Q1_ORDINARY_MONSTER_COUNT, classname);
}
bool qa_app_sim_q2_ordinary_monster(const char *classname) {
  return in_set(QA_APP_SIM_Q2_ORDINARY_MONSTERS, QA_APP_SIM_Q2_ORDINARY_MONSTER_COUNT, classname);
}

typedef struct record record;
struct record {
  qa_authored_monster entry; /* first: &entry.base is the AuthoredTarget the map program holds */
  qa_app_sim_selected_monsters *owner;
  qa_monster_definition_reference definition;
  qa_monster_mission mission;
  qa_actor_id route_goal, combat_goal, placement_activator, activation_activator;
  qa_authored_monster_barrier *barriers;
  /* q2PathFollower / q2CombatFollower storage and their creation-time values. */
  qa_q2m_path_follower path;
  bool path_has_move_target; qa_actor_id path_move_target;
  bool path_has_enemy; qa_actor_id path_enemy;
  qa_q2m_combat_follower combat;
  bool combat_has_enemy; qa_actor_id combat_enemy;
  bool combat_has_old_enemy; qa_actor_id combat_old_enemy;
  bool combat_has_activator; qa_actor_id combat_activator;
};

typedef struct index_entry { qa_actor_id actor; record *value; } index_entry;

struct qa_app_sim_selected_monsters {
  const qa_enemy_selection *selection;
  qa_app_sim_monster_map map;
  qa_app_sim_selected_monster_behavior behavior;
  /* TS authored / definitions maps (set together, deleted together). */
  index_entry *index; size_t count, capacity;
  /* Every record ever created: TS entries stay reachable through mission
   * closures after their map rows are deleted. */
  record **records; size_t record_count, record_capacity;
  char **strings; size_t string_count, string_capacity;
  qa_session_actor_registry *actors;
  int64_t release_subscription;
};

static void *grow(void *base, size_t *capacity, size_t count, size_t size) {
  if (count < *capacity) return base;
  size_t next = *capacity == 0 ? 8 : *capacity * 2;
  void *grown = realloc(base, next * size);
  if (grown == NULL) qa_range_error("Out of memory");
  *capacity = next;
  return grown;
}
/* Entry strings outlive any single assignment: a follower snapshot or a
 * caller may hold a replaced value, as a TS string value would survive. */
static char *intern(qa_app_sim_selected_monsters *m, const char *text) {
  if (text == NULL) text = "";
  for (size_t i = 0; i < m->string_count; i++) if (strcmp(m->strings[i], text) == 0) return m->strings[i];
  m->strings = grow(m->strings, &m->string_capacity, m->string_count, sizeof *m->strings);
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  m->strings[m->string_count++] = copy;
  return copy;
}

static bool is_q1(const qa_app_sim_selected_monsters *m) { return same(m->map.kind, "q1"); }
static qa_q2_game_services *q2_game(const qa_app_sim_selected_monsters *m) { return qa_q2_entity_services_game(m->map.q2); }
static qa_shared_body_table *bodies_of(const qa_app_sim_selected_monsters *m) {
  return is_q1(m) ? m->map.q1->host.bodies : q2_game(m)->host->bodies;
}
static qa_gameplay_authority *combat_of(const qa_app_sim_selected_monsters *m) {
  return is_q1(m) ? m->map.q1->host.combat : q2_game(m)->host->combat;
}
static double q2_now(const qa_app_sim_selected_monsters *m) {
  qa_q2_foundation_host *host = q2_game(m)->host;
  return host->now(host->ctx);
}
static void set_can_take_damage(qa_app_sim_selected_monsters *m, qa_owned_actor actor, bool value) {
  qa_combat_trait_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_can_take_damage = true;
  changes.can_take_damage = value;
  qa_gameplay_authority_set_traits(combat_of(m), actor, changes);
}

static ptrdiff_t find_index(const qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  for (size_t i = 0; i < m->count; i++) if (qa_same_actor(m->index[i].actor, actor)) return (ptrdiff_t)i;
  return -1;
}
static record *lookup(const qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  ptrdiff_t at = find_index(m, actor);
  return at < 0 ? NULL : m->index[at].value;
}
/* TS authored.set + definitions.set: an existing key keeps its position. */
static void index_set(qa_app_sim_selected_monsters *m, qa_actor_id actor, record *value) {
  ptrdiff_t at = find_index(m, actor);
  if (at >= 0) { m->index[at].value = value; return; }
  m->index = grow(m->index, &m->capacity, m->count, sizeof *m->index);
  m->index[m->count].actor = actor;
  m->index[m->count].value = value;
  m->count++;
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_app_sim_selected_monsters *m = ctx;
  ptrdiff_t at = find_index(m, actor.id);
  if (at < 0) return;
  memmove(&m->index[at], &m->index[at + 1], (m->count - (size_t)at - 1) * sizeof *m->index);
  m->count--;
}
static void set_authored_target(qa_app_sim_selected_monsters *m, record *rec) {
  if (is_q1(m)) qa_q1_set_authored_target(m->map.q1, rec->entry.base.actor.id, &rec->entry.base);
  else qa_q2_entity_services_set_authored_target(m->map.q2, &rec->entry.base);
}
/* TS definitions.get(actor) for the actor's current map row. */
static bool definition_of(const qa_app_sim_selected_monsters *m, qa_actor_id actor, qa_monster_definition_reference *out) {
  record *rec = lookup(m, actor);
  if (rec == NULL) return false;
  *out = rec->definition;
  return true;
}
static qa_monster_definition_reference copy_definition(qa_app_sim_selected_monsters *m, qa_monster_definition_reference definition) {
  qa_monster_definition_reference out;
  out.source.provider = intern(m, definition.source.provider);
  out.source.content = intern(m, definition.source.content);
  out.classname = intern(m, definition.classname);
  return out;
}
static void set_route_goal(record *rec, const qa_actor_id *goal) {
  if (goal == NULL) { rec->entry.route_goal = NULL; return; }
  rec->route_goal = *goal;
  rec->entry.route_goal = &rec->route_goal;
}
static void set_combat_goal(record *rec, const qa_actor_id *goal) {
  if (goal == NULL) { rec->entry.combat_goal = NULL; return; }
  rec->combat_goal = *goal;
  rec->entry.combat_goal = &rec->combat_goal;
}
static bool enemy_of(qa_app_sim_selected_monsters *m, qa_actor_id actor, qa_actor_id *out) {
  return m->behavior.enemy(m->behavior.ctx, actor, out);
}

/* ---- MonsterMission ---- */
static const qa_actor_id *route(qa_app_sim_selected_monsters *m, record *rec);
static void mission_started(void *ctx) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  qa_monster_definition_reference definition;
  if (!definition_of(m, rec->entry.base.actor.id, &definition)) qa_raise("Error", 0, 0, "Started monster has no selected definition");
  (void)m->behavior.validate_placement(m->behavior.ctx, &rec->entry, definition, NULL);
}
static const qa_actor_id *mission_route(void *ctx) {
  record *rec = ctx;
  return route(rec->owner, rec);
}
static void mission_found_target(void *ctx) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  qa_actor_id enemy;
  if (rec->entry.combat_target[0] != '\0' && enemy_of(m, rec->entry.base.actor.id, &enemy) && !is_q1(m)) {
    qa_q2_game_services *game = q2_game(m);
    qa_q2_entity *target = game->pick_target(game->ctx, rec->entry.combat_target);
    if (target != NULL) {
      rec->entry.combat_target = intern(m, "");
      set_combat_goal(rec, &target->actor.id);
      if (game->options.edition == QA_Q2_EDITION_CLASSIC) target->targetname = "";
    }
  }
}
static qa_monster_combat_route mission_combat_route(void *ctx) {
  record *rec = ctx;
  qa_monster_combat_route out = {rec->entry.combat_goal, rec->entry.stand_ground};
  return out;
}
static bool mission_use(void *ctx, const qa_actor_id *activator) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  if (same(rec->entry.placement.kind, "waiting")) {
    if (activator == NULL) rec->entry.placement.activator = NULL;
    else { rec->placement_activator = *activator; rec->entry.placement.activator = &rec->placement_activator; }
    return true;
  }
  if (same(rec->entry.activation.kind, "active")) return false;
  if (is_q1(m)) qa_raise("Error", 0, 0, "Triggered monster use requires its Q2 map program");
  if (same(rec->entry.activation.kind, "dormant")) {
    qa_q2_game_services *game = q2_game(m);
    double now = q2_now(m); /* TS now() + frameSeconds(): left operand first */
    double delay = game->options.edition == QA_Q2_EDITION_RERELEASE ? game->host->frame_seconds(game->host->ctx) : 0.1;
    rec->entry.activation.kind = "scheduled";
    rec->entry.activation.at = now + delay;
    if (activator == NULL) rec->entry.activation.activator = NULL;
    else { rec->activation_activator = *activator; rec->entry.activation.activator = &rec->activation_activator; }
  }
  return true;
}
static void mission_spawned(void *ctx) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  if (is_q1(m)) {
    m->map.q1->total_monsters++;
    if (same(rec->entry.base.classname, "monster_fish") && same(m->map.q1->options.edition, "classic")) m->map.q1->total_monsters++;
  } else qa_q2_entity_services_counters(m->map.q2)->total_monsters++;
}
static void mission_killed(void *ctx, const qa_actor_id *attacker) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  if (rec->entry.counted_death) return;
  rec->entry.counted_death = true;
  if (is_q1(m)) {
    qa_q1_services *game = m->map.q1;
    game->killed_monsters++;
    qa_q1_event event;
    memset(&event, 0, sizeof event);
    event.kind = "monster-killed";
    event.has_actor = true;
    event.actor = rec->entry.base.actor.id;
    event.total = game->total_monsters;
    event.found = game->killed_monsters;
    qa_q1_emit(game, event);
    qa_q1_use_authored_targets(game, &rec->entry.base, attacker);
    return;
  }
  qa_q2_game_services *game = q2_game(m);
  qa_q2_entity_services_counters(m->map.q2)->killed_monsters++;
  if (rec->entry.drop_item[0] != '\0') (void)qa_q2_item_module_drop_monster(m->map.items, rec->entry.base.actor, game, rec->entry.drop_item);
  if (rec->entry.death_target[0] != '\0') rec->entry.base.target = rec->entry.death_target;
  game->use_targets(game->ctx, rec->entry.base, attacker, false);
}

static const qa_actor_id *route(qa_app_sim_selected_monsters *m, record *rec) {
  qa_authored_monster *entry = &rec->entry;
  if (entry->route_resolved) return entry->route_goal;
  if (!is_q1(m)) {
    qa_q2_game_services *game = q2_game(m);
    size_t count = 0;
    qa_q2_entity *const *targets = game->targets(game->ctx, entry->route, &count);
    for (size_t i = 0; i < count; i++)
      if (same(targets[i]->classname, "point_combat")) {
        entry->combat_target = entry->route;
        entry->base.target = intern(m, "");
        entry->route = intern(m, "");
        break;
      }
  }
  bool path_corner = false;
  qa_actor_id goal;
  if (is_q1(m)) {
    qa_q1_actor *found = NULL;
    if (qa_q1_find(m->map.q1, entry->route, &found, 1) != 0 && same(found->classname, "path_corner")
        && qa_session_actor_registry_is_live(m->actors, found->actor.id)) { path_corner = true; goal = found->actor.id; }
  } else {
    qa_q2_game_services *game = q2_game(m);
    qa_q2_entity *target = game->pick_target(game->ctx, entry->route);
    if (target != NULL && same(target->classname, "path_corner") && qa_session_actor_registry_is_live(m->actors, target->actor.id)) {
      path_corner = true; goal = target->actor.id;
    }
  }
  set_route_goal(rec, path_corner ? &goal : NULL);
  entry->route_resolved = true;
  if (!is_q1(m) && entry->route_goal != NULL) entry->base.target = intern(m, "");
  return entry->route_goal;
}

static record *new_record(qa_app_sim_selected_monsters *m) {
  record *rec = calloc(1, sizeof *rec);
  if (rec == NULL) qa_range_error("Out of memory");
  rec->owner = m;
  m->records = grow(m->records, &m->record_capacity, m->record_count, sizeof *m->records);
  m->records[m->record_count++] = rec;
  qa_monster_mission *mission = &rec->mission;
  mission->ctx = rec;
  mission->spawned = mission_spawned;
  mission->started = mission_started;
  mission->killed = mission_killed;
  mission->route = mission_route;
  mission->use = mission_use;
  mission->combat_route = mission_combat_route;
  mission->found_target = mission_found_target;
  return rec;
}

qa_app_sim_selected_monsters *qa_app_sim_selected_monsters_create(const qa_enemy_selection *selection,
    qa_app_sim_monster_map map, qa_app_sim_selected_monster_behavior behavior) {
  qa_app_sim_selected_monsters *m = calloc(1, sizeof *m);
  if (m == NULL) qa_range_error("Out of memory");
  m->selection = selection;
  m->map = map;
  m->behavior = behavior;
  m->actors = same(map.kind, "q1") ? map.q1->host.actors : qa_q2_entity_services_game(map.q2)->host->actors;
  m->release_subscription = qa_session_actor_registry_on_release(m->actors, on_release, m);
  return m;
}

void qa_app_sim_selected_monsters_destroy(qa_app_sim_selected_monsters *m) {
  if (m == NULL) return;
  qa_session_actor_registry_off_release(m->actors, m->release_subscription);
  for (size_t i = 0; i < m->record_count; i++) { free(m->records[i]->barriers); free(m->records[i]); }
  for (size_t i = 0; i < m->string_count; i++) free(m->strings[i]);
  free(m->records); free(m->strings); free(m->index); free(m);
}

/* TS new Map(properties) / Q2 values Map: get(key). */
static const char *field_get(qa_app_sim_monster_fields fields, const char *key) {
  if (fields.q1 != NULL) {
    const char *found = NULL;
    for (size_t i = 0; i < fields.q1->property_count; i++)
      if (same(fields.q1->properties[i].key, key)) found = fields.q1->properties[i].value;
    return found;
  }
  return fields.q2 == NULL ? NULL : qa_q2_spawn_get(fields.q2, key);
}
/* TS Number(fields.get(key) ?? 0). */
static double field_number(qa_app_sim_monster_fields fields, const char *key) {
  const char *value = field_get(fields, key);
  return value == NULL ? 0 : qa_js_string_to_number(value);
}
static const char *field_text(qa_app_sim_monster_fields fields, const char *key) {
  const char *value = field_get(fields, key);
  return value == NULL ? "" : value;
}
/* TS selection.byClassname[classname] ?? selection.default. */
static qa_monster_selection_target selected_target(const qa_app_sim_selected_monsters *m, const char *classname) {
  for (size_t i = 0; i < m->selection->by_classname_count; i++)
    if (same(m->selection->by_classname[i].classname, classname)) return m->selection->by_classname[i].target;
  return m->selection->default_target;
}

bool qa_app_sim_selected_monsters_resolve(qa_app_sim_selected_monsters *m, const char *classname,
    qa_app_sim_monster_fields fields, qa_monster_definition_reference *out) {
  if (strncmp(classname, "monster_", 8) != 0) return false;
  qa_monster_selection_target target = selected_target(m, classname);
  if (target.is_map_defined) return false;
  bool supported = is_q1(m) ? qa_app_sim_q1_ordinary_monster(classname) : qa_app_sim_q2_ordinary_monster(classname);
  if (!supported) qa_raise("Error", 0, 0, "Selected monster admission does not yet preserve authored %s obligations", classname);
  double flags = field_number(fields, "spawnflags");
  int32_t inhibition = is_q1(m) ? 0xf00 : q2_game(m)->options.edition == QA_Q2_EDITION_RERELEASE ? 0xff00 : 0x1f00;
  int32_t bits = qa_js_to_int32(flags);
  if ((bits & ~inhibition & ~(is_q1(m) ? 1 : 3)) != 0 || (same(classname, "monster_zombie") && (bits & 1) != 0))
    qa_raise("Error", 0, 0, "Selected monster admission does not yet preserve %s spawn flags %s", classname, QA_JS_NUMBER(flags));
  *out = target.reference;
  return true;
}

static void admit(qa_app_sim_selected_monsters *m, qa_owned_actor actor, qa_app_sim_monster_fields fields, double ordinal,
                  qa_monster_definition_reference definition) {
  record *rec = new_record(m);
  qa_authored_monster *entry = &rec->entry;
  const char *target = field_text(fields, "target");
  entry->base.actor = actor;
  entry->base.classname = intern(m, field_text(fields, "classname"));
  entry->source_ordinal = ordinal;
  entry->spawnflags = field_number(fields, "spawnflags");
  entry->death_target = intern(m, field_text(fields, "deathtarget"));
  entry->drop_item = intern(m, field_text(fields, "item"));
  entry->base.targetname = intern(m, field_text(fields, "targetname"));
  entry->base.target = intern(m, target);
  entry->base.killtarget = intern(m, field_text(fields, "killtarget"));
  entry->base.message = intern(m, field_text(fields, "message"));
  entry->base.delay = field_number(fields, "delay");
  entry->route = intern(m, target);
  entry->route_goal = NULL;
  entry->route_resolved = false;
  entry->counted_death = false;
  entry->combat_target = intern(m, field_text(fields, "combattarget"));
  entry->combat_goal = NULL;
  entry->stand_ground = false;
  entry->placement.kind = "ready";
  entry->activation.kind = !is_q1(m) && (qa_js_to_int32(field_number(fields, "spawnflags")) & 2) != 0 ? "dormant" : "active";
  rec->definition = copy_definition(m, definition);
  rec->mission.ambush = (qa_js_to_int32(entry->spawnflags) & 1) != 0;
  index_set(m, actor.id, rec);
  set_authored_target(m, rec);
  m->behavior.attach(m->behavior.ctx, actor, rec->definition, &rec->mission);
  if (!same(entry->activation.kind, "active")) {
    set_can_take_damage(m, actor, false);
    qa_shared_body_table_link(bodies_of(m), actor, NULL);
  }
}

void qa_app_sim_selected_monsters_admit_q1(qa_app_sim_selected_monsters *m, qa_owned_actor actor,
    const qa_q1_entity *source, double ordinal, qa_monster_definition_reference definition) {
  qa_app_sim_monster_fields fields = {source, NULL};
  admit(m, actor, fields, ordinal, definition);
}
void qa_app_sim_selected_monsters_admit_q2(qa_app_sim_selected_monsters *m, qa_owned_actor actor,
    const qa_q2_spawn_fields *source, qa_monster_definition_reference definition) {
  qa_app_sim_monster_fields fields = {NULL, source};
  admit(m, actor, fields, source->ordinal, definition);
}

bool qa_app_sim_selected_monsters_active(qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  record *rec = lookup(m, actor);
  return rec == NULL || (same(rec->entry.activation.kind, "active") && !same(rec->entry.placement.kind, "waiting"));
}

void qa_app_sim_saved_authored_monsters_free(qa_app_sim_saved_authored_monsters *saved) {
  if (saved == NULL) return;
  for (size_t i = 0; i < saved->count; i++) free((void *)saved->entries[i].placement.barriers);
  free(saved->entries);
  saved->entries = NULL;
  saved->count = 0;
}

qa_app_sim_saved_authored_monsters qa_app_sim_selected_monsters_capture(qa_app_sim_selected_monsters *m) {
  qa_app_sim_saved_authored_monsters out = {NULL, 0};
  if (m->count == 0) return out;
  out.entries = calloc(m->count, sizeof *out.entries);
  if (out.entries == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < m->count; i++) {
    const record *rec = m->index[i].value;
    const qa_authored_monster *entry = &rec->entry;
    qa_app_sim_saved_authored_monster *saved = &out.entries[i];
    out.count = i + 1;
    saved->definition = rec->definition;
    saved->actor = qa_saved_actor_id_of(entry->base.actor.id);
    saved->classname = entry->base.classname;
    saved->source_ordinal = entry->source_ordinal;
    saved->spawnflags = entry->spawnflags;
    saved->targetname = entry->base.targetname;
    saved->target = entry->base.target;
    saved->killtarget = entry->base.killtarget;
    saved->message = entry->base.message;
    saved->delay = entry->base.delay;
    saved->death_target = entry->death_target;
    saved->drop_item = entry->drop_item;
    saved->route = entry->route;
    saved->has_route_goal = entry->route_goal != NULL;
    if (entry->route_goal != NULL) saved->route_goal = qa_saved_actor_id_of(*entry->route_goal);
    saved->route_resolved = entry->route_resolved;
    saved->counted_death = entry->counted_death;
    saved->combat_target = entry->combat_target;
    saved->has_combat_goal = entry->combat_goal != NULL;
    if (entry->combat_goal != NULL) saved->combat_goal = qa_saved_actor_id_of(*entry->combat_goal);
    saved->stand_ground = entry->stand_ground;
    saved->placement.kind = entry->placement.kind;
    saved->placement.origin = entry->placement.origin;
    if (same(entry->placement.kind, "waiting")) {
      size_t count = entry->placement.barrier_count;
      qa_app_sim_saved_monster_barrier *barriers = count == 0 ? NULL : malloc(count * sizeof *barriers);
      if (count != 0 && barriers == NULL) qa_range_error("Out of memory");
      for (size_t b = 0; b < count; b++) {
        barriers[b].actor = qa_saved_actor_id_of(entry->placement.barriers[b].actor);
        barriers[b].origin = entry->placement.barriers[b].origin;
      }
      saved->placement.barriers = barriers;
      saved->placement.barrier_count = count;
      saved->placement.has_activator = entry->placement.activator != NULL;
      if (entry->placement.activator != NULL) saved->placement.activator = qa_saved_actor_id_of(*entry->placement.activator);
    }
    saved->activation.kind = entry->activation.kind;
    saved->activation.at = entry->activation.at;
    if (same(entry->activation.kind, "scheduled")) {
      saved->activation.has_activator = entry->activation.activator != NULL;
      if (entry->activation.activator != NULL) saved->activation.activator = qa_saved_actor_id_of(*entry->activation.activator);
    }
  }
  return out;
}

static qa_actor_id reference_saved(qa_app_sim_selected_monsters *m, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(m->actors, saved, "checkpoint");
}

void qa_app_sim_selected_monsters_restore(qa_app_sim_selected_monsters *m,
    const qa_app_sim_saved_authored_monster *entries, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_saved_authored_monster *saved = &entries[i];
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(m->actors, saved->actor, &actor);
    qa_provider_id provider = is_q1(m) ? qa_q1_services_provider(m->map.q1) : q2_game(m)->options.provider;
    if (!resolved || !same(actor.owner, provider)) qa_raise("Error", 0, 0, "Missing authored monster map owner");
    qa_monster_selection_target expected = selected_target(m, saved->classname);
    bool differs = expected.is_map_defined || !same(expected.reference.classname, saved->definition.classname)
        || !same(expected.reference.source.provider, saved->definition.source.provider)
        || !same(expected.reference.source.content, saved->definition.source.content);
    if (!differs) {
      qa_actor_observation_result observed = qa_session_actor_registry_observe(m->actors, actor.id);
      size_t length = strlen(expected.reference.source.provider) + strlen(expected.reference.classname) + 2;
      char *name = malloc(length);
      if (name == NULL) qa_range_error("Out of memory");
      snprintf(name, length, "%s/%s", expected.reference.source.provider, expected.reference.classname);
      differs = !observed.has_value || !same(observed.value.definition, name);
      free(name);
    }
    if (differs) qa_raise("Error", 0, 0, "Saved monster definition differs from selected actor");
    record *rec = new_record(m);
    qa_authored_monster *entry = &rec->entry;
    entry->base.actor = actor;
    entry->base.classname = intern(m, saved->classname);
    entry->base.targetname = intern(m, saved->targetname);
    entry->base.target = intern(m, saved->target);
    entry->base.killtarget = intern(m, saved->killtarget);
    entry->base.message = intern(m, saved->message);
    entry->base.delay = saved->delay;
    entry->source_ordinal = saved->source_ordinal;
    entry->spawnflags = saved->spawnflags;
    entry->death_target = intern(m, saved->death_target);
    entry->drop_item = intern(m, saved->drop_item);
    entry->route = intern(m, saved->route);
    if (saved->has_route_goal) { qa_actor_id goal = reference_saved(m, saved->route_goal); set_route_goal(rec, &goal); }
    entry->route_resolved = saved->route_resolved;
    entry->counted_death = saved->counted_death;
    entry->combat_target = intern(m, saved->combat_target);
    if (saved->has_combat_goal) { qa_actor_id goal = reference_saved(m, saved->combat_goal); set_combat_goal(rec, &goal); }
    entry->stand_ground = saved->stand_ground;
    if (same(saved->placement.kind, "waiting")) {
      entry->placement.kind = "waiting";
      size_t barrier_count = saved->placement.barrier_count;
      rec->barriers = barrier_count == 0 ? NULL : malloc(barrier_count * sizeof *rec->barriers);
      if (barrier_count != 0 && rec->barriers == NULL) qa_range_error("Out of memory");
      for (size_t b = 0; b < barrier_count; b++) {
        rec->barriers[b].actor = reference_saved(m, saved->placement.barriers[b].actor);
        rec->barriers[b].origin = saved->placement.barriers[b].origin;
      }
      entry->placement.barriers = rec->barriers;
      entry->placement.barrier_count = barrier_count;
      if (saved->placement.has_activator) {
        rec->placement_activator = reference_saved(m, saved->placement.activator);
        entry->placement.activator = &rec->placement_activator;
      }
    } else if (same(saved->placement.kind, "teleport")) {
      entry->placement.kind = "teleport";
      entry->placement.origin = saved->placement.origin;
    } else entry->placement.kind = "ready";
    if (same(saved->activation.kind, "scheduled")) {
      entry->activation.kind = "scheduled";
      entry->activation.at = saved->activation.at;
      if (saved->activation.has_activator) {
        rec->activation_activator = reference_saved(m, saved->activation.activator);
        entry->activation.activator = &rec->activation_activator;
      }
    } else entry->activation.kind = same(saved->activation.kind, "dormant") ? "dormant" : "active";
    rec->definition = copy_definition(m, saved->definition);
    rec->mission.ambush = (qa_js_to_int32(entry->spawnflags) & 1) != 0;
    if (same(entry->placement.kind, "waiting")) {
      if (is_q1(m) || entry->base.targetname[0] == '\0' || entry->placement.barrier_count == 0)
        qa_raise("Error", 0, 0, "Saved waiting monster has no authored door encounter");
      qa_q2_game_services *game = q2_game(m);
      for (size_t b = 0; b < entry->placement.barrier_count; b++) {
        qa_actor_id barrier = entry->placement.barriers[b].actor;
        qa_q2_entity *door = game->entity(game->ctx, &barrier);
        bool bad = door == NULL ? qa_session_actor_registry_is_live(m->actors, barrier)
            : !same(door->classname, "func_door") || !same(door->targetname, entry->base.targetname);
        if (bad) qa_raise("Error", 0, 0, "Saved waiting monster barrier differs from its authored encounter");
      }
    }
    index_set(m, actor.id, rec);
    set_authored_target(m, rec);
  }
}

bool qa_app_sim_selected_monsters_before_turn(qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  record *rec = lookup(m, actor);
  if (rec == NULL) return true;
  qa_authored_monster *entry = &rec->entry;
  if (same(entry->placement.kind, "teleport")) {
    qa_body_state_result body = qa_shared_body_table_read(bodies_of(m), actor);
    if (body.has_value) {
      qa_vec3 origin = body.value.origin;
      if (origin.x != entry->placement.origin.x || origin.y != entry->placement.origin.y || origin.z != entry->placement.origin.z) {
        if (!m->behavior.placement_ready(m->behavior.ctx, entry)) {
          entry->placement.kind = "teleport";
          entry->placement.origin = origin;
        } else {
          qa_monster_definition_reference definition;
          if (!definition_of(m, actor, &definition)) qa_raise("Error", 0, 0, "Teleported monster has no selected definition");
          entry->placement.kind = "ready";
          if (!m->behavior.validate_placement(m->behavior.ctx, entry, definition, &origin)) return false;
        }
      }
    }
  }
  if (same(entry->placement.kind, "waiting")) {
    if (!m->behavior.placement_ready(m->behavior.ctx, entry)) return false;
    qa_monster_definition_reference definition;
    if (!definition_of(m, actor, &definition)) qa_raise("Error", 0, 0, "Waiting monster has no selected definition");
    bool has_activator = entry->placement.activator != NULL;
    qa_actor_id activator;
    if (has_activator) activator = *entry->placement.activator;
    if (!m->behavior.validate_placement(m->behavior.ctx, entry, definition, NULL)) return false;
    entry->placement.kind = "ready";
    entry->placement.barriers = NULL;
    entry->placement.barrier_count = 0;
    entry->placement.activator = NULL;
    set_can_take_damage(m, entry->base.actor, true);
    qa_shared_body_table_link(bodies_of(m), entry->base.actor, NULL);
    m->behavior.resume(m->behavior.ctx, actor, has_activator ? &activator : NULL);
  }
  if (same(entry->activation.kind, "active")) return qa_app_sim_selected_monsters_active(m, actor);
  if (is_q1(m)) qa_raise("Error", 0, 0, "Triggered monster activation requires its Q2 map program");
  if (same(entry->activation.kind, "dormant") || entry->activation.at > q2_now(m)) return false;
  bool has_activator = entry->activation.activator != NULL;
  qa_actor_id activator;
  if (has_activator) activator = *entry->activation.activator;
  qa_q2_game_services *game = q2_game(m);
  qa_q2m_place_triggered_monster(game, entry->base.actor);
  if (!qa_session_actor_registry_is_live(m->actors, actor)) return false;
  entry->activation.kind = "active";
  entry->activation.activator = NULL;
  set_can_take_damage(m, entry->base.actor, true);
  qa_shared_body_table_link(bodies_of(m), entry->base.actor, NULL);
  bool pass = (qa_js_to_int32(entry->spawnflags) & 1) == 0 && has_activator;
  m->behavior.resume(m->behavior.ctx, actor, pass ? &activator : NULL);
  return qa_session_actor_registry_is_live(m->actors, actor) && qa_app_sim_selected_monsters_active(m, actor);
}

/* ---- path and combat followers ---- */
static void q1_advance(void *ctx, const char *name, const qa_actor_id *goal, double pause_until) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  rec->entry.route = intern(m, goal == NULL ? "" : name);
  set_route_goal(rec, goal);
  rec->entry.route_resolved = true;
  m->behavior.set_route(m->behavior.ctx, rec->entry.base.actor.id, goal, pause_until);
}
bool qa_app_sim_selected_monsters_q1_path_follower(qa_app_sim_selected_monsters *m, qa_actor_id actor,
    qa_app_sim_q1_path_follower *out) {
  record *rec = lookup(m, actor);
  if (rec == NULL) return false;
  memset(out, 0, sizeof *out);
  out->targetname = rec->entry.route;
  out->has_enemy = enemy_of(m, actor, &out->enemy);
  out->ctx = rec;
  out->advance = q1_advance;
  return true;
}

static bool path_move_target(void *ctx, qa_actor_id *out) {
  record *rec = ctx;
  if (rec->path_has_move_target) *out = rec->path_move_target;
  return rec->path_has_move_target;
}
static bool path_enemy(void *ctx, qa_actor_id *out) {
  record *rec = ctx;
  if (rec->path_has_enemy) *out = rec->path_enemy;
  return rec->path_has_enemy;
}
static void q2_advance(void *ctx, const char *name, const qa_actor_id *goal, double pause_until) {
  record *rec = ctx;
  qa_app_sim_selected_monsters *m = rec->owner;
  rec->entry.route = intern(m, name);
  set_route_goal(rec, goal);
  rec->entry.route_resolved = true;
  m->behavior.set_route(m->behavior.ctx, rec->entry.base.actor.id, goal, pause_until);
}
const qa_q2m_path_follower *qa_app_sim_selected_monsters_q2_path_follower(qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  record *rec = lookup(m, actor);
  if (rec == NULL) return NULL;
  rec->path_has_move_target = rec->entry.route_goal != NULL;
  if (rec->path_has_move_target) rec->path_move_target = *rec->entry.route_goal;
  rec->path_has_enemy = enemy_of(m, actor, &rec->path_enemy);
  rec->path.ctx = rec;
  rec->path.actor = rec->entry.base.actor;
  rec->path.move_target = path_move_target;
  rec->path.enemy = path_enemy;
  rec->path.advance = q2_advance;
  return &rec->path;
}

static bool combat_move_target(void *ctx, qa_actor_id *out) {
  record *rec = ctx;
  if (rec->entry.combat_goal != NULL) *out = *rec->entry.combat_goal;
  return rec->entry.combat_goal != NULL;
}
static bool combat_enemy(void *ctx, qa_actor_id *out) {
  record *rec = ctx;
  if (rec->combat_has_enemy) *out = rec->combat_enemy;
  return rec->combat_has_enemy;
}
static bool combat_old_enemy(void *ctx, qa_actor_id *out) {
  record *rec = ctx;
  if (rec->combat_has_old_enemy) *out = rec->combat_old_enemy;
  return rec->combat_has_old_enemy;
}
static bool combat_activator(void *ctx, qa_actor_id *out) {
  record *rec = ctx;
  if (rec->combat_has_activator) *out = rec->combat_activator;
  return rec->combat_has_activator;
}
static void combat_advance(void *ctx, const char *target, const qa_actor_id *goal, const qa_actor_id *move_target) {
  record *rec = ctx;
  (void)goal; /* TS advance(target, _goal, moveTarget) ignores the goal. */
  rec->entry.base.target = intern(rec->owner, target);
  set_combat_goal(rec, move_target);
}
static void combat_hold(void *ctx) {
  record *rec = ctx;
  rec->entry.stand_ground = true;
}
static void combat_finish(void *ctx) {
  record *rec = ctx;
  rec->entry.base.target = intern(rec->owner, "");
  set_combat_goal(rec, NULL);
}
const qa_q2m_combat_follower *qa_app_sim_selected_monsters_q2_combat_follower(qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  record *rec = lookup(m, actor);
  if (rec == NULL) return NULL;
  rec->combat_has_enemy = enemy_of(m, actor, &rec->combat_enemy);
  rec->combat_has_old_enemy = m->behavior.old_enemy(m->behavior.ctx, actor, &rec->combat_old_enemy);
  rec->combat_has_activator = same(rec->entry.activation.kind, "scheduled") && rec->entry.activation.activator != NULL;
  if (rec->combat_has_activator) rec->combat_activator = *rec->entry.activation.activator;
  rec->combat.ctx = rec;
  rec->combat.move_target = combat_move_target;
  rec->combat.enemy = combat_enemy;
  rec->combat.old_enemy = combat_old_enemy;
  rec->combat.activator = combat_activator;
  rec->combat.walking = true;
  rec->combat.advance = combat_advance;
  rec->combat.hold = combat_hold;
  rec->combat.finish = combat_finish;
  return &rec->combat;
}

const qa_monster_mission *qa_app_sim_selected_monsters_mission(qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  record *rec = lookup(m, actor);
  if (rec == NULL) return NULL;
  rec->mission.ambush = (qa_js_to_int32(rec->entry.spawnflags) & 1) != 0;
  return &rec->mission;
}
qa_authored_monster *qa_app_sim_selected_monsters_authored(qa_app_sim_selected_monsters *m, qa_actor_id actor) {
  record *rec = lookup(m, actor);
  return rec == NULL ? NULL : &rec->entry;
}
bool qa_app_sim_selected_monsters_definition(qa_app_sim_selected_monsters *m, qa_actor_id actor,
    qa_monster_definition_reference *out) {
  return definition_of(m, actor, out);
}
size_t qa_app_sim_selected_monsters_count(const qa_app_sim_selected_monsters *m) { return m->count; }
qa_authored_monster *qa_app_sim_selected_monsters_at(qa_app_sim_selected_monsters *m, size_t index) {
  return index < m->count ? &m->index[index].value->entry : NULL;
}

void qa_app_sim_authored_monster_set_waiting(qa_authored_monster *entry, const qa_authored_monster_barrier *barriers, size_t count) {
  record *rec = (record *)entry; /* entry is the record's first member */
  qa_authored_monster_barrier *copy = count == 0 ? NULL : malloc(count * sizeof *copy);
  if (count != 0 && copy == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(copy, barriers, count * sizeof *copy);
  free(rec->barriers);
  rec->barriers = copy;
  entry->placement.kind = "waiting";
  entry->placement.barriers = copy; entry->placement.barrier_count = count;
  entry->placement.activator = NULL;
}
