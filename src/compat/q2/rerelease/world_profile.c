/* Port of src/compat/q2/rerelease/world-profile.ts. Retail
 * inventory rows transcribed mechanically from the TS classnames
 * and ammoSlots tables. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/world_profile.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_primary_reader.h"
#include "qa/persistence/execution.h"

#define FA(f) {QA_Q2CA_FIELD, (f), {false, NULL, {NULL, 0, 0, 0, NULL, 0}}, NULL, 0, NULL}
static const qa_compat_q2_rerelease_armor_repair retail_repair[] = {
  {{false, NULL, 0xe0, "uint32"}, {true, "rbx", 0, "uint32"}}};
static const qa_item_id retail_regular[] = {"q2:item_armor_jacket", "q2:item_armor_combat", "q2:item_armor_body"};
static const qa_compat_q2_rerelease_speed_load retail_loads[] = {
  {0xe8293, 0}, {0xe889c, 1}, {0xe88ce, 0}, {0xe8b15, 1}, {0xe8b1f, 1}, {0xe9e3e, 10}};
static const qa_compat_q2_rerelease_inventory_row retail_inventory[] = {
  {"q2:none", "index", NULL, 0, false, 0, 0},
  {"q2:item_armor_body", "classname", "item_armor_body", 0, false, 0, 2147483647},
  {"q2:item_armor_combat", "classname", "item_armor_combat", 0, false, 0, 2147483647},
  {"q2:item_armor_jacket", "classname", "item_armor_jacket", 0, false, 0, 2147483647},
  {"q2:item_armor_shard", "classname", "item_armor_shard", 0, false, 0, 2147483647},
  {"q2:item_power_screen", "classname", "item_power_screen", 0, false, 0, 2147483647},
  {"q2:item_power_shield", "classname", "item_power_shield", 0, false, 0, 2147483647},
  {"q2:weapon_grapple", "classname", "weapon_grapple", 0, false, 0, 2147483647},
  {"q2:weapon_blaster", "classname", "weapon_blaster", 0, false, 0, 2147483647},
  {"q2:weapon_chainfist", "classname", "weapon_chainfist", 0, false, 0, 2147483647},
  {"q2:weapon_shotgun", "classname", "weapon_shotgun", 0, false, 0, 2147483647},
  {"q2:weapon_supershotgun", "classname", "weapon_supershotgun", 0, false, 0, 2147483647},
  {"q2:weapon_machinegun", "classname", "weapon_machinegun", 0, false, 0, 2147483647},
  {"q2:weapon_etf_rifle", "classname", "weapon_etf_rifle", 0, false, 0, 2147483647},
  {"q2:weapon_chaingun", "classname", "weapon_chaingun", 0, false, 0, 2147483647},
  {"q2:ammo_grenades", "classname", "ammo_grenades", 0, true, 3, 0},
  {"q2:ammo_trap", "classname", "ammo_trap", 0, true, 7, 0},
  {"q2:ammo_tesla", "classname", "ammo_tesla", 0, true, 9, 0},
  {"q2:weapon_grenadelauncher", "classname", "weapon_grenadelauncher", 0, false, 0, 2147483647},
  {"q2:weapon_proxlauncher", "classname", "weapon_proxlauncher", 0, false, 0, 2147483647},
  {"q2:weapon_rocketlauncher", "classname", "weapon_rocketlauncher", 0, false, 0, 2147483647},
  {"q2:weapon_hyperblaster", "classname", "weapon_hyperblaster", 0, false, 0, 2147483647},
  {"q2:weapon_boomer", "classname", "weapon_boomer", 0, false, 0, 2147483647},
  {"q2:weapon_plasmabeam", "classname", "weapon_plasmabeam", 0, false, 0, 2147483647},
  {"q2:weapon_railgun", "classname", "weapon_railgun", 0, false, 0, 2147483647},
  {"q2:weapon_phalanx", "classname", "weapon_phalanx", 0, false, 0, 2147483647},
  {"q2:weapon_bfg", "classname", "weapon_bfg", 0, false, 0, 2147483647},
  {"q2:weapon_disintegrator", "classname", "weapon_disintegrator", 0, false, 0, 2147483647},
  {"q2:ammo_shells", "classname", "ammo_shells", 0, true, 1, 0},
  {"q2:ammo_bullets", "classname", "ammo_bullets", 0, true, 0, 0},
  {"q2:ammo_cells", "classname", "ammo_cells", 0, true, 4, 0},
  {"q2:ammo_rockets", "classname", "ammo_rockets", 0, true, 2, 0},
  {"q2:ammo_slugs", "classname", "ammo_slugs", 0, true, 5, 0},
  {"q2:ammo_magslug", "classname", "ammo_magslug", 0, true, 6, 0},
  {"q2:ammo_flechettes", "classname", "ammo_flechettes", 0, true, 8, 0},
  {"q2:ammo_prox", "classname", "ammo_prox", 0, true, 11, 0},
  {"q2:ammo_nuke", "classname", "ammo_nuke", 0, false, 0, 2147483647},
  {"q2:ammo_disruptor", "classname", "ammo_disruptor", 0, true, 10, 0},
  {"q2:item_quad", "classname", "item_quad", 0, false, 0, 2147483647},
  {"q2:item_quadfire", "classname", "item_quadfire", 0, false, 0, 2147483647},
  {"q2:item_invulnerability", "classname", "item_invulnerability", 0, false, 0, 2147483647},
  {"q2:item_invisibility", "classname", "item_invisibility", 0, false, 0, 2147483647},
  {"q2:item_silencer", "classname", "item_silencer", 0, false, 0, 2147483647},
  {"q2:item_breather", "classname", "item_breather", 0, false, 0, 2147483647},
  {"q2:item_enviro", "classname", "item_enviro", 0, false, 0, 2147483647},
  {"q2:item_ancient_head", "classname", "item_ancient_head", 0, false, 0, 2147483647},
  {"q2:item_legacy_head", "classname", "item_legacy_head", 0, false, 0, 2147483647},
  {"q2:item_adrenaline", "classname", "item_adrenaline", 0, false, 0, 2147483647},
  {"q2:item_bandolier", "classname", "item_bandolier", 0, false, 0, 2147483647},
  {"q2:item_pack", "classname", "item_pack", 0, false, 0, 2147483647},
  {"q2:item_ir_goggles", "classname", "item_ir_goggles", 0, false, 0, 2147483647},
  {"q2:item_double", "classname", "item_double", 0, false, 0, 2147483647},
  {"q2:item_sphere_vengeance", "classname", "item_sphere_vengeance", 0, false, 0, 2147483647},
  {"q2:item_sphere_hunter", "classname", "item_sphere_hunter", 0, false, 0, 2147483647},
  {"q2:item_sphere_defender", "classname", "item_sphere_defender", 0, false, 0, 2147483647},
  {"q2:item_doppleganger", "classname", "item_doppleganger", 0, false, 0, 2147483647},
  {"q2:key_data_cd", "classname", "key_data_cd", 0, false, 0, 2147483647},
  {"q2:key_power_cube", "classname", "key_power_cube", 0, false, 0, 2147483647},
  {"q2:key_explosive_charges", "classname", "key_explosive_charges", 0, false, 0, 2147483647},
  {"q2:key_yellow_key", "classname", "key_yellow_key", 0, false, 0, 2147483647},
  {"q2:key_power_core", "classname", "key_power_core", 0, false, 0, 2147483647},
  {"q2:key_pyramid", "classname", "key_pyramid", 0, false, 0, 2147483647},
  {"q2:key_data_spinner", "classname", "key_data_spinner", 0, false, 0, 2147483647},
  {"q2:key_pass", "classname", "key_pass", 0, false, 0, 2147483647},
  {"q2:key_blue_key", "classname", "key_blue_key", 0, false, 0, 2147483647},
  {"q2:key_red_key", "classname", "key_red_key", 0, false, 0, 2147483647},
  {"q2:key_green_key", "classname", "key_green_key", 0, false, 0, 2147483647},
  {"q2:key_commander_head", "classname", "key_commander_head", 0, false, 0, 2147483647},
  {"q2:key_airstrike_target", "classname", "key_airstrike_target", 0, false, 0, 2147483647},
  {"q2:key_nuke_container", "classname", "key_nuke_container", 0, false, 0, 2147483647},
  {"q2:key_nuke", "classname", "key_nuke", 0, false, 0, 2147483647},
  {"q2:item_health_small", "classname", "item_health_small", 0, false, 0, 2147483647},
  {"q2:item_health", "classname", "item_health", 0, false, 0, 2147483647},
  {"q2:item_health_large", "classname", "item_health_large", 0, false, 0, 2147483647},
  {"q2:item_health_mega", "classname", "item_health_mega", 0, false, 0, 2147483647},
  {"q2:item_flag_team1", "classname", "item_flag_team1", 0, false, 0, 2147483647},
  {"q2:item_flag_team2", "classname", "item_flag_team2", 0, false, 0, 2147483647},
  {"q2:item_tech1", "classname", "item_tech1", 0, false, 0, 2147483647},
  {"q2:item_tech2", "classname", "item_tech2", 0, false, 0, 2147483647},
  {"q2:item_tech3", "classname", "item_tech3", 0, false, 0, 2147483647},
  {"q2:item_tech4", "classname", "item_tech4", 0, false, 0, 2147483647},
  {"q2:item_flashlight", "classname", "item_flashlight", 0, false, 0, 2147483647},
  {"q2:item_compass", "classname", "item_compass", 0, false, 0, 2147483647},
  {"q2:item_tag_token", "remaining", NULL, 0, false, 0, 2147483647},

};
static qa_compat_q2_rerelease_primary_world_profile retail;
static bool retail_built = false;
static void stock(qa_compat_q2_native_combat_call *call, qa_compat_q2_native_combat_argument *storage,
                  const qa_compat_q2_native_combat_field *fields, size_t count) {
  call->convention = "microsoft-x64"; call->arguments = storage; call->argument_count = count;
  for (size_t i = 0; i < count; i++) storage[i] = (qa_compat_q2_native_combat_argument)FA(fields[i]);
}
static void build_retail(void) {
  if (retail_built) return;
  retail_built = true;
  qa_compat_q2_rerelease_layouts_ensure();
  memset(&retail, 0, sizeof retail);
  const qa_compat_q2_native_combat_field pain[] = {QA_Q2CF_TARGET, QA_Q2CF_ATTACKER, QA_Q2CF_KICK, QA_Q2CF_AMOUNT, QA_Q2CF_CAUSE};
  const qa_compat_q2_native_combat_field death[] = {QA_Q2CF_TARGET, QA_Q2CF_INFLICTOR, QA_Q2CF_ATTACKER, QA_Q2CF_AMOUNT, QA_Q2CF_POINT, QA_Q2CF_CAUSE};
  const qa_compat_q2_native_combat_field dmg[] = {QA_Q2CF_TARGET, QA_Q2CF_INFLICTOR, QA_Q2CF_ATTACKER, QA_Q2CF_DIRECTION, QA_Q2CF_POINT,
                                                 QA_Q2CF_NORMAL, QA_Q2CF_AMOUNT, QA_Q2CF_KNOCKBACK, QA_Q2CF_FLAGS, QA_Q2CF_CAUSE};
  const qa_compat_q2_native_combat_field pow[] = {QA_Q2CF_TARGET, QA_Q2CF_POINT, QA_Q2CF_NORMAL, QA_Q2CF_AMOUNT, QA_Q2CF_FLAGS};
  stock(&retail.calls.pain, retail.calls.storage[0], pain, 5);
  stock(&retail.calls.death, retail.calls.storage[1], death, 6);
  stock(&retail.calls.process_pain, retail.calls.storage[2], pain, 1); /* deferred-reaction: [target] */
  stock(&retail.calls.damage, retail.calls.storage[3], dmg, 10);
  stock(&retail.calls.power_armor, retail.calls.storage[4], pow, 5);
  retail.digest = "sha256:045d49c53722d9b922caf14f168dd28a97d4c514a6e443a3140560f8668baccd";
  retail.edict = qa_compat_q2_rerelease_private_edict_prefix_layout;
  retail.edict.byte_length = 3688;
  retail.client = *qa_compat_q2_rerelease_get_retail_client_profile();
  retail.entries.spawn = 0x964b0; retail.entries.free_ = 0x96600; retail.entries.damage = 0x5cae0;
  retail.entries.power_armor = 0x5c100; retail.entries.process_pain = 0x76e20; retail.entries.time = 0x241b28;
  retail.entries.regular_armor.entry = 0x5d022; retail.entries.regular_armor.join = 0x5d154;
  retail.regular_armor.target = (qa_compat_q2_rerelease_world_location){true, "rdi", 0, "pointer"};
  retail.regular_armor.amount = (qa_compat_q2_rerelease_world_location){true, "r14", 0, "int32"};
  retail.regular_armor.point = (qa_compat_q2_rerelease_world_location){true, "r13", 0, "pointer"};
  retail.regular_armor.normal = (qa_compat_q2_rerelease_world_location){false, NULL, 0x108, "pointer"};
  retail.regular_armor.flags = (qa_compat_q2_rerelease_world_location){false, NULL, 0x120, "int32"};
  retail.regular_armor.result = (qa_compat_q2_rerelease_world_location){true, "r12", 0, "int32"};
  retail.regular_armor.repair = retail_repair; retail.regular_armor.repair_count = 1;
  retail.monster.attacker = 3120; retail.monster.inflictor = 3128; retail.monster.blood = 3136;
  retail.monster.knockback = 3140; retail.monster.point = 3144; retail.monster.mod = 3156; retail.monster.invincible_time = 0xb88;
  retail.inventory = retail_inventory; retail.inventory_count = sizeof retail_inventory / sizeof retail_inventory[0];
  retail.armor.table = 0x1953a8; retail.armor.stride = 192; retail.armor.normal = 8; retail.armor.energy = 12;
  retail.armor.regular = retail_regular; retail.armor.regular_count = 3;
  retail.armor.empty = "q2:item_armor_body"; retail.armor.screen = "q2:item_power_screen";
  retail.armor.shield = "q2:item_power_shield"; retail.armor.cells = "q2:ammo_cells"; retail.armor.cells_index = 30;
  retail.flags.godmode = 16; retail.flags.notarget = 32; retail.flags.no_knockback = 2048; retail.flags.power_armor = 4096;
  retail.movement.has_body = true;
  retail.movement.body.dimensions = 0xe9ff0; retail.movement.body.trace = 0xe71a0; retail.movement.body.movement_global = 0x23c9c8;
  retail.movement.game_api = 0x6bcd0; retail.movement.pmove = 0xea560;
  retail.movement.speed_loads = retail_loads; retail.movement.speed_load_count = 6;
}
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_get_retail_world_profile(void) { build_retail(); return &retail; }
const qa_compat_q2_rerelease_primary_world_profile *qa_compat_q2_rerelease_get_primary_world_profile(qa_content_digest digest) {
  build_retail(); return digest && strcmp(digest, retail.digest) == 0 ? &retail : NULL;
}
static void range(double value, double min, double max) {
  if (value != trunc(value) || fabs(value) > 9007199254740991.0 || value < min || value > max)
    qa_range_error("Source world metadata is outside its declared range");
}
static double storage_bytes(const char *storage) {
  if (!strcmp(storage, "int8") || !strcmp(storage, "uint8")) return 1;
  if (!strcmp(storage, "int16") || !strcmp(storage, "uint16")) return 2;
  if (!strcmp(storage, "int32") || !strcmp(storage, "uint32") || !strcmp(storage, "float32")) return 4;
  return 8;
}
static const qa_guest_field_layout *find_field(const qa_guest_layout *layout, const char *name) {
  for (size_t i = 0; i < layout->field_count; i++) if (strcmp(layout->fields[i].name, name) == 0) return &layout->fields[i];
  return NULL;
}
static void validate_layout(const qa_guest_layout *layout, const qa_guest_layout *public_layout) {
  /* validateValueLayout(aggregate, 8): pointer bytes domain. */
  if (layout->pointer_bytes != 8) qa_range_error("Rerelease layout must use 8-byte pointers");
  for (size_t i = 0; i < layout->field_count; i++) {
    const qa_guest_field_layout *f = &layout->fields[i];
    double end = f->byte_offset + storage_bytes(f->storage) * f->count;
    if (f->count <= 0) qa_range_error("Overlapping or empty native source fields");
    for (size_t j = 0; j < i; j++) {
      const qa_guest_field_layout *g = &layout->fields[j];
      double gend = g->byte_offset + storage_bytes(g->storage) * g->count;
      if (strcmp(f->name, g->name) == 0 || (f->byte_offset < gend && end > g->byte_offset))
        qa_range_error("Overlapping or empty native source fields");
    }
  }
  for (size_t i = 0; i < public_layout->field_count; i++) {
    const qa_guest_field_layout *expected = &public_layout->fields[i];
    char buf[512]; snprintf(buf, sizeof buf, "shared.%s", expected->name);
    const qa_guest_field_layout *actual = find_field(layout, buf);
    if (!actual || actual->byte_offset != expected->byte_offset || strcmp(actual->storage, expected->storage) != 0 || actual->count != expected->count)
      qa_range_error("Source world layout changes the public API2023 prefix");
  }
}
void qa_compat_q2_validate_rerelease_primary_world_profile(const qa_compat_q2_rerelease_primary_world_profile *p, qa_content_digest digest) {
  qa_compat_q2_rerelease_layouts_ensure();
  qa_native_call_abi abi = qa_compat_q2_rerelease_abi;
  qa_compat_q2_validate_native_combat_call(&p->calls.pain, QA_Q2CO_PAIN, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.death, QA_Q2CO_DEATH, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.process_pain, QA_Q2CO_DEFERRED, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.damage, QA_Q2CO_DAMAGE, abi);
  qa_compat_q2_validate_native_combat_call(&p->calls.power_armor, QA_Q2CO_POWER_ARMOR, abi);
  if (!p->digest || !digest || strcmp(p->digest, digest) != 0 || !p->client.authority_is_artifact ||
      !p->client.digest || strcmp(p->client.digest, digest) != 0)
    qa_range_error("Rerelease source world profile belongs to another artifact");
  const double rvas[] = {p->entries.spawn, p->entries.free_, p->entries.damage, p->entries.power_armor, p->entries.process_pain,
                         p->entries.time, p->entries.regular_armor.entry, p->entries.regular_armor.join, p->armor.table, p->movement.game_api, p->movement.pmove};
  for (size_t i = 0; i < 11; i++) range(rvas[i], 1, 4294967295.0);
  range(p->client.inventory_count, 1, 65536); range(p->client.ammo_count, 1, 65536);
  range(p->armor.stride, 8, 65536); range(p->armor.normal, 0, 65532); range(p->armor.energy, 0, 65532);
  range(p->armor.cells_index, 0, p->client.inventory_count - 1);
  const double flag_values[] = {p->flags.godmode, p->flags.notarget, p->flags.no_knockback, p->flags.power_armor};
  for (size_t i = 0; i < 4; i++) range(flag_values[i], 1, 9007199254740991.0);
  if (p->movement.has_body) { range(p->movement.body.dimensions, 1, 4294967295.0); range(p->movement.body.trace, 1, 4294967295.0); range(p->movement.body.movement_global, 1, 4294967295.0); }
  for (size_t i = 0; i < p->movement.speed_load_count; i++) {
    range(p->movement.speed_loads[i].next, 1, 4294967295.0); range(p->movement.speed_loads[i].reg, 0, 15);
    for (size_t j = 0; j < i; j++) if (p->movement.speed_loads[j].next == p->movement.speed_loads[i].next)
      qa_range_error("Repeated source movement load would apply equipment twice");
  }
  const qa_compat_q2_rerelease_world_location *locs[] = {&p->regular_armor.target, &p->regular_armor.amount, &p->regular_armor.point,
                                                         &p->regular_armor.normal, &p->regular_armor.flags, &p->regular_armor.result};
  for (size_t i = 0; i < 6; i++) if (!locs[i]->is_register) range(locs[i]->offset, 0, 1048576 - storage_bytes(locs[i]->storage));
  for (size_t i = 0; i < p->regular_armor.repair_count; i++) {
    if (!p->regular_armor.repair[i].source.is_register) range(p->regular_armor.repair[i].source.offset, 0, 1048576 - storage_bytes(p->regular_armor.repair[i].source.storage));
    if (!p->regular_armor.repair[i].target.is_register) range(p->regular_armor.repair[i].target.offset, 0, 1048576 - storage_bytes(p->regular_armor.repair[i].target.storage));
  }
  validate_layout(&p->edict, &qa_compat_q2_rerelease_edict_layout);
  validate_layout(&p->client.layout, &qa_compat_q2_rerelease_client_layout);
  for (size_t i = 0; i < qa_compat_q2_rerelease_private_edict_prefix_layout.field_count; i++) {
    const qa_guest_field_layout *expected = &qa_compat_q2_rerelease_private_edict_prefix_layout.fields[i];
    if (strncmp(expected->name, "shared.", 7) == 0) continue;
    const qa_guest_field_layout *field = find_field(&p->edict, expected->name);
    if (!field || strcmp(field->storage, expected->storage) != 0 || field->count != expected->count)
      qa_range_error("Missing typed source edict field %s", expected->name);
  }
  const struct { const char *name; const char *storage; double count; } required[] = {
    {"pers.inventory", "int32", p->client.inventory_count}, {"pers.max_ammo", "int16", p->client.ammo_count},
    {"v_angle", "float32", 3}, {"invincible_time", "int64", 1}};
  for (size_t i = 0; i < 4; i++) {
    const qa_guest_field_layout *field = find_field(&p->client.layout, required[i].name);
    if (!field || strcmp(field->storage, required[i].storage) != 0 || field->count != required[i].count)
      qa_range_error("Missing typed source client field %s", required[i].name);
  }
  const double monster[] = {p->monster.attacker, p->monster.inflictor, p->monster.blood, p->monster.knockback,
                            p->monster.point, p->monster.mod, p->monster.invincible_time};
  const double monster_bytes[] = {8, 8, 4, 4, 12, 3, 8};
  for (size_t i = 0; i < 7; i++)
    if (monster[i] != trunc(monster[i]) || fabs(monster[i]) > 9007199254740991.0 || monster[i] < 0 || monster[i] + monster_bytes[i] > p->edict.byte_length)
      qa_range_error("Source monster accumulator exceeds its edict");
  if (strcmp(p->regular_armor.target.storage, "pointer") != 0 || strcmp(p->regular_armor.point.storage, "pointer") != 0 ||
      strcmp(p->regular_armor.normal.storage, "pointer") != 0)
    qa_range_error("Source armor geometry requires pointer locations");
  if (strcmp(p->regular_armor.amount.storage, "int32") != 0 || strcmp(p->regular_armor.flags.storage, "int32") != 0 ||
      strcmp(p->regular_armor.result.storage, "int32") != 0)
    qa_range_error("Source armor values require int32 locations");
  for (size_t i = 0; i < p->regular_armor.repair_count; i++)
    if (strcmp(p->regular_armor.repair[i].source.storage, p->regular_armor.repair[i].target.storage) != 0)
      qa_range_error("Source armor repair changes scalar representation");
  if (p->entries.regular_armor.entry == p->entries.regular_armor.join) qa_range_error("Source armor region is empty");
  size_t remaining = 0;
  for (size_t i = 0; i < p->inventory_count; i++) {
    if (strcmp(p->inventory[i].source_kind, "remaining") == 0) remaining++;
    for (size_t j = 0; j < i; j++) if (strcmp(p->inventory[i].item, p->inventory[j].item) == 0)
      qa_range_error("Source inventory requires one complete unique roster");
  }
  if ((double)p->inventory_count != p->client.inventory_count || remaining > 1)
    qa_range_error("Source inventory requires one complete unique roster");
  for (size_t i = 0; i < p->inventory_count; i++) {
    const qa_compat_q2_rerelease_inventory_row *row = &p->inventory[i];
    if (strcmp(row->source_kind, "index") == 0) range(row->source_index, 0, p->client.inventory_count - 1);
    if (row->capacity_is_ammo) range(row->capacity_source_index, 0, p->client.ammo_count - 1);
    else range(row->capacity_count, 0, 2147483647.0);
    if ((strcmp(row->source_kind, "index") == 0 && row->source_index >= p->client.inventory_count) ||
        (strcmp(row->source_kind, "classname") == 0 && (!row->source_name || row->source_name[0] == 0)) ||
        (row->capacity_is_ammo && row->capacity_source_index >= p->client.ammo_count))
      qa_range_error("Source inventory mapping exceeds its private storage");
  }
  bool has_empty = false;
  for (size_t i = 0; i < p->armor.regular_count; i++) {
    if (strcmp(p->armor.regular[i], p->armor.empty) == 0) has_empty = true;
    for (size_t j = 0; j < i; j++) if (strcmp(p->armor.regular[i], p->armor.regular[j]) == 0)
      qa_range_error("Source armor metadata has no declared inventory item");
  }
  const qa_item_id armor_items[4] = {0}; (void)armor_items;
  bool all_present = has_empty && p->armor.cells_index < p->client.inventory_count;
  {
    const qa_item_id need_screen = p->armor.screen, need_shield = p->armor.shield, need_cells = p->armor.cells;
    bool have_screen = false, have_shield = false, have_cells = false, have_regular = true;
    for (size_t i = 0; i < p->inventory_count; i++) {
      if (strcmp(p->inventory[i].item, need_screen) == 0) have_screen = true;
      if (strcmp(p->inventory[i].item, need_shield) == 0) have_shield = true;
      if (strcmp(p->inventory[i].item, need_cells) == 0) have_cells = true;
    }
    for (size_t i = 0; i < p->armor.regular_count && have_regular; i++) {
      bool found = false;
      for (size_t j = 0; j < p->inventory_count; j++) if (strcmp(p->inventory[j].item, p->armor.regular[i]) == 0) found = true;
      have_regular = found;
    }
    all_present = all_present && have_screen && have_shield && have_cells && have_regular;
  }
  if (!all_present) qa_range_error("Source armor metadata has no declared inventory item");
  if (p->armor.normal + 4 > 65536 || p->armor.energy + 4 > 65536 || p->movement.speed_load_count == 0)
    qa_range_error("Invalid source armor or movement metadata");
}
static double bounded(const qa_save_reader *r, double min, double max) {
  double v = qa_save_reader_integer_min(r, min);
  if (v > max) qa_save_reader_fail(r, "Expected integer at most %.0f", max);
  return v;
}
static qa_compat_q2_rerelease_world_location read_location(const qa_save_reader *r) {
  qa_save_reader kind_r, storage_r; qa_save_reader_field(r, "kind", &kind_r); qa_save_reader_field(r, "storage", &storage_r);
  static const char *const kinds[] = {"register", "stack"}; static const char *const storages[] = {"pointer", "int32", "uint32"};
  const char *kind = qa_save_reader_choice_string(&kind_r, kinds, 2);
  const char *storage = qa_save_reader_choice_string(&storage_r, storages, 3);
  qa_save_reader_destroy(&kind_r); qa_save_reader_destroy(&storage_r);
  qa_compat_q2_rerelease_world_location loc; memset(&loc, 0, sizeof loc); loc.storage = storage;
  if (strcmp(kind, "register") == 0) {
    static const char *const regs[] = {"rax", "rcx", "rdx", "rbx", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
    qa_save_reader f; qa_save_reader_field(r, "register", &f);
    loc.is_register = true; loc.reg = qa_save_reader_choice_string(&f, regs, 15); qa_save_reader_destroy(&f);
  } else {
    qa_save_reader f; qa_save_reader_field(r, "offset", &f);
    loc.offset = bounded(&f, 0, 1048576 - storage_bytes(storage)); qa_save_reader_destroy(&f);
  }
  return loc;
}
static bool read_call(const qa_save_reader *parent, const char *n, qa_compat_q2_native_combat_operation op, qa_compat_q2_native_combat_call *out) {
  qa_save_reader f; qa_save_reader_field(parent, n, &f);
  bool ok = qa_compat_q2_read_native_combat_call(&f, op, qa_compat_q2_rerelease_abi, out);
  qa_save_reader_destroy(&f); return ok;
}
static double rva_field(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); double v = bounded(&f, 1, 4294967295.0); qa_save_reader_destroy(&f); return v; }
static double int_field(const qa_save_reader *r, const char *n, double min) { qa_save_reader f; qa_save_reader_field(r, n, &f); double v = qa_save_reader_integer_min(&f, min); qa_save_reader_destroy(&f); return v; }
bool qa_compat_q2_read_rerelease_primary_world_profile(const qa_save_reader *reader, qa_content_digest digest,
                                                       qa_compat_q2_rerelease_primary_world_profile *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader client, entries, regular, monster, armor, flags, movement, calls;
  qa_save_reader_field(reader, "client", &client); qa_save_reader_field(reader, "entries", &entries);
  qa_save_reader_field(reader, "regularArmor", &regular); qa_save_reader_field(reader, "monster", &monster);
  qa_save_reader_field(reader, "armor", &armor); qa_save_reader_field(reader, "flags", &flags);
  qa_save_reader_field(reader, "movement", &movement); qa_save_reader_field(reader, "calls", &calls);
  bool ok = read_call(&calls, "pain", QA_Q2CO_PAIN, &out->calls.pain) && read_call(&calls, "death", QA_Q2CO_DEATH, &out->calls.death) &&
            read_call(&calls, "processPain", QA_Q2CO_DEFERRED, &out->calls.process_pain) &&
            read_call(&calls, "damage", QA_Q2CO_DAMAGE, &out->calls.damage) && read_call(&calls, "powerArmor", QA_Q2CO_POWER_ARMOR, &out->calls.power_armor);
  if (!ok) goto done;
  out->digest = digest;
  { qa_save_reader f; qa_save_reader_field(reader, "edict", &f); out->edict = qa_read_layout(&f); qa_save_reader_destroy(&f); }
  out->client.authority_is_artifact = true; out->client.header_digest = NULL; out->client.digest = digest;
  { qa_save_reader f; qa_save_reader_field(&client, "layout", &f); out->client.layout = qa_read_layout(&f); qa_save_reader_destroy(&f); }
  { qa_save_reader f; qa_save_reader_field(&client, "inventoryCount", &f); out->client.inventory_count = bounded(&f, 1, 65536); qa_save_reader_destroy(&f); }
  { qa_save_reader f; qa_save_reader_field(&client, "ammoCount", &f); out->client.ammo_count = bounded(&f, 1, 65536); qa_save_reader_destroy(&f); }
  out->entries.spawn = rva_field(&entries, "spawn"); out->entries.free_ = rva_field(&entries, "free");
  out->entries.damage = rva_field(&entries, "damage"); out->entries.power_armor = rva_field(&entries, "powerArmor");
  out->entries.process_pain = rva_field(&entries, "processPain"); out->entries.time = rva_field(&entries, "time");
  { qa_save_reader f; qa_save_reader_field(&entries, "regularArmor", &f);
    out->entries.regular_armor.entry = rva_field(&f, "entry"); out->entries.regular_armor.join = rva_field(&f, "join"); qa_save_reader_destroy(&f); }
  { const char *names[] = {"target", "amount", "point", "normal", "flags", "result"};
    qa_compat_q2_rerelease_world_location *slots[] = {&out->regular_armor.target, &out->regular_armor.amount, &out->regular_armor.point,
                                                      &out->regular_armor.normal, &out->regular_armor.flags, &out->regular_armor.result};
    for (int i = 0; i < 6; i++) { qa_save_reader f; qa_save_reader_field(&regular, names[i], &f); *slots[i] = read_location(&f); qa_save_reader_destroy(&f); } }
  { qa_save_reader f; qa_save_reader_field(&regular, "repair", &f); size_t n = qa_save_reader_list_count(&f);
    qa_compat_q2_rerelease_armor_repair *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    for (size_t i = 0; i < n; i++) { qa_save_reader x, s, t; qa_save_reader_item(&f, i, &x);
      qa_save_reader_field(&x, "source", &s); arr[i].source = read_location(&s); qa_save_reader_destroy(&s);
      qa_save_reader_field(&x, "target", &t); arr[i].target = read_location(&t); qa_save_reader_destroy(&t);
      qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); out->regular_armor.repair = arr; out->regular_armor.repair_count = n; }
  out->monster.attacker = int_field(&monster, "attacker", 0); out->monster.inflictor = int_field(&monster, "inflictor", 0);
  out->monster.blood = int_field(&monster, "blood", 0); out->monster.knockback = int_field(&monster, "knockback", 0);
  out->monster.point = int_field(&monster, "point", 0); out->monster.mod = int_field(&monster, "mod", 0);
  out->monster.invincible_time = int_field(&monster, "invincibleTime", 0);
  { qa_save_reader f; qa_save_reader_field(reader, "inventory", &f); size_t n = qa_save_reader_list_count(&f);
    qa_compat_q2_rerelease_inventory_row *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    memset(arr, 0, (n ? n : 1) * sizeof *arr);
    for (size_t i = 0; i < n; i++) { qa_save_reader x, source, capacity; qa_save_reader_item(&f, i, &x);
      { qa_save_reader it; qa_save_reader_field(&x, "item", &it); arr[i].item = qa_namespaced(&it); qa_save_reader_destroy(&it); }
      qa_save_reader_field(&x, "source", &source); qa_save_reader_field(&x, "capacity", &capacity);
      { qa_save_reader k; static const char *const kinds[] = {"classname", "index", "remaining"};
        qa_save_reader_field(&source, "kind", &k); arr[i].source_kind = qa_save_reader_choice_string(&k, kinds, 3); qa_save_reader_destroy(&k); }
      if (strcmp(arr[i].source_kind, "classname") == 0) { qa_save_reader nm; qa_save_reader_field(&source, "name", &nm);
          static char names_scratch[65536]; (void)names_scratch; arr[i].source_name = strdup(qa_save_reader_string(&nm)); qa_save_reader_destroy(&nm); }
      else if (strcmp(arr[i].source_kind, "index") == 0) arr[i].source_index = int_field(&source, "index", 0);
      { qa_save_reader k; static const char *const kinds[] = {"ammo", "fixed"};
        qa_save_reader_field(&capacity, "kind", &k); const char *ck = qa_save_reader_choice_string(&k, kinds, 2); qa_save_reader_destroy(&k);
        arr[i].capacity_is_ammo = strcmp(ck, "ammo") == 0; }
      if (arr[i].capacity_is_ammo) arr[i].capacity_source_index = int_field(&capacity, "sourceIndex", 0);
      else { qa_save_reader cf; qa_save_reader_field(&capacity, "count", &cf); arr[i].capacity_count = bounded(&cf, 0, 2147483647.0); qa_save_reader_destroy(&cf); }
      qa_save_reader_destroy(&source); qa_save_reader_destroy(&capacity); qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); out->inventory = arr; out->inventory_count = n; }
  out->armor.table = rva_field(&armor, "table");
  { qa_save_reader f; qa_save_reader_field(&armor, "stride", &f); out->armor.stride = bounded(&f, 8, 65536); qa_save_reader_destroy(&f); }
  out->armor.normal = int_field(&armor, "normal", 0); out->armor.energy = int_field(&armor, "energy", 0);
  { qa_save_reader f; qa_save_reader_field(&armor, "regular", &f); size_t n = qa_save_reader_list_count(&f);
    qa_item_id *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    for (size_t i = 0; i < n; i++) { qa_save_reader x; qa_save_reader_item(&f, i, &x); arr[i] = qa_namespaced(&x); qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); out->armor.regular = arr; out->armor.regular_count = n; }
  { qa_save_reader f; qa_save_reader_field(&armor, "empty", &f); out->armor.empty = qa_namespaced(&f); qa_save_reader_destroy(&f); }
  { qa_save_reader f; qa_save_reader_field(&armor, "screen", &f); out->armor.screen = qa_namespaced(&f); qa_save_reader_destroy(&f); }
  { qa_save_reader f; qa_save_reader_field(&armor, "shield", &f); out->armor.shield = qa_namespaced(&f); qa_save_reader_destroy(&f); }
  { qa_save_reader f; qa_save_reader_field(&armor, "cells", &f); out->armor.cells = qa_namespaced(&f); qa_save_reader_destroy(&f); }
  out->armor.cells_index = int_field(&armor, "cellsIndex", 0);
  out->flags.godmode = int_field(&flags, "godmode", 1); out->flags.notarget = int_field(&flags, "notarget", 1);
  out->flags.no_knockback = int_field(&flags, "noKnockback", 1); out->flags.power_armor = int_field(&flags, "powerArmor", 1);
  { qa_save_reader f; qa_save_reader_field(&movement, "body", &f); if (!qa_save_reader_is_undefined(&f)) {
      out->movement.has_body = true; out->movement.body.dimensions = rva_field(&f, "dimensions");
      out->movement.body.trace = rva_field(&f, "trace"); out->movement.body.movement_global = rva_field(&f, "movementGlobal"); }
    qa_save_reader_destroy(&f); }
  out->movement.game_api = rva_field(&movement, "gameApi"); out->movement.pmove = rva_field(&movement, "pmove");
  { qa_save_reader f; qa_save_reader_field(&movement, "speedLoads", &f); size_t n = qa_save_reader_list_count(&f);
    qa_compat_q2_rerelease_speed_load *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    for (size_t i = 0; i < n; i++) { qa_save_reader x; qa_save_reader_item(&f, i, &x);
      arr[i].next = rva_field(&x, "next");
      { qa_save_reader rg; qa_save_reader_field(&x, "register", &rg); arr[i].reg = bounded(&rg, 0, 15); qa_save_reader_destroy(&rg); }
      qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); out->movement.speed_loads = arr; out->movement.speed_load_count = n; }
  qa_compat_q2_validate_rerelease_primary_world_profile(out, digest);
done:
  qa_save_reader_destroy(&client); qa_save_reader_destroy(&entries); qa_save_reader_destroy(&regular);
  qa_save_reader_destroy(&monster); qa_save_reader_destroy(&armor); qa_save_reader_destroy(&flags);
  qa_save_reader_destroy(&movement); qa_save_reader_destroy(&calls);
  return ok;
}
void qa_compat_q2_rerelease_world_profile_free(qa_compat_q2_rerelease_primary_world_profile *p) {
  if (!p || p == &retail) return;
  qa_compat_q2_native_combat_call *calls[] = {&p->calls.pain, &p->calls.death, &p->calls.process_pain, &p->calls.damage, &p->calls.power_armor};
  for (size_t i = 0; i < 5; i++) if (calls[i]->arguments && calls[i]->arguments != p->calls.storage[i]) qa_compat_q2_native_combat_call_free(calls[i]);
  if (p->regular_armor.repair != retail_repair) free((void *)p->regular_armor.repair);
  if (p->inventory != retail_inventory) {
    for (size_t i = 0; i < p->inventory_count; i++) free((void *)p->inventory[i].source_name);
    free((void *)p->inventory);
  }
  if (p->armor.regular != retail_regular) free((void *)p->armor.regular);
  if (p->movement.speed_loads != retail_loads) free((void *)p->movement.speed_loads);
}
