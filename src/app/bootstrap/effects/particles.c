/* Port of src/app/bootstrap/effects/particles.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/effects/particles.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/formats/q12_model/normals.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/particles/legacy.h"
#include "qa/foundation/js.h"
static const qa_vec3 zero_v = {0, 0, 0};
static const qa_vec3 gravity_v = {0, 0, -40};
static const double q1_fire_ramp[6] = {109, 107, 6, 5, 4, 3};
struct qa_app_source_particles {
  qa_app_sim_source_random *random;
  double capacity;
  qa_q1_particle_state *q1; size_t q1_count, q1_cap;
  qa_q2_particle_state *q2; size_t q2_count, q2_cap;
  double tracer_count;
  qa_vec3 *angular; size_t angular_count;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static double fr(double v) { return (double)(float)v; }
static int64_t rand_i(qa_app_source_particles *p) {
  return (int64_t)qa_app_sim_source_random_next_integer(p->random);
}
static double unit(qa_app_source_particles *p) { return (double)(rand_i(p) & 32767) / 32767.0; }
static double signed_unit(qa_app_source_particles *p) { return 2 * unit(p) - 1; }
static bool first(qa_app_source_particles *p, qa_q1_particle_state particle) {
  if ((double)p->q1_count == p->capacity) return false;
  if (p->q1_count == p->q1_cap) {
    size_t cap = p->q1_cap == 0 ? 64 : p->q1_cap * 2;
    qa_q1_particle_state *next = realloc(p->q1, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    p->q1 = next; p->q1_cap = cap;
  }
  p->q1[p->q1_count++] = particle;
  return true;
}
static bool second(qa_app_source_particles *p, qa_q2_particle_state particle) {
  if ((double)p->q2_count == p->capacity) return false;
  if (p->q2_count == p->q2_cap) {
    size_t cap = p->q2_cap == 0 ? 64 : p->q2_cap * 2;
    qa_q2_particle_state *next = realloc(p->q2, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    p->q2 = next; p->q2_cap = cap;
  }
  p->q2[p->q2_count++] = particle;
  return true;
}
qa_app_source_particles *qa_app_source_particles_create(qa_app_sim_source_random *random, double capacity) {
  qa_app_source_particles *p = calloc(1, sizeof *p);
  if (p == NULL) qa_range_error("Out of memory");
  p->random = random;
  p->capacity = capacity < 0 ? 4096 : capacity;
  return p;
}
void qa_app_source_particles_destroy(qa_app_source_particles *particles) {
  if (particles == NULL) return;
  free(particles->q1); free(particles->q2); free(particles->angular);
  free(particles);
}
qa_app_sim_source_random *qa_app_source_particles_random(const qa_app_source_particles *particles) { return particles->random; }
double qa_app_source_particles_capacity(const qa_app_source_particles *particles) { return particles->capacity; }
void qa_app_source_particles_clear(qa_app_source_particles *particles) { particles->q1_count = 0; particles->q2_count = 0; }
void qa_app_source_particles_sample(qa_app_source_particles *particles, double seconds, double elapsed,
    qa_scene_particle **out_q1, size_t *out_q1_count, qa_scene_particle **out_q2, size_t *out_q2_count) {
  size_t kept = 0;
  for (size_t i = 0; i < particles->q1_count; i++)
    if (particles->q1[i].die >= seconds) particles->q1[kept++] = particles->q1[i];
  particles->q1_count = kept;
  qa_scene_particle *q1 = malloc((kept == 0 ? 1 : kept) * sizeof *q1);
  if (q1 == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < kept; i++) {
    qa_q1_particle_state particle = particles->q1[kept - 1 - i];
    q1[i] = (qa_scene_particle){0};
    q1[i].is_rgba = false; /* TS kind "indexed" */
    q1[i].origin = particle.origin;
    q1[i].palette_index = particle.color;
    q1[i].alpha = 1;
    q1[i].size = 1;
  }
  for (size_t i = 0; i < kept; i++)
    particles->q1[i] = qa_advance_q1_particle(particles->q1[i], elapsed, 800);
  qa_scene_particle *q2 = malloc((particles->q2_count == 0 ? 1 : particles->q2_count) * sizeof *q2);
  if (q2 == NULL) qa_range_error("Out of memory");
  size_t q2_count = 0, retained = 0;
  for (size_t i = 0; i < particles->q2_count; i++) {
    qa_q2_particle_state particle = particles->q2[i];
    qa_scene_particle sample;
    if (qa_sample_q2_particle(particle, seconds * 1000, &sample)) {
      q2[q2_count++] = sample; /* reversed below (TS unshift) */
      if (particle.alpha_velocity != -10000) particles->q2[retained++] = particle;
    }
  }
  particles->q2_count = retained;
  for (size_t i = 0; i < q2_count / 2; i++) {
    qa_scene_particle tmp = q2[i]; q2[i] = q2[q2_count - 1 - i]; q2[q2_count - 1 - i] = tmp;
  }
  *out_q1 = q1; *out_q1_count = kept;
  *out_q2 = q2; *out_q2_count = q2_count;
}
void qa_app_source_particles_q1_impact(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction,
                                       double color, double count, double seconds) {
  if (count == 1024) { qa_app_source_particles_q1_explosion(p, origin, seconds, false); return; }
  for (double i = 0; i < count; i++) {
    double die = seconds + 0.1 * (double)(rand_i(p) % 5);
    double shade = (double)((qa_js_to_int32(color) & ~7LL) + (rand_i(p) & 7));
    double x2 = origin.x + (double)((rand_i(p) & 15) - 8);
    double y2 = origin.y + (double)((rand_i(p) & 15) - 8);
    double z2 = origin.z + (double)((rand_i(p) & 15) - 8);
    qa_vec3 at = {x2, y2, z2};
    if (!first(p, (qa_q1_particle_state){at, qa_scale3(direction, 15), shade, 0, die, "slow-gravity"})) return;
  }
}
void qa_app_source_particles_q1_entity(qa_app_source_particles *p, qa_vec3 origin, double seconds) {
  size_t normal_count = 0;
  const qa_vec3 *normals = qa_alias_normal_table(&normal_count);
  if (p->angular_count == 0 || p->angular[0].x == 0) {
    double x2;
    double y2;
    double z2;
    free(p->angular);
    p->angular = malloc(normal_count * sizeof *p->angular);
    if (p->angular == NULL) qa_range_error("Out of memory");
    p->angular_count = normal_count;
    for (size_t i = 0; i < normal_count; i++)
      p->angular[i] = (x2 = fr((double)(rand_i(p) & 255) * 0.01), y2 = fr((double)(rand_i(p) & 255) * 0.01), z2 = fr((double)(rand_i(p) & 255) * 0.01), (qa_vec3){x2,
                                y2,
                                z2});
  }
  for (size_t index = 0; index < normal_count; index++) {
    if ((double)p->q1_count == p->capacity) return;
    if (index >= p->angular_count) qa_range_error("Missing Q1 particle angular velocity");
    qa_vec3 velocity = p->angular[index];
    double yaw = fr(seconds * velocity.x), pitch = fr(seconds * velocity.y);
    double cp = fr(cos(pitch)), sp = fr(sin(pitch)), cy = fr(cos(yaw)), sy = fr(sin(yaw));
    qa_vec3 forward = {fr(cp * cy), fr(cp * sy), -sp};
    qa_vec3 normal = normals[index];
    qa_vec3 at = {fr(fr(origin.x + fr(normal.x * 64)) + fr(forward.x * 16)),
                  fr(fr(origin.y + fr(normal.y * 64)) + fr(forward.y * 16)),
                  fr(fr(origin.z + fr(normal.z * 64)) + fr(forward.z * 16))};
    first(p, (qa_q1_particle_state){at, zero_v, 111, 0, fr(seconds + 0.01), "explode"});
  }
}
void qa_app_source_particles_q1_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                      double type, double seconds) {
  qa_vec3 delta = {fr(end.x - start.x), fr(end.y - start.y), fr(end.z - start.z)};
  double remaining = fr(sqrt(fr(fr(fr(delta.x * delta.x) + fr(delta.y * delta.y)) + fr(delta.z * delta.z))));
  double inverse = remaining == 0 ? 0 : fr(1 / remaining);
  qa_vec3 direction = {fr(delta.x * inverse), fr(delta.y * inverse), fr(delta.z * inverse)};
  qa_vec3 point = start;
  while (remaining > 0) {
    remaining = fr(remaining - 3);
    if ((double)p->q1_count == p->capacity) return;
    qa_vec3 origin = point, velocity = zero_v;
    double color = 0, ramp = 0, die = fr(seconds + 2);
    const char *particle_type = "static";
    if (type == 0 || type == 1) {
      ramp = (double)(rand_i(p) & 3) + (type == 1 ? 2 : 0);
      color = ramp >= 0 && ramp < 6 ? q1_fire_ramp[(size_t)ramp] : 0;
      particle_type = "fire";
      double x4 = fr(point.x + (double)(rand_i(p) % 6 - 3));
      double y4 = fr(point.y + (double)(rand_i(p) % 6 - 3));
      double z4 = fr(point.z + (double)(rand_i(p) % 6 - 3));
      origin = (qa_vec3){x4, y4, z4};
    } else if (type == 2 || type == 4) {
      color = 67 + (double)(rand_i(p) & 3);
      particle_type = "gravity";
      double x3 = fr(point.x + (double)(rand_i(p) % 6 - 3));
      double y3 = fr(point.y + (double)(rand_i(p) % 6 - 3));
      double z3 = fr(point.z + (double)(rand_i(p) % 6 - 3));
      origin = (qa_vec3){x3, y3, z3};
      if (type == 4) remaining = fr(remaining - 3);
    } else if (type == 3 || type == 5) {
      die = fr(seconds + 0.5);
      color = (type == 3 ? 52 : 230) + (double)((qa_js_to_int32(p->tracer_count) & 4) << 1);
      p->tracer_count++;
      velocity = ((qa_js_to_int32(p->tracer_count) & 1) != 0)
        ? (qa_vec3){fr(30 * direction.y), fr(30 * -direction.x), 0}
        : (qa_vec3){fr(30 * -direction.y), fr(30 * direction.x), 0};
    } else {
      color = 152 + (double)(rand_i(p) & 3);
      die = fr(seconds + 0.3);
      double x2 = fr(point.x + (double)((rand_i(p) & 15) - 8));
      double y2 = fr(point.y + (double)((rand_i(p) & 15) - 8));
      double z2 = fr(point.z + (double)((rand_i(p) & 15) - 8));
      origin = (qa_vec3){x2, y2, z2};
    }
    first(p, (qa_q1_particle_state){origin, velocity, color, ramp, die, particle_type});
    point = (qa_vec3){fr(point.x + direction.x), fr(point.y + direction.y), fr(point.z + direction.z)};
  }
}
void qa_app_source_particles_q1_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds, bool blob) {
  for (int i = 0; i < 1024; i++) {
    double die = seconds + (blob ? 1 + (double)(rand_i(p) & 8) * 0.05 : 5);
    double ramp = blob ? 0 : (double)(rand_i(p) & 3);
    double color = blob ? (i & 1 ? 66 : 150) + (double)(rand_i(p) % 6) : 111;
    double ox = origin.x + (double)(rand_i(p) % 32 - 16), vx = (double)(rand_i(p) % 512 - 256);
    double oy = origin.y + (double)(rand_i(p) % 32 - 16), vy = (double)(rand_i(p) % 512 - 256);
    double oz = origin.z + (double)(rand_i(p) % 32 - 16), vz = (double)(rand_i(p) % 512 - 256);
    const char *type = blob ? (i & 1 ? "blob" : "blob2") : (i & 1 ? "explode" : "explode2");
    if (!first(p, (qa_q1_particle_state){{ox, oy, oz}, {vx, vy, vz}, color, ramp, die, type})) return;
  }
}
void qa_app_source_particles_q1_color_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds,
                                                double color_start, double color_length) {
  if (color_start != trunc(color_start) || color_start < 0 || color_start > 255
      || color_length != trunc(color_length) || color_length > 255)
    qa_raise("Error", 0, 0, "Invalid Quake colored explosion palette range");
  /* A zero-length range divides by zero in the source; the explosion is dropped. */
  if (color_length < 1) return;
  for (int i = 0; i < 512 && (double)p->q1_count < p->capacity; i++) {
    double ox = origin.x + (double)(rand_i(p) % 32 - 16), vx = (double)(rand_i(p) % 512 - 256);
    double oy = origin.y + (double)(rand_i(p) % 32 - 16), vy = (double)(rand_i(p) % 512 - 256);
    double oz = origin.z + (double)(rand_i(p) % 32 - 16), vz = (double)(rand_i(p) % 512 - 256);
    double color = color_start + (double)(i % (int)color_length);
    first(p, (qa_q1_particle_state){{ox, oy, oz}, {vx, vy, vz}, color, 0, seconds + 0.3, "blob"});
  }
}
void qa_app_source_particles_q1_splash(qa_app_source_particles *p, qa_vec3 origin, double seconds, bool lava) {
  int step = lava ? 1 : 4;
  for (int i = -16; i < 16; i += step)
    for (int j = -16; j < 16; j += step)
      for (int k = lava ? 0 : -24; k < (lava ? 1 : 32); k += 4) {
        double die = seconds + (lava ? 2 + (double)(rand_i(p) & 31) * 0.02 : 0.2 + (double)(rand_i(p) & 7) * 0.02);
        double color = (lava ? 224 : 7) + (double)(rand_i(p) & 7);
        qa_vec3 direction, point;
        if (lava) {
          double x3 = j * 8 + (double)(rand_i(p) & 7);
          double y3 = i * 8 + (double)(rand_i(p) & 7);
          direction = (qa_vec3){x3, y3, 256};
          point = (qa_vec3){origin.x + direction.x, origin.y + direction.y, origin.z + (double)(rand_i(p) & 63)};
        } else {
          direction = (qa_vec3){j * 8, i * 8, k * 8};
          double x2 = i + (double)(rand_i(p) & 3);
          double y2 = j + (double)(rand_i(p) & 3);
          double z2 = k + (double)(rand_i(p) & 3);
          point = qa_add3(origin, (qa_vec3){x2, y2, z2});
        }
        qa_vec3 velocity = qa_scale3(qa_normalize3_or_zero(direction), 50 + (double)(rand_i(p) & 63));
        if (!first(p, (qa_q1_particle_state){point, velocity, color, 0, die, "slow-gravity"})) return;
      }
}
void qa_app_source_particles_q2_impact(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction,
                                       double color, double count, double seconds, const char *variant) {
  const char *v = variant != NULL ? variant : "normal";
  for (double i = 0; i < count; i++) {
    double shade = color + (is(v, "fixed") || is(v, "up") ? 0 : (double)(rand_i(p) & 7));
    int64_t mask = is(v, "normal") ? 31 : is(v, "blaster") ? 15 : 7;
    double distance = (double)(rand_i(p) & mask);
    double ox = origin.x + (double)(rand_i(p) & 7) - 4 + distance * direction.x;
    double vx = is(v, "blaster") ? direction.x * 30 + signed_unit(p) * 40 : signed_unit(p) * 20;
    double oy = origin.y + (double)(rand_i(p) & 7) - 4 + distance * direction.y;
    double vy = is(v, "blaster") ? direction.y * 30 + signed_unit(p) * 40 : signed_unit(p) * 20;
    double oz = origin.z + (double)(rand_i(p) & 7) - 4 + distance * direction.z;
    double vz = is(v, "blaster") ? direction.z * 30 + signed_unit(p) * 40 : signed_unit(p) * 20;
    qa_q2_particle_state particle = {seconds * 1000, {ox, oy, oz}, {vx, vy, vz},
      is(v, "up") ? qa_scale3(gravity_v, -1) : gravity_v, shade, 1, -1 / (0.5 + unit(p) * 0.3)};
    if (!second(p, particle)) return;
  }
}
void qa_app_source_particles_q2_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds, bool bfg) {
  for (int i = 0; i < 256; i++) {
    double color = (bfg ? 0xd0 : 0xe0) + (double)(rand_i(p) & 7);
    double ox = origin.x + (double)(rand_i(p) % 32 - 16), vx = (double)(rand_i(p) % 384 - 192);
    double oy = origin.y + (double)(rand_i(p) % 32 - 16), vy = (double)(rand_i(p) % 384 - 192);
    double oz = origin.z + (double)(rand_i(p) % 32 - 16), vz = (double)(rand_i(p) % 384 - 192);
    if (!second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz}, {vx, vy, vz}, gravity_v, color, 1, -0.8 / (0.5 + unit(p) * 0.3)})) return;
  }
}
void qa_app_source_particles_q2_color_explosion(qa_app_source_particles *p, qa_vec3 origin, double seconds,
                                                double color, double run) {
  for (int i = 0; i < 128 && (double)p->q2_count < p->capacity; i++) {
    double shade = color + (double)(rand_i(p) % (int64_t)run);
    double ox = origin.x + (double)(rand_i(p) % 32 - 16), vx = (double)(rand_i(p) % 256 - 128);
    double oy = origin.y + (double)(rand_i(p) % 32 - 16), vy = (double)(rand_i(p) % 256 - 128);
    double oz = origin.z + (double)(rand_i(p) % 32 - 16), vz = (double)(rand_i(p) % 256 - 128);
    second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz}, {vx, vy, vz}, gravity_v, shade, 1, -0.4 / (0.6 + unit(p) * 0.2)});
  }
}
static void right_up(qa_vec3 direction, qa_vec3 *right, qa_vec3 *up) {
  qa_vec3 initial = {direction.z, -direction.x, direction.y};
  *right = qa_normalize3_or_zero(qa_sub3(initial, qa_scale3(direction, qa_dot3(initial, direction))));
  *up = qa_cross3(*right, direction);
}
void qa_app_source_particles_q2_berserk_slam(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction, double seconds) {
  qa_vec3 right, up;
  right_up(direction, &right, &up);
  for (int i = 0; i < 700 && (double)p->q2_count < p->capacity; i++) {
    double color = 110 + 2 * (double)(rand_i(p) & 3);
    qa_vec3 a = qa_scale3(direction, unit(p) * 192);
    qa_vec3 b = qa_scale3(right, signed_unit(p) * 192);
    qa_vec3 velocity = qa_add3(qa_add3(a, b),
                               qa_scale3(up, signed_unit(p) * 192));
    second(p, (qa_q2_particle_state){seconds * 1000, origin, velocity, zero_v, color, 1, -1 / (0.5 + unit(p) * 0.3)});
  }
}
bool qa_app_source_particles_q2_steam(qa_app_source_particles *p, qa_vec3 origin, qa_vec3 direction,
                                      double color, double count, double magnitude, double seconds, bool smoke) {
  qa_vec3 right, up;
  right_up(direction, &right, &up);
  for (double i = 0; i < count; i++) {
    if ((double)p->q2_count == p->capacity) return false;
    double shade = color + (double)(rand_i(p) & 7);
    double x = magnitude * 0.1 * signed_unit(p);
    double y = magnitude * 0.1 * signed_unit(p);
    double z = magnitude * 0.1 * signed_unit(p);
    qa_vec3 point = qa_add3(origin, (qa_vec3){x,
                                              y,
                                              z});
    qa_vec3 a = qa_add3(qa_scale3(direction, magnitude), qa_scale3(right, signed_unit(p) * magnitude / 3));
    qa_vec3 b = qa_scale3(up, signed_unit(p) * magnitude / 3);
    qa_vec3 velocity = qa_add3(a, b);
    second(p, (qa_q2_particle_state){seconds * 1000, point, velocity,
      smoke ? zero_v : qa_scale3(gravity_v, 0.5), shade, 1, -1 / (0.5 + unit(p) * 0.3)});
  }
  return true;
}
void qa_app_source_particles_q2_force_wall(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                           double color, double seconds) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  for (double distance = 0; distance < length && (double)p->q2_count < p->capacity; distance += 4) {
    double x;
    double y;
    double z;
    if (unit(p) <= 0.3) continue;
    double alpha_velocity = -1 / (3 + unit(p) * 0.5);
    qa_vec3 origin = qa_add3(qa_add3(start, qa_scale3(direction, distance)),
                             (x = signed_unit(p) * 3, y = signed_unit(p) * 3, z = signed_unit(p) * 3, (qa_vec3){x, y, z}));
    second(p, (qa_q2_particle_state){seconds * 1000, origin,
      {0, 0, -40 - signed_unit(p) * 10}, zero_v, color, 1, alpha_velocity});
  }
}
void qa_app_source_particles_q2_tracker_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end, double seconds) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  double horizontal = hypot(direction.x, direction.y);
  qa_axis axis = qa_angles_to_axis((qa_vec3){-atan2(direction.z, horizontal) * 180 / M_PI,
    horizontal == 0 ? 0 : atan2(direction.y, direction.x) * 180 / M_PI, 0});
  for (double distance = 0; distance < length && (double)p->q2_count < p->capacity; distance += 3) {
    qa_vec3 point = qa_add3(start, qa_scale3(direction, distance));
    second(p, (qa_q2_particle_state){seconds * 1000,
      qa_add3(point, qa_scale3(axis.vectors[2], 8 * cos(qa_dot3(point, axis.vectors[0])))),
      {0, 0, 5}, zero_v, 0, 1, -2});
  }
}
void qa_app_source_particles_q2_tracker_shell(qa_app_source_particles *p, qa_vec3 origin, double seconds) {
  for (int i = 0; i < 300 && (double)p->q2_count < p->capacity; i++) {
    double x = signed_unit(p);
    double y = signed_unit(p);
    double z = signed_unit(p);
    qa_vec3 random_v = {x, y, z};
    second(p, (qa_q2_particle_state){seconds * 1000,
      qa_add3(origin, qa_scale3(qa_normalize3_or_zero(random_v), 40)), zero_v, zero_v, 0, 1, -10000});
  }
}
void qa_app_source_particles_q2_respawn(qa_app_source_particles *p, qa_vec3 origin, double seconds, const char *kind) {
  bool logout = !is(kind, "item");
  double base_color = is(kind, "login") ? 0xd0 : is(kind, "logout") ? 0x40 : is(kind, "respawn") ? 0xe0 : 0xd4;
  for (int i = 0; i < (logout ? 500 : 64); i++) {
    double x3;
    double y3;
    double z3;
    double x2;
    double y2;
    double z2;
    double color = base_color + (double)(rand_i(p) & (logout ? 7 : 3));
    qa_vec3 point = logout
      ? qa_add3(origin, (x3 = -16 + unit(p) * 32, y3 = -16 + unit(p) * 32, z3 = -24 + unit(p) * 56, (qa_vec3){x3, y3, z3}))
      : qa_add3(origin, (x2 = signed_unit(p) * 8, y2 = signed_unit(p) * 8, z2 = signed_unit(p) * 8, (qa_vec3){x2, y2, z2}));
    double x = signed_unit(p) * (logout ? 20 : 8);
    double y = signed_unit(p) * (logout ? 20 : 8);
    double z = signed_unit(p) * (logout ? 20 : 8);
    qa_vec3 velocity = {x, y, z};
    if (!second(p, (qa_q2_particle_state){seconds * 1000, point, velocity,
        qa_scale3(gravity_v, logout ? 1 : 0.2), color, 1, -1 / (1 + unit(p) * 0.3)})) return;
  }
}
void qa_app_source_particles_q2_teleport(qa_app_source_particles *p, qa_vec3 origin, double seconds) {
  for (int i = -16; i <= 16; i += 4)
    for (int j = -16; j <= 16; j += 4)
      for (int k = -16; k <= 32; k += 4) {
        double color = 7 + (double)(rand_i(p) & 7);
        double alpha_velocity = -1 / (0.3 + (double)(rand_i(p) & 7) * 0.02);
        double x = i + (double)(rand_i(p) & 3);
        double y = j + (double)(rand_i(p) & 3);
        double z = k + (double)(rand_i(p) & 3);
        qa_vec3 point = qa_add3(origin, (qa_vec3){x, y, z});
        qa_vec3 velocity = qa_scale3(qa_normalize3_or_zero((qa_vec3){j * 8, i * 8, k * 8}), 50 + (double)(rand_i(p) & 63));
        if (!second(p, (qa_q2_particle_state){seconds * 1000, point, velocity, gravity_v, color, 1, alpha_velocity})) return;
      }
}
void qa_app_source_particles_q2_big_teleport(qa_app_source_particles *p, qa_vec3 origin, double seconds) {
  static const double colors[4] = {16, 104, 168, 144};
  for (int i = 0; i < 4096 && (double)p->q2_count < p->capacity; i++) {
    double color = colors[rand_i(p) & 3];
    double angle = M_PI * 2 * ((double)(rand_i(p) & 1023) / 1023);
    double distance = (double)(rand_i(p) & 31);
    double x = cos(angle), y = sin(angle);
    double velocity_x = x * (70 + (double)(rand_i(p) & 63));
    double velocity_y = y * (70 + (double)(rand_i(p) & 63));
    qa_vec3 at = {origin.x + x * distance, origin.y + y * distance, origin.z + 8 + (double)(rand_i(p) % 90)};
    qa_vec3 velocity = {velocity_x, velocity_y, -100 + (double)(rand_i(p) & 31)};
    qa_vec3 acceleration = {-x * 100, -y * 100, 160};
    second(p, (qa_q2_particle_state){seconds * 1000, at, velocity, acceleration, color, 1,
      -0.3 / (0.5 + unit(p) * 0.3)});
  }
}
void qa_app_source_particles_q2_teleporter(qa_app_source_particles *p, qa_vec3 origin, double seconds) {
  for (int i = 0; i < 8 && (double)p->q2_count < p->capacity; i++) {
    double ox = origin.x - 16 + (double)(rand_i(p) & 31), vx = signed_unit(p) * 14;
    double oy = origin.y - 16 + (double)(rand_i(p) & 31), vy = signed_unit(p) * 14;
    double spawn_milliseconds = seconds * 1000;
    qa_vec3 origin2 = {ox, oy, origin.z - 8 + (double)(rand_i(p) & 7)};
    qa_vec3 velocity = {vx, vy, 80 + (double)(rand_i(p) & 7)};
    second(p, (qa_q2_particle_state){spawn_milliseconds,
      origin2,
      velocity, gravity_v, 0xdb, 1, -0.5});
  }
}
void qa_app_source_particles_q2_blaster_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                              double seconds, bool green) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  for (double distance = 0; distance < length && (double)p->q2_count < p->capacity; distance += 5) {
    double alpha_velocity = -1 / (0.3 + unit(p) * 0.2);
    qa_vec3 origin = qa_add3(start, qa_scale3(direction, distance));
    double ox = origin.x + signed_unit(p), vx = signed_unit(p) * 5;
    double oy = origin.y + signed_unit(p), vy = signed_unit(p) * 5;
    double oz = origin.z + signed_unit(p), vz = signed_unit(p) * 5;
    second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz}, {vx, vy, vz}, zero_v,
      green ? 0xd0 : 0xe0, 1, alpha_velocity});
  }
}
double qa_app_source_particles_q2_diminishing_trail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end,
                                                    double seconds, double count, const char *kind) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  double origin_scale = count > 900 ? 4 : count > 800 ? 2 : 1;
  double velocity_scale = count > 900 ? 15 : count > 800 ? 10 : 5;
  bool blood = is(kind, "blood") || is(kind, "green-blood");
  for (double distance = 0; distance < length && (double)p->q2_count < p->capacity; distance += 0.5) {
    if ((double)(rand_i(p) & 1023) < count) {
      double alpha_velocity = -1 / (1 + unit(p) * (blood ? 0.4 : 0.2));
      double color = (blood ? (is(kind, "blood") ? 0xe8 : 0xdb) : 4) + (double)(rand_i(p) & 7);
      qa_vec3 point = qa_add3(start, qa_scale3(direction, distance));
      double ox = point.x + signed_unit(p) * origin_scale, vx = signed_unit(p) * velocity_scale;
      double oy = point.y + signed_unit(p) * origin_scale, vy = signed_unit(p) * velocity_scale;
      double oz = point.z + signed_unit(p) * origin_scale, vz = signed_unit(p) * velocity_scale;
      second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz},
        {vx, vy, vz - (blood ? 40 : 0)}, blood ? zero_v : (qa_vec3){0, 0, 20}, color, 1, alpha_velocity});
    }
    count = qa_js_max(100, count - 5);
  }
  if (is(kind, "rocket"))
    for (double distance = 0; distance < length && (double)p->q2_count < p->capacity; distance++) {
      if ((rand_i(p) & 7) != 0) continue;
      double alpha_velocity = -1 / (1 + unit(p) * 0.2);
      double color = 0xdc + (double)(rand_i(p) & 3);
      qa_vec3 point = qa_add3(start, qa_scale3(direction, distance));
      double ox = point.x + signed_unit(p) * 5, vx = signed_unit(p) * 20;
      double oy = point.y + signed_unit(p) * 5, vy = signed_unit(p) * 20;
      double oz = point.z + signed_unit(p) * 5, vz = signed_unit(p) * 20;
      second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz}, {vx, vy, vz}, gravity_v, color, 1, alpha_velocity});
    }
  return count;
}
void qa_app_source_particles_q2_rail(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end, double seconds) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  qa_vec3 right, up;
  right_up(direction, &right, &up);
  for (double i = 0; i < length; i++) {
    qa_vec3 radial = qa_add3(qa_scale3(right, cos(i * 0.1)), qa_scale3(up, sin(i * 0.1)));
    double alpha_velocity = -1 / (1 + unit(p) * 0.2);
    double color = 0x74 + (double)(rand_i(p) & 7);
    if (!second(p, (qa_q2_particle_state){seconds * 1000,
        qa_add3(qa_add3(start, qa_scale3(direction, i)), qa_scale3(radial, 3)),
        qa_scale3(radial, 6), zero_v, color, 1, alpha_velocity})) return;
  }
  for (double i = 0; i < length; i += 0.75) {
    double alpha_velocity = -1 / (0.6 + unit(p) * 0.2);
    double color = (double)(rand_i(p) & 15);
    qa_vec3 point = qa_add3(start, qa_scale3(direction, i));
    double ox = point.x + signed_unit(p) * 3, vx = signed_unit(p) * 3;
    double oy = point.y + signed_unit(p) * 3, vy = signed_unit(p) * 3;
    double oz = point.z + signed_unit(p) * 3, vz = signed_unit(p) * 3;
    if (!second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz}, {vx, vy, vz}, zero_v, color, 1, alpha_velocity})) return;
  }
}
void qa_app_source_particles_q2_bubbles(qa_app_source_particles *p, qa_vec3 start, qa_vec3 end, double seconds) {
  qa_vec3 delta = qa_sub3(end, start);
  double length = qa_length3(delta);
  qa_vec3 direction = qa_normalize3_or_zero(delta);
  for (double i = 0; i < length; i += 32) {
    double alpha_velocity = -1 / (1 + unit(p) * 0.2);
    double color = 4 + (double)(rand_i(p) & 7);
    qa_vec3 point = qa_add3(start, qa_scale3(direction, i));
    double ox = point.x + signed_unit(p) * 2, vx = signed_unit(p) * 5;
    double oy = point.y + signed_unit(p) * 2, vy = signed_unit(p) * 5;
    double oz = point.z + signed_unit(p) * 2, vz = signed_unit(p) * 5;
    if (!second(p, (qa_q2_particle_state){seconds * 1000, {ox, oy, oz}, {vx, vy, vz + 6}, zero_v, color, 1, alpha_velocity})) return;
  }
}
