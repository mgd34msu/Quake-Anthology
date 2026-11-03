/* Port of src/app/bootstrap/mod-presentation.ts: one original component
 * cgame instance for one live source generation and viewing seat
 * (ApplicationModPresentation). The TS ComponentPresentationMediaRequest
 * re-export is qa_component_presentation_media_request
 * (contracts/presentation.h).
 *
 * C accommodations:
 *  - Async is synchronous: awaited host calls (nextFrame, presentationMedia,
 *    media/services creation) run to completion inside the trap.
 *  - Errors raise through qa_raise with the TS class and message (Error,
 *    AggregateError); callers catch with qa_try.
 *  - The Q3 services take world and audio hosts where TS reads
 *    ApplicationAssets/ApplicationAudio directly; those lower-layer hosts
 *    ride qa_app_q3_presentation_hosts and the runtime fills them.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MOD_PRESENTATION_H
#define QA_APP_BOOTSTRAP_MOD_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/component_bodies.h"
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include "qa/app/bootstrap/q3_client/overlay.h"
#include "qa/app/bootstrap/q3_client/scene.h"
#include "qa/app/bootstrap/q3_client/services.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/mod_presentation.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/render.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"
#include "qa/text/draw2d.h"
#include "qa/world/collision/index.h"
#include "qa/world/session/mod_presentations.h"

/* TS ApplicationQ3ServiceOptions["clock"]. */
typedef struct qa_app_q3_service_clock {
  void *ctx;
  double (*now)(void *ctx);
  double (*frame_number)(void *ctx);
} qa_app_q3_service_clock;

/* Hosts the lower Q3 ports take where TS reads the application owners
 * directly: q3_client/services.h world and audio. */
typedef struct qa_app_q3_presentation_hosts {
  qa_app_q3_services_world world;
  qa_app_q3_application_audio audio;
} qa_app_q3_presentation_hosts;
/* The hosts over ApplicationAssets (world fog selections read live, the content world's map) and ApplicationAudio
 * (engine streams, music), as TS ApplicationQ3Services reads them (q3_presentation_hosts.c). Both are borrowed; the
 * fog view is kept by the assets' loaded content. */
qa_app_q3_presentation_hosts qa_app_application_assets_q3_presentation_hosts(qa_app_application_assets *assets,
    qa_app_application_audio *audio);

/* TS QvmDisplayOptions["renderer"] = Pick<NativeRenderer, "backend" |
 * "driver" | "glConfig">. */
typedef struct qa_app_qvm_display_renderer {
  double backend_stencil_bits;
  const qa_app_gl_driver *driver;    /* NULL is TS null */
  const qa_app_gl_config *gl_config; /* NULL is TS null */
} qa_app_qvm_display_renderer;

/* TS Omit<ApplicationQ3ServiceOptions["output"], "audio">. */
typedef struct qa_app_mod_presentation_output {
  void *ctx;
  void (*scene)(void *ctx, qa_q3_pres_presented_scene *scene);
  void (*command)(void *ctx, const qa_render_command *command);
  void (*text)(void *ctx, const qa_material_text_draw *draw);
  void (*listener)(void *ctx, qa_vec3 origin, qa_axis axis);
} qa_app_mod_presentation_output;

typedef struct qa_app_application_mod_presentation qa_app_application_mod_presentation;

/* TS systemCinematics?: a host, or a function of the owner. Both NULL is
 * TS undefined; value wins when set. */
typedef struct qa_app_mod_presentation_system_cinematics {
  const qa_app_system_cinematic_host *value;
  void *ctx;
  const qa_app_system_cinematic_host *(*of)(void *ctx, qa_app_application_mod_presentation *owner);
} qa_app_mod_presentation_system_cinematics;

/* TS presentationMedia?(request, initializing, current). deliver NULL is TS
 * undefined. */
typedef struct qa_app_mod_presentation_media {
  void *ctx;
  void (*deliver)(void *ctx, const qa_component_presentation_media_request *request, bool initializing,
                  bool (*current)(void *current_ctx), void *current_ctx);
} qa_app_mod_presentation_media;

/* TS ApplicationModPresentationOptions. Every pointer is borrowed and must
 * outlive the owner; source is read live (its generation and context). ctx
 * is the context of print, view_origin, view_axis, next_frame and scalar. */
typedef struct qa_app_mod_presentation_options {
  qa_app_application_assets *assets;
  qa_app_application_audio *audio;
  qa_shared_scene_queries *queries;
  qa_seat_id seat;
  qa_actor_id viewer;
  qa_rect viewport;
  const qa_app_qvm_display_renderer *renderer; /* NULL is TS undefined */
  const qa_active_mod_presentation *source;
  qa_app_q3_service_clock clock;
  qa_app_mod_presentation_output output;
  qa_app_mod_presentation_system_cinematics system_cinematics;
  bool has_commands; /* TS commands?: append, register, remove, reliable */
  qa_qvm_console_commands commands;
  qa_app_mod_presentation_media presentation_media;
  void *ctx;
  void (*print)(void *ctx, const char *text);
  qa_vec3 (*view_origin)(void *ctx);
  qa_axis (*view_axis)(void *ctx); /* NULL is TS undefined */
  void (*next_frame)(void *ctx);
  /* TS scalar?(call, owner): false is the TS null; NULL is TS undefined. */
  bool (*scalar)(void *ctx, const qa_qvm_host_call *call, qa_app_application_mod_presentation *owner,
                 qa_qvm_host_result *out);
  qa_app_q3_presentation_hosts hosts;
} qa_app_mod_presentation_options;

/* node:os freemem() (MemAvailable), for CG_MEMORY_REMAINING and the scene
 * cgame's memoryRemaining. */
double qa_app_free_memory(void);
/* TS world.map.leaves.length. */
double qa_app_world_leaf_count(const qa_world_scene *world);

/* A QvmModPresentationDeclaration as its saved value (TS interface member
 * order); owned tree. */
qa_save_value *qa_app_qvm_presentation_declaration_value(const qa_qvm_mod_presentation_declaration *declaration);

/* TS ApplicationModPresentation.create(options, baselineSequence = -1). */
qa_app_application_mod_presentation *qa_app_application_mod_presentation_create(
    const qa_app_mod_presentation_options *options, double baseline_sequence);
/* TS ApplicationModPresentation.restore(options, value, resolveActor). */
qa_app_application_mod_presentation *qa_app_application_mod_presentation_restore(
    const qa_app_mod_presentation_options *options, const qa_save_value *value,
    qa_qvm_resolve_saved_actor_fn resolve_actor, void *resolve_ctx);
/* Closes (TS close) and frees. A close failure raises after the free. */
void qa_app_application_mod_presentation_destroy(qa_app_application_mod_presentation *presentation);

const qa_app_mod_presentation_options *qa_app_application_mod_presentation_options(
    const qa_app_application_mod_presentation *presentation);
qa_cvar_registry *qa_app_application_mod_presentation_cvars(qa_app_application_mod_presentation *presentation);
qa_content_mounted *qa_app_application_mod_presentation_file_mounts(qa_app_application_mod_presentation *presentation);
qa_app_application_q3_assets *qa_app_application_mod_presentation_media_assets(qa_app_application_mod_presentation *presentation);
qa_app_application_q3_services *qa_app_application_mod_presentation_services(qa_app_application_mod_presentation *presentation);
qa_app_application_q3_scene_renderer *qa_app_application_mod_presentation_renderer(qa_app_application_mod_presentation *presentation);
/* TS bodies/hud getters: borrowed until the next frame or close. */
const qa_app_component_body *qa_app_application_mod_presentation_bodies(qa_app_application_mod_presentation *presentation,
    size_t *out_count);
const qa_app_q3_overlay_submission *qa_app_application_mod_presentation_hud(qa_app_application_mod_presentation *presentation,
    size_t *out_count);
double qa_app_application_mod_presentation_time(qa_app_application_mod_presentation *presentation);
/* TS owns(source, viewer): source identity is the source ctx and the
 * prepared artifact. */
bool qa_app_application_mod_presentation_owns(const qa_app_application_mod_presentation *presentation,
    const qa_active_mod_presentation *source, qa_actor_id viewer);
/* Owned tree. */
qa_save_value *qa_app_application_mod_presentation_capture_checkpoint(qa_app_application_mod_presentation *presentation);
void qa_app_application_mod_presentation_publish_commands(qa_app_application_mod_presentation *presentation);
bool qa_app_application_mod_presentation_command(qa_app_application_mod_presentation *presentation,
    const char *const *arguments, size_t count);
void qa_app_application_mod_presentation_consume(qa_app_application_mod_presentation *presentation,
    const qa_app_q3_source_player_event *event, double sequence);
/* Borrowed scene, valid until the next frame or close. */
const qa_q3_pres_scene_content *qa_app_application_mod_presentation_frame(qa_app_application_mod_presentation *presentation,
    double sequence);
void qa_app_application_mod_presentation_close(qa_app_application_mod_presentation *presentation);
#endif
