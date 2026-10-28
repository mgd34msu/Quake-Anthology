/* Port of src/app/bootstrap/network/q2-client-receiver.ts.
 * DemoRecordingSeed (sibling demo-recording) rides as the packet
 * list below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_CLIENT_RECEIVER_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_CLIENT_RECEIVER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/q2_downloads.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/network/q2/server_messages.h"
/* TS Q2ApplicationClientDownloads as used by the receiver. */
typedef struct qa_app_q2_receiver_downloads {
  void *ctx;
  qa_app_q2_download_preparation (*prepare)(void *ctx, const qa_app_q2_game_state *state);
  qa_app_q2_download_receive (*receive)(void *ctx, const qa_q2_server_event *block);
  void (*close)(void *ctx);
} qa_app_q2_receiver_downloads;
typedef struct qa_app_q2_receiver_source {
  bool is_demo;
  bool has_downloads; qa_app_q2_receiver_downloads downloads;
  void *ctx;
  void (*command)(void *ctx, const char *text);
  void (*reset_commands)(void *ctx);
  bool (*closed)(void *ctx);
} qa_app_q2_receiver_source;
typedef struct qa_app_q2_recording_packet { uint8_t *message; size_t length; } qa_app_q2_recording_packet;
typedef struct qa_app_q2_recording_seed {
  qa_q2_protocol_identity protocol;
  qa_app_q2_recording_packet *packets; size_t packet_count;
} qa_app_q2_recording_seed;
void qa_app_q2_recording_seed_free(qa_app_q2_recording_seed *seed);
/* qa_app_q2_game_state payloads built by this receiver:
 * data is a qa_q2_server_data_result*, config_strings a snapshot
 * below, baselines a qa_q2_baseline_list*. */
typedef struct qa_app_q2_config_snapshot {
  qa_q2_config_string_entry *entries; size_t count;
} qa_app_q2_config_snapshot;
typedef struct qa_app_q2_client_receiver qa_app_q2_client_receiver;
qa_app_q2_client_receiver *qa_app_q2_client_receiver_create(
    qa_app_q2_client_host host, qa_app_q2_receiver_source source);
void qa_app_q2_client_receiver_destroy(qa_app_q2_client_receiver *receiver);
qa_q2_server_message_reader *qa_app_q2_client_receiver_reader(qa_app_q2_client_receiver *receiver);
const char *qa_app_q2_client_receiver_phase(const qa_app_q2_client_receiver *receiver);
double qa_app_q2_client_receiver_acknowledged_frame(const qa_app_q2_client_receiver *receiver);
double qa_app_q2_client_receiver_world_generation(const qa_app_q2_client_receiver *receiver);
bool qa_app_q2_client_receiver_recorded_time(const qa_app_q2_client_receiver *receiver, double *out);
bool qa_app_q2_client_receiver_disconnected_demo(const qa_app_q2_client_receiver *receiver);
void qa_app_q2_client_receiver_seed(qa_app_q2_client_receiver *receiver, qa_app_q2_recording_seed *out);
void qa_app_q2_client_receiver_request_full_frame(qa_app_q2_client_receiver *receiver);
/* Returned records are freed with qa_q2_server_records_free. */
qa_q2_server_record *qa_app_q2_client_receiver_receive(qa_app_q2_client_receiver *receiver,
    const uint8_t *bytes, size_t length, double now_milliseconds, size_t *out_count);
/* Records are borrowed (acceptDecoded copies what it retains). */
void qa_app_q2_client_receiver_receive_records(qa_app_q2_client_receiver *receiver,
    qa_q2_server_record *records, size_t count, double now_milliseconds);
void qa_app_q2_client_receiver_prepare_game_state(qa_app_q2_client_receiver *receiver);
void qa_app_q2_client_receiver_close(qa_app_q2_client_receiver *receiver);
#endif
