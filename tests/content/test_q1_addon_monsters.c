/*
 * Checks for src/content/q1/addons/monsters over the real Q1 game: the
 * world fixture (tests/content/q1_world_fixture.h), Q1Base and a
 * Q1AddonContext (tests/content/q1_addon_fixture.h). Key cases follow
 * tests/gameplay/q1-addons/monsters/heavy/source.test.ts and
 * tests/gameplay/q1-addons/common.test.ts (retail-asset integration tests
 * in TS; the cases here keep the same observable counts/values), plus
 * direct checks of the frame tables, MG3 AI helpers, navigation, startup,
 * path targets and registries. Every host double checks its ctx.
 * GPL-2.0-or-later.
 */
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
#include <unistd.h>

#include "q1_addon_fixture.h"
#include "qa/bots/navigation/runtime.h"
#include "qa/content/q1/addons/monsters.h"
#include "qa/content/q1/missionpacks/types.h"

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
    fflush(stdout);                                                     \
    fflush(stderr);                                                     \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      (void)freopen("/dev/null", "w", stderr);                          \
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

/* ---- host doubles layered over the world fixture (ctx = the fixture) ---- */
static q1_addon_fixture AF;
static int host_ctx_errors;
static void *grunt_callbacks; /* qa_q1_register_monster_callbacks closure of the "test" source */
#define HOST_CTX(ctx) do { if ((ctx) != &AF.fx) host_ctx_errors++; } while (0)
static double random_queue[64];
static size_t random_head, random_tail;
static qa_q1_trace trace_result;
static bool walk_move_result = true;
static int damage_count;
static double last_damage;
static qa_actor_id last_damage_target;

static double t_random(void *ctx) {
  HOST_CTX(ctx);
  if (random_head < random_tail) return random_queue[random_head++];
  return AF.fx.random_value;
}
static qa_q1_trace t_trace(void *ctx, qa_q1_trace_request request) {
  HOST_CTX(ctx);
  qa_q1_trace trace = trace_result;
  if (trace.fraction == 1 && !trace.has_actor) trace.end = request.end;
  return trace;
}
static bool t_walk_move(void *ctx, qa_owned_actor actor, double yaw, double distance) {
  (void)actor; (void)yaw; (void)distance;
  HOST_CTX(ctx);
  return walk_move_result;
}
static bool t_damage_allowed(void *ctx, qa_damage_request request) {
  if (ctx != &AF) host_ctx_errors++;
  damage_count++;
  last_damage = request.amount;
  last_damage_target = request.target;
  return true;
}
static void queue_random(double value) { random_queue[random_tail++] = value; }

static void open_world(void) {
  q1af_open(&AF, QA_Q1AD_MG3);
  qa_q1_services *game = AF.fx.game;
  game->host.random = t_random;
  game->host.trace = t_trace;
  game->host.walk_move = t_walk_move;
  AF.fx.damage_allowed = t_damage_allowed;
  AF.fx.damage_allowed_ctx = &AF;
  random_head = random_tail = 0;
  memset(&trace_result, 0, sizeof trace_result);
  trace_result.fraction = 1;
  walk_move_result = true;
  damage_count = 0;
  last_damage = 0;
  game->world = q1af_create(&AF, "worldspawn");
}
static void close_world(void) {
  CHECK(AF.fx.ctx_errors == 0);
  CHECK(q1af_ctx_errors == 0);
  CHECK(host_ctx_errors == 0);
  qa_q1am_release_addon_monsters(AF.fx.game);
  q1af_close(&AF);
  free(grunt_callbacks);
  grunt_callbacks = NULL;
}
static qa_q1_services *game_of(void) { return AF.fx.game; }
static void set_player_origin(qa_actor_id player, qa_vec3 origin) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(AF.fx.actors, player, &owned)) return;
  qa_body_state body = qa_shared_body_table_read(AF.fx.bodies, player).value;
  body.origin = origin;
  qa_shared_body_table_write(AF.fx.bodies, owned, body);
}
static void set_health(qa_owned_actor actor, double health) {
  qa_gameplay_authority_set_health(AF.fx.combat, actor, health);
}
static qa_q1_actor *create_monster_entity(const char *classname) {
  qa_q1_actor *entity = q1af_create(&AF, classname);
  qa_q1_actor_set_damageable(entity, true);
  return entity;
}
/* The path of the newest "sound" event (TS game.sound). */
static const char *last_sound(void) {
  for (size_t i = AF.fx.event_count; i-- > 0;)
    if (strcmp(AF.fx.events[i].kind, "sound") == 0) return AF.fx.events[i].path;
  return "";
}
static size_t count_classname(const char *classname) {
  size_t n = 0;
  qa_q1_services *game = game_of();
  for (size_t i = 0; i < game->entity_count; i++)
    if (strcmp(game->entities[i]->classname, classname) == 0) n++;
  return n;
}
/* game.named.register of an existing name throws (Duplicate Q1 callback). */
static void register_again(const char *name) {
  qa_q1_callback_handlers handlers;
  memset(&handlers, 0, sizeof handlers);
  qa_q1_callback_registry_register(game_of()->named, name, handlers);
}

/* ---- navigation: a real NavigationRuntime over a constructed graph ---- */
static int nav_world_sentinel, nav_host_sentinel;
static int nav_ctx_errors;
static qa_traversal_admission nav_admit(qa_traversal_request request) {
  qa_traversal_admission a;
  memset(&a, 0, sizeof a);
  a.admitted = true;
  a.seconds = 1;
  a.trajectory = malloc(2 * sizeof *a.trajectory); /* TS trajectory: [from, ...waypoints, to] */
  a.trajectory[0] = request.from;
  a.trajectory[1] = request.to;
  a.trajectory_count = 2;
  return a;
}
static qa_traversal_admission nav_world_admit(void *ctx, qa_traversal_request request, qa_navigation_profile profile) {
  (void)profile;
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  return nav_admit(request);
}
static qa_traversal_admission nav_route_admit(void *ctx, qa_traversal_request request) {
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  return nav_admit(request);
}
static qa_navigation_route_prediction nav_begin_route(void *ctx, qa_navigation_profile profile) {
  (void)profile;
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  return (qa_navigation_route_prediction){&nav_world_sentinel, nav_route_admit};
}
static bool nav_entity(void *ctx, qa_navigation_entity_binding binding, qa_navigation_entity_state *out) {
  (void)binding; (void)out;
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  return false;
}
static bool nav_hazard(void *ctx, qa_bounds bounds) {
  (void)bounds;
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  return false;
}
static qa_trace_result nav_scene_trace(void *ctx, qa_trace_query query) {
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  qa_trace_result r;
  memset(&r, 0, sizeof r);
  r.kind = "q1";
  r.fraction = 1;
  r.end = query.end;
  return r;
}
static qa_point_contents_result nav_scene_contents(void *ctx, qa_point_contents_query query) {
  (void)query;
  if (ctx != &nav_world_sentinel) nav_ctx_errors++;
  return (qa_point_contents_result){"q1", 0, 0, 0};
}
static qa_navigation_node nav_nodes[3];
static qa_navigation_edge nav_edges[2];
static int64_t nav_cluster_ids[3] = {1, 2, 3};
static qa_navigation_cluster_list nav_cluster;
static qa_navigation_graph nav_graph;
static qa_navigation_world nav_world;
static qa_navigation_runtime nav_runtime;
static qa_actor_id nav_pass_actor;
static qa_navigation_runtime *nav_for_actor(void *ctx, qa_owned_actor actor) {
  if (ctx != &nav_host_sentinel) nav_ctx_errors++;
  nav_pass_actor = actor.id;
  return &nav_runtime;
}
static void nav_open(void) {
  static const qa_vec3 origins[3] = {{0, 0, 0}, {100, 0, 0}, {300, 0, 0}};
  for (int64_t i = 0; i < 3; i++) {
    qa_navigation_node *node = &nav_nodes[i];
    memset(node, 0, sizeof *node);
    node->id = i + 1;
    node->origin = origins[i];
    node->bounds = (qa_bounds){qa_sub3(origins[i], (qa_vec3){40, 40, 40}), qa_add3(origins[i], (qa_vec3){40, 40, 40})};
    node->radius = 40;
    node->source.kind = QA_NS_CONSTRUCTED;
  }
  /* 1 -> 2 walks; 1 -> 3 only teleports, which walkpathtogoal's edge filter rejects. */
  memset(nav_edges, 0, sizeof nav_edges);
  nav_edges[0].id = 1; nav_edges[0].from = 1; nav_edges[0].to = 2; nav_edges[0].mode = QA_TM_WALK;
  nav_edges[0].start = origins[0]; nav_edges[0].end = origins[1]; nav_edges[0].travel_seconds = 1;
  nav_edges[0].source.kind = QA_NS_CONSTRUCTED;
  nav_edges[1].id = 2; nav_edges[1].from = 1; nav_edges[1].to = 3; nav_edges[1].mode = QA_TM_TELEPORT;
  nav_edges[1].start = origins[0]; nav_edges[1].end = origins[2]; nav_edges[1].travel_seconds = 1;
  nav_edges[1].source.kind = QA_NS_CONSTRUCTED;
  memset(&nav_cluster, 0, sizeof nav_cluster);
  nav_cluster.nodes = nav_cluster_ids;
  nav_cluster.count = 3;
  memset(&nav_graph, 0, sizeof nav_graph);
  nav_graph.map.name = "nav";
  nav_graph.map.digest = "sha256:map-digest";
  nav_graph.profile.monster = true;
  nav_graph.profile.shape.kind = "box";
  nav_graph.profile.shape.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, 32}};
  nav_graph.profile.capabilities = (1u << QA_TM_WALK) | (1u << QA_TM_TELEPORT);
  nav_graph.profile.maximum_step = 18;
  nav_graph.profile.minimum_floor_normal = 0.7;
  nav_graph.profile.maximum_drop = 256;
  nav_graph.profile.team = -1;
  nav_graph.nodes = nav_nodes;
  nav_graph.node_count = 3;
  nav_graph.edges = nav_edges;
  nav_graph.edge_count = 2;
  nav_graph.clusters = &nav_cluster;
  nav_graph.cluster_count = 1;
  memset(&nav_world, 0, sizeof nav_world);
  nav_world.scene.ctx = &nav_world_sentinel;
  nav_world.scene.trace = nav_scene_trace;
  nav_world.scene.point_contents = nav_scene_contents;
  nav_world.pass_actor = &nav_pass_actor;
  nav_world.ctx = &nav_world_sentinel;
  nav_world.admit = nav_world_admit;
  nav_world.begin_route = nav_begin_route;
  nav_world.get_entity = nav_entity;
  nav_world.hazard = nav_hazard;
  CHECK(qa_navigation_runtime_init(&nav_runtime, &nav_graph, &nav_world));
}

/* A plain Mg3Monster over the ordinary MG3 source tables (army frames). */
static qa_q1am_mg3_monster grunt;
static int grunt_lookup_sentinel;
static qa_q1_base_monster *grunt_lookup(void *ctx, qa_q1_actor *entity) {
  if (ctx != &grunt_lookup_sentinel) host_ctx_errors++;
  (void)entity;
  return &grunt.base;
}
static void grunt_init(qa_q1_actor *entity) {
  /* TS registerMonsterCallbacks(game, "test", ...): the source's monster_frame continuations. */
  free(grunt_callbacks);
  grunt_callbacks = qa_q1_register_monster_callbacks(game_of(), "test", grunt_lookup, &grunt_lookup_sentinel);
  qa_q1_base_monster_source source;
  memset(&source, 0, sizeof source);
  source.callback_prefix = "test";
  source.frames = &qa_q1am_ordinary_mg3_frames;
  CHECK(qa_q1am_mg3_monster_init(&grunt, game_of(), entity, &qa_q1am_army_species,
                                 qa_q1_base_as_monster_services(AF.base), &source, NULL, NULL));
}

int main(void) {
  /* Frame tables: counts cross-checked against the bun JSON dump. */
  CHECK(qa_q1am_mg3_lavaman_frames.count == 131);
  CHECK(qa_q1am_mg3_rknight_frames.count == 164);
  CHECK(qa_q1am_mg3_super_shambler_frames.count == 106);
  CHECK(qa_q1am_mg3_lavaman_frames.count + qa_q1am_mg3_rknight_frames.count +
        qa_q1am_mg3_super_shambler_frames.count == 401);
  CHECK(qa_q1am_final_boss_frames.count == 119);
  CHECK(qa_q1am_ghost_frames.count == 64);
  CHECK(qa_q1am_oldnew_frames.count == 127);
  CHECK(qa_q1am_orb_frames.count == 17);
  CHECK(qa_q1am_szombie_frames.count == 193);
  CHECK(qa_q1am_demodog_frames.count == 86);
  CHECK(qa_q1am_infected_frames.count == 113);
  const qa_q1_monster_frame_entry *first = &qa_q1am_final_boss_frames.entries[0];
  CHECK(strcmp(first->name, "boss_final_rise1") == 0 && first->frame.frame == 0 &&
        strcmp(first->frame.next, "boss_final_rise2") == 0 && first->frame.operation_count == 1 &&
        first->frame.operations[0].kind == QA_Q1_OP_ACTION);
  const qa_q1_monster_frame *dog = qa_q1_monster_frame_table_get(&qa_q1am_demodog_frames, "demodog_atta4");
  CHECK(dog != NULL && dog->frame == 3 && dog->operation_count == 2 &&
        dog->operations[0].kind == QA_Q1_OP_SOUND &&
        strcmp(dog->operations[1].name, "demodog_bite") == 0);
  const qa_q1_monster_frame *corpse = qa_q1_monster_frame_table_get(&qa_q1am_infected_frames, "hknight_corpse2");
  CHECK(corpse != NULL && corpse->frame == 62 && corpse->operations[0].kind == QA_Q1_OP_SOLID);
  /* Heavy tables: every next resolves and every action has a handler. */
  const qa_q1am_heavy_definition *defs[2] = {&qa_q1am_super_shambler_definition, &qa_q1am_rune_knight_definition};
  for (int d = 0; d < 2; d++) {
    const qa_q1am_heavy_definition *def = defs[d];
    for (size_t i = 0; i < def->frames->count; i++) {
      const qa_q1_monster_frame *frame = &def->frames->entries[i].frame;
      bool next_ok = qa_q1_monster_frame_table_has(def->frames, frame->next);
      for (size_t a = 0; a < def->action_count && !next_ok; a++)
        next_ok = strcmp(def->actions[a].name, frame->next) == 0;
      CHECK(next_ok);
      for (size_t o = 0; o < frame->operation_count; o++) {
        if (frame->operations[o].kind != QA_Q1_OP_ACTION) continue;
        bool found = false;
        for (size_t a = 0; a < def->action_count; a++)
          found = found || strcmp(def->actions[a].name, frame->operations[o].name) == 0;
        CHECK(found);
      }
    }
  }
  /* Sphere points. */
  qa_vec3 p0 = qa_q1am_sphere_point(0);
  CHECK(p0.x == 0.056 && p0.y == 0.13 && p0.z == -0.99);
  qa_vec3 p99 = qa_q1am_sphere_point(99);
  CHECK(p99.x == 0.297 && p99.y == -0.098 && p99.z == -0.95);
  CHECK_ABORTS(qa_q1am_sphere_point(100));
  CHECK_ABORTS(qa_q1am_sphere_point(-1));
  /* Vector helpers (foundation yawFor/normalize, mission-pack velocityAngles). */
  CHECK(qa_q1_yaw_for((qa_vec3){0, -1, 0}) == 270);
  qa_vec3 va = qa_q1mp_velocity_angles((qa_vec3){0, 0, 5});
  CHECK(va.x == 90 && va.y == 0 && va.z == 0);
  qa_vec3 n = qa_q1_normalize((qa_vec3){0, 0, 0});
  CHECK(n.x == 0 && n.y == 0 && n.z == 0);

  open_world();
  qa_q1_services *game = game_of();
  /* Actor field helpers (context.setNumber writes String(Math.fround(value))). */
  qa_q1_actor *probe = q1af_create(&AF, "probe");
  qa_q1ad_set_number(AF.context, probe, "hp", 0.1);
  CHECK(qa_q1_actor_number(probe, "hp") == (double)(float)0.1);
  (void)qa_q1_actor_field_set(probe, "v", "1.5 -2 3");
  qa_vec3 parsed = qa_q1_actor_vector(probe, "v");
  CHECK(parsed.x == 1.5 && parsed.y == -2 && parsed.z == 3);
  CHECK(qa_q1_actor_number_or(probe, "missing", 7) == 7);

  /* MG3 AI: attackFinished fround staging and range categories. */
  qa_owned_actor player = q1_world_fixture_admit_player(&AF.fx, (qa_vec3){300, 0, 0}, 100);
  qa_q1_actor *grunt_entity = create_monster_entity("monster_army");
  grunt_init(grunt_entity);
  qa_q1_base_monster *m = &grunt.base;
  game->time = 1.23456789;
  qa_q1_base_monster_attack_finished(m, 0.5);
  CHECK(m->state->attack_finished == (double)(float)(1.23456789 + 0.5));
  CHECK(!m->state->refired);
  qa_q1_base_monster_set_enemy(m, &player.id);
  /* eye distance: origins 300 apart, same view offset. */
  CHECK(qa_q1am_mg3_range_category(m, NULL) == 1);
  set_player_origin(player.id, (qa_vec3){50, 0, 0});
  CHECK(qa_q1am_mg3_range_category(m, NULL) == 0);
  set_player_origin(player.id, (qa_vec3){2000, 0, 0});
  CHECK(qa_q1am_mg3_range_category(m, NULL) == 3);
  /* Sight sound: enforcer random selection (choice 0 -> sight3). */
  qa_q1ad_set_text(&grunt_entity->classname, "monster_enforcer");
  queue_random(0);
  qa_q1am_mg3_sight_sound(m);
  CHECK(strcmp(last_sound(), "enforcer/sight3.wav") == 0);
  /* Contents damage: lava zombie takes 120 and reports death check. */
  qa_q1ad_set_text(&grunt_entity->classname, "monster_zombie");
  grunt_entity->water_level = 2;
  grunt_entity->water_type = -5;
  set_health(grunt_entity->actor, 1000);
  int before_damage = damage_count;
  CHECK(qa_q1am_mg3_check_contents_damage(m) == true);
  CHECK(damage_count == before_damage + 1 && last_damage == 120);
  /* Second call inside dmgtime window does nothing. */
  CHECK(qa_q1am_mg3_check_contents_damage(m) == false);
  CHECK(damage_count == before_damage + 1);
  close_world();

  /* Army attack and pain against the host trace. */
  open_world();
  game = game_of();
  player = q1_world_fixture_admit_player(&AF.fx, (qa_vec3){100, 0, 0}, 100);
  grunt_entity = create_monster_entity("monster_army");
  grunt_init(grunt_entity);
  m = &grunt.base;
  qa_q1_base_monster_set_enemy(m, &player.id);
  qa_q1ad_set_number(AF.context, game->world, "enemy_range", 0);
  trace_result.has_actor = true;
  trace_result.actor = player.id;
  queue_random(0.1); /* under 0.9 chance */
  queue_random(0.5); /* attackFinished delay */
  queue_random(0.9); /* no lefty flip */
  CHECK(qa_q1am_army_attack(m) == true);
  CHECK(strcmp(m->current_frame, "army_atk1") == 0);
  queue_random(0.1);
  qa_q1am_army_pain(m, &player.id, 10, false);
  CHECK(strcmp(m->current_frame, "army_pain1") == 0);
  CHECK(strcmp(last_sound(), "soldier/pain1.wav") == 0);

  /* Infected species resolution. */
  qa_q1_actor *inf = q1af_create(&AF, "monster_army_infected");
  (void)qa_q1_actor_field_set(inf, "infected.kind", "army");
  qa_q1_base_monster_species spec;
  qa_q1am_infected_species(inf, &spec);
  CHECK(strcmp(spec.species, "army") == 0 && spec.health == 30);
  qa_q1ad_set_number(AF.context, inf, "infected.transformed", 1);
  qa_q1am_infected_species(inf, &spec);
  CHECK(strcmp(spec.species, "zombie") == 0 && spec.missile == NULL && spec.melee);
  (void)qa_q1_actor_field_set(inf, "infected.kind", "hellknight");
  qa_q1ad_set_number(AF.context, inf, "infected.transformed", 0);
  inf->spawnflags = 65536;
  qa_q1am_infected_species(inf, &spec);
  CHECK(strcmp(spec.stand, "hknight_corpse1") == 0 && strcmp(spec.run, "hknight_corpse1_rise0") == 0);
  (void)qa_q1_actor_field_set(inf, "infected.kind", "bogus");
  CHECK_ABORTS(qa_q1am_infected_kind(inf));
  close_world();

  /* Heavy: Super Shambler blast counts (TS: 11 then 20 knightspikes). */
  open_world();
  game = game_of();
  player = q1_world_fixture_admit_player(&AF.fx, (qa_vec3){500, 0, 0}, 100);
  qa_q1am_mg3_heavy *heavy = qa_q1am_register_mg3_heavy(AF.context);
  qa_q1_actor *sham_entity = q1af_spawn(&AF, "monster_super_shambler");
  qa_q1am_heavy_monster *sham = qa_q1am_mg3_heavy_require(heavy, sham_entity);
  qa_q1_base_monster_set_enemy(&sham->base.base, &player.id);
  qa_q1_base_monster_play(&sham->base.base, "supsham_smash10");
  CHECK(count_classname("knightspike") == 11);
  qa_q1_base_monster_play(&sham->base.base, "supsham_swingl7");
  CHECK(count_classname("knightspike") == 20);
  /* Rune Knight burst counter setup frame. */
  qa_q1_actor *rk_entity = q1af_spawn(&AF, "monster_ranged_knight");
  qa_q1am_heavy_monster *rk = qa_q1am_mg3_heavy_require(heavy, rk_entity);
  qa_q1_base_monster_set_enemy(&rk->base.base, &player.id);
  qa_q1_base_monster_play(&rk->base.base, "rknight_magicb6");
  CHECK(qa_q1_actor_number(rk_entity, "ammo_nails") == 3); /* floor(0.4*8+0.5) */
  /* Heavy checkpoint capture/restore through the state extension path. */
  qa_save_value *captured = qa_q1_base_monster_capture(&rk->base.base);
  qa_save_reader reader;
  qa_save_reader_init(&reader, captured, "mg3:heavy");
  qa_q1_base_monster_restore(&rk->base.base, &reader);
  qa_save_reader_destroy(&reader);
  qa_save_value_free(captured);
  CHECK(strcmp(rk->base.base.current_frame, "rknight_magicb6") == 0);
  /* Lava Man pain plays the shock sequence on first hit. */
  const char *lava_fields[] = {"targetname", "lava1"};
  qa_q1_actor *lava_entity = q1af_spawn_fields(&AF, "monster_lava_man", lava_fields, 1);
  qa_q1am_heavy_monster *lava = qa_q1am_mg3_heavy_require(heavy, lava_entity);
  qa_q1_base_monster_pain(&lava->base.base, &player.id, 1);
  CHECK(lava_entity->count == 1);
  CHECK(strcmp(lava->base.base.current_frame, "lavaman_shocka1") == 0);
  /* Heavy lightning damage hits a repeated trace actor only once. */
  trace_result.has_actor = true;
  trace_result.actor = player.id;
  trace_result.end = (qa_vec3){10, 0, 0};
  int damages = damage_count;
  qa_q1am_heavy_lightning_damage(game, sham_entity->actor.id, QA_Q1_ZERO, (qa_vec3){10, 0, 0}, 10);
  CHECK(damage_count == damages + 1 && last_damage == 10);
  close_world();

  /* Navigation: in-progress walk, duplicate registration, missing host. */
  open_world();
  game = game_of();
  player = q1_world_fixture_admit_player(&AF.fx, (qa_vec3){100, 0, 0}, 100);
  grunt_entity = create_monster_entity("monster_army");
  grunt_init(grunt_entity);
  m = &grunt.base;
  CHECK_ABORTS(qa_q1am_walk_mg3_path_to_goal(m, 5, (qa_vec3){100, 0, 0}));
  nav_open();
  qa_q1am_nav_host nav_host = {&nav_host_sentinel, nav_for_actor};
  qa_q1am_register_mg3_monster_navigation(game, nav_host);
  CHECK(qa_q1am_walk_mg3_path_to_goal(m, 5, (qa_vec3){100, 0, 0}) == QA_Q1AM_PATH_IN_PROGRESS);
  /* The walk edge filter admits walk and rejects teleport: the 1 -> 3 goal has no walking route. */
  CHECK(qa_q1am_walk_mg3_path_to_goal(m, 5, (qa_vec3){300, 0, 0}) == QA_Q1AM_PATH_ERROR);
  CHECK(qa_q1am_walk_mg3_path_to_goal(m, 5, (qa_vec3){0, 0, 0}) == QA_Q1AM_PATH_REACHED_GOAL);
  CHECK_ABORTS(qa_q1am_register_mg3_monster_navigation(game, nav_host));
  CHECK(nav_ctx_errors == 0);

  /* Startup: activator selection. */
  qa_actor_id resolved;
  CHECK(qa_q1am_mg3_monster_activator(game, NULL, &resolved));
  CHECK(qa_same_actor(resolved, player.id));
  set_health(player, 0);
  CHECK(!qa_q1am_mg3_monster_activator(game, NULL, &resolved));
  set_health(player, 100);
  qa_q1am_release_mg3_monster_navigation(game);
  qa_navigation_runtime_destroy(&nav_runtime);
  close_world();

  /* Path targets: spawn validation errors. */
  open_world();
  game = game_of();
  qa_q1am_register_mg3_path_targets(AF.context);
  qa_q1_actor *corner = q1af_create(&AF, "path_corner");
  CHECK_ABORTS(qa_q1_spawn_entity(game, corner));
  qa_q1ad_set_text(&corner->targetname, "corner1");
  qa_q1_spawn_entity(game, corner);
  CHECK(strcmp(corner->solid, "trigger") == 0);
  CHECK(qa_q1_get_body(game, corner).bounds.max.x == 8);
  close_world();

  /* Boss registry: ghost spawn initializes. */
  open_world();
  game = game_of();
  qa_q1am_register_ghost(AF.context);
  queue_random(0.0); /* death choice */
  queue_random(0.5); /* wander direction */
  queue_random(0.0); /* count */
  qa_q1_actor *ghost_entity = q1af_spawn(&AF, "monster_ghost");
  CHECK(strcmp(qa_q1_actor_text(ghost_entity, "ghost.death"), "ghost_diea1") == 0);
  CHECK(qa_q1_health(game, ghost_entity->actor.id) == 10);
  CHECK(ghost_entity->wait == 128);
  close_world();

  /* Demodog spawn state. */
  open_world();
  game = game_of();
  player = q1_world_fixture_admit_player(&AF.fx, (qa_vec3){100, 0, 0}, 100);
  qa_q1am_demodog_registry *dogs = qa_q1am_register_mg3_demodog(AF.context);
  qa_q1_actor *dog_entity = q1af_spawn(&AF, "monster_demodog");
  CHECK(strcmp(dog_entity->classname, "monster_dog") == 0);
  CHECK(strcmp(qa_q1_actor_text(dog_entity, "allowPathFind"), "1") == 0);
  CHECK(qa_q1_health(game, dog_entity->actor.id) == 25);
  CHECK(qa_q1am_demodog_registry_get(dogs, dog_entity->actor) != NULL);
  close_world();

  /* Top-level registration installs ordinary + mg3 spawns once. */
  open_world();
  game = game_of();
  const size_t spawners = game->spawner_count;
  qa_q1am_register_addon_monsters(AF.context);
  CHECK(game->spawner_count - spawners > 20);
  CHECK_ABORTS(register_again("mg3:final:T_RockTouch2"));
  CHECK_ABORTS(register_again("mg3:bosses:actual_sphere"));
  CHECK_ABORTS(register_again("mg3:t_movetarget"));
  close_world();

  printf("test_q1_addon_monsters: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
