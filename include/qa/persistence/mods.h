/*
 * Port of src/persistence/mods.ts: boundary readers for mod
 * identities and mod session checkpoints. Returned structs borrow
 * strings/bytes from the checkpoint tree; list fields are malloc'd
 * arrays the caller frees (elements stay borrowed, as in
 * execution.h/shared.h). qa_read_gameplay_mod's dependency
 * selections are the exception: TS readModSelection builds fresh
 * strings, so requires/conflicts elements own malloc'd
 * product/id pairs (release each with qa_mod_selection_free).
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_MODS_H
#define QA_PERSISTENCE_MODS_H

#include "qa/contracts/mods.h"
#include "qa/persistence/value.h"

qa_resolved_gameplay_mod qa_read_gameplay_mod(const qa_save_reader *reader);
qa_mod_identity qa_read_mod_identity(const qa_save_reader *reader);
struct qa_mod_session_checkpoint qa_read_mod_session(const qa_save_reader *reader);

#endif
