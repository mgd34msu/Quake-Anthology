/* Port of src/network/q2/codecs/kex-usercmd.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/kex_usercmd.h"

void qa_q2_read_kex_usercmd(qa_q2_sizebuf *message, const qa_q2_wire_usercmd *from,
                            qa_q2_wire_usercmd *to) {
  int32_t bits = qa_q2_msg_read_byte(message);
  to->angles[0] = from->angles[0];
  to->angles[1] = from->angles[1];
  to->angles[2] = from->angles[2];
  to->forwardmove = from->forwardmove;
  to->sidemove = from->sidemove;
  to->buttons = from->buttons;
  to->server_frame = from->server_frame;
  to->upmove = 0;
  to->impulse = 0;
  for (int axis = 0; axis < 3; axis++) {
    if ((bits & (1 << axis)) != 0) {
      to->angles[axis] = (int16_t)qa_q2_angle2short(qa_q2_msg_read_float(message));
    }
  }
  if ((bits & 8) != 0) to->forwardmove = qa_q2_msg_read_float(message);
  if ((bits & 16) != 0) to->sidemove = qa_q2_msg_read_float(message);
  if ((bits & 64) != 0) to->buttons = (double)qa_q2_msg_read_byte(message);
  if ((bits & 128) != 0) to->server_frame = (double)qa_q2_msg_read_long(message);
  to->msec = (double)qa_q2_msg_read_byte(message);
}

void qa_q2_write_kex_usercmd(qa_q2_sizebuf *message, const qa_q2_wire_usercmd *from,
                             const qa_q2_wire_usercmd *to) {
  int32_t bits = 0;
  for (int axis = 0; axis < 3; axis++) {
    if (from->angles[axis] != to->angles[axis]) bits |= 1 << axis;
  }
  if (from->forwardmove != to->forwardmove) bits |= 8;
  if (from->sidemove != to->sidemove) bits |= 16;
  if (from->buttons != to->buttons) bits |= 64;
  if (from->server_frame != to->server_frame) bits |= 128;
  qa_q2_msg_write_byte(message, (double)bits);
  for (int axis = 0; axis < 3; axis++) {
    if ((bits & (1 << axis)) != 0) {
      qa_q2_msg_write_float(message, qa_q2_short2angle((double)to->angles[axis]));
    }
  }
  if ((bits & 8) != 0) qa_q2_msg_write_float(message, to->forwardmove);
  if ((bits & 16) != 0) qa_q2_msg_write_float(message, to->sidemove);
  if ((bits & 64) != 0) qa_q2_msg_write_byte(message, to->buttons);
  if ((bits & 128) != 0) qa_q2_msg_write_long(message, to->server_frame);
  qa_q2_msg_write_byte(message, to->msec);
}
