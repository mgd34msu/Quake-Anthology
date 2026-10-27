/* Port of src/console/llm.ts. GPL-2.0-or-later. */
#include "qa/console/llm.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/console/llm_batch.h"

static char *dup_text(const char *text) {
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

typedef struct text_builder {
  char *data;
  size_t length;
  size_t capacity;
} text_builder;

static bool builder_add(text_builder *builder, const char *text) {
  size_t length = strlen(text);
  if (builder->length + length + 1 > builder->capacity) {
    size_t capacity = builder->capacity == 0 ? 256 : builder->capacity;
    while (capacity < builder->length + length + 1) capacity *= 2;
    char *data = (char *)realloc(builder->data, capacity);
    if (data == NULL) return false;
    builder->data = data;
    builder->capacity = capacity;
  }
  memcpy(builder->data + builder->length, text, length + 1);
  builder->length += length;
  return true;
}
static char *builder_finish(text_builder *builder) {
  if (builder->data == NULL) return dup_text("");
  return builder->data;
}

static void print_to(qa_llm_commands_registration *reg, const char *text,
                     const qa_command_context *source) {
  reg->print(reg->print_ctx, text, source);
}
static void print_format(qa_llm_commands_registration *reg, const qa_command_context *source,
                         const char *format, ...) {
  text_builder out = {0};
  char scratch[1024];
  va_list args;
  va_start(args, format);
  int written = vsnprintf(scratch, sizeof scratch, format, args);
  va_end(args);
  if (written < 0) return;
  if ((size_t)written < sizeof scratch) {
    print_to(reg, scratch, source);
    return;
  }
  char *big = (char *)malloc((size_t)written + 1);
  if (big == NULL) return;
  va_start(args, format);
  vsnprintf(big, (size_t)written + 1, format, args);
  va_end(args);
  builder_add(&out, big);
  free(big);
  if (out.data != NULL) print_to(reg, out.data, source);
  free(out.data);
}

static qa_llm_abort_signal *pending_get(qa_llm_commands_registration *reg, const char *key) {
  for (size_t i = 0; i < reg->pending_count; i++)
    if (strcmp(reg->pending[i].key, key) == 0) return reg->pending[i].signal;
  return NULL;
}
static bool pending_set(qa_llm_commands_registration *reg, const char *key,
                        qa_llm_abort_signal *signal) {
  if (reg->pending_count == reg->pending_capacity) {
    size_t capacity = reg->pending_capacity == 0 ? 4 : reg->pending_capacity * 2;
    qa_llm_pending_request *grown =
        (qa_llm_pending_request *)realloc(reg->pending, capacity * sizeof *grown);
    if (grown == NULL) return false;
    reg->pending = grown;
    reg->pending_capacity = capacity;
  }
  char *copy = dup_text(key);
  if (copy == NULL) return false;
  reg->pending[reg->pending_count].key = copy;
  reg->pending[reg->pending_count].signal = signal;
  reg->pending_count++;
  return true;
}
static void pending_delete(qa_llm_commands_registration *reg, const char *key) {
  for (size_t i = 0; i < reg->pending_count; i++) {
    if (strcmp(reg->pending[i].key, key) == 0) {
      free(reg->pending[i].key);
      memmove(reg->pending + i, reg->pending + i + 1,
              (reg->pending_count - i - 1) * sizeof *reg->pending);
      reg->pending_count--;
      return;
    }
  }
}

static char *origin_key(const qa_command_origin *origin) {
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    char scratch[64];
    snprintf(scratch, sizeof scratch, "seat:%lld", (long long)origin->seat.index);
    return dup_text(scratch);
  }
  return dup_text("console");
}

static bool origin_allowed(const qa_command_invocation *invocation) {
  return invocation->direct &&
         (invocation->source.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ||
          invocation->source.origin.kind == QA_COMMAND_ORIGIN_LOCAL_CONSOLE);
}

static const char direct_only_message[] =
    "LLM commands require direct input from a local player console. Scripts, modules, "
    "and servers cannot start requests.\n";

static void cancel_handler(void *ctx, qa_command_invocation *invocation) {
  qa_llm_commands_registration *reg = (qa_llm_commands_registration *)ctx;
  reg->refs++;
  qa_command_context source = invocation->source;
  if (!origin_allowed(invocation)) {
    print_to(reg, direct_only_message, &source);
  } else if (invocation->argc > 1) {
    print_to(reg, "Usage: llm_cancel\n", &source);
  } else {
    char *key = origin_key(&source.origin);
    qa_llm_abort_signal *signal = key == NULL ? NULL : pending_get(reg, key);
    if (signal == NULL) {
      print_to(reg, "No LLM request is running in this console.\n", &source);
    } else {
      qa_llm_signal_abort(signal);
      pending_delete(reg, key);
      print_to(reg, "LLM request cancelled.\n", &source);
    }
    free(key);
  }
  if (--reg->refs == 0) free(reg);
}

typedef struct ask_stream {
  qa_llm_commands_registration *reg;
  const char *key;
  qa_llm_abort_signal *signal;
  qa_command_context source;
  bool streamed;
} ask_stream;

static bool request_current(qa_llm_commands_registration *reg, const char *key,
                            qa_llm_abort_signal *signal) {
  return !reg->disposed && !signal->aborted && pending_get(reg, key) == signal;
}

static void ask_on_text(void *ctx, const char *chunk) {
  ask_stream *stream = (ask_stream *)ctx;
  if (request_current(stream->reg, stream->key, stream->signal) && chunk[0] != '\0') {
    stream->streamed = true;
    print_to(stream->reg, chunk, &stream->source);
  }
}

static bool signal_aborted(void *ctx) {
  return qa_llm_signal_is_aborted((qa_llm_abort_signal *)ctx);
}

typedef struct llm_definition {
  const char *name;
  const char *usage;
  const char *summary;
  const char *example;
  bool is_exec;
} llm_definition;

static void run_request(qa_llm_commands_registration *reg, const llm_definition *definition,
                        qa_command_invocation *invocation) {
  qa_command_context source = invocation->source;
  if (!origin_allowed(invocation)) {
    print_to(reg, direct_only_message, &source);
    return;
  }
  text_builder joined = {0};
  for (size_t i = 1; i < invocation->argc; i++) {
    if (i > 1) builder_add(&joined, " ");
    builder_add(&joined, invocation->argv[i]);
  }
  char *prompt = builder_finish(&joined);
  if (prompt == NULL) return;
  size_t begin = 0, end = strlen(prompt);
  while (prompt[begin] == ' ' || (prompt[begin] >= '\t' && prompt[begin] <= '\r')) begin++;
  while (end > begin && (prompt[end - 1] == ' ' || (prompt[end - 1] >= '\t' && prompt[end - 1] <= '\r')))
    end--;
  memmove(prompt, prompt + begin, end - begin);
  prompt[end - begin] = '\0';
  if (prompt[0] == '\0') {
    print_format(reg, &source, "Usage: %s\nQuote text containing semicolons.\n",
                 definition->usage);
    free(prompt);
    return;
  }
  if (!reg->has_requester) {
    print_to(reg, "Configure a provider and model in Options > LLM before sending a "
                  "request.\n", &source);
    free(prompt);
    return;
  }
  if (strlen(prompt) > 4096) {
    print_to(reg, "LLM prompt exceeds 4096 characters. Ask a shorter question.\n", &source);
    free(prompt);
    return;
  }
  char *key = origin_key(&source.origin);
  if (key == NULL) {
    free(prompt);
    return;
  }
  if (pending_get(reg, key) != NULL) {
    print_to(reg, "An LLM request is already running in this console.\n", &source);
    free(key);
    free(prompt);
    return;
  }
  qa_llm_abort_signal *signal = (qa_llm_abort_signal *)malloc(sizeof *signal);
  if (signal == NULL || !pending_set(reg, key, signal)) {
    free(signal);
    free(key);
    free(prompt);
    return;
  }
  qa_llm_signal_init(signal);
  qa_llm_signal_set_timeout(signal, 120000);
  char *error = NULL;
  char *answer = NULL;
  ask_stream stream;
  memset(&stream, 0, sizeof stream);
  char *session_instructions = qa_llm_session_instructions(reg->commands, &source, &error);
  char *instructions = NULL;
  if (session_instructions != NULL) {
    if (definition->is_exec) {
      instructions = qa_llm_exec_turn_instructions(reg->commands, &source, prompt);
    } else {
      char *docs = qa_llm_relevant_console_docs(reg->commands, &source, prompt,
                                                QA_LLM_CATALOG_ASK);
      text_builder ask = {0};
      builder_add(&ask,
                  "This is an llm_ask request. Answer the user's question concisely, "
                  "including general questions unrelated to the game. For advice about "
                  "this running engine, use only command and setting names from the "
                  "session catalog and the relevant documentation below. If a requested "
                  "setting or command is absent, say it is not registered. You only "
                  "answer questions; do not claim to have executed commands.\n");
      builder_add(&ask, docs == NULL ? "" : docs);
      free(docs);
      instructions = builder_finish(&ask);
    }
  }
  if (instructions != NULL) {
    print_to(reg, "LLM request started. Use llm_cancel to cancel.\n", &source);
    qa_llm_request_input input;
    memset(&input, 0, sizeof input);
    input.prompt = prompt;
    input.instructions = instructions;
    input.session_instructions = session_instructions;
    input.signal = signal;
    if (!definition->is_exec) {
      stream.reg = reg;
      stream.key = key;
      stream.signal = signal;
      stream.source = source;
      input.on_text = ask_on_text;
      input.on_text_ctx = &stream;
    }
    answer = reg->requester.request(reg->requester.ctx, &input, &error);
  }
  /* The TS 120 s timer aborts and reports before any late result is
   * observed; the deadline latch reproduces it on return. */
  (void)qa_llm_signal_is_aborted(signal); /* latches timed_out + aborted */
  if (signal->timed_out) {
    if (!reg->disposed && pending_get(reg, key) == signal) {
      pending_delete(reg, key);
      print_to(reg, "LLM request timed out. Try again.\n", &source);
    }
  } else if (error != NULL || (instructions != NULL && answer == NULL)) {
    if (request_current(reg, key, signal))
      print_format(reg, &source, "LLM request failed: %s\n",
                   error == NULL ? "Unknown error" : error);
  } else if (answer != NULL) {
    if (request_current(reg, key, signal)) {
      if (!definition->is_exec) {
        if (stream.streamed)
          print_to(reg, "\n", &source);
        else
          print_format(reg, &source, "%s\n", answer);
      } else {
        char **batch = NULL;
        size_t batch_count = 0;
        char *batch_error = NULL;
        if (!qa_validate_llm_batch(answer, reg->commands, &source, &batch, &batch_count,
                                   &batch_error)) {
          if (request_current(reg, key, signal))
            print_format(reg, &source, "LLM request failed: %s\n",
                         batch_error == NULL ? "Unknown error" : batch_error);
          free(batch_error);
        } else if (request_current(reg, key, signal)) {
          text_builder printed = {0}, submitted = {0};
          for (size_t i = 0; i < batch_count; i++) {
            if (i > 0) builder_add(&printed, "; ");
            builder_add(&printed, batch[i]);
            builder_add(&submitted, batch[i]);
            builder_add(&submitted, "\n");
          }
          if (printed.data != NULL) print_format(reg, &source, "%s\n", printed.data);
          if (submitted.data != NULL)
            qa_command_buffer_execute_batch(reg->commands, submitted.data, source,
                                            signal_aborted, signal);
          free(printed.data);
          free(submitted.data);
        }
        qa_llm_batch_lines_free(batch, batch_count);
      }
    }
  }
  free(answer);
  free(error);
  free(instructions);
  free(session_instructions);
  if (pending_get(reg, key) == signal) pending_delete(reg, key);
  free(signal);
  free(key);
  free(prompt);
}

static const llm_definition ask_definition = {
    "llm_ask", "llm_ask <question>",
    "Ask the configured LLM and print its answer in this console.",
    "llm_ask \"How do I change brightness?\"", false};
static const llm_definition exec_definition = {
    "llm_exec", "llm_exec <request>",
    "Ask the configured LLM for console commands, validate the whole batch, then print "
    "and execute it in your context.",
    "llm_exec \"Set brightness to the default\"", true};

static void ask_handler(void *ctx, qa_command_invocation *invocation) {
  qa_llm_commands_registration *reg = (qa_llm_commands_registration *)ctx;
  reg->refs++;
  run_request(reg, &ask_definition, invocation);
  if (--reg->refs == 0) free(reg);
}
static void exec_handler(void *ctx, qa_command_invocation *invocation) {
  qa_llm_commands_registration *reg = (qa_llm_commands_registration *)ctx;
  reg->refs++;
  run_request(reg, &exec_definition, invocation);
  if (--reg->refs == 0) free(reg);
}

void qa_prime_llm_console_session(qa_command_buffer *commands,
                                  const qa_llm_command_requester *requester,
                                  const qa_command_context *source) {
  if (requester == NULL || requester->prime == NULL) return;
  qa_command_context fallback;
  const qa_command_context *context = source;
  if (context == NULL) {
    fallback = qa_command_buffer_context(commands);
    context = &fallback;
  }
  char *error = NULL;
  char *instructions = qa_llm_session_instructions(commands, context, &error);
  free(error);
  if (instructions == NULL) return;
  requester->prime(requester->ctx, instructions);
  free(instructions);
}

qa_llm_commands_registration *qa_register_llm_commands(
    qa_command_buffer *commands, qa_command_print_source_fn print, void *print_ctx,
    const qa_llm_command_requester *requester) {
  qa_llm_commands_registration *reg =
      (qa_llm_commands_registration *)calloc(1, sizeof *reg);
  if (reg == NULL) return NULL;
  reg->commands = commands;
  reg->print = print;
  reg->print_ctx = print_ctx;
  reg->has_requester = requester != NULL;
  if (requester != NULL) reg->requester = *requester;
  reg->refs = 1;
  static const char *const cancel_examples[] = {"llm_cancel"};
  static const qa_command_documentation cancel_doc = {
      "Cancel this console's pending LLM request.", "llm_cancel", cancel_examples, 1,
      NULL, 0};
  if (qa_command_buffer_register_engine(commands, "llm_cancel", cancel_handler, reg,
                                        &cancel_doc))
    reg->names[reg->name_count++] = "llm_cancel";
  const llm_definition *definitions[] = {&ask_definition, &exec_definition};
  qa_command_handler_fn handlers[] = {ask_handler, exec_handler};
  static const char *const examples[2][1] = {{"llm_ask \"How do I change brightness?\""},
                                             {"llm_exec \"Set brightness to the default\""}};
  for (size_t i = 0; i < 2; i++) {
    qa_command_documentation doc;
    memset(&doc, 0, sizeof doc);
    doc.summary = definitions[i]->summary;
    doc.usage = definitions[i]->usage;
    doc.examples = examples[i];
    doc.example_count = 1;
    if (qa_command_buffer_register_engine(commands, definitions[i]->name, handlers[i], reg,
                                          &doc))
      reg->names[reg->name_count++] = definitions[i]->name;
  }
  if (requester != NULL && reg->name_count != 0)
    qa_prime_llm_console_session(commands, requester, NULL);
  return reg;
}

void qa_llm_commands_registration_remove(qa_llm_commands_registration *reg) {
  if (reg == NULL) return;
  reg->disposed = true;
  for (size_t i = 0; i < reg->pending_count; i++) {
    qa_llm_signal_abort(reg->pending[i].signal);
    free(reg->pending[i].key);
  }
  free(reg->pending);
  reg->pending = NULL;
  reg->pending_count = 0;
  reg->pending_capacity = 0;
  for (size_t i = 0; i < reg->name_count; i++)
    qa_command_buffer_unregister(reg->commands, reg->names[i], NULL, NULL, false);
  if (--reg->refs == 0) free(reg);
}
