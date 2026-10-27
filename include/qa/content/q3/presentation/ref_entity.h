/* Port of src/content/q3/presentation/ref-entity.ts. GPL-2.0-or-later.
 * Source refEntity_t presentation records. id Software, GPL-2.0-or-later.
 * TS nested Vec objects are C value structs, so the TS deep copies are
 * struct assignments; resource handle identity is retained as in TS. */
#ifndef QA_CONTENT_Q3_PRESENTATION_REF_ENTITY_H
#define QA_CONTENT_Q3_PRESENTATION_REF_ENTITY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
enum {
  QA_Q3_PRES_RF_MINLIGHT = 1, QA_Q3_PRES_RF_THIRD_PERSON = 2, QA_Q3_PRES_RF_FIRST_PERSON = 4,
  QA_Q3_PRES_RF_DEPTHHACK = 8, QA_Q3_PRES_RF_NOSHADOW = 64, QA_Q3_PRES_RF_LIGHTING_ORIGIN = 128,
  QA_Q3_PRES_RF_SHADOW_PLANE = 256, QA_Q3_PRES_RF_WRAP_FRAMES = 512
};
typedef enum qa_q3_pres_scene_model_kind {
  QA_Q3_PRES_SCENE_MODEL_DEFAULT, /* "default" */
  QA_Q3_PRES_SCENE_MODEL_LOADED,  /* "model" */
  QA_Q3_PRES_SCENE_MODEL_INLINE   /* "inline" */
} qa_q3_pres_scene_model_kind;
typedef struct qa_q3_pres_scene_model {
  qa_q3_pres_scene_model_kind kind;
  const char *path; /* "*default" for the default model */
  qa_decoded_model model;        /* loaded */
  double index;                  /* inline */
  qa_decoded_world geometry;     /* inline */
  qa_bounds bounds;              /* inline */
  qa_resolved_resource_reference resource; /* loaded / inline */
} qa_q3_pres_scene_model;
extern const qa_q3_pres_scene_model qa_q3_pres_default_model; /* TS DEFAULT_MODEL (frozen) */
typedef struct qa_q3_pres_scene_shader { const char *name; } qa_q3_pres_scene_shader;
typedef struct qa_q3_pres_scene_skin_surface { const char *name; const char *shader; } qa_q3_pres_scene_skin_surface;
typedef struct qa_q3_pres_scene_skin {
  const char *path;
  const qa_q3_pres_scene_skin_surface *surfaces;
  size_t surface_count;
} qa_q3_pres_scene_skin;
typedef enum qa_q3_pres_ref_entity_kind {
  QA_Q3_PRES_REF_MODEL,         /* "model" */
  QA_Q3_PRES_REF_SPRITE,        /* "sprite" */
  QA_Q3_PRES_REF_BEAM,          /* "beam" */
  QA_Q3_PRES_REF_RAIL_CORE,     /* "rail-core" */
  QA_Q3_PRES_REF_RAIL_RINGS,    /* "rail-rings" */
  QA_Q3_PRES_REF_LIGHTNING,     /* "lightning" */
  QA_Q3_PRES_REF_PORTAL,        /* "portal-surface" */
  QA_Q3_PRES_REF_POLY           /* "poly" (source record only) */
} qa_q3_pres_ref_entity_kind;
const char *qa_q3_pres_ref_entity_kind_name(qa_q3_pres_ref_entity_kind kind);
/* TS ShadedEntity fields, inlined per kind below via this prefix struct. */
typedef struct qa_q3_pres_shading {
  double render_flags;
  const qa_q3_pres_scene_shader *custom_shader; /* nullable */
  qa_vec4 shader_rgba;  /* Source byte channels, including zero defaults from Com_Memset. */
  qa_vec2 shader_tex_coord;
  double shader_time;   /* Seconds subtracted from the submitted scene shader clock. */
} qa_q3_pres_shading;
typedef struct qa_q3_pres_ref_model_entity {
  qa_q3_pres_shading shading;
  qa_q3_pres_scene_model model;
  qa_vec3 origin, old_origin;
  qa_axis axis;
  bool non_normalized_axes;
  qa_vec3 lighting_origin;
  double shadow_plane, frame, old_frame, back_lerp, skin_num;
  const qa_q3_pres_scene_skin *custom_skin; /* nullable */
} qa_q3_pres_ref_model_entity;
typedef struct qa_q3_pres_ref_sprite_entity {
  qa_q3_pres_shading shading;
  qa_vec3 origin;
  double radius, rotation;
} qa_q3_pres_ref_sprite_entity;
typedef struct qa_q3_pres_ref_beam_entity {
  qa_q3_pres_shading shading;
  qa_vec3 origin, old_origin;
  qa_axis axis;
  double radius; /* Used for source sprite-fog selection; beam geometry has fixed radius four. */
} qa_q3_pres_ref_beam_entity;
typedef struct qa_q3_pres_ref_rail_entity { /* rail-core / rail-rings / lightning */
  qa_q3_pres_shading shading;
  qa_vec3 origin, old_origin;
  double radius;
} qa_q3_pres_ref_rail_entity;
typedef struct qa_q3_pres_ref_portal_entity {
  double render_flags;
  qa_vec3 origin, old_origin;
  qa_axis axis;
  double frame, old_frame, skin_num;
} qa_q3_pres_ref_portal_entity;
typedef struct qa_q3_pres_ref_entity {
  qa_q3_pres_ref_entity_kind kind;
  union {
    qa_q3_pres_ref_model_entity model;
    qa_q3_pres_ref_sprite_entity sprite;
    qa_q3_pres_ref_beam_entity beam;
    qa_q3_pres_ref_rail_entity rail; /* rail-core / rail-rings / lightning */
    qa_q3_pres_ref_portal_entity portal;
  } as;
} qa_q3_pres_ref_entity;
typedef struct qa_q3_pres_ref_poly_vertex {
  qa_vec3 position;
  qa_vec2 tex_coord;
  qa_vec4 color; /* Source polyVert_t.modulate byte channels. */
} qa_q3_pres_ref_poly_vertex;
typedef struct qa_q3_pres_ref_poly {
  const qa_q3_pres_scene_shader *shader; /* nullable */
  qa_q3_pres_ref_poly_vertex *vertices;  /* owned */
  size_t vertex_count;
} qa_q3_pres_ref_poly;
qa_q3_pres_ref_model_entity qa_q3_pres_create_model_entity(qa_q3_pres_scene_model model);
qa_q3_pres_ref_model_entity qa_q3_pres_create_model_entity_default(void); /* TS default argument */
qa_q3_pres_ref_sprite_entity qa_q3_pres_create_sprite_entity(void);
qa_q3_pres_ref_beam_entity qa_q3_pres_create_beam_entity(void);
qa_q3_pres_ref_rail_entity qa_q3_pres_create_rail_core_entity(void);
qa_q3_pres_ref_rail_entity qa_q3_pres_create_rail_rings_entity(void);
qa_q3_pres_ref_rail_entity qa_q3_pres_create_lightning_entity(void);
qa_q3_pres_ref_portal_entity qa_q3_pres_create_portal_entity(void);
/* Wrap a per-kind record as a tagged qa_q3_pres_ref_entity. */
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_model(qa_q3_pres_ref_model_entity entity);
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_sprite(qa_q3_pres_ref_sprite_entity entity);
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_beam(qa_q3_pres_ref_beam_entity entity);
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_rail(qa_q3_pres_ref_entity_kind kind, qa_q3_pres_ref_rail_entity entity);
qa_q3_pres_ref_entity qa_q3_pres_ref_entity_from_portal(qa_q3_pres_ref_portal_entity entity);
/** RE_AddRefEntityToScene owns a value copy; resource handle identity is retained. */
qa_q3_pres_ref_entity qa_q3_pres_copy_ref_entity(const qa_q3_pres_ref_entity *entity);
/* TS SourceHandles<Entity>: model/customShader/customSkin may be a VM number. */
typedef struct qa_q3_pres_source_model_handle {
  bool is_handle;
  double handle;
  qa_q3_pres_scene_model model;
} qa_q3_pres_source_model_handle;
typedef struct qa_q3_pres_source_ref_handle {
  bool is_handle;
  double handle;
  const void *value; /* SceneShader* / SceneSkin*, nullable when not a handle */
} qa_q3_pres_source_ref_handle;
/** The complete refEntity_t value survives admission, including retained
 * backend pose reads. TS SourceRefEntityRecord (all model fields + radius +
 * rotation, kind may be "poly"). */
typedef struct qa_q3_pres_source_ref_entity_record {
  qa_q3_pres_ref_entity_kind kind;
  double render_flags;
  qa_q3_pres_source_ref_handle custom_shader;
  qa_vec4 shader_rgba;
  qa_vec2 shader_tex_coord;
  double shader_time;
  qa_q3_pres_source_model_handle model;
  qa_vec3 origin, old_origin;
  qa_axis axis;
  bool non_normalized_axes;
  qa_vec3 lighting_origin;
  double shadow_plane, frame, old_frame, back_lerp, skin_num;
  qa_q3_pres_source_ref_handle custom_skin;
  double radius, rotation;
} qa_q3_pres_source_ref_entity_record;
/* TS Q3AdmittedRefEntity = RefEntity | poly source record. */
typedef struct qa_q3_pres_admitted_ref_entity {
  bool is_poly_record;
  qa_q3_pres_ref_entity entity;                 /* !is_poly_record */
  qa_q3_pres_source_ref_entity_record record;   /* is_poly_record */
} qa_q3_pres_admitted_ref_entity;
qa_q3_pres_admitted_ref_entity qa_q3_pres_copy_admitted_ref_entity(const qa_q3_pres_admitted_ref_entity *entity);
/* TS SourceRefEntity = SourceHandles<RefEntity> | SourceRefEntityRecord.
 * has_lighting_origin is the TS `"lightingOrigin" in entity` test (true for
 * records and model handles). Fields are the record superset. */
typedef struct qa_q3_pres_source_ref_entity {
  qa_q3_pres_source_ref_entity_record fields;
  bool has_lighting_origin;
} qa_q3_pres_source_ref_entity;
qa_q3_pres_source_ref_entity qa_q3_pres_copy_source_ref_entity(const qa_q3_pres_source_ref_entity *entity);
/* Deep-copies vertices into owned storage. False only on OOM. */
bool qa_q3_pres_copy_ref_poly(const qa_q3_pres_ref_poly *poly, qa_q3_pres_ref_poly *out);
void qa_q3_pres_ref_poly_destroy(qa_q3_pres_ref_poly *poly);
#endif
