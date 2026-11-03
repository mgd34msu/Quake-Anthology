/* Port of src/app/bootstrap/startup-config.ts. Async reads/execution
 * are synchronous (landed CommandBuffer). read returns malloc'd text
 * or NULL for TS undefined. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_CONFIG_H
#define QA_APP_BOOTSTRAP_STARTUP_CONFIG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
/* TS StartupScriptScope exact strings. */
typedef const char *qa_app_startup_script_scope;
typedef struct qa_app_startup_config_options {
  qa_command_dialect dialect;
  qa_command_context context;
  bool has_mod;
  const char *scope; /* "source"|"seat", NULL = source */
  bool safe_mode;
  void *read_ctx;
  char *(*read)(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope);
  void *apply_ctx;
  void (*apply_selected_defaults)(void *ctx);
  void (*apply_archive)(void *ctx);
  void (*apply_launch_options)(void *ctx);
  void (*replay_startup_variables)(void *ctx); /* NULL absent */
} qa_app_startup_config_options;
typedef struct qa_app_startup_config qa_app_startup_config;
qa_app_startup_config *qa_app_startup_config_create(const qa_app_startup_config_options *options);
void qa_app_startup_config_destroy(qa_app_startup_config *c);
bool qa_app_startup_config_owns_source(const qa_app_startup_config *c, qa_command_context source);
bool qa_app_startup_config_restrict_shared_configuration(const qa_app_startup_config *c);
/* TS readScript property: malloc'd text or NULL. */
char *qa_app_startup_config_read_script(qa_app_startup_config *c, const char *name, qa_command_context source);
/* TS onScriptComplete property (bind via qa_command_buffer_bind_script_completion). */
void qa_app_startup_config_on_script_complete(qa_app_startup_config *c, const qa_script_completion *event);
/* TS Pick<CommandBuffer, "append">: the append executeFrame queues `exec <script>` through.
 * fn NULL is the buffer's own append (PreparedStartup passes its CommandBuffer); the profile
 * configuration passes an appendPreparation adapter, as TS does. */
typedef struct qa_app_startup_config_append {
  void (*fn)(void *ctx, qa_command_buffer *commands, const char *text, const qa_command_context *context);
  void *ctx;
} qa_app_startup_config_append;
/* False means a native wait left work for another frame. */
bool qa_app_startup_config_execute_frame(qa_app_startup_config *c, qa_command_buffer *commands,
                                         qa_app_startup_config_append append,
                                         qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                         qa_should_continue_fn should_continue, void *continue_ctx);
typedef struct qa_app_startup_script_reader_options {
  void *ctx;
  uint8_t *(*mounted)(void *ctx, const char *name, size_t *out_length); /* NULL = undefined */
  char *(*user)(void *ctx, const char *name, qa_command_context source);
  const char *const *base_loose_roots; size_t base_loose_root_count;
  const char *const *game_loose_roots; size_t game_loose_root_count;
  const char *seat_root; /* NULL absent */
} qa_app_startup_script_reader_options;
/* Fills out_read/out_ctx with a reader over a malloc'd options copy
 * (leaked for the reader's lifetime, as the TS closure captures). */
void qa_app_create_startup_script_reader(const qa_app_startup_script_reader_options *options,
                                         char *(**out_read)(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope),
                                         void **out_ctx);
#endif
