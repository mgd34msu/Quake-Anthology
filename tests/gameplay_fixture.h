#ifndef QA_TEST_GAMEPLAY_FIXTURE_H
#define QA_TEST_GAMEPLAY_FIXTURE_H

#include "qa/binary.h"
#include "qa/collision.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, \
                #expression, error.message); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

typedef struct gameplay_map {
    uint8_t bytes[320];
    qa_collision_geometry *geometry;
} gameplay_map;

static inline void gameplay_float(void *destination, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    qa_store_u32le(destination, bits);
}

/* An original BSP29 floor. Its immutable bytes outlive the geometry. */
static inline void gameplay_map_create(gameplay_map *map)
{
    qa_error error = {0};
    *map = (gameplay_map){0};
    qa_store_u32le(map->bytes, 29);
    const uint32_t leaves = 124, model = leaves + 56;
    const uint32_t plane = model + 64, node = plane + 20, clip = node + 24;
    qa_store_u32le(map->bytes + 4 + 10 * 8, leaves);
    qa_store_u32le(map->bytes + 8 + 10 * 8, 56);
    qa_store_u32le(map->bytes + 4 + 14 * 8, model);
    qa_store_u32le(map->bytes + 8 + 14 * 8, 64);
    qa_store_u32le(map->bytes + 4 + 1 * 8, plane);
    qa_store_u32le(map->bytes + 8 + 1 * 8, 20);
    qa_store_u32le(map->bytes + 4 + 5 * 8, node);
    qa_store_u32le(map->bytes + 8 + 5 * 8, 24);
    qa_store_u32le(map->bytes + 4 + 9 * 8, clip);
    qa_store_u32le(map->bytes + 8 + 9 * 8, 8);
    gameplay_float(map->bytes + plane + 8, 1);
    qa_store_u32le(map->bytes + plane + 16, 2);
    qa_store_u16le(map->bytes + node + 4, UINT16_MAX - 1);
    qa_store_u16le(map->bytes + node + 6, UINT16_MAX);
    qa_store_u16le(map->bytes + clip + 4, UINT16_MAX);
    qa_store_u16le(map->bytes + clip + 6, UINT16_MAX - 1);
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t *leaf = map->bytes + leaves + i * 28;
        qa_store_u32le(leaf, i ? UINT32_MAX : UINT32_MAX - 1);
        qa_store_u32le(leaf + 4, UINT32_MAX);
        for (unsigned axis = 0; axis < 3; ++axis) {
            qa_store_u16le(leaf + 8 + axis * 2, (uint16_t)-1024);
            qa_store_u16le(leaf + 14 + axis * 2, 1024);
        }
    }
    for (unsigned axis = 0; axis < 3; ++axis) {
        gameplay_float(map->bytes + model + axis * 4, -1024);
        gameplay_float(map->bytes + model + 12 + axis * 4, 1024);
        qa_store_u16le(map->bytes + node + 8 + axis * 2, (uint16_t)-1024);
        qa_store_u16le(map->bytes + node + 14 + axis * 2, 1024);
    }
    qa_store_u32le(map->bytes + model + 52, 1);
    qa_bsp_view bsp;
    GAME_CHECK(qa_bsp_open((qa_bytes){map->bytes, clip + 8}, &bsp, &error));
    GAME_CHECK(qa_collision_create(&bsp, &map->geometry, &error));
}

#endif
