/* See include/qa/ui/common/legacy/script/preprocessor.h.
 * The TS callbackAborts distinction (a user callback throwing its
 * own ScriptLanguageError) has no C counterpart: callbacks cannot
 * throw through the fail channel. SourceReadFalse rides its own
 * setjmp stack in the engine. GPL-2.0-or-later. */
#include "qa/ui/common/legacy/script/preprocessor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "qa/core/commands/text.h"
#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"
#include "qa/ui/common/legacy/script/source_memory.h"

#define DEFAULT_LIMIT 9007199254740991LL
#define MAX_DEFINE_PARAMETERS 128

static char *copy_text(const char *text, size_t length) {
  char *result = malloc(length + 1);
  if (result != NULL) {
    memcpy(result, text, length);
    result[length] = '\0';
  }
  return result;
}
static char *copy_cstring(const char *text) {
  return copy_text(text, strlen(text));
}
static void oom_fail(void) {
  qa_range_error("script preprocessor allocation failed");
}

/* ---- evaluationDecimal: sprintf("%1.2f", fabs(v)) profile ---- */

typedef struct bignum {
  uint32_t limbs[40]; /* little-endian, enough for 2^1024 * 10^6 */
  size_t count;
} bignum;

static void big_from_u64(bignum *big, uint64_t value) {
  big->limbs[0] = (uint32_t)value;
  big->limbs[1] = (uint32_t)(value >> 32);
  big->count = big->limbs[1] != 0 ? 2 : 1;
}
static void big_mul_small(bignum *big, uint32_t factor) {
  uint64_t carry = 0;
  for (size_t i = 0; i < big->count; i++) {
    uint64_t product = (uint64_t)big->limbs[i] * factor + carry;
    big->limbs[i] = (uint32_t)product;
    carry = product >> 32;
  }
  while (carry != 0) {
    big->limbs[big->count++] = (uint32_t)carry;
    carry >>= 32;
  }
}
static void big_shl(bignum *big, unsigned bits) {
  unsigned words = bits / 32, rem = bits % 32;
  size_t old = big->count;
  size_t total = old + words + (rem != 0 ? 1 : 0);
  for (size_t i = total; i-- > 0;) {
    uint32_t value = 0;
    if (i >= words && i - words < old)
      value |= big->limbs[i - words] << rem;
    if (rem != 0 && i > words && i - words - 1 < old)
      value |= big->limbs[i - words - 1] >> (32 - rem);
    big->limbs[i] = value;
  }
  big->count = total;
  while (big->count > 1 && big->limbs[big->count - 1] == 0) big->count--;
}
static int big_bit(const bignum *big, unsigned bit) {
  size_t limb = bit / 32;
  if (limb >= big->count) return 0;
  return (int)((big->limbs[limb] >> (bit % 32)) & 1u);
}
static bool big_any_below(const bignum *big, unsigned bit) {
  for (unsigned i = 0; i < bit; i++)
    if (big_bit(big, i)) return true;
  return false;
}
static void big_shr(bignum *big, unsigned bits) {
  unsigned words = bits / 32, rem = bits % 32;
  if (words >= big->count) {
    big->limbs[0] = 0;
    big->count = 1;
    return;
  }
  for (size_t i = 0; i + words < big->count; i++) {
    uint32_t low = big->limbs[i + words] >> rem;
    uint32_t high = rem == 0 || i + words + 1 >= big->count
                        ? 0
                        : big->limbs[i + words + 1] << (32 - rem);
    big->limbs[i] = low | high;
  }
  big->count -= words;
  while (big->count > 1 && big->limbs[big->count - 1] == 0) big->count--;
}
static void big_add_one(bignum *big) {
  for (size_t i = 0; i < big->count; i++) {
    if (++big->limbs[i] != 0) return;
  }
  big->limbs[big->count++] = 1;
}
static uint32_t big_divmod_small(bignum *big, uint32_t divisor) {
  uint64_t remainder = 0;
  for (size_t i = big->count; i-- > 0;) {
    uint64_t current = (remainder << 32) | big->limbs[i];
    big->limbs[i] = (uint32_t)(current / divisor);
    remainder = current % divisor;
  }
  while (big->count > 1 && big->limbs[big->count - 1] == 0) big->count--;
  return (uint32_t)remainder;
}

/* Malloc'd decimal of round(magnitude * 10^precision) / 10^precision. */
static char *evaluation_decimal(double magnitude, int precision) {
  uint64_t bits = qa_float64_bits(magnitude);
  uint32_t exponent = (uint32_t)((bits >> 52) & 0x7ff);
  uint64_t fraction = bits & 0xfffffffffffffull;
  if (exponent == 0x7ff) return copy_cstring(fraction == 0 ? "inf" : "nan");
  uint64_t significand =
      exponent == 0 ? fraction : fraction | 0x10000000000000ull;
  int shift = exponent == 0 ? -1074 : (int)exponent - 1075;
  uint32_t scale = 1;
  for (int i = 0; i < precision; i++) scale *= 10;
  bignum big;
  memset(&big, 0, sizeof big);
  big_from_u64(&big, significand);
  big_mul_small(&big, scale);
  if (shift >= 0) {
    big_shl(&big, (unsigned)shift);
  } else {
    unsigned down = (unsigned)-shift;
    bool half = big_bit(&big, down - 1) != 0;
    bool below = big_any_below(&big, down - 1);
    bool greater = half && below;
    bool equal = half && !below;
    bool lower_odd = big_bit(&big, down) != 0;
    big_shr(&big, down);
    if (greater || (equal && lower_odd)) big_add_one(&big);
  }
  /* Decimal digits, most significant last in the scratch buffer. */
  char scratch[400];
  size_t digits = 0;
  if (big.count == 1 && big.limbs[0] == 0) scratch[digits++] = '0';
  while (!(big.count == 1 && big.limbs[0] == 0)) {
    uint32_t chunk = big_divmod_small(&big, 1000000000u);
    for (int i = 0; i < 9; i++) {
      scratch[digits++] = (char)('0' + chunk % 10);
      chunk /= 10;
    }
  }
  while (digits > 1 && scratch[digits - 1] == '0') digits--;
  size_t int_digits = digits > (size_t)precision
                          ? digits - (size_t)precision
                          : 0;
  size_t out_length = (int_digits == 0 ? 1 : int_digits) + 1
                      + (size_t)precision;
  char *result = malloc(out_length + 1);
  if (result == NULL) return NULL;
  size_t at = 0;
  if (int_digits == 0) result[at++] = '0';
  else
    for (size_t i = 0; i < int_digits; i++)
      result[at++] = scratch[digits - 1 - i];
  result[at++] = '.';
  for (int i = precision - 1; i >= 0; i--)
    result[at++] = (size_t)i < digits ? scratch[i] : '0';
  result[at] = '\0';
  return result;
}

typedef struct eval_value {
  int64_t integer_value;
  double float_value;
} eval_value;

/* Malloc'd DEBUG_EVAL value text. */
static char *debug_evaluation_value(eval_value value, bool integer_mode) {
  if (integer_mode) {
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%lld", (long long)value.integer_value);
    return copy_cstring(buffer);
  }
  double number = value.float_value;
  char *decimal = evaluation_decimal(fabs(number), 6);
  if (decimal == NULL) return NULL;
  bool negative = number < 0 || signbit(number);
  if (!negative) return decimal;
  char *result = malloc(strlen(decimal) + 2);
  if (result == NULL) {
    free(decimal);
    return NULL;
  }
  result[0] = '-';
  strcpy(result + 1, decimal);
  free(decimal);
  return result;
}

static int64_t signed_expression_value(double value) {
  if (value != trunc(value) || value < -2147483648.0
      || value > 2147483647.0)
    qa_range_error("preprocessor operation exceeds the source signed-long range");
  return value == 0 ? 0 : (int64_t)value;
}

/* ---- engine types ---- */

typedef struct rf_catch {
  jmp_buf buf;
  struct rf_catch *prev;
} rf_catch;

typedef struct source_frame {
  int64_t id;
  qa_ui_script_lexer *lexer;
  int64_t diagnostic_next;
  int64_t token_count;
} source_frame;

typedef struct token_chain {
  int64_t first, last, count;
} token_chain;

typedef struct limits {
  int64_t include_depth, macro_expansions, queued_tokens, output_tokens;
  int64_t source_tokens, defines, expression_tokens;
} limits;

typedef struct preprocessor_engine preprocessor_engine;
struct preprocessor_engine {
  qa_ui_script_resolve_fn resolve;
  void *resolve_ctx;
  limits limit;
  qa_ui_script_now_fn now;
  void *now_ctx;
  qa_ui_script_report_fn report;
  void *report_ctx;
  qa_ui_script_write_fn debug_eval;
  void *debug_eval_ctx;
  const qa_ui_script_memory *memory;
  bool has_memory;
  qa_ui_script_diagnostic *reported;
  size_t reported_count, reported_cap;
  qa_ui_source_record *source;
  qa_ui_precomp_memory *heap;
  bool owns_heap;
  /* MacroTable entries (define IDs). */
  int64_t *macro_entries;
  size_t macro_count, macro_cap;
  source_frame **frames; /* index id-1 */
  size_t frame_count, frame_cap;
  size_t live_frames;
  int64_t next_frame;
  source_frame *root_frame;
  qa_ui_precomp_token *cursor_token;
  qa_ui_precomp_token *output;
  int64_t expansion_count, output_count;
  bool disposed;
  bool reading_async;
  qa_ui_script_validate_fn async_validate;
  void *async_validate_ctx;
  int lifetime; /* 0 source, 1 global */
  qa_ui_script_fail_env env;
  rf_catch *rf_top;
};

static int64_t positive_limit(int64_t value, const char *label) {
  if (value == 0) return DEFAULT_LIMIT;
  if (value < 1) qa_range_error("%s must be a positive integer", label);
  return value;
}

static struct limits make_limits(
    const qa_ui_script_preprocessor_options *options) {
  struct limits result;
  result.include_depth =
      positive_limit(options->max_include_depth, "maxIncludeDepth");
  result.macro_expansions =
      positive_limit(options->max_macro_expansions, "maxMacroExpansions");
  result.queued_tokens =
      positive_limit(options->max_queued_tokens, "maxQueuedTokens");
  result.output_tokens =
      positive_limit(options->max_output_tokens, "maxOutputTokens");
  result.source_tokens =
      positive_limit(options->max_source_tokens, "maxSourceTokens");
  result.defines = positive_limit(options->max_defines, "maxDefines");
  result.expression_tokens = positive_limit(options->max_expression_tokens,
                                            "maxExpressionTokens");
  return result;
}

static bool push_reported(preprocessor_engine *engine,
                          qa_ui_script_diagnostic *diagnostic) {
  if (engine->reported_count == engine->reported_cap) {
    size_t cap = engine->reported_cap == 0 ? 8 : engine->reported_cap * 2;
    qa_ui_script_diagnostic *grown =
        realloc(engine->reported, cap * sizeof *grown);
    if (grown == NULL) return false;
    engine->reported = grown;
    engine->reported_cap = cap;
  }
  engine->reported[engine->reported_count++] = *diagnostic;
  return true;
}

static bool copy_diagnostic(const qa_ui_script_diagnostic *from,
                            qa_ui_script_diagnostic *to) {
  memset(to, 0, sizeof *to);
  to->severity = from->severity;
  to->message = copy_cstring(from->message);
  to->location.path = copy_cstring(from->location.path);
  to->location.line = from->location.line;
  to->location.column = from->location.column;
  if (to->message == NULL || to->location.path == NULL) {
    qa_ui_script_diagnostic_destroy(to);
    return false;
  }
  return true;
}

static void record_diagnostic(preprocessor_engine *engine,
                              const qa_ui_script_diagnostic *diagnostic) {
  qa_ui_script_diagnostic copy;
  if (!copy_diagnostic(diagnostic, &copy) || !push_reported(engine, &copy)) {
    qa_ui_script_diagnostic_destroy(&copy);
    oom_fail();
  }
  if (engine->report != NULL)
    engine->report(engine->report_ctx,
                   &engine->reported[engine->reported_count - 1]);
}

static void engine_report_trampoline(
    void *ctx, const qa_ui_script_diagnostic *diagnostic) {
  record_diagnostic(ctx, diagnostic);
}

static source_frame *frame_of(preprocessor_engine *engine, int64_t id) {
  if (id < 1 || id > (int64_t)engine->frame_count
      || engine->frames[id - 1] == NULL)
    qa_range_error("source script pointer does not identify a live script");
  return engine->frames[id - 1];
}
static source_frame *current_frame(preprocessor_engine *engine) {
  return frame_of(engine, qa_ui_source_record_script(engine->source));
}

static void current_location(preprocessor_engine *engine,
                             qa_ui_script_location *out) {
  const char *path;
  int64_t line, column;
  qa_ui_script_lexer_current_location(current_frame(engine)->lexer, &path,
                                      &line, &column);
  memset(out, 0, sizeof *out);
  out->path = copy_cstring(path);
  out->line = line;
  out->column = column;
  if (out->path == NULL) oom_fail();
}

static void engine_diag(preprocessor_engine *engine,
                        qa_ui_script_severity severity,
                        const char *message) {
  qa_ui_script_diagnostic diagnostic;
  memset(&diagnostic, 0, sizeof diagnostic);
  diagnostic.severity = severity;
  diagnostic.message = copy_cstring(message);
  current_location(engine, &diagnostic.location);
  if (diagnostic.message == NULL) oom_fail();
  record_diagnostic(engine, &diagnostic);
  qa_ui_script_diagnostic_destroy(&diagnostic);
}
static void engine_warn(preprocessor_engine *engine, const char *message) {
  engine_diag(engine, QA_UI_SCRIPT_SEVERITY_WARNING, message);
}
static void engine_error(preprocessor_engine *engine, const char *message) {
  engine_diag(engine, QA_UI_SCRIPT_SEVERITY_ERROR, message);
}

_Noreturn static void engine_fail(preprocessor_engine *engine,
                                  const char *message) {
  qa_ui_script_diagnostic diagnostic;
  memset(&diagnostic, 0, sizeof diagnostic);
  diagnostic.severity = QA_UI_SCRIPT_SEVERITY_ERROR;
  diagnostic.message = copy_cstring(message);
  current_location(engine, &diagnostic.location);
  if (diagnostic.message == NULL) oom_fail();
  record_diagnostic(engine, &diagnostic);
  const qa_ui_script_diagnostic *identity =
      &engine->reported[engine->reported_count - 1];
  qa_ui_script_fail_throw(&engine->env, &diagnostic, identity);
}

_Noreturn static void throw_read_false(preprocessor_engine *engine) {
  if (engine->rf_top == NULL)
    qa_ui_script_plain_error("source read failed outside a token operation");
  longjmp(engine->rf_top->buf, 1);
}

static void require_live(preprocessor_engine *engine) {
  if (engine->disposed) qa_ui_script_plain_error("Script source has been freed");
}


/* ---- MacroTable ---- */

static int64_t macro_hash(const char *name, size_t length) {
  int32_t hash = 0;
  for (size_t index = 0; index < length; index++) {
    int character = (uint8_t)name[index];
    if (character == 0) break;
    int32_t signed_char = character < 128 ? character : character - 256;
    hash = (int32_t)(hash + signed_char * (int32_t)(119 + index));
  }
  return (hash ^ (hash >> 10) ^ (hash >> 20))
         & (QA_UI_SOURCE_DEFINE_HASH_BUCKETS - 1);
}

static qa_ui_precomp_define *macro_get(preprocessor_engine *engine,
                                       const char *name, size_t length) {
  int64_t remaining = (int64_t)engine->macro_count + 1;
  int64_t bucket = macro_hash(name, length);
  for (int64_t id = qa_ui_source_record_hash_head(engine->source, bucket);
       id != 0;) {
    if (--remaining == 0)
      qa_range_error("source define hash contains a cycle");
    qa_ui_precomp_define *entry =
        qa_ui_precomp_memory_define(engine->heap, id);
    char entry_name[1056];
    size_t entry_length =
        qa_ui_precomp_define_name_into(entry, entry_name, sizeof entry_name);
    if (entry_length == length
        && memcmp(entry_name, name, length) == 0)
      return entry;
    id = qa_ui_precomp_define_hashnext(entry);
  }
  return NULL;
}
static bool macro_has(preprocessor_engine *engine, const char *name,
                      size_t length) {
  return macro_get(engine, name, length) != NULL;
}
static void macro_prepend(preprocessor_engine *engine,
                          qa_ui_precomp_define *macro) {
  char *name = qa_ui_precomp_define_name(macro);
  if (name == NULL) oom_fail();
  int64_t bucket = macro_hash(name, strlen(name));
  free(name);
  qa_ui_precomp_define_set_hashnext(
      macro, qa_ui_source_record_hash_head(engine->source, bucket));
  if (engine->macro_count == engine->macro_cap) {
    size_t cap = engine->macro_cap == 0 ? 8 : engine->macro_cap * 2;
    int64_t *grown = realloc(engine->macro_entries, cap * sizeof *grown);
    if (grown == NULL) oom_fail();
    engine->macro_entries = grown;
    engine->macro_cap = cap;
  }
  engine->macro_entries[engine->macro_count++] =
      qa_ui_precomp_define_id(macro);
  qa_ui_source_record_set_hash_head(engine->source, bucket,
                                    (uint32_t)qa_ui_precomp_define_id(macro));
}
static void macro_delete(preprocessor_engine *engine, const char *name,
                         size_t length) {
  int64_t bucket = macro_hash(name, length);
  qa_ui_precomp_define *previous = NULL;
  int64_t remaining = (int64_t)engine->macro_count + 1;
  for (int64_t id = qa_ui_source_record_hash_head(engine->source, bucket);
       id != 0;) {
    if (--remaining == 0)
      qa_range_error("source define hash contains a cycle");
    qa_ui_precomp_define *entry =
        qa_ui_precomp_memory_define(engine->heap, id);
    char entry_name[1056];
    size_t entry_length =
        qa_ui_precomp_define_name_into(entry, entry_name, sizeof entry_name);
    if (entry_length == length && memcmp(entry_name, name, length) == 0) {
      if (previous == NULL)
        qa_ui_source_record_set_hash_head(
            engine->source, bucket, qa_ui_precomp_define_hashnext(entry));
      else
        qa_ui_precomp_define_set_hashnext(
            previous, qa_ui_precomp_define_hashnext(entry));
      qa_ui_precomp_memory_free_define(engine->heap, entry);
      for (size_t i = 0; i < engine->macro_count; i++) {
        if (engine->macro_entries[i] == id) {
          memmove(&engine->macro_entries[i], &engine->macro_entries[i + 1],
                  (engine->macro_count - i - 1) * sizeof(int64_t));
          engine->macro_count--;
          break;
        }
      }
      return;
    }
    previous = entry;
    id = qa_ui_precomp_define_hashnext(entry);
  }
}
static qa_ui_precomp_define *macro_first(preprocessor_engine *engine) {
  for (int64_t bucket = 0; bucket < QA_UI_SOURCE_DEFINE_HASH_BUCKETS;
       bucket++) {
    int64_t id = qa_ui_source_record_hash_head(engine->source, bucket);
    if (id != 0) return qa_ui_precomp_memory_define(engine->heap, id);
  }
  return NULL;
}
static void macro_clear(preprocessor_engine *engine) {
  for (int64_t bucket = 0; bucket < QA_UI_SOURCE_DEFINE_HASH_BUCKETS;
       bucket++) {
    int64_t id;
    while ((id = qa_ui_source_record_hash_head(engine->source, bucket))
           != 0) {
      qa_ui_precomp_define *entry =
          qa_ui_precomp_memory_define(engine->heap, id);
      qa_ui_source_record_set_hash_head(
          engine->source, bucket, qa_ui_precomp_define_hashnext(entry));
      qa_ui_precomp_memory_free_define(engine->heap, entry);
      for (size_t i = 0; i < engine->macro_count; i++) {
        if (engine->macro_entries[i] == id) {
          memmove(&engine->macro_entries[i], &engine->macro_entries[i + 1],
                  (engine->macro_count - i - 1) * sizeof(int64_t));
          engine->macro_count--;
          break;
        }
      }
    }
  }
}

/* ---- frames and token plumbing ---- */

static bool read_token(preprocessor_engine *engine,
                       qa_ui_precomp_token *output,
                       qa_ui_script_token **out);
static void directive(preprocessor_engine *engine);
static void dollar_directive(preprocessor_engine *engine);
static bool expand_macro_into_source(preprocessor_engine *engine,
                                     qa_ui_script_token *invocation);
static qa_ui_precomp_define *define_from_string(
    const char *text, const qa_ui_script_memory *memory, bool has_memory,
    qa_ui_precomp_memory *heap, qa_ui_script_report_fn report,
    void *report_ctx);

static bool grow_frames(preprocessor_engine *engine, size_t need) {
  if (need <= engine->frame_cap) return true;
  size_t cap = engine->frame_cap == 0 ? 4 : engine->frame_cap;
  while (cap < need) cap *= 2;
  source_frame **grown = realloc(engine->frames, cap * sizeof *grown);
  if (grown == NULL) return false;
  for (size_t i = engine->frame_cap; i < cap; i++) grown[i] = NULL;
  engine->frames = grown;
  engine->frame_cap = cap;
  return true;
}

static source_frame *create_frame(preprocessor_engine *engine,
                                  const qa_ui_script_source *source) {
  qa_ui_source_script_storage *storage = source->storage;
  if (storage == NULL && engine->has_memory) {
    storage = qa_ui_script_allocate_source((int64_t)source->text_length,
                                           source->path, engine->memory);
    if (storage == NULL) oom_fail();
    qa_ui_source_script_copy_text(storage, source->text,
                                  source->text_length);
  }
  qa_ui_script_lexer_options options;
  memset(&options, 0, sizeof options);
  options.report = engine_report_trampoline;
  options.report_ctx = engine;
  qa_ui_script_lexer *lexer =
      storage != NULL
          ? qa_ui_script_lexer_create_storage_env(storage, source->path,
                                                  &options, &engine->env)
          : qa_ui_script_lexer_create_text_env(
                source->text, source->text_length, source->path, &options,
                &engine->env);
  if (lexer == NULL) oom_fail();
  source_frame *frame = calloc(1, sizeof *frame);
  if (frame == NULL) oom_fail();
  frame->id = engine->next_frame++;
  frame->lexer = lexer;
  if (!grow_frames(engine, (size_t)frame->id)) oom_fail();
  engine->frames[frame->id - 1] = frame;
  if ((size_t)frame->id > engine->frame_count)
    engine->frame_count = (size_t)frame->id;
  engine->live_frames++;
  return frame;
}

static int64_t next_frame_id(source_frame *frame) {
  qa_ui_source_script_storage *storage =
      qa_ui_script_lexer_source_storage(frame->lexer);
  return storage == NULL ? frame->diagnostic_next
                         : qa_ui_source_script_next_script(storage);
}
static void set_next_frame(source_frame *frame, int64_t next) {
  qa_ui_source_script_storage *storage =
      qa_ui_script_lexer_source_storage(frame->lexer);
  if (storage == NULL) frame->diagnostic_next = next;
  else qa_ui_source_script_set_next_script(storage, (uint32_t)next);
}

static void set_token_metadata(qa_ui_script_token *token, bool uninitialized,
                               int64_t subtype, uint32_t integer_value,
                               double float_value, const char *reason) {
  token->has_metadata = true;
  token->metadata_uninitialized = uninitialized;
  token->metadata_subtype = subtype;
  token->metadata_integer_value = integer_value;
  token->metadata_float_value = float_value;
  free(token->metadata_reason);
  token->metadata_reason = reason == NULL ? NULL : copy_cstring(reason);
  if (reason != NULL && token->metadata_reason == NULL) oom_fail();
}

/* Writes the TS TokenMetadata for `token` into out fields. */
static void token_metadata(qa_ui_script_token *token, bool *uninitialized,
                           int64_t *subtype, uint32_t *integer_value,
                           double *float_value, const char **reason) {
  if (token->has_metadata) {
    *uninitialized = token->metadata_uninitialized;
    *subtype = token->metadata_subtype;
    *integer_value = token->metadata_integer_value;
    *float_value = token->metadata_float_value;
    *reason = token->metadata_reason;
    return;
  }
  *uninitialized = false;
  *reason = NULL;
  switch (token->kind) {
    case QA_UI_SCRIPT_KIND_PRIMITIVE:
      *subtype = 0;
      *integer_value = 0;
      *float_value = 0;
      break;
    case QA_UI_SCRIPT_KIND_NUMBER:
      *subtype = token->flags;
      *integer_value = token->integer_value;
      *float_value = token->float_value;
      break;
    case QA_UI_SCRIPT_KIND_PUNCTUATION:
      *subtype = token->punctuation;
      *integer_value = 0;
      *float_value = 0;
      break;
    default:
      *subtype = token->length;
      *integer_value = 0;
      *float_value = 0;
      break;
  }
}

static qa_ui_script_token *snapshot_token(preprocessor_engine *engine,
                                          qa_ui_precomp_token *stored) {
  (void)engine;
  qa_ui_script_token_record record;
  if (!qa_ui_source_token_read_record(
          qa_ui_precomp_token_memory(stored),
          qa_ui_precomp_token_context(stored), false, 0, &record))
    oom_fail();
  const char *unsupported = qa_ui_precomp_token_unsupported(stored);
  if (unsupported == NULL) {
    set_token_metadata(record.token, false, record.subtype,
                       record.integer_value, record.float_value, NULL);
  } else {
    set_token_metadata(record.token, true, 0, 0, 0, unsupported);
  }
  return record.token;
}

static qa_ui_precomp_token *token_value(preprocessor_engine *engine,
                                        qa_ui_script_token *token) {
  (void)engine;
  qa_ui_precomp_token *stored = qa_ui_precomp_token_local();
  if (stored == NULL) oom_fail();
  qa_ui_source_token_write_token(qa_ui_precomp_token_memory(stored), token);
  bool uninitialized;
  int64_t subtype;
  uint32_t integer_value;
  double float_value;
  const char *reason;
  token_metadata(token, &uninitialized, &subtype, &integer_value,
                 &float_value, &reason);
  if (!uninitialized) {
    qa_ui_source_token_memory *memory = qa_ui_precomp_token_memory(stored);
    qa_ui_source_token_set_subtype(memory, (int32_t)subtype);
    qa_ui_source_token_set_integer_value(memory, integer_value);
    qa_ui_source_token_set_float_value(memory, float_value);
  } else if (!qa_ui_precomp_token_set_unsupported(stored, reason)) {
    oom_fail();
  }
  qa_ui_source_token_context context;
  memset(&context, 0, sizeof context);
  if (!qa_ui_source_token_context_set(
          &context, token->location.path, token->location.column,
          token->leading_whitespace, token->leading_whitespace_length))
    oom_fail();
  bool ok = qa_ui_precomp_token_set_context(stored, &context);
  qa_ui_source_token_context_destroy(&context);
  if (!ok) oom_fail();
  return stored;
}

static void unread_token(preprocessor_engine *engine,
                         qa_ui_script_token *token) {
  qa_ui_precomp_token *value = token_value(engine, token);
  qa_ui_precomp_token *copied =
      qa_ui_precomp_memory_copy_token(engine->heap, value);
  qa_ui_precomp_token_destroy(value);
  qa_ui_source_token_set_next(qa_ui_precomp_token_memory(copied),
                              qa_ui_source_record_tokens(engine->source));
  qa_ui_source_record_set_tokens(engine->source,
                                 (uint32_t)qa_ui_precomp_token_id(copied));
}

static void free_pending_tokens(preprocessor_engine *engine) {
  while (qa_ui_source_record_tokens(engine->source) != 0) {
    qa_ui_precomp_token *token = qa_ui_precomp_memory_token(
        engine->heap, qa_ui_source_record_tokens(engine->source));
    qa_ui_source_record_set_tokens(
        engine->source,
        qa_ui_source_token_next(qa_ui_precomp_token_memory(token)));
    qa_ui_precomp_memory_free_token(engine->heap, token);
  }
}

/* Cursor next over an explicit output token (TS readSourceToken). */
static bool read_source_token(preprocessor_engine *engine,
                              qa_ui_precomp_token *output,
                              qa_ui_script_token **out) {
  *out = NULL;
  while (qa_ui_source_record_tokens(engine->source) == 0) {
    source_frame *frame = current_frame(engine);
    qa_ui_script_token *volatile token = NULL;
    volatile bool failed = false;
    qa_ui_script_catch catch_block;
    catch_block.prev = engine->env.top;
    engine->env.top = &catch_block;
    if (setjmp(catch_block.buf) == 0) {
      qa_ui_script_token_record record;
      if (qa_ui_script_lexer_next_into(
              frame->lexer, qa_ui_precomp_token_memory(output), &record)) {
        token = record.token;
        qa_ui_source_token_context context;
        memset(&context, 0, sizeof context);
        if (!qa_ui_source_token_context_set(
                &context, token->location.path, token->location.column,
                token->leading_whitespace,
                token->leading_whitespace_length))
          oom_fail();
        bool ok = qa_ui_precomp_token_set_context(output, &context);
        qa_ui_source_token_context_destroy(&context);
        if (!ok || !qa_ui_precomp_token_set_unsupported(output, NULL))
          oom_fail();
      }
      engine->env.top = catch_block.prev;
    } else {
      engine->env.top = catch_block.prev;
      failed = true; /* pending stays for the rethrow below */
    }
    if (token != NULL) {
      if (frame->token_count >= engine->limit.source_tokens) {
        char message[128];
        snprintf(message, sizeof message,
                 "source exceeds the %lld-token limit",
                 (long long)engine->limit.source_tokens);
        qa_ui_script_token *doomed = (qa_ui_script_token *)token;
        qa_ui_script_token_release(doomed);
        engine_fail(engine, message);
      }
      frame->token_count++;
      qa_ui_script_token *source_token = snapshot_token(engine, output);
      qa_ui_script_token_release((qa_ui_script_token *)token);
      *out = source_token;
      return true;
    }
    if (qa_ui_script_lexer_end_of_script(current_frame(engine)->lexer)) {
      while (qa_ui_source_record_has_current_indent(engine->source)) {
        engine_warn(engine, "missing #endif");
        qa_ui_source_indent_type type;
        int64_t skip;
        qa_ui_source_record_pop_indent(engine->source, &type, &skip);
      }
    }
    source_frame *exhausted = current_frame(engine);
    int64_t next = next_frame_id(exhausted);
    if (next == 0) {
      if (failed) {
        /* Rethrow the lexer's failure as the engine's. */
        qa_ui_script_diagnostic pending = engine->env.pending;
        engine->env.has_pending = false;
        engine->env.pending_identity = NULL;
        memset(&engine->env.pending, 0, sizeof engine->env.pending);
        /* identity: the engine's copy recorded via the report cb */
        const qa_ui_script_diagnostic *identity =
            engine->reported_count > 0
                ? &engine->reported[engine->reported_count - 1]
                : NULL;
        qa_ui_script_fail_throw(&engine->env, &pending, identity);
      }
      return false;
    }
    if (failed) qa_ui_script_fail_env_clear_pending(&engine->env);
    qa_ui_source_record_set_script(engine->source, (uint32_t)next);
    engine->frames[exhausted->id - 1] = NULL;
    engine->live_frames--;
    qa_ui_script_lexer_dispose(exhausted->lexer);
    qa_ui_script_lexer_destroy(exhausted->lexer);
    free(exhausted);
  }
  qa_ui_precomp_token *token = qa_ui_precomp_memory_token(
      engine->heap, qa_ui_source_record_tokens(engine->source));
  qa_ui_precomp_token_copy_from(output, token);
  qa_ui_source_record_set_tokens(
      engine->source,
      qa_ui_source_token_next(qa_ui_precomp_token_memory(token)));
  qa_ui_precomp_memory_free_token(engine->heap, token);
  *out = snapshot_token(engine, output);
  return true;
}

static bool cursor_next(preprocessor_engine *engine,
                        qa_ui_script_token **out) {
  return read_source_token(engine, engine->cursor_token, out);
}

/* TS readLineToken over the engine cursor. */
static bool read_line_token(preprocessor_engine *engine,
                            qa_ui_script_token **out) {
  bool may_cross = false;
  for (;;) {
    qa_ui_script_token *token;
    if (!cursor_next(engine, &token)) return false;
    if (token->lines_crossed > (may_cross ? 1 : 0)) {
      unread_token(engine, token);
      qa_ui_script_token_release(token);
      return false;
    }
    if (token->text_length == 1 && token->text[0] == '\\') {
      may_cross = true;
      qa_ui_script_token_release(token);
      continue;
    }
    *out = token;
    return true;
  }
}

static bool is_punctuation(qa_ui_script_token *token, int64_t punctuation) {
  return token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION
         && token->punctuation == punctuation;
}

static bool token_text_is(qa_ui_script_token *token, const char *text) {
  size_t length = strlen(text);
  return token->text_length == length
         && memcmp(token->text, text, length) == 0;
}

/* ---- expression parser (TS ExpressionParser) ---- */

typedef struct expr_parser {
  preprocessor_engine *engine;
  qa_ui_script_token **tokens;
  size_t count;
  bool integer_mode;
} expr_parser;

static int64_t precedence_of(int64_t punctuation, bool *found) {
  *found = true;
  switch (punctuation) {
    case QA_UI_SCRIPT_PUNCT_MULTIPLY:
    case QA_UI_SCRIPT_PUNCT_DIVIDE:
    case QA_UI_SCRIPT_PUNCT_MODULO: return 15;
    case QA_UI_SCRIPT_PUNCT_ADD:
    case QA_UI_SCRIPT_PUNCT_SUBTRACT: return 14;
    case QA_UI_SCRIPT_PUNCT_RIGHT_SHIFT:
    case QA_UI_SCRIPT_PUNCT_LEFT_SHIFT: return 13;
    case QA_UI_SCRIPT_PUNCT_GREATER_OR_EQUAL:
    case QA_UI_SCRIPT_PUNCT_LESS_OR_EQUAL:
    case QA_UI_SCRIPT_PUNCT_GREATER:
    case QA_UI_SCRIPT_PUNCT_LESS: return 12;
    case QA_UI_SCRIPT_PUNCT_EQUAL:
    case QA_UI_SCRIPT_PUNCT_NOT_EQUAL: return 11;
    case QA_UI_SCRIPT_PUNCT_BINARY_AND: return 10;
    case QA_UI_SCRIPT_PUNCT_BINARY_XOR: return 9;
    case QA_UI_SCRIPT_PUNCT_BINARY_OR: return 8;
    case QA_UI_SCRIPT_PUNCT_LOGICAL_AND: return 7;
    case QA_UI_SCRIPT_PUNCT_LOGICAL_OR: return 6;
    case QA_UI_SCRIPT_PUNCT_QUESTION:
    case QA_UI_SCRIPT_PUNCT_COLON: return 5;
    default: *found = false; return 0;
  }
}

_Noreturn static void expr_fail(expr_parser *parser, const char *message) {
  engine_fail(parser->engine, message);
}

static void expr_require_integer(expr_parser *parser,
                                 qa_ui_script_token *operator) {
  if (!parser->integer_mode) {
    char message[1200];
    snprintf(message, sizeof message,
             "operator %s is invalid in a floating-point expression",
             operator->text);
    expr_fail(parser, message);
  }
}

static eval_value expr_binary(expr_parser *parser,
                              qa_ui_script_token *operator,
                              eval_value left, eval_value right) {
  eval_value result;
  switch (operator->punctuation) {
    case QA_UI_SCRIPT_PUNCT_MULTIPLY:
      result.integer_value = signed_expression_value(
          (double)left.integer_value * (double)right.integer_value);
      result.float_value = left.float_value * right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_DIVIDE:
      if (right.integer_value == 0 || right.float_value == 0)
        expr_fail(parser, "divide by zero in #if/#elif\n");
      result.integer_value = signed_expression_value(
          trunc((double)left.integer_value / (double)right.integer_value));
      result.float_value = left.float_value / right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_MODULO:
      expr_require_integer(parser, operator);
      if (right.integer_value == 0)
        expr_fail(parser, "divide by zero in #if/#elif\n");
      (void)signed_expression_value(
          trunc((double)left.integer_value / (double)right.integer_value));
      result.integer_value = signed_expression_value(
          (double)(left.integer_value % right.integer_value));
      result.float_value = left.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_ADD:
      result.integer_value = signed_expression_value(
          (double)left.integer_value + (double)right.integer_value);
      result.float_value = left.float_value + right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_SUBTRACT:
      result.integer_value = signed_expression_value(
          (double)left.integer_value - (double)right.integer_value);
      result.float_value = left.float_value - right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_LOGICAL_AND:
      result.integer_value =
          left.integer_value != 0 && right.integer_value != 0 ? 1 : 0;
      result.float_value =
          left.float_value != 0 && right.float_value != 0 ? 1 : 0;
      return result;
    case QA_UI_SCRIPT_PUNCT_LOGICAL_OR:
      result.integer_value =
          left.integer_value != 0 || right.integer_value != 0 ? 1 : 0;
      result.float_value =
          left.float_value != 0 || right.float_value != 0 ? 1 : 0;
      return result;
    case QA_UI_SCRIPT_PUNCT_GREATER_OR_EQUAL:
      result.integer_value = left.integer_value >= right.integer_value;
      result.float_value = left.float_value >= right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_LESS_OR_EQUAL:
      result.integer_value = left.integer_value <= right.integer_value;
      result.float_value = left.float_value <= right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_EQUAL:
      result.integer_value = left.integer_value == right.integer_value;
      result.float_value = left.float_value == right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_NOT_EQUAL:
      result.integer_value = left.integer_value != right.integer_value;
      result.float_value = left.float_value != right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_GREATER:
      result.integer_value = left.integer_value > right.integer_value;
      result.float_value = left.float_value > right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_LESS:
      result.integer_value = left.integer_value < right.integer_value;
      result.float_value = left.float_value < right.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_RIGHT_SHIFT:
      expr_require_integer(parser, operator);
      if (right.integer_value < 0 || right.integer_value >= 32)
        qa_range_error("source right shift count exceeds its signed-long width");
      result.integer_value =
          (int32_t)left.integer_value >> (int)right.integer_value;
      result.float_value = left.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_LEFT_SHIFT:
      expr_require_integer(parser, operator);
      if (right.integer_value < 0 || right.integer_value >= 32
          || left.integer_value < 0)
        qa_range_error("source signed left shift has an undefined operand");
      result.integer_value = signed_expression_value(
          (double)left.integer_value * ldexp(1.0, (int)right.integer_value));
      result.float_value = left.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_BINARY_AND:
      expr_require_integer(parser, operator);
      result.integer_value =
          (int32_t)((uint32_t)left.integer_value
                    & (uint32_t)right.integer_value);
      result.float_value = left.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_BINARY_OR:
      expr_require_integer(parser, operator);
      result.integer_value =
          (int32_t)((uint32_t)left.integer_value
                    | (uint32_t)right.integer_value);
      result.float_value = left.float_value;
      return result;
    case QA_UI_SCRIPT_PUNCT_BINARY_XOR:
      expr_require_integer(parser, operator);
      result.integer_value =
          (int32_t)((uint32_t)left.integer_value
                    ^ (uint32_t)right.integer_value);
      result.float_value = left.float_value;
      return result;
    default: {
      char message[1200];
      snprintf(message, sizeof message,
               "unsupported expression operator %s", operator->text);
      expr_fail(parser, message);
    }
  }
}

typedef struct expr_value_node {
  eval_value value;
  int previous, next;
} expr_value_node;
typedef struct expr_operator_node {
  qa_ui_script_token *token;
  int64_t parentheses, priority;
  int previous, next;
} expr_operator_node;

static void debug_write(preprocessor_engine *engine, const char *text) {
  if (engine->debug_eval != NULL)
    engine->debug_eval(engine->debug_eval_ctx, text);
}

static eval_value expr_evaluate(expr_parser *parser) {
  preprocessor_engine *engine = parser->engine;
  expr_value_node values[65];
  expr_operator_node operators[65];
  int value_count = 0, operator_count = 0;
  int values_first = -1, values_last = -1;
  int operators_first = -1, operators_last = -1;
  int64_t num_values = 0, num_operators = 0;
  int64_t parentheses = 0;
  bool last_was_value = false, negative = false;
  for (size_t index = 0; index < parser->count; index++) {
    qa_ui_script_token *token = parser->tokens[index];
    if (token->kind == QA_UI_SCRIPT_KIND_NAME) {
      if (last_was_value || negative)
        expr_fail(parser, "syntax error in #if/#elif");
      if (!token_text_is(token, "defined")) {
        char message[1200];
        snprintf(message, sizeof message, "undefined name %s in #if/#elif",
                 token->text);
        expr_fail(parser, message);
      }
      qa_ui_script_token *name =
          ++index < parser->count ? parser->tokens[index] : NULL;
      if (name == NULL)
        qa_range_error("defined reads a null source token before checking its name");
      bool brace = token_text_is(name, "(");
      if (brace) name = ++index < parser->count ? parser->tokens[index] : NULL;
      if (name == NULL || name->kind != QA_UI_SCRIPT_KIND_NAME)
        expr_fail(parser, "defined without name in #if/#elif");
      int64_t value = macro_has(engine, name->value, name->value_length)
                          ? 1
                          : 0;
      if (num_values++ >= 64) expr_fail(parser, "out of value space\n");
      values[value_count].value = (eval_value){value, (double)value};
      values[value_count].previous = values_last;
      values[value_count].next = -1;
      if (values_last < 0) values_first = value_count;
      else values[values_last].next = value_count;
      values_last = value_count++;
      last_was_value = true;
      if (brace) {
        qa_ui_script_token *close =
            ++index < parser->count ? parser->tokens[index] : NULL;
        if (close == NULL || !token_text_is(close, ")"))
          expr_fail(parser, "defined without ) in #if/#elif");
      }
    } else if (token->kind == QA_UI_SCRIPT_KIND_NUMBER) {
      if (last_was_value) expr_fail(parser, "syntax error in #if/#elif");
      int32_t integer = (int32_t)token->integer_value;
      eval_value value;
      if (negative) {
        value.integer_value = signed_expression_value(-(double)integer);
        value.float_value = -token->float_value;
      } else {
        value.integer_value = integer;
        value.float_value = token->float_value;
      }
      if (num_values++ >= 64) expr_fail(parser, "out of value space\n");
      values[value_count].value = value;
      values[value_count].previous = values_last;
      values[value_count].next = -1;
      if (values_last < 0) values_first = value_count;
      else values[values_last].next = value_count;
      values_last = value_count++;
      last_was_value = true;
      negative = false;
    } else if (token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION) {
      if (negative)
        expr_fail(parser, "misplaced minus sign in #if/#elif");
      int64_t op = token->punctuation;
      if (op == QA_UI_SCRIPT_PUNCT_PARENTHESIS_OPEN) {
        parentheses++;
        continue;
      }
      if (op == QA_UI_SCRIPT_PUNCT_PARENTHESIS_CLOSE) {
        if (--parentheses < 0)
          expr_fail(parser, "too many ) in #if/#elsif");
        continue;
      }
      if (!parser->integer_mode
          && (op == QA_UI_SCRIPT_PUNCT_BINARY_NOT
              || op == QA_UI_SCRIPT_PUNCT_MODULO
              || op == QA_UI_SCRIPT_PUNCT_RIGHT_SHIFT
              || op == QA_UI_SCRIPT_PUNCT_LEFT_SHIFT
              || op == QA_UI_SCRIPT_PUNCT_BINARY_AND
              || op == QA_UI_SCRIPT_PUNCT_BINARY_OR
              || op == QA_UI_SCRIPT_PUNCT_BINARY_XOR)) {
        char message[1200];
        snprintf(message, sizeof message,
                 "illigal operator %s on floating point operands\n",
                 token->text);
        expr_fail(parser, message);
      }
      if (op == QA_UI_SCRIPT_PUNCT_LOGICAL_NOT
          || op == QA_UI_SCRIPT_PUNCT_BINARY_NOT) {
        if (last_was_value)
          expr_fail(parser, "! or ~ after value in #if/#elif");
      } else if (op == QA_UI_SCRIPT_PUNCT_SUBTRACT && !last_was_value) {
        negative = true;
        continue;
      } else if (op == QA_UI_SCRIPT_PUNCT_INCREMENT
                 || op == QA_UI_SCRIPT_PUNCT_DECREMENT) {
        engine_error(engine, "++ or -- used in #if/#elif");
      } else {
        bool found;
        (void)precedence_of(op, &found);
        if (!found) {
          char message[1200];
          snprintf(message, sizeof message,
                   "invalid operator %s in #if/#elif", token->text);
          expr_fail(parser, message);
        }
        if (!last_was_value) {
          char message[1200];
          snprintf(message, sizeof message,
                   "operator %s after operator in #if/#elif", token->text);
          expr_fail(parser, message);
        }
      }
      if (num_operators++ >= 64)
        expr_fail(parser, "out of operator space\n");
      bool found;
      int64_t priority =
          op == QA_UI_SCRIPT_PUNCT_LOGICAL_NOT
                  || op == QA_UI_SCRIPT_PUNCT_BINARY_NOT
              ? 16
              : op == QA_UI_SCRIPT_PUNCT_INCREMENT
                        || op == QA_UI_SCRIPT_PUNCT_DECREMENT
                    ? 0
                    : precedence_of(op, &found);
      operators[operator_count].token = token;
      operators[operator_count].parentheses = parentheses;
      operators[operator_count].priority = priority;
      operators[operator_count].previous = operators_last;
      operators[operator_count].next = -1;
      if (operators_last < 0) operators_first = operator_count;
      else operators[operators_last].next = operator_count;
      operators_last = operator_count++;
      last_was_value = false;
    } else {
      char message[1200];
      snprintf(message, sizeof message, "unknown %s in #if/#elif",
               token->text);
      expr_fail(parser, message);
    }
  }
  if (!last_was_value) expr_fail(parser, "trailing operator in #if/#elif");
  if (parentheses != 0) expr_fail(parser, "too many ( in #if/#elif");
  bool has_question = false;
  eval_value question = {0, 0};
  while (operators_first >= 0) {
    int operator_index = operators_first;
    int value_index = values_first;
    while (operators[operator_index].next >= 0) {
      int next_operator = operators[operator_index].next;
      if (operators[operator_index].parentheses
              > operators[next_operator].parentheses
          || (operators[operator_index].parentheses
                  == operators[next_operator].parentheses
              && operators[operator_index].priority
                     >= operators[next_operator].priority))
        break;
      if (operators[operator_index].token->punctuation
              != QA_UI_SCRIPT_PUNCT_LOGICAL_NOT
          && operators[operator_index].token->punctuation
                 != QA_UI_SCRIPT_PUNCT_BINARY_NOT)
        value_index = value_index >= 0 ? values[value_index].next : -1;
      if (value_index < 0) expr_fail(parser, "mising values in #if/#elif");
      operator_index = next_operator;
    }
    if (value_index < 0)
      qa_range_error("source expression reduction dereferences a missing value");
    qa_ui_script_token *operator_token = operators[operator_index].token;
    int64_t op = operator_token->punctuation;
    if (engine->debug_eval != NULL) {
      char *text = debug_evaluation_value(values[value_index].value,
                                          parser->integer_mode);
      char line[1300];
      snprintf(line, sizeof line, "operator %s, value1 = %s",
               operator_token->text, text != NULL ? text : "");
      free(text);
      debug_write(engine, line);
      if (values[value_index].next >= 0) {
        text = debug_evaluation_value(
            values[values[value_index].next].value, parser->integer_mode);
        snprintf(line, sizeof line, "value2 = %s", text != NULL ? text : "");
        free(text);
        debug_write(engine, line);
      }
    }
    /* TS wraps the reduction to emit the debug result on failure;
     * failures abort or longjmp here, so the line is emitted by the
     * token-operation catch only when a catcher exists. */
    if (op == QA_UI_SCRIPT_PUNCT_LOGICAL_NOT) {
      values[value_index].value = (eval_value){
          values[value_index].value.integer_value == 0 ? 1 : 0,
          values[value_index].value.float_value == 0 ? 1 : 0};
    } else if (op == QA_UI_SCRIPT_PUNCT_BINARY_NOT) {
      values[value_index].value.integer_value =
          ~(int32_t)values[value_index].value.integer_value;
    } else {
      int next_index = values[value_index].next;
      int removed;
      if (op == QA_UI_SCRIPT_PUNCT_QUESTION) {
        if (has_question) expr_fail(parser, "? after ? in #if/#elif");
        has_question = true;
        question = values[value_index].value;
        removed = value_index;
      } else {
        if (next_index < 0)
          qa_range_error("source expression binary reduction dereferences a missing value");
        if (op == QA_UI_SCRIPT_PUNCT_COLON) {
          if (!has_question)
            expr_fail(parser, ": without ? in #if/#elif");
          if (parser->integer_mode) {
            values[value_index].value.integer_value =
                question.integer_value == 0
                    ? values[next_index].value.integer_value
                    : values[value_index].value.integer_value;
          } else {
            values[value_index].value.float_value =
                question.float_value == 0
                    ? values[next_index].value.float_value
                    : values[value_index].value.float_value;
          }
          has_question = false;
        } else if (op != QA_UI_SCRIPT_PUNCT_INCREMENT
                   && op != QA_UI_SCRIPT_PUNCT_DECREMENT) {
          values[value_index].value = expr_binary(
              parser, operator_token, values[value_index].value,
              values[next_index].value);
        }
        removed = next_index;
      }
      if (values[removed].previous < 0)
        values_first = values[removed].next;
      else values[values[removed].previous].next = values[removed].next;
      if (values[removed].next < 0)
        values_last = values[removed].previous;
      else values[values[removed].next].previous = values[removed].previous;
      if (removed == value_index) value_index = values[removed].next;
    }
    if (engine->debug_eval != NULL && value_index >= 0) {
      char *text = debug_evaluation_value(values[value_index].value,
                                          parser->integer_mode);
      char line[1300];
      snprintf(line, sizeof line, "result value = %s",
               text != NULL ? text : "");
      free(text);
      debug_write(engine, line);
    }
    if (operators[operator_index].previous < 0)
      operators_first = operators[operator_index].next;
    else
      operators[operators[operator_index].previous].next =
          operators[operator_index].next;
    if (operators[operator_index].next < 0)
      operators_last = operators[operator_index].previous;
    else
      operators[operators[operator_index].next].previous =
          operators[operator_index].previous;
  }
  if (values_first < 0)
    qa_range_error("source expression has no result value");
  return values[values_first].value;
}

/* ---- macro expansion ---- */

static void append_token(preprocessor_engine *engine, token_chain *list,
                         qa_ui_precomp_token *token) {
  if (list->count >= engine->limit.queued_tokens) {
    char message[160];
    snprintf(message, sizeof message,
             "macro expansion queue exceeds %lld tokens",
             (long long)engine->limit.queued_tokens);
    engine_fail(engine, message);
  }
  if (list->last == 0) list->first = qa_ui_precomp_token_id(token);
  else
    qa_ui_source_token_set_next(
        qa_ui_precomp_token_memory(
            qa_ui_precomp_memory_token(engine->heap, list->last)),
        (uint32_t)qa_ui_precomp_token_id(token));
  list->last = qa_ui_precomp_token_id(token);
  list->count++;
}

static void check_argument_capacity(preprocessor_engine *engine,
                                    int64_t count) {
  if (count >= engine->limit.queued_tokens) {
    char message[160];
    snprintf(message, sizeof message,
             "macro argument queue exceeds %lld tokens",
             (long long)engine->limit.queued_tokens);
    engine_fail(engine, message);
  }
}

static void read_macro_arguments(preprocessor_engine *engine,
                                 qa_ui_precomp_define *macro,
                                 int64_t *arguments) {
  char *macro_name = qa_ui_precomp_define_name(macro);
  if (macro_name == NULL) oom_fail();
  char message[1200];
  qa_ui_script_token *opening;
  if (!cursor_next(engine, &opening)) {
    snprintf(message, sizeof message, "define %s missing parms", macro_name);
    free(macro_name);
    engine_fail(engine, message);
  }
  int32_t numparms = qa_ui_precomp_define_numparms(macro);
  if (numparms > MAX_DEFINE_PARAMETERS) {
    snprintf(message, sizeof message,
             "define with more than %d parameters", MAX_DEFINE_PARAMETERS);
    qa_ui_script_token_release(opening);
    free(macro_name);
    engine_fail(engine, message);
  }
  for (int32_t i = 0; i < numparms; i++) arguments[i] = 0;
  if (!token_text_is(opening, "(")) {
    unread_token(engine, opening);
    qa_ui_script_token_release(opening);
    snprintf(message, sizeof message, "define %s missing parms", macro_name);
    free(macro_name);
    engine_fail(engine, message);
  }
  qa_ui_script_token_release(opening);
  int64_t argument_token_count = 0;
  int64_t depth = 0;
  bool done = false;
  for (int64_t argument = 0; !done; argument++) {
    if (argument >= MAX_DEFINE_PARAMETERS) {
      snprintf(message, sizeof message, "define %s with too many parms",
               macro_name);
      free(macro_name);
      engine_fail(engine, message);
    }
    if (argument >= numparms) {
      /* TS records a warning diagnostic then throws it as a
       * ScriptLanguageError (not via fail()). */
      char warning[1200];
      snprintf(warning, sizeof warning, "define %s has too many parms",
               macro_name);
      free(macro_name);
      qa_ui_script_diagnostic diagnostic;
      memset(&diagnostic, 0, sizeof diagnostic);
      diagnostic.severity = QA_UI_SCRIPT_SEVERITY_WARNING;
      diagnostic.message = copy_cstring(warning);
      current_location(engine, &diagnostic.location);
      if (diagnostic.message == NULL) oom_fail();
      record_diagnostic(engine, &diagnostic);
      const qa_ui_script_diagnostic *identity =
          &engine->reported[engine->reported_count - 1];
      qa_ui_script_fail_throw(&engine->env, &diagnostic, identity);
    }
    arguments[argument] = 0;
    qa_ui_precomp_token *last = NULL;
    bool last_comma = true;
    while (!done) {
      qa_ui_script_token *token;
      if (!cursor_next(engine, &token)) {
        snprintf(message, sizeof message, "define %s incomplete",
                 macro_name);
        free(macro_name);
        engine_fail(engine, message);
      }
      if (token_text_is(token, ",") && depth <= 0) {
        if (last_comma) engine_warn(engine, "too many comma's");
        qa_ui_script_token_release(token);
        break;
      }
      last_comma = false;
      if (token_text_is(token, "(")) {
        depth++;
        qa_ui_script_token_release(token);
        continue;
      }
      if (token_text_is(token, ")") && --depth <= 0) {
        if (numparms - 1 < 0)
          qa_range_error("define parameter count exceeds its local argument table");
        if (arguments[numparms - 1] == 0)
          engine_warn(engine, "too few define parms");
        done = true;
        qa_ui_script_token_release(token);
        break;
      }
      check_argument_capacity(engine, argument_token_count++);
      qa_ui_precomp_token *value = token_value(engine, token);
      qa_ui_precomp_token *copied =
          qa_ui_precomp_memory_copy_token(engine->heap, value);
      qa_ui_precomp_token_destroy(value);
      qa_ui_script_token_release(token);
      if (last == NULL) arguments[argument] = qa_ui_precomp_token_id(copied);
      else
        qa_ui_source_token_set_next(qa_ui_precomp_token_memory(last),
                                    (uint32_t)qa_ui_precomp_token_id(copied));
      last = copied;
    }
  }
  free(macro_name);
}

typedef struct append_chain {
  preprocessor_engine *engine;
  token_chain *list;
} append_chain;

static bool append_chain_visit(void *ctx, qa_ui_precomp_token *token) {
  append_chain *chain = ctx;
  append_token(chain->engine, chain->list,
               qa_ui_precomp_memory_copy_token(chain->engine->heap, token));
  return true;
}

typedef struct collect_text {
  char *text;
  size_t length, cap;
} collect_text;

static bool collect_text_visit(void *ctx, qa_ui_precomp_token *token) {
  collect_text *collect = ctx;
  char text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(qa_ui_precomp_token_memory(token), text);
  size_t length = strlen(text);
  if (collect->length + length + 1 > collect->cap) {
    size_t cap = collect->cap == 0 ? 64 : collect->cap;
    while (cap < collect->length + length + 1) cap *= 2;
    char *grown = realloc(collect->text, cap);
    if (grown == NULL) oom_fail();
    collect->text = grown;
    collect->cap = cap;
  }
  memcpy(collect->text + collect->length, text, length + 1);
  collect->length += length;
  return true;
}

static bool debug_token_visit(void *ctx, qa_ui_precomp_token *token) {
  char text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(qa_ui_precomp_token_memory(token), text);
  debug_write(ctx, text);
  return true;
}

static token_chain expand_define(preprocessor_engine *engine,
                                 qa_ui_precomp_define *macro) {
  int32_t numparms = qa_ui_precomp_define_numparms(macro);
  int64_t arguments[MAX_DEFINE_PARAMETERS];
  if (numparms != 0) read_macro_arguments(engine, macro, arguments);
  if (engine->debug_eval != NULL) {
    for (int32_t index = 0; index < numparms; index++) {
      char line[64];
      snprintf(line, sizeof line, "define parms %d:", (int)index);
      debug_write(engine, line);
      qa_ui_precomp_memory_chain(engine->heap, arguments[index],
                                 debug_token_visit, engine);
    }
  }
  token_chain expanded = {0, 0, 0};
  for (int64_t id = qa_ui_precomp_define_tokens(macro); id != 0;) {
    qa_ui_precomp_token *defined =
        qa_ui_precomp_memory_token(engine->heap, id);
    qa_ui_source_token_memory *memory =
        qa_ui_precomp_token_memory(defined);
    char defined_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
    qa_ui_source_token_string(memory, defined_text);
    int64_t parameter = qa_ui_source_token_type(memory) == 4
                            ? qa_ui_precomp_define_parameter_index(
                                  macro, defined_text)
                            : -1;
    if (parameter >= 0) {
      if (parameter >= numparms)
        qa_range_error("define parameter does not identify a supplied argument slot");
      append_chain chain = {engine, &expanded};
      qa_ui_precomp_memory_chain(engine->heap, arguments[parameter],
                                 append_chain_visit, &chain);
      id = qa_ui_source_token_next(memory);
      continue;
    }
    qa_ui_precomp_token *copied;
    if (strcmp(defined_text, "#") == 0) {
      uint32_t next_id = qa_ui_source_token_next(memory);
      qa_ui_precomp_token *next =
          next_id == 0 ? NULL
                       : qa_ui_precomp_memory_token(engine->heap, next_id);
      int64_t string_parameter = -1;
      if (next != NULL) {
        char next_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
        qa_ui_source_token_string(qa_ui_precomp_token_memory(next),
                                  next_text);
        string_parameter =
            qa_ui_precomp_define_parameter_index(macro, next_text);
      }
      if (next == NULL || string_parameter < 0) {
        engine_warn(engine, "stringizing operator without define parameter");
        id = qa_ui_source_token_next(memory);
        continue;
      }
      defined = next;
      memory = qa_ui_precomp_token_memory(defined);
      if (string_parameter >= numparms)
        qa_range_error("stringizing parameter does not identify an argument slot");
      collect_text collect = {NULL, 0, 0};
      collect.text = copy_cstring("\"");
      if (collect.text == NULL) oom_fail();
      collect.length = 1;
      collect.cap = 2;
      qa_ui_precomp_memory_chain(engine->heap, arguments[string_parameter],
                                 collect_text_visit, &collect);
      if (collect.length + 2 > collect.cap) {
        char *grown = realloc(collect.text, collect.length + 2);
        if (grown == NULL) oom_fail();
        collect.text = grown;
      }
      collect.text[collect.length++] = '"';
      collect.text[collect.length] = '\0';
      qa_ui_precomp_token *stringized = qa_ui_precomp_token_local();
      if (stringized == NULL) oom_fail();
      qa_ui_source_token_memory *smemory =
          qa_ui_precomp_token_memory(stringized);
      qa_ui_source_token_set_type(smemory, 1);
      qa_ui_source_token_write_string(smemory, collect.text,
                                      collect.length);
      free(collect.text);
      const char *path;
      int64_t line, column;
      qa_ui_script_lexer_current_location(current_frame(engine)->lexer,
                                          &path, &line, &column);
      (void)line;
      qa_ui_source_token_context context;
      memset(&context, 0, sizeof context);
      if (!qa_ui_source_token_context_set(&context, path, column, "", 0))
        oom_fail();
      bool ok = qa_ui_precomp_token_set_context(stringized, &context);
      qa_ui_source_token_context_destroy(&context);
      if (!ok
          || !qa_ui_precomp_token_set_unsupported(
                 stringized,
                 "macro stringizing leaves subtype and numeric fields uninitialized"))
        oom_fail();
      copied = qa_ui_precomp_memory_copy_token(engine->heap, stringized);
      qa_ui_precomp_token_destroy(stringized);
    } else {
      copied = qa_ui_precomp_memory_copy_token(engine->heap, defined);
    }
    append_token(engine, &expanded, copied);
    id = qa_ui_source_token_next(memory);
  }
  for (int64_t id = expanded.first; id != 0;) {
    qa_ui_precomp_token *first =
        qa_ui_precomp_memory_token(engine->heap, id);
    qa_ui_source_token_memory *first_memory =
        qa_ui_precomp_token_memory(first);
    uint32_t merge_id = qa_ui_source_token_next(first_memory);
    qa_ui_precomp_token *merge =
        merge_id == 0 ? NULL
                      : qa_ui_precomp_memory_token(engine->heap, merge_id);
    bool merged = false;
    if (merge != NULL) {
      qa_ui_source_token_memory *merge_memory =
          qa_ui_precomp_token_memory(merge);
      char merge_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
      qa_ui_source_token_string(merge_memory, merge_text);
      uint32_t second_id = qa_ui_source_token_next(merge_memory);
      if (strncmp(merge_text, "##", 2) == 0 && second_id != 0) {
        qa_ui_precomp_token *second =
            qa_ui_precomp_memory_token(engine->heap, second_id);
        qa_ui_source_token_memory *second_memory =
            qa_ui_precomp_token_memory(second);
        char left[QA_UI_SOURCE_TOKEN_STRING_BYTES];
        char right[QA_UI_SOURCE_TOKEN_STRING_BYTES];
        qa_ui_source_token_string(first_memory, left);
        qa_ui_source_token_string(second_memory, right);
        int32_t left_type = qa_ui_source_token_type(first_memory);
        int32_t right_type = qa_ui_source_token_type(second_memory);
        char combined[2048];
        if (left_type == 4 && (right_type == 4 || right_type == 3)) {
          snprintf(combined, sizeof combined, "%s%s", left, right);
          qa_ui_source_token_write_string(first_memory, combined,
                                          strlen(combined));
        } else if (left_type == 1 && right_type == 1) {
          size_t left_length = strlen(left);
          if (left_length > 0) left[left_length - 1] = '\0';
          snprintf(combined, sizeof combined, "%s%s", left,
                   right[0] != '\0' ? right + 1 : right);
          qa_ui_source_token_write_string(first_memory, combined,
                                          strlen(combined));
        } else {
          char message[2100];
          snprintf(message, sizeof message, "can't merge %s with %s", left,
                   right);
          engine_fail(engine, message);
        }
        uint32_t after = qa_ui_source_token_next(second_memory);
        bool was_last = second_id == (uint32_t)expanded.last;
        qa_ui_precomp_memory_free_token(engine->heap, merge);
        qa_ui_source_token_set_next(first_memory, after);
        if (was_last) expanded.last = id;
        qa_ui_precomp_memory_free_token(engine->heap, second);
        merged = true;
      }
    }
    if (!merged) id = qa_ui_source_token_next(first_memory);
  }
  for (int32_t index = 0; index < numparms; index++) {
    if (index >= MAX_DEFINE_PARAMETERS)
      qa_range_error("define parameter count exceeds its argument table");
    qa_ui_precomp_memory_free_tokens(engine->heap, arguments[index]);
  }
  return expanded;
}

static qa_ui_script_date default_now(void) {
  time_t now = time(NULL);
  struct tm local;
  localtime_r(&now, &local);
  qa_ui_script_date date;
  date.year = local.tm_year + 1900;
  date.month = local.tm_mon;
  date.day = local.tm_mday;
  date.hour = local.tm_hour;
  date.minute = local.tm_min;
  date.second = local.tm_sec;
  return date;
}

static token_chain expand_builtin(preprocessor_engine *engine,
                                  qa_ui_precomp_define *macro,
                                  qa_ui_script_token *invocation) {
  qa_ui_precomp_token *value = token_value(engine, invocation);
  qa_ui_precomp_token *copied =
      qa_ui_precomp_memory_copy_token(engine->heap, value);
  qa_ui_precomp_token_destroy(value);
  qa_ui_source_token_memory *token = qa_ui_precomp_token_memory(copied);
  char text[1200];
  switch (qa_ui_precomp_define_builtin(macro)) {
    case 1:
      snprintf(text, sizeof text, "%lld",
               (long long)invocation->location.line);
      qa_ui_source_token_write_string(token, text, strlen(text));
      qa_ui_source_token_set_integer_value(
          token, (uint32_t)invocation->location.line);
      qa_ui_source_token_set_float_value(
          token, (double)invocation->location.line);
      qa_ui_source_token_set_type(token, 3);
      qa_ui_source_token_set_subtype(
          token, QA_UI_SCRIPT_NUMBER_DECIMAL | QA_UI_SCRIPT_NUMBER_INTEGER);
      break;
    case 2: {
      const char *path;
      int64_t line, column;
      qa_ui_script_lexer_current_location(current_frame(engine)->lexer,
                                          &path, &line, &column);
      qa_ui_source_token_write_string(token, path, strlen(path));
      qa_ui_source_token_set_type(token, 4);
      qa_ui_source_token_set_subtype(
          token, (int32_t)qa_ui_source_token_string_length(token));
      break;
    }
    case 3: {
      qa_ui_script_date date =
          engine->now != NULL ? engine->now(engine->now_ctx) : default_now();
      static const char *const months[12] = {
          "Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      const char *month =
          date.month >= 0 && date.month < 12 ? months[date.month] : "???";
      snprintf(text, sizeof text, "\"%s %2lld %lld\"", month,
               (long long)date.day, (long long)date.year);
      qa_ui_source_token_write_string(token, text, strlen(text));
      qa_ui_source_token_set_type(token, 4);
      qa_ui_source_token_set_subtype(
          token, (int32_t)qa_ui_source_token_string_length(token));
      break;
    }
    case 4: {
      qa_ui_script_date date =
          engine->now != NULL ? engine->now(engine->now_ctx) : default_now();
      snprintf(text, sizeof text, "\"%02lld:%02lld:%02lld\"",
               (long long)date.hour, (long long)date.minute,
               (long long)date.second);
      qa_ui_source_token_write_string(token, text, strlen(text));
      qa_ui_source_token_set_type(token, 4);
      qa_ui_source_token_set_subtype(
          token, (int32_t)qa_ui_source_token_string_length(token));
      break;
    }
    default:
      return (token_chain){0, 0, 0};
  }
  return (token_chain){qa_ui_precomp_token_id(copied),
                       qa_ui_precomp_token_id(copied), 1};
}

static bool expand_macro_into_source(preprocessor_engine *engine,
                                     qa_ui_script_token *invocation) {
  qa_ui_precomp_define *macro =
      macro_get(engine, invocation->value, invocation->value_length);
  if (macro == NULL) return true;
  engine->expansion_count++;
  if (engine->expansion_count > engine->limit.macro_expansions) {
    char message[160];
    snprintf(message, sizeof message,
             "macro expansion count exceeds %lld",
             (long long)engine->limit.macro_expansions);
    engine_fail(engine, message);
  }
  token_chain expanded = qa_ui_precomp_define_builtin(macro) != 0
                             ? expand_builtin(engine, macro, invocation)
                             : expand_define(engine, macro);
  if (expanded.first == 0 || expanded.last == 0) return false;
  qa_ui_source_token_set_next(
      qa_ui_precomp_token_memory(
          qa_ui_precomp_memory_token(engine->heap, expanded.last)),
      qa_ui_source_record_tokens(engine->source));
  qa_ui_source_record_set_tokens(engine->source, (uint32_t)expanded.first);
  return true;
}

/* ---- evaluation directives ---- */

static qa_ui_script_token *make_number_token(preprocessor_engine *engine,
                                             const char *text, double value,
                                             int64_t flags) {
  qa_ui_script_location location;
  current_location(engine, &location);
  qa_ui_script_token *token = qa_ui_script_token_create(
      QA_UI_SCRIPT_KIND_NUMBER, text, strlen(text), &location, "", 0, 0);
  qa_ui_script_location_destroy(&location);
  if (token == NULL) oom_fail();
  token->flags = flags;
  token->integer_value = (uint32_t)trunc(value);
  token->float_value = value;
  return token;
}

static qa_ui_script_token *make_punct_token(preprocessor_engine *engine,
                                            const char *text,
                                            int64_t punctuation) {
  qa_ui_script_location location;
  current_location(engine, &location);
  qa_ui_script_token *token = qa_ui_script_token_create(
      QA_UI_SCRIPT_KIND_PUNCTUATION, text, strlen(text), &location, "", 0,
      0);
  qa_ui_script_location_destroy(&location);
  if (token == NULL) oom_fail();
  if (!qa_ui_script_token_set_value(token, text, strlen(text))) oom_fail();
  token->punctuation = punctuation;
  return token;
}

typedef struct collect_chain {
  preprocessor_engine *engine;
  qa_ui_script_token **tokens;
  size_t count, cap;
} collect_chain;

static bool collect_chain_visit(void *ctx, qa_ui_precomp_token *token) {
  collect_chain *collect = ctx;
  if (collect->count == collect->cap) {
    size_t cap = collect->cap == 0 ? 8 : collect->cap * 2;
    qa_ui_script_token **grown =
        realloc(collect->tokens, cap * sizeof *grown);
    if (grown == NULL) oom_fail();
    collect->tokens = grown;
    collect->cap = cap;
  }
  collect->tokens[collect->count++] =
      snapshot_token(collect->engine, token);
  return true;
}

static eval_value evaluate_expression(preprocessor_engine *engine,
                                      bool integer_mode, bool hash_form) {
  if (!hash_form) {
    qa_ui_script_token *discarded;
    if (cursor_next(engine, &discarded))
      qa_ui_script_token_release(discarded);
  }
  qa_ui_script_token *token;
  bool found = hash_form ? read_line_token(engine, &token)
                         : cursor_next(engine, &token);
  if (!found)
    engine_fail(engine, hash_form ? "no value after #if/#elif"
                                  : "nothing to evaluate");
  token_chain expression = {0, 0, 0};
  bool defined_state = false;
  int64_t depth = 1;
  for (;;) {
    if (token->kind == QA_UI_SCRIPT_KIND_NAME) {
      if (defined_state) {
        defined_state = false;
      } else if (token_text_is(token, "defined")) {
        defined_state = true;
      } else {
        if (!macro_has(engine, token->value, token->value_length)) {
          char message[1200];
          snprintf(message, sizeof message, "can't evaluate %s, not defined",
                   token->value);
          qa_ui_script_token_release(token);
          engine_fail(engine, message);
        }
        if (!expand_macro_into_source(engine, token)) {
          qa_ui_script_token_release(token);
          throw_read_false(engine);
        }
        qa_ui_script_token_release(token);
        if (!(hash_form ? read_line_token(engine, &token)
                        : cursor_next(engine, &token)))
          break;
        continue;
      }
    } else if (token->kind == QA_UI_SCRIPT_KIND_NUMBER
               || token->kind == QA_UI_SCRIPT_KIND_PUNCTUATION) {
      if (!hash_form) {
        if (token->text_length > 0 && token->text[0] == '(') depth++;
        else if (token->text_length > 0 && token->text[0] == ')') depth--;
        if (depth <= 0) {
          qa_ui_script_token_release(token);
          break;
        }
      }
    } else {
      char message[1200];
      snprintf(message, sizeof message, "can't evaluate %s", token->text);
      qa_ui_script_token_release(token);
      engine_fail(engine, message);
    }
    qa_ui_precomp_token *value = token_value(engine, token);
    append_token(engine, &expression,
                 qa_ui_precomp_memory_copy_token(engine->heap, value));
    qa_ui_precomp_token_destroy(value);
    qa_ui_script_token_release(token);
    if (expression.count > engine->limit.expression_tokens) {
      char message[160];
      snprintf(message, sizeof message, "expression exceeds %lld tokens",
               (long long)engine->limit.expression_tokens);
      engine_fail(engine, message);
    }
    if (!(hash_form ? read_line_token(engine, &token)
                    : cursor_next(engine, &token)))
      break;
  }
  collect_chain collect = {engine, NULL, 0, 0};
  qa_ui_precomp_memory_chain(engine->heap, expression.first,
                             collect_chain_visit, &collect);
  expr_parser parser = {engine, collect.tokens, collect.count, integer_mode};
  eval_value value = expr_evaluate(&parser);
  if (engine->debug_eval != NULL) {
    const char *label = hash_form ? "eval" : "$eval";
    char line[1300];
    snprintf(line, sizeof line, "%s:", label);
    debug_write(engine, line);
    for (size_t i = 0; i < collect.count; i++) {
      snprintf(line, sizeof line, " %s", collect.tokens[i]->text);
      debug_write(engine, line);
    }
    char *text = debug_evaluation_value(value, integer_mode);
    snprintf(line, sizeof line, "%s result: %s", label,
             text != NULL ? text : "");
    free(text);
    debug_write(engine, line);
  }
  for (size_t i = 0; i < collect.count; i++)
    qa_ui_script_token_release(collect.tokens[i]);
  free(collect.tokens);
  qa_ui_precomp_memory_free_tokens(engine->heap, expression.first);
  return value;
}

static void evaluate_directive(preprocessor_engine *engine,
                               bool integer_mode, bool hash_form) {
  eval_value value = evaluate_expression(engine, integer_mode, hash_form);
  double numeric = integer_mode ? (double)value.integer_value
                                : value.float_value;
  double absolute = fabs(numeric);
  char *text = NULL;
  if (integer_mode) {
    char buffer[64];
    /* String(Math.trunc(absolute)): values here fit int64 unless the
     * integer value came back beyond it, which the parser forbids. */
    snprintf(buffer, sizeof buffer, "%lld",
             (long long)trunc(absolute));
    text = copy_cstring(buffer);
  } else {
    text = evaluation_decimal(absolute, 2);
  }
  if (text == NULL) oom_fail();
  int64_t flags = QA_UI_SCRIPT_NUMBER_DECIMAL | QA_UI_SCRIPT_NUMBER_LONG
                  | (integer_mode ? QA_UI_SCRIPT_NUMBER_INTEGER
                                  : QA_UI_SCRIPT_NUMBER_FLOAT);
  qa_ui_script_token *number =
      make_number_token(engine, text, absolute, flags);
  free(text);
  if (hash_form) {
    set_token_metadata(number, true, 0, 0, 0,
                       "#eval and #evalfloat leave numeric fields uninitialized");
  } else {
    set_token_metadata(number, false, flags, (uint32_t)trunc(numeric),
                       numeric, NULL);
  }
  unread_token(engine, number);
  qa_ui_script_token_release(number);
  if (numeric < 0) {
    qa_ui_script_token *sign =
        make_punct_token(engine, "-", QA_UI_SCRIPT_PUNCT_SUBTRACT);
    set_token_metadata(sign, true, 0, 0, 0,
                       "evaluation sign token leaves numeric fields uninitialized");
    unread_token(engine, sign);
    qa_ui_script_token_release(sign);
  }
}

static void dollar_directive(preprocessor_engine *engine) {
  qa_ui_script_token *name;
  if (!cursor_next(engine, &name) || name->lines_crossed > 0) {
    bool missing = name == NULL;
    if (!missing) {
      unread_token(engine, name);
      qa_ui_script_token_release(name);
    }
    engine_fail(engine, missing ? "found $ without name"
                                : "found $ at end of line");
  }
  bool integer_mode = name->kind == QA_UI_SCRIPT_KIND_NAME
                      && token_text_is(name, "evalint");
  if (!integer_mode
      && !(name->kind == QA_UI_SCRIPT_KIND_NAME
           && token_text_is(name, "evalfloat"))) {
    char message[1200];
    snprintf(message, sizeof message, "unknown precompiler directive %s",
             name->text);
    unread_token(engine, name);
    qa_ui_script_token_release(name);
    engine_fail(engine, message);
  }
  qa_ui_script_token_release(name);
  evaluate_directive(engine, integer_mode, false);
}

/* ---- #directives ---- */

static bool source_active(preprocessor_engine *engine) {
  return qa_ui_source_record_skip(engine->source) == 0;
}

static void undefine_one(preprocessor_engine *engine,
                         qa_ui_script_token *name) {
  if (name == NULL || name->kind != QA_UI_SCRIPT_KIND_NAME) {
    char message[1200];
    if (name == NULL) {
      engine_fail(engine, "undef without name");
    }
    snprintf(message, sizeof message, "expected name, found %s",
             name->text);
    unread_token(engine, name);
    engine_fail(engine, message);
  }
  qa_ui_precomp_define *macro =
      macro_get(engine, name->value, name->value_length);
  if (macro != NULL && qa_ui_precomp_define_fixed(macro)) {
    char message[1200];
    snprintf(message, sizeof message, "can't undef %s", name->value);
    engine_warn(engine, message);
    return;
  }
  macro_delete(engine, name->value, name->value_length);
}

static void if_directive(preprocessor_engine *engine) {
  bool selected = evaluate_expression(engine, true, true).integer_value != 0;
  qa_ui_source_record_push_indent(engine->source, QA_UI_SOURCE_INDENT_IF,
                                  !selected);
}

static void ifdef_directive(preprocessor_engine *engine, bool negate) {
  qa_ui_script_token *name;
  if (!read_line_token(engine, &name)
      || name->kind != QA_UI_SCRIPT_KIND_NAME) {
    char message[1200];
    if (name == NULL) engine_fail(engine, "#ifdef without name");
    snprintf(message, sizeof message, "expected name after #ifdef, found %s",
             name->text);
    unread_token(engine, name);
    qa_ui_script_token_release(name);
    engine_fail(engine, message);
  }
  bool is_defined = macro_has(engine, name->value, name->value_length);
  bool selected = negate ? !is_defined : is_defined;
  qa_ui_script_token_release(name);
  qa_ui_source_record_push_indent(
      engine->source,
      negate ? QA_UI_SOURCE_INDENT_IFNDEF : QA_UI_SOURCE_INDENT_IFDEF,
      !selected);
}

static void elif_directive(preprocessor_engine *engine) {
  qa_ui_source_indent_type type;
  int64_t skip;
  if (!qa_ui_source_record_pop_indent(engine->source, &type, &skip))
    engine_fail(engine, "misplaced #elif");
  if (type == QA_UI_SOURCE_INDENT_ELSE)
    engine_fail(engine, "misplaced #elif");
  bool selected = evaluate_expression(engine, true, true).integer_value != 0;
  qa_ui_source_record_push_indent(engine->source, QA_UI_SOURCE_INDENT_ELIF,
                                  !selected);
}

static void else_directive(preprocessor_engine *engine) {
  qa_ui_source_indent_type type;
  int64_t skip;
  if (!qa_ui_source_record_pop_indent(engine->source, &type, &skip))
    engine_fail(engine, "misplaced #else");
  if (type == QA_UI_SOURCE_INDENT_ELSE)
    engine_fail(engine, "#else after #else");
  qa_ui_source_record_push_indent(engine->source, QA_UI_SOURCE_INDENT_ELSE,
                                  skip == 0);
}

static void endif_directive(preprocessor_engine *engine) {
  qa_ui_source_indent_type type;
  int64_t skip;
  if (!qa_ui_source_record_pop_indent(engine->source, &type, &skip))
    engine_fail(engine, "misplaced #endif");
}

static void read_line_discard(preprocessor_engine *engine) {
  qa_ui_script_token *token;
  while (read_line_token(engine, &token)) qa_ui_script_token_release(token);
}

static void define_directive(preprocessor_engine *engine) {
  qa_ui_script_token *name;
  if (!read_line_token(engine, &name)
      || name->kind != QA_UI_SCRIPT_KIND_NAME) {
    if (name == NULL) engine_fail(engine, "#define without name");
    char message[1200];
    snprintf(message, sizeof message,
             "expected name after #define, found %s", name->text);
    unread_token(engine, name);
    qa_ui_script_token_release(name);
    engine_fail(engine, message);
  }
  qa_ui_precomp_define *existing =
      macro_get(engine, name->value, name->value_length);
  if (existing != NULL && qa_ui_precomp_define_fixed(existing)) {
    char message[1200];
    snprintf(message, sizeof message, "can't redefine %s", name->value);
    qa_ui_script_token_release(name);
    engine_fail(engine, message);
  }
  if (existing != NULL) {
    char message[1200];
    snprintf(message, sizeof message, "redefinition of %s", name->value);
    engine_warn(engine, message);
    unread_token(engine, name);
    qa_ui_script_token *reread;
    bool found = read_line_token(engine, &reread);
    undefine_one(engine, found ? reread : NULL);
    if (found) qa_ui_script_token_release(reread);
  } else if ((int64_t)engine->macro_count >= engine->limit.defines) {
    char message[160];
    snprintf(message, sizeof message, "define count exceeds %lld",
             (long long)engine->limit.defines);
    qa_ui_script_token_release(name);
    engine_fail(engine, message);
  }
  qa_ui_precomp_define *macro = qa_ui_precomp_memory_allocate_define(
      engine->heap, name->value, name->value_length, true);
  qa_ui_script_token_release(name);
  macro_prepend(engine, macro);
  qa_ui_script_token *token;
  if (!read_line_token(engine, &token)) return;
  qa_ui_precomp_token *opening_value = token_value(engine, token);
  qa_ui_source_token_memory *opening =
      qa_ui_precomp_token_memory(opening_value);
  bool is_paren = token_text_is(token, "(")
                  && qa_ui_source_token_whitespace_end(opening)
                         <= qa_ui_source_token_whitespace_start(opening);
  qa_ui_precomp_token_destroy(opening_value);
  if (is_paren) {
    qa_ui_script_token_release(token);
    qa_ui_precomp_token *last = NULL;
    qa_ui_precomp_token *check_output = qa_ui_precomp_token_local();
    if (check_output == NULL) oom_fail();
    qa_ui_script_token *checked;
    bool has_checked = read_token(engine, check_output, &checked);
    qa_ui_precomp_token_destroy(check_output);
    if (!has_checked || !token_text_is(checked, ")")) {
      if (has_checked) {
        unread_token(engine, checked);
        qa_ui_script_token_release(checked);
      }
      for (;;) {
        qa_ui_script_token *parameter;
        if (!read_line_token(engine, &parameter)
            || parameter->kind != QA_UI_SCRIPT_KIND_NAME) {
          if (parameter != NULL) qa_ui_script_token_release(parameter);
          engine_fail(engine, parameter == NULL
                                  ? "expected define parameter"
                                  : "invalid define parameter");
        }
        if (qa_ui_precomp_define_parameter_index(macro, parameter->value)
            >= 0) {
          qa_ui_script_token_release(parameter);
          engine_fail(engine, "two the same define parameters");
        }
        qa_ui_precomp_token *value = token_value(engine, parameter);
        qa_ui_script_token_release(parameter);
        qa_ui_precomp_token *copied =
            qa_ui_precomp_memory_copy_token(engine->heap, value);
        qa_ui_precomp_token_destroy(value);
        qa_ui_precomp_token_clear_whitespace(copied);
        if (last == NULL)
          qa_ui_precomp_define_set_parms(
              macro, (uint32_t)qa_ui_precomp_token_id(copied));
        else
          qa_ui_source_token_set_next(
              qa_ui_precomp_token_memory(last),
              (uint32_t)qa_ui_precomp_token_id(copied));
        last = copied;
        qa_ui_precomp_define_set_numparms(
            macro, qa_ui_precomp_define_numparms(macro) + 1);
        qa_ui_script_token *separator;
        if (!read_line_token(engine, &separator))
          engine_fail(engine, "define parameters not terminated");
        bool is_close = token_text_is(separator, ")");
        bool is_comma = token_text_is(separator, ",");
        qa_ui_script_token_release(separator);
        if (is_close) break;
        if (!is_comma) engine_fail(engine, "define not terminated");
      }
    } else {
      qa_ui_script_token_release(checked);
    }
    if (!read_line_token(engine, &token)) return;
  }
  qa_ui_precomp_token *last = NULL;
  for (;;) {
    qa_ui_precomp_token *value = token_value(engine, token);
    qa_ui_precomp_token *copied =
        qa_ui_precomp_memory_copy_token(engine->heap, value);
    qa_ui_precomp_token_destroy(value);
    qa_ui_source_token_memory *memory = qa_ui_precomp_token_memory(copied);
    char copied_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
    qa_ui_source_token_string(memory, copied_text);
    char *macro_name = qa_ui_precomp_define_name(macro);
    bool recursive = qa_ui_source_token_type(memory) == 4
                     && macro_name != NULL
                     && strcmp(copied_text, macro_name) == 0;
    free(macro_name);
    if (recursive) {
      engine_error(engine, "recursive define (removed recursion)");
      qa_ui_precomp_memory_free_token(engine->heap, copied);
    } else {
      qa_ui_precomp_token_clear_whitespace(copied);
      if (last == NULL)
        qa_ui_precomp_define_set_tokens(
            macro, (uint32_t)qa_ui_precomp_token_id(copied));
      else
        qa_ui_source_token_set_next(
            qa_ui_precomp_token_memory(last),
            (uint32_t)qa_ui_precomp_token_id(copied));
      last = copied;
    }
    qa_ui_script_token_release(token);
    if (!read_line_token(engine, &token)) break;
  }
  if (last != NULL) {
    char first_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
    char last_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
    qa_ui_source_token_string(
        qa_ui_precomp_token_memory(qa_ui_precomp_memory_token(
            engine->heap, qa_ui_precomp_define_tokens(macro))),
        first_text);
    qa_ui_source_token_string(qa_ui_precomp_token_memory(last), last_text);
    if (strcmp(first_text, "##") == 0 || strcmp(last_text, "##") == 0)
      engine_fail(engine, "define with misplaced ##");
  }
}

static char *collapse_slashes(const char *text) {
  char *result = malloc(strlen(text) + 1);
  if (result == NULL) return NULL;
  size_t out = 0;
  for (size_t i = 0; text[i] != '\0';) {
    if (text[i] == '\\' || text[i] == '/') {
      result[out++] = '/';
      while (text[i] == '\\' || text[i] == '/') i++;
    } else {
      result[out++] = text[i++];
    }
  }
  result[out] = '\0';
  return result;
}

static void include_directive(preprocessor_engine *engine,
                              const char *source_path) {
  qa_ui_script_token *first;
  if (!cursor_next(engine, &first) || first->lines_crossed > 0) {
    if (first != NULL) qa_ui_script_token_release(first);
    engine_fail(engine, "#include without file name");
  }
  qa_ui_script_include_request request;
  memset(&request, 0, sizeof request);
  request.from_path = source_path;
  char *requested = NULL;
  char *include_path = qa_ui_source_record_include_path(engine->source);
  if (include_path == NULL) oom_fail();
  if (first->kind == QA_UI_SCRIPT_KIND_STRING) {
    request.kind = QA_UI_SCRIPT_INCLUDE_QUOTED;
    requested = copy_cstring(first->value);
    request.requested_path = requested;
    request.include_path = include_path[0] != '\0' ? include_path : NULL;
  } else if (is_punctuation(first, QA_UI_SCRIPT_PUNCT_LESS)) {
    collect_text pieces = {NULL, 0, 0};
    pieces.text = copy_cstring("");
    if (pieces.text == NULL) oom_fail();
    pieces.cap = 1;
    bool closed = false;
    for (;;) {
      qa_ui_script_token *token;
      if (!cursor_next(engine, &token)) break;
      if (token->lines_crossed > 0) {
        unread_token(engine, token);
        qa_ui_script_token_release(token);
        break;
      }
      if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_GREATER)) {
        closed = true;
        qa_ui_script_token_release(token);
        break;
      }
      size_t need = pieces.length + token->text_length + 1;
      if (need > pieces.cap) {
        char *grown = realloc(pieces.text, need * 2);
        if (grown == NULL) oom_fail();
        pieces.text = grown;
        pieces.cap = need * 2;
      }
      memcpy(pieces.text + pieces.length, token->text, token->text_length);
      pieces.length += token->text_length;
      pieces.text[pieces.length] = '\0';
      qa_ui_script_token_release(token);
    }
    if (!closed) engine_warn(engine, "#include missing trailing >");
    requested = malloc(strlen(include_path) + pieces.length + 1);
    if (requested == NULL) oom_fail();
    strcpy(requested, include_path);
    strcat(requested, pieces.text);
    free(pieces.text);
    if (requested[0] == '\0')
      engine_fail(engine, "#include without file name between < >");
    request.kind = QA_UI_SCRIPT_INCLUDE_SYSTEM;
    request.requested_path = requested;
  } else {
    qa_ui_script_token_release(first);
    free(include_path);
    engine_fail(engine, "#include without file name");
  }
  qa_ui_script_token_release(first);
  if ((int64_t)engine->live_frames >= engine->limit.include_depth) {
    char message[160];
    snprintf(message, sizeof message, "include depth exceeds %lld",
             (long long)engine->limit.include_depth);
    free(requested);
    free(include_path);
    engine_fail(engine, message);
  }
  qa_ui_script_source included;
  memset(&included, 0, sizeof included);
  bool resolved = engine->resolve != NULL
                  && engine->resolve(engine->resolve_ctx, &request,
                                     &included);
  if (engine->async_validate != NULL)
    engine->async_validate(engine->async_validate_ctx);
  if (!resolved) {
    char *converted = collapse_slashes(requested);
    if (converted == NULL) oom_fail();
    char *failed = NULL;
    if (request.kind == QA_UI_SCRIPT_INCLUDE_QUOTED) {
      failed = malloc(strlen(include_path) + strlen(converted) + 1);
      if (failed == NULL) oom_fail();
      strcpy(failed, include_path);
      strcat(failed, converted);
    } else {
      failed = copy_cstring(converted);
      if (failed == NULL) oom_fail();
    }
    free(converted);
    char message[1300];
    snprintf(message, sizeof message, "file %s not found", failed);
    free(failed);
    free(requested);
    free(include_path);
    engine_fail(engine, message);
  }
  free(requested);
  free(include_path);
  if (included.path[0] == '\0')
    engine_fail(engine, "include resolver returned an empty canonical path");
  source_frame *included_frame = create_frame(engine, &included);
  int64_t remaining = (int64_t)engine->live_frames + 1;
  for (int64_t id = qa_ui_source_record_script(engine->source); id != 0;) {
    if (--remaining == 0)
      qa_range_error("source script stack contains a cycle");
    source_frame *frame = frame_of(engine, id);
    const char *frame_path;
    int64_t line, column;
    qa_ui_script_lexer_current_location(frame->lexer, &frame_path, &line,
                                        &column);
    char *left_raw = qa_source_command_text(frame_path);
    char *right_raw = qa_source_command_text(included.path);
    char *left = left_raw != NULL ? qa_ascii_fold(left_raw) : NULL;
    char *right = right_raw != NULL ? qa_ascii_fold(right_raw) : NULL;
    bool same = left != NULL && right != NULL && strcmp(left, right) == 0;
    free(left_raw);
    free(right_raw);
    free(left);
    free(right);
    if (same) {
      char message[1300];
      snprintf(message, sizeof message, "%s recursively included",
               included.path);
      engine_error(engine, message);
      engine->frames[included_frame->id - 1] = NULL;
      engine->live_frames--;
      qa_ui_script_lexer_destroy(included_frame->lexer);
      free(included_frame);
      return;
    }
    id = next_frame_id(frame);
  }
  set_next_frame(included_frame, qa_ui_source_record_script(engine->source));
  qa_ui_source_record_set_script(engine->source,
                                 (uint32_t)included_frame->id);
}

static void directive(preprocessor_engine *engine) {
  qa_ui_script_token *directive_token;
  if (!cursor_next(engine, &directive_token)
      || directive_token->lines_crossed > 0) {
    bool missing = directive_token == NULL;
    if (!missing) {
      unread_token(engine, directive_token);
      qa_ui_script_token_release(directive_token);
    }
    engine_fail(engine, missing ? "found # without name"
                                : "found # at end of line");
  }
  if (directive_token->kind != QA_UI_SCRIPT_KIND_NAME) {
    char message[1200];
    snprintf(message, sizeof message, "unknown precompiler directive %s",
             directive_token->text);
    qa_ui_script_token_release(directive_token);
    engine_fail(engine, message);
  }
  const char *name = directive_token->value;
  if (strcmp(name, "if") == 0) {
    qa_ui_script_token_release(directive_token);
    if_directive(engine);
    return;
  }
  if (strcmp(name, "ifdef") == 0 || strcmp(name, "ifndef") == 0) {
    bool negate = name[2] == 'n';
    qa_ui_script_token_release(directive_token);
    ifdef_directive(engine, negate);
    return;
  }
  if (strcmp(name, "elif") == 0) {
    qa_ui_script_token_release(directive_token);
    elif_directive(engine);
    return;
  }
  if (strcmp(name, "else") == 0) {
    qa_ui_script_token_release(directive_token);
    else_directive(engine);
    return;
  }
  if (strcmp(name, "endif") == 0) {
    qa_ui_script_token_release(directive_token);
    endif_directive(engine);
    return;
  }
  if (strcmp(name, "include") == 0) {
    qa_ui_script_token_release(directive_token);
    if (source_active(engine)) {
      const char *path;
      int64_t line, column;
      qa_ui_script_lexer_current_location(current_frame(engine)->lexer,
                                          &path, &line, &column);
      (void)line;
      (void)column;
      char *path_copy = copy_cstring(path);
      if (path_copy == NULL) oom_fail();
      include_directive(engine, path_copy);
      free(path_copy);
    }
    return;
  }
  if (strcmp(name, "define") == 0) {
    qa_ui_script_token_release(directive_token);
    if (source_active(engine)) define_directive(engine);
    return;
  }
  if (strcmp(name, "undef") == 0) {
    qa_ui_script_token_release(directive_token);
    if (source_active(engine)) {
      qa_ui_script_token *undef_name;
      bool found = read_line_token(engine, &undef_name);
      undefine_one(engine, found ? undef_name : NULL);
      if (found) qa_ui_script_token_release(undef_name);
    }
    return;
  }
  if (strcmp(name, "eval") == 0) {
    qa_ui_script_token_release(directive_token);
    evaluate_directive(engine, true, true);
    return;
  }
  if (strcmp(name, "evalfloat") == 0) {
    qa_ui_script_token_release(directive_token);
    evaluate_directive(engine, false, true);
    return;
  }
  if (strcmp(name, "line") == 0) {
    qa_ui_script_token_release(directive_token);
    engine_fail(engine, "#line directive not supported");
  }
  if (strcmp(name, "error") == 0) {
    qa_ui_script_token_release(directive_token);
    qa_ui_script_token *next;
    char message[1200];
    bool found = cursor_next(engine, &next);
    snprintf(message, sizeof message, "#error directive: %s",
             found ? next->text : "");
    if (found) qa_ui_script_token_release(next);
    engine_fail(engine, message);
  }
  if (strcmp(name, "pragma") == 0) {
    qa_ui_script_token_release(directive_token);
    engine_warn(engine, "#pragma directive not supported");
    read_line_discard(engine);
    return;
  }
  char message[1200];
  snprintf(message, sizeof message, "unknown precompiler directive %s",
           name);
  qa_ui_script_token_release(directive_token);
  engine_fail(engine, message);
}

static bool read_token(preprocessor_engine *engine,
                       qa_ui_precomp_token *output,
                       qa_ui_script_token **out) {
  for (;;) {
    qa_ui_script_token *token;
    if (!read_source_token(engine, output, &token)) return false;
    if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_PREPROCESSOR)) {
      qa_ui_script_token_release(token);
      directive(engine);
      continue;
    }
    if (is_punctuation(token, QA_UI_SCRIPT_PUNCT_DOLLAR)) {
      qa_ui_script_token_release(token);
      dollar_directive(engine);
      continue;
    }
    if (token->kind == QA_UI_SCRIPT_KIND_STRING) {
      qa_ui_precomp_token *local = qa_ui_precomp_token_local();
      if (local == NULL) oom_fail();
      qa_ui_script_token *volatile next = NULL;
      rf_catch rf;
      rf.prev = engine->rf_top;
      engine->rf_top = &rf;
      qa_ui_script_catch catch_block;
      catch_block.prev = engine->env.top;
      engine->env.top = &catch_block;
      volatile bool caught = false;
      if (setjmp(rf.buf) == 0) {
        if (setjmp(catch_block.buf) == 0) {
          qa_ui_script_token *found;
          if (read_token(engine, local, &found)) next = found;
        } else {
          caught = true;
        }
      } else {
        caught = true;
      }
      engine->rf_top = rf.prev;
      engine->env.top = catch_block.prev;
      if (caught) {
        qa_ui_script_fail_env_clear_pending(&engine->env);
        next = NULL;
      }
      if (next != NULL && next->kind == QA_UI_SCRIPT_KIND_STRING) {
        /* token_value needs next's snapshot, held by `local`. */
        qa_ui_precomp_token *following = token_value(engine, (qa_ui_script_token *)next);
        qa_ui_precomp_token_destroy(local);
        char following_text[QA_UI_SOURCE_TOKEN_STRING_BYTES];
        qa_ui_source_token_string(qa_ui_precomp_token_memory(following),
                                  following_text);
        qa_ui_precomp_token_destroy(following);
        qa_ui_script_token_release((qa_ui_script_token *)next);
        qa_ui_source_token_memory *memory =
            qa_ui_precomp_token_memory(output);
        size_t length = qa_ui_source_token_string_length(memory);
        qa_ui_source_token_set_string_byte(memory, (int64_t)length - 1, 0);
        char head[QA_UI_SOURCE_TOKEN_STRING_BYTES];
        qa_ui_source_token_string(memory, head);
        char combined[2048];
        snprintf(combined, sizeof combined, "%s%s", head,
                 following_text[0] != '\0' ? following_text + 1
                                           : following_text);
        if (strlen(combined) + 1 >= QA_UI_SCRIPT_DEFAULT_TOKEN_LIMIT) {
          char message[128];
          snprintf(message, sizeof message,
                   "string longer than MAX_TOKEN %d\n",
                   QA_UI_SCRIPT_DEFAULT_TOKEN_LIMIT);
          qa_ui_script_token_release(token);
          engine_fail(engine, message);
        }
        qa_ui_source_token_write_string(memory, combined, strlen(combined));
        qa_ui_script_token_release(token);
        token = snapshot_token(engine, output);
      } else if (next != NULL) {
        unread_token(engine, (qa_ui_script_token *)next);
        qa_ui_script_token_release((qa_ui_script_token *)next);
        qa_ui_precomp_token_destroy(local);
      } else {
        qa_ui_precomp_token_destroy(local);
      }
    }
    if (!source_active(engine)) {
      qa_ui_script_token_release(token);
      continue;
    }
    if (token->kind == QA_UI_SCRIPT_KIND_NAME
        && macro_has(engine, token->value, token->value_length)) {
      bool expanded = expand_macro_into_source(engine, token);
      qa_ui_script_token_release(token);
      if (!expanded) return false;
      continue;
    }
    qa_ui_precomp_token_copy_from(
        qa_ui_source_record_token(engine->source), output);
    *out = token;
    return true;
  }
}

/* ---- engine lifecycle ---- */

static void install_builtins(preprocessor_engine *engine) {
  static const char *const names[4] = {"__LINE__", "__FILE__", "__DATE__",
                                       "__TIME__"};
  for (int index = 0; index < 4; index++) {
    qa_ui_precomp_define *macro = qa_ui_precomp_memory_allocate_define(
        engine->heap, names[index], strlen(names[index]), true);
    qa_ui_precomp_define_set_flags(
        macro, qa_ui_precomp_define_flags(macro) | 1);
    qa_ui_precomp_define_set_builtin(macro, index + 1);
    macro_prepend(engine, macro);
  }
}

static void record_trampoline(void *ctx,
                              const qa_ui_script_diagnostic *diagnostic) {
  record_diagnostic(ctx, diagnostic);
}

static void publish_define_trampoline(void *ctx,
                                      qa_ui_precomp_define *define) {
  macro_prepend(ctx, define);
}

static void install_initial_defines(
    preprocessor_engine *engine, const char *const *defines, size_t count) {
  for (size_t index = 0; index < count; index++) {
    qa_ui_precomp_define *macro = define_from_string(
        defines[index], engine->memory, engine->has_memory, engine->heap,
        record_trampoline, engine);
    if (macro == NULL) engine_fail(engine, "initial define is invalid");
    macro_prepend(engine, macro);
  }
}

static preprocessor_engine *engine_alloc(
    qa_ui_script_resolve_fn resolve, void *resolve_ctx,
    const qa_ui_script_preprocessor_options *options, int lifetime,
    qa_ui_precomp_memory *heap) {
  preprocessor_engine *engine = calloc(1, sizeof *engine);
  if (engine == NULL) return NULL;
  engine->resolve = resolve;
  engine->resolve_ctx = resolve_ctx;
  engine->lifetime = lifetime;
  engine->next_frame = 1;
  qa_ui_script_fail_env_init(&engine->env);
  qa_ui_script_preprocessor_options empty;
  memset(&empty, 0, sizeof empty);
  if (options == NULL) options = &empty;
  engine->now = options->now;
  engine->now_ctx = options->now_ctx;
  engine->report = options->report;
  engine->report_ctx = options->report_ctx;
  engine->debug_eval = options->debug_eval;
  engine->debug_eval_ctx = options->debug_eval_ctx;
  engine->limit = make_limits(options);
  engine->cursor_token = qa_ui_precomp_token_local();
  engine->output = qa_ui_precomp_token_local();
  engine->heap = heap;
  return engine;
}

static void engine_destroy(preprocessor_engine *engine);

static preprocessor_engine *engine_create(
    const qa_ui_script_source *root, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options,
    int lifetime, qa_ui_precomp_memory *heap) {
  if (root->path[0] == '\0')
    qa_range_error("root source path cannot be empty");
  preprocessor_engine *engine =
      engine_alloc(resolve, resolve_ctx, options, lifetime, heap);
  if (engine == NULL) return NULL;
  engine->memory = options != NULL ? options->memory : NULL;
  engine->has_memory = engine->memory != NULL;
  if (root->storage != NULL) {
    engine->memory = qa_ui_source_script_memory(root->storage);
    engine->has_memory = engine->memory != NULL;
  }
  if (engine->heap == NULL) {
    engine->heap =
        qa_ui_precomp_memory_create(engine->has_memory ? engine->memory
                                                       : NULL);
    if (engine->heap == NULL) {
      engine_destroy(engine);
      return NULL;
    }
    engine->owns_heap = true;
  }
  if (engine->cursor_token == NULL || engine->output == NULL) {
    engine_destroy(engine);
    return NULL;
  }
  engine->root_frame = create_frame(engine, root);
  set_next_frame(engine->root_frame, 0);
  engine->source = qa_ui_source_record_create(
      root->path, engine->root_frame->id,
      engine->has_memory ? engine->memory : NULL, engine->has_memory,
      lifetime == 1 ? QA_UI_SOURCE_LIFETIME_STACK
                    : QA_UI_SOURCE_LIFETIME_HEAP);
  if (engine->source == NULL) {
    engine_destroy(engine);
    return NULL;
  }
  if (options != NULL && options->globals != NULL) {
    qa_ui_script_global_defines_copy_to(options->globals, engine->heap,
                                        publish_define_trampoline, engine);
  }
  if (options != NULL && options->global_defines != NULL) {
    for (size_t i = 0; i < options->global_defines->count; i++)
      macro_prepend(engine, qa_ui_precomp_memory_copy_define(
                                engine->heap,
                                options->global_defines->definitions[i]));
  }
  if (options != NULL && options->install_builtins)
    install_builtins(engine);
  if (options != NULL)
    install_initial_defines(engine, options->initial_defines,
                            options->initial_define_count);
  return engine;
}

static void engine_dispose(preprocessor_engine *engine) {
  if (engine->disposed) return;
  if (engine->lifetime == 1) {
    free_pending_tokens(engine);
    qa_ui_source_record_free_hash(engine->source);
    qa_ui_script_lexer_dispose(engine->root_frame->lexer);
  } else {
    while (qa_ui_source_record_script(engine->source) != 0) {
      source_frame *frame = current_frame(engine);
      qa_ui_source_record_set_script(engine->source,
                                     (uint32_t)next_frame_id(frame));
      engine->frames[frame->id - 1] = NULL;
      engine->live_frames--;
      qa_ui_script_lexer_dispose(frame->lexer);
      if (frame != engine->root_frame) {
        qa_ui_script_lexer_destroy(frame->lexer);
        free(frame);
      }
    }
    free_pending_tokens(engine);
    macro_clear(engine);
    qa_ui_source_record_free_indents(engine->source);
    qa_ui_source_record_free_hash(engine->source);
    qa_ui_source_record_free_record(engine->source);
  }
  engine->disposed = true;
}

static void engine_destroy(preprocessor_engine *engine) {
  if (engine == NULL) return;
  if (!engine->disposed && engine->source != NULL) engine_dispose(engine);
  for (size_t i = 0; i < engine->frame_count; i++) {
    if (engine->frames[i] != NULL) {
      qa_ui_script_lexer_destroy(engine->frames[i]->lexer);
      free(engine->frames[i]);
    }
  }
  free(engine->frames);
  qa_ui_source_record_destroy(engine->source);
  if (engine->owns_heap) qa_ui_precomp_memory_destroy(engine->heap);
  qa_ui_precomp_token_destroy(engine->cursor_token);
  qa_ui_precomp_token_destroy(engine->output);
  for (size_t i = 0; i < engine->reported_count; i++)
    qa_ui_script_diagnostic_destroy(&engine->reported[i]);
  free(engine->reported);
  free(engine->macro_entries);
  qa_ui_script_fail_env_destroy(&engine->env);
  free(engine);
}

/* TS tokenOperation: one public token with output bookkeeping. */
static bool token_operation(preprocessor_engine *engine,
                            qa_ui_script_token **out) {
  require_live(engine);
  qa_ui_precomp_token *output = qa_ui_precomp_token_local();
  if (output == NULL) oom_fail();
  qa_ui_precomp_token *volatile held_output = output;
  qa_ui_script_token *volatile token = NULL;
  volatile bool read_false = false;
  rf_catch rf;
  rf.prev = engine->rf_top;
  engine->rf_top = &rf;
  qa_ui_script_catch catch_block;
  catch_block.prev = engine->env.top;
  engine->env.top = &catch_block;
  volatile bool failed = false;
  if (setjmp(rf.buf) == 0) {
    if (setjmp(catch_block.buf) == 0) {
      qa_ui_script_token *found;
      if (read_token(engine, output, &found)) token = found;
    } else {
      failed = true;
    }
  } else {
    read_false = true;
  }
  engine->rf_top = rf.prev;
  engine->env.top = catch_block.prev;
  qa_ui_precomp_token_destroy(engine->output);
  engine->output = (qa_ui_precomp_token *)held_output;
  if (read_false) return false;
  if (failed) {
    /* Rethrow the pending failure. */
    qa_ui_script_diagnostic pending = engine->env.pending;
    const qa_ui_script_diagnostic *identity = engine->env.pending_identity;
    engine->env.has_pending = false;
    engine->env.pending_identity = NULL;
    memset(&engine->env.pending, 0, sizeof engine->env.pending);
    qa_ui_script_fail_throw(&engine->env, &pending, identity);
  }
  if (token == NULL) return false;
  if (engine->output_count >= engine->limit.output_tokens) {
    char message[160];
    snprintf(message, sizeof message,
             "preprocessor output exceeds %lld tokens",
             (long long)engine->limit.output_tokens);
    qa_ui_script_token_release((qa_ui_script_token *)token);
    engine_fail(engine, message);
  }
  engine->output_count++;
  *out = (qa_ui_script_token *)token;
  return true;
}

static bool engine_next_token(preprocessor_engine *engine,
                              qa_ui_script_token **out) {
  if (engine->reading_async)
    qa_ui_script_plain_error("Script source already has a pending token read");
  return token_operation(engine, out);
}

static void engine_current_record(preprocessor_engine *engine,
                                  qa_ui_script_token_record *out) {
  const char *unsupported =
      qa_ui_precomp_token_unsupported(engine->output);
  if (unsupported != NULL) {
    char message[1300];
    snprintf(message, sizeof message,
             "source token profile is unsupported: %s", unsupported);
    engine_fail(engine, message);
  }
  if (!qa_ui_source_token_read_record(
          qa_ui_precomp_token_memory(engine->output),
          qa_ui_precomp_token_context(engine->output), false, 0, out))
    oom_fail();
}

static bool engine_next_record(preprocessor_engine *engine,
                               qa_ui_script_token_record *out) {
  qa_ui_script_token *token;
  if (!engine_next_token(engine, &token)) return false;
  qa_ui_script_token_release(token);
  engine_current_record(engine, out);
  return true;
}

static bool read_expected(preprocessor_engine *engine,
                          qa_ui_script_token_record *out) {
  memset(out, 0, sizeof *out);
  qa_ui_script_catch catch_block;
  catch_block.prev = engine->env.top;
  engine->env.top = &catch_block;
  volatile bool result = false;
  if (setjmp(catch_block.buf) == 0) {
    result = engine_next_record(engine, out);
    engine->env.top = catch_block.prev;
  } else {
    engine->env.top = catch_block.prev;
    qa_ui_script_fail_env_clear_pending(&engine->env);
    result = false;
  }
  return result;
}

static qa_ui_precomp_define *define_from_string(
    const char *text, const qa_ui_script_memory *memory, bool has_memory,
    qa_ui_precomp_memory *heap, qa_ui_script_report_fn report,
    void *report_ctx) {
  qa_ui_script_preprocessor_options options;
  memset(&options, 0, sizeof options);
  options.memory = has_memory ? memory : NULL;
  options.report = report;
  options.report_ctx = report_ctx;
  qa_ui_script_source root;
  memset(&root, 0, sizeof root);
  root.path = "*extern";
  root.text = (const uint8_t *)text;
  root.text_length = strlen(text);
  preprocessor_engine *engine =
      engine_create(&root, NULL, NULL, &options, 1, heap);
  if (engine == NULL) oom_fail();
  /* parseGlobal: defineDirective then macros.first(). */
  volatile bool caught = false;
  rf_catch rf;
  rf.prev = engine->rf_top;
  engine->rf_top = &rf;
  qa_ui_script_catch catch_block;
  catch_block.prev = engine->env.top;
  engine->env.top = &catch_block;
  if (setjmp(rf.buf) == 0) {
    if (setjmp(catch_block.buf) == 0) {
      define_directive(engine);
    } else {
      caught = true;
    }
  } else {
    caught = true;
  }
  engine->rf_top = rf.prev;
  engine->env.top = catch_block.prev;
  qa_ui_precomp_define *define = NULL;
  if (caught) {
    qa_ui_script_fail_env_clear_pending(&engine->env);
  } else {
    define = macro_first(engine);
  }
  engine_dispose(engine);
  engine_destroy(engine);
  return define;
}

/* ---- engine checkpoint ---- */

static bool set_num(qa_save_value *object, const char *key, double value) {
  return qa_save_value_set(object, key, qa_save_value_number(value));
}
static bool set_str(qa_save_value *object, const char *key,
                    const char *value) {
  return qa_save_value_set(object, key, qa_save_value_string(value));
}

static qa_save_value *engine_capture(
    preprocessor_engine *engine,
    const qa_ui_script_memory_capture *capture) {
  require_live(engine);
  if (engine->reading_async)
    qa_ui_script_plain_error("Cannot checkpoint an in-flight asynchronous script read");
  qa_save_value *object = qa_save_value_object();
  qa_save_value *limits_value = qa_save_value_object();
  qa_save_value *frames = qa_save_value_array();
  qa_save_value *reported = qa_save_value_array();
  qa_save_value *heap = qa_ui_precomp_memory_capture_save_state(engine->heap,
                                                                capture);
  qa_save_value *source =
      qa_ui_source_record_capture_save_state(engine->source, capture);
  qa_save_value *macros = qa_save_value_array();
  qa_save_value *cursor =
      qa_ui_precomp_token_capture_save_state(engine->cursor_token);
  qa_save_value *output =
      qa_ui_precomp_token_capture_save_state(engine->output);
  if (object == NULL || limits_value == NULL || frames == NULL
      || reported == NULL || heap == NULL || source == NULL
      || macros == NULL || cursor == NULL || output == NULL)
    goto oom;
  if (!set_str(object, "lifetime",
               engine->lifetime == 1 ? "global" : "source"))
    goto oom;
  if (!set_num(limits_value, "includeDepth",
               (double)engine->limit.include_depth)
      || !set_num(limits_value, "macroExpansions",
                  (double)engine->limit.macro_expansions)
      || !set_num(limits_value, "queuedTokens",
                  (double)engine->limit.queued_tokens)
      || !set_num(limits_value, "outputTokens",
                  (double)engine->limit.output_tokens)
      || !set_num(limits_value, "sourceTokens",
                  (double)engine->limit.source_tokens)
      || !set_num(limits_value, "defines", (double)engine->limit.defines)
      || !set_num(limits_value, "expressionTokens",
                  (double)engine->limit.expression_tokens)
      || !qa_save_value_set(object, "limits", limits_value))
    goto oom;
  limits_value = NULL;
  if (!qa_save_value_set(object, "heap", heap)) goto oom;
  heap = NULL;
  if (!qa_save_value_set(object, "source", source)) goto oom;
  source = NULL;
  for (size_t i = 0; i < engine->macro_count; i++) {
    if (!qa_save_value_push(
            macros, qa_save_value_number((double)engine->macro_entries[i])))
      goto oom;
  }
  if (!qa_save_value_set(object, "macros", macros)) goto oom;
  macros = NULL;
  if (!set_num(object, "nextFrame", (double)engine->next_frame)) goto oom;
  if (!set_num(object, "rootFrame", (double)engine->root_frame->id))
    goto oom;
  for (size_t i = 0; i < engine->frame_count; i++) {
    source_frame *frame = engine->frames[i];
    if (frame == NULL) continue;
    qa_save_value *entry = qa_save_value_object();
    qa_save_value *lexer =
        qa_ui_script_lexer_capture_save_state(frame->lexer, capture);
    bool ok = entry != NULL && lexer != NULL
              && set_num(entry, "id", (double)frame->id)
              && qa_save_value_set(entry, "lexer", lexer);
    if (ok) lexer = NULL;
    ok = ok && set_num(entry, "diagnosticNext",
                       (double)frame->diagnostic_next)
         && set_num(entry, "tokenCount", (double)frame->token_count)
         && qa_save_value_push(frames, entry);
    if (!ok) {
      qa_save_value_free(entry);
      qa_save_value_free(lexer);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "frames", frames)) goto oom;
  frames = NULL;
  if (!qa_save_value_set(object, "cursorToken", cursor)) goto oom;
  cursor = NULL;
  if (!qa_save_value_set(object, "output", output)) goto oom;
  output = NULL;
  if (!set_num(object, "expansionCount", (double)engine->expansion_count))
    goto oom;
  if (!set_num(object, "outputCount", (double)engine->output_count))
    goto oom;
  for (size_t i = 0; i < engine->reported_count; i++) {
    qa_save_value *entry =
        qa_ui_script_diagnostic_capture(&engine->reported[i]);
    if (entry == NULL || !qa_save_value_push(reported, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "reported", reported)) goto oom;
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(limits_value);
  qa_save_value_free(frames);
  qa_save_value_free(reported);
  qa_save_value_free(heap);
  qa_save_value_free(source);
  qa_save_value_free(macros);
  qa_save_value_free(cursor);
  qa_save_value_free(output);
  return NULL;
}

static bool rfld(const qa_save_reader *reader, const char *name,
                 qa_save_reader *out) {
  return qa_save_reader_field(reader, name, out);
}
static int64_t rfld_int_min(const qa_save_reader *reader, const char *name,
                            double minimum) {
  qa_save_reader field;
  if (!rfld(reader, name, &field)) oom_fail();
  int64_t value = (int64_t)qa_save_reader_integer_min(&field, minimum);
  qa_save_reader_destroy(&field);
  return value;
}

static preprocessor_engine *engine_restore(
    const qa_save_value *value, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options,
    const qa_ui_script_memory_restore *restore) {
  qa_save_reader reader, field;
  if (!qa_save_reader_init(&reader, value, "script.preprocessor"))
    return NULL;
  if (!rfld(&reader, "lifetime", &field)) {
    qa_save_reader_destroy(&reader);
    return NULL;
  }
  const char *lifetimes[2] = {"source", "global"};
  const char *lifetime_text =
      qa_save_reader_choice_string(&field, lifetimes, 2);
  int lifetime = strcmp(lifetime_text, "global") == 0 ? 1 : 0;
  qa_save_reader_destroy(&field);
  preprocessor_engine *engine =
      engine_alloc(resolve, resolve_ctx, options, lifetime, NULL);
  if (engine == NULL) {
    qa_save_reader_destroy(&reader);
    return NULL;
  }
  engine->memory = options != NULL ? options->memory : NULL;
  engine->has_memory = engine->memory != NULL;
  engine->heap =
      qa_ui_precomp_memory_create(engine->has_memory ? engine->memory
                                                     : NULL);
  if (engine->heap == NULL || engine->cursor_token == NULL
      || engine->output == NULL)
    goto oom;
  engine->owns_heap = true;
  if (!rfld(&reader, "limits", &field)) goto oom;
  engine->limit.include_depth = rfld_int_min(&field, "includeDepth", 1);
  engine->limit.macro_expansions =
      rfld_int_min(&field, "macroExpansions", 1);
  engine->limit.queued_tokens = rfld_int_min(&field, "queuedTokens", 1);
  engine->limit.output_tokens = rfld_int_min(&field, "outputTokens", 1);
  engine->limit.source_tokens = rfld_int_min(&field, "sourceTokens", 1);
  engine->limit.defines = rfld_int_min(&field, "defines", 1);
  engine->limit.expression_tokens =
      rfld_int_min(&field, "expressionTokens", 1);
  qa_save_reader_destroy(&field);
  if (!rfld(&reader, "heap", &field)) goto oom;
  qa_ui_precomp_memory_restore_save_state(engine->heap, field.value,
                                          restore);
  qa_save_reader_destroy(&field);
  if (!rfld(&reader, "source", &field)) goto oom;
  engine->source = qa_ui_source_record_restore_save_state(
      field.value, engine->has_memory ? engine->memory : NULL,
      engine->has_memory, restore);
  qa_save_reader_destroy(&field);
  if (engine->source == NULL) goto oom;
  if (!rfld(&reader, "macros", &field)) goto oom;
  {
    qa_save_reader macros_reader;
    if (!qa_save_reader_init(&macros_reader, field.value, "script.macros"))
      goto oom_field;
    size_t count = qa_save_reader_list_count(&macros_reader);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader cell;
      if (!qa_save_reader_item(&macros_reader, i, &cell)) {
        qa_save_reader_destroy(&macros_reader);
        goto oom_field;
      }
      int64_t id = (int64_t)qa_save_reader_integer_min(&cell, 1);
      qa_save_reader_destroy(&cell);
      for (size_t j = 0; j < engine->macro_count; j++)
        if (engine->macro_entries[j] == id)
          qa_save_reader_fail(&macros_reader, "duplicate macro identity");
      (void)qa_ui_precomp_memory_define(engine->heap, id);
      if (engine->macro_count == engine->macro_cap) {
        size_t cap = engine->macro_cap == 0 ? 8 : engine->macro_cap * 2;
        int64_t *grown = realloc(engine->macro_entries, cap * sizeof *grown);
        if (grown == NULL) {
          qa_save_reader_destroy(&macros_reader);
          goto oom_field;
        }
        engine->macro_entries = grown;
        engine->macro_cap = cap;
      }
      engine->macro_entries[engine->macro_count++] = id;
    }
    qa_save_reader_destroy(&macros_reader);
  }
  qa_save_reader_destroy(&field);
  engine->next_frame = rfld_int_min(&reader, "nextFrame", 1);
  if (!rfld(&reader, "frames", &field)) goto oom;
  {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader cell;
      if (!qa_save_reader_item(&field, i, &cell)) goto oom_field;
      int64_t id = rfld_int_min(&cell, "id", 1);
      if (id >= engine->next_frame
          || (id <= (int64_t)engine->frame_count
              && engine->frames[id - 1] != NULL))
        qa_save_reader_fail(&cell, "invalid frame identity");
      qa_save_reader lexer_field;
      if (!rfld(&cell, "lexer", &lexer_field)) {
        qa_save_reader_destroy(&cell);
        goto oom_field;
      }
      qa_ui_script_lexer *lexer = qa_ui_script_lexer_restore_save_state_env(
          lexer_field.value, engine->has_memory ? engine->memory : NULL,
          engine->has_memory, restore, engine_report_trampoline, engine,
          &engine->env);
      qa_save_reader_destroy(&lexer_field);
      if (lexer == NULL) {
        qa_save_reader_destroy(&cell);
        goto oom_field;
      }
      source_frame *frame = calloc(1, sizeof *frame);
      if (frame == NULL) {
        qa_ui_script_lexer_destroy(lexer);
        qa_save_reader_destroy(&cell);
        goto oom_field;
      }
      frame->id = id;
      frame->lexer = lexer;
      frame->diagnostic_next = rfld_int_min(&cell, "diagnosticNext", 0);
      frame->token_count = rfld_int_min(&cell, "tokenCount", 0);
      qa_save_reader_destroy(&cell);
      if (!grow_frames(engine, (size_t)id)) goto oom_field;
      engine->frames[id - 1] = frame;
      if ((size_t)id > engine->frame_count)
        engine->frame_count = (size_t)id;
      engine->live_frames++;
    }
  }
  qa_save_reader_destroy(&field);
  engine->root_frame = frame_of(engine, rfld_int_min(&reader, "rootFrame", 1));
  if (!rfld(&reader, "cursorToken", &field)) goto oom;
  qa_ui_precomp_token_restore_save_state(engine->cursor_token, field.value,
                                         false);
  qa_save_reader_destroy(&field);
  if (!rfld(&reader, "output", &field)) goto oom;
  qa_ui_precomp_token_restore_save_state(engine->output, field.value, false);
  qa_save_reader_destroy(&field);
  engine->expansion_count = rfld_int_min(&reader, "expansionCount", 0);
  engine->output_count = rfld_int_min(&reader, "outputCount", 0);
  if (!rfld(&reader, "reported", &field)) goto oom;
  {
    size_t count = qa_save_reader_list_count(&field);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader cell;
      if (!qa_save_reader_item(&field, i, &cell)) goto oom_field;
      qa_ui_script_diagnostic diagnostic;
      bool ok = qa_ui_script_read_diagnostic(&cell, &diagnostic)
                && push_reported(engine, &diagnostic);
      if (!ok) qa_ui_script_diagnostic_destroy(&diagnostic);
      qa_save_reader_destroy(&cell);
      if (!ok) goto oom_field;
    }
  }
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
  return engine;
oom_field:
  qa_save_reader_destroy(&field);
oom:
  qa_save_reader_destroy(&reader);
  engine_destroy(engine);
  return NULL;
}

/* ---- ScriptSourceReader ---- */

struct qa_ui_script_source_reader {
  preprocessor_engine *engine;
};

qa_ui_script_source_reader *qa_ui_script_source_reader_open(
    const qa_ui_script_source *root, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options) {
  /* engine_create takes root->storage ownership via create_frame. */
  qa_ui_script_source mutable_root = *root;
  preprocessor_engine *engine =
      engine_create(&mutable_root, resolve, resolve_ctx, options, 0, NULL);
  if (engine == NULL) return NULL;
  qa_ui_script_source_reader *reader = malloc(sizeof *reader);
  if (reader == NULL) {
    engine_destroy(engine);
    return NULL;
  }
  reader->engine = engine;
  return reader;
}

qa_ui_script_source_reader *qa_ui_script_source_reader_restore_save_state(
    const qa_save_value *value, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options,
    const qa_ui_script_memory_restore *restore) {
  preprocessor_engine *engine =
      engine_restore(value, resolve, resolve_ctx, options, restore);
  if (engine == NULL) return NULL;
  qa_ui_script_source_reader *reader = malloc(sizeof *reader);
  if (reader == NULL) {
    engine_destroy(engine);
    return NULL;
  }
  reader->engine = engine;
  return reader;
}

qa_save_value *qa_ui_script_source_reader_capture_save_state(
    qa_ui_script_source_reader *reader,
    const qa_ui_script_memory_capture *capture) {
  return engine_capture(reader->engine, capture);
}

void qa_ui_script_source_reader_dispose(qa_ui_script_source_reader *reader) {
  engine_dispose(reader->engine);
}
void qa_ui_script_source_reader_dispose_record_only(
    qa_ui_script_source_reader *reader) {
  preprocessor_engine *engine = reader->engine;
  if (engine->disposed) return;
  qa_ui_source_record_free_record(engine->source);
  engine->disposed = true;
}
void qa_ui_script_source_reader_destroy(qa_ui_script_source_reader *reader) {
  if (reader == NULL) return;
  engine_destroy(reader->engine);
  free(reader);
}

bool qa_ui_script_source_reader_next(qa_ui_script_source_reader *reader,
                                     qa_ui_script_token_record *out) {
  return engine_next_record(reader->engine, out);
}

bool qa_ui_script_source_reader_next_async(
    qa_ui_script_source_reader *reader, qa_ui_script_validate_fn validate,
    void *validate_ctx, qa_ui_script_token_record *out) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  if (engine->reading_async)
    qa_ui_script_plain_error("Script source already has a pending token read");
  if (validate != NULL) validate(validate_ctx);
  engine->reading_async = true;
  engine->async_validate = validate;
  engine->async_validate_ctx = validate_ctx;
  qa_ui_script_token *token;
  bool found = token_operation(engine, &token);
  engine->reading_async = false;
  engine->async_validate = NULL;
  engine->async_validate_ctx = NULL;
  if (!found) return false;
  qa_ui_script_token_release(token);
  engine_current_record(engine, out);
  return true;
}

qa_ui_source_token_memory *qa_ui_script_source_reader_raw_token(
    qa_ui_script_source_reader *reader) {
  const char *unsupported =
      qa_ui_precomp_token_unsupported(reader->engine->output);
  if (unsupported != NULL)
    qa_range_error("source token profile is unsupported: %s", unsupported);
  return qa_ui_precomp_token_memory(reader->engine->output);
}

void qa_ui_script_source_reader_current_record(
    qa_ui_script_source_reader *reader, qa_ui_script_token_record *out) {
  engine_current_record(reader->engine, out);
}

void qa_ui_script_source_reader_unread(
    qa_ui_script_source_reader *reader,
    const qa_ui_script_token_record *record) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  set_token_metadata(record->token, false, record->subtype,
                     record->integer_value, record->float_value, NULL);
  unread_token(engine, record->token);
}

void qa_ui_script_source_reader_unread_last(
    qa_ui_script_source_reader *reader) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  qa_ui_precomp_token *copied = qa_ui_precomp_memory_copy_token(
      engine->heap, qa_ui_source_record_token(engine->source));
  qa_ui_source_token_set_next(qa_ui_precomp_token_memory(copied),
                              qa_ui_source_record_tokens(engine->source));
  qa_ui_source_record_set_tokens(engine->source,
                                 (uint32_t)qa_ui_precomp_token_id(copied));
}

void qa_ui_script_source_reader_set_include_path(
    qa_ui_script_source_reader *reader, const char *path) {
  require_live(reader->engine);
  qa_ui_source_record_set_include_path(reader->engine->source, path);
}

void qa_ui_script_source_reader_set_punctuations(
    qa_ui_script_source_reader *reader,
    const qa_ui_script_punctuation_spec *specs, size_t count) {
  require_live(reader->engine);
  qa_ui_source_record_set_punctuations(reader->engine->source, specs, count);
}

bool qa_ui_script_source_reader_add_define(
    qa_ui_script_source_reader *reader, const char *text) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  qa_ui_precomp_define *macro = define_from_string(
      text, engine->memory, engine->has_memory, engine->heap,
      record_trampoline, engine);
  if (macro == NULL) return false;
  macro_prepend(engine, macro);
  return true;
}

void qa_ui_script_source_reader_add_builtin_defines(
    qa_ui_script_source_reader *reader) {
  require_live(reader->engine);
  install_builtins(reader->engine);
}

void qa_ui_script_source_reader_print_define_hash_table(
    qa_ui_script_source_reader *reader, qa_ui_script_write_fn write,
    void *write_ctx) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  for (int64_t bucket = 0; bucket < QA_UI_SOURCE_DEFINE_HASH_BUCKETS;
       bucket++) {
    char line[1200];
    snprintf(line, sizeof line, "%4lld:", (long long)bucket);
    write(write_ctx, line);
    for (int64_t id = qa_ui_source_record_hash_head(engine->source, bucket);
         id != 0;) {
      qa_ui_precomp_define *define =
          qa_ui_precomp_memory_define(engine->heap, id);
      char *name = qa_ui_precomp_define_name(define);
      if (name == NULL) oom_fail();
      snprintf(line, sizeof line, " %s", name);
      free(name);
      write(write_ctx, line);
      id = qa_ui_precomp_define_hashnext(define);
    }
    write(write_ctx, "\n");
  }
}

bool qa_ui_script_source_reader_expect_token_string(
    qa_ui_script_source_reader *reader, const char *text) {
  preprocessor_engine *engine = reader->engine;
  qa_ui_script_token_record record;
  char message[1300];
  if (!read_expected(engine, &record)) {
    snprintf(message, sizeof message, "couldn't find expected %s", text);
    engine_error(engine, message);
    return false;
  }
  bool matches = token_text_is(record.token, text);
  if (!matches)
    snprintf(message, sizeof message, "expected %s, found %s", text,
             record.token->text);
  qa_ui_script_token_record_release(&record);
  if (!matches) {
    engine_error(engine, message);
    return false;
  }
  return true;
}

bool qa_ui_script_source_reader_expect_any_token(
    qa_ui_script_source_reader *reader,
    qa_ui_source_token_memory *output) {
  preprocessor_engine *engine = reader->engine;
  qa_ui_script_token_record record;
  bool found = read_expected(engine, &record);
  if (found) qa_ui_script_token_record_release(&record);
  qa_ui_source_token_copy_from(
      output, qa_ui_script_source_reader_raw_token(reader));
  if (found) return true;
  engine_error(engine, "couldn't read expected token");
  return false;
}

bool qa_ui_script_source_reader_expect_token_type(
    qa_ui_script_source_reader *reader, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output) {
  preprocessor_engine *engine = reader->engine;
  if (!qa_ui_script_source_reader_expect_any_token(reader, output))
    return false;
  char found[QA_UI_SOURCE_TOKEN_STRING_BYTES];
  qa_ui_source_token_string(output, found);
  char message[1300];
  if (qa_ui_source_token_type(output) != type) {
    snprintf(message, sizeof message, "expected a %s, found %s",
             qa_ui_script_token_type_name(type), found);
    engine_error(engine, message);
    return false;
  }
  if (type == QA_UI_SCRIPT_TOKEN_NUMBER
      && (qa_ui_source_token_subtype(output) & subtype) != subtype) {
    char *name = qa_ui_script_number_subtype_name(subtype);
    if (name == NULL) oom_fail();
    snprintf(message, sizeof message, "expected %s, found %s", name, found);
    free(name);
    engine_error(engine, message);
    return false;
  }
  if (type == QA_UI_SCRIPT_TOKEN_PUNCTUATION
      && qa_ui_source_token_subtype(output) != subtype) {
    snprintf(message, sizeof message, "found %s", found);
    engine_error(engine, message);
    return false;
  }
  return true;
}

bool qa_ui_script_source_reader_check_token_string(
    qa_ui_script_source_reader *reader, const char *text) {
  preprocessor_engine *engine = reader->engine;
  qa_ui_script_token_record record;
  if (!read_expected(engine, &record)) return false;
  if (token_text_is(record.token, text)) {
    qa_ui_script_token_record_release(&record);
    return true;
  }
  qa_ui_script_source_reader_unread(reader, &record);
  qa_ui_script_token_record_release(&record);
  return false;
}

bool qa_ui_script_source_reader_check_token_type(
    qa_ui_script_source_reader *reader, int64_t type, int64_t subtype,
    qa_ui_source_token_memory *output) {
  preprocessor_engine *engine = reader->engine;
  qa_ui_script_token_record record;
  if (!read_expected(engine, &record)) return false;
  if (qa_ui_source_token_type(qa_ui_script_source_reader_raw_token(reader))
          == type
      && (record.subtype & subtype) == subtype) {
    qa_ui_source_token_copy_from(
        output, qa_ui_script_source_reader_raw_token(reader));
    qa_ui_script_token_record_release(&record);
    return true;
  }
  qa_ui_script_source_reader_unread(reader, &record);
  qa_ui_script_token_record_release(&record);
  return false;
}

bool qa_ui_script_source_reader_skip_until_string(
    qa_ui_script_source_reader *reader, const char *text) {
  qa_ui_script_token_record record;
  while (read_expected(reader->engine, &record)) {
    bool matches = token_text_is(record.token, text);
    qa_ui_script_token_record_release(&record);
    if (matches) return true;
  }
  return false;
}

bool qa_ui_script_source_reader_read_line_into(
    qa_ui_script_source_reader *reader,
    qa_ui_source_token_memory *output) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  qa_ui_precomp_token *current = qa_ui_precomp_token_local();
  if (current == NULL) oom_fail();
  qa_ui_script_catch catch_block;
  catch_block.prev = engine->env.top;
  engine->env.top = &catch_block;
  volatile bool failed = false;
  qa_ui_script_token *volatile result = NULL;
  if (setjmp(catch_block.buf) == 0) {
    /* readLineToken over readSourceToken(current) */
    bool may_cross = false;
    for (;;) {
      qa_ui_script_token *token;
      if (!read_source_token(engine, current, &token)) break;
      if (token->lines_crossed > (may_cross ? 1 : 0)) {
        unread_token(engine, token);
        qa_ui_script_token_release(token);
        break;
      }
      if (token_text_is(token, "\\")) {
        may_cross = true;
        qa_ui_script_token_release(token);
        continue;
      }
      result = token;
      break;
    }
    engine->env.top = catch_block.prev;
  } else {
    engine->env.top = catch_block.prev;
    qa_ui_script_fail_env_clear_pending(&engine->env);
    failed = true;
  }
  qa_ui_source_token_copy_from(output, qa_ui_precomp_token_memory(current));
  qa_ui_precomp_token_destroy(current);
  if (result != NULL)
    qa_ui_script_token_release((qa_ui_script_token *)result);
  return !failed && result != NULL;
}

bool qa_ui_script_source_reader_is_source_failure(
    const qa_ui_script_source_reader *reader,
    const qa_ui_script_diagnostic *candidate) {
  const preprocessor_engine *engine = reader->engine;
  if (!engine->env.has_pending || candidate == NULL) return false;
  for (size_t i = 0; i < engine->reported_count; i++)
    if (&engine->reported[i] == candidate) return true;
  return false;
}

void qa_ui_script_source_reader_position(qa_ui_script_source_reader *reader,
                                         char *filename, size_t capacity,
                                         int64_t *out_line) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  char *name = qa_ui_source_record_filename(engine->source);
  if (name == NULL) oom_fail();
  snprintf(filename, capacity, "%s", name);
  free(name);
  const char *path;
  int64_t column;
  qa_ui_script_lexer_current_location(current_frame(engine)->lexer, &path,
                                      out_line, &column);
}

void qa_ui_script_source_reader_current_filename(
    qa_ui_script_source_reader *reader, char *filename, size_t capacity) {
  preprocessor_engine *engine = reader->engine;
  require_live(engine);
  const char *path;
  int64_t line, column;
  qa_ui_script_lexer_current_location(current_frame(engine)->lexer, &path,
                                      &line, &column);
  snprintf(filename, capacity, "%s", path);
}

const qa_ui_script_diagnostic *qa_ui_script_source_reader_diagnostics(
    const qa_ui_script_source_reader *reader, size_t *out_count) {
  *out_count = reader->engine->reported_count;
  return reader->engine->reported;
}

/* ---- ScriptGlobalDefines ---- */

struct qa_ui_script_global_defines {
  qa_ui_precomp_memory *heap;
  int64_t first;
  qa_ui_script_diagnostic *reported;
  size_t reported_count, reported_cap;
  qa_ui_script_report_fn report;
  void *report_ctx;
  const qa_ui_script_memory *memory;
};

static void globals_report(void *ctx,
                           const qa_ui_script_diagnostic *diagnostic) {
  qa_ui_script_global_defines *globals = ctx;
  qa_ui_script_diagnostic copy;
  if (!copy_diagnostic(diagnostic, &copy)) oom_fail();
  if (globals->reported_count == globals->reported_cap) {
    size_t cap = globals->reported_cap == 0 ? 8 : globals->reported_cap * 2;
    qa_ui_script_diagnostic *grown =
        realloc(globals->reported, cap * sizeof *grown);
    if (grown == NULL) oom_fail();
    globals->reported = grown;
    globals->reported_cap = cap;
  }
  globals->reported[globals->reported_count++] = copy;
  if (globals->report != NULL)
    globals->report(globals->report_ctx,
                    &globals->reported[globals->reported_count - 1]);
}

qa_ui_script_global_defines *qa_ui_script_global_defines_create(
    qa_ui_script_report_fn report, void *report_ctx,
    const qa_ui_script_memory *memory) {
  qa_ui_script_global_defines *globals = calloc(1, sizeof *globals);
  if (globals == NULL) return NULL;
  globals->heap = qa_ui_precomp_memory_create(memory);
  if (globals->heap == NULL) {
    free(globals);
    return NULL;
  }
  globals->report = report;
  globals->report_ctx = report_ctx;
  globals->memory = memory;
  return globals;
}

void qa_ui_script_global_defines_destroy(
    qa_ui_script_global_defines *globals) {
  if (globals == NULL) return;
  qa_ui_precomp_memory_destroy(globals->heap);
  for (size_t i = 0; i < globals->reported_count; i++)
    qa_ui_script_diagnostic_destroy(&globals->reported[i]);
  free(globals->reported);
  free(globals);
}

const qa_ui_script_diagnostic *qa_ui_script_global_defines_diagnostics(
    const qa_ui_script_global_defines *globals, size_t *out_count) {
  *out_count = globals->reported_count;
  return globals->reported;
}

bool qa_ui_script_global_defines_add(qa_ui_script_global_defines *globals,
                                     const char *text) {
  qa_ui_precomp_define *macro = define_from_string(
      text, globals->memory, globals->memory != NULL, globals->heap,
      globals_report, globals);
  if (macro == NULL) return false;
  qa_ui_precomp_define_set_next(macro, (uint32_t)globals->first);
  globals->first = qa_ui_precomp_define_id(macro);
  return true;
}

bool qa_ui_script_global_defines_remove(
    qa_ui_script_global_defines *globals, const char *name) {
  for (int64_t id = globals->first; id != 0;) {
    qa_ui_precomp_define *define =
        qa_ui_precomp_memory_define(globals->heap, id);
    char *define_name = qa_ui_precomp_define_name(define);
    if (define_name == NULL) oom_fail();
    bool same = strcmp(define_name, name) == 0;
    free(define_name);
    if (same) {
      qa_ui_precomp_memory_free_define(globals->heap, define);
      return true;
    }
    id = qa_ui_precomp_define_next(define);
  }
  return false;
}

bool qa_ui_script_global_defines_snapshot(
    qa_ui_script_global_defines *globals,
    qa_ui_script_global_snapshot *out) {
  memset(out, 0, sizeof *out);
  out->owner = qa_ui_precomp_memory_create(NULL);
  if (out->owner == NULL) return false;
  size_t cap = 0;
  for (int64_t id = globals->first; id != 0;) {
    qa_ui_precomp_define *define =
        qa_ui_precomp_memory_define(globals->heap, id);
    if (out->count == cap) {
      cap = cap == 0 ? 4 : cap * 2;
      qa_ui_precomp_define **grown =
          realloc(out->definitions, cap * sizeof *grown);
      if (grown == NULL) {
        free(out->definitions);
        qa_ui_precomp_memory_destroy(out->owner);
        memset(out, 0, sizeof *out);
        return false;
      }
      out->definitions = grown;
    }
    out->definitions[out->count++] =
        qa_ui_precomp_memory_copy_define(out->owner, define);
    id = qa_ui_precomp_define_next(define);
  }
  return true;
}

void qa_ui_script_global_snapshot_destroy(
    qa_ui_script_global_snapshot *snapshot) {
  if (snapshot == NULL) return;
  free(snapshot->definitions);
  qa_ui_precomp_memory_destroy(snapshot->owner);
  memset(snapshot, 0, sizeof *snapshot);
}

void qa_ui_script_global_defines_copy_to(
    qa_ui_script_global_defines *globals, qa_ui_precomp_memory *memory,
    qa_ui_script_publish_define_fn publish, void *publish_ctx) {
  for (int64_t id = globals->first; id != 0;) {
    qa_ui_precomp_define *define =
        qa_ui_precomp_memory_define(globals->heap, id);
    publish(publish_ctx, qa_ui_precomp_memory_copy_define(memory, define));
    id = qa_ui_precomp_define_next(define);
  }
}

void qa_ui_script_global_defines_clear(
    qa_ui_script_global_defines *globals) {
  while (globals->first != 0) {
    qa_ui_precomp_define *define =
        qa_ui_precomp_memory_define(globals->heap, globals->first);
    globals->first = qa_ui_precomp_define_next(define);
    qa_ui_precomp_memory_free_define(globals->heap, define);
  }
}

qa_save_value *qa_ui_script_global_defines_capture_save_state(
    qa_ui_script_global_defines *globals,
    const qa_ui_script_memory_capture *capture) {
  qa_save_value *object = qa_save_value_object();
  qa_save_value *heap =
      qa_ui_precomp_memory_capture_save_state(globals->heap, capture);
  qa_save_value *reported = qa_save_value_array();
  if (object == NULL || heap == NULL || reported == NULL) goto oom;
  if (!set_num(object, "first", (double)globals->first)) goto oom;
  if (!qa_save_value_set(object, "heap", heap)) goto oom;
  heap = NULL;
  for (size_t i = 0; i < globals->reported_count; i++) {
    qa_save_value *entry =
        qa_ui_script_diagnostic_capture(&globals->reported[i]);
    if (entry == NULL || !qa_save_value_push(reported, entry)) {
      qa_save_value_free(entry);
      goto oom;
    }
  }
  if (!qa_save_value_set(object, "reported", reported)) goto oom;
  return object;
oom:
  qa_save_value_free(object);
  qa_save_value_free(heap);
  qa_save_value_free(reported);
  return NULL;
}

void qa_ui_script_global_defines_restore_save_state(
    qa_ui_script_global_defines *globals, const qa_save_value *value,
    const qa_ui_script_memory_restore *restore) {
  qa_save_reader reader, field;
  if (!qa_save_reader_init(&reader, value, "script.globals")) oom_fail();
  if (!rfld(&reader, "heap", &field)) oom_fail();
  qa_ui_precomp_memory_restore_save_state(globals->heap, field.value,
                                          restore);
  qa_save_reader_destroy(&field);
  globals->first = rfld_int_min(&reader, "first", 0);
  for (size_t i = 0; i < globals->reported_count; i++)
    qa_ui_script_diagnostic_destroy(&globals->reported[i]);
  globals->reported_count = 0;
  if (!rfld(&reader, "reported", &field)) oom_fail();
  size_t count = qa_save_reader_list_count(&field);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell;
    if (!qa_save_reader_item(&field, i, &cell)) oom_fail();
    qa_ui_script_diagnostic diagnostic;
    if (!qa_ui_script_read_diagnostic(&cell, &diagnostic)) oom_fail();
    if (globals->reported_count == globals->reported_cap) {
      size_t cap =
          globals->reported_cap == 0 ? 8 : globals->reported_cap * 2;
      qa_ui_script_diagnostic *grown =
          realloc(globals->reported, cap * sizeof *grown);
      if (grown == NULL) oom_fail();
      globals->reported = grown;
      globals->reported_cap = cap;
    }
    globals->reported[globals->reported_count++] = diagnostic;
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
}

typedef struct owned_capture {
  qa_ui_script_allocation **allocations;
  const uint8_t **bytes;
  size_t *lengths;
  size_t count, cap;
  qa_ui_script_global_defines *globals;
} owned_capture;

static int64_t owned_reference(void *ctx,
                               qa_ui_script_allocation *allocation) {
  owned_capture *capture = ctx;
  for (size_t i = 0; i < capture->count; i++)
    if (capture->allocations[i] == allocation) return (int64_t)i;
  if (capture->count == capture->cap) {
    size_t cap = capture->cap == 0 ? 8 : capture->cap * 2;
    capture->allocations =
        realloc(capture->allocations, cap * sizeof *capture->allocations);
    capture->bytes = realloc(capture->bytes, cap * sizeof *capture->bytes);
    capture->lengths =
        realloc(capture->lengths, cap * sizeof *capture->lengths);
    if (capture->allocations == NULL || capture->bytes == NULL
        || capture->lengths == NULL)
      oom_fail();
    capture->cap = cap;
  }
  size_t index = capture->count++;
  capture->allocations[index] = allocation;
  uint8_t *copy = malloc(allocation->length == 0 ? 1 : allocation->length);
  if (copy == NULL) oom_fail();
  memcpy(copy, allocation->bytes, allocation->length);
  capture->bytes[index] = copy;
  capture->lengths[index] = allocation->length;
  return (int64_t)index;
}

qa_save_value *qa_ui_script_global_defines_capture_owned_save_state(
    qa_ui_script_global_defines *globals) {
  owned_capture capture;
  memset(&capture, 0, sizeof capture);
  qa_ui_script_memory_capture api = {&capture, owned_reference};
  qa_save_value *state =
      qa_ui_script_global_defines_capture_save_state(globals, &api);
  qa_save_value *object = qa_save_value_object();
  qa_save_value *allocations = qa_save_value_array();
  bool ok = state != NULL && object != NULL && allocations != NULL;
  for (size_t i = 0; ok && i < capture.count; i++)
    ok = qa_save_value_push(
        allocations,
        qa_save_value_bytes(capture.bytes[i], capture.lengths[i]));
  ok = ok && qa_save_value_set(object, "allocations", allocations);
  if (ok) allocations = NULL;
  ok = ok && qa_save_value_set(object, "state", state);
  if (ok) state = NULL;
  for (size_t i = 0; i < capture.count; i++) free((void *)capture.bytes[i]);
  free(capture.allocations);
  free(capture.bytes);
  free(capture.lengths);
  if (!ok) {
    qa_save_value_free(object);
    qa_save_value_free(allocations);
    qa_save_value_free(state);
    return NULL;
  }
  return object;
}

typedef struct owned_restore {
  qa_ui_script_global_defines *globals;
  const uint8_t **bytes;
  size_t *lengths;
  size_t count;
  qa_ui_script_allocation *allocations;
  bool *created;
} owned_restore;

static qa_ui_script_allocation owned_allocation(void *ctx, int64_t id) {
  owned_restore *restore = ctx;
  if (id < 0 || id >= (int64_t)restore->count)
    qa_ui_script_plain_error("Invalid global define allocation");
  if (!restore->created[id]) {
    qa_ui_script_allocation allocation;
    if (!qa_ui_script_memory_allocate(restore->globals->memory,
                                      restore->lengths[id], false,
                                      &allocation))
      oom_fail();
    memcpy(allocation.bytes, restore->bytes[id], restore->lengths[id]);
    restore->allocations[id] = allocation;
    restore->created[id] = true;
  }
  return restore->allocations[id];
}

void qa_ui_script_global_defines_restore_owned_save_state(
    qa_ui_script_global_defines *globals, const qa_save_value *value) {
  if (globals->first != 0)
    qa_ui_script_plain_error("Global define restore requires an empty owner");
  qa_save_reader reader, field;
  if (!qa_save_reader_init(&reader, value, "script.globalOwner"))
    oom_fail();
  if (!rfld(&reader, "allocations", &field)) oom_fail();
  size_t count = qa_save_reader_list_count(&field);
  owned_restore restore;
  memset(&restore, 0, sizeof restore);
  restore.globals = globals;
  restore.count = count;
  restore.bytes = calloc(count == 0 ? 1 : count, sizeof *restore.bytes);
  restore.lengths = calloc(count == 0 ? 1 : count, sizeof *restore.lengths);
  restore.allocations =
      calloc(count == 0 ? 1 : count, sizeof *restore.allocations);
  restore.created = calloc(count == 0 ? 1 : count, sizeof *restore.created);
  if (restore.bytes == NULL || restore.lengths == NULL
      || restore.allocations == NULL || restore.created == NULL)
    oom_fail();
  for (size_t i = 0; i < count; i++) {
    qa_save_reader cell;
    if (!qa_save_reader_item(&field, i, &cell)) oom_fail();
    restore.bytes[i] = qa_save_reader_bytes(&cell, &restore.lengths[i]);
    qa_save_reader_destroy(&cell);
  }
  qa_save_reader_destroy(&field);
  if (!rfld(&reader, "state", &field)) oom_fail();
  qa_ui_script_memory_restore api = {&restore, owned_allocation};
  qa_ui_script_global_defines_restore_save_state(globals, field.value, &api);
  qa_save_reader_destroy(&field);
  qa_save_reader_destroy(&reader);
  free(restore.bytes);
  free(restore.lengths);
  free(restore.allocations);
  free(restore.created);
}

/* ---- ScriptPreprocessor (eager) ---- */

struct qa_ui_script_preprocessor {
  qa_ui_script_token **tokens;
  size_t count;
  qa_ui_script_diagnostic *reported;
  size_t reported_count;
  size_t index;
  qa_ui_script_token *unread_token;
  preprocessor_engine *engine;
};

qa_ui_script_preprocessor *qa_ui_script_preprocessor_create(
    const qa_ui_script_source *root, qa_ui_script_resolve_fn resolve,
    void *resolve_ctx, const qa_ui_script_preprocessor_options *options) {
  qa_ui_script_source mutable_root = *root;
  preprocessor_engine *engine =
      engine_create(&mutable_root, resolve, resolve_ctx, options, 0, NULL);
  if (engine == NULL) return NULL;
  qa_ui_script_preprocessor *pre = calloc(1, sizeof *pre);
  if (pre == NULL) {
    engine_destroy(engine);
    return NULL;
  }
  size_t cap = 0;
  for (;;) {
    qa_ui_script_token *token;
    if (!engine_next_token(engine, &token)) break;
    if (pre->count == cap) {
      cap = cap == 0 ? 16 : cap * 2;
      qa_ui_script_token **grown =
          realloc(pre->tokens, cap * sizeof *grown);
      if (grown == NULL) {
        qa_ui_script_token_release(token);
        qa_ui_script_preprocessor_destroy(pre);
        engine_destroy(engine);
        return NULL;
      }
      pre->tokens = grown;
    }
    pre->tokens[pre->count++] = token;
  }
  pre->reported_count = engine->reported_count;
  if (pre->reported_count > 0) {
    pre->reported =
        calloc(pre->reported_count, sizeof *pre->reported);
    if (pre->reported == NULL) {
      qa_ui_script_preprocessor_destroy(pre);
      engine_destroy(engine);
      return NULL;
    }
    for (size_t i = 0; i < pre->reported_count; i++) {
      if (!copy_diagnostic(&engine->reported[i], &pre->reported[i])) {
        qa_ui_script_preprocessor_destroy(pre);
        engine_destroy(engine);
        return NULL;
      }
    }
  }
  pre->engine = engine;
  return pre;
}

void qa_ui_script_preprocessor_dispose(qa_ui_script_preprocessor *pre) {
  engine_dispose(pre->engine);
}

void qa_ui_script_preprocessor_destroy(qa_ui_script_preprocessor *pre) {
  if (pre == NULL) return;
  for (size_t i = 0; i < pre->count; i++)
    qa_ui_script_token_release(pre->tokens[i]);
  free(pre->tokens);
  for (size_t i = 0; i < pre->reported_count; i++)
    qa_ui_script_diagnostic_destroy(&pre->reported[i]);
  free(pre->reported);
  qa_ui_script_token_release(pre->unread_token);
  engine_destroy(pre->engine);
  free(pre);
}

const qa_ui_script_diagnostic *qa_ui_script_preprocessor_diagnostics(
    const qa_ui_script_preprocessor *pre, size_t *out_count) {
  *out_count = pre->reported_count;
  return pre->reported;
}

bool qa_ui_script_preprocessor_next(qa_ui_script_preprocessor *pre,
                                    qa_ui_script_token **out) {
  if (pre->unread_token != NULL) {
    *out = pre->unread_token;
    pre->unread_token = NULL;
    return true;
  }
  if (pre->index >= pre->count) return false;
  *out = qa_ui_script_token_retain(pre->tokens[pre->index++]);
  return true;
}

void qa_ui_script_preprocessor_unread(qa_ui_script_preprocessor *pre,
                                      qa_ui_script_token *token) {
  if (pre->unread_token != NULL)
    qa_ui_script_plain_error("ScriptPreprocessor can only unread one token");
  pre->unread_token = qa_ui_script_token_retain(token);
}

void qa_ui_script_preprocessor_reset(qa_ui_script_preprocessor *pre) {
  pre->index = 0;
  qa_ui_script_token_release(pre->unread_token);
  pre->unread_token = NULL;
}

qa_ui_script_token *const *qa_ui_script_preprocessor_all(
    const qa_ui_script_preprocessor *pre, size_t *out_count) {
  *out_count = pre->count;
  return pre->tokens;
}
