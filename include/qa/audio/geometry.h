/*
 * Port of src/audio/geometry.ts: obstruction transmission policy.
 * The trace closure becomes a function pointer plus ctx.
 * GPL-2.0-or-later.
 */
#ifndef QA_AUDIO_GEOMETRY_H
#define QA_AUDIO_GEOMETRY_H

#include <stdbool.h>

#include "qa/contracts/math.h"

typedef struct qa_audio_geometry_hit {
  double fraction;
  bool start_solid;
  bool all_solid;
} qa_audio_geometry_hit;

typedef qa_audio_geometry_hit (*qa_audio_geometry_trace_fn)(qa_vec3 start,
                                                            qa_vec3 end,
                                                            void *ctx);

double qa_geometry_transmission(qa_vec3 start, qa_vec3 end,
                                qa_audio_geometry_trace_fn trace, void *ctx);

#endif
