/* Port of src/world/session/mod-commands.ts. GPL-2.0-or-later. */
#include "qa/world/session/mod_commands.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/commands/text.h"
#include "qa/foundation/event_ring.h"

static noreturn void qa_errorf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fprintf(stderr, "Error: ");
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
  abort();
}
static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}
static bool str_ieq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  while (*a != '\0' && *b != '\0') {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
      return false;
    a++;
    b++;
  }
  return *a == *b;
}
static bool same_session(qa_session_id a, qa_session_id b) {
  return a.token == b.token;
}
static void *xrealloc(void *ptr, size_t size) {
  void *next = realloc(ptr, size != 0 ? size : 1);
  if (next == NULL) abort();
  return next;
}
static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) abort();
  memcpy(copy, text, length);
  return copy;
}

struct qa_mod_command_port {
  qa_mod_commands *commands;
  qa_mod_command_binding binding;
  qa_command_producer producer;
  void *instance; /* owned unique cell (TS symbol) */
  char *key;      /* owned selection key */
  qa_resource_scope *resources;
  qa_command_buffer **buffers;
  size_t buffer_count;
  size_t buffer_capacity;
  bool closed;
};

struct pending_command {
  int32_t kind; /* 0 append, 1 insert */
  char *text;   /* owned */
  qa_command_context source;
  int32_t dialect;
};

struct qa_mod_commands {
  qa_command_context context;
  qa_mod_commands_buffer_fn commands_fn;
  void *commands_ctx;
  qa_mod_command_port **entries;
  size_t entry_count;
  size_t entry_capacity;
  qa_event_ring pending;
  qa_event_cursor pending_cursor;
};

static qa_command_buffer *current_buffer(qa_mod_commands *commands) {
  return commands->commands_fn(commands->commands_ctx);
}

static void submit(qa_mod_commands *commands, int32_t kind,
                   const char *text, qa_command_context source,
                   qa_command_dialect dialect) {
  if (!same_session(source.session, commands->context.session))
    qa_errorf("Staged command belongs to another session");
  qa_command_buffer *buffer = current_buffer(commands);
  if (buffer == NULL) {
    struct pending_command pending;
    memset(&pending, 0, sizeof pending);
    pending.kind = kind;
    pending.text = xstrdup(text);
    pending.source = source;
    pending.dialect = (int32_t)dialect;
    qa_event_ring_admit(&commands->pending, &pending);
    return;
  }
  qa_mod_commands_flush(commands);
  if (kind == 0)
    qa_command_buffer_append(buffer, text, &source, (int)dialect);
  else
    qa_command_buffer_insert(buffer, text, &source, (int)dialect);
}

void qa_mod_commands_append(qa_mod_commands *commands, const char *text,
                            qa_command_context source,
                            qa_command_dialect dialect) {
  submit(commands, 0, text, source, dialect);
}
void qa_mod_commands_insert(qa_mod_commands *commands, const char *text,
                            qa_command_context source,
                            qa_command_dialect dialect) {
  submit(commands, 1, text, source, dialect);
}

void qa_mod_commands_flush(qa_mod_commands *commands) {
  if (qa_event_ring_pending(&commands->pending,
                            &commands->pending_cursor) == 0)
    return;
  qa_command_buffer *buffer = current_buffer(commands);
  if (buffer == NULL)
    qa_errorf("Source commands require the candidate's prepared command buffer");
  size_t count = 0;
  struct pending_command *pending =
      qa_event_ring_take(&commands->pending, &commands->pending_cursor,
                         &count);
  qa_event_ring_retire(&commands->pending);
  for (size_t i = 0; i < count; i++) {
    if (pending[i].kind == 0)
      qa_command_buffer_append(buffer, pending[i].text,
                               &pending[i].source, pending[i].dialect);
    else
      qa_command_buffer_insert(buffer, pending[i].text,
                               &pending[i].source, pending[i].dialect);
    free(pending[i].text);
  }
  free(pending);
}

qa_mod_commands *qa_mod_commands_create(
    qa_command_context context, qa_mod_commands_buffer_fn commands,
    void *commands_ctx) {
  qa_mod_commands *registry = calloc(1, sizeof *registry);
  if (registry == NULL) return NULL;
  registry->context = context;
  registry->commands_fn = commands;
  registry->commands_ctx = commands_ctx;
  if (!qa_event_ring_init(&registry->pending, 4096,
                          sizeof(struct pending_command)) ||
      !qa_event_ring_open_cursor(&registry->pending,
                                 &registry->pending_cursor, false)) {
    free(registry);
    return NULL;
  }
  return registry;
}

static void free_pending_text(const void *record, int64_t id, void *ctx) {
  (void)id;
  (void)ctx;
  const struct pending_command *pending = record;
  free(pending->text);
}
void qa_mod_commands_destroy(qa_mod_commands *commands) {
  if (commands == NULL) return;
  qa_event_ring_for_each_live(&commands->pending, free_pending_text, NULL);
  qa_event_ring_destroy(&commands->pending);
  free(commands->entries);
  free(commands);
}

bool qa_read_mod_command(const char *raw, qa_mod_selection *out_selection,
                         char **out_text) {
  qa_command_tokens tokens;
  memset(&tokens, 0, sizeof tokens);
  if (!qa_tokenize_command(raw, QA_COMMAND_DIALECT_Q3,
                           QA_COMMAND_TEXT_SOURCE, &tokens))
    return false;
  const char *selected = tokens.argc > 1 ? tokens.argv[1] : NULL;
  char *text = qa_command_text_tail(raw, QA_COMMAND_DIALECT_Q3, 2);
  if (selected == NULL || text == NULL || text[0] == '\0')
    qa_errorf("Usage: modcmd PRODUCT/COMPONENT_ID <command>");
  char *copy = xstrdup(selected);
  qa_command_tokens_destroy(&tokens);
  bool ok = qa_read_mod_selection(copy, out_selection);
  free(copy);
  if (!ok) {
    free(text);
    return false;
  }
  *out_text = text;
  return true;
}

static qa_mod_command_port *find_entry(qa_mod_commands *commands,
                                       const void *instance) {
  for (size_t i = 0; i < commands->entry_count; i++)
    if (commands->entries[i]->instance == instance)
      return commands->entries[i];
  return NULL;
}
static qa_mod_command_port *find_selection(qa_mod_commands *commands,
                                           const char *key) {
  for (size_t i = 0; i < commands->entry_count; i++)
    if (str_eq(commands->entries[i]->key, key))
      return commands->entries[i];
  return NULL;
}

static qa_command_buffer *port_current(qa_mod_command_port *port) {
  qa_resource_scope_assert_open(port->resources);
  if (port->closed) qa_errorf("Mod commands are closed: %s", port->key);
  qa_command_buffer *buffer = current_buffer(port->commands);
  if (buffer == NULL) return NULL;
  qa_command_context context = qa_command_buffer_context(buffer);
  if (!same_session(context.session, port->commands->context.session))
    qa_errorf("Mod command buffer belongs to another session");
  bool seen = false;
  for (size_t i = 0; i < port->buffer_count; i++)
    if (port->buffers[i] == buffer) seen = true;
  if (!seen) {
    if (port->buffer_count == port->buffer_capacity) {
      port->buffer_capacity =
          port->buffer_capacity != 0 ? port->buffer_capacity * 2 : 4;
      port->buffers = xrealloc(port->buffers, port->buffer_capacity *
                                                  sizeof *port->buffers);
    }
    port->buffers[port->buffer_count++] = buffer;
  }
  return buffer;
}

static qa_command_context port_source(qa_mod_command_port *port,
                                      qa_command_buffer *buffer,
                                      const qa_command_context *caller) {
  qa_command_context context;
  if (caller != NULL) {
    context = *caller;
  } else if (buffer != NULL &&
             qa_command_buffer_execution_context(buffer) != NULL) {
    context = *qa_command_buffer_execution_context(buffer);
  } else {
    context = port->commands->context;
  }
  if (!same_session(context.session, port->commands->context.session))
    qa_errorf("Mod command caller belongs to another session");
  qa_command_context source;
  source.session = context.session;
  source.origin = context.origin;
  source.producer = &port->producer;
  return source;
}

qa_command_producer qa_mod_command_port_producer(
    const qa_mod_command_port *port) {
  return port->producer;
}
void qa_mod_command_port_append(qa_mod_command_port *port,
                                const char *text,
                                const qa_command_context *caller) {
  qa_command_buffer *buffer = port_current(port);
  qa_command_context source = port_source(port, buffer, caller);
  qa_mod_commands_append(port->commands, text, source,
                         qa_cvar_registry_dialect(port->binding.cvars));
}
void qa_mod_command_port_insert(qa_mod_command_port *port,
                                const char *text,
                                const qa_command_context *caller) {
  qa_command_buffer *buffer = port_current(port);
  qa_command_context source = port_source(port, buffer, caller);
  qa_mod_commands_insert(port->commands, text, source,
                         qa_cvar_registry_dialect(port->binding.cvars));
}
double qa_mod_command_port_execute_now(
    qa_mod_command_port *port, const char *text,
    const qa_command_context *caller) {
  qa_command_buffer *buffer = port_current(port);
  if (buffer == NULL)
    qa_errorf(
        "Immediate mod commands require the candidate's prepared command "
        "buffer");
  qa_mod_commands_flush(port->commands);
  qa_command_context source = port_source(port, buffer, caller);
  return qa_command_buffer_execute_now(
      buffer, text, &source,
      (int)qa_cvar_registry_dialect(port->binding.cvars));
}

struct filter_ctx {
  const void *instance;
  char **texts;
  size_t count;
  size_t capacity;
};
static void collect_discarded(const void *record, int64_t id, void *ctx) {
  (void)id;
  struct filter_ctx *filter = ctx;
  const struct pending_command *pending = record;
  if (pending->source.producer != NULL &&
      pending->source.producer->instance == filter->instance) {
    if (filter->count == filter->capacity) {
      filter->capacity = filter->capacity != 0 ? filter->capacity * 2 : 8;
      filter->texts =
          xrealloc(filter->texts, filter->capacity * sizeof *filter->texts);
    }
    filter->texts[filter->count++] = pending->text;
  }
}
static bool keep_pending(const void *record, void *ctx) {
  const struct pending_command *pending = record;
  const void *instance = ctx;
  return pending->source.producer == NULL ||
         pending->source.producer->instance != instance;
}

void qa_mod_command_port_close(qa_mod_command_port *port) {
  if (port->closed) return;
  port->closed = true;
  qa_mod_commands *commands = port->commands;
  for (size_t i = 0; i < commands->entry_count; i++)
    if (commands->entries[i] == port) {
      memmove(&commands->entries[i], &commands->entries[i + 1],
              (commands->entry_count - i - 1) * sizeof *commands->entries);
      commands->entry_count--;
      break;
    }
  struct filter_ctx filter = {port->instance, NULL, 0, 0};
  qa_event_ring_for_each_live(&commands->pending, collect_discarded,
                              &filter);
  qa_event_ring_filter_live(&commands->pending, keep_pending,
                            port->instance);
  for (size_t i = 0; i < filter.count; i++) free(filter.texts[i]);
  free(filter.texts);
  qa_command_buffer *current = current_buffer(commands);
  if (current != NULL) {
    bool seen = false;
    for (size_t i = 0; i < port->buffer_count; i++)
      if (port->buffers[i] == current) seen = true;
    if (!seen) {
      if (port->buffer_count == port->buffer_capacity) {
        port->buffer_capacity =
            port->buffer_capacity != 0 ? port->buffer_capacity * 2 : 4;
        port->buffers = xrealloc(
            port->buffers, port->buffer_capacity * sizeof *port->buffers);
      }
      port->buffers[port->buffer_count++] = current;
    }
  }
  for (size_t i = 0; i < port->buffer_count; i++)
    qa_command_buffer_discard_producer(port->buffers[i], port->instance);
}
void qa_mod_command_port_destroy(qa_mod_command_port *port) {
  if (port == NULL) return;
  qa_mod_command_port_close(port);
  free(port->buffers);
  free(port->key);
  free(port->instance);
  free(port);
}
static void port_close_ctx(void *ctx) {
  qa_mod_command_port_close((qa_mod_command_port *)ctx);
}
qa_session_resource qa_mod_command_port_as_resource(
    qa_mod_command_port *port) {
  qa_session_resource resource = {port, port_close_ctx};
  return resource;
}

qa_mod_command_port *qa_mod_commands_bind(
    qa_mod_commands *commands, qa_mod_command_binding binding,
    qa_resource_scope *resources) {
  qa_resource_scope_assert_open(resources);
  char *key = qa_mod_selection_key(binding.selection);
  if (key == NULL) abort();
  char *instance_provider = qa_mod_instance_provider(binding.selection);
  if (instance_provider == NULL) abort();
  bool module_ok = str_eq(binding.module.id, instance_provider);
  free(instance_provider);
  if (!module_ok)
    qa_errorf("Mod commands require their component's module identity");
  qa_command_context cvars_context =
      qa_cvar_registry_context(binding.cvars);
  if (!same_session(cvars_context.session, commands->context.session))
    qa_errorf("Mod command cvars belong to another session");
  if (find_selection(commands, key) != NULL)
    qa_errorf("Mod commands are already bound: %s", key);
  qa_mod_command_port *port = calloc(1, sizeof *port);
  if (port == NULL) abort();
  port->commands = commands;
  port->binding = binding;
  port->key = key;
  port->resources = resources;
  port->instance = malloc(1);
  if (port->instance == NULL) abort();
  port->producer.kind = "game-module";
  port->producer.module = binding.module;
  port->producer.instance = port->instance;
  if (commands->entry_count == commands->entry_capacity) {
    commands->entry_capacity =
        commands->entry_capacity != 0 ? commands->entry_capacity * 2 : 8;
    commands->entries = xrealloc(
        commands->entries, commands->entry_capacity * sizeof *commands->entries);
  }
  commands->entries[commands->entry_count++] = port;
  qa_resource_scope_own(resources, qa_mod_command_port_as_resource(port));
  return port;
}

static qa_mod_command_port *entry_of(qa_mod_commands *commands,
                                     qa_command_context source) {
  if (source.producer == NULL || source.producer->instance == NULL)
    return NULL;
  if (!same_session(source.session, commands->context.session))
    qa_errorf("Mod command source belongs to another session");
  qa_mod_command_port *entry =
      find_entry(commands, source.producer->instance);
  if (entry == NULL)
    qa_errorf("Mod command producer is no longer bound to this world");
  qa_module_identity module = source.producer->module;
  qa_module_identity expected = entry->producer.module;
  if (!str_eq(module.id, expected.id) ||
      !str_eq(module.artifact_path, expected.artifact_path) ||
      !str_eq(module.digest, expected.digest) ||
      !str_eq(module.revision, expected.revision))
    qa_errorf("Mod command source differs from its admitted module");
  return entry;
}

qa_cvar_registry *qa_mod_commands_cvars(qa_mod_commands *commands,
                                        qa_command_context source) {
  qa_mod_command_port *entry = entry_of(commands, source);
  return entry != NULL ? entry->binding.cvars : NULL;
}
bool qa_mod_commands_active(qa_mod_commands *commands,
                            qa_command_context source) {
  if (source.producer == NULL || source.producer->instance == NULL)
    return false;
  if (find_entry(commands, source.producer->instance) == NULL)
    return false;
  return entry_of(commands, source) != NULL;
}
bool qa_mod_commands_handles(qa_mod_commands *commands, const char *name,
                             qa_command_context source) {
  qa_mod_command_port *entry = entry_of(commands, source);
  if (entry == NULL) return false;
  for (size_t i = 0; i < entry->binding.name_count; i++)
    if (str_ieq(entry->binding.names[i], name)) return true;
  return false;
}
const char *qa_mod_commands_read_script(qa_mod_commands *commands,
                                        const char *name,
                                        qa_command_context source) {
  qa_mod_command_port *entry = entry_of(commands, source);
  if (entry == NULL || entry->binding.read_script == NULL) return NULL;
  return entry->binding.read_script(entry->binding.ctx, name);
}
bool qa_mod_commands_invoke(qa_mod_commands *commands,
                            qa_command_invocation *command) {
  qa_command_invocation_assert_active(command);
  qa_mod_command_port *entry = entry_of(commands, command->source);
  return entry != NULL &&
         entry->binding.invoke(entry->binding.ctx, command);
}
double qa_mod_commands_execute(qa_mod_commands *commands,
                               qa_mod_selection selection,
                               const char *text,
                               qa_command_context caller) {
  char *key = qa_mod_selection_key(selection);
  if (key == NULL) abort();
  qa_mod_command_port *entry = find_selection(commands, key);
  if (entry == NULL) qa_errorf("Mod commands are unavailable: %s", key);
  free(key);
  return qa_mod_command_port_execute_now(entry, text, &caller);
}
