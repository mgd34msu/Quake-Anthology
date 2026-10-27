/* Port of src/render/scene/patch-lod.ts. GPL-2.0-or-later.
 * TS PatchGrid unifies plain {mesh,origin,radius} and TemporaryPatchMesh
 * (a Mesh that is its own grid). Here a grid always holds a plain mesh;
 * use qa_patch_grid_from_temporary / _prepare_allocated for the block form. */
#ifndef QA_RENDER_SCENE_PATCH_LOD_H
#define QA_RENDER_SCENE_PATCH_LOD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/math.h"
#include "qa/render/scene/patch.h"
typedef struct qa_patch_grid {
  qa_patch_mesh mesh; qa_vec3 lod_origin; double lod_radius;
  double lod_fixed; bool lod_stitched; /* preparation state */
} qa_patch_grid;
qa_patch_grid qa_create_patch_grid(qa_patch_mesh mesh, qa_vec3 bounds[2]);
/* TS createPatchGrid(TemporaryPatchMesh): sets volume on mesh; out wraps a snapshot. */
bool qa_create_patch_grid_allocated(qa_allocated_patch_mesh *mesh, qa_vec3 bounds[2], qa_patch_grid *out);
typedef void (*qa_scene_count_fn)(void *ctx, double count);
typedef void (*qa_scene_grid_fn)(void *ctx, double index, const qa_patch_grid *grid);
/* Mutates grids in place (TS returns new array of same identities). */
void qa_prepare_patch_grids(qa_patch_grid *grids, size_t count,
  qa_scene_count_fn on_stitched, void *on_stitched_ctx, qa_scene_grid_fn on_replaced, void *on_replaced_ctx);
bool qa_select_patch_lod(const qa_patch_grid *grid, qa_vec3 world_lod_origin, qa_vec3 view_origin, qa_vec3 view_forward, double lod_curve_error, qa_patch_mesh *out);
#endif
