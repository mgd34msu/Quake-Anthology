/* Port of src/app/bootstrap/q1-fog.ts. The unlanded
 * SimulationPresentationEvent q1-fog / presentation-owner variants
 * are carried as the minimal view below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_FOG_H
#define QA_APP_BOOTSTRAP_Q1_FOG_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/render.h"
#include "qa/materials/legacy_fog.h"
typedef struct qa_app_q1_fog_event {
  const char *kind;       /* "q1-fog"|"presentation-owner"|other */
  const char *event_kind; /* "retired" for presentation-owner */
  bool has_owner;
  qa_presentation_owner owner;
  qa_content_id content;
  bool has_player;        /* TS event.player !== null */
  qa_actor_id player;
  qa_q1_fog_transition transition;
  double sky_factor;
} qa_app_q1_fog_event;
typedef struct qa_app_q1_map_fog qa_app_q1_map_fog;
/* contents are borrowed content ids. */
qa_app_q1_map_fog *qa_app_q1_map_fog_create(const char *entities,
    const qa_content_id *contents, size_t content_count, qa_actor_id actor, bool enabled);
void qa_app_q1_map_fog_destroy(qa_app_q1_map_fog *fog);
void qa_app_q1_map_fog_receive(qa_app_q1_map_fog *fog, const qa_app_q1_fog_event *events, size_t event_count);
bool qa_app_q1_map_fog_active(const qa_app_q1_map_fog *fog);
/* SceneFog with kind "q1". */
qa_scene_fog qa_app_q1_map_fog_current(qa_app_q1_map_fog *fog, double seconds);
#endif
