/* Port of src/materials/q2-lighting.ts. GPL-2.0-or-later. */
#include <math.h>
#include "qa/foundation/js.h"

#include "qa/core/math.h"
#include "qa/materials/q2_lighting.h"

double qa_point_light_falloff(double distance, double radius) {
  double effective = radius + 64;
  double used = effective - distance - 64;
  return (used > 0 ? used : 0) / effective;
}

double qa_spot_cone_attenuation(qa_vec3 direction, qa_vec3 cone_direction,
                                 double cone_cos) {
  double magnitude = -(direction.x * cone_direction.x +
                       direction.y * cone_direction.y +
                       direction.z * cone_direction.z);
  if (cone_cos >= 1) return 0;
  double value = 1 - (1 - magnitude) * (1 / (1 - cone_cos));
  return value > 0 ? value : 0;
}

qa_vec3 qa_calc_dynamic_light_contribution(qa_dynamic_light_sample light,
    qa_vec3 position, qa_vec3 normal) {
  qa_vec3 out = {0, 0, 0};
  qa_calc_dynamic_light_contribution_into(&out, light, position, normal);
  return out;
}

void qa_calc_dynamic_light_contribution_into(qa_vec3 *out,
    qa_dynamic_light_sample light, qa_vec3 position, qa_vec3 normal) {
  double lx = light.cone != NULL ? light.origin.x : light.origin.x + normal.x * 16;
  double ly = light.cone != NULL ? light.origin.y : light.origin.y + normal.y * 16;
  double lz = light.cone != NULL ? light.origin.z : light.origin.z + normal.z * 16;
  double dx = lx - position.x, dy = ly - position.y, dz = lz - position.z;
  if (isfinite(light.scale)) {
    double radius = light.radius;
    if (radius >= 0 && dx * dx + dy * dy + dz * dz >
                           radius * radius * (1 + qa_js_pow(2, -40))) {
      out->x = 0; out->y = 0; out->z = 0;
      return;
    }
    if (light.color.x >= 0) {
      double tx = dx * normal.x, ty = dy * normal.y, tz = dz * normal.z;
      double dot = tx + ty + tz;
      if (dot < -(fabs(tx) + fabs(ty) + fabs(tz)) * qa_js_pow(2, -40)) {
        out->x = 0; out->y = 0; out->z = 0;
        return;
      }
    }
  }
  qa_vec3 delta = {dx, dy, dz};
  double distance = qa_hypot3(delta);
  double falloff = qa_point_light_falloff(distance, light.radius);
  double inverse = 1 / (distance > 1 ? distance : 1);
  double dirx = dx * inverse, diry = dy * inverse, dirz = dz * inverse;
  double facing = dirx * normal.x + diry * normal.y + dirz * normal.z;
  double lambert = light.color.x < 0 ? 1 : (facing > 0 ? facing : 0);
  double scale = falloff * lambert * light.scale;
  if (light.cone != NULL) {
    qa_vec3 dir = {dirx, diry, dirz};
    scale *= qa_spot_cone_attenuation(dir, light.cone->direction,
                                      light.cone->cos_half_angle);
  }
  out->x = light.color.x * scale;
  out->y = light.color.y * scale;
  out->z = light.color.z * scale;
}
