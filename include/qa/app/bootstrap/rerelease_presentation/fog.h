/* Port of src/app/bootstrap/rerelease-presentation/fog.ts
 * (Q2 rerelease P_ForceFogTransition / V_FogParamsChanged).
 * Math.fround staging preserved. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_RERELEASE_PRESENTATION_FOG_H
#define QA_APP_BOOTSTRAP_RERELEASE_PRESENTATION_FOG_H
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/render.h"
#include "qa/network/q2/fog.h"
/* TS q2FogFromSource. */
qa_q2_fog_state qa_app_q2_fog_from_source(qa_q2_fog_state value);
/* TS RereleaseFog. */
typedef struct qa_app_rerelease_fog {
  qa_q2_fog_state start;
  qa_q2_fog_state target;
  double starts;
  double duration;
} qa_app_rerelease_fog;
void qa_app_rerelease_fog_init(qa_app_rerelease_fog *fog);
void qa_app_rerelease_fog_receive(qa_app_rerelease_fog *fog, qa_q2_fog_state value,
                                  double duration_milliseconds, double seconds);
/* TS current(): the SceneFog "q2" variant. */
qa_scene_fog qa_app_rerelease_fog_current(const qa_app_rerelease_fog *fog, double seconds);
/* TS q2FogFromWire. */
qa_q2_fog_state qa_app_q2_fog_from_wire(qa_q2_fog_state previous, qa_q2_svc_fog_data value);
#endif
