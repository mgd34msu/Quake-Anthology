/* Port of src/network/q2/codecs/q2pro-fields.ts. GPL-2.0-or-later. */
#include "qa/network/q2/codecs/q2pro_fields.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/constants.h"

bool qa_q2pro_extensions(qa_q2pro_features features) {
  /* Keep TS operator precedence: (rev>=1024 && flags&8) || (rev>=1025 && flags&16). */
  int32_t flags = (int32_t)features.flags;
  return (features.revision >= 1024 && (flags & 8) != 0) ||
         (features.revision >= 1025 && (flags & 16) != 0);
}
bool qa_q2pro_extensions_v2(qa_q2pro_features features) {
  int32_t flags = (int32_t)features.flags;
  return features.revision >= 1025 && (flags & 16) != 0;
}

int32_t qa_q2pro_read_int23(qa_q2_sizebuf *message, int32_t previous) {
  int32_t word = qa_q2_msg_read_short(message);
  if ((word & 1) != 0) {
    int32_t hi = qa_q2_msg_read_char(message);
    return (((word & 65535) | (hi << 16)) >> 1);
  }
  return previous + (word >> 1);
}

void qa_q2pro_write_int23(qa_q2_sizebuf *message, int32_t current, int32_t previous) {
  if (current < -4194304 || current > 4194303) {
    qa_range_error("Q2PRO coordinate exceeds signed 23-bit range");
  }
  int32_t delta = current - previous;
  if (delta >= -16384 && delta < 16384) {
    qa_q2_msg_write_short(message, (double)(delta << 1));
  } else {
    int32_t value = (current << 1) | 1;
    qa_q2_msg_write_short(message, (double)value);
    qa_q2_msg_write_byte(message, (double)((uint32_t)value >> 16));
  }
}

uint64_t qa_q2pro_read_var64(qa_q2_sizebuf *message) {
  uint64_t result = 0;
  for (int shift = 0; shift < 70; shift += 7) {
    int32_t byte = qa_q2_msg_read_byte(message);
    if (byte < 0 || (shift == 63 && byte > 1)) {
      qa_q2_throw_error("Invalid Q2PRO variable uint64");
    }
    result |= (uint64_t)(byte & 127) << shift;
    if ((byte & 128) == 0) return result;
  }
  qa_q2_throw_error("Unterminated Q2PRO variable uint64");
}

void qa_q2pro_write_var64(qa_q2_sizebuf *message, uint64_t value) {
  /* TS RangeError for value < 0 or > 2^64-1 cannot trigger for uint64. */
  do {
    int32_t byte = (int32_t)(value & 127u);
    value >>= 7;
    qa_q2_msg_write_byte(message, (double)(byte | (value != 0 ? 128 : 0)));
  } while (value != 0);
}

static void read_color(qa_q2_sizebuf *message, double out[3]) {
  out[0] = (double)qa_q2_msg_read_byte(message);
  out[1] = (double)qa_q2_msg_read_byte(message);
  out[2] = (double)qa_q2_msg_read_byte(message);
}
static void write_color(qa_q2_sizebuf *message, const double values[3]) {
  for (int i = 0; i < 3; i++) qa_q2_msg_write_byte(message, values[i]);
}

void qa_q2pro_read_fog(qa_q2_sizebuf *message, const qa_q2_pro_player_fog *from,
                       qa_q2_pro_player_fog *out) {
  int32_t bits = qa_q2_msg_read_byte(message);
  *out = *from;
  if (bits & 1) read_color(message, out->color);
  if (bits & 2) {
    out->density = (double)qa_q2_msg_read_word(message);
    out->sky_factor = (double)qa_q2_msg_read_word(message);
  }
  if (bits & 4) out->height_density = (double)qa_q2_msg_read_word(message);
  if (bits & 8) out->height_falloff = (double)qa_q2_msg_read_word(message);
  if (bits & 16) read_color(message, out->height_start_color);
  if (bits & 32) read_color(message, out->height_end_color);
  if (bits & 64) out->height_start_distance = (double)qa_q2pro_read_int23(message, 0);
  if (bits & 128) out->height_end_distance = (double)qa_q2pro_read_int23(message, 0);
}

static bool triple_differs(const double a[3], const double b[3]) {
  return a[0] != b[0] || a[1] != b[1] || a[2] != b[2];
}

int32_t qa_q2pro_fog_bits(const qa_q2_pro_player_fog *from,
                          const qa_q2_pro_player_fog *to) {
  return (triple_differs(from->color, to->color) ? 1 : 0) |
         (from->density != to->density || from->sky_factor != to->sky_factor ? 2 : 0) |
         (from->height_density != to->height_density ? 4 : 0) |
         (from->height_falloff != to->height_falloff ? 8 : 0) |
         (triple_differs(from->height_start_color, to->height_start_color) ? 16 : 0) |
         (triple_differs(from->height_end_color, to->height_end_color) ? 32 : 0) |
         (from->height_start_distance != to->height_start_distance ? 64 : 0) |
         (from->height_end_distance != to->height_end_distance ? 128 : 0);
}

void qa_q2pro_write_fog(qa_q2_sizebuf *message, int32_t bits,
                        const qa_q2_pro_player_fog *fog) {
  qa_q2_msg_write_byte(message, (double)bits);
  if (bits & 1) write_color(message, fog->color);
  if (bits & 2) {
    qa_q2_msg_write_short(message, fog->density);
    qa_q2_msg_write_short(message, fog->sky_factor);
  }
  if (bits & 4) qa_q2_msg_write_short(message, fog->height_density);
  if (bits & 8) qa_q2_msg_write_short(message, fog->height_falloff);
  if (bits & 16) write_color(message, fog->height_start_color);
  if (bits & 32) write_color(message, fog->height_end_color);
  if (bits & 64) qa_q2pro_write_int23(message, (int32_t)fog->height_start_distance, 0);
  if (bits & 128) qa_q2pro_write_int23(message, (int32_t)fog->height_end_distance, 0);
}

static const uint64_t ORIGIN_BITS[3] = {1ull, 2ull, 512ull};
static const uint64_t ANGLE_BITS[3] = {1024ull, 4ull, 8ull};
static const uint64_t MODEL_BITS[4] = {2048ull, 1048576ull, 2097152ull, 4194304ull};
static const uint64_t NUMBER16 = 256ull;
static const uint64_t ANGLE16 = 8192ull;
static const uint64_t MODEL16 = 268435456ull;
static const uint64_t MORE_FX8 = 536870912ull;
static const uint64_t ALPHA_BIT = 1073741824ull;
static const uint64_t SCALE_BIT = 4294967296ull;
static const uint64_t MORE_FX16 = 8589934592ull;

static uint64_t width_bits(double value, uint64_t byte_bit, uint64_t word_bit) {
  uint32_t v = (uint32_t)(int32_t)value;
  return v < 256u ? byte_bit : v < 65536u ? word_bit : byte_bit | word_bit;
}
static void write_width(qa_q2_sizebuf *message, uint64_t bits, double value,
                        uint64_t byte_bit, uint64_t word_bit) {
  if ((bits & (byte_bit | word_bit)) == (byte_bit | word_bit)) {
    qa_q2_msg_write_long(message, value);
  } else if ((bits & byte_bit) != 0) {
    qa_q2_msg_write_byte(message, value);
  } else if ((bits & word_bit) != 0) {
    qa_q2_msg_write_short(message, value);
  }
}
static double read_width(qa_q2_sizebuf *message, uint64_t bits, double previous,
                         uint64_t byte_bit, uint64_t word_bit) {
  if ((bits & (byte_bit | word_bit)) == (byte_bit | word_bit)) {
    return (double)(uint32_t)qa_q2_msg_read_long(message);
  }
  if ((bits & byte_bit) != 0) return (double)qa_q2_msg_read_byte(message);
  if ((bits & word_bit) != 0) return (double)qa_q2_msg_read_word(message);
  return previous;
}

void qa_q2pro_read_entity_bits(qa_q2_sizebuf *message, qa_q2_entity_bits *out) {
  uint64_t bits = (uint64_t)(int64_t)qa_q2_msg_read_byte(message);
  for (int i = 1; i <= 4; i++) {
    if ((bits & (1ull << (i * 8 - 1))) != 0) {
      bits |= (uint64_t)(int64_t)qa_q2_msg_read_byte(message) << (i * 8);
    }
  }
  out->number = (bits & NUMBER16) != 0 ? (double)qa_q2_msg_read_word(message)
                                        : (double)qa_q2_msg_read_byte(message);
  out->bits = (double)bits;
  out->bits_u64 = bits;
}

void qa_q2pro_write_entity(qa_q2_sizebuf *message, qa_q2pro_features features,
                           const qa_q2_wire_entity *from,
                           const qa_q2_wire_entity *to, bool force,
                           bool new_entity) {
  if (to->number < 1 || to->number > 8191) {
    qa_range_error("Q2PRO entity number outside source range");
  }
  bool extended = qa_q2pro_extensions(features);
  bool v2 = qa_q2pro_extensions_v2(features);
  uint64_t bits = 0;
  for (int i = 0; i < 3; i++) {
    if (from->origin[i] != to->origin[i]) bits |= ORIGIN_BITS[i];
    if (from->angles[i] != to->angles[i]) bits |= ANGLE_BITS[i];
  }
  if ((bits & (1024ull | 4ull | 8ull)) != 0 && features.revision >= 1018) bits |= ANGLE16;
  double old_models[4] = {from->modelindex, from->modelindex2, from->modelindex3,
                          from->modelindex4};
  double models[4] = {to->modelindex, to->modelindex2, to->modelindex3, to->modelindex4};
  for (int i = 0; i < 4; i++) {
    if (old_models[i] != models[i]) {
      bits |= MODEL_BITS[i];
      if (models[i] > 255) bits |= MODEL16;
    }
  }
  if (from->frame != to->frame) bits |= to->frame < 256 ? 16ull : 131072ull;
  if (from->skinnum != to->skinnum) bits |= width_bits(to->skinnum, 65536ull, 33554432ull);
  if (from->effects != to->effects) bits |= width_bits(to->effects, 16384ull, 524288ull);
  if (from->renderfx != to->renderfx) bits |= width_bits(to->renderfx, 4096ull, 262144ull);
  if (from->morefx != to->morefx) bits |= width_bits(to->morefx, MORE_FX8, MORE_FX16);
  if (from->alpha != to->alpha) bits |= ALPHA_BIT;
  if (from->scale != to->scale) bits |= SCALE_BIT;
  if (!extended && (bits & (MODEL16 | MORE_FX8 | MORE_FX16 | ALPHA_BIT | SCALE_BIT)) != 0) {
    qa_q2_throw_error("Q2PRO entity needs negotiated game extensions");
  }
  if (from->solid != to->solid) bits |= 134217728ull;
  if (to->event != 0) bits |= 32ull;
  bool volume_changed = from->loop_volume != to->loop_volume;
  bool attenuation_changed = from->loop_attenuation != to->loop_attenuation;
  if (from->sound != to->sound || volume_changed || attenuation_changed) bits |= 67108864ull;
  if (!extended && (to->sound > 255 || volume_changed || attenuation_changed)) {
    qa_q2_throw_error("Q2PRO looping sound needs negotiated game extensions");
  }
  bool old_origin_differs = from->old_origin[0] != to->old_origin[0] ||
                            from->old_origin[1] != to->old_origin[1] ||
                            from->old_origin[2] != to->old_origin[2];
  if (new_entity || (((int32_t)to->renderfx & 128) != 0 &&
                     (features.revision < 1017 || old_origin_differs))) {
    bits |= 16777216ull;
  }
  if (bits == 0 && !force) return;
  if (to->number >= 256) bits |= NUMBER16;
  for (int i = 4; i >= 1; i--) {
    if ((bits >> (i * 8)) != 0) bits |= 1ull << (i * 8 - 1);
  }
  qa_q2_msg_write_byte(message, (double)(bits & 255ull));
  for (int i = 1; i <= 4; i++) {
    if ((bits & (1ull << (i * 8 - 1))) != 0) {
      qa_q2_msg_write_byte(message, (double)((bits >> (i * 8)) & 255ull));
    }
  }
  if ((bits & NUMBER16) != 0) qa_q2_msg_write_short(message, to->number);
  else qa_q2_msg_write_byte(message, to->number);
  for (int i = 0; i < 4; i++) {
    if ((bits & MODEL_BITS[i]) != 0) {
      if ((bits & MODEL16) != 0) qa_q2_msg_write_short(message, models[i]);
      else qa_q2_msg_write_byte(message, models[i]);
    }
  }
  if ((bits & 16ull) != 0) qa_q2_msg_write_byte(message, to->frame);
  else if ((bits & 131072ull) != 0) qa_q2_msg_write_short(message, to->frame);
  write_width(message, bits, to->skinnum, 65536ull, 33554432ull);
  write_width(message, bits, to->effects, 16384ull, 524288ull);
  write_width(message, bits, to->renderfx, 4096ull, 262144ull);
  for (int i = 0; i < 3; i++) {
    if ((bits & ORIGIN_BITS[i]) != 0) {
      int32_t current = (int32_t)trunc((double)to->origin[i] * 8.0);
      if (v2) {
        qa_q2pro_write_int23(message, current,
                             (int32_t)trunc((double)from->origin[i] * 8.0));
      } else {
        qa_q2_msg_write_short(message, (double)current);
      }
    }
  }
  for (int i = 0; i < 3; i++) {
    if ((bits & ANGLE_BITS[i]) != 0) {
      if ((bits & ANGLE16) != 0) qa_q2_msg_write_angle16(message, (double)to->angles[i]);
      else qa_q2_msg_write_angle(message, (double)to->angles[i]);
    }
  }
  if ((bits & 16777216ull) != 0) {
    for (int i = 0; i < 3; i++) {
      if (v2) qa_q2pro_write_int23(message, (int32_t)trunc((double)to->old_origin[i] * 8.0), 0);
      else qa_q2_msg_write_short(message, trunc((double)to->old_origin[i] * 8.0));
    }
  }
  if ((bits & 67108864ull) != 0) {
    if (extended) {
      int32_t packed = (int32_t)to->sound | (volume_changed ? 16384 : 0) |
                       (attenuation_changed ? 32768 : 0);
      qa_q2_msg_write_short(message, (double)packed);
      if (volume_changed) qa_q2_msg_write_byte(message, trunc(to->loop_volume * 255.0));
      if (attenuation_changed) {
        qa_q2_msg_write_byte(message, to->loop_attenuation == -1
                                          ? 192
                                          : trunc(to->loop_attenuation * 64.0));
      }
    } else {
      qa_q2_msg_write_byte(message, to->sound);
    }
  }
  if ((bits & 32ull) != 0) qa_q2_msg_write_byte(message, to->event);
  if ((bits & 134217728ull) != 0) qa_q2_msg_write_long(message, to->solid);
  write_width(message, bits, to->morefx, MORE_FX8, MORE_FX16);
  if ((bits & ALPHA_BIT) != 0) {
    double v = to->alpha == 0 ? 0 : trunc(to->alpha * 255.0);
    if (to->alpha != 0) {
      if (v < 1) v = 1;
      if (v > 255) v = 255;
    }
    qa_q2_msg_write_byte(message, v);
  }
  if ((bits & SCALE_BIT) != 0) {
    double v = to->scale == 0 ? 0 : trunc(to->scale * 16.0);
    if (to->scale != 0) {
      if (v < 1) v = 1;
      if (v > 255) v = 255;
    }
    qa_q2_msg_write_byte(message, v);
  }
}

void qa_q2pro_read_entity(qa_q2_sizebuf *message, qa_q2pro_features features,
                          const qa_q2_wire_entity *from,
                          qa_q2_wire_entity *to, double number, uint64_t bits) {
  bool extended = qa_q2pro_extensions(features);
  bool v2 = qa_q2pro_extensions_v2(features);
  *to = *from;
  to->origin[0] = from->origin[0];
  to->origin[1] = from->origin[1];
  to->origin[2] = from->origin[2];
  to->angles[0] = from->angles[0];
  to->angles[1] = from->angles[1];
  to->angles[2] = from->angles[2];
  /* TS quirk: to.old_origin.set(from.origin). */
  to->old_origin[0] = from->origin[0];
  to->old_origin[1] = from->origin[1];
  to->old_origin[2] = from->origin[2];
  to->number = number;
  to->event = 0;
  if ((bits & 2048ull) != 0) {
    to->modelindex = extended && (bits & MODEL16) != 0
                         ? (double)qa_q2_msg_read_word(message)
                         : (double)qa_q2_msg_read_byte(message);
  }
  if ((bits & 1048576ull) != 0) {
    to->modelindex2 = extended && (bits & MODEL16) != 0
                          ? (double)qa_q2_msg_read_word(message)
                          : (double)qa_q2_msg_read_byte(message);
  }
  if ((bits & 2097152ull) != 0) {
    to->modelindex3 = extended && (bits & MODEL16) != 0
                          ? (double)qa_q2_msg_read_word(message)
                          : (double)qa_q2_msg_read_byte(message);
  }
  if ((bits & 4194304ull) != 0) {
    to->modelindex4 = extended && (bits & MODEL16) != 0
                          ? (double)qa_q2_msg_read_word(message)
                          : (double)qa_q2_msg_read_byte(message);
  }
  if ((bits & 16ull) != 0) to->frame = (double)qa_q2_msg_read_byte(message);
  else if ((bits & 131072ull) != 0) to->frame = (double)qa_q2_msg_read_word(message);
  to->skinnum = read_width(message, bits, to->skinnum, 65536ull, 33554432ull);
  to->effects = read_width(message, bits, to->effects, 16384ull, 524288ull);
  to->renderfx = read_width(message, bits, to->renderfx, 4096ull, 262144ull);
  for (int i = 0; i < 3; i++) {
    if ((bits & ORIGIN_BITS[i]) != 0) {
      int32_t raw = v2 ? qa_q2pro_read_int23(
                             message, (int32_t)trunc((double)from->origin[i] * 8.0))
                       : qa_q2_msg_read_short(message);
      to->origin[i] = (float)((double)raw / 8.0);
    }
  }
  for (int i = 0; i < 3; i++) {
    if ((bits & ANGLE_BITS[i]) != 0) {
      to->angles[i] = (float)((bits & ANGLE16) != 0 ? qa_q2_msg_read_angle16(message)
                                                     : qa_q2_msg_read_angle(message));
    }
  }
  if ((bits & 16777216ull) != 0) {
    for (int i = 0; i < 3; i++) {
      int32_t raw = v2 ? qa_q2pro_read_int23(message, 0) : qa_q2_msg_read_short(message);
      to->old_origin[i] = (float)((double)raw / 8.0);
    }
  }
  if ((bits & 67108864ull) != 0) {
    if (extended) {
      int32_t sound = qa_q2_msg_read_word(message);
      to->sound = (double)(sound & 16383);
      if ((sound & 16384) != 0) to->loop_volume = (double)qa_q2_msg_read_byte(message) / 255.0;
      if ((sound & 32768) != 0) {
        int32_t value = qa_q2_msg_read_byte(message);
        to->loop_attenuation = value == 192 ? -1 : (double)value / 64.0;
      }
    } else {
      to->sound = (double)qa_q2_msg_read_byte(message);
    }
  }
  if ((bits & 32ull) != 0) to->event = (double)qa_q2_msg_read_byte(message);
  if ((bits & 134217728ull) != 0) to->solid = (double)(uint32_t)qa_q2_msg_read_long(message);
  to->morefx = read_width(message, bits, to->morefx, MORE_FX8, MORE_FX16);
  if ((bits & ALPHA_BIT) != 0) to->alpha = (double)qa_q2_msg_read_byte(message) / 255.0;
  if ((bits & SCALE_BIT) != 0) to->scale = (double)qa_q2_msg_read_byte(message) / 16.0;
}
