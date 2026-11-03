/* Port of src/network/q3/client-server-command.ts. GPL-2.0-or-later. */
#include "qa/network/q3/client_server_command.h"
#include "qa/core/commands/text.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct qa_q3_server_command_executor { char *big; };
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
bool qa_q3_server_command_executor_init(qa_q3_server_command_executor **out) {
  qa_q3_server_command_executor *e = calloc(1, sizeof *e);
  if (!e) return false;
  e->big = xstrdup("");
  if (!e->big) { free(e); return false; }
  *out = e; return true;
}
void qa_q3_server_command_executor_destroy(qa_q3_server_command_executor *e) { if (!e) return; free(e->big); free(e); }
void qa_q3_server_command_executor_clear(qa_q3_server_command_executor *e) {
  char *empty = xstrdup("");
  if (!empty) qa_range_error("server command allocation failed");
  free(e->big); e->big = empty;
}
/* Owned scratch of one execute. The command is server-provided: tokenizing it
 * (RangeError on an overlong token), gameState.modify (drop) and the host
 * bindings can all raise, and a catching caller must lose nothing. */
typedef struct { char *text, *joined, *message; qa_command_tokens argv; } execute_scratch;
static void execute_scratch_release(void *arg) {
  execute_scratch *sc = arg;
  free(sc->text); free(sc->joined); free(sc->message);
  sc->text = sc->joined = sc->message = NULL;
  qa_command_tokens_destroy(&sc->argv);
}
static noreturn void out_of_memory(void) { qa_range_error("server command allocation failed"); }
static void retokenize(execute_scratch *sc) {
  qa_command_tokens_destroy(&sc->argv);
  if (!qa_tokenize_command(sc->text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &sc->argv)) out_of_memory();
}
static const char *arg(const qa_command_tokens *t, size_t i) { return i < t->argc ? t->argv[i] : ""; }
static bool execute_body(qa_q3_server_command_executor *e, execute_scratch *sc,
                         qa_q3_client_game_state_storage *game_state, const qa_q3_server_command_bindings *bindings) {
  retokenize(sc);
  const char *name = arg(&sc->argv, 0);
  if (strcmp(name, "disconnect") == 0) {
    if (sc->argv.argc >= 2) {
      const char *reason = arg(&sc->argv, 1);
      size_t n = strlen(reason) + sizeof "Server Disconnected - ";
      sc->message = malloc(n);
      if (!sc->message) out_of_memory();
      snprintf(sc->message, n, "Server Disconnected - %s", reason);
      qa_common_error(QA_COMMON_ERROR_SERVER_DISCONNECT, sc->message);
    }
    qa_common_error(QA_COMMON_ERROR_SERVER_DISCONNECT, "Server disconnected\n");
  }
  if (strcmp(name, "bcs0") == 0) {
    size_t n = strlen(arg(&sc->argv, 1)) + strlen(arg(&sc->argv, 2)) + 8;
    char *big = malloc(n);
    if (!big) out_of_memory();
    snprintf(big, n, "cs %s \"%s", arg(&sc->argv, 1), arg(&sc->argv, 2));
    if (strlen(big) > 8191) big[8191] = 0;
    free(e->big); e->big = big;
    return false;
  }
  if (strcmp(name, "bcs1") == 0 || strcmp(name, "bcs2") == 0) {
    bool last = strcmp(name, "bcs2") == 0;
    const char *suffix = arg(&sc->argv, 2);
    size_t big_len = strlen(e->big), suffix_len = strlen(suffix);
    if (big_len + suffix_len + (last ? 1 : 0) >= 8192) qa_common_error(QA_COMMON_ERROR_DROP, "bcs exceeded BIG_INFO_STRING");
    char *big = realloc(e->big, big_len + suffix_len + 2);
    if (!big) out_of_memory();
    e->big = big;
    memcpy(big + big_len, suffix, suffix_len + 1);
    if (!last) return false;
    big[big_len + suffix_len] = '"'; big[big_len + suffix_len + 1] = 0;
    char *text = xstrdup(e->big);
    if (!text) out_of_memory();
    free(sc->text); sc->text = text;
    retokenize(sc);
    name = arg(&sc->argv, 0);
  }
  if (strcmp(name, "cs") == 0) {
    int64_t index = qa_native_atoi(arg(&sc->argv, 1));
    /* argv.slice(2).join(" ") */
    size_t n = 1;
    for (size_t i = 2; i < sc->argv.argc; i++) n += strlen(sc->argv.argv[i]) + 1;
    sc->joined = malloc(n);
    if (!sc->joined) out_of_memory();
    size_t o = 0;
    for (size_t i = 2; i < sc->argv.argc; i++) {
      if (i > 2) sc->joined[o++] = ' ';
      size_t len = strlen(sc->argv.argv[i]);
      memcpy(sc->joined + o, sc->argv.argv[i], len); o += len;
    }
    sc->joined[o] = 0;
    bool modified = qa_q3_client_game_state_storage_modify(game_state, index, sc->joined);
    free(sc->joined); sc->joined = NULL;
    if (modified && index == 1) {
      bindings->system_info(bindings->ctx);
      bindings->assert_current(bindings->ctx);
    }
    retokenize(sc);
  } else if (strcmp(name, "map_restart") == 0) {
    bindings->map_restart(bindings->ctx);
  } else if (strcmp(name, "clientLevelShot") == 0) {
    if (!bindings->local_server_running(bindings->ctx)) return false;
    bindings->level_shot(bindings->ctx);
  }
  return true;
}
bool qa_q3_server_command_executor_execute(qa_q3_server_command_executor *e, const char *command,
                                           qa_q3_client_game_state_storage *game_state,
                                           const qa_q3_server_command_bindings *bindings,
                                           char ***argv_out, size_t *argc_out) {
  *argv_out = NULL; *argc_out = 0;
  execute_scratch sc = { xstrdup(command), NULL, NULL, { NULL, 0, NULL } };
  if (!sc.text) out_of_memory();
  qa_cleanup guard;
  qa_cleanup_push(&guard, execute_scratch_release, &sc);
  bool result = execute_body(e, &sc, game_state, bindings);
  qa_cleanup_pop(&guard);
  if (!result) { execute_scratch_release(&sc); return false; }
  *argv_out = sc.argv.argv; *argc_out = sc.argv.argc;
  free(sc.argv.args_text);
  free(sc.text);
  return true;
}
