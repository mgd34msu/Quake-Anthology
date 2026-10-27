#define _POSIX_C_SOURCE 200809L
/* See include/qa/llm/auth_worker.h. GPL-2.0-or-later. */
#include "qa/llm/auth_worker.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct qa_llm_auth_worker {
  int listen_fd;
  int port;
  char *expected_state;
  qa_llm_auth_worker_post_fn post;
  void *post_ctx;
  bool stopped;
};

const char *qa_llm_auth_worker_message_kind_name(qa_llm_auth_worker_message_kind kind) {
  switch (kind) {
    case QA_LLM_AUTH_WORKER_READY: return "ready";
    case QA_LLM_AUTH_WORKER_CODE: return "code";
    case QA_LLM_AUTH_WORKER_STATE_MISMATCH: return "state-mismatch";
    case QA_LLM_AUTH_WORKER_DENIED: return "denied";
    case QA_LLM_AUTH_WORKER_LISTEN_ERROR: return "listen-error";
    case QA_LLM_AUTH_WORKER_STOPPED: return "stopped";
  }
  return "";
}

static void post_message(qa_llm_auth_worker *worker, qa_llm_auth_worker_message_kind kind, const char *code) {
  qa_llm_auth_worker_message message;
  message.kind = kind;
  message.port = worker->port;
  message.code = code;
  if (worker->post != NULL) worker->post(worker->post_ctx, &message);
}

void qa_llm_auth_worker_validate(int requested_port, const char *expected_state) {
  if (requested_port < 0 || requested_port > 65535 || expected_state == NULL || expected_state[0] == '\0') {
    qa_llm_plain_error("Invalid subscription callback worker data.");
  }
}

void qa_llm_auth_worker_parse_data(const qa_llm_json *worker_data, int *out_port, char **out_state) {
  /* fields(): only a plain object is admitted. */
  if (!qa_llm_json_is_object(worker_data)) qa_llm_plain_error("Invalid subscription callback worker data.");
  const qa_llm_json *port = qa_llm_json_get(worker_data, "port");
  const qa_llm_json *state = qa_llm_json_get(worker_data, "state");
  if (port == NULL || port->kind != QA_LLM_JSON_NUMBER || port->number != (double)(int)port->number ||
      port->number < 0 || port->number > 65535 ||
      state == NULL || state->kind != QA_LLM_JSON_STRING || state->string[0] == '\0') {
    qa_llm_plain_error("Invalid subscription callback worker data.");
  }
  *out_port = (int)port->number;
  *out_state = qa_llm_strdup(state->string);
}

qa_llm_auth_worker *qa_llm_auth_worker_start(int requested_port, const char *expected_state,
                                             qa_llm_auth_worker_post_fn post, void *post_ctx) {
  qa_llm_auth_worker_validate(requested_port, expected_state);
  qa_llm_auth_worker *worker = calloc(1, sizeof *worker);
  if (worker == NULL) qa_llm_oom();
  worker->listen_fd = -1;
  worker->expected_state = qa_llm_strdup(expected_state);
  worker->post = post;
  worker->post_ctx = post_ctx;

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  bool opened = false;
  if (fd >= 0) {
    struct sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1 */
    address.sin_port = htons((uint16_t)requested_port);
    if (bind(fd, (struct sockaddr *)&address, sizeof address) == 0 && listen(fd, 8) == 0) {
      struct sockaddr_in bound;
      socklen_t length = sizeof bound;
      if (getsockname(fd, (struct sockaddr *)&bound, &length) == 0) {
        worker->listen_fd = fd;
        worker->port = ntohs(bound.sin_port);
        opened = true;
      }
    }
    if (!opened) close(fd);
  }
  if (!opened) {
    post_message(worker, QA_LLM_AUTH_WORKER_LISTEN_ERROR, NULL);
    free(worker->expected_state);
    free(worker);
    return NULL;
  }
  post_message(worker, QA_LLM_AUTH_WORKER_READY, NULL);
  return worker;
}

int qa_llm_auth_worker_port(const qa_llm_auth_worker *worker) { return worker->port; }

/* URLSearchParams decoding for one query component. */
static char *query_decode(const char *text, size_t length) {
  qa_llm_strbuf out;
  qa_llm_strbuf_init(&out);
  for (size_t i = 0; i < length; i++) {
    char c = text[i];
    if (c == '+') { qa_llm_strbuf_append_char(&out, ' '); continue; }
    if (c == '%' && i + 2 < length) {
      int hi = -1, lo = -1;
      char a = text[i + 1], b = text[i + 2];
      if (a >= '0' && a <= '9') hi = a - '0'; else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10; else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
      if (b >= '0' && b <= '9') lo = b - '0'; else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10; else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
      if (hi >= 0 && lo >= 0) {
        qa_llm_strbuf_append_char(&out, (char)(hi * 16 + lo));
        i += 2;
        continue;
      }
    }
    qa_llm_strbuf_append_char(&out, c);
  }
  return qa_llm_strbuf_steal(&out);
}

static char *query_get(const char *query, const char *name, bool *present) {
  *present = false;
  if (query == NULL) return NULL;
  const char *p = query;
  while (*p != '\0') {
    const char *amp = strchr(p, '&');
    size_t span = amp != NULL ? (size_t)(amp - p) : strlen(p);
    const char *eq = memchr(p, '=', span);
    size_t key_len = eq != NULL ? (size_t)(eq - p) : span;
    char *key = query_decode(p, key_len);
    bool match = strcmp(key, name) == 0;
    free(key);
    if (match) {
      *present = true;
      if (eq == NULL) return qa_llm_strdup("");
      return query_decode(eq + 1, span - (size_t)(eq - p) - 1);
    }
    if (amp == NULL) break;
    p = amp + 1;
  }
  return NULL;
}

void qa_llm_auth_worker_handle_request(qa_llm_auth_worker *worker, const char *method, const char *target,
                                       int *out_status, char **out_body) {
  const char *query_at = strchr(target, '?');
  size_t path_len = query_at != NULL ? (size_t)(query_at - target) : strlen(target);
  const char *query = query_at != NULL ? query_at + 1 : NULL;
  if (!(path_len == strlen("/auth/callback") && memcmp(target, "/auth/callback", path_len) == 0)) {
    *out_status = 404;
    *out_body = qa_llm_strdup("Not found");
    return;
  }
  if (strcmp(method, "GET") != 0) {
    *out_status = 405;
    *out_body = qa_llm_strdup("Method not allowed");
    return;
  }
  bool present = false;
  char *state = query_get(query, "state", &present);
  bool matches = state != NULL && strcmp(state, worker->expected_state) == 0;
  free(state);
  if (!matches) {
    post_message(worker, QA_LLM_AUTH_WORKER_STATE_MISMATCH, NULL);
    *out_status = 400;
    *out_body = qa_llm_strdup("Sign-in failed. State mismatch.");
    return;
  }
  char *code = query_get(query, "code", &present);
  bool has_error = false;
  char *error_value = query_get(query, "error", &has_error);
  free(error_value);
  if (has_error || code == NULL || code[0] == '\0') {
    free(code);
    post_message(worker, QA_LLM_AUTH_WORKER_DENIED, NULL);
    *out_status = 400;
    *out_body = qa_llm_strdup("Sign-in was not authorized.");
    return;
  }
  post_message(worker, QA_LLM_AUTH_WORKER_CODE, code);
  free(code);
  *out_status = 200;
  *out_body = qa_llm_strdup("Authorization received. You can return to the game.");
}

bool qa_llm_auth_worker_serve_once(qa_llm_auth_worker *worker, int timeout_ms) {
  if (worker->stopped || worker->listen_fd < 0) return false;
  struct pollfd poller = { worker->listen_fd, POLLIN, 0 };
  int ready = poll(&poller, 1, timeout_ms);
  if (ready <= 0 || (poller.revents & POLLIN) == 0) return true;
  int connection = accept(worker->listen_fd, NULL, NULL);
  if (connection < 0) return true;
  /* One request line is all the handler needs; read until its end. */
  char buffer[8192];
  size_t length = 0;
  struct pollfd reader = { connection, POLLIN, 0 };
  while (length + 1 < sizeof buffer) {
    if (poll(&reader, 1, 1000) <= 0) break;
    ssize_t got = read(connection, buffer + length, sizeof buffer - 1 - length);
    if (got <= 0) break;
    length += (size_t)got;
    buffer[length] = '\0';
    if (strstr(buffer, "\r\n") != NULL) break;
  }
  buffer[length] = '\0';
  char method[16] = "";
  char target[4096] = "";
  if (sscanf(buffer, "%15s %4095s", method, target) == 2) {
    int status = 400;
    char *body = NULL;
    qa_llm_auth_worker_handle_request(worker, method, target, &status, &body);
    const char *reason = status == 200 ? "OK" : status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Bad Request";
    qa_llm_strbuf response;
    qa_llm_strbuf_init(&response);
    qa_llm_strbuf_appendf(&response, "HTTP/1.1 %d %s\r\nContent-Type: text/plain;charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
                          status, reason, strlen(body != NULL ? body : ""), body != NULL ? body : "");
    const char *data = qa_llm_strbuf_text(&response);
    size_t remaining = response.length;
    while (remaining > 0) {
      ssize_t sent = write(connection, data, remaining);
      if (sent <= 0) break;
      data += sent;
      remaining -= (size_t)sent;
    }
    qa_llm_strbuf_free(&response);
    free(body);
  }
  close(connection);
  return true;
}

void qa_llm_auth_worker_handle_message(qa_llm_auth_worker *worker, const qa_llm_json *message) {
  if (!qa_llm_json_is_object(message)) return;
  const qa_llm_json *kind = qa_llm_json_get(message, "kind");
  if (kind == NULL || kind->kind != QA_LLM_JSON_STRING || strcmp(kind->string, "stop") != 0) return;
  if (!worker->stopped) {
    worker->stopped = true;
    if (worker->listen_fd >= 0) {
      close(worker->listen_fd);
      worker->listen_fd = -1;
    }
  }
  post_message(worker, QA_LLM_AUTH_WORKER_STOPPED, NULL);
}

void qa_llm_auth_worker_destroy(qa_llm_auth_worker *worker, bool post_stopped) {
  if (worker == NULL) return;
  if (!worker->stopped) {
    worker->stopped = true;
    if (worker->listen_fd >= 0) close(worker->listen_fd);
    if (post_stopped) post_message(worker, QA_LLM_AUTH_WORKER_STOPPED, NULL);
  }
  free(worker->expected_state);
  free(worker);
}
