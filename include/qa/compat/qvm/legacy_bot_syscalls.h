/*
 * Port of src/compat/qvm/legacy-bot-syscalls.ts. False means another
 * service owns the trap (TS null). Services are the landed
 * qa_qvm_bot_library_services (TS QvmBotLibraryServices, owner
 * src/compat/qvm/bot-library-syscalls.ts); this module calls the real
 * BotLibrary's actions, characters and chat with the legacy arities.
 * BotActionFlag values (TS owner src/bots/behavior/library/
 * actions.ts) are inlined as constants.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_LEGACY_BOT_SYSCALLS_H
#define QA_COMPAT_QVM_LEGACY_BOT_SYSCALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/bot_library_syscalls.h"

int32_t qa_qvm_legacy_bot_action_flags(int32_t flags);
bool qa_qvm_legacy_bot_library_syscall(const qa_qvm_host_call *call,
                                       qa_qvm_bot_library_services *services, int32_t *out);

#endif
