/*
 * Port of src/content/mods/callbacks.ts: QuakeC gameplay-mod
 * declaration readers. Returned declarations borrow strings from
 * the save tree / parsed JSON tree; list fields are malloc'd
 * arrays the caller frees (the declaration itself is malloc'd,
 * NULL only on OOM at the top level). readModCallbacks keeps the
 * parsed tree alive by leaking it (declarations borrow from it),
 * mirroring the TS where the parsed value lives as long as the
 * declaration. Small readers from still-unported modules
 * (normalizeResourcePath, TS owner content/mounts/paths.ts;
 * readItemIconDeclaration, content/item-icon.ts;
 * readHeldWeaponDeclaration + readModelGrip, content/held-weapon.ts
 * / content/model-attachment.ts; readQcWeaponStageDeclaration,
 * content/q1/quakec/weapon-stage-declaration.ts) are mirrored as
 * statics in callbacks.c until those modules land.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_CALLBACKS_H
#define QA_CONTENT_MODS_CALLBACKS_H

#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/mod_callbacks.h"
#include "qa/persistence/value.h"

qa_mod_callback_declaration *qa_read_mod_callbacks(const uint8_t *bytes,
                                                   size_t length);
qa_mod_callback_declaration *qa_read_quakec_mod_declaration(
    const qa_save_reader *reader);
/* TS readQcCombat; result is malloc'd, NULL only on OOM. */
qa_mod_qc_combat *qa_read_qc_combat(const qa_save_reader *reader);

#endif
