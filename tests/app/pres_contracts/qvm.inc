/* pres_contracts part: qvm. Included by tests/app/test_pres_contracts.c.
 * ModQvmPresentationSource.context returns the compat QvmPresentationContext
 * through both producers (the QVM mod provider and the unified component
 * consumer); client collision takes SharedQvmClientClipModels as
 * QvmClientClipModels; the Q3 client assets host is built from a real
 * ApplicationAssets, with expected values from ApplicationQ3Assets.create
 * run under bun on the same loose files
 * (build/agents/pres-contracts-qvm/tmp/assets_host.ts). */
#include <sys/stat.h>
#include <unistd.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/network/unified_component_consumer.h"
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/q3_client/guest_collision.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/client_collision_syscalls.h"
#include "qa/compat/qvm/mod_presentation.h"
#include "qa/compat/qvm/mod_provider.h"
#include "qa/compat/qvm/trace_record.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mod_presentations.h"

/* One C type for TS Omit<QvmPresentationContext, "frameTimeMilliseconds" |
 * "viewOrigin"> | null, the member of ModQvmPresentationSource. */
typedef const qa_qvm_presentation_context *(*pres_qvm_context_fn)(void *ctx, qa_actor_id viewer);
_Static_assert(__builtin_types_compatible_p(__typeof__(((qa_mod_qvm_presentation_source *)0)->context),
                                            pres_qvm_context_fn),
               "presentation source context is QvmPresentationContext");
_Static_assert(__builtin_types_compatible_p(__typeof__(((qa_qvm_mod_provider *)0)->presentation_context),
                                            qa_qvm_presentation_context *),
               "the provider context storage is QvmPresentationContext");

/* ---------- producer: UnifiedComponentConsumers ---------- */
static qa_actor_id pres_qvm_viewer;
static int pres_qvm_admitted, pres_qvm_borrowed, pres_qvm_closed;
static char pres_qvm_installed;
static void pres_qvm_assert(void *ctx) { CHECK_SENTINEL(ctx); }
static bool pres_qvm_viewer_of(void *ctx, qa_actor_id *out) {
  CHECK_SENTINEL(ctx);
  *out = pres_qvm_viewer;
  return true;
}
static void pres_qvm_command(void *ctx, qa_presentation_owner owner, double generation, const char *const *args,
                             size_t count) {
  CHECK_SENTINEL(ctx);
  (void)owner; (void)generation; (void)args; (void)count;
}
static void pres_qvm_admit(void *ctx, qa_presentation_owner owner, qa_content_id content) {
  CHECK_SENTINEL(ctx);
  (void)owner; (void)content;
  pres_qvm_admitted++;
}
static void pres_qvm_retire(void *ctx, qa_presentation_owner owner) { CHECK_SENTINEL(ctx); (void)owner; }
static const void *pres_qvm_installed_mounts(void *ctx, qa_content_id content) {
  CHECK_SENTINEL(ctx);
  CHECK_STR(content, "q2:classic:baseq2:1");
  return &pres_qvm_installed;
}
static const void *pres_qvm_borrow(void *ctx, const char *selection, qa_content_id content, const void *installed,
                                   const void **out_writable) {
  CHECK_SENTINEL(ctx);
  (void)selection; (void)content;
  CHECK(installed == &pres_qvm_installed);
  *out_writable = NULL;
  pres_qvm_borrowed++;
  return malloc(8);
}
static void pres_qvm_close_mounts(void *ctx, const void *mounts) {
  CHECK_SENTINEL(ctx);
  pres_qvm_closed++;
  free((void *)mounts);
}
typedef struct pres_qvm_job {
  qa_app_unified_component_consumers *consumers;
  const qa_app_unified_component_update *update;
  const qa_app_unified_component_frames *frames;
} pres_qvm_job;
static void pres_qvm_job_body(void *ctx) {
  pres_qvm_job *job = ctx;
  if (job->update != NULL) qa_app_unified_component_consumers_update(job->consumers, job->update);
  else (void)qa_app_unified_component_consumers_accept(job->consumers, job->frames, pres_qvm_viewer);
}
static bool pres_qvm_run(qa_app_unified_component_consumers *consumers, const qa_app_unified_component_update *update,
                         const qa_app_unified_component_frames *frames) {
  pres_qvm_job job = {consumers, update, frames};
  qa_caught_error caught;
  bool ok = qa_try(pres_qvm_job_body, &job, &caught);
  if (!ok) fprintf(stderr, "qvm component raised %s\n", caught.message);
  return ok;
}

static void pres_qvm_consumer_context(void) {
  static const char identity_json[] =
      "{\"selection\":{\"product\":\"q2-test\",\"id\":\"hud\"},\"source\":{\"provider\":\"q2:hud\","
      "\"content\":\"q2:classic:baseq2:1\"},\"declarationDigest\":\"sha256:"
      "0000000000000000000000000000000000000000000000000000000000000000\",\"modules\":[],\"providers\":[]}";
  qa_identity_owner owner;
  CHECK(qa_identity_owner_create(&owner, "pres-qvm"));
  pres_qvm_viewer = qa_identity_owner_actor(&owner, 4, 0);
  qa_actor_id other = qa_identity_owner_actor(&owner, 5, 0);
  qa_save_value *identity_tree = qa_parse_json_value((const uint8_t *)identity_json, strlen(identity_json), false);
  CHECK(identity_tree != NULL);
  qa_save_reader reader;
  qa_save_reader_init(&reader, identity_tree, "identity");
  qa_qvm_presentation_hud hud = {"overlay", NULL, 0};
  qa_qvm_player_event_presentation events;
  memset(&events, 0, sizeof events);
  events.runtime = "qvm-player-events";
  events.base.gameplay.abi_profile = "q3-modern";
  events.base.hud = &hud;
  qa_prepared_mod prepared;
  memset(&prepared, 0, sizeof prepared);
  prepared.identity = qa_read_mod_identity(&reader);
  prepared.has_presentation = true;
  prepared.presentation.declaration.is_scene = false;
  prepared.presentation.declaration.as.player_events = &events;
  prepared.presentation.source.id = "mod:hud";
  qa_app_unified_component_consumer_options options;
  memset(&options, 0, sizeof options);
  options.ctx = PRES_SENTINEL;
  options.prepared_mods = &prepared;
  options.prepared_mod_count = 1;
  options.assert_current = pres_qvm_assert;
  options.viewer = pres_qvm_viewer_of;
  options.command = pres_qvm_command;
  options.admit_replicated_owner = pres_qvm_admit;
  options.retire_replicated_owner = pres_qvm_retire;
  options.installed_mounts = pres_qvm_installed_mounts;
  options.borrow_mounts = pres_qvm_borrow;
  options.close_mounts = pres_qvm_close_mounts;
  qa_app_unified_component_consumers *consumers = qa_app_unified_component_consumers_create(options);

  qa_app_unified_component_state state;
  memset(&state, 0, sizeof state);
  state.identity.owner = (qa_presentation_owner){"mod:hud", 2};
  state.identity.identity = prepared.identity;
  state.identity.generation = 1;
  state.identity.abi = "q3-modern";
  state.identity.runtime = "qvm-player-events";
  state.game_state_revision = 1;
  state.has_game_state = true;
  state.game_state.string_offsets = calloc(1024, sizeof *state.game_state.string_offsets);
  state.game_state.string_data = calloc(16000, 1);
  state.game_state.string_data[1] = 7;
  state.game_state.data_count = 2;
  qa_app_unified_component_update update = {1, &state, 1, NULL, 0};
  CHECK(pres_qvm_run(consumers, &update, NULL) && pres_qvm_admitted == 1);

  qa_app_unified_component_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.owner = state.identity.owner;
  frame.generation = 1;
  frame.abi = "q3-modern";
  frame.viewer = pres_qvm_viewer;
  frame.client_number = 3;
  frame.game_state_revision = 1;
  frame.weapon_presented = true;
  frame.snapshot.server_time = 1250;
  frame.snapshot.player_state.client_number = 3;
  frame.snapshot.player_state.weapon = 5;
  for (size_t i = 0; i < 64; i++) frame.snapshot.player_slots[i] = (double)i + 0.5;
  qa_qvm_scene_actor binding = {pres_qvm_viewer, 3, false};
  frame.bindings = &binding;
  frame.binding_count = 1;
  qa_app_unified_component_frames frames = {1, &frame, 1, false, NULL, 0};
  CHECK(pres_qvm_run(consumers, NULL, &frames));
  size_t count = 0;
  const struct qa_active_mod_presentation *sources = qa_app_unified_component_consumers_sources(consumers, &count);
  CHECK(count == 1);
  if (count == 1) {
    pres_qvm_context_fn context_of = sources[0].source.context;
    const qa_qvm_presentation_context *context = context_of(sources[0].source.ctx, pres_qvm_viewer);
    CHECK(context != NULL);
    if (context != NULL) {
      CHECK(context->has_client_number && context->client_number == 3);
      CHECK(context->game_state_revision == 1 && context->game_state.string_data[1] == 7);
      CHECK(context->has_weapon_presented && context->weapon_presented);
      CHECK(context->snapshot_server_time == 1250 && context->snapshot_player_state.weapon == 5);
      /* The player-state slot views point into the context's own storage. */
      CHECK(context->snapshot_player_state.stats.values == context->player_slots &&
            context->snapshot_player_state.ammo.values == context->player_slots + 48);
      CHECK(context->snapshot_player_state.persistent.count == 16 &&
            context->snapshot_player_state.persistent.values[2] == 18.5);
      /* The omitted members are left for the consumer. */
      CHECK(context->frame_time_milliseconds == 0 && context->view_origin.x == 0 && !context->has_time);
      CHECK(context->scene == NULL);
    }
    /* TS null: another viewer has no original source context. */
    CHECK(context_of(sources[0].source.ctx, other) == NULL);
  }
  qa_app_unified_component_consumers_close(consumers);
  qa_app_unified_component_consumers_destroy(consumers);
  CHECK(pres_qvm_borrowed == pres_qvm_closed);
  free(state.game_state.string_offsets);
  free(state.game_state.string_data);
  qa_save_reader_destroy(&reader);
  free((void *)prepared.identity.modules);
  free((void *)prepared.identity.providers);
  qa_save_value_free(identity_tree);
  qa_identity_owner_destroy(&owner);
}

/* ---------- producer: QvmModProvider ---------- */
static bool pres_qvm_runtime_active(void *ctx) { CHECK_SENTINEL(ctx); return false; }
static int64_t pres_qvm_runtime_stack(void *ctx) { CHECK_SENTINEL(ctx); return 16000; }
static size_t pres_qvm_runtime_bind(void *ctx, int64_t index, qa_qvm_function_hook_fn hook, void *hook_ctx) {
  CHECK_SENTINEL(ctx);
  (void)index; (void)hook; (void)hook_ctx;
  return 1;
}
static size_t pres_qvm_runtime_resolver(void *ctx, qa_qvm_function_resolver_fn resolve, void *resolve_ctx) {
  CHECK_SENTINEL(ctx);
  (void)resolve; (void)resolve_ctx;
  return 1;
}
static size_t pres_qvm_runtime_observe(void *ctx, int64_t index, qa_qvm_function_observer_fn observe,
                                       void *observe_ctx) {
  CHECK_SENTINEL(ctx);
  (void)index; (void)observe; (void)observe_ctx;
  return 1;
}
static void pres_qvm_runtime_unbind(void *ctx, size_t id) { CHECK_SENTINEL(ctx); (void)id; }
static void pres_qvm_engine_print(void *ctx, const char *text) { CHECK_SENTINEL(ctx); (void)text; }
static bool pres_qvm_engine_world(void *ctx, qa_actor_id *out) { CHECK_SENTINEL(ctx); (void)out; return false; }
static qa_source_time pres_qvm_world_time(void *ctx) {
  CHECK_SENTINEL(ctx);
  return (qa_source_time){QA_SOURCE_TIME_SECONDS, 2};
}
static qa_bounds pres_qvm_abs_bounds(void *ctx, qa_owned_actor actor, qa_body_state state) {
  (void)ctx;
  return qa_translated_body_bounds(actor, state);
}
static void pres_qvm_link(void *ctx, qa_linked_body body) { (void)ctx; (void)body; }
static void pres_qvm_unlink(void *ctx, qa_actor_id actor) { (void)ctx; (void)actor; }

static void pres_qvm_provider_context(void) {
  static uint8_t guest_bytes[16384];
  static qa_qvm_instruction instructions[] = {
      {.opcode = QA_QVM_OP_ENTER, .operand_width = 4, .operand = 0},
      {.opcode = QA_QVM_OP_LEAVE, .operand_width = 4, .operand = 0}};
  static qa_qvm_mod_actor_field fields[] = {{.offset = 0, .binding = "health", .encoding = "int32"}};
  static qa_qvm_mod_actor_record records[] = {{"ents", 256, 64, 4, fields, 1}};
  qa_qvm_memory guest;
  memset(guest_bytes, 0, sizeof guest_bytes);
  qa_qvm_memory_init(&guest, guest_bytes, sizeof guest_bytes, NULL, NULL);
  qa_identity_owner identities;
  CHECK(qa_identity_owner_create(&identities, "pres-qvm-provider"));
  qa_session_actor_registry *actors = qa_session_actor_registry_create(&identities, 64);
  qa_actor_callback_table *callbacks = qa_actor_callback_table_create(actors);
  qa_shared_body_table *bodies =
      qa_shared_body_table_create(actors, (qa_body_link_hooks){NULL, pres_qvm_abs_bounds, pres_qvm_link, pres_qvm_unlink});
  qa_shared_inventory_table *inventory = qa_shared_inventory_table_create(actors);
  qa_gameplay_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  qa_gameplay_authority *combat = qa_gameplay_authority_create(actors, callbacks, hooks);
  qa_session_mod_engine_scene scene;
  memset(&scene, 0, sizeof scene);
  qa_session_mod_engine_services engine;
  memset(&engine, 0, sizeof engine);
  engine.ctx = PRES_SENTINEL;
  engine.print = pres_qvm_engine_print;
  engine.scene = &scene;
  engine.world = pres_qvm_engine_world;
  engine.presentation_map = "maps/q3dm1.bsp";
  qa_mod_host_services services;
  memset(&services, 0, sizeof services);
  services.actors = actors;
  services.callbacks = callbacks;
  services.bodies = bodies;
  services.combat = combat;
  services.inventory = inventory;
  services.seed = 3;
  services.ctx = PRES_SENTINEL;
  services.time = pres_qvm_world_time;
  services.engine = &engine;
  qa_resolved_qvm_artifact artifact;
  memset(&artifact, 0, sizeof artifact);
  artifact.module.id = "q3:test:game";
  artifact.module.artifact_path = "vm/qagame.qvm";
  artifact.module.digest = "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  artifact.module.revision = "1";
  artifact.role = "qagame";
  artifact.has_abi_profile = true;
  artifact.abi_profile = "q3-modern";
  artifact.image.instructions = instructions;
  artifact.image.instruction_count = 2;
  artifact.image.data_length = 4096;
  artifact.image.bss_length = 4096;
  artifact.image.allocated_data_length = 8192;
  qa_qvm_mod_callback_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  declaration.version = 1;
  declaration.runtime = "qvm";
  declaration.program.path = "vm/qagame.qvm";
  declaration.program.digest = artifact.module.digest;
  declaration.abi_profile = "q3-modern";
  declaration.actor_records = records;
  declaration.actor_record_count = 1;
  qa_qvm_interpreter_runtime runtime;
  memset(&runtime, 0, sizeof runtime);
  runtime.ctx = PRES_SENTINEL;
  runtime.address_space = &guest;
  runtime.is_active = pres_qvm_runtime_active;
  runtime.stack_pointer = pres_qvm_runtime_stack;
  runtime.bind_function = pres_qvm_runtime_bind;
  runtime.bind_invocation = pres_qvm_runtime_bind;
  runtime.bind_function_resolver = pres_qvm_runtime_resolver;
  runtime.observe_function = pres_qvm_runtime_observe;
  runtime.unbind = pres_qvm_runtime_unbind;
  qa_qvm_mod_provider provider;
  CHECK(qa_qvm_mod_provider_init(&provider, &artifact, &declaration, &services, pres_qvm_assert, PRES_SENTINEL,
                                 "q3:test:content", NULL, NULL, &runtime, NULL));
  pres_qvm_context_fn context_of = provider.presentation_source.context;
  CHECK(provider.presentation_source.ctx == &provider);
  /* A viewer without an admitted source client slot has no context (TS null). */
  qa_actor_id stranger = qa_identity_owner_actor(&identities, 9, 0);
  CHECK(context_of(provider.presentation_source.ctx, stranger) == NULL);
  qa_qvm_mod_provider_close(&provider);
  qa_qvm_mod_provider_destroy(&provider);
  qa_gameplay_authority_destroy(combat);
  qa_shared_inventory_table_destroy(inventory);
  qa_shared_body_table_destroy(bodies);
  qa_actor_callback_table_destroy(callbacks);
  qa_session_actor_registry_destroy(actors);
  qa_identity_owner_destroy(&identities);
}

/* ---------- client collision over SharedQvmClientClipModels ---------- */
static uint8_t pres_qvm_guest_bytes[4096];
static uint8_t pres_qvm_words[64];
static qa_qvm_memory pres_qvm_guest;
static qa_qvm_client_clip_models pres_qvm_models_value;
static int pres_qvm_models_calls;
static bool pres_qvm_cvar_get(void *ctx, const char *name, qa_cvar_snapshot *out) {
  CHECK_SENTINEL(ctx);
  (void)name; (void)out;
  return false;
}
static qa_qvm_client_clip_models pres_qvm_models(void *ctx) {
  CHECK_SENTINEL(ctx);
  pres_qvm_models_calls++;
  return pres_qvm_models_value;
}
static void pres_qvm_load_map(void *ctx, const char *name) { CHECK_SENTINEL(ctx); (void)name; }
static qa_qvm_host_call pres_qvm_call(int32_t code, const int32_t *args, size_t count) {
  memset(pres_qvm_words, 0, sizeof pres_qvm_words);
  memcpy(pres_qvm_words, &code, 4);
  for (size_t i = 0; i < count; i++) memcpy(pres_qvm_words + (i + 1) * 4, &args[i], 4);
  qa_qvm_host_call call;
  memset(&call, 0, sizeof call);
  call.kind = "engine";
  call.role = "cgame";
  call.code = code;
  call.guest = &pres_qvm_guest;
  call.base.words = pres_qvm_words;
  call.base.words_length = sizeof pres_qvm_words;
  call.base.guest = &pres_qvm_guest;
  call.base.memory = pres_qvm_guest_bytes;
  call.base.memory_length = sizeof pres_qvm_guest_bytes;
  return call;
}
static float pres_qvm_f32(size_t offset) {
  float value;
  memcpy(&value, pres_qvm_guest_bytes + offset, 4);
  return value;
}
static int32_t pres_qvm_i32(size_t offset) {
  int32_t value;
  memcpy(&value, pres_qvm_guest_bytes + offset, 4);
  return value;
}

static void pres_qvm_collision(void) {
  memset(pres_qvm_guest_bytes, 0, sizeof pres_qvm_guest_bytes);
  qa_qvm_memory_init(&pres_qvm_guest, pres_qvm_guest_bytes, sizeof pres_qvm_guest_bytes, NULL, NULL);
  /* A Q3 world with two inline models and no BSP nodes. */
  qa_q3_world_geometry geometry;
  memset(&geometry, 0, sizeof geometry);
  geometry.model_count = 2;
  qa_shared_scene_queries scene;
  memset(&scene, 0, sizeof scene);
  scene.geometry.kind = QA_DECODED_WORLD_Q3;
  scene.geometry.as.q3 = &geometry;
  scene.kind = "q3";
  qa_app_qvm_cvar_services cvars;
  memset(&cvars, 0, sizeof cvars);
  cvars.ctx = PRES_SENTINEL;
  cvars.get = pres_qvm_cvar_get;
  qa_app_shared_qvm_client_clip_models *shared = qa_app_shared_qvm_client_clip_models_create(&scene, cvars);
  pres_qvm_models_value = qa_app_shared_qvm_client_clip_models_as_client(shared);
  CHECK(pres_qvm_models_value.ctx == shared);
  qa_qvm_client_collision_services services = {PRES_SENTINEL, pres_qvm_models, pres_qvm_load_map};
  qa_qvm_host_result out = -1;
  qa_qvm_host_call call = pres_qvm_call(QA_QVM_CGAME_IMPORT_CG_CM_NUMINLINEMODELS, NULL, 0);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == 2);
  const int32_t inline_args[] = {1};
  call = pres_qvm_call(QA_QVM_CGAME_IMPORT_CG_CM_INLINEMODEL, inline_args, 1);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == 1);
  /* tempBoxModel stores the bounds; the capsule variant returns its handle. */
  memcpy(pres_qvm_guest_bytes + 100, (float[]){-1, -2, -3}, 12);
  memcpy(pres_qvm_guest_bytes + 112, (float[]){1, 2, 3}, 12);
  const int32_t box_args[] = {100, 112};
  call = pres_qvm_call(QA_QVM_CGAME_IMPORT_CG_CM_TEMPBOXMODEL, box_args, 2);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == QA_SOURCE_BOX_MODEL_HANDLE);
  call = pres_qvm_call(QA_QVM_CGAME_IMPORT_CG_CM_TEMPCAPSULEMODEL, box_args, 2);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == QA_SOURCE_CAPSULE_MODEL_HANDLE);
  /* Without nodes: no contents, and the unloaded trace is the empty source trace. */
  const int32_t contents_args[] = {100, 0};
  call = pres_qvm_call(QA_QVM_CGAME_IMPORT_CG_CM_POINTCONTENTS, contents_args, 2);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == 0);
  memset(pres_qvm_guest_bytes + 1000, 0x7f, QA_QVM_TRACE_BYTES);
  const int32_t trace_args[] = {1000, 100, 112, 0, 0, 0, 1};
  call = pres_qvm_call(QA_QVM_CGAME_IMPORT_CG_CM_BOXTRACE, trace_args, 7);
  CHECK(qa_qvm_client_collision_syscall(&call, services, &out) && out == 0);
  qa_source_trace_result empty = qa_empty_source_trace();
  CHECK(pres_qvm_f32(1000 + 8) == (float)empty.fraction && pres_qvm_i32(1000 + 52) == 0);
  CHECK(pres_qvm_models_calls == 6);
  qa_app_shared_qvm_client_clip_models_destroy(shared);
}

/* ---------- Q3 client assets host from ApplicationAssets ---------- */
typedef struct pres_qvm_content {
  qa_content_mounted *mounts;
  qa_executable_recipe recipe;
  qa_content_catalog *catalog;
} pres_qvm_content;
static pres_qvm_content pres_qvm_loaded;
static const char *pres_qvm_open_loaded(void *ctx, qa_app_loaded_content *content) {
  CHECK_SENTINEL(ctx);
  qa_app_loaded_content_set_catalog(content, pres_qvm_loaded.catalog);
  qa_app_loaded_content_set_mounts(content, pres_qvm_loaded.mounts);
  qa_app_loaded_content_set_recipe(content, pres_qvm_loaded.recipe);
  return NULL;
}
static const char *pres_qvm_for_content(void *ctx, qa_app_loaded_content *content, qa_content_id id, const void **out) {
  CHECK_SENTINEL(ctx);
  (void)content;
  CHECK_STR(id, "q3-baseq3");
  *out = pres_qvm_loaded.mounts;
  return NULL;
}
static double pres_qvm_clock(void *ctx) { CHECK_SENTINEL(ctx); return 0; }
static qa_finish_shader_profile pres_qvm_profile(void *ctx) {
  CHECK_SENTINEL(ctx);
  return qa_default_shader_profile;
}
static void pres_qvm_warn(void *ctx, const char *text) { CHECK_SENTINEL(ctx); (void)text; }
static int pres_qvm_prints;
static void pres_qvm_print(void *ctx, const char *text) { CHECK_SENTINEL(ctx); (void)text; pres_qvm_prints++; }
static bool pres_qvm_save_fonts(void *ctx) { CHECK_SENTINEL(ctx); return false; }
static void pres_qvm_write(const char *dir, const char *name, const void *bytes, size_t length) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  for (char *at = path + strlen(dir) + 1; *at != '\0'; at++)
    if (*at == '/') { *at = '\0'; mkdir(path, 0700); *at = '/'; }
  FILE *file = fopen(path, "wb");
  CHECK(file != NULL);
  if (file == NULL) return;
  fwrite(bytes, 1, length, file);
  fclose(file);
}
static size_t pres_qvm_wav(const int16_t *samples, size_t count, uint8_t *out) {
  uint32_t data = (uint32_t)count * 2, riff = 36 + data, fmt = 16, rate = 22050, bytes = 44100;
  uint16_t pcm = 1, channels = 1, align = 2, bits = 16;
  memcpy(out, "RIFF", 4); memcpy(out + 4, &riff, 4); memcpy(out + 8, "WAVEfmt ", 8);
  memcpy(out + 16, &fmt, 4); memcpy(out + 20, &pcm, 2); memcpy(out + 22, &channels, 2);
  memcpy(out + 24, &rate, 4); memcpy(out + 28, &bytes, 4); memcpy(out + 32, &align, 2); memcpy(out + 34, &bits, 2);
  memcpy(out + 36, "data", 4); memcpy(out + 40, &data, 4);
  memcpy(out + 44, samples, data);
  return 44 + data;
}
typedef struct pres_qvm_sync_read { qa_app_application_q3_assets *assets; const char *path; } pres_qvm_sync_read;
static void pres_qvm_read_sync_body(void *ctx) {
  pres_qvm_sync_read *read = ctx;
  size_t length = 0;
  (void)qa_app_application_q3_assets_read_sync(read->assets, read->path, &length);
}

static void pres_qvm_assets_host(void) {
  const char *base = getenv("TMPDIR");
  char dir[512];
  snprintf(dir, sizeof dir, "%s/pres-qvm-XXXXXX", base != NULL ? base : "/tmp");
  CHECK(mkdtemp(dir) != NULL);
  const char *menu = "menu { }\n", *readme = "hello\n";
  const char *skin = "h_head,models/players/x/head.tga\ntag_head,\n";
  pres_qvm_write(dir, "scripts/A.menu", menu, strlen(menu));
  pres_qvm_write(dir, "readme.TXT", readme, strlen(readme));
  uint8_t wav[128];
  const int16_t step[] = {0, 1000, -1000, 2000, -2000, 0, 5, -5}, intro[] = {1, 2};
  pres_qvm_write(dir, "sound/feet/Step.wav", wav, pres_qvm_wav(step, 8, wav));
  pres_qvm_write(dir, "music/intro.wav", wav, pres_qvm_wav(intro, 2, wav));
  pres_qvm_write(dir, "models/players/x/head.skin", skin, strlen(skin));

  qa_catalog_archive_entry entries[2] = {{"Maps/Q3DM1.bsp", 0, 10}, {"scripts/B.shader", 1, 20}};
  qa_catalog_archive archive = {"/nowhere/pak0.pk3", QA_ARCHIVE_FORMAT_ZIP, entries, 2};
  qa_catalog_discovered_product product;
  memset(&product, 0, sizeof product);
  product.id = "q3-baseq3";
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, "q3-baseq3") == 0) product.expectation = qa_expected_products[i];
  product.availability.kind = QA_CATALOG_INSTALLED;
  product.archives = &archive;
  product.archive_count = 1;
  qa_content_catalog catalog;
  qa_content_catalog_init(&catalog, dir, &product, 1, NULL, 0, 0, NULL, NULL, 0);
  qa_content_mount mount;
  memset(&mount, 0, sizeof mount);
  mount.kind = QA_CONTENT_MOUNT_LOOSE;
  mount.as.loose.identity.id = "mount:q3assets:loose";
  mount.as.loose.identity.content = "q3-baseq3";
  mount.as.loose.root_path = dir;
  qa_mount_id order[1] = {"mount:q3assets:loose"};
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:q3assets";
  plan.mounts = &mount;
  plan.mount_count = 1;
  plan.default_order = order;
  plan.default_order_count = 1;
  memset(&pres_qvm_loaded, 0, sizeof pres_qvm_loaded);
  pres_qvm_loaded.catalog = &catalog;
  pres_qvm_loaded.mounts = qa_open_mount_plan(&plan, NULL);
  CHECK(pres_qvm_loaded.mounts != NULL);
  pres_qvm_loaded.recipe.presentation.assets = "q3-baseq3";
  pres_qvm_loaded.recipe.character.appearance.content = "q3-baseq3";
  qa_provider_reference weapons[1] = {{"q3:weapons", "q3-baseq3"}};
  pres_qvm_loaded.recipe.weapons.values = weapons;
  pres_qvm_loaded.recipe.weapons.count = 1;
  qa_app_content_host content_host;
  memset(&content_host, 0, sizeof content_host);
  content_host.ctx = PRES_SENTINEL;
  content_host.open_loaded = pres_qvm_open_loaded;
  content_host.for_content = pres_qvm_for_content;
  qa_app_options app_options;
  memset(&app_options, 0, sizeof app_options);
  qa_app_loaded_content *content = qa_app_load_application_content(content_host, app_options, NULL, false);
  qa_identity_owner ids;
  CHECK(qa_identity_owner_create(&ids, "pres-qvm-assets"));
  int owner_symbol = 0;
  qa_renderer_resource_owner owner = {&owner_symbol, ids.session, 0};
  qa_media_clock clock = {PRES_SENTINEL, pres_qvm_clock};
  qa_app_application_assets_options assets_options;
  memset(&assets_options, 0, sizeof assets_options);
  assets_options.shader_profile = pres_qvm_profile;
  assets_options.shader_profile_ctx = PRES_SENTINEL;
  assets_options.warn = pres_qvm_warn;
  assets_options.warn_ctx = PRES_SENTINEL;
  qa_app_application_assets *assets = qa_app_application_assets_create(content, owner, &clock, &assets_options);
  CHECK(assets != NULL);

  /* The host members read the real ApplicationAssets. */
  qa_app_q3_application_assets_host host = qa_app_q3_application_assets_host_create(assets);
  CHECK(host.ctx != NULL);
  qa_app_q3_provider_scene_assets provider;
  host.provider(host.ctx, "q3-baseq3", &provider);
  qa_app_provider_scene_assets real = qa_app_application_assets_provider(assets, "q3-baseq3");
  CHECK(provider.mounts == real.mounts && provider.ctx == real.shaders);
  CHECK(provider.archive_path_count == 0 && provider.loose_root_count == 1 && strcmp(provider.loose_roots[0], dir) == 0);
  CHECK_STR(provider.source_default_picture.name, "*default");
  const char *const *names = NULL;
  size_t name_count = 0;
  CHECK(host.catalog_archive_entries(host.ctx, "/nowhere/pak0.pk3", &names, &name_count) && name_count == 2);
  CHECK(name_count == 2 && strcmp(names[0], "Maps/Q3DM1.bsp") == 0 && strcmp(names[1], "scripts/B.shader") == 0);
  CHECK(!host.catalog_archive_entries(host.ctx, "/nowhere/pak1.pk3", &names, &name_count));
  CHECK(host.user_content_root(host.ctx, "q3-baseq3") == NULL);
  CHECK(host.product_is_q3(host.ctx, "q3-baseq3"));
  CHECK_STR(host.character_content(host.ctx), "q3-baseq3");
  qa_content_id weapon = NULL;
  CHECK(host.weapon_content(host.ctx, &weapon) && strcmp(weapon, "q3-baseq3") == 0);
  qa_app_q3_application_assets_host_destroy(&host);
  CHECK(host.ctx == NULL);

  /* TS ApplicationQ3Assets.create(assets, content, print, saveFontData); expected
   * values from bun on the same files. */
  pres_qvm_prints = 0;
  qa_app_application_q3_assets *q3 = qa_app_application_q3_assets_create_from_application(
      assets, "q3-baseq3", pres_qvm_print, PRES_SENTINEL, pres_qvm_save_fonts, PRES_SENTINEL, NULL, NULL);
  size_t count = 0;
  char **list = qa_app_application_q3_assets_list(q3, "", &count);
  static const char *const expected_list[] = {"models/players/x/head.skin", "music/intro.wav", "readme.txt",
                                              "scripts/a.menu", "sound/feet/step.wav"};
  CHECK(count == 5);
  for (size_t i = 0; i < count && i < 5; i++) CHECK_STR(list[i], expected_list[i]);
  for (size_t i = 0; i < count; i++) free(list[i]);
  free(list);
  list = qa_app_application_q3_assets_list(q3, "SOUND/", &count);
  CHECK(count == 1 && strcmp(list[0], "sound/feet/step.wav") == 0);
  for (size_t i = 0; i < count; i++) free(list[i]);
  free(list);
  CHECK(qa_app_application_q3_assets_has(q3, "SCRIPTS/a.MENU"));
  CHECK(!qa_app_application_q3_assets_has(q3, "scripts/b.menu"));
  CHECK(qa_app_application_q3_assets_read_file_length(q3, "readme.TXT") == 6);
  CHECK(qa_app_application_q3_assets_read_file_length(q3, "missing.txt") == -1);
  size_t length = 0;
  CHECK(qa_app_application_q3_assets_read_sync(q3, "SCRIPTS/A.MENU", &length) != NULL && length == 9);
  pres_qvm_sync_read unretained = {q3, "models/players/x/head.skin"};
  qa_caught_error caught;
  CHECK(!qa_try(pres_qvm_read_sync_body, &unretained, &caught));
  CHECK(strcmp(caught.message, "Q3 source requested an unretained synchronous asset: models/players/x/head.skin") == 0);
  uint8_t *music = qa_app_application_q3_assets_read(q3, "music/intro.wav", &length);
  CHECK(length == 48);
  free(music);
  CHECK(qa_app_application_q3_assets_read_sync(q3, "music/intro.wav", &length) != NULL && length == 48);
  const qa_sound_asset *sound = qa_app_application_q3_assets_sound(q3, "feet/step.wav");
  CHECK(sound != NULL);
  if (sound != NULL) {
    CHECK_STR(sound->name, "sound/feet/step.wav");
    CHECK(sound->pcm.frame_count == 8 && sound->pcm.sample_rate == 22050);
  }
  CHECK(qa_app_application_q3_assets_sound(q3, "#SOUND/FEET/STEP.WAV") == sound);
  CHECK(qa_app_application_q3_assets_sound(q3, "feet/none.wav") == NULL);

  qa_q3_pres_scene_recorder_view recorder;
  memset(&recorder, 0, sizeof recorder);
  qa_q3_pres_resource_host resources = qa_app_application_q3_assets_resource_host(q3, recorder);
  CHECK_STR(resources.zero_picture.name, "*default");
  const qa_q3_pres_scene_skin *parsed = NULL;
  CHECK(resources.skin(resources.ctx, "models/players/x/head.skin", &parsed) && parsed != NULL);
  if (parsed != NULL) {
    CHECK_STR(parsed->path, "models/players/x/head.skin");
    CHECK(parsed->surface_count == 1);
    if (parsed->surface_count == 1) {
      CHECK_STR(parsed->surfaces[0].name, "h_head");
      CHECK_STR(parsed->surfaces[0].shader, "models/players/x/head.tga");
    }
  }
  const qa_q3_pres_scene_skin *missing_skin = parsed;
  CHECK(resources.skin(resources.ctx, "models/players/x/none.skin", &missing_skin) && missing_skin == NULL);
  qa_q3_pres_scene_model model;
  CHECK(resources.model(resources.ctx, "models/none.md3", &model) && model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  CHECK(resources.model(resources.ctx, "../escape.md3", &model) && model.kind == QA_Q3_PRES_SCENE_MODEL_DEFAULT);
  const qa_material_picture *picture = &resources.zero_picture;
  CHECK(resources.shader(resources.ctx, "bad//name", true, &picture) && picture == NULL);
  /* The host is built over the real provider registry: with no shader script
   * or image named textures/wall in the mounts, registerSourcePicture compiles
   * the source default and answers null (shaders.ts registerSourcePicture), so
   * the normalized request resolves to no picture. */
  const qa_material_picture *wall = &resources.zero_picture;
  CHECK(resources.shader(resources.ctx, "textures\\Wall", true, &wall) && wall == NULL);
  CHECK(pres_qvm_prints == 0);
  qa_app_application_q3_assets_destroy(q3);
  qa_app_application_assets_destroy(assets);
  qa_identity_owner_destroy(&ids);
  qa_app_loaded_content_destroy(content);
  qa_content_mounted_destroy(pres_qvm_loaded.mounts);
  qa_content_catalog_destroy(&catalog);
  char command[700];
  snprintf(command, sizeof command, "rm -rf '%s'", dir);
  CHECK(system(command) == 0);
}

static void pres_qvm_tests(void) {
  pres_qvm_consumer_context();
  pres_qvm_provider_context();
  pres_qvm_collision();
  pres_qvm_assets_host();
}
