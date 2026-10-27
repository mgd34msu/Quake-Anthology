/* Port of src/network/q2/message.ts. GPL-2.0-or-later. */
#include "qa/network/q2/message.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/anorms.h"
#include "qa/network/q2/constants.h"

/* TS number -> int32 (the ToInt32 behind &, >>, |). */
static int32_t to_i32(double v) {
  if (!isfinite(v)) return 0;
  double t = trunc(v);
  double m = fmod(fabs(t), 4294967296.0);
  if (t < 0) m = -m;
  if (m >= 2147483648.0) m -= 4294967296.0;
  if (m < -2147483648.0) m += 4294967296.0;
  return (int32_t)m;
}
static uint32_t to_u32(double v) { return (uint32_t)to_i32(v); }

void qa_q2_sizebuf_init_empty(qa_q2_sizebuf *buf) { memset(buf, 0, sizeof *buf); }

void qa_q2_sz_init(qa_q2_sizebuf *buf, uint8_t *data, size_t data_length,
                   int64_t length) {
  buf->allow_overflow = false;
  buf->overflowed = false;
  buf->cursize = 0;
  buf->readcount = 0;
  buf->data = data;
  buf->data_length = data_length;
  buf->maxsize = length;
  buf->owns_data = false;
}

void qa_q2_sz_clear(qa_q2_sizebuf *buf) {
  buf->cursize = 0;
  buf->overflowed = false;
}

int64_t qa_q2_sz_get_space(qa_q2_sizebuf *buf, int64_t length) {
  if (buf->observer != NULL) buf->observer(buf->observer_ctx, buf, buf->cursize, length);
  if (buf->cursize + length > buf->maxsize) {
    if (!buf->allow_overflow) {
      qa_q2_com_error_throw(QA_Q2_ERR_FATAL,
                            "SZ_GetSpace: overflow without allowoverflow set");
    }
    if (length > buf->maxsize) {
      qa_q2_com_error_throw(QA_Q2_ERR_FATAL,
                            "SZ_GetSpace: %lld is > full buffer size",
                            (long long)length);
    }
    qa_q2_sz_clear(buf);
    buf->overflowed = true;
  }
  int64_t offset = buf->cursize;
  buf->cursize += length;
  return offset;
}

void qa_q2_sz_write(qa_q2_sizebuf *buf, const uint8_t *data, size_t data_length,
                    int64_t length) {
  int64_t offset = qa_q2_sz_get_space(buf, length);
  /* TS: buf.data.set(data.subarray(0, length), offset) silently drops
   * bytes past the destination span. */
  for (int64_t i = 0; i < length; i++) {
    int64_t at = offset + i;
    if (at >= 0 && (uint64_t)at < (uint64_t)buf->data_length &&
        (uint64_t)i < (uint64_t)data_length) {
      buf->data[at] = data[i];
    }
  }
}

static size_t nul_terminated_bytes(const char *s, uint8_t *out) {
  size_t n = strlen(s);
  for (size_t i = 0; i < n; i++) out[i] = (uint8_t)s[i];
  out[n] = 0;
  return n + 1;
}

void qa_q2_sz_print(qa_q2_sizebuf *buf, const char *text) {
  size_t n = strlen(text);
  uint8_t *bytes = (uint8_t *)malloc(n + 1);
  if (bytes == NULL) qa_range_error("out of memory");
  size_t len = nul_terminated_bytes(text, bytes);
  if (buf->cursize != 0) {
    if (qa_q2_read_element_u8(buf->data, buf->data_length, buf->cursize - 1) != 0) {
      int64_t offset = qa_q2_sz_get_space(buf, (int64_t)len);
      for (size_t i = 0; i < len; i++) {
        int64_t at = offset + (int64_t)i;
        if (at >= 0 && (uint64_t)at < (uint64_t)buf->data_length) buf->data[at] = bytes[i];
      }
    } else {
      int64_t offset = qa_q2_sz_get_space(buf, (int64_t)len - 1) - 1;
      for (size_t i = 0; i < len; i++) {
        int64_t at = offset + (int64_t)i;
        if (at >= 0 && (uint64_t)at < (uint64_t)buf->data_length) buf->data[at] = bytes[i];
      }
    }
  } else {
    int64_t offset = qa_q2_sz_get_space(buf, (int64_t)len);
    for (size_t i = 0; i < len; i++) {
      int64_t at = offset + (int64_t)i;
      if (at >= 0 && (uint64_t)at < (uint64_t)buf->data_length) buf->data[at] = bytes[i];
    }
  }
  free(bytes);
}

size_t qa_q2_string_to_bytes(const char *text, uint8_t *out, size_t out_capacity) {
  size_t n = strlen(text);
  size_t m = n < out_capacity ? n : out_capacity;
  for (size_t i = 0; i < m; i++) out[i] = (uint8_t)text[i];
  return n;
}

static void put_byte(qa_q2_sizebuf *sb, int64_t offset, uint8_t v) {
  /* TS typed-array stores past the span drop silently. */
  if (offset >= 0 && (uint64_t)offset < (uint64_t)sb->data_length) sb->data[offset] = v;
}

void qa_q2_msg_write_char(qa_q2_sizebuf *sb, double c) {
  int64_t offset = qa_q2_sz_get_space(sb, 1);
  put_byte(sb, offset, (uint8_t)(to_i32(c) & 0xff));
}
void qa_q2_msg_write_byte(qa_q2_sizebuf *sb, double c) {
  int64_t offset = qa_q2_sz_get_space(sb, 1);
  put_byte(sb, offset, (uint8_t)(to_i32(c) & 0xff));
}
void qa_q2_msg_write_short(qa_q2_sizebuf *sb, double c) {
  int32_t v = to_i32(c);
  int64_t offset = qa_q2_sz_get_space(sb, 2);
  put_byte(sb, offset, (uint8_t)(v & 0xff));
  put_byte(sb, offset + 1, (uint8_t)((v >> 8) & 0xff));
}
void qa_q2_msg_write_long(qa_q2_sizebuf *sb, double c) {
  int32_t v = to_i32(c);
  int64_t offset = qa_q2_sz_get_space(sb, 4);
  put_byte(sb, offset, (uint8_t)(v & 0xff));
  put_byte(sb, offset + 1, (uint8_t)((v >> 8) & 0xff));
  put_byte(sb, offset + 2, (uint8_t)((v >> 16) & 0xff));
  put_byte(sb, offset + 3, (uint8_t)((v >> 24) & 0xff));
}
static void check_view_span(qa_q2_sizebuf *sb, int64_t offset, int64_t width) {
  if (offset < 0 || offset + width > (int64_t)sb->data_length) {
    qa_range_error("Offset is outside the bounds of the DataView");
  }
}
void qa_q2_msg_write_float(qa_q2_sizebuf *sb, double f) {
  int64_t offset = qa_q2_sz_get_space(sb, 4);
  check_view_span(sb, offset, 4);
  float v = (float)f;
  uint32_t bits;
  memcpy(&bits, &v, sizeof bits);
  for (int i = 0; i < 4; i++) sb->data[offset + i] = (uint8_t)((bits >> (8 * i)) & 0xffu);
}
void qa_q2_msg_write_long64(qa_q2_sizebuf *sb, int64_t c) {
  int64_t offset = qa_q2_sz_get_space(sb, 8);
  check_view_span(sb, offset, 8);
  uint64_t bits = (uint64_t)c;
  for (int i = 0; i < 8; i++) sb->data[offset + i] = (uint8_t)((bits >> (8 * i)) & 0xffu);
}
void qa_q2_msg_write_string(qa_q2_sizebuf *sb, const char *s) {
  if (s == NULL) {
    uint8_t zero = 0;
    qa_q2_sz_write(sb, &zero, 1, 1);
  } else {
    size_t n = strlen(s);
    uint8_t *bytes = (uint8_t *)malloc(n + 1);
    if (bytes == NULL) qa_range_error("out of memory");
    size_t len = nul_terminated_bytes(s, bytes);
    qa_q2_sz_write(sb, bytes, len, (int64_t)len);
    free(bytes);
  }
}
void qa_q2_msg_write_coord(qa_q2_sizebuf *sb, double f) {
  qa_q2_msg_write_short(sb, trunc(f * 8.0));
}
void qa_q2_msg_write_pos(qa_q2_sizebuf *sb, const qa_q2_wire_vec3 pos) {
  qa_q2_msg_write_short(sb, trunc((double)pos[0] * 8.0));
  qa_q2_msg_write_short(sb, trunc((double)pos[1] * 8.0));
  qa_q2_msg_write_short(sb, trunc((double)pos[2] * 8.0));
}
void qa_q2_msg_write_angle(qa_q2_sizebuf *sb, double f) {
  qa_q2_msg_write_byte(sb, (double)(to_i32(trunc((f * 256.0) / 360.0)) & 255));
}
void qa_q2_msg_write_angle16(qa_q2_sizebuf *sb, double f) {
  qa_q2_msg_write_short(sb, (double)qa_q2_angle2short(f));
}

void qa_q2_msg_write_delta_usercmd(qa_q2_sizebuf *buf,
                                   const qa_q2_wire_usercmd *from,
                                   const qa_q2_wire_usercmd *cmd) {
  int32_t bits = 0;
  if (cmd->angles[0] != from->angles[0]) bits |= QA_Q2_CM_ANGLE1;
  if (cmd->angles[1] != from->angles[1]) bits |= QA_Q2_CM_ANGLE2;
  if (cmd->angles[2] != from->angles[2]) bits |= QA_Q2_CM_ANGLE3;
  if (cmd->forwardmove != from->forwardmove) bits |= QA_Q2_CM_FORWARD;
  if (cmd->sidemove != from->sidemove) bits |= QA_Q2_CM_SIDE;
  if (cmd->upmove != from->upmove) bits |= QA_Q2_CM_UP;
  if (cmd->buttons != from->buttons) bits |= QA_Q2_CM_BUTTONS;
  if (cmd->impulse != from->impulse) bits |= QA_Q2_CM_IMPULSE;
  qa_q2_msg_write_byte(buf, (double)bits);
  if (bits & QA_Q2_CM_ANGLE1) qa_q2_msg_write_short(buf, (double)cmd->angles[0]);
  if (bits & QA_Q2_CM_ANGLE2) qa_q2_msg_write_short(buf, (double)cmd->angles[1]);
  if (bits & QA_Q2_CM_ANGLE3) qa_q2_msg_write_short(buf, (double)cmd->angles[2]);
  if (bits & QA_Q2_CM_FORWARD) qa_q2_msg_write_short(buf, cmd->forwardmove);
  if (bits & QA_Q2_CM_SIDE) qa_q2_msg_write_short(buf, cmd->sidemove);
  if (bits & QA_Q2_CM_UP) qa_q2_msg_write_short(buf, cmd->upmove);
  if (bits & QA_Q2_CM_BUTTONS) qa_q2_msg_write_byte(buf, cmd->buttons);
  if (bits & QA_Q2_CM_IMPULSE) qa_q2_msg_write_byte(buf, cmd->impulse);
  qa_q2_msg_write_byte(buf, cmd->msec);
  qa_q2_msg_write_byte(buf, cmd->lightlevel);
}

void qa_q2_msg_write_dir(qa_q2_sizebuf *sb, const qa_q2_wire_vec3 dir_or_null) {
  if (dir_or_null == NULL) {
    qa_q2_msg_write_byte(sb, 0);
    return;
  }
  double bestd = 0;
  int32_t best = 0;
  for (int i = 0; i < QA_Q2_NUMVERTEXNORMALS; i++) {
    double d = qa_q2_dot_product(dir_or_null, qa_q2_bytedirs[i]);
    if (d > bestd) {
      bestd = d;
      best = i;
    }
  }
  qa_q2_msg_write_byte(sb, (double)best);
}

void qa_q2_msg_read_dir(qa_q2_sizebuf *sb, qa_q2_wire_vec3 dir) {
  int32_t b = qa_q2_msg_read_byte(sb);
  if (b >= QA_Q2_NUMVERTEXNORMALS) {
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "MSF_ReadDir: out of range");
  }
  if (b < 0) {
    qa_range_error("Q2 field index outside storage: %d", (int)b);
  }
  qa_q2_vector_copy(qa_q2_bytedirs[b], dir);
}

void qa_q2_msg_write_delta_entity(const qa_q2_wire_entity *from,
                                  const qa_q2_wire_entity *to,
                                  qa_q2_sizebuf *msg, bool force,
                                  bool newentity) {
  if (to->number == 0) qa_q2_com_error_throw(QA_Q2_ERR_FATAL, "Unset entity number");
  if (to->number >= QA_Q2_MAX_EDICTS) {
    qa_q2_com_error_throw(QA_Q2_ERR_FATAL, "Entity number >= MAX_EDICTS");
  }
  int32_t bits = 0;
  if (to->number >= 256) bits |= QA_Q2_U_NUMBER16;
  if (to->origin[0] != from->origin[0]) bits |= QA_Q2_U_ORIGIN1;
  if (to->origin[1] != from->origin[1]) bits |= QA_Q2_U_ORIGIN2;
  if (to->origin[2] != from->origin[2]) bits |= QA_Q2_U_ORIGIN3;
  if (to->angles[0] != from->angles[0]) bits |= QA_Q2_U_ANGLE1;
  if (to->angles[1] != from->angles[1]) bits |= QA_Q2_U_ANGLE2;
  if (to->angles[2] != from->angles[2]) bits |= QA_Q2_U_ANGLE3;
  if (to->skinnum != from->skinnum) {
    if (to_u32(to->skinnum) < 256u) bits |= QA_Q2_U_SKIN8;
    else if (to_u32(to->skinnum) < 0x10000u) bits |= QA_Q2_U_SKIN16;
    else bits |= QA_Q2_U_SKIN8 | QA_Q2_U_SKIN16;
  }
  if (to->frame != from->frame) {
    if (to->frame < 256) bits |= QA_Q2_U_FRAME8;
    else bits |= QA_Q2_U_FRAME16;
  }
  if (to->effects != from->effects) {
    if (to->effects < 256) bits |= QA_Q2_U_EFFECTS8;
    else if (to->effects < 0x8000) bits |= QA_Q2_U_EFFECTS16;
    else bits |= QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16;
  }
  if (to->renderfx != from->renderfx) {
    if (to->renderfx < 256) bits |= QA_Q2_U_RENDERFX8;
    else if (to->renderfx < 0x8000) bits |= QA_Q2_U_RENDERFX16;
    else bits |= QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16;
  }
  if (to->solid != from->solid) bits |= QA_Q2_U_SOLID;
  if (to->event != 0) bits |= QA_Q2_U_EVENT;
  if (to->modelindex != from->modelindex) bits |= QA_Q2_U_MODEL;
  if (to->modelindex2 != from->modelindex2) bits |= QA_Q2_U_MODEL2;
  if (to->modelindex3 != from->modelindex3) bits |= QA_Q2_U_MODEL3;
  if (to->modelindex4 != from->modelindex4) bits |= QA_Q2_U_MODEL4;
  if (to->sound != from->sound) bits |= QA_Q2_U_SOUND;
  if (newentity || (to_i32(to->renderfx) & QA_Q2_RF_BEAM) != 0) bits |= QA_Q2_U_OLDORIGIN;
  if (bits == 0 && !force) return;
  if ((bits & (int32_t)0xff000000u) != 0) {
    bits |= QA_Q2_U_MOREBITS3 | QA_Q2_U_MOREBITS2 | QA_Q2_U_MOREBITS1;
  } else if ((bits & 0x00ff0000) != 0) {
    bits |= QA_Q2_U_MOREBITS2 | QA_Q2_U_MOREBITS1;
  } else if ((bits & 0x0000ff00) != 0) {
    bits |= QA_Q2_U_MOREBITS1;
  }
  qa_q2_msg_write_byte(msg, (double)(bits & 255));
  if ((bits & (int32_t)0xff000000u) != 0) {
    qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
    qa_q2_msg_write_byte(msg, (double)((bits >> 16) & 255));
    qa_q2_msg_write_byte(msg, (double)((bits >> 24) & 255));
  } else if ((bits & 0x00ff0000) != 0) {
    qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
    qa_q2_msg_write_byte(msg, (double)((bits >> 16) & 255));
  } else if ((bits & 0x0000ff00) != 0) {
    qa_q2_msg_write_byte(msg, (double)((bits >> 8) & 255));
  }
  if (bits & QA_Q2_U_NUMBER16) qa_q2_msg_write_short(msg, to->number);
  else qa_q2_msg_write_byte(msg, to->number);
  if (bits & QA_Q2_U_MODEL) qa_q2_msg_write_byte(msg, to->modelindex);
  if (bits & QA_Q2_U_MODEL2) qa_q2_msg_write_byte(msg, to->modelindex2);
  if (bits & QA_Q2_U_MODEL3) qa_q2_msg_write_byte(msg, to->modelindex3);
  if (bits & QA_Q2_U_MODEL4) qa_q2_msg_write_byte(msg, to->modelindex4);
  if (bits & QA_Q2_U_FRAME8) qa_q2_msg_write_byte(msg, to->frame);
  if (bits & QA_Q2_U_FRAME16) qa_q2_msg_write_short(msg, to->frame);
  if ((bits & QA_Q2_U_SKIN8) != 0 && (bits & QA_Q2_U_SKIN16) != 0) {
    qa_q2_msg_write_long(msg, to->skinnum);
  } else if (bits & QA_Q2_U_SKIN8) {
    qa_q2_msg_write_byte(msg, to->skinnum);
  } else if (bits & QA_Q2_U_SKIN16) {
    qa_q2_msg_write_short(msg, to->skinnum);
  }
  if ((bits & (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) ==
      (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) {
    qa_q2_msg_write_long(msg, to->effects);
  } else if (bits & QA_Q2_U_EFFECTS8) {
    qa_q2_msg_write_byte(msg, to->effects);
  } else if (bits & QA_Q2_U_EFFECTS16) {
    qa_q2_msg_write_short(msg, to->effects);
  }
  if ((bits & (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) ==
      (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) {
    qa_q2_msg_write_long(msg, to->renderfx);
  } else if (bits & QA_Q2_U_RENDERFX8) {
    qa_q2_msg_write_byte(msg, to->renderfx);
  } else if (bits & QA_Q2_U_RENDERFX16) {
    qa_q2_msg_write_short(msg, to->renderfx);
  }
  if (bits & QA_Q2_U_ORIGIN1) qa_q2_msg_write_coord(msg, (double)to->origin[0]);
  if (bits & QA_Q2_U_ORIGIN2) qa_q2_msg_write_coord(msg, (double)to->origin[1]);
  if (bits & QA_Q2_U_ORIGIN3) qa_q2_msg_write_coord(msg, (double)to->origin[2]);
  if (bits & QA_Q2_U_ANGLE1) qa_q2_msg_write_angle(msg, (double)to->angles[0]);
  if (bits & QA_Q2_U_ANGLE2) qa_q2_msg_write_angle(msg, (double)to->angles[1]);
  if (bits & QA_Q2_U_ANGLE3) qa_q2_msg_write_angle(msg, (double)to->angles[2]);
  if (bits & QA_Q2_U_OLDORIGIN) {
    qa_q2_msg_write_coord(msg, (double)to->old_origin[0]);
    qa_q2_msg_write_coord(msg, (double)to->old_origin[1]);
    qa_q2_msg_write_coord(msg, (double)to->old_origin[2]);
  }
  if (bits & QA_Q2_U_SOUND) qa_q2_msg_write_byte(msg, to->sound);
  if (bits & QA_Q2_U_EVENT) qa_q2_msg_write_byte(msg, to->event);
  if (bits & QA_Q2_U_SOLID) qa_q2_msg_write_short(msg, to->solid);
}

void qa_q2_msg_begin_reading(qa_q2_sizebuf *msg) { msg->readcount = 0; }

int32_t qa_q2_msg_read_char(qa_q2_sizebuf *msg) {
  int32_t c;
  if (msg->readcount + 1 > msg->cursize) {
    c = -1;
  } else {
    int32_t b = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount);
    c = (b << 24) >> 24;
  }
  msg->readcount++;
  return c;
}
int32_t qa_q2_msg_read_byte(qa_q2_sizebuf *msg) {
  int32_t c;
  if (msg->readcount + 1 > msg->cursize) {
    c = -1;
  } else {
    c = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount);
  }
  msg->readcount++;
  return c;
}
int32_t qa_q2_msg_read_short(qa_q2_sizebuf *msg) {
  int32_t c;
  if (msg->readcount + 2 > msg->cursize) {
    c = -1;
  } else {
    int32_t b0 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount);
    int32_t b1 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount + 1);
    c = ((b0 | (b1 << 8)) << 16) >> 16;
  }
  msg->readcount += 2;
  return c;
}
int32_t qa_q2_msg_read_word(qa_q2_sizebuf *msg) {
  int32_t c;
  if (msg->readcount + 2 > msg->cursize) {
    c = -1;
  } else {
    int32_t b0 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount);
    int32_t b1 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount + 1);
    c = (int32_t)((uint32_t)(b0 | (b1 << 8)));
  }
  msg->readcount += 2;
  return c;
}
int32_t qa_q2_msg_read_long(qa_q2_sizebuf *msg) {
  int32_t c;
  if (msg->readcount + 4 > msg->cursize) {
    c = -1;
  } else {
    int32_t b0 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount);
    int32_t b1 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount + 1);
    int32_t b2 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount + 2);
    int32_t b3 = qa_q2_read_element_u8(msg->data, msg->data_length, msg->readcount + 3);
    c = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
  }
  msg->readcount += 4;
  return c;
}
double qa_q2_msg_read_float(qa_q2_sizebuf *msg) {
  double f;
  if (msg->readcount + 4 > msg->cursize) {
    f = -1;
  } else {
    check_view_span(msg, msg->readcount, 4);
    uint32_t bits = 0;
    for (int i = 0; i < 4; i++) {
      bits |= (uint32_t)msg->data[msg->readcount + i] << (8 * i);
    }
    float v;
    memcpy(&v, &bits, sizeof v);
    f = (double)v;
  }
  msg->readcount += 4;
  return f;
}
int64_t qa_q2_msg_read_long64(qa_q2_sizebuf *msg) {
  int64_t c;
  if (msg->readcount + 8 > msg->cursize) {
    c = -1;
  } else {
    check_view_span(msg, msg->readcount, 8);
    uint64_t bits = 0;
    for (int i = 0; i < 8; i++) {
      bits |= (uint64_t)msg->data[msg->readcount + i] << (8 * i);
    }
    memcpy(&c, &bits, sizeof c);
  }
  msg->readcount += 8;
  return c;
}
static size_t read_string_impl(qa_q2_sizebuf *msg, char *out, size_t out_capacity,
                               bool line) {
  size_t l = 0;
  size_t stored = 0;
  for (;;) {
    int32_t c = qa_q2_msg_read_char(msg);
    if (c == -1 || c == 0 || (line && c == 10)) break;
    if (out != NULL && stored + 1 < out_capacity) out[stored++] = (char)(c & 0xff);
    l++;
    if (!(l < 2048 - 1)) break;
  }
  if (out != NULL && out_capacity > 0) out[stored] = '\0';
  return l;
}
size_t qa_q2_msg_read_string(qa_q2_sizebuf *msg, char *out, size_t out_capacity) {
  return read_string_impl(msg, out, out_capacity, false);
}
size_t qa_q2_msg_read_string_line(qa_q2_sizebuf *msg, char *out,
                                  size_t out_capacity) {
  return read_string_impl(msg, out, out_capacity, true);
}
double qa_q2_msg_read_coord(qa_q2_sizebuf *msg) {
  return (double)qa_q2_msg_read_short(msg) * (1.0 / 8.0);
}
void qa_q2_msg_read_pos(qa_q2_sizebuf *msg, qa_q2_wire_vec3 pos) {
  /* Float32Array stores round each double to binary32. */
  pos[0] = (float)((double)qa_q2_msg_read_short(msg) * (1.0 / 8.0));
  pos[1] = (float)((double)qa_q2_msg_read_short(msg) * (1.0 / 8.0));
  pos[2] = (float)((double)qa_q2_msg_read_short(msg) * (1.0 / 8.0));
}
double qa_q2_msg_read_angle(qa_q2_sizebuf *msg) {
  return (double)qa_q2_msg_read_char(msg) * (360.0 / 256.0);
}
double qa_q2_msg_read_angle16(qa_q2_sizebuf *msg) {
  return qa_q2_short2angle((double)qa_q2_msg_read_short(msg));
}
void qa_q2_msg_read_delta_usercmd(qa_q2_sizebuf *msg,
                                  const qa_q2_wire_usercmd *from,
                                  qa_q2_wire_usercmd *move) {
  move->msec = from->msec;
  move->buttons = from->buttons;
  move->angles[0] = from->angles[0];
  move->angles[1] = from->angles[1];
  move->angles[2] = from->angles[2];
  move->forwardmove = from->forwardmove;
  move->sidemove = from->sidemove;
  move->upmove = from->upmove;
  move->impulse = from->impulse;
  move->lightlevel = from->lightlevel;
  int32_t bits = qa_q2_msg_read_byte(msg);
  if (bits & QA_Q2_CM_ANGLE1) move->angles[0] = (int16_t)qa_q2_msg_read_short(msg);
  if (bits & QA_Q2_CM_ANGLE2) move->angles[1] = (int16_t)qa_q2_msg_read_short(msg);
  if (bits & QA_Q2_CM_ANGLE3) move->angles[2] = (int16_t)qa_q2_msg_read_short(msg);
  if (bits & QA_Q2_CM_FORWARD) move->forwardmove = (double)qa_q2_msg_read_short(msg);
  if (bits & QA_Q2_CM_SIDE) move->sidemove = (double)qa_q2_msg_read_short(msg);
  if (bits & QA_Q2_CM_UP) move->upmove = (double)qa_q2_msg_read_short(msg);
  if (bits & QA_Q2_CM_BUTTONS) move->buttons = (double)qa_q2_msg_read_byte(msg);
  if (bits & QA_Q2_CM_IMPULSE) move->impulse = (double)qa_q2_msg_read_byte(msg);
  move->msec = (double)qa_q2_msg_read_byte(msg);
  move->lightlevel = (double)qa_q2_msg_read_byte(msg);
}
void qa_q2_msg_read_data(qa_q2_sizebuf *msg, uint8_t *data, size_t data_length,
                         int64_t len) {
  for (int64_t i = 0; i < len; i++) {
    uint8_t v = (uint8_t)(qa_q2_msg_read_byte(msg) & 0xff);
    /* TS typed-array stores past the span drop silently. */
    if (i >= 0 && (uint64_t)i < (uint64_t)data_length) data[i] = v;
  }
}

bool qa_q2_message_create(qa_q2_sizebuf *out, int64_t capacity) {
  if (capacity < 0 || capacity > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Invalid Q2 message capacity");
  }
  qa_q2_sizebuf_init_empty(out);
  uint8_t *data = (uint8_t *)calloc(capacity > 0 ? (size_t)capacity : 1, 1);
  if (data == NULL) return false;
  qa_q2_sz_init(out, data, (size_t)capacity, capacity);
  out->owns_data = true;
  return true;
}
void qa_q2_message_destroy(qa_q2_sizebuf *message) {
  if (message->owns_data) free(message->data);
  qa_q2_sizebuf_init_empty(message);
}
void qa_q2_message_load(qa_q2_sizebuf *message, uint8_t *bytes, size_t length) {
  /* TS loadMessage re-inits over the given array (dropping ownership
   * of any createMessage buffer to GC; the C caller destroys first
   * when the previous buffer was owned and still needed). */
  if (message->owns_data && message->data != bytes) free(message->data);
  qa_q2_sz_init(message, bytes, length, (int64_t)length);
  message->cursize = (int64_t)length;
}
uint8_t *qa_q2_message_bytes(const qa_q2_sizebuf *message, size_t *out_length) {
  size_t n = message->cursize > 0 ? (size_t)message->cursize : 0;
  if ((int64_t)n > (int64_t)message->data_length) n = message->data_length;
  uint8_t *out = (uint8_t *)malloc(n > 0 ? n : 1);
  if (out == NULL) return NULL;
  if (n > 0) memcpy(out, message->data, n);
  if (out_length != NULL) *out_length = n;
  return out;
}
void qa_q2_message_check_read(const qa_q2_sizebuf *message) {
  if (message->readcount > message->cursize) {
    qa_q2_com_error_throw(QA_Q2_ERR_DROP, "Truncated Quake II message");
  }
}
