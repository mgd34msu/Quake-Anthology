/* Port of src/compat/q2/classic/combat-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/combat_profile.h"
#include <math.h>
#include <string.h>

#define FA(f) {QA_Q2CA_FIELD, (f), {false, 0, {0}}, NULL, 0, NULL}
#define CALLS_INIT { \
  {"cdecl", qa_compat_q2_xatrix_combat_profile.calls.storage[0], 4}, {"cdecl", qa_compat_q2_xatrix_combat_profile.calls.storage[1], 5}, \
  {"cdecl", qa_compat_q2_xatrix_combat_profile.calls.storage[2], 10}, {"cdecl", qa_compat_q2_xatrix_combat_profile.calls.storage[3], 6}, \
  {"cdecl", qa_compat_q2_xatrix_combat_profile.calls.storage[4], 5}, \
  {{FA(QA_Q2CF_TARGET), FA(QA_Q2CF_ATTACKER), FA(QA_Q2CF_KICK), FA(QA_Q2CF_AMOUNT)}, \
   {FA(QA_Q2CF_TARGET), FA(QA_Q2CF_INFLICTOR), FA(QA_Q2CF_ATTACKER), FA(QA_Q2CF_AMOUNT), FA(QA_Q2CF_POINT)}, \
   {FA(QA_Q2CF_TARGET), FA(QA_Q2CF_INFLICTOR), FA(QA_Q2CF_ATTACKER), FA(QA_Q2CF_DIRECTION), FA(QA_Q2CF_POINT), FA(QA_Q2CF_NORMAL), FA(QA_Q2CF_AMOUNT), FA(QA_Q2CF_KNOCKBACK), FA(QA_Q2CF_FLAGS), FA(QA_Q2CF_CAUSE)}, \
   {FA(QA_Q2CF_TARGET), FA(QA_Q2CF_POINT), FA(QA_Q2CF_NORMAL), FA(QA_Q2CF_AMOUNT), FA(QA_Q2CF_SPARKS), FA(QA_Q2CF_FLAGS)}, \
   {FA(QA_Q2CF_TARGET), FA(QA_Q2CF_POINT), FA(QA_Q2CF_NORMAL), FA(QA_Q2CF_AMOUNT), FA(QA_Q2CF_FLAGS)}}}
static const double armor_regular[] = {3, 2, 1};
const qa_compat_q2_classic_combat_profile qa_compat_q2_xatrix_combat_profile = {
  CALLS_INIT,
  "sha256:8187df3fd5b4d435d8227434d3351aad2b47e546236403e52adcd4d275810c45", "xatrix", 896,
  {480, 512, 264, 400, 376, 452, 456},
  {740, 256, 1776, 3728, 188, 512, 3652},
  {0x5050, 0x5580, 0x5760, 0x19090, 0x19140},
  {0x76800, 0x4b828, 76},
  {3, 2, 1, 5, 6, 23, 12},
  {0, 64}, {8, 12},
  {armor_regular, 3, 1},
  {16, 32, 2048, 4096}, {64, 128}
};
/* The 1998 profile shares the same stock call shapes (storage of the
 * first profile is referenced; calls are equal by value as in TS). */
const qa_compat_q2_classic_combat_profile qa_compat_q2_xatrix1998_combat_profile = {
  CALLS_INIT,
  "sha256:0b0d119ea9ed1abbce02f32fe1243bcb69457d10a34a49b775c473210bcb65a3", "xatrix", 896,
  {480, 512, 264, 400, 376, 452, 456},
  {740, 256, 1776, 3716, 188, 512, 3460},
  {0x48b0, 0x4de0, 0x4fc0, 0x186a0, 0x18750},
  {0x73fe0, 0x497c8, 72},
  {3, 2, 1, 5, 6, 23, 12},
  {0, 60}, {8, 12},
  {armor_regular, 3, 1},
  {16, 32, 2048, 4096}, {64, 128}
};
static bool safe_int(double v) { return v == trunc(v) && fabs(v) <= 9007199254740991.0; }
static void scalar(double value, double length) {
  if (!safe_int(value) || value < 0 || value + length > 4294967296.0) qa_range_error("Classic combat source field exceeds its address range");
}
void qa_compat_q2_validate_classic_combat_profile(const qa_compat_q2_classic_combat_profile *p) {
  qa_native_call_abi abi = qa_compat_q2_classic_abi;
  qa_compat_q2_validate_native_combat_call(&p->calls.pain, QA_Q2CO_PAIN, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.death, QA_Q2CO_DEATH, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.damage, QA_Q2CO_DAMAGE, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.regular_armor, QA_Q2CO_REGULAR_ARMOR, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.power_armor, QA_Q2CO_POWER_ARMOR, abi);
  scalar(p->entity_bytes, 0); scalar(p->globals.item_bytes, 0);
  if (p->entity_bytes < 260 || p->globals.item_bytes < 4 || !safe_int(p->client.inventory_count) || p->client.inventory_count < 1)
    qa_range_error("Classic combat source record sizes are invalid");
  const struct { const char *name; double off; } fields[] = {
    {"health", p->fields.health}, {"damageable", p->fields.damageable}, {"flags", p->fields.flags}, {"mass", p->fields.mass},
    {"velocity", p->fields.velocity}, {"pain", p->fields.pain}, {"die", p->fields.die}};
  for (size_t i = 0; i < 7; i++) {
    scalar(fields[i].off, 4);
    if ((int64_t)fields[i].off % 4 != 0 || fields[i].off + (strcmp(fields[i].name, "velocity") == 0 ? 12 : 4) > p->entity_bytes)
      qa_range_error("Classic combat field is outside its source edict");
  }
  const struct { const char *name; double off; } clients[] = {
    {"inventory", p->client.inventory}, {"maxGrenades", p->client.max_grenades}, {"invincibleFrame", p->client.invincible_frame},
    {"userinfo", p->client.userinfo}, {"viewAngles", p->client.view_angles}};
  for (size_t i = 0; i < 5; i++) {
    scalar(clients[i].off, 4);
    if (strcmp(clients[i].name, "userinfo") != 0 && (int64_t)clients[i].off % 4 != 0) qa_range_error("Classic combat client field is unaligned");
  }
  if (!safe_int(p->client.userinfo_bytes) || p->client.userinfo_bytes < 1) qa_range_error("Classic combat userinfo requires a bounded source string");
  scalar(p->client.userinfo, p->client.userinfo_bytes); scalar(p->client.inventory, p->client.inventory_count * 4);
  const double entries[] = {p->entries.damage, p->entries.power_armor, p->entries.regular_armor, p->entries.spawn, p->entries.free, p->globals.level_frame, p->globals.item_list};
  for (size_t i = 0; i < 7; i++) scalar(entries[i], 4);
  const double item_fields[] = {p->item_fields.class_name, p->item_fields.armor_info};
  for (size_t i = 0; i < 2; i++) { scalar(item_fields[i], 4); if ((int64_t)item_fields[i] % 4 != 0 || item_fields[i] + 4 > p->globals.item_bytes) qa_range_error("Classic armor item field exceeds its original item record"); }
  const double armor_info[] = {p->armor_info.normal_protection, p->armor_info.energy_protection};
  for (size_t i = 0; i < 2; i++) { scalar(armor_info[i], 4); if ((int64_t)armor_info[i] % 4 != 0) qa_range_error("Classic armor information is unaligned"); }
  if (p->armor_info.normal_protection == p->armor_info.energy_protection || p->item_fields.class_name == p->item_fields.armor_info)
    qa_range_error("Classic armor fields overlap");
  bool has_empty = false;
  for (size_t i = 0; i < p->armor.regular_count; i++) {
    if (p->armor.regular[i] == p->armor.empty) has_empty = true;
    for (size_t j = i + 1; j < p->armor.regular_count; j++) if (p->armor.regular[i] == p->armor.regular[j]) qa_range_error("Classic regular armor requires distinct source priorities and an admitted empty tier");
  }
  if (p->armor.regular_count == 0 || !has_empty) qa_range_error("Classic regular armor requires distinct source priorities and an admitted empty tier");
  const double items[] = {p->items.jacket, p->items.combat, p->items.body, p->items.screen, p->items.shield, p->items.cells, p->items.grenades};
  for (size_t i = 0; i < 7; i++) if (!safe_int(items[i]) || items[i] < 1 || items[i] >= p->client.inventory_count) qa_range_error("Classic combat inventory index exceeds its declared source storage");
  for (size_t i = 0; i < p->armor.regular_count; i++) if (!safe_int(p->armor.regular[i]) || p->armor.regular[i] < 1 || p->armor.regular[i] >= p->client.inventory_count) qa_range_error("Classic combat inventory index exceeds its declared source storage");
  const double masks[] = {p->flags.invulnerable, p->flags.notarget, p->flags.no_knockback, p->flags.power_armor, p->teams.model, p->teams.skin};
  for (size_t i = 0; i < 6; i++) if (masks[i] != trunc(masks[i]) || masks[i] < 0 || masks[i] > 4294967295.0) qa_range_error("Classic combat mask exceeds its uint32 source value");
}
const qa_compat_q2_classic_combat_profile *qa_compat_q2_classic_get_combat_profile(qa_content_digest digest) {
  if (digest && strcmp(digest, qa_compat_q2_xatrix_combat_profile.digest) == 0) return &qa_compat_q2_xatrix_combat_profile;
  if (digest && strcmp(digest, qa_compat_q2_xatrix1998_combat_profile.digest) == 0) return &qa_compat_q2_xatrix1998_combat_profile;
  return NULL;
}
