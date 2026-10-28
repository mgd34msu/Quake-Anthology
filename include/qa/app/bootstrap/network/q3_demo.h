/* Port of src/app/bootstrap/network/q3-demo.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_DEMO_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_DEMO_H
#include <stdbool.h>
#include "qa/app/bootstrap/network/recorded_source.h"
#include "qa/app/bootstrap/network/q3_client.h"
#include "qa/network/q3/clock.h"
#include "qa/network/q3/demo.h"
typedef struct qa_app_q3_demo_frame {
  const char *kind; /* pending|frame|end */
  double server_time;
  qa_q3_demo_result end; bool has_end;
  bool has_timing; qa_app_demo_timing timing;
} qa_app_q3_demo_frame;
typedef struct qa_app_q3_demo_playback_options {
  qa_app_q3_client_host host;
  qa_q3_client_clock *clock;   /* borrowed */
  qa_q3_demo_reader *reader;   /* borrowed */
} qa_app_q3_demo_playback_options;
typedef struct qa_app_q3_demo_playback qa_app_q3_demo_playback;
qa_app_q3_demo_playback *qa_app_q3_demo_playback_create(qa_app_q3_demo_playback_options options);
void qa_app_q3_demo_playback_destroy(qa_app_q3_demo_playback *playback);
qa_q3_client_connection *qa_app_q3_demo_playback_connection(qa_app_q3_demo_playback *playback);
const qa_app_q3_demo_playback_options *qa_app_q3_demo_playback_options_of(
    const qa_app_q3_demo_playback *playback);
const char *qa_app_q3_demo_playback_phase(const qa_app_q3_demo_playback *playback);
/* NULL when no end yet; borrowed result otherwise. */
const qa_q3_demo_result *qa_app_q3_demo_playback_prime(
    qa_app_q3_demo_playback *playback, double real_time);
qa_app_q3_demo_frame qa_app_q3_demo_playback_advance_frame(
    qa_app_q3_demo_playback *playback, double real_time, qa_app_recorded_q3_options options);
void qa_app_q3_demo_playback_close(qa_app_q3_demo_playback *playback);
#endif
