/* Port of src/network/q3/client-authorization.ts. GPL-2.0-or-later. */
#include "qa/network/q3/client_authorization.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/connectionless.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
struct qa_q3_client_authorization {
  qa_q3_client_authorization_options options;
  bool has_address; qa_q3_mirror_ipv4_address address;
};
static bool default_resolve(qa_q3_mirror_ipv4_address *out) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo("authorize.quake3arena.com", "27952", &hints, &res) != 0 || !res) return false;
  struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
  memset(out, 0, sizeof *out);
  out->kind = QA_Q3_ADDR_IPV4;
  memcpy(out->host, &sin->sin_addr.s_addr, 4);
  out->port = 27952;
  freeaddrinfo(res);
  return true;
}
bool qa_q3_client_authorization_init(qa_q3_client_authorization **out, const qa_q3_client_authorization_options *options) {
  qa_q3_client_authorization *a = calloc(1, sizeof *a);
  if (!a) return false;
  a->options = *options;
  *out = a; return true;
}
void qa_q3_client_authorization_destroy(qa_q3_client_authorization *a) { free(a); }
typedef struct resolve_scope { qa_q3_client_authorization_options *o; qa_q3_mirror_ipv4_address *addr; char *err; size_t err_len; bool ok; } resolve_scope;
static void resolve_body(void *ctx) {
  resolve_scope *scope = ctx;
  scope->ok = scope->o->resolve ? scope->o->resolve(scope->o->ctx, scope->addr, scope->err, scope->err_len) : default_resolve(scope->addr);
}
static double cvar_integer(qa_cvar_registry *cvars, const char *name) {
  qa_cvar_snapshot snap;
  if (!qa_cvar_registry_get(cvars, name, &snap)) return 0;
  double value = snap.integer_value;
  qa_cvar_read_free(&snap);
  return value;
}
void qa_q3_client_authorization_request(qa_q3_client_authorization *a, qa_q3_client_auth_assert_fn assert_current, void *assert_ctx,
                                        qa_q3_client_auth_send_fn send, void *send_ctx) {
  qa_q3_client_authorization_options *o = &a->options;
  assert_current(assert_ctx);
  /* The key is sent in cleartext to a legacy authority, so it leaves the machine only when the player opts in. */
  qa_cvar_snapshot registered;
  if (qa_cvar_registry_register(o->cvars, "cl_authorizeKey", "0", QA_CVAR_FLAG_ARCHIVE, &registered)) qa_cvar_read_free(&registered);
  if (cvar_integer(o->cvars, "cl_authorizeKey") == 0) return;
  if (!a->has_address) {
    qa_q3_mirror_ipv4_address addr; memset(&addr, 0, sizeof addr);
    char err[128] = "";
    /* TS `catch { address = null; }` around the resolver. */
    resolve_scope scope = { o, &addr, err, sizeof err, false };
    qa_caught_error caught;
    bool ok = qa_try(resolve_body, &scope, &caught) && scope.ok;
    if (!ok || addr.kind != QA_Q3_ADDR_IPV4) {
      assert_current(assert_ctx);
      if (o->print) o->print(o->print_ctx, "Couldn't resolve Q3 authorization server\n");
      return;
    }
    assert_current(assert_ctx);
    a->address = addr; a->has_address = true;
  }
  char key[40] = "";
  if (o->demo_restricted(o->ctx)) {
    strcpy(key, "demota");
  } else {
    uint8_t bytes[33];
    qa_q3_cd_key_state_read_authorization(o->keys, bytes, sizeof bytes);
    size_t n = 0;
    for (int i = 0; i < 32; i++) {
      uint8_t b = bytes[i];
      if (b == 0) break;
      if ((b >= 48 && b <= 57) || (b >= 65 && b <= 90) || (b >= 97 && b <= 122)) key[n++] = (char)b;
    }
    key[n] = 0;
  }
  qa_cvar_snapshot snap;
  if (qa_cvar_registry_register(o->cvars, "cl_anonymous", "0", QA_CVAR_FLAG_INIT | QA_CVAR_FLAG_SYSTEM_INFO, &snap)) qa_cvar_read_free(&snap);
  double anonymous = cvar_integer(o->cvars, "cl_anonymous");
  assert_current(assert_ctx);
  char buf[96];
  snprintf(buf, sizeof buf, "getKeyAuthorize %lld %s", (long long)anonymous, key);
  uint8_t *packet = NULL; size_t len = 0;
  if (qa_q3_encode_connectionless_text(buf, &packet, &len)) {
    send(send_ctx, &a->address, packet, len);
    free(packet);
  }
  assert_current(assert_ctx);
}
