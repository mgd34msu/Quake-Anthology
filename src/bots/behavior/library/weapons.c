/* See header. be_ai_weap.c/h selected 32-bit layout. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/weapons.h"
#include "library_internal.h"
#include "qa/bots/behavior/library/structure.h"
#include "qa/input/support.h"
#include <math.h>
#define DEFAULT_WEAPON_CAPACITY 32
#define DEFAULT_PROJECTILE_CAPACITY 32
#define MAX_CONFIG_PATH_BYTES 63
#define WEAPON_CONFIG_BYTES 16
#define WEAPON_INFO_BYTES 552
#define PROJECTILE_INFO_BYTES 208
#define PROJECTILE_INFO_OFFSET 344
struct qa_weapon_config_bots_a {
  qa_bot_memory *memory;
  qa_bot_memory_allocation allocation;
  char *path;
  int64_t defined_weapon_count;
  size_t projectile_count;
  lib_diagnostics diagnostics;
};
#define FIELD(n, o, t) {n, o, t, 0, 0, 0, NULL}
#define ARRAY_FIELD(n, o) {n, o, QA_STRUCT_FLOAT | QA_STRUCT_ARRAY, 3, 0, 0, NULL}
static const qa_structure_field_definition projectile_fields[] = {
    FIELD("name", 0, QA_STRUCT_STRING),      FIELD("model", 80, QA_STRUCT_STRING),
    FIELD("flags", 160, QA_STRUCT_INT),      FIELD("gravity", 164, QA_STRUCT_FLOAT),
    FIELD("damage", 168, QA_STRUCT_INT),     FIELD("radius", 172, QA_STRUCT_FLOAT),
    FIELD("visdamage", 176, QA_STRUCT_INT),  FIELD("damagetype", 180, QA_STRUCT_INT),
    FIELD("healthinc", 184, QA_STRUCT_INT),  FIELD("push", 188, QA_STRUCT_FLOAT),
    FIELD("detonation", 192, QA_STRUCT_FLOAT), FIELD("bounce", 196, QA_STRUCT_FLOAT),
    FIELD("bouncefric", 200, QA_STRUCT_FLOAT), FIELD("bouncestop", 204, QA_STRUCT_FLOAT),
};
static const qa_structure_definition projectile_dump = {PROJECTILE_INFO_BYTES, projectile_fields,
                                                        sizeof projectile_fields / sizeof projectile_fields[0]};
static const qa_structure_field_definition weapon_fields[] = {
    FIELD("number", 4, QA_STRUCT_INT),        FIELD("name", 8, QA_STRUCT_STRING),
    FIELD("level", 168, QA_STRUCT_INT),       FIELD("model", 88, QA_STRUCT_STRING),
    FIELD("weaponindex", 172, QA_STRUCT_INT), FIELD("flags", 176, QA_STRUCT_INT),
    FIELD("projectile", 180, QA_STRUCT_STRING), FIELD("numprojectiles", 260, QA_STRUCT_INT),
    FIELD("hspread", 264, QA_STRUCT_FLOAT),   FIELD("vspread", 268, QA_STRUCT_FLOAT),
    FIELD("speed", 272, QA_STRUCT_FLOAT),     FIELD("acceleration", 276, QA_STRUCT_FLOAT),
    ARRAY_FIELD("recoil", 280),               ARRAY_FIELD("offset", 292),
    ARRAY_FIELD("angleoffset", 304),          FIELD("extrazvelocity", 316, QA_STRUCT_FLOAT),
    FIELD("ammoamount", 320, QA_STRUCT_INT),  FIELD("ammoindex", 324, QA_STRUCT_INT),
    FIELD("activate", 328, QA_STRUCT_FLOAT),  FIELD("reload", 332, QA_STRUCT_FLOAT),
    FIELD("spinup", 336, QA_STRUCT_FLOAT),    FIELD("spindown", 340, QA_STRUCT_FLOAT),
};
static const qa_structure_definition weapon_dump = {WEAPON_INFO_BYTES, weapon_fields,
                                                    sizeof weapon_fields / sizeof weapon_fields[0]};
/* Bounds-checked DataView cell access over one allocation. */
static uint8_t *cell(const qa_bot_memory_allocation *allocation, int64_t offset, int64_t size) {
  if (offset < 0 || (uint64_t)(offset + size) > allocation->length)
    qa_range_error("Offset is outside the bounds of the DataView");
  return allocation->bytes + offset;
}
static void read_text(const qa_bot_memory_allocation *allocation, int64_t offset, char *out) {
  const uint8_t *bytes = allocation->bytes + offset;
  int64_t available = (int64_t)allocation->length - offset, used = 0;
  while (used < 79 && used < available && bytes[used] != 0) {
    out[used] = (char)bytes[used];
    used++;
  }
  /* TS text() reads 80 bytes; byte 79 is always zero for stored text. */
  out[used] = '\0';
}
static void set_text(const qa_bot_memory_allocation *allocation, int64_t offset, const char *value) {
  uint8_t *bytes = cell(allocation, offset, 80);
  memset(bytes, 0, 80);
  size_t length = strlen(value);
  if (length > 79) length = 79;
  memcpy(bytes, value, length);
}
static void read_projectile(const qa_bot_memory_allocation *a, int64_t offset, qa_projectile_info_bots_a *out) {
  read_text(a, offset, out->name);
  read_text(a, offset + 80, out->model);
  out->flags = lib_i32(cell(a, offset + 160, 4));
  out->gravity = lib_f32(cell(a, offset + 164, 4));
  out->damage = lib_i32(cell(a, offset + 168, 4));
  out->radius = lib_f32(cell(a, offset + 172, 4));
  out->visible_damage = lib_i32(cell(a, offset + 176, 4));
  out->damage_type = lib_i32(cell(a, offset + 180, 4));
  out->health_increase = lib_i32(cell(a, offset + 184, 4));
  out->push = lib_f32(cell(a, offset + 188, 4));
  out->detonation = lib_f32(cell(a, offset + 192, 4));
  out->bounce = lib_f32(cell(a, offset + 196, 4));
  out->bounce_friction = lib_f32(cell(a, offset + 200, 4));
  out->bounce_stop = lib_f32(cell(a, offset + 204, 4));
}
static void read_weapon(const qa_bot_memory_allocation *a, int64_t offset, qa_weapon_info_bots_a *out) {
  out->valid = lib_i32(cell(a, offset, 4)) != 0;
  out->number = lib_i32(cell(a, offset + 4, 4));
  read_text(a, offset + 8, out->name);
  read_text(a, offset + 88, out->model);
  out->level = lib_i32(cell(a, offset + 168, 4));
  out->weapon_inventory_index = lib_i32(cell(a, offset + 172, 4));
  out->flags = lib_i32(cell(a, offset + 176, 4));
  read_text(a, offset + 180, out->projectile);
  out->projectile_count = lib_i32(cell(a, offset + 260, 4));
  out->horizontal_spread = lib_f32(cell(a, offset + 264, 4));
  out->vertical_spread = lib_f32(cell(a, offset + 268, 4));
  out->speed = lib_f32(cell(a, offset + 272, 4));
  out->acceleration = lib_f32(cell(a, offset + 276, 4));
  out->recoil = lib_vec3(cell(a, offset + 280, 12));
  out->offset = lib_vec3(cell(a, offset + 292, 12));
  out->angle_offset = lib_vec3(cell(a, offset + 304, 12));
  out->extra_z_velocity = lib_f32(cell(a, offset + 316, 4));
  out->ammo_amount = lib_i32(cell(a, offset + 320, 4));
  out->ammo_inventory_index = lib_i32(cell(a, offset + 324, 4));
  out->activate = lib_f32(cell(a, offset + 328, 4));
  out->reload = lib_f32(cell(a, offset + 332, 4));
  out->spin_up = lib_f32(cell(a, offset + 336, 4));
  out->spin_down = lib_f32(cell(a, offset + 340, 4));
  read_projectile(a, offset + PROJECTILE_INFO_OFFSET, &out->projectile_info);
}
static int64_t config_capacity(const qa_weapon_config_bots_a *c) { return lib_i32(c->allocation.bytes); }
static int64_t projectiles_start(int64_t capacity) {
  return (int64_t)(uint32_t)((uint32_t)WEAPON_CONFIG_BYTES + (uint32_t)((int64_t)capacity * WEAPON_INFO_BYTES));
}
static int64_t weapon_offset(const qa_weapon_config_bots_a *c, int64_t index) {
  int64_t start = WEAPON_CONFIG_BYTES + index * WEAPON_INFO_BYTES;
  if (start < WEAPON_CONFIG_BYTES || start + WEAPON_INFO_BYTES > (int64_t)c->allocation.length)
    qa_range_error("weapon info copy exceeds the source configuration allocation");
  return start;
}
const char *qa_weapon_config_path(const qa_weapon_config_bots_a *c) { return c->path; }
int64_t qa_weapon_config_capacity(const qa_weapon_config_bots_a *c) { return config_capacity(c); }
int64_t qa_weapon_config_defined_count(const qa_weapon_config_bots_a *c) { return c->defined_weapon_count; }
size_t qa_weapon_config_projectile_count(const qa_weapon_config_bots_a *c) { return c->projectile_count; }
const qa_ui_script_diagnostic *qa_weapon_config_diagnostics(const qa_weapon_config_bots_a *c, size_t *out_count) {
  *out_count = c->diagnostics.count;
  return c->diagnostics.items;
}
static bool weapon_valid_at(const qa_weapon_config_bots_a *c, int64_t index) {
  if (index < 0 || index >= config_capacity(c)) return false;
  int64_t offset = WEAPON_CONFIG_BYTES + index * WEAPON_INFO_BYTES;
  return lib_i32(cell(&c->allocation, offset, 4)) != 0;
}
bool qa_weapon_config_weapon(const qa_weapon_config_bots_a *c, int64_t index, qa_weapon_info_bots_a *out) {
  if (!weapon_valid_at(c, index)) return false;
  read_weapon(&c->allocation, WEAPON_CONFIG_BYTES + index * WEAPON_INFO_BYTES, out);
  return true;
}
void qa_weapon_config_weapon_info(const qa_weapon_config_bots_a *c, int64_t index, qa_weapon_info_bots_a *out) {
  read_weapon(&c->allocation, weapon_offset(c, index), out);
}
void qa_weapon_config_projectile(const qa_weapon_config_bots_a *c, size_t index, qa_projectile_info_bots_a *out) {
  int64_t start = WEAPON_CONFIG_BYTES + config_capacity(c) * WEAPON_INFO_BYTES + (int64_t)index * PROJECTILE_INFO_BYTES;
  read_projectile(&c->allocation, start, out);
}
const uint8_t *qa_weapon_config_weapon_bytes(const qa_weapon_config_bots_a *c, int64_t index) {
  return c->allocation.bytes + weapon_offset(c, index);
}
const uint8_t *qa_weapon_config_projectile_bytes(const qa_weapon_config_bots_a *c, int64_t index) {
  int64_t start = projectiles_start(config_capacity(c)) + index * PROJECTILE_INFO_BYTES;
  if (index < 0 || (size_t)index >= c->projectile_count ||
      start + PROJECTILE_INFO_BYTES > (int64_t)c->allocation.length)
    qa_range_error("projectile dump exceeds the source configuration allocation");
  return c->allocation.bytes + start;
}
/* ---- diagnostics ---- */
static const char *severity_name(qa_weapon_severity severity) {
  static const char *const names[] = {"message", "warning", "error", "fatal"};
  return names[severity];
}
static qa_weapon_diagnostic *push_reported(qa_weapon_ai_bots_a *w, bool origin_print, qa_weapon_severity severity,
                                           const char *message, const qa_ui_script_location *at) {
  if (w->reported_count == w->reported_capacity) {
    w->reported_capacity = lib_grow(w->reported_capacity, w->reported_count + 1);
    w->reported = lib_realloc(w->reported, w->reported_capacity * sizeof *w->reported);
  }
  qa_weapon_diagnostic *d = &w->reported[w->reported_count++];
  char * message2 = lib_strdup(message);
  qa_ui_script_location location = lib_location(at->path, at->line, at->column);
  *d = (qa_weapon_diagnostic){origin_print, severity, message2, location};
  return d;
}
__attribute__((format(printf, 4, 5))) static void emit(qa_weapon_ai_bots_a *w, qa_weapon_severity severity,
                                                       const char *path, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *text;
  if (vasprintf(&text, format, args) < 0) qa_range_error("out of memory");
  va_end(args);
  qa_ui_script_location at = {(char *)(uintptr_t)path, 1, 1};
  qa_weapon_diagnostic copy = *push_reported(w, true, severity, text, &at);
  free(text);
  if (w->options.report != NULL) w->options.report(w->options.report_ctx, &copy);
}
typedef struct setup_scope {
  qa_weapon_ai_bots_a *weapons;
  int64_t generation, revision;
} setup_scope;
static bool scope_current(const setup_scope *scope) {
  return scope->generation == scope->weapons->generation && scope->revision == scope->weapons->setup_revision;
}
/* TS appendScriptDiagnostics for one diagnostic. */
static bool append_script_diagnostic(setup_scope *scope, bool origin_print, const qa_ui_script_diagnostic *d) {
  qa_weapon_ai_bots_a *w = scope->weapons;
  if (!scope_current(scope)) return false;
  qa_weapon_severity severity = d->severity == QA_UI_SCRIPT_SEVERITY_WARNING ? QA_WEAPON_WARNING : QA_WEAPON_ERROR;
  bool duplicate = false;
  for (size_t i = 0; i < w->reported_count && !duplicate; i++) {
    const qa_weapon_diagnostic *c = &w->reported[i];
    duplicate = c->severity == severity && strcmp(c->message, d->message) == 0 &&
                strcmp(c->location.path, d->location.path) == 0 && c->location.line == d->location.line &&
                c->location.column == d->location.column;
  }
  qa_weapon_diagnostic local = {origin_print, severity, d->message, d->location};
  if (!duplicate) local = *push_reported(w, origin_print, severity, d->message, &d->location);
  if (w->options.report != NULL) w->options.report(w->options.report_ctx, &local);
  return scope_current(scope);
}
/* ---- pointers ---- */
static int64_t pointer_next(qa_weapon_ai_bots_a *w) {
  if (w->next_pointer > 0xffffffffLL) qa_range_error("weapon pointer IDs exhausted");
  return w->next_pointer++;
}
static qa_weapon_pointer_record *record_append(qa_weapon_ai_bots_a *w, qa_weapon_pointer_record record) {
  if (w->record_count == w->record_capacity) {
    w->record_capacity = lib_grow(w->record_capacity, w->record_count + 1);
    w->records = lib_realloc(w->records, w->record_capacity * sizeof *w->records);
  }
  w->records[w->record_count] = record;
  return &w->records[w->record_count++];
}
static qa_weapon_pointer_record *record_find(qa_weapon_ai_bots_a *w, int64_t pointer) {
  for (size_t i = 0; i < w->record_count; i++)
    if (w->records[i].pointer == pointer) return &w->records[i];
  return NULL;
}
static void record_delete(qa_weapon_ai_bots_a *w, int64_t pointer) {
  for (size_t i = 0; i < w->record_count; i++) {
    if (w->records[i].pointer != pointer) continue;
    memmove(&w->records[i], &w->records[i + 1], (w->record_count - i - 1) * sizeof *w->records);
    w->record_count--;
    return;
  }
}
static void identity_set(qa_weapon_ai_bots_a *w, qa_weight_config_bots_a *config, int64_t pointer) {
  if (w->identity_count == w->identity_capacity) {
    w->identity_capacity = lib_grow(w->identity_capacity, w->identity_count + 1);
    w->identities = lib_realloc(w->identities, w->identity_capacity * sizeof *w->identities);
  }
  w->identities[w->identity_count++] = (qa_weapon_config_identity){config, pointer};
}
static int64_t identity_find(const qa_weapon_ai_bots_a *w, const qa_weight_config_bots_a *config) {
  for (size_t i = 0; i < w->identity_count; i++)
    if (w->identities[i].config == config) return w->identities[i].pointer;
  return 0;
}
static int64_t pointers_config(qa_weapon_ai_bots_a *w, qa_weight_config_bots_a *config) {
  int64_t existing = identity_find(w, config);
  if (existing != 0) {
    qa_weapon_pointer_record *record = record_find(w, existing);
    if (record != NULL && record->is_config) record->references++;
    else if (record != NULL) *record = (qa_weapon_pointer_record){existing, true, config, 1, {NULL, 0, NULL}};
    else record_append(w, (qa_weapon_pointer_record){existing, true, config, 1, {NULL, 0, NULL}});
    return existing;
  }
  int64_t pointer = pointer_next(w);
  record_append(w, (qa_weapon_pointer_record){pointer, true, config, 1, {NULL, 0, NULL}});
  identity_set(w, config, pointer);
  return pointer;
}
static qa_weapon_pointer_record *pointers_resolve(qa_weapon_ai_bots_a *w, uint32_t pointer) {
  if (pointer == 0) return NULL;
  qa_weapon_pointer_record *record = record_find(w, pointer);
  if (record == NULL) qa_ui_script_plain_error("invalid weapon pointer %u", pointer);
  return record;
}
static void pointers_release_config(qa_weapon_ai_bots_a *w, uint32_t pointer) {
  qa_weapon_pointer_record *record = pointers_resolve(w, pointer);
  if (record == NULL) return;
  if (!record->is_config) qa_ui_script_plain_error("weapon configuration pointer has wrong type");
  if (--record->references == 0) record_delete(w, pointer);
}
/* ---- state ---- */
static qa_weight_config_bots_a *state_config(qa_weapon_ai_bots_a *w, const qa_weapon_state_bots_a *state) {
  qa_weapon_pointer_record *record = pointers_resolve(w, lib_u32(state->allocation.bytes));
  if (record == NULL) return NULL;
  if (!record->is_config) qa_ui_script_plain_error("weapon configuration pointer has wrong type");
  return record->config;
}
static void state_set_config(qa_weapon_ai_bots_a *w, qa_weapon_state_bots_a *state, qa_weight_config_bots_a *config) {
  int64_t pointer = config == NULL ? 0 : pointers_config(w, config);
  pointers_release_config(w, lib_u32(state->allocation.bytes));
  lib_set_u32(state->allocation.bytes, (uint32_t)pointer);
}
static qa_bot_memory_allocation *state_indexes(qa_weapon_ai_bots_a *w, const qa_weapon_state_bots_a *state) {
  qa_weapon_pointer_record *record = pointers_resolve(w, lib_u32(state->allocation.bytes + 4));
  if (record == NULL) return NULL;
  if (record->is_config) qa_ui_script_plain_error("weapon index pointer has wrong type");
  return &record->allocation;
}
static qa_weapon_state_bots_a *state_of(qa_weapon_ai_bots_a *w, int64_t handle) {
  if (handle <= 0 || handle > QA_MAX_WEAPON_STATES) {
    emit(w, QA_WEAPON_FATAL, "<weapon-ai>", "move state handle %lld out of range", (long long)handle);
    return NULL;
  }
  qa_weapon_state_bots_a *state = &w->states[handle];
  if (!state->used) {
    emit(w, QA_WEAPON_FATAL, "<weapon-ai>", "invalid move state %lld", (long long)handle);
    return NULL;
  }
  return state;
}
static void release_weights(qa_weapon_ai_bots_a *w, qa_weapon_state_bots_a *state) {
  qa_weight_config_bots_a *config = state_config(w, state);
  if (config != NULL) {
    qa_weight_config_store_free(w->weights, config);
    /* Source leaves dangling pointers. Retain the existing managed clear
     * after each successful free, without hiding a later failure's residue. */
    state_set_config(w, state, NULL);
  }
  qa_bot_memory_allocation *indexes = state_indexes(w, state);
  if (indexes != NULL) {
    uint32_t pointer = lib_u32(state->allocation.bytes + 4);
    qa_bot_memory_free(w->memory, indexes);
    record_delete(w, pointer);
    lib_set_u32(state->allocation.bytes + 4, 0);
  }
}
/* ---- lifecycle ---- */
/* TS sourceCapacity. */
static double source_capacity(double value, bool defined, double fallback, const char *label, bool *corrected) {
  *corrected = false;
  if (!defined) return fallback;
  if (!isfinite(value) || trunc(value) != value) qa_range_error("%s must be a finite integer", label);
  if (value < 0) {
    *corrected = true;
    return fallback;
  }
  if (value > 0x7fffffff) qa_range_error("%s must fit a source signed 32-bit integer", label);
  return value;
}
void qa_weapon_ai_bots_a_init(qa_weapon_ai_bots_a *w) {
  memset(w, 0, sizeof *w);
  qa_bot_memory_init(&w->owned_memory);
  w->memory = &w->owned_memory;
  w->next_pointer = 1;
  w->next_serial = 1;
}
static void js_number_emit(qa_weapon_ai_bots_a *w, const char *label, double value) {
  char *number = qa_input_js_number(value);
  emit(w, QA_WEAPON_ERROR, "<weapon-ai>", "%s = %s", label, number != NULL ? number : "");
  free(number);
}
void qa_weapon_ai_bots_a_configure(qa_weapon_ai_bots_a *w, const qa_bot_script_reader *resolver,
                                   qa_weight_config_store_bots_a *weights, const qa_weapon_ai_options *options) {
  w->resolver = *resolver;
  w->has_resolver = true;
  w->weights = weights;
  memset(&w->options, 0, sizeof w->options);
  if (options != NULL) w->options = *options;
  if (w->options.memory != NULL) w->memory = w->options.memory;
  memset(&w->preprocessor, 0, sizeof w->preprocessor);
  if (w->options.preprocessor != NULL) w->preprocessor = *w->options.preprocessor;
  /* A static capacity is validated, corrected and reported at construction. */
  bool corrected;
  if (w->options.max_weapon_info.fn == NULL) {
    double original = w->options.max_weapon_info.value;
    double value = source_capacity(original, w->options.max_weapon_info.has_value, DEFAULT_WEAPON_CAPACITY,
                                   "maxWeaponInfo", &corrected);
    w->options.max_weapon_info.has_value = true;
    w->options.max_weapon_info.value = value;
    if (corrected) js_number_emit(w, "max_weaponinfo", original);
  }
  if (w->options.max_projectile_info.fn == NULL) {
    double original = w->options.max_projectile_info.value;
    double value = source_capacity(original, w->options.max_projectile_info.has_value, DEFAULT_PROJECTILE_CAPACITY,
                                   "maxProjectileInfo", &corrected);
    w->options.max_projectile_info.has_value = true;
    w->options.max_projectile_info.value = value;
    if (corrected) js_number_emit(w, "max_projectileinfo", original);
  }
}
void qa_weapon_ai_bots_a_destroy(qa_weapon_ai_bots_a *w) {
  for (size_t i = 0; i < w->reported_count; i++) {
    free(w->reported[i].message);
    qa_ui_script_location_destroy(&w->reported[i].location);
  }
  for (size_t i = 0; i < w->config_count; i++) {
    free(w->configs[i]->path);
    lib_diagnostics_destroy(&w->configs[i]->diagnostics);
    free(w->configs[i]);
  }
  free(w->reported);
  free(w->configs);
  free(w->records);
  free(w->identities);
  w->reported = NULL;
  w->configs = NULL;
  w->records = NULL;
  w->identities = NULL;
  w->reported_count = w->reported_capacity = w->config_count = w->config_capacity = 0;
  w->record_count = w->record_capacity = w->identity_count = w->identity_capacity = 0;
  w->current_config = NULL;
  qa_bot_memory_destroy(&w->owned_memory);
}
const qa_weapon_diagnostic *qa_weapon_ai_diagnostics(const qa_weapon_ai_bots_a *w, size_t *out_count) {
  *out_count = w->reported_count;
  return w->reported;
}
qa_weapon_config_bots_a *qa_weapon_ai_config(const qa_weapon_ai_bots_a *w) { return w->current_config; }
/* ---- configuration loading ---- */
typedef struct weapon_tokens {
  qa_ui_script_source_reader *source;
  qa_ui_script_token *unread;
  const bool *retired; /* the TS RetiredWeaponSetup throw stops reading */
} weapon_tokens;
static int tokens_next(void *ctx, qa_ui_script_token **out) {
  weapon_tokens *tokens = ctx;
  if (tokens->unread != NULL) {
    *out = tokens->unread;
    tokens->unread = NULL;
    return QA_STRUCTURE_SOURCE_TOKEN;
  }
  if (*tokens->retired) return QA_STRUCTURE_SOURCE_FAILURE;
  *out = lib_source_token(tokens->source);
  if (*tokens->retired) {
    if (*out != NULL) qa_ui_script_token_release(*out);
    return QA_STRUCTURE_SOURCE_FAILURE;
  }
  return *out != NULL ? QA_STRUCTURE_SOURCE_TOKEN : QA_STRUCTURE_SOURCE_END;
}
static void tokens_unread(void *ctx, qa_ui_script_token *token) {
  weapon_tokens *tokens = ctx;
  if (tokens->unread != NULL) qa_ui_script_token_release(tokens->unread);
  tokens->unread = token;
}
typedef struct load_context {
  setup_scope *scope;
  const qa_ui_script_preprocessor_options *base;
  bool retired;
} load_context;
static void load_report(void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  load_context *load = ctx;
  if (load->base->report != NULL) load->base->report(load->base->report_ctx, diagnostic);
  if (!append_script_diagnostic(load->scope, false, diagnostic)) load->retired = true;
}
static bool load_resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  qa_weapon_ai_bots_a *w = ctx;
  return w->resolver.resolve(w->resolver.ctx, request, out);
}
typedef struct weapon_parser {
  qa_weapon_ai_bots_a *weapons;
  load_context *load;
  weapon_tokens *tokens;
  qa_structure_reader *reader;
  const char *path;
  int64_t weapon_capacity, projectile_capacity;
  qa_bot_memory_allocation allocation;
  bool *defined;
  size_t defined_length;
  char *thrown; /* TS non-script Error caught by setup */
} weapon_parser;
__attribute__((format(printf, 3, 4))) static bool parser_fail(weapon_parser *p, const qa_ui_script_location *at,
                                                              const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *text;
  if (vasprintf(&text, format, args) < 0) qa_range_error("out of memory");
  va_end(args);
  /* WeaponPrintDiagnostic joins the reported set when the caller handles the error. */
  qa_ui_script_diagnostic diagnostic = {QA_UI_SCRIPT_SEVERITY_ERROR, text, *at};
  (void)append_script_diagnostic(p->load->scope, true, &diagnostic);
  free(text);
  return false;
}
static bool read_vector(qa_structure_reader *reader, qa_bot_memory_allocation *scratch, int64_t offset) {
  float values[3];
  size_t count = 0;
  if (!qa_structure_reader_read_float_array(reader, 3, values, &count)) return false;
  for (size_t i = 0; i < count; i++) lib_set_f32(scratch->bytes + offset + (int64_t)i * 4, values[i]);
  return true;
}
/* The caller owns field; true continues the field loop. */
static bool read_weapon_field(weapon_parser *p, qa_bot_memory_allocation *weapon, const qa_structure_field *field) {
  typedef struct { const char *name; int64_t offset; char type; } spec;
  static const spec specs[] = {
      {"number", 4, 'i'},       {"name", 8, 's'},          {"level", 168, 'i'},
      {"model", 88, 's'},       {"weaponindex", 172, 'i'}, {"flags", 176, 'i'},
      {"projectile", 180, 's'}, {"numprojectiles", 260, 'i'}, {"hspread", 264, 'f'},
      {"vspread", 268, 'f'},    {"speed", 272, 'f'},       {"acceleration", 276, 'f'},
      {"recoil", 280, 'v'},     {"offset", 292, 'v'},      {"angleoffset", 304, 'v'},
      {"extrazvelocity", 316, 'f'}, {"ammoamount", 320, 'i'}, {"ammoindex", 324, 'i'},
      {"activate", 328, 'f'},   {"reload", 332, 'f'},      {"spinup", 336, 'f'},
      {"spindown", 340, 'f'},
  };
  for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++) {
    if (strcmp(specs[i].name, field->name) != 0) continue;
    int32_t integer;
    float real;
    char *text;
    switch (specs[i].type) {
      case 'i':
        if (!qa_structure_reader_read_int(p->reader, &integer)) return false;
        lib_set_i32(weapon->bytes + specs[i].offset, integer);
        return true;
      case 'f':
        if (!qa_structure_reader_read_float(p->reader, &real)) return false;
        lib_set_f32(weapon->bytes + specs[i].offset, real);
        return true;
      case 's':
        if (!qa_structure_reader_read_string(p->reader, QA_STRUCTURE_DEFAULT_STRING_BYTES, &text)) return false;
        set_text(weapon, specs[i].offset, text);
        free(text);
        return true;
      default:
        return read_vector(p->reader, weapon, specs[i].offset);
    }
  }
  return qa_structure_reader_reject_field(p->reader, field);
}
static bool read_projectile_field(weapon_parser *p, int64_t offset, const qa_structure_field *field) {
  typedef struct { const char *name; int64_t offset; char type; } spec;
  /* be_ai_weap.c uses WEAPON_OFS(model), eight bytes past projectile.model. */
  static const spec specs[] = {
      {"name", 0, 's'},        {"model", 88, 's'},      {"flags", 160, 'i'},   {"gravity", 164, 'f'},
      {"damage", 168, 'i'},    {"radius", 172, 'f'},    {"visdamage", 176, 'i'}, {"damagetype", 180, 'i'},
      {"healthinc", 184, 'i'}, {"push", 188, 'f'},      {"detonation", 192, 'f'}, {"bounce", 196, 'f'},
      {"bouncefric", 200, 'f'}, {"bouncestop", 204, 'f'},
  };
  for (size_t i = 0; i < sizeof specs / sizeof specs[0]; i++) {
    if (strcmp(specs[i].name, field->name) != 0) continue;
    int32_t integer;
    float real;
    char *text;
    switch (specs[i].type) {
      case 'i':
        if (!qa_structure_reader_read_int(p->reader, &integer)) return false;
        lib_set_i32(cell(&p->allocation, offset + specs[i].offset, 4), integer);
        return true;
      case 'f':
        if (!qa_structure_reader_read_float(p->reader, &real)) return false;
        lib_set_f32(cell(&p->allocation, offset + specs[i].offset, 4), real);
        return true;
      default:
        if (!qa_structure_reader_read_string(p->reader, QA_STRUCTURE_DEFAULT_STRING_BYTES, &text)) return false;
        set_text(&p->allocation, offset + specs[i].offset, text);
        free(text);
        return true;
    }
  }
  return qa_structure_reader_reject_field(p->reader, field);
}
static bool read_projectile_definition(weapon_parser *p, int64_t offset) {
  if (offset < 0 || offset + PROJECTILE_INFO_BYTES > (int64_t)p->allocation.length) {
    p->thrown = lib_strdup("projectile clear exceeds the source configuration allocation");
    return false;
  }
  memset(p->allocation.bytes + offset, 0, PROJECTILE_INFO_BYTES);
  if (!qa_structure_reader_begin(p->reader)) return false;
  while (true) {
    qa_structure_field field;
    bool found = false;
    if (!qa_structure_reader_next_field(p->reader, &field, &found)) return false;
    if (!found) return true;
    bool ok = read_projectile_field(p, offset, &field);
    qa_structure_field_release(&field);
    if (!ok) return false;
  }
}
static bool parse_weapon_config(weapon_parser *p, qa_weapon_config_bots_a *out) {
  qa_bot_memory_allocation *a = &p->allocation;
  /* The fixup loop reads only the four-byte valid flag for an unused record. */
  int64_t available = (int64_t)floor(((double)a->length - WEAPON_CONFIG_BYTES + WEAPON_INFO_BYTES - 4) / WEAPON_INFO_BYTES);
  int64_t weapons_length = p->weapon_capacity < available ? p->weapon_capacity : available;
  p->defined_length = weapons_length > 0 ? (size_t)weapons_length : 0;
  p->defined = lib_alloc(p->defined_length + 1);
  while (true) {
    qa_ui_script_token *definition;
    int status = tokens_next(p->tokens, &definition);
    if (status == QA_STRUCTURE_SOURCE_FAILURE) return false;
    if (status == QA_STRUCTURE_SOURCE_END) break;
    if (lib_token_is(definition, "weaponinfo")) {
      uint8_t scratch_bytes[WEAPON_INFO_BYTES];
      memset(scratch_bytes, 0, sizeof scratch_bytes);
      qa_bot_memory_allocation scratch = {scratch_bytes, sizeof scratch_bytes, NULL};
      bool ok = qa_structure_reader_begin(p->reader);
      while (ok) {
        qa_structure_field field;
        bool found = false;
        ok = qa_structure_reader_next_field(p->reader, &field, &found);
        if (!ok || !found) break;
        ok = read_weapon_field(p, &scratch, &field);
        qa_structure_field_release(&field);
      }
      if (!ok) {
        qa_ui_script_token_release(definition);
        return false;
      }
      int32_t number = lib_i32(scratch_bytes + 4);
      if (number < 0 || number >= p->weapon_capacity) {
        parser_fail(p, &definition->location, "weapon info number %d out of range in %s", number, p->path);
        qa_ui_script_token_release(definition);
        return false;
      }
      int64_t offset = WEAPON_CONFIG_BYTES + (int64_t)number * WEAPON_INFO_BYTES;
      /* Typed-array fill clamps; the assignment below raises through the view bounds. */
      if (offset < (int64_t)a->length) {
        size_t span = (size_t)(offset + WEAPON_INFO_BYTES) <= a->length ? WEAPON_INFO_BYTES : a->length - (size_t)offset;
        memset(a->bytes + offset, 0, span);
      }
      memcpy(cell(a, offset, WEAPON_INFO_BYTES), scratch_bytes, WEAPON_INFO_BYTES);
      lib_set_i32(a->bytes + offset, 1);
      if ((size_t)number >= p->defined_length) {
        size_t grown = (size_t)number + 1;
        p->defined = lib_realloc(p->defined, grown);
        memset(p->defined + p->defined_length, 0, grown - p->defined_length);
        p->defined_length = grown;
      }
      p->defined[number] = true;
    } else if (lib_token_is(definition, "projectileinfo")) {
      int32_t count = lib_i32(a->bytes + 4);
      if (count >= p->projectile_capacity) {
        parser_fail(p, &definition->location, "more than %lld projectiles defined in %s",
                    (long long)p->projectile_capacity, p->path);
        qa_ui_script_token_release(definition);
        return false;
      }
      int64_t offset = projectiles_start(p->weapon_capacity) + (int64_t)count * PROJECTILE_INFO_BYTES;
      if (!read_projectile_definition(p, offset)) {
        qa_ui_script_token_release(definition);
        return false;
      }
      lib_set_i32(a->bytes + 4, count + 1);
    } else {
      parser_fail(p, &definition->location, "unknown definition %s in %s", definition->text, p->path);
      qa_ui_script_token_release(definition);
      return false;
    }
    qa_ui_script_token_release(definition);
  }
  /* completeSource: every preprocessor diagnostic was appended through load_report. */
  if (p->load->retired || !scope_current(p->load->scope)) {
    p->load->retired = true;
    return false;
  }
  qa_ui_script_source_reader_dispose(p->tokens->source);
  int64_t projectile_count = lib_i32(a->bytes + 4);
  qa_ui_script_location file = {(char *)(uintptr_t)p->path, 1, 1};
  int64_t defined_count = 0;
  for (size_t index = 0; index < p->defined_length; index++) {
    if (!p->defined[index]) continue;
    defined_count++;
    char name[80], projectile[80];
    int64_t offset = WEAPON_CONFIG_BYTES + (int64_t)index * WEAPON_INFO_BYTES;
    read_text(a, offset + 8, name);
    read_text(a, offset + 180, projectile);
    if (name[0] == '\0') return parser_fail(p, &file, "weapon %zu has no name in %s", index, p->path);
    if (projectile[0] == '\0') return parser_fail(p, &file, "weapon %s has no projectile in %s", name, p->path);
    int64_t found = -1;
    int64_t start = projectiles_start(p->weapon_capacity);
    for (int64_t i = 0; i < projectile_count && found < 0; i++) {
      char candidate[80];
      read_text(a, start + i * PROJECTILE_INFO_BYTES, candidate);
      if (strcmp(candidate, projectile) == 0) found = i;
    }
    if (found < 0) return parser_fail(p, &file, "weapon %s uses undefined projectile in %s", name, p->path);
    memmove(a->bytes + offset + PROJECTILE_INFO_OFFSET, a->bytes + start + found * PROJECTILE_INFO_BYTES,
            PROJECTILE_INFO_BYTES);
  }
  if ((int64_t)p->defined_length < p->weapon_capacity) {
    p->thrown = lib_strdup("weapon validation exceeds the source configuration allocation");
    return false;
  }
  size_t count = 0;
  const qa_ui_script_diagnostic *diagnostics = qa_ui_script_source_reader_diagnostics(p->tokens->source, &count);
  for (size_t i = 0; i < count; i++) lib_diagnostics_copy(&out->diagnostics, &diagnostics[i]);
  if (p->weapon_capacity == 0) {
    lib_diagnostics_push(&out->diagnostics, QA_UI_SCRIPT_SEVERITY_WARNING, "no weapon info loaded", p->path, 1, 1);
    if (!append_script_diagnostic(p->load->scope, true, &out->diagnostics.items[out->diagnostics.count - 1])) {
      p->load->retired = true;
      return false;
    }
  }
  out->defined_weapon_count = defined_count;
  out->projectile_count = (size_t)projectile_count;
  return true;
}
static qa_weapon_config_bots_a *config_create(qa_weapon_ai_bots_a *w, qa_bot_memory_allocation allocation, const char *path) {
  qa_weapon_config_bots_a *config = lib_alloc(sizeof *config);
  config->memory = w->memory;
  config->allocation = allocation;
  config->path = lib_strdup(path);
  if (w->config_count == w->config_capacity) {
    w->config_capacity = lib_grow(w->config_capacity, w->config_count + 1);
    w->configs = lib_realloc(w->configs, w->config_capacity * sizeof *w->configs);
  }
  w->configs[w->config_count++] = config;
  return config;
}
typedef enum load_status { LOAD_OK, LOAD_SCRIPT_ERROR, LOAD_THROWN, LOAD_RETIRED } load_status;
/* TS loadConfig; *out_thrown holds a non-script error message for the setup catch. */
static load_status load_config(qa_weapon_ai_bots_a *w, const char *path, int64_t weapon_capacity,
                               int64_t projectile_capacity, setup_scope *scope, qa_weapon_config_bots_a **out,
                               char **out_thrown) {
  if (!w->has_resolver) qa_range_error("WeaponAi has no script resolver");
  qa_ui_script_source source;
  if (!w->resolver.resolve_root(w->resolver.ctx, path, &source)) {
    *out_thrown = lib_format("counldn't load %s", path);
    return LOAD_THROWN;
  }
  if (source.path == NULL || source.path[0] == '\0') {
    *out_thrown = lib_strdup("weapon config resolver returned an empty canonical path");
    return LOAD_THROWN;
  }
  char *canonical = lib_strdup(source.path);
  load_context load = {scope, &w->preprocessor, false};
  qa_ui_script_preprocessor_options options = w->preprocessor;
  options.globals = w->resolver.globals;
  if (w->resolver.debug_eval != NULL) {
    options.debug_eval = w->resolver.debug_eval;
    options.debug_eval_ctx = w->resolver.debug_eval_ctx;
  }
  options.report = load_report;
  options.report_ctx = &load;
  qa_ui_script_source_reader *preprocessor = qa_ui_script_source_reader_open(&source, load_resolve, w, &options);
  if (preprocessor == NULL) qa_range_error("out of memory");
  /* (WEAPON_CONFIG_BYTES + imul + imul) | 0 wraps; a negative size is memory.allocate's RangeError. */
  int32_t size = (int32_t)((uint32_t)WEAPON_CONFIG_BYTES + (uint32_t)(weapon_capacity * WEAPON_INFO_BYTES) +
                           (uint32_t)(projectile_capacity * PROJECTILE_INFO_BYTES));
  if (size < 0 || (int64_t)size > 2147483647LL - 4) {
    *out_thrown = lib_strdup("Bot memory allocation must fit a signed source size with its four-byte prefix");
    free(canonical);
    return LOAD_THROWN;
  }
  qa_bot_memory_allocation allocation = lib_mem_alloc(w->memory, size, QA_BOT_MEMORY_HUNK, true);
  /* Managed pointers are allocation-relative offsets in the selected 32-bit layout. */
  lib_set_i32(cell(&allocation, 0, 4), (int32_t)weapon_capacity);
  lib_set_i32(cell(&allocation, 8, 4), (int32_t)(WEAPON_CONFIG_BYTES + (uint32_t)(weapon_capacity * WEAPON_INFO_BYTES)));
  lib_set_i32(cell(&allocation, 12, 4), WEAPON_CONFIG_BYTES);
  weapon_tokens tokens = {preprocessor, NULL, &load.retired};
  size_t prior_count = 0;
  const qa_ui_script_diagnostic *prior = qa_ui_script_source_reader_diagnostics(preprocessor, &prior_count);
  qa_structure_reader reader;
  qa_structure_reader_init(&reader, (qa_structure_token_source){&tokens, tokens_next, tokens_unread}, canonical,
                           prior, prior_count, NULL, NULL);
  reader.capture_range_errors = true;
  qa_weapon_config_bots_a *config = config_create(w, allocation, canonical);
  weapon_parser parser = {w, &load, &tokens, &reader, canonical, weapon_capacity, projectile_capacity, allocation,
                          NULL, 0, NULL};
  bool ok = parse_weapon_config(&parser, config);
  free(parser.defined);
  if (tokens.unread != NULL) qa_ui_script_token_release(tokens.unread);
  load_status status = LOAD_OK;
  if (ok && (load.retired || !scope_current(scope))) {
    status = LOAD_RETIRED;
  } else if (!ok) {
    if (load.retired) {
      status = LOAD_RETIRED;
    } else if (parser.thrown != NULL || reader.range_error != NULL) {
      /* Non-script errors skip the TS catch's free/dispose and reach the setup catch. */
      *out_thrown = parser.thrown != NULL ? parser.thrown : lib_strdup(reader.range_error);
      status = LOAD_THROWN;
    } else {
      /* The StructureReader failure joins the reported set here; parser_fail already appended. */
      size_t count = 0;
      const qa_ui_script_diagnostic *issues = qa_structure_reader_diagnostics(&reader, &count);
      bool current = true;
      if (count > prior_count) current = append_script_diagnostic(scope, false, &issues[count - 1]);
      status = LOAD_SCRIPT_ERROR;
      if (current && scope_current(scope)) {
        qa_bot_memory_free(w->memory, &config->allocation);
        qa_ui_script_source_reader_dispose(preprocessor);
      } else {
        status = LOAD_RETIRED;
      }
    }
  }
  qa_structure_reader_destroy(&reader);
  qa_ui_script_source_reader_destroy(preprocessor);
  free(canonical);
  *out = config;
  return status;
}
static void validate_config_path(const char *path) {
  size_t length = strlen(path);
  if (length == 0) qa_range_error("weapon config path cannot be empty");
  if (length > MAX_CONFIG_PATH_BYTES)
    qa_range_error("weapon config path cannot exceed %d source bytes", MAX_CONFIG_PATH_BYTES);
}
typedef struct debug_writer {
  qa_weapon_ai_bots_a *weapons;
} debug_writer;
static int64_t debug_write(void *ctx, const char *text) {
  debug_writer *writer = ctx;
  return writer->weapons->options.debug_file_write(writer->weapons->options.debug_ctx, text);
}
qa_weapon_load_result qa_weapon_ai_setup(qa_weapon_ai_bots_a *w, const char *path_argument) {
  const char *path = path_argument != NULL ? path_argument : "weapons.c";
  validate_config_path(path);
  setup_scope scope = {w, w->generation, ++w->setup_revision};
  qa_weapon_capacity weapon_option = w->options.max_weapon_info;
  double weapon_value = weapon_option.fn != NULL ? weapon_option.fn(weapon_option.ctx) : weapon_option.value;
  if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  bool corrected;
  double weapons = source_capacity(weapon_value, weapon_option.fn != NULL || weapon_option.has_value,
                                   DEFAULT_WEAPON_CAPACITY, "maxWeaponInfo", &corrected);
  if (corrected) {
    js_number_emit(w, "max_weaponinfo", weapon_value);
    if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  }
  qa_weapon_capacity projectile_option = w->options.max_projectile_info;
  double projectile_value = projectile_option.fn != NULL ? projectile_option.fn(projectile_option.ctx) : projectile_option.value;
  if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  double projectiles = source_capacity(projectile_value, projectile_option.fn != NULL || projectile_option.has_value,
                                       DEFAULT_PROJECTILE_CAPACITY, "maxProjectileInfo", &corrected);
  if (corrected) {
    js_number_emit(w, "max_projectileinfo", projectile_value);
    if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  }
  qa_weapon_config_bots_a *config = NULL;
  char *thrown = NULL;
  load_status status = load_config(w, path, (int64_t)weapons, (int64_t)projectiles, &scope, &config, &thrown);
  if (status != LOAD_OK) {
    if (!scope_current(&scope)) {
      free(thrown);
      return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
    }
    if (thrown != NULL) emit(w, QA_WEAPON_ERROR, path, "%s", thrown);
    free(thrown);
    if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
    w->current_config = NULL;
    emit(w, QA_WEAPON_FATAL, path, "couldn't load the weapon config");
    return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  }
  if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  emit(w, QA_WEAPON_MESSAGE, config->path, "loaded %s", config->path);
  if (!scope_current(&scope)) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  w->current_config = config;
  if (w->options.debug_has_file != NULL && w->options.debug_has_file(w->options.debug_ctx)) {
    debug_writer writer = {w};
    for (size_t index = 0; index < config->projectile_count; index++) {
      (void)qa_write_structure_bots_a(debug_write, &writer, &projectile_dump,
                                      qa_weapon_config_projectile_bytes(config, (int64_t)index), PROJECTILE_INFO_BYTES);
      if (w->options.debug_flush != NULL) w->options.debug_flush(w->options.debug_ctx);
    }
    int64_t capacity = config_capacity(config);
    for (int64_t index = 0; index < capacity; index++) {
      (void)qa_write_structure_bots_a(debug_write, &writer, &weapon_dump,
                                      qa_weapon_config_weapon_bytes(config, index), WEAPON_INFO_BYTES);
      if (w->options.debug_flush != NULL) w->options.debug_flush(w->options.debug_ctx);
    }
  }
  return QA_WEAPON_LOAD_NO_ERROR;
}
int64_t qa_weapon_ai_allocate_state(qa_weapon_ai_bots_a *w) {
  for (int64_t handle = 1; handle <= QA_MAX_WEAPON_STATES; handle++) {
    if (w->states[handle].used) continue;
    w->states[handle] = (qa_weapon_state_bots_a){true, w->next_serial++,
                                                 lib_mem_alloc(w->memory, 8, QA_BOT_MEMORY_HEAP, true), 0};
    return handle;
  }
  return 0;
}
void qa_weapon_ai_free_state(qa_weapon_ai_bots_a *w, int64_t handle) {
  qa_weapon_state_bots_a *state = state_of(w, handle);
  if (state == NULL) return;
  state->revision++;
  release_weights(w, state);
  qa_bot_memory_free(w->memory, &state->allocation);
  state->used = false;
}
void qa_weapon_ai_free_weights(qa_weapon_ai_bots_a *w, int64_t handle) {
  qa_weapon_state_bots_a *state = state_of(w, handle);
  if (state == NULL) return;
  state->revision++;
  release_weights(w, state);
}
/* TS reset rewrites both retained words: BotResetWeaponState preserves them. */
void qa_weapon_ai_reset_state(qa_weapon_ai_bots_a *w, int64_t handle) { (void)state_of(w, handle); }
static qa_weapon_load_result load_weights(qa_weapon_ai_bots_a *w, int64_t handle, const char *fixed,
                                          const char *(*read)(void *ctx), void *ctx) {
  qa_weapon_state_bots_a *state = state_of(w, handle);
  if (state == NULL) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_WEIGHTS;
  state->revision++;
  int64_t revision = state->revision, generation = w->generation;
  uint64_t serial = state->serial;
  release_weights(w, state);
  char *path = lib_strdup(fixed != NULL ? fixed : read(ctx));
  qa_weight_config_error error = {QA_WEIGHT_CONFIG_ERROR_NONE, NULL};
  qa_weight_config_bots_a *weights = qa_weight_config_store_load(w->weights, path, &error);
  bool same = generation == w->generation && state->used && state->serial == serial && state->revision == revision;
  if (weights == NULL) {
    qa_weight_config_error_destroy(&error);
    if (same) emit(w, QA_WEAPON_FATAL, path, "couldn't load weapon config %s", path);
    free(path);
    return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_WEIGHTS;
  }
  free(path);
  if (!same) {
    qa_weight_config_store_free(w->weights, weights);
    return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_WEIGHTS;
  }
  state_set_config(w, state, weights);
  qa_weapon_config_bots_a *config = w->current_config;
  if (config == NULL) return QA_WEAPON_LOAD_CANNOT_LOAD_WEAPON_CONFIG;
  int64_t capacity = config_capacity(config);
  qa_bot_memory_allocation indexes = lib_mem_alloc(w->memory, capacity * 4, QA_BOT_MEMORY_HEAP, true);
  for (int64_t index = 0; index < capacity; index++) {
    char name[80] = "";
    if (weapon_valid_at(config, index)) read_text(&config->allocation, WEAPON_CONFIG_BYTES + index * WEAPON_INFO_BYTES + 8, name);
    lib_set_i32(indexes.bytes + index * 4, (int32_t)qa_weight_config_find(weights, name));
  }
  int64_t pointer = pointer_next(w);
  record_append(w, (qa_weapon_pointer_record){pointer, false, NULL, 0, indexes});
  lib_set_u32(state->allocation.bytes + 4, (uint32_t)pointer);
  return QA_WEAPON_LOAD_NO_ERROR;
}
qa_weapon_load_result qa_weapon_ai_load_weights(qa_weapon_ai_bots_a *w, int64_t handle, const char *path) {
  return load_weights(w, handle, path, NULL, NULL);
}
qa_weapon_load_result qa_weapon_ai_load_weights_fn(qa_weapon_ai_bots_a *w, int64_t handle,
                                                   const char *(*read)(void *ctx), void *ctx) {
  return load_weights(w, handle, NULL, read, ctx);
}
static const qa_weapon_config_bots_a *weapon_info_config(qa_weapon_ai_bots_a *w, int64_t handle, int64_t number) {
  const qa_weapon_config_bots_a *config = w->current_config;
  if (config == NULL) return NULL;
  if (number <= 0 || number > config_capacity(config)) {
    emit(w, QA_WEAPON_ERROR, config->path, "weapon number out of range");
    return NULL;
  }
  if (state_of(w, handle) == NULL) return NULL;
  return config;
}
const uint8_t *qa_weapon_ai_weapon_info_bytes(qa_weapon_ai_bots_a *w, int64_t handle, int64_t weapon, size_t *out_length) {
  const qa_weapon_config_bots_a *config = weapon_info_config(w, handle, weapon);
  if (config == NULL) return NULL;
  if (out_length != NULL) *out_length = WEAPON_INFO_BYTES;
  return qa_weapon_config_weapon_bytes(config, weapon);
}
bool qa_weapon_ai_get_weapon_info(qa_weapon_ai_bots_a *w, int64_t handle, int64_t weapon, qa_weapon_info_bots_a *out) {
  const qa_weapon_config_bots_a *config = weapon_info_config(w, handle, weapon);
  if (config == NULL) return false;
  qa_weapon_config_weapon_info(config, weapon, out);
  return true;
}
bool qa_weapon_ai_evaluate_fight_weapon(qa_weapon_ai_bots_a *w, int64_t handle, int64_t weapon,
                                        qa_weight_inventory inventory, float *out) {
  qa_weapon_state_bots_a *state = state_of(w, handle);
  const qa_weapon_config_bots_a *config = w->current_config;
  if (state == NULL || config == NULL) return false;
  qa_weight_config_bots_a *weights = state_config(w, state);
  const qa_bot_memory_allocation *indexes = state_indexes(w, state);
  if (weights == NULL || indexes == NULL) return false;
  if (!weapon_valid_at(config, weapon)) return false;
  if ((uint64_t)(weapon * 4) >= indexes->length) return false;
  int32_t index = lib_i32(indexes->bytes + weapon * 4);
  if (index < 0) return false;
  *out = qa_weight_config_evaluate(weights, index, inventory);
  return true;
}
int64_t qa_weapon_ai_choose_best_fight_weapon(qa_weapon_ai_bots_a *w, int64_t handle, qa_weight_inventory inventory) {
  qa_weapon_state_bots_a *state = state_of(w, handle);
  const qa_weapon_config_bots_a *config = w->current_config;
  if (state == NULL || config == NULL || state_config(w, state) == NULL) return 0;
  float best_weight = 0;
  int64_t best_weapon = 0, capacity = config_capacity(config);
  for (int64_t index = 0; index < capacity; index++) {
    if (!weapon_valid_at(config, index)) continue;
    if (state_indexes(w, state) == NULL) return best_weapon;
    float weight;
    if (!qa_weapon_ai_evaluate_fight_weapon(w, handle, index, inventory, &weight)) continue;
    if (weight > best_weight) {
      best_weight = weight;
      best_weapon = index;
    }
  }
  return best_weapon;
}
void qa_weapon_ai_shutdown(qa_weapon_ai_bots_a *w) {
  w->generation++;
  w->setup_revision++;
  if (w->current_config != NULL) qa_bot_memory_free(w->memory, &w->current_config->allocation);
  w->current_config = NULL;
  for (int64_t handle = 1; handle <= QA_MAX_WEAPON_STATES; handle++) {
    qa_weapon_state_bots_a *state = &w->states[handle];
    if (!state->used) continue;
    state->revision++;
    release_weights(w, state);
    qa_bot_memory_free(w->memory, &state->allocation);
    state->used = false;
  }
}
/* ---- checkpoint ---- */
qa_save_value *qa_weapon_ai_checkpoint(qa_weapon_ai_bots_a *w, qa_bot_memory_capture *memory) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "generation", (double)w->generation);
  lib_set_num(object, "setupRevision", (double)w->setup_revision);
  qa_save_value *reported = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < w->reported_count; i++) {
    const qa_weapon_diagnostic *d = &w->reported[i];
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set(entry, "origin", qa_save_value_string(d->origin_print ? "print" : "source"));
    lib_set(entry, "severity", qa_save_value_string(severity_name(d->severity)));
    lib_set(entry, "message", qa_save_value_string(d->message));
    qa_save_value *location = lib_sv(qa_save_value_object());
    lib_set(location, "path", qa_save_value_string(d->location.path));
    lib_set_num(location, "line", (double)d->location.line);
    lib_set_num(location, "column", (double)d->location.column);
    lib_set(entry, "location", location);
    lib_push(reported, entry);
  }
  lib_set(object, "reported", reported);
  qa_save_value *pointers = lib_sv(qa_save_value_object());
  lib_set_num(pointers, "next", (double)w->next_pointer);
  qa_save_value *records = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < w->record_count; i++) {
    qa_weapon_pointer_record *record = &w->records[i];
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)record->pointer);
    lib_set(entry, "kind", qa_save_value_string(record->is_config ? "config" : "indexes"));
    lib_set_num(entry, "target",
                record->is_config ? (double)qa_weight_config_store_reference(w->weights, record->config)
                                  : (double)qa_bot_memory_capture_reference(memory, &record->allocation));
    lib_set_num(entry, "references", record->is_config ? (double)record->references : 0);
    lib_push(records, entry);
  }
  lib_set(pointers, "records", records);
  qa_save_value *configs = lib_sv(qa_save_value_array());
  size_t owned_count = 0;
  qa_weight_config_bots_a *const *owned = qa_weight_config_store_configurations(w->weights, &owned_count);
  for (size_t i = 0; i < owned_count; i++) {
    int64_t pointer = identity_find(w, owned[i]);
    if (pointer == 0) continue;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)pointer);
    lib_set_num(entry, "config", (double)qa_weight_config_store_reference(w->weights, owned[i]));
    lib_push(configs, entry);
  }
  lib_set(pointers, "configs", configs);
  lib_set(object, "pointers", pointers);
  qa_save_value *states = lib_sv(qa_save_value_array());
  for (int64_t handle = 0; handle <= QA_MAX_WEAPON_STATES; handle++) {
    qa_weapon_state_bots_a *state = &w->states[handle];
    if (!state->used) {
      lib_push(states, qa_save_value_null());
      continue;
    }
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "allocation", (double)qa_bot_memory_capture_reference(memory, &state->allocation));
    lib_set_num(entry, "revision", (double)state->revision);
    lib_push(states, entry);
  }
  lib_set(object, "states", states);
  if (w->current_config == NULL) {
    lib_set(object, "config", qa_save_value_null());
  } else {
    qa_save_value *config = lib_sv(qa_save_value_object());
    lib_set_num(config, "allocation", (double)qa_bot_memory_capture_reference(memory, &w->current_config->allocation));
    lib_set(config, "path", qa_save_value_string(w->current_config->path));
    lib_set(config, "diagnostics", lib_diagnostics_value(&w->current_config->diagnostics));
    lib_set(object, "config", config);
  }
  return object;
}
static void pointers_restore(qa_weapon_ai_bots_a *w, const qa_save_value *value, qa_bot_memory_restored *memory) {
  static const char *const kinds[] = {"config", "indexes"};
  qa_save_reader reader, records, configs;
  if (!qa_save_reader_init(&reader, value, "bot.weapons.pointers")) qa_range_error("out of memory");
  double next = lib_int_min(&reader, "next", 1);
  lib_field(&reader, "records", &records);
  lib_field(&reader, "configs", &configs);
  if (w->record_count != 0 || next < 1 || next > 4294967296.0)
    qa_ui_script_plain_error("Invalid weapon pointer restoration");
  size_t record_count = qa_save_reader_list_count(&records);
  for (size_t i = 0; i < record_count; i++) {
    qa_save_reader entry, kind_reader;
    lib_item(&records, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    lib_field(&entry, "kind", &kind_reader);
    bool is_config = strcmp(qa_save_reader_choice_string(&kind_reader, kinds, 2), "config") == 0;
    qa_save_reader_destroy(&kind_reader);
    double target = lib_int_min(&entry, "target", 0);
    double references = lib_int_min(&entry, "references", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next || record_find(w, (int64_t)pointer) != NULL)
      qa_ui_script_plain_error("Invalid saved weapon pointer");
    if (is_config) {
      if (references < 1) qa_ui_script_plain_error("Invalid saved weapon weight reference count");
      record_append(w, (qa_weapon_pointer_record){(int64_t)pointer, true, qa_weight_config_store_resolve(w->weights, target),
                                                  (int64_t)references, {NULL, 0, NULL}});
    } else {
      record_append(w, (qa_weapon_pointer_record){(int64_t)pointer, false, NULL, 0,
                                                  qa_bot_memory_restored_allocation(memory, (int64_t)target)});
    }
  }
  size_t config_count = qa_save_reader_list_count(&configs);
  for (size_t i = 0; i < config_count; i++) {
    qa_save_reader entry;
    lib_item(&configs, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    double reference = lib_int_min(&entry, "config", 0);
    qa_save_reader_destroy(&entry);
    qa_weight_config_bots_a *config = qa_weight_config_store_resolve(w->weights, reference);
    if (pointer < 1 || pointer >= next || identity_find(w, config) != 0)
      qa_ui_script_plain_error("Invalid saved weapon weight identity");
    qa_weapon_pointer_record *live = record_find(w, (int64_t)pointer);
    if (live != NULL && (!live->is_config || live->config != config))
      qa_ui_script_plain_error("Saved weapon weight identities disagree");
    identity_set(w, config, (int64_t)pointer);
  }
  w->next_pointer = (int64_t)next;
  qa_save_reader_destroy(&records);
  qa_save_reader_destroy(&configs);
  qa_save_reader_destroy(&reader);
}
void qa_weapon_ai_restore(qa_weapon_ai_bots_a *w, const qa_save_value *value, qa_bot_memory_restored *memory) {
  static const char *const origins[] = {"source", "print"};
  static const char *const severities[] = {"message", "warning", "error", "fatal"};
  qa_save_reader reader, reported, pointers, states, config;
  if (!qa_save_reader_init(&reader, value, "bot.weapons")) qa_range_error("out of memory");
  double generation = lib_int_min(&reader, "generation", 0);
  double revision = lib_int_min(&reader, "setupRevision", 0);
  lib_field(&reader, "reported", &reported);
  lib_field(&reader, "pointers", &pointers);
  lib_field(&reader, "states", &states);
  lib_field(&reader, "config", &config);
  bool any = w->current_config != NULL;
  for (int64_t handle = 0; handle <= QA_MAX_WEAPON_STATES && !any; handle++) any = w->states[handle].used;
  bool first_null = false;
  if (qa_save_reader_list_count(&states) == QA_MAX_WEAPON_STATES + 1) {
    qa_save_reader first;
    lib_item(&states, 0, &first);
    first_null = qa_save_reader_is_null(&first);
    qa_save_reader_destroy(&first);
  }
  if (any || !first_null || generation < 0 || revision < 0)
    qa_ui_script_plain_error("Invalid bot weapon restoration");
  pointers_restore(w, pointers.value, memory);
  for (size_t index = 0; index <= QA_MAX_WEAPON_STATES; index++) {
    qa_save_reader entry;
    lib_item(&states, index, &entry);
    if (!qa_save_reader_is_null(&entry)) {
      double reference = lib_int_min(&entry, "allocation", 0);
      double state_revision = lib_int_min(&entry, "revision", 0);
      qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
      if (allocation.length != 8 || state_revision < 0) qa_ui_script_plain_error("Invalid saved bot weapon state");
      w->states[index] = (qa_weapon_state_bots_a){true, w->next_serial++, allocation, (int64_t)state_revision};
      (void)state_config(w, &w->states[index]);
    }
    qa_save_reader_destroy(&entry);
  }
  if (!qa_save_reader_is_null(&config)) {
    double reference = lib_int_min(&config, "allocation", 0);
    char *path = lib_read_string(&config, "path");
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (allocation.length < WEAPON_CONFIG_BYTES)
      qa_ui_script_plain_error("Saved weapon configuration header exceeds allocation");
    int32_t capacity = lib_i32(allocation.bytes), count = lib_i32(allocation.bytes + 4);
    if (capacity < 0 || count < 0 ||
        WEAPON_CONFIG_BYTES + (int64_t)capacity * WEAPON_INFO_BYTES + (int64_t)count * PROJECTILE_INFO_BYTES >
            (int64_t)allocation.length)
      qa_ui_script_plain_error("Saved weapon configuration header exceeds allocation");
    qa_weapon_config_bots_a *restored = config_create(w, allocation, path);
    free(path);
    lib_read_diagnostics(&config, "diagnostics", &restored->diagnostics);
    restored->projectile_count = (size_t)count;
    int64_t defined = 0;
    for (int64_t index = 0; index < capacity; index++) defined += weapon_valid_at(restored, index) ? 1 : 0;
    restored->defined_weapon_count = defined;
    w->current_config = restored;
  }
  w->generation = (int64_t)generation;
  w->setup_revision = (int64_t)revision;
  size_t reported_count = qa_save_reader_list_count(&reported);
  for (size_t i = 0; i < reported_count; i++) {
    qa_save_reader entry, origin_reader, severity_reader, location;
    lib_item(&reported, i, &entry);
    lib_field(&entry, "origin", &origin_reader);
    bool print = strcmp(qa_save_reader_choice_string(&origin_reader, origins, 2), "print") == 0;
    lib_field(&entry, "severity", &severity_reader);
    const char *severity = qa_save_reader_choice_string(&severity_reader, severities, 4);
    qa_weapon_severity level = QA_WEAPON_MESSAGE;
    for (int s = 0; s < 4; s++) if (strcmp(severity, severities[s]) == 0) level = (qa_weapon_severity)s;
    char *message = lib_read_string(&entry, "message");
    lib_field(&entry, "location", &location);
    char *path = lib_read_string(&location, "path");
    qa_ui_script_location at = {path, (int64_t)lib_int_min(&location, "line", 0), (int64_t)lib_int_min(&location, "column", 0)};
    (void)push_reported(w, print, level, message, &at);
    free(path);
    free(message);
    qa_save_reader_destroy(&location);
    qa_save_reader_destroy(&severity_reader);
    qa_save_reader_destroy(&origin_reader);
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&reported);
  qa_save_reader_destroy(&pointers);
  qa_save_reader_destroy(&states);
  qa_save_reader_destroy(&config);
  qa_save_reader_destroy(&reader);
}
