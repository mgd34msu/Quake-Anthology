/* Port of src/content/q2/rerelease/fog.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/fog.h"
#include "qa/content/q2/foundation/fields.h"
#include <stdio.h>
#include <string.h>

qa_q2_fog_state qa_q2_rerelease_fog_fields(const qa_q2_spawn_fields *f, bool off) {
  const char *s = off ? "_off" : "";
  char k[64];
  qa_q2_fog_state r;
#define KEY(base) (snprintf(k, sizeof k, "%s%s", base, s), k)
  r.fog.density = qa_q2_number_field(f, KEY("fog_density"), 0);
  r.fog.color = qa_q2_vector_field(f, KEY("fog_color"));
  r.fog.sky_factor = qa_q2_number_field(f, KEY("fog_sky_factor"), 0);
  r.height_fog.start_color = qa_q2_vector_field(f, KEY("heightfog_start_color"));
  r.height_fog.start_distance = qa_q2_number_field(f, KEY("heightfog_start_dist"), 0);
  r.height_fog.end_color = qa_q2_vector_field(f, KEY("heightfog_end_color"));
  r.height_fog.end_distance = qa_q2_number_field(f, KEY("heightfog_end_dist"), 0);
  r.height_fog.density = qa_q2_number_field(f, KEY("heightfog_density"), 0);
  r.height_fog.falloff = qa_q2_number_field(f, KEY("heightfog_falloff"), 0);
  r.height_fog.density_first = true;
#undef KEY
  return r;
}
static double sc(double a, double b, double f) { return a + (b - a) * f; }
static qa_vec3 vc(qa_vec3 a, qa_vec3 b, double f) { qa_vec3 r = {sc(a.x,b.x,f), sc(a.y,b.y,f), sc(a.z,b.z,f)}; return r; }
qa_q2_fog_state qa_q2_interpolate_fog(qa_q2_fog_state off, qa_q2_fog_state on, double f) {
  qa_q2_fog_state r;
  r.fog.density = sc(off.fog.density, on.fog.density, f);
  r.fog.color = vc(off.fog.color, on.fog.color, f);
  r.fog.sky_factor = sc(off.fog.sky_factor, on.fog.sky_factor, f);
  r.height_fog.start_color = vc(off.height_fog.start_color, on.height_fog.start_color, f);
  r.height_fog.start_distance = sc(off.height_fog.start_distance, on.height_fog.start_distance, f);
  r.height_fog.end_color = vc(off.height_fog.end_color, on.height_fog.end_color, f);
  r.height_fog.end_distance = sc(off.height_fog.end_distance, on.height_fog.end_distance, f);
  r.height_fog.falloff = sc(off.height_fog.falloff, on.height_fog.falloff, f);
  r.height_fog.density = sc(off.height_fog.density, on.height_fog.density, f);
  r.height_fog.density_first = false;
  return r;
}
static bool veq(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool qa_q2_equal_fog(qa_q2_fog_state a, qa_q2_fog_state b) {
  return a.fog.density == b.fog.density && a.fog.sky_factor == b.fog.sky_factor && veq(a.fog.color, b.fog.color)
    && a.height_fog.density == b.height_fog.density && a.height_fog.falloff == b.height_fog.falloff
    && a.height_fog.start_distance == b.height_fog.start_distance && a.height_fog.end_distance == b.height_fog.end_distance
    && veq(a.height_fog.start_color, b.height_fog.start_color) && veq(a.height_fog.end_color, b.height_fog.end_color);
}
