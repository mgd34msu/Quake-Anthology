/*
 * Port of src/persistence/mods.ts: boundary readers for mod
 * identities and mod session checkpoints. Returned structs borrow
 * strings/bytes from the checkpoint tree; list fields are malloc'd
 * arrays the caller frees (elements stay borrowed, as in
 * execution.h/shared.h).
 *
 * BLOCKED: qa_read_gameplay_mod (TS readGameplayMod) is not yet
 * ported. It needs readGameplayModDeclaration from
 * src/content/mods/declaration.ts, which has no C header
 * (include/qa/content/ does not exist). The readers below have no
 * such dependency and are complete.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_MODS_H
#define QA_PERSISTENCE_MODS_H

#include "qa/contracts/mods.h"
#include "qa/persistence/value.h"

qa_mod_identity qa_read_mod_identity(const qa_save_reader *reader);
struct qa_mod_session_checkpoint qa_read_mod_session(const qa_save_reader *reader);

#endif
