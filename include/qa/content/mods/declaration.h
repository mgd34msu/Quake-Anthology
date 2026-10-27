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
#include "qa/persistence/value.h"

qa_mod_declaration qa_read_gameplay_mod_declaration(const qa_save_reader *reader);
qa_mod_declaration qa_parse_gameplay_mod_declaration(const uint8_t *bytes,
                                                     size_t length);

#endif
