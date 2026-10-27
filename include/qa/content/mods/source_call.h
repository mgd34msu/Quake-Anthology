/*
 * Port of src/content/mods/source-call.ts: readModSourceValue /
 * readModSourceCall over a SaveReader. Returned strings borrow
 * from the save tree; argument/global lists are malloc'd arrays
 * the caller frees (NULL arguments on OOM is signalled only by
 * qa_read_mod_source_call returning false via its out-param form
 * -- the value reader aborts like the TS reader on OOM through
 * the shared "out of memory" fail used by other ports).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_SOURCE_CALL_H
#define QA_CONTENT_MODS_SOURCE_CALL_H

#include "qa/contracts/mod_callbacks.h"
#include "qa/persistence/value.h"

qa_mod_callback_value qa_read_mod_source_value(const qa_save_reader *reader);
qa_mod_source_call qa_read_mod_source_call(const qa_save_reader *reader);

#endif
