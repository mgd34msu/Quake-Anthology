/* Port of src/compat/q2/native-mod-armor.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_armor.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/world/gameplay/authority.h"
#include "qa/foundation/js.h"

typedef qa_compat_q2_native_mod_armor_state armor;
typedef qa_native_mod_armor_field field_t;
static bool safe_integer(double v) { return isfinite(v) && v == trunc(v) && fabs(v) <= (double)QA_MAX_SAFE_INTEGER; }
static bool is_float(const char *encoding) { return strncmp(encoding, "float", 5) == 0; }
static double bytes_of(qa_native_mod_scalar_field field) { return qa_compat_q2_native_scalar_bytes(field.encoding); }
double qa_compat_q2_native_mod_armor_scalar_bytes(qa_native_mod_scalar_field field) { return bytes_of(field); }
static bool selected(const qa_native_mod_armor_selection *selection, bool has, double value) {
  return has && (selection->is_enum ? value == selection->value : value > 0);
}
void qa_compat_q2_native_mod_armor_validate_value(qa_native_mod_scalar_field field, double value) {
  double bits = bytes_of(field) * 8; bool is_unsigned = strncmp(field.encoding, "uint", 4) == 0;
  if (!isfinite(value) || (!strcmp(field.encoding, "float32") && !isfinite((double)(float)value))
      || (!is_float(field.encoding) && (!safe_integer(value) || value < (is_unsigned ? 0 : -qa_js_pow(2, bits - 1)) || value >= qa_js_pow(2, is_unsigned ? bits : bits - 1))))
    qa_range_error("Native armor value exceeds its declared storage");
}
#define validate_value qa_compat_q2_native_mod_armor_validate_value
static const qa_native_mod_actor_record *find_record(const qa_native_mod_declaration *d, const char *id) {
  for (size_t i = 0; i < d->actor_record_count; i++) if (qa_compat_q2_text_equal(d->actor_records[i].id, id)) return &d->actor_records[i];
  return NULL;
}
static void add_field(armor *s, field_t field) {
  size_t capacity = s->field_count; /* fields is presized in init */
  s->fields[capacity] = field; s->field_count++;
}
void qa_compat_q2_native_mod_armor_state_init(armor *s, qa_native_mod_armor definition, const qa_native_mod_declaration *declaration,
    const qa_compat_q2_native_mod_host *host, qa_compat_q2_native_mod_scalar_fn scalar, void *scalar_ctx) {
  memset(s, 0, sizeof *s);
  s->definition = definition; s->declaration = declaration; s->host = host; s->scalar = scalar; s->scalar_ctx = scalar_ctx;
  bool none = !strcmp(definition.kind, "none"), q2 = !strcmp(definition.kind, "q2");
  size_t regular_count = none ? 0 : q2 ? definition.q2_regular_count : definition.regular_count;
  size_t power_count = none ? 0 : definition.power_count;
  s->fields = calloc(regular_count * 2 + power_count * 3 + 1, sizeof *s->fields);
  s->regular = calloc(regular_count + 1, sizeof *s->regular);
  if (s->fields == NULL || s->regular == NULL) qa_range_error("Out of memory");
  s->regular_count = regular_count;
  for (size_t i = 0; i < regular_count; i++) {
    qa_compat_q2_native_armor_regular *view = &s->regular[i];
    if (q2) { const qa_native_mod_q2_regular_armor_item *item = &definition.q2_regular[i]; view->item = item->item; view->selection = &item->selection; view->points = &item->points; view->normal_protection = item->normal_protection; view->energy_protection = item->energy_protection; }
    else { const qa_native_mod_regular_armor_item *item = &definition.regular[i]; view->item = item->item; view->selection = &item->selection; view->points = &item->points; }
    add_field(s, view->selection->field); add_field(s, *view->points);
  }
  for (size_t i = 0; i < power_count; i++) {
    const qa_native_mod_power_armor_item *item = &definition.power[i];
    add_field(s, item->selection.field); add_field(s, item->cells);
    if (item->enabled != NULL) add_field(s, item->enabled->field);
  }
  for (size_t i = 0; i < s->field_count; i++) {
    field_t field = s->fields[i]; const qa_native_mod_actor_record *record = find_record(declaration, field.record);
    if (record == NULL || (!qa_compat_q2_text_equal(record->id, declaration->entity_record) && strcmp(record->base.kind, "clients") != 0)
        || !safe_integer(field.field.offset) || field.field.offset < 0 || field.field.offset + bytes_of(field.field) > record->stride)
      qa_range_error("Native armor field requires a declared entity or public client record");
  }
  for (size_t i = 0; i < s->field_count; i++) for (size_t j = 0; j < i; j++) {
    field_t a = s->fields[i], b = s->fields[j];
    if (qa_compat_q2_text_equal(a.record, b.record) && a.field.offset < b.field.offset + bytes_of(b.field) && b.field.offset < a.field.offset + bytes_of(a.field)
        && (a.field.offset != b.field.offset || strcmp(a.field.encoding, b.field.encoding) != 0))
      qa_range_error("Overlapping native armor fields have incompatible storage");
  }
  if (none) return;
  for (size_t i = 0; i < regular_count; i++) for (size_t j = 0; j < i; j++) if (qa_compat_q2_text_equal(s->regular[i].item, s->regular[j].item)) qa_range_error("Native armor selection is ambiguous");
  for (size_t i = 0; i < power_count; i++) for (size_t j = 0; j < i; j++) if (!strcmp(definition.power[i].kind, definition.power[j].kind)) qa_range_error("Native armor selection is ambiguous");
  for (size_t i = 0; i < regular_count + power_count; i++) {
    const qa_native_mod_armor_selection *selection = i < regular_count ? s->regular[i].selection : &definition.power[i - regular_count].selection;
    if (!selection->is_enum) continue;
    validate_value(selection->field.field, selection->value); validate_value(selection->field.field, selection->none);
    if (selection->value == selection->none) qa_range_error("Native armor selection equals its inactive value");
    if (!strcmp(selection->field.field.encoding, "float32") && ((double)(float)selection->value != selection->value || (double)(float)selection->none != selection->none))
      qa_range_error("Native armor selection is not exact in its declared storage");
  }
  if (q2) for (size_t i = 0; i < regular_count; i++)
    if (!isfinite(s->regular[i].normal_protection) || !isfinite(s->regular[i].energy_protection)) qa_range_error("Native armor protection must be finite");
  for (size_t i = 0; i < power_count; i++) {
    const qa_native_mod_armor_enabled *enabled = definition.power[i].enabled; if (enabled == NULL) continue;
    if (is_float(enabled->field.field.encoding) || !safe_integer(enabled->mask) || enabled->mask <= 0) qa_range_error("Native power armor requires an integer enabled mask");
    validate_value(enabled->field.field, enabled->mask);
  }
}
void qa_compat_q2_native_mod_armor_state_destroy(armor *s) { free(s->fields); free(s->regular); s->fields = NULL; s->regular = NULL; s->field_count = s->regular_count = 0; }
/* False is TS null. */
static bool location(armor *s, double slot, field_t field, qa_guest_address *base) {
  const qa_compat_q2_native_mod_host *host = s->host;
  if (qa_compat_q2_text_equal(field.record, s->declaration->entity_record)) *base = host->entity(host->ctx, slot).address;
  else if (!host->client(host->ctx, slot, base)) return false;
  host->memory->check(host->memory->ctx, qa_compat_q2_mem_offset(host->memory, *base, (int64_t)field.field.offset), bytes_of(field.field), "read");
  return true;
}
static bool value_of(armor *s, double slot, field_t field, double *out) {
  qa_guest_address base; if (!location(s, slot, field, &base)) return false;
  *out = s->scalar(s->scalar_ctx, base, field.field, false, 0); return true;
}
static double required(armor *s, double slot, field_t field) {
  double value; if (!value_of(s, slot, field, &value)) qa_range_error("Selected native armor has no source storage"); return value;
}
typedef bool (*capture_read)(void *ctx, field_t field, double *out);
static double capture_required(capture_read read, void *ctx, field_t field) {
  double value; if (!read(ctx, field, &value)) qa_range_error("Selected native armor has no source storage"); return value;
}
static double integer_of(double value) { /* TS BigInt(number): integral or a RangeError */
  if (!safe_integer(value) && !(isfinite(value) && value == trunc(value))) qa_range_error("The number %s cannot be converted to a BigInt because it is not an integer", QA_JS_NUMBER(value));
  return value;
}
/* The low 64 bits of BigInt(value) in two's complement, for any integral double
 * (a 64-bit field read can exceed int64, where a C cast is undefined). */
static uint64_t low_bits(double value) {
  uint64_t magnitude = (uint64_t)fmod(fabs(value), 18446744073709551616.0);
  return value < 0 ? (uint64_t)0 - magnitude : magnitude;
}
static qa_armor_state capture(armor *s, capture_read read, void *ctx) {
  qa_armor_state state; memset(&state, 0, sizeof state); state.regular.kind = QA_REGULAR_ARMOR_NONE; state.powered.kind = "none";
  if (!strcmp(s->definition.kind, "none")) return state;
  for (size_t i = 0; i < s->regular_count; i++) {
    const qa_compat_q2_native_armor_regular *item = &s->regular[i]; double value; bool has = read(ctx, item->selection->field, &value);
    if (!selected(item->selection, has, value)) continue;
    state.regular.item = item->item; state.regular.points = capture_required(read, ctx, *item->points);
    if (!strcmp(s->definition.kind, "q2")) { state.regular.kind = QA_REGULAR_ARMOR_Q2; state.regular.normal_protection = item->normal_protection; state.regular.energy_protection = item->energy_protection; }
    else state.regular.kind = QA_REGULAR_ARMOR_SOURCE;
    break;
  }
  for (size_t i = 0; i < s->definition.power_count; i++) {
    const qa_native_mod_power_armor_item *item = &s->definition.power[i]; double value; bool has = read(ctx, item->selection.field, &value);
    if (!selected(&item->selection, has, value)) continue;
    if (item->enabled != NULL && (low_bits(integer_of(capture_required(read, ctx, item->enabled->field))) & (uint64_t)integer_of(item->enabled->mask)) == 0) continue;
    state.powered.kind = item->kind; state.powered.cells = capture_required(read, ctx, item->cells);
    break;
  }
  return state;
}
typedef struct slot_read { armor *s; double slot; } slot_read;
static bool read_slot(void *ctx, field_t field, double *out) { slot_read *r = ctx; return value_of(r->s, r->slot, field, out); }
qa_armor_state qa_compat_q2_native_mod_armor_read(armor *s, double slot) { slot_read r = {s, slot}; return capture(s, read_slot, &r); }
double qa_compat_q2_native_mod_armor_read_count(armor *s, double slot, field_t field) { return required(s, slot, field); }
void qa_compat_q2_native_mod_armor_write_count(armor *s, double slot, field_t field, double value) {
  validate_value(field.field, value);
  qa_guest_address base; if (!location(s, slot, field, &base)) qa_range_error("Native armor count has no source storage");
  s->scalar(s->scalar_ctx, base, field.field, true, value);
}
qa_armor_state qa_compat_q2_native_mod_armor_normalize_legacy(armor *s, double slot, qa_armor_state value) {
  if (!strcmp(s->definition.kind, "none")) return value;
  for (size_t i = 0; i < s->definition.power_count; i++)
    if (!strcmp(s->definition.power[i].kind, value.powered.kind)) return qa_normalize_legacy_power_only_armor(value, qa_compat_q2_native_mod_armor_read(s, slot), s->definition.power[i].item);
  return value;
}

typedef struct store { int64_t address; qa_guest_address base; field_t field; double value; } store;
typedef struct store_plan { armor *s; double slot; store *stores; size_t count, capacity; } store_plan;
static bool pending(void *ctx, field_t field, double *out) {
  store_plan *p = ctx; qa_guest_address base;
  if (!location(p->s, p->slot, field, &base)) return false;
  int64_t address = base.byte_offset + (int64_t)field.field.offset;
  for (size_t i = 0; i < p->count; i++) if (p->stores[i].address == address) { *out = p->stores[i].value; return true; }
  *out = p->s->scalar(p->s->scalar_ctx, base, field.field, false, 0); return true;
}
static void plan_store(store_plan *p, field_t field, double value) {
  validate_value(field.field, value);
  qa_guest_address base;
  if (!location(p->s, p->slot, field, &base)) { if (value != 0) qa_range_error("Native armor write requires its source record"); return; }
  qa_compat_q2_memory *memory = p->s->host->memory; qa_guest_address address = qa_compat_q2_mem_offset(memory, base, (int64_t)field.field.offset);
  memory->check(memory->ctx, address, bytes_of(field.field), "write");
  double stored = !strcmp(field.field.encoding, "float32") ? (double)(float)value : value;
  for (size_t i = 0; i < p->count; i++) if (p->stores[i].address == address.byte_offset) { p->stores[i].base = base; p->stores[i].field = field; p->stores[i].value = stored; return; }
  store *entry = qa_compat_q2_grow((void **)&p->stores, &p->count, &p->capacity, sizeof *entry);
  entry->address = address.byte_offset; entry->base = base; entry->field = field; entry->value = stored;
}
static void plan_select(store_plan *p, const qa_native_mod_armor_selection *selection, bool active) {
  double current = 0;
  if (!selection->is_enum && active) current = required(p->s, p->slot, selection->field);
  plan_store(p, selection->field, selection->is_enum ? (active ? selection->value : selection->none) : (active ? qa_js_max(1, current) : 0));
}
static void plan_enable(store_plan *p, const qa_native_mod_power_armor_item *item, bool active) {
  if (item->enabled == NULL) { plan_select(p, &item->selection, active); return; }
  double current;
  if (!pending(p, item->enabled->field, &current)) { if (active) qa_range_error("Native power armor has no enabled storage"); return; }
  /* Number(BigInt(now) | mask) and Number(BigInt(now) & ~mask) for a safe positive mask:
   * x | m = x + (m - (x & m)) and x & ~m = x - (x & m), each rounded once. */
  double mask = integer_of(item->enabled->mask), now = integer_of(current);
  double common = (double)(low_bits(now) & (uint64_t)mask);
  plan_store(p, item->enabled->field, active ? now + (mask - common) : now - common);
  if (active) plan_select(p, &item->selection, true);
}
/* Returns a malloc'd store list. */
static store *plan_stores(armor *s, double slot, qa_armor_state value, size_t *out_count) {
  const qa_native_mod_armor *definition = &s->definition; const qa_regular_armor_state *requested = &value.regular;
  bool none = !strcmp(definition->kind, "none"), power_none = !strcmp(value.powered.kind, "none");
  *out_count = 0;
  if (none) {
    if (requested->kind != QA_REGULAR_ARMOR_NONE || !power_none) qa_range_error("Native owned actor declares no armor storage");
    return NULL;
  }
  bool definition_q2 = !strcmp(definition->kind, "q2");
  qa_regular_armor_kind family = definition_q2 ? QA_REGULAR_ARMOR_Q2 : QA_REGULAR_ARMOR_SOURCE;
  if (requested->kind != QA_REGULAR_ARMOR_NONE && requested->kind != family) qa_range_error("Native armor declaration cannot represent another armor family");
  const qa_compat_q2_native_armor_regular *regular = NULL; const qa_native_mod_power_armor_item *power = NULL;
  if (requested->kind != QA_REGULAR_ARMOR_NONE) for (size_t i = 0; i < s->regular_count && regular == NULL; i++) if (qa_compat_q2_text_equal(s->regular[i].item, requested->item)) regular = &s->regular[i];
  if (!power_none) for (size_t i = 0; i < definition->power_count && power == NULL; i++) if (!strcmp(definition->power[i].kind, value.powered.kind)) power = &definition->power[i];
  if ((!power_none && power == NULL) || (requested->kind != QA_REGULAR_ARMOR_NONE && regular == NULL)
      || (requested->kind == QA_REGULAR_ARMOR_Q2 && (regular == NULL || requested->normal_protection != regular->normal_protection || requested->energy_protection != regular->energy_protection)))
    qa_range_error("Armor state differs from its declared native representation");
  store_plan p = {s, slot, NULL, 0, 0};
  if (requested->kind == QA_REGULAR_ARMOR_NONE) for (size_t i = 0; i < s->regular_count; i++) {
    double current; bool has = value_of(s, slot, s->regular[i].selection->field, &current);
    if (selected(s->regular[i].selection, has, current)) { plan_select(&p, s->regular[i].selection, false); plan_store(&p, *s->regular[i].points, 0); break; }
  }
  qa_powered_protection_state current_power = qa_compat_q2_native_mod_armor_read(s, slot).powered;
  bool power_changes = strcmp(current_power.kind, value.powered.kind) != 0;
  if (power_changes) for (size_t i = 0; i < definition->power_count; i++) plan_enable(&p, &definition->power[i], false);
  if (requested->kind != QA_REGULAR_ARMOR_NONE && regular != NULL) { plan_select(&p, regular->selection, true); plan_store(&p, *regular->points, requested->points); }
  if (power != NULL && !power_none) {
    if (power_changes) plan_enable(&p, power, true);
    if (!strcmp(current_power.kind, "none") || power_changes || current_power.cells != value.powered.cells) plan_store(&p, power->cells, value.powered.cells);
  }
  qa_armor_state result = capture(s, pending, &p);
  double requested_points = requested->kind != QA_REGULAR_ARMOR_NONE ? (regular != NULL && !strcmp(regular->points->field.encoding, "float32") ? (double)(float)requested->points : requested->points) : 0;
  double requested_cells = power_none ? 0 : (power != NULL && !strcmp(power->cells.field.encoding, "float32") ? (double)(float)value.powered.cells : value.powered.cells);
  bool empty_regular = requested->kind == QA_REGULAR_ARMOR_NONE || (requested_points == 0 && regular != NULL && !regular->selection->is_enum
      && qa_compat_q2_text_equal(regular->selection->field.record, regular->points->record) && regular->selection->field.field.offset == regular->points->field.offset);
  bool result_none = result.regular.kind == QA_REGULAR_ARMOR_NONE, result_power_none = !strcmp(result.powered.kind, "none");
  if (strcmp(value.powered.kind, result.powered.kind) != 0 || requested_points != (!result_none ? result.regular.points : 0)
      || (empty_regular && !result_none)
      || requested_cells != (result_power_none ? 0 : result.powered.cells)
      || (!empty_regular && ((result.regular.kind != QA_REGULAR_ARMOR_Q2 && result.regular.kind != QA_REGULAR_ARMOR_SOURCE) || !qa_compat_q2_text_equal(result.regular.item, requested->item))))
    qa_range_error("Native source selection cannot represent this armor without changing other ownership");
  *out_count = p.count; return p.stores;
}
void qa_compat_q2_native_mod_armor_validate_write(armor *s, double slot, qa_armor_state value) { size_t count; free(plan_stores(s, slot, value, &count)); }
void qa_compat_q2_native_mod_armor_write(armor *s, double slot, qa_armor_state value) {
  size_t count; store *stores = plan_stores(s, slot, value, &count);
  for (size_t i = 0; i < count; i++) s->scalar(s->scalar_ctx, stores[i].base, stores[i].field.field, true, stores[i].value);
  free(stores);
}

typedef struct observe_group { struct qa_compat_q2_native_armor_observation *owner; qa_guest_address base; double start; const field_t **fields; size_t field_count; uint64_t handle; } observe_group;
struct qa_compat_q2_native_armor_observation {
  armor *s; double slot; void *ctx; qa_compat_q2_native_armor_changed_fn changed;
  qa_armor_state previous; observe_group *groups; size_t group_count;
};
static bool armor_equal(qa_armor_state a, qa_armor_state b) { return qa_compat_q2_regular_armor_equal(a.regular, b.regular) && qa_compat_q2_powered_protection_equal(a.powered, b.powered); }
static void group_written(void *ctx, const qa_compat_q2_written_range *ranges, size_t count) {
  observe_group *group = ctx; qa_compat_q2_native_armor_observation *o = group->owner; bool hit = false;
  for (size_t i = 0; i < count && !hit; i++) for (size_t j = 0; j < group->field_count && !hit; j++) {
    const qa_native_mod_scalar_field *field = &group->fields[j]->field;
    hit = ranges[i].byte_offset + group->start < field->offset + bytes_of(*field) && field->offset < ranges[i].byte_offset + group->start + ranges[i].byte_length;
  }
  if (!hit) return;
  qa_armor_state before = o->previous, after = qa_compat_q2_native_mod_armor_read(o->s, o->slot); o->previous = after;
  if (!armor_equal(before, after)) o->changed(o->ctx, before, after);
}
qa_compat_q2_native_armor_observation *qa_compat_q2_native_mod_armor_observe(armor *s, double slot, void *ctx, qa_compat_q2_native_armor_changed_fn changed) {
  qa_compat_q2_native_armor_observation *o = calloc(1, sizeof *o); if (o == NULL) qa_range_error("Out of memory");
  o->s = s; o->slot = slot; o->ctx = ctx; o->changed = changed;
  o->groups = calloc(s->field_count + 1, sizeof *o->groups); if (o->groups == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < s->field_count; i++) {
    qa_guest_address base; if (!location(s, slot, s->fields[i], &base)) continue;
    observe_group *group = NULL;
    for (size_t j = 0; j < o->group_count && group == NULL; j++) if (o->groups[j].base.byte_offset == base.byte_offset) group = &o->groups[j];
    if (group == NULL) {
      group = &o->groups[o->group_count++]; group->owner = o; group->base = base;
      group->fields = calloc(s->field_count, sizeof *group->fields); if (group->fields == NULL) qa_range_error("Out of memory");
    }
    group->fields[group->field_count++] = &s->fields[i];
  }
  o->previous = qa_compat_q2_native_mod_armor_read(s, slot);
  for (size_t i = 0; i < o->group_count; i++) {
    observe_group *group = &o->groups[i]; double start = INFINITY, end = -INFINITY;
    for (size_t j = 0; j < group->field_count; j++) { const qa_native_mod_scalar_field *field = &group->fields[j]->field; start = qa_js_min(start, field->offset); end = qa_js_max(end, field->offset + bytes_of(*field)); }
    group->start = start;
    group->handle = s->host->observe_writes(s->host->ctx, qa_compat_q2_mem_offset(s->host->memory, group->base, (int64_t)start), end - start, group, group_written);
  }
  return o;
}
void qa_compat_q2_native_armor_observation_close(qa_compat_q2_native_armor_observation *o) {
  if (o == NULL) return;
  for (size_t i = 0; i < o->group_count; i++) { o->s->host->unobserve_writes(o->s->host->ctx, o->groups[i].handle); free(o->groups[i].fields); }
  free(o->groups); free(o);
}
