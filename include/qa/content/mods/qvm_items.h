/*
 * Port of src/content/mods/qvm-items.ts. readQvmModItems takes
 * the QVM source-call reader as fn+ctx (TS readCall parameter).
 * Returned structs borrow strings from the save tree; lists and
 * the items object are malloc'd (items NULL only on OOM).
 * readItemIconDeclaration / readHeldWeaponDeclaration /
 * normalizeResourcePath mirrors: see callbacks.c header note.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_QVM_ITEMS_H
#define QA_CONTENT_MODS_QVM_ITEMS_H

#include "qa/contracts/qvm_mod_callbacks.h"
#include "qa/contracts/qvm_mod_items.h"
#include "qa/persistence/value.h"

qa_qvm_item_storage qa_read_qvm_item_storage(const qa_save_reader *reader);
qa_qvm_mod_items *qa_read_qvm_mod_items(
    const qa_save_reader *reader,
    void (*read_call)(void *ctx, const qa_save_reader *reader, void *out),
    void *read_ctx);

#endif
