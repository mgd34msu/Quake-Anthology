/* Port of src/settings/restart.ts. GPL-2.0-or-later. */
#include "qa/settings/restart.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

bool qa_restart_controls_init(
    qa_restart_controls *controls,
    const qa_restart_service services[QA_RESTART_KIND_COUNT],
    qa_cvar_registry *cvars,
    const char *const *const latched[QA_RESTART_KIND_COUNT],
    const size_t latched_count[QA_RESTART_KIND_COUNT]) {
  memset(controls, 0, sizeof *controls);
  memcpy(controls->services, services, sizeof controls->services);
  controls->cvars = cvars;
  for (int i = 0; i < QA_RESTART_KIND_COUNT; i++) {
    controls->latched[i] = latched[i];
    controls->latched_count[i] = latched_count[i];
  }
  return qa_bounded_queue_init(&controls->pending, 4, sizeof(qa_restart_kind));
}

void qa_restart_controls_destroy(qa_restart_controls *controls) {
  qa_bounded_queue_destroy(&controls->pending);
  memset(controls, 0, sizeof *controls);
}

static bool kind_matches(const void *value, void *ctx) {
  return *(const qa_restart_kind *)value == *(const qa_restart_kind *)ctx;
}

void qa_restart_controls_request(qa_restart_controls *controls,
                                 qa_restart_kind kind) {
  if (qa_bounded_queue_find_index(&controls->pending, kind_matches, &kind) < 0)
    qa_bounded_queue_push(&controls->pending, &kind);
}

void qa_restart_controls_drain(qa_restart_controls *controls) {
  if (controls->running)
    qa_error("Restart controls already have an active operation");
  controls->running = true;
  qa_restart_kind kind;
  while (qa_bounded_queue_shift(&controls->pending, &kind)) {
    for (size_t i = 0; i < controls->latched_count[kind]; i++) {
      size_t applied_count = 0;
      qa_cvar_snapshot *applied = qa_cvar_registry_apply_latched(
          controls->cvars, controls->latched[kind][i], &applied_count);
      for (size_t j = 0; j < applied_count; j++) qa_cvar_read_free(&applied[j]);
      free(applied);
    }
    if (controls->services[kind].restart != NULL)
      controls->services[kind].restart(controls->services[kind].ctx);
  }
  controls->running = false;
}

static const char *const restart_command_names[QA_RESTART_KIND_COUNT] = {
    "vid_restart", "in_restart", "snd_restart"};

static void restart_command_handler(void *ctx, qa_command_invocation *command) {
  (void)command;
  qa_restart_command *handler = (qa_restart_command *)ctx;
  qa_restart_controls_request(handler->controls, handler->kind);
}

void qa_restart_controls_register(qa_restart_controls *controls,
                                  qa_command_buffer *commands,
                                  qa_restart_registration *out) {
  memset(out, 0, sizeof *out);
  out->commands = commands;
  for (int i = 0; i < QA_RESTART_KIND_COUNT; i++) {
    out->handlers[i].controls = controls;
    out->handlers[i].kind = (qa_restart_kind)i;
    out->registered[i] = qa_command_buffer_register(
        commands, restart_command_names[i], restart_command_handler,
        &out->handlers[i], NULL, QA_COMMAND_WORLD);
  }
}

void qa_restart_registration_unregister(qa_restart_registration *registration) {
  for (int i = 0; i < QA_RESTART_KIND_COUNT; i++) {
    if (!registration->registered[i]) continue;
    qa_command_buffer_unregister(registration->commands,
                                 restart_command_names[i],
                                 restart_command_handler,
                                 &registration->handlers[i], true);
    registration->registered[i] = false;
  }
}
