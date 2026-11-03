/*
 * Port of src/compat/qvm/bot-library-syscalls.ts. The services carry the real
 * BotLibrary (bots/behavior/q3/library.h) and the dispatcher calls it directly.
 * qa_qvm_string_contains / qa_qvm_touching_goal are the chat.ts stringContains
 * and goals.ts touchingGoal helpers over the QVM argument shapes.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_BOT_LIBRARY_SYSCALLS_H
#define QA_COMPAT_QVM_BOT_LIBRARY_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/bots/behavior/q3/library.h"
#include "qa/compat/qvm/bot_navigation_records.h"
#include "qa/compat/qvm/client_state_record.h"
#include "qa/compat/qvm/script_record.h"
#include "qa/compat/qvm/syscalls.h"

/* TS QvmBotLibraryServices. library is borrowed. Every callback receives ctx.
 * console_message returns a malloc'd string the dispatcher frees (NULL is TS
 * null). started_frame NULL is the absent optional member. */
typedef struct qa_qvm_bot_library_services {
  qa_bot_library_bots_a *library;
  void *ctx;
  double (*setup)(void *ctx);
  double (*shutdown)(void *ctx);
  double (*load_map)(void *ctx, const char *name);
  double (*update_entity)(void *ctx, double number, const qa_qvm_host_call *call, double pointer);
  double (*snapshot_entity)(void *ctx, double client, double sequence);
  char *(*console_message)(void *ctx, double client);
  void (*user_command)(void *ctx, double client, qa_qvmx_wire_user_command command);
  double (*allocate_client)(void *ctx);
  void (*free_client)(void *ctx, double client);
  void (*started_frame)(void *ctx);
} qa_qvm_bot_library_services;

/* TS stringContains over nullable texts (NULL is TS null). */
double qa_qvm_string_contains(const char *text, const char *part, bool case_sensitive);
/* TS touchingGoal over a QVM goal record. */
bool qa_qvm_touching_goal(qa_vec3 origin, qa_qvmx_bot_goal goal);

bool qa_qvm_bot_library_syscall(const qa_qvm_host_call *call, qa_qvm_bot_library_services *services,
                                qa_qvm_host_result *out);

#endif
