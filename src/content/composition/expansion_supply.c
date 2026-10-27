/* Port of src/content/composition/expansion-supply.ts. GPL-2.0-or-later. */
#include "qa/content/composition/expansion_supply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const NAMES[QA_COMPOSITION_EXPANSION_COUNT] = {
    "q1-rogue", "q1-mg3", "q2-xatrix", "q2-rogue", "q3-missionpack"};

const char *qa_composition_expansion_supply_name(qa_composition_expansion_supply_kind expansion) {
  return NAMES[expansion];
}

typedef struct catalog_entry {
  qa_item_id key;
  const qa_item_id *destinations;
  size_t destination_count;
} catalog_entry;
typedef struct catalog {
  const catalog_entry *entries;
  size_t count;
} catalog;

#define CE(key, ...) {key, (const qa_item_id[]){__VA_ARGS__}, sizeof((const qa_item_id[]){__VA_ARGS__}) / sizeof(qa_item_id)}

static const catalog_entry AMMO_Q1_ROGUE[] = {
    CE("q1:ammo/nails", "rogue:ammo/lava-nails"), CE("q1:ammo/rockets", "rogue:ammo/multi-rockets"),
    CE("q1:ammo/cells", "rogue:ammo/plasma")};
static const catalog_entry AMMO_Q2_XATRIX[] = {
    CE("q2:ammo_grenades", "q2:ammo_trap"), CE("q2:ammo_cells", "q2:ammo_magslug"),
    CE("q2:ammo_slugs", "q2:ammo_magslug")};
static const catalog_entry AMMO_Q2_ROGUE[] = {
    CE("q2:ammo_bullets", "q2:ammo_flechettes"), CE("q2:ammo_grenades", "q2:ammo_prox", "q2:ammo_tesla"),
    CE("q2:ammo_cells", "q2:ammo_disruptor"), CE("q2:ammo_slugs", "q2:ammo_disruptor")};
static const catalog_entry AMMO_Q3_MP[] = {
    CE("q3:ammo/machinegun", "q3:ammo/nailgun", "q3:ammo/chaingun"),
    CE("q3:ammo/grenadelauncher", "q3:ammo/proxlauncher")};
static const catalog AMMO[QA_COMPOSITION_EXPANSION_COUNT] = {
    {AMMO_Q1_ROGUE, 3}, {NULL, 0}, {AMMO_Q2_XATRIX, 3}, {AMMO_Q2_ROGUE, 4}, {AMMO_Q3_MP, 2}};

static const catalog_entry WEAPONS_Q1_ROGUE[] = {
    CE("q1:weapon/nailgun", "q1:weapon/rogue:lava-nailgun"),
    CE("q1:weapon/supernailgun", "q1:weapon/rogue:lava-supernailgun"),
    CE("q1:weapon/grenadelauncher", "q1:weapon/rogue:multi-grenade"),
    CE("q1:weapon/rocketlauncher", "q1:weapon/rogue:multi-rocket"),
    CE("q1:weapon/lightning", "q1:weapon/rogue:plasma")};
static const catalog_entry WEAPONS_Q1_MG3[] = {
    CE("q1:weapon/lightning", "q1:weapon/mg3:laser", "q1:weapon/mg3:mjolnir")};
static const catalog_entry WEAPONS_Q2_XATRIX[] = {
    CE("q2:weapon_hyperblaster", "q2:weapon_boomer", "q2:weapon_phalanx"),
    CE("q2:weapon_railgun", "q2:weapon_phalanx")};
static const catalog_entry WEAPONS_Q2_ROGUE[] = {
    CE("q2:weapon_shotgun", "q2:weapon_chainfist"), CE("q2:weapon_machinegun", "q2:weapon_etf_rifle"),
    CE("q2:weapon_grenadelauncher", "q2:weapon_proxlauncher"),
    CE("q2:weapon_hyperblaster", "q2:weapon_plasmabeam", "q2:weapon_disintegrator"),
    CE("q2:weapon_railgun", "q2:weapon_disintegrator")};
static const catalog_entry WEAPONS_Q3_MP[] = {
    CE("q3:weapon/machinegun", "q3:weapon/nailgun", "q3:weapon/chaingun"),
    CE("q3:weapon/grenadelauncher", "q3:weapon/proxlauncher")};
static const catalog WEAPONS[QA_COMPOSITION_EXPANSION_COUNT] = {
    {WEAPONS_Q1_ROGUE, 5}, {WEAPONS_Q1_MG3, 1}, {WEAPONS_Q2_XATRIX, 2}, {WEAPONS_Q2_ROGUE, 5}, {WEAPONS_Q3_MP, 2}};

static const qa_pickup_supply_owner PERIODIC_Q1_ROGUE[] = {
    {"rogue:ammo/lava-nails", "q3:ammo/nailgun"}, {"rogue:ammo/multi-rockets", "q3:ammo/proxlauncher"},
    {"rogue:ammo/plasma", "q3:ammo/plasmagun"}};
static const qa_pickup_supply_owner PERIODIC_Q2_XATRIX[] = {
    {"q2:ammo_magslug", "q3:ammo/railgun"}, {"q2:ammo_trap", "q3:ammo/proxlauncher"}};
static const qa_pickup_supply_owner PERIODIC_Q2_ROGUE[] = {
    {"q2:ammo_flechettes", "q3:ammo/nailgun"}, {"q2:ammo_prox", "q3:ammo/proxlauncher"},
    {"q2:ammo_tesla", "q3:ammo/proxlauncher"}, {"q2:ammo_disruptor", "q3:ammo/bfg"}};
static const qa_pickup_supply_owner PERIODIC_Q3_MP[] = {
    {"q3:ammo/nailgun", "q3:ammo/machinegun"}, {"q3:ammo/chaingun", "q3:ammo/machinegun"},
    {"q3:ammo/proxlauncher", "q3:ammo/grenadelauncher"}};
static const struct { const qa_pickup_supply_owner *rows; size_t count; } PERIODIC[QA_COMPOSITION_EXPANSION_COUNT] = {
    {PERIODIC_Q1_ROGUE, 3}, {NULL, 0}, {PERIODIC_Q2_XATRIX, 2}, {PERIODIC_Q2_ROGUE, 4}, {PERIODIC_Q3_MP, 3}};

static const catalog_entry *catalog_get(const catalog *cat, qa_item_id item) {
  for (size_t i = 0; i < cat->count; i++)
    if (strcmp(cat->entries[i].key, item) == 0) return &cat->entries[i];
  return NULL;
}

static const qa_pickup_supply_owner EMPTY_OWNER = {"", ""};

qa_pickup_supply_profile qa_composition_owned_profile_view(const qa_composition_owned_profile *owned) {
  qa_pickup_supply_profile view;
  view.id = owned->id;
  view.ammo = owned->ammo;
  view.ammo_count = owned->ammo_count;
  view.weapons = owned->weapons;
  view.weapon_count = owned->weapon_count;
  view.weapon_owners = !owned->has_weapon_owners ? NULL
                       : owned->weapon_owner_count > 0 ? owned->weapon_owners : &EMPTY_OWNER;
  view.weapon_owner_count = owned->weapon_owner_count;
  view.ammo_owners = !owned->has_ammo_owners ? NULL
                     : owned->ammo_owner_count > 0 ? owned->ammo_owners : &EMPTY_OWNER;
  view.ammo_owner_count = owned->ammo_owner_count;
  return view;
}

void qa_composition_owned_profile_destroy(qa_composition_owned_profile *owned) {
  if (owned == NULL) return;
  for (size_t i = 0; i < owned->ammo_count; i++) free((void *)owned->ammo[i].destinations);
  for (size_t i = 0; i < owned->weapon_count; i++) free((void *)owned->weapons[i].destinations);
  free(owned->ammo);
  free(owned->weapons);
  free(owned->weapon_owners);
  free(owned->ammo_owners);
  free(owned->id);
  memset(owned, 0, sizeof *owned);
}

/* TS: [first, ...new Set([...rest, ...additional].filter(item => item !== first))]. */
static bool extend_rows(const qa_pickup_supply_mapping *rows, size_t row_count, const catalog *catalogs,
                        const qa_composition_expansion_supply_kind *expansions, size_t expansion_count,
                        qa_pickup_supply_mapping **out_rows) {
  qa_pickup_supply_mapping *out = NULL;
  if (row_count > 0) {
    out = (qa_pickup_supply_mapping *)calloc(row_count, sizeof *out);
    if (out == NULL) return false;
  }
  for (size_t r = 0; r < row_count; r++) {
    const qa_pickup_supply_mapping *row = &rows[r];
    out[r].source = row->source;
    size_t cap = row->destination_count + 8, have = 0;
    qa_item_id *destinations = (qa_item_id *)malloc(cap * sizeof *destinations);
    if (destinations == NULL) goto oom;
    if (row->destination_count > 0) {
      qa_item_id first = row->destinations[0];
      destinations[have++] = first;
      /* rest, then additional per expansion/item, deduped, minus first. */
      size_t tail_cap = 16, tail_have = 0;
      qa_item_id *tail = (qa_item_id *)malloc(tail_cap * sizeof *tail);
      if (tail == NULL) { free(destinations); goto oom; }
      for (size_t i = 1; i < row->destination_count; i++) {
        if (tail_have == tail_cap) { tail_cap *= 2; qa_item_id *grown = (qa_item_id *)realloc(tail, tail_cap * sizeof *tail); if (grown == NULL) { free(tail); free(destinations); goto oom; } tail = grown; }
        tail[tail_have++] = row->destinations[i];
      }
      for (size_t e = 0; e < expansion_count; e++)
        for (size_t i = 0; i < row->destination_count; i++) {
          const catalog_entry *entry = catalog_get(&catalogs[expansions[e]], row->destinations[i]);
          if (entry == NULL) continue;
          for (size_t d = 0; d < entry->destination_count; d++) {
            if (tail_have == tail_cap) { tail_cap *= 2; qa_item_id *grown = (qa_item_id *)realloc(tail, tail_cap * sizeof *tail); if (grown == NULL) { free(tail); free(destinations); goto oom; } tail = grown; }
            tail[tail_have++] = entry->destinations[d];
          }
        }
      for (size_t i = 0; i < tail_have; i++) {
        if (strcmp(tail[i], first) == 0) continue;
        bool seen = false;
        for (size_t j = 1; j < have; j++) if (strcmp(destinations[j], tail[i]) == 0) { seen = true; break; }
        if (seen) continue;
        if (have == cap) { cap *= 2; qa_item_id *grown = (qa_item_id *)realloc(destinations, cap * sizeof *destinations); if (grown == NULL) { free(tail); free(destinations); goto oom; } destinations = grown; }
        destinations[have++] = tail[i];
      }
      free(tail);
    }
    out[r].destinations = destinations;
    out[r].destination_count = have;
  }
  *out_rows = out;
  return true;
oom:
  if (out != NULL) {
    for (size_t i = 0; i < row_count; i++) free((void *)out[i].destinations);
    free(out);
  }
  return false;
}

bool qa_composition_expansion_supply(const qa_pickup_supply_profile *profile,
                                     const qa_composition_expansion_supply_kind *expansions,
                                     size_t expansion_count, qa_composition_owned_profile *out) {
  memset(out, 0, sizeof *out);
  size_t length = strlen(profile->id) + 1;
  for (size_t i = 0; i < expansion_count; i++) length += strlen(NAMES[expansions[i]]) + (i > 0 ? 1 : 0);
  out->id = (char *)malloc(length + 1);
  if (out->id == NULL) return false;
  char *cursor = out->id;
  cursor += sprintf(cursor, "%s/", profile->id);
  for (size_t i = 0; i < expansion_count; i++) cursor += sprintf(cursor, "%s%s", i > 0 ? "+" : "", NAMES[expansions[i]]);
  out->ammo_count = profile->ammo_count;
  out->weapon_count = profile->weapon_count;
  if (!extend_rows(profile->ammo, profile->ammo_count, AMMO, expansions, expansion_count, &out->ammo) ||
      !extend_rows(profile->weapons, profile->weapon_count, WEAPONS, expansions, expansion_count, &out->weapons)) {
    /* counts are set before the arrays exist only for destroy safety on partial rows */
    if (out->ammo == NULL) out->ammo_count = 0;
    if (out->weapons == NULL) out->weapon_count = 0;
    qa_composition_owned_profile_destroy(out);
    return false;
  }
  if (profile->weapon_owners != NULL) {
    out->has_weapon_owners = true;
    size_t count = profile->weapon_owner_count;
    for (size_t e = 0; e < expansion_count; e++)
      for (size_t i = 0; i < profile->weapon_owner_count; i++) {
        const catalog_entry *entry = catalog_get(&WEAPONS[expansions[e]], profile->weapon_owners[i].item);
        if (entry != NULL) count += entry->destination_count;
      }
    if (count > 0) {
      out->weapon_owners = (qa_pickup_supply_owner *)malloc(count * sizeof *out->weapon_owners);
      if (out->weapon_owners == NULL) { qa_composition_owned_profile_destroy(out); return false; }
    }
    size_t have = 0;
    for (size_t i = 0; i < profile->weapon_owner_count; i++) out->weapon_owners[have++] = profile->weapon_owners[i];
    for (size_t e = 0; e < expansion_count; e++)
      for (size_t i = 0; i < profile->weapon_owner_count; i++) {
        const catalog_entry *entry = catalog_get(&WEAPONS[expansions[e]], profile->weapon_owners[i].item);
        if (entry == NULL) continue;
        for (size_t d = 0; d < entry->destination_count; d++)
          out->weapon_owners[have++] = (qa_pickup_supply_owner){entry->destinations[d], profile->weapon_owners[i].source};
      }
    out->weapon_owner_count = have;
  }
  if (profile->ammo_owners != NULL) {
    out->has_ammo_owners = true;
    size_t count = profile->ammo_owner_count;
    for (size_t e = 0; e < expansion_count; e++) count += PERIODIC[expansions[e]].count;
    if (count > 0) {
      out->ammo_owners = (qa_pickup_supply_owner *)malloc(count * sizeof *out->ammo_owners);
      if (out->ammo_owners == NULL) { qa_composition_owned_profile_destroy(out); return false; }
    }
    size_t have = 0;
    for (size_t i = 0; i < profile->ammo_owner_count; i++) out->ammo_owners[have++] = profile->ammo_owners[i];
    for (size_t e = 0; e < expansion_count; e++)
      for (size_t i = 0; i < PERIODIC[expansions[e]].count; i++) out->ammo_owners[have++] = PERIODIC[expansions[e]].rows[i];
    out->ammo_owner_count = have;
  }
  return true;
}
