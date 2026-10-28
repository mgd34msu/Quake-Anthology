/* See include/qa/compat/qvm/mod_player_events.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/mod_player_events.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/player_record.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

static qa_save_reader field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static qa_save_reader item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out)) qa_range_error("QVM memory allocation failed");
  return out;
}

static int32_t reader_int32(qa_save_reader *reader) {
  double value = qa_save_reader_integer_min(reader, -2147483648.0);
  if (value > 2147483647.0) qa_save_reader_fail(reader, "expected a source int32");
  return (int32_t)value;
}

bool qa_qvm_read_player_events(const qa_save_reader *reader, qa_qvm_player_events_checkpoint *out) {
  memset(out, 0, sizeof *out);
  if (qa_save_reader_is_undefined(reader)) return false;
  qa_save_reader next_order_reader = field(reader, "nextOrder");
  int64_t next_order = (int64_t)qa_save_reader_integer_min(&next_order_reader, 0);
  qa_save_reader_destroy(&next_order_reader);
  out->next_order = next_order;
  /* Seen orders across every cursor, checked in TS order (predictable
   * entries before externalOrder within each client). */
  int64_t *orders = NULL;
  size_t order_count = 0;
  qa_save_reader clients_reader = field(reader, "clients");
  size_t client_count = qa_save_reader_list_count(&clients_reader);
  out->clients = xmalloc(client_count * sizeof *out->clients);
  out->client_count = client_count;
  for (size_t i = 0; i < client_count; i++) {
    qa_save_reader entry_reader = item(&clients_reader, i);
    qa_qvm_saved_event_cursor *cursor = &out->clients[i];
    memset(cursor, 0, sizeof *cursor);
    qa_save_reader actor_reader = field(&entry_reader, "actor");
    cursor->actor = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    for (size_t j = 0; j < i; j++) {
      if (out->clients[j].actor.slot == cursor->actor.slot &&
          out->clients[j].actor.generation == cursor->actor.generation)
        qa_save_reader_fail(&entry_reader, "duplicate player event cursor");
    }
    qa_save_reader sequence_reader = field(&entry_reader, "sequence");
    cursor->sequence = reader_int32(&sequence_reader);
    qa_save_reader_destroy(&sequence_reader);
    qa_save_reader observed_reader = field(&entry_reader, "observedSequence");
    cursor->observed_sequence = reader_int32(&observed_reader);
    qa_save_reader_destroy(&observed_reader);
    qa_save_reader predictable_reader = field(&entry_reader, "predictable");
    size_t predictable_count = qa_save_reader_list_count(&predictable_reader);
    cursor->predictable = xmalloc(predictable_count * sizeof *cursor->predictable);
    cursor->predictable_count = predictable_count;
    for (size_t p = 0; p < predictable_count; p++) {
      qa_save_reader predictable_entry = item(&predictable_reader, p);
      qa_save_reader pseq = field(&predictable_entry, "sequence");
      cursor->predictable[p].sequence = reader_int32(&pseq);
      qa_save_reader_destroy(&pseq);
      qa_save_reader porder = field(&predictable_entry, "order");
      int64_t order_value = (int64_t)qa_save_reader_integer_min(&porder, 0);
      bool seen = false;
      for (size_t o = 0; o < order_count; o++) if (orders[o] == order_value) seen = true;
      if (order_value >= next_order || seen)
        qa_save_reader_fail(&porder, "invalid player event publication order");
      orders = realloc(orders, (order_count + 1) * sizeof *orders);
      if (orders == NULL) qa_range_error("QVM memory allocation failed");
      orders[order_count++] = order_value;
      cursor->predictable[p].order = order_value;
      qa_save_reader_destroy(&porder);
      qa_save_reader_destroy(&predictable_entry);
    }
    qa_save_reader_destroy(&predictable_reader);
    bool invalid = predictable_count > 2;
    for (size_t p = 0; p < predictable_count; p++) {
      for (size_t q = p + 1; q < predictable_count; q++)
        if (cursor->predictable[p].sequence == cursor->predictable[q].sequence) invalid = true;
      int32_t delta = (int32_t)((uint32_t)cursor->observed_sequence - (uint32_t)cursor->predictable[p].sequence);
      if (delta < 1 || delta > 2) invalid = true;
    }
    if (invalid) qa_save_reader_fail(&entry_reader, "invalid predictable player event cursor");
    qa_save_reader external_reader = field(&entry_reader, "external");
    cursor->external = reader_int32(&external_reader);
    qa_save_reader_destroy(&external_reader);
    qa_save_reader external_time_reader = field(&entry_reader, "externalTime");
    cursor->external_time = reader_int32(&external_time_reader);
    qa_save_reader_destroy(&external_time_reader);
    qa_save_reader external_order_reader = field(&entry_reader, "externalOrder");
    if (qa_save_reader_is_null(&external_order_reader)) {
      cursor->has_external_order = false;
    } else {
      int64_t order_value = (int64_t)qa_save_reader_integer_min(&external_order_reader, 0);
      bool seen = false;
      for (size_t o = 0; o < order_count; o++) if (orders[o] == order_value) seen = true;
      if (order_value >= next_order || seen)
        qa_save_reader_fail(&external_order_reader, "invalid player event publication order");
      orders = realloc(orders, (order_count + 1) * sizeof *orders);
      if (orders == NULL) qa_range_error("QVM memory allocation failed");
      orders[order_count++] = order_value;
      cursor->has_external_order = true;
      cursor->external_order = order_value;
    }
    qa_save_reader_destroy(&external_order_reader);
    qa_save_reader_destroy(&entry_reader);
  }
  qa_save_reader_destroy(&clients_reader);
  free(orders);
  return true;
}

void qa_qvm_player_events_checkpoint_free(qa_qvm_player_events_checkpoint *checkpoint) {
  for (size_t i = 0; i < checkpoint->client_count; i++) free(checkpoint->clients[i].predictable);
  free(checkpoint->clients);
  memset(checkpoint, 0, sizeof *checkpoint);
}

typedef struct cursor {
  int32_t external;
  int32_t external_time;
  int32_t sequence;
  int32_t observed_sequence;
  bool has_external_order;
  int64_t external_order;
  qa_qvm_predictable_event predictable[4];
  size_t predictable_count;
} cursor;

typedef struct observe_state {
  qa_qvm_mod_player_events *events;
  qa_actor_id actor;
} observe_state;

typedef struct entry {
  cursor cursor;
  qa_actor_id actor;
  int64_t address;
  size_t watch_id;
  observe_state *observe; /* owned, freed with the entry */
} entry;

static int64_t ordinal(qa_qvm_mod_player_events *events) {
  if (events->next_order >= 9007199254740991LL)
    qa_range_error("Player event publication sequence exhausted");
  return events->next_order++;
}

static void predictable_set(cursor *value, int32_t sequence, int64_t order) {
  for (size_t i = 0; i < value->predictable_count; i++) {
    if (value->predictable[i].sequence == sequence) {
      value->predictable[i].order = order;
      return;
    }
  }
  if (value->predictable_count < 4)
    value->predictable[value->predictable_count++] = (qa_qvm_predictable_event){sequence, order};
}

static bool predictable_get(const cursor *value, int32_t sequence, int64_t *out) {
  for (size_t i = 0; i < value->predictable_count; i++) {
    if (value->predictable[i].sequence == sequence) {
      *out = value->predictable[i].order;
      return true;
    }
  }
  return false;
}

static void observe_write(void *ctx, const qa_qvm_committed_write *event) {
  observe_state *state = ctx;
  qa_qvm_mod_player_events *events = state->events;
  entry *current = NULL;
  {
    entry *all = events->entries;
    for (size_t i = 0; i < events->entry_count; i++)
      if (qa_same_actor(all[i].actor, state->actor)) current = &all[i];
  }
  if (current == NULL) return;
  int64_t address = current->address;
  bool touches_sequence = false;
  bool touches_external = false;
  for (size_t i = 0; i < event->range_count; i++) {
    int64_t start = event->ranges[i].byte_offset;
    int64_t end = start + (int64_t)event->ranges[i].length;
    if (start < address + 108 + 4 && address + 108 < end) touches_sequence = true;
    if (start < address + 128 + 4 && address + 128 < end) touches_external = true;
  }
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(events->operations.memory, address,
                          qa_qvm_player_state_bytes(events->operations.abi_profile), &view);
  if (touches_sequence) {
    int32_t next = qa_qvm_writable_view_get_int32(&view, 108, true);
    int32_t delta = (int32_t)((uint32_t)next - (uint32_t)current->cursor.observed_sequence);
    if (delta < 0) {
      current->cursor.sequence = next;
      current->cursor.predictable_count = 0;
    } else if (delta > 0) {
      size_t kept = 0;
      for (size_t i = 0; i < current->cursor.predictable_count; i++) {
        int32_t age = (int32_t)((uint32_t)next - (uint32_t)current->cursor.predictable[i].sequence);
        if (age <= 2) current->cursor.predictable[kept++] = current->cursor.predictable[i];
      }
      current->cursor.predictable_count = kept;
      int32_t steps = delta < 2 ? delta : 2;
      for (int32_t step = steps; step > 0; step--)
        predictable_set(&current->cursor, (int32_t)((uint32_t)next - (uint32_t)step), ordinal(events));
    }
    current->cursor.observed_sequence = next;
  }
  if (touches_external) {
    current->cursor.has_external_order = true;
    current->cursor.external_order = ordinal(events);
  }
}

void qa_qvm_mod_player_events_init(qa_qvm_mod_player_events *events,
                                   const qa_qvm_mod_player_events_operations *operations) {
  memset(events, 0, sizeof *events);
  events->operations = *operations;
}

void qa_qvm_mod_player_events_destroy(qa_qvm_mod_player_events *events) {
  entry *entries = events->entries;
  for (size_t i = 0; i < events->entry_count; i++) free(entries[i].observe);
  free(events->entries);
  memset(events, 0, sizeof *events);
}

void qa_qvm_mod_player_events_track(qa_qvm_mod_player_events *events, qa_actor_id actor, int64_t address,
                                    const qa_qvm_saved_event_cursor *saved) {
  entry *entries = events->entries;
  for (size_t i = 0; i < events->entry_count; i++)
    if (qa_same_actor(entries[i].actor, actor)) return;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(events->operations.memory, address,
                          qa_qvm_player_state_bytes(events->operations.abi_profile), &view);
  int32_t sequence = qa_qvm_writable_view_get_int32(&view, 108, true);
  if (saved != NULL && saved->observed_sequence != sequence)
    qa_range_error("Player event cursor differs from restored source sequence");
  if (events->entry_count == events->entry_capacity) {
    events->entry_capacity = events->entry_capacity == 0 ? 8 : events->entry_capacity * 2;
    events->entries = realloc(events->entries, events->entry_capacity * sizeof(entry));
    if (events->entries == NULL) qa_range_error("QVM memory allocation failed");
    entries = events->entries;
  }
  entry *created = &entries[events->entry_count++];
  memset(created, 0, sizeof *created);
  created->actor = actor;
  created->address = address;
  if (saved == NULL) {
    created->cursor.external = qa_qvm_writable_view_get_int32(&view, 128, true);
    created->cursor.external_time = qa_qvm_writable_view_get_int32(&view, 136, true);
    created->cursor.sequence = sequence;
    created->cursor.observed_sequence = sequence;
  } else {
    created->cursor.external = saved->external;
    created->cursor.external_time = saved->external_time;
    created->cursor.sequence = saved->sequence;
    created->cursor.observed_sequence = saved->observed_sequence;
    created->cursor.has_external_order = saved->has_external_order;
    created->cursor.external_order = saved->external_order;
    for (size_t i = 0; i < saved->predictable_count && i < 4; i++)
      created->cursor.predictable[created->cursor.predictable_count++] = saved->predictable[i];
  }
  observe_state *state = xmalloc(sizeof *state);
  state->events = events;
  state->actor = actor;
  created->observe = state;
  qa_qvm_write_range range = {address + 108, 32};
  created->watch_id = qa_qvm_memory_observe_writes(events->operations.memory, &range, 1,
                                                   observe_write, state, NULL, NULL);
}

typedef struct pending {
  int64_t order;
  qa_app_q3_source_player_event event;
  double slots[64];
} pending;

static int compare_pending(const void *left, const void *right) {
  int64_t a = ((const pending *)left)->order;
  int64_t b = ((const pending *)right)->order;
  return a < b ? -1 : a > b ? 1 : 0;
}

void qa_qvm_mod_player_events_publish(qa_qvm_mod_player_events *events) {
  pending *list = NULL;
  size_t count = 0;
  entry *entries = events->entries;
  for (size_t e = 0; e < events->entry_count; e++) {
    entry *current = &entries[e];
    if (!events->operations.live(events->operations.ctx, current->actor)) continue;
    double storage[64];
    qa_q3_player_state ps;
    memset(&ps, 0, sizeof ps);
    ps.stats.values = storage;
    ps.persistent.values = storage + 16;
    ps.powerups.values = storage + 32;
    ps.ammo.values = storage + 48;
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(events->operations.memory, current->address,
                            qa_qvm_player_state_bytes(events->operations.abi_profile), &view);
    qa_qvm_read_source_player_state(&view, events->operations.abi_profile, &ps);
    bool external = ps.external_event != 0 &&
        ((int32_t)ps.external_event != current->cursor.external ||
         (int32_t)ps.external_event_time_milliseconds != current->cursor.external_time);
    int32_t delta = (int32_t)((uint32_t)(int32_t)ps.event_sequence - (uint32_t)current->cursor.sequence);
    int32_t event_count = delta < 0 ? 0 : delta > 2 ? 2 : delta;
    if (external || event_count != 0) {
      qa_vec3 origin = events->operations.origin(events->operations.ctx, current->actor);
      double time = events->operations.time(events->operations.ctx);
      qa_app_q3_source_player_event base;
      memset(&base, 0, sizeof base);
      base.kind = "player-event";
      base.actor = current->actor;
      base.source_module = events->operations.module;
      base.source_abi_profile = events->operations.abi_profile;
      base.origin = origin;
      base.time = time;
      if (external) {
        list = realloc(list, (count + 1) * sizeof *list);
        if (list == NULL) qa_range_error("QVM memory allocation failed");
        pending *next = &list[count++];
        memcpy(next->slots, storage, sizeof next->slots);
        next->event = base;
        next->event.player_state = ps;
        next->event.player_state.stats.values = next->slots;
        next->event.player_state.persistent.values = next->slots + 16;
        next->event.player_state.powerups.values = next->slots + 32;
        next->event.player_state.ammo.values = next->slots + 48;
        next->order = current->cursor.has_external_order ? current->cursor.external_order : ordinal(events);
        next->event.event = ps.external_event;
        next->event.parameter = ps.external_event_parameter;
        next->event.sequence_is_predictable = false;
        next->event.sequence_time = ps.external_event_time_milliseconds;
      }
      for (int32_t step = event_count; step > 0; step--) {
        int32_t sequence = (int32_t)((uint32_t)(int32_t)ps.event_sequence - (uint32_t)step);
        int slot = sequence & 1;
        int32_t event_value = (int32_t)ps.events[slot];
        if (event_value != 0) {
          list = realloc(list, (count + 1) * sizeof *list);
          if (list == NULL) qa_range_error("QVM memory allocation failed");
          pending *next = &list[count++];
          memcpy(next->slots, storage, sizeof next->slots);
          next->event = base;
          next->event.player_state = ps;
          next->event.player_state.stats.values = next->slots;
          next->event.player_state.persistent.values = next->slots + 16;
          next->event.player_state.powerups.values = next->slots + 32;
          next->event.player_state.ammo.values = next->slots + 48;
          int64_t known = 0;
          next->order = predictable_get(&current->cursor, sequence, &known) ? known : ordinal(events);
          next->event.event = event_value;
          next->event.parameter = ps.event_parameters[slot];
          next->event.sequence_is_predictable = true;
          next->event.sequence_number = sequence;
        }
      }
    }
    current->cursor.external = (int32_t)ps.external_event;
    current->cursor.external_time = (int32_t)ps.external_event_time_milliseconds;
    current->cursor.sequence = (int32_t)ps.event_sequence;
    current->cursor.observed_sequence = (int32_t)ps.event_sequence;
    current->cursor.has_external_order = false;
    current->cursor.predictable_count = 0;
  }
  qsort(list, count, sizeof *list, compare_pending);
  /* The player-state views point into each element's own slots, so
   * rebind them after the realloc growth above and the qsort moves. */
  for (size_t i = 0; i < count; i++) {
    list[i].event.player_state.stats.values = list[i].slots;
    list[i].event.player_state.persistent.values = list[i].slots + 16;
    list[i].event.player_state.powerups.values = list[i].slots + 32;
    list[i].event.player_state.ammo.values = list[i].slots + 48;
  }
  for (size_t i = 0; i < count; i++) {
    bool tracked = false;
    entry *current_entries = events->entries;
    for (size_t e = 0; e < events->entry_count; e++)
      if (qa_same_actor(current_entries[e].actor, list[i].event.actor)) tracked = true;
    if (tracked && events->operations.live(events->operations.ctx, list[i].event.actor))
      events->operations.emit(events->operations.ctx, &list[i].event);
  }
  free(list);
}

void qa_qvm_mod_player_events_discard(qa_qvm_mod_player_events *events) {
  entry *entries = events->entries;
  for (size_t i = 0; i < events->entry_count; i++) {
    qa_qvm_writable_view view;
    qa_qvm_memory_data_view(events->operations.memory, entries[i].address,
                            qa_qvm_player_state_bytes(events->operations.abi_profile), &view);
    entries[i].cursor.external = qa_qvm_writable_view_get_int32(&view, 128, true);
    entries[i].cursor.external_time = qa_qvm_writable_view_get_int32(&view, 136, true);
    entries[i].cursor.sequence = qa_qvm_writable_view_get_int32(&view, 108, true);
    entries[i].cursor.observed_sequence = entries[i].cursor.sequence;
    entries[i].cursor.has_external_order = false;
    entries[i].cursor.predictable_count = 0;
  }
}

void qa_qvm_mod_player_events_checkpoint(qa_qvm_mod_player_events *events, qa_qvm_player_events_checkpoint *out) {
  memset(out, 0, sizeof *out);
  out->next_order = events->next_order;
  out->clients = xmalloc(events->entry_count * sizeof *out->clients);
  out->client_count = events->entry_count;
  entry *entries = events->entries;
  for (size_t i = 0; i < events->entry_count; i++) {
    qa_qvm_saved_event_cursor *saved = &out->clients[i];
    memset(saved, 0, sizeof *saved);
    saved->actor = qa_saved_actor_id_of(entries[i].actor);
    saved->external = entries[i].cursor.external;
    saved->external_time = entries[i].cursor.external_time;
    saved->sequence = entries[i].cursor.sequence;
    saved->observed_sequence = entries[i].cursor.observed_sequence;
    saved->has_external_order = entries[i].cursor.has_external_order;
    saved->external_order = entries[i].cursor.external_order;
    saved->predictable = xmalloc(entries[i].cursor.predictable_count * sizeof *saved->predictable);
    saved->predictable_count = entries[i].cursor.predictable_count;
    memcpy(saved->predictable, entries[i].cursor.predictable,
           saved->predictable_count * sizeof *saved->predictable);
  }
}

void qa_qvm_mod_player_events_restore(qa_qvm_mod_player_events *events,
                                      const qa_qvm_player_events_checkpoint *saved,
                                      const qa_qvm_player_address *players, size_t player_count) {
  qa_qvm_mod_player_events_close(events);
  events->next_order = saved != NULL ? saved->next_order : 0;
  for (size_t i = 0; i < player_count; i++) {
    const qa_qvm_saved_event_cursor *cursor = NULL;
    if (saved != NULL) {
      for (size_t j = 0; j < saved->client_count; j++) {
        qa_actor_id referenced = events->operations.reference_saved(events->operations.ctx, saved->clients[j].actor);
        if (qa_same_actor(referenced, players[i].actor)) {
          cursor = &saved->clients[j]; /* TS Array.find: first match */
          break;
        }
      }
      if (cursor == NULL) qa_range_error("Missing restored player event cursor");
    }
    qa_qvm_mod_player_events_track(events, players[i].actor, players[i].address, cursor);
  }
}

void qa_qvm_mod_player_events_release(qa_qvm_mod_player_events *events, qa_actor_id actor) {
  entry *entries = events->entries;
  for (size_t i = 0; i < events->entry_count; i++) {
    if (!qa_same_actor(entries[i].actor, actor)) continue;
    qa_qvm_memory_writes_unobserve(&events->operations.memory->writes, entries[i].watch_id);
    free(entries[i].observe);
    memmove(&entries[i], &entries[i + 1], (events->entry_count - i - 1) * sizeof *entries);
    events->entry_count--;
    return;
  }
}

void qa_qvm_mod_player_events_close(qa_qvm_mod_player_events *events) {
  entry *entries = events->entries;
  for (size_t i = 0; i < events->entry_count; i++) {
    qa_qvm_memory_writes_unobserve(&events->operations.memory->writes, entries[i].watch_id);
    free(entries[i].observe);
    entries[i].observe = NULL;
  }
  events->entry_count = 0;
}
