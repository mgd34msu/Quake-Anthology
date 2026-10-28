/* Port of src/content/q3/presentation/marks.ts. GPL-2.0-or-later.
 * Impact mark lifetime/texture mapping from id Software's cg_marks.c.
 * Returned polys are caller-owned: destroy each with
 * qa_q3_pres_ref_poly_destroy, then free the array. */
#ifndef QA_CONTENT_Q3_PRESENTATION_MARKS_H
#define QA_CONTENT_Q3_PRESENTATION_MARKS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/presentation/mark_projector.h"
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/math.h"
enum {
  QA_Q3_PRES_MAX_MARK_POLYS = 256, QA_Q3_PRES_MAX_MARK_POLY_VERTICES = 10,
  QA_Q3_PRES_MAX_MARK_FRAGMENTS = 128, QA_Q3_PRES_MAX_MARK_POINTS = 384,
  QA_Q3_PRES_MARK_TOTAL_TIME = 10000, QA_Q3_PRES_MARK_FADE_TIME = 1000
};
/* TS ImpactMarkRequest. */
typedef struct qa_q3_pres_impact_mark_request {
  const qa_q3_pres_scene_shader *shader; /* nullable */
  qa_vec3 origin, direction;
  double orientation;
  qa_vec4 color;
  bool alpha_fade;
  double radius;
  bool temporary;
} qa_q3_pres_impact_mark_request;
/* TS ImpactMarkOptions. */
typedef struct qa_q3_pres_impact_mark_options {
  void *ctx;
  double (*clock)(void *ctx);
  bool (*enabled)(void *ctx);
  const qa_q3_pres_scene_shader *(*energy_shader)(void *ctx);
} qa_q3_pres_impact_mark_options;
/* TS Pick<ImpactMarkSystem,"impactMark"> view (landed consumers). */
typedef struct qa_q3_pres_impact_mark_system_view {
  void *ctx;
  bool (*impact_mark)(void *ctx, qa_q3_pres_impact_mark_request request,
                      qa_q3_pres_ref_poly **out_polys, size_t *out_count);
} qa_q3_pres_impact_mark_system_view;
typedef struct qa_q3_pres_impact_mark_system qa_q3_pres_impact_mark_system;
/* Projector is borrowed. NULL only on OOM. */
qa_q3_pres_impact_mark_system *qa_q3_pres_impact_mark_system_create(
    qa_q3_pres_bsp_mark_projector *projector, qa_q3_pres_impact_mark_options options);
void qa_q3_pres_impact_mark_system_destroy(qa_q3_pres_impact_mark_system *system);
void qa_q3_pres_impact_mark_system_reset(qa_q3_pres_impact_mark_system *system);
double qa_q3_pres_impact_mark_system_active_mark_count(qa_q3_pres_impact_mark_system *system);
/* TS impactMark. False only on OOM. Temporary polys are returned;
 * non-temporary marks join the pool and the result is empty. */
bool qa_q3_pres_impact_mark_system_impact_mark(qa_q3_pres_impact_mark_system *system,
    qa_q3_pres_impact_mark_request request, qa_q3_pres_ref_poly **out_polys, size_t *out_count);
/* TS addMarks. False only on OOM. */
bool qa_q3_pres_impact_mark_system_add_marks(qa_q3_pres_impact_mark_system *system,
    qa_q3_pres_ref_poly **out_polys, size_t *out_count);
qa_q3_pres_impact_mark_system_view qa_q3_pres_impact_mark_system_as_view(
    qa_q3_pres_impact_mark_system *system);
#endif
