/*
 * Port of src/network/q2/mvd-profile.ts. C accommodation: the TS
 * configStrings Map becomes a malloc'd entry list (free helper).
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_MVD_PROFILE_H
#define QA_NETWORK_Q2_MVD_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/protocol.h"
#include "qa/network/q2/codecs/codec.h"

/* TS MvdProfile. */
typedef struct qa_q2_mvd_profile {
  double revision;
  double flags;
  qa_q2_protocol_identity protocol;
  bool rerelease, extended, v2, fog;
  double max_config_strings, max_clients_index, max_entities;
} qa_q2_mvd_profile;

/* TS mvdProfile(revision, flags); unsupported values raise the TS Error. */
void qa_q2_get_mvd_profile(double revision, double flags, qa_q2_mvd_profile *out);

/* One configStrings Map entry. */
typedef struct qa_q2_mvd_config_string {
  double index;
  char *value; /* owned */
} qa_q2_mvd_config_string;

/* TS MvdHeader. */
typedef struct qa_q2_mvd_header {
  qa_q2_mvd_profile profile;
  qa_q2_server_data_result data;
  qa_q2_mvd_config_string *config_strings; /* owned */
  size_t config_string_count;
  double frame_offset;
  double dummy;
  double max_clients;
} qa_q2_mvd_header;

/* TS readMvdHeader(bytes); malformed headers raise the TS Error (nothing is
 * held on the heap after a caught raise). False only on OOM. */
bool qa_q2_read_mvd_header(const uint8_t *bytes, size_t length,
                           qa_q2_mvd_header *out);
void qa_q2_mvd_header_free(qa_q2_mvd_header *header);
/* Map lookup; NULL when absent. */
const char *qa_q2_mvd_header_config_string(const qa_q2_mvd_header *header,
                                           double index);

#endif
