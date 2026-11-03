/*
 * The value of a gameplay-mod declaration as TypeScript holds it: the object
 * the readers in src/content/mods/callbacks.ts, qvm-callbacks.ts and
 * native-callbacks.ts build. TS compares and saves declarations with
 * JSON.stringify / encodeCheckpointValue of that object, so its key order,
 * optional members and numbers are behavior; these writers rebuild the same
 * tree from the C declaration (TS has no writer: it serializes the object it
 * read). A member the reader leaves out is left out; an optional list the
 * reader kept, even empty, is written. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_DECLARATION_WRITER_H
#define QA_CONTENT_MODS_DECLARATION_WRITER_H

#include <stddef.h>

#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/mods.h"
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/persistence/value.h"

/* Owned trees; out of memory aborts. */
qa_save_value *qa_quakec_mod_declaration_value(const qa_mod_callback_declaration *declaration);
qa_save_value *qa_qvm_mod_declaration_value(const qa_qvm_mod_callback_declaration *declaration);
qa_save_value *qa_native_mod_declaration_value(const qa_native_mod_declaration *declaration);
qa_save_value *qa_gameplay_mod_declaration_value(qa_mod_declaration declaration);

/* JSON.stringify of a plain value tree: insertion-ordered keys, -0 as "0",
 * NaN and infinities as "null", lone surrogates (WTF-8 in C strings) as
 * \udXXX escapes. Malloc'd and NUL-terminated; out of memory aborts. */
char *qa_json_stringify_value(const qa_save_value *value, size_t *out_length);
/* JSON.stringify(declaration). Malloc'd; out of memory aborts. */
char *qa_gameplay_mod_declaration_json(qa_mod_declaration declaration);

#endif
