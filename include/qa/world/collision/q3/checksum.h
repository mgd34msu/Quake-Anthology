/* Port of src/world/collision/q3/checksum.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_CHECKSUM_H
#define QA_WORLD_COLLISION_Q3_CHECKSUM_H
#include <stddef.h>
#include <stdint.h>
typedef struct qa_collision_checksum_lump { double offset; double length; } qa_collision_checksum_lump;
double qa_collision_lump_checksum(const uint8_t *bytes, size_t byte_length, qa_collision_checksum_lump lump);
double qa_collision_checksum(const uint8_t *bytes, size_t byte_length, const qa_collision_checksum_lump *lumps, size_t lump_count);
#endif
