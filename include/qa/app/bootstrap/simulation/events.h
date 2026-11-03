/* Port of src/app/bootstrap/simulation/events.ts. Adds authoritative
 * native protocol output to the shared presentation history.
 * SimulationEvents extends PresentationState in TS; the superclass
 * surface used here (emitOwned, resourceFor, assertOutputConsumed,
 * now/sourceSlot from the base constructor) is the host bundle below,
 * supplied by the runtime slice over qa_app_presentation_state.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_EVENTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_EVENTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/composition/q2/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h"
#include "qa/content/q2/multiplayer/ctf/types.h"
#include "qa/content/q2/multiplayer/lmctf/types.h"
#include "qa/contracts/presentation.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/session.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/body.h"
/* Undrained authoritative events retained between publications. */
#define QA_APP_SIM_SIMULATION_EVENT_CAPACITY 16384
/* emitOwned's TS argument type, SourcePresentationEvent (types.h); borrowed
 * for the call. */
typedef qa_app_sim_source_presentation_event qa_app_sim_source_event_input;
typedef struct qa_app_sim_simulation_events_options {
  void *ctx;
  qa_shared_body_table *bodies;
  qa_source_time (*now)(void *ctx);
  bool (*client_for)(void *ctx, qa_actor_id actor, qa_client_id *out);
  bool (*source_slot)(void *ctx, qa_actor_id actor, double *out);
  /* PresentationState.resourceFor: false is the TS undefined. */
  bool (*resource_for)(void *ctx, qa_content_id content, const char *path, qa_resource_id *out);
  /* PresentationState.emitOwned (super). Fills the returned
   * presentation event (a view borrowing source's payload), notably its
   * sequence. */
  void (*emit_super)(void *ctx, bool has_owner, qa_presentation_owner owner, qa_content_id content,
      qa_app_sim_source_event_input source, qa_source_time time, bool has_recipient, qa_actor_id recipient,
      qa_app_sim_presentation_event *out);
  void (*assert_super_output_consumed)(void *ctx); /* may be NULL */
  /* PresentationState.registerResource / lightStyle (super); may be NULL
   * when the host composes no presentation resources or styles. */
  void (*register_resource_super)(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource);
  const char *(*light_style_super)(void *ctx, double style);
} qa_app_sim_simulation_events_options;
typedef struct qa_app_sim_simulation_events qa_app_sim_simulation_events;
qa_app_sim_simulation_events *qa_app_sim_simulation_events_create(qa_app_sim_simulation_events_options options);
void qa_app_sim_simulation_events_destroy(qa_app_sim_simulation_events *events);
double qa_app_sim_simulation_events_next_sequence(const qa_app_sim_simulation_events *events);
/* TS emitOwned: has_time false uses options.now(). */
qa_app_sim_presentation_event qa_app_sim_simulation_events_emit_owned(qa_app_sim_simulation_events *events,
    bool has_owner, qa_presentation_owner owner, qa_content_id content, qa_app_sim_source_event_input source,
    bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient);
/* TS PresentationState.emit: emitOwned with no owner. */
void qa_app_sim_simulation_events_emit(qa_app_sim_simulation_events *events, qa_content_id content,
    qa_app_sim_source_event_input source, bool has_time, qa_source_time time, bool has_recipient, qa_actor_id recipient);
/* TS PresentationState.registerResource. */
void qa_app_sim_simulation_events_register_resource(qa_app_sim_simulation_events *events, qa_content_id content,
    const char *path, qa_resolved_resource_reference resource);
/* TS PresentationState.lightStyle: "" when unset. */
const char *qa_app_sim_simulation_events_light_style(qa_app_sim_simulation_events *events, double style);
/* TS append: has_actor false is the TS null (world audience). */
void qa_app_sim_simulation_events_append(qa_app_sim_simulation_events *events,
    qa_simulation_event_payload payload, bool has_actor, qa_actor_id actor);
/* TS message: has_sequence mirrors the optional argument. */
void qa_app_sim_simulation_events_message(qa_app_sim_simulation_events *events, qa_network_event event,
    bool has_actor, qa_actor_id actor, bool has_sequence, double source_presentation_sequence);
/* TS take: events-owned, valid until the next append. */
const qa_simulation_event *qa_app_sim_simulation_events_take(qa_app_sim_simulation_events *events, size_t *out_count);
/* TS drainEvents: zero-allocation in-place visit; returns the count. */
size_t qa_app_sim_simulation_events_drain(qa_app_sim_simulation_events *events,
    void (*visit)(void *ctx, qa_simulation_event event), void *visit_ctx);
void qa_app_sim_simulation_events_assert_output_consumed(qa_app_sim_simulation_events *events);
/* TS capture/restore sequence component; the super (presentation
 * state) capture/restore rides the caller's presentation-state host. */
double qa_app_sim_simulation_events_capture_sequence(const qa_app_sim_simulation_events *events);
void qa_app_sim_simulation_events_restore(qa_app_sim_simulation_events *events, const qa_save_reader *reader);
#endif
