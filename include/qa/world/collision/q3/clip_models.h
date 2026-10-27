/* Port of src/world/collision/q3/clip-models.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_CLIP_MODELS_H
#define QA_WORLD_COLLISION_Q3_CLIP_MODELS_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/collision/q3/world.h"
#define QA_SOURCE_BOX_MODEL_HANDLE 255
#define QA_SOURCE_CAPSULE_MODEL_HANDLE 254
typedef struct qa_source_clip_models {
  qa_collision_world *world; qa_bounds bounds;
  qa_temporary_collision_model *box; qa_collision_box_storage *box_storage;
} qa_source_clip_models;
/* temporary_storage: "world" | "private" */
qa_source_clip_models *qa_source_clip_models_create(qa_collision_world *world, const char *temporary_storage);
void qa_source_clip_models_destroy(qa_source_clip_models *m);
qa_save_value *qa_source_clip_capture_temporary_checkpoint(qa_source_clip_models *m);
void qa_source_clip_restore_temporary_checkpoint(qa_source_clip_models *m, const qa_save_value *value);
double qa_source_clip_model_count(qa_source_clip_models *m);
double qa_source_clip_inline_model(qa_source_clip_models *m, double index);
double qa_source_clip_temp_box_model(qa_source_clip_models *m, qa_vec3 mins, qa_vec3 maxs, bool capsule);
qa_bounds qa_source_clip_model_bounds(qa_source_clip_models *m, double handle);
double qa_source_clip_point_contents(qa_source_clip_models *m, qa_vec3 point, double handle);
double qa_source_clip_transformed_point_contents(qa_source_clip_models *m, qa_vec3 point, double handle, qa_vec3 origin, qa_vec3 angles);
qa_source_trace_result qa_source_clip_trace(qa_source_clip_models *m, qa_temporary_trace_query query, double handle);
bool qa_source_clip_trace_without_nodes(qa_source_clip_models *m, double handle, qa_source_trace_result *out);
qa_source_trace_result qa_source_clip_transformed_trace(qa_source_clip_models *m, qa_temporary_trace_query query, double handle, qa_vec3 origin, qa_vec3 angles);
#endif
