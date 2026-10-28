/* Port of src/app/bootstrap/native-q2-client.ts. Q2NativeWorld and
 * the network/q2-layout + q2-service-presentation helpers are other
 * bootstrap slices behind the host below. The unlanded
 * SimulationPresentationEvent is carried as an opaque kind/payload
 * record. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NATIVE_Q2_CLIENT_H
#define QA_APP_BOOTSTRAP_NATIVE_Q2_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/network/q2/server_messages.h"
typedef struct qa_app_native_q2_event {
  const char *kind;      /* borrowed */
  const void *payload;   /* borrowed, host-defined */
} qa_app_native_q2_event;
typedef struct qa_app_native_q2_layout {
  double max_config_strings, images, sounds, player_skins;
} qa_app_native_q2_layout;
/* Q2ServicePresentationHost surface filled by the client below and
 * consumed by the host's translate/print functions. */
typedef struct qa_app_native_q2_presentation_host {
  void *ctx;
  qa_content_id (*content)(void *ctx);
  double seconds;
  double (*next_sequence)(void *ctx);
  const char *edition;
  bool (*player)(void *ctx, qa_actor_id *actor_out, double *source_entity_out);
  bool (*actor)(void *ctx, double slot, qa_actor_id *out);
  const void *(*entity)(void *ctx, double slot);
  const void *(*fog)(void *ctx, const void *value);
  double image_config_offset, sound_config_offset, player_skin_config_offset;
  const char *(*config_string)(void *ctx, double index);
  void (*set_config_string)(void *ctx, double index, const char *value);
  void (*set_inventory)(void *ctx, const double *counts, size_t count);
  void (*set_layout)(void *ctx, const char *value);
  void (*emit)(void *ctx, qa_app_native_q2_event event);
} qa_app_native_q2_presentation_host;
typedef struct qa_app_native_q2_world {
  void *ctx;
  const char *edition; /* "classic"|"rerelease" */
  bool (*actor)(void *ctx, double slot, qa_actor_id *out);
  /* Borrowed configstring rows at construction. */
  size_t (*configstrings)(void *ctx, const double **indices, const char *const **values);
  const void *(*player_state)(void *ctx, double slot);
  bool (*entity_active)(void *ctx, double slot);
  const void *(*entity_state)(void *ctx, double slot);
  /* q2ApplicationLayout(protocol) + q2FogFromWire + service records. */
  qa_app_native_q2_layout (*layout)(void *ctx, qa_q2_protocol_identity protocol);
  const void *(*fog_from_wire)(void *ctx, const void *current, const void *value);
  const void *empty_fog;
  /* translateQ2ServiceRecords: remaining records borrowed. */
  size_t (*translate)(void *ctx, qa_q2_server_record *records, size_t count,
      qa_app_native_q2_presentation_host *host, qa_q2_server_record **remaining_out);
  void (*service_print)(void *ctx, qa_app_native_q2_presentation_host *host, double level, const char *text);
} qa_app_native_q2_world;
typedef struct qa_app_native_q2_client qa_app_native_q2_client;
/* Aborts when the source slot is not the admitted actor (TS Error). */
qa_app_native_q2_client *qa_app_native_q2_client_create(qa_app_native_q2_world world,
    double source_slot, qa_actor_id actor, qa_content_id content);
void qa_app_native_q2_client_destroy(qa_app_native_q2_client *client);
const char *qa_app_native_q2_client_config_string(qa_app_native_q2_client *client, double index);
const double *qa_app_native_q2_client_inventory(qa_app_native_q2_client *client, size_t *out_count);
const char *qa_app_native_q2_client_layout(qa_app_native_q2_client *client);
const void *qa_app_native_q2_client_player_state(qa_app_native_q2_client *client);
/* Remaining records are malloc'd (caller frees the array). */
size_t qa_app_native_q2_client_receive(qa_app_native_q2_client *client,
    const uint8_t *const *messages, const size_t *lengths, size_t message_count,
    double seconds, qa_q2_server_record **remaining_out);
void qa_app_native_q2_client_print(qa_app_native_q2_client *client, double level,
    const char *text, double seconds);
/* Drained events are malloc'd (caller frees the array). */
size_t qa_app_native_q2_client_take_events(qa_app_native_q2_client *client,
    qa_app_native_q2_event **out);
#endif
