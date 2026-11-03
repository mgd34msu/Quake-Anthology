/* Port of src/app/bootstrap/server-administration.ts. Async ->
 * synchronous; resolveAddress failures are reported through the host
 * print as in TS (host resolve returns an error message or NULL).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SERVER_ADMINISTRATION_H
#define QA_APP_BOOTSTRAP_SERVER_ADMINISTRATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/common.h"
#include "qa/core/cvars/index.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/services/admin.h"
#include "qa/settings/config.h"
typedef struct qa_app_server_operator_host {
  void *ctx;
  qa_command_dialect dialect;
  qa_cvar_registry *cvars;
  bool dedicated;
  void (*print)(void *ctx, const char *text);
  bool (*send)(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length);
  void (*write_config)(void *ctx, const char *name, const char *text);
  void (*heartbeat)(void *ctx);
  /* TS resolveAddress(name, port, 4): error message or NULL on success. */
  const char *(*resolve_address)(void *ctx, const char *name, int64_t default_port, qa_net_address *out);
} qa_app_server_operator_host;
typedef struct qa_app_server_operator_state qa_app_server_operator_state;
/* TS ServerOperatorState.open. store/scope borrowed (store outlives). */
qa_app_server_operator_state *qa_app_server_operator_state_open(const qa_config_store *store, const char *scope);
void qa_app_server_operator_state_destroy(qa_app_server_operator_state *s);
typedef struct qa_app_limited_rcon { char *password; char **prefixes; size_t prefix_count; } qa_app_limited_rcon;
qa_app_limited_rcon qa_app_server_operator_limited_rcon(qa_app_server_operator_state *s, qa_cvar_registry *cvars);
void qa_app_limited_rcon_free(qa_app_limited_rcon *r);
bool qa_app_server_operator_limited_rcon_command(qa_app_server_operator_state *s, const char *name, const char *raw_args,
                                                 void (*print)(void *ctx, const char *text), void *print_ctx);
bool qa_app_server_operator_rcon_rate_allowed(qa_app_server_operator_state *s, qa_cvar_registry *cvars, double now,
                                              void (*print)(void *ctx, const char *text), void *print_ctx);
void qa_app_server_operator_recharge_rcon_rate(qa_app_server_operator_state *s);
/* Malloc'd address array (destroy each + free). */
qa_net_address *qa_app_server_operator_masters(qa_app_server_operator_state *s, qa_command_dialect dialect, size_t *out_count);
bool qa_app_server_operator_rejects(qa_app_server_operator_state *s, const qa_net_address *address, double filterban);
bool qa_app_server_operator_filter_command(qa_app_server_operator_state *s, qa_app_server_operator_host host, const char *name,
                                           const char *const *args, size_t arg_count);
void qa_app_server_operator_set_masters(qa_app_server_operator_state *s, qa_app_server_operator_host host,
                                        const char *const *names, size_t name_count);
void qa_app_server_operator_refresh_q3_masters(qa_app_server_operator_state *s, qa_app_server_operator_host host, qa_cvar_registry *cvars);
void qa_app_server_operator_state_close(qa_app_server_operator_state *s);
typedef struct qa_app_source_administration_options {
  qa_command_dialect dialect;
  qa_app_server_operator_state *state;
  qa_cvar_registry *cvars;
  bool dedicated;
  void *execute_ctx;
  void (*execute)(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx);
  void *record_ctx;
  void (*record)(void *ctx, const qa_net_address *address, const char *result);
} qa_app_source_administration_options;
const char *const *qa_app_source_administration_command_names(qa_command_dialect dialect, size_t *out_count);
void qa_app_register_source_administration_cvars(qa_cvar_registry *cvars);
/* TS sourceServerAdministration. The returned vtable's ctx holds the options copy and the
 * strings and masters it last returned (rcon_password / masters results stay valid until
 * the next call of the same member); release it with qa_app_source_server_administration_destroy. */
qa_server_administration qa_app_source_server_administration(const qa_app_source_administration_options *options);
void qa_app_source_server_administration_destroy(qa_server_administration *administration);
#endif
