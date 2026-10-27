/* Port of src/content/mods/callbacks.ts. GPL-2.0-or-later. */
#include "qa/content/mods/callbacks.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/client_input.h"
#include "qa/content/mods/client_outputs.h"
#include "qa/content/mods/item_actions.h"
#include "qa/content/mods/match.h"
#include "qa/content/mods/pickups.h"
#include "qa/content/mods/source_call.h"
#include "qa/contracts/content.h"
#include "qa/contracts/qc_weapon_stage.h"
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

/* Mirror of normalizeResourcePath (TS owner:
 * src/content/mounts/paths.ts) until that module lands. Returns a
 * malloc'd normalized copy; invalid paths abort with the TS
 * RangeError message. */
static char *normalize_resource_path(const char *path) {
  size_t n = strlen(path);
  char *out = (char *)malloc(n + 1);
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

static qa_qc_statement read_statement_ab(const qa_save_reader *r, bool min0) {
  qa_qc_statement s;
  s.opcode = fint(r, "opcode", 0);
  if (min0) {
    s.a = fint(r, "a", 0);
    s.b = fint(r, "b", 0);
    s.c = fint(r, "c", 0);
  } else {
    qa_save_reader f = field(r, "a");
    s.a = qa_save_reader_integer(&f);
    qa_save_reader_destroy(&f);
    f = field(r, "b");
    s.b = qa_save_reader_integer(&f);
    qa_save_reader_destroy(&f);
    f = field(r, "c");
    s.c = qa_save_reader_integer(&f);
    qa_save_reader_destroy(&f);
  }
  return s;
}
static qa_qc_statement *read_statements(const qa_save_reader *r, bool min0,
                                        size_t *out_count) {
  size_t count = qa_save_reader_list_count(r);
  qa_qc_statement *rows = (qa_qc_statement *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(r, i);
    rows[i] = read_statement_ab(&entry, min0);
    qa_save_reader_destroy(&entry);
  }
  *out_count = count;
  return rows;
}

/* Mirror of readQcWeaponStageDeclaration (TS owner:
 * src/content/q1/quakec/weapon-stage-declaration.ts). */
static qa_qc_weapon_stage_declaration *read_weapon_stage(const qa_save_reader *r) {
  qa_qc_weapon_stage_declaration *stage =
      (qa_qc_weapon_stage_declaration *)xalloc(r, 1, sizeof *stage);
  stage->dispatcher = fstr(r, "dispatcher");
  {
    qa_save_reader list = field(r, "continuations");
    size_t count = qa_save_reader_list_count(&list);
    const char **vals = (const char **)xalloc(r, count, sizeof *vals);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      vals[i] = qa_save_reader_string(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    stage->continuations.values = vals;
    stage->continuations.count = count;
  }
  {
    qa_save_reader list = field(r, "repeats");
    size_t count = qa_save_reader_list_count(&list);
    qa_qc_weapon_stage_repeat *rows =
        (qa_qc_weapon_stage_repeat *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].function = fstr(&entry, "function");
      rows[i].entry = fint(&entry, "entry", 0);
      rows[i].exit = fint(&entry, "exit", 0);
      qa_save_reader result = field(&entry, "result");
      rows[i].result_word = fint(&result, "word", 28);
      {
        static const double choices[] = {0, 1};
        qa_save_reader vf = field(&result, "value");
        rows[i].result_value = (int)qa_save_reader_choice_number(&vf, choices, 2);
        qa_save_reader_destroy(&vf);
      }
      qa_save_reader_destroy(&result);
      qa_save_reader stmts = field(&entry, "statements");
      rows[i].statements = read_statements(&stmts, false, &rows[i].statement_count);
      qa_save_reader_destroy(&stmts);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    stage->repeats = rows;
    stage->repeat_count = count;
  }
  return stage;
}

/* Adapters for the generic readers in match/client-input/etc. */
static void read_source_call_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(qa_mod_source_call *)out = qa_read_mod_source_call(r);
}
static void read_string_adapter(void *ctx, const qa_save_reader *r, void *out) {
  (void)ctx;
  *(const char **)out = qa_save_reader_string(r);
}
static qa_mod_source_call *read_source_calls(const qa_save_reader *r,
                                             const char *name, size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_mod_source_call *rows = (qa_mod_source_call *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = qa_read_mod_source_call(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static qa_qc_mod_objective_storage read_objective_storage(const qa_save_reader *r) {
  qa_qc_mod_objective_storage storage;
  memset(&storage, 0, sizeof storage);
  if (r->value != NULL && r->value->kind == QA_SAVE_STRING) {
    storage.name = qa_save_reader_string(r);
    return storage;
  }
  storage.is_entity_field = true;
  {
    qa_save_reader f = field(r, "kind");
    (void)qa_save_reader_literal_string(&f, "entity-field");
    qa_save_reader_destroy(&f);
  }
  storage.global = fstr(r, "global");
  {
    qa_save_reader list = field(r, "indirections");
    size_t count = qa_save_reader_list_count(&list);
    const char **vals = (const char **)xalloc(r, count, sizeof *vals);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      vals[i] = qa_save_reader_string(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    storage.indirections.values = vals;
    storage.indirections.count = count;
  }
  storage.field = fstr(r, "field");
  return storage;
}
static void read_objective_storage_adapter(void *ctx, const qa_save_reader *r,
                                           void *out) {
  (void)ctx;
  *(qa_qc_mod_objective_storage *)out = read_objective_storage(r);
}

static qa_mod_actor_field read_actor_field(const qa_save_reader *r) {
  static const char *const bindings[] = {
      "health", "origin", "velocity", "angles", "bounds-min", "bounds-max",
      "think", "nextthink", "inventory", "constant", "private", "classname",
      "client-flags", "view-offset", "userinfo", "client-input", "team", "score"};
  static const char *const inputs[] = {"view-angles", "attack", "jump", "impulse",
                                       "forward-move", "side-move", "up-move"};
  static const char *const updates[] = {"always", "nonzero"};
  qa_mod_actor_field result;
  memset(&result, 0, sizeof result);
  result.field = fstr(r, "field");
  result.binding = fchoice(r, "binding", bindings, sizeof bindings / sizeof bindings[0]);
  if (strcmp(result.binding, "team") == 0) {
    qa_save_reader f = field(r, "values");
    result.team_values = qa_read_source_team_values(&f, &result.team_value_count);
    qa_save_reader_destroy(&f);
  } else if (strcmp(result.binding, "client-input") == 0) {
    result.input = fchoice(r, "input", inputs, 7);
    result.update = fchoice(r, "update", updates, 2);
    if (fpresent(r, "scale")) {
      result.has_scale = true;
      result.scale = fnum(r, "scale");
    }
  } else if (strcmp(result.binding, "client-flags") == 0) {
    if (fpresent(r, "grounded")) {
      qa_save_reader f = field(r, "grounded");
      result.has_grounded = qa_save_reader_literal_boolean(&f, true);
      qa_save_reader_destroy(&f);
    }
    if (fpresent(r, "privateMask")) {
      result.has_private_mask = true;
      result.private_mask = fint(r, "privateMask", 0);
    }
  } else if (strcmp(result.binding, "userinfo") == 0) {
    result.key = fstr(r, "key");
  } else if (strcmp(result.binding, "inventory") == 0) {
    result.item = fns(r, "item");
  } else if (strcmp(result.binding, "constant") == 0) {
    qa_save_reader f = field(r, "value");
    qa_mod_callback_value constant = qa_read_mod_source_value(&f);
    qa_save_reader_destroy(&f);
    if (strcmp(constant.kind, "input") == 0)
      qa_save_reader_fail(r, "actor field constants cannot reference callback inputs");
    result.constant_value = constant;
  }
  return result;
}

static qa_mod_callback read_callback(const qa_save_reader *r) {
  static const char *const operations[] = {
      "damage", "inventory.give", "inventory.consume", "actor.think",
      "actor.touch", "actor.use", "actor.pain", "actor.die"};
  static const char *const stages[] = {"observe", "transform", "replace"};
  static const char *const damage_results[] = {"amount", "knockback"};
  qa_mod_callback callback;
  memset(&callback, 0, sizeof callback);
  callback.binding.id = fns(r, "id");
  callback.call.function = fstr(r, "function");
  {
    qa_save_reader list = field(r, "arguments");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_callback_value *rows =
        (qa_mod_callback_value *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = qa_read_mod_source_value(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    callback.call.arguments = rows;
    callback.call.argument_count = count;
  }
  {
    qa_save_reader list = field(r, "globals");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_named_value *rows = (qa_mod_named_value *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].name = fstr(&entry, "name");
      qa_save_reader f = field(&entry, "value");
      rows[i].value = qa_read_mod_source_value(&f);
      qa_save_reader_destroy(&f);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    callback.call.globals = rows;
    callback.call.global_count = count;
  }
  if (callback.call.argument_count > 8)
    qa_save_reader_fail(r, "source callbacks accept at most eight arguments");
  callback.binding.operation = fchoice(r, "operation", operations, 8);
  const char *stage = fchoice(r, "stage", stages, 3);
  const char *op = callback.binding.operation;
  bool inventory = strcmp(op, "inventory.give") == 0 || strcmp(op, "inventory.consume") == 0;
  if (strcmp(stage, "observe") == 0) {
    callback.binding.stage = "observe";
  } else if (strcmp(stage, "transform") == 0) {
    if (strcmp(op, "damage") == 0) {
      callback.binding.stage = "transform";
      callback.binding.result = fchoice(r, "result", damage_results, 2);
    } else if (inventory) {
      callback.binding.stage = "transform";
      qa_save_reader f = field(r, "result");
      callback.binding.result = qa_save_reader_literal_string(&f, "amount");
      qa_save_reader_destroy(&f);
    } else {
      qa_save_reader_fail(r, "actor callbacks support observation or replacement");
    }
  } else {
    if (strcmp(op, "damage") == 0 || inventory)
      qa_save_reader_fail(r, "this source contract replaces actor callbacks only");
    callback.binding.stage = "replace";
    qa_save_reader f = field(r, "result");
    callback.binding.result = qa_save_reader_literal_string(&f, "boolean");
    qa_save_reader_destroy(&f);
  }
  return callback;
}

static qa_mod_console_value read_console_value(const qa_save_reader *r) {
  static const char *const kinds[] = {"float", "string", "vector",
                                      "argument", "arguments-text", "argument-count"};
  static const char *const types[] = {"string", "float"};
  qa_mod_console_value value;
  memset(&value, 0, sizeof value);
  value.kind = fchoice(r, "kind", kinds, 6);
  if (strcmp(value.kind, "float") == 0) {
    value.number_value = fnum(r, "value");
  } else if (strcmp(value.kind, "string") == 0) {
    value.string_value = fstr(r, "value");
  } else if (strcmp(value.kind, "vector") == 0) {
    qa_save_reader f = field(r, "value");
    value.vector_value = qa_read_vector(&f);
    qa_save_reader_destroy(&f);
  } else if (strcmp(value.kind, "argument") == 0) {
    value.index = fint(r, "index", 0);
    value.type = fchoice(r, "type", types, 2);
  }
  return value;
}

static qa_mod_qc_armor_stage read_armor_stage(const qa_save_reader *r) {
  static const char *const flag_kinds[] = {"none", "bits"};
  qa_mod_qc_armor_stage stage;
  memset(&stage, 0, sizeof stage);
  stage.function = fstr(r, "function");
  stage.entry = fint(r, "entry", 0);
  stage.exit = fint(r, "exit", 0);
  stage.target = fint(r, "target", 0);
  stage.damage = fint(r, "damage", 0);
  stage.saved = fint(r, "saved", 0);
  if (fpresent(r, "regularScale")) {
    qa_save_reader list = field(r, "regularScale");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_qc_armor_regular_scale *rows =
        (qa_mod_qc_armor_regular_scale *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].caller = fstr(&entry, "caller");
      rows[i].statement = fint(&entry, "statement", 0);
      rows[i].scale = fnum(&entry, "scale");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    stage.regular_scale = rows;
    stage.regular_scale_count = count;
  }
  {
    qa_save_reader flags = field(r, "flags");
    if (strcmp(fchoice(&flags, "kind", flag_kinds, 2), "bits") == 0) {
      stage.flags.is_bits = true;
      stage.flags.word = fint(&flags, "word", 0);
      stage.flags.no_armor = fint(&flags, "noArmor", 0);
      stage.flags.no_power_armor = fint(&flags, "noPowerArmor", 0);
      stage.flags.no_regular_armor = fint(&flags, "noRegularArmor", 0);
      stage.flags.energy = fint(&flags, "energy", 0);
    }
    qa_save_reader_destroy(&flags);
  }
  {
    qa_save_reader list = field(r, "statements");
    stage.statements = read_statements(&list, true, &stage.statement_count);
    qa_save_reader_destroy(&list);
  }
  return stage;
}

static qa_mod_protection_admission read_admission(const qa_save_reader *r) {
  static const char *const kinds[] = {"claim", "replace-primary",
                                      "replace-current-primary"};
  qa_mod_protection_admission admission;
  memset(&admission, 0, sizeof admission);
  admission.kind = fchoice(r, "kind", kinds, 3);
  if (strcmp(admission.kind, "replace-primary") == 0) admission.owner = fns(r, "owner");
  return admission;
}

static qa_mod_qc_protection read_protection(const qa_save_reader *r) {
  static const char *const absorb_kinds[] = {"function", "region"};
  static const char *const channels[] = {"regular", "powered"};
  static const char *const powered_kinds[] = {"screen", "shield"};
  static const char *const kind_values[] = {"none", "screen", "shield"};
  qa_mod_qc_protection prot;
  memset(&prot, 0, sizeof prot);
  prot.id = fns(r, "id");
  {
    qa_save_reader absorb = field(r, "absorb");
    bool region = strcmp(fchoice(&absorb, "kind", absorb_kinds, 2), "region") == 0;
    prot.absorb_is_region = region;
    qa_save_reader call = field(&absorb, "call");
    prot.absorb_call = qa_read_mod_source_call(&call);
    qa_save_reader_destroy(&call);
    if (region) {
      qa_save_reader stage = field(&absorb, "stage");
      prot.absorb_stage = read_armor_stage(&stage);
      qa_save_reader_destroy(&stage);
    }
    qa_save_reader_destroy(&absorb);
  }
  if (fpresent(r, "admission")) {
    qa_save_reader f = field(r, "admission");
    prot.admission = read_admission(&f);
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader flags = field(r, "flags");
    prot.flags.no_armor = fint(&flags, "noArmor", 0);
    prot.flags.no_power_armor = fint(&flags, "noPowerArmor", 0);
    prot.flags.no_regular_armor = fint(&flags, "noRegularArmor", 0);
    prot.flags.energy = fint(&flags, "energy", 0);
    prot.flags.radius = fint(&flags, "radius", 0);
    qa_save_reader_destroy(&flags);
  }
  prot.channel = fchoice(r, "channel", channels, 2);
  qa_save_reader storage = field(r, "storage");
  qa_save_reader selection = field(&storage, "selection");
  bool has_selection = !qa_save_reader_is_undefined(&selection);
  prot.has_selection = has_selection;
  if (has_selection) {
    prot.selection_field = fstr(&selection, "field");
    if (fpresent(&selection, "mask")) {
      prot.has_selection_mask = true;
      prot.selection_mask = fint(&selection, "mask", 0);
    }
  }
  if (strcmp(prot.channel, "regular") == 0) {
    prot.storage_points = fstr(&storage, "points");
    qa_save_reader f = field(&storage, "item");
    prot.storage_item = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
    qa_save_reader_destroy(&f);
    if (has_selection) {
      qa_save_reader list = field(&selection, "values");
      size_t count = qa_save_reader_list_count(&list);
      qa_mod_qc_protection_item_value *rows =
          (qa_mod_qc_protection_item_value *)xalloc(r, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].value = fnum(&entry, "value");
        qa_save_reader it = field(&entry, "item");
        rows[i].item = qa_save_reader_is_null(&it) ? NULL : qa_namespaced(&it);
        qa_save_reader_destroy(&it);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      prot.item_values = rows;
      prot.item_value_count = count;
    }
  } else {
    prot.storage_cells = fstr(&storage, "cells");
    prot.storage_kind = fchoice(&storage, "kind", powered_kinds, 2);
    if (has_selection) {
      qa_save_reader list = field(&selection, "values");
      size_t count = qa_save_reader_list_count(&list);
      qa_mod_qc_protection_kind_value *rows =
          (qa_mod_qc_protection_kind_value *)xalloc(r, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].value = fnum(&entry, "value");
        rows[i].kind = fchoice(&entry, "kind", kind_values, 3);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      prot.kind_values = rows;
      prot.kind_value_count = count;
    }
  }
  qa_save_reader_destroy(&selection);
  qa_save_reader_destroy(&storage);
  return prot;
}

static void read_weapon_values(const qa_save_reader *r, const char **out_field,
                               qa_mod_qc_weapon_value **out_values,
                               size_t *out_count) {
  *out_field = fstr(r, "field");
  qa_save_reader list = field(r, "values");
  size_t count = qa_save_reader_list_count(&list);
  qa_mod_qc_weapon_value *rows =
      (qa_mod_qc_weapon_value *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i].value = ffinite(&entry, "value");
    rows[i].item = fns(&entry, "item");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_values = rows;
  *out_count = count;
}

static qa_mod_qc_items *read_items(const qa_save_reader *r) {
  static const char *const admissions[] = {"add", "replace-primary"};
  static const char *const def_kinds[] = {"counter", "weapon"};
  static const char *const storage_kinds[] = {"counter", "bits"};
  static const char *const capacity_kinds[] = {"constant", "field"};
  qa_mod_qc_items *items = (qa_mod_qc_items *)xalloc(r, 1, sizeof *items);
  {
    qa_save_reader list = field(r, "definitions");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_qc_item_definition *rows =
        (qa_mod_qc_item_definition *)xalloc(r, count, sizeof *rows);
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
            &f, read_source_call_adapter, NULL, sizeof(qa_mod_source_call));
        qa_save_reader_destroy(&f);
        rows[i].has_actions = true;
        rows[i].action_use = (const qa_mod_source_call *)actions.use_call;
        rows[i].action_drop = (const qa_mod_source_call *)actions.drop_call;
      }
      const char *kind = fchoice(&entry, "kind", def_kinds, 2);
      if (strcmp(kind, "weapon") == 0) {
        rows[i].is_weapon = true;
        if (fpresent(&entry, "held")) {
          qa_save_reader f = field(&entry, "held");
          rows[i].weapon.held = read_held_weapon(&f);
          qa_save_reader_destroy(&f);
        }
        qa_save_reader f = field(&entry, "ammo");
        rows[i].weapon.ammo = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
        qa_save_reader_destroy(&f);
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
    qa_mod_qc_item_storage *rows =
        (qa_mod_qc_item_storage *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].field = fstr(&entry, "field");
      rows[i].kind = fchoice(&entry, "kind", storage_kinds, 2);
      if (strcmp(rows[i].kind, "bits") == 0) {
        rows[i].private_mask = fint(&entry, "privateMask", 0);
        qa_save_reader ilist = field(&entry, "items");
        size_t icount = qa_save_reader_list_count(&ilist);
        qa_mod_qc_item_mask *irows =
            (qa_mod_qc_item_mask *)xalloc(r, icount, sizeof *irows);
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
        const char *ckind = fchoice(&capacity, "kind", capacity_kinds, 2);
        if (strcmp(ckind, "constant") == 0) {
          rows[i].capacity.is_constant = true;
          rows[i].capacity.value = ffinite(&capacity, "value");
        } else {
          rows[i].capacity.field = fstr(&capacity, "field");
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
    qa_mod_qc_weapons *w = (qa_mod_qc_weapons *)xalloc(r, 1, sizeof *w);
    qa_save_reader f = field(&weapons, "stage");
    w->stage = read_weapon_stage(&f);
    qa_save_reader_destroy(&f);
    f = field(&weapons, "selected");
    qa_mod_qc_weapon_value *vals = NULL;
    read_weapon_values(&f, &w->selected_field, &vals, &w->selected_value_count);
    w->selected_values = vals;
    qa_save_reader_destroy(&f);
    f = field(&weapons, "select");
    read_weapon_values(&f, &w->select_field, &vals, &w->select_value_count);
    w->select_values = vals;
    qa_save_reader call = field(&f, "call");
    w->select_call = qa_read_mod_source_call(&call);
    qa_save_reader_destroy(&call);
    qa_save_reader_destroy(&f);
    w->resume = read_source_calls(&weapons, "resume", &w->resume_count);
    f = field(&weapons, "model");
    w->model_field = fstr(&f, "field");
    w->model_frame = fstr(&f, "frame");
    qa_save_reader_destroy(&f);
    qa_save_reader_destroy(&weapons);
    items->weapons = w;
  }
  return items;
}

qa_mod_qc_combat *qa_read_qc_combat(const qa_save_reader *reader) {
  static const char *const scale_kinds[] = {"multiplier", "identity", "transform"};
  static const char *const empty_items[] = {"q1:item_armor1", "q1:item_armor2",
                                            "q1:item_armorInv"};
  qa_mod_qc_combat *combat = (qa_mod_qc_combat *)calloc(1, sizeof *combat);
  if (combat == NULL) return NULL;
  qa_save_reader f = field(reader, "damage");
  combat->damage = qa_read_mod_source_call(&f);
  qa_save_reader_destroy(&f);
  if (fpresent(reader, "damageScale")) {
    qa_save_reader scale = field(reader, "damageScale");
    qa_mod_qc_damage_scale *row =
        (qa_mod_qc_damage_scale *)xalloc(reader, 1, sizeof *row);
    if (fpresent(&scale, "kind")) row->kind = fchoice(&scale, "kind", scale_kinds, 3);
    row->function = fstr(&scale, "function");
    row->entry = fint(&scale, "entry", 0);
    row->exit = fint(&scale, "exit", 0);
    row->damage = fint(&scale, "damage", 28);
    qa_save_reader list = field(&scale, "statements");
    row->statements = read_statements(&list, false, &row->statement_count);
    qa_save_reader_destroy(&list);
    qa_save_reader_destroy(&scale);
    combat->damage_scale = row;
  }
  if (fpresent(reader, "armorStage")) {
    qa_save_reader stage = field(reader, "armorStage");
    qa_mod_qc_armor_stage *row =
        (qa_mod_qc_armor_stage *)xalloc(reader, 1, sizeof *row);
    *row = read_armor_stage(&stage);
    qa_save_reader_destroy(&stage);
    combat->armor_stage = row;
  }
  if (fpresent(reader, "emptyArmor")) {
    qa_save_reader empty = field(reader, "emptyArmor");
    qa_mod_qc_empty_armor *row =
        (qa_mod_qc_empty_armor *)xalloc(reader, 1, sizeof *row);
    row->item = fchoice(&empty, "item", empty_items, 3);
    row->absorption = ffinite(&empty, "absorption");
    qa_save_reader_destroy(&empty);
    combat->empty_armor = row;
  }
  return combat;
}

static qa_mod_qc_input_output read_input_output(const qa_save_reader *r) {
  static const char *const kinds[] = {"field", "handler"};
  static const char *const inputs[] = {"attack", "jump", "impulse",
                                       "forward-move", "side-move", "up-move"};
  qa_mod_qc_input_output out;
  memset(&out, 0, sizeof out);
  if (strcmp(fchoice(r, "kind", kinds, 2), "field") == 0) {
    out.field = fstr(r, "field");
    return out;
  }
  out.is_handler = true;
  out.function = fstr(r, "function");
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
  *(qa_mod_qc_input_output *)out = read_input_output(r);
}

qa_mod_callback_declaration *qa_read_quakec_mod_declaration(
    const qa_save_reader *reader) {
  static const char *const hud_choices[] = {"none", "replace-vitals"};
  static const char *const view_choices[] = {"none", "set-view"};
  qa_mod_callback_declaration *decl =
      (qa_mod_callback_declaration *)calloc(1, sizeof *decl);
  if (decl == NULL) return NULL;
  {
    qa_save_reader f = field(reader, "version");
    decl->version = (int64_t)qa_save_reader_literal_number(&f, 1);
    qa_save_reader_destroy(&f);
    f = field(reader, "runtime");
    decl->runtime = qa_save_reader_literal_string(&f, "quakec");
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
  if (fpresent(reader, "objectives")) {
    qa_save_reader f = field(reader, "objectives");
    decl->objectives = qa_read_source_objectives(
        &f, read_objective_storage_adapter, NULL, sizeof(qa_qc_mod_objective_storage),
        read_objective_storage_adapter, NULL, sizeof(qa_qc_mod_objective_storage),
        read_source_call_adapter, NULL, sizeof(qa_mod_source_call),
        &decl->objective_count);
    qa_save_reader_destroy(&f);
  }
  {
    qa_save_reader list = field(reader, "actorFields");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_actor_field *rows = (qa_mod_actor_field *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = read_actor_field(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->actor_fields = rows;
    decl->actor_field_count = count;
  }
  {
    qa_save_reader list = field(reader, "callbacks");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_callback *rows = (qa_mod_callback *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = read_callback(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->callbacks = rows;
    decl->callback_count = count;
  }
  if (fpresent(reader, "clientPresentation")) {
    qa_save_reader f = field(reader, "clientPresentation");
    qa_qc_mod_client_presentation *pres =
        (qa_qc_mod_client_presentation *)xalloc(reader, 1, sizeof *pres);
    pres->hud = fchoice(&f, "hud", hud_choices, 2);
    pres->view = fchoice(&f, "view", view_choices, 2);
    qa_save_reader_destroy(&f);
    decl->client_presentation = pres;
  }
  if (fpresent(reader, "clients")) {
    qa_save_reader clients = field(reader, "clients");
    qa_mod_qc_clients *rows = (qa_mod_qc_clients *)xalloc(reader, 1, sizeof *rows);
    rows->maximum = fint(&clients, "maximum", 1);
    if (fpresent(&clients, "outputs")) {
      qa_save_reader f = field(&clients, "outputs");
      rows->outputs = qa_read_client_output_declarations(
          &f, read_string_adapter, NULL, sizeof(const char *), read_string_adapter,
          NULL, sizeof(const char *), &rows->output_count);
      qa_save_reader_destroy(&f);
    }
    rows->admit = read_source_calls(&clients, "admit", &rows->admit_count);
    rows->userinfo = read_source_calls(&clients, "userinfo", &rows->userinfo_count);
    rows->disconnect =
        read_source_calls(&clients, "disconnect", &rows->disconnect_count);
    if (fpresent(&clients, "frame"))
      rows->frame = read_source_calls(&clients, "frame", &rows->frame_count);
    if (fpresent(&clients, "input")) {
      qa_save_reader f = field(&clients, "input");
      rows->input = qa_read_mod_client_input(
          &f, read_source_call_adapter, NULL, sizeof(qa_mod_source_call),
          read_input_output_adapter, NULL, sizeof(qa_mod_qc_input_output),
          &rows->input_count);
      qa_save_reader_destroy(&f);
    }
    qa_save_reader_destroy(&clients);
    decl->clients = rows;
  }
  if (fpresent(reader, "initialize"))
    decl->initialize = read_source_calls(reader, "initialize", &decl->initialize_count);
  if (fpresent(reader, "frame")) {
    qa_save_reader f = field(reader, "frame");
    qa_mod_source_call *row = (qa_mod_source_call *)xalloc(reader, 1, sizeof *row);
    *row = qa_read_mod_source_call(&f);
    qa_save_reader_destroy(&f);
    decl->frame = row;
  }
  if (fpresent(reader, "cvars")) {
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
  if (fpresent(reader, "commands")) {
    qa_save_reader list = field(reader, "commands");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_console_command *rows =
        (qa_mod_console_command *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].name = fstr(&entry, "name");
      rows[i].function = fstr(&entry, "function");
      qa_save_reader alist = field(&entry, "arguments");
      size_t acount = qa_save_reader_list_count(&alist);
      qa_mod_console_value *avalues =
          (qa_mod_console_value *)xalloc(reader, acount, sizeof *avalues);
      for (size_t j = 0; j < acount; j++) {
        qa_save_reader e2 = item(&alist, j);
        avalues[j] = read_console_value(&e2);
        qa_save_reader_destroy(&e2);
      }
      qa_save_reader_destroy(&alist);
      rows[i].arguments = avalues;
      rows[i].argument_count = acount;
      qa_save_reader glist = field(&entry, "globals");
      size_t gcount = qa_save_reader_list_count(&glist);
      qa_mod_console_named_value *gvalues =
          (qa_mod_console_named_value *)xalloc(reader, gcount, sizeof *gvalues);
      for (size_t j = 0; j < gcount; j++) {
        qa_save_reader e2 = item(&glist, j);
        gvalues[j].name = fstr(&e2, "name");
        qa_save_reader vf = field(&e2, "value");
        gvalues[j].value = read_console_value(&vf);
        qa_save_reader_destroy(&vf);
        qa_save_reader_destroy(&e2);
      }
      qa_save_reader_destroy(&glist);
      rows[i].globals = gvalues;
      rows[i].global_count = gcount;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->commands = rows;
    decl->command_count = count;
  }
  if (fpresent(reader, "protection")) {
    qa_save_reader list = field(reader, "protection");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_qc_protection *rows =
        (qa_mod_qc_protection *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = read_protection(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->protection = rows;
    decl->protection_count = count;
  }
  if (fpresent(reader, "pickups")) {
    qa_save_reader list = field(reader, "pickups");
    size_t count = qa_save_reader_list_count(&list);
    qa_mod_pickup_rule *rows = (qa_mod_pickup_rule *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = qa_read_mod_pickup_rule(&entry, read_source_call_adapter, NULL,
                                        sizeof(qa_mod_source_call));
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    decl->pickups = rows;
    decl->pickup_count = count;
  }
  if (fpresent(reader, "items")) {
    qa_save_reader f = field(reader, "items");
    decl->items = read_items(&f);
    qa_save_reader_destroy(&f);
  }
  if (fpresent(reader, "combat")) {
    qa_save_reader f = field(reader, "combat");
    decl->combat = qa_read_qc_combat(&f);
    qa_save_reader_destroy(&f);
  }
  return decl;
}

qa_mod_callback_declaration *qa_read_mod_callbacks(const uint8_t *bytes,
                                                   size_t length) {
  /* TS: JSON.parse(new TextDecoder("utf-8", {fatal:true}).decode(bytes)).
   * The C JSON reader is lenient about UTF-8 (see value.h). The
   * parsed tree intentionally leaks: declarations borrow from it. */
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return NULL;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) return NULL;
  qa_mod_callback_declaration *decl = qa_read_quakec_mod_declaration(&reader);
  qa_save_reader_destroy(&reader);
  return decl;
}

