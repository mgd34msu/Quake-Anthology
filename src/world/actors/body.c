/* Port of src/world/actors/body.ts. GPL-2.0-or-later. */
#include "qa/world/actors/body.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}

qa_vec3 qa_copy_vector(qa_vec3 vector) { return vector; }
qa_bounds qa_copy_bounds(qa_bounds bounds) { return bounds; }
qa_body_state qa_copy_body(qa_body_state state) { return state; }

qa_bounds qa_translated_body_bounds(qa_owned_actor actor, qa_body_state state) {
  (void)actor;
  qa_bounds bounds;
  bounds.min.x = state.origin.x + state.bounds.min.x;
  bounds.min.y = state.origin.y + state.bounds.min.y;
  bounds.min.z = state.origin.z + state.bounds.min.z;
  bounds.max.x = state.origin.x + state.bounds.max.x;
  bounds.max.y = state.origin.y + state.bounds.max.y;
  bounds.max.z = state.origin.z + state.bounds.max.z;
  return bounds;
}

typedef struct body_record {
  qa_owned_actor actor;
  bool external;
  qa_body_state_binding binding; /* external */
  bool has_snapshot;
  qa_body_state snapshot;        /* external snapshot / local state */
  qa_actor_id ground_storage;    /* backs snapshot.ground */
  bool has_linked;
  qa_linked_body linked;
  qa_actor_id linked_ground_storage;
  double link_count;
  bool has_attachment;
  qa_body_attachment attachment;
} body_record;

struct qa_shared_body_table {
  qa_session_actor_registry *actors;
  qa_body_link_hooks hooks;
  body_record **records; /* indexed by actor slot */
  size_t record_capacity;
  int64_t release_subscription;
};

/* Retarget state.ground onto caller-owned storage. */
static qa_body_state own_ground(qa_body_state state, qa_actor_id *storage) {
  if (state.ground != NULL) {
    *storage = *state.ground;
    state.ground = storage;
  }
  return state;
}

static body_record *record_at(qa_shared_body_table *table, int64_t slot_value) {
  if (slot_value < 0 || (size_t)slot_value >= table->record_capacity) return NULL;
  return table->records[slot_value];
}
static body_record *live_record(qa_shared_body_table *table, qa_actor_id actor) {
  if (!qa_session_actor_registry_is_live(table->actors, actor)) return NULL;
  return record_at(table, actor.slot);
}
static void set_record(qa_shared_body_table *table, int64_t slot_value, body_record *record) {
  if ((size_t)slot_value >= table->record_capacity) {
    size_t capacity = table->record_capacity != 0 ? table->record_capacity : 16;
    while ((size_t)slot_value >= capacity) capacity *= 2;
    table->records = xrealloc(table->records, capacity * sizeof *table->records);
    for (size_t i = table->record_capacity; i < capacity; i++) table->records[i] = NULL;
    table->record_capacity = capacity;
  }
  table->records[slot_value] = record;
}

static void on_release(void *ctx, qa_owned_actor actor) {
  qa_shared_body_table *table = ctx;
  body_record *own = record_at(table, actor.id.slot);
  bool own_is_actor = own != NULL && qa_same_owned_actor(own->actor, actor);
  /* Collect children attached to the released anchor. */
  qa_owned_actor *children = NULL;
  size_t child_count = 0, child_capacity = 0;
  for (size_t i = 0; i < table->record_capacity; i++) {
    body_record *record = table->records[i];
    if (record == NULL) continue;
    if (qa_same_owned_actor(record->actor, actor)) record->has_attachment = false;
    else if (record->has_attachment && qa_same_actor(record->attachment.anchor, actor.id)) {
      record->has_attachment = false;
      if (child_count == child_capacity) {
        child_capacity = child_capacity != 0 ? child_capacity * 2 : 4;
        children = xrealloc(children, child_capacity * sizeof *children);
      }
      children[child_count++] = record->actor;
    }
  }
  bool was_linked = false;
  if (own_is_actor) {
    was_linked = own->has_linked;
    table->records[actor.id.slot] = NULL;
    free(own);
  }
  if (was_linked) table->hooks.on_unlink(table->hooks.ctx, actor.id);
  for (size_t i = 0; i < child_count; i++) {
    if (qa_session_actor_registry_is_live(table->actors, children[i].id))
      qa_session_actor_registry_release(table->actors, children[i]);
  }
  free(children);
}

qa_shared_body_table *qa_shared_body_table_create(qa_session_actor_registry *actors,
                                                  qa_body_link_hooks hooks) {
  qa_shared_body_table *table = calloc(1, sizeof *table);
  if (table == NULL) return NULL;
  table->actors = actors;
  table->hooks = hooks;
  table->release_subscription =
      qa_session_actor_registry_on_release(actors, on_release, table);
  return table;
}

void qa_shared_body_table_destroy(qa_shared_body_table *table) {
  if (table == NULL) return;
  qa_session_actor_registry_off_release(table->actors, table->release_subscription);
  for (size_t i = 0; i < table->record_capacity; i++) free(table->records[i]);
  free(table->records);
  free(table);
}

void qa_shared_body_table_bind(qa_shared_body_table *table, qa_owned_actor actor,
                               qa_body_state_binding binding) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  if (record_at(table, actor.id.slot) != NULL) qa_error("Actor already has a body binding");
  body_record *record = calloc(1, sizeof *record);
  if (record == NULL) abort();
  record->actor = actor;
  record->external = true;
  record->binding = binding;
  set_record(table, actor.id.slot, record);
}

void qa_shared_body_table_rebind(qa_shared_body_table *table, qa_owned_actor actor,
                                 qa_body_state_binding binding) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *previous = live_record(table, actor.id);
  if (previous == NULL) {
    qa_shared_body_table_bind(table, actor, binding);
    return;
  }
  previous->external = true;
  previous->binding = binding;
  previous->has_snapshot = false;
}

void qa_shared_body_table_create_body(qa_shared_body_table *table, qa_owned_actor actor,
                                      qa_body_state initial) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  if (record_at(table, actor.id.slot) != NULL) qa_error("Actor already has a body binding");
  body_record *record = calloc(1, sizeof *record);
  if (record == NULL) abort();
  record->actor = actor;
  record->external = false;
  record->snapshot = own_ground(initial, &record->ground_storage);
  record->has_snapshot = true;
  set_record(table, actor.id.slot, record);
}

qa_body_state_result qa_shared_body_table_read(qa_shared_body_table *table, qa_actor_id actor) {
  qa_body_state_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  body_record *record = live_record(table, actor);
  if (record == NULL) return result;
  if (!record->external) {
    result.has_value = true;
    result.value = record->snapshot;
    return result;
  }
  qa_body_state current = record->binding.read(record->binding.ctx);
  record->snapshot = own_ground(current, &record->ground_storage);
  record->has_snapshot = true;
  result.has_value = true;
  result.value = record->snapshot;
  return result;
}

void qa_shared_body_table_write(qa_shared_body_table *table, qa_owned_actor actor,
                                qa_body_state state) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *record = live_record(table, actor.id);
  if (record == NULL) {
    qa_shared_body_table_create_body(table, actor, state);
    return;
  }
  if (record->external) {
    qa_actor_id ground;
    record->binding.write(record->binding.ctx, own_ground(state, &ground));
    return;
  }
  record->snapshot = own_ground(state, &record->ground_storage);
  record->has_snapshot = true;
}

void qa_shared_body_table_attach(qa_shared_body_table *table, qa_owned_actor actor,
                                 qa_body_attachment attachment) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *record = live_record(table, actor.id);
  if (record == NULL) qa_error("Cannot attach an actor without a body");
  body_record *anchor = live_record(table, attachment.anchor);
  if (anchor == NULL) qa_error("Cannot attach to a missing anchor body");
  for (body_record *ancestor = anchor; ancestor != NULL;) {
    if (qa_same_owned_actor(ancestor->actor, actor)) qa_error("Body attachment cycle");
    ancestor = ancestor->has_attachment ? live_record(table, ancestor->attachment.anchor) : NULL;
  }
  record->attachment = attachment;
  record->has_attachment = true;
}

void qa_shared_body_table_detach(qa_shared_body_table *table, qa_owned_actor actor) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *record = record_at(table, actor.id.slot);
  if (record != NULL && qa_same_owned_actor(record->actor, actor)) record->has_attachment = false;
}

qa_body_attachment_result qa_shared_body_table_attachment(qa_shared_body_table *table,
                                                          qa_actor_id actor) {
  qa_body_attachment_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  body_record *record = live_record(table, actor);
  if (record != NULL && record->has_attachment) {
    result.has_value = true;
    result.value = record->attachment;
  }
  return result;
}

static void transport_one(qa_shared_body_table *table, qa_numeric_operations numeric,
                          qa_owned_actor actor, bool *transported, size_t transported_count);

static double vec_component(qa_vec3 vector, int axis) {
  return axis == 0 ? vector.x : axis == 1 ? vector.y : vector.z;
}

static void transport_one(qa_shared_body_table *table, qa_numeric_operations numeric,
                          qa_owned_actor actor, bool *transported, size_t transported_count) {
  if (actor.id.slot >= 0 && (size_t)actor.id.slot < transported_count) {
    if (transported[actor.id.slot]) return;
    transported[actor.id.slot] = true;
  }
  body_record *record = record_at(table, actor.id.slot);
  if (record == NULL || !qa_same_owned_actor(record->actor, actor) || !record->has_attachment) return;
  qa_body_attachment attachment = record->attachment;
  body_record *anchor_record = live_record(table, attachment.anchor);
  if (anchor_record == NULL) return;
  transport_one(table, numeric, anchor_record->actor, transported, transported_count);
  qa_body_state_result anchor = qa_shared_body_table_read(table, attachment.anchor);
  qa_body_state_result body = qa_shared_body_table_read(table, actor.id);
  if (!anchor.has_value || !body.has_value) return;
  double components[3];
  for (int axis = 0; axis < 3; axis++) {
    double offset;
    if (attachment.follow_kind == QA_BODY_FOLLOW_CENTER) {
      offset = numeric.multiply(numeric.ctx,
          numeric.add(numeric.ctx, vec_component(anchor.value.bounds.min, axis),
                      vec_component(anchor.value.bounds.max, axis)), 0.5);
    } else if (attachment.follow_kind == QA_BODY_FOLLOW_BOUNDS_MIN) {
      offset = numeric.add(numeric.ctx, vec_component(anchor.value.bounds.min, axis),
                           vec_component(attachment.follow_offset, axis));
    } else {
      offset = vec_component(attachment.follow_offset, axis);
    }
    components[axis] = numeric.store(numeric.ctx,
        numeric.add(numeric.ctx, vec_component(anchor.value.origin, axis), offset));
  }
  qa_vec3 origin = {components[0], components[1], components[2]};
  /* TS Object.is comparison (distinguishes -0). */
  if (memcmp(&origin.x, &body.value.origin.x, sizeof(double)) == 0 &&
      memcmp(&origin.y, &body.value.origin.y, sizeof(double)) == 0 &&
      memcmp(&origin.z, &body.value.origin.z, sizeof(double)) == 0) return;
  qa_body_state next = body.value;
  next.origin = origin;
  qa_shared_body_table_write(table, actor, next);
  qa_shared_body_table_link(table, actor, NULL);
}

void qa_shared_body_table_transport_attachments(qa_shared_body_table *table,
                                                qa_numeric_operations numeric) {
  size_t count = table->record_capacity != 0 ? table->record_capacity : 1;
  bool *transported = calloc(count, sizeof *transported);
  if (transported == NULL) abort();
  /* Snapshot attached actors first (TS iterates the attachment map). */
  qa_owned_actor *actors = NULL;
  size_t actor_count = 0, actor_capacity = 0;
  for (size_t i = 0; i < table->record_capacity; i++) {
    body_record *record = table->records[i];
    if (record != NULL && record->has_attachment) {
      if (actor_count == actor_capacity) {
        actor_capacity = actor_capacity != 0 ? actor_capacity * 2 : 8;
        actors = xrealloc(actors, actor_capacity * sizeof *actors);
      }
      actors[actor_count++] = record->actor;
    }
  }
  for (size_t i = 0; i < actor_count; i++)
    transport_one(table, numeric, actors[i], transported, count);
  free(actors);
  free(transported);
}

qa_linked_body_result qa_shared_body_table_linked(qa_shared_body_table *table,
                                                  qa_actor_id actor) {
  qa_linked_body_result result;
  result.has_value = false;
  memset(&result.value, 0, sizeof result.value);
  body_record *record = live_record(table, actor);
  if (record != NULL && record->has_linked) {
    result.has_value = true;
    result.value = record->linked;
  }
  return result;
}

bool qa_shared_body_table_link_state(qa_shared_body_table *table, qa_actor_id actor,
                                     qa_body_link_state *out) {
  body_record *record = live_record(table, actor);
  if (record == NULL) return false;
  out->link_count = record->link_count;
  out->has_linked = record->has_linked;
  if (record->has_linked) out->linked = record->linked;
  return true;
}

void qa_shared_body_table_restore_link_state(qa_shared_body_table *table,
                                             qa_owned_actor actor, qa_body_link_state saved) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *record = live_record(table, actor.id);
  if (record == NULL) qa_error("Cannot restore links without an actor body");
  if (!(saved.link_count >= 0) || saved.link_count > 9007199254740991.0 ||
      saved.link_count != (double)(int64_t)saved.link_count ||
      (saved.has_linked && saved.link_count == 0))
    qa_range_error("Invalid saved body link count");
  record->link_count = saved.link_count;
  if (!saved.has_linked) {
    record->has_linked = false;
    table->hooks.on_unlink(table->hooks.ctx, actor.id);
    return;
  }
  record->linked.actor = actor.id;
  record->linked.state = own_ground(saved.linked.state, &record->linked_ground_storage);
  record->linked.absolute_bounds = saved.linked.absolute_bounds;
  record->linked.link_count = saved.link_count;
  record->has_linked = true;
  if (record->external && record->binding.linked != NULL)
    record->binding.linked(record->binding.ctx, record->linked);
  table->hooks.on_link(table->hooks.ctx, record->linked);
}

void qa_shared_body_table_link(qa_shared_body_table *table, qa_owned_actor actor,
                               const qa_vec3 *origin) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *record = live_record(table, actor.id);
  if (record == NULL) qa_error("Cannot link an actor without a body");
  qa_body_state current = !record->external ? record->snapshot
                                            : record->binding.read(record->binding.ctx);
  qa_body_state state = current;
  if (origin != NULL) state.origin = *origin;
  record->linked.actor = actor.id;
  record->linked.state = own_ground(state, &record->linked_ground_storage);
  record->linked.absolute_bounds =
      table->hooks.absolute_bounds(table->hooks.ctx, actor, record->linked.state);
  record->linked.link_count = ++record->link_count;
  record->has_linked = true;
  if (record->external && record->binding.linked != NULL)
    record->binding.linked(record->binding.ctx, record->linked);
  table->hooks.on_link(table->hooks.ctx, record->linked);
}

void qa_shared_body_table_unlink(qa_shared_body_table *table, qa_owned_actor actor) {
  qa_session_actor_registry_assert_owned(table->actors, actor);
  body_record *record = live_record(table, actor.id);
  if (record == NULL || !record->has_linked) return;
  record->has_linked = false;
  table->hooks.on_unlink(table->hooks.ctx, actor.id);
}

/* --- contracts adapter --- */
static qa_body_state_result c_read(void *ctx, qa_actor_id actor) {
  return qa_shared_body_table_read(ctx, actor);
}
static void c_write(void *ctx, qa_owned_actor actor, qa_body_state state) {
  qa_shared_body_table_write(ctx, actor, state);
}
static void c_attach(void *ctx, qa_owned_actor actor, qa_body_attachment attachment) {
  qa_shared_body_table_attach(ctx, actor, attachment);
}
static void c_detach(void *ctx, qa_owned_actor actor) { qa_shared_body_table_detach(ctx, actor); }
static qa_body_attachment_result c_attachment(void *ctx, qa_actor_id actor) {
  return qa_shared_body_table_attachment(ctx, actor);
}
static qa_linked_body_result c_linked(void *ctx, qa_actor_id actor) {
  return qa_shared_body_table_linked(ctx, actor);
}
static void c_link(void *ctx, qa_owned_actor actor, const qa_vec3 *origin) {
  qa_shared_body_table_link(ctx, actor, origin);
}
static void c_unlink(void *ctx, qa_owned_actor actor) { qa_shared_body_table_unlink(ctx, actor); }
qa_body_table qa_shared_body_table_as_contract(qa_shared_body_table *table) {
  qa_body_table contract;
  contract.ctx = table;
  contract.read = c_read;
  contract.write = c_write;
  contract.attach = c_attach;
  contract.detach = c_detach;
  contract.attachment = c_attachment;
  contract.linked = c_linked;
  contract.link = c_link;
  contract.unlink = c_unlink;
  return contract;
}
