/* See header. Release32 layouts: weightconfig_t is numweights, then 128 {name, firstseperator}
 * pointer pairs, then filename[64]; fuzzyseperator_t is 32 bytes. Pointer words are IDs scoped
 * to the store, not native addresses. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/weights.h"
#include "library_internal.h"
#include <math.h>
#include "qa/foundation/js.h"
#define CACHED_FILENAME_LENGTH 63
#define WEIGHT_CONFIG_BYTES 1092
#define WEIGHT_FILENAME_OFFSET 1028
#define FUZZY_SEPARATOR_BYTES 32
#define KIND_NAME 1
#define KIND_SEPARATOR 2
struct qa_weight_config_bots_a {
  qa_weight_config_store_bots_a *store;
  char *path;
  qa_bot_memory_allocation allocation;
  lib_diagnostics reported;
  bool disposed;
};
typedef uint32_t sep_t;
void qa_string_list_destroy(qa_string_list *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
  list->items = NULL;
  list->count = 0;
}
void qa_weight_config_error_destroy(qa_weight_config_error *error) {
  free(error->message);
  error->message = NULL;
  error->kind = QA_WEIGHT_CONFIG_ERROR_NONE;
}
/* ---- WeightHeap ---- */
static int64_t heap_pointer(qa_weight_config_store_bots_a *s) {
  if (s->next_pointer > 0xffffffffLL) qa_range_error("fuzzy weight pointer IDs exhausted");
  return s->next_pointer++;
}
static void heap_set(qa_weight_config_store_bots_a *s, int64_t pointer, uint8_t kind,
                     qa_bot_memory_allocation allocation) {
  if ((size_t)pointer >= s->heap_capacity) {
    size_t next = lib_grow(s->heap_capacity, (size_t)pointer + 1);
    s->heap = lib_realloc(s->heap, next * sizeof *s->heap);
    memset(s->heap + s->heap_capacity, 0, (next - s->heap_capacity) * sizeof *s->heap);
    s->heap_capacity = next;
  }
  s->heap[pointer] = (qa_weight_heap_entry){kind, allocation};
}
static uint32_t heap_allocate_name(qa_weight_config_store_bots_a *s, const char *name, size_t length) {
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(s->memory, (int64_t)length + 1, QA_BOT_MEMORY_HEAP, true);
  memcpy(allocation.bytes, name, length);
  int64_t pointer = heap_pointer(s);
  heap_set(s, pointer, KIND_NAME, allocation);
  return (uint32_t)pointer;
}
static const char *heap_name(const qa_weight_config_store_bots_a *s, uint32_t pointer) {
  if (pointer >= s->heap_capacity || s->heap[pointer].kind != KIND_NAME)
    qa_ui_script_plain_error("invalid fuzzy weight name pointer %u", pointer);
  return (const char *)s->heap[pointer].allocation.bytes;
}
static void heap_free_name(qa_weight_config_store_bots_a *s, uint32_t pointer) {
  (void)heap_name(s, pointer);
  qa_bot_memory_free(s->memory, &s->heap[pointer].allocation);
  s->heap[pointer].kind = 0;
}
static sep_t heap_allocate_separator(qa_weight_config_store_bots_a *s) {
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(s->memory, FUZZY_SEPARATOR_BYTES, QA_BOT_MEMORY_HEAP, true);
  int64_t pointer = heap_pointer(s);
  heap_set(s, pointer, KIND_SEPARATOR, allocation);
  return (sep_t)pointer;
}
static uint8_t *sep_bytes(const qa_weight_config_store_bots_a *s, sep_t pointer) {
  if (pointer >= s->heap_capacity || s->heap[pointer].kind != KIND_SEPARATOR)
    qa_ui_script_plain_error("invalid fuzzy separator pointer %u", pointer);
  return s->heap[pointer].allocation.bytes;
}
#define S_INDEX(b) lib_i32((b) + 0)
#define S_THRESHOLD(b) lib_i32((b) + 4)
#define S_BALANCED(b) (lib_i32((b) + 8) == 1)
#define S_WEIGHT(b) lib_f32((b) + 12)
#define S_MIN(b) lib_f32((b) + 16)
#define S_MAX(b) lib_f32((b) + 20)
#define S_CHILD(b) lib_u32((b) + 24)
#define S_NEXT(b) lib_u32((b) + 28)
static void free_separators(qa_weight_config_store_bots_a *s, sep_t separator) {
  if (separator == 0) return;
  uint8_t *b = sep_bytes(s, separator);
  free_separators(s, S_CHILD(b));
  free_separators(s, S_NEXT(b));
  qa_bot_memory_free(s->memory, &s->heap[separator].allocation);
  s->heap[separator].kind = 0;
}
/* ---- Parser ---- */
typedef struct weight_parser {
  qa_weight_config_store_bots_a *store;
  qa_weight_config_bots_a *config;
  qa_ui_script_source_reader *source;
  const char *path; /* the requested path; requireCurrent names it */
  int64_t generation;
  char *failure; /* set by fail(): "path:line:column: message" */
} weight_parser;
static void require_current(qa_weight_config_store_bots_a *s, int64_t generation, const char *path) {
  if (generation != s->generation)
    qa_ui_script_plain_error("weight config owner changed during synchronous source read: %s", path);
}
static void store_report(qa_weight_config_store_bots_a *s, const qa_ui_script_diagnostic *d,
                         int64_t generation, const char *path) {
  require_current(s, generation, path);
  if (s->options.print != NULL) {
    char *text = lib_format("file %s, line %lld: %s\n", d->location.path, (long long)d->location.line,
                            d->message);
    s->options.print(s->options.print_ctx, d->severity == QA_UI_SCRIPT_SEVERITY_WARNING ? 2 : 3, text);
    free(text);
  }
  require_current(s, generation, path);
}
static qa_ui_script_token *next_token(weight_parser *p) {
  require_current(p->store, p->generation, p->path);
  qa_ui_script_token *token = lib_source_token(p->source);
  require_current(p->store, p->generation, p->path);
  return token;
}
static void push_diagnostic(weight_parser *p, qa_ui_script_severity severity, const char *message,
                            int64_t column) {
  qa_ui_script_location at = lib_source_location(p->source, column);
  lib_diagnostics_push(&p->config->reported, severity, message, at.path, at.line, at.column);
  qa_ui_script_location_destroy(&at);
  /* The list may be reallocated by a reentrant source read; copy across the callback. */
  lib_diagnostics single = {NULL, 0, 0};
  lib_diagnostics_copy(&single, &p->config->reported.items[p->config->reported.count - 1]);
  store_report(p->store, &single.items[0], p->generation, p->path);
  if (severity == QA_UI_SCRIPT_SEVERITY_ERROR && p->failure == NULL)
    p->failure = lib_format("%s:%lld:%lld: %s", single.items[0].location.path,
                            (long long)single.items[0].location.line,
                            (long long)single.items[0].location.column, message);
  lib_diagnostics_destroy(&single);
}
__attribute__((format(printf, 3, 4))) static bool parse_fail(weight_parser *p, int64_t column,
                                                             const char *format, ...) {
  va_list args;
  va_start(args, format);
  char buffer[2048];
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  push_diagnostic(p, QA_UI_SCRIPT_SEVERITY_ERROR, buffer, column);
  return false;
}
static qa_ui_script_token *expect_any(weight_parser *p) {
  qa_ui_script_token *token = next_token(p);
  if (token == NULL) parse_fail(p, 1, "couldn't read expected token");
  return token;
}
static bool expect_text(weight_parser *p, const char *text) {
  qa_ui_script_token *token = next_token(p);
  if (token == NULL) return parse_fail(p, 1, "couldn't find expected %s", text);
  bool same = lib_token_is(token, text);
  if (!same) parse_fail(p, token->location.column, "expected %s, found %s", text, token->text);
  qa_ui_script_token_release(token);
  return same;
}
static qa_ui_script_token *expect_integer(weight_parser *p) {
  qa_ui_script_token *token = expect_any(p);
  if (token == NULL) return NULL;
  if (token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
    parse_fail(p, token->location.column, "expected a number, found %s", token->text);
    qa_ui_script_token_release(token);
    return NULL;
  }
  if ((token->flags & QA_UI_SCRIPT_NUMBER_INTEGER) == 0) {
    parse_fail(p, token->location.column, "expected integer, found %s", token->text);
    qa_ui_script_token_release(token);
    return NULL;
  }
  return token;
}
/* Consumes first. */
static bool value_from_first_token(weight_parser *p, qa_ui_script_token *first, float *out) {
  qa_ui_script_token *token = first;
  if (lib_token_is(token, "-")) {
    push_diagnostic(p, QA_UI_SCRIPT_SEVERITY_WARNING, "negative value set to zero\n",
                    token->location.column);
    qa_ui_script_token_release(token);
    token = expect_any(p);
    if (token == NULL) return false;
    if (token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
      parse_fail(p, token->location.column, "expected a number, found %s", token->text);
      qa_ui_script_token_release(token);
      return false;
    }
  }
  bool ok = true;
  if (token->kind != QA_UI_SCRIPT_KIND_NUMBER) {
    ok = parse_fail(p, token->location.column, "invalid return value %s\n", token->text);
  } else if (!isfinite(token->float_value) || !isfinite((float)token->float_value)) {
    ok = parse_fail(p, token->location.column, "return value exceeds finite float32 range");
  } else {
    *out = (float)token->float_value;
  }
  qa_ui_script_token_release(token);
  return ok;
}
static bool read_value(weight_parser *p, float *out) {
  qa_ui_script_token *token = expect_any(p);
  return token != NULL && value_from_first_token(p, token, out);
}
static bool parse_return(weight_parser *p, sep_t separator) {
  qa_ui_script_token *first = next_token(p);
  uint8_t *b = sep_bytes(p->store, separator);
  float value;
  if (first != NULL && lib_token_is(first, "balance")) {
    qa_ui_script_token_release(first);
    lib_set_i32(b + 8, 1);
    if (!expect_text(p, "(") || !read_value(p, &value)) return false;
    lib_set_f32(b + 12, value);
    if (!expect_text(p, ",") || !read_value(p, &value)) return false;
    lib_set_f32(b + 16, value);
    if (!expect_text(p, ",") || !read_value(p, &value)) return false;
    lib_set_f32(b + 20, value);
    if (!expect_text(p, ")")) return false;
  } else {
    lib_set_i32(b + 8, 0);
    bool ok = first == NULL ? read_value(p, &value) : value_from_first_token(p, first, &value);
    if (!ok) return false;
    lib_set_f32(b + 12, value);
    lib_set_f32(b + 16, value);
    lib_set_f32(b + 20, value);
  }
  return expect_text(p, ";");
}
/* 0 is a failure already recorded through parse_fail. */
static sep_t parse_switch(weight_parser *p, int64_t column) {
  if (!expect_text(p, "(")) return 0;
  qa_ui_script_token *index_token = expect_integer(p);
  if (index_token == NULL) return 0;
  int32_t inventory_index = (int32_t)index_token->integer_value;
  qa_ui_script_token_release(index_token);
  if (!expect_text(p, ")") || !expect_text(p, "{")) return 0;
  sep_t first = 0, last = 0;
  bool found_default = false, free_on_error = true;
  qa_ui_script_token *label = expect_any(p);
  if (label == NULL) goto failed;
  while (true) {
    bool is_default = lib_token_is(label, "default");
    if (!is_default && !lib_token_is(label, "case")) {
      free_on_error = false;
      free_separators(p->store, first);
      parse_fail(p, label->location.column, "invalid name %s\n", label->text);
      qa_ui_script_token_release(label);
      goto failed;
    }
    sep_t separator = heap_allocate_separator(p->store);
    uint8_t *b = sep_bytes(p->store, separator);
    lib_set_i32(b, inventory_index);
    if (last == 0) first = separator;
    else lib_set_u32(sep_bytes(p->store, last) + 28, separator);
    last = separator;
    if (is_default) {
      if (found_default) {
        parse_fail(p, label->location.column, "switch already has a default\n");
        qa_ui_script_token_release(label);
        goto failed;
      }
      lib_set_i32(b + 4, QA_MAX_INVENTORY_VALUE_BOTS_A);
      found_default = true;
    } else {
      qa_ui_script_token *threshold = expect_integer(p);
      if (threshold == NULL) {
        qa_ui_script_token_release(label);
        goto failed;
      }
      lib_set_i32(sep_bytes(p->store, separator) + 4, (int32_t)threshold->integer_value);
      qa_ui_script_token_release(threshold);
    }
    qa_ui_script_token_release(label);
    if (!expect_text(p, ":")) goto failed;
    qa_ui_script_token *action = expect_any(p);
    if (action == NULL) goto failed;
    bool braced = false;
    if (lib_token_is(action, "{")) {
      braced = true;
      qa_ui_script_token_release(action);
      action = expect_any(p);
      if (action == NULL) goto failed;
    }
    if (lib_token_is(action, "return")) {
      qa_ui_script_token_release(action);
      if (!parse_return(p, separator)) goto failed;
    } else if (lib_token_is(action, "switch")) {
      int64_t action_column = action->location.column;
      qa_ui_script_token_release(action);
      sep_t child = parse_switch(p, action_column);
      if (child == 0) goto failed;
      lib_set_u32(sep_bytes(p->store, separator) + 24, child);
    } else {
      /* This source return omits FreeFuzzySeperators_r for its local chain. */
      free_on_error = false;
      parse_fail(p, action->location.column, "invalid name %s\n", action->text);
      qa_ui_script_token_release(action);
      goto failed;
    }
    if (braced && !expect_text(p, "}")) goto failed;
    label = expect_any(p);
    if (label == NULL) goto failed;
    if (lib_token_is(label, "}")) {
      qa_ui_script_token_release(label);
      break;
    }
  }
  if (!found_default) {
    push_diagnostic(p, QA_UI_SCRIPT_SEVERITY_WARNING, "switch without default\n", column);
    sep_t separator = heap_allocate_separator(p->store);
    uint8_t *b = sep_bytes(p->store, separator);
    lib_set_i32(b, inventory_index);
    lib_set_i32(b + 4, QA_MAX_INVENTORY_VALUE_BOTS_A);
    if (last == 0) first = separator;
    else lib_set_u32(sep_bytes(p->store, last) + 28, separator);
  }
  return first;
failed:
  if (free_on_error) free_separators(p->store, first);
  return 0;
}
static bool config_set_name(qa_weight_config_bots_a *c, int64_t index, const char *name, size_t length) {
  uint32_t pointer = heap_allocate_name(c->store, name, length);
  lib_set_u32(c->allocation.bytes + 4 + index * 8, pointer);
  return true;
}
static int64_t config_count(const qa_weight_config_bots_a *c);
static bool parse_config(weight_parser *p) {
  qa_weight_config_bots_a *config = p->config;
  while (true) {
    qa_ui_script_token *token = next_token(p);
    if (token == NULL) return true;
    if (!lib_token_is(token, "weight")) {
      parse_fail(p, token->location.column, "invalid name %s\n", token->text);
      qa_ui_script_token_release(token);
      return false;
    }
    if (config_count(config) >= QA_MAX_FUZZY_WEIGHTS) {
      push_diagnostic(p, QA_UI_SCRIPT_SEVERITY_WARNING, "too many fuzzy weights\n",
                      token->location.column);
      qa_ui_script_token_release(token);
      return true;
    }
    qa_ui_script_token_release(token);
    qa_ui_script_token *name = expect_any(p);
    if (name == NULL) return false;
    if (name->kind != QA_UI_SCRIPT_KIND_STRING) {
      parse_fail(p, name->location.column, "expected a string, found %s", name->text);
      qa_ui_script_token_release(name);
      return false;
    }
    int64_t weight_index = config_count(config);
    const char *zero = memchr(name->value, 0, name->value_length);
    size_t name_length = zero != NULL ? (size_t)(zero - name->value) : name->value_length;
    config_set_name(config, weight_index, name->value, name_length);
    qa_ui_script_token_release(name);
    qa_ui_script_token *action = expect_any(p);
    if (action == NULL) return false;
    bool braced = false;
    if (lib_token_is(action, "{")) {
      braced = true;
      qa_ui_script_token_release(action);
      action = expect_any(p);
      if (action == NULL) return false;
    }
    sep_t separators;
    if (lib_token_is(action, "switch")) {
      int64_t column = action->location.column;
      qa_ui_script_token_release(action);
      separators = parse_switch(p, column);
      if (separators == 0) return false;
    } else if (lib_token_is(action, "return")) {
      qa_ui_script_token_release(action);
      separators = heap_allocate_separator(p->store);
      uint8_t *b = sep_bytes(p->store, separators);
      lib_set_i32(b, 0);
      lib_set_i32(b + 4, QA_MAX_INVENTORY_VALUE_BOTS_A);
      if (!parse_return(p, separators)) {
        free_separators(p->store, separators);
        return false;
      }
    } else {
      parse_fail(p, action->location.column, "invalid name %s\n", action->text);
      qa_ui_script_token_release(action);
      return false;
    }
    lib_set_u32(config->allocation.bytes + 8 + weight_index * 8, separators);
    if (braced && !expect_text(p, "}")) return false;
    lib_set_i32(config->allocation.bytes, (int32_t)weight_index + 1);
  }
}
/* ---- Evaluation ---- */
static int32_t inventory_value(qa_weight_inventory inventory, int64_t index) {
  double value;
  if (inventory.values != NULL || inventory.read == NULL) {
    if (index < 0 || (size_t)index >= inventory.count)
      qa_range_error("inventory index %lld is outside %zu entries", (long long)index, inventory.count);
    value = inventory.values[index];
  } else {
    value = inventory.read(inventory.ctx, index);
  }
  if (trunc(value) != value || value < INT32_MIN || value > INT32_MAX)
    qa_range_error("inventory value at index %lld must be an int32", (long long)index);
  return (int32_t)value;
}
float qa_bot_source_unit_random(qa_bot_random random) {
  int64_t raw = random.next_int(random.ctx);
  if (raw < INT32_MIN || raw > 0xffffffffLL)
    qa_range_error("bot random nextInt must return an int32 or uint32 value");
  return (float)((double)(raw & 0x7fff) / (double)(float)0x7fff);
}
static float source_interpolation_scale(int32_t inventory, int32_t lower, int32_t upper) {
  int32_t numerator = (int32_t)((int64_t)inventory - lower);
  int32_t denominator = (int32_t)((int64_t)upper - lower);
  if (denominator == 0) qa_range_error("fuzzy separator thresholds produce division by zero");
  return (float)trunc((double)numerator / (double)denominator);
}
static float interpolate(float scale, float first, float second) {
  float first_part = (float)((double)scale * (double)first);
  float inverse = (float)(1.0 - (double)scale);
  float second_part = (float)((double)inverse * (double)second);
  return (float)((double)first_part + (double)second_part);
}
static float evaluate_separators(const qa_weight_config_store_bots_a *s, sep_t first,
                                 qa_weight_inventory inventory);
static float leaf_value(const qa_weight_config_store_bots_a *s, sep_t separator,
                        qa_weight_inventory inventory) {
  const uint8_t *b = sep_bytes(s, separator);
  sep_t child = S_CHILD(b);
  return child != 0 ? evaluate_separators(s, child, inventory) : S_WEIGHT(b);
}
static float evaluate_separators(const qa_weight_config_store_bots_a *s, sep_t first,
                                 qa_weight_inventory inventory) {
  sep_t separator = first;
  while (true) {
    const uint8_t *b = sep_bytes(s, separator);
    if (inventory_value(inventory, S_INDEX(b)) < S_THRESHOLD(b))
      return leaf_value(s, separator, inventory);
    sep_t next = S_NEXT(b);
    if (next == 0) return S_WEIGHT(b);
    const uint8_t *n = sep_bytes(s, next);
    if (inventory_value(inventory, S_INDEX(b)) < S_THRESHOLD(n)) {
      float first_value = leaf_value(s, separator, inventory);
      float second_value = leaf_value(s, next, inventory);
      return interpolate(source_interpolation_scale(inventory_value(inventory, S_INDEX(b)),
                                                    S_THRESHOLD(b), S_THRESHOLD(n)),
                         first_value, second_value);
    }
    separator = next;
  }
}
static float evaluate_undecided_separators(const qa_weight_config_store_bots_a *s, sep_t first,
                                           qa_weight_inventory inventory, qa_bot_random random);
static float undecided_leaf_value(const qa_weight_config_store_bots_a *s, sep_t separator,
                                  qa_weight_inventory inventory, qa_bot_random random) {
  const uint8_t *b = sep_bytes(s, separator);
  sep_t child = S_CHILD(b);
  if (child != 0) return evaluate_undecided_separators(s, child, inventory, random);
  float range = (float)((double)S_MAX(b) - (double)S_MIN(b));
  float unit = qa_bot_source_unit_random(random);
  return (float)((double)S_MIN(b) + (double)(float)((double)unit * (double)range));
}
static float evaluate_undecided_separators(const qa_weight_config_store_bots_a *s, sep_t first,
                                           qa_weight_inventory inventory, qa_bot_random random) {
  sep_t separator = first;
  while (true) {
    const uint8_t *b = sep_bytes(s, separator);
    if (inventory_value(inventory, S_INDEX(b)) < S_THRESHOLD(b))
      return undecided_leaf_value(s, separator, inventory, random);
    sep_t next = S_NEXT(b);
    if (next == 0) return S_WEIGHT(b);
    const uint8_t *n = sep_bytes(s, next);
    if (inventory_value(inventory, S_INDEX(b)) < S_THRESHOLD(n)) {
      float first_value = undecided_leaf_value(s, separator, inventory, random);
      sep_t next_child = S_CHILD(n);
      float second_value = next_child != 0
                               ? evaluate_separators(s, next_child, inventory)
                               : undecided_leaf_value(s, next, inventory, random);
      return interpolate(source_interpolation_scale(inventory_value(inventory, S_INDEX(b)),
                                                    S_THRESHOLD(b), S_THRESHOLD(n)),
                         first_value, second_value);
    }
    separator = next;
  }
}
typedef void (*leaf_visit_fn)(void *ctx, uint8_t *leaf);
static void visit_leaves(const qa_weight_config_store_bots_a *s, sep_t first, leaf_visit_fn visit,
                         void *ctx) {
  for (sep_t separator = first; separator != 0;) {
    uint8_t *b = sep_bytes(s, separator);
    sep_t child = S_CHILD(b);
    if (child != 0) visit_leaves(s, child, visit, ctx);
    else visit(ctx, b);
    separator = S_NEXT(sep_bytes(s, separator));
  }
}
typedef struct report_sink {
  qa_string_list *errors;
  void (*report)(void *ctx, const char *message);
  void *ctx;
} report_sink;
static void report_error(report_sink *sink, const char *message) {
  qa_string_list *list = sink->errors;
  list->items = lib_realloc(list->items, (list->count + 1) * sizeof *list->items);
  list->items[list->count++] = lib_strdup(message);
  if (sink->report != NULL) sink->report(sink->ctx, message);
}
static bool interbreed_separators(const qa_weight_config_store_bots_a *s1,
                                  const qa_weight_config_store_bots_a *s2,
                                  const qa_weight_config_store_bots_a *so, sep_t parent1,
                                  sep_t parent2, sep_t output, report_sink *report) {
  sep_t first = parent1, second = parent2, destination = output;
  while (true) {
    const uint8_t *f = sep_bytes(s1, first), *g = sep_bytes(s2, second);
    uint8_t *d = sep_bytes(so, destination);
    if (S_CHILD(f) != 0) {
      if (S_CHILD(g) == 0 || S_CHILD(d) == 0) {
        report_error(report, "cannot interbreed weight configs, unequal child");
        return false;
      }
      /* The source recurses with the second parent's children twice. */
      if (!interbreed_separators(s2, s2, so, S_CHILD(g), S_CHILD(g), S_CHILD(d), report)) return false;
    } else if (S_BALANCED(f)) {
      if (!S_BALANCED(g) || !S_BALANCED(d)) {
        report_error(report, "cannot interbreed weight configs, unequal balance");
        return false;
      }
      float sum = (float)((double)S_WEIGHT(f) + (double)S_WEIGHT(g));
      lib_set_f32(d + 12, (float)((double)sum / 2.0));
      if (S_WEIGHT(d) > S_MAX(d)) lib_set_f32(d + 20, S_WEIGHT(d));
      if (S_WEIGHT(d) > S_MIN(d)) lib_set_f32(d + 16, S_WEIGHT(d));
    }
    sep_t next = S_NEXT(f);
    if (next == 0) return true;
    sep_t second_next = S_NEXT(g), output_next = S_NEXT(d);
    if (second_next == 0 || output_next == 0) {
      report_error(report, "cannot interbreed weight configs, unequal next");
      return false;
    }
    first = next;
    second = second_next;
    destination = output_next;
  }
}
/* ---- OwnedWeightConfig ---- */
static void require_open(const qa_weight_config_bots_a *c) {
  if (c->disposed) qa_ui_script_plain_error("weight config %s has been freed", c->path);
}
static int64_t config_count(const qa_weight_config_bots_a *c) {
  int32_t count = lib_i32(c->allocation.bytes);
  if (count < 0 || count > QA_MAX_FUZZY_WEIGHTS) qa_range_error("invalid fuzzy weight count %d", count);
  return count;
}
int64_t qa_weight_config_weight_count(const qa_weight_config_bots_a *c) {
  require_open(c);
  return config_count(c);
}
const char *qa_weight_config_path(const qa_weight_config_bots_a *c) { return c->path; }
static sep_t config_separators(const qa_weight_config_bots_a *c, int64_t index) {
  int64_t count = config_count(c);
  if (index < 0 || index >= count)
    qa_range_error("weight index %lld is outside %lld entries", (long long)index, (long long)count);
  sep_t separator = lib_u32(c->allocation.bytes + 8 + index * 8);
  if (separator == 0) qa_ui_script_plain_error("weight %lld has no fuzzy separator", (long long)index);
  (void)sep_bytes(c->store, separator);
  return separator;
}
const char *qa_weight_config_name(const qa_weight_config_bots_a *c, int64_t index) {
  require_open(c);
  return heap_name(c->store, lib_u32(c->allocation.bytes + 4 + index * 8));
}
static int64_t maximum_index(const qa_weight_config_store_bots_a *s, sep_t first) {
  int64_t result = -1;
  for (sep_t separator = first; separator != 0;) {
    const uint8_t *b = sep_bytes(s, separator);
    if (S_INDEX(b) > result) result = S_INDEX(b);
    sep_t child = S_CHILD(b);
    if (child != 0) {
      int64_t nested = maximum_index(s, child);
      if (nested > result) result = nested;
    }
    separator = S_NEXT(b);
  }
  return result;
}
int64_t qa_weight_config_max_inventory_index(const qa_weight_config_bots_a *c) {
  require_open(c);
  int64_t result = -1, count = config_count(c);
  for (int64_t index = 0; index < count; index++) {
    int64_t nested = maximum_index(c->store, config_separators(c, index));
    if (nested > result) result = nested;
  }
  return result;
}
const qa_ui_script_diagnostic *qa_weight_config_diagnostics(const qa_weight_config_bots_a *c,
                                                            size_t *out_count) {
  *out_count = c->reported.count;
  return c->reported.items;
}
int64_t qa_weight_config_find(const qa_weight_config_bots_a *c, const char *name) {
  require_open(c);
  int64_t count = config_count(c);
  for (int64_t index = 0; index < count; index++)
    if (strcmp(qa_weight_config_name(c, index), name) == 0) return index;
  return -1;
}
float qa_weight_config_evaluate(const qa_weight_config_bots_a *c, int64_t index,
                                qa_weight_inventory inventory) {
  require_open(c);
  return evaluate_separators(c->store, config_separators(c, index), inventory);
}
float qa_weight_config_evaluate_undecided(const qa_weight_config_bots_a *c, int64_t index,
                                          qa_weight_inventory inventory, qa_bot_random random) {
  require_open(c);
  return evaluate_undecided_separators(c->store, config_separators(c, index), inventory, random);
}
static void scale_weight_leaf(void *ctx, uint8_t *leaf) {
  if (!S_BALANCED(leaf)) return;
  float scale = *(float *)ctx;
  float sum = (float)((double)S_MAX(leaf) + (double)S_MIN(leaf));
  lib_set_f32(leaf + 12, (float)((double)sum * (double)scale));
  if (S_WEIGHT(leaf) < S_MIN(leaf)) lib_set_f32(leaf + 12, S_MIN(leaf));
  else if (S_WEIGHT(leaf) > S_MAX(leaf)) lib_set_f32(leaf + 12, S_MAX(leaf));
}
void qa_weight_config_scale_weight(qa_weight_config_bots_a *c, const char *name, double scale) {
  require_open(c);
  float stored = (float)scale;
  if (stored < 0) stored = 0;
  else if (stored > 1) stored = 1;
  int64_t index = qa_weight_config_find(c, name);
  if (index < 0) return;
  visit_leaves(c->store, config_separators(c, index), scale_weight_leaf, &stored);
}
static void scale_range_leaf(void *ctx, uint8_t *leaf) {
  if (!S_BALANCED(leaf)) return;
  float scale = *(float *)ctx;
  float midpoint = (float)((double)(float)((double)S_MIN(leaf) + (double)S_MAX(leaf)) * 0.5);
  lib_set_f32(leaf + 20,
              (float)((double)midpoint +
                      (double)(float)((double)(float)((double)S_MAX(leaf) - (double)midpoint) * (double)scale)));
  lib_set_f32(leaf + 16,
              (float)((double)midpoint +
                      (double)(float)((double)(float)((double)S_MIN(leaf) - (double)midpoint) * (double)scale)));
  if (S_MAX(leaf) < S_MIN(leaf)) lib_set_f32(leaf + 20, S_MIN(leaf));
}
void qa_weight_config_scale_balance_range(qa_weight_config_bots_a *c, double scale) {
  require_open(c);
  float stored = (float)scale;
  if (stored < 0) stored = 0;
  else if (stored > 100) stored = 100;
  int64_t count = config_count(c);
  for (int64_t index = 0; index < count; index++)
    visit_leaves(c->store, config_separators(c, index), scale_range_leaf, &stored);
}
static void evolve_leaf(void *ctx, uint8_t *leaf) {
  if (!S_BALANCED(leaf)) return;
  qa_bot_random random = *(qa_bot_random *)ctx;
  bool leap = (double)qa_bot_source_unit_random(random) < 0.01;
  double centered = 2 * ((double)qa_bot_source_unit_random(random) - 0.5);
  float range = (float)((double)S_MAX(leaf) - (double)S_MIN(leaf));
  double delta = centered * (double)range * (leap ? 1 : 0.5);
  lib_set_f32(leaf + 12, (float)((double)S_WEIGHT(leaf) + delta));
  if (S_WEIGHT(leaf) < S_MIN(leaf)) lib_set_f32(leaf + 16, S_WEIGHT(leaf));
  else if (S_WEIGHT(leaf) > S_MAX(leaf)) lib_set_f32(leaf + 20, S_WEIGHT(leaf));
}
void qa_weight_config_evolve(qa_weight_config_bots_a *c, qa_bot_random random) {
  require_open(c);
  int64_t count = config_count(c);
  for (int64_t index = 0; index < count; index++)
    visit_leaves(c->store, config_separators(c, index), evolve_leaf, &random);
}
qa_string_list qa_weight_config_interbreed_from(qa_weight_config_bots_a *c,
                                                const qa_weight_config_bots_a *parent1,
                                                const qa_weight_config_bots_a *parent2,
                                                void (*report)(void *ctx, const char *message),
                                                void *report_ctx) {
  require_open(c);
  require_open(parent1);
  require_open(parent2);
  qa_string_list errors = {NULL, 0};
  report_sink sink = {&errors, report, report_ctx};
  if (config_count(parent1) != config_count(parent2) || config_count(parent1) != config_count(c)) {
    report_error(&sink, "cannot interbreed weight configs, unequal numweights");
    return errors;
  }
  int64_t count = config_count(parent1);
  for (int64_t index = 0; index < count; index++)
    (void)interbreed_separators(parent1->store, parent2->store, c->store,
                                config_separators(parent1, index), config_separators(parent2, index),
                                config_separators(c, index), &sink);
  return errors;
}
static void config_free(qa_weight_config_bots_a *c) {
  require_open(c);
  int64_t count = config_count(c);
  for (int64_t index = 0; index < count; index++) {
    sep_t separator = lib_u32(c->allocation.bytes + 8 + index * 8);
    if (separator != 0) (void)sep_bytes(c->store, separator);
    free_separators(c->store, separator);
    uint32_t name = lib_u32(c->allocation.bytes + 4 + index * 8);
    if (name != 0) heap_free_name(c->store, name);
  }
  qa_bot_memory_free(c->store->memory, &c->allocation);
  c->disposed = true;
}
static qa_weight_config_bots_a *config_create(qa_weight_config_store_bots_a *s, const char *path,
                                              qa_bot_memory_allocation allocation, const char *filename) {
  qa_weight_config_bots_a *c = lib_alloc(sizeof *c);
  c->store = s;
  c->path = lib_strdup(path);
  c->allocation = allocation;
  if (filename != NULL) {
    size_t length = strlen(filename);
    if (length > CACHED_FILENAME_LENGTH) length = CACHED_FILENAME_LENGTH;
    memcpy(allocation.bytes + WEIGHT_FILENAME_OFFSET, filename, length);
  }
  if (s->created_count == s->created_capacity) {
    s->created_capacity = lib_grow(s->created_capacity, s->created_count + 1);
    s->created = lib_realloc(s->created, s->created_capacity * sizeof *s->created);
  }
  s->created[s->created_count++] = c;
  return c;
}
/* ---- WeightConfigStore ---- */
void qa_weight_config_store_bots_a_init(qa_weight_config_store_bots_a *s) {
  memset(s, 0, sizeof *s);
  qa_bot_memory_init(&s->owned_memory);
  s->memory = &s->owned_memory;
  s->next_pointer = 1;
  s->max_cached_configs = QA_MAX_CACHED_WEIGHT_CONFIGS;
}
void qa_weight_config_store_bots_a_configure(qa_weight_config_store_bots_a *s,
                                             const qa_bot_script_reader *resolver,
                                             const qa_weight_config_store_options *options) {
  s->resolver = *resolver;
  s->has_resolver = true;
  memset(&s->options, 0, sizeof s->options);
  if (options != NULL) s->options = *options;
  if (s->options.memory != NULL) s->memory = s->options.memory;
  if (s->options.has_max_cached_configs) {
    double value = s->options.max_cached_configs;
    if (trunc(value) != value || value < 1)
      qa_range_error("maxCachedConfigs must be a positive integer");
    if (value > QA_MAX_CACHED_WEIGHT_CONFIGS)
      qa_range_error("maxCachedConfigs cannot exceed source limit %d", QA_MAX_CACHED_WEIGHT_CONFIGS);
    s->max_cached_configs = (int64_t)value;
  }
  memset(&s->preprocessor, 0, sizeof s->preprocessor);
  if (s->options.preprocessor != NULL) s->preprocessor = *s->options.preprocessor;
}
void qa_weight_config_store_bots_a_destroy(qa_weight_config_store_bots_a *s) {
  for (size_t i = 0; i < s->created_count; i++) {
    lib_diagnostics_destroy(&s->created[i]->reported);
    free(s->created[i]->path);
    free(s->created[i]);
  }
  free(s->created);
  free(s->owned);
  free(s->heap);
  s->created = s->owned = NULL;
  s->heap = NULL;
  s->created_count = s->created_capacity = s->owned_count = s->owned_capacity = s->heap_capacity = 0;
  qa_bot_memory_destroy(&s->owned_memory);
}
static bool reload_characters(qa_weight_config_store_bots_a *s) {
  return s->options.reload_characters_fn != NULL ? s->options.reload_characters_fn(s->options.reload_ctx)
                                                 : s->options.reload_characters;
}
static void own(qa_weight_config_store_bots_a *s, qa_weight_config_bots_a *c) {
  if (s->owned_count == s->owned_capacity) {
    s->owned_capacity = lib_grow(s->owned_capacity, s->owned_count + 1);
    s->owned = lib_realloc(s->owned, s->owned_capacity * sizeof *s->owned);
  }
  s->owned[s->owned_count++] = c;
}
static void disown(qa_weight_config_store_bots_a *s, qa_weight_config_bots_a *c) {
  for (size_t i = 0; i < s->owned_count; i++) {
    if (s->owned[i] != c) continue;
    memmove(&s->owned[i], &s->owned[i + 1], (s->owned_count - i - 1) * sizeof *s->owned);
    s->owned_count--;
    return;
  }
}
static bool matches_filename(const qa_weight_config_bots_a *c, const char *filename) {
  return strncmp((const char *)c->allocation.bytes + WEIGHT_FILENAME_OFFSET, filename,
                 WEIGHT_CONFIG_BYTES - WEIGHT_FILENAME_OFFSET) == 0 &&
         strlen(filename) < WEIGHT_CONFIG_BYTES - WEIGHT_FILENAME_OFFSET;
}
static qa_weight_config_bots_a *throw_error(qa_weight_config_error *error,
                                            qa_weight_config_error_kind kind, char *message) {
  if (error == NULL) {
    fprintf(stderr, "%s: %s\n",
            kind == QA_WEIGHT_CONFIG_ERROR_PARSE ? "WeightConfigError" : "WeightConfigLoadError", message);
    abort();
  }
  error->kind = kind;
  error->message = message;
  return NULL;
}
typedef struct read_context {
  qa_weight_config_store_bots_a *store;
  const char *path;
  int64_t generation;
  lib_diagnostics pending; /* preprocessor reports before the config exists */
  qa_weight_config_bots_a *config;
} read_context;
static void read_report(void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  read_context *r = ctx;
  lib_diagnostics *list = r->config != NULL ? &r->config->reported : &r->pending;
  lib_diagnostics_copy(list, diagnostic);
  if (r->store->preprocessor.report != NULL)
    r->store->preprocessor.report(r->store->preprocessor.report_ctx, diagnostic);
  store_report(r->store, diagnostic, r->generation, r->path);
}
static bool read_resolve(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  qa_weight_config_store_bots_a *s = ctx;
  return s->resolver.resolve(s->resolver.ctx, request, out);
}
static qa_weight_config_bots_a *store_read(qa_weight_config_store_bots_a *s, const char *path,
                                           int64_t generation, qa_weight_config_error *error) {
  if (!s->has_resolver) qa_range_error("WeightConfigStore has no script resolver");
  qa_ui_script_source source;
  bool resolved = s->resolver.resolve_root(s->resolver.ctx, path, &source);
  require_current(s, generation, path);
  if (!resolved) {
    if (s->options.print != NULL) {
      char *text = lib_format("counldn't load %s\n", path);
      s->options.print(s->options.print_ctx, 3, text);
      free(text);
    }
    return throw_error(error, QA_WEIGHT_CONFIG_ERROR_LOAD, lib_format("could not load weight config %s", path));
  }
  if (source.path == NULL || source.path[0] == '\0')
    qa_ui_script_plain_error("weight config resolver returned an empty canonical path");
  char *canonical = lib_strdup(source.path);
  read_context context = {s, path, generation, {NULL, 0, 0}, NULL};
  qa_ui_script_preprocessor_options options = s->preprocessor;
  options.globals = s->resolver.globals;
  if (s->resolver.debug_eval != NULL) {
    options.debug_eval = s->resolver.debug_eval;
    options.debug_eval_ctx = s->resolver.debug_eval_ctx;
  }
  options.report = read_report;
  options.report_ctx = &context;
  qa_ui_script_source_reader *preprocessor =
      qa_ui_script_source_reader_open(&source, read_resolve, s, &options);
  if (preprocessor == NULL) qa_range_error("out of memory");
  require_current(s, generation, path);
  qa_bot_memory_allocation allocation =
      lib_mem_alloc(s->memory, WEIGHT_CONFIG_BYTES, QA_BOT_MEMORY_HEAP, true);
  qa_weight_config_bots_a *config = config_create(s, canonical, allocation, path);
  free(canonical);
  for (size_t i = 0; i < context.pending.count; i++)
    lib_diagnostics_copy(&config->reported, &context.pending.items[i]);
  lib_diagnostics_destroy(&context.pending);
  context.config = config;
  weight_parser parser = {s, config, preprocessor, path, generation, NULL};
  if (!parse_config(&parser)) {
    bool reload = reload_characters(s);
    require_current(s, generation, path);
    if (reload) config_free(config);
    qa_ui_script_source_reader_dispose(preprocessor);
    qa_ui_script_source_reader_destroy(preprocessor);
    return throw_error(error, QA_WEIGHT_CONFIG_ERROR_PARSE, parser.failure);
  }
  qa_ui_script_source_reader_dispose(preprocessor);
  qa_ui_script_source_reader_destroy(preprocessor);
  return config;
}
qa_weight_config_bots_a *qa_weight_config_store_load(qa_weight_config_store_bots_a *s, const char *path,
                                                     qa_weight_config_error *error) {
  bool debug = s->options.debug_milliseconds != NULL;
  double start = debug ? s->options.debug_milliseconds(s->options.debug_ctx) : 0;
  int64_t generation = s->generation;
  bool reload = reload_characters(s);
  require_current(s, generation, path);
  int64_t available = 0;
  if (!reload) {
    available = -1;
    for (int64_t index = 0; index < s->max_cached_configs; index++) {
      qa_weight_config_bots_a *config = (size_t)index < s->cached_length ? s->cached[index] : NULL;
      if (config == NULL) {
        if (available == -1) available = index;
        continue;
      }
      if (matches_filename(config, path)) return config;
    }
    if (available == -1) {
      if (s->options.print != NULL) {
        char *text = lib_format("weightFileList was full trying to load %s\n", path);
        s->options.print(s->options.print_ctx, 3, text);
        free(text);
      }
      return throw_error(error, QA_WEIGHT_CONFIG_ERROR_LOAD,
                         lib_format("weight config cache is full at %lld entries",
                                    (long long)s->max_cached_configs));
    }
  }
  qa_weight_config_bots_a *config = store_read(s, path, generation, error);
  if (config == NULL) return NULL;
  if (generation != s->generation) {
    config_free(config);
    require_current(s, generation, path);
  }
  own(s, config);
  if (s->options.print != NULL) {
    char *text = lib_format("loaded %s\n", path);
    s->options.print(s->options.print_ctx, 1, text);
    free(text);
  }
  require_current(s, generation, path);
  if (debug && s->options.debug_developer != NULL && s->options.debug_developer(s->options.debug_ctx)) {
    require_current(s, generation, path);
    int32_t elapsed = qa_js_to_int32(s->options.debug_milliseconds(s->options.debug_ctx) - start); /* (now - start) | 0 */
    require_current(s, generation, path);
    if (s->options.print != NULL) {
      char *text = lib_format("weights loaded in %d msec\n", elapsed);
      s->options.print(s->options.print_ctx, 1, text);
      free(text);
    }
  }
  require_current(s, generation, path);
  bool reload_now = reload_characters(s);
  require_current(s, generation, path);
  if (!reload_now) {
    s->cached[available] = config;
    if ((size_t)available >= s->cached_length) s->cached_length = (size_t)available + 1;
  }
  return config;
}
void qa_weight_config_store_free(qa_weight_config_store_bots_a *s, qa_weight_config_bots_a *config) {
  bool owned = false;
  for (size_t i = 0; i < s->owned_count && !owned; i++) owned = s->owned[i] == config;
  if (!owned) qa_ui_script_plain_error("weight config is not owned by this store");
  if (!reload_characters(s)) return;
  config_free(config);
  disown(s, config);
}
void qa_weight_config_store_shutdown(qa_weight_config_store_bots_a *s) {
  s->generation++;
  for (int64_t index = 0; index < s->max_cached_configs; index++) {
    qa_weight_config_bots_a *config = (size_t)index < s->cached_length ? s->cached[index] : NULL;
    if (config == NULL) continue;
    config_free(config);
    disown(s, config);
    s->cached[index] = NULL;
  }
  s->cached_length = 0;
}
int64_t qa_weight_config_store_reference(qa_weight_config_store_bots_a *s,
                                         const qa_weight_config_bots_a *config) {
  for (size_t i = 0; i < s->owned_count; i++)
    if (s->owned[i] == config) return (int64_t)i;
  qa_ui_script_plain_error("Saved weight configuration belongs to another owner");
}
qa_weight_config_bots_a *qa_weight_config_store_resolve(qa_weight_config_store_bots_a *s, double reference) {
  if (trunc(reference) != reference || reference < 0 || reference >= (double)s->owned_count)
    qa_ui_script_plain_error("Saved weight configuration reference is missing");
  return s->owned[(size_t)reference];
}
qa_weight_config_bots_a *const *qa_weight_config_store_configurations(
    const qa_weight_config_store_bots_a *s, size_t *out_count) {
  *out_count = s->owned_count;
  return s->owned;
}
qa_save_value *qa_weight_config_store_checkpoint(qa_weight_config_store_bots_a *s,
                                                 qa_bot_memory_capture *memory) {
  qa_save_value *object = lib_sv(qa_save_value_object());
  lib_set_num(object, "generation", (double)s->generation);
  qa_save_value *heap = lib_sv(qa_save_value_object());
  lib_set_num(heap, "nextPointer", (double)s->next_pointer);
  qa_save_value *pointers = lib_sv(qa_save_value_array());
  for (size_t pointer = 0; pointer < s->heap_capacity; pointer++) {
    qa_weight_heap_entry *entry = &s->heap[pointer];
    if (entry->kind == 0) continue;
    qa_save_value *cell = lib_sv(qa_save_value_object());
    lib_set_num(cell, "pointer", (double)pointer);
    lib_set(cell, "kind", qa_save_value_string(entry->kind == KIND_NAME ? "name" : "separator"));
    lib_set_num(cell, "allocation", (double)qa_bot_memory_capture_reference(memory, &entry->allocation));
    lib_push(pointers, cell);
  }
  lib_set(heap, "pointers", pointers);
  lib_set(object, "heap", heap);
  qa_save_value *configs = lib_sv(qa_save_value_array());
  for (size_t i = 0; i < s->owned_count; i++) {
    qa_weight_config_bots_a *config = s->owned[i];
    require_open(config);
    qa_save_value *cell = lib_sv(qa_save_value_object());
    lib_set(cell, "path", qa_save_value_string(config->path));
    lib_set_num(cell, "allocation", (double)qa_bot_memory_capture_reference(memory, &config->allocation));
    lib_set(cell, "reported", lib_diagnostics_value(&config->reported));
    lib_push(configs, cell);
  }
  lib_set(object, "configs", configs);
  qa_save_value *cached = lib_sv(qa_save_value_array());
  for (size_t index = 0; index < s->cached_length; index++) {
    if (s->cached[index] == NULL) {
      lib_push(cached, qa_save_value_null());
      continue;
    }
    double position = -1;
    for (size_t i = 0; i < s->owned_count; i++)
      if (s->owned[i] == s->cached[index]) position = (double)i;
    lib_push(cached, qa_save_value_number(position));
  }
  lib_set(object, "cached", cached);
  return object;
}
static void heap_restore(qa_weight_config_store_bots_a *s, const qa_save_value *value,
                         qa_bot_memory_restored *memory) {
  qa_save_reader reader, pointers;
  if (!qa_save_reader_init(&reader, value, "bot.weights.heap")) qa_range_error("out of memory");
  double next = lib_int_min(&reader, "nextPointer", 1);
  lib_field(&reader, "pointers", &pointers);
  bool any = false;
  for (size_t i = 0; i < s->heap_capacity && !any; i++) any = s->heap[i].kind != 0;
  if (any || next < 1 || next > 4294967296.0) qa_ui_script_plain_error("Invalid fuzzy heap restoration");
  size_t count = qa_save_reader_list_count(&pointers);
  static const char *const kinds[] = {"name", "separator"};
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry, kind_reader;
    lib_item(&pointers, i, &entry);
    double pointer = lib_int_min(&entry, "pointer", 1);
    lib_field(&entry, "kind", &kind_reader);
    const char *kind = qa_save_reader_choice_string(&kind_reader, kinds, 2);
    bool is_name = strcmp(kind, "name") == 0;
    qa_save_reader_destroy(&kind_reader);
    double reference = lib_int_min(&entry, "allocation", 0);
    qa_save_reader_destroy(&entry);
    if (pointer < 1 || pointer >= next ||
        ((size_t)pointer < s->heap_capacity && s->heap[(size_t)pointer].kind != 0))
      qa_ui_script_plain_error("Invalid saved fuzzy pointer");
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (is_name) {
      if (memchr(allocation.bytes, 0, allocation.length) == NULL)
        qa_ui_script_plain_error("Saved fuzzy name is unterminated");
    } else if (allocation.length != FUZZY_SEPARATOR_BYTES) {
      qa_ui_script_plain_error("Saved fuzzy separator size mismatch");
    }
    heap_set(s, (int64_t)pointer, is_name ? KIND_NAME : KIND_SEPARATOR, allocation);
  }
  s->next_pointer = (int64_t)next;
  /* Depth-first cycle check: 1 pending, 2 visited. */
  uint8_t *marks = lib_alloc(s->heap_capacity + 1);
  sep_t *stack = NULL;
  size_t depth = 0, stack_capacity = 0;
  uint8_t *stages = lib_alloc(s->heap_capacity + 1);
  for (size_t root = 0; root < s->heap_capacity; root++) {
    if (s->heap[root].kind != KIND_SEPARATOR || marks[root] != 0) continue;
    /* Iterative walk over (separator, stage) preserves the recursive visit's order. */
    if (stack_capacity < 2) {
      stack_capacity = 64;
      stack = lib_realloc(stack, stack_capacity * sizeof *stack);
    }
    depth = 0;
    stack[depth++] = (sep_t)root;
    marks[root] = 1;
    while (depth > 0) {
      sep_t current = stack[depth - 1];
      const uint8_t *b = sep_bytes(s, current);
      sep_t following = stages[current] == 0 ? S_CHILD(b) : stages[current] == 1 ? S_NEXT(b) : 0;
      bool finished = stages[current] >= 2;
      stages[current]++;
      if (finished) {
        marks[current] = 2;
        depth--;
        continue;
      }
      if (following == 0) continue;
      (void)sep_bytes(s, following);
      if (marks[following] == 2) continue;
      if (marks[following] == 1) qa_ui_script_plain_error("Saved fuzzy separator graph has a cycle");
      marks[following] = 1;
      if (depth == stack_capacity) {
        stack_capacity *= 2;
        stack = lib_realloc(stack, stack_capacity * sizeof *stack);
      }
      stack[depth++] = following;
    }
  }
  free(stages);
  free(stack);
  free(marks);
  qa_save_reader_destroy(&pointers);
  qa_save_reader_destroy(&reader);
}
void qa_weight_config_store_restore(qa_weight_config_store_bots_a *s, const qa_save_value *value,
                                    qa_bot_memory_restored *memory) {
  qa_save_reader reader, heap, configs, cached;
  if (!qa_save_reader_init(&reader, value, "bot.weights")) qa_range_error("out of memory");
  double generation = lib_int_min(&reader, "generation", 0);
  lib_field(&reader, "heap", &heap);
  lib_field(&reader, "configs", &configs);
  lib_field(&reader, "cached", &cached);
  size_t cached_count = qa_save_reader_list_count(&cached);
  if (s->owned_count != 0 || s->cached_length != 0 || generation < 0 ||
      (int64_t)cached_count > s->max_cached_configs)
    qa_ui_script_plain_error("Invalid weight configuration restoration");
  heap_restore(s, heap.value, memory);
  size_t config_total = qa_save_reader_list_count(&configs);
  qa_weight_config_bots_a **restored = lib_alloc(config_total * sizeof *restored);
  for (size_t i = 0; i < config_total; i++) {
    qa_save_reader entry;
    lib_item(&configs, i, &entry);
    double reference = lib_int_min(&entry, "allocation", 0);
    char *path = lib_read_string(&entry, "path");
    qa_bot_memory_allocation allocation = qa_bot_memory_restored_allocation(memory, (int64_t)reference);
    if (allocation.length != WEIGHT_CONFIG_BYTES)
      qa_ui_script_plain_error("Saved weight configuration allocation size mismatch");
    qa_weight_config_bots_a *config = config_create(s, path, allocation, NULL);
    free(path);
    lib_read_diagnostics(&entry, "reported", &config->reported);
    qa_save_reader_destroy(&entry);
    (void)qa_weight_config_max_inventory_index(config);
    restored[i] = config;
  }
  for (size_t index = 0; index < cached_count; index++) {
    qa_save_reader entry;
    lib_item(&cached, index, &entry);
    if (!qa_save_reader_is_null(&entry)) {
      double position = qa_save_reader_integer_min(&entry, 0);
      if (position >= (double)config_total)
        qa_ui_script_plain_error("Saved weight cache reference is missing");
      s->cached[index] = restored[(size_t)position];
    } else {
      s->cached[index] = NULL;
    }
    qa_save_reader_destroy(&entry);
  }
  for (size_t i = 0; i < config_total; i++) own(s, restored[i]);
  s->cached_length = cached_count;
  s->generation = (int64_t)generation;
  free(restored);
  qa_save_reader_destroy(&heap);
  qa_save_reader_destroy(&configs);
  qa_save_reader_destroy(&cached);
  qa_save_reader_destroy(&reader);
}
