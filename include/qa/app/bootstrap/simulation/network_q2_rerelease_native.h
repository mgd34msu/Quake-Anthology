/* Port of src/app/bootstrap/simulation/network-q2-rerelease-native.ts (API 2023 guest
 * projection through the shared native Q2 server channel). The guest world is the
 * qa_app_sim_q2_guest_world surface (network_q2_guest.h) filled by the rerelease world:
 * player_state fills its `rerelease` argument and entity_states returns
 * qa_q2_rerelease_entity_state. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_RERELEASE_NATIVE_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_RERELEASE_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/network_q2_guest.h"

/* TS createRereleaseNativeQ2ApplicationServerHost(options & { world }). host.ctx is
 * owned; free with _destroy. */
qa_app_q2_server_host qa_app_sim_create_rerelease_native_q2_application_server_host(
    qa_app_sim_q2_server_binding_options options, const qa_app_sim_q2_guest_world *world);
void qa_app_sim_rerelease_native_q2_application_server_host_destroy(qa_app_q2_server_host host);

/* TS createRereleaseNativeMessageTranscoder(protocol): game imports write FLOAT/1038
 * records; KEX clients receive newly encoded KEX records. The returned closure keeps its
 * reader and codec state across calls. Raises Error for other protocols. */
typedef struct qa_app_sim_rerelease_transcoder qa_app_sim_rerelease_transcoder;
qa_app_sim_rerelease_transcoder *qa_app_sim_create_rerelease_native_message_transcoder(qa_q2_protocol_identity protocol);
/* The closure call: malloc'd bytes. Raises as TS throws. */
uint8_t *qa_app_sim_rerelease_transcode(qa_app_sim_rerelease_transcoder *transcoder, const uint8_t *bytes, size_t length,
    size_t *out_length);
void qa_app_sim_rerelease_transcoder_destroy(qa_app_sim_rerelease_transcoder *transcoder);
#endif
