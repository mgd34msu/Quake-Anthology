/* Port of src/app/bootstrap/startup-config.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/mounts/paths.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
typedef struct script { const char *name; const char *scope; } script;
struct qa_app_startup_config {
  qa_app_startup_config_options options;
  script scripts[6]; size_t script_count;
  long caller_depth;
  size_t index;
  bool has_active; script active;
  bool completed;
  const char *failure;
  bool dedicated_qw;
  bool defaults_applied, archive_applied;
};
static long script_depth(const qa_command_origin *origin) {
  long depth = 0;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) { depth++; origin = origin->caller; }
  return depth;
}
static size_t startup_scripts(qa_command_dialect dialect, bool has_mod, bool dedicated_qw, script *out) {
  size_t n = 0;
  if (dedicated_qw) { out[n++] = (script){"server.cfg", "user"}; return n; }
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD) {
    out[n++] = (script){"quake.rc", "mounted"}; return n;
  }
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    out[n++] = (script){"default.cfg", "mounted"};
    out[n++] = (script){"q3config.cfg", "user"};
    out[n++] = (script){"autoexec.cfg", "user"};
    return n;
  }
  if (dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    out[n++] = (script){"default.cfg", "mounted"};
    out[n++] = (script){"config.cfg", "loose"};
    if (has_mod) out[n++] = (script){"autoexec.cfg", "base-loose"};
    out[n++] = (script){"autoexec.cfg", "game-loose"};
    out[n++] = (script){"postexec.cfg", "loose"};
    return n;
  }
  out[n++] = (script){"default.cfg", "mounted"};
  out[n++] = (script){"config.cfg", "user"};
  out[n++] = (script){"autoexec.cfg", "game-loose"};
  return n;
}
qa_app_startup_config *qa_app_startup_config_create(const qa_app_startup_config_options *options) {
  qa_app_startup_config *c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->options = *options;
  const qa_command_origin *origin = &options->context.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  c->dedicated_qw = options->dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD && origin->kind == QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  bool seat_scope = options->scope != NULL && strcmp(options->scope, "seat") == 0;
  if (seat_scope) {
    c->scripts[0] = (script){options->dialect == QA_COMMAND_DIALECT_Q3 ? "q3config.cfg" : "config.cfg", "seat"};
    c->scripts[1] = (script){"autoexec.cfg", "seat"};
    c->script_count = 2;
  } else c->script_count = startup_scripts(options->dialect, options->has_mod, c->dedicated_qw, c->scripts);
  c->defaults_applied = seat_scope;
  c->caller_depth = script_depth(&options->context.origin);
  return c;
}
void qa_app_startup_config_destroy(qa_app_startup_config *c) { free(c); }
bool qa_app_startup_config_owns_source(const qa_app_startup_config *c, qa_command_context source) {
  if (!c->has_active || source.session.token != c->options.context.session.token) return false;
  const qa_command_origin *root = &source.origin;
  long depth = script_depth(root);
  while (root->kind == QA_COMMAND_ORIGIN_SCRIPT && depth > c->caller_depth + 1) { root = root->caller; depth--; }
  if (root->kind != QA_COMMAND_ORIGIN_SCRIPT || depth != c->caller_depth + 1 || strcmp(root->name, c->active.name) != 0) return false;
  const qa_command_origin *caller = root->caller;
  const qa_command_origin *expected = &c->options.context.origin;
  while (caller->kind == QA_COMMAND_ORIGIN_SCRIPT && expected->kind == QA_COMMAND_ORIGIN_SCRIPT) {
    if (strcmp(caller->name, expected->name) != 0) return false;
    caller = caller->caller; expected = expected->caller;
  }
  switch (caller->kind) {
    case QA_COMMAND_ORIGIN_LOCAL_CONSOLE:
    case QA_COMMAND_ORIGIN_SERVER_CONSOLE: return caller->kind == expected->kind;
    case QA_COMMAND_ORIGIN_LOCAL_SEAT:
      return expected->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT && qa_same_seat(caller->seat, expected->seat) && qa_same_client(caller->client, expected->client);
    case QA_COMMAND_ORIGIN_REMOTE_CLIENT:
      return expected->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT && qa_same_client(caller->client, expected->client);
    case QA_COMMAND_ORIGIN_SCRIPT: return false;
  }
  return false;
}
bool qa_app_startup_config_restrict_shared_configuration(const qa_app_startup_config *c) {
  bool seat_scope = c->options.scope != NULL && strcmp(c->options.scope, "seat") == 0;
  return seat_scope && c->has_active && (strcmp(c->active.name, "config.cfg") == 0 || strcmp(c->active.name, "q3config.cfg") == 0);
}
char *qa_app_startup_config_read_script(qa_app_startup_config *c, const char *name, qa_command_context source) {
  long depth = script_depth(&source.origin);
  bool direct = c->has_active && depth == c->caller_depth && strcmp(name, c->active.name) == 0;
  bool q1_default = c->has_active && strcmp(c->active.name, "quake.rc") == 0 && depth == c->caller_depth + 1
      && source.origin.kind == QA_COMMAND_ORIGIN_SCRIPT && strcmp(source.origin.name, "quake.rc") == 0 && strcmp(name, "default.cfg") == 0;
  bool seat_scope = c->options.scope != NULL && strcmp(c->options.scope, "seat") == 0;
  const char *scope = direct ? c->active.scope : q1_default ? "mounted" : seat_scope ? "seat" : "user";
  return c->options.read(c->options.read_ctx, name, source, scope);
}
void qa_app_startup_config_on_script_complete(qa_app_startup_config *c, const qa_script_completion *event) {
  if (!c->has_active) return;
  const qa_command_origin *origin = &event->source.origin;
  long depth = script_depth(origin);
  bool direct = depth == c->caller_depth + 1 && strcmp(event->name, c->active.name) == 0;
  bool q1_child = strcmp(c->active.name, "quake.rc") == 0 && depth == c->caller_depth + 2
      && origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller->kind == QA_COMMAND_ORIGIN_SCRIPT
      && strcmp(origin->caller->name, "quake.rc") == 0;
  if (event->result == QA_SCRIPT_FAILED) qa_range_error("%s", event->error_message ? event->error_message : "Script failed");
  if (!direct && !q1_child) return;
  if (strcmp(event->name, "default.cfg") == 0 && !c->defaults_applied) {
    c->defaults_applied = true; c->options.apply_selected_defaults(c->options.apply_ctx);
  }
  if ((strcmp(event->name, "config.cfg") == 0 || strcmp(event->name, "q3config.cfg") == 0) && !c->archive_applied) {
    c->archive_applied = true; c->options.apply_archive(c->options.apply_ctx);
  }
  qa_command_dialect d = c->options.dialect;
  if ((d == QA_COMMAND_DIALECT_Q3 && direct && strcmp(event->name, "autoexec.cfg") == 0)
      || ((d == QA_COMMAND_DIALECT_Q2_CLASSIC || d == QA_COMMAND_DIALECT_Q2_RERELEASE) && direct && strcmp(event->name, "config.cfg") == 0)) {
    if (c->options.replay_startup_variables) c->options.replay_startup_variables(c->options.apply_ctx);
  }
  if (direct) c->has_active = false;
}
bool qa_app_startup_config_execute_frame(qa_app_startup_config *c, qa_command_buffer *commands,
                                         qa_app_startup_config_append append,
                                         qa_after_dispatch_fn after_dispatch, void *after_ctx,
                                         qa_should_continue_fn should_continue, void *continue_ctx) {
  if (c->failure != NULL) qa_range_error("%s", c->failure);
  if (c->completed) return true;
  if (c->dedicated_qw && !c->archive_applied) {
    c->defaults_applied = true; c->options.apply_selected_defaults(c->options.apply_ctx);
    c->archive_applied = true; c->options.apply_archive(c->options.apply_ctx);
  }
  for (;;) {
    if (!c->has_active) {
      if (c->index >= c->script_count) {
        if (!c->defaults_applied || !c->archive_applied) {
          c->failure = "Startup script did not reach default and archived configuration stages";
          qa_range_error("%s", c->failure);
        }
        c->completed = true;
        c->options.apply_launch_options(c->options.apply_ctx);
        return true;
      }
      script s = c->scripts[c->index++];
      if (c->options.dialect == QA_COMMAND_DIALECT_Q3 && c->options.safe_mode && strcmp(s.name, "q3config.cfg") == 0) {
        c->archive_applied = true;
        continue;
      }
      c->active = s; c->has_active = true;
      char text[512];
      snprintf(text, sizeof text, "exec %s\n", s.name);
      if (append.fn != NULL) append.fn(append.ctx, commands, text, &c->options.context);
      else qa_command_buffer_append(commands, text, &c->options.context, -1);
    }
    qa_command_buffer_execute_scripts_async(commands, after_dispatch, after_ctx, should_continue, continue_ctx);
    if (c->has_active || (should_continue != NULL && !should_continue(continue_ctx))) return false;
  }
}
static char *read_file_latin1(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)size + 1);
  if (!buf) { fclose(f); qa_range_error("Out of memory"); }
  size_t got = fread(buf, 1, (size_t)size, f);
  fclose(f);
  buf[got] = '\0';
  return buf;
}
static char *reader_read(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope) {
  qa_app_startup_script_reader_options *options = ctx;
  if (strcmp(scope, "seat") == 0) {
    const qa_command_origin *origin = &source.origin;
    while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
    if (origin->kind != QA_COMMAND_ORIGIN_LOCAL_SEAT || options->seat_root == NULL) return NULL;
    char rel[1024];
    char *idx = qa_input_js_number((double)origin->seat.index);
    snprintf(rel, sizeof rel, "settings/seat-%s/%s", idx ? idx : "0", name);
    free(idx);
    char *path = NULL;
    if (!qa_find_content_path(options->seat_root, rel, QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) return NULL;
    char *r = read_file_latin1(path);
    free(path);
    return r;
  }
  if (strcmp(scope, "user") == 0) return options->user(options->ctx, name, source);
  if (strcmp(scope, "mounted") == 0) {
    size_t len = 0;
    uint8_t *bytes = options->mounted(options->ctx, name, &len);
    if (bytes == NULL) return NULL;
    char *r = qa_input_dup_range((const char *)bytes, len);
    free(bytes);
    if (!r) qa_range_error("Out of memory");
    return r;
  }
  const char *const *first = options->game_loose_roots;
  size_t first_count = options->game_loose_root_count;
  const char *const *second = options->base_loose_roots;
  size_t second_count = options->base_loose_root_count;
  if (strcmp(scope, "base-loose") == 0) { first = second; first_count = second_count; second_count = 0; }
  if (strcmp(scope, "game-loose") == 0) second_count = 0;
  for (int pass = 0; pass < 2; pass++) {
    const char *const *roots = pass == 0 ? first : second;
    size_t count = pass == 0 ? first_count : second_count;
    for (size_t i = 0; i < count; i++) {
      char *path = NULL;
      if (!qa_find_content_path(roots[i], name, QA_CONTENT_PATH_CASE_INSENSITIVE, &path)) continue;
      char *r = read_file_latin1(path);
      free(path);
      return r;
    }
  }
  return NULL;
}
void qa_app_create_startup_script_reader(const qa_app_startup_script_reader_options *options,
                                         char *(**out_read)(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope),
                                         void **out_ctx) {
  qa_app_startup_script_reader_options *copy = malloc(sizeof *copy);
  if (!copy) qa_range_error("Out of memory");
  *copy = *options;
  *out_read = reader_read;
  *out_ctx = copy;
}
