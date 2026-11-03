/* Port of src/app/bootstrap/mod-presentations.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/mod_presentations.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/component_commands.h"
#include "qa/app/bootstrap/q3_client/overlay.h"
#include "qa/app/bootstrap/simulation/q3/host.h"
#include "qa/persistence/save_image.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/input/client_commands.h"
#include "qa/render/scene/world.h"

static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *xstrdup(const char *text) {
  char *copy = strdup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
typedef struct cleanup_failures { size_t count; qa_caught_error first; } cleanup_failures;
static void attempt(cleanup_failures *failures, void (*fn)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return;
  if (failures->count++ == 0) failures->first = caught;
}
static void raise_failures(const cleanup_failures *failures, const char *message) {
  qa_raise("AggregateError", 0, (long long)failures->count, "%s (%zu errors; first %s: %s)", message, failures->count,
           failures->first.name, failures->first.message);
}

/* ---- the shared scene effect frame ---- */
typedef qa_app_q3_scene_operations owned_operations;
static void release_owned_operations(void *value) {
  qa_app_q3_scene_operations_release(value);
  free(value);
}
static void release_admission(void *value) {
  qa_world_surface_admission_destroy(value);
  free(value);
}
void qa_app_q3_scene_effect_frame(qa_app_application_q3_scene_renderer *renderer, const qa_q3_pres_scene_content *scene,
    qa_scene_camera camera, qa_source_scene_order *order, const qa_scene_fog *q1_fog, qa_seat_id seat,
    double time_milliseconds, bool split_screen, qa_app_application_effect_frame *out) {
  memset(out, 0, sizeof *out);
  double first = qa_reserve_source_entity_range(order, (double)scene->admission.entity_count);
  out->light_count = scene->light_count;
  out->lights = xcalloc(scene->light_count, sizeof *out->lights);
  size_t q3_count = scene->light_count < 32 ? scene->light_count : 32;
  out->q3_lights = xcalloc(q3_count, sizeof *out->q3_lights);
  out->q3_light_count = q3_count;
  for (size_t i = 0; i < scene->light_count; i++) {
    const qa_scene_light *light = &scene->lights[i];
    out->lights[i] = (qa_surface_dynamic_light){light->origin, light->radius, 0, light->color};
    if (i < q3_count) out->q3_lights[i] = (qa_q3_dynamic_light){light->origin, light->radius, light->color, light->additive};
  }
  out->q3_admissions = xcalloc(1, sizeof *out->q3_admissions);
  out->q3_admissions[0] = &scene->admission; out->q3_admission_count = 1;
  qa_world_surface_admission *admission = xcalloc(1, sizeof *admission);
  qa_create_world_surface_admission(order, admission);
  qa_app_effect_storage_add(&out->storage, release_admission, admission);
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.source = admission; input.camera = camera;
  input.time.kind = QA_SOURCE_TIME_MILLISECONDS; input.time.value = time_milliseconds;
  input.target_is_seat = true; input.target_seat = seat;
  input.lights = out->lights; input.light_count = out->light_count; input.has_lights = true;
  if (q1_fog != NULL) { input.has_q1_fog = true; input.q1_fog = *q1_fog; }
  qa_app_q3_scene_render_options options = {.no_world_model = false, .split_screen = split_screen};
  owned_operations *owned = xcalloc(1, sizeof *owned);
  qa_app_effect_storage_add(&out->storage, release_owned_operations, owned);
  qa_app_application_q3_scene_renderer_operations(renderer, scene, &input, first, options, NULL, 0, owned);
  for (size_t i = 0; i < owned->count; i++) {
    qa_app_effect_operation operation;
    memset(&operation, 0, sizeof operation);
    operation.scene = owned->items[i];
    qa_app_effect_operations_push(&out->operations, operation);
  }
}

/* ---- collection ---- */
typedef struct entry entry;
typedef struct qa_app_application_mod_presentations collection;
struct entry {
  collection *owner;
  qa_app_viewing_seat presentation;
  const qa_active_mod_presentation *source;
  qa_app_application_mod_presentation *consumer;
  qa_app_mod_presentation_options consumer_options;
  qa_app_component_client_command_target target;
  char instance; /* the TS Symbol: its address is the identity */
  qa_command_producer producer;
  qa_command_origin origin_storage;
  qa_command_context context;
  qa_command_context command_source;
  qa_command_producer command_producer;
  double generation;
  bool commands_closed;
  bool has_release_producer; int64_t release_producer;
  qa_client_command_owner *registration;
  bool bound; int64_t unbind;
  bool published;
  const qa_q3_pres_scene_content *scene;
  double time;
  /* Overlay draw storage (TS GC). */
  qa_world_material_context material;
  owned_operations *overlay_operations; qa_finished_operations overlay_finished;
  char *render_text[8];
};
typedef struct seat_group {
  qa_app_viewing_seat seat;
  entry **entries; size_t count, capacity;
} seat_group;
struct qa_app_application_mod_presentations {
  qa_app_mod_presentations_options options;
  seat_group *groups; size_t group_count, group_capacity;
  bool closed, busy, unpublished;
};

static void assert_open(collection *c) {
  if (c->closed) qa_raise("Error", 0, 0, "Component presentation collection is closed");
}
static const char *entry_id(const entry *e) { return e->source->prepared.source.id; }

/* TS current(): the command owner is live. */
static void current(entry *e) {
  assert_open(e->owner);
  const qa_mod_qvm_presentation_source *source = &e->source->source;
  source->assert_current(source->ctx);
  if (e->commands_closed || source->generation != e->generation || !source->live(source->ctx, e->target.viewer)
      || !qa_same_actor(e->presentation.actor(e->presentation.ctx), e->target.viewer))
    qa_raise("Error", 0, 0, "Component client command owner is retired");
}
static qa_app_application_input *input_of(entry *e) {
  current(e);
  if (e->owner->options.input == NULL || e->owner->options.queue_command == NULL)
    qa_raise("Error", 0, 0, "Component cgame requires actual client command services");
  return e->owner->options.input;
}
static void queue(entry *e, const char *mode, const char *const *argv, size_t argc, const qa_command_context *caller) {
  (void)input_of(e);
  if (argc == 0) return;
  qa_app_component_client_command_request request;
  memset(&request, 0, sizeof request);
  request.consumer = &e->target; request.mode = mode; request.name = argv[0];
  request.arguments = argv + 1; request.argument_count = argc - 1;
  request.seat = e->target.seat;
  request.source = *caller; request.source.producer = &e->producer;
  if (e->owner->options.queue_command != NULL) e->owner->options.queue_command(e->owner->options.ctx, &request);
}
typedef struct close_step { entry *e; int step; } close_step;
static void command_close_step(void *ctx) {
  close_step *s = ctx;
  entry *e = s->e;
  if (s->step == 0) {
    if (e->has_release_producer) {
      e->has_release_producer = false;
      qa_command_buffer_unbind_producer(qa_app_application_input_commands(e->owner->options.input), e->release_producer);
    }
  } else if (s->step == 1) {
    if (e->registration != NULL)
      qa_client_command_owner_close(qa_app_application_input_client_commands(e->owner->options.input), e->registration);
  } else if (e->owner->options.input != NULL) {
    qa_command_buffer_discard_producer(qa_app_application_input_commands(e->owner->options.input), &e->instance);
  }
}
static void close_commands(entry *e) {
  if (e->commands_closed) return;
  e->commands_closed = true;
  cleanup_failures failures = {0};
  for (int step = 0; step < 3; step++) { close_step s = {e, step}; attempt(&failures, command_close_step, &s); }
  e->registration = NULL;
  if (failures.count != 0) raise_failures(&failures, "Component client command cleanup failed");
}
static void close_commands_run(void *ctx) { close_commands(ctx); }

/* ---- consumer options ---- */
static void entry_print(void *ctx, const char *text) {
  entry *e = ctx;
  e->owner->options.print(e->owner->options.ctx, text);
}
static qa_vec3 entry_view_origin(void *ctx) {
  entry *e = ctx;
  return e->presentation.camera(e->presentation.ctx).origin;
}
static qa_axis entry_view_axis(void *ctx) {
  entry *e = ctx;
  return e->presentation.camera(e->presentation.ctx).axis;
}
static void entry_next_frame(void *ctx) {
  entry *e = ctx;
  e->owner->options.next_frame(e->owner->options.ctx);
}
static void append_text(void *ctx, const char *text) {
  entry *e = ctx;
  qa_app_application_input *host = input_of(e);
  const qa_command_context *caller = e->owner != NULL && e->consumer != NULL ? &e->command_source : &e->context;
  qa_command_origin origin;
  memset(&origin, 0, sizeof origin);
  origin.kind = QA_COMMAND_ORIGIN_SCRIPT; origin.name = e->producer.module.artifact_path; origin.caller = &caller->origin;
  qa_command_context source = e->context;
  source.origin = origin;
  qa_command_buffer_append(qa_app_application_input_commands(host), text, &source, QA_COMMAND_DIALECT_Q3);
}
static const qa_app_system_cinematic_host *entry_system_cinematics(void *ctx, qa_app_application_mod_presentation *owner) {
  entry *e = ctx;
  qa_app_component_cinematic_scope scope = {qa_app_application_mod_presentation_cvars(owner),
      qa_app_application_mod_presentation_file_mounts(owner), e, append_text};
  return e->owner->options.system_cinematics(e->owner->options.ctx, e->source, &e->presentation, &scope);
}
typedef struct media_current { entry *e; bool (*consumer_current)(void *ctx); void *consumer_ctx; } media_current;
static bool media_is_current(void *ctx) {
  media_current *m = ctx;
  entry *e = m->e;
  return !e->owner->closed && !e->commands_closed && qa_same_actor(e->presentation.actor(e->presentation.ctx), e->target.viewer)
      && m->consumer_current(m->consumer_ctx);
}
static void entry_media(void *ctx, const qa_component_presentation_media_request *request, bool initializing,
                        bool (*consumer_current)(void *current_ctx), void *consumer_ctx) {
  entry *e = ctx;
  current(e);
  media_current m = {e, consumer_current, consumer_ctx};
  e->owner->options.presentation_media(e->owner->options.ctx, e->source, request, initializing, media_is_current, &m);
  current(e);
}
static void client_execute(void *ctx, qa_command_invocation *command) {
  entry *e = ctx;
  queue(e, "console", (const char *const *)command->argv, command->argc, &command->source);
}
static void entry_register(void *ctx, const char *name) {
  entry *e = ctx;
  qa_app_application_input *host = input_of(e);
  if (e->registration == NULL) {
    qa_client_command_handler handler = {e, &e->instance, e->source->prepared.source.id, client_execute};
    e->registration = qa_app_application_input_client_command_registration(host, e->target.seat, &handler);
  }
  qa_client_command_owner_register(qa_app_application_input_client_commands(host), e->registration, name);
}
static void entry_remove(void *ctx, const char *name) {
  entry *e = ctx;
  current(e);
  if (e->registration != NULL)
    qa_client_command_owner_remove(qa_app_application_input_client_commands(e->owner->options.input), e->registration, name);
}
static void entry_reliable(void *ctx, const char *text) {
  entry *e = ctx;
  qa_command_tokens tokens;
  memset(&tokens, 0, sizeof tokens);
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))qa_command_tokens_destroy, &tokens);
  queue(e, "reliable", (const char *const *)tokens.argv, tokens.argc, e->consumer != NULL ? &e->command_source : &e->context);
  qa_cleanup_pop(&node);
  qa_command_tokens_destroy(&tokens);
}
static void reject_takeover(void) {
  qa_raise("Error", 0, 0, "Component cgame requires an unsupported destination view or overlay takeover");
}
static void reject_scene(void *ctx, qa_q3_pres_presented_scene *scene) { (void)ctx; qa_q3_pres_presented_scene_destroy(scene); reject_takeover(); }
static void reject_command(void *ctx, const qa_render_command *command) { (void)ctx; (void)command; reject_takeover(); }
static void reject_text(void *ctx, const qa_material_text_draw *draw) { (void)ctx; (void)draw; reject_takeover(); }
static void reject_listener(void *ctx, qa_vec3 origin, qa_axis axis) { (void)ctx; (void)origin; (void)axis; reject_takeover(); }

/* ---- publish: effects, overlay and producer bindings ---- */
static void entry_effect_frame(void *ctx, qa_scene_camera camera, qa_source_scene_order *order, const qa_scene_fog *q1_fog,
                               qa_app_application_effect_frame *out) {
  entry *e = ctx;
  memset(out, 0, sizeof *out);
  if (e->scene == NULL || !qa_app_application_mod_presentation_owns(e->consumer, e->source, e->presentation.actor(e->presentation.ctx)))
    return;
  qa_app_q3_scene_effect_frame(qa_app_application_mod_presentation_renderer(e->consumer), e->scene, camera, order, q1_fog,
      e->presentation.seat(e->presentation.ctx), e->time, e->presentation.split_screen(e->presentation.ctx), out);
}
static const qa_app_component_body *entry_bodies(void *ctx, size_t *out_count) {
  entry *e = ctx;
  *out_count = 0;
  if (!qa_app_application_mod_presentation_owns(e->consumer, e->source, e->presentation.actor(e->presentation.ctx))) return NULL;
  return qa_app_application_mod_presentation_bodies(e->consumer, out_count);
}
static void release_overlay(entry *e) {
  if (e->overlay_operations != NULL) { release_owned_operations(e->overlay_operations); e->overlay_operations = NULL; }
  qa_finished_operations_destroy(&e->overlay_finished);
  memset(&e->overlay_finished, 0, sizeof e->overlay_finished);
  for (size_t i = 0; i < 8; i++) { free(e->render_text[i]); e->render_text[i] = NULL; }
}
static qa_material_draw_context overlay_material(void *ctx, qa_scene_camera camera, qa_seat_id seat, double time) {
  entry *e = ctx;
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.camera = camera; input.target_is_seat = true; input.target_seat = seat;
  input.time.kind = QA_SOURCE_TIME_MILLISECONDS; input.time.value = time;
  qa_world_scene_material_context(qa_app_application_assets_world(e->owner->options.assets), &input, NULL, NULL, &e->material);
  return e->material.context;
}
static void overlay_scene(void *ctx, const qa_q3_pres_presented_scene *scene, qa_scene_camera camera, qa_seat_id seat,
                          double time, const qa_render_operation **out_operations, size_t *out_count) {
  entry *e = ctx;
  release_overlay(e);
  qa_source_scene_order order;
  qa_source_scene_order_init(&order, qa_app_application_assets_material_registrations(e->owner->options.assets));
  qa_world_surface_admission admission;
  qa_create_world_surface_admission(&order, &admission);
  qa_world_view_input input;
  memset(&input, 0, sizeof input);
  input.camera = camera; input.target_is_seat = true; input.target_seat = seat;
  input.time.kind = QA_SOURCE_TIME_MILLISECONDS; input.time.value = time; input.source = &admission;
  for (size_t i = 0; i < 8; i++) {
    const qa_q3_pres_render_row *row = &scene->source.text.rows[i];
    size_t length = row->length < 32 ? row->length : 32;
    e->render_text[i] = xcalloc(length + 1, 1);
    memcpy(e->render_text[i], row->bytes, length);
  }
  input.render_text = (const char *const *)e->render_text; input.render_text_count = 8;
  double first = qa_reserve_source_entity_range(&order, (double)scene->content.admission.entity_count);
  qa_app_q3_scene_render_options options = {.no_world_model = true};
  e->overlay_operations = xcalloc(1, sizeof *e->overlay_operations);
  qa_app_application_q3_scene_renderer_operations(qa_app_application_mod_presentation_renderer(e->consumer),
      &scene->content, &input, first, options, NULL, 0, e->overlay_operations);
  if (!qa_finish_scene_operations(e->overlay_operations->items, e->overlay_operations->count, &e->overlay_finished))
    qa_range_error("Out of memory");
  qa_world_surface_admission_destroy(&admission);
  *out_operations = e->overlay_finished.operations; *out_count = e->overlay_finished.count;
}
static void entry_overlay_draw(void *ctx, qa_scene_frame_builder *frames, qa_scene_camera camera) {
  entry *e = ctx;
  if (!qa_app_application_mod_presentation_owns(e->consumer, e->source, e->presentation.actor(e->presentation.ctx))) return;
  size_t count = 0;
  const qa_app_q3_overlay_submission *hud = qa_app_application_mod_presentation_hud(e->consumer, &count);
  qa_app_q3_overlay_options options = {hud, count, {e, overlay_material, overlay_scene}, frames, camera,
      e->presentation.viewport(e->presentation.ctx), e->presentation.seat(e->presentation.ctx), e->time};
  qa_app_draw_q3_overlay(options);
}
static qa_cvar_registry *producer_cvars(void *ctx, qa_command_context caller) {
  entry *e = ctx;
  current(e);
  const qa_module_identity *expected = &e->producer.module;
  const qa_module_identity *actual = caller.producer != NULL ? &caller.producer->module : NULL;
  if (actual == NULL || strcmp(actual->id, expected->id) != 0 || strcmp(actual->digest, expected->digest) != 0
      || strcmp(actual->revision, expected->revision) != 0 || strcmp(actual->artifact_path, expected->artifact_path) != 0)
    qa_raise("Error", 0, 0, "Component command producer differs from its original client module");
  return qa_app_application_mod_presentation_cvars(e->consumer);
}
typedef struct script_job { entry *e; const char *name; qa_command_context caller; qa_script_read_result result; } script_job;
static void read_script_run(void *ctx) {
  script_job *job = ctx;
  entry *e = job->e;
  current(e);
  qa_content_opened_resource resource;
  bool found = qa_content_mounted_open(qa_app_application_mod_presentation_file_mounts(e->consumer), job->name, NULL, NULL, &resource);
  qa_cleanup node;
  qa_cleanup_push(&node, free, found ? resource.bytes : NULL);
  current(e);
  if (job->caller.producer == NULL || job->caller.producer->instance != &e->instance)
    qa_raise("Error", 0, 0, "Component script changed its producer");
  qa_cleanup_pop(&node);
  job->result.kind = QA_SCRIPT_READ_READY;
  if (!found) return;
  job->result.has_text = true;
  job->result.text = xcalloc(resource.byte_length + 1, 1);
  memcpy(job->result.text, resource.bytes, resource.byte_length);
  free(resource.bytes);
}
static qa_script_read_result producer_read_script(void *ctx, const char *name, qa_command_context caller) {
  script_job job;
  memset(&job, 0, sizeof job);
  job.e = ctx; job.name = name; job.caller = caller;
  qa_caught_error error;
  if (qa_try(read_script_run, &job, &error)) return job.result;
  qa_script_read_result failed;
  memset(&failed, 0, sizeof failed);
  failed.kind = QA_SCRIPT_READ_FAILED; failed.error_message = xstrdup(error.message);
  return failed;
}
static void producer_command(void *ctx, qa_command_invocation *command) {
  entry *e = ctx;
  qa_command_invocation_assert_active(command);
  queue(e, "console", (const char *const *)command->argv, command->argc, &command->source);
}
static bool producer_engine_command(void *ctx, const char *name) {
  entry *e = ctx;
  size_t length = strlen(name);
  char *lower = xcalloc(length + 1, 1);
  for (size_t i = 0; i < length; i++) lower[i] = (char)tolower((unsigned char)name[i]);
  bool result = qa_app_component_engine_command(qa_command_buffer_dialect(qa_app_application_input_commands(e->owner->options.input)),
      qa_app_application_assets_content(e->owner->options.assets), lower);
  free(lower);
  return result;
}
static void publish(entry *e) {
  if (e->published) return;
  current(e);
  qa_app_component_effect_frame frame = {e, entry_effect_frame};
  qa_app_component_overlay overlay = {e->source->owner, e, entry_bodies, entry_overlay_draw};
  e->unbind = e->presentation.bind_component_effects(e->presentation.ctx, frame, &overlay);
  e->bound = true;
  if (e->owner->options.input != NULL) {
    qa_command_producer_services services;
    memset(&services, 0, sizeof services);
    services.ctx = e; services.cvars = producer_cvars; services.read_script = producer_read_script; services.read_script_ctx = e;
    services.command = producer_command; services.engine_command = producer_engine_command;
    e->release_producer = qa_command_buffer_bind_producer(qa_app_application_input_commands(e->owner->options.input), &e->instance, services);
    e->has_release_producer = true;
  }
  qa_app_application_mod_presentation_publish_commands(e->consumer);
  e->published = true;
}
static void entry_unbind(entry *e) {
  if (!e->bound) return;
  e->bound = false;
  e->presentation.unbind_component_effects(e->presentation.ctx, e->unbind);
}
static void entry_unbind_run(void *ctx) { entry_unbind(ctx); }
static void entry_free(entry *e) {
  release_overlay(e);
  free(e);
}
static void consumer_destroy_run(void *ctx) {
  entry *e = ctx;
  qa_app_application_mod_presentation *consumer = e->consumer;
  e->consumer = NULL;
  qa_app_application_mod_presentation_destroy(consumer);
}

typedef struct restore_checkpoint {
  const qa_save_value *value;
  qa_qvm_resolve_saved_actor_fn resolve; void *resolve_ctx;
} restore_checkpoint;
typedef struct create_job { entry *e; const restore_checkpoint *checkpoint; } create_job;
static void create_consumer_run(void *ctx) {
  create_job *job = ctx;
  entry *e = job->e;
  e->consumer = job->checkpoint == NULL ? qa_app_application_mod_presentation_create(&e->consumer_options, -1)
      : qa_app_application_mod_presentation_restore(&e->consumer_options, job->checkpoint->value, job->checkpoint->resolve,
                                                    job->checkpoint->resolve_ctx);
}
static void after_create_run(void *ctx) {
  create_job *job = ctx;
  assert_open(job->e->owner);
  if (job->checkpoint == NULL) publish(job->e);
}
static entry *create_entry(collection *c, const qa_app_viewing_seat *presentation, const qa_active_mod_presentation *source,
                           const restore_checkpoint *checkpoint) {
  entry *e = xcalloc(1, sizeof *e);
  e->owner = c; e->presentation = *presentation; e->source = source;
  e->target.owner = source->owner; e->target.instance = &e->instance;
  e->target.seat = presentation->seat(presentation->ctx); e->target.viewer = presentation->actor(presentation->ctx);
  e->producer.kind = "client-module"; e->producer.module = source->prepared.artifact->module; e->producer.instance = &e->instance;
  e->context.session = e->target.seat.session;
  e->context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT; e->context.origin.seat = e->target.seat;
  e->context.origin.client = presentation->client(presentation->ctx);
  e->context.producer = &e->producer;
  e->command_source = e->context;
  e->generation = source->source.generation;
  qa_app_mod_presentation_options *o = &e->consumer_options;
  o->assets = c->options.assets; o->audio = c->options.audio; o->queries = c->options.queries;
  o->seat = e->target.seat; o->viewer = e->target.viewer; o->viewport = presentation->viewport(presentation->ctx);
  o->renderer = c->options.renderer; o->source = source; o->clock = c->options.clock;
  o->output = (qa_app_mod_presentation_output){e, reject_scene, reject_command, reject_text, reject_listener};
  if (c->options.system_cinematics != NULL) { o->system_cinematics.ctx = e; o->system_cinematics.of = entry_system_cinematics; }
  o->has_commands = true;
  o->commands.ctx = e; o->commands.append = append_text; o->commands.register_command = entry_register;
  o->commands.remove_command = entry_remove; o->commands.reliable = entry_reliable;
  if (c->options.presentation_media != NULL) { o->presentation_media.ctx = e; o->presentation_media.deliver = entry_media; }
  o->ctx = e; o->print = entry_print; o->view_origin = entry_view_origin; o->view_axis = entry_view_axis;
  o->next_frame = entry_next_frame; o->hosts = c->options.hosts;
  create_job job = {e, checkpoint};
  qa_caught_error error;
  if (!qa_try(create_consumer_run, &job, &error)) {
    qa_caught_error cleanup;
    bool closed = qa_try(close_commands_run, e, &cleanup);
    entry_free(e);
    if (!closed)
      qa_raise("AggregateError", 0, 2, "Component cgame initialization and command cleanup failed (first %s: %s; cleanup %s: %s)",
               error.name, error.message, cleanup.name, cleanup.message);
    qa_rethrow(&error);
  }
  if (!qa_try(after_create_run, &job, &error)) {
    cleanup_failures failures = {0};
    attempt(&failures, entry_unbind_run, e);
    attempt(&failures, close_commands_run, e);
    attempt(&failures, consumer_destroy_run, e);
    entry_free(e);
    if (failures.count > 0)
      qa_raise("AggregateError", 0, (long long)failures.count + 1, "Component presentation creation and cleanup failed (first %s: %s; cleanup %s: %s)",
               error.name, error.message, failures.first.name, failures.first.message);
    qa_rethrow(&error);
  }
  return e;
}
/* TS remove(entries, id, entry): the entry is already out of its map. */
static void remove_entry(entry *e) {
  cleanup_failures failures = {0};
  attempt(&failures, entry_unbind_run, e);
  attempt(&failures, close_commands_run, e);
  attempt(&failures, consumer_destroy_run, e);
  entry_free(e);
  if (failures.count != 0) raise_failures(&failures, "Component presentation removal failed");
}
static void remove_entry_run(void *ctx) { remove_entry(ctx); }
static entry *take_entry(seat_group *group, size_t index) {
  entry *e = group->entries[index];
  memmove(&group->entries[index], &group->entries[index + 1], (group->count - index - 1) * sizeof *group->entries);
  group->count--;
  return e;
}
static void group_add(seat_group *group, entry *e) {
  if (group->count == group->capacity) {
    group->capacity = group->capacity == 0 ? 4 : group->capacity * 2;
    entry **next = realloc(group->entries, group->capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    group->entries = next;
  }
  group->entries[group->count++] = e;
}
static seat_group *group_of(collection *c, const void *seat_ctx) {
  for (size_t i = 0; i < c->group_count; i++) if (c->groups[i].seat.ctx == seat_ctx) return &c->groups[i];
  return NULL;
}
static seat_group *group_add_seat(collection *c, const qa_app_viewing_seat *seat) {
  if (c->group_count == c->group_capacity) {
    c->group_capacity = c->group_capacity == 0 ? 4 : c->group_capacity * 2;
    seat_group *next = realloc(c->groups, c->group_capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    c->groups = next;
  }
  seat_group *group = &c->groups[c->group_count++];
  memset(group, 0, sizeof *group);
  group->seat = *seat;
  return group;
}
static void group_delete(collection *c, size_t index) {
  free(c->groups[index].entries);
  memmove(&c->groups[index], &c->groups[index + 1], (c->group_count - index - 1) * sizeof *c->groups);
  c->group_count--;
}
static bool seat_listed(const qa_app_viewing_seat *seats, size_t count, const void *ctx) {
  for (size_t i = 0; i < count; i++) if (seats[i].ctx == ctx) return true;
  return false;
}

qa_app_application_mod_presentations *qa_app_application_mod_presentations_create(const qa_app_mod_presentations_options *options) {
  collection *c = xcalloc(1, sizeof *c);
  c->options = *options;
  return c;
}

qa_save_value *qa_app_application_mod_presentations_capture_checkpoint(collection *c, const qa_app_viewing_seat *seats, size_t seat_count,
    const qa_active_mod_presentation *const *sources, size_t source_count) {
  assert_open(c);
  if (c->busy || c->unpublished || qa_app_application_mod_presentations_pending_commands(c))
    qa_raise("Error", 0, 0, "Component client checkpoint requires an idle published collection without queued commands");
  for (size_t i = 0; i < source_count; i++) for (size_t j = 0; j < i; j++)
    if (strcmp(sources[i]->prepared.source.id, sources[j]->prepared.source.id) == 0)
      qa_raise("Error", 0, 0, "Duplicate component presentation source identity");
  for (size_t g = 0; g < c->group_count; g++) {
    seat_group *group = &c->groups[g];
    if (!seat_listed(seats, seat_count, group->seat.ctx)) qa_raise("Error", 0, 0, "Component checkpoint contains a retired seat");
    for (size_t i = 0; i < group->count; i++) {
      const qa_active_mod_presentation *source = NULL;
      for (size_t s = 0; s < source_count && source == NULL; s++)
        if (strcmp(sources[s]->prepared.source.id, entry_id(group->entries[i])) == 0) source = sources[s];
      if (source == NULL || !qa_app_application_mod_presentation_owns(group->entries[i]->consumer, source, group->seat.actor(group->seat.ctx)))
        qa_raise("Error", 0, 0, "Component checkpoint contains a retired source consumer");
    }
  }
  qa_save_value *value = qa_save_value_object(), *rows = qa_save_value_array();
  if (value == NULL || rows == NULL) qa_range_error("Out of memory");
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))qa_save_value_free, value);
  qa_save_value_set(value, "version", qa_save_value_number(1));
  for (size_t s = 0; s < seat_count; s++) {
    const qa_app_viewing_seat *seat = &seats[s];
    qa_save_value *row = qa_save_value_object(), *client = qa_save_value_object(), *viewer = qa_save_value_object(), *list = qa_save_value_array();
    if (row == NULL || client == NULL || viewer == NULL || list == NULL) qa_range_error("Out of memory");
    qa_client_id id = seat->client(seat->ctx);
    qa_saved_actor_id saved = qa_saved_actor_id_of(seat->actor(seat->ctx));
    qa_save_value_set(row, "seat", qa_save_value_number((double)seat->seat(seat->ctx).index));
    qa_save_value_set(client, "slot", qa_save_value_number((double)id.slot));
    qa_save_value_set(client, "generation", qa_save_value_number((double)id.generation));
    qa_save_value_set(row, "client", client);
    qa_save_value_set(viewer, "slot", qa_save_value_number((double)saved.slot));
    qa_save_value_set(viewer, "generation", qa_save_value_number((double)saved.generation));
    qa_save_value_set(row, "viewer", viewer);
    seat_group *group = group_of(c, seat->ctx);
    for (size_t i = 0; i < source_count; i++) {
      const qa_active_mod_presentation *source = sources[i];
      entry *e = NULL;
      for (size_t k = 0; group != NULL && k < group->count && e == NULL; k++)
        if (strcmp(entry_id(group->entries[k]), source->prepared.source.id) == 0) e = group->entries[k];
      qa_save_value *saved_source = qa_save_value_object(), *owner = qa_save_value_object(), *state = qa_save_value_object();
      if (saved_source == NULL || owner == NULL || state == NULL) qa_range_error("Out of memory");
      qa_save_value_set(owner, "provider", qa_save_value_string(source->owner.provider));
      qa_save_value_set(owner, "generation", qa_save_value_number(source->owner.generation));
      qa_save_value_set(saved_source, "owner", owner);
      qa_save_value_set(saved_source, "identity", qa_save_mod_identity_value(&source->identity));
      qa_save_value *module = qa_save_value_object();
      if (module == NULL) qa_range_error("Out of memory");
      const qa_module_identity *identity = &source->prepared.artifact->module;
      qa_save_value_set(module, "id", qa_save_value_string(identity->id));
      qa_save_value_set(module, "artifactPath", qa_save_value_string(identity->artifact_path));
      qa_save_value_set(module, "digest", qa_save_value_string(identity->digest));
      qa_save_value_set(module, "revision", qa_save_value_string(identity->revision));
      qa_save_value_set(saved_source, "module", module);
      qa_save_value_set(saved_source, "declaration", qa_app_qvm_presentation_declaration_value(&source->prepared.declaration));
      qa_save_value_set(state, "kind", qa_save_value_string(e == NULL ? "lazy" : "initialized"));
      if (e != NULL) {
        qa_save_value_set(state, "checkpoint", qa_app_application_mod_presentation_capture_checkpoint(e->consumer));
      }
      qa_save_value_set(saved_source, "state", state);
      qa_save_value_push(list, saved_source);
    }
    qa_save_value_set(row, "sources", list);
    qa_save_value_push(rows, row);
  }
  qa_save_value_set(value, "seats", rows);
  qa_cleanup_pop(&node);
  return value;
}

/* node:util isDeepStrictEqual over checkpoint values (object key order is ignored). */
static bool values_equal(const qa_save_value *a, const qa_save_value *b) {
  if (a == NULL || b == NULL) return a == b;
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_UNDEFINED: case QA_SAVE_NULL: return true;
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number || (a->number != a->number && b->number != b->number);
    case QA_SAVE_BIGINT: case QA_SAVE_STRING: return strcmp(a->text, b->text) == 0;
    case QA_SAVE_BYTES: return a->byte_length == b->byte_length && (a->byte_length == 0 || memcmp(a->bytes, b->bytes, a->byte_length) == 0);
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!values_equal(a->items[i], b->items[i])) return false;
      return true;
    default:
      if (a->entry_count != b->entry_count) return false;
      for (size_t i = 0; i < a->entry_count; i++)
        if (!values_equal(a->entries[i].value, qa_save_value_get(b, a->entries[i].key))) return false;
      return true;
  }
}
static bool reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("Out of memory");
  return true;
}
static bool reader_item(const qa_save_reader *reader, size_t index, qa_save_reader *out) {
  if (!qa_save_reader_item(reader, index, out)) qa_range_error("Out of memory");
  return true;
}
static bool saved_equal(const qa_save_reader *field, qa_save_value *expected) {
  bool equal = values_equal(field->value, expected);
  qa_save_value_free(expected);
  return equal;
}
typedef struct restore_client { const qa_active_mod_presentation *source; bool initialized; const qa_save_value *checkpoint; } restore_client;
typedef struct restore_row { const qa_app_viewing_seat *presentation; restore_client *clients; size_t count; } restore_row;
typedef struct restore_job {
  collection *c;
  restore_row *rows; size_t row_count;
  qa_qvm_resolve_saved_actor_fn resolve; void *resolve_ctx;
} restore_job;
static void restore_run(void *ctx) {
  restore_job *job = ctx;
  collection *c = job->c;
  for (size_t r = 0; r < job->row_count; r++) for (size_t i = 0; i < job->rows[r].count; i++) {
    restore_client *client = &job->rows[r].clients[i];
    if (!client->initialized) continue;
    restore_checkpoint checkpoint = {client->checkpoint, job->resolve, job->resolve_ctx};
    entry *e = create_entry(c, job->rows[r].presentation, client->source, &checkpoint);
    if (c->closed) { remove_entry(e); assert_open(c); }
    seat_group *group = group_of(c, job->rows[r].presentation->ctx);
    if (group == NULL) group = group_add_seat(c, job->rows[r].presentation);
    group_add(group, e);
  }
}
static void close_run(void *ctx) { qa_app_application_mod_presentations_close(ctx); }
void qa_app_application_mod_presentations_restore_checkpoint(collection *c, const qa_save_value *value,
    const qa_app_viewing_seat *seats, size_t seat_count, const qa_active_mod_presentation *const *sources, size_t source_count,
    qa_qvm_resolve_saved_actor_fn resolve_actor, void *resolve_ctx) {
  assert_open(c);
  if (c->busy || c->unpublished || c->group_count != 0)
    qa_raise("Error", 0, 0, "Component client restore requires an unpublished empty collection");
  qa_save_reader r, field;
  if (!qa_save_reader_init(&r, value, "component-clients")) qa_range_error("Out of memory");
  reader_field(&r, "version", &field); qa_save_reader_literal_number(&field, 1);
  qa_save_reader list;
  reader_field(&r, "seats", &list);
  size_t row_count = qa_save_reader_list_count(&list);
  restore_job job = {c, xcalloc(row_count, sizeof(restore_row)), row_count, resolve_actor, resolve_ctx};
  qa_cleanup rows_node;
  qa_cleanup_push(&rows_node, free, job.rows);
  for (size_t index = 0; index < row_count; index++) {
    qa_save_reader row, part, client;
    reader_item(&list, index, &row);
    reader_field(&row, "seat", &part);
    double seat = qa_save_reader_integer_min(&part, 0);
    const qa_app_viewing_seat *presentation = NULL;
    for (size_t s = 0; s < seat_count && presentation == NULL; s++)
      if ((double)seats[s].seat(seats[s].ctx).index == seat) presentation = &seats[s];
    bool duplicate = false;
    for (size_t k = 0; k < index; k++) if (job.rows[k].presentation == presentation) duplicate = true;
    if (duplicate || presentation == NULL) qa_save_reader_fail(&row, "duplicate or missing component seat");
    qa_client_id id = presentation->client(presentation->ctx);
    reader_field(&row, "client", &client);
    qa_save_reader slot, generation, viewer;
    reader_field(&client, "slot", &slot); reader_field(&client, "generation", &generation);
    reader_field(&row, "viewer", &viewer);
    bool changed = qa_save_reader_integer_min(&slot, 0) != (double)id.slot || qa_save_reader_integer_min(&generation, 0) != (double)id.generation;
    if (!changed) changed = !qa_same_actor(resolve_actor(resolve_ctx, qa_read_saved_actor(&viewer)), presentation->actor(presentation->ctx));
    if (changed) qa_save_reader_fail(&row, "component viewing client changed");
    qa_save_reader saved_list;
    reader_field(&row, "sources", &saved_list);
    size_t count = qa_save_reader_list_count(&saved_list);
    restore_client *clients = xcalloc(count, sizeof *clients);
    job.rows[index].presentation = presentation; job.rows[index].clients = clients; job.rows[index].count = count;
    for (size_t i = 0; i < count; i++) {
      qa_save_reader saved, owner, provider_field;
      reader_item(&saved_list, i, &saved);
      reader_field(&saved, "owner", &owner);
      reader_field(&owner, "provider", &provider_field);
      const char *provider = qa_save_reader_string(&provider_field);
      const qa_active_mod_presentation *source = NULL;
      for (size_t s = 0; s < source_count && source == NULL; s++) if (strcmp(sources[s]->owner.provider, provider) == 0) source = sources[s];
      bool seen = false;
      for (size_t k = 0; k < i; k++) if (strcmp(clients[k].source->owner.provider, provider) == 0) seen = true;
      if (source == NULL || seen) qa_save_reader_fail(&saved, "duplicate or missing component source");
      qa_save_value *expected_owner = qa_save_value_object();
      if (expected_owner == NULL) qa_range_error("Out of memory");
      qa_save_value_set(expected_owner, "provider", qa_save_value_string(source->owner.provider));
      qa_save_value_set(expected_owner, "generation", qa_save_value_number(source->owner.generation));
      qa_save_value *module = qa_save_value_object();
      if (module == NULL) qa_range_error("Out of memory");
      const qa_module_identity *identity = &source->prepared.artifact->module;
      qa_save_value_set(module, "id", qa_save_value_string(identity->id));
      qa_save_value_set(module, "artifactPath", qa_save_value_string(identity->artifact_path));
      qa_save_value_set(module, "digest", qa_save_value_string(identity->digest));
      qa_save_value_set(module, "revision", qa_save_value_string(identity->revision));
      qa_save_reader saved_identity, saved_module, saved_declaration;
      reader_field(&saved, "identity", &saved_identity); reader_field(&saved, "module", &saved_module);
      reader_field(&saved, "declaration", &saved_declaration);
      bool owner_equal = saved_equal(&owner, expected_owner);
      bool identity_equal = saved_equal(&saved_identity, qa_save_mod_identity_value(&source->identity));
      bool module_equal = saved_equal(&saved_module, module);
      bool declaration_equal = saved_equal(&saved_declaration, qa_app_qvm_presentation_declaration_value(&source->prepared.declaration));
      if (!owner_equal || !identity_equal || !module_equal || !declaration_equal)
        qa_save_reader_fail(&saved, "component activation or original module identity changed");
      qa_save_reader state, kind;
      reader_field(&saved, "state", &state); reader_field(&state, "kind", &kind);
      static const char *const kinds[] = {"lazy", "initialized"};
      const char *chosen = qa_save_reader_choice_string(&kind, kinds, 2);
      clients[i].source = source; clients[i].initialized = strcmp(chosen, "initialized") == 0;
      if (clients[i].initialized) { qa_save_reader checkpoint; reader_field(&state, "checkpoint", &checkpoint); clients[i].checkpoint = checkpoint.value; }
    }
    if (count != source_count) qa_save_reader_fail(&row, "required component client state is missing");
  }
  if (row_count != seat_count) qa_save_reader_fail(&r, "required viewing seat state is missing");
  c->busy = true; c->unpublished = true;
  qa_caught_error error;
  bool ok = qa_try(restore_run, &job, &error);
  qa_cleanup_pop(&rows_node);
  for (size_t i = 0; i < row_count; i++) free(job.rows[i].clients);
  free(job.rows);
  if (!ok) {
    qa_caught_error cleanup;
    if (!qa_try(close_run, c, &cleanup)) {
      c->busy = false;
      qa_raise("AggregateError", 0, 2, "Component client restore and cleanup failed (first %s: %s; cleanup %s: %s)",
               error.name, error.message, cleanup.name, cleanup.message);
    }
    c->busy = false;
    qa_rethrow(&error);
  }
  c->busy = false;
}
void qa_app_application_mod_presentations_publish_restored(collection *c) {
  assert_open(c);
  if (c->busy) qa_raise("Error", 0, 0, "Component client restore is still running");
  if (!c->unpublished) return;
  for (size_t g = 0; g < c->group_count; g++) for (size_t i = 0; i < c->groups[g].count; i++) publish(c->groups[g].entries[i]);
  c->unpublished = false;
}
bool qa_app_application_mod_presentations_pending_commands(collection *c) {
  if (c->options.input == NULL) return false;
  qa_command_buffer *commands = qa_app_application_input_commands(c->options.input);
  for (size_t g = 0; g < c->group_count; g++) for (size_t i = 0; i < c->groups[g].count; i++)
    if (qa_command_buffer_producer_pending(commands, &c->groups[g].entries[i]->instance)) return true;
  return false;
}
/* TS commandEntry(source): NULL is the TS null. */
static entry *command_entry(collection *c, const qa_command_context *source) {
  const qa_command_producer *producer = source->producer;
  if (producer == NULL || producer->kind == NULL || strcmp(producer->kind, "client-module") != 0) return NULL;
  assert_open(c);
  for (size_t g = 0; g < c->group_count; g++) for (size_t i = 0; i < c->groups[g].count; i++) {
    entry *e = c->groups[g].entries[i];
    if (&e->instance != producer->instance) continue;
    const qa_module_identity *expected = &e->source->prepared.artifact->module;
    if (strcmp(producer->module.id, expected->id) != 0 || strcmp(producer->module.digest, expected->digest) != 0
        || strcmp(producer->module.revision, expected->revision) != 0 || strcmp(producer->module.artifact_path, expected->artifact_path) != 0
        || !qa_app_application_mod_presentation_owns(e->consumer, e->source, e->target.viewer))
      qa_raise("Error", 0, 0, "Component client command producer is retired or differs from its source");
    return e;
  }
  qa_raise("Error", 0, 0, "Component client command producer is retired");
}
qa_cvar_registry *qa_app_application_mod_presentations_command_cvars(collection *c, const qa_command_context *source) {
  entry *e = command_entry(c, source);
  return e == NULL ? NULL : qa_app_application_mod_presentation_cvars(e->consumer);
}
char *qa_app_application_mod_presentations_read_command_script(collection *c, const char *name, const qa_command_context *source) {
  entry *e = command_entry(c, source);
  if (e == NULL) return NULL;
  qa_content_opened_resource resource;
  bool found = qa_content_mounted_open(qa_app_application_mod_presentation_file_mounts(e->consumer), name, NULL, NULL, &resource);
  qa_cleanup node;
  qa_cleanup_push(&node, free, found ? resource.bytes : NULL);
  if (command_entry(c, source) != e) qa_raise("Error", 0, 0, "Component script owner changed while reading");
  qa_cleanup_pop(&node);
  if (!found) return NULL;
  char *text = xcalloc(resource.byte_length + 1, 1);
  memcpy(text, resource.bytes, resource.byte_length);
  free(resource.bytes);
  return text;
}
bool qa_app_application_mod_presentations_queue_console(collection *c, qa_command_invocation *command) {
  entry *e = command_entry(c, &command->source);
  if (e == NULL) return false;
  qa_command_invocation_assert_active(command);
  if (command->argc == 0) return true;
  if (c->options.queue_command == NULL) qa_raise("Error", 0, 0, "Component command queue is unavailable");
  qa_app_component_client_command_request request;
  memset(&request, 0, sizeof request);
  request.consumer = &e->target; request.mode = "console"; request.name = command->argv[0];
  request.arguments = (const char *const *)command->argv + 1; request.argument_count = command->argc - 1;
  request.source = command->source; request.seat = e->target.seat;
  c->options.queue_command(c->options.ctx, &request);
  return true;
}
typedef struct dispatch_job { entry *e; const qa_app_component_client_command_request *request; bool handled; } dispatch_job;
static void dispatch_run(void *ctx) {
  dispatch_job *job = ctx;
  entry *e = job->e;
  const qa_app_component_client_command_request *request = job->request;
  const char **arguments = xcalloc(request->argument_count + 1, sizeof *arguments);
  qa_cleanup node;
  qa_cleanup_push(&node, free, arguments);
  arguments[0] = request->name;
  for (size_t i = 0; i < request->argument_count; i++) arguments[i + 1] = request->arguments[i];
  if (strcmp(request->mode, "console") == 0
      && qa_app_application_mod_presentation_command(e->consumer, arguments, request->argument_count + 1)) {
    job->handled = true;
  } else if (!qa_app_application_mod_presentation_owns(e->consumer, e->source, request->consumer->viewer)) {
    job->handled = false;
  } else {
    const qa_mod_qvm_presentation_source *source = &e->source->source;
    if (source->client_command == NULL) qa_raise("Error", 0, 0, "Component source has no admitted client command receiver");
    source->client_command(source->ctx, request->consumer->viewer, arguments, request->argument_count + 1);
    job->handled = true;
  }
  qa_cleanup_pop(&node);
  free(arguments);
}
bool qa_app_application_mod_presentations_dispatch_command(collection *c, const qa_app_component_client_command_request *request) {
  assert_open(c);
  entry *e = NULL;
  for (size_t g = 0; g < c->group_count && e == NULL; g++) for (size_t i = 0; i < c->groups[g].count && e == NULL; i++)
    if (&c->groups[g].entries[i]->target == request->consumer) e = c->groups[g].entries[i];
  if (e == NULL || !qa_app_application_mod_presentation_owns(e->consumer, e->source, request->consumer->viewer)) return false;
  qa_command_context previous = e->command_source;
  e->command_source = request->source;
  dispatch_job job = {e, request, false};
  qa_caught_error error;
  bool ok = qa_try(dispatch_run, &job, &error);
  e->command_source = previous;
  if (!ok) qa_rethrow(&error);
  return job.handled;
}
void qa_app_application_mod_presentations_retain_presentations(collection *c, const qa_app_viewing_seat *seats, size_t seat_count) {
  assert_open(c);
  cleanup_failures failures = {0};
  for (size_t g = 0; g < c->group_count;) {
    if (seat_listed(seats, seat_count, c->groups[g].seat.ctx)) { g++; continue; }
    while (c->groups[g].count > 0) attempt(&failures, remove_entry_run, take_entry(&c->groups[g], 0));
    group_delete(c, g);
  }
  if (failures.count != 0) raise_failures(&failures, "Component seat retirement failed");
}
typedef struct prepare_job {
  collection *c;
  const qa_app_viewing_seat *seats; size_t seat_count;
  const qa_active_mod_presentation *const *sources; size_t source_count;
  const qa_app_sim_presentation_event *events; size_t event_count;
  double frame_sequence;
} prepare_job;
typedef struct entry_frame_job { prepare_job *job; entry *e; const qa_active_mod_presentation *source; qa_actor_id viewer; } entry_frame_job;
static void entry_frame_run(void *ctx) {
  entry_frame_job *f = ctx;
  prepare_job *job = f->job;
  entry *e = f->e;
  const qa_active_mod_presentation *source = f->source;
  if (!source->prepared.declaration.is_scene) for (size_t i = 0; i < job->event_count; i++) {
    const qa_app_sim_presentation_event *event = &job->events[i];
    if (strcmp(event->kind, "q3-source") != 0 || event->q3_source->kind != QA_APP_Q3_EVENT_PLAYER
        || (event->has_recipient && !qa_same_actor(event->recipient, f->viewer))) continue;
    const qa_app_q3_source_player_event *player = &event->q3_source->player;
    const qa_module_identity *actual = &player->source_module, *expected = &source->prepared.source;
    if (strcmp(actual->id, expected->id) != 0 || strcmp(actual->artifact_path, expected->artifact_path) != 0
        || strcmp(actual->digest, expected->digest) != 0 || strcmp(actual->revision, expected->revision) != 0
        || strcmp(player->source_abi_profile, source->source.abi_profile) != 0) continue;
    qa_app_application_mod_presentation_consume(e->consumer, player, event->sequence);
    assert_open(job->c);
  }
  e->scene = qa_app_application_mod_presentation_frame(e->consumer, job->frame_sequence);
  assert_open(job->c);
  e->time = qa_app_application_mod_presentation_time(e->consumer);
}
static void prepare_run(void *ctx) {
  prepare_job *job = ctx;
  collection *c = job->c;
  qa_app_application_mod_presentations_retain_presentations(c, job->seats, job->seat_count);
  for (size_t i = 0; i < job->source_count; i++) for (size_t j = 0; j < i; j++)
    if (strcmp(job->sources[i]->prepared.source.id, job->sources[j]->prepared.source.id) == 0)
      qa_raise("Error", 0, 0, "Duplicate component presentation source identity");
  for (size_t g = 0; g < c->group_count;) {
    seat_group *group = &c->groups[g];
    for (size_t i = 0; i < group->count;) {
      entry *e = group->entries[i];
      const qa_active_mod_presentation *source = NULL;
      for (size_t s = 0; s < job->source_count && source == NULL; s++)
        if (strcmp(job->sources[s]->prepared.source.id, entry_id(e)) == 0) source = job->sources[s];
      if (source != NULL && qa_app_application_mod_presentation_owns(e->consumer, source, group->seat.actor(group->seat.ctx))) {
        i++; continue;
      }
      remove_entry(take_entry(group, i));
    }
    if (group->count == 0) group_delete(c, g); else g++;
  }
  for (size_t s = 0; s < job->seat_count; s++) for (size_t k = 0; k < job->source_count; k++) {
    assert_open(c);
    const qa_app_viewing_seat *presentation = &job->seats[s];
    const qa_active_mod_presentation *source = job->sources[k];
    qa_actor_id viewer = presentation->actor(presentation->ctx);
    const qa_qvm_presentation_context *context = source->source.context(source->source.ctx, viewer);
    const char *id = source->prepared.source.id;
    seat_group *group = group_of(c, presentation->ctx);
    size_t index = 0;
    entry *e = NULL;
    for (size_t i = 0; group != NULL && i < group->count && e == NULL; i++)
      if (strcmp(entry_id(group->entries[i]), id) == 0) { e = group->entries[i]; index = i; }
    if (context == NULL) {
      if (group != NULL && e != NULL) remove_entry(take_entry(group, index));
      continue;
    }
    if (e == NULL) {
      e = create_entry(c, presentation, source, NULL);
      group = group_of(c, presentation->ctx);
      if (group == NULL) group = group_add_seat(c, presentation);
      group_add(group, e);
    }
    entry_frame_job frame = {job, e, source, viewer};
    qa_caught_error error;
    if (qa_try(entry_frame_run, &frame, &error)) continue;
    group = group_of(c, presentation->ctx);
    qa_caught_error cleanup;
    bool removed = true;
    if (group != NULL) for (size_t i = 0; i < group->count; i++) if (group->entries[i] == e) {
      removed = qa_try(remove_entry_run, take_entry(group, i), &cleanup);
      break;
    }
    if (!removed)
      qa_raise("AggregateError", 0, 2, "Component presentation preparation and cleanup failed (first %s: %s; cleanup %s: %s)",
               error.name, error.message, cleanup.name, cleanup.message);
    qa_rethrow(&error);
  }
}
void qa_app_application_mod_presentations_prepare(collection *c, const qa_app_viewing_seat *seats, size_t seat_count,
    const qa_active_mod_presentation *const *sources, size_t source_count, const qa_app_sim_presentation_event *events,
    size_t event_count, double frame_sequence) {
  assert_open(c);
  if (c->unpublished) qa_raise("Error", 0, 0, "Restored component presentations are not published");
  if (c->busy) qa_raise("Error", 0, 0, "Component presentation preparation is already running");
  c->busy = true;
  prepare_job job = {c, seats, seat_count, sources, source_count, events, event_count, frame_sequence};
  qa_caught_error error;
  bool ok = qa_try(prepare_run, &job, &error);
  c->busy = false;
  if (!ok) qa_rethrow(&error);
}
void qa_app_application_mod_presentations_close(collection *c) {
  if (c->closed) return;
  c->closed = true;
  cleanup_failures failures = {0};
  for (size_t g = 0; g < c->group_count; g++)
    while (c->groups[g].count > 0) attempt(&failures, remove_entry_run, take_entry(&c->groups[g], 0));
  while (c->group_count > 0) group_delete(c, c->group_count - 1);
  if (failures.count != 0) raise_failures(&failures, "Component presentation collection cleanup failed");
}
void qa_app_application_mod_presentations_destroy(collection *c) {
  if (c == NULL) return;
  qa_caught_error error;
  bool ok = qa_try(close_run, c, &error);
  free(c->groups);
  free(c);
  if (!ok) qa_rethrow(&error);
}
