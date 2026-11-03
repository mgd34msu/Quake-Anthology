/* Port of src/app/bootstrap/audio.ts (ApplicationAudio): the
 * application's presentation audio over the shared UnifiedAudio
 * engine, SoundBank, ApplicationMusic and the per-family event sound
 * tables. C accommodations:
 *  - The async TS methods are synchronous; every await point keeps its
 *    TS checks (closed, presentation owner revision, music request
 *    counter) in place, so they hold the values TS would observe with no
 *    interleaving. Promise caches (banks, sounds, playlists, footsteps)
 *    are plain caches; a load that raises inserts nothing, the role of
 *    cacheEvictingRejection.
 *  - TS throws are qa_raise("Error", ...) (catchable with qa_try); TS
 *    catch blocks that recover (default environment file, footstep
 *    config, haptics rejection, device fallback) recover here.
 *  - The ApplicationInput haptics owner is the qa_app_audio_haptics
 *    interface: the two members TS calls (soundHaptics, bindHaptics).
 *  - SimulationPresentationEvent is qa_app_sim_presentation_event.
 *  - Records queued for the next frame (uiSound, receiveEffectSounds,
 *    receiveCgameFrame) are copied; strings are interned by the audio.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_AUDIO_H
#define QA_APP_BOOTSTRAP_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/audio/playlist_settings.h"
#include "qa/app/bootstrap/audio/q3.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/effects/q3.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/audio/engine.h"
#include "qa/audio/music.h"
#include "qa/audio/output.h"
#include "qa/audio/types.h"
#include "qa/content/q3/presentation/audio.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"

/* TS print: (text) => undefined. */
typedef void (*qa_app_audio_print_fn)(void *ctx, const char *text);

/* TS ApplicationAudioCommand. registrations is the TS
 * Pick<SoundRegistration, "path" | "sound"> list (NULL/0 = absent);
 * print NULL = absent (the audio's own print). */
typedef struct qa_app_audio_command_request {
  const char *name;
  const char *const *args; size_t arg_count;
  bool has_seat; qa_seat_id seat; /* false = TS null */
  const qa_q3_pres_sound_registration *registrations; size_t registration_count;
  qa_app_audio_print_fn print; void *print_ctx;
} qa_app_audio_command_request;

/* TS ApplicationAudioOptions. q3_team_game NULL = TS default
 * (() => false); music_controls NULL = TS undefined (owned default). */
typedef struct qa_app_audio_options {
  bool (*q3_team_game)(void *ctx); void *q3_team_game_ctx;
  qa_music_controls *music_controls;
  bool has_output_format; qa_audio_output_format output_format;
  bool defer_output;
  const char *device_name; /* NULL = TS undefined/null */
  bool has_effects_volume; double effects_volume;
  bool has_music_volume; double music_volume;
} qa_app_audio_options;

/* TS ApplicationEffectSound = SourceEffectSound. */
typedef qa_app_source_effect_sound qa_app_application_effect_sound;

/* TS ApplicationAudioSeatEvents. scene NULL = TS undefined. */
typedef struct qa_app_audio_seat_events {
  qa_seat_id seat;
  const qa_scene_queries *scene;
  const qa_world_snapshot *snapshot;
  const qa_app_sim_presentation_event *events; size_t event_count;
  bool music;
  const qa_app_application_effect_sound *effect_sounds; size_t effect_sound_count;
} qa_app_audio_seat_events;

/* TS bindHaptics load: (request) => Promise<Uint8Array | null>.
 * Returns malloc'd bytes, or NULL for the TS null. */
typedef uint8_t *(*qa_app_audio_haptic_load_fn)(void *ctx, qa_resource_request request, size_t *out_length);
/* The ApplicationInput members TS ApplicationAudio calls.
 * sound_haptics raises (qa_raise) where the TS promise rejects. */
typedef struct qa_app_audio_haptics {
  void *ctx;
  void (*sound_haptics)(void *ctx, qa_content_id content, const char *sound, bool has_actor,
                        qa_actor_id actor, qa_audio_audience audience);
  void (*bind_haptics)(void *ctx, qa_app_audio_haptic_load_fn load, void *load_ctx);
} qa_app_audio_haptics;

typedef struct qa_app_application_audio qa_app_application_audio;

/* TS constructor. content is borrowed (its catalog is a
 * qa_content_catalog and for_content returns qa_content_mounted).
 * Raises the TS errors (device open failure). NULL only on OOM. */
qa_app_application_audio *qa_app_application_audio_create(qa_app_loaded_content *content,
    double (*now)(void *ctx), void *now_ctx, double seed, const char *character_model,
    qa_app_audio_print_fn print, void *print_ctx, const qa_app_audio_options *options);
/* Closes (TS close) and frees everything. */
void qa_app_application_audio_destroy(qa_app_application_audio *audio);

/* TS engine field (borrowed). */
qa_unified_audio *qa_app_application_audio_engine(qa_app_application_audio *audio);
/* TS prepareOutputTransfer: commit with qa_unified_audio_transfer_commit. */
qa_unified_audio_transfer qa_app_application_audio_prepare_output_transfer(qa_app_application_audio *audio,
    qa_app_application_audio *next);
qa_audio_output_format qa_app_application_audio_output_format(const qa_app_application_audio *audio);
void qa_app_application_audio_bind_output_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars);
void qa_app_application_audio_select_output_format(qa_app_application_audio *audio, qa_audio_output_format format);
void qa_app_application_audio_restart_output(qa_app_application_audio *audio);
/* Borrowed engine text; NULL is the TS null. */
const char *qa_app_application_audio_selected_output(const qa_app_application_audio *audio);
/* Free with qa_sdl_audio_device_names_free. */
char **qa_app_application_audio_output_device_names(qa_app_application_audio *audio, size_t *out_count);
void qa_app_application_audio_select_output(qa_app_application_audio *audio, const char *device_name);
void qa_app_application_audio_detach_output(qa_app_application_audio *audio);

/* TS bindHaptics(input). */
void qa_app_application_audio_bind_haptics(qa_app_application_audio *audio, qa_app_audio_haptics input);
/* The load closure TS hands input.bindHaptics (ctx = audio). */
uint8_t *qa_app_application_audio_load_haptic(void *audio, qa_resource_request request, size_t *out_length);

/* TS prepareEnvironment(scene). scene is copied (its ctx borrowed). */
void qa_app_application_audio_prepare_environment(qa_app_application_audio *audio, const qa_scene_queries *scene);

/* TS musicPreferences getter (menu_track owned by the caller). */
qa_app_audio_music_preferences qa_app_application_audio_music_preferences(qa_app_application_audio *audio);
void qa_app_application_audio_bind_volume_cvars(qa_app_application_audio *audio, qa_cvar_registry *cvars);
double qa_app_application_audio_effects_volume(const qa_app_application_audio *audio);
void qa_app_application_audio_set_effects_volume(qa_app_application_audio *audio, double value);
double qa_app_application_audio_music_volume(const qa_app_application_audio *audio);
void qa_app_application_audio_set_music_volume(qa_app_application_audio *audio, double value);

/* TS uiSound(sound: UiSound, seat). */
void qa_app_application_audio_ui_sound(qa_app_application_audio *audio, const char *sound, qa_seat_id seat);
void qa_app_application_audio_receive_effect_sounds(qa_app_application_audio *audio,
    const qa_app_application_effect_sound *sounds, size_t count);
void qa_app_application_audio_receive_cgame_frame(qa_app_application_audio *audio,
    const qa_app_q3_seat_audio_frame *frame);

/* TS command: false when not an audio command. */
bool qa_app_application_audio_command(qa_app_application_audio *audio, const qa_app_audio_command_request *request);
/* selected_model NULL = TS undefined. */
void qa_app_application_audio_preload_sound(qa_app_application_audio *audio, qa_content_id content,
    const char *path, const char *selected_model);
void qa_app_application_audio_preload_character_footsteps(qa_app_application_audio *audio);
/* owner NULL = TS undefined. */
void qa_app_application_audio_play_music(qa_app_application_audio *audio, qa_content_id content,
    const char *track, const qa_presentation_owner *owner);
/* TS playComponentMedia(source, active): event is the TS music or
 * music-stop request (shader-remap is excluded by the TS type and
 * raises); owner NULL = TS undefined. */
void qa_app_application_audio_play_component_media(qa_app_application_audio *audio,
    const qa_presentation_owner *owner, qa_content_id content,
    const qa_component_presentation_media_request *event, bool (*active)(void *ctx), void *active_ctx);
void qa_app_application_audio_start_world_music(qa_app_application_audio *audio);
/* TS receive(events, audience = world, music = true). */
void qa_app_application_audio_receive(qa_app_application_audio *audio,
    const qa_app_sim_presentation_event *events, size_t count, qa_audio_audience audience, bool music);
/* TS frame(snapshot, listeners, events, frameStartedAt, seatEvents):
 * frame_started_at is the caller's performance.now() milliseconds
 * (qa_app_audio_now_milliseconds) when the frame began. */
void qa_app_application_audio_frame(qa_app_application_audio *audio, const qa_world_snapshot *snapshot,
    const qa_audio_listener *listeners, size_t listener_count,
    const qa_app_sim_presentation_event *events, size_t event_count, double frame_started_at,
    const qa_app_audio_seat_events *seat_events, size_t seat_event_count);
void qa_app_application_audio_reset_round(qa_app_application_audio *audio);
void qa_app_application_audio_close(qa_app_application_audio *audio);

/* performance.now(): monotonic milliseconds. */
double qa_app_audio_now_milliseconds(void);
#endif
