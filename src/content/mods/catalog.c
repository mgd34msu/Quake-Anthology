/* Port of src/content/mods/catalog.ts. GPL-2.0-or-later. */
#include "qa/content/mods/catalog.h"

#include <ctype.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/declaration.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

/* TS throws plain Error for duplicate components. */
static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

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

/* Mirror of normalizeResourcePath (TS owner:
 * src/content/mounts/paths.ts). See callbacks.c. */
static char *normalize_resource_path(const char *path) {
  size_t n = strlen(path);
  char *out = (char *)malloc(n + 1);
  if (out == NULL) qa_range_error("out of memory");
  for (size_t i = 0; i <= n; i++) out[i] = path[i] == '\\' ? '/' : path[i];
  bool bad = n == 0;
  if (n >= 2 && out[1] == ':' &&
      ((out[0] >= 'a' && out[0] <= 'z') || (out[0] >= 'A' && out[0] <= 'Z')))
    bad = true;
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

static char *format_string(const char *format, const char *a) {
  size_t length = strlen(format) + (a != NULL ? strlen(a) : 0) + 1;
  char *out = (char *)malloc(length);
  if (out == NULL) qa_error("out of memory");
  (void)snprintf(out, length, format, a);
  return out;
}

static qa_mod_selection *read_selections(const qa_save_reader *r, const char *name,
                                         size_t *out_count) {
  qa_save_reader list = field(r, name);
  size_t count = qa_save_reader_list_count(&list);
  qa_mod_selection *rows =
      count > 0 ? (qa_mod_selection *)calloc(count, sizeof *rows) : NULL;
  if (count > 0 && rows == NULL) qa_save_reader_fail(&list, "out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry = item(&list, i);
    const char *text = qa_save_reader_string(&entry);
    if (!qa_read_mod_selection(text, &rows[i]))
      qa_save_reader_fail(&list, "out of memory");
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  *out_count = count;
  return rows;
}

static const qa_mod_program *declaration_program(qa_mod_declaration declaration) {
  switch (declaration.runtime) {
    case QA_MOD_RUNTIME_QUAKEC:
      return declaration.as.quakec != NULL ? &declaration.as.quakec->program : NULL;
    case QA_MOD_RUNTIME_QVM:
      return declaration.as.qvm != NULL ? &declaration.as.qvm->program : NULL;
    case QA_MOD_RUNTIME_NATIVE:
      return declaration.as.native != NULL ? &declaration.as.native->program : NULL;
  }
  return NULL;
}

qa_discovered_gameplay_mod *qa_discover_gameplay_mods(
    const qa_mod_catalog_product *product,
    const qa_mod_mounted_content *mounted, size_t *out_count) {
  static const char *const purposes[] = {"addition", "game-type"};
  *out_count = 0;
  qa_mod_catalog_file document;
  if (!mounted->open(mounted->ctx, "gameplay-mods.json", product->id, &document))
    return NULL;
  qa_save_value *value = qa_parse_json_value(document.bytes, document.length, false);
  if (value == NULL) return NULL;
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) return NULL;
  {
    qa_save_reader version = field(&reader, "version");
    (void)qa_save_reader_literal_number(&version, 1);
    qa_save_reader_destroy(&version);
  }
  qa_save_reader components = field(&reader, "components");
  size_t component_count = qa_save_reader_list_count(&components);
  struct declared {
    const char *id;
    const char *title;
    const char *purpose;
    qa_save_reader reader;
  };
  struct declared *declared = component_count > 0
      ? (struct declared *)calloc(component_count, sizeof *declared) : NULL;
  if (component_count > 0 && declared == NULL)
    qa_save_reader_fail(&components, "out of memory");
  for (size_t i = 0; i < component_count; i++) {
    declared[i].reader = item(&components, i);
    declared[i].id = fstr(&declared[i].reader, "id");
    declared[i].title = fstr(&declared[i].reader, "title");
    qa_save_reader purpose = field(&declared[i].reader, "purpose");
    declared[i].purpose = qa_save_reader_choice_string(&purpose, purposes, 2);
    qa_save_reader_destroy(&purpose);
  }
  /* Source title: `${title} (${FAMILY}${edition === "rerelease" ?
   * " rerelease" : ""})` with FAMILY upper-cased. */
  char family_upper[64];
  size_t family_length = strlen(product->expectation.family);
  if (family_length >= sizeof family_upper) qa_error("out of memory");
  for (size_t i = 0; i <= family_length; i++)
    family_upper[i] = (char)toupper((unsigned char)product->expectation.family[i]);
  size_t source_title_length = strlen(product->expectation.title) + family_length + 16;
  char *source_title = (char *)malloc(source_title_length);
  if (source_title == NULL) qa_error("out of memory");
  (void)snprintf(source_title, source_title_length, "%s (%s%s)",
                 product->expectation.title, family_upper,
                 strcmp(product->expectation.edition, "rerelease") == 0
                     ? " rerelease"
                     : "");
  char *provider = format_string("%s:official", product->expectation.family);

  qa_discovered_gameplay_mod *result = NULL;
  size_t result_count = 0;
  char **seen = NULL;
  size_t seen_count = 0;
  for (size_t i = 0; i < component_count; i++) {
    qa_mod_selection selection = {product->expectation.id, declared[i].id};
    char *key = qa_mod_selection_key(selection);
    if (key == NULL) qa_error("out of memory");
    for (size_t j = 0; j < seen_count; j++) {
      if (strcmp(seen[j], key) == 0) {
        char *message = format_string("Duplicate mod component: %s", key);
        qa_error(message);
      }
    }
    seen = (char **)realloc(seen, (seen_count + 1) * sizeof *seen);
    if (seen == NULL) qa_error("out of memory");
    seen[seen_count++] = key;
    if (strcmp(declared[i].purpose, "game-type") == 0) continue;
    qa_mod_description description;
    memset(&description, 0, sizeof description);
    description.selection = selection;
    description.title = declared[i].title;
    description.source_title = source_title;
    description.source.provider = provider;
    description.source.content = product->id;
    description.purpose = "addition";

    result = (qa_discovered_gameplay_mod *)realloc(
        result, (result_count + 1) * sizeof *result);
    if (result == NULL) qa_error("out of memory");
    qa_discovered_gameplay_mod *slot = &result[result_count++];
    memset(slot, 0, sizeof *slot);

    char *path = normalize_resource_path(fstr(&declared[i].reader, "callbacks"));
    size_t requires_count = 0;
    qa_mod_selection *requires =
        read_selections(&declared[i].reader, "requires", &requires_count);
    size_t conflicts_count = 0;
    qa_mod_selection *conflicts =
        read_selections(&declared[i].reader, "conflicts", &conflicts_count);
    qa_mod_catalog_file file;
    bool opened = mounted->open(mounted->ctx, path, NULL, &file);
    const char *reason = NULL;
    qa_mod_declaration declaration;
    memset(&declaration, 0, sizeof declaration);
    if (!opened) {
      slot->description = description;
      slot->description.is_available = false;
      slot->description.unavailable_reason =
          format_string("Missing callback declaration %s", path);
      free(path);
      continue;
    }
    declaration = qa_parse_gameplay_mod_declaration(file.bytes, file.length);
    const qa_mod_program *program = declaration_program(declaration);
    qa_mod_catalog_file program_file;
    bool program_opened = program != NULL &&
        mounted->open(mounted->ctx, program->path, NULL, &program_file);
    if (!program_opened || strcmp(program_file.digest, program->digest) != 0)
      reason = "Executable differs from its callback declaration";
    free(path);
    if (reason != NULL) {
      slot->description = description;
      slot->description.is_available = false;
      slot->description.unavailable_reason = reason;
      continue;
    }
    slot->is_available = true;
    slot->description = description;
    slot->description.is_available = true;
    slot->description.requires = requires;
    slot->description.requires_count = requires_count;
    slot->description.conflicts = conflicts;
    slot->description.conflicts_count = conflicts_count;
    slot->mod.selection = selection;
    slot->mod.source = description.source;
    slot->mod.title = declared[i].title;
    slot->mod.source_title = source_title;
    slot->mod.requires = requires;
    slot->mod.requires_count = requires_count;
    slot->mod.conflicts = conflicts;
    slot->mod.conflicts_count = conflicts_count;
    slot->mod.declaration = declaration;
    slot->mod.declaration_digest = file.digest;
  }
  for (size_t i = 0; i < component_count; i++)
    qa_save_reader_destroy(&declared[i].reader);
  free(declared);
  for (size_t i = 0; i < seen_count; i++) free(seen[i]);
  free(seen);
  qa_save_reader_destroy(&components);
  qa_save_reader_destroy(&reader);
  *out_count = result_count;
  return result;
}
