/*
 * Port of src/compat/qvm/legacy-bot-syscalls.ts. False means another
 * service owns the trap (TS null). Mirrored types (parallel slice /
 * unlanded owners): qa_qvmy_bot_library mirrors the BotLibrary surface
 * used here (TS owner src/bots/behavior/q3/library.ts) and
 * qa_qvmy_bot_library_services mirrors QvmBotLibraryServices (TS owner
 * src/compat/qvm/bot-library-syscalls.ts); only the members this
 * module calls are mirrored. BotActionFlag values (TS owner
 * src/bots/behavior/library/actions.ts) are inlined as constants.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_LEGACY_BOT_SYSCALLS_H
#define QA_COMPAT_QVM_LEGACY_BOT_SYSCALLS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/compat/qvm/syscalls.h"
#include "qa/network/q3/message.h"

typedef struct qa_qvmy_bot_library {
  void *ctx;
  void (*actions_use_item)(void *ctx, int32_t client, const char *item);
  void (*actions_drop_item)(void *ctx, int32_t client, const char *item);
  void (*actions_use_inventory)(void *ctx, int32_t client, const char *item);
  void (*actions_drop_inventory)(void *ctx, int32_t client, const char *item);
  /* Writes the 40 input bytes to out. */
  void (*actions_get_input_bytes)(void *ctx, int32_t client, float think_time, uint8_t out[40]);
  int32_t (*characters_load)(void *ctx, const char *name, int32_t skill);
  void (*chat_set_name)(void *ctx, int32_t state, const char *name);
  void (*chat_enter_chat)(void *ctx, int32_t state, int32_t type, int32_t team, int32_t subtype);
} qa_qvmy_bot_library;

typedef struct qa_qvmy_bot_library_services {
  const qa_qvmy_bot_library *library;
  void *ctx;
  void (*user_command)(void *ctx, int32_t client, const qa_q3_wire_user_command *command);
} qa_qvmy_bot_library_services;

int32_t qa_qvm_legacy_bot_action_flags(int32_t flags);
bool qa_qvm_legacy_bot_library_syscall(const qa_qvm_host_call *call,
                                       const qa_qvmy_bot_library_services *services, int32_t *out);

#endif
