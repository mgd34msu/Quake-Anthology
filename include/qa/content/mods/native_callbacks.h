/*
 * Port of src/content/mods/native-callbacks.ts. Returned
 * declarations borrow strings from the save/JSON tree; lists and
 * the declaration are malloc'd (NULL only on top-level OOM).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_NATIVE_CALLBACKS_H
#define QA_CONTENT_MODS_NATIVE_CALLBACKS_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/native_mod_callbacks.h"
#include "qa/persistence/value.h"

qa_native_mod_declaration *qa_read_native_mod_callbacks(const uint8_t *bytes,
                                                        size_t length);
qa_native_mod_declaration *qa_read_native_mod_declaration(
    const qa_save_reader *reader);

#endif
