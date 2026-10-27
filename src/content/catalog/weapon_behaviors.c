/* Port of src/content/catalog/weapon-behaviors.ts, including
 * the qcWeaponBehaviorCapabilityError check (TS owner:
 * src/compat/qc/weapon-behavior-profile.ts). GPL-2.0-or-later. */
#include "qa/content/catalog/weapon_behaviors.h"

#include "catalog_internal.h"
#include "qa/persistence/value.h"

enum { QC_OP_ADDRESS = 30, QC_OP_STORE_FN = 36, QC_OP_STORE_P_FN = 42 };

static const qa_catalog_qc_definition *find_definition(
    const qa_catalog_qc_definition *values, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(values[i].name, name) == 0) return &values[i];
  return NULL;
}
static const qa_catalog_qc_function *find_function(
    const qa_catalog_qc_program *program, const char *name) {
  for (size_t i = 0; i < program->function_count; i++)
    if (strcmp(program->functions[i].name, name) == 0)
      return &program->functions[i];
  return NULL;
}

typedef struct name_type { const char *name; const char *type; } name_type;
static const name_type REQUIRED_FIELDS[] = {
    {"origin", "vector"}, {"velocity", "vector"}, {"angles", "vector"},
    {"mins", "vector"}, {"maxs", "vector"}, {"v_angle", "vector"},
    {"health", "float"}, {"solid", "float"}, {"nextthink", "float"},
    {"classname", "string"}, {"netname", "string"}, {"think", "function"}};
static const name_type REQUIRED_GLOBALS[] = {{"self", "entity"},
                                             {"other", "entity"}};
static const name_type OPTIONAL_FIELDS[] = {{"model", "string"},
                                            {"modelindex", "float"},
                                            {"chain", "entity"}};
static const name_type OPTIONAL_GLOBALS[] = {
    {"time", "float"}, {"trace_endpos", "vector"},
    {"trace_plane_normal", "vector"}, {"trace_ent", "entity"},
    {"deathmatch", "float"}, {"coop", "float"}, {"trace_fraction", "float"},
    {"trace_allsolid", "float"}, {"trace_startsolid", "float"},
    {"trace_inwater", "float"}, {"trace_inopen", "float"},
    {"trace_plane_dist", "float"}};

/* Malloc'd error or NULL. */
static char *capability_error(const qa_catalog_qc_program *program) {
  for (size_t i = 0; i < sizeof REQUIRED_FIELDS / sizeof REQUIRED_FIELDS[0]; i++) {
    const qa_catalog_qc_definition *field = find_definition(
        program->fields, program->field_count, REQUIRED_FIELDS[i].name);
    if (field == NULL || strcmp(field->type, REQUIRED_FIELDS[i].type) != 0)
      return qa_catalog_format(
          "QuakeC trajectory adapter requires entity field %s of type %s",
          REQUIRED_FIELDS[i].name, REQUIRED_FIELDS[i].type);
  }
  for (size_t i = 0; i < sizeof REQUIRED_GLOBALS / sizeof REQUIRED_GLOBALS[0]; i++) {
    const qa_catalog_qc_definition *global = find_definition(
        program->globals, program->global_count, REQUIRED_GLOBALS[i].name);
    if (global == NULL || strcmp(global->type, REQUIRED_GLOBALS[i].type) != 0)
      return qa_catalog_format(
          "QuakeC trajectory adapter requires global %s of type %s",
          REQUIRED_GLOBALS[i].name, REQUIRED_GLOBALS[i].type);
  }
  for (size_t i = 0; i < sizeof OPTIONAL_FIELDS / sizeof OPTIONAL_FIELDS[0]; i++) {
    const qa_catalog_qc_definition *field = find_definition(
        program->fields, program->field_count, OPTIONAL_FIELDS[i].name);
    if (field != NULL && strcmp(field->type, OPTIONAL_FIELDS[i].type) != 0)
      return qa_catalog_format(
          "QuakeC trajectory adapter requires declared entity field %s to have type %s",
          OPTIONAL_FIELDS[i].name, OPTIONAL_FIELDS[i].type);
  }
  for (size_t i = 0; i < sizeof OPTIONAL_GLOBALS / sizeof OPTIONAL_GLOBALS[0]; i++) {
    const qa_catalog_qc_definition *global = find_definition(
        program->globals, program->global_count, OPTIONAL_GLOBALS[i].name);
    if (global != NULL && strcmp(global->type, OPTIONAL_GLOBALS[i].type) != 0)
      return qa_catalog_format(
          "QuakeC trajectory adapter requires declared global %s to have type %s",
          OPTIONAL_GLOBALS[i].name, OPTIONAL_GLOBALS[i].type);
  }
  return NULL;
}

static bool find_callback(const qa_catalog_qc_program *program, const char *name,
                          qa_module_identity module,
                          qa_weapon_behavior_callback *out) {
  const qa_catalog_qc_function *fn = find_function(program, name);
  if (fn == NULL || fn->index == 0 || fn->first_statement < 0 ||
      fn->parameter_size_count != 0) return false;
  memset(out, 0, sizeof *out);
  out->kind = QA_WEAPON_CALLBACK_QUAKEC;
  out->module = module;
  out->function_index = (double)fn->index;
  return true;
}

qa_weapon_behavior_compatibility qa_resolve_qc_weapon_behavior(
    qa_module_identity module, const qa_catalog_qc_program *program,
    qa_source_weapon_behavior_metadata metadata) {
  qa_weapon_behavior_compatibility result = {0};
  if (strcmp(module.digest, program->digest) != 0 ||
      strcmp(metadata.artifact_digest, program->digest) != 0) {
    result.is_supported = false;
    result.reason = "Behavior metadata belongs to a different source artifact";
    return result;
  }
  char *capability = capability_error(program);
  if (capability != NULL) {
    /* The reason string outlives this call in the result; leak-
     * free ownership is the discovery list's owned_strings in
     * the discover path, and for direct callers the string is
     * intentionally retained (never freed) like a literal. */
    result.is_supported = false;
    result.reason = capability;
    return result;
  }
  qa_weapon_behavior_callback fire = {0}, activate = {0};
  bool have_fire = find_callback(program, metadata.fire_function, module, &fire);
  bool have_activate = metadata.activation_function == NULL ||
                       find_callback(program, metadata.activation_function,
                                     module, &activate);
  if (!have_fire || !have_activate) {
    result.is_supported = false;
    result.reason = "Declared behavior entrypoint is absent from the source program";
    return result;
  }
  result.is_supported = true;
  result.definition.id = metadata.id;
  result.definition.title = metadata.title;
  result.definition.module = module;
  result.definition.role = metadata.role;
  result.definition.aspect = metadata.aspect;
  result.definition.has_activate = metadata.activation_function != NULL;
  result.definition.activate = activate;
  result.definition.fire = fire;
  return result;
}

void qa_weapon_behavior_catalog_init(qa_weapon_behavior_catalog *catalog) {
  memset(catalog, 0, sizeof *catalog);
}
void qa_weapon_behavior_catalog_destroy(qa_weapon_behavior_catalog *catalog) {
  free(catalog->entries);
  memset(catalog, 0, sizeof *catalog);
}
bool qa_weapon_behavior_catalog_add(qa_weapon_behavior_catalog *catalog,
                                    const qa_weapon_behavior_definition *definition) {
  for (size_t i = 0; i < catalog->count; i++)
    if (strcmp(catalog->entries[i]->id, definition->id) == 0)
      qa_catalog_error("Duplicate weapon behavior %s", definition->id);
  if (catalog->count == catalog->capacity) {
    size_t next = catalog->capacity != 0 ? catalog->capacity * 2 : 8;
    const qa_weapon_behavior_definition **grown =
        (const qa_weapon_behavior_definition **)realloc(catalog->entries,
                                                        next * sizeof *grown);
    if (grown == NULL) return false;
    catalog->entries = grown; catalog->capacity = next;
  }
  catalog->entries[catalog->count++] = definition;
  return true;
}
const qa_weapon_behavior_definition **qa_weapon_behavior_catalog_for_role(
    const qa_weapon_behavior_catalog *catalog, qa_projectile_role role,
    size_t *out_count) {
  *out_count = 0;
  const qa_weapon_behavior_definition **values = NULL;
  size_t capacity = 0;
  for (size_t i = 0; i < catalog->count; i++) {
    if (strcmp(catalog->entries[i]->role, role) != 0) continue;
    if (*out_count == capacity) {
      size_t next = capacity != 0 ? capacity * 2 : 4;
      const qa_weapon_behavior_definition **grown =
          (const qa_weapon_behavior_definition **)realloc(values,
                                                          next * sizeof *grown);
      if (grown == NULL) { free(values); *out_count = 0; return NULL; }
      values = grown; capacity = next;
    }
    values[(*out_count)++] = catalog->entries[i];
  }
  return values;
}
const qa_weapon_behavior_definition *qa_weapon_behavior_catalog_require(
    const qa_weapon_behavior_catalog *catalog, const char *id) {
  for (size_t i = 0; i < catalog->count; i++)
    if (strcmp(catalog->entries[i]->id, id) == 0) return catalog->entries[i];
  qa_catalog_error("Unknown weapon behavior %s", id);
}

static bool word_at(const qa_catalog_qc_program *program, int64_t offset,
                    int32_t *out) {
  if (offset < 0 || (uint64_t)offset * 4 + 4 > program->initial_globals_length)
    return false;
  const uint8_t *bytes = program->initial_globals + (size_t)offset * 4;
  *out = (int32_t)((uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                   ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24));
  return true;
}

static int compare_functions(const void *left, const void *right) {
  const qa_catalog_qc_function *const *a =
      (const qa_catalog_qc_function *const *)left;
  const qa_catalog_qc_function *const *b =
      (const qa_catalog_qc_function *const *)right;
  return (*a)->first_statement < (*b)->first_statement ? -1
         : (*a)->first_statement > (*b)->first_statement ? 1 : 0;
}

qa_qc_trajectory_binding_inspection *qa_inspect_qc_trajectory_bindings(
    const qa_catalog_qc_program *program, size_t *out_count) {
  *out_count = 0;
  const qa_catalog_qc_definition *think =
      find_definition(program->fields, program->field_count, "think");
  if (think == NULL) return NULL;
  const qa_catalog_qc_function **functions = NULL;
  size_t function_count = 0, function_capacity = 0;
  for (size_t i = 0; i < program->function_count; i++) {
    const qa_catalog_qc_function *fn = &program->functions[i];
    if (fn->index == 0 || fn->first_statement < 0) continue;
    if (function_count == function_capacity) {
      size_t next = function_capacity != 0 ? function_capacity * 2 : 8;
      const qa_catalog_qc_function **grown =
          (const qa_catalog_qc_function **)realloc(functions, next * sizeof *grown);
      if (grown == NULL) { free(functions); return NULL; }
      functions = grown; function_capacity = next;
    }
    functions[function_count++] = fn;
  }
  if (function_count > 1)
    qsort(functions, function_count, sizeof *functions, compare_functions);
  qa_qc_trajectory_binding_inspection *result = NULL;
  size_t capacity = 0;
  for (size_t index = 0; index < function_count; index++) {
    const qa_catalog_qc_function *fn = functions[index];
    int64_t end = index + 1 < function_count
                      ? functions[index + 1]->first_statement
                      : (int64_t)program->statement_count;
    for (int64_t cursor = fn->first_statement; cursor + 1 < end; cursor++) {
      const qa_catalog_qc_statement *address = &program->statements[cursor];
      const qa_catalog_qc_statement *store = &program->statements[cursor + 1];
      int32_t field_offset = 0;
      if (address->opcode != QC_OP_ADDRESS || store->opcode != QC_OP_STORE_P_FN ||
          address->c != store->b || !word_at(program, address->b, &field_offset) ||
          (int64_t)field_offset != think->offset) continue;
      int32_t target_word = 0;
      if (!word_at(program, store->a, &target_word)) continue;
      if (target_word < 0 || (size_t)target_word >= program->function_count) continue;
      const qa_catalog_qc_function *callback =
          &program->functions[target_word];
      /* TS indexes by position in the functions array. */
      if (callback->index == 0 || callback->first_statement < 0) continue;
      bool literal_global = false;
      for (size_t g = 0; g < program->global_count; g++)
        if (strcmp(program->globals[g].type, "function") == 0 &&
            program->globals[g].offset == store->a &&
            strcmp(program->globals[g].name, callback->name) == 0)
          literal_global = true;
      if (!literal_global) continue;
      bool stored = false;
      for (size_t s = 0; s < program->statement_count; s++)
        if (program->statements[s].opcode == QC_OP_STORE_FN &&
            program->statements[s].b == store->a) stored = true;
      if (stored) continue;
      if (*out_count == capacity) {
        size_t next = capacity != 0 ? capacity * 2 : 8;
        qa_qc_trajectory_binding_inspection *grown =
            (qa_qc_trajectory_binding_inspection *)realloc(result,
                                                           next * sizeof *grown);
        if (grown == NULL) { free(functions); free(result); *out_count = 0; return NULL; }
        result = grown; capacity = next;
      }
      result[*out_count].producer_function = fn->index;
      result[*out_count].think_function = callback->index;
      result[*out_count].statement = cursor + 1;
      (*out_count)++;
    }
  }
  free(functions);
  return result;
}

static bool is_record_value(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_OBJECT;
}
static bool behavior_id_valid(const char *value) {
  /* ^[^:\s]+:[^\s]+$ */
  const char *colon = strchr(value, ':');
  if (colon == NULL || colon == value || colon[1] == '\0') return false;
  for (size_t i = 0; value[i] != '\0'; i++)
    if (isspace((unsigned char)value[i])) return false;
  return true;
}
static bool projectile_role_valid(const char *value) {
  static const char *const roles[] = {"rocket", "grenade", "nail", "bolt",
                                      "plasma", "energy", "grapple"};
  for (size_t i = 0; i < 7; i++) if (strcmp(value, roles[i]) == 0) return true;
  return false;
}

static const char *string_or_null(const qa_save_value *value) {
  return value != NULL && value->kind == QA_SAVE_STRING
             ? (value->text != NULL ? value->text : "") : NULL;
}

void qa_mounted_weapon_behavior_discovery_free(
    qa_mounted_weapon_behavior_discovery *discovery) {
  free(discovery->declarations);
  free(discovery->bindings);
  for (size_t i = 0; i < discovery->owned_count; i++)
    free(discovery->owned_strings[i]);
  free(discovery->owned_strings);
  memset(discovery, 0, sizeof *discovery);
}

static bool own_discovery_string(qa_mounted_weapon_behavior_discovery *out,
                                 char *owned) {
  char **grown = (char **)realloc(out->owned_strings,
                                  (out->owned_count + 1) * sizeof *grown);
  if (grown == NULL) { free(owned); return false; }
  out->owned_strings = grown;
  out->owned_strings[out->owned_count++] = owned;
  return true;
}

bool qa_discover_qc_weapon_behaviors(qa_catalog_mounted_reader content,
                                     qa_module_identity module,
                                     const qa_catalog_qc_program *program,
                                     qa_mounted_weapon_behavior_discovery *out) {
  memset(out, 0, sizeof *out);
  qa_catalog_opened_resource resource = {0};
  bool found = content.open(content.ctx, "weapon-behaviors.json", &resource);
  if (!found) {
    out->is_declared = false;
    out->bindings = qa_inspect_qc_trajectory_bindings(program, &out->binding_count);
    out->reason =
        "No authored weapon behavior declaration; bytecode callback stores do not establish projectile role, activation gate or selected aspect";
    return true;
  }
  qa_save_value *value =
      qa_parse_json_value(resource.bytes, resource.byte_length, false);
  if (value == NULL) return false;
  const qa_save_value *version =
      is_record_value(value) ? qa_save_value_get(value, "version") : NULL;
  const qa_save_value *behaviors =
      is_record_value(value) ? qa_save_value_get(value, "behaviors") : NULL;
  if (!is_record_value(value) || version == NULL ||
      version->kind != QA_SAVE_NUMBER || version->number != 1 ||
      behaviors == NULL || behaviors->kind != QA_SAVE_ARRAY) {
    qa_save_value_free(value);
    qa_catalog_error("Invalid weapon behavior declaration");
  }
  out->is_declared = true;
  out->declarations =
      (qa_weapon_behavior_compatibility *)calloc(
          behaviors->item_count != 0 ? behaviors->item_count : 1,
          sizeof *out->declarations);
  if (out->declarations == NULL) { qa_save_value_free(value); return false; }
  /* Keep the parsed tree alive through owned string dups of every
   * borrowed field, then free it. */
  for (size_t i = 0; i < behaviors->item_count; i++) {
    const qa_save_value *entry = behaviors->items[i];
    const char *id = is_record_value(entry)
                         ? string_or_null(qa_save_value_get(entry, "id")) : NULL;
    const char *title = is_record_value(entry)
                            ? string_or_null(qa_save_value_get(entry, "title")) : NULL;
    const char *digest = is_record_value(entry)
        ? string_or_null(qa_save_value_get(entry, "artifactDigest")) : NULL;
    const char *fire = is_record_value(entry)
        ? string_or_null(qa_save_value_get(entry, "fireFunction")) : NULL;
    const qa_save_value *activation =
        is_record_value(entry) ? qa_save_value_get(entry, "activationFunction") : NULL;
    const char *activation_text = string_or_null(activation);
    const char *role = is_record_value(entry)
                           ? string_or_null(qa_save_value_get(entry, "role")) : NULL;
    const char *aspect = is_record_value(entry)
                             ? string_or_null(qa_save_value_get(entry, "aspect")) : NULL;
    if (!is_record_value(entry) || id == NULL || !behavior_id_valid(id) ||
        title == NULL || digest == NULL || fire == NULL ||
        (activation != NULL && activation->kind != QA_SAVE_UNDEFINED &&
         activation_text == NULL) ||
        role == NULL || !projectile_role_valid(role) || aspect == NULL ||
        strcmp(aspect, "trajectory") != 0) {
      qa_save_value_free(value);
      qa_mounted_weapon_behavior_discovery_free(out);
      qa_catalog_error("Invalid weapon behavior entry");
    }
    char *artifact_digest = NULL;
    bool raw_hex = strlen(digest) == 64;
    if (raw_hex)
      for (size_t h = 0; h < 64; h++) {
        char c = digest[h];
        if (!((c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'))) raw_hex = false;
      }
    if (qa_is_content_digest(digest)) artifact_digest = qa_catalog_dup(digest);
    else if (raw_hex) artifact_digest = qa_create_content_digest(digest);
    if (artifact_digest == NULL || !own_discovery_string(out, artifact_digest)) {
      qa_save_value_free(value);
      qa_mounted_weapon_behavior_discovery_free(out);
      qa_catalog_error("Invalid weapon behavior artifact digest");
    }
    for (size_t prior = 0; prior < out->declaration_count; prior++) {
      if (out->declarations[prior].is_supported &&
          strcmp(out->declarations[prior].definition.id, id) == 0) {
        qa_save_value_free(value);
        qa_mounted_weapon_behavior_discovery_free(out);
        qa_catalog_error("Duplicate declared weapon behavior %s", id);
      }
    }
    /* Duplicate ids among unsupported entries still count in TS
     * (the seen set is separate); record ids in owned strings. */
    char *owned_id = qa_catalog_dup(id);
    char *owned_title = qa_catalog_dup(title);
    char *owned_fire = qa_catalog_dup(fire);
    char *owned_activation = qa_catalog_dup(activation_text);
    char *owned_role = qa_catalog_dup(role);
    bool duplicate = false;
    for (size_t s = 0; s < out->owned_count; s++)
      if (qa_catalog_starts_with(out->owned_strings[s], "id:") &&
          strcmp(out->owned_strings[s] + 3, id) == 0) duplicate = true;
    if (duplicate) {
      free(owned_id); free(owned_title); free(owned_fire);
      free(owned_activation); free(owned_role);
      qa_save_value_free(value);
      qa_mounted_weapon_behavior_discovery_free(out);
      qa_catalog_error("Duplicate declared weapon behavior %s", id);
    }
    char *seen = qa_catalog_format("id:%s", id);
    if (owned_id == NULL || owned_title == NULL || owned_fire == NULL ||
        owned_role == NULL || !own_discovery_string(out, owned_id) ||
        !own_discovery_string(out, owned_title) ||
        !own_discovery_string(out, owned_fire) ||
        (owned_activation != NULL && !own_discovery_string(out, owned_activation)) ||
        !own_discovery_string(out, owned_role) ||
        !own_discovery_string(out, seen)) {
      qa_save_value_free(value);
      qa_mounted_weapon_behavior_discovery_free(out);
      return false;
    }
    qa_source_weapon_behavior_metadata metadata = {0};
    metadata.id = owned_id;
    metadata.title = owned_title;
    metadata.artifact_digest = artifact_digest;
    metadata.role = owned_role;
    metadata.aspect = "trajectory";
    metadata.fire_function = owned_fire;
    metadata.activation_function = owned_activation;
    qa_weapon_behavior_compatibility resolved =
        qa_resolve_qc_weapon_behavior(module, program, metadata);
    if (!resolved.is_supported && resolved.reason != NULL &&
        strcmp(resolved.reason,
               "Behavior metadata belongs to a different source artifact") != 0 &&
        strcmp(resolved.reason,
               "Declared behavior entrypoint is absent from the source program") != 0) {
      /* Capability reasons are malloc'd by the resolve call. */
      (void)own_discovery_string(out, (char *)resolved.reason);
    }
    out->declarations[out->declaration_count++] = resolved;
  }
  qa_save_value_free(value);
  return true;
}
