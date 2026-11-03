/* Port of src/app/bootstrap/native-q2-client.ts: NativeQ2ClientPresentation, the client
 * presentation state decoded from the source host's recipient-filtered messages over the
 * real Q2NativeWorld (classic or rerelease guest world), Q2ServerMessageReader and the Q2
 * service presentation (network/q2_service_presentation.h). Events are the
 * SimulationPresentationEvent union. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NATIVE_Q2_CLIENT_H
#define QA_APP_BOOTSTRAP_NATIVE_Q2_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/classic_guest_player.h"
#include "qa/app/bootstrap/simulation/native_primary_weapons.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
#include "qa/network/q2/server_messages.h"
typedef struct qa_app_native_q2_client qa_app_native_q2_client;
/* TS Pick<ClassicGuestMessage, "bytes">. */
typedef struct qa_app_native_q2_message {
  const uint8_t *bytes;
  size_t length;
} qa_app_native_q2_message;
/* TS world.playerState(slot): the classic or rerelease player state. */
typedef struct qa_app_native_q2_player_state {
  bool rerelease;
  qa_q2_player_state classic;
  qa_q2_rerelease_player_state rerelease_state;
} qa_app_native_q2_player_state;
/* TS constructor. Raises Error "Native client presentation requires its admitted source
 * player" when the slot's actor is not `actor`. The world is borrowed; content is copied. */
qa_app_native_q2_client *qa_app_native_q2_client_create(qa_app_sim_q2_native_world world,
    double source_slot, qa_actor_id actor, qa_content_id content);
void qa_app_native_q2_client_destroy(qa_app_native_q2_client *client);
qa_app_sim_q2_native_world qa_app_native_q2_client_world(const qa_app_native_q2_client *client);
double qa_app_native_q2_client_source_slot(const qa_app_native_q2_client *client);
qa_actor_id qa_app_native_q2_client_actor(const qa_app_native_q2_client *client);
qa_content_id qa_app_native_q2_client_content(const qa_app_native_q2_client *client);
/* TS configstrings (ReadonlyMap<number, string>): entries in Map order, and one lookup
 * (NULL is undefined). Borrowed until the next receive/print. */
const qa_app_sim_configstring *qa_app_native_q2_client_configstrings(const qa_app_native_q2_client *client,
    size_t *out_count);
const char *qa_app_native_q2_client_config_string(const qa_app_native_q2_client *client, double index);
const double *qa_app_native_q2_client_inventory(const qa_app_native_q2_client *client, size_t *out_count);
const char *qa_app_native_q2_client_layout(const qa_app_native_q2_client *client);
qa_app_native_q2_player_state qa_app_native_q2_client_player_state(const qa_app_native_q2_client *client);
/* TS receive(messages, seconds): the records translateQ2ServiceRecords leaves, owned by the
 * caller (qa_q2_server_records_free). */
size_t qa_app_native_q2_client_receive(qa_app_native_q2_client *client, const qa_app_native_q2_message *messages,
    size_t message_count, double seconds, qa_q2_server_record **remaining_out);
void qa_app_native_q2_client_print(qa_app_native_q2_client *client, double level, const char *text, double seconds);
/* TS takeEvents(): owned events (release each with qa_app_sim_presentation_event_release,
 * then free the array); NULL with 0 when none are pending. */
size_t qa_app_native_q2_client_take_events(qa_app_native_q2_client *client, qa_app_sim_presentation_event **out);
#endif
