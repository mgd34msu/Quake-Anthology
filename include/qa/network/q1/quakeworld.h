/* Port of src/network/q1/quakeworld.ts. Flat tagged message struct;
 * decoded strings/bytes/entities are malloc'd (destroy frees).
 * GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_QUAKEWORLD_H
#define QA_NETWORK_Q1_QUAKEWORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q1/decoder_checkpoint.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/profile.h"
#include "qa/network/q1/qw_constants.h"
#include "qa/persistence/value.h"
typedef struct qa_qw_move_variables {
  double gravity, stop_speed, max_speed, spectator_max_speed, accelerate;
  double air_accelerate, water_accelerate, friction, water_friction, entity_gravity;
} qa_qw_move_variables;
typedef struct qa_quakeworld_entity {
  qa_q1_extended_entity_state state; double quakeworld_flags;
} qa_quakeworld_entity;
typedef struct qa_qw_nail { qa_vec3 origin; double pitch, yaw; } qa_qw_nail;
typedef struct qa_quakeworld_message {
  const char *kind;
  double index, value, level, entity, slot, user_id, track, degrees, count, first, next;
  char *text, *str_value, *key, *game_directory, *level_name;
  qa_vec3 angles, origin, source;
  qa_quakeworld_profile protocol; double server_count, player_slot; bool spectator;
  qa_qw_move_variables move_variables;
  qa_q1_extended_entity_state state;
  double channel, volume, attenuation, armor, blood;
  qa_temporary_entity effect;
  bool paused;
  bool download_missing; double download_percent; uint8_t *download_bytes; size_t download_length;
  qa_qw_player_state player;
  qa_qw_nail *projectiles; size_t projectile_count;
  char **names; size_t name_count;
  double sequence; bool has_delta_sequence; double delta_sequence, requested;
  qa_quakeworld_entity *entities; size_t entity_count;
} qa_quakeworld_message;
void qa_quakeworld_message_destroy(qa_quakeworld_message *m);

qa_qw_user_command qa_qw_command_from_wire(const qa_qw_usercmd *command);
void qa_qw_wire_command_from_contract(const qa_qw_user_command *command, qa_qw_usercmd *out);
qa_quakeworld_entity qa_qw_entity_from_wire(const qa_qw_entity_state *s);
void qa_qw_wire_entity_from_extended(const qa_q1_extended_entity_state *s, bool solid, qa_qw_entity_state *out);

typedef struct qa_qw_baseline_entry { double slot; qa_qw_entity_state state; } qa_qw_baseline_entry;
typedef struct qa_qw_frame_entry { double sequence; qa_qw_entity_state *states; size_t count; } qa_qw_frame_entry;
typedef struct qa_qw_delta_entry { double sequence; bool has_base; double base; } qa_qw_delta_entry;
typedef struct qa_quakeworld_decoder {
  qa_quakeworld_profile protocol; double player_model_index;
  qa_qw_baseline_entry *baselines; size_t baseline_count, baseline_cap;
  qa_qw_frame_entry *frames; size_t frame_count, frame_cap;
  qa_qw_delta_entry *delta_requests; size_t delta_count, delta_cap;
} qa_quakeworld_decoder;
void qa_quakeworld_decoder_init(qa_quakeworld_decoder *d, qa_quakeworld_profile protocol);
void qa_quakeworld_decoder_destroy(qa_quakeworld_decoder *d);
typedef struct qa_qw_decoder_capture {
  double version, flags, player_model_index;
  struct { double slot; qa_captured_qw_wire_entity state; } *baselines; size_t baseline_count;
  struct { double sequence; qa_captured_qw_wire_entity *states; size_t count; } *frames; size_t frame_count;
  struct { double sequence; bool has_base; double base; } *delta_requests; size_t delta_count;
} qa_qw_decoder_capture;
void qa_quakeworld_decoder_capture(const qa_quakeworld_decoder *d, qa_qw_decoder_capture *out);
void qa_qw_decoder_capture_destroy(qa_qw_decoder_capture *c);
void qa_quakeworld_decoder_restore(qa_quakeworld_decoder *d, const qa_save_reader *reader);
void qa_quakeworld_decoder_record_delta_request(qa_quakeworld_decoder *d, double command_sequence, bool has_base, double base_sequence);
qa_quakeworld_message *qa_quakeworld_decoder_decode(qa_quakeworld_decoder *d, const uint8_t *bytes, size_t length,
                                                    double sequence, size_t *out_count);

typedef struct qa_qw_baseline_map_entry { double number; const qa_qw_entity_state *state; } qa_qw_baseline_map_entry;
typedef struct qa_qw_previous_frame { double sequence; const qa_qw_entity_state *states; size_t count; } qa_qw_previous_frame;
void qa_write_quakeworld_entities(qa_size_buf *sb, qa_quakeworld_profile profile,
                                  const qa_qw_entity_state *states, size_t count,
                                  const qa_qw_baseline_map_entry *baseline, size_t baseline_count,
                                  const qa_qw_previous_frame *previous);
void qa_write_quakeworld_player(qa_size_buf *sb, qa_quakeworld_profile profile, const qa_qw_player_state *state);
void qa_write_quakeworld_server_data(qa_size_buf *sb, const qa_quakeworld_message *m);
void qa_write_quakeworld_download(qa_size_buf *sb, bool missing, double percent, const uint8_t *bytes, size_t length);
void qa_write_quakeworld_message(qa_size_buf *sb, qa_quakeworld_profile profile, const qa_quakeworld_message *m);
#endif
