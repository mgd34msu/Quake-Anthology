/* Port of src/app/bootstrap/network/socks-settings.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_SOCKS_SETTINGS_H
#define QA_APP_BOOTSTRAP_NETWORK_SOCKS_SETTINGS_H
#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
#include "qa/core/commands/text.h"
#include "qa/core/cvars/index.h"
#include "qa/network/common/transport.h"
typedef struct qa_app_client_socks_settings qa_app_client_socks_settings;
/* NULL only on OOM. Owns its Q3-dialect registry. */
qa_app_client_socks_settings *qa_app_client_socks_settings_create(
    qa_command_context context, qa_command_print_fn print, void *print_ctx);
void qa_app_client_socks_settings_destroy(qa_app_client_socks_settings *settings);
qa_cvar_registry *qa_app_client_socks_settings_cvars(qa_app_client_socks_settings *settings);
/* TS route(): wraps `base` (copied by value). The returned routing's
 * ctx borrows the settings; visible list storage is owned by the
 * settings and refreshed per call. */
qa_command_cvar_routing qa_app_client_socks_settings_route(
    qa_app_client_socks_settings *settings, qa_command_cvar_routing base);
/* TS connect(): async -> synchronous. `cvars` NULL uses own registry. */
void qa_app_client_socks_settings_connect(qa_app_client_socks_settings *settings,
    qa_udp_transport *transport, qa_cvar_registry *cvars);
#endif
