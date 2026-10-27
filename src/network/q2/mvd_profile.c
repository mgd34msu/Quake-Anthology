/* Port of src/network/q2/mvd-profile.ts. GPL-2.0-or-later. */
#include "qa/network/q2/mvd_profile.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"

void qa_q2_get_mvd_profile(double revision, double flags, qa_q2_mvd_profile *out) {
  if (!((revision >= 2009 && revision <= 2013) || revision == 3038)) {
    qa_q2_throw_error("Unsupported MVD revision %g", revision);
  }
  bool rerelease = revision == 3038;
  int32_t iflags = (int32_t)flags;
  bool extended = rerelease || (revision >= 2011 && (iflags & 4) != 0);
  bool v2 = !rerelease && revision >= 2012 && (iflags & 8) != 0;
  if (v2 && !extended) qa_q2_throw_error("MVD extended-v2 requires extended limits");
  memset(out, 0, sizeof *out);
  out->revision = revision;
  out->flags = flags;
  if (rerelease) {
    out->protocol.kind = "q2-rerelease";
    out->protocol.version = 1038;
  } else if (extended) {
    out->protocol.kind = "q2-q2pro";
    out->protocol.version = 36;
    out->protocol.revision = revision == 2013 ? 1026 : v2 ? 1025 : 1024;
  } else {
    out->protocol.kind = "q2-classic";
    out->protocol.version = 34;
  }
  out->rerelease = rerelease;
  out->extended = extended;
  out->v2 = v2;
  out->fog = v2 && revision >= 2013;
  out->max_config_strings = rerelease ? 12448 : extended ? 13630 : 2080;
  out->max_clients_index = extended ? 60 : 30;
  out->max_entities = extended ? 8192 : 1024;
}

const char *qa_q2_mvd_header_config_string(const qa_q2_mvd_header *header,
                                           double index) {
  for (size_t i = 0; i < header->config_string_count; i++) {
    if (header->config_strings[i].index == index) return header->config_strings[i].value;
  }
  return NULL;
}

void qa_q2_mvd_header_free(qa_q2_mvd_header *header) {
  if (header->config_strings != NULL) {
    for (size_t i = 0; i < header->config_string_count; i++) {
      free(header->config_strings[i].value);
    }
    free(header->config_strings);
  }
  header->config_strings = NULL;
  header->config_string_count = 0;
}

bool qa_q2_read_mvd_header(const uint8_t *bytes, size_t length,
                           qa_q2_mvd_header *out) {
  memset(out, 0, sizeof *out);
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, (int64_t)length)) return false;
  /* loadMessage over a private copy (bytes are const here). */
  uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
  if (copy == NULL) {
    qa_q2_message_destroy(&message);
    return false;
  }
  if (length > 0) memcpy(copy, bytes, length);
  qa_q2_message_load(&message, copy, length);
  int32_t command = qa_q2_msg_read_byte(&message);
  if ((command & 31) != 4 || qa_q2_msg_read_long(&message) != 37) {
    qa_q2_throw_error("MVD gamestate header expected");
  }
  double revision = (double)qa_q2_msg_read_word(&message);
  double flags = (revision >= 2012 && revision != 3038)
                     ? (double)qa_q2_msg_read_word(&message)
                     : (double)((uint32_t)command >> 5);
  qa_q2_get_mvd_profile(revision, flags, &out->profile);
  double servercount = (double)qa_q2_msg_read_long(&message);
  char gamedir[2048];
  qa_q2_msg_read_string(&message, gamedir, sizeof gamedir);
  double dummy = (double)qa_q2_msg_read_short(&message);
  size_t capacity = 16;
  size_t count = 0;
  qa_q2_mvd_config_string *entries =
      (qa_q2_mvd_config_string *)calloc(capacity, sizeof *entries);
  if (entries == NULL) {
    free(copy);
    qa_q2_message_destroy(&message);
    return false;
  }
  for (;;) {
    double index = (double)qa_q2_msg_read_word(&message);
    qa_q2_message_check_read(&message);
    if (index == out->profile.max_config_strings) break;
    if (index >= out->profile.max_config_strings) {
      qa_q2_throw_error("Invalid MVD configstring index");
    }
    char value[2048];
    qa_q2_msg_read_string(&message, value, sizeof value);
    /* Map.set replaces an existing key. */
    size_t at = count;
    for (size_t i = 0; i < count; i++) {
      if (entries[i].index == index) at = i;
    }
    size_t vlen = strlen(value);
    char *owned = (char *)malloc(vlen + 1);
    if (owned == NULL) {
      for (size_t i = 0; i < count; i++) free(entries[i].value);
      free(entries);
      free(copy);
      qa_q2_message_destroy(&message);
      return false;
    }
    memcpy(owned, value, vlen + 1);
    if (at == count) {
      if (count == capacity) {
        capacity *= 2;
        qa_q2_mvd_config_string *grown =
            (qa_q2_mvd_config_string *)realloc(entries, capacity * sizeof *entries);
        if (grown == NULL) {
          free(owned);
          for (size_t i = 0; i < count; i++) free(entries[i].value);
          free(entries);
          free(copy);
          qa_q2_message_destroy(&message);
          return false;
        }
        entries = grown;
      }
      entries[count].index = index;
      entries[count].value = owned;
      count++;
    } else {
      free(entries[at].value);
      entries[at].value = owned;
    }
  }
  out->config_strings = entries;
  out->config_string_count = count;
  const char *max_str = qa_q2_mvd_header_config_string(out, out->profile.max_clients_index);
  double max_clients = max_str != NULL ? strtod(max_str, NULL) : NAN;
  /* TS Number(undefined) is NaN; Number("") is 0. strtod("") is 0. */
  if (max_str != NULL && max_str[0] == '\0') max_clients = 0;
  if (max_clients != trunc(max_clients) || max_clients < 1 || max_clients > 256 ||
      dummy < -1 || dummy >= max_clients) {
    qa_q2_throw_error("Invalid MVD player limits");
  }
  qa_q2_message_check_read(&message);
  memset(&out->data, 0, sizeof out->data);
  out->data.servercount = servercount;
  size_t glen = strlen(gamedir);
  memcpy(out->data.gamedir, gamedir, glen + 1 < sizeof out->data.gamedir
                                        ? glen + 1
                                        : sizeof out->data.gamedir);
  out->data.clientnum = 0;
  const char *level = qa_q2_mvd_header_config_string(out, 0);
  if (level != NULL) {
    size_t llen = strlen(level);
    if (llen >= sizeof out->data.levelname) llen = sizeof out->data.levelname - 1;
    memcpy(out->data.levelname, level, llen);
    out->data.levelname[llen] = '\0';
  }
  out->data.attractloop = true;
  out->data.server_state = 2;
  out->data.has_server_fps = true;
  out->data.server_fps = 10;
  if (strcmp(out->profile.protocol.kind, "q2-q2pro") == 0) {
    out->data.has_q2pro_version = true;
    out->data.q2pro_version = (double)out->profile.protocol.revision;
    out->data.has_wire_flags = true;
    out->data.wire_flags = out->profile.v2 ? 24 : 8;
  }
  out->frame_offset = (double)message.readcount;
  out->dummy = dummy;
  out->max_clients = max_clients;
  free(copy);
  qa_q2_message_destroy(&message);
  return true;
}
