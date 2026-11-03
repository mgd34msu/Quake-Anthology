/* Port of src/app/bootstrap/simulation/mod-client-checkpoint.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MOD_CLIENT_CHECKPOINT_H
#define QA_APP_BOOTSTRAP_SIMULATION_MOD_CLIENT_CHECKPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
#include "qa/world/session/mod_clients.h"
/* TS captureModClientCommand: an owned save-value tree (TS plain
 * object; command keys follow the TS UserCommand interface order). */
qa_save_value *qa_app_sim_capture_mod_client_command(const qa_mod_client_command_receipt *value, qa_client_id client);
/* TS readModClientCommands host. client false is the TS null. */
typedef struct qa_app_sim_mod_client_command_host {
  const qa_identity_owner *identity;
  void *ctx;
  qa_actor_id (*actor)(void *ctx, qa_saved_actor_id saved);
  bool (*client)(void *ctx, qa_actor_id actor, qa_client_id *out);
} qa_app_sim_mod_client_command_host;
/* commands[i].input.arsenal points into arsenals (owned with the
 * result); strings are borrowed from the reader's tree. */
typedef struct qa_app_sim_mod_client_commands {
  qa_mod_client_command_receipt *commands;
  qa_arsenal_intent *arsenals;
  size_t count;
} qa_app_sim_mod_client_commands;
qa_app_sim_mod_client_commands qa_app_sim_read_mod_client_commands(const qa_save_reader *reader,
                                                                   qa_app_sim_mod_client_command_host host);
void qa_app_sim_mod_client_commands_free(qa_app_sim_mod_client_commands *commands);
#endif
