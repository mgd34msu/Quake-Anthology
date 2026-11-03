/* Port of src/app/bootstrap/simulation/q3/guest-bots.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_bots.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/world/collision/index.h"
#include "qa/world/actors/registry.h"

enum { SOLID_BBOX = 2, SOLID_BSP = 3, BLERR_LIBRARYNOTSETUP = 1, BLERR_INVALIDENTITYNUMBER = 2, MAX_ENTITIES = 1024 };

/* One AasEntityInfo. restored_order: the object was built by restore (its key order). */
typedef struct observation {
  bool present, restored_order;
  qa_qvm_aas_entity_info info;
} observation;
typedef struct classname_entry { int64_t model; char *classname; } classname_entry;

struct qa_app_q3_guest_bots {
  qa_app_q3_guest_bot_options options;
  char *entities, *map_name;
  qa_source_bot_library_options_bots_a library_options;
  qa_bot_library_bots_a library;
  qa_aas_bsp_entities bsp_entities;
  qa_source_bot_navigation_bots_a navigation;
  /* TS observations: Map<number, AasEntityInfo>; order lists numbers in insertion order. */
  observation observations[MAX_ENTITIES];
  int32_t order[MAX_ENTITIES];
  size_t order_count;
  classname_entry *classnames;
  size_t classname_count, classname_capacity;
  bool initialized, loaded;
  qa_qvm_bot_library_services services;
};

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *copy_text(const char *text) { return checked(strdup(text == NULL ? "" : text)); }

/* ---- the BotLibrary options (TS { ...preparation.library, clientCommand }) ---- */
static const uint8_t *library_read(void *ctx, const char *path, size_t *out_len) {
  qa_app_q3_guest_bots *bots = ctx;
  const qa_bot_catalog_files_bots_a *files = &bots->options.library.files.files;
  return files->read(files->ctx, path, out_len);
}
static double library_milliseconds(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->options.library.milliseconds(bots->options.library.ctx);
}
static void library_print(void *ctx, int severity, const char *text) {
  qa_app_q3_guest_bots *bots = ctx;
  bots->options.library.print(bots->options.library.ctx, severity, text);
}
static void library_client_command(void *ctx, int64_t client, const char *command) {
  qa_app_q3_guest_bots *bots = ctx;
  bots->options.client_command(bots->options.command_ctx, client, command);
}
static qa_bot_log_open_result library_open_log(void *ctx, const char *filename) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->options.library.open_log(bots->options.library.ctx, filename);
}
static qa_bot_log_open_result library_resume_log(void *ctx, const char *filename, int64_t position) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->options.library.resume_log(bots->options.library.ctx, filename, position);
}
static void bsp_print(void *ctx, int severity, const char *text) { library_print(ctx, severity, text); }

/* ---- observations ---- */
static qa_qvm_aas_entity_info empty_info(double number) {
  qa_qvm_aas_entity_info info;
  memset(&info, 0, sizeof info);
  info.number = number;
  return info;
}
static const observation *observation_at(const qa_app_q3_guest_bots *bots, double number) {
  if (!(number >= 0 && number < MAX_ENTITIES) || number != trunc(number)) return NULL;
  const observation *entry = &bots->observations[(int32_t)number];
  return entry->present ? entry : NULL;
}
/* TS this.observations.get(number) ?? emptyInfo(number). */
static qa_qvm_aas_entity_info info_or_empty(const qa_app_q3_guest_bots *bots, double number) {
  const observation *entry = observation_at(bots, number);
  return entry == NULL ? empty_info(number) : entry->info;
}
static void observation_set(qa_app_q3_guest_bots *bots, int32_t number, qa_qvm_aas_entity_info info, bool restored_order) {
  observation *entry = &bots->observations[number];
  if (!entry->present) bots->order[bots->order_count++] = number;
  entry->present = true;
  entry->restored_order = restored_order;
  entry->info = info;
}
static void observations_clear(qa_app_q3_guest_bots *bots) {
  memset(bots->observations, 0, sizeof bots->observations);
  bots->order_count = 0;
}
static void classnames_clear(qa_app_q3_guest_bots *bots) {
  for (size_t i = 0; i < bots->classname_count; i++) free(bots->classnames[i].classname);
  bots->classname_count = 0;
}
/* TS nextEntity(after): the first valid number above after, else 0. */
static int64_t next_entity(qa_app_q3_guest_bots *bots, int64_t after) {
  int64_t start = after < 0 ? 0 : after + 1;
  for (int64_t number = start; number < MAX_ENTITIES; number++)
    if (bots->observations[number].present && bots->observations[number].info.valid) return number;
  return 0;
}

/* ---- AasBspEntities classnames ---- */
static char *bsp_value(qa_app_q3_guest_bots *bots, int64_t entity, const char *key) {
  uint8_t bytes[1024];
  memset(bytes, 0, sizeof bytes);
  if (!qa_aas_bsp_entities_value(&bots->bsp_entities, entity, key, bytes, sizeof bytes)) return copy_text("");
  size_t end = 0;
  while (end < sizeof bytes && bytes[end] != 0) end++;
  char *text = checked(malloc(end + 1));
  memcpy(text, bytes, end);
  text[end] = 0;
  return text;
}
static char *find_model_classname(qa_app_q3_guest_bots *bots, int64_t model) {
  char wanted[32];
  snprintf(wanted, sizeof wanted, "*%lld", (long long)model);
  for (int64_t entity = qa_aas_bsp_entities_next_entity(&bots->bsp_entities, 0); entity != 0;
       entity = qa_aas_bsp_entities_next_entity(&bots->bsp_entities, entity)) {
    char *value = bsp_value(bots, entity, "model");
    bool match = strcmp(value, wanted) == 0;
    free(value);
    if (match) return bsp_value(bots, entity, "classname");
  }
  return copy_text("");
}
static const char *model_classname(qa_app_q3_guest_bots *bots, int64_t model) {
  for (size_t i = 0; i < bots->classname_count; i++)
    if (bots->classnames[i].model == model) return bots->classnames[i].classname;
  char *found = find_model_classname(bots, model);
  if (bots->classname_count == bots->classname_capacity) {
    size_t next = bots->classname_capacity == 0 ? 8 : bots->classname_capacity * 2;
    bots->classnames = checked(realloc(bots->classnames, next * sizeof *bots->classnames));
    bots->classname_capacity = next;
  }
  bots->classnames[bots->classname_count].model = model;
  bots->classnames[bots->classname_count].classname = found;
  return bots->classnames[bots->classname_count++].classname;
}

/* ---- SourceBotNavigation host ---- */
static qa_navigation_runtime *nav_for_client(void *ctx, int64_t client) {
  qa_app_q3_guest_bots *bots = ctx;
  const qa_selected_bot_navigation_bots_a *selected = &bots->options.selected->selected;
  return selected->for_client(selected->ctx, client);
}
static double nav_time(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  return qa_bot_library_time_bots_a(&bots->library);
}
static int64_t nav_point_contents(void *ctx, qa_vec3 point) {
  qa_app_q3_guest_bots *bots = ctx;
  return qa_app_q3_guest_spatial_point_contents(bots->options.spatial, point, -1);
}
static int64_t nav_entity_model_index(void *ctx, int64_t number) {
  const observation *entry = observation_at(ctx, (double)number);
  return entry == NULL ? 0 : (int64_t)entry->info.model_index;
}
static int64_t nav_entity_type(void *ctx, int64_t number) {
  const observation *entry = observation_at(ctx, (double)number);
  return entry == NULL ? 0 : (int64_t)entry->info.type;
}
static int64_t nav_entity_weapon(void *ctx, int64_t number) {
  const observation *entry = observation_at(ctx, (double)number);
  return entry == NULL ? 0 : (int64_t)entry->info.weapon;
}
static qa_bot_server_trace_result nav_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds,
                                            int64_t pass_entity, int64_t mask) {
  qa_app_q3_guest_bots *bots = ctx;
  qa_qvm_server_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = start;
  query.end = end;
  query.pass_entity_num = qa_js_to_int32((double)pass_entity);
  query.mask = qa_js_to_int32((double)mask);
  query.shape_kind = bounds == NULL ? "point" : "box";
  if (bounds != NULL) query.shape_bounds = *bounds;
  qa_qvm_trace_record trace = qa_app_q3_guest_spatial_trace(bots->options.spatial, &query);
  qa_bot_server_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = trace.result.fraction;
  out.end = trace.result.end;
  out.entity_num = trace.entity_num;
  out.contents = trace.result.contents;
  out.surface_flags = trace.result.surface_flags;
  out.solidity = trace.result.all_solid ? "all-solid" : trace.result.start_solid ? "start-solid" : "clear";
  out.has_plane = trace.result.fraction != 1;
  if (out.has_plane) out.plane = trace.result.plane.plane;
  return out;
}
static qa_bot_travel_prediction_result nav_predict(void *ctx, qa_bot_movement_prediction query) {
  qa_app_q3_guest_bots *bots = ctx;
  const qa_selected_bot_navigation_bots_a *selected = &bots->options.selected->selected;
  return selected->predict_client_movement(selected->ctx, query);
}
static bool nav_model_bounds(void *ctx, int64_t model, qa_bounds *out) {
  qa_app_q3_guest_bots *bots = ctx;
  *out = qa_app_q3_guest_spatial_model_bounds(bots->options.spatial, (double)model);
  return true;
}
static bool nav_model_info(void *ctx, int64_t model, qa_bot_travel_model *out) {
  qa_app_q3_guest_bots *bots = ctx;
  for (size_t i = 0; i < bots->order_count; i++) {
    const observation *entry = &bots->observations[bots->order[i]];
    if (entry->info.model_index != (double)model) continue;
    qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(bots->options.records, bots->order[i]);
    qa_qvm_entity_collision_model collision = qa_qvm_shared_entity_get_model(entity);
    if (collision.kind == NULL || strcmp(collision.kind, "inline") != 0) continue;
    const char *classname = model_classname(bots, model);
    memset(out, 0, sizeof *out);
    out->entity = bots->order[i];
    out->origin = entry->info.origin;
    out->bounds.min = entry->info.mins;
    out->bounds.max = entry->info.maxs;
    out->kind = strcmp(classname, "func_plat") == 0      ? "elevator"
                : strcmp(classname, "func_bobbing") == 0 ? "bobbing"
                : strcmp(classname, "func_door") == 0    ? "door"
                : strcmp(classname, "func_train") == 0   ? "train"
                                                         : "static";
    return true;
  }
  return false;
}
static int64_t nav_next_entity(void *ctx, int64_t after) { return next_entity(ctx, after); }
static bool nav_travel_weapon(void *ctx, int64_t client, const char *mode, int64_t *out) {
  qa_app_q3_guest_bots *bots = ctx;
  const qa_selected_bot_navigation_bots_a *selected = &bots->options.selected->selected;
  return selected->travel_weapon(selected->ctx, client, mode, out);
}

/* ---- goalWorld() ---- */
static double goal_trace(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, int64_t pass_entity, int64_t mask) {
  return nav_trace(ctx, start, end, bounds, pass_entity, mask).fraction;
}
static qa_goal_entity_info goal_entity_info(void *ctx, int64_t number) {
  qa_qvm_aas_entity_info info = info_or_empty(ctx, (double)number);
  qa_goal_entity_info out;
  memset(&out, 0, sizeof out);
  out.type = (int64_t)info.type;
  out.model_index = (int64_t)info.model_index;
  out.origin = info.origin;
  out.last_visible_origin = info.last_visible_origin;
  out.last_update_time = info.last_update_time;
  return out;
}
static int64_t goal_point_area(void *ctx, qa_vec3 origin) {
  qa_app_q3_guest_bots *bots = ctx;
  return qa_source_bot_navigation_point_area(&bots->navigation, origin);
}
static qa_goal_world goal_world(qa_app_q3_guest_bots *bots) {
  qa_goal_world world;
  memset(&world, 0, sizeof world);
  world.bsp_entities = &bots->bsp_entities;
  world.has_navigation = true;
  world.navigation = qa_source_bot_navigation_goal_interface(&bots->navigation);
  world.point_area = goal_point_area;
  world.point_area_ctx = bots;
  world.host.ctx = bots;
  world.host.trace = goal_trace;
  world.host.point_contents = nav_point_contents;
  world.host.next_entity = nav_next_entity;
  world.host.entity_info = goal_entity_info;
  return world;
}

/* ---- services ---- */
static double service_setup(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  int64_t result = qa_bot_library_setup_bots_a(&bots->library);
  bots->initialized = result == 0;
  return (double)result;
}
static double service_shutdown(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  bots->initialized = false;
  bots->loaded = false;
  return (double)qa_bot_library_shutdown_bots_a(&bots->library);
}
static double service_load_map(void *ctx, const char *name) {
  qa_app_q3_guest_bots *bots = ctx;
  if (strcmp(name, bots->map_name) != 0)
    qa_raise("Error", 0, 0, "Bot map %s does not match active guest world %s", name, bots->map_name);
  qa_aas_bsp_entities_load(&bots->bsp_entities, bots->entities);
  observations_clear(bots);
  classnames_clear(bots);
  qa_goal_world world = goal_world(bots);
  int64_t result = qa_bot_library_load_map_bots_a(&bots->library, &world);
  bots->loaded = result == 0;
  return (double)result;
}
static void service_started_frame(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  for (size_t i = 0; i < bots->order_count; i++) {
    observation *entry = &bots->observations[bots->order[i]];
    if (entry->info.valid) entry->info.valid = false;
  }
}
/* AAS_UpdateEntity: a brush entity takes its bounds from the model (a radius box once rotated), and other
 * solids keep the last mins, maxs and angles. */
static qa_qvm_bot_entity_update source_bounds(qa_app_q3_guest_bots *bots, qa_qvm_bot_entity_update state, const observation *previous) {
  if (state.solid == SOLID_BBOX) return state;
  if (state.solid != SOLID_BSP) {
    if (previous == NULL) return state;
    state.mins = previous->info.mins;
    state.maxs = previous->info.maxs;
    state.angles = previous->info.angles;
    return state;
  }
  qa_bounds bounds = qa_app_q3_guest_spatial_model_bounds(bots->options.spatial, state.model_index);
  if (state.angles.x == 0 && state.angles.y == 0 && state.angles.z == 0) {
    state.mins = bounds.min;
    state.maxs = bounds.max;
    return state;
  }
  double radius = qa_js_hypot3(qa_js_max(fabs(bounds.min.x), fabs(bounds.max.x)), qa_js_max(fabs(bounds.min.y), fabs(bounds.max.y)),
                               qa_js_max(fabs(bounds.min.z), fabs(bounds.max.z)));
  state.mins = (qa_vec3){-radius, -radius, -radius};
  state.maxs = (qa_vec3){radius, radius, radius};
  return state;
}
static double service_update_entity(void *ctx, double number, const qa_qvm_host_call *call, double pointer) {
  qa_app_q3_guest_bots *bots = ctx;
  if (!bots->initialized) return BLERR_LIBRARYNOTSETUP;
  if (number < 0 || number >= MAX_ENTITIES) {
    char text[96];
    snprintf(text, sizeof text, "AAS_UpdateEntity: entnum %s out of range\n", QA_JS_NUMBER(number));
    bots->options.library.print(bots->options.library.ctx, 4, text);
    return BLERR_INVALIDENTITYNUMBER;
  }
  int32_t slot = (int32_t)number;
  /* AAS keeps an entity's last state when the game stops updating it; only `valid` clears. */
  if (pointer == 0) {
    observation *previous = &bots->observations[slot];
    if (previous->present) previous->info.valid = false;
    return 0;
  }
  qa_qvm_writable_view view;
  qa_qvm_memory_view(call->guest, (int64_t)pointer, QA_QVM_BOT_ENTITY_STATE_BYTES, 0, &view);
  qa_qvmx_view record = {view.bytes + view.start, (size_t)view.length};
  qa_qvm_bot_entity_update read = qa_qvm_read_bot_entity_state(record);
  const observation *previous = observation_at(bots, number);
  double time = qa_bot_library_time_bots_a(&bots->library);
  qa_qvm_bot_entity_update state = source_bounds(bots, read, previous);
  qa_qvm_aas_entity_info prior = previous == NULL ? empty_info(number) : previous->info;
  qa_qvm_aas_entity_info info;
  memset(&info, 0, sizeof info);
  info.type = state.type;
  info.flags = state.flags;
  info.origin = state.origin;
  info.angles = state.angles;
  info.old_origin = state.old_origin;
  info.mins = state.mins;
  info.maxs = state.maxs;
  info.ground_entity = state.ground_entity;
  info.solid = state.solid;
  info.model_index = state.model_index;
  info.model_index2 = state.model_index2;
  info.frame = state.frame;
  info.event = state.event;
  info.event_parameter = state.event_parameter;
  info.powerups = state.powerups;
  info.weapon = state.weapon;
  info.legs_animation = state.legs_animation;
  info.torso_animation = state.torso_animation;
  info.number = number;
  info.valid = true;
  info.last_visible_origin = prior.origin;
  info.last_update_time = time;
  info.update_interval = time - prior.last_update_time;
  observation_set(bots, slot, info, false);
  return 0;
}
static double service_snapshot_entity(void *ctx, double client, double sequence) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->options.clients.snapshot_entity(bots->options.clients.ctx, client, sequence);
}
static char *service_console_message(void *ctx, double client) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->options.clients.console_message(bots->options.clients.ctx, client);
}
static void service_user_command(void *ctx, double client, qa_qvmx_wire_user_command command) {
  qa_app_q3_guest_bots *bots = ctx;
  bots->options.clients.user_command(bots->options.clients.ctx, client, command);
}
static double service_allocate_client(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->options.clients.allocate_client(bots->options.clients.ctx);
}
static void service_free_client(void *ctx, double client) {
  qa_app_q3_guest_bots *bots = ctx;
  bots->options.clients.free_client(bots->options.clients.ctx, client);
}

qa_app_q3_guest_bots *qa_app_q3_guest_bots_create(const qa_app_q3_guest_bot_options *options) {
  qa_app_q3_guest_bots *bots = calloc(1, sizeof *bots);
  if (bots == NULL) return NULL;
  bots->options = *options;
  bots->entities = strdup(options->entities == NULL ? "" : options->entities);
  bots->map_name = strdup(options->map_name == NULL ? "" : options->map_name);
  if (bots->entities == NULL || bots->map_name == NULL) {
    free(bots->entities);
    free(bots->map_name);
    free(bots);
    return NULL;
  }
  qa_source_bot_library_options_bots_a *library = &bots->library_options;
  library->ctx = bots;
  library->read_file = library_read;
  library->random = options->library.random;
  library->debug = options->library.debug;
  library->milliseconds = library_milliseconds;
  library->print = library_print;
  library->client_command = library_client_command;
  library->open_log = library_open_log;
  library->resume_log = options->library.resume_log == NULL ? NULL : library_resume_log;
  qa_bot_library_bots_a_init(&bots->library, library);
  qa_aas_bsp_entities_init(&bots->bsp_entities);
  qa_aas_bsp_entities_configure(&bots->bsp_entities, bsp_print, bots, &bots->library.memory);
  const qa_selected_bot_navigation_bots_a *selected = &options->selected->selected;
  qa_source_bot_navigation_host_bots_a host;
  memset(&host, 0, sizeof host);
  host.ctx = bots;
  host.runtime = selected->runtime;
  host.bsp_entities = &bots->bsp_entities;
  host.for_client = nav_for_client;
  host.move_states = &bots->library.move_states;
  host.actions = &bots->library.actions;
  host.random = options->library.random;
  host.crouched_bounds = selected->crouched_bounds;
  host.time = nav_time;
  host.point_contents = nav_point_contents;
  host.entity_model_index = nav_entity_model_index;
  host.trace = nav_trace;
  host.predict_client_movement = nav_predict;
  host.model_bounds = nav_model_bounds;
  host.model_info = nav_model_info;
  host.next_entity = nav_next_entity;
  host.entity_type = nav_entity_type;
  host.entity_weapon = nav_entity_weapon;
  host.travel_weapon = nav_travel_weapon;
  qa_source_bot_navigation_bots_a_init(&bots->navigation, &host);
  qa_qvm_bot_library_services *services = &bots->services;
  services->library = &bots->library;
  services->ctx = bots;
  services->setup = service_setup;
  services->shutdown = service_shutdown;
  services->load_map = service_load_map;
  services->update_entity = service_update_entity;
  services->snapshot_entity = service_snapshot_entity;
  services->console_message = service_console_message;
  services->user_command = service_user_command;
  services->allocate_client = service_allocate_client;
  services->free_client = service_free_client;
  services->started_frame = service_started_frame;
  return bots;
}
void qa_app_q3_guest_bots_destroy(qa_app_q3_guest_bots *bots) {
  if (bots == NULL) return;
  classnames_clear(bots);
  free(bots->classnames);
  qa_aas_bsp_entities_destroy(&bots->bsp_entities);
  qa_bot_library_bots_a_destroy(&bots->library);
  free(bots->entities);
  free(bots->map_name);
  free(bots);
}
qa_bot_library_bots_a *qa_app_q3_guest_bots_library(qa_app_q3_guest_bots *bots) { return &bots->library; }
qa_source_bot_navigation_bots_a *qa_app_q3_guest_bots_navigation(qa_app_q3_guest_bots *bots) { return &bots->navigation; }
qa_aas_bsp_entities *qa_app_q3_guest_bots_bsp_entities(qa_app_q3_guest_bots *bots) { return &bots->bsp_entities; }

/* ---- syscall ---- */
static double nav_services_time(void *ctx) { return nav_time(ctx); }
static bool nav_services_initialized(void *ctx) {
  qa_app_q3_guest_bots *bots = ctx;
  return bots->initialized && bots->loaded && qa_source_bot_navigation_ready(&bots->navigation);
}
static bool nav_services_entity_info(void *ctx, double number, qa_qvm_aas_entity_info *out) {
  *out = info_or_empty(ctx, number);
  return true;
}
static double require_slot(void *ctx, qa_actor_id actor) {
  qa_app_q3_guest_bots *bots = ctx;
  return qa_app_q3_guest_records_require_slot(bots->options.records, actor);
}
static qa_qvm_aas_movement_prediction nav_services_predict(void *ctx, qa_bot_movement_prediction query) {
  qa_app_q3_guest_bots *bots = ctx;
  qa_qvm_entity_number_fn entity_number = {bots, require_slot};
  return qa_qvm_bot_movement_prediction(&bots->navigation, query, entity_number);
}
bool qa_app_q3_guest_bots_syscall(qa_app_q3_guest_bots *bots, const qa_qvm_host_call *call, qa_qvm_host_result *out) {
  if (qa_qvm_bot_library_syscall(call, &bots->services, out)) return true;
  qa_qvm_bot_navigation_services services;
  memset(&services, 0, sizeof services);
  services.navigation = &bots->navigation;
  services.library = &bots->library;
  services.bsp_entities = &bots->bsp_entities;
  services.ctx = bots;
  services.time = nav_services_time;
  services.initialized = nav_services_initialized;
  services.entity_info = nav_services_entity_info;
  services.predict_client_movement = nav_services_predict;
  return qa_qvm_bot_navigation_syscall(call, &services, out);
}

/* ---- checkpoint ---- */
static void set_or_raise(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    qa_range_error("Out of memory");
  }
}
static qa_save_value *vector_value(qa_vec3 v) {
  qa_save_value *object = checked(qa_save_value_object());
  set_or_raise(object, "x", qa_save_value_number(v.x));
  set_or_raise(object, "y", qa_save_value_number(v.y));
  set_or_raise(object, "z", qa_save_value_number(v.z));
  return object;
}
/* One AasEntityInfo in the key order of the object that holds it. */
static qa_save_value *observation_value(const observation *entry) {
  const qa_qvm_aas_entity_info *info = &entry->info;
  qa_save_value *object = checked(qa_save_value_object());
  if (entry->restored_order) {
    set_or_raise(object, "number", qa_save_value_number(info->number));
    set_or_raise(object, "valid", qa_save_value_boolean(info->valid));
    set_or_raise(object, "type", qa_save_value_number(info->type));
    set_or_raise(object, "flags", qa_save_value_number(info->flags));
    set_or_raise(object, "origin", vector_value(info->origin));
    set_or_raise(object, "oldOrigin", vector_value(info->old_origin));
    set_or_raise(object, "angles", vector_value(info->angles));
    set_or_raise(object, "mins", vector_value(info->mins));
    set_or_raise(object, "maxs", vector_value(info->maxs));
    set_or_raise(object, "lastVisibleOrigin", vector_value(info->last_visible_origin));
    set_or_raise(object, "lastUpdateTime", qa_save_value_number(info->last_update_time));
    set_or_raise(object, "updateInterval", qa_save_value_number(info->update_interval));
  } else {
    set_or_raise(object, "type", qa_save_value_number(info->type));
    set_or_raise(object, "flags", qa_save_value_number(info->flags));
    set_or_raise(object, "origin", vector_value(info->origin));
    set_or_raise(object, "angles", vector_value(info->angles));
    set_or_raise(object, "oldOrigin", vector_value(info->old_origin));
    set_or_raise(object, "mins", vector_value(info->mins));
    set_or_raise(object, "maxs", vector_value(info->maxs));
  }
  set_or_raise(object, "groundEntity", qa_save_value_number(info->ground_entity));
  set_or_raise(object, "solid", qa_save_value_number(info->solid));
  set_or_raise(object, "modelIndex", qa_save_value_number(info->model_index));
  set_or_raise(object, "modelIndex2", qa_save_value_number(info->model_index2));
  set_or_raise(object, "frame", qa_save_value_number(info->frame));
  set_or_raise(object, "event", qa_save_value_number(info->event));
  set_or_raise(object, "eventParameter", qa_save_value_number(info->event_parameter));
  set_or_raise(object, "powerups", qa_save_value_number(info->powerups));
  set_or_raise(object, "weapon", qa_save_value_number(info->weapon));
  set_or_raise(object, "legsAnimation", qa_save_value_number(info->legs_animation));
  set_or_raise(object, "torsoAnimation", qa_save_value_number(info->torso_animation));
  if (!entry->restored_order) {
    set_or_raise(object, "number", qa_save_value_number(info->number));
    set_or_raise(object, "valid", qa_save_value_boolean(info->valid));
    set_or_raise(object, "lastVisibleOrigin", vector_value(info->last_visible_origin));
    set_or_raise(object, "lastUpdateTime", qa_save_value_number(info->last_update_time));
    set_or_raise(object, "updateInterval", qa_save_value_number(info->update_interval));
  }
  return object;
}
static const char *provenance(qa_app_q3_guest_bots *bots) {
  const qa_source_bot_files *files = &bots->options.library.files;
  return files->provenance == NULL ? NULL : files->provenance(files->files.ctx);
}
qa_save_value *qa_app_q3_guest_bots_checkpoint(qa_app_q3_guest_bots *bots) {
  const char *assets = provenance(bots);
  if (assets == NULL) qa_raise("Error", 0, 0, "Guest bot persistence requires mounted asset provenance");
  qa_bot_memory_capture memory;
  if (!qa_bot_memory_checkpoint(&bots->library.memory, &memory)) qa_range_error("Out of memory");
  qa_save_value *root = checked(qa_save_value_object());
  set_or_raise(root, "version", qa_save_value_number(1));
  set_or_raise(root, "assets", qa_save_value_string(assets));
  set_or_raise(root, "initialized", qa_save_value_boolean(bots->initialized));
  set_or_raise(root, "loaded", qa_save_value_boolean(bots->loaded));
  set_or_raise(root, "memory", memory.image);
  memory.image = NULL;
  set_or_raise(root, "library", qa_bot_library_capture_save_state(&bots->library, &memory));
  set_or_raise(root, "bsp", qa_aas_bsp_entities_checkpoint(&bots->bsp_entities, &memory));
  set_or_raise(root, "navigation", bots->options.selected->checkpoint(bots->options.selected->ctx));
  qa_save_value *observations = checked(qa_save_value_array());
  for (size_t i = 0; i < bots->order_count; i++) {
    qa_save_value *item = observation_value(&bots->observations[bots->order[i]]);
    if (!qa_save_value_push(observations, item)) {
      qa_save_value_free(item);
      qa_save_value_free(observations);
      qa_range_error("Out of memory");
    }
  }
  set_or_raise(root, "observations", observations);
  qa_bot_memory_capture_destroy(&memory);
  return root;
}

/* ---- restore ---- */
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static double read_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader cell = field(reader, name);
  double value = qa_save_reader_number(&cell);
  qa_save_reader_destroy(&cell);
  return value;
}
static double read_integer(const qa_save_reader *reader, const char *name) {
  qa_save_reader cell = field(reader, name);
  double value = qa_save_reader_integer(&cell);
  qa_save_reader_destroy(&cell);
  return value;
}
static bool read_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader cell = field(reader, name);
  bool value = qa_save_reader_boolean(&cell);
  qa_save_reader_destroy(&cell);
  return value;
}
static qa_vec3 read_vector(const qa_save_reader *reader, const char *name) {
  qa_save_reader cell = field(reader, name);
  qa_vec3 value;
  value.x = read_number(&cell, "x");
  value.y = read_number(&cell, "y");
  value.z = read_number(&cell, "z");
  qa_save_reader_destroy(&cell);
  return value;
}
static qa_actor_id restore_actor(void *ctx, qa_saved_actor_id saved) {
  qa_app_q3_guest_bots *bots = ctx;
  const qa_app_q3_guest_record_host *host = qa_app_q3_guest_records_host_of(bots->options.records);
  qa_owned_actor actor;
  if (!qa_session_actor_registry_resolve_saved(host->actors, saved, &actor))
    qa_raise("Error", 0, 0, "Saved guest bot actor is absent");
  return actor.id;
}
static bool restore_edge(void *ctx, int64_t client, int64_t id) {
  qa_app_q3_guest_bots *bots = ctx;
  return qa_navigation_runtime_edge(nav_for_client(bots, client), id) != NULL;
}
typedef struct restore_frame {
  qa_save_reader reader, library;
  bool has_library;
  qa_bot_memory_restored memory;
  bool has_memory;
} restore_frame;
static void restore_release(void *ctx) {
  restore_frame *frame = ctx;
  if (frame->has_memory) qa_bot_memory_restored_destroy(&frame->memory);
  if (frame->has_library) qa_save_reader_destroy(&frame->library);
  qa_save_reader_destroy(&frame->reader);
}
void qa_app_q3_guest_bots_restore(qa_app_q3_guest_bots *bots, const qa_save_value *value) {
  restore_frame frame;
  memset(&frame, 0, sizeof frame);
  if (!qa_save_reader_init(&frame.reader, value, "q3.guest.bots")) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, restore_release, &frame);
  qa_save_reader cell = field(&frame.reader, "version");
  qa_save_reader_literal_number(&cell, 1);
  qa_save_reader_destroy(&cell);
  const char *assets = provenance(bots);
  bool matches = false;
  if (assets != NULL) {
    cell = field(&frame.reader, "assets");
    matches = strcmp(qa_save_reader_string(&cell), assets) == 0;
    qa_save_reader_destroy(&cell);
  }
  if (!matches) qa_raise("Error", 0, 0, "Guest bot assets differ from saved source");
  cell = field(&frame.reader, "navigation");
  bots->options.selected->restore_checkpoint(bots->options.selected->ctx, cell.value);
  qa_save_reader_destroy(&cell);
  cell = field(&frame.reader, "memory");
  qa_bot_memory_restore(&bots->library.memory, cell.value, &frame.memory);
  frame.has_memory = true;
  qa_save_reader_destroy(&cell);
  cell = field(&frame.reader, "bsp");
  qa_aas_bsp_entities_restore(&bots->bsp_entities, cell.value, &frame.memory);
  qa_save_reader_destroy(&cell);
  frame.library = field(&frame.reader, "library");
  frame.has_library = true;
  qa_save_reader goals = field(&frame.library, "goals");
  bool has_world = read_boolean(&goals, "hasWorld");
  qa_save_reader_destroy(&goals);
  qa_goal_world world = goal_world(bots);
  qa_bot_library_restore_save_state(&bots->library, frame.library.value, &frame.memory, has_world ? &world : NULL, restore_actor,
                                    bots, restore_edge, bots);
  qa_save_reader list = field(&frame.reader, "observations");
  size_t count = qa_save_reader_list_count(&list);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&list, i, &item)) qa_range_error("Out of memory");
    qa_qvm_aas_entity_info info;
    memset(&info, 0, sizeof info);
    qa_save_reader number = field(&item, "number");
    info.number = qa_save_reader_integer_min(&number, 0);
    qa_save_reader_destroy(&number);
    info.valid = read_boolean(&item, "valid");
    info.type = read_integer(&item, "type");
    info.flags = read_integer(&item, "flags");
    info.origin = read_vector(&item, "origin");
    info.old_origin = read_vector(&item, "oldOrigin");
    info.angles = read_vector(&item, "angles");
    info.mins = read_vector(&item, "mins");
    info.maxs = read_vector(&item, "maxs");
    info.last_visible_origin = read_vector(&item, "lastVisibleOrigin");
    info.last_update_time = read_number(&item, "lastUpdateTime");
    info.update_interval = read_number(&item, "updateInterval");
    info.ground_entity = read_integer(&item, "groundEntity");
    info.solid = read_integer(&item, "solid");
    info.model_index = read_integer(&item, "modelIndex");
    info.model_index2 = read_integer(&item, "modelIndex2");
    info.frame = read_integer(&item, "frame");
    info.event = read_integer(&item, "event");
    info.event_parameter = read_integer(&item, "eventParameter");
    info.powerups = read_integer(&item, "powerups");
    info.weapon = read_integer(&item, "weapon");
    info.legs_animation = read_integer(&item, "legsAnimation");
    info.torso_animation = read_integer(&item, "torsoAnimation");
    qa_save_reader_destroy(&item);
    /* The C table holds AAS entity numbers below MAX_ENTITIES (AAS_UpdateEntity's range). */
    if (info.number >= MAX_ENTITIES) qa_save_reader_fail(&list, "guest bot observation outside the AAS entity range");
    if (bots->observations[(int32_t)info.number].present) qa_raise("Error", 0, 0, "Duplicate guest bot observation");
    observation_set(bots, (int32_t)info.number, info, true);
  }
  qa_save_reader_destroy(&list);
  bots->initialized = read_boolean(&frame.reader, "initialized");
  bots->loaded = read_boolean(&frame.reader, "loaded");
  qa_cleanup_pop(&cleanup);
  restore_release(&frame);
}

void qa_app_q3_guest_bots_close(qa_app_q3_guest_bots *bots) { (void)service_shutdown(bots); }
