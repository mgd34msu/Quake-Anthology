/* Port of src/core/cvars/index.ts. GPL-2.0-or-later. */
#include "qa/core/cvars/index.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/cvars/info.h"
#include "qa/core/cvars/numbers.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

#define Q2_NO_ARCHIVE                                                        \
  (QA_Q2_CVAR_FLAG_NO_SET | QA_Q2_CVAR_FLAG_CHEAT | QA_Q2_CVAR_FLAG_PRIVATE | \
   QA_Q2_CVAR_FLAG_READ_ONLY | QA_Q2_CVAR_FLAG_NO_ARCHIVE)

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static bool set_owned(char **slot, const char *text) {
  char *copy = dup_text(text);
  if (copy == NULL) return false;
  free(*slot);
  *slot = copy;
  return true;
}
typedef struct text_buffer { char *data; size_t length; size_t capacity; } text_buffer;
static bool text_add(text_buffer *b, const char *text) {
  size_t length = strlen(text);
  if (b->length + length + 1 > b->capacity) {
    size_t cap = b->capacity == 0 ? 256 : b->capacity;
    while (cap < b->length + length + 1) cap *= 2;
    char *data = (char *)realloc(b->data, cap);
    if (data == NULL) return false;
    b->data = data;
    b->capacity = cap;
  }
  memcpy(b->data + b->length, text, length + 1);
  b->length += length;
  return true;
}

typedef struct cvar_state {
  int64_t index;
  char *name; /* owned */
  char *key;  /* owned lookup key */
  char *value;
  char *reset_value;
  char *latched_value; /* NULL = undefined */
  int32_t flags;
  bool modified;
  double modification_count;
  double numeric_value;
  double integer_value;
  int64_t next; /* index into states, -1 nil */
} cvar_state;

typedef struct alias_entry {
  char *key;
  char *name;
  char *target;
  char *target_key;
  qa_command_documentation documentation;
  qa_cvar_alias_conversion conversion;
} alias_entry;
typedef struct binding_entry {
  char *key;
  int64_t id;
  qa_cvar_value_binding binding;
} binding_entry;
typedef struct document_entry {
  char *key;
  qa_command_documentation documentation;
} document_entry;
typedef struct output_binding {
  int64_t id;
  qa_command_print_fn print;
  void *ctx;
} output_binding;
typedef struct alias_handle {
  int64_t handle;
  char *name;
} alias_handle;

struct qa_cvar_registry {
  qa_cvar_registry_options options;
  cvar_state **indexes; /* NULL holes */
  size_t index_count;
  size_t index_capacity;
  int64_t first; /* state index, -1 nil */
  int32_t changed_flags;
  bool cheats_enabled;
  bool server_active;
  bool client_connected;
  bool high_characters;
  char *client_info;
  char *server_info;
  qa_cvar_effect *effects;
  size_t effect_count;
  size_t effect_capacity;
  bool userinfo_dirty;
  char **console_variables;
  size_t console_count;
  size_t console_capacity;
  document_entry *documents;
  size_t document_count;
  size_t document_capacity;
  binding_entry *bindings;
  size_t binding_count;
  size_t binding_capacity;
  alias_entry *aliases;
  size_t alias_count;
  size_t alias_capacity;
  alias_handle *alias_handles;
  size_t alias_handle_count;
  size_t alias_handle_capacity;
  output_binding *output_bindings;
  size_t output_count;
  size_t output_capacity;
  int64_t next_binding_id;
  double mutation_revision;
};

static void state_free(cvar_state *state) {
  if (state == NULL) return;
  free(state->name);
  free(state->key);
  free(state->value);
  free(state->reset_value);
  free(state->latched_value);
  free(state);
}
void qa_cvar_read_free(qa_cvar_read *read) {
  free(read->name);
  free(read->value);
  free(read->reset_value);
  free(read->latched_value);
  memset(read, 0, sizeof *read);
}
void qa_cvar_effect_free(qa_cvar_effect *effect) {
  free(effect->name);
  free(effect->value);
  free(effect->info);
  free(effect->command);
  free(effect->text);
  free(effect->directory);
  memset(effect, 0, sizeof *effect);
}
static bool effect_copy(qa_cvar_effect *dest, const qa_cvar_effect *source) {
  memset(dest, 0, sizeof *dest);
  dest->kind = source->kind;
  dest->context = source->context;
  dest->name = source->name != NULL ? dup_text(source->name) : NULL;
  dest->value = source->value != NULL ? dup_text(source->value) : NULL;
  dest->info = source->info != NULL ? dup_text(source->info) : NULL;
  dest->command = source->command != NULL ? dup_text(source->command) : NULL;
  dest->text = source->text != NULL ? dup_text(source->text) : NULL;
  dest->directory = source->directory != NULL ? dup_text(source->directory) : NULL;
  return (source->name == NULL || dest->name != NULL) &&
         (source->value == NULL || dest->value != NULL) &&
         (source->info == NULL || dest->info != NULL) &&
         (source->command == NULL || dest->command != NULL) &&
         (source->text == NULL || dest->text != NULL) &&
         (source->directory == NULL || dest->directory != NULL);
}
#include <stdarg.h>

static bool valid_info(const char *text) {
  return strchr(text, '\\') == NULL && strchr(text, '"') == NULL &&
         strchr(text, ';') == NULL;
}
static bool grow_index_array(qa_cvar_registry *r) {
  if (r->index_count < r->index_capacity) return true;
  size_t cap = r->index_capacity == 0 ? 16 : r->index_capacity * 2;
  cvar_state **items = (cvar_state **)realloc(r->indexes, cap * sizeof *items);
  if (items == NULL) return false;
  r->indexes = items;
  r->index_capacity = cap;
  return true;
}
static cvar_state *state_at(qa_cvar_registry *r, int64_t index) {
  if (index < 0 || (size_t)index >= r->index_count) return NULL;
  return r->indexes[index];
}
static char *registry_key(qa_cvar_registry *r, const char *name) {
  char *text = qa_source_command_text(name);
  if (text == NULL) return NULL;
  if (r->options.dialect != QA_COMMAND_DIALECT_Q3) return text;
  char *folded = qa_ascii_fold(text);
  free(text);
  return folded;
}
static cvar_state *find_state_by_key(qa_cvar_registry *r, const char *key) {
  for (size_t i = 0; i < r->index_count; i++)
    if (r->indexes[i] != NULL && strcmp(r->indexes[i]->key, key) == 0)
      return r->indexes[i];
  return NULL;
}
static cvar_state *find_state(qa_cvar_registry *r, const char *name) {
  char *key = registry_key(r, name);
  if (key == NULL) return NULL;
  cvar_state *state = find_state_by_key(r, key);
  free(key);
  return state;
}
static alias_entry *find_alias_by_key(qa_cvar_registry *r, const char *key) {
  for (size_t i = 0; i < r->alias_count; i++)
    if (strcmp(r->aliases[i].key, key) == 0) return &r->aliases[i];
  return NULL;
}
static alias_entry *find_alias(qa_cvar_registry *r, const char *name) {
  char *key = registry_key(r, name);
  if (key == NULL) return NULL;
  alias_entry *alias = find_alias_by_key(r, key);
  free(key);
  return alias;
}
static binding_entry *find_binding_by_key(qa_cvar_registry *r, const char *key) {
  for (size_t i = 0; i < r->binding_count; i++)
    if (strcmp(r->bindings[i].key, key) == 0) return &r->bindings[i];
  return NULL;
}
static void registry_print(qa_cvar_registry *r, const char *text) {
  qa_command_print_fn output = r->options.print;
  void *ctx = r->options.print_ctx;
  for (size_t i = 0; i < r->output_count; i++) {
    output = r->output_bindings[i].print;
    ctx = r->output_bindings[i].ctx;
  }
  if (output != NULL) output(ctx, text);
}
static void registry_print_format(qa_cvar_registry *r, const char *format, ...) {
  char buffer[4096];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  registry_print(r, buffer);
}
static void compute_numbers(qa_cvar_registry *r, const char *value,
                            double *numeric, double *integer) {
  double raw = qa_command_dialect_is_q1(r->options.dialect) ? qa_quake_atof(value)
                                                            : qa_native_atof(value);
  *numeric = (double)(float)raw;
  *integer = (double)qa_native_atoi(value);
}
static bool snapshot_state(const cvar_state *state, qa_cvar_read *out) {
  memset(out, 0, sizeof *out);
  out->name = dup_text(state->name);
  out->value = dup_text(state->value);
  out->reset_value = dup_text(state->reset_value);
  out->latched_value = state->latched_value != NULL ? dup_text(state->latched_value) : NULL;
  out->flags = state->flags;
  out->modified = state->modified;
  out->modification_count = state->modification_count;
  out->numeric_value = state->numeric_value;
  out->integer_value = state->integer_value;
  if (out->name == NULL || out->value == NULL || out->reset_value == NULL ||
      (state->latched_value != NULL && out->latched_value == NULL)) {
    qa_cvar_read_free(out);
    return false;
  }
  return true;
}
static bool snapshot_or_false(const cvar_state *state, qa_cvar_snapshot *out) {
  /* Allocation failure has no TS counterpart; report the state
   * fields that fit and fail the call. */
  return snapshot_state(state, out);
}

qa_cvar_registry *qa_cvar_registry_create(const qa_cvar_registry_options *options) {
  qa_cvar_registry *r = (qa_cvar_registry *)calloc(1, sizeof *r);
  if (r == NULL) return NULL;
  r->options = *options;
  r->first = -1;
  r->cheats_enabled = true;
  r->client_info = dup_text("");
  r->server_info = dup_text("");
  r->next_binding_id = 1;
  if (r->client_info == NULL || r->server_info == NULL) {
    qa_cvar_registry_destroy(r);
    return NULL;
  }
  return r;
}
void qa_cvar_registry_destroy(qa_cvar_registry *r) {
  if (r == NULL) return;
  for (size_t i = 0; i < r->index_count; i++) state_free(r->indexes[i]);
  free(r->indexes);
  free(r->client_info);
  free(r->server_info);
  for (size_t i = 0; i < r->effect_count; i++) qa_cvar_effect_free(&r->effects[i]);
  free(r->effects);
  for (size_t i = 0; i < r->console_count; i++) free(r->console_variables[i]);
  free(r->console_variables);
  for (size_t i = 0; i < r->document_count; i++) free(r->documents[i].key);
  free(r->documents);
  for (size_t i = 0; i < r->binding_count; i++) free(r->bindings[i].key);
  free(r->bindings);
  for (size_t i = 0; i < r->alias_count; i++) {
    free(r->aliases[i].key);
    free(r->aliases[i].name);
    free(r->aliases[i].target);
    free(r->aliases[i].target_key);
  }
  free(r->aliases);
  for (size_t i = 0; i < r->alias_handle_count; i++) free(r->alias_handles[i].name);
  free(r->alias_handles);
  free(r->output_bindings);
  free(r);
}
qa_command_dialect qa_cvar_registry_dialect(const qa_cvar_registry *r) {
  return r->options.dialect;
}
qa_command_context qa_cvar_registry_context(const qa_cvar_registry *r) {
  return r->options.context;
}
double qa_cvar_registry_modified_flags(const qa_cvar_registry *r) {
  return (double)r->changed_flags;
}
bool qa_cvar_registry_userinfo_modified(const qa_cvar_registry *r) {
  return r->userinfo_dirty;
}
double qa_cvar_registry_index_count(const qa_cvar_registry *r) {
  return (double)r->index_count;
}
int64_t qa_cvar_registry_bind_output(qa_cvar_registry *r, qa_command_print_fn print, void *ctx) {
  if (r->output_count == r->output_capacity) {
    size_t cap = r->output_capacity == 0 ? 4 : r->output_capacity * 2;
    output_binding *items = (output_binding *)realloc(r->output_bindings, cap * sizeof *items);
    if (items == NULL) return -1;
    r->output_bindings = items;
    r->output_capacity = cap;
  }
  int64_t id = r->next_binding_id++;
  r->output_bindings[r->output_count].id = id;
  r->output_bindings[r->output_count].print = print;
  r->output_bindings[r->output_count].ctx = ctx;
  r->output_count++;
  return id;
}
void qa_cvar_registry_unbind_output(qa_cvar_registry *r, int64_t binding) {
  for (size_t i = 0; i < r->output_count; i++) {
    if (r->output_bindings[i].id == binding) {
      memmove(&r->output_bindings[i], &r->output_bindings[i + 1],
              (r->output_count - i - 1) * sizeof *r->output_bindings);
      r->output_count--;
      return;
    }
  }
}
static void emit_effect(qa_cvar_registry *r, qa_cvar_effect *effect) {
  if (r->effect_count == r->effect_capacity) {
    size_t cap = r->effect_capacity == 0 ? 8 : r->effect_capacity * 2;
    qa_cvar_effect *items = (qa_cvar_effect *)realloc(r->effects, cap * sizeof *items);
    if (items == NULL) return;
    r->effects = items;
    r->effect_capacity = cap;
  }
  r->effects[r->effect_count] = *effect;
  r->effect_count++;
  if (r->options.on_effect != NULL)
    r->options.on_effect(r->options.on_effect_ctx, &r->effects[r->effect_count - 1]);
}
static void info_print(void *ctx, const char *text) {
  registry_print((qa_cvar_registry *)ctx, text);
}
static void game_directory(qa_cvar_registry *r, cvar_state *state) {
  if (strcmp(state->name, "game") != 0) return;
  qa_cvar_effect effect;
  memset(&effect, 0, sizeof effect);
  effect.kind = QA_CVAR_EFFECT_GAME_DIRECTORY;
  effect.context = r->options.context;
  effect.directory = dup_text(state->value);
  emit_effect(r, &effect);
}
static void propagate(qa_cvar_registry *r, cvar_state *state, const char *value, bool changed) {
  qa_command_dialect dialect = r->options.dialect;
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    if ((state->flags & QA_CVAR_FLAG_SERVER_INFO) != 0 && changed && r->server_active) {
      qa_cvar_effect effect;
      memset(&effect, 0, sizeof effect);
      effect.kind = QA_CVAR_EFFECT_BROADCAST;
      effect.context = r->options.context;
      text_buffer text = {NULL, 0, 0};
      (void)text_add(&text, "\"");
      (void)text_add(&text, state->name);
      (void)text_add(&text, "\" changed to \"");
      (void)text_add(&text, value);
      (void)text_add(&text, "\"\n");
      effect.text = text.data;
      emit_effect(r, &effect);
    }
    return;
  }
  if (dialect != QA_COMMAND_DIALECT_Q1_QUAKEWORLD) return;
  static const qa_cvar_info_target defaults[] = {
      QA_CVAR_INFO_CLIENT_USERINFO, QA_CVAR_INFO_SERVER_INFO};
  const qa_cvar_info_target *targets = r->options.info_targets != NULL
      ? r->options.info_targets : defaults;
  size_t target_count = r->options.info_targets != NULL ? r->options.info_target_count : 2;
  for (size_t i = 0; i < target_count; i++) {
    qa_cvar_info_target target = targets[i];
    int32_t flag = target == QA_CVAR_INFO_CLIENT_USERINFO ? QA_CVAR_FLAG_USER_INFO
                                                          : QA_CVAR_FLAG_SERVER_INFO;
    if ((state->flags & flag) == 0) continue;
    qa_info_options options;
    options.dialect = dialect;
    options.maximum_length = target == QA_CVAR_INFO_CLIENT_USERINFO ? 196 : 512;
    options.target = target;
    options.server_high_characters = r->high_characters;
    options.print = info_print;
    options.print_ctx = r;
    const char *current = target == QA_CVAR_INFO_CLIENT_USERINFO ? r->client_info : r->server_info;
    char *info = qa_set_info_value(current, state->name, value, &options);
    if (info == NULL) continue;
    qa_cvar_effect effect;
    memset(&effect, 0, sizeof effect);
    effect.context = r->options.context;
    effect.name = dup_text(state->name);
    effect.value = dup_text(value);
    effect.info = dup_text(info);
    if (target == QA_CVAR_INFO_CLIENT_USERINFO) {
      free(r->client_info);
      r->client_info = info;
      effect.kind = QA_CVAR_EFFECT_USERINFO;
      if (r->client_connected) {
        text_buffer command = {NULL, 0, 0};
        (void)text_add(&command, "setinfo \"");
        (void)text_add(&command, state->name);
        (void)text_add(&command, "\" \"");
        (void)text_add(&command, value);
        (void)text_add(&command, "\"\n");
        effect.command = command.data;
      }
    } else {
      free(r->server_info);
      r->server_info = info;
      effect.kind = QA_CVAR_EFFECT_SERVER_INFO;
    }
    emit_effect(r, &effect);
  }
}
static void binding_changed(qa_cvar_registry *r, const char *key, const char *value) {
  binding_entry *entry = find_binding_by_key(r, key);
  if (entry != NULL && entry->binding.changed != NULL)
    entry->binding.changed(entry->binding.ctx, value);
}
static void apply_value(qa_cvar_registry *r, cvar_state *state, const char *value, bool mark) {
  if (mark) {
    state->modified = true;
    state->modification_count++;
  }
  (void)set_owned(&state->value, value);
  compute_numbers(r, state->value, &state->numeric_value, &state->integer_value);
  binding_changed(r, state->key, state->value);
}

static void reject_alias_info_flags(qa_cvar_registry *r, const char *name, int32_t flags) {
  int32_t mask = QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_SERVER_INFO |
      (r->options.dialect == QA_COMMAND_DIALECT_Q3 ? QA_CVAR_FLAG_SYSTEM_INFO : 0);
  if ((flags & mask) != 0)
    qa_error("Cvar alias %s requires an explicit protocol info-key mapping", name);
}
static char *alias_value(alias_entry *alias, const char *value) {
  if (alias->conversion.is_identity) return dup_text(value);
  return alias->conversion.read(alias->conversion.ctx, value);
}
static char *alias_write(qa_cvar_registry *r, alias_entry *alias, const char *value) {
  if (alias->conversion.is_identity) return dup_text(value);
  char *converted = NULL;
  char *message = NULL;
  if (alias->conversion.write(alias->conversion.ctx, value, &converted, &message))
    return converted;
  registry_print_format(r, "%s: %s\n", alias->name, message != NULL ? message : "");
  free(message);
  return NULL;
}
static bool projected_read(qa_cvar_registry *r, alias_entry *alias, cvar_state *state,
                           qa_cvar_read *out) {
  memset(out, 0, sizeof *out);
  out->name = dup_text(alias->name);
  out->value = alias_value(alias, state->value);
  out->reset_value = alias_value(alias, state->reset_value);
  out->latched_value = state->latched_value != NULL
      ? alias_value(alias, state->latched_value) : NULL;
  out->flags = state->flags;
  out->modified = state->modified;
  out->modification_count = state->modification_count;
  if (out->value != NULL)
    compute_numbers(r, out->value, &out->numeric_value, &out->integer_value);
  if (out->name == NULL || out->value == NULL || out->reset_value == NULL ||
      (state->latched_value != NULL && out->latched_value == NULL)) {
    qa_cvar_read_free(out);
    return false;
  }
  return true;
}
bool qa_cvar_registry_find(qa_cvar_registry *r, const char *name, qa_cvar_read *out) {
  memset(out, 0, sizeof *out);
  char *key = registry_key(r, name);
  if (key == NULL) return false;
  alias_entry *alias = find_alias_by_key(r, key);
  if (alias == NULL) {
    cvar_state *state = find_state_by_key(r, key);
    free(key);
    return state != NULL && snapshot_state(state, out);
  }
  cvar_state *state = find_state_by_key(r, alias->target_key);
  free(key);
  return state != NULL && projected_read(r, alias, state, out);
}
bool qa_cvar_registry_get(qa_cvar_registry *r, const char *name, qa_cvar_snapshot *out) {
  return qa_cvar_registry_find(r, name, out);
}
char *qa_cvar_registry_canonical_name(qa_cvar_registry *r, const char *name) {
  alias_entry *alias = find_alias(r, name);
  return dup_text(alias != NULL ? alias->target : name);
}
void qa_cvar_registry_register_alias(qa_cvar_registry *r, qa_cvar_alias alias) {
  r->mutation_revision++;
  char *key = registry_key(r, alias.name);
  char *target_key = registry_key(r, alias.target);
  if (key == NULL || target_key == NULL) qa_error("out of memory");
  if (strcmp(key, target_key) == 0 || find_alias_by_key(r, target_key) != NULL)
    qa_error("Cvar alias %s must target a canonical variable, not an alias or itself",
             alias.name);
  if (find_state_by_key(r, key) != NULL || find_alias_by_key(r, key) != NULL ||
      (r->options.command_exists != NULL &&
       r->options.command_exists(r->options.command_exists_ctx, alias.name)))
    qa_error("Cvar alias %s is already declared", alias.name);
  cvar_state *target_state = find_state_by_key(r, target_key);
  if (target_state == NULL)
    qa_error("Cvar alias %s target %s is not registered", alias.name, alias.target);
  reject_alias_info_flags(r, alias.name, target_state->flags);
  if (r->alias_count == r->alias_capacity) {
    size_t cap = r->alias_capacity == 0 ? 8 : r->alias_capacity * 2;
    alias_entry *items = (alias_entry *)realloc(r->aliases, cap * sizeof *items);
    if (items == NULL) qa_error("out of memory");
    r->aliases = items;
    r->alias_capacity = cap;
  }
  alias_entry *entry = &r->aliases[r->alias_count++];
  entry->key = key;
  entry->name = dup_text(alias.name);
  entry->target = dup_text(alias.target);
  entry->target_key = target_key;
  entry->documentation = alias.documentation;
  entry->conversion = alias.conversion;
}
bool qa_cvar_registry_is_console_created(qa_cvar_registry *r, const char *name) {
  char *canonical = qa_cvar_registry_canonical_name(r, name);
  char *key = canonical != NULL ? registry_key(r, canonical) : NULL;
  free(canonical);
  if (key == NULL) return false;
  bool found = false;
  for (size_t i = 0; i < r->console_count; i++)
    if (strcmp(r->console_variables[i], key) == 0) found = true;
  free(key);
  return found;
}
static bool validate_binding_values(qa_cvar_value_binding *binding, const char *name,
                                    cvar_state *state) {
  const char *values[3] = {state->value, state->reset_value, state->latched_value};
  size_t count = state->latched_value != NULL ? 3 : 2;
  for (size_t i = 0; i < count; i++) {
    char *error = binding->validate != NULL ? binding->validate(binding->ctx, values[i]) : NULL;
    if (error != NULL) {
      qa_error("%s: %s", name, error);
    }
  }
  return true;
}
int64_t qa_cvar_registry_bind_value(qa_cvar_registry *r, const char *name,
                                    qa_cvar_value_binding binding) {
  if (find_alias(r, name) != NULL) {
    char *canonical = qa_cvar_registry_canonical_name(r, name);
    qa_error("Bind the canonical cvar %s instead of alias %s",
             canonical != NULL ? canonical : "", name);
  }
  char *key = registry_key(r, name);
  cvar_state *state = key != NULL ? find_state_by_key(r, key) : NULL;
  if (state == NULL) qa_error("Cannot bind unregistered cvar %s", name);
  if (find_binding_by_key(r, key) != NULL)
    qa_error("Cvar %s already has a value binding", name);
  (void)validate_binding_values(&binding, name, state);
  if (r->binding_count == r->binding_capacity) {
    size_t cap = r->binding_capacity == 0 ? 8 : r->binding_capacity * 2;
    binding_entry *items = (binding_entry *)realloc(r->bindings, cap * sizeof *items);
    if (items == NULL) qa_error("out of memory");
    r->bindings = items;
    r->binding_capacity = cap;
  }
  int64_t id = r->next_binding_id++;
  r->bindings[r->binding_count].key = key;
  r->bindings[r->binding_count].id = id;
  r->bindings[r->binding_count].binding = binding;
  r->binding_count++;
  return id;
}
void qa_cvar_registry_unbind_value(qa_cvar_registry *r, int64_t binding) {
  for (size_t i = 0; i < r->binding_count; i++) {
    if (r->bindings[i].id == binding) {
      free(r->bindings[i].key);
      memmove(&r->bindings[i], &r->bindings[i + 1],
              (r->binding_count - i - 1) * sizeof *r->bindings);
      r->binding_count--;
      return;
    }
  }
}
static bool valid_bound_value(qa_cvar_registry *r, const char *name, const char *value) {
  char *key = registry_key(r, name);
  binding_entry *entry = key != NULL ? find_binding_by_key(r, key) : NULL;
  free(key);
  if (entry == NULL || entry->binding.validate == NULL) return true;
  char *error = entry->binding.validate(entry->binding.ctx, value);
  if (error == NULL) return true;
  registry_print_format(r, "%s: %s\n", name, error);
  free(error);
  return false;
}
void qa_cvar_registry_document(qa_cvar_registry *r, const char *name,
                               qa_command_documentation documentation) {
  r->mutation_revision++;
  qa_cvar_read found;
  if (!qa_cvar_registry_find(r, name, &found))
    qa_error("Cannot document unregistered cvar %s", name);
  qa_cvar_read_free(&found);
  char *key = registry_key(r, name);
  for (size_t i = 0; i < r->document_count; i++) {
    if (strcmp(r->documents[i].key, key) == 0) {
      r->documents[i].documentation = documentation;
      free(key);
      return;
    }
  }
  if (r->document_count == r->document_capacity) {
    size_t cap = r->document_capacity == 0 ? 8 : r->document_capacity * 2;
    document_entry *items = (document_entry *)realloc(r->documents, cap * sizeof *items);
    if (items == NULL) qa_error("out of memory");
    r->documents = items;
    r->document_capacity = cap;
  }
  r->documents[r->document_count].key = key;
  r->documents[r->document_count].documentation = documentation;
  r->document_count++;
}
const qa_command_documentation *qa_cvar_registry_documentation(qa_cvar_registry *r,
                                                               const char *name) {
  qa_cvar_read found;
  if (!qa_cvar_registry_find(r, name, &found)) return NULL;
  qa_cvar_read_free(&found);
  char *key = registry_key(r, name);
  const qa_command_documentation *result = NULL;
  for (size_t i = 0; i < r->document_count; i++)
    if (strcmp(r->documents[i].key, key) == 0) result = &r->documents[i].documentation;
  if (result == NULL) {
    alias_entry *alias = find_alias_by_key(r, key);
    if (alias != NULL) result = &alias->documentation;
  }
  free(key);
  return result;
}
char *qa_cvar_registry_variable_string(qa_cvar_registry *r, const char *name) {
  qa_cvar_read found;
  if (!qa_cvar_registry_find(r, name, &found)) return dup_text("");
  char *value = found.value;
  found.value = NULL;
  qa_cvar_read_free(&found);
  return value;
}
double qa_cvar_registry_variable_value(qa_cvar_registry *r, const char *name) {
  qa_cvar_read found;
  if (!qa_cvar_registry_find(r, name, &found)) return 0;
  double value = found.numeric_value;
  qa_cvar_read_free(&found);
  return value;
}
void qa_cvar_registry_set_server_active(qa_cvar_registry *r, bool active) {
  if (r->server_active != active) r->mutation_revision++;
  r->server_active = active;
}
void qa_cvar_registry_set_client_connected(qa_cvar_registry *r, bool connected) {
  if (r->client_connected != connected) r->mutation_revision++;
  r->client_connected = connected;
}
void qa_cvar_registry_set_server_high_characters(qa_cvar_registry *r, bool enabled) {
  if (r->high_characters != enabled) r->mutation_revision++;
  r->high_characters = enabled;
}
static bool console_has(qa_cvar_registry *r, const char *key) {
  for (size_t i = 0; i < r->console_count; i++)
    if (strcmp(r->console_variables[i], key) == 0) return true;
  return false;
}
static bool console_delete(qa_cvar_registry *r, const char *key) {
  for (size_t i = 0; i < r->console_count; i++) {
    if (strcmp(r->console_variables[i], key) == 0) {
      free(r->console_variables[i]);
      memmove(&r->console_variables[i], &r->console_variables[i + 1],
              (r->console_count - i - 1) * sizeof *r->console_variables);
      r->console_count--;
      return true;
    }
  }
  return false;
}
static bool console_add(qa_cvar_registry *r, const char *key) {
  if (console_has(r, key)) return true;
  if (r->console_count == r->console_capacity) {
    size_t cap = r->console_capacity == 0 ? 8 : r->console_capacity * 2;
    char **items = (char **)realloc(r->console_variables, cap * sizeof *items);
    if (items == NULL) return false;
    r->console_variables = items;
    r->console_capacity = cap;
  }
  r->console_variables[r->console_count] = dup_text(key);
  if (r->console_variables[r->console_count] == NULL) return false;
  r->console_count++;
  return true;
}
static bool allow_cheats(qa_cvar_registry *r) {
  if (r->options.cheats_allowed != NULL) {
    int result = r->options.cheats_allowed(r->options.cheats_allowed_ctx);
    if (result >= 0) return result != 0;
  }
  qa_cvar_read cheats;
  if (!qa_cvar_registry_find(r, "sv_cheats", &cheats)) return r->cheats_enabled;
  bool allowed = cheats.integer_value != 0;
  qa_cvar_read_free(&cheats);
  return allowed;
}
static cvar_state *create_state(qa_cvar_registry *r, const char *name, const char *key,
                                const char *default_value, int32_t flags) {
  cvar_state *state = (cvar_state *)calloc(1, sizeof *state);
  if (state == NULL) return NULL;
  state->index = (int64_t)r->index_count;
  state->name = dup_text(name);
  state->key = dup_text(key);
  state->value = dup_text(default_value);
  state->reset_value = dup_text(default_value);
  state->flags = flags;
  state->modified = true;
  state->modification_count = 1;
  compute_numbers(r, default_value, &state->numeric_value, &state->integer_value);
  state->next = r->first;
  if (state->name == NULL || state->key == NULL || state->value == NULL ||
      state->reset_value == NULL || !grow_index_array(r)) {
    state_free(state);
    return NULL;
  }
  r->indexes[r->index_count++] = state;
  r->first = state->index;
  return state;
}

bool qa_cvar_registry_register(qa_cvar_registry *r, const char *name_input,
                               const char *default_input, double flags_in,
                               qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  r->mutation_revision++;
  int32_t flags = qa_int32(flags_in);
  char *name = qa_source_command_text(name_input);
  if (name == NULL) return false;
  qa_command_dialect dialect = r->options.dialect;
  if (find_alias(r, name) != NULL) {
    reject_alias_info_flags(r, name, flags);
    bool found = qa_cvar_registry_get(r, name, out);
    free(name);
    return found;
  }
  char *default_value = qa_source_command_text(default_input);
  if (default_value == NULL) { free(name); return false; }
  if (!valid_bound_value(r, name, default_value)) {
    bool found = qa_cvar_registry_get(r, name, out);
    free(name);
    free(default_value);
    return found;
  }
  if (dialect == QA_COMMAND_DIALECT_Q3 && !valid_info(name)) {
    registry_print_format(r, "invalid cvar name string: %s\n", name);
    free(name);
    name = dup_text("BADNAME");
    if (name == NULL) { free(default_value); return false; }
  }
  if (qa_command_dialect_is_q2(dialect) && (flags & 6) != 0 && !valid_info(name)) {
    registry_print(r, "invalid info cvar name\n");
    free(name);
    free(default_value);
    return false;
  }
  char *key = registry_key(r, name);
  cvar_state *existing = key != NULL ? find_state_by_key(r, key) : NULL;
  if (existing != NULL) {
    if (qa_command_dialect_is_q1(dialect)) {
      if (console_delete(r, key)) {
        (void)set_owned(&existing->reset_value, default_value);
        existing->flags |= flags;
        if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD)
          propagate(r, existing, existing->value, true);
      } else {
        registry_print_format(r, "Can't register variable %s, allready defined\n", name);
      }
      bool ok = snapshot_or_false(existing, out);
      free(name); free(default_value); free(key);
      return ok;
    }
    if (dialect == QA_COMMAND_DIALECT_Q3) {
      if ((existing->flags & QA_CVAR_FLAG_USER_CREATED) != 0 &&
          (flags & QA_CVAR_FLAG_USER_CREATED) == 0 && default_value[0] != '\0') {
        existing->flags &= ~QA_CVAR_FLAG_USER_CREATED;
        (void)set_owned(&existing->reset_value, default_value);
        r->changed_flags |= flags;
      }
      if (existing->reset_value[0] == '\0')
        (void)set_owned(&existing->reset_value, default_value);
    }
    if (qa_command_dialect_is_q2(dialect) &&
        (existing->flags & QA_Q2_CVAR_FLAG_CUSTOM) != 0 &&
        (flags & QA_Q2_CVAR_FLAG_CUSTOM) == 0) {
      (void)set_owned(&existing->reset_value, default_value);
      existing->flags &= ~QA_Q2_CVAR_FLAG_CUSTOM;
      if ((flags & (QA_Q2_CVAR_FLAG_READ_ONLY | QA_Q2_CVAR_FLAG_NO_SET)) != 0 ||
          ((flags & QA_Q2_CVAR_FLAG_CHEAT) != 0 && !allow_cheats(r)) ||
          ((flags & (QA_Q2_CVAR_FLAG_USER_INFO | QA_Q2_CVAR_FLAG_SERVER_INFO)) != 0 &&
              !valid_info(existing->value))) {
        qa_cvar_snapshot ignored;
        (void)qa_cvar_registry_set(r, existing->name, default_value, true, &ignored);
        qa_cvar_read_free(&ignored);
      }
    }
    existing->flags |= flags;
    if (qa_command_dialect_is_q2(dialect) && (flags & Q2_NO_ARCHIVE) != 0)
      existing->flags &= ~QA_Q2_CVAR_FLAG_ARCHIVE;
    if (dialect == QA_COMMAND_DIALECT_Q3 && existing->latched_value != NULL) {
      char *value = existing->latched_value;
      existing->latched_value = NULL;
      qa_cvar_snapshot ignored;
      (void)qa_cvar_registry_set(r, existing->name, value, true, &ignored);
      qa_cvar_read_free(&ignored);
      free(value);
    }
    bool ok = snapshot_or_false(existing, out);
    free(name); free(default_value); free(key);
    return ok;
  }
  if (qa_command_dialect_is_q1(dialect) && r->options.command_exists != NULL &&
      r->options.command_exists(r->options.command_exists_ctx, name)) {
    registry_print_format(r, "Cvar_RegisterVariable: %s is a command\n", name);
    free(name); free(default_value); free(key);
    return false;
  }
  if (qa_command_dialect_is_q2(dialect) && (flags & 6) != 0 && !valid_info(default_value)) {
    registry_print(r, "invalid info cvar value\n");
    free(name); free(default_value); free(key);
    return false;
  }
  if (dialect == QA_COMMAND_DIALECT_Q3 && r->index_count == 1024)
    qa_range_error("MAX_CVARS");
  cvar_state *state = create_state(r, name, key != NULL ? key : name, default_value, flags);
  free(name); free(default_value); free(key);
  if (state == NULL) return false;
  if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD)
    propagate(r, state, state->value, true);
  return snapshot_or_false(state, out);
}

bool qa_cvar_registry_set(qa_cvar_registry *r, const char *name_input,
                          const char *value_input, bool force, qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  r->mutation_revision++;
  char *name = qa_source_command_text(name_input);
  if (name == NULL) return false;
  qa_command_dialect dialect = r->options.dialect;
  alias_entry *alias = find_alias(r, name);
  if (alias != NULL) {
    char *raw = qa_source_command_text(value_input);
    char *value = raw != NULL ? alias_write(r, alias, raw) : NULL;
    free(raw);
    if (value == NULL) { free(name); return false; }
    qa_cvar_snapshot ignored;
    bool ok = qa_cvar_registry_set(r, alias->target, value, force, &ignored);
    qa_cvar_read_free(&ignored);
    free(value);
    if (!ok) { free(name); return false; }
    bool found = qa_cvar_registry_get(r, name, out);
    free(name);
    return found;
  }
  if (dialect == QA_COMMAND_DIALECT_Q3 && !valid_info(name)) {
    registry_print_format(r, "invalid cvar name string: %s\n", name);
    free(name);
    name = dup_text("BADNAME");
    if (name == NULL) return false;
  }
  char *value = qa_source_command_text(value_input);
  if (value == NULL) { free(name); return false; }
  cvar_state *state = find_state(r, name);
  if (!valid_bound_value(r, name, value)) { free(name); free(value); return false; }
  if (state == NULL) {
    if (qa_command_dialect_is_q1(dialect)) {
      registry_print_format(r, "Cvar_Set: variable %s not found\n", name);
      free(name); free(value);
      return false;
    }
    bool ok = qa_cvar_registry_register(r, name, value,
        dialect == QA_COMMAND_DIALECT_Q3 && !force ? QA_CVAR_FLAG_USER_CREATED : 0, out);
    free(name); free(value);
    return ok;
  }
  if (qa_command_dialect_is_q1(dialect)) {
    propagate(r, state, value, strcmp(state->value, value) != 0);
    apply_value(r, state, value, false);
    bool ok = snapshot_or_false(state, out);
    free(name); free(value);
    return ok;
  }
  if (qa_command_dialect_is_q2(dialect)) {
    if ((state->flags & 6) != 0 && !valid_info(value)) {
      registry_print(r, "invalid info cvar value\n");
      bool ok = snapshot_or_false(state, out);
      free(name); free(value);
      return ok;
    }
    if (!force) {
      if ((state->flags & QA_Q2_CVAR_FLAG_READ_ONLY) != 0) {
        registry_print_format(r, "%s is read only.\n", name);
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
      if ((state->flags & QA_Q2_CVAR_FLAG_CHEAT) != 0 && !allow_cheats(r)) {
        registry_print_format(r, "%s is cheat protected.\n", name);
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
      if ((state->flags & QA_Q2_CVAR_FLAG_NO_SET) != 0) {
        registry_print_format(r, "%s is write protected.\n", name);
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
      if ((state->flags & QA_Q2_CVAR_FLAG_LATCH) != 0) {
        const char *current = state->latched_value != NULL ? state->latched_value : state->value;
        if (strcmp(value, current) == 0) {
          bool ok = snapshot_or_false(state, out);
          free(name); free(value);
          return ok;
        }
        free(state->latched_value);
        state->latched_value = NULL;
        if (r->server_active) {
          registry_print_format(r, "%s will be changed for next game.\n", name);
          state->latched_value = dup_text(value);
        } else {
          apply_value(r, state, value, false);
          game_directory(r, state);
        }
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
    } else {
      free(state->latched_value);
      state->latched_value = NULL;
    }
  } else {
    if (strcmp(value, state->value) == 0) {
      binding_changed(r, state->key, value);
      bool ok = snapshot_or_false(state, out);
      free(name); free(value);
      return ok;
    }
    r->changed_flags |= state->flags;
    if (!force) {
      if ((state->flags & QA_CVAR_FLAG_READ_ONLY) != 0) {
        registry_print_format(r, "%s is read only.\n", name);
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
      if ((state->flags & QA_CVAR_FLAG_INIT) != 0) {
        registry_print_format(r, "%s is write protected.\n", name);
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
      if ((state->flags & QA_CVAR_FLAG_LATCH) != 0) {
        if (state->latched_value != NULL && strcmp(state->latched_value, value) == 0) {
          bool ok = snapshot_or_false(state, out);
          free(name); free(value);
          return ok;
        }
        registry_print_format(r, "%s will be changed upon restarting.\n", name);
        (void)set_owned(&state->latched_value, value);
        state->modified = true;
        state->modification_count++;
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
      if ((state->flags & QA_CVAR_FLAG_CHEAT) != 0 && !allow_cheats(r)) {
        registry_print_format(r, "%s is cheat protected.\n", name);
        bool ok = snapshot_or_false(state, out);
        free(name); free(value);
        return ok;
      }
    } else {
      free(state->latched_value);
      state->latched_value = NULL;
    }
  }
  if (strcmp(state->value, value) != 0) {
    apply_value(r, state, value, true);
    if (qa_command_dialect_is_q2(dialect) && (state->flags & QA_CVAR_FLAG_USER_INFO) != 0)
      r->userinfo_dirty = true;
  } else {
    binding_changed(r, state->key, value);
  }
  bool ok = snapshot_or_false(state, out);
  free(name); free(value);
  return ok;
}
bool qa_cvar_registry_set_console(qa_cvar_registry *r, const char *name,
                                  const char *value, qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  r->mutation_revision++;
  alias_entry *alias = find_alias(r, name);
  if (alias != NULL) {
    char *raw = qa_source_command_text(value);
    char *converted = raw != NULL ? alias_write(r, alias, raw) : NULL;
    free(raw);
    if (converted == NULL) return false;
    qa_cvar_snapshot ignored;
    bool ok = qa_cvar_registry_set_console(r, alias->target, converted, &ignored);
    qa_cvar_read_free(&ignored);
    free(converted);
    if (!ok) return false;
    return qa_cvar_registry_get(r, name, out);
  }
  cvar_state *state = find_state(r, name);
  if (qa_command_dialect_is_q2(r->options.dialect) && state != NULL &&
      strcmp(state->value, value) == 0) {
    free(state->latched_value);
    state->latched_value = NULL;
    binding_changed(r, state->key, value);
    return snapshot_or_false(state, out);
  }
  /* set() increments the revision again, as the TS nested call does. */
  return qa_cvar_registry_set(r, name, value, false, out);
}

void qa_cvar_registry_set_command_flags(qa_cvar_registry *r, const char *name,
                                        const char *value, qa_cvar_command_flag_kind kind) {
  r->mutation_revision++;
  alias_entry *alias = find_alias(r, name);
  if (alias != NULL) {
    if (kind != QA_CVAR_COMMAND_FLAG_ARCHIVE) {
      registry_print_format(r, "Cvar alias %s requires an explicit protocol info-key mapping\n", name);
      return;
    }
    char *raw = qa_source_command_text(value);
    char *converted = raw != NULL ? alias_write(r, alias, raw) : NULL;
    free(raw);
    if (converted != NULL) {
      qa_cvar_registry_set_command_flags(r, alias->target, converted, kind);
      free(converted);
    }
    return;
  }
  char *checked = qa_source_command_text(value);
  bool valid = checked != NULL && valid_bound_value(r, name, checked);
  free(checked);
  if (!valid) return;
  bool q2 = qa_command_dialect_is_q2(r->options.dialect);
  int32_t flag = kind == QA_CVAR_COMMAND_FLAG_ARCHIVE
      ? (q2 ? QA_Q2_CVAR_FLAG_ARCHIVE : QA_CVAR_FLAG_ARCHIVE)
      : kind == QA_CVAR_COMMAND_FLAG_USERINFO
          ? (q2 ? QA_Q2_CVAR_FLAG_USER_INFO : QA_CVAR_FLAG_USER_INFO)
          : (q2 ? QA_Q2_CVAR_FLAG_SERVER_INFO : QA_CVAR_FLAG_SERVER_INFO);
  cvar_state *state = find_state(r, name);
  int32_t previous_flags = state != NULL ? state->flags : 0;
  if (kind != QA_CVAR_COMMAND_FLAG_ARCHIVE &&
      (!valid_info(name) || !valid_info(value) ||
       (q2 && (strlen(name) >= 64 || strlen(value) >= 64)))) {
    registry_print(r, "invalid info cvar name or value\n");
    return;
  }
  if (state == NULL) {
    qa_cvar_snapshot created;
    int32_t create_flags = flag | (q2 ? QA_Q2_CVAR_FLAG_CUSTOM
        : r->options.dialect == QA_COMMAND_DIALECT_Q3 ? QA_CVAR_FLAG_USER_CREATED : 0);
    if (!qa_cvar_registry_register(r, name, value, (double)create_flags, &created)) return;
    state = find_state(r, created.name);
    if (state == NULL) { qa_cvar_read_free(&created); return; }
    if (qa_command_dialect_is_q1(r->options.dialect)) (void)console_add(r, state->key);
    qa_cvar_read_free(&created);
  } else {
    qa_cvar_snapshot ignored;
    (void)qa_cvar_registry_set_console(r, name, value, &ignored);
    qa_cvar_read_free(&ignored);
    if (kind != QA_CVAR_COMMAND_FLAG_ARCHIVE &&
        (!valid_info(state->value) || (q2 && strlen(state->value) >= 64))) {
      registry_print(r, "invalid retained info cvar value\n");
      return;
    }
    if (kind != QA_CVAR_COMMAND_FLAG_ARCHIVE && q2)
      state->flags &= ~(QA_Q2_CVAR_FLAG_USER_INFO | QA_Q2_CVAR_FLAG_SERVER_INFO);
    if (kind != QA_CVAR_COMMAND_FLAG_ARCHIVE || !q2 || (state->flags & Q2_NO_ARCHIVE) == 0)
      state->flags |= flag;
  }
  if (q2 && (state->flags & Q2_NO_ARCHIVE) != 0) state->flags &= ~QA_Q2_CVAR_FLAG_ARCHIVE;
  if (kind != QA_CVAR_COMMAND_FLAG_ARCHIVE) {
    if (q2) {
      if (((previous_flags | state->flags) & QA_Q2_CVAR_FLAG_USER_INFO) != 0)
        r->userinfo_dirty = true;
    } else propagate(r, state, state->value, true);
  }
}
void qa_cvar_registry_reset_console(qa_cvar_registry *r, const char *name, bool all) {
  qa_cvar_read state;
  if (!qa_cvar_registry_find(r, name, &state)) return;
  if (all && (strcmp(name, "game") == 0 || strcmp(name, "fs_game") == 0)) {
    qa_cvar_read_free(&state);
    return;
  }
  if (all && (qa_command_dialect_is_q2(r->options.dialect)
          ? (state.flags & (QA_Q2_CVAR_FLAG_NO_SET | QA_Q2_CVAR_FLAG_READ_ONLY)) != 0
          : r->options.dialect == QA_COMMAND_DIALECT_Q3 &&
                (state.flags & (QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_INIT |
                                QA_CVAR_FLAG_NO_RESTART)) != 0)) {
    qa_cvar_read_free(&state);
    return;
  }
  qa_cvar_snapshot ignored;
  (void)qa_cvar_registry_set_console(r, name, state.reset_value, &ignored);
  qa_cvar_read_free(&ignored);
  qa_cvar_read_free(&state);
}
bool qa_cvar_registry_full_set(qa_cvar_registry *r, const char *name,
                               const char *value, double flags_in, qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  r->mutation_revision++;
  int32_t flags = qa_int32(flags_in);
  if (!qa_command_dialect_is_q2(r->options.dialect))
    qa_error("Cvar_FullSet belongs to Quake II");
  alias_entry *alias = find_alias(r, name);
  if (alias != NULL) {
    reject_alias_info_flags(r, name, flags);
    char *raw = qa_source_command_text(value);
    char *converted = raw != NULL ? alias_write(r, alias, raw) : NULL;
    free(raw);
    if (converted == NULL) return false;
    qa_cvar_snapshot ignored;
    bool ok = qa_cvar_registry_full_set(r, alias->target, converted, flags_in, &ignored);
    qa_cvar_read_free(&ignored);
    free(converted);
    if (!ok) return false;
    return qa_cvar_registry_get(r, name, out);
  }
  cvar_state *state = find_state(r, name);
  char *checked = qa_source_command_text(value);
  if (checked == NULL || !valid_bound_value(r, name, checked)) {
    free(checked);
    return state != NULL && snapshot_or_false(state, out);
  }
  free(checked);
  if (state == NULL) return qa_cvar_registry_register(r, name, value, flags_in, out);
  if ((state->flags & QA_CVAR_FLAG_USER_INFO) != 0) r->userinfo_dirty = true;
  char *text = qa_source_command_text(value);
  if (text == NULL) return false;
  apply_value(r, state, text, true);
  free(text);
  state->flags = flags;
  return snapshot_or_false(state, out);
}
bool qa_cvar_registry_set_value(qa_cvar_registry *r, const char *name, double value,
                                qa_cvar_snapshot *out) {
  char *text = qa_cvar_value_text(value, !qa_command_dialect_is_q1(r->options.dialect));
  if (text == NULL) { memset(out, 0, sizeof *out); return false; }
  size_t length = strlen(text);
  if (length >= 32) {
    if (qa_command_dialect_is_q1(r->options.dialect))
      qa_range_error("Cvar_SetValue overflows source val[32]");
    registry_print_format(r, "Com_sprintf: overflow of %zu in 32\n", length);
  }
  if (length > 31) text[31] = '\0';
  bool ok = qa_cvar_registry_set(r, name, text,
                                 r->options.dialect == QA_COMMAND_DIALECT_Q3, out);
  free(text);
  return ok;
}
bool qa_cvar_registry_stage(qa_cvar_registry *r, const char *name, const char *input,
                            qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  r->mutation_revision++;
  alias_entry *alias = find_alias(r, name);
  if (alias != NULL) {
    char *raw = qa_source_command_text(input);
    char *value = raw != NULL ? alias_write(r, alias, raw) : NULL;
    free(raw);
    if (value != NULL) {
      qa_cvar_snapshot ignored;
      (void)qa_cvar_registry_stage(r, alias->target, value, &ignored);
      qa_cvar_read_free(&ignored);
      free(value);
    }
    if (!qa_cvar_registry_get(r, name, out))
      qa_error("Cannot stage an unregistered cvar %s", name);
    return true;
  }
  cvar_state *state = find_state(r, name);
  char *value = qa_source_command_text(input);
  if (state == NULL) qa_error("Cannot stage an unregistered cvar %s", name);
  if (value == NULL || !valid_bound_value(r, name, value)) {
    free(value);
    return snapshot_or_false(state, out);
  }
  bool q2 = qa_command_dialect_is_q2(r->options.dialect);
  if (r->options.dialect == QA_COMMAND_DIALECT_Q3
          ? (state->flags & (QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_INIT)) != 0
          : q2 && (state->flags & QA_Q2_CVAR_FLAG_NO_SET) != 0)
    qa_error("Cannot stage a protected cvar %s", name);
  if (q2 && (state->flags & 6) != 0 && !valid_info(value))
    qa_error("Invalid staged info cvar %s", name);
  bool has_pending = strcmp(value, state->value) != 0;
  bool differs = has_pending != (state->latched_value != NULL) ||
      (has_pending && strcmp(state->latched_value, value) != 0);
  if (differs) {
    free(state->latched_value);
    state->latched_value = has_pending ? dup_text(value) : NULL;
    state->modified = true;
    state->modification_count++;
    if (r->options.dialect == QA_COMMAND_DIALECT_Q3) r->changed_flags |= state->flags;
  }
  free(value);
  return snapshot_or_false(state, out);
}
qa_cvar_snapshot *qa_cvar_registry_apply_latched(qa_cvar_registry *r,
                                                 const char *name_in, size_t *out_count) {
  r->mutation_revision++;
  char *name = NULL;
  char *name_key = NULL;
  if (name_in != NULL) {
    name = qa_cvar_registry_canonical_name(r, name_in);
    name_key = name != NULL ? registry_key(r, name) : NULL;
  }
  qa_cvar_snapshot *changed = NULL;
  size_t count = 0;
  size_t capacity = 0;
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    int64_t next = state->next;
    bool wanted = name_key == NULL || strcmp(state->key, name_key) == 0;
    if (wanted && state->latched_value != NULL) {
      char *value = state->latched_value;
      state->latched_value = NULL;
      apply_value(r, state, value, r->options.dialect == QA_COMMAND_DIALECT_Q3);
      free(value);
      if (qa_command_dialect_is_q2(r->options.dialect)) game_directory(r, state);
      if (count == capacity) {
        size_t cap = capacity == 0 ? 8 : capacity * 2;
        qa_cvar_snapshot *items = (qa_cvar_snapshot *)realloc(changed, cap * sizeof *items);
        if (items == NULL) break;
        changed = items;
        capacity = cap;
      }
      if (snapshot_state(state, &changed[count])) count++;
    }
    index = next;
  }
  free(name);
  free(name_key);
  *out_count = count;
  return changed;
}
bool qa_cvar_registry_reset(qa_cvar_registry *r, const char *name, bool force,
                            qa_cvar_snapshot *out) {
  qa_cvar_read state;
  if (!qa_cvar_registry_find(r, name, &state)) {
    memset(out, 0, sizeof *out);
    return false;
  }
  bool ok = qa_cvar_registry_set(r, name, state.reset_value, force, out);
  qa_cvar_read_free(&state);
  return ok;
}
void qa_cvar_registry_reset_all(qa_cvar_registry *r) {
  r->mutation_revision++;
  if (r->options.dialect != QA_COMMAND_DIALECT_Q3)
    qa_error("cvar_restart belongs to Quake III");
  int64_t previous = -1;
  for (;;) {
    int64_t index = previous < 0 ? r->first : state_at(r, previous)->next;
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    if ((state->flags & (QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_INIT |
                         QA_CVAR_FLAG_NO_RESTART)) != 0) {
      previous = index;
      continue;
    }
    if ((state->flags & QA_CVAR_FLAG_USER_CREATED) != 0) {
      int64_t next = state->next;
      if (previous < 0) r->first = next;
      else state_at(r, previous)->next = next;
      for (size_t i = 0; i < r->document_count; i++) {
        if (strcmp(r->documents[i].key, state->key) == 0) {
          free(r->documents[i].key);
          memmove(&r->documents[i], &r->documents[i + 1],
                  (r->document_count - i - 1) * sizeof *r->documents);
          r->document_count--;
          break;
        }
      }
      r->indexes[state->index] = NULL;
      state_free(state);
    } else {
      qa_cvar_snapshot ignored;
      char *reset = dup_text(state->reset_value);
      (void)qa_cvar_registry_set(r, state->name, reset != NULL ? reset : "", true, &ignored);
      qa_cvar_read_free(&ignored);
      free(reset);
      previous = index;
    }
  }
}
void qa_cvar_registry_set_cheats_enabled(qa_cvar_registry *r, bool enabled) {
  r->mutation_revision++;
  r->cheats_enabled = enabled;
  if (enabled || r->options.dialect != QA_COMMAND_DIALECT_Q3) return;
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    int64_t next = state->next;
    if ((state->flags & QA_CVAR_FLAG_CHEAT) != 0) {
      free(state->latched_value);
      state->latched_value = NULL;
      qa_cvar_snapshot ignored;
      char *reset = dup_text(state->reset_value);
      (void)qa_cvar_registry_set(r, state->name, reset != NULL ? reset : "", true, &ignored);
      qa_cvar_read_free(&ignored);
      free(reset);
    }
    index = next;
  }
}

qa_cvar_snapshot *qa_cvar_registry_canonical_snapshots(qa_cvar_registry *r,
                                                       double flags_in, size_t *out_count) {
  int32_t flags = qa_int32(flags_in);
  qa_cvar_snapshot *values = NULL;
  size_t count = 0;
  size_t capacity = 0;
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    int64_t next = state->next;
    if (flags == 0 || (state->flags & flags) != 0) {
      if (count == capacity) {
        size_t cap = capacity == 0 ? 8 : capacity * 2;
        qa_cvar_snapshot *items = (qa_cvar_snapshot *)realloc(values, cap * sizeof *items);
        if (items == NULL) break;
        values = items;
        capacity = cap;
      }
      if (snapshot_state(state, &values[count])) count++;
    }
    index = next;
  }
  *out_count = count;
  return values;
}
qa_cvar_snapshot *qa_cvar_registry_snapshots(qa_cvar_registry *r, double flags_in,
                                             size_t *out_count) {
  int32_t flags = qa_int32(flags_in);
  size_t count = 0;
  qa_cvar_snapshot *values = qa_cvar_registry_canonical_snapshots(r, flags_in, &count);
  size_t capacity = count;
  for (size_t i = 0; i < r->alias_count; i++) {
    qa_cvar_snapshot value;
    if (!qa_cvar_registry_get(r, r->aliases[i].name, &value)) continue;
    if (flags != 0 && (value.flags & flags) == 0) {
      qa_cvar_read_free(&value);
      continue;
    }
    if (count == capacity) {
      size_t cap = capacity == 0 ? 8 : capacity * 2;
      qa_cvar_snapshot *items = (qa_cvar_snapshot *)realloc(values, cap * sizeof *items);
      if (items == NULL) { qa_cvar_read_free(&value); break; }
      values = items;
      capacity = cap;
    }
    values[count++] = value;
  }
  *out_count = count;
  return values;
}
char *qa_cvar_registry_complete(qa_cvar_registry *r, const char *partial_input) {
  char *partial = qa_source_command_text(partial_input);
  if (partial == NULL || partial[0] == '\0') { free(partial); return NULL; }
  if (r->options.dialect != QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    qa_cvar_read exact;
    if (qa_cvar_registry_find(r, partial, &exact)) {
      char *name = exact.name;
      exact.name = NULL;
      qa_cvar_read_free(&exact);
      free(partial);
      return name;
    }
  }
  char *partial_key = registry_key(r, partial);
  size_t count = 0;
  qa_cvar_snapshot *values = qa_cvar_registry_snapshots(r, 0, &count);
  char *result = NULL;
  for (size_t i = 0; i < count && result == NULL; i++) {
    char *candidate_key = registry_key(r, values[i].name);
    if (candidate_key != NULL && partial_key != NULL &&
        strncmp(candidate_key, partial_key, strlen(partial_key)) == 0)
      result = dup_text(values[i].name);
    free(candidate_key);
  }
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&values[i]);
  free(values);
  free(partial);
  free(partial_key);
  return result;
}
char *qa_cvar_registry_info_string(qa_cvar_registry *r, double flags_in,
                                   int64_t maximum_length) {
  int32_t flags = qa_int32(flags_in);
  if (maximum_length < 0)
    maximum_length = r->options.dialect == QA_COMMAND_DIALECT_Q3 ? 1024 : 512;
  char *info = dup_text("");
  if (info == NULL) return NULL;
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    int64_t next = state->next;
    if ((state->flags & flags) != 0 &&
        !(qa_command_dialect_is_q2(r->options.dialect) &&
          (state->flags & QA_Q2_CVAR_FLAG_PRIVATE) != 0)) {
      qa_info_options options;
      options.dialect = r->options.dialect;
      options.maximum_length = maximum_length;
      options.target = (flags & QA_CVAR_FLAG_USER_INFO) != 0
          ? QA_CVAR_INFO_CLIENT_USERINFO : QA_CVAR_INFO_SERVER_INFO;
      options.server_high_characters = r->high_characters;
      options.print = info_print;
      options.print_ctx = r;
      char *next_info = qa_set_info_value(info, state->name, state->value, &options);
      if (next_info == NULL) { free(info); return NULL; }
      free(info);
      info = next_info;
    }
    index = next;
  }
  return info;
}
char *qa_cvar_registry_propagated_info(qa_cvar_registry *r, qa_cvar_info_target target) {
  return dup_text(target == QA_CVAR_INFO_CLIENT_USERINFO ? r->client_info : r->server_info);
}
static bool archive_includes(qa_cvar_registry *r, cvar_state *state,
                             qa_cvar_include_fn include, void *include_ctx) {
  if (include != NULL && !include(include_ctx, state->name)) return false;
  if (qa_command_dialect_is_q2(r->options.dialect) && (state->flags & Q2_NO_ARCHIVE) != 0)
    return false;
  if ((state->flags & QA_CVAR_FLAG_ARCHIVE) == 0) return false;
  if (r->options.dialect == QA_COMMAND_DIALECT_Q3) {
    char *folded = qa_ascii_fold(state->name);
    bool is_cdkey = folded != NULL && strcmp(folded, "cl_cdkey") == 0;
    free(folded);
    if (is_cdkey) return false;
  }
  return true;
}
static const char *archive_value(qa_cvar_registry *r, cvar_state *state) {
  return r->options.dialect == QA_COMMAND_DIALECT_Q3 && state->latched_value != NULL
      ? state->latched_value : state->value;
}
qa_cvar_archive_entry *qa_cvar_registry_archive_entries(qa_cvar_registry *r,
                                                        qa_cvar_include_fn include,
                                                        void *include_ctx, size_t *out_count) {
  qa_cvar_archive_entry *entries = NULL;
  size_t count = 0;
  size_t capacity = 0;
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    int64_t next = state->next;
    if (archive_includes(r, state, include, include_ctx)) {
      if (count == capacity) {
        size_t cap = capacity == 0 ? 8 : capacity * 2;
        qa_cvar_archive_entry *items =
            (qa_cvar_archive_entry *)realloc(entries, cap * sizeof *items);
        if (items == NULL) break;
        entries = items;
        capacity = cap;
      }
      entries[count].name = dup_text(state->name);
      entries[count].value = dup_text(archive_value(r, state));
      if (entries[count].name != NULL && entries[count].value != NULL) count++;
      else {
        free(entries[count].name);
        free(entries[count].value);
      }
    }
    index = next;
  }
  *out_count = count;
  return entries;
}
void qa_cvar_registry_apply_archive(qa_cvar_registry *r,
                                    const qa_cvar_archive_entry *entries, size_t count) {
  for (size_t i = 0; i < count; i++)
    qa_cvar_registry_set_command_flags(r, entries[i].name, entries[i].value,
                                       QA_CVAR_COMMAND_FLAG_ARCHIVE);
}
char **qa_cvar_registry_archive_commands(qa_cvar_registry *r,
                                         qa_cvar_include_fn include, void *include_ctx,
                                         size_t *out_count) {
  char **commands = NULL;
  size_t count = 0;
  size_t capacity = 0;
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    int64_t next = state->next;
    if (archive_includes(r, state, include, include_ctx)) {
      bool q2 = qa_command_dialect_is_q2(r->options.dialect);
      const char *prefix = r->options.dialect == QA_COMMAND_DIALECT_Q3 ||
              console_has(r, state->key) ||
              (q2 && (state->flags & QA_Q2_CVAR_FLAG_CUSTOM) != 0)
          ? "seta " : q2 ? "set " : "";
      text_buffer command = {NULL, 0, 0};
      (void)text_add(&command, prefix);
      (void)text_add(&command, state->name);
      (void)text_add(&command, " \"");
      (void)text_add(&command, archive_value(r, state));
      (void)text_add(&command, "\"");
      if (count == capacity) {
        size_t cap = capacity == 0 ? 8 : capacity * 2;
        char **items = (char **)realloc(commands, cap * sizeof *items);
        if (items == NULL) { free(command.data); break; }
        commands = items;
        capacity = cap;
      }
      commands[count++] = command.data != NULL ? command.data : dup_text("");
    }
    index = next;
  }
  *out_count = count;
  return commands;
}
void qa_cvar_registry_write_variables(qa_cvar_registry *r, qa_cvar_write_fn write, void *ctx) {
  size_t count = 0;
  char **commands = qa_cvar_registry_archive_commands(r, NULL, NULL, &count);
  bool q1 = qa_command_dialect_is_q1(r->options.dialect);
  for (size_t i = 0; i < count; i++) {
    text_buffer line = {NULL, 0, 0};
    (void)text_add(&line, commands[i]);
    (void)text_add(&line, "\n");
    if (!q1 && line.length >= 1024)
      registry_print_format(r, "Com_sprintf: overflow of %zu in 1024\n", line.length);
    if (!q1 && line.length > 1023) line.data[1023] = '\0';
    if (write != NULL && line.data != NULL) write(ctx, line.data);
    free(line.data);
    free(commands[i]);
  }
  free(commands);
}
double qa_cvar_registry_take_modified_flags(qa_cvar_registry *r) {
  int32_t flags = r->changed_flags;
  if (flags != 0) r->mutation_revision++;
  r->changed_flags = 0;
  return (double)flags;
}
void qa_cvar_registry_mark_modified_flags(qa_cvar_registry *r, double flags_in) {
  int32_t flags = qa_int32(flags_in);
  if ((r->changed_flags | flags) != r->changed_flags) r->mutation_revision++;
  r->changed_flags |= flags;
}
void qa_cvar_registry_clear_modified_flags(qa_cvar_registry *r, double flags_in) {
  int32_t flags = qa_int32(flags_in);
  if ((r->changed_flags & flags) != 0) r->mutation_revision++;
  r->changed_flags &= ~flags;
}
void qa_cvar_registry_clear_userinfo_modified(qa_cvar_registry *r) {
  if (r->userinfo_dirty) r->mutation_revision++;
  r->userinfo_dirty = false;
}
void qa_cvar_registry_add_flags(qa_cvar_registry *r, const char *name, double flags_in) {
  r->mutation_revision++;
  int32_t flags = qa_int32(flags_in);
  if (find_alias(r, name) != NULL) reject_alias_info_flags(r, name, flags);
  char *canonical = qa_cvar_registry_canonical_name(r, name);
  cvar_state *state = canonical != NULL ? find_state(r, canonical) : NULL;
  free(canonical);
  if (state != NULL) state->flags |= flags;
}
void qa_cvar_registry_clear_modified(qa_cvar_registry *r, const char *name) {
  char *canonical = qa_cvar_registry_canonical_name(r, name);
  cvar_state *state = canonical != NULL ? find_state(r, canonical) : NULL;
  free(canonical);
  if (state != NULL && state->modified) {
    r->mutation_revision++;
    state->modified = false;
  }
}
qa_cvar_effect *qa_cvar_registry_take_effects(qa_cvar_registry *r, size_t *out_count) {
  qa_cvar_effect *effects = r->effects;
  *out_count = r->effect_count;
  if (r->effect_count != 0) r->mutation_revision++;
  r->effects = NULL;
  r->effect_count = 0;
  r->effect_capacity = 0;
  return effects;
}

/* --- VM cvars --- */
static void require_q3_vm(qa_cvar_registry *r, const char *message) {
  if (r->options.dialect != QA_COMMAND_DIALECT_Q3) qa_error("%s", message);
}
bool qa_cvar_registry_create_vm(qa_cvar_registry *r, qa_vm_cvar *out) {
  require_q3_vm(r, "VM cvar mirrors belong to Quake III");
  qa_vm_cvar_init(out, r);
  return out->text != NULL;
}
void qa_vm_cvar_init(qa_vm_cvar *vm, qa_cvar_registry *registry) {
  memset(vm, 0, sizeof *vm);
  vm->registry = registry;
  vm->text = dup_text("");
}
void qa_vm_cvar_destroy(qa_vm_cvar *vm) {
  free(vm->text);
  vm->text = NULL;
}
int64_t qa_cvar_registry_bind_vm(qa_cvar_registry *r, const char *name,
                                 const char *default_value, double flags_in) {
  require_q3_vm(r, "VM cvar handles belong to Quake III");
  int32_t flags = qa_int32(flags_in);
  alias_entry *alias = find_alias(r, name);
  if (alias != NULL && !alias->conversion.is_identity) {
    reject_alias_info_flags(r, name, flags);
    char *key = registry_key(r, name);
    for (size_t i = 0; i < r->alias_handle_count; i++) {
      char *existing_key = registry_key(r, r->alias_handles[i].name);
      bool same = existing_key != NULL && key != NULL &&
                  strcmp(existing_key, key) == 0;
      free(existing_key);
      if (same) { free(key); return r->alias_handles[i].handle; }
    }
    free(key);
    if (r->index_count == 1024) qa_range_error("MAX_CVARS");
    int64_t handle = (int64_t)r->index_count;
    r->mutation_revision++;
    if (!grow_index_array(r)) qa_error("out of memory");
    r->indexes[r->index_count++] = NULL;
    if (r->alias_handle_count == r->alias_handle_capacity) {
      size_t cap = r->alias_handle_capacity == 0 ? 8 : r->alias_handle_capacity * 2;
      alias_handle *items = (alias_handle *)realloc(r->alias_handles, cap * sizeof *items);
      if (items == NULL) qa_error("out of memory");
      r->alias_handles = items;
      r->alias_handle_capacity = cap;
    }
    r->alias_handles[r->alias_handle_count].handle = handle;
    r->alias_handles[r->alias_handle_count].name = dup_text(alias->name);
    r->alias_handle_count++;
    return handle;
  }
  qa_cvar_snapshot registered;
  bool ok = qa_cvar_registry_register(r, name, default_value, flags_in, &registered);
  cvar_state *state = NULL;
  if (ok) {
    char *canonical = qa_cvar_registry_canonical_name(r, registered.name);
    state = canonical != NULL ? find_state(r, canonical) : NULL;
    free(canonical);
  }
  qa_cvar_read_free(&registered);
  if (state == NULL) qa_error("VM cvar registration failed");
  return state->index;
}
bool qa_cvar_registry_read_vm(qa_cvar_registry *r, double handle_d, qa_cvar_snapshot *out) {
  memset(out, 0, sizeof *out);
  if (!(handle_d == trunc(handle_d)) || handle_d < 0 || handle_d >= (double)r->index_count)
    qa_range_error("Cvar_Update: handle out of range");
  int64_t handle = (int64_t)handle_d;
  for (size_t i = 0; i < r->alias_handle_count; i++)
    if (r->alias_handles[i].handle == handle)
      return qa_cvar_registry_get(r, r->alias_handles[i].name, out);
  cvar_state *state = state_at(r, handle);
  return state != NULL && snapshot_state(state, out);
}
void qa_vm_cvar_register(qa_vm_cvar *vm, const char *name, const char *default_value,
                         double flags) {
  vm->handle = qa_cvar_registry_bind_vm(vm->registry, name, default_value, flags);
  vm->has_handle = true;
  vm->count = -1;
  qa_vm_cvar_update(vm);
}
void qa_vm_cvar_update(qa_vm_cvar *vm) {
  if (!vm->has_handle) return;
  qa_cvar_snapshot source;
  if (!qa_cvar_registry_read_vm(vm->registry, (double)vm->handle, &source)) return;
  if (source.modification_count == vm->count) { qa_cvar_read_free(&source); return; }
  vm->count = source.modification_count;
  if (strlen(source.value) > 255)
    qa_range_error("Cvar_Update: value exceeds MAX_CVAR_VALUE_STRING");
  (void)set_owned(&vm->text, source.value);
  vm->numeric = source.numeric_value;
  vm->integer = source.integer_value;
  qa_cvar_read_free(&source);
}
void qa_vm_cvar_write_integer(qa_vm_cvar *vm, double value) {
  if (!(value == trunc(value) && fabs(value) <= 9007199254740991.0))
    qa_range_error("VM cvar integer write requires a safe integer");
  vm->integer = (double)qa_int32(value);
}
bool qa_cvar_registry_register_vm(qa_cvar_registry *r, const char *name,
                                  const char *default_value, double flags, qa_vm_cvar *out) {
  if (!qa_cvar_registry_create_vm(r, out)) return false;
  qa_vm_cvar_register(out, name, default_value, flags);
  return true;
}

/* --- Save state capture / restore --- */
static const char *dialect_name(qa_command_dialect dialect) {
  switch (dialect) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE: return "q1-netquake";
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD: return "q1-quakeworld";
    case QA_COMMAND_DIALECT_Q2_CLASSIC: return "q2-classic";
    case QA_COMMAND_DIALECT_Q2_RERELEASE: return "q2-rerelease";
    case QA_COMMAND_DIALECT_Q3: return "q3";
  }
  return "q3";
}
static bool value_set(qa_save_value *object, const char *key, qa_save_value *child) {
  if (child == NULL || !qa_save_value_set(object, key, child)) {
    qa_save_value_free(child);
    return false;
  }
  return true;
}
static qa_save_value *snapshot_registry_state(qa_cvar_registry *r) {
  qa_save_value *root = qa_save_value_object();
  qa_save_value *variables = qa_save_value_array();
  qa_save_value *order = qa_save_value_array();
  qa_save_value *console = qa_save_value_array();
  if (root == NULL || variables == NULL || order == NULL || console == NULL) goto oom;
  for (size_t i = 0; i < r->index_count; i++) {
    cvar_state *state = r->indexes[i];
    if (state == NULL) {
      if (!qa_save_value_push(variables, qa_save_value_null())) goto oom;
      continue;
    }
    qa_save_value *entry = qa_save_value_object();
    if (entry == NULL ||
        !value_set(entry, "name", qa_save_value_string(state->name)) ||
        !value_set(entry, "value", qa_save_value_string(state->value)) ||
        !value_set(entry, "resetValue", qa_save_value_string(state->reset_value)) ||
        !value_set(entry, "latchedValue",
            state->latched_value != NULL ? qa_save_value_string(state->latched_value)
                                         : qa_save_value_null()) ||
        !value_set(entry, "flags", qa_save_value_number((double)state->flags)) ||
        !value_set(entry, "modified", qa_save_value_boolean(state->modified)) ||
        !value_set(entry, "modificationCount",
            qa_save_value_number(state->modification_count)) ||
        !value_set(entry, "numericValue", qa_save_value_number(state->numeric_value)) ||
        !value_set(entry, "integerValue", qa_save_value_number(state->integer_value)) ||
        !qa_save_value_push(variables, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  for (int64_t index = r->first; index >= 0;) {
    cvar_state *state = state_at(r, index);
    if (state == NULL) break;
    if (!qa_save_value_push(order, qa_save_value_string(state->name))) goto oom;
    index = state->next;
  }
  for (size_t i = 0; i < r->console_count; i++)
    if (!qa_save_value_push(console, qa_save_value_string(r->console_variables[i])))
      goto oom;
  if (!value_set(root, "variables", variables) ||
      !value_set(root, "order", order) ||
      !value_set(root, "changedFlags", qa_save_value_number((double)r->changed_flags)) ||
      !value_set(root, "cheatsEnabled", qa_save_value_boolean(r->cheats_enabled)) ||
      !value_set(root, "serverActive", qa_save_value_boolean(r->server_active)) ||
      !value_set(root, "clientConnected", qa_save_value_boolean(r->client_connected)) ||
      !value_set(root, "highCharacters", qa_save_value_boolean(r->high_characters)) ||
      !value_set(root, "clientInfo", qa_save_value_string(r->client_info)) ||
      !value_set(root, "serverInfo", qa_save_value_string(r->server_info)) ||
      !value_set(root, "userinfoDirty", qa_save_value_boolean(r->userinfo_dirty)) ||
      !value_set(root, "consoleVariables", console)) goto oom;
  if (r->alias_handle_count != 0) {
    qa_save_value *handles = qa_save_value_array();
    if (handles == NULL) goto oom;
    for (size_t i = 0; i < r->alias_handle_count; i++) {
      qa_save_value *entry = qa_save_value_object();
      if (entry == NULL ||
          !value_set(entry, "handle",
              qa_save_value_number((double)r->alias_handles[i].handle)) ||
          !value_set(entry, "name", qa_save_value_string(r->alias_handles[i].name)) ||
          !qa_save_value_push(handles, entry)) {
        qa_save_value_free(entry);
        qa_save_value_free(handles);
        goto oom;
      }
    }
    if (!value_set(root, "aliasHandles", handles)) goto oom;
  }
  return root;
oom:
  qa_save_value_free(root);
  if (variables != NULL && (root == NULL || qa_save_value_get(root, "variables") == NULL))
    qa_save_value_free(variables);
  if (order != NULL && (root == NULL || qa_save_value_get(root, "order") == NULL))
    qa_save_value_free(order);
  if (console != NULL && (root == NULL || qa_save_value_get(root, "consoleVariables") == NULL))
    qa_save_value_free(console);
  return NULL;
}
static qa_save_value *capture_registry_state(qa_cvar_registry *r) {
  if (r->effect_count != 0) qa_error("Cvar save requires drained effects");
  return snapshot_registry_state(r);
}
static qa_save_value *with_dialect(qa_cvar_registry *r, qa_save_value *state) {
  if (state == NULL) return NULL;
  qa_save_value *root = qa_save_value_object();
  if (root == NULL || !value_set(root, "dialect", qa_save_value_string(dialect_name(r->options.dialect)))) {
    qa_save_value_free(root);
    qa_save_value_free(state);
    return NULL;
  }
  /* Move the state entries into the root (TS object spread). */
  for (size_t i = 0; i < state->entry_count; i++) {
    qa_save_entry *entries = (qa_save_entry *)realloc(
        root->entries, (root->entry_count + 1) * sizeof *entries);
    if (entries == NULL) {
      qa_save_value_free(root);
      qa_save_value_free(state);
      return NULL;
    }
    root->entries = entries;
    root->entries[root->entry_count] = state->entries[i];
    root->entry_count++;
  }
  free(state->entries);
  free(state);
  return root;
}
qa_save_value *qa_cvar_registry_capture_save_state(qa_cvar_registry *r) {
  return with_dialect(r, capture_registry_state(r));
}
qa_save_value *qa_cvar_registry_capture_world_transfer_state(qa_cvar_registry *r) {
  return with_dialect(r, snapshot_registry_state(r));
}
qa_save_value *qa_cvar_registry_capture_quakec_state(qa_cvar_registry *r) {
  if (!qa_command_dialect_is_q1(r->options.dialect))
    qa_error("QC cvar save requires a Q1 registry");
  return capture_registry_state(r);
}

typedef struct prepared_restore {
  cvar_state **indexes;
  size_t index_count;
  int64_t first;
  alias_handle *alias_handles;
  size_t alias_handle_count;
  int32_t changed_flags;
  bool cheats_enabled, server_active, client_connected, high_characters;
  char *client_info;
  char *server_info;
  bool userinfo_dirty;
  char **console_variables;
  size_t console_count;
} prepared_restore;
static void prepared_free(prepared_restore *p) {
  for (size_t i = 0; i < p->index_count; i++) state_free(p->indexes[i]);
  free(p->indexes);
  for (size_t i = 0; i < p->alias_handle_count; i++) free(p->alias_handles[i].name);
  free(p->alias_handles);
  free(p->client_info);
  free(p->server_info);
  for (size_t i = 0; i < p->console_count; i++) free(p->console_variables[i]);
  free(p->console_variables);
  free(p);
}
static qa_save_reader save_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static qa_save_reader save_item(const qa_save_reader *reader, size_t index) {
  qa_save_reader out;
  if (!qa_save_reader_item(reader, index, &out))
    qa_save_reader_fail(reader, "out of memory");
  return out;
}
static alias_entry *find_alias_in(alias_entry *aliases, size_t count, const char *key) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(aliases[i].key, key) == 0) return &aliases[i];
  return NULL;
}
/* Mirrors prepareRegistryRestore: validates fully, then the caller
 * applies with an effects list. `aliases` is the alias array to
 * validate against (default the registry's own). */
static prepared_restore *prepare_registry_restore(qa_cvar_registry *r,
                                                  const qa_save_value *value,
                                                  alias_entry *aliases,
                                                  size_t alias_count) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "cvars"))
    qa_range_error("out of memory");
  prepared_restore *prepared = (prepared_restore *)calloc(1, sizeof *prepared);
  if (prepared == NULL) qa_range_error("out of memory");
  prepared->first = -1;
  qa_save_reader variables = save_field(&reader, "variables");
  size_t variable_count = qa_save_reader_list_count(&variables);
  prepared->indexes = variable_count > 0
      ? (cvar_state **)calloc(variable_count, sizeof *prepared->indexes) : NULL;
  prepared->index_count = variable_count;
  size_t state_count = 0;
  for (size_t index = 0; index < variable_count; index++) {
    qa_save_reader entry = save_item(&variables, index);
    if (qa_save_reader_is_null(&entry)) { qa_save_reader_destroy(&entry); continue; }
    qa_save_reader name_r = save_field(&entry, "name");
    qa_save_reader value_r = save_field(&entry, "value");
    qa_save_reader reset_r = save_field(&entry, "resetValue");
    qa_save_reader latched_r = save_field(&entry, "latchedValue");
    qa_save_reader flags_r = save_field(&entry, "flags");
    qa_save_reader modified_r = save_field(&entry, "modified");
    qa_save_reader count_r = save_field(&entry, "modificationCount");
    qa_save_reader numeric_r = save_field(&entry, "numericValue");
    qa_save_reader integer_r = save_field(&entry, "integerValue");
    const char *name = qa_save_reader_string(&name_r);
    cvar_state *state = (cvar_state *)calloc(1, sizeof *state);
    if (state == NULL) qa_range_error("out of memory");
    state->index = (int64_t)index;
    state->name = dup_text(name);
    state->value = dup_text(qa_save_reader_string(&value_r));
    state->reset_value = dup_text(qa_save_reader_string(&reset_r));
    state->latched_value = qa_save_reader_is_null(&latched_r)
        ? NULL : dup_text(qa_save_reader_string(&latched_r));
    state->flags = qa_int32(qa_save_reader_integer_min(&flags_r, 0));
    state->modified = qa_save_reader_boolean(&modified_r);
    state->modification_count = qa_save_reader_integer_min(&count_r, 0);
    state->numeric_value = qa_save_reader_number(&numeric_r);
    state->integer_value = qa_save_reader_integer(&integer_r);
    state->next = -1;
    state->key = registry_key(r, state->name);
    qa_save_reader_destroy(&name_r);
    qa_save_reader_destroy(&value_r);
    qa_save_reader_destroy(&reset_r);
    qa_save_reader_destroy(&latched_r);
    qa_save_reader_destroy(&flags_r);
    qa_save_reader_destroy(&modified_r);
    qa_save_reader_destroy(&count_r);
    qa_save_reader_destroy(&numeric_r);
    qa_save_reader_destroy(&integer_r);
    qa_save_reader_destroy(&entry);
    if (find_alias_in(aliases, alias_count, state->key) != NULL)
      qa_save_reader_fail(&reader, "saved cvar %s conflicts with an alias", state->name);
    for (size_t i = 0; i < index; i++)
      if (prepared->indexes[i] != NULL && strcmp(prepared->indexes[i]->key, state->key) == 0)
        qa_save_reader_fail(&reader, "duplicate cvar");
    prepared->indexes[index] = state;
    state_count++;
  }
  qa_save_reader_destroy(&variables);
  qa_save_reader order = save_field(&reader, "order");
  size_t order_count = qa_save_reader_list_count(&order);
  char **order_keys = order_count > 0 ? (char **)calloc(order_count, sizeof *order_keys) : NULL;
  size_t distinct = 0;
  for (size_t i = 0; i < order_count; i++) {
    qa_save_reader item = save_item(&order, i);
    order_keys[i] = registry_key(r, qa_save_reader_string(&item));
    qa_save_reader_destroy(&item);
    bool seen = false;
    for (size_t j = 0; j < i; j++)
      if (strcmp(order_keys[j], order_keys[i]) == 0) seen = true;
    if (!seen) distinct++;
  }
  if (distinct != state_count || order_count != state_count)
    qa_save_reader_fail(&reader, "invalid cvar order");
  int64_t first = -1;
  for (size_t n = order_count; n-- > 0;) {
    cvar_state *found = NULL;
    for (size_t i = 0; i < variable_count; i++)
      if (prepared->indexes[i] != NULL && strcmp(prepared->indexes[i]->key, order_keys[n]) == 0)
        found = prepared->indexes[i];
    if (found == NULL) qa_save_reader_fail(&reader, "unknown ordered cvar");
    found->next = first;
    first = found->index;
  }
  prepared->first = first;
  for (size_t i = 0; i < order_count; i++) free(order_keys[i]);
  free(order_keys);
  qa_save_reader_destroy(&order);
  qa_save_reader changed = save_field(&reader, "changedFlags");
  prepared->changed_flags = qa_int32(qa_save_reader_integer_min(&changed, 0));
  qa_save_reader_destroy(&changed);
  qa_save_reader cheats = save_field(&reader, "cheatsEnabled");
  prepared->cheats_enabled = qa_save_reader_boolean(&cheats);
  qa_save_reader_destroy(&cheats);
  qa_save_reader server = save_field(&reader, "serverActive");
  prepared->server_active = qa_save_reader_boolean(&server);
  qa_save_reader_destroy(&server);
  qa_save_reader client = save_field(&reader, "clientConnected");
  prepared->client_connected = qa_save_reader_boolean(&client);
  qa_save_reader_destroy(&client);
  qa_save_reader high = save_field(&reader, "highCharacters");
  prepared->high_characters = qa_save_reader_boolean(&high);
  qa_save_reader_destroy(&high);
  qa_save_reader client_info = save_field(&reader, "clientInfo");
  prepared->client_info = dup_text(qa_save_reader_string(&client_info));
  qa_save_reader_destroy(&client_info);
  qa_save_reader server_info = save_field(&reader, "serverInfo");
  prepared->server_info = dup_text(qa_save_reader_string(&server_info));
  qa_save_reader_destroy(&server_info);
  qa_save_reader dirty = save_field(&reader, "userinfoDirty");
  prepared->userinfo_dirty = qa_save_reader_boolean(&dirty);
  qa_save_reader_destroy(&dirty);
  qa_save_reader console = save_field(&reader, "consoleVariables");
  size_t console_count = qa_save_reader_list_count(&console);
  prepared->console_variables =
      console_count > 0 ? (char **)calloc(console_count, sizeof(char *)) : NULL;
  prepared->console_count = console_count;
  for (size_t i = 0; i < console_count; i++) {
    qa_save_reader item = save_item(&console, i);
    prepared->console_variables[i] = dup_text(qa_save_reader_string(&item));
    qa_save_reader_destroy(&item);
    for (size_t j = 0; j < i; j++)
      if (strcmp(prepared->console_variables[j], prepared->console_variables[i]) == 0)
        qa_save_reader_fail(&reader, "duplicate console variable");
  }
  qa_save_reader_destroy(&console);
  qa_save_reader handles = save_field(&reader, "aliasHandles");
  if (!qa_save_reader_is_undefined(&handles)) {
    size_t handle_count = qa_save_reader_list_count(&handles);
    prepared->alias_handles =
        handle_count > 0 ? (alias_handle *)calloc(handle_count, sizeof(alias_handle)) : NULL;
    for (size_t i = 0; i < handle_count; i++) {
      qa_save_reader item = save_item(&handles, i);
      qa_save_reader handle_r = save_field(&item, "handle");
      qa_save_reader name_r = save_field(&item, "name");
      double handle = qa_save_reader_integer_min(&handle_r, 0);
      const char *handle_name = qa_save_reader_string(&name_r);
      char *handle_key = registry_key(r, handle_name);
      alias_entry *alias = find_alias_in(aliases, alias_count, handle_key);
      bool duplicate = false;
      for (size_t j = 0; j < i; j++)
        if ((double)prepared->alias_handles[j].handle == handle) duplicate = true;
      bool target_known = alias != NULL && !alias->conversion.is_identity;
      bool target_present = false;
      if (alias != NULL) {
        for (size_t s = 0; s < variable_count; s++)
          if (prepared->indexes[s] != NULL &&
              strcmp(prepared->indexes[s]->key, alias->target_key) == 0)
            target_present = true;
      }
      if (handle >= (double)variable_count ||
          prepared->indexes[(size_t)handle] != NULL || duplicate || alias == NULL ||
          !target_known || !target_present)
        qa_save_reader_fail(&reader, "invalid cvar alias handle");
      prepared->alias_handles[i].handle = (int64_t)handle;
      prepared->alias_handles[i].name = dup_text(handle_name);
      prepared->alias_handle_count++;
      free(handle_key);
      qa_save_reader_destroy(&handle_r);
      qa_save_reader_destroy(&name_r);
      qa_save_reader_destroy(&item);
    }
  }
  qa_save_reader_destroy(&handles);
  for (size_t i = 0; i < r->binding_count; i++) {
    binding_entry *binding = &r->bindings[i];
    cvar_state *found = NULL;
    for (size_t s = 0; s < variable_count; s++)
      if (prepared->indexes[s] != NULL && strcmp(prepared->indexes[s]->key, binding->key) == 0)
        found = prepared->indexes[s];
    if (found == NULL)
      qa_save_reader_fail(&reader, "missing bound cvar %s", binding->key);
    const char *texts[3] = {found->value, found->reset_value, found->latched_value};
    size_t text_count = found->latched_value != NULL ? 3 : 2;
    for (size_t t = 0; t < text_count; t++) {
      char *error = binding->binding.validate != NULL
          ? binding->binding.validate(binding->binding.ctx, texts[t]) : NULL;
      if (error != NULL)
        qa_save_reader_fail(&reader, "%s: %s", binding->key, error);
    }
  }
  qa_save_reader_destroy(&reader);
  return prepared;
}
static void apply_prepared(qa_cvar_registry *r, prepared_restore *prepared,
                           qa_cvar_effect *effects, size_t effect_count) {
  r->mutation_revision++;
  for (size_t i = 0; i < r->index_count; i++) state_free(r->indexes[i]);
  free(r->indexes);
  r->indexes = prepared->indexes;
  r->index_count = prepared->index_count;
  r->index_capacity = prepared->index_count;
  prepared->indexes = NULL;
  prepared->index_count = 0;
  for (size_t i = 0; i < r->alias_handle_count; i++) free(r->alias_handles[i].name);
  free(r->alias_handles);
  r->alias_handles = prepared->alias_handles;
  r->alias_handle_count = prepared->alias_handle_count;
  r->alias_handle_capacity = prepared->alias_handle_count;
  prepared->alias_handles = NULL;
  prepared->alias_handle_count = 0;
  r->first = prepared->first;
  for (size_t i = 0; i < r->effect_count; i++) qa_cvar_effect_free(&r->effects[i]);
  free(r->effects);
  r->effects = effects;
  r->effect_count = effect_count;
  r->effect_capacity = effect_count;
  r->changed_flags = prepared->changed_flags;
  r->cheats_enabled = prepared->cheats_enabled;
  r->server_active = prepared->server_active;
  r->client_connected = prepared->client_connected;
  r->high_characters = prepared->high_characters;
  free(r->client_info);
  free(r->server_info);
  r->client_info = prepared->client_info;
  r->server_info = prepared->server_info;
  prepared->client_info = NULL;
  prepared->server_info = NULL;
  r->userinfo_dirty = prepared->userinfo_dirty;
  for (size_t i = 0; i < r->console_count; i++) free(r->console_variables[i]);
  free(r->console_variables);
  r->console_variables = prepared->console_variables;
  r->console_count = prepared->console_count;
  r->console_capacity = prepared->console_count;
  prepared->console_variables = NULL;
  prepared->console_count = 0;
  for (size_t i = 0; i < r->binding_count; i++) {
    cvar_state *state = find_state_by_key(r, r->bindings[i].key);
    if (state != NULL)
      binding_changed(r, r->bindings[i].key, state->value);
  }
  prepared_free(prepared);
}
static void restore_registry_state(qa_cvar_registry *r, const qa_save_value *value) {
  prepared_restore *prepared =
      prepare_registry_restore(r, value, r->aliases, r->alias_count);
  apply_prepared(r, prepared, NULL, 0);
}
void qa_cvar_registry_restore_save_state(qa_cvar_registry *r, const qa_save_value *value) {
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, value, "cvars"))
    qa_range_error("out of memory");
  qa_save_reader dialect = save_field(&reader, "dialect");
  (void)qa_save_reader_literal_string(&dialect, dialect_name(r->options.dialect));
  qa_save_reader_destroy(&dialect);
  qa_save_reader_destroy(&reader);
  restore_registry_state(r, value);
}
void qa_cvar_registry_restore_quakec_state(qa_cvar_registry *r, const qa_save_value *value) {
  if (!qa_command_dialect_is_q1(r->options.dialect))
    qa_error("QC cvar restore requires a Q1 registry");
  restore_registry_state(r, value);
}

/* --- Transfers --- */
typedef struct transfer_ctx {
  qa_cvar_registry *owner;
  qa_cvar_registry *candidate;
  bool owns_candidate;
  uint8_t *original;
  size_t original_length;
  double revision;
  qa_cvar_effect *effects;
  size_t effect_count;
  char **alias_keys;
  size_t alias_count;
  char **document_keys;
  size_t document_count;
  bool published;
} transfer_ctx;
static void noop_changed(void *ctx, const char *value) {
  (void)ctx;
  (void)value;
}
static bool same_session(qa_command_context a, qa_command_context b) {
  return a.session.token == b.session.token &&
         ((a.session.name == NULL && b.session.name == NULL) ||
          (a.session.name != NULL && b.session.name != NULL &&
              strcmp(a.session.name, b.session.name) == 0));
}
static uint8_t *capture_json(qa_cvar_registry *r, size_t *out_length) {
  qa_save_value *state = qa_cvar_registry_capture_world_transfer_state(r);
  if (state == NULL) return NULL;
  uint8_t *bytes = qa_encode_checkpoint_value(state, out_length);
  qa_save_value_free(state);
  return bytes;
}
static bool effects_equal(qa_cvar_effect *a, size_t a_count, qa_cvar_effect *b, size_t b_count) {
  if (a_count != b_count) return false;
  for (size_t i = 0; i < a_count; i++) {
    if (a[i].kind != b[i].kind) return false;
    const char *fields_a[5] = {a[i].name, a[i].value, a[i].info, a[i].text, a[i].directory};
    const char *fields_b[5] = {b[i].name, b[i].value, b[i].info, b[i].text, b[i].directory};
    for (int f = 0; f < 5; f++) {
      if (fields_a[f] == NULL && fields_b[f] == NULL) continue;
      if (fields_a[f] == NULL || fields_b[f] == NULL || strcmp(fields_a[f], fields_b[f]) != 0)
        return false;
    }
    if ((a[i].command == NULL) != (b[i].command == NULL)) return false;
    if (a[i].command != NULL && strcmp(a[i].command, b[i].command) != 0) return false;
  }
  return true;
}
static void transfer_validate(void *ctx) {
  transfer_ctx *t = (transfer_ctx *)ctx;
  qa_cvar_registry *owner = t->owner;
  if (t->published) qa_error("Cvar transfer already published");
  size_t length = 0;
  uint8_t *current = capture_json(owner, &length);
  bool same = current != NULL && length == t->original_length &&
              memcmp(current, t->original, length) == 0;
  free(current);
  bool aliases_same = owner->alias_count == t->alias_count;
  if (aliases_same) {
    for (size_t i = 0; i < t->alias_count; i++)
      if (find_alias_by_key(owner, t->alias_keys[i]) == NULL) aliases_same = false;
  }
  bool documents_same = owner->document_count == t->document_count;
  if (documents_same) {
    for (size_t i = 0; i < t->document_count; i++) {
      bool found = false;
      for (size_t j = 0; j < owner->document_count; j++)
        if (strcmp(owner->documents[j].key, t->document_keys[i]) == 0) found = true;
      if (!found) documents_same = false;
    }
  }
  if (t->revision != owner->mutation_revision || !same ||
      !effects_equal(owner->effects, owner->effect_count, t->effects, t->effect_count) ||
      !aliases_same || !documents_same)
    qa_error("Cvar owner changed during preparation");
  qa_save_value *candidate_state =
      qa_cvar_registry_capture_world_transfer_state(t->candidate);
  prepared_restore *prepared = prepare_registry_restore(
      owner, candidate_state, t->candidate->aliases, t->candidate->alias_count);
  prepared_free(prepared);
  qa_save_value_free(candidate_state);
}
static void transfer_publish(void *ctx) {
  transfer_ctx *t = (transfer_ctx *)ctx;
  qa_cvar_registry *owner = t->owner;
  transfer_validate(ctx);
  qa_save_value *candidate_state =
      qa_cvar_registry_capture_world_transfer_state(t->candidate);
  prepared_restore *prepared = prepare_registry_restore(
      owner, candidate_state, t->candidate->aliases, t->candidate->alias_count);
  qa_save_value_free(candidate_state);
  size_t pending_count = t->candidate->effect_count;
  qa_cvar_effect *pending = pending_count > 0
      ? (qa_cvar_effect *)calloc(pending_count, sizeof *pending) : NULL;
  for (size_t i = 0; i < pending_count; i++)
    (void)effect_copy(&pending[i], &t->candidate->effects[i]);
  t->published = true;
  /* Replace owner aliases/documents with the candidate's. */
  for (size_t i = 0; i < owner->alias_count; i++) {
    free(owner->aliases[i].key);
    free(owner->aliases[i].name);
    free(owner->aliases[i].target);
    free(owner->aliases[i].target_key);
  }
  free(owner->aliases);
  owner->aliases = NULL;
  owner->alias_count = 0;
  owner->alias_capacity = 0;
  for (size_t i = 0; i < t->candidate->alias_count; i++) {
    alias_entry *source = &t->candidate->aliases[i];
    if (owner->alias_count == owner->alias_capacity) {
      size_t cap = owner->alias_capacity == 0 ? 8 : owner->alias_capacity * 2;
      alias_entry *items = (alias_entry *)realloc(owner->aliases, cap * sizeof *items);
      if (items == NULL) qa_error("out of memory");
      owner->aliases = items;
      owner->alias_capacity = cap;
    }
    alias_entry *entry = &owner->aliases[owner->alias_count++];
    entry->key = dup_text(source->key);
    entry->name = dup_text(source->name);
    entry->target = dup_text(source->target);
    entry->target_key = dup_text(source->target_key);
    entry->documentation = source->documentation;
    entry->conversion = source->conversion;
  }
  for (size_t i = 0; i < owner->document_count; i++) free(owner->documents[i].key);
  free(owner->documents);
  owner->documents = NULL;
  owner->document_count = 0;
  owner->document_capacity = 0;
  for (size_t i = 0; i < t->candidate->document_count; i++) {
    if (owner->document_count == owner->document_capacity) {
      size_t cap = owner->document_capacity == 0 ? 8 : owner->document_capacity * 2;
      document_entry *items = (document_entry *)realloc(owner->documents, cap * sizeof *items);
      if (items == NULL) qa_error("out of memory");
      owner->documents = items;
      owner->document_capacity = cap;
    }
    owner->documents[owner->document_count].key =
        dup_text(t->candidate->documents[i].key);
    owner->documents[owner->document_count].documentation =
        t->candidate->documents[i].documentation;
    owner->document_count++;
  }
  size_t total = t->effect_count + pending_count;
  qa_cvar_effect *merged = total > 0 ? (qa_cvar_effect *)calloc(total, sizeof *merged) : NULL;
  for (size_t i = 0; i < t->effect_count; i++) (void)effect_copy(&merged[i], &t->effects[i]);
  for (size_t i = 0; i < pending_count; i++) merged[t->effect_count + i] = pending[i];
  free(pending);
  apply_prepared(owner, prepared, merged, total);
  if (owner->options.on_effect != NULL) {
    for (size_t i = 0; i < pending_count; i++)
      owner->options.on_effect(owner->options.on_effect_ctx, &merged[t->effect_count + i]);
  }
}
static void transfer_destroy(void *ctx) {
  transfer_ctx *t = (transfer_ctx *)ctx;
  free(t->original);
  for (size_t i = 0; i < t->effect_count; i++) qa_cvar_effect_free(&t->effects[i]);
  free(t->effects);
  for (size_t i = 0; i < t->alias_count; i++) free(t->alias_keys[i]);
  free(t->alias_keys);
  for (size_t i = 0; i < t->document_count; i++) free(t->document_keys[i]);
  free(t->document_keys);
  if (t->owns_candidate) qa_cvar_registry_destroy(t->candidate);
  free(t);
}
static qa_cvar_transfer *make_transfer(qa_cvar_registry *owner, qa_cvar_registry *candidate,
                                       bool owns_candidate) {
  if (candidate == owner || candidate->options.dialect != owner->options.dialect ||
      !same_session(candidate->options.context, owner->options.context))
    qa_error("Cvar transfer requires a separate owner in the same session and dialect");
  transfer_ctx *t = (transfer_ctx *)calloc(1, sizeof *t);
  if (t == NULL) return NULL;
  t->owner = owner;
  t->candidate = candidate;
  t->owns_candidate = owns_candidate;
  t->original = capture_json(owner, &t->original_length);
  t->revision = owner->mutation_revision;
  t->effect_count = owner->effect_count;
  t->effects = t->effect_count > 0
      ? (qa_cvar_effect *)calloc(t->effect_count, sizeof *t->effects) : NULL;
  for (size_t i = 0; i < t->effect_count; i++)
    (void)effect_copy(&t->effects[i], &owner->effects[i]);
  t->alias_count = owner->alias_count;
  t->alias_keys = t->alias_count > 0 ? (char **)calloc(t->alias_count, sizeof(char *)) : NULL;
  for (size_t i = 0; i < t->alias_count; i++) t->alias_keys[i] = dup_text(owner->aliases[i].key);
  t->document_count = owner->document_count;
  t->document_keys =
      t->document_count > 0 ? (char **)calloc(t->document_count, sizeof(char *)) : NULL;
  for (size_t i = 0; i < t->document_count; i++)
    t->document_keys[i] = dup_text(owner->documents[i].key);
  /* Seed the candidate like prepareTransfer does. */
  for (size_t i = 0; i < candidate->alias_count; i++) {
    free(candidate->aliases[i].key);
    free(candidate->aliases[i].name);
    free(candidate->aliases[i].target);
    free(candidate->aliases[i].target_key);
  }
  free(candidate->aliases);
  candidate->aliases = NULL;
  candidate->alias_count = 0;
  candidate->alias_capacity = 0;
  for (size_t i = 0; i < owner->alias_count; i++) {
    alias_entry *source = &owner->aliases[i];
    if (candidate->alias_count == candidate->alias_capacity) {
      size_t cap = candidate->alias_capacity == 0 ? 8 : candidate->alias_capacity * 2;
      alias_entry *items = (alias_entry *)realloc(candidate->aliases, cap * sizeof *items);
      if (items == NULL) qa_error("out of memory");
      candidate->aliases = items;
      candidate->alias_capacity = cap;
    }
    alias_entry *entry = &candidate->aliases[candidate->alias_count++];
    entry->key = dup_text(source->key);
    entry->name = dup_text(source->name);
    entry->target = dup_text(source->target);
    entry->target_key = dup_text(source->target_key);
    entry->documentation = source->documentation;
    entry->conversion = source->conversion;
  }
  for (size_t i = 0; i < candidate->document_count; i++) free(candidate->documents[i].key);
  free(candidate->documents);
  candidate->documents = NULL;
  candidate->document_count = 0;
  candidate->document_capacity = 0;
  for (size_t i = 0; i < owner->document_count; i++) {
    if (candidate->document_count == candidate->document_capacity) {
      size_t cap = candidate->document_capacity == 0 ? 8 : candidate->document_capacity * 2;
      document_entry *items = (document_entry *)realloc(candidate->documents, cap * sizeof *items);
      if (items == NULL) qa_error("out of memory");
      candidate->documents = items;
      candidate->document_capacity = cap;
    }
    candidate->documents[candidate->document_count].key = dup_text(owner->documents[i].key);
    candidate->documents[candidate->document_count].documentation =
        owner->documents[i].documentation;
    candidate->document_count++;
  }
  qa_save_value *state = qa_cvar_registry_capture_world_transfer_state(owner);
  qa_cvar_registry_restore_save_state(candidate, state);
  qa_save_value_free(state);
  qa_cvar_transfer *transfer = (qa_cvar_transfer *)calloc(1, sizeof *transfer);
  if (transfer == NULL) { transfer_destroy(t); return NULL; }
  transfer->ctx = t;
  transfer->cvars = candidate;
  transfer->validate_publication = transfer_validate;
  transfer->publish = transfer_publish;
  transfer->destroy = transfer_destroy;
  return transfer;
}
qa_cvar_transfer *qa_cvar_registry_prepare_transfer(qa_cvar_registry *r,
                                                    qa_cvar_registry *candidate) {
  return make_transfer(r, candidate, false);
}
qa_cvar_transfer *qa_cvar_registry_prepare_candidate(qa_cvar_registry *r,
                                                     qa_command_print_fn print,
                                                     void *print_ctx, bool has_print) {
  qa_cvar_registry_options options;
  memset(&options, 0, sizeof options);
  options.dialect = r->options.dialect;
  options.context = r->options.context;
  if (has_print) {
    options.print = print;
    options.print_ctx = print_ctx;
  }
  options.command_exists = r->options.command_exists;
  options.command_exists_ctx = r->options.command_exists_ctx;
  options.cheats_allowed = r->options.cheats_allowed;
  options.cheats_allowed_ctx = r->options.cheats_allowed_ctx;
  options.info_targets = r->options.info_targets;
  options.info_target_count = r->options.info_target_count;
  qa_cvar_registry *candidate = qa_cvar_registry_create(&options);
  if (candidate == NULL) return NULL;
  for (size_t i = 0; i < r->binding_count; i++) {
    if (candidate->binding_count == candidate->binding_capacity) {
      size_t cap = candidate->binding_capacity == 0 ? 8 : candidate->binding_capacity * 2;
      binding_entry *items = (binding_entry *)realloc(candidate->bindings, cap * sizeof *items);
      if (items == NULL) qa_error("out of memory");
      candidate->bindings = items;
      candidate->binding_capacity = cap;
    }
    binding_entry *entry = &candidate->bindings[candidate->binding_count++];
    entry->key = dup_text(r->bindings[i].key);
    entry->id = candidate->next_binding_id++;
    entry->binding.ctx = r->bindings[i].binding.ctx;
    entry->binding.validate = r->bindings[i].binding.validate;
    entry->binding.changed = noop_changed;
  }
  return make_transfer(r, candidate, true);
}
