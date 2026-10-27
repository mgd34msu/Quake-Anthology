/*
 * Port of src/formats/bsp-kind.ts. C accommodations: the byte input is
 * an explicit pointer+length; `source` NULL means "<bsp>"; the result
 * is a static string ("q1"|"q2"|"q3"). BinaryError aborts via
 * qa_binary_error. Returns NULL only on allocation failure.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_BSP_KIND_H
#define QA_FORMATS_BSP_KIND_H

#include <stddef.h>
#include <stdint.h>

const char *qa_classify_bsp(const uint8_t *data, size_t length,
                            const char *source);

#endif
