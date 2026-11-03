/* Port of src/app/bootstrap/rerelease-presentation/fog.ts.
 * GPL-2.0-or-later. */
#include "qa/app/bootstrap/rerelease_presentation/fog.h"
#include <math.h>
#include <stdint.h>
#include "qa/foundation/js.h"
static double fr(double v) { return (double)(float)v; }
static double fraction(double value) {
  int64_t whole = qa_js_to_int32(trunc(fr(value * 255))); /* Math.trunc(...) & 255 */
  return fr((double)(whole & 255) / 255.0);
}
static qa_vec3 color_of(qa_vec3 v) {
  qa_vec3 out = {fraction(v.x), fraction(v.y), fraction(v.z)};
  return out;
}
qa_q2_fog_state qa_app_q2_fog_from_source(qa_q2_fog_state value) {
  qa_q2_fog_state out;
  out.fog.density = fr(value.fog.density);
  out.fog.color = color_of(value.fog.color);
  out.fog.sky_factor = fraction(value.fog.sky_factor);
  out.height_fog.start_color = color_of(value.height_fog.start_color);
  /* TS |0: ToInt32. */
  out.height_fog.start_distance = (double)qa_js_to_int32(value.height_fog.start_distance);
  out.height_fog.end_color = color_of(value.height_fog.end_color);
  out.height_fog.end_distance = (double)qa_js_to_int32(value.height_fog.end_distance);
  out.height_fog.falloff = fr(value.height_fog.falloff);
  out.height_fog.density = fr(value.height_fog.density);
  return out;
}
void qa_app_rerelease_fog_init(qa_app_rerelease_fog *fog) {
  fog->start = qa_q2_create_fog();
  fog->target = qa_q2_create_fog();
  fog->starts = 0;
  fog->duration = 0;
}
void qa_app_rerelease_fog_receive(qa_app_rerelease_fog *fog, qa_q2_fog_state value,
                                  double duration_milliseconds, double seconds) {
  if (duration_milliseconds != 0) {
    fog->start = fog->target;
    fog->starts = seconds * 1000;
  }
  fog->duration = duration_milliseconds;
  fog->target = qa_app_q2_fog_from_source(value);
}
static double mix(double a, double b, double back, double front) { return a * back + b * front; }
static qa_vec3 mix_color(qa_vec3 a, qa_vec3 b, double back, double front) {
  qa_vec3 out = {fr(mix(a.x, b.x, back, front)), fr(mix(a.y, b.y, back, front)),
                 fr(mix(a.z, b.z, back, front))};
  return out;
}
qa_scene_fog qa_app_rerelease_fog_current(const qa_app_rerelease_fog *fog, double seconds) {
  double elapsed = seconds * 1000 - fog->starts;
  double front = fog->duration == 0 || elapsed > fog->duration ? 1 : elapsed / fog->duration;
  double back = 1 - front;
  const qa_q2_fog_state *a = &fog->start, *b = &fog->target;
  qa_scene_fog out;
  out.kind = "q2";
  out.color = mix_color(a->fog.color, b->fog.color, back, front);
  out.density = mix(a->fog.density, b->fog.density, back, front);
  out.sky_factor = mix(a->fog.sky_factor, b->fog.sky_factor, back, front);
  out.depth_for_opaque = 0;
  out.height.start.color = mix_color(a->height_fog.start_color, b->height_fog.start_color, back, front);
  out.height.start.distance = mix(a->height_fog.start_distance, b->height_fog.start_distance, back, front);
  out.height.end.color = mix_color(a->height_fog.end_color, b->height_fog.end_color, back, front);
  out.height.end.distance = mix(a->height_fog.end_distance, b->height_fog.end_distance, back, front);
  out.height.density = mix(a->height_fog.density, b->height_fog.density, back, front);
  out.height.falloff = mix(a->height_fog.falloff, b->height_fog.falloff, back, front);
  out.plane = NULL;
  return out;
}
qa_q2_fog_state qa_app_q2_fog_from_wire(qa_q2_fog_state previous, qa_q2_svc_fog_data value) {
  int64_t bits = qa_js_to_int32(value.bits); /* value.bits & bit */
  qa_q2_fog_state out;
  out.fog.density = (bits & QA_Q2_SVC_FOG_BIT_DENSITY) ? value.density : previous.fog.density;
  out.fog.sky_factor = (bits & QA_Q2_SVC_FOG_BIT_DENSITY) ? value.skyfactor / 255 : previous.fog.sky_factor;
  out.fog.color.x = (bits & QA_Q2_SVC_FOG_BIT_R) ? value.red / 255 : previous.fog.color.x;
  out.fog.color.y = (bits & QA_Q2_SVC_FOG_BIT_G) ? value.green / 255 : previous.fog.color.y;
  out.fog.color.z = (bits & QA_Q2_SVC_FOG_BIT_B) ? value.blue / 255 : previous.fog.color.z;
  out.height_fog.falloff = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_FALLOFF) ? value.hf_falloff : previous.height_fog.falloff;
  out.height_fog.density = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_DENSITY) ? value.hf_density : previous.height_fog.density;
  out.height_fog.start_distance = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_DIST) ? value.hf_start_dist : previous.height_fog.start_distance;
  out.height_fog.end_distance = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_DIST) ? value.hf_end_dist : previous.height_fog.end_distance;
  out.height_fog.start_color.x = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_R) ? value.hf_start_r / 255 : previous.height_fog.start_color.x;
  out.height_fog.start_color.y = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_G) ? value.hf_start_g / 255 : previous.height_fog.start_color.y;
  out.height_fog.start_color.z = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_START_B) ? value.hf_start_b / 255 : previous.height_fog.start_color.z;
  out.height_fog.end_color.x = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_R) ? value.hf_end_r / 255 : previous.height_fog.end_color.x;
  out.height_fog.end_color.y = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_G) ? value.hf_end_g / 255 : previous.height_fog.end_color.y;
  out.height_fog.end_color.z = (bits & QA_Q2_SVC_FOG_BIT_HEIGHTFOG_END_B) ? value.hf_end_b / 255 : previous.height_fog.end_color.z;
  return out;
}
