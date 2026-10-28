/* Port of src/app/bootstrap/campaign-cinematic.ts. Movie decode,
 * playback, fullscreen presentation and audio lanes are landed/host
 * services; request path selection, checkpoint field validation and
 * lifecycle/input gating are kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CAMPAIGN_CINEMATIC_H
#define QA_APP_BOOTSTRAP_CAMPAIGN_CINEMATIC_H
#include <stdbool.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/persistence/value.h"
/* TS ScreenCinematicRequest. */
typedef struct qa_app_screen_cinematic_request { const char *name; bool loop, hold, silent; } qa_app_screen_cinematic_request;
typedef struct qa_app_campaign_cinematic qa_app_campaign_cinematic;
typedef struct qa_app_campaign_cinematic_host {
  void *ctx;
  /* Open a mounted resource; false = TS null. Fills path identity text. */
  bool (*open)(void *ctx, const char *path, const void **out_resource, char *identity, size_t identity_cap);
  bool (*current)(void *ctx);
  /* Build playback/fullscreen from the opened resource; error or NULL.
   * saved_value NULL mirrors fresh prepare. */
  const char *(*build)(void *ctx, const char *path, const char *extension, qa_app_screen_cinematic_request request, const qa_save_value *saved_value);
  void (*captions_prepare)(void *ctx, const char *path);
  const char *(*status)(void *ctx); /* CinematicStatus text */
  double (*playback_milliseconds)(void *ctx);
  void (*pause)(void *ctx, bool paused);
  void (*skip)(void *ctx);
  void (*start_audio)(void *ctx);
  bool (*frame_draw)(void *ctx, qa_rect viewport, const char *focus_kind, double sequence); /* false = blank */
  void (*pump_audio)(void *ctx);
  void (*close_movie)(void *ctx, double sequence);
  qa_vec2 (*drawable_size)(void *ctx);
  qa_save_value *(*capture_owned)(void *ctx);
} qa_app_campaign_cinematic_host;
/* Malloc'd selected resource path (pure path logic); aborts exactly. */
char *qa_app_cinematic_resource_path(const char *name, bool exists_as_selected, bool cin_fallback_exists);
/* TS readAudio checkpoint validation is host-side via SaveReader. */
qa_app_campaign_cinematic *qa_app_campaign_cinematic_prepare(qa_app_campaign_cinematic_host host, qa_app_screen_cinematic_request request, bool has_captions);
qa_app_campaign_cinematic *qa_app_campaign_cinematic_restore(qa_app_campaign_cinematic_host host, const qa_save_value *value, bool has_captions);
void qa_app_campaign_cinematic_destroy(qa_app_campaign_cinematic *cinematic);
qa_save_value *qa_app_campaign_cinematic_capture_checkpoint(qa_app_campaign_cinematic *cinematic);
void qa_app_campaign_cinematic_activate(qa_app_campaign_cinematic *cinematic);
const char *qa_app_campaign_cinematic_status(qa_app_campaign_cinematic *cinematic);
void qa_app_campaign_cinematic_pause(qa_app_campaign_cinematic *cinematic, bool paused);
void qa_app_campaign_cinematic_skip(qa_app_campaign_cinematic *cinematic);
bool qa_app_campaign_cinematic_input(qa_app_campaign_cinematic *cinematic, const qa_seat_input_event *event);
bool qa_app_campaign_cinematic_frame(qa_app_campaign_cinematic *cinematic, double elapsed_milliseconds, double sequence, bool console_open, bool menu_open);
void qa_app_campaign_cinematic_close(qa_app_campaign_cinematic *cinematic, double sequence);
#endif
