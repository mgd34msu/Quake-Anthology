/* Port of src/network/q3/client-authorization.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_CLIENT_AUTHORIZATION_H
#define QA_NETWORK_Q3_CLIENT_AUTHORIZATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/core/cvars/index.h"
#include "qa/core/q3_cd_key.h"
#include "qa/network/q3/authorization.h"
#include "qa/network/q3/message.h"
typedef bool (*qa_q3_client_auth_bool_fn)(void *ctx);
/* TS Q3ClientAuthorizationOptions. resolve NULL = default resolver;
 * resolve returning false or non-ipv4 mirrors TS null/result handling. */
typedef struct qa_q3_client_authorization_options {
  qa_cvar_registry *cvars;
  const qa_q3_cd_key_state *keys;
  qa_q3_client_auth_bool_fn demo_restricted;
  qa_q3_auth_resolve_fn resolve; /* may be NULL */
  qa_q3_message_print_fn print; void *print_ctx;
  void *ctx;
} qa_q3_client_authorization_options;
typedef void (*qa_q3_client_auth_assert_fn)(void *ctx);
typedef void (*qa_q3_client_auth_send_fn)(void *ctx, const qa_q3_mirror_ipv4_address *address, const uint8_t *packet, size_t len);
typedef struct qa_q3_client_authorization qa_q3_client_authorization;
bool qa_q3_client_authorization_init(qa_q3_client_authorization **out, const qa_q3_client_authorization_options *options);
void qa_q3_client_authorization_destroy(qa_q3_client_authorization *a);
void qa_q3_client_authorization_request(qa_q3_client_authorization *a, qa_q3_client_auth_assert_fn assert_current, void *assert_ctx,
                                        qa_q3_client_auth_send_fn send, void *send_ctx);
#endif
