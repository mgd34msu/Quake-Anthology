/* Port of src/network/q3/clock.ts (CL_SetCGameTime). GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_CLOCK_H
#define QA_NETWORK_Q3_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/network/q3/server_message.h"
/* TS Q3ClockOptions. */
typedef struct qa_q3_clock_options {
  bool paused; double time_nudge, timescale; bool demo, freeze_demo, timedemo;
} qa_q3_clock_options;
typedef struct qa_q3_client_clock qa_q3_client_clock;
bool qa_q3_client_clock_init(qa_q3_client_clock **out);
void qa_q3_client_clock_destroy(qa_q3_client_clock *c);
int64_t qa_q3_client_clock_time(const qa_q3_client_clock *c);
int64_t qa_q3_client_clock_delta(const qa_q3_client_clock *c);
void qa_q3_client_clock_set_time(qa_q3_client_clock *c, int64_t time);
void qa_q3_client_clock_set_delta(qa_q3_client_clock *c, int64_t delta);
/* publish stores borrowed snapshot pointer (TS object reference). */
void qa_q3_client_clock_publish(qa_q3_client_clock *c, const qa_q3_wire_snapshot *snapshot);
void qa_q3_client_clock_clear(qa_q3_client_clock *c);
/* advance: has_result false = TS null. */
bool qa_q3_client_clock_advance(qa_q3_client_clock *c, int64_t real_time, const qa_q3_clock_options *options, int64_t *out);
bool qa_q3_client_clock_needs_demo_message(const qa_q3_client_clock *c);
/* demoTiming: false = null. */
bool qa_q3_client_clock_demo_timing(const qa_q3_client_clock *c, int64_t milliseconds, int64_t *frames, int64_t *elapsed_ms);
#endif
