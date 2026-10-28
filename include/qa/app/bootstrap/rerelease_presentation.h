/* Port of src/app/bootstrap/rerelease-presentation.ts. Fog
 * (rerelease-presentation/fog.ts), ApplicationAssets providers
 * (assets.ts) and SimulationPresentationEvent (simulation/types.ts)
 * are sibling slices: mirrored as host fn+ctx / field mirrors here.
 * q2PlayerNameTokens and seat routing are real. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_RERELEASE_PRESENTATION_H
#define QA_APP_BOOTSTRAP_RERELEASE_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/rerelease_presentation/fog.h"
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
/* Mirror: TS SimulationPresentationEvent fields read here. */
typedef struct qa_app_rerelease_event {
  const char *kind; const char *event_kind;
  qa_content_id content; double seconds; double sequence;
  bool has_recipient; qa_actor_id recipient;
  qa_actor_id actor, item, target;
  bool visible;
  qa_q2_fog_state fog_value; double fog_transition_milliseconds;
  const char *text, *name;
  const char *const *args; size_t arg_count;
  double sky_rotation; bool sky_auto_rotate; qa_vec3 sky_axis;
  double slot; const char *userinfo_name;
  bool has_actor; double level;
  bool has_source_entity; double source_entity;
} qa_app_rerelease_event;
/* A localized q2-player print produced by prepare(). */
typedef struct qa_app_rerelease_print {
  qa_content_id content; double seconds, sequence;
  bool has_recipient; qa_actor_id recipient;
  qa_actor_id target; double level; const char *text; /* owned copy */
} qa_app_rerelease_print;
typedef struct qa_app_rerelease_host {
  void *ctx;
  /* NativeLanguageSettings catalog mirror. */
  const void *(*catalog)(void *ctx, qa_seat_id seat, const char *language, qa_content_id content);
  void (*catalogs_clear)(void *ctx, qa_seat_id seat);
  char *(*localized_text)(void *ctx, const void *catalog, const char *text, const char *const *args, size_t arg_count);
  bool (*language_binding)(void *ctx, const void *catalog, qa_ui_setting_binding *out);
  /* ApplicationAssets provider mirror. */
  const char *(*provider_family)(void *ctx, qa_content_id content);
  bool (*load_sky_image)(void *ctx, qa_content_id content, const char *path, qa_renderer_image *out);
  qa_renderer_image (*missing_image)(void *ctx, qa_content_id content);
} qa_app_rerelease_host;
typedef struct qa_app_rerelease_presentation qa_app_rerelease_presentation;
char *qa_app_q2_player_name_tokens(const char *text, const double *slots, const char *const *names, size_t count);
qa_app_rerelease_presentation *qa_app_rerelease_presentation_create(qa_app_rerelease_host host,
    const qa_app_rerelease_seat *seats, size_t seat_count, qa_cvar_registry *language_settings);
void qa_app_rerelease_presentation_destroy(qa_app_rerelease_presentation *p);
typedef struct qa_app_rerelease_refresh qa_app_rerelease_refresh;
qa_app_rerelease_refresh *qa_app_rerelease_prepare_image_refresh(qa_app_rerelease_presentation *p);
void qa_app_rerelease_refresh_publish(qa_app_rerelease_refresh *refresh);
void qa_app_rerelease_publish_seats(qa_app_rerelease_presentation *p, const qa_app_rerelease_seat *seats, size_t seat_count);
void qa_app_rerelease_receive(qa_app_rerelease_presentation *p, const qa_app_rerelease_event *events, size_t count);
const char *qa_app_rerelease_selected_language(qa_app_rerelease_presentation *p, qa_seat_id id);
bool qa_app_rerelease_item_visible(qa_app_rerelease_presentation *p, qa_actor_id actor, qa_actor_id item);
/* TS sourceLocalizer returns a closure: localize via this call. Malloc'd. */
char *qa_app_rerelease_source_localize(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count);
char *qa_app_rerelease_localize_message(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content, const char *text, const char *const *args, size_t arg_count);
/* False = TS undefined (non-q1/q2 family). */
bool qa_app_rerelease_language_binding(qa_app_rerelease_presentation *p, qa_seat_id id, qa_content_id content,
                                       void (*failed)(void *ctx, const char *error), void *failed_ctx, qa_ui_setting_binding *out);
void qa_app_rerelease_prepare(qa_app_rerelease_presentation *p);
const qa_app_rerelease_print *qa_app_rerelease_drain_prints(qa_app_rerelease_presentation *p, size_t *out_count);
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
