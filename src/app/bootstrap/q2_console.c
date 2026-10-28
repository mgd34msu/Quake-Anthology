/* Port of src/app/bootstrap/q2-console.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_console.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/base/player/index.h"
#include "qa/foundation/support.h"
struct qa_app_q2_console { qa_app_q2_console_options options; };
struct qa_app_q2_console_binding {
  qa_command_buffer *commands;
  qa_app_q2_console *console;
  const char *names[5];
  qa_command_handler_fn handlers[5];
  void *ctxs[5];
  size_t count;
};
const char *qa_app_q2_operator_player_name(const char *userinfo, const char *name,
    char *scratch, size_t scratch_size) {
  const char *value = qa_q2_userinfo_get(userinfo, "name", scratch, scratch_size);
  return value != NULL ? value : name;
}
qa_app_q2_console *qa_app_q2_console_create(qa_app_q2_console_options options) {
  if (!options.has_q2_source(options.ctx)) qa_range_error("Q2 console requires a Q2 source runtime");
  qa_app_q2_console *console = calloc(1, sizeof *console);
  if (console == NULL) qa_range_error("Out of memory");
  console->options = options;
  return console;
}
void qa_app_q2_console_destroy(qa_app_q2_console *console) { free(console); }
qa_cvar_registry *qa_app_q2_console_cvars(qa_app_q2_console *console) {
  qa_cvar_registry *cvars = console->options.q2_server_cvars(console->options.ctx);
  if (cvars == NULL) qa_range_error("Q2 console requires the simulation's source registry");
  return cvars;
}
typedef struct forward_ctx { qa_app_q2_console *console; const char *name; } forward_ctx;
static void forward_handler(void *v, qa_command_invocation *invocation) {
  forward_ctx *ctx = v;
  size_t count = invocation->argc > 0 ? invocation->argc - 1 : 0;
  ctx->console->options.execute(ctx->console->options.ctx, ctx->name,
      count > 0 ? (const char *const *)(invocation->argv + 1) : NULL, count, invocation->source);
}
static void print_text(qa_app_q2_console *console, const char *text) {
  console->options.print(console->options.ctx, text);
}
static void status_handler(void *v, qa_command_invocation *invocation) {
  (void)invocation;
  qa_app_q2_console *console = v;
  if (!console->options.has_q2_source(console->options.ctx)) { print_text(console, "No server running.\n"); return; }
  char header[1024];
  snprintf(header, sizeof header, "map              : %s\nnum score ping name\n",
      console->options.map_name(console->options.ctx));
  print_text(console, header);
  const qa_app_q2_console_player *players = NULL;
  size_t count = console->options.players(console->options.ctx, &players);
  for (size_t i = 0; i < count; i++) {
    if (!players[i].connected) continue;
    char scratch[256];
    const char *name = qa_app_q2_operator_player_name(players[i].userinfo, players[i].name, scratch, sizeof scratch);
    char line[1024];
    snprintf(line, sizeof line, "%lld %lld %lld %s\n", (long long)players[i].slot,
        (long long)players[i].score, (long long)players[i].ping, name);
    print_text(console, line);
  }
}
static bool all_digits(const char *text) {
  if (text[0] == '\0') return false;
  for (const char *p = text; *p != '\0'; p++) if (!isdigit((unsigned char)*p)) return false;
  return true;
}
static void dumpuser_handler(void *v, qa_command_invocation *invocation) {
  qa_app_q2_console *console = v;
  size_t arg_count = invocation->argc > 0 ? invocation->argc - 1 : 0;
  const char *target = arg_count > 0 ? invocation->argv[1] : NULL;
  if (target == NULL || arg_count != 1) { print_text(console, "Usage: dumpuser <player name|slot>\n"); return; }
  const qa_app_q2_console_player *players = NULL;
  size_t count = console->options.has_q2_source(console->options.ctx)
      ? console->options.players(console->options.ctx, &players) : 0;
  const qa_app_q2_console_player *player = NULL;
  char scratch[256];
  for (size_t i = 0; i < count; i++) {
    if (!players[i].connected) continue;
    bool match = all_digits(target) ? players[i].slot == strtod(target, NULL)
        : strcmp(qa_app_q2_operator_player_name(players[i].userinfo, players[i].name, scratch, sizeof scratch), target) == 0;
    if (match) player = &players[i];
  }
  if (player == NULL) {
    char line[1024];
    snprintf(line, sizeof line, "Player %s is not on the server\n", target);
    print_text(console, line);
    return;
  }
  print_text(console, "userinfo\n--------\n");
  /* q2Userinfo iteration over \key\value pairs in source order. */
  const char *p = player->userinfo;
  if (*p == '\\') p++;
  while (*p != '\0') {
    const char *key_end = strchr(p, '\\');
    if (key_end == NULL) break;
    const char *value = key_end + 1;
    const char *value_end = strchr(value, '\\');
    size_t key_len = (size_t)(key_end - p);
    size_t value_len = value_end != NULL ? (size_t)(value_end - value) : strlen(value);
    char line[1024];
    char key[256];
    if (key_len >= sizeof key) key_len = sizeof key - 1;
    memcpy(key, p, key_len); key[key_len] = '\0';
    char value_text[512];
    if (value_len >= sizeof value_text) value_len = sizeof value_text - 1;
    memcpy(value_text, value, value_len); value_text[value_len] = '\0';
    snprintf(line, sizeof line, "%-20s%s\n", key, value_text);
    print_text(console, line);
    p = value_end != NULL ? value_end + 1 : value + value_len;
  }
}
qa_app_q2_console_binding *qa_app_q2_console_bind(qa_app_q2_console *console, qa_command_buffer *commands) {
  qa_app_q2_console_binding *binding = calloc(1, sizeof *binding);
  if (binding == NULL) qa_range_error("Out of memory");
  binding->commands = commands;
  binding->console = console;
  static forward_ctx forwards[3];
  static const char *const forward_names[] = {"quit", "map", "say"};
  for (size_t i = 0; i < 3; i++) {
    forwards[i].console = console; forwards[i].name = forward_names[i];
    if (qa_command_buffer_register(commands, forward_names[i], forward_handler, &forwards[i], NULL, QA_COMMAND_WORLD)) {
      binding->names[binding->count] = forward_names[i];
      binding->handlers[binding->count] = forward_handler;
      binding->ctxs[binding->count] = &forwards[i];
      binding->count++;
    }
  }
  if (qa_command_buffer_register(commands, "status", status_handler, console, NULL, QA_COMMAND_WORLD)) {
    binding->names[binding->count] = "status"; binding->handlers[binding->count] = status_handler;
    binding->ctxs[binding->count] = console; binding->count++;
  }
  if (qa_command_buffer_register(commands, "dumpuser", dumpuser_handler, console, NULL, QA_COMMAND_WORLD)) {
    binding->names[binding->count] = "dumpuser"; binding->handlers[binding->count] = dumpuser_handler;
    binding->ctxs[binding->count] = console; binding->count++;
  }
  return binding;
}
void qa_app_q2_console_binding_release(qa_app_q2_console_binding *binding) {
  if (binding == NULL) return;
  for (size_t i = 0; i < binding->count; i++)
    qa_command_buffer_unregister(binding->commands, binding->names[i], binding->handlers[i], binding->ctxs[i], true);
  free(binding);
}
const char *const *qa_app_q2_console_shared_names(qa_app_q2_console *console, size_t *out_count) {
  static const char *base_names[] = {"dmflags", "timelimit", "fraglimit", "capturelimit"};
  static const char *all_names[64];
  if (!console->options.is_lmctf(console->options.ctx)) { *out_count = 4; return base_names; }
  size_t at = 0;
  for (size_t i = 0; i < 4; i++) all_names[at++] = base_names[i];
  for (size_t i = 0; i < qa_lmctf_console_name_count && at < 64; i++) all_names[at++] = qa_lmctf_console_names[i];
  *out_count = at;
  return all_names;
}
void qa_app_q2_console_initialize(qa_app_q2_console *console) { qa_app_q2_console_bind_current(console); }
void qa_app_q2_console_bind_current(qa_app_q2_console *console) {
  if (!console->options.is_lmctf(console->options.ctx)) return;
  qa_cvar_registry *cvars = qa_app_q2_console_cvars(console);
  qa_lmctf_rules *rules = console->options.lmctf_rules(console->options.ctx);
  console->options.bind_lmctf_rules(console->options.ctx, cvars, rules);
  char *maplist_file = qa_cvar_registry_variable_string(cvars, "maplist_file");
  size_t length = 0;
  uint8_t *bytes = maplist_file != NULL
      ? console->options.open_match_file(console->options.ctx, maplist_file, &length) : NULL;
  free(maplist_file);
  if (bytes == NULL) bytes = console->options.open_match_file(console->options.ctx, "maplist.txt", &length);
  if (bytes == NULL) return;
  /* Split lines, trim, first token lowercased. */
  size_t capacity = 0, count = 0;
  const char **names = NULL;
  size_t at = 0;
  while (at < length) {
    size_t end = at;
    while (end < length && bytes[end] != '\n') end++;
    size_t line_end = end;
    if (line_end > at && bytes[line_end - 1] == '\r') line_end--;
    size_t start = at;
    while (start < line_end && isspace(bytes[start])) start++;
    size_t stop = start;
    while (stop < line_end && !isspace(bytes[stop])) stop++;
    if (stop > start) {
      char *name = malloc(stop - start + 1);
      if (name == NULL) qa_range_error("Out of memory");
      for (size_t c = 0; c < stop - start; c++) name[c] = (char)tolower(bytes[start + c]);
      name[stop - start] = '\0';
      if (count == capacity) {
        capacity = capacity == 0 ? 8 : capacity * 2;
        const char **next = realloc(names, capacity * sizeof *next);
        if (next == NULL) qa_range_error("Out of memory");
        names = next;
      }
      names[count++] = name;
    }
    at = end + 1;
  }
  free(bytes);
  for (size_t i = 0; i < rules->map_count; i++) free((void *)rules->map_list[i]);
  free((void *)rules->map_list);
  rules->map_list = names;
  rules->map_count = count;
}
