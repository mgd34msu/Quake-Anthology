/* Port of src/audio/geometry.ts. GPL-2.0-or-later. */
#include "qa/audio/geometry.h"

#include <math.h>
#include "qa/foundation/js.h"

double qa_geometry_transmission(qa_vec3 start, qa_vec3 end,
                                qa_audio_geometry_trace_fn trace, void *ctx) {
  double dx = end.x - start.x, dy = end.y - start.y, dz = end.z - start.z;
  double distance = qa_js_hypot3(dx, dy, dz); /* Math.hypot */
  if (distance == 0) return 1;
  qa_audio_geometry_hit forward = trace(start, end, ctx);
  if (forward.fraction == 1 && !forward.start_solid && !forward.all_solid)
    return 1;
  qa_audio_geometry_hit reverse = trace(end, start, ctx);
  double thickness =
      (forward.all_solid || reverse.all_solid)
          ? distance
          : qa_js_max(0, distance * (1 - (forward.start_solid ? 0 : forward.fraction) -
                                (reverse.start_solid ? 0 : reverse.fraction)));
  return qa_js_pow(0.5, 1 + thickness / 64);
}
