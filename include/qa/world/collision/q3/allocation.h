/* Port of src/world/collision/q3/allocation.ts: the allocation shape shared with the winding
 * library (TS dropped the source-hunk accounting). GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_ALLOCATION_H
#define QA_WORLD_COLLISION_Q3_ALLOCATION_H
#include <stddef.h>
#include <stdint.h>
typedef struct qa_zone_allocation { uint8_t *bytes; size_t byte_length; } qa_zone_allocation;
#endif
