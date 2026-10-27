/* Port of src/network/q1/demos.ts. Record messages malloc'd, caller
 * frees the message bytes. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_DEMOS_H
#define QA_NETWORK_Q1_DEMOS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q1/message.h"
typedef struct qa_netquake_demo_record { qa_vec3 view_angles; uint8_t *message; size_t length; } qa_netquake_demo_record;
typedef struct qa_netquake_demo_reader {
  qa_message_reader reader; double forced_track, max_message_bytes;
  const uint8_t *owned_base;
} qa_netquake_demo_reader;
void qa_netquake_demo_reader_init(qa_netquake_demo_reader *r, const uint8_t *bytes, size_t length, double max_message_bytes);
int qa_netquake_demo_reader_next(qa_netquake_demo_reader *r, qa_netquake_demo_record *out);
uint8_t *qa_write_netquake_demo_header(double track, size_t *out_len);
uint8_t *qa_write_netquake_demo_record(const qa_netquake_demo_record *record, size_t *out_len);

typedef struct qa_quakeworld_demo_record {
  const char *kind; /* "command"|"packet"|"sequences" */
  double seconds; qa_qw_user_command command; qa_vec3 view_angles;
  uint8_t *message; size_t length; double outgoing, incoming;
} qa_quakeworld_demo_record;
void qa_quakeworld_demo_record_destroy(qa_quakeworld_demo_record *r);
typedef struct qa_quakeworld_demo_reader { qa_message_reader reader; double max_message_bytes; } qa_quakeworld_demo_reader;
void qa_quakeworld_demo_reader_init(qa_quakeworld_demo_reader *r, const uint8_t *bytes, size_t length, double max_message_bytes);
int qa_quakeworld_demo_reader_next(qa_quakeworld_demo_reader *r, qa_quakeworld_demo_record *out);
uint8_t *qa_write_quakeworld_demo_record(const qa_quakeworld_demo_record *record, size_t *out_len);
typedef struct qa_quakeworld_demo_playback {
  qa_quakeworld_demo_reader *reader; bool has_pending; qa_quakeworld_demo_record pending;
} qa_quakeworld_demo_playback;
void qa_quakeworld_demo_playback_init(qa_quakeworld_demo_playback *p, qa_quakeworld_demo_reader *reader);
void qa_quakeworld_demo_playback_destroy(qa_quakeworld_demo_playback *p);
qa_quakeworld_demo_record *qa_quakeworld_demo_playback_drain(qa_quakeworld_demo_playback *p, double seconds, bool force, size_t *out_count);
bool qa_quakeworld_demo_playback_ended(const qa_quakeworld_demo_playback *p);
#endif
