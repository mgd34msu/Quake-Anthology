/* Port of src/movement/q3/view.ts (PM_UpdateViewAngles). GPL-2.0-or-later. */
#include "qa/movement/q3/view.h"

#include <stdbool.h>
#include <stdint.h>

#include "qa/core/numeric.h"

static int32_t word16(double a, double b) { return (int32_t)((int16_t)qa_int32(a + b)); }

qa_q3_view_angles_result qa_q3_view_angles(qa_vec3 command, qa_vec3 delta, qa_vec3 previous,
                                           double health, double movement_type,
                                           const double *intermission, size_t intermission_count) {
  bool inter = false;
  for (size_t i = 0; i < intermission_count; i++)
    if (intermission[i] == movement_type) inter = true;
  if (inter || (movement_type != 2 && health <= 0)) {
    qa_q3_view_angles_result r = {previous, delta};
    return r;
  }
  int32_t pitch = word16(command.x, delta.x);
  if (pitch > 16000) { delta.x = (double)qa_int32(16000 - command.x); pitch = 16000; }
  else if (pitch < -16000) { delta.x = (double)qa_int32(-16000 - command.x); pitch = -16000; }
  qa_q3_view_angles_result r = {
      {(double)pitch * (360.0 / 65536.0),
       (double)word16(command.y, delta.y) * (360.0 / 65536.0),
       (double)word16(command.z, delta.z) * (360.0 / 65536.0)},
      delta};
  return r;
}
