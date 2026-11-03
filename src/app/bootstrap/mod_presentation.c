/* Port of src/app/bootstrap/mod-presentation.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/mod_presentation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "qa/app/bootstrap/component_scene.h"
#include "qa/app/bootstrap/q3_client/guest_collision.h"
#include "qa/app/bootstrap/q3_client/qvm_display.h"
#include "qa/audio/bank.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/client_audio_syscalls.h"
#include "qa/compat/qvm/client_cinematic_syscalls.h"
#include "qa/compat/qvm/client_collision_syscalls.h"
#include "qa/compat/qvm/client_mark_syscalls.h"
#include "qa/compat/qvm/client_render_syscalls.h"
#include "qa/compat/qvm/client_script_syscalls.h"
#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/memory.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/world_mark_projector.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/text/q3_font_registry.h"
#include "qa/ui/common/legacy/script/preprocessor.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/settings.h"

/* ---- shared helpers ---- */
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
/* node:os freemem(): libuv reads MemAvailable from /proc/meminfo. */
double qa_app_free_memory(void) {
  FILE *file = fopen("/proc/meminfo", "r");
  if (file != NULL) {
    char line[256];
    while (fgets(line, sizeof line, file) != NULL) {
      unsigned long long kilobytes;
      if (sscanf(line, "MemAvailable: %llu kB", &kilobytes) == 1) { fclose(file); return (double)kilobytes * 1024; }
    }
    fclose(file);
  }
  long pages = sysconf(_SC_AVPHYS_PAGES), size = sysconf(_SC_PAGESIZE);
  return pages < 0 || size < 0 ? 0 : (double)pages * (double)size;
}
double qa_app_world_leaf_count(const qa_world_scene *world) {
  switch (world->map.kind) {
    case QA_DECODED_WORLD_Q1: return (double)world->map.as.q1->leaf_count;
    case QA_DECODED_WORLD_Q2: return (double)world->map.as.q2->leaf_count;
    default: return (double)world->map.as.q3->leaf_count;
  }
}
/* CvarRegistry as QvmCvarServices (the TS registry satisfies the interface). */
static bool cvar_get(void *ctx, const char *name, qa_cvar_snapshot *out) { return qa_cvar_registry_get(ctx, name, out); }
static bool cvar_set(void *ctx, const char *name, const char *value, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set(ctx, name, value, force, out);
}
static bool cvar_set_value(void *ctx, const char *name, double value, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set_value(ctx, name, value, out);
}
static bool cvar_reset(void *ctx, const char *name, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_reset(ctx, name, force, out);
}
static bool cvar_register(void *ctx, const char *name, const char *value, bool has_flags, double flags, qa_cvar_snapshot *out) {
  return qa_cvar_registry_register(ctx, name, value, has_flags ? flags : 0, out);
}
static double cvar_bind_vm(void *ctx, const char *name, const char *value, double flags) {
  return (double)qa_cvar_registry_bind_vm(ctx, name, value, flags);
}
static bool cvar_read_vm(void *ctx, double handle, qa_cvar_snapshot *out) { return qa_cvar_registry_read_vm(ctx, handle, out); }
static char *cvar_info_string(void *ctx, double flags, double maximum_length) {
  return qa_cvar_registry_info_string(ctx, flags, maximum_length < 0 ? 1024 : (int64_t)maximum_length);
}
static qa_app_qvm_cvar_services registry_services(qa_cvar_registry *cvars) {
  qa_app_qvm_cvar_services services = {cvars, cvar_get, cvar_set, cvar_set_value, cvar_reset, cvar_register,
      cvar_bind_vm, cvar_read_vm, cvar_info_string};
  return services;
}
typedef struct cleanup_failures { size_t count; qa_caught_error first; } cleanup_failures;
static void attempt(cleanup_failures *failures, void (*fn)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (qa_try(fn, ctx, &caught)) return;
  if (failures->count++ == 0) failures->first = caught;
}

/* ---- owner ---- */
typedef struct qa_app_application_mod_presentation_storage {
  qa_app_component_body *bodies; size_t body_count;
  qa_app_component_body_part *parts;
  qa_app_body_material *materials;
} body_storage;

struct qa_app_application_mod_presentation {
  qa_app_mod_presentation_options options;
  qa_cvar_registry *cvars;
  double generation;
  qa_ui_script_global_defines *globals;
  qa_event_ring audio_operations; qa_event_cursor audio_cursor;
  qa_app_application_q3_assets *media;
  qa_app_application_q3_services *services;
  bool has_core; qa_qvm_mod_presentation core;
  qa_app_application_q3_scene_renderer *renderer;
  bool has_captured; qa_q3_pres_scene_content captured;
  qa_event_ring pending_hud; qa_event_cursor hud_cursor;
  qa_vec4 hud_color;
  qa_app_q3_overlay_submission *captured_hud; size_t captured_hud_count;
  body_storage captured_bodies;
  bool published;
  int operations;
  bool commands_published;
  double frame_sequence, frame_offset, milliseconds_offset;
  char **registered_commands; size_t registered_count, registered_capacity;
  bool has_previous_frame_time; double previous_frame_time;
  qa_qvm_files *files;
  qa_qvm_client_scripts *scripts;
  qa_source_clip_models *source_collision;
  qa_app_shared_qvm_client_clip_models *shared_collision;
  bool has_marks; qa_q3_pres_world_mark_projector marks;
  bool closed, initializing;
  bool has_scene_context; qa_qvm_scene_context scene_context;
  bool has_scene_baseline; qa_qvm_scene_context scene_baseline;
  bool has_scene_time_offset; double scene_time_offset;
  /* C storage the TS closures and GC keep alive. */
  qa_command_origin caller, origin;
  qa_collision_map_settings collision_settings;
};
typedef qa_app_application_mod_presentation owner_t;

static const qa_qvm_presentation_base *declaration_base(const qa_qvm_mod_presentation_declaration *declaration) {
  return declaration->is_scene ? &declaration->as.scene->base : &declaration->as.player_events->base;
}
static bool scene_runtime(const owner_t *p) { return p->options.source->prepared.declaration.is_scene; }
static bool has_hud(const owner_t *p) { return declaration_base(&p->options.source->prepared.declaration)->hud != NULL; }
static void print_text(void *ctx, const char *text) {
  owner_t *p = ctx;
  p->options.print(p->options.ctx, text);
}

static void assert_current(owner_t *p) {
  const qa_mod_qvm_presentation_source *source = &p->options.source->source;
  if (p->closed || source->generation != p->generation || !source->live(source->ctx, p->options.viewer))
    qa_raise("Error", 0, 0, "Component presentation belongs to a retired source or viewer");
  source->assert_current(source->ctx);
}
static void assert_current_ctx(void *ctx) { assert_current(ctx); }
bool qa_app_application_mod_presentation_owns(const owner_t *p, const qa_active_mod_presentation *source, qa_actor_id viewer) {
  const qa_active_mod_presentation *own = p->options.source;
  return !p->closed && source->source.ctx == own->source.ctx && source->prepared.artifact == own->prepared.artifact
      && qa_same_presentation_owner(&source->owner, own->owner)
      && source->source.generation == p->generation && qa_same_actor(viewer, p->options.viewer)
      && source->source.live(source->source.ctx, viewer);
}

const qa_app_mod_presentation_options *qa_app_application_mod_presentation_options(const owner_t *p) { return &p->options; }
qa_cvar_registry *qa_app_application_mod_presentation_cvars(owner_t *p) { return p->cvars; }
qa_app_application_q3_assets *qa_app_application_mod_presentation_media_assets(owner_t *p) {
  assert_current(p);
  if (p->media == NULL) qa_raise("Error", 0, 0, "Component presentation media are not initialized");
  return p->media;
}
qa_app_application_q3_services *qa_app_application_mod_presentation_services(owner_t *p) {
  assert_current(p);
  if (p->services == NULL) qa_raise("Error", 0, 0, "Component presentation services are not initialized");
  return p->services;
}
qa_app_application_q3_scene_renderer *qa_app_application_mod_presentation_renderer(owner_t *p) {
  assert_current(p);
  if (p->renderer == NULL) qa_raise("Error", 0, 0, "Component scene renderer is not initialized");
  return p->renderer;
}
qa_content_mounted *qa_app_application_mod_presentation_file_mounts(owner_t *p) {
  assert_current(p);
  const qa_mod_qvm_presentation_source *source = &p->options.source->source;
  qa_qvm_presentation_files files;
  if (source->files != NULL && source->files(source->ctx, &files) && files.mounts != NULL) return files.mounts;
  return qa_app_application_q3_assets_provider(qa_app_application_mod_presentation_media_assets(p))->mounts;
}
const qa_app_component_body *qa_app_application_mod_presentation_bodies(owner_t *p, size_t *out_count) {
  assert_current(p);
  *out_count = p->captured_bodies.body_count;
  return p->captured_bodies.bodies;
}
const qa_app_q3_overlay_submission *qa_app_application_mod_presentation_hud(owner_t *p, size_t *out_count) {
  assert_current(p);
  *out_count = p->captured_hud_count;
  return p->captured_hud;
}

/* TS context(): the source context completed with frame time, view and scene. */
static void scene_context_release(owner_t *p) {
  if (!p->has_scene_context) return;
  if (p->has_scene_baseline && p->scene_context.baseline == &p->scene_baseline) p->scene_context.baseline = NULL;
  qa_qvm_scene_context_free(&p->scene_context);
  p->has_scene_context = false;
}
static void context(owner_t *p, qa_qvm_presentation_context *out) {
  assert_current(p);
  const qa_mod_qvm_presentation_source *source = &p->options.source->source;
  const qa_qvm_presentation_context *current = source->context(source->ctx, p->options.viewer);
  if (current == NULL) qa_raise("Error", 0, 0, "Component presentation viewer has no original source context");
  double server_time = (double)current->snapshot_server_time;
  double frame_time = p->has_previous_frame_time ? server_time - p->previous_frame_time : 0;
  if (frame_time < 0 && !scene_runtime(p)) qa_raise("Error", 0, 0, "Component presentation source time moved backward");
  *out = *current;
  if (scene_runtime(p)) {
    qa_qvm_scene_pair publication;
    bool has_publication = current->scene == NULL && source->scene != NULL && source->scene(source->ctx, &publication);
    if (current->scene == NULL && !has_publication)
      qa_raise("Error", 0, 0, "Scene cgame requires an admitted gameplay entity snapshot source");
    if (current->scene != NULL) {
      qa_qvm_scene_context copy;
      qa_app_copy_component_scene(current->scene, &copy);
      scene_context_release(p);
      p->scene_context = copy; p->has_scene_context = true;
    } else if (!p->has_scene_context || (double)p->scene_context.revision != publication.current.revision) {
      double leaves = qa_app_world_leaf_count(qa_app_application_assets_world(p->options.assets));
      if (!p->has_scene_baseline && publication.baseline != NULL) {
        qa_app_select_component_scene(publication.baseline, p->options.viewer, p->options.queries, leaves, print_text, p,
            &p->scene_baseline);
        p->has_scene_baseline = true;
      }
      qa_qvm_scene_context selected;
      qa_app_select_component_scene(&publication.current, p->options.viewer, p->options.queries, leaves, print_text, p, &selected);
      scene_context_release(p);
      p->scene_context = selected; p->has_scene_context = true;
      if (p->has_scene_baseline) p->scene_context.baseline = &p->scene_baseline;
    }
    if (!p->has_scene_context) qa_raise("Error", 0, 0, "Component scene context is unavailable");
    double scene_time = p->scene_context.snapshot.server_time;
    if (!p->has_scene_time_offset) {
      p->scene_time_offset = scene_time - p->options.clock.now(p->options.clock.ctx);
      p->has_scene_time_offset = true;
    }
    double now = p->options.clock.now(p->options.clock.ctx);
    double time = trunc(qa_js_max(qa_js_max(scene_time, p->has_previous_frame_time ? p->previous_frame_time : scene_time),
                                  now + p->scene_time_offset));
    out->has_time = true; out->time_milliseconds = (int64_t)time;
    out->frame_time_milliseconds = qa_js_to_int32(p->has_previous_frame_time ? time - p->previous_frame_time : 0);
    out->scene = &p->scene_context;
  } else {
    out->frame_time_milliseconds = qa_js_to_int32(frame_time);
  }
  out->view_origin = p->options.view_origin(p->options.ctx);
  out->has_view_axis = p->options.view_axis != NULL;
  if (out->has_view_axis) out->view_axis = p->options.view_axis(p->options.ctx);
}
static void core_context(void *ctx, qa_qvm_presentation_context *out) { context(ctx, out); }
static bool core_actor(void *ctx, int64_t slot, qa_actor_id *out) {
  owner_t *p = ctx;
  return p->options.source->source.actor(p->options.source->source.ctx, (double)slot, out);
}
static bool core_live(void *ctx, qa_actor_id actor) {
  owner_t *p = ctx;
  return p->options.source->source.live(p->options.source->source.ctx, actor);
}

/* ---- host ---- */
static bool owns_current(void *ctx) {
  owner_t *p = ctx;
  return qa_app_application_mod_presentation_owns(p, p->options.source, p->options.viewer);
}
static void register_name(owner_t *p, const char *name) {
  for (size_t i = 0; i < p->registered_count; i++) if (strcmp(p->registered_commands[i], name) == 0) return;
  if (p->registered_count == p->registered_capacity) {
    p->registered_capacity = p->registered_capacity == 0 ? 8 : p->registered_capacity * 2;
    char **next = realloc(p->registered_commands, p->registered_capacity * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    p->registered_commands = next;
  }
  p->registered_commands[p->registered_count] = strdup(name);
  if (p->registered_commands[p->registered_count] == NULL) qa_range_error("Out of memory");
  p->registered_count++;
}
static void unregister_name(owner_t *p, const char *name) {
  for (size_t i = 0; i < p->registered_count; i++) if (strcmp(p->registered_commands[i], name) == 0) {
    free(p->registered_commands[i]);
    memmove(&p->registered_commands[i], &p->registered_commands[i + 1], (p->registered_count - i - 1) * sizeof(char *));
    p->registered_count--;
    return;
  }
}
/* commandHost = options.commands ?? { every member rejects the call }. */
typedef struct host_frame { owner_t *p; const qa_qvm_host_call *call; } host_frame;
static void command_append(void *ctx, const char *text) {
  host_frame *f = ctx;
  if (!f->p->options.has_commands) qa_qvm_reject_syscall(f->call);
  f->p->options.commands.append(f->p->options.commands.ctx, text);
}
static void command_reliable(void *ctx, const char *text) {
  host_frame *f = ctx;
  if (!f->p->options.has_commands) qa_qvm_reject_syscall(f->call);
  f->p->options.commands.reliable(f->p->options.commands.ctx, text);
}
static void command_register(void *ctx, const char *name) {
  host_frame *f = ctx;
  if (!f->p->options.has_commands) qa_qvm_reject_syscall(f->call);
  f->p->options.commands.register_command(f->p->options.commands.ctx, name);
  register_name(f->p, name);
}
static void command_remove(void *ctx, const char *name) {
  host_frame *f = ctx;
  if (!f->p->options.has_commands) qa_qvm_reject_syscall(f->call);
  f->p->options.commands.remove_command(f->p->options.commands.ctx, name);
  unregister_name(f->p, name);
}
static double common_milliseconds(void *ctx) {
  host_frame *f = ctx;
  return f->p->options.clock.now(f->p->options.clock.ctx) + f->p->milliseconds_offset;
}
static const char *const *common_arguments(void *ctx, size_t *out_count) {
  host_frame *f = ctx;
  if (!f->p->has_core) { *out_count = 0; return NULL; }
  return qa_qvm_mod_presentation_arguments(&f->p->core, out_count);
}
static void common_print(void *ctx, const char *text) {
  host_frame *f = ctx;
  f->p->options.print(f->p->options.ctx, text);
}
static void display_viewport(void *ctx, double *out_width, double *out_height) {
  owner_t *p = ctx;
  *out_width = p->options.viewport.width; *out_height = p->options.viewport.height;
}
static void developer_print(void *ctx, const char *text) {
  owner_t *p = ctx;
  qa_cvar_snapshot developer;
  double value = 0;
  if (qa_cvar_registry_get(p->cvars, "developer", &developer)) { value = developer.integer_value; qa_cvar_read_free(&developer); }
  if (value != 0) p->options.print(p->options.ctx, text);
}
/* ApplicationQ3Cinematics as QvmClientCinematics. */
static double cinematic_play(void *ctx, const char *path, qa_rect rect, double bits) {
  return qa_app_application_q3_cinematics_play_guest(ctx, path, rect, bits);
}
static double cinematic_run(void *ctx, double handle) { return qa_app_application_q3_cinematics_run_guest(ctx, handle); }
static double cinematic_stop(void *ctx, double handle) { return qa_app_application_q3_cinematics_stop_guest(ctx, handle); }
static void cinematic_draw(void *ctx, double handle, const qa_draw2d *draw) {
  qa_app_application_q3_cinematics_draw_guest(ctx, handle, (qa_draw2d *)draw);
}
static void cinematic_extents(void *ctx, double handle, qa_rect rect) { qa_app_application_q3_cinematics_set_extents(ctx, handle, rect); }
static qa_qvm_client_clip_models collision_models(void *ctx) {
  owner_t *p = ctx;
  if (p->source_collision == NULL && p->shared_collision == NULL) qa_raise("Error", 0, 0, "Component collision owner is closed");
  return p->shared_collision != NULL ? qa_app_shared_qvm_client_clip_models_as_client(p->shared_collision)
                                     : qa_qvm_source_client_clip_models(p->source_collision);
}
static void collision_load_map(void *ctx, const char *path) {
  owner_t *p = ctx;
  assert_current(p);
  const char *requested = qa_app_loaded_content_recipe(qa_app_application_assets_content(p->options.assets))->map.geometry.requested_path;
  if (strcmp(path, requested) != 0) qa_raise("Error", 0, 0, "Component requested a different collision map: %s", path);
}
static char *guest_string(const qa_qvm_host_call *call, int32_t pointer) {
  if (pointer == 0) { char *empty = strdup(""); if (empty == NULL) qa_range_error("Out of memory"); return empty; }
  char *text = qa_qvm_memory_read_string(call->guest, pointer);
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}
static void write_game_state(qa_qvm_memory *memory, int64_t address, const qa_q3_source_game_state_record *state) {
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(memory, address, QA_QVM_GAME_STATE_BYTES, &view);
  for (int i = 0; i < 1024; i++) qa_qvm_writable_view_set_int32(&view, i * 4, state->string_offsets[i], true);
  qa_qvm_memory_write_bytes(memory, address + 4096, state->string_data, 16000);
  qa_qvm_writable_view_set_int32(&view, 20096, (int32_t)state->data_count, true);
}
static qa_qvm_host_result host(void *ctx, const qa_qvm_host_call *call) {
  owner_t *p = ctx;
  assert_current(p);
  if (call->role == NULL || strcmp(call->role, "cgame") != 0 || p->files == NULL || p->scripts == NULL
      || (p->source_collision == NULL && p->shared_collision == NULL) || !p->has_marks)
    qa_qvm_reject_syscall(call);
  bool engine = strcmp(call->kind, "engine") == 0;
  if (engine && (call->code == QA_QVM_CGAME_IMPORT_CG_S_STARTBACKGROUNDTRACK || call->code == QA_QVM_CGAME_IMPORT_CG_S_STOPBACKGROUNDTRACK)) {
    if (p->options.presentation_media.deliver == NULL) qa_qvm_reject_syscall(call);
    qa_component_presentation_media_request request;
    memset(&request, 0, sizeof request);
    char *intro = NULL, *loop = NULL;
    if (call->code == QA_QVM_CGAME_IMPORT_CG_S_STOPBACKGROUNDTRACK) request.kind = QA_MEDIA_REQUEST_MUSIC_STOP;
    else {
      intro = guest_string(call, qa_qvm_host_call_word(call, 1));
      loop = guest_string(call, qa_qvm_host_call_word(call, 2));
      request.kind = QA_MEDIA_REQUEST_MUSIC; request.intro = intro; request.loop = loop;
    }
    qa_cleanup intro_node, loop_node;
    qa_cleanup_push(&intro_node, free, intro); qa_cleanup_push(&loop_node, free, loop);
    p->options.presentation_media.deliver(p->options.presentation_media.ctx, &request, p->initializing, owns_current, p);
    qa_cleanup_pop(&loop_node); qa_cleanup_pop(&intro_node);
    free(intro); free(loop);
    assert_current(p);
    return 0;
  }
  if (engine && call->code == QA_QVM_CGAME_IMPORT_CG_R_REMAP_SHADER) {
    if (p->options.presentation_media.deliver == NULL) qa_qvm_reject_syscall(call);
    char *original = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 1));
    char *replacement = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 2));
    char *offset_text = qa_qvm_memory_read_string(call->guest, qa_qvm_host_call_word(call, 3));
    if (original == NULL || replacement == NULL || offset_text == NULL) qa_range_error("Out of memory");
    double offset = qa_js_parse_float(offset_text);
    free(offset_text);
    qa_component_presentation_media_request request;
    memset(&request, 0, sizeof request);
    request.kind = QA_MEDIA_REQUEST_SHADER_REMAP; request.original = original; request.replacement = replacement;
    request.time_offset = isnan(offset) ? 0 : offset;
    qa_cleanup original_node, replacement_node;
    qa_cleanup_push(&original_node, free, original); qa_cleanup_push(&replacement_node, free, replacement);
    p->options.presentation_media.deliver(p->options.presentation_media.ctx, &request, p->initializing, owns_current, p);
    qa_cleanup_pop(&replacement_node); qa_cleanup_pop(&original_node);
    free(original); free(replacement);
    assert_current(p);
    return 0;
  }
  qa_app_application_q3_services *services = qa_app_application_mod_presentation_services(p);
  if (engine && call->code == QA_QVM_CGAME_IMPORT_CG_UPDATESCREEN) {
    p->options.next_frame(p->options.ctx);
    assert_current(p);
    return 0;
  }
  if (engine && call->code == QA_QVM_CGAME_IMPORT_CG_GETGAMESTATE) {
    qa_qvm_presentation_context current;
    int32_t address = qa_qvm_host_call_word(call, 1);
    context(p, &current);
    write_game_state(call->guest, address, &current.game_state);
    return 0;
  }
  host_frame frame = {p, call};
  qa_qvm_host_result result = 0;
  qa_qvm_common_services common;
  memset(&common, 0, sizeof common);
  common.role = "cgame"; common.cvars = p->cvars; common.ctx = &frame;
  common.print = common_print; common.milliseconds = common_milliseconds; common.arguments = common_arguments;
  common.commands.ctx = &frame; common.commands.append = command_append; common.commands.reliable = command_reliable;
  common.commands.register_command = command_register; common.commands.remove_command = command_remove;
  if (qa_qvm_common_syscall(call, common, &result)) return result;
  if (p->options.renderer != NULL) {
    qa_app_qvm_display_options display = {p->options.renderer->backend_stencil_bits, p->options.renderer->driver,
        p->options.renderer->gl_config, p->media, qa_app_application_q3_services_resources(services), p, display_viewport,
        p, assert_current_ctx};
    if (qa_app_qvm_display_syscall(call, display, &result)) return result;
  }
  qa_app_application_q3_cinematics *cinematics = qa_app_application_q3_services_cinematics(services);
  qa_qvm_client_cinematic_services cinematic = {{cinematics, cinematic_play, cinematic_run, cinematic_stop, cinematic_draw,
      cinematic_extents}, qa_app_application_q3_services_draw(services), p, developer_print};
  if (qa_qvm_client_cinematic_syscall(call, cinematic, &result)) return result;
  if (qa_qvm_file_syscall(call, p->files, &result)) return result;
  if (qa_qvm_client_script_syscall(call, p->scripts, &result)) return result;
  if (qa_qvm_client_render_syscall(call, qa_app_application_q3_services_resources(services),
      qa_app_application_q3_services_draw(services), &result)) return result;
  qa_qvm_client_audio_services audio = {"cgame", qa_app_application_q3_services_sound(services), p, print_text};
  if (qa_qvm_client_audio_syscall(call, audio, &result)) return result;
  qa_qvm_client_collision_services collision = {p, collision_models, collision_load_map};
  if (qa_qvm_client_collision_syscall(call, collision, &result)) return result;
  if (qa_qvm_client_mark_syscall(call, qa_qvm_bsp_mark_projector(&p->marks.projector), &result)) return result;
  if (p->options.scalar != NULL && p->options.scalar(p->options.ctx, call, p, &result)) return result;
  if (engine && call->code == QA_QVM_CGAME_IMPORT_CG_MEMORY_REMAINING) {
    double free_memory = qa_app_free_memory();
    return qa_qvm_host_word(free_memory < 0x7fffffff ? free_memory : 0x7fffffff);
  }
  qa_qvm_reject_syscall(call);
}

/* ---- services output ---- */
static void reject_overlay(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
static void hud_admit(owner_t *p, const qa_app_q3_overlay_submission *submission) {
  if (qa_event_ring_admit(&p->pending_hud, submission) < 0) qa_range_error("Out of memory");
}
static void free_hud_submission(qa_app_q3_overlay_submission *submission) {
  if (submission->scene != NULL) {
    qa_q3_pres_presented_scene_destroy((qa_q3_pres_presented_scene *)submission->scene);
    free((void *)submission->scene);
  }
}
static void output_scene(void *ctx, qa_q3_pres_presented_scene *scene) {
  owner_t *p = ctx;
  assert_current(p);
  if (!has_hud(p)) { p->options.output.scene(p->options.output.ctx, scene); return; }
  if (((int64_t)scene->source.render_flags & QA_Q3_PRES_RDF_NOWORLDMODEL) == 0) {
    qa_q3_pres_presented_scene_destroy(scene);
    reject_overlay("Component HUD cannot replace the world view");
  }
  qa_q3_pres_presented_scene *owned = xcalloc(1, sizeof *owned);
  *owned = *scene;
  qa_app_q3_overlay_submission submission;
  memset(&submission, 0, sizeof submission);
  submission.kind = "scene"; submission.scene = owned;
  hud_admit(p, &submission);
}
static void output_command(void *ctx, const qa_render_command *command) {
  owner_t *p = ctx;
  assert_current(p);
  if (!has_hud(p)) { p->options.output.command(p->options.output.ctx, command); return; }
  if (command->kind != QA_RENDER_COMMAND_SET_COLOR && command->kind != QA_RENDER_COMMAND_STRETCH_PIC)
    reject_overlay("Component HUD cannot control the framebuffer");
  qa_app_q3_overlay_submission submission;
  memset(&submission, 0, sizeof submission);
  submission.kind = "command";
  if (command->kind == QA_RENDER_COMMAND_SET_COLOR) p->hud_color = command->color;
  else {
    submission.command.kind = QA_RENDER_COMMAND_SET_COLOR; submission.command.color = p->hud_color;
    hud_admit(p, &submission);
  }
  submission.command = *command;
  hud_admit(p, &submission);
}
static void output_text(void *ctx, const qa_material_text_draw *draw) {
  owner_t *p = ctx;
  assert_current(p);
  if (!has_hud(p)) { p->options.output.text(p->options.output.ctx, draw); return; }
  qa_app_q3_overlay_submission submission;
  memset(&submission, 0, sizeof submission);
  submission.kind = "text"; submission.draw = *draw;
  hud_admit(p, &submission);
}
static void output_listener(void *ctx, qa_vec3 origin, qa_axis axis) {
  owner_t *p = ctx;
  assert_current(p);
  p->options.output.listener(p->options.output.ctx, origin, axis);
}
static void output_audio(void *ctx, qa_app_q3_seat_audio_operation operation) {
  owner_t *p = ctx;
  assert_current(p);
  if (qa_event_ring_admit(&p->audio_operations, &operation) < 0) qa_range_error("Out of memory");
}
static bool services_actor_at(void *ctx, double slot, qa_actor_id *out) {
  owner_t *p = ctx;
  assert_current(p);
  return p->options.source->source.actor(p->options.source->source.ctx, slot, out);
}
static double services_now(void *ctx) {
  owner_t *p = ctx;
  return p->options.clock.now(p->options.clock.ctx) + p->milliseconds_offset;
}
static double services_frame(void *ctx) {
  owner_t *p = ctx;
  return p->options.clock.frame_number(p->options.clock.ctx);
}
static void reset_hud(owner_t *p) {
  size_t count = 0;
  qa_app_q3_overlay_submission *pending = qa_event_ring_take(&p->pending_hud, &p->hud_cursor, &count);
  for (size_t i = 0; i < count; i++) free_hud_submission(&pending[i]);
  free(pending);
  qa_event_ring_reset(&p->pending_hud);
}

static void flush_audio(owner_t *p) {
  if (qa_event_ring_pending(&p->audio_operations, &p->audio_cursor) == 0) return;
  assert_current(p);
  size_t count = 0;
  qa_app_q3_seat_audio_operation *operations = qa_event_ring_take(&p->audio_operations, &p->audio_cursor, &count);
  qa_event_ring_retire(&p->audio_operations);
  qa_app_q3_seat_audio_frame frame = {p->options.source->identity.source.content, p->options.seat, true,
      p->options.source->prepared.source.id, operations, count};
  qa_cleanup node;
  qa_cleanup_push(&node, free, operations);
  qa_app_application_audio_receive_cgame_frame(p->options.audio, &frame);
  qa_cleanup_pop(&node);
  free(operations);
}

/* ---- construction ---- */
static bool no_font_data(void *ctx) { (void)ctx; return false; }
/* Releases a constructed owner that never reached its TS try block. */
static void free_owner(owner_t *p) {
  qa_event_ring_close_cursor(&p->audio_operations, &p->audio_cursor);
  qa_event_ring_destroy(&p->audio_operations);
  qa_event_ring_close_cursor(&p->pending_hud, &p->hud_cursor);
  qa_event_ring_destroy(&p->pending_hud);
  if (p->globals != NULL) qa_ui_script_global_defines_destroy(p->globals);
  if (p->cvars != NULL) qa_cvar_registry_destroy(p->cvars);
  free(p);
}
static owner_t *construct(const qa_app_mod_presentation_options *options) {
  owner_t *p = xcalloc(1, sizeof *p);
  p->options = *options;
  p->published = true; p->commands_published = true; p->frame_sequence = -1; p->initializing = true;
  p->hud_color = (qa_vec4){1, 1, 1, 1};
  if (!qa_event_ring_init(&p->audio_operations, 4096, sizeof(qa_app_q3_seat_audio_operation))
      || !qa_event_ring_open_cursor(&p->audio_operations, &p->audio_cursor, true)
      || !qa_event_ring_init(&p->pending_hud, 4096, sizeof(qa_app_q3_overlay_submission))
      || !qa_event_ring_open_cursor(&p->pending_hud, &p->hud_cursor, true))
    qa_range_error("Out of memory");
  p->globals = qa_ui_script_global_defines_create(NULL, NULL, NULL);
  if (p->globals == NULL) qa_range_error("Out of memory");
  const qa_active_mod_presentation *source = options->source;
  p->generation = source->source.generation;
  p->caller.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  p->origin.kind = QA_COMMAND_ORIGIN_SCRIPT; p->origin.name = source->prepared.artifact->module.artifact_path;
  p->origin.caller = &p->caller;
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context.session = options->viewer.session; cvar_options.context.origin = p->origin;
  cvar_options.print = (qa_command_print_fn)print_text; cvar_options.print_ctx = p;
  p->cvars = qa_cvar_registry_create(&cvar_options);
  if (p->cvars == NULL) qa_range_error("Out of memory");
  qa_module_identity module = source->source.module, expected = source->prepared.source;
  if (strcmp(module.id, expected.id) != 0 || strcmp(module.artifact_path, expected.artifact_path) != 0
      || strcmp(module.digest, expected.digest) != 0 || strcmp(module.revision, expected.revision) != 0
      || strcmp(source->source.abi_profile, declaration_base(&source->prepared.declaration)->gameplay.abi_profile) != 0) {
    free_owner(p);
    qa_raise("Error", 0, 0, "Component presentation source differs from its prepared gameplay module");
  }
  return p;
}

typedef struct restore_input {
  const qa_save_value *value;
  qa_qvm_resolve_saved_actor_fn resolve; void *resolve_ctx;
} restore_input;
static void restore_owned(owner_t *p, const restore_input *checkpoint);
typedef struct create_job { owner_t *p; double baseline; const restore_input *checkpoint; } create_job;
static void create_run(void *ctx) {
  create_job *job = ctx;
  owner_t *p = job->p;
  const qa_app_mod_presentation_options *options = &p->options;
  const qa_active_mod_presentation *source = options->source;
  assert_current(p);
  if (source->prepared.declaration.is_scene)
    for (size_t i = 0; i < source->prepared.declaration.as.scene->cvar_count; i++) {
      qa_cvar_snapshot set;
      const qa_qvm_presentation_cvar *variable = &source->prepared.declaration.as.scene->cvars[i];
      if (qa_cvar_registry_set(p->cvars, variable->name, variable->value, false, &set)) qa_cvar_read_free(&set);
    }
  p->media = qa_app_application_q3_assets_create_from_application(options->assets, source->identity.source.content,
      print_text, p, no_font_data, p, "guest-async", "source");
  if (p->media == NULL) qa_range_error("Out of memory");
  assert_current(p);
  qa_collision_map_settings_init(&p->collision_settings, p->cvars);
  qa_collision_map_settings_register_map(&p->collision_settings);
  qa_source_clip_models *native = qa_shared_native_q3_clip_models(options->queries);
  if (native == NULL) {
    p->shared_collision = qa_app_shared_qvm_client_clip_models_create(options->queries, registry_services(p->cvars));
    if (p->shared_collision == NULL) qa_range_error("Out of memory");
  } else {
    p->source_collision = qa_source_clip_models_create(native->world, "private");
    if (p->source_collision == NULL) qa_range_error("Out of memory");
  }
  qa_q3_pres_world_mark_projector_init(&p->marks, qa_app_application_assets_world(options->assets));
  p->has_marks = true;
  qa_app_application_q3_service_options services;
  memset(&services, 0, sizeof services);
  services.collision_settings = &p->collision_settings; services.media = p->media;
  services.world = options->hosts.world; services.audio = options->hosts.audio;
  services.has_owner = true; services.owner = source->prepared.source.id; services.resource_handles = "client";
  services.system_cinematics = options->system_cinematics.value != NULL ? options->system_cinematics.value
      : options->system_cinematics.of != NULL ? options->system_cinematics.of(options->system_cinematics.ctx, p) : NULL;
  services.seat = options->seat; services.viewport = &p->options.viewport;
  services.queries = qa_shared_as_scene_queries(options->queries); services.shared_queries = options->queries;
  services.actor_at = services_actor_at; services.actor_ctx = p;
  services.now = services_now; services.now_ctx = p; services.frame_number = services_frame; services.frame_ctx = p;
  services.output_ctx = p; services.output_scene = output_scene; services.output_command = output_command;
  services.output_text = output_text; services.output_listener = output_listener; services.output_audio = output_audio;
  p->services = qa_app_create_application_q3_services(services);
  assert_current(p);
  p->renderer = qa_app_application_q3_scene_renderer_create(p->media,
      qa_app_application_q3_services_resources(p->services));
  qa_qvm_presentation_files source_files;
  bool has_files = source->source.files != NULL && source->source.files(source->source.ctx, &source_files);
  qa_content_mounted *mounts = has_files && source_files.mounts != NULL ? source_files.mounts
      : qa_app_application_q3_assets_provider(p->media)->mounts;
  qa_qvm_file_services files = {mounts, has_files ? source_files.writable : NULL, p, print_text, assert_current_ctx};
  p->files = qa_qvm_files_create(files);
  qa_qvm_client_script_services scripts = {mounts, p->globals, p, assert_current_ctx, print_text};
  p->scripts = qa_qvm_client_scripts_create(scripts);
  if (p->files == NULL || p->scripts == NULL) qa_range_error("Out of memory");
  qa_qvm_mod_presentation_options core;
  memset(&core, 0, sizeof core);
  core.module_options.artifact = source->prepared.artifact;
  core.module_options.host = host; core.module_options.host_ctx = p;
  core.source = source->prepared.source;
  core.declaration = &source->prepared.declaration;
  core.ctx = p; core.context = core_context; core.actor = core_actor; core.live = core_live;
  core.assert_current = assert_current_ctx;
  qa_qvm_mod_presentation_init(&p->core, &core);
  p->has_core = true;
  if (job->checkpoint == NULL) qa_qvm_mod_presentation_initialize(&p->core, (int64_t)job->baseline);
  else restore_owned(p, job->checkpoint);
  assert_current(p);
  p->initializing = false;
  qa_q3_pres_scene_recorder_clear_scene(qa_app_application_q3_services_scene(p->services));
  reset_hud(p);
  flush_audio(p);
}
static void close_run(void *ctx) { qa_app_application_mod_presentation_close(ctx); }
static owner_t *create_owner(const qa_app_mod_presentation_options *options, double baseline, const restore_input *checkpoint) {
  owner_t *p = construct(options);
  qa_caught_error assert_error;
  if (!qa_try(assert_current_ctx, p, &assert_error)) { free_owner(p); qa_rethrow(&assert_error); }
  if (checkpoint != NULL) p->published = false;
  create_job job = {p, baseline, checkpoint};
  qa_caught_error error;
  if (qa_try(create_run, &job, &error)) return p;
  qa_caught_error cleanup;
  bool closed = qa_try(close_run, p, &cleanup);
  qa_app_application_mod_presentation_destroy(p);
  if (!closed)
    qa_raise("AggregateError", 0, 2, "Component presentation initialization and cleanup failed (first %s: %s; cleanup %s: %s)",
             error.name, error.message, cleanup.name, cleanup.message);
  qa_rethrow(&error);
}
qa_app_application_mod_presentation *qa_app_application_mod_presentation_create(const qa_app_mod_presentation_options *options,
    double baseline_sequence) {
  return create_owner(options, baseline_sequence, NULL);
}
qa_app_application_mod_presentation *qa_app_application_mod_presentation_restore(const qa_app_mod_presentation_options *options,
    const qa_save_value *value, qa_qvm_resolve_saved_actor_fn resolve_actor, void *resolve_ctx) {
  restore_input checkpoint = {value, resolve_actor, resolve_ctx};
  return create_owner(options, -1, &checkpoint);
}

/* ---- checkpoints ---- */
static qa_save_value *own(qa_save_value *value) { if (value == NULL) qa_range_error("Out of memory"); return value; }
static void set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("Out of memory");
}
static void push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child)) qa_range_error("Out of memory");
}
static qa_save_value *numbers(qa_double_view view) {
  qa_save_value *array = own(qa_save_value_array());
  for (size_t i = 0; i < view.count; i++) push(array, qa_save_value_number(view.values[i]));
  return array;
}
static qa_save_value *program_value(const qa_qvm_presentation_program *program) {
  qa_save_value *value = own(qa_save_value_object());
  set(value, "path", qa_save_value_string(program->path));
  set(value, "digest", qa_save_value_string(program->digest));
  set(value, "abiProfile", qa_save_value_string(program->abi_profile));
  return value;
}
static qa_save_value *calls_value(const qa_qvm_presentation_call *calls, size_t count) {
  qa_save_value *array = own(qa_save_value_array());
  for (size_t i = 0; i < count; i++) {
    qa_save_value *call = own(qa_save_value_object());
    set(call, "entry", qa_save_value_number(calls[i].entry));
    if (calls[i].when != NULL) set(call, "when", qa_save_value_string(calls[i].when));
    qa_save_value *arguments = own(qa_save_value_array());
    for (size_t a = 0; a < calls[i].argument_count; a++) {
      const qa_qvm_presentation_argument *argument = &calls[i].arguments[a];
      qa_save_value *row = own(qa_save_value_object());
      set(row, "kind", qa_save_value_string(argument->kind));
      set(row, "value", strcmp(argument->kind, "source") == 0 ? qa_save_value_string(argument->source_value)
                                                              : qa_save_value_number(argument->number_value));
      push(arguments, row);
    }
    set(call, "arguments", arguments);
    push(array, call);
  }
  return array;
}
static qa_save_value *entry_arg(double entry, double centity) {
  qa_save_value *value = own(qa_save_value_object());
  set(value, "entry", qa_save_value_number(entry));
  set(value, "centityArgument", qa_save_value_number(centity));
  return value;
}
/* The prepared QvmModPresentationDeclaration as a value, in the TS interface's
 * member order. */
qa_save_value *qa_app_qvm_presentation_declaration_value(const qa_qvm_mod_presentation_declaration *declaration) {
  const qa_qvm_presentation_base *base = declaration_base(declaration);
  qa_save_value *value = own(qa_save_value_object());
  set(value, "version", qa_save_value_number((double)base->version));
  set(value, "gameplay", program_value(&base->gameplay));
  set(value, "cgame", program_value(&base->cgame));
  set(value, "initialize", calls_value(base->initialize, base->initialize_count));
  set(value, "refresh", calls_value(base->refresh, base->refresh_count));
  set(value, "frame", calls_value(base->frame, base->frame_count));
  if (base->hud != NULL) {
    qa_save_value *hud = own(qa_save_value_object());
    set(hud, "mode", qa_save_value_string(base->hud->mode));
    set(hud, "frame", calls_value(base->hud->frame, base->hud->frame_count));
    set(value, "hud", hud);
  }
  qa_save_value *storage = own(qa_save_value_object());
  if (!declaration->is_scene) {
    const qa_qvm_player_event_presentation *events = declaration->as.player_events;
    set(value, "runtime", qa_save_value_string(events->runtime));
    const qa_qvm_player_event_storage *s = &events->storage;
    set(storage, "gameState", qa_save_value_number(s->game_state));
    set(storage, "playerState", qa_save_value_number(s->player_state));
    qa_save_value *snapshot = own(qa_save_value_object());
    set(snapshot, "kind", qa_save_value_string("synthetic-player-event"));
    set(snapshot, "address", qa_save_value_number(s->snapshot.address));
    set(snapshot, "pointers", numbers(s->snapshot.pointers));
    set(storage, "snapshot", snapshot);
    qa_save_value *centities = own(qa_save_value_object());
    set(centities, "address", qa_save_value_number(s->centities.address));
    set(centities, "stride", qa_save_value_number(s->centities.stride));
    set(centities, "capacity", qa_save_value_number(s->centities.capacity));
    set(centities, "state", qa_save_value_number(s->centities.state));
    set(centities, "origin", qa_save_value_number(s->centities.origin));
    set(storage, "centities", centities);
    set(storage, "time", numbers(s->time)); set(storage, "frameTime", numbers(s->frame_time));
    set(storage, "viewOrigin", numbers(s->view_origin));
    if (s->view_angles.count > 0) set(storage, "viewAngles", numbers(s->view_angles));
    if (s->view_axis.count > 0) set(storage, "viewAxis", numbers(s->view_axis));
    set(value, "storage", storage);
    set(value, "project", calls_value(events->project, events->project_count));
    set(value, "event", calls_value(&events->event, 1));
    return value;
  }
  const qa_qvm_scene_presentation *scene = declaration->as.scene;
  set(value, "runtime", qa_save_value_string(scene->runtime));
  qa_save_value *cvars = own(qa_save_value_array());
  for (size_t i = 0; i < scene->cvar_count; i++) {
    qa_save_value *row = own(qa_save_value_object());
    set(row, "name", qa_save_value_string(scene->cvars[i].name));
    set(row, "value", qa_save_value_string(scene->cvars[i].value));
    push(cvars, row);
  }
  set(value, "cvars", cvars);
  const qa_qvm_scene_storage *s = &scene->storage;
  set(storage, "gameState", qa_save_value_number(s->game_state));
  set(storage, "serverCommandSequence", qa_save_value_number(s->server_command_sequence));
  set(storage, "time", numbers(s->time)); set(storage, "frameTime", numbers(s->frame_time));
  set(storage, "viewOrigin", numbers(s->view_origin));
  if (s->view_angles.count > 0) set(storage, "viewAngles", numbers(s->view_angles));
  if (s->view_axis.count > 0) set(storage, "viewAxis", numbers(s->view_axis));
  qa_save_value *centities = own(qa_save_value_object());
  set(centities, "address", qa_save_value_number(s->centities.address));
  set(centities, "stride", qa_save_value_number(s->centities.stride));
  set(centities, "capacity", qa_save_value_number(s->centities.capacity));
  set(centities, "state", qa_save_value_number(s->centities.state));
  set(centities, "previousEvent", qa_save_value_number(s->centities.previous_event));
  set(centities, "snapshotTime", qa_save_value_number(s->centities.snapshot_time));
  set(storage, "centities", centities);
  set(value, "storage", storage);
  set(value, "snapshots", calls_value(scene->snapshots, scene->snapshot_count));
  set(value, "eventEntityType", qa_save_value_number(scene->event_entity_type));
  set(value, "eventCheck", entry_arg(scene->event_check.entry, scene->event_check.centity_argument));
  qa_save_value *body = own(qa_save_value_object());
  set(body, "player", entry_arg(scene->body.player.entry, scene->body.player.centity_argument));
  qa_save_value *mesh = own(qa_save_value_object());
  set(mesh, "entry", qa_save_value_number(scene->body.mesh.entry));
  set(mesh, "entityArgument", qa_save_value_number(scene->body.mesh.entity_argument));
  set(mesh, "stateArgument", qa_save_value_number(scene->body.mesh.state_argument));
  set(mesh, "shaderOffset", qa_save_value_number(scene->body.mesh.shader_offset));
  if (scene->body.mesh.parts != NULL) {
    qa_save_value *parts = own(qa_save_value_array());
    for (size_t i = 0; i < scene->body.mesh.part_count; i++) {
      qa_save_value *row = own(qa_save_value_object());
      set(row, "call", qa_save_value_number(scene->body.mesh.parts[i].call));
      set(row, "part", qa_save_value_string(scene->body.mesh.parts[i].part));
      push(parts, row);
    }
    set(mesh, "parts", parts);
  }
  set(body, "mesh", mesh);
  set(value, "body", body);
  return value;
}
static bool hud_pending(owner_t *p) { return qa_event_ring_pending(&p->pending_hud, &p->hud_cursor) != 0; }
qa_save_value *qa_app_application_mod_presentation_capture_checkpoint(owner_t *p) {
  assert_current(p);
  if (p->initializing || p->operations != 0 || !p->has_core || p->files == NULL || p->scripts == NULL
      || (p->source_collision == NULL && p->shared_collision == NULL)
      || qa_event_ring_pending(&p->audio_operations, &p->audio_cursor) != 0 || hud_pending(p))
    qa_raise("Error", 0, 0, "Component checkpoint requires an idle completed frame");
  const qa_qvm_mod_presentation_declaration *declaration = &p->options.source->prepared.declaration;
  qa_qvm_abi_profile profile = declaration_base(declaration)->cgame.abi_profile;
  qa_save_value *value = own(qa_save_value_object());
  qa_cleanup node;
  qa_cleanup_push(&node, (void (*)(void *))qa_save_value_free, value);
  set(value, "version", qa_save_value_number(1));
  set(value, "declaration", qa_app_qvm_presentation_declaration_value(declaration));
  qa_guest_checkpoint core;
  qa_qvm_mod_presentation_capture_checkpoint(&p->core, &core);
  set(value, "core", qa_save_guest_checkpoint_value(&core));
  set(value, "cvars", qa_cvar_registry_capture_save_state(p->cvars));
  set(value, "files", qa_qvm_files_checkpoint_value(p->files));
  set(value, "scripts", qa_qvm_client_scripts_capture(p->scripts));
  qa_app_application_q3_services *services = qa_app_application_mod_presentation_services(p);
  qa_app_application_q3_assets *media = qa_app_application_mod_presentation_media_assets(p);
  set(value, "resources", qa_q3_pres_q3_renderer_resources_capture_checkpoint(qa_app_application_q3_services_resources(services)));
  set(value, "sounds", qa_q3_pres_presentation_sound_bank_capture_checkpoint(qa_app_application_q3_assets_bank(media)));
  set(value, "fonts", qa_q3_renderer_font_registry_capture_checkpoint(qa_app_application_q3_assets_fonts(media)));
  set(value, "collision", p->shared_collision != NULL
      ? qa_app_shared_qvm_client_clip_models_capture_temporary_checkpoint(p->shared_collision)
      : qa_source_clip_capture_temporary_checkpoint(p->source_collision));
  set(value, "cinematics", qa_app_application_q3_cinematics_capture_checkpoint(qa_app_application_q3_services_cinematics(services)));
  qa_save_value *commands = own(qa_save_value_array());
  for (size_t i = 0; i < p->registered_count; i++) push(commands, qa_save_value_string(p->registered_commands[i]));
  set(value, "commands", commands);
  double now = p->options.clock.now(p->options.clock.ctx);
  set(value, "frameSequence", qa_save_value_number(p->frame_sequence));
  set(value, "frameOrdinal", qa_save_value_number(p->options.clock.frame_number(p->options.clock.ctx) + p->frame_offset));
  set(value, "previousFrameTime", p->has_previous_frame_time ? qa_save_value_number(p->previous_frame_time) : qa_save_value_null());
  set(value, "milliseconds", qa_save_value_number(now + p->milliseconds_offset));
  set(value, "sceneTime", p->has_scene_time_offset ? qa_save_value_number(p->options.clock.now(p->options.clock.ctx) + p->scene_time_offset)
                                                   : qa_save_value_null());
  qa_save_value *color = own(qa_save_value_object());
  set(color, "x", qa_save_value_number(p->hud_color.x)); set(color, "y", qa_save_value_number(p->hud_color.y));
  set(color, "z", qa_save_value_number(p->hud_color.z)); set(color, "w", qa_save_value_number(p->hud_color.w));
  set(value, "hudColor", color);
  set(value, "sceneContext", p->has_scene_context ? qa_qvm_capture_scene_context(&p->scene_context, profile) : qa_save_value_null());
  set(value, "sceneBaseline", p->has_scene_baseline ? qa_qvm_capture_scene_context(&p->scene_baseline, profile) : qa_save_value_null());
  qa_cleanup_pop(&node);
  return value;
}
static bool reader_field(const qa_save_reader *reader, const char *name, qa_save_reader *out) {
  if (!qa_save_reader_field(reader, name, out)) qa_range_error("Out of memory");
  return true;
}
static bool encoded_equal(const qa_save_value *left, const qa_save_value *right) {
  size_t left_length = 0, right_length = 0;
  uint8_t *a = qa_encode_checkpoint_value(left, &left_length), *b = qa_encode_checkpoint_value(right, &right_length);
  if (a == NULL || b == NULL) qa_range_error("Out of memory");
  bool equal = left_length == right_length && memcmp(a, b, left_length) == 0;
  free(a); free(b);
  return equal;
}
static void restore_owned(owner_t *p, const restore_input *checkpoint) {
  if (!p->has_core || p->files == NULL || p->scripts == NULL || (p->source_collision == NULL && p->shared_collision == NULL))
    qa_raise("Error", 0, 0, "Component host is not allocated");
  p->commands_published = false;
  qa_save_reader r, field;
  if (!qa_save_reader_init(&r, checkpoint->value, "component-presentation")) qa_range_error("Out of memory");
  reader_field(&r, "version", &field); qa_save_reader_literal_number(&field, 1);
  const qa_qvm_mod_presentation_declaration *declaration = &p->options.source->prepared.declaration;
  reader_field(&r, "declaration", &field);
  qa_save_value *expected = qa_app_qvm_presentation_declaration_value(declaration);
  bool same = field.value != NULL && encoded_equal(field.value, expected);
  qa_save_value_free(expected);
  if (!same) qa_save_reader_fail(&field, "component declaration changed");
  qa_qvm_abi_profile profile = declaration_base(declaration)->cgame.abi_profile;
  reader_field(&r, "frameSequence", &field); p->frame_sequence = qa_save_reader_integer_min(&field, -1);
  reader_field(&r, "frameOrdinal", &field);
  p->frame_offset = qa_save_reader_integer_min(&field, 0) - p->options.clock.frame_number(p->options.clock.ctx);
  reader_field(&r, "previousFrameTime", &field);
  p->has_previous_frame_time = !qa_save_reader_is_null(&field);
  if (p->has_previous_frame_time) p->previous_frame_time = qa_save_reader_integer_min(&field, 0);
  reader_field(&r, "milliseconds", &field);
  p->milliseconds_offset = qa_save_reader_finite(&field) - p->options.clock.now(p->options.clock.ctx);
  reader_field(&r, "sceneTime", &field);
  p->has_scene_time_offset = !qa_save_reader_is_null(&field);
  if (p->has_scene_time_offset) p->scene_time_offset = qa_save_reader_finite(&field) - p->options.clock.now(p->options.clock.ctx);
  qa_save_reader color, part;
  reader_field(&r, "hudColor", &color);
  reader_field(&color, "x", &part); p->hud_color.x = (float)qa_save_reader_number(&part);
  reader_field(&color, "y", &part); p->hud_color.y = (float)qa_save_reader_number(&part);
  reader_field(&color, "z", &part); p->hud_color.z = (float)qa_save_reader_number(&part);
  reader_field(&color, "w", &part); p->hud_color.w = (float)qa_save_reader_number(&part);
  reader_field(&r, "sceneContext", &field);
  if (!qa_save_reader_is_null(&field)) {
    qa_qvm_scene_context read;
    qa_qvm_read_scene_context(&field, profile, checkpoint->resolve, checkpoint->resolve_ctx, &read);
    qa_app_copy_component_scene(&read, &p->scene_context);
    qa_qvm_scene_context_free(&read);
    p->has_scene_context = true;
  }
  reader_field(&r, "sceneBaseline", &field);
  if (!qa_save_reader_is_null(&field)) {
    qa_qvm_scene_context read;
    qa_qvm_read_scene_context(&field, profile, checkpoint->resolve, checkpoint->resolve_ctx, &read);
    qa_app_copy_component_scene(&read, &p->scene_baseline);
    qa_qvm_scene_context_free(&read);
    p->has_scene_baseline = true;
  }
  reader_field(&r, "cvars", &field); qa_cvar_registry_restore_save_state(p->cvars, field.value);
  qa_app_application_q3_assets *media = qa_app_application_mod_presentation_media_assets(p);
  qa_app_application_q3_services *services = qa_app_application_mod_presentation_services(p);
  reader_field(&r, "fonts", &field);
  if (!qa_q3_renderer_font_registry_restore_checkpoint(qa_app_application_q3_assets_fonts(media), field.value)) qa_range_error("Out of memory");
  assert_current(p);
  reader_field(&r, "resources", &field);
  if (!qa_q3_pres_q3_renderer_resources_restore_checkpoint(qa_app_application_q3_services_resources(services), field.value))
    qa_range_error("Out of memory");
  assert_current(p);
  reader_field(&r, "sounds", &field);
  if (!qa_q3_pres_presentation_sound_bank_restore_checkpoint(qa_app_application_q3_assets_bank(media), field.value))
    qa_range_error("Out of memory");
  assert_current(p);
  reader_field(&r, "files", &field); qa_qvm_files_restore_checkpoint(p->files, field.value);
  reader_field(&r, "scripts", &field); qa_qvm_client_scripts_restore(p->scripts, field.value);
  reader_field(&r, "collision", &field);
  if (p->shared_collision != NULL) qa_app_shared_qvm_client_clip_models_restore_temporary_checkpoint(p->shared_collision, field.value);
  else qa_source_clip_restore_temporary_checkpoint(p->source_collision, field.value);
  reader_field(&r, "cinematics", &field);
  qa_app_application_q3_cinematics_restore_checkpoint(qa_app_application_q3_services_cinematics(services), field.value);
  assert_current(p);
  reader_field(&r, "core", &field);
  qa_qvm_mod_presentation_restore_checkpoint(&p->core, field.value, checkpoint->resolve, checkpoint->resolve_ctx);
  reader_field(&r, "commands", &field);
  size_t count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&field, i, &item)) qa_range_error("Out of memory");
    const char *name = qa_save_reader_string(&item);
    for (size_t j = 0; j < i; j++) {
      qa_save_reader earlier;
      if (!qa_save_reader_item(&field, j, &earlier)) qa_range_error("Out of memory");
      if (strcmp(qa_save_reader_string(&earlier), name) == 0) qa_save_reader_fail(&r, "duplicate component command registration");
    }
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item;
    if (!qa_save_reader_item(&field, i, &item)) qa_range_error("Out of memory");
    register_name(p, qa_save_reader_string(&item));
  }
}

void qa_app_application_mod_presentation_publish_commands(owner_t *p) {
  assert_current(p);
  if (p->initializing) qa_raise("Error", 0, 0, "Cannot publish an initializing component");
  if (p->commands_published) return;
  if (!p->options.has_commands && p->registered_count != 0)
    qa_raise("Error", 0, 0, "Restored component commands have no destination registry");
  for (size_t i = 0; i < p->registered_count; i++)
    if (p->options.has_commands) p->options.commands.register_command(p->options.commands.ctx, p->registered_commands[i]);
  p->commands_published = true; p->published = true;
  qa_app_application_q3_cinematics_publish_restored(qa_app_application_q3_services_cinematics(qa_app_application_mod_presentation_services(p)));
}

/* ---- operations ---- */
static void fail_closed(owner_t *p, const qa_caught_error *error, const char *message) {
  qa_caught_error cleanup;
  if (!qa_try(close_run, p, &cleanup))
    qa_raise("AggregateError", 0, 2, "%s (first %s: %s; cleanup %s: %s)", message, error->name, error->message,
             cleanup.name, cleanup.message);
  qa_rethrow(error);
}
typedef struct command_job { owner_t *p; const char *const *arguments; size_t count; bool result; } command_job;
static void command_run(void *ctx) {
  command_job *job = ctx;
  job->result = qa_qvm_mod_presentation_console_command(&job->p->core, job->arguments, job->count);
  assert_current(job->p);
  flush_audio(job->p);
}
bool qa_app_application_mod_presentation_command(owner_t *p, const char *const *arguments, size_t count) {
  if (!p->published) qa_raise("Error", 0, 0, "Component presentation has not been published");
  assert_current(p);
  if (!p->has_core) qa_raise("Error", 0, 0, "Component presentation has not initialized");
  p->operations++;
  command_job job = {p, arguments, count, false};
  qa_caught_error error;
  bool ok = qa_try(command_run, &job, &error);
  p->operations--;
  if (!ok) fail_closed(p, &error, "Component command and cleanup failed");
  return job.result;
}
typedef struct consume_job { owner_t *p; const qa_app_q3_source_player_event *event; double sequence; } consume_job;
static void consume_run(void *ctx) {
  consume_job *job = ctx;
  owner_t *p = job->p;
  qa_qvm_mod_presentation_consume(&p->core, job->event, (int64_t)job->sequence);
  assert_current(p);
  if (p->options.source->source.live(p->options.source->source.ctx, job->event->actor)) flush_audio(p);
  else qa_event_ring_reset(&p->audio_operations);
}
void qa_app_application_mod_presentation_consume(owner_t *p, const qa_app_q3_source_player_event *event, double sequence) {
  if (!p->published) qa_raise("Error", 0, 0, "Component presentation has not been published");
  assert_current(p);
  if (!p->has_core) qa_raise("Error", 0, 0, "Component presentation has not initialized");
  p->operations++;
  consume_job job = {p, event, sequence};
  qa_caught_error error;
  bool ok = qa_try(consume_run, &job, &error);
  if (!ok) qa_event_ring_reset(&p->audio_operations);
  p->operations--;
  if (!ok) fail_closed(p, &error, "Component presentation execution and cleanup failed");
}
static void release_bodies(body_storage *storage) {
  free(storage->bodies); free(storage->parts); free(storage->materials);
  memset(storage, 0, sizeof *storage);
}
static void release_captured_hud(owner_t *p) {
  for (size_t i = 0; i < p->captured_hud_count; i++) free_hud_submission(&p->captured_hud[i]);
  free(p->captured_hud);
  p->captured_hud = NULL; p->captured_hud_count = 0;
}
/* TS core.bodies mapped to ComponentBody with resolved shader/skin handles. */
static body_storage capture_bodies(owner_t *p, double time) {
  size_t count = 0;
  const qa_qvm_body_presentation *bodies = qa_qvm_mod_presentation_bodies(&p->core, &count);
  body_storage storage;
  memset(&storage, 0, sizeof storage);
  size_t parts = 0, materials = 0;
  for (size_t b = 0; b < count; b++) {
    parts += bodies[b].part_count;
    for (size_t m = 0; m < bodies[b].part_count; m++) materials += bodies[b].parts[m].pass_count;
  }
  storage.bodies = xcalloc(count, sizeof *storage.bodies); storage.body_count = count;
  storage.parts = xcalloc(parts, sizeof *storage.parts);
  storage.materials = xcalloc(materials, sizeof *storage.materials);
  qa_q3_pres_q3_renderer_resources *resources = qa_app_application_q3_services_resources(p->services);
  size_t part_index = 0, material_index = 0;
  for (size_t b = 0; b < count; b++) {
    qa_app_component_body *body = &storage.bodies[b];
    body->owner = p->options.source->owner; body->actor = bodies[b].actor;
    body->content = p->options.source->identity.source.content; body->time = time;
    body->parts = &storage.parts[part_index]; body->part_count = bodies[b].part_count;
    for (size_t m = 0; m < bodies[b].part_count; m++) {
      const qa_qvm_body_mesh *mesh = &bodies[b].parts[m];
      qa_app_component_body_part *part = &storage.parts[part_index++];
      part->part = mesh->part; part->base = mesh->base;
      part->passes = &storage.materials[material_index]; part->pass_count = mesh->pass_count;
      for (size_t i = 0; i < mesh->pass_count; i++) {
        const qa_q3_pres_source_ref_entity_record *pass = &mesh->passes[i];
        qa_app_body_material *material = &storage.materials[material_index++];
        const qa_q3_pres_scene_shader *shader = pass->custom_shader.is_handle
            ? qa_q3_pres_q3_renderer_resources_shader_for_handle(resources, pass->custom_shader.handle) : pass->custom_shader.value.shader;
        const qa_q3_pres_scene_skin *skin = pass->custom_skin.is_handle
            ? qa_q3_pres_q3_renderer_resources_skin_for_handle(resources, pass->custom_skin.handle) : pass->custom_skin.value.skin;
        material->has_custom_shader = shader != NULL; material->custom_shader_name = shader != NULL ? shader->name : NULL;
        material->has_custom_skin = skin != NULL; material->custom_skin_surfaces = skin != NULL ? skin->path : NULL;
        material->custom_skin = skin;
        material->shader_rgba = pass->shader_rgba; material->shader_tex_coord = pass->shader_tex_coord;
        material->shader_time = pass->shader_time; material->render_flags = pass->render_flags;
        material->lighting_origin = pass->lighting_origin; material->shadow_plane = pass->shadow_plane;
        material->non_normalized_axes = pass->non_normalized_axes;
      }
    }
  }
  return storage;
}
typedef struct frame_job { owner_t *p; double sequence; const qa_q3_pres_scene_content *result; } frame_job;
static void frame_run(void *ctx) {
  frame_job *job = ctx;
  owner_t *p = job->p;
  qa_qvm_presentation_context current;
  context(p, &current);
  double time = current.has_time ? (double)current.time_milliseconds : (double)current.snapshot_server_time;
  qa_qvm_mod_presentation_advance(&p->core, (int64_t)job->sequence);
  assert_current(p);
  body_storage bodies = capture_bodies(p, time);
  qa_cleanup bodies_node;
  qa_cleanup_push(&bodies_node, (void (*)(void *))release_bodies, &bodies);
  qa_q3_pres_scene_recorder *recorder = qa_app_application_q3_services_scene(p->services);
  qa_q3_pres_scene_content scene;
  memset(&scene, 0, sizeof scene);
  if (!qa_q3_pres_scene_recorder_capture(recorder, &scene)) qa_range_error("Out of memory");
  qa_cleanup scene_node;
  qa_cleanup_push(&scene_node, (void (*)(void *))qa_q3_pres_scene_content_destroy, &scene);
  qa_q3_pres_scene_recorder_clear_scene(recorder);
  if (has_hud(p)) qa_qvm_mod_presentation_draw_hud(&p->core, (int64_t)job->sequence);
  assert_current(p);
  size_t hud_count = 0;
  qa_app_q3_overlay_submission *hud = qa_event_ring_take(&p->pending_hud, &p->hud_cursor, &hud_count);
  qa_event_ring_retire(&p->pending_hud);
  qa_q3_pres_scene_recorder_clear_scene(recorder);
  const qa_q3_pres_scene_content **contents = xcalloc(hud_count + 1, sizeof *contents);
  size_t content_count = 0;
  contents[content_count++] = &scene;
  for (size_t i = 0; i < hud_count; i++) if (hud[i].scene != NULL) contents[content_count++] = &hud[i].scene->content;
  qa_app_application_q3_scene_renderer_preload(qa_app_application_mod_presentation_renderer(p), contents, content_count);
  free(contents);
  assert_current(p);
  flush_audio(p);
  qa_cleanup_pop(&scene_node);
  qa_cleanup_pop(&bodies_node);
  p->has_previous_frame_time = true; p->previous_frame_time = time; p->frame_sequence = job->sequence;
  if (p->has_captured) qa_q3_pres_scene_content_destroy(&p->captured);
  p->captured = scene; p->has_captured = true;
  release_captured_hud(p);
  p->captured_hud = hud; p->captured_hud_count = hud_count;
  release_bodies(&p->captured_bodies);
  p->captured_bodies = bodies;
  job->result = &p->captured;
}
const qa_q3_pres_scene_content *qa_app_application_mod_presentation_frame(owner_t *p, double sequence) {
  if (!p->published) qa_raise("Error", 0, 0, "Component presentation has not been published");
  assert_current(p);
  if (!(sequence == trunc(sequence) && fabs(sequence) <= 9007199254740991.0) || sequence < 0) qa_raise("Error", 0, 0, "Invalid component presentation frame sequence");
  sequence += p->frame_offset;
  if (sequence <= p->frame_sequence && p->has_captured) return &p->captured;
  if (!p->has_core) qa_raise("Error", 0, 0, "Component presentation has not initialized");
  p->operations++;
  frame_job job = {p, sequence, NULL};
  qa_caught_error error;
  bool ok = qa_try(frame_run, &job, &error);
  p->operations--;
  if (!ok) fail_closed(p, &error, "Component presentation frame and cleanup failed");
  return job.result;
}
double qa_app_application_mod_presentation_time(owner_t *p) {
  if (p->has_previous_frame_time) return p->previous_frame_time;
  qa_qvm_presentation_context current;
  context(p, &current);
  return (double)current.snapshot_server_time;
}

typedef struct close_step { owner_t *p; int step; } close_step;
static void close_step_run(void *ctx) {
  close_step *s = ctx;
  owner_t *p = s->p;
  switch (s->step) {
    case 0: if (p->has_core) qa_qvm_mod_presentation_close(&p->core); break;
    case 1: if (p->files != NULL) qa_qvm_files_close_all(p->files); break;
    case 2: if (p->scripts != NULL) qa_qvm_client_scripts_close_all(p->scripts); break;
    case 3: qa_ui_script_global_defines_clear(p->globals); break;
    case 4: if (p->services != NULL) qa_app_application_q3_cinematics_close(qa_app_application_q3_services_cinematics(p->services)); break;
    case 5: if (p->renderer != NULL) qa_app_application_q3_scene_renderer_close(p->renderer); break;
    case 6: if (p->media != NULL) qa_sound_bank_clear(qa_q3_pres_presentation_sound_bank_bank(qa_app_application_q3_assets_bank(p->media))); break;
    case 7: if (p->media != NULL) qa_app_application_q3_assets_close(p->media); break;
    default:
      if (p->published) {
        qa_app_q3_seat_audio_operation release;
        memset(&release, 0, sizeof release);
        release.kind = QA_APP_Q3_SEAT_AUDIO_RELEASE_OWNER;
        qa_app_q3_seat_audio_frame frame = {p->options.source->identity.source.content, p->options.seat, true,
            p->options.source->prepared.source.id, &release, 1};
        qa_app_application_audio_receive_cgame_frame(p->options.audio, &frame);
      }
  }
}
void qa_app_application_mod_presentation_close(owner_t *p) {
  if (p->closed) return;
  p->closed = true;
  qa_event_ring_reset(&p->audio_operations);
  reset_hud(p);
  release_captured_hud(p);
  release_bodies(&p->captured_bodies);
  cleanup_failures failures = {0};
  for (int step = 0; step < 9; step++) { close_step s = {p, step}; attempt(&failures, close_step_run, &s); }
  /* TS drops the references; C frees what they owned. */
  if (p->has_core) { qa_qvm_mod_presentation_destroy(&p->core); p->has_core = false; }
  if (p->files != NULL) { qa_qvm_files_destroy(p->files); p->files = NULL; }
  if (p->scripts != NULL) { qa_qvm_client_scripts_destroy(p->scripts); p->scripts = NULL; }
  if (p->services != NULL) { qa_app_application_q3_services_destroy(p->services); p->services = NULL; }
  if (p->media != NULL) { qa_app_application_q3_assets_destroy(p->media); p->media = NULL; }
  if (p->source_collision != NULL) { qa_source_clip_models_destroy(p->source_collision); p->source_collision = NULL; }
  if (p->shared_collision != NULL) { qa_app_shared_qvm_client_clip_models_destroy(p->shared_collision); p->shared_collision = NULL; }
  if (p->has_marks) { qa_q3_pres_world_mark_projector_destroy(&p->marks); p->has_marks = false; }
  if (p->renderer != NULL) { qa_app_application_q3_scene_renderer_destroy(p->renderer); p->renderer = NULL; }
  if (p->has_captured) { qa_q3_pres_scene_content_destroy(&p->captured); p->has_captured = false; }
  if (failures.count != 0)
    qa_raise("AggregateError", 0, (long long)failures.count, "Component presentation cleanup failed (%zu errors; first %s: %s)",
             failures.count, failures.first.name, failures.first.message);
}
void qa_app_application_mod_presentation_destroy(owner_t *p) {
  if (p == NULL) return;
  qa_caught_error error;
  bool ok = qa_try(close_run, p, &error);
  scene_context_release(p);
  if (p->has_scene_baseline) qa_qvm_scene_context_free(&p->scene_baseline);
  for (size_t i = 0; i < p->registered_count; i++) free(p->registered_commands[i]);
  free(p->registered_commands);
  qa_event_ring_close_cursor(&p->audio_operations, &p->audio_cursor);
  qa_event_ring_destroy(&p->audio_operations);
  qa_event_ring_close_cursor(&p->pending_hud, &p->hud_cursor);
  qa_event_ring_destroy(&p->pending_hud);
  if (p->globals != NULL) qa_ui_script_global_defines_destroy(p->globals);
  if (p->cvars != NULL) qa_cvar_registry_destroy(p->cvars);
  free(p);
  if (!ok) qa_rethrow(&error);
}
