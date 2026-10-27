/* Port of src/persistence/save-image.ts (see save_image.h for the
 * blocked image codec; the source actor checkpoint helpers at the
 * end are complete). GPL-2.0-or-later. */
#include "qa/persistence/save_image.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/shared.h"

/* Field reader or abort (OOM has no TS counterpart). */
static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_number(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_number(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer_min(const qa_save_reader *reader, const char *name,
                                double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_namespaced(const qa_save_reader *reader,
                                    const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}
/* TS nullable(namespaced): NULL when the field value is null. */
static const char *field_nullable_namespaced(const qa_save_reader *reader,
                                             const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_is_null(&f) ? NULL : qa_namespaced(&f);
  qa_save_reader_destroy(&f);
  return value;
}

qa_saved_actor_id qa_saved_actor_id_of(qa_actor_id actor) {
  qa_saved_actor_id saved = {(double)actor.slot, (double)actor.generation};
  return saved;
}

qa_saved_actor_id qa_read_saved_actor(const qa_save_reader *reader) {
  qa_saved_actor_id saved;
  saved.slot = field_integer_min(reader, "slot", 0);
  saved.generation = field_integer_min(reader, "generation", 0);
  return saved;
}

qa_saved_body_state qa_read_saved_body(const qa_save_reader *reader) {
  qa_saved_body_state body;
  memset(&body, 0, sizeof body);
  qa_save_reader origin = field(reader, "origin");
  body.origin = qa_read_vector(&origin);
  qa_save_reader_destroy(&origin);
  qa_save_reader angles = field(reader, "angles");
  body.angles = qa_read_vector(&angles);
  qa_save_reader_destroy(&angles);
  qa_save_reader velocity = field(reader, "velocity");
  body.velocity = qa_read_vector(&velocity);
  qa_save_reader_destroy(&velocity);
  qa_save_reader bounds = field(reader, "bounds");
  body.bounds = qa_read_bounds(&bounds);
  qa_save_reader_destroy(&bounds);
  qa_save_reader ground = field(reader, "ground");
  if (!qa_save_reader_is_null(&ground)) {
    qa_saved_actor_id *saved = malloc(sizeof *saved);
    if (saved == NULL) qa_save_reader_fail(reader, "out of memory");
    *saved = qa_read_saved_actor(&ground);
    body.ground = saved;
  }
  qa_save_reader_destroy(&ground);
  return body;
}

qa_saved_body_attachment qa_read_saved_body_attachment(
    const qa_save_reader *reader) {
  static const char *const kinds[] = {"translation", "center", "bounds-min"};
  qa_saved_body_attachment attachment;
  memset(&attachment, 0, sizeof attachment);
  qa_save_reader follow = field(reader, "follow");
  const char *kind = field_choice(&follow, "kind", kinds, 3);
  qa_save_reader anchor = field(reader, "anchor");
  attachment.anchor = qa_read_saved_actor(&anchor);
  qa_save_reader_destroy(&anchor);
  if (strcmp(kind, "center") == 0) {
    attachment.follow_kind = QA_BODY_FOLLOW_CENTER;
  } else {
    attachment.follow_kind = strcmp(kind, "translation") == 0
                                 ? QA_BODY_FOLLOW_TRANSLATION
                                 : QA_BODY_FOLLOW_BOUNDS_MIN;
    qa_save_reader offset = field(&follow, "offset");
    attachment.follow_offset = qa_read_vector(&offset);
    qa_save_reader_destroy(&offset);
  }
  qa_save_reader_destroy(&follow);
  return attachment;
}

qa_regular_armor_state qa_read_regular_armor(const qa_save_reader *reader) {
  static const char *const kinds[] = {"none", "q1", "q2", "q3", "source"};
  qa_regular_armor_state armor;
  memset(&armor, 0, sizeof armor);
  const char *kind = field_choice(reader, "kind", kinds, 5);
  if (strcmp(kind, "none") == 0) {
    armor.kind = QA_REGULAR_ARMOR_NONE;
  } else if (strcmp(kind, "source") == 0) {
    armor.kind = QA_REGULAR_ARMOR_SOURCE;
    armor.points = field_number(reader, "points");
    armor.item = field_nullable_namespaced(reader, "item");
  } else if (strcmp(kind, "q1") == 0) {
    armor.kind = QA_REGULAR_ARMOR_Q1;
    armor.points = field_number(reader, "points");
    armor.absorption = field_number(reader, "absorption");
    armor.item = field_namespaced(reader, "item");
  } else if (strcmp(kind, "q3") == 0) {
    armor.kind = QA_REGULAR_ARMOR_Q3;
    armor.points = field_number(reader, "points");
    armor.protection = field_number(reader, "protection");
  } else {
    armor.kind = QA_REGULAR_ARMOR_Q2;
    armor.points = field_number(reader, "points");
    armor.normal_protection = field_number(reader, "normalProtection");
    armor.energy_protection = field_number(reader, "energyProtection");
    armor.item = field_namespaced(reader, "item");
  }
  return armor;
}

static qa_powered_protection_state read_powered_protection(
    const qa_save_reader *reader) {
  static const char *const kinds[] = {"none", "screen", "shield"};
  qa_powered_protection_state powered;
  memset(&powered, 0, sizeof powered);
  powered.kind = field_choice(reader, "kind", kinds, 3);
  if (strcmp(powered.kind, "none") != 0)
    powered.cells = field_number(reader, "cells");
  return powered;
}

qa_armor_state qa_read_armor(const qa_save_reader *reader) {
  qa_armor_state armor;
  memset(&armor, 0, sizeof armor);
  qa_save_reader regular = field(reader, "regular");
  bool has_regular = !qa_save_reader_is_undefined(&regular);
  qa_save_reader_destroy(&regular);
  if (has_regular) {
    qa_save_reader regular_reader = field(reader, "regular");
    armor.regular = qa_read_regular_armor(&regular_reader);
    qa_save_reader_destroy(&regular_reader);
    qa_save_reader powered = field(reader, "powered");
    armor.powered = read_powered_protection(&powered);
    qa_save_reader_destroy(&powered);
    return armor;
  }
  armor.regular = qa_read_regular_armor(reader);
  if (armor.regular.kind == QA_REGULAR_ARMOR_Q2) {
    qa_save_reader power = field(reader, "powerArmor");
    armor.powered = read_powered_protection(&power);
    qa_save_reader_destroy(&power);
  } else {
    armor.powered.kind = "none";
  }
  return armor;
}

qa_inventory_entry qa_read_inventory_entry(const qa_save_reader *reader) {
  static const char *const policy_kinds[] = {"stack", "source-counter"};
  static const char *const arithmetics[] = {"binary32", "binary64", "int32"};
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = field_namespaced(reader, "item");
  entry.count = field_number(reader, "count");
  entry.capacity = field_number(reader, "capacity");
  qa_save_reader policy = field(reader, "countPolicy");
  if (!qa_save_reader_is_undefined(&policy)) {
    entry.has_count_policy = true;
    entry.count_policy.kind = field_choice(&policy, "kind", policy_kinds, 2);
    if (strcmp(entry.count_policy.kind, "source-counter") == 0)
      entry.count_policy.arithmetic =
          field_choice(&policy, "arithmetic", arithmetics, 3);
  }
  qa_save_reader_destroy(&policy);
  return entry;
}

/* --- sourceActorsCheckpoint / readSourceActorsCheckpoint --- */
static bool set_own(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static bool push_own(qa_save_value *array, qa_save_value *child) {
  if (child == NULL) return false;
  if (!qa_save_value_push(array, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static qa_save_value *actor_value(qa_saved_actor_id actor) {
  qa_save_value *obj = qa_save_value_object();
  if (obj == NULL) return NULL;
  if (!set_own(obj, "slot", qa_save_value_number(actor.slot)) ||
      !set_own(obj, "generation", qa_save_value_number(actor.generation))) {
    qa_save_value_free(obj);
    return NULL;
  }
  return obj;
}

bool qa_source_actors_checkpoint(const qa_source_actor_checkpoint *sources,
                                 size_t count, qa_provider_checkpoint *out) {
  memset(out, 0, sizeof *out);
  qa_save_value *entries = qa_save_value_array();
  if (entries == NULL) return false;
  for (size_t i = 0; i < count; i++) {
    qa_save_value *entry = qa_save_value_object();
    bool ok = entry != NULL &&
              set_own(entry, "provider",
                      qa_save_value_string(sources[i].provider)) &&
              set_own(entry, "sourceSlot",
                      qa_save_value_number(sources[i].source_slot)) &&
              set_own(entry, "actor", actor_value(sources[i].actor));
    if (!ok) {
      qa_save_value_free(entry);
      qa_save_value_free(entries);
      return false;
    }
    if (!push_own(entries, entry)) { /* push_own frees entry on failure */
      qa_save_value_free(entries);
      return false;
    }
  }
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(entries, &length);
  qa_save_value_free(entries);
  if (bytes == NULL) return false;
  out->provider = "world:actors";
  out->schema = "world:source-slots";
  out->version = 1;
  out->bytes.bytes = bytes;
  out->bytes.length = length;
  return true;
}

void qa_source_actors_result_free(qa_source_actors_result *result) {
  if (result == NULL) return;
  free(result->entries);
  qa_save_value_free(result->root);
  memset(result, 0, sizeof *result);
}

bool qa_read_source_actors_checkpoint(const qa_provider_checkpoint *checkpoint,
                                      qa_source_actors_result *out) {
  memset(out, 0, sizeof *out);
  if (checkpoint->provider == NULL ||
      strcmp(checkpoint->provider, "world:actors") != 0 ||
      checkpoint->schema == NULL ||
      strcmp(checkpoint->schema, "world:source-slots") != 0 ||
      checkpoint->version != 1)
    qa_save_format_error("source-slots", "unsupported source actor checkpoint");
  qa_save_value *root =
      qa_decode_checkpoint_value(checkpoint->bytes.bytes, checkpoint->bytes.length);
  if (root == NULL) return false;
  out->root = root;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, root, "source-slots")) return false;
  size_t count = qa_save_reader_list_count(&reader);
  if (count > 0) {
    out->entries = calloc(count, sizeof *out->entries);
    if (out->entries == NULL) {
      qa_save_reader_destroy(&reader);
      return false;
    }
  }
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&reader, i, &entry))
      qa_save_reader_fail(&reader, "out of memory");
    qa_source_actor_checkpoint *source = &out->entries[out->count];
    source->provider = field_namespaced(&entry, "provider");
    source->source_slot = field_integer_min(&entry, "sourceSlot", 0);
    qa_save_reader actor = field(&entry, "actor");
    source->actor = qa_read_saved_actor(&actor);
    qa_save_reader_destroy(&actor);
    qa_save_reader_destroy(&entry);
    out->count++;
  }
  qa_save_reader_destroy(&reader);
  return true;
}
