/* Port of src/world/gameplay/pickups.ts. GPL-2.0-or-later. */
#include "qa/world/gameplay/pickups.h"

#include <math.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static void *xcalloc(size_t size) {
  void *value = calloc(1, size != 0 ? size : 1);
  if (value == NULL) abort();
  return value;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static void not_admitted(qa_item_id item) {
  char message[2048];
  (void)snprintf(message, sizeof message, "Pickup destination %s was not admitted", item);
  qa_error(message);
}

qa_pickup_supply_preview qa_preview_pickup_grants(const qa_inventory_entry *inventory,
                                                  size_t inventory_count,
                                                  qa_pickup_grant_plan plan) {
  /* Working copy of entries keyed by item. */
  qa_inventory_entry *entries = xcalloc((inventory_count != 0 ? inventory_count : 1) * sizeof *entries);
  if (inventory_count != 0) memcpy(entries, inventory, inventory_count * sizeof *entries);
  /* Validate destinations. */
  const qa_pickup_ammo_grant *weapon_grants = plan.is_weapon || !plan.weapons_shared_ammo
                                                  ? plan.weapons : NULL;
  size_t weapon_grant_count = weapon_grants != NULL ? plan.weapon_count : 0;
  for (size_t pass = 0; pass < 3; pass++) {
    size_t count = pass == 0 ? weapon_grant_count
                 : pass == 1 ? (plan.weapons_shared_ammo && !plan.is_weapon ? plan.shared_item_count : 0)
                             : plan.ammo_count;
    for (size_t i = 0; i < count; i++) {
      qa_item_id item = pass == 0 ? weapon_grants[i].item
                      : pass == 1 ? plan.shared_items[i] : plan.ammo[i].item;
      bool found = false;
      for (size_t e = 0; e < inventory_count; e++) {
        if (str_eq(entries[e].item, item)) found = true;
      }
      if (!found) not_admitted(item);
    }
  }
  if (!plan.is_weapon && plan.weapons_shared_ammo) {
    for (size_t i = 0; i < plan.shared_item_count; i++) {
      bool found = false;
      for (size_t a = 0; a < plan.ammo_count; a++) {
        if (str_eq(plan.ammo[a].item, plan.shared_items[i])) found = true;
      }
      if (!found) {
        char message[2048];
        (void)snprintf(message, sizeof message, "Shared weapon %s has no ammo grant",
                       plan.shared_items[i]);
        qa_error(message);
      }
    }
  }
  /* give() over the working entries. */
  qa_pickup_supply_preview preview;
  memset(&preview, 0, sizeof preview);
  qa_pickup_ammo_receipt *weapon_receipts =
      xcalloc((weapon_grant_count != 0 ? weapon_grant_count : 1) * sizeof *weapon_receipts);
  qa_pickup_ammo_receipt *ammo_receipts =
      xcalloc((plan.ammo_count != 0 ? plan.ammo_count : 1) * sizeof *ammo_receipts);
  /* Local give closure as a loop over a grant list. */
  if (plan.is_weapon) {
    for (size_t w = 0; w < weapon_grant_count; w++) {
      for (size_t e = 0; e < inventory_count; e++) {
        if (str_eq(entries[e].item, plan.weapons[w].item)) {
          qa_inventory_give_transition transition = qa_inventory_give(entries[e], plan.weapons[w].amount);
          weapon_receipts[w].item = plan.weapons[w].item;
          weapon_receipts[w].before = entries[e].count;
          weapon_receipts[w].given = transition.given;
          if (transition.is_write) entries[e] = transition.entry;
          break;
        }
      }
    }
    for (size_t a = 0; a < plan.ammo_count; a++) {
      for (size_t e = 0; e < inventory_count; e++) {
        if (str_eq(entries[e].item, plan.ammo[a].item)) {
          qa_inventory_give_transition transition = qa_inventory_give(entries[e], plan.ammo[a].amount);
          ammo_receipts[a].item = plan.ammo[a].item;
          ammo_receipts[a].before = entries[e].count;
          ammo_receipts[a].given = transition.given;
          if (transition.is_write) entries[e] = transition.entry;
          break;
        }
      }
    }
    preview.accepted = true;
    preview.weapons = weapon_receipts;
    preview.weapon_count = weapon_grant_count;
    preview.ammo = ammo_receipts;
    preview.ammo_count = plan.ammo_count;
    free(entries);
    return preview;
  }
  for (size_t a = 0; a < plan.ammo_count; a++) {
    for (size_t e = 0; e < inventory_count; e++) {
      if (str_eq(entries[e].item, plan.ammo[a].item)) {
        qa_inventory_give_transition transition = qa_inventory_give(entries[e], plan.ammo[a].amount);
        ammo_receipts[a].item = plan.ammo[a].item;
        ammo_receipts[a].before = entries[e].count;
        ammo_receipts[a].given = transition.given;
        if (transition.is_write) entries[e] = transition.entry;
        break;
      }
    }
  }
  bool accepted = false;
  for (size_t a = 0; a < plan.ammo_count; a++) {
    if (str_eq(plan.acceptance, "positive") ? ammo_receipts[a].given > 0
                                            : ammo_receipts[a].given != 0) accepted = true;
  }
  preview.accepted = accepted;
  preview.ammo = ammo_receipts;
  preview.ammo_count = plan.ammo_count;
  if (!accepted) {
    free(weapon_receipts);
    preview.weapons = NULL;
    preview.weapon_count = 0;
  } else if (!plan.weapons_shared_ammo) {
    for (size_t w = 0; w < weapon_grant_count; w++) {
      bool reused = false;
      for (size_t a = 0; a < plan.ammo_count; a++) {
        if (str_eq(ammo_receipts[a].item, plan.weapons[w].item)) {
          weapon_receipts[w] = ammo_receipts[a];
          reused = true;
          break;
        }
      }
      if (!reused) {
        for (size_t e = 0; e < inventory_count; e++) {
          if (str_eq(entries[e].item, plan.weapons[w].item)) {
            qa_inventory_give_transition transition =
                qa_inventory_give(entries[e], plan.weapons[w].amount);
            weapon_receipts[w].item = plan.weapons[w].item;
            weapon_receipts[w].before = entries[e].count;
            weapon_receipts[w].given = transition.given;
            if (transition.is_write) entries[e] = transition.entry;
            break;
          }
        }
      }
    }
    preview.weapons = weapon_receipts;
    preview.weapon_count = weapon_grant_count;
  } else {
    free(weapon_receipts);
    qa_pickup_ammo_receipt *shared = xcalloc((plan.ammo_count != 0 ? plan.ammo_count : 1) * sizeof *shared);
    size_t shared_count = 0;
    for (size_t a = 0; a < plan.ammo_count; a++) {
      for (size_t i = 0; i < plan.shared_item_count; i++) {
        if (str_eq(plan.shared_items[i], ammo_receipts[a].item)) {
          shared[shared_count++] = ammo_receipts[a];
          break;
        }
      }
    }
    preview.weapons = shared;
    preview.weapon_count = shared_count;
  }
  free(entries);
  return preview;
}
void qa_free_pickup_supply_preview(qa_pickup_supply_preview preview) {
  free((void *)preview.ammo);
  free((void *)preview.weapons);
}

struct qa_shared_pickup_admission {
  qa_shared_pickup_admission_options options;
};

qa_shared_pickup_admission *qa_shared_pickup_admission_create(
    qa_shared_pickup_admission_options options) {
  char message[2048];
  for (int kind = 0; kind < 2; kind++) {
    const qa_pickup_supply_mapping *mappings = kind == 0 ? options.profile.ammo : options.profile.weapons;
    size_t count = kind == 0 ? options.profile.ammo_count : options.profile.weapon_count;
    for (size_t i = 0; i < count; i++) {
      for (size_t j = 0; j < i; j++) {
        if (str_eq(mappings[i].source, mappings[j].source)) {
          (void)snprintf(message, sizeof message, "Duplicate pickup mapping for %s", mappings[i].source);
          qa_error(message);
        }
      }
      for (size_t d = 0; d < mappings[i].destination_count; d++) {
        for (size_t e = d + 1; e < mappings[i].destination_count; e++) {
          if (str_eq(mappings[i].destinations[d], mappings[i].destinations[e])) {
            (void)snprintf(message, sizeof message, "Duplicate pickup destination for %s",
                           mappings[i].source);
            qa_error(message);
          }
        }
      }
    }
  }
  qa_shared_pickup_admission *admission = calloc(1, sizeof *admission);
  if (admission == NULL) return NULL;
  admission->options = options;
  return admission;
}
void qa_shared_pickup_admission_destroy(qa_shared_pickup_admission *admission) { free(admission); }

bool qa_shared_pickup_admission_maps(qa_shared_pickup_admission *admission, const char *kind,
                                     qa_item_id item) {
  const qa_pickup_supply_mapping *mappings =
      str_eq(kind, "ammo") ? admission->options.profile.ammo : admission->options.profile.weapons;
  size_t count = str_eq(kind, "ammo") ? admission->options.profile.ammo_count
                                      : admission->options.profile.weapon_count;
  for (size_t i = 0; i < count; i++) {
    if (str_eq(mappings[i].source, item)) return true;
  }
  return false;
}
static const qa_pickup_supply_mapping *destinations(qa_shared_pickup_admission *admission,
                                                    const char *kind, qa_item_id item) {
  const qa_pickup_supply_mapping *mappings =
      str_eq(kind, "ammo") ? admission->options.profile.ammo : admission->options.profile.weapons;
  size_t count = str_eq(kind, "ammo") ? admission->options.profile.ammo_count
                                      : admission->options.profile.weapon_count;
  for (size_t i = 0; i < count; i++) {
    if (str_eq(mappings[i].source, item)) return &mappings[i];
  }
  char message[2048];
  (void)snprintf(message, sizeof message, "Pickup supply %s has no %s mapping for %s",
                 admission->options.profile.id, kind, item);
  qa_error(message);
}
static void require_entries(qa_shared_pickup_admission *admission, qa_actor_id actor,
                            const qa_item_id *items, size_t count) {
  size_t entry_count = 0;
  const qa_inventory_entry *entries =
      qa_shared_inventory_table_entries(admission->options.inventory, actor, &entry_count);
  for (size_t i = 0; i < count; i++) {
    bool found = false;
    for (size_t e = 0; e < entry_count; e++) {
      if (str_eq(entries[e].item, items[i])) found = true;
    }
    if (!found) not_admitted(items[i]);
  }
}
static qa_pickup_ammo_grant *resolve_ammo(qa_shared_pickup_admission *admission,
                                          const qa_pickup_ammo_grant *offers, size_t offer_count,
                                          bool source_quantity, size_t *out_count) {
  size_t total = 0;
  for (size_t i = 0; i < offer_count; i++) {
    if (!isfinite(offers[i].amount) || (!source_quantity && offers[i].amount < 0))
      qa_range_error("Pickup amount must be finite and nonnegative");
    total += destinations(admission, "ammo", offers[i].item)->destination_count;
  }
  qa_pickup_ammo_grant *grants = xcalloc((total != 0 ? total : 1) * sizeof *grants);
  size_t at = 0;
  for (size_t i = 0; i < offer_count; i++) {
    const qa_pickup_supply_mapping *mapping = destinations(admission, "ammo", offers[i].item);
    for (size_t d = 0; d < mapping->destination_count; d++) {
      grants[at].item = mapping->destinations[d];
      grants[at].amount = offers[i].amount;
      at++;
    }
  }
  *out_count = total;
  return grants;
}

typedef struct quantities_result {
  qa_pickup_ammo_grant *ammo;
  size_t count;
  bool *exact;
  bool has_accepted;
  bool accepted;
} quantities_result;
static quantities_result apply_quantities(qa_shared_pickup_admission *admission, qa_actor_id actor,
                                          const qa_pickup_ammo_grant *grants, size_t count,
                                          qa_pickup_quantity_fn resolve, void *resolve_ctx) {
  quantities_result result;
  result.ammo = xcalloc((count != 0 ? count : 1) * sizeof *result.ammo);
  result.exact = xcalloc((count != 0 ? count : 1) * sizeof *result.exact);
  result.count = count;
  result.has_accepted = false;
  result.accepted = false;
  for (size_t i = 0; i < count; i++) {
    if (resolve == NULL) { result.ammo[i] = grants[i]; continue; }
    size_t entry_count = 0;
    const qa_inventory_entry *entries =
        qa_shared_inventory_table_entries(admission->options.inventory, actor, &entry_count);
    bool found = false;
    qa_inventory_entry entry;
    for (size_t e = 0; e < entry_count; e++) {
      if (str_eq(entries[e].item, grants[i].item)) { found = true; entry = entries[e]; break; }
    }
    if (!found) qa_error("Source quantity requires an admitted destination pool");
    qa_pickup_quantity_result quantity = resolve(resolve_ctx, entry);
    if (!isfinite(quantity.amount) || (!quantity.is_source && quantity.amount < 0))
      qa_range_error("Original pickup quantity is invalid");
    if (quantity.is_source) {
      result.exact[i] = true;
      result.accepted = result.accepted || quantity.accepted;
      result.has_accepted = true;
    }
    result.ammo[i].item = grants[i].item;
    result.ammo[i].amount = quantity.amount;
  }
  return result;
}
static qa_pickup_ammo_receipt *give_ammo(qa_shared_pickup_admission *admission,
                                         qa_owned_actor actor, const qa_pickup_ammo_grant *grants,
                                         size_t count, const bool *exact) {
  qa_pickup_ammo_receipt *receipts = xcalloc((count != 0 ? count : 1) * sizeof *receipts);
  for (size_t i = 0; i < count; i++) {
    double before = qa_shared_inventory_table_count(admission->options.inventory, actor.id,
                                                    grants[i].item);
    receipts[i].item = grants[i].item;
    receipts[i].before = before;
    if (exact == NULL || !exact[i]) {
      receipts[i].given = qa_shared_inventory_table_give(admission->options.inventory, actor,
                                                         grants[i].item, grants[i].amount);
    } else {
      size_t entry_count = 0;
      const qa_inventory_entry *entries =
          qa_shared_inventory_table_entries(admission->options.inventory, actor.id, &entry_count);
      bool found = false;
      qa_inventory_entry entry;
      for (size_t e = 0; e < entry_count; e++) {
        if (str_eq(entries[e].item, grants[i].item)) { found = true; entry = entries[e]; break; }
      }
      if (!found) qa_error("Original pickup lost its admitted destination");
      entry.count = before + grants[i].amount;
      qa_shared_inventory_table_configure(admission->options.inventory, actor, entry, NULL, NULL);
      receipts[i].given =
          qa_shared_inventory_table_count(admission->options.inventory, actor.id, grants[i].item) -
          before;
    }
  }
  return receipts;
}

static bool grant_ammo_impl(qa_shared_pickup_admission *admission, qa_owned_actor actor,
                            const qa_pickup_ammo_grant *ammo, size_t ammo_count, bool auto_switch,
                            qa_pickup_quantity_fn quantity, void *quantity_ctx) {
  quantities_result grants = apply_quantities(admission, actor.id, ammo, ammo_count, quantity,
                                              quantity_ctx);
  qa_item_id *items = xcalloc((grants.count != 0 ? grants.count : 1) * sizeof *items);
  for (size_t i = 0; i < grants.count; i++) items[i] = grants.ammo[i].item;
  require_entries(admission, actor.id, items, grants.count);
  free(items);
  qa_pickup_ammo_receipt *receipt = give_ammo(admission, actor, grants.ammo, grants.count,
                                              grants.exact);
  bool accepted = grants.has_accepted ? grants.accepted : false;
  if (!grants.has_accepted) {
    for (size_t i = 0; i < grants.count; i++) {
      if (receipt[i].given > 0) accepted = true;
    }
  }
  free(grants.ammo);
  free(grants.exact);
  if (!accepted) { free(receipt); return false; }
  admission->options.ammo_granted(admission->options.ctx, actor, receipt, grants.count, auto_switch);
  free(receipt);
  return true;
}
static bool grant_ammo_weapon_impl(qa_shared_pickup_admission *admission, qa_owned_actor actor,
                                   const qa_item_id *weapons, size_t weapon_count,
                                   const qa_pickup_ammo_grant *ammo, size_t ammo_count,
                                   qa_ammo_weapon_selection selection,
                                   qa_pickup_quantity_fn quantity, void *quantity_ctx) {
  quantities_result grants = apply_quantities(admission, actor.id, ammo, ammo_count, quantity,
                                              quantity_ctx);
  qa_item_id *items = xcalloc((weapon_count + grants.count != 0 ? weapon_count + grants.count : 1) * sizeof *items);
  if (weapon_count != 0) memcpy(items, weapons, weapon_count * sizeof *items);
  for (size_t i = 0; i < grants.count; i++) items[weapon_count + i] = grants.ammo[i].item;
  require_entries(admission, actor.id, items, weapon_count + grants.count);
  free(items);
  qa_pickup_ammo_receipt *receipt = give_ammo(admission, actor, grants.ammo, grants.count,
                                              grants.exact);
  bool accepted = grants.has_accepted ? grants.accepted : false;
  if (!grants.has_accepted) {
    for (size_t i = 0; i < grants.count; i++) {
      if (receipt[i].given > 0) accepted = true;
    }
  }
  if (!accepted) {
    free(receipt);
    free(grants.ammo);
    free(grants.exact);
    return false;
  }
  for (size_t w = 0; w < weapon_count; w++) {
    bool in_ammo = false;
    for (size_t i = 0; i < grants.count; i++) {
      if (str_eq(grants.ammo[i].item, weapons[w])) in_ammo = true;
    }
    if (!in_ammo)
      (void)qa_shared_inventory_table_give(admission->options.inventory, actor, weapons[w], 1);
  }
  bool select = str_eq(selection.when, "always");
  if (!select) {
    for (size_t i = 0; i < grants.count; i++) {
      if (receipt[i].before == 0 && receipt[i].given > 0) select = true;
    }
  }
  admission->options.weapon_granted(admission->options.ctx, actor, weapons, weapon_count,
                                    select ? selection.mode : QA_PICKUP_SELECTION_NEVER);
  free(receipt);
  free(grants.ammo);
  free(grants.exact);
  return true;
}
static bool give_resolved_cargo(qa_shared_pickup_admission *admission, qa_owned_actor actor,
                                const qa_item_id *weapons, size_t weapon_count,
                                const qa_pickup_ammo_grant *ammo, size_t ammo_count,
                                qa_pickup_selection selection, qa_pickup_quantity_fn quantity,
                                void *quantity_ctx) {
  qa_item_id *items = xcalloc((weapon_count + ammo_count != 0 ? weapon_count + ammo_count : 1) * sizeof *items);
  if (weapon_count != 0) memcpy(items, weapons, weapon_count * sizeof *items);
  for (size_t i = 0; i < ammo_count; i++) items[weapon_count + i] = ammo[i].item;
  require_entries(admission, actor.id, items, weapon_count + ammo_count);
  free(items);
  quantities_result grants = apply_quantities(admission, actor.id, ammo, ammo_count, quantity,
                                              quantity_ctx);
  for (size_t w = 0; w < weapon_count; w++)
    (void)qa_shared_inventory_table_give(admission->options.inventory, actor, weapons[w], 1);
  qa_pickup_ammo_receipt *receipt = give_ammo(admission, actor, grants.ammo, grants.count,
                                              grants.exact);
  free(receipt);
  free(grants.ammo);
  free(grants.exact);
  if (weapon_count != 0)
    admission->options.weapon_granted(admission->options.ctx, actor, weapons, weapon_count,
                                      selection);
  return true;
}
static bool give_cargo_impl(qa_shared_pickup_admission *admission, qa_owned_actor actor,
                            const qa_pickup_cargo_entry *cargo, size_t cargo_count,
                            qa_pickup_selection selection, qa_pickup_quantity_fn quantity,
                            void *quantity_ctx) {
  bool invalid = false;
  for (size_t i = 0; i < cargo_count; i++) {
    if (!isfinite(cargo[i].count) || (str_eq(cargo[i].kind, "weapon") && cargo[i].count != 1))
      invalid = true;
    for (size_t j = i + 1; j < cargo_count; j++) {
      if (str_eq(cargo[i].item, cargo[j].item)) invalid = true;
    }
  }
  if (invalid) qa_error("Invalid pickup cargo");
  qa_item_id *weapons = NULL;
  size_t weapon_count = 0, weapon_capacity = 0;
  qa_pickup_ammo_grant *counters = xcalloc((cargo_count != 0 ? cargo_count : 1) * sizeof *counters);
  size_t counter_count = 0;
  for (size_t i = 0; i < cargo_count; i++) {
    if (str_eq(cargo[i].kind, "weapon")) {
      const qa_pickup_supply_mapping *mapping = destinations(admission, "weapons", cargo[i].item);
      for (size_t d = 0; d < mapping->destination_count; d++) {
        bool seen = false;
        for (size_t w = 0; w < weapon_count; w++) {
          if (str_eq(weapons[w], mapping->destinations[d])) seen = true;
        }
        if (!seen) {
          if (weapon_count == weapon_capacity) {
            weapon_capacity = weapon_capacity != 0 ? weapon_capacity * 2 : 8;
            weapons = xrealloc(weapons, weapon_capacity * sizeof *weapons);
          }
          weapons[weapon_count++] = mapping->destinations[d];
        }
      }
    } else {
      counters[counter_count].item = cargo[i].item;
      counters[counter_count].amount = cargo[i].count;
      counter_count++;
    }
  }
  size_t mapped_count = 0;
  qa_pickup_ammo_grant *mapped = resolve_ammo(admission, counters, counter_count,
                                              quantity != NULL, &mapped_count);
  free(counters);
  bool result = give_resolved_cargo(admission, actor, weapons, weapon_count, mapped, mapped_count,
                                    selection, quantity, quantity_ctx);
  free(weapons);
  free(mapped);
  return result;
}

qa_pickup_supply_preview qa_shared_pickup_admission_preview(
    qa_shared_pickup_admission *admission, qa_actor_id actor, qa_pickup_supply_offer offer) {
  const qa_pickup_supply_mapping *weapon_mapping = NULL;
  if (offer.kind != QA_PICKUP_SUPPLY_AMMO) {
    weapon_mapping = destinations(admission, "weapons",
                                  offer.kind == QA_PICKUP_SUPPLY_WEAPON ? offer.item : offer.weapon);
  }
  qa_pickup_ammo_grant single[1];
  const qa_pickup_ammo_grant *offers = offer.ammo;
  size_t offer_count = offer.ammo_count;
  if (offer.kind != QA_PICKUP_SUPPLY_WEAPON) {
    single[0] = offer.grant;
    offers = single;
    offer_count = 1;
  }
  size_t ammo_count = 0;
  qa_pickup_ammo_grant *ammo = resolve_ammo(admission, offers, offer_count, false, &ammo_count);
  size_t entry_count = 0;
  const qa_inventory_entry *entries =
      qa_shared_inventory_table_entries(admission->options.inventory, actor, &entry_count);
  size_t weapon_count = weapon_mapping != NULL ? weapon_mapping->destination_count : 0;
  qa_pickup_ammo_grant *weapon_grants = xcalloc((weapon_count != 0 ? weapon_count : 1) * sizeof *weapon_grants);
  for (size_t i = 0; i < weapon_count; i++) {
    weapon_grants[i].item = weapon_mapping->destinations[i];
    weapon_grants[i].amount = 1;
  }
  qa_pickup_grant_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.is_weapon = offer.kind == QA_PICKUP_SUPPLY_WEAPON;
  plan.weapons = weapon_grants;
  plan.weapon_count = weapon_count;
  plan.ammo = ammo;
  plan.ammo_count = ammo_count;
  plan.acceptance = "positive";
  qa_pickup_supply_preview preview = qa_preview_pickup_grants(entries, entry_count, plan);
  free(weapon_grants);
  free(ammo);
  return preview;
}
bool qa_shared_pickup_admission_owns(qa_shared_pickup_admission *admission, qa_actor_id actor,
                                     qa_item_id source_weapon) {
  const qa_pickup_supply_mapping *mapping = destinations(admission, "weapons", source_weapon);
  for (size_t i = 0; i < mapping->destination_count; i++) {
    if (!(qa_shared_inventory_table_count(admission->options.inventory, actor,
                                          mapping->destinations[i]) > 0)) return false;
  }
  return true;
}
bool qa_shared_pickup_admission_ammo(qa_shared_pickup_admission *admission,
                                     qa_owned_actor actor, qa_pickup_ammo_grant offer,
                                     bool auto_switch, qa_pickup_quantity_fn quantity,
                                     void *quantity_ctx) {
  size_t count = 0;
  qa_pickup_ammo_grant *ammo = resolve_ammo(admission, &offer, 1, quantity != NULL, &count);
  bool result = grant_ammo_impl(admission, actor, ammo, count, auto_switch, quantity, quantity_ctx);
  free(ammo);
  return result;
}
bool qa_shared_pickup_admission_ammo_weapon(qa_shared_pickup_admission *admission,
                                            qa_owned_actor actor,
                                            qa_pickup_ammo_weapon_offer offer,
                                            qa_ammo_weapon_selection selection,
                                            qa_pickup_quantity_fn quantity, void *quantity_ctx) {
  const qa_pickup_supply_mapping *mapping = destinations(admission, "weapons", offer.weapon);
  size_t count = 0;
  qa_pickup_ammo_grant *ammo = resolve_ammo(admission, &offer.grant, 1, quantity != NULL, &count);
  bool result = grant_ammo_weapon_impl(admission, actor, mapping->destinations,
                                       mapping->destination_count, ammo, count, selection,
                                       quantity, quantity_ctx);
  free(ammo);
  return result;
}
bool qa_shared_pickup_admission_weapon(qa_shared_pickup_admission *admission,
                                       qa_owned_actor actor, qa_pickup_weapon_offer offer,
                                       qa_pickup_selection selection,
                                       qa_pickup_quantity_fn quantity, void *quantity_ctx) {
  qa_pickup_cargo_entry *cargo = xcalloc((offer.ammo_count + 1) * sizeof *cargo);
  cargo[0].kind = "weapon";
  cargo[0].item = offer.item;
  cargo[0].count = 1;
  for (size_t i = 0; i < offer.ammo_count; i++) {
    cargo[i + 1].kind = "counter";
    cargo[i + 1].item = offer.ammo[i].item;
    cargo[i + 1].count = offer.ammo[i].amount;
  }
  bool result = give_cargo_impl(admission, actor, cargo, offer.ammo_count + 1, selection,
                                quantity, quantity_ctx);
  free(cargo);
  return result;
}
void qa_shared_pickup_admission_select_weapon(qa_shared_pickup_admission *admission,
                                              qa_owned_actor actor, qa_item_id item,
                                              qa_pickup_selection selection) {
  const qa_pickup_supply_mapping *mapping = destinations(admission, "weapons", item);
  require_entries(admission, actor.id, mapping->destinations, mapping->destination_count);
  admission->options.weapon_granted(admission->options.ctx, actor, mapping->destinations,
                                    mapping->destination_count, selection);
}
bool qa_shared_pickup_admission_cargo(qa_shared_pickup_admission *admission,
                                      qa_owned_actor actor, const qa_pickup_cargo_entry *cargo,
                                      size_t cargo_count, qa_pickup_selection selection) {
  return give_cargo_impl(admission, actor, cargo, cargo_count, selection, NULL, NULL);
}
bool qa_shared_pickup_admission_canonical(qa_shared_pickup_admission *admission,
                                          qa_owned_actor actor, qa_pickup_supply_offer offer,
                                          qa_pickup_selection selection,
                                          qa_pickup_quantity_fn quantity, void *quantity_ctx) {
  if (offer.kind == QA_PICKUP_SUPPLY_AMMO)
    return grant_ammo_impl(admission, actor, &offer.grant, 1, true, quantity, quantity_ctx);
  if (offer.kind == QA_PICKUP_SUPPLY_AMMO_WEAPON) {
    qa_ammo_weapon_selection ammo_selection;
    ammo_selection.mode = selection;
    ammo_selection.when = "empty-ammo";
    qa_pickup_ammo_grant grant = offer.grant;
    qa_item_id weapons[1] = {offer.weapon};
    return grant_ammo_weapon_impl(admission, actor, weapons, 1, &grant, 1, ammo_selection,
                                  quantity, quantity_ctx);
  }
  qa_item_id weapons[1] = {offer.item};
  return give_resolved_cargo(admission, actor, weapons, 1, offer.ammo, offer.ammo_count,
                             selection, quantity, quantity_ctx);
}

static bool c_maps(void *ctx, const char *kind, qa_item_id item) {
  return qa_shared_pickup_admission_maps(ctx, kind, item);
}
static qa_pickup_supply_preview c_preview(void *ctx, qa_actor_id actor,
                                          qa_pickup_supply_offer offer) {
  return qa_shared_pickup_admission_preview(ctx, actor, offer);
}
static bool c_owns(void *ctx, qa_actor_id actor, qa_item_id source_weapon) {
  return qa_shared_pickup_admission_owns(ctx, actor, source_weapon);
}
static bool c_ammo(void *ctx, qa_owned_actor actor, qa_pickup_ammo_grant offer,
                   bool has_auto_switch, bool auto_switch) {
  return qa_shared_pickup_admission_ammo(ctx, actor, offer, has_auto_switch ? auto_switch : true,
                                         NULL, NULL);
}
static bool c_ammo_weapon(void *ctx, qa_owned_actor actor, qa_pickup_ammo_weapon_offer offer,
                          qa_ammo_weapon_selection selection) {
  return qa_shared_pickup_admission_ammo_weapon(ctx, actor, offer, selection, NULL, NULL);
}
static bool c_weapon(void *ctx, qa_owned_actor actor, qa_pickup_weapon_offer offer,
                     qa_pickup_selection selection) {
  return qa_shared_pickup_admission_weapon(ctx, actor, offer, selection, NULL, NULL);
}
static bool c_cargo(void *ctx, qa_owned_actor actor, const qa_pickup_cargo_entry *cargo,
                    size_t cargo_count, qa_pickup_selection selection) {
  return qa_shared_pickup_admission_cargo(ctx, actor, cargo, cargo_count, selection);
}
qa_pickup_admission qa_shared_pickup_admission_as_contract(qa_shared_pickup_admission *admission) {
  qa_pickup_admission table;
  table.ctx = admission;
  table.maps = c_maps;
  table.preview = c_preview;
  table.owns = c_owns;
  table.ammo = c_ammo;
  table.ammo_weapon = c_ammo_weapon;
  table.weapon = c_weapon;
  table.cargo = c_cargo;
  return table;
}

qa_weapon_source_entry *qa_selected_weapon_sources(
    qa_pickup_supply_profile profile, const qa_selected_pickup_weapon *selected,
    size_t selected_count, const qa_pickup_weapon *original, size_t original_count,
    size_t *out_count) {
  /* Validate declared weapon owners. */
  char message[2048];
  for (size_t i = 0; i < profile.weapon_owner_count; i++) {
    bool duplicate = false;
    for (size_t j = 0; j < i; j++) {
      if (str_eq(profile.weapon_owners[j].item, profile.weapon_owners[i].item)) duplicate = true;
    }
    bool valid = false;
    for (size_t m = 0; m < profile.weapon_count; m++) {
      if (str_eq(profile.weapons[m].source, profile.weapon_owners[i].source)) {
        for (size_t d = 0; d < profile.weapons[m].destination_count; d++) {
          if (str_eq(profile.weapons[m].destinations[d], profile.weapon_owners[i].item)) valid = true;
        }
      }
    }
    if (duplicate || !valid) {
      (void)snprintf(message, sizeof message,
                     "Selected weapon %s has an invalid or duplicate original owner in %s",
                     profile.weapon_owners[i].item, profile.id);
      qa_error(message);
    }
  }
  qa_weapon_source_entry *result = xcalloc((selected_count != 0 ? selected_count : 1) * sizeof *result);
  for (size_t s = 0; s < selected_count; s++) {
    const qa_selected_pickup_weapon *weapon = &selected[s];
    result[s].item = weapon->item;
    result[s].source = NULL;
    if (str_eq(weapon->drop, "none")) continue;
    bool is_original = false;
    for (size_t o = 0; o < original_count; o++) {
      if (str_eq(original[o].item, weapon->item)) is_original = true;
    }
    if (is_original) { result[s].source = weapon->item; continue; }
    const qa_pickup_supply_owner *owner = NULL;
    for (size_t i = 0; i < profile.weapon_owner_count; i++) {
      if (str_eq(profile.weapon_owners[i].item, weapon->item)) owner = &profile.weapon_owners[i];
    }
    if (owner != NULL) {
      bool available = false;
      for (size_t o = 0; o < original_count; o++) {
        if (str_eq(original[o].item, owner->source)) available = true;
      }
      if (!available) {
        (void)snprintf(message, sizeof message,
                       "Selected weapon %s names an unavailable original owner %s",
                       weapon->item, owner->source);
        qa_error(message);
      }
      result[s].source = owner->source;
      continue;
    }
    const qa_pickup_weapon **candidates =
        xcalloc((original_count != 0 ? original_count : 1) * sizeof *candidates);
    size_t candidate_count = 0;
    for (size_t o = 0; o < original_count; o++) {
      bool maps = false;
      for (size_t m = 0; m < profile.weapon_count; m++) {
        if (str_eq(profile.weapons[m].source, original[o].item)) {
          for (size_t d = 0; d < profile.weapons[m].destination_count; d++) {
            if (str_eq(profile.weapons[m].destinations[d], weapon->item)) maps = true;
          }
        }
      }
      if (maps) candidates[candidate_count++] = &original[o];
    }
    if (candidate_count == 0 && weapon->ammo != NULL) {
      for (size_t o = 0; o < original_count; o++) {
        bool maps = false;
        for (size_t m = 0; m < profile.ammo_count; m++) {
          if (str_eq(profile.ammo[m].source, original[o].ammo)) {
            for (size_t d = 0; d < profile.ammo[m].destination_count; d++) {
              if (str_eq(profile.ammo[m].destinations[d], weapon->ammo)) maps = true;
            }
          }
        }
        if (maps) candidates[candidate_count++] = &original[o];
      }
    }
    if (candidate_count > 1 && weapon->ammo != NULL) {
      const qa_pickup_supply_owner *ammo_owner = NULL;
      for (size_t i = 0; i < profile.ammo_owner_count; i++) {
        if (str_eq(profile.ammo_owners[i].item, weapon->ammo)) ammo_owner = &profile.ammo_owners[i];
      }
      size_t canonical_count = 0;
      const qa_pickup_weapon *canonical = NULL;
      for (size_t o = 0; o < original_count; o++) {
        if (ammo_owner == NULL || !str_eq(original[o].ammo, ammo_owner->source)) continue;
        bool maps = false;
        for (size_t m = 0; m < profile.ammo_count; m++) {
          if (str_eq(profile.ammo[m].source, ammo_owner->source)) {
            for (size_t d = 0; d < profile.ammo[m].destination_count; d++) {
              if (str_eq(profile.ammo[m].destinations[d], weapon->ammo)) maps = true;
            }
          }
        }
        if (maps) { canonical_count++; canonical = &original[o]; }
      }
      if (canonical_count == 1) {
        candidates[0] = canonical;
        candidate_count = 1;
      }
    }
    if (candidate_count != 1) {
      if (candidate_count == 0) {
        (void)snprintf(message, sizeof message,
                       "Selected weapon %s has no original drop mapping in %s",
                       weapon->item, profile.id);
      } else {
        char list[1024];
        list[0] = '\0';
        for (size_t c = 0; c < candidate_count; c++) {
          if (c != 0) strncat(list, ", ", sizeof list - strlen(list) - 1);
          strncat(list, candidates[c]->item, sizeof list - strlen(list) - 1);
        }
        (void)snprintf(message, sizeof message,
                       "Selected weapon %s has ambiguous original drop mapping in %s: %s",
                       weapon->item, profile.id, list);
      }
      free((void *)candidates);
      qa_error(message);
    }
    result[s].source = candidates[0]->item;
    free((void *)candidates);
  }
  *out_count = selected_count;
  return result;
}
