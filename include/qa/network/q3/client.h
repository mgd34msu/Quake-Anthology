/* Port of src/network/q3/client.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_CLIENT_H
#define QA_NETWORK_Q3_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/identity.h"
#include "qa/network/q3/demo.h"
#include "qa/network/q3/game_state.h"
#include "qa/network/q3/message.h"
#include "qa/network/q3/netchan.h"
#include "qa/network/q3/parse_entities.h"
#include "qa/network/q3/reliable.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/snapshot_history.h"
#include "qa/network/q3/state/product.h"
/* TS Q3ConnectionIdentity. */
typedef struct qa_q3_connection_identity {
  qa_client_id client; bool has_seat; qa_seat_id seat;
} qa_q3_connection_identity;
typedef void (*qa_q3_client_thunk_fn)(void *ctx);
typedef void (*qa_q3_client_system_info_fn)(void *ctx, const char *info);
typedef void (*qa_q3_client_gamestate_fn)(void *ctx, const qa_q3_gamestate *state, int64_t generation);
typedef void (*qa_q3_client_snapshot_fn)(void *ctx, const qa_q3_wire_snapshot *snapshot, int64_t ping);
typedef int64_t (*qa_q3_client_download_size_fn)(void *ctx, int64_t size);
typedef void (*qa_q3_client_download_fn)(void *ctx, const qa_q3_download_block *block);
typedef bool (*qa_q3_client_bool_fn)(void *ctx);
/* TS Q3ClientBindings (async bindings called synchronously). */
typedef struct qa_q3_client_bindings {
  qa_q3_client_thunk_fn assert_current;
  qa_q3_message_print_fn print; void *print_ctx;
  qa_q3_client_thunk_fn clear_active;
  qa_q3_client_system_info_fn system_info;
  qa_q3_client_gamestate_fn gamestate;
  qa_q3_client_snapshot_fn snapshot;
  qa_q3_client_download_size_fn download_size;
  qa_q3_client_download_fn download;
  qa_q3_client_thunk_fn map_restart, level_shot;
  qa_q3_client_bool_fn local_server_running;
  void *ctx;
} qa_q3_client_bindings;
/* TS Q3ClientPacketResult. message/plaintext owned when accepted. */
typedef struct qa_q3_client_packet_result {
  qa_q3_channel_result channel; /* non-accepted passthrough */
  qa_q3_server_message message;
  uint8_t *plaintext; size_t plaintext_len;
} qa_q3_client_packet_result;
void qa_q3_client_packet_result_destroy(qa_q3_client_packet_result *r);
/* Mirror of demo.ts DemoMessageReader interface as fn+ctx. */
typedef bool (*qa_q3_mirror_demo_next_fn)(void *ctx, qa_q3_demo_sequence_fn on_sequence, void *seq_ctx, qa_q3_demo_result *out);
typedef struct qa_q3_mirror_demo_message_reader { qa_q3_mirror_demo_next_fn next; void *ctx; } qa_q3_mirror_demo_message_reader;
typedef enum qa_q3_client_mode_kind { QA_Q3_CLIENT_MODE_NETWORK, QA_Q3_CLIENT_MODE_DEMO } qa_q3_client_mode_kind;
/* TS Q3ClientMode. */
typedef struct qa_q3_client_mode {
  qa_q3_client_mode_kind kind;
  int64_t challenge, qport; /* network */
  qa_q3_mirror_demo_message_reader reader; /* demo */
} qa_q3_client_mode;
/* TS Q3CommandHistory. Named qa_q3_wire_command_history because movement/q3
 * prediction already owns qa_q3_command_history for contract commands. */
typedef struct qa_q3_wire_command_history qa_q3_wire_command_history;
bool qa_q3_wire_command_history_init(qa_q3_wire_command_history **out);
void qa_q3_wire_command_history_destroy(qa_q3_wire_command_history *h);
int64_t qa_q3_wire_command_history_current_number(const qa_q3_wire_command_history *h);
int64_t qa_q3_wire_command_history_append(qa_q3_wire_command_history *h, const qa_q3_wire_user_command *command);
/* read: false = TS null. */
bool qa_q3_wire_command_history_read(const qa_q3_wire_command_history *h, int64_t number, qa_q3_wire_user_command *out);
void qa_q3_wire_command_history_restart(qa_q3_wire_command_history *h);
void qa_q3_wire_command_history_clear(qa_q3_wire_command_history *h);
/* TS Q3ClientSendOptions / Q3ClientSendReadiness (numbers are doubles in TS). */
typedef struct qa_q3_client_send_options { double real_time, packet_dup; bool no_delta; } qa_q3_client_send_options;
typedef struct qa_q3_client_send_readiness {
  double real_time; bool active, primed, cinematic, downloading, local, lan; double maximum_packets;
} qa_q3_client_send_readiness;
typedef struct qa_q3_client_connection qa_q3_client_connection;
bool qa_q3_client_connection_init(qa_q3_client_connection **out, qa_q3_connection_identity identity,
                                  qa_q3_product product, const qa_q3_client_mode *mode, const qa_q3_client_bindings *bindings);
void qa_q3_client_connection_destroy(qa_q3_client_connection *c);
/* Owned sub-objects (borrowed pointers). */
qa_q3_client_reliable_commands *qa_q3_client_connection_reliable(const qa_q3_client_connection *c);
qa_q3_source_parse_entities *qa_q3_client_connection_parse_entities(const qa_q3_client_connection *c);
qa_q3_snapshot_history *qa_q3_client_connection_history(const qa_q3_client_connection *c);
qa_q3_client_game_state_storage *qa_q3_client_connection_game_state(const qa_q3_client_connection *c);
qa_q3_wire_command_history *qa_q3_client_connection_commands(const qa_q3_client_connection *c);
qa_q3_source_message_state *qa_q3_client_connection_source_state(const qa_q3_client_connection *c);
qa_q3_entity_state_record *qa_q3_client_connection_baselines(const qa_q3_client_connection *c, size_t *count);
qa_q3_netchannel *qa_q3_client_connection_channel(const qa_q3_client_connection *c); /* NULL in demo mode */
qa_q3_connection_identity qa_q3_client_connection_identity(const qa_q3_client_connection *c);
qa_q3_product qa_q3_client_connection_get_product(const qa_q3_client_connection *c);
const qa_q3_client_mode *qa_q3_client_connection_get_mode(const qa_q3_client_connection *c);
const qa_q3_client_bindings *qa_q3_client_connection_get_bindings(const qa_q3_client_connection *c);
int64_t qa_q3_client_connection_server_message_sequence(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_server_message_sequence(qa_q3_client_connection *c, int64_t v);
int64_t qa_q3_client_connection_server_command_sequence(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_server_command_sequence(qa_q3_client_connection *c, int64_t v);
int64_t qa_q3_client_connection_last_executed_server_command(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_last_executed_server_command(qa_q3_client_connection *c, int64_t v);
int64_t qa_q3_client_connection_server_id(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_server_id(qa_q3_client_connection *c, int64_t v);
int64_t qa_q3_client_connection_checksum_feed(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_checksum_feed(qa_q3_client_connection *c, int64_t v);
int64_t qa_q3_client_connection_client_number(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_client_number(qa_q3_client_connection *c, int64_t v);
int64_t qa_q3_client_connection_generation(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_generation(qa_q3_client_connection *c, int64_t v);
bool qa_q3_client_connection_demo_waiting(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_demo_waiting(qa_q3_client_connection *c, bool v);
double qa_q3_client_connection_last_packet_sent_time(const qa_q3_client_connection *c);
void qa_q3_client_connection_set_last_packet_sent_time(qa_q3_client_connection *c, double v);
/* snapshotPing: false = null. */
bool qa_q3_client_connection_snapshot_ping(qa_q3_client_connection *c, int64_t number, int64_t *out);
bool qa_q3_client_connection_receive_message(qa_q3_client_connection *c, int64_t sequence, const uint8_t *bytes, size_t len, double real_time, qa_q3_server_message *out);
bool qa_q3_client_connection_copy_gamestate(qa_q3_client_connection *c, qa_q3_gamestate *out);
bool qa_q3_client_connection_receive_datagram(qa_q3_client_connection *c, const uint8_t *packet, size_t len, double real_time, qa_q3_client_packet_result *out);
/* readDemo: is_end true -> end_out valid; else message_out. */
bool qa_q3_client_connection_read_demo(qa_q3_client_connection *c, double real_time, bool *is_end, qa_q3_demo_result *end_out, qa_q3_server_message *message_out);
/* getServerCommand: false = null; argv owned (free each + array). */
bool qa_q3_client_connection_get_server_command(qa_q3_client_connection *c, int64_t sequence, char ***argv_out, size_t *argc_out);
void qa_q3_client_connection_transmit(qa_q3_client_connection *c, const qa_q3_client_send_options *options, const qa_q3_channel_delivery *delivery);
bool qa_q3_client_connection_ready_to_send(const qa_q3_client_connection *c, const qa_q3_client_send_readiness *options);
void qa_q3_client_connection_disconnect_packets(qa_q3_client_connection *c, const qa_q3_client_send_options *options, const qa_q3_channel_delivery *delivery);
#endif
