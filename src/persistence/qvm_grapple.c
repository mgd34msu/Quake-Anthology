/* Port of src/persistence/qvm-grapple.ts. GPL-2.0-or-later. */
#include "qa/persistence/qvm_grapple.h"

#include <stdlib.h>
#include <string.h>

#include "qa/persistence/execution.h"
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
static const char *field_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static const char *field_choice(const qa_save_reader *reader, const char *name,
                                const char *const *choices, size_t count) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_choice_string(&f, choices, count);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_integer_min(const qa_save_reader *reader, const char *name, double minimum) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return value;
}
static bool field_nullable_integer(const qa_save_reader *reader, const char *name,
                                   double minimum, double *out) {
  qa_save_reader f = field(reader, name);
  bool present = !qa_save_reader_is_null(&f);
  if (present) *out = qa_save_reader_integer_min(&f, minimum);
  qa_save_reader_destroy(&f);
  return present;
}
static const char *field_nullable_string(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  const char *value = qa_save_reader_is_null(&f) ? NULL : qa_save_reader_string(&f);
  qa_save_reader_destroy(&f);
  return value;
}
static double field_finite(const qa_save_reader *reader, const char *name) {
  qa_save_reader f = field(reader, name);
  double value = qa_save_reader_finite(&f);
  qa_save_reader_destroy(&f);
  return value;
}

qa_qvm_grapple_definition *qa_read_qvm_grapple_definition(const qa_save_reader *reader) {
  static const char *const profiles[] = {"q3-modern", "q3-1.16n-base"};
  static const char *const cable_kinds[] = {"shader", "model"};
  qa_qvm_grapple_definition *definition =
      (qa_qvm_grapple_definition *)calloc(1, sizeof *definition);
  if (definition == NULL) return NULL;
  qa_save_reader fields = field(reader, "fields");
  qa_save_reader globals = field(reader, "globals");
  qa_save_reader callbacks = field(reader, "callbacks");
  qa_save_reader movement = field(reader, "movement");
  qa_save_reader cvars = field(reader, "initialCvars");
  const qa_save_value *cvar_value = cvars.value;
  if (cvar_value == NULL || cvar_value->kind != QA_SAVE_OBJECT)
    qa_save_reader_fail(&cvars, "Expected source settings");
  qa_save_reader presentation = field(reader, "presentation");
  qa_save_reader cable = field(&presentation, "cable");
  const char *cable_kind = field_choice(&cable, "kind", cable_kinds, 2);
  qa_save_reader anchor = field(&presentation, "viewAnchor");

  definition->id = field_string(reader, "id");
  definition->title = field_string(reader, "title");
  {
    qa_save_reader module = field(reader, "module");
    definition->module = qa_read_module(&module);
    qa_save_reader_destroy(&module);
  }
  definition->abi_profile = field_choice(reader, "abiProfile", profiles, 2);
  definition->entity_stride = field_integer_min(reader, "entityStride", 1);
  definition->client_stride = field_integer_min(reader, "clientStride", 1);
  definition->pulling_flag = field_integer_min(reader, "pullingFlag", 1);
  definition->fields.inuse = field_integer_min(&fields, "inuse", 0);
  definition->fields.client = field_integer_min(&fields, "client", 0);
  definition->fields.parent = field_integer_min(&fields, "parent", 0);
  definition->fields.target = field_integer_min(&fields, "target", 0);
  definition->fields.has_mover =
      field_nullable_integer(&fields, "mover", 0, &definition->fields.mover);
  definition->fields.hook = field_integer_min(&fields, "hook", 0);
  definition->fields.health = field_integer_min(&fields, "health", 0);
  definition->fields.takedamage = field_integer_min(&fields, "takedamage", 0);
  definition->fields.event_time = field_integer_min(&fields, "eventTime", 0);
  definition->fields.free_after_event = field_integer_min(&fields, "freeAfterEvent", 0);
  definition->globals.time = field_integer_min(&globals, "time", 0);
  definition->globals.frame = field_integer_min(&globals, "frame", 0);
  definition->globals.movement = field_integer_min(&globals, "movement", 0);
  definition->globals.forward = field_integer_min(&globals, "forward", 0);
  definition->globals.ground_plane = field_integer_min(&globals, "groundPlane", 0);
  definition->callbacks.allocate = field_integer_min(&callbacks, "allocate", 1);
  definition->callbacks.free = field_integer_min(&callbacks, "free", 1);
  definition->callbacks.fire = field_integer_min(&callbacks, "fire", 1);
  definition->callbacks.release = field_integer_min(&callbacks, "release", 1);
  definition->callbacks.force_release = field_integer_min(&callbacks, "forceRelease", 1);
  definition->callbacks.missile = field_integer_min(&callbacks, "missile", 1);
  definition->callbacks.has_follow =
      field_nullable_integer(&callbacks, "follow", 1, &definition->callbacks.follow);
  definition->callbacks.think = field_integer_min(&callbacks, "think", 1);
  definition->callbacks.pull = field_integer_min(&callbacks, "pull", 1);
  definition->callbacks.has_move_mover_hooks = field_nullable_integer(
      &callbacks, "moveMoverHooks", 1, &definition->callbacks.move_mover_hooks);
  definition->callbacks.damage = field_integer_min(&callbacks, "damage", 1);
  definition->callbacks.same_team = field_integer_min(&callbacks, "sameTeam", 1);
  definition->callbacks.player_move = field_integer_min(&callbacks, "playerMove", 1);
  {
    qa_save_reader arguments = field(reader, "fireArguments");
    size_t count = qa_save_reader_list_count(&arguments);
    double *values = count > 0 ? (double *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&arguments, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&arguments, i);
      values[i] = qa_save_reader_integer(&entry);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&arguments);
    definition->fire_arguments.values = values;
    definition->fire_arguments.count = count;
  }
  definition->movement.byte_length = field_integer_min(&movement, "byteLength", 4);
  {
    qa_save_reader words = field(&movement, "words");
    size_t count = qa_save_reader_list_count(&words);
    qa_qvm_grapple_movement_word *values =
        count > 0 ? (qa_qvm_grapple_movement_word *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&words, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&words, i);
      values[i].offset = field_integer_min(&entry, "offset", 4);
      qa_save_reader value = field(&entry, "value");
      values[i].value = qa_save_reader_integer(&value);
      qa_save_reader_destroy(&value);
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&words);
    definition->movement.words = values;
    definition->movement.word_count = count;
  }
  {
    size_t count = cvar_value->entry_count;
    qa_qvm_grapple_cvar *values =
        count > 0 ? (qa_qvm_grapple_cvar *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&cvars, "out of memory");
    for (size_t i = 0; i < count; i++) {
      values[i].name = cvar_value->entries[i].key;
      values[i].value = field_string(&cvars, cvar_value->entries[i].key);
    }
    definition->initial_cvars = values;
    definition->initial_cvar_count = count;
  }
  definition->event_lifetime_milliseconds =
      field_integer_min(reader, "eventLifetimeMilliseconds", 1);
  definition->grapple_damage_method = field_integer_min(reader, "grappleDamageMethod", 0);
  definition->presentation.projectile_model = field_string(&presentation, "projectileModel");
  definition->presentation.view_model = field_string(&presentation, "viewModel");
  definition->presentation.weapon_index = field_integer_min(&presentation, "weaponIndex", 1);
  definition->presentation.view_anchor.path = field_string(&anchor, "path");
  definition->presentation.view_anchor.tag = field_string(&anchor, "tag");
  {
    qa_save_reader offset = field(&anchor, "offset");
    definition->presentation.view_anchor.offset = qa_read_vector(&offset);
    qa_save_reader_destroy(&offset);
    qa_save_reader fov = field(&anchor, "fovOffset");
    definition->presentation.view_anchor.fov_offset_above =
        field_integer_min(&fov, "above", 1);
    definition->presentation.view_anchor.fov_offset_scale = field_finite(&fov, "scale");
    qa_save_reader_destroy(&fov);
  }
  {
    qa_save_reader attachments = field(&presentation, "viewAttachments");
    size_t count = qa_save_reader_list_count(&attachments);
    qa_qvm_grapple_view_attachment *values = count > 0
        ? (qa_qvm_grapple_view_attachment *)malloc(count * sizeof *values) : NULL;
    if (count > 0 && values == NULL) qa_save_reader_fail(&attachments, "out of memory");
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry = item(&attachments, i);
      values[i].path = field_string(&entry, "path");
      values[i].tag = field_string(&entry, "tag");
      qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&attachments);
    definition->presentation.view_attachments = values;
    definition->presentation.view_attachment_count = count;
  }
  if (strcmp(cable_kind, "shader") == 0) {
    definition->presentation.cable.is_shader = true;
    definition->presentation.cable.path = field_string(&cable, "path");
    definition->presentation.cable.width = field_integer_min(&cable, "width", 1);
  } else {
    definition->presentation.cable.is_shader = false;
    definition->presentation.cable.flight = field_string(&cable, "flight");
    definition->presentation.cable.pull = field_string(&cable, "pull");
    definition->presentation.cable.hold = field_string(&cable, "hold");
    definition->presentation.cable.segment_length =
        field_integer_min(&cable, "segmentLength", 1);
  }
  definition->presentation.fire_sound = field_nullable_string(&presentation, "fireSound");
  definition->presentation.attach_sound = field_nullable_string(&presentation, "attachSound");
  definition->presentation.release_sound = field_nullable_string(&presentation, "releaseSound");
  definition->presentation.pull_sound = field_nullable_string(&presentation, "pullSound");
  definition->presentation.hang_sound = field_nullable_string(&presentation, "hangSound");
  qa_save_reader_destroy(&fields);
  qa_save_reader_destroy(&globals);
  qa_save_reader_destroy(&callbacks);
  qa_save_reader_destroy(&movement);
  qa_save_reader_destroy(&cvars);
  qa_save_reader_destroy(&cable);
  qa_save_reader_destroy(&anchor);
  qa_save_reader_destroy(&presentation);
  return definition;
}
