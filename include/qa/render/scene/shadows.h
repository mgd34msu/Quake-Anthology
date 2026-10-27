/* Port of src/render/scene/shadows.ts. GPL-2.0-or-later.
 * Depth draw positions are owned; destroy helpers free them.
 * Q2ShadowScene caches are arrays (light index / string key). */
#ifndef QA_RENDER_SCENE_SHADOWS_H
#define QA_RENDER_SCENE_SHADOWS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/materials/geometry.h"
#include "qa/render/scene/resources.h"
#define QA_Q2_SHADOW_ATLAS_SIZE 2048.0
#define QA_Q2_SHADOW_NEAR 4.0
typedef struct qa_shadow_mesh {
  const qa_vec3 *positions; size_t position_count;
  const double *indices; size_t index_count;
} qa_shadow_mesh;
typedef struct qa_shadow_sphere { qa_vec3 origin; double radius; } qa_shadow_sphere;
typedef struct qa_shadow_caster {
  qa_vec3 origin; double radius;
  const qa_shadow_mesh *meshes; size_t mesh_count;
} qa_shadow_caster;
typedef struct qa_shadow_atlas_options { bool has_enabled; bool enabled; bool has_resolution_cap; double resolution_cap; } qa_shadow_atlas_options;
typedef struct qa_shadow_lighting { qa_q2_fragment_light *lights; size_t light_count; qa_q2_shadow_atlas atlas_value; } qa_shadow_lighting;
typedef struct qa_prepared_shadows {
  qa_shadow_lighting lighting; /* atlas NULL when lighting.atlas_is_null */
  const qa_q2_shadow_atlas *lighting_atlas;
  qa_render_operation *operations; size_t operation_count;
  qa_depth_atlas_pass *passes; size_t pass_count;
  double **pass_positions; qa_depth_atlas_draw **pass_draws; /* owned per pass */
  struct { double lights, cached_lights, rebuilt_lights, faces_rendered, entity_casters; } stats;
} qa_prepared_shadows;
void qa_prepared_shadows_destroy(qa_prepared_shadows *p);
typedef struct qa_shadow_face_basis { qa_vec3 forward, right, up; } qa_shadow_face_basis;
extern const qa_shadow_face_basis qa_q2_shadow_cube_faces[6];
/* Positions borrowed copy vector is caller storage; helper fills pointers. */
typedef struct qa_scene_shadow_mesh_owned { qa_shadow_mesh mesh; qa_vec3 *owned_positions; } qa_scene_shadow_mesh_owned;
void qa_scene_shadow_mesh_owned_destroy(qa_scene_shadow_mesh_owned *m);
bool qa_shadow_mesh_new(qa_material_geometry geometry, qa_scene_shadow_mesh_owned *out);
qa_shadow_caster qa_shadow_caster_new(qa_vec3 origin, const qa_shadow_mesh *meshes, size_t mesh_count);
double qa_shadow_map_resolution(double requested, double cap);
typedef struct qa_shadow_pack_request { double width, height; } qa_shadow_pack_request;
typedef struct qa_shadow_pack_result { qa_rect rect; bool is_null; } qa_shadow_pack_result;
/* size default = QA_Q2_SHADOW_ATLAS_SIZE when has_size false. */
bool qa_pack_shadow_atlas(const qa_shadow_pack_request *requests, size_t count, bool has_size, double size, qa_shadow_pack_result *out);
qa_mat4 qa_shadow_matrix_multiply(qa_mat4 a, qa_mat4 b);
qa_vec4 qa_shadow_project(qa_mat4 matrix, qa_vec3 position);
double qa_shadow_cone_fov(double cos_half_angle);
qa_mat4 qa_shadow_cone_matrix(qa_q2_fragment_light light);
/* TS closure (sphere)=>boolean as state + apply. */
typedef struct qa_shadow_body_filter {
  qa_vec3 origins[8]; double radii[8];
  bool has_cone[8]; qa_vec3 cone_direction[8]; double cone_cos[8];
  size_t count;
} qa_shadow_body_filter;
qa_shadow_body_filter qa_shadow_body_filter_new(const qa_scene_light *source, size_t count);
bool qa_shadow_body_filter_test(qa_shadow_body_filter filter, qa_shadow_sphere sphere);
typedef struct qa_static_shadow_world {
  qa_shadow_mesh *meshes; size_t mesh_count; /* owned copies */
  qa_vec3 *owned_positions[64]; double *owned_indices[64]; size_t owned_count;
  char *digest; /* lazily owned */
} qa_static_shadow_world;
bool qa_static_shadow_world_init(qa_static_shadow_world *w, const qa_shadow_mesh *meshes, size_t count);
void qa_static_shadow_world_destroy(qa_static_shadow_world *w);
const char *qa_static_shadow_world_digest(qa_static_shadow_world *w);
typedef struct qa_q2_shadow_scene {
  qa_scene_image_registry *images;
  const qa_scene_image_handle *image; bool has_image;
  void *_cached; void *_world_draw_cache;
} qa_q2_shadow_scene;
void qa_q2_shadow_scene_init(qa_q2_shadow_scene *s, qa_scene_image_registry *images);
void qa_q2_shadow_scene_destroy(qa_q2_shadow_scene *s);
/* world_static NULL + world meshes, or a StaticShadowWorld. */
bool qa_q2_shadow_scene_prepare(qa_q2_shadow_scene *s, const qa_scene_light *source, size_t source_count,
  const qa_shadow_mesh *world_meshes, size_t world_count, qa_static_shadow_world *world_static,
  const qa_shadow_caster *casters, size_t caster_count, const qa_shadow_atlas_options *options, qa_prepared_shadows *out);
void qa_q2_shadow_scene_close(qa_q2_shadow_scene *s);
#endif
