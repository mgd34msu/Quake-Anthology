/* Port of src/app/bootstrap/q1-client-commands.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q1_client_commands.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q1_client_settings.h"
#include "qa/foundation/support.h"
typedef struct command_ctx {
  qa_app_execute_q1_client_command_fn execute;
  void *execute_ctx;
  const char *name;
} command_ctx;
struct qa_app_q1_client_commands {
  qa_command_buffer *commands;
  qa_app_q1_view_commands *view;
  const char **names; size_t count;
  command_ctx *ctxs;
  char *doc_storage; /* usage/example scratch, one block */
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static void handler(void *v, qa_command_invocation *invocation) {
  command_ctx *ctx = v;
  const qa_command_origin *origin = &invocation->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL) origin = origin->caller;
  size_t arg_count = invocation->argc > 0 ? invocation->argc - 1 : 0;
  const char *const *args = arg_count > 0 ? (const char *const *)(invocation->argv + 1) : NULL;
  const char *name = ctx->name;
  if (is(name, "giveall")) {
    static const char *const just_all[] = {"all"};
    if (origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE) {
      const char **extended = malloc((arg_count + 1) * sizeof *extended);
      if (extended == NULL) qa_range_error("Out of memory");
      for (size_t i = 0; i < arg_count; i++) extended[i] = args[i];
      extended[arg_count] = "all";
      ctx->execute(ctx->execute_ctx, "give", extended, arg_count + 1,
          origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &origin->seat : NULL, invocation->source);
      free(extended);
      return;
    }
    args = just_all; arg_count = 1; name = "give";
  } else if (is(name, "suicide")) name = "kill";
  ctx->execute(ctx->execute_ctx, name, args, arg_count,
      origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &origin->seat : NULL, invocation->source);
}
qa_app_q1_client_commands *qa_app_register_q1_client_commands(qa_command_buffer *commands,
    qa_command_dialect dialect, qa_app_execute_q1_client_command_fn execute, void *execute_ctx) {
  qa_app_q1_client_commands *registration = calloc(1, sizeof *registration);
  if (registration == NULL) qa_range_error("Out of memory");
  registration->commands = commands;
  registration->view = qa_app_register_q1_view_commands(commands);
  static const struct { const char *name; const char *summary; } base[] = {
    {"god", "Toggle god mode; multiplayer authority controls cheat access."},
    {"notarget", "Toggle monster targeting immunity; multiplayer authority controls cheat access."},
    {"noclip", "Toggle movement through walls; multiplayer authority controls cheat access."},
    {"give", "Give all, health, armor, weapons, ammo, keys, or a named item; source authority controls cheat access."},
    {"giveall", "Give the source all grant, including the currently selected arsenal."},
    {"kill", "Suicide through the source game's player lifecycle."},
    {"suicide", "Alias for kill."},
    {"fly", "Toggle flying with collision; source authority controls cheat access."},
  };
  static const struct { const char *name; const char *summary; } q1_extra[] = {
    {"pause", "Toggle server pause when pausable permits it."},
    {"status", "Show the current source server and connected players."},
    {"ping", "Show measured client round trip times."},
  };
  bool q1 = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  size_t total = 8 + (q1 ? 3 : 0);
  registration->names = calloc(total, sizeof *registration->names);
  registration->ctxs = calloc(total, sizeof *registration->ctxs);
  registration->doc_storage = calloc(total, 256);
  if (registration->names == NULL || registration->ctxs == NULL || registration->doc_storage == NULL)
    qa_range_error("Out of memory");
  static const char *const give_examples[] = {"give all", "give health 100"};
  for (size_t i = 0; i < total; i++) {
    const char *name = i < 8 ? base[i].name : q1_extra[i - 8].name;
    const char *summary = i < 8 ? base[i].summary : q1_extra[i - 8].summary;
    if (qa_command_buffer_exists(commands, name)) continue;
    char *usage = registration->doc_storage + i * 256;
    if (is(name, "give"))
      snprintf(usage, 256, "give [client slot: server console only] <all|health|armor|weapons|ammo|keys|item> [amount]");
    else
      snprintf(usage, 256, "%s [client slot: server console only]", name);
    const char *example_storage[1];
    example_storage[0] = name;
    qa_command_documentation documentation = {summary, usage,
        is(name, "give") ? give_examples : example_storage, is(name, "give") ? 2 : 1, NULL, 0};
    command_ctx *ctx = &registration->ctxs[registration->count];
    ctx->execute = execute; ctx->execute_ctx = execute_ctx; ctx->name = name;
    if (qa_command_buffer_register(commands, name, handler, ctx, &documentation, QA_COMMAND_WORLD))
      registration->names[registration->count++] = name;
  }
  return registration;
}
void qa_app_q1_client_commands_release(qa_app_q1_client_commands *registration) {
  if (registration == NULL) return;
  qa_app_q1_view_commands_release(registration->view);
  for (size_t i = 0; i < registration->count; i++)
    qa_command_buffer_unregister(registration->commands, registration->names[i], NULL, NULL, false);
  free(registration->names); free(registration->ctxs); free(registration->doc_storage); free(registration);
}
qa_actor_id qa_app_resolve_q1_host_command_actor(const char *name,
    const char *const *args, size_t arg_count, const qa_command_context *source,
    const qa_app_q1_host_player *players, size_t player_count,
    qa_app_local_actor_fn local_actor, void *local_actor_ctx, bool takes_arguments) {
  const qa_command_origin *origin = source != NULL ? &source->origin : NULL;
  while (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin != NULL && origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE) {
    const char *target = arg_count > 0 ? args[0] : NULL;
    bool digits = target != NULL && target[0] != '\0';
    if (digits) for (const char *p = target; *p != '\0'; p++) if (!isdigit((unsigned char)*p)) digits = false;
    if ((!takes_arguments && arg_count != 1) || target == NULL || !digits) {
      char slots[256] = "";
      for (size_t i = 0; i < player_count; i++) {
        char piece[32];
        snprintf(piece, sizeof piece, "%s%lld", i == 0 ? "" : ", ", (long long)players[i].client.slot);
        strncat(slots, piece, sizeof slots - strlen(slots) - 1);
      }
      qa_range_error("Usage: %s <client slot>; connected slots: %s", name, player_count == 0 ? "none" : slots);
    }
    long long slot = strtoll(target, NULL, 10);
    for (size_t i = 0; i < player_count; i++)
      if (players[i].client.slot == slot) return players[i].actor;
    qa_range_error("Client slot %s is not on the server", target);
  }
  if (!takes_arguments && arg_count != 0)
    qa_range_error("%s: only the server console may select a client slot", name);
  if (origin != NULL && (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT || origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT)) {
    for (size_t i = 0; i < player_count; i++)
      if (qa_same_client(players[i].client, origin->client)) return players[i].actor;
    qa_range_error("Command requires an admitted client");
  }
  return local_actor(local_actor_ctx);
}
