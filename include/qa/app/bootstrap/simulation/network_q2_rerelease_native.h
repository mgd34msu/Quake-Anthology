/* Port of src/app/bootstrap/simulation/network-q2-rerelease-native.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_RERELEASE_NATIVE_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_RERELEASE_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
typedef struct qa_app_sim_rerelease_transcode {
  void *ctx;
  /* Malloc'd transcoded bytes; caller frees. */
  uint8_t *(*transcode)(void *ctx, const uint8_t *bytes, size_t length, size_t *out_length);
} qa_app_sim_rerelease_transcode;
typedef struct qa_app_sim_rerelease_native_binding_options {
  qa_app_sim_q2_server_binding_options base;
  qa_app_sim_rerelease_guest_world *world;
  qa_cvar_registry *cvars;
  double max_clients;
  const char *map_path;
  int64_t map_checksum;
  const char *air_accelerate;
  qa_app_q2_server_host host; /* frame projection */
} qa_app_sim_rerelease_native_binding_options;
qa_app_q2_server_host qa_app_sim_create_rerelease_native_q2_application_server_host(qa_app_sim_rerelease_native_binding_options options);
qa_app_sim_rerelease_transcode qa_app_sim_create_rerelease_native_message_transcoder(qa_q2_protocol_identity protocol,
    void *ctx, uint8_t *(*transcode)(void *ctx, const uint8_t *bytes, size_t length, size_t *out_length));
#endif
