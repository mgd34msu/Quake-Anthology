/* Port of src/app/bootstrap/weapon-behavior-tool.ts. GPL-2.0-or-later.
 * The TS catalog discovery + mount-plan opening stays behind
 * host.open_product, which must return the landed qa_content_mounted*
 * for the product (see weapon_behavior_tool.h). The QC/QVM/native
 * bodies below call the landed content/compat/guest services directly,
 * mirroring the TS inline work. Async -> synchronous. Service payloads
 * allocated by the local adapters (resolved QVM artifacts, bridges,
 * profiles, native declarations) are retained for the process
 * lifetime: this is a one-shot CLI tool and the catalog list
 * destructors do not free service payloads. Fatal-UTF-8 TextDecoder
 * checks before JSON.parse are the lenient qa_parse_json_value decode
 * (see persistence/value.h). */
#include "qa/app/bootstrap/weapon_behavior_tool.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/compat/q2/rerelease/weapon_behavior_profile.h"
#include "qa/compat/qc/program.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/compat/qvm/image.h"
#include "qa/compat/qvm/weapon_behavior_profile.h"
#include "qa/content/catalog/native_weapon_behaviors.h"
#include "qa/content/catalog/qvm_weapon_behaviors.h"
#include "qa/content/catalog/weapon_behavior_document.h"
#include "qa/content/catalog/weapon_behaviors.h"
#include "qa/content/mounts.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/guest/pe/format.h"
#include "qa/input/support.h"
#include "qa/persistence/value.h"

/* TS `throw new Error(msg)`: prints the Error string form and aborts. */
static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

static void *xmalloc(size_t size) {
  void *result = malloc(size != 0 ? size : 1);
  if (result == NULL) qa_range_error("Out of memory");
  return result;
}
static void *xcalloc(size_t count, size_t size) {
  void *result = calloc(count != 0 ? count : 1, size);
  if (result == NULL) qa_range_error("Out of memory");
  return result;
}
static char *xformat(const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("Out of memory");
  char *result = xmalloc((size_t)length + 1);
  vsnprintf(result, (size_t)length + 1, format, args);
  va_end(args);
  return result;
}

/* Growable text for JSON.stringify(value, null, 2) output. */
typedef struct text {
  char *data;
  size_t length;
  size_t capacity;
} text;
static void text_add(text *out, const char *bytes, size_t length) {
  if (out->length + length + 1 > out->capacity) {
    size_t next = out->capacity != 0 ? out->capacity : 256;
    while (next < out->length + length + 1) next *= 2;
    char *grown = realloc(out->data, next);
    if (grown == NULL) qa_range_error("Out of memory");
    out->data = grown;
    out->capacity = next;
  }
  memcpy(out->data + out->length, bytes, length);
  out->length += length;
  out->data[out->length] = '\0';
}
static void text_add_cstr(text *out, const char *bytes) {
  text_add(out, bytes, strlen(bytes));
}
static void text_add_indent(text *out, int depth) {
  for (int i = 0; i < depth * 2; i++) text_add(out, " ", 1);
}
static void json_write_string(text *out, const char *value) {
  text_add(out, "\"", 1);
  for (const unsigned char *p = (const unsigned char *)(value != NULL ? value : "");
       *p != '\0'; p++) {
    switch (*p) {
      case '"': text_add_cstr(out, "\\\""); break;
      case '\\': text_add_cstr(out, "\\\\"); break;
      case '\b': text_add_cstr(out, "\\b"); break;
      case '\f': text_add_cstr(out, "\\f"); break;
      case '\n': text_add_cstr(out, "\\n"); break;
      case '\r': text_add_cstr(out, "\\r"); break;
      case '\t': text_add_cstr(out, "\\t"); break;
      default:
        if (*p < 0x20) {
          char escape[8];
          snprintf(escape, sizeof escape, "\\u%04x", *p);
          text_add_cstr(out, escape);
        } else {
          text_add(out, (const char *)p, 1);
        }
    }
  }
  text_add(out, "\"", 1);
}
static void json_write_number(text *out, double value) {
  char buffer[64];
  if (value == trunc(value) && value >= -9007199254740991.0 &&
      value <= 9007199254740991.0) {
    snprintf(buffer, sizeof buffer, "%lld", (long long)value);
  } else {
    /* Shortest round-trip decimal, as JSON.stringify prints. */
    for (int precision = 1; precision <= 17; precision++) {
      snprintf(buffer, sizeof buffer, "%.*g", precision, value);
      if (strtod(buffer, NULL) == value) break;
    }
  }
  text_add_cstr(out, buffer);
}
static void json_write_value(text *out, const qa_save_value *value, int depth) {
  if (value == NULL || value->kind == QA_SAVE_UNDEFINED ||
      value->kind == QA_SAVE_NULL) {
    text_add_cstr(out, "null");
    return;
  }
  switch (value->kind) {
    case QA_SAVE_BOOLEAN:
      text_add_cstr(out, value->boolean ? "true" : "false");
      return;
    case QA_SAVE_NUMBER:
      json_write_number(out, value->number);
      return;
    case QA_SAVE_BIGINT:
      text_add_cstr(out, value->text != NULL ? value->text : "0");
      return;
    case QA_SAVE_STRING:
      json_write_string(out, value->text);
      return;
    case QA_SAVE_BYTES:
      text_add_cstr(out, "null");
      return;
    case QA_SAVE_ARRAY: {
      if (value->item_count == 0) { text_add_cstr(out, "[]"); return; }
      text_add_cstr(out, "[\n");
      for (size_t i = 0; i < value->item_count; i++) {
        text_add_indent(out, depth + 1);
        json_write_value(out, value->items[i], depth + 1);
        if (i + 1 < value->item_count) text_add(out, ",", 1);
        text_add(out, "\n", 1);
      }
      text_add_indent(out, depth);
      text_add(out, "]", 1);
      return;
    }
    case QA_SAVE_OBJECT: {
      size_t visible = 0;
      for (size_t i = 0; i < value->entry_count; i++)
        if (value->entries[i].value != NULL &&
            value->entries[i].value->kind != QA_SAVE_UNDEFINED) visible++;
      if (visible == 0) { text_add_cstr(out, "{}"); return; }
      text_add_cstr(out, "{\n");
      size_t written = 0;
      for (size_t i = 0; i < value->entry_count; i++) {
        if (value->entries[i].value == NULL ||
            value->entries[i].value->kind == QA_SAVE_UNDEFINED) continue;
        text_add_indent(out, depth + 1);
        json_write_string(out, value->entries[i].key);
        text_add_cstr(out, ": ");
        json_write_value(out, value->entries[i].value, depth + 1);
        if (++written < visible) text_add(out, ",", 1);
        text_add(out, "\n", 1);
      }
      text_add_indent(out, depth);
      text_add(out, "}", 1);
      return;
    }
    default:
      text_add_cstr(out, "null");
      return;
  }
}
/* Malloc'd JSON.stringify(value, null, 2). */
static char *json_stringify(const qa_save_value *value) {
  text out = {0};
  json_write_value(&out, value, 0);
  if (out.data == NULL) return xformat("%s", "");
  return out.data;
}

/* qa_save_value builders (OOM aborts; there is no TS counterpart). */
static qa_save_value *value_object(void) {
  qa_save_value *value = qa_save_value_object();
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static qa_save_value *value_array(void) {
  qa_save_value *value = qa_save_value_array();
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
static void value_set_string(qa_save_value *object, const char *key,
                             const char *string) {
  qa_save_value *child = qa_save_value_string(string != NULL ? string : "");
  if (child == NULL || !qa_save_value_set(object, key, child))
    qa_range_error("Out of memory");
}
static void value_set_number(qa_save_value *object, const char *key,
                             double number) {
  qa_save_value *child = qa_save_value_number(number);
  if (child == NULL || !qa_save_value_set(object, key, child))
    qa_range_error("Out of memory");
}
static void value_set(qa_save_value *object, const char *key,
                      qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child))
    qa_range_error("Out of memory");
}
static void value_push(qa_save_value *array, qa_save_value *child) {
  if (child == NULL || !qa_save_value_push(array, child))
    qa_range_error("Out of memory");
}
static qa_save_value *value_copy(const qa_save_value *value) {
  if (value == NULL) {
    qa_save_value *copy = qa_save_value_undefined();
    if (copy == NULL) qa_range_error("Out of memory");
    return copy;
  }
  qa_save_value *copy = NULL;
  switch (value->kind) {
    case QA_SAVE_UNDEFINED: copy = qa_save_value_undefined(); break;
    case QA_SAVE_NULL: copy = qa_save_value_null(); break;
    case QA_SAVE_BOOLEAN: copy = qa_save_value_boolean(value->boolean); break;
    case QA_SAVE_NUMBER: copy = qa_save_value_number(value->number); break;
    case QA_SAVE_BIGINT:
      copy = qa_save_value_bigint_text(value->text != NULL ? value->text : "0");
      break;
    case QA_SAVE_STRING:
      copy = qa_save_value_string(value->text != NULL ? value->text : "");
      break;
    case QA_SAVE_BYTES:
      copy = qa_save_value_bytes(value->bytes, value->byte_length);
      break;
    case QA_SAVE_ARRAY:
      copy = value_array();
      for (size_t i = 0; i < value->item_count; i++)
        value_push(copy, value_copy(value->items[i]));
      return copy;
    case QA_SAVE_OBJECT:
      copy = value_object();
      for (size_t i = 0; i < value->entry_count; i++)
        value_set(copy, value->entries[i].key,
                  value_copy(value->entries[i].value));
      return copy;
  }
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}

/* Mounted content access. Opened byte buffers are retained for the
 * whole tool run (the catalog reader borrows them). */
typedef struct tool_content {
  qa_content_mounted *mounted;
  uint8_t **buffers;
  size_t buffer_count;
  size_t buffer_capacity;
} tool_content;
typedef struct opened_file {
  qa_resolved_resource_reference reference; /* strings borrow the mounter */
  const uint8_t *bytes;
  size_t byte_length;
} opened_file;
static bool content_open(tool_content *content, const char *path,
                         opened_file *out) {
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(content->mounted, path, NULL, NULL, &opened))
    return false;
  if (opened.bytes != NULL) {
    if (content->buffer_count == content->buffer_capacity) {
      size_t next = content->buffer_capacity != 0
                        ? content->buffer_capacity * 2 : 8;
      uint8_t **grown = realloc(content->buffers, next * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      content->buffers = grown;
      content->buffer_capacity = next;
    }
    content->buffers[content->buffer_count++] = opened.bytes;
  }
  out->reference = *opened.reference;
  out->bytes = opened.bytes;
  out->byte_length = opened.byte_length;
  return true;
}
static bool reader_open(void *ctx, const char *path,
                        qa_catalog_opened_resource *out) {
  opened_file opened = {0};
  if (!content_open((tool_content *)ctx, path, &opened)) return false;
  out->reference = opened.reference;
  out->bytes = opened.bytes;
  out->byte_length = opened.byte_length;
  return true;
}
static qa_catalog_mounted_reader content_reader(tool_content *content) {
  qa_catalog_mounted_reader reader = {0};
  reader.ctx = content;
  reader.open = reader_open;
  return reader;
}
static void content_free_buffers(tool_content *content) {
  for (size_t i = 0; i < content->buffer_count; i++) free(content->buffers[i]);
  free(content->buffers);
  content->buffers = NULL;
  content->buffer_count = 0;
  content->buffer_capacity = 0;
}

/* Catalog service adapters over the landed compat services. */
static bool qvm_resolve_artifact(void *ctx, qa_catalog_qvm_artifact *artifact,
                                 const uint8_t *bytes, size_t length) {
  (void)ctx;
  qa_resolved_qvm_artifact *resolved = xcalloc(1, sizeof *resolved);
  qa_qvm_resolve_artifact_options options = {0};
  options.module = artifact->module;
  options.role = artifact->role;
  options.bytes = bytes;
  options.length = length;
  options.has_abi_profile = artifact->abi_profile != NULL;
  options.abi_profile = artifact->abi_profile;
  if (!qa_qvm_resolve_artifact(options, resolved)) {
    free(resolved);
    return false;
  }
  artifact->kind = resolved->is_typescript ? "typescript" : "bytecode";
  artifact->service_value = resolved;
  return true;
}
static bool qvm_read_profile(void *ctx, const qa_save_value *declaration,
                             const qa_catalog_qvm_artifact *artifact,
                             qa_catalog_qvm_weapon_profile *out) {
  (void)ctx;
  const qa_resolved_qvm_artifact *resolved =
      (const qa_resolved_qvm_artifact *)artifact->service_value;
  qa_qvm_artifact_bridge *bridge = xcalloc(1, sizeof *bridge);
  if (!qa_qvm_artifact_bridge_init(bridge, resolved)) {
    free(bridge);
    return false;
  }
  qa_qvm_weapon_profile *profile = xcalloc(1, sizeof *profile);
  qa_qvm_read_weapon_profile(declaration, &bridge->view, profile);
  out->definition = profile->definition;
  out->service_value = profile;
  return true;
}
static qa_qvm_weapon_behavior_services qvm_services(void) {
  qa_qvm_weapon_behavior_services services = {0};
  services.resolve_artifact = qvm_resolve_artifact;
  services.read_profile = qvm_read_profile;
  return services;
}

static bool native_read_declaration(
    void *ctx, const qa_save_value *value, const qa_module_identity *module,
    qa_native_weapon_behavior_declaration *out) {
  (void)ctx;
  return qa_compat_q2_read_native_weapon_declaration(value, module, out);
}
static bool native_validate_image(
    void *ctx, const qa_native_weapon_behavior_declaration *declaration,
    const uint8_t *bytes, size_t length) {
  (void)ctx;
  qa_pe_file image = {0};
  qa_parse_pe(&image, bytes, length);
  qa_compat_q2_pe_section *sections =
      xcalloc(image.section_count, sizeof *sections);
  for (size_t i = 0; i < image.section_count; i++) {
    sections[i].rva = (double)image.sections[i].rva;
    sections[i].mapped_size = (double)image.sections[i].mapped_size;
    sections[i].permissions =
        qa_guest_permissions_name(image.sections[i].permissions);
  }
  qa_compat_q2_pe_file mirror = {0};
  mirror.image_size = (double)image.image_size;
  mirror.pointer_bytes = image.abi.pointer_bytes;
  mirror.sections = sections;
  mirror.section_count = image.section_count;
  qa_compat_q2_validate_native_weapon_image(declaration, image.abi.kind,
                                            mirror);
  free(sections);
  qa_pe_file_free(&image);
  return true;
}
static bool native_weapon_definition(
    void *ctx, qa_module_identity module,
    const qa_native_weapon_behavior_declaration *declaration,
    qa_weapon_behavior_definition *out) {
  (void)ctx;
  return qa_compat_q2_rerelease_weapon_definition(module, declaration, out);
}
static bool native_builtin_declaration(void *ctx, qa_module_identity module,
                                       qa_save_value **out) {
  (void)ctx;
  qa_native_weapon_behavior_declaration declaration = {0};
  if (!qa_compat_q2_built_in_rerelease_weapon_declaration(module,
                                                          &declaration))
    return false;
  char *json = qa_compat_q2_serialize_native_weapon_declaration(&declaration);
  qa_compat_q2_native_weapon_declaration_free(&declaration);
  if (json == NULL) return false;
  *out = qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  free(json);
  return *out != NULL;
}
static qa_native_weapon_behavior_services native_services(void) {
  qa_native_weapon_behavior_services services = {0};
  services.read_declaration = native_read_declaration;
  services.validate_image = native_validate_image;
  services.weapon_definition = native_weapon_definition;
  services.builtin_declaration = native_builtin_declaration;
  return services;
}
/* TS entry.declaration as a JSON value (owned tree). */
static qa_save_value *native_declaration_value(
    const qa_native_weapon_behavior_declaration *declaration) {
  char *json = qa_compat_q2_serialize_native_weapon_declaration(declaration);
  if (json == NULL) qa_range_error("Out of memory");
  qa_save_value *value =
      qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  free(json);
  if (value == NULL) qa_range_error("Out of memory");
  return value;
}
/* TS `new SaveReader(value, path).field("id").string()` (borrowed). */
static const char *save_reader_id(const qa_save_value *value,
                                  const char *path) {
  qa_save_reader reader = {0}, id = {0};
  if (!qa_save_reader_init(&reader, value, path) ||
      !qa_save_reader_field(&reader, "id", &id))
    qa_range_error("Out of memory");
  const char *result = qa_save_reader_string(&id);
  qa_save_reader_destroy(&id);
  qa_save_reader_destroy(&reader);
  return result;
}

typedef void (*print_fn)(void *ctx, const char *text);
typedef struct tool_product {
  const char *expectation_id;
  const char *content_directory;
  const char *product_id;
  const char *edition;
} tool_product;
static void print_saved(print_fn print, void *print_ctx, const char *id,
                        const char *destination, const char *expectation_id) {
  char *message = xformat("Saved %s to %s\nSelect with --weapon-behavior %s/%s\n",
                          id, destination, expectation_id, id);
  print(print_ctx, message);
  free(message);
}
static char *write_document(const qa_app_weapon_behavior_tool_command *command,
                            const tool_product *product, const char *name,
                            qa_save_value *document) {
  char *json = json_stringify(document);
  char *bytes = xformat("%s\n", json);
  free(json);
  char *directory = qa_user_product_directory(command->user_content_root,
                                              product->content_directory);
  if (directory == NULL) qa_range_error("Out of memory");
  char *destination = qa_app_write_behavior_document(directory, name, bytes);
  free(directory);
  free(bytes);
  return destination;
}

/* Adapt the landed QC program to the catalog's mirrored shape. The
 * arrays borrow the program's owned strings; free arrays only. */
typedef struct catalog_program {
  qa_catalog_qc_program view;
  qa_catalog_qc_statement *statements;
  qa_catalog_qc_function *functions;
  qa_catalog_qc_definition *globals;
  qa_catalog_qc_definition *fields;
} catalog_program;
static void catalog_program_init(catalog_program *out,
                                 const qa_compat_qc_program *program) {
  memset(out, 0, sizeof *out);
  out->statements = xcalloc(program->statement_count, sizeof *out->statements);
  for (size_t i = 0; i < program->statement_count; i++) {
    out->statements[i].opcode = (int64_t)program->statements[i].opcode;
    out->statements[i].a = program->statements[i].a;
    out->statements[i].b = program->statements[i].b;
    out->statements[i].c = program->statements[i].c;
  }
  out->functions = xcalloc(program->function_count, sizeof *out->functions);
  for (size_t i = 0; i < program->function_count; i++) {
    out->functions[i].index = program->functions[i].index;
    out->functions[i].name = program->functions[i].name;
    out->functions[i].first_statement = program->functions[i].first_statement;
    out->functions[i].parameter_size_count =
        program->functions[i].parameter_size_count;
  }
  out->globals = xcalloc(program->global_count, sizeof *out->globals);
  for (size_t i = 0; i < program->global_count; i++) {
    out->globals[i].name = program->globals[i].name;
    out->globals[i].type = program->globals[i].type;
    out->globals[i].offset = program->globals[i].offset;
  }
  out->fields = xcalloc(program->field_count, sizeof *out->fields);
  for (size_t i = 0; i < program->field_count; i++) {
    out->fields[i].name = program->fields[i].name;
    out->fields[i].type = program->fields[i].type;
    out->fields[i].offset = program->fields[i].offset;
  }
  out->view.statements = out->statements;
  out->view.statement_count = program->statement_count;
  out->view.functions = out->functions;
  out->view.function_count = program->function_count;
  out->view.globals = out->globals;
  out->view.global_count = program->global_count;
  out->view.fields = out->fields;
  out->view.field_count = program->field_count;
  out->view.initial_globals = program->initial_globals;
  out->view.initial_globals_length = program->initial_globals_length;
  out->view.digest = program->digest;
}
static void catalog_program_free(catalog_program *program) {
  free(program->statements);
  free(program->functions);
  free(program->globals);
  free(program->fields);
  memset(program, 0, sizeof *program);
}

static void run_qc_tool(const qa_app_weapon_behavior_tool_command *command,
                        const tool_product *product, tool_content *content,
                        print_fn print, void *print_ctx) {
  opened_file existing = {0};
  bool have_existing = content_open(content, "weapon-behaviors.json",
                                    &existing);
  qa_weapon_behavior_document document = {0};
  bool have_document = false;
  if (have_existing) {
    if (!qa_read_weapon_behavior_document(existing.bytes,
                                          existing.byte_length, &document))
      qa_range_error("Out of memory");
    have_document = true;
  }
  const char *default_path =
      strcmp(product->edition, "quakeworld") == 0 ? "qwprogs.dat"
                                                  : "progs.dat";
  const char *path = command->artifact != NULL ? command->artifact
                     : have_document && document.has_artifact_path
                         ? document.artifact_path
                         : default_path;
  opened_file artifact = {0};
  if (!content_open(content, path, &artifact))
    qa_error("Mounted behavior artifact is missing: %s", path);
  qa_compat_qc_program program = {0};
  if (!qa_compat_qc_load_program(artifact.bytes, artifact.byte_length, NULL,
                                 NULL, &program))
    qa_range_error("Out of memory");
  catalog_program catalog = {0};
  catalog_program_init(&catalog, &program);
  char *module_id = xformat("weapon-behavior:%s", product->product_id);
  qa_module_identity module = {0};
  module.id = module_id;
  module.artifact_path = path;
  module.digest = artifact.reference.digest;
  module.revision = artifact.reference.digest;
  if (strcmp(command->action, "inspect") == 0) {
    qa_save_value *report = value_object();
    value_set_string(report, "product", product->expectation_id);
    value_set_string(report, "artifact", path);
    value_set_string(report, "digest", program.digest);
    value_set_string(report, "scope",
                     "Callback identities and statement positions apply only "
                     "to this exact artifact digest; roles and activation "
                     "gates require explicit selection.");
    qa_save_value *callbacks = value_array();
    for (size_t i = 0; i < program.function_count; i++) {
      const qa_compat_qc_function *fn = &program.functions[i];
      if (fn->index == 0 || fn->first_statement < 0 ||
          fn->parameter_size_count != 0) continue;
      qa_save_value *entry = value_object();
      value_set_string(entry, "name", fn->name);
      value_set_number(entry, "index", (double)fn->index);
      value_push(callbacks, entry);
    }
    value_set(report, "callbacks", callbacks);
    size_t binding_count = 0;
    qa_qc_trajectory_binding_inspection *bindings =
        qa_inspect_qc_trajectory_bindings(&catalog.view, &binding_count);
    qa_save_value *assignments = value_array();
    for (size_t i = 0; i < binding_count; i++) {
      qa_save_value *entry = value_object();
      /* TS indexes program.functions by array position; a miss is
       * undefined and JSON.stringify drops the key. */
      if (bindings[i].producer_function >= 0 &&
          (size_t)bindings[i].producer_function < program.function_count)
        value_set_string(
            entry, "producer",
            program.functions[bindings[i].producer_function].name);
      if (bindings[i].think_function >= 0 &&
          (size_t)bindings[i].think_function < program.function_count)
        value_set_string(entry, "think",
                         program.functions[bindings[i].think_function].name);
      value_set_number(entry, "statement", (double)bindings[i].statement);
      value_push(assignments, entry);
    }
    free(bindings);
    value_set(report, "thinkAssignments", assignments);
    char *json = json_stringify(report);
    char *output = xformat("%s\n", json);
    free(json);
    print(print_ctx, output);
    free(output);
    qa_save_value_free(report);
    catalog_program_free(&catalog);
    qa_compat_qc_program_destroy(&program);
    free(module_id);
    if (have_document) qa_weapon_behavior_document_free(&document);
    return;
  }
  qa_source_weapon_behavior_metadata declaration = {0};
  declaration.id = command->id;
  declaration.title = command->title;
  declaration.artifact_digest = program.digest;
  declaration.role = command->role;
  declaration.aspect = "trajectory";
  declaration.fire_function = command->fire;
  declaration.activation_function = command->activate;
  qa_weapon_behavior_compatibility validated =
      qa_resolve_qc_weapon_behavior(module, &catalog.view, declaration);
  if (!validated.is_supported) qa_error("%s", validated.reason);
  if (have_document) {
    const char *old_path =
        document.has_artifact_path ? document.artifact_path : default_path;
    if (strcmp(old_path, path) != 0) {
      for (size_t i = 0; i < document.behavior_count; i++)
        if (strcmp(qa_weapon_behavior_entry_id(document.behaviors[i]),
                   command->id) != 0)
          qa_error("Cannot change the artifact for unrelated existing "
                   "behavior declarations");
    }
    qa_mounted_weapon_behavior_discovery discovery = {0};
    if (!qa_discover_qc_weapon_behaviors(content_reader(content), module,
                                         &catalog.view, &discovery))
      qa_range_error("Out of memory");
    qa_mounted_weapon_behavior_discovery_free(&discovery);
  }
  qa_save_value *behaviors = value_array();
  if (have_document) {
    for (size_t i = 0; i < document.behavior_count; i++)
      if (strcmp(qa_weapon_behavior_entry_id(document.behaviors[i]),
                 command->id) != 0)
        value_push(behaviors, value_copy(document.behaviors[i]));
  }
  qa_save_value *entry = value_object();
  value_set_string(entry, "id", declaration.id);
  value_set_string(entry, "title", declaration.title);
  value_set_string(entry, "artifactDigest", declaration.artifact_digest);
  value_set_string(entry, "role", declaration.role);
  value_set_string(entry, "aspect", declaration.aspect);
  value_set_string(entry, "fireFunction", declaration.fire_function);
  if (declaration.activation_function != NULL)
    value_set_string(entry, "activationFunction",
                     declaration.activation_function);
  value_push(behaviors, entry);
  qa_save_value *written = value_object();
  value_set_number(written, "version", 1);
  value_set_string(written, "artifactPath", path);
  value_set(written, "behaviors", behaviors);
  char *destination =
      write_document(command, product, "weapon-behaviors.json", written);
  qa_save_value_free(written);
  print_saved(print, print_ctx, command->id, destination,
              product->expectation_id);
  free(destination);
  catalog_program_free(&catalog);
  qa_compat_qc_program_destroy(&program);
  free(module_id);
  if (have_document) qa_weapon_behavior_document_free(&document);
}

static void run_qvm_tool(const qa_app_weapon_behavior_tool_command *command,
                         const tool_product *product, tool_content *content,
                         print_fn print, void *print_ctx) {
  char *provider = xformat("weapon-behavior:%s", product->product_id);
  qa_catalog_mounted_reader reader = content_reader(content);
  qa_qvm_weapon_behavior_services services = qvm_services();
  if (strcmp(command->action, "inspect") == 0) {
    const char *path =
        command->artifact != NULL ? command->artifact : "vm/qagame.qvm";
    opened_file opened = {0};
    if (!content_open(content, path, &opened))
      qa_error("Mounted QVM artifact is missing: %s", path);
    qa_qvm_image image = {0};
    if (!qa_qvm_parse(opened.bytes, opened.byte_length, path, &image))
      qa_range_error("Out of memory");
    qa_mounted_qvm_weapon_behavior_list declarations = {0};
    bool found = false;
    if (!qa_discover_qvm_weapon_behaviors(reader, services, provider,
                                          &declarations, &found))
      qa_range_error("Out of memory");
    qa_save_value *report = value_object();
    value_set_string(report, "product", product->expectation_id);
    value_set_string(report, "artifact", path);
    value_set_string(report, "digest", opened.reference.digest);
    value_set_string(report, "scope",
                     "Instruction entries are exact bytecode boundaries, "
                     "not inferred symbols or trajectory features. "
                     "Author-declared ABI and private layout are required.");
    value_set_number(report, "instructionCount",
                     (double)image.instruction_count);
    value_set_number(report, "dataBytes",
                     (double)(image.data_length + image.literal_length +
                              image.bss_length));
    qa_save_value *entries = value_array();
    for (size_t i = 0; i < image.instruction_count; i++) {
      if (image.instructions[i].opcode != QA_QVM_OP_ENTER) continue;
      qa_save_value *entry = value_object();
      value_set_number(entry, "instructionIndex", (double)i);
      value_push(entries, entry);
    }
    value_set(report, "entries", entries);
    qa_save_value *declared = value_array();
    if (found) {
      for (size_t i = 0; i < declarations.count; i++) {
        qa_save_value *entry = value_object();
        value_set_string(entry, "id",
                         declarations.values[i].profile.definition.id);
        value_set_string(entry, "artifact",
                         declarations.values[i].resource.requested_path);
        value_set_string(entry, "digest",
                         declarations.values[i].resource.digest);
        value_push(declared, entry);
      }
    }
    value_set(report, "declared", declared);
    char *json = json_stringify(report);
    char *output = xformat("%s\n", json);
    free(json);
    print(print_ctx, output);
    free(output);
    qa_save_value_free(report);
    qa_mounted_qvm_weapon_behavior_list_free(&declarations);
    qa_qvm_image_destroy(&image);
    free(provider);
    return;
  }
  if (strcmp(command->action, "declare-qvm") != 0)
    qa_error("QVM declarations require declare-qvm --profile "
             "MOUNTED_PROFILE_JSON; function names alone do not establish "
             "a private layout");
  opened_file profile_file = {0};
  if (!content_open(content, command->profile, &profile_file))
    qa_error("Mounted QVM author profile is missing: %s", command->profile);
  qa_save_value *declaration = qa_parse_json_value(
      profile_file.bytes, profile_file.byte_length, false);
  if (declaration == NULL) qa_range_error("Out of memory");
  char *owned_path = NULL;
  qa_mounted_qvm_weapon_behavior entry = {0};
  if (!qa_load_qvm_weapon_behavior(reader, services, provider, declaration,
                                   &owned_path, &entry))
    qa_range_error("Out of memory");
  if (command->artifact != NULL &&
      strcmp(command->artifact, entry.resource.requested_path) != 0)
    qa_error("--artifact differs from the authored QVM profile");
  qa_save_value *profiles = value_array();
  opened_file existing = {0};
  if (content_open(content, "qvm-weapon-behaviors.json", &existing)) {
    qa_qvm_weapon_behavior_document document = {0};
    if (!qa_read_qvm_weapon_behavior_document(existing.bytes,
                                              existing.byte_length,
                                              &document))
      qa_range_error("Out of memory");
    for (size_t i = 0; i < document.profile_count; i++) {
      const qa_save_value *old = document.profiles[i];
      char *old_id = xformat("qvm:%s", save_reader_id(old, "profile"));
      bool same = strcmp(old_id, entry.profile.definition.id) == 0;
      free(old_id);
      if (same) continue;
      char *old_path_owned = NULL;
      qa_mounted_qvm_weapon_behavior retained_entry = {0};
      if (!qa_load_qvm_weapon_behavior(reader, services, provider, old,
                                       &old_path_owned, &retained_entry))
        qa_range_error("Out of memory");
      free(old_path_owned);
      value_push(profiles, value_copy(old));
    }
    qa_qvm_weapon_behavior_document_free(&document);
  }
  value_push(profiles, value_copy(declaration));
  qa_save_value *written = value_object();
  value_set_number(written, "version", 1);
  value_set(written, "profiles", profiles);
  char *destination =
      write_document(command, product, "qvm-weapon-behaviors.json", written);
  qa_save_value_free(written);
  print_saved(print, print_ctx, entry.profile.definition.id, destination,
              product->expectation_id);
  free(destination);
  free(owned_path);
  qa_save_value_free(declaration);
  free(provider);
}

static void run_native_tool(
    const qa_app_weapon_behavior_tool_command *command,
    const tool_product *product, tool_content *content, print_fn print,
    void *print_ctx) {
  char *provider = xformat("weapon-behavior:%s", product->product_id);
  qa_catalog_mounted_reader reader = content_reader(content);
  qa_native_weapon_behavior_services services = native_services();
  if (strcmp(command->action, "inspect") == 0) {
    qa_mounted_native_weapon_behavior_list declarations = {0};
    bool found = false;
    if (!qa_discover_native_weapon_behaviors(reader, services, provider,
                                             &declarations, &found))
      qa_range_error("Out of memory");
    const char *path = command->artifact != NULL ? command->artifact
                       : found && declarations.count > 0
                           ? declarations.values[0].resource.requested_path
                           : "game_x64.dll";
    opened_file opened = {0};
    if (!content_open(content, path, &opened))
      qa_error("Mounted native artifact is missing: %s", path);
    qa_pe_file image = {0};
    qa_parse_pe(&image, opened.bytes, opened.byte_length);
    qa_save_value *report = value_object();
    value_set_string(report, "product", product->expectation_id);
    value_set_string(report, "artifact", path);
    value_set_string(report, "digest", opened.reference.digest);
    value_set_string(report, "abi", image.abi.kind);
    value_set_string(report, "scope",
                     "PE sections identify image ranges only. Private "
                     "weapon layout, callback signatures and provisioning "
                     "require an explicit source-backed declaration pinned "
                     "to this artifact.");
    value_set_number(report, "entryPointRva", (double)image.entry_point_rva);
    qa_save_value *sections = value_array();
    for (size_t i = 0; i < image.section_count; i++) {
      qa_save_value *section = value_object();
      value_set_string(section, "name", image.sections[i].name);
      value_set_number(section, "rva", (double)image.sections[i].rva);
      value_set_number(section, "byteLength",
                       (double)image.sections[i].mapped_size);
      value_set_string(section, "permissions",
                       qa_guest_permissions_name(
                           image.sections[i].permissions));
      value_push(sections, section);
    }
    value_set(report, "sections", sections);
    qa_save_value *declared = value_array();
    if (found) {
      for (size_t i = 0; i < declarations.count; i++)
        value_push(declared,
                   native_declaration_value(&declarations.values[i]
                                                 .declaration));
    }
    value_set(report, "declared", declared);
    char *json = json_stringify(report);
    char *output = xformat("%s\n", json);
    free(json);
    print(print_ctx, output);
    free(output);
    qa_save_value_free(report);
    qa_pe_file_free(&image);
    qa_mounted_native_weapon_behavior_list_free(&declarations);
    free(provider);
    return;
  }
  if (strcmp(command->action, "declare-native") != 0)
    qa_error("Native declarations require declare-native --profile "
             "MOUNTED_PROFILE_JSON");
  opened_file profile_file = {0};
  if (!content_open(content, command->profile, &profile_file))
    qa_error("Mounted native author profile is missing: %s",
             command->profile);
  qa_save_value *value = qa_parse_json_value(
      profile_file.bytes, profile_file.byte_length, false);
  if (value == NULL) qa_range_error("Out of memory");
  qa_mounted_native_weapon_behavior entry = {0};
  if (!qa_load_native_weapon_behavior(reader, services, provider, value,
                                      &entry))
    qa_range_error("Out of memory");
  if (command->artifact != NULL &&
      strcmp(command->artifact, entry.resource.requested_path) != 0)
    qa_error("--artifact differs from the authored native profile");
  qa_save_value *profiles = value_array();
  opened_file existing = {0};
  if (content_open(content, "native-weapon-behaviors.json", &existing)) {
    qa_native_weapon_behavior_document document = {0};
    if (!qa_read_native_weapon_behavior_document(
            existing.bytes, existing.byte_length, &document))
      qa_range_error("Out of memory");
    for (size_t i = 0; i < document.profile_count; i++) {
      const qa_save_value *old = document.profiles[i];
      if (strcmp(save_reader_id(old, "native-profile"),
                 entry.definition.id) == 0) continue;
      qa_mounted_native_weapon_behavior retained_entry = {0};
      if (!qa_load_native_weapon_behavior(reader, services, provider, old,
                                          &retained_entry))
        qa_range_error("Out of memory");
      value_push(profiles,
                 native_declaration_value(&retained_entry.declaration));
    }
    qa_native_weapon_behavior_document_free(&document);
  }
  value_push(profiles, native_declaration_value(&entry.declaration));
  qa_save_value *written = value_object();
  value_set_number(written, "version", 1);
  value_set(written, "profiles", profiles);
  char *destination = write_document(command, product,
                                     "native-weapon-behaviors.json", written);
  qa_save_value_free(written);
  print_saved(print, print_ctx, entry.definition.id, destination,
              product->expectation_id);
  free(destination);
  qa_save_value_free(value);
  free(provider);
}




void qa_app_run_weapon_behavior_tool(qa_app_weapon_behavior_tool_host host,
                                     qa_app_weapon_behavior_tool_command command,
                                     void (*print)(void *ctx, const char *text),
                                     void *print_ctx) {
  if (strcmp(command.action, "help") == 0) {
    print(print_ctx, qa_app_weapon_behavior_tool_help);
    return;
  }
  const char *family = NULL, *edition = NULL, *expectation_id = NULL,
             *content_directory = NULL, *product_id = NULL;
  const void *opened = host.open_product(
      host.ctx, &command, &family, &edition, &expectation_id,
      &content_directory, &product_id);
  tool_content content = {0};
  content.mounted = (qa_content_mounted *)(const void *)opened;
  tool_product product = {0};
  product.expectation_id = expectation_id;
  product.content_directory = content_directory;
  product.product_id = product_id;
  product.edition = edition;
  if (strcmp(family, "q2") == 0 && strcmp(edition, "rerelease") != 0) {
    host.close_product(host.ctx, opened);
    qa_error("Native weapon declarations currently require the Q2 rerelease "
             "API2023 Windows x64 adapter");
  }
  if (strcmp(family, "q2") == 0) {
    run_native_tool(&command, &product, &content, print, print_ctx);
  } else if (strcmp(family, "q3") == 0) {
    run_qvm_tool(&command, &product, &content, print, print_ctx);
  } else {
    if (strcmp(command.action, "declare-qvm") == 0 ||
        strcmp(command.action, "declare-native") == 0) {
      host.close_product(host.ctx, opened);
      qa_error("%s requires its matching Q3 or Q2 rerelease provider",
               command.action);
    }
    run_qc_tool(&command, &product, &content, print, print_ctx);
  }
  content_free_buffers(&content);
  host.close_product(host.ctx, opened);
}

char *qa_app_write_behavior_document(const char *directory, const char *name,
                                     const char *bytes) {
  /* mkdir -p (single level walk). */
  char *walk = qa_input_dup(directory);
  if (!walk) qa_range_error("Out of memory");
  for (char *p = walk + 1; *p; p++) {
    if (*p == '/') { *p = '\0'; mkdir(walk, 0777); *p = '/'; }
  }
  mkdir(walk, 0777);
  free(walk);
  size_t dir_len = strlen(directory), name_len = strlen(name);
  char *destination = malloc(dir_len + 1 + name_len + 1);
  char *temporary = malloc(dir_len + 64);
  if (!destination || !temporary) qa_range_error("Out of memory");
  snprintf(destination, dir_len + 1 + name_len + 1, "%s/%s", directory, name);
  snprintf(temporary, dir_len + 64, "%s/.weapon-behaviors-%ld.tmp", directory,
           (long)getpid());
  int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) qa_range_error("Cannot write behavior document: %s", strerror(errno));
  size_t len = strlen(bytes), written = 0;
  while (written < len) {
    ssize_t n = write(fd, bytes + written, len - written);
    if (n < 0) {
      close(fd);
      unlink(temporary);
      qa_range_error("Cannot write behavior document: %s", strerror(errno));
    }
    written += (size_t)n;
  }
  fsync(fd);
  close(fd);
  if (rename(temporary, destination) != 0) {
    unlink(temporary);
    qa_range_error("Cannot write behavior document: %s", strerror(errno));
  }
  unlink(temporary);
  free(temporary);
  return destination;
}
