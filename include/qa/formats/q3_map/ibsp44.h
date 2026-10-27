/* Port of src/formats/q3-map/ibsp44.ts. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q3_MAP_IBSP44_H
#define QA_FORMATS_Q3_MAP_IBSP44_H

#include <stddef.h>
#include <stdint.h>

/* Malloc'd IBSP46 bytes (a copy of the input when it is not v44). */
uint8_t *qa_q3_normalize_bsp(const uint8_t *data, size_t data_length, const char *source,
                             size_t *out_length);

#endif
