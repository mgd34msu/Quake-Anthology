/* Port of src/world/collision/q3/allocation.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_ALLOCATION_H
#define QA_WORLD_COLLISION_Q3_ALLOCATION_H
#include <stddef.h>
#include <stdint.h>
typedef struct qa_zone_allocation { uint8_t *bytes; size_t byte_length; } qa_zone_allocation;
typedef struct qa_hunk_allocation {
  uint8_t *bytes; const char *kind; /* "permanent"|"temporary" */
  double byte_offset; double byte_length;
} qa_hunk_allocation;
/* TS HunkAccountingProfile: kind "unaccounted" | "source-hunk"(reserve fn+ctx). */
typedef qa_hunk_allocation (*qa_hunk_reserve_fn)(void *ctx, const char *source, const char *resource, double bytes, const char *preference);
typedef struct qa_hunk_accounting_profile {
  const char *kind; void *ctx; qa_hunk_reserve_fn reserve;
} qa_hunk_accounting_profile;
typedef struct qa_source_hunk_release32 {
  double pointer, disk_shader, collision_model, collision_node, brush, leaf, area, plane, brush_side, collision_patch;
} qa_source_hunk_release32;
extern const qa_source_hunk_release32 qa_source_hunk_release32_sizes;
#endif
