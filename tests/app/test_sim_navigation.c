/* Checks for src/app/bootstrap/simulation/navigation.c and
 * monster_navigation.c (TS navigation.ts / monster-navigation.ts).
 * botNavigationProfile expectations come from running the TS under bun;
 * entity/hazard/checkpoint cases drive createApplicationBotNavigation
 * over an empty Q1 world, a loose mount and scripted SharedSimulation
 * members. GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/simulation/monster_navigation.h"
#include "qa/app/bootstrap/simulation/navigation.h"
#include "qa/content/q1/addons/monsters/ai/path.h"
#include "qa/foundation/support.h"
#include "../content/q1_world_fixture.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    checks++;                                                             \
    if (!(cond)) {                                                        \
      failures++;                                                         \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)
static bool same_bounds(qa_bounds a, qa_bounds b) {
  return a.min.x == b.min.x && a.min.y == b.min.y && a.min.z == b.min.z && a.max.x == b.max.x && a.max.y == b.max.y && a.max.z == b.max.z;
}
static qa_bounds box(double x0, double y0, double z0, double x1, double y1, double z1) { return (qa_bounds){{x0, y0, z0}, {x1, y1, z1}}; }

/* ---- botNavigationProfile (bun: tests/.. scratch profile.ts) ---- */
static qa_app_sim_locomotion_player locomotion(const char *kind, qa_game_family character, bool source) {
  qa_app_sim_locomotion_player p; memset(&p, 0, sizeof p);
  p.profile.kind = kind; p.character = character; p.world_gravity = 600;
  p.standing_bounds = qa_app_sim_player_standing_bounds(character);
  if (source) {
    p.has_source_movement = true; p.source_movement.has_trace_mask = true; p.source_movement.trace_mask = 0x2000001;
    p.source_movement.has_fixed_msec = true; p.source_movement.fixed_msec = 8; p.source_movement.no_footsteps = true;
  }
  return p;
}
static uint32_t modes(const qa_travel_mode *list, size_t count) {
  uint32_t out = 0; for (size_t i = 0; i < count; i++) out |= 1u << list[i]; return out;
}
static void test_profile(void) {
  const qa_travel_mode base[] = {QA_TM_WALK, QA_TM_JUMP, QA_TM_DROP, QA_TM_SWIM, QA_TM_WATER_JUMP, QA_TM_LADDER, QA_TM_MOVER};
  const qa_travel_mode crouch[] = {QA_TM_WALK, QA_TM_CROUCH, QA_TM_JUMP, QA_TM_DROP, QA_TM_SWIM, QA_TM_WATER_JUMP, QA_TM_LADDER, QA_TM_MOVER};
  const qa_travel_mode q3[] = {QA_TM_WALK, QA_TM_CROUCH, QA_TM_JUMP, QA_TM_DROP, QA_TM_SWIM, QA_TM_WATER_JUMP, QA_TM_LADDER, QA_TM_MOVER, QA_TM_TELEPORT, QA_TM_JUMP_PAD};
  const qa_bounds q1b = box(-16, -16, -24, 16, 16, 32);
  for (int i = 0; i < 2; i++) {
    qa_navigation_profile p = qa_app_sim_bot_navigation_profile(locomotion(i == 0 ? "q1-netquake" : "q1-quakeworld", QA_GAME_FAMILY_Q1, false));
    CHECK(p.capabilities == modes(base, 7));
    CHECK(!p.has_crouched_shape && strcmp(p.shape.kind, "box") == 0 && same_bounds(p.shape.bounds, q1b));
    CHECK(strcmp(p.policy.kind, "q1") == 0 && strcmp(p.policy.move, "normal") == 0 && !p.policy.has_hull);
    CHECK(p.maximum_step == 18 && p.minimum_floor_normal == 0.7 && p.maximum_drop == 128 && p.team == -1 && !p.monster);
    CHECK(p.movement.parameters.gravity == 600);
  }
  for (int i = 0; i < 2; i++) {
    qa_navigation_profile p = qa_app_sim_bot_navigation_profile(locomotion(i == 0 ? "q2-classic" : "q2-rerelease", QA_GAME_FAMILY_Q2, false));
    CHECK(p.capabilities == modes(crouch, 8));
    CHECK(p.has_crouched_shape && same_bounds(p.crouched_shape.bounds, box(-16, -16, -24, 16, 16, 4)));
    CHECK(strcmp(p.policy.kind, "q2") == 0 && p.policy.contents_mask == 33619971);
    CHECK(strcmp(p.policy.leaf_contents, i == 0 ? "stored" : "merged") == 0);
  }
  qa_navigation_profile p = qa_app_sim_bot_navigation_profile(locomotion("q3", QA_GAME_FAMILY_Q3, true));
  CHECK(p.capabilities == modes(q3, 10));
  CHECK(same_bounds(p.shape.bounds, box(-15, -15, -24, 15, 15, 32)) && same_bounds(p.crouched_shape.bounds, box(-15, -15, -24, 15, 15, 16)));
  CHECK(strcmp(p.policy.kind, "q3") == 0 && p.policy.contents_mask == 33554433 && p.policy.curves && p.policy.player_curve_clip);
  CHECK(p.movement.has_fixed_milliseconds && p.movement.fixed_milliseconds == 8);
  p = qa_app_sim_bot_navigation_profile(locomotion("q3", QA_GAME_FAMILY_Q1, false));
  CHECK(p.capabilities == modes(q3, 10) && same_bounds(p.crouched_shape.bounds, box(-16, -16, -24, 16, 16, 4)));
  CHECK(p.policy.contents_mask == 33619969 && !p.movement.has_fixed_milliseconds);
}

/* ---- scripted SharedSimulation over an empty Q1 world ---- */
static qa_q1_leaf leaves[2];
static qa_q1_world_model models[1];
static double headnodes[4] = {-2, -1, -1, -1};
static qa_q1_world_geometry geometry;
static void build_empty_world(void) {
  qa_bounds b = box(-256, -256, -256, 256, 256, 256);
  leaves[0] = (qa_q1_leaf){-2, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  leaves[1] = (qa_q1_leaf){-1, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  memset(&models, 0, sizeof models);
  models[0].bounds = b; models[0].headnodes = (qa_double_view){headnodes, 4}; models[0].visible_leaves = 1;
  memset(&geometry, 0, sizeof geometry);
  geometry.format = "bsp29"; geometry.entities = "";
  geometry.leaves = leaves; geometry.leaf_count = 2;
  geometry.models = models; geometry.model_count = 1;
}
static int sentinel;
static int ctx_errors;
#define SENTINEL_CHECK(ctx) do { if ((ctx) != &sentinel) ctx_errors++; } while (0)
typedef struct host_state {
  q1_world_fixture fx;
  qa_shared_scene_queries *scene;
  qa_content_mounted *mounted;
  qa_executable_recipe recipe;
  double time;
  qa_owned_actor player_actor; bool has_players;
  int player_identity;
  qa_q2_game_services q2game; qa_q2_foundation_host q2host;
  qa_q2_base_entity_module *base; qa_q2_mover_module *movers;
  qa_q2_entity q2entities[4]; size_t q2count;
  double q2now;
  int connections_calls; qa_navigation_profile connection_profile;
  int predict_calls; const void *predicted_identity; qa_navigation_runtime *predicted_runtime;
} host_state;
static host_state H;
static double h_time(void *ctx) { SENTINEL_CHECK(ctx); return H.time; }
static const qa_owned_actor *h_players(void *ctx, size_t *n) { SENTINEL_CHECK(ctx); *n = H.has_players ? 1 : 0; return &H.player_actor; }
static bool h_movement_player(void *ctx, qa_owned_actor actor, qa_app_sim_nav_player *out) {
  SENTINEL_CHECK(ctx);
  if (!qa_same_actor(actor.id, H.player_actor.id)) return false;
  memset(out, 0, sizeof *out);
  out->identity = &H.player_identity;
  out->value.client = (qa_client_id){0}; out->value.client.slot = 0;
  out->value.actor = actor;
  out->value.profile.kind = "q1-netquake";
  out->value.character = QA_GAME_FAMILY_Q1;
  out->value.standing_bounds = qa_app_sim_player_standing_bounds(QA_GAME_FAMILY_Q1);
  out->value.bounds = out->value.standing_bounds;
  out->value.world_gravity = 800;
  return true;
}
static bool h_q3_guest(void *ctx, qa_app_q3_guest_movement_host *out) { SENTINEL_CHECK(ctx); (void)out; return false; }
static const qa_app_q3_player *h_q3_guest_players(void *ctx, size_t *n) { SENTINEL_CHECK(ctx); *n = 0; return NULL; }
static qa_q1_services *h_q1_game(void *ctx) { SENTINEL_CHECK(ctx); return H.fx.game; }
static qa_q2_entity *q2_entity(void *ctx, const qa_actor_id *actor) {
  SENTINEL_CHECK(ctx);
  for (size_t i = 0; actor != NULL && i < H.q2count; i++) if (qa_same_actor(H.q2entities[i].actor.id, *actor)) return &H.q2entities[i];
  return NULL;
}
static qa_q2_entity *const *q2_entities(void *ctx, size_t *n) { SENTINEL_CHECK(ctx); *n = 0; return NULL; }
static double q2_now(void *ctx) { SENTINEL_CHECK(ctx); return H.q2now; }
static bool h_q2_source(void *ctx, qa_app_sim_nav_q2_source *out) {
  SENTINEL_CHECK(ctx);
  memset(out, 0, sizeof *out);
  out->game = &H.q2game; out->base_entities = H.base; out->movers = H.movers;
  return true;
}
static bool h_q3_source(void *ctx, qa_app_sim_nav_q3_source *out) { SENTINEL_CHECK(ctx); (void)out; return false; }
static qa_combat_state_result p_combat(void *ctx, qa_actor_id actor) {
  SENTINEL_CHECK(ctx); (void)actor;
  qa_combat_state_result r; memset(&r, 0, sizeof r); r.has_value = true; r.value.health = 100; return r;
}
static qa_app_sim_prediction_simulation h_prediction(void *ctx) {
  SENTINEL_CHECK(ctx);
  qa_app_sim_prediction_simulation view; memset(&view, 0, sizeof view);
  view.ctx = &sentinel; view.combat_read = p_combat; view.scene = qa_shared_as_scene_queries(H.scene); view.time_seconds = H.time;
  return view;
}
static const qa_navigation_connection *h_connections(void *ctx, qa_navigation_profile profile, size_t *n) {
  SENTINEL_CHECK(ctx); H.connections_calls++; H.connection_profile = profile; *n = 0; return NULL;
}
static qa_bot_travel_prediction_result h_predict(void *ctx, const qa_app_sim_nav_player *player, qa_bot_movement_prediction query,
    qa_navigation_runtime *runtime) {
  SENTINEL_CHECK(ctx); H.predict_calls++; H.predicted_identity = player->identity; H.predicted_runtime = runtime;
  qa_bot_travel_prediction_result r; memset(&r, 0, sizeof r); r.end = query.origin; r.frames = 7; return r;
}
static qa_content_mounted *c_for_content(void *ctx, qa_content_id content) { SENTINEL_CHECK(ctx); (void)content; return H.mounted; }

static qa_app_sim_navigation_simulation simulation_view(void) {
  qa_app_sim_navigation_simulation s; memset(&s, 0, sizeof s);
  s.ctx = &sentinel; s.recipe = &H.recipe;
  s.world.kind = QA_DECODED_WORLD_Q1; s.world.as.q1 = &geometry;
  s.scene = H.scene; s.inventory = H.fx.inventory;
  s.time_seconds = h_time; s.players = h_players; s.movement_player = h_movement_player;
  s.q3_guest = h_q3_guest; s.q3_guest_players = h_q3_guest_players; s.q1_game = h_q1_game;
  s.q2_source = h_q2_source; s.q3_source = h_q3_source; s.prediction = h_prediction;
  s.train_connections = h_connections; s.predict_movement = h_predict;
  return s;
}
static qa_app_sim_navigation_content content_view(void) {
  qa_app_sim_navigation_content c; memset(&c, 0, sizeof c);
  c.ctx = &sentinel; c.recipe = &H.recipe; c.mounts = H.mounted; c.for_content = c_for_content;
  return c;
}
static char root[512];
static char *content_id, *mount_id, *plan_id, *recipe_path, *recipe_digest, *recipe_member, *recipe_id;
static qa_content_mount mounts[1];
static qa_mount_id order[1];
static qa_resolved_mount_plan plan;
static void setup(void) {
  memset(&H, 0, sizeof H);
  CHECK(q1_world_fixture_create(&H.fx, q1_world_fixture_options()));
  build_empty_world();
  qa_decoded_world world; memset(&world, 0, sizeof world);
  world.kind = QA_DECODED_WORLD_Q1; world.as.q1 = &geometry;
  H.scene = qa_create_scene_queries(world);
  CHECK(H.scene != NULL);
  const char *tmp = getenv("TMPDIR");
  snprintf(root, sizeof root, "%s/qa-nav-%d", tmp != NULL ? tmp : "/tmp", (int)getpid());
  char path[600];
  mkdir(root, 0700); snprintf(path, sizeof path, "%s/maps", root); mkdir(path, 0700);
  snprintf(path, sizeof path, "%s/maps/test.bsp", root);
  FILE *file = fopen(path, "wb"); CHECK(file != NULL);
  if (file) { fputs("not a real bsp", file); fclose(file); }
  qa_content_identity identity = {QA_GAME_FAMILY_Q1, "test", "navigation", "1"};
  content_id = qa_create_content_id(identity);
  mount_id = qa_create_mount_id("navigation-test", "main");
  plan_id = qa_create_mount_plan_id("navigation-test", "r1");
  memset(mounts, 0, sizeof mounts);
  mounts[0].kind = QA_CONTENT_MOUNT_LOOSE;
  mounts[0].as.loose.identity = qa_create_mount_identity(mount_id, content_id, 0);
  mounts[0].as.loose.root_path = root;
  order[0] = mount_id;
  memset(&plan, 0, sizeof plan);
  plan.id = plan_id; plan.mounts = mounts; plan.mount_count = 1; plan.default_order = order; plan.default_order_count = 1;
  qa_open_mount_options options; memset(&options, 0, sizeof options);
  H.mounted = qa_open_mount_plan(&plan, &options);
  CHECK(H.mounted != NULL);
  const qa_resolved_resource_reference *reference = NULL;
  CHECK(qa_content_mounted_resolve(H.mounted, "maps/test.bsp", &reference));
  memset(&H.recipe, 0, sizeof H.recipe);
  /* The recipe owns its strings (the mounter's reference storage is not stable). */
  if (reference) {
    H.recipe.map.geometry = *reference;
    recipe_path = strdup(reference->requested_path); recipe_digest = strdup(reference->digest);
    H.recipe.map.geometry.requested_path = recipe_path; H.recipe.map.geometry.digest = recipe_digest;
    recipe_member = strdup(reference->provenance.member_path); recipe_id = strdup(reference->id);
    H.recipe.map.geometry.provenance.member_path = recipe_member; H.recipe.map.geometry.id = recipe_id;
  }
  H.player_actor = q1_world_fixture_admit_player(&H.fx, (qa_vec3){0, 0, 0}, 100);
  H.has_players = true;
  H.q2host.ctx = &sentinel; H.q2host.now = q2_now;
  H.q2game.ctx = &sentinel; H.q2game.host = &H.q2host; H.q2game.entity = q2_entity; H.q2game.entities = q2_entities;
  qa_q2_base_entity_hooks hooks; memset(&hooks, 0, sizeof hooks);
  H.base = qa_q2_base_entity_module_create(hooks);
  qa_q2_mover_hooks mover_hooks; memset(&mover_hooks, 0, sizeof mover_hooks);
  H.movers = qa_q2_mover_module_create(mover_hooks);
}
static void teardown(void) {
  qa_q2_mover_module_destroy(H.movers);
  qa_q2_base_entity_module_destroy(H.base);
  qa_content_mounted_close(H.mounted);
  qa_content_mounted_destroy(H.mounted);
  qa_shared_scene_queries_destroy(H.scene);
  q1_world_fixture_destroy(&H.fx);
  char path[600];
  snprintf(path, sizeof path, "%s/maps/test.bsp", root); unlink(path);
  snprintf(path, sizeof path, "%s/maps", root); rmdir(path); rmdir(root);
  free(content_id); free(mount_id); free(plan_id); free(recipe_path); free(recipe_digest); free(recipe_member); free(recipe_id);
}
static void link_model(qa_actor_id actor, qa_vec3 origin, qa_bounds absolute, double model, const char *role) {
  qa_linked_body body; memset(&body, 0, sizeof body);
  body.actor = actor; body.state.origin = origin; body.absolute_bounds = absolute; body.state.velocity = (qa_vec3){1, 2, 3};
  qa_actor_collision collision; memset(&collision, 0, sizeof collision);
  collision.family = "q1"; collision.shape_kind = model >= 0 ? "model" : "box"; collision.shape_model = model >= 0 ? model : 0;
  collision.role = role;
  qa_shared_link(H.scene, body, collision);
}
static void touch_noop(void *ctx, qa_q1_services *game, qa_q1_actor *entity, qa_actor_id other, const qa_vec3 *normal, const qa_touch_surface *surface) {
  (void)ctx; (void)game; (void)entity; (void)other; (void)normal; (void)surface;
}
static void q2_touch_noop(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_touch_contact contact) { (void)ctx; (void)e; (void)g; (void)contact; }

static double remap_shift(void *ctx, double saved) { SENTINEL_CHECK(ctx); return saved + 1; }
static double remap_negative(void *ctx, double saved) { SENTINEL_CHECK(ctx); (void)saved; return -1; }
static double remap_same(void *ctx, double saved) { SENTINEL_CHECK(ctx); (void)saved; return 0; }
typedef struct restore_call { qa_app_sim_application_bot_navigation *nav; const qa_save_value *value; double (*remap)(void *, double); } restore_call;
static void run_restore(void *ctx) { restore_call *c = ctx; qa_app_sim_application_bot_navigation_restore_checkpoint(c->nav, c->value, c->remap, &sentinel); }

static void test_application_navigation(void) {
  setup();
  qa_app_sim_application_bot_navigation *nav = qa_app_sim_create_application_bot_navigation(content_view(), simulation_view());
  CHECK(nav != NULL);
  CHECK(H.connections_calls == 1 && strcmp(H.connection_profile.movement.kind, "q1-netquake") == 0);
  CHECK(H.connection_profile.movement.parameters.gravity == 800);
  const qa_selected_bot_navigation_bots_a *selected = qa_app_sim_application_bot_navigation_selected(nav);
  CHECK(selected->runtime == qa_app_sim_application_bot_navigation_runtime(nav));
  /* Q1 has no crouched shape: crouchedBounds falls back to first.standingBounds. */
  CHECK(same_bounds(selected->crouched_bounds, box(-16, -16, -24, 16, 16, 32)));
  int64_t weapon = 5;
  CHECK(!selected->travel_weapon(selected->ctx, 0, "rocket-jump", &weapon) && weapon == 5);
  /* World revision is timeSeconds * 1000 (TS getter). */
  qa_navigation_runtime *base = qa_app_sim_application_bot_navigation_runtime(nav);
  int64_t generation = qa_navigation_runtime_generation(base);
  H.time = 1.5;
  CHECK(qa_navigation_runtime_generation(base) == generation + 1 && base->world_revision == 1500);
  CHECK(qa_navigation_runtime_generation(base) == generation + 1);
  const qa_navigation_world *world = base->world;
  CHECK(world->pass_actor != NULL && qa_same_actor(*world->pass_actor, H.player_actor.id));

  /* Q1 door: gold key with a toucher and no key held is locked. */
  qa_q1_actor *door = qa_q1_create(H.fx.game, "func_door", NULL, NULL);
  door->solid = "bsp"; door->state = "bottom"; door->spawnflags = 8; door->touch.fn = touch_noop;
  door->pos1 = (qa_vec3){0, 0, 64}; door->pos2 = (qa_vec3){0, 0, 0};
  qa_bounds absolute = box(-33, -9, -1, 33, 9, 97);
  link_model(door->actor.id, (qa_vec3){0, 0, 0}, absolute, 1, "solid");
  qa_navigation_entity_binding binding; memset(&binding, 0, sizeof binding);
  binding.bounds = box(-33, -9, 63, 33, 9, 161); /* the linked box at pos1 */
  qa_navigation_entity_state state;
  CHECK(world->get_entity(world->ctx, binding, &state));
  CHECK(qa_same_actor(state.actor, door->actor.id) && state.enabled && state.locked && !state.has_destination && !state.has_elevator);
  CHECK(same_bounds(state.bounds, absolute) && state.velocity.x == 1 && state.velocity.z == 3);
  door->spawnflags = 0;
  CHECK(world->get_entity(world->ctx, binding, &state) && !state.locked);
  door->max_health = 10;
  CHECK(world->get_entity(world->ctx, binding, &state) && state.locked);
  door->max_health = 0; door->state = "top"; free(door->targetname); door->targetname = strdup("gate");
  door->has_move = true; door->move.destination = (qa_vec3){4, 5, 6};
  CHECK(world->get_entity(world->ctx, binding, &state) && !state.locked && state.has_destination && state.destination.y == 5);
  /* A binding that matches no mover endpoint is TS null. */
  qa_navigation_entity_binding missing = binding; missing.bounds.min.x = -999;
  CHECK(!world->get_entity(world->ctx, missing, &state));
  /* func_plat: elevator from pos2 (bottom) to pos1 (top); locked while inactive. */
  qa_q1_actor *plat = qa_q1_create(H.fx.game, "func_plat", NULL, NULL);
  plat->solid = "bsp"; plat->state = "up"; plat->activated = false;
  plat->pos1 = (qa_vec3){100, 0, 128}; plat->pos2 = (qa_vec3){100, 0, 0};
  link_model(plat->actor.id, (qa_vec3){100, 0, 8}, box(80, -20, 0, 120, 20, 16), 2, "solid");
  qa_navigation_entity_binding plat_binding; memset(&plat_binding, 0, sizeof plat_binding);
  plat_binding.has_model = true; plat_binding.model = 2;
  CHECK(world->get_entity(world->ctx, plat_binding, &state));
  CHECK(state.has_elevator && state.elevator_phase == 1 && state.elevator_top.z == 128 && state.elevator_bottom.z == 0 && state.elevator_origin.z == 8);
  CHECK(state.locked);
  plat->activated = true;
  CHECK(world->get_entity(world->ctx, plat_binding, &state) && !state.locked);
  /* Two linked movers matching one binding are ambiguous (TS null). */
  qa_q1_actor *twin = qa_q1_create(H.fx.game, "func_door", NULL, NULL);
  twin->pos1 = (qa_vec3){0, 0, 64};
  link_model(twin->actor.id, (qa_vec3){0, 0, 0}, absolute, 3, "solid");
  qa_navigation_entity_binding fresh = binding; fresh.raw_count = 1; /* a distinct binding */
  CHECK(!world->get_entity(world->ctx, fresh, &state));
  /* The first binding keeps its resolved model (TS boundModels). */
  CHECK(world->get_entity(world->ctx, binding, &state) && qa_same_actor(state.actor, door->actor.id));

  /* Q2 brush without mover state: enabled, unlocked, no destination. */
  qa_owned_actor q2actor = qa_session_actor_registry_allocate(H.fx.actors, "q2:test", "q2:func_wall");
  qa_q2_entity *wall = &H.q2entities[H.q2count++];
  memset(wall, 0, sizeof *wall); wall->actor = q2actor; wall->classname = "func_wall"; wall->solid = "brush"; wall->targetname = "";
  link_model(q2actor.id, (qa_vec3){0, 0, 0}, box(200, 0, 0, 210, 10, 10), 4, "solid");
  qa_navigation_entity_binding wall_binding; memset(&wall_binding, 0, sizeof wall_binding);
  wall_binding.has_model = true; wall_binding.model = 4;
  CHECK(world->get_entity(world->ctx, wall_binding, &state));
  CHECK(qa_same_actor(state.actor, q2actor.id) && state.enabled && !state.locked && !state.has_destination && !state.has_train && !state.has_elevator);
  wall->solid = "none";
  CHECK(world->get_entity(world->ctx, wall_binding, &state) && !state.enabled);

  /* Hazards: Q1 trigger_hurt needs trigger solid, damage and a toucher; Q2 also waits for its timestamp. */
  qa_q1_actor *hurt = qa_q1_create(H.fx.game, "trigger_hurt", NULL, NULL);
  hurt->solid = "trigger"; hurt->damage = 5; hurt->touch.fn = touch_noop;
  link_model(hurt->actor.id, (qa_vec3){0, 0, 0}, box(-200, -200, -200, -190, -190, -190), -1, "trigger");
  qa_bounds hazard_area = box(-205, -205, -205, -185, -185, -185);
  CHECK(world->hazard(world->ctx, hazard_area));
  hurt->damage = 0;
  CHECK(!world->hazard(world->ctx, hazard_area));
  qa_owned_actor q2hurt = qa_session_actor_registry_allocate(H.fx.actors, "q2:test", "q2:trigger_hurt");
  qa_q2_entity *trigger = &H.q2entities[H.q2count++];
  memset(trigger, 0, sizeof *trigger); trigger->actor = q2hurt; trigger->classname = "trigger_hurt"; trigger->solid = "trigger";
  trigger->damage = 3; trigger->touch.fn = q2_touch_noop; trigger->timestamp = 2;
  link_model(q2hurt.id, (qa_vec3){0, 0, 0}, box(-198, -198, -198, -194, -194, -194), -1, "trigger");
  H.q2now = 1;
  CHECK(!world->hazard(world->ctx, hazard_area));
  H.q2now = 2;
  CHECK(world->hazard(world->ctx, hazard_area));
  CHECK(!world->hazard(world->ctx, box(150, 150, 150, 160, 160, 160)));

  /* forClient caches per player identity and locomotion; predictClientMovement routes through it. */
  qa_navigation_runtime *client = qa_app_sim_application_bot_navigation_for_client(nav, 0);
  CHECK(client != NULL && client != base && client == qa_app_sim_application_bot_navigation_for_client(nav, 0));
  CHECK(client->world->pass_actor != NULL && qa_same_actor(*client->world->pass_actor, H.player_actor.id));
  qa_bot_movement_prediction query; memset(&query, 0, sizeof query);
  query.entity_num = 0; query.origin = (qa_vec3){1, 2, 3};
  qa_bot_travel_prediction_result predicted = selected->predict_client_movement(selected->ctx, query);
  CHECK(H.predict_calls == 1 && predicted.frames == 7 && H.predicted_identity == &H.player_identity && H.predicted_runtime == client);
  query.entity_num = -1;
  predicted = selected->predict_client_movement(selected->ctx, query);
  CHECK(H.predict_calls == 2 && H.predicted_runtime == client);
  /* With no admitted player the probe returns its own origin. */
  H.has_players = false;
  query.velocity = (qa_vec3){5, 0, 0};
  predicted = selected->predict_client_movement(selected->ctx, query);
  CHECK(H.predict_calls == 2 && predicted.end.x == 1 && predicted.velocity.x == 5 && predicted.trajectory_count == 1
      && predicted.trajectory[0].z == 3 && !predicted.has_end_area && !predicted.has_trace && !predicted.has_bounds);
  qa_caught_error caught;
  H.has_players = true;

  /* Checkpoint round trip, reusable flags and remapClient validation. */
  qa_app_sim_bot_navigation_checkpoint checkpoint;
  CHECK(qa_app_sim_application_bot_navigation_checkpoint(nav, &checkpoint));
  CHECK(checkpoint.version == 1 && checkpoint.client_count == 1 && checkpoint.clients[0].client == 0 && checkpoint.clients[0].reusable);
  CHECK(checkpoint.base.world_revision == 1500 && checkpoint.base.generation == generation + 1);
  CHECK(strcmp(checkpoint.base.map_name, "maps/test.bsp") == 0 && strcmp(checkpoint.base.map_format, "q1-bsp") == 0);
  qa_save_value *value = qa_app_sim_bot_navigation_checkpoint_value(&checkpoint);
  CHECK(value != NULL);
  qa_app_sim_bot_navigation_checkpoint_destroy(&checkpoint);
  H.time = 4;
  CHECK(qa_navigation_runtime_generation(base) == generation + 2);
  qa_app_sim_application_bot_navigation_restore_checkpoint(nav, value, NULL, NULL);
  CHECK(base->world_revision == 1500 && base->generation == generation + 1);
  qa_navigation_runtime *restored = qa_app_sim_application_bot_navigation_for_client(nav, 0);
  CHECK(restored != client && restored == qa_app_sim_application_bot_navigation_for_client(nav, 0));
  restore_call shifted = {nav, value, remap_shift};
  bool ok = qa_try(run_restore, &shifted, &caught);
  CHECK(!ok && strcmp(caught.name, "Error") == 0 && strstr(caught.message, "Navigation client 1 is not admitted") != NULL);
  restore_call negative = {nav, value, remap_negative};
  CHECK(!qa_try(run_restore, &negative, &caught) && strstr(caught.message, "invalid or duplicate navigation client mapping") != NULL);
  /* An unreusable entry restores with no player; two saved clients cannot map to one. */
  qa_save_value *clients = NULL;
  for (size_t i = 0; i < value->entry_count; i++) if (strcmp(value->entries[i].key, "clients") == 0) clients = value->entries[i].value;
  CHECK(clients != NULL && clients->item_count == 1);
  if (clients != NULL && clients->item_count == 1) {
    for (size_t i = 0; i < clients->items[0]->entry_count; i++)
      if (strcmp(clients->items[0]->entries[i].key, "reusable") == 0) clients->items[0]->entries[i].value->boolean = false;
    qa_app_sim_application_bot_navigation_restore_checkpoint(nav, value, remap_shift, &sentinel);
    CHECK(qa_app_sim_application_bot_navigation_checkpoint(nav, &checkpoint));
    CHECK(checkpoint.client_count == 1 && checkpoint.clients[0].client == 1 && !checkpoint.clients[0].reusable);
    qa_app_sim_bot_navigation_checkpoint_destroy(&checkpoint);
    qa_save_value *second = qa_save_value_object();
    qa_save_value_set(second, "client", qa_save_value_number(1));
    qa_save_value_set(second, "reusable", qa_save_value_boolean(false));
    qa_save_value *runtime = NULL;
    for (size_t i = 0; i < clients->items[0]->entry_count; i++) if (strcmp(clients->items[0]->entries[i].key, "runtime") == 0) runtime = clients->items[0]->entries[i].value;
    qa_navigation_runtime_checkpoint copy;
    CHECK(qa_navigation_runtime_make_checkpoint(base, &copy));
    qa_save_value_set(second, "runtime", qa_app_sim_navigation_runtime_checkpoint_value(&copy));
    qa_navigation_runtime_checkpoint_destroy(&copy);
    (void)runtime;
    qa_save_value_push(clients, second);
    restore_call duplicate = {nav, value, remap_same};
    CHECK(!qa_try(run_restore, &duplicate, &caught) && strstr(caught.message, "invalid or duplicate navigation client mapping") != NULL);
  }
  qa_save_value_free(value);
  /* restartRound replaces the base runtime and drops client runtimes. */
  qa_app_sim_application_bot_navigation_restart_round(nav);
  qa_navigation_runtime *next = qa_app_sim_application_bot_navigation_runtime(nav);
  CHECK(next != base && qa_app_sim_application_bot_navigation_selected(nav)->runtime == next && next->world->pass_actor == NULL);
  CHECK(qa_app_sim_application_bot_navigation_checkpoint(nav, &checkpoint) && checkpoint.client_count == 0);
  qa_app_sim_bot_navigation_checkpoint_destroy(&checkpoint);
  qa_app_sim_application_bot_navigation_destroy(nav);
  teardown();
  CHECK(ctx_errors == 0 && H.fx.ctx_errors == 0);
}

/* ---- monster navigation ---- */
static qa_q1_monster_move_state monster_state;
static int monster_reads, monster_writes;
static bool m_read(void *ctx, qa_actor_id actor, qa_q1_monster_move_state *out) {
  SENTINEL_CHECK(ctx); (void)actor; monster_reads++; *out = monster_state; return true;
}
static bool m_read_target(void *ctx, qa_actor_id actor, qa_q1_monster_move_target *out) { SENTINEL_CHECK(ctx); (void)actor; (void)out; return false; }
static void m_write(void *ctx, qa_owned_actor actor, qa_q1_monster_move_state state) { SENTINEL_CHECK(ctx); (void)actor; (void)state; monster_writes++; }
static void m_link(void *ctx, qa_owned_actor actor, bool triggers) { SENTINEL_CHECK(ctx); (void)actor; (void)triggers; }
static double m_random(void *ctx) { SENTINEL_CHECK(ctx); return 0; }
static qa_trace_result m_trace(void *ctx, qa_trace_query query) {
  SENTINEL_CHECK(ctx);
  qa_trace_result r; memset(&r, 0, sizeof r); r.kind = "q1"; r.fraction = 1; r.end = query.end;
  if (query.end.z < query.start.z) { r.fraction = 0.5; r.end = query.start; r.end.z -= 1; r.contact.has_plane = true; r.contact.plane.normal.z = 1; r.hit.kind = QA_TRACE_HIT_WORLD; }
  return r;
}
static qa_point_contents_result m_contents(void *ctx, qa_point_contents_query query) {
  SENTINEL_CHECK(ctx); (void)query;
  qa_point_contents_result r; memset(&r, 0, sizeof r); r.kind = "q1"; r.contents = -1; return r;
}
static qa_q1_services *h_no_game(void *ctx) { SENTINEL_CHECK(ctx); return NULL; }
typedef struct install_call { qa_app_sim_application_monster_navigation *navigation; qa_app_sim_monster_navigation_simulation simulation; qa_q1_monster_movement *movement; } install_call;
static void run_install(void *ctx) { install_call *c = ctx; qa_app_sim_application_monster_navigation_install(c->navigation, c->simulation, c->movement); }
static void test_monster_navigation_preload(void) {
  setup();
  qa_app_sim_monster_navigation_content content; memset(&content, 0, sizeof content);
  content.ctx = &sentinel; content.recipe = &H.recipe; content.world.kind = QA_DECODED_WORLD_Q1; content.world.as.q1 = &geometry;
  content.mounts = H.mounted; content.for_content = c_for_content;
  /* Only MG3 map entities preload navigation (TS content.split(":")[2] === "mg3"). */
  H.recipe.map.entities.content = "q1:rerelease:id1:1";
  CHECK(qa_app_sim_preload_application_monster_navigation(content) == NULL);
  H.recipe.map.entities.content = "q1:rerelease:mg3x:1";
  CHECK(qa_app_sim_preload_application_monster_navigation(content) == NULL);
  H.recipe.map.entities.content = "q1:rerelease:mg3:1";
  qa_app_sim_application_monster_navigation *navigation = qa_app_sim_preload_application_monster_navigation(content);
  CHECK(navigation != NULL);
  /* install(simulation, movement) */
  qa_q1_monster_move_services services; memset(&services, 0, sizeof services);
  services.ctx = &sentinel; services.scene = (qa_scene_queries){&sentinel, m_trace, m_contents, NULL, NULL, NULL};
  services.random = (qa_q1_monster_random){&sentinel, m_random};
  services.read = m_read; services.read_target = m_read_target; services.write = m_write; services.link = m_link;
  qa_q1_monster_movement movement; qa_q1_monster_movement_init(&movement, services);
  qa_provider_timing timing[1]; memset(timing, 0, sizeof timing);
  timing[0].provider = "q1:official"; timing[0].clock.kind = QA_CLOCK_Q2_CLASSIC;
  H.recipe.timing = timing; H.recipe.timing_count = 1;
  H.recipe.map.entities.provider = "q1:official";
  qa_app_sim_monster_navigation_simulation simulation; memset(&simulation, 0, sizeof simulation);
  simulation.ctx = &sentinel; simulation.recipe = &H.recipe; simulation.scene = H.scene; simulation.time_seconds = h_time;
  simulation.q1_game = h_q1_game;
  install_call call = {navigation, simulation, &movement};
  qa_caught_error caught;
  CHECK(!qa_try(run_install, &call, &caught) && strcmp(caught.name, "Error") == 0
      && strcmp(caught.message, "MG3 monsters require Q1 source movement") == 0);
  timing[0].clock.kind = QA_CLOCK_Q1_NETQUAKE;
  /* No Q1 source program: nothing to navigate, nothing registered. */
  qa_app_sim_monster_navigation_simulation absent = simulation; absent.q1_game = h_no_game;
  qa_app_sim_application_monster_navigation_install(navigation, absent, &movement);
  qa_app_sim_application_monster_navigation_install(navigation, simulation, &movement);
  /* A second registration for the same game is refused by the MG3 path module. */
  char message[256];
  CHECK(!qa_range_error_catch(run_install, &call, message, sizeof message)
      && strcmp(message, "MG3 monster navigation is already registered") == 0);
  qa_q1am_release_mg3_monster_navigation(H.fx.game);
  qa_app_sim_application_monster_navigation_destroy(navigation);
  CHECK(monster_reads == 0 && monster_writes == 0);
  teardown();
  CHECK(ctx_errors == 0);
}

int main(void) {
  test_profile();
  test_application_navigation();
  test_monster_navigation_preload();
  printf("test_sim_navigation: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
