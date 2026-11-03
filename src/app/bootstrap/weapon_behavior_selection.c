/* Port of src/app/bootstrap/weapon-behavior-selection.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include <stdio.h>
#include <stdnoreturn.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
#include "qa/compat/qc/program.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/weapon_behavior_profile.h"
#include "qa/content/catalog/native_weapon_behaviors.h"
#include "qa/content/catalog/qvm_weapon_behaviors.h"
#include "qa/content/catalog/weapon_behavior_document.h"
#include "qa/content/catalog/weapon_behaviors.h"
#include "qa/content/mounts.h"
#include "qa/foundation/support.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/guest/pe/format.h"
#include "qa/input/support.h"
#include "qa/persistence/value.h"
static char *xdup_range(const char *s, size_t n) { char *r = qa_input_dup_range(s, n); if (!r) qa_range_error("Out of memory"); return r; }
qa_app_weapon_behavior_request qa_app_read_weapon_behavior_request(const char *value) {
  const char *slash = strchr(value, '/');
  size_t len = strlen(value);
  bool has_space = false;
  for (const char *p = value; *p; p++) if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f') has_space = true;
  if (slash == NULL || slash == value || (size_t)(slash - value) == len - 1 || has_space || strchr(slash + 1, ':') == NULL)
    qa_range_error("Weapon behavior must be PRODUCT/DECLARED_ID");
  qa_app_weapon_behavior_request r = {xdup_range(value, (size_t)(slash - value)), NULL};
  r.id = qa_input_dup(slash + 1);
  if (!r.id) qa_range_error("Out of memory");
  return r;
}
void qa_app_weapon_behavior_request_free(qa_app_weapon_behavior_request *r) { free(r->product); free(r->id); memset(r, 0, sizeof *r); }
qa_app_options qa_app_configured_weapon_behavior_options(qa_app_options options, qa_cvar_registry *cvars) {
  if (options.has_weapon_behavior || options.mod_count != 0) return options;
  qa_cvar_read found; memset(&found, 0, sizeof found);
  const char *value = "";
  if (qa_cvar_registry_find(cvars, "qts_weaponBehavior", &found)) value = found.value;
  if (value[0] == '\0') {
    if (value != (const char *)"") qa_cvar_read_free(&found);
    return options;
  }
  qa_app_weapon_behavior_request request = qa_app_read_weapon_behavior_request(value);
  qa_cvar_read_free(&found);
  options.has_weapon_behavior = true;
  free(options.weapon_behavior_product); options.weapon_behavior_product = request.product;
  free(options.weapon_behavior_id); options.weapon_behavior_id = request.id;
  return options;
}
qa_app_weapon_behavior_choice *qa_app_weapon_behavior_choices(qa_app_weapon_behavior_host host, qa_content_catalog *catalog, const char *product_id, size_t *out_count) {
  return host.choices_for_product(host.ctx, catalog, product_id, out_count);
}
qa_executable_recipe qa_app_select_weapon_behavior(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                   qa_executable_recipe recipe, qa_app_weapon_behavior_request request,
                                                   qa_resolved_weapon_behavior_selection *selection_storage) {
  for (size_t i = 0; i < recipe.execution_count; i++) {
    const qa_resolved_execution_module *module = &recipe.execution[i];
    /* role "server-game" */
    if (strcmp(module->role, "server-game") == 0 && module->kind != QA_EXECUTION_MODULE_TYPESCRIPT)
      qa_range_error("Selected native or bytecode game has no shared projectile behavior hook; choose a TypeScript launcher provider");
  }
  size_t count = 0;
  qa_app_weapon_behavior_choice *choices = qa_app_weapon_behavior_choices(host, catalog, request.product, &count);
  const qa_resolved_weapon_behavior_selection *selected = NULL;
  const char *unavailable = NULL;
  for (size_t i = 0; i < count; i++) {
    if (choices[i].selection != NULL && strcmp(choices[i].selection->definition->id, request.id) == 0) selected = choices[i].selection;
    if (unavailable == NULL && choices[i].unavailable != NULL) unavailable = choices[i].unavailable;
  }
  if (selected == NULL) {
    char *reason = unavailable ? qa_input_dup(unavailable) : NULL;
    host.choices_free(host.ctx, choices, count);
    qa_range_error("Weapon behavior %s/%s: %s", request.product, request.id, reason ? reason : "No such declared compatible behavior");
  }
  *selection_storage = *selected;
  host.choices_free(host.ctx, choices, count);
  recipe.weapon_behaviors = selection_storage;
  recipe.weapon_behavior_count = 1;
  return recipe;
}
qa_app_prepared_configured_recipe qa_app_prepare_configured_recipe(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                                   const qa_app_options *options, qa_executable_recipe recipe) {
  size_t request_count = options->mod_count + (options->has_weapon_behavior ? 1 : 0);
  if (request_count == 0) { qa_app_prepared_configured_recipe r = {catalog, recipe}; return r; }
  qa_app_weapon_behavior_request *requests = calloc(request_count, sizeof *requests);
  if (!requests) qa_range_error("Out of memory");
  size_t at = 0;
  for (size_t i = 0; i < options->mod_count; i++) {
    requests[at].product = qa_input_dup(options->mods[i].product);
    requests[at].id = qa_input_dup(options->mods[i].id);
    if (!requests[at].product || !requests[at].id) qa_range_error("Out of memory");
    at++;
  }
  if (options->has_weapon_behavior) {
    requests[at].product = qa_input_dup(options->weapon_behavior_product);
    requests[at].id = qa_input_dup(options->weapon_behavior_id);
    if (!requests[at].product || !requests[at].id) qa_range_error("Out of memory");
    at++;
  }
  bool missing = false;
  for (size_t i = 0; i < request_count; i++) if (!host.catalog_has_product(host.ctx, catalog, requests[i].product)) missing = true;
  if (missing) {
    qa_content_catalog *fresh = host.rediscover_with_mods(host.ctx, catalog);
    if (fresh != NULL) catalog = fresh;
  }
  qa_executable_recipe prepared = host.apply_mods(host.ctx, catalog, recipe, requests, request_count);
  for (size_t i = 0; i < request_count; i++) qa_app_weapon_behavior_request_free(&requests[i]);
  free(requests);
  qa_app_prepared_configured_recipe r = {catalog, prepared};
  return r;
}
/* --- Concrete choicesFromMounts / applicationWeaponBehaviorChoices.
 * Service adapters mirror weapon_behavior_tool.c's composition over
 * the same landed compat services. --- */
typedef struct native_backing {
  qa_catalog_mount_list mounts;
  qa_mount_id *order;
  char *plan_id;
  qa_content_mounted *mounted; bool borrowed_mounted; /* borrowed: the content's forContent mounts */
  uint8_t **buffers; size_t buffer_count, buffer_capacity;
  qa_mounted_qvm_weapon_behavior_list qvm;
  qa_mounted_native_weapon_behavior_list native;
  qa_weapon_behavior_document document; bool has_document;
  qa_compat_qc_program program; bool has_program;
  qa_catalog_qc_program view;
  qa_catalog_qc_statement *statements;
  qa_catalog_qc_function *functions;
  qa_catalog_qc_definition *globals, *fields;
  qa_mounted_weapon_behavior_discovery discovery; bool has_discovery;
  qa_resolved_weapon_behavior_selection *selections; size_t selection_count;
  qa_qvm_weapon_behavior_layout *layouts;
  char *module_id;
} native_backing;
static void *native_xcalloc(size_t count, size_t size) {
  void *ptr = calloc(count == 0 ? 1 : count, size);
  if (!ptr) qa_range_error("Out of memory");
  return ptr;
}
static char *native_format(const char *format, const char *a, const char *b, const char *c) {
  int length = snprintf(NULL, 0, format, a, b, c);
  if (length < 0) qa_range_error("Out of memory");
  char *out = native_xcalloc((size_t)length + 1, 1);
  (void)snprintf(out, (size_t)length + 1, format, a, b, c);
  return out;
}
static char *native_hex_of(const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = native_xcalloc(length * 2 + 1, 1);
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 15];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 15];
  }
  return out;
}
static bool native_reader_open(void *ctx, const char *path, qa_catalog_opened_resource *out) {
  native_backing *backing = ctx;
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(backing->mounted, path, NULL, NULL, &opened)) return false;
  if (backing->buffer_count == backing->buffer_capacity) {
    size_t capacity = backing->buffer_capacity == 0 ? 8 : backing->buffer_capacity * 2;
    uint8_t **grown = realloc(backing->buffers, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    backing->buffers = grown;
    backing->buffer_capacity = capacity;
  }
  backing->buffers[backing->buffer_count++] = opened.bytes;
  out->reference = *opened.reference;
  out->bytes = opened.bytes;
  out->byte_length = opened.byte_length;
  return true;
}
static bool native_qvm_resolve(void *ctx, qa_catalog_qvm_artifact *artifact,
    const uint8_t *bytes, size_t length) {
  (void)ctx;
  qa_resolved_qvm_artifact *resolved = native_xcalloc(1, sizeof *resolved);
  qa_qvm_resolve_artifact_options options = {0};
  options.module = artifact->module;
  options.role = artifact->role;
  options.bytes = bytes;
  options.length = length;
  options.has_abi_profile = artifact->abi_profile != NULL;
  options.abi_profile = artifact->abi_profile;
  if (!qa_qvm_resolve_artifact(options, resolved)) { free(resolved); return false; }
  artifact->kind = resolved->is_typescript ? "typescript" : "bytecode";
  artifact->service_value = resolved;
  return true;
}
static bool native_qvm_profile(void *ctx, const qa_save_value *declaration,
    const qa_catalog_qvm_artifact *artifact, qa_catalog_qvm_weapon_profile *out) {
  (void)ctx;
  const qa_resolved_qvm_artifact *resolved = artifact->service_value;
  if (resolved->is_typescript) return false;
  qa_qvm_weapon_profile *profile = native_xcalloc(1, sizeof *profile);
  qa_qvm_read_weapon_profile(declaration, resolved, profile);
  out->definition = profile->definition;
  out->service_value = profile;
  return true;
}
static bool native_read_declaration(void *ctx, const qa_save_value *value,
    const qa_module_identity *module, qa_native_weapon_behavior_declaration *out) {
  (void)ctx;
  return qa_compat_q2_read_native_weapon_declaration(value, module, out);
}
static bool native_validate_image(void *ctx, const qa_native_weapon_behavior_declaration *declaration,
    const uint8_t *bytes, size_t length) {
  (void)ctx;
  qa_pe_file image = {0};
  qa_parse_pe(&image, bytes, length);
  qa_compat_q2_pe_section *sections = native_xcalloc(image.section_count + 1, sizeof *sections);
  for (size_t i = 0; i < image.section_count; i++) {
    sections[i].rva = (double)image.sections[i].rva;
    sections[i].mapped_size = (double)image.sections[i].mapped_size;
    sections[i].permissions = qa_guest_permissions_name(image.sections[i].permissions);
  }
  qa_compat_q2_pe_file mirror = {0};
  mirror.image_size = (double)image.image_size;
  mirror.pointer_bytes = image.abi.pointer_bytes;
  mirror.sections = sections;
  mirror.section_count = image.section_count;
  qa_compat_q2_validate_native_weapon_image(declaration, image.abi.kind, mirror);
  free(sections);
  qa_pe_file_free(&image);
  return true;
}
static bool native_weapon_definition(void *ctx, qa_module_identity module,
    const qa_native_weapon_behavior_declaration *declaration, qa_weapon_behavior_definition *out) {
  (void)ctx;
  return qa_compat_q2_rerelease_weapon_definition(module, declaration, out);
}
static bool native_builtin_declaration(void *ctx, qa_module_identity module, qa_save_value **out) {
  (void)ctx;
  qa_native_weapon_behavior_declaration declaration = {0};
  if (!qa_compat_q2_built_in_rerelease_weapon_declaration(module, &declaration)) return false;
  char *json = qa_compat_q2_serialize_native_weapon_declaration(&declaration);
  qa_compat_q2_native_weapon_declaration_free(&declaration);
  if (json == NULL) return false;
  *out = qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  free(json);
  return *out != NULL;
}
static qa_app_weapon_behavior_choice native_unavailable(const char *product_id, const char *title,
    const char *reason) {
  qa_app_weapon_behavior_choice choice = {0};
  choice.id = native_format("%s/unavailable", product_id, "", "");
  choice.title = qa_input_dup(title);
  choice.unavailable = qa_input_dup(reason);
  if (!choice.title || !choice.unavailable) qa_range_error("Out of memory");
  return choice;
}
static qa_app_weapon_behavior_choice_list native_single(qa_app_weapon_behavior_choice choice,
    native_backing *backing) {
  qa_app_weapon_behavior_choice_list list = {0};
  list.items = native_xcalloc(1, sizeof *list.items);
  list.items[0] = choice;
  list.count = 1;
  list.backing = backing;
  return list;
}
static void native_catalog_view(native_backing *backing, const qa_compat_qc_program *program) {
  backing->statements = native_xcalloc(program->statement_count + 1, sizeof *backing->statements);
  for (size_t i = 0; i < program->statement_count; i++) {
    backing->statements[i].opcode = (int64_t)program->statements[i].opcode;
    backing->statements[i].a = program->statements[i].a;
    backing->statements[i].b = program->statements[i].b;
    backing->statements[i].c = program->statements[i].c;
  }
  backing->functions = native_xcalloc(program->function_count + 1, sizeof *backing->functions);
  for (size_t i = 0; i < program->function_count; i++) {
    backing->functions[i].index = program->functions[i].index;
    backing->functions[i].name = program->functions[i].name;
    backing->functions[i].first_statement = program->functions[i].first_statement;
    backing->functions[i].parameter_size_count = program->functions[i].parameter_size_count;
  }
  backing->globals = native_xcalloc(program->global_count + 1, sizeof *backing->globals);
  for (size_t i = 0; i < program->global_count; i++) {
    backing->globals[i].name = program->globals[i].name;
    backing->globals[i].type = program->globals[i].type;
    backing->globals[i].offset = program->globals[i].offset;
  }
  backing->fields = native_xcalloc(program->field_count + 1, sizeof *backing->fields);
  for (size_t i = 0; i < program->field_count; i++) {
    backing->fields[i].name = program->fields[i].name;
    backing->fields[i].type = program->fields[i].type;
    backing->fields[i].offset = program->fields[i].offset;
  }
  backing->view.statements = backing->statements;
  backing->view.statement_count = program->statement_count;
  backing->view.functions = backing->functions;
  backing->view.function_count = program->function_count;
  backing->view.globals = backing->globals;
  backing->view.global_count = program->global_count;
  backing->view.fields = backing->fields;
  backing->view.field_count = program->field_count;
  backing->view.initial_globals = program->initial_globals;
  backing->view.initial_globals_length = program->initial_globals_length;
  backing->view.digest = program->digest;
}
/* TS choicesFromMounts(catalog, productId, mounts) over backing->mounted. */
static qa_app_weapon_behavior_choice_list choices_from_mounts(qa_content_catalog *catalog, const char *product_id,
    native_backing *backing) {
  const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, product_id);
  const char *title = product->expectation.title;
  char *provider = native_format("weapon-behavior:%s", product->id, "", "");
  /* Trim the two unused format args' effect: format has one %s. */
  qa_catalog_mounted_reader reader = {backing, native_reader_open, NULL};
  if (product->expectation.family == QA_GAME_FAMILY_Q3) {
    qa_qvm_weapon_behavior_services services = {NULL, native_qvm_resolve, native_qvm_profile};
    bool found = false;
    if (!qa_discover_qvm_weapon_behaviors(reader, services, provider, &backing->qvm, &found))
      qa_range_error("Out of memory");
    free(provider);
    if (!found) return native_single(native_unavailable(product_id, title,
        "No authored qvm-weapon-behaviors.json declaration with exact artifact and source layout"), backing);
    if (backing->qvm.count == 0) return native_single(native_unavailable(product_id, title,
        "The source declares no QVM trajectory behaviors"), backing);
    backing->selections = native_xcalloc(backing->qvm.count, sizeof *backing->selections);
    backing->layouts = native_xcalloc(backing->qvm.count, sizeof *backing->layouts);
    backing->selection_count = backing->qvm.count;
    qa_app_weapon_behavior_choice_list list = {0};
    list.items = native_xcalloc(backing->qvm.count, sizeof *list.items);
    list.count = backing->qvm.count;
    list.backing = backing;
    for (size_t i = 0; i < backing->qvm.count; i++) {
      qa_mounted_qvm_weapon_behavior *entry = &backing->qvm.values[i];
      const qa_qvm_weapon_profile *profile = entry->profile.service_value;
      qa_qvm_weapon_behavior_layout *layout = &backing->layouts[i];
      layout->entity_stride = (double)profile->entity_stride;
      layout->level_time = (double)profile->level_time;
      layout->allocate = (double)profile->allocate;
      layout->free = (double)profile->free_entry;
      layout->field_inuse = (double)profile->field_inuse;
      layout->field_nextthink = (double)profile->field_nextthink;
      layout->field_think = (double)profile->field_think;
      layout->field_health = (double)profile->field_health;
      layout->fire_abi = profile->fire_abi;
      qa_resolved_weapon_behavior_selection *selection = &backing->selections[i];
      selection->has_component = true;
      selection->component_is_qvm = true;
      selection->component_abi_profile = entry->artifact.abi_profile != NULL
          ? entry->artifact.abi_profile : "q3-modern";
      selection->component_layout = layout;
      selection->source.provider = entry->artifact.module.id;
      selection->source.content = product->id;
      selection->artifact = entry->resource;
      selection->definition = &entry->profile.definition;
      list.items[i].id = native_format("%s/%s", product_id, entry->profile.definition.id, "");
      list.items[i].title = native_format("%s \xe2\x80\x94 %s (%s)", title,
          entry->profile.definition.title, entry->profile.definition.role);
      list.items[i].selection = selection;
    }
    return list;
  }
  if (product->expectation.family == QA_GAME_FAMILY_Q2
      && product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0) {
    qa_native_weapon_behavior_services services = {NULL, native_read_declaration, native_validate_image,
        native_weapon_definition, native_builtin_declaration};
    bool found = false;
    if (!qa_discover_native_weapon_behaviors(reader, services, provider, &backing->native, &found))
      qa_range_error("Out of memory");
    free(provider);
    if (!found) return native_single(native_unavailable(product_id, title,
        "No authored native-weapon-behaviors.json declaration or matching built-in native profile"), backing);
    if (backing->native.count == 0) return native_single(native_unavailable(product_id, title,
        "The source declares no native trajectory behaviors"), backing);
    backing->selections = native_xcalloc(backing->native.count, sizeof *backing->selections);
    backing->selection_count = backing->native.count;
    qa_app_weapon_behavior_choice_list list = {0};
    list.items = native_xcalloc(backing->native.count, sizeof *list.items);
    list.count = backing->native.count;
    list.backing = backing;
    for (size_t i = 0; i < backing->native.count; i++) {
      qa_mounted_native_weapon_behavior *entry = &backing->native.values[i];
      qa_resolved_weapon_behavior_selection *selection = &backing->selections[i];
      selection->has_component = true;
      selection->component_is_qvm = false;
      selection->component_declaration = &entry->declaration;
      selection->source.provider = entry->definition.module.id;
      selection->source.content = product->id;
      selection->artifact = entry->resource;
      selection->definition = &entry->definition;
      list.items[i].id = native_format("%s/%s", product_id, entry->definition.id, "");
      list.items[i].title = native_format("%s \xe2\x80\x94 %s (%s)", title,
          entry->definition.title, entry->definition.role);
      list.items[i].selection = selection;
    }
    return list;
  }
  free(provider);
  if (product->expectation.family != QA_GAME_FAMILY_Q1)
    return native_single(native_unavailable(product_id, title,
        "This provider has no supported declared or artifact-qualified trajectory adapter"), backing);
  qa_catalog_opened_resource descriptor = {0};
  if (!native_reader_open(backing, "weapon-behaviors.json", &descriptor))
    return native_single(native_unavailable(product_id, title,
        "No authored weapon-behaviors.json trajectory declaration; use weapon-behavior inspect to inspect source callbacks"), backing);
  if (!qa_read_weapon_behavior_document(descriptor.bytes, descriptor.byte_length, &backing->document))
    qa_range_error("Out of memory");
  backing->has_document = true;
  const char *path = backing->document.has_artifact_path ? backing->document.artifact_path
      : (product->expectation.edition != NULL && strcmp(product->expectation.edition, "quakeworld") == 0
          ? "qwprogs.dat" : "progs.dat");
  qa_catalog_opened_resource artifact = {0};
  if (!native_reader_open(backing, path, &artifact)) {
    char *reason = native_format("Declared behavior requires mounted %s", path, "", "");
    qa_app_weapon_behavior_choice choice = native_unavailable(product_id, title, reason);
    free(reason);
    return native_single(choice, backing);
  }
  if (!qa_compat_qc_load_program(artifact.bytes, artifact.byte_length, NULL, NULL, &backing->program))
    qa_range_error("Out of memory");
  backing->has_program = true;
  native_catalog_view(backing, &backing->program);
  backing->module_id = native_format("weapon-behavior:%s", product->id, "", "");
  qa_module_identity module = {0};
  module.id = backing->module_id;
  module.artifact_path = path;
  module.digest = artifact.reference.digest;
  module.revision = artifact.reference.digest;
  if (!qa_discover_qc_weapon_behaviors(reader, module, &backing->view, &backing->discovery))
    qa_range_error("Out of memory");
  backing->has_discovery = true;
  if (!backing->discovery.is_declared)
    return native_single(native_unavailable(product_id, title, backing->discovery.reason), backing);
  if (backing->discovery.declaration_count == 0)
    return native_single(native_unavailable(product_id, title,
        "The source declares no trajectory behaviors"), backing);
  backing->selections = native_xcalloc(backing->discovery.declaration_count, sizeof *backing->selections);
  backing->selection_count = backing->discovery.declaration_count;
  qa_app_weapon_behavior_choice_list list = {0};
  list.items = native_xcalloc(backing->discovery.declaration_count, sizeof *list.items);
  list.count = backing->discovery.declaration_count;
  list.backing = backing;
  for (size_t i = 0; i < backing->discovery.declaration_count; i++) {
    qa_weapon_behavior_compatibility *value = &backing->discovery.declarations[i];
    if (!value->is_supported) {
      int length = snprintf(NULL, 0, "%s/unsupported-%zu", product_id, i);
      list.items[i].id = native_xcalloc((size_t)length + 1, 1);
      (void)snprintf(list.items[i].id, (size_t)length + 1, "%s/unsupported-%zu", product_id, i);
      list.items[i].title = qa_input_dup(title);
      list.items[i].unavailable = qa_input_dup(value->reason);
      if (!list.items[i].title || !list.items[i].unavailable) qa_range_error("Out of memory");
      continue;
    }
    qa_resolved_weapon_behavior_selection *selection = &backing->selections[i];
    selection->source.provider = backing->module_id;
    selection->source.content = product->id;
    selection->artifact = artifact.reference;
    selection->definition = &value->definition;
    list.items[i].id = native_format("%s/%s", product_id, value->definition.id, "");
    list.items[i].title = native_format("%s \xe2\x80\x94 %s (%s)", title,
        value->definition.title, value->definition.role);
    list.items[i].selection = selection;
  }
  return list;
}
qa_app_weapon_behavior_choice_list qa_app_weapon_behavior_choices_native(qa_content_catalog *catalog,
    const char *product_id) {
  const qa_catalog_discovered_product *product = qa_content_catalog_require(catalog, product_id);
  native_backing *backing = native_xcalloc(1, sizeof *backing);
  if (!qa_content_catalog_mounts_for(catalog, product->id, &backing->mounts)) qa_range_error("Out of memory");
  backing->order = native_xcalloc(backing->mounts.count + 1, sizeof *backing->order);
  for (size_t i = 0; i < backing->mounts.count; i++)
    backing->order[i] = backing->mounts.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE
        ? backing->mounts.mounts[i].as.archive.identity.id
        : backing->mounts.mounts[i].as.loose.identity.id;
  char *hex = native_hex_of(product->id);
  backing->plan_id = qa_create_mount_plan_id("weapon-behavior", hex);
  free(hex);
  if (!backing->plan_id) qa_range_error("Out of memory");
  qa_resolved_mount_plan plan = {backing->plan_id, backing->mounts.mounts, backing->mounts.count,
      backing->order, backing->mounts.count, NULL, 0};
  qa_open_mount_options mount_options = {0};
  backing->mounted = qa_open_mount_plan(&plan, &mount_options);
  if (!backing->mounted) qa_range_error("Out of memory");
  return choices_from_mounts(catalog, product_id, backing);
}
void qa_app_weapon_behavior_choice_list_free(qa_app_weapon_behavior_choice_list *list) {
  if (list == NULL) return;
  for (size_t i = 0; i < list->count; i++) {
    free(list->items[i].id);
    free(list->items[i].title);
    free(list->items[i].unavailable);
  }
  free(list->items);
  native_backing *backing = list->backing;
  if (backing != NULL) {
    /* The service values native_qvm_resolve / native_qvm_profile allocated for each entry. */
    for (size_t i = 0; i < backing->qvm.count; i++) {
      qa_qvm_weapon_profile *profile = backing->qvm.values[i].profile.service_value;
      if (profile != NULL) { qa_qvm_weapon_profile_free(profile); free(profile); }
      qa_resolved_qvm_artifact *artifact = backing->qvm.values[i].artifact.service_value;
      if (artifact != NULL) { qa_resolved_qvm_artifact_destroy(artifact); free(artifact); }
    }
    qa_mounted_qvm_weapon_behavior_list_free(&backing->qvm);
    qa_mounted_native_weapon_behavior_list_free(&backing->native);
    if (backing->has_discovery) qa_mounted_weapon_behavior_discovery_free(&backing->discovery);
    if (backing->has_document) qa_weapon_behavior_document_free(&backing->document);
    if (backing->has_program) qa_compat_qc_program_destroy(&backing->program);
    free(backing->statements);
    free(backing->functions);
    free(backing->globals);
    free(backing->fields);
    free(backing->selections);
    free(backing->layouts);
    free(backing->module_id);
    for (size_t i = 0; i < backing->buffer_count; i++) free(backing->buffers[i]);
    free(backing->buffers);
    if (backing->mounted && !backing->borrowed_mounted) qa_content_mounted_destroy(backing->mounted);
    free(backing->order);
    free(backing->plan_id);
    qa_content_catalog_mount_list_free(&backing->mounts);
    free(backing);
  }
  memset(list, 0, sizeof *list);
}

/* ---- TS prepareApplicationWeaponBehavior ---------------------------------- */
static qa_qvm_weapon_behavior_layout profile_layout(const qa_qvm_weapon_profile *profile) {
  qa_qvm_weapon_behavior_layout layout;
  memset(&layout, 0, sizeof layout);
  layout.entity_stride = (double)profile->entity_stride;
  layout.level_time = (double)profile->level_time;
  layout.allocate = (double)profile->allocate;
  layout.free = (double)profile->free_entry;
  layout.field_inuse = (double)profile->field_inuse;
  layout.field_nextthink = (double)profile->field_nextthink;
  layout.field_think = (double)profile->field_think;
  layout.field_health = (double)profile->field_health;
  layout.fire_abi = profile->fire_abi;
  return layout;
}
static noreturn void prepare_error(const char *message) { qa_raise("Error", 0, 0, "%s", message); }
typedef struct quakec_payload {
  qa_app_prepared_quakec_weapon_behavior record;
  qa_compat_qc_program program;
  qa_app_sim_quakec_resources resources;
} quakec_payload;
typedef struct native_payload {
  qa_app_prepared_native_weapon_behavior record;
  qa_app_sim_prepared_rerelease_guest guest;
} native_payload;
static void abandon_quakec_payload(void *ctx) {
  quakec_payload *payload = ctx;
  qa_compat_qc_program_destroy(&payload->program);
  free(payload);
}
static void release_choices(void *ctx) { qa_app_weapon_behavior_choice_list_free(ctx); }
qa_app_prepared_weapon_behavior qa_app_prepare_application_weapon_behavior(qa_content_catalog *catalog,
    qa_resolved_weapon_behavior_selection selection, qa_app_weapon_behavior_for_content_fn for_content, void *for_content_ctx) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, selection.source.content);
  qa_content_mounted *mounts = for_content(for_content_ctx, product->id);
  native_backing *backing = native_xcalloc(1, sizeof *backing);
  backing->mounted = mounts;
  backing->borrowed_mounted = true;
  qa_app_weapon_behavior_choice_list choices = choices_from_mounts(catalog, product->expectation.id, backing);
  qa_cleanup choices_cleanup;
  qa_cleanup_push(&choices_cleanup, release_choices, &choices);
  const qa_resolved_weapon_behavior_selection *current = NULL;
  for (size_t i = 0; i < choices.count && current == NULL; i++)
    if (choices.items[i].selection != NULL && strcmp(choices.items[i].selection->definition->id, selection.definition->id) == 0)
      current = choices.items[i].selection;
  if (current == NULL || strcmp(current->artifact.requested_path, selection.artifact.requested_path) != 0 ||
      strcmp(current->artifact.digest, selection.artifact.digest) != 0 ||
      strcmp(current->source.provider, selection.source.provider) != 0 ||
      !qa_same_weapon_behavior(*current->definition, *selection.definition))
    prepare_error("Selected weapon behavior differs from its mounted declaration or artifact");
  /* current.component?.kind !== selection.component?.kind */
  if (current->has_component != selection.has_component ||
      (current->has_component && current->component_is_qvm != selection.component_is_qvm))
    prepare_error("Selected behavior component differs from its mounted declaration");
  if (current->has_component && current->component_is_qvm && selection.has_component && selection.component_is_qvm &&
      (strcmp(current->component_abi_profile, selection.component_abi_profile) != 0 ||
       !qa_same_qvm_weapon_layout(*current->component_layout, *selection.component_layout)))
    prepare_error("Selected QVM behavior layout differs from its mounted declaration");
  if (current->has_component && !current->component_is_qvm && selection.has_component && !selection.component_is_qvm &&
      !qa_compat_q2_same_native_weapon_declaration(current->component_declaration, selection.component_declaration))
    prepare_error("Selected native behavior profile differs from its mounted declaration");
  qa_cleanup_pop(&choices_cleanup);
  qa_app_weapon_behavior_choice_list_free(&choices);
  qa_app_prepared_weapon_behavior out;
  memset(&out, 0, sizeof out);
  out.selection = selection;
  out.mounts = mounts;
  if (selection.definition->fire.kind == QA_WEAPON_CALLBACK_QVM) {
    native_backing *entries = native_xcalloc(1, sizeof *entries);
    entries->mounted = mounts;
    entries->borrowed_mounted = true;
    qa_app_weapon_behavior_choice_list owner = {NULL, 0, entries};
    qa_cleanup owner_cleanup;
    qa_cleanup_push(&owner_cleanup, release_choices, &owner);
    qa_catalog_mounted_reader reader = {entries, native_reader_open, NULL};
    qa_qvm_weapon_behavior_services services = {NULL, native_qvm_resolve, native_qvm_profile};
    bool found = false;
    if (!qa_discover_qvm_weapon_behaviors(reader, services, selection.source.provider, &entries->qvm, &found))
      qa_range_error("Out of memory");
    const qa_mounted_qvm_weapon_behavior *entry = NULL;
    for (size_t i = 0; found && i < entries->qvm.count && entry == NULL; i++)
      if (strcmp(entries->qvm.values[i].profile.definition.id, selection.definition->id) == 0) entry = &entries->qvm.values[i];
    const qa_qvm_weapon_profile *profile = entry != NULL ? entry->profile.service_value : NULL;
    const char *abi = entry != NULL && entry->artifact.abi_profile != NULL ? entry->artifact.abi_profile : "q3-modern";
    if (entry == NULL || !selection.has_component || !selection.component_is_qvm ||
        !qa_same_weapon_behavior(entry->profile.definition, *selection.definition) ||
        strcmp(abi, selection.component_abi_profile) != 0 ||
        !qa_same_qvm_weapon_layout(profile_layout(profile), *selection.component_layout))
      prepare_error("QVM behavior declaration changed during preparation");
    qa_app_prepared_qvm_weapon_behavior *record = native_xcalloc(1, sizeof *record);
    record->artifact = entry->artifact.service_value;
    record->profile = profile;
    record->mounts = mounts;
    record->owner = entries;
    qa_cleanup_pop(&owner_cleanup);
    out.kind = "qvm";
    out.payload = record;
    return out;
  }
  qa_content_opened_resource artifact;
  if (!qa_content_mounted_open(mounts, selection.artifact.requested_path, NULL, NULL, &artifact))
    prepare_error("Selected weapon behavior program is missing");
  qa_cleanup artifact_cleanup;
  qa_cleanup_push(&artifact_cleanup, free, artifact.bytes);
  if (selection.definition->fire.kind == QA_WEAPON_CALLBACK_NATIVE_ARTIFACT) {
    if (!selection.has_component || selection.component_is_qvm) prepare_error("Native weapon behavior is missing its retained declaration");
    qa_pe_file pe;
    qa_parse_pe(&pe, artifact.bytes, artifact.byte_length);
    qa_guest_native_abi abi = pe.abi;
    qa_pe_file_free(&pe);
    qa_cleanup_pop(&artifact_cleanup);
    free(artifact.bytes);
    qa_resolved_execution_module module;
    memset(&module, 0, sizeof module);
    module.kind = QA_EXECUTION_MODULE_NATIVE;
    module.owner = selection.source;
    module.artifact = artifact.reference;
    module.role = "server-game";
    module.api.kind = QA_GAME_API_Q2_GAME;
    module.api.as.q2_game.kind = QA_Q2_GAME_API_RERELEASE;
    module.api.as.q2_game.version = 2023;
    module.profile = (qa_native_abi){abi.kind, abi.image, abi.pointer_bytes, abi.call};
    native_payload *payload = native_xcalloc(1, sizeof *payload);
    qa_cleanup payload_cleanup;
    qa_cleanup_push(&payload_cleanup, free, payload);
    qa_app_sim_prepare_rerelease_guest(&module, mounts, &payload->guest);
    qa_cleanup_pop(&payload_cleanup);
    payload->record.prepared = &payload->guest;
    payload->record.declaration = selection.component_declaration;
    payload->record.mounts = mounts;
    out.kind = "rerelease-native";
    out.payload = &payload->record;
    return out;
  }
  if (selection.definition->fire.kind != QA_WEAPON_CALLBACK_QUAKEC) prepare_error("Selected behavior has no executable preparation adapter");
  quakec_payload *payload = native_xcalloc(1, sizeof *payload);
  qa_cleanup payload_cleanup;
  qa_cleanup_push(&payload_cleanup, free, payload);
  if (!qa_compat_qc_load_program(artifact.bytes, artifact.byte_length, NULL, NULL, &payload->program)) qa_range_error("Out of memory");
  qa_cleanup_pop(&payload_cleanup);
  qa_cleanup_pop(&artifact_cleanup);
  free(artifact.bytes);
  qa_cleanup program_cleanup;
  qa_cleanup_push(&program_cleanup, abandon_quakec_payload, payload);
  payload->resources = qa_app_sim_prepare_quakec_resources(&payload->program, mounts, NULL);
  qa_cleanup_pop(&program_cleanup);
  payload->record.program = &payload->program;
  payload->record.resources = &payload->resources;
  payload->record.mounts = mounts;
  out.kind = "quakec";
  out.payload = &payload->record;
  return out;
}
void qa_app_prepared_weapon_behavior_destroy(qa_app_prepared_weapon_behavior *prepared) {
  if (prepared == NULL || prepared->payload == NULL) return;
  if (strcmp(prepared->kind, "qvm") == 0) {
    qa_app_prepared_qvm_weapon_behavior *record = (qa_app_prepared_qvm_weapon_behavior *)prepared->payload;
    qa_app_weapon_behavior_choice_list owner = {NULL, 0, record->owner};
    qa_app_weapon_behavior_choice_list_free(&owner);
    free(record);
  } else if (strcmp(prepared->kind, "rerelease-native") == 0) {
    native_payload *payload = (native_payload *)prepared->payload;
    qa_app_sim_prepared_rerelease_guest_free(&payload->guest);
    free(payload);
  } else {
    quakec_payload *payload = (quakec_payload *)prepared->payload;
    qa_app_sim_quakec_resources_destroy(&payload->resources);
    qa_compat_qc_program_destroy(&payload->program);
    free(payload);
  }
  prepared->payload = NULL;
}
