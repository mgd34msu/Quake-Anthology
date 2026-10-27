/*
 * Port of src/formats/images/mip.ts. C accommodations: profile strings
 * become enums; outputs are malloc'd qa_image_level pixels freed with
 * qa_image_level_destroy. RangeError -> qa_range_error. Functions
 * returning int return 0 only on allocation failure. JS out-of-range
 * typed-array loads (the `?? 0` reads) yield 0, as in the TS.
 * GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_IMAGES_MIP_H
#define QA_FORMATS_IMAGES_MIP_H

#include <stddef.h>

#include "qa/contracts/render.h"

void qa_image_level_destroy(qa_image_level *level);

/* TS resample profile: "q1"|"q2"|"q3" (q2/q3 share the 2x2 average). */
typedef enum qa_resample_profile {
  QA_RESAMPLE_Q1, QA_RESAMPLE_Q2, QA_RESAMPLE_Q3
} qa_resample_profile;
int qa_resample_image(const qa_image_level *input, double width, double height,
                      qa_resample_profile profile, qa_image_level *out);

/* TS mip profile: "box"|"q3-weighted". */
typedef enum qa_mip_profile {
  QA_MIP_BOX, QA_MIP_Q3_WEIGHTED
} qa_mip_profile;
int qa_mip_image(const qa_image_level *input, qa_mip_profile profile,
                 qa_image_level *out);

/* levels[0] borrows the input's pixels (TS keeps the same object);
 * later levels own theirs. Destroy frees levels[1..] and the array. */
typedef struct qa_mip_chain {
  qa_image_level *levels;
  size_t count;
} qa_mip_chain;
int qa_generate_mip_chain(const qa_image_level *input, qa_mip_profile profile,
                          qa_mip_chain *out);
void qa_mip_chain_destroy(qa_mip_chain *chain);

#endif
