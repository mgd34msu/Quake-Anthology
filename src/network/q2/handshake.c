/* Port of src/network/q2/handshake.ts. GPL-2.0-or-later. */
#include "qa/network/q2/handshake.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/common_parse.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"

noreturn void qa_q2_throw_utf8_error(void) {
  qa_q2_throw_error("The encoded data was not valid for encoding utf-8");
}

static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s, n + 1);
  return out;
}
static char *dup_span(const char *s, size_t n) {
  char *out = (char *)malloc(n + 1);
  if (out != NULL) {
    memcpy(out, s, n);
    out[n] = '\0';
  }
  return out;
}
static bool valid_utf8(const uint8_t *bytes, size_t length) {
  size_t i = 0;
  while (i < length) {
    uint8_t c = bytes[i];
    if (c < 0x80) {
      i++;
      continue;
    }
    int need;
    uint32_t cp;
    if ((c & 0xe0) == 0xc0) { need = 1; cp = c & 0x1f; if (cp == 0) return false; }
    else if ((c & 0xf0) == 0xe0) { need = 2; cp = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0) { need = 3; cp = c & 0x07; }
    else return false;
    if (i + (size_t)need >= length + 1) return false;
    for (int k = 0; k < need; k++) {
      uint8_t d = bytes[i + 1 + (size_t)k];
      if ((d & 0xc0) != 0x80) return false;
      cp = (cp << 6) | (uint32_t)(d & 0x3f);
    }
    if (cp >= 0xd800 && cp <= 0xdfff) return false;
    if (cp > 0x10ffff) return false;
    i += (size_t)need + 1;
  }
  return true;
}

void qa_q2_connectionless_message_free(qa_q2_connectionless_message *message) {
  free(message->command);
  if (message->arguments != NULL) {
    for (size_t i = 0; i < message->argument_count; i++) free(message->arguments[i]);
    free(message->arguments);
  }
  free(message->text);
  free(message->body);
  memset(message, 0, sizeof *message);
}

uint8_t *qa_q2_out_of_band(const char *text, bool utf8, size_t *out_length) {
  (void)utf8;
  size_t n = strlen(text);
  uint8_t *packet = (uint8_t *)malloc(n + 4);
  if (packet == NULL) return NULL;
  memset(packet, 255, 4);
  memcpy(packet + 4, text, n);
  if (out_length != NULL) *out_length = n + 4;
  return packet;
}

bool qa_q2_read_out_of_band(const uint8_t *bytes, size_t length, bool utf8,
                            qa_q2_connectionless_message *out) {
  memset(out, 0, sizeof *out);
  if (length < 4) return false;
  uint32_t tag = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                 ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
  if ((int32_t)tag != -1) return false;
  const uint8_t *rest = bytes + 4;
  size_t rest_len = length - 4;
  char *text;
  if (utf8) {
    size_t zero = rest_len;
    for (size_t i = 0; i < rest_len; i++) {
      if (rest[i] == 0) { zero = i; break; }
    }
    if (!valid_utf8(rest, zero)) qa_q2_throw_utf8_error();
    text = dup_span((const char *)rest, zero);
  } else {
    size_t n = 0;
    while (n < rest_len && rest[n] != 0) n++;
    text = dup_span((const char *)rest, n);
  }
  if (text == NULL) return false;
  const char *newline = strchr(text, '\n');
  size_t line_len = newline != NULL ? (size_t)(newline - text) : strlen(text);
  char *line = dup_span(text, line_len);
  if (line == NULL) {
    free(text);
    return false;
  }
  qa_legacy_parse_state cursor = {line, line_len, 0};
  char *command = qa_parse_q2_token(&cursor, 0);
  if (command == NULL) {
    free(line);
    free(text);
    return false;
  }
  size_t capacity = 8;
  size_t count = 0;
  char **args = (char **)calloc(capacity, sizeof(char *));
  if (args == NULL) {
    free(command);
    free(line);
    free(text);
    return false;
  }
  while (cursor.index < line_len) {
    size_t before = cursor.index;
    char *value = qa_parse_q2_token(&cursor, 0);
    if (value == NULL) break;
    if (cursor.index == before) {
      free(value);
      break;
    }
    if (count == capacity) {
      capacity *= 2;
      char **grown = (char **)realloc(args, capacity * sizeof(char *));
      if (grown == NULL) {
        free(value);
        for (size_t i = 0; i < count; i++) free(args[i]);
        free(args);
        free(command);
        free(line);
        free(text);
        return false;
      }
      args = grown;
    }
    args[count++] = value;
  }
  char *body = newline != NULL ? dup_str(newline + 1) : dup_str("");
  free(line);
  if (body == NULL) {
    for (size_t i = 0; i < count; i++) free(args[i]);
    free(args);
    free(command);
    free(text);
    return false;
  }
  out->command = command;
  out->arguments = args;
  out->argument_count = count;
  out->text = text;
  out->body = body;
  return true;
}

void qa_q2_get_protocol(double version, double minor, qa_q2_protocol_identity *out) {
  memset(out, 0, sizeof *out);
  if (version == 34) {
    out->kind = "q2-classic";
    out->version = 34;
  } else if (version == 35) {
    out->kind = "q2-r1q2";
    out->version = 35;
    out->revision = minor <= 1903 ? 1903 : minor == 1904 ? 1904 : 1905;
  } else if (version == 36) {
    out->kind = "q2-q2pro";
    out->version = 36;
    out->revision = minor <= 1016   ? 1015
                     : minor == 1017 ? 1017
                     : minor == 1018 ? 1018
                     : minor == 1019 ? 1019
                     : minor == 1020 ? 1020
                     : minor == 1021 ? 1021
                     : minor == 1022 ? 1022
                     : minor == 1023 ? 1023
                     : minor == 1024 ? 1024
                     : minor == 1025 ? 1025
                                     : 1026;
  } else if (version == 1038) {
    out->kind = "q2-rerelease";
    out->version = 1038;
  } else if (version == 4038) {
    out->kind = "q2-private-classic";
    out->version = 4038;
  } else if (version == 2022) {
    out->kind = "q2-kex-demo";
    out->version = 2022;
  } else if (version == 2023) {
    out->kind = "q2-kex";
    out->version = 2023;
  } else {
    qa_q2_throw_error("Unsupported Q2 wire version %g", version);
  }
}

void qa_q2_connect_request_free(qa_q2_connect_request *request) {
  free(request->userinfo);
  if (request->social_ids != NULL) {
    for (size_t i = 0; i < request->social_id_count; i++) free(request->social_ids[i]);
    free(request->social_ids);
  }
  memset(request, 0, sizeof *request);
}

bool qa_q2_connect_request_copy_into(const qa_q2_connect_request *in,
                                     qa_q2_connect_request *out) {
  qa_q2_connect_request result;
  memset(&result, 0, sizeof result);
  result = *in;
  result.userinfo = in->userinfo != NULL ? dup_str(in->userinfo) : dup_str("");
  if (result.userinfo == NULL) return false;
  result.social_ids = NULL;
  result.social_id_count = 0;
  if (in->has_social_ids) {
    char **ids = (char **)calloc(in->social_id_count > 0 ? in->social_id_count : 1,
                                 sizeof(char *));
    if (ids == NULL) {
      free(result.userinfo);
      return false;
    }
    for (size_t i = 0; i < in->social_id_count; i++) {
      ids[i] = in->social_ids != NULL && in->social_ids[i] != NULL
                   ? dup_str(in->social_ids[i])
                   : dup_str("");
      if (ids[i] == NULL) {
        for (size_t k = 0; k < i; k++) free(ids[k]);
        free(ids);
        free(result.userinfo);
        return false;
      }
    }
    result.social_ids = ids;
    result.social_id_count = in->social_id_count;
  }
  *out = result;
  return true;
}

static bool format_has_bad(const char *value, bool allow_backslash) {
  for (const char *p = value; *p != '\0'; p++) {
    if (*p == '"' || *p == '\r' || *p == '\n' || (!allow_backslash && *p == '\\')) return true;
  }
  return false;
}

static char *format_string(const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int needed = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (needed < 0) {
    va_end(args);
    return NULL;
  }
  char *out = (char *)malloc((size_t)needed + 1);
  if (out != NULL) (void)vsnprintf(out, (size_t)needed + 1, format, args);
  va_end(args);
  return out;
}

static double number_text(double value, char *buffer, size_t capacity) {
  /* TS template interpolation of a number; integers print plainly. */
  if (value == (double)(int64_t)value && fabs(value) < 9007199254740992.0) {
    (void)snprintf(buffer, capacity, "%lld", (long long)(int64_t)value);
  } else {
    (void)snprintf(buffer, capacity, "%g", value);
  }
  return value;
}

uint8_t *qa_q2_write_connect(const qa_q2_connect_request *request,
                             size_t *out_length) {
  const char *userinfo = request->userinfo != NULL ? request->userinfo : "";
  if (format_has_bad(userinfo, true)) qa_q2_throw_error("Invalid Q2 connect userinfo");
  const char *kind = request->protocol.kind != NULL ? request->protocol.kind : "";
  char b1[32];
  char b2[32];
  char b3[32];
  char b4[32];
  char b5[32];
  if (strcmp(kind, "q2-kex") == 0) {
    const char *fallback_id = "";
    size_t count = request->has_social_ids ? request->social_id_count : 1;
    if (count < 1 || count > 8) qa_q2_throw_error("Invalid KEX social identity");
    for (size_t i = 0; i < count; i++) {
      const char *value = request->has_social_ids && request->social_ids != NULL &&
                                  request->social_ids[i] != NULL
                              ? request->social_ids[i]
                              : fallback_id;
      if (format_has_bad(value, false)) qa_q2_throw_error("Invalid KEX social identity");
    }
    char *kex_userinfo = qa_q2_kex_client_userinfo(userinfo);
    if (kex_userinfo == NULL) return NULL;
    /* Chunk by UTF-8 byte length over characters (C iterates bytes of
     * each UTF-8 character as one unit). */
    size_t total = strlen(kex_userinfo);
    size_t chunk_count = 0;
    size_t chunk_cap = 4;
    char **chunks = (char **)calloc(chunk_cap, sizeof(char *));
    if (chunks == NULL) {
      free(kex_userinfo);
      return NULL;
    }
    size_t chunk_start = 0;
    size_t i = 0;
    bool oom = false;
    while (i < total && !oom) {
      size_t char_len = 1;
      uint8_t lead = (uint8_t)kex_userinfo[i];
      if ((lead & 0xe0) == 0xc0) char_len = 2;
      else if ((lead & 0xf0) == 0xe0) char_len = 3;
      else if ((lead & 0xf8) == 0xf0) char_len = 4;
      if ((i - chunk_start) + char_len > 510) {
        if (chunk_count == chunk_cap) {
          chunk_cap *= 2;
          char **grown = (char **)realloc(chunks, chunk_cap * sizeof(char *));
          if (grown == NULL) { oom = true; break; }
          chunks = grown;
        }
        chunks[chunk_count] = dup_span(kex_userinfo + chunk_start, i - chunk_start);
        if (chunks[chunk_count] == NULL) { oom = true; break; }
        chunk_count++;
        chunk_start = i;
      }
      i += char_len;
    }
    if (!oom && chunk_start < total) {
      if (chunk_count == chunk_cap) {
        chunk_cap *= 2;
        char **grown = (char **)realloc(chunks, chunk_cap * sizeof(char *));
        if (grown == NULL) oom = true;
        else chunks = grown;
      }
      if (!oom) {
        chunks[chunk_count] = dup_span(kex_userinfo + chunk_start, total - chunk_start);
        if (chunks[chunk_count] == NULL) oom = true;
        else chunk_count++;
      }
    }
    if (!oom && chunk_count == 0) {
      chunks[0] = dup_str("");
      if (chunks[0] == NULL) oom = true;
      else chunk_count = 1;
    }
    free(kex_userinfo);
    if (oom) {
      for (size_t k = 0; k < chunk_count; k++) free(chunks[k]);
      free(chunks);
      return NULL;
    }
    size_t needed = 64;
    for (size_t k = 0; k < count; k++) {
      const char *value = request->has_social_ids && request->social_ids != NULL &&
                                  request->social_ids[k] != NULL
                              ? request->social_ids[k]
                              : fallback_id;
      needed += strlen(value) + 3;
    }
    for (size_t k = 0; k < chunk_count; k++) needed += strlen(chunks[k]) + 3;
    char *text = (char *)malloc(needed);
    if (text == NULL) {
      for (size_t k = 0; k < chunk_count; k++) free(chunks[k]);
      free(chunks);
      return NULL;
    }
    char *w = text;
    w += sprintf(w, "connect 2023 %zu", count);
    for (size_t k = 0; k < count; k++) {
      const char *value = request->has_social_ids && request->social_ids != NULL &&
                                  request->social_ids[k] != NULL
                              ? request->social_ids[k]
                              : fallback_id;
      w += sprintf(w, " \"%s\"", value);
    }
    for (size_t k = 0; k < chunk_count; k++) w += sprintf(w, " \"%s\"", chunks[k]);
    *w++ = '\n';
    *w = '\0';
    for (size_t k = 0; k < chunk_count; k++) free(chunks[k]);
    free(chunks);
    uint8_t *packet = qa_q2_out_of_band(text, true, out_length);
    free(text);
    return packet;
  }
  if (strcmp(kind, "q2-kex-demo") == 0) {
    qa_q2_throw_error(
        "KEX native transport connect dialect is not established by the available engine source");
  }
  char tail[256];
  tail[0] = '\0';
  if (strcmp(kind, "q2-r1q2") == 0) {
    (void)snprintf(tail, sizeof tail, " %s %s",
                   (number_text(request->payload_bytes, b1, sizeof b1), b1),
                   (number_text((double)request->protocol.revision, b2, sizeof b2), b2));
  } else if (strcmp(kind, "q2-q2pro") == 0) {
    (void)snprintf(tail, sizeof tail, " %s %s %s %s",
                   (number_text(request->payload_bytes, b1, sizeof b1), b1),
                   request->channel_new ? "1" : "0",
                   request->compression ? "1" : "0",
                   (number_text((double)request->protocol.revision, b2, sizeof b2), b2));
  } else if (strcmp(kind, "q2-rerelease") == 0 ||
             strcmp(kind, "q2-private-classic") == 0) {
    (void)snprintf(tail, sizeof tail, " %s %s",
                   (number_text(request->payload_bytes, b1, sizeof b1), b1),
                   request->compression ? "1" : "0");
  }
  int32_t qport = strcmp(kind, "q2-classic") == 0
                      ? (int32_t)request->qport & 65535
                      : (int32_t)request->qport & 255;
  char *text = format_string("connect %s %s %s \"%s\"%s\n",
                             (number_text((double)request->protocol.version, b3,
                                          sizeof b3), b3),
                             (number_text((double)qport, b4, sizeof b4), b4),
                             (number_text(request->challenge, b5, sizeof b5), b5),
                             userinfo, tail);
  if (text == NULL) return NULL;
  uint8_t *packet = qa_q2_out_of_band(text, false, out_length);
  free(text);
  return packet;
}

static double decimal_arg(char **args, size_t count, size_t index, bool has_fallback,
                          double fallback) {
  const char *value = index < count ? args[index] : NULL;
  if (value == NULL && has_fallback) return fallback;
  if (value == NULL) qa_q2_throw_error("Invalid Q2 connection integer");
  const char *p = value;
  if (*p == '-' || *p == '+') p++;
  if (*p == '\0') qa_q2_throw_error("Invalid Q2 connection integer");
  for (const char *q = p; *q != '\0'; q++) {
    if (!isdigit((unsigned char)*q)) qa_q2_throw_error("Invalid Q2 connection integer");
  }
  double result = strtod(value, NULL);
  if (result != trunc(result) || result > 9007199254740991.0 ||
      result < -9007199254740991.0) {
    qa_q2_throw_error("Q2 connection integer outside range");
  }
  return result;
}

bool qa_q2_read_connect(const qa_q2_connectionless_message *message,
                        qa_q2_connect_request *out) {
  memset(out, 0, sizeof *out);
  if (strcmp(message->command != NULL ? message->command : "", "connect") != 0) {
    qa_q2_throw_error("Not a Q2 connection request");
  }
  char **args = message->arguments;
  size_t count = message->argument_count;
  if (count > 0 && strcmp(args[0], "2023") == 0) {
    double n = decimal_arg(args, count, 1, false, 0);
    if (n < 1 || n > 8 || count < 3 + (size_t)n) {
      qa_q2_throw_error("Invalid KEX connection players");
    }
    size_t players = (size_t)n;
    size_t joined_len = 0;
    for (size_t i = 2 + players; i < count; i++) joined_len += strlen(args[i]);
    char *userinfo = (char *)malloc(joined_len + 1);
    if (userinfo == NULL) return false;
    char *w = userinfo;
    for (size_t i = 2 + players; i < count; i++) {
      size_t len = strlen(args[i]);
      memcpy(w, args[i], len);
      w += len;
    }
    *w = '\0';
    if (strlen(userinfo) > 8192 || format_has_bad(userinfo, true)) {
      free(userinfo);
      qa_q2_throw_error("Invalid KEX userinfo");
    }
    char **ids = (char **)calloc(players, sizeof(char *));
    if (ids == NULL) {
      free(userinfo);
      return false;
    }
    for (size_t i = 0; i < players; i++) {
      ids[i] = dup_str(args[2 + i]);
      if (ids[i] == NULL) {
        for (size_t k = 0; k < i; k++) free(ids[k]);
        free(ids);
        free(userinfo);
        return false;
      }
    }
    out->protocol.kind = "q2-kex";
    out->protocol.version = 2023;
    out->userinfo = userinfo;
    out->has_social_ids = true;
    out->social_ids = ids;
    out->social_id_count = players;
    out->payload_bytes = 65527;
    out->channel_new = false;
    out->compression = false;
    return true;
  }
  double version = decimal_arg(args, count, 0, false, 0);
  double qport = decimal_arg(args, count, 1, false, 0);
  double challenge = decimal_arg(args, count, 2, false, 0);
  const char *userinfo = count > 3 ? args[3] : NULL;
  if (userinfo == NULL || format_has_bad(userinfo, true)) {
    qa_q2_throw_error("Missing or invalid Q2 userinfo");
  }
  double payload = version == 34 ? 1390 : decimal_arg(args, count, 4, true, 1390);
  if (version != 34) {
    if (payload > 4086) payload = 4086;
    if (payload < 512) payload = 512;
  }
  double minor = version == 35   ? decimal_arg(args, count, 5, true, 1903)
                 : version == 36 ? decimal_arg(args, count, 7, true, 1015)
                                 : 0;
  qa_q2_protocol_identity protocol;
  qa_q2_get_protocol(version, minor, &protocol);
  bool channel_new = false;
  bool compression = false;
  if (version == 36) {
    channel_new = decimal_arg(args, count, 5, true, 1) == 1;
    compression = decimal_arg(args, count, 6, true, 0) != 0;
  } else if (version == 1038 || version == 4038) {
    channel_new = true;
    compression = decimal_arg(args, count, 5, true, 0) != 0;
  } else if (version == 35) {
    compression = true;
  } else if (version != 34) {
    qa_q2_throw_error("KEX native connect is unbound");
  }
  out->protocol = protocol;
  out->qport = version == 34 ? (double)((int32_t)qport & 65535)
                              : (double)((int32_t)qport & 255);
  out->challenge = challenge;
  out->userinfo = dup_str(userinfo);
  if (out->userinfo == NULL) return false;
  out->payload_bytes = payload;
  out->channel_new = channel_new;
  out->compression = compression;
  return true;
}

void qa_q2_challenge_free(qa_q2_challenge *challenge) {
  free(challenge->versions);
  memset(challenge, 0, sizeof *challenge);
}

bool qa_q2_read_challenge(const qa_q2_connectionless_message *message,
                          qa_q2_challenge *out) {
  memset(out, 0, sizeof *out);
  if (strcmp(message->command != NULL ? message->command : "", "challenge") != 0) {
    qa_q2_throw_error("Not a Q2 challenge");
  }
  out->challenge = decimal_arg(message->arguments, message->argument_count, 0, false, 0);
  const char *offer = NULL;
  for (size_t i = 0; i < message->argument_count; i++) {
    if (strncmp(message->arguments[i], "p=", 2) == 0) {
      offer = message->arguments[i] + 2;
      break;
    }
  }
  if (offer == NULL) {
    out->versions = (double *)malloc(sizeof(double));
    if (out->versions == NULL) return false;
    out->versions[0] = 34;
    out->version_count = 1;
    return true;
  }
  char *copy = dup_str(offer);
  if (copy == NULL) return false;
  size_t capacity = 4;
  size_t count = 0;
  double *versions = (double *)malloc(capacity * sizeof(double));
  if (versions == NULL) {
    free(copy);
    return false;
  }
  char *part = copy;
  for (;;) {
    char *comma = strchr(part, ',');
    if (comma != NULL) *comma = '\0';
    char *single[1] = {part};
    double v = decimal_arg(single, 1, 0, false, 0);
    if (count == capacity) {
      capacity *= 2;
      double *grown = (double *)realloc(versions, capacity * sizeof(double));
      if (grown == NULL) {
        free(versions);
        free(copy);
        return false;
      }
      versions = grown;
    }
    versions[count++] = v;
    if (comma == NULL) break;
    part = comma + 1;
  }
  free(copy);
  out->versions = versions;
  out->version_count = count;
  return true;
}

static bool ascii_case_prefix(const char *s, const char *prefix) {
  for (size_t i = 0; prefix[i] != '\0'; i++) {
    if (s[i] == '\0' || tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
      return false;
    }
  }
  return true;
}

char *qa_q2_read_download_server(char **arguments, size_t argument_count) {
  const char *advertised = NULL;
  for (size_t i = 0; i < argument_count; i++) {
    if (strncmp(arguments[i], "dlserver=", 9) == 0) {
      advertised = arguments[i] + 9;
      break;
    }
  }
  if (advertised == NULL || advertised[0] == '\0' || strlen(advertised) >= 512 ||
      (!ascii_case_prefix(advertised, "http://") &&
       !ascii_case_prefix(advertised, "https://"))) {
    return NULL;
  }
  /* Approximate the TS URL validation: reject credentials, fragment,
   * query; a full WHATWG parser is out of scope for this port. */
  const char *rest = strstr(advertised, "://") + 3;
  if (strchr(advertised, '#') != NULL || strchr(advertised, '?') != NULL) return NULL;
  const char *slash = strchr(rest, '/');
  const char *at = strchr(rest, '@');
  if (at != NULL && (slash == NULL || at < slash)) return NULL;
  if (rest[0] == '\0' || (slash == rest)) return NULL;
  for (const char *p = advertised; *p != '\0'; p++) {
    if (*p == ' ' || *p == '"' || *p == '<' || *p == '>' || *p == '\\') return NULL;
  }
  size_t n = strlen(advertised);
  bool ends_slash = slash != NULL && n > 0 && advertised[n - 1] == '/';
  char *out;
  if (slash == NULL) {
    out = format_string("%s/", advertised);
  } else if (!ends_slash) {
    out = format_string("%s/", advertised);
  } else {
    out = dup_str(advertised);
  }
  return out;
}

static const char *call_string(qa_q2_string_fn fn, void *ctx) {
  const char *value = fn != NULL ? fn(ctx) : "";
  return value != NULL ? value : "";
}

void qa_q2_client_handshake_destroy(qa_q2_client_handshake *handshake) {
  qa_net_address_destroy(&handshake->remote);
  free(handshake->preferences);
  qa_q2_connect_request_free(&handshake->request);
  free(handshake->download_server);
  free(handshake->reason);
  memset(handshake, 0, sizeof *handshake);
}

bool qa_q2_client_handshake_init(qa_q2_client_handshake *handshake,
                                 const qa_net_address *remote,
                                 const qa_q2_protocol_identity *preferences,
                                 size_t preference_count, double qport,
                                 qa_q2_string_fn userinfo, void *userinfo_ctx,
                                 bool has_payload_bytes, double payload_bytes,
                                 bool has_retry, double retry_milliseconds,
                                 qa_q2_string_fn social_id, void *social_ctx) {
  memset(handshake, 0, sizeof *handshake);
  if (preference_count == 0) {
    qa_q2_throw_error("Q2 handshake needs an explicit protocol preference");
  }
  if (qa_net_address_clone(remote, &handshake->remote) == 0) return false;
  handshake->preferences = (qa_q2_protocol_identity *)malloc(
      preference_count * sizeof(qa_q2_protocol_identity));
  if (handshake->preferences == NULL) {
    qa_net_address_destroy(&handshake->remote);
    memset(handshake, 0, sizeof *handshake);
    return false;
  }
  memcpy(handshake->preferences, preferences,
         preference_count * sizeof(qa_q2_protocol_identity));
  handshake->preference_count = preference_count;
  handshake->qport = qport;
  handshake->userinfo = userinfo;
  handshake->userinfo_ctx = userinfo_ctx;
  handshake->payload_bytes = has_payload_bytes ? payload_bytes : 1390;
  handshake->retry_milliseconds = has_retry ? retry_milliseconds : 3000;
  handshake->social_id = social_id;
  handshake->social_ctx = social_ctx;
  handshake->kind = QA_Q2_HANDSHAKE_CHALLENGING;
  qa_q2_protocol_identity protocol = preferences[0];
  if (protocol.kind != NULL && strcmp(protocol.kind, "q2-kex") == 0) {
    qa_q2_connect_request request;
    memset(&request, 0, sizeof request);
    request.protocol = protocol;
    request.userinfo = dup_str(call_string(userinfo, userinfo_ctx));
    if (request.userinfo == NULL) {
      qa_net_address_destroy(&handshake->remote);
      free(handshake->preferences);
      memset(handshake, 0, sizeof *handshake);
      return false;
    }
    request.has_social_ids = true;
    request.social_ids = (char **)calloc(1, sizeof(char *));
    if (request.social_ids == NULL) {
      free(request.userinfo);
      qa_net_address_destroy(&handshake->remote);
      free(handshake->preferences);
      memset(handshake, 0, sizeof *handshake);
      return false;
    }
    request.social_ids[0] = dup_str(call_string(social_id, social_ctx));
    request.social_id_count = 1;
    if (request.social_ids[0] == NULL) {
      free(request.social_ids);
      free(request.userinfo);
      qa_net_address_destroy(&handshake->remote);
      free(handshake->preferences);
      memset(handshake, 0, sizeof *handshake);
      return false;
    }
    request.payload_bytes = 65527;
    request.channel_new = false;
    request.compression = false;
    handshake->kind = QA_Q2_HANDSHAKE_CONNECTING;
    handshake->request = request;
  }
  return true;
}

uint8_t *qa_q2_client_handshake_poll(qa_q2_client_handshake *handshake, double now,
                                     size_t *out_length) {
  if (handshake->kind == QA_Q2_HANDSHAKE_CONNECTED ||
      handshake->kind == QA_Q2_HANDSHAKE_REJECTED) return NULL;
  if (handshake->has_last_sent && now - handshake->last_sent < handshake->retry_milliseconds) return NULL;
  if (handshake->kind == QA_Q2_HANDSHAKE_CHALLENGING) {
    handshake->kind = QA_Q2_HANDSHAKE_CHALLENGING;
    handshake->has_last_sent = true;
    handshake->last_sent = now;
    return qa_q2_out_of_band("getchallenge\n", false, out_length);
  }
  qa_q2_connect_request request;
  if (!qa_q2_connect_request_copy_into(&handshake->request, &request)) return NULL;
  free(request.userinfo);
  request.userinfo = dup_str(call_string(handshake->userinfo, handshake->userinfo_ctx));
  if (request.userinfo == NULL) {
    qa_q2_connect_request_free(&request);
    return NULL;
  }
  if (request.protocol.kind != NULL && strcmp(request.protocol.kind, "q2-kex") == 0) {
    if (request.social_ids != NULL) {
      for (size_t i = 0; i < request.social_id_count; i++) free(request.social_ids[i]);
      free(request.social_ids);
    }
    request.social_ids = (char **)calloc(1, sizeof(char *));
    if (request.social_ids == NULL) {
      qa_q2_connect_request_free(&request);
      return NULL;
    }
    request.social_ids[0] = dup_str(call_string(handshake->social_id, handshake->social_ctx));
    request.social_id_count = 1;
    request.has_social_ids = true;
    if (request.social_ids[0] == NULL) {
      qa_q2_connect_request_free(&request);
      return NULL;
    }
  }
  uint8_t *packet = qa_q2_write_connect(&request, out_length);
  qa_q2_connect_request_free(&handshake->request);
  handshake->request = request;
  handshake->kind = QA_Q2_HANDSHAKE_CONNECTING;
  handshake->has_last_sent = true;
  handshake->last_sent = now;
  return packet;
}

static void reject_with(qa_q2_client_handshake *handshake, const char *reason) {
  free(handshake->reason);
  handshake->reason = dup_str(reason);
  handshake->kind = QA_Q2_HANDSHAKE_REJECTED;
}

bool qa_q2_client_handshake_receive(qa_q2_client_handshake *handshake,
                                    const qa_net_address *from,
                                    const qa_q2_connectionless_message *message) {
  /* TS sameAddress(from, remote) defaults includePort=true. */
  if (!qa_net_same_address(from, &handshake->remote, true)) return false;
  const char *command = message->command != NULL ? message->command : "";
  if (strcmp(command, "challenge") == 0 && handshake->kind != QA_Q2_HANDSHAKE_CONNECTED) {
    qa_q2_challenge challenge;
    if (!qa_q2_read_challenge(message, &challenge)) return false;
    qa_q2_protocol_identity selected;
    bool found = false;
    memset(&selected, 0, sizeof selected);
    for (size_t i = 0; i < handshake->preference_count && !found; i++) {
      for (size_t k = 0; k < challenge.version_count; k++) {
        if (challenge.versions[k] == (double)handshake->preferences[i].version) {
          selected = handshake->preferences[i];
          found = true;
          break;
        }
      }
    }
    if (!found) {
      qa_q2_challenge_free(&challenge);
      reject_with(handshake, "No compatible advertised Quake II protocol");
      return true;
    }
    if (selected.kind != NULL && (strcmp(selected.kind, "q2-kex") == 0 ||
        strcmp(selected.kind, "q2-kex-demo") == 0)) {
      qa_q2_challenge_free(&challenge);
      reject_with(handshake, "KEX native live transport connect is unbound");
      return true;
    }
    qa_q2_codec_support support;
    qa_q2_get_codec_support(selected, &support);
    if (!support.is_supported || !support.encode) {
      qa_q2_challenge_free(&challenge);
      reject_with(handshake, !support.is_supported
                                 ? support.reason
                                 : "Selected Q2 wire has no native connect encoder");
      return true;
    }
    qa_q2_connect_request request;
    memset(&request, 0, sizeof request);
    request.protocol = selected;
    request.qport = selected.version == 34
                        ? (double)((int32_t)handshake->qport & 65535)
                        : (double)((int32_t)handshake->qport & 255);
    request.challenge = challenge.challenge;
    request.userinfo = dup_str(call_string(handshake->userinfo, handshake->userinfo_ctx));
    qa_q2_challenge_free(&challenge);
    if (request.userinfo == NULL) return false;
    request.payload_bytes = handshake->payload_bytes;
    request.channel_new = !(selected.version == 34 || selected.version == 35);
    request.compression = selected.version != 34;
    qa_q2_connect_request_free(&handshake->request);
    handshake->request = request;
    handshake->kind = QA_Q2_HANDSHAKE_CONNECTING;
    handshake->has_last_sent = false;
    return true;
  }
  if (strcmp(command, "client_connect") == 0 &&
      handshake->kind == QA_Q2_HANDSHAKE_CONNECTING &&
      (!(handshake->request.protocol.kind != NULL &&
         strcmp(handshake->request.protocol.kind, "q2-kex") == 0) ||
       (message->argument_count > 0 && strcmp(message->arguments[0], "2023") == 0))) {
    free(handshake->download_server);
    handshake->download_server =
        qa_q2_read_download_server(message->arguments, message->argument_count);
    handshake->kind = QA_Q2_HANDSHAKE_CONNECTED;
    return true;
  }
  return false;
}

bool qa_q2_challenge_table_init(qa_q2_challenge_table *table, qa_q2_random_fn random,
                                void *ctx, double capacity) {
  memset(table, 0, sizeof *table);
  table->random = random;
  table->random_ctx = ctx;
  table->capacity = capacity == 0 ? 1024 : capacity;
  table->entry_capacity = 16;
  table->entries = (qa_q2_challenge_entry *)calloc(16, sizeof(qa_q2_challenge_entry));
  if (table->entries == NULL) {
    memset(table, 0, sizeof *table);
    return false;
  }
  return true;
}

void qa_q2_challenge_table_destroy(qa_q2_challenge_table *table) {
  if (table->entries != NULL) {
    for (size_t i = 0; i < table->entry_count; i++) free(table->entries[i].key);
    free(table->entries);
  }
  memset(table, 0, sizeof *table);
}

static long challenge_find(const qa_q2_challenge_table *table, const char *key) {
  for (size_t i = 0; i < table->entry_count; i++) {
    if (strcmp(table->entries[i].key, key) == 0) return (long)i;
  }
  return -1;
}

double qa_q2_challenge_table_issue(qa_q2_challenge_table *table,
                                   const qa_net_address *from, double now) {
  char *key = qa_net_address_key(from, false);
  if (key == NULL) return 0;
  long found = challenge_find(table, key);
  if (found >= 0) {
    double value = table->entries[found].value;
    free(key);
    return value;
  }
  if ((double)table->entry_count >= table->capacity) {
    long oldest = -1;
    double time = INFINITY;
    for (size_t i = 0; i < table->entry_count; i++) {
      if (table->entries[i].time < time) {
        oldest = (long)i;
        time = table->entries[i].time;
      }
    }
    if (oldest >= 0) {
      free(table->entries[oldest].key);
      memmove(&table->entries[oldest], &table->entries[oldest + 1],
              (table->entry_count - (size_t)oldest - 1) * sizeof(qa_q2_challenge_entry));
      table->entry_count--;
    }
  }
  double value = (double)((int32_t)table->random(table->random_ctx) & 0x7fff);
  if (table->entry_count == table->entry_capacity) {
    size_t next = table->entry_capacity * 2;
    qa_q2_challenge_entry *grown =
        (qa_q2_challenge_entry *)realloc(table->entries, next * sizeof(qa_q2_challenge_entry));
    if (grown == NULL) {
      free(key);
      return value;
    }
    table->entries = grown;
    table->entry_capacity = next;
  }
  table->entries[table->entry_count].key = key;
  table->entries[table->entry_count].value = value;
  table->entries[table->entry_count].time = now;
  table->entry_count++;
  return value;
}

bool qa_q2_challenge_table_validate(const qa_q2_challenge_table *table,
                                    const qa_net_address *from, double challenge) {
  if (from->kind == QA_NET_LOOPBACK) return true;
  char *key = qa_net_address_key(from, false);
  if (key == NULL) return false;
  long found = challenge_find(table, key);
  bool ok = found >= 0 && table->entries[found].value == challenge;
  free(key);
  return ok;
}

uint8_t *qa_q2_challenge_table_reply(qa_q2_challenge_table *table,
                                     const qa_net_address *from, double now,
                                     const qa_q2_protocol_identity *protocols,
                                     size_t protocol_count, size_t *out_length) {
  double value = qa_q2_challenge_table_issue(table, from, now);
  char list[512];
  list[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < protocol_count; i++) {
    bool seen = false;
    for (size_t k = 0; k < i; k++) {
      if (protocols[k].version == protocols[i].version) seen = true;
    }
    if (seen) continue;
    int wrote = snprintf(list + used, sizeof list - used, "%s%lld", used > 0 ? "," : "",
                         (long long)protocols[i].version);
    if (wrote > 0) used += (size_t)wrote < sizeof list - used ? (size_t)wrote : sizeof list - used - 1;
  }
  char *text = format_string("challenge %lld p=%s", (long long)(int64_t)value, list);
  if (text == NULL) return NULL;
  uint8_t *packet = qa_q2_out_of_band(text, false, out_length);
  free(text);
  return packet;
}

/* Split on backslash into malloc'd parts ("" handled by callers). */
static bool split_backslash(const char *text, char ***out_parts, size_t *out_count) {
  size_t capacity = 8;
  size_t count = 0;
  char **parts = (char **)calloc(capacity, sizeof(char *));
  if (parts == NULL) return false;
  const char *start = text;
  for (;;) {
    const char *slash = strchr(start, '\\');
    size_t len = slash != NULL ? (size_t)(slash - start) : strlen(start);
    if (count == capacity) {
      capacity *= 2;
      char **grown = (char **)realloc(parts, capacity * sizeof(char *));
      if (grown == NULL) {
        for (size_t i = 0; i < count; i++) free(parts[i]);
        free(parts);
        return false;
      }
      parts = grown;
    }
    parts[count] = dup_span(start, len);
    if (parts[count] == NULL) {
      for (size_t i = 0; i < count; i++) free(parts[i]);
      free(parts);
      return false;
    }
    count++;
    if (slash == NULL) break;
    start = slash + 1;
  }
  *out_parts = parts;
  *out_count = count;
  return true;
}
static void free_parts(char **parts, size_t count) {
  for (size_t i = 0; i < count; i++) free(parts[i]);
  free(parts);
}

/* Insertion-ordered pair map (set replaces in place). */
typedef struct pair_map {
  char **keys;
  char **values;
  size_t count, capacity;
} pair_map;
static bool pair_map_set(pair_map *map, const char *key, const char *value) {
  for (size_t i = 0; i < map->count; i++) {
    if (strcmp(map->keys[i], key) == 0) {
      char *copy = dup_str(value);
      if (copy == NULL) return false;
      free(map->values[i]);
      map->values[i] = copy;
      return true;
    }
  }
  if (map->count == map->capacity) {
    size_t next = map->capacity > 0 ? map->capacity * 2 : 8;
    char **keys = (char **)realloc(map->keys, next * sizeof(char *));
    if (keys == NULL) return false;
    char **values = (char **)realloc(map->values, next * sizeof(char *));
    if (values == NULL) {
      map->keys = keys;
      return false;
    }
    map->keys = keys;
    map->values = values;
    map->capacity = next;
  }
  char *k = dup_str(key);
  char *v = dup_str(value);
  if (k == NULL || v == NULL) {
    free(k);
    free(v);
    return false;
  }
  map->keys[map->count] = k;
  map->values[map->count] = v;
  map->count++;
  return true;
}
static void pair_map_free(pair_map *map) {
  for (size_t i = 0; i < map->count; i++) {
    free(map->keys[i]);
    free(map->values[i]);
  }
  free(map->keys);
  free(map->values);
}

char *qa_q2_kex_seat_userinfo(const char *text, double seat) {
  if (text[0] == '\0') return dup_str("");
  char **parts;
  size_t count;
  if (!split_backslash(text, &parts, &count)) return NULL;
  pair_map pairs = {0};
  pair_map selected = {0};
  size_t start = text[0] == '\\' ? 1 : 0;
  bool ok = true;
  for (size_t index = start; index < count && ok; index += 2) {
    const char *key = parts[index];
    const char *value = index + 1 < count ? parts[index + 1] : NULL;
    if (value == NULL) {
      free_parts(parts, count);
      pair_map_free(&pairs);
      pair_map_free(&selected);
      qa_q2_throw_error("Invalid KEX userinfo pairs");
    }
    const char *underscore = strrchr(key, '_');
    bool suffixed = false;
    double suffix_value = 0;
    if (underscore != NULL && underscore[1] != '\0') {
      suffixed = true;
      for (const char *p = underscore + 1; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) suffixed = false;
      }
      if (suffixed) suffix_value = strtod(underscore + 1, NULL);
    }
    if (!suffixed) {
      ok = pair_map_set(&pairs, key, value);
    } else if (suffix_value == seat) {
      char *base = dup_span(key, (size_t)(underscore - key));
      if (base == NULL) { ok = false; break; }
      ok = pair_map_set(&selected, base, value);
      free(base);
    }
  }
  free_parts(parts, count);
  if (ok) {
    for (size_t i = 0; i < selected.count && ok; i++) {
      ok = pair_map_set(&pairs, selected.keys[i], selected.values[i]);
    }
  }
  pair_map_free(&selected);
  if (!ok) {
    pair_map_free(&pairs);
    return NULL;
  }
  size_t needed = 1;
  for (size_t i = 0; i < pairs.count; i++) {
    needed += strlen(pairs.keys[i]) + strlen(pairs.values[i]) + 2;
  }
  char *out = (char *)malloc(needed);
  if (out == NULL) {
    pair_map_free(&pairs);
    return NULL;
  }
  char *w = out;
  for (size_t i = 0; i < pairs.count; i++) w += sprintf(w, "\\%s\\%s", pairs.keys[i], pairs.values[i]);
  *w = '\0';
  pair_map_free(&pairs);
  return out;
}

char *qa_q2_kex_client_userinfo(const char *text) {
  if (text[0] == '\0') return dup_str("");
  char **parts;
  size_t count;
  if (!split_backslash(text, &parts, &count)) return NULL;
  size_t needed = strlen(text) + 1;
  for (size_t index = 1; index < count; index += 2) {
    const char *key = parts[index];
    const char *value = index + 1 < count ? parts[index + 1] : NULL;
    if (value == NULL) {
      free_parts(parts, count);
      qa_q2_throw_error("Invalid KEX client userinfo");
    }
    bool suffixed = false;
    const char *underscore = strrchr(key, '_');
    if (underscore != NULL && underscore[1] != '\0') {
      suffixed = true;
      for (const char *p = underscore + 1; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) suffixed = false;
      }
    }
    bool has_zero = false;
    char zero_key[512];
    (void)snprintf(zero_key, sizeof zero_key, "%s_0", key);
    for (size_t k = 1; k < count; k += 2) {
      if (strcmp(parts[k], zero_key) == 0) has_zero = true;
    }
    if (!suffixed && !has_zero) needed += strlen(key) + strlen(value) + 4;
  }
  char *out = (char *)malloc(needed);
  if (out == NULL) {
    free_parts(parts, count);
    return NULL;
  }
  (void)strcpy(out, text);
  char *w = out + strlen(text);
  for (size_t index = 1; index < count; index += 2) {
    const char *key = parts[index];
    const char *value = parts[index + 1]; /* checked above */
    bool suffixed = false;
    const char *underscore = strrchr(key, '_');
    if (underscore != NULL && underscore[1] != '\0') {
      suffixed = true;
      for (const char *p = underscore + 1; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) suffixed = false;
      }
    }
    bool has_zero = false;
    char zero_key[512];
    (void)snprintf(zero_key, sizeof zero_key, "%s_0", key);
    for (size_t k = 1; k < count; k += 2) {
      if (strcmp(parts[k], zero_key) == 0) has_zero = true;
    }
    if (!suffixed && !has_zero) w += sprintf(w, "\\%s_0\\%s", key, value);
  }
  free_parts(parts, count);
  return out;
}
