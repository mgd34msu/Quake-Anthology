/* Port of src/network/q3/authorization.ts. GPL-2.0-or-later.
 * resolveAddress (common/endpoint.ts, another slice) defaults to
 * getaddrinfo("authorize.quake3arena.com", 27952, IPv4). */
#ifndef QA_NETWORK_Q3_AUTHORIZATION_H
#define QA_NETWORK_Q3_AUTHORIZATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/admission.h"
#include "qa/network/q3/message.h"
/* Resolve callback: true fills out; false writes an error message into err. NULL = default resolver. */
typedef bool (*qa_q3_auth_resolve_fn)(void *ctx, qa_q3_mirror_ipv4_address *out, char *err, size_t err_len);
typedef bool (*qa_q3_auth_bool_fn)(void *ctx);
typedef const char *(*qa_q3_auth_string_fn)(void *ctx);
typedef void (*qa_q3_auth_send_fn)(void *ctx, const qa_q3_address *address, const uint8_t *packet, size_t len);
/* TS Q3ServerAuthorizationOptions. */
typedef struct qa_q3_server_authorization_options {
  qa_q3_auth_resolve_fn resolve; /* may be NULL */
  qa_q3_auth_bool_fn enabled;
  qa_q3_auth_string_fn game_directory, strict_auth;
  qa_q3_auth_send_fn send;
  qa_q3_message_print_fn print; void *print_ctx;
  void *ctx;
} qa_q3_server_authorization_options;
typedef struct qa_q3_server_authorization qa_q3_server_authorization;
bool qa_q3_server_authorization_init(qa_q3_server_authorization **out, const qa_q3_server_authorization_options *options);
void qa_q3_server_authorization_destroy(qa_q3_server_authorization *a);
/* address getter: false = null. */
bool qa_q3_server_authorization_get_address(const qa_q3_server_authorization *a, qa_q3_mirror_ipv4_address *out);
void qa_q3_server_authorization_request(qa_q3_server_authorization *a, const qa_q3_challenge *challenge);
#endif
