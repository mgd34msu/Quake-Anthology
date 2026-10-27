/* Port of src/world/actors/triggers.ts. GPL-2.0-or-later. */
#include "qa/world/actors/triggers.h"

#include <string.h>

typedef struct visit_state {
  qa_q1_trigger_services services;
  qa_owned_actor actor;
} visit_state;

static const char *visit_candidate(void *ctx, qa_spatial_actor candidate) {
  visit_state *state = ctx;
  qa_q1_trigger_services *services = &state->services;
  qa_owned_actor actor = state->actor;
  if (!qa_session_actor_registry_is_live(services->actors, actor.id)) return "stop";
  qa_actor_id id = candidate.body.actor;
  if (candidate.collision.role != NULL && strcmp(candidate.collision.role, "trigger") == 0 &&
      !qa_same_actor(id, actor.id)) {
    qa_owned_actor trigger;
    qa_linked_body_result current = qa_shared_body_table_linked(services->bodies, id);
    qa_linked_body_result moving = qa_shared_body_table_linked(services->bodies, actor.id);
    if (qa_session_actor_registry_resolve_owned(services->actors, id, &trigger) &&
        current.has_value && moving.has_value &&
        services->is_trigger(services->ctx, trigger) &&
        qa_bounds_intersect(current.value.absolute_bounds, moving.value.absolute_bounds)) {
      qa_touch_contact contact;
      contact.self = trigger;
      contact.other = actor.id;
      contact.plane = NULL;
      contact.surface = NULL;
      contact.source_trace = NULL;
      services->touch(services->ctx, contact);
    }
  }
  return qa_session_actor_registry_is_live(services->actors, actor.id) ? "continue" : "stop";
}

void qa_touch_q1_triggers(qa_q1_trigger_services services, qa_owned_actor actor) {
  qa_linked_body_result linked = qa_shared_body_table_linked(services.bodies, actor.id);
  if (!linked.has_value || !qa_session_actor_registry_is_live(services.actors, actor.id)) return;
  visit_state state = {services, actor};
  qa_spatial_index_visit(services.spatial, linked.value.absolute_bounds, visit_candidate, &state);
}
