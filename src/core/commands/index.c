/* Port of src/core/commands/index.ts. GPL-2.0-or-later. */
#include "qa/core/commands/index.h"

#include <math.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/cvars/numbers.h"
#include "qa/core/numeric.h"
#include "qa/foundation/event_ring.h"
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
static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}
static char *dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out != NULL) {
    memcpy(out, text, length);
    out[length] = '\0';
  }
  return out;
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
static bool folded_equal(const char *a, const char *b) {
  char *fa = qa_ascii_fold(a);
  char *fb = qa_ascii_fold(b);
  bool equal = fa != NULL && fb != NULL && strcmp(fa, fb) == 0;
  free(fa);
  free(fb);
  return equal;
}
static bool same_session_id(qa_session_id a, qa_session_id b) {
  return a.token == b.token;
}

/* --- Context/origin ownership (TS copyOrigin/copyContext) --- */
static qa_command_origin *origin_chain_copy(const qa_command_origin *origin,
                                            qa_session_id session);
static void origin_chain_free(qa_command_origin *origin) {
  while (origin != NULL) {
    qa_command_origin *caller = (qa_command_origin *)origin->caller;
    free((void *)origin->name);
    free(origin);
    origin = caller;
  }
}
static void context_origin_free(qa_command_context *context) {
  free((void *)context->origin.name);
  context->origin.name = NULL;
  origin_chain_free((qa_command_origin *)context->origin.caller);
  context->origin.caller = NULL;
}
static qa_command_origin *origin_chain_copy(const qa_command_origin *origin,
                                            qa_session_id session) {
  qa_command_origin *copy = (qa_command_origin *)calloc(1, sizeof *copy);
  if (copy == NULL) return NULL;
  *copy = *origin;
  copy->name = NULL;
  copy->caller = NULL;
  switch (origin->kind) {
    case QA_COMMAND_ORIGIN_LOCAL_SEAT:
      if (!same_session_id(origin->seat.session, session) ||
          !same_session_id(origin->client.session, session))
        qa_range_error("Command seat belongs to another session");
      break;
    case QA_COMMAND_ORIGIN_REMOTE_CLIENT:
      if (!same_session_id(origin->client.session, session))
        qa_range_error("Command client belongs to another session");
      break;
    case QA_COMMAND_ORIGIN_SCRIPT:
      copy->name = dup_text(origin->name != NULL ? origin->name : "");
      if (origin->caller != NULL)
        copy->caller = origin_chain_copy(origin->caller, session);
      break;
    default: break;
  }
  return copy;
}
static qa_command_context copy_context(qa_command_context context,
                                       const qa_command_origin *origin_override) {
  const qa_command_origin *origin = origin_override != NULL ? origin_override : &context.origin;
  qa_command_origin *copied = origin_chain_copy(origin, context.session);
  qa_command_context result;
  memset(&result, 0, sizeof result);
  result.session = context.session;
  result.producer = context.producer;
  if (copied != NULL) {
    result.origin = *copied;
    free(copied);
  }
  return result;
}
static bool same_origin(const qa_command_origin *left, const qa_command_origin *right) {
  if (left->kind != right->kind) return false;
  switch (left->kind) {
    case QA_COMMAND_ORIGIN_LOCAL_CONSOLE:
    case QA_COMMAND_ORIGIN_SERVER_CONSOLE: return true;
    case QA_COMMAND_ORIGIN_LOCAL_SEAT:
      return qa_same_seat(left->seat, right->seat) && qa_same_client(left->client, right->client);
    case QA_COMMAND_ORIGIN_REMOTE_CLIENT:
      return qa_same_client(left->client, right->client);
    case QA_COMMAND_ORIGIN_SCRIPT:
      if (strcmp(left->name != NULL ? left->name : "", right->name != NULL ? right->name : "") != 0)
        return false;
      if (left->caller == NULL || right->caller == NULL) return left->caller == right->caller;
      return same_origin(left->caller, right->caller);
  }
  return false;
}
static bool same_producer(qa_command_context left, qa_command_context right) {
  const qa_command_producer *first = left.producer;
  const qa_command_producer *second = right.producer;
  if (first == NULL || second == NULL) return first == second;
  return first->instance == second->instance &&
         strcmp(first->module.id, second->module.id) == 0 &&
         strcmp(first->module.digest, second->module.digest) == 0 &&
         strcmp(first->module.artifact_path, second->module.artifact_path) == 0 &&
         strcmp(first->module.revision, second->module.revision) == 0;
}
static double positive_integer(double value, const char *label) {
  if (!(value == trunc(value) && fabs(value) <= 9007199254740991.0) || value < 1)
    qa_range_error("%s must be a positive integer", label);
  return value;
}

/* --- Chunks --- */
typedef struct command_chunk {
  bool is_completion;
  qa_command_dialect dialect;
  qa_command_text_mode text_mode;
  char *text; /* text chunks */
  qa_command_context source; /* owned origin chain */
  bool direct;
  /* completion chunks */
  char *event_name;
  qa_command_context event_source;
  qa_script_result_kind event_result;
  char *event_error;
} command_chunk;

static void chunk_free_contents(command_chunk *chunk) {
  free(chunk->text);
  context_origin_free(&chunk->source);
  free(chunk->event_name);
  context_origin_free(&chunk->event_source);
  free(chunk->event_error);
  memset(chunk, 0, sizeof *chunk);
}
static bool chunk_clone(const command_chunk *source, command_chunk *dest) {
  memset(dest, 0, sizeof *dest);
  dest->is_completion = source->is_completion;
  dest->dialect = source->dialect;
  dest->text_mode = source->text_mode;
  dest->direct = source->direct;
  dest->event_result = source->event_result;
  dest->text = source->text != NULL ? dup_text(source->text) : NULL;
  dest->event_name = source->event_name != NULL ? dup_text(source->event_name) : NULL;
  dest->event_error = source->event_error != NULL ? dup_text(source->event_error) : NULL;
  dest->source = copy_context(source->source, NULL);
  dest->event_source = copy_context(source->event_source, NULL);
  return (source->text == NULL || dest->text != NULL) &&
         (source->event_name == NULL || dest->event_name != NULL);
}

#define CHUNK_RING_CAPACITY 32768
typedef struct chunk_ring { qa_event_ring ring; } chunk_ring;
static bool ring_init(chunk_ring *ring) {
  return qa_event_ring_init(&ring->ring, CHUNK_RING_CAPACITY, sizeof(command_chunk));
}
static void ring_destroy_contents(chunk_ring *ring) {
  size_t count = 0;
  command_chunk *chunks = (command_chunk *)qa_event_ring_drain_all(&ring->ring, &count);
  for (size_t i = 0; i < count; i++) chunk_free_contents(&chunks[i]);
  free(chunks);
  qa_event_ring_destroy(&ring->ring);
}
static int64_t ring_live(chunk_ring *ring) { return qa_event_ring_live(&ring->ring); }
static void ring_admit(chunk_ring *ring, command_chunk *chunk) {
  (void)qa_event_ring_admit(&ring->ring, chunk);
}
static void ring_prepend(chunk_ring *ring, command_chunk *chunks, size_t count) {
  if (count == 0) return;
  qa_event_ring_prepend(&ring->ring, chunks, count);
}
static const command_chunk *ring_head(chunk_ring *ring) {
  return (const command_chunk *)qa_event_ring_head(&ring->ring);
}
static bool ring_shift(chunk_ring *ring, command_chunk *out) {
  return qa_event_ring_shift(&ring->ring, out);
}
static const command_chunk *ring_at(chunk_ring *ring, int64_t id) {
  return (const command_chunk *)qa_event_ring_at(&ring->ring, id);
}
static const command_chunk *ring_at_offset(chunk_ring *ring, int64_t index) {
  return (const command_chunk *)qa_event_ring_at_offset(&ring->ring, index);
}
static void ring_clone_into(chunk_ring *source, chunk_ring *dest) {
  size_t count = 0;
  command_chunk *snapshot = (command_chunk *)qa_event_ring_snapshot(&source->ring, &count);
  for (size_t i = 0; i < count; i++) {
    command_chunk clone;
    if (chunk_clone(&snapshot[i], &clone)) ring_admit(dest, &clone);
  }
  free(snapshot);
}
typedef struct filter_ctx {
  bool (*retired)(void *ctx, qa_command_context source);
  void *ctx;
} filter_ctx;
static qa_command_context chunk_source(const command_chunk *chunk) {
  return chunk->is_completion ? chunk->event_source : chunk->source;
}
static void ring_filter(chunk_ring *ring, bool (*retired)(void *, qa_command_context), void *ctx) {
  size_t count = 0;
  command_chunk *snapshot = (command_chunk *)qa_event_ring_snapshot(&ring->ring, &count);
  qa_event_ring_reset(&ring->ring);
  for (size_t i = 0; i < count; i++) {
    if (!retired(ctx, chunk_source(&snapshot[i]))) ring_admit(ring, &snapshot[i]);
    else chunk_free_contents(&snapshot[i]);
  }
  free(snapshot);
}
typedef struct visit_ctx {
  void (*visit)(void *ctx, const command_chunk *chunk);
  void *ctx;
  bool found;
  bool (*pred)(void *ctx, const command_chunk *chunk);
} visit_ctx;
static void visit_trampoline(const void *record, int64_t id, void *ctx) {
  (void)id;
  visit_ctx *visit = (visit_ctx *)ctx;
  const command_chunk *chunk = (const command_chunk *)record;
  if (visit->visit != NULL) visit->visit(visit->ctx, chunk);
  if (visit->pred != NULL && visit->pred(visit->ctx, chunk)) visit->found = true;
}
static void ring_for_each(chunk_ring *ring, void (*visit)(void *, const command_chunk *), void *ctx) {
  visit_ctx trampoline = {visit, ctx, false, NULL};
  qa_event_ring_for_each_live(&ring->ring, visit_trampoline, &trampoline);
}
static bool ring_some(chunk_ring *ring, bool (*pred)(void *, const command_chunk *), void *ctx) {
  visit_ctx trampoline = {NULL, ctx, false, pred};
  qa_event_ring_for_each_live(&ring->ring, visit_trampoline, &trampoline);
  return trampoline.found;
}

/* --- Buffer state --- */
typedef struct script_read {
  qa_command_dialect dialect;
  qa_command_text_mode text_mode;
  char *name;
  qa_command_context source;
  qa_script_read_kind result;
  bool has_text;
  char *text;
  char *error_message;
  void *poll_ctx;
  qa_script_read_result (*poll)(void *poll_ctx);
  void (*cancel_fn)(void *poll_ctx);
} script_read;
static void script_read_free(script_read *read) {
  if (read == NULL) return;
  free(read->name);
  context_origin_free(&read->source);
  free(read->text);
  free(read->error_message);
  free(read);
}
static void script_read_poll(script_read *read) {
  if (read->result != QA_SCRIPT_READ_PENDING || read->poll == NULL) return;
  qa_script_read_result result = read->poll(read->poll_ctx);
  if (result.kind == QA_SCRIPT_READ_PENDING) {
    free(result.text);
    free(result.error_message);
    return;
  }
  read->result = result.kind;
  read->has_text = result.has_text;
  read->text = result.text;
  read->error_message = result.error_message;
}

typedef struct program_tail program_tail;
struct program_tail {
  chunk_ring chunks;
  chunk_ring deferred;
  int32_t wait_frames;
  bool has_wait_dialect;
  qa_command_dialect wait_dialect;
  bool has_wait_source;
  qa_command_context wait_source;
  script_read *script_read;
  char **tokens;
  size_t token_count;
  int alias_count;
  char *startup_command_text;
  program_tail *preparation_tail;
};
static void tail_free(program_tail *tail) {
  if (tail == NULL) return;
  ring_destroy_contents(&tail->chunks);
  ring_destroy_contents(&tail->deferred);
  context_origin_free(&tail->wait_source);
  script_read_free(tail->script_read);
  for (size_t i = 0; i < tail->token_count; i++) free(tail->tokens[i]);
  free(tail->tokens);
  free(tail->startup_command_text);
  tail_free(tail->preparation_tail);
  free(tail);
}
static program_tail *clone_program_tail(program_tail *tail);

typedef struct registered_entry {
  qa_command_ownership ownership;
  char *name;
  qa_command_handler_fn handler; /* NULL = TS null */
  void *handler_ctx;
  bool has_documentation;
  qa_command_documentation documentation;
  struct registered_entry *next;
} registered_entry;
typedef struct alias_entry {
  char *name;
  const void *instance;
  char *value;
  qa_command_text_mode text_mode;
  qa_command_dialect dialect;
} alias_entry;
typedef struct execution_frame {
  qa_command_dialect dialect;
  qa_command_context source; /* shallow: origin borrowed */
  bool direct;
  qa_command_text_mode text_mode;
  struct execution_frame *parent;
  bool active;
} execution_frame;
typedef struct producer_entry {
  const void *instance;
  qa_command_producer_services services;
  int64_t id;
} producer_entry;
typedef struct output_binding {
  int64_t id;
  qa_command_print_source_fn print;
  void *ctx;
} output_binding;
typedef struct script_listener {
  int64_t id;
  qa_script_complete_fn listener;
  void *ctx;
} script_listener;
typedef struct builtin_entry {
  char *name;
  qa_command_handler_fn handler;
  void *handler_ctx;
} builtin_entry;

struct qa_command_buffer {
  qa_command_buffer_options options;
  qa_command_context context; /* owned origin */
  qa_command_dialect current_dialect;
  bool has_builtin_dialect;
  qa_command_dialect builtin_dialect;
  bool after_dispatch;
  double program_revision;
  bool inherited_async_drain;
  bool has_wait_dialect;
  qa_command_dialect wait_dialect;
  bool has_wait_source;
  qa_command_context wait_source;
  const void **retired_producers;
  size_t retired_producer_count;
  size_t retired_producer_capacity;
  producer_entry *producers;
  size_t producer_count;
  size_t producer_capacity;
  qa_client_id *retired_clients;
  size_t retired_client_count;
  size_t retired_client_capacity;
  qa_cvar_registry *fallback_cvars;
  builtin_entry *builtin_handlers;
  size_t builtin_count;
  size_t builtin_capacity;
  output_binding *output_bindings;
  size_t output_count;
  size_t output_capacity;
  script_listener *script_listeners;
  size_t listener_count;
  size_t listener_capacity;
  registered_entry *handlers;
  alias_entry *aliases;
  size_t alias_count;
  size_t alias_capacity;
  chunk_ring chunks;
  program_tail *preparation_tail;
  chunk_ring deferred;
  int32_t wait_frames;
  char *startup_command_text;
  int alias_count_dispatch;
  bool async_draining;
  script_read *script_read;
  execution_frame *frame;
  qa_command_tokens tokens;
  bool tokens_valid;
  bool batch_active;
  int batch_remaining;
  qa_abort_signal_fn batch_aborted;
  void *batch_signal_ctx;
  double maximum_buffer;
  double maximum_command;
  int64_t next_binding_id;
};

static qa_command_dialect execution_dialect(qa_command_buffer *b) {
  return b->frame != NULL ? b->frame->dialect : b->current_dialect;
}
static void register_builtins(qa_command_buffer *b, qa_command_dialect dialect);
static void select_builtins(qa_command_buffer *b, qa_command_dialect dialect);
static bool buffer_exists(qa_command_buffer *b, const char *name);
static bool buffer_unregister(qa_command_buffer *b, const char *name,
                              qa_command_handler_fn expected, void *expected_ctx,
                              bool has_expected);
static void discard_retired_work(qa_command_buffer *b);
static qa_command_context input_context(qa_command_buffer *b,
                                        const qa_command_context *source, bool own_copy);
static void buffer_print(qa_command_buffer *b, const char *text,
                         const qa_command_context *source);
static void buffer_print_format(qa_command_buffer *b, const qa_command_context *source,
                                const char *format, ...);
static double dispatch(qa_command_buffer *b, const char *raw, qa_command_context source,
                       bool direct, qa_command_text_mode text_mode,
                       qa_command_dialect dialect, bool macro_expansion);
static script_read *script_read_clone(script_read *read);
static bool producer_services(qa_command_buffer *b, qa_command_context source,
                              qa_command_producer_services *out);
static qa_cvar_registry *cvar_owner(qa_command_buffer *b, const char *name,
                                    qa_command_context source);
typedef struct visible_list { qa_cvar_registry **items; size_t count; } visible_list;
static visible_list visible_cvars(qa_command_buffer *b, qa_command_context source);
static void append_for(qa_command_buffer *b, const char *input, qa_command_context source,
                       int direct, int text_mode, int dialect, bool preparation);
static void insert_for(qa_command_buffer *b, const char *input, qa_command_context source,
                       int direct, command_chunk *completion, int text_mode, int dialect);
static void fallback_command(qa_command_buffer *b, qa_command_invocation *command);
static void execute_script_command(qa_command_buffer *b, qa_command_invocation *command);
static void discard_producer_internal(qa_command_buffer *b, const void *instance);
static void discard_client_internal(qa_command_buffer *b, qa_client_id client);
static double drain_loop(qa_command_buffer *b, bool reset_aliases,
                         qa_should_continue_fn should_continue, void *continue_ctx,
                         qa_after_dispatch_fn after, void *after_ctx);

static program_tail *clone_program_tail(program_tail *tail) {
  if (tail == NULL) return NULL;
  program_tail *copy = (program_tail *)calloc(1, sizeof *copy);
  if (copy == NULL) return NULL;
  (void)ring_init(&copy->chunks);
  (void)ring_init(&copy->deferred);
  ring_clone_into(&tail->chunks, &copy->chunks);
  ring_clone_into(&tail->deferred, &copy->deferred);
  copy->wait_frames = tail->wait_frames;
  copy->has_wait_dialect = tail->has_wait_dialect;
  copy->wait_dialect = tail->wait_dialect;
  copy->has_wait_source = tail->has_wait_source;
  if (tail->has_wait_source) copy->wait_source = copy_context(tail->wait_source, NULL);
  copy->script_read = script_read_clone(tail->script_read);
  copy->token_count = tail->token_count;
  copy->tokens = tail->token_count > 0
      ? (char **)calloc(tail->token_count, sizeof(char *)) : NULL;
  for (size_t i = 0; i < tail->token_count; i++) copy->tokens[i] = dup_text(tail->tokens[i]);
  copy->alias_count = tail->alias_count;
  copy->startup_command_text = tail->startup_command_text != NULL
      ? dup_text(tail->startup_command_text) : NULL;
  copy->preparation_tail = clone_program_tail(tail->preparation_tail);
  return copy;
}

qa_command_buffer *qa_command_buffer_create(const qa_command_buffer_options *options) {
  qa_command_buffer *b = (qa_command_buffer *)calloc(1, sizeof *b);
  if (b == NULL) return NULL;
  b->options = *options;
  b->current_dialect = options->dialect;
  b->fallback_cvars = options->cvars;
  b->startup_command_text = options->startup_command_text != NULL
      ? dup_text(options->startup_command_text) : NULL;
  b->context = copy_context(options->context, NULL);
  (void)ring_init(&b->chunks);
  (void)ring_init(&b->deferred);
  b->next_binding_id = 1;
  if (options->cvars != NULL &&
      (!same_session_id(qa_cvar_registry_context(options->cvars).session, b->context.session) ||
       qa_cvar_registry_dialect(options->cvars) != execution_dialect(b)))
    qa_range_error("Commands and cvars require the same session and dialect");
  b->maximum_buffer = positive_integer(options->max_buffer_length > 0
      ? options->max_buffer_length
      : (execution_dialect(b) == QA_COMMAND_DIALECT_Q3 ? 16384 : 8192), "maxBufferLength");
  b->maximum_command = positive_integer(options->max_command_length > 0
      ? options->max_command_length : 1024, "maxCommandLength");
  bool builtins = !options->builtins_set || options->builtins;
  if (builtins) register_builtins(b, execution_dialect(b));
  return b;
}
void qa_command_buffer_destroy(qa_command_buffer *b) {
  if (b == NULL) return;
  context_origin_free(&b->context);
  context_origin_free(&b->wait_source);
  ring_destroy_contents(&b->chunks);
  ring_destroy_contents(&b->deferred);
  tail_free(b->preparation_tail);
  script_read_free(b->script_read);
  registered_entry *entry = b->handlers;
  while (entry != NULL) {
    registered_entry *next = entry->next;
    free(entry->name);
    free(entry);
    entry = next;
  }
  for (size_t i = 0; i < b->alias_count; i++) {
    free(b->aliases[i].name);
    free(b->aliases[i].value);
  }
  free(b->aliases);
  free(b->retired_producers);
  free(b->producers);
  free(b->retired_clients);
  for (size_t i = 0; i < b->builtin_count; i++) free(b->builtin_handlers[i].name);
  free(b->builtin_handlers);
  free(b->output_bindings);
  free(b->script_listeners);
  free(b->startup_command_text);
  if (b->tokens_valid) qa_command_tokens_destroy(&b->tokens);
  free(b);
}
qa_command_dialect qa_command_buffer_dialect(const qa_command_buffer *b) {
  return b->current_dialect;
}
qa_command_context qa_command_buffer_context(const qa_command_buffer *b) {
  return b->context;
}
bool qa_command_buffer_preparation_complete(qa_command_buffer *b) {
  return b->frame == NULL && !b->async_draining && !b->batch_active &&
         b->script_read == NULL && ring_live(&b->chunks) == 0 &&
         ring_live(&b->deferred) == 0 && b->wait_frames == 0;
}
bool qa_command_buffer_program_complete(qa_command_buffer *b) {
  return b->preparation_tail == NULL && qa_command_buffer_preparation_complete(b);
}
bool qa_command_buffer_has_preparation_prefix(qa_command_buffer *b) {
  return b->preparation_tail != NULL;
}
bool qa_command_buffer_has_pending_commands(qa_command_buffer *b) {
  return b->preparation_tail != NULL || ring_live(&b->chunks) > 0 || b->script_read != NULL;
}
static void collect_text(void *ctx, const command_chunk *chunk) {
  text_buffer *text = (text_buffer *)ctx;
  if (!chunk->is_completion && chunk->text != NULL) (void)text_add(text, chunk->text);
}
static char *program_text(qa_command_buffer *b, bool deferred) {
  text_buffer text = {NULL, 0, 0};
  ring_for_each(deferred ? &b->deferred : &b->chunks, collect_text, &text);
  for (program_tail *tail = b->preparation_tail; tail != NULL; tail = tail->preparation_tail)
    ring_for_each(deferred ? &tail->deferred : &tail->chunks, collect_text, &text);
  if (text.data == NULL) return dup_text("");
  return text.data;
}
char *qa_command_buffer_pending_text(qa_command_buffer *b) { return program_text(b, false); }
char *qa_command_buffer_deferred_text(qa_command_buffer *b) { return program_text(b, true); }
char *const *qa_command_buffer_tokenized_arguments(qa_command_buffer *b, size_t *out_count) {
  if (!b->tokens_valid) { *out_count = 0; return NULL; }
  *out_count = b->tokens.argc;
  return b->tokens.argv;
}
double qa_command_buffer_maximum_command_length(const qa_command_buffer *b) {
  return b->maximum_command;
}
double qa_command_buffer_maximum_buffer_length(const qa_command_buffer *b) {
  return b->maximum_buffer;
}
const qa_command_context *qa_command_buffer_execution_context(qa_command_buffer *b) {
  return b->frame != NULL ? &b->frame->source : NULL;
}
void qa_command_buffer_validate_profile(qa_command_buffer *b, qa_command_dialect dialect,
                                        qa_cvar_registry *cvars) {
  if (b->frame != NULL || b->batch_active || (b->async_draining && !b->after_dispatch))
    qa_error("Command profile requires a completed program or after-dispatch boundary");
  if (cvars != NULL &&
      (!same_session_id(qa_cvar_registry_context(cvars).session, b->context.session) ||
       qa_cvar_registry_dialect(cvars) != dialect))
    qa_error("Command profile registry belongs to another session or dialect");
}
void qa_command_buffer_set_profile(qa_command_buffer *b, qa_command_dialect dialect,
                                   qa_cvar_registry *cvars) {
  qa_command_buffer_validate_profile(b, dialect, cvars);
  bool same_profile = b->current_dialect == dialect;
  b->current_dialect = dialect;
  b->fallback_cvars = cvars;
  b->maximum_buffer = b->options.max_buffer_length > 0 ? b->options.max_buffer_length
      : (dialect == QA_COMMAND_DIALECT_Q3 ? 16384 : 8192);
  bool builtins = !b->options.builtins_set || b->options.builtins;
  if (same_profile && builtins) register_builtins(b, dialect);
  else select_builtins(b, dialect);
}
static void select_builtins(qa_command_buffer *b, qa_command_dialect dialect) {
  bool builtins_enabled = !b->options.builtins_set || b->options.builtins;
  if (!builtins_enabled || (b->has_builtin_dialect && b->builtin_dialect == dialect)) return;
  for (size_t i = 0; i < b->builtin_count; i++) {
    for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next) {
      if (strcmp(entry->name, b->builtin_handlers[i].name) == 0 &&
          entry->handler == b->builtin_handlers[i].handler &&
          entry->handler_ctx == b->builtin_handlers[i].handler_ctx) {
        (void)buffer_unregister(b, b->builtin_handlers[i].name, NULL, NULL, false);
        break;
      }
    }
  }
  for (size_t i = 0; i < b->builtin_count; i++) free(b->builtin_handlers[i].name);
  b->builtin_count = 0;
  register_builtins(b, dialect);
}
static void buffer_print(qa_command_buffer *b, const char *text,
                         const qa_command_context *source) {
  qa_command_print_source_fn output = b->options.print;
  void *ctx = b->options.print_ctx;
  for (size_t i = 0; i < b->output_count; i++) {
    output = b->output_bindings[i].print;
    ctx = b->output_bindings[i].ctx;
  }
  if (output != NULL)
    output(ctx, text, source != NULL ? source
                      : b->frame != NULL ? &b->frame->source : NULL);
}
static void buffer_print_format(qa_command_buffer *b, const qa_command_context *source,
                                const char *format, ...) {
  char fixed[4096];
  va_list args;
  va_start(args, format);
  int written = vsnprintf(fixed, sizeof fixed, format, args);
  va_end(args);
  if (written < 0) return;
  if ((size_t)written < sizeof fixed) {
    buffer_print(b, fixed, source);
    return;
  }
  char *dynamic = (char *)malloc((size_t)written + 1);
  if (dynamic == NULL) return;
  va_start(args, format);
  (void)vsnprintf(dynamic, (size_t)written + 1, format, args);
  va_end(args);
  buffer_print(b, dynamic, source);
  free(dynamic);
}
int64_t qa_command_buffer_bind_output(qa_command_buffer *b,
                                      qa_command_print_source_fn print, void *ctx) {
  if (b->output_count == b->output_capacity) {
    size_t cap = b->output_capacity == 0 ? 4 : b->output_capacity * 2;
    output_binding *items = (output_binding *)realloc(b->output_bindings, cap * sizeof *items);
    if (items == NULL) return -1;
    b->output_bindings = items;
    b->output_capacity = cap;
  }
  int64_t id = b->next_binding_id++;
  b->output_bindings[b->output_count].id = id;
  b->output_bindings[b->output_count].print = print;
  b->output_bindings[b->output_count].ctx = ctx;
  b->output_count++;
  return id;
}
void qa_command_buffer_unbind_output(qa_command_buffer *b, int64_t binding) {
  for (size_t i = 0; i < b->output_count; i++) {
    if (b->output_bindings[i].id == binding) {
      memmove(&b->output_bindings[i], &b->output_bindings[i + 1],
              (b->output_count - i - 1) * sizeof *b->output_bindings);
      b->output_count--;
      return;
    }
  }
}
int64_t qa_command_buffer_bind_script_completion(qa_command_buffer *b,
                                                 qa_script_complete_fn listener, void *ctx) {
  if (b->listener_count == b->listener_capacity) {
    size_t cap = b->listener_capacity == 0 ? 4 : b->listener_capacity * 2;
    script_listener *items = (script_listener *)realloc(b->script_listeners, cap * sizeof *items);
    if (items == NULL) return -1;
    b->script_listeners = items;
    b->listener_capacity = cap;
  }
  int64_t id = b->next_binding_id++;
  b->script_listeners[b->listener_count].id = id;
  b->script_listeners[b->listener_count].listener = listener;
  b->script_listeners[b->listener_count].ctx = ctx;
  b->listener_count++;
  return id;
}
void qa_command_buffer_unbind_script_completion(qa_command_buffer *b, int64_t binding) {
  for (size_t i = 0; i < b->listener_count; i++) {
    if (b->script_listeners[i].id == binding) {
      memmove(&b->script_listeners[i], &b->script_listeners[i + 1],
              (b->listener_count - i - 1) * sizeof *b->script_listeners);
      b->listener_count--;
      return;
    }
  }
}

/* --- Small helpers --- */
static bool truthy_number(double value) { return value != 0 && !isnan(value); }
static double buffer_limit_for(qa_command_buffer *b, qa_command_dialect dialect) {
  return b->options.max_buffer_length > 0 ? b->options.max_buffer_length
      : (dialect == QA_COMMAND_DIALECT_Q3 ? 16384 : 8192);
}
static char *join_args(char *const *argv, size_t from, size_t argc) {
  text_buffer text = {NULL, 0, 0};
  for (size_t i = from; i < argc; i++) {
    if (i > from) (void)text_add(&text, " ");
    (void)text_add(&text, argv[i]);
  }
  return text.data != NULL ? text.data : dup_text("");
}
static bool source_client_of(qa_command_context source, qa_client_id *out) {
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller != NULL)
    origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ||
      origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    *out = origin->client;
    return true;
  }
  return false;
}
static bool retired_client(qa_command_buffer *b, qa_command_context source) {
  if (b->retired_client_count == 0) return false;
  qa_client_id client;
  if (!source_client_of(source, &client)) return false;
  for (size_t i = 0; i < b->retired_client_count; i++)
    if (qa_same_client(b->retired_clients[i], client)) return true;
  return false;
}
static bool producer_retired(qa_command_buffer *b, const void *instance) {
  for (size_t i = 0; i < b->retired_producer_count; i++)
    if (b->retired_producers[i] == instance) return true;
  return false;
}
/* Always returns an owned deep copy; free with context_origin_free. */
static qa_command_context input_context(qa_command_buffer *b,
                                        const qa_command_context *source, bool own_copy) {
  (void)own_copy;
  qa_command_context resolved = source != NULL ? *source
      : b->frame != NULL ? b->frame->source : b->context;
  if (!same_session_id(resolved.session, b->context.session))
    qa_range_error("Command input belongs to another session");
  if (retired_client(b, resolved)) qa_error("Command client is disconnected");
  const void *instance = resolved.producer != NULL ? resolved.producer->instance : NULL;
  if (instance != NULL && producer_retired(b, instance))
    qa_error("Command producer is closed");
  return copy_context(resolved, NULL);
}
static qa_command_text_mode input_text_mode(qa_command_buffer *b,
                                            qa_command_context source, bool direct) {
  if (source.origin.kind != QA_COMMAND_ORIGIN_LOCAL_SEAT &&
      source.origin.kind != QA_COMMAND_ORIGIN_LOCAL_CONSOLE)
    return QA_COMMAND_TEXT_SOURCE;
  return b->frame != NULL ? b->frame->text_mode
      : (direct ? QA_COMMAND_TEXT_CONSOLE : QA_COMMAND_TEXT_SOURCE);
}
static bool default_direct(qa_command_buffer *b, qa_command_context source) {
  return b->frame == NULL && source.origin.kind != QA_COMMAND_ORIGIN_SCRIPT;
}

/* --- Producer / cvar routing --- */
static bool producer_services(qa_command_buffer *b, qa_command_context source,
                              qa_command_producer_services *out) {
  if (source.producer == NULL || source.producer->instance == NULL ||
      source.producer->kind == NULL || strcmp(source.producer->kind, "client-module") != 0)
    return false;
  for (size_t i = 0; i < b->producer_count; i++) {
    if (b->producers[i].instance == source.producer->instance) {
      *out = b->producers[i].services;
      if (out->cvars != NULL) (void)out->cvars(out->ctx, source);
      return true;
    }
  }
  qa_error("Component client command producer is not admitted");
}
static qa_cvar_registry *cvar_owner(qa_command_buffer *b, const char *name,
                                    qa_command_context source) {
  qa_cvar_registry *owner = NULL;
  qa_command_producer_services services;
  if (producer_services(b, source, &services) && services.cvars != NULL)
    owner = services.cvars(services.ctx, source);
  if (owner == NULL && b->options.cvar_routing.owner != NULL)
    owner = b->options.cvar_routing.owner(b->options.cvar_routing.ctx, name, source);
  if (owner == NULL) owner = b->fallback_cvars;
  if (owner != NULL &&
      !same_session_id(qa_cvar_registry_context(owner).session, source.session))
    qa_range_error("Command cvar owner belongs to another session");
  return owner;
}
static visible_list visible_cvars(qa_command_buffer *b, qa_command_context source) {
  visible_list list = {NULL, 0};
  qa_command_producer_services services;
  if (producer_services(b, source, &services)) {
    qa_cvar_registry *registry = services.cvars != NULL
        ? services.cvars(services.ctx, source) : NULL;
    if (registry != NULL) {
      list.items = (qa_cvar_registry **)malloc(sizeof *list.items);
      if (list.items != NULL) { list.items[0] = registry; list.count = 1; }
    }
  } else if (b->options.cvar_routing.visible != NULL) {
    size_t count = 0;
    qa_cvar_registry *const *registries =
        b->options.cvar_routing.visible(b->options.cvar_routing.ctx, source, &count);
    if (count > 0 && registries != NULL) {
      list.items = (qa_cvar_registry **)malloc(count * sizeof *list.items);
      if (list.items != NULL) {
        for (size_t i = 0; i < count; i++) list.items[i] = registries[i];
        list.count = count;
      }
    }
  } else if (b->fallback_cvars != NULL) {
    list.items = (qa_cvar_registry **)malloc(sizeof *list.items);
    if (list.items != NULL) { list.items[0] = b->fallback_cvars; list.count = 1; }
  }
  for (size_t i = 0; i < list.count; i++) {
    if (!same_session_id(qa_cvar_registry_context(list.items[i]).session, source.session))
      qa_range_error("Visible cvar owner belongs to another session");
  }
  return list;
}
bool qa_command_buffer_find_cvar(qa_command_buffer *b, const char *name,
                                 const qa_command_context *source, qa_cvar_read *out) {
  qa_command_context context = input_context(b, source, true);
  qa_cvar_registry *owner = cvar_owner(b, name, context);
  bool found = owner != NULL && qa_cvar_registry_find(owner, name, out);
  context_origin_free(&context);
  return found;
}
qa_cvar_snapshot *qa_command_buffer_cvar_snapshots(qa_command_buffer *b,
                                                   const qa_command_context *source,
                                                   size_t *out_count) {
  qa_command_context context = input_context(b, source, true);
  visible_list visible = visible_cvars(b, context);
  qa_cvar_snapshot *result = NULL;
  size_t count = 0, capacity = 0;
  for (size_t i = 0; i < visible.count; i++) {
    size_t variable_count = 0;
    qa_cvar_snapshot *variables =
        qa_cvar_registry_snapshots(visible.items[i], 0, &variable_count);
    for (size_t j = 0; j < variable_count; j++) {
      if (cvar_owner(b, variables[j].name, context) == visible.items[i]) {
        if (count == capacity) {
          capacity = capacity == 0 ? 8 : capacity * 2;
          qa_cvar_snapshot *grown =
              (qa_cvar_snapshot *)realloc(result, capacity * sizeof *grown);
          if (grown == NULL) { qa_cvar_read_free(&variables[j]); continue; }
          result = grown;
        }
        result[count++] = variables[j];
      } else {
        qa_cvar_read_free(&variables[j]);
      }
    }
    free(variables);
  }
  free(visible.items);
  context_origin_free(&context);
  *out_count = count;
  return result;
}
typedef struct archive_include_ctx {
  qa_command_buffer *buffer;
  qa_command_context context;
  qa_cvar_registry *registry;
} archive_include_ctx;
static bool archive_include(void *ctx, const char *name) {
  archive_include_ctx *include = (archive_include_ctx *)ctx;
  return cvar_owner(include->buffer, name, include->context) == include->registry;
}
char **qa_command_buffer_archive_commands(qa_command_buffer *b,
                                          const qa_command_context *source,
                                          size_t *out_count) {
  qa_command_context context = input_context(b, source, true);
  visible_list visible = visible_cvars(b, context);
  char **result = NULL;
  size_t count = 0, capacity = 0;
  for (size_t i = 0; i < visible.count; i++) {
    archive_include_ctx include = {b, context, visible.items[i]};
    size_t line_count = 0;
    char **lines = qa_cvar_registry_archive_commands(visible.items[i], archive_include,
                                                     &include, &line_count);
    for (size_t j = 0; j < line_count; j++) {
      if (count == capacity) {
        capacity = capacity == 0 ? 8 : capacity * 2;
        char **grown = (char **)realloc(result, capacity * sizeof *grown);
        if (grown == NULL) { free(lines[j]); continue; }
        result = grown;
      }
      result[count++] = lines[j];
    }
    free(lines);
  }
  free(visible.items);
  context_origin_free(&context);
  *out_count = count;
  return result;
}

/* --- Registration --- */
static bool buffer_exists(qa_command_buffer *b, const char *name_input) {
  char *name = qa_source_command_text(name_input);
  bool found = false;
  if (name != NULL) {
    for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next)
      if (strcmp(entry->name, name) == 0) found = true;
  }
  free(name);
  return found;
}
bool qa_command_buffer_exists(qa_command_buffer *b, const char *name) {
  return buffer_exists(b, name);
}
bool qa_command_buffer_register(qa_command_buffer *b, const char *name_input,
                                qa_command_handler_fn handler, void *handler_ctx,
                                const qa_command_documentation *documentation,
                                qa_command_ownership ownership) {
  char *name = qa_source_command_text(name_input);
  if (name == NULL) return false;
  if (buffer_exists(b, name)) {
    if (handler != NULL || execution_dialect(b) != QA_COMMAND_DIALECT_Q3)
      buffer_print_format(b, NULL, "Cmd_AddCommand: %s already defined\n", name);
    free(name);
    return false;
  }
  if (execution_dialect(b) != QA_COMMAND_DIALECT_Q3) {
    qa_command_context source = b->frame != NULL ? b->frame->source : b->context;
    qa_cvar_registry *owner = cvar_owner(b, name, source);
    char *variable = owner != NULL ? qa_cvar_registry_variable_string(owner, name) : NULL;
    bool defined = variable != NULL && variable[0] != '\0';
    free(variable);
    if (defined) {
      buffer_print_format(b, NULL, "Cmd_AddCommand: %s already defined as a var\n", name);
      free(name);
      return false;
    }
  }
  registered_entry *entry = (registered_entry *)calloc(1, sizeof *entry);
  if (entry == NULL) { free(name); return false; }
  entry->ownership = ownership;
  entry->name = name;
  entry->handler = handler;
  entry->handler_ctx = handler_ctx;
  if (documentation != NULL) {
    entry->has_documentation = true;
    entry->documentation = *documentation;
  }
  entry->next = b->handlers;
  b->handlers = entry;
  return true;
}
bool qa_command_buffer_register_engine(qa_command_buffer *b, const char *name,
                                       qa_command_handler_fn handler, void *handler_ctx,
                                       const qa_command_documentation *documentation) {
  return qa_command_buffer_register(b, name, handler, handler_ctx, documentation,
                                    QA_COMMAND_ENGINE);
}
bool qa_command_buffer_register_fallback_name(qa_command_buffer *b, const char *name) {
  return qa_command_buffer_register(b, name, NULL, NULL, NULL, QA_COMMAND_WORLD);
}
static bool buffer_unregister(qa_command_buffer *b, const char *name_input,
                              qa_command_handler_fn expected, void *expected_ctx,
                              bool has_expected) {
  char *name = qa_source_command_text(name_input);
  if (name == NULL) return false;
  registered_entry *previous = NULL;
  for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next) {
    if (strcmp(entry->name, name) == 0) {
      if (has_expected && (entry->handler != expected || entry->handler_ctx != expected_ctx)) {
        free(name);
        return false;
      }
      if (previous == NULL) b->handlers = entry->next; else previous->next = entry->next;
      free(entry->name);
      free(entry);
      free(name);
      return true;
    }
    previous = entry;
  }
  free(name);
  return false;
}
bool qa_command_buffer_unregister(qa_command_buffer *b, const char *name,
                                  qa_command_handler_fn expected, void *expected_ctx,
                                  bool has_expected) {
  return buffer_unregister(b, name, expected, expected_ctx, has_expected);
}
char **qa_command_buffer_registered_names(qa_command_buffer *b, size_t *out_count) {
  size_t count = 0;
  for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next) count++;
  char **names = count > 0 ? (char **)calloc(count, sizeof *names) : NULL;
  size_t index = 0;
  for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next)
    if (names != NULL) names[index++] = dup_text(entry->name);
  *out_count = names != NULL ? count : 0;
  return names;
}
const qa_command_documentation *qa_command_buffer_command_documentation(
    qa_command_buffer *b, const char *name) {
  for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next)
    if (folded_equal(entry->name, name))
      return entry->has_documentation ? &entry->documentation : NULL;
  return NULL;
}
const qa_command_documentation *qa_command_buffer_cvar_documentation(
    qa_command_buffer *b, const char *name, const qa_command_context *source) {
  qa_command_context context = input_context(b, source, true);
  qa_cvar_registry *owner = cvar_owner(b, name, context);
  const qa_command_documentation *result =
      owner != NULL ? qa_cvar_registry_documentation(owner, name) : NULL;
  context_origin_free(&context);
  return result;
}
static const void *frame_instance(qa_command_buffer *b) {
  if (b->frame == NULL || b->frame->source.producer == NULL) return NULL;
  return b->frame->source.producer->instance;
}
char *qa_command_buffer_alias_value(qa_command_buffer *b, const char *name) {
  const void *instance = frame_instance(b);
  for (size_t i = 0; i < b->alias_count; i++)
    if (b->aliases[i].instance == instance && folded_equal(b->aliases[i].name, name))
      return dup_text(b->aliases[i].value);
  return NULL;
}
void qa_command_buffer_complete_names(qa_command_buffer *b,
                                      qa_command_name_visitor_fn visitor, void *ctx) {
  for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next)
    visitor(ctx, entry->name);
}
char *qa_command_buffer_complete(qa_command_buffer *b, const char *partial_input) {
  char *partial = qa_source_command_text(partial_input);
  if (partial == NULL || partial[0] == '\0') { free(partial); return NULL; }
  qa_command_dialect dialect = execution_dialect(b);
  bool include_aliases = qa_command_dialect_is_q2(dialect) ||
                         dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  char *result = NULL;
  if (dialect != QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    for (registered_entry *entry = b->handlers; entry != NULL && result == NULL; entry = entry->next)
      if (strcmp(entry->name, partial) == 0) result = dup_text(entry->name);
    if (result == NULL && include_aliases) {
      const void *instance = frame_instance(b);
      for (size_t i = 0; i < b->alias_count && result == NULL; i++)
        if (b->aliases[i].instance == instance && strcmp(b->aliases[i].name, partial) == 0)
          result = dup_text(b->aliases[i].name);
    }
  }
  if (result == NULL) {
    size_t partial_length = strlen(partial);
    char *folded_partial = dialect == QA_COMMAND_DIALECT_Q3 ? qa_ascii_fold(partial) : NULL;
    for (registered_entry *entry = b->handlers; entry != NULL && result == NULL; entry = entry->next) {
      if (dialect == QA_COMMAND_DIALECT_Q3) {
        char *folded = qa_ascii_fold(entry->name);
        if (folded != NULL && folded_partial != NULL &&
            strncmp(folded, folded_partial, partial_length) == 0)
          result = dup_text(entry->name);
        free(folded);
      } else if (strncmp(entry->name, partial, partial_length) == 0) {
        result = dup_text(entry->name);
      }
    }
    if (result == NULL && include_aliases) {
      const void *instance = frame_instance(b);
      for (size_t i = 0; i < b->alias_count && result == NULL; i++) {
        if (b->aliases[i].instance != instance) continue;
        if (dialect == QA_COMMAND_DIALECT_Q3) {
          char *folded = qa_ascii_fold(b->aliases[i].name);
          if (folded != NULL && folded_partial != NULL &&
              strncmp(folded, folded_partial, partial_length) == 0)
            result = dup_text(b->aliases[i].name);
          free(folded);
        } else if (strncmp(b->aliases[i].name, partial, partial_length) == 0) {
          result = dup_text(b->aliases[i].name);
        }
      }
    }
    free(folded_partial);
  }
  free(partial);
  return result;
}
bool qa_command_buffer_define_alias(qa_command_buffer *b, const char *name_input,
                                    const char *text_input) {
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3)
    qa_error("Quake III uses vstr instead of console aliases");
  char *name = qa_source_command_text(name_input);
  char *text = qa_source_command_text(text_input);
  if (name == NULL || text == NULL) { free(name); free(text); return false; }
  if (strlen(name) >= 32) {
    buffer_print(b, "Alias name is too long\n", NULL);
    free(name);
    free(text);
    return false;
  }
  b->program_revision++;
  const void *instance = frame_instance(b);
  qa_command_text_mode text_mode = b->frame != NULL ? b->frame->text_mode
                                                    : QA_COMMAND_TEXT_SOURCE;
  for (size_t i = 0; i < b->alias_count; i++) {
    if (strcmp(b->aliases[i].name, name) == 0 && b->aliases[i].instance == instance) {
      free(b->aliases[i].value);
      b->aliases[i].value = text;
      b->aliases[i].text_mode = text_mode;
      b->aliases[i].dialect = execution_dialect(b);
      free(name);
      return true;
    }
  }
  if (b->alias_count == b->alias_capacity) {
    size_t cap = b->alias_capacity == 0 ? 8 : b->alias_capacity * 2;
    alias_entry *items = (alias_entry *)realloc(b->aliases, cap * sizeof *items);
    if (items == NULL) { free(name); free(text); return false; }
    b->aliases = items;
    b->alias_capacity = cap;
  }
  memmove(&b->aliases[1], &b->aliases[0], b->alias_count * sizeof *b->aliases);
  b->aliases[0].name = name;
  b->aliases[0].instance = instance;
  b->aliases[0].value = text;
  b->aliases[0].text_mode = text_mode;
  b->aliases[0].dialect = execution_dialect(b);
  b->alias_count++;
  return true;
}
char **qa_command_buffer_alias_names(qa_command_buffer *b, size_t *out_count) {
  *out_count = 0;
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) return NULL;
  const void *instance = frame_instance(b);
  size_t count = 0;
  for (size_t i = 0; i < b->alias_count; i++)
    if (b->aliases[i].instance == instance) count++;
  char **names = count > 0 ? (char **)calloc(count, sizeof *names) : NULL;
  size_t index = 0;
  for (size_t i = 0; i < b->alias_count; i++)
    if (b->aliases[i].instance == instance && names != NULL)
      names[index++] = dup_text(b->aliases[i].name);
  *out_count = names != NULL ? count : 0;
  return names;
}

/* --- Text admission --- */
static void append_for(qa_command_buffer *b, const char *input, qa_command_context source,
                       int direct_in, int text_mode_in, int dialect_in, bool preparation) {
  qa_command_dialect dialect = dialect_in >= 0 ? (qa_command_dialect)dialect_in
                                               : execution_dialect(b);
  bool direct = direct_in >= 0 ? direct_in != 0 : default_direct(b, source);
  qa_command_text_mode text_mode = text_mode_in >= 0 ? (qa_command_text_mode)text_mode_in
                                                     : input_text_mode(b, source, direct);
  program_tail *tail = b->preparation_tail;
  while (tail != NULL && tail->preparation_tail != NULL) tail = tail->preparation_tail;
  chunk_ring *target = !preparation && b->frame == NULL && tail != NULL ? &tail->chunks
                                                                         : &b->chunks;
  char *text = qa_source_command_text(input);
  if (text == NULL) return;
  char *pending = program_text(b, false);
  double limit = buffer_limit_for(b, dialect);
  bool overflow = (double)(strlen(pending != NULL ? pending : "") + strlen(text)) >= limit;
  free(pending);
  if (overflow) {
    buffer_print(b, "Cbuf_AddText: overflow\n", NULL);
    free(text);
    return;
  }
  if (text[0] != '\0') {
    command_chunk chunk;
    memset(&chunk, 0, sizeof chunk);
    chunk.dialect = dialect;
    chunk.text_mode = text_mode;
    chunk.text = text;
    chunk.source = copy_context(source, NULL);
    chunk.direct = direct;
    ring_admit(target, &chunk);
    b->program_revision++;
  } else {
    free(text);
  }
}
static void insert_for(qa_command_buffer *b, const char *input, qa_command_context source,
                       int direct_in, command_chunk *completion, int text_mode_in,
                       int dialect_in) {
  qa_command_dialect dialect = dialect_in >= 0 ? (qa_command_dialect)dialect_in
                                               : execution_dialect(b);
  bool direct = direct_in >= 0 ? direct_in != 0 : default_direct(b, source);
  qa_command_text_mode text_mode = text_mode_in >= 0 ? (qa_command_text_mode)text_mode_in
                                                     : input_text_mode(b, source, direct);
  char *base = qa_source_command_text(input);
  if (base == NULL) {
    if (completion != NULL) chunk_free_contents(completion);
    return;
  }
  text_buffer built = {NULL, 0, 0};
  (void)text_add(&built, base);
  free(base);
  if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD || dialect == QA_COMMAND_DIALECT_Q3)
    (void)text_add(&built, "\n");
  char *text = built.data != NULL ? built.data : dup_text("");
  if (text == NULL) {
    if (completion != NULL) chunk_free_contents(completion);
    return;
  }
  double limit = buffer_limit_for(b, dialect);
  char *pending = program_text(b, false);
  size_t pending_length = strlen(pending != NULL ? pending : "");
  free(pending);
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    if ((double)(pending_length + strlen(text)) > limit) {
      buffer_print(b, "Cbuf_InsertText overflowed\n", NULL);
      free(text);
      if (completion != NULL) chunk_free_contents(completion);
      return;
    }
  } else {
    if ((double)strlen(text) >= limit) {
      buffer_print(b, "Cbuf_AddText: overflow\n", NULL);
      free(text);
      if (completion != NULL) chunk_free_contents(completion);
      return;
    }
    if ((double)(pending_length + strlen(text)) > limit) {
      free(text);
      if (completion != NULL) chunk_free_contents(completion);
      qa_range_error("Cbuf_InsertText overflows source sizebuf");
    }
  }
  if (completion != NULL || text[0] != '\0') b->program_revision++;
  if (completion != NULL) ring_prepend(&b->chunks, completion, 1);
  if (text[0] != '\0') {
    command_chunk chunk;
    memset(&chunk, 0, sizeof chunk);
    chunk.dialect = dialect;
    chunk.text_mode = text_mode;
    chunk.text = text;
    chunk.source = copy_context(source, NULL);
    chunk.direct = direct;
    ring_prepend(&b->chunks, &chunk, 1);
  } else {
    free(text);
  }
}
void qa_command_buffer_append(qa_command_buffer *b, const char *text,
                              const qa_command_context *source, int dialect) {
  qa_command_context context = input_context(b, source, true);
  append_for(b, text, context, -1, -1, dialect, false);
  context_origin_free(&context);
}
void qa_command_buffer_append_preparation(qa_command_buffer *b, const char *text,
                                          const qa_command_context *source, int dialect) {
  if (b->preparation_tail == NULL) qa_error("Command program has no preparation prefix");
  qa_command_context context = input_context(b, source, true);
  append_for(b, text, context, -1, -1, dialect, true);
  context_origin_free(&context);
}
void qa_command_buffer_insert(qa_command_buffer *b, const char *text,
                              const qa_command_context *source, int dialect) {
  qa_command_context context = input_context(b, source, true);
  insert_for(b, text, context, -1, NULL, -1, dialect);
  context_origin_free(&context);
}
void qa_command_buffer_copy_to_defer(qa_command_buffer *b) {
  if (!qa_command_dialect_is_q2(execution_dialect(b)))
    qa_error("Deferred command buffers belong to Quake II");
  b->program_revision++;
  ring_destroy_contents(&b->deferred);
  b->deferred = b->chunks;
  (void)ring_init(&b->chunks);
}
void qa_command_buffer_insert_from_defer(qa_command_buffer *b) {
  if (!qa_command_dialect_is_q2(execution_dialect(b)))
    qa_error("Deferred command buffers belong to Quake II");
  char *pending = program_text(b, false);
  char *deferred = program_text(b, true);
  bool overflow = (double)(strlen(pending != NULL ? pending : "") +
                           strlen(deferred != NULL ? deferred : "")) > b->maximum_buffer;
  free(pending);
  free(deferred);
  if (overflow) qa_range_error("Deferred commands overflow source sizebuf");
  b->program_revision++;
  size_t count = 0;
  command_chunk *snapshot =
      (command_chunk *)qa_event_ring_snapshot(&b->deferred.ring, &count);
  if (count > 0) ring_prepend(&b->chunks, snapshot, count);
  free(snapshot);
  qa_event_ring_destroy(&b->deferred.ring);
  (void)ring_init(&b->deferred);
}

/* --- Discarding retired producers/clients --- */
typedef struct discard_pred {
  qa_command_buffer *buffer;
  const void *instance; /* NULL selects client matching */
  bool by_instance;
  qa_client_id client;
} discard_pred;
static bool discard_matches(void *ctx, qa_command_context source) {
  discard_pred *pred = (discard_pred *)ctx;
  if (pred->by_instance)
    return source.producer != NULL && source.producer->instance == pred->instance;
  qa_client_id client;
  return source_client_of(source, &client) && qa_same_client(client, pred->client);
}
static script_read *discard_read(script_read *read, discard_pred *pred) {
  if (read == NULL || !discard_matches(pred, read->source)) return read;
  if (read->cancel_fn != NULL) read->cancel_fn(read->poll_ctx);
  script_read_free(read);
  return NULL;
}
static void discard_tail_sources(program_tail *tail, discard_pred *pred) {
  if (tail == NULL) return;
  ring_filter(&tail->chunks, discard_matches, pred);
  ring_filter(&tail->deferred, discard_matches, pred);
  tail->script_read = discard_read(tail->script_read, pred);
  if (tail->has_wait_source && discard_matches(pred, tail->wait_source)) {
    tail->wait_frames = 0;
    tail->has_wait_dialect = false;
    context_origin_free(&tail->wait_source);
    tail->has_wait_source = false;
  }
  discard_tail_sources(tail->preparation_tail, pred);
}
static void discard_sources(qa_command_buffer *b, discard_pred *pred) {
  ring_filter(&b->chunks, discard_matches, pred);
  ring_filter(&b->deferred, discard_matches, pred);
  b->script_read = discard_read(b->script_read, pred);
  discard_tail_sources(b->preparation_tail, pred);
  if (b->has_wait_source && discard_matches(pred, b->wait_source)) {
    b->wait_frames = 0;
    b->has_wait_dialect = false;
    context_origin_free(&b->wait_source);
    b->has_wait_source = false;
  }
  b->program_revision++;
}
static void discard_producer_internal(qa_command_buffer *b, const void *instance) {
  if (!producer_retired(b, instance)) {
    if (b->retired_producer_count == b->retired_producer_capacity) {
      size_t cap = b->retired_producer_capacity == 0 ? 4 : b->retired_producer_capacity * 2;
      const void **items = (const void **)realloc(b->retired_producers, cap * sizeof *items);
      if (items != NULL) {
        b->retired_producers = items;
        b->retired_producer_capacity = cap;
      }
    }
    if (b->retired_producer_count < b->retired_producer_capacity)
      b->retired_producers[b->retired_producer_count++] = instance;
  }
  discard_pred pred = {b, instance, true, {{NULL, NULL}, 0, 0, NULL}};
  discard_sources(b, &pred);
  for (size_t i = b->alias_count; i-- > 0;) {
    if (b->aliases[i].instance == instance) {
      free(b->aliases[i].name);
      free(b->aliases[i].value);
      memmove(&b->aliases[i], &b->aliases[i + 1],
              (b->alias_count - i - 1) * sizeof *b->aliases);
      b->alias_count--;
    }
  }
}
static void discard_client_internal(qa_command_buffer *b, qa_client_id client) {
  bool present = false;
  for (size_t i = 0; i < b->retired_client_count; i++)
    if (qa_same_client(b->retired_clients[i], client)) present = true;
  if (!present) {
    if (b->retired_client_count == b->retired_client_capacity) {
      size_t cap = b->retired_client_capacity == 0 ? 4 : b->retired_client_capacity * 2;
      qa_client_id *items = (qa_client_id *)realloc(b->retired_clients, cap * sizeof *items);
      if (items != NULL) {
        b->retired_clients = items;
        b->retired_client_capacity = cap;
      }
    }
    if (b->retired_client_count < b->retired_client_capacity)
      b->retired_clients[b->retired_client_count++] = client;
  }
  discard_pred pred = {b, NULL, false, client};
  discard_sources(b, &pred);
}
void qa_command_buffer_discard_producer(qa_command_buffer *b, const void *instance) {
  discard_producer_internal(b, instance);
}
void qa_command_buffer_discard_client(qa_command_buffer *b, qa_client_id client) {
  discard_client_internal(b, client);
}
typedef struct retired_scan {
  qa_command_buffer *buffer;
  const void **instances;
  size_t instance_count, instance_capacity;
  qa_client_id *clients;
  size_t client_count, client_capacity;
} retired_scan;
static void scan_remember(retired_scan *scan, qa_command_context source) {
  if (retired_client(scan->buffer, source)) {
    qa_client_id client;
    if (source_client_of(source, &client)) {
      bool present = false;
      for (size_t i = 0; i < scan->client_count; i++)
        if (qa_same_client(scan->clients[i], client)) present = true;
      if (!present && scan->client_count == scan->client_capacity) {
        size_t cap = scan->client_capacity == 0 ? 4 : scan->client_capacity * 2;
        qa_client_id *items = (qa_client_id *)realloc(scan->clients, cap * sizeof *items);
        if (items != NULL) { scan->clients = items; scan->client_capacity = cap; }
      }
      if (!present && scan->client_count < scan->client_capacity)
        scan->clients[scan->client_count++] = client;
    }
  }
  const void *instance = source.producer != NULL ? source.producer->instance : NULL;
  if (instance != NULL && producer_retired(scan->buffer, instance)) {
    bool present = false;
    for (size_t i = 0; i < scan->instance_count; i++)
      if (scan->instances[i] == instance) present = true;
    if (!present && scan->instance_count == scan->instance_capacity) {
      size_t cap = scan->instance_capacity == 0 ? 4 : scan->instance_capacity * 2;
      const void **items = (const void **)realloc(scan->instances, cap * sizeof *items);
      if (items != NULL) { scan->instances = items; scan->instance_capacity = cap; }
    }
    if (!present && scan->instance_count < scan->instance_capacity)
      scan->instances[scan->instance_count++] = instance;
  }
}
static void scan_chunk(void *ctx, const command_chunk *chunk) {
  scan_remember((retired_scan *)ctx, chunk_source(chunk));
}
static void discard_retired_work(qa_command_buffer *b) {
  retired_scan scan = {b, NULL, 0, 0, NULL, 0, 0};
  ring_for_each(&b->chunks, scan_chunk, &scan);
  ring_for_each(&b->deferred, scan_chunk, &scan);
  if (b->has_wait_source) scan_remember(&scan, b->wait_source);
  if (b->script_read != NULL) scan_remember(&scan, b->script_read->source);
  for (program_tail *tail = b->preparation_tail; tail != NULL; tail = tail->preparation_tail) {
    ring_for_each(&tail->chunks, scan_chunk, &scan);
    ring_for_each(&tail->deferred, scan_chunk, &scan);
    if (tail->has_wait_source) scan_remember(&scan, tail->wait_source);
    if (tail->script_read != NULL) scan_remember(&scan, tail->script_read->source);
  }
  for (size_t i = 0; i < b->alias_count; i++) {
    if (b->aliases[i].instance != NULL && producer_retired(b, b->aliases[i].instance)) {
      qa_command_context fake;
      memset(&fake, 0, sizeof fake);
      qa_command_producer producer;
      memset(&producer, 0, sizeof producer);
      producer.instance = b->aliases[i].instance;
      fake.producer = &producer;
      scan_remember(&scan, fake);
    }
  }
  for (size_t i = 0; i < scan.instance_count; i++)
    discard_producer_internal(b, scan.instances[i]);
  for (size_t i = 0; i < scan.client_count; i++)
    discard_client_internal(b, scan.clients[i]);
  free(scan.instances);
  free(scan.clients);
}
static bool owns_instance(qa_command_context source, const void *instance) {
  return source.producer != NULL && source.producer->instance == instance;
}
static bool chunk_owns_instance(void *ctx, const command_chunk *chunk) {
  return owns_instance(chunk_source(chunk), *(const void **)ctx);
}
bool qa_command_buffer_producer_pending(qa_command_buffer *b, const void *instance) {
  if (b->frame != NULL && owns_instance(b->frame->source, instance)) return true;
  if (b->has_wait_source && owns_instance(b->wait_source, instance)) return true;
  if (b->script_read != NULL && owns_instance(b->script_read->source, instance)) return true;
  const void *target = instance;
  if (ring_some(&b->chunks, chunk_owns_instance, &target)) return true;
  if (ring_some(&b->deferred, chunk_owns_instance, &target)) return true;
  for (program_tail *tail = b->preparation_tail; tail != NULL; tail = tail->preparation_tail) {
    if (tail->has_wait_source && owns_instance(tail->wait_source, instance)) return true;
    if (tail->script_read != NULL && owns_instance(tail->script_read->source, instance))
      return true;
    if (ring_some(&tail->chunks, chunk_owns_instance, &target)) return true;
    if (ring_some(&tail->deferred, chunk_owns_instance, &target)) return true;
  }
  return false;
}
int64_t qa_command_buffer_bind_producer(qa_command_buffer *b, const void *instance,
                                        qa_command_producer_services services) {
  for (size_t i = 0; i < b->producer_count; i++)
    if (b->producers[i].instance == instance)
      qa_error("Command producer is already bound or retired");
  if (producer_retired(b, instance))
    qa_error("Command producer is already bound or retired");
  if (b->producer_count == b->producer_capacity) {
    size_t cap = b->producer_capacity == 0 ? 4 : b->producer_capacity * 2;
    producer_entry *items = (producer_entry *)realloc(b->producers, cap * sizeof *items);
    if (items == NULL) return -1;
    b->producers = items;
    b->producer_capacity = cap;
  }
  int64_t id = b->next_binding_id++;
  b->producers[b->producer_count].instance = instance;
  b->producers[b->producer_count].services = services;
  b->producers[b->producer_count].id = id;
  b->producer_count++;
  return id;
}
void qa_command_buffer_unbind_producer(qa_command_buffer *b, int64_t binding) {
  for (size_t i = 0; i < b->producer_count; i++) {
    if (b->producers[i].id == binding) {
      const void *instance = b->producers[i].instance;
      memmove(&b->producers[i], &b->producers[i + 1],
              (b->producer_count - i - 1) * sizeof *b->producers);
      b->producer_count--;
      discard_producer_internal(b, instance);
      return;
    }
  }
}

/* --- Program state copy / preparation --- */
static script_read *script_read_clone(script_read *read) {
  if (read == NULL) return NULL;
  script_read *copy = (script_read *)calloc(1, sizeof *copy);
  if (copy == NULL) return NULL;
  copy->dialect = read->dialect;
  copy->text_mode = read->text_mode;
  copy->name = read->name != NULL ? dup_text(read->name) : NULL;
  copy->source = copy_context(read->source, NULL);
  copy->result = read->result;
  copy->has_text = read->has_text;
  copy->text = read->text != NULL ? dup_text(read->text) : NULL;
  copy->error_message = read->error_message != NULL ? dup_text(read->error_message) : NULL;
  copy->poll_ctx = read->poll_ctx;
  copy->poll = read->poll;
  copy->cancel_fn = read->cancel_fn;
  return copy;
}
static void free_program_state(qa_command_buffer *b) {
  ring_destroy_contents(&b->chunks);
  ring_destroy_contents(&b->deferred);
  tail_free(b->preparation_tail);
  b->preparation_tail = NULL;
  script_read_free(b->script_read);
  b->script_read = NULL;
  context_origin_free(&b->wait_source);
  b->has_wait_source = false;
  if (b->tokens_valid) qa_command_tokens_destroy(&b->tokens);
  b->tokens_valid = false;
  for (size_t i = 0; i < b->alias_count; i++) {
    free(b->aliases[i].name);
    free(b->aliases[i].value);
  }
  free(b->aliases);
  b->aliases = NULL;
  b->alias_count = 0;
  b->alias_capacity = 0;
  free(b->startup_command_text);
  b->startup_command_text = NULL;
  free(b->retired_producers);
  b->retired_producers = NULL;
  b->retired_producer_count = 0;
  b->retired_producer_capacity = 0;
  free(b->retired_clients);
  b->retired_clients = NULL;
  b->retired_client_count = 0;
  b->retired_client_capacity = 0;
}
static void copy_program_state(qa_command_buffer *dest, qa_command_buffer *src) {
  free_program_state(dest);
  (void)ring_init(&dest->chunks);
  (void)ring_init(&dest->deferred);
  ring_clone_into(&src->chunks, &dest->chunks);
  ring_clone_into(&src->deferred, &dest->deferred);
  dest->preparation_tail = clone_program_tail(src->preparation_tail);
  dest->wait_frames = src->wait_frames;
  dest->has_wait_dialect = src->has_wait_dialect;
  dest->wait_dialect = src->wait_dialect;
  dest->has_wait_source = src->has_wait_source;
  if (src->has_wait_source) dest->wait_source = copy_context(src->wait_source, NULL);
  if (src->retired_producer_count > 0) {
    dest->retired_producers = (const void **)malloc(
        src->retired_producer_count * sizeof *dest->retired_producers);
    if (dest->retired_producers != NULL) {
      memcpy(dest->retired_producers, src->retired_producers,
             src->retired_producer_count * sizeof *dest->retired_producers);
      dest->retired_producer_count = src->retired_producer_count;
      dest->retired_producer_capacity = src->retired_producer_count;
    }
  }
  if (src->retired_client_count > 0) {
    dest->retired_clients =
        (qa_client_id *)malloc(src->retired_client_count * sizeof *dest->retired_clients);
    if (dest->retired_clients != NULL) {
      memcpy(dest->retired_clients, src->retired_clients,
             src->retired_client_count * sizeof *dest->retired_clients);
      dest->retired_client_count = src->retired_client_count;
      dest->retired_client_capacity = src->retired_client_count;
    }
  }
  dest->alias_count_dispatch = src->alias_count_dispatch;
  if (src->tokens_valid) {
    qa_command_tokens tokens;
    memset(&tokens, 0, sizeof tokens);
    tokens.argc = src->tokens.argc;
    tokens.argv = tokens.argc > 0 ? (char **)calloc(tokens.argc, sizeof(char *)) : NULL;
    for (size_t i = 0; i < tokens.argc; i++) tokens.argv[i] = dup_text(src->tokens.argv[i]);
    tokens.args_text = dup_text(src->tokens.args_text != NULL ? src->tokens.args_text : "");
    dest->tokens = tokens;
    dest->tokens_valid = true;
  }
  dest->startup_command_text = src->startup_command_text != NULL
      ? dup_text(src->startup_command_text) : NULL;
  dest->script_read = script_read_clone(src->script_read);
  if (src->alias_count > 0) {
    dest->aliases = (alias_entry *)calloc(src->alias_count, sizeof *dest->aliases);
    if (dest->aliases != NULL) {
      dest->alias_capacity = src->alias_count;
      for (size_t i = 0; i < src->alias_count; i++) {
        dest->aliases[i].name = dup_text(src->aliases[i].name);
        dest->aliases[i].instance = src->aliases[i].instance;
        dest->aliases[i].value = dup_text(src->aliases[i].value);
        dest->aliases[i].text_mode = src->aliases[i].text_mode;
        dest->aliases[i].dialect = src->aliases[i].dialect;
        dest->alias_count++;
      }
    }
  }
  discard_retired_work(dest);
}
void qa_command_buffer_copy_pending_from(qa_command_buffer *b, qa_command_buffer *previous) {
  if (b->frame != NULL || b->async_draining || previous->frame != NULL ||
      previous->async_draining)
    qa_error("Cannot copy commands during execution");
  if (!same_session_id(b->context.session, previous->context.session))
    qa_error("Commands belong to another session");
  char *pending = qa_command_buffer_pending_text(previous);
  char *deferred = qa_command_buffer_deferred_text(previous);
  bool overflow = (double)(strlen(pending != NULL ? pending : "") +
                           strlen(deferred != NULL ? deferred : "")) >= b->maximum_buffer;
  free(pending);
  free(deferred);
  if (overflow) qa_error("Replacement command buffer cannot hold pending commands");
  copy_program_state(b, previous);
  b->program_revision++;
}
static void restore_tail(qa_command_buffer *b, program_tail *tail) {
  ring_destroy_contents(&b->chunks);
  ring_destroy_contents(&b->deferred);
  context_origin_free(&b->wait_source);
  script_read_free(b->script_read);
  if (b->tokens_valid) qa_command_tokens_destroy(&b->tokens);
  free(b->startup_command_text);
  b->chunks = tail->chunks;
  b->deferred = tail->deferred;
  b->wait_frames = tail->wait_frames;
  b->has_wait_dialect = tail->has_wait_dialect;
  b->wait_dialect = tail->wait_dialect;
  b->has_wait_source = tail->has_wait_source;
  b->wait_source = tail->wait_source;
  memset(&tail->wait_source, 0, sizeof tail->wait_source);
  b->script_read = tail->script_read;
  b->tokens.argv = tail->tokens;
  b->tokens.argc = tail->token_count;
  b->tokens.args_text = dup_text("");
  b->tokens_valid = true;
  b->alias_count_dispatch = tail->alias_count;
  b->startup_command_text = tail->startup_command_text;
  b->preparation_tail = tail->preparation_tail;
  free(tail);
  discard_retired_work(b);
}
void qa_command_buffer_finish_preparation(qa_command_buffer *b) {
  program_tail *tail = b->preparation_tail;
  if (tail == NULL) qa_error("Command program has no preparation prefix");
  if (!qa_command_buffer_preparation_complete(b))
    qa_error("Prepared configuration has unfinished commands");
  restore_tail(b, tail);
  b->program_revision++;
}
typedef enum prepared_phase {
  PREPARED_READY, PREPARED_PREPARING, PREPARED_FAILED, PREPARED_PUBLISHED
} prepared_phase;
typedef struct prepared_state {
  qa_command_buffer *authority;
  qa_command_buffer *commands;
  double revision;
  prepared_phase phase;
  qa_command_buffer_options options;
  qa_prepared_program pub;
} prepared_state;
static const char *phase_name(prepared_phase phase) {
  switch (phase) {
    case PREPARED_READY: return "ready";
    case PREPARED_PREPARING: return "preparing";
    case PREPARED_FAILED: return "failed";
    case PREPARED_PUBLISHED: return "published";
  }
  return "ready";
}
static void prepared_validate(prepared_state *state) {
  if (state->phase == PREPARED_PUBLISHED)
    qa_error("Prepared command program already published");
  if (state->phase != PREPARED_READY)
    qa_error("Prepared command program is %s", phase_name(state->phase));
  qa_command_buffer_validate_profile(state->authority, state->authority->current_dialect,
                                     state->authority->fallback_cvars);
  if (state->authority->program_revision != state->revision)
    qa_error("Authority command program changed during preparation");
  if (state->commands->frame != NULL || state->commands->async_draining ||
      state->commands->batch_active)
    qa_error("Prepared command program is still executing");
}
static bool prepared_prepare_prefix(void *ctx, qa_prepare_prefix_run_fn run, void *run_ctx) {
  prepared_state *state = (prepared_state *)ctx;
  prepared_validate(state);
  qa_command_buffer *commands = state->commands;
  bool inherited = commands->inherited_async_drain;
  program_tail *pending = (program_tail *)calloc(1, sizeof *pending);
  if (pending == NULL) return false;
  pending->chunks = commands->chunks;
  pending->deferred = commands->deferred;
  pending->wait_frames = commands->wait_frames;
  pending->has_wait_dialect = commands->has_wait_dialect;
  pending->wait_dialect = commands->wait_dialect;
  pending->has_wait_source = commands->has_wait_source;
  pending->wait_source = commands->wait_source;
  memset(&commands->wait_source, 0, sizeof commands->wait_source);
  pending->script_read = commands->script_read;
  if (commands->tokens_valid) {
    pending->token_count = commands->tokens.argc;
    pending->tokens = pending->token_count > 0
        ? (char **)calloc(pending->token_count, sizeof(char *)) : NULL;
    for (size_t i = 0; i < pending->token_count; i++)
      pending->tokens[i] = dup_text(commands->tokens.argv[i]);
    qa_command_tokens_destroy(&commands->tokens);
    commands->tokens_valid = false;
  }
  pending->alias_count = commands->alias_count_dispatch;
  pending->startup_command_text = commands->startup_command_text;
  pending->preparation_tail = commands->preparation_tail;
  state->phase = PREPARED_PREPARING;
  commands->preparation_tail = pending;
  (void)ring_init(&commands->chunks);
  (void)ring_init(&commands->deferred);
  commands->wait_frames = 0;
  commands->has_wait_dialect = false;
  commands->has_wait_source = false;
  commands->script_read = NULL;
  commands->alias_count_dispatch = 0;
  commands->startup_command_text = state->options.startup_command_text != NULL
      ? dup_text(state->options.startup_command_text) : NULL;
  commands->inherited_async_drain = false;
  bool complete = run(run_ctx);
  if (complete) qa_command_buffer_finish_preparation(commands);
  state->phase = PREPARED_READY;
  commands->inherited_async_drain = inherited;
  return complete;
}
static void prepared_validate_publication(void *ctx) {
  prepared_validate((prepared_state *)ctx);
}
static void prepared_publish(void *ctx) {
  prepared_state *state = (prepared_state *)ctx;
  prepared_validate(state);
  copy_program_state(state->authority, state->commands);
  state->authority->program_revision++;
  state->phase = PREPARED_PUBLISHED;
}
static void prepared_destroy(void *ctx) {
  prepared_state *state = (prepared_state *)ctx;
  qa_command_buffer_destroy(state->commands);
  free(state);
}
qa_prepared_program *qa_command_buffer_prepare_program(
    qa_command_buffer *b, const qa_command_buffer_options *options) {
  qa_command_buffer_validate_profile(b, b->current_dialect, b->fallback_cvars);
  if (!same_session_id(options->context.session, b->context.session) ||
      !same_origin(&options->context.origin, &b->context.origin))
    qa_error("Prepared command program requires the same owner");
  qa_command_buffer *commands = qa_command_buffer_create(options);
  if (commands == NULL) return NULL;
  prepared_state *state = (prepared_state *)calloc(1, sizeof *state);
  if (state == NULL) {
    qa_command_buffer_destroy(commands);
    return NULL;
  }
  state->authority = b;
  state->commands = commands;
  state->revision = b->program_revision;
  state->phase = PREPARED_READY;
  state->options = *options;
  copy_program_state(commands, b);
  commands->inherited_async_drain = b->async_draining;
  state->pub.ctx = state;
  state->pub.commands = commands;
  state->pub.prepare_prefix = prepared_prepare_prefix;
  state->pub.validate_publication = prepared_validate_publication;
  state->pub.publish = prepared_publish;
  state->pub.destroy = prepared_destroy;
  return &state->pub;
}

/* --- Script completion --- */
void qa_script_completion_free(qa_script_completion *event) {
  if (event == NULL) return;
  free(event->name);
  context_origin_free(&event->source);
  free(event->error_message);
  memset(event, 0, sizeof *event);
}
static void make_completion_chunk(command_chunk *out, const char *name,
                                  qa_command_context caller, qa_script_result_kind result,
                                  const char *error_message, qa_command_dialect dialect,
                                  qa_command_text_mode text_mode) {
  memset(out, 0, sizeof *out);
  out->is_completion = true;
  out->dialect = dialect;
  out->text_mode = text_mode;
  out->event_name = dup_text(name);
  out->event_result = result;
  out->event_error = error_message != NULL ? dup_text(error_message) : NULL;
  qa_command_origin script_origin;
  memset(&script_origin, 0, sizeof script_origin);
  script_origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  script_origin.name = name;
  script_origin.caller = &caller.origin;
  out->event_source = copy_context(caller, &script_origin);
}
static void insert_script(qa_command_buffer *b, const char *filename, bool has_file,
                          const char *file, qa_command_context caller,
                          qa_command_dialect dialect, qa_command_text_mode text_mode) {
  if (!has_file) {
    buffer_print_format(b, &caller, "couldn't exec %s\n", filename);
    command_chunk completion;
    make_completion_chunk(&completion, filename, caller, QA_SCRIPT_MISSING, NULL,
                          dialect, text_mode);
    ring_prepend(&b->chunks, &completion, 1);
    return;
  }
  buffer_print_format(b, &caller, "execing %s\n", filename);
  char *text = qa_source_command_text(file != NULL ? file : "");
  if (text == NULL) return;
  text_buffer built = {NULL, 0, 0};
  (void)text_add(&built, text);
  free(text);
  size_t length = built.data != NULL ? strlen(built.data) : 0;
  if (qa_command_dialect_is_q1(dialect) && (length == 0 || built.data[length - 1] != '\n'))
    (void)text_add(&built, "\n");
  command_chunk completion;
  make_completion_chunk(&completion, filename, caller, QA_SCRIPT_COMPLETED, NULL,
                        dialect, text_mode);
  qa_command_context script_source = completion.event_source;
  insert_for(b, built.data != NULL ? built.data : "", script_source, 0, &completion,
             (int)text_mode, (int)dialect);
  free(built.data);
}
static void execute_script_command(qa_command_buffer *b, qa_command_invocation *command) {
  if (command->argc != 2) {
    buffer_print(b, "exec <filename> : execute a script file\n", NULL);
    return;
  }
  if (b->script_read != NULL) {
    text_buffer retry = {NULL, 0, 0};
    (void)text_add(&retry, command->raw != NULL ? command->raw : "");
    (void)text_add(&retry, "\n");
    insert_for(b, retry.data != NULL ? retry.data : "", command->source, -1, NULL, -1, -1);
    free(retry.data);
    return;
  }
  const char *requested = command->argv[1];
  char *filename = NULL;
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) {
    const char *slash = strrchr(requested, '/');
    const char *base = slash != NULL ? slash + 1 : requested;
    if (strchr(base, '.') == NULL) {
      text_buffer built = {NULL, 0, 0};
      (void)text_add(&built, requested);
      (void)text_add(&built, ".cfg");
      filename = built.data;
    }
  }
  if (filename == NULL) filename = dup_text(requested);
  if (filename == NULL) return;
  qa_command_producer_services services;
  bool has_producer = producer_services(b, command->source, &services);
  qa_read_script_fn read_script = has_producer ? services.read_script
                                               : b->options.read_script;
  void *read_ctx = has_producer ? services.read_script_ctx : b->options.read_script_ctx;
  if (read_script == NULL) {
    insert_script(b, filename, false, NULL, command->source, execution_dialect(b),
                  b->frame != NULL ? b->frame->text_mode : QA_COMMAND_TEXT_SOURCE);
    free(filename);
    return;
  }
  qa_script_read_result result = read_script(read_ctx, filename, command->source);
  if (result.kind == QA_SCRIPT_READ_READY) {
    insert_script(b, filename, result.has_text, result.text, command->source,
                  execution_dialect(b),
                  b->frame != NULL ? b->frame->text_mode : QA_COMMAND_TEXT_SOURCE);
    free(result.text);
    free(result.error_message);
    free(filename);
    return;
  }
  script_read *read = (script_read *)calloc(1, sizeof *read);
  if (read == NULL) {
    free(result.text);
    free(result.error_message);
    free(filename);
    return;
  }
  read->name = filename;
  read->source = copy_context(command->source, NULL);
  read->dialect = command->dialect;
  read->text_mode = b->frame != NULL ? b->frame->text_mode : QA_COMMAND_TEXT_SOURCE;
  read->result = result.kind;
  read->has_text = result.has_text;
  read->text = result.text;
  read->error_message = result.error_message;
  read->poll_ctx = result.poll_ctx;
  read->poll = result.poll;
  read->cancel_fn = result.cancel;
  b->program_revision++;
  b->script_read = read;
}

/* --- Invocation --- */
static void require_active(qa_command_invocation *command) {
  qa_command_buffer *b = command->buffer;
  if (retired_client(b, command->source)) qa_error("Command client is disconnected");
  execution_frame *frame = (execution_frame *)command->frame;
  if (frame == NULL || !frame->active || b->frame != frame)
    qa_error("Command invocation is no longer active");
}
void qa_command_invocation_assert_active(qa_command_invocation *command) {
  require_active(command);
}
void qa_command_invocation_append(qa_command_invocation *command, const char *text) {
  require_active(command);
  append_for(command->buffer, text, command->source, -1, -1, -1, false);
}
void qa_command_invocation_insert(qa_command_invocation *command, const char *text) {
  require_active(command);
  insert_for(command->buffer, text, command->source, -1, NULL, -1, -1);
}
double qa_command_invocation_execute_now(qa_command_invocation *command, const char *text) {
  require_active(command);
  return qa_command_buffer_execute_now(command->buffer, text, NULL, -1);
}
void qa_command_invocation_execute_script(qa_command_invocation *command) {
  require_active(command);
  execute_script_command(command->buffer, command);
}
void qa_command_buffer_forward_to_server(qa_command_buffer *b,
                                         qa_command_invocation *command) {
  require_active(command);
  qa_command_producer_services services;
  if (producer_services(b, command->source, &services)) {
    if (services.command != NULL) services.command(services.ctx, command);
    return;
  }
  if (b->options.forward_to_server != NULL)
    b->options.forward_to_server(b->options.forward_to_server_ctx, command);
}
static void fallback_command(qa_command_buffer *b, qa_command_invocation *command) {
  const char *name = command->argc > 0 ? command->argv[0] : "";
  qa_cvar_registry *cvars = cvar_owner(b, name, command->source);
  if (cvars != NULL) {
    qa_cvar_read variable;
    memset(&variable, 0, sizeof variable);
    if (qa_cvar_registry_find(cvars, name, &variable)) {
      if (command->argc < 2) {
        if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) {
          buffer_print_format(b, NULL, "\"%s\" is:\"%s^7\" default:\"%s^7\"\n",
                              variable.name, variable.value, variable.reset_value);
          if (variable.latched_value != NULL)
            buffer_print_format(b, NULL, "latched: \"%s\"\n", variable.latched_value);
        } else {
          buffer_print_format(b, NULL, "\"%s\" is \"%s\"\n", variable.name, variable.value);
        }
      } else {
        qa_cvar_snapshot snapshot;
        memset(&snapshot, 0, sizeof snapshot);
        if (qa_cvar_registry_set(cvars, variable.name, command->argv[1], false, &snapshot))
          qa_cvar_read_free(&snapshot);
      }
      qa_cvar_read_free(&variable);
      return;
    }
  }
  qa_command_producer_services services;
  if (producer_services(b, command->source, &services)) {
    if (services.command != NULL) services.command(services.ctx, command);
    return;
  }
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) {
    if ((b->options.client_game != NULL &&
         b->options.client_game(b->options.client_game_ctx, command) == 1) ||
        (b->options.server_game != NULL &&
         b->options.server_game(b->options.server_game_ctx, command) == 1) ||
        (b->options.ui != NULL && b->options.ui(b->options.ui_ctx, command) == 1))
      return;
  }
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3 ||
      qa_command_dialect_is_q2(execution_dialect(b))) {
    qa_command_buffer_forward_to_server(b, command);
    return;
  }
  bool warn = execution_dialect(b) != QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  if (!warn) {
    qa_cvar_read read;
    memset(&read, 0, sizeof read);
    if (qa_command_buffer_find_cvar(b, "cl_warncmd", &command->source, &read)) {
      warn = truthy_number(read.numeric_value);
      qa_cvar_read_free(&read);
    }
    if (!warn && qa_command_buffer_find_cvar(b, "developer", &command->source, &read)) {
      warn = truthy_number(read.numeric_value);
      qa_cvar_read_free(&read);
    }
  }
  if (warn) buffer_print_format(b, NULL, "Unknown command \"%s\"\n", name);
}

/* --- Dispatch --- */
typedef struct macro_ctx {
  qa_command_buffer *buffer;
  qa_command_context source;
  char *scratch;
} macro_ctx;
static const char *macro_variable(void *ctx, const char *name) {
  macro_ctx *macro = (macro_ctx *)ctx;
  free(macro->scratch);
  macro->scratch = NULL;
  qa_cvar_registry *owner = cvar_owner(macro->buffer, name, macro->source);
  if (owner == NULL) return "";
  qa_cvar_read variable;
  memset(&variable, 0, sizeof variable);
  if (!qa_cvar_registry_find(owner, name, &variable)) return "";
  const char *value = variable.value;
  if (qa_command_dialect_is_q2(qa_cvar_registry_dialect(owner)) &&
      (variable.flags & QA_Q2_CVAR_FLAG_PRIVATE) != 0)
    value = "";
  macro->scratch = dup_text(value != NULL ? value : "");
  qa_cvar_read_free(&variable);
  return macro->scratch != NULL ? macro->scratch : "";
}
static void macro_print(void *ctx, const char *text) {
  buffer_print((qa_command_buffer *)ctx, text, NULL);
}
static void set_buffer_tokens(qa_command_buffer *b, const qa_command_tokens *tokens) {
  if (b->tokens_valid) qa_command_tokens_destroy(&b->tokens);
  memset(&b->tokens, 0, sizeof b->tokens);
  b->tokens.argc = tokens != NULL ? tokens->argc : 0;
  b->tokens.argv = b->tokens.argc > 0 ? (char **)calloc(b->tokens.argc, sizeof(char *)) : NULL;
  for (size_t i = 0; i < b->tokens.argc; i++) b->tokens.argv[i] = dup_text(tokens->argv[i]);
  b->tokens.args_text = dup_text(tokens != NULL && tokens->args_text != NULL
                                     ? tokens->args_text : "");
  b->tokens_valid = true;
}
static void touch_entry(qa_command_buffer *b, registered_entry *entry) {
  if (entry == b->handlers) return;
  for (registered_entry *previous = b->handlers; previous != NULL; previous = previous->next) {
    if (previous->next != entry) continue;
    previous->next = entry->next;
    entry->next = b->handlers;
    b->handlers = entry;
    return;
  }
}
static bool builtin_handler_for(qa_command_buffer *b, const char *name,
                                qa_command_handler_fn *out_fn, void **out_ctx) {
  for (size_t i = 0; i < b->builtin_count; i++) {
    if (folded_equal(b->builtin_handlers[i].name, name)) {
      *out_fn = b->builtin_handlers[i].handler;
      *out_ctx = b->builtin_handlers[i].handler_ctx;
      return true;
    }
  }
  return false;
}
static double dispatch(qa_command_buffer *b, const char *raw, qa_command_context source,
                       bool direct, qa_command_text_mode text_mode,
                       qa_command_dialect dialect, bool macro_expansion) {
  b->program_revision++;
  if (b->batch_active && b->batch_aborted != NULL && b->batch_aborted(b->batch_signal_ctx))
    qa_error("Command batch cancelled; remaining batch discarded.");
  if (b->batch_active && --b->batch_remaining < 0)
    qa_error("Command batch exceeded 128 dispatched commands; remaining batch discarded.");
  char *expanded_owned = NULL;
  const char *expanded = raw;
  if (qa_command_dialect_is_q2(dialect) && macro_expansion) {
    macro_ctx macro = {b, source, NULL};
    char *out = NULL;
    bool ok = qa_expand_command_macros(raw, macro_variable, &macro, macro_print, b,
                                       text_mode, &out);
    free(macro.scratch);
    if (!ok) {
      set_buffer_tokens(b, NULL);
      return 0;
    }
    expanded_owned = out;
    expanded = out != NULL ? out : "";
  }
  qa_command_tokens tokens;
  memset(&tokens, 0, sizeof tokens);
  if (!qa_tokenize_command(expanded, dialect, text_mode, &tokens)) {
    free(expanded_owned);
    return 0;
  }
  set_buffer_tokens(b, &tokens);
  if (tokens.argc == 0) {
    qa_command_tokens_destroy(&tokens);
    free(expanded_owned);
    return 0;
  }
  const char *name = tokens.argv[0];
  execution_frame frame;
  memset(&frame, 0, sizeof frame);
  frame.dialect = dialect;
  frame.source = source;
  frame.direct = direct && (source.origin.kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE ||
                             source.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT);
  frame.text_mode = text_mode;
  frame.parent = b->frame;
  frame.active = true;
  b->frame = &frame;
  select_builtins(b, dialect);
  qa_command_invocation command;
  memset(&command, 0, sizeof command);
  command.source = source;
  command.direct = frame.direct;
  command.dialect = execution_dialect(b);
  command.argv = tokens.argv;
  command.argc = tokens.argc;
  command.args_text = tokens.args_text;
  command.raw = raw;
  command.buffer = b;
  command.frame = &frame;
  double result = 1;
  bool handled = false;
  if (b->options.allow_command != NULL &&
      !b->options.allow_command(b->options.allow_command_ctx, &command)) {
    handled = true;
  }
  if (!handled) {
    qa_command_handler_fn builtin_fn = NULL;
    void *builtin_ctx = NULL;
    bool has_builtin = builtin_handler_for(b, name, &builtin_fn, &builtin_ctx);
    registered_entry *selected = b->handlers;
    while (selected != NULL && !folded_equal(selected->name, name)) selected = selected->next;
    bool selected_is_builtin = has_builtin && selected != NULL &&
        selected->handler == builtin_fn && selected->handler_ctx == builtin_ctx;
    qa_command_producer_services services;
    bool has_producer = producer_services(b, source, &services);
    if (has_producer && selected != NULL && selected->ownership != QA_COMMAND_ENGINE &&
        !selected_is_builtin &&
        !(services.engine_command != NULL && services.engine_command(services.ctx, name))) {
      if (services.command != NULL) services.command(services.ctx, &command);
      handled = true;
    }
    if (!handled && (selected == NULL || selected->ownership != QA_COMMAND_ENGINE) &&
        !selected_is_builtin && b->options.source_command != NULL &&
        b->options.source_command(b->options.source_command_ctx, &command,
                                  selected != NULL) == 1) {
      handled = true;
    }
    if (!handled) {
      for (registered_entry *entry = b->handlers; entry != NULL; entry = entry->next) {
        if (!folded_equal(entry->name, name)) continue;
        if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) touch_entry(b, entry);
        if (entry->handler != NULL) {
          entry->handler(entry->handler_ctx, &command);
        } else if (qa_command_dialect_is_q2(execution_dialect(b))) {
          text_buffer nested = {NULL, 0, 0};
          (void)text_add(&nested, "cmd ");
          (void)text_add(&nested, raw);
          (void)dispatch(b, nested.data != NULL ? nested.data : "", source, false,
                         input_text_mode(b, source, false), execution_dialect(b), true);
          free(nested.data);
        } else if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) {
          fallback_command(b, &command);
        } else {
          qa_error("Quake I command registration has a null callback");
        }
        handled = true;
        break;
      }
    }
    if (!handled && execution_dialect(b) != QA_COMMAND_DIALECT_Q3) {
      const void *instance = frame_instance(b);
      for (size_t i = 0; i < b->alias_count; i++) {
        if (b->aliases[i].instance != instance || !folded_equal(b->aliases[i].name, name))
          continue;
        if (qa_command_dialect_is_q2(execution_dialect(b)) &&
            ++b->alias_count_dispatch == 16) {
          buffer_print(b, "ALIAS_LOOP_COUNT\n", NULL);
          handled = true;
          break;
        }
        char *value = dup_text(b->aliases[i].value);
        qa_command_text_mode alias_mode = b->aliases[i].text_mode;
        qa_command_dialect alias_dialect = b->aliases[i].dialect;
        insert_for(b, value != NULL ? value : "", source, 0, NULL, (int)alias_mode,
                   (int)alias_dialect);
        free(value);
        handled = true;
        break;
      }
    }
    if (!handled) fallback_command(b, &command);
  }
  frame.active = false;
  b->frame = frame.parent;
  select_builtins(b, execution_dialect(b));
  qa_command_tokens_destroy(&tokens);
  free(expanded_owned);
  return result;
}

/* --- Drain --- */
static char *command_text(qa_command_buffer *b, const command_chunk *first) {
  text_buffer text = {NULL, 0, 0};
  const qa_command_origin *origin = &first->source.origin;
  bool resumed_caller = false;
  int64_t first_id = qa_event_ring_stats_of(&b->chunks.ring).first;
  for (int64_t id = first_id;; id++) {
    const command_chunk *chunk = ring_at(&b->chunks, id);
    if (chunk == NULL) break;
    if (chunk->dialect != first->dialect || chunk->text_mode != first->text_mode) break;
    if (!same_producer(first->source, chunk_source(chunk))) break;
    if (chunk->is_completion) {
      if (!qa_command_dialect_is_q2(first->dialect) ||
          origin->kind != QA_COMMAND_ORIGIN_SCRIPT ||
          chunk->event_result != QA_SCRIPT_COMPLETED ||
          !same_origin(&chunk->event_source.origin, origin))
        break;
      origin = origin->caller;
      if (origin == NULL) break;
      resumed_caller = true;
      continue;
    }
    if ((!resumed_caller && chunk->direct != first->direct) ||
        !same_origin(&chunk->source.origin, origin))
      break;
    if (chunk->text != NULL) (void)text_add(&text, chunk->text);
  }
  return text.data != NULL ? text.data : dup_text("");
}
static void consume_chunks(qa_command_buffer *b, size_t count) {
  if (count > 0) b->program_revision++;
  int64_t index = 0;
  while (count > 0) {
    const command_chunk *chunk = ring_at_offset(&b->chunks, index);
    if (chunk == NULL) return;
    if (chunk->is_completion) {
      index++;
      continue;
    }
    size_t length = chunk->text != NULL ? strlen(chunk->text) : 0;
    if (length > count) {
      command_chunk replacement = *chunk;
      replacement.text = dup_text(chunk->text + count);
      free(chunk->text);
      qa_event_ring_replace_at_offset(&b->chunks.ring, index, &replacement);
      return;
    }
    command_chunk removed = *chunk;
    qa_event_ring_remove_at_offset(&b->chunks.ring, index);
    chunk_free_contents(&removed);
    count -= length;
  }
}
static double drain_loop(qa_command_buffer *b, bool reset_aliases,
                         qa_should_continue_fn should_continue, void *continue_ctx,
                         qa_after_dispatch_fn after, void *after_ctx) {
  if (reset_aliases) b->alias_count_dispatch = 0;
  double executed = 0;
  while ((ring_live(&b->chunks) > 0 || b->script_read != NULL) &&
         (should_continue == NULL || should_continue(continue_ctx))) {
    if (b->script_read != NULL) {
      script_read_poll(b->script_read);
      if (b->script_read->result == QA_SCRIPT_READ_PENDING) return executed;
      script_read *script = b->script_read;
      b->program_revision++;
      b->script_read = NULL;
      if (script->result == QA_SCRIPT_READ_FAILED) {
        buffer_print_format(b, &script->source, "couldn't exec %s: %s\n",
                            script->name != NULL ? script->name : "",
                            script->error_message != NULL ? script->error_message : "");
        command_chunk completion;
        make_completion_chunk(&completion, script->name != NULL ? script->name : "",
                              script->source, QA_SCRIPT_FAILED, script->error_message,
                              script->dialect, script->text_mode);
        ring_prepend(&b->chunks, &completion, 1);
      } else {
        insert_script(b, script->name != NULL ? script->name : "", script->has_text,
                      script->text, script->source, script->dialect, script->text_mode);
      }
      script_read_free(script);
      continue;
    }
    bool wait_is_q3 = b->has_wait_dialect && b->wait_dialect == QA_COMMAND_DIALECT_Q3;
    if (wait_is_q3 && b->wait_frames != 0) {
      b->program_revision++;
      b->wait_frames = (int32_t)(b->wait_frames - 1);
      break;
    }
    const command_chunk *first = ring_head(&b->chunks);
    if (first == NULL) break;
    if (first->is_completion) {
      b->program_revision++;
      command_chunk chunk;
      memset(&chunk, 0, sizeof chunk);
      if (!ring_shift(&b->chunks, &chunk)) break;
      qa_script_completion event;
      memset(&event, 0, sizeof event);
      event.name = chunk.event_name;
      chunk.event_name = NULL;
      event.source = chunk.event_source;
      memset(&chunk.event_source, 0, sizeof chunk.event_source);
      event.result = chunk.event_result;
      event.error_message = chunk.event_error;
      chunk.event_error = NULL;
      event.buffer = b;
      execution_frame frame;
      memset(&frame, 0, sizeof frame);
      frame.dialect = chunk.dialect;
      frame.source = event.source;
      frame.direct = false;
      frame.text_mode = chunk.text_mode;
      frame.parent = b->frame;
      frame.active = true;
      b->frame = &frame;
      select_builtins(b, chunk.dialect);
      if (b->options.on_script_complete != NULL)
        b->options.on_script_complete(b->options.on_script_complete_ctx, &event);
      for (size_t i = 0; i < b->listener_count; i++)
        if (b->script_listeners[i].listener != NULL)
          b->script_listeners[i].listener(b->script_listeners[i].ctx, &event);
      frame.active = false;
      b->frame = frame.parent;
      select_builtins(b, execution_dialect(b));
      qa_script_completion_free(&event);
      chunk_free_contents(&chunk);
      continue;
    }
    qa_command_dialect dialect = first->dialect;
    qa_command_text_mode text_mode = first->text_mode;
    bool direct = first->direct;
    qa_command_context source = copy_context(first->source, NULL);
    char *buffer = command_text(b, first);
    size_t offset = qa_command_separator_offset(buffer != NULL ? buffer : "", dialect);
    if ((double)offset >= b->maximum_command) {
      if (dialect != QA_COMMAND_DIALECT_Q3) {
        free(buffer);
        context_origin_free(&source);
        qa_range_error("Command line overflows source line buffer");
      }
      offset = (size_t)b->maximum_command - 1;
    }
    size_t buffer_length = strlen(buffer != NULL ? buffer : "");
    char *line = dup_range(buffer != NULL ? buffer : "", offset);
    size_t consumed = offset == buffer_length ? offset : offset + 1;
    free(buffer);
    consume_chunks(b, consumed);
    double count = dispatch(b, line != NULL ? line : "", source, direct, text_mode,
                            dialect, true);
    free(line);
    context_origin_free(&source);
    if (count != 0) {
      executed += count;
      if (after != NULL) {
        b->after_dispatch = true;
        after(after_ctx);
        b->after_dispatch = false;
      }
    }
    bool wait_q3_now = b->has_wait_dialect && b->wait_dialect == QA_COMMAND_DIALECT_Q3;
    if (!wait_q3_now && b->wait_frames != 0) {
      b->program_revision++;
      b->wait_frames = 0;
      break;
    }
  }
  return executed;
}
double qa_command_buffer_execute(qa_command_buffer *b) {
  if (b->async_draining || b->inherited_async_drain)
    qa_error("Command buffer is already draining asynchronously");
  return drain_loop(b, true, NULL, NULL, NULL, NULL);
}
typedef struct drain_continue {
  qa_command_buffer *buffer;
  program_tail *prefix;
  qa_should_continue_fn should;
  void *ctx;
} drain_continue;
static bool drain_can_continue(void *ctx) {
  drain_continue *cont = (drain_continue *)ctx;
  return cont->buffer->preparation_tail == cont->prefix &&
         (cont->should == NULL || cont->should(cont->ctx));
}
static double drain_async(qa_command_buffer *b, qa_after_dispatch_fn after, void *after_ctx,
                          bool await_scripts, qa_should_continue_fn should_continue,
                          void *continue_ctx) {
  if (b->async_draining || b->inherited_async_drain || b->frame != NULL)
    qa_error("Command buffer is already executing");
  b->async_draining = true;
  drain_continue cont = {b, b->preparation_tail, should_continue, continue_ctx};
  double executed = 0;
  bool first_drain = true;
  for (;;) {
    executed += drain_loop(b, first_drain, drain_can_continue, &cont, after, after_ctx);
    script_read *read = b->script_read;
    if (!await_scripts || read == NULL || !drain_can_continue(&cont)) break;
    bool settled = false;
    while (!settled) {
      script_read_poll(read);
      if (read->result != QA_SCRIPT_READ_PENDING) {
        settled = true;
      } else if (after != NULL) {
        b->after_dispatch = true;
        after(after_ctx);
        b->after_dispatch = false;
        if (b->script_read != read) settled = true;
      } else {
        break;
      }
    }
    if (!settled) break;
    first_drain = false;
  }
  b->async_draining = false;
  return executed;
}
double qa_command_buffer_execute_async(qa_command_buffer *b,
                                       qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                       qa_should_continue_fn should_continue,
                                       void *continue_ctx) {
  return drain_async(b, after_dispatch, after_ctx, false, should_continue, continue_ctx);
}
double qa_command_buffer_execute_scripts_async(qa_command_buffer *b,
                                               qa_after_dispatch_fn after_dispatch,
                                               void *after_ctx,
                                               qa_should_continue_fn should_continue,
                                               void *continue_ctx) {
  return drain_async(b, after_dispatch, after_ctx, true, should_continue, continue_ctx);
}
static void noop_after_dispatch(void *ctx) { (void)ctx; }
void qa_command_buffer_advance_program_frame(qa_command_buffer *b) {
  if (b->frame != NULL || b->async_draining || b->batch_active)
    qa_error("Command program is already executing");
  if (ring_live(&b->deferred) != 0) qa_command_buffer_insert_from_defer(b);
  if (ring_live(&b->chunks) == 0 && b->script_read == NULL && b->wait_frames != 0) {
    b->program_revision++;
    b->wait_frames = (int32_t)(b->wait_frames - 1);
    return;
  }
  (void)qa_command_buffer_execute_scripts_async(b, noop_after_dispatch, NULL, NULL, NULL);
}
double qa_command_buffer_execute_now(qa_command_buffer *b, const char *text,
                                     const qa_command_context *source, int dialect) {
  char *value = text == NULL ? dup_text("") : qa_source_command_text(text);
  if (value == NULL || value[0] == '\0') {
    free(value);
    return qa_command_buffer_execute(b);
  }
  qa_command_context context = input_context(b, source, true);
  bool direct = b->frame == NULL;
  qa_command_dialect use_dialect = dialect >= 0 ? (qa_command_dialect)dialect
                                                : execution_dialect(b);
  double result = dispatch(b, value, context, direct,
                           input_text_mode(b, context, direct), use_dialect, true);
  context_origin_free(&context);
  free(value);
  return result;
}

/* --- Batches --- */
typedef struct batch_saved {
  chunk_ring chunks;
  chunk_ring deferred;
  int32_t wait_frames;
  bool has_wait_dialect;
  qa_command_dialect wait_dialect;
  bool has_wait_source;
  qa_command_context wait_source;
  int alias_count;
  qa_command_tokens tokens;
  bool tokens_valid;
  script_read *script_read;
} batch_saved;
static void begin_batch(qa_command_buffer *b, const char *text, qa_command_context source,
                        qa_abort_signal_fn aborted, void *signal_ctx, bool append,
                        batch_saved *saved) {
  /* The TS throws when a frame is active; its only executeBatch
   * caller (console/llm) runs the batch after an await, outside any
   * frame. The synchronous C port runs that batch inside the handler,
   * so an active frame is admitted here (the saved outer program is
   * restored untouched). Async drains still reject, as in the TS. */
  if (b->async_draining || b->inherited_async_drain)
    qa_error("Command buffer is already executing");
  qa_command_context context = input_context(b, &source, true);
  char *value = qa_source_command_text(text);
  if (value == NULL || strcmp(value, text) != 0 ||
      (double)strlen(value) >= b->maximum_buffer) {
    free(value);
    context_origin_free(&context);
    qa_range_error("Command batch exceeds the engine buffer limit.");
  }
  bool quoted = false;
  size_t length = 0;
  for (const char *p = value; *p != '\0'; p++) {
    if (*p == '"') quoted = !quoted;
    if ((!quoted && *p == ';') || *p == '\n' ||
        (execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && *p == '\r')) {
      length = 0;
    } else if ((double)++length >= b->maximum_command) {
      free(value);
      context_origin_free(&context);
      qa_range_error("Command batch line exceeds the engine line limit.");
    }
  }
  if (b->batch_active) {
    free(value);
    context_origin_free(&context);
    qa_error("Nested command batches are not supported.");
  }
  memset(saved, 0, sizeof *saved);
  saved->chunks = b->chunks;
  saved->deferred = b->deferred;
  saved->wait_frames = b->wait_frames;
  saved->has_wait_dialect = b->has_wait_dialect;
  saved->wait_dialect = b->wait_dialect;
  saved->has_wait_source = b->has_wait_source;
  saved->wait_source = b->wait_source;
  memset(&b->wait_source, 0, sizeof b->wait_source);
  saved->alias_count = b->alias_count_dispatch;
  saved->tokens = b->tokens;
  saved->tokens_valid = b->tokens_valid;
  saved->script_read = b->script_read;
  b->script_read = NULL;
  (void)ring_init(&b->chunks);
  (void)ring_init(&b->deferred);
  b->wait_frames = 0;
  b->has_wait_source = false;
  b->alias_count_dispatch = 0;
  memset(&b->tokens, 0, sizeof b->tokens);
  b->tokens_valid = false;
  b->batch_active = true;
  b->batch_remaining = 128;
  b->batch_aborted = aborted;
  b->batch_signal_ctx = signal_ctx;
  if (append) append_for(b, value, context, 0, -1, -1, true);
  free(value);
  context_origin_free(&context);
}
static void restore_batch(qa_command_buffer *b, batch_saved *saved) {
  ring_destroy_contents(&b->chunks);
  ring_destroy_contents(&b->deferred);
  context_origin_free(&b->wait_source);
  script_read_free(b->script_read);
  if (b->tokens_valid) qa_command_tokens_destroy(&b->tokens);
  b->chunks = saved->chunks;
  b->deferred = saved->deferred;
  b->wait_frames = saved->wait_frames;
  b->has_wait_dialect = saved->has_wait_dialect;
  b->wait_dialect = saved->wait_dialect;
  b->has_wait_source = saved->has_wait_source;
  b->wait_source = saved->wait_source;
  b->alias_count_dispatch = saved->alias_count;
  b->tokens = saved->tokens;
  b->tokens_valid = saved->tokens_valid;
  b->batch_active = false;
  b->batch_aborted = NULL;
  b->batch_signal_ctx = NULL;
  b->script_read = saved->script_read;
  discard_retired_work(b);
}
static void complete_batch(qa_command_buffer *b) {
  if (ring_live(&b->chunks) > 0 || ring_live(&b->deferred) > 0 ||
      b->script_read != NULL || b->wait_frames != 0)
    qa_error("Command batch paused or deferred execution; remaining batch discarded. "
             "Use immediate commands.");
}
double qa_command_buffer_execute_batch(qa_command_buffer *b, const char *text,
                                       qa_command_context source,
                                       qa_abort_signal_fn aborted, void *signal_ctx) {
  batch_saved saved;
  begin_batch(b, text, source, aborted, signal_ctx, true, &saved);
  double count = qa_command_buffer_execute(b);
  complete_batch(b);
  restore_batch(b, &saved);
  return count;
}
double qa_command_buffer_execute_batch_async(qa_command_buffer *b, const char *text,
                                             qa_command_context source,
                                             qa_after_dispatch_fn after_dispatch,
                                             void *after_ctx, qa_abort_signal_fn aborted,
                                             void *signal_ctx) {
  batch_saved saved;
  begin_batch(b, text, source, aborted, signal_ctx, true, &saved);
  double count = qa_command_buffer_execute_scripts_async(b, after_dispatch, after_ctx,
                                                         NULL, NULL);
  complete_batch(b);
  restore_batch(b, &saved);
  return count;
}
double qa_command_buffer_execute_command_async(qa_command_buffer *b, const char *text,
                                               qa_command_context source,
                                               qa_after_dispatch_fn after_dispatch,
                                               void *after_ctx, bool macro_expansion,
                                               qa_abort_signal_fn aborted, void *signal_ctx) {
  if ((double)strlen(text) >= b->maximum_command)
    qa_range_error("Command line exceeds the engine line limit.");
  batch_saved saved;
  begin_batch(b, text, source, aborted, signal_ctx, false, &saved);
  qa_command_context context = input_context(b, &source, true);
  double count = dispatch(b, text, context, false, input_text_mode(b, context, false),
                          execution_dialect(b), macro_expansion);
  context_origin_free(&context);
  if (after_dispatch != NULL) after_dispatch(after_ctx);
  count += qa_command_buffer_execute_scripts_async(b, after_dispatch, after_ctx, NULL, NULL);
  complete_batch(b);
  restore_batch(b, &saved);
  return count;
}

/* --- Builtins --- */
static void set_command(qa_command_buffer *b, qa_command_invocation *command, int flags) {
  const char *first = command->argc > 1 ? command->argv[1] : "";
  qa_cvar_registry *cvars = cvar_owner(b, first, command->source);
  if (cvars == NULL) return;
  if (command->argc < 3 ||
      (qa_command_dialect_is_q2(execution_dialect(b)) && command->argc > 4) ||
      (flags != 0 && command->argc != 3)) {
    buffer_print(b, "set <variable> <value>\n", NULL);
    return;
  }
  const char *name = command->argv[1];
  const char *value = command->argv[2];
  if (qa_command_dialect_is_q2(execution_dialect(b)) && command->argc == 4) {
    const char *requested = command->argv[3];
    if (strcmp(requested, "u") != 0 && strcmp(requested, "s") != 0) {
      buffer_print(b, "flags can only be 'u' or 's'\n", NULL);
      return;
    }
    qa_cvar_snapshot snapshot;
    memset(&snapshot, 0, sizeof snapshot);
    if (qa_cvar_registry_full_set(cvars, name, value,
                                  requested[0] == 'u' ? QA_CVAR_FLAG_USER_INFO
                                                      : QA_CVAR_FLAG_SERVER_INFO,
                                  &snapshot))
      qa_cvar_read_free(&snapshot);
    return;
  }
  char *combined = NULL;
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) {
    text_buffer built = {NULL, 0, 0};
    long length = 0;
    for (size_t i = 2; i < command->argc; i++) {
      const char *argument = command->argv[i];
      long source_length = (long)strlen(argument) - 1;
      if (source_length < 0) source_length = 0;
      if (length + source_length >= 1022) break;
      (void)text_add(&built, argument);
      if (i != command->argc - 1) (void)text_add(&built, " ");
      if (built.data != NULL && strlen(built.data) >= 1024) {
        free(built.data);
        qa_range_error("Cvar_Set overflows source combined buffer");
      }
      length += source_length;
    }
    combined = built.data != NULL ? built.data : dup_text("");
  }
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(cvars, name, combined != NULL ? combined : value, false,
                           &snapshot)) {
    qa_cvar_read_free(&snapshot);
    qa_cvar_registry_add_flags(cvars, name, flags);
  }
  free(combined);
}
static void builtin_wait(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  b->program_revision++;
  b->has_wait_dialect = true;
  b->wait_dialect = command->dialect;
  context_origin_free(&b->wait_source);
  b->wait_source = copy_context(command->source, NULL);
  b->has_wait_source = true;
  b->wait_frames = execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && command->argc == 2
      ? qa_native_atoi(command->argv[1]) : 1;
}
static void builtin_echo(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  char *joined = join_args(command->argv, 1, command->argc);
  buffer_print_format(command->buffer, NULL, "%s%s\n", joined != NULL ? joined : "",
                      command->argc > 1 ? " " : "");
  free(joined);
}
static void builtin_cmd(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer_forward_to_server(command->buffer, command);
}
static void builtin_exec(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_invocation_execute_script(command);
}
static void builtin_alias(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  if (command->argc < 2) {
    buffer_print(b, "Current alias commands:\n", NULL);
    const void *instance = frame_instance(b);
    for (size_t i = 0; i < b->alias_count; i++)
      if (b->aliases[i].instance == instance)
        buffer_print_format(b, NULL, "%s : %s\n", b->aliases[i].name, b->aliases[i].value);
    return;
  }
  char *joined = join_args(command->argv, 2, command->argc);
  text_buffer text = {NULL, 0, 0};
  (void)text_add(&text, joined != NULL ? joined : "");
  free(joined);
  if (qa_command_dialect_is_q1(execution_dialect(b)) && command->argc > 2)
    (void)text_add(&text, " ");
  (void)text_add(&text, "\n");
  if (text.data != NULL && strlen(text.data) >= 1024) {
    free(text.data);
    qa_range_error("Alias body overflows source cmd[1024]");
  }
  (void)qa_command_buffer_define_alias(b, command->argv[1],
                                       text.data != NULL ? text.data : "");
  free(text.data);
}
static void builtin_stuffcmds(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q1_NETQUAKE && command->argc != 1) {
    buffer_print(b, "stuffcmds : execute command line parameters\n", NULL);
    return;
  }
  if (b->startup_command_text != NULL) {
    qa_command_invocation_insert(command, b->startup_command_text);
    return;
  }
  text_buffer joined = {NULL, 0, 0};
  for (size_t i = 1; i < b->options.command_line_count; i++) {
    if (i > 1) (void)text_add(&joined, " ");
    (void)text_add(&joined, b->options.command_line[i]);
  }
  const char *text = joined.data != NULL ? joined.data : "";
  size_t length = strlen(text);
  text_buffer script = {NULL, 0, 0};
  for (size_t offset = 0; offset < length; offset++) {
    if (text[offset] != '+') continue;
    size_t start = ++offset;
    while (offset < length && text[offset] != '+' && text[offset] != '-') offset++;
    char *slice = dup_range(text + start, offset - start);
    (void)text_add(&script, slice != NULL ? slice : "");
    free(slice);
    (void)text_add(&script, "\n");
    offset--;
  }
  if (script.data != NULL && script.data[0] != '\0')
    qa_command_invocation_insert(command, script.data);
  free(script.data);
  free(joined.data);
}
static void builtin_set(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  set_command(command->buffer, command, 0);
}
static void builtin_cmdlist(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  const char *pattern = execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && command->argc > 1
      ? command->argv[1] : NULL;
  size_t count = 0;
  char **names = qa_command_buffer_registered_names(b, &count);
  size_t shown = 0;
  for (size_t i = 0; i < count; i++) {
    if (pattern == NULL || qa_source_filter(pattern, names[i], false)) {
      buffer_print_format(b, NULL, "%s\n", names[i]);
      shown++;
    }
    free(names[i]);
  }
  free(names);
  buffer_print_format(b, NULL, "%zu commands\n", shown);
}
static void builtin_cvarlist(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  size_t count = 0;
  qa_cvar_snapshot *variables =
      qa_command_buffer_cvar_snapshots(b, &command->source, &count);
  const char *pattern = execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && command->argc > 1
      ? command->argv[1] : NULL;
  for (size_t i = 0; i < count; i++) {
    if (pattern != NULL && !qa_source_filter(pattern, variables[i].name, false)) continue;
    int32_t flags = variables[i].flags;
    char markers[8];
    if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3) {
      markers[0] = (flags & 4) != 0 ? 'S' : ' ';
      markers[1] = (flags & 2) != 0 ? 'U' : ' ';
      markers[2] = (flags & 64) != 0 ? 'R' : ' ';
      markers[3] = (flags & 16) != 0 ? 'I' : ' ';
      markers[4] = (flags & 1) != 0 ? 'A' : ' ';
      markers[5] = (flags & 32) != 0 ? 'L' : ' ';
      markers[6] = (flags & 512) != 0 ? 'C' : ' ';
      markers[7] = '\0';
    } else {
      markers[0] = (flags & 1) != 0 ? '*' : ' ';
      markers[1] = (flags & 2) != 0 ? 'U' : ' ';
      markers[2] = (flags & 4) != 0 ? 'S' : ' ';
      markers[3] = (flags & 8) != 0 ? '-' : ((flags & 16) != 0 ? 'L' : ' ');
      markers[4] = '\0';
    }
    buffer_print_format(b, NULL, "%s %s \"%s\"\n", markers, variables[i].name,
                        variables[i].value);
  }
  qa_command_context context = input_context(b, &command->source, true);
  visible_list visible = visible_cvars(b, context);
  double indexes = 0;
  for (size_t i = 0; i < visible.count; i++)
    indexes += qa_cvar_registry_index_count(visible.items[i]);
  free(visible.items);
  context_origin_free(&context);
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3)
    buffer_print_format(b, NULL, "\n%zu total cvars\n%.0f cvar indexes\n", count, indexes);
  else
    buffer_print_format(b, NULL, "%zu cvars\n", count);
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&variables[i]);
  free(variables);
}
static bool numeric_value_text(const char *value) {
  const char *p = value;
  if (*p == '-') p++;
  bool digits_before = false;
  while (*p >= '0' && *p <= '9') { p++; digits_before = true; }
  if (digits_before) {
    if (*p == '.') {
      p++;
      while (*p >= '0' && *p <= '9') p++;
    }
    return *p == '\0';
  }
  if (*p != '.') return false;
  p++;
  while (*p >= '0' && *p <= '9') p++;
  return *p == '\0';
}
static void builtin_inc_dec(void *ctx, qa_command_invocation *command) {
  const char *name = (const char *)ctx;
  qa_command_buffer *b = command->buffer;
  if (command->argc < 2) {
    buffer_print_format(b, NULL, "Usage: %s <variable> [value]\n", name);
    return;
  }
  const char *variable_name = command->argv[1];
  qa_cvar_registry *cvars = cvar_owner(b, variable_name, command->source);
  qa_cvar_read variable;
  memset(&variable, 0, sizeof variable);
  if (cvars == NULL || !qa_cvar_registry_find(cvars, variable_name, &variable)) {
    buffer_print_format(b, NULL, "%s is not a variable\n", variable_name);
    return;
  }
  if (!numeric_value_text(variable.value)) {
    buffer_print_format(b, NULL, "\"%s\" is \"%s\", can't %s\n", variable.name,
                        variable.value, name);
    qa_cvar_read_free(&variable);
    return;
  }
  float amount = (float)(command->argc > 2 ? qa_native_atof(command->argv[2]) : 1);
  float value = (float)(variable.numeric_value +
                        (strcmp(name, "dec") == 0 ? -(double)amount : (double)amount));
  char *text = NULL;
  if ((double)value == variable.numeric_value) {
    text = dup_text(variable.value);
  } else if (!isfinite(value)) {
    text = dup_text(isnan(value) ? "nan" : (value < 0 ? "-inf" : "inf"));
  } else if ((float)((double)value - floor((double)value)) < (float)1e-6) {
    char fixed[64];
    (void)snprintf(fixed, sizeof fixed, "%.0f", (double)value);
    text = dup_text(fixed);
  } else {
    text = qa_cvar_value_text(value, false);
  }
  if (text != NULL && strlen(text) > 31) text[31] = '\0';
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set_console(cvars, variable.name, text != NULL ? text : "",
                                   &snapshot))
    qa_cvar_read_free(&snapshot);
  free(text);
  qa_cvar_read_free(&variable);
}
static void builtin_resetall(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  size_t count = 0;
  qa_cvar_snapshot *variables =
      qa_command_buffer_cvar_snapshots(b, &command->source, &count);
  for (size_t i = 0; i < count; i++) {
    qa_cvar_registry *cvars = cvar_owner(b, variables[i].name, command->source);
    if (cvars != NULL) {
      char *canonical = qa_cvar_registry_canonical_name(cvars, variables[i].name);
      bool alias = canonical == NULL || strcmp(canonical, variables[i].name) != 0;
      free(canonical);
      if (alias) continue;
      qa_cvar_registry_reset_console(cvars, variables[i].name, true);
    }
  }
  for (size_t i = 0; i < count; i++) qa_cvar_read_free(&variables[i]);
  free(variables);
}
static void builtin_vstr(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  if (command->argc != 2) {
    buffer_print(b, "vstr <variablename> : execute a variable command\n", NULL);
    return;
  }
  qa_cvar_read read;
  memset(&read, 0, sizeof read);
  bool found = qa_command_buffer_find_cvar(b, command->argv[1], &command->source, &read);
  text_buffer text = {NULL, 0, 0};
  (void)text_add(&text, found ? read.value : "");
  (void)text_add(&text, "\n");
  if (found) qa_cvar_read_free(&read);
  qa_command_invocation_insert(command, text.data != NULL ? text.data : "");
  free(text.data);
}
typedef struct set_flag_spec {
  const char *name;
  int flag;
  qa_cvar_command_flag_kind kind;
} set_flag_spec;
static void builtin_set_flags(void *ctx, qa_command_invocation *command) {
  const set_flag_spec *spec = (const set_flag_spec *)ctx;
  qa_command_buffer *b = command->buffer;
  if (command->argc < 3 ||
      (execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && command->argc != 3)) {
    buffer_print_format(b, NULL, "Usage: %s <variable> <value>\n", spec->name);
    return;
  }
  const char *variable = command->argv[1];
  qa_cvar_registry *cvars = cvar_owner(b, variable, command->source);
  if (cvars != NULL && qa_cvar_registry_dialect(cvars) == QA_COMMAND_DIALECT_Q3) {
    set_command(b, command, spec->flag);
    return;
  }
  if (cvars != NULL) {
    char *joined = join_args(command->argv, 2, command->argc);
    qa_cvar_registry_set_command_flags(cvars, variable, joined != NULL ? joined : "",
                                       spec->kind);
    free(joined);
  }
}
static void builtin_toggle(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  if (command->argc < 2) {
    buffer_print(b, "Usage: toggle <variable> [values]\n", NULL);
    return;
  }
  const char *name = command->argv[1];
  qa_cvar_registry *cvars = cvar_owner(b, name, command->source);
  if (execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && command->argc == 2) {
    if (cvars != NULL) {
      qa_cvar_snapshot snapshot;
      memset(&snapshot, 0, sizeof snapshot);
      const char *value = trunc(qa_cvar_registry_variable_value(cvars, name)) == 0 ? "1" : "0";
      if (qa_cvar_registry_set(cvars, name, value, false, &snapshot))
        qa_cvar_read_free(&snapshot);
    }
    return;
  }
  qa_cvar_read variable;
  memset(&variable, 0, sizeof variable);
  if (cvars == NULL || !qa_cvar_registry_find(cvars, name, &variable)) {
    buffer_print_format(b, NULL, "%s is not a variable\n", name);
    return;
  }
  if (command->argc == 2) {
    if (strcmp(variable.value, "0") == 0 || strcmp(variable.value, "1") == 0) {
      qa_cvar_snapshot snapshot;
      memset(&snapshot, 0, sizeof snapshot);
      if (qa_cvar_registry_set_console(cvars, name,
                                       strcmp(variable.value, "0") == 0 ? "1" : "0",
                                       &snapshot))
        qa_cvar_read_free(&snapshot);
    } else {
      buffer_print_format(b, NULL, "\"%s\" is \"%s\", can't toggle\n", name, variable.value);
    }
  } else {
    long index = -1;
    for (size_t i = 2; i < command->argc; i++) {
      if (folded_equal(command->argv[i], variable.value)) {
        index = (long)i - 2;
        break;
      }
    }
    if (index < 0) {
      buffer_print_format(b, NULL, "\"%s\" is \"%s\", can't cycle\n", name, variable.value);
    } else {
      size_t value_count = command->argc - 2;
      const char *next = command->argv[2 + ((size_t)index + 1) % value_count];
      qa_cvar_snapshot snapshot;
      memset(&snapshot, 0, sizeof snapshot);
      if (qa_cvar_registry_set_console(cvars, name, next, &snapshot))
        qa_cvar_read_free(&snapshot);
    }
  }
  qa_cvar_read_free(&variable);
}
static void builtin_reset(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  if (command->argc < 2 ||
      (execution_dialect(b) == QA_COMMAND_DIALECT_Q3 && command->argc != 2)) {
    buffer_print(b, "reset <variable> : reset a cvar\n", NULL);
    return;
  }
  qa_cvar_registry *cvars = cvar_owner(b, command->argv[1], command->source);
  if (cvars != NULL) qa_cvar_registry_reset_console(cvars, command->argv[1], false);
}
static void builtin_cvar_restart(void *ctx, qa_command_invocation *command) {
  (void)ctx;
  qa_command_buffer *b = command->buffer;
  qa_command_context context = input_context(b, &command->source, true);
  visible_list visible = visible_cvars(b, context);
  for (size_t i = 0; i < visible.count; i++)
    if (qa_cvar_registry_dialect(visible.items[i]) == QA_COMMAND_DIALECT_Q3)
      qa_cvar_registry_reset_all(visible.items[i]);
  free(visible.items);
  context_origin_free(&context);
}
static const char *const echo_examples[] = {"echo hello"};
static const char *const set_examples[] = {"set name \"Player\""};
static const char *const cmdlist_examples[] = {"cmdlist"};
static const char *const cvarlist_examples[] = {"cvarlist"};
static const char *const vstr_examples[] = {"vstr nextmap"};
static const qa_command_documentation echo_doc = {
    "Print text to the console.", "echo <text>", echo_examples, 1, NULL, 0};
static const qa_command_documentation set_doc = {
    "Set a console variable.", "set <variable> <value>", set_examples, 1, NULL, 0};
static const qa_command_documentation cmdlist_doc_q3 = {
    "List registered console commands.", "cmdlist [pattern]", cmdlist_examples, 1, NULL, 0};
static const qa_command_documentation cmdlist_doc = {
    "List registered console commands.", "cmdlist", cmdlist_examples, 1, NULL, 0};
static const qa_command_documentation cvarlist_doc_q3 = {
    "List visible console variables and their current values.", "cvarlist [pattern]",
    cvarlist_examples, 1, NULL, 0};
static const qa_command_documentation cvarlist_doc = {
    "List visible console variables and their current values.", "cvarlist",
    cvarlist_examples, 1, NULL, 0};
static const qa_command_documentation vstr_doc = {
    "Execute the invoking owner's variable as commands in this command buffer.",
    "vstr <variablename>", vstr_examples, 1, NULL, 0};
static const set_flag_spec seta_spec = {"seta", QA_CVAR_FLAG_ARCHIVE,
                                        QA_CVAR_COMMAND_FLAG_ARCHIVE};
static const set_flag_spec setu_spec = {"setu", QA_CVAR_FLAG_USER_INFO,
                                        QA_CVAR_COMMAND_FLAG_USERINFO};
static const set_flag_spec sets_spec = {"sets", QA_CVAR_FLAG_SERVER_INFO,
                                        QA_CVAR_COMMAND_FLAG_SERVERINFO};
typedef struct builtin_spec {
  const char *name;
  qa_command_handler_fn handler;
  void *ctx;
  const qa_command_documentation *documentation;
} builtin_spec;
static void add_builtin_handler(qa_command_buffer *b, const char *name,
                                qa_command_handler_fn handler, void *ctx) {
  if (b->builtin_count == b->builtin_capacity) {
    size_t cap = b->builtin_capacity == 0 ? 16 : b->builtin_capacity * 2;
    builtin_entry *items = (builtin_entry *)realloc(b->builtin_handlers, cap * sizeof *items);
    if (items == NULL) return;
    b->builtin_handlers = items;
    b->builtin_capacity = cap;
  }
  b->builtin_handlers[b->builtin_count].name = dup_text(name);
  b->builtin_handlers[b->builtin_count].handler = handler;
  b->builtin_handlers[b->builtin_count].handler_ctx = ctx;
  b->builtin_count++;
}
static void register_builtins(qa_command_buffer *b, qa_command_dialect dialect) {
  b->has_builtin_dialect = true;
  b->builtin_dialect = dialect;
  const builtin_spec specs[] = {
      {"wait", builtin_wait, NULL, NULL},
      {"echo", builtin_echo, NULL, &echo_doc},
      {"cmd", builtin_cmd, NULL, NULL},
      {"exec", builtin_exec, NULL, NULL},
      {"alias", dialect == QA_COMMAND_DIALECT_Q3 ? NULL : builtin_alias, NULL, NULL},
      {"stuffcmds", qa_command_dialect_is_q1(dialect) ? builtin_stuffcmds : NULL, NULL, NULL},
      {"set", builtin_set, NULL, &set_doc},
      {"cmdlist", builtin_cmdlist, NULL,
       dialect == QA_COMMAND_DIALECT_Q3 ? &cmdlist_doc_q3 : &cmdlist_doc},
      {"cvarlist", builtin_cvarlist, NULL,
       dialect == QA_COMMAND_DIALECT_Q3 ? &cvarlist_doc_q3 : &cvarlist_doc},
      {"inc", builtin_inc_dec, (void *)"inc", NULL},
      {"dec", builtin_inc_dec, (void *)"dec", NULL},
      {"resetall", builtin_resetall, NULL, NULL},
      {"vstr", builtin_vstr, NULL, &vstr_doc},
      {"seta", builtin_set_flags, (void *)&seta_spec, NULL},
      {"setu", builtin_set_flags, (void *)&setu_spec, NULL},
      {"sets", builtin_set_flags, (void *)&sets_spec, NULL},
      {"toggle", builtin_toggle, NULL, NULL},
      {"reset", builtin_reset, NULL, NULL},
      {"cvar_restart", builtin_cvar_restart, NULL, NULL},
  };
  static const char *const order_nq[] = {"stuffcmds", "exec", "echo", "alias", "cmd", "wait"};
  static const char *const order_qw[] = {"stuffcmds", "exec", "echo", "alias", "wait", "cmd"};
  static const char *const order_q2[] = {"cmdlist", "exec", "echo", "alias",
                                         "wait", "set", "cvarlist", "cmd"};
  static const char *const order_q3[] = {"toggle", "set", "sets", "setu", "seta", "reset",
                                         "cvarlist", "cvar_restart", "cmdlist", "exec",
                                         "vstr", "echo", "wait", "cmd"};
  static const char *const extras[] = {"set", "cmdlist", "cvarlist", "vstr", "inc", "dec",
                                       "resetall", "seta", "setu", "sets", "reset", "toggle"};
  const char *const *order;
  size_t order_count;
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    order = order_nq;
    order_count = sizeof order_nq / sizeof *order_nq;
  } else if (dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    order = order_qw;
    order_count = sizeof order_qw / sizeof *order_qw;
  } else if (qa_command_dialect_is_q2(dialect)) {
    order = order_q2;
    order_count = sizeof order_q2 / sizeof *order_q2;
  } else {
    order = order_q3;
    order_count = sizeof order_q3 / sizeof *order_q3;
  }
  size_t spec_count = sizeof specs / sizeof *specs;
  for (int pass = 0; pass < 2; pass++) {
    const char *const *names = pass == 0 ? order : extras;
    size_t name_count = pass == 0 ? order_count : sizeof extras / sizeof *extras;
    for (size_t i = 0; i < name_count; i++) {
      if (pass == 1) {
        bool in_order = false;
        for (size_t j = 0; j < order_count; j++)
          if (strcmp(order[j], names[i]) == 0) in_order = true;
        if (in_order) continue;
      }
      const builtin_spec *spec = NULL;
      for (size_t j = 0; j < spec_count; j++)
        if (strcmp(specs[j].name, names[i]) == 0) spec = &specs[j];
      if (spec == NULL || spec->handler == NULL) continue;
      if (!buffer_exists(b, spec->name) &&
          qa_command_buffer_register(b, spec->name, spec->handler, spec->ctx,
                                     spec->documentation, QA_COMMAND_WORLD))
        add_builtin_handler(b, spec->name, spec->handler, spec->ctx);
    }
  }
}
