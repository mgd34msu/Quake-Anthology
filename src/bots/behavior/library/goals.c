/* See header. be_ai_goal.c layouts in the 32-bit source shape: itemconfig_t followed by
 * iteminfo_t cells; pointer words hold owner-local IDs or one-based cell IDs.
 * GPL-2.0-or-later. */
#include "qa/bots/behavior/library/goals.h"
#include "library_internal.h"
#include "qa/bots/behavior/library/structure.h"
#include "qa/core/math.h"
#include "qa/input/support.h"
#include <math.h>
#include <setjmp.h>
#define ITEM_CONFIG_BYTES 8
#define ITEM_INFO_BYTES 236
#define LEVEL_ITEM_BYTES 60
#define MAP_LOCATION_BYTES 148
#define CAMP_SPOT_BYTES 164
#define GOAL_STACK_OFFSET 16
#define GOAL_STACK_TOP_OFFSET (GOAL_STACK_OFFSET + QA_MAX_GOAL_STACK * QA_GOAL_BYTES)
#define AVOID_GOALS_OFFSET (GOAL_STACK_TOP_OFFSET + 4)
#define AVOID_TIMES_OFFSET (AVOID_GOALS_OFFSET + QA_MAX_AVOID_GOALS * 4)
#define GOAL_STATE_BYTES (AVOID_TIMES_OFFSET + QA_MAX_AVOID_GOALS * 4)
struct qa_item_config_bots_a {
  qa_bot_memory *memory;
  qa_bot_memory *owned_memory; /* TS `new BotMemory()` default */
  qa_bot_memory_allocation allocation;
  char *path;
  lib_diagnostics diagnostics;
};
bool qa_is_source_goal_number(double n) {
  return trunc(n) == n && n >= QA_SOURCE_GOAL_NUMBER_MIN && n <= 2147483647.0;
}
bool qa_touching_goal(qa_vec3 o, qa_bot_goal g) {
  qa_vec3 pmin = {-15, -15, -24}, pmax = {15, 15, 32};
  qa_vec3 mn = qa_add3(qa_sub3(g.mins, pmax), g.origin);
  qa_vec3 mx = qa_add3(qa_sub3(g.maxs, pmin), g.origin);
  if (o.x < mn.x || o.x > mx.x) return false;
  if (o.y < mn.y || o.y > mx.y) return false;
  if (o.z < mn.z || o.z > mx.z) return false;
  return true;
}
/* ---- item info cells ---- */
static void cell_text(const uint8_t *cell, size_t offset, size_t length, char *out) {
  size_t used = 0;
  while (used < length && cell[offset + used] != 0) {
    out[used] = (char)cell[offset + used];
    used++;
  }
  out[used] = '\0';
}
static void cell_set_text(uint8_t *cell, size_t offset, size_t length, const char *value) {
  memset(cell + offset, 0, length);
  size_t size = strlen(value);
  if (size > length - 1) size = length - 1;
  memcpy(cell + offset, value, size);
}
#define I_MODEL_INDEX(c) lib_i32((c) + 192)
#define I_RESPAWN(c) lib_f32((c) + 204)
#define I_NUMBER(c) lib_i32((c) + 232)
static const uint8_t *config_cell(const qa_item_config_bots_a *config, size_t index) {
  return config->allocation.bytes + ITEM_CONFIG_BYTES + index * ITEM_INFO_BYTES;
}
size_t qa_item_config_count(const qa_item_config_bots_a *config) {
  return (size_t)lib_i32(config->allocation.bytes);
}
const char *qa_item_config_path(const qa_item_config_bots_a *config) { return config->path; }
void qa_item_config_item(const qa_item_config_bots_a *config, size_t index, qa_item_info_bots_a *out) {
  const uint8_t *cell = config_cell(config, index);
  cell_text(cell, 0, 31, out->classname);
  cell_text(cell, 32, 79, out->name);
  cell_text(cell, 112, 79, out->model);
  out->model_index = I_MODEL_INDEX(cell);
  out->type = lib_i32(cell + 196);
  out->index = lib_i32(cell + 200);
  out->respawn_time = I_RESPAWN(cell);
  out->mins = lib_vec3(cell + 208);
  out->maxs = lib_vec3(cell + 220);
  out->number = I_NUMBER(cell);
}
const qa_ui_script_diagnostic *qa_item_config_diagnostics(const qa_item_config_bots_a *config, size_t *out_count) {
  *out_count = config->diagnostics.count;
  return config->diagnostics.items;
}
void qa_item_config_free(qa_item_config_bots_a *config) {
  qa_bot_memory_free(config->memory, &config->allocation);
}
void qa_item_config_destroy(qa_item_config_bots_a *config) {
  if (config->owned_memory != NULL) {
    qa_bot_memory_destroy(config->owned_memory);
    free(config->owned_memory);
  }
  lib_diagnostics_destroy(&config->diagnostics);
  free(config->path);
  free(config);
}
/* ---- loadItemConfig ---- */
typedef struct item_tokens {
  qa_ui_script_source_reader *source;
  qa_ui_script_token *unread;
} item_tokens;
static int item_tokens_next(void *ctx, qa_ui_script_token **out) {
  item_tokens *tokens = ctx;
  if (tokens->unread != NULL) {
    *out = tokens->unread;
    tokens->unread = NULL;
    return QA_STRUCTURE_SOURCE_TOKEN;
  }
  *out = lib_source_token(tokens->source);
  return *out != NULL ? QA_STRUCTURE_SOURCE_TOKEN : QA_STRUCTURE_SOURCE_END;
}
static void item_tokens_unread(void *ctx, qa_ui_script_token *token) {
  item_tokens *tokens = ctx;
  if (tokens->unread != NULL) qa_ui_script_token_release(tokens->unread);
  tokens->unread = token;
}
static void emit_report(qa_goal_report_fn report, void *ctx, qa_goal_severity severity, const char *message) {
  if (report == NULL) return;
  qa_goal_diagnostic diagnostic = {severity, (char *)(uintptr_t)message};
  report(ctx, &diagnostic);
}
typedef struct item_load {
  const qa_item_config_options *options;
  const qa_ui_script_preprocessor_options *base;
  const qa_bot_script_reader *resolver;
  size_t source_reported; /* reader diagnostics already reported, in order */
} item_load;
static void report_script_diagnostic(const item_load *load, const qa_ui_script_diagnostic *d) {
  if (load->options->report == NULL) return;
  char *text = lib_format("file %s, line %lld: %s\n", d->location.path, (long long)d->location.line, d->message);
  emit_report(load->options->report, load->options->report_ctx,
              d->severity == QA_UI_SCRIPT_SEVERITY_WARNING ? QA_GOAL_WARNING : QA_GOAL_ERROR, text);
  free(text);
}
static void item_load_report(void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  item_load *load = ctx;
  if (load->base != NULL && load->base->report != NULL) load->base->report(load->base->report_ctx, diagnostic);
  load->source_reported++;
  report_script_diagnostic(load, diagnostic);
}
static bool item_load_resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  const qa_bot_script_reader *resolver = ctx;
  return resolver->resolve(resolver->ctx, request, out);
}
/* TS reportDiagnostics(preprocessor.diagnostics): the Set is the reported prefix here. */
static void flush_source_diagnostics(item_load *load, qa_ui_script_source_reader *source) {
  size_t count = 0;
  const qa_ui_script_diagnostic *items = qa_ui_script_source_reader_diagnostics(source, &count);
  for (; load->source_reported < count; load->source_reported++)
    report_script_diagnostic(load, &items[load->source_reported]);
}
typedef struct item_failure {
  bool failed;
  bool classname;
  qa_ui_script_diagnostic diagnostic; /* owned copy of the error thrown */
} item_failure;
static void failure_set(item_failure *failure, const qa_ui_script_location *at, const char *message) {
  failure->failed = true;
  char * message2 = lib_strdup(message);
  qa_ui_script_location location = lib_location(at->path, at->line, at->column);
  failure->diagnostic = (qa_ui_script_diagnostic){QA_UI_SCRIPT_SEVERITY_ERROR, message2, location};
}
static void failure_from_reader(item_failure *failure, const qa_structure_reader *reader) {
  size_t count = 0;
  const qa_ui_script_diagnostic *items = qa_structure_reader_diagnostics(reader, &count);
  failure_set(failure, &items[count - 1].location, items[count - 1].message);
}
static bool overlay_vector(qa_structure_reader *reader, uint8_t *target, item_failure *failure) {
  float values[3];
  size_t count = 0;
  if (!qa_structure_reader_read_float_array(reader, 3, values, &count)) {
    failure_from_reader(failure, reader);
    return false;
  }
  for (size_t i = 0; i < count; i++) lib_set_f32(target + i * 4, values[i]);
  return true;
}
static bool parse_item_config(item_tokens *tokens, qa_structure_reader *reader, int64_t maximum,
                              qa_bot_memory_allocation *allocation, item_failure *failure) {
  while (true) {
    qa_ui_script_token *token;
    if (item_tokens_next(tokens, &token) != QA_STRUCTURE_SOURCE_TOKEN) return true;
    if (!lib_token_is(token, "iteminfo")) {
      char *message = lib_format("unknown definition %s\n", token->text);
      failure_set(failure, &token->location, message);
      free(message);
      qa_ui_script_token_release(token);
      return false;
    }
    int32_t count = lib_i32(allocation->bytes);
    if (count >= maximum) {
      char *message = lib_format("more than %lld item info defined\n", (long long)maximum);
      failure_set(failure, &token->location, message);
      free(message);
      qa_ui_script_token_release(token);
      return false;
    }
    qa_ui_script_token_release(token);
    uint8_t *item = allocation->bytes + ITEM_CONFIG_BYTES + (size_t)count * ITEM_INFO_BYTES;
    memset(item, 0, ITEM_INFO_BYTES);
    char *text = NULL;
    if (!qa_structure_reader_read_string(reader, 31, &text)) {
      failure_from_reader(failure, reader);
      failure->classname = true;
      return false;
    }
    cell_set_text(item, 0, 32, text);
    free(text);
    if (!qa_structure_reader_begin(reader)) {
      failure_from_reader(failure, reader);
      return false;
    }
    while (true) {
      qa_structure_field field;
      bool found = false;
      if (!qa_structure_reader_next_field(reader, &field, &found)) {
        failure_from_reader(failure, reader);
        return false;
      }
      if (!found) break;
      bool ok = true;
      int32_t integer = 0;
      float real = 0;
      if (strcmp(field.name, "name") == 0 || strcmp(field.name, "model") == 0) {
        size_t offset = field.name[0] == 'n' ? 32 : 112;
        ok = qa_structure_reader_read_string(reader, QA_STRUCTURE_DEFAULT_STRING_BYTES, &text);
        if (ok) {
          cell_set_text(item, offset, 80, text);
          free(text);
        }
      } else if (strcmp(field.name, "modelindex") == 0 || strcmp(field.name, "type") == 0 ||
                 strcmp(field.name, "index") == 0) {
        size_t offset = field.name[0] == 'm' ? 192 : field.name[0] == 't' ? 196 : 200;
        ok = qa_structure_reader_read_int(reader, &integer);
        if (ok) lib_set_i32(item + offset, integer);
      } else if (strcmp(field.name, "respawntime") == 0) {
        ok = qa_structure_reader_read_float(reader, &real);
        if (ok) lib_set_f32(item + 204, real);
      } else if (strcmp(field.name, "mins") == 0 || strcmp(field.name, "maxs") == 0) {
        if (!overlay_vector(reader, item + (field.name[1] == 'i' ? 208 : 220), failure)) {
          qa_structure_field_release(&field);
          return false;
        }
      } else {
        char *message = lib_format("unknown structure field %s", field.name);
        failure_set(failure, field.location, message);
        free(message);
        qa_structure_field_release(&field);
        return false;
      }
      qa_structure_field_release(&field);
      if (!ok) {
        failure_from_reader(failure, reader);
        return false;
      }
    }
    lib_set_i32(item + 232, count);
    lib_set_i32(allocation->bytes, count + 1);
  }
}
static qa_item_config_bots_a *throw_load_error(char **error, const char *path, int64_t line, int64_t column,
                                               const char *message) {
  char *text = lib_format("%s:%lld:%lld: %s", path, (long long)line, (long long)column, message);
  if (error == NULL) {
    fprintf(stderr, "ScriptLanguageError: %s\n", text);
    abort();
  }
  *error = text;
  return NULL;
}
qa_item_config_bots_a *qa_load_item_config(const qa_bot_script_reader *resolver, const char *path,
                                           const qa_item_config_options *options_argument, char **error) {
  qa_item_config_options options_value;
  memset(&options_value, 0, sizeof options_value);
  const qa_item_config_options *options = options_argument != NULL ? options_argument : &options_value;
  double maximum_value = options->has_max_items ? options->max_items : 256;
  if (trunc(maximum_value) != maximum_value || maximum_value < 0 || maximum_value > 0x7fffffff)
    qa_range_error("maximum item definitions must be a nonnegative signed 32-bit integer");
  int64_t maximum = (int64_t)maximum_value;
  qa_ui_script_source root;
  if (!resolver->resolve_root(resolver->ctx, path, &root)) {
    char *text = lib_format("counldn't load %s\n", path);
    emit_report(options->report, options->report_ctx, QA_GOAL_ERROR, text);
    free(text);
    char *message = lib_format("couldn't load %s", path);
    qa_item_config_bots_a *result = throw_load_error(error, path, 1, 1, message);
    free(message);
    return result;
  }
  char *canonical = lib_strdup(root.path);
  item_load load = {options, options->preprocessor, resolver, 0};
  qa_ui_script_preprocessor_options preprocessor_options;
  memset(&preprocessor_options, 0, sizeof preprocessor_options);
  if (options->preprocessor != NULL) preprocessor_options = *options->preprocessor;
  preprocessor_options.globals = resolver->globals;
  if (resolver->debug_eval != NULL) {
    preprocessor_options.debug_eval = resolver->debug_eval;
    preprocessor_options.debug_eval_ctx = resolver->debug_eval_ctx;
  }
  preprocessor_options.report = item_load_report;
  preprocessor_options.report_ctx = &load;
  qa_ui_script_source_reader *source =
      qa_ui_script_source_reader_open(&root, item_load_resolve, (void *)(uintptr_t)resolver, &preprocessor_options);
  if (source == NULL) qa_range_error("out of memory");
  flush_source_diagnostics(&load, source);
  qa_item_config_bots_a *config = lib_alloc(sizeof *config);
  if (options->memory != NULL) {
    config->memory = options->memory;
  } else {
    config->owned_memory = lib_alloc(sizeof *config->owned_memory);
    qa_bot_memory_init(config->owned_memory);
    config->memory = config->owned_memory;
  }
  config->path = canonical;
  config->allocation = lib_mem_alloc(config->memory, ITEM_CONFIG_BYTES + maximum * ITEM_INFO_BYTES, QA_BOT_MEMORY_HUNK, true);
  /* Pointer slots contain allocation-relative offsets in the managed 32-bit layout. */
  lib_set_u32(config->allocation.bytes + 4, ITEM_CONFIG_BYTES);
  item_tokens tokens = {source, NULL};
  size_t prior_count = 0;
  const qa_ui_script_diagnostic *prior = qa_ui_script_source_reader_diagnostics(source, &prior_count);
  qa_structure_reader reader;
  qa_structure_reader_init(&reader, (qa_structure_token_source){&tokens, item_tokens_next, item_tokens_unread},
                           canonical, prior, prior_count, NULL, NULL);
  item_failure failure;
  memset(&failure, 0, sizeof failure);
  bool parsed = parse_item_config(&tokens, &reader, maximum, &config->allocation, &failure);
  if (tokens.unread != NULL) qa_ui_script_token_release(tokens.unread);
  qa_structure_reader_destroy(&reader);
  if (!parsed) {
    flush_source_diagnostics(&load, source);
    report_script_diagnostic(&load, &failure.diagnostic);
    qa_bot_memory_free(config->memory, &config->allocation);
    /* Failed PC_ExpectTokenType uses FreeMemory(source), leaving its scripts. */
    if (failure.classname) qa_ui_script_source_reader_dispose_record_only(source);
    else qa_ui_script_source_reader_dispose(source);
    qa_item_config_bots_a *result =
        throw_load_error(error, failure.diagnostic.location.path, failure.diagnostic.location.line,
                         failure.diagnostic.location.column, failure.diagnostic.message);
    qa_ui_script_diagnostic_destroy(&failure.diagnostic);
    if (!failure.classname) qa_ui_script_source_reader_destroy(source);
    qa_item_config_destroy(config);
    return result;
  }
  size_t count = 0;
  const qa_ui_script_diagnostic *items = qa_ui_script_source_reader_diagnostics(source, &count);
  for (size_t i = 0; i < count; i++) lib_diagnostics_copy(&config->diagnostics, &items[i]);
  if (qa_item_config_count(config) == 0)
    lib_diagnostics_push(&config->diagnostics, QA_UI_SCRIPT_SEVERITY_WARNING, "no item info loaded", canonical, 1, 1);
  flush_source_diagnostics(&load, source);
  qa_ui_script_source_reader_dispose(source);
  qa_ui_script_source_reader_destroy(source);
  if (qa_item_config_count(config) == 0)
    emit_report(options->report, options->report_ctx, QA_GOAL_WARNING, "no item info loaded\n");
  char *loaded = lib_format("loaded %s\n", path);
  emit_report(options->report, options->report_ctx, QA_GOAL_MESSAGE, loaded);
  free(loaded);
  return config;
}
/* ---- library helpers ---- */
void qa_bot_goal_library_init(qa_bot_goal_library *l) {
  memset(l, 0, sizeof *l);
  qa_bot_memory_init(&l->owned_memory);
  l->memory = &l->owned_memory;
  l->next_id = 1;
  l->next_serial = 1;
  l->next_source_goal = 0x7fffffff;
  l->next_info_pointer = 1;
}
void qa_bot_goal_library_configure(qa_bot_goal_library *l, const qa_bot_goal_options *options) {
  l->options = *options;
  if (options->memory != NULL) l->memory = options->memory;
  if (options->resolver != NULL) {
    l->resolver = *options->resolver;
    l->has_resolver = true;
  }
  memset(&l->preprocessor, 0, sizeof l->preprocessor);
  if (options->preprocessor != NULL) l->preprocessor = *options->preprocessor;
}
static void clear_source_goals(qa_bot_goal_library *l) {
  for (size_t i = 0; i < l->source_goal_count; i++) free(l->source_goals[i].name);
  l->source_goal_count = 0;
}
void qa_bot_goal_library_destroy(qa_bot_goal_library *l) {
  clear_source_goals(l);
  for (size_t i = 0; i < l->reported_count; i++) free(l->reported[i].message);
  for (size_t i = 0; i < l->config_count; i++) qa_item_config_destroy(l->configs[i]);
  free(l->reported);
  free(l->configs);
  free(l->source_goals);
  free(l->info_entities);
  free(l->config_references);
  free(l->index_references);
  l->reported = NULL;
  l->configs = NULL;
  l->source_goals = NULL;
  l->info_entities = NULL;
  l->config_references = NULL;
  l->index_references = NULL;
  l->reported_count = l->reported_capacity = l->config_count = l->config_capacity = 0;
  l->source_goal_capacity = l->info_entity_capacity = 0;
  l->config_reference_count = l->config_reference_capacity = 0;
  l->index_reference_count = l->index_reference_capacity = 0;
  l->config = NULL;
  qa_bot_memory_destroy(&l->owned_memory);
}
const qa_goal_diagnostic *qa_bot_goal_diagnostics(const qa_bot_goal_library *l, size_t *out_count) {
  *out_count = l->reported_count;
  return l->reported;
}
qa_item_config_bots_a *qa_bot_goal_item_config(const qa_bot_goal_library *l) { return l->config; }
int64_t qa_bot_goal_game_type(const qa_bot_goal_library *l) { return l->configured_game_type; }
static void report_text(qa_bot_goal_library *l, qa_goal_severity severity, const char *message) {
  if (l->reported_count == l->reported_capacity) {
    l->reported_capacity = lib_grow(l->reported_capacity, l->reported_count + 1);
    l->reported = lib_realloc(l->reported, l->reported_capacity * sizeof *l->reported);
  }
  l->reported[l->reported_count++] = (qa_goal_diagnostic){severity, lib_strdup(message)};
  qa_goal_diagnostic copy = l->reported[l->reported_count - 1];
  if (l->options.report != NULL) l->options.report(l->options.report_ctx, &copy);
}
__attribute__((format(printf, 3, 4))) static void report(qa_bot_goal_library *l, qa_goal_severity severity,
                                                         const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *text;
  if (vasprintf(&text, format, args) < 0) qa_range_error("out of memory");
  va_end(args);
  report_text(l, severity, text);
  free(text);
}
__attribute__((format(printf, 2, 3))) static void log_write(qa_bot_goal_library *l, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *text;
  if (vasprintf(&text, format, args) < 0) qa_range_error("out of memory");
  va_end(args);
  if (l->options.log_write != NULL) l->options.log_write(l->options.log_ctx, text);
  free(text);
}
static float finite_float(double value, const char *name) {
  float result = (float)value;
  if (!isfinite(result)) qa_range_error("%s must be finite float32", name);
  return result;
}
static float now(qa_bot_goal_library *l) {
  return finite_float(l->options.clock(l->options.clock_ctx), "AAS time");
}
/* Log_Write promotes the stored float to double; %f rounds decimal ties to even. */
static char *goal_float_text(double value, int digits) {
  float single = (float)value;
  if (isnan(single)) return lib_strdup("nan");
  if (isinf(single)) return lib_strdup(single < 0 ? "-inf" : "inf");
  return lib_format("%.*f", digits, (double)single);
}
static bool same_vector(qa_vec3 a, qa_vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static bool same_actor(qa_actor_id a, qa_actor_id b) {
  return a.session.token == b.session.token && a.slot == b.slot && a.generation == b.generation && a.token == b.token;
}
static int upper(unsigned char c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
static bool goal_name_key_equal(const char *a, const char *b) {
  for (size_t i = 0; i < 99999; i++) {
    int x = upper((unsigned char)a[i]), y = upper((unsigned char)b[i]);
    if (x != y) return false;
    if (x == 0) return true;
  }
  return true;
}
static bool goal_name_matches(qa_goal_name_query query, const char *candidate) {
  if (query.text == NULL) return query.matches(query.ctx, candidate);
  return goal_name_key_equal(query.text, candidate);
}
static qa_bounds item_bounds(const uint8_t *cell) { return (qa_bounds){lib_vec3(cell + 208), lib_vec3(cell + 220)}; }
static double default_avoid_time(const uint8_t *cell) {
  double respawn = I_RESPAWN(cell) == 0 ? 30 : I_RESPAWN(cell);
  return respawn > 10 ? respawn : isnan(respawn) ? respawn : 10;
}
static qa_bot_goal blank_goal(void) {
  qa_bot_goal goal;
  memset(&goal, 0, sizeof goal);
  return goal;
}
/* ---- state cells ---- */
static uint8_t *state_bytes(qa_goal_state_cell *state) { return state->allocation.bytes; }
static uint8_t *goal_record(qa_goal_state_cell *state, int64_t index) {
  if (index < 0 || index >= QA_MAX_GOAL_STACK) qa_range_error("goal stack index exceeds its allocation");
  return state_bytes(state) + GOAL_STACK_OFFSET + index * QA_GOAL_BYTES;
}
static int32_t stack_top(qa_goal_state_cell *state) { return lib_i32(state_bytes(state) + GOAL_STACK_TOP_OFFSET); }
static void set_stack_top(qa_goal_state_cell *state, int32_t value) { lib_set_i32(state_bytes(state) + GOAL_STACK_TOP_OFFSET, value); }
static qa_bot_goal read_goal(qa_goal_state_cell *state, int64_t index) {
  const uint8_t *r = goal_record(state, index);
  qa_bot_goal goal = {lib_vec3(r), lib_i32(r + 12), lib_vec3(r + 16), lib_vec3(r + 28),
                      lib_i32(r + 40), lib_i32(r + 44), lib_i32(r + 48), lib_i32(r + 52)};
  return goal;
}
static void write_goal(qa_goal_state_cell *state, int64_t index, const qa_bot_goal *goal) {
  uint8_t *r = goal_record(state, index);
  lib_set_vec3(r, goal->origin);
  lib_set_i32(r + 12, (int32_t)goal->area);
  lib_set_vec3(r + 16, goal->mins);
  lib_set_vec3(r + 28, goal->maxs);
  lib_set_i32(r + 40, (int32_t)goal->entity);
  lib_set_i32(r + 44, (int32_t)goal->number);
  lib_set_i32(r + 48, (int32_t)goal->flags);
  lib_set_i32(r + 52, (int32_t)goal->item_info);
}
#define AVOID_NUMBER(b, i) ((b) + AVOID_GOALS_OFFSET + (i) * 4)
#define AVOID_TIME(b, i) ((b) + AVOID_TIMES_OFFSET + (i) * 4)
static int64_t next_reference(qa_bot_goal_library *l) {
  /* Managed objects use owner-local IDs, not host addresses, in pointer words. */
  if (l->next_id > 0xffffffffLL) qa_range_error("Goal pointer IDs exhausted");
  return l->next_id++;
}
static qa_weight_config_bots_a *state_weight_config(qa_bot_goal_library *l, qa_goal_state_cell *state) {
  uint32_t id = lib_u32(state_bytes(state));
  if (id == 0) return NULL;
  for (size_t i = 0; i < l->config_reference_count; i++)
    if (l->config_references[i].id == id) return l->config_references[i].config;
  qa_ui_script_plain_error("Invalid goal weight configuration pointer");
}
static void state_set_weight_config(qa_bot_goal_library *l, qa_goal_state_cell *state, qa_weight_config_bots_a *config) {
  if (config == NULL) {
    lib_set_u32(state_bytes(state), 0);
    return;
  }
  int64_t id = 0;
  for (size_t i = 0; i < l->config_reference_count && id == 0; i++)
    if (l->config_references[i].config == config) id = l->config_references[i].id;
  if (id == 0) {
    id = next_reference(l);
    if (l->config_reference_count == l->config_reference_capacity) {
      l->config_reference_capacity = lib_grow(l->config_reference_capacity, l->config_reference_count + 1);
      l->config_references = lib_realloc(l->config_references, l->config_reference_capacity * sizeof *l->config_references);
    }
    l->config_references[l->config_reference_count++] = (qa_goal_config_reference){id, config};
  }
  lib_set_u32(state_bytes(state), (uint32_t)id);
}
static qa_bot_memory_allocation *state_indexes(qa_bot_goal_library *l, qa_goal_state_cell *state) {
  uint32_t id = lib_u32(state_bytes(state) + 4);
  if (id == 0) return NULL;
  for (size_t i = 0; i < l->index_reference_count; i++)
    if (l->index_references[i].id == id) return &l->index_references[i].allocation;
  qa_ui_script_plain_error("Invalid goal item weight index pointer");
}
static void state_clear_indexes(qa_bot_goal_library *l, qa_goal_state_cell *state) {
  uint32_t id = lib_u32(state_bytes(state) + 4);
  for (size_t i = 0; i < l->index_reference_count; i++) {
    if (l->index_references[i].id != id) continue;
    memmove(&l->index_references[i], &l->index_references[i + 1],
            (l->index_reference_count - i - 1) * sizeof *l->index_references);
    l->index_reference_count--;
    break;
  }
  lib_set_u32(state_bytes(state) + 4, 0);
}
static void state_set_indexes(qa_bot_goal_library *l, qa_goal_state_cell *state, qa_bot_memory_allocation allocation) {
  int64_t id = next_reference(l);
  if (l->index_reference_count == l->index_reference_capacity) {
    l->index_reference_capacity = lib_grow(l->index_reference_capacity, l->index_reference_count + 1);
    l->index_references = lib_realloc(l->index_references, l->index_reference_capacity * sizeof *l->index_references);
  }
  l->index_references[l->index_reference_count++] = (qa_goal_index_reference){id, allocation};
  lib_set_u32(state_bytes(state) + 4, (uint32_t)id);
}
static qa_goal_state_cell *state_of(qa_bot_goal_library *l, int64_t handle) {
  if (handle <= 0 || handle > QA_MAX_GOAL_STATES) {
    report(l, QA_GOAL_FATAL, "goal state handle %lld out of range\n", (long long)handle);
    return NULL;
  }
  qa_goal_state_cell *state = &l->states[handle];
  if (!state->used) {
    report(l, QA_GOAL_FATAL, "invalid goal state %lld\n", (long long)handle);
    return NULL;
  }
  return state;
}
static void release_weights(qa_bot_goal_library *l, qa_goal_state_cell *state) {
  qa_weight_config_bots_a *config = state_weight_config(l, state);
  if (config != NULL) qa_weight_config_store_free(l->options.weight_store, config);
  qa_bot_memory_allocation *indexes = state_indexes(l, state);
  if (indexes != NULL) qa_bot_memory_free(l->memory, indexes);
  state_set_weight_config(l, state, NULL);
  state_clear_indexes(l, state);
  if (config == NULL) return;
  for (int64_t handle = 1; handle <= QA_MAX_GOAL_STATES; handle++)
    if (l->states[handle].used && state_weight_config(l, &l->states[handle]) == config) return;
  for (size_t i = 0; i < l->config_reference_count; i++) {
    if (l->config_references[i].config != config) continue;
    memmove(&l->config_references[i], &l->config_references[i + 1],
            (l->config_reference_count - i - 1) * sizeof *l->config_references);
    l->config_reference_count--;
    return;
  }
}
/* ---- setup ---- */
typedef struct setup_frame {
  jmp_buf buf;
  qa_bot_goal_library *library;
  int64_t generation, revision;
  qa_bot_script_reader wrapped;
} setup_frame;
/* TS checkCurrent: RetiredGoalSetup unwinds to the setup catch. */
static void check_current(setup_frame *frame) {
  qa_bot_goal_library *l = frame->library;
  if (frame->generation != l->generation || frame->revision != l->setup_revision) longjmp(frame->buf, 1);
}
static bool setup_resolve_root(void *ctx, const char *filename, qa_ui_script_source *out) {
  setup_frame *frame = ctx;
  qa_bot_goal_library *l = frame->library;
  bool result = l->resolver.resolve_root(l->resolver.ctx, filename, out);
  check_current(frame);
  return result;
}
static bool setup_resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  setup_frame *frame = ctx;
  qa_bot_goal_library *l = frame->library;
  bool result = l->resolver.resolve(l->resolver.ctx, request, out);
  check_current(frame);
  return result;
}
static void setup_report(void *ctx, const qa_goal_diagnostic *diagnostic) {
  setup_frame *frame = ctx;
  check_current(frame);
  report_text(frame->library, diagnostic->severity, diagnostic->message);
  check_current(frame);
}
static void track_config(qa_bot_goal_library *l, qa_item_config_bots_a *config) {
  if (l->config_count == l->config_capacity) {
    l->config_capacity = lib_grow(l->config_capacity, l->config_count + 1);
    l->configs = lib_realloc(l->configs, l->config_capacity * sizeof *l->configs);
  }
  l->configs[l->config_count++] = config;
}
static int64_t setup_body(setup_frame *frame, const char *path) {
  qa_bot_goal_library *l = frame->library;
  double game_type = trunc((double)finite_float(l->options.game_type(l->options.game_type_ctx), "game type"));
  check_current(frame);
  l->configured_game_type = (int64_t)game_type;
  double maximum = 256;
  if (l->options.max_item_info_get != NULL)
    maximum = l->options.max_item_info_get(l->options.max_item_info_ctx);
  maximum = trunc((double)finite_float(maximum, "max_iteminfo"));
  check_current(frame);
  if (maximum < 0) {
    char *number = qa_input_js_number(maximum);
    report(l, QA_GOAL_ERROR, "max_iteminfo = %s\n", number != NULL ? number : "");
    free(number);
    check_current(frame);
    maximum = 256;
    if (l->options.max_item_info_set != NULL)
      l->options.max_item_info_set(l->options.max_item_info_ctx, maximum);
    check_current(frame);
  }
  if (!l->has_resolver) qa_range_error("BotGoalLibrary has no script resolver");
  frame->wrapped = (qa_bot_script_reader){frame, l->resolver.globals, l->resolver.debug_eval, l->resolver.debug_eval_ctx,
                                          setup_resolve_root, setup_resolve};
  qa_item_config_options options;
  memset(&options, 0, sizeof options);
  options.memory = l->memory;
  options.has_max_items = true;
  options.max_items = maximum;
  if (l->options.preprocessor != NULL) options.preprocessor = &l->preprocessor;
  options.report = setup_report;
  options.report_ctx = frame;
  char *error = NULL;
  qa_item_config_bots_a *config = qa_load_item_config(&frame->wrapped, path, &options, &error);
  if (config == NULL) {
    free(error);
    if (frame->generation == l->generation && frame->revision == l->setup_revision) {
      l->config = NULL;
      report_text(l, QA_GOAL_FATAL, "couldn't load item config\n");
    }
    return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_CONFIG;
  }
  track_config(l, config);
  check_current(frame);
  l->config = config;
  return QA_GOAL_ERROR_NONE;
}
int64_t qa_bot_goal_setup(qa_bot_goal_library *l, const char *path) {
  setup_frame frame;
  frame.library = l;
  frame.generation = l->generation;
  frame.revision = ++l->setup_revision;
  if (setjmp(frame.buf) != 0) return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_CONFIG;
  return setup_body(&frame, path != NULL ? path : "items.c");
}
int64_t qa_bot_goal_alloc_goal_state(qa_bot_goal_library *l, int64_t client) {
  for (int64_t handle = 1; handle <= QA_MAX_GOAL_STATES; handle++) {
    if (l->states[handle].used) continue;
    l->states[handle] = (qa_goal_state_cell){true, l->next_serial++,
                                             lib_mem_alloc(l->memory, GOAL_STATE_BYTES, QA_BOT_MEMORY_HEAP, true)};
    lib_set_i32(state_bytes(&l->states[handle]) + 8, (int32_t)client);
    return handle;
  }
  return 0;
}
void qa_bot_goal_free_goal_state(qa_bot_goal_library *l, int64_t handle) {
  if (handle > 0 && handle <= QA_MAX_GOAL_STATES && !l->states[handle].used) {
    report(l, QA_GOAL_FATAL, "invalid goal state handle %lld\n", (long long)handle);
    return;
  }
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return;
  release_weights(l, state);
  qa_bot_memory_free(l->memory, &state->allocation);
  state->used = false;
}
static int64_t load_item_weights(qa_bot_goal_library *l, int64_t handle, const char *fixed,
                                 const char *(*read)(void *ctx), void *ctx) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS;
  int64_t generation = l->generation;
  uint64_t serial = state->serial;
  char *filename = lib_strdup(fixed != NULL ? fixed : read(ctx));
  qa_weight_config_error error = {QA_WEIGHT_CONFIG_ERROR_NONE, NULL};
  qa_weight_config_bots_a *config = qa_weight_config_store_load(l->options.weight_store, filename, &error);
  free(filename);
  bool same = generation == l->generation && state->used && state->serial == serial;
  if (config == NULL) {
    qa_weight_config_error_destroy(&error);
    if (same) {
      state_set_weight_config(l, state, NULL);
      report_text(l, QA_GOAL_FATAL, "couldn't load weights\n");
    }
    return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS;
  }
  if (!same) {
    qa_weight_config_store_free(l->options.weight_store, config);
    return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS;
  }
  state_set_weight_config(l, state, config);
  qa_item_config_bots_a *item_config = l->config;
  if (item_config == NULL) return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS;
  size_t count = qa_item_config_count(item_config);
  qa_bot_memory_allocation indexes = lib_mem_alloc(l->memory, (int64_t)count * 4, QA_BOT_MEMORY_HEAP, true);
  for (size_t item_index = 0; item_index < count; item_index++) {
    char classname[32];
    cell_text(config_cell(item_config, item_index), 0, 31, classname);
    int64_t index = qa_weight_config_find(config, classname);
    lib_set_i32(indexes.bytes + item_index * 4, (int32_t)index);
    if (index < 0) log_write(l, "item info %zu \"%s\" has no fuzzy weight\r\n", item_index, classname);
  }
  if (generation != l->generation || !state->used || state->serial != serial)
    return QA_GOAL_ERROR_CANNOT_LOAD_ITEM_WEIGHTS;
  state_set_indexes(l, state, indexes);
  return QA_GOAL_ERROR_NONE;
}
int64_t qa_bot_goal_load_item_weights(qa_bot_goal_library *l, int64_t handle, const char *path) {
  return load_item_weights(l, handle, path, NULL, NULL);
}
int64_t qa_bot_goal_load_item_weights_fn(qa_bot_goal_library *l, int64_t handle, const char *(*read)(void *ctx), void *ctx) {
  return load_item_weights(l, handle, NULL, read, ctx);
}
void qa_bot_goal_free_item_weights(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state != NULL) release_weights(l, state);
}
static void interbreed_report(void *ctx, const char *message) {
  qa_bot_goal_library *l = ctx;
  report(l, QA_GOAL_ERROR, "%s\n", message);
}
qa_string_list qa_bot_goal_interbreed_goal_fuzzy_logic(qa_bot_goal_library *l, int64_t parent1, int64_t parent2, int64_t child) {
  qa_string_list none = {NULL, 0};
  qa_goal_state_cell *first = state_of(l, parent1), *second = state_of(l, parent2), *output = state_of(l, child);
  if (first == NULL || second == NULL || output == NULL) return none;
  qa_weight_config_bots_a *a = state_weight_config(l, first), *b = state_weight_config(l, second);
  qa_weight_config_bots_a *c = state_weight_config(l, output);
  if (a == NULL || b == NULL || c == NULL) {
    report_text(l, QA_GOAL_FATAL, "goal fuzzy interbreeding requires loaded item weights");
    return none;
  }
  return qa_weight_config_interbreed_from(c, a, b, interbreed_report, l);
}
void qa_bot_goal_mutate_goal_fuzzy_logic(qa_bot_goal_library *l, int64_t handle, double range) {
  (void)range; /* TS `_range`: be_ai_goal.c ignores it. */
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return;
  qa_weight_config_bots_a *config = state_weight_config(l, state);
  if (config == NULL) {
    report_text(l, QA_GOAL_FATAL, "goal fuzzy mutation requires loaded item weights");
    return;
  }
  qa_weight_config_evolve(config, l->options.random);
}
/* be_ai_goal.c:239-246 deliberately never calls WriteWeightConfig. */
void qa_bot_goal_save_goal_fuzzy_logic(qa_bot_goal_library *l, int64_t handle, const char *filename) {
  (void)filename;
  (void)state_of(l, handle);
}
/* The stack-overflow path of TS pushGoal; true when the slot is ready. */
static bool push_prepare(qa_bot_goal_library *l, int64_t handle, qa_goal_state_cell **out) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return false;
  if (stack_top(state) >= QA_MAX_GOAL_STACK - 1) {
    report_text(l, QA_GOAL_ERROR, "goal heap overflow\n");
    (void)qa_bot_goal_dump_goal_stack(l, handle, NULL, 0);
    return false;
  }
  set_stack_top(state, stack_top(state) + 1);
  *out = state;
  return true;
}
static void write_goal_bytes(qa_goal_state_cell *state, const uint8_t *bytes, size_t length) {
  if (length < QA_GOAL_BYTES) qa_range_error("goal source record requires 56 bytes");
  memcpy(goal_record(state, stack_top(state)), bytes, QA_GOAL_BYTES);
}
void qa_bot_goal_push_goal(qa_bot_goal_library *l, int64_t handle, const qa_bot_goal *goal) {
  qa_goal_state_cell *state;
  if (push_prepare(l, handle, &state)) write_goal(state, stack_top(state), goal);
}
void qa_bot_goal_push_goal_bytes(qa_bot_goal_library *l, int64_t handle, const uint8_t *bytes, size_t length) {
  qa_goal_state_cell *state;
  if (push_prepare(l, handle, &state)) write_goal_bytes(state, bytes, length);
}
void qa_bot_goal_push_goal_fn(qa_bot_goal_library *l, int64_t handle, qa_goal_push_fn produce, void *ctx) {
  qa_goal_state_cell *state;
  if (!push_prepare(l, handle, &state)) return;
  qa_bot_goal goal = blank_goal();
  size_t length = 0;
  const uint8_t *bytes = produce(ctx, &goal, &length);
  if (bytes != NULL) write_goal_bytes(state, bytes, length);
  else write_goal(state, stack_top(state), &goal);
}
void qa_bot_goal_pop_goal(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state != NULL && stack_top(state) > 0) set_stack_top(state, stack_top(state) - 1);
}
void qa_bot_goal_empty_goal_stack(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state != NULL) set_stack_top(state, 0);
}
bool qa_bot_goal_get_top_goal(qa_bot_goal_library *l, int64_t handle, qa_bot_goal *out) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL || stack_top(state) == 0) return false;
  *out = read_goal(state, stack_top(state));
  return true;
}
bool qa_bot_goal_get_second_goal(qa_bot_goal_library *l, int64_t handle, qa_bot_goal *out) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL || stack_top(state) <= 1) return false;
  *out = read_goal(state, stack_top(state) - 1);
  return true;
}
uint8_t *qa_bot_goal_get_top_goal_bytes(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  return state == NULL || stack_top(state) == 0 ? NULL : goal_record(state, stack_top(state));
}
uint8_t *qa_bot_goal_get_second_goal_bytes(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  return state == NULL || stack_top(state) <= 1 ? NULL : goal_record(state, stack_top(state) - 1);
}
size_t qa_bot_goal_dump_goal_stack(qa_bot_goal_library *l, int64_t handle, qa_bot_goal *out, size_t capacity) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return 0;
  size_t count = 0;
  for (int32_t index = 1; index <= stack_top(state); index++) {
    qa_bot_goal goal = read_goal(state, index);
    if (out != NULL && count < capacity) out[count] = goal;
    count++;
    log_write(l, "%d: %.31s", index, qa_bot_goal_goal_name(l, goal.number));
  }
  return count;
}
static void clear_avoid(uint8_t *bytes) {
  memset(bytes + AVOID_GOALS_OFFSET, 0, QA_MAX_AVOID_GOALS * 8);
}
void qa_bot_goal_reset_avoid_goals(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state != NULL) clear_avoid(state_bytes(state));
}
void qa_bot_goal_reset_goal_state(qa_bot_goal_library *l, int64_t handle) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return;
  memset(state_bytes(state) + GOAL_STACK_OFFSET, 0, GOAL_STACK_TOP_OFFSET + 4 - GOAL_STACK_OFFSET);
  clear_avoid(state_bytes(state));
}
static void add_avoid(qa_bot_goal_library *l, qa_goal_state_cell *state, int64_t number, double duration) {
  /* Source lines 751-774: expired slots use strict <, even when every slot is zero at time zero. */
  uint8_t *b = state_bytes(state);
  int slot = -1;
  for (int i = 0; i < QA_MAX_AVOID_GOALS && slot < 0; i++)
    if (lib_i32(AVOID_NUMBER(b, i)) == number) slot = i;
  for (int i = 0; i < QA_MAX_AVOID_GOALS && slot < 0; i++)
    if (lib_f32(AVOID_TIME(b, i)) < now(l)) slot = i;
  if (slot < 0) return;
  lib_set_i32(AVOID_NUMBER(b, slot), (int32_t)number);
  lib_set_f32(AVOID_TIME(b, slot), (float)((double)now(l) + duration));
}
static uint8_t *level_item(qa_bot_goal_library *l, int64_t pointer) {
  if (!l->has_level_item_heap || pointer <= 0 || (uint64_t)pointer * LEVEL_ITEM_BYTES > l->level_item_heap.length)
    qa_range_error("Invalid level item pointer");
  return l->level_item_heap.bytes + (pointer - 1) * LEVEL_ITEM_BYTES;
}
#define L_NUMBER(p) lib_i32((p) + 0)
#define L_INFO(p) lib_i32((p) + 4)
#define L_FLAGS(p) lib_i32((p) + 8)
#define L_WEIGHT(p) lib_f32((p) + 12)
#define L_ORIGIN(p) lib_vec3((p) + 16)
#define L_GOAL_AREA(p) lib_i32((p) + 28)
#define L_GOAL_ORIGIN(p) lib_vec3((p) + 32)
#define L_ENTITY(p) lib_i32((p) + 44)
#define L_TIMEOUT(p) lib_f32((p) + 48)
#define L_PREV(p) lib_u32((p) + 52)
#define L_NEXT(p) lib_u32((p) + 56)
static const uint8_t *item_info_cell(const qa_bot_goal_library *l, int32_t index) {
  if (l->config == NULL || index < 0 || (size_t)index >= qa_item_config_count(l->config))
    qa_range_error("level item info index is stale for the current item configuration");
  return config_cell(l->config, (size_t)index);
}
static uint32_t level_item_pointer(const qa_bot_goal_library *l, const uint8_t *item) {
  return (uint32_t)((size_t)(item - l->level_item_heap.bytes) / LEVEL_ITEM_BYTES) + 1;
}
static uint8_t *find_level_item_number(qa_bot_goal_library *l, int64_t number) {
  for (int64_t pointer = l->level_head; pointer != 0;) {
    uint8_t *item = level_item(l, pointer);
    if (L_NUMBER(item) == number) return item;
    pointer = L_NEXT(item);
  }
  return NULL;
}
void qa_bot_goal_set_avoid_goal_time(qa_bot_goal_library *l, int64_t handle, int64_t number, double duration) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return;
  float time = (float)duration;
  if (time < 0) {
    uint8_t *item = find_level_item_number(l, number);
    if (l->config != NULL && item != NULL)
      add_avoid(l, state, number, default_avoid_time(item_info_cell(l, L_INFO(item))));
    return;
  }
  add_avoid(l, state, number, time);
}
static int find_active_avoid(qa_bot_goal_library *l, qa_goal_state_cell *state, int64_t number) {
  const uint8_t *b = state_bytes(state);
  for (int i = 0; i < QA_MAX_AVOID_GOALS; i++)
    if (lib_i32(AVOID_NUMBER(b, i)) == number && lib_f32(AVOID_TIME(b, i)) >= now(l)) return i;
  return -1;
}
void qa_bot_goal_remove_from_avoid_goals(qa_bot_goal_library *l, int64_t handle, int64_t number) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return;
  int slot = find_active_avoid(l, state, number);
  if (slot >= 0) lib_set_f32(AVOID_TIME(state_bytes(state), slot), 0);
}
float qa_bot_goal_avoid_goal_time(qa_bot_goal_library *l, int64_t handle, int64_t number) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return 0;
  int slot = find_active_avoid(l, state, number);
  return slot < 0 ? 0 : (float)((double)lib_f32(AVOID_TIME(state_bytes(state), slot)) - (double)now(l));
}
size_t qa_bot_goal_dump_avoid_goals(qa_bot_goal_library *l, int64_t handle, qa_avoid_goal_dump *out, size_t capacity) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL) return 0;
  size_t count = 0;
  for (int i = 0; i < QA_MAX_AVOID_GOALS; i++) {
    const uint8_t *b = state_bytes(state);
    if (!(lib_f32(AVOID_TIME(b, i)) >= now(l))) continue;
    int32_t number = lib_i32(AVOID_NUMBER(b, i));
    char name[32];
    snprintf(name, sizeof name, "%s", qa_bot_goal_goal_name(l, number));
    float remaining = (float)((double)lib_f32(AVOID_TIME(b, i)) - (double)now(l));
    if (out != NULL && count < capacity) out[count] = (qa_avoid_goal_dump){number, remaining};
    count++;
    char *text = goal_float_text(remaining, 6);
    log_write(l, "avoid goal %s, number %d for %s seconds", name, number, text);
    free(text);
  }
  return count;
}
/* ---- map info ---- */
static qa_goal_info_entity *info_entity(qa_bot_goal_library *l, int64_t pointer) {
  if (pointer < 0 || (size_t)pointer >= l->info_entity_capacity || !l->info_entities[pointer].live)
    qa_range_error("Invalid map info entity pointer");
  return &l->info_entities[pointer];
}
static uint32_t info_next(const qa_goal_info_entity *entity) {
  return lib_u32(entity->allocation.bytes + (entity->is_camp ? 160 : 144));
}
static void info_set_next(qa_goal_info_entity *entity, uint32_t next) {
  lib_set_u32(entity->allocation.bytes + (entity->is_camp ? 160 : 144), next);
}
static void info_release(qa_bot_goal_library *l, int64_t pointer) {
  qa_goal_info_entity *entity = info_entity(l, pointer);
  qa_bot_memory_free(l->memory, &entity->allocation);
  entity->live = false;
}
void qa_bot_goal_free_info_entities(qa_bot_goal_library *l) {
  l->map_revision++;
  for (int64_t pointer = l->location_head; pointer != 0;) {
    int64_t next = info_next(info_entity(l, pointer));
    info_release(l, pointer);
    pointer = next;
  }
  l->location_head = 0;
  for (int64_t pointer = l->camp_head; pointer != 0;) {
    int64_t next = info_next(info_entity(l, pointer));
    info_release(l, pointer);
    pointer = next;
  }
  l->camp_head = 0;
}
void qa_bot_goal_shutdown(qa_bot_goal_library *l) {
  l->generation++;
  l->setup_revision++;
  if (l->config != NULL) qa_item_config_free(l->config);
  l->config = NULL;
  l->has_world = false;
  clear_source_goals(l);
  if (l->has_level_item_heap) qa_bot_memory_free(l->memory, &l->level_item_heap);
  l->has_level_item_heap = false;
  l->level_head = 0;
  l->free_level_head = 0;
  l->initial_item_count = 0;
  qa_bot_goal_free_info_entities(l);
  for (int64_t handle = 1; handle <= QA_MAX_GOAL_STATES; handle++)
    if (l->states[handle].used) qa_bot_goal_free_goal_state(l, handle);
}
static int64_t info_allocate(qa_bot_goal_library *l, bool is_camp) {
  if (l->next_info_pointer > 0xffffffffLL) qa_range_error("Goal info pointer IDs exhausted");
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(l->memory, is_camp ? CAMP_SPOT_BYTES : MAP_LOCATION_BYTES, QA_BOT_MEMORY_HEAP, true);
  int64_t pointer = l->next_info_pointer++;
  if ((size_t)pointer >= l->info_entity_capacity) {
    size_t next = lib_grow(l->info_entity_capacity, (size_t)pointer + 1);
    l->info_entities = lib_realloc(l->info_entities, next * sizeof *l->info_entities);
    memset(l->info_entities + l->info_entity_capacity, 0, (next - l->info_entity_capacity) * sizeof *l->info_entities);
    l->info_entity_capacity = next;
  }
  l->info_entities[pointer] = (qa_goal_info_entity){true, is_camp, allocation};
  return pointer;
}
static bool bsp_string(qa_aas_bsp_entities *bsp, int64_t entity, const char *key, char *out) {
  return qa_aas_bsp_entities_value(bsp, entity, key, (uint8_t *)out, 128);
}
void qa_bot_goal_init_info_entities(qa_bot_goal_library *l, const qa_goal_world *world_argument) {
  qa_goal_world world = *world_argument;
  qa_bot_goal_free_info_entities(l);
  int64_t revision = l->map_revision;
  qa_aas_bsp_entities *bsp = world.bsp_entities;
  int64_t location_count = 0, camp_count = 0;
  for (int64_t entity = qa_aas_bsp_entities_next_entity(bsp, 0); entity != 0;
       entity = qa_aas_bsp_entities_next_entity(bsp, entity)) {
    char classname[128];
    if (!bsp_string(bsp, entity, "classname", classname)) continue;
    bool location = strcmp(classname, "target_location") == 0;
    if (!location && strcmp(classname, "info_camp") != 0) continue;
    int64_t pointer = info_allocate(l, !location);
    uint8_t *bytes = l->info_entities[pointer].allocation.bytes;
    lib_set_vec3(bytes, qa_aas_bsp_entities_vector(bsp, entity, "origin").value);
    qa_aas_bsp_entities_value(bsp, entity, "message", bytes + 16, 128);
    if (location) {
      int64_t area = world.point_area(world.point_area_ctx, lib_vec3(bytes));
      if (revision != l->map_revision) return;
      lib_set_i32(bytes + 12, (int32_t)area);
      info_set_next(&l->info_entities[pointer], l->location_head);
      l->location_head = (uint32_t)pointer;
      location_count++;
    } else {
      lib_set_f32(bytes + 144, (float)qa_aas_bsp_entities_float(bsp, entity, "range").value);
      lib_set_f32(bytes + 148, (float)qa_aas_bsp_entities_float(bsp, entity, "weight").value);
      lib_set_f32(bytes + 152, (float)qa_aas_bsp_entities_float(bsp, entity, "wait").value);
      lib_set_f32(bytes + 156, (float)qa_aas_bsp_entities_float(bsp, entity, "random").value);
      int64_t area = world.point_area(world.point_area_ctx, lib_vec3(bytes));
      if (revision != l->map_revision) return;
      lib_set_i32(bytes + 12, (int32_t)area);
      if (area == 0) {
        qa_vec3 origin = lib_vec3(bytes);
        char *x = goal_float_text(origin.x, 1), *y = goal_float_text(origin.y, 1), *z = goal_float_text(origin.z, 1);
        report(l, QA_GOAL_MESSAGE, "camp spot at %s %s %s in solid\n", x, y, z);
        free(x);
        free(y);
        free(z);
        if (revision != l->map_revision) return;
        info_release(l, pointer);
        continue;
      }
      info_set_next(&l->info_entities[pointer], l->camp_head);
      l->camp_head = (uint32_t)pointer;
      camp_count++;
    }
  }
  bool developer = l->options.developer != NULL && l->options.developer(l->options.developer_ctx);
  if (revision != l->map_revision) return;
  if (developer) {
    report(l, QA_GOAL_MESSAGE, "%lld map locations\n", (long long)location_count);
    if (revision != l->map_revision) return;
    report(l, QA_GOAL_MESSAGE, "%lld camp spots\n", (long long)camp_count);
  }
}
static bool any_state(const qa_bot_goal_library *l) {
  for (int64_t handle = 1; handle <= QA_MAX_GOAL_STATES; handle++)
    if (l->states[handle].used) return true;
  return false;
}
void qa_bot_goal_rebind_world(qa_bot_goal_library *l, const qa_goal_world *world) {
  if (!l->has_world || l->world.bsp_entities != world->bsp_entities || any_state(l))
    qa_ui_script_plain_error("Goal world rebinding requires retained map metadata and retired bot states");
  l->map_revision++;
  l->world = *world;
  clear_source_goals(l);
}
/* ---- level item lists ---- */
static uint8_t *alloc_level_item(qa_bot_goal_library *l) {
  if (l->free_level_head == 0) {
    report_text(l, QA_GOAL_FATAL, "out of level items\n");
    return NULL;
  }
  uint8_t *item = level_item(l, l->free_level_head);
  l->free_level_head = L_NEXT(item);
  memset(item, 0, LEVEL_ITEM_BYTES);
  return item;
}
static void free_level_item(qa_bot_goal_library *l, uint8_t *item) {
  lib_set_u32(item + 56, l->free_level_head);
  l->free_level_head = level_item_pointer(l, item);
}
static void add_level_item(qa_bot_goal_library *l, uint8_t *item) {
  uint32_t pointer = level_item_pointer(l, item);
  if (l->level_head != 0) lib_set_u32(level_item(l, l->level_head) + 52, pointer);
  lib_set_u32(item + 52, 0);
  lib_set_u32(item + 56, l->level_head);
  l->level_head = pointer;
}
static void remove_level_item(qa_bot_goal_library *l, uint8_t *item) {
  if (L_PREV(item) != 0) lib_set_u32(level_item(l, L_PREV(item)) + 56, L_NEXT(item));
  else l->level_head = L_NEXT(item);
  if (L_NEXT(item) != 0) lib_set_u32(level_item(l, L_NEXT(item)) + 52, L_PREV(item));
}
static void validate_source_goal_range(const qa_bot_goal_library *l) {
  if (l->has_world && l->world.has_source_pickups &&
      (int64_t)l->initial_item_count + 1000000 >= QA_SOURCE_GOAL_NUMBER_MIN)
    qa_range_error("Native item goal numbers overlap the reserved source pickup range");
}
static const uint8_t *find_item_by_classname(const qa_item_config_bots_a *config, const char *classname, int32_t *out_index) {
  size_t count = qa_item_config_count(config);
  for (size_t i = 0; i < count; i++) {
    char candidate[32];
    cell_text(config_cell(config, i), 0, 31, candidate);
    if (strcmp(candidate, classname) == 0) {
      *out_index = (int32_t)i;
      return config_cell(config, i);
    }
  }
  return NULL;
}
void qa_bot_goal_init_level_items(qa_bot_goal_library *l, const qa_goal_world *world_argument) {
  if (world_argument == NULL && !l->has_world)
    qa_ui_script_plain_error("BotInitLevelItems requires a retained goal world");
  qa_goal_world world = world_argument != NULL ? *world_argument : l->world;
  l->world = world;
  l->has_world = true;
  clear_source_goals(l);
  int64_t revision = l->map_revision + 1;
  qa_bot_goal_init_info_entities(l, &world);
  if (revision != l->map_revision) return;
  if (l->has_level_item_heap) qa_bot_memory_free(l->memory, &l->level_item_heap);
  l->has_level_item_heap = false;
  double maximum_value = 256;
  if (l->options.max_level_items != NULL) maximum_value = l->options.max_level_items(l->options.max_level_items_ctx);
  maximum_value = trunc((double)finite_float(maximum_value, "max_levelitems"));
  if (revision != l->map_revision) return;
  if (maximum_value < 1 || maximum_value > 0x7fffffff)
    qa_range_error("maximum level items must be a positive signed 32-bit integer");
  int64_t maximum = (int64_t)maximum_value;
  l->level_item_heap = lib_mem_alloc(l->memory, maximum * LEVEL_ITEM_BYTES, QA_BOT_MEMORY_HEAP, true);
  l->has_level_item_heap = true;
  /* Link words use one-based cell IDs; zero is the source NULL pointer. */
  for (int64_t pointer = 1; pointer < maximum; pointer++)
    lib_set_u32(level_item(l, pointer) + 56, (uint32_t)(pointer + 1));
  l->free_level_head = 1;
  l->level_head = 0;
  l->initial_item_count = 0;
  const qa_item_config_bots_a *config = l->config;
  qa_aas_bsp_entities *bsp = world.bsp_entities;
  if (config == NULL || !world.has_navigation) return;
  size_t count = qa_item_config_count(config);
  for (size_t i = 0; i < count; i++) {
    if (I_MODEL_INDEX(config_cell(config, i)) != 0) continue;
    char classname[32];
    cell_text(config_cell(config, i), 0, 31, classname);
    log_write(l, "item %s has modelindex 0", classname);
    if (revision != l->map_revision) return;
  }
  const qa_goal_navigation *navigation = &world.navigation;
  for (int64_t entity = qa_aas_bsp_entities_next_entity(bsp, 0); entity != 0;
       entity = qa_aas_bsp_entities_next_entity(bsp, entity)) {
    char classname[128];
    if (!bsp_string(bsp, entity, "classname", classname)) continue;
    int32_t spawn_flags = (int32_t)qa_aas_bsp_entities_int(bsp, entity, "spawnflags").value;
    int32_t info_index = -1;
    const uint8_t *info = find_item_by_classname(config, classname, &info_index);
    if (info == NULL) {
      log_write(l, "entity %s unknown item\r\n", classname);
      if (revision != l->map_revision) return;
      continue;
    }
    qa_aas_bsp_vector_result entity_origin = qa_aas_bsp_entities_vector(bsp, entity, "origin");
    if (!entity_origin.found) {
      report(l, QA_GOAL_ERROR, "item %s without origin\n", classname);
      if (revision != l->map_revision) return;
      continue;
    }
    qa_vec3 origin = entity_origin.value;
    qa_bounds bounds = item_bounds(info);
    int64_t goal_area = 0;
    if ((spawn_flags & 1) != 0) {
      int64_t contents = world.host.point_contents(world.host.ctx, origin);
      if (revision != l->map_revision) return;
      if ((contents & 32) == 0) {
        qa_vec3 end = qa_add3(origin, (qa_vec3){0, 0, -32});
        double fraction = world.host.trace(world.host.ctx, origin, end, &bounds, -1, 1 | 0x10000);
        if (revision != l->map_revision) return;
        if (fraction >= 1) {
          goal_area = navigation->best_reachable_from_jump_pad_area(navigation->ctx, origin, bounds);
          if (revision != l->map_revision) return;
          char info_class[32];
          cell_text(info, 0, 31, info_class);
          log_write(l, "item %s reachable from jumppad area %lld\r\n", info_class, (long long)goal_area);
          if (revision != l->map_revision) return;
          if (goal_area == 0) continue;
        }
      }
    }
    uint8_t *item = alloc_level_item(l);
    if (item == NULL) return;
    lib_set_i32(item + 0, ++l->initial_item_count);
    validate_source_goal_range(l);
    lib_set_f32(item + 48, 0);
    lib_set_i32(item + 44, 0);
    int32_t flags = 0;
    if (qa_aas_bsp_entities_int(bsp, entity, "notfree").value != 0) flags |= 1;
    if (qa_aas_bsp_entities_int(bsp, entity, "notteam").value != 0) flags |= 2;
    if (qa_aas_bsp_entities_int(bsp, entity, "notsingle").value != 0) flags |= 4;
    if (qa_aas_bsp_entities_int(bsp, entity, "notbot").value != 0) flags |= 8;
    if (strcmp(classname, "item_botroam") == 0) {
      flags |= 16;
      lib_set_f32(item + 12, (float)qa_aas_bsp_entities_float(bsp, entity, "weight").value);
    }
    lib_set_i32(item + 8, flags);
    if ((spawn_flags & 1) == 0) {
      qa_goal_drop_result dropped = navigation->drop_to_floor(navigation->ctx, origin, bounds);
      if (revision != l->map_revision) return;
      origin = dropped.origin;
      if (!dropped.success) {
        char *x = goal_float_text(origin.x, 1), *y = goal_float_text(origin.y, 1), *z = goal_float_text(origin.z, 1);
        report(l, QA_GOAL_MESSAGE, "%s in solid at (%s %s %s)\n", classname, x, y, z);
        free(x);
        free(y);
        free(z);
        if (revision != l->map_revision) return;
      }
    }
    lib_set_i32(item + 4, info_index);
    lib_set_vec3(item + 16, origin);
    qa_goal_reachable_position position = {goal_area, origin};
    if (goal_area == 0) position = navigation->best_reachable_area(navigation->ctx, origin, bounds);
    if (revision != l->map_revision) return;
    lib_set_i32(item + 28, (int32_t)position.area);
    lib_set_vec3(item + 32, position.origin);
    if (position.area == 0) {
      char *x = goal_float_text(origin.x, 1), *y = goal_float_text(origin.y, 1), *z = goal_float_text(origin.z, 1);
      report(l, QA_GOAL_MESSAGE, "%s not reachable for bots at (%s %s %s)\n", classname, x, y, z);
      free(x);
      free(y);
      free(z);
      if (revision != l->map_revision) return;
    }
    add_level_item(l, item);
  }
  report(l, QA_GOAL_MESSAGE, "found %d level items\n", l->initial_item_count);
}
/* ---- names and goals ---- */
static qa_goal_source_binding *find_source_goal(qa_bot_goal_library *l, int64_t number) {
  for (size_t i = 0; i < l->source_goal_count; i++)
    if (l->source_goals[i].number == number) return &l->source_goals[i];
  return NULL;
}
const char *qa_bot_goal_goal_name(qa_bot_goal_library *l, int64_t number) {
  if (qa_is_source_goal_number((double)number)) {
    qa_goal_source_binding *binding = find_source_goal(l, number);
    return binding != NULL ? binding->name : "";
  }
  if (l->config == NULL) return "";
  uint8_t *item = find_level_item_number(l, number);
  if (item == NULL) return "";
  /* Stored names are written with a terminator inside their 80-byte field. */
  return (const char *)item_info_cell(l, L_INFO(item)) + 32;
}
void qa_bot_goal_write_goal_name(qa_bot_goal_library *l, int64_t number, void (*found)(void *ctx, const char *name),
                                 void (*missing)(void *ctx), void *ctx) {
  if (qa_is_source_goal_number((double)number)) {
    qa_goal_source_binding *source = find_source_goal(l, number);
    if (source == NULL) missing(ctx);
    else found(ctx, source->name);
    return;
  }
  if (l->config == NULL) return;
  uint8_t *item = find_level_item_number(l, number);
  if (item == NULL) missing(ctx);
  else found(ctx, (const char *)item_info_cell(l, L_INFO(item)) + 32);
}
static qa_bot_goal item_goal(const qa_bot_goal_library *l, const uint8_t *item) {
  const uint8_t *info = item_info_cell(l, L_INFO(item));
  qa_bot_goal goal = {L_GOAL_ORIGIN(item), L_GOAL_AREA(item), lib_vec3(info + 208), lib_vec3(info + 220), L_ENTITY(item),
                      L_NUMBER(item),
                      QA_GOAL_FLAG_ITEM | (L_TIMEOUT(item) != 0 ? QA_GOAL_FLAG_DROPPED : 0) |
                          ((L_FLAGS(item) & 16) != 0 ? QA_GOAL_FLAG_ROAM : 0),
                      I_NUMBER(info)};
  return goal;
}
static bool allowed(const qa_bot_goal_library *l, int32_t flags) {
  return (flags & (l->configured_game_type == 2 ? 4 : l->configured_game_type >= 3 ? 2 : 1)) == 0;
}
static bool next_source_item_goal(qa_bot_goal_library *l, bool has_after, int64_t after, qa_goal_name_query name,
                                  const qa_bot_goal *prior, qa_bot_goal *out) {
  bool next = !has_after;
  for (size_t i = 0; i < l->source_goal_count; i++) {
    qa_goal_source_binding *binding = &l->source_goals[i];
    if (!next) {
      if (binding->number == after) next = true;
      continue;
    }
    if (goal_name_matches(name, binding->name)) {
      *out = binding->goal;
      out->item_info = prior->item_info;
      return true;
    }
  }
  return false;
}
bool qa_bot_goal_get_level_item_goal(qa_bot_goal_library *l, int64_t index, qa_goal_name_query name,
                                     const qa_bot_goal *prior_argument, qa_bot_goal *out) {
  qa_bot_goal prior = prior_argument != NULL ? *prior_argument : blank_goal();
  if (l->config == NULL) return false;
  int64_t pointer = l->level_head;
  if (qa_is_source_goal_number((double)index)) return next_source_item_goal(l, true, index, name, &prior, out);
  if (index >= 0) {
    uint8_t *previous = find_level_item_number(l, index);
    if (previous == NULL) return false;
    pointer = L_NEXT(previous);
  }
  for (; pointer != 0;) {
    uint8_t *item = level_item(l, pointer);
    if (allowed(l, L_FLAGS(item)) && (L_FLAGS(item) & 8) == 0) {
      char candidate[80];
      cell_text(item_info_cell(l, L_INFO(item)), 32, 79, candidate);
      if (goal_name_matches(name, candidate)) {
        *out = item_goal(l, item);
        out->item_info = prior.item_info;
        out->flags = QA_GOAL_FLAG_ITEM | (L_TIMEOUT(item) != 0 ? QA_GOAL_FLAG_DROPPED : 0);
        return true;
      }
    }
    pointer = L_NEXT(item);
  }
  return next_source_item_goal(l, false, 0, name, &prior, out);
}
static qa_bot_goal info_goal(const qa_goal_info_entity *info, const qa_bot_goal *prior) {
  qa_bot_goal goal = *prior;
  goal.origin = lib_vec3(info->allocation.bytes);
  goal.area = lib_i32(info->allocation.bytes + 12);
  goal.entity = 0;
  goal.mins = (qa_vec3){-8, -8, -8};
  goal.maxs = (qa_vec3){8, 8, 8};
  return goal;
}
bool qa_bot_goal_get_map_location_goal(qa_bot_goal_library *l, qa_goal_name_query name, const qa_bot_goal *prior_argument,
                                       qa_bot_goal *out) {
  qa_bot_goal prior = prior_argument != NULL ? *prior_argument : blank_goal();
  for (int64_t pointer = l->location_head; pointer != 0;) {
    qa_goal_info_entity *location = info_entity(l, pointer);
    char text[129];
    cell_text(location->allocation.bytes, 16, 128, text);
    if (goal_name_matches(name, text)) {
      *out = info_goal(location, &prior);
      return true;
    }
    pointer = info_next(location);
  }
  return false;
}
bool qa_bot_goal_get_next_camp_spot_goal(qa_bot_goal_library *l, int64_t index, const qa_bot_goal *prior_argument,
                                         int64_t *out_next, qa_bot_goal *out) {
  qa_bot_goal prior = prior_argument != NULL ? *prior_argument : blank_goal();
  int64_t position = index > 0 ? index : 0, remaining = position;
  for (int64_t pointer = l->camp_head; pointer != 0;) {
    qa_goal_info_entity *camp = info_entity(l, pointer);
    if (--remaining < 0) {
      *out_next = position + 1;
      *out = info_goal(camp, &prior);
      return true;
    }
    pointer = info_next(camp);
  }
  return false;
}
/* TS entityNumbers' per-step validation. */
static int64_t next_entity_checked(const qa_goal_world_host *host, int64_t after, int64_t previous) {
  int64_t entity = host->next_entity(host->ctx, after);
  if (entity != 0 && (entity <= previous || entity > 1000000))
    qa_range_error("AAS entity enumeration must increase within the configured entity range");
  return entity;
}
void qa_bot_goal_find_entity_for_level_item(qa_bot_goal_library *l, int64_t number) {
  if (!l->has_world || l->config == NULL) return;
  uint8_t *item = find_level_item_number(l, number);
  if (item == NULL) return;
  qa_goal_world_host host = l->world.host;
  int64_t previous = 0;
  for (int64_t entity = next_entity_checked(&host, 0, previous); entity != 0;) {
    qa_goal_entity_info info = host.entity_info(host.ctx, entity);
    if (info.model_index != 0 && same_vector(info.origin, info.last_visible_origin) &&
        info.model_index == I_MODEL_INDEX(item_info_cell(l, L_INFO(item))) &&
        qa_length3(qa_sub3(L_ORIGIN(item), info.origin)) < 30)
      lib_set_i32(item + 44, (int32_t)entity);
    previous = entity;
    entity = next_entity_checked(&host, entity, previous);
  }
}
static void move_item(qa_bot_goal_library *l, uint8_t *item, qa_vec3 origin, const qa_goal_navigation *navigation) {
  if (same_vector(L_ORIGIN(item), origin)) return;
  lib_set_vec3(item + 16, origin);
  qa_goal_reachable_position position =
      navigation->best_reachable_area(navigation->ctx, L_ORIGIN(item), item_bounds(item_info_cell(l, L_INFO(item))));
  lib_set_vec3(item + 32, position.origin);
  lib_set_i32(item + 28, (int32_t)position.area);
}
void qa_bot_goal_update_entity_items(qa_bot_goal_library *l) {
  for (int64_t pointer = l->level_head; pointer != 0;) {
    uint8_t *item = level_item(l, pointer);
    pointer = L_NEXT(item);
    if (L_TIMEOUT(item) != 0 && L_TIMEOUT(item) < now(l)) {
      remove_level_item(l, item);
      free_level_item(l, item);
    }
  }
  if (!l->has_world || l->config == NULL || !l->world.has_navigation) return;
  qa_goal_world world = l->world;
  const qa_item_config_bots_a *config = l->config;
  const qa_goal_navigation *navigation = &world.navigation;
  int64_t previous = 0;
  for (int64_t entity = next_entity_checked(&world.host, 0, previous); entity != 0;
       previous = entity, entity = next_entity_checked(&world.host, entity, previous)) {
    qa_goal_entity_info info = world.host.entity_info(world.host.ctx, entity);
    if (info.type != 2 || info.model_index == 0 || !same_vector(info.origin, info.last_visible_origin)) continue;
    uint8_t *existing = NULL;
    for (int64_t pointer = l->level_head; pointer != 0 && existing == NULL;) {
      uint8_t *item = level_item(l, pointer);
      if (L_ENTITY(item) != 0 && L_ENTITY(item) == entity) existing = item;
      pointer = L_NEXT(item);
    }
    if (existing != NULL) {
      if (I_MODEL_INDEX(item_info_cell(l, L_INFO(existing))) == info.model_index) {
        move_item(l, existing, info.origin, navigation);
        continue;
      }
      remove_level_item(l, existing);
      free_level_item(l, existing);
    }
    uint8_t *unlinked = NULL;
    for (int64_t pointer = l->level_head; pointer != 0 && unlinked == NULL;) {
      uint8_t *item = level_item(l, pointer);
      if (L_ENTITY(item) == 0 && allowed(l, L_FLAGS(item)) &&
          I_MODEL_INDEX(item_info_cell(l, L_INFO(item))) == info.model_index &&
          qa_length3(qa_sub3(L_ORIGIN(item), info.origin)) < 30)
        unlinked = item;
      pointer = L_NEXT(item);
    }
    if (unlinked != NULL) {
      lib_set_i32(unlinked + 44, (int32_t)entity);
      move_item(l, unlinked, info.origin, navigation);
      if (l->options.debug) {
        char classname[32];
        cell_text(item_info_cell(l, L_INFO(unlinked)), 0, 31, classname);
        log_write(l, "linked item %s to an entity", classname);
      }
      continue;
    }
    int32_t info_index = -1;
    size_t count = qa_item_config_count(config);
    for (size_t i = 0; i < count && info_index < 0; i++)
      if (I_MODEL_INDEX(config_cell(config, i)) == info.model_index) info_index = (int32_t)i;
    if (info_index < 0) continue;
    uint8_t *item = alloc_level_item(l);
    if (item == NULL) continue;
    lib_set_i32(item + 44, (int32_t)entity);
    lib_set_i32(item + 0, (int32_t)(l->initial_item_count + entity));
    lib_set_i32(item + 4, info_index);
    lib_set_vec3(item + 16, info.origin);
    qa_goal_reachable_position goal = navigation->best_reachable_area(navigation->ctx, L_ORIGIN(item),
                                                                      item_bounds(config_cell(config, (size_t)info_index)));
    lib_set_i32(item + 28, (int32_t)goal.area);
    lib_set_vec3(item + 32, goal.origin);
    qa_goal_area_info settings = navigation->area(navigation->ctx, goal.area);
    if ((settings.contents & 128) != 0) {
      free_level_item(l, item);
      continue;
    }
    lib_set_f32(item + 48, (float)((double)now(l) + 30));
    add_level_item(l, item);
  }
}
/* ---- item choice ---- */
static double travel_time(const qa_goal_navigation *navigation, int64_t area, qa_vec3 origin, int64_t goal_area,
                          int64_t travel_flags) {
  qa_goal_route_request request = {area, origin, goal_area, travel_flags};
  qa_goal_route_result result = navigation->route(navigation->ctx, &request);
  return result.found ? result.travel_time : 0;
}
typedef struct candidate {
  bool native;
  int64_t pointer; /* native */
  void *heap_record;
  qa_bot_goal goal; /* source */
  double utility;
} candidate;
typedef struct chooser {
  qa_bot_goal_library *library;
  qa_goal_state_cell *state;
  int64_t handle;
  qa_goal_world world;
  qa_goal_navigation navigation;
  int64_t area;
  qa_vec3 origin;
  qa_weight_inventory inventory;
  int64_t travel_flags;
  bool nearby;
  const qa_bot_goal *long_term_goal;
  double max_time;
  double long_term_time;
  double best_weight;
  bool has_best;
  candidate best;
} chooser;
/* False is the TS `return false` from inside the candidate loop. */
static bool consider_candidate(chooser *c, const candidate *current) {
  qa_bot_goal_library *l = c->library;
  double weight;
  int64_t goal_area, number;
  qa_vec3 goal_origin;
  bool return_leg = true;
  if (current->native) {
    uint8_t *item = level_item(l, current->pointer);
    int32_t flags = L_FLAGS(item);
    if (!allowed(l, flags) || (flags & 8) != 0 || L_GOAL_AREA(item) == 0 ||
        (L_ENTITY(item) == 0 && (flags & 16) == 0))
      return true;
    if (c->world.has_source_pickups && c->world.source_pickups.owns_item_goal != NULL) {
      int64_t revision = l->map_revision;
      bool owned = c->world.source_pickups.owns_item_goal(c->world.source_pickups.ctx,
                                                          lib_i32(state_bytes(c->state) + 8), L_ENTITY(item));
      if (revision != l->map_revision) return false;
      if (owned) return true;
    }
    const qa_bot_memory_allocation *indexes = state_indexes(l, c->state);
    if (indexes == NULL) qa_range_error("item weight indexes have not been initialized");
    int64_t offset = (int64_t)I_NUMBER(item_info_cell(l, L_INFO(item))) * 4;
    if (offset < 0 || (uint64_t)(offset + 4) > indexes->length)
      qa_range_error("item weight index is stale for the current item configuration");
    int32_t index = lib_i32(indexes->bytes + offset);
    if (index < 0) return true;
    float evaluated = qa_weight_config_evaluate_undecided(state_weight_config(l, c->state), index, c->inventory,
                                                          l->options.random);
    if (L_TIMEOUT(item) != 0) {
      double dropped = l->options.dropped_weight != NULL ? l->options.dropped_weight(l->options.dropped_weight_ctx) : 1000;
      evaluated = (float)((double)evaluated + (double)(float)dropped);
    }
    if ((L_FLAGS(item) & 16) != 0) evaluated = (float)((double)evaluated * (double)L_WEIGHT(item));
    weight = evaluated;
    goal_area = L_GOAL_AREA(item);
    number = L_NUMBER(item);
    goal_origin = L_GOAL_ORIGIN(item);
    return_leg = L_TIMEOUT(item) == 0;
  } else {
    weight = (float)current->utility;
    goal_area = current->goal.area;
    number = current->goal.number;
    goal_origin = current->goal.origin;
  }
  if (!(weight > 0)) return true;
  double time = travel_time(&c->navigation, c->area, c->origin, goal_area, c->travel_flags);
  if (time <= 0 || (c->nearby && !(time < (double)(float)c->max_time))) return true;
  if ((double)qa_bot_goal_avoid_goal_time(l, c->handle, number) - time * 0.009 > 0) return true;
  weight = (float)(weight / ((double)(float)time * 0.01));
  if (!(weight > c->best_weight)) return true;
  if (c->nearby) {
    double back = c->long_term_goal != NULL && return_leg
                      ? travel_time(&c->navigation, goal_area, goal_origin, c->long_term_goal->area, c->travel_flags)
                      : 0;
    if (back > c->long_term_time) return true;
  }
  c->best = *current;
  c->best_weight = weight;
  c->has_best = true;
  return true;
}
/* TS itemCandidates' source half. False is the `return false` from the consumer loop. */
static bool consider_source_candidates(chooser *c) {
  qa_bot_goal_library *l = c->library;
  if (!l->has_world || !l->world.has_source_pickups || !l->world.has_navigation) return true;
  qa_source_pickup_goals pickups = l->world.source_pickups;
  qa_goal_navigation navigation = l->world.navigation;
  validate_source_goal_range(l);
  int64_t revision = l->map_revision, client = lib_i32(state_bytes(c->state) + 8);
  size_t count = 0;
  const qa_source_pickup_goal *listed = pickups.candidates(pickups.ctx, client, &count);
  if (revision != l->map_revision) return true;
  qa_actor_id *actors = lib_alloc(count * sizeof *actors);
  for (size_t i = 0; i < count; i++) actors[i] = listed[i].actor;
  bool result = true;
  for (size_t i = 0; i < count; i++) {
    qa_source_pickup_goal current;
    bool inspected = pickups.inspect(pickups.ctx, client, actors[i], &current);
    if (revision != l->map_revision) break;
    if (!inspected || !same_actor(current.actor, actors[i]) || !(current.utility > 0)) continue;
    qa_goal_source_binding *binding = NULL;
    for (size_t b = 0; b < l->source_goal_count && binding == NULL; b++)
      if (same_actor(l->source_goals[b].actor, current.actor)) binding = &l->source_goals[b];
    qa_vec3 world_min = qa_add3(current.origin, current.bounds.min), world_max = qa_add3(current.origin, current.bounds.max);
    qa_vec3 center = qa_scale3(qa_add3(world_min, world_max), 0.5);
    qa_bounds relative = {qa_sub3(world_min, center), qa_sub3(world_max, center)};
    char *name = lib_strdup(current.name);
    uint64_t binding_serial = binding != NULL ? binding->serial : 0;
    qa_goal_reachable_position position = navigation.best_reachable_area(navigation.ctx, center, relative);
    if (revision != l->map_revision) {
      free(name);
      break;
    }
    if (position.area == 0) {
      free(name);
      continue;
    }
    qa_vec3 mins = qa_sub3(world_min, position.origin), maxs = qa_sub3(world_max, position.origin);
    if (binding_serial != 0) {
      binding = NULL;
      for (size_t b = 0; b < l->source_goal_count && binding == NULL; b++)
        if (l->source_goals[b].serial == binding_serial) binding = &l->source_goals[b];
    } else {
      binding = NULL;
    }
    if (binding == NULL) {
      if (!qa_is_source_goal_number((double)l->next_source_goal))
        qa_range_error("Source pickup goal number range exhausted");
      int64_t number = l->next_source_goal--;
      if (l->source_goal_count == l->source_goal_capacity) {
        l->source_goal_capacity = lib_grow(l->source_goal_capacity, l->source_goal_count + 1);
        l->source_goals = lib_realloc(l->source_goals, l->source_goal_capacity * sizeof *l->source_goals);
      }
      binding = &l->source_goals[l->source_goal_count++];
      *binding = (qa_goal_source_binding){number, l->next_serial++, current.actor, NULL, blank_goal()};
      binding->goal = (qa_bot_goal){position.origin, position.area, mins, maxs, current.entity, number,
                                    QA_GOAL_FLAG_ITEM, -1};
    } else {
      binding->goal.origin = position.origin;
      binding->goal.area = position.area;
      binding->goal.mins = mins;
      binding->goal.maxs = maxs;
      binding->goal.entity = current.entity;
    }
    free(binding->name);
    binding->name = name;
    candidate source = {false, 0, NULL, binding->goal, current.utility};
    if (!consider_candidate(c, &source)) {
      result = false;
      break;
    }
  }
  free(actors);
  return result;
}
static bool choose_item(qa_bot_goal_library *l, int64_t handle, qa_vec3 origin, qa_weight_inventory inventory,
                        int64_t travel_flags, bool nearby, const qa_bot_goal *long_term_goal, double max_time) {
  qa_goal_state_cell *state = state_of(l, handle);
  if (state == NULL || state_weight_config(l, state) == NULL || !l->has_world) return false;
  if (!l->world.has_navigation) return false;
  chooser c;
  memset(&c, 0, sizeof c);
  c.library = l;
  c.state = state;
  c.handle = handle;
  c.world = l->world;
  c.navigation = l->world.navigation;
  c.origin = origin;
  c.inventory = inventory;
  c.travel_flags = travel_flags;
  c.nearby = nearby;
  c.long_term_goal = long_term_goal;
  c.max_time = max_time;
  uint8_t *b = state_bytes(state);
  int64_t area = c.navigation.reachability_area(c.navigation.ctx, origin, lib_i32(b + 8));
  if (area == 0 || c.navigation.area(c.navigation.ctx, area).reachable_area_count == 0) area = lib_i32(b + 12);
  lib_set_i32(b + 12, (int32_t)area);
  if (area == 0) return false;
  c.area = area;
  c.long_term_time = nearby && long_term_goal != NULL
                         ? travel_time(&c.navigation, area, origin, long_term_goal->area, travel_flags)
                         : 99999;
  if (l->config == NULL) return false;
  for (int64_t pointer = l->level_head; pointer != 0;) {
    candidate native = {true, pointer, l->level_item_heap.record, blank_goal(), 0};
    if (!consider_candidate(&c, &native)) return false;
    pointer = L_NEXT(level_item(l, pointer));
  }
  if (!consider_source_candidates(&c)) return false;
  if (!c.has_best) return false;
  if (c.best.native) {
    /* TS keeps the LevelItem's allocation: reading it after a re-init raises the freed guard. */
    if (!l->has_level_item_heap || l->level_item_heap.record != c.best.heap_record)
      qa_range_error("Bot memory allocation has been freed");
    uint8_t *item = level_item(l, c.best.pointer);
    add_avoid(l, state, L_NUMBER(item),
              L_TIMEOUT(item) != 0 ? 10 : default_avoid_time(item_info_cell(l, L_INFO(item))));
    qa_bot_goal goal = item_goal(l, item);
    qa_bot_goal_push_goal(l, handle, &goal);
  } else {
    qa_bot_goal_push_goal(l, handle, &c.best.goal);
  }
  return true;
}
bool qa_bot_goal_choose_ltg_item(qa_bot_goal_library *l, int64_t handle, qa_vec3 origin, qa_weight_inventory inventory,
                                 int64_t travel_flags) {
  return choose_item(l, handle, origin, inventory, travel_flags, false, NULL, 0);
}
bool qa_bot_goal_choose_nbg_item(qa_bot_goal_library *l, int64_t handle, qa_vec3 origin, qa_weight_inventory inventory,
                                 int64_t travel_flags, const qa_bot_goal *long_term_goal, double max_time) {
  return choose_item(l, handle, origin, inventory, travel_flags, true, long_term_goal, max_time);
}
qa_source_goal_status qa_bot_goal_source_goal_status(qa_bot_goal_library *l, int64_t client, const qa_bot_goal *goal) {
  if (!qa_is_source_goal_number((double)goal->number)) return QA_SOURCE_GOAL_NATIVE;
  qa_goal_source_binding *binding = find_source_goal(l, goal->number);
  if (binding == NULL || !l->has_world || !l->world.has_source_pickups) return QA_SOURCE_GOAL_UNAVAILABLE;
  qa_source_pickup_goals pickups = l->world.source_pickups;
  uint64_t serial = binding->serial;
  qa_actor_id actor = binding->actor;
  qa_source_pickup_goal current;
  bool inspected = pickups.inspect(pickups.ctx, client, actor, &current);
  qa_goal_source_binding *after = find_source_goal(l, goal->number);
  return after != NULL && after->serial == serial && inspected && same_actor(current.actor, actor) &&
                 current.entity == goal->entity && current.utility > 0
             ? QA_SOURCE_GOAL_AVAILABLE
             : QA_SOURCE_GOAL_UNAVAILABLE;
}
bool qa_bot_goal_item_goal_in_vis_but_not_visible(qa_bot_goal_library *l, int64_t viewer, qa_vec3 eye, qa_vec3 view_angles,
                                                  const qa_bot_goal *goal) {
  (void)view_angles; /* TS `_viewAngles` is unused. */
  qa_source_goal_status source = qa_bot_goal_source_goal_status(l, viewer, goal);
  if (source != QA_SOURCE_GOAL_NATIVE) return source == QA_SOURCE_GOAL_UNAVAILABLE;
  if ((goal->flags & QA_GOAL_FLAG_ITEM) == 0 || !l->has_world) return false;
  /* be_ai_goal.c:1647 adds mins to mins, not mins to maxs. Preserve its corner target. */
  qa_vec3 middle = qa_add3(goal->origin, qa_scale3(qa_add3(goal->mins, goal->mins), 0.5));
  qa_goal_world_host host = l->world.host;
  if (host.trace(host.ctx, eye, middle, NULL, viewer, 1) < 1 || goal->entity <= 0) return false;
  return host.entity_info(host.ctx, goal->entity).last_update_time < (double)now(l) - 0.5;
}
/* ---- checkpoint ---- */
static const char *goal_severity_name(qa_goal_severity severity) {
  static const char *const names[] = {"message", "warning", "error", "fatal"};
  return names[severity];
}
static qa_save_value *goal_value(const qa_bot_goal *goal) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set(object, "origin", lib_vec3_value(goal->origin));
  lib_set_num(object, "area", (double)goal->area);
  lib_set(object, "mins", lib_vec3_value(goal->mins));
  lib_set(object, "maxs", lib_vec3_value(goal->maxs));
  lib_set_num(object, "entity", (double)goal->entity);
  lib_set_num(object, "number", (double)goal->number);
  lib_set_num(object, "flags", (double)goal->flags);
  lib_set_num(object, "itemInfo", (double)goal->item_info);
  return object;
}
qa_save_value *qa_bot_goal_checkpoint(qa_bot_goal_library *l, qa_bot_memory_capture *memory) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "generation", (double)l->generation);
  lib_set_num(object, "setupRevision", (double)l->setup_revision);
  lib_set_num(object, "mapRevision", (double)l->map_revision);
  lib_set_num(object, "configuredGameType", (double)l->configured_game_type);
  lib_set(object, "hasWorld", qa_save_value_boolean(l->has_world));
  qa_save_value *reported = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < l->reported_count; i++) {
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set(entry, "severity", qa_save_value_string(goal_severity_name(l->reported[i].severity)));
    lib_set(entry, "message", qa_save_value_string(l->reported[i].message));
    lib_push(reported, entry);
  }
  lib_set(object, "reported", reported);
  lib_set_num(object, "nextId", (double)l->next_id);
  qa_save_value *configs = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < l->config_reference_count; i++) {
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)l->config_references[i].id);
    lib_set_num(entry, "config", (double)qa_weight_config_store_reference(l->options.weight_store, l->config_references[i].config));
    lib_push(configs, entry);
  }
  lib_set(object, "configs", configs);
  qa_save_value *indexes = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < l->index_reference_count; i++) {
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)l->index_references[i].id);
    lib_set_num(entry, "allocation", (double)qa_bot_memory_capture_reference(memory, &l->index_references[i].allocation));
    lib_push(indexes, entry);
  }
  lib_set(object, "indexes", indexes);
  /* TS Map order is allocation order. */
  qa_save_value *states = lib_sv(qa_save_value_array());
  uint64_t last = 0;
  while (true) {
    int64_t chosen = 0;
    for (int64_t handle = 1; handle <= QA_MAX_GOAL_STATES; handle++)
      if (l->states[handle].used && l->states[handle].serial > last &&
          (chosen == 0 || l->states[handle].serial < l->states[chosen].serial))
        chosen = handle;
    if (chosen == 0) break;
    last = l->states[chosen].serial;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "handle", (double)chosen);
    lib_set_num(entry, "allocation", (double)qa_bot_memory_capture_reference(memory, &l->states[chosen].allocation));
    lib_push(states, entry);
  }
  lib_set(object, "states", states);
  if (l->config == NULL) {
    lib_set(object, "config", qa_save_value_null());
  } else {
    qa_save_value *config = lib_sv(qa_save_value_object());
    lib_set_num(config, "allocation", (double)qa_bot_memory_capture_reference(memory, &l->config->allocation));
    lib_set(config, "path", qa_save_value_string(l->config->path));
    lib_set(config, "diagnostics", lib_diagnostics_value(&l->config->diagnostics));
    lib_set(object, "config", config);
  }
  lib_set_num(object, "nextSourceGoal", (double)l->next_source_goal);
  qa_save_value *source_goals = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < l->source_goal_count; i++) {
    const qa_goal_source_binding *binding = &l->source_goals[i];
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "number", (double)binding->number);
    qa_save_value *actor = lib_sv(qa_save_value_object());
    lib_set_num(actor, "slot", (double)binding->actor.slot);
    lib_set_num(actor, "generation", (double)binding->actor.generation);
    lib_set(entry, "actor", actor);
    lib_set(entry, "name", qa_save_value_string(binding->name));
    lib_set(entry, "goal", goal_value(&binding->goal));
    lib_push(source_goals, entry);
  }
  lib_set(object, "sourceGoals", source_goals);
  lib_set(object, "levelItemHeap",
          l->has_level_item_heap ? qa_save_value_number((double)qa_bot_memory_capture_reference(memory, &l->level_item_heap))
                                 : qa_save_value_null());
  uint8_t map_words[20];
  lib_set_u32(map_words, l->level_head);
  lib_set_u32(map_words + 4, l->free_level_head);
  lib_set_i32(map_words + 8, l->initial_item_count);
  lib_set_u32(map_words + 12, l->location_head);
  lib_set_u32(map_words + 16, l->camp_head);
  qa_save_value *words = lib_sv(qa_save_value_array());
  for (int i = 0; i < 20; i++) lib_push(words, qa_save_value_number(map_words[i]));
  lib_set(object, "mapWords", words);
  lib_set_num(object, "nextInfoPointer", (double)l->next_info_pointer);
  qa_save_value *info = lib_sv(qa_save_value_array());
  for (size_t pointer = 0; pointer < l->info_entity_capacity; pointer++) {
    qa_goal_info_entity *entity = &l->info_entities[pointer];
    if (!entity->live) continue;
    qa_save_value *entry = lib_sv(qa_save_value_object());
    lib_set_num(entry, "pointer", (double)pointer);
    lib_set_num(entry, "allocation", (double)qa_bot_memory_capture_reference(memory, &entity->allocation));
    lib_set(entry, "kind", qa_save_value_string(entity->is_camp ? "camp" : "location"));
    lib_push(info, entry);
  }
  lib_set(object, "infoEntities", info);
  return object;
}
static qa_vec3 read_vector_field(const qa_save_reader *goal, const char *name) {
  qa_save_reader vector, x, y, z;
  lib_field(goal, name, &vector);
  lib_field(&vector, "x", &x);
  lib_field(&vector, "y", &y);
  lib_field(&vector, "z", &z);
  qa_vec3 result = {qa_save_reader_number(&x), qa_save_reader_number(&y), qa_save_reader_number(&z)};
  qa_save_reader_destroy(&x);
  qa_save_reader_destroy(&y);
  qa_save_reader_destroy(&z);
  qa_save_reader_destroy(&vector);
  return result;
}
static void visit_list(qa_bot_goal_library *l, int64_t start, int kind) {
  /* TS visit(): a seen-set walk over one saved link chain. */
  size_t capacity = 16, count = 0;
  int64_t *seen = lib_alloc(capacity * sizeof *seen);
  for (int64_t pointer = start; pointer != 0;) {
    for (size_t i = 0; i < count; i++)
      if (seen[i] == pointer) qa_ui_script_plain_error("Saved goal list has a cycle");
    if (count == capacity) {
      capacity *= 2;
      seen = lib_realloc(seen, capacity * sizeof *seen);
    }
    seen[count++] = pointer;
    pointer = kind == 0 ? (int64_t)L_NEXT(level_item(l, pointer)) : (int64_t)info_next(info_entity(l, pointer));
  }
  free(seen);
}
void qa_bot_goal_restore(qa_bot_goal_library *l, const qa_save_value *value, qa_bot_memory_restored *memory,
                         const qa_goal_world *world, qa_goal_restore_actor_fn actor, void *actor_ctx) {
  static const char *const severities[] = {"message", "warning", "error", "fatal"};
  static const char *const kinds[] = {"camp", "location"};
  qa_save_reader reader, reported, configs, indexes, states, config, source_goals, heap, words, info;
  if (!qa_save_reader_init(&reader, value, "bot.goals")) qa_range_error("out of memory");
  double generation = lib_int_min(&reader, "generation", 0), setup_revision = lib_int_min(&reader, "setupRevision", 0);
  double map_revision = lib_int_min(&reader, "mapRevision", 0), game_type = lib_int(&reader, "configuredGameType");
  bool has_world = lib_read_bool(&reader, "hasWorld");
  double next_id = lib_int_min(&reader, "nextId", 1), next_source = lib_int_min(&reader, "nextSourceGoal", 0);
  double next_info = lib_int_min(&reader, "nextInfoPointer", 1);
  lib_field(&reader, "reported", &reported);
  lib_field(&reader, "configs", &configs);
  lib_field(&reader, "indexes", &indexes);
  lib_field(&reader, "states", &states);
  lib_field(&reader, "config", &config);
  lib_field(&reader, "sourceGoals", &source_goals);
  lib_field(&reader, "levelItemHeap", &heap);
  lib_field(&reader, "mapWords", &words);
  lib_field(&reader, "infoEntities", &info);
  uint8_t map_words[20];
  bool words_ok = qa_save_reader_list_count(&words) == 20;
  for (size_t i = 0; words_ok && i < 20; i++) {
    qa_save_reader entry;
    lib_item(&words, i, &entry);
    double byte = qa_save_reader_integer_min(&entry, 0);
    qa_save_reader_destroy(&entry);
    if (byte > 255) words_ok = false;
    else map_words[i] = (uint8_t)byte;
  }
  if (any_state(l) || l->config != NULL || l->has_world || has_world != (world != NULL) || !words_ok ||
      next_id < 1 || next_id > 4294967296.0 || next_info < 1 || next_info > 4294967296.0)
    qa_ui_script_plain_error("Invalid goal library restoration");
  size_t config_count = qa_save_reader_list_count(&configs);
  int64_t *admitted = lib_alloc((config_count + qa_save_reader_list_count(&indexes) + 1) * sizeof *admitted);
  size_t admitted_count = 0;
  for (size_t i = 0; i < config_count; i++) {
    qa_save_reader entry;
    lib_item(&configs, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1), reference = lib_int_min(&entry, "config", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next_id) qa_ui_script_plain_error("Invalid saved goal pointer");
    for (size_t a = 0; a < admitted_count; a++)
      if (admitted[a] == (int64_t)pointer) qa_ui_script_plain_error("Invalid saved goal pointer");
    admitted[admitted_count++] = (int64_t)pointer;
    qa_weight_config_bots_a *weight_config = qa_weight_config_store_resolve(l->options.weight_store, reference);
    for (size_t r = 0; r < l->config_reference_count; r++)
      if (l->config_references[r].config == weight_config)
        qa_ui_script_plain_error("Duplicate saved goal weight identity");
    if (l->config_reference_count == l->config_reference_capacity) {
      l->config_reference_capacity = lib_grow(l->config_reference_capacity, l->config_reference_count + 1);
      l->config_references = lib_realloc(l->config_references, l->config_reference_capacity * sizeof *l->config_references);
    }
    l->config_references[l->config_reference_count++] = (qa_goal_config_reference){(int64_t)pointer, weight_config};
  }
  size_t index_count = qa_save_reader_list_count(&indexes);
  for (size_t i = 0; i < index_count; i++) {
    qa_save_reader entry;
    lib_item(&indexes, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1), reference = lib_int_min(&entry, "allocation", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next_id) qa_ui_script_plain_error("Invalid saved goal pointer");
    for (size_t a = 0; a < admitted_count; a++)
      if (admitted[a] == (int64_t)pointer) qa_ui_script_plain_error("Invalid saved goal pointer");
    admitted[admitted_count++] = (int64_t)pointer;
    if (l->index_reference_count == l->index_reference_capacity) {
      l->index_reference_capacity = lib_grow(l->index_reference_capacity, l->index_reference_count + 1);
      l->index_references = lib_realloc(l->index_references, l->index_reference_capacity * sizeof *l->index_references);
    }
    l->index_references[l->index_reference_count++] =
        (qa_goal_index_reference){(int64_t)pointer, qa_bot_memory_restored_allocation(memory, (int64_t)reference)};
  }
  free(admitted);
  size_t state_count = qa_save_reader_list_count(&states);
  for (size_t i = 0; i < state_count; i++) {
    qa_save_reader entry;
    lib_item(&states, i, &entry);
    double handle = lib_int_min(&entry, "handle", 1), reference = lib_int_min(&entry, "allocation", 0);
    qa_save_reader_destroy(&entry);
    if (handle < 1 || handle > QA_MAX_GOAL_STATES || l->states[(size_t)handle].used)
      qa_ui_script_plain_error("Invalid saved goal state handle");
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (allocation.length != GOAL_STATE_BYTES) qa_ui_script_plain_error("Saved goal state allocation size mismatch");
    qa_goal_state_cell *state = &l->states[(size_t)handle];
    *state = (qa_goal_state_cell){true, l->next_serial++, allocation};
    (void)state_weight_config(l, state);
    (void)state_indexes(l, state);
    if (stack_top(state) < 0 || stack_top(state) > QA_MAX_GOAL_STACK)
      qa_ui_script_plain_error("Saved goal stack exceeds capacity");
  }
  if (!qa_save_reader_is_null(&config)) {
    double reference = lib_int_min(&config, "allocation", 0);
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (allocation.length < ITEM_CONFIG_BYTES) qa_ui_script_plain_error("Saved item configuration is truncated");
    int32_t count = lib_i32(allocation.bytes);
    if (count < 0 || ITEM_CONFIG_BYTES + (int64_t)count * ITEM_INFO_BYTES > (int64_t)allocation.length)
      qa_ui_script_plain_error("Saved item count exceeds allocation");
    qa_item_config_bots_a *restored = lib_alloc(sizeof *restored);
    restored->memory = l->memory;
    restored->allocation = allocation;
    restored->path = lib_read_string(&config, "path");
    lib_read_diagnostics(&config, "diagnostics", &restored->diagnostics);
    track_config(l, restored);
    l->config = restored;
  }
  size_t source_count = qa_save_reader_list_count(&source_goals);
  for (size_t i = 0; i < source_count; i++) {
    qa_save_reader entry, goal, saved_actor;
    lib_item(&source_goals, i, &entry);
    double number = lib_int(&entry, "number");
    lib_field(&entry, "actor", &saved_actor);
    qa_saved_actor_id saved = {lib_int_min(&saved_actor, "slot", 0), lib_int_min(&saved_actor, "generation", 0)};
    qa_save_reader_destroy(&saved_actor);
    char *name = lib_read_string(&entry, "name");
    lib_field(&entry, "goal", &goal);
    qa_bot_goal restored = {read_vector_field(&goal, "origin"), (int64_t)lib_int(&goal, "area"),
                            read_vector_field(&goal, "mins"), read_vector_field(&goal, "maxs"),
                            (int64_t)lib_int(&goal, "entity"), (int64_t)lib_int(&goal, "number"),
                            (int64_t)lib_int(&goal, "flags"), (int64_t)lib_int(&goal, "itemInfo")};
    qa_save_reader_destroy(&goal);
    qa_save_reader_destroy(&entry);
    if (!qa_is_source_goal_number(number) || find_source_goal(l, (int64_t)number) != NULL)
      qa_ui_script_plain_error("Invalid saved source pickup goal identity");
    if (l->source_goal_count == l->source_goal_capacity) {
      l->source_goal_capacity = lib_grow(l->source_goal_capacity, l->source_goal_count + 1);
      l->source_goals = lib_realloc(l->source_goals, l->source_goal_capacity * sizeof *l->source_goals);
    }
    l->source_goals[l->source_goal_count++] =
        (qa_goal_source_binding){(int64_t)number, l->next_serial++, actor(actor_ctx, saved), name, restored};
  }
  if (!qa_save_reader_is_null(&heap)) {
    double reference = qa_save_reader_integer_min(&heap, 0);
    l->level_item_heap = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    l->has_level_item_heap = true;
    if (l->level_item_heap.length % LEVEL_ITEM_BYTES != 0)
      qa_ui_script_plain_error("Saved level item heap size mismatch");
  }
  size_t info_count = qa_save_reader_list_count(&info);
  for (size_t i = 0; i < info_count; i++) {
    qa_save_reader entry, kind_reader;
    lib_item(&info, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1), reference = lib_int_min(&entry, "allocation", 0);
    lib_field(&entry, "kind", &kind_reader);
    bool is_camp = strcmp(qa_save_reader_choice_string(&kind_reader, kinds, 2), "camp") == 0;
    qa_save_reader_destroy(&kind_reader);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next_info ||
        ((size_t)pointer < l->info_entity_capacity && l->info_entities[(size_t)pointer].live))
      qa_ui_script_plain_error("Invalid saved goal info pointer");
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (allocation.length != (size_t)(is_camp ? CAMP_SPOT_BYTES : MAP_LOCATION_BYTES))
      qa_ui_script_plain_error("Saved goal info allocation size mismatch");
    if ((size_t)pointer >= l->info_entity_capacity) {
      size_t next = lib_grow(l->info_entity_capacity, (size_t)pointer + 1);
      l->info_entities = lib_realloc(l->info_entities, next * sizeof *l->info_entities);
      memset(l->info_entities + l->info_entity_capacity, 0, (next - l->info_entity_capacity) * sizeof *l->info_entities);
      l->info_entity_capacity = next;
    }
    l->info_entities[(size_t)pointer] = (qa_goal_info_entity){true, is_camp, allocation};
  }
  l->level_head = lib_u32(map_words);
  l->free_level_head = lib_u32(map_words + 4);
  l->initial_item_count = lib_i32(map_words + 8);
  l->location_head = lib_u32(map_words + 12);
  l->camp_head = lib_u32(map_words + 16);
  l->next_id = (int64_t)next_id;
  l->next_info_pointer = (int64_t)next_info;
  l->next_source_goal = (int64_t)next_source;
  l->generation = (int64_t)generation;
  l->setup_revision = (int64_t)setup_revision;
  l->map_revision = (int64_t)map_revision;
  l->configured_game_type = (int64_t)game_type;
  if (world != NULL) {
    l->world = *world;
    l->has_world = true;
  }
  size_t reported_count = qa_save_reader_list_count(&reported);
  for (size_t i = 0; i < reported_count; i++) {
    qa_save_reader entry, severity_reader;
    lib_item(&reported, i, &entry);
    lib_field(&entry, "severity", &severity_reader);
    const char *severity = qa_save_reader_choice_string(&severity_reader, severities, 4);
    qa_goal_severity level = QA_GOAL_MESSAGE;
    for (int s = 0; s < 4; s++) if (strcmp(severity, severities[s]) == 0) level = (qa_goal_severity)s;
    char *message = lib_read_string(&entry, "message");
    if (l->reported_count == l->reported_capacity) {
      l->reported_capacity = lib_grow(l->reported_capacity, l->reported_count + 1);
      l->reported = lib_realloc(l->reported, l->reported_capacity * sizeof *l->reported);
    }
    l->reported[l->reported_count++] = (qa_goal_diagnostic){level, message};
    qa_save_reader_destroy(&severity_reader);
    qa_save_reader_destroy(&entry);
  }
  visit_list(l, l->level_head, 0);
  visit_list(l, l->free_level_head, 0);
  visit_list(l, l->location_head, 1);
  visit_list(l, l->camp_head, 1);
  qa_save_reader_destroy(&reported);
  qa_save_reader_destroy(&configs);
  qa_save_reader_destroy(&indexes);
  qa_save_reader_destroy(&states);
  qa_save_reader_destroy(&config);
  qa_save_reader_destroy(&source_goals);
  qa_save_reader_destroy(&heap);
  qa_save_reader_destroy(&words);
  qa_save_reader_destroy(&info);
  qa_save_reader_destroy(&reader);
}
