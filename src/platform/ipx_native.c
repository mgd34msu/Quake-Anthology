/* Port of src/platform/ipx-native.ts (Linux branch). GPL-2.0-or-later. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "qa/platform/ipx_native.h"

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "qa/foundation/support.h"

static double port_number(double port, bool allow_zero) {
  if (!isfinite(port) || floor(port) != port || port < (allow_zero ? 0 : 1) ||
      port > 65535)
    qa_range_error("Invalid network port");
  return port;
}

qa_platform_ipx_address qa_platform_ipx_address_make(uint32_t network,
                                                     const uint8_t node[6],
                                                     double port) {
  qa_platform_ipx_address address;
  address.network = network;
  memcpy(address.node, node, sizeof address.node);
  address.port = (uint16_t)port_number(port, false);
  return address;
}

void qa_platform_ipx_receive_event_dispose(qa_platform_ipx_receive_event *event) {
  free(event->payload);
  event->payload = NULL;
  event->payload_length = 0;
}

static void checked_options(const qa_platform_ipx_bind_options *options) {
  (void)port_number(options->port, true);
  if (!isfinite(options->packet_type) || floor(options->packet_type) != options->packet_type ||
      options->packet_type < 0 || options->packet_type > 255 || !options->broadcast)
    qa_range_error("Invalid native IPX socket options");
}

/* Linux sockaddr_ipx as bytes: family, port (BE), network (BE),
 * node, packet type, pad. */
static void sockaddr_build(uint8_t bytes[16], uint16_t port, int type,
                           const qa_platform_ipx_address *address) {
  memset(bytes, 0, 16);
  bytes[0] = 4; /* AF_IPX, little-endian host order as in the TS */
  bytes[1] = 0;
  bytes[2] = (uint8_t)(port >> 8);
  bytes[3] = (uint8_t)port;
  uint32_t network = address == NULL ? 0 : address->network;
  bytes[4] = (uint8_t)(network >> 24);
  bytes[5] = (uint8_t)(network >> 16);
  bytes[6] = (uint8_t)(network >> 8);
  bytes[7] = (uint8_t)network;
  if (address != NULL) memcpy(bytes + 8, address->node, 6);
  bytes[14] = (uint8_t)type;
}

static bool address_from(const uint8_t *bytes, size_t length,
                         qa_platform_ipx_address *out,
                         qa_platform_ipx_error *error) {
  if (length < 16) {
    if (error != NULL) {
      error->unsupported = false;
      snprintf(error->message, sizeof error->message,
               "Native IPX returned a truncated address");
    }
    return false;
  }
  if (bytes[0] != 4 || bytes[1] != 0) {
    if (error != NULL) {
      error->unsupported = false;
      snprintf(error->message, sizeof error->message,
               "Native IPX returned another address family");
    }
    return false;
  }
  uint32_t network = ((uint32_t)bytes[4] << 24) | ((uint32_t)bytes[5] << 16) |
                     ((uint32_t)bytes[6] << 8) | (uint32_t)bytes[7];
  uint16_t port = (uint16_t)(((uint16_t)bytes[2] << 8) | bytes[3]);
  *out = qa_platform_ipx_address_make(network, bytes + 8, (double)port);
  return true;
}

static bool failure(qa_platform_ipx_error *error, const char *operation, int code) {
  if (error != NULL) {
    const char *name = strerrorname_np(code);
    bool family_missing = code == 97 || code == 93 || code == 94;
    bool no_route = code == 99 || code == 100 || code == 101;
    error->unsupported = family_missing;
    if (family_missing)
      snprintf(error->message, sizeof error->message,
               "ipx-native: Linux AF_IPX %s: %s (%d); the OS has no installed "
               "IPX datagram provider",
               operation, name == NULL ? "errno" : name, code);
    else if (no_route)
      snprintf(error->message, sizeof error->message,
               "Linux AF_IPX %s: %s (%d); no usable configured IPX interface "
               "or route",
               operation, name == NULL ? "errno" : name, code);
    else
      snprintf(error->message, sizeof error->message, "Linux AF_IPX %s: %s (%d)",
               operation, name == NULL ? "errno" : name, code);
  }
  return false;
}

static double performance_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

bool qa_bind_native_ipx_socket(const qa_platform_ipx_bind_options *options,
                               qa_native_ipx_socket *out,
                               qa_platform_ipx_error *error) {
  checked_options(options);
  memset(out, 0, sizeof *out);
  int fd = socket(4 /* AF_IPX */, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) return failure(error, "socket", errno);
  int one = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one) < 0) {
    int code = errno;
    close(fd);
    return failure(error, "SO_BROADCAST", code);
  }
  int packet_type = (int)options->packet_type;
  if (setsockopt(fd, 256 /* SOL_IPX */, 1 /* IPX_TYPE */, &packet_type,
                 sizeof packet_type) < 0) {
    int code = errno;
    close(fd);
    return failure(error, "IPX_TYPE", code);
  }
  uint8_t local[16];
  sockaddr_build(local, (uint16_t)options->port, packet_type, NULL);
  if (bind(fd, (const struct sockaddr *)local, sizeof local) < 0) {
    int code = errno;
    close(fd);
    return failure(error, "bind", code);
  }
  socklen_t length = sizeof local;
  if (getsockname(fd, (struct sockaddr *)local, &length) < 0) {
    int code = errno;
    close(fd);
    return failure(error, "getsockname", code);
  }
  qa_platform_ipx_address address;
  if (!address_from(local, (size_t)length, &address, error)) {
    close(fd);
    return false;
  }
  out->descriptor = fd;
  out->address = address;
  /* The portable IPX MTU is 576 bytes, including its 30-byte network header. */
  out->max_datagram_bytes = 546;
  out->packet_type = packet_type;
  out->opened = true;
  return true;
}

bool qa_native_ipx_socket_send(qa_native_ipx_socket *socket,
                               const qa_platform_ipx_address *to,
                               const uint8_t *payload, size_t length,
                               bool *out_sent, qa_platform_ipx_error *error) {
  static const uint8_t empty[1] = {0};
  uint8_t target[16];
  sockaddr_build(target, to->port, socket->packet_type, to);
  ssize_t sent = sendto(socket->descriptor, length == 0 ? empty : payload, length,
                        MSG_DONTWAIT, (const struct sockaddr *)target,
                        sizeof target);
  if (sent < 0) {
    int code = errno;
    if (code == EAGAIN || code == EINTR || code == ENOBUFS) {
      *out_sent = false;
      return true;
    }
    return failure(error, "sendto", code);
  }
  if ((uint64_t)sent != (uint64_t)length) {
    if (error != NULL) {
      error->unsupported = false;
      snprintf(error->message, sizeof error->message,
               "Linux IPX sent a partial datagram");
    }
    return false;
  }
  *out_sent = true;
  return true;
}

bool qa_native_ipx_socket_receive(qa_native_ipx_socket *socket,
                                  qa_platform_ipx_receive_event *out,
                                  qa_platform_ipx_error *error) {
  memset(out, 0, sizeof *out);
  uint8_t *receive = (uint8_t *)malloc(65535);
  if (receive == NULL) {
    if (error != NULL) {
      error->unsupported = false;
      snprintf(error->message, sizeof error->message, "out of memory");
    }
    return false;
  }
  uint8_t from[16];
  socklen_t from_length = sizeof from;
  ssize_t size = recvfrom(socket->descriptor, receive, 65535, 0,
                          (struct sockaddr *)from, &from_length);
  if (size < 0) {
    int code = errno;
    free(receive);
    if (code == EAGAIN || code == EINTR) {
      out->kind = QA_PLATFORM_IPX_RECEIVE_NONE;
      return true;
    }
    return failure(error, "recvfrom", code);
  }
  qa_platform_ipx_address sender;
  if (!address_from(from, (size_t)from_length, &sender, error)) {
    free(receive);
    return false;
  }
  out->from = sender;
  if ((int64_t)size > socket->max_datagram_bytes) {
    free(receive);
    out->kind = QA_PLATFORM_IPX_RECEIVE_DROPPED;
    return true;
  }
  out->kind = QA_PLATFORM_IPX_RECEIVE_PACKET;
  out->payload = receive;
  out->payload_length = (size_t)size;
  out->received_at = performance_now();
  return true;
}

bool qa_native_ipx_socket_readable(qa_native_ipx_socket *socket, bool *out,
                                   qa_platform_ipx_error *error) {
  struct pollfd pollfd;
  pollfd.fd = socket->descriptor;
  pollfd.events = POLLIN;
  pollfd.revents = 0;
  int result = poll(&pollfd, 1, 0);
  if (result < 0) {
    int code = errno;
    if (code == EINTR) {
      *out = false;
      return true;
    }
    return failure(error, "poll", code);
  }
  if ((pollfd.revents & POLLNVAL) != 0) {
    if (error != NULL) {
      error->unsupported = false;
      snprintf(error->message, sizeof error->message,
               "Linux IPX descriptor is invalid");
    }
    return false;
  }
  if ((pollfd.revents & (POLLERR | POLLHUP)) != 0) {
    if (error != NULL) {
      error->unsupported = false;
      snprintf(error->message, sizeof error->message,
               "Linux IPX socket reported a device error or hangup");
    }
    return false;
  }
  *out = result > 0 && (pollfd.revents & POLLIN) != 0;
  return true;
}

bool qa_native_ipx_socket_close(qa_native_ipx_socket *socket,
                                qa_platform_ipx_error *error) {
  if (close(socket->descriptor) < 0) {
    int code = errno;
    socket->opened = false;
    if (code != EINTR) return failure(error, "close", code);
  }
  socket->opened = false;
  return true;
}
