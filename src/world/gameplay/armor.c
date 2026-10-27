/* Port of src/world/gameplay/armor.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/armor.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static double fround_of(double value) { return (double)(float)value; }

qa_armor_result qa_absorb_native_armor(qa_armor_state armor, double damage,
                                       qa_armor_damage_flags flags,
                                       qa_victim_armor_context context) {
  bool stage_is_power = flags.has_stage && str_eq(flags.stage, "power");
  bool stage_is_regular = flags.has_stage && str_eq(flags.stage, "regular");
  if (((!stage_is_power && armor.regular.kind == QA_REGULAR_ARMOR_Q2) ||
       (!stage_is_regular && !str_eq(armor.powered.kind, "none"))) && !context.has_q2)
    qa_error("Q2 victim armor requires an explicit classic or rerelease source profile");
  qa_armor_result result;
  result.armor = armor;
  result.power_saved = 0;
  result.regular_saved = 0;
  if (damage == 0 || flags.no_armor ||
      (armor.regular.kind == QA_REGULAR_ARMOR_NONE && str_eq(armor.powered.kind, "none")))
    return result;
  bool binary32 = str_eq(context.arithmetic, "binary32");
  double protection_scale = flags.has_regular_protection_scale ? flags.regular_protection_scale : 1;
  double power_saved = 0;
  qa_powered_protection_state powered = armor.powered;
  bool rerelease = context.has_q2 && str_eq(context.q2_product, "rerelease");
  double facing_limit = rerelease ? fround_of(0.3) : 0.3;
  if (!stage_is_regular && !flags.no_power_armor && (!rerelease || context.q2_alive) &&
      !str_eq(powered.kind, "none") && powered.cells > 0 &&
      (!str_eq(powered.kind, "screen") || context.screen_facing_dot > facing_limit)) {
    double damage_per_cell = str_eq(powered.kind, "screen") || (context.has_q2 && context.q2_ctf) ? 1 : 2;
    double divided_damage = trunc(str_eq(powered.kind, "screen") ? damage / 3 : (2 * damage) / 3);
    double protected_damage = rerelease ? fmax(1, divided_damage) : divided_damage;
    bool doubled_cost = rerelease ? flags.energy : flags.no_regular_armor;
    double base_available = powered.cells * damage_per_cell;
    double divided_available = doubled_cost ? trunc(base_available / 2) : base_available;
    double available = rerelease ? fmax(1, divided_available) : divided_available;
    power_saved = fmin(available, protected_damage);
    double used = trunc(power_saved / damage_per_cell) * (doubled_cost ? 2 : 1);
    powered.cells = rerelease ? fmax(0, powered.cells - fmax(damage_per_cell, used))
                              : powered.cells - used;
  }
  qa_regular_armor_state regular = armor.regular;
  double regular_saved = 0;
  if (!flags.no_regular_armor && !stage_is_power) {
    /* TS multiply(): binary32 stages each operand and the product. */
    switch (regular.kind) {
      case QA_REGULAR_ARMOR_NONE: break;
      case QA_REGULAR_ARMOR_SOURCE:
        qa_error("Source regular armor requires its original absorption binding");
      case QA_REGULAR_ARMOR_Q1: {
        double inner = binary32 ? fround_of(fround_of(regular.absorption) * fround_of(protection_scale))
                                : regular.absorption * protection_scale;
        double product = binary32 ? fround_of(fround_of(inner) * fround_of(damage - power_saved))
                                  : inner * (damage - power_saved);
        regular_saved = fmin(regular.points, ceil(product));
        double points_before = regular.points;
        regular.points -= regular_saved;
        regular.absorption = regular_saved >= points_before ? 0 : regular.absorption;
        break;
      }
      case QA_REGULAR_ARMOR_Q2: {
        double protection = flags.energy ? regular.energy_protection : regular.normal_protection;
        double inner = binary32 ? fround_of(fround_of(protection) * fround_of(protection_scale))
                                : protection * protection_scale;
        double product = binary32 ? fround_of(fround_of(inner) * fround_of(damage - power_saved))
                                  : inner * (damage - power_saved);
        regular_saved = fmin(regular.points, ceil(product));
        regular.points -= regular_saved;
        break;
      }
      case QA_REGULAR_ARMOR_Q3: {
        double product = fround_of(fround_of(damage - power_saved) *
                                   fround_of(fround_of(regular.protection) * fround_of(protection_scale)));
        regular_saved = fmin(regular.points, ceil(product));
        regular.points -= regular_saved;
        break;
      }
    }
  }
  result.armor.regular = regular;
  result.armor.powered = powered;
  result.power_saved = power_saved;
  result.regular_saved = regular_saved;
  return result;
}

qa_attack_damage_flags qa_attack_damage_flags_of(qa_damage_request request) {
  qa_attack_cause cause = request.attack.cause;
  /* TS bitwise ops coerce to int32. */
  int32_t q2 = cause.kind == QA_ATTACK_CAUSE_Q2 ? (int32_t)cause.damage_flags : 0;
  int32_t q3 = cause.kind == QA_ATTACK_CAUSE_Q3 ? (int32_t)cause.damage_flags : 0;
  qa_attack_damage_flags result;
  memset(&result, 0, sizeof result);
  result.flags.no_armor = ((q2 | q3) & 2) != 0 ||
                          (cause.kind == QA_ATTACK_CAUSE_Q1 && str_eq(cause.armor_effect, "bypass"));
  result.flags.no_power_armor = (q2 & 0x100) != 0;
  result.flags.no_regular_armor = (q2 & 0x80) != 0;
  result.flags.energy = (q2 & 4) != 0;
  result.flags.has_regular_protection_scale = true;
  result.flags.regular_protection_scale =
      cause.kind == QA_ATTACK_CAUSE_Q1 && str_eq(cause.armor_effect, "half-effectiveness") ? 0.5 : 1;
  result.no_knockback = (q2 & 8) != 0 || (q3 & 4) != 0;
  result.no_protection = (q2 & 0x20) != 0 || (q3 & 8) != 0;
  result.no_team_protection = (q3 & 0x10) != 0;
  result.destroy_armor = (q2 & 0x40) != 0;
  return result;
}
