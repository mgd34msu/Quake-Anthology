/* Port of src/render/scene/patch.ts. GPL-2.0-or-later.
 * The TS 44-byte float32 block layout (AllocatedPatchMesh) is retained:
 * header 92 bytes, drawvert_t stride 44, lod errors in side blocks.
 * Vertices/errors are read through the accessors (TS getters). */
#ifndef QA_RENDER_SCENE_PATCH_H
#define QA_RENDER_SCENE_PATCH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
typedef struct qa_patch_allocation {
  void *ctx; uint8_t *bytes; size_t length; double address;
  void (*free_fn)(void *ctx, struct qa_patch_allocation *allocation);
} qa_patch_allocation;
typedef struct qa_patch_allocator {
  void *ctx;
  qa_patch_allocation (*allocate)(void *ctx, double length);
} qa_patch_allocator;
typedef struct qa_patch_mesh {
  double width, height;
  qa_q3_bsp_vertex *vertices; size_t vertex_count; /* owned */
  double *indices; size_t index_count;              /* owned */
  double *width_lod_error, *height_lod_error;       /* owned */
} qa_patch_mesh;
void qa_patch_mesh_destroy(qa_patch_mesh *mesh);
/* TS PatchMemoryProfile: zone NULL is "diagnostic". */
typedef struct qa_patch_memory_profile { const qa_patch_allocator *zone; } qa_patch_memory_profile;
typedef struct qa_allocated_patch_mesh {
  uint8_t *block; uint8_t *width_block, *height_block;
  const double *indices; size_t index_count;
  void *_grid_alloc, *_width_alloc, *_height_alloc; /* TemporaryPatchMesh */
  bool temporary; qa_patch_memory_profile profile;
} qa_allocated_patch_mesh;
double qa_allocated_patch_mesh_width(const qa_allocated_patch_mesh *mesh);
double qa_allocated_patch_mesh_height(const qa_allocated_patch_mesh *mesh);
qa_q3_bsp_vertex qa_allocated_patch_mesh_vertex(const qa_allocated_patch_mesh *mesh, double index);
double qa_allocated_patch_mesh_width_error(const qa_allocated_patch_mesh *mesh, double index);
double qa_allocated_patch_mesh_height_error(const qa_allocated_patch_mesh *mesh, double index);
qa_vec3 qa_allocated_patch_mesh_lod_origin(const qa_allocated_patch_mesh *mesh);
double qa_allocated_patch_mesh_lod_radius(const qa_allocated_patch_mesh *mesh);
void qa_allocated_patch_mesh_set_lod_radius(qa_allocated_patch_mesh *mesh, double radius);
double qa_allocated_patch_mesh_lod_fixed(const qa_allocated_patch_mesh *mesh);
void qa_allocated_patch_mesh_set_lod_fixed(qa_allocated_patch_mesh *mesh, double value);
bool qa_allocated_patch_mesh_lod_stitched(const qa_allocated_patch_mesh *mesh);
void qa_allocated_patch_mesh_set_lod_stitched(qa_allocated_patch_mesh *mesh, bool value);
void qa_allocated_patch_mesh_set_lod_volume(qa_allocated_patch_mesh *mesh, qa_vec3 origin, double radius);
void qa_temporary_patch_mesh_create(const qa_patch_mesh *mesh, qa_patch_memory_profile profile, qa_allocated_patch_mesh *out);
void qa_temporary_patch_mesh_replace(qa_allocated_patch_mesh *temp, const qa_patch_mesh *mesh, qa_allocated_patch_mesh *out);
void qa_temporary_patch_mesh_free(qa_allocated_patch_mesh *temp);
/* allocate callback returns a zeroed caller-owned block of length bytes. */
typedef uint8_t *(*qa_patch_hunk_alloc_fn)(void *ctx, const char *call, size_t length, double *address_out);
void qa_temporary_patch_mesh_move_to_hunk(qa_allocated_patch_mesh *temp, qa_patch_hunk_alloc_fn allocate, void *ctx, qa_allocated_patch_mesh *out);
/* subdivisions: TS default 4 (caller passes). Result owned. */
bool qa_tessellate_patch(const qa_q3_bsp_vertex *points, size_t point_count, double width, double height, double subdivisions, qa_patch_mesh *out);
/* direction_width true is TS "width". For a temporary mesh use the
 * _allocated form (TS mesh.replace path). Result owned. */
bool qa_insert_patch_strip(const qa_patch_mesh *mesh, bool direction_width, double index, double edge, qa_vec3 position, double lod_error, qa_patch_mesh *out);
bool qa_insert_patch_strip_allocated(qa_allocated_patch_mesh *temp, bool direction_width, double index, double edge, qa_vec3 position, double lod_error, qa_allocated_patch_mesh *out);
/* Snapshot an allocated mesh into an owned plain mesh. */
bool qa_allocated_patch_mesh_snapshot(const qa_allocated_patch_mesh *mesh, qa_patch_mesh *out);
#endif
