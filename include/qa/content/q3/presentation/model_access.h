/* Port of src/content/q3/presentation/model-access.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_PRESENTATION_MODEL_ACCESS_H
#define QA_CONTENT_Q3_PRESENTATION_MODEL_ACCESS_H
#include <stdbool.h>
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/contracts/math.h"
qa_bounds qa_q3_pres_model_bounds(const qa_q3_pres_scene_model *source);
typedef struct qa_q3_pres_model_tag { qa_vec3 origin; qa_axis axes; } qa_q3_pres_model_tag;
/* False is TS null. */
bool qa_q3_pres_lerp_model_tag(const qa_q3_pres_scene_model *source, const char *name, double start, double end,
                               double fraction, qa_q3_pres_model_tag *out);
#endif
