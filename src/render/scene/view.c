/* Port of src/render/scene/view.ts. GPL-2.0-or-later. */
#include "qa/render/scene/view.h"
#include <math.h>
#include "qa/core/math.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
static float fr(double v) { return (float)v; }
double qa_scene_model_scale(qa_scene_model_transform model) {
  double s = model.has_scale ? model.scale : 1;
  if (!isfinite(s) || s == 0) qa_range_error("Model scale must be finite and nonzero");
  return s;
}
qa_vec3 qa_scene_local_vector(qa_vec3 v, qa_scene_model_transform m) {
  double s = qa_scene_model_scale(m);
  return qa_math_vec3(qa_dot3(v, m.axis.vectors[0]) / s, qa_dot3(v, m.axis.vectors[1]) / s, qa_dot3(v, m.axis.vectors[2]) / s);
}
qa_vec3 qa_scene_local_point(qa_vec3 p, qa_scene_model_transform m) { return qa_scene_local_vector(qa_sub3(p, m.origin), m); }
qa_mat4 qa_scene_perspective_projection(double fov_x, double fov_y, double far_p, double near_p) {
  if (!(isfinite(fov_x) && fov_x > 0 && fov_x < 180) || !(isfinite(fov_y) && fov_y > 0 && fov_y < 180)
      || !isfinite(far_p) || !isfinite(near_p) || near_p <= 0 || far_p <= near_p)
    qa_range_error("Camera requires finite fields of view and ordered positive clipping distances");
  double w = fr(2 * fr(near_p * tan(fov_x * 3.141592653589793 / 360)));
  double h = fr(2 * fr(near_p * tan(fov_y * 3.141592653589793 / 360))), d = fr(far_p - near_p);
  qa_mat4 r; for (int i = 0; i < 16; i++) r.values[i] = 0;
  r.values[0] = fr(2 * near_p / w); r.values[5] = fr(2 * near_p / h);
  r.values[10] = fr(-fr(far_p + near_p) / d); r.values[11] = -1;
  r.values[14] = fr(fr(-2 * far_p * near_p) / d); r.values[15] = 0;
  return r;
}
qa_mat4 qa_scene_view_projection_matrix(qa_scene_camera c) {
  qa_vec3 fw = c.axis.vectors[0], lf = c.axis.vectors[1], up = c.axis.vectors[2];
  qa_vec4 ex = { -lf.x, -lf.y, -lf.z, qa_dot3(c.origin, lf) };
  qa_vec4 ey = { up.x, up.y, up.z, -qa_dot3(c.origin, up) };
  qa_vec4 ez = { -fw.x, -fw.y, -fw.z, qa_dot3(c.origin, fw) };
  qa_mat4 v = { { ex.x, ey.x, ez.x, 0, ex.y, ey.y, ez.y, 0, ex.z, ey.z, ez.z, 0, ex.w, ey.w, ez.w, 1 } };
  return qa_multiply_mat4(c.projection, v);
}
static double comp(double x, double y, double z, double a, double b, double cc, double d) {
  return fr(fr(fr(fr(x * a) + fr(y * b)) + fr(z * cc)) + d);
}
qa_vec4 qa_scene_project_point(qa_mat4 m, qa_vec3 p) {
  return qa_math_vec4(comp(p.x, p.y, p.z, m.values[0], m.values[4], m.values[8], m.values[12]),
    comp(p.x, p.y, p.z, m.values[1], m.values[5], m.values[9], m.values[13]),
    comp(p.x, p.y, p.z, m.values[2], m.values[6], m.values[10], m.values[14]),
    comp(p.x, p.y, p.z, m.values[3], m.values[7], m.values[11], m.values[15]));
}
static qa_vec4 vrow(qa_vec3 a, double t) { return qa_math_vec4(a.x, a.y, a.z, t); }
qa_scene_view_projector qa_scene_create_view_projector(qa_scene_camera c) {
  qa_scene_view_projector p; p.projection = c.projection;
  p.eye_x = vrow(qa_scale3(c.axis.vectors[1], -1), qa_dot3(c.origin, c.axis.vectors[1]));
  p.eye_y = vrow(c.axis.vectors[2], -qa_dot3(c.origin, c.axis.vectors[2]));
  p.eye_z = vrow(qa_scale3(c.axis.vectors[0], -1), qa_dot3(c.origin, c.axis.vectors[0]));
  return p;
}
static qa_vec4 mrow(qa_scene_model_transform m, qa_vec3 a, double t) {
  double s = qa_scene_model_scale(m);
  return qa_math_vec4(fr(qa_dot3(m.axis.vectors[0], a) * s), fr(qa_dot3(m.axis.vectors[1], a) * s),
    fr(qa_dot3(m.axis.vectors[2], a) * s), fr(qa_dot3(m.origin, a) + t));
}
qa_scene_view_projector qa_scene_create_model_projector(qa_scene_camera c, qa_scene_model_transform m) {
  qa_scene_view_projector p; p.projection = c.projection;
  p.eye_x = mrow(m, qa_scale3(c.axis.vectors[1], -1), qa_dot3(c.origin, c.axis.vectors[1]));
  p.eye_y = mrow(m, c.axis.vectors[2], -qa_dot3(c.origin, c.axis.vectors[2]));
  p.eye_z = mrow(m, qa_scale3(c.axis.vectors[0], -1), qa_dot3(c.origin, c.axis.vectors[0]));
  return p;
}
/* TS dot3(point, eyeX): the binary32-staged dot over the row's x, y, z. */
static double dot4(qa_vec3 p, qa_vec4 e) { return qa_dot3(p, qa_math_vec3(e.x, e.y, e.z)); }
qa_vec4 qa_scene_projector_project(qa_scene_view_projector p, qa_vec3 pt) {
  double x = fr(dot4(pt, p.eye_x) + p.eye_x.w), y = fr(dot4(pt, p.eye_y) + p.eye_y.w), z = fr(dot4(pt, p.eye_z) + p.eye_z.w);
  const double *v = p.projection.values;
  return qa_math_vec4(comp(x, y, z, v[0], v[4], v[8], v[12]), comp(x, y, z, v[1], v[5], v[9], v[13]),
    comp(x, y, z, v[2], v[6], v[10], v[14]), comp(x, y, z, v[3], v[7], v[11], v[15]));
}
static qa_plane side(qa_scene_camera c, qa_vec3 dir, double scale, double sign) {
  double inv = 1 / qa_js_hypot2(1, scale);
  qa_vec3 n = qa_add3(qa_scale3(c.axis.vectors[0], inv), qa_scale3(dir, scale * inv * sign));
  return (qa_plane){ n, qa_dot3(c.origin, n) };
}
size_t qa_scene_camera_frustum(qa_scene_camera c, qa_plane out[5]) {
  out[0] = side(c, c.axis.vectors[1], c.projection.values[0], 1);
  out[1] = side(c, c.axis.vectors[1], c.projection.values[0], -1);
  out[2] = side(c, c.axis.vectors[2], c.projection.values[5], 1);
  out[3] = side(c, c.axis.vectors[2], c.projection.values[5], -1);
  if (c.clip_is_portal) { out[4] = c.clip_plane; return 5; }
  return 4;
}
bool qa_scene_bounds_in_frustum(qa_bounds b, const qa_plane *pl, size_t n) {
  for (size_t i = 0; i < n; i++) {
    double x = pl[i].normal.x >= 0 ? b.max.x : b.min.x, y = pl[i].normal.y >= 0 ? b.max.y : b.min.y, z = pl[i].normal.z >= 0 ? b.max.z : b.min.z;
    if (x * pl[i].normal.x + y * pl[i].normal.y + z * pl[i].normal.z < pl[i].distance) return false;
  }
  return true;
}
double qa_scene_far_clip(qa_vec3 origin, qa_bounds b) {
  double mx = 0;
  double xs[2] = { b.min.x, b.max.x }, ys[2] = { b.min.y, b.max.y }, zs[2] = { b.min.z, b.max.z };
  for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) for (int k = 0; k < 2; k++) {
    qa_vec3 r = qa_sub3(qa_math_vec3(xs[i], ys[j], zs[k]), origin);
    mx = qa_js_max(mx, qa_dot3(r, r)); /* Math.max keeps NaN */
  }
  return fr(sqrt(mx));
}
bool qa_scene_portal_clip_plane(qa_scene_camera c, qa_vec4 *out) {
  if (!c.clip_is_portal) return false;
  qa_plane pl = c.clip_plane; const double *v = c.projection.values;
  double a = qa_dot3(c.axis.vectors[0], pl.normal), b = qa_dot3(c.axis.vectors[1], pl.normal), cc = qa_dot3(c.axis.vectors[2], pl.normal);
  double d = fr(qa_dot3(pl.normal, c.origin) - pl.distance);
  *out = qa_math_vec4(-b / v[0], cc / v[5], d / v[14], a + d * v[10] / v[14]);
  return true;
}
qa_vec3 qa_scene_world_vector(qa_vec3 p, qa_scene_model_transform m) {
  double s = qa_scene_model_scale(m);
  return qa_add3(qa_add3(qa_scale3(m.axis.vectors[0], p.x * s), qa_scale3(m.axis.vectors[1], p.y * s)), qa_scale3(m.axis.vectors[2], p.z * s));
}
qa_vec3 qa_scene_world_point(qa_vec3 p, qa_scene_model_transform m) { return qa_add3(m.origin, qa_scene_world_vector(p, m)); }
