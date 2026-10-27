/* Port of src/network/common/commands.ts. Receiver interface ->
 * fn+ctx. Decoded arsenal strings/intent are malloc'd; free with
 * qa_unified_actor_command_dispose. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_COMMANDS_H
#define QA_NETWORK_COMMON_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
typedef struct qa_unified_command_actor {
  qa_actor_id actor;
  qa_user_command_kind movement;
  qa_provider_id arsenal;
} qa_unified_command_actor;
typedef struct qa_unified_command_receiver {
  void *ctx;
  qa_command_source source;
  /* Returns 1 and fills *out for a currently controlled actor, else 0. */
  int (*resolve_controlled_actor)(void *ctx, double slot, double generation, qa_unified_command_actor *out);
} qa_unified_command_receiver;
/* Malloc'd packet bytes, caller frees; NULL on OOM. */
uint8_t *qa_encode_unified_actor_command(const qa_actor_command *input, size_t *out_len);
typedef struct qa_decoded_actor_command {
  qa_actor_command command;
  qa_arsenal_intent intent; bool has_intent;
  char *provider, *weapon;
} qa_decoded_actor_command;
void qa_decoded_actor_command_destroy(qa_decoded_actor_command *d);
void qa_decode_unified_actor_command(const uint8_t *bytes, size_t length,
                                     const qa_unified_command_receiver *receiver,
                                     qa_decoded_actor_command *out);
#endif
