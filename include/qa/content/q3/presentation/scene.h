/* Port of src/content/q3/presentation/scene.ts. GPL-2.0-or-later.
 * Cgame submissions translated into the shared scene model.
 * Admission identity is a fresh owned token per snapshot; equality is
 * token identity, as the TS private symbol. Captured content owns its
 * arrays and its admitted polys; destroy with the matching destroy. */
#ifndef QA_CONTENT_Q3_PRESENTATION_SCENE_H
#define QA_CONTENT_Q3_PRESENTATION_SCENE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/presentation/ref_entity.h"
#include "qa/content/q3/presentation/refdef.h"
#include "qa/content/q3/presentation/scene_host.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
typedef enum qa_q3_pres_scene_admission_origin {
  QA_Q3_PRES_SCENE_ORIGIN_NATIVE, QA_Q3_PRES_SCENE_ORIGIN_MIXED
} qa_q3_pres_scene_admission_origin;
const char *qa_q3_pres_scene_admission_origin_name(qa_q3_pres_scene_admission_origin origin);
typedef struct qa_q3_pres_scene_admission_id {
  qa_q3_pres_scene_admission_origin origin;
  const void *token; /* owned by the admission */
} qa_q3_pres_scene_admission_id;
bool qa_q3_pres_scene_admission_id_equals(qa_q3_pres_scene_admission_id a, qa_q3_pres_scene_admission_id b);
/* TS Q3FogSelection: volume bounds are the read field. */
typedef struct qa_q3_pres_fog_selection { double index; qa_bounds bounds; } qa_q3_pres_fog_selection;
/* TS Q3AdmittedPoly. fog NULL is TS null (borrowed selection). */
typedef struct qa_q3_pres_admitted_poly {
  qa_q3_pres_ref_poly poly; /* owned vertices */
  const qa_q3_pres_fog_selection *fog;
} qa_q3_pres_admitted_poly;
void qa_q3_pres_admitted_poly_destroy(qa_q3_pres_admitted_poly *poly);
typedef struct qa_q3_pres_scene_admission {
  qa_q3_pres_scene_admission_id id;
  void *token_storage; /* owned, identity only */
  qa_q3_pres_admitted_ref_entity *entities; size_t entity_count; /* owned */
  qa_q3_pres_admitted_poly *polygons; size_t polygon_count; /* owned */
} qa_q3_pres_scene_admission;
void qa_q3_pres_scene_admission_destroy(qa_q3_pres_scene_admission *admission);
/* False only on OOM. Entities/polygons are copied (poly fog kept). */
bool qa_q3_pres_snapshot_scene_admission(qa_q3_pres_scene_admission_origin origin,
    const qa_q3_pres_admitted_ref_entity *entities, size_t entity_count,
    const qa_q3_pres_admitted_poly *polygons, size_t polygon_count,
    qa_q3_pres_scene_admission *out);
/* TS admitQ3Poly. False only on OOM; the TS RangeError aborts. */
bool qa_q3_pres_admit_poly(const qa_q3_pres_ref_poly *poly,
    const qa_q3_pres_fog_selection *fogs, size_t fog_count, qa_q3_pres_admitted_poly *out);
const qa_q3_pres_fog_selection *qa_q3_pres_procedural_fog(qa_vec3 origin, double radius,
    const qa_q3_pres_fog_selection *fogs, size_t fog_count);
typedef enum qa_q3_pres_geometry_admission_kind {
  QA_Q3_PRES_GEOMETRY_REFENTITY, QA_Q3_PRES_GEOMETRY_POLYGON
} qa_q3_pres_geometry_admission_kind;
typedef struct qa_q3_pres_geometry_admission {
  qa_q3_pres_geometry_admission_kind kind; double index;
} qa_q3_pres_geometry_admission;
/* TS PresentedModel. source borrows the admission entity. custom_skin
 * surfaces borrow the entity's skin. */
typedef struct qa_q3_pres_presented_model {
  double entity_index;
  qa_scene_entity entity;
  qa_actor_id actor_storage; /* valid when entity.actor != NULL */
  const char *custom_shader; /* nullable */
  const qa_q3_pres_scene_skin_surface *custom_skin; size_t custom_skin_count;
  bool non_normalized_axes;
  const qa_q3_pres_ref_model_entity *source;
} qa_q3_pres_presented_model;
/* TS prepareQ3Model. False is the TS null (default model). actor NULL
 * is TS null. The scene entity's model for an inline model carries the
 * world geometry through source->model; entity.model mirrors TS
 * brush-model vs model via model.kind on the source record. */
bool qa_q3_pres_prepare_model(const qa_q3_pres_ref_model_entity *entity, const qa_actor_id *actor,
                              double entity_index, qa_q3_pres_presented_model *out);
typedef struct qa_q3_pres_presented_geometry {
  qa_q3_pres_geometry_admission admission;
  const qa_q3_pres_scene_shader *shader; /* nullable */
  const qa_q3_pres_admitted_ref_entity *source_entity; /* refentity kind */
  const qa_q3_pres_admitted_poly *source_poly; /* polygon kind */
} qa_q3_pres_presented_geometry;
typedef struct qa_q3_pres_presented_special {
  double entity_index;
  const qa_q3_pres_admitted_ref_entity *source;
} qa_q3_pres_presented_special;
typedef struct qa_q3_pres_presented_portal {
  double entity_index;
  const qa_q3_pres_admitted_ref_entity *source;
} qa_q3_pres_presented_portal;
typedef struct qa_q3_pres_scene_content {
  qa_q3_pres_scene_admission admission;
  qa_q3_pres_presented_model *models; size_t model_count;
  qa_q3_pres_presented_geometry *effects; size_t effect_count;
  qa_q3_pres_presented_special *special_entities; size_t special_count;
  qa_q3_pres_presented_portal *portals; size_t portal_count;
  qa_scene_light *lights; size_t light_count;
} qa_q3_pres_scene_content;
void qa_q3_pres_scene_content_destroy(qa_q3_pres_scene_content *content);
typedef struct qa_q3_pres_presented_scene {
  qa_q3_pres_scene_content content;
  qa_seat_id seat;
  qa_rect viewport;
  qa_scene_camera camera;
  qa_q3_pres_refdef source;
} qa_q3_pres_presented_scene;
void qa_q3_pres_presented_scene_destroy(qa_q3_pres_presented_scene *scene);
/* TS Q3SceneTarget. publish takes ownership of the scene content. */
typedef struct qa_q3_pres_scene_target {
  qa_seat_id seat;
  qa_rect viewport;
  double far_clip, near_clip;
  void *ctx;
  const qa_q3_pres_fog_selection *(*fog_selections)(void *ctx, size_t *out_count);
  void (*print)(void *ctx, const char *text);
  const qa_actor_id *(*actor)(void *ctx, const qa_q3_pres_ref_model_entity *entity);
  void (*publish)(void *ctx, qa_q3_pres_presented_scene *scene);
} qa_q3_pres_scene_target;
typedef struct qa_q3_pres_scene_recorder qa_q3_pres_scene_recorder;
/* TS Q3SceneRecorder view used by landed resources (fn+ctx table). */
typedef struct qa_q3_pres_scene_recorder_view {
  void *ctx;
  void (*clear_scene)(void *ctx);
  void (*add_ref_entity)(void *ctx, const qa_q3_pres_admitted_ref_entity *entity);
  void (*add_poly)(void *ctx, const qa_q3_pres_ref_poly *poly);
  void (*add_light)(void *ctx, qa_q3_pres_dynamic_light light);
  void (*render_scene)(void *ctx, const qa_q3_pres_refdef *refdef);
} qa_q3_pres_scene_recorder_view;
/* NULL only on OOM. Allocation failure inside add_* (void in TS, GC)
 * aborts via qa_range_error-free OOM path: the recorder drops the add
 * only when the poly copy fails are still fatal in practice; see .c. */
qa_q3_pres_scene_recorder *qa_q3_pres_scene_recorder_create(qa_q3_pres_scene_target target);
void qa_q3_pres_scene_recorder_destroy(qa_q3_pres_scene_recorder *recorder);
qa_q3_pres_scene_target qa_q3_pres_scene_recorder_target(qa_q3_pres_scene_recorder *recorder);
void qa_q3_pres_scene_recorder_clear_scene(qa_q3_pres_scene_recorder *recorder);
void qa_q3_pres_scene_recorder_add_ref_entity(qa_q3_pres_scene_recorder *recorder,
                                              const qa_q3_pres_admitted_ref_entity *entity);
void qa_q3_pres_scene_recorder_add_poly(qa_q3_pres_scene_recorder *recorder, const qa_q3_pres_ref_poly *poly);
void qa_q3_pres_scene_recorder_add_light(qa_q3_pres_scene_recorder *recorder, qa_q3_pres_dynamic_light light);
/* False only on OOM. */
bool qa_q3_pres_scene_recorder_capture(qa_q3_pres_scene_recorder *recorder, qa_q3_pres_scene_content *out);
void qa_q3_pres_scene_recorder_render_scene(qa_q3_pres_scene_recorder *recorder, const qa_q3_pres_refdef *input);
qa_q3_pres_scene_recorder_view qa_q3_pres_scene_recorder_as_view(qa_q3_pres_scene_recorder *recorder);
#endif
