/* Port of src/content/mods/qvm-callbacks.ts. GPL-2.0-or-later. */
#include "qa/content/mods/qvm_callbacks.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/client_input.h"
#include "qa/content/mods/client_outputs.h"
#include "qa/content/mods/match.h"
#include "qa/content/mods/pickups.h"
#include "qa/content/mods/qvm_items.h"
#include "qa/content/mods/qvm_presentation.h"
#include "qa/contracts/qvm_combat.h"
#include "qa/contracts/qvm_mod_actor_frame.h"
#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"

#define QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS 62

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *fstr(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static const char *fchoice(const qa_save_reader *r, const char *name,
                           const char *const *choices, size_t n) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_choice_string(&f, choices, n);
  qa_save_reader_destroy(&f);
  return v;
}
static double fint(const qa_save_reader *r, const char *name, double min) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_integer_min(&f, min);
  qa_save_reader_destroy(&f);
  return v;
}
static double fint_any(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_integer(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static double fnum(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static bool fpresent(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  bool p = !qa_save_reader_is_undefined(&f);
  qa_save_reader_destroy(&f);
  return p;
}
static const char *fnullable_str(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_is_null(&f) ? NULL : qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static const char *fns(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *v = qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static void *xalloc(const qa_save_reader *r, size_t count, size_t size) {
  void *p = count > 0 ? calloc(count, size) : NULL;
  if (count > 0 && p == NULL) qa_save_reader_fail(r, "out of memory");
  return p;
}
static const char *scalar_choice(const qa_save_reader *r, const char *name) {
  static const char *const scalars[] = {"int32", "float32"};
  return fchoice(r, name, scalars, 2);
}
static double nullable_int(const qa_save_reader *r, const char *name, bool *has) {
  qa_save_reader f = field(r, name);
  *has = !qa_save_reader_is_null(&f);
  double v = *has ? qa_save_reader_integer_min(&f, 0) : 0;
  qa_save_reader_destroy(&f);
  return v;
}

/* Mirror of normalizeResourcePath (TS owner:
 * src/content/mounts/paths.ts). See callbacks.c. */
static char *normalize_resource_path(const char *path) {
  size_t n = strlen(path);
  char *out = (char *)malloc(n + 1);
  if (out == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i <= n; i++) out[i] = path[i] == '\\' ? '/' : path[i];
  bool bad = n == 0;
  if (n >= 2 && out[1] == ':' &&
      ((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z')))
    bad = true;
  size_t start = 0;
  for (size_t i = 0; i <= n && !bad; i++) {
    if (out[i] == '/' || out[i] == '\0') {
      size_t len = i - start;
      if (len == 0) bad = true;
      if (len == 1 && out[start] == '.') bad = true;
      if (len == 2 && out[start] == '.' && out[start + 1] == '.') bad = true;
      start = i + 1;
    }
  }
  if (bad) qa_range_error("Invalid relative resource path: %s", path);
  return out;
}

static qa_mod_callback_value read_value(const qa_save_reader *r) {
  static const char *const kinds[] = {"input", "float", "vector", "string"};
  static const char *const inputs[] = {
      "self", "other", "activator", "attacker", "inflictor", "amount",
      "knockback", "point", "direction", "normal", "item", "time", "elapsed",
      "result", "view-angles", "attack", "jump", "impulse", "forward-move",
      "side-move", "up-move", "damage-flags", "regular-protection-scale",
      "pickup-count", "pickup-has-count", "pickup-dropped"};
  qa_mod_callback_value value;
  memset(&value, 0, sizeof value);
  value.kind = fchoice(r, "kind", kinds, 4);
  if (strcmp(value.kind, "input") == 0) {
    value.name = fchoice(r, "name", inputs, sizeof inputs / sizeof inputs[0]);
  } else if (strcmp(value.kind, "float") == 0) {
    value.number_value = fnum(r, "value");
  } else if (strcmp(value.kind, "vector") == 0) {
    qa_save_reader f = field(r, "value");
    value.vector_value = qa_read_vector(&f);
    qa_save_reader_destroy(&f);
  } else {
    value.string_value = fstr(r, "value");
  }
  return value;
}

static qa_qvm_mod_value read_argument(const qa_save_reader *r) {
  static const char *const kinds[] = {"int32", "float32", "vector", "string",
                                      "actor", "client", "time", "address"};
  static const char *const actor_inputs[] = {"self", "other", "activator",
                                             "attacker", "inflictor"};
  static const char *const time_inputs[] = {"time", "elapsed"};
  static const char *const units[] = {"seconds", "milliseconds"};
  qa_qvm_mod_value value;
  memset(&value, 0, sizeof value);
  value.kind = fchoice(r, "kind", kinds, 8);
  if (strcmp(value.kind, "actor") == 0) {
    value.record = fstr(r, "record");
    value.input = fchoice(r, "input", actor_inputs, 5);
  } else if (strcmp(value.kind, "client") == 0) {
    value.input = fchoice(r, "input", actor_inputs, 5);
  } else if (strcmp(value.kind, "time") == 0) {
    value.time_input = fchoice(r, "input", time_inputs, 2);
    value.units = fchoice(r, "units", units, 2);
    value.encoding = scalar_choice(r, "encoding");
  } else if (strcmp(value.kind, "address") == 0) {
    value.address_value = fint(r, "value", 0);
  } else {
    qa_save_reader f = field(r, "value");
    value.value = read_value(&f);
    qa_save_reader_destroy(&f);
  }
  return value;
}

static qa_qvm_mod_value *read_arguments(const qa_save_reader *r, const char *name,
                                        size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_mod_value *rows = (qa_qvm_mod_value *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_argument(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}
static qa_qvm_mod_global_value *read_globals(const qa_save_reader *r,
                                             const char *name, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_mod_global_value *rows =
      (qa_qvm_mod_global_value *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i].address = fint(&entry, "address", 0);
    qa_save_reader f = field(&entry, "value");
    rows[i].value = read_argument(&f);
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_qvm_mod_source_call read_source_call(const qa_save_reader *r) {
  static const char *const returns[] = {"int32", "float32", "void"};
  qa_qvm_mod_source_call call;
  memset(&call, 0, sizeof call);
  call.entry = fint(r, "entry", 0);
  call.arguments = read_arguments(r, "arguments", &call.argument_count);
  call.globals = read_globals(r, "globals", &call.global_count);
  call.returns = fchoice(r, "returns", returns, 3);
  return call;
}
static void read_source_call_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_qvm_mod_source_call *)out = read_source_call(r);
}
static qa_qvm_mod_source_call *read_source_calls(const qa_save_reader *r,
                                                 const char *name,
                                                 size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_mod_source_call *rows =
      (qa_qvm_mod_source_call *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_source_call(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_mod_callback_binding read_binding(const qa_save_reader *r) {
  static const char *const operations[] = {
      "damage", "inventory.give", "inventory.consume", "actor.think",
      "actor.touch", "actor.use", "actor.pain", "actor.die"};
  static const char *const stages[] = {"observe", "transform", "replace"};
  static const char *const damage_results[] = {"amount", "knockback"};
  qa_mod_callback_binding binding;
  memset(&binding, 0, sizeof binding);
  binding.id = fns(r, "id");
  binding.operation = fchoice(r, "operation", operations, 8);
  const char *stage = fchoice(r, "stage", stages, 3);
  const char *op = binding.operation;
  bool inventory = strcmp(op, "inventory.give") == 0 || strcmp(op, "inventory.consume") == 0;
  if (strcmp(stage, "observe") == 0) {
    binding.stage = "observe";
  } else if (strcmp(stage, "transform") == 0) {
    if (strcmp(op, "damage") == 0) {
      binding.stage = "transform";
      binding.result = fchoice(r, "result", damage_results, 2);
    } else if (inventory) {
      binding.stage = "transform";
      qa_save_reader f = field(r, "result");
      binding.result = qa_save_reader_literal_string(&f, "amount");
      qa_save_reader_destroy(&f);
    } else {
      qa_save_reader_fail(r, "actor callbacks support observation or replacement");
    }
  } else {
    if (strcmp(op, "damage") == 0 || inventory)
      qa_save_reader_fail(r, "only actor callbacks support replacement");
    binding.stage = "replace";
    qa_save_reader f = field(r, "result");
    binding.result = qa_save_reader_literal_string(&f, "boolean");
    qa_save_reader_destroy(&f);
  }
  return binding;
}

static qa_qvm_mod_actor_field read_actor_field(const qa_save_reader *r) {
  static const char *const bindings[] = {
      "health", "inventory", "origin", "velocity", "angles", "bounds-min",
      "bounds-max", "record", "constant", "constant-vector", "private",
      "team", "score"};
  static const char *const accesses[] = {"read-only", "read-write"};
  static const char *const canonical[] = {"health", "inventory", "origin",
                                          "velocity", "angles", "bounds-min",
                                          "bounds-max"};
  qa_qvm_mod_actor_field result;
  memset(&result, 0, sizeof result);
  result.offset = fint(r, "offset", 0);
  result.binding = fchoice(r, "binding", bindings, sizeof bindings / sizeof bindings[0]);
  bool has_access = fpresent(r, "access");
  if (has_access) result.access = fchoice(r, "access", accesses, 2);
  if (has_access) {
    bool ok = false;
    for (size_t i = 0; i < sizeof canonical / sizeof canonical[0]; i++)
      if (strcmp(result.binding, canonical[i]) == 0) ok = true;
    if (!ok)
      qa_save_reader_fail(r, "Projection access applies only to canonical actor fields");
  }
  const char *binding = result.binding;
  if (strcmp(binding, "team") == 0) {
    result.encoding = scalar_choice(r, "encoding");
    qa_save_reader f = field(r, "values");
    result.team_values = qa_read_source_team_values(&f, &result.team_value_count);
    qa_save_reader_destroy(&f);
  } else if (strcmp(binding, "score") == 0 || strcmp(binding, "health") == 0) {
    result.encoding = scalar_choice(r, "encoding");
  } else if (strcmp(binding, "inventory") == 0) {
    result.encoding = scalar_choice(r, "encoding");
    result.item = fns(r, "item");
  } else if (strcmp(binding, "record") == 0) {
    result.record = fstr(r, "record");
  } else if (strcmp(binding, "constant") == 0) {
    result.encoding = scalar_choice(r, "encoding");
    result.constant_value = fnum(r, "value");
  } else if (strcmp(binding, "constant-vector") == 0) {
    qa_save_reader f = field(r, "value");
    result.constant_vector = qa_read_vector(&f);
    qa_save_reader_destroy(&f);
  } else if (strcmp(binding, "private") == 0) {
    result.byte_length = fint(r, "byteLength", 1);
  }
  return result;
}

static qa_qvm_mod_protection_scalar read_protection_scalar(const qa_save_reader *r) {
  qa_qvm_mod_protection_scalar scalar;
  memset(&scalar, 0, sizeof scalar);
  scalar.record = fstr(r, "record");
  scalar.offset = fint(r, "offset", 0);
  scalar.encoding = scalar_choice(r, "encoding");
  return scalar;
}
static void read_protection_scalar_adapter(void *ctx, const qa_save_reader *r,
                                           void *out) {
  (void)ctx;
  *(qa_qvm_mod_protection_scalar *)out = read_protection_scalar(r);
}
static qa_qvm_mod_record_offset read_record_offset(const qa_save_reader *r) {
  qa_qvm_mod_record_offset row;
  row.record = fstr(r, "record");
  row.offset = fint(r, "offset", 0);
  return row;
}
static void read_record_offset_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_qvm_mod_record_offset *)out = read_record_offset(r);
}

static qa_qvm_mod_protection_selection read_protection_selection(
    const qa_save_reader *r, bool powered) {
  static const char *const selected_kinds[] = {"none", "screen", "shield"};
  qa_qvm_mod_protection_selection selection;
  memset(&selection, 0, sizeof selection);
  qa_save_reader f = field(r, "field");
  selection.field = read_protection_scalar(&f);
  qa_save_reader_destroy(&f);
  qa_save_reader mask = field(r, "mask");
  if (!qa_save_reader_is_null(&mask)) {
    selection.has_mask = true;
    selection.mask = qa_save_reader_integer_min(&mask, 0);
  }
  qa_save_reader_destroy(&mask);
  qa_save_reader list = field(r, "values");
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_mod_protection_selection_value *rows =
      (qa_qvm_mod_protection_selection_value *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i].value = fnum(&entry, "value");
    qa_save_reader selected = field(&entry, "selected");
    if (powered) {
      rows[i].selected_kind =
          qa_save_reader_choice_string(&selected, selected_kinds, 3);
    } else {
      rows[i].selected_item = qa_save_reader_is_null(&selected)
                                  ? NULL
                                  : qa_namespaced(&selected);
    }
    qa_save_reader_destroy(&selected);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  selection.values = rows;
  selection.value_count = count;
  return selection;
}

static qa_qvm_mod_protection read_protection(const qa_save_reader *r) {
  static const char *const admission_kinds[] = {"claim", "replace-primary",
                                                "replace-current-primary"};
  static const char *const channels[] = {"regular", "powered"};
  qa_qvm_mod_protection prot;
  memset(&prot, 0, sizeof prot);
  prot.id = fns(r, "id");
  {
    qa_save_reader admission = field(r, "admission");
    prot.admission.kind = fchoice(&admission, "kind", admission_kinds, 3);
    if (strcmp(prot.admission.kind, "replace-primary") == 0)
      prot.admission.owner = fns(&admission, "owner");
    qa_save_reader_destroy(&admission);
  }
  {
    qa_save_reader f = field(r, "absorb");
    prot.absorb = read_source_call(&f);
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader flags = field(r, "flags");
    prot.flags.no_armor = fint(&flags, "noArmor", 0);
    prot.flags.no_power_armor = fint(&flags, "noPowerArmor", 0);
    prot.flags.no_regular_armor = fint(&flags, "noRegularArmor", 0);
    prot.flags.energy = fint(&flags, "energy", 0);
    prot.flags.radius = fpresent(&flags, "radius") ? fint(&flags, "radius", 0) : 0;
    qa_save_reader_destroy(&flags);
  }
  prot.channel = fchoice(r, "channel", channels, 2);
  qa_save_reader storage = field(r, "storage");
  if (strcmp(prot.channel, "regular") == 0) {
    qa_save_reader f = field(&storage, "points");
    prot.storage_points = read_protection_scalar(&f);
    qa_save_reader_destroy(&f);
    /* TS storage.field("item").nullable(namespaced) */
    {
      qa_save_reader item_reader = field(&storage, "item");
      prot.storage_item = qa_save_reader_is_null(&item_reader)
                              ? NULL
                              : qa_namespaced(&item_reader);
      qa_save_reader_destroy(&item_reader);
    }
    if (fpresent(&storage, "selection")) {
      qa_save_reader f2 = field(&storage, "selection");
      prot.has_selection = true;
      prot.selection = read_protection_selection(&f2, false);
      qa_save_reader_destroy(&f2);
    }
  } else {
    qa_save_reader f = field(&storage, "cells");
    prot.storage_cells = read_protection_scalar(&f);
    qa_save_reader_destroy(&f);
    qa_save_reader f2 = field(&storage, "selection");
    prot.has_selection = true;
    prot.selection = read_protection_selection(&f2, true);
    qa_save_reader_destroy(&f2);
  }
  qa_save_reader_destroy(&storage);
  return prot;
}

static qa_qvm_combat_call read_combat_call(const qa_save_reader *r,
                                           const char *const *role_names,
                                           size_t role_count) {
  static const char *const extra_kinds[] = {"int32", "float32", "address"};
  qa_qvm_combat_call call;
  memset(&call, 0, sizeof call);
  qa_save_reader roles_reader = field(r, "roles");
  qa_qvm_combat_role_index *roles =
      (qa_qvm_combat_role_index *)xalloc(r, role_count, sizeof *roles);
  for (size_t i = 0; i < role_count; i++) {
    roles[i].role = role_names[i];
    roles[i].index = fint(&roles_reader, role_names[i], 0);
  }
  qa_save_reader_destroy(&roles_reader);
  call.roles = roles;
  call.role_count = role_count;
  qa_save_reader list = field(r, "extras");
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_combat_extra *rows = (qa_qvm_combat_extra *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i].index = fint(&entry, "index", 0);
    rows[i].kind = fchoice(&entry, "kind", extra_kinds, 3);
    rows[i].value = fnum(&entry, "value");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  call.extras = rows;
  call.extra_count = count;
  return call;
}

static qa_qvm_mod_combat *read_combat(const qa_save_reader *combat_reader) {
  static const char *const abis[] = {"q3-g-damage", "declared"};
  static const char *const mass_kinds[] = {"constant", "entity"};
  static const char *const damage_roles[] = {"target", "inflictor", "attacker",
                                             "direction", "point", "amount",
                                             "flags", "method"};
  static const char *const touch_roles[] = {"target", "other", "trace"};
  static const char *const use_roles[] = {"target", "other", "activator"};
  static const char *const pain_roles[] = {"target", "attacker", "amount"};
  static const char *const die_roles[] = {"target", "inflictor", "attacker",
                                           "amount", "method"};
  const qa_save_reader *combat = combat_reader;
  qa_qvm_mod_combat *row = (qa_qvm_mod_combat *)xalloc(combat, 1, sizeof *row);
  row->fields.entry = fint(combat, "entry", 0);
  row->fields.health = fint(combat, "health", 0);
  row->fields.takedamage = fint(combat, "takedamage", 0);
  row->fields.flags = fint(combat, "flags", 0);
  row->fields.godmode = fint(combat, "godmode", 1);
  row->fields.no_knockback = fint(combat, "noKnockback", 1);
  row->fields.globals = read_globals(combat, "globals", &row->fields.global_count);
  {
    qa_save_reader client = field(combat, "client");
    if (!qa_save_reader_is_null(&client)) {
      qa_qvm_mod_combat_client *c =
          (qa_qvm_mod_combat_client *)xalloc(combat, 1, sizeof *c);
      c->pointer = fint(&client, "pointer", 0);
      c->record = fstr(&client, "record");
      c->health = fint(&client, "health", 0);
      c->armor = fint(&client, "armor", 0);
      c->protection = fnum(&client, "protection");
      c->team = fint(&client, "team", 0);
      row->fields.client = c;
    }
    qa_save_reader_destroy(&client);
  }
  row->abi = fchoice(combat, "abi", abis, 2);
  if (strcmp(row->abi, "q3-g-damage") == 0) return row;
  {
    qa_save_reader calls = field(combat, "calls");
    qa_save_reader f = field(&calls, "damage");
    row->calls.damage = read_combat_call(&f, damage_roles, 8);
    qa_save_reader_destroy(&f);
    f = field(&calls, "touch");
    row->calls.touch = read_combat_call(&f, touch_roles, 3);
    qa_save_reader_destroy(&f);
    f = field(&calls, "use");
    row->calls.use = read_combat_call(&f, use_roles, 3);
    qa_save_reader_destroy(&f);
    f = field(&calls, "pain");
    row->calls.pain = read_combat_call(&f, pain_roles, 3);
    qa_save_reader_destroy(&f);
    f = field(&calls, "die");
    row->calls.die = read_combat_call(&f, die_roles, 5);
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&calls);
  }
  {
    qa_save_reader flags = field(combat, "damageFlags");
    row->damage_flags.radius = fint(&flags, "radius", 1);
    row->damage_flags.no_armor = fint(&flags, "noArmor", 1);
    row->damage_flags.no_knockback = fint(&flags, "noKnockback", 1);
    row->damage_flags.no_protection = fint(&flags, "noProtection", 1);
    row->damage_flags.no_team_protection = fint(&flags, "noTeamProtection", 1);
    qa_save_reader_destroy(&flags);
  }
  {
    qa_save_reader mass = field(combat, "mass");
    const char *kind = fchoice(&mass, "kind", mass_kinds, 2);
    if (strcmp(kind, "constant") == 0) {
      row->mass.is_constant = true;
      row->mass.value = fnum(&mass, "value");
    } else {
      row->mass.offset = fint(&mass, "offset", 0);
      row->mass.storage = scalar_choice(&mass, "storage");
    }
    qa_save_reader_destroy(&mass);
  }
  {
    qa_save_reader list = field(combat, "teams");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_combat_team *rows = (qa_qvm_combat_team *)xalloc(combat, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].value = fint_any(&entry, "value");
      rows[i].team = fns(&entry, "team");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    row->teams = rows;
    row->team_count = count;
  }
  return row;
}

static qa_qvm_mod_input_pointer read_input_pointer(const qa_save_reader *r) {
  static const char *const kinds[] = {"argument", "global"};
  qa_qvm_mod_input_pointer pointer;
  memset(&pointer, 0, sizeof pointer);
  {
    qa_save_reader list = field(r, "indirections");
    size_t count = qa_save_reader_list_count(&list);
    double *vals = (double *)xalloc(r, count, sizeof *vals);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      vals[i] = qa_save_reader_integer_min(&entry, 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    pointer.indirections.values = vals;
    pointer.indirections.count = count;
  }
  pointer.offset = fint(r, "offset", 0);
  if (strcmp(fchoice(r, "kind", kinds, 2), "global") == 0) {
    pointer.is_global = true;
    pointer.address = fint(r, "address", 0);
    return pointer;
  }
  pointer.index = fint(r, "index", 0);
  if (pointer.index >= QA_QVM_MAX_PRIVATE_ARGUMENT_WORDS)
    qa_save_reader_fail(r, "Input pointer argument exceeds source call ABI");
  return pointer;
}

static qa_qvm_mod_objective_address read_objective_address(const qa_save_reader *r) {
  qa_qvm_mod_objective_address address;
  memset(&address, 0, sizeof address);
  if (r->value != NULL && r->value->kind == QA_SAVE_NUMBER) {
    address.address = qa_save_reader_integer_min(r, 0);
    return address;
  }
  qa_qvm_mod_input_pointer pointer = read_input_pointer(r);
  if (!pointer.is_global)
    qa_save_reader_fail(r, "Persistent objective storage requires a source global pointer");
  address.is_global_pointer = true;
  address.pointer = pointer;
  return address;
}
static void read_objective_address_adapter(void *ctx, const qa_save_reader *r,
                                           void *out) {
  (void)ctx;
  *(qa_qvm_mod_objective_address *)out = read_objective_address(r);
}
static void read_objective_scalar_adapter(void *ctx, const qa_save_reader *r,
                                          void *out) {
  (void)ctx;
  qa_qvm_mod_objective_scalar *scalar = (qa_qvm_mod_objective_scalar *)out;
  memset(scalar, 0, sizeof *scalar);
  qa_save_reader f = field(r, "address");
  qa_qvm_mod_objective_address address = read_objective_address(&f);
  qa_save_reader_destroy(&f);
  scalar->address_is_global_pointer = address.is_global_pointer;
  scalar->address = address.is_global_pointer ? address.pointer.address : address.address;
  if (address.is_global_pointer) {
    scalar->indirections = address.pointer.indirections;
    scalar->offset = address.pointer.offset;
  }
  scalar->encoding = scalar_choice(r, "encoding");
}

static const char **read_input_names(const qa_save_reader *r, const char *name,
                                     const char *const *choices, size_t nchoices,
                                     size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  const char **vals = (const char **)xalloc(r, count, sizeof *vals);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    vals[i] = qa_save_reader_choice_string(&entry, choices, nchoices);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return vals;
}

static qa_qvm_mod_input_output read_input_output(const qa_save_reader *r) {
  static const char *const kinds[] = {"field", "handler", "command"};
  static const char *const field_inputs[] = {"view-angles", "attack", "jump",
                                             "impulse", "forward-move", "side-move",
                                             "up-move"};
  static const char *const command_inputs[] = {"view-angles", "attack", "jump",
                                               "forward-move", "side-move", "up-move"};
  static const char *const handler_inputs[] = {"attack", "jump", "impulse",
                                               "forward-move", "side-move", "up-move"};
  qa_qvm_mod_input_output out;
  memset(&out, 0, sizeof out);
  out.kind = fchoice(r, "kind", kinds, 3);
  if (strcmp(out.kind, "field") == 0) {
    qa_save_reader value = field(r, "value");
    const char *input = fchoice(&value, "input", field_inputs, 7);
    out.record = fstr(r, "record");
    out.offset = fint(r, "offset", 0);
    out.value_input = input;
    if (strcmp(input, "view-angles") != 0) {
      double scale = fnum(&value, "scale");
      if (scale <= 0) qa_save_reader_fail(&value, "Input field scale must be positive");
      out.value_encoding = scalar_choice(&value, "encoding");
      out.value_scale = scale;
    }
    qa_save_reader_destroy(&value);
    return out;
  }
  out.entry = fint(r, "entry", 0);
  {
    qa_save_reader actor = field(r, "actor");
    out.actor_record = fstr(&actor, "record");
    qa_save_reader f = field(&actor, "pointer");
    out.actor_pointer = read_input_pointer(&f);
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&actor);
  }
  if (strcmp(out.kind, "command") == 0) {
    qa_save_reader f = field(r, "command");
    out.command = read_input_pointer(&f);
    qa_save_reader_destroy(&f);
    out.inputs = (const qa_mod_client_input *)read_input_names(
        r, "inputs", command_inputs, 6, &out.input_count);
    return out;
  }
  out.inputs = (const qa_mod_client_input *)read_input_names(
      r, "inputs", handler_inputs, 6, &out.input_count);
  if (fpresent(r, "returns")) {
    qa_save_reader f = field(r, "returns");
    out.has_returns = true;
    out.returns_encoding = scalar_choice(&f, "encoding");
    out.returns_value = fnum(&f, "value");
    qa_save_reader_destroy(&f);
  }
  return out;
}
static void read_input_output_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_qvm_mod_input_output *)out = read_input_output(r);
}

static qa_qvm_mod_source_actors *read_source_actors(const qa_save_reader *actors) {
  qa_qvm_mod_source_actors *row =
      (qa_qvm_mod_source_actors *)xalloc(actors, 1, sizeof *row);
  row->allocate = fint(actors, "allocate", 0);
  {
    qa_save_reader release = field(actors, "release");
    row->release.entry = fint(&release, "entry", 0);
    row->release.argument = fint(&release, "argument", 0);
    qa_save_reader_destroy(&release);
  }
  if (fpresent(actors, "initialStores")) {
    qa_save_reader list = field(actors, "initialStores");
    size_t count = qa_save_reader_list_count(&list);
    double *vals = (double *)xalloc(actors, count, sizeof *vals);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      vals[i] = qa_save_reader_integer_min(&entry, 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    row->initial_stores.values = vals;
    row->initial_stores.count = count;
  }
  row->inuse = fint(actors, "inuse", 0);
  row->event_entity_type = fint(actors, "eventEntityType", 0);
  {
    qa_save_reader update = field(actors, "update");
    if (!qa_save_reader_is_null(&update)) {
      qa_qvm_mod_source_call *call =
          (qa_qvm_mod_source_call *)xalloc(actors, 1, sizeof *call);
      *call = read_source_call(&update);
      row->update = call;
    }
    qa_save_reader_destroy(&update);
  }
  if (fpresent(actors, "frame")) {
    qa_save_reader frame = field(actors, "frame");
    qa_qvm_mod_actor_frame *f2 =
        (qa_qvm_mod_actor_frame *)xalloc(actors, 1, sizeof *f2);
    qa_save_reader call = field(&frame, "call");
    f2->call = read_source_call(&call);
    qa_save_reader_destroy(&call);
    qa_save_reader clock = field(&frame, "clock");
    f2->clock.address = fint(&clock, "address", 0);
    f2->clock.store = fint(&clock, "store", 0);
    f2->clock.argument = fint(&clock, "argument", 0);
    qa_save_reader_destroy(&clock);
    qa_save_reader list = field(&frame, "owned");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_mod_actor_frame_owned *owned =
        (qa_qvm_mod_actor_frame_owned *)xalloc(actors, count, sizeof *owned);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      owned[i].instruction = fint(&entry, "instruction", 0);
      owned[i].local_instruction = fint(&entry, "localInstruction", 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    f2->owned = owned;
    f2->owned_count = count;
    qa_save_reader end = field(&frame, "end");
    f2->end.instruction = fint(&end, "instruction", 0);
    {
      qa_save_reader f3 = field(&end, "completedTaken");
      f2->end.completed_taken = qa_save_reader_boolean(&f3);
      qa_save_reader_destroy(&f3);
    }
    qa_save_reader_destroy(&end);
    qa_save_reader_destroy(&frame);
    row->frame = f2;
  }
  if (fpresent(actors, "callbacks")) {
    qa_save_reader callbacks = field(actors, "callbacks");
    qa_qvm_mod_source_callbacks *c =
        (qa_qvm_mod_source_callbacks *)xalloc(actors, 1, sizeof *c);
    c->touch = nullable_int(&callbacks, "touch", &c->has_touch);
    c->use = nullable_int(&callbacks, "use", &c->has_use);
    c->pain = nullable_int(&callbacks, "pain", &c->has_pain);
    c->die = nullable_int(&callbacks, "die", &c->has_die);
    qa_save_reader_destroy(&callbacks);
    row->callbacks = c;
  }
  return row;
}

qa_qvm_mod_callback_declaration *qa_read_qvm_mod_declaration(
    const qa_save_reader *reader) {
  static const char *const profiles[] = {"q3-modern", "q3-1.16n-base"};
  qa_qvm_mod_callback_declaration *decl =
      (qa_qvm_mod_callback_declaration *)calloc(1, sizeof *decl);
  if (decl == NULL) return NULL;
  {
    qa_save_reader f = field(reader, "version");
    decl->version = (int64_t)qa_save_reader_literal_number(&f, 1);
    qa_save_reader_destroy(&f);
    f = field(reader, "runtime");
    decl->runtime = qa_save_reader_literal_string(&f, "qvm");
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader program = field(reader, "program");
    decl->program.path = normalize_resource_path(fstr(&program, "path"));
    qa_save_reader f = field(&program, "digest");
    decl->program.digest = qa_read_digest(&f);
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&program);
  }
  decl->abi_profile = fchoice(reader, "abiProfile", profiles, 2);
  if (fpresent(reader, "presentation")) {
    qa_save_reader f = field(reader, "presentation");
    decl->presentation = qa_read_qvm_mod_presentation_declaration(&f);
    qa_save_reader_destroy(&f);
  }
  decl->has_spawn_entities = fpresent(reader, "spawnEntities");
  decl->spawn_entities = decl->has_spawn_entities
                             ? fnullable_str(reader, "spawnEntities")
                             : NULL;
  if (fpresent(reader, "clients")) {
    qa_save_reader clients = field(reader, "clients");
    qa_qvm_mod_clients *rows = (qa_qvm_mod_clients *)xalloc(reader, 1, sizeof *rows);
    rows->maximum = fint(&clients, "maximum", 1);
    if (fpresent(&clients, "outputs")) {
      qa_save_reader f = field(&clients, "outputs");
      rows->outputs = qa_read_client_output_declarations(
          &f, read_protection_scalar_adapter, NULL,
          sizeof(qa_qvm_mod_protection_scalar), read_record_offset_adapter, NULL,
          sizeof(qa_qvm_mod_record_offset), &rows->output_count);
      qa_save_reader_destroy(&f);
    }
    {
      qa_save_reader list = field(&clients, "records");
      size_t count = qa_save_reader_list_count(&list);
      const char **vals = (const char **)xalloc(reader, count, sizeof *vals);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        vals[i] = qa_save_reader_string(&entry);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      rows->records.values = vals;
      rows->records.count = count;
    }
    rows->player_state_record = fstr(&clients, "playerStateRecord");
    rows->admit = read_source_calls(&clients, "admit", &rows->admit_count);
    rows->userinfo = read_source_calls(&clients, "userinfo", &rows->userinfo_count);
    rows->disconnect =
        read_source_calls(&clients, "disconnect", &rows->disconnect_count);
    if (fpresent(&clients, "frame"))
      rows->frame = read_source_calls(&clients, "frame", &rows->frame_count);
    if (fpresent(&clients, "input")) {
      qa_save_reader f = field(&clients, "input");
      rows->input = qa_read_mod_client_input(
          &f, read_source_call_adapter, NULL, sizeof(qa_qvm_mod_source_call),
          read_input_output_adapter, NULL, sizeof(qa_qvm_mod_input_output),
          &rows->input_count);
      qa_save_reader_destroy(&f);
    }
    qa_save_reader_destroy(&clients);
    decl->clients = rows;
  }
  decl->entity_record = fnullable_str(reader, "entityRecord");
  if (fpresent(reader, "sourceActors")) {
    qa_save_reader f = field(reader, "sourceActors");
    decl->source_actors = read_source_actors(&f);
    qa_save_reader_destroy(&f);
  }
  if (fpresent(reader, "combat")) {
    qa_save_reader f = field(reader, "combat");
    decl->combat = read_combat(&f);
    qa_save_reader_destroy(&f);
  }
  if (fpresent(reader, "protection")) {
    qa_save_reader list = field(reader, "protection");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_mod_protection *rows =
        (qa_qvm_mod_protection *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = read_protection(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->protection = rows;
    decl->protection_count = count;
  }
  if (fpresent(reader, "items")) {
    qa_save_reader f = field(reader, "items");
    decl->items = qa_read_qvm_mod_items(&f, read_source_call_adapter, NULL);
    qa_save_reader_destroy(&f);
  }
  if (fpresent(reader, "pickups")) {
    qa_save_reader list = field(reader, "pickups");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_mod_pickup *rows = (qa_qvm_mod_pickup *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].rule = qa_read_mod_pickup_rule(&entry, read_source_call_adapter, NULL,
                                             sizeof(qa_qvm_mod_source_call));
      qa_save_reader clist = field(&entry, "context");
      size_t ccount = qa_save_reader_list_count(&clist);
      qa_qvm_mod_pickup_context *crows =
          (qa_qvm_mod_pickup_context *)xalloc(reader, ccount, sizeof *crows);
      for (size_t j = 0; j < ccount; j++) {
        qa_save_reader centry = item(&clist, j);
        crows[j].record = fstr(&centry, "record");
        crows[j].offset = fint(&centry, "offset", 0);
        qa_save_reader vf = field(&centry, "value");
        crows[j].value = read_argument(&vf);
        qa_save_reader_destroy(&vf);
        qa_save_reader_destroy(&centry);
      }
      qa_save_reader_destroy(&clist);
      rows[i].context = crows;
      rows[i].context_count = ccount;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->pickups = rows;
    decl->pickup_count = count;
  }
  if (fpresent(reader, "objectives")) {
    qa_save_reader f = field(reader, "objectives");
    decl->objectives = qa_read_source_objectives(
        &f, read_objective_scalar_adapter, NULL, sizeof(qa_qvm_mod_objective_scalar),
        read_objective_address_adapter, NULL, sizeof(qa_qvm_mod_objective_address),
        read_source_call_adapter, NULL, sizeof(qa_qvm_mod_source_call),
        &decl->objective_count);
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader list = field(reader, "actorRecords");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_mod_actor_record *rows =
        (qa_qvm_mod_actor_record *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].id = fstr(&entry, "id");
      rows[i].address = fint(&entry, "address", 1);
      rows[i].stride = fint(&entry, "stride", 4);
      rows[i].capacity = fint(&entry, "capacity", 1);
      qa_save_reader flist = field(&entry, "fields");
      size_t fcount = qa_save_reader_list_count(&flist);
      qa_qvm_mod_actor_field *frows =
          (qa_qvm_mod_actor_field *)xalloc(reader, fcount, sizeof *frows);
      for (size_t j = 0; j < fcount; j++) {
        qa_save_reader fentry = item(&flist, j);
        frows[j] = read_actor_field(&fentry);
        qa_save_reader_destroy(&fentry);
      }
      qa_save_reader_destroy(&flist);
      rows[i].fields = frows;
      rows[i].field_count = fcount;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->actor_records = rows;
    decl->actor_record_count = count;
  }
  decl->initialize = read_source_calls(reader, "initialize", &decl->initialize_count);
  {
    qa_save_reader list = field(reader, "callbacks");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_mod_callback *rows =
        (qa_qvm_mod_callback *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].binding = read_binding(&entry);
      rows[i].call = read_source_call(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->callbacks = rows;
    decl->callback_count = count;
  }
  return decl;
}

qa_qvm_mod_callback_declaration *qa_read_qvm_mod_callbacks(const uint8_t *bytes,
                                                           size_t length) {
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return NULL;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) return NULL;
  qa_qvm_mod_callback_declaration *decl = qa_read_qvm_mod_declaration(&reader);
  qa_save_reader_destroy(&reader);
  return decl;
}

