/* Q3SourceRuntime construction (q3/runtime.ts constructor + load) over retail
 * maps, compared line for line with the TS oracle dump
 * (tests/app/oracles/q3_source_oracle.ts -> q3_source_<map>.txt): engine
 * prints/logs, configstrings, spawn outcomes, world variables, spawned
 * entities and cvars. Every host callback checks its sentinel context.
 * Skips when ~/qfiles is absent. GPL-2.0-or-later. */
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/app/bootstrap/simulation/physics.h"
#include "qa/app/bootstrap/simulation/q3/guest_artifact.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include <dirent.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/content/archive.h"
#include "qa/content/q3/base/settings.h"
#include "qa/contracts/identity.h"
#include "qa/core/numeric.h"
#include "qa/formats/q3_map/world.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/actors/registry.h"
#include "qa/world/collision/index.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/world/gameplay/original_pickups.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static int HOST_SENTINEL, EMIT_SENTINEL, MOVER_SENTINEL, PHYSICS_SENTINEL;
#define SENTINEL(ctx, which) do { if ((ctx) != (void *)&(which)) { fprintf(stderr, "wrong ctx in %s\n", __func__); abort(); } } while (0)

typedef struct dump { char **lines; size_t count, capacity; } dump;
static void push(dump *d, const char *format, ...) {
  char buffer[8192];
  va_list args; va_start(args, format); vsnprintf(buffer, sizeof buffer, format, args); va_end(args);
  if (d->count == d->capacity) { d->capacity = d->capacity ? d->capacity * 2 : 256; d->lines = realloc(d->lines, d->capacity * sizeof *d->lines); }
  d->lines[d->count++] = strdup(buffer);
}
/* JSON.stringify of a string. */
static const char *json(const char *text, char *out, size_t size) {
  size_t used = 0;
  out[used++] = '"';
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p && used + 8 < size; p++) {
    if (*p == '"' || *p == '\\') { out[used++] = '\\'; out[used++] = (char)*p; }
    else if (*p == '\n') { out[used++] = '\\'; out[used++] = 'n'; }
    else if (*p == '\t') { out[used++] = '\\'; out[used++] = 't'; }
    else if (*p == '\r') { out[used++] = '\\'; out[used++] = 'r'; }
    else if (*p < 0x20) used += (size_t)snprintf(out + used, size - used, "\\u%04x", *p);
    else out[used++] = (char)*p;
  }
  out[used++] = '"'; out[used] = 0;
  return out;
}

static dump events;
static void emit(void *ctx, qa_app_q3_source_event event) {
  SENTINEL(ctx, EMIT_SENTINEL);
  char a[8192];
  if (event.kind == QA_APP_Q3_EVENT_PRINT) push(&events, "print %s", json(event.text, a, sizeof a));
  else if (event.kind == QA_APP_Q3_EVENT_LOG) push(&events, "log %s", json(event.text, a, sizeof a));
  else if (event.kind == QA_APP_Q3_EVENT_CONFIGSTRING) push(&events, "configstring %s %s", QA_JS_NUMBER(event.index), json(event.value, a, sizeof a));
  else push(&events, "event %d", (int)event.kind);
}
static double client_number(void *ctx, qa_actor_id actor) { SENTINEL(ctx, EMIT_SENTINEL); (void)actor; return -1; }
static double host_now(void *ctx) { SENTINEL(ctx, HOST_SENTINEL); return 100; }
static void host_schedule(void *ctx, qa_owned_actor actor, bool has_due, double due) { SENTINEL(ctx, HOST_SENTINEL); (void)actor; (void)has_due; (void)due; }
static void host_run_think(void *ctx, qa_owned_actor actor, double time) { SENTINEL(ctx, HOST_SENTINEL); (void)actor; (void)time; }
static qa_app_sim_shared_physics *physics;
static void host_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  SENTINEL(ctx, HOST_SENTINEL); qa_app_sim_shared_physics_set_collision(physics, actor, collision);
}
static qa_victim_armor_context host_armor(void *ctx, qa_damage_request request) {
  SENTINEL(ctx, HOST_SENTINEL); (void)request;
  qa_victim_armor_context context; memset(&context, 0, sizeof context); context.screen_facing_dot = 1; context.arithmetic = "binary32";
  return context;
}
static qa_q3_game_entity *host_foreign(void *ctx, qa_actor_id actor) { SENTINEL(ctx, HOST_SENTINEL); (void)actor; return NULL; }
static bool host_is_player(void *ctx, qa_actor_id actor) { SENTINEL(ctx, HOST_SENTINEL); (void)actor; return false; }
static qa_q3_base_user_command host_source_command(void *ctx, qa_actor_command input) {
  SENTINEL(ctx, HOST_SENTINEL); (void)input; qa_range_error("unused");
}
static qa_q3_ta_client_movement_result host_move(void *ctx, qa_q3_game_entity *entity, qa_q3_base_user_command command,
    qa_q3_ta_client_movement_options options) {
  SENTINEL(ctx, HOST_SENTINEL); (void)entity; (void)command; (void)options; qa_range_error("unused");
}
static void host_select(void *ctx, qa_q3_game_entity *entity) { SENTINEL(ctx, HOST_SENTINEL); (void)entity; }
static void host_spawn(void *ctx, qa_q3_game_entity *entity, qa_q3_ta_spawn_pose pose) { SENTINEL(ctx, HOST_SENTINEL); (void)entity; (void)pose; }
static qa_session_actor_registry *actors;
static bool mover_observe(void *ctx, qa_actor_id actor, qa_q3_shared_mover_body *out) { SENTINEL(ctx, MOVER_SENTINEL); (void)actor; (void)out; return false; }
static void mover_write(void *ctx, qa_owned_actor actor, qa_vec3 origin, const qa_actor_id *ground) { SENTINEL(ctx, MOVER_SENTINEL); (void)actor; (void)origin; (void)ground; }
static void mover_link(void *ctx, qa_owned_actor actor) {
  SENTINEL(ctx, MOVER_SENTINEL); qa_shared_body_table_link(qa_app_sim_shared_physics_bodies(physics), actor, NULL);
}
static void mover_release(void *ctx, qa_owned_actor actor) { SENTINEL(ctx, MOVER_SENTINEL); qa_session_actor_registry_release(actors, actor); }
static double p_order(void *ctx, qa_actor_id a, qa_actor_id b) {
  SENTINEL(ctx, PHYSICS_SENTINEL);
  qa_actor_source_result sa = qa_session_actor_registry_source_of(actors, a), sb = qa_session_actor_registry_source_of(actors, b);
  double left = sa.has_value ? (double)sa.slot : (double)a.slot;
  double right = sb.has_value ? (double)sb.slot : (double)b.slot;
  return left - right;
}
static bool p_world(void *ctx, qa_actor_id *out) {
  SENTINEL(ctx, PHYSICS_SENTINEL);
  qa_owned_actor actor;
  if (!qa_session_actor_registry_at_source(actors, "q3:game", 1022, &actor)) return false;
  *out = actor.id; return true;
}
static void p_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id other) { SENTINEL(ctx, PHYSICS_SENTINEL); (void)pusher; (void)other; }

static char *read_golden(const char *name, size_t *out_length);
static int GUEST_SENTINEL, OUTPUT_SENTINEL, COMMON_SENTINEL, STATE_SENTINEL, RECORDS_SENTINEL;
static dump guest_out;
static void guest_state_print(void *ctx, const char *text) { SENTINEL(ctx, STATE_SENTINEL); char a[8192]; push(&guest_out, "print %s", json(text, a, sizeof a)); }
static double guest_state_now(void *ctx) { SENTINEL(ctx, STATE_SENTINEL); return 1000; }
static double guest_now(void *ctx) { SENTINEL(ctx, GUEST_SENTINEL); return 1000; }
static void guest_assert_current(void *ctx) { SENTINEL(ctx, GUEST_SENTINEL); }
static double common_ms(void *ctx) { SENTINEL(ctx, COMMON_SENTINEL); return 1; }
static double common_real(void *ctx, bool has_output, qa_qvm_calendar *out) { SENTINEL(ctx, COMMON_SENTINEL); (void)has_output; (void)out; return 0; }
static void common_execute(void *ctx, const char *text) { SENTINEL(ctx, COMMON_SENTINEL); (void)text; push(&guest_out, "execute"); }
static void common_append(void *ctx, const char *text) { SENTINEL(ctx, COMMON_SENTINEL); char a[8192]; push(&guest_out, "append %s", json(text, a, sizeof a)); }
static void common_insert(void *ctx, const char *text) { SENTINEL(ctx, COMMON_SENTINEL); char a[8192]; push(&guest_out, "insert %s", json(text, a, sizeof a)); }
static void output_drop(void *ctx, double slot, const char *reason) { SENTINEL(ctx, OUTPUT_SENTINEL); (void)slot; (void)reason; }
static void output_server(void *ctx, double slot, const char *text) {
  SENTINEL(ctx, OUTPUT_SENTINEL); char a[8192]; push(&guest_out, "server %s %s", QA_JS_NUMBER(slot), json(text, a, sizeof a));
}
static void output_configstring(void *ctx, double index, const char *value) {
  SENTINEL(ctx, OUTPUT_SENTINEL); char a[8192]; push(&guest_out, "configstring %s %s", QA_JS_NUMBER(index), json(value, a, sizeof a));
}
static void records_collision(void *ctx, qa_owned_actor actor, qa_actor_collision collision) {
  SENTINEL(ctx, RECORDS_SENTINEL); qa_app_sim_shared_physics_set_collision(physics, actor, collision);
}
static int pak_compare(const void *a, const void *b) { return -strcmp(*(char *const *)a, *(char *const *)b); }
static size_t add_paks(const char *dir, char **paths, size_t count) {
  DIR *handle = opendir(dir);
  if (!handle) return count;
  char *names[64]; size_t found = 0;
  for (struct dirent *entry; (entry = readdir(handle)) != NULL;) {
    size_t length = strlen(entry->d_name);
    if (length > 4 && strcasecmp(entry->d_name + length - 4, ".pk3") == 0 && found < 64) names[found++] = strdup(entry->d_name);
  }
  closedir(handle);
  qsort(names, found, sizeof *names, pak_compare);
  for (size_t i = 0; i < found; i++) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s", dir, names[i]);
    paths[count++] = strdup(path); free(names[i]);
  }
  return count;
}

static bool compare_golden(dump *out, const char *golden) {
  size_t golden_length = 0;
  char *expected = read_golden(golden, &golden_length);
  if (!expected) { fprintf(stderr, "missing %s\n", golden); return false; }
  size_t line = 0, mismatches = 0;
  for (char *cursor = expected, *next; cursor && *cursor; cursor = next, line++) {
    next = strchr(cursor, '\n');
    if (next) *next++ = 0;
    const char *actual = line < out->count ? out->lines[line] : "<missing>";
    if (strcmp(cursor, actual) != 0 && mismatches++ < 12) fprintf(stderr, "%s line %zu\n  ts: %.300s\n  c:  %.300s\n", golden, line + 1, cursor, actual);
  }
  if (line != out->count && mismatches++ < 12) fprintf(stderr, "%s: ts %zu lines, c %zu lines\n", golden, line, out->count);
  fprintf(stderr, "%s: %zu lines compared, %zu mismatches\n", golden, line, mismatches);
  free(expected);
  return mismatches == 0;
}

static char *read_golden(const char *name, size_t *out_length) {
  char path[512];
  snprintf(path, sizeof path, "tests/app/oracles/%s", name);
  FILE *file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END); long length = ftell(file); fseek(file, 0, SEEK_SET);
  char *bytes = malloc((size_t)length + 1);
  *out_length = fread(bytes, 1, (size_t)length, file); bytes[*out_length] = 0;
  fclose(file);
  return bytes;
}

static void run_case(const char *pack, const char *map_name, const char *product, double game_type, const char *golden) {
  const char *home = getenv("HOME");
  char path[512];
  snprintf(path, sizeof path, "%s/qfiles/Quake 3 Arena/%s", home ? home : "", pack);
  if (access(path, R_OK) != 0) { fprintf(stderr, "skip %s: corpus missing\n", path); return; }
  qa_archive_handle *archive = qa_open_archive(path, false, QA_ARCHIVE_FORMAT_PK3);
  CHECK(archive != NULL);
  char entry_path[128];
  snprintf(entry_path, sizeof entry_path, "maps/%s.bsp", map_name);
  const qa_archive_entry *entry = NULL;
  CHECK(qa_archive_find_entries(archive, entry_path, QA_ARCHIVE_PATH_ASCII_INSENSITIVE, &entry, 1) == 1);
  size_t length = 0;
  uint8_t *bytes = qa_archive_read_entry_entry(archive, entry, &length);
  qa_q3_decoded_world *decoded = calloc(1, sizeof *decoded);
  qa_q3_decode_world(bytes, length, entry_path, decoded);
  free(bytes);
  qa_archive_destroy(archive);

  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "q3-source-oracle"));
  actors = qa_session_actor_registry_create(&identity, 4096);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_decoded_world world; memset(&world, 0, sizeof world);
  world.kind = QA_DECODED_WORLD_Q3; world.as.q3 = &decoded->world;
  qa_shared_scene_queries *scene = qa_create_scene_queries(world);
  qa_app_sim_shared_physics_options po; memset(&po, 0, sizeof po);
  po.ctx = &PHYSICS_SENTINEL; po.actors = actors; po.callbacks = callbacks; po.scene = scene; po.numeric = qa_q3_binary32_profile;
  po.source_order = p_order; po.world_actor = p_world; po.on_blocked = p_blocked;
  physics = qa_app_sim_shared_physics_create(po);
  qa_gameplay_hooks hooks; memset(&hooks, 0, sizeof hooks);
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, hooks);
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_shared_original_pickup_admission *pickups = qa_shared_original_pickup_admission_create(actors, combat, inventory, NULL, NULL);
  qa_original_pickup_admission pickup_contract = qa_shared_original_pickup_admission_as_contract(pickups);
  qa_q3_death_animation_sequence animations = {0};

  qa_app_q3_host_operations operations; memset(&operations, 0, sizeof operations);
  qa_app_q3_source_host *base = &operations.base;
  base->ctx = &HOST_SENTINEL;
  base->actors = actors; base->bodies = qa_app_sim_shared_physics_bodies(physics); base->callbacks = callbacks;
  base->combat = combat; base->inventory = inventory; base->scene = scene; base->original_pickups = &pickup_contract;
  base->mover_actors.ctx = &MOVER_SENTINEL; base->mover_actors.observe = mover_observe; base->mover_actors.write = mover_write;
  base->mover_actors.link = mover_link; base->mover_actors.release = mover_release;
  base->death_animations = &animations; base->now = host_now; base->schedule = host_schedule; base->run_think = host_run_think;
  base->collision = host_collision; base->armor_context = host_armor; base->foreign = host_foreign; base->is_player = host_is_player;
  base->source_command = host_source_command; base->move_client = host_move; base->select_spawn_weapon = host_select;
  base->spawn_player = host_spawn;
  operations.bots.kind = "unavailable"; operations.bots.reason = "oracle";
  operations.emit_ctx = &EMIT_SENTINEL; operations.emit = emit; operations.client_number = client_number;
  operations.session = qa_session_actor_registry_session(actors);
  qa_app_q3_host_settings settings; memset(&settings, 0, sizeof settings);
  settings.game_type = game_type; settings.max_clients = 8; settings.map_name = map_name;
  qa_app_q3_created_source_host *created = qa_app_create_q3_source_host(operations, settings);
  CHECK(created != NULL);
  qa_app_q3_source_host *host = qa_app_q3_created_source_host_get(created);
  CHECK(host->entity_event_ctx == created && host->source_command_ctx == created && host->ctx == &HOST_SENTINEL);
  size_t definition_count = 0;
  qa_q3_base_product product_kind = strcmp(product, "missionpack") == 0 ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
  const qa_q3_base_cvar_definition *definitions = qa_q3_base_game_cvar_definitions(product_kind, &definition_count);
  for (size_t i = 0; i < definition_count; i++) {
    qa_cvar_snapshot result;
    if (qa_cvar_registry_register(host->cvars, definitions[i].name, definitions[i].value, definitions[i].flags, &result)) qa_cvar_read_free(&result);
  }
  qa_app_q3_source_options options; memset(&options, 0, sizeof options);
  const char *content = "q3:classic:baseq3:retail";
  options.recipe.map.entities = (qa_provider_reference){"q3:game", content};
  options.recipe.combat = (qa_provider_reference){"q3:combat", content};
  options.recipe.inventory = (qa_provider_reference){"q3:inventory", content};
  options.recipe.movement = (qa_provider_reference){"q3:movement", content};
  options.weapon_provider = (qa_provider_reference){"q3:weapons", content};
  options.product = product; options.entities = decoded->world.entities;
  options.seed = 42; options.max_clients = 8; options.build_date = "TypeScript port";
  qa_app_q3_source_runtime_mode mode = {false, NULL};
  qa_app_q3_source_runtime *runtime = qa_app_q3_source_runtime_new(options, *host, mode);
  CHECK(runtime != NULL);
  const qa_app_q3_spawn_report *report = qa_app_q3_source_runtime_load(runtime);

  dump out = events; memset(&events, 0, sizeof events);
  char a[8192], b[8192];
  for (size_t i = 0; i < report->outcome_count; i++) {
    const qa_app_q3_spawn_outcome *o = &report->outcomes[i];
    push(&out, "outcome %zu %s %s %s", i, o->kind, QA_JS_NUMBER(o->slot), strcmp(o->kind, "filtered") == 0 ? o->reason : json(o->classname, a, sizeof a));
  }
  for (size_t i = 0; i < report->world_variable_count; i++)
    push(&out, "world %s %s", json(report->world_variables[i].key, a, sizeof a), json(report->world_variables[i].value, b, sizeof b));
  qa_q3_entity_pool *pool = qa_app_q3_source_runtime_pool(runtime);
  for (double slot = 0; slot < pool->num_entities; slot++) {
    qa_q3_game_entity *entity = qa_q3_entity_pool_at(pool, slot);
    if (!qa_q3_game_entity_inuse(entity)) continue;
    qa_vec3 o = qa_q3_base_entity_shared_current_origin(&entity->r);
    char x[40], y[40], z[40], e[40], m[40], c[40], s[40];
    snprintf(x, sizeof x, "%s", QA_JS_NUMBER(o.x)); snprintf(y, sizeof y, "%s", QA_JS_NUMBER(o.y)); snprintf(z, sizeof z, "%s", QA_JS_NUMBER(o.z));
    snprintf(e, sizeof e, "%s", QA_JS_NUMBER(entity->s.e_type)); snprintf(m, sizeof m, "%s", QA_JS_NUMBER(entity->s.modelindex));
    snprintf(c, sizeof c, "%s", QA_JS_NUMBER(entity->r.contents)); snprintf(s, sizeof s, "%s", QA_JS_NUMBER(entity->s.solid));
    push(&out, "entity %s %s %s %s %s %s %s %s %s", QA_JS_NUMBER(slot), json(qa_q3_game_entity_classname(entity), a, sizeof a), x, y, z, e, m, c, s);
  }
  static const char *const CVARS[] = {"g_gametype", "sv_maxclients", "timelimit", "fraglimit", "g_motd", "dmflags", "g_speed", "g_gravity",
      "session", "g_restarted", "mapname"};
  for (size_t i = 0; i < sizeof CVARS / sizeof *CVARS; i++) {
    char *value = qa_cvar_registry_variable_string(host->cvars, CVARS[i]);
    push(&out, "cvar %s %s", CVARS[i], json(value ? value : "", a, sizeof a));
    free(value);
  }
  push(&out, "numEntities %s", QA_JS_NUMBER(pool->num_entities));

  size_t golden_length = 0;
  char *expected = read_golden(golden, &golden_length);
  CHECK(expected != NULL);
  size_t line = 0, mismatches = 0;
  for (char *cursor = expected, *next; expected && cursor && *cursor; cursor = next, line++) {
    next = strchr(cursor, '\n');
    if (next) *next++ = 0;
    const char *actual = line < out.count ? out.lines[line] : "<missing>";
    if (strcmp(cursor, actual) != 0 && mismatches++ < 12) fprintf(stderr, "%s line %zu\n  ts: %s\n  c:  %s\n", golden, line + 1, cursor, actual);
  }
  if (line != out.count && mismatches++ < 12) fprintf(stderr, "%s: ts %zu lines, c %zu lines\n", golden, line, out.count);
  CHECK(mismatches == 0);
  fprintf(stderr, "%s: %zu lines compared, %zu mismatches\n", golden, line, mismatches);
  free(expected);
  for (size_t i = 0; i < out.count; i++) free(out.lines[i]);
  free(out.lines);

  qa_app_q3_source_runtime_destroy(runtime);
  qa_app_q3_created_source_host_destroy(created);
  qa_shared_original_pickup_admission_destroy(pickups);
  qa_shared_inventory_table_destroy(inventory);
  qa_gameplay_authority_destroy(combat);
  qa_app_sim_shared_physics_destroy(physics);
  qa_shared_scene_queries_destroy(scene);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
  qa_q3_decoded_world_destroy(decoded);
  free(decoded);
}

static void run_guest_case(const char *base, const char *extra, const char *map_name, const char *provider, const char *golden) {
  const char *home = getenv("HOME");
  char base_dir[512], extra_dir[512];
  snprintf(base_dir, sizeof base_dir, "%s/qfiles/Quake 3 Arena/%s", home ? home : "", base);
  if (extra) snprintf(extra_dir, sizeof extra_dir, "%s/qfiles/Quake 3 Arena/%s", home ? home : "", extra);
  if (access(base_dir, R_OK) != 0) { fprintf(stderr, "skip %s: corpus missing\n", base_dir); return; }
  char *paths[128]; size_t path_count = 0;
  if (extra) path_count = add_paks(extra_dir, paths, path_count);
  path_count = add_paks(base_dir, paths, path_count);
  const char *content = "q3:classic:oracle:installed";
  qa_content_mount *mounts = calloc(path_count, sizeof *mounts);
  qa_mount_id *order = calloc(path_count, sizeof *order);
  char **ids = calloc(path_count, sizeof *ids), **digests = calloc(path_count, sizeof *digests);
  for (size_t i = 0; i < path_count; i++) {
    char id[64]; snprintf(id, sizeof id, "mount:oracle:%zu", i);
    ids[i] = strdup(id); digests[i] = qa_digest_file(paths[i]);
    mounts[i].kind = QA_CONTENT_MOUNT_ARCHIVE;
    mounts[i].as.archive.identity = qa_create_mount_identity(ids[i], content, 1);
    mounts[i].as.archive.format = QA_ARCHIVE_FORMAT_PK3;
    mounts[i].as.archive.archive_path = paths[i];
    mounts[i].as.archive.archive_digest = digests[i];
    order[i] = ids[i];
  }
  qa_resolved_mount_plan plan; memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:oracle:1"; plan.mounts = mounts; plan.mount_count = path_count;
  plan.default_order = order; plan.default_order_count = path_count;
  qa_content_mounted *mounted = qa_open_mount_plan(&plan, NULL);
  CHECK(mounted != NULL);
  char map_path[128]; snprintf(map_path, sizeof map_path, "maps/%s.bsp", map_name);
  qa_content_opened_resource map, qvm;
  CHECK(qa_content_mounted_open(mounted, map_path, NULL, NULL, &map));
  CHECK(qa_content_mounted_open(mounted, "vm/qagame.qvm", NULL, NULL, &qvm));
  free(qvm.bytes);
  qa_resolved_resource_reference artifact_reference = *qvm.reference;
  char *owned[4] = {strdup(qvm.reference->id), strdup(qvm.reference->requested_path), strdup(qvm.reference->digest),
      strdup(qvm.reference->provenance.member_path ? qvm.reference->provenance.member_path : "")};
  artifact_reference.id = owned[0]; artifact_reference.requested_path = owned[1]; artifact_reference.digest = owned[2];
  artifact_reference.provenance.member_path = owned[3];
  qa_execution_module execution; memset(&execution, 0, sizeof execution);
  execution.kind = QA_EXECUTION_MODULE_QVM; execution.owner = (qa_provider_reference){provider, content};
  execution.role = "server-game"; execution.api.kind = QA_GAME_API_Q3;
  execution.api.as.q3.kind = QA_Q3_API_QAGAME; execution.api.as.q3.version = 8;
  execution.artifact = &artifact_reference;
  qa_app_prepared_q3_game *prepared = calloc(1, sizeof *prepared);
  qa_app_prepare_q3_game(execution, mounted, prepared);
  memset(&guest_out, 0, sizeof guest_out);
  const qa_resolved_qvm_artifact *a = &prepared->artifact;
  push(&guest_out, "artifact %s %s %s %s %s", a->module.id, a->module.digest, a->module.revision, a->has_abi_profile ? a->abi_profile : "none",
      a->known ? a->known->product : "unknown");
  char weapons[4096]; size_t used = 0; weapons[0] = 0;
  for (size_t i = 0; i < prepared->weapons.count; i++) {
    const qa_q3_guest_weapon *w = &prepared->weapons.values[i];
    used += (size_t)snprintf(weapons + used, sizeof weapons - used, "%s%s:%s:%s", i ? "," : "", QA_JS_NUMBER(w->weapon), w->item, w->ammo ? w->ammo : "-");
  }
  push(&guest_out, "weapons %s", weapons);
  push(&guest_out, "items %s", prepared->has_items ? "declared" : "none");
  const qa_qvm_primary_profile *primary = &prepared->primary;
  char input[96];
  if (primary->input) { char e[40]; snprintf(e, sizeof e, "%s", QA_JS_NUMBER(primary->input->entity_stride)); snprintf(input, sizeof input, "%s/%s", e, QA_JS_NUMBER(primary->input->client_stride)); }
  else snprintf(input, sizeof input, "null");
  push(&guest_out, "primary declaration=%s input=%s weapons=%s inventory=%s pickups=%s combat=%s",
      qa_app_prepared_q3_primary_declaration_digest(prepared) ? qa_app_prepared_q3_primary_declaration_digest(prepared) : "null", input,
      primary->weapons ? "set" : "null", primary->inventory ? "set" : "null", primary->pickups ? "set" : "null", primary->combat ? "set" : "null");

  qa_q3_decoded_world *decoded = calloc(1, sizeof *decoded);
  qa_q3_decode_world(map.bytes, map.byte_length, map_path, decoded);
  free(map.bytes);
  qa_identity_owner identity;
  CHECK(qa_identity_owner_create(&identity, "guest-oracle"));
  actors = qa_session_actor_registry_create(&identity, 4096);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_decoded_world world; memset(&world, 0, sizeof world);
  world.kind = QA_DECODED_WORLD_Q3; world.as.q3 = &decoded->world;
  qa_shared_scene_queries *scene = qa_create_scene_queries(world);
  qa_app_sim_shared_physics_options po; memset(&po, 0, sizeof po);
  po.ctx = &PHYSICS_SENTINEL; po.actors = actors; po.callbacks = callbacks; po.scene = scene; po.numeric = qa_q3_binary32_profile;
  po.source_order = p_order; po.world_actor = p_world; po.on_blocked = p_blocked;
  physics = qa_app_sim_shared_physics_create(po);
  qa_app_q3_server_state_options state_options; memset(&state_options, 0, sizeof state_options);
  state_options.session = qa_session_actor_registry_session(actors);
  state_options.settings.game_type = 0; state_options.settings.max_clients = 8; state_options.settings.map_name = map_name;
  state_options.ctx = &STATE_SENTINEL; state_options.now = guest_state_now; state_options.print = guest_state_print;
  qa_app_q3_server_state *state = qa_app_q3_server_state_create(state_options);
  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(state);
  qa_cvar_snapshot result;
  if (qa_cvar_registry_set(cvars, "bot_enable", "0", true, &result)) qa_cvar_read_free(&result);
  if (qa_cvar_registry_set(cvars, "fs_game", strcmp(provider, "q3:missionpack") == 0 ? "missionpack" : "baseq3", true, &result)) qa_cvar_read_free(&result);
  char root[] = "/tmp/q3-guest-test-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  qa_user_file_store writable;
  CHECK(qa_user_file_store_init(&writable, root));
  qa_qvm_common_services common; memset(&common, 0, sizeof common);
  common.ctx = &COMMON_SENTINEL; common.milliseconds = common_ms; common.real_time = common_real;
  common.commands.ctx = &COMMON_SENTINEL; common.commands.execute_now = common_execute;
  common.commands.append = common_append; common.commands.insert = common_insert;
  qa_app_q3_qvm_server_game_options options; memset(&options, 0, sizeof options);
  options.module.kind = "qvm"; options.module.artifact = &prepared->artifact; options.state = state; options.mounts = mounted; options.writable = &writable;
  options.records.actors = actors; options.records.bodies = qa_app_sim_shared_physics_bodies(physics); options.records.scene = scene;
  options.records.provider = provider; options.records.ctx = &RECORDS_SENTINEL; options.records.collision = records_collision;
  options.max_clients = 8; options.seed = 42; options.entity_text = decoded->world.entities; options.common = &common;
  options.ctx = &GUEST_SENTINEL; options.now = guest_now; options.assert_current = guest_assert_current;
  qa_app_q3_qvm_server_game *game = qa_app_q3_qvm_server_game_create(options);
  CHECK(game != NULL);
  CHECK(qa_app_q3_qvm_server_game_game(game) != NULL);
  qa_app_q3_guest_output output = {&OUTPUT_SENTINEL, output_drop, output_server, output_configstring};
  qa_app_q3_qvm_server_game_initialize(game, output, "new", NULL, 0);
  push(&guest_out, "numEntities %s", QA_JS_NUMBER(qa_qvm_game_data_get_num_entities(&qa_app_q3_qvm_server_game_game(game)->data)));
  static const char *const CVARS[] = {"g_gametype", "sv_maxclients", "dedicated", "bot_enable", "vm_game", "timelimit", "fraglimit", "g_motd", "mapname"};
  for (size_t i = 0; i < sizeof CVARS / sizeof *CVARS; i++) {
    char *value = qa_cvar_registry_variable_string(cvars, CVARS[i]); char a2[8192];
    push(&guest_out, "cvar %s %s", CVARS[i], json(value ? value : "", a2, sizeof a2));
    free(value);
  }
  qa_app_q3_qvm_server_game_shutdown(game);
  CHECK(compare_golden(&guest_out, golden));
  for (size_t i = 0; i < guest_out.count; i++) free(guest_out.lines[i]);
  free(guest_out.lines); memset(&guest_out, 0, sizeof guest_out);
  qa_app_q3_qvm_server_game_destroy(game);
  qa_user_file_store_destroy(&writable);
  char command[600]; snprintf(command, sizeof command, "rm -rf '%s'", root); if (system(command) != 0) failures++;
  qa_app_q3_server_state_destroy(state);
  qa_app_sim_shared_physics_destroy(physics);
  qa_shared_scene_queries_destroy(scene);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identity);
  qa_q3_decoded_world_destroy(decoded); free(decoded);
  qa_app_prepared_q3_game_destroy(prepared); free(prepared);
  for (size_t i = 0; i < 4; i++) free(owned[i]);
  qa_content_mounted_destroy(mounted);
  for (size_t i = 0; i < path_count; i++) { free(paths[i]); free(ids[i]); free(digests[i]); }
  free(mounts); free(order); free(ids); free(digests);
}

int main(void) {
  run_case("baseq3/pak0.pk3", "q3dm1", "baseq3", 0, "q3_source_q3dm1.txt");
  run_case("missionpack/pak0.pk3", "mpteam1", "missionpack", 0, "q3_source_mpteam1.txt");
  run_guest_case("baseq3", NULL, "q3dm1", "q3:game", "q3_guest_q3dm1.txt");
  run_guest_case("baseq3", "missionpack", "mpteam1", "q3:missionpack", "q3_guest_mpteam1.txt");
  if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
  puts("test_q3_source: ok");
  return 0;
}
