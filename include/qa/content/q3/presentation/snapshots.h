/* Port of src/content/q3/presentation/snapshots.ts. GPL-2.0-or-later.
 * Snapshot transitions from id Software's code/cgame/cg_snapshot.c and
 * the CL_GetSnapshot boundary from code/client/cl_cgame.c.
 * TS Snapshot is the landed wire qa_q3_wire_snapshot; SourcePlayerState
 * is the landed base/shared record. TS async host calls are synchronous.
 * SnapshotRuntime owns the retail snapshots it publishes into
 * state snap/next_snap (destroy frees them and clears the pointers). */
#ifndef QA_CONTENT_Q3_PRESENTATION_SNAPSHOTS_H
#define QA_CONTENT_Q3_PRESENTATION_SNAPSHOTS_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/retail_snapshot.h"
#include "qa/content/q3/presentation/state.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/snapshot_history.h"
typedef struct qa_q3_pres_snapshot_current { double number, server_time; } qa_q3_pres_snapshot_current;
/* TS SnapshotSource. read false is TS null; on true out is caller-owned
 * (destroy with qa_q3_wire_snapshot_destroy). */
typedef struct qa_q3_pres_snapshot_source {
  void *ctx;
  qa_q3_pres_snapshot_current (*current)(void *ctx);
  bool (*read)(void *ctx, double number, qa_q3_wire_snapshot *out);
} qa_q3_pres_snapshot_source;
/* TS HistorySnapshotSource. history is borrowed;
 * parse_entities_number/debug_print are the constructor closures. */
typedef struct qa_q3_pres_history_snapshot_source {
  qa_q3_snapshot_history *history;
  void *ctx;
  double (*parse_entities_number)(void *ctx);
  void (*debug_print)(void *ctx, const char *message);
} qa_q3_pres_history_snapshot_source;
void qa_q3_pres_history_snapshot_source_init(qa_q3_pres_history_snapshot_source *source,
    qa_q3_snapshot_history *history, void *ctx, double (*parse_entities_number)(void *ctx),
    void (*debug_print)(void *ctx, const char *message));
qa_q3_pres_snapshot_current qa_q3_pres_history_snapshot_source_current(qa_q3_pres_history_snapshot_source *source);
bool qa_q3_pres_history_snapshot_source_read(qa_q3_pres_history_snapshot_source *source, double number,
    qa_q3_wire_snapshot *out);
qa_q3_pres_snapshot_source qa_q3_pres_history_snapshot_source_as_source(qa_q3_pres_history_snapshot_source *source);
typedef struct qa_q3_pres_snapshot_host {
  qa_q3_pres_snapshot_source source;
  bool demo_playback, no_predict, synchronous_clients;
  void *ctx;
  void (*execute_server_commands)(void *ctx, double sequence);
  void (*respawn)(void *ctx);
  void (*reset_player_entity)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*check_events)(void *ctx, qa_q3_pres_client_entity *entity);
  void (*transition_player_state)(void *ctx, qa_q3_base_source_player_state *current,
                                  const qa_q3_base_source_player_state *previous);
  void (*lagometer_snapshot)(void *ctx, const qa_q3_wire_snapshot *snapshot); /* nullable */
  void (*warn)(void *ctx, const char *message);
} qa_q3_pres_snapshot_host;
typedef struct qa_q3_pres_snapshot_runtime {
  qa_q3_pres_client_game_state *state;
  qa_q3_pres_snapshot_host host;
  bool transitioning;
  qa_q3_pres_retail_snapshot snap_storage, next_snap_storage; /* owned when has_* */
  bool has_snap, has_next_snap;
} qa_q3_pres_snapshot_runtime;
void qa_q3_pres_snapshot_runtime_init(qa_q3_pres_snapshot_runtime *runtime,
    qa_q3_pres_client_game_state *state, qa_q3_pres_snapshot_host host);
/* Frees owned retail snapshots; clears state snap/next_snap if they
 * point into this runtime's storage. */
void qa_q3_pres_snapshot_runtime_destroy(qa_q3_pres_snapshot_runtime *runtime);
/* TS async setInitialSnapshot / transitionSnapshot / processSnapshots,
 * synchronous here. False only on OOM in retail conversion / list growth. */
bool qa_q3_pres_snapshot_runtime_set_initial_snapshot(qa_q3_pres_snapshot_runtime *runtime,
    const qa_q3_wire_snapshot *input);
bool qa_q3_pres_snapshot_runtime_transition_snapshot(qa_q3_pres_snapshot_runtime *runtime);
bool qa_q3_pres_snapshot_runtime_process_snapshots(qa_q3_pres_snapshot_runtime *runtime);
/* False only on OOM in retail conversion / list growth. */
bool qa_q3_pres_snapshot_runtime_set_next_snapshot(qa_q3_pres_snapshot_runtime *runtime,
    const qa_q3_wire_snapshot *input);
#endif
