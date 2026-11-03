/* Port of worldMarkProjector from src/content/q3/presentation/mark-projector.ts:
 * the world decal geometry of a WorldScene for BspMarkProjector.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_PRESENTATION_WORLD_MARK_PROJECTOR_H
#define QA_CONTENT_Q3_PRESENTATION_WORLD_MARK_PROJECTOR_H
#include <stddef.h>
#include "qa/content/q3/presentation/mark_projector.h"
#include "qa/render/scene/world.h"
/* Owns the converted arrays; face indices borrow the scene's surface
 * geometry, so the scene must outlive it. */
typedef struct qa_q3_pres_world_mark_projector {
  qa_q3_pres_bsp_mark_projector projector;
  qa_q3_pres_mark_surface *surfaces;
  qa_q3_pres_mark_node *nodes;
  qa_plane *planes;
  qa_q3_pres_mark_leaf *leaves;
  void **owned; size_t owned_count;
} qa_q3_pres_world_mark_projector;
/* TS worldMarkProjector(world). Foreign BSP owners and surfaces without a
 * source shader raise the TS errors. */
void qa_q3_pres_world_mark_projector_init(qa_q3_pres_world_mark_projector *out, const qa_world_scene *world);
void qa_q3_pres_world_mark_projector_destroy(qa_q3_pres_world_mark_projector *projector);
#endif
