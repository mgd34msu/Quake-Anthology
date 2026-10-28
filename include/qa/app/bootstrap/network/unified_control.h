/* Port of src/app/bootstrap/network/unified-control.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_CONTROL_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/unified_components.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/protocol.h"
typedef enum qa_app_unified_control_kind {
  QA_APP_UNIFIED_OFFER, QA_APP_UNIFIED_READY, QA_APP_UNIFIED_ADMITTED,
  QA_APP_UNIFIED_RESOURCES, QA_APP_UNIFIED_EVENTS, QA_APP_UNIFIED_COMPONENTS,
  QA_APP_UNIFIED_COMPONENT_COMMAND, QA_APP_UNIFIED_COMMAND,
  QA_APP_UNIFIED_USERINFO, QA_APP_UNIFIED_DISCONNECT
} qa_app_unified_control_kind;
typedef struct qa_app_unified_actor_reference { double slot, generation; } qa_app_unified_actor_reference;
typedef struct qa_app_unified_control {
  qa_app_unified_control_kind kind;
  double epoch;
  /* offer */ qa_app_unified_composition *composition; const char *mode; double max_clients;
  /* ready */ const char *composition_digest, *userinfo;
  /* admitted */ qa_app_unified_actor_reference client, actor; double source_entity;
  /* resources */ qa_app_unified_resource_key *resources; size_t resource_count;
  /* events */ double frame; uint8_t *payload; size_t payload_length;
  uint8_t *simulation; size_t simulation_length;
  /* components */ qa_app_unified_component_update update; bool has_update;
  /* component-command */ qa_presentation_owner owner; double generation;
  const char **args; size_t arg_count;
  /* command */ const char *name;
  /* userinfo */ const char *value;
  /* disconnect */ const char *reason;
} qa_app_unified_control;
void qa_app_unified_control_free(qa_app_unified_control *control);
bool qa_app_unified_command_in_bounds(const qa_user_command *command);
uint8_t *qa_app_unified_encode_control(const qa_app_unified_control *value, size_t *out_length);
qa_app_unified_control qa_app_unified_decode_control(const uint8_t *bytes, size_t length);
typedef struct qa_app_unified_input {
  double sequence; qa_user_command command;
  bool has_arsenal; qa_arsenal_intent arsenal;
} qa_app_unified_input;
typedef struct qa_app_unified_input_batch {
  double epoch; qa_app_unified_input *commands; size_t command_count;
} qa_app_unified_input_batch;
void qa_app_unified_input_batch_free(qa_app_unified_input_batch *batch);
uint8_t *qa_app_unified_encode_inputs(const qa_app_unified_input_batch *value, size_t *out_length);
qa_app_unified_input_batch qa_app_unified_decode_inputs(const uint8_t *bytes, size_t length);
typedef enum qa_app_unified_handshake_kind {
  QA_APP_UNIFIED_HELLO, QA_APP_UNIFIED_CHALLENGE, QA_APP_UNIFIED_CONNECT
} qa_app_unified_handshake_kind;
typedef struct qa_app_unified_handshake {
  qa_app_unified_handshake_kind kind; const char *nonce, *token;
} qa_app_unified_handshake;
uint8_t *qa_app_unified_encode_handshake(const qa_app_unified_handshake *value, size_t *out_length);
/* false is TS null (any malformed input). */
bool qa_app_unified_decode_handshake(const uint8_t *bytes, size_t length,
    qa_app_unified_handshake *out);
#endif
