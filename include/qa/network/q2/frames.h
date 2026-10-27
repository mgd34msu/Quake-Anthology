/*
 * Port of src/network/q2/frames.ts. Frames are caller-allocated
 * structs with malloc'd areaBits/entities/split lists (free helper);
 * baselines and history frames are insertion-ordered lists like
 * TS Maps. read() stores into history and returns a pointer owned
 * by history (valid until accept/clear/destroy). GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_FRAMES_H
#define QA_NETWORK_Q2_FRAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/network/q2/codec.h"
#include "qa/network/q2/codecs/codec.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/state.h"

/* One splitPlayers entry. */
typedef struct qa_q2_split_player {
  uint8_t *area_bits; /* owned */
  size_t area_bits_length;
  qa_q2_wire_player player;
} qa_q2_split_player;

/* TS Q2WireFrame. */
typedef struct qa_q2_wire_frame {
  bool has_valid;
  bool valid;
  double server_frame;
  double delta_frame;
  double suppressed_count;
  uint8_t *area_bits; /* owned */
  size_t area_bits_length;
  qa_q2_wire_player player;
  bool has_split;
  qa_q2_split_player *split_players; /* owned */
  size_t split_count;
  qa_q2_wire_entity *entities; /* owned */
  size_t entity_count;
} qa_q2_wire_frame;

void qa_q2_wire_frame_free(qa_q2_wire_frame *frame);
/* Deep copy (history ownership). False only on OOM. */
bool qa_q2_wire_frame_clone(const qa_q2_wire_frame *in, qa_q2_wire_frame *out);

/* One baselines Map entry. */
typedef struct qa_q2_baseline_entry {
  double number;
  qa_q2_wire_entity state;
} qa_q2_baseline_entry;
typedef struct qa_q2_baseline_list {
  qa_q2_baseline_entry *entries;
  size_t count, capacity;
} qa_q2_baseline_list;

void qa_q2_baseline_list_free(qa_q2_baseline_list *list);
bool qa_q2_baseline_list_set(qa_q2_baseline_list *list, double number,
                             const qa_q2_wire_entity *state);
const qa_q2_wire_entity *qa_q2_baseline_list_get(const qa_q2_baseline_list *list,
                                                 double number);

/* TS writePacketEntities / encodeQ2Frame (malloc'd bytes or NULL). */
void qa_q2_write_packet_entities(qa_q2_protocol_codec *codec, qa_q2_sizebuf *message,
                                 const qa_q2_wire_entity *old_entities,
                                 size_t old_count,
                                 const qa_q2_wire_entity *current, size_t current_count,
                                 const qa_q2_baseline_list *baselines,
                                 double max_clients);
uint8_t *qa_q2_encode_frame(qa_q2_wire_codec *wire, const qa_q2_wire_frame *frame,
                            const qa_q2_wire_frame *old_or_null,
                            const qa_q2_baseline_list *baselines, double max_clients,
                            size_t *out_length);

/* TS Q2FrameHistory. frames are owned clones in insertion order. */
typedef struct qa_q2_frame_history {
  double capacity;
  qa_q2_wire_frame *frames;
  size_t frame_count, frame_capacity;
  qa_q2_baseline_list baselines;
} qa_q2_frame_history;

/* capacity 0 is TS default 16; invalid capacities abort. */
bool qa_q2_frame_history_init(qa_q2_frame_history *history, double capacity);
void qa_q2_frame_history_destroy(qa_q2_frame_history *history);
qa_q2_wire_frame *qa_q2_frame_history_get(qa_q2_frame_history *history, double number);
qa_q2_wire_frame *qa_q2_frame_history_latest(qa_q2_frame_history *history);
void qa_q2_frame_history_clear(qa_q2_frame_history *history);
/* TS accept(frame): stores a deep clone. False only on OOM. */
bool qa_q2_frame_history_accept(qa_q2_frame_history *history,
                                const qa_q2_wire_frame *frame);
/* TS read(wire, readSuppressByte=true): NULL only on OOM. */
qa_q2_wire_frame *qa_q2_frame_history_read(qa_q2_frame_history *history,
                                           qa_q2_wire_codec *wire,
                                           bool read_suppress_byte);

#endif
