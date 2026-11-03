/* Port of src/network/q2/kex/discovery.ts. GPL-2.0-or-later. */
#include "qa/network/q2/kex/discovery.h"
#include "qa/foundation/js.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include <sys/socket.h>
#include <unistd.h>

#include "qa/network/q2/constants.h"
#include "qa/network/q2/handshake.h"
#include "qa/network/q2/kex/packet.h"
#include "qa/foundation/js_case.h"

#define SERVICE_NAME "_game._udp.local"
#define INSTANCE_NAME "Quake II._game._udp.local"
#define MULTICAST_ADDR "224.0.0.251"

static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s, n + 1);
  return out;
}
/* TS name.toLowerCase() over UTF-8 DNS labels (Unicode lowering). */
static char *dup_lower(const char *s) {
  return qa_js_to_lower_case(s);
}

uint8_t *qa_kex_discovery_query(size_t *out_length) {
  qa_kex_packet_writer writer;
  qa_kex_packet_writer_init(&writer);
  (void)qa_kex_packet_writer_string(&writer, "CRANTIME");
  (void)qa_kex_packet_writer_string(&writer, "QuakeII");
  size_t payload_len = 0;
  uint8_t *payload = qa_kex_packet_writer_finish(&writer, &payload_len);
  if (payload == NULL) return NULL;
  qa_kex_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.flags = 0;
  packet.kind = 129;
  packet.payload = payload;
  packet.payload_length = payload_len;
  uint8_t *out = qa_kex_write_packet(&packet, out_length);
  free(payload);
  return out;
}

/* The reader raises (truncated string or integer, invalid UTF-8) on remote bytes; what one
 * decode owns is released on that path so a catching caller (the browser's poll) leaks nothing. */
typedef struct kex_status_scope {
  char *name, *key, *value;
  qa_qw_info rules;
} kex_status_scope;
static void kex_status_scope_release(void *arg) {
  kex_status_scope *scope = arg;
  free(scope->name); free(scope->key); free(scope->value);
  scope->name = scope->key = scope->value = NULL;
  qa_qw_info_destroy(&scope->rules);
}
bool qa_kex_read_discovery(const uint8_t *bytes, size_t length,
                           qa_server_status *out) {
  memset(out, 0, sizeof *out);
  kex_status_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_qw_info_init(&scope.rules);
  qa_cleanup guard;
  qa_cleanup_push(&guard, kex_status_scope_release, &scope);
  qa_kex_packet_reader reader;
  qa_kex_packet_reader_init(&reader, bytes, length);
  bool ok = false;
  scope.name = qa_kex_packet_reader_string(&reader);
  if (scope.name != NULL) {
    uint64_t players = qa_kex_packet_reader_integer(&reader);
    uint64_t maximum = qa_kex_packet_reader_integer(&reader);
    if (players > 255 || maximum > 255 || players > maximum || strlen(scope.name) > 1024)
      qa_q2_throw_error("Invalid KEX lobby status");
    bool oom = false;
    while (qa_kex_packet_reader_remaining(&reader) != 0) {
      scope.key = qa_kex_packet_reader_string(&reader);
      if (scope.key == NULL) { oom = true; break; }
      scope.value = qa_kex_packet_reader_string(&reader);
      if (scope.value == NULL) { oom = true; break; }
      if (scope.rules.count >= 256) qa_q2_throw_error("KEX lobby has too many attributes");
      qa_qw_info_set(&scope.rules, scope.key, scope.value);
      free(scope.key); free(scope.value);
      scope.key = scope.value = NULL;
    }
    const char *mapname = qa_qw_info_get(&scope.rules, "map");
    char *map = oom ? NULL : dup_str(mapname != NULL ? mapname : "");
    if (map != NULL) {
      out->name = scope.name;
      out->map = map;
      out->players = (double)players;
      out->max_players = (double)maximum;
      out->rules = scope.rules;
      memset(&out->wire, 0, sizeof out->wire);
      out->wire.protocol.kind = "q2-kex";
      out->wire.protocol.version = 2023;
      out->wire.protocol.flags = 0;
      out->wire.protocol.revision = 0;
      scope.name = NULL;
      qa_qw_info_init(&scope.rules);
      ok = true;
    }
  }
  qa_cleanup_pop(&guard);
  kex_status_scope_release(&scope);
  return ok;
}

/* --- mDNS helpers (TS domain/short/record) --- */

typedef struct byte_list {
  uint8_t *data;
  size_t length, capacity;
} byte_list;

static bool bytes_push(byte_list *list, const uint8_t *data, size_t length) {
  if (list->length + length > list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity : 128;
    while (next < list->length + length) next *= 2;
    uint8_t *grown = (uint8_t *)realloc(list->data, next);
    if (grown == NULL) return false;
    list->data = grown;
    list->capacity = next;
  }
  if (length > 0) memcpy(list->data + list->length, data, length);
  list->length += length;
  return true;
}
static bool byte_push(byte_list *list, uint8_t value) {
  return bytes_push(list, &value, 1);
}
static bool short_push(byte_list *list, double value) {
  uint32_t v = qa_js_to_uint32(value); /* value >>> 8 & 255, value & 255 */
  uint8_t pair[2] = {(uint8_t)((v >> 8) & 255u), (uint8_t)(v & 255u)};
  return bytes_push(list, pair, 2);
}
static bool domain_push(byte_list *list, const char *value) {
  const char *start = value;
  for (;;) {
    const char *dot = strchr(start, '.');
    size_t len = dot != NULL ? (size_t)(dot - start) : strlen(start);
    if (len < 1 || len > 63) qa_q2_throw_error("Invalid mDNS label");
    if (!byte_push(list, (uint8_t)len)) return false;
    if (!bytes_push(list, (const uint8_t *)start, len)) return false;
    if (dot == NULL) break;
    start = dot + 1;
  }
  return byte_push(list, 0);
}
static bool record_push(byte_list *list, const char *name, double type,
                        const uint8_t *data, size_t data_length, double ttl) {
  if (!domain_push(list, name)) return false;
  if (!short_push(list, type)) return false;
  if (!short_push(list, type == 12 ? 1 : 0x8001)) return false;
  if (!byte_push(list, 0) || !byte_push(list, 0)) return false;
  if (!short_push(list, ttl)) return false;
  if (!short_push(list, (double)data_length)) return false;
  return bytes_push(list, data, data_length);
}

/* TS DnsReader. */
typedef struct dns_reader {
  const uint8_t *bytes;
  size_t length;
  size_t position;
} dns_reader;

static uint16_t dns_u16_at(const uint8_t *bytes, size_t length, size_t offset) {
  /* DataView defaults to big-endian; out-of-bounds throws in TS. */
  if (offset + 2 > length) qa_q2_throw_error("Offset is outside the bounds of the DataView");
  return (uint16_t)(((uint32_t)bytes[offset] << 8) | bytes[offset + 1]);
}
static int64_t dns_word(dns_reader *reader) {
  int64_t value = (int64_t)dns_u16_at(reader->bytes, reader->length, reader->position);
  reader->position += 2;
  return value;
}
static uint8_t byte_at(const uint8_t *bytes, size_t length, size_t offset, const char *message) {
  if (offset >= length) qa_q2_throw_error("%s", message);
  return bytes[offset];
}
static bool utf8_check(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = bytes[i];
    if (c < 0x80) { i++; continue; }
    int need;
    uint32_t cp;
    if ((c & 0xe0) == 0xc0) { need = 1; cp = c & 0x1f; if (cp == 0) return false; }
    else if ((c & 0xf0) == 0xe0) { need = 2; cp = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0) { need = 3; cp = c & 0x07; }
    else return false;
    if (i + (size_t)need >= length) return false;
    for (int k = 0; k < need; k++) {
      uint8_t d = bytes[i + 1 + (size_t)k];
      if ((d & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (uint32_t)(d & 0x3f);
    }
    if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
        (need == 3 && cp < 0x10000)) return false;
    if ((cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) return false;
    i += (size_t)need + 1;
  }
  return true;
}
static void byte_list_release(void *arg) {
  byte_list *list = (byte_list *)arg;
  free(list->data);
  list->data = NULL;
}
static char *dns_name(dns_reader *reader, bool use_position, size_t initial_offset,
                      bool advance) {
  size_t offset = use_position ? reader->position : initial_offset;
  bool has_end = false;
  size_t end = 0;
  byte_list joined = {NULL, 0, 0};
  qa_cleanup node;
  qa_cleanup_push(&node, byte_list_release, &joined);
  size_t seen[128];
  size_t seen_count = 0;
  char *out = NULL;
  for (;;) {
    bool dup = false;
    for (size_t i = 0; i < seen_count; i++) {
      if (seen[i] == offset) dup = true;
    }
    if (dup || seen_count >= 128) qa_q2_throw_error("Invalid mDNS compression");
    seen[seen_count++] = offset;
    uint8_t size = byte_at(reader->bytes, reader->length, offset++, "Truncated mDNS name");
    if (size == 0) {
      if (advance) reader->position = has_end ? end : offset;
      size_t n = joined.length > 0 ? joined.length - 1 : 0;
      out = (char *)malloc(n + 1);
      if (out != NULL) {
        if (n > 0) memcpy(out, joined.data, n);
        out[n] = '\0';
      }
      break;
    }
    if ((size & 192) == 192) {
      uint8_t low = byte_at(reader->bytes, reader->length, offset++, "Truncated mDNS pointer");
      if (!has_end) {
        end = offset;
        has_end = true;
      }
      offset = ((size_t)(size & 63) << 8) | low;
      continue;
    }
    if (size > 63 || offset + size > reader->length) qa_q2_throw_error("Invalid mDNS label");
    if (!utf8_check(reader->bytes + offset, size)) qa_q2_throw_utf8_error();
    if (!bytes_push(&joined, reader->bytes + offset, size) || !byte_push(&joined, '.')) break;
    offset += size;
  }
  qa_cleanup_pop(&node);
  free(joined.data);
  return out;
}

typedef struct dns_record {
  char *name;
  int64_t type;
  size_t start;
  int64_t length;
} dns_record;

typedef struct dns_parsed {
  bool question;
  dns_record *records;
  size_t record_count;
} dns_parsed;

static void dns_parsed_free(dns_parsed *parsed) {
  if (parsed->records != NULL) {
    for (size_t i = 0; i < parsed->record_count; i++) free(parsed->records[i].name);
    free(parsed->records);
  }
}

static void free_arg(void *arg) { free(arg); }
/* Fills `out` in place so a raise leaves every record it read owned by `out`; the caller frees
 * it with dns_parsed_free on every path. */
static bool dns_read(dns_reader *reader, dns_parsed *out) {
  memset(out, 0, sizeof *out);
  int64_t questions = (int64_t)dns_u16_at(reader->bytes, reader->length, 4);
  int64_t count = (int64_t)dns_u16_at(reader->bytes, reader->length, 6) +
                  (int64_t)dns_u16_at(reader->bytes, reader->length, 8) +
                  (int64_t)dns_u16_at(reader->bytes, reader->length, 10);
  if (questions + count > 256) qa_q2_throw_error("Too many mDNS records");
  for (int64_t i = 0; i < questions; i++) {
    char *name = dns_name(reader, true, 0, true);
    if (name == NULL) return false;
    qa_cleanup node;
    qa_cleanup_push(&node, free_arg, name);
    int64_t type = dns_word(reader);
    (void)dns_word(reader);
    qa_cleanup_pop(&node);
    char *lower = dup_lower(name);
    free(name);
    if (lower == NULL) return false;
    if (strcmp(lower, SERVICE_NAME) == 0 && (type == 12 || type == 255)) out->question = true;
    free(lower);
  }
  if (count > 0) {
    out->records = (dns_record *)calloc((size_t)count, sizeof *out->records);
    if (out->records == NULL) return false;
  }
  for (int64_t i = 0; i < count; i++) {
    char *name = dns_name(reader, true, 0, true);
    if (name == NULL) return false;
    dns_record *record = &out->records[out->record_count++];
    record->name = name;
    record->type = dns_word(reader);
    (void)dns_word(reader);
    reader->position += 4;
    int64_t rec_len = dns_word(reader);
    size_t start = reader->position;
    reader->position += (size_t)(rec_len > 0 ? rec_len : 0);
    if (reader->position > reader->length) qa_q2_throw_error("Truncated mDNS record");
    record->start = start;
    record->length = rec_len;
  }
  return true;
}

static void mdns_notify_failed(qa_kex_mdns *mdns, const char *message) {
  if (mdns->failed != NULL) mdns->failed(mdns->failed_ctx, message);
}
static bool mdns_send(qa_kex_mdns *mdns, const uint8_t *bytes, size_t length) {
  struct sockaddr_in target;
  memset(&target, 0, sizeof target);
  target.sin_family = AF_INET;
  target.sin_port = htons(5353);
  target.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);
  ssize_t sent = sendto(mdns->socket_fd, bytes, length, MSG_DONTWAIT,
                        (struct sockaddr *)&target, sizeof target);
  if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
    mdns_notify_failed(mdns, strerror(errno));
    return false;
  }
  return sent >= 0;
}

static bool parse_ip_text(const char *host, double port, qa_net_address *out) {
  return qa_net_make_ip(out, host, (int64_t)port, port == 0) != 0;
}

static void mdns_announce(qa_kex_mdns *mdns, double ttl);

static bool set_endpoint(qa_kex_mdns *mdns, const char *name, const char *target,
                         double port) {
  for (size_t i = 0; i < mdns->endpoint_count; i++) {
    if (strcmp(mdns->endpoints[i].name, name) == 0) {
      char *copy = dup_str(target);
      if (copy == NULL) return false;
      free(mdns->endpoints[i].target);
      mdns->endpoints[i].target = copy;
      mdns->endpoints[i].port = port;
      return true;
    }
  }
  if (mdns->endpoint_count == mdns->endpoint_capacity) {
    size_t next = mdns->endpoint_capacity > 0 ? mdns->endpoint_capacity * 2 : 8;
    qa_kex_mdns_endpoint *grown =
        (qa_kex_mdns_endpoint *)realloc(mdns->endpoints, next * sizeof *grown);
    if (grown == NULL) return false;
    mdns->endpoints = grown;
    mdns->endpoint_capacity = next;
  }
  char *owned_name = dup_str(name);
  char *owned_target = dup_str(target);
  if (owned_name == NULL || owned_target == NULL) {
    free(owned_name);
    free(owned_target);
    return false;
  }
  mdns->endpoints[mdns->endpoint_count].name = owned_name;
  mdns->endpoints[mdns->endpoint_count].target = owned_target;
  mdns->endpoints[mdns->endpoint_count].port = port;
  mdns->endpoint_count++;
  return true;
}

static bool set_address(qa_kex_mdns *mdns, const char *name_lower,
                        const qa_net_address *address) {
  for (size_t i = 0; i < mdns->address_count; i++) {
    if (strcmp(mdns->addresses[i].name, name_lower) == 0) {
      qa_net_address clone;
      if (qa_net_address_clone(address, &clone) == 0) return false;
      qa_net_address_destroy(&mdns->addresses[i].address);
      mdns->addresses[i].address = clone;
      return true;
    }
  }
  if (mdns->address_count == mdns->address_capacity) {
    size_t next = mdns->address_capacity > 0 ? mdns->address_capacity * 2 : 8;
    qa_kex_mdns_address *grown =
        (qa_kex_mdns_address *)realloc(mdns->addresses, next * sizeof *grown);
    if (grown == NULL) return false;
    mdns->addresses = grown;
    mdns->address_capacity = next;
  }
  char *owned = dup_str(name_lower);
  qa_net_address clone;
  if (owned == NULL || qa_net_address_clone(address, &clone) == 0) {
    free(owned);
    return false;
  }
  mdns->addresses[mdns->address_count].name = owned;
  mdns->addresses[mdns->address_count].address = clone;
  mdns->address_count++;
  return true;
}

static const qa_net_address *get_address(const qa_kex_mdns *mdns, const char *target) {
  for (size_t i = 0; i < mdns->address_count; i++) {
    if (strcmp(mdns->addresses[i].name, target) == 0) return &mdns->addresses[i].address;
  }
  return NULL;
}

static bool ends_with_service(const char *name) {
  const char *suffix = "." SERVICE_NAME;
  return qa_js_ends_with_ignore_case(name, suffix); /* TS toLowerCase().endsWith */
}

static void address_release(void *arg) { qa_net_address_destroy((qa_net_address *)arg); }
static void dns_parsed_release(void *arg) { dns_parsed_free((dns_parsed *)arg); }
static void mdns_receive(qa_kex_mdns *mdns, const uint8_t *bytes, size_t length) {
  if (length < 12 || length > 9000) qa_q2_throw_error("Invalid mDNS packet");
  dns_reader reader = {bytes, length, 12};
  dns_parsed parsed;
  memset(&parsed, 0, sizeof parsed);
  qa_cleanup node;
  qa_cleanup_push(&node, dns_parsed_release, &parsed);
  if (!dns_read(&reader, &parsed)) {
    qa_cleanup_pop(&node);
    dns_parsed_free(&parsed);
    return;
  }
  if (parsed.question) mdns_announce(mdns, 120);
  for (size_t i = 0; i < parsed.record_count; i++) {
    dns_record *entry = &parsed.records[i];
    if (entry->type == 33 && ends_with_service(entry->name) && entry->length >= 7) {
      int64_t port = (int64_t)dns_u16_at(bytes, length, entry->start + 4);
      char *target_raw = dns_name(&reader, false, entry->start + 6, false);
      if (target_raw == NULL) continue;
      char *target = dup_lower(target_raw);
      free(target_raw);
      if (target == NULL) continue;
      if (port != 0 && mdns->endpoint_count < 256) {
        (void)set_endpoint(mdns, entry->name, target, (double)port);
      }
      free(target);
    } else if (entry->type == 1 && entry->length == 4 && mdns->address_count < 256) {
      char text[64];
      (void)snprintf(text, sizeof text, "%u.%u.%u.%u", bytes[entry->start],
                     bytes[entry->start + 1], bytes[entry->start + 2],
                     bytes[entry->start + 3]);
      char *lower = dup_lower(entry->name);
      if (lower != NULL) {
        qa_net_address address;
        if (parse_ip_text(text, 5069, &address)) {
          (void)set_address(mdns, lower, &address);
          qa_net_address_destroy(&address);
        }
        free(lower);
      }
    } else if (entry->type == 28 && entry->length == 16 && mdns->address_count < 256) {
      char text[128];
      char *w = text;
      for (int k = 0; k < 8; k++) {
        w += sprintf(w, "%s%x", k > 0 ? ":" : "",
                     (unsigned)dns_u16_at(bytes, length, entry->start + (size_t)k * 2));
      }
      char *lower = dup_lower(entry->name);
      if (lower != NULL) {
        qa_net_address address;
        if (parse_ip_text(text, 5069, &address)) {
          (void)set_address(mdns, lower, &address);
          qa_net_address_destroy(&address);
        }
        free(lower);
      }
    }
  }
  for (size_t i = 0; i < mdns->endpoint_count; i++) {
    const qa_net_address *address = get_address(mdns, mdns->endpoints[i].target);
    if (address != NULL && mdns->found != NULL) {
      qa_net_address with_port;
      if (qa_net_address_clone(address, &with_port) != 0) {
        with_port.port = (int64_t)mdns->endpoints[i].port;
        qa_cleanup found_node;
        qa_cleanup_push(&found_node, address_release, &with_port);
        mdns->found(mdns->found_ctx, &with_port);
        qa_cleanup_pop(&found_node);
        qa_net_address_destroy(&with_port);
      }
    }
  }
  qa_cleanup_pop(&node);
  dns_parsed_free(&parsed);
}

typedef struct receive_scope {
  qa_kex_mdns *mdns;
  const uint8_t *bytes;
  size_t length;
} receive_scope;
static void receive_body(void *arg) {
  receive_scope *scope = (receive_scope *)arg;
  mdns_receive(scope->mdns, scope->bytes, scope->length);
}

bool qa_kex_mdns_open(qa_kex_mdns *mdns, bool has_advertised_port, double advertised_port,
                      qa_kex_mdns_found_fn found, void *found_ctx,
                      qa_kex_mdns_failed_fn failed, void *failed_ctx) {
  memset(mdns, 0, sizeof *mdns);
  mdns->socket_fd = -1;
  mdns->has_advertised_port = has_advertised_port;
  mdns->advertised_port = advertised_port;
  mdns->found = found;
  mdns->found_ctx = found_ctx;
  mdns->failed = failed;
  mdns->failed_ctx = failed_ctx;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  int reuse = 1;
  (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
  struct sockaddr_in local;
  memset(&local, 0, sizeof local);
  local.sin_family = AF_INET;
  local.sin_port = htons(5353);
  local.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(fd, (struct sockaddr *)&local, sizeof local) < 0) {
    (void)close(fd);
    return false;
  }
  struct ip_mreq membership;
  memset(&membership, 0, sizeof membership);
  membership.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
  membership.imr_interface.s_addr = htonl(INADDR_ANY);
  (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof membership);
  unsigned char ttl = 255;
  (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
  unsigned char loop = 1;
  (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  mdns->socket_fd = fd;
  if (has_advertised_port) mdns_announce(mdns, 120);
  return true;
}

bool qa_kex_mdns_message(qa_kex_mdns *mdns, const uint8_t *bytes, size_t length) {
  /* Unrelated or malformed multicast traffic is ignored, as the TS message handler does. */
  receive_scope scope = {mdns, bytes, length};
  qa_caught_error caught;
  return qa_try(receive_body, &scope, &caught);
}

size_t qa_kex_mdns_poll(qa_kex_mdns *mdns) {
  if (mdns->closed || mdns->socket_fd < 0) return 0;
  size_t handled = 0;
  for (;;) {
    uint8_t buffer[9216];
    ssize_t received = recvfrom(mdns->socket_fd, buffer, sizeof buffer, MSG_DONTWAIT,
                                NULL, NULL);
    if (received < 0) break;
    (void)qa_kex_mdns_message(mdns, buffer, (size_t)received);
    handled++;
  }
  return handled;
}

void qa_kex_mdns_query(qa_kex_mdns *mdns) {
  if (mdns->closed) return;
  byte_list bytes = {NULL, 0, 0};
  static const uint8_t header[12] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
  if (!bytes_push(&bytes, header, 12) || !domain_push(&bytes, SERVICE_NAME) ||
      !byte_push(&bytes, 0) || !byte_push(&bytes, 12) || !byte_push(&bytes, 0) ||
      !byte_push(&bytes, 1)) {
    free(bytes.data);
    return;
  }
  (void)mdns_send(mdns, bytes.data, bytes.length);
  free(bytes.data);
}

void qa_kex_mdns_close(qa_kex_mdns *mdns) {
  if (mdns->closed) return;
  if (mdns->has_advertised_port) mdns_announce(mdns, 0);
  mdns->closed = true;
  if (mdns->socket_fd >= 0) (void)close(mdns->socket_fd);
  mdns->socket_fd = -1;
  if (mdns->endpoints != NULL) {
    for (size_t i = 0; i < mdns->endpoint_count; i++) {
      free(mdns->endpoints[i].name);
      free(mdns->endpoints[i].target);
    }
    free(mdns->endpoints);
    mdns->endpoints = NULL;
  }
  if (mdns->addresses != NULL) {
    for (size_t i = 0; i < mdns->address_count; i++) {
      free(mdns->addresses[i].name);
      qa_net_address_destroy(&mdns->addresses[i].address);
    }
    free(mdns->addresses);
    mdns->addresses = NULL;
  }
}

static void mdns_announce(qa_kex_mdns *mdns, double ttl) {
  if (!mdns->has_advertised_port || mdns->closed) return;
  char hostbuf[256];
  const char *host = gethostname(hostbuf, sizeof hostbuf) == 0 ? hostbuf : "";
  char cleaned[64];
  size_t used = 0;
  for (const char *p = host; *p != '\0' && used < 63; p++) {
    cleaned[used++] = isalnum((unsigned char)*p) || *p == '-' ? *p : '-';
  }
  cleaned[used] = '\0';
  char target[80];
  (void)snprintf(target, sizeof target, "%s.local", cleaned);
  /* Build records, counting first for the header. */
  typedef struct rec {
    byte_list bytes;
  } rec_t;
  rec_t records[24];
  size_t record_count = 0;
  memset(records, 0, sizeof records);
  byte_list instance_domain = {NULL, 0, 0};
  (void)domain_push(&instance_domain, INSTANCE_NAME);
  byte_list first = {NULL, 0, 0};
  (void)record_push(&first, SERVICE_NAME, 12, instance_domain.data, instance_domain.length, ttl);
  records[record_count++].bytes = first;
  free(instance_domain.data);
  byte_list srv_data = {NULL, 0, 0};
  (void)byte_push(&srv_data, 0);
  (void)byte_push(&srv_data, 0);
  (void)byte_push(&srv_data, 0);
  (void)byte_push(&srv_data, 0);
  (void)short_push(&srv_data, mdns->advertised_port);
  (void)domain_push(&srv_data, target);
  byte_list second = {NULL, 0, 0};
  (void)record_push(&second, INSTANCE_NAME, 33, srv_data.data, srv_data.length, ttl);
  records[record_count++].bytes = second;
  free(srv_data.data);
  uint8_t txt_data = 0;
  byte_list third = {NULL, 0, 0};
  (void)record_push(&third, INSTANCE_NAME, 16, &txt_data, 1, ttl);
  records[record_count++].bytes = third;
  struct ifaddrs *interfaces = NULL;
  if (getifaddrs(&interfaces) == 0) {
    for (struct ifaddrs *entry = interfaces; entry != NULL && record_count < 24;
         entry = entry->ifa_next) {
      if (entry->ifa_addr == NULL || entry->ifa_addr->sa_family != AF_INET) continue;
      if ((entry->ifa_flags & IFF_LOOPBACK) != 0) continue;
      struct sockaddr_in *sin = (struct sockaddr_in *)entry->ifa_addr;
      uint8_t octets[4];
      memcpy(octets, &sin->sin_addr.s_addr, sizeof octets);
      byte_list rec = {NULL, 0, 0};
      (void)record_push(&rec, target, 1, octets, sizeof octets, ttl);
      records[record_count++].bytes = rec;
    }
    freeifaddrs(interfaces);
  }
  byte_list packet = {NULL, 0, 0};
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0x84);
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0);
  (void)short_push(&packet, (double)record_count);
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0);
  (void)byte_push(&packet, 0);
  for (size_t i = 0; i < record_count; i++) {
    (void)bytes_push(&packet, records[i].bytes.data, records[i].bytes.length);
    free(records[i].bytes.data);
  }
  (void)mdns_send(mdns, packet.data, packet.length);
  free(packet.data);
}
