/* Port of src/app/bootstrap/remote-seat-source.ts. The per-family
 * network/presentation classes live in app/bootstrap/network (sibling
 * slice, unlanded) and content.ts/frame-clock.ts (sibling slices):
 * mirrored as host fn+ctx. Family construction validation and the
 * download-owner state machine are kept here. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEAT_SOURCE_H
#define QA_APP_BOOTSTRAP_REMOTE_SEAT_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/network/common/endpoint.h"
#include "qa/world/session/session.h"
/* TS RemoteSeatPresentation / RemoteSeatNetwork unions: opaque + kind. */
typedef enum qa_app_remote_seat_family { QA_APP_REMOTE_FAMILY_UNIFIED, QA_APP_REMOTE_FAMILY_Q1, QA_APP_REMOTE_FAMILY_QW, QA_APP_REMOTE_FAMILY_Q2, QA_APP_REMOTE_FAMILY_Q3 } qa_app_remote_seat_family;
typedef struct qa_app_remote_seat_network_mirror {
  void *ctx; qa_app_remote_seat_family family;
  const char *(*phase)(void *ctx); /* "active"|"loading"|... */
  void (*poll)(void *ctx, double now_milliseconds);
  void (*command)(void *ctx, const char *text);
  void (*player_command)(void *ctx, const char *name, const char *const *args, size_t count);
  void (*component_command)(void *ctx, const void *source, double generation, const char *const *args, size_t count);
  void (*submit)(void *ctx, const void *const *commands, size_t count, double wall_now);
  void (*submit_timed)(void *ctx, const void *const *commands, size_t count, double wall_now, double frame_ms);
  void (*close)(void *ctx);
  double (*connect_packet_count)(void *ctx);
  void *(*native)(void *ctx); /* Q3ClientConnection* mirror for q3 */
  void (*send_packet)(void *ctx);
} qa_app_remote_seat_network_mirror;
typedef struct qa_app_remote_seat_presentation_mirror {
  void *ctx; qa_app_remote_seat_family family;
  qa_session_client *client;      /* borrowed */
  bool has_player; qa_actor_id player_actor;
  const void *scene;              /* borrowed opaque */
  void *const *(*drain_presentation_events)(void *ctx, size_t *out_count);
  void *const *(*drain_simulation_events)(void *ctx, size_t *out_count);
  const void *const *(*presentations)(void *ctx, size_t *out_count);
  const void *const *(*character_views)(void *ctx, size_t *out_count);
  bool (*is_player)(void *ctx, qa_actor_id actor);
  const char *const *(*download_progress)(void *ctx, size_t *out_count); /* q2 */
  void (*cancel_downloads)(void *ctx);
  void (*retry_downloads)(void *ctx);
} qa_app_remote_seat_presentation_mirror;
/* TS RemoteSeatSourceHooks: opaque content/world values stay void*. */
typedef struct qa_app_remote_seat_source_hooks {
  void *ctx;
  const void *(*options)(void *ctx);
  const void *(*mounts)(void *ctx);
  const void *(*content)(void *ctx);
  const void *(*current_content)(void *ctx);
  const void *(*select_content)(void *ctx, const char *product, const char *game_dir, void (*assert_current)(void *actx), void *actx);
  const void *(*refresh_content)(void *ctx, void (*assert_current)(void *actx), void *actx);
  const void *(*load_q1_world)(void *ctx, const void *world, bool refresh_content);
  const void *(*load_q2_world)(void *ctx, const void *state);
  const void *(*load_q3_world)(void *ctx, const void *world, const void *connection, const void *prepared_content);
  void (*initialize_q3)(void *ctx, const void *connection);
  void (*shutdown_q3)(void *ctx);
  void (*cinematic_start)(void *ctx, const char *name);
  void (*cinematic_stop)(void *ctx);
  void (*publish)(void *ctx, const void *output);
  void (*disconnected)(void *ctx, const char *reason);
  void (*print)(void *ctx, const char *text);
  bool (*recording)(void *ctx);
} qa_app_remote_seat_source_hooks;
/* TS RemoteSeatSourceOptions (common + family extras). */
typedef struct qa_app_remote_seat_source_options {
  qa_app_remote_seat_family family;
  qa_engine_session *session; qa_session_seat *seat;
  qa_cvar_registry *(*cvars)(void *ctx); void *cvars_ctx;
  const void *transport; /* borrowed opaque ApplicationTransport mirror */
  qa_net_address address;
  double qport;
  bool has_download_permission;
  bool (*download_permission)(void *ctx, const char *transport, const char *category); void *download_permission_ctx;
  qa_app_remote_seat_source_hooks hooks;
  /* Host factories for the family remote+network (network slice). */
  void *factory_ctx;
  qa_app_remote_seat_presentation_mirror (*make_remote)(void *ctx, qa_app_remote_seat_family family);
  qa_app_remote_seat_network_mirror (*make_network)(void *ctx, qa_app_remote_seat_family family);
  /* Download receivers (qw/q3, network slice mirrors). */
  bool (*prepare_qw_downloads)(void *ctx, const char *game_directory);
  bool (*prepare_q3_downloads)(void *ctx, const void *connection);
  const void *(*load_q3_world_prepared)(void *ctx, const void *world, const void *connection); /* fills pureCommand owner */
  char *(*pure_command)(void *ctx, double server_id);
  const char *const *(*qw_download_progress)(void *ctx, size_t *out_count);
  const char *const *(*q3_download_progress)(void *ctx, size_t *out_count);
  void (*cancel_qw_downloads)(void *ctx); void (*cancel_q3_downloads)(void *ctx);
  void (*retry_qw_downloads)(void *ctx); void (*retry_q3_downloads)(void *ctx);
  void (*close_qw_downloads)(void *ctx); void (*close_q3_downloads)(void *ctx);
  void (*close_q3_content)(void *ctx);
} qa_app_remote_seat_source_options;
typedef struct qa_app_remote_seat_source qa_app_remote_seat_source;
qa_app_remote_seat_source *qa_app_remote_seat_source_create(const qa_app_remote_seat_source_options *owner);
void qa_app_remote_seat_source_destroy(qa_app_remote_seat_source *s);
const qa_app_remote_seat_presentation_mirror *qa_app_remote_seat_source_remote(const qa_app_remote_seat_source *s);
const qa_app_remote_seat_network_mirror *qa_app_remote_seat_source_network(const qa_app_remote_seat_source *s);
double qa_app_remote_seat_source_qport(const qa_app_remote_seat_source *s);
const void *qa_app_remote_seat_source_transport(const qa_app_remote_seat_source *s);
void qa_app_remote_seat_source_set_all_skins(qa_app_remote_seat_source *s, const char *value);
const char *qa_app_remote_seat_source_all_skins(const qa_app_remote_seat_source *s);
char *qa_app_remote_seat_source_pure_command(qa_app_remote_seat_source *s, double server_id); /* NULL = TS null */
const char *const *qa_app_remote_seat_source_download_progress(qa_app_remote_seat_source *s, size_t *out_count);
void qa_app_remote_seat_source_cancel_downloads(qa_app_remote_seat_source *s);
void qa_app_remote_seat_source_retry_downloads(qa_app_remote_seat_source *s);
/* TS private require*Downloads surfaced for the network-slice wiring. */
void qa_app_remote_seat_source_require_qw_downloads(qa_app_remote_seat_source *s);
void qa_app_remote_seat_source_require_q3_downloads(qa_app_remote_seat_source *s);
void qa_app_remote_seat_source_prepare_qw_downloads(qa_app_remote_seat_source *s, const char *game_directory);
bool qa_app_remote_seat_source_prepare_q3_downloads(qa_app_remote_seat_source *s, const void *connection);
const void *qa_app_remote_seat_source_load_q3_world(qa_app_remote_seat_source *s, const void *world, const void *connection, double generation);
void qa_app_remote_seat_source_close(qa_app_remote_seat_source *s);
#endif
