/* Port of src/app/bootstrap/network/q2-demo.ts. GTV shares the
 * header/presentation types through gtv_source.h. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_DEMO_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_DEMO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/network/q2/demo.h"
#include "qa/network/q2/mvd_playback.h"
#include "qa/network/q2/mvd_profile.h"
typedef struct qa_app_q2_playback_header {
  bool is_mvd;
  qa_q2_demo_header dm2;          /* when !is_mvd */
  qa_q2_protocol_identity protocol; /* mvd */
  qa_q2_server_data_result data;    /* mvd (also in dm2) */
  qa_q2_mvd_header profile;         /* mvd; config strings owned */
} qa_app_q2_playback_header;
void qa_app_q2_read_playback_header(const uint8_t *bytes, size_t length,
    qa_app_q2_playback_header *out);
void qa_app_q2_playback_header_free(qa_app_q2_playback_header *header);
typedef struct qa_app_q2_mvd_presentation {
  qa_q2_mvd_visibility visibility;
  void *ctx;
  void (*select_view)(void *ctx, double clientnum);
} qa_app_q2_mvd_presentation;
typedef struct qa_app_q2_demo_advance {
  const char *kind; /* frame|eof|disconnected|closed */
  double time_milliseconds;
} qa_app_q2_demo_advance;
typedef struct qa_app_q2_demo_playback qa_app_q2_demo_playback;
/* presentation NULL requires DM2 bytes. select_recorded_view NULL
 * is TS undefined. Bytes are borrowed. */
qa_app_q2_demo_playback *qa_app_q2_demo_playback_create(
    const uint8_t *bytes, size_t length, qa_app_q2_client_host host,
    const qa_app_q2_mvd_presentation *presentation,
    void (*select_recorded_view)(void *ctx, double clientnum), void *select_ctx);
void qa_app_q2_demo_playback_destroy(qa_app_q2_demo_playback *playback);
qa_app_q2_client_receiver *qa_app_q2_demo_playback_receiver(qa_app_q2_demo_playback *playback);
void qa_app_q2_demo_playback_select_player(qa_app_q2_demo_playback *playback, double clientnum);
bool qa_app_q2_demo_playback_recorded_time(qa_app_q2_demo_playback *playback, double *out);
double qa_app_q2_demo_playback_consumed_bytes(const qa_app_q2_demo_playback *playback);
qa_app_q2_demo_advance qa_app_q2_demo_playback_advance(
    qa_app_q2_demo_playback *playback, double target_milliseconds);
qa_app_q2_demo_advance qa_app_q2_demo_playback_next_frame(qa_app_q2_demo_playback *playback);
void qa_app_q2_demo_playback_close(qa_app_q2_demo_playback *playback);
#endif
