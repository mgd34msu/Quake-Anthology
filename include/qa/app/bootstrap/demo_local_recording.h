/* Port of src/app/bootstrap/demo-local-recording.ts. Per-family wire
 * encoding (source hosts in network slices) is wired through the
 * host below; seed/attach/publish sequencing, identity checks and
 * the QW packet-size guard are kept here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_LOCAL_RECORDING_H
#define QA_APP_BOOTSTRAP_DEMO_LOCAL_RECORDING_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/demo_recording.h"
/* TS LocalRecordingSource discriminant + world identity. */
typedef struct qa_app_local_recording_source {
  const char *kind; /* "q1"|"qw"|"q2"|"q3" */
  const void *world;
  const void *host, *player;
} qa_app_local_recording_source;
typedef struct qa_app_local_demo_recording_host {
  void *ctx;
  qa_app_local_recording_source (*read)(void *ctx);
  /* supported false fills reasons text ("; "-joined by caller buffer). */
  bool (*supports_wire)(void *ctx, qa_app_local_recording_source source, char *reasons, size_t cap);
  /* Build the family seed packets (host keeps storage until publish). */
  const char *(*build_seed)(void *ctx, qa_app_local_recording_source source, qa_app_demo_recording_seed *out);
  /* Encode one published frame into packets appended to the sink. */
  const char *(*publish_frame)(void *ctx, qa_app_local_recording_source source, const void *output, const void *events, size_t event_count, qa_app_demo_recording_sink sink);
} qa_app_local_demo_recording_host;
typedef struct qa_app_local_demo_recording qa_app_local_demo_recording;
qa_app_local_demo_recording *qa_app_local_demo_recording_create(qa_app_local_demo_recording_host host);
void qa_app_local_demo_recording_destroy(qa_app_local_demo_recording *recording);
/* TS seed(): aborts with joined admission reasons / host errors. */
qa_app_demo_recording_seed qa_app_local_demo_recording_seed(qa_app_local_demo_recording *recording);
/* TS attach(): token released through _detach (identity-checked). */
const void *qa_app_local_demo_recording_attach(qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink);
void qa_app_local_demo_recording_detach(qa_app_local_demo_recording *recording, const void *token);
const char *qa_app_local_demo_recording_publish(qa_app_local_demo_recording *recording, const void *output, const void *events, size_t event_count);
/* TS qwPacket guard: payloads over 1442 abort with the RangeError. */
void qa_app_local_demo_recording_check_qw_payload(size_t length);
#endif
