/* Port of src/network/q3/configstrings.ts. GPL-2.0-or-later.
 * Q3ServerConnection is reduced to the surface this module uses
 * (phase, reliable ring, bindings print/drop/assertCurrent) as fn+ctx,
 * avoiding a server.h cycle; server.c adapts its connection type. */
#ifndef QA_NETWORK_Q3_CONFIGSTRINGS_H
#define QA_NETWORK_Q3_CONFIGSTRINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#include "qa/network/q3/reliable.h"
/* q3ConfigstringCommands: out is malloc'd string array (caller frees). */
bool qa_q3_configstring_commands(int64_t index, const char *value, char ***out, size_t *count);
typedef enum qa_q3_configstring_client_phase {
  QA_Q3_CS_PHASE_OTHER, QA_Q3_CS_PHASE_PRIMED, QA_Q3_CS_PHASE_ACTIVE
} qa_q3_configstring_client_phase;
typedef void (*qa_q3_cs_drop_fn)(void *ctx, const char *message);
typedef void (*qa_q3_cs_assert_fn)(void *ctx);
/* TS Q3ConfigStringClient: connection surface used here. */
typedef struct qa_q3_configstring_client {
  qa_q3_configstring_client_phase phase;
  qa_q3_server_reliable_commands *reliable;
  qa_q3_message_print_fn print; void *print_ctx;
  qa_q3_cs_drop_fn drop; void *drop_ctx;
  qa_q3_cs_assert_fn assert_current; void *assert_ctx;
  bool no_server_info;
} qa_q3_configstring_client;
typedef bool (*qa_q3_cs_bool_fn)(void *ctx);
typedef const qa_q3_configstring_client *(*qa_q3_cs_clients_fn)(void *ctx, size_t *count);
/* TS Q3ConfigStringBindings. */
typedef struct qa_q3_configstring_bindings {
  qa_q3_cs_bool_fn running, restarting;
  qa_q3_cs_clients_fn clients;
  void *ctx;
} qa_q3_configstring_bindings;
typedef struct qa_q3_server_config_strings qa_q3_server_config_strings;
bool qa_q3_server_config_strings_init(qa_q3_server_config_strings **out, const qa_q3_configstring_bindings *bindings);
void qa_q3_server_config_strings_destroy(qa_q3_server_config_strings *s);
const qa_q3_configstring_bindings *qa_q3_server_config_strings_get_bindings(const qa_q3_server_config_strings *s);
const char *qa_q3_server_config_strings_get(const qa_q3_server_config_strings *s, int64_t index);
/* getBuffer: writes slice(0,bufferSize-1) into out (cap), returns out. */
const char *qa_q3_server_config_strings_get_buffer(const qa_q3_server_config_strings *s, int64_t index, int64_t buffer_size, char *out, size_t out_cap);
void qa_q3_server_config_strings_set(qa_q3_server_config_strings *s, int64_t index, const char *value); /* NULL = "" */
#endif
