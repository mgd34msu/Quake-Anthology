/* Port of src/input/weapon-bindings.ts. GPL-2.0-or-later. */
#include "qa/input/weapon_bindings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/q1/foundation/types.h"
#include "qa/content/q1/foundation/weapon_names.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/definitions.h"
#include "qa/content/q2/missionpacks/items.h"
#include "qa/content/q2/missionpacks/weapons/definitions.h"
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/input/support.h"

/*
 * Weapon catalog data comes from the landed content ports (TS
 * owners noted per use): Q1 WEAPONS/weaponItem and
 * q1WeaponDisplayName, missionWeapons, Q2 base/mission weapon
 * definitions and display names, Q3_WEAPON_ITEMS and itemList
 * pickup names. Only name/item/label fields are read here.
 */

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
  char label[128];
  const char *display =
      qa_q1_weapon_display_name(weapon, label, sizeof label);
  return push_item(list, qa_q1_weapon_item(weapon), display);
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
      for (int i = 0; i < 8 && ok; i++) ok = push_q1_weapon(&list, QA_Q1_WEAPONS[i]);
      if (ok && strcmp(campaign, "mg3") == 0) {
        ok = push_q1_weapon(&list, "mg3:laser") && push_q1_weapon(&list, "mg3:mjolnir");
      }
      for (int i = 0; i < 8 && ok; i++) {
        if ((strcmp(campaign, "hipnotic") == 0 || strcmp(campaign, "rogue") == 0) &&
            strncmp(qa_q1mp_mission_weapons[i].id, campaign, strlen(campaign)) == 0 &&
            qa_q1mp_mission_weapons[i].id[strlen(campaign)] == ':') {
          ok = push_q1_weapon(&list, qa_q1mp_mission_weapons[i].id);
        }
      }
      break;
    }
    case QA_GAME_FAMILY_Q2: {
      const qa_q2w_definition *extra = NULL;
      size_t extra_count = 0;
      bool rerelease_all = strcmp(edition, "rerelease") == 0 &&
                           (strcmp(campaign, "baseq2") == 0 ||
                            strcmp(campaign, "xatrix") == 0 ||
                            strcmp(campaign, "rogue") == 0 ||
                            strcmp(campaign, "mg2") == 0);
      for (size_t i = 0; i < qa_q2w_base_weapon_count && ok; i++) {
        const char *label = qa_q2_base_weapon_display_name(qa_q2w_base_weapons[i].item);
        if (label == NULL) label = qa_q2mp_mission_weapon_display_name(qa_q2w_base_weapons[i].name);
        if (label == NULL) label = qa_q2w_base_weapons[i].name;
        ok = push_item(&list, qa_q2w_base_weapons[i].item, label);
      }
      if (rerelease_all) {
        for (size_t i = 0; i < qa_q2mp_xatrix_weapon_definition_count && ok; i++) {
          const char *label = qa_q2_base_weapon_display_name(qa_q2mp_xatrix_weapon_definitions[i].item);
          if (label == NULL) label = qa_q2mp_mission_weapon_display_name(qa_q2mp_xatrix_weapon_definitions[i].name);
          if (label == NULL) label = qa_q2mp_xatrix_weapon_definitions[i].name;
          ok = push_item(&list, qa_q2mp_xatrix_weapon_definitions[i].item, label);
        }
        extra = qa_q2mp_rogue_weapon_definitions;
        extra_count = qa_q2mp_rogue_weapon_definition_count;
      } else if (strcmp(campaign, "xatrix") == 0) {
        extra = qa_q2mp_xatrix_weapon_definitions;
        extra_count = qa_q2mp_xatrix_weapon_definition_count;
      } else if (strcmp(campaign, "rogue") == 0) {
        extra = qa_q2mp_rogue_weapon_definitions;
        extra_count = qa_q2mp_rogue_weapon_definition_count;
      }
      for (size_t i = 0; i < extra_count && ok; i++) {
        const char *label = qa_q2_base_weapon_display_name(extra[i].item);
        if (label == NULL) label = qa_q2mp_mission_weapon_display_name(extra[i].name);
        if (label == NULL) label = extra[i].name;
        ok = push_item(&list, extra[i].item, label);
      }
      break;
    }
    case QA_GAME_FAMILY_Q3: {
      bool missionpack = strcmp(campaign, "missionpack") == 0;
      qa_q3_base_product product = missionpack
          ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
      for (int i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && ok; i++) {
        if (!missionpack && qa_q3_weapon_items[i].weapon > 10) continue;
        /* itemList(product).find(IT_WEAPON, tag)?.pickupName. */
        const char *label = "Grappling Hook";
        {
          size_t row_count = 0;
          const qa_q3_base_item_definition *rows =
              qa_q3_base_item_list(product, &row_count);
          for (size_t r = 0; r < row_count; r++) {
            if (rows[r].type == QA_Q3_BASE_IT_WEAPON &&
                rows[r].tag == qa_q3_weapon_items[i].weapon) {
              if (rows[r].pickup_name != NULL) label = rows[r].pickup_name;
              break;
            }
          }
        }
        ok = push_item(&list, qa_q3_weapon_items[i].item, label);
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
  char key[16];
  for (int i = 0; i < 8 && ok; i++) {
    snprintf(key, sizeof key, "%d", i + 1);
    ok = append_binding(&list, items, item_count,
                        qa_q1_weapon_item(QA_Q1_WEAPONS[i]), key);
  }
  if (ok) ok = append_binding(&list, items, item_count, qa_q1_weapon_item("hipnotic:laser"), "9");
  if (ok) ok = append_binding(&list, items, item_count, qa_q1_weapon_item("hipnotic:mjolnir"), "0");
  if (ok) ok = append_binding(&list, items, item_count, qa_q1_weapon_item("mg3:laser"), "9");
  int slot = 1;
  for (size_t i = 0; i < qa_q2w_base_weapon_count && ok; i++) {
    if (strcmp(qa_q2w_base_weapons[i].name, "grenades") == 0) {
      ok = append_binding(&list, items, item_count, qa_q2w_base_weapons[i].item, "g");
    } else {
      snprintf(key, sizeof key, "%d", slot % 10);
      slot++;
      ok = append_binding(&list, items, item_count, qa_q2w_base_weapons[i].item, key);
    }
  }
  for (int i = 0; i < QA_Q3_WEAPON_ITEM_COUNT && ok; i++) {
    if (qa_q3_weapon_items[i].weapon <= 10) {
      snprintf(key, sizeof key, "%d", (int)qa_q3_weapon_items[i].weapon % 10);
      ok = append_binding(&list, items, item_count, qa_q3_weapon_items[i].item, key);
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
    for (int i = 0; i < 8; i++) known = known || strcmp(weapon, QA_Q1_WEAPONS[i]) == 0;
    for (int i = 0; i < QA_Q1MP_MISSION_WEAPONS_COUNT; i++) {
      known = known || strcmp(weapon, qa_q1mp_mission_weapons[i].id) == 0;
    }
    if (known) {
      if (normalized_equals(weapon, requested)) return true;
      char display[128];
      const char *name =
          qa_q1_weapon_display_name(weapon, display, sizeof display);
      if (normalized_equals(name, requested)) return true;
    }
  }
  /* Q3: the item suffix after "q3:weapon/". */
  for (int i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
    if (strcmp(item->id, qa_q3_weapon_items[i].item) == 0 &&
        normalized_equals(item->id + 10, requested)) {
      return true;
    }
  }
  /* Q2: definition name or its display label. */
  const qa_q2w_definition *found = NULL;
  const qa_q2w_definition *tables[3] = {qa_q2w_base_weapons,
      qa_q2mp_xatrix_weapon_definitions, qa_q2mp_rogue_weapon_definitions};
  const size_t counts[3] = {qa_q2w_base_weapon_count,
      qa_q2mp_xatrix_weapon_definition_count,
      qa_q2mp_rogue_weapon_definition_count};
  for (int t = 0; t < 3 && found == NULL; t++) {
    for (size_t i = 0; i < counts[t]; i++) {
      if (strcmp(item->id, tables[t][i].item) == 0) found = &tables[t][i];
    }
  }
  const char *label = NULL;
  if (found != NULL) {
    label = qa_q2_base_weapon_display_name(found->item);
    if (label == NULL) label = qa_q2mp_mission_weapon_display_name(found->name);
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
        snprintf(id, sizeof id, "q1:weapon/%s", QA_Q1_WEAPONS[w]);
        if (strcmp(items[i].id, id) == 0) base_q1 = true;
      }
      if (!base_q1) return false;
    }
  }
  const char *id = NULL;
  char id_buffer[128];
  if (is_weapon) {
    for (int i = 0; i < QA_Q3_WEAPON_ITEM_COUNT; i++) {
      if (qa_q3_weapon_items[i].weapon == number) id = qa_q3_weapon_items[i].item;
    }
  } else if (number >= 1 && number <= 8 && number == (double)(int)number) {
    snprintf(id_buffer, sizeof id_buffer, "q1:weapon/%s", QA_Q1_WEAPONS[(int)number - 1]);
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
