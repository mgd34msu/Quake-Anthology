/* Port of src/app/bootstrap/rerelease-presentation.ts over the ApplicationAssets
 * providers (assets.ts: family, mounts, textures) and per-seat
 * NativeLanguageSettings catalogs (ui/settings/services.h) on the rerelease
 * localization files. Async is synchronous: a TS rejection raises (qa_raise).
 * drawStory takes the Draw2D/SeatTextPresentation calls it makes as fn+ctx.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_RERELEASE_PRESENTATION_H
#define QA_APP_BOOTSTRAP_RERELEASE_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/rerelease_presentation/fog.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/core/cvars/index.h"
#include "qa/render/scene/q2_sky.h"
#include "qa/ui/settings/index.h"
typedef struct qa_app_rerelease_seat {
  qa_seat_id seat; qa_actor_id actor; const char *language; /* NULL absent */
} qa_app_rerelease_seat;
typedef qa_q2_sky_view qa_app_rerelease_sky_view;
typedef struct qa_app_rerelease_presentation qa_app_rerelease_presentation;
char *qa_app_q2_player_name_tokens(const char *text, const double *slots, const char *const *names, size_t count);
/* TS new ApplicationRereleasePresentation(assets, seats, languageSettings = null): assets and language_settings
 * (NULL is TS null) are borrowed for the presentation's life; seat languages are copied. */
qa_app_rerelease_presentation *qa_app_rerelease_presentation_create(qa_app_application_assets *assets,
    const qa_app_rerelease_seat *seats, size_t seat_count, qa_cvar_registry *language_settings);
void qa_app_rerelease_presentation_destroy(qa_app_rerelease_presentation *p);
typedef struct qa_app_rerelease_refresh qa_app_rerelease_refresh;
/* TS prepareImageRefresh(images): the skies load through the prepared images' providers. */
qa_app_rerelease_refresh *qa_app_rerelease_prepare_image_refresh(qa_app_rerelease_presentation *p, qa_app_prepared_images *images);
void qa_app_rerelease_refresh_publish(qa_app_rerelease_refresh *refresh);
/* Frees an unpublished refresh (TS drops the unused sky closure). */
void qa_app_rerelease_refresh_discard(qa_app_rerelease_refresh *refresh);
void qa_app_rerelease_publish_seats(qa_app_rerelease_presentation *p, const qa_app_rerelease_seat *seats, size_t seat_count);
/* Retains owned copies of the events prepare() consumes. */
void qa_app_rerelease_receive(qa_app_rerelease_presentation *p, const qa_app_sim_presentation_event *events, size_t count);
const char *qa_app_rerelease_selected_language(qa_app_rerelease_presentation *p, qa_seat_id id);
bool qa_app_rerelease_item_visible(qa_app_rerelease_presentation *p, qa_actor_id actor, qa_actor_id item);
/* TS sourceLocalizer(id, content): the returned localizer keeps the catalog current at the call (a later language
 * change does not reach it), valid while the presentation lives. Its text is malloc'd. */
typedef struct qa_app_rerelease_localizer qa_app_rerelease_localizer;
qa_app_rerelease_localizer *qa_app_rerelease_source_localizer(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content);
char *qa_app_rerelease_localizer_text(qa_app_rerelease_localizer *localizer, const char *text, const char *const *args, size_t arg_count);
/* sourceLocalizer(id, content) applied once (a fresh closure per call). Malloc'd. */
char *qa_app_rerelease_source_localize(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count);
char *qa_app_rerelease_localize_message(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count);
/* False = TS undefined (non-q1/q2 family). The binding (read/write the seat language; write raises "Language is not
 * installed" and reloads the catalog, reporting a load failure to failed as String(error), "<name>: <message>") stays
 * valid while the presentation lives. */
bool qa_app_rerelease_language_binding(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content,
                                       void (*failed)(void *ctx, const char *error), void *failed_ctx, qa_ui_setting_binding *out);
void qa_app_rerelease_prepare(qa_app_rerelease_presentation *p);
/* TS drainPrints: owned q2-player print events, valid until the next
 * drain or destroy. */
const qa_app_sim_presentation_event *qa_app_rerelease_drain_prints(qa_app_rerelease_presentation *p, size_t *out_count);
bool qa_app_rerelease_story_active(qa_app_rerelease_presentation *p, qa_actor_id actor);
typedef struct qa_app_rerelease_view {
  bool has_fog; qa_q2_fog_operation fog;
  const qa_q2_sky_view *sky; /* NULL absent; borrowed from presentation */
} qa_app_rerelease_view;
qa_app_rerelease_view qa_app_rerelease_view_of(qa_app_rerelease_presentation *p, qa_actor_id actor, double seconds);
/* TS drawStory over Draw2D/SeatTextPresentation mirrors. */
typedef struct qa_app_rerelease_draw {
  void *ctx;
  double width, height;
  qa_seat_id text_seat;
  void (*layout)(void *ctx, const char *text, double scale, double *out_width, double *out_height);
  void (*draw)(void *ctx, const char *text, double scale, double max_width, double x, double y);
} qa_app_rerelease_draw;
void qa_app_rerelease_draw_story(qa_app_rerelease_presentation *p, qa_actor_id actor, qa_app_rerelease_draw draw, double scale);
#endif
