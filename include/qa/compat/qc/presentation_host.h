/* Port of src/compat/qc/presentation-host.ts. GPL-2.0-or-later.
 * Presentation world is mirrored as fn+ctx (TS QcWorldHost pick).
 * Broadcast buffers/owners are owned by the struct; capture/restore
 * use SaveReader trees via the landed persistence value API.
 * capture() returns an owned qa_save_value built by caller-supplied
 * encoder helpers in the .c (checkpoint value tree). */
#ifndef QA_COMPAT_QC_PRESENTATION_HOST_H
#define QA_COMPAT_QC_PRESENTATION_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/builtins.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/contracts/content.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/network/q1/message.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/quakeworld.h"
#include "qa/persistence/value.h"

typedef struct qa_compat_qc_presentation_world {
  void *ctx; qa_compat_qc_program *program; qa_compat_qc_entity_memory *entities;
  const qa_compat_qc_host_binding *host; size_t host_count;
  qa_owned_actor (*actor)(void *ctx, int64_t slot);
  bool (*slot_actor)(void *ctx, int64_t slot, qa_actor_id *out); /* slots.at */
} qa_compat_qc_presentation_world;
typedef struct qa_compat_qc_precached_resource { double index; qa_resolved_resource_reference resource; } qa_compat_qc_precached_resource;
typedef enum qa_compat_qc_message_destination_kind { QA_QC_DEST_BROADCAST, QA_QC_DEST_CLIENT, QA_QC_DEST_SIGNON, QA_QC_DEST_MULTICAST } qa_compat_qc_message_destination_kind;
typedef struct qa_compat_qc_message_destination {
  qa_compat_qc_message_destination_kind kind; bool reliable;
  qa_actor_id actor; /* client */
  qa_vec3 origin; const char *visibility; /* multicast: "all"|"pvs"|"phs" */
} qa_compat_qc_message_destination;
typedef struct qa_compat_qc_routed_message { qa_quakeworld_message message; bool has_actor; qa_actor_id actor; } qa_compat_qc_routed_message;
typedef struct qa_compat_qc_netquake_message_services {
  void *ctx; const char *message_dialect; bool has_message_dialect;
  bool (*native)(void *ctx); bool (*local)(void *ctx); /* local nullable */
  bool (*loading)(void *ctx); bool (*client)(void *ctx, qa_actor_id actor);
  /* view_targets: parallel arrays index->actor (has_actor flags), may be NULL */
  void (*route)(void *ctx, const qa_netquake_message *messages, size_t count, qa_compat_qc_message_destination destination, const qa_actor_id *view_targets, const bool *view_has, size_t view_count);
} qa_compat_qc_netquake_message_services;
typedef struct qa_compat_qc_quakeworld_message_services {
  void *ctx;
  bool (*loading)(void *ctx); bool (*client)(void *ctx, qa_actor_id actor); bool (*phs)(void *ctx);
  void (*route)(void *ctx, const qa_compat_qc_routed_message *entries, size_t count, qa_compat_qc_message_destination destination);
} qa_compat_qc_quakeworld_message_services;
typedef struct qa_compat_qc_presentation_services {
  void *ctx;
  bool (*finale_finished)(void *ctx); /* nullable */
  const qa_compat_qc_quakeworld_message_services *qw;
  const qa_compat_qc_netquake_message_services *nq;
  qa_content_id content;
  qa_compat_qc_precached_resource (*precache)(void *ctx, const char *kind, const char *name);
  bool (*lookup)(void *ctx, const char *kind, const char *name, qa_compat_qc_precached_resource *out);
  bool (*loading)(void *ctx);
  void (*print)(void *ctx, const char *text);
  void (*broadcast_print)(void *ctx, const char *text, double level); /* nullable */
  bool print_broadcasts_to_clients;
  void (*message)(void *ctx, qa_network_event event, qa_actor_id actor); /* nullable */
  void (*emit)(void *ctx, qa_content_id content, qa_q1_event event);
  void (*register_resource)(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource);
} qa_compat_qc_presentation_services;

typedef struct qa_compat_qc_finale_ack {
  qa_actor_id *actors; bool *held; size_t count, capacity;
  bool has_last_poll; double last_poll; bool acknowledged;
} qa_compat_qc_finale_ack;
void qa_compat_qc_finale_ack_init(qa_compat_qc_finale_ack *a);
void qa_compat_qc_finale_ack_destroy(qa_compat_qc_finale_ack *a);
void qa_compat_qc_finale_ack_reset(qa_compat_qc_finale_ack *a);
bool qa_compat_qc_finale_ack_poll(qa_compat_qc_finale_ack *a, double seconds, const qa_actor_id *actors, const bool *down, size_t count);
void qa_compat_qc_finale_ack_dismiss(qa_compat_qc_finale_ack *a, double seconds);

typedef struct qa_compat_qc_presentation_bindings { qa_compat_qc_host_binding entries[40]; size_t count; } qa_compat_qc_presentation_bindings;
typedef struct qa_compat_qc_presentation_state qa_compat_qc_presentation_state;
struct qa_compat_qc_presentation_state {
  qa_compat_qc_presentation_world *world; qa_compat_qc_presentation_services services;
  struct { const char *name; struct qa_compat_qc_presentation_state *state; int which; } ctxs[40];
};
typedef struct qa_compat_qc_presentation_state qa_compat_qc_presentation_state;
qa_compat_qc_presentation_bindings qa_compat_qc_create_presentation_bindings(qa_compat_qc_presentation_world *world, qa_compat_qc_presentation_services services, qa_compat_qc_presentation_state *storage);

typedef struct qa_compat_qc_owner_entry { int64_t offset; bool has_actor; qa_actor_id actor; } qa_compat_qc_owner_entry;
typedef struct qa_compat_qc_routed_buffer {
  char *key; qa_size_buf buffer; qa_compat_qc_owner_entry *owners; size_t owner_count, owner_capacity;
  bool has_destination; qa_compat_qc_message_destination destination;
} qa_compat_qc_routed_buffer;
struct qa_compat_qc_broadcast_messages {
  qa_compat_qc_host_binding host_entries[9]; size_t host_count;
  qa_size_buf buffer; qa_compat_qc_owner_entry *owners; size_t owner_count, owner_capacity;
  qa_netquake_decoder decoder; int64_t signon_buffers; qa_quakeworld_decoder qw_decoder;
  qa_compat_qc_routed_buffer *routed; size_t routed_count, routed_capacity;
  qa_compat_qc_presentation_world *world; void *emit_ctx;
  void (*emit)(void *ctx, qa_q1_event effect, const qa_actor_id *recipient);
  const qa_compat_qc_quakeworld_message_services *qw;
  const qa_compat_qc_netquake_message_services *nq;
  struct { struct qa_compat_qc_broadcast_messages *self; int which; } ctxs[9];
};
typedef struct qa_compat_qc_broadcast_messages qa_compat_qc_broadcast_messages;
bool qa_compat_qc_broadcast_messages_init(qa_compat_qc_broadcast_messages *b, qa_compat_qc_presentation_world *world, void *emit_ctx, void (*emit)(void *ctx, qa_q1_event effect, const qa_actor_id *recipient), const qa_compat_qc_quakeworld_message_services *qw, const qa_compat_qc_netquake_message_services *nq);
void qa_compat_qc_broadcast_messages_destroy(qa_compat_qc_broadcast_messages *b);
uint8_t *qa_compat_qc_broadcast_messages_bytes(qa_compat_qc_broadcast_messages *b, size_t *out_length);
void qa_compat_qc_broadcast_messages_flush(qa_compat_qc_broadcast_messages *b);
void qa_compat_qc_broadcast_messages_flush_signon(qa_compat_qc_broadcast_messages *b);
uint8_t *qa_compat_qc_broadcast_capture_netquake_messages(qa_compat_qc_broadcast_messages *b, const qa_netquake_message *messages, size_t count, size_t *out_length);
qa_netquake_message *qa_compat_qc_broadcast_restore_netquake_messages(qa_compat_qc_broadcast_messages *b, const uint8_t *bytes, size_t length, size_t *out_count);
bool qa_compat_qc_saved_actor(const qa_actor_id *actor, qa_saved_actor_id *out);
/* Captured destination flattens client actor to saved id (has_saved). */
typedef struct qa_compat_qc_captured_destination { qa_compat_qc_message_destination destination; bool has_saved; qa_saved_actor_id saved; } qa_compat_qc_captured_destination;
qa_compat_qc_captured_destination qa_compat_qc_capture_destination(qa_compat_qc_message_destination destination);
typedef struct qa_compat_qc_actor_resolver { void *ctx; qa_actor_id (*fn)(void *ctx, qa_saved_actor_id saved); } qa_compat_qc_actor_resolver;
qa_compat_qc_message_destination qa_compat_qc_read_destination(const qa_save_reader *reader, qa_compat_qc_actor_resolver resolve);
#endif
/* TS QcBroadcastMessages.capture/restore + entry capture. Owned tree: qa_save_value_free. */
qa_save_value *qa_compat_qc_broadcast_messages_capture(qa_compat_qc_broadcast_messages *b);
void qa_compat_qc_broadcast_messages_restore(qa_compat_qc_broadcast_messages *b, const qa_save_value *value, qa_compat_qc_actor_resolver resolve);
qa_save_value *qa_compat_qc_broadcast_capture_entries(qa_compat_qc_broadcast_messages *b, const qa_compat_qc_routed_message *entries, size_t count);
qa_compat_qc_routed_message *qa_compat_qc_broadcast_restore_entries(qa_compat_qc_broadcast_messages *b, const qa_save_value *value, qa_compat_qc_actor_resolver resolve, size_t *out_count);
void qa_compat_qc_routed_messages_free(qa_compat_qc_routed_message *entries, size_t count);
