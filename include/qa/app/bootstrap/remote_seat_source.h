/* Port of src/app/bootstrap/remote-seat-source.ts (RemoteSeatSource, the
 * RemoteSeatPresentation / RemoteSeatNetwork unions and RemoteSeatSourceHooks).
 *
 * The five family classes are the typed C ports in app/bootstrap/network
 * (UnifiedRemotePresentation, Q1/Qw/Q2/Q3RemotePresentation and their client
 * networks). Their options take C-only content accessors (recipe, world, scene
 * queries, identity) that TS reads from LoadedApplicationContent directly; the
 * source fills them from qa_app_remote_access (remote_access.h).
 *
 * Async members are synchronous; a TS throw is qa_raise("Error", ...).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_SEAT_SOURCE_H
#define QA_APP_BOOTSTRAP_REMOTE_SEAT_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/frame_clock.h"
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/q1_client.h"
#include "qa/app/bootstrap/network/q2.h"
#include "qa/app/bootstrap/network/q3_client.h"
#include "qa/app/bootstrap/network/qw_client.h"
#include "qa/app/bootstrap/network/remote.h"
#include "qa/app/bootstrap/network/remote_q1.h"
#include "qa/app/bootstrap/network/remote_q3.h"
#include "qa/app/bootstrap/network/remote_qw.h"
#include "qa/app/bootstrap/network/remote_unified.h"
#include "qa/app/bootstrap/network/transport.h"
#include "qa/app/bootstrap/network/unified_client.h"
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/identity.h"
#include "qa/core/cvars/index.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/q3/client.h"
#include "qa/world/collision/index.h"
#include "qa/world/session/session.h"

/* The family tag of RemoteSeatPresentation and RemoteSeatNetwork. */
typedef enum qa_app_remote_seat_family {
  QA_APP_REMOTE_FAMILY_UNIFIED, QA_APP_REMOTE_FAMILY_Q1, QA_APP_REMOTE_FAMILY_QW, QA_APP_REMOTE_FAMILY_Q2,
  QA_APP_REMOTE_FAMILY_Q3
} qa_app_remote_seat_family;

/* TS RemoteSeatPresentation: exactly the member named by family is set. */
typedef struct qa_app_remote_seat_presentation {
  qa_app_remote_seat_family family;
  qa_app_unified_remote *unified;
  qa_app_q1_remote *q1;
  qa_app_qw_remote *qw;
  qa_app_q2_remote *q2;
  qa_app_q3_remote *q3;
} qa_app_remote_seat_presentation;

/* TS RemoteSeatNetwork: exactly the member named by family is set. */
typedef struct qa_app_remote_seat_network {
  qa_app_remote_seat_family family;
  qa_app_unified_client_network *unified;
  qa_app_q1_client_network *q1;
  qa_app_qw_client_network *qw;
  qa_app_q2_client_network *q2;
  qa_app_q3_client_network *q3;
} qa_app_remote_seat_network;

/* The members the union types share, dispatched on family. */
qa_client_id qa_app_remote_seat_presentation_client(const qa_app_remote_seat_presentation *remote);
/* TS remote.player?.actor: false is TS null. */
bool qa_app_remote_seat_presentation_player(const qa_app_remote_seat_presentation *remote, qa_actor_id *out);
/* TS remote.scene (raises "Remote server has not supplied a world" before the first world). */
qa_shared_scene_queries *qa_app_remote_seat_presentation_scene(const qa_app_remote_seat_presentation *remote);
bool qa_app_remote_seat_presentation_is_player(const qa_app_remote_seat_presentation *remote, qa_actor_id actor);
/* TS remote.output: NULL is TS null. */
const qa_simulation_output *qa_app_remote_seat_presentation_output(const qa_app_remote_seat_presentation *remote);
const qa_app_simulation_presentation *qa_app_remote_seat_presentation_presentations(
    const qa_app_remote_seat_presentation *remote, size_t *out_count);
const qa_q3_character_view *qa_app_remote_seat_presentation_character_views(
    const qa_app_remote_seat_presentation *remote, size_t *out_count);
/* TS drainPresentationEvents for the Q1/QW/Q2/Q3 presentations (rows borrowed until the next drain).
 * The unified presentation drains through qa_app_unified_remote_drain_presentation_events. */
const qa_app_sim_presentation_event *qa_app_remote_seat_presentation_drain_events(
    const qa_app_remote_seat_presentation *remote, size_t *out_count);
qa_app_sim_player_view qa_app_remote_seat_presentation_player_view(const qa_app_remote_seat_presentation *remote,
    qa_actor_id actor);
const qa_world_text *qa_app_remote_seat_presentation_world_text(const qa_app_remote_seat_presentation *remote,
    size_t *out_count);
/* TS remote.disconnected(reason) / print(text). */
void qa_app_remote_seat_presentation_disconnected(const qa_app_remote_seat_presentation *remote, const char *reason);
void qa_app_remote_seat_presentation_destroy(qa_app_remote_seat_presentation *remote);

qa_app_network_phase qa_app_remote_seat_network_phase(const qa_app_remote_seat_network *network);
void qa_app_remote_seat_network_poll(const qa_app_remote_seat_network *network, double now);
void qa_app_remote_seat_network_command(const qa_app_remote_seat_network *network, const char *text);
void qa_app_remote_seat_network_submit(const qa_app_remote_seat_network *network, const qa_actor_command *commands,
    size_t count, double now);
void qa_app_remote_seat_network_close(const qa_app_remote_seat_network *network);
void qa_app_remote_seat_network_destroy(qa_app_remote_seat_network *network);

/* TS RemoteSeatSourceHooks. ctx is passed to every hook. A raise from a hook
 * propagates as the TS rejection. assert_current hooks raise when cancelled. */
typedef void (*qa_app_remote_assert_fn)(void *ctx);
typedef struct qa_app_remote_seat_source_hooks {
  void *ctx;
  const qa_app_options *(*options)(void *ctx);
  qa_content_mounted *(*mounts)(void *ctx);
  qa_app_loaded_content *(*content)(void *ctx);
  qa_app_remote_content_mounts *(*current_content)(void *ctx); /* NULL is TS null */
  qa_app_remote_content_mounts *(*select_content)(void *ctx, const qa_remote_content_selection *selection,
      qa_app_remote_assert_fn assert_current, void *assert_ctx);
  qa_app_remote_content_mounts *(*refresh_content)(void *ctx, qa_app_remote_assert_fn assert_current, void *assert_ctx);
  qa_app_loaded_content *(*load_q1_world)(void *ctx, const qa_app_q1_remote_world_info *world, bool refresh_content);
  qa_app_loaded_content *(*load_q2_world)(void *ctx, const qa_app_q2_game_state *state);
  qa_app_loaded_content *(*load_q3_world)(void *ctx, const qa_app_q1_remote_world_info *world,
      qa_q3_client_connection *connection, qa_app_loaded_content *prepared_content);
  void (*initialize_q3)(void *ctx, qa_q3_client_connection *connection);
  void (*shutdown_q3)(void *ctx);
  /* TS cinematic { start(name, ended), stop() } (ctx is this struct's ctx). */
  void (*cinematic_start)(void *ctx, const char *name, qa_app_q2_cinematic_ended_fn ended, void *ended_ctx);
  void (*cinematic_stop)(void *ctx);
  void (*publish)(void *ctx, const qa_simulation_output *output);
  void (*disconnected)(void *ctx, const char *reason);
  void (*print)(void *ctx, const char *text);
  bool (*recording)(void *ctx);
} qa_app_remote_seat_source_hooks;

/* TS Pick<UnifiedRemoteOptions, "loadContent" | "model" | "publishComponents">.
 * publish_components NULL is TS undefined. */
typedef struct qa_app_remote_seat_unified_hooks {
  void *ctx;
  qa_app_loaded_content *(*load_content)(void *ctx, const qa_app_unified_control *offer,
      qa_app_unified_operation_fn assert_current, void *assert_ctx);
  qa_decoded_model (*model)(void *ctx, const qa_app_unified_resource_key *key, bool has_brush_model, double brush_model);
  void (*publish_components)(void *ctx, qa_app_unified_operation_fn operation, void *operation_ctx);
} qa_app_remote_seat_unified_hooks;

/* TS RemoteSeatSourceOptions = RemoteSeatSourceCommon & family extras. The
 * identity, session, seat, clock and transport are borrowed for the source's
 * life; address is copied. */
typedef struct qa_app_remote_seat_source_options {
  const qa_identity_owner *identity;
  qa_engine_session *session;
  qa_session_seat *seat;
  qa_cvar_registry *(*cvars)(void *ctx); void *cvars_ctx;
  qa_app_presentation_time *clock;
  qa_app_application_transport *transport;
  qa_net_address address;
  double qport;
  const qa_app_client_download_permission *download_permission; /* NULL is TS null */
  qa_app_remote_seat_source_hooks hooks;
  qa_app_remote_seat_family family;
  qa_app_remote_seat_unified_hooks unified;  /* family unified */
  qa_q2_protocol_identity protocol;          /* family q2 */
  qa_app_q3_authorization authorization;     /* family q3 (Pick<Q3ClientAuthorization, "request">) */
} qa_app_remote_seat_source_options;

typedef struct qa_app_remote_seat_source qa_app_remote_seat_source;
/* TS new RemoteSeatSource(owner). Raises the TS constructor errors ("QuakeWorld
 * requires UDP", "Native Q3 remote requires IPv4", "Q2 remote client has no
 * download policy"). */
qa_app_remote_seat_source *qa_app_remote_seat_source_create(const qa_app_remote_seat_source_options *owner);
/* Frees a source (closing it first when still open). */
void qa_app_remote_seat_source_destroy(qa_app_remote_seat_source *source);
/* TS readonly remote / network. */
const qa_app_remote_seat_presentation *qa_app_remote_seat_source_remote(const qa_app_remote_seat_source *source);
const qa_app_remote_seat_network *qa_app_remote_seat_source_network(const qa_app_remote_seat_source *source);
double qa_app_remote_seat_source_qport(const qa_app_remote_seat_source *source);
qa_app_application_transport *qa_app_remote_seat_source_transport(const qa_app_remote_seat_source *source);
void qa_app_remote_seat_source_set_all_skins(qa_app_remote_seat_source *source, const char *value);
/* TS pureCommand(serverId): malloc'd, NULL is TS null. */
char *qa_app_remote_seat_source_pure_command(qa_app_remote_seat_source *source, double server_id);
/* TS downloadProgress: rows borrowed until the next call. */
const qa_app_client_download_progress *qa_app_remote_seat_source_download_progress(qa_app_remote_seat_source *source,
    size_t *out_count);
void qa_app_remote_seat_source_cancel_downloads(qa_app_remote_seat_source *source);
void qa_app_remote_seat_source_retry_downloads(qa_app_remote_seat_source *source);
/* TS close(): idempotent; a second close returns after the first. */
void qa_app_remote_seat_source_close(qa_app_remote_seat_source *source);
#endif
