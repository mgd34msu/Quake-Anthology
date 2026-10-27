/* Port of src/network/q3/authorization.ts. GPL-2.0-or-later. */
#include "qa/network/q3/authorization.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/connectionless.h"
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
struct qa_q3_server_authorization {
  qa_q3_server_authorization_options options;
  bool lookup_done, has_resolved;
  qa_q3_mirror_ipv4_address resolved;
};
static bool default_resolve(qa_q3_mirror_ipv4_address *out, char *err, size_t err_len) {
  struct addrinfo hints, *res = NULL;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
  int rc = getaddrinfo("authorize.quake3arena.com", "27952", &hints, &res);
  if (rc != 0 || !res) { snprintf(err, err_len, "%s", gai_strerror(rc)); return false; }
  struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
  memset(out, 0, sizeof *out);
  out->kind = QA_Q3_ADDR_IPV4;
  memcpy(out->host, &sin->sin_addr.s_addr, 4); /* network order = host octets in order */
  out->port = 27952;
  freeaddrinfo(res);
  return true;
}
bool qa_q3_server_authorization_init(qa_q3_server_authorization **out, const qa_q3_server_authorization_options *options) {
  qa_q3_server_authorization *a = calloc(1, sizeof *a);
  if (!a) return false;
  a->options = *options;
  *out = a; return true;
}
void qa_q3_server_authorization_destroy(qa_q3_server_authorization *a) { free(a); }
bool qa_q3_server_authorization_get_address(const qa_q3_server_authorization *a, qa_q3_mirror_ipv4_address *out) {
  if (!a->has_resolved) return false;
  *out = a->resolved; return true;
}
static bool resolve_cached(qa_q3_server_authorization *a, qa_q3_mirror_ipv4_address *out) {
  if (!a->lookup_done) {
    a->lookup_done = true;
    char err[256] = "";
    qa_q3_mirror_ipv4_address addr; memset(&addr, 0, sizeof addr);
    bool ok = a->options.resolve
      ? a->options.resolve(a->options.ctx, &addr, err, sizeof err)
      : default_resolve(&addr, err, sizeof err);
    if (!ok) {
      if (a->options.print) {
        char buf[512]; snprintf(buf, sizeof buf, "Couldn't resolve Q3 authorization server: %s\n", err);
        a->options.print(a->options.print_ctx, buf);
      }
      return false;
    }
    if (addr.kind != QA_Q3_ADDR_IPV4) {
      if (a->options.print) a->options.print(a->options.print_ctx, "Couldn't resolve Q3 authorization server: Q3 authorization requires IPv4\n");
      return false;
    }
    a->resolved = addr; a->has_resolved = true;
  }
  if (!a->has_resolved) return false;
  *out = a->resolved; return true;
}
void qa_q3_server_authorization_request(qa_q3_server_authorization *a, const qa_q3_challenge *challenge) {
  if (!challenge->has_address) return;
  qa_q3_address client; memset(&client, 0, sizeof client);
  if (!qa_q3_mirror_address_copy(&client, &challenge->address)) return;
  int64_t number = challenge->challenge;
  if (client.kind != QA_Q3_ADDR_IPV4 || !a->options.enabled(a->options.ctx)) { qa_q3_mirror_address_destroy(&client); return; }
  qa_q3_mirror_ipv4_address authority;
  if (!resolve_cached(a, &authority)) { qa_q3_mirror_address_destroy(&client); return; }
  if (!a->options.enabled(a->options.ctx)
      || !challenge->has_address || !qa_q3_mirror_same_address(&challenge->address, &client, true)
      || challenge->challenge != number) { qa_q3_mirror_address_destroy(&client); return; }
  const char *game = a->options.game_directory(a->options.ctx);
  if (!game || !game[0]) game = "baseq3";
  if (strlen(game) >= 1024) { qa_q3_mirror_address_destroy(&client); qa_range_error("Q3 authorization game directory exceeds source buffer"); }
  char buf[1400];
  snprintf(buf, sizeof buf, "getIpAuthorize %lld %u.%u.%u.%u %s 0 %s", (long long)number,
           client.host[0], client.host[1], client.host[2], client.host[3], game, a->options.strict_auth(a->options.ctx));
  uint8_t *packet = NULL; size_t len = 0;
  if (qa_q3_encode_connectionless_text(buf, &packet, &len)) {
    qa_q3_address auth = authority;
    a->options.send(a->options.ctx, &auth, packet, len);
    free(packet);
  }
  qa_q3_mirror_address_destroy(&client);
}
