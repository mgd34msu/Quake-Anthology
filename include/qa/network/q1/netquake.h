/* Port of src/network/q1/netquake.ts. Message unions become one flat
 * tagged struct (kind string is the exact TS kind); strings/lists in
 * decoded messages are malloc'd and freed by the destroy helpers.
 * Capture mirrors decoder-checkpoint: plain struct + SaveReader restore.
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_NETQUAKE_H
#define QA_NETWORK_Q1_NETQUAKE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q1/codecs/codec.h"
#include "qa/network/q1/decoder_checkpoint.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/profile.h"
#include "qa/network/q1/wire_types.h"
#include "qa/persistence/value.h"
typedef enum qa_temp_entity_kind { QA_TE_POINT, QA_TE_BEAM, QA_TE_EXPLOSION_COLORS } qa_temp_entity_kind;
typedef struct qa_temporary_entity {
  qa_temp_entity_kind kind; double type;
  qa_vec3 origin; double count;
  double entity; qa_vec3 start, end;
  double color_start, color_length;
} qa_temporary_entity;
/* coord is called once per component, in order. */
typedef double (*qa_coord_fn)(void *ctx);
qa_temporary_entity qa_read_temporary_entity(qa_message_reader *r, qa_coord_fn coord, void *ctx, bool quakeworld);

typedef struct qa_netquake_message {
  const char *kind; /* exact TS kind string */
  char *text;
  double seconds, version, index, value, entity, slot, stage, track, loop_track;
  qa_vec3 angles, origin, direction, source, color;
  qa_q1_protocol_identity protocol; double max_clients, game_type; char *level;
  char **models; size_t model_count; char **sounds; size_t sound_count;
  char *str_value;
  qa_q1_client_data data; double weapon_alpha;
  qa_q1_extended_entity_state state;
  double channel, volume, attenuation, armor, blood, count;
  qa_temporary_entity effect;
  bool paused;
  double density, transition_seconds, choices, impulse, color_value;
} qa_netquake_message;
void qa_netquake_message_destroy(qa_netquake_message *m);

qa_q1_extended_entity_state qa_nq_entity_state_from_wire(double number, const qa_q1_wire_entity_state *s);
void qa_nq_wire_entity_from_extended(const qa_q1_extended_entity_state *s, qa_q1_wire_entity_state *out);

typedef struct qa_nq_baseline_entry { double slot; qa_q1_wire_entity_state state; } qa_nq_baseline_entry;
typedef struct qa_netquake_decoder {
  qa_q1_protocol_identity protocol;
  const char *rerelease_messages; bool standard_quake;
  double time_seconds, flags_value;
  qa_nq_baseline_entry *baselines; size_t baseline_count, baseline_cap;
} qa_netquake_decoder;
void qa_netquake_decoder_init(qa_netquake_decoder *d, qa_q1_protocol_identity protocol,
                              const char *rerelease_messages, bool standard_quake);
void qa_netquake_decoder_destroy(qa_netquake_decoder *d);
double qa_netquake_decoder_get_flags(const qa_netquake_decoder *d);
typedef struct qa_nq_decoder_capture {
  double version, flags, time_seconds;
  struct { double slot; qa_captured_wire_entity state; } *baselines; size_t count;
} qa_nq_decoder_capture;
void qa_netquake_decoder_capture(const qa_netquake_decoder *d, qa_nq_decoder_capture *out);
void qa_nq_decoder_capture_destroy(qa_nq_decoder_capture *c);
void qa_netquake_decoder_restore(qa_netquake_decoder *d, const qa_save_reader *reader);
/* Malloc'd message array; destroy each + free. */
qa_netquake_message *qa_netquake_decoder_decode(qa_netquake_decoder *d, const uint8_t *bytes, size_t length, size_t *out_count);

void qa_write_netquake_move(qa_size_buf *sb, const qa_q1_user_command *command,
                            qa_q1_protocol_identity protocol, double flags, bool has_flags);
typedef struct qa_netquake_client_message {
  const char *kind; char *text; qa_q1_user_command command;
} qa_netquake_client_message;
void qa_netquake_client_message_destroy(qa_netquake_client_message *m);
qa_netquake_client_message *qa_decode_netquake_client(const uint8_t *bytes, size_t length,
                                                      qa_q1_protocol_identity protocol, size_t *out_count);
void qa_write_netquake_entity(qa_size_buf *sb, qa_q1_protocol_identity profile,
                              const qa_q1_extended_entity_state *state,
                              const qa_q1_extended_entity_state *baseline, double server_time_seconds);
void qa_write_netquake_client_data(qa_size_buf *sb, qa_q1_protocol_identity profile,
                                   const qa_q1_client_data *data, double weapon_alpha, bool standard_quake);
bool qa_write_netquake_sound(qa_size_buf *sb, qa_q1_protocol_identity profile, const qa_netquake_message *m);
void qa_write_netquake_server_info(qa_size_buf *sb, const qa_netquake_message *m);
void qa_write_netquake_time(qa_size_buf *sb, double seconds);
void qa_write_netquake_stat(qa_size_buf *sb, double index, double value);
void qa_write_netquake_message(qa_size_buf *sb, qa_q1_protocol_identity profile, const qa_netquake_message *m,
                               const char *rerelease_messages, bool standard_quake);
typedef struct qa_netquake_move_sender { double messages; } qa_netquake_move_sender;
void qa_netquake_move_sender_init(qa_netquake_move_sender *s);
void qa_netquake_move_sender_reset(qa_netquake_move_sender *s);
uint8_t *qa_netquake_move_sender_next(qa_netquake_move_sender *s, const qa_q1_user_command *command,
                                      qa_q1_protocol_identity protocol, bool demo_playback, size_t *out_len);
#endif
