/* Port of src/app/bootstrap/q2-console.ts. SharedSimulation and
 * LoadedApplicationContent (other slices) are the host view below.
 * The TS re-exports of settings/server/lmctf-cvars are the landed
 * qa_lmctf_console_names / qa_register_lmctf_console_cvars.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_CONSOLE_H
#define QA_APP_BOOTSTRAP_Q2_CONSOLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/common.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/settings/server/lmctf_cvars.h"
/* TS q2OperatorPlayerName: writes into scratch. */
const char *qa_app_q2_operator_player_name(const char *userinfo, const char *name,
    char *scratch, size_t scratch_size);
typedef struct qa_app_q2_console_player {
  bool connected;
  double slot, score, ping;
  const char *userinfo; /* borrowed */
  const char *name;     /* borrowed */
} qa_app_q2_console_player;
typedef struct qa_app_q2_console_options {
  void *ctx;
  bool (*has_q2_source)(void *ctx);
  qa_cvar_registry *(*q2_server_cvars)(void *ctx);
  const char *(*map_name)(void *ctx);
  size_t (*players)(void *ctx, const qa_app_q2_console_player **out);
  bool (*is_lmctf)(void *ctx);
  qa_lmctf_rules *(*lmctf_rules)(void *ctx);
  /* content.forContent(recipe.match.content).open(path). */
  uint8_t *(*open_match_file)(void *ctx, const char *path, size_t *out_length);
  void (*print)(void *ctx, const char *text);
  void (*execute)(void *ctx, const char *name, const char *const *args, size_t arg_count,
      qa_command_context source);
  /* TS bindLmctfConsoleRules beyond registration. */
  void (*bind_lmctf_rules)(void *ctx, qa_cvar_registry *cvars, qa_lmctf_rules *rules);
} qa_app_q2_console_options;
typedef struct qa_app_q2_console qa_app_q2_console;
/* Aborts without a Q2 source runtime (TS Error). */
qa_app_q2_console *qa_app_q2_console_create(qa_app_q2_console_options options);
void qa_app_q2_console_destroy(qa_app_q2_console *console);
qa_cvar_registry *qa_app_q2_console_cvars(qa_app_q2_console *console);
typedef struct qa_app_q2_console_binding qa_app_q2_console_binding;
qa_app_q2_console_binding *qa_app_q2_console_bind(qa_app_q2_console *console, qa_command_buffer *commands);
void qa_app_q2_console_binding_release(qa_app_q2_console_binding *binding);
/* Borrowed static + LMCTF names; count via out param. */
const char *const *qa_app_q2_console_shared_names(qa_app_q2_console *console, size_t *out_count);
void qa_app_q2_console_initialize(qa_app_q2_console *console);
void qa_app_q2_console_bind_current(qa_app_q2_console *console);
#endif
