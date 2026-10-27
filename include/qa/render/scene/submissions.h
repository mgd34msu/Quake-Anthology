/* Port of src/render/scene/submissions.ts. GPL-2.0-or-later.
 * TS SceneGroup wraps RenderOperations. Here a SceneOperation is a tagged
 * struct; groups hold draw operations only (SceneModelGroup path) or, for
 * the sky/source adapters, arbitrary operations. Batches are borrowed by
 * group constructors; merged output is owned (qa_merged_operations). */
#ifndef QA_RENDER_SCENE_SUBMISSIONS_H
#define QA_RENDER_SCENE_SUBMISSIONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/materials/compile.h"
#include "qa/render/scene/material_registrations.h"
typedef struct qa_source_scene_order {
  qa_scene_material_registrations *registrations;
  double entity_count;
  const void *identity; /* self address token */
} qa_source_scene_order;
void qa_source_scene_order_init(qa_source_scene_order *o, qa_scene_material_registrations *registrations);
bool qa_source_scene_order_equals(const qa_source_scene_order *a, const qa_source_scene_order *b);
double qa_source_scene_order_reserve_entities(qa_source_scene_order *o, double count);
double qa_reserve_source_entity_range(qa_source_scene_order *view, double count);
typedef struct qa_source_entity_order { bool is_refentity; double index; } qa_source_entity_order;
typedef struct qa_source_surface_order {
  qa_source_scene_order *view; qa_source_entity_order entity; double surface, fog, dlight;
} qa_source_surface_order;
typedef enum qa_sequence_phase { QA_SEQUENCE_SKY, QA_SEQUENCE_OPAQUE, QA_SEQUENCE_TRANSLUCENT } qa_sequence_phase;
const char *qa_sequence_phase_name(qa_sequence_phase phase);
typedef enum qa_scene_group_order_kind { QA_GROUP_COMPILED, QA_GROUP_SOURCE, QA_GROUP_SEQUENCE } qa_scene_group_order_kind;
typedef struct qa_scene_group_order {
  qa_scene_group_order_kind kind;
  const qa_compiled_material *material;
  qa_registered_scene_material source_material; /* source kind */
  qa_source_surface_order source;                 /* source kind */
  qa_sequence_phase phase;                        /* sequence kind */
} qa_scene_group_order;
typedef struct qa_scene_operation qa_scene_operation;
typedef struct qa_scene_group {
  qa_scene_group_order order;
  const qa_render_operation *operations; size_t operation_count;
  qa_render_operation *owned_operation; /* constructors' single draw op */
} qa_scene_group;
struct qa_scene_operation {
  bool is_group;
  qa_render_operation operation;   /* when !is_group */
  qa_scene_group group;            /* when is_group */
};
void qa_scene_group_destroy(qa_scene_group *group);
qa_scene_group qa_compiled_draw_group(const qa_compiled_material *material, const qa_draw_batch *batches, size_t count);
qa_scene_group qa_source_draw_group(qa_registered_scene_material material, qa_source_surface_order source, const qa_draw_batch *batches, size_t count);
qa_scene_group qa_sequence_draw_group(qa_sequence_phase phase, const qa_draw_batch *batches, size_t count);
/* Batches borrowed (group ops). Array malloc'd, caller frees array. */
const qa_draw_batch **qa_scene_model_batches(const qa_scene_group *groups, size_t count, size_t *out_count);
typedef struct qa_finished_operations { qa_render_operation *operations; size_t count; } qa_finished_operations;
void qa_finished_operations_destroy(qa_finished_operations *o);
bool qa_finish_scene_operations(const qa_scene_operation *input, size_t count, qa_finished_operations *out);
typedef struct qa_merged_operations {
  qa_render_operation *operations; size_t count;
  qa_draw_batch **batches;                 /* per-draw-op owned arrays */
  void *_builders;
} qa_merged_operations;
void qa_merged_operations_destroy(qa_merged_operations *o);
bool qa_merge_draw_operations(const qa_render_operation *input, size_t count, qa_merged_operations *out);
#endif
