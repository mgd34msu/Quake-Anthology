/* Port of src/app/bootstrap/demo-local-recording.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/demo_local_recording.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_local_demo_recording {
  qa_app_local_demo_recording_host host;
  bool has_sink;
  qa_app_demo_recording_sink sink;
  bool has_prepared;
  qa_app_local_recording_source prepared_source;
  qa_app_demo_recording_seed prepared_seed;
};
qa_app_local_demo_recording *qa_app_local_demo_recording_create(qa_app_local_demo_recording_host host) {
  qa_app_local_demo_recording *recording = calloc(1, sizeof *recording);
  if (!recording) return NULL;
  recording->host = host;
  return recording;
}
void qa_app_local_demo_recording_destroy(qa_app_local_demo_recording *recording) { free(recording); }
qa_app_demo_recording_seed qa_app_local_demo_recording_seed(qa_app_local_demo_recording *recording) {
  qa_app_local_recording_source source = recording->host.read(recording->host.ctx);
  char reasons[2048] = "";
  if (!recording->host.supports_wire(recording->host.ctx, source, reasons, sizeof reasons))
    qa_range_error("%s", reasons);
  qa_app_demo_recording_seed seed = {0};
  const char *error = recording->host.build_seed(recording->host.ctx, source, &seed);
  if (error) qa_range_error("%s", error);
  recording->has_prepared = true;
  recording->prepared_source = source;
  recording->prepared_seed = seed;
  return seed;
}
const void *qa_app_local_demo_recording_attach(qa_app_local_demo_recording *recording, qa_app_demo_recording_sink sink) {
  if (recording->has_sink || !recording->has_prepared)
    qa_range_error("Local recording requires an unattached prepared seed");
  recording->sink = sink;
  recording->has_sink = true;
  return sink.ctx;
}
void qa_app_local_demo_recording_detach(qa_app_local_demo_recording *recording, const void *token) {
  if (recording->has_sink && recording->sink.ctx == token) recording->has_sink = false;
}
void qa_app_local_demo_recording_check_qw_payload(size_t length) {
  if (length > 1442) qa_range_error("QWD message exceeds native packet capacity");
}
const char *qa_app_local_demo_recording_publish(qa_app_local_demo_recording *recording, const void *output, const void *events, size_t event_count) {
  if (!recording->has_sink) return NULL;
  qa_app_local_recording_source source = recording->host.read(recording->host.ctx);
  if (!recording->has_prepared) return "Local recording lost its seed";
  if (recording->prepared_source.world != source.world) {
    qa_app_demo_recording_identity before = recording->prepared_seed.identity;
    qa_app_demo_recording_seed seed = qa_app_local_demo_recording_seed(recording);
    if (seed.identity.kind != before.kind || seed.identity.protocol != before.protocol ||
        seed.identity.track != before.track)
      return "Local recording source protocol changed";
    for (size_t i = 0; i < seed.packet_count; i++) {
      const char *error = recording->sink.append(recording->sink.ctx, seed.packets[i]);
      if (error) return error;
    }
  } else if (strcmp(recording->prepared_source.kind, source.kind) != 0) {
    return "Local recording source changed without a world boundary";
  }
  return recording->host.publish_frame(recording->host.ctx, source, output, events, event_count, recording->sink);
}
