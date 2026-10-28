/* Port of src/app/bootstrap/network/qw-camera.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/network/qw_camera.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
struct qa_app_qw_spectator_camera {
  qa_app_qw_camera_options options; qa_app_qw_camera_host host;
  bool tracking, locked; double slot, old_buttons;
  qa_vec3 desired; double last_view_seconds;
  bool has_teleport; qa_vec3 teleport;
  bool has_current; qa_app_qw_camera_view current;
};
/* Every TS vector() call is a Math.fround staging point. */
static qa_vec3 vector3(double x, double y, double z) {
  qa_vec3 v = {(float)x, (float)y, (float)z}; return v;
}
static qa_vec3 subtract(qa_vec3 a, qa_vec3 b) { return vector3(a.x - b.x, a.y - b.y, a.z - b.z); }
static qa_vec3 add(qa_vec3 a, qa_vec3 b) { return vector3(a.x + b.x, a.y + b.y, a.z + b.z); }
static double vlength(qa_vec3 v) { return sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
static qa_vec3 view_angles(qa_vec3 v) {
  if (v.x == 0 && v.y == 0) return vector3(v.z > 0 ? -90 : -270, 0, 0);
  double yaw = trunc(atan2(v.y, v.x) * 180 / M_PI);
  double pitch = trunc(atan2(v.z, sqrt(v.x * v.x + v.y * v.y)) * 180 / M_PI);
  return vector3(-(pitch < 0 ? pitch + 360 : pitch), yaw < 0 ? yaw + 360 : yaw, 0);
}
qa_app_qw_spectator_camera *qa_app_qw_spectator_camera_create(
    qa_app_qw_camera_options options, qa_app_qw_camera_host host) {
  qa_app_qw_spectator_camera *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->options = options; c->host = host;
  return c;
}
void qa_app_qw_spectator_camera_destroy(qa_app_qw_spectator_camera *camera) { free(camera); }
bool qa_app_qw_spectator_camera_view(const qa_app_qw_spectator_camera *camera, qa_app_qw_camera_view *out) {
  if (!(camera->tracking && camera->locked && camera->has_current)) return false;
  *out = camera->current; return true;
}
bool qa_app_qw_spectator_camera_take_teleport(qa_app_qw_spectator_camera *camera, qa_vec3 *out) {
  if (!camera->has_teleport) return false;
  *out = camera->teleport; camera->has_teleport = false; return true;
}
void qa_app_qw_spectator_camera_reset(qa_app_qw_spectator_camera *camera) {
  camera->tracking = false; camera->locked = false; camera->slot = 0;
  camera->old_buttons = 0; camera->has_current = false; camera->has_teleport = false;
}
static void cam_lock(qa_app_qw_spectator_camera *c, double slot) {
  char text[32];
  snprintf(text, sizeof text, "ptrack %d", (int)slot);
  c->host.send_command(c->host.ctx, text);
  c->slot = slot; c->locked = false; c->has_current = false;
}
static void cam_unlock(qa_app_qw_spectator_camera *c) {
  if (c->tracking) c->host.send_command(c->host.ctx, "ptrack");
  c->tracking = false; c->locked = false; c->has_current = false;
}
static bool eligible(const qa_app_qw_camera_frame *frame, int slot) {
  return frame->has_user[slot] && frame->users[slot].name && frame->users[slot].name[0]
      && !frame->users[slot].spectator;
}
static void high_target(qa_app_qw_spectator_camera *c, const qa_app_qw_camera_frame *frame) {
  int best = -1; double frags = -9999;
  for (int slot = 0; slot < 32; slot++)
    if (frame->has_user[slot] && eligible(frame, slot) && frame->users[slot].frags > frags) {
      best = slot; frags = frame->users[slot].frags;
    }
  if (best < 0) cam_unlock(c);
  else {
    double current = frame->has_user[(int)c->slot] ? frame->users[(int)c->slot].frags : -9999;
    if (!c->locked || frags > current) cam_lock(c, best);
  }
}
static bool visible(qa_app_qw_spectator_camera *c, const qa_qw_player_state *target) {
  qa_app_qw_camera_trace result = c->host.trace(c->host.ctx, target->origin, c->desired);
  return result.fraction == 1 && !result.in_water
      && vlength(subtract(target->origin, c->desired)) >= 16;
}
static bool flyby(qa_app_qw_spectator_camera *c, const qa_qw_player_state *self,
    const qa_qw_player_state *target, bool check_visibility) {
  double yaw = target->command.angles.y * M_PI / 180, roll = target->command.angles.z * M_PI / 180;
  qa_vec3 forward = vector3(cos(yaw), sin(yaw), 0);
  qa_vec3 right = vector3(cos(roll) * sin(yaw), -cos(roll) * cos(yaw), -sin(roll));
  qa_vec3 up = vector3(sin(roll) * sin(yaw), -sin(roll) * cos(yaw), cos(roll));
  qa_vec3 directions[12] = {
    add(add(forward, up), right), subtract(add(forward, up), right),
    add(forward, right), subtract(forward, right), add(forward, up), subtract(forward, up),
    subtract(add(up, right), forward), subtract(subtract(up, right), forward),
    vector3(-forward.x, -forward.y, -forward.z), forward,
    vector3(-right.x, -right.y, -right.z), right};
  double best = 1000; bool found = false; qa_vec3 position = {0, 0, 0};
  for (int i = 0; i < 12; i++) {
    qa_vec3 direction = directions[i];
    double magnitude = vlength(direction);
    qa_vec3 unit = vector3(direction.x / magnitude, direction.y / magnitude, direction.z / magnitude);
    qa_vec3 end = vector3(target->origin.x + 800 * unit.x, target->origin.y + 800 * unit.y,
        target->origin.z + 800 * unit.z);
    qa_app_qw_camera_trace result = c->host.trace(c->host.ctx, target->origin, end);
    if (result.in_water) continue;
    double distance = vlength(subtract(result.end, target->origin));
    if (distance < 32 || distance > 800) continue;
    if (check_visibility) {
      distance = vlength(subtract(result.end, self->origin));
      qa_app_qw_camera_trace seen = c->host.trace(c->host.ctx, self->origin, result.end);
      if (seen.fraction != 1 || seen.in_water) continue;
    }
    if (distance < best) { best = distance; position = result.end; found = true; }
  }
  if (!found) return false;
  c->locked = true; c->desired = position; return true;
}
static void finish(qa_app_qw_spectator_camera *c, qa_qw_user_command command,
    const qa_app_qw_camera_frame *frame) {
  int buttons = (int)command.buttons, old = (int)c->old_buttons;
  if ((buttons & 1) != 0) {
    if ((old & 1) != 0) return;
    c->old_buttons = old | 1;
    if (c->tracking) { cam_unlock(c); return; }
    c->tracking = true;
  } else {
    c->old_buttons = old & ~1;
    if (!c->tracking) return;
  }
  if (c->options.hightrack(c->options.ctx) != 0) { high_target(c, frame); return; }
  if (c->locked) {
    if ((buttons & 2) != 0 && ((int)c->old_buttons & 2) != 0) return;
    if ((buttons & 2) == 0) { c->old_buttons = (int)c->old_buttons & ~2; return; }
    c->old_buttons = (int)c->old_buttons | 2;
  }
  int start = c->locked ? ((int)c->slot + 1) % 32 : (int)c->slot;
  for (int offset = 0; offset < 32; offset++) {
    int slot = (start + offset) % 32;
    if (eligible(frame, slot)) { cam_lock(c, slot); return; }
  }
  c->host.print(c->host.ctx, "No target found ...\n");
  c->tracking = false; c->locked = false; c->has_current = false;
}
qa_qw_user_command qa_app_qw_spectator_camera_command(qa_app_qw_spectator_camera *camera,
    qa_qw_user_command command, const qa_app_qw_camera_frame *frame) {
  qa_qw_user_command result = command;
  if (camera->options.hightrack(camera->options.ctx) != 0 && !camera->locked)
    high_target(camera, frame);
  if (camera->tracking) {
    if (camera->locked && !eligible(frame, (int)camera->slot)) {
      camera->locked = false;
      if (camera->options.hightrack(camera->options.ctx) != 0) high_target(camera, frame);
      else cam_unlock(camera);
    } else if (frame->has_player[(int)camera->slot]) {
      const qa_qw_player_state *target = &frame->players[(int)camera->slot];
      if (!camera->locked || !visible(camera, target)) {
        if (!camera->locked || frame->seconds - camera->last_view_seconds > 0.1) {
          if (!flyby(camera, &frame->self, target, true))
            flyby(camera, &frame->self, target, false);
          camera->last_view_seconds = frame->seconds;
        }
      } else camera->last_view_seconds = frame->seconds;
      if (camera->locked) {
        bool chase = camera->options.chasecam(camera->options.ctx) != 0;
        if (chase) camera->desired = target->origin;
        qa_vec3 delta = subtract(camera->desired, frame->self.origin);
        if (chase ? delta.x != 0 || delta.y != 0 || delta.z != 0 : vlength(delta) > 16) {
          camera->teleport = camera->desired; camera->has_teleport = true;
        }
        qa_vec3 angles = chase ? target->command.angles
            : view_angles(subtract(target->origin, camera->desired));
        camera->current.origin = camera->desired; camera->current.angles = angles;
        camera->current.target = *target; camera->current.chase = chase;
        camera->has_current = true;
        result.angles = angles;
        result.forward_move = 0; result.side_move = 0; result.up_move = 0;
      }
    }
  }
  finish(camera, result, frame);
  return result;
}
