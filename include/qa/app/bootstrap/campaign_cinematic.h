/* Port of src/app/bootstrap/campaign-cinematic.ts: CampaignCinematic over the mounted content, ApplicationAssets'
 * images, the ApplicationAudio engine, the NativeRenderer and the media layer (CinematicPlayback,
 * FullscreenCinematic, cinematicAudio, cinematicBytes/cinematicPcx). GPL-2.0-or-later.
 * C accommodations: TS async prepare/restore are synchronous; TS throws are qa_raise "Error" (RangeError for the
 * frame interval), so the callers' catches see them. The cinematic owns its playback, opened resource bytes, the
 * resource reference copy and the restored PCM checkpoint until qa_app_campaign_cinematic_destroy (TS GC). */
#ifndef QA_APP_BOOTSTRAP_CAMPAIGN_CINEMATIC_H
#define QA_APP_BOOTSTRAP_CAMPAIGN_CINEMATIC_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/persistence/value.h"
#include "qa/media/types.h"
#include "qa/text/media_captions.h"
/* TS ScreenCinematicRequest. */
typedef struct qa_app_screen_cinematic_request { const char *name; bool loop, hold, silent; } qa_app_screen_cinematic_request;
/* TS CinematicTimeline as ScreenCinematicCaptions.commands reads it. */
typedef struct qa_app_cinematic_caption_timeline {
  qa_caption_playback_state state;
  double elapsed_milliseconds;
} qa_app_cinematic_caption_timeline;
/* TS ScreenCinematicCaptions. commands returns a malloc'd array (caller frees). */
typedef struct qa_app_screen_cinematic_captions {
  void *ctx;
  void (*prepare)(void *ctx, const char *source);
  qa_render_command *(*commands)(void *ctx, const qa_app_cinematic_caption_timeline *timeline, qa_rect viewport, size_t *out_count);
} qa_app_screen_cinematic_captions;
typedef struct qa_app_campaign_cinematic qa_app_campaign_cinematic;
struct qa_content_mounted;
struct qa_app_application_assets;
struct qa_app_application_audio;
struct qa_app_native_renderer;
/* TS CampaignCinematic.prepare(request, content, assets, audio, renderer, seat, current, captions). mounts is
 * content.mounts; captions NULL is TS null. current is called with current_ctx. */
qa_app_campaign_cinematic *qa_app_campaign_cinematic_prepare_content(qa_app_screen_cinematic_request request,
    const struct qa_content_mounted *mounts, struct qa_app_application_assets *assets, struct qa_app_application_audio *audio,
    struct qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx,
    qa_app_screen_cinematic_captions *captions);
/* The same over the members TS reads: { images } (a SceneImageRegistry) and { engine } (a UnifiedAudio). */
struct qa_scene_image_registry;
struct qa_unified_audio;
qa_app_campaign_cinematic *qa_app_campaign_cinematic_prepare_engine(qa_app_screen_cinematic_request request,
    const struct qa_content_mounted *mounts, struct qa_scene_image_registry *images, struct qa_unified_audio *engine,
    struct qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx,
    qa_app_screen_cinematic_captions *captions);
/* TS CampaignCinematic.restore(value, ...). value is borrowed for the call. */
qa_app_campaign_cinematic *qa_app_campaign_cinematic_restore_content(const qa_save_value *value,
    const struct qa_content_mounted *mounts, struct qa_app_application_assets *assets, struct qa_app_application_audio *audio,
    struct qa_app_native_renderer *renderer, qa_seat_id seat, bool (*current)(void *ctx), void *current_ctx,
    qa_app_screen_cinematic_captions *captions);
/* Closes (sequence 0) when still open, then frees the cinematic (TS GC). */
void qa_app_campaign_cinematic_destroy(qa_app_campaign_cinematic *cinematic);
/* TS captureCheckpoint(): an owned tree. */
qa_save_value *qa_app_campaign_cinematic_capture_checkpoint(qa_app_campaign_cinematic *cinematic);
void qa_app_campaign_cinematic_activate(qa_app_campaign_cinematic *cinematic);
/* TS timeline: movie.playback.timeline. */
struct qa_cinematic_timeline qa_app_campaign_cinematic_timeline(qa_app_campaign_cinematic *cinematic);
/* TS status: the CinematicStatus name. */
const char *qa_app_campaign_cinematic_status(qa_app_campaign_cinematic *cinematic);
void qa_app_campaign_cinematic_pause(qa_app_campaign_cinematic *cinematic, bool paused);
void qa_app_campaign_cinematic_skip(qa_app_campaign_cinematic *cinematic);
bool qa_app_campaign_cinematic_input(qa_app_campaign_cinematic *cinematic, const qa_seat_input_event *event);
/* TS frame(elapsedMilliseconds, sequence, consoleOpen = false, menuOpen = false). */
bool qa_app_campaign_cinematic_frame(qa_app_campaign_cinematic *cinematic, double elapsed_milliseconds, double sequence, bool console_open, bool menu_open);
void qa_app_campaign_cinematic_close(qa_app_campaign_cinematic *cinematic, double sequence);
#endif
