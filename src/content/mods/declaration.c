/* Port of src/content/mods/declaration.ts. GPL-2.0-or-later. */
#include "qa/content/mods/declaration.h"

#include <stdlib.h>
#include <string.h>

#include "qa/content/mods/callbacks.h"
#include "qa/content/mods/native_callbacks.h"
#include "qa/content/mods/qvm_callbacks.h"

struct qa_mod_declaration_owner {
  void **blocks;
  size_t count, capacity;
  qa_save_value *value;
};
static _Thread_local qa_mod_declaration_owner *active_owner;

static void owner_add(qa_mod_declaration_owner *owner, void *block) {
  if (owner->count == owner->capacity) {
    size_t capacity = owner->capacity == 0 ? 32 : owner->capacity * 2;
    void **grown = realloc(owner->blocks, capacity * sizeof *grown);
    if (grown == NULL) { free(block); qa_range_error("out of memory"); }
    owner->blocks = grown;
    owner->capacity = capacity;
  }
  owner->blocks[owner->count++] = block;
}
void qa_mod_declaration_owner_free(qa_mod_declaration_owner *owner) {
  if (owner == NULL) return;
  for (size_t i = owner->count; i-- > 0;) free(owner->blocks[i]);
  free(owner->blocks);
  if (owner->value != NULL) qa_save_value_free(owner->value);
  free(owner);
}
static void scope_unwind(void *arg) {
  qa_mod_declaration_scope *scope = arg;
  active_owner = scope->previous;
  qa_mod_declaration_owner_free(scope->owner);
  scope->owner = NULL;
}
void qa_mod_declaration_scope_begin(qa_mod_declaration_scope *scope) {
  scope->owner = calloc(1, sizeof *scope->owner);
  if (scope->owner == NULL) qa_range_error("out of memory");
  scope->previous = active_owner;
  active_owner = scope->owner;
  qa_cleanup_push(&scope->cleanup, scope_unwind, scope);
}
qa_mod_declaration_owner *qa_mod_declaration_scope_end(qa_mod_declaration_scope *scope) {
  qa_cleanup_pop(&scope->cleanup);
  active_owner = scope->previous;
  return scope->owner;
}
void qa_mod_declaration_adopt(void *block) {
  if (block != NULL && active_owner != NULL) owner_add(active_owner, block);
}
void *qa_mod_declaration_malloc(size_t size) {
  void *block = malloc(size == 0 ? 1 : size);
  qa_mod_declaration_adopt(block);
  return block;
}
void *qa_mod_declaration_calloc(size_t count, size_t size) {
  void *block = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  qa_mod_declaration_adopt(block);
  return block;
}
void qa_mod_declaration_owner_adopt_value(qa_mod_declaration_owner *owner, qa_save_value *value) {
  if (owner->value != NULL) qa_save_value_free(owner->value);
  owner->value = value;
}
void qa_mod_callback_declaration_free(qa_mod_callback_declaration *declaration) {
  if (declaration != NULL) qa_mod_declaration_owner_free(declaration->owner);
}
void qa_qvm_mod_callback_declaration_free(qa_qvm_mod_callback_declaration *declaration) {
  if (declaration != NULL) qa_mod_declaration_owner_free(declaration->owner);
}
void qa_native_mod_declaration_free(qa_native_mod_declaration *declaration) {
  if (declaration != NULL) qa_mod_declaration_owner_free(declaration->owner);
}
void qa_mod_declaration_free(qa_mod_declaration *declaration) {
  if (declaration == NULL) return;
  switch (declaration->runtime) {
    case QA_MOD_RUNTIME_QUAKEC:
      qa_mod_callback_declaration_free((qa_mod_callback_declaration *)declaration->as.quakec);
      break;
    case QA_MOD_RUNTIME_QVM:
      qa_qvm_mod_callback_declaration_free((qa_qvm_mod_callback_declaration *)declaration->as.qvm);
      break;
    case QA_MOD_RUNTIME_NATIVE:
      qa_native_mod_declaration_free((qa_native_mod_declaration *)declaration->as.native);
      break;
  }
  memset(declaration, 0, sizeof *declaration);
}

void qa_mod_declaration_free_parsed_value(void *value) { qa_save_value_free(value); }
const void *qa_mod_declaration_present(const void *rows) {
  if (rows != NULL) return rows;
  void *marker = qa_mod_declaration_malloc(1);
  if (marker == NULL) qa_range_error("out of memory");
  return marker;
}
static void free_parsed_value(void *value) { qa_save_value_free(value); }
static qa_mod_declaration_owner *declaration_owner(qa_mod_declaration declaration) {
  switch (declaration.runtime) {
    case QA_MOD_RUNTIME_QUAKEC: return declaration.as.quakec != NULL ? declaration.as.quakec->owner : NULL;
    case QA_MOD_RUNTIME_QVM: return declaration.as.qvm != NULL ? declaration.as.qvm->owner : NULL;
    case QA_MOD_RUNTIME_NATIVE: return declaration.as.native != NULL ? declaration.as.native->owner : NULL;
  }
  return NULL;
}

qa_mod_declaration qa_read_gameplay_mod_declaration(const qa_save_reader *reader) {
  static const char *const runtimes[] = {"quakec", "qvm", "native"};
  qa_save_reader runtime;
  if (!qa_save_reader_field(reader, "runtime", &runtime))
    qa_save_reader_fail(reader, "out of memory");
  const char *kind = qa_save_reader_choice_string(&runtime, runtimes, 3);
  qa_save_reader_destroy(&runtime);
  qa_mod_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  if (strcmp(kind, "quakec") == 0) {
    declaration.runtime = QA_MOD_RUNTIME_QUAKEC;
    declaration.as.quakec = qa_read_quakec_mod_declaration(reader);
  } else if (strcmp(kind, "qvm") == 0) {
    declaration.runtime = QA_MOD_RUNTIME_QVM;
    declaration.as.qvm = qa_read_qvm_mod_declaration(reader);
  } else {
    declaration.runtime = QA_MOD_RUNTIME_NATIVE;
    declaration.as.native = qa_read_native_mod_declaration(reader);
  }
  return declaration;
}

qa_mod_declaration qa_parse_gameplay_mod_declaration(const uint8_t *bytes,
                                                     size_t length) {
  /* TS: JSON.parse(new TextDecoder("utf-8", {fatal:true}).decode(bytes));
   * the declaration's owner keeps the parsed tree its strings borrow. */
  qa_mod_declaration declaration;
  memset(&declaration, 0, sizeof declaration);
  qa_save_value *value = qa_parse_json_value(bytes, length, false);
  if (value == NULL) return declaration;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free_parsed_value, value);
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, NULL)) { qa_cleanup_pop(&cleanup); qa_save_value_free(value); return declaration; }
  declaration = qa_read_gameplay_mod_declaration(&reader);
  qa_save_reader_destroy(&reader);
  qa_cleanup_pop(&cleanup);
  qa_mod_declaration_owner *owner = declaration_owner(declaration);
  if (owner != NULL) qa_mod_declaration_owner_adopt_value(owner, value);
  else qa_save_value_free(value);
  return declaration;
}
