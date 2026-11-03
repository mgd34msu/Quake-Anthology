/* Port of src/content/mods/native-callbacks.ts. GPL-2.0-or-later. */
#include "qa/content/mods/native_callbacks.h"
#include "qa/content/mods/declaration.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/client_input.h"
#include "qa/content/mods/client_outputs.h"
#include "qa/content/mods/item_actions.h"
#include "qa/content/mods/match.h"
#include "qa/content/mods/pickups.h"
#include "qa/contracts/content.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/native_mod_region.h"
#include "qa/contracts/scene.h"
#include "qa/foundation/support.h"
#include "qa/persistence/shared.h"

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
static double fnum(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static double ffinite(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_finite(&f);
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
static const char *fns_nullable(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  const char *v = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static void *xalloc(const qa_save_reader *r, size_t count, size_t size) {
  void *p = count > 0 ? qa_mod_declaration_calloc(count, size) : NULL;
  if (count > 0 && p == NULL) qa_save_reader_fail(r, "out of memory");
  return p;
}
static const char *const SCALARS[] = {"int8", "uint8", "int16", "uint16",
                                      "int32", "uint32", "int64", "uint64",
                                      "float32", "float64"};
#define NSCALARS (sizeof SCALARS / sizeof SCALARS[0])
static const char *const RETURNS[] = {"int8", "uint8", "int16", "uint16", "int32",
                                      "uint32", "int64", "uint64", "float32",
                                      "float64", "void"};
#define NRETURNS (sizeof RETURNS / sizeof RETURNS[0])
static const char *encoding_choice(const qa_save_reader *r, const char *name) {
  return fchoice(r, name, SCALARS, NSCALARS);
}
static double nullable_int(const qa_save_reader *r, const char *name, bool *has) {
  qa_save_reader f = field(r, name);
  *has = !qa_save_reader_is_null(&f);
  double v = *has ? qa_save_reader_integer_min(&f, 0) : 0;
  qa_save_reader_destroy(&f);
  return v;
}

/* Mirror of normalizeResourcePath (TS owner:
 * src/content/mounts/paths.ts) until that module lands. Returns a
 * malloc'd normalized copy; invalid paths abort with the TS
 * RangeError message. */
static char *normalize_resource_path(const char *path) {
  size_t n = strlen(path);
  char *out = (char *)qa_mod_declaration_malloc(n + 1);
  if (out == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i <= n; i++) out[i] = path[i] == '\\' ? '/' : path[i];
  bool bad = n == 0;
  if (n >= 2 && out[1] == ':' &&
      ((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z')))
    bad = true;
  /* split("/") parts must be nonempty and not "." / ".." */
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

/* Mirror of readItemIconDeclaration (TS owner:
 * src/content/item-icon.ts). Returned struct is malloc'd. */
static qa_source_item_icon_declaration *read_item_icon(const qa_save_reader *r) {
  static const char *const kinds[] = {"image", "wad-picture", "shader"};
  qa_source_item_icon_declaration *icon =
      (qa_source_item_icon_declaration *)xalloc(r, 1, sizeof *icon);
  icon->kind = fchoice(r, "kind", kinds, 3);
  if (strcmp(icon->kind, "shader") == 0) {
    icon->name = normalize_resource_path(fstr(r, "name"));
    return icon;
  }
  icon->path = normalize_resource_path(fstr(r, "path"));
  if (strcmp(icon->kind, "image") == 0) return icon;
  icon->lump = fstr(r, "lump");
  size_t len = strlen(icon->lump);
  if (len == 0 || len > 16)
    qa_save_reader_fail(r, "Item icon requires a valid WAD lump name");
  return icon;
}

static qa_vec3 read_vec_finite(const qa_save_reader *r) {
  qa_vec3 v = {ffinite(r, "x"), ffinite(r, "y"), ffinite(r, "z")};
  return v;
}
static double dot3(qa_vec3 a, qa_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

/* Mirror of readModelGrip (TS owner:
 * src/content/model-attachment.ts). Plain-Error throws use
 * qa_range_error's abort with the identical message text. */
static qa_model_transform read_model_grip(const qa_save_reader *grip) {
  qa_save_reader axis_reader = field(grip, "axis");
  size_t count = qa_save_reader_list_count(&axis_reader);
  qa_vec3 axis[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  for (size_t i = 0; i < count && i < 3; i++) {
    qa_save_reader entry = item(&axis_reader, i);
    axis[i] = read_vec_finite(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&axis_reader);
  if (count != 3) qa_range_error("Model grip requires three source axes");
  qa_vec3 cross = {axis[0].y * axis[1].z - axis[0].z * axis[1].y,
                   axis[0].z * axis[1].x - axis[0].x * axis[1].z,
                   axis[0].x * axis[1].y - axis[0].y * axis[1].x};
  bool bad = false;
  for (size_t i = 0; i < 3; i++)
    if (fabs(dot3(axis[i], axis[i]) - 1) > 0.001) bad = true;
  if (fabs(dot3(axis[0], axis[1])) > 0.001 || fabs(dot3(axis[0], axis[2])) > 0.001 ||
      fabs(dot3(axis[1], axis[2])) > 0.001 || dot3(cross, axis[2]) < 0.999)
    bad = true;
  if (bad) qa_range_error("Model grip axes must form a rotation");
  qa_model_transform transform;
  memset(&transform, 0, sizeof transform);
  if (fpresent(grip, "scale")) {
    qa_save_reader f = field(grip, "scale");
    transform.scale = read_vec_finite(&f);
    qa_save_reader_destroy(&f);
  } else {
    transform.scale.x = 1;
    transform.scale.y = 1;
    transform.scale.z = 1;
  }
  if (transform.scale.x == 0 || transform.scale.y == 0 || transform.scale.z == 0)
    qa_save_reader_fail(grip, "Model grip scale must be invertible");
  {
    qa_save_reader f = field(grip, "origin");
    transform.origin = read_vec_finite(&f);
    qa_save_reader_destroy(&f);
  }
  transform.axis.vectors[0] = axis[0];
  transform.axis.vectors[1] = axis[1];
  transform.axis.vectors[2] = axis[2];
  return transform;
}

static const char *read_held_digest(const qa_save_reader *r) {
  const char *parsed = qa_save_reader_string(r);
  if (!qa_is_content_digest(parsed))
    qa_save_reader_fail(r, "held model requires a SHA256 digest");
  return parsed;
}

/* Mirror of readHeldWeaponDeclaration (TS owner:
 * src/content/held-weapon.ts). Returned struct is malloc'd. */
static qa_held_weapon_declaration *read_held_weapon(const qa_save_reader *r) {
  static const char *const kinds[] = {"none", "model"};
  qa_held_weapon_declaration *decl =
      (qa_held_weapon_declaration *)xalloc(r, 1, sizeof *decl);
  const char *kind = fchoice(r, "kind", kinds, 2);
  if (strcmp(kind, "none") == 0) return decl;
  decl->is_model = true;
  qa_save_reader model = field(r, "model");
  qa_save_reader part = field(&model, "part");
  bool has_part = !qa_save_reader_is_undefined(&part);
  qa_held_weapon_part *subset = NULL;
  if (has_part) {
    subset = (qa_held_weapon_part *)xalloc(r, 1, sizeof *subset);
    qa_save_reader digests = field(&part, "digests");
    size_t dcount = qa_save_reader_list_count(&digests);
    const char **dvals = (const char **)xalloc(r, dcount, sizeof *dvals);
    for (size_t i = 0; i < dcount; i++) {
      qa_save_reader entry = item(&digests, i);
      dvals[i] = read_held_digest(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&digests);
    qa_save_reader vertices = field(&part, "vertices");
    size_t vcount = qa_save_reader_list_count(&vertices);
    double *vvals = (double *)xalloc(r, vcount, sizeof *vvals);
    for (size_t i = 0; i < vcount; i++) {
      qa_save_reader entry = item(&vertices, i);
      vvals[i] = qa_save_reader_integer_min(&entry, 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&vertices);
    subset->digests.values = dvals;
    subset->digests.count = dcount;
    subset->vertices.values = vvals;
    subset->vertices.count = vcount;
    bool distinct = true;
    for (size_t i = 0; i < vcount && distinct; i++)
      for (size_t j = i + 1; j < vcount; j++)
        if (vvals[j] == vvals[i]) distinct = false;
    if (dcount == 0 || vcount == 0 || !distinct)
      qa_save_reader_fail(&part,
                          "held model subset requires source digests and distinct vertices");
  }
  decl->model.path = normalize_resource_path(fstr(&model, "path"));
  decl->model.reference_frame = fint(&model, "referenceFrame", 0);
  {
    qa_save_reader grip = field(&model, "grip");
    decl->model.grip = read_model_grip(&grip);
    qa_save_reader_destroy(&grip);
  }
  if (fpresent(&model, "digest")) {
    qa_save_reader d = field(&model, "digest");
    decl->model.has_digest = true;
    decl->model.digest = read_held_digest(&d);
    qa_save_reader_destroy(&d);
  }
  if (fpresent(&model, "fallback"))
    decl->model.fallback = normalize_resource_path(fstr(&model, "fallback"));
  decl->model.part = subset;
  qa_save_reader_destroy(&part);
  qa_save_reader_destroy(&model);
  return decl;
}


static qa_mod_callback_value read_value(const qa_save_reader *r) {
  static const char *const kinds[] = {"input", "float", "vector", "string"};
  static const char *const inputs[] = {
      "self", "other", "activator", "attacker", "inflictor", "amount",
      "damage-flags", "regular-protection-scale", "knockback", "point",
      "direction", "normal", "item", "time", "elapsed", "result",
      "view-angles", "attack", "jump", "impulse", "forward-move", "side-move",
      "up-move", "pickup-count", "pickup-has-count", "pickup-dropped"};
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

static qa_native_mod_address read_address(const qa_save_reader *r) {
  qa_native_mod_address address;
  memset(&address, 0, sizeof address);
  address.rva = fint(r, "rva", 0);
  qa_save_reader list = field(r, "indirections");
  size_t count = qa_save_reader_list_count(&list);
  double *vals = (double *)xalloc(r, count, sizeof *vals);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    vals[i] = qa_save_reader_integer_min(&entry, 0);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  address.indirections.values = vals;
  address.indirections.count = count;
  return address;
}
static qa_native_mod_address *read_nullable_address(const qa_save_reader *r,
                                                    const char *name) {
  qa_save_reader f = field(r, name);
  if (qa_save_reader_is_null(&f)) {
    qa_save_reader_destroy(&f);
    return NULL;
  }
  qa_native_mod_address *address =
      (qa_native_mod_address *)xalloc(r, 1, sizeof *address);
  *address = read_address(&f);
  qa_save_reader_destroy(&f);
  return address;
}
static void read_address_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_native_mod_address *)out = read_address(r);
}
static void read_objective_scalar_adapter(void *ctx, const qa_save_reader *r,
                                          void *out) {
  (void)ctx;
  qa_native_mod_objective_scalar *scalar = (qa_native_mod_objective_scalar *)out;
  qa_save_reader f = field(r, "address");
  scalar->address = read_address(&f);
  qa_save_reader_destroy(&f);
  scalar->encoding = encoding_choice(r, "encoding");
}

static qa_native_mod_entry read_entry(const qa_save_reader *r) {
  static const char *const kinds[] = {"export", "rva"};
  qa_native_mod_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.kind = fchoice(r, "kind", kinds, 2);
  if (strcmp(entry.kind, "export") == 0) {
    entry.name = fstr(r, "name");
  } else {
    entry.rva = fint(r, "rva", 0);
  }
  return entry;
}
static qa_native_mod_entry read_named_entry(const qa_save_reader *r,
                                            const char *name) {
  qa_save_reader f = field(r, name);
  qa_native_mod_entry v = read_entry(&f);
  qa_save_reader_destroy(&f);
  return v;
}

static qa_native_mod_value read_argument(const qa_save_reader *r) {
  static const char *const kinds[] = {
      "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64", "uint64",
      "float32", "float64", "vector", "string", "actor", "client", "userinfo",
      "time", "address", "user-command"};
  static const char *const actor_inputs[] = {"self", "other", "activator",
                                             "attacker", "inflictor"};
  static const char *const time_inputs[] = {"time", "elapsed"};
  static const char *const units[] = {"seconds", "milliseconds"};
  qa_native_mod_value value;
  memset(&value, 0, sizeof value);
  value.kind = fchoice(r, "kind", kinds, sizeof kinds / sizeof kinds[0]);
  const char *kind = value.kind;
  if (strcmp(kind, "user-command") == 0) {
    return value;
  } else if (strcmp(kind, "client") == 0 || strcmp(kind, "userinfo") == 0) {
    value.input = fchoice(r, "input", actor_inputs, 5);
  } else if (strcmp(kind, "actor") == 0) {
    value.record = fstr(r, "record");
    value.input = fchoice(r, "input", actor_inputs, 5);
  } else if (strcmp(kind, "time") == 0) {
    value.time_input = fchoice(r, "input", time_inputs, 2);
    value.units = fchoice(r, "units", units, 2);
    value.encoding = encoding_choice(r, "encoding");
  } else if (strcmp(kind, "address") == 0) {
    value.address_value = read_nullable_address(r, "value");
  } else {
    qa_save_reader f = field(r, "value");
    value.value = read_value(&f);
    qa_save_reader_destroy(&f);
  }
  return value;
}
static qa_native_mod_value *read_arguments(const qa_save_reader *r,
                                           const char *name, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_value *rows = (qa_native_mod_value *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_argument(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}
static qa_native_mod_global_value *read_globals(const qa_save_reader *r,
                                                const char *name,
                                                size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_global_value *rows =
      (qa_native_mod_global_value *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    qa_save_reader f = field(&entry, "address");
    rows[i].address = read_address(&f);
    qa_save_reader_destroy(&f);
    f = field(&entry, "value");
    rows[i].value = read_argument(&f);
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_native_mod_source_call read_source_call(const qa_save_reader *r) {
  qa_native_mod_source_call call;
  memset(&call, 0, sizeof call);
  {
    qa_save_reader entry = field(r, "entry");
    qa_save_reader kind = field(&entry, "kind");
    const char *kind_text = qa_save_reader_string(&kind);
    qa_save_reader_destroy(&kind);
    if (strcmp(kind_text, "game-export") == 0) {
      call.entry.kind = "game-export";
      call.entry.name = fstr(&entry, "name");
    } else {
      call.entry = read_entry(&entry);
    }
    qa_save_reader_destroy(&entry);
  }
  call.arguments = read_arguments(r, "arguments", &call.argument_count);
  call.globals = read_globals(r, "globals", &call.global_count);
  call.returns = fchoice(r, "returns", RETURNS, NRETURNS);
  if (fpresent(r, "skips")) {
    qa_save_reader list = field(r, "skips");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_mod_skip *rows = (qa_native_mod_skip *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].entry = fint(&entry, "entry", 0);
      rows[i].join = fint(&entry, "join", 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    call.skips = qa_mod_declaration_present(rows);
    call.skip_count = count;
  }
  return call;
}
static void read_source_call_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_native_mod_source_call *)out = read_source_call(r);
}
static qa_native_mod_source_call *read_source_calls(const qa_save_reader *r,
                                                    const char *name,
                                                    size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_source_call *rows =
      (qa_native_mod_source_call *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_source_call(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_native_mod_input_output read_input_output(const qa_save_reader *r) {
  static const char *const kinds[] = {"field", "handler"};
  static const char *const inputs[] = {"attack", "jump", "impulse",
                                       "forward-move", "side-move", "up-move"};
  qa_native_mod_input_output out;
  memset(&out, 0, sizeof out);
  out.kind = fchoice(r, "kind", kinds, 2);
  if (strcmp(out.kind, "field") == 0) {
    out.record = fstr(r, "record");
    out.offset = fint(r, "offset", 0);
    return out;
  }
  out.entry = read_named_entry(r, "entry");
  out.arguments = read_arguments(r, "arguments", &out.argument_count);
  qa_save_reader list = field(r, "inputs");
  size_t count = qa_save_reader_list_count(&list);
  const char **vals = (const char **)xalloc(r, count, sizeof *vals);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    vals[i] = qa_save_reader_choice_string(&entry, inputs, 6);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  out.inputs = (const qa_mod_client_input *)vals;
  out.input_count = count;
  return out;
}
static void read_input_output_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_native_mod_input_output *)out = read_input_output(r);
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

static qa_native_mod_actor_field read_actor_field(const qa_save_reader *r) {
  static const char *const bindings[] = {
      "health", "inventory", "inventory-capacity", "origin", "velocity",
      "angles", "bounds-min", "bounds-max", "record", "constant",
      "constant-vector", "private", "address", "team", "score"};
  qa_native_mod_actor_field result;
  memset(&result, 0, sizeof result);
  result.offset = fint(r, "offset", 0);
  result.binding = fchoice(r, "binding", bindings, sizeof bindings / sizeof bindings[0]);
  result.qvm_field.offset = result.offset;
  result.qvm_field.binding = result.binding;
  const char *binding = result.binding;
  if (strcmp(binding, "address") == 0) {
    result.address_value = read_nullable_address(r, "value");
  } else if (strcmp(binding, "team") == 0) {
    result.encoding = encoding_choice(r, "encoding");
    qa_save_reader f = field(r, "values");
    result.team_values = qa_read_source_team_values(&f, &result.team_value_count);
    qa_save_reader_destroy(&f);
    result.qvm_field.team_values = result.team_values;
    result.qvm_field.team_value_count = result.team_value_count;
  } else if (strcmp(binding, "score") == 0 || strcmp(binding, "health") == 0) {
    result.encoding = encoding_choice(r, "encoding");
  } else if (strcmp(binding, "inventory") == 0 ||
             strcmp(binding, "inventory-capacity") == 0) {
    result.encoding = encoding_choice(r, "encoding");
    result.item = fns(r, "item");
    result.qvm_field.item = result.item;
  } else if (strcmp(binding, "record") == 0) {
    result.qvm_field.record = fstr(r, "record");
  } else if (strcmp(binding, "constant") == 0) {
    result.encoding = encoding_choice(r, "encoding");
    result.constant_value = fnum(r, "value");
    result.qvm_field.constant_value = result.constant_value;
  } else if (strcmp(binding, "constant-vector") == 0) {
    qa_save_reader f = field(r, "value");
    result.qvm_field.constant_vector = qa_read_vector(&f);
    qa_save_reader_destroy(&f);
  } else if (strcmp(binding, "private") == 0) {
    result.qvm_field.byte_length = fint(r, "byteLength", 1);
  }
  return result;
}

static qa_native_mod_armor_field read_armor_field(const qa_save_reader *r) {
  qa_native_mod_armor_field armor;
  memset(&armor, 0, sizeof armor);
  armor.record = fstr(r, "record");
  armor.field.offset = fint(r, "offset", 0);
  armor.field.encoding = encoding_choice(r, "encoding");
  return armor;
}
static qa_native_mod_armor_field read_named_armor_field(const qa_save_reader *r,
                                                        const char *name) {
  qa_save_reader f = field(r, name);
  qa_native_mod_armor_field v = read_armor_field(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static void read_armor_field_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_native_mod_armor_field *)out = read_armor_field(r);
}
static qa_native_mod_record_offset read_record_offset(const qa_save_reader *r) {
  qa_native_mod_record_offset row;
  row.record = fstr(r, "record");
  row.offset = fint(r, "offset", 0);
  return row;
}
static void read_record_offset_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_native_mod_record_offset *)out = read_record_offset(r);
}
static qa_native_mod_armor_selection read_armor_selection(const qa_save_reader *r) {
  static const char *const kinds[] = {"positive", "enum"};
  qa_native_mod_armor_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.field = read_named_armor_field(r, "field");
  if (strcmp(fchoice(r, "kind", kinds, 2), "enum") == 0) {
    selection.is_enum = true;
    selection.value = fnum(r, "value");
    selection.none = fnum(r, "none");
  }
  return selection;
}
static qa_native_mod_armor_selection read_named_selection(const qa_save_reader *r,
                                                          const char *name) {
  qa_save_reader f = field(r, name);
  qa_native_mod_armor_selection v = read_armor_selection(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static qa_native_mod_regular_armor_item read_regular_armor(const qa_save_reader *r) {
  qa_native_mod_regular_armor_item row;
  memset(&row, 0, sizeof row);
  row.item = fns_nullable(r, "item");
  row.selection = read_named_selection(r, "selection");
  row.points = read_named_armor_field(r, "points");
  return row;
}
static qa_native_mod_regular_armor_item *read_regular_armors(
    const qa_save_reader *r, const char *name, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_regular_armor_item *rows =
      (qa_native_mod_regular_armor_item *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_regular_armor(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}
static qa_native_mod_power_armor_item read_power_armor(const qa_save_reader *r) {
  static const char *const kinds[] = {"screen", "shield"};
  qa_native_mod_power_armor_item row;
  memset(&row, 0, sizeof row);
  row.item = fns(r, "item");
  row.kind = fchoice(r, "kind", kinds, 2);
  row.selection = read_named_selection(r, "selection");
  row.cells = read_named_armor_field(r, "cells");
  qa_save_reader enabled = field(r, "enabled");
  if (!qa_save_reader_is_null(&enabled)) {
    qa_native_mod_armor_enabled *e =
        (qa_native_mod_armor_enabled *)xalloc(r, 1, sizeof *e);
    e->field = read_named_armor_field(&enabled, "field");
    e->mask = fint(&enabled, "mask", 1);
    row.enabled = e;
  }
  qa_save_reader_destroy(&enabled);
  return row;
}
static qa_native_mod_power_armor_item *read_power_armors(const qa_save_reader *r,
                                                         const char *name,
                                                         size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_power_armor_item *rows =
      (qa_native_mod_power_armor_item *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_power_armor(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_native_mod_armor read_armor(const qa_save_reader *r) {
  static const char *const kinds[] = {"none", "q2", "source"};
  qa_native_mod_armor armor;
  memset(&armor, 0, sizeof armor);
  armor.kind = fchoice(r, "kind", kinds, 3);
  if (strcmp(armor.kind, "none") == 0) return armor;
  if (strcmp(armor.kind, "source") == 0) {
    armor.regular = read_regular_armors(r, "regular", &armor.regular_count);
    armor.power = read_power_armors(r, "power", &armor.power_count);
    return armor;
  }
  qa_save_reader list = field(r, "regular");
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_q2_regular_armor_item *rows =
      (qa_native_mod_q2_regular_armor_item *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i].item = fns(&entry, "item");
    rows[i].selection = read_named_selection(&entry, "selection");
    rows[i].points = read_named_armor_field(&entry, "points");
    rows[i].normal_protection = fnum(&entry, "normalProtection");
    rows[i].energy_protection = fnum(&entry, "energyProtection");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  armor.q2_regular = rows;
  armor.q2_regular_count = count;
  armor.power = read_power_armors(r, "power", &armor.power_count);
  return armor;
}

static qa_native_mod_region_location read_region_location(const qa_save_reader *r) {
  static const char *const kinds[] = {"register", "stack", "simd"};
  static const char *const registers[] = {"rax", "rcx", "rdx", "rbx", "rbp",
                                          "rsi", "rdi", "r8", "r9", "r10",
                                          "r11", "r12", "r13", "r14", "r15"};
  qa_native_mod_region_location location;
  memset(&location, 0, sizeof location);
  const char *kind = fchoice(r, "kind", kinds, 3);
  if (strcmp(kind, "simd") == 0) {
    location.kind = QA_NATIVE_MOD_LOCATION_SIMD;
    location.index = fint(r, "index", 0);
    location.offset = fint(r, "offset", 0);
    location.storage = encoding_choice(r, "storage");
    return location;
  }
  {
    qa_save_reader storage = field(r, "storage");
    location.storage =
        (storage.value != NULL && storage.value->kind == QA_SAVE_STRING &&
         strcmp(storage.value->text, "pointer") == 0)
            ? "pointer"
            : qa_save_reader_choice_string(&storage, SCALARS, NSCALARS);
    qa_save_reader_destroy(&storage);
  }
  if (strcmp(kind, "stack") == 0) {
    location.kind = QA_NATIVE_MOD_LOCATION_STACK;
    location.offset = fint(r, "offset", 0);
    return location;
  }
  location.kind = QA_NATIVE_MOD_LOCATION_REGISTER;
  location.reg = fchoice(r, "register", registers, sizeof registers / sizeof registers[0]);
  return location;
}

static qa_native_mod_protection_definition read_protection(const qa_save_reader *r,
                                                           bool legacy) {
  static const char *const channels[] = {"regular", "powered"};
  static const char *const admission_kinds[] = {"claim", "replace-current-primary",
                                                "replace-primary"};
  static const char *const abis[] = {"q2-check-power-armor", "q2-check-armor",
                                     "source-call", "source-region"};
  static const char *const q2_flags[] = {"q2-classic", "q2-rerelease"};
  qa_native_mod_protection_definition prot;
  memset(&prot, 0, sizeof prot);
  prot.channel = legacy ? "powered" : fchoice(r, "channel", channels, 2);
  prot.id = fstr(r, "id");
  if (fpresent(r, "admission")) {
    qa_save_reader admission = field(r, "admission");
    const char *kind = fchoice(&admission, "kind", admission_kinds, 3);
    prot.admission.kind = kind;
    if (strcmp(kind, "replace-primary") == 0)
      prot.admission.owner = fns(&admission, "owner");
    qa_save_reader_destroy(&admission);
  }
  qa_save_reader absorb = field(r, "absorb");
  const char *abi = fchoice(&absorb, "abi", abis, 4);
  prot.absorb_abi = abi;
  bool regular = strcmp(prot.channel, "regular") == 0;
  if (strcmp(abi, "source-call") == 0 || strcmp(abi, "source-region") == 0) {
    if (legacy)
      qa_save_reader_fail(&absorb, "Legacy powered protection requires its original Q2 ABI");
    qa_save_reader call_reader = field(&absorb, "call");
    qa_native_mod_source_call call = read_source_call(&call_reader);
    qa_save_reader_destroy(&call_reader);
    if (strcmp(abi, "source-call") == 0) {
      prot.absorb_call = call;
    } else {
      qa_native_mod_protection_region *region =
          (qa_native_mod_protection_region *)xalloc(r, 1, sizeof *region);
      region->call = call;
      qa_save_reader frame = field(&absorb, "frame");
      region->frame.entry = fint(&frame, "entry", 0);
      region->frame.exit = fint(&frame, "exit", 0);
      region->frame.stack_bytes = fint(&frame, "stackBytes", 0);
      region->frame.argument_bytes = fint(&frame, "argumentBytes", 0);
      qa_save_reader_destroy(&frame);
      region->entry = fint(&absorb, "entry", 0);
      region->join = fint(&absorb, "join", 0);
      qa_save_reader result = field(&absorb, "result");
      region->result = read_region_location(&result);
      qa_save_reader_destroy(&result);
      qa_save_reader list = field(&absorb, "inputs");
      size_t count = qa_save_reader_list_count(&list);
      qa_native_mod_region_input *rows =
          (qa_native_mod_region_input *)xalloc(r, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        qa_save_reader target = field(&entry, "target");
        rows[i].target = read_region_location(&target);
        qa_save_reader_destroy(&target);
        qa_save_reader vf = field(&entry, "value");
        rows[i].value = read_argument(&vf);
        qa_save_reader_destroy(&vf);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      region->inputs = rows;
      region->input_count = count;
      prot.absorb_region = region;
      prot.absorb_call = call;
    }
  } else {
    prot.absorb_q2.entry = read_named_entry(&absorb, "entry");
    prot.absorb_q2.flags = fchoice(&absorb, "flags", q2_flags, 2);
    if (fpresent(&absorb, "globals"))
      prot.absorb_q2.globals =
          qa_mod_declaration_present(read_globals(&absorb, "globals", &prot.absorb_q2.global_count));
    if (regular) {
      if (strcmp(abi, "q2-check-armor") != 0)
        qa_save_reader_fail(&absorb,
                            "Regular protection requires its original regular armor ABI");
      prot.absorb_sparks = fint(&absorb, "sparks", 0);
    } else if (strcmp(abi, "q2-check-power-armor") != 0) {
      qa_save_reader_fail(&absorb,
                          "Powered protection requires its original power armor ABI");
    }
  }
  qa_save_reader_destroy(&absorb);
  if (regular) {
    prot.regular_storage = read_regular_armors(r, "storage", &prot.regular_storage_count);
  } else {
    prot.power_storage = read_power_armors(r, "storage", &prot.power_storage_count);
  }
  return prot;
}

static qa_native_mod_scalar_field read_scalar_field(const qa_save_reader *r) {
  qa_native_mod_scalar_field row;
  row.offset = fint(r, "offset", 0);
  row.encoding = encoding_choice(r, "encoding");
  return row;
}
static qa_native_mod_scalar_field read_named_scalar(const qa_save_reader *r,
                                                    const char *name) {
  qa_save_reader f = field(r, name);
  qa_native_mod_scalar_field v = read_scalar_field(&f);
  qa_save_reader_destroy(&f);
  return v;
}

static qa_native_mod_source_actors *read_source_actors(const qa_save_reader *r) {
  static const char *const units[] = {"seconds", "milliseconds"};
  static const char *const clock_inputs[] = {"time", "frame"};
  static const char *const callback_abis[] = {"q2-classic", "q2-rerelease"};
  static const char *const cause_editions[] = {"classic", "rerelease"};
  static const char *const cause_games[] = {"base", "xatrix", "rogue", "ctf"};
  qa_native_mod_source_actors *row =
      (qa_native_mod_source_actors *)xalloc(r, 1, sizeof *row);
  row->allocate = read_named_entry(r, "allocate");
  row->release = read_named_entry(r, "release");
  {
    qa_save_reader update = field(r, "update");
    row->update.entry = read_named_entry(&update, "entry");
    row->update.returns = fchoice(&update, "returns", RETURNS, NRETURNS);
    qa_save_reader_destroy(&update);
  }
  row->frame_seconds = fnum(r, "frameSeconds");
  {
    qa_save_reader list = field(r, "clock");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_mod_clock_entry *rows =
        (qa_native_mod_clock_entry *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      qa_save_reader f = field(&entry, "address");
      rows[i].address = read_address(&f);
      qa_save_reader_destroy(&f);
      rows[i].input = fchoice(&entry, "input", clock_inputs, 2);
      rows[i].encoding = encoding_choice(&entry, "encoding");
      rows[i].units = fchoice(&entry, "units", units, 2);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    row->clock = rows;
    row->clock_count = count;
  }
  if (fpresent(r, "callbacks")) {
    qa_save_reader callbacks = field(r, "callbacks");
    qa_native_mod_source_callbacks *c =
        (qa_native_mod_source_callbacks *)xalloc(r, 1, sizeof *c);
    c->abi = fchoice(&callbacks, "abi", callback_abis, 2);
    c->touch = nullable_int(&callbacks, "touch", &c->has_touch);
    c->pain = nullable_int(&callbacks, "pain", &c->has_pain);
    c->die = nullable_int(&callbacks, "die", &c->has_die);
    qa_save_reader_destroy(&callbacks);
    row->callbacks = c;
  }
  if (fpresent(r, "combat")) {
    qa_save_reader combat = field(r, "combat");
    qa_native_mod_combat *c = (qa_native_mod_combat *)xalloc(r, 1, sizeof *c);
    qa_save_reader damage = field(&combat, "damage");
    c->damage.entry = read_named_entry(&damage, "entry");
    c->damage.abi = fchoice(&damage, "abi", callback_abis, 2);
    qa_save_reader_destroy(&damage);
    qa_save_reader causes = field(&combat, "causes");
    c->causes.edition = fchoice(&causes, "edition", cause_editions, 2);
    if (strcmp(c->causes.edition, "classic") == 0)
      c->causes.game = fchoice(&causes, "game", cause_games, 4);
    qa_save_reader_destroy(&causes);
    c->health = read_named_scalar(&combat, "health");
    c->mass = read_named_scalar(&combat, "mass");
    c->takedamage = read_named_scalar(&combat, "takedamage");
    qa_save_reader flags = field(&combat, "flags");
    c->flags.field = read_scalar_field(&flags);
    c->flags.invulnerable = fint(&flags, "invulnerable", 0);
    c->flags.no_knockback = fint(&flags, "noKnockback", 0);
    qa_save_reader_destroy(&flags);
    qa_save_reader armor_reader = field(&combat, "armor");
    c->armor = read_armor(&armor_reader);
    qa_save_reader_destroy(&armor_reader);
    if (fpresent(&combat, "deferred")) {
      qa_save_reader deferred = field(&combat, "deferred");
      qa_native_mod_deferred_damage *d =
          (qa_native_mod_deferred_damage *)xalloc(r, 1, sizeof *d);
      d->process = read_named_entry(&deferred, "process");
      d->attacker = fint(&deferred, "attacker", 0);
      d->inflictor = fint(&deferred, "inflictor", 0);
      d->blood = read_named_scalar(&deferred, "blood");
      d->knockback = read_named_scalar(&deferred, "knockback");
      d->point = fint(&deferred, "point", 0);
      d->mod = fint(&deferred, "mod", 0);
      d->receipt = fint(&deferred, "receipt", 0);
      qa_save_reader_destroy(&deferred);
      c->deferred = d;
    }
    qa_save_reader_destroy(&combat);
    row->combat = c;
  }
  {
    qa_save_reader fields = field(r, "fields");
    row->fields.velocity = fint(&fields, "velocity", 0);
    row->fields.ground = fint(&fields, "ground", 0);
    row->fields.use = nullable_int(&fields, "use", &row->fields.has_use);
    row->fields.think = fint(&fields, "think", 0);
    qa_save_reader nextthink = field(&fields, "nextthink");
    row->fields.nextthink.offset = fint(&nextthink, "offset", 0);
    row->fields.nextthink.encoding = encoding_choice(&nextthink, "encoding");
    row->fields.nextthink.units = fchoice(&nextthink, "units", units, 2);
    qa_save_reader_destroy(&nextthink);
    qa_save_reader_destroy(&fields);
  }
  return row;
}

static qa_native_item_pointer read_pointer_row(const qa_save_reader *r) {
  qa_native_item_pointer row;
  row.record = fstr(r, "record");
  row.offset = fint(r, "offset", 0);
  return row;
}
static qa_native_item_test read_item_test(const qa_save_reader *r) {
  static const char *const kinds[] = {"scalar", "pointer"};
  static const char *const comparisons[] = {"equals", "at-most"};
  qa_native_item_test test;
  memset(&test, 0, sizeof test);
  test.kind = fchoice(r, "kind", kinds, 2);
  if (strcmp(test.kind, "pointer") == 0) {
    qa_save_reader f = field(r, "field");
    test.pointer_field = read_pointer_row(&f);
    qa_save_reader_destroy(&f);
    test.pointer_value = read_nullable_address(r, "value");
    return test;
  }
  test.field = read_named_armor_field(r, "field");
  {
    qa_save_reader mask = field(r, "mask");
    if (!qa_save_reader_is_null(&mask)) {
      test.has_mask = true;
      test.mask = qa_save_reader_integer_min(&mask, 0);
    }
    qa_save_reader_destroy(&mask);
  }
  test.comparison = fchoice(r, "comparison", comparisons, 2);
  test.scalar_value = fnum(r, "value");
  return test;
}
static qa_native_item_test_row *read_test_rows(const qa_save_reader *r,
                                               const char *name, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_native_item_test_row *rows =
      (qa_native_item_test_row *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader row_reader = item(&list, i);
    size_t tcount = qa_save_reader_list_count(&row_reader);
    qa_native_item_test *tests =
        (qa_native_item_test *)xalloc(r, tcount, sizeof *tests);
    for (size_t j = 0; j < tcount; j++) {
      qa_save_reader entry = item(&row_reader, j);
      tests[j] = read_item_test(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&row_reader);
    rows[i].tests = tests;
    rows[i].count = tcount;
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_native_mod_items *read_native_items(const qa_save_reader *r) {
  static const char *const admissions[] = {"add", "replace-primary"};
  static const char *const def_kinds[] = {"counter", "weapon"};
  static const char *const storage_kinds[] = {"counter", "bits"};
  static const char *const capacity_kinds[] = {"constant", "field", "source"};
  qa_native_mod_items *items = (qa_native_mod_items *)xalloc(r, 1, sizeof *items);
  {
    qa_save_reader list = field(r, "definitions");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_mod_item_definition *rows =
        (qa_native_mod_item_definition *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].item = fns(&entry, "item");
      rows[i].label = fstr(&entry, "label");
      if (fpresent(&entry, "icon")) {
        rows[i].has_icon = true;
        qa_save_reader f = field(&entry, "icon");
        rows[i].icon = qa_save_reader_is_null(&f) ? NULL : read_item_icon(&f);
        qa_save_reader_destroy(&f);
      }
      rows[i].admission = fchoice(&entry, "admission", admissions, 2);
      if (fpresent(&entry, "actions")) {
        qa_save_reader f = field(&entry, "actions");
        qa_source_item_action_calls actions = qa_read_item_actions(
            &f, read_source_call_adapter, NULL, sizeof(qa_native_mod_source_call));
        qa_save_reader_destroy(&f);
        rows[i].has_actions = true;
        rows[i].action_use = (const qa_native_mod_source_call *)actions.use_call;
        rows[i].action_drop = (const qa_native_mod_source_call *)actions.drop_call;
      }
      const char *kind = fchoice(&entry, "kind", def_kinds, 2);
      if (strcmp(kind, "weapon") == 0) {
        rows[i].is_weapon = true;
        if (fpresent(&entry, "held")) {
          qa_save_reader f = field(&entry, "held");
          rows[i].weapon.held = read_held_weapon(&f);
          qa_save_reader_destroy(&f);
        }
        rows[i].weapon.ammo = fns_nullable(&entry, "ammo");
      }
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    items->definitions = rows;
    items->definition_count = count;
  }
  {
    qa_save_reader list = field(r, "storage");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_item_storage *rows =
        (qa_native_item_storage *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].field = read_named_armor_field(&entry, "field");
      rows[i].kind = fchoice(&entry, "kind", storage_kinds, 2);
      if (strcmp(rows[i].kind, "bits") == 0) {
        rows[i].private_mask = fint(&entry, "privateMask", 0);
        qa_save_reader ilist = field(&entry, "items");
        size_t icount = qa_save_reader_list_count(&ilist);
        qa_native_item_mask *irows =
            (qa_native_item_mask *)xalloc(r, icount, sizeof *irows);
        for (size_t j = 0; j < icount; j++) {
          qa_save_reader e2 = item(&ilist, j);
          irows[j].item = fns(&e2, "item");
          irows[j].mask = fint(&e2, "mask", 1);
          qa_save_reader_destroy(&e2);
        }
        qa_save_reader_destroy(&ilist);
        rows[i].items = irows;
        rows[i].item_count = icount;
      } else {
        rows[i].item = fns(&entry, "item");
        qa_save_reader capacity = field(&entry, "capacity");
        const char *ckind = fchoice(&capacity, "kind", capacity_kinds, 3);
        rows[i].capacity.kind = ckind;
        if (strcmp(ckind, "constant") == 0) {
          rows[i].capacity.value = fnum(&capacity, "value");
        } else if (strcmp(ckind, "field") == 0) {
          rows[i].capacity.field = read_named_armor_field(&capacity, "field");
        } else {
          qa_save_reader f = field(&capacity, "address");
          rows[i].capacity.address = read_address(&f);
          qa_save_reader_destroy(&f);
          rows[i].capacity.encoding = encoding_choice(&capacity, "encoding");
        }
        qa_save_reader_destroy(&capacity);
      }
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    items->storage = rows;
    items->storage_count = count;
  }
  if (fpresent(r, "weapons")) {
    qa_save_reader weapons = field(r, "weapons");
    qa_native_weapon_stage *w = (qa_native_weapon_stage *)xalloc(r, 1, sizeof *w);
    qa_save_reader dispatcher = field(&weapons, "dispatcher");
    w->dispatcher.entry = read_named_entry(&dispatcher, "entry");
    w->dispatcher.record = fstr(&dispatcher, "record");
    w->dispatcher.argument = fint(&dispatcher, "argument", 0);
    w->dispatcher.arguments = fint(&dispatcher, "arguments", 1);
    qa_save_reader_destroy(&dispatcher);
    {
      qa_save_reader list = field(&weapons, "decisions");
      size_t count = qa_save_reader_list_count(&list);
      qa_native_weapon_decision *rows =
          (qa_native_weapon_decision *)xalloc(r, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].entry = fint(&entry, "entry", 0);
        rows[i].join = fint(&entry, "join", 0);
        qa_save_reader flist = field(&entry, "fields");
        size_t fcount = qa_save_reader_list_count(&flist);
        qa_native_weapon_decision_field *frows =
            (qa_native_weapon_decision_field *)xalloc(r, fcount, sizeof *frows);
        for (size_t j = 0; j < fcount; j++) {
          qa_save_reader e2 = item(&flist, j);
          frows[j].field = read_named_armor_field(&e2, "field");
          frows[j].clear_mask = fint(&e2, "clearMask", 1);
          qa_save_reader_destroy(&e2);
        }
        qa_save_reader_destroy(&flist);
        rows[i].fields = frows;
        rows[i].field_count = fcount;
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      w->decisions = rows;
      w->decision_count = count;
    }
    if (fpresent(&weapons, "committedInput"))
      w->committed_input =
          qa_mod_declaration_present(read_test_rows(&weapons, "committedInput", &w->committed_input_row_count));
    w->continuations =
        read_test_rows(&weapons, "continuations", &w->continuation_row_count);
    w->settled = read_test_rows(&weapons, "settled", &w->settled_row_count);
    {
      qa_save_reader selection = field(&weapons, "selection");
      qa_save_reader active = field(&selection, "active");
      w->selection.active = read_pointer_row(&active);
      qa_save_reader_destroy(&active);
      qa_save_reader pending = field(&selection, "pending");
      if (!qa_save_reader_is_null(&pending)) {
        qa_native_item_pointer *p =
            (qa_native_item_pointer *)xalloc(r, 1, sizeof *p);
        *p = read_pointer_row(&pending);
        w->selection.pending = p;
      }
      qa_save_reader_destroy(&pending);
      qa_save_reader list = field(&selection, "values");
      size_t count = qa_save_reader_list_count(&list);
      qa_native_weapon_selection_value *rows =
          (qa_native_weapon_selection_value *)xalloc(r, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].item = fns(&entry, "item");
        qa_save_reader f = field(&entry, "address");
        rows[i].address = read_address(&f);
        qa_save_reader_destroy(&f);
        f = field(&entry, "request");
        rows[i].request = read_source_call(&f);
        qa_save_reader_destroy(&f);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      w->selection.values = rows;
      w->selection.value_count = count;
      qa_save_reader_destroy(&selection);
    }
    qa_save_reader_destroy(&weapons);
    items->weapons = w;
  }
  return items;
}

static qa_native_mod_pickup_context *read_pickup_contexts(
    const qa_save_reader *pickup, size_t *out_count) {
  *out_count = 0;
  if (!fpresent(pickup, "context")) return NULL;
  qa_save_reader list = field(pickup, "context");
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_pickup_context *rows =
      (qa_native_mod_pickup_context *)xalloc(pickup, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    qa_save_reader vf = field(&entry, "value");
    qa_native_mod_value value = read_argument(&vf);
    qa_save_reader_destroy(&vf);
    const char *kind = value.kind;
    if (strcmp(kind, "actor") == 0 || strcmp(kind, "client") == 0 ||
        strcmp(kind, "userinfo") == 0 || strcmp(kind, "string") == 0 ||
        strcmp(kind, "user-command") == 0)
      qa_save_reader_fail(&entry,
                          "Native pickup context requires scalar, vector, time or image address values");
    rows[i].record = fstr(&entry, "record");
    rows[i].offset = fint(&entry, "offset", 0);
    rows[i].value = value;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_native_mod_client_input_field *read_input_fields(
    const qa_save_reader *clients, size_t *out_count) {
  qa_save_reader list = field(clients, "inputFields");
  size_t count = qa_save_reader_list_count(&list);
  qa_native_mod_client_input_field *rows =
      (qa_native_mod_client_input_field *)xalloc(clients, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    qa_save_reader vf = field(&entry, "value");
    qa_native_mod_value value = read_argument(&vf);
    qa_save_reader_destroy(&vf);
    const char *kind = value.kind;
    if (strcmp(kind, "actor") == 0 || strcmp(kind, "client") == 0 ||
        strcmp(kind, "userinfo") == 0 || strcmp(kind, "address") == 0 ||
        strcmp(kind, "string") == 0 || strcmp(kind, "user-command") == 0)
      qa_save_reader_fail(&entry,
                          "Native input fields require scalar, vector or time values");
    rows[i].record = fstr(&entry, "record");
    rows[i].offset = fint(&entry, "offset", 0);
    rows[i].value = value;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_native_mod_declaration *read_native_mod_declaration(
    const qa_save_reader *reader) {
  static const char *const target_kinds[] = {"q2-classic-game", "q2-rerelease-game"};
  static const char *const hud_choices[] = {"none", "layout-overlay", "replace-status"};
  static const char *const view_choices[] = {"none", "playerstate"};
  static const char *const accepts[] = {"always", "nonzero"};
  static const char *const base_kinds[] = {"entities", "clients", "address"};
  qa_native_mod_declaration *decl =
      (qa_native_mod_declaration *)qa_mod_declaration_calloc(1, sizeof *decl);
  if (decl == NULL) return NULL;
  {
    qa_save_reader target = field(reader, "target");
    qa_save_reader api = field(&target, "api");
    qa_save_reader abi = field(&target, "abi");
    const char *kind = fchoice(&api, "kind", target_kinds, 2);
    qa_save_reader f;
    if (strcmp(kind, "q2-classic-game") == 0) {
      decl->target.api.kind = QA_Q2_GAME_API_CLASSIC;
      f = field(&api, "version");
      decl->target.api.version = (int64_t)qa_save_reader_literal_number(&f, 3);
      qa_save_reader_destroy(&f);
      f = field(&abi, "kind");
      decl->target.abi.kind = qa_save_reader_literal_string(&f, "windows-i386");
      qa_save_reader_destroy(&f);
      f = field(&abi, "image");
      decl->target.abi.image = qa_save_reader_literal_string(&f, "pe32");
      qa_save_reader_destroy(&f);
      f = field(&abi, "pointerBytes");
      decl->target.abi.pointer_bytes = (int64_t)qa_save_reader_literal_number(&f, 4);
      qa_save_reader_destroy(&f);
      f = field(&abi, "call");
      decl->target.abi.call = qa_save_reader_literal_string(&f, "cdecl");
      qa_save_reader_destroy(&f);
    } else {
      decl->target.api.kind = QA_Q2_GAME_API_RERELEASE;
      f = field(&api, "version");
      decl->target.api.version = (int64_t)qa_save_reader_literal_number(&f, 2023);
      qa_save_reader_destroy(&f);
      f = field(&abi, "kind");
      decl->target.abi.kind = qa_save_reader_literal_string(&f, "windows-x86-64");
      qa_save_reader_destroy(&f);
      f = field(&abi, "image");
      decl->target.abi.image = qa_save_reader_literal_string(&f, "pe32+");
      qa_save_reader_destroy(&f);
      f = field(&abi, "pointerBytes");
      decl->target.abi.pointer_bytes = (int64_t)qa_save_reader_literal_number(&f, 8);
      qa_save_reader_destroy(&f);
      f = field(&abi, "call");
      decl->target.abi.call = qa_save_reader_literal_string(&f, "microsoft-x64");
      qa_save_reader_destroy(&f);
    }
    qa_save_reader_destroy(&abi);
    qa_save_reader_destroy(&api);
    qa_save_reader_destroy(&target);
  }
  {
    qa_save_reader f = field(reader, "version");
    decl->version = (int64_t)qa_save_reader_literal_number(&f, 1);
    qa_save_reader_destroy(&f);
    f = field(reader, "runtime");
    decl->runtime = qa_save_reader_literal_string(&f, "native");
    qa_save_reader_destroy(&f);
  }
  if (fpresent(reader, "items")) {
    qa_save_reader f = field(reader, "items");
    decl->items = read_native_items(&f);
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
  if (fpresent(reader, "clientPresentation")) {
    qa_save_reader f = field(reader, "clientPresentation");
    qa_native_mod_client_presentation *pres =
        (qa_native_mod_client_presentation *)xalloc(reader, 1, sizeof *pres);
    pres->hud = fchoice(&f, "hud", hud_choices, 3);
    pres->view = fchoice(&f, "view", view_choices, 2);
    qa_save_reader_destroy(&f);
    decl->client_presentation = pres;
  }
  if (fpresent(reader, "sourceActors")) {
    qa_save_reader f = field(reader, "sourceActors");
    decl->source_actors = read_source_actors(&f);
    qa_save_reader_destroy(&f);
  }
  {
    bool has_current = fpresent(reader, "protection");
    bool has_legacy = fpresent(reader, "poweredProtection");
    if (has_current && has_legacy) {
      qa_save_reader f = field(reader, "protection");
      qa_save_reader_fail(&f,
                          "Protection declarations cannot mix legacy and current layouts");
    }
    if (has_current) {
      qa_save_reader list = field(reader, "protection");
      size_t count = qa_save_reader_list_count(&list);
      qa_native_mod_protection_definition *rows =
          (qa_native_mod_protection_definition *)xalloc(reader, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i] = read_protection(&entry, false);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      decl->protection = qa_mod_declaration_present(rows);
      decl->protection_count = count;
    } else if (has_legacy) {
      qa_native_mod_protection_definition *rows =
          (qa_native_mod_protection_definition *)xalloc(reader, 1, sizeof *rows);
      qa_save_reader f = field(reader, "poweredProtection");
      rows[0] = read_protection(&f, true);
      qa_save_reader_destroy(&f);
      decl->protection = rows;
      decl->protection_count = 1;
    }
  }
  if (fpresent(reader, "pickups")) {
    qa_save_reader list = field(reader, "pickups");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_mod_pickup *rows =
        (qa_native_mod_pickup *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].rule = qa_read_mod_pickup_rule(&entry, read_source_call_adapter, NULL,
                                             sizeof(qa_native_mod_source_call));
      rows[i].context = read_pickup_contexts(&entry, &rows[i].context_count);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->pickups = qa_mod_declaration_present(rows);
    decl->pickup_count = count;
  }
  if (fpresent(reader, "clients")) {
    qa_save_reader clients = field(reader, "clients");
    qa_native_mod_clients *rows =
        (qa_native_mod_clients *)xalloc(reader, 1, sizeof *rows);
    rows->maximum = fint(&clients, "maximum", 1);
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
    {
      qa_save_reader list = field(&clients, "admit");
      size_t count = qa_save_reader_list_count(&list);
      qa_native_mod_admission_call *arows =
          (qa_native_mod_admission_call *)xalloc(reader, count, sizeof *arows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        arows[i].call = read_source_call(&entry);
        arows[i].accepts = fchoice(&entry, "accepts", accepts, 2);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      rows->admit = arows;
      rows->admit_count = count;
    }
    rows->userinfo = read_source_calls(&clients, "userinfo", &rows->userinfo_count);
    rows->disconnect =
        read_source_calls(&clients, "disconnect", &rows->disconnect_count);
    rows->command = read_source_calls(&clients, "command", &rows->command_count);
    if (fpresent(&clients, "input")) {
      qa_save_reader f = field(&clients, "input");
      rows->input = qa_read_mod_client_input(
          &f, read_source_call_adapter, NULL, sizeof(qa_native_mod_source_call),
          read_input_output_adapter, NULL, sizeof(qa_native_mod_input_output),
          &rows->input_count);
      rows->input = qa_mod_declaration_present(rows->input);
      qa_save_reader_destroy(&f);
    }
    if (fpresent(&clients, "frame"))
      rows->frame = qa_mod_declaration_present(read_source_calls(&clients, "frame", &rows->frame_count));
    if (fpresent(&clients, "endFrame"))
      rows->end_frame = qa_mod_declaration_present(read_source_calls(&clients, "endFrame", &rows->end_frame_count));
    if (fpresent(&clients, "outputs")) {
      qa_save_reader f = field(&clients, "outputs");
      rows->outputs = qa_read_client_output_declarations(
          &f, read_armor_field_adapter, NULL, sizeof(qa_native_mod_armor_field),
          read_record_offset_adapter, NULL, sizeof(qa_native_mod_record_offset),
          &rows->output_count);
      rows->outputs = qa_mod_declaration_present(rows->outputs);
      qa_save_reader_destroy(&f);
    }
    if (fpresent(&clients, "pose")) {
      qa_save_reader pose = field(&clients, "pose");
      qa_native_mod_client_pose *p =
          (qa_native_mod_client_pose *)xalloc(reader, 1, sizeof *p);
      p->view_height = read_named_armor_field(&pose, "viewHeight");
      qa_save_reader crouched = field(&pose, "crouched");
      p->crouched.field = read_named_armor_field(&crouched, "field");
      p->crouched.mask = fint(&crouched, "mask", 1);
      qa_save_reader_destroy(&crouched);
      qa_save_reader_destroy(&pose);
      rows->pose = p;
    }
    if (fpresent(&clients, "inputFields"))
      rows->input_fields = qa_mod_declaration_present(read_input_fields(&clients, &rows->input_field_count));
    qa_save_reader_destroy(&clients);
    decl->clients = rows;
  }
  {
    qa_save_reader list = field(reader, "cvars");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_named_string *rows =
        (qa_mod_named_string *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].name = fstr(&entry, "name");
      rows[i].value = fstr(&entry, "value");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->cvars = rows;
    decl->cvar_count = count;
  }
  decl->spawn_entities = fnullable_str(reader, "spawnEntities");
  decl->entity_record = fnullable_str(reader, "entityRecord");
  if (fpresent(reader, "objectives")) {
    qa_save_reader f = field(reader, "objectives");
    decl->objectives = qa_read_source_objectives(
        &f, read_objective_scalar_adapter, NULL,
        sizeof(qa_native_mod_objective_scalar), read_address_adapter, NULL,
        sizeof(qa_native_mod_address), read_source_call_adapter, NULL,
        sizeof(qa_native_mod_source_call), &decl->objective_count);
    decl->objectives = qa_mod_declaration_present(decl->objectives);
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader list = field(reader, "actorRecords");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_mod_actor_record *rows =
        (qa_native_mod_actor_record *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].id = fstr(&entry, "id");
      qa_save_reader base = field(&entry, "base");
      const char *bkind = fchoice(&base, "kind", base_kinds, 3);
      rows[i].base.kind = bkind;
      if (strcmp(bkind, "address") == 0) rows[i].base.address = read_address(&base);
      qa_save_reader_destroy(&base);
      rows[i].stride = fint(&entry, "stride", 4);
      rows[i].first_slot = fint(&entry, "firstSlot", 0);
      rows[i].capacity = fint(&entry, "capacity", 1);
      qa_save_reader flist = field(&entry, "fields");
      size_t fcount = qa_save_reader_list_count(&flist);
      qa_native_mod_actor_field *frows =
          (qa_native_mod_actor_field *)xalloc(reader, fcount, sizeof *frows);
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
  decl->project = read_source_calls(reader, "project", &decl->project_count);
  decl->release = read_source_calls(reader, "release", &decl->release_count);
  {
    qa_save_reader list = field(reader, "callbacks");
    size_t count = qa_save_reader_list_count(&list);
    qa_native_mod_callback *rows =
        (qa_native_mod_callback *)xalloc(reader, count, sizeof *rows);
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

/* Every block the reader allocates joins one owner (content/mods/declaration.h). */
qa_native_mod_declaration *qa_read_native_mod_declaration(
    const qa_save_reader *reader) {
  qa_mod_declaration_scope scope;
  qa_mod_declaration_scope_begin(&scope);
  qa_native_mod_declaration *decl = read_native_mod_declaration(reader);
  qa_mod_declaration_owner *owner = qa_mod_declaration_scope_end(&scope);
  if (decl == NULL) { qa_mod_declaration_owner_free(owner); return NULL; }
  decl->owner = owner;
  return decl;
}

qa_native_mod_declaration *qa_read_native_mod_callbacks(const uint8_t *bytes,
    size_t length) {
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return NULL;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, qa_mod_declaration_free_parsed_value, value);
  qa_save_reader reader;
  qa_native_mod_declaration *decl = NULL;
  if (qa_save_reader_init(&reader, value, NULL)) {
    decl = qa_read_native_mod_declaration(&reader);
    qa_save_reader_destroy(&reader);
  }
  qa_cleanup_pop(&cleanup);
  /* The declaration's strings borrow the parsed tree; its owner keeps it. */
  if (decl != NULL) qa_mod_declaration_owner_adopt_value(decl->owner, value);
  else qa_save_value_free(value);
  return decl;
}

