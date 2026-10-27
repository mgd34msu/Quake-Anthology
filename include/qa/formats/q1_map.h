/*
 * Umbrella for src/formats/q1-map/index.ts. The index module's runtime
 * entry points (readQ1Bsp/readQ1Lit) are declared here; the rest are
 * re-exported from the per-file headers. C accommodation: the TS
 * try/catch around an external .lit becomes a non-aborting validation
 * with the same fallthrough. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q1_MAP_H
#define QA_FORMATS_Q1_MAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q1_map/extensions.h"
#include "qa/formats/q1_map/queries.h"
#include "qa/formats/q1_map/records.h"
#include "qa/formats/q1_map/textures.h"
#include "qa/formats/q1_map/types.h"

#define QA_Q1_BSP_VERSION 29
#define QA_Q1_BSP2_VERSION 0x32505342
#define QA_Q1_2PSB_VERSION 0x42535032
#define QA_Q1_QUAKE64_VERSION 0x51363420

/* Malloc'd RGB samples; has_sample_count false is the TS null default. */
uint8_t *qa_q1_read_lit(const uint8_t *data, size_t data_length, bool has_sample_count,
                        double sample_count, size_t *out_length);
void qa_q1_read_bsp(const uint8_t *data, size_t data_length, const qa_q1_map_options *options,
                    qa_q1_map *out);
void qa_q1_map_destroy(qa_q1_map *map);

#endif
