/* Port of src/app/bootstrap/q3-client/cinematics.ts. Async loads
 * are synchronous; MissingCinematicError is a distinct false
 * result from prepare (other failures abort with the TS Error
 * texts). The shader picture construction and the checkpoint
 * resource writer are host mirrors (materials/persistence
 * owners). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_CINEMATICS_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_CINEMATICS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/media/audio.h"
#include "qa/media/types.h"
#include "qa/persistence/value.h"
#include "qa/text/draw2d.h"
/* TS ScreenCinematicRequest (campaign-cinematic.ts mirror subset). */
typedef struct qa_app_screen_cinematic_request {
  const char *name;
  bool loop, hold, silent;
} qa_app_screen_cinematic_request;
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
  qa_app_system_cinematic_handle (*open)(void *ctx, qa_app_screen_cinematic_request request,
                                         bool (*current)(void *cctx), void *current_ctx);
  qa_app_system_cinematic_handle (*restore)(void *ctx, const qa_save_value *value,
                                            bool (*current)(void *cctx), void *current_ctx); /* nullable fn */
} qa_app_system_cinematic_host;
/* Host mirrors: assets.images allocate/commit/release, the
 * implicit picture material, the checkpoint resource writer. */
typedef struct qa_app_q3_cinematics_host {
  void *ctx;
  /* images.allocate(width,height,{kind:"resource",resource}) and
   * the CinematicImage reallocation form; token is opaque. */
  void *(*image_allocate)(void *ctx, double width, double height, qa_resolved_resource_reference resource);
  void (*image_commit)(void *ctx, void *image);
  void (*image_release)(void *ctx, void *image);
  /* Builds the {kind:"material",name,material} picture for a movie;
   * frame access rides the returned picture's video binding. */
  qa_picture_asset (*make_picture)(void *ctx, const char *name, void *image);
  bool (*has_frame)(void *ctx, void *image);
  /* persistence recipe resource writer (owned tree). */
  qa_save_value *(*write_resource)(void *ctx, qa_resolved_resource_reference resource);
} qa_app_q3_cinematics_host;
/* TS UiCinematicAsset. */
typedef struct qa_app_ui_cinematic_asset { const char *path; } qa_app_ui_cinematic_asset;
/* TS ApplicationQ3Cinematics. system NULL = TS undefined. */
typedef struct qa_app_application_q3_cinematics qa_app_application_q3_cinematics;
qa_app_application_q3_cinematics *qa_app_application_q3_cinematics_create(
    qa_content_mounted *mounts, qa_app_q3_cinematics_host host,
    void (*print)(void *ctx, const char *text), void *print_ctx,
    qa_cinematic_mixer mixer, qa_seat_id seat,
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
