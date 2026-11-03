/* Port of src/app/bootstrap/demo-local-recording.ts. The application
 * borrows its actual source host and publishes after that host's
 * observation; this file encodes the per-family wire records with the
 * landed network encoders. Async append chains become synchronous sink
 * calls; a sink error or a TS throw raises "Error" through qa_raise.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_LOCAL_RECORDING_H
#define QA_APP_BOOTSTRAP_DEMO_LOCAL_RECORDING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/demo_recording.h"
#include "qa/app/bootstrap/network/q1_types.h"
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/network/qw_server_types.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
/* TS LocalRecordingSource kind. */
typedef enum qa_app_local_recording_kind {
  QA_APP_LOCAL_RECORDING_Q1, QA_APP_LOCAL_RECORDING_QW,
  QA_APP_LOCAL_RECORDING_Q2, QA_APP_LOCAL_RECORDING_Q3
} qa_app_local_recording_kind;
/* TS LocalRecordingSource. world is compared by identity only. Host
 * pointers are borrowed for the duration of the call that read them;
 * only the fields of `kind` are read. Host payload conventions follow
 * the network modules: q1 game state info/signon/spawn/frame message
 * arrays are qa_netquake_message values, baselines a
 * qa_app_q1_baselines, entities qa_q1_extended_entity_state values; qw
 * signon is a qa_qw_signon_host, baselines and frame entities point to
 * qa_quakeworld_entity, frame messages to qa_quakeworld_message; q2
 * game state data/config strings/baselines are qa_q2_server_data_result
 * / qa_app_q2_config_snapshot / qa_q2_baseline_list, the frame a
 * qa_q2_wire_frame, events carry qa_q2_server_event; q3 product points
 * to a qa_q3_product, game state is a qa_q3_gamestate, snapshot player
 * and entities point to qa_q3_player_state_record /
 * qa_q3_entity_state_record. */
typedef struct qa_app_local_recording_source {
  qa_app_local_recording_kind kind;
  const void *world;
  qa_app_q1_server_host *q1_host; qa_app_q1_player q1_player;
  qa_app_qw_server_host *qw_host; qa_app_qw_player qw_player; double seconds; /* qw */
  qa_vec3 view_angles; /* q1, qw */
  qa_app_q2_server_host *q2_host; qa_app_q2_player q2_player; qa_q2_protocol_identity protocol;
  qa_app_q3_server_host *q3_host; qa_app_q3_player q3_player; double server_id;
  int snapshot_server_bit; /* 0 | 4 */
} qa_app_local_recording_source;
/* TS constructor's `read` closure. */
typedef qa_app_local_recording_source (*qa_app_local_recording_read_fn)(void *ctx);
typedef struct qa_app_local_demo_recording qa_app_local_demo_recording;
/* NULL only on allocation failure. */
qa_app_local_demo_recording *qa_app_local_demo_recording_create(
    qa_app_local_recording_read_fn read, void *read_ctx);
void qa_app_local_demo_recording_destroy(qa_app_local_demo_recording *recording);
/* TS seed(). The returned packets are owned by the recording and valid
 * until the next seed (also one made by publish) or destroy. Raises
 * "Error" with the joined wire admission reasons or the TS messages. */
qa_app_demo_recording_seed qa_app_local_demo_recording_seed(qa_app_local_demo_recording *recording);
/* TS attach(sink) and its returned detach closure: detach clears the
 * sink only while it is still the attached one (repeat calls are
 * no-ops). Raises "Local recording requires an unattached prepared seed". */
typedef struct qa_app_local_demo_recording_attachment {
  qa_app_local_demo_recording *recording;
  qa_app_demo_recording_sink sink;
} qa_app_local_demo_recording_attachment;
qa_app_local_demo_recording_attachment qa_app_local_demo_recording_attach(
    qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink);
void qa_app_local_demo_recording_detach(const qa_app_local_demo_recording_attachment *attachment);
/* TS publish(output, events). output is the borrowed SimulationOutput
 * the hosts receive; events are the frame's presentation events. */
void qa_app_local_demo_recording_publish(qa_app_local_demo_recording *recording,
    const void *output, const qa_app_sim_presentation_event *const *events, size_t event_count);
#endif
