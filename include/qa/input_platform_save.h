#ifndef QA_INPUT_PLATFORM_SAVE_H
#define QA_INPUT_PLATFORM_SAVE_H
#include "qa/input_platform.h"
/* Actual isolated owner allocation. Settings, seats and callback contexts must
 * already exist. It creates no native endpoint, changes no SDL global state,
 * and invokes no output/routing/input callbacks. Keep it detached until the
 * complete saved platform state and native handoff are qualified. */
qa_input_platform *qa_input_platform_create_detached(const qa_input_platform_options *, qa_error *);
#endif
