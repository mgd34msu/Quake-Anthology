/*
 * Port of src/compat/q3/native/game-syscalls.ts: Quake III Arena
 * server/sv_game.c SV_GameSystemCalls for native i386 modules, the call
 * record the shared server-game trap table decodes. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_Q3_NATIVE_GAME_SYSCALLS_H
#define QA_COMPAT_Q3_NATIVE_GAME_SYSCALLS_H

#include "qa/compat/qvm/syscalls.h"

/* A native module's trap, decoded exactly like the same trap from an
 * interpreted module. words (borrowed, >= 4 bytes) and guest are borrowed by
 * *out; command_arguments NULL with has_command_arguments false is TS null. */
void qa_native_q3_game_call(const uint8_t *words, size_t words_length, const qa_qvm_game_memory *guest,
                            const char *const *command_arguments, size_t command_argument_count,
                            bool has_command_arguments, qa_qvm_host_call *out);

#endif
