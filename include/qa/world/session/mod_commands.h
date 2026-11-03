/*
 * Port of src/world/session/mod-commands.ts: per-component command
 * producers over one prepared world's command buffer. TS plain
 * Error throws abort identically. The TS symbol instance becomes a
 * unique heap cell owned by the port. readScript is synchronous
 * (the TS Promise alternative has no C counterpart) and returns a
 * borrowed string or NULL. Ports are heap objects owned through
 * the caller's ResourceScope (TS resources.own); destroy after the
 * scope closes them. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SESSION_MOD_COMMANDS_H
#define QA_WORLD_SESSION_MOD_COMMANDS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/mods.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/world/session/resources.h"

/** TS ModCommandBinding. read_script may be NULL. */
typedef struct qa_mod_command_binding {
  void *ctx;
  qa_mod_selection selection;
  qa_module_identity module;
  qa_cvar_registry *cvars;
  const char *const *names; /* optional */
  size_t name_count;
  bool (*invoke)(void *ctx, qa_command_invocation *command);
  const char *(*read_script)(void *ctx, const char *name);
} qa_mod_command_binding;

/** TS ModCommandPort (a SessionResource). caller may be NULL;
 * execute_now text may be NULL (TS null drains the buffer). */
typedef struct qa_mod_command_port qa_mod_command_port;
qa_command_producer qa_mod_command_port_producer(
    const qa_mod_command_port *port);
void qa_mod_command_port_append(qa_mod_command_port *port,
                                const char *text,
                                const qa_command_context *caller);
void qa_mod_command_port_insert(qa_mod_command_port *port,
                                const char *text,
                                const qa_command_context *caller);
double qa_mod_command_port_execute_now(
    qa_mod_command_port *port, const char *text,
    const qa_command_context *caller);
void qa_mod_command_port_close(qa_mod_command_port *port);
void qa_mod_command_port_destroy(qa_mod_command_port *port);
qa_session_resource qa_mod_command_port_as_resource(
    qa_mod_command_port *port);

/* TS readModCommand. On success selection is filled (release with
 * qa_mod_selection_free) and *out_text is malloc'd. False is OOM. */
bool qa_read_mod_command(const char *raw, qa_mod_selection *out_selection,
                         char **out_text);

typedef qa_command_buffer *(*qa_mod_commands_buffer_fn)(void *ctx);

typedef struct qa_mod_commands qa_mod_commands;

/* NULL only on allocation failure. */
qa_mod_commands *qa_mod_commands_create(
    qa_command_context context, qa_mod_commands_buffer_fn commands,
    void *commands_ctx);
/* Frees the commands and every port bound to them; close the owning mod scopes first. */
void qa_mod_commands_destroy(qa_mod_commands *commands);
void qa_mod_commands_append(qa_mod_commands *commands, const char *text,
                            qa_command_context source,
                            qa_command_dialect dialect);
void qa_mod_commands_insert(qa_mod_commands *commands, const char *text,
                            qa_command_context source,
                            qa_command_dialect dialect);
void qa_mod_commands_flush(qa_mod_commands *commands);
/* Binds and owns the port through resources (TS resources.own). */
qa_mod_command_port *qa_mod_commands_bind(
    qa_mod_commands *commands, qa_mod_command_binding binding,
    qa_resource_scope *resources);
/* NULL is the TS null (no producer entry). */
qa_cvar_registry *qa_mod_commands_cvars(qa_mod_commands *commands,
                                        qa_command_context source);
bool qa_mod_commands_active(qa_mod_commands *commands,
                            qa_command_context source);
bool qa_mod_commands_handles(qa_mod_commands *commands, const char *name,
                             qa_command_context source);
const char *qa_mod_commands_read_script(qa_mod_commands *commands,
                                        const char *name,
                                        qa_command_context source);
bool qa_mod_commands_invoke(qa_mod_commands *commands,
                            qa_command_invocation *command);
double qa_mod_commands_execute(qa_mod_commands *commands,
                               qa_mod_selection selection,
                               const char *text,
                               qa_command_context caller);

#endif
