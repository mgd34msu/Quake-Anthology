/* Port of src/app/bootstrap/remote-seat-view.ts. WorldSeatPresentation /
 * ApplicationAssets/Effects/Q3Client/SeatUi (sibling bootstrap slices)
 * are host fn+ctx; lifecycle, Q3 guest guard, view-angle decode and
 * frame preparation ordering are kept here. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEAT_VIEW_H
#define QA_APP_BOOTSTRAP_REMOTE_SEAT_VIEW_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/remote_seat_source.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/core/cvars/index.h"
#include "qa/world/session/session.h"
typedef struct qa_app_remote_seat_view_host {
  void *ctx;
  qa_session_seat *seat;
  bool remote_is_unified, remote_is_q3;
  bool owns_assets; /* content !== assets.content */
  bool character_is_q3;
  bool has_q3_guest;
  /* Q3 guest command angles (raw int16 triples); false = TS null command. */
  bool (*q3_command_angles)(void *ctx, int32_t out_angles[3]);
  void (*set_view_angles)(void *ctx, double x, double y, double z);
  const char *(*load_assets_world)(void *ctx);
  const char *(*load_console_font)(void *ctx);
  const char *(*load_menu_typography)(void *ctx);
  const char *(*preload_effects)(void *ctx); /* prints optional skips via print */
  const char *(*create_ui)(void *ctx);
  const char *(*create_q3_client)(void *ctx);
  const char *(*load_q3_character)(void *ctx);
  const char *(*create_presentation)(void *ctx);
  void (*publish_layout)(void *ctx, double index, double count);
  void (*validate_presentation)(void *ctx);
  void (*attach_presentation)(void *ctx);
  bool (*seat_presentation_is_this)(void *ctx);
  void (*clear_seat_presentation)(void *ctx);
  void (*close_presentation)(void *ctx);
  void (*close_ui)(void *ctx);
  void (*close_effects)(void *ctx);
  void (*close_assets)(void *ctx);
  const char *(*shutdown_q3_client)(void *ctx);
  /* Frame preparation pieces. */
  void *const *(*drain_presentation_events)(void *ctx, size_t *out_count);
  void *const *(*drain_simulation_events)(void *ctx, size_t *out_count);
  void (*seat_receive)(void *ctx, void *const *events, size_t count);
  const void *const *(*remote_presentations)(void *ctx, size_t *out_count);
  const void *const *(*remote_character_views)(void *ctx, size_t *out_count);
  void (*effects_receive)(void *ctx, void *const *events, size_t count);
  const char *(*effects_prepare)(void *ctx, qa_world_snapshot snapshot);
  /* Unhandled effect reporting: key is "content:reason". */
  size_t (*effects_drain_unhandled)(void *ctx, const char ***out_keys, const char ***out_kinds, const char ***out_reasons);
  void (*presentation_source_events)(void *ctx, void *const *events, size_t count);
  const char *(*presentation_prepare)(void *ctx, qa_world_snapshot snapshot);
  const void *(*remote_scene)(void *ctx);
  qa_actor_id (*player_actor)(void *ctx);
  double (*seat_index)(void *ctx);
  const void *const *(*effects_drain_sounds)(void *ctx, size_t *out_count);
  size_t (*effects_drain_recipient_sounds)(void *ctx, qa_actor_id actor, const void ***out_sounds);
  double (*count)(void *ctx);
  double (*index)(void *ctx);
  void (*assert_current)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_app_remote_seat_view_host;
/* TS ApplicationAudioSeatEvents subset returned by prepareFrame. */
typedef struct qa_app_remote_seat_audio_events {
  qa_seat_id seat;
  qa_world_snapshot snapshot;
  void *const *events; size_t event_count; /* borrowed */
  const void *scene; bool music;
  const void *const *effect_sounds; size_t effect_sound_count; /* borrowed */
} qa_app_remote_seat_audio_events;
typedef struct qa_app_remote_seat_view qa_app_remote_seat_view;
qa_app_remote_seat_view *qa_app_remote_seat_view_prepare(qa_app_remote_seat_view_host host, qa_app_remote_seat_source *source);
void qa_app_remote_seat_view_destroy(qa_app_remote_seat_view *v);
void qa_app_remote_seat_view_publish(qa_app_remote_seat_view *v, double index, double count);
qa_app_remote_seat_audio_events qa_app_remote_seat_view_prepare_frame(qa_app_remote_seat_view *v, qa_simulation_output output, bool music);
void qa_app_remote_seat_view_close(qa_app_remote_seat_view *v);
#endif
