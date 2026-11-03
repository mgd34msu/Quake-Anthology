/* Port of src/app/bootstrap/q3-client/cinematics.ts. Async loads
 * are synchronous; MissingCinematicError is a distinct false
 * result from prepare (other failures raise the TS Error texts).
 * Movies allocate their CinematicImage in the ApplicationAssets
 * image registry behind the ApplicationQ3Assets and draw through an
 * implicit picture material bound to the playback as a video map.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_CINEMATICS_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_CINEMATICS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/q3_client/assets.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/media/audio.h"
#include "qa/media/types.h"
#include "qa/persistence/value.h"
#include "qa/text/draw2d.h"
/* TS ScreenCinematicRequest (campaign-cinematic.ts mirror subset). */
typedef struct qa_app_q3_screen_cinematic_request {
  const char *name;
  bool loop, hold, silent;
} qa_app_q3_screen_cinematic_request;
/* TS SystemCinematicHandle. capture/publish NULL = TS optional absent. */
typedef struct qa_app_system_cinematic_handle qa_app_system_cinematic_handle;
struct qa_app_system_cinematic_handle {
  void *ctx;
  const char *status; /* CinematicStatus text */
  void (*refresh_status)(void *ctx, qa_app_system_cinematic_handle *handle);
  void (*skip)(void *ctx);
  void (*stop)(void *ctx);
  qa_save_value *(*capture_checkpoint)(void *ctx); /* nullable fn */
  void (*publish_restored)(void *ctx);             /* nullable fn */
};
/* TS SystemCinematicHost. current: TS () => boolean liveness. */
typedef struct qa_app_system_cinematic_host {
  void *ctx;
  qa_app_system_cinematic_handle (*open)(void *ctx, qa_app_q3_screen_cinematic_request request,
                                         bool (*current)(void *cctx), void *current_ctx);
  qa_app_system_cinematic_handle (*restore)(void *ctx, const qa_save_value *value,
                                            bool (*current)(void *cctx), void *current_ctx); /* nullable fn */
} qa_app_system_cinematic_host;
/* TS UiCinematicAsset. */
typedef struct qa_app_ui_cinematic_asset { const char *path; } qa_app_ui_cinematic_asset;
/* TS ApplicationQ3Cinematics(assets, audio, seat, now, system): assets is
 * the ApplicationQ3Assets (provider mounts, print, and the images of the
 * ApplicationAssets it was built over); mixer is audio.engine. system NULL
 * = TS undefined. media must outlive the cinematics. */
typedef struct qa_app_application_q3_cinematics qa_app_application_q3_cinematics;
qa_app_application_q3_cinematics *qa_app_application_q3_cinematics_create(
    qa_app_application_q3_assets *media, qa_cinematic_mixer mixer, qa_seat_id seat,
    double (*now)(void *ctx), void *now_ctx,
    const qa_app_system_cinematic_host *system);
void qa_app_application_q3_cinematics_destroy(qa_app_application_q3_cinematics *cinematics);
/* Fields: assets/audio/seat/now accessors. */
qa_seat_id qa_app_application_q3_cinematics_seat(const qa_app_application_q3_cinematics *c);
/* owner.prepare(path): false is MissingCinematicError. */
bool qa_app_application_q3_cinematics_prepare(qa_app_application_q3_cinematics *c, const char *path,
                                              qa_app_ui_cinematic_asset *out);
/* owner.stopSlot(index) == stop(index). */
/* TS play(): false is the TS undefined (no free slot). */
bool qa_app_application_q3_cinematics_play(qa_app_application_q3_cinematics *c,
    qa_app_ui_cinematic_asset asset, qa_rect rect, double *out_handle_index);
/* Owned checkpoint tree; aborts with the TS Error texts. */
qa_save_value *qa_app_application_q3_cinematics_capture_checkpoint(qa_app_application_q3_cinematics *c);
void qa_app_application_q3_cinematics_restore_checkpoint(qa_app_application_q3_cinematics *c,
                                                         const qa_save_value *value);
void qa_app_application_q3_cinematics_publish_restored(qa_app_application_q3_cinematics *c);
double qa_app_application_q3_cinematics_play_guest(qa_app_application_q3_cinematics *c,
                                                   const char *path, qa_rect rect, double bits);
double qa_app_application_q3_cinematics_run_guest(qa_app_application_q3_cinematics *c, double handle);
double qa_app_application_q3_cinematics_stop_guest(qa_app_application_q3_cinematics *c, double handle);
void qa_app_application_q3_cinematics_draw_guest(qa_app_application_q3_cinematics *c, double handle, qa_draw2d *draw);
void qa_app_application_q3_cinematics_set_extents(qa_app_application_q3_cinematics *c, double handle, qa_rect rect);
void qa_app_application_q3_cinematics_run(qa_app_application_q3_cinematics *c, double handle);
void qa_app_application_q3_cinematics_draw(qa_app_application_q3_cinematics *c, double handle, qa_rect rect, qa_draw2d *draw);
void qa_app_application_q3_cinematics_stop(qa_app_application_q3_cinematics *c, double handle);
void qa_app_application_q3_cinematics_close(qa_app_application_q3_cinematics *c);
#endif
