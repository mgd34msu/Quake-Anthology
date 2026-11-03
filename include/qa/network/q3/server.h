/* Port of src/network/q3/server.ts. GPL-2.0-or-later.
 * execute_message catches InvalidClientOpcodeError ("WARNING: bad command byte
 * for client") and InvalidClientCommandCountError ("cmdCount < 1" / "cmdCount >
 * MAX_PACKET_USERCMDS") with qa_try, as TS does; every other raise propagates to
 * the caller (qa_try-catchable), with owned decode buffers released. */
#ifndef QA_NETWORK_Q3_SERVER_H
#define QA_NETWORK_Q3_SERVER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/client.h"
#include "qa/network/q3/client_message.h"
#include "qa/network/q3/message.h"
#include "qa/network/q3/netchan.h"
#include "qa/network/q3/pure.h"
#include "qa/network/q3/reliable.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/snapshot_store.h"
typedef void (*qa_q3_server_thunk_fn)(void *ctx);
typedef int64_t (*qa_q3_server_int_fn)(void *ctx);
typedef double (*qa_q3_server_time_fn)(void *ctx);
typedef bool (*qa_q3_server_bool_fn)(void *ctx);
typedef const char *(*qa_q3_server_string_fn)(void *ctx);
typedef bool (*qa_q3_server_command_binding_fn)(void *ctx, const qa_q3_reliable_command *command, bool client_ok);
typedef void (*qa_q3_server_user_command_fn)(void *ctx, const qa_q3_wire_user_command *command);
typedef void (*qa_q3_server_drop_fn)(void *ctx, const char *reason);
/* TS Q3ServerBindings. */
typedef struct qa_q3_server_bindings {
  qa_q3_server_thunk_fn assert_current;
  qa_q3_server_int_fn server_id, restarted_server_id, checksum_feed;
  qa_q3_server_bool_fn pure;
  bool debug_build;
  qa_q3_server_time_fn time;
  qa_q3_server_bool_fn client_running, flood_protect;
  qa_q3_server_string_fn download_name;
  qa_q3_server_command_binding_fn command;
  qa_q3_server_user_command_fn enter_world, think;
  qa_q3_server_thunk_fn resend_gamestate;
  qa_q3_server_drop_fn drop;
  qa_q3_message_print_fn print; void *print_ctx;
  void *ctx;
} qa_q3_server_bindings;
/* TS Q3ServerRate (numbers are doubles in TS). */
typedef struct qa_q3_server_rate {
  double rate, max_rate, snapshot_msec;
  bool local, force_lan, lan;
} qa_q3_server_rate;
int64_t qa_q3_rate_milliseconds(double message_size, double rate, double max_rate);
typedef enum qa_q3_server_connection_phase {
  QA_Q3_SERVER_PHASE_CONNECTED, QA_Q3_SERVER_PHASE_PRIMED, QA_Q3_SERVER_PHASE_ACTIVE, QA_Q3_SERVER_PHASE_ZOMBIE
} qa_q3_server_connection_phase;
typedef struct qa_q3_server_connection qa_q3_server_connection;
bool qa_q3_server_connection_init(qa_q3_server_connection **out, qa_q3_connection_identity identity, int64_t challenge,
                                  int64_t qport, qa_q3_server_snapshot_history *snapshots, const qa_q3_server_bindings *bindings);
void qa_q3_server_connection_destroy(qa_q3_server_connection *s);
qa_q3_netchannel *qa_q3_server_connection_channel(const qa_q3_server_connection *s);
qa_q3_server_reliable_commands *qa_q3_server_connection_reliable(const qa_q3_server_connection *s);
qa_q3_source_message_state *qa_q3_server_connection_source_state(const qa_q3_server_connection *s);
qa_q3_connection_identity qa_q3_server_connection_identity(const qa_q3_server_connection *s);
int64_t qa_q3_server_connection_challenge(const qa_q3_server_connection *s);
qa_q3_server_snapshot_history *qa_q3_server_connection_snapshots(const qa_q3_server_connection *s);
const qa_q3_server_bindings *qa_q3_server_connection_get_bindings(const qa_q3_server_connection *s);
qa_q3_server_connection_phase qa_q3_server_connection_get_phase(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_phase(qa_q3_server_connection *s, qa_q3_server_connection_phase phase);
int64_t qa_q3_server_connection_last_client_command(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_last_client_command(qa_q3_server_connection *s, int64_t v);
const char *qa_q3_server_connection_last_client_command_string(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_last_client_command_string(qa_q3_server_connection *s, const char *v);
int64_t qa_q3_server_connection_message_acknowledge(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_message_acknowledge(qa_q3_server_connection *s, int64_t v);
int64_t qa_q3_server_connection_delta_message(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_delta_message(qa_q3_server_connection *s, int64_t v);
int64_t qa_q3_server_connection_gamestate_message_number(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_gamestate_message_number(qa_q3_server_connection *s, int64_t v);
qa_q3_wire_user_command qa_q3_server_connection_last_user_command(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_last_user_command(qa_q3_server_connection *s, const qa_q3_wire_user_command *v);
bool qa_q3_server_connection_pure_authentic(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_pure_authentic(qa_q3_server_connection *s, bool v);
bool qa_q3_server_connection_got_pure_command(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_got_pure_command(qa_q3_server_connection *s, bool v);
int64_t qa_q3_server_connection_next_snapshot_time(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_next_snapshot_time(qa_q3_server_connection *s, int64_t v);
bool qa_q3_server_connection_rate_delayed(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_rate_delayed(qa_q3_server_connection *s, bool v);
int64_t qa_q3_server_connection_reliable_sent(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_reliable_sent(qa_q3_server_connection *s, int64_t v);
int64_t qa_q3_server_connection_next_reliable_time(const qa_q3_server_connection *s);
void qa_q3_server_connection_set_next_reliable_time(qa_q3_server_connection *s, int64_t v);
bool qa_q3_server_connection_receive_datagram(qa_q3_server_connection *s, const uint8_t *packet, size_t len, qa_q3_channel_result *out);
void qa_q3_server_connection_execute_message(qa_q3_server_connection *s, qa_q3_client_message_reader *reader);
typedef void (*qa_q3_append_download_fn)(void *ctx, qa_q3_message_writer *writer);
void qa_q3_server_connection_send_gamestate(qa_q3_server_connection *s, const qa_q3_gamestate *state,
                                            const qa_q3_server_rate *rate, const qa_q3_channel_delivery *delivery);
void qa_q3_server_connection_send_snapshot(qa_q3_server_connection *s, int64_t server_flags, const qa_q3_server_rate *rate,
                                           const qa_q3_channel_delivery *delivery, qa_q3_append_download_fn append_download, void *append_ctx);
void qa_q3_server_connection_transmit_next_fragment(qa_q3_server_connection *s, const qa_q3_channel_delivery *delivery);
typedef void (*qa_q3_send_rejected_snapshot_fn)(void *ctx);
qa_q3_pure_result qa_q3_server_connection_verify_pure(qa_q3_server_connection *s, const qa_q3_pure_server *server,
                                                      const char *const *argv, size_t argc,
                                                      qa_q3_send_rejected_snapshot_fn send_rejected_snapshot, void *send_ctx);
#endif
