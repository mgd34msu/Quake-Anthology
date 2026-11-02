/* Concrete SharedSimulation assembly (TS loadSimulation over the
 * landed sibling modules): build over a decoded Q1 world, admit a
 * player at info_player_start, step frames through the real
 * scheduler/physics/movement pipeline, checkpoint a save image, and
 * close cleanly. Self-contained: the Q1 world is an in-memory
 * fixture in the style of test_sim_c's physics world.
 * GPL-2.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/core/numeric.h"
#include "qa/formats/q1_map.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"

static int checks, failures;
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
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
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

/* Empty Q1 world: solid leaf 0, empty leaf 1, one model. */
static qa_q1_leaf leaves[2];
static qa_q1_world_model models[1];
static double headnodes[4] = {-2, -1, -1, -1};
static qa_q1_entity entities[2];
static qa_q1_entity_property world_props[2], start_props[3];
static qa_q1_map q1_map;
static void build_world(void) {
  qa_bounds b = {{-512, -512, -512}, {512, 512, 512}};
  leaves[0] = (qa_q1_leaf){-2, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  leaves[1] = (qa_q1_leaf){-1, b, {0, 0}, false, 0, {0, 0, 0, 0}};
  memset(models, 0, sizeof models);
  models[0].bounds = b;
  models[0].headnodes = (qa_double_view){headnodes, 4};
  models[0].visible_leaves = 1;
  world_props[0] = (qa_q1_entity_property){"classname", "worldspawn"};
  world_props[1] = (qa_q1_entity_property){"message", "Concrete Test"};
  start_props[0] = (qa_q1_entity_property){"classname", "info_player_start"};
  start_props[1] = (qa_q1_entity_property){"origin", "32 64 128"};
  start_props[2] = (qa_q1_entity_property){"angle", "90"};
  entities[0] = (qa_q1_entity){world_props, 2};
  entities[1] = (qa_q1_entity){start_props, 3};
  memset(&q1_map, 0, sizeof q1_map);
  q1_map.world.format = "bsp29";
  q1_map.world.entities = "";
  q1_map.world.leaves = leaves; q1_map.world.leaf_count = 2;
  q1_map.world.models = models; q1_map.world.model_count = 1;
  q1_map.entity_list = entities; q1_map.entity_count = 2;
}

static qa_provider_reference ref(const char *provider) {
  qa_provider_reference out = {provider, "q1:classic:id1:installed"};
  return out;
}
static qa_provider_reference weapons_storage[1];
static qa_provider_timing timing_storage[1];
static qa_execution_module execution_storage[1];
static qa_app_application_world app_world;
static qa_app_simulation_options concrete_options(qa_identity_owner *identity) {
  qa_app_simulation_options options; memset(&options, 0, sizeof options);
  qa_executable_recipe *recipe = &options.recipe;
  recipe->schema_version = 3;
  recipe->map.geometry.requested_path = "maps/e1m1.bsp";
  recipe->map.geometry_content = "q1:classic:id1:installed";
  recipe->map.entities = ref("q1:official");
  recipe->movement = ref("q1:official");
  recipe->character.definition = ref("q1:official");
  recipe->character.appearance = ref("q1:official");
  weapons_storage[0] = ref("q1:official");
  recipe->weapons.values = weapons_storage; recipe->weapons.count = 1;
  recipe->engine_behavior = ref("q1:official");
  recipe->combat = ref("q1:official");
  recipe->inventory = ref("q1:official");
  recipe->match = ref("q1:official");
  recipe->transition = ref("q1:official");
  qa_clock_profile clock; memset(&clock, 0, sizeof clock);
  clock.kind = QA_CLOCK_Q1_NETQUAKE;
  clock.minimum_frame_seconds = 0.001; clock.maximum_frame_seconds = 0.1;
  timing_storage[0] = (qa_provider_timing){"q1:official", clock, qa_q1_donor_profile};
  recipe->timing = timing_storage; recipe->timing_count = 1;
  recipe->ordering.kind = QA_FRAME_ORDERING_NATIVE;
  recipe->ordering.clock = clock;
  memset(execution_storage, 0, sizeof execution_storage);
  execution_storage[0].kind = QA_EXECUTION_MODULE_TYPESCRIPT;
  execution_storage[0].owner = ref("q1:official");
  execution_storage[0].role = "server-game";
  execution_storage[0].api.kind = QA_GAME_API_QUAKEC;
  execution_storage[0].api.as.quakec.kind = QA_QUAKEC_API_Q1_NETQUAKE;
  recipe->execution = execution_storage; recipe->execution_count = 1;
  options.identity = *identity;
  app_world = (qa_app_application_world){"q1-bsp", &q1_map};
  options.world = &app_world;
  options.skill = 1; options.mode = "singleplayer";
  options.seed = 7; options.max_clients = 1;
  return options;
}

static void count_progress(void *ctx, qa_simulation_progress frame) {
  (*(int *)ctx)++;
  CHECK(frame.output != NULL && frame.pending_milliseconds == 0);
}

int main(void) {
  build_world();
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "concrete-test"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identity, 1024);
  CHECK(actors != NULL);
  qa_session_id session = qa_session_actor_registry_session(actors);
  qa_app_simulation_options options = concrete_options(&identity);

  /* Build: world actor at source slot 0, zeroed source frame. */
  qa_app_sim_shared_simulation *sim =
      qa_app_sim_load_simulation_concrete(options, actors, session, NULL, NULL);
  CHECK(sim != NULL);
  qa_app_sim_concrete_simulation *concrete = qa_app_sim_shared_simulation_concrete(sim);
  CHECK(concrete != NULL);
  CHECK(qa_app_sim_concrete_simulation_physics(concrete) != NULL);
  CHECK(qa_app_sim_concrete_simulation_scene(concrete) != NULL);
  qa_actor_id world_actor; memset(&world_actor, 0, sizeof world_actor);
  CHECK(qa_app_sim_concrete_simulation_world_actor(concrete, &world_actor));
  qa_owned_actor at_world; memset(&at_world, 0, sizeof at_world);
  CHECK(qa_session_actor_registry_at_source(actors, "q1:official", 0, &at_world));
  CHECK(qa_same_actor(at_world.id, world_actor));
  CHECK(qa_app_sim_concrete_simulation_frame(concrete).time.value == 0);

  /* Admit: spawns at the fixture info_player_start. */
  qa_client_id client = qa_identity_owner_client(&identity, 0, 1);
  qa_app_sim_player_admission admission =
      qa_app_sim_concrete_simulation_admit_player(concrete, client);
  CHECK(!qa_same_actor(admission.actor, world_actor));
  CHECK(admission.view_height == 22);
  qa_shared_body_table *bodies =
      qa_app_sim_shared_physics_bodies(qa_app_sim_concrete_simulation_physics(concrete));
  qa_body_state_result spawned = qa_shared_body_table_read(bodies, admission.actor);
  CHECK(spawned.has_value);
  CHECK(spawned.value.origin.x == 32 && spawned.value.origin.y == 64
        && spawned.value.origin.z == 128);
  CHECK(spawned.value.angles.y == 90);
  CHECK_ABORTS(qa_app_sim_concrete_simulation_admit_player(concrete, client));

  /* Step N frames with no commands: the free Q1 clock advances by
   * the clamped elapsed (50ms) once per step. */
  qa_input_batch input; memset(&input, 0, sizeof input);
  input.elapsed_milliseconds = 50;
  qa_simulation_output output = qa_app_sim_shared_simulation_step(sim, input);
  for (int i = 1; i < 10; i++) output = qa_app_sim_shared_simulation_step(sim, input);
  CHECK(fabs(output.snapshot.frame.time.value - 0.5) < 1e-9);
  CHECK(output.snapshot.frame.frame == 10);
  CHECK(output.snapshot.actor_count >= 2);
  CHECK(output.snapshot.body_count >= 2);
  CHECK(output.snapshot.configuration_count == 1);
  CHECK(output.snapshot.inventory_count == 1);
  CHECK(output.snapshot.scene.world != NULL);
  CHECK(output.snapshot.scene.world->geometry.kind == QA_DECODED_WORLD_Q1);

  /* A NetQuake command through the movement player moves the body
   * (gravity + forward wish velocity in the empty leaf). */
  qa_body_state_result before = qa_shared_body_table_read(bodies, admission.actor);
  qa_actor_command command; memset(&command, 0, sizeof command);
  command.actor = admission.actor;
  command.source.kind = QA_COMMAND_SOURCE_LOCAL_SEAT;
  command.source.client = client;
  command.sequence = 1;
  command.command.kind = QA_USER_COMMAND_Q1;
  command.command.as.q1.view_angles = (qa_vec3){0, 90, 0};
  command.command.as.q1.forward_move = 400;
  input.commands = &command; input.command_count = 1;
  for (int i = 0; i < 4; i++) {
    command.sequence = 1 + i;
    output = qa_app_sim_shared_simulation_step(sim, input);
  }
  qa_body_state_result after = qa_shared_body_table_read(bodies, admission.actor);
  CHECK(after.has_value);
  CHECK(after.value.origin.x != before.value.origin.x
        || after.value.origin.y != before.value.origin.y
        || after.value.origin.z != before.value.origin.z
        || after.value.velocity.x != 0 || after.value.velocity.y != 0
        || after.value.velocity.z != 0);

  /* stepAsync falls through to step() and reports progress once. */
  input.command_count = 0; input.commands = NULL;
  qa_simulation as_simulation = qa_app_sim_shared_simulation_as_simulation(sim);
  int progress_calls = 0;
  output = as_simulation.step_async(as_simulation.ctx, input, count_progress, &progress_calls);
  CHECK(progress_calls == 1);
  CHECK(output.snapshot.frame.frame == 15);

  /* Commands for an unadmitted actor abort (TS step guard). */
  qa_actor_command bad = command;
  bad.actor.slot += 500;
  qa_input_batch bad_input = input; bad_input.commands = &bad; bad_input.command_count = 1;
  CHECK_ABORTS((void)qa_app_sim_shared_simulation_step(sim, bad_input));
  /* A second admission for the same client aborts in-process too. */
  input.command_count = 0; input.commands = NULL;

  /* Checkpoint: a validated save image over the concrete owners. */
  qa_save_image image = qa_app_sim_shared_simulation_checkpoint(sim);
  CHECK(image.schema_version == 3);
  CHECK(image.clock_count == 1 && image.random_count == 1);
  CHECK(image.clocks[0].time.value == image.frame.time.value);
  CHECK(image.actor_count >= 2);
  CHECK(image.body_count >= 2);
  CHECK(image.combat_count == 1);
  CHECK(image.configuration_count == 1);
  CHECK(image.provider_count == 2);
  CHECK(strcmp(image.providers[0].schema, "world:source-slots") == 0);
  CHECK(strcmp(image.providers[1].schema, "world:simulation") == 0);
  /* Full TS validation also requires the q1:foundation provider
   * capture from the Q1 source game, which the concrete builder
   * does not construct yet: validation must refuse this image
   * rather than pass it silently. */
  CHECK_ABORTS(qa_app_sim_validate_simulation_save(image));
  qa_app_sim_saved_settings settings = qa_app_sim_saved_simulation_settings(image);
  CHECK(settings.skill == 1 && strcmp(settings.mode, "singleplayer") == 0);
  CHECK(settings.max_clients == 1 && settings.seed == 7);
  CHECK(settings.client_slot_count == 1 && settings.client_slots[0] == 0);
  CHECK(settings.host_milliseconds == 750);
  qa_save_value_free(settings.value);
  free(settings.client_slots);

  /* Close is clean and idempotent through the shared guards; a
   * stepped-after-close call aborts in a child. */
  qa_app_sim_shared_simulation_close(sim);
  qa_app_sim_shared_simulation_close(sim);
  CHECK_ABORTS((void)qa_app_sim_shared_simulation_step(sim, input));
  qa_app_sim_shared_simulation_destroy(sim);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);

  /* Constructor guards: no weapon provider, guest execution kinds. */
  qa_identity_owner identity2;
  CHECK(qa_identity_owner_create(&identity2, "concrete-guards"));
  qa_session_actor_registry *actors2 = qa_session_actor_registry_create(&identity2, 64);
  qa_app_simulation_options no_weapons = concrete_options(&identity2);
  no_weapons.recipe.weapons.count = 0;
  CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(no_weapons, actors2,
      qa_session_actor_registry_session(actors2), NULL, NULL));
  qa_execution_module quakec; memset(&quakec, 0, sizeof quakec);
  quakec.kind = QA_EXECUTION_MODULE_QUAKEC;
  qa_app_simulation_options guest = concrete_options(&identity2);
  guest.recipe.execution = &quakec; guest.recipe.execution_count = 1;
  CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(guest, actors2,
      qa_session_actor_registry_session(actors2), NULL, NULL));
  /* No world and no mounts cannot construct. */
  qa_app_simulation_options no_world = concrete_options(&identity2);
  no_world.world = NULL;
  CHECK_ABORTS((void)qa_app_sim_load_simulation_concrete(no_world, actors2,
      qa_session_actor_registry_session(actors2), NULL, NULL));
  qa_session_actor_registry_destroy(actors2);
  qa_identity_owner_destroy(&identity2);

  printf("test_sim_concrete: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
