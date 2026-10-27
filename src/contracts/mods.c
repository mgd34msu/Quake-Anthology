/* Port of src/contracts/mods.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/mods.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* TS throws plain Error in this module (not RangeError); the C port
 * aborts printing the identical message under the Error class name. */
static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

static bool is_alnum(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* product: ^[a-zA-Z0-9][a-zA-Z0-9._+-]*$ */
static bool valid_product(const char *value, size_t length) {
  if (length == 0 || !is_alnum(value[0])) return false;
  for (size_t i = 1; i < length; i++) {
    char c = value[i];
    if (!is_alnum(c) && c != '.' && c != '_' && c != '+' && c != '-') return false;
  }
  return true;
}

/* id: ^[a-zA-Z0-9][a-zA-Z0-9._+:/-]*$ */
static bool valid_component_id(const char *value, size_t length) {
  if (length == 0 || !is_alnum(value[0])) return false;
  for (size_t i = 1; i < length; i++) {
    char c = value[i];
    if (!is_alnum(c) && c != '.' && c != '_' && c != '+' && c != ':' && c != '/' &&
        c != '-') {
      return false;
    }
  }
  return true;
}

static void validate_selection(const char *product, size_t product_length,
                               const char *id, size_t id_length) {
  if (!valid_product(product, product_length) ||
      !valid_component_id(id, id_length)) {
    qa_error("Mod selection requires a package and an authored component ID");
  }
}

char *qa_mod_selection_key(qa_mod_selection selection) {
  size_t product_length = selection.product != NULL ? strlen(selection.product) : 0;
  size_t id_length = selection.id != NULL ? strlen(selection.id) : 0;
  validate_selection(selection.product != NULL ? selection.product : "", product_length,
                     selection.id != NULL ? selection.id : "", id_length);
  char *out = (char *)malloc(product_length + 1 + id_length + 1);
  if (out == NULL) return NULL;
  (void)snprintf(out, product_length + 1 + id_length + 1, "%s/%s", selection.product,
                 selection.id);
  return out;
}

bool qa_read_mod_selection(const char *value, qa_mod_selection *out) {
  const char *slash = value != NULL ? strchr(value, '/') : NULL;
  /* TS: slash < 1 short-circuits before the key check. */
  if (slash == NULL || slash == value) {
    qa_error("Mod selection must be PRODUCT/COMPONENT_ID");
  }
  size_t product_length = (size_t)(slash - value);
  const char *id = slash + 1;
  size_t id_length = strlen(id);
  /* TS recomputes modSelectionKey(selection), which throws its own
   * message for invalid components; rejoining valid components
   * always reproduces `value`, as in TS. */
  validate_selection(value, product_length, id, id_length);
  char *product = (char *)malloc(product_length + 1);
  char *component = (char *)malloc(id_length + 1);
  if (product == NULL || component == NULL) {
    free(product);
    free(component);
    out->product = NULL;
    out->id = NULL;
    return false;
  }
  memcpy(product, value, product_length);
  product[product_length] = '\0';
  memcpy(component, id, id_length + 1);
  out->product = product;
  out->id = component;
  return true;
}

void qa_mod_selection_free(qa_mod_selection *selection) {
  free((void *)selection->product);
  free((void *)selection->id);
  selection->product = NULL;
  selection->id = NULL;
}

/* TS encodeURIComponent (see content.c for the unescaped set). */
static bool uri_unescaped(unsigned char c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
  switch (c) {
    case '-': case '_': case '.': case '!': case '~':
    case '*': case '\'': case '(': case ')':
      return true;
    default:
      return false;
  }
}

char *qa_mod_instance_provider(qa_mod_selection selection) {
  char *key = qa_mod_selection_key(selection);
  if (key == NULL) return NULL;
  size_t length = 4;
  for (const unsigned char *p = (const unsigned char *)key; *p != '\0'; p++) {
    length += uri_unescaped(*p) ? 1 : 3;
  }
  char *out = (char *)malloc(length + 1);
  if (out == NULL) {
    free(key);
    return NULL;
  }
  static const char hex_digits[] = "0123456789ABCDEF";
  char *cursor = out;
  memcpy(cursor, "mod:", 4);
  cursor += 4;
  for (const unsigned char *p = (const unsigned char *)key; *p != '\0'; p++) {
    if (uri_unescaped(*p)) {
      *cursor++ = (char)*p;
    } else {
      *cursor++ = '%';
      *cursor++ = hex_digits[*p >> 4];
      *cursor++ = hex_digits[*p & 0xf];
    }
  }
  *cursor = '\0';
  free(key);
  return out;
}

static bool string_equal(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}

bool qa_same_mod_identity(const qa_mod_identity *left, const qa_mod_identity *right) {
  /* TS compares modSelectionKey() output, which also validates both
   * selections (throwing on invalid ones); keys are equal exactly
   * when product and id are, since product cannot contain '/'. */
  validate_selection(left->selection.product != NULL ? left->selection.product : "",
                     left->selection.product != NULL ? strlen(left->selection.product) : 0,
                     left->selection.id != NULL ? left->selection.id : "",
                     left->selection.id != NULL ? strlen(left->selection.id) : 0);
  validate_selection(right->selection.product != NULL ? right->selection.product : "",
                      right->selection.product != NULL ? strlen(right->selection.product) : 0,
                      right->selection.id != NULL ? right->selection.id : "",
                      right->selection.id != NULL ? strlen(right->selection.id) : 0);
  if (!string_equal(left->selection.product, right->selection.product) ||
      !string_equal(left->selection.id, right->selection.id) ||
      !string_equal(left->source.provider, right->source.provider) ||
      !string_equal(left->source.content, right->source.content) ||
      !string_equal(left->declaration_digest, right->declaration_digest) ||
      left->module_count != right->module_count ||
      left->provider_count != right->provider_count) {
    return false;
  }
  for (size_t i = 0; i < left->module_count; i++) {
    const qa_module_identity *module = &left->modules[i];
    const qa_module_identity *other = &right->modules[i];
    if (!string_equal(module->id, other->id) ||
        !string_equal(module->artifact_path, other->artifact_path) ||
        !string_equal(module->digest, other->digest) ||
        !string_equal(module->revision, other->revision)) {
      return false;
    }
  }
  for (size_t i = 0; i < left->provider_count; i++) {
    const qa_provider_checkpoint_ref *provider = &left->providers[i];
    const qa_provider_checkpoint_ref *other = &right->providers[i];
    if (!string_equal(provider->provider, other->provider) ||
        !string_equal(provider->schema, other->schema) ||
        provider->version != other->version) {
      return false;
    }
  }
  return true;
}
