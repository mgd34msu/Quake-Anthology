/* Port of src/content/mods/qvm-items.ts. GPL-2.0-or-later. */
#include "qa/content/mods/qvm_items.h"
#include "qa/content/mods/declaration.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/item_actions.h"
#include "qa/contracts/content.h"
#include "qa/contracts/scene.h"
#include "qa/foundation/support.h"

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
static double ffinite(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  double v = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static bool fbool(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  bool v = qa_save_reader_boolean(&f);
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
  void *p = count > 0 ? qa_mod_declaration_calloc(count, size) : NULL;
  if (count > 0 && p == NULL) qa_save_reader_fail(r, "out of memory");
  return p;
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


static qa_qvm_item_field read_field(const qa_save_reader *r) {
  qa_qvm_item_field f;
  f.record = fstr(r, "record");
  f.offset = fint(r, "offset", 0);
  return f;
}
static qa_qvm_item_field read_named_field(const qa_save_reader *r, const char *name) {
  qa_save_reader f = field(r, name);
  qa_qvm_item_field v = read_field(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static qa_qvm_item_test read_test(const qa_save_reader *r) {
  static const char *const comparisons[] = {"equals", "at-most"};
  qa_qvm_item_test test;
  memset(&test, 0, sizeof test);
  test.field = read_named_field(r, "field");
  qa_save_reader mask = field(r, "mask");
  if (!qa_save_reader_is_null(&mask)) {
    test.has_mask = true;
    test.mask = qa_save_reader_integer_min(&mask, 0);
  }
  qa_save_reader_destroy(&mask);
  test.comparison = fchoice(r, "comparison", comparisons, 2);
  test.value = fint_any(r, "value");
  return test;
}
static qa_qvm_item_test *read_tests(const qa_save_reader *r, const char *name,
                                    size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_item_test *rows = (qa_qvm_item_test *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i] = read_test(&entry);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}
static qa_qvm_mod_input_pointer read_pointer(const qa_save_reader *r) {
  static const char *const kinds[] = {"argument", "global"};
  qa_qvm_mod_input_pointer pointer;
  memset(&pointer, 0, sizeof pointer);
  const char *kind = fchoice(r, "kind", kinds, 2);
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
  if (strcmp(kind, "argument") == 0) {
    pointer.index = fint(r, "index", 0);
  } else {
    pointer.is_global = true;
    pointer.address = fint(r, "address", 0);
  }
  return pointer;
}
static qa_qvm_weapon_actor read_actor(const qa_save_reader *r) {
  qa_qvm_weapon_actor actor;
  memset(&actor, 0, sizeof actor);
  actor.record = fstr(r, "record");
  qa_save_reader f = field(r, "pointer");
  actor.pointer = read_pointer(&f);
  qa_save_reader_destroy(&f);
  return actor;
}
static qa_qvm_weapon_actor read_named_actor(const qa_save_reader *r,
                                            const char *name) {
  qa_save_reader f = field(r, name);
  qa_qvm_weapon_actor v = read_actor(&f);
  qa_save_reader_destroy(&f);
  return v;
}
static qa_qvm_item_capacity read_capacity(const qa_save_reader *r) {
  static const char *const kinds[] = {"constant", "field", "source"};
  static const char *const comparisons[] = {"equals", "not-equals"};
  qa_qvm_item_capacity capacity;
  memset(&capacity, 0, sizeof capacity);
  capacity.kind = fchoice(r, "kind", kinds, 3);
  if (strcmp(capacity.kind, "constant") == 0) {
    capacity.value = fint(r, "value", 0);
  } else if (strcmp(capacity.kind, "field") == 0) {
    capacity.field = read_named_field(r, "field");
  } else {
    capacity.instruction = fint(r, "instruction", 0);
    qa_save_reader list = field(r, "overrides");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_item_capacity_override *rows =
        (qa_qvm_item_capacity_override *)xalloc(r, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].address = fint(&entry, "address", 0);
      rows[i].comparison = fchoice(&entry, "comparison", comparisons, 2);
      rows[i].value = fint_any(&entry, "value");
      rows[i].instruction = fint(&entry, "instruction", 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    capacity.overrides = rows;
    capacity.override_count = count;
  }
  return capacity;
}

qa_qvm_item_storage qa_read_qvm_item_storage(const qa_save_reader *reader) {
  static const char *const kinds[] = {"counter", "bits"};
  qa_qvm_item_storage storage;
  memset(&storage, 0, sizeof storage);
  storage.field = read_named_field(reader, "field");
  storage.kind = fchoice(reader, "kind", kinds, 2);
  if (strcmp(storage.kind, "counter") == 0) {
    storage.item = fns(reader, "item");
    qa_save_reader f = field(reader, "capacity");
    storage.capacity = read_capacity(&f);
    qa_save_reader_destroy(&f);
  } else {
    storage.private_mask = fint(reader, "privateMask", 0);
    qa_save_reader list = field(reader, "items");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_item_mask *rows = (qa_qvm_item_mask *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i].item = fns(&entry, "item");
      rows[i].mask = fint(&entry, "mask", 1);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    storage.items = rows;
    storage.item_count = count;
  }
  return storage;
}

static qa_qvm_weapon_predicate *read_predicates(const qa_save_reader *r,
                                                const char *name,
                                                size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_qvm_weapon_predicate *rows =
      (qa_qvm_weapon_predicate *)xalloc(r, count, sizeof *rows);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    rows[i].instruction = fint(&entry, "instruction", 0);
    rows[i].unselected = fbool(&entry, "unselected");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

qa_qvm_mod_items *qa_read_qvm_mod_items(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *read_ctx) {
  static const char *const admissions[] = {"add", "replace-primary"};
  static const char *const def_kinds[] = {"counter", "weapon"};
  qa_qvm_mod_items *items = (qa_qvm_mod_items *)qa_mod_declaration_calloc(1, sizeof *items);
  if (items == NULL) return NULL;
  {
    qa_save_reader list = field(reader, "definitions");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_mod_item_definition *rows =
        (qa_qvm_mod_item_definition *)xalloc(reader, count, sizeof *rows);
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
            &f, read_call, read_ctx, sizeof(qa_qvm_mod_source_call));
        qa_save_reader_destroy(&f);
        rows[i].has_actions = true;
        rows[i].action_use = (const qa_qvm_mod_source_call *)actions.use_call;
        rows[i].action_drop = (const qa_qvm_mod_source_call *)actions.drop_call;
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
    qa_save_reader list = field(reader, "storage");
    size_t count = qa_save_reader_list_count(&list);
    qa_qvm_item_storage *rows =
        (qa_qvm_item_storage *)xalloc(reader, count, sizeof *rows);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&list, i);
      rows[i] = qa_read_qvm_item_storage(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list);
    items->storage = rows;
    items->storage_count = count;
  }
  if (fpresent(reader, "weapons")) {
    qa_save_reader weapons = field(reader, "weapons");
    qa_qvm_mod_weapons *w = (qa_qvm_mod_weapons *)xalloc(reader, 1, sizeof *w);
    qa_save_reader input = field(&weapons, "input");
    w->input.entry = fint(&input, "entry", 0);
    w->input.clock = read_named_field(&input, "clock");
    qa_save_reader_destroy(&input);
    qa_save_reader stage = field(&weapons, "stage");
    qa_save_reader dispatcher = field(&stage, "dispatcher");
    w->stage.dispatcher.entry = fint(&dispatcher, "entry", 0);
    w->stage.dispatcher.actor = read_named_actor(&dispatcher, "actor");
    qa_save_reader_destroy(&dispatcher);
    w->stage.predicates =
        read_predicates(&stage, "predicates", &w->stage.predicate_count);
    w->stage.settled = read_tests(&stage, "settled", &w->stage.settled_count);
    {
      qa_save_reader selection = field(&stage, "selection");
      w->stage.selection.field = read_named_field(&selection, "field");
      qa_save_reader list = field(&selection, "values");
      size_t count = qa_save_reader_list_count(&list);
      qa_qvm_weapon_selection_value *rows =
          (qa_qvm_weapon_selection_value *)xalloc(reader, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].value = fint_any(&entry, "value");
        rows[i].item = fns(&entry, "item");
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      w->stage.selection.values = rows;
      w->stage.selection.value_count = count;
      qa_save_reader_destroy(&selection);
    }
    {
      qa_save_reader request = field(&stage, "request");
      w->stage.request.entry = fint(&request, "entry", 0);
      w->stage.request.argument = fint(&request, "argument", 0);
      w->stage.request.accepted =
          read_tests(&request, "accepted", &w->stage.request.accepted_count);
      qa_save_reader_destroy(&request);
    }
    {
      qa_save_reader cont = field(&stage, "continuation");
      qa_qvm_weapon_continuation *c = &w->stage.continuation;
      c->entry = fint(&cont, "entry", 0);
      c->actor = read_named_actor(&cont, "actor");
      c->instruction = fint(&cont, "instruction", 0);
      c->original_taken = fbool(&cont, "originalTaken");
      c->when = read_tests(&cont, "when", &c->when_count);
      c->predicates = read_predicates(&cont, "predicates", &c->predicate_count);
      qa_save_reader projection = field(&cont, "projection");
      qa_save_reader movement = field(&projection, "movement");
      c->projection.movement = read_pointer(&movement);
      qa_save_reader_destroy(&movement);
      c->projection.byte_length = fint(&projection, "byteLength", 1);
      c->projection.minimum = fint(&projection, "minimum", 0);
      c->projection.maximum = fint(&projection, "maximum", 0);
      c->projection.view_height = read_named_field(&projection, "viewHeight");
      c->projection.ground = read_named_field(&projection, "ground");
      qa_save_reader_destroy(&projection);
      qa_save_reader list = field(&cont, "calls");
      size_t count = qa_save_reader_list_count(&list);
      qa_qvm_weapon_continuation_call *rows =
          (qa_qvm_weapon_continuation_call *)xalloc(reader, count, sizeof *rows);
      for (size_t i = 0; i < count; i++) {
        qa_save_reader entry = item(&list, i);
        rows[i].instruction = fint(&entry, "instruction", 0);
        qa_save_reader call = field(&entry, "call");
        read_call(read_ctx, &call, &rows[i].call);
        qa_save_reader_destroy(&call);
        qa_save_reader_destroy(&entry);
      }
      qa_save_reader_destroy(&list);
      c->calls = rows;
      c->call_count = count;
      qa_save_reader_destroy(&cont);
    }
    qa_save_reader_destroy(&stage);
    qa_save_reader_destroy(&weapons);
    items->weapons = w;
  }
  return items;
}
