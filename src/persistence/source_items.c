/* Port of src/persistence/source-items.ts. The icon/held/grip/path
 * readers are mirrored statics of src/content/item-icon.ts
 * (readSourceItemIcon), src/content/held-weapon.ts
 * (readHeldWeaponDeclaration), src/content/model-attachment.ts
 * (readModelGrip) and src/content/mounts/paths.ts
 * (normalizeResourcePath); the content port replaces them.
 * GPL-2.0-or-later. */
#include "qa/persistence/source_items.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_image.h"

static const char k_schema[] = "world:source-items";

static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}
static bool same_text(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}
/* isDeepStrictEqual number equality: NaN matches NaN, -0 differs
 * from +0 (the TS prebound body test relies on the latter). */
static bool same_number(double left, double right) {
  if (left == right) return left != 0 || signbit(left) == signbit(right);
  return isnan(left) && isnan(right);
}
static bool same_saved_actor(qa_saved_actor_id left, qa_saved_actor_id right) {
  /* TS Set key is `${slot}/${generation}`; String(-0) is "0", so
   * plain == (where -0 == 0) matches the key comparison. */
  return left.slot == right.slot && left.generation == right.generation;
}
static bool same_owned(qa_owned_actor left, qa_owned_actor right) {
  return left.token == right.token && qa_same_actor(left.id, right.id) &&
         same_text(left.owner, right.owner);
}

/* --- SaveReader helpers (q2_classic_guest.c precedent) --- */
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
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) { qa_save_value_free(child); return false; }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) { qa_save_value_free(child); return false; }
  return true;
}

/* --- mirrored content readers --- */
/* Ownership pool for normalized paths inside a decode result. */
static const char *pool_string(qa_source_items_result *result, char *text,
                               const qa_save_reader *reader) {
  if (text == NULL) qa_save_reader_fail(reader, "out of memory");
  if (result->owned_string_count == result->owned_string_capacity) {
    size_t grown = result->owned_string_capacity == 0
        ? 8 : result->owned_string_capacity * 2;
    char **strings = (char **)realloc(result->owned_strings, grown * sizeof *strings);
    if (strings == NULL) { free(text); qa_save_reader_fail(reader, "out of memory"); }
    result->owned_strings = strings;
    result->owned_string_capacity = grown;
  }
  result->owned_strings[result->owned_string_count++] = text;
  return text;
}
/* Mirror of normalizeResourcePath (content/mounts/paths.ts).
 * Malloc'd; NULL on OOM. The TS interior-NUL rejection is
 * unobservable for C strings, which end at the first NUL. */
static char *normalize_resource_path(const char *path) {
  size_t length = strlen(path);
  char *normalized = (char *)malloc(length + 1);
  if (normalized == NULL) return NULL;
  memcpy(normalized, path, length + 1);
  for (size_t i = 0; i < length; i++)
    if (normalized[i] == '\\') normalized[i] = '/';
  bool drive = length >= 2 && normalized[1] == ':' &&
      ((normalized[0] >= 'a' && normalized[0] <= 'z') ||
       (normalized[0] >= 'A' && normalized[0] <= 'Z'));
  bool invalid = length == 0 || drive;
  const char *part = normalized;
  while (!invalid) {
    const char *slash = strchr(part, '/');
    size_t part_length = slash != NULL ? (size_t)(slash - part) : strlen(part);
    if (part_length == 0 || (part_length == 1 && part[0] == '.') ||
        (part_length == 2 && part[0] == '.' && part[1] == '.')) invalid = true;
    if (slash == NULL) break;
    part = slash + 1;
  }
  if (invalid) {
    free(normalized);
    qa_range_error("Invalid relative resource path: %s", path);
  }
  return normalized;
}
static const char *normalized_field(qa_source_items_result *result,
                                    const qa_save_reader *reader) {
  return pool_string(result, normalize_resource_path(qa_save_reader_string(reader)),
                     reader);
}
static double dot3(qa_vec3 left, qa_vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}
static qa_vec3 cross3(qa_vec3 left, qa_vec3 right) {
  qa_vec3 out = {left.y * right.z - left.z * right.y,
                 left.z * right.x - left.x * right.z,
                 left.x * right.y - left.y * right.x};
  return out;
}
static qa_vec3 read_finite_vector(const qa_save_reader *reader) {
  qa_vec3 out;
  qa_save_reader x = field(reader, "x");
  out.x = qa_save_reader_finite(&x);
  qa_save_reader_destroy(&x);
  qa_save_reader y = field(reader, "y");
  out.y = qa_save_reader_finite(&y);
  qa_save_reader_destroy(&y);
  qa_save_reader z = field(reader, "z");
  out.z = qa_save_reader_finite(&z);
  qa_save_reader_destroy(&z);
  return out;
}
/* Mirror of readModelGrip (content/model-attachment.ts). The TS
 * throws there are plain Errors. */
static qa_model_transform read_model_grip(const qa_save_reader *grip) {
  qa_model_transform out;
  memset(&out, 0, sizeof out);
  qa_save_reader axis = field(grip, "axis");
  size_t axis_count = qa_save_reader_list_count(&axis);
  qa_vec3 vectors[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  for (size_t i = 0; i < axis_count; i++) {
    qa_save_reader entry = item(&axis, i);
    qa_vec3 vector = read_finite_vector(&entry);
    if (i < 3) vectors[i] = vector;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&axis);
  if (axis_count != 3) qa_error("Model grip requires three source axes");
  if (fabs(dot3(vectors[0], vectors[0]) - 1) > 0.001 ||
      fabs(dot3(vectors[1], vectors[1]) - 1) > 0.001 ||
      fabs(dot3(vectors[2], vectors[2]) - 1) > 0.001 ||
      fabs(dot3(vectors[0], vectors[1])) > 0.001 ||
      fabs(dot3(vectors[0], vectors[2])) > 0.001 ||
      fabs(dot3(vectors[1], vectors[2])) > 0.001 ||
      dot3(cross3(vectors[0], vectors[1]), vectors[2]) < 0.999)
    qa_error("Model grip axes must form a rotation");
  qa_save_reader scale = field(grip, "scale");
  if (qa_save_reader_is_undefined(&scale)) {
    out.scale.x = 1;
    out.scale.y = 1;
    out.scale.z = 1;
  } else {
    out.scale = read_finite_vector(&scale);
  }
  qa_save_reader_destroy(&scale);
  if (out.scale.x == 0 || out.scale.y == 0 || out.scale.z == 0)
    qa_save_reader_fail(grip, "Model grip scale must be invertible");
  qa_save_reader origin = field(grip, "origin");
  out.origin = read_finite_vector(&origin);
  qa_save_reader_destroy(&origin);
  out.axis.vectors[0] = vectors[0];
  out.axis.vectors[1] = vectors[1];
  out.axis.vectors[2] = vectors[2];
  return out;
}
/* Mirror of readSourceItemIcon (content/item-icon.ts). The WAD
 * lump interior-NUL check is unobservable for C strings. */
static void read_source_item_icon(qa_source_items_result *result,
                                  const qa_save_reader *reader,
                                  qa_content_id content, qa_weapon_hud_icon *out) {
  static const char *const kinds[] = {"image", "wad-picture", "shader"};
  memset(out, 0, sizeof *out);
  qa_save_reader kind_field = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_field, kinds, 3);
  qa_save_reader_destroy(&kind_field);
  bool shader = strcmp(kind, "shader") == 0;
  qa_save_reader resource;
  bool own_resource = false;
  if (shader) {
    resource = *reader;
  } else {
    resource = field(reader, "resource");
    own_resource = true;
  }
  qa_save_reader content_field = field(&resource, "content");
  const char *actual = qa_save_reader_string(&content_field);
  qa_save_reader_destroy(&content_field);
  if (!same_text(actual, content))
    qa_save_reader_fail(reader, "Item icon belongs to another content source");
  if (shader) {
    out->kind = QA_WEAPON_HUD_ICON_SHADER;
    out->content = content;
    qa_save_reader name = field(reader, "name");
    out->name = normalized_field(result, &name);
    qa_save_reader_destroy(&name);
    return;
  }
  qa_save_reader path = field(&resource, "path");
  const char *resource_path = normalized_field(result, &path);
  qa_save_reader_destroy(&path);
  if (own_resource) qa_save_reader_destroy(&resource);
  out->resource.content = content;
  out->resource.path = resource_path;
  if (strcmp(kind, "image") == 0) {
    out->kind = QA_WEAPON_HUD_ICON_IMAGE;
    return;
  }
  out->kind = QA_WEAPON_HUD_ICON_WAD_PICTURE;
  qa_save_reader lump = field(reader, "lump");
  out->lump = qa_save_reader_string(&lump);
  qa_save_reader_destroy(&lump);
  size_t lump_length = strlen(out->lump);
  if (lump_length == 0 || lump_length > 16)
    qa_save_reader_fail(reader, "Item icon requires a valid WAD lump name");
}
static const char *read_digest(const qa_save_reader *reader) {
  const char *parsed = qa_save_reader_string(reader);
  if (!qa_is_content_digest(parsed))
    qa_save_reader_fail(reader, "held model requires a SHA256 digest");
  return parsed;
}
/* Mirror of readHeldWeaponDeclaration (content/held-weapon.ts).
 * out->model.part arrays are malloc'd for the result to free. */
static void read_held_weapon_declaration(qa_source_items_result *result,
                                         const qa_save_reader *reader,
                                         qa_held_weapon_declaration *out) {
  static const char *const kinds[] = {"none", "model"};
  memset(out, 0, sizeof *out);
  qa_save_reader kind_field = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_field, kinds, 2);
  qa_save_reader_destroy(&kind_field);
  if (strcmp(kind, "none") == 0) return;
  out->is_model = true;
  qa_save_reader model = field(reader, "model");
  qa_save_reader part = field(&model, "part");
  qa_save_reader digest = field(&model, "digest");
  qa_held_weapon_part *subset = NULL;
  if (!qa_save_reader_is_undefined(&part)) {
    subset = (qa_held_weapon_part *)calloc(1, sizeof *subset);
    if (subset == NULL) qa_save_reader_fail(&part, "out of memory");
    qa_save_reader digests = field(&part, "digests");
    size_t digest_count = qa_save_reader_list_count(&digests);
    const char **digest_values =
        (const char **)calloc(digest_count != 0 ? digest_count : 1,
                              sizeof *digest_values);
    if (digest_values == NULL) qa_save_reader_fail(&digests, "out of memory");
    for (size_t i = 0; i < digest_count; i++) {
      qa_save_reader entry = item(&digests, i);
      digest_values[i] = read_digest(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&digests);
    subset->digests.values = digest_values;
    subset->digests.count = digest_count;
    qa_save_reader vertices = field(&part, "vertices");
    size_t vertex_count = qa_save_reader_list_count(&vertices);
    double *vertex_values =
        (double *)calloc(vertex_count != 0 ? vertex_count : 1, sizeof *vertex_values);
    if (vertex_values == NULL) qa_save_reader_fail(&vertices, "out of memory");
    for (size_t i = 0; i < vertex_count; i++) {
      qa_save_reader entry = item(&vertices, i);
      vertex_values[i] = qa_save_reader_integer_min(&entry, 0);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&vertices);
    subset->vertices.values = vertex_values;
    subset->vertices.count = vertex_count;
    bool duplicate = false;
    for (size_t i = 0; i < vertex_count && !duplicate; i++)
      for (size_t j = i + 1; j < vertex_count; j++)
        if (vertex_values[j] == vertex_values[i]) duplicate = true;
    if (digest_count == 0 || vertex_count == 0 || duplicate)
      qa_save_reader_fail(&part,
          "held model subset requires source digests and distinct vertices");
  }
  qa_save_reader path = field(&model, "path");
  out->model.path = normalized_field(result, &path);
  qa_save_reader_destroy(&path);
  qa_save_reader frame = field(&model, "referenceFrame");
  out->model.reference_frame = qa_save_reader_integer_min(&frame, 0);
  qa_save_reader_destroy(&frame);
  qa_save_reader grip = field(&model, "grip");
  out->model.grip = read_model_grip(&grip);
  qa_save_reader_destroy(&grip);
  if (!qa_save_reader_is_undefined(&digest)) {
    out->model.has_digest = true;
    out->model.digest = read_digest(&digest);
  }
  qa_save_reader fallback = field(&model, "fallback");
  if (!qa_save_reader_is_undefined(&fallback))
    out->model.fallback = normalized_field(result, &fallback);
  qa_save_reader_destroy(&fallback);
  out->model.part = subset;
  qa_save_reader_destroy(&digest);
  qa_save_reader_destroy(&part);
  qa_save_reader_destroy(&model);
}

/* TS definition() in source-items.ts. */
static void read_definition(qa_source_items_result *result,
                            const qa_save_reader *reader,
                            qa_source_item_definition *out) {
  static const char *const action_names[] = {"use", "drop"};
  static const char *const kinds[] = {"counter", "weapon"};
  memset(out, 0, sizeof *out);
  qa_save_reader source = field(reader, "source");
  out->source = qa_read_provider(&source);
  qa_save_reader_destroy(&source);
  qa_save_reader item_field = field(reader, "item");
  out->item = qa_namespaced(&item_field);
  qa_save_reader_destroy(&item_field);
  qa_save_reader label = field(reader, "label");
  out->label = qa_save_reader_string(&label);
  qa_save_reader_destroy(&label);
  qa_save_reader icon = field(reader, "icon");
  if (!qa_save_reader_is_undefined(&icon)) {
    out->has_icon = true;
    if (!qa_save_reader_is_null(&icon)) {
      qa_weapon_hud_icon *decoded =
          (qa_weapon_hud_icon *)calloc(1, sizeof *decoded);
      if (decoded == NULL) qa_save_reader_fail(&icon, "out of memory");
      read_source_item_icon(result, &icon, out->source.content, decoded);
      out->icon = decoded;
    }
  }
  qa_save_reader_destroy(&icon);
  qa_save_reader actions = field(reader, "actions");
  bool has_actions = !qa_save_reader_is_undefined(&actions);
  if (has_actions) {
    size_t count = qa_save_reader_list_count(&actions);
    qa_source_item_action *decoded =
        (qa_source_item_action *)calloc(count != 0 ? count : 1, sizeof *decoded);
    if (decoded == NULL) qa_save_reader_fail(&actions, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&actions, i);
      const char *action = qa_save_reader_choice_string(&entry, action_names, 2);
      decoded[i] = strcmp(action, "use") == 0 ? QA_SOURCE_ITEM_ACTION_USE
                                              : QA_SOURCE_ITEM_ACTION_DROP;
      qa_save_reader_destroy(&entry);
    }
    out->actions = decoded;
    out->action_count = count;
  }
  qa_save_reader_destroy(&actions);
  if (has_actions) {
    bool duplicate = false;
    for (size_t i = 0; i < out->action_count && !duplicate; i++)
      for (size_t j = i + 1; j < out->action_count; j++)
        if (out->actions[j] == out->actions[i]) duplicate = true;
    if (out->action_count == 0 || duplicate)
      qa_save_reader_fail(reader, "Source item actions are empty or duplicated");
  }
  if (strlen(out->label) == 0)
    qa_save_reader_fail(reader, "source item label is empty");
  qa_save_reader kind_field = field(reader, "kind");
  const char *kind = qa_save_reader_choice_string(&kind_field, kinds, 2);
  qa_save_reader_destroy(&kind_field);
  if (strcmp(kind, "counter") == 0) return;
  out->is_weapon = true;
  qa_save_reader held = field(reader, "held");
  if (!qa_save_reader_is_undefined(&held)) {
    qa_held_weapon_declaration *decoded =
        (qa_held_weapon_declaration *)calloc(1, sizeof *decoded);
    if (decoded == NULL) qa_save_reader_fail(&held, "out of memory");
    read_held_weapon_declaration(result, &held, decoded);
    out->weapon.held = decoded;
  }
  qa_save_reader_destroy(&held);
  qa_save_reader ammo = field(reader, "ammo");
  out->weapon.ammo = qa_save_reader_is_null(&ammo) ? NULL : qa_namespaced(&ammo);
  qa_save_reader_destroy(&ammo);
}

/* --- deep equality (isDeepStrictEqual over the contract shapes) --- */
static bool same_inventory_entry(const qa_inventory_entry *left,
                                 const qa_inventory_entry *right) {
  if (!same_text(left->item, right->item) || !same_number(left->count, right->count) ||
      !same_number(left->capacity, right->capacity) ||
      left->has_count_policy != right->has_count_policy) return false;
  if (!left->has_count_policy) return true;
  return same_text(left->count_policy.kind, right->count_policy.kind) &&
         same_text(left->count_policy.arithmetic, right->count_policy.arithmetic);
}
static bool same_inventory_entries(const qa_inventory_entry *left, size_t left_count,
                                   const qa_inventory_entry *right, size_t right_count) {
  if (left_count != right_count) return false;
  for (size_t i = 0; i < left_count; i++)
    if (!same_inventory_entry(&left[i], &right[i])) return false;
  return true;
}
static bool same_vec3(qa_vec3 left, qa_vec3 right) {
  return same_number(left.x, right.x) && same_number(left.y, right.y) &&
         same_number(left.z, right.z);
}
static bool same_icon(const qa_weapon_hud_icon *left, const qa_weapon_hud_icon *right) {
  if (left->kind != right->kind) return false;
  if (left->kind == QA_WEAPON_HUD_ICON_SHADER)
    return same_text(left->content, right->content) &&
           same_text(left->name, right->name);
  if (!same_text(left->resource.content, right->resource.content) ||
      !same_text(left->resource.path, right->resource.path)) return false;
  return left->kind != QA_WEAPON_HUD_ICON_WAD_PICTURE ||
         same_text(left->lump, right->lump);
}
static bool same_held(const qa_held_weapon_declaration *left,
                      const qa_held_weapon_declaration *right) {
  if (left->is_model != right->is_model) return false;
  if (!left->is_model) return true;
  const qa_held_weapon_model *a = &left->model;
  const qa_held_weapon_model *b = &right->model;
  if (!same_text(a->path, b->path) ||
      !same_number(a->reference_frame, b->reference_frame) ||
      !same_vec3(a->grip.origin, b->grip.origin) ||
      !same_vec3(a->grip.axis.vectors[0], b->grip.axis.vectors[0]) ||
      !same_vec3(a->grip.axis.vectors[1], b->grip.axis.vectors[1]) ||
      !same_vec3(a->grip.axis.vectors[2], b->grip.axis.vectors[2]) ||
      !same_vec3(a->grip.scale, b->grip.scale) ||
      a->has_digest != b->has_digest ||
      (a->has_digest && !same_text(a->digest, b->digest)) ||
      !same_text(a->fallback, b->fallback) ||
      (a->part == NULL) != (b->part == NULL)) return false;
  if (a->part == NULL) return true;
  if (a->part->digests.count != b->part->digests.count ||
      a->part->vertices.count != b->part->vertices.count) return false;
  for (size_t i = 0; i < a->part->digests.count; i++)
    if (!same_text(a->part->digests.values[i], b->part->digests.values[i]))
      return false;
  for (size_t i = 0; i < a->part->vertices.count; i++)
    if (!same_number(a->part->vertices.values[i], b->part->vertices.values[i]))
      return false;
  return true;
}
static bool same_definition(const qa_source_item_definition *left,
                            const qa_source_item_definition *right) {
  if (!same_text(left->item, right->item) || !same_text(left->label, right->label) ||
      !same_text(left->source.provider, right->source.provider) ||
      !same_text(left->source.content, right->source.content) ||
      left->has_icon != right->has_icon ||
      (left->icon == NULL) != (right->icon == NULL) ||
      (left->icon != NULL && !same_icon(left->icon, right->icon)) ||
      (left->actions == NULL) != (right->actions == NULL) ||
      left->action_count != right->action_count ||
      left->is_weapon != right->is_weapon) return false;
  for (size_t i = 0; i < left->action_count; i++)
    if (left->actions[i] != right->actions[i]) return false;
  if (!left->is_weapon) return true;
  return same_text(left->weapon.ammo, right->weapon.ammo) &&
         (left->weapon.held == NULL) == (right->weapon.held == NULL) &&
         (left->weapon.held == NULL ||
          same_held(left->weapon.held, right->weapon.held));
}
static bool item_in(const char *const *items, size_t count, const char *item) {
  for (size_t i = 0; i < count; i++)
    if (same_text(items[i], item)) return true;
  return false;
}
static bool inventory_has(const qa_inventory_entry *entries, size_t count,
                          const char *item) {
  for (size_t i = 0; i < count; i++)
    if (same_text(entries[i].item, item)) return true;
  return false;
}

bool qa_read_source_items(const qa_save_image *save, qa_source_items_result *out) {
  static const char *const admissions[] = {"add", "replace-primary"};
  memset(out, 0, sizeof *out);
  const qa_provider_checkpoint *record = NULL;
  size_t matches = 0;
  for (size_t i = 0; i < save->provider_count; i++) {
    if (!same_text(save->providers[i].schema, k_schema)) continue;
    if (record == NULL) record = &save->providers[i];
    matches++;
  }
  if (matches > 1 ||
      (record != NULL && (!same_text(record->provider, "world:gameplay") ||
                          record->version != 1)))
    qa_save_format_error(k_schema, "unsupported or duplicate source item checkpoint");
  if (record == NULL) return true;
  qa_save_value *root =
      qa_decode_checkpoint_value(record->bytes.bytes, record->bytes.length);
  if (root == NULL) return false;
  out->root = root;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, k_schema)) return false;
  size_t count = qa_save_reader_list_count(&reader);
  out->records = (qa_source_items_record *)calloc(count != 0 ? count : 1,
                                                  sizeof *out->records);
  if (out->records == NULL) qa_save_reader_fail(&reader, "out of memory");
  out->count = count;
  for (size_t index = 0; index < count; index++) {
    qa_source_items_record *decoded = &out->records[index];
    qa_save_reader entry = item(&reader, index);
    qa_save_reader actor_field = field(&entry, "actor");
    decoded->actor = qa_read_saved_actor(&actor_field);
    qa_save_reader_destroy(&actor_field);
    qa_save_reader owner_field = field(&entry, "owner");
    decoded->owner = qa_namespaced(&owner_field);
    qa_save_reader_destroy(&owner_field);
    qa_save_reader primary = field(&entry, "primary");
    size_t primary_count = qa_save_reader_list_count(&primary);
    decoded->primary = (qa_inventory_entry *)calloc(
        primary_count != 0 ? primary_count : 1, sizeof *decoded->primary);
    if (decoded->primary == NULL) qa_save_reader_fail(&primary, "out of memory");
    decoded->primary_count = primary_count;
    for (size_t i = 0; i < primary_count; i++) {
      qa_save_reader row = item(&primary, i);
      decoded->primary[i] = qa_read_inventory_entry(&row);
      qa_save_reader_destroy(&row);
    }
    qa_save_reader_destroy(&primary);
    bool duplicate_actor = false;
    for (size_t i = 0; i < index; i++)
      if (same_saved_actor(out->records[i].actor, decoded->actor))
        duplicate_actor = true;
    bool duplicate_primary = false;
    for (size_t i = 0; i < primary_count && !duplicate_primary; i++)
      for (size_t j = i + 1; j < primary_count; j++)
        if (same_text(decoded->primary[j].item, decoded->primary[i].item))
          duplicate_primary = true;
    if (duplicate_actor || duplicate_primary)
      qa_save_reader_fail(&entry, "duplicate primary inventory or actor");
    /* Source-item ids admitted across every group of this record. */
    const char **items = NULL;
    size_t item_count = 0;
    size_t item_capacity = 0;
    qa_save_reader groups = field(&entry, "groups");
    size_t group_count = qa_save_reader_list_count(&groups);
    decoded->groups = (qa_source_items_group *)calloc(
        group_count != 0 ? group_count : 1, sizeof *decoded->groups);
    if (decoded->groups == NULL) qa_save_reader_fail(&groups, "out of memory");
    decoded->group_count = group_count;
    for (size_t g = 0; g < group_count; g++) {
      qa_source_items_group *group = &decoded->groups[g];
      qa_save_reader group_reader = item(&groups, g);
      qa_save_reader group_owner = field(&group_reader, "owner");
      group->owner = qa_namespaced(&group_owner);
      qa_save_reader_destroy(&group_owner);
      qa_save_reader rows = field(&group_reader, "items");
      size_t row_count = qa_save_reader_list_count(&rows);
      group->items = (qa_source_item_admission *)calloc(
          row_count != 0 ? row_count : 1, sizeof *group->items);
      if (group->items == NULL) qa_save_reader_fail(&rows, "out of memory");
      group->item_count = row_count;
      for (size_t r = 0; r < row_count; r++) {
        qa_save_reader row = item(&rows, r);
        qa_save_reader definition = field(&row, "definition");
        read_definition(out, &definition, &group->items[r].definition);
        qa_save_reader_destroy(&definition);
        qa_save_reader admission = field(&row, "admission");
        group->items[r].admission =
            qa_save_reader_choice_string(&admission, admissions, 2);
        qa_save_reader_destroy(&admission);
        const qa_source_item_definition *def = &group->items[r].definition;
        bool in_primary =
            inventory_has(decoded->primary, primary_count, def->item);
        bool admission_ok = strcmp(group->items[r].admission, "add") == 0
            ? !in_primary : in_primary;
        if (!same_text(def->source.provider, group->owner) ||
            item_in(items, item_count, def->item) || !admission_ok)
          qa_save_reader_fail(&row,
              "source item admission differs from primary ownership");
        if (item_count == item_capacity) {
          size_t grown = item_capacity == 0 ? 8 : item_capacity * 2;
          const char **grown_items =
              (const char **)realloc(items, grown * sizeof *grown_items);
          if (grown_items == NULL) qa_save_reader_fail(&row, "out of memory");
          items = grown_items;
          item_capacity = grown;
        }
        items[item_count++] = def->item;
        qa_save_reader_destroy(&row);
      }
      qa_save_reader_destroy(&rows);
      if (row_count == 0)
        qa_save_reader_fail(&group_reader, "empty source item group");
      qa_save_reader_destroy(&group_reader);
    }
    qa_save_reader_destroy(&groups);
    const qa_inventory_checkpoint *effective = NULL;
    size_t effective_count = 0;
    for (size_t i = 0; i < save->inventory_count; i++) {
      if (!same_saved_actor(save->inventories[i].actor, decoded->actor)) continue;
      if (effective == NULL) effective = &save->inventories[i];
      effective_count++;
    }
    if (group_count == 0 || effective_count != 1)
      qa_save_reader_fail(&entry,
          "source items have no distinct effective inventory");
    const qa_inventory_entry *rows = effective->entries;
    size_t row_count = effective->entry_count;
    bool coverage_bad = false;
    for (size_t i = 0; i < row_count && !coverage_bad; i++)
      for (size_t j = i + 1; j < row_count; j++)
        if (same_text(rows[j].item, rows[i].item)) coverage_bad = true;
    for (size_t i = 0; i < item_count && !coverage_bad; i++)
      if (!inventory_has(rows, row_count, items[i])) coverage_bad = true;
    if (!coverage_bad) {
      /* isDeepStrictEqual(rows minus items, primary minus items). */
      size_t row_index = 0;
      size_t primary_index = 0;
      for (;;) {
        while (row_index < row_count &&
               item_in(items, item_count, rows[row_index].item)) row_index++;
        while (primary_index < primary_count &&
               item_in(items, item_count, decoded->primary[primary_index].item))
          primary_index++;
        if ((row_index < row_count) != (primary_index < primary_count)) {
          coverage_bad = true;
          break;
        }
        if (row_index >= row_count) break;
        if (!same_inventory_entry(&rows[row_index],
                                  &decoded->primary[primary_index])) {
          coverage_bad = true;
          break;
        }
        row_index++;
        primary_index++;
      }
    }
    free(items);
    if (coverage_bad)
      qa_save_reader_fail(&entry,
          "source item coverage differs from effective inventory");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&reader);
  return true;
}

static void definition_owned_free(qa_source_item_definition *definition) {
  free((void *)definition->icon);
  free((void *)definition->actions);
  qa_held_weapon_declaration *held =
      (qa_held_weapon_declaration *)definition->weapon.held;
  if (held != NULL) {
    qa_held_weapon_part *part = (qa_held_weapon_part *)held->model.part;
    if (part != NULL) {
      free((void *)part->digests.values);
      free((void *)part->vertices.values);
      free(part);
    }
    free(held);
  }
}
void qa_source_items_result_free(qa_source_items_result *result) {
  if (result == NULL) return;
  for (size_t i = 0; i < result->count; i++) {
    qa_source_items_record *record = &result->records[i];
    free(record->primary);
    for (size_t g = 0; g < record->group_count; g++) {
      for (size_t r = 0; r < record->groups[g].item_count; r++)
        definition_owned_free(&record->groups[g].items[r].definition);
      free(record->groups[g].items);
    }
    free(record->groups);
  }
  free(result->records);
  for (size_t i = 0; i < result->owned_string_count; i++)
    free(result->owned_strings[i]);
  free(result->owned_strings);
  qa_save_value_free(result->root);
  memset(result, 0, sizeof *result);
}

/* --- capture encoding --- */
static qa_save_value *build_inventory_entry(const qa_inventory_entry *entry) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!set_own(object, "item", qa_save_value_string(entry->item != NULL ? entry->item : "")) ||
      !set_own(object, "count", qa_save_value_number(entry->count)) ||
      !set_own(object, "capacity", qa_save_value_number(entry->capacity))) goto fail;
  if (entry->has_count_policy) {
    qa_save_value *policy = qa_save_value_object();
    if (policy == NULL ||
        !set_own(policy, "kind", qa_save_value_string(
            entry->count_policy.kind != NULL ? entry->count_policy.kind : "")))
      goto fail;
    if (same_text(entry->count_policy.kind, "source-counter") &&
        !set_own(policy, "arithmetic", qa_save_value_string(
            entry->count_policy.arithmetic != NULL
                ? entry->count_policy.arithmetic : ""))) {
      qa_save_value_free(policy);
      goto fail;
    }
    if (!set_own(object, "countPolicy", policy)) goto fail;
  }
  return object;
fail:
  qa_save_value_free(object);
  return NULL;
}
static qa_save_value *build_provider(qa_provider_reference source) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!set_own(object, "provider",
               qa_save_value_string(source.provider != NULL ? source.provider : "")) ||
      !set_own(object, "content",
               qa_save_value_string(source.content != NULL ? source.content : ""))) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}
static qa_save_value *build_vector(qa_vec3 vector) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!set_own(object, "x", qa_save_value_number(vector.x)) ||
      !set_own(object, "y", qa_save_value_number(vector.y)) ||
      !set_own(object, "z", qa_save_value_number(vector.z))) {
    qa_save_value_free(object);
    return NULL;
  }
  return object;
}
static qa_save_value *build_icon(const qa_weapon_hud_icon *icon) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  const char *kind = icon->kind == QA_WEAPON_HUD_ICON_SHADER ? "shader"
      : icon->kind == QA_WEAPON_HUD_ICON_IMAGE ? "image" : "wad-picture";
  if (!set_own(object, "kind", qa_save_value_string(kind))) goto fail;
  if (icon->kind == QA_WEAPON_HUD_ICON_SHADER) {
    if (!set_own(object, "content", qa_save_value_string(
            icon->content != NULL ? icon->content : "")) ||
        !set_own(object, "name", qa_save_value_string(
            icon->name != NULL ? icon->name : ""))) goto fail;
    return object;
  }
  qa_save_value *resource = qa_save_value_object();
  if (resource == NULL ||
      !set_own(resource, "content", qa_save_value_string(
          icon->resource.content != NULL ? icon->resource.content : "")) ||
      !set_own(resource, "path", qa_save_value_string(
          icon->resource.path != NULL ? icon->resource.path : "")) ||
      !set_own(object, "resource", resource)) goto fail;
  if (icon->kind == QA_WEAPON_HUD_ICON_WAD_PICTURE &&
      !set_own(object, "lump", qa_save_value_string(
          icon->lump != NULL ? icon->lump : ""))) goto fail;
  return object;
fail:
  qa_save_value_free(object);
  return NULL;
}
static qa_save_value *build_held(const qa_held_weapon_declaration *held) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!held->is_model) {
    if (!set_own(object, "kind", qa_save_value_string("none"))) goto fail;
    return object;
  }
  if (!set_own(object, "kind", qa_save_value_string("model"))) goto fail;
  qa_save_value *model = qa_save_value_object();
  if (model == NULL) goto fail;
  if (!set_own(model, "path", qa_save_value_string(
          held->model.path != NULL ? held->model.path : "")) ||
      !set_own(model, "referenceFrame",
               qa_save_value_number(held->model.reference_frame))) goto fail_model;
  qa_save_value *grip = qa_save_value_object();
  if (grip == NULL) goto fail_model;
  qa_save_value *axis = qa_save_value_array();
  bool grip_ok = axis != NULL;
  for (size_t i = 0; i < 3 && grip_ok; i++)
    grip_ok = push_own(axis, build_vector(held->model.grip.axis.vectors[i]));
  if (grip_ok)
    grip_ok = set_own(grip, "origin", build_vector(held->model.grip.origin));
  bool axis_transferred = false;
  if (grip_ok) {
    /* set_own frees the child on failure and owns it on success. */
    grip_ok = set_own(grip, "axis", axis);
    axis_transferred = true;
  }
  if (grip_ok)
    grip_ok = set_own(grip, "scale", build_vector(held->model.grip.scale));
  bool grip_transferred = false;
  if (grip_ok) {
    grip_ok = set_own(model, "grip", grip);
    grip_transferred = true;
  }
  if (!grip_ok) {
    if (!axis_transferred) qa_save_value_free(axis);
    if (!grip_transferred) qa_save_value_free(grip);
    goto fail_model;
  }
  if (held->model.has_digest &&
      !set_own(model, "digest", qa_save_value_string(
          held->model.digest != NULL ? held->model.digest : ""))) goto fail_model;
  if (held->model.fallback != NULL &&
      !set_own(model, "fallback", qa_save_value_string(held->model.fallback)))
    goto fail_model;
  if (held->model.part != NULL) {
    qa_save_value *part = qa_save_value_object();
    qa_save_value *digests = qa_save_value_array();
    qa_save_value *vertices = qa_save_value_array();
    bool part_ok = part != NULL && digests != NULL && vertices != NULL;
    for (size_t i = 0; i < held->model.part->digests.count && part_ok; i++)
      part_ok = push_own(digests, qa_save_value_string(
          held->model.part->digests.values[i] != NULL
              ? held->model.part->digests.values[i] : ""));
    for (size_t i = 0; i < held->model.part->vertices.count && part_ok; i++)
      part_ok = push_own(vertices,
          qa_save_value_number(held->model.part->vertices.values[i]));
    bool digests_transferred = false;
    bool vertices_transferred = false;
    bool part_transferred = false;
    if (part_ok) {
      part_ok = set_own(part, "digests", digests);
      digests_transferred = true;
    }
    if (part_ok) {
      part_ok = set_own(part, "vertices", vertices);
      vertices_transferred = true;
    }
    if (part_ok) {
      part_ok = set_own(model, "part", part);
      part_transferred = true;
    }
    if (!part_ok) {
      if (!part_transferred) qa_save_value_free(part);
      if (!digests_transferred) qa_save_value_free(digests);
      if (!vertices_transferred) qa_save_value_free(vertices);
      goto fail_model;
    }
  }
  if (!set_own(object, "model", model)) goto fail;
  return object;
fail_model:
  qa_save_value_free(model);
fail:
  qa_save_value_free(object);
  return NULL;
}
static qa_save_value *build_definition(const qa_source_item_definition *definition) {
  qa_save_value *object = qa_save_value_object();
  if (object == NULL) return NULL;
  if (!set_own(object, "item", qa_save_value_string(
          definition->item != NULL ? definition->item : "")) ||
      !set_own(object, "label", qa_save_value_string(
          definition->label != NULL ? definition->label : "")) ||
      !set_own(object, "source", build_provider(definition->source))) goto fail;
  if (definition->has_icon) {
    qa_save_value *icon = definition->icon != NULL
        ? build_icon(definition->icon) : qa_save_value_null();
    if (!set_own(object, "icon", icon)) goto fail;
  }
  if (definition->actions != NULL) {
    qa_save_value *actions = qa_save_value_array();
    bool ok = actions != NULL;
    for (size_t i = 0; i < definition->action_count && ok; i++)
      ok = push_own(actions, qa_save_value_string(
          qa_source_item_action_name(definition->actions[i])));
    if (!ok || !set_own(object, "actions", actions)) {
      qa_save_value_free(actions);
      goto fail;
    }
  }
  if (!set_own(object, "kind", qa_save_value_string(
          definition->is_weapon ? "weapon" : "counter"))) goto fail;
  if (definition->is_weapon) {
    if (definition->weapon.held != NULL &&
        !set_own(object, "held", build_held(definition->weapon.held))) goto fail;
    qa_save_value *ammo = definition->weapon.ammo != NULL
        ? qa_save_value_string(definition->weapon.ammo) : qa_save_value_null();
    if (!set_own(object, "ammo", ammo)) goto fail;
  }
  return object;
fail:
  qa_save_value_free(object);
  return NULL;
}
static qa_save_value *build_entries(const qa_inventory_entry *entries, size_t count) {
  qa_save_value *array = qa_save_value_array();
  if (array == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    if (!push_own(array, build_inventory_entry(&entries[i]))) {
      qa_save_value_free(array);
      return NULL;
    }
  }
  return array;
}
bool qa_capture_source_items(const qa_session_actor_registry *actors,
                             const qa_shared_inventory_table *inventory,
                             qa_provider_checkpoint *out) {
  qa_save_value *entries = qa_save_value_array();
  if (entries == NULL) return false;
  size_t observation_count = 0;
  const qa_actor_observation *observations =
      actors->observations(actors->ctx, &observation_count);
  for (size_t i = 0; i < observation_count; i++) {
    qa_owned_actor actor;
    if (!actors->resolve_owned(actors->ctx, observations[i].id, &actor)) continue;
    qa_source_item_snapshot source;
    if (!inventory->source_items(inventory->ctx, actor, &source)) continue;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *saved = qa_save_value_object();
    qa_saved_actor_id saved_id = qa_saved_actor_id_of(actor.id);
    bool ok = entry != NULL && saved != NULL &&
        set_own(saved, "slot", qa_save_value_number(saved_id.slot)) &&
        set_own(saved, "generation", qa_save_value_number(saved_id.generation));
    bool saved_transferred = false;
    if (ok) {
      ok = set_own(entry, "actor", saved);
      saved_transferred = true;
    }
    ok = ok && set_own(entry, "owner", qa_save_value_string(
            actor.owner != NULL ? actor.owner : "")) &&
        set_own(entry, "primary", build_entries(source.primary, source.primary_count));
    qa_save_value *groups = NULL;
    bool groups_transferred = false;
    if (ok) {
      groups = qa_save_value_array();
      ok = groups != NULL;
      for (size_t g = 0; g < source.group_count && ok; g++) {
        qa_save_value *group = qa_save_value_object();
        qa_save_value *rows = qa_save_value_array();
        bool group_ok = group != NULL && rows != NULL;
        for (size_t r = 0; r < source.groups[g].item_count && group_ok; r++) {
          qa_save_value *row = qa_save_value_object();
          if (row == NULL ||
              !set_own(row, "definition",
                       build_definition(&source.groups[g].items[r].definition)) ||
              !set_own(row, "admission", qa_save_value_string(
                  source.groups[g].items[r].admission != NULL
                      ? source.groups[g].items[r].admission : ""))) {
            qa_save_value_free(row);
            group_ok = false;
            break;
          }
          if (!push_own(rows, row)) group_ok = false; /* push_own freed row */
        }
        bool rows_transferred = false;
        bool group_transferred = false;
        if (group_ok)
          group_ok = set_own(group, "owner", qa_save_value_string(
              source.groups[g].owner != NULL ? source.groups[g].owner : ""));
        if (group_ok) {
          group_ok = set_own(group, "items", rows);
          rows_transferred = true;
        }
        if (group_ok) {
          group_ok = push_own(groups, group);
          group_transferred = true;
        }
        if (!group_ok) {
          if (!group_transferred) qa_save_value_free(group);
          if (!rows_transferred) qa_save_value_free(rows);
          ok = false;
        }
      }
      if (ok) {
        ok = set_own(entry, "groups", groups);
        groups_transferred = true;
      }
    }
    if (!ok || !push_own(entries, entry)) {
      if (!saved_transferred) qa_save_value_free(saved);
      if (!groups_transferred) qa_save_value_free(groups);
      if (ok) {
        /* push_own already freed entry. */
      } else {
        qa_save_value_free(entry);
      }
      qa_save_value_free(entries);
      return false;
    }
  }
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(entries, &length);
  qa_save_value_free(entries);
  if (bytes == NULL) return false;
  out->provider = "world:gameplay";
  out->schema = k_schema;
  out->version = 1;
  out->bytes.bytes = bytes;
  out->bytes.length = length;
  return true;
}

/* --- prepareSourceItemRestore --- */
static const qa_source_items_record *record_for(
    const qa_source_items_result *records, qa_saved_actor_id actor) {
  for (size_t i = 0; i < records->count; i++)
    if (same_saved_actor(records->records[i].actor, actor))
      return &records->records[i];
  return NULL;
}
bool qa_prepare_source_item_restore(const qa_save_image *save,
                                    qa_source_item_restore *out) {
  memset(out, 0, sizeof *out);
  out->save = save;
  out->primary = *save;
  if (!qa_read_source_items(save, &out->records)) return false;
  if (out->records.count == 0) return true;
  out->primary_inventories = (qa_inventory_checkpoint *)calloc(
      save->inventory_count != 0 ? save->inventory_count : 1,
      sizeof *out->primary_inventories);
  if (out->primary_inventories == NULL) return false;
  for (size_t i = 0; i < save->inventory_count; i++) {
    out->primary_inventories[i] = save->inventories[i];
    const qa_source_items_record *record =
        record_for(&out->records, save->inventories[i].actor);
    if (record != NULL) {
      out->primary_inventories[i].entries = record->primary;
      out->primary_inventories[i].entry_count = record->primary_count;
    }
  }
  out->primary.inventories = out->primary_inventories;
  return true;
}
void qa_source_item_restore_free(qa_source_item_restore *restore) {
  if (restore == NULL) return;
  qa_source_items_result_free(&restore->records);
  free(restore->primary_inventories);
  free(restore->effective);
  memset(restore, 0, sizeof *restore);
}
const qa_inventory_checkpoint *qa_source_item_restore_effective(
    qa_source_item_restore *restore, const qa_inventory_checkpoint *inventories,
    size_t count, size_t *out_count) {
  *out_count = count;
  if (restore->records.count == 0) return inventories;
  qa_inventory_checkpoint *mapped = (qa_inventory_checkpoint *)calloc(
      count != 0 ? count : 1, sizeof *mapped);
  if (mapped == NULL) return NULL;
  for (size_t i = 0; i < count; i++) {
    mapped[i] = inventories[i];
    if (record_for(&restore->records, inventories[i].actor) == NULL) continue;
    const qa_inventory_checkpoint *original = NULL;
    for (size_t j = 0; j < restore->save->inventory_count; j++) {
      if (same_saved_actor(restore->save->inventories[j].actor,
                           inventories[i].actor)) {
        original = &restore->save->inventories[j];
        break;
      }
    }
    if (original == NULL) {
      free(mapped);
      qa_save_format_error(k_schema, "missing saved effective inventory");
    }
    mapped[i] = *original;
  }
  free(restore->effective);
  restore->effective = mapped;
  restore->effective_count = count;
  return mapped;
}
static bool same_snapshot(const qa_source_item_snapshot *source,
                          const qa_source_items_record *saved) {
  if (!same_inventory_entries(source->primary, source->primary_count,
                              saved->primary, saved->primary_count)) return false;
  if (source->group_count != saved->group_count) return false;
  for (size_t g = 0; g < source->group_count; g++) {
    if (!same_text(source->groups[g].owner, saved->groups[g].owner) ||
        source->groups[g].item_count != saved->groups[g].item_count) return false;
    for (size_t r = 0; r < source->groups[g].item_count; r++) {
      if (!same_definition(&source->groups[g].items[r].definition,
                           &saved->groups[g].items[r].definition) ||
          !same_text(source->groups[g].items[r].admission,
                     saved->groups[g].items[r].admission)) return false;
    }
  }
  return true;
}
void qa_source_item_restore_finish(const qa_source_item_restore *restore,
                                   const qa_session_actor_registry *actors,
                                   const qa_shared_inventory_table *inventory) {
  size_t record_count = restore->records.count;
  bool *restored = (bool *)calloc(record_count != 0 ? record_count : 1,
                                  sizeof *restored);
  if (restored == NULL)
    qa_save_format_error(k_schema, "out of memory");
  size_t restored_count = 0;
  size_t observation_count = 0;
  const qa_actor_observation *observations =
      actors->observations(actors->ctx, &observation_count);
  for (size_t i = 0; i < observation_count; i++) {
    qa_owned_actor actor;
    if (!actors->resolve_owned(actors->ctx, observations[i].id, &actor)) continue;
    qa_source_item_snapshot source;
    bool has_source = inventory->source_items(inventory->ctx, actor, &source);
    const qa_source_items_record *saved = NULL;
    size_t saved_index = 0;
    for (size_t r = 0; r < record_count; r++) {
      qa_owned_actor resolved;
      if (actors->resolve_saved(actors->ctx, restore->records.records[r].actor,
                                &resolved) && same_owned(resolved, actor)) {
        saved = &restore->records.records[r];
        saved_index = r;
        break;
      }
    }
    bool mismatch = !has_source
        ? saved != NULL
        : saved == NULL || !same_text(saved->owner, actor.owner) ||
              !same_snapshot(&source, saved);
    if (mismatch)
      qa_save_format_error(k_schema,
          "restored source item ownership or hidden inventory differs");
    if (saved != NULL) {
      const qa_inventory_checkpoint *effective = NULL;
      for (size_t j = 0; j < restore->save->inventory_count; j++) {
        if (same_saved_actor(restore->save->inventories[j].actor, saved->actor)) {
          effective = &restore->save->inventories[j];
          break;
        }
      }
      size_t entry_count = 0;
      const qa_inventory_entry *entries =
          effective != NULL
              ? inventory->entries(inventory->ctx, actor.id, &entry_count) : NULL;
      if (effective == NULL ||
          !same_inventory_entries(entries, entry_count, effective->entries,
                                  effective->entry_count))
        qa_save_format_error(k_schema,
            "restored source inventory differs from its committed state");
      if (!restored[saved_index]) {
        restored[saved_index] = true;
        restored_count++;
      }
    }
  }
  free(restored);
  if (restored_count != record_count)
    qa_save_format_error(k_schema, "saved source item actor was not restored");
}
