/* Port of src/world/actors/source-slots.ts. GPL-2.0-or-later. */
#include "qa/world/actors/source_slots.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static bool safe_integer(double value) {
  return value >= -9007199254740991.0 && value <= 9007199254740991.0 &&
         value == (double)(int64_t)value;
}

static bool quake_reusable(void *ctx, qa_source_time freed_at, qa_source_time now) {
  (void)ctx;
  if (freed_at.kind != QA_SOURCE_TIME_SECONDS || now.kind != QA_SOURCE_TIME_SECONDS)
    qa_range_error("Q1/Q2 classic edict lifetime uses seconds");
  return freed_at.value < 2 || now.value - freed_at.value > 0.5;
}
static bool q3_reusable(void *ctx, qa_source_time freed_at, qa_source_time now) {
  const qa_q3_lifetime_state *state = ctx;
  if (freed_at.kind != QA_SOURCE_TIME_MILLISECONDS || now.kind != QA_SOURCE_TIME_MILLISECONDS)
    qa_range_error("Q3 entity lifetime uses milliseconds");
  /* TS: (mapStart + 2000) | 0 and (now - freed) | 0 (int32 truncation). */
  int32_t threshold = qa_js_to_int32(state->map_start_milliseconds + 2000);
  int32_t elapsed = qa_js_to_int32(now.value - freed_at.value);
  return !(freed_at.value > (double)threshold && elapsed < 1000);
}

qa_actor_lifetime_policy qa_quake_edict_lifetime(double first_dynamic_slot,
                                                 const char *exhaustion) {
  qa_actor_lifetime_policy policy;
  policy.first_dynamic_slot = first_dynamic_slot;
  policy.reuse = "source-cooldown";
  policy.ctx = NULL;
  policy.reusable_after = quake_reusable;
  policy.clear = "source-edict";
  policy.exhaustion = exhaustion != NULL ? exhaustion : "fatal";
  return policy;
}
qa_actor_lifetime_policy qa_q3_entity_lifetime(qa_q3_lifetime_state *state,
                                               double first_dynamic_slot,
                                               double map_start_milliseconds) {
  state->map_start_milliseconds = map_start_milliseconds;
  qa_actor_lifetime_policy policy;
  policy.first_dynamic_slot = first_dynamic_slot;
  policy.reuse = "source-cooldown";
  policy.ctx = state;
  policy.reusable_after = q3_reusable;
  policy.clear = "source-edict"; /* ActorLifetimePolicy.clear has one TS value; "source-gentity" was dropped. */
  policy.exhaustion = "fatal";
  return policy;
}

struct qa_source_actor_slots {
  qa_session_actor_registry *actors;
  qa_source_actor_slots_options options;
};

static int64_t slot_count(qa_source_actor_slots *slots) {
  double count = slots->options.storage.count(slots->options.storage.ctx);
  if (!safe_integer(count) || count < slots->options.lifetime.first_dynamic_slot ||
      count > slots->options.capacity)
    qa_range_error("Invalid source actor high-water count");
  return (int64_t)count;
}

qa_source_actor_slots *qa_source_actor_slots_create(qa_session_actor_registry *actors,
                                                    qa_source_actor_slots_options options) {
  double first = options.lifetime.first_dynamic_slot;
  if (!safe_integer(first) || first < 0 || !safe_integer(options.capacity) ||
      options.capacity <= first)
    qa_range_error("Invalid source actor slot range");
  qa_source_actor_slots *slots = malloc(sizeof *slots);
  if (slots == NULL) return NULL;
  slots->actors = actors;
  slots->options = options;
  (void)slot_count(slots);
  return slots;
}
void qa_source_actor_slots_destroy(qa_source_actor_slots *slots) { free(slots); }

bool qa_source_actor_slots_at(qa_source_actor_slots *slots, int64_t slot, qa_owned_actor *out) {
  return qa_session_actor_registry_at_source(slots->actors, slots->options.provider, slot, out);
}

qa_owned_actor qa_source_actor_slots_bind_existing(qa_source_actor_slots *slots, int64_t slot,
                                                   const char *definition) {
  if (slot < 0 || slot >= slot_count(slots))
    qa_range_error("Source slot is outside the opened table");
  qa_owned_actor existing;
  if (qa_source_actor_slots_at(slots, slot, &existing)) return existing;
  return qa_session_actor_registry_allocate_at_source(slots->actors, slots->options.provider,
                                                      slot, definition);
}

qa_owned_actor qa_source_actor_slots_allocate(qa_source_actor_slots *slots,
                                              const char *definition) {
  qa_source_time now = slots->options.now(slots->options.ctx);
  int64_t count = slot_count(slots);
  int64_t selected = count;
  for (int64_t slot = (int64_t)slots->options.lifetime.first_dynamic_slot; slot < count; slot++) {
    qa_source_slot_state state = slots->options.storage.read(slots->options.storage.ctx, slot);
    if (state.free &&
        (strcmp(slots->options.lifetime.reuse, "immediate") == 0 ||
         slots->options.lifetime.reusable_after(slots->options.lifetime.ctx,
                                                 state.freed_at, now))) {
      selected = slot;
      break;
    }
  }
  if (selected == (int64_t)slots->options.capacity) {
    slots->options.exhausted(slots->options.ctx, selected);
    if (strcmp(slots->options.lifetime.exhaustion, "fatal") == 0)
      qa_range_error("%s: no free source actors", slots->options.provider);
    selected--;
    qa_owned_actor previous;
    if (qa_source_actor_slots_at(slots, selected, &previous)) {
      slots->options.unlink(slots->options.ctx, previous);
      qa_session_actor_registry_release(slots->actors, previous);
    }
  } else if (selected == count) {
    slots->options.storage.set_count(slots->options.storage.ctx, (double)(count + 1));
  }
  qa_owned_actor previous;
  if (qa_source_actor_slots_at(slots, selected, &previous))
    qa_session_actor_registry_release(slots->actors, previous);
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(
      slots->actors, slots->options.provider, selected, definition);
  slots->options.storage.initialize(slots->options.storage.ctx, selected, actor);
  return actor;
}

bool qa_source_actor_slots_free(qa_source_actor_slots *slots, qa_owned_actor actor) {
  qa_session_actor_registry_assert_owned(slots->actors, actor);
  qa_actor_source_result source = qa_session_actor_registry_source_of(slots->actors, actor.id);
  if (!source.has_value || strcmp(source.provider, slots->options.provider) != 0)
    qa_range_error("Actor is outside this source table");
  slots->options.unlink(slots->options.ctx, actor);
  if (!slots->options.storage.can_free(slots->options.storage.ctx, source.slot)) return false;
  slots->options.storage.clear_freed(slots->options.storage.ctx, source.slot,
                                     slots->options.now(slots->options.ctx));
  qa_session_actor_registry_release(slots->actors, actor);
  return true;
}
