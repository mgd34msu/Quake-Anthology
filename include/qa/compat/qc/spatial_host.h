/* Port of src/compat/qc/spatial-host.ts. GPL-2.0-or-later.
 * Host map becomes caller-owned binding array in the returned
 * struct; builtin ctx is the caller's world pointer. */
#ifndef QA_COMPAT_QC_SPATIAL_HOST_H
#define QA_COMPAT_QC_SPATIAL_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/builtins.h"
#include "qa/contracts/scene.h"
typedef struct qa_compat_qc_spatial_model { double index; qa_bounds bounds; } qa_compat_qc_spatial_model;
typedef struct qa_compat_qc_spatial_world {
  void *ctx;
  qa_compat_qc_program *program; qa_compat_qc_entity_memory *entities;
  qa_scene_queries scene; qa_numeric_profile numeric;
  bool (*model)(void *ctx, const char *name, qa_compat_qc_spatial_model *out);
  qa_owned_actor (*actor)(void *ctx, int64_t slot);
  int64_t (*reference)(void *ctx, qa_actor_id actor);
  bool (*is_free_entity)(void *ctx, int64_t slot);
  void (*prepare_entities)(void *ctx); /* nullable */
  void (*link)(void *ctx, int64_t slot);
} qa_compat_qc_spatial_world;
typedef struct qa_compat_qc_spatial_bindings { qa_compat_qc_host_binding entries[7]; size_t count; } qa_compat_qc_spatial_bindings;
qa_compat_qc_spatial_bindings qa_compat_qc_create_spatial_bindings(qa_compat_qc_spatial_world *world);
#endif
