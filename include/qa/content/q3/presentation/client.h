/* Port of src/content/q3/presentation/client.ts. GPL-2.0-or-later.
 * Composition of source cgame runtimes over one unified client/seat.
 * The factory owns state/static/config-strings and the frame/console
 * runtimes; component hosts (media, snapshots, prediction, view,
 * packet, particles, weapons, HUD chain) are supplied ready-built in
 * the options, matching the landed app/bootstrap wiring style.
 * TS async creation is synchronous; create returns false only on OOM. */
#ifndef QA_CONTENT_Q3_PRESENTATION_CLIENT_H
#define QA_CONTENT_Q3_PRESENTATION_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/audio/mixer.h"
#include "qa/audio/wav.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/presentation/audio.h"
#include "qa/content/q3/presentation/config.h"
#include "qa/content/q3/presentation/console.h"
#include "qa/content/q3/presentation/frame.h"
#include "qa/content/q3/presentation/media.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/contracts/math.h"
typedef enum qa_q3_pres_session_mode { QA_Q3_PRES_SESSION_LIVE, QA_Q3_PRES_SESSION_DEMO } qa_q3_pres_session_mode;
/* TS Q3PresentationSession. get_game_state/get_server_command write
 * borrowed token spans; get_server_command false is TS null. */
typedef struct qa_q3_pres_presentation_session {
  qa_q3_base_product product;
  double client_number, server_message_sequence, last_executed_server_command;
  qa_q3_pres_session_mode mode;
  void *ctx;
  bool (*status_visible)(void *ctx); /* NULL is the TS absent optional */
  const char *const *(*get_game_state)(void *ctx, size_t *out_count);
  bool (*get_server_command)(void *ctx, double sequence, const char *const **out, size_t *out_count);
  bool (*snapshot_ping)(void *ctx, double number, double *out);
  void (*add_reliable_command)(void *ctx, const char *text);
  void (*append_console_command)(void *ctx, const char *text);
  void (*register_cgame_command)(void *ctx, const char *name);
  void (*set_user_command_value)(void *ctx, double weapon, double sensitivity);
  void (*assert_current)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_q3_pres_presentation_session;
/* TS Q3ClientSound. origin NULL is TS null. */
typedef struct qa_q3_pres_client_sound {
  qa_q3_pres_presentation_sound_bank *bank;
  void *ctx;
  void (*start_sound)(void *ctx, const qa_vec3 *origin, double entity, double channel, qa_pcm_sound *pcm);
  void (*start_source_sound)(void *ctx, qa_pcm_sound *pcm, qa_start_sound_options options);
  void (*start_local_sound)(void *ctx, qa_pcm_sound *pcm, double channel);
  void (*add_loop_sound)(void *ctx, double entity, qa_vec3 origin, qa_vec3 velocity, qa_pcm_sound *pcm, bool real);
  void (*update_sound_position)(void *ctx, double entity, qa_vec3 position);
  void (*stop_looping_sound)(void *ctx, double entity);
  void (*clear_looping_sounds)(void *ctx, bool kill_all);
  void (*set_listener)(void *ctx, double client, qa_vec3 origin, qa_axis axis);
  bool (*start_background_track)(void *ctx, const char *intro, const char *loop);
} qa_q3_pres_client_sound;
typedef void (*qa_q3_pres_client_source_fn)(void *ctx);
/* TS Q3ClientPresentationOptions, component-host form. frame_host /
 * console_host carry the already-built sub-runtimes; media_host may
 * be NULL to skip the registration sequence. */
typedef struct qa_q3_pres_client_presentation_options {
  qa_q3_pres_presentation_session session;
  qa_q3_pres_client_sound sound;
  qa_q3_pres_client_configuration *configuration;
  qa_q3_pres_client_media *media;
  qa_q3_pres_client_media_host *media_host; /* nullable */
  qa_q3_pres_presentation_frame_host frame_host;
  qa_q3_pres_client_console_host console_host;
  qa_q3_pres_local_entity_pool *pool;
  qa_q3_pres_impact_mark_system *marks;
  qa_particle_system *particles;
  qa_q3_pres_renderer_resources resources;
  void *ctx;
  void (*clear_particles)(void *ctx);
  bool (*set_config_values)(void *ctx);
  bool (*start_music)(void *ctx);
  bool (*shader_state_changed)(void *ctx);
  void (*loading_string_done)(void *ctx);
  void (*init_team_chat)(void *ctx); /* nullable (mission menus) */
  bool (*dispose_server_commands)(void *ctx);
  bool (*dispose_menus)(void *ctx); /* nullable */
  bool (*key_event)(void *ctx, double key, bool down); /* nullable */
  bool (*mouse_event)(void *ctx, double x, double y); /* nullable */
  bool (*event_handling)(void *ctx, double type); /* nullable */
} qa_q3_pres_client_presentation_options;
/* TS Q3ScenePresentationOptions: scene frame host + centerPrint. */
typedef struct qa_q3_pres_scene_presentation_options {
  qa_q3_pres_presentation_session session;
  qa_q3_pres_client_sound sound;
  qa_q3_pres_client_configuration *configuration;
  qa_q3_pres_client_media *media;
  qa_q3_pres_client_media_host *media_host; /* nullable */
  qa_q3_pres_presentation_scene_host scene_host;
  qa_q3_pres_local_entity_pool *pool;
  qa_q3_pres_impact_mark_system *marks;
  qa_particle_system *particles;
  qa_q3_pres_renderer_resources resources;
  void *ctx;
  void (*clear_particles)(void *ctx);
  bool (*set_config_values)(void *ctx);
  void (*center_print)(void *ctx, const char *text, double time_ms, double duration_ms);
} qa_q3_pres_scene_presentation_options;
typedef struct qa_q3_pres_client_presentation qa_q3_pres_client_presentation;
/* TS createQ3ClientPresentation. NULL only on OOM; the TS HUD-owner
 * loss aborts. The presentation owns state/static/strings/frame/
 * console; component pointers in options stay caller-owned. */
qa_q3_pres_client_presentation *qa_q3_pres_create_client_presentation(
    qa_q3_pres_client_presentation_options options);
qa_q3_pres_client_presentation *qa_q3_pres_create_scene_presentation(
    qa_q3_pres_scene_presentation_options options);
void qa_q3_pres_client_presentation_destroy(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_game_state *qa_q3_pres_client_presentation_state(qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_game_static_state *qa_q3_pres_client_presentation_static_state(
    qa_q3_pres_client_presentation *presentation);
qa_q3_pres_presentation_frame_runtime *qa_q3_pres_client_presentation_frames(
    qa_q3_pres_client_presentation *presentation);
qa_q3_pres_client_console_runtime *qa_q3_pres_client_presentation_console(
    qa_q3_pres_client_presentation *presentation); /* NULL for scene scope */
const char *qa_q3_pres_client_presentation_config_string(qa_q3_pres_client_presentation *presentation,
                                                         double index);
/* Takes a borrowed span copy (strings duplicated). False only OOM. */
bool qa_q3_pres_client_presentation_set_config_strings(qa_q3_pres_client_presentation *presentation,
    const char *const *strings, size_t count);
void qa_q3_pres_client_presentation_close(qa_q3_pres_client_presentation *presentation);
bool qa_q3_pres_client_presentation_key_event(qa_q3_pres_client_presentation *presentation, double key, bool down);
bool qa_q3_pres_client_presentation_mouse_event(qa_q3_pres_client_presentation *presentation, double x, double y);
bool qa_q3_pres_client_presentation_event_handling(qa_q3_pres_client_presentation *presentation, double type);
#endif
