/* Port of src/world/session/mod-client-outputs.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_client_outputs.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

/* TS `throw new Error(...)`: a catchable raise (aborts only uncaught). */
static noreturn void qa_errorf(const char *format, ...) {
  char message[2048];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static bool actor_eq(qa_actor_id a, qa_actor_id b) {
  return qa_same_actor(a, b);
}

struct output_value {
  qa_actor_id actor;
  qa_mod_client_output value;
};
struct channel_entry {
  bool occupied;
  /* TS Entry identity: each claim makes a new entry object. */
  uint64_t generation;
  qa_provider_id owner;
  struct output_value *values;
  size_t count;
  size_t capacity;
};
struct qa_mod_client_outputs {
  qa_mod_client_live_fn live;
  void *live_ctx;
  struct channel_entry channels[4];
  uint64_t next_generation;
  /* Every lease claimed, freed at destroy: a holder may call a closed
   * lease (TS: publish throws retired, close is a no-op). */
  struct lease_state **leases;
  size_t lease_count, lease_capacity;
  /* Scratch backing read() pointers until the next read. */
  qa_vec3 scratch_view;
  qa_bounds scratch_body;
};

struct lease_state {
  qa_mod_client_outputs *outputs;
  qa_mod_client_output_channel *channels;
  uint64_t *generations; /* the entry this lease created, per channel */
  size_t channel_count;
  bool active;
};
/* TS this.owners.get(channel) === entry */
static bool lease_owns(const struct lease_state *lease, size_t i) {
  const struct channel_entry *entry = &lease->outputs->channels[lease->channels[i]];
  return entry->occupied && entry->generation == lease->generations[i];
}

static qa_vec3 checked_vector(qa_vec3 value) {
  if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z))
    qa_range_error("Client output vector must be finite");
  return value;
}
static qa_mod_client_output detached(qa_mod_client_output output) {
  switch (output.kind) {
    case QA_MOD_CLIENT_OUTPUT_BODY_SHAPE: {
      qa_vec3 min = checked_vector(output.body_shape.min);
      qa_vec3 max = checked_vector(output.body_shape.max);
      if (min.x > max.x || min.y > max.y || min.z > max.z)
        qa_range_error("Client body output has backwards bounds");
      output.body_shape.min = min;
      output.body_shape.max = max;
      return output;
    }
    case QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET:
      output.view_offset = checked_vector(output.view_offset);
      return output;
    case QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE:
    case QA_MOD_CLIENT_OUTPUT_STANCE:
      return output;
  }
  return output;
}

static void entry_set(struct channel_entry *entry, qa_actor_id actor,
                      qa_mod_client_output value) {
  for (size_t i = 0; i < entry->count; i++)
    if (actor_eq(entry->values[i].actor, actor)) {
      entry->values[i].value = value;
      return;
    }
  if (entry->count == entry->capacity) {
    entry->capacity = entry->capacity != 0 ? entry->capacity * 2 : 8;
    entry->values =
        xrealloc(entry->values, entry->capacity * sizeof *entry->values);
  }
  entry->values[entry->count].actor = actor;
  entry->values[entry->count].value = value;
  entry->count++;
}
static void entry_delete(struct channel_entry *entry, qa_actor_id actor) {
  for (size_t i = 0; i < entry->count; i++)
    if (actor_eq(entry->values[i].actor, actor)) {
      memmove(&entry->values[i], &entry->values[i + 1],
              (entry->count - i - 1) * sizeof *entry->values);
      entry->count--;
      return;
    }
}
static const qa_mod_client_output *entry_get(
    const struct channel_entry *entry, qa_actor_id actor) {
  if (!entry->occupied) return NULL;
  for (size_t i = 0; i < entry->count; i++)
    if (actor_eq(entry->values[i].actor, actor))
      return &entry->values[i].value;
  return NULL;
}

static void lease_current(const struct lease_state *lease) {
  if (!lease->active) qa_errorf("Component client output owner is retired");
  for (size_t i = 0; i < lease->channel_count; i++)
    if (!lease_owns(lease, i))
      qa_errorf("Component client output owner is retired");
}
static void lease_publish(void *ctx, qa_actor_id actor,
                          const qa_mod_client_output *outputs,
                          size_t count) {
  struct lease_state *lease = ctx;
  lease_current(lease);
  if (!lease->outputs->live(lease->outputs->live_ctx, actor))
    qa_errorf("Component client output requires the current client actor");
  bool mismatch = count != lease->channel_count;
  for (size_t i = 0; !mismatch && i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (outputs[i].kind == outputs[j].kind) mismatch = true;
  if (mismatch)
    qa_errorf("Client output publication differs from its declared channels");
  /* Validate every output before storing any (TS maps then sets). */
  for (size_t i = 0; i < count; i++) {
    bool declared = false;
    for (size_t j = 0; j < lease->channel_count; j++)
      if (lease->channels[j] == outputs[i].kind) declared = true;
    if (!declared) qa_errorf("Undeclared component client output");
    (void)detached(outputs[i]);
  }
  for (size_t i = 0; i < count; i++)
    entry_set(&lease->outputs->channels[outputs[i].kind], actor,
              detached(outputs[i]));
}
static void lease_release(void *ctx, qa_actor_id actor) {
  struct lease_state *lease = ctx;
  /* A retired lease's entry is detached in TS: deleting from it changes
   * nothing a reader can see. */
  for (size_t i = 0; i < lease->channel_count; i++)
    if (lease_owns(lease, i))
      entry_delete(&lease->outputs->channels[lease->channels[i]], actor);
}
static void lease_close(void *ctx) {
  struct lease_state *lease = ctx;
  if (!lease->active) return;
  lease->active = false;
  for (size_t i = 0; i < lease->channel_count; i++) {
    if (!lease_owns(lease, i)) continue;
    struct channel_entry *entry =
        &lease->outputs->channels[lease->channels[i]];
    entry->count = 0;
    entry->occupied = false;
    entry->owner = NULL;
  }
}

qa_mod_client_outputs *qa_mod_client_outputs_create(
    qa_mod_client_live_fn live, void *live_ctx) {
  qa_mod_client_outputs *outputs = calloc(1, sizeof *outputs);
  if (outputs == NULL) return NULL;
  outputs->live = live;
  outputs->live_ctx = live_ctx;
  return outputs;
}
void qa_mod_client_outputs_destroy(qa_mod_client_outputs *outputs) {
  if (outputs == NULL) return;
  qa_mod_client_outputs_close(outputs);
  for (size_t i = 0; i < 4; i++) free(outputs->channels[i].values);
  for (size_t i = 0; i < outputs->lease_count; i++) {
    free(outputs->leases[i]->channels);
    free(outputs->leases[i]->generations);
    free(outputs->leases[i]);
  }
  free(outputs->leases);
  free(outputs);
}

qa_mod_client_output_lease qa_mod_client_outputs_claim(
    qa_mod_client_outputs *outputs, qa_provider_id owner,
    const qa_mod_client_output_channel *channels, size_t channel_count) {
  for (size_t i = 0; i < channel_count; i++)
    for (size_t j = i + 1; j < channel_count; j++)
      if (channels[i] == channels[j])
        qa_errorf("Duplicate component client output channel");
  static const char *const names[4] = {"view-offset", "movement-mode",
                                       "stance", "body-shape"};
  for (size_t i = 0; i < channel_count; i++) {
    const struct channel_entry *previous = &outputs->channels[channels[i]];
    if (previous->occupied)
      qa_errorf("Client %s is already owned by %s; %s cannot also claim it",
                names[channels[i]], previous->owner, owner);
  }
  struct lease_state *lease = malloc(sizeof *lease);
  if (lease == NULL) abort();
  lease->outputs = outputs;
  lease->channel_count = channel_count;
  lease->channels = NULL;
  lease->generations = NULL;
  lease->active = true;
  if (channel_count != 0) {
    lease->channels = malloc(channel_count * sizeof *lease->channels);
    lease->generations = malloc(channel_count * sizeof *lease->generations);
    if (lease->channels == NULL || lease->generations == NULL) abort();
    memcpy(lease->channels, channels,
           channel_count * sizeof *lease->channels);
  }
  for (size_t i = 0; i < channel_count; i++) {
    outputs->channels[channels[i]].occupied = true;
    outputs->channels[channels[i]].generation = ++outputs->next_generation;
    outputs->channels[channels[i]].owner = owner;
    outputs->channels[channels[i]].count = 0;
    lease->generations[i] = outputs->channels[channels[i]].generation;
  }
  if (outputs->lease_count == outputs->lease_capacity) {
    outputs->lease_capacity = outputs->lease_capacity != 0 ? outputs->lease_capacity * 2 : 4;
    outputs->leases = xrealloc(outputs->leases, outputs->lease_capacity * sizeof *outputs->leases);
  }
  outputs->leases[outputs->lease_count++] = lease;
  qa_mod_client_output_lease result = {lease, lease_publish, lease_release,
                                       lease_close};
  return result;
}

bool qa_mod_client_outputs_read(qa_mod_client_outputs *outputs,
                                qa_actor_id actor,
                                qa_mod_client_movement_outputs *out) {
  bool any = false;
  for (size_t i = 0; i < 4; i++)
    if (outputs->channels[i].occupied) any = true;
  if (!any) return false;
  const qa_mod_client_output *view =
      entry_get(&outputs->channels[QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET], actor);
  const qa_mod_client_output *mode =
      entry_get(&outputs->channels[QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE],
                actor);
  const qa_mod_client_output *stance =
      entry_get(&outputs->channels[QA_MOD_CLIENT_OUTPUT_STANCE], actor);
  const qa_mod_client_output *body =
      entry_get(&outputs->channels[QA_MOD_CLIENT_OUTPUT_BODY_SHAPE], actor);
  if (view == NULL && mode == NULL && stance == NULL && body == NULL)
    return false;
  if (!outputs->live(outputs->live_ctx, actor)) return false;
  memset(out, 0, sizeof *out);
  if (view != NULL) {
    outputs->scratch_view = view->view_offset;
    out->view_offset = &outputs->scratch_view;
  }
  if (mode != NULL) {
    out->has_mode = true;
    out->mode = mode->mode;
  }
  if (stance != NULL) {
    out->has_stance = true;
    out->stance = stance->stance;
  }
  if (body != NULL) {
    outputs->scratch_body = body->body_shape;
    out->body_bounds = &outputs->scratch_body;
  }
  return true;
}

void qa_mod_client_outputs_release(qa_mod_client_outputs *outputs,
                                   qa_actor_id actor) {
  for (size_t i = 0; i < 4; i++)
    if (outputs->channels[i].occupied)
      entry_delete(&outputs->channels[i], actor);
}
void qa_mod_client_outputs_close(qa_mod_client_outputs *outputs) {
  for (size_t i = 0; i < 4; i++) {
    outputs->channels[i].count = 0;
    outputs->channels[i].occupied = false;
    outputs->channels[i].owner = NULL;
  }
}

void qa_read_mod_client_outputs(
    const qa_mod_client_output_declaration *declarations, size_t count,
    qa_client_output_scalar_fn scalar, qa_client_output_vector_fn vector,
    void *read_ctx, qa_mod_client_output *out) {
  for (size_t i = 0; i < count; i++) {
    const qa_mod_client_output_declaration *declaration = &declarations[i];
    memset(&out[i], 0, sizeof out[i]);
    out[i].kind = declaration->kind;
    if (declaration->kind == QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) {
      out[i].body_shape.min = vector(read_ctx, declaration->min_vector);
      out[i].body_shape.max = vector(read_ctx, declaration->max_vector);
      continue;
    }
    if (declaration->kind == QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET) {
      out[i].view_offset =
          declaration->height != NULL
              ? (qa_vec3){0, 0, scalar(read_ctx, declaration->height)}
              : vector(read_ctx, declaration->field);
      continue;
    }
    double raw = scalar(read_ctx, declaration->field);
    if (!isfinite(raw) ||
        (declaration->has_mask &&
         (trunc(raw) != raw || raw < -2147483648.0 || raw > 4294967295.0)))
      qa_errorf(
          "Client output requires a finite source value, integral when "
          "masked");
    /* (raw & mask) >>> 0 */
    double value =
        !declaration->has_mask
            ? raw
            : (double)(qa_js_to_uint32(raw) & qa_js_to_uint32(declaration->mask));
    if (declaration->kind == QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE) {
      bool found = false;
      for (size_t j = 0; j < declaration->mode_value_count; j++)
        if (declaration->mode_values[j].value == value) {
          out[i].mode = declaration->mode_values[j].mode;
          found = true;
          break; /* Array.find: the first match */
        }
      if (!found) qa_errorf("Undeclared source movement mode %s", QA_JS_NUMBER(value));
    } else {
      bool found = false;
      for (size_t j = 0; j < declaration->stance_value_count; j++)
        if (declaration->stance_values[j].value == value) {
          out[i].stance = declaration->stance_values[j].crouched;
          found = true;
          break; /* Array.find: the first match */
        }
      if (!found) qa_errorf("Undeclared source client stance %s", QA_JS_NUMBER(value));
    }
  }
}

void qa_validate_mod_client_outputs(
    const qa_mod_client_output_declaration *declarations, size_t count,
    qa_client_output_check_fn scalar, qa_client_output_check_fn vector,
    void *check_ctx) {
  for (size_t i = 0; i < count; i++)
    for (size_t j = i + 1; j < count; j++)
      if (declarations[i].kind == declarations[j].kind)
        qa_errorf("Duplicate source client output channel");
  for (size_t i = 0; i < count; i++) {
    const qa_mod_client_output_declaration *declaration = &declarations[i];
    if (declaration->kind == QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) {
      vector(check_ctx, declaration->min_vector);
      vector(check_ctx, declaration->max_vector);
      continue;
    }
    if (declaration->kind == QA_MOD_CLIENT_OUTPUT_VIEW_OFFSET) {
      if (declaration->height != NULL)
        scalar(check_ctx, declaration->height);
      else
        vector(check_ctx, declaration->field);
      continue;
    }
    scalar(check_ctx, declaration->field);
    if (declaration->has_mask &&
        (trunc(declaration->mask) != declaration->mask ||
         declaration->mask <= 0 || declaration->mask > 4294967295.0))
      qa_errorf("Invalid source client output mask");
    bool mode = declaration->kind == QA_MOD_CLIENT_OUTPUT_MOVEMENT_MODE;
    size_t value_count =
        mode ? declaration->mode_value_count : declaration->stance_value_count;
    if (declaration->has_mask) {
      for (size_t j = 0; j < value_count; j++) {
        double entry = mode ? declaration->mode_values[j].value
                            : declaration->stance_values[j].value;
        if (trunc(entry) != entry || entry < 0 || entry > 4294967295.0 ||
            (double)((uint32_t)entry & (uint32_t)declaration->mask) != entry)
          qa_errorf("Source output values escape their declared mask");
      }
    }
    if (value_count == 0) qa_errorf("Ambiguous source client output values");
    for (size_t j = 0; j < value_count; j++) {
      double entry = mode ? declaration->mode_values[j].value
                          : declaration->stance_values[j].value;
      if (!isfinite(entry)) qa_errorf("Ambiguous source client output values");
      for (size_t k = j + 1; k < value_count; k++) {
        double other = mode ? declaration->mode_values[k].value
                            : declaration->stance_values[k].value;
        if (other == entry) qa_errorf("Ambiguous source client output values");
      }
    }
  }
}

struct qa_source_mod_client_outputs {
  qa_provider_id owner;
  const qa_mod_client_output_declaration *declarations;
  size_t count;
  bool has_claim;
  qa_client_output_claim_fn claim;
  void *claim_ctx;
  qa_source_output_scalar_fn scalar;
  qa_source_output_vector_fn vector;
  void *read_ctx;
  bool has_lease;
  qa_mod_client_output_lease lease;
  qa_actor_id *actors;
  size_t actor_count;
  size_t actor_capacity;
};

qa_source_mod_client_outputs *qa_source_mod_client_outputs_create(
    qa_provider_id owner,
    const qa_mod_client_output_declaration *declarations, size_t count,
    bool has_claim, qa_client_output_claim_fn claim, void *claim_ctx,
    qa_source_output_scalar_fn scalar, qa_source_output_vector_fn vector,
    void *read_ctx) {
  if (count != 0 && !has_claim)
    qa_errorf("Declared client outputs require a destination output owner");
  qa_source_mod_client_outputs *outputs = calloc(1, sizeof *outputs);
  if (outputs == NULL) return NULL;
  outputs->owner = owner;
  outputs->declarations = declarations;
  outputs->count = count;
  outputs->has_claim = has_claim;
  outputs->claim = claim;
  outputs->claim_ctx = claim_ctx;
  outputs->scalar = scalar;
  outputs->vector = vector;
  outputs->read_ctx = read_ctx;
  return outputs;
}
void qa_source_mod_client_outputs_destroy(
    qa_source_mod_client_outputs *outputs) {
  if (outputs == NULL) return;
  qa_source_mod_client_outputs_close(outputs);
  free(outputs->actors);
  free(outputs);
}
bool qa_source_mod_client_outputs_enabled(
    const qa_source_mod_client_outputs *outputs) {
  return outputs->count != 0;
}
bool qa_source_mod_client_outputs_has(
    const qa_source_mod_client_outputs *outputs, qa_actor_id actor) {
  for (size_t i = 0; i < outputs->actor_count; i++)
    if (actor_eq(outputs->actors[i], actor)) return true;
  return false;
}

struct read_adapter {
  qa_source_mod_client_outputs *outputs;
  qa_actor_id actor;
};
static double adapt_scalar(void *ctx, const void *field) {
  struct read_adapter *adapter = ctx;
  return adapter->outputs->scalar(adapter->outputs->read_ctx, adapter->actor,
                                  field);
}
static qa_vec3 adapt_vector(void *ctx, const void *field) {
  struct read_adapter *adapter = ctx;
  return adapter->outputs->vector(adapter->outputs->read_ctx, adapter->actor,
                                  field);
}

static void free_values(void *arg) { free(*(qa_mod_client_output **)arg); }
static void free_channels(void *arg) { free(*(qa_mod_client_output_channel **)arg); }
void qa_source_mod_client_outputs_publish(
    qa_source_mod_client_outputs *outputs, qa_actor_id actor) {
  if (outputs->count == 0) return;
  qa_mod_client_output *values =
      malloc(outputs->count * sizeof *values);
  if (values == NULL) abort();
  struct read_adapter adapter = {outputs, actor};
  qa_cleanup node; /* a throwing read, claim or publish drops the values */
  qa_cleanup_push(&node, free_values, &values);
  qa_read_mod_client_outputs(outputs->declarations, outputs->count,
                             adapt_scalar, adapt_vector, &adapter, values);
  if (!outputs->has_lease) {
    if (!outputs->has_claim)
      qa_errorf("Client output admission is unavailable");
    qa_mod_client_output_channel *channels =
        malloc(outputs->count * sizeof *channels);
    if (channels == NULL) abort();
    for (size_t i = 0; i < outputs->count; i++)
      channels[i] = outputs->declarations[i].kind;
    qa_cleanup channels_node;
    qa_cleanup_push(&channels_node, free_channels, &channels);
    outputs->lease = outputs->claim(outputs->claim_ctx, outputs->owner,
                                    channels, outputs->count);
    qa_cleanup_pop(&channels_node);
    free(channels);
    outputs->has_lease = true;
  }
  outputs->lease.publish(outputs->lease.ctx, actor, values, outputs->count);
  qa_cleanup_pop(&node);
  free(values);
  if (!qa_source_mod_client_outputs_has(outputs, actor)) {
    if (outputs->actor_count == outputs->actor_capacity) {
      outputs->actor_capacity =
          outputs->actor_capacity != 0 ? outputs->actor_capacity * 2 : 8;
      outputs->actors = xrealloc(
          outputs->actors, outputs->actor_capacity * sizeof *outputs->actors);
    }
    outputs->actors[outputs->actor_count++] = actor;
  }
}
void qa_source_mod_client_outputs_release(
    qa_source_mod_client_outputs *outputs, qa_actor_id actor) {
  if (outputs->has_lease) outputs->lease.release(outputs->lease.ctx, actor);
  for (size_t i = 0; i < outputs->actor_count; i++)
    if (actor_eq(outputs->actors[i], actor)) {
      memmove(&outputs->actors[i], &outputs->actors[i + 1],
              (outputs->actor_count - i - 1) * sizeof *outputs->actors);
      outputs->actor_count--;
      break;
    }
}
void qa_source_mod_client_outputs_clear(
    qa_source_mod_client_outputs *outputs) {
  if (outputs->has_lease)
    for (size_t i = 0; i < outputs->actor_count; i++)
      outputs->lease.release(outputs->lease.ctx, outputs->actors[i]);
  outputs->actor_count = 0;
}
void qa_source_mod_client_outputs_close(
    qa_source_mod_client_outputs *outputs) {
  if (outputs->has_lease) {
    outputs->lease.close(outputs->lease.ctx);
    outputs->has_lease = false;
  }
  outputs->actor_count = 0;
}
