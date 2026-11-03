/* Port of src/persistence/save-image.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/persistence/save_image.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/content/mods/declaration_writer.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/mods.h"
#include "qa/persistence/recipe.h"
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

/* --- parseSaveImage / decodeSaveImage / file read+write --- */
static qa_save_reader item_at(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static void *xalloc(const qa_save_reader *reader, size_t count, size_t size) {
  if (count == 0) return NULL;
  void *memory = calloc(count, size);
  if (memory == NULL) qa_save_reader_fail(reader, "out of memory");
  return memory;
}
static bool field_boolean(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  bool value = qa_save_reader_boolean(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_source_time field_time(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  qa_source_time value = qa_read_time(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_saved_actor_id field_saved_actor(const qa_save_reader *reader,
                                           const char *name) {
  qa_save_reader f = field(reader, name);
  qa_saved_actor_id value = qa_read_saved_actor(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static qa_actor_slot_checkpoint read_actor_slot(const qa_save_reader *reader) {
  static const char *const kinds[] = {"active", "free"};
  qa_actor_slot_checkpoint slot;
  memset(&slot, 0, sizeof slot);
  slot.id = qa_read_saved_actor(reader);
  qa_save_reader lifetime = field(reader, "lifetime");
  const char *kind = field_choice(&lifetime, "kind", kinds, 2);
  if (strcmp(kind, "free") == 0) {
    qa_save_reader freed = field(&lifetime, "freedAt");
    if (!qa_save_reader_is_null(&freed)) {
      qa_source_time *time = xalloc(reader, 1, sizeof *time);
      *time = qa_read_time(&freed);
      slot.freed_at = time;
    }
    qa_save_reader_destroy(&freed);
  } else {
    slot.lifetime_is_active = true;
    slot.owner = field_namespaced(&lifetime, "owner");
    slot.definition = field_namespaced(&lifetime, "definition");
  }
  qa_save_reader_destroy(&lifetime);
  return slot;
}
static qa_combat_state read_combat(const qa_save_reader *reader) {
  qa_combat_state state;
  memset(&state, 0, sizeof state);
  state.health = field_number(reader, "health");
  qa_save_reader armor = field(reader, "armor");
  state.armor = qa_read_armor(&armor);
  qa_save_reader_destroy(&armor);
  state.mass = field_number(reader, "mass");
  state.can_take_damage = field_boolean(reader, "canTakeDamage");
  state.invulnerable = field_boolean(reader, "invulnerable");
  qa_save_reader team = field(reader, "team");
  state.team = qa_save_reader_is_null(&team) ? NULL : qa_save_reader_string(&team);
  qa_save_reader_destroy(&team);
  qa_save_reader knockback = field(reader, "noKnockback");
  if (!qa_save_reader_is_undefined(&knockback)) {
    state.has_no_knockback = true;
    state.no_knockback = qa_save_reader_boolean(&knockback);
  }
  qa_save_reader_destroy(&knockback);
  return state;
}
static qa_body_checkpoint read_body_checkpoint(const qa_save_reader *reader) {
  qa_body_checkpoint body;
  memset(&body, 0, sizeof body);
  body.actor = field_saved_actor(reader, "actor");
  qa_save_reader state = field(reader, "body");
  body.body = qa_read_saved_body(&state);
  qa_save_reader_destroy(&state);
  qa_save_reader attachment = field(reader, "attachment");
  if (!qa_save_reader_is_null(&attachment)) {
    qa_saved_body_attachment *saved = xalloc(reader, 1, sizeof *saved);
    *saved = qa_read_saved_body_attachment(&attachment);
    body.attachment = saved;
  }
  qa_save_reader_destroy(&attachment);
  body.link_count = field_integer_min(reader, "linkCount", 0);
  qa_save_reader linked = field(reader, "linked");
  if (!qa_save_reader_is_null(&linked)) {
    qa_body_checkpoint_linked *link = xalloc(reader, 1, sizeof *link);
    qa_save_reader inner = field(&linked, "state");
    link->state = qa_read_saved_body(&inner);
    qa_save_reader_destroy(&inner);
    inner = field(&linked, "absoluteBounds");
    link->absolute_bounds = qa_read_bounds(&inner);
    qa_save_reader_destroy(&inner);
    body.linked = link;
  }
  qa_save_reader_destroy(&linked);
  return body;
}
static void release_partial_image(void *arg) {
  qa_save_image_result_free((qa_save_image_result *)arg);
}
bool qa_parse_save_image(const qa_save_value *value, qa_save_image_result *out) {
  static const double versions[] = {2, 3, 4};
  static const char *const boundaries[] = {"before-physics", "during-physics",
                                           "after-physics"};
  memset(out, 0, sizeof *out);
  qa_save_image *image = &out->image;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) return false;
  /* A malformed save raises part way; everything read so far is attached to
     *out as it is allocated, so this node frees it (the role GC plays in TS). */
  qa_cleanup partial;
  qa_cleanup_push(&partial, release_partial_image, out);
  qa_save_reader version_reader = field(&reader, "schemaVersion");
  double version = qa_save_reader_choice_number(&version_reader, versions, 3);
  qa_save_reader_destroy(&version_reader);
  qa_save_reader legacy = field(&reader, "legacyArmorLayout");
  bool legacy_true = false;
  if (!qa_save_reader_is_undefined(&legacy))
    legacy_true = qa_save_reader_literal_boolean(&legacy, true);
  qa_save_reader_destroy(&legacy);
  image->schema_version = 4;
  image->key_order = QA_SAVE_IMAGE_ORDER_PARSED;
  image->legacy_armor_layout = version == 2 || legacy_true;
  {
    qa_save_reader recipe = field(&reader, "recipe");
    image->recipe = qa_read_recipe(&recipe);
    qa_save_reader_destroy(&recipe);
  }
  {
    qa_save_reader frame_reader = field(&reader, "frame");
    image->frame = qa_read_frame(&frame_reader);
    qa_save_reader_destroy(&frame_reader);
  }
  image->next_event_sequence = field_integer_min(&reader, "nextEventSequence", 0);
  {
    qa_save_reader clocks = field(&reader, "clocks");
    size_t count = qa_save_reader_list_count(&clocks);
    qa_save_clock *values = xalloc(&clocks, count, sizeof *values);
    image->clocks = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->clock_count = i + 1;
      qa_save_reader entry = item_at(&clocks, i);
      values[i].provider = field_namespaced(&entry, "provider");
      values[i].time = field_time(&entry, "time");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&clocks);
    image->clocks = values;
    image->clock_count = count;
  }
  {
    qa_save_reader random = field(&reader, "random");
    size_t count = qa_save_reader_list_count(&random);
    qa_save_random *values = xalloc(&random, count, sizeof *values);
    image->random = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->random_count = i + 1;
      qa_save_reader entry = item_at(&random, i);
      values[i].provider = field_namespaced(&entry, "provider");
      qa_save_reader state = field(&entry, "state");
      values[i].state = qa_read_random(&state);
      qa_save_reader_destroy(&state);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&random);
    image->random = values;
    image->random_count = count;
  }
  {
    qa_save_reader actors = field(&reader, "actors");
    size_t count = qa_save_reader_list_count(&actors);
    qa_actor_slot_checkpoint *values = xalloc(&actors, count, sizeof *values);
    image->actors = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->actor_count = i + 1;
      qa_save_reader entry = item_at(&actors, i);
      values[i] = read_actor_slot(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&actors);
    image->actors = values;
    image->actor_count = count;
  }
  {
    qa_save_reader bodies = field(&reader, "bodies");
    size_t count = qa_save_reader_list_count(&bodies);
    qa_body_checkpoint *values = xalloc(&bodies, count, sizeof *values);
    image->bodies = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->body_count = i + 1;
      qa_save_reader entry = item_at(&bodies, i);
      values[i] = read_body_checkpoint(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&bodies);
    image->bodies = values;
    image->body_count = count;
  }
  {
    qa_save_reader combat = field(&reader, "combat");
    size_t count = qa_save_reader_list_count(&combat);
    qa_combat_checkpoint *values = xalloc(&combat, count, sizeof *values);
    image->combat = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->combat_count = i + 1;
      qa_save_reader entry = item_at(&combat, i);
      values[i].actor = field_saved_actor(&entry, "actor");
      qa_save_reader state = field(&entry, "state");
      values[i].state = read_combat(&state);
      qa_save_reader_destroy(&state);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&combat);
    image->combat = values;
    image->combat_count = count;
  }
  {
    qa_save_reader inventories = field(&reader, "inventories");
    size_t count = qa_save_reader_list_count(&inventories);
    qa_inventory_checkpoint *values = xalloc(&inventories, count, sizeof *values);
    image->inventories = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->inventory_count = i + 1;
      qa_save_reader entry = item_at(&inventories, i);
      values[i].actor = field_saved_actor(&entry, "actor");
      qa_save_reader entries = field(&entry, "entries");
      size_t entry_count = qa_save_reader_list_count(&entries);
      qa_inventory_entry *items = xalloc(&entries, entry_count, sizeof *items);
      values[i].entries = items;
      for (size_t k = 0; k < entry_count; k++) {
        values[i].entry_count = k + 1;
        qa_save_reader item_reader = item_at(&entries, k);
        items[k] = qa_read_inventory_entry(&item_reader);
        qa_save_reader_destroy(&item_reader);
      }
      qa_save_reader_destroy(&entries);
      values[i].entries = items;
      values[i].entry_count = entry_count;
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&inventories);
    image->inventories = values;
    image->inventory_count = count;
  }
  {
    qa_save_reader configurations = field(&reader, "configurations");
    size_t count = qa_save_reader_list_count(&configurations);
    qa_actor_configuration_checkpoint *values =
        xalloc(&configurations, count, sizeof *values);
    image->configurations = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->configuration_count = i + 1;
      qa_save_reader entry = item_at(&configurations, i);
      values[i].actor = field_saved_actor(&entry, "actor");
      qa_save_reader movement = field(&entry, "movement");
      values[i].movement = qa_read_provider(&movement);
      qa_save_reader_destroy(&movement);
      qa_save_reader character = field(&entry, "character");
      values[i].character = qa_read_character(&character);
      qa_save_reader_destroy(&character);
      qa_save_reader weapons = field(&entry, "weapons");
      size_t weapon_count = qa_save_reader_list_count(&weapons);
      qa_provider_reference *refs = xalloc(&weapons, weapon_count, sizeof *refs);
      values[i].weapons.values = refs;
      for (size_t k = 0; k < weapon_count; k++) {
        values[i].weapons.count = k + 1;
        qa_save_reader weapon = item_at(&weapons, k);
        refs[k] = qa_read_provider(&weapon);
        qa_save_reader_destroy(&weapon);
      }
      qa_save_reader_destroy(&weapons);
      values[i].weapons.values = refs;
      values[i].weapons.count = weapon_count;
      qa_save_reader inventory = field(&entry, "inventory");
      values[i].inventory = qa_read_provider(&inventory);
      qa_save_reader_destroy(&inventory);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&configurations);
    image->configurations = values;
    image->configuration_count = count;
  }
  {
    qa_save_reader thinks = field(&reader, "thinks");
    size_t count = qa_save_reader_list_count(&thinks);
    qa_think_checkpoint *values = xalloc(&thinks, count, sizeof *values);
    image->thinks = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->think_count = i + 1;
      qa_save_reader entry = item_at(&thinks, i);
      values[i].actor = field_saved_actor(&entry, "actor");
      values[i].callback = field_namespaced(&entry, "callback");
      values[i].due = field_time(&entry, "due");
      const char *boundary = field_choice(&entry, "boundary", boundaries, 3);
      values[i].boundary = strcmp(boundary, "before-physics") == 0
                               ? QA_THINK_BEFORE_PHYSICS
                           : strcmp(boundary, "during-physics") == 0
                               ? QA_THINK_DURING_PHYSICS
                               : QA_THINK_AFTER_PHYSICS;
      values[i].provider = field_namespaced(&entry, "provider");
      values[i].sequence = field_integer_min(&entry, "sequence", 0);
      qa_save_reader execution = field(&entry, "executionProvider");
      if (!qa_save_reader_is_undefined(&execution)) {
        values[i].has_execution_provider = true;
        values[i].execution_provider = qa_namespaced(&execution);
      }
      qa_save_reader_destroy(&execution);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&thinks);
    image->thinks = values;
    image->think_count = count;
  }
  {
    qa_save_reader providers = field(&reader, "providers");
    size_t count = qa_save_reader_list_count(&providers);
    qa_provider_checkpoint *values = xalloc(&providers, count, sizeof *values);
    image->providers = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->provider_count = i + 1;
      qa_save_reader entry = item_at(&providers, i);
      values[i].provider = field_namespaced(&entry, "provider");
      values[i].schema = field_namespaced(&entry, "schema");
      values[i].version = field_integer_min(&entry, "version", 0);
      qa_save_reader bytes = field(&entry, "bytes");
      values[i].bytes.bytes =
          qa_save_reader_bytes(&bytes, &values[i].bytes.length);
      qa_save_reader_destroy(&bytes);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&providers);
    image->providers = values;
    image->provider_count = count;
  }
  {
    qa_save_reader guests = field(&reader, "guests");
    size_t count = qa_save_reader_list_count(&guests);
    qa_guest_checkpoint *values = xalloc(&guests, count, sizeof *values);
    image->guests = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->guest_count = i + 1;
      qa_save_reader entry = item_at(&guests, i);
      values[i] = qa_read_guest(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&guests);
    image->guests = values;
    image->guest_count = count;
  }
  {
    qa_save_reader mods = field(&reader, "mods");
    if (!qa_save_reader_is_undefined(&mods)) {
      qa_mod_session_checkpoint *session = xalloc(&mods, 1, sizeof *session);
      image->mods = session;
      *session = qa_read_mod_session(&mods);
    }
    qa_save_reader_destroy(&mods);
  }
  /* Versions 2 and 3 predate the record; they migrate to an empty one. */
  if (version == 4) {
    static const char *const axes[] = {"enemies", "movement", "character"};
    static const char *const outcomes[] = {"honored", "partial", "guest-native"};
    qa_save_reader consumption = field(&reader, "consumption");
    size_t count = qa_save_reader_list_count(&consumption);
    qa_selection_consumption_entry *values =
        xalloc(&consumption, count, sizeof *values);
    image->consumption = values; /* attached early: a raise frees what was read */
    for (size_t i = 0; i < count; i++) {
      image->consumption_count = i + 1;
      qa_save_reader entry = item_at(&consumption, i);
      values[i].axis = field_choice(&entry, "axis", axes, 3);
      qa_save_reader text = field(&entry, "requested");
      values[i].requested = qa_save_reader_string(&text);
      qa_save_reader_destroy(&text);
      values[i].outcome = field_choice(&entry, "outcome", outcomes, 3);
      text = field(&entry, "detail");
      values[i].detail = qa_save_reader_string(&text);
      qa_save_reader_destroy(&text);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&consumption);
    image->consumption = values;
    image->consumption_count = count;
  }
  if (image->recipe.mod_count != 0 && image->mods == NULL) {
    qa_save_reader mods = field(&reader, "mods");
    qa_save_reader_fail(&mods,
                        "selected gameplay mods require their saved checkpoint");
  }
  qa_cleanup_pop(&partial);
  qa_save_reader_destroy(&reader);
  return true;
}
void qa_save_image_result_free(qa_save_image_result *result) {
  if (result == NULL) return;
  qa_save_image *image = &result->image;
  free((void *)image->clocks);
  /* qa_read_random allocates the word lists. */
  for (size_t i = 0; i < image->random_count; i++)
    free((void *)image->random[i].state.words.values);
  free((void *)image->random);
  for (size_t i = 0; i < image->actor_count; i++)
    free((void *)image->actors[i].freed_at);
  free((void *)image->actors);
  for (size_t i = 0; i < image->body_count; i++) {
    free((void *)image->bodies[i].body.ground);
    free((void *)image->bodies[i].attachment);
    if (image->bodies[i].linked != NULL)
      free((void *)image->bodies[i].linked->state.ground);
    free((void *)image->bodies[i].linked);
  }
  free((void *)image->bodies);
  free((void *)image->combat);
  for (size_t i = 0; i < image->inventory_count; i++)
    free((void *)image->inventories[i].entries);
  free((void *)image->inventories);
  for (size_t i = 0; i < image->configuration_count; i++)
    free((void *)image->configurations[i].weapons.values);
  free((void *)image->configurations);
  free((void *)image->thinks);
  free((void *)image->providers);
  for (size_t i = 0; i < image->guest_count; i++)
    qa_guest_checkpoint_release((qa_guest_checkpoint *)&image->guests[i]);
  free((void *)image->guests);
  free((void *)image->consumption);
  if (image->mods != NULL) free((void *)image->mods->mods);
  free((void *)image->mods);
  qa_executable_recipe_free(&image->recipe);
  qa_save_value_free(result->root);
  memset(result, 0, sizeof *result);
}
static const char SAVE_MAGIC[] = "QTSAVE5\n";
static const char SAVE_V4_MAGIC[] = "QTSAVE4\n";
static const char SAVE_V3_MAGIC[] = "QTSAVE3\n";
static const char SAVE_LEGACY_MAGIC[] = "QTSAVE2\n";
enum { SAVE_MAGIC_LENGTH = 8, SAVE_HEADER_LIMIT = 4096 };
/* TS indexes bytes[i] past the end as undefined, which simply fails the
 * signature comparison on a short buffer. */
static bool starts_with(const uint8_t *bytes, size_t length, const char *magic) {
  return length >= SAVE_MAGIC_LENGTH && memcmp(bytes, magic, SAVE_MAGIC_LENGTH) == 0;
}
static size_t header_length(const uint8_t *bytes, size_t length) {
  if (length < SAVE_MAGIC_LENGTH + 4) qa_save_format_error("save", "truncated save header");
  const uint8_t *p = bytes + SAVE_MAGIC_LENGTH;
  uint32_t value = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
  if (value > SAVE_HEADER_LIMIT) qa_save_format_error("save", "save header is too large");
  return value;
}
/* TS readSaveHeaderValue: borrowed strings from `value`. */
static qa_save_header read_save_header_value(const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "save-header")) qa_save_format_error("save-header", "out of memory");
  qa_save_header header;
  qa_save_reader map = field(&reader, "map");
  header.map = qa_save_reader_string(&map);
  qa_save_reader_destroy(&map);
  qa_save_reader content = field(&reader, "content");
  header.content = qa_read_content_id(&content);
  qa_save_reader_destroy(&content);
  qa_save_reader_destroy(&reader);
  return header;
}
static bool text_equal(const char *a, const char *b) {
  return a == b || (a != NULL && b != NULL && strcmp(a, b) == 0);
}
static void release_value_slot(void *raw) {
  qa_save_value **slot = (qa_save_value **)raw;
  qa_save_value_free(*slot);
  *slot = NULL;
}
bool qa_decode_save_image(const uint8_t *bytes, size_t length,
                          qa_save_image_result *out) {
  memset(out, 0, sizeof *out);
  double version = starts_with(bytes, length, SAVE_MAGIC) ? 5
      : starts_with(bytes, length, SAVE_V4_MAGIC) ? 4
      : starts_with(bytes, length, SAVE_V3_MAGIC) ? 3
      : starts_with(bytes, length, SAVE_LEGACY_MAGIC) ? 2 : 0;
  if (version == 0)
    qa_save_format_error("save", "unsupported unified save signature/version");
  const uint8_t *body = bytes + SAVE_MAGIC_LENGTH;
  size_t body_length = length - SAVE_MAGIC_LENGTH;
  /* TS has GC: a SaveFormatError raised while reading the decoded trees must not leak them. */
  qa_save_value *header_tree = NULL, *value = NULL;
  qa_cleanup header_guard, value_guard;
  qa_cleanup_push(&header_guard, release_value_slot, &header_tree);
  qa_cleanup_push(&value_guard, release_value_slot, &value);
  if (version >= 4) {
    size_t header_size = header_length(bytes, length), start = SAVE_MAGIC_LENGTH + 4;
    if (length < start + header_size) qa_save_format_error("save", "truncated save header");
    header_tree = qa_decode_checkpoint_value(bytes + start, header_size);
    if (header_tree == NULL) goto oom;
    (void)read_save_header_value(header_tree);
    body = bytes + start + header_size;
    body_length = length - start - header_size;
  }
  value = qa_decode_checkpoint_value(body, body_length);
  if (value == NULL) goto oom;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) goto oom;
  qa_save_reader schema = field(&reader, "schemaVersion");
  (void)qa_save_reader_literal_number(&schema, version == 5 ? 4 : version == 4 ? 3 : version);
  qa_save_reader_destroy(&schema);
  qa_save_reader_destroy(&reader);
  if (!qa_parse_save_image(value, out)) goto oom;
  bool same = true;
  if (header_tree != NULL) {
    qa_save_header header = read_save_header_value(header_tree);
    same = text_equal(header.map, out->image.recipe.map.geometry.requested_path) &&
           text_equal(header.content, out->image.recipe.map.entities.content);
  }
  qa_cleanup_pop(&value_guard);
  qa_cleanup_pop(&header_guard);
  qa_save_value_free(header_tree);
  out->root = value;
  if (!same) {
    qa_save_image_result_free(out);
    memset(out, 0, sizeof *out);
    qa_save_format_error("save", "save header differs from its recipe");
  }
  return true;
oom:
  qa_cleanup_pop(&value_guard);
  qa_cleanup_pop(&header_guard);
  qa_save_value_free(header_tree);
  qa_save_value_free(value);
  return false;
}

bool qa_read_save_header(const char *path, qa_save_header_result *out) {
  memset(out, 0, sizeof *out);
  FILE *file = fopen(path, "rb");
  if (file == NULL) return false;
  uint8_t prefix[SAVE_MAGIC_LENGTH + 4];
  size_t got = fread(prefix, 1, sizeof prefix, file);
  if (ferror(file)) {
    fclose(file);
    return false;
  }
  if (!starts_with(prefix, got, SAVE_MAGIC) && !starts_with(prefix, got, SAVE_V4_MAGIC)) {
    fclose(file);
    out->present = false;
    return true;
  }
  size_t size = header_length(prefix, got);
  uint8_t *bytes = malloc(size == 0 ? 1 : size);
  if (bytes == NULL) {
    fclose(file);
    return false;
  }
  size_t read = fread(bytes, 1, size, file);
  bool failed = ferror(file) != 0;
  fclose(file);
  if (failed) {
    free(bytes);
    return false;
  }
  if (read != size) {
    free(bytes);
    qa_save_format_error("save", "truncated save header");
  }
  out->root = qa_decode_checkpoint_value(bytes, size);
  free(bytes);
  if (out->root == NULL) return false;
  out->header = read_save_header_value(out->root);
  out->present = true;
  return true;
}
void qa_save_header_result_free(qa_save_header_result *result) {
  if (result == NULL) return;
  qa_save_value_free(result->root);
  memset(result, 0, sizeof *result);
}
bool qa_read_save_image(const char *path, qa_save_image_result *out) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) return false;
  size_t capacity = 65536, length = 0;
  uint8_t *bytes = malloc(capacity);
  if (bytes == NULL) {
    fclose(file);
    return false;
  }
  for (;;) {
    if (length == capacity) {
      capacity *= 2;
      uint8_t *grown = realloc(bytes, capacity);
      if (grown == NULL) {
        free(bytes);
        fclose(file);
        return false;
      }
      bytes = grown;
    }
    size_t count = fread(bytes + length, 1, capacity - length, file);
    length += count;
    if (count == 0) break;
  }
  bool failed = ferror(file) != 0;
  fclose(file);
  if (failed) {
    free(bytes);
    return false;
  }
  bool ok = qa_decode_save_image(bytes, length, out);
  free(bytes);
  return ok;
}
bool qa_write_save_image(const char *path, const qa_save_image *image) {
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!qa_encode_save_image(image, &bytes, &length)) return false;
  /* TS: temporary sibling `.<basename>.<randomUUID>.tmp`, rename
   * commits, rm(force) always runs. The UUID becomes a pid/counter
   * suffix (save_policy.c precedent); the name is unobservable
   * after commit. */
  const char *slash = strrchr(path, '/');
  const char *base = slash == NULL ? path : slash + 1;
  size_t directory_length = slash == NULL ? 1 : (size_t)(slash - path) + 1;
  static unsigned counter;
  size_t temporary_length = directory_length + 1 + strlen(base) + 32;
  char *temporary = malloc(temporary_length);
  if (temporary == NULL) {
    free(bytes);
    return false;
  }
  if (slash == NULL)
    snprintf(temporary, temporary_length, ".%s.%ld-%u.tmp", base,
             (long)getpid(), counter++);
  else
    snprintf(temporary, temporary_length, "%.*s.%s.%ld-%u.tmp",
             (int)directory_length, path, base, (long)getpid(), counter++);
  /* TS open(temporary, "wx", 0o600), writeFile, sync, close, then rename. */
  bool ok = false;
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd >= 0) {
    ok = true;
    size_t written = 0;
    while (ok && written < length) {
      ssize_t chunk = write(fd, bytes + written, length - written);
      if (chunk <= 0) ok = false;
      else written += (size_t)chunk;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok) ok = rename(temporary, path) == 0;
  }
  remove(temporary);
  free(temporary);
  free(bytes);
  return ok;
}

/* --- encodeSaveImage: checkpoint-value writers (NULL only on OOM) --- */
static qa_save_value *obj(void) { return qa_save_value_object(); }
static qa_save_value *arr(void) { return qa_save_value_array(); }
static bool set_str(qa_save_value *o, const char *key, const char *value) {
  return set_own(o, key, qa_save_value_string(value));
}
static bool set_num(qa_save_value *o, const char *key, double value) {
  return set_own(o, key, qa_save_value_number(value));
}
static bool set_bool(qa_save_value *o, const char *key, bool value) {
  return set_own(o, key, qa_save_value_boolean(value));
}
static bool set_null(qa_save_value *o, const char *key) {
  return set_own(o, key, qa_save_value_null());
}
static bool set_bytes(qa_save_value *o, const char *key, qa_byte_view bytes) {
  return set_own(o, key, qa_save_value_bytes(bytes.bytes, bytes.length));
}
#define TRY(expr)                                  \
  do {                                             \
    if (!(expr)) {                                 \
      qa_save_value_free(o);                       \
      return NULL;                                 \
    }                                              \
  } while (0)
static qa_save_value *vector_value(qa_vec3 v) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_num(o, "x", v.x) && set_num(o, "y", v.y) && set_num(o, "z", v.z));
  return o;
}
static qa_save_value *bounds_value(qa_bounds b) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_own(o, "min", vector_value(b.min)) &&
      set_own(o, "max", vector_value(b.max)));
  return o;
}
static qa_save_value *time_value(qa_source_time t) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "kind",
              t.kind == QA_SOURCE_TIME_SECONDS ? "seconds" : "milliseconds") &&
      set_num(o, "value", t.value));
  return o;
}
static qa_save_value *clock_value(qa_clock_profile c) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (c.kind) {
    case QA_CLOCK_Q1_NETQUAKE:
      TRY(set_str(o, "kind", "q1-netquake") &&
          set_num(o, "minimumFrameSeconds", c.minimum_frame_seconds) &&
          set_num(o, "maximumFrameSeconds", c.maximum_frame_seconds) &&
          (c.has_fixed_frame_seconds
               ? set_num(o, "fixedFrameSeconds", c.fixed_frame_seconds)
               : set_null(o, "fixedFrameSeconds")));
      break;
    case QA_CLOCK_Q1_QUAKEWORLD:
      TRY(set_str(o, "kind", "q1-quakeworld") &&
          set_num(o, "maximumCommandMilliseconds",
                  c.maximum_command_milliseconds));
      break;
    case QA_CLOCK_Q2_CLASSIC:
      TRY(set_str(o, "kind", "q2-classic") &&
          set_num(o, "frameMilliseconds", c.frame_milliseconds));
      break;
    case QA_CLOCK_Q2_RERELEASE:
      TRY(set_str(o, "kind", "q2-rerelease") &&
          set_num(o, "frameMilliseconds", c.frame_milliseconds) &&
          set_str(o, "preparation", "before-frame"));
      break;
    case QA_CLOCK_Q3:
      TRY(set_str(o, "kind", "q3") &&
          set_num(o, "serverFrameMilliseconds", c.server_frame_milliseconds) &&
          (c.has_fixed_movement_milliseconds
               ? set_num(o, "fixedMovementMilliseconds",
                         c.fixed_movement_milliseconds)
               : set_null(o, "fixedMovementMilliseconds")) &&
          set_num(o, "maximumCommandMilliseconds",
                  c.maximum_command_milliseconds));
      break;
  }
  return o;
}
static const char *rounding_name(qa_floating_rounding rounding) {
  switch (rounding) {
    case QA_FLOATING_ROUNDING_TOWARD_ZERO: return "toward-zero";
    case QA_FLOATING_ROUNDING_TOWARD_POSITIVE: return "toward-positive";
    case QA_FLOATING_ROUNDING_TOWARD_NEGATIVE: return "toward-negative";
    case QA_FLOATING_ROUNDING_NEAREST_EVEN: return "nearest-even";
  }
  return "nearest-even";
}
static qa_save_value *arithmetic_value(qa_arithmetic_profile p) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (p.kind) {
    case QA_ARITHMETIC_BINARY32:
      TRY(set_str(o, "kind", "binary32") &&
          set_str(o, "round", "each-operation"));
      break;
    case QA_ARITHMETIC_DONOR_BINARY64:
      TRY(set_str(o, "kind", "donor-binary64") && set_str(o, "source", p.source));
      break;
    case QA_ARITHMETIC_X87:
      TRY(set_str(o, "kind", "x87") &&
          set_num(o, "precisionBits", (double)p.precision_bits) &&
          set_str(o, "rounding", rounding_name(p.rounding)));
      break;
    case QA_ARITHMETIC_SSE:
      TRY(set_str(o, "kind", "sse") &&
          set_bool(o, "flushToZero", p.flush_to_zero) &&
          set_bool(o, "denormalsAreZero", p.denormals_are_zero) &&
          set_str(o, "rounding", rounding_name(p.rounding)));
      break;
  }
  return o;
}
static qa_save_value *numeric_value(qa_numeric_profile p) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  const char *conversion =
      p.float_to_int == QA_FLOAT_TO_INT_QVM_INDEFINITE ? "qvm-indefinite"
      : p.float_to_int == QA_FLOAT_TO_INT_X86_INDEFINITE ? "x86-indefinite"
                                                         : "checked-c-truncation";
  TRY(set_str(o, "id", p.id) &&
      set_own(o, "arithmetic", arithmetic_value(p.arithmetic)) &&
      set_str(o, "scalarStorage", "binary32") &&
      set_str(o, "floatToInt", conversion) &&
      set_str(o, "integerOverflow", "wrap32"));
  return o;
}
static qa_save_value *ordering_value(qa_frame_ordering ordering) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  if (ordering.kind == QA_FRAME_ORDERING_NATIVE) {
    TRY(set_str(o, "kind", "native") &&
        set_str(o, "traversal", "source-slot-order") &&
        set_own(o, "clock", clock_value(ordering.clock)));
    return o;
  }
  qa_save_value *providers = arr();
  if (providers == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  for (size_t i = 0; i < ordering.provider_count; i++)
    if (!push_own(providers, qa_save_value_string(ordering.providers[i]))) {
      qa_save_value_free(providers);
      qa_save_value_free(o);
      return NULL;
    }
  TRY(set_str(o, "kind", "mixed") && set_own(o, "providers", providers) &&
      set_str(o, "entityOrder", "source-slot-order") &&
      set_str(o, "ties", "provider-entity-invocation"));
  return o;
}
/* TS readRandom / SourceRandom.checkpoint key order: kind first, draws last. */
static qa_save_value *random_value(qa_random_state s) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (s.kind) {
    case QA_RANDOM_STATE_Q3_LCG:
    case QA_RANDOM_STATE_MSVCRT_RAND:
      TRY(set_str(o, "kind",
                  s.kind == QA_RANDOM_STATE_Q3_LCG ? "q3-lcg" : "msvcrt-rand") &&
          set_num(o, "seed", s.seed));
      break;
    case QA_RANDOM_STATE_GLIBC_RANDOM:
    case QA_RANDOM_STATE_Q2_RERELEASE_MT19937: {
      qa_save_value *words = arr();
      if (words == NULL) {
        qa_save_value_free(o);
        return NULL;
      }
      for (size_t i = 0; i < s.words.count; i++)
        if (!push_own(words, qa_save_value_number(s.words.values[i]))) {
          qa_save_value_free(words);
          qa_save_value_free(o);
          return NULL;
        }
      if (s.kind == QA_RANDOM_STATE_GLIBC_RANDOM) {
        TRY(set_str(o, "kind", "glibc-random") &&
            set_own(o, "words", words) && set_num(o, "front", s.front) &&
            set_num(o, "rear", s.rear));
      } else {
        TRY(set_str(o, "kind", "q2-rerelease-mt19937") &&
            set_str(o, "distribution", "msvc-2022-17.6") &&
            set_own(o, "words", words) && set_num(o, "index", s.index));
      }
      break;
    }
    case QA_RANDOM_STATE_GUEST:
      TRY(set_str(o, "kind", "guest") && set_str(o, "module", s.module) &&
          set_bytes(o, "bytes", s.bytes));
      break;
  }
  TRY(set_num(o, "draws", s.draws));
  return o;
}
static qa_save_value *frame_value(qa_frame_context f) {
  static const char *const phases[] = {
      "frame-entry", "client-command", "entity-prethink", "entity-physics",
      "entity-think", "client-end-frame", "frame-exit"};
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_num(o, "frame", f.frame) && set_own(o, "time", time_value(f.time)) &&
      set_own(o, "elapsed", time_value(f.elapsed)) &&
      set_str(o, "phase", phases[f.phase]));
  return o;
}
static qa_save_value *provider_value(qa_provider_reference p) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "provider", p.provider) && set_str(o, "content", p.content));
  return o;
}
static qa_save_value *mount_identity_value(qa_mount_identity identity) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "id", identity.id) && set_str(o, "content", identity.content) &&
      set_num(o, "generation", (double)identity.generation));
  return o;
}
/* `parsed`: persistence/recipe.ts readMount order `{ identity, kind, ... }`;
 * otherwise the catalog's mountsFor record order `{ kind, identity, ... }`. */
static qa_save_value *mount_value(qa_content_mount mount, bool parsed) {
  static const char *const formats[] = {"pak", "pk3", "kpf", "zip"};
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  if (parsed) {
    if (mount.kind == QA_CONTENT_MOUNT_LOOSE) {
      TRY(set_own(o, "identity", mount_identity_value(mount.as.loose.identity)) &&
          set_str(o, "kind", "loose") &&
          set_str(o, "rootPath", mount.as.loose.root_path));
      return o;
    }
    TRY(set_own(o, "identity", mount_identity_value(mount.as.archive.identity)) &&
        set_str(o, "kind", "archive") &&
        set_str(o, "format", formats[mount.as.archive.format]) &&
        set_str(o, "archivePath", mount.as.archive.archive_path) &&
        set_str(o, "archiveDigest", mount.as.archive.archive_digest));
    return o;
  }
  if (mount.kind == QA_CONTENT_MOUNT_LOOSE) {
    TRY(set_str(o, "kind", "loose") &&
        set_own(o, "identity", mount_identity_value(mount.as.loose.identity)) &&
        set_str(o, "rootPath", mount.as.loose.root_path));
    return o;
  }
  TRY(set_str(o, "kind", "archive") &&
      set_own(o, "identity",
              mount_identity_value(mount.as.archive.identity)) &&
      set_str(o, "archivePath", mount.as.archive.archive_path) &&
      set_str(o, "archiveDigest", mount.as.archive.archive_digest) &&
      set_str(o, "format", formats[mount.as.archive.format]));
  return o;
}
static qa_save_value *resource_value(qa_resolved_resource_reference r) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *provenance = obj();
  if (provenance == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  {
    qa_save_value *pv = provenance;
    bool ok;
    if (r.provenance.kind == QA_CONTENT_MOUNT_ARCHIVE) {
      qa_content_mount mount;
      memset(&mount, 0, sizeof mount);
      mount.kind = QA_CONTENT_MOUNT_ARCHIVE;
      mount.as.archive = *r.provenance.archive_mount;
      /* mounts/index.ts: kind, mount, memberPath, memberIndex;
       * readProvenance: kind, memberPath, mount, memberIndex. */
      ok = set_str(pv, "kind", "archive") &&
           (!r.parsed_order || set_str(pv, "memberPath", r.provenance.member_path)) &&
           set_own(pv, "mount", mount_value(mount, r.parsed_order)) &&
           (r.parsed_order || set_str(pv, "memberPath", r.provenance.member_path)) &&
           set_num(pv, "memberIndex", (double)r.provenance.member_index);
    } else {
      qa_content_mount mount;
      memset(&mount, 0, sizeof mount);
      mount.kind = QA_CONTENT_MOUNT_LOOSE;
      mount.as.loose = *r.provenance.loose_mount;
      ok = set_str(pv, "kind", "loose") &&
           (!r.parsed_order || set_str(pv, "memberPath", r.provenance.member_path)) &&
           set_own(pv, "mount", mount_value(mount, r.parsed_order)) &&
           (r.parsed_order || set_str(pv, "memberPath", r.provenance.member_path));
    }
    if (!ok) {
      qa_save_value_free(provenance);
      qa_save_value_free(o);
      return NULL;
    }
  }
  qa_save_value *resolution = obj();
  if (resolution == NULL) {
    qa_save_value_free(provenance);
    qa_save_value_free(o);
    return NULL;
  }
  {
    /* TS readResolution: { kind, plan, ... }. */
    bool ok = true;
    if (ok) {
      switch (r.resolution.kind) {
        case QA_RESOURCE_RESOLUTION_DEFAULT_ORDER:
          ok = set_str(resolution, "kind", "default-order") &&
               set_str(resolution, "plan", r.resolution.plan) &&
               set_num(resolution, "rank", (double)r.resolution.rank);
          break;
        case QA_RESOURCE_RESOLUTION_PREFIX_ORDER:
          ok = set_str(resolution, "kind", "prefix-order") &&
               set_str(resolution, "plan", r.resolution.plan) &&
               set_num(resolution, "rank", (double)r.resolution.rank) &&
               set_str(resolution, "prefix", r.resolution.prefix);
          break;
        case QA_RESOURCE_RESOLUTION_LINK:
          ok = set_str(resolution, "kind", "link") &&
               set_str(resolution, "plan", r.resolution.plan) &&
               set_str(resolution, "sourcePrefix", r.resolution.source_prefix) &&
               set_str(resolution, "targetPath", r.resolution.target_path);
          break;
      }
    }
    if (!ok) {
      qa_save_value_free(resolution);
      qa_save_value_free(provenance);
      qa_save_value_free(o);
      return NULL;
    }
  }
  /* mounts/index.ts builds `{ ...resource, id }` (id last); a reference read
   * back by recipe.ts readResource is `{ id, ...resource }` (id first). */
  TRY((!r.parsed_order || set_str(o, "id", r.id)) &&
      set_str(o, "requestedPath", r.requested_path) &&
      set_own(o, "provenance", provenance) &&
      set_str(o, "digest", r.digest) &&
      set_num(o, "byteLength", (double)r.byte_length) &&
      set_own(o, "resolution", resolution) && (r.parsed_order || set_str(o, "id", r.id)));
  return o;
}
qa_save_value *qa_save_resource_reference_value(const qa_resolved_resource_reference *resource) {
  return resource_value(*resource);
}
static qa_save_value *api_value(qa_game_api_identity api) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (api.kind) {
    case QA_GAME_API_QUAKEC:
      TRY(set_str(o, "kind",
                  api.as.quakec.kind == QA_QUAKEC_API_Q1_NETQUAKE
                      ? "q1-netquake"
                      : "q1-quakeworld") &&
          set_num(o, "programVersion", (double)api.as.quakec.program_version) &&
          set_num(o, "systemCrc", (double)api.as.quakec.system_crc));
      break;
    case QA_GAME_API_Q2_GAME:
      TRY(set_str(o, "kind",
                  api.as.q2_game.kind == QA_Q2_GAME_API_CLASSIC
                      ? "q2-classic-game"
                      : "q2-rerelease-game") &&
          set_num(o, "version", (double)api.as.q2_game.version));
      break;
    case QA_GAME_API_Q2_CGAME:
      TRY(set_str(o, "kind", "q2-rerelease-cgame") &&
          set_num(o, "version", (double)api.as.q2_cgame.version));
      break;
    case QA_GAME_API_Q3:
      TRY(set_str(o, "kind",
                  api.as.q3.kind == QA_Q3_API_QAGAME ? "q3-qagame"
                  : api.as.q3.kind == QA_Q3_API_CGAME ? "q3-cgame"
                                                      : "q3-ui") &&
          set_num(o, "version", (double)api.as.q3.version));
      break;
  }
  return o;
}
static qa_save_value *abi_value(qa_native_abi abi) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "kind", abi.kind) && set_str(o, "image", abi.image) &&
      set_num(o, "pointerBytes", (double)abi.pointer_bytes) &&
      set_str(o, "call", abi.call));
  return o;
}
static qa_save_value *module_value(qa_module_identity m) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "id", m.id) && set_str(o, "artifactPath", m.artifact_path) &&
      set_str(o, "digest", m.digest) && set_str(o, "revision", m.revision));
  return o;
}
static qa_save_value *layout_value(qa_guest_layout layout) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *fields = arr();
  if (fields == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  for (size_t i = 0; i < layout.field_count; i++) {
    qa_save_value *entry = obj();
    bool ok = entry != NULL &&
              set_str(entry, "name", layout.fields[i].name) &&
              set_num(entry, "byteOffset", layout.fields[i].byte_offset) &&
              set_str(entry, "storage", layout.fields[i].storage) &&
              set_num(entry, "count", layout.fields[i].count) &&
              push_own(fields, entry);
    if (!ok) {
      qa_save_value_free(fields);
      qa_save_value_free(o);
      return NULL;
    }
  }
  TRY(set_str(o, "id", layout.id) &&
      set_num(o, "byteLength", layout.byte_length) &&
      set_num(o, "alignment", layout.alignment) &&
      set_num(o, "pointerBytes", (double)layout.pointer_bytes) &&
      set_str(o, "byteOrder", "little-endian") &&
      set_own(o, "fields", fields));
  return o;
}
/* { ...PROCESSOR_LAYOUT, pointerBytes }: the native Q3 processor layout's spread key order. */
static qa_save_value *native_processor_layout_value(qa_guest_layout layout) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *fields = arr();
  if (fields == NULL || layout.field_count != 0) {
    qa_save_value_free(fields);
    qa_save_value_free(o);
    return layout.field_count != 0 ? layout_value(layout) : NULL;
  }
  TRY(set_str(o, "id", layout.id) && set_num(o, "byteLength", layout.byte_length) &&
      set_num(o, "alignment", layout.alignment) && set_str(o, "byteOrder", "little-endian") &&
      set_own(o, "fields", fields) && set_num(o, "pointerBytes", (double)layout.pointer_bytes));
  return o;
}
static qa_save_value *value_layout_value(qa_guest_value_layout layout) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  if (layout.is_aggregate) {
    TRY(set_str(o, "kind", "aggregate") &&
        set_own(o, "layout", layout_value(layout.layout)));
    return o;
  }
  TRY(set_str(o, "kind", "scalar") && set_str(o, "storage", layout.storage));
  return o;
}
static qa_save_value *callback_reference_value(
    qa_saved_guest_callback_reference saved) {
  qa_guest_callback_reference ref = saved.reference;
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (ref.kind) {
    case QA_GUEST_CALLBACK_TYPESCRIPT:
      TRY(set_str(o, "kind", "typescript") &&
          set_str(o, "provider", ref.provider) &&
          set_str(o, "callback", ref.callback));
      break;
    case QA_GUEST_CALLBACK_QUAKEC:
      TRY(set_str(o, "kind", "quakec") &&
          set_own(o, "module", module_value(ref.module)) &&
          set_num(o, "functionIndex", ref.function_index));
      break;
    case QA_GUEST_CALLBACK_QVM:
      TRY(set_str(o, "kind", "qvm") &&
          set_own(o, "module", module_value(ref.module)) &&
          set_num(o, "instructionIndex", ref.instruction_index));
      break;
    case QA_GUEST_CALLBACK_NATIVE_GUEST:
      TRY(set_str(o, "kind", "native-guest") &&
          set_own(o, "module", module_value(ref.module)) &&
          set_own(o, "byteOffset", qa_save_value_bigint_i64(saved.byte_offset)) &&
          set_own(o, "abi", abi_value(ref.abi)));
      break;
  }
  return o;
}
static qa_save_value *callback_value(qa_saved_guest_callback_binding binding) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *parameters = arr();
  if (parameters == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  for (size_t i = 0; i < binding.parameter_count; i++)
    if (!push_own(parameters, value_layout_value(binding.parameters[i]))) {
      qa_save_value_free(parameters);
      qa_save_value_free(o);
      return NULL;
    }
  TRY(set_str(o, "id", binding.id) &&
      set_own(o, "reference", callback_reference_value(binding.reference)) &&
      set_own(o, "parameters", parameters) &&
      (binding.result_is_void
           ? set_str(o, "result", "void")
           : set_own(o, "result", value_layout_value(binding.result))));
  return o;
}
static qa_save_value *private_value(qa_guest_private_state state) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_own(o, "module", module_value(state.module)) &&
      set_str(o, "format", state.format) && set_bytes(o, "bytes", state.bytes));
  return o;
}
/* Moves the listed keys, in order, to the front of an object (the rest keep
 * their relative order): a producer's JS insertion order. */
static void order_keys(qa_save_value *o, const char *const *keys, size_t key_count) {
  size_t at = 0;
  for (size_t k = 0; k < key_count; k++)
    for (size_t i = at; i < o->entry_count; i++)
      if (strcmp(o->entries[i].key, keys[k]) == 0) {
        qa_save_entry moved = o->entries[i];
        memmove(&o->entries[at + 1], &o->entries[at], (i - at) * sizeof *o->entries);
        o->entries[at++] = moved;
        break;
      }
}
/* The checkpoint producers' key orders (QvmModule.checkpoint, QuakeC executor checkpoint). */
static void order_guest_checkpoint(qa_save_value *o, qa_guest_checkpoint_kind kind) {
  static const char *const qvm[] = {"kind", "module", "api", "abiProfile", "data", "instructionIndex", "programStack", "operandStack",
      "random", "callbacks", "hostState"};
  static const char *const quakec[] = {"kind", "module", "api", "random", "callbacks", "globals", "entities", "entityStrideBytes",
      "entityCount", "strings", "statement", "functionIndex", "argumentCount", "callStack", "locals", "hostState"};
  if (o == NULL) return;
  if (kind == QA_GUEST_CHECKPOINT_QVM) order_keys(o, qvm, sizeof qvm / sizeof qvm[0]);
  else if (kind == QA_GUEST_CHECKPOINT_QUAKEC) order_keys(o, quakec, sizeof quakec / sizeof quakec[0]);
}
/* TS NativeGuestHeapImage, keys in the producer's order. */
static qa_save_value *bigint_or_null(bool present, int64_t value) {
  return present ? qa_save_value_bigint_i64(value) : qa_save_value_null();
}
static qa_save_value *windows_tables_value(const qa_windows_heap_tables *w) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *heaps = arr(), *reservations = arr(), *tls = arr(), *fls = arr();
  bool ok = heaps != NULL && reservations != NULL && tls != NULL && fls != NULL;
  for (size_t i = 0; ok && i < w->heap_count; i++) ok = push_own(heaps, qa_save_value_bigint_i64(w->heaps[i]));
  for (size_t i = 0; ok && i < w->reservation_count; i++) {
    qa_save_value *entry = obj();
    ok = entry != NULL && set_own(entry, "base", qa_save_value_bigint_i64(w->reservations[i].base)) &&
         set_num(entry, "size", w->reservations[i].size);
    if (ok) ok = push_own(reservations, entry);
    else qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < w->tls_count; i++) ok = push_own(tls, qa_save_value_number(w->tls[i]));
  for (size_t i = 0; ok && i < w->fls_count; i++) {
    qa_save_value *slot = obj();
    ok = slot != NULL && set_num(slot, "index", w->fls[i].index) &&
         set_own(slot, "callback", bigint_or_null(w->fls[i].has_callback, w->fls[i].callback)) &&
         set_own(slot, "value", bigint_or_null(w->fls[i].has_value, w->fls[i].value));
    if (ok) ok = push_own(fls, slot);
    else qa_save_value_free(slot);
  }
  if (!ok) {
    qa_save_value_free(heaps);
    qa_save_value_free(reservations);
    qa_save_value_free(tls);
    qa_save_value_free(fls);
    qa_save_value_free(o);
    return NULL;
  }
  TRY(set_own(o, "heaps", heaps) && set_own(o, "reservations", reservations) && set_own(o, "tls", tls) &&
      set_own(o, "fls", fls) && set_num(o, "lastError", w->last_error));
  return o;
}
static qa_save_value *native_heap_value(const qa_native_guest_heap_image *heap) {
  if (heap == NULL) return qa_save_value_null();
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *layout = arr(), *allocator = obj(), *hints = arr(), *slabs = arr(), *free_pages = arr(),
                *blocks = arr();
  bool ok = layout != NULL && allocator != NULL && hints != NULL && slabs != NULL && free_pages != NULL &&
            blocks != NULL;
  for (size_t i = 0; ok && i < heap->layout_count; i++) {
    const qa_guest_mapping_record *m = &heap->layout[i];
    qa_save_value *entry = obj();
    ok = entry != NULL && set_own(entry, "base", qa_save_value_bigint_i64(m->base)) &&
         set_num(entry, "byteLength", m->byte_length) && set_str(entry, "permissions", m->permissions) &&
         set_str(entry, "label", m->label);
    if (ok) ok = push_own(layout, entry);
    else qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < heap->allocator.hint_count; i++) {
    const qa_guest_allocator_hint *h = &heap->allocator.hints[i];
    qa_save_value *entry = obj();
    ok = entry != NULL && set_own(entry, "alignment", qa_save_value_bigint_i64(h->alignment)) &&
         set_own(entry, "base", qa_save_value_bigint_i64(h->base)) && set_num(entry, "byteLength", h->byte_length);
    if (ok) ok = push_own(hints, entry);
    else qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < heap->allocator.slab_count; i++) {
    const qa_guest_allocator_slab *sl = &heap->allocator.slabs[i];
    qa_save_value *entry = obj();
    ok = entry != NULL && set_own(entry, "base", qa_save_value_bigint_i64(sl->base)) &&
         set_bytes(entry, "used", sl->used) && set_num(entry, "next", sl->next);
    if (ok) ok = push_own(slabs, entry);
    else qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < heap->allocator.free_page_count; i++)
    ok = push_own(free_pages, qa_save_value_number(heap->allocator.free_pages[i]));
  for (size_t i = 0; ok && i < heap->block_count; i++) {
    const qa_native_heap_block *b = &heap->blocks[i];
    qa_save_value *entry = obj();
    ok = entry != NULL && set_own(entry, "base", qa_save_value_bigint_i64(b->base)) && set_num(entry, "size", b->size) &&
         set_own(entry, "heap", qa_save_value_bigint_i64(b->heap));
    if (ok) ok = push_own(blocks, entry);
    else qa_save_value_free(entry);
  }
  if (ok) {
    ok = set_own(allocator, "hints", hints);
    hints = NULL;
    if (ok) ok = set_own(allocator, "slabs", slabs);
    slabs = NULL;
    if (ok) ok = set_own(allocator, "freePages", free_pages);
    free_pages = NULL;
  }
  if (!ok) {
    qa_save_value_free(layout);
    qa_save_value_free(allocator);
    qa_save_value_free(hints);
    qa_save_value_free(slabs);
    qa_save_value_free(free_pages);
    qa_save_value_free(blocks);
    qa_save_value_free(o);
    return NULL;
  }
  TRY(set_own(o, "layout", layout) && set_own(o, "allocator", allocator) && set_own(o, "blocks", blocks) &&
      set_own(o, "windows", heap->windows == NULL ? qa_save_value_null() : windows_tables_value(heap->windows)));
  return o;
}
static qa_save_value *guest_value(const qa_guest_checkpoint *guest) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *random = arr();
  qa_save_value *callbacks = arr();
  if (random == NULL || callbacks == NULL) {
    qa_save_value_free(random);
    qa_save_value_free(callbacks);
    qa_save_value_free(o);
    return NULL;
  }
  for (size_t i = 0; i < guest->random_count; i++)
    if (!push_own(random, random_value(guest->random[i]))) goto oom;
  for (size_t i = 0; i < guest->callback_count; i++)
    if (!push_own(callbacks, callback_value(guest->callbacks[i]))) goto oom;
  /* A native Q3 process image is written in Q3NativeGuestSource.checkpoint()'s key order: kind and version first. */
  if (guest->kind == QA_GUEST_CHECKPOINT_NATIVE_GUEST && !(set_str(o, "kind", "native-guest") && set_num(o, "version", 2))) goto oom;
  TRY(set_own(o, "module", module_value(guest->module)) &&
      set_own(o, "random", random) && set_own(o, "callbacks", callbacks));
  random = NULL;
  callbacks = NULL;
  switch (guest->kind) {
    case QA_GUEST_CHECKPOINT_TYPESCRIPT:
      TRY(set_str(o, "kind", "typescript") &&
          set_own(o, "api", api_value(guest->game_api)) &&
          set_own(o, "state", private_value(guest->state)));
      break;
    case QA_GUEST_CHECKPOINT_QUAKEC: {
      qa_game_api_identity api;
      memset(&api, 0, sizeof api);
      api.kind = QA_GAME_API_QUAKEC;
      api.as.quakec = guest->quakec_api;
      qa_save_value *stack = arr();
      if (stack == NULL) goto oom;
      for (size_t i = 0; i < guest->call_stack_count; i++) {
        qa_save_value *entry = obj();
        bool ok = entry != NULL &&
                  set_num(entry, "statement", guest->call_stack[i].statement) &&
                  set_num(entry, "functionIndex",
                          guest->call_stack[i].function_index) &&
                  push_own(stack, entry);
        if (!ok) {
          qa_save_value_free(stack);
          goto oom;
        }
      }
      TRY(set_str(o, "kind", "quakec") && set_own(o, "api", api_value(api)) &&
          set_bytes(o, "globals", guest->globals) &&
          set_bytes(o, "entities", guest->entities) &&
          set_num(o, "entityStrideBytes", guest->entity_stride_bytes) &&
          set_num(o, "entityCount", guest->entity_count) &&
          set_bytes(o, "strings", guest->strings) &&
          set_num(o, "statement", guest->statement) &&
          set_num(o, "functionIndex", guest->function_index) &&
          set_num(o, "argumentCount", guest->argument_count) &&
          set_own(o, "callStack", stack) &&
          set_bytes(o, "locals", guest->locals) &&
          set_own(o, "hostState", private_value(guest->host_state)));
      break;
    }
    case QA_GUEST_CHECKPOINT_QVM: {
      qa_game_api_identity api;
      memset(&api, 0, sizeof api);
      api.kind = QA_GAME_API_Q3;
      api.as.q3 = guest->q3_api;
      qa_save_value *stack = arr();
      if (stack == NULL) goto oom;
      for (size_t i = 0; i < guest->operand_stack.count; i++)
        if (!push_own(stack,
                      qa_save_value_number(guest->operand_stack.values[i]))) {
          qa_save_value_free(stack);
          goto oom;
        }
      /* TS readGuest defaults an absent abiProfile to "q3-modern";
       * the C struct cannot distinguish absent from that default
       * once parsed, so encode always writes the profile. */
      TRY(set_str(o, "kind", "qvm") && set_own(o, "api", api_value(api)) &&
          set_str(o, "abiProfile",
                  guest->has_abi_profile ? guest->abi_profile : "q3-modern") &&
          set_bytes(o, "data", guest->data) &&
          set_num(o, "instructionIndex", guest->instruction_index) &&
          set_num(o, "programStack", guest->program_stack) &&
          set_own(o, "operandStack", stack) &&
          set_own(o, "hostState", private_value(guest->host_state)));
      break;
    }
    case QA_GUEST_CHECKPOINT_NATIVE_GUEST: {
      qa_save_value *regions = arr();
      if (regions == NULL) goto oom;
      for (size_t i = 0; i < guest->region_count; i++) {
        qa_save_value *entry = obj();
        bool ok = entry != NULL &&
                  set_own(entry, "base",
                          qa_save_value_bigint_i64(guest->regions[i].base)) &&
                  set_str(entry, "permissions", guest->regions[i].permissions) &&
                  set_bytes(entry, "bytes", guest->regions[i].bytes) &&
                  push_own(regions, entry);
        if (!ok) {
          qa_save_value_free(regions);
          goto oom;
        }
      }
      TRY(set_own(o, "abi", abi_value(guest->abi)) &&
          set_own(o, "regions", regions) &&
          set_own(o, "heap", native_heap_value(guest->heap)) &&
          set_own(o, "processorLayout",
                  native_processor_layout_value(guest->processor_layout)) &&
          set_bytes(o, "processorState", guest->processor_state) &&
          set_own(o, "runtimeState", private_value(guest->runtime_state)));
      break;
    }
  }
  return o;
oom:
  qa_save_value_free(random);
  qa_save_value_free(callbacks);
  qa_save_value_free(o);
  return NULL;
}
/* TS readExecution: { kind, owner, implementation | artifact, profile?, role, api }. */
static qa_save_value *execution_value(qa_resolved_execution_module module) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  static const char *const kinds[] = {"typescript", "quakec", "qvm", "native"};
  const char *kind = module.kind == QA_EXECUTION_MODULE_TYPESCRIPT ? kinds[0]
      : module.kind == QA_EXECUTION_MODULE_QUAKEC ? kinds[1]
      : module.kind == QA_EXECUTION_MODULE_QVM ? kinds[2] : kinds[3];
  /* Key order of the content.ts selections (launch replaces artifact in
   * place): typescript {kind, owner, implementation, role, api}; quakec,
   * qvm and native {kind, owner, role, artifact, api, profile?}. */
  TRY(set_str(o, "kind", kind) && set_own(o, "owner", provider_value(module.owner)));
  if (module.kind == QA_EXECUTION_MODULE_TYPESCRIPT)
    TRY(set_str(o, "implementation", module.implementation) && set_str(o, "role", module.role));
  else
    TRY(set_str(o, "role", module.role) &&
        set_own(o, "artifact",
                resource_value(*(const qa_resolved_resource_reference *)module.artifact)));
  TRY(set_own(o, "api", api_value(module.api)));
  if (module.kind == QA_EXECUTION_MODULE_NATIVE)
    TRY(set_own(o, "profile", abi_value(module.profile)));
  return o;
}
static qa_save_value *character_value(qa_character_selection c) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_own(o, "definition", provider_value(c.definition)) &&
      set_own(o, "appearance", provider_value(c.appearance)));
  return o;
}
static qa_save_value *monster_target_value(qa_monster_selection_target t) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  if (t.is_map_defined) {
    TRY(set_str(o, "kind", "map-defined"));
    return o;
  }
  TRY(set_own(o, "source", provider_value(t.reference.source)) &&
      set_str(o, "classname", t.reference.classname));
  return o;
}
static qa_save_value *enemies_value(qa_enemy_selection e) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (e.kind) {
    case QA_ENEMY_SELECTION_MAP_DEFINED:
      TRY(set_str(o, "kind", "map-defined"));
      break;
    case QA_ENEMY_SELECTION_PLAYERS:
      TRY(set_str(o, "kind", "players") &&
          set_own(o, "character", provider_value(e.character)));
      break;
    case QA_ENEMY_SELECTION_REPLACE: {
      qa_save_value *by_classname = obj();
      if (by_classname == NULL) {
        qa_save_value_free(o);
        return NULL;
      }
      for (size_t i = 0; i < e.by_classname_count; i++)
        if (!set_own(by_classname, e.by_classname[i].classname,
                     monster_target_value(e.by_classname[i].target))) {
          qa_save_value_free(by_classname);
          qa_save_value_free(o);
          return NULL;
        }
      TRY(set_str(o, "kind", "replace") &&
          set_own(o, "default", monster_target_value(e.default_target)) &&
          set_own(o, "byClassname", by_classname));
      break;
    }
  }
  return o;
}
static qa_save_value *equipment_value(qa_equipment_selection equipment) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *grapple = obj();
  if (grapple == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  if (!equipment.grapple.is_enabled) {
    if (!set_str(grapple, "kind", "disabled")) goto grapple_oom;
  } else if (strcmp(equipment.grapple.mechanic, "q3-qvm") == 0) {
    /* No checkpoint-value writer for qa_qvm_grapple_definition yet. */
    qa_save_format_error("save",
                         "q3-qvm grapple profile writer is not yet ported");
  } else {
    bool ok = set_str(grapple, "kind", "enabled") &&
              set_own(grapple, "source",
                      provider_value(equipment.grapple.source)) &&
              set_str(grapple, "binding", equipment.grapple.binding) &&
              set_str(grapple, "mechanic", equipment.grapple.mechanic) &&
              set_str(grapple, "edition", equipment.grapple.edition);
    if (!ok) goto grapple_oom;
  }
  {
    qa_save_value *grenades = obj();
    if (grenades == NULL) goto grapple_oom;
    bool ok;
    if (!equipment.hand_grenades.is_enabled) {
      ok = set_str(grenades, "kind", "disabled");
    } else {
      ok = set_str(grenades, "kind", "enabled") &&
           set_own(grenades, "source",
                   provider_value(equipment.hand_grenades.source)) &&
           set_str(grenades, "binding", "offhand") &&
           set_str(grenades, "edition", equipment.hand_grenades.edition) &&
           set_num(grenades, "initialAmmo",
                   equipment.hand_grenades.initial_ammo) &&
           set_num(grenades, "capacity", equipment.hand_grenades.capacity);
    }
    if (!ok) {
      qa_save_value_free(grenades);
      goto grapple_oom;
    }
    TRY(set_own(o, "grapple", grapple) &&
        set_own(o, "handGrenades", grenades));
    return o;
  }
grapple_oom:
  qa_save_value_free(grapple);
  qa_save_value_free(o);
  return NULL;
}
static qa_save_value *presentation_value(qa_presentation_selection p) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *doppler = obj();
  qa_save_value *environment = obj();
  if (doppler == NULL || environment == NULL) goto oom;
  if (!set_str(doppler, "kind", p.doppler.is_source ? "source" : "disabled"))
    goto oom;
  switch (p.environment.kind) {
    case QA_ENVIRONMENT_AUDIO_CONTENT:
      if (!set_str(environment, "kind", "audio-content")) goto oom;
      break;
    case QA_ENVIRONMENT_DISABLED:
      if (!set_str(environment, "kind", "disabled")) goto oom;
      break;
    case QA_ENVIRONMENT_SELECTED: {
      qa_save_value *resource = obj();
      bool ok = resource != NULL &&
                set_str(resource, "content", p.environment.resource.content) &&
                set_str(resource, "path", p.environment.resource.path) &&
                set_str(environment, "kind", "selected") &&
                set_own(environment, "resource", resource);
      if (!ok) goto oom;
      break;
    }
  }
  TRY(set_own(o, "doppler", doppler) &&
      set_own(o, "environment", environment) &&
      set_str(o, "assets", p.assets) &&
      set_own(o, "hud", provider_value(p.hud)) &&
      set_own(o, "effects", provider_value(p.effects)) &&
      set_own(o, "audio", provider_value(p.audio)));
  return o;
oom:
  qa_save_value_free(doppler);
  qa_save_value_free(environment);
  qa_save_value_free(o);
  return NULL;
}
static qa_save_value *mount_plan_value(qa_resolved_mount_plan plan, bool parsed) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *mounts = arr();
  qa_save_value *order = arr();
  qa_save_value *orders = arr();
  if (mounts == NULL || order == NULL || orders == NULL) goto oom;
  for (size_t i = 0; i < plan.mount_count; i++)
    if (!push_own(mounts, mount_value(plan.mounts[i], parsed))) goto oom;
  for (size_t i = 0; i < plan.default_order_count; i++)
    if (!push_own(order, qa_save_value_string(plan.default_order[i]))) goto oom;
  for (size_t i = 0; i < plan.prefix_order_count; i++) {
    qa_save_value *entry = obj();
    qa_save_value *ids = arr();
    bool ok = entry != NULL && ids != NULL;
    for (size_t k = 0; ok && k < plan.prefix_orders[i].mount_count; k++)
      ok = push_own(ids, qa_save_value_string(plan.prefix_orders[i].mounts[k]));
    ok = ok && set_str(entry, "prefix", plan.prefix_orders[i].prefix) &&
         set_own(entry, "mounts", ids) && push_own(orders, entry);
    if (!ok) {
      qa_save_value_free(ids);
      goto oom;
    }
  }
  TRY(set_str(o, "id", plan.id) && set_own(o, "mounts", mounts) &&
      set_own(o, "defaultOrder", order) && set_own(o, "prefixOrders", orders));
  return o;
oom:
  qa_save_value_free(mounts);
  qa_save_value_free(order);
  qa_save_value_free(orders);
  qa_save_value_free(o);
  return NULL;
}
static qa_save_value *behavior_callback_value(qa_weapon_behavior_callback cb) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (cb.kind) {
    case QA_WEAPON_CALLBACK_QUAKEC:
      TRY(set_str(o, "kind", "quakec") &&
          set_own(o, "module", module_value(cb.module)) &&
          set_num(o, "functionIndex", cb.function_index));
      break;
    case QA_WEAPON_CALLBACK_QVM:
      TRY(set_str(o, "kind", "qvm") &&
          set_own(o, "module", module_value(cb.module)) &&
          set_num(o, "instructionIndex", cb.instruction_index));
      break;
    case QA_WEAPON_CALLBACK_NATIVE_ARTIFACT:
      TRY(set_str(o, "kind", "native-artifact") &&
          set_own(o, "module", module_value(cb.module)) &&
          set_own(o, "imageOffset", qa_save_value_bigint_i64(cb.image_offset)) &&
          set_own(o, "abi", abi_value(cb.abi)));
      break;
  }
  return o;
}
/* WeaponBehaviorDefinition { id, title, module, role, aspect, fire, activate }. */
static qa_save_value *behavior_definition_value(const qa_weapon_behavior_definition *definition) {
  qa_save_value *value = obj();
  if (value == NULL) return NULL;
  bool ok = set_str(value, "id", definition->id) &&
            set_str(value, "title", definition->title) &&
            set_own(value, "module", module_value(definition->module)) &&
            set_str(value, "role", definition->role) &&
            set_str(value, "aspect", definition->aspect) &&
            set_own(value, "fire", behavior_callback_value(definition->fire)) &&
            (definition->has_activate
                 ? set_own(value, "activate",
                           behavior_callback_value(definition->activate))
                 : set_null(value, "activate"));
  if (!ok) {
    qa_save_value_free(value);
    return NULL;
  }
  return value;
}
static qa_save_value *weapon_behavior_value(
    qa_resolved_weapon_behavior_selection behavior) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *value = behavior_definition_value(behavior.definition);
  if (value == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  TRY(set_own(o, "source", provider_value(behavior.source)) &&
      set_own(o, "artifact", resource_value(behavior.artifact)));
  if (behavior.has_component) {
    qa_save_value *component = obj();
    if (component == NULL) {
      qa_save_value_free(value);
      qa_save_value_free(o);
      return NULL;
    }
    bool component_ok;
    if (behavior.component_is_qvm) {
      const qa_qvm_weapon_behavior_layout *layout = behavior.component_layout;
      qa_save_value *layout_value_obj = obj();
      qa_save_value *fields = obj();
      component_ok = layout_value_obj != NULL && fields != NULL &&
                     set_num(fields, "inuse", layout->field_inuse) &&
                     set_num(fields, "nextthink", layout->field_nextthink) &&
                     set_num(fields, "think", layout->field_think) &&
                     set_num(fields, "health", layout->field_health) &&
                     set_num(layout_value_obj, "entityStride",
                             layout->entity_stride) &&
                     set_num(layout_value_obj, "levelTime", layout->level_time) &&
                     set_num(layout_value_obj, "allocate", layout->allocate) &&
                     set_num(layout_value_obj, "free", layout->free) &&
                     set_own(layout_value_obj, "fields", fields) &&
                     set_str(layout_value_obj, "fireAbi", layout->fire_abi) &&
                     set_str(component, "kind", "qvm") &&
                     set_str(component, "abiProfile",
                             behavior.component_abi_profile) &&
                     set_own(component, "layout", layout_value_obj);
      if (!component_ok) qa_save_value_free(fields);
    } else {
      char *json = qa_compat_q2_serialize_native_weapon_declaration(
          behavior.component_declaration);
      qa_save_value *declaration =
          json == NULL ? NULL
                       : qa_parse_json_value((const uint8_t *)json,
                                             strlen(json), false);
      free(json);
      component_ok =
          declaration != NULL && set_str(component, "kind", "rerelease-native") &&
          set_own(component, "declaration", declaration);
    }
    if (!component_ok || !set_own(o, "component", component)) {
      qa_save_value_free(component);
      qa_save_value_free(value);
      qa_save_value_free(o);
      return NULL;
    }
  }
  TRY(set_own(o, "definition", value));
  return o;
}
static qa_save_value *selection_value(qa_mod_selection selection) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "product", selection.product) &&
      set_str(o, "id", selection.id));
  return o;
}
static qa_save_value *selections_value(const qa_mod_selection *rows, size_t count) {
  qa_save_value *a = arr();
  if (a == NULL) return NULL;
  for (size_t i = 0; i < count; i++)
    if (!push_own(a, selection_value(rows[i]))) {
      qa_save_value_free(a);
      return NULL;
    }
  return a;
}
/* The TS ResolvedGameplayMod object as the image holds it: discoverGameplayMods'
 * key order, or readGameplayMod's for a mod read back from a save. */
static qa_save_value *gameplay_mod_value(const qa_resolved_gameplay_mod *mod) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_own(o, "selection", selection_value(mod->selection)));
  if (mod->read_order)
    TRY(set_own(o, "source", provider_value(mod->source)) && set_str(o, "title", mod->title) &&
        set_str(o, "sourceTitle", mod->source_title));
  else
    TRY(set_str(o, "title", mod->title) && set_str(o, "sourceTitle", mod->source_title) &&
        set_own(o, "source", provider_value(mod->source)));
  TRY(set_own(o, "requires", selections_value(mod->requires, mod->requires_count)) &&
      set_own(o, "conflicts", selections_value(mod->conflicts, mod->conflicts_count)) &&
      set_own(o, "declaration", qa_gameplay_mod_declaration_value(mod->declaration)) &&
      set_str(o, "declarationDigest", mod->declaration_digest));
  return o;
}
static qa_save_value *recipe_value(const qa_executable_recipe *recipe) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  if (recipe->mod_count > 0) {
    qa_save_value *mods = arr();
    if (mods == NULL) goto oom;
    for (size_t i = 0; i < recipe->mod_count; i++)
      if (!push_own(mods, gameplay_mod_value(recipe->mods[i]))) {
        qa_save_value_free(mods);
        goto oom;
      }
    TRY(set_own(o, "mods", mods));
  }
  if (recipe->weapon_behavior_count > 0) {
    qa_save_value *behaviors = arr();
    if (behaviors == NULL) goto oom;
    for (size_t i = 0; i < recipe->weapon_behavior_count; i++)
      if (!push_own(behaviors,
                    weapon_behavior_value(recipe->weapon_behaviors[i]))) {
        qa_save_value_free(behaviors);
        goto oom;
      }
    TRY(set_own(o, "weaponBehaviors", behaviors));
  }
  {
    qa_save_value *map = obj();
    bool ok = map != NULL &&
              set_str(map, "geometryContent", recipe->map.geometry_content) &&
              set_own(map, "geometry", resource_value(recipe->map.geometry)) &&
              set_own(map, "entities", provider_value(recipe->map.entities));
    if (!ok) {
      qa_save_value_free(map);
      goto oom;
    }
    qa_save_value *campaign = obj();
    ok = campaign != NULL &&
         (recipe->campaign.is_campaign
              ? set_str(campaign, "kind", "campaign") &&
                    set_own(campaign, "mission",
                            provider_value(recipe->campaign.mission)) &&
                    set_own(campaign, "gamecode",
                            provider_value(recipe->campaign.gamecode))
              : set_str(campaign, "kind", "none"));
    if (!ok) {
      qa_save_value_free(campaign);
      qa_save_value_free(map);
      goto oom;
    }
    qa_save_value *weapons = arr();
    ok = weapons != NULL;
    for (size_t i = 0; ok && i < recipe->weapons.count; i++)
      ok = push_own(weapons, provider_value(recipe->weapons.values[i]));
    if (!ok) {
      qa_save_value_free(weapons);
      qa_save_value_free(campaign);
      qa_save_value_free(map);
      goto oom;
    }
    qa_save_value *execution = arr();
    ok = execution != NULL;
    for (size_t i = 0; ok && i < recipe->execution_count; i++)
      ok = push_own(execution, execution_value(recipe->execution[i]));
    if (!ok) {
      qa_save_value_free(execution);
      qa_save_value_free(weapons);
      qa_save_value_free(campaign);
      qa_save_value_free(map);
      goto oom;
    }
    qa_save_value *resources = arr();
    ok = resources != NULL;
    for (size_t i = 0; ok && i < recipe->resource_count; i++)
      ok = push_own(resources, resource_value(recipe->resources[i]));
    if (!ok) {
      qa_save_value_free(resources);
      qa_save_value_free(execution);
      qa_save_value_free(weapons);
      qa_save_value_free(campaign);
      qa_save_value_free(map);
      goto oom;
    }
    qa_save_value *timing = arr();
    ok = timing != NULL;
    for (size_t i = 0; ok && i < recipe->timing_count; i++) {
      qa_save_value *entry = obj();
      ok = entry != NULL &&
           set_str(entry, "provider", recipe->timing[i].provider) &&
           /* Catalog timing (nativeProviderTiming, equipment) is
            * { provider, numeric, clock }; recipe.ts readRecipe reads
            * { provider, clock, numeric }. */
           (recipe->launch_key_order ||
            set_own(entry, "clock", clock_value(recipe->timing[i].clock))) &&
           set_own(entry, "numeric", numeric_value(recipe->timing[i].numeric)) &&
           (!recipe->launch_key_order ||
            set_own(entry, "clock", clock_value(recipe->timing[i].clock))) &&
           push_own(timing, entry);
    }
    if (!ok) {
      qa_save_value_free(timing);
      qa_save_value_free(resources);
      qa_save_value_free(execution);
      qa_save_value_free(weapons);
      qa_save_value_free(campaign);
      qa_save_value_free(map);
      goto oom;
    }
    if (recipe->launch_key_order) {
      TRY(set_str(o, "id", recipe->id) &&
          set_own(o, "map", map) && set_own(o, "campaign", campaign) &&
          set_own(o, "movement", provider_value(recipe->movement)) &&
          set_own(o, "character", character_value(recipe->character)) &&
          set_own(o, "weapons", weapons) &&
          set_own(o, "equipment", equipment_value(recipe->equipment)) &&
          set_own(o, "enemies", enemies_value(recipe->enemies)) &&
          set_own(o, "presentation", presentation_value(recipe->presentation)) &&
          set_own(o, "engineBehavior", provider_value(recipe->engine_behavior)) &&
          set_own(o, "combat", provider_value(recipe->combat)) &&
          set_own(o, "inventory", provider_value(recipe->inventory)) &&
          set_own(o, "match", provider_value(recipe->match)) &&
          set_own(o, "transition", provider_value(recipe->transition)) &&
          set_own(o, "execution", execution) && set_own(o, "timing", timing) &&
          set_own(o, "ordering", ordering_value(recipe->ordering)) &&
          set_str(o, "preset", recipe->preset) &&
          set_num(o, "schemaVersion", (double)recipe->schema_version) &&
          set_own(o, "mounts", mount_plan_value(recipe->mounts, !recipe->launch_key_order)) &&
          set_own(o, "resources", resources));
      return o;
    }
    TRY(set_num(o, "schemaVersion", (double)recipe->schema_version) &&
        set_str(o, "id", recipe->id) && set_str(o, "preset", recipe->preset) &&
        set_own(o, "map", map) && set_own(o, "campaign", campaign) &&
        set_own(o, "movement", provider_value(recipe->movement)) &&
        set_own(o, "character", character_value(recipe->character)) &&
        set_own(o, "weapons", weapons) &&
        set_own(o, "equipment", equipment_value(recipe->equipment)) &&
        set_own(o, "enemies", enemies_value(recipe->enemies)) &&
        set_own(o, "presentation", presentation_value(recipe->presentation)) &&
        set_own(o, "engineBehavior",
                provider_value(recipe->engine_behavior)) &&
        set_own(o, "combat", provider_value(recipe->combat)) &&
        set_own(o, "inventory", provider_value(recipe->inventory)) &&
        set_own(o, "match", provider_value(recipe->match)) &&
        set_own(o, "transition", provider_value(recipe->transition)) &&
        set_own(o, "execution", execution) &&
        set_own(o, "mounts", mount_plan_value(recipe->mounts, !recipe->launch_key_order)) &&
        set_own(o, "resources", resources) && set_own(o, "timing", timing) &&
        set_own(o, "ordering", ordering_value(recipe->ordering)));
  }
  return o;
oom:
  qa_save_value_free(o);
  return NULL;
}
static qa_save_value *saved_body_value(qa_saved_body_state body) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_own(o, "origin", vector_value(body.origin)) &&
      set_own(o, "angles", vector_value(body.angles)) &&
      set_own(o, "velocity", vector_value(body.velocity)) &&
      set_own(o, "bounds", bounds_value(body.bounds)) &&
      (body.ground != NULL ? set_own(o, "ground", actor_value(*body.ground))
                           : set_null(o, "ground")));
  return o;
}
static qa_save_value *regular_armor_value(qa_regular_armor_state armor) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  switch (armor.kind) {
    case QA_REGULAR_ARMOR_NONE:
      TRY(set_str(o, "kind", "none"));
      break;
    case QA_REGULAR_ARMOR_SOURCE:
      TRY(set_str(o, "kind", "source") && set_num(o, "points", armor.points) &&
          (armor.item != NULL ? set_str(o, "item", armor.item)
                              : set_null(o, "item")));
      break;
    case QA_REGULAR_ARMOR_Q1:
      TRY(set_str(o, "kind", "q1") && set_num(o, "points", armor.points) &&
          set_num(o, "absorption", armor.absorption) &&
          set_str(o, "item", armor.item));
      break;
    case QA_REGULAR_ARMOR_Q3:
      TRY(set_str(o, "kind", "q3") && set_num(o, "points", armor.points) &&
          set_num(o, "protection", armor.protection));
      break;
    case QA_REGULAR_ARMOR_Q2:
      TRY(set_str(o, "kind", "q2") && set_num(o, "points", armor.points) &&
          set_num(o, "normalProtection", armor.normal_protection) &&
          set_num(o, "energyProtection", armor.energy_protection) &&
          set_str(o, "item", armor.item));
      break;
  }
  return o;
}
static qa_save_value *armor_value(qa_armor_state armor) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *powered = obj();
  if (powered == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  bool ok = set_str(powered, "kind", armor.powered.kind) &&
            (strcmp(armor.powered.kind, "none") == 0 ||
             set_num(powered, "cells", armor.powered.cells));
  if (!ok) {
    qa_save_value_free(powered);
    qa_save_value_free(o);
    return NULL;
  }
  TRY(set_own(o, "regular", regular_armor_value(armor.regular)) &&
      set_own(o, "powered", powered));
  return o;
}
static qa_save_value *combat_value(qa_combat_state state) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_num(o, "health", state.health) &&
      set_own(o, "armor", armor_value(state.armor)) &&
      set_num(o, "mass", state.mass) &&
      set_bool(o, "canTakeDamage", state.can_take_damage) &&
      set_bool(o, "invulnerable", state.invulnerable) &&
      (state.team != NULL ? set_str(o, "team", state.team)
                          : set_null(o, "team")) &&
      (!state.has_no_knockback ||
       set_bool(o, "noKnockback", state.no_knockback)));
  return o;
}
static qa_save_value *inventory_entry_value(qa_inventory_entry entry) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "item", entry.item) && set_num(o, "count", entry.count) &&
      set_num(o, "capacity", entry.capacity));
  if (entry.has_count_policy) {
    qa_save_value *policy = obj();
    bool ok = policy != NULL &&
              set_str(policy, "kind", entry.count_policy.kind) &&
              (strcmp(entry.count_policy.kind, "source-counter") != 0 ||
               set_str(policy, "arithmetic", entry.count_policy.arithmetic)) &&
              set_own(o, "countPolicy", policy);
    if (!ok) {
      qa_save_value_free(policy);
      qa_save_value_free(o);
      return NULL;
    }
  }
  return o;
}
static qa_save_value *provider_checkpoint_value(qa_provider_checkpoint cp) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  TRY(set_str(o, "provider", cp.provider) && set_str(o, "schema", cp.schema) &&
      set_num(o, "version", cp.version) && set_bytes(o, "bytes", cp.bytes));
  return o;
}
static qa_save_value *mod_identity_value(qa_mod_identity identity) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *modules = arr();
  qa_save_value *providers = arr();
  if (modules == NULL || providers == NULL) goto oom;
  for (size_t i = 0; i < identity.module_count; i++)
    if (!push_own(modules, module_value(identity.modules[i]))) goto oom;
  for (size_t i = 0; i < identity.provider_count; i++) {
    qa_save_value *entry = obj();
    bool ok = entry != NULL &&
              set_str(entry, "provider", identity.providers[i].provider) &&
              set_str(entry, "schema", identity.providers[i].schema) &&
              set_num(entry, "version", identity.providers[i].version) &&
              push_own(providers, entry);
    if (!ok) goto oom;
  }
  TRY(set_own(o, "selection", selection_value(identity.selection)) &&
      set_own(o, "source", provider_value(identity.source)) &&
      set_str(o, "declarationDigest", identity.declaration_digest) &&
      set_own(o, "modules", modules) && set_own(o, "providers", providers));
  return o;
oom:
  qa_save_value_free(modules);
  qa_save_value_free(providers);
  qa_save_value_free(o);
  return NULL;
}
static qa_save_value *mod_session_value(
    const qa_mod_session_checkpoint *session) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  qa_save_value *mods = arr();
  if (mods == NULL) {
    qa_save_value_free(o);
    return NULL;
  }
  for (size_t i = 0; i < session->mod_count; i++) {
    const qa_mod_checkpoint *checkpoint = &session->mods[i];
    qa_save_value *state = obj();
    qa_save_value *guests = arr();
    qa_save_value *providers = arr();
    bool ok = state != NULL && guests != NULL && providers != NULL;
    for (size_t k = 0; ok && k < checkpoint->state.guest_count; k++)
      ok = push_own(guests, guest_value(&checkpoint->state.guests[k]));
    for (size_t k = 0; ok && k < checkpoint->state.provider_count; k++)
      ok = push_own(providers, provider_checkpoint_value(
                                   checkpoint->state.providers[k]));
    ok = ok && set_own(state, "guests", guests) &&
         set_own(state, "providers", providers);
    if (!ok) {
      qa_save_value_free(state);
      qa_save_value_free(guests);
      qa_save_value_free(providers);
      qa_save_value_free(mods);
      qa_save_value_free(o);
      return NULL;
    }
    qa_save_value *entry = obj();
    ok = entry != NULL &&
         set_own(entry, "identity", mod_identity_value(checkpoint->identity)) &&
         set_own(entry, "state", state) && push_own(mods, entry);
    if (!ok) {
      qa_save_value_free(mods);
      qa_save_value_free(o);
      return NULL;
    }
  }
  TRY(set_num(o, "version", (double)session->version) &&
      set_own(o, "mods", mods));
  return o;
}
static qa_save_value *consumption_value(const qa_save_image *image) {
  qa_save_value *list = arr();
  if (list == NULL) return NULL;
  for (size_t i = 0; i < image->consumption_count; i++) {
    const qa_selection_consumption_entry *entry = &image->consumption[i];
    qa_save_value *o = obj();
    bool ok = o != NULL && set_str(o, "axis", entry->axis) &&
              set_str(o, "requested", entry->requested) &&
              set_str(o, "outcome", entry->outcome) &&
              set_str(o, "detail", entry->detail);
    if (!ok || !push_own(list, o)) {
      if (!ok) qa_save_value_free(o);
      qa_save_value_free(list);
      return NULL;
    }
  }
  return list;
}
static qa_save_value *image_value(const qa_save_image *image) {
  qa_save_value *o = obj();
  if (o == NULL) return NULL;
  bool parsed = image->key_order == QA_SAVE_IMAGE_ORDER_PARSED;
  TRY(set_num(o, "schemaVersion", (double)image->schema_version) &&
      (!image->legacy_armor_layout ||
       set_bool(o, "legacyArmorLayout", true)) &&
      set_own(o, "recipe", recipe_value(&image->recipe)) &&
      set_own(o, "frame", frame_value(image->frame)) &&
      set_num(o, "nextEventSequence", image->next_event_sequence));
  if (!parsed) {
    if (image->mods != NULL) TRY(set_own(o, "mods", mod_session_value(image->mods)));
    TRY(set_own(o, "consumption", consumption_value(image)));
  }
  {
    qa_save_value *clocks = arr();
    if (clocks == NULL) goto oom;
    for (size_t i = 0; i < image->clock_count; i++) {
      qa_save_value *entry = obj();
      bool ok = entry != NULL &&
                set_str(entry, "provider", image->clocks[i].provider) &&
                set_own(entry, "time", time_value(image->clocks[i].time)) &&
                push_own(clocks, entry);
      if (!ok) {
        qa_save_value_free(clocks);
        goto oom;
      }
    }
    TRY(set_own(o, "clocks", clocks));
  }
  {
    qa_save_value *random = arr();
    if (random == NULL) goto oom;
    for (size_t i = 0; i < image->random_count; i++) {
      qa_save_value *entry = obj();
      bool ok = entry != NULL &&
                set_str(entry, "provider", image->random[i].provider) &&
                set_own(entry, "state", random_value(image->random[i].state)) &&
                push_own(random, entry);
      if (!ok) {
        qa_save_value_free(random);
        goto oom;
      }
    }
    TRY(set_own(o, "random", random));
  }
  {
    qa_save_value *actors = arr();
    if (actors == NULL) goto oom;
    for (size_t i = 0; i < image->actor_count; i++) {
      const qa_actor_slot_checkpoint *slot = &image->actors[i];
      qa_save_value *entry = obj();
      qa_save_value *lifetime = obj();
      bool ok = entry != NULL && lifetime != NULL &&
                set_num(entry, "slot", slot->id.slot) &&
                set_num(entry, "generation", slot->id.generation);
      if (ok && slot->lifetime_is_active)
        ok = set_str(lifetime, "kind", "active") &&
             set_str(lifetime, "owner", slot->owner) &&
             set_str(lifetime, "definition", slot->definition);
      else if (ok)
        ok = set_str(lifetime, "kind", "free") &&
             (slot->freed_at != NULL
                  ? set_own(lifetime, "freedAt", time_value(*slot->freed_at))
                  : set_null(lifetime, "freedAt"));
      ok = ok && set_own(entry, "lifetime", lifetime) &&
           push_own(actors, entry);
      if (!ok) {
        qa_save_value_free(actors);
        goto oom;
      }
    }
    TRY(set_own(o, "actors", actors));
  }
  {
    qa_save_value *bodies = arr();
    if (bodies == NULL) goto oom;
    for (size_t i = 0; i < image->body_count; i++) {
      const qa_body_checkpoint *body = &image->bodies[i];
      qa_save_value *entry = obj();
      bool ok = entry != NULL &&
                set_own(entry, "actor", actor_value(body->actor)) &&
                set_own(entry, "body", saved_body_value(body->body));
      /* captureSharedBodies writes linkCount before attachment; readSaveImage after. */
      if (!parsed) ok = ok && set_num(entry, "linkCount", body->link_count);
      if (ok && body->attachment != NULL) {
        qa_save_value *attachment = obj();
        qa_save_value *follow = obj();
        const char *kind =
            body->attachment->follow_kind == QA_BODY_FOLLOW_TRANSLATION
                ? "translation"
            : body->attachment->follow_kind == QA_BODY_FOLLOW_CENTER
                ? "center"
                : "bounds-min";
        ok = attachment != NULL && follow != NULL &&
             set_str(follow, "kind", kind) &&
             (body->attachment->follow_kind == QA_BODY_FOLLOW_CENTER ||
              set_own(follow, "offset",
                      vector_value(body->attachment->follow_offset))) &&
             set_own(attachment, "anchor",
                     actor_value(body->attachment->anchor)) &&
             set_own(attachment, "follow", follow) &&
             set_own(entry, "attachment", attachment);
        if (!ok) {
          qa_save_value_free(bodies);
          goto oom;
        }
      } else if (ok) {
        ok = set_null(entry, "attachment");
      }
      if (parsed) ok = ok && set_num(entry, "linkCount", body->link_count);
      if (ok && body->linked != NULL) {
        qa_save_value *linked = obj();
        ok = linked != NULL &&
             set_own(linked, "state", saved_body_value(body->linked->state)) &&
             set_own(linked, "absoluteBounds",
                     bounds_value(body->linked->absolute_bounds)) &&
             set_own(entry, "linked", linked);
      } else if (ok) {
        ok = set_null(entry, "linked");
      }
      ok = ok && push_own(bodies, entry);
      if (!ok) {
        qa_save_value_free(bodies);
        goto oom;
      }
    }
    TRY(set_own(o, "bodies", bodies));
  }
  {
    qa_save_value *combat = arr();
    if (combat == NULL) goto oom;
    for (size_t i = 0; i < image->combat_count; i++) {
      qa_save_value *entry = obj();
      bool ok = entry != NULL &&
                set_own(entry, "actor", actor_value(image->combat[i].actor)) &&
                set_own(entry, "state", combat_value(image->combat[i].state)) &&
                push_own(combat, entry);
      if (!ok) {
        qa_save_value_free(combat);
        goto oom;
      }
    }
    TRY(set_own(o, "combat", combat));
  }
  {
    qa_save_value *inventories = arr();
    if (inventories == NULL) goto oom;
    for (size_t i = 0; i < image->inventory_count; i++) {
      qa_save_value *entry = obj();
      qa_save_value *entries = arr();
      bool ok = entry != NULL && entries != NULL;
      for (size_t k = 0; ok && k < image->inventories[i].entry_count; k++)
        ok = push_own(entries, inventory_entry_value(
                                   image->inventories[i].entries[k]));
      ok = ok &&
           set_own(entry, "actor", actor_value(image->inventories[i].actor)) &&
           set_own(entry, "entries", entries) && push_own(inventories, entry);
      if (!ok) {
        qa_save_value_free(inventories);
        goto oom;
      }
    }
    TRY(set_own(o, "inventories", inventories));
  }
  {
    qa_save_value *configurations = arr();
    if (configurations == NULL) goto oom;
    for (size_t i = 0; i < image->configuration_count; i++) {
      const qa_actor_configuration_checkpoint *config =
          &image->configurations[i];
      qa_save_value *entry = obj();
      qa_save_value *weapons = arr();
      bool ok = entry != NULL && weapons != NULL;
      for (size_t k = 0; ok && k < config->weapons.count; k++)
        ok = push_own(weapons, provider_value(config->weapons.values[k]));
      ok = ok && set_own(entry, "actor", actor_value(config->actor)) &&
           set_own(entry, "movement", provider_value(config->movement)) &&
           set_own(entry, "character", character_value(config->character)) &&
           set_own(entry, "weapons", weapons) &&
           set_own(entry, "inventory", provider_value(config->inventory)) &&
           push_own(configurations, entry);
      if (!ok) {
        qa_save_value_free(configurations);
        goto oom;
      }
    }
    TRY(set_own(o, "configurations", configurations));
  }
  {
    static const char *const boundaries[] = {"before-physics", "during-physics",
                                             "after-physics"};
    qa_save_value *thinks = arr();
    if (thinks == NULL) goto oom;
    for (size_t i = 0; i < image->think_count; i++) {
      const qa_think_checkpoint *think = &image->thinks[i];
      qa_save_value *entry = obj();
      /* checkpointFromSource spreads executionProvider after callback;
       * parseSaveImage appends it after sequence. */
      bool ok = entry != NULL &&
                set_own(entry, "actor", actor_value(think->actor)) &&
                set_str(entry, "callback", think->callback) &&
                (parsed || !think->has_execution_provider ||
                 set_str(entry, "executionProvider",
                         think->execution_provider)) &&
                set_own(entry, "due", time_value(think->due)) &&
                set_str(entry, "boundary", boundaries[think->boundary]) &&
                set_str(entry, "provider", think->provider) &&
                set_num(entry, "sequence", think->sequence) &&
                (!parsed || !think->has_execution_provider ||
                 set_str(entry, "executionProvider",
                         think->execution_provider)) &&
                push_own(thinks, entry);
      if (!ok) {
        qa_save_value_free(thinks);
        goto oom;
      }
    }
    TRY(set_own(o, "thinks", thinks));
  }
  {
    qa_save_value *providers = arr();
    if (providers == NULL) goto oom;
    for (size_t i = 0; i < image->provider_count; i++)
      if (!push_own(providers,
                    provider_checkpoint_value(image->providers[i]))) {
        qa_save_value_free(providers);
        goto oom;
      }
    TRY(set_own(o, "providers", providers));
  }
  {
    qa_save_value *guests = arr();
    if (guests == NULL) goto oom;
    for (size_t i = 0; i < image->guest_count; i++) {
      qa_save_value *guest = guest_value(&image->guests[i]);
      if (!parsed) order_guest_checkpoint(guest, image->guests[i].kind);
      if (!push_own(guests, guest)) {
        qa_save_value_free(guests);
        goto oom;
      }
    }
    TRY(set_own(o, "guests", guests));
  }
  if (parsed) {
    if (image->mods != NULL) TRY(set_own(o, "mods", mod_session_value(image->mods)));
    TRY(set_own(o, "consumption", consumption_value(image)));
  }
  return o;
oom:
  qa_save_value_free(o);
  return NULL;
}
#undef TRY
/* TS saveHeaderOf + encodeSaveImage: magic, u32 LE header length, the
 * header record {map, content}, then the image payload. */
bool qa_encode_save_image(const qa_save_image *image, uint8_t **out_bytes,
                          size_t *out_length) {
  *out_bytes = NULL;
  *out_length = 0;
  qa_save_value *header = obj();
  if (header == NULL) return false;
  if (!set_str(header, "map", image->recipe.map.geometry.requested_path) ||
      !set_str(header, "content", image->recipe.map.entities.content)) {
    qa_save_value_free(header);
    return false;
  }
  size_t header_size = 0;
  uint8_t *header_bytes = qa_encode_checkpoint_value(header, &header_size);
  qa_save_value_free(header);
  if (header_bytes == NULL) return false;
  qa_save_value *value = image_value(image);
  if (value == NULL) {
    free(header_bytes);
    return false;
  }
  size_t payload_length = 0;
  uint8_t *payload = qa_encode_checkpoint_value(value, &payload_length);
  qa_save_value_free(value);
  if (payload == NULL) {
    free(header_bytes);
    return false;
  }
  size_t total = SAVE_MAGIC_LENGTH + 4 + header_size + payload_length;
  uint8_t *output = malloc(total);
  if (output == NULL) {
    free(header_bytes);
    free(payload);
    return false;
  }
  memcpy(output, SAVE_MAGIC, SAVE_MAGIC_LENGTH);
  /* TS storeU32LE(output, 8, header.length): the length wraps modulo 2^32. */
  uint32_t stored = (uint32_t)header_size;
  output[8] = (uint8_t)stored;
  output[9] = (uint8_t)(stored >> 8);
  output[10] = (uint8_t)(stored >> 16);
  output[11] = (uint8_t)(stored >> 24);
  memcpy(output + SAVE_MAGIC_LENGTH + 4, header_bytes, header_size);
  memcpy(output + SAVE_MAGIC_LENGTH + 4 + header_size, payload, payload_length);
  free(header_bytes);
  free(payload);
  *out_bytes = output;
  *out_length = total;
  return true;
}

/* TS persistence of a GuestCheckpoint value (the guest_value encoder). */
qa_save_value *qa_save_guest_checkpoint_value(const qa_guest_checkpoint *guest) { return guest_value(guest); }

/* Public value builders over the encoder's writers (the TS plain records
 * other checkpoint codecs embed). NULL only on OOM. */
qa_save_value *qa_save_actor_value(qa_saved_actor_id actor) { return actor_value(actor); }
qa_save_value *qa_save_vector_value(qa_vec3 vector) { return vector_value(vector); }
qa_save_value *qa_save_bounds_value(qa_bounds bounds) { return bounds_value(bounds); }
qa_save_value *qa_save_time_value(qa_source_time time) { return time_value(time); }
qa_save_value *qa_save_frame_value(qa_frame_context frame) { return frame_value(frame); }
qa_save_value *qa_save_random_value(qa_random_state state) { return random_value(state); }
qa_save_value *qa_save_provider_value(qa_provider_reference provider) { return provider_value(provider); }
qa_save_value *qa_save_armor_value(qa_armor_state armor) { return armor_value(armor); }
qa_save_value *qa_save_combat_value(qa_combat_state state) { return combat_value(state); }
qa_save_value *qa_save_inventory_entry_value(qa_inventory_entry entry) { return inventory_entry_value(entry); }
qa_save_value *qa_save_recipe_value(const qa_executable_recipe *recipe) { return recipe_value(recipe); }
qa_save_value *qa_save_weapon_behavior_definition_value(const qa_weapon_behavior_definition *definition) {
  return behavior_definition_value(definition);
}
qa_save_value *qa_save_guest_value(const qa_guest_checkpoint *guest) { return guest_value(guest); }
/* The saved form of a ModIdentity (owned tree; NULL only on OOM). */
qa_save_value *qa_save_mod_identity_value(const qa_mod_identity *identity) { return mod_identity_value(*identity); }
