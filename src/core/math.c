/* Port of src/core/math.ts. GPL-2.0-or-later. */
#include "qa/core/math.h"

#include <math.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"

/* Math.fround: round a binary64 to the nearest binary32 value. */
static float fr(double value) { return (float)value; }
static const double DEGREES_TO_RADIANS = 3.141592653589793 * 2.0 / 360.0;
static const double QA_PI = 3.141592653589793;

qa_vec2 qa_math_vec2(double x, double y) {
  qa_vec2 v = {(double)fr(x), (double)fr(y)};
  return v;
}
qa_vec3 qa_math_vec3(double x, double y, double z) {
  qa_vec3 v = {(double)fr(x), (double)fr(y), (double)fr(z)};
  return v;
}
qa_vec4 qa_math_vec4(double x, double y, double z, double w) {
  qa_vec4 v = {(double)fr(x), (double)fr(y), (double)fr(z), (double)fr(w)};
  return v;
}

qa_vec3 qa_add3(qa_vec3 a, qa_vec3 b) { return qa_math_vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
void qa_add3_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b) {
  double x = (double)fr(a.x + b.x), y = (double)fr(a.y + b.y), z = (double)fr(a.z + b.z);
  out->x = x; out->y = y; out->z = z;
}
qa_vec3 qa_sub3(qa_vec3 a, qa_vec3 b) { return qa_math_vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
void qa_sub3_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b) {
  double x = (double)fr(a.x - b.x), y = (double)fr(a.y - b.y), z = (double)fr(a.z - b.z);
  out->x = x; out->y = y; out->z = z;
}
qa_vec3 qa_scale3(qa_vec3 value, double scale) {
  return qa_math_vec3(value.x * scale, value.y * scale, value.z * scale);
}
void qa_scale3_into(qa_vec3 *out, qa_vec3 value, double scale) {
  double x = (double)fr(value.x * scale), y = (double)fr(value.y * scale);
  double z = (double)fr(value.z * scale);
  out->x = x; out->y = y; out->z = z;
}
static double dot_components(double ax, double ay, double az,
                             double bx, double by, double bz) {
  return (double)fr((double)fr((double)fr(ax * bx) + (double)fr(ay * by)) +
                    (double)fr(az * bz));
}
double qa_dot3(qa_vec3 a, qa_vec3 b) { return dot_components(a.x, a.y, a.z, b.x, b.y, b.z); }
qa_vec3 qa_cross3(qa_vec3 a, qa_vec3 b) {
  return qa_math_vec3((double)fr(a.y * b.z) - (double)fr(a.z * b.y),
                      (double)fr(a.z * b.x) - (double)fr(a.x * b.z),
                      (double)fr(a.x * b.y) - (double)fr(a.y * b.x));
}
void qa_cross3_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b) {
  double ax = a.x, ay = a.y, az = a.z, bx = b.x, by = b.y, bz = b.z;
  double x = (double)fr((double)fr(ay * bz) - (double)fr(az * by));
  double y = (double)fr((double)fr(az * bx) - (double)fr(ax * bz));
  double z = (double)fr((double)fr(ax * by) - (double)fr(ay * bx));
  out->x = x; out->y = y; out->z = z;
}
double qa_length3(qa_vec3 value) { return (double)fr(sqrt(qa_dot3(value, value))); }
qa_vec3 qa_normalize3(qa_vec3 value) {
  double length = qa_length3(value);
  if (length == 0) return qa_math_vec3(value.x, value.y, value.z);
  return qa_scale3(value, (double)fr(1.0 / length));
}
qa_vec3 qa_normalize3_or_zero(qa_vec3 value) {
  double length = qa_length3(value);
  if (length == 0) return qa_math_vec3(0, 0, 0);
  return qa_scale3(value, (double)fr(1.0 / length));
}
double qa_normalize3_into(qa_vec3 *out, qa_vec3 value) {
  double length = qa_length3(value);
  if (length == 0) {
    double x = (double)fr(value.x), y = (double)fr(value.y), z = (double)fr(value.z);
    out->x = x; out->y = y; out->z = z;
    return length;
  }
  qa_scale3_into(out, value, (double)fr(1.0 / length));
  return length;
}
double qa_normalize3_or_zero_into(qa_vec3 *out, qa_vec3 value) {
  double length = qa_length3(value);
  if (length == 0) {
    out->x = 0; out->y = 0; out->z = 0;
    return length;
  }
  qa_scale3_into(out, value, (double)fr(1.0 / length));
  return length;
}

static int32_t math_signed_integer(double value) {
  if (value != trunc(value) || value < -2147483648.0 || value > 2147483647.0) {
    qa_range_error("q_math integer argument must be a signed 32-bit integer");
  }
  return qa_int32(value);
}
int32_t qa_clamp_short(double value) {
  int32_t integer = math_signed_integer(value);
  if (integer < -32768) return -32768;
  if (integer > 32767) return 32767;
  return integer;
}
int32_t qa_q_log2(double value) {
  int32_t integer = math_signed_integer(value);
  if (integer < 0) qa_range_error("Q_log2 does not terminate for negative source integers");
  int32_t answer = 0;
  while ((integer >>= 1) != 0) answer++;
  return answer;
}

double qa_normalize_color(qa_vec3 input, qa_vec3 *output) {
  double maximum = (double)fr(input.x);
  if ((double)fr(input.y) > maximum) maximum = (double)fr(input.y);
  if ((double)fr(input.z) > maximum) maximum = (double)fr(input.z);
  if (maximum == 0) {
    output->x = 0; output->y = 0; output->z = 0;
  } else {
    output->x = (double)fr((double)fr(input.x) / maximum);
    output->y = (double)fr((double)fr(input.y) / maximum);
    output->z = (double)fr((double)fr(input.z) / maximum);
  }
  return maximum;
}

static uint8_t native_color_byte(double value) {
  double integer = trunc((double)fr((double)fr(value) * 255.0));
  if (!isfinite(integer) || integer < 0 || integer > 255) {
    qa_range_error("ColorBytes3 native float-to-byte conversion is outside its defined range");
  }
  return (uint8_t)integer;
}
uint32_t qa_color_bytes3(double red, double green, double blue,
                         uint8_t *storage, size_t storage_length) {
  if (storage_length < 4) qa_range_error("ColorBytes3 requires a retained four-byte word");
  storage[0] = native_color_byte(red);
  storage[1] = native_color_byte(green);
  storage[2] = native_color_byte(blue);
  return qa_load_u32_le(storage, storage_length, 0);
}

qa_vec3 qa_lerp3(qa_vec3 from, qa_vec3 to, double fraction) {
  return qa_math_vec3(from.x + fraction * (to.x - from.x),
                      from.y + fraction * (to.y - from.y),
                      from.z + fraction * (to.z - from.z));
}
void qa_lerp3_into(qa_vec3 *out, qa_vec3 from, qa_vec3 to, double fraction) {
  double x = (double)fr(from.x + fraction * (to.x - from.x));
  double y = (double)fr(from.y + fraction * (to.y - from.y));
  double z = (double)fr(from.z + fraction * (to.z - from.z));
  out->x = x; out->y = y; out->z = z;
}

qa_vec3 qa_add3_double(qa_vec3 a, qa_vec3 b) {
  qa_vec3 v = {a.x + b.x, a.y + b.y, a.z + b.z};
  return v;
}
qa_vec3 qa_sub3_double(qa_vec3 a, qa_vec3 b) {
  qa_vec3 v = {a.x - b.x, a.y - b.y, a.z - b.z};
  return v;
}
qa_vec3 qa_scale3_double(qa_vec3 value, double scale) {
  qa_vec3 v = {value.x * scale, value.y * scale, value.z * scale};
  return v;
}
qa_vec3 qa_ma3_double(qa_vec3 base, double scale, qa_vec3 direction) {
  qa_vec3 v = {base.x + direction.x * scale, base.y + direction.y * scale,
               base.z + direction.z * scale};
  return v;
}
double qa_dot3_double(qa_vec3 a, qa_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
qa_vec3 qa_cross3_double(qa_vec3 a, qa_vec3 b) {
  qa_vec3 v = {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
  return v;
}
double qa_length3_double(qa_vec3 value) {
  return sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
qa_vec3 qa_lerp3_double(qa_vec3 from, qa_vec3 to, double fraction) {
  qa_vec3 v = {from.x + (to.x - from.x) * fraction, from.y + (to.y - from.y) * fraction,
               from.z + (to.z - from.z) * fraction};
  return v;
}
qa_vec3 qa_cross3_stored(qa_vec3 a, qa_vec3 b) {
  return qa_math_vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
qa_vec3 qa_normalize3_stored(qa_vec3 value) {
  double length = sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return length == 0 ? value : qa_scale3(value, 1.0 / length);
}
qa_vec3 qa_normalize3_stored_or_zero(qa_vec3 value) {
  double length = sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  return length == 0 ? qa_math_vec3(0, 0, 0) : qa_scale3(value, 1.0 / length);
}
/* Math.hypot(x,y,z): scaled hypotenuse, max component factored out. */
double qa_hypot3(qa_vec3 value) {
  double ax = fabs(value.x), ay = fabs(value.y), az = fabs(value.z);
  if (isinf(ax) || isinf(ay) || isinf(az)) return INFINITY;
  double max = ax;
  if (ay > max) max = ay;
  if (az > max) max = az;
  if (max == 0) return 0;
  double sx = ax / max, sy = ay / max, sz = az / max;
  return max * sqrt(sx * sx + sy * sy + sz * sz);
}
double qa_distance3_double(qa_vec3 a, qa_vec3 b) {
  return qa_length3_double(qa_sub3_double(a, b));
}
qa_vec3 qa_normalize3_double_hypot(qa_vec3 value) {
  double magnitude = qa_hypot3(value);
  return magnitude == 0 ? value : qa_scale3_double(value, 1.0 / magnitude);
}
qa_vec3 qa_normalize3_double_or_zero(qa_vec3 value) {
  double length = qa_length3_double(value);
  if (length == 0) {
    qa_vec3 v = {0, 0, 0};
    return v;
  }
  qa_vec3 v = {value.x / length, value.y / length, value.z / length};
  return v;
}

void qa_angle_vectors_rule_into(qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up,
                                qa_vec3 angles, qa_angle_math_rule rule) {
  if (rule == QA_ANGLE_RULE_FLOAT || rule == QA_ANGLE_RULE_VM) {
    float vm_radians = fr(QA_PI * 2.0 / 360.0);
    float yaw = rule == QA_ANGLE_RULE_VM ? fr((double)fr(angles.y) * (double)vm_radians)
                                         : fr(angles.y * DEGREES_TO_RADIANS);
    float pitch = rule == QA_ANGLE_RULE_VM ? fr((double)fr(angles.x) * (double)vm_radians)
                                           : fr(angles.x * DEGREES_TO_RADIANS);
    float roll = rule == QA_ANGLE_RULE_VM ? fr((double)fr(angles.z) * (double)vm_radians)
                                          : fr(angles.z * DEGREES_TO_RADIANS);
    float sy = fr(sin((double)yaw)), cy = fr(cos((double)yaw));
    float sp = fr(sin((double)pitch)), cp = fr(cos((double)pitch));
    float sr = fr(sin((double)roll)), cr = fr(cos((double)roll));
    forward->x = (double)fr((double)cp * (double)cy);
    forward->y = (double)fr((double)cp * (double)sy);
    forward->z = (double)fr(-(double)sp);
    right->x = (double)fr((double)fr((double)fr(-(double)sr * (double)sp) * (double)cy) +
                          (double)fr(-(double)cr * -(double)sy));
    right->y = (double)fr((double)fr((double)fr(-(double)sr * (double)sp) * (double)sy) +
                          (double)fr(-(double)cr * (double)cy));
    right->z = (double)fr(-(double)sr * (double)cp);
    up->x = (double)fr((double)fr((double)fr((double)cr * (double)sp) * (double)cy) +
                       (double)fr(-(double)sr * -(double)sy));
    up->y = (double)fr((double)fr((double)fr((double)cr * (double)sp) * (double)sy) +
                       (double)fr(-(double)sr * (double)cy));
    up->z = (double)fr((double)cr * (double)cp);
    return;
  }
  double yaw, pitch, roll;
  if (rule == QA_ANGLE_RULE_DONOR) {
    yaw = angles.y * DEGREES_TO_RADIANS;
    pitch = angles.x * DEGREES_TO_RADIANS;
    roll = angles.z * DEGREES_TO_RADIANS;
  } else if (rule == QA_ANGLE_RULE_BOT) {
    double deg_to_rad = QA_PI / 180.0;
    yaw = angles.y * deg_to_rad;
    pitch = angles.x * deg_to_rad;
    roll = angles.z * deg_to_rad;
  } else {
    yaw = angles.y * QA_PI / 180.0;
    pitch = angles.x * QA_PI / 180.0;
    roll = angles.z * QA_PI / 180.0;
  }
  double sy = sin(yaw), cy = cos(yaw), sp = sin(pitch), cp = cos(pitch);
  double sr = sin(roll), cr = cos(roll);
  if (rule == QA_ANGLE_RULE_DONOR || rule == QA_ANGLE_RULE_Q1) {
    forward->x = (double)fr(cp * cy);
    forward->y = (double)fr(cp * sy);
    forward->z = (double)fr(-sp);
    right->x = (double)fr(-sr * sp * cy + cr * sy);
    right->y = (double)fr(-sr * sp * sy - cr * cy);
    right->z = (double)fr(-sr * cp);
    up->x = (double)fr(cr * sp * cy + sr * sy);
    up->y = (double)fr(cr * sp * sy - sr * cy);
    up->z = (double)fr(cr * cp);
    return;
  }
  forward->x = cp * cy; forward->y = cp * sy; forward->z = -sp;
  right->x = -sr * sp * cy + cr * sy;
  right->y = -sr * sp * sy - cr * cy;
  right->z = -sr * cp;
  up->x = cr * sp * cy + sr * sy;
  up->y = cr * sp * sy - sr * cy;
  up->z = cr * cp;
}

qa_angle_vectors qa_math_angle_vectors_rule(qa_vec3 angles, qa_angle_math_rule rule) {
  qa_angle_vectors result = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  qa_angle_vectors_rule_into(&result.forward, &result.right, &result.up, angles, rule);
  return result;
}
void qa_angle_vectors_into(qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up, qa_vec3 angles) {
  qa_angle_vectors_rule_into(forward, right, up, angles, QA_ANGLE_RULE_FLOAT);
}
qa_angle_vectors qa_math_angle_vectors(qa_vec3 angles) {
  return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_FLOAT);
}
qa_axis qa_angles_to_axis(qa_vec3 angles) {
  qa_angle_vectors vectors = qa_math_angle_vectors(angles);
  qa_axis axis = {{vectors.forward, qa_scale3(vectors.right, -1), vectors.up}};
  return axis;
}
void qa_angles_to_axis_into(qa_vec3 *forward, qa_vec3 *left, qa_vec3 *up, qa_vec3 angles) {
  qa_angle_vectors_into(forward, left, up, angles);
  left->x = (double)fr(left->x * -1.0);
  left->y = (double)fr(left->y * -1.0);
  left->z = (double)fr(left->z * -1.0);
}

static qa_vec3 vector_to_angles_float(qa_vec3 value) {
  float qvm_pi = fr(QA_PI);
  double yaw, pitch;
  if (value.y == 0 && value.x == 0) {
    yaw = 0;
    pitch = value.z > 0 ? 90 : 270;
  } else {
    if (value.x != 0) {
      float yaw_radians = fr(atan2(value.y, value.x));
      yaw = (double)fr((double)fr((double)yaw_radians * 180.0) / (double)qvm_pi);
    } else {
      yaw = value.y > 0 ? 90 : 270;
    }
    if (yaw < 0) yaw = (double)fr(yaw + 360.0);
    float x_squared = fr(value.x * value.x);
    float y_squared = fr(value.y * value.y);
    float forward_squared = fr((double)x_squared + (double)y_squared);
    float forward = fr(sqrt((double)forward_squared));
    float pitch_radians = fr(atan2(value.z, (double)forward));
    pitch = (double)fr((double)fr((double)pitch_radians * 180.0) / (double)qvm_pi);
    if (pitch < 0) pitch = (double)fr(pitch + 360.0);
  }
  return qa_math_vec3(-pitch, yaw, 0);
}

qa_vec3 qa_vector_to_angles_with_rule(qa_vec3 value, qa_vector_to_angles_rule rule) {
  if (rule == QA_VECTOR_TO_ANGLES_NATIVE) {
    if (value.x == 0 && value.y == 0) {
      qa_vec3 v = {value.z > 0 ? -90 : -270, 0, 0};
      return v;
    }
    double yaw = atan2(value.y, value.x) * 180.0 / QA_PI;
    if (yaw < 0) yaw += 360.0;
    double pitch = atan2(value.z, hypot(value.x, value.y)) * 180.0 / QA_PI;
    if (pitch < 0) pitch += 360.0;
    qa_vec3 v = {-pitch, yaw, 0};
    return v;
  }
  if (rule == QA_VECTOR_TO_ANGLES_NATIVE_AI) {
    double yaw = value.x == 0 && value.y == 0 ? 0 : atan2(value.y, value.x) * 180.0 / QA_PI;
    double pitch = atan2(value.z, hypot(value.x, value.y)) * 180.0 / QA_PI;
    qa_vec3 v = {-pitch, yaw < 0 ? yaw + 360.0 : yaw, 0};
    return v;
  }
  if (rule == QA_VECTOR_TO_ANGLES_BOT) {
    if (value.x == 0 && value.y == 0) {
      qa_vec3 v = {value.z > 0 ? -90 : 90, 0, 0};
      return v;
    }
    double rad_to_deg = 180.0 / QA_PI;
    /* JS % keeps the dividend's sign, like fmod. */
    double yaw = fmod(fmod(atan2(value.y, value.x) * rad_to_deg, 360.0) + 360.0, 360.0);
    double forward = sqrt(value.x * value.x + value.y * value.y);
    qa_vec3 v = {-atan2(value.z, forward) * rad_to_deg, yaw, 0};
    return v;
  }
  if (rule == QA_VECTOR_TO_ANGLES_BOT_Q3) {
    double yaw, pitch;
    if (value.y == 0 && value.x == 0) {
      yaw = 0;
      pitch = value.z > 0 ? 90 : 270;
    } else {
      yaw = value.x != 0 ? (double)fr(atan2(value.y, value.x) * 180.0 / QA_PI)
                         : value.y > 0 ? 90 : 270;
      if (yaw < 0) yaw = (double)fr(yaw + 360.0);
      float forward = fr(sqrt((double)fr((double)fr(value.x * value.x) +
                                          (double)fr(value.y * value.y))));
      pitch = (double)fr(atan2(value.z, (double)forward) * 180.0 / QA_PI);
      if (pitch < 0) pitch = (double)fr(pitch + 360.0);
    }
    return qa_math_vec3(-pitch, yaw, 0);
  }
  return vector_to_angles_float(value);
}

qa_vec3 qa_vector_to_angles(qa_vec3 value) { return vector_to_angles_float(value); }

double qa_angle_mod(double angle) {
  return (360.0 / 65536.0) * (double)(qa_int32(angle * (65536.0 / 360.0)) & 65535);
}
double qa_angle_mod_scaled(double angle) {
  return (double)(qa_int32(trunc(angle * 65536.0 / 360.0)) & 65535) * 360.0 / 65536.0;
}
double qa_qvm_angle_mod(double angle) {
  float scaled = fr((double)fr(angle) * (double)fr(65536.0 / 360.0));
  return (double)fr((double)(qa_qvm_float_to_int((double)scaled) & 65535) *
                    (double)fr(360.0 / 65536.0));
}
qa_angle_vectors qa_qvm_angle_vectors(qa_vec3 angles) {
  return qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_VM);
}
qa_axis qa_qvm_angles_to_axis(qa_vec3 angles) {
  qa_angle_vectors vectors = qa_qvm_angle_vectors(angles);
  qa_axis axis = {{vectors.forward, qa_sub3(qa_math_vec3(0, 0, 0), vectors.right), vectors.up}};
  return axis;
}
double qa_angle_normalize360(double angle) { return qa_angle_mod(angle); }
double qa_angle_normalize180(double angle) {
  double normalized = qa_angle_normalize360(angle);
  return normalized > 180 ? normalized - 360 : normalized;
}
double qa_angle_delta(double angle1, double angle2) {
  return qa_angle_normalize180(angle1 - angle2);
}

void qa_project_point_on_plane_into(qa_vec3 *out, qa_vec3 point, qa_vec3 normal) {
  double px = point.x, py = point.y, pz = point.z;
  double nx = normal.x, ny = normal.y, nz = normal.z;
  double inverse_denominator = (double)fr(1.0 / dot_components(nx, ny, nz, nx, ny, nz));
  double distance = (double)fr(dot_components(nx, ny, nz, px, py, pz) * inverse_denominator);
  double sx = (double)fr(nx * inverse_denominator);
  double sy = (double)fr(ny * inverse_denominator);
  double sz = (double)fr(nz * inverse_denominator);
  out->x = (double)fr(px - (double)fr(sx * distance));
  out->y = (double)fr(py - (double)fr(sy * distance));
  out->z = (double)fr(pz - (double)fr(sz * distance));
}
qa_vec3 qa_project_point_on_plane(qa_vec3 point, qa_vec3 normal) {
  qa_vec3 out = {0, 0, 0};
  qa_project_point_on_plane_into(&out, point, normal);
  return out;
}

void qa_perpendicular_vector_into(qa_vec3 *out, qa_vec3 source) {
  double sx = source.x, sy = source.y, sz = source.z;
  double ax = 1, ay = 0, az = 0;
  double minimum = 1;
  if (fabs(sx) < minimum) { minimum = fabs(sx); ax = 1; ay = 0; az = 0; }
  if (fabs(sy) < minimum) { minimum = fabs(sy); ax = 0; ay = 1; az = 0; }
  if (fabs(sz) < minimum) { ax = 0; ay = 0; az = 1; }
  double inverse_denominator = (double)fr(1.0 / dot_components(sx, sy, sz, sx, sy, sz));
  double distance = (double)fr(dot_components(sx, sy, sz, ax, ay, az) * inverse_denominator);
  double px = (double)fr(ax - (double)fr((double)fr(sx * inverse_denominator) * distance));
  double py = (double)fr(ay - (double)fr((double)fr(sy * inverse_denominator) * distance));
  double pz = (double)fr(az - (double)fr((double)fr(sz * inverse_denominator) * distance));
  double length = (double)fr(sqrt(dot_components(px, py, pz, px, py, pz)));
  if (length == 0) {
    out->x = px; out->y = py; out->z = pz;
    return;
  }
  double scale = (double)fr(1.0 / length);
  out->x = (double)fr(px * scale);
  out->y = (double)fr(py * scale);
  out->z = (double)fr(pz * scale);
}
qa_vec3 qa_perpendicular_vector(qa_vec3 source) {
  qa_vec3 out = {0, 0, 0};
  qa_perpendicular_vector_into(&out, source);
  return out;
}

static void multiply_mat3_into(double out[9], const double a[9], const double b[9]) {
  for (int row = 0; row < 3; row++) {
    double ax = a[row * 3], ay = a[row * 3 + 1], az = a[row * 3 + 2];
    for (int column = 0; column < 3; column++) {
      out[row * 3 + column] = dot_components(ax, ay, az, b[column], b[3 + column], b[6 + column]);
    }
  }
}

void qa_rotate_point_around_vector_rule_into(qa_vec3 *out, qa_vec3 direction,
                                             qa_vec3 point, double degrees,
                                             qa_rotate_rule rule) {
  qa_vec3 forward = rule == QA_ROTATE_VM ? qa_math_vec3(direction.x, direction.y, direction.z)
                                         : direction;
  qa_vec3 input = rule == QA_ROTATE_VM ? qa_math_vec3(point.x, point.y, point.z) : point;
  qa_vec3 radial = {0, 0, 0}, vertical = {0, 0, 0};
  qa_perpendicular_vector_into(&radial, forward);
  qa_cross3_into(&vertical, radial, forward);
  float radians = rule == QA_ROTATE_VM
                      ? fr((double)fr((double)fr(degrees) * (double)fr(QA_PI)) / 180.0)
                      : fr((double)fr(degrees) * QA_PI / 180.0);
  float cosine = fr(cos((double)radians));
  float sine = fr(sin((double)radians));
  double basis[9] = {radial.x, vertical.x, forward.x,
                     radial.y, vertical.y, forward.y,
                     radial.z, vertical.z, forward.z};
  double z_rotation[9] = {(double)cosine, (double)sine, 0,
                          -(double)sine, (double)cosine, 0, 0, 0, 1};
  double inverse[9] = {radial.x, radial.y, radial.z,
                       vertical.x, vertical.y, vertical.z,
                       forward.x, forward.y, forward.z};
  double temporary[9] = {0}, rotation[9] = {0};
  multiply_mat3_into(temporary, basis, z_rotation);
  multiply_mat3_into(rotation, temporary, inverse);
  double px = input.x, py = input.y, pz = input.z;
  out->x = dot_components(rotation[0], rotation[1], rotation[2], px, py, pz);
  out->y = dot_components(rotation[3], rotation[4], rotation[5], px, py, pz);
  out->z = dot_components(rotation[6], rotation[7], rotation[8], px, py, pz);
}
void qa_rotate_point_around_vector_into(qa_vec3 *out, qa_vec3 direction,
                                        qa_vec3 point, double degrees) {
  qa_rotate_point_around_vector_rule_into(out, direction, point, degrees, QA_ROTATE_FLOAT);
}
qa_vec3 qa_qvm_rotate_point_around_vector(qa_vec3 direction, qa_vec3 point, double degrees) {
  qa_vec3 out = {0, 0, 0};
  qa_rotate_point_around_vector_rule_into(&out, direction, point, degrees, QA_ROTATE_VM);
  return out;
}
qa_vec3 qa_rotate_point_around_vector(qa_vec3 direction, qa_vec3 point, double degrees) {
  qa_vec3 out = {0, 0, 0};
  qa_rotate_point_around_vector_into(&out, direction, point, degrees);
  return out;
}

bool qa_math_bounds_overlap(qa_bounds a, qa_bounds b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y &&
         a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}
qa_bounds qa_empty_bounds(void) {
  qa_bounds bounds = {qa_math_vec3(99999, 99999, 99999), qa_math_vec3(-99999, -99999, -99999)};
  return bounds;
}
qa_bounds qa_add_point_to_bounds(qa_bounds bounds, qa_vec3 point) {
  qa_bounds result = {
      qa_math_vec3(point.x < bounds.min.x ? point.x : bounds.min.x,
                   point.y < bounds.min.y ? point.y : bounds.min.y,
                   point.z < bounds.min.z ? point.z : bounds.min.z),
      qa_math_vec3(point.x > bounds.max.x ? point.x : bounds.max.x,
                   point.y > bounds.max.y ? point.y : bounds.max.y,
                   point.z > bounds.max.z ? point.z : bounds.max.z)};
  return result;
}
double qa_radius_from_bounds(qa_bounds bounds) {
  double corner_x = (double)fr(fabs(bounds.min.x) > fabs(bounds.max.x) ? fabs(bounds.min.x) : fabs(bounds.max.x));
  double corner_y = (double)fr(fabs(bounds.min.y) > fabs(bounds.max.y) ? fabs(bounds.min.y) : fabs(bounds.max.y));
  double corner_z = (double)fr(fabs(bounds.min.z) > fabs(bounds.max.z) ? fabs(bounds.min.z) : fabs(bounds.max.z));
  return (double)fr(sqrt(dot_components(corner_x, corner_y, corner_z, corner_x, corner_y, corner_z)));
}
bool qa_plane_from_points(qa_plane *out, qa_vec3 a, qa_vec3 b, qa_vec3 c) {
  double first_x = (double)fr(b.x - a.x), first_y = (double)fr(b.y - a.y), first_z = (double)fr(b.z - a.z);
  double second_x = (double)fr(c.x - a.x), second_y = (double)fr(c.y - a.y), second_z = (double)fr(c.z - a.z);
  double cross_x = (double)fr((double)fr(second_y * first_z) - (double)fr(second_z * first_y));
  double cross_y = (double)fr((double)fr(second_z * first_x) - (double)fr(second_x * first_z));
  double cross_z = (double)fr((double)fr(second_x * first_y) - (double)fr(second_y * first_x));
  double length = (double)fr(sqrt(dot_components(cross_x, cross_y, cross_z, cross_x, cross_y, cross_z)));
  if (length == 0) return false;
  double scale = (double)fr(1.0 / length);
  qa_vec3 normal = qa_math_vec3(cross_x * scale, cross_y * scale, cross_z * scale);
  out->normal = normal;
  out->distance = qa_dot3(a, normal);
  return true;
}
double qa_distance_to_plane(qa_plane plane, qa_vec3 point) {
  return qa_dot3(plane.normal, point) - plane.distance;
}
int qa_box_on_plane_side(qa_bounds bounds, qa_plane plane) {
  double nx = plane.normal.x, ny = plane.normal.y, nz = plane.normal.z;
  double front = dot_components(nx, ny, nz,
                                (double)fr(nx < 0 ? bounds.min.x : bounds.max.x),
                                (double)fr(ny < 0 ? bounds.min.y : bounds.max.y),
                                (double)fr(nz < 0 ? bounds.min.z : bounds.max.z)) - plane.distance;
  double back = dot_components(nx, ny, nz,
                               (double)fr(nx < 0 ? bounds.max.x : bounds.min.x),
                               (double)fr(ny < 0 ? bounds.max.y : bounds.min.y),
                               (double)fr(nz < 0 ? bounds.max.z : bounds.min.z)) - plane.distance;
  int sides = 0;
  if (front >= 0) sides = 1;
  if (back < 0) sides |= 2;
  return sides;
}

qa_mat4 qa_identity_mat4(void) {
  qa_mat4 m = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
  return m;
}
qa_mat4 qa_multiply_mat4(qa_mat4 a, qa_mat4 b) {
  const double *av = a.values, *bv = b.values;
  qa_mat4 m;
  for (int column = 0; column < 4; column++) {
    for (int row = 0; row < 4; row++) {
      /* Same left-to-right sum order as the TS literal expression. */
      m.values[column * 4 + row] = av[row] * bv[column * 4] + av[4 + row] * bv[column * 4 + 1] +
                                   av[8 + row] * bv[column * 4 + 2] + av[12 + row] * bv[column * 4 + 3];
    }
  }
  return m;
}
qa_mat4 qa_perspective_mat4(double vertical_fov_degrees, double aspect,
                            double near_plane, double far_plane) {
  double focal = 1.0 / tan(vertical_fov_degrees * DEGREES_TO_RADIANS / 2.0);
  double depth_scale = 1.0 / (near_plane - far_plane);
  qa_mat4 m = {{focal / aspect, 0, 0, 0,
                0, focal, 0, 0,
                0, 0, (far_plane + near_plane) * depth_scale, -1,
                0, 0, 2.0 * far_plane * near_plane * depth_scale, 0}};
  return m;
}
qa_mat4 qa_look_at_mat4(qa_vec3 eye, qa_vec3 target, qa_vec3 up) {
  double bx = (double)fr(eye.x - target.x), by = (double)fr(eye.y - target.y), bz = (double)fr(eye.z - target.z);
  double backward_length = (double)fr(sqrt(dot_components(bx, by, bz, bx, by, bz)));
  if (backward_length != 0) {
    double scale = (double)fr(1.0 / backward_length);
    bx = (double)fr(bx * scale); by = (double)fr(by * scale); bz = (double)fr(bz * scale);
  }
  double rx = (double)fr((double)fr(up.y * bz) - (double)fr(up.z * by));
  double ry = (double)fr((double)fr(up.z * bx) - (double)fr(up.x * bz));
  double rz = (double)fr((double)fr(up.x * by) - (double)fr(up.y * bx));
  double right_length = (double)fr(sqrt(dot_components(rx, ry, rz, rx, ry, rz)));
  if (right_length != 0) {
    double scale = (double)fr(1.0 / right_length);
    rx = (double)fr(rx * scale); ry = (double)fr(ry * scale); rz = (double)fr(rz * scale);
  }
  double ux = (double)fr((double)fr(by * rz) - (double)fr(bz * ry));
  double uy = (double)fr((double)fr(bz * rx) - (double)fr(bx * rz));
  double uz = (double)fr((double)fr(bx * ry) - (double)fr(by * rx));
  qa_mat4 m = {{rx, ux, bx, 0,
                ry, uy, by, 0,
                rz, uz, bz, 0,
                -dot_components(rx, ry, rz, eye.x, eye.y, eye.z),
                -dot_components(ux, uy, uz, eye.x, eye.y, eye.z),
                -dot_components(bx, by, bz, eye.x, eye.y, eye.z), 1}};
  return m;
}
qa_vec4 qa_transform_vec4(qa_mat4 matrix, qa_vec4 value) {
  const double *mv = matrix.values;
  return qa_math_vec4(mv[0] * value.x + mv[4] * value.y + mv[8] * value.z + mv[12] * value.w,
                      mv[1] * value.x + mv[5] * value.y + mv[9] * value.z + mv[13] * value.w,
                      mv[2] * value.x + mv[6] * value.y + mv[10] * value.z + mv[14] * value.w,
                      mv[3] * value.x + mv[7] * value.y + mv[11] * value.z + mv[15] * value.w);
}

void qa_float32_vector_view_init(qa_float32_vector_view *view, uint8_t *data,
                                 size_t data_length, int64_t byte_offset) {
  if (byte_offset < 0 || byte_offset > QA_MAX_SAFE_INTEGER ||
      (uint64_t)byte_offset + 12 > (uint64_t)data_length) {
    qa_range_error("A vector requires twelve bytes within its guest memory view");
  }
  view->bytes = data + byte_offset;
}
double qa_float32_vector_view_get(const qa_float32_vector_view *view, int component) {
  return (double)qa_load_f32_le(view->bytes, 12, (size_t)component * 4);
}
void qa_float32_vector_view_set(qa_float32_vector_view *view, int component, double value) {
  qa_store_f32_le(view->bytes, 12, (size_t)component * 4, value);
}
qa_vec3 qa_float32_vector_view_value(const qa_float32_vector_view *view) {
  qa_vec3 v = {qa_float32_vector_view_get(view, 0), qa_float32_vector_view_get(view, 1),
               qa_float32_vector_view_get(view, 2)};
  return v;
}
void qa_mutable_vec3_storage_init(qa_mutable_vec3_storage *storage,
                                  double x, double y, double z) {
  qa_float32_vector_view_init(&storage->view, storage->bytes, 12, 0);
  qa_float32_vector_view_set(&storage->view, 0, x);
  qa_float32_vector_view_set(&storage->view, 1, y);
  qa_float32_vector_view_set(&storage->view, 2, z);
}

static double plain_get(void *ctx, int component) {
  const qa_vec3 *value = (const qa_vec3 *)ctx;
  return component == 0 ? value->x : component == 1 ? value->y : value->z;
}
static void plain_set(void *ctx, int component, double number) {
  qa_vec3 *value = (qa_vec3 *)ctx;
  if (component == 0) value->x = number;
  else if (component == 1) value->y = number;
  else value->z = number;
}
static double view_get(void *ctx, int component) {
  return qa_float32_vector_view_get((const qa_float32_vector_view *)ctx, component);
}
static void view_set(void *ctx, int component, double number) {
  qa_float32_vector_view_set((qa_float32_vector_view *)ctx, component, number);
}
qa_vec3_access qa_vec3_access_plain(qa_vec3 *value) {
  qa_vec3_access access = {value, plain_get, plain_set};
  return access;
}
qa_vec3_access qa_vec3_access_view(qa_float32_vector_view *view) {
  qa_vec3_access access = {view, view_get, view_set};
  return access;
}

void qa_create_mutable_vector_math(qa_mutable_vector_math *math,
                                   const qa_numeric_operations *operations,
                                   qa_zero_length_output zero_length_output) {
  math->operations = operations;
  math->donor_arithmetic = operations->profile.arithmetic.kind == QA_ARITHMETIC_DONOR_BINARY64;
  math->zero_length_output = zero_length_output;
}

static qa_mutable_vector_math q1_donor_math_instance;
static qa_mutable_vector_math q2_donor_math_instance;
static int donor_math_ready;

static void init_donor_math(void) {
  if (donor_math_ready) return;
  qa_create_mutable_vector_math(&q1_donor_math_instance,
                                qa_create_numeric_operations(&qa_q1_donor_profile),
                                QA_ZERO_LENGTH_PRESERVE);
  qa_create_mutable_vector_math(&q2_donor_math_instance,
                                qa_create_numeric_operations(&qa_q2_donor_profile),
                                QA_ZERO_LENGTH_PRESERVE);
  donor_math_ready = 1;
}
const qa_mutable_vector_math *qa_q1_donor_math(void) {
  init_donor_math();
  return &q1_donor_math_instance;
}
const qa_mutable_vector_math *qa_q2_donor_math(void) {
  init_donor_math();
  return &q2_donor_math_instance;
}

/* Component access in TS source order; overlapping writes stay visible. */
#define GET(access, i) ((access).get((access).ctx, (i)))
#define SET(access, i, v) ((access).set((access).ctx, (i), (v)))
#define OPS(m) ((m)->operations)
#define CALL1(m, name, a) (OPS(m)->name(OPS(m)->ctx, (a)))
#define CALL2(m, name, a, b) (OPS(m)->name(OPS(m)->ctx, (a), (b)))

double qa_mvm_dot_product(const qa_mutable_vector_math *m, qa_vec3_access a, qa_vec3_access b) {
  return CALL2(m, add, CALL2(m, add, CALL2(m, multiply, GET(a, 0), GET(b, 0)),
                             CALL2(m, multiply, GET(a, 1), GET(b, 1))),
               CALL2(m, multiply, GET(a, 2), GET(b, 2)));
}
void qa_mvm_vector_set(const qa_mutable_vector_math *m, qa_vec3_access out,
                       double x, double y, double z) {
  SET(out, 0, CALL1(m, store, x));
  SET(out, 1, CALL1(m, store, y));
  SET(out, 2, CALL1(m, store, z));
}
void qa_mvm_vector_copy(const qa_mutable_vector_math *m, qa_vec3_access in, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, GET(in, 0)));
  SET(out, 1, CALL1(m, store, GET(in, 1)));
  SET(out, 2, CALL1(m, store, GET(in, 2)));
}
void qa_mvm_vector_add(const qa_mutable_vector_math *m, qa_vec3_access a,
                       qa_vec3_access b, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, CALL2(m, add, GET(a, 0), GET(b, 0))));
  SET(out, 1, CALL1(m, store, CALL2(m, add, GET(a, 1), GET(b, 1))));
  SET(out, 2, CALL1(m, store, CALL2(m, add, GET(a, 2), GET(b, 2))));
}
void qa_mvm_vector_subtract(const qa_mutable_vector_math *m, qa_vec3_access a,
                            qa_vec3_access b, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, CALL2(m, subtract, GET(a, 0), GET(b, 0))));
  SET(out, 1, CALL1(m, store, CALL2(m, subtract, GET(a, 1), GET(b, 1))));
  SET(out, 2, CALL1(m, store, CALL2(m, subtract, GET(a, 2), GET(b, 2))));
}
void qa_mvm_vector_scale(const qa_mutable_vector_math *m, qa_vec3_access in,
                         double scale, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, CALL2(m, multiply, GET(in, 0), scale)));
  SET(out, 1, CALL1(m, store, CALL2(m, multiply, GET(in, 1), scale)));
  SET(out, 2, CALL1(m, store, CALL2(m, multiply, GET(in, 2), scale)));
}
void qa_mvm_vector_ma(const qa_mutable_vector_math *m, qa_vec3_access a, double scale,
                      qa_vec3_access b, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, CALL2(m, add, GET(a, 0), CALL2(m, multiply, scale, GET(b, 0)))));
  SET(out, 1, CALL1(m, store, CALL2(m, add, GET(a, 1), CALL2(m, multiply, scale, GET(b, 1)))));
  SET(out, 2, CALL1(m, store, CALL2(m, add, GET(a, 2), CALL2(m, multiply, scale, GET(b, 2)))));
}
void qa_mvm_cross_product(const qa_mutable_vector_math *m, qa_vec3_access a,
                          qa_vec3_access b, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, CALL2(m, subtract, CALL2(m, multiply, GET(a, 1), GET(b, 2)),
                                    CALL2(m, multiply, GET(a, 2), GET(b, 1)))));
  SET(out, 1, CALL1(m, store, CALL2(m, subtract, CALL2(m, multiply, GET(a, 2), GET(b, 0)),
                                    CALL2(m, multiply, GET(a, 0), GET(b, 2)))));
  SET(out, 2, CALL1(m, store, CALL2(m, subtract, CALL2(m, multiply, GET(a, 0), GET(b, 1)),
                                    CALL2(m, multiply, GET(a, 1), GET(b, 0)))));
}
double qa_mvm_vector_length(const qa_mutable_vector_math *m, qa_vec3_access in) {
  return CALL1(m, square_root, qa_mvm_dot_product(m, in, in));
}
double qa_mvm_vector_normalize(const qa_mutable_vector_math *m, qa_vec3_access in) {
  double length = qa_mvm_vector_length(m, in);
  /* Donor sources test Boolean(length) (NaN counts as nonzero path in
   * neither: Boolean(NaN) is false, NaN !== 0 is true). */
  if (m->donor_arithmetic ? length != 0 && !isnan(length) : length != 0) {
    qa_mvm_vector_scale(m, in, CALL2(m, divide, 1.0, length), in);
  }
  return length;
}
double qa_mvm_vector_normalize2(const qa_mutable_vector_math *m, qa_vec3_access in,
                                qa_vec3_access out) {
  double length = qa_mvm_vector_length(m, in);
  if (m->donor_arithmetic ? length != 0 && !isnan(length) : length != 0) {
    qa_mvm_vector_scale(m, in, CALL2(m, divide, 1.0, length), out);
  } else if (m->zero_length_output == QA_ZERO_LENGTH_CLEAR && length == 0) {
    qa_mvm_vector_set(m, out, 0, 0, 0);
  }
  return length;
}
void qa_mvm_vector_clear(const qa_mutable_vector_math *m, qa_vec3_access out) {
  (void)m;
  /* The source chained assignment visits z, y, x. */
  SET(out, 2, 0);
  SET(out, 1, 0);
  SET(out, 0, 0);
}
void qa_mvm_vector_negate(const qa_mutable_vector_math *m, qa_vec3_access in, qa_vec3_access out) {
  SET(out, 0, CALL1(m, store, -GET(in, 0)));
  SET(out, 1, CALL1(m, store, -GET(in, 1)));
  SET(out, 2, CALL1(m, store, -GET(in, 2)));
}
void qa_mvm_vector_inverse(const qa_mutable_vector_math *m, qa_vec3_access in) {
  qa_mvm_vector_negate(m, in, in);
}
int qa_mvm_vector_compare(const qa_mutable_vector_math *m, qa_vec3_access a, qa_vec3_access b) {
  (void)m;
  return GET(a, 0) == GET(b, 0) && GET(a, 1) == GET(b, 1) && GET(a, 2) == GET(b, 2) ? 1 : 0;
}
void qa_mvm_project_point_on_plane(const qa_mutable_vector_math *m, qa_vec3_access out,
                                   qa_vec3_access point, qa_vec3_access normal) {
  double inverse = CALL2(m, divide, 1.0, qa_mvm_dot_product(m, normal, normal));
  double distance = CALL2(m, multiply, qa_mvm_dot_product(m, normal, point), inverse);
  /* The TS temporary is mutableVec3() storage; every value written to
   * it passes operations.store (binary32), so a plain temp agrees. */
  qa_vec3 scaled = {0, 0, 0};
  qa_mvm_vector_scale(m, normal, inverse, qa_vec3_access_plain(&scaled));
  SET(out, 0, CALL1(m, store, CALL2(m, subtract, GET(point, 0), CALL2(m, multiply, distance, scaled.x))));
  SET(out, 1, CALL1(m, store, CALL2(m, subtract, GET(point, 1), CALL2(m, multiply, distance, scaled.y))));
  SET(out, 2, CALL1(m, store, CALL2(m, subtract, GET(point, 2), CALL2(m, multiply, distance, scaled.z))));
}
void qa_mvm_perpendicular_vector(const qa_mutable_vector_math *m, qa_vec3_access out,
                                 qa_vec3_access source) {
  int axis = 0;
  double minimum = 1;
  for (int component = 0; component < 3; component++) {
    if (fabs(GET(source, component)) < minimum) {
      minimum = fabs(GET(source, component));
      axis = component;
    }
  }
  qa_vec3 temporary = {0, 0, 0};
  qa_vec3_access temp_access = qa_vec3_access_plain(&temporary);
  SET(temp_access, axis, 1);
  qa_mvm_project_point_on_plane(m, out, temp_access, source);
  qa_mvm_vector_normalize(m, out);
}
void qa_mvm_clear_bounds(const qa_mutable_vector_math *m, qa_vec3_access min, qa_vec3_access max) {
  (void)m;
  /* Chained assignments: minimum z,y,x receive 99999 in that order
   * (x = y = z = v evaluates z first), then maximum likewise. */
  SET(min, 2, 99999); SET(min, 1, 99999); SET(min, 0, 99999);
  SET(max, 2, -99999); SET(max, 1, -99999); SET(max, 0, -99999);
}
void qa_mvm_add_point_to_bounds(const qa_mutable_vector_math *m, qa_vec3_access point,
                                qa_vec3_access min, qa_vec3_access max) {
  for (int component = 0; component < 3; component++) {
    double value = GET(point, component);
    if (value < GET(min, component)) SET(min, component, CALL1(m, store, value));
    if (value > GET(max, component)) SET(max, component, CALL1(m, store, value));
  }
}

#undef GET
#undef SET
#undef OPS
#undef CALL1
#undef CALL2

void qa_donor_angle_vectors(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up) {
  static qa_vec3 donor_scratch; /* the TS module-level donorScratch */
  qa_angle_vectors_rule_into(forward != NULL ? forward : &donor_scratch,
                             right != NULL ? right : &donor_scratch,
                             up != NULL ? up : &donor_scratch, angles, QA_ANGLE_RULE_DONOR);
}
void qa_donor_concat_rotations(const qa_vec3 first[3], const qa_vec3 second[3], qa_vec3 output[3]) {
  for (int i = 0; i < 3; i++) {
    /* Row components are re-read per assignment, as the TS does, so an
     * output aliasing its source row observes the earlier writes. */
    output[i].x = (double)fr(first[i].x * second[0].x + first[i].y * second[1].x + first[i].z * second[2].x);
    output[i].y = (double)fr(first[i].x * second[0].y + first[i].y * second[1].y + first[i].z * second[2].y);
    output[i].z = (double)fr(first[i].x * second[0].z + first[i].y * second[1].z + first[i].z * second[2].z);
  }
}
void qa_donor_rotate_point_around_vector(qa_vec3 *out, qa_vec3 direction,
                                         qa_vec3 point, double degrees) {
  const qa_mutable_vector_math *donor = qa_q1_donor_math();
  /* TS temporaries are mutableVec3() (binary32) storage; values written
   * there are already store()-rounded, so plain doubles agree. */
  qa_vec3 forward = qa_math_vec3(direction.x, direction.y, direction.z);
  qa_vec3 radial = {0, 0, 0}, vertical = {0, 0, 0};
  qa_vec3 direction_copy = direction;
  qa_mvm_perpendicular_vector(donor, qa_vec3_access_plain(&radial),
                              qa_vec3_access_plain(&direction_copy));
  qa_mvm_cross_product(donor, qa_vec3_access_plain(&radial),
                       qa_vec3_access_plain(&forward), qa_vec3_access_plain(&vertical));
  qa_vec3 basis[3] = {qa_math_vec3(radial.x, vertical.x, forward.x),
                      qa_math_vec3(radial.y, vertical.y, forward.y),
                      qa_math_vec3(radial.z, vertical.z, forward.z)};
  double radians = degrees * QA_PI / 180.0;
  double cosine = cos(radians), sine = sin(radians);
  qa_vec3 rotation[3] = {qa_math_vec3(cosine, sine, 0), qa_math_vec3(-sine, cosine, 0),
                         qa_math_vec3(0, 0, 1)};
  qa_vec3 temporary[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  qa_vec3 result[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  qa_vec3 inverse_rows[3] = {radial, vertical, forward};
  qa_donor_concat_rotations(basis, rotation, temporary);
  qa_donor_concat_rotations(temporary, inverse_rows, result);
  qa_vec3 point_copy = point;
  out->x = (double)fr(qa_mvm_dot_product(donor, qa_vec3_access_plain(&result[0]),
                                         qa_vec3_access_plain(&point_copy)));
  out->y = (double)fr(qa_mvm_dot_product(donor, qa_vec3_access_plain(&result[1]),
                                         qa_vec3_access_plain(&point_copy)));
  out->z = (double)fr(qa_mvm_dot_product(donor, qa_vec3_access_plain(&result[2]),
                                         qa_vec3_access_plain(&point_copy)));
}

double qa_lerp_angle_with_rule(double from, double to, double fraction, qa_lerp_angle_rule rule) {
  if (rule == QA_LERP_ANGLE_DELTA) {
    double delta = to - from;
    return from + (delta > 180 ? delta - 360 : delta < -180 ? delta + 360 : delta) * fraction;
  }
  if (rule == QA_LERP_ANGLE_FLOAT) {
    double end = to;
    if ((double)fr(end - from) > 180) end = (double)fr(end - 360.0);
    if ((double)fr(end - from) < -180) end = (double)fr(end + 360.0);
    return (double)fr(from + (double)fr(fraction * (double)fr(end - from)));
  }
  double end = to;
  if (end - from > 180) end -= 360;
  if (end - from < -180) end += 360;
  return from + fraction * (end - from);
}
double qa_lerp_angle(double from, double to, double fraction) {
  return qa_lerp_angle_with_rule(from, to, fraction, QA_LERP_ANGLE_NATIVE);
}
static double lerp_angle_wrapped_axis(double from, double to, double fraction) {
  return from + (fmod(to - from + 540.0, 360.0) - 180.0) * fraction;
}
qa_vec3 qa_lerp_angles_wrapped(qa_vec3 a, qa_vec3 b, double fraction) {
  qa_vec3 v = {lerp_angle_wrapped_axis(a.x, b.x, fraction),
               lerp_angle_wrapped_axis(a.y, b.y, fraction),
               lerp_angle_wrapped_axis(a.z, b.z, fraction)};
  return v;
}

qa_floor_div_mod_result qa_floor_div_mod(double numerator, double denominator) {
  if (denominator <= 0) qa_range_error("FloorDivMod: bad denominator");
  double quotient, remainder;
  if (numerator >= 0) {
    quotient = trunc(floor(numerator / denominator));
    remainder = trunc(floor(numerator - quotient * denominator));
  } else {
    double positive = floor(-numerator / denominator);
    quotient = -trunc(positive);
    remainder = trunc(floor(-numerator - positive * denominator));
    if (remainder != 0) {
      quotient--;
      remainder = trunc(denominator) - remainder;
    }
  }
  qa_floor_div_mod_result result = {quotient, remainder};
  return result;
}
double qa_greatest_common_divisor(double first, double second) {
  if (first > second) return second == 0 ? first : qa_greatest_common_divisor(second, fmod(first, second));
  return first == 0 ? second : qa_greatest_common_divisor(first, fmod(second, first));
}
