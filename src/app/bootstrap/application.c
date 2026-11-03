/* Port of src/app/bootstrap/application.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/application.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/world/session/session.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/content/mounts.h"
#include "qa/formats/q1_map.h"

typedef struct {
  char *name;
  char **arguments; size_t argument_count;
  bool has_seat; qa_seat_id seat;
  bool has_source; qa_command_context source;
} queued_command;

struct qa_app_application {
  qa_app_options options;
  qa_app_application_host host;
  bool has_preferences;
  qa_app_frontend_preference_overrides preferences;
  double frames, elapsed;
  bool stopping, closed, closing, stepping;
  queued_command *queued; size_t queued_count, queued_cap;
  qa_seat_id *ranking_requests; size_t ranking_count, ranking_cap;
  /* Owned-path composition (TS openSource owned branch). */
  qa_app_loaded_content *content;
  qa_app_application_world world; /* borrowed view of content's world */
  qa_identity_owner identity; bool has_identity;
  char *identity_name;
  qa_engine_session *session;
  qa_session_actor_registry *actors; bool owns_actors;
  qa_app_sim_shared_simulation *simulation;
  /* TS guestOptions input: prepared QuakeC artifact handed to
   * loadSimulation (owned program; freed after the simulation). */
  qa_app_sim_prepared_quakec_source prepared_quakec; bool has_prepared_quakec;
  double max_clients;
  size_t local_seats;
  qa_simulation_output last_output; bool has_last_output;
};

static char *dup(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}
static void oom(void) { qa_range_error("Out of memory"); }

/* qa_app_loaded_content_destroy frees its options copy, so content
 * gets a deep copy of the owned strings. Addon roots and mod
 * selections are already resolved by host discovery and are not
 * duplicated into the content copy. */
static qa_app_options clone_options_for_content(const qa_app_options *in) {
  qa_app_options out = *in;
  out.network_transport_relay = dup(in->network_transport_relay);
  out.startup_commands = NULL;
  if (in->startup_command_count > 0) {
    out.startup_commands = calloc(in->startup_command_count, sizeof *out.startup_commands);
    if (!out.startup_commands) oom();
    for (size_t i = 0; i < in->startup_command_count; i++)
      out.startup_commands[i] = dup(in->startup_commands[i]);
  }
  out.remote_content.directory = dup(in->remote_content.directory);
  out.server_profile_path = dup(in->server_profile_path);
  out.corpus_root = dup(in->corpus_root);
  out.user_content_root = dup(in->user_content_root);
  out.addon_content_roots = NULL;
  out.addon_content_root_count = 0;
  out.product = dup(in->product);
  out.map_product = dup(in->map_product);
  out.map = dup(in->map);
  out.quakec_program = dup(in->quakec_program);
  out.q2_game_library = dup(in->q2_game_library);
  out.weapon_behavior_product = dup(in->weapon_behavior_product);
  out.weapon_behavior_id = dup(in->weapon_behavior_id);
  out.mods = NULL;
  out.mod_count = 0;
  out.movement_product = dup(in->movement_product);
  out.character_model = dup(in->character_model);
  out.character_skin = dup(in->character_skin);
  out.network_host = dup(in->network_host);
  out.network_remote = dup(in->network_remote);
  return out;
}

static void loading_stage(qa_app_application *a, const char *message) {
  if (a->host.loading_stage) a->host.loading_stage(a->host.ctx, message);
}

/* TS openSource defaultCapacity: q1-quakeworld 8, singleplayer
 * 8 for a q3 engine product else 1, otherwise 16; maxClients then
 * also covers every local seat slot. */
static double resolve_max_clients(qa_app_application *a,
                                  const qa_executable_recipe *recipe, bool has_recipe) {
  double capacity;
  if (a->options.product && strcmp(a->options.product, "q1-quakeworld") == 0) {
    capacity = 8;
  } else if (a->options.mode && strcmp(a->options.mode, "singleplayer") == 0) {
    bool q3_engine = false;
    const char *engine = has_recipe && recipe ? recipe->engine_behavior.content : NULL;
    if (engine && a->host.has_content && a->host.content.product) {
      qa_game_family family = QA_GAME_FAMILY_Q1;
      const char *edition = NULL, *campaign = NULL;
      bool installed = false, builtin = false;
      if (a->host.content.product(a->host.content.ctx, engine, &family,
                                   &edition, &campaign, &installed, &builtin))
        q3_engine = family == QA_GAME_FAMILY_Q3;
    }
    capacity = q3_engine ? 8 : 1;
  } else {
    capacity = 16;
  }
  if ((double)a->local_seats > capacity) capacity = (double)a->local_seats;
  return capacity;
}

/* TS prepareQuakeCSource mounts.read(artifact) over the loaded
 * content mounts (the concrete builder prepares precache resources
 * from the same mounts). */
static bool compose_read_quakec_artifact(void *ctx, const qa_resolved_resource_reference *artifact,
                                         uint8_t **out, size_t *out_length) {
  qa_content_mounted *mounts = ctx;
  if (!mounts || !artifact) return false;
  *out = qa_content_mounted_read_reference(mounts, artifact, out_length);
  if (!*out && artifact->requested_path)
    *out = qa_content_mounted_read_path(mounts, artifact->requested_path, out_length);
  return *out != NULL;
}

static void compose_world(qa_app_application *a, const qa_executable_recipe *recipe, bool has_recipe) {
  /* TS: identity = createIdentityOwner(`quake:${product}:${map}`);
   * session = new EngineSession(identity, dedicated ? headless : local). */
  const char *product = a->options.product ? a->options.product : "";
  const char *map = a->options.map ? a->options.map : "";
  size_t name_len = strlen("quake::") + strlen(product) + strlen(map) + 1;
  a->identity_name = malloc(name_len);
  if (!a->identity_name) oom();
  (void)snprintf(a->identity_name, name_len, "quake:%s:%s", product, map);
  if (!qa_identity_owner_create(&a->identity, a->identity_name)) oom();
  a->has_identity = true;
  qa_session_mode mode = {a->options.dedicated ? QA_SESSION_MODE_HEADLESS : QA_SESSION_MODE_LOCAL};
  a->session = qa_engine_session_create(&a->identity, mode);
  if (!a->session) oom();
  /* Session-level half of prepareInitialConfiguration: owned
   * graphical applications get one loopback client + seat per
   * requested seat; dedicated applications have no local seats.
   * The input/presentation halves stay with the graphical host. */
  if (!a->options.dedicated) {
    double seats = a->options.seats;
    size_t count = seats > 0 ? (size_t)seats : 0;
    for (size_t i = 0; i < count; i++) {
      qa_session_client *client = qa_engine_session_create_client(a->session, (int64_t)i);
      (void)qa_session_client_connect(client, "loopback");
      (void)qa_engine_session_create_seat(a->session, (int64_t)i, client);
      a->local_seats++;
    }
  }
  a->max_clients = resolve_max_clients(a, recipe, has_recipe);
  if (!a->host.has_simulation) return;
  loading_stage(a, "Preparing world...");
  a->actors = a->host.actors;
  a->owns_actors = a->actors == NULL;
  if (a->owns_actors) {
    /* TS SessionActorRegistry default capacity. */
    a->actors = qa_session_actor_registry_create(&a->identity, 65536);
    if (!a->actors) oom();
  }
  qa_app_simulation_options sim_options = {0};
  sim_options.dedicated = a->options.dedicated;
  sim_options.identity = a->identity;
  if (has_recipe && recipe) sim_options.recipe = *recipe;
  sim_options.skill = a->options.skill;
  sim_options.mode = a->options.mode;
  sim_options.seed = a->options.seed;
  sim_options.max_clients = a->max_clients;
  /* TS playerColors clamp: min(13, max(0, trunc(color ?? 0))). */
  double top = trunc(a->options.has_character_top_color ? a->options.character_top_color : 0);
  double bottom = trunc(a->options.has_character_bottom_color ? a->options.character_bottom_color : 0);
  sim_options.has_player_colors = true;
  sim_options.player_color_top = top < 0 ? 0 : top > 13 ? 13 : top;
  sim_options.player_color_bottom = bottom < 0 ? 0 : bottom > 13 ? 13 : bottom;
  if (a->content) {
    /* TS loadSimulation({world: content.world, mounts: content.mounts}).
     * The accessor returns by value, so the application retains the
     * view; content outlives the simulation (closed after it). A
     * {NULL, NULL} view means the content host decoded no world, and
     * the concrete builder then decodes from mounts. */
    a->world = qa_app_loaded_content_world(a->content);
    sim_options.world = &a->world;
    sim_options.mounts = qa_app_loaded_content_mounts(a->content);
  }
  if (has_recipe && recipe && sim_options.mounts) {
    /* TS guestOptions + preparedQuakeC: a QuakeC server-game
     * execution prepares its artifact (progs.dat) from the mounts
     * and hands it to loadSimulation; the runtime constructs the
     * QuakeCSource game from it and runs spawnMap. The TS q2Guest/
     * q3Guest preparations have no concrete C content accessors
     * (preparedQ2Game/preparedQ3Game) and stay unresolved: those
     * recipes abort in the builder with the TS messages. */
    for (size_t i = 0; i < recipe->execution_count; i++) {
      const qa_execution_module *module = &recipe->execution[i];
      if (module->kind != QA_EXECUTION_MODULE_QUAKEC) continue;
      const char *entity_text = "";
      if (a->content && a->world.kind && strcmp(a->world.kind, "q1-bsp") == 0 && a->world.value)
        entity_text = ((const qa_q1_map *)a->world.value)->world.entities;
      qa_app_sim_quakec_prepare_host prepare_host = {0};
      prepare_host.ctx = (void *)sim_options.mounts;
      prepare_host.read_artifact = compose_read_quakec_artifact;
      a->prepared_quakec = qa_app_sim_prepare_quakec_source(*module, entity_text, prepare_host);
      a->has_prepared_quakec = true;
      sim_options.prepared_quakec = &a->prepared_quakec;
      break;
    }
  }
  if (a->options.server_profile) sim_options.server_profile = a->options.server_profile;
  a->simulation = qa_app_sim_load_simulation(sim_options, a->host.simulation, a->actors,
                                             qa_engine_session_id(a->session), NULL, NULL);
  if (!a->simulation) oom();
  /* TS: if (ownership.kind === "owned") session.attachWorld(simulation). */
  (void)qa_engine_session_attach_world(a->session, qa_app_sim_shared_simulation_as_simulation(a->simulation));
}

qa_app_application *qa_app_application_open(qa_app_options options, qa_app_application_host host,
                                            const qa_executable_recipe *recipe, bool has_recipe,
                                            bool has_preferences, qa_app_frontend_preference_overrides preferences) {
  /* TS openSource: remote clients carry no local simulation. */
  const char *network = options.network_kind;
  if (network && (strcmp(network, "qw-client") == 0 || strcmp(network, "q1-client") == 0 ||
                  strcmp(network, "q2-client") == 0 || strcmp(network, "q3-client") == 0 ||
                  strcmp(network, "unified-client") == 0))
    qa_range_error("Remote clients require RemoteApplication without a local simulation");
  qa_app_application *application = calloc(1, sizeof *application);
  if (!application) return NULL;
  application->options = options;
  application->host = host;
  application->has_preferences = has_preferences;
  application->preferences = preferences;
  loading_stage(application, "Preparing configuration...");
  const qa_executable_recipe *effective_recipe = recipe;
  bool has_effective_recipe = has_recipe;
  if (host.has_content) {
    loading_stage(application, "Loading map...");
    application->content = qa_app_load_application_content(
        host.content, clone_options_for_content(&options), recipe, has_recipe);
    if (!application->content) oom();
    if (!has_effective_recipe) {
      effective_recipe = qa_app_loaded_content_recipe(application->content);
      has_effective_recipe = effective_recipe != NULL;
    }
  }
  if (host.has_content || host.has_simulation)
    compose_world(application, effective_recipe, has_effective_recipe);
  if (application->simulation) loading_stage(application, "Starting game...");
  if (host.open) {
    const char *error = host.open(host.ctx, application);
    if (error) {
      /* TS openSource catch: close what was staged, then rethrow. */
      (void)qa_app_application_close(application);
      qa_range_error("%s", error);
    }
  }
  if (application->simulation && host.print && has_effective_recipe && effective_recipe &&
      effective_recipe->map.geometry.requested_path && effective_recipe->movement.provider &&
      effective_recipe->character.appearance.provider) {
    char message[1024];
    (void)snprintf(message, sizeof message, "Loaded %s with %s and %s.\n",
                   effective_recipe->map.geometry.requested_path,
                   effective_recipe->movement.provider,
                   effective_recipe->character.appearance.provider);
    host.print(host.ctx, message);
  }
  return application;
}
qa_app_application *qa_app_open_application(qa_app_options options, qa_app_application_host host,
                                            const qa_executable_recipe *recipe, bool has_recipe,
                                            bool has_preferences, qa_app_frontend_preference_overrides preferences) {
  return qa_app_application_open(options, host, recipe, has_recipe, has_preferences, preferences);
}
static void free_queued_command(queued_command *command) {
  free(command->name);
  for (size_t i = 0; i < command->argument_count; i++) free(command->arguments[i]);
  free(command->arguments);
  memset(command, 0, sizeof *command);
}
void qa_app_application_destroy(qa_app_application *application) {
  if (!application) return;
  qa_app_application_close(application);
  for (size_t i = 0; i < application->queued_count; i++) free_queued_command(&application->queued[i]);
  free(application->queued);
  free(application->ranking_requests);
  qa_app_options_destroy(&application->options);
  free(application);
}
const qa_app_options *qa_app_application_options(const qa_app_application *a) { return &a->options; }
double qa_app_application_frame_count(const qa_app_application *a) { return a->frames; }
double qa_app_application_time_milliseconds(const qa_app_application *a) { return a->elapsed; }
bool qa_app_application_finished(const qa_app_application *a) {
  return a->stopping || a->closed || (a->options.has_frame_limit && a->frames >= a->options.frame_limit);
}
bool qa_app_application_client_commands_blocked(qa_app_application *a) {
  return (a->host.capture_blocks_transition && a->host.capture_blocks_transition(a->host.ctx)) ||
      (a->host.video_restart_pending && a->host.video_restart_pending(a->host.ctx));
}
void qa_app_application_request_quit(qa_app_application *a) { a->stopping = true; }
void qa_app_application_queue_command(qa_app_application *a, const char *name,
                                      const char *const *arguments, size_t argument_count,
                                      bool has_seat, qa_seat_id seat, bool has_source, qa_command_context source) {
  /* TS queueCommand admits {target:"application", name, args copy, seat, source}. */
  if (a->queued_count == a->queued_cap) {
    a->queued_cap = a->queued_cap ? a->queued_cap * 2 : 16;
    a->queued = realloc(a->queued, a->queued_cap * sizeof *a->queued);
    if (!a->queued) oom();
  }
  queued_command command = {0};
  command.name = dup(name);
  command.has_seat = has_seat;
  command.seat = seat;
  command.has_source = has_source;
  command.source = source;
  if (argument_count > 0) {
    command.arguments = calloc(argument_count, sizeof *command.arguments);
    if (!command.arguments) oom();
    for (size_t i = 0; i < argument_count; i++) command.arguments[i] = dup(arguments[i]);
    command.argument_count = argument_count;
  }
  a->queued[a->queued_count++] = command;
}
size_t qa_app_application_queued_command_count(const qa_app_application *a) { return a->queued_count; }
const char *qa_app_application_queued_command_name(const qa_app_application *a, size_t index) {
  return index < a->queued_count ? a->queued[index].name : NULL;
}
size_t qa_app_application_queued_command_argument_count(const qa_app_application *a, size_t index) {
  return index < a->queued_count ? a->queued[index].argument_count : 0;
}
const char *qa_app_application_queued_command_argument(const qa_app_application *a, size_t index, size_t argument) {
  if (index >= a->queued_count || argument >= a->queued[index].argument_count) return NULL;
  return a->queued[index].arguments[argument];
}
/* TS commands(): drain admitted application requests in order.
 * Returns the first dispatch error; dispatched entries are removed,
 * the failing entry and everything after it stay queued. */
static const char *dispatch_queued_commands(qa_app_application *a) {
  size_t done = 0;
  const char *error = NULL;
  while (done < a->queued_count) {
    queued_command *command = &a->queued[done];
    if (a->host.execute_command) {
      error = a->host.execute_command(a->host.ctx, command->name,
                                      (const char *const *)command->arguments, command->argument_count,
                                      command->has_seat, command->seat,
                                      command->has_source, command->source);
      if (error) break;
    } else if (command->name && strcmp(command->name, "quit") == 0) {
      a->stopping = true;
    } else {
      break; /* no dispatcher: the request stays admitted */
    }
    done++;
  }
  for (size_t i = 0; i < done; i++) free_queued_command(&a->queued[i]);
  if (done > 0) {
    memmove(a->queued, a->queued + done, (a->queued_count - done) * sizeof *a->queued);
    a->queued_count -= done;
  }
  return error;
}
bool qa_app_application_input(qa_app_application *a, const qa_seat_input_event *event) {
  if (a->closed) qa_range_error("Application is closed");
  return a->host.input ? a->host.input(a->host.ctx, event) : false;
}
void qa_app_application_pump_client_input(qa_app_application *a) {
  if (a->host.pump_client_input) a->host.pump_client_input(a->host.ctx);
}
const char *qa_app_application_advance_client_startup(qa_app_application *a, bool *advanced) {
  if (qa_app_application_client_commands_blocked(a)) {
    *advanced = true; /* TS: blocked startup reports a consumed frame */
    return NULL;
  }
  return a->host.advance_client_startup ? a->host.advance_client_startup(a->host.ctx, advanced)
                                         : (*advanced = false, (const char *)NULL);
}
const char *qa_app_application_flush_client_commands(qa_app_application *a) {
  return a->host.flush_client_commands ? a->host.flush_client_commands(a->host.ctx) : NULL;
}
const char *qa_app_application_step(qa_app_application *a, double elapsed_milliseconds, const void **out_output) {
  /* TS step() guards, in order. */
  if (a->closed) qa_range_error("Application is closed");
  if (a->stepping) qa_range_error("Application step is already in progress");
  if (!isfinite(elapsed_milliseconds) || elapsed_milliseconds <= 0)
    qa_range_error("Application step requires positive elapsed milliseconds");
  if (out_output) *out_output = NULL;
  a->stepping = true;
  const char *error = NULL;
  /* TS: a stopping application with no local players returns the
   * retained output without advancing the frame counters. */
  if (a->stopping && a->local_seats == 0) {
    if (out_output && a->has_last_output) *out_output = &a->last_output;
    a->stepping = false;
    return NULL;
  }
  /* TS owned path: advance client startup, flush, pump input. */
  bool advanced = false;
  error = qa_app_application_advance_client_startup(a, &advanced);
  if (!error) error = qa_app_application_flush_client_commands(a);
  if (error) { a->stepping = false; return error; }
  qa_app_application_pump_client_input(a);
  /* TS sourceCommands.executeAsync + commands(): queued requests. */
  error = dispatch_queued_commands(a);
  if (error) { a->stepping = false; return error; }
  if (a->closed) { a->stepping = false; qa_range_error("Application closed during step"); }
  if (a->session && a->simulation) {
    /* TS session.stepAsync({elapsedMilliseconds, commands}). Local
     * actor commands are built by the graphical input host; the
     * dedicated path steps with an empty command batch. */
    qa_input_batch batch = {0};
    batch.elapsed_milliseconds = elapsed_milliseconds;
    qa_simulation_output output = qa_engine_session_step_async(a->session, batch, NULL, NULL);
    a->last_output = output;
    a->has_last_output = true;
    if (out_output) *out_output = &a->last_output;
  } else if (a->host.step) {
    error = a->host.step(a->host.ctx, a, elapsed_milliseconds, out_output);
    if (error) { a->stepping = false; return error; }
  }
  a->frames++;
  a->elapsed += elapsed_milliseconds;
  /* TS tail: commands() runs once more after presentation. */
  error = dispatch_queued_commands(a);
  a->stepping = false;
  return error;
}
const char *qa_app_application_run(qa_app_application *a, double frame_milliseconds) {
  /* TS run(): step until stopping/closed/frameLimit; the pumped
   * C loop owns pacing (TS sleeps to a 4 ms floor between steps). */
  while (!qa_app_application_finished(a)) {
    const void *output = NULL;
    const char *error = qa_app_application_step(a, frame_milliseconds, &output);
    if (error) return error;
  }
  return NULL;
}
const char *qa_app_application_change_level(qa_app_application *a, const char *map, const char *spawn_point) {
  return a->host.change_level(a->host.ctx, map, spawn_point ? spawn_point : "");
}
const char *qa_app_application_save_game(qa_app_application *a, const char *path, const char *format) {
  return a->host.save_game(a->host.ctx, path, format ? format : "shared");
}
const char *qa_app_application_load_game(qa_app_application *a, const char *path, const char *source_product) {
  return a->host.load_game(a->host.ctx, path, source_product);
}
const char *qa_app_application_execute_request(qa_app_application *a, qa_app_configuration_command_request request) {
  return a->host.execute_request(a->host.ctx, request);
}
const char *qa_app_application_return_to_lobby(qa_app_application *a) {
  /* TS returnToLobby throws when the host retained no lobby. */
  if (!a->host.return_to_lobby) return "This session has no retained lobby";
  return a->host.return_to_lobby(a->host.ctx);
}
const char *qa_app_application_read_client_resource(qa_app_application *a, const char *path, uint8_t **out_bytes, size_t *out_count, bool *found) {
  return a->host.read_client_resource(a->host.ctx, path, out_bytes, out_count, found);
}
const char *qa_app_application_read_pixels(qa_app_application *a, uint8_t **out_bytes, size_t *out_count) {
  if (!a->host.read_pixels) {
    *out_bytes = NULL;
    *out_count = 0;
    return "Dedicated applications have no framebuffer";
  }
  return a->host.read_pixels(a->host.ctx, out_bytes, out_count);
}
const char *qa_app_application_capture_next_frame(qa_app_application *a, uint8_t **out_bytes, size_t *out_count) {
  return qa_app_application_read_pixels(a, out_bytes, out_count);
}
const char *qa_app_application_close(qa_app_application *a) {
  if (!a || a->closed || a->closing) return NULL;
  /* TS close(): closed/stopping publish immediately, then
   * closeOwned tears down in order: world, graphical services,
   * session, content. */
  a->closing = true;
  a->closed = true;
  a->stopping = true;
  if (a->session && a->simulation) qa_engine_session_close_world(a->session);
  if (a->simulation) {
    qa_app_sim_shared_simulation_close(a->simulation);
    qa_app_sim_shared_simulation_destroy(a->simulation);
    a->simulation = NULL;
  }
  if (a->has_prepared_quakec) {
    qa_compat_qc_program_destroy(&a->prepared_quakec.program);
    a->has_prepared_quakec = false;
  }
  const char *error = a->host.close ? a->host.close(a->host.ctx, a) : NULL;
  if (a->session) {
    qa_engine_session_close(a->session);
    qa_engine_session_destroy(a->session);
    a->session = NULL;
  }
  if (a->owns_actors && a->actors) qa_session_actor_registry_destroy(a->actors);
  a->actors = NULL;
  a->owns_actors = false;
  if (a->has_identity) {
    qa_identity_owner_destroy(&a->identity);
    a->has_identity = false;
  }
  free(a->identity_name);
  a->identity_name = NULL;
  if (a->content) {
    qa_app_loaded_content_destroy(a->content);
    a->content = NULL;
  }
  return error;
}
void qa_app_application_request_ranking_menu(qa_app_application *a, qa_seat_id seat) {
  for (size_t i = 0; i < a->ranking_count; i++) if (qa_same_seat(a->ranking_requests[i], seat)) return;
  if (a->ranking_count == a->ranking_cap) {
    a->ranking_cap = a->ranking_cap ? a->ranking_cap * 2 : 8;
    a->ranking_requests = realloc(a->ranking_requests, a->ranking_cap * sizeof *a->ranking_requests);
    if (!a->ranking_requests) oom();
  }
  a->ranking_requests[a->ranking_count++] = seat;
}
bool qa_app_application_take_ranking_menu_request(qa_app_application *a, qa_seat_id seat) {
  for (size_t i = 0; i < a->ranking_count; i++)
    if (qa_same_seat(a->ranking_requests[i], seat)) {
      a->ranking_requests[i] = a->ranking_requests[--a->ranking_count];
      return true;
    }
  return false;
}
void qa_app_application_release_settings(qa_app_application *a) { (void)a; /* tools/view cvars are host-owned */ }
const char *qa_app_application_retire(qa_app_application *a) { return qa_app_application_close(a); }
qa_engine_session *qa_app_application_session(qa_app_application *a) { return a->session; }
qa_app_sim_shared_simulation *qa_app_application_simulation(qa_app_application *a) { return a->simulation; }
qa_app_loaded_content *qa_app_application_content(qa_app_application *a) { return a->content; }
double qa_app_application_max_clients(const qa_app_application *a) { return a->max_clients; }
size_t qa_app_application_local_seat_count(const qa_app_application *a) { return a->local_seats; }
bool qa_app_application_stopping(const qa_app_application *a) { return a->stopping; }
const qa_simulation_output *qa_app_application_last_output(const qa_app_application *a) {
  return a->has_last_output ? &a->last_output : NULL;
}
