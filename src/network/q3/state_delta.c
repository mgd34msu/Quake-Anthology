/* Port of src/network/q3/state-delta.ts. GPL-2.0-or-later. */
#include "qa/network/q3/state_delta.h"
#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define FLOAT_INT_BITS 13
#define FLOAT_INT_BIAS 4096
/* Field descriptors: bits==0 -> float (double storage), else int32 storage at offset.
 * Player event slots use kind 2 with offset of array and index. */
typedef struct { const char *name; int bits; size_t offset; int index; } field_t;
#define EI(f, b) { #f, (b), offsetof(qa_q3_entity_state_record, f), 0 }
#define EV(f, c, n) { (n), 0, offsetof(qa_q3_entity_state_record, f) + offsetof(qa_vec3, c), 0 }
#define ET(traj, f, b, n) { (n), (b), offsetof(qa_q3_entity_state_record, traj) + offsetof(qa_q3_wire_trajectory, f), 0 }
#define ETV(traj, v, c, n) { (n), 0, offsetof(qa_q3_entity_state_record, traj) + offsetof(qa_q3_wire_trajectory, v) + offsetof(qa_vec3, c), 0 }
static const field_t ENTITY_FIELDS[] = {
  ET(pos, time, 32, "pos.trTime"),
  ETV(pos, base, x, "pos.trBase[0]"), ETV(pos, base, y, "pos.trBase[1]"),
  ETV(pos, delta, x, "pos.trDelta[0]"), ETV(pos, delta, y, "pos.trDelta[1]"),
  ETV(pos, base, z, "pos.trBase[2]"), ETV(apos, base, y, "apos.trBase[1]"),
  ETV(pos, delta, z, "pos.trDelta[2]"), ETV(apos, base, x, "apos.trBase[0]"),
  EI(event, 10), EV(angles2, y, "angles2[1]"), { "eType", 8, offsetof(qa_q3_entity_state_record, e_type), 0 },
  { "torsoAnim", 8, offsetof(qa_q3_entity_state_record, torso_anim), 0 },
  { "eventParm", 8, offsetof(qa_q3_entity_state_record, event_parm), 0 },
  { "legsAnim", 8, offsetof(qa_q3_entity_state_record, legs_anim), 0 },
  { "groundEntityNum", 10, offsetof(qa_q3_entity_state_record, ground_entity_num), 0 },
  ET(pos, type, 8, "pos.trType"),
  { "eFlags", 19, offsetof(qa_q3_entity_state_record, e_flags), 0 },
  { "otherEntityNum", 10, offsetof(qa_q3_entity_state_record, other_entity_num), 0 },
  EI(weapon, 8),
  { "clientNum", 8, offsetof(qa_q3_entity_state_record, client_num), 0 },
  EV(angles, y, "angles[1]"), ET(pos, duration, 32, "pos.trDuration"),
  ET(apos, type, 8, "apos.trType"), EV(origin, x, "origin[0]"),
  EV(origin, y, "origin[1]"), EV(origin, z, "origin[2]"), EI(solid, 24),
  EI(powerups, 16), EI(modelindex, 8),
  { "otherEntityNum2", 10, offsetof(qa_q3_entity_state_record, other_entity_num2), 0 },
  { "loopSound", 8, offsetof(qa_q3_entity_state_record, loop_sound), 0 },
  EI(generic1, 8), EV(origin2, z, "origin2[2]"),
  EV(origin2, x, "origin2[0]"), EV(origin2, y, "origin2[1]"), EI(modelindex2, 8),
  EV(angles, x, "angles[0]"), EI(time, 32), ET(apos, time, 32, "apos.trTime"),
  ET(apos, duration, 32, "apos.trDuration"), ETV(apos, base, z, "apos.trBase[2]"),
  ETV(apos, delta, x, "apos.trDelta[0]"), ETV(apos, delta, y, "apos.trDelta[1]"),
  ETV(apos, delta, z, "apos.trDelta[2]"), EI(time2, 32), EV(angles, z, "angles[2]"),
  EV(angles2, x, "angles2[0]"), EV(angles2, z, "angles2[2]"),
  { "constantLight", 32, offsetof(qa_q3_entity_state_record, constant_light), 0 }, EI(frame, 16),
};
#define N_EF (sizeof ENTITY_FIELDS / sizeof ENTITY_FIELDS[0])
#define PI(f, b, n) { (n), (b), offsetof(qa_q3_player_state_record, f), 0 }
#define PV(f, c, b, n) { (n), (b), offsetof(qa_q3_player_state_record, f) + offsetof(qa_vec3, c), 0 }
static const field_t PLAYER_FIELDS[] = {
  PI(command_time, 32, "commandTime"), PV(origin, x, 0, "origin[0]"), PV(origin, y, 0, "origin[1]"),
  PI(bob_cycle, 8, "bobCycle"), PV(velocity, x, 0, "velocity[0]"), PV(velocity, y, 0, "velocity[1]"),
  PV(viewangles, y, 0, "viewangles[1]"), PV(viewangles, x, 0, "viewangles[0]"), PI(weapon_time, -16, "weaponTime"),
  PV(origin, z, 0, "origin[2]"), PV(velocity, z, 0, "velocity[2]"), PI(legs_timer, 8, "legsTimer"),
  PI(pm_time, -16, "pm_time"), PI(event_sequence, 16, "eventSequence"), PI(torso_anim, 8, "torsoAnim"),
  PI(movement_dir, 4, "movementDir"), { "events[0]", 8, offsetof(qa_q3_player_state_record, events), 0 },
  PI(legs_anim, 8, "legsAnim"), { "events[1]", 8, offsetof(qa_q3_player_state_record, events), 1 },
  PI(pm_flags, 16, "pm_flags"), PI(ground_entity_num, 10, "groundEntityNum"),
  PI(weapon_state, 4, "weaponstate"),
  PI(e_flags, 16, "eFlags"), PI(external_event, 10, "externalEvent"), PI(gravity, 16, "gravity"),
  PI(speed, 16, "speed"), PV(delta_angles, y, 16, "delta_angles[1]"), PI(external_event_parm, 8, "externalEventParm"),
  PI(viewheight, -8, "viewheight"), PI(damage_event, 8, "damageEvent"), PI(damage_yaw, 8, "damageYaw"),
  PI(damage_pitch, 8, "damagePitch"), PI(damage_count, 8, "damageCount"), PI(generic1, 8, "generic1"),
  PI(pm_type, 8, "pm_type"),
  PV(delta_angles, x, 16, "delta_angles[0]"), PV(delta_angles, z, 16, "delta_angles[2]"), PI(torso_timer, 12, "torsoTimer"),
  { "eventParms[0]", 8, offsetof(qa_q3_player_state_record, event_parms), 0 },
  { "eventParms[1]", 8, offsetof(qa_q3_player_state_record, event_parms), 1 }, PI(client_num, 8, "clientNum"),
  PI(weapon, 5, "weapon"),
  PV(viewangles, z, 0, "viewangles[2]"), PV(grapple_point, x, 0, "grapplePoint[0]"), PV(grapple_point, y, 0, "grapplePoint[1]"),
  PV(grapple_point, z, 0, "grapplePoint[2]"), PI(jumppad_ent, 10, "jumppad_ent"), PI(loop_sound, 16, "loopSound"),
};
#define N_PF (sizeof PLAYER_FIELDS / sizeof PLAYER_FIELDS[0])
/* Player delta_angles entries have bits=16 but Vec3 double storage: TS playerVector
 * passes bits through, so get returns the double and rawValue does |0. Our float/int
 * split by storage: fields with bits!=0 read storage as double->int for vec offsets.
 * Distinguish by a parallel marker: delta_angles offsets fall in that member range. */
static bool player_float_int(const field_t *f) {
  size_t base = offsetof(qa_q3_player_state_record, delta_angles);
  return f->offset >= base && f->offset < base + sizeof(qa_vec3);
}
static double fget_d(const void *s, const field_t *f) { double v; memcpy(&v, (const char *)s + f->offset, sizeof v); return v; }
static int32_t fget_i(const void *s, const field_t *f) {
  int32_t v; memcpy(&v, (const char *)s + f->offset + (size_t)f->index * sizeof(int32_t), sizeof v); return v;
}
static void fset_d(void *s, const field_t *f, double v) { memcpy((char *)s + f->offset, &v, sizeof v); }
static void fset_i(void *s, const field_t *f, int32_t v) { memcpy((char *)s + f->offset + (size_t)f->index * sizeof(int32_t), &v, sizeof v); }
static int32_t raw_value(const field_t *f, const void *s, bool player) {
  if (f->bits == 0) return (int32_t)qa_float32_to_bits(fget_d(s, f));
  if (player && player_float_int(f)) return (int32_t)fget_d(s, f); /* TS value|0 */
  return fget_i(s, f);
}
static void field_set(const field_t *f, void *s, double value, bool player) {
  if (f->bits == 0) fset_d(s, f, value);
  else if (player && player_float_int(f)) fset_d(s, f, (int32_t)value);
  else fset_i(s, f, (int32_t)value);
}
static double field_get(const field_t *f, const void *s, bool player) {
  if (f->bits == 0) return fget_d(s, f);
  if (player && player_float_int(f)) return fget_d(s, f);
  return fget_i(s, f);
}
static int last_changed(const field_t *fields, size_t n, const void *from, const void *to, bool player) {
  int last = 0;
  for (size_t i = 0; i < n; i++) if (raw_value(&fields[i], from, player) != raw_value(&fields[i], to, player)) last = (int)i + 1;
  return last;
}
static void write_field(qa_q3_message_writer *w, const field_t *f, const void *from, const void *to, bool entity, bool player) {
  bool changed = raw_value(f, from, player) != raw_value(f, to, player);
  qa_q3_message_writer_write_bits(w, changed ? 1 : 0, 1);
  if (!changed) return;
  double value = f->bits == 0 ? (float)field_get(f, to, player) : (int32_t)field_get(f, to, player);
  if (entity) {
    qa_q3_message_writer_write_bits(w, value == 0 ? 0 : 1, 1);
    if (value == 0) {
      qa_q3_source_message_state *ss = qa_q3_message_writer_source_state(w);
      if (f->bits == 0 && ss) qa_q3_source_message_state_add_oldsize(ss, FLOAT_INT_BITS);
      return;
    }
  }
  if (f->bits != 0) { qa_q3_message_writer_write_bits(w, (int32_t)value, f->bits); return; }
  bool compact = value == (int32_t)value && value >= -FLOAT_INT_BIAS && value < FLOAT_INT_BIAS;
  qa_q3_message_writer_write_bits(w, compact ? 0 : 1, 1);
  if (compact) qa_q3_message_writer_write_bits(w, (int32_t)value + FLOAT_INT_BIAS, FLOAT_INT_BITS);
  else qa_q3_message_writer_write_float(w, value);
}
static int64_t diag_bit_pos(qa_q3_message_reader *r, int64_t offset) {
  int64_t bit = qa_q3_message_reader_bit_position(r) + offset * 8;
  int64_t count = qa_q3_message_reader_read_count(r) + offset;
  return bit == 0 ? count * 8 - QA_Q3_ENTITY_NUMBER_BITS : (count - 1) * 8 + bit - QA_Q3_ENTITY_NUMBER_BITS;
}
static void read_field(qa_q3_message_reader *r, const field_t *f, const void *from, void *state,
                       bool entity, bool player, const qa_q3_delta_message_diagnostics *diag) {
  if (qa_q3_message_reader_read_bits(r, 1) == 0) { field_set(f, state, field_get(f, from, player), player); return; }
  double value; char text[64]; bool has_text = false;
  if (entity && qa_q3_message_reader_read_bits(r, 1) == 0) value = 0;
  else if (f->bits != 0) {
    value = (double)qa_q3_message_reader_read_bits(r, f->bits);
    snprintf(text, sizeof text, "%d", (int32_t)value); has_text = true;
  } else if (qa_q3_message_reader_read_bits(r, 1) == 0) {
    value = (double)qa_q3_message_reader_read_bits(r, FLOAT_INT_BITS) - FLOAT_INT_BIAS;
    snprintf(text, sizeof text, "%lld", (long long)value); has_text = true;
  } else {
    int64_t bits = qa_q3_message_reader_read_bits(r, 32);
    value = qa_bits_to_float32((uint32_t)bits);
    if (diag) { /* TS diagnosticFloat: C-locale %f of the binary32 value */
      snprintf(text, sizeof text, "%f", (double)(float)value); has_text = true;
    }
  }
  field_set(f, state, value, player);
  if (diag && has_text && diag->print) {
    char buf[160]; snprintf(buf, sizeof buf, "%s:%s ", f->name, text);
    diag->print(diag->print_ctx, buf);
  }
}
static void check_entity_number(int64_t n) {
  if (n < 0 || n >= QA_Q3_MAX_ENTITIES) qa_range_error("Bad delta entity number %lld", (long long)n);
}
const qa_q3_state_delta_field *qa_q3_state_delta_fields(int kind, size_t *count) {
  static qa_q3_state_delta_field e_out[N_EF], p_out[N_PF];
  static bool built = false;
  if (!built) {
    for (size_t i = 0; i < N_EF; i++) { e_out[i].name = ENTITY_FIELDS[i].name; e_out[i].bits = ENTITY_FIELDS[i].bits; }
    for (size_t i = 0; i < N_PF; i++) { p_out[i].name = PLAYER_FIELDS[i].name; p_out[i].bits = PLAYER_FIELDS[i].bits; }
    built = true;
  }
  if (kind == 0) { *count = N_EF; return e_out; }
  *count = N_PF; return p_out;
}
void qa_q3_write_delta_entity(qa_q3_message_writer *w, const qa_q3_entity_state_record *from,
                              const qa_q3_entity_state_record *to, bool force) {
  if (!to) {
    if (!from) return;
    check_entity_number(from->number);
    qa_q3_message_writer_write_bits(w, from->number, QA_Q3_ENTITY_NUMBER_BITS);
    qa_q3_message_writer_write_bits(w, 1, 1);
    return;
  }
  check_entity_number(to->number);
  qa_q3_entity_state_record zero; qa_q3_entity_state_record_init(&zero, 0);
  const qa_q3_entity_state_record *base = from ? from : &zero;
  int last = last_changed(ENTITY_FIELDS, N_EF, base, to, false);
  if (last == 0 && !force) return;
  qa_q3_message_writer_write_bits(w, to->number, QA_Q3_ENTITY_NUMBER_BITS);
  qa_q3_message_writer_write_bits(w, 0, 1);
  qa_q3_message_writer_write_bits(w, last == 0 ? 0 : 1, 1);
  if (last == 0) return;
  qa_q3_message_writer_write_byte(w, last);
  qa_q3_source_message_state *ss = qa_q3_message_writer_source_state(w);
  if (ss) qa_q3_source_message_state_add_oldsize(ss, (int64_t)N_EF);
  for (int i = 0; i < last; i++) write_field(w, &ENTITY_FIELDS[i], base, to, true, false);
}
static int64_t shownet_of(const qa_q3_delta_message_diagnostics *d) { return d && d->shownet ? d->shownet(d->shownet_ctx) : 0; }
void qa_q3_read_delta_entity(qa_q3_message_reader *r, const qa_q3_entity_state_record *from, int64_t number,
                             const qa_q3_delta_message_diagnostics *diag, qa_q3_entity_state_record *out) {
  check_entity_number(number);
  int64_t offset = diag ? diag->offset : 0;
  int64_t start_bit = diag_bit_pos(r, offset);
  if (qa_q3_message_reader_read_bits(r, 1) != 0) {
    qa_q3_entity_state_record_init(out, 0);
    out->number = QA_Q3_ENTITYNUM_NONE;
    if (diag && diag->print && (shownet_of(diag) >= 2 || shownet_of(diag) == -1)) {
      char buf[128]; snprintf(buf, sizeof buf, "%3lld: #%-3lld remove\n",
        (long long)(qa_q3_message_reader_read_count(r) + offset), (long long)number);
      diag->print(diag->print_ctx, buf);
    }
    return;
  }
  if (qa_q3_message_reader_read_bits(r, 1) == 0) { *out = *from; out->number = (int32_t)number; return; }
  int64_t last = qa_q3_message_reader_read_byte(r);
  const qa_q3_delta_message_diagnostics *printing =
    (diag && (shownet_of(diag) >= 2 || shownet_of(diag) == -1)) ? diag : NULL;
  if (printing && printing->print) {
    char buf[128]; snprintf(buf, sizeof buf, "%3lld: #%-3d ",
      (long long)(qa_q3_message_reader_read_count(r) + offset), out->number);
    printing->print(printing->print_ctx, buf);
  }
  out->number = (int32_t)number;
  if (last > (int64_t)N_EF)
    qa_binary_error(qa_q3_message_reader_source(r), qa_q3_message_reader_read_count(r), "Invalid entity last field %lld", (long long)last);
  for (int64_t i = 0; i < last; i++) read_field(r, &ENTITY_FIELDS[i], from, out, true, false, printing);
  for (size_t i = (size_t)last; i < N_EF; i++) field_set(&ENTITY_FIELDS[i], out, field_get(&ENTITY_FIELDS[i], from, false), false);
  if (printing && printing->print) {
    char buf[64]; snprintf(buf, sizeof buf, " (%lld bits)\n", (long long)(diag_bit_pos(r, offset) - start_bit));
    printing->print(printing->print_ctx, buf);
  }
}
typedef struct { int kind; int width; const char *name; } slot_field_t;
static const slot_field_t SLOT_FIELDS[4] = {{2,16,"STATS"},{3,16,"PERSISTANT"},{5,16,"AMMO"},{4,32,"POWERUPS"}};
static int slot_mask(const qa_q3_player_state_record *a, const qa_q3_player_state_record *b, int kind) {
  int bits = 0;
  for (int i = 0; i < 16; i++)
    if (qa_q3_player_state_slot_get(a, kind, i) != qa_q3_player_state_slot_get(b, kind, i)) bits |= 1 << i;
  return bits;
}
void qa_q3_write_delta_player_state(qa_q3_message_writer *w, const qa_q3_player_state_record *from,
                                    const qa_q3_player_state_record *to) {
  if (from && from->product != to->product) qa_range_error("Player delta cannot cross products");
  qa_q3_player_state_record zero; qa_q3_player_state_record_init(&zero, to->product, 0, 0, 0);
  const qa_q3_player_state_record *base = from ? from : &zero;
  int last = last_changed(PLAYER_FIELDS, N_PF, base, to, true);
  qa_q3_message_writer_write_byte(w, last);
  qa_q3_source_message_state *ss = qa_q3_message_writer_source_state(w);
  if (ss) qa_q3_source_message_state_add_oldsize(ss, (int64_t)N_PF - last);
  for (int i = 0; i < last; i++) write_field(w, &PLAYER_FIELDS[i], base, to, false, true);
  bool changed = false;
  for (int i = 0; i < 4; i++) if (slot_mask(base, to, SLOT_FIELDS[i].kind) != 0) changed = true;
  qa_q3_message_writer_write_bits(w, changed ? 1 : 0, 1);
  if (!changed) { if (ss) qa_q3_source_message_state_add_oldsize(ss, 4); return; }
  for (int i = 0; i < 4; i++) {
    int mask = slot_mask(base, to, SLOT_FIELDS[i].kind);
    qa_q3_message_writer_write_bits(w, mask == 0 ? 0 : 1, 1);
    if (!mask) continue;
    qa_q3_message_writer_write_short(w, mask);
    for (int j = 0; j < 16; j++)
      if (mask & (1 << j)) qa_q3_message_writer_write_bits(w, qa_q3_player_state_slot_get(to, SLOT_FIELDS[i].kind, j), SLOT_FIELDS[i].width);
  }
}
void qa_q3_read_delta_player_state(qa_q3_message_reader *r, const qa_q3_player_state_record *from, qa_q3_product product,
                                   const qa_q3_delta_message_diagnostics *diag, qa_q3_player_state_record *out) {
  if (from && from->product != product) qa_range_error("Player delta cannot cross products");
  qa_q3_player_state_record zero; qa_q3_player_state_record_init(&zero, product, 0, 0, 0);
  const qa_q3_player_state_record *base = from ? from : &zero;
  *out = *base;
  int64_t offset = diag ? diag->offset : 0;
  int64_t start_bit = diag_bit_pos(r, offset);
  const qa_q3_delta_message_diagnostics *printing =
    (diag && (shownet_of(diag) >= 2 || shownet_of(diag) == -2)) ? diag : NULL;
  if (printing && printing->print) {
    char buf[64]; snprintf(buf, sizeof buf, "%3lld: playerstate ", (long long)(qa_q3_message_reader_read_count(r) + offset));
    printing->print(printing->print_ctx, buf);
  }
  int64_t last = qa_q3_message_reader_read_byte(r);
  if (last > (int64_t)N_PF)
    qa_binary_error(qa_q3_message_reader_source(r), qa_q3_message_reader_read_count(r), "Invalid player last field %lld", (long long)last);
  for (int64_t i = 0; i < last; i++) read_field(r, &PLAYER_FIELDS[i], base, out, false, true, printing);
  for (size_t i = (size_t)last; i < N_PF; i++) field_set(&PLAYER_FIELDS[i], out, field_get(&PLAYER_FIELDS[i], base, true), true);
  if (qa_q3_message_reader_read_bits(r, 1) != 0) {
    for (int i = 0; i < 4; i++) {
      if (qa_q3_message_reader_read_bits(r, 1) == 0) continue;
      if (diag && diag->print && shownet_of(diag) == 4) {
        char buf[32]; snprintf(buf, sizeof buf, "PS_%s ", SLOT_FIELDS[i].name);
        diag->print(diag->print_ctx, buf);
      }
      int64_t mask = qa_q3_message_reader_read_short(r);
      for (int j = 0; j < 16; j++)
        if (mask & (1 << j))
          qa_q3_player_state_slot_set(out, SLOT_FIELDS[i].kind, j,
            (int32_t)(SLOT_FIELDS[i].width == 16 ? qa_q3_message_reader_read_short(r) : qa_q3_message_reader_read_long(r)));
    }
  }
  if (printing && printing->print) {
    char buf[64]; snprintf(buf, sizeof buf, " (%lld bits)\n", (long long)(diag_bit_pos(r, offset) - start_bit));
    printing->print(printing->print_ctx, buf);
  }
}
