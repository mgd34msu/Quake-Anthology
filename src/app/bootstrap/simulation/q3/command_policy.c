/* Port of src/app/bootstrap/simulation/q3/command-policy.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/command_policy.h"
#include "qa/foundation/support.h"
typedef struct policy { qa_q3_base_user_command before, after; bool frozen; } policy;
static double forward_axis(policy p, double value, double changed) {
  return p.frozen ? 0 : p.before.forwardmove == p.after.forwardmove ? value : changed;
}
static double side_axis(policy p, double value, double changed) {
  return p.frozen ? 0 : p.before.rightmove == p.after.rightmove ? value : changed;
}
static double up_axis(policy p, double value, double changed) {
  return p.frozen ? 0 : p.before.upmove == p.after.upmove ? value : changed;
}
static qa_vec3 angles(policy p, qa_vec3 value, qa_vec3 changed) {
  qa_vec3 result = {p.before.angles.x == p.after.angles.x ? value.x : changed.x,
                    p.before.angles.y == p.after.angles.y ? value.y : changed.y,
                    p.before.angles.z == p.after.angles.z ? value.z : changed.z};
  return result;
}
static double buttons(policy p, double value, double changed, long long jump_mask) {
  if (p.frozen) return 0;
  long long mask = 1 | (p.before.upmove == p.after.upmove ? 0 : jump_mask);
  return (double)((((long long)value) & ~mask) | (((long long)changed) & mask));
}
qa_actor_command qa_app_apply_q3_command_policy(qa_actor_command pending, qa_q3_base_user_command before,
    qa_q3_base_user_command after, qa_user_command converted, bool frozen,
    qa_arsenal_intent *arsenal_storage) {
  qa_actor_command envelope = pending;
  if (frozen && pending.arsenal) {
    *arsenal_storage = *pending.arsenal;
    arsenal_storage->weapon = NULL;
    arsenal_storage->use_holdable = false;
    envelope.arsenal = arsenal_storage;
  }
  qa_user_command original = pending.command;
  if (original.kind == QA_USER_COMMAND_Q3) { envelope.command = converted; return envelope; }
  policy p = {before, after, frozen};
  switch (original.kind) {
    case QA_USER_COMMAND_Q1: {
      if (converted.kind != original.kind) qa_range_error("Q3 policy changed the selected command dialect");
      qa_user_command command = original;
      command.as.q1.acknowledged_server_time_seconds = before.server_time == after.server_time
          ? original.as.q1.acknowledged_server_time_seconds : converted.as.q1.acknowledged_server_time_seconds;
      command.as.q1.view_angles = angles(p, original.as.q1.view_angles, converted.as.q1.view_angles);
      command.as.q1.forward_move = forward_axis(p, original.as.q1.forward_move, converted.as.q1.forward_move);
      command.as.q1.side_move = side_axis(p, original.as.q1.side_move, converted.as.q1.side_move);
      command.as.q1.up_move = up_axis(p, original.as.q1.up_move, converted.as.q1.up_move);
      command.as.q1.buttons = buttons(p, original.as.q1.buttons, converted.as.q1.buttons, 2);
      command.as.q1.impulse = frozen ? 0 : original.as.q1.impulse;
      envelope.command = command; return envelope;
    }
    case QA_USER_COMMAND_QW: {
      if (converted.kind != original.kind) qa_range_error("Q3 policy changed the selected command dialect");
      qa_user_command command = original;
      command.as.qw.milliseconds = converted.as.qw.milliseconds;
      command.as.qw.angles = angles(p, original.as.qw.angles, converted.as.qw.angles);
      command.as.qw.forward_move = forward_axis(p, original.as.qw.forward_move, converted.as.qw.forward_move);
      command.as.qw.side_move = side_axis(p, original.as.qw.side_move, converted.as.qw.side_move);
      command.as.qw.up_move = up_axis(p, original.as.qw.up_move, converted.as.qw.up_move);
      command.as.qw.buttons = buttons(p, original.as.qw.buttons, converted.as.qw.buttons, 0);
      command.as.qw.impulse = frozen ? 0 : original.as.qw.impulse;
      envelope.command = command; return envelope;
    }
    case QA_USER_COMMAND_Q2: {
      if (converted.kind != original.kind) qa_range_error("Q3 policy changed the selected command dialect");
      qa_user_command command = original;
      command.as.q2.milliseconds = converted.as.q2.milliseconds;
      command.as.q2.angle_shorts.values[0] = before.angles.x == after.angles.x ? original.as.q2.angle_shorts.values[0] : converted.as.q2.angle_shorts.values[0];
      command.as.q2.angle_shorts.values[1] = before.angles.y == after.angles.y ? original.as.q2.angle_shorts.values[1] : converted.as.q2.angle_shorts.values[1];
      command.as.q2.angle_shorts.values[2] = before.angles.z == after.angles.z ? original.as.q2.angle_shorts.values[2] : converted.as.q2.angle_shorts.values[2];
      command.as.q2.forward_move = forward_axis(p, original.as.q2.forward_move, converted.as.q2.forward_move);
      command.as.q2.side_move = side_axis(p, original.as.q2.side_move, converted.as.q2.side_move);
      command.as.q2.up_move = up_axis(p, original.as.q2.up_move, converted.as.q2.up_move);
      command.as.q2.buttons = buttons(p, original.as.q2.buttons, converted.as.q2.buttons, 0);
      command.as.q2.impulse = frozen ? 0 : original.as.q2.impulse;
      envelope.command = command; return envelope;
    }
    case QA_USER_COMMAND_Q2_RERELEASE: {
      if (converted.kind != original.kind) qa_range_error("Q3 policy changed the selected command dialect");
      qa_user_command command = original;
      command.as.q2_rerelease.milliseconds = converted.as.q2_rerelease.milliseconds;
      command.as.q2_rerelease.angles = angles(p, original.as.q2_rerelease.angles, converted.as.q2_rerelease.angles);
      command.as.q2_rerelease.forward_move = forward_axis(p, original.as.q2_rerelease.forward_move, converted.as.q2_rerelease.forward_move);
      command.as.q2_rerelease.side_move = side_axis(p, original.as.q2_rerelease.side_move, converted.as.q2_rerelease.side_move);
      command.as.q2_rerelease.buttons = buttons(p, original.as.q2_rerelease.buttons, converted.as.q2_rerelease.buttons, 8 | 16);
      envelope.command = command; return envelope;
    }
    default: return envelope;
  }
}
