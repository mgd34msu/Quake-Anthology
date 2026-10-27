/* Port of src/compat/q2/classic/world-profile.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/world_profile.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_primary_reader.h"

static const double capacities[] = {0x6e4, 0x6e8, 0x6ec, 0x6f0, 0x6f4, 0x6f8, 0x6fc, 0x700};
static const qa_compat_q2_classic_unnamed_item unnamed[] = {{0, "", "q2:none"}, {47, "Health", "q2:item_health"}};
bool qa_compat_q2_classic_get_primary_world_profile(qa_content_digest digest, qa_compat_q2_classic_world_profile *out) {
  const qa_compat_q2_classic_combat_profile *p = qa_compat_q2_classic_get_combat_profile(digest);
  if (!p) return false;
  memset(out, 0, sizeof *out); out->base = *p;
  out->inventory_table.count = 48; out->inventory_table.class_name = 0; out->inventory_table.label = 40;
  out->inventory_table.flags = 56; out->inventory_table.ammo_flag = 2; out->inventory_table.tag = p->globals.item_bytes - 8;
  out->inventory_table.capacities = capacities; out->inventory_table.capacity_count = 8;
  out->inventory_table.unnamed = unnamed; out->inventory_table.unnamed_count = 2;
  out->inventory_table.empty_index = 0; out->inventory_table.sentinel = true;
  return true;
}
static double offield(const qa_save_reader *r, const char *name) {
  qa_save_reader f; qa_save_reader_field(r, name, &f); double v = qa_compat_q2_native_offset(&f); qa_save_reader_destroy(&f); return v;
}
static double intfield(const qa_save_reader *r, const char *name, double min) {
  qa_save_reader f; qa_save_reader_field(r, name, &f); double v = qa_save_reader_integer_min(&f, min); qa_save_reader_destroy(&f); return v;
}
static bool read_call(const qa_save_reader *calls, const char *name, qa_compat_q2_native_combat_operation op, qa_compat_q2_native_combat_call *out) {
  qa_save_reader f; qa_save_reader_field(calls, name, &f);
  bool ok = qa_compat_q2_read_native_combat_call(&f, op, qa_compat_q2_classic_abi, out);
  qa_save_reader_destroy(&f); return ok;
}
bool qa_compat_q2_read_classic_primary_world_profile(const qa_save_reader *reader, qa_content_digest digest, qa_compat_q2_classic_world_profile *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader fields, client, entries, globals, items, item_fields, armor_info, flags, teams, armor, table, calls;
  qa_save_reader_field(reader, "fields", &fields); qa_save_reader_field(reader, "client", &client);
  qa_save_reader_field(reader, "entries", &entries); qa_save_reader_field(reader, "globals", &globals);
  qa_save_reader_field(reader, "items", &items); qa_save_reader_field(reader, "itemFields", &item_fields);
  qa_save_reader_field(reader, "armorInfo", &armor_info); qa_save_reader_field(reader, "flags", &flags);
  qa_save_reader_field(reader, "teams", &teams); qa_save_reader_field(reader, "armor", &armor);
  qa_save_reader_field(reader, "inventoryTable", &table); qa_save_reader_field(reader, "calls", &calls);
  qa_compat_q2_classic_combat_profile *p = &out->base;
  bool ok = read_call(&calls, "pain", QA_Q2CO_PAIN, &p->calls.pain) && read_call(&calls, "death", QA_Q2CO_DEATH, &p->calls.death) &&
            read_call(&calls, "damage", QA_Q2CO_DAMAGE, &p->calls.damage) && read_call(&calls, "regularArmor", QA_Q2CO_REGULAR_ARMOR, &p->calls.regular_armor) &&
            read_call(&calls, "powerArmor", QA_Q2CO_POWER_ARMOR, &p->calls.power_armor);
  if (!ok) goto done;
  p->digest = digest;
  { qa_save_reader f; qa_save_reader_field(reader, "game", &f); static const char *const games[] = {"base", "xatrix", "rogue", "ctf"};
    p->game = qa_save_reader_choice_string(&f, games, 4); qa_save_reader_destroy(&f); }
  p->entity_bytes = intfield(reader, "entityBytes", 1);
  p->fields.health = offield(&fields, "health"); p->fields.damageable = offield(&fields, "damageable");
  p->fields.flags = offield(&fields, "flags"); p->fields.mass = offield(&fields, "mass");
  p->fields.velocity = offield(&fields, "velocity"); p->fields.pain = offield(&fields, "pain"); p->fields.die = offield(&fields, "die");
  p->client.inventory = offield(&client, "inventory"); p->client.inventory_count = intfield(&client, "inventoryCount", 1);
  p->client.max_grenades = offield(&client, "maxGrenades"); p->client.invincible_frame = offield(&client, "invincibleFrame");
  p->client.userinfo = offield(&client, "userinfo"); p->client.view_angles = offield(&client, "viewAngles");
  p->client.userinfo_bytes = intfield(&client, "userinfoBytes", 1);
  p->entries.damage = offield(&entries, "damage"); p->entries.power_armor = offield(&entries, "powerArmor");
  p->entries.regular_armor = offield(&entries, "regularArmor"); p->entries.spawn = offield(&entries, "spawn"); p->entries.free = offield(&entries, "free");
  p->globals.level_frame = offield(&globals, "levelFrame"); p->globals.item_list = offield(&globals, "itemList");
  p->globals.item_bytes = intfield(&globals, "itemBytes", 1);
  p->items.jacket = offield(&items, "jacket"); p->items.combat = offield(&items, "combat"); p->items.body = offield(&items, "body");
  p->items.screen = offield(&items, "screen"); p->items.shield = offield(&items, "shield"); p->items.cells = offield(&items, "cells");
  p->items.grenades = offield(&items, "grenades");
  p->item_fields.class_name = offield(&item_fields, "className"); p->item_fields.armor_info = offield(&item_fields, "armorInfo");
  p->armor_info.normal_protection = offield(&armor_info, "normalProtection"); p->armor_info.energy_protection = offield(&armor_info, "energyProtection");
  p->flags.invulnerable = offield(&flags, "invulnerable"); p->flags.notarget = offield(&flags, "notarget");
  p->flags.no_knockback = offield(&flags, "noKnockback"); p->flags.power_armor = offield(&flags, "powerArmor");
  p->teams.model = offield(&teams, "model"); p->teams.skin = offield(&teams, "skin");
  { qa_save_reader f; qa_save_reader_field(&armor, "regular", &f); size_t n = qa_save_reader_list_count(&f);
    double *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    for (size_t i = 0; i < n; i++) { qa_save_reader x; qa_save_reader_item(&f, i, &x); arr[i] = qa_compat_q2_native_offset(&x); qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); p->armor.regular = arr; p->armor.regular_count = n; }
  p->armor.empty = offield(&armor, "empty");
  out->inventory_table.count = intfield(&table, "count", 1);
  out->inventory_table.class_name = offield(&table, "className"); out->inventory_table.label = offield(&table, "label");
  out->inventory_table.flags = offield(&table, "flags"); out->inventory_table.ammo_flag = intfield(&table, "ammoFlag", 1);
  out->inventory_table.tag = offield(&table, "tag");
  { qa_save_reader f; qa_save_reader_field(&table, "capacities", &f); size_t n = qa_save_reader_list_count(&f);
    double *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    for (size_t i = 0; i < n; i++) { qa_save_reader x; qa_save_reader_item(&f, i, &x); arr[i] = qa_compat_q2_native_offset(&x); qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); out->inventory_table.capacities = arr; out->inventory_table.capacity_count = n; }
  { qa_save_reader f; qa_save_reader_field(&table, "unnamed", &f); size_t n = qa_save_reader_list_count(&f);
    qa_compat_q2_classic_unnamed_item *arr = malloc((n ? n : 1) * sizeof *arr); if (!arr) { qa_save_reader_destroy(&f); ok = false; goto done; }
    for (size_t i = 0; i < n; i++) { qa_save_reader x, lf, itf; qa_save_reader_item(&f, i, &x);
      arr[i].index = offield(&x, "index");
      qa_save_reader_field(&x, "label", &lf); arr[i].label = qa_save_reader_string(&lf); qa_save_reader_destroy(&lf);
      qa_save_reader_field(&x, "item", &itf); arr[i].item = qa_namespaced(&itf); qa_save_reader_destroy(&itf);
      qa_save_reader_destroy(&x); }
    qa_save_reader_destroy(&f); out->inventory_table.unnamed = arr; out->inventory_table.unnamed_count = n; }
  out->inventory_table.empty_index = intfield(&table, "emptyIndex", 0);
  { qa_save_reader f; qa_save_reader_field(&table, "sentinel", &f); out->inventory_table.sentinel = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f); }
  qa_compat_q2_validate_classic_combat_profile(p);
  if (out->inventory_table.count > p->client.inventory_count || out->inventory_table.empty_index >= out->inventory_table.count)
    qa_save_reader_fail(&table, "item table exceeds original inventory storage");
  { const double its[] = {p->items.jacket, p->items.combat, p->items.body, p->items.screen, p->items.shield, p->items.cells, p->items.grenades};
    for (size_t i = 0; i < 7; i++) if (its[i] >= out->inventory_table.count) qa_save_reader_fail(&table, "combat item is outside the original source table"); }
  for (size_t i = 0; i < p->armor.regular_count; i++) if (p->armor.regular[i] >= out->inventory_table.count) qa_save_reader_fail(&table, "combat item is outside the original source table");
  { const double fs[] = {out->inventory_table.class_name, out->inventory_table.label, out->inventory_table.flags, out->inventory_table.tag};
    for (size_t i = 0; i < 4; i++) if (fs[i] + 4 > p->globals.item_bytes) qa_save_reader_fail(&table, "item field exceeds declared stride"); }
  for (size_t i = 0; i < out->inventory_table.unnamed_count; i++) {
    const qa_compat_q2_classic_unnamed_item *e = &out->inventory_table.unnamed[i];
    bool dup = false;
    for (size_t j = 0; j < i; j++) if (out->inventory_table.unnamed[j].index == e->index || strcmp(out->inventory_table.unnamed[j].item, e->item) == 0) dup = true;
    if (e->index >= out->inventory_table.count || dup) qa_save_reader_fail(&table, "unnamed item identities must be unique source slots");
  }
done:
  qa_save_reader_destroy(&fields); qa_save_reader_destroy(&client); qa_save_reader_destroy(&entries);
  qa_save_reader_destroy(&globals); qa_save_reader_destroy(&items); qa_save_reader_destroy(&item_fields);
  qa_save_reader_destroy(&armor_info); qa_save_reader_destroy(&flags); qa_save_reader_destroy(&teams);
  qa_save_reader_destroy(&armor); qa_save_reader_destroy(&table); qa_save_reader_destroy(&calls);
  return ok;
}
void qa_compat_q2_classic_world_profile_free(qa_compat_q2_classic_world_profile *profile) {
  if (!profile || profile->inventory_table.capacities == capacities) return; /* borrowed static profile */
  qa_compat_q2_native_combat_call *calls[] = {&profile->base.calls.pain, &profile->base.calls.death, &profile->base.calls.damage,
                                              &profile->base.calls.regular_armor, &profile->base.calls.power_armor};
  for (size_t i = 0; i < 5; i++) if (calls[i]->arguments && calls[i]->arguments != profile->base.calls.storage[i]) qa_compat_q2_native_combat_call_free(calls[i]);
  free((void *)profile->base.armor.regular);
  free((void *)profile->inventory_table.capacities);
  free((void *)profile->inventory_table.unnamed);
  memset(profile, 0, sizeof *profile);
}
