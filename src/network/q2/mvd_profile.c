/* Port of src/network/q2/mvd-profile.ts. GPL-2.0-or-later. */
#include "qa/network/q2/mvd_profile.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* TS Number(text): trimmed, empty is 0, full parse or NaN. */
static double js_number(const char *text) { return qa_js_string_to_number(text); }

void qa_q2_get_mvd_profile(double revision, double flags, qa_q2_mvd_profile *out) {
  if (!((revision >= 2009 && revision <= 2013) || revision == 3038)) {
    qa_q2_throw_error("Unsupported MVD revision %s", QA_JS_NUMBER(revision));
  }
  bool rerelease = revision == 3038;
  int32_t iflags = qa_js_to_int32(flags); /* flags & 4, flags & 8 */
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

/* Owned configstring list held across reads that can raise. */
typedef struct header_entries {
  qa_q2_mvd_config_string *items;
  size_t count;
} header_entries;
static void header_entries_release(void *arg) {
  header_entries *held = (header_entries *)arg;
  for (size_t i = 0; i < held->count; i++) free(held->items[i].value);
  free(held->items);
  held->items = NULL;
  held->count = 0;
}

bool qa_q2_read_mvd_header(const uint8_t *bytes, size_t length,
                           qa_q2_mvd_header *out) {
  memset(out, 0, sizeof *out);
  /* createMessage + loadMessage: reads never write, so the borrowed bytes are loaded directly
   * and nothing on the heap is held across the raising reads below. */
  qa_q2_sizebuf message;
  qa_q2_sizebuf_init_empty(&message);
  qa_q2_message_load(&message, (uint8_t *)(uintptr_t)bytes, length);
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
  size_t capacity = 0;
  header_entries held = {NULL, 0};
  qa_cleanup node;
  qa_cleanup_push(&node, header_entries_release, &held);
  for (;;) {
    double index = (double)qa_q2_msg_read_word(&message);
    qa_q2_message_check_read(&message);
    if (index == out->profile.max_config_strings) break;
    if (index >= out->profile.max_config_strings) {
      qa_q2_throw_error("Invalid MVD configstring index");
    }
    char value[2048];
    qa_q2_msg_read_string(&message, value, sizeof value);
    size_t vlen = strlen(value);
    char *owned = (char *)malloc(vlen + 1);
    if (owned != NULL) memcpy(owned, value, vlen + 1);
    if (owned == NULL) {
      qa_cleanup_pop(&node);
      header_entries_release(&held);
      return false;
    }
    /* Map.set replaces an existing key in place. */
    size_t at = held.count;
    for (size_t i = 0; i < held.count; i++) {
      if (held.items[i].index == index) at = i;
    }
    if (at < held.count) {
      free(held.items[at].value);
      held.items[at].value = owned;
      continue;
    }
    if (held.count == capacity) {
      size_t next = capacity > 0 ? capacity * 2 : 16;
      qa_q2_mvd_config_string *grown =
          (qa_q2_mvd_config_string *)realloc(held.items, next * sizeof *grown);
      if (grown == NULL) {
        free(owned);
        qa_cleanup_pop(&node);
        header_entries_release(&held);
        return false;
      }
      held.items = grown;
      capacity = next;
    }
    held.items[held.count].index = index;
    held.items[held.count].value = owned;
    held.count++;
  }
  out->config_strings = held.items;
  out->config_string_count = held.count;
  const char *max_str = qa_q2_mvd_header_config_string(out, out->profile.max_clients_index);
  double max_clients = max_str != NULL ? js_number(max_str) : NAN;
  /* Number.isInteger rejects NaN and the infinities before the range test. */
  if (!isfinite(max_clients) || max_clients != trunc(max_clients) || max_clients < 1 ||
      max_clients > 256 || dummy < -1 || dummy >= max_clients) {
    qa_q2_throw_error("Invalid MVD player limits");
  }
  qa_q2_message_check_read(&message);
  qa_cleanup_pop(&node);
  memset(&out->data, 0, sizeof out->data);
  out->data.servercount = servercount;
  memcpy(out->data.gamedir, gamedir, sizeof out->data.gamedir < sizeof gamedir
                                         ? sizeof out->data.gamedir
                                         : sizeof gamedir);
  out->data.gamedir[sizeof out->data.gamedir - 1] = '\0';
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
  return true;
}
