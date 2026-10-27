/*
 * Port of src/network/q2/mvd-playback.ts. The MvdVisibility interface
 * becomes fn + ctx; its entities() result is an in-place compaction
 * (callback keeps/reorders the sorted copy and returns its length).
 * Records reuse qa_q2_server_record (free with
 * qa_q2_server_records_free). GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_MVD_PLAYBACK_H
#define QA_NETWORK_Q2_MVD_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/codecs/q2repro.h"
#include "qa/network/q2/mvd_encoding.h"
#include "qa/network/q2/mvd_profile.h"
#include "qa/network/q2/server_messages.h"
#include "qa/network/q2/state.h"

/* TS MvdVisibility. area_bits returns malloc'd bytes (or NULL). */
typedef struct qa_q2_mvd_visibility {
  void *ctx;
  size_t (*entities)(void *ctx, qa_q2_wire_entity *entities, size_t count,
                     const qa_q2_wire_player *player,
                     const uint8_t *portal_bits, size_t portal_length);
  bool (*visible)(void *ctx, double leaf, bool is_pvs,
                  const qa_q2_wire_player *player,
                  const uint8_t *portal_bits, size_t portal_length);
  uint8_t *(*area_bits)(void *ctx, const qa_q2_wire_player *player,
                        const uint8_t *portal_bits, size_t portal_length,
                        size_t *out_length);
  bool (*sound_audible)(void *ctx, const float origin[3],
                        const qa_q2_wire_player *player,
                        const uint8_t *portal_bits, size_t portal_length);
  void (*sound_origin)(void *ctx, const qa_q2_wire_entity *entity,
                       float out_origin[3]);
} qa_q2_mvd_visibility;

/* TS MvdPlayback. config_strings/players are Map-like owned lists. */
typedef struct qa_q2_mvd_playback {
  qa_q2_mvd_visibility visibility;
  qa_q2_mvd_profile profile;
  qa_q2_mvd_config_string *config_strings; /* owned */
  size_t config_string_count, config_string_capacity;
  qa_q2_mvd_capture_player *players; /* owned */
  size_t player_count, player_capacity;
  qa_q2_wire_entity *entities; /* owned */
  size_t entity_count, entity_capacity;
  qa_q2_wire_codec wire;
  qa_q2repro_context rerelease;
  qa_q2_server_message_reader embedded;
  bool has_data;
  qa_q2_server_data_result data;
  double frame_number, max_clients, selected, dummy;
  uint8_t *portal_bits; /* owned */
  size_t portal_bits_length;
} qa_q2_mvd_playback;

/* False only on OOM. */
bool qa_q2_mvd_playback_init(qa_q2_mvd_playback *playback,
                             qa_q2_mvd_visibility visibility);
void qa_q2_mvd_playback_destroy(qa_q2_mvd_playback *playback);
/* TS get protocol / get header (NULL when TS null) / get selectedPlayer. */
qa_q2_protocol_identity qa_q2_mvd_playback_get_protocol(
    const qa_q2_mvd_playback *playback);
const qa_q2_server_data_result *qa_q2_mvd_playback_get_header(
    const qa_q2_mvd_playback *playback);
double qa_q2_mvd_playback_get_selected_player(
    const qa_q2_mvd_playback *playback);
/* TS selectPlayer; invalid selections abort like the TS RangeError. */
void qa_q2_mvd_playback_select_player(qa_q2_mvd_playback *playback,
                                      double number);
/* Player Map access (NULL when absent). */
const qa_q2_wire_player *qa_q2_mvd_playback_player(
    const qa_q2_mvd_playback *playback, double number);
size_t qa_q2_mvd_playback_player_count(const qa_q2_mvd_playback *playback);
const char *qa_q2_mvd_playback_config_string(
    const qa_q2_mvd_playback *playback, double index);
/* TS read(bytes) / readRecords collected. False only on OOM. */
bool qa_q2_mvd_playback_read(qa_q2_mvd_playback *playback, const uint8_t *bytes,
                             size_t length, qa_q2_server_record **out,
                             size_t *out_count);
/* TS get projection: *out_frame owned by caller's frame (free with
 * qa_q2_wire_frame_free). False only on OOM. */
bool qa_q2_mvd_playback_projection(qa_q2_mvd_playback *playback,
                                   double *out_clientnum,
                                   qa_q2_wire_frame *out_frame);

#endif
