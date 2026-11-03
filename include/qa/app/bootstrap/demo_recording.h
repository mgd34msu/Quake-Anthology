/* Port of src/app/bootstrap/demo-recording.ts. Promise write chain
 * becomes synchronous ordered FILE writes (error string or NULL).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_RECORDING_H
#define QA_APP_BOOTSTRAP_DEMO_RECORDING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q1/demos.h"
/* TS DemoRecordingPacket/Identity kinds. */
typedef enum qa_app_demo_recording_kind {
  QA_APP_RECORDING_Q2_SERVER, QA_APP_RECORDING_MVD, QA_APP_RECORDING_Q1,
  QA_APP_RECORDING_QW, QA_APP_RECORDING_Q2, QA_APP_RECORDING_Q3
} qa_app_demo_recording_kind;
typedef struct qa_app_demo_recording_identity {
  qa_app_demo_recording_kind kind;
  double protocol; /* 34 | revision | 15/666/999 | 28 | 68 */
  qa_q2_protocol_identity q2_protocol; /* kind q2 */
  double track; /* kind q1 */
} qa_app_demo_recording_identity;
typedef struct qa_app_demo_recording_packet {
  qa_app_demo_recording_kind kind;
  const uint8_t *message; size_t message_length; /* message kinds */
  qa_vec3 view_angles; /* q1 */
  qa_quakeworld_demo_record record; /* qw (borrowed message) */
  double sequence; /* q3 */
} qa_app_demo_recording_packet;
typedef struct qa_app_demo_recording_seed {
  qa_app_demo_recording_identity identity;
  const qa_app_demo_recording_packet *packets; size_t packet_count;
} qa_app_demo_recording_seed;
/* TS DemoRecordingSink. append returns error or NULL. */
typedef struct qa_app_demo_recording_sink {
  void *ctx;
  const char *(*append)(void *ctx, qa_app_demo_recording_packet packet);
} qa_app_demo_recording_sink;
typedef struct qa_app_demo_recording qa_app_demo_recording;
/* Malloc'd; caller frees. Aborts via normalizeResourcePath as TS. */
char *qa_app_recording_path(const char *root, const char *name, qa_app_demo_recording_identity identity);
/* TS DemoRecording.open: *out_error set (static text) on failure, NULL return. */
/* TS open(root, name, seed, replace = false): replace opens "w" (a typed name overwrites), else "wx". */
qa_app_demo_recording *qa_app_demo_recording_open(const char *root, const char *name, qa_app_demo_recording_seed seed, bool replace,
                                                  const char **out_error);
const char *qa_app_demo_recording_path(const qa_app_demo_recording *recording);
qa_app_demo_recording_identity qa_app_demo_recording_identity_of(const qa_app_demo_recording *recording);
/* Sink view of the recording itself (append forwards, errors as strings). */
qa_app_demo_recording_sink qa_app_demo_recording_sink_of(qa_app_demo_recording *recording);
const char *qa_app_demo_recording_append(qa_app_demo_recording *recording, qa_app_demo_recording_packet packet);
const char *qa_app_demo_recording_stop(qa_app_demo_recording *recording);
const char *qa_app_demo_recording_abort(qa_app_demo_recording *recording);
void qa_app_demo_recording_destroy(qa_app_demo_recording *recording);
#endif
