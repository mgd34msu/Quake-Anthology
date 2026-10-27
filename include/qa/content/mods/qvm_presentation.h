/*
 * Port of src/content/mods/qvm-presentation.ts. Returned
 * declarations borrow strings from the save/JSON tree; sub-objects
 * and lists are malloc'd (top-level NULL only on OOM).
 * normalizeResourcePath (TS owner content/mounts/paths.ts) is
 * mirrored as a static until that module lands.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_QVM_PRESENTATION_H
#define QA_CONTENT_MODS_QVM_PRESENTATION_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/qvm_mod_presentation.h"
#include "qa/persistence/value.h"

qa_qvm_mod_presentation_declaration *qa_read_qvm_mod_presentation(
    const uint8_t *bytes, size_t length);
qa_qvm_mod_presentation_declaration *qa_read_qvm_mod_presentation_declaration(
    const qa_save_reader *reader);

#endif
