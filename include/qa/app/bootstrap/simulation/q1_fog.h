/* Port of src/app/bootstrap/simulation/q1-fog.ts. Capture returns a
 * qa_save_value tree (TS plain object). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q1_FOG_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q1_FOG_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
typedef struct qa_app_sim_q1_fog_options {
  qa_content_id content;
  const qa_content_id *accepted_contents; size_t accepted_content_count; bool has_accepted;
  const char *entities;
  void *alive_ctx; bool (*alive)(void *ctx, qa_actor_id actor);
} qa_app_sim_q1_fog_options;
typedef struct qa_app_sim_q1_fog_context {
  qa_content_id content; double sequence; double seconds;
  bool has_source_entity; double source_entity;
} qa_app_sim_q1_fog_context;
/* TS Extract<Q1AddonEvent,{kind:"fog"}>. */
typedef struct qa_app_sim_q1_fog_event_in {
  bool has_player; qa_actor_id player; /* TS player null when !has_player */
  double density; qa_vec3 color; double duration; double sky_factor;
} qa_app_sim_q1_fog_event_in;
typedef struct qa_app_sim_q1_fog qa_app_sim_q1_fog;
qa_app_sim_q1_fog *qa_app_sim_q1_fog_create(qa_app_sim_q1_fog_options options);
void qa_app_sim_q1_fog_destroy(qa_app_sim_q1_fog *fog);
void qa_app_sim_q1_fog_reset(qa_app_sim_q1_fog *fog);
void qa_app_sim_q1_fog_retire(qa_app_sim_q1_fog *fog, qa_actor_id actor);
/* Borrowed output buffer owned by fog; valid until next call. */
const qa_app_sim_presentation_event *qa_app_sim_q1_fog_update(qa_app_sim_q1_fog *fog,
    qa_app_sim_q1_fog_context context, qa_app_sim_q1_fog_event_in event, size_t *out_count);
const qa_app_sim_presentation_event *qa_app_sim_q1_fog_presentation(qa_app_sim_q1_fog *fog, size_t *out_count);
/* Owned save tree; caller frees with qa_save_value_free. */
qa_save_value *qa_app_sim_q1_fog_capture(qa_app_sim_q1_fog *fog);
const qa_app_sim_presentation_event *qa_app_sim_q1_fog_restore(qa_app_sim_q1_fog *fog, const qa_save_reader *reader,
    qa_actor_id (*reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx, size_t *out_count);
#endif
