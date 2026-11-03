/* Port of src/app/bootstrap/mod-presentations.ts: the local seats share
 * source events; each cgame keeps its own viewer, media and transient
 * state (ApplicationModPresentations). Async is synchronous; errors raise
 * through qa_raise with the TS class and message. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MOD_PRESENTATIONS_H
#define QA_APP_BOOTSTRAP_MOD_PRESENTATIONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/mod_presentation.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
#include "qa/core/commands/index.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"

/* TS ComponentClientCommandTarget. instance is the TS symbol (an identity
 * owned by the collection entry). */
typedef struct qa_app_component_client_command_target {
  qa_presentation_owner owner;
  const void *instance;
  qa_seat_id seat;
  qa_actor_id viewer;
} qa_app_component_client_command_target;
/* TS ComponentClientCommandRequest (target is always "component-client").
 * consumer is the collection's target (identity compared by pointer);
 * arguments and source are borrowed for the call. */
typedef struct qa_app_component_client_command_request {
  const qa_app_component_client_command_target *consumer;
  const char *mode; /* "console" | "reliable" */
  const char *name;
  const char *const *arguments; size_t argument_count;
  qa_seat_id seat;
  qa_command_context source;
} qa_app_component_client_command_request;
/* TS systemCinematics scope { cvars, mounts, append(text) }. */
typedef struct qa_app_component_cinematic_scope {
  qa_cvar_registry *cvars;
  qa_content_mounted *mounts;
  void *ctx;
  void (*append)(void *ctx, const char *text);
} qa_app_component_cinematic_scope;

/* TS ApplicationModPresentationsOptions. ctx is the context of print,
 * next_frame, presentation_media, queue_command and system_cinematics;
 * NULL optional members are TS undefined. input is the TS Pick<ApplicationInput,
 * "commands" | "clientCommandRegistration">. */
typedef struct qa_app_mod_presentations_options {
  qa_app_application_assets *assets;
  qa_app_application_audio *audio;
  qa_shared_scene_queries *queries;
  qa_app_q3_service_clock clock;
  const qa_app_qvm_display_renderer *renderer;
  qa_app_application_input *input;
  void *ctx;
  void (*print)(void *ctx, const char *text);
  void (*next_frame)(void *ctx);
  void (*presentation_media)(void *ctx, const qa_active_mod_presentation *source,
                             const qa_component_presentation_media_request *request, bool initializing,
                             bool (*current)(void *current_ctx), void *current_ctx);
  void (*queue_command)(void *ctx, const qa_app_component_client_command_request *request);
  const qa_app_system_cinematic_host *(*system_cinematics)(void *ctx, const qa_active_mod_presentation *source,
      const qa_app_viewing_seat *presentation, const qa_app_component_cinematic_scope *scope);
  qa_app_q3_presentation_hosts hosts;
} qa_app_mod_presentations_options;

/* Builds the TS bindComponentEffects frame of one captured Q3 scene: the
 * scene admission, its lights (minimum 0) and q3 lights (first 32), and the
 * renderer's operations for a world surface admission of order. */
void qa_app_q3_scene_effect_frame(qa_app_application_q3_scene_renderer *renderer, const qa_q3_pres_scene_content *scene,
    qa_scene_camera camera, qa_source_scene_order *order, const qa_scene_fog *q1_fog, qa_seat_id seat,
    double time_milliseconds, bool split_screen, qa_app_application_effect_frame *out);

typedef struct qa_app_application_mod_presentations qa_app_application_mod_presentations;
/* options is copied; its pointers are borrowed. */
qa_app_application_mod_presentations *qa_app_application_mod_presentations_create(
    const qa_app_mod_presentations_options *options);
/* Closes (TS close), then frees; a close failure raises after the free. */
void qa_app_application_mod_presentations_destroy(qa_app_application_mod_presentations *presentations);
/* Owned tree. sources are borrowed views. */
qa_save_value *qa_app_application_mod_presentations_capture_checkpoint(qa_app_application_mod_presentations *presentations,
    const qa_app_viewing_seat *seats, size_t seat_count, const qa_active_mod_presentation *const *sources, size_t source_count);
void qa_app_application_mod_presentations_restore_checkpoint(qa_app_application_mod_presentations *presentations,
    const qa_save_value *value, const qa_app_viewing_seat *seats, size_t seat_count,
    const qa_active_mod_presentation *const *sources, size_t source_count,
    qa_qvm_resolve_saved_actor_fn resolve_actor, void *resolve_ctx);
void qa_app_application_mod_presentations_publish_restored(qa_app_application_mod_presentations *presentations);
bool qa_app_application_mod_presentations_pending_commands(qa_app_application_mod_presentations *presentations);
/* NULL is TS null. */
qa_cvar_registry *qa_app_application_mod_presentations_command_cvars(qa_app_application_mod_presentations *presentations,
    const qa_command_context *source);
/* malloc'd text; NULL is TS undefined. */
char *qa_app_application_mod_presentations_read_command_script(qa_app_application_mod_presentations *presentations,
    const char *name, const qa_command_context *source);
bool qa_app_application_mod_presentations_queue_console(qa_app_application_mod_presentations *presentations,
    qa_command_invocation *command);
/* true is "handled", false "retired". */
bool qa_app_application_mod_presentations_dispatch_command(qa_app_application_mod_presentations *presentations,
    const qa_app_component_client_command_request *request);
void qa_app_application_mod_presentations_retain_presentations(qa_app_application_mod_presentations *presentations,
    const qa_app_viewing_seat *seats, size_t seat_count);
void qa_app_application_mod_presentations_prepare(qa_app_application_mod_presentations *presentations,
    const qa_app_viewing_seat *seats, size_t seat_count, const qa_active_mod_presentation *const *sources,
    size_t source_count, const qa_app_sim_presentation_event *events, size_t event_count, double frame_sequence);
void qa_app_application_mod_presentations_close(qa_app_application_mod_presentations *presentations);
#endif
