/* Port of src/input/weapon-bindings.ts. GPL-2.0-or-later. */
#include "qa/input/weapon_bindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/input/support.h"

/*
 * Weapon catalog data mirrored from the unported content module
 * (its C port has not landed): TS owners src/content/q1/foundation
 * (types.ts WEAPONS/weaponItem, weapon-names.ts display names),
 * src/content/q1/missionpacks/types.ts (missionWeapons),
 * src/content/q2/foundation/weapons/definitions.ts and items.ts,
 * src/content/q2/missionpacks (weapons/definitions.ts, items.ts),
 * src/content/q3/foundation/arsenal.ts and
 * src/content/q3/base/shared/items.ts (pickup names). Only the
 * name/item/label fields this module reads are mirrored.
 */
static const char *const q1_weapons[8] = {"axe", "shotgun", "supershotgun",
                                          "nailgun", "supernailgun",
                                          "grenadelauncher", "rocketlauncher",
                                          "lightning"};
static const char *const q1_display[8] = {"Axe", "Shotgun",
                                          "Double-barrelled Shotgun", "Nailgun",
                                          "Super Nailgun", "Grenade Launcher",
                                          "Rocket Launcher", "Thunderbolt"};
static const char *const q1_mission_weapons[8] = {
    "hipnotic:laser", "hipnotic:mjolnir", "hipnotic:proximity",
    "rogue:lava-nailgun", "rogue:lava-supernailgun", "rogue:multi-grenade",
    "rogue:multi-rocket", "rogue:plasma"};

typedef struct q2_weapon {
  const char *name;
  const char *item;
} q2_weapon;

static const q2_weapon q2_base[11] = {
    {"blaster", "q2:weapon_blaster"}, {"shotgun", "q2:weapon_shotgun"},
    {"supershotgun", "q2:weapon_supershotgun"},
    {"machinegun", "q2:weapon_machinegun"}, {"chaingun", "q2:weapon_chaingun"},
    {"grenades", "q2:ammo_grenades"},
    {"grenadelauncher", "q2:weapon_grenadelauncher"},
    {"rocketlauncher", "q2:weapon_rocketlauncher"},
    {"hyperblaster", "q2:weapon_hyperblaster"}, {"railgun", "q2:weapon_railgun"},
    {"bfg", "q2:weapon_bfg"}};
static const char *const q2_base_display[11] = {
    "Blaster", "Shotgun", "Super Shotgun", "Machinegun", "Chaingun",
    "Grenades", "Grenade Launcher", "Rocket Launcher", "HyperBlaster",
    "Railgun", "BFG10K"};
static const q2_weapon q2_xatrix[3] = {{"trap", "q2:ammo_trap"},
                                       {"ionripper", "q2:weapon_boomer"},
                                       {"phalanx", "q2:weapon_phalanx"}};
static const q2_weapon q2_rogue[6] = {
    {"tesla", "q2:ammo_tesla"}, {"proxlauncher", "q2:weapon_proxlauncher"},
    {"chainfist", "q2:weapon_chainfist"},
    {"disintegrator", "q2:weapon_disintegrator"},
    {"etf_rifle", "q2:weapon_etf_rifle"}, {"heatbeam", "q2:weapon_plasmabeam"}};
static const struct {
  const char *name;
  const char *display;
} q2_mission_display[7] = {{"ionripper", "Ionripper"}, {"phalanx", "Phalanx"},
                           {"etf_rifle", "ETF Rifle"}, {"heatbeam", "Plasma Beam"},
                           {"chainfist", "Chainfist"},
                           {"disintegrator", "Disruptor"},
                           {"proxlauncher", "Prox Launcher"}};
static const struct {
  const char *classname;
  const char *display;
} q2_mission_ammo[6] = {{"ammo_magslug", "Mag Slug"}, {"ammo_trap", "Trap"},
                        {"ammo_flechettes", "Flechettes"}, {"ammo_prox", "Prox"},
                        {"ammo_tesla", "Tesla"}, {"ammo_disruptor", "Rounds"}};

typedef struct q3_weapon {
  double weapon;
  const char *item;
  const char *label;
} q3_weapon;

static const q3_weapon q3_weapons[13] = {
    {1, "q3:weapon/gauntlet", "Gauntlet"},
    {2, "q3:weapon/machinegun", "Machinegun"},
    {3, "q3:weapon/shotgun", "Shotgun"},
    {4, "q3:weapon/grenadelauncher", "Grenade Launcher"},
    {5, "q3:weapon/rocketlauncher", "Rocket Launcher"},
    {6, "q3:weapon/lightning", "Lightning Gun"},
    {7, "q3:weapon/railgun", "Railgun"},
    {8, "q3:weapon/plasmagun", "Plasma Gun"},
    {9, "q3:weapon/bfg", "BFG10K"},
    {10, "q3:weapon/grapple", "Grappling Hook"},
    {11, "q3:weapon/nailgun", "Nailgun"},
    {12, "q3:weapon/proxlauncher", "Prox Launcher"},
    {13, "q3:weapon/chaingun", "Chaingun"}};

/* TS q1WeaponDisplayName over an arbitrary Q1Weapon string. */
static char *q1_weapon_display_name(const char *weapon) {
  for (int i = 0; i < 8; i++) {
    if (strcmp(weapon, q1_weapons[i]) == 0) return qa_input_dup(q1_display[i]);
  }
  if (strcmp(weapon, "hipnotic:laser") == 0) return qa_input_dup("Laser Cannon");
  if (strcmp(weapon, "hipnotic:mjolnir") == 0) return qa_input_dup("Mjolnir");
  if (strcmp(weapon, "hipnotic:proximity") == 0) return qa_input_dup("Proximity Gun");
  const char *name = strrchr(weapon, ':');
  name = name != NULL ? name + 1 : weapon;
  char *out = qa_input_dup(name);
  if (out == NULL) return NULL;
  for (size_t i = 0; out[i] != '\0'; i++) {
    if (out[i] == '_' || out[i] == '-') out[i] = ' ';
  }
  /* \b\w uppercasing under ASCII word characters. */
  for (size_t i = 0; out[i] != '\0'; i++) {
    unsigned char c = (unsigned char)out[i];
    bool word = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_';
    bool before = false;
    if (i > 0) {
      unsigned char p = (unsigned char)out[i - 1];
      before = (p >= 'A' && p <= 'Z') || (p >= 'a' && p <= 'z') ||
               (p >= '0' && p <= '9') || p == '_';
    }
    if (word && !before && c >= 'a' && c <= 'z') out[i] = (char)(c - 32);
  }
  return out;
}

static const char *q2_base_display_name(const char *item) {
  for (int i = 0; i < 11; i++) {
    if (strcmp(item, q2_base[i].item) == 0) return q2_base_display[i];
  }
  return NULL;
}

static const char *q2_mission_display_name(const char *name) {
  for (int i = 0; i < 7; i++) {
    if (strcmp(name, q2_mission_display[i].name) == 0) {
      return q2_mission_display[i].display;
    }
  }
  char classname[64];
  snprintf(classname, sizeof classname, "ammo_%s", name);
  for (int i = 0; i < 6; i++) {
    if (strcmp(classname, q2_mission_ammo[i].classname) == 0) {
      return q2_mission_ammo[i].display;
    }
  }
  return NULL;
}

typedef struct item_list {
  qa_weapon_binding_item *items;
  size_t count;
  size_t capacity;
} item_list;

static bool push_item(item_list *list, const char *id, const char *label) {
  if (list->count == list->capacity) {
    size_t grown = list->capacity == 0 ? 8 : list->capacity * 2;
    qa_weapon_binding_item *items = (qa_weapon_binding_item *)realloc(
        list->items, grown * sizeof *items);
    if (items == NULL) return false;
    list->items = items;
    list->capacity = grown;
  }
  char *id_copy = qa_input_dup(id);
  char *label_copy = qa_input_dup(label);
  if (id_copy == NULL || label_copy == NULL) {
    free(id_copy);
    free(label_copy);
    return false;
  }
  list->items[list->count].id = id_copy;
  list->items[list->count].label = label_copy;
  list->items[list->count].kind = QA_WEAPON_BINDING_WEAPON;
  list->count++;
  return true;
}

static bool push_q1_weapon(item_list *list, const char *weapon) {
  char id[128];
  snprintf(id, sizeof id, "q1:weapon/%s", weapon);
  char *label = q1_weapon_display_name(weapon);
  if (label == NULL) return false;
  bool ok = push_item(list, id, label);
  free(label);
  return ok;
}

qa_weapon_binding_item *qa_base_weapon_binding_items(qa_game_family family,
                                                     const char *campaign,
                                                     const char *edition,
                                                     size_t *out_count) {
  if (campaign == NULL) campaign = "";
  if (edition == NULL) edition = "classic";
  item_list list = {NULL, 0, 0};
  bool ok = true;
  switch (family) {
    case QA_GAME_FAMILY_Q1: {
      for (int i = 0; i < 8 && ok; i++) ok = push_q1_weapon(&list, q1_weapons[i]);
      if (ok && strcmp(campaign, "mg3") == 0) {
        ok = push_q1_weapon(&list, "mg3:laser") && push_q1_weapon(&list, "mg3:mjolnir");
      }
      for (int i = 0; i < 8 && ok; i++) {
        if ((strcmp(campaign, "hipnotic") == 0 || strcmp(campaign, "rogue") == 0) &&
            strncmp(q1_mission_weapons[i], campaign, strlen(campaign)) == 0 &&
            q1_mission_weapons[i][strlen(campaign)] == ':') {
          ok = push_q1_weapon(&list, q1_mission_weapons[i]);
        }
      }
      break;
    }
    case QA_GAME_FAMILY_Q2: {
      const q2_weapon *extra = NULL;
      size_t extra_count = 0;
      bool rerelease_all = strcmp(edition, "rerelease") == 0 &&
                           (strcmp(campaign, "baseq2") == 0 ||
                            strcmp(campaign, "xatrix") == 0 ||
                            strcmp(campaign, "rogue") == 0 ||
                            strcmp(campaign, "mg2") == 0);
      for (int i = 0; i < 11 && ok; i++) {
        const char *label = q2_base_display_name(q2_base[i].item);
        if (label == NULL) label = q2_mission_display_name(q2_base[i].name);
        if (label == NULL) label = q2_base[i].name;
        ok = push_item(&list, q2_base[i].item, label);
      }
      if (rerelease_all) {
        for (int i = 0; i < 3 && ok; i++) {
          const char *label = q2_base_display_name(q2_xatrix[i].item);
          if (label == NULL) label = q2_mission_display_name(q2_xatrix[i].name);
          if (label == NULL) label = q2_xatrix[i].name;
          ok = push_item(&list, q2_xatrix[i].item, label);
        }
        extra = q2_rogue;
        extra_count = 6;
      } else if (strcmp(campaign, "xatrix") == 0) {
        extra = q2_xatrix;
        extra_count = 3;
      } else if (strcmp(campaign, "rogue") == 0) {
        extra = q2_rogue;
        extra_count = 6;
      }
      for (size_t i = 0; i < extra_count && ok; i++) {
        const char *label = q2_base_display_name(extra[i].item);
        if (label == NULL) label = q2_mission_display_name(extra[i].name);
        if (label == NULL) label = extra[i].name;
        ok = push_item(&list, extra[i].item, label);
      }
      break;
    }
    case QA_GAME_FAMILY_Q3: {
      bool missionpack = strcmp(campaign, "missionpack") == 0;
      for (int i = 0; i < 13 && ok; i++) {
        if (!missionpack && q3_weapons[i].weapon > 10) continue;
        ok = push_item(&list, q3_weapons[i].item, q3_weapons[i].label);
      }
      break;
    }
  }
  if (!ok) {
    qa_weapon_binding_items_free(list.items, list.count);
    *out_count = 0;
    return NULL;
  }
  *out_count = list.count;
  return list.items;
}

void qa_weapon_binding_items_free(qa_weapon_binding_item *items, size_t count) {
  if (items == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free((void *)items[i].id);
    free((void *)items[i].label);
  }
  free(items);
}

typedef struct pair_list {
  qa_weapon_key_binding *pairs;
  size_t count;
  size_t capacity;
} pair_list;

static bool append_binding(pair_list *list, const qa_weapon_binding_item *items,
                           size_t item_count, const char *id, const char *key) {
  for (size_t i = 0; i < list->count; i++) {
    if (strcmp(list->pairs[i].key, key) == 0) return true;
  }
  bool present = false;
  for (size_t i = 0; i < item_count; i++) {
    if (items[i].kind == QA_WEAPON_BINDING_WEAPON && strcmp(items[i].id, id) == 0) {
      present = true;
    }
  }
  if (!present) return true;
  if (list->count == list->capacity) {
    size_t grown = list->capacity == 0 ? 8 : list->capacity * 2;
    qa_weapon_key_binding *pairs =
        (qa_weapon_key_binding *)realloc(list->pairs, grown * sizeof *pairs);
    if (pairs == NULL) return false;
    list->pairs = pairs;
    list->capacity = grown;
  }
  char command[256];
  snprintf(command, sizeof command, "use %s", id);
  char *key_copy = qa_input_dup(key);
  char *command_copy = qa_input_dup(command);
  if (key_copy == NULL || command_copy == NULL) {
    free(key_copy);
    free(command_copy);
    return false;
  }
  list->pairs[list->count].key = key_copy;
  list->pairs[list->count].command = command_copy;
  list->count++;
  return true;
}

qa_weapon_key_binding *qa_default_weapon_bindings(const qa_weapon_binding_item *items,
                                                  size_t item_count,
                                                  size_t *out_count) {
  pair_list list = {NULL, 0, 0};
  bool ok = true;
  char key[16], id[128];
  for (int i = 0; i < 8 && ok; i++) {
    snprintf(id, sizeof id, "q1:weapon/%s", q1_weapons[i]);
    snprintf(key, sizeof key, "%d", i + 1);
    ok = append_binding(&list, items, item_count, id, key);
  }
  if (ok) ok = append_binding(&list, items, item_count, "q1:weapon/hipnotic:laser", "9");
  if (ok) ok = append_binding(&list, items, item_count, "q1:weapon/hipnotic:mjolnir", "0");
  if (ok) ok = append_binding(&list, items, item_count, "q1:weapon/mg3:laser", "9");
  int slot = 1;
  for (int i = 0; i < 11 && ok; i++) {
    if (strcmp(q2_base[i].name, "grenades") == 0) {
      ok = append_binding(&list, items, item_count, q2_base[i].item, "g");
    } else {
      snprintf(key, sizeof key, "%d", slot % 10);
      slot++;
      ok = append_binding(&list, items, item_count, q2_base[i].item, key);
    }
  }
  for (int i = 0; i < 13 && ok; i++) {
    if (q3_weapons[i].weapon <= 10) {
      snprintf(key, sizeof key, "%d", (int)q3_weapons[i].weapon % 10);
      ok = append_binding(&list, items, item_count, q3_weapons[i].item, key);
    }
  }
  if (!ok) {
    qa_weapon_key_bindings_free(list.pairs, list.count);
    *out_count = 0;
    return NULL;
  }
  *out_count = list.count;
  return list.pairs;
}

void qa_weapon_key_bindings_free(qa_weapon_key_binding *bindings, size_t count) {
  if (bindings == NULL) return;
  for (size_t i = 0; i < count; i++) {
    free(bindings[i].key);
    free(bindings[i].command);
  }
  free(bindings);
}

/* TS normalized(): lowercase, then drop every space. */
static char *normalized(const char *value) {
  size_t length = strlen(value);
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  size_t at = 0;
  for (size_t i = 0; i < length; i++) {
    char c = value[i];
    if (c == ' ') continue;
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    out[at++] = c;
  }
  out[at] = '\0';
  return out;
}

static bool normalized_equals(const char *value, const char *wanted_normalized) {
  char *folded = normalized(value);
  if (folded == NULL) return false;
  bool equal = strcmp(folded, wanted_normalized) == 0;
  free(folded);
  return equal;
}

static bool argument_forbidden(const char *argument) {
  if (strstr(argument, "//") != NULL || strstr(argument, "/*") != NULL) return true;
  for (const char *at = argument; *at != '\0'; at++) {
    if (*at == ';' || *at == '\r' || *at == '\n' || *at == '\\' || *at == '"') {
      return true;
    }
  }
  return false;
}

static bool item_matches_use(const qa_weapon_binding_item *item,
                             const char *requested) {
  if (normalized_equals(item->label, requested)) return true;
  /* Q1: the weapon whose weaponItem() id is this item. */
  if (strncmp(item->id, "q1:weapon/", 10) == 0) {
    const char *weapon = item->id + 10;
    bool known = false;
    for (int i = 0; i < 8; i++) known = known || strcmp(weapon, q1_weapons[i]) == 0;
    for (int i = 0; i < 8; i++) {
      known = known || strcmp(weapon, q1_mission_weapons[i]) == 0;
    }
    if (known) {
      if (normalized_equals(weapon, requested)) return true;
      char *display = q1_weapon_display_name(weapon);
      bool equal = display != NULL && normalized_equals(display, requested);
      free(display);
      if (equal) return true;
    }
  }
  /* Q3: the item suffix after "q3:weapon/". */
  for (int i = 0; i < 13; i++) {
    if (strcmp(item->id, q3_weapons[i].item) == 0 &&
        normalized_equals(item->id + 10, requested)) {
      return true;
    }
  }
  /* Q2: definition name or its display label. */
  const q2_weapon *found = NULL;
  const q2_weapon *tables[3] = {q2_base, q2_xatrix, q2_rogue};
  const int counts[3] = {11, 3, 6};
  for (int t = 0; t < 3 && found == NULL; t++) {
    for (int i = 0; i < counts[t]; i++) {
      if (strcmp(item->id, tables[t][i].item) == 0) found = &tables[t][i];
    }
  }
  const char *label = NULL;
  if (found != NULL) {
    label = q2_base_display_name(found->item);
    if (label == NULL) label = q2_mission_display_name(found->name);
  }
  return (found != NULL && normalized_equals(found->name, requested)) ||
         (label != NULL && normalized_equals(label, requested));
}

bool qa_resolve_weapon_selection(const char *command, const char *const *args,
                                 size_t arg_count,
                                 const qa_weapon_binding_item *items,
                                 size_t item_count,
                                 qa_resolved_weapon_selection *out) {
  char *name = qa_input_dup(command);
  if (name == NULL) return false;
  for (char *at = name; *at != '\0'; at++) {
    if (*at >= 'A' && *at <= 'Z') *at = (char)(*at + 32);
  }
  bool is_use = strcmp(name, "use") == 0;
  bool is_weapon = strcmp(name, "weapon") == 0;
  bool is_impulse = strcmp(name, "impulse") == 0;
  free(name);
  if (!is_use && !is_weapon && !is_impulse) return false;
  if (arg_count == 0) return false;
  for (size_t i = 0; i < arg_count; i++) {
    if (argument_forbidden(args[i])) return false;
  }
  if (is_use) {
    size_t total = 0;
    for (size_t i = 0; i < arg_count; i++) total += strlen(args[i]);
    char *joined = (char *)malloc(total + 1);
    if (joined == NULL) return false;
    joined[0] = '\0';
    for (size_t i = 0; i < arg_count; i++) strcat(joined, args[i]);
    char *requested = normalized(joined);
    free(joined);
    if (requested == NULL) return false;
    for (size_t i = 0; i < item_count; i++) {
      if (strcmp(items[i].id, requested) == 0) {
        out->kind = items[i].kind;
        out->item = items[i].id;
        free(requested);
        return true;
      }
    }
    const qa_weapon_binding_item *match = NULL;
    size_t matches = 0;
    for (size_t i = 0; i < item_count; i++) {
      if (item_matches_use(&items[i], requested)) {
        match = &items[i];
        matches++;
      }
    }
    free(requested);
    if (matches != 1 || match == NULL) return false;
    out->kind = match->kind;
    out->item = match->id;
    return true;
  }
  if (arg_count != 1) return false;
  const char *argument = args[0];
  if (argument[0] < '1' || argument[0] > '9') return false;
  for (const char *at = argument + 1; *at != '\0'; at++) {
    if (*at < '0' || *at > '9') return false;
  }
  double number = qa_input_js_parse_number(argument);
  if (is_impulse) {
    for (size_t i = 0; i < item_count; i++) {
      if (items[i].kind != QA_WEAPON_BINDING_WEAPON) continue;
      bool base_q1 = false;
      for (int w = 0; w < 8; w++) {
        char id[128];
        snprintf(id, sizeof id, "q1:weapon/%s", q1_weapons[w]);
        if (strcmp(items[i].id, id) == 0) base_q1 = true;
      }
      if (!base_q1) return false;
    }
  }
  const char *id = NULL;
  char id_buffer[128];
  if (is_weapon) {
    for (int i = 0; i < 13; i++) {
      if (q3_weapons[i].weapon == number) id = q3_weapons[i].item;
    }
  } else if (number >= 1 && number <= 8 && number == (double)(int)number) {
    snprintf(id_buffer, sizeof id_buffer, "q1:weapon/%s", q1_weapons[(int)number - 1]);
    id = id_buffer;
  }
  if (id == NULL) return false;
  for (size_t i = 0; i < item_count; i++) {
    if (items[i].kind == QA_WEAPON_BINDING_WEAPON && strcmp(items[i].id, id) == 0) {
      out->kind = items[i].kind;
      out->item = items[i].id;
      return true;
    }
  }
  return false;
}

static bool space_byte(unsigned char byte) {
  return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\v' ||
         byte == '\f' || byte == '\r' || byte == 160;
}

const char *qa_weapon_binding_item_for_text(const char *text,
                                   const qa_weapon_binding_item *items,
                                   size_t item_count) {
  if (strstr(text, "//") != NULL || strstr(text, "/*") != NULL) return NULL;
  for (const char *at = text; *at != '\0'; at++) {
    if (*at == ';' || *at == '\r' || *at == '\n' || *at == '\\') return NULL;
  }
  const unsigned char *at = (const unsigned char *)text;
  while (space_byte(*at)) at++;
  static const char *const names[3] = {"use", "weapon", "impulse"};
  const char *matched = NULL;
  size_t name_length = 0;
  for (int i = 0; i < 3; i++) {
    size_t length = strlen(names[i]);
    bool equal = true;
    for (size_t j = 0; j < length; j++) {
      unsigned char c = at[j];
      if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
      if (c != (unsigned char)names[i][j]) equal = false;
    }
    if (equal) {
      matched = names[i];
      name_length = length;
      break;
    }
  }
  if (matched == NULL) return NULL;
  at += name_length;
  if (!space_byte(*at)) return NULL;
  while (space_byte(*at)) at++;
  char *argument = NULL;
  if (*at == '"') {
    const unsigned char *start = ++at;
    while (*at != '\0' && *at != '"' && *at != '\r' && *at != '\n') at++;
    if (*at != '"' || at == start) return NULL;
    argument = qa_input_dup_range((const char *)start, (size_t)(at - start));
    at++;
    while (space_byte(*at)) at++;
    if (*at != '\0') {
      free(argument);
      return NULL;
    }
  } else {
    const unsigned char *start = at;
    const unsigned char *end = at + strlen((const char *)at);
    while (end > start && space_byte(end[-1])) end--;
    if (end == start) return NULL;
    for (const unsigned char *scan = start; scan < end; scan++) {
      if (*scan == '"' || *scan == '\r' || *scan == '\n') return NULL;
    }
    argument = qa_input_dup_range((const char *)start, (size_t)(end - start));
  }
  if (argument == NULL) return NULL;
  const char *args[1] = {argument};
  qa_resolved_weapon_selection resolved;
  bool found = qa_resolve_weapon_selection(matched, args, 1, items, item_count,
                                           &resolved);
  free(argument);
  return found ? resolved.item : NULL;
}
