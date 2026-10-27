/*
 * Port of src/movement/q3/provider.ts. GPL-2.0-or-later.
 * The provider struct embeds its options; qa_q3_create_movement_provider
 * fills a qa_movement_provider whose ctx is the options storage, which
 * must outlive the provider. TS TypeError/Error throws abort via
 * qa_range_error with the same messages.
 */
#ifndef QA_MOVEMENT_Q3_PROVIDER_H
#define QA_MOVEMENT_Q3_PROVIDER_H
#include <stdbool.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/scene.h"
#include "qa/movement/q3/types.h"

typedef struct qa_q3_movement_provider_options {
  void *ctx;
  qa_provider_id id;
  qa_q3_movement_hooks hooks;
  qa_q3_postures (*postures)(void *ctx, qa_q3_movement_input input);
  /* NULL = TS undefined; otherwise returns the q3 trace policy. */
  qa_trace_policy (*trace_policy)(void *ctx, qa_q3_movement_input input);
  qa_q3_diagnostics *diagnostics; /* NULL absent */
} qa_q3_movement_provider_options;

/* TS q3Command: command must be the q3 user-command member. */
qa_q3_move_command qa_q3_command(qa_q3_user_command command);
qa_movement_provider qa_q3_create_movement_provider(qa_q3_movement_provider_options *options);
#endif
