/* Row-audit regressions for Q2 base and rerelease over the base fake game
 * (copied from test_q2_base.c, with sentinel ctx checks in every fake).
 * Expected values come from the TypeScript run under bun on the same inputs
 * (scratch oracle q2rows_base_oracle.ts). GPL-2.0-or-later. */
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "qa/content/q2/base/entities.h"
#include "qa/content/q2/base/player.h"
#include "qa/content/q2/foundation.h"
#include "qa/content/q2/rerelease/campaign.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q2/rerelease/spawns.h"
#include "qa/world/actors.h"
#include "qa/world/gameplay.h"
#include <unistd.h>


static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0); /* no coredump for expected aborts */ \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)


/* ---------------- pure helpers (TS "Q2 entity helpers") ---------------- */

/* Every fake receives the sentinel its host or game was built with. */
static char SENTINEL;
static int ctx_errors = 0;
static void check_ctx(void *c) { if (c != (void *)&SENTINEL) { ctx_errors++; if (getenv("CTX_ABORT")) abort(); } }

typedef struct fake_game {
  qa_identity_owner identities;
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_actor_callback_table *callbacks;
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  qa_q2_source_callbacks *source_callbacks;
  qa_q2_foundation_host host;
  qa_q2_game_services game;
  qa_q2_counters counters;
  qa_owned_actor world;
  qa_q2_entity **entities; size_t entity_count, entity_cap;
  qa_q2_entity **target_list;
  double now;
  double randoms[64]; size_t random_count, random_at;
  int emits, diagnostics, gibs, respawns, deaths;
  qa_q2_base_player_view last_view;
  qa_attack_provenance attack_storage;
  qa_actor_id attack_attacker, attack_inflictor;
} fake_game;

static fake_game *FG;

static double fake_now(void *c) { check_ctx(c); return FG->now; }
static double fake_frame(void *c) { check_ctx(c); return 0.1; }
static double fake_random(void *c) {
  check_ctx(c);
  if (FG->random_at < FG->random_count) return FG->randoms[FG->random_at++];
  return 0.5;
}
static void fake_emit(void *c, qa_q2_presentation_event e) { check_ctx(c); (void)e; FG->emits++; }
static void fake_diagnostic(void *c, const char *m) { check_ctx(c); (void)m; FG->diagnostics++; }
static double fake_contents(void *c, qa_vec3 p) { check_ctx(c); (void)p; return 0; }
static qa_actor_id PLAYER_LIST[4]; static size_t PLAYER_COUNT;
static bool fake_is_player(void *c, qa_actor_id a) {
  check_ctx(c);
  for (size_t i = 0; i < PLAYER_COUNT; i++) if (qa_same_actor(PLAYER_LIST[i], a)) return true;
  return false;
}
static bool fake_is_monster(void *c, qa_actor_id a) { check_ctx(c); (void)a; return false; }
static const qa_actor_id *fake_players_list(void *c, size_t *n) { check_ctx(c); *n = PLAYER_COUNT; return PLAYER_LIST; }
static qa_trace_result fake_trace(void *c, qa_q2_trace_request r) {
  check_ctx(c);
  qa_trace_result out = {0};
  out.fraction = 1;
  out.end = r.end;
  out.hit.kind = QA_TRACE_HIT_NONE;
  return out;
}
static qa_actor_id fake_world(void *c) { check_ctx(c); return FG->world.id; }
static void fake_portal(void *c, double p, bool open) { check_ctx(c); (void)p; (void)open; }
static void fake_prepare(void *c, const char *m, const qa_q2_landmark_carry *l, double f) { check_ctx(c); (void)m; (void)l; (void)f; }
static void fake_transition(void *c, qa_transition_intent t) { check_ctx(c); (void)t; }
static qa_q2_player_view_result fake_player_view(void *c, qa_actor_id a) {
  check_ctx(c); (void)a;
  qa_q2_player_view_result out = {0};
  out.has_value = true;
  return out;
}

static qa_body_state default_body(void) {
  qa_body_state state = {0};
  state.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  return state;
}
static qa_q2_entity *fake_add_entity(qa_owned_actor actor, const char *classname, const qa_q2_field_pair *values, size_t count) {
  qa_q2_entity *entity = calloc(1, sizeof *entity);
  qa_q2_spawn_fields fields = {0};
  fields.ordinal = -1;
  fields.classname = classname;
  qa_q2_spawn_set(&fields, "classname", classname);
  for (size_t i = 0; i < count; i++) qa_q2_spawn_set(&fields, values[i].key, values[i].value);
  qa_q2_entity_init(entity, actor, fields);
  if (!qa_shared_body_table_read(FG->bodies, actor.id).has_value)
    qa_shared_body_table_create_body(FG->bodies, actor, default_body());
  if (FG->entity_count == FG->entity_cap) {
    FG->entity_cap = FG->entity_cap ? FG->entity_cap * 2 : 8;
    FG->entities = realloc(FG->entities, FG->entity_cap * sizeof *FG->entities);
  }
  FG->entities[FG->entity_count++] = entity;
  return entity;
}
static qa_q2_entity *fake_create(void *c, const char *classname, const qa_q2_field_pair *values, size_t count) {
  check_ctx(c);
  qa_owned_actor actor = qa_session_actor_registry_allocate(FG->actors, "q2:test", classname);
  return fake_add_entity(actor, classname, values, count);
}
static qa_q2_entity *fake_spawn(void *c, qa_q2_spawn_fields fields) {
  check_ctx(c);
  qa_owned_actor actor = qa_session_actor_registry_allocate(FG->actors, "q2:test", fields.classname);
  size_t count = fields.value_count;
  const qa_q2_field_pair *values = fields.values;
  qa_q2_field_pair *copy = NULL;
  size_t copy_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (strcmp(values[i].key, "classname") == 0) continue;
    copy = realloc(copy, (copy_count + 1) * sizeof *copy);
    copy[copy_count++] = values[i];
  }
  qa_q2_entity *entity = fake_add_entity(actor, fields.classname, copy, copy_count);
  free(copy);
  return entity;
}
static qa_q2_entity *const *fake_entities(void *c, size_t *n) {
  check_ctx(c);
  *n = FG->entity_count;
  return FG->entities;
}
static void fake_remove(void *c, qa_q2_entity *entity) {
  check_ctx(c);
  for (size_t i = 0; i < FG->entity_count; i++) {
    if (FG->entities[i] == entity) {
      FG->entities[i] = FG->entities[--FG->entity_count];
      qa_owned_actor actor = entity->actor;
      qa_q2_entity_destroy(entity);
      free(entity);
      qa_session_actor_registry_release(FG->actors, actor);
      return;
    }
  }
}
static qa_q2_entity *fake_entity_of(void *c, const qa_actor_id *actor) {
  check_ctx(c);
  if (!actor) return NULL;
  for (size_t i = 0; i < FG->entity_count; i++)
    if (qa_same_actor(FG->entities[i]->actor.id, *actor)) return FG->entities[i];
  return NULL;
}
static qa_q2_entity *const *fake_targets(void *c, const char *name, size_t *n) {
  check_ctx(c);
  size_t count = 0;
  free(FG->target_list);
  FG->target_list = NULL;
  for (size_t i = 0; i < FG->entity_count; i++) {
    if (strcmp(FG->entities[i]->targetname, name) == 0) {
      FG->target_list = realloc(FG->target_list, (count + 1) * sizeof *FG->target_list);
      FG->target_list[count++] = FG->entities[i];
    }
  }
  *n = count;
  return FG->target_list;
}
static qa_q2_entity *fake_pick_target(void *c, const char *name) {
  check_ctx(c);
  for (size_t i = 0; i < FG->entity_count; i++)
    if (strcmp(FG->entities[i]->targetname, name) == 0) return FG->entities[i];
  return NULL;
}
static qa_body_state fake_body(void *c, qa_owned_actor actor) {
  check_ctx(c);
  qa_body_state_result body = qa_shared_body_table_read(FG->bodies, actor.id);
  if (!body.has_value) { qa_body_state empty = {0}; return empty; }
  return body.value;
}
static void fake_move(void *c, qa_q2_entity *entity, qa_q2_body_changes changes, bool link) {
  check_ctx(c); (void)link;
  qa_body_state next = fake_body(c, entity->actor);
  if (changes.has_origin) next.origin = changes.origin;
  if (changes.has_angles) next.angles = changes.angles;
  if (changes.has_velocity) next.velocity = changes.velocity;
  if (changes.has_bounds) next.bounds = changes.bounds;
  qa_actor_id ground = changes.ground;
  if (changes.has_ground) next.ground = changes.ground_null ? NULL : &ground;
  qa_shared_body_table_write(FG->bodies, entity->actor, next);
}
static void fake_link(void *c, qa_q2_entity *e) { check_ctx(c); (void)e; }
static void fake_solid(void *c, qa_q2_entity *e, const char *s) { check_ctx(c); e->solid = s; }
static void fake_motion(void *c, qa_q2_entity *e, const char *k) { check_ctx(c); e->motion = k; }
static void fake_schedule(void *c, qa_q2_entity *e, double delay, qa_q2_think think) {
  check_ctx(c);
  e->think = think;
  e->has_next_think = true;
  e->next_think = FG->now + delay;
}
static void fake_cancel(void *c, qa_q2_entity *e) { check_ctx(c); e->think = (qa_q2_think){0}; e->has_next_think = false; }
static void fake_show(void *c, qa_q2_entity *e) { check_ctx(c); (void)e; }
static void fake_sound(void *c, qa_owned_actor a, const char *p, double ch, double v, double at) { check_ctx(c); (void)a; (void)p; (void)ch; (void)v; (void)at; }
static void fake_use_targets(void *c, qa_authored_target e, const qa_actor_id *a, bool d) { check_ctx(c); (void)e; (void)a; (void)d; }
static qa_damage_outcome fake_damage(void *c, qa_actor_id t, qa_q2_entity *i, qa_actor_id ii, const qa_actor_id *a, double amt, double kb, qa_vec3 d, qa_vec3 p, qa_vec3 n, double m, double f, qa_item_id w) {
  check_ctx(c); (void)t; (void)i; (void)ii; (void)a; (void)amt; (void)kb; (void)d; (void)p; (void)n; (void)m; (void)f; (void)w;
  qa_damage_outcome out = {0};
  return out;
}
static void fake_radius(void *c, qa_q2_entity *i, const qa_actor_id *a, double d, const qa_actor_id *ig, double r, double m, double f, qa_item_id w) {
  check_ctx(c); (void)i; (void)a; (void)d; (void)ig; (void)r; (void)m; (void)f; (void)w;
}
static qa_attack_provenance fake_attack(void *c, qa_q2_entity *i, qa_actor_id ii, const qa_actor_id *a, double m, double f, qa_item_id w) {
  check_ctx(c); (void)i;
  FG->attack_inflictor = ii;
  if (a) FG->attack_attacker = *a;
  qa_attack_provenance attack = {0};
  attack.attacker = a ? &FG->attack_attacker : NULL;
  attack.inflictor = &FG->attack_inflictor;
  attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  attack.cause.means_of_death = m;
  attack.cause.damage_flags = f;
  (void)w;
  FG->attack_storage = attack;
  return attack;
}

static qa_bounds link_bounds(void *c, qa_owned_actor a, qa_body_state b) {
  check_ctx(c); (void)a;
  return (qa_bounds){qa_q2_add(b.origin, b.bounds.min), qa_q2_add(b.origin, b.bounds.max)};
}
static void link_noop(void *c, qa_linked_body b) { check_ctx(c); (void)b; }
static void unlink_noop(void *c, qa_actor_id a) { check_ctx(c); (void)a; }

static void fake_setup(qa_q2_mode mode) {
  FG = calloc(1, sizeof *FG);
  FG->now = 10;
  qa_identity_owner_create(&FG->identities, "test");
  FG->actors = qa_session_actor_registry_create(&FG->identities, 65536);
  qa_body_link_hooks link_hooks = {0};
  link_hooks.ctx = &SENTINEL;
  link_hooks.absolute_bounds = link_bounds;
  link_hooks.on_link = link_noop;
  link_hooks.on_unlink = unlink_noop;
  FG->bodies = qa_shared_body_table_create(FG->actors, link_hooks);
  FG->callbacks = qa_actor_callback_table_create(FG->actors);
  qa_gameplay_hooks hooks = {0};
  FG->combat = qa_gameplay_authority_create(FG->actors, FG->callbacks, hooks);
  FG->inventory = qa_shared_inventory_table_create(FG->actors);
  FG->source_callbacks = qa_q2_source_callbacks_create();
  FG->world = qa_session_actor_registry_allocate(FG->actors, "q2:test", "world");
  qa_shared_body_table_create_body(FG->bodies, FG->world, default_body());
  FG->host = (qa_q2_foundation_host){0};
  FG->host.now = fake_now; FG->host.frame_seconds = fake_frame; FG->host.random = fake_random;
  FG->host.actors = FG->actors; FG->host.bodies = FG->bodies; FG->host.callbacks = FG->callbacks;
  FG->host.combat = FG->combat; FG->host.inventory = FG->inventory;
  FG->host.world_actor = fake_world; FG->host.trace = fake_trace;
  FG->host.point_contents = fake_contents; FG->host.emit = fake_emit;
  FG->host.prepare_level_change = fake_prepare; FG->host.transition = fake_transition;
  FG->host.set_area_portal = fake_portal; FG->host.is_player = fake_is_player;
  FG->host.is_monster = fake_is_monster; FG->host.players = fake_players_list;
  FG->host.player_view_state = fake_player_view; FG->host.diagnostic = fake_diagnostic;
  FG->game = (qa_q2_game_services){0};
  FG->game.host = &FG->host;
  FG->host.ctx = &SENTINEL;
  FG->game.ctx = &SENTINEL;
  FG->game.source_callbacks = FG->source_callbacks;
  FG->game.counters = &FG->counters;
  FG->game.options.mode = mode;
  FG->game.options.edition = QA_Q2_EDITION_CLASSIC;
  FG->game.options.max_clients = 4;
  FG->game.options.campaign = "q2";
  FG->game.options.map_name = "base1";
  FG->game.options.inventory_provider = "q2:items";
  FG->game.entities = fake_entities; FG->game.spawn = fake_spawn; FG->game.create = fake_create;
  FG->game.remove = fake_remove; FG->game.entity = fake_entity_of; FG->game.body = fake_body;
  FG->game.move = fake_move; FG->game.link = fake_link; FG->game.solid = fake_solid;
  FG->game.motion = fake_motion; FG->game.schedule = fake_schedule; FG->game.cancel = fake_cancel;
  FG->game.use_targets = fake_use_targets; FG->game.targets = fake_targets;
  FG->game.pick_target = fake_pick_target; FG->game.show = fake_show; FG->game.sound = fake_sound;
  FG->game.damage = fake_damage; FG->game.radius_damage = fake_radius; FG->game.attack = fake_attack;
}
static void fake_teardown(void) {
  qa_q2_source_callbacks_destroy(FG->source_callbacks);
  qa_shared_inventory_table_destroy(FG->inventory);
  qa_gameplay_authority_destroy(FG->combat);
  qa_actor_callback_table_destroy(FG->callbacks);
  qa_shared_body_table_destroy(FG->bodies);
  qa_session_actor_registry_close(FG->actors);
  qa_session_actor_registry_destroy(FG->actors);
  qa_identity_owner_destroy(&FG->identities);
  free(FG->entities);
  free(FG->target_list);
  free(FG);
  FG = NULL;
}

/* ---------------- character (TS "Q2 character adapter") ---------------- */

/* ---------------- row regressions ---------------- */

/* TS clock messages are immutable strings: a display keeps its text when the
 * clock table grows (C kept them inside a realloc'd array). Oracle: " 0". */
static void test_clock_message_survives_growth(void) {
  fake_setup(QA_Q2_MODE_SINGLEPLAYER);
  qa_q2_base_entity_hooks hooks = {0};
  qa_q2_base_entity_module *module = qa_q2_base_entity_module_create(hooks);
  CHECK(module != NULL);
  qa_q2_entity *display = fake_create(&SENTINEL, "target_string", NULL, 0);
  display->targetname = "clockdisplay";
  qa_q2_entity *clocks[10];
  for (int i = 0; i < 10; i++) {
    clocks[i] = fake_create(&SENTINEL, "func_clock", NULL, 0);
    clocks[i]->spawnflags = 1 | 4; /* timer up, triggered */
    clocks[i]->target = "clockdisplay";
  }
  CHECK(qa_q2_base_entity_module_spawn(module, clocks[0], &FG->game));
  CHECK(clocks[0]->use.fn != NULL);
  qa_actor_id activator = clocks[0]->actor.id;
  clocks[0]->use.fn(clocks[0]->use.ctx, clocks[0], &FG->game, NULL, &activator);
  CHECK(display->message != NULL && strcmp(display->message, " 0") == 0);
  for (int i = 1; i < 10; i++) CHECK(qa_q2_base_entity_module_spawn(module, clocks[i], &FG->game));
  CHECK(strcmp(display->message, " 0") == 0);
  CHECK(strcmp(clocks[0]->message, " 0") == 0);
  qa_q2_base_entity_module_destroy(module);
  while (FG->entity_count > 0) fake_remove(&SENTINEL, FG->entities[0]);
  fake_teardown();
}

/* TS runQ2ClientCommand lowercases the whole client-supplied name. */
static size_t HOOK_COMMAND_LENGTH; static bool HOOK_COMMAND_LOWER;
static bool hook_command(void *c, qa_q2_entity *e, qa_q2_game_services *g, const char *name, const char *const *a, size_t n) {
  check_ctx(c); (void)e; (void)g; (void)a; (void)n;
  HOOK_COMMAND_LENGTH = strlen(name);
  HOOK_COMMAND_LOWER = strspn(name, "a") == HOOK_COMMAND_LENGTH;
  return true;
}
static void test_long_command_name(void) {
  qa_q2_player_hooks hooks = {0};
  hooks.ctx = &SENTINEL; hooks.command = hook_command;
  qa_q2_player_context context = {0};
  context.hooks = &hooks;
  char name[301];
  memset(name, 'A', 300); name[300] = '\0';
  CHECK(qa_q2_run_client_command(NULL, &context, name, NULL, 0));
  CHECK(HOOK_COMMAND_LENGTH == 300 && HOOK_COMMAND_LOWER);
}

/* TS q2RereleaseUnitReport is a stable sort. Oracle: m7,m4,m2,m6,m1,m3,m5,m8. */
static void test_unit_report_stable(void) {
  qa_q2_rerelease_campaign_state campaign;
  CHECK(qa_q2_create_rerelease_campaign_state(&campaign));
  static const struct { const char *map, *name; double order; } rows[] = {
    {"m1", "", 0}, {"m2", "Two", 0}, {"m3", "", 0}, {"m4", "Four", 2},
    {"m5", "", 0}, {"m6", "Six", 0}, {"m7", "Seven", 1}, {"m8", "", 0}};
  campaign.levels = calloc(8, sizeof *campaign.levels);
  campaign.level_cap = 8;
  for (size_t i = 0; i < 8; i++) {
    campaign.levels[i].map = strdup(rows[i].map);
    campaign.levels[i].name = strdup(rows[i].name);
    campaign.levels[i].visit_order = rows[i].order;
  }
  campaign.level_count = 8;
  size_t n = 0;
  qa_q2_rerelease_level_entry *report = qa_q2_rerelease_unit_report(&campaign, &n);
  static const char *expected[] = {"m7", "m4", "m2", "m6", "m1", "m3", "m5", "m8"};
  CHECK(n == 8);
  for (size_t i = 0; i < n && i < 8; i++) CHECK(strcmp(report[i].map, expected[i]) == 0);
  qa_q2_rerelease_unit_report_free(report, n);
  qa_q2_rerelease_campaign_destroy(&campaign);
}

/* TS selectQ2RereleaseSpawn ranks deathmatch spots with q2PlayersRange (the
 * C read an unset hook as 0). Oracle: slot of the spot at x=200, both modes. */
static void test_rerelease_deathmatch_spawn(void) {
  for (int farthest = 1; farthest >= 0; farthest--) {
    fake_setup(QA_Q2_MODE_DEATHMATCH);
    FG->game.options.edition = QA_Q2_EDITION_RERELEASE;
    qa_q2_entity *spots[3];
    const double xs[3] = {200, 0, 100};
    for (int i = 0; i < 3; i++) {
      spots[i] = fake_create(&SENTINEL, "info_player_deathmatch", NULL, 0);
      qa_q2_body_changes changes = {0};
      changes.has_origin = true; changes.origin = (qa_vec3){xs[i], 0, 0};
      fake_move(&SENTINEL, spots[i], changes, true);
    }
    qa_q2_entity *player = fake_create(&SENTINEL, "player", NULL, 0);
    qa_combat_state combat = {0};
    combat.health = 100;
    qa_gameplay_authority_create_state(FG->combat, player->actor, combat, NULL, NULL);
    PLAYER_LIST[0] = player->actor.id; PLAYER_COUNT = 1;
    qa_q2_rerelease_options options = qa_q2_create_rerelease_options(NULL);
    options.deathmatch_spawn_farthest = farthest != 0;
    qa_bounds bounds = {{0, 0, 0}, {0, 0, 0}};
    qa_q2_entity *spot = qa_q2_select_rerelease_spawn(&FG->game, player, bounds, options, "", false);
    CHECK(spot == spots[0]);
    PLAYER_COUNT = 0;
    while (FG->entity_count > 0) fake_remove(&SENTINEL, FG->entities[0]);
    fake_teardown();
  }
}

/* TS captureRerelease/restoreRerelease round-trip (absent in C before). */
static void test_rerelease_players_round_trip(void) {
  fake_setup(QA_Q2_MODE_COOP);
  qa_q2_entity *a = fake_create(&SENTINEL, "player", NULL, 0);
  qa_q2_entity *b = fake_create(&SENTINEL, "player", NULL, 0);
  qa_q2_rerelease_players source;
  memset(&source, 0, sizeof source);
  source.rerelease_options = qa_q2_create_rerelease_options(NULL);
  source.rerelease_options.coop_num_lives = 5;
  source.coop_restart_time = 12.5; source.deadly_kill_box = true; source.intermission_flags = 32;
  source.has_intermission_camera = true; source.intermission_camera_origin = (qa_vec3){1, 2, 3};
  qa_q2rr_player_extra_state *extra_a = calloc(1, sizeof *extra_a), *extra_b = calloc(1, sizeof *extra_b);
  qa_q2rr_player_extra_state_init(extra_a, 0, "a"); qa_q2rr_player_extra_state_init(extra_b, 1, "b");
  extra_a->lives = 3; extra_b->lives = 1;
  CHECK(qa_q2rr_players_set_extra(&source, a->actor.id, extra_a));
  CHECK(qa_q2rr_players_set_extra(&source, b->actor.id, extra_b));
  source.squad_spawns = calloc(1, sizeof *source.squad_spawns); source.squad_cap = 1; source.squad_count = 1;
  source.squad_spawns[0].actor = b->actor.id;
  source.squad_spawns[0].origin = (qa_vec3){4, 5, 6}; source.squad_spawns[0].angles = (qa_vec3){0, 90, 0};
  qa_q2_rerelease_players_checkpoint checkpoint = qa_q2_rerelease_players_capture(&source);
  CHECK(checkpoint.version == 1 && checkpoint.player_count == 2 && checkpoint.squad_count == 1);
  CHECK(checkpoint.players[0].state.lives == 3 && checkpoint.players[1].state.lives == 1);
  qa_q2_rerelease_players target;
  memset(&target, 0, sizeof target);
  qa_q2rr_player_extra_state *stale = calloc(1, sizeof *stale);
  qa_q2rr_player_extra_state_init(stale, 2, "stale");
  CHECK(qa_q2rr_players_set_extra(&target, a->actor.id, stale));
  qa_q2_rerelease_players_restore(&target, &FG->game, &checkpoint);
  CHECK(target.extra_count == 2 && target.squad_count == 1);
  CHECK(qa_same_actor(target.rerelease_states[0].actor, a->actor.id));
  CHECK(target.rerelease_states[0].state->lives == 3 && target.rerelease_states[1].state->lives == 1);
  CHECK(target.rerelease_states[0].state != extra_a);
  CHECK(target.rerelease_options.coop_num_lives == 5 && target.coop_restart_time == 12.5 && target.deadly_kill_box);
  CHECK(target.intermission_flags == 32 && target.has_intermission_camera && target.intermission_camera_origin.z == 3);
  CHECK(target.squad_spawns[0].origin.y == 5 && target.squad_spawns[0].angles.y == 90);
  qa_q2_rerelease_players_checkpoint_clear(&checkpoint);
  for (qa_q2_rerelease_players *p = &source; p; p = p == &source ? &target : NULL) {
    while (p->extra_count > 0) qa_q2rr_players_delete_extra(p, p->rerelease_states[0].actor);
    free(p->rerelease_states); free(p->squad_spawns);
  }
  while (FG->entity_count > 0) fake_remove(&SENTINEL, FG->entities[0]);
  fake_teardown();
}

int main(void) {
  test_clock_message_survives_growth();
  test_long_command_name();
  test_unit_report_stable();
  test_rerelease_deathmatch_spawn();
  test_rerelease_players_round_trip();
  CHECK(ctx_errors == 0);
  printf("test_q2_rows_base: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
