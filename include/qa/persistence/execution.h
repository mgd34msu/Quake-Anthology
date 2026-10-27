/*
 * Port of src/persistence/execution.ts: boundary readers for module
 * identities, game APIs, native ABIs, guest layouts, and guest
 * checkpoints. Returned structs borrow strings/bytes from the
 * checkpoint tree; list fields are malloc'd arrays the caller frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_EXECUTION_H
#define QA_PERSISTENCE_EXECUTION_H

#include "qa/contracts/execution.h"
#include "qa/persistence/value.h"

qa_module_identity qa_read_module(const qa_save_reader *reader);
qa_game_api_identity qa_read_api(const qa_save_reader *reader);
qa_native_call_abi qa_read_native_call_abi(const qa_save_reader *reader);
qa_native_abi qa_read_native_abi(const qa_save_reader *reader);
qa_guest_layout qa_read_layout(const qa_save_reader *reader);
qa_guest_checkpoint qa_read_guest(const qa_save_reader *reader);

#endif
