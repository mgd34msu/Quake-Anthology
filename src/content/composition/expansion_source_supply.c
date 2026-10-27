/* Port of src/content/composition/expansion-source-supply.ts. GPL-2.0-or-later. */
#include "qa/content/composition/expansion_source_supply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct alias { qa_item_id source; qa_item_id base; } alias;

static const alias AMMO_ALIASES[] = {
    {"rogue:ammo/lava-nails", "q1:ammo/nails"}, {"rogue:ammo/multi-rockets", "q1:ammo/rockets"},
    {"rogue:ammo/plasma", "q1:ammo/cells"},
    {"q2:ammo_magslug", "q2:ammo_slugs"}, {"q2:ammo_flechettes", "q2:ammo_bullets"},
    {"q2:ammo_disruptor", "q2:ammo_cells"}, {"q2:ammo_trap", "q2:ammo_grenades"},
    {"q2:ammo_tesla", "q2:ammo_grenades"}, {"q2:ammo_prox", "q2:ammo_grenades"},
    {"q3:ammo/nailgun", "q3:ammo/machinegun"}, {"q3:ammo/chaingun", "q3:ammo/machinegun"},
    {"q3:ammo/proxlauncher", "q3:ammo/grenadelauncher"}};
static const alias WEAPON_ALIASES[] = {
    {"q1:weapon/hipnotic:laser", "q1:weapon/lightning"}, {"q1:weapon/hipnotic:mjolnir", "q1:weapon/axe"},
    {"q1:weapon/hipnotic:proximity", "q1:weapon/grenadelauncher"},
    {"q1:weapon/mg3:laser", "q1:weapon/lightning"}, {"q1:weapon/mg3:mjolnir", "q1:weapon/axe"},
    {"q1:weapon/rogue:lava-nailgun", "q1:weapon/nailgun"}, {"q1:weapon/rogue:lava-supernailgun", "q1:weapon/supernailgun"},
    {"q1:weapon/rogue:multi-grenade", "q1:weapon/grenadelauncher"}, {"q1:weapon/rogue:multi-rocket", "q1:weapon/rocketlauncher"},
    {"q1:weapon/rogue:plasma", "q1:weapon/lightning"},
    {"q2:weapon_boomer", "q2:weapon_hyperblaster"}, {"q2:weapon_phalanx", "q2:weapon_railgun"},
    {"q2:weapon_plasmabeam", "q2:weapon_hyperblaster"}, {"q2:weapon_etf_rifle", "q2:weapon_machinegun"},
    {"q2:weapon_proxlauncher", "q2:weapon_grenadelauncher"}, {"q2:weapon_disintegrator", "q2:weapon_bfg"},
    {"q2:weapon_chainfist", "q2:weapon_shotgun"},
    {"q2:ammo_trap", "q2:ammo_grenades"}, {"q2:ammo_tesla", "q2:ammo_grenades"},
    {"q3:weapon/nailgun", "q3:weapon/machinegun"}, {"q3:weapon/chaingun", "q3:weapon/machinegun"},
    {"q3:weapon/proxlauncher", "q3:weapon/grenadelauncher"}};

static bool copy_row(const qa_pickup_supply_mapping *row, qa_pickup_supply_mapping *out) {
  out->source = row->source;
  out->destination_count = row->destination_count;
  out->destinations = NULL;
  if (row->destination_count == 0) return true;
  qa_item_id *destinations = (qa_item_id *)malloc(row->destination_count * sizeof *destinations);
  if (destinations == NULL) return false;
  memcpy(destinations, row->destinations, row->destination_count * sizeof *destinations);
  out->destinations = destinations;
  return true;
}

/* TS extend(): original rows, then one row per alias whose base row exists
 * and whose source is not already a row source (checked against the
 * ORIGINAL rows only, as in TS). */
static bool extend_rows(const qa_pickup_supply_mapping *rows, size_t row_count,
                        const alias *aliases, size_t alias_count,
                        qa_pickup_supply_mapping **out_rows, size_t *out_count) {
  size_t extra = 0;
  for (size_t a = 0; a < alias_count; a++) {
    bool base_found = false, source_found = false;
    for (size_t i = 0; i < row_count; i++) {
      if (strcmp(rows[i].source, aliases[a].base) == 0) base_found = true;
      if (strcmp(rows[i].source, aliases[a].source) == 0) source_found = true;
    }
    if (base_found && !source_found) extra++;
  }
  size_t total = row_count + extra;
  qa_pickup_supply_mapping *out = NULL;
  if (total > 0) {
    out = (qa_pickup_supply_mapping *)calloc(total, sizeof *out);
    if (out == NULL) return false;
  }
  size_t have = 0;
  for (size_t i = 0; i < row_count; i++, have++)
    if (!copy_row(&rows[i], &out[have])) goto oom;
  for (size_t a = 0; a < alias_count; a++) {
    const qa_pickup_supply_mapping *mapping = NULL;
    bool source_found = false;
    for (size_t i = 0; i < row_count; i++) {
      if (mapping == NULL && strcmp(rows[i].source, aliases[a].base) == 0) mapping = &rows[i];
      if (strcmp(rows[i].source, aliases[a].source) == 0) source_found = true;
    }
    if (mapping == NULL || source_found) continue;
    qa_pickup_supply_mapping row = {aliases[a].source, mapping->destinations, mapping->destination_count};
    if (!copy_row(&row, &out[have])) goto oom;
    have++;
  }
  *out_rows = out;
  *out_count = have;
  return true;
oom:
  for (size_t i = 0; i < total; i++) free((void *)out[i].destinations);
  free(out);
  return false;
}

bool qa_composition_expansion_source_supply(const qa_pickup_supply_profile *profile,
                                            qa_composition_owned_profile *out) {
  memset(out, 0, sizeof *out);
  size_t length = strlen(profile->id) + strlen("/expansion-sources");
  out->id = (char *)malloc(length + 1);
  if (out->id == NULL) return false;
  (void)sprintf(out->id, "%s/expansion-sources", profile->id);
  if (!extend_rows(profile->ammo, profile->ammo_count, AMMO_ALIASES, sizeof AMMO_ALIASES / sizeof *AMMO_ALIASES, &out->ammo, &out->ammo_count) ||
      !extend_rows(profile->weapons, profile->weapon_count, WEAPON_ALIASES, sizeof WEAPON_ALIASES / sizeof *WEAPON_ALIASES, &out->weapons, &out->weapon_count)) {
    qa_composition_owned_profile_destroy(out);
    return false;
  }
  /* weaponOwners passes through untouched (undefined stays undefined). */
  if (profile->weapon_owners != NULL) {
    out->has_weapon_owners = true;
    out->weapon_owner_count = profile->weapon_owner_count;
    if (profile->weapon_owner_count > 0) {
      out->weapon_owners = (qa_pickup_supply_owner *)malloc(profile->weapon_owner_count * sizeof *out->weapon_owners);
      if (out->weapon_owners == NULL) { qa_composition_owned_profile_destroy(out); return false; }
      memcpy(out->weapon_owners, profile->weapon_owners, profile->weapon_owner_count * sizeof *out->weapon_owners);
    }
  }
  /* ammoOwners: existing rows plus, for each distinct ammo destination
   * (first-seen order) not already owned, its unique source row if any. */
  out->has_ammo_owners = true;
  size_t distinct = 0;
  for (size_t i = 0; i < profile->ammo_count; i++) distinct += profile->ammo[i].destination_count;
  size_t cap = profile->ammo_owner_count + distinct;
  if (cap > 0) {
    out->ammo_owners = (qa_pickup_supply_owner *)malloc(cap * sizeof *out->ammo_owners);
    if (out->ammo_owners == NULL) { qa_composition_owned_profile_destroy(out); return false; }
  }
  size_t have = 0;
  if (profile->ammo_owners != NULL)
    for (size_t i = 0; i < profile->ammo_owner_count; i++) out->ammo_owners[have++] = profile->ammo_owners[i];
  for (size_t r = 0; r < profile->ammo_count; r++)
    for (size_t d = 0; d < profile->ammo[r].destination_count; d++) {
      qa_item_id item = profile->ammo[r].destinations[d];
      bool seen = false; /* new Set(...) first-seen iteration */
      for (size_t r2 = 0; r2 <= r && !seen; r2++) {
        size_t limit = r2 == r ? d : profile->ammo[r2].destination_count;
        for (size_t d2 = 0; d2 < limit; d2++)
          if (strcmp(profile->ammo[r2].destinations[d2], item) == 0) { seen = true; break; }
      }
      if (seen) continue;
      bool owned = false;
      for (size_t i = 0; i < have; i++) if (strcmp(out->ammo_owners[i].item, item) == 0) { owned = true; break; }
      if (owned) continue;
      qa_item_id source = NULL;
      size_t sources = 0;
      for (size_t i = 0; i < profile->ammo_count; i++)
        for (size_t k = 0; k < profile->ammo[i].destination_count; k++)
          if (strcmp(profile->ammo[i].destinations[k], item) == 0) { sources++; source = profile->ammo[i].source; break; }
      if (sources == 1 && source != NULL) out->ammo_owners[have++] = (qa_pickup_supply_owner){item, source};
    }
  out->ammo_owner_count = have;
  return true;
}
