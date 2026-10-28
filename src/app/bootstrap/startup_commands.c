/* Port of src/app/bootstrap/startup-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_commands.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static void append(char **buf, const char *add) {
  size_t a = strlen(*buf), b = strlen(add);
  char *n = realloc(*buf, a + b + 1);
  if (!n) qa_range_error("Out of memory");
  memcpy(n + a, add, b + 1); *buf = n;
}
/* operand(): validates and returns "\"value\"" malloc'd. */
static char *operand(const char *value) {
  char *checked = qa_source_command_text(value); free(checked);
  if (strchr(value, '"') || strchr(value, '\r') || strchr(value, '\n'))
    qa_range_error("Startup argument cannot contain quotes or line breaks; use a quoted +command batch or exec a cfg file.");
  size_t len = strlen(value);
  char *r = malloc(len + 3);
  if (!r) qa_range_error("Out of memory");
  r[0] = '"'; memcpy(r + 1, value, len); r[len + 1] = '"'; r[len + 2] = '\0';
  return r;
}
static char *first_token_folded(const char *line, qa_command_dialect d, char **second) {
  qa_command_tokens t; memset(&t, 0, sizeof t);
  if (!qa_tokenize_command(line, d, QA_COMMAND_TEXT_SOURCE, &t)) qa_range_error("Out of memory");
  char *r = t.argc ? qa_ascii_fold(t.argv[0]) : xdup("");
  if (second) *second = t.argc > 1 ? qa_ascii_fold(t.argv[1]) : xdup("");
  qa_command_tokens_destroy(&t);
  if (!r) qa_range_error("Out of memory");
  return r;
}
qa_app_startup_command qa_app_read_startup_command(const char *const *argv, size_t argc, size_t index) {
  const char *first = index < argc ? argv[index] : NULL;
  if (first == NULL || first[0] != '+' || first[1] == '\0') qa_range_error("Expected +command");
  char *text = xdup(first + 1);
  size_t end = index;
  while (end + 1 < argc) {
    const char *next = argv[end + 1];
    if (next == NULL || next[0] == '+' || strncmp(next, "--", 2) == 0) break;
    char *op = operand(next);
    append(&text, " "); append(&text, op); free(op); end++;
  }
  { char *c = qa_source_command_text(text); free(c); }
  if (strchr(text, '\r') || strchr(text, '\n') ||
      qa_command_separator_offset(text, QA_COMMAND_DIALECT_Q3) < strlen(text))
    qa_range_error("Use a separate +command for each startup command.");
  qa_command_tokens t; memset(&t, 0, sizeof t);
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &t)) qa_range_error("Out of memory");
  if (t.argc == 0) qa_range_error("Expected +command");
  char *tok0 = qa_ascii_fold(t.argv[0]);
  char *tok1 = t.argc > 1 ? qa_ascii_fold(t.argv[1]) : xdup("");
  qa_command_tokens_destroy(&t);
  if (tok0 && strcmp(tok0, "connect") == 0)
    qa_range_error("Ordered +connect startup is unsupported; use --connect-q1, --connect-qw, --connect-q2 or --connect-q3.");
  if (tok0 && strcmp(tok0, "set") == 0 && tok1) {
    static const char *const bad[] = {"game", "fs_game", "basedir", "cddir", "fs_basepath", "fs_homepath", "fs_cdpath"};
    for (size_t i = 0; i < 7; i++)
      if (strcmp(tok1, bad[i]) == 0)
        qa_range_error("Filesystem +set selection is unsupported; use --game with an installed catalog product and --content-root or --user-content-root.");
  }
  free(tok0); free(tok1);
  qa_app_startup_command r = {text, end};
  return r;
}
bool qa_app_startup_requests_world(const char *const *lines, size_t count) {
  static const char *const cmds[] = {"map", "devmap", "spmap", "spdevmap"};
  for (size_t i = 0; i < count; i++) {
    /* TS uses toLowerCase (unicode); asciiFold matches for these ASCII names. */
    char *t = first_token_folded(lines[i], QA_COMMAND_DIALECT_Q3, NULL);
    bool hit = false;
    for (size_t j = 0; j < 4; j++) if (strcmp(t, cmds[j]) == 0) hit = true;
    free(t);
    if (hit) return true;
  }
  return false;
}
static char *join_lines(const char *const *lines, size_t count) {
  if (count == 0) return xdup("");
  char *r = xdup("");
  for (size_t i = 0; i < count; i++) { if (i) append(&r, "\n"); append(&r, lines[i]); }
  append(&r, "\n");
  return r;
}
qa_app_startup_phases qa_app_startup_command_phases(const char *const *lines, size_t count, qa_command_dialect dialect) {
  long safe_index = -1;
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    for (size_t i = 0; i < count; i++) {
      char *t = first_token_folded(lines[i], dialect, NULL);
      bool hit = strcmp(t, "safe") == 0 || strcmp(t, "cvar_restart") == 0;
      free(t);
      if (hit) { safe_index = (long)i; break; }
    }
  }
  const char **early = calloc(count ? count : 1, sizeof *early);
  const char **late = calloc(count ? count : 1, sizeof *late);
  if (!early || !late) qa_range_error("Out of memory");
  size_t ne = 0, nl = 0;
  qa_app_startup_phases out; memset(&out, 0, sizeof out);
  out.variables = calloc(count ? count : 1, sizeof *out.variables);
  if (!out.variables) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    if ((long)i == safe_index) continue;
    qa_command_tokens t; memset(&t, 0, sizeof t);
    if (!qa_tokenize_command(lines[i], dialect, QA_COMMAND_TEXT_SOURCE, &t)) qa_range_error("Out of memory");
    bool set = t.argc > 0 && strcmp(t.argv[0], "set") == 0;
    if (dialect == QA_COMMAND_DIALECT_Q3 && set) {
      out.variables[out.variable_count].name = xdup(t.argc > 1 ? t.argv[1] : "");
      out.variables[out.variable_count].value = xdup(t.argc > 2 ? t.argv[2] : "");
      out.variable_count++;
    }
    qa_command_tokens_destroy(&t);
    bool q2 = dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE;
    bool q1 = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    if (q2 && set) early[ne++] = lines[i];
    if (dialect == QA_COMMAND_DIALECT_Q3 || !set || q1) late[nl++] = lines[i];
  }
  out.early = join_lines(early, ne);
  out.late = join_lines(late, nl);
  out.stuffed = join_lines(lines, count);
  out.safe = safe_index != -1;
  free(early); free(late);
  return out;
}
void qa_app_startup_phases_free(qa_app_startup_phases *p) {
  free(p->early); free(p->late); free(p->stuffed);
  for (size_t i = 0; i < p->variable_count; i++) { free(p->variables[i].name); free(p->variables[i].value); }
  free(p->variables); memset(p, 0, sizeof *p);
}
