/* Port of src/network/q1/checksum.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_CHECKSUM_H
#define QA_NETWORK_Q1_CHECKSUM_H
#include <stddef.h>
#include <stdint.h>
double qa_quake_world_checksum(const uint8_t *bytes, size_t length, double sequence);
int32_t qa_quake_world_map_checksum2(const uint8_t *bytes, size_t length);
#endif
