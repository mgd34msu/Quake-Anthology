/* Port of src/content/q1/addons/travel.ts. The base travel functions
 * (src/content/q1/base/travel.ts) are supplied by the caller as
 * qa_q1ad_base_travel ops until base lands. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_TRAVEL_H
#define QA_CONTENT_Q1_ADDONS_TRAVEL_H
#include "qa/content/q1/addons/context.h"
typedef struct qa_q1ad_base_travel {
  void *ctx;
  qa_q1ad_travel (*new_travel)(void *ctx, qa_q1ad_context *context);
  qa_q1ad_travel (*capture)(void *ctx, qa_q1ad_context *context, qa_owned_actor actor);
  void (*admit)(void *ctx, qa_q1ad_context *context, qa_owned_actor actor, const qa_q1ad_travel *state);
} qa_q1ad_base_travel;
qa_q1ad_travel qa_q1ad_new_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context);
qa_q1ad_travel qa_q1ad_capture_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context, qa_owned_actor actor);
qa_q1ad_travel qa_q1ad_decode_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context, const qa_q1ad_travel *state);
void qa_q1ad_admit_travel(const qa_q1ad_base_travel *base, qa_q1ad_context *context, qa_owned_actor actor, const qa_q1ad_travel *state);
#endif
