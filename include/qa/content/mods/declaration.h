/*
 * Port of src/content/mods/declaration.ts: runtime dispatch over
 * the quakec/qvm/native declaration readers. The returned
 * qa_mod_declaration points at the malloc'd concrete declaration
 * (NULL union member only on OOM). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_DECLARATION_H
#define QA_CONTENT_MODS_DECLARATION_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/mods.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"

qa_mod_declaration qa_read_gameplay_mod_declaration(const qa_save_reader *reader);
qa_mod_declaration qa_parse_gameplay_mod_declaration(const uint8_t *bytes,
                                                     size_t length);

/*
 * C ownership for declarations (TS GC). Every block a declaration reader
 * allocates while a scope is open joins that scope's owner, which the
 * concrete declaration keeps in its `owner` field; a parsed-from-bytes
 * declaration's owner also holds the JSON tree its strings borrow. A
 * declaration read from a caller's save tree still borrows that tree.
 * Outside a scope the allocators are plain malloc/calloc the caller owns.
 * A raise inside a scope frees the owner (qa_cleanup) and closes the scope.
 */
typedef struct qa_mod_declaration_owner qa_mod_declaration_owner;
typedef struct qa_mod_declaration_scope {
  qa_mod_declaration_owner *owner;
  qa_mod_declaration_owner *previous;
  qa_cleanup cleanup;
} qa_mod_declaration_scope;
void qa_mod_declaration_scope_begin(qa_mod_declaration_scope *scope);
/* Closes the scope and returns its owner (never NULL). */
qa_mod_declaration_owner *qa_mod_declaration_scope_end(qa_mod_declaration_scope *scope);
void *qa_mod_declaration_malloc(size_t size);
void *qa_mod_declaration_calloc(size_t count, size_t size);
/* Hands a malloc'd block to the open scope's owner (no-op outside one). */
void qa_mod_declaration_adopt(void *block);
/* The owner frees value with qa_save_value_free. */
void qa_mod_declaration_owner_adopt_value(qa_mod_declaration_owner *owner, qa_save_value *value);
void qa_mod_declaration_owner_free(qa_mod_declaration_owner *owner);
/* qa_save_value_free with the qa_cleanup signature, for parse-from-bytes readers. */
void qa_mod_declaration_free_parsed_value(void *value);
/* Free a declaration and everything its reader allocated; NULL-safe. */
void qa_mod_callback_declaration_free(qa_mod_callback_declaration *declaration);
void qa_qvm_mod_callback_declaration_free(qa_qvm_mod_callback_declaration *declaration);
void qa_native_mod_declaration_free(qa_native_mod_declaration *declaration);
void qa_mod_declaration_free(qa_mod_declaration *declaration);
/* An optional list the TS reader kept is a member of the declaration even when
 * it is empty (JSON.stringify writes it): readers store `rows` when non-NULL and
 * otherwise a one-byte block of the open scope, so NULL means absent. */
const void *qa_mod_declaration_present(const void *rows);

#endif
