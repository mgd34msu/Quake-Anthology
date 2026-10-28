/* Port of src/app/bootstrap/simulation/native-q1-pusher.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q1_PUSHER_H
#define QA_APP_BOOTSTRAP_SIMULATION_NATIVE_Q1_PUSHER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_native_q1_pusher qa_app_sim_native_q1_pusher;
typedef struct qa_app_sim_native_q1_pusher_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_native_q1_pusher_options;
qa_app_sim_native_q1_pusher *qa_app_sim_native_q1_pusher_create(qa_app_sim_native_q1_pusher_options options);
void qa_app_sim_native_q1_pusher_destroy(qa_app_sim_native_q1_pusher *value);
#endif
