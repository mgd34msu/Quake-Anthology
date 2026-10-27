/* Port of src/network/q3/client-server-command.ts. GPL-2.0-or-later. */
#include "qa/network/q3/client_server_command.h"
#include "qa/core/commands/text.h"
#include "qa/core/common_error.h"
#include "qa/core/numeric.h"
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
void qa_q3_server_command_executor_clear(qa_q3_server_command_executor *e) { free(e->big); e->big = xstrdup(""); }
static bool tokens_of(const char *text, qa_command_tokens *out) {
  return qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, out);
}
static const char *arg(const qa_command_tokens *t, size_t i) { return i < t->argc ? t->argv[i] : ""; }
bool qa_q3_server_command_executor_execute(qa_q3_server_command_executor *e, const char *command,
                                           qa_q3_client_game_state_storage *game_state,
                                           const qa_q3_server_command_bindings *bindings,
                                           char ***argv_out, size_t *argc_out) {
  *argv_out = NULL; *argc_out = 0;
  char *text = xstrdup(command);
  if (!text) return false;
  qa_command_tokens argv;
  if (!tokens_of(text, &argv)) { free(text); return false; }
  const char *name = arg(&argv, 0);
  if (strcmp(name, "disconnect") == 0) {
    char buf[1200];
    if (argv.argc >= 2) snprintf(buf, sizeof buf, "Server Disconnected - %s", arg(&argv, 1));
    else snprintf(buf, sizeof buf, "Server disconnected\n");
    qa_command_tokens_destroy(&argv); free(text);
    qa_common_error(QA_COMMON_ERROR_SERVER_DISCONNECT, buf);
  }
  if (strcmp(name, "bcs0") == 0) {
    size_t n = strlen(arg(&argv, 1)) + strlen(arg(&argv, 2)) + 8;
    char *big = malloc(n);
    if (!big) { qa_command_tokens_destroy(&argv); free(text); return false; }
    snprintf(big, n, "cs %s \"%s", arg(&argv, 1), arg(&argv, 2));
    if (strlen(big) > 8191) big[8191] = 0;
    free(e->big); e->big = big;
    qa_command_tokens_destroy(&argv); free(text);
    return false;
  }
  if (strcmp(name, "bcs1") == 0 || strcmp(name, "bcs2") == 0) {
    bool last = strcmp(name, "bcs2") == 0;
    const char *suffix = arg(&argv, 2);
    if (strlen(e->big) + strlen(suffix) + (last ? 1 : 0) >= 8192) {
      qa_command_tokens_destroy(&argv); free(text);
      qa_common_error(QA_COMMON_ERROR_DROP, "bcs exceeded BIG_INFO_STRING");
    }
    size_t n = strlen(e->big) + strlen(suffix) + 2;
    char *big = realloc(e->big, n);
    if (!big) { qa_command_tokens_destroy(&argv); free(text); return false; }
    e->big = big;
    strcat(e->big, suffix);
    if (!last) { qa_command_tokens_destroy(&argv); free(text); return false; }
    strcat(e->big, "\"");
    free(text); text = xstrdup(e->big);
    if (!text) { qa_command_tokens_destroy(&argv); return false; }
    qa_command_tokens_destroy(&argv);
    if (!tokens_of(text, &argv)) { free(text); return false; }
    name = arg(&argv, 0);
  }
  if (strcmp(name, "cs") == 0) {
    int64_t index = qa_native_atoi(arg(&argv, 1));
    /* argv.slice(2).join(" ") */
    size_t n = 1;
    for (size_t i = 2; i < argv.argc; i++) n += strlen(argv.argv[i]) + 1;
    char *joined = calloc(n, 1);
    if (!joined) { qa_command_tokens_destroy(&argv); free(text); return false; }
    for (size_t i = 2; i < argv.argc; i++) { if (i > 2) strcat(joined, " "); strcat(joined, argv.argv[i]); }
    bool modified = qa_q3_client_game_state_storage_modify(game_state, index, joined);
    free(joined);
    if (modified && index == 1) {
      bindings->system_info(bindings->ctx);
      bindings->assert_current(bindings->ctx);
    }
    qa_command_tokens_destroy(&argv);
    if (!tokens_of(text, &argv)) { free(text); return false; }
  } else if (strcmp(name, "map_restart") == 0) {
    bindings->map_restart(bindings->ctx);
  } else if (strcmp(name, "clientLevelShot") == 0) {
    if (!bindings->local_server_running(bindings->ctx)) { qa_command_tokens_destroy(&argv); free(text); return false; }
    bindings->level_shot(bindings->ctx);
  }
  *argv_out = argv.argv; *argc_out = argv.argc;
  free(argv.args_text);
  free(text);
  return true;
}
