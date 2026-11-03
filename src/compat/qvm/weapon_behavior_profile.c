/* See include/qa/compat/qvm/weapon_behavior_profile.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/weapon_behavior_profile.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/operations.h"
#include "qa/compat/qvm/shared_entity_record.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static void set_string(qa_save_value *object, const char *key, const char *value) {
  qa_save_value *child = qa_save_value_string(value);
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("QVM memory allocation failed");
}

static void set_number(qa_save_value *object, const char *key, double value) {
  qa_save_value *child = qa_save_value_number(value);
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("QVM memory allocation failed");
}

static void set_child(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) qa_range_error("QVM memory allocation failed");
}

qa_save_value *qa_qvm_weapon_profile_declaration(const qa_qvm_weapon_profile *profile,
                                                 qa_qvm_abi_profile abi_profile) {
  const qa_weapon_behavior_definition *definition = &profile->definition;
  if (definition->fire.kind != QA_WEAPON_CALLBACK_QVM ||
      (definition->has_activate && definition->activate.kind != QA_WEAPON_CALLBACK_QVM) ||
      strncmp(definition->id, "qvm:", 4) != 0)
    qa_range_error("Invalid QVM weapon callback identity");
  qa_save_value *result = qa_save_value_object();
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  set_number(result, "version", 1);
  set_string(result, "artifactDigest", definition->module.digest);
  set_string(result, "artifactPath", definition->module.artifact_path);
  set_string(result, "abiProfile", abi_profile);
  set_string(result, "id", definition->id + 4);
  set_string(result, "title", definition->title);
  set_string(result, "role", definition->role);
  set_string(result, "aspect", definition->aspect);
  set_number(result, "fireFunction", definition->fire.instruction_index);
  if (definition->has_activate) set_number(result, "activationFunction", definition->activate.instruction_index);
  else set_child(result, "activationFunction", qa_save_value_null());
  set_number(result, "entityStride", (double)profile->entity_stride);
  set_number(result, "levelTime", (double)profile->level_time);
  set_number(result, "allocateFunction", (double)profile->allocate);
  set_number(result, "freeFunction", (double)profile->free_entry);
  qa_save_value *fields = qa_save_value_object();
  set_number(fields, "inuse", (double)profile->field_inuse);
  set_number(fields, "nextthink", (double)profile->field_nextthink);
  set_number(fields, "think", (double)profile->field_think);
  set_number(fields, "health", (double)profile->field_health);
  set_child(result, "fields", fields);
  set_string(result, "fireAbi", profile->fire_abi);
  return result;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static int64_t read_entry(const qa_resolved_qvm_artifact *artifact, qa_save_reader *reader) {
  int64_t index = (int64_t)qa_save_reader_integer_min(reader, 1);
  if (index < 0 || (uint64_t)index >= artifact->image.instruction_count ||
      artifact->image.instructions[index].opcode != QA_QVMY_OP_ENTER)
    qa_save_reader_fail(reader, "callback is not a QVM function entry");
  return index;
}

static bool valid_id(const char *id) {
  if (id[0] < 'a' || id[0] > 'z') {
    if (!(id[0] >= '0' && id[0] <= '9')) return false;
  }
  for (const char *p = id; *p != '\0'; p++) {
    char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  }
  return id[0] != '\0';
}

void qa_qvm_read_weapon_profile(const qa_save_value *value, const qa_resolved_qvm_artifact *artifact,
                                qa_qvm_weapon_profile *out) {
  memset(out, 0, sizeof *out);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "qvm-weapon-profile"))
    qa_range_error("QVM memory allocation failed");
  const char *abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  qa_save_reader version = field(&reader, "version");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_save_reader digest = field(&reader, "artifactDigest");
  (void)qa_save_reader_literal_string(&digest, artifact->module.digest);
  qa_save_reader_destroy(&digest);
  qa_save_reader path = field(&reader, "artifactPath");
  (void)qa_save_reader_literal_string(&path, artifact->module.artifact_path);
  qa_save_reader_destroy(&path);
  qa_save_reader abi = field(&reader, "abiProfile");
  (void)qa_save_reader_literal_string(&abi, abi_profile);
  qa_save_reader_destroy(&abi);
  if (strcmp(artifact->role, "qagame") != 0)
    qa_save_reader_fail(&reader, "weapon behavior requires a qagame artifact");
  qa_save_reader id_reader = field(&reader, "id");
  const char *id = qa_save_reader_string(&id_reader);
  if (!valid_id(id)) qa_save_reader_fail(&id_reader, "expected a stable behavior identifier");
  size_t id_length = strlen(id);
  out->owned_id = xmalloc(id_length + 5);
  memcpy(out->owned_id, "qvm:", 4);
  memcpy(out->owned_id + 4, id, id_length + 1);
  qa_save_reader_destroy(&id_reader);
  qa_weapon_behavior_definition *definition = &out->definition;
  memset(definition, 0, sizeof *definition);
  definition->id = out->owned_id;
  definition->module = artifact->module;
  qa_save_reader title = field(&reader, "title");
  /* The declaration tree may be gone before the profile is (validate frees it). */
  {
    const char *text = qa_save_reader_string(&title);
    size_t length = strlen(text);
    out->owned_title = xmalloc(length + 1);
    memcpy(out->owned_title, text, length + 1);
    definition->title = out->owned_title;
  }
  qa_save_reader_destroy(&title);
  qa_save_reader role = field(&reader, "role");
  static const char *const roles[] = {"rocket", "grenade", "nail", "bolt", "plasma", "energy", "grapple"};
  definition->role = qa_save_reader_choice_string(&role, roles, 7);
  qa_save_reader_destroy(&role);
  qa_save_reader aspect = field(&reader, "aspect");
  definition->aspect = qa_save_reader_literal_string(&aspect, "trajectory");
  qa_save_reader_destroy(&aspect);
  definition->fire.kind = QA_WEAPON_CALLBACK_QVM;
  definition->fire.module = artifact->module;
  qa_save_reader fire = field(&reader, "fireFunction");
  definition->fire.instruction_index = (double)read_entry(artifact, &fire);
  qa_save_reader_destroy(&fire);
  qa_save_reader activation = field(&reader, "activationFunction");
  if (qa_save_reader_is_null(&activation) || qa_save_reader_is_undefined(&activation)) {
    definition->has_activate = false;
  } else {
    definition->has_activate = true;
    definition->activate.kind = QA_WEAPON_CALLBACK_QVM;
    definition->activate.module = artifact->module;
    definition->activate.instruction_index = (double)read_entry(artifact, &activation);
  }
  qa_save_reader_destroy(&activation);
  int64_t minimum = qa_qvm_shared_entity_bytes(abi_profile);
  qa_save_reader stride_reader = field(&reader, "entityStride");
  out->entity_stride = (int64_t)qa_save_reader_integer_min(&stride_reader, (double)minimum);
  if (out->entity_stride % 4 != 0 || out->entity_stride > artifact->image.allocated_data_length)
    qa_save_reader_fail(&stride_reader, "invalid entity stride");
  qa_save_reader_destroy(&stride_reader);
  qa_save_reader fields_reader = field(&reader, "fields");
  const char *field_names[] = {"inuse", "nextthink", "think", "health"};
  int64_t *field_slots[] = {&out->field_inuse, &out->field_nextthink, &out->field_think, &out->field_health};
  int64_t occupied[4];
  size_t occupied_count = 0;
  /* TS reads allocate/free through the return literal after fields;
   * reader calls here follow TS evaluation order (levelTime, then
   * allocate, free, fields) for identical failure points. */
  qa_save_reader level_reader = field(&reader, "levelTime");
  out->level_time = (int64_t)qa_save_reader_integer_min(&level_reader, 4);
  if (out->level_time % 4 != 0 ||
      out->level_time > artifact->image.data_length + artifact->image.literal_length + artifact->image.bss_length - 4)
    qa_save_reader_fail(&level_reader, "level clock is outside declared guest data");
  qa_save_reader_destroy(&level_reader);
  qa_save_reader allocate_reader = field(&reader, "allocateFunction");
  out->allocate = read_entry(artifact, &allocate_reader);
  qa_save_reader_destroy(&allocate_reader);
  qa_save_reader free_reader = field(&reader, "freeFunction");
  out->free_entry = read_entry(artifact, &free_reader);
  qa_save_reader_destroy(&free_reader);
  for (int i = 0; i < 4; i++) {
    qa_save_reader source = field(&fields_reader, field_names[i]);
    int64_t offset = (int64_t)qa_save_reader_integer_min(&source, (double)minimum);
    bool seen = false;
    for (size_t j = 0; j < occupied_count; j++) if (occupied[j] == offset) seen = true;
    if (offset % 4 != 0 || offset > out->entity_stride - 4 || seen)
      qa_save_reader_fail(&source, "private field is overlapping, unaligned or outside the entity");
    occupied[occupied_count++] = offset;
    *field_slots[i] = offset;
    qa_save_reader_destroy(&source);
  }
  qa_save_reader_destroy(&fields_reader);
  qa_save_reader fire_abi = field(&reader, "fireAbi");
  out->fire_abi = qa_save_reader_literal_string(&fire_abi, "entity-pointer-start-direction");
  qa_save_reader_destroy(&fire_abi);
  qa_save_reader_destroy(&reader);
}

void qa_qvm_validate_weapon_profile(const qa_qvm_weapon_profile *profile,
                                    const qa_resolved_qvm_artifact *artifact, qa_qvm_weapon_profile *out) {
  const char *abi_profile = artifact->has_abi_profile ? artifact->abi_profile : "q3-modern";
  qa_save_value *declaration = qa_qvm_weapon_profile_declaration(profile, abi_profile);
  qa_qvm_read_weapon_profile(declaration, artifact, out);
  qa_save_value_free(declaration);
  if (!qa_same_weapon_behavior(profile->definition, out->definition))
    qa_range_error("QVM weapon callback identity differs from the loaded artifact");
}

void qa_qvm_weapon_profile_free(qa_qvm_weapon_profile *profile) {
  free(profile->owned_id);
  free(profile->owned_title);
  memset(profile, 0, sizeof *profile);
}
