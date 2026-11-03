/* Port of src/compat/q2/native-mod-provider.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_provider.h"
#include "qa/app/bootstrap/simulation/native_mod_presentation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_mod_actors.h"
#include "qa/compat/q2/native_mod_call_plan.h"
#include "qa/compat/q2/native_mod_client_stages.h"
#include "qa/compat/q2/native_mod_clients.h"
#include "qa/compat/q2/native_mod_invocations.h"
#include "qa/compat/q2/native_mod_items.h"
#include "qa/compat/q2/native_mod_protection.h"
#include "qa/compat/q2/native_mod_region.h"
#include "qa/compat/q2/rerelease/player_state.h"
#include "qa/contracts/native_mod_items.h"
#include "qa/contracts/native_mod_region.h"
#include "qa/core/commands/text.h"
#include "qa/persistence/execution.h"
#include "qa/persistence/q2_classic_guest.h"
#include "qa/persistence/save_image.h"
#include "qa/world/session/mod_client_input_values.h"
#include "qa/world/session/mod_client_outputs.h"
#include "qa/world/session/mod_objectives.h"
#include "qa/foundation/js.h"

typedef qa_compat_q2_native_mod_provider provider_t;
typedef qa_native_mod_declaration declaration_t;
typedef qa_native_mod_source_call call_t;
typedef qa_native_mod_actor_field field_t;
typedef qa_native_mod_actor_record record_t;
typedef qa_native_mod_value value_t;
typedef qa_compat_q2_native_pickup_input input_row;
typedef qa_compat_q2_native_pickup_inputs inputs_t;

static bool eq(const char *a, const char *b) { return qa_compat_q2_text_equal(a, b); }
static bool safe_integer(double v) { return isfinite(v) && v == trunc(v) && fabs(v) <= (double)QA_MAX_SAFE_INTEGER; }
static bool shared(const field_t *f) {
  static const char *const names[] = {"team", "score", "health", "inventory", "inventory-capacity", "origin", "velocity", "angles", "bounds-min", "bounds-max"};
  for (int i = 0; i < 10; i++) if (!strcmp(f->binding, names[i])) return true;
  return false;
}
static bool is_inventory(const field_t *f) { return !strcmp(f->binding, "inventory") || !strcmp(f->binding, "inventory-capacity"); }
static bool is_match(const field_t *f) { return !strcmp(f->binding, "team") || !strcmp(f->binding, "score"); }
static double scalar_size(const char *kind) { return qa_compat_q2_native_storage_bytes(kind, 4); }
static double size_of(const field_t *f, int64_t pointer_bytes) {
  if (!strcmp(f->binding, "private")) return f->qvm_field.byte_length;
  if (!strcmp(f->binding, "record") || !strcmp(f->binding, "address")) return (double)pointer_bytes;
  /* TS "encoding" in field: team|score|health|inventory*|constant. */
  if (f->encoding != NULL) return scalar_size(f->encoding);
  return 12;
}
/* TS scalar(value, kind): the GuestCallValue for a declared scalar. */
static qa_guest_call_value scalar_value(double value, const char *kind) {
  qa_guest_call_value v; memset(&v, 0, sizeof v);
  if (!isfinite(value)) qa_range_error("Native mod value exceeds %s", kind);
  if (!strcmp(kind, "float32")) {
    if (!isfinite((double)(float)value)) qa_range_error("Native mod value exceeds float32");
    v.kind = QA_GUEST_VALUE_FLOAT32; v.number_value = (double)(float)value; return v;
  }
  if (!strcmp(kind, "float64")) { v.kind = QA_GUEST_VALUE_FLOAT64; v.number_value = value; return v; }
  double integer = trunc(value);
  if (!safe_integer(integer) || (!strcmp(kind, "int32") && (integer < -2147483648.0 || integer > 2147483647.0))
      || (!strcmp(kind, "uint32") && (integer < 0 || integer > 4294967295.0)) || (!strcmp(kind, "uint64") && integer < 0)) qa_range_error("Native mod value exceeds %s", kind);
  if (!strcmp(kind, "int64")) { v.kind = QA_GUEST_VALUE_INT64; v.int64_value = (int64_t)integer; return v; }
  if (!strcmp(kind, "uint64")) { v.kind = QA_GUEST_VALUE_UINT64; v.uint64_value = (uint64_t)integer; return v; }
  if (!strcmp(kind, "int8") || !strcmp(kind, "int16")) {
    double maximum = !strcmp(kind, "int8") ? 127 : 32767;
    if (integer < -maximum - 1 || integer > maximum) qa_range_error("Native mod value exceeds %s", kind);
    v.kind = QA_GUEST_VALUE_INT32; v.number_value = integer; return v;
  }
  if (!strcmp(kind, "uint8") || !strcmp(kind, "uint16")) {
    if (integer < 0 || integer > (!strcmp(kind, "uint8") ? 255 : 65535)) qa_range_error("Native mod value exceeds %s", kind);
    v.kind = QA_GUEST_VALUE_UINT32; v.number_value = integer; return v;
  }
  v.kind = !strcmp(kind, "int32") ? QA_GUEST_VALUE_INT32 : QA_GUEST_VALUE_UINT32; v.number_value = integer; return v;
}
static double number_result(const qa_guest_call_result *result) {
  double value;
  switch (result->kind) {
    case QA_GUEST_VALUE_VOID: return 0;
    case QA_GUEST_VALUE_AGGREGATE: case QA_GUEST_VALUE_POINTER: qa_range_error("Native mod operation must return a scalar");
    case QA_GUEST_VALUE_INT64: value = (double)result->int64_value; break;
    case QA_GUEST_VALUE_UINT64: value = (double)result->uint64_value; break;
    default: value = result->number_value; break;
  }
  bool big = result->kind == QA_GUEST_VALUE_INT64 || result->kind == QA_GUEST_VALUE_UINT64;
  if (!isfinite(value) || (big && !safe_integer(value))) qa_range_error("Native callback result cannot be represented by the shared operation");
  return value;
}
/* TS layout(value).storage. */
static const char *value_storage(const value_t *value) {
  if (!strcmp(value->kind, "time")) return value->encoding;
  if (!strcmp(value->kind, "client")) return "int32";
  if (!strcmp(value->kind, "actor") || !strcmp(value->kind, "address") || !strcmp(value->kind, "vector") || !strcmp(value->kind, "string") || !strcmp(value->kind, "userinfo") || !strcmp(value->kind, "user-command")) return "pointer";
  return value->kind;
}
/* TS value.value.kind === "time" ? encoding : kind, for scalar lengths. */
static const char *value_scalar_kind(const value_t *value) { return !strcmp(value->kind, "time") ? value->encoding : value->kind; }
static const record_t *find_record(const declaration_t *d, const char *id) {
  for (size_t i = 0; i < d->actor_record_count; i++) if (eq(d->actor_records[i].id, id)) return &d->actor_records[i];
  return NULL;
}
static bool client_record(const declaration_t *d, const char *id) {
  if (d->clients == NULL) return false;
  for (size_t i = 0; i < d->clients->records.count; i++) if (eq(d->clients->records.values[i], id)) return true;
  return false;
}
typedef struct name_set { const char *const *names; size_t count; } name_set;
static bool in_names(name_set set, const char *name) { for (size_t i = 0; i < set.count; i++) if (!strcmp(set.names[i], name)) return true; return false; }
#define NAMES(...) ((name_set){(const char *const[]){__VA_ARGS__}, sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)})
static bool one_of(const char *name, name_set set) { return in_names(set, name); }

/* --- validateNativeModDeclaration. --- */
typedef struct output_check { const declaration_t *d; double length; } output_check;
static void output_field(const declaration_t *d, const char *record_id, double offset, double length) {
  const qa_native_mod_clients *clients = d->clients; const record_t *record = find_record(d, record_id);
  bool ok = clients != NULL && record != NULL && (client_record(d, record->id) || eq(record->id, d->entity_record)) && isfinite(offset) && offset == trunc(offset) && offset >= 0;
  if (ok) {
    bool declared = false;
    for (size_t i = 0; i < record->field_count && !declared; i++) {
      const field_t *value = &record->fields[i];
      declared = (!strcmp(value->binding, "private") && value->offset <= offset && offset + length <= value->offset + value->qvm_field.byte_length)
          || (length == 12 && (!strcmp(value->binding, "bounds-min") || !strcmp(value->binding, "bounds-max")) && value->offset == offset);
    }
    ok = declared;
  }
  if (ok) for (size_t i = 0; i < clients->input_field_count && ok; i++) {
    const qa_native_mod_client_input_field *value = &clients->input_fields[i];
    double bytes = !strcmp(value->value.kind, "vector") ? 12 : scalar_size(value_scalar_kind(&value->value));
    if (eq(value->record, record_id) && value->offset < offset + length && offset < value->offset + bytes) ok = false;
  }
  if (ok && clients->pose != NULL) {
    const qa_native_mod_armor_field *pose[2] = {&clients->pose->view_height, &clients->pose->crouched.field};
    for (int i = 0; i < 2 && ok; i++) if (eq(pose[i]->record, record_id) && pose[i]->field.offset < offset + length && offset < pose[i]->field.offset + scalar_size(pose[i]->field.encoding)) ok = false;
  }
  if (!ok) qa_range_error("Native client output requires exclusive private client storage");
}
static void output_scalar_check(void *ctx, const void *field) { const qa_native_mod_armor_field *f = field; output_field(ctx, f->record, f->field.offset, scalar_size(f->field.encoding)); }
static void output_vector_check(void *ctx, const void *field) { const qa_native_mod_record_offset *f = field; output_field(ctx, f->record, f->offset, 12); }
typedef struct available_t { const char *const *names; size_t count; } available_t;
static bool available_has(available_t a, const char *name) { return in_names((name_set){a.names, a.count}, name); }
static void check_values(const declaration_t *d, const value_t *values, size_t count, available_t available) {
  for (size_t i = 0; i < count; i++) {
    const value_t *value = &values[i];
    if (!strcmp(value->kind, "user-command")) { if (!available_has(available, "view-angles")) qa_range_error("Native user command requires an active input application"); continue; }
    if (!strcmp(value->kind, "address")) continue;
    if (!strcmp(value->kind, "client") || !strcmp(value->kind, "userinfo")) { if (d->clients == NULL || !available_has(available, value->input)) qa_range_error("Unavailable native client input"); continue; }
    if (!strcmp(value->kind, "actor")) { if (!available_has(available, value->input) || find_record(d, value->record) == NULL) qa_range_error("Unavailable native actor input"); continue; }
    if (!strcmp(value->kind, "time")) { if (!available_has(available, value->time_input)) qa_range_error("Unavailable native time input"); continue; }
    const qa_mod_callback_value *input = &value->value; bool named = !strcmp(input->kind, "input");
    if (named && !available_has(available, input->name)) qa_range_error("Unavailable native callback input %s", input->name);
    const char *kind = !named ? input->kind
        : one_of(input->name, NAMES("point", "direction", "normal", "view-angles")) ? "vector"
        : !strcmp(input->name, "item") ? "string"
        : one_of(input->name, NAMES("self", "other", "activator", "attacker", "inflictor")) ? "actor" : "float";
    const char *expected = !strcmp(value->kind, "vector") || !strcmp(value->kind, "string") ? value->kind : "float";
    if (strcmp(kind, expected) != 0) qa_range_error("Native callback input representation differs from its declaration");
  }
}
static void check_call(const declaration_t *d, const call_t *call, available_t available) {
  check_values(d, call->arguments, call->argument_count, available);
  for (size_t i = 0; i < call->global_count; i++) check_values(d, &call->globals[i].value, 1, available);
  for (size_t i = 0; i < call->skip_count; i++) {
    const qa_native_mod_skip *region = &call->skips[i]; bool repeated = false;
    for (size_t j = 0; j < i; j++) if (region->entry == call->skips[j].entry) repeated = true;
    if (!safe_integer(region->entry) || !safe_integer(region->join) || region->entry < 0 || region->join <= region->entry || repeated) qa_range_error("Invalid or repeated native source exclusion");
  }
}
#define AVAILABLE(...) ((available_t){(const char *const[]){__VA_ARGS__}, sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)})
void qa_compat_q2_validate_native_mod_declaration(const declaration_t *d) {
  qa_compat_q2_validate_native_mod_items(d);
  qa_compat_q2_validate_native_mod_pickups(d);
  const qa_native_mod_source_actors *owned = d->source_actors; int64_t pointer_bytes = d->target.abi.pointer_bytes;
  if (owned != NULL && (!isfinite(owned->frame_seconds) || owned->frame_seconds <= 0 || owned->clock_count == 0)) qa_range_error("Native owned actors require a positive source frame period and clock");
  const char *expected_abi = d->target.api.kind == QA_Q2_GAME_API_CLASSIC ? "q2-classic" : "q2-rerelease"; bool expected_classic = !strcmp(expected_abi, "q2-classic");
  if (owned != NULL && owned->callbacks != NULL && strcmp(owned->callbacks->abi, expected_abi) != 0) qa_range_error("Native callback ABI differs from the selected module target");
  if (owned != NULL && owned->combat != NULL && (owned->callbacks == NULL || strcmp(owned->combat->damage.abi, expected_abi) != 0 || !strcmp(owned->combat->causes.edition, "classic") != expected_classic))
    qa_range_error("Native combat requires its matching source callback and damage ABIs");
  if (owned != NULL && owned->combat != NULL && owned->combat->deferred != NULL && expected_classic) qa_range_error("Native deferred damage requires its declared rerelease mod_t ABI");
  /* TS authority Set across every record's shared fields. */
  for (size_t r = 0; r < d->actor_record_count; r++) {
    const record_t *record = &d->actor_records[r]; bool duplicate = false;
    for (size_t o = 0; o < r; o++) if (eq(d->actor_records[o].id, record->id)) duplicate = true;
    if (record->id == NULL || record->id[0] == '\0' || duplicate || !safe_integer(record->stride) || record->stride < 4 || !safe_integer(record->capacity) || record->capacity < 1 || record->capacity > 65536 || !safe_integer(record->first_slot) || record->first_slot < 0) qa_range_error("Invalid native mod actor record");
    for (size_t f = 0; f < record->field_count; f++) {
      const field_t *field = &record->fields[f];
      if (is_match(field)) { qa_source_match_field match = {!strcmp(field->binding, "score"), field->team_values, field->team_value_count}; qa_validate_source_match_field(match); }
      double length = size_of(field, pointer_bytes);
      if (!safe_integer(field->offset) || field->offset < 0 || !safe_integer(length) || length < 1 || field->offset + length > record->stride) qa_range_error("Native mod field exceeds its source record");
      for (size_t g = 0; g < f; g++) {
        const field_t *prior = &record->fields[g];
        if (field->offset < prior->offset + size_of(prior, pointer_bytes) && prior->offset < field->offset + length) qa_range_error("Overlapping native mod fields");
      }
      if (shared(field)) for (size_t pr = 0; pr <= r; pr++) {
        const record_t *other = &d->actor_records[pr];
        for (size_t pf = 0; pf < (pr == r ? f : other->field_count); pf++) {
          const field_t *prior = &other->fields[pf];
          if (shared(prior) && !strcmp(prior->binding, field->binding) && (!is_inventory(field) || eq(prior->item, field->item))) {
            if (is_inventory(field)) qa_range_error("Multiple native stores for %s:%s", field->binding, field->item);
            qa_range_error("Multiple native stores for %s", field->binding);
          }
        }
      }
      if (!strcmp(field->binding, "constant")) (void)scalar_value(field->constant_value, field->encoding);
      if (!strcmp(field->binding, "constant-vector")) { qa_vec3 v = field->qvm_field.constant_vector; (void)scalar_value(v.x, "float32"); (void)scalar_value(v.y, "float32"); (void)scalar_value(v.z, "float32"); }
    }
  }
  if (d->entity_record != NULL) { const record_t *entity = find_record(d, d->entity_record); if (entity == NULL || strcmp(entity->base.kind, "entities") != 0) qa_range_error("Native engine entity record must use the source public edict table"); }
  const qa_native_mod_clients *clients = d->clients;
  if (clients != NULL && clients->output_count != 0) qa_validate_mod_client_outputs(clients->outputs, clients->output_count, output_scalar_check, output_vector_check, (void *)d);
  if (clients != NULL) for (size_t i = 0; i < clients->output_count; i++) {
    const qa_mod_client_output_declaration *output = &clients->outputs[i]; if (output->kind != QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) continue;
    const qa_native_mod_record_offset *pairs[2] = {output->min_vector, output->max_vector}; const char *bindings[2] = {"bounds-min", "bounds-max"};
    for (int k = 0; k < 2; k++) {
      const record_t *record = find_record(d, pairs[k]->record); bool found = false;
      if (record != NULL) for (size_t f = 0; f < record->field_count && !found; f++) found = !strcmp(record->fields[f].binding, bindings[k]) && record->fields[f].offset == pairs[k]->offset;
      if (!found) qa_range_error("Client body shape must name its writable source mins/maxs");
    }
  }
  if (d->client_presentation != NULL && (clients == NULL || owned == NULL || clients->end_frame_count == 0)) qa_range_error("Native client presentation requires declared original end-frame calls");
  for (size_t i = 0; i < d->protection_count; i++) {
    const qa_native_mod_protection_definition *protection = &d->protection[i];
    for (size_t j = 0; j < i; j++) if (!strcmp(d->protection[j].channel, protection->channel)) qa_range_error("Duplicate native protection channel");
    bool regular = !strcmp(protection->channel, "regular"); size_t storage = regular ? protection->regular_storage_count : protection->power_storage_count;
    bool direct = !strcmp(protection->absorb_abi, "source-call") || !strcmp(protection->absorb_abi, "source-region");
    if (clients == NULL || d->entity_record == NULL || protection->id == NULL || protection->id[0] == '\0' || storage == 0 || (!direct && !eq(protection->absorb_q2.flags, expected_abi))) qa_range_error("Native protection requires declared clients and its matching source ABI");
  }
  if (clients != NULL) {
    const record_t *entity = d->entity_record != NULL ? find_record(d, d->entity_record) : NULL; bool repeated = false;
    for (size_t i = 0; i < clients->records.count; i++) for (size_t j = 0; j < i; j++) if (eq(clients->records.values[i], clients->records.values[j])) repeated = true;
    if (!safe_integer(clients->maximum) || clients->maximum < 1 || clients->maximum > 256 || clients->records.count == 0 || repeated || d->entity_record == NULL || entity == NULL || entity->first_slot != 1) qa_range_error("Invalid native component client layout");
    for (size_t i = 0; i < clients->records.count; i++) {
      const record_t *record = find_record(d, clients->records.values[i]);
      if (record == NULL || !strcmp(record->base.kind, "entities") || record->capacity < clients->maximum) qa_range_error("Native component clients require declared private record arrays");
    }
    for (size_t i = 0; i < clients->admit_count; i++) {
      const qa_native_mod_admission_call *call = &clients->admit[i]; bool nonzero = eq(call->accepts, "nonzero");
      if (nonzero && !strcmp(call->call.returns, "void")) qa_range_error("Native client admission requires its declared return value");
      if (!strcmp(call->call.entry.kind, "game-export") && eq(call->call.entry.name, "ClientConnect") && !nonzero) qa_range_error("Original native ClientConnect rejection cannot be ignored");
    }
    for (size_t i = 0; i < clients->records.count; i++) {
      const record_t *record = find_record(d, clients->records.values[i]);
      if (record != NULL && !strcmp(record->base.kind, "clients") && (record->first_slot != 0 || record->capacity != clients->maximum)) qa_range_error("Public native client rows must match the reserved source slots");
    }
    for (size_t r = 0; r < d->actor_record_count; r++) if (d->actor_records[r].capacity < clients->maximum) qa_range_error("Native actor array cannot hold its declared clients");
  }
  for (size_t r = 0; r < d->actor_record_count; r++) for (size_t f = 0; f < d->actor_records[r].field_count; f++) {
    const field_t *field = &d->actor_records[r].fields[f];
    if (!strcmp(field->binding, "record") && find_record(d, field->qvm_field.record) == NULL) qa_range_error("Unknown linked native actor record");
  }
  for (size_t r = 0; r < d->actor_record_count; r++) if (!strcmp(d->actor_records[r].base.kind, "clients") && !client_record(d, d->actor_records[r].id)) qa_range_error("Native public client records require declared client ownership");
  if (d->actor_record_count > 0 && (d->project_count == 0 || d->release_count == 0)) qa_range_error("Native actor projections require authored initialization and release callbacks");
  for (size_t i = 0; i < d->objective_count; i++) {
    const qa_source_objective_declaration *objective = &d->objectives[i];
    if (objective->is_owned && objective->change != NULL) check_call(d, objective->change, AVAILABLE("self", "other", "activator", "amount", "time"));
  }
  available_t pickup_inputs = AVAILABLE("self", "other", "item", "time", "pickup-count", "pickup-has-count", "pickup-dropped");
  for (size_t i = 0; i < d->pickup_count; i++) {
    const qa_native_mod_pickup *pickup = &d->pickups[i];
    check_call(d, pickup->rule.operation.grant, pickup_inputs);
    if (pickup->rule.operation.is_gate_then_grant) check_call(d, pickup->rule.operation.gate, pickup_inputs);
    for (size_t c = 0; c < pickup->context_count; c++) {
      const qa_native_mod_pickup_context *field = &pickup->context[c];
      check_values(d, &field->value, 1, pickup_inputs);
      const record_t *record = find_record(d, field->record);
      double length = !strcmp(field->value.kind, "vector") ? 12 : !strcmp(field->value.kind, "address") ? (double)pointer_bytes : scalar_size(value_scalar_kind(&field->value));
      bool ok = record != NULL && !client_record(d, record->id) && safe_integer(field->offset) && field->offset >= 0 && field->offset + length <= record->stride;
      if (ok) {
        bool declared = false;
        for (size_t f = 0; f < record->field_count && !declared; f++) {
          const field_t *value = &record->fields[f];
          declared = one_of(value->binding, NAMES("private", "constant", "address", "constant-vector")) && value->offset <= field->offset && field->offset + length <= value->offset + size_of(value, pointer_bytes);
        }
        ok = declared;
      }
      if (ok) for (size_t p = 0; p < c && ok; p++) {
        const qa_native_mod_pickup_context *prior = &pickup->context[p];
        double prior_length = !strcmp(prior->value.kind, "vector") ? 12 : !strcmp(prior->value.kind, "address") ? (double)pointer_bytes : scalar_size(value_scalar_kind(&prior->value));
        if (eq(prior->record, field->record) && field->offset < prior->offset + prior_length && prior->offset < field->offset + length) ok = false;
      }
      if (!ok) qa_range_error("Native pickup context requires separate declared source storage");
      if (eq(field->record, d->entity_record) && owned != NULL) {
        double pointers[6]; size_t n = 0; pointers[n++] = owned->fields.ground; pointers[n++] = owned->fields.think;
        if (owned->fields.has_use) pointers[n++] = owned->fields.use;
        if (owned->callbacks != NULL) { if (owned->callbacks->has_touch) pointers[n++] = owned->callbacks->touch; if (owned->callbacks->has_pain) pointers[n++] = owned->callbacks->pain; if (owned->callbacks->has_die) pointers[n++] = owned->callbacks->die; }
        bool overlaps = false;
        for (size_t k = 0; k < n; k++) if (field->offset < pointers[k] + (double)pointer_bytes && pointers[k] < field->offset + length) overlaps = true;
        double next_offset = owned->fields.nextthink.offset;
        if (overlaps || (field->offset < next_offset + scalar_size(owned->fields.nextthink.encoding) && next_offset < field->offset + length)) qa_range_error("Native pickup context overlaps source actor lifetime");
      }
    }
  }
  available_t protection_inputs = AVAILABLE("self", "attacker", "inflictor", "time", "amount", "damage-flags", "regular-protection-scale", "knockback", "direction", "point", "normal");
  for (size_t i = 0; i < d->protection_count; i++) {
    const qa_native_mod_protection_definition *protection = &d->protection[i];
    bool region = !strcmp(protection->absorb_abi, "source-region"), source_call = !strcmp(protection->absorb_abi, "source-call");
    if (region) {
      const qa_native_mod_protection_region *r = protection->absorb_region;
      qa_compat_q2_validate_native_mod_region(r, pointer_bytes);
      for (size_t k = 0; k < r->input_count; k++) check_values(d, &r->inputs[k].value, 1, protection_inputs);
      const char **storages = calloc(r->call.argument_count + 1, sizeof *storages); if (storages == NULL) qa_range_error("Out of memory");
      for (size_t k = 0; k < r->call.argument_count; k++) storages[k] = value_storage(&r->call.arguments[k]);
      int64_t stack = qa_compat_q2_native_plan_stack_bytes(d->target.abi.kind, d->target.abi.image, pointer_bytes, d->target.abi.call, storages, r->call.argument_count, !strcmp(r->call.returns, "void") ? NULL : r->call.returns);
      free(storages);
      if (r->frame.argument_bytes != (double)(stack - pointer_bytes)) qa_range_error("Native donor argument area differs from its original call ABI");
      for (size_t k = 0; k < r->input_count; k++) if (strcmp(value_storage(&r->inputs[k].value), r->inputs[k].target.storage) != 0) qa_range_error("Native donor input differs from its machine storage");
      check_call(d, &r->call, protection_inputs);
    } else if (source_call) {
      if (!strcmp(protection->absorb_call.returns, "void")) qa_range_error("Native protection requires a source return value");
      check_call(d, &protection->absorb_call, protection_inputs);
    } else for (size_t k = 0; k < protection->absorb_q2.global_count; k++) check_values(d, &protection->absorb_q2.globals[k].value, 1, protection_inputs);
  }
  available_t self_time = AVAILABLE("self", "time");
  if (d->items != NULL) {
    for (size_t i = 0; i < d->items->definition_count; i++) {
      const qa_native_mod_item_definition *item = &d->items->definitions[i];
      if (item->action_use != NULL) check_call(d, item->action_use, self_time);
      if (item->action_drop != NULL) check_call(d, item->action_drop, self_time);
    }
    if (d->items->weapons != NULL) for (size_t i = 0; i < d->items->weapons->selection.value_count; i++) check_call(d, &d->items->weapons->selection.values[i].request, self_time);
  }
  for (size_t i = 0; i < d->initialize_count; i++) check_call(d, &d->initialize[i], AVAILABLE("time"));
  for (size_t i = 0; i < d->project_count; i++) check_call(d, &d->project[i], self_time);
  for (size_t i = 0; i < d->release_count; i++) check_call(d, &d->release[i], self_time);
  if (clients != NULL) {
    for (size_t i = 0; i < clients->admit_count; i++) check_call(d, &clients->admit[i].call, self_time);
    for (size_t i = 0; i < clients->userinfo_count; i++) check_call(d, &clients->userinfo[i], self_time);
    for (size_t i = 0; i < clients->disconnect_count; i++) check_call(d, &clients->disconnect[i], self_time);
    for (size_t i = 0; i < clients->command_count; i++) check_call(d, &clients->command[i], self_time);
    for (size_t i = 0; i < clients->frame_count; i++) check_call(d, &clients->frame[i], self_time);
    for (size_t i = 0; i < clients->end_frame_count; i++) check_call(d, &clients->end_frame[i], self_time);
    if ((clients->frame_count != 0 || clients->end_frame_count != 0) && owned == NULL) qa_range_error("Native client frames require the original source actor clock");
    available_t available = AVAILABLE("self", "time", "elapsed", "view-angles", "attack", "jump", "impulse", "forward-move", "side-move", "up-move");
    for (size_t i = 0; i < clients->input_count; i++) for (size_t c = 0; c < clients->input[i].call_count; c++) check_call(d, (const call_t *)clients->input[i].calls + c, available);
    if (clients->input_field_count != 0 && clients->input_count == 0) qa_range_error("Native input fields require declared input callbacks");
    for (size_t i = 0; i < clients->input_field_count; i++) {
      const qa_native_mod_client_input_field *field = &clients->input_fields[i]; const record_t *record = find_record(d, field->record);
      double length = !strcmp(field->value.kind, "vector") ? 12 : scalar_size(value_scalar_kind(&field->value));
      bool ok = record != NULL && (eq(field->record, d->entity_record) || client_record(d, field->record)) && safe_integer(field->offset) && field->offset >= 0 && field->offset + length <= record->stride;
      if (ok) {
        bool declared = false;
        for (size_t f = 0; f < record->field_count && !declared; f++) { const field_t *c = &record->fields[f]; declared = !strcmp(c->binding, "private") && field->offset >= c->offset && field->offset + length <= c->offset + c->qvm_field.byte_length; }
        ok = declared;
      }
      if (!ok) qa_range_error("Native input field requires declared private client storage");
      for (size_t p = 0; p < i; p++) {
        const qa_native_mod_client_input_field *prior = &clients->input_fields[p];
        double prior_length = !strcmp(prior->value.kind, "vector") ? 12 : scalar_size(value_scalar_kind(&prior->value));
        if (eq(prior->record, field->record) && field->offset < prior->offset + prior_length && prior->offset < field->offset + length) qa_range_error("Overlapping native input fields");
      }
      check_values(d, &field->value, 1, available);
    }
    if (clients->pose != NULL) {
      const qa_native_mod_armor_field *pose[2] = {&clients->pose->view_height, &clients->pose->crouched.field};
      for (int i = 0; i < 2; i++) {
        const record_t *record = find_record(d, pose[i]->record); double offset = pose[i]->field.offset, length = scalar_size(pose[i]->field.encoding);
        bool ok = record != NULL && (eq(pose[i]->record, d->entity_record) || client_record(d, pose[i]->record)) && safe_integer(offset) && offset >= 0 && offset + length <= record->stride;
        if (ok) for (size_t f = 0; f < record->field_count && ok; f++) { const field_t *value = &record->fields[f]; if (shared(value) && offset < value->offset + size_of(value, pointer_bytes) && value->offset < offset + length) ok = false; }
        if (!ok) qa_range_error("Native client pose requires separate private source fields");
      }
      double mask = clients->pose->crouched.mask;
      if (!safe_integer(mask) || mask < 1 || mask > 2147483647.0) qa_range_error("Invalid native source crouch mask");
    }
    for (size_t i = 0; i < clients->input_count; i++) {
      const qa_mod_client_input_binding *binding = &clients->input[i]; if (!eq(binding->phase, "before")) continue;
      const qa_native_mod_input_output *outputs = binding->outputs;
      for (size_t o = 0; o < binding->output_count; o++) {
        const qa_native_mod_input_output *output = &outputs[o];
        if (!strcmp(output->kind, "handler")) {
          check_values(d, output->arguments, output->argument_count, available); size_t selves = 0;
          for (size_t a = 0; a < output->argument_count; a++) if (!strcmp(output->arguments[a].kind, "actor") && eq(output->arguments[a].input, "self")) selves++;
          if (selves != 1) qa_range_error("Native output handler requires its exact client actor argument");
        }
        if (!strcmp(output->kind, "field")) {
          bool found = false;
          for (size_t f = 0; f < clients->input_field_count && !found; f++) {
            const qa_native_mod_client_input_field *field = &clients->input_fields[f];
            found = eq(field->record, output->record) && field->offset == output->offset && strcmp(field->value.kind, "time") != 0 && eq(field->value.value.kind, "input")
                && one_of(field->value.value.name, NAMES("view-angles", "attack", "jump", "impulse", "forward-move", "side-move", "up-move"));
          }
          if (!found) qa_range_error("Native input output requires its declared input field");
        }
      }
    }
  }
  for (size_t i = 0; i < d->callback_count; i++) {
    const qa_native_mod_callback *call = &d->callbacks[i]; bool observe = !strcmp(call->binding.stage, "observe"), duplicate = false;
    for (size_t j = 0; j < i; j++) if (eq(d->callbacks[j].binding.id, call->binding.id)) duplicate = true;
    if (duplicate || (!observe && !strcmp(call->call.returns, "void"))) qa_range_error("Duplicate native callback or missing return value");
    const char *names[16]; size_t n = 0; names[n++] = "self"; names[n++] = "time"; if (observe) names[n++] = "result";
    const char *operation = call->binding.operation;
    static const char *const extras_damage[] = {"attacker", "inflictor", "amount", "knockback", "direction", "point", "normal", NULL};
    static const char *const extras_inventory[] = {"item", "amount", NULL};
    static const char *const extras_use[] = {"other", "activator", NULL};
    static const char *const extras_touch[] = {"other", NULL};
    static const char *const extras_think[] = {"elapsed", NULL};
    static const char *const extras_pain[] = {"attacker", "amount", "knockback", NULL};
    static const char *const extras_die[] = {"attacker", "inflictor", "amount", "knockback", "point", NULL};
    const char *const *extra = !strcmp(operation, "damage") ? extras_damage : (!strcmp(operation, "inventory.give") || !strcmp(operation, "inventory.consume")) ? extras_inventory
        : !strcmp(operation, "actor.use") ? extras_use : !strcmp(operation, "actor.touch") ? extras_touch : !strcmp(operation, "actor.think") ? extras_think : !strcmp(operation, "actor.pain") ? extras_pain : extras_die;
    for (; *extra != NULL; extra++) names[n++] = *extra;
    available_t available = {names, n};
    check_call(d, &call->call, available);
  }
}

/* --- Checkpoint reader. --- */
typedef struct saved_actor_row { qa_saved_actor_id actor; double slot; bool appearance; } saved_actor_row;
typedef struct saved_client_row { qa_saved_actor_id actor; double slot; bool admitted; } saved_client_row;
typedef struct saved_mod {
  qa_save_value *root; bool has_items; qa_compat_q2_native_item_checkpoint items; bool has_owned; qa_compat_q2_saved_native_actors owned;
  const char *map; qa_compat_q2_native_mod_source_save source; qa_compat_q2_native_mod_configstring *configstrings;
  saved_actor_row *actors; size_t actor_count; saved_client_row *clients; size_t client_count; const qa_save_value *presentation;
} saved_mod;
typedef qa_save_reader reader_t;
static const reader_t *rd(const reader_t *r, const char *name, reader_t *out) { if (!qa_save_reader_field(r, name, out)) qa_range_error("Out of memory"); return out; }
static double rd_int(const reader_t *r, const char *name, double minimum) { reader_t f; double v = qa_save_reader_integer_min(rd(r, name, &f), minimum); qa_save_reader_destroy(&f); return v; }
static bool rd_bool(const reader_t *r, const char *name) { reader_t f; bool v = qa_save_reader_boolean(rd(r, name, &f)); qa_save_reader_destroy(&f); return v; }
static qa_saved_actor_id rd_actor(const reader_t *r, const char *name) {
  reader_t f; rd(r, name, &f); qa_saved_actor_id id; id.slot = rd_int(&f, "slot", 0); id.generation = rd_int(&f, "generation", 0); qa_save_reader_destroy(&f); return id;
}
static bool same_saved(qa_saved_actor_id a, qa_saved_actor_id b) { return a.slot == b.slot && a.generation == b.generation; }
static bool same_module(qa_module_identity a, qa_module_identity b) { return eq(a.id, b.id) && eq(a.artifact_path, b.artifact_path) && eq(a.digest, b.digest) && eq(a.revision, b.revision); }
static const char *rd_namespaced(const reader_t *r, const char *name) { reader_t f; const char *v = qa_namespaced(rd(r, name, &f)); qa_save_reader_destroy(&f); return v; }
static qa_byte_view rd_bytes(const reader_t *r, const char *name) { reader_t f; qa_byte_view v; v.bytes = qa_save_reader_bytes(rd(r, name, &f), &v.length); qa_save_reader_destroy(&f); return v; }
static void saved_mod_free(saved_mod *saved) {
  free(saved->actors); free(saved->clients); free(saved->configstrings);
  if (saved->has_owned) qa_compat_q2_saved_native_actors_free(&saved->owned);
  if (saved->has_items) qa_compat_q2_native_item_checkpoint_free(&saved->items);
  qa_save_value_free(saved->root);
}
static saved_mod read_checkpoint(const qa_provider_checkpoint *record, qa_module_identity module, const declaration_t *d) {
  if (!eq(record->provider, module.id) || !eq(record->schema, "native:mod") || record->version != 1) qa_range_error("Invalid native mod checkpoint owner");
  saved_mod out; memset(&out, 0, sizeof out);
  out.root = qa_decode_checkpoint_value(record->bytes.bytes, record->bytes.length); if (out.root == NULL) qa_range_error("Out of memory");
  reader_t reader, f, state; if (!qa_save_reader_init(&reader, out.root, NULL)) qa_range_error("Out of memory");
  qa_module_identity saved_module = qa_read_module(rd(&reader, "module", &f)); qa_save_reader_destroy(&f);
  if (!same_module(saved_module, module)) qa_range_error("Native mod checkpoint differs from the selected source");
  rd(&reader, "source", &state);
  static const char *const editions[] = {"classic", "rerelease"};
  const char *edition = qa_save_reader_choice_string(rd(&state, "edition", &f), editions, 2); qa_save_reader_destroy(&f);
  out.map = qa_save_reader_string(rd(&reader, "map", &f)); qa_save_reader_destroy(&f);
  if (!strcmp(edition, "classic")) {
    if (d->target.api.kind != QA_Q2_GAME_API_CLASSIC) qa_range_error("Native mod checkpoint API differs");
    reader_t value; rd(&state, "original", &value);
    out.source.rerelease = false; out.source.original.provider = rd_namespaced(&value, "provider"); out.source.original.schema = rd_namespaced(&value, "schema");
    out.source.original.version = rd_int(&value, "version", 1); out.source.original.bytes = rd_bytes(&value, "bytes");
    qa_save_reader_destroy(&value);
    qa_q2_classic_original_identity identity; identity.module = module; identity.map = out.map; qa_q2_classic_original_save decoded;
    if (!qa_decode_q2_classic_original_save(&out.source.original, identity, &decoded)) qa_range_error("Out of memory");
    qa_q2_classic_original_save_free(&decoded);
  } else {
    if (d->target.api.kind != QA_Q2_GAME_API_RERELEASE) qa_range_error("Native mod checkpoint API differs");
    out.source.rerelease = true; out.source.game = rd_bytes(&state, "game"); out.source.level = rd_bytes(&state, "level"); out.source.cvars = rd_bytes(&state, "cvars");
    reader_t list; rd(&state, "configstrings", &list); size_t count = qa_save_reader_list_count(&list);
    out.configstrings = calloc(count + 1, sizeof *out.configstrings); if (out.configstrings == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      reader_t entry, text; if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("Out of memory");
      out.configstrings[i].index = rd_int(&entry, "index", 0); out.configstrings[i].value = qa_save_reader_string(rd(&entry, "value", &text)); qa_save_reader_destroy(&text); qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&list); out.source.configstrings = out.configstrings; out.source.configstring_count = count;
    if (out.source.game.length == 0 || out.source.level.length == 0 || memchr(out.source.game.bytes, 0, out.source.game.length) != NULL || memchr(out.source.level.bytes, 0, out.source.level.length) != NULL) qa_range_error("Invalid native source JSON save");
    qa_save_value *cvars = qa_decode_checkpoint_value(out.source.cvars.bytes, out.source.cvars.length); if (cvars == NULL) qa_range_error("Out of memory"); qa_save_value_free(cvars);
  }
  qa_save_reader_destroy(&state);
  reader_t list; rd(&reader, "actors", &list); out.actor_count = qa_save_reader_list_count(&list);
  out.actors = calloc(out.actor_count + 1, sizeof *out.actors); if (out.actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < out.actor_count; i++) {
    reader_t entry; if (!qa_save_reader_item(&list, i, &entry)) qa_range_error("Out of memory");
    out.actors[i].actor = rd_actor(&entry, "actor"); out.actors[i].slot = rd_int(&entry, "slot", 0); out.actors[i].appearance = rd_bool(&entry, "appearance"); qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&list);
  double capacity = INFINITY;
  for (size_t r = 0; r < d->actor_record_count; r++) if (!client_record(d, d->actor_records[r].id)) capacity = qa_js_min(capacity, d->actor_records[r].capacity);
  bool invalid = out.actor_count > 0 && d->actor_record_count == 0;
  for (size_t i = 0; i < out.actor_count; i++) {
    if (out.actors[i].slot >= capacity) invalid = true;
    for (size_t j = 0; j < i; j++) if (out.actors[i].slot == out.actors[j].slot || same_saved(out.actors[i].actor, out.actors[j].actor)) invalid = true;
  }
  if (invalid) qa_range_error("Invalid native mod actor checkpoint");
  reader_t client_reader; rd(&reader, "clients", &client_reader);
  bool has_clients_field = !qa_save_reader_is_undefined(&client_reader);
  if (has_clients_field) {
    out.client_count = qa_save_reader_list_count(&client_reader);
    out.clients = calloc(out.client_count + 1, sizeof *out.clients); if (out.clients == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < out.client_count; i++) {
      reader_t entry; if (!qa_save_reader_item(&client_reader, i, &entry)) qa_range_error("Out of memory");
      out.clients[i].actor = rd_actor(&entry, "actor"); out.clients[i].slot = rd_int(&entry, "slot", 0); out.clients[i].admitted = rd_bool(&entry, "admitted"); qa_save_reader_destroy(&entry);
    }
  }
  qa_save_reader_destroy(&client_reader);
  double maximum = d->clients != NULL ? d->clients->maximum : 0;
  invalid = (d->clients != NULL && !has_clients_field) || (double)out.client_count > maximum;
  for (size_t i = 0; i < out.client_count; i++) {
    bool matched = false;
    for (size_t j = 0; j < i; j++) if (out.clients[i].slot == out.clients[j].slot || same_saved(out.clients[i].actor, out.clients[j].actor)) invalid = true;
    for (size_t a = 0; a < out.actor_count && !matched; a++) matched = out.actors[a].slot == out.clients[i].slot && same_saved(out.actors[a].actor, out.clients[i].actor);
    if (out.clients[i].slot >= maximum || !matched) invalid = true;
  }
  for (size_t a = 0; a < out.actor_count; a++) if (out.actors[a].slot < maximum) {
    bool matched = false;
    for (size_t i = 0; i < out.client_count && !matched; i++) matched = out.clients[i].slot == out.actors[a].slot;
    if (!matched) invalid = true;
  }
  if (invalid) qa_range_error("Invalid native component client checkpoint");
  reader_t owned; rd(&reader, "owned", &owned);
  out.has_owned = !qa_save_reader_is_undefined(&owned) && !qa_save_reader_is_null(&owned);
  if (out.has_owned) {
    reader_t x; out.owned.next_frame = qa_save_reader_finite(rd(&owned, "nextFrame", &x)); qa_save_reader_destroy(&x);
    out.owned.frame = rd_int(&owned, "frame", 0);
    rd(&owned, "deferred", &x);
    if (!qa_save_reader_is_undefined(&x)) {
      out.owned.deferred_count = qa_save_reader_list_count(&x);
      out.owned.deferred = calloc(out.owned.deferred_count + 1, sizeof *out.owned.deferred); if (out.owned.deferred == NULL) qa_range_error("Out of memory");
      for (size_t i = 0; i < out.owned.deferred_count; i++) { reader_t entry; if (!qa_save_reader_item(&x, i, &entry)) qa_range_error("Out of memory"); out.owned.deferred[i] = qa_compat_q2_read_native_deferred_damage(&entry); qa_save_reader_destroy(&entry); }
    }
    qa_save_reader_destroy(&x);
    rd(&owned, "actors", &x); out.owned.actor_count = qa_save_reader_list_count(&x);
    out.owned.actors = calloc(out.owned.actor_count + 1, sizeof *out.owned.actors); if (out.owned.actors == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < out.owned.actor_count; i++) {
      reader_t entry; if (!qa_save_reader_item(&x, i, &entry)) qa_range_error("Out of memory");
      out.owned.actors[i].actor = rd_actor(&entry, "actor"); out.owned.actors[i].slot = rd_int(&entry, "slot", 1); out.owned.actors[i].linked = rd_bool(&entry, "linked"); qa_save_reader_destroy(&entry);
    }
    qa_save_reader_destroy(&x);
  }
  qa_save_reader_destroy(&owned);
  invalid = out.has_owned != (d->source_actors != NULL);
  if (out.has_owned) for (size_t i = 0; i < out.owned.actor_count; i++) for (size_t j = 0; j < i; j++) if (out.owned.actors[i].slot == out.owned.actors[j].slot || same_saved(out.owned.actors[i].actor, out.owned.actors[j].actor)) invalid = true;
  if (invalid) qa_range_error("Invalid native owned actor checkpoint");
  if (out.has_owned) {
    invalid = out.owned.deferred_count > 0 && (d->source_actors->combat == NULL || d->source_actors->combat->deferred == NULL);
    for (size_t i = 0; i < out.owned.deferred_count; i++) {
      bool matched = false;
      for (size_t j = 0; j < i; j++) if (same_saved(out.owned.deferred[i].target, out.owned.deferred[j].target)) invalid = true;
      for (size_t a = 0; a < out.owned.actor_count && !matched; a++) matched = same_saved(out.owned.actors[a].actor, out.owned.deferred[i].target);
      if (!matched) invalid = true;
    }
    if (invalid) qa_range_error("Invalid native deferred attack checkpoint");
  }
  reader_t items; rd(&reader, "items", &items);
  bool items_present = !qa_save_reader_is_undefined(&items) && !qa_save_reader_is_null(&items);
  if (items_present != (d->items != NULL)) qa_range_error("Native item checkpoint differs from source declaration");
  if (d->items != NULL) {
    out.items = qa_compat_q2_read_native_item_checkpoint(&items, d->items); out.has_items = true;
    for (size_t i = 0; i < out.items.request_count; i++) {
      bool admitted = false;
      for (size_t c = 0; c < out.client_count && !admitted; c++) admitted = out.clients[c].admitted && same_saved(out.clients[c].actor, out.items.requests[i].actor);
      if (!admitted) qa_range_error("Saved native item request has no admitted client");
    }
  }
  qa_save_reader_destroy(&items);
  out.presentation = qa_save_value_get(out.root, "presentation");
  { /* TS presentation: readNativeModPresentation(reader.field("presentation")). */
    reader_t presentation; qa_app_sim_native_mod_presentation_checkpoint checkpoint;
    if (!qa_save_reader_field(&reader, "presentation", &presentation)) qa_range_error("Out of memory");
    qa_app_sim_read_native_mod_presentation(&presentation, &checkpoint);
    qa_app_sim_native_mod_presentation_checkpoint_free(&checkpoint); qa_save_reader_destroy(&presentation); }
  qa_save_reader_destroy(&reader);
  return out;
}
void qa_compat_q2_validate_native_mod_checkpoint(const qa_provider_checkpoint *record, qa_module_identity module, const declaration_t *d) {
  saved_mod saved = read_checkpoint(record, module, d); saved_mod_free(&saved);
}

/* --- NativeModProvider. --- */
typedef struct projection_row { qa_actor_id actor; double slot; } projection_row;
typedef struct observation { qa_actor_id actor; qa_guest_address address; const field_t *field; uint8_t bytes[16]; size_t length; } observation;
typedef enum commit_kind { COMMIT_MATCH, COMMIT_HEALTH, COMMIT_INVENTORY, COMMIT_BODY } commit_kind;
typedef struct pending_commit {
  commit_kind kind; qa_actor_id actor; qa_guest_address address; const field_t *field; double value; qa_vec3 vector;
  bool has_count, has_capacity; double count, capacity;
  bool has_commit; qa_compat_q2_native_protection_inventory_commit commit;
} pending_commit;
typedef struct invocation { observation *observations; size_t observation_count; pending_commit *pending; size_t pending_count, pending_capacity, cursor; } invocation;
typedef struct input_store { qa_guest_address address; uint8_t bytes[16], previous[16]; size_t length; } input_store;
typedef struct input_scope {
  provider_t *owner; qa_mod_client_application *application; input_row rows[18]; size_t count; qa_actor_id self;
  input_store *stores; size_t store_count; struct input_scope *outer;
} input_scope;
typedef struct pickup_scope { qa_actor_id actor; const qa_original_pickup_execution *execution; invocation *frame; provider_t *owner; } pickup_scope;
typedef struct restore_link { double slot; qa_guest_address address; void *ctx; qa_compat_q2_native_import_fn invoke; qa_guest_call_value *values; size_t count; } restore_link;
typedef struct match_binding { provider_t *owner; qa_actor_id actor; qa_owned_actor owned; double slot; const field_t *team, *score; struct match_binding *next; } match_binding;
struct qa_compat_q2_native_mod_provider {
  const declaration_t *declaration; const qa_mod_host_services *services; qa_provider_id instance; const char *map;
  void *assert_ctx; void (*assert_current)(void *);
  const qa_compat_q2_native_mod_host *host_;
  qa_compat_q2_native_mod_actors *owned;
  bool has_clients; qa_compat_q2_native_mod_clients_binding clients;
  bool has_stages; qa_compat_q2_native_mod_stage_runner stages;
  qa_compat_q2_native_mod_items *items;
  qa_compat_q2_native_mod_protection *protection;
  qa_compat_q2_native_mod_pickups *pickups;
  projection_row *projections; size_t projection_count, projection_capacity;
  invocation **frames; size_t frame_count, frame_capacity;
  pickup_scope **pickup_scopes; size_t pickup_scope_count, pickup_scope_capacity;
  int projection_writes;
  input_scope **input_scopes; size_t input_scope_count, input_scope_capacity;
  input_scope *input_context;
  qa_actor_id *appearance_actors; size_t appearance_count, appearance_capacity;
  qa_actor_id *pending_releases; size_t pending_release_count, pending_release_capacity;
  int64_t subscription; bool subscribed;
  qa_source_mod_client_outputs *client_outputs;
  bool closed, closing, pending_host_close, ready, lifecycle, restoring;
  int source_calls;
  const qa_compat_q2_native_mod_region_authority **region_authorities; size_t region_authority_count, region_authority_capacity;
  bool has_invocations; qa_compat_q2_native_mod_invocations invocations;
  qa_mod_source_objectives *objectives;
  bool match_bound; qa_source_match_unbind release_match; match_binding *matches;
  restore_link *restore_links; size_t restore_link_count, restore_link_capacity;
  qa_app_simulation_presentation *scratch; size_t scratch_count, scratch_capacity;
  char location[256];
};
static const qa_compat_q2_native_mod_host *host_of(const provider_t *s) { if (s->host_ == NULL) qa_range_error("Native mod has no source host"); return s->host_; }
static void current(provider_t *s) { if (!s->closing) s->assert_current(s->assert_ctx); if (s->closed) qa_range_error("Native mod is closed"); }
static qa_compat_q2_memory *memory_of(const provider_t *s) { return host_of(s)->memory; }
static const record_t *record_of(const provider_t *s, const char *id) { return find_record(s->declaration, id); }
static bool world_actor(const provider_t *s, qa_actor_id *out) { const qa_session_mod_engine_services *e = s->services->engine; return e != NULL && e->world(e->ctx, out); }
static bool is_world(const provider_t *s, qa_actor_id actor) { qa_actor_id world; return world_actor(s, &world) && qa_same_actor(world, actor); }
static bool clients_has(const provider_t *s, qa_actor_id actor) { return s->has_clients && qa_compat_q2_native_mod_clients_has(&s->clients, actor); }
static bool clients_admitted(provider_t *s, qa_actor_id actor) { return s->has_clients && qa_compat_q2_native_mod_clients_admitted(&s->clients, actor); }
static bool clients_rejects(const provider_t *s, qa_actor_id actor) { return s->has_clients && qa_compat_q2_native_mod_clients_rejects(&s->clients, actor); }
static bool is_live(const provider_t *s, qa_actor_id actor) { return qa_session_actor_registry_is_live(s->services->actors, actor); }
static projection_row *find_projection(const provider_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->projection_count; i++) if (qa_same_actor(s->projections[i].actor, actor)) return &s->projections[i]; return NULL; }
static void delete_projection(provider_t *s, qa_actor_id actor) {
  projection_row *row = find_projection(s, actor); if (row == NULL) return;
  size_t index = (size_t)(row - s->projections); memmove(row, row + 1, (s->projection_count - index - 1) * sizeof *row); s->projection_count--;
}
static bool actor_list_has(const qa_actor_id *list, size_t count, qa_actor_id actor) { for (size_t i = 0; i < count; i++) if (qa_same_actor(list[i], actor)) return true; return false; }
static void actor_list_delete(qa_actor_id *list, size_t *count, qa_actor_id actor) { for (size_t i = 0; i < *count; i++) if (qa_same_actor(list[i], actor)) { memmove(&list[i], &list[i + 1], (*count - i - 1) * sizeof *list); (*count)--; return; } }
static void appearance_add(provider_t *s, qa_actor_id actor) { if (actor_list_has(s->appearance_actors, s->appearance_count, actor)) return; qa_actor_id *v = qa_compat_q2_grow((void **)&s->appearance_actors, &s->appearance_count, &s->appearance_capacity, sizeof *v); *v = actor; }
static void pending_release_add(provider_t *s, qa_actor_id actor) { if (actor_list_has(s->pending_releases, s->pending_release_count, actor)) return; qa_actor_id *v = qa_compat_q2_grow((void **)&s->pending_releases, &s->pending_release_count, &s->pending_release_capacity, sizeof *v); *v = actor; }
static const qa_mod_runtime_value *find_input(inputs_t inputs, const char *name) { for (size_t i = 0; i < inputs.count; i++) if (!strcmp(inputs.rows[i].name, name)) return &inputs.rows[i].value; return NULL; }
/* TS this.inputs(actor?): the input context's values, or time; self replaced. */
typedef struct input_buffer { input_row rows[18]; size_t count; qa_actor_id self; } input_buffer;
static void buffer_set(input_buffer *b, const char *name, qa_mod_runtime_value value) {
  for (size_t i = 0; i < b->count; i++) if (!strcmp(b->rows[i].name, name)) { b->rows[i].value = value; return; }
  b->rows[b->count].name = name; b->rows[b->count].value = value; b->count++;
}
static inputs_t build_inputs(const provider_t *s, const qa_actor_id *actor, input_buffer *b) {
  memset(b, 0, sizeof *b); const input_scope *scope = s->input_context;
  if (scope != NULL) { for (size_t i = 0; i < scope->count; i++) b->rows[i] = scope->rows[i]; b->count = scope->count; }
  else {
    qa_mod_runtime_value time; memset(&time, 0, sizeof time); time.kind = "float";
    time.number_value = s->owned != NULL ? qa_compat_q2_native_mod_actors_source_time(s->owned) : qa_compat_q2_time_seconds(qa_compat_q2_services_time(s->services));
    buffer_set(b, "time", time);
  }
  if (actor != NULL) { b->self = *actor; qa_mod_runtime_value self; memset(&self, 0, sizeof self); self.kind = "actor"; self.actor_value = &b->self; buffer_set(b, "self", self); }
  inputs_t inputs = {b->rows, b->count}; return inputs;
}
static void scalar_write(provider_t *s, qa_guest_address address, double value, const char *kind) {
  qa_guest_call_value lowered = scalar_value(value, kind); uint8_t bytes[8]; qa_compat_q2_memory *memory = memory_of(s);
  int length = qa_compat_q2_native_encode_scalar(memory, kind, &lowered, bytes);
  memory->write(memory->ctx, address, bytes, (size_t)length);
}
static double scalar_read(provider_t *s, qa_guest_address address, const char *kind) {
  uint8_t bytes[8]; qa_compat_q2_memory *memory = memory_of(s);
  memory->copy(memory->ctx, address, bytes, scalar_size(kind));
  qa_guest_call_result value = qa_compat_q2_native_decode_scalar(memory, kind, bytes);
  return number_result(&value);
}
static void objective_check(provider_t *s, bool objective) { if (objective) { current(s); if (s->closing || s->restoring) qa_range_error("Native objective source is unavailable"); } }
static qa_guest_address resolve(provider_t *s, qa_native_mod_address value, bool objective) {
  qa_compat_q2_memory *memory = memory_of(s);
  objective_check(s, objective);
  qa_guest_address address = qa_compat_q2_native_mod_image_rva(host_of(s), value.rva);
  for (size_t i = 0; i < value.indirections.count; i++) {
    objective_check(s, objective);
    qa_guest_address pointer;
    if (!qa_compat_q2_mem_read_pointer(memory, address, &pointer)) qa_range_error("Native mod layout follows a null source pointer");
    address = qa_compat_q2_mem_offset(memory, pointer, (int64_t)value.indirections.values[i]);
  }
  objective_check(s, objective);
  return address;
}
static qa_guest_address record_base(provider_t *s, const record_t *record) {
  const qa_compat_q2_native_mod_host *host = host_of(s); qa_compat_q2_memory *memory = host->memory;
  if (!strcmp(record->base.kind, "address")) return qa_compat_q2_mem_offset(memory, resolve(s, record->base.address, false), (int64_t)(record->first_slot * record->stride));
  if (!strcmp(record->base.kind, "clients")) qa_range_error("Native private client rows are addressed through their source edicts");
  qa_compat_q2_native_mod_entities entities = host->entities(host->ctx);
  if (entities.stride != record->stride || record->first_slot + record->capacity > entities.capacity) qa_range_error("Declared native actor layout differs from the source export table");
  return qa_compat_q2_mem_offset(memory, entities.base, (int64_t)(record->first_slot * record->stride));
}
static qa_guest_address record_address(provider_t *s, const record_t *record, double slot) {
  const qa_compat_q2_native_mod_host *host = host_of(s); qa_compat_q2_memory *memory = host->memory;
  if (!strcmp(record->base.kind, "clients")) {
    qa_guest_address address;
    if (!host->client(host->ctx, record->first_slot + slot + 1, &address)) qa_range_error("Authored native initialization has not provided this client's private storage");
    memory->check(memory->ctx, address, record->stride, "write"); return address;
  }
  return qa_compat_q2_mem_offset(memory, record_base(s, record), (int64_t)(slot * record->stride));
}
/* TS actorRecords(actor) as a membership test over declaration.actorRecords. */
static bool actor_uses_record(const provider_t *s, bool client, const record_t *record) { return client || !client_record(s->declaration, record->id); }
static void validate_inventory_capacity(provider_t *s, qa_actor_id actor, bool client) {
  const declaration_t *d = s->declaration;
  for (size_t r = 0; r < d->actor_record_count; r++) {
    const record_t *record = &d->actor_records[r]; if (!actor_uses_record(s, client, record)) continue;
    for (size_t f = 0; f < record->field_count; f++) if (!strcmp(record->fields[f].binding, "inventory-capacity") && !qa_shared_inventory_table_mutable_capacity(s->services->inventory, actor, record->fields[f].item))
      qa_range_error("Native mod requires mutable inventory capacity for %s", record->fields[f].item);
  }
}
static bool execute(provider_t *s, const call_t *call, inputs_t inputs, bool transfer_flag, const qa_native_mod_protection_region *region, const qa_compat_q2_native_mod_region_authority *authority, double *out);
static void release_pending(provider_t *s);
static bool pointer_for(provider_t *s, const qa_actor_id *actor, const char *id, qa_guest_address *out);
static void seed(provider_t *s, qa_actor_id actor, double slot, bool constants) {
  const declaration_t *d = s->declaration; qa_compat_q2_memory *memory = memory_of(s); bool client = clients_has(s, actor);
  for (size_t r = 0; r < d->actor_record_count; r++) {
    const record_t *record = &d->actor_records[r]; if (!actor_uses_record(s, client, record)) continue;
    for (size_t f = 0; f < record->field_count; f++) {
      const field_t *field = &record->fields[f];
      qa_guest_address address = qa_compat_q2_mem_offset(memory, record_address(s, record, slot), (int64_t)field->offset), none; memset(&none, 0, sizeof none);
      if (!strcmp(field->binding, "constant") && constants) scalar_write(s, address, field->constant_value, field->encoding);
      else if (!strcmp(field->binding, "address")) { if (field->address_value == NULL) qa_compat_q2_mem_write_pointer(memory, address, false, none); else qa_compat_q2_mem_write_pointer(memory, address, true, resolve(s, *field->address_value, false)); }
      else if (!strcmp(field->binding, "constant-vector") && constants) qa_compat_q2_write_classic_vector(memory, address, field->qvm_field.constant_vector);
      else if (!strcmp(field->binding, "record")) { qa_guest_address linked; bool has = pointer_for(s, &actor, field->qvm_field.record, &linked); qa_compat_q2_mem_write_pointer(memory, address, has, has ? linked : none); }
      else if (constants && (!strcmp(field->binding, "bounds-min") || !strcmp(field->binding, "bounds-max"))) {
        qa_body_state_result body = qa_shared_body_table_read(s->services->bodies, actor);
        if (body.has_value) qa_compat_q2_write_classic_vector(memory, address, !strcmp(field->binding, "bounds-min") ? body.value.bounds.min : body.value.bounds.max);
      }
    }
  }
}
/* TS pointer(actor, id). False is TS null. */
static bool pointer_for(provider_t *s, const qa_actor_id *actor_ptr, const char *id, qa_guest_address *out) {
  if (actor_ptr == NULL) return false;
  qa_actor_id actor = *actor_ptr; double owned_slot; const qa_compat_q2_native_mod_host *host = host_of(s); const declaration_t *d = s->declaration;
  if (s->owned != NULL && qa_compat_q2_native_mod_actors_slot_of(s->owned, actor, &owned_slot)) {
    if (!eq(id, d->entity_record)) qa_range_error("Owned native actor requires its original edict record");
    *out = host->entity(host->ctx, owned_slot).address; return true;
  }
  const record_t *record = record_of(s, id); if (record == NULL) qa_range_error("Unknown native actor record");
  double client_slot = 0; bool has_client_slot = s->has_clients && qa_compat_q2_native_mod_clients_slot(&s->clients, actor, &client_slot);
  if (client_record(d, id) && !has_client_slot) return false;
  projection_row *row = find_projection(s, actor); double slot;
  if (row != NULL) slot = row->slot;
  else {
    if (!is_live(s, actor)) qa_range_error("Native mod cannot borrow an expired actor");
    validate_inventory_capacity(s, actor, clients_has(s, actor));
    release_pending(s);
    double next = has_client_slot ? client_slot : d->clients != NULL ? d->clients->maximum : 0;
    bool occupied = false;
    for (;;) {
      occupied = false;
      for (size_t i = 0; i < s->projection_count && !occupied; i++) occupied = s->projections[i].slot == next;
      if (!occupied || has_client_slot) break;
      next++;
    }
    if (has_client_slot && occupied) qa_range_error("Native source client row is already projected");
    bool client = clients_has(s, actor);
    for (size_t r = 0; r < d->actor_record_count; r++) if (actor_uses_record(s, client, &d->actor_records[r]) && next >= d->actor_records[r].capacity) qa_range_error("Native actor projection capacity exceeded");
    for (size_t r = 0; r < d->actor_record_count; r++) {
      const record_t *candidate = &d->actor_records[r];
      if (actor_uses_record(s, client, candidate) && !strcmp(candidate->base.kind, "entities") && candidate->first_slot + next >= host->entities(host->ctx).count) qa_range_error("Native actor projection requires a slot allocated by its authored initialization");
    }
    slot = next;
    projection_row *added = qa_compat_q2_grow((void **)&s->projections, &s->projection_count, &s->projection_capacity, sizeof *added); added->actor = actor; added->slot = slot;
    double source_slot; bool has_source = qa_compat_q2_native_mod_provider_slot_of(s, actor, &source_slot); char *appearance = NULL;
    if (has_source) { const char *text = host->presentation.signature(host->presentation.ctx, source_slot); size_t n = strlen(text) + 1; appearance = malloc(n); if (appearance == NULL) qa_range_error("Out of memory"); memcpy(appearance, text, n); }
    seed(s, actor, slot, true);
    if (!has_client_slot) for (size_t i = 0; i < d->project_count; i++) { input_buffer buffer; double result; (void)execute(s, &d->project[i], build_inputs(s, &actor, &buffer), false, NULL, NULL, &result); }
    seed(s, actor, slot, true);
    if (has_source && strcmp(appearance, host->presentation.signature(host->presentation.ctx, source_slot)) != 0) appearance_add(s, actor);
    free(appearance);
  }
  *out = record_address(s, record, slot); return true;
}
bool qa_compat_q2_native_mod_provider_slot_of(provider_t *s, qa_actor_id actor, double *out) {
  if (is_world(s, actor)) { *out = 0; return true; }
  if (s->owned != NULL && qa_compat_q2_native_mod_actors_slot_of(s->owned, actor, out)) return true;
  const projection_row *row = find_projection(s, actor);
  const record_t *definition = s->declaration->entity_record != NULL ? record_of(s, s->declaration->entity_record) : NULL;
  if (row == NULL || definition == NULL) return false;
  *out = definition->first_slot + row->slot; return true;
}
bool qa_compat_q2_native_mod_provider_actor_at(provider_t *s, double slot, qa_actor_id *out) {
  if (slot == 0) return world_actor(s, out);
  qa_owned_actor owned;
  if (s->owned != NULL && qa_compat_q2_native_mod_actors_actor_at(s->owned, slot, &owned)) { *out = owned.id; return true; }
  for (size_t i = 0; i < s->projection_count; i++) { double row_slot; if (qa_compat_q2_native_mod_provider_slot_of(s, s->projections[i].actor, &row_slot) && row_slot == slot && is_live(s, s->projections[i].actor)) { *out = s->projections[i].actor; return true; } }
  return false;
}
bool qa_compat_q2_native_mod_provider_accepts_client(provider_t *s, double slot) {
  qa_actor_id actor; if (!qa_compat_q2_native_mod_provider_actor_at(s, slot, &actor)) return false;
  if (s->has_clients) return qa_compat_q2_native_mod_clients_has(&s->clients, actor);
  const qa_session_mod_engine_services *engine = s->services->engine;
  if (engine == NULL || engine->presentation_players == NULL) return false;
  size_t count = 0; const qa_actor_id *players = engine->presentation_players(engine->ctx, &count);
  return actor_list_has(players, count, actor);
}
static const qa_app_simulation_presentation *scratch_finish(provider_t *s, size_t *out_count) { *out_count = s->scratch_count; return s->scratch; }
static void scratch_append(provider_t *s, const qa_app_simulation_presentation *rows, size_t count) {
  for (size_t i = 0; i < count; i++) { qa_app_simulation_presentation *row = qa_compat_q2_grow((void **)&s->scratch, &s->scratch_count, &s->scratch_capacity, sizeof *row); *row = rows[i]; }
}
const qa_app_simulation_presentation *qa_compat_q2_native_mod_provider_appearance_overrides(provider_t *s, size_t *out_count) {
  s->scratch_count = 0; const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
  for (size_t i = 0; i < s->appearance_count; i++) {
    qa_actor_id actor = s->appearance_actors[i]; double slot;
    if (qa_compat_q2_native_mod_provider_slot_of(s, actor, &slot) && is_live(s, actor)) { size_t n = 0; const qa_app_simulation_presentation *rows = p->appearance(p->ctx, actor, slot, &n); scratch_append(s, rows, n); }
  }
  return scratch_finish(s, out_count);
}
const qa_app_simulation_presentation *qa_compat_q2_native_mod_provider_presentations(provider_t *s, size_t *out_count) {
  s->scratch_count = 0;
  if (s->owned != NULL) {
    size_t count = 0; qa_compat_q2_native_mod_actor_slot *rows = qa_compat_q2_native_mod_actors_entries(s->owned, &count); const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
    for (size_t i = 0; i < count; i++) { size_t n = 0; const qa_app_simulation_presentation *items = p->appearance(p->ctx, rows[i].actor, rows[i].slot, &n); scratch_append(s, items, n); }
    free(rows);
  }
  return scratch_finish(s, out_count);
}
bool qa_compat_q2_native_mod_provider_project(provider_t *s, qa_raw_entity_view record, qa_owned_actor *out) {
  if (record.slot == 0) { qa_actor_id world; return world_actor(s, &world) && qa_session_actor_registry_resolve_owned(s->services->actors, world, out); }
  if (s->owned != NULL && qa_compat_q2_native_mod_actors_actor_at(s->owned, record.slot, out)) return true;
  const record_t *definition = s->declaration->entity_record != NULL ? record_of(s, s->declaration->entity_record) : NULL; if (definition == NULL) return false;
  double slot = record.slot - definition->first_slot;
  for (size_t i = 0; i < s->projection_count; i++) if (s->projections[i].slot == slot) return qa_session_actor_registry_resolve_owned(s->services->actors, s->projections[i].actor, out);
  return false;
}
qa_guest_address qa_compat_q2_native_mod_provider_address(provider_t *s, qa_actor_id actor) {
  const qa_compat_q2_native_mod_host *host = host_of(s); double slot;
  if (is_world(s, actor)) return host->entities(host->ctx).base;
  if (s->owned != NULL && qa_compat_q2_native_mod_actors_slot_of(s->owned, actor, &slot)) return host->entity(host->ctx, slot).address;
  if (s->declaration->entity_record == NULL) qa_range_error("Native mod requires a declared engine actor projection");
  qa_guest_address value;
  if (!pointer_for(s, &actor, s->declaration->entity_record, &value)) qa_range_error("Native actor projection returned null");
  return value;
}
/* --- Field projection. --- */
static qa_source_match_field match_field_of(const field_t *f) { qa_source_match_field m; m.is_score = !strcmp(f->binding, "score"); m.values = f->team_values; m.value_count = f->team_value_count; return m; }
static bool body_output_field(provider_t *s, qa_actor_id actor, const char *record, const field_t *field) {
  double slot; const qa_native_mod_clients *clients = s->declaration->clients;
  if (!s->has_clients || !qa_compat_q2_native_mod_clients_slot(&s->clients, actor, &slot) || clients == NULL) return false;
  for (size_t i = 0; i < clients->output_count; i++) {
    const qa_mod_client_output_declaration *output = &clients->outputs[i]; if (output->kind != QA_MOD_CLIENT_OUTPUT_BODY_SHAPE) continue;
    const qa_native_mod_record_offset *min = output->min_vector, *max = output->max_vector;
    if ((eq(min->record, record) && min->offset == field->offset) || (eq(max->record, record) && max->offset == field->offset)) return true;
  }
  return false;
}
static observation *observe_all(provider_t *s, size_t *out_count) {
  observation *values = NULL; size_t count = 0, capacity = 0; qa_compat_q2_memory *memory = memory_of(s); const declaration_t *d = s->declaration;
  for (size_t p = 0; p < s->projection_count; p++) {
    qa_actor_id actor = s->projections[p].actor; double slot = s->projections[p].slot; bool client = clients_has(s, actor);
    for (size_t r = 0; r < d->actor_record_count; r++) {
      const record_t *record = &d->actor_records[r]; if (!actor_uses_record(s, client, record)) continue;
      for (size_t f = 0; f < record->field_count; f++) {
        const field_t *field = &record->fields[f]; if (!shared(field) || body_output_field(s, actor, record->id, field)) continue;
        observation *o = qa_compat_q2_grow((void **)&values, &count, &capacity, sizeof *o);
        o->actor = actor; o->address = qa_compat_q2_mem_offset(memory, record_address(s, record, slot), (int64_t)field->offset); o->field = field; o->length = (size_t)size_of(field, memory->pointer_bytes);
        memory->copy(memory->ctx, o->address, o->bytes, (double)o->length);
      }
    }
  }
  *out_count = count; return values;
}
static void frame_observe(provider_t *s, invocation *frame) { free(frame->observations); frame->observations = observe_all(s, &frame->observation_count); }
static qa_guest_address objective_address(provider_t *s, const qa_native_mod_address *value) { return resolve(s, *value, true); }
static void projection_writes_done(void *ctx) { ((provider_t *)ctx)->projection_writes--; }
static void refresh_body(void *ctx) {
  provider_t *s = ctx; qa_compat_q2_memory *memory = memory_of(s); const declaration_t *d = s->declaration; const qa_compat_q2_native_mod_host *host = host_of(s);
  for (size_t p = 0; p < s->projection_count; p++) {
    qa_actor_id actor = s->projections[p].actor; double slot = s->projections[p].slot; bool client = clients_has(s, actor);
    validate_inventory_capacity(s, actor, client);
    size_t inventory_count = 0; const qa_inventory_entry *inventory = qa_shared_inventory_table_entries(s->services->inventory, actor, &inventory_count);
    for (size_t r = 0; r < d->actor_record_count; r++) {
      const record_t *record = &d->actor_records[r]; if (!actor_uses_record(s, client, record)) continue;
      for (size_t f = 0; f < record->field_count; f++) {
        const field_t *field = &record->fields[f]; if (!shared(field) || body_output_field(s, actor, record->id, field)) continue;
        qa_guest_address address = qa_compat_q2_mem_offset(memory, record_address(s, record, slot), (int64_t)field->offset);
        if (is_match(field)) scalar_write(s, address, qa_read_source_match_field(s->services->match, actor, match_field_of(field)), field->encoding);
        else if (!strcmp(field->binding, "health")) { qa_combat_state_result state = qa_gameplay_authority_read(s->services->combat, actor); scalar_write(s, address, state.has_value ? state.value.health : 0, field->encoding); }
        else if (is_inventory(field)) {
          double value = 0;
          for (size_t i = 0; i < inventory_count; i++) if (!strcmp(inventory[i].item, field->item)) { value = !strcmp(field->binding, "inventory") ? inventory[i].count : inventory[i].capacity; break; }
          scalar_write(s, address, value, field->encoding);
        } else {
          qa_body_state_result state = qa_shared_body_table_read(s->services->bodies, actor); qa_vec3 value = {0, 0, 0};
          if (state.has_value) value = !strcmp(field->binding, "bounds-min") ? state.value.bounds.min : !strcmp(field->binding, "bounds-max") ? state.value.bounds.max : !strcmp(field->binding, "origin") ? state.value.origin : !strcmp(field->binding, "velocity") ? state.value.velocity : state.value.angles;
          qa_compat_q2_write_classic_vector(memory, address, value);
        }
      }
    }
    const qa_native_mod_client_pose *pose = d->clients != NULL ? d->clients->pose : NULL;
    if (pose != NULL && clients_admitted(s, actor)) {
      const qa_mod_client_services *services = s->services->clients; qa_client_id client_id;
      if (services == NULL || services->player_view == NULL || !services->for_actor(services->ctx, actor, &client_id)) qa_range_error("Native client pose requires the live destination movement view");
      qa_mod_client_player_view view = services->player_view(services->ctx, client_id);
      const record_t *height_record = record_of(s, pose->view_height.record), *crouch_record = record_of(s, pose->crouched.field.record);
      if (height_record == NULL || crouch_record == NULL) qa_range_error("Native pose record is unavailable");
      qa_guest_address crouch_address = qa_compat_q2_mem_offset(memory, record_address(s, crouch_record, slot), (int64_t)pose->crouched.field.field.offset);
      qa_guest_address height_address = qa_compat_q2_mem_offset(memory, record_address(s, height_record, slot), (int64_t)pose->view_height.field.offset);
      double flags = scalar_read(s, crouch_address, pose->crouched.field.field.encoding); const char *height_encoding = pose->view_height.field.encoding;
      scalar_write(s, height_address, !strcmp(height_encoding, "float32") || !strcmp(height_encoding, "float64") ? view.view_offset.z : trunc(view.view_offset.z), height_encoding);
      int32_t mask = qa_js_to_int32(pose->crouched.mask), current_flags = qa_js_to_int32(flags); /* flags | mask: ToInt32 */
      scalar_write(s, crouch_address, (double)(view.crouched ? current_flags | mask : current_flags & ~mask), pose->crouched.field.field.encoding);
      double source_slot;
      if (!qa_compat_q2_native_mod_provider_slot_of(s, actor, &source_slot)) qa_range_error("Native pose lost its source entity");
      if (host->project_player_view != NULL) host->project_player_view(host->ctx, source_slot, view.view_offset.z);
    }
  }
  if (s->frame_count != 0) for (size_t i = 0; i < s->frame_count; i++) frame_observe(s, s->frames[i]);
}
static void refresh(provider_t *s) {
  qa_mod_source_objectives_refresh(s->objectives);
  s->projection_writes++;
  qa_compat_q2_native_mod_finally(refresh_body, s, projection_writes_done, s);
}
static pickup_scope *pickup_scope_current(const provider_t *s) {
  if (s->pickup_scope_count == 0) return NULL;
  pickup_scope *scope = s->pickup_scopes[s->pickup_scope_count - 1];
  invocation *top = s->frame_count == 0 ? NULL : s->frames[s->frame_count - 1];
  return scope->frame == top ? scope : NULL;
}
static bool execution_current(const qa_original_pickup_execution *execution) { return execution->current(execution->current_ctx); }
static void validate_pickup_writes(provider_t *s, const observation *const *changes, size_t count) {
  const pickup_scope *scope = pickup_scope_current(s); if (scope == NULL || count == 0 || s->projection_writes != 0) return;
  if (!execution_current(scope->execution)) qa_range_error("Original pickup resource binding is no longer current");
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, scope->actor, &owner)) qa_range_error("Original pickup recipient was retired");
  for (size_t i = 0; i < count; i++) {
    const field_t *field = changes[i]->field;
    if (!qa_same_actor(changes[i]->actor, scope->actor) || !is_inventory(field)) qa_range_error("Original pickup changed an undeclared resource");
    bool permitted = false, count_field = !strcmp(field->binding, "inventory"); const qa_pickup_writes *writes = &scope->execution->writes;
    for (size_t w = 0; w < writes->count && !permitted; w++) {
      const qa_pickup_write *write = &writes->values[w];
      if (write->kind == QA_PICKUP_RESOURCE_INVENTORY) permitted = eq(write->item, field->item) && (!strcmp(write->fields, "count-and-capacity") || !strcmp(write->fields, count_field ? "count" : "capacity"));
      else if (count_field) {
        size_t n = 0; const qa_item_id *items = qa_gameplay_authority_protection_inventory_items(s->services->combat, owner, write->channel, &n);
        for (size_t k = 0; k < n && !permitted; k++) permitted = eq(items[k], field->item);
      }
    }
    if (!permitted) qa_range_error("Original pickup changed undeclared %s %s", field->binding, field->item);
  }
}
static void flush_frames(provider_t *s, const qa_compat_q2_native_protection_inventory_commit *inventory_commit);
typedef struct configure_call { provider_t *owner; qa_actor_id actor; qa_item_id item; bool has_commit; qa_compat_q2_native_protection_inventory_commit commit; qa_inventory_committed_change change; } configure_call;
static void committed_writes(void *ctx) {
  configure_call *c = ctx; provider_t *s = c->owner;
  size_t count = 0; observation *all = observe_all(s, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_actor(all[i].actor, c->actor) && is_inventory(all[i].field) && eq(all[i].field->item, c->item))
    scalar_write(s, all[i].address, !strcmp(all[i].field->binding, "inventory") ? c->change.after.count : c->change.after.capacity, all[i].field->encoding);
  free(all);
}
static void inventory_committed(void *ctx, qa_inventory_committed_change change) {
  configure_call *c = ctx; provider_t *s = c->owner; c->change = change;
  if (c->has_commit && qa_same_actor(c->commit.actor, c->actor)) for (size_t i = 0; i < c->commit.item_count; i++) if (eq(c->commit.items[i], c->item)) { c->commit.committed(c->commit.ctx, change); break; }
  s->projection_writes++;
  qa_compat_q2_native_mod_finally(committed_writes, c, projection_writes_done, s);
  for (size_t i = 0; i < s->frame_count; i++) frame_observe(s, s->frames[i]);
  flush_frames(s, c->has_commit ? &c->commit : NULL);
}
typedef struct match_write { provider_t *s; const pending_commit *commit; } match_write;
static void match_write_body(void *ctx) { match_write *m = ctx; scalar_write(m->s, m->commit->address, qa_read_source_match_field(m->s->services->match, m->commit->actor, match_field_of(m->commit->field)), m->commit->field->encoding); }
static void run_commit(provider_t *s, const pending_commit *commit) {
  if (clients_rejects(s, commit->actor)) return;
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, commit->actor, &owner)) qa_range_error("Native mod wrote an expired actor");
  const field_t *field = commit->field;
  switch (commit->kind) {
    case COMMIT_MATCH:
      qa_write_source_match_field(s->services->match, commit->actor, match_field_of(field), commit->value);
      if (is_live(s, commit->actor) && find_projection(s, commit->actor) != NULL) {
        s->projection_writes++;
        match_write m = {s, commit};
        qa_compat_q2_native_mod_finally(match_write_body, &m, projection_writes_done, s);
      }
      break;
    case COMMIT_HEALTH: qa_gameplay_authority_set_health(s->services->combat, owner, commit->value); break;
    case COMMIT_INVENTORY: {
      size_t count = 0; const qa_inventory_entry *entries = qa_shared_inventory_table_entries(s->services->inventory, commit->actor, &count); bool found = false; qa_inventory_entry entry; memset(&entry, 0, sizeof entry);
      for (size_t i = 0; i < count && !found; i++) if (!strcmp(entries[i].item, field->item)) { entry = entries[i]; found = true; }
      if (!found) qa_range_error("Native mod wrote an undeclared destination item");
      if (commit->has_capacity && !qa_shared_inventory_table_mutable_capacity(s->services->inventory, commit->actor, field->item)) qa_range_error("Native mod requires mutable inventory capacity for %s", field->item);
      const pickup_scope *scope = pickup_scope_current(s);
      if (scope != NULL && !execution_current(scope->execution)) qa_range_error("Original pickup resource binding is no longer current");
      if (commit->has_count) entry.count = commit->count;
      if (commit->has_capacity) entry.capacity = commit->capacity;
      configure_call c; memset(&c, 0, sizeof c); c.owner = s; c.actor = commit->actor; c.item = field->item; c.has_commit = commit->has_commit; c.commit = commit->commit;
      bool observed = scope != NULL || commit->has_commit;
      qa_shared_inventory_table_configure(s->services->inventory, owner, entry, observed ? inventory_committed : NULL, observed ? &c : NULL);
      if (scope != NULL) {
        if (!execution_current(scope->execution)) qa_range_error("Original pickup resource binding is no longer current");
        refresh(s);
      }
      break;
    }
    case COMMIT_BODY: {
      qa_body_state_result body = qa_shared_body_table_read(s->services->bodies, commit->actor);
      if (!body.has_value) qa_range_error("Native mod wrote an actor without a body");
      qa_body_state next = body.value;
      if (!strcmp(field->binding, "bounds-min")) next.bounds.min = commit->vector;
      else if (!strcmp(field->binding, "bounds-max")) next.bounds.max = commit->vector;
      else if (!strcmp(field->binding, "origin")) next.origin = commit->vector;
      else if (!strcmp(field->binding, "velocity")) next.velocity = commit->vector;
      else next.angles = commit->vector;
      qa_shared_body_table_write(s->services->bodies, owner, next);
      break;
    }
  }
}
static void flush_frames(provider_t *s, const qa_compat_q2_native_protection_inventory_commit *inventory_commit) {
  qa_mod_source_objectives_flush(s->objectives);
  if (s->frame_count == 0) return;
  invocation *frame = s->frames[s->frame_count - 1]; qa_compat_q2_memory *memory = memory_of(s);
  /* A nested flush rewrites frame->observations; compare against this snapshot. */
  size_t change_capacity = frame->observation_count + 1;
  observation *changed = calloc(change_capacity, sizeof *changed); const observation **pointers = calloc(change_capacity, sizeof *pointers);
  if (changed == NULL || pointers == NULL) qa_range_error("Out of memory");
  size_t change_count = 0;
  for (size_t i = 0; i < frame->observation_count; i++) {
    uint8_t now[16]; const observation *o = &frame->observations[i];
    memory->copy(memory->ctx, o->address, now, (double)o->length);
    if (memcmp(now, o->bytes, o->length) != 0) { changed[change_count] = *o; pointers[change_count] = &changed[change_count]; change_count++; }
  }
  validate_pickup_writes(s, pointers, change_count);
  for (size_t i = 0; i < s->frame_count; i++) frame_observe(s, s->frames[i]);
  if (change_count == 0 && frame->cursor == frame->pending_count) { free(changed); free(pointers); return; }
  for (size_t i = 0; i < change_count; i++) {
    const observation *o = &changed[i]; const field_t *field = o->field; bool queued = false;
    pending_commit commit; memset(&commit, 0, sizeof commit); commit.actor = o->actor; commit.address = o->address; commit.field = field;
    if (is_match(field)) { commit.kind = COMMIT_MATCH; commit.value = scalar_read(s, o->address, field->encoding); queued = true; }
    else if (!strcmp(field->binding, "health")) { commit.kind = COMMIT_HEALTH; commit.value = scalar_read(s, o->address, field->encoding); queued = true; }
    else if (is_inventory(field)) {
      bool first = true;
      for (size_t j = 0; j < i && first; j++) if (is_inventory(changed[j].field) && qa_same_actor(changed[j].actor, o->actor) && eq(changed[j].field->item, field->item)) first = false;
      if (first) {
        commit.kind = COMMIT_INVENTORY; queued = true;
        for (size_t j = i; j < change_count; j++) {
          const observation *other = &changed[j]; if (!is_inventory(other->field) || !qa_same_actor(other->actor, o->actor) || !eq(other->field->item, field->item)) continue;
          double value = scalar_read(s, other->address, other->field->encoding);
          if (!strcmp(other->field->binding, "inventory")) { commit.has_count = true; commit.count = value; } else { commit.has_capacity = true; commit.capacity = value; }
        }
        if (inventory_commit != NULL) { commit.has_commit = true; commit.commit = *inventory_commit; }
      }
    } else {
      commit.kind = COMMIT_BODY; commit.vector = qa_compat_q2_read_classic_vector(memory, o->address);
      (void)scalar_value(commit.vector.x, "float32"); (void)scalar_value(commit.vector.y, "float32"); (void)scalar_value(commit.vector.z, "float32");
      queued = true;
    }
    if (queued) { pending_commit *slot = qa_compat_q2_grow((void **)&frame->pending, &frame->pending_count, &frame->pending_capacity, sizeof *slot); *slot = commit; }
  }
  free(changed); free(pointers);
  while (frame->cursor < frame->pending_count) { pending_commit * value2 = frame->pending; size_t value3 = frame->cursor++; pending_commit commit = value2[value3]; run_commit(s, &commit); }
  frame->pending_count = 0; frame->cursor = 0;
}
static void publish_client_outputs(provider_t *s) {
  if (!qa_source_mod_client_outputs_enabled(s->client_outputs)) return;
  for (size_t i = 0; i < s->projection_count; i++) {
    qa_actor_id actor = s->projections[i].actor;
    if (is_live(s, actor) && qa_source_mod_client_outputs_has(s->client_outputs, actor) && clients_admitted(s, actor)) qa_source_mod_client_outputs_publish(s->client_outputs, actor);
  }
}
/* TS entries(). Malloc'd rows. */
static qa_compat_q2_native_mod_actor_slot *entries_of(provider_t *s, size_t *out_count) {
  size_t owned_count = 0; qa_compat_q2_native_mod_actor_slot *owned = s->owned != NULL ? qa_compat_q2_native_mod_actors_entries(s->owned, &owned_count) : NULL;
  qa_compat_q2_native_mod_actor_slot *rows = calloc(owned_count + s->projection_count + 1, sizeof *rows); if (rows == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < owned_count; i++) rows[count++] = owned[i];
  free(owned);
  for (size_t i = 0; i < s->projection_count; i++) { double slot; qa_actor_id actor = s->projections[i].actor; if (qa_compat_q2_native_mod_provider_slot_of(s, actor, &slot) && is_live(s, actor)) { rows[count].actor = actor; rows[count].slot = slot; count++; } }
  *out_count = count; return rows;
}
static void publish(provider_t *s, bool events) {
  if (s->closing) return;
  publish_client_outputs(s);
  size_t count = 0; qa_compat_q2_native_mod_actor_slot *rows = entries_of(s, &count); const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
  p->publish(p->ctx, rows, count, events); free(rows);
}
/* --- Lowering source call values. --- */
typedef struct allocation { qa_guest_address address; double bytes; } allocation;
typedef struct correction { qa_guest_address address; qa_actor_id actor; char *before; double capacity; } correction;
typedef struct lower_state { allocation *allocations; size_t allocation_count, allocation_capacity; correction *corrections; size_t correction_count, correction_capacity; } lower_state;
static void add_allocation(lower_state *l, qa_guest_address address, double bytes) { allocation *a = qa_compat_q2_grow((void **)&l->allocations, &l->allocation_count, &l->allocation_capacity, sizeof *a); a->address = address; a->bytes = bytes; }
static void write_rerelease(void *ctx, uint8_t *view, size_t length, qa_q2_rerelease_user_command command) { (void)ctx; (void)length; qa_compat_q2_write_rerelease_user_command(view, command); }
static bool classic_target(const provider_t *s) { return s->declaration->target.api.kind == QA_Q2_GAME_API_CLASSIC; }
static qa_guest_call_value lower(provider_t *s, const value_t *value, inputs_t inputs, lower_state *l) {
  qa_compat_q2_memory *memory = memory_of(s); qa_guest_address none; memset(&none, 0, sizeof none);
  if (!strcmp(value->kind, "user-command")) {
    const input_scope *scope = s->input_context;
    if (scope == NULL) qa_range_error("Native user command requires its active input application");
    const qa_mod_client_application *application = scope->application; bool rerelease = !classic_target(s);
    qa_compat_q2_mod_command_inputs command; memset(&command, 0, sizeof command);
    inputs_t scoped = {scope->rows, scope->count};
    static const char *const names[6] = {"attack", "jump", "impulse", "forward-move", "side-move", "up-move"};
    double *fields[6] = {&command.attack, &command.jump, &command.impulse, &command.forward_move, &command.side_move, &command.up_move};
    for (int i = 0; i < 6; i++) {
      const qa_mod_runtime_value *row = find_input(scoped, names[i]);
      if (row == NULL || strcmp(row->kind, "float") != 0 || !isfinite(row->number_value)) qa_range_error("Missing native command input %s", names[i]);
      *fields[i] = row->number_value; command.has[i] = true;
    }
    qa_source_time elapsed = application->frame.elapsed; double elapsed_ms = elapsed.value * (elapsed.kind == QA_SOURCE_TIME_SECONDS ? 1000 : 1);
    double frame = s->owned != NULL ? qa_compat_q2_native_mod_actors_source_frame(s->owned) : 0;
    uint8_t bytes[28]; size_t length = qa_compat_q2_native_mod_user_command(bytes, rerelease, frame, elapsed_ms, application->absolute_aim, command, NULL, write_rerelease);
    qa_guest_address address = memory->allocate(memory->ctx, (double)length, false, 0, "native component command");
    add_allocation(l, address, (double)length); memory->write(memory->ctx, address, bytes, length);
    return qa_compat_q2_call_pointer(true, address);
  }
  if (!strcmp(value->kind, "address")) return value->address_value == NULL ? qa_compat_q2_call_pointer(false, none) : qa_compat_q2_call_pointer(true, resolve(s, *value->address_value, false));
  if (!strcmp(value->kind, "actor")) {
    const qa_mod_runtime_value *input = find_input(inputs, value->input);
    if (input == NULL || strcmp(input->kind, "actor") != 0) qa_range_error("Missing native actor input");
    qa_guest_address pointer; bool has = pointer_for(s, input->actor_value, value->record, &pointer);
    return qa_compat_q2_call_pointer(has, has ? pointer : none);
  }
  if (!strcmp(value->kind, "client") || !strcmp(value->kind, "userinfo")) {
    const qa_mod_runtime_value *input = find_input(inputs, value->input);
    if (input == NULL || strcmp(input->kind, "actor") != 0 || input->actor_value == NULL || !s->has_clients) qa_range_error("Missing native client input");
    qa_actor_id actor = *input->actor_value; double slot;
    if (!qa_compat_q2_native_mod_clients_slot(&s->clients, actor, &slot)) qa_range_error("Native client input does not identify a destination client");
    if (!strcmp(value->kind, "client")) return qa_compat_q2_call_int32(slot);
    double capacity = classic_target(s) ? 512 : 2048, bytes = capacity + 4;
    qa_guest_address address = memory->allocate(memory->ctx, bytes, false, 0, "native mod mutable userinfo");
    add_allocation(l, address, bytes);
    const char *before = qa_compat_q2_native_mod_clients_userinfo(&s->clients, actor); size_t before_length = strlen(before);
    if (classic_target(s)) qa_compat_q2_write_classic_string(memory, address, before, capacity);
    else {
      if ((double)before_length >= capacity) qa_range_error("Native userinfo exceeds its public API string limit");
      memory->write(memory->ctx, address, (const uint8_t *)before, before_length + 1);
    }
    correction *c = qa_compat_q2_grow((void **)&l->corrections, &l->correction_count, &l->correction_capacity, sizeof *c);
    c->address = address; c->actor = actor; c->capacity = capacity; c->before = malloc(before_length + 1); if (c->before == NULL) qa_range_error("Out of memory"); memcpy(c->before, before, before_length + 1);
    return qa_compat_q2_call_pointer(true, address);
  }
  if (!strcmp(value->kind, "time")) {
    const qa_mod_runtime_value *input = find_input(inputs, value->time_input);
    if (input == NULL || strcmp(input->kind, "float") != 0) qa_range_error("Missing native time input");
    return scalar_value(input->number_value * (!strcmp(value->units, "milliseconds") ? 1000 : 1), value->encoding);
  }
  qa_mod_runtime_value constant; memset(&constant, 0, sizeof constant); const qa_mod_runtime_value *resolved;
  bool named = !strcmp(value->value.kind, "input");
  if (named) resolved = find_input(inputs, value->value.name);
  else { constant.kind = value->value.kind; constant.number_value = value->value.number_value; constant.string_value = value->value.string_value; constant.vector_value = value->value.vector_value; resolved = &constant; }
  if (!strcmp(value->kind, "vector")) {
    if (resolved == NULL || strcmp(resolved->kind, "vector") != 0) qa_range_error("Missing native vector input");
    qa_guest_address address = memory->allocate(memory->ctx, 12, false, 0, "native mod vector argument");
    add_allocation(l, address, 12); qa_compat_q2_write_classic_vector(memory, address, resolved->vector_value);
    return qa_compat_q2_call_pointer(true, address);
  }
  if (!strcmp(value->kind, "string")) {
    if (resolved == NULL || strcmp(resolved->kind, "string") != 0) qa_range_error("Missing native string input");
    qa_guest_address address = qa_compat_q2_allocate_classic_string(memory, resolved->string_value);
    add_allocation(l, address, qa_compat_q2_classic_string_allocation_bytes(resolved->string_value));
    return qa_compat_q2_call_pointer(true, address);
  }
  if (resolved == NULL || strcmp(resolved->kind, "float") != 0) qa_range_error("Missing native scalar input");
  if (named && !strcmp(value->value.name, "pickup-count") && strcmp(value->kind, "float32") != 0 && strcmp(value->kind, "float64") != 0 && (!isfinite(resolved->number_value) || resolved->number_value != trunc(resolved->number_value)))
    qa_range_error("Native pickup count does not fit its declared integer ABI");
  return scalar_value(resolved->number_value, value->kind);
}
static void apply_corrections(provider_t *s, lower_state *l) {
  qa_compat_q2_memory *memory = memory_of(s);
  for (size_t i = 0; i < l->correction_count; i++) {
    const correction *c = &l->corrections[i]; char *after;
    if (classic_target(s)) after = qa_compat_q2_read_classic_string(memory, true, c->address, c->capacity);
    else {
      size_t capacity = (size_t)c->capacity; after = malloc(capacity); if (after == NULL) qa_range_error("Out of memory");
      memory->copy(memory->ctx, c->address, (uint8_t *)after, c->capacity);
      if (memchr(after, 0, capacity) == NULL) qa_range_error("Original native userinfo is unterminated");
    }
    if (strcmp(after, c->before) != 0) qa_compat_q2_native_mod_clients_set_userinfo(&s->clients, c->actor, after);
    free(after);
  }
}
static void lower_state_release(provider_t *s, lower_state *l, bool unmap) {
  if (unmap) { qa_compat_q2_memory *memory = memory_of(s); for (size_t i = l->allocation_count; i > 0; i--) memory->unmap(memory->ctx, l->allocations[i - 1].address, l->allocations[i - 1].bytes); }
  for (size_t i = 0; i < l->correction_count; i++) free(l->corrections[i].before);
  free(l->allocations); free(l->corrections); memset(l, 0, sizeof *l);
}
/* --- transfer, execute. --- */
static invocation *push_frame(provider_t *s, bool observed) {
  invocation *frame = calloc(1, sizeof *frame); if (frame == NULL) qa_range_error("Out of memory");
  if (observed) frame_observe(s, frame);
  invocation **slot = qa_compat_q2_grow((void **)&s->frames, &s->frame_count, &s->frame_capacity, sizeof *slot); *slot = frame;
  return frame;
}
static void pop_frame(provider_t *s) { invocation *frame = s->frames[--s->frame_count]; free(frame->observations); free(frame->pending); free(frame); }
static void finish_host_close(provider_t *s) {
  if (!s->pending_host_close || s->source_calls != 0) return;
  s->pending_host_close = false;
  if (s->host_ != NULL) s->host_->close(s->host_->ctx);
}
typedef struct transfer_call { provider_t *s; void (*invoke)(void *); void *ctx; } transfer_call;
static void transfer_body(void *value) {
  transfer_call *c = value; provider_t *s = c->s; c->invoke(c->ctx);
  if (s->closed || s->closing) return;
  flush_frames(s, NULL);
  if (s->owned == NULL || !qa_compat_q2_native_mod_actors_advancing(s->owned)) publish(s, true);
}
static void transfer_cleanup(void *value) { transfer_call *c = value; pop_frame(c->s); c->s->source_calls--; finish_host_close(c->s); }
static void transfer(provider_t *s, void (*invoke)(void *), void *ctx) {
  current(s); if (s->owned != NULL) qa_compat_q2_native_mod_actors_synchronize_clock(s->owned);
  flush_frames(s, NULL); refresh(s);
  (void)push_frame(s, true); s->source_calls++;
  transfer_call c = {s, invoke, ctx};
  qa_compat_q2_native_mod_finally(transfer_body, &c, transfer_cleanup, &c);
}
typedef struct execution_scope { provider_t *s; bool has_actor; qa_actor_id actor; bool has_client; qa_client_id client; } execution_scope;
static bool execution_scope_current(void *value) {
  execution_scope *e = value; provider_t *s = e->s;
  if (s->closed || s->closing) return false;
  if (!e->has_actor) return true;
  if (!is_live(s, e->actor)) return false;
  if (!e->has_client) return true;
  const qa_mod_client_services *clients = s->services->clients; qa_client_id now; qa_actor_id back;
  return clients != NULL && clients->for_actor(clients->ctx, e->actor, &now) && qa_same_client(now, e->client) && clients->actor(clients->ctx, e->client, &back) && qa_same_actor(back, e->actor);
}
static qa_compat_q2_native_mod_invocation_kind source_execution(provider_t *s, const qa_actor_id *actor, void (*invoke)(void *), void *ctx) {
  if (!s->has_invocations) { invoke(ctx); return QA_Q2INV_COMPLETED; }
  execution_scope e; memset(&e, 0, sizeof e); e.s = s;
  if (actor != NULL) { e.has_actor = true; e.actor = *actor; const qa_mod_client_services *clients = s->services->clients; e.has_client = clients != NULL && clients->for_actor(clients->ctx, *actor, &e.client); }
  return qa_compat_q2_native_mod_invocations_run(&s->invocations, &e, execution_scope_current, ctx, invoke);
}
typedef struct stored_global { qa_guest_address address; uint8_t bytes[16]; size_t length; } stored_global;
typedef struct current_call {
  provider_t *s; const call_t *call; inputs_t inputs; bool transfer_flag; const qa_native_mod_protection_region *region; const qa_compat_q2_native_mod_region_authority *authority;
  lower_state lowered; stored_global *globals; size_t global_count; bool frame_pushed;
  qa_guest_call_value *arguments; qa_guest_value_layout *layouts;
  qa_compat_q2_native_mod_region_execution *regional; qa_guest_address target; qa_compat_q2_guest_call_signature signature; qa_guest_call_result result; double value;
} current_call;
static void current_invoke(void *ctx) {
  current_call *c = ctx; const qa_compat_q2_native_mod_host *host = host_of(c->s);
  c->result = host->invoke(host->ctx, c->target, c->signature, c->arguments, c->call->argument_count);
}
static void region_bind(void *ctx) { current_call *c = ctx; qa_compat_q2_native_mod_region_execution_bind(c->regional, c->target); }
static void region_unbind(void *ctx) { current_call *c = ctx; qa_compat_q2_native_mod_region_execution_unbind(c->regional); }
static qa_guest_address call_target(provider_t *s, const call_t *call);
typedef struct appearance_row { qa_actor_id actor; char *signature; } appearance_row;
static appearance_row *capture_appearances(provider_t *s, size_t *out_count) {
  appearance_row *rows = calloc(s->projection_count + 1, sizeof *rows); if (rows == NULL) qa_range_error("Out of memory");
  const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation; size_t count = 0;
  for (size_t i = 0; i < s->projection_count; i++) {
    double slot; if (!qa_compat_q2_native_mod_provider_slot_of(s, s->projections[i].actor, &slot)) continue;
    const char *text = p->signature(p->ctx, slot); size_t n = strlen(text) + 1; char *copy = malloc(n); if (copy == NULL) qa_range_error("Out of memory"); memcpy(copy, text, n);
    rows[count].actor = s->projections[i].actor; rows[count].signature = copy; count++;
  }
  *out_count = count; return rows;
}
static void compare_appearances(provider_t *s, appearance_row *rows, size_t count) {
  const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
  for (size_t i = 0; i < count; i++) { double slot; if (qa_compat_q2_native_mod_provider_slot_of(s, rows[i].actor, &slot) && strcmp(rows[i].signature, p->signature(p->ctx, slot)) != 0) appearance_add(s, rows[i].actor); }
}
static void free_appearances(appearance_row *rows, size_t count) { if (rows == NULL) return; for (size_t i = 0; i < count; i++) free(rows[i].signature); free(rows); }
static void current_body(void *ctx) {
  current_call *c = ctx; provider_t *s = c->s; const call_t *call = c->call; const qa_compat_q2_native_mod_host *host = host_of(s); qa_compat_q2_memory *memory = host->memory;
  c->arguments = calloc(call->argument_count + 1, sizeof *c->arguments); c->layouts = calloc(call->argument_count + 1, sizeof *c->layouts);
  if (c->arguments == NULL || c->layouts == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < call->argument_count; i++) c->arguments[i] = lower(s, &call->arguments[i], c->inputs, &c->lowered);
  if (c->region != NULL) {
    qa_guest_call_value *values = calloc(c->region->input_count + 1, sizeof *values); if (values == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < c->region->input_count; i++) values[i] = lower(s, &c->region->inputs[i].value, c->inputs, &c->lowered);
    c->regional = qa_compat_q2_native_mod_region_execution_create(host, c->region, values, c->region->input_count, c->authority);
    free(values);
  }
  c->globals = calloc(call->global_count + 1, sizeof *c->globals); if (c->globals == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < call->global_count; i++) {
    const qa_native_mod_global_value *global = &call->globals[i];
    qa_guest_address address = resolve(s, global->address, false); qa_guest_call_value lowered = lower(s, &global->value, c->inputs, &c->lowered);
    const char *storage = value_storage(&global->value); bool vector = !strcmp(global->value.kind, "vector");
    size_t length = vector ? 12 : (size_t)qa_compat_q2_native_storage_bytes(storage, memory->pointer_bytes);
    stored_global *saved = &c->globals[c->global_count++]; saved->address = address; saved->length = length;
    memory->copy(memory->ctx, address, saved->bytes, (double)length);
    if (vector) {
      if (lowered.kind != QA_GUEST_VALUE_POINTER || !lowered.pointer_value.has_value) qa_range_error("Missing native vector storage");
      uint8_t bytes[12]; memory->copy(memory->ctx, lowered.pointer_value.value, bytes, 12); memory->write(memory->ctx, address, bytes, 12);
    } else if (lowered.kind == QA_GUEST_VALUE_POINTER) qa_compat_q2_mem_write_pointer(memory, address, lowered.pointer_value.has_value, lowered.pointer_value.value);
    else if (lowered.kind == QA_GUEST_VALUE_AGGREGATE) qa_range_error("Native mod aggregate global requires a declared layout");
    else { uint8_t bytes[8]; int n = qa_compat_q2_native_encode_scalar(memory, storage, &lowered, bytes); memory->write(memory->ctx, address, bytes, (size_t)n); }
  }
  appearance_row *appearances = NULL; size_t appearance_count = 0;
  if (c->transfer_flag) { appearances = capture_appearances(s, &appearance_count); refresh(s); (void)push_frame(s, true); c->frame_pushed = true; }
  c->target = call_target(s, call);
  memory->check(memory->ctx, c->target, 1, "execute");
  if (!s->has_stages) qa_range_error("Native source stages are unavailable");
  memset(&c->signature, 0, sizeof c->signature);
  for (size_t i = 0; i < call->argument_count; i++) { c->layouts[i].is_aggregate = false; c->layouts[i].storage = value_storage(&call->arguments[i]); }
  c->signature.abi = s->declaration->target.abi; c->signature.parameters = c->layouts; c->signature.parameter_count = call->argument_count; c->signature.variadic = false;
  c->signature.result_is_void = !strcmp(call->returns, "void");
  if (!c->signature.result_is_void) { c->signature.result.is_aggregate = false; c->signature.result.storage = call->returns; }
  qa_compat_q2_native_mod_stage_runner_run(&s->stages, call, c, current_invoke, c, c->regional != NULL ? region_bind : NULL, region_unbind);
  apply_corrections(s, &c->lowered);
  if (c->transfer_flag) { flush_frames(s, NULL); compare_appearances(s, appearances, appearance_count); publish(s, true); }
  free_appearances(appearances, appearance_count);
  if (c->regional != NULL) { qa_guest_call_value result = qa_compat_q2_native_mod_region_execution_result(c->regional); c->value = number_result(&result); }
  else c->value = number_result(&c->result);
}
static void current_cleanup(current_call *c) {
  provider_t *s = c->s; qa_compat_q2_memory *memory = s->host_ != NULL ? s->host_->memory : NULL;
  /* A mismatch is reported only after the restores and unmaps below have run. */
  bool ownership_changed = false;
  if (c->authority != NULL)
    ownership_changed = s->region_authority_count == 0 || s->region_authorities[--s->region_authority_count] != c->authority;
  if (c->frame_pushed) pop_frame(s);
  if (!s->closed && memory != NULL) for (size_t i = c->global_count; i > 0; i--) memory->write(memory->ctx, c->globals[i - 1].address, c->globals[i - 1].bytes, c->globals[i - 1].length);
  lower_state_release(s, &c->lowered, !s->closed);
  if (c->regional != NULL) qa_compat_q2_native_mod_region_execution_destroy(c->regional);
  free(c->globals); free(c->arguments); free(c->layouts);
  if (ownership_changed) qa_range_error("Native donor invocation ownership changed");
}
/* TS executeCurrent: the cancellation catch restores the processor and returns 0. */
static double execute_current(provider_t *s, const call_t *call, inputs_t inputs, bool transfer_flag, const qa_native_mod_protection_region *region, const qa_compat_q2_native_mod_region_authority *authority) {
  current(s); if (s->owned != NULL) qa_compat_q2_native_mod_actors_synchronize_clock(s->owned);
  if (transfer_flag) flush_frames(s, NULL);
  const qa_mod_runtime_value *self = find_input(inputs, "self"); const qa_compat_q2_runner *runner = host_of(s)->runner;
  bool has_cancellation = self != NULL && !strcmp(self->kind, "actor") && self->actor_value != NULL && s->items != NULL;
  qa_compat_q2_native_item_cancellation cancellation; memset(&cancellation, 0, sizeof cancellation);
  if (has_cancellation) cancellation = qa_compat_q2_native_mod_items_cancellation(s->items, *self->actor_value);
  void *processor = has_cancellation ? runner->capture_processor(runner->ctx) : NULL; double depth = qa_compat_q2_native_depth(runner);
  if (authority != NULL) { const qa_compat_q2_native_mod_region_authority **slot = qa_compat_q2_grow((void **)&s->region_authorities, &s->region_authority_count, &s->region_authority_capacity, sizeof *slot); *slot = authority; }
  current_call c; memset(&c, 0, sizeof c); c.s = s; c.call = call; c.inputs = inputs; c.transfer_flag = transfer_flag; c.region = region; c.authority = authority;
  qa_compat_q2_native_thrown thrown;
  bool completed = qa_compat_q2_native_try(current_body, &c, &thrown); double value = c.value; bool accepted = false;
  if (!completed && has_cancellation && qa_compat_q2_native_item_cancellation_accepts(&cancellation, &thrown)) {
    accepted = true; value = 0; qa_compat_q2_native_unwind(runner, depth); runner->restore_processor(runner->ctx, processor);
  }
  if (processor != NULL && runner->free_processor != NULL) runner->free_processor(runner->ctx, processor);
  current_cleanup(&c);
  if (!completed && !accepted) qa_compat_q2_native_rethrow(&thrown);
  return value;
}
typedef struct execute_call { provider_t *s; const call_t *call; inputs_t inputs; bool transfer_flag; const qa_native_mod_protection_region *region; const qa_compat_q2_native_mod_region_authority *authority; double value; bool has_value; } execute_call;
static void execute_invoke(void *ctx) { execute_call *e = ctx; e->value = execute_current(e->s, e->call, e->inputs, e->transfer_flag, e->region, e->authority); }
static void execute_inner(void *ctx) {
  execute_call *e = ctx;
  if (e->authority == NULL) { execute_invoke(e); return; }
  if (!e->s->has_invocations) qa_range_error("Native donor invocation is unavailable");
  qa_compat_q2_native_mod_invocations_guard(&e->s->invocations, e->authority, e, execute_invoke);
}
static void execute_body(void *ctx) {
  execute_call *e = ctx; const qa_mod_runtime_value *self = find_input(e->inputs, "self");
  const qa_actor_id *actor = self != NULL && !strcmp(self->kind, "actor") ? self->actor_value : NULL;
  e->has_value = source_execution(e->s, actor, execute_inner, e) != QA_Q2INV_RETIRED;
}
static void execute_cleanup(void *ctx) {
  execute_call *e = ctx; provider_t *s = e->s;
  if (!s->closed && e->transfer_flag && s->frame_count == 0) release_pending(s);
  s->source_calls--; finish_host_close(s);
}
static bool execute(provider_t *s, const call_t *call, inputs_t inputs, bool transfer_flag, const qa_native_mod_protection_region *region, const qa_compat_q2_native_mod_region_authority *authority, double *out) {
  s->source_calls++;
  execute_call e = {s, call, inputs, transfer_flag, region, authority, 0, false};
  qa_compat_q2_native_mod_finally(execute_body, &e, execute_cleanup, &e);
  if (e.has_value) *out = e.value;
  return e.has_value;
}
static qa_guest_address game_entry_of(provider_t *s, const call_t *call) {
  const qa_compat_q2_native_mod_host *host = host_of(s);
  qa_compat_q2_native_mod_game_entry entry = host->game_entry(host->ctx, call->entry.name); const qa_compat_q2_guest_call_signature *e = &entry.signature;
  bool same = eq(e->abi.kind, s->declaration->target.abi.kind) && eq(e->abi.call, s->declaration->target.abi.call) && e->parameter_count == call->argument_count && e->result_is_void == !strcmp(call->returns, "void") && !e->variadic;
  if (same && !e->result_is_void) same = !e->result.is_aggregate && eq(e->result.storage, call->returns);
  for (size_t i = 0; same && i < call->argument_count; i++) same = !e->parameters[i].is_aggregate && eq(e->parameters[i].storage, value_storage(&call->arguments[i]));
  if (!same) qa_range_error("Native component callback differs from its public game ABI");
  return entry.address;
}
static qa_guest_address call_target(provider_t *s, const call_t *call) {
  if (!strcmp(call->entry.kind, "game-export")) return game_entry_of(s, call);
  return qa_compat_q2_native_mod_entry_address(host_of(s), call->entry);
}
/* --- Releases. --- */
static void release_pending(provider_t *s) {
  if (s->frame_count != 0 || s->lifecycle || s->closed) return;
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_drain_releases(s->owned);
  s->lifecycle = true; const declaration_t *d = s->declaration; const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
  while (s->pending_release_count > 0) {
    qa_actor_id actor = s->pending_releases[0];
    if (!clients_has(s, actor)) for (size_t i = 0; i < d->release_count; i++) { input_buffer buffer; double result; (void)execute(s, &d->release[i], build_inputs(s, &actor, &buffer), false, NULL, NULL, &result); }
    p->release(p->ctx, actor); delete_projection(s, actor); actor_list_delete(s->appearance_actors, &s->appearance_count, actor);
    actor_list_delete(s->pending_releases, &s->pending_release_count, actor);
    if (s->has_clients) qa_compat_q2_native_mod_clients_forget(&s->clients, actor);
  }
  s->lifecycle = false;
}
static void on_actor_release(void *ctx, qa_owned_actor actor) {
  provider_t *s = ctx; qa_source_mod_client_outputs_release(s->client_outputs, actor.id);
  if (s->items != NULL) qa_compat_q2_native_mod_items_release(s->items, actor.id);
  qa_compat_q2_native_mod_pickups_release(s->pickups, actor.id);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_release(s->protection, actor.id);
  if (find_projection(s, actor.id) != NULL) { pending_release_add(s, actor.id); if (s->host_ != NULL) s->host_->presentation.release(s->host_->presentation.ctx, actor.id); }
}
/* --- Objectives. --- */
static bool objective_current(void *ctx) { provider_t *s = ctx; return !s->closed && !s->closing && !s->restoring; }
static const char *objective_location(void *ctx, const qa_source_objective_declaration *declaration) {
  provider_t *s = ctx; const qa_native_mod_objective_scalar *storage = declaration->state_storage; const qa_native_mod_address *carrier = declaration->carrier, *target = declaration->target;
  long long value = objective_address(s, &storage->address).byte_offset;
  long long value2 = carrier != NULL ? objective_address(s, carrier).byte_offset : -1;
  long long value3 = target != NULL ? objective_address(s, target).byte_offset : -1;
  long long values[3] = {value, value2, value3};
  (void)snprintf(s->location, sizeof s->location, "%lld|%lld|%lld", values[0], values[1], values[2]);
  return s->location;
}
static double objective_read_scalar(void *ctx, const void *storage) { provider_t *s = ctx; const qa_native_mod_objective_scalar *o = storage; return scalar_read(s, objective_address(s, &o->address), o->encoding); }
static void objective_write_scalar(void *ctx, const void *storage, double value) { provider_t *s = ctx; const qa_native_mod_objective_scalar *o = storage; scalar_write(s, objective_address(s, &o->address), value, o->encoding); }
static bool objective_read_actor(void *ctx, const void *storage, qa_actor_id *out) {
  provider_t *s = ctx; qa_guest_address pointer;
  const qa_compat_q2_memory * m = memory_of(s);
  qa_guest_address a = objective_address(s, storage);
  if (!qa_compat_q2_mem_read_pointer(m, a, &pointer)) return false;
  double slot = qa_compat_q2_native_mod_table_slot(host_of(s), pointer, "Native objective pointer is outside the source actor table");
  if (!qa_compat_q2_native_mod_provider_actor_at(s, slot, out)) qa_range_error("Native objective references an absent actor");
  return true;
}
static void objective_write_actor(void *ctx, const void *storage, const qa_actor_id *value) {
  provider_t *s = ctx; qa_guest_address pointer; memset(&pointer, 0, sizeof pointer);
  if (value != NULL) pointer = qa_compat_q2_native_mod_provider_address(s, *value);
  const qa_compat_q2_memory * m = memory_of(s);
  qa_guest_address a = objective_address(s, storage);
  qa_compat_q2_mem_write_pointer(m, a, value != NULL, pointer);
}
static void objective_invoke(void *ctx, const void *call, const qa_mod_objective_input_entry *inputs, size_t count) {
  provider_t *s = ctx; input_row *rows = calloc(count + 1, sizeof *rows); if (rows == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) { rows[i].name = inputs[i].name; rows[i].value = inputs[i].value; }
  inputs_t view = {rows, count}; double result; (void)execute(s, call, view, true, NULL, NULL, &result); free(rows);
}
static double objective_seconds(void *ctx) { return qa_compat_q2_time_seconds(qa_compat_q2_services_time(((provider_t *)ctx)->services)); }
/* --- Client outputs. --- */
static qa_guest_address client_output_address(provider_t *s, qa_actor_id actor, const char *record_id, double offset) {
  const record_t *record = record_of(s, record_id); const projection_row *row = find_projection(s, actor);
  if (record == NULL || row == NULL) qa_range_error("Native client output lost its source projection");
  const qa_compat_q2_memory * m = memory_of(s);
  qa_guest_address a = record_address(s, record, row->slot);
  return qa_compat_q2_mem_offset(m, a, (int64_t)offset);
}
static double output_scalar(void *ctx, qa_actor_id actor, const void *field) { provider_t *s = ctx; const qa_native_mod_armor_field *f = field; return scalar_read(s, client_output_address(s, actor, f->record, f->field.offset), f->field.encoding); }
static qa_vec3 output_vector(void *ctx, qa_actor_id actor, const void *field) { provider_t *s = ctx; const qa_native_mod_record_offset *f = field; const qa_compat_q2_memory * memory = memory_of(s); qa_guest_address address = client_output_address(s, actor, f->record, f->offset); return qa_compat_q2_read_classic_vector(memory, address); }
/* --- Pickups. --- */
static void pickups_current(void *ctx) { current(ctx); }
static bool pickups_eligible(void *ctx, qa_actor_id actor) { return clients_admitted(ctx, actor); }
typedef struct context_store { qa_guest_address address; uint8_t bytes[16]; size_t length; } context_store;
typedef struct context_call { provider_t *s; context_store *stores; size_t store_count; lower_state lowered; void *ctx; void (*execute)(void *); const qa_native_mod_pickup *definition; const qa_original_pickup_offer *offer; qa_compat_q2_native_pickup_inputs inputs; } context_call;
static void context_body(void *value) {
  context_call *c = value; provider_t *s = c->s; qa_compat_q2_memory *memory = memory_of(s); const declaration_t *d = s->declaration; const qa_compat_q2_native_mod_host *host = host_of(s);
  for (size_t i = 0; i < c->definition->context_count; i++) {
    const qa_native_mod_pickup_context *field = &c->definition->context[i];
    current(s);
    qa_guest_address base;
    if (!pointer_for(s, &c->offer->pickup, field->record, &base)) qa_range_error("Native pickup has no original source projection");
    if (eq(field->record, d->entity_record)) {
      double slot;
      if (!qa_compat_q2_native_mod_provider_slot_of(s, c->offer->pickup, &slot) || field->offset < host->entity(host->ctx, slot).public_layout.byte_length) qa_range_error("Native pickup context overlaps the public entity record");
    }
    qa_guest_address address = qa_compat_q2_mem_offset(memory, base, (int64_t)field->offset);
    qa_guest_call_value lowered = lower(s, &field->value, c->inputs, &c->lowered);
    const char *storage = value_storage(&field->value); bool vector = !strcmp(field->value.kind, "vector");
    size_t length = vector ? 12 : (size_t)qa_compat_q2_native_storage_bytes(storage, memory->pointer_bytes);
    context_store *saved = &c->stores[c->store_count++]; saved->address = address; saved->length = length;
    memory->copy(memory->ctx, address, saved->bytes, (double)length);
    if (vector) {
      if (lowered.kind != QA_GUEST_VALUE_POINTER || !lowered.pointer_value.has_value) qa_range_error("Native pickup vector storage is unavailable");
      uint8_t bytes[12]; memory->copy(memory->ctx, lowered.pointer_value.value, bytes, 12); memory->write(memory->ctx, address, bytes, 12);
    } else if (lowered.kind == QA_GUEST_VALUE_POINTER) qa_compat_q2_mem_write_pointer(memory, address, lowered.pointer_value.has_value, lowered.pointer_value.value);
    else if (lowered.kind == QA_GUEST_VALUE_AGGREGATE) qa_range_error("Native pickup requires a scalar context field");
    else { uint8_t bytes[8]; int n = qa_compat_q2_native_encode_scalar(memory, storage, &lowered, bytes); memory->write(memory->ctx, address, bytes, (size_t)n); }
  }
  c->execute(c->ctx);
}
static void context_cleanup(void *value) {
  context_call *c = value; provider_t *s = c->s;
  if (!s->closed) { qa_compat_q2_memory *memory = memory_of(s); for (size_t i = c->store_count; i > 0; i--) memory->write(memory->ctx, c->stores[i - 1].address, c->stores[i - 1].bytes, c->stores[i - 1].length); }
  lower_state_release(s, &c->lowered, !s->closed);
  free(c->stores);
}
static void pickups_context(void *ctx, const qa_native_mod_pickup *definition, const qa_original_pickup_offer *offer, qa_compat_q2_native_pickup_inputs inputs, void *execute_ctx, void (*execute_fn)(void *)) {
  provider_t *s = ctx; current(s); qa_owned_actor pickup_owner; double owned_slot; qa_client_id client; qa_actor_id world;
  bool foreign = !qa_same_actor(offer->pickup, offer->recipient)
      && !(qa_session_actor_registry_resolve_owned(s->services->actors, offer->pickup, &pickup_owner) && eq(pickup_owner.owner, s->instance))
      && !(s->owned != NULL && qa_compat_q2_native_mod_actors_slot_of(s->owned, offer->pickup, &owned_slot))
      && !(s->services->clients != NULL && s->services->clients->for_actor(s->services->clients->ctx, offer->pickup, &client))
      && !(world_actor(s, &world) && qa_same_actor(world, offer->pickup));
  if (!foreign) qa_range_error("Native pickup context requires a borrowed foreign pickup actor");
  context_call c; memset(&c, 0, sizeof c); c.s = s; c.ctx = execute_ctx; c.execute = execute_fn; c.definition = definition; c.offer = offer; c.inputs = inputs;
  c.stores = calloc(definition->context_count + 1, sizeof *c.stores); if (c.stores == NULL) qa_range_error("Out of memory");
  qa_compat_q2_native_mod_finally(context_body, &c, context_cleanup, &c);
}
typedef struct observe_pickup_call { provider_t *s; qa_actor_id actor; const qa_original_pickup_execution *observer; void *ctx; void (*execute)(void *); pickup_scope scope; uint64_t *removers; size_t remover_count; } observe_pickup_call;
static void pickup_written(void *ctx, const qa_compat_q2_written_range *ranges, size_t count) {
  (void)ranges; (void)count; /* TS observer ignores its ranges. */
  pickup_scope *scope = ctx; provider_t *s = scope->owner;
  if (s->pickup_scope_count == 0 || s->pickup_scopes[s->pickup_scope_count - 1] != scope || s->frame_count == 0 || s->frames[s->frame_count - 1] != scope->frame || s->projection_writes != 0) return;
  if (!execution_current(scope->execution)) qa_range_error("Original pickup resource binding is no longer current");
  flush_frames(s, NULL);
}
static void observe_pickup_body(void *value) {
  observe_pickup_call *c = value; provider_t *s = c->s; const declaration_t *d = s->declaration; const qa_compat_q2_native_mod_host *host = host_of(s);
  c->removers = calloc(s->projection_count * d->actor_record_count + 1, sizeof *c->removers); if (c->removers == NULL) qa_range_error("Out of memory");
  for (size_t p = 0; p < s->projection_count; p++) {
    bool client = clients_has(s, s->projections[p].actor);
    for (size_t r = 0; r < d->actor_record_count; r++) {
      const record_t *record = &d->actor_records[r]; if (!actor_uses_record(s, client, record)) continue;
      c->removers[c->remover_count++] = host->observe_writes(host->ctx, record_address(s, record, s->projections[p].slot), record->stride, &c->scope, pickup_written);
    }
  }
  c->execute(c->ctx);
}
static void observe_pickup_cleanup(void *value) {
  observe_pickup_call *c = value; const qa_compat_q2_native_mod_host *host = host_of(c->s);
  for (size_t i = 0; i < c->remover_count; i++) host->unobserve_writes(host->ctx, c->removers[i]);
  free(c->removers); c->s->pickup_scope_count--;
}
static void observe_pickup(void *value) {
  observe_pickup_call *c = value; provider_t *s = c->s;
  if (s->frame_count == 0) qa_range_error("Pickup requires its native source frame");
  c->scope.actor = c->actor; c->scope.execution = c->observer; c->scope.frame = s->frames[s->frame_count - 1]; c->scope.owner = s;
  pickup_scope **slot = qa_compat_q2_grow((void **)&s->pickup_scopes, &s->pickup_scope_count, &s->pickup_scope_capacity, sizeof *slot); *slot = &c->scope;
  qa_compat_q2_native_mod_finally(observe_pickup_body, c, observe_pickup_cleanup, c);
}
static void pickups_observe(void *ctx, qa_actor_id actor, const qa_original_pickup_execution *observer, void *execute_ctx, void (*execute_fn)(void *)) {
  provider_t *s = ctx; observe_pickup_call c; memset(&c, 0, sizeof c); c.s = s; c.actor = actor; c.observer = observer; c.ctx = execute_ctx; c.execute = execute_fn;
  if (s->protection == NULL) transfer(s, observe_pickup, &c);
  else qa_compat_q2_native_mod_protection_observe(s->protection, actor, observer->observer, &c, observe_pickup, NULL, NULL);
}
static bool pickups_invoke(void *ctx, const call_t *call, qa_compat_q2_native_pickup_inputs inputs, double *out) { return execute(ctx, call, inputs, false, NULL, NULL, out); }
/* --- Protection operations. --- */
static void protection_current(void *ctx) { current(ctx); }
static const qa_original_pickup_rule *protection_pickups(void *ctx, qa_actor_id actor, qa_protection_channel channel, size_t *out_count) { return qa_compat_q2_native_mod_pickups_protection(((provider_t *)ctx)->pickups, actor, channel, out_count); }
static bool protection_slot(void *ctx, qa_actor_id actor, double *out) { return qa_compat_q2_native_mod_provider_slot_of(ctx, actor, out); }
static double shared_scalar(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value) {
  provider_t *s = ctx; qa_guest_address address = qa_compat_q2_mem_offset(memory_of(s), base, (int64_t)field.offset);
  if (has_value) scalar_write(s, address, value, field.encoding);
  return scalar_read(s, address, field.encoding);
}
static void protection_transfer(void *ctx, void (*invoke)(void *), void *invoke_ctx) { transfer(ctx, invoke, invoke_ctx); }
static void protection_flush(void *ctx, const qa_compat_q2_native_protection_inventory_commit *inventory) { flush_frames(ctx, inventory); }
static bool protection_invoke(void *ctx, const call_t *call, qa_compat_q2_native_pickup_inputs inputs, const qa_native_mod_protection_region *region, const qa_compat_q2_native_mod_region_authority *authority, double *out) { return execute(ctx, call, inputs, false, region, authority, out); }
provider_t *qa_compat_q2_native_mod_provider_create(const declaration_t *declaration, const qa_mod_host_services *services, qa_provider_id instance, const char *map, void *assert_ctx, void (*assert_current)(void *)) {
  provider_t *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->declaration = declaration; s->services = services; s->instance = instance; s->map = map; s->assert_ctx = assert_ctx; s->assert_current = assert_current;
  qa_mod_objective_storage storage; memset(&storage, 0, sizeof storage);
  storage.ctx = s; storage.current = objective_current; storage.location = objective_location; storage.read_scalar = objective_read_scalar; storage.write_scalar = objective_write_scalar;
  storage.read_actor = objective_read_actor; storage.write_actor = objective_write_actor; storage.invoke = objective_invoke; storage.seconds = objective_seconds;
  s->objectives = qa_mod_source_objectives_create(instance, declaration->objectives, declaration->objective_count, services->match, storage); if (s->objectives == NULL) qa_range_error("Out of memory");
  qa_compat_q2_validate_native_mod_declaration(declaration);
  const qa_native_mod_clients *clients = declaration->clients; const qa_mod_client_services *client_services = services->clients;
  bool has_claim = client_services != NULL && client_services->claim_outputs != NULL;
  s->client_outputs = qa_source_mod_client_outputs_create(instance, clients != NULL ? clients->outputs : NULL, clients != NULL ? clients->output_count : 0, has_claim, has_claim ? client_services->claim_outputs : NULL, has_claim ? client_services->ctx : NULL, output_scalar, output_vector, s);
  if (s->client_outputs == NULL) qa_range_error("Out of memory");
  qa_compat_q2_native_mod_pickup_operations pickups; memset(&pickups, 0, sizeof pickups);
  pickups.ctx = s; pickups.current = pickups_current; pickups.eligible = pickups_eligible; pickups.context = pickups_context; pickups.observe = pickups_observe; pickups.invoke = pickups_invoke;
  s->pickups = qa_compat_q2_native_mod_pickups_create(declaration->pickups, declaration->pickup_count, services, instance, pickups);
  if (declaration->protection_count != 0) {
    qa_compat_q2_native_protection_operations operations; memset(&operations, 0, sizeof operations);
    operations.ctx = s; operations.current = protection_current; operations.pickups = protection_pickups; operations.slot = protection_slot; operations.eligible = pickups_eligible;
    operations.scalar = shared_scalar; operations.transfer = protection_transfer; operations.flush = protection_flush; operations.invoke = protection_invoke;
    s->protection = qa_compat_q2_native_mod_protection_create(declaration->protection, declaration->protection_count, declaration, services, instance, operations);
  }
  s->subscription = qa_session_actor_registry_on_release(services->actors, on_actor_release, s); s->subscribed = true;
  return s;
}
void qa_compat_q2_native_mod_provider_reserve_protection(provider_t *s) { current(s); if (s->protection != NULL) qa_compat_q2_native_mod_protection_reserve(s->protection); }
/* --- Match. --- */
static qa_guest_address match_address(match_binding *b, double offset) {
  provider_t *s = b->owner; current(s); qa_owned_actor owned; double slot;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, b->actor, &owned) || !qa_same_owned_actor(owned, b->owned) || s->owned == NULL || !qa_compat_q2_native_mod_actors_slot_of(s->owned, b->actor, &slot) || slot != b->slot) qa_range_error("Native match actor was retired");
  const qa_compat_q2_native_mod_host *host = host_of(s); return qa_compat_q2_mem_offset(host->memory, host->entity(host->ctx, slot).address, (int64_t)offset);
}
static const char *match_team(void *ctx) { match_binding *b = ctx; if (b->team == NULL) return NULL; return qa_source_team(b->team->team_values, b->team->team_value_count, scalar_read(b->owner, match_address(b, b->team->offset), b->team->encoding)); }
static double match_score(void *ctx) { match_binding *b = ctx; if (b->score == NULL) qa_range_error("Native match actor has no score field"); return scalar_read(b->owner, match_address(b, b->score->offset), b->score->encoding); }
static void match_set_team(void *ctx, const char *value) { match_binding *b = ctx; if (b->team == NULL) qa_range_error("Native match actor has no team field"); provider_t * s = b->owner; qa_guest_address address = match_address(b, b->team->offset); double value2 = qa_original_team(b->team->team_values, b->team->team_value_count, value); scalar_write(s, address, value2, b->team->encoding); }
static void match_set_score(void *ctx, double value) { match_binding *b = ctx; if (b->score == NULL) qa_range_error("Native match actor has no score field"); scalar_write(b->owner, match_address(b, b->score->offset), value, b->score->encoding); }
static qa_source_match_player_result match_player(void *ctx, qa_actor_id actor) {
  provider_t *s = ctx; qa_source_match_player_result none; memset(&none, 0, sizeof none); qa_owned_actor owned; double slot;
  const record_t *record = s->declaration->entity_record != NULL ? record_of(s, s->declaration->entity_record) : NULL;
  if (s->closed || s->closing || !qa_session_actor_registry_resolve_owned(s->services->actors, actor, &owned) || !eq(owned.owner, s->instance) || s->owned == NULL || !qa_compat_q2_native_mod_actors_slot_of(s->owned, actor, &slot) || record == NULL) return none;
  match_binding *b = calloc(1, sizeof *b); if (b == NULL) qa_range_error("Out of memory");
  b->owner = s; b->actor = actor; b->owned = owned; b->slot = slot; b->next = s->matches; s->matches = b;
  for (size_t f = 0; f < record->field_count; f++) { const field_t *field = &record->fields[f]; if (b->team == NULL && !strcmp(field->binding, "team")) b->team = field; if (b->score == NULL && !strcmp(field->binding, "score")) b->score = field; }
  qa_source_match_player_result result; memset(&result, 0, sizeof result); result.has_value = true;
  result.value.ctx = b; result.value.owner = s->instance; result.value.team = match_team; result.value.score = match_score; result.value.set_team = match_set_team; result.value.set_score = match_set_score;
  return result;
}
void qa_compat_q2_native_mod_provider_activate_protection(provider_t *s) {
  current(s); qa_mod_source_objectives_activate(s->objectives); const declaration_t *d = s->declaration; bool has_match_field = false;
  for (size_t r = 0; r < d->actor_record_count; r++) for (size_t f = 0; f < d->actor_records[r].field_count; f++) if (is_match(&d->actor_records[r].fields[f])) has_match_field = true;
  if (!s->match_bound && has_match_field) {
    if (s->services->match == NULL) qa_range_error("Declared match fields require destination match services");
    s->release_match = s->services->match->bind_source(s->services->match->ctx, s->instance, match_player, s); s->match_bound = true;
  }
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_activate(s->protection);
  qa_compat_q2_native_mod_pickups_activate(s->pickups);
}
/* --- Input scopes. --- */
static void input_scope_close(input_scope *scope, bool failed) {
  provider_t *s = scope->owner; size_t index = (size_t)-1;
  for (size_t i = 0; i < s->input_scope_count; i++) if (s->input_scopes[i] == scope) { index = i; break; }
  if (index == (size_t)-1) return;
  memmove(&s->input_scopes[index], &s->input_scopes[index + 1], (s->input_scope_count - index - 1) * sizeof *s->input_scopes); s->input_scope_count--;
  bool outer_live = false;
  if (scope->outer != NULL) for (size_t i = 0; i < s->input_scope_count; i++) if (s->input_scopes[i] == scope->outer) outer_live = true;
  if (failed || outer_live) {
    qa_actor_id actor = scope->application->identity.actor; const qa_mod_client_services *clients = s->services->clients; qa_compat_q2_memory *memory = memory_of(s);
    for (size_t i = 0; i < scope->store_count; i++) {
      qa_actor_id back; qa_client_id forward; qa_client_id client = scope->application->identity.client;
      if (s->closed || !is_live(s, actor) || clients == NULL || !clients->actor(clients->ctx, client, &back) || !qa_same_actor(back, actor) || !clients->for_actor(clients->ctx, actor, &forward) || !qa_same_client(forward, client)) break;
      memory->write(memory->ctx, scope->stores[i].address, scope->stores[i].previous, scope->stores[i].length);
    }
  }
  if (s->input_context == scope) s->input_context = NULL;
  free(scope->stores); free(scope);
}
static void input_scope_cleanup(void *ctx) { input_scope_close(ctx, false); }
static void open_input(void *ctx, qa_mod_client_application *application, void (**cleanup)(void *), void **cleanup_ctx) {
  provider_t *s = ctx; current(s); qa_compat_q2_memory *memory = memory_of(s); qa_actor_id actor = application->identity.actor; const qa_native_mod_clients *clients = s->declaration->clients;
  const projection_row *row = find_projection(s, actor); if (row == NULL) qa_range_error("Native input client has no source row");
  double slot = row->slot;
  input_scope *scope = calloc(1, sizeof *scope); if (scope == NULL) qa_range_error("Out of memory");
  scope->owner = s; scope->application = application;
  qa_mod_client_input_values values = qa_mod_client_input_values_of(application);
  for (size_t i = 0; i < values.count; i++) { scope->rows[i].name = values.entries[i].name; scope->rows[i].value = values.entries[i].value; }
  scope->count = values.count; scope->self = actor;
  for (size_t i = 0; i < scope->count; i++) if (!strcmp(scope->rows[i].name, "self")) scope->rows[i].value.actor_value = &scope->self;
  if (s->items != NULL && !qa_compat_q2_native_mod_items_accepts_attack(s->items, actor)) for (size_t i = 0; i < scope->count; i++) if (!strcmp(scope->rows[i].name, "attack")) { memset(&scope->rows[i].value, 0, sizeof scope->rows[i].value); scope->rows[i].value.kind = "float"; scope->rows[i].value.number_value = 0; }
  for (size_t i = 0; i < s->input_scope_count; i++) if (qa_same_actor(s->input_scopes[i]->application->identity.actor, actor)) scope->outer = s->input_scopes[i];
  size_t field_count = clients != NULL ? clients->input_field_count : 0; inputs_t view = {scope->rows, scope->count};
  scope->stores = calloc(field_count + 1, sizeof *scope->stores); if (scope->stores == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < field_count; i++) {
    const qa_native_mod_client_input_field *field = &clients->input_fields[i]; const record_t *record = record_of(s, field->record);
    if (record == NULL) qa_range_error("Native input record is unavailable");
    input_store *store = &scope->stores[scope->store_count++]; store->address = qa_compat_q2_mem_offset(memory, record_address(s, record, slot), (int64_t)field->offset);
    if (!strcmp(field->value.kind, "vector")) {
      qa_vec3 vector;
      if (!strcmp(field->value.value.kind, "input")) { const qa_mod_runtime_value *input = find_input(view, field->value.value.name); if (input == NULL || strcmp(input->kind, "vector") != 0) qa_range_error("Native input vector is unavailable"); vector = input->vector_value; }
      else { if (strcmp(field->value.value.kind, "vector") != 0) qa_range_error("Native input vector is unavailable"); vector = field->value.value.vector_value; }
      (void)scalar_value(vector.x, "float32"); (void)scalar_value(vector.y, "float32"); (void)scalar_value(vector.z, "float32");
      qa_compat_q2_view_set_f32(store->bytes, 0, vector.x); qa_compat_q2_view_set_f32(store->bytes, 4, vector.y); qa_compat_q2_view_set_f32(store->bytes, 8, vector.z); store->length = 12;
    } else {
      lower_state none; memset(&none, 0, sizeof none); qa_guest_call_value lowered = lower(s, &field->value, view, &none);
      store->length = (size_t)qa_compat_q2_native_encode_scalar(memory, value_scalar_kind(&field->value), &lowered, store->bytes);
      lower_state_release(s, &none, true);
    }
    memory->check(memory->ctx, store->address, (double)store->length, "write");
    memory->copy(memory->ctx, store->address, store->previous, (double)store->length);
  }
  input_scope **slot_scope = qa_compat_q2_grow((void **)&s->input_scopes, &s->input_scope_count, &s->input_scope_capacity, sizeof *slot_scope); *slot_scope = scope;
  const qa_mod_client_services *services = s->services->clients;
  for (size_t i = 0; i < scope->store_count; i++) {
    current(s); qa_actor_id back; qa_client_id forward; qa_client_id client = application->identity.client;
    if (s->closed || !is_live(s, actor) || services == NULL || !services->actor(services->ctx, client, &back) || !qa_same_actor(back, actor) || !services->for_actor(services->ctx, actor, &forward) || !qa_same_client(forward, client)) break;
    memory->write(memory->ctx, scope->stores[i].address, scope->stores[i].bytes, scope->stores[i].length);
  }
  *cleanup = input_scope_cleanup; *cleanup_ctx = scope;
}
typedef struct input_invoke_call { provider_t *s; const call_t *call; input_scope *scope, *previous; } input_invoke_call;
static void input_invoke_body(void *value) { input_invoke_call *c = value; inputs_t view = {c->scope->rows, c->scope->count}; double result; (void)execute(c->s, c->call, view, true, NULL, NULL, &result); }
static void input_invoke_cleanup(void *value) { input_invoke_call *c = value; c->s->input_context = c->previous; }
static void invoke_input(void *ctx, const call_t *call, qa_mod_client_application *application) {
  provider_t *s = ctx; input_scope *scope = NULL;
  for (size_t i = 0; i < s->input_scope_count && scope == NULL; i++) if (s->input_scopes[i]->application == application) scope = s->input_scopes[i];
  if (scope == NULL) qa_range_error("Native input callback has no active application");
  input_invoke_call c = {s, call, scope, s->input_context}; s->input_context = scope;
  qa_compat_q2_native_mod_finally(input_invoke_body, &c, input_invoke_cleanup, &c);
}
typedef struct handler_state { provider_t *s; qa_mod_client_application *application; const qa_native_mod_input_output *output; size_t index; bool has_expected; qa_guest_address expected; bool called; uint64_t observer; qa_guest_value_layout *layouts; } handler_state;
typedef struct field_state { const qa_native_mod_input_output *output; const char *name; const char *kind; qa_guest_address address; size_t length; uint8_t before[16]; } field_state;
static bool output_live(provider_t *s, qa_actor_id actor) { return !s->closed && is_live(s, actor) && clients_admitted(s, actor); }
static void handler_observed(void *ctx) {
  handler_state *h = ctx; provider_t *s = h->s;
  if (s->input_context == NULL || s->input_context->application != h->application || !output_live(s, h->application->identity.actor)) return;
  qa_compat_q2_guest_call_signature signature; memset(&signature, 0, sizeof signature);
  signature.abi = s->declaration->target.abi; signature.parameters = h->layouts; signature.parameter_count = h->output->argument_count; signature.result_is_void = true; signature.variadic = false;
  qa_guest_call_value *actual = calloc(h->output->argument_count + 1, sizeof *actual); if (actual == NULL) qa_range_error("Out of memory");
  const qa_compat_q2_runner *runner = host_of(s)->runner;
  size_t count = runner->adapter_arguments(runner->ctx, signature, actual, h->output->argument_count);
  if (h->index < count && actual[h->index].kind == QA_GUEST_VALUE_POINTER && actual[h->index].pointer_value.has_value && h->has_expected && actual[h->index].pointer_value.value.byte_offset == h->expected.byte_offset) h->called = true;
  free(actual);
}
static void input_output_fn(void *ctx, const qa_native_mod_input_output *outputs, size_t output_count, qa_mod_client_application *application, void (*run)(void *), void *run_ctx, qa_mod_client_input_output **out_outputs, size_t *out_count) {
  provider_t *s = ctx; qa_compat_q2_memory *memory = memory_of(s); const qa_compat_q2_native_mod_host *host = host_of(s); qa_actor_id actor = application->identity.actor; const qa_native_mod_clients *clients = s->declaration->clients;
  const projection_row *row = find_projection(s, actor); if (row == NULL) qa_range_error("Native output client has no source row");
  double slot = row->slot;
  handler_state *handlers = calloc(output_count + 1, sizeof *handlers); field_state *fields = calloc(output_count + 1, sizeof *fields);
  if (handlers == NULL || fields == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < output_count; i++) {
    const qa_native_mod_input_output *output = &outputs[i];
    if (!strcmp(output->kind, "handler")) {
      handler_state *h = &handlers[i]; h->s = s; h->application = application; h->output = output;
      qa_guest_address address = qa_compat_q2_native_mod_entry_address(host, output->entry);
      const value_t *self_value = NULL;
      for (size_t a = 0; a < output->argument_count && self_value == NULL; a++) if (!strcmp(output->arguments[a].kind, "actor") && eq(output->arguments[a].input, "self")) { self_value = &output->arguments[a]; h->index = a; }
      if (self_value == NULL) qa_range_error("Native output handler lacks its client actor argument");
      h->has_expected = pointer_for(s, &actor, self_value->record, &h->expected);
      h->layouts = calloc(output->argument_count + 1, sizeof *h->layouts); if (h->layouts == NULL) qa_range_error("Out of memory");
      for (size_t a = 0; a < output->argument_count; a++) { h->layouts[a].is_aggregate = false; h->layouts[a].storage = value_storage(&output->arguments[a]); }
      h->observer = host->runner->observe_entry(host->runner->ctx, address, h, handler_observed);
      continue;
    }
    const qa_native_mod_client_input_field *field = NULL; const record_t *record = record_of(s, output->record);
    if (clients != NULL) for (size_t f = 0; f < clients->input_field_count && field == NULL; f++) if (eq(clients->input_fields[f].record, output->record) && clients->input_fields[f].offset == output->offset) field = &clients->input_fields[f];
    if (field == NULL || record == NULL || !strcmp(field->value.kind, "time") || !eq(field->value.value.kind, "input")) qa_range_error("Native output lacks its declared input field");
    const char *name = field->value.value.name;
    if (!one_of(name, NAMES("view-angles", "attack", "jump", "impulse", "forward-move", "side-move", "up-move"))) qa_range_error("Native output field is not a client input");
    field_state *state = &fields[i]; state->output = output; state->name = name; state->kind = field->value.kind;
    state->address = qa_compat_q2_mem_offset(memory, record_address(s, record, slot), (int64_t)field->offset);
    state->length = !strcmp(state->kind, "vector") ? 12 : (size_t)scalar_size(state->kind);
    memory->copy(memory->ctx, state->address, state->before, (double)state->length);
  }
  run(run_ctx);
  qa_mod_client_input_output *result = calloc(output_count + 1, sizeof *result); if (result == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  if (output_live(s, actor)) for (size_t i = 0; i < output_count; i++) {
    const qa_native_mod_input_output *output = &outputs[i];
    if (!strcmp(output->kind, "handler")) {
      if (handlers[i].called) { result[count].kind = QA_MOD_CLIENT_INPUT_OUTPUT_CONSUME; result[count].inputs = output->inputs; result[count].input_count = output->input_count; count++; }
      continue;
    }
    field_state *state = &fields[i]; uint8_t now[16]; memory->copy(memory->ctx, state->address, now, (double)state->length);
    if (memcmp(now, state->before, state->length) == 0) continue;
    bool view_angles = !strcmp(state->name, "view-angles"), vector = !strcmp(state->kind, "vector");
    result[count].kind = QA_MOD_CLIENT_INPUT_OUTPUT_SET; result[count].input = state->name;
    if (view_angles && vector) result[count].vector_value = qa_compat_q2_read_classic_vector(memory, state->address);
    else if (!view_angles && !vector) result[count].number_value = scalar_read(s, state->address, state->kind);
    else qa_range_error("Native output representation differs from its input");
    count++;
  }
  for (size_t i = output_count; i > 0; i--) if (handlers[i - 1].observer != 0) host->runner->remove_entry(host->runner->ctx, handlers[i - 1].observer);
  for (size_t i = 0; i < output_count; i++) free(handlers[i].layouts);
  free(handlers); free(fields);
  *out_outputs = result; *out_count = count;
}
/* --- attach(). --- */
static void client_project(void *ctx, qa_actor_id actor) {
  provider_t *s = ctx; if (s->protection != NULL) qa_compat_q2_native_mod_protection_reserve_actor(s->protection, actor);
  (void)qa_compat_q2_native_mod_provider_address(s, actor);
}
static void client_admitted(void *ctx, qa_actor_id actor) {
  provider_t *s = ctx; qa_source_mod_client_outputs_publish(s->client_outputs, actor);
  if (s->items != NULL) qa_compat_q2_native_mod_items_admit(s->items, actor);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_bind_actor(s->protection, actor);
  qa_compat_q2_native_mod_pickups_bind_actor(s->pickups, actor);
}
static void client_release(void *ctx, qa_actor_id actor) {
  provider_t *s = ctx; qa_source_mod_client_outputs_release(s->client_outputs, actor);
  if (s->items != NULL) qa_compat_q2_native_mod_items_release(s->items, actor);
  qa_compat_q2_native_mod_pickups_release(s->pickups, actor);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_release(s->protection, actor);
  if (s->closing) {
    const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation; p->release(p->ctx, actor);
    delete_projection(s, actor); actor_list_delete(s->appearance_actors, &s->appearance_count, actor); actor_list_delete(s->pending_releases, &s->pending_release_count, actor);
  } else { pending_release_add(s, actor); release_pending(s); }
}
static bool client_invoke(void *ctx, const call_t *call, qa_actor_id actor, double *out) { provider_t *s = ctx; input_buffer buffer; return execute(s, call, build_inputs(s, &actor, &buffer), true, NULL, NULL, out); }
static void client_with_command(void *ctx, qa_command_invocation *command, void (*invoke)(void *), void *invoke_ctx) { const qa_compat_q2_native_mod_host *host = host_of(ctx); host->with_command(host->ctx, command, invoke, invoke_ctx); }
static qa_guest_address actors_resolve(void *ctx, qa_native_mod_address address) { return resolve(ctx, address, false); }
static void actors_scalar(void *ctx, qa_guest_address address, double value, qa_native_mod_scalar encoding) { scalar_write(ctx, address, value, encoding); }
typedef struct entry_call { provider_t *s; qa_guest_address entry; qa_compat_q2_guest_call_signature signature; const qa_guest_call_value *values; size_t count; const qa_actor_id *actor; qa_guest_call_result result; bool retired; } entry_call;
static void entry_invoke(void *value) { entry_call *c = value; const qa_compat_q2_native_mod_host *host = host_of(c->s); c->result = host->invoke(host->ctx, c->entry, c->signature, c->values, c->count); }
static void entry_transfer(void *value) { entry_call *c = value; c->retired = source_execution(c->s, c->actor, entry_invoke, c) == QA_Q2INV_RETIRED; }
static bool actors_invoke(void *ctx, qa_guest_address entry, const qa_guest_call_value *values, size_t count, const char *returns, const qa_actor_id *actor, qa_guest_call_result *out) {
  provider_t *s = ctx; qa_guest_value_layout *layouts = calloc(count + 1, sizeof *layouts); if (layouts == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) { layouts[i].is_aggregate = false; layouts[i].storage = "pointer"; }
  entry_call c; memset(&c, 0, sizeof c); c.s = s; c.entry = entry; c.values = values; c.count = count; c.actor = actor;
  c.signature.abi = s->declaration->target.abi; c.signature.parameters = layouts; c.signature.parameter_count = count; c.signature.variadic = false; c.signature.result_is_void = !strcmp(returns, "void");
  if (!c.signature.result_is_void) { c.signature.result.is_aggregate = false; c.signature.result.storage = returns; }
  transfer(s, entry_transfer, &c);
  free(layouts);
  if (c.retired) return false;
  *out = c.result; return true;
}
static qa_guest_address actors_address(void *ctx, qa_actor_id actor) { return qa_compat_q2_native_mod_provider_address(ctx, actor); }
static bool actors_actor_at(void *ctx, double slot, qa_actor_id *out) { return qa_compat_q2_native_mod_provider_actor_at(ctx, slot, out); }
static bool actors_client_frame(void *ctx, double slot) { provider_t *s = ctx; return s->has_clients && qa_compat_q2_native_mod_clients_frame(&s->clients, slot); }
static void actors_synchronize_frame(void *ctx, double seconds, double frame) { const qa_compat_q2_native_mod_host *host = host_of(ctx); host->synchronize_frame(host->ctx, seconds, frame); }
static void actors_begin_frame(void *ctx) {
  provider_t *s = ctx; const qa_compat_q2_native_mod_host *host = host_of(s); size_t count = 0; qa_compat_q2_native_mod_actor_slot *rows = entries_of(s, &count);
  for (size_t i = 0; i < count; i++) host->clear_entity_event(host->ctx, rows[i].slot);
  free(rows); host->presentation.begin_frame(host->presentation.ctx);
}
static void actors_end_frame(void *ctx) { provider_t *s = ctx; if (s->has_clients) qa_compat_q2_native_mod_clients_end_frame(&s->clients); if (!s->closed) publish(s, true); }
static bool combat_eligible(void *ctx, qa_actor_id actor) { return !clients_rejects(ctx, actor); }
static void combat_transfer(void *ctx, void (*invoke)(void *), void *invoke_ctx) { transfer(ctx, invoke, invoke_ctx); }
static qa_compat_q2_native_mod_invocation_kind combat_source_execution(void *ctx, const qa_actor_id *actor, void (*invoke)(void *), void *invoke_ctx) { return source_execution(ctx, actor, invoke, invoke_ctx); }
static void combat_synchronize(void *ctx) {
  provider_t *s = ctx; flush_frames(s, NULL); refresh(s);
  if (s->frame_count != 0) frame_observe(s, s->frames[s->frame_count - 1]);
}
/* Items operations. */
static qa_guest_address items_pointer(void *ctx, qa_actor_id actor, const char *record) { qa_guest_address address; if (!pointer_for(ctx, &actor, record, &address)) qa_range_error("Native source item lost its record"); return address; }
static qa_guest_address items_resolve(void *ctx, qa_native_mod_address address) { return resolve(ctx, address, false); }
static bool items_live(void *ctx, qa_actor_id actor) { provider_t *s = ctx; return !s->closed && !s->closing && !s->restoring && clients_admitted(s, actor) && is_live(s, actor); }
static double items_read(void *ctx, qa_guest_address address, qa_native_mod_scalar encoding) { return scalar_read(ctx, address, encoding); }
static void items_write(void *ctx, qa_guest_address address, qa_native_mod_scalar encoding, double value) { scalar_write(ctx, address, value, encoding); }
static void items_invoke(void *ctx, qa_actor_id actor, const call_t *call) { provider_t *s = ctx; input_buffer buffer; double result; (void)execute(s, call, build_inputs(s, &actor, &buffer), true, NULL, NULL, &result); }
static const qa_source_weapon_presentation_model *items_model(void *ctx, qa_actor_id actor) {
  provider_t *s = ctx; double slot; const qa_compat_q2_native_mod_host *host = host_of(s);
  if (!s->has_clients || !qa_compat_q2_native_mod_clients_slot(&s->clients, actor, &slot)) qa_range_error("Native viewmodel lost its client");
  return host->weapon_model(host->ctx, slot + 1);
}
static const qa_original_pickup_execution *items_pickup(void *ctx) { provider_t *s = ctx; return s->pickup_scope_count == 0 ? NULL : s->pickup_scopes[s->pickup_scope_count - 1]->execution; }
void qa_compat_q2_native_mod_provider_attach(provider_t *s, const qa_compat_q2_native_mod_host *host) {
  if (s->host_ != NULL) qa_range_error("Native mod already attached");
  s->host_ = host; const declaration_t *d = s->declaration;
  qa_compat_q2_native_mod_stage_runner_init(&s->stages, host); s->has_stages = true;
  for (size_t i = 0; i < d->protection_count; i++) if (!strcmp(d->protection[i].absorb_abi, "source-region")) { qa_compat_q2_native_mod_invocations_init(&s->invocations, host); s->has_invocations = true; break; }
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_attach(s->protection, host);
  if (d->items != NULL) {
    qa_compat_q2_native_mod_item_host item_host; memset(&item_host, 0, sizeof item_host);
    item_host.memory = host->memory; item_host.runner = host->runner; item_host.image_base = host->image_base; item_host.ctx = host->ctx; item_host.invoke = host->invoke; item_host.entry = host->entry;
    qa_compat_q2_native_mod_item_operations operations; memset(&operations, 0, sizeof operations);
    operations.ctx = s; operations.pointer = items_pointer; operations.resolve = items_resolve; operations.live = items_live; operations.read = items_read; operations.write = items_write;
    operations.invoke = items_invoke; operations.model = items_model; operations.pickup = items_pickup;
    s->items = qa_compat_q2_native_mod_items_create(d->items, d->target.abi, item_host, s->services, s->instance, host->content, operations);
  }
  if (d->clients != NULL) {
    if (s->services->clients == NULL) qa_range_error("Native component clients require destination client services");
    qa_compat_q2_native_mod_client_operations operations; memset(&operations, 0, sizeof operations);
    operations.services = s->services->clients; operations.declaration = d->clients; operations.content = host->content; operations.ctx = s;
    operations.project = client_project; operations.admitted = client_admitted; operations.release = client_release; operations.invoke = client_invoke; operations.open_input = open_input;
    operations.invoke_input = invoke_input; operations.input_output = input_output_fn; operations.with_command = client_with_command;
    qa_compat_q2_native_mod_clients_init(&s->clients, operations); s->has_clients = true;
  }
  if (d->source_actors != NULL) {
    qa_compat_q2_native_mod_source_calls calls; memset(&calls, 0, sizeof calls);
    calls.ctx = s; calls.resolve = actors_resolve; calls.scalar = actors_scalar; calls.invoke = actors_invoke; calls.address = actors_address; calls.actor_at = actors_actor_at;
    calls.client_frame = actors_client_frame; calls.synchronize_frame = actors_synchronize_frame; calls.begin_frame = actors_begin_frame; calls.end_frame = actors_end_frame;
    calls.combat.ctx = s; calls.combat.eligible = combat_eligible; calls.combat.transfer = combat_transfer; calls.combat.source_execution = combat_source_execution; calls.combat.scalar = shared_scalar; calls.combat.synchronize = combat_synchronize;
    s->owned = qa_compat_q2_native_mod_actors_create(d->source_actors, d, host, s->services, s->instance, calls);
  }
}
static bool presentation_frame(void *ctx, qa_actor_id actor, qa_mod_client_presentation_frame *out) {
  provider_t *s = ctx; current(s); const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
  return clients_admitted(s, actor) && p->client_frame(p->ctx, actor, out);
}
static void presentation_assert(void *ctx) { current(ctx); }
bool qa_compat_q2_native_mod_provider_client_presentation(provider_t *s, qa_mod_client_presentation_source *out) {
  const qa_compat_q2_native_mod_presentation *p = &host_of(s)->presentation;
  memset(out, 0, sizeof *out); out->ctx = s; out->generation = p->generation(p->ctx); out->frame = presentation_frame; out->assert_current = presentation_assert; return true;
}
typedef struct command_call { provider_t *s; qa_command_invocation *command; bool result; } command_call;
static void command_invoke(void *value) { command_call *c = value; const qa_compat_q2_native_mod_host *host = host_of(c->s); c->result = host->invoke_command(host->ctx, c->command); }
static void command_transfer(void *value) {
  command_call *c = value; provider_t *s = c->s; size_t count = 0; appearance_row *rows = capture_appearances(s, &count);
  if (source_execution(s, NULL, command_invoke, c) == QA_Q2INV_RETIRED) { c->result = true; free_appearances(rows, count); return; }
  compare_appearances(s, rows, count); free_appearances(rows, count);
}
bool qa_compat_q2_native_mod_provider_invoke_command(provider_t *s, qa_command_invocation *command) {
  char *fold = qa_ascii_fold(command->argc > 0 ? command->argv[0] : ""); if (fold == NULL) qa_range_error("Out of memory");
  bool is_sv = !strcmp(fold, "sv"); free(fold);
  if (!is_sv) return s->has_clients && qa_compat_q2_native_mod_clients_invoke_command(&s->clients, command);
  qa_command_invocation_assert_active(command); release_pending(s);
  command_call c = {s, command, false}; transfer(s, command_transfer, &c);
  return c.result;
}
void qa_compat_q2_native_mod_provider_advance(provider_t *s, qa_frame_context frame) {
  current(s); release_pending(s);
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_advance(s->owned, frame);
  if (s->closed) return;
  publish(s, true);
  if (s->owned == NULL) actors_begin_frame(s);
}
/* --- importBoundary. --- */
typedef struct import_call { provider_t *s; const qa_guest_call_value *values; size_t count; void *ctx; qa_compat_q2_native_import_fn invoke; qa_guest_call_result result; } import_call;
static void import_body(void *value) { import_call *c = value; c->result = c->invoke(c->ctx, c->values, c->count); }
static void import_cleanup(void *value) { import_call *c = value; refresh(c->s); if (c->s->frame_count != 0) frame_observe(c->s, c->s->frames[c->s->frame_count - 1]); }
static qa_guest_call_result import_current(provider_t *s, const char *name, const qa_guest_call_value *values, size_t count, void *invoke_ctx, qa_compat_q2_native_import_fn invoke) {
  if (!s->ready) return invoke(invoke_ctx, values, count);
  bool entity_link = !strcmp(name, "linkentity") || !strcmp(name, "unlinkentity");
  if (s->lifecycle) {
    if (!entity_link) return invoke(invoke_ctx, values, count);
    if (s->restoring && count > 0 && values[0].kind == QA_GUEST_VALUE_POINTER && values[0].pointer_value.has_value) {
      const qa_compat_q2_native_mod_host *host = host_of(s);
      double slot = qa_compat_q2_native_mod_table_slot(host, values[0].pointer_value.value, "Native restore links outside its source table");
      restore_link *link = NULL;
      for (size_t i = 0; i < s->restore_link_count && link == NULL; i++) if (s->restore_links[i].slot == slot) link = &s->restore_links[i];
      if (link == NULL) link = qa_compat_q2_grow((void **)&s->restore_links, &s->restore_link_count, &s->restore_link_capacity, sizeof *link); else free(link->values);
      link->slot = slot; link->address = values[0].pointer_value.value; link->ctx = invoke_ctx; link->invoke = invoke; link->count = count;
      link->values = calloc(count, sizeof *link->values); if (link->values == NULL) qa_range_error("Out of memory");
      memcpy(link->values, values, count * sizeof *values);
    }
    return qa_compat_q2_call_void();
  }
  flush_frames(s, NULL);
  if (entity_link || !strcmp(name, "setmodel")) {
    if (count == 0 || values[0].kind != QA_GUEST_VALUE_POINTER || !values[0].pointer_value.has_value) qa_range_error("Native entity import requires an actor");
    qa_guest_address value = values[0].pointer_value.value; const qa_compat_q2_native_mod_host *host = host_of(s);
    int64_t world = host->entities(host->ctx).base.byte_offset; const qa_actor_id *actor = NULL; qa_actor_id actor_storage; bool owned = false;
    for (size_t i = 0; i < s->projection_count && actor == NULL; i++) if (qa_compat_q2_native_mod_provider_address(s, s->projections[i].actor).byte_offset == value.byte_offset) { actor_storage = s->projections[i].actor; actor = &actor_storage; }
    if (s->owned != NULL) {
      size_t n = 0; qa_compat_q2_native_mod_actor_slot *rows = qa_compat_q2_native_mod_actors_entries(s->owned, &n);
      for (size_t i = 0; i < n && !owned; i++) owned = host->entity(host->ctx, rows[i].slot).address.byte_offset == value.byte_offset;
      free(rows);
    }
    if (value.byte_offset != world && actor == NULL && !owned) qa_range_error("Native source allocated an actor without a declared shared lifecycle");
    if (actor != NULL && entity_link) {
      qa_owned_actor owner;
      if (!qa_session_actor_registry_resolve_owned(s->services->actors, *actor, &owner)) qa_range_error("Native mod linked an expired actor");
      if (!strcmp(name, "linkentity")) qa_shared_body_table_link(s->services->bodies, owner, NULL); else qa_shared_body_table_unlink(s->services->bodies, owner);
      return qa_compat_q2_call_void();
    }
  }
  import_call c = {s, values, count, invoke_ctx, invoke, {0}};
  qa_compat_q2_native_mod_finally(import_body, &c, import_cleanup, &c);
  return c.result;
}
qa_guest_call_result qa_compat_q2_native_mod_provider_import_boundary(provider_t *s, const char *name, const qa_guest_call_value *values, size_t count, void *invoke_ctx, qa_compat_q2_native_import_fn invoke) {
  const qa_compat_q2_native_mod_region_authority *authority = s->region_authority_count == 0 ? NULL : s->region_authorities[s->region_authority_count - 1];
  if (authority != NULL && !authority->current(authority->ctx)) qa_compat_q2_native_throw(authority, authority->error);
  qa_guest_call_result result = import_current(s, name, values, count, invoke_ctx, invoke);
  if (authority != NULL && !authority->current(authority->ctx)) qa_compat_q2_native_throw(authority, authority->error);
  return result;
}
/* --- Lifecycle. --- */
typedef struct record_range { int64_t address; double bytes; } record_range;
static void validate_records(provider_t *s) {
  const declaration_t *d = s->declaration; qa_compat_q2_memory *memory = memory_of(s);
  record_range *ranges = NULL; size_t count = 0, capacity = 0;
  for (size_t r = 0; r < d->actor_record_count; r++) {
    const record_t *record = &d->actor_records[r];
    if (!strcmp(record->base.kind, "clients")) for (double slot = 0; slot < record->capacity; slot++) {
      qa_guest_address address = record_address(s, record, slot); memory->check(memory->ctx, address, record->stride, "write");
      record_range *range = qa_compat_q2_grow((void **)&ranges, &count, &capacity, sizeof *range); range->address = address.byte_offset; range->bytes = record->stride;
    } else {
      qa_guest_address address = record_base(s, record); memory->check(memory->ctx, address, record->stride * record->capacity, "write");
      record_range *range = qa_compat_q2_grow((void **)&ranges, &count, &capacity, sizeof *range); range->address = address.byte_offset; range->bytes = record->stride * record->capacity;
    }
  }
  for (size_t i = 0; i < count; i++) for (size_t j = 0; j < i; j++)
    if ((double)ranges[i].address < (double)ranges[j].address + ranges[j].bytes && (double)ranges[j].address < (double)ranges[i].address + ranges[i].bytes) qa_range_error("Overlapping native actor arrays");
  free(ranges);
}
static void check_call_entry(provider_t *s, const call_t *call) { qa_compat_q2_memory *memory = memory_of(s); memory->check(memory->ctx, call_target(s, call), 1, "execute"); }
void qa_compat_q2_native_mod_provider_initialize(provider_t *s, bool restoring) {
  current(s); if (s->protection != NULL) qa_compat_q2_native_mod_protection_reserve(s->protection);
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_suspend(s->owned, restoring);
  const qa_compat_q2_native_mod_host *host = host_of(s); host->initialize(host->ctx, restoring); current(s);
  const declaration_t *d = s->declaration;
  if (!restoring && s->owned != NULL) qa_compat_q2_native_mod_actors_validate(s->owned);
  if (!restoring) {
    validate_records(s);
    for (size_t i = 0; i < d->initialize_count; i++) { input_buffer buffer; double result; (void)execute(s, &d->initialize[i], build_inputs(s, NULL, &buffer), false, NULL, NULL, &result); }
  }
  for (size_t i = 0; i < d->initialize_count; i++) check_call_entry(s, &d->initialize[i]);
  for (size_t i = 0; i < d->project_count; i++) check_call_entry(s, &d->project[i]);
  for (size_t i = 0; i < d->release_count; i++) check_call_entry(s, &d->release[i]);
  for (size_t i = 0; i < d->callback_count; i++) check_call_entry(s, &d->callbacks[i].call);
  for (size_t i = 0; i < d->objective_count; i++) if (d->objectives[i].is_owned && d->objectives[i].change != NULL) check_call_entry(s, d->objectives[i].change);
  if (d->items != NULL) {
    if (d->items->weapons != NULL) for (size_t i = 0; i < d->items->weapons->selection.value_count; i++) check_call_entry(s, &d->items->weapons->selection.values[i].request);
    for (size_t i = 0; i < d->items->definition_count; i++) {
      if (d->items->definitions[i].action_use != NULL) check_call_entry(s, d->items->definitions[i].action_use);
      if (d->items->definitions[i].action_drop != NULL) check_call_entry(s, d->items->definitions[i].action_drop);
    }
  }
  const qa_native_mod_clients *clients = d->clients;
  if (clients != NULL) {
    for (size_t i = 0; i < clients->admit_count; i++) check_call_entry(s, &clients->admit[i].call);
    for (size_t i = 0; i < clients->userinfo_count; i++) check_call_entry(s, &clients->userinfo[i]);
    for (size_t i = 0; i < clients->disconnect_count; i++) check_call_entry(s, &clients->disconnect[i]);
    for (size_t i = 0; i < clients->command_count; i++) check_call_entry(s, &clients->command[i]);
    for (size_t i = 0; i < clients->frame_count; i++) check_call_entry(s, &clients->frame[i]);
    for (size_t i = 0; i < clients->end_frame_count; i++) check_call_entry(s, &clients->end_frame[i]);
    for (size_t i = 0; i < clients->input_count; i++) for (size_t c = 0; c < clients->input[i].call_count; c++) check_call_entry(s, (const call_t *)clients->input[i].calls + c);
  }
  s->ready = true;
  if (!restoring) { if (s->has_clients) qa_compat_q2_native_mod_clients_start(&s->clients); publish(s, true); }
}
bool qa_compat_q2_native_mod_provider_invoke(provider_t *s, const call_t *call, inputs_t inputs, double *out) {
  current(s);
  for (size_t i = 0; i < inputs.count; i++) if (!strcmp(inputs.rows[i].value.kind, "actor") && inputs.rows[i].value.actor_value != NULL && clients_rejects(s, *inputs.rows[i].value.actor_value)) return false;
  return execute(s, call, inputs, true, NULL, NULL, out);
}
/* --- Checkpoint. --- */
static qa_save_value *set_v(qa_save_value *object, const char *key, qa_save_value *value) { if (value == NULL || !qa_save_value_set(object, key, value)) qa_range_error("Out of memory"); return object; }
static qa_save_value *new_object(void) { qa_save_value *o = qa_save_value_object(); if (o == NULL) qa_range_error("Out of memory"); return o; }
static qa_save_value *new_array(void) { qa_save_value *o = qa_save_value_array(); if (o == NULL) qa_range_error("Out of memory"); return o; }
static void push_v(qa_save_value *array, qa_save_value *value) { if (value == NULL || !qa_save_value_push(array, value)) qa_range_error("Out of memory"); }
static qa_save_value *saved_id_value(qa_saved_actor_id id) { qa_save_value *o = new_object(); set_v(o, "slot", qa_save_value_number(id.slot)); set_v(o, "generation", qa_save_value_number(id.generation)); return o; }
static qa_save_value *source_value(const qa_compat_q2_native_mod_source_save *source) {
  qa_save_value *o = new_object();
  if (!source->rerelease) {
    qa_save_value *original = new_object();
    set_v(original, "provider", qa_save_value_string(source->original.provider)); set_v(original, "schema", qa_save_value_string(source->original.schema));
    set_v(original, "version", qa_save_value_number(source->original.version)); set_v(original, "bytes", qa_save_value_bytes(source->original.bytes.bytes, source->original.bytes.length));
    set_v(o, "edition", qa_save_value_string("classic")); set_v(o, "original", original); return o;
  }
  set_v(o, "edition", qa_save_value_string("rerelease"));
  set_v(o, "game", qa_save_value_bytes(source->game.bytes, source->game.length)); set_v(o, "level", qa_save_value_bytes(source->level.bytes, source->level.length)); set_v(o, "cvars", qa_save_value_bytes(source->cvars.bytes, source->cvars.length));
  qa_save_value *list = new_array();
  for (size_t i = 0; i < source->configstring_count; i++) { qa_save_value *row = new_object(); set_v(row, "index", qa_save_value_number(source->configstrings[i].index)); set_v(row, "value", qa_save_value_string(source->configstrings[i].value)); push_v(list, row); }
  set_v(o, "configstrings", list); return o;
}
static qa_save_value *items_value(const qa_compat_q2_native_item_checkpoint *items) {
  qa_save_value *o = new_object(), *list = new_array();
  set_v(o, "version", qa_save_value_number(1)); set_v(o, "nextRequest", qa_save_value_number(items->next_request));
  for (size_t i = 0; i < items->request_count; i++) {
    const qa_compat_q2_native_item_checkpoint_request *r = &items->requests[i]; qa_save_value *row = new_object();
    set_v(row, "actor", saved_id_value(r->actor)); set_v(row, "id", qa_save_value_number(r->id));
    set_v(row, "item", r->item != NULL ? qa_save_value_string(r->item) : qa_save_value_null()); set_v(row, "status", qa_save_value_string(r->status)); push_v(list, row);
  }
  set_v(o, "requests", list); return o;
}
qa_provider_checkpoint qa_compat_q2_native_mod_provider_checkpoint(provider_t *s) {
  qa_compat_q2_native_mod_pickups_assert_idle(s->pickups);
  current(s);
  if (s->frame_count != 0 || s->input_scope_count != 0) qa_range_error("Cannot save an active native mod callback");
  release_pending(s); flush_frames(s, NULL); refresh(s);
  size_t client_count = 0; qa_compat_q2_native_mod_client_slot *clients = s->has_clients ? qa_compat_q2_native_mod_clients_checkpoint(&s->clients, &client_count) : NULL;
  const qa_compat_q2_native_mod_host *host = host_of(s); qa_compat_q2_native_mod_source_save source = host->checkpoint(host->ctx); current(s);
  if (s->pending_release_count != 0) qa_range_error("Actors changed during native mod capture");
  qa_save_value *root = new_object(), *module = new_object();
  const qa_module_identity *identity = &host->memory->module;
  set_v(module, "id", qa_save_value_string(identity->id)); set_v(module, "artifactPath", qa_save_value_string(identity->artifact_path)); set_v(module, "digest", qa_save_value_string(identity->digest)); set_v(module, "revision", qa_save_value_string(identity->revision));
  set_v(root, "module", module); set_v(root, "map", qa_save_value_string(s->map)); set_v(root, "source", source_value(&source));
  if (s->items != NULL) { qa_compat_q2_native_item_checkpoint items = qa_compat_q2_native_mod_items_checkpoint(s->items); set_v(root, "items", items_value(&items)); qa_compat_q2_native_item_checkpoint_free(&items); }
  else set_v(root, "items", qa_save_value_null());
  if (s->owned != NULL) {
    qa_compat_q2_saved_native_actors saved = qa_compat_q2_native_mod_actors_checkpoint(s->owned); qa_save_value *owned = new_object(), *deferred = new_array(), *actors = new_array();
    set_v(owned, "frame", qa_save_value_number(saved.frame)); set_v(owned, "nextFrame", qa_save_value_number(saved.next_frame));
    for (size_t i = 0; i < saved.deferred_count; i++) push_v(deferred, qa_compat_q2_native_deferred_damage_value(&saved.deferred[i]));
    set_v(owned, "deferred", deferred);
    for (size_t i = 0; i < saved.actor_count; i++) { qa_save_value *row = new_object(); set_v(row, "actor", saved_id_value(saved.actors[i].actor)); set_v(row, "slot", qa_save_value_number(saved.actors[i].slot)); set_v(row, "linked", qa_save_value_boolean(saved.actors[i].linked)); push_v(actors, row); }
    set_v(owned, "actors", actors); set_v(root, "owned", owned); qa_compat_q2_saved_native_actors_free(&saved);
  } else set_v(root, "owned", qa_save_value_null());
  set_v(root, "presentation", host->presentation.checkpoint(host->presentation.ctx));
  qa_save_value *client_list = new_array();
  for (size_t i = 0; i < client_count; i++) { qa_save_value *row = new_object(); set_v(row, "actor", saved_id_value(qa_saved_actor_id_of(clients[i].actor))); set_v(row, "slot", qa_save_value_number(clients[i].slot)); set_v(row, "admitted", qa_save_value_boolean(clients[i].admitted)); push_v(client_list, row); }
  set_v(root, "clients", client_list); free(clients);
  qa_save_value *actor_list = new_array();
  for (size_t i = 0; i < s->projection_count; i++) { qa_save_value *row = new_object(); set_v(row, "actor", saved_id_value(qa_saved_actor_id_of(s->projections[i].actor))); set_v(row, "slot", qa_save_value_number(s->projections[i].slot)); set_v(row, "appearance", qa_save_value_boolean(actor_list_has(s->appearance_actors, s->appearance_count, s->projections[i].actor))); push_v(actor_list, row); }
  set_v(root, "actors", actor_list);
  size_t length = 0; uint8_t *bytes = qa_encode_checkpoint_value(root, &length); if (bytes == NULL) qa_range_error("Out of memory");
  qa_save_value_free(root);
  qa_provider_checkpoint checkpoint; memset(&checkpoint, 0, sizeof checkpoint);
  checkpoint.provider = s->instance; checkpoint.schema = "native:mod"; checkpoint.version = 1; checkpoint.bytes.bytes = bytes; checkpoint.bytes.length = length;
  return checkpoint;
}
static qa_actor_id reference_saved(const provider_t *s, qa_saved_actor_id actor) {
  return s->services->reference_saved != NULL ? s->services->reference_saved(s->services->ctx, actor) : qa_session_actor_registry_reference_saved(s->services->actors, actor, "current");
}
void qa_compat_q2_native_mod_provider_restore(provider_t *s, const qa_provider_checkpoint *record) {
  qa_compat_q2_native_mod_pickups_assert_idle(s->pickups);
  current(s);
  if (s->frame_count != 0 || s->input_scope_count != 0) qa_range_error("Cannot restore an active native callback");
  const qa_compat_q2_native_mod_host *host = host_of(s); const qa_compat_q2_native_mod_presentation *presentation = &host->presentation;
  saved_mod saved = read_checkpoint(record, host->memory->module, s->declaration);
  if (!eq(saved.map, s->map)) qa_range_error("Native mod source save belongs to another map");
  reader_t presentation_reader; if (!qa_save_reader_init(&presentation_reader, saved.presentation, "presentation")) qa_range_error("Out of memory");
  qa_app_sim_native_mod_presentation_checkpoint saved_presentation; qa_app_sim_read_native_mod_presentation(&presentation_reader, &saved_presentation);
  qa_save_reader_destroy(&presentation_reader);
  size_t fog_count = saved_presentation.fog_count; qa_saved_actor_id *fog = calloc(fog_count + 1, sizeof *fog); if (fog == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < fog_count; i++) fog[i] = saved_presentation.fog[i].actor;
  qa_app_sim_native_mod_presentation_checkpoint_free(&saved_presentation);
  qa_actor_id *actors = calloc(saved.actor_count + 1, sizeof *actors); if (actors == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved.actor_count; i++) { actors[i] = reference_saved(s, saved.actors[i].actor); if (!is_live(s, actors[i])) qa_range_error("Saved native projection actor is unavailable"); }
  qa_compat_q2_native_mod_client_slot *clients = calloc(saved.client_count + 1, sizeof *clients); if (clients == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved.client_count; i++) { clients[i].actor = reference_saved(s, saved.clients[i].actor); clients[i].slot = saved.clients[i].slot; clients[i].admitted = saved.clients[i].admitted; }
  if (s->has_clients) qa_compat_q2_native_mod_clients_validate_restore(&s->clients, clients, saved.client_count);
  for (size_t i = 0; i < saved.actor_count; i++) {
    bool client = false;
    for (size_t c = 0; c < saved.client_count && !client; c++) client = qa_same_actor(clients[c].actor, actors[i]);
    validate_inventory_capacity(s, actors[i], client);
  }
  for (size_t i = 0; i < fog_count; i++) if (!is_live(s, reference_saved(s, fog[i]))) qa_range_error("Saved native presentation player is unavailable");
  free(fog);
  size_t owned_count = 0; qa_compat_q2_native_owned_saved *owned_actors = saved.has_owned && s->owned != NULL ? qa_compat_q2_native_mod_actors_validate_saved(s->owned, &saved.owned, &owned_count) : NULL;
  qa_source_mod_client_outputs_clear(s->client_outputs);
  if (s->items != NULL) qa_compat_q2_native_mod_items_clear(s->items);
  qa_compat_q2_native_mod_pickups_close(s->pickups);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_prepare_restore(s->protection);
  s->lifecycle = true; s->restoring = true; s->restore_link_count = 0;
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_suspend(s->owned, true);
  if (s->has_clients) qa_compat_q2_native_mod_clients_restore(&s->clients, clients, saved.client_count);
  s->appearance_count = 0;
  for (size_t i = 0; i < saved.actor_count; i++) if (saved.actors[i].appearance) appearance_add(s, actors[i]);
  s->projection_count = 0;
  for (size_t i = 0; i < saved.actor_count; i++) { projection_row *row = qa_compat_q2_grow((void **)&s->projections, &s->projection_count, &s->projection_capacity, sizeof *row); row->actor = actors[i]; row->slot = saved.actors[i].slot; }
  s->pending_release_count = 0;
  host->restore(host->ctx, &saved.source);
  validate_records(s);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_validate_restored_inventory(s->protection);
  for (size_t i = 0; i < s->projection_count; i++) seed(s, s->projections[i].actor, s->projections[i].slot, false);
  refresh(s);
  presentation->restore(presentation->ctx, saved.presentation);
  if (saved.has_owned && s->owned != NULL) qa_compat_q2_native_mod_actors_restore(s->owned, &saved.owned, owned_actors, owned_count);
  for (size_t i = 0; i < s->restore_link_count; i++) {
    restore_link *link = &s->restore_links[i]; qa_owned_actor owned;
    if (s->owned == NULL || !qa_compat_q2_native_mod_actors_actor_at(s->owned, link->slot, &owned)) continue;
    if (host->entity(host->ctx, link->slot).address.byte_offset != link->address.byte_offset) qa_range_error("Native restore retained a stale source link");
    (void)link->invoke(link->ctx, link->values, link->count);
  }
  publish(s, false);
  s->lifecycle = false; s->restoring = false;
  for (size_t i = 0; i < s->restore_link_count; i++) free(s->restore_links[i].values);
  s->restore_link_count = 0;
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_suspend(s->owned, false);
  qa_mod_source_objectives_restored(s->objectives);
  for (size_t i = 0; i < saved.client_count; i++) if (clients[i].admitted) { qa_source_mod_client_outputs_publish(s->client_outputs, clients[i].actor); if (s->items != NULL) qa_compat_q2_native_mod_items_admit(s->items, clients[i].actor); }
  if (s->items != NULL && saved.has_items) qa_compat_q2_native_mod_items_restore(s->items, &saved.items);
  if (s->has_clients) qa_compat_q2_native_mod_clients_start(&s->clients);
  free(actors); free(clients); free(owned_actors); saved_mod_free(&saved);
}
void qa_compat_q2_native_mod_provider_close(provider_t *s) {
  qa_mod_source_objectives_close(s->objectives);
  if (s->match_bound) { s->release_match.invoke(s->release_match.ctx); s->match_bound = false; }
  if (s->closed || s->closing) return;
  s->closing = true;
  qa_source_mod_client_outputs_close(s->client_outputs);
  if (s->items != NULL) qa_compat_q2_native_mod_items_close(s->items);
  qa_compat_q2_native_mod_pickups_close(s->pickups);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_close(s->protection);
  if (s->has_clients) qa_compat_q2_native_mod_clients_close(&s->clients);
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_close(s->owned);
  s->closed = true;
  if (s->subscribed) { qa_session_actor_registry_off_release(s->services->actors, s->subscription); s->subscribed = false; }
  s->projection_count = 0; s->appearance_count = 0; s->pending_release_count = 0;
  if (s->host_ != NULL) s->host_->presentation.close(s->host_->presentation.ctx);
  s->pending_host_close = true;
  finish_host_close(s);
}
void qa_compat_q2_native_mod_provider_destroy(provider_t *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_provider_close(s);
  if (s->owned != NULL) qa_compat_q2_native_mod_actors_destroy(s->owned);
  if (s->items != NULL) qa_compat_q2_native_mod_items_destroy(s->items);
  if (s->protection != NULL) qa_compat_q2_native_mod_protection_destroy(s->protection);
  qa_compat_q2_native_mod_pickups_destroy(s->pickups);
  if (s->has_clients) qa_compat_q2_native_mod_clients_destroy(&s->clients);
  if (s->has_invocations) qa_compat_q2_native_mod_invocations_destroy(&s->invocations);
  qa_source_mod_client_outputs_destroy(s->client_outputs); qa_mod_source_objectives_destroy(s->objectives);
  while (s->matches != NULL) { match_binding *next = s->matches->next; free(s->matches); s->matches = next; }
  while (s->frame_count > 0) pop_frame(s);
  free(s->projections); free(s->frames); free(s->pickup_scopes); free(s->input_scopes); free(s->appearance_actors); free(s->pending_releases); free(s->region_authorities); free(s->restore_links); free(s->scratch); free(s);
}
