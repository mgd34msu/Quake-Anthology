/* Port of src/app/bootstrap/q3-client/cvars.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/cvars.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/cvars/info.h"
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
typedef struct handle_entry {
  qa_cvar_registry *owner;
  char *name;
  qa_cvar_registry *previous_owner;
  bool has_previous;
  qa_cvar_snapshot previous;
  double revision;
} handle_entry;
struct qa_app_q3_client_cvars {
  qa_app_q3_client_cvars_options options;
  handle_entry *handles;
  size_t handle_count, handle_capacity;
};
static char *xstrdup(const char *s) { char *p = strdup(s); if (p == NULL) qa_range_error("Out of memory"); return p; }
qa_app_q3_client_cvars *qa_app_q3_client_cvars_create(qa_app_q3_client_cvars_options options) {
  qa_app_q3_client_cvars *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->options = options;
  return c;
}
void qa_app_q3_client_cvars_destroy(qa_app_q3_client_cvars *cvars) {
  if (cvars == NULL) return;
  for (size_t i = 0; i < cvars->handle_count; i++) {
    free(cvars->handles[i].name);
    if (cvars->handles[i].has_previous) qa_cvar_read_free(&cvars->handles[i].previous);
  }
  free(cvars->handles);
  free(cvars);
}
static qa_cvar_registry *owner_of(qa_app_q3_client_cvars *c, const char *name) {
  return c->options.owner(c->options.ctx, name);
}
bool qa_app_q3_client_cvars_get(qa_app_q3_client_cvars *c, const char *name, qa_cvar_snapshot *out) {
  return qa_cvar_registry_get(owner_of(c, name), name, out);
}
bool qa_app_q3_client_cvars_set(qa_app_q3_client_cvars *c, const char *name, const char *value,
                                bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set(owner_of(c, name), name, value, force, out);
}
bool qa_app_q3_client_cvars_set_value(qa_app_q3_client_cvars *c, const char *name, double value, qa_cvar_snapshot *out) {
  return qa_cvar_registry_set_value(owner_of(c, name), name, value, out);
}
bool qa_app_q3_client_cvars_reset(qa_app_q3_client_cvars *c, const char *name, bool force, qa_cvar_snapshot *out) {
  return qa_cvar_registry_reset(owner_of(c, name), name, force, out);
}
bool qa_app_q3_client_cvars_register(qa_app_q3_client_cvars *c, const char *name, const char *value,
                                     bool has_flags, double flags, qa_cvar_snapshot *out) {
  qa_cvar_registry *owner = owner_of(c, name);
  if (qa_cvar_registry_dialect(owner) != QA_COMMAND_DIALECT_Q3) {
    char *canonical = qa_cvar_registry_canonical_name(owner, name);
    double effective = has_flags ? flags : 0;
    bool alias = canonical == NULL || strcmp(canonical, name) != 0;
    free(canonical);
    if (alias && ((qa_js_to_int32(effective)) & (QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_SERVER_INFO | QA_CVAR_FLAG_SYSTEM_INFO)) != 0)
      qa_range_error("Cvar alias %s requires an explicit protocol info-key mapping", name);
    if (!qa_cvar_registry_get(owner, name, out))
      qa_range_error("Unknown guest cvars require a Q3 seat owner");
    return true;
  }
  return qa_cvar_registry_register(owner, name, value, has_flags ? flags : 0, out);
}
double qa_app_q3_client_cvars_bind_vm(qa_app_q3_client_cvars *c, const char *name, const char *value, double flags) {
  qa_cvar_registry *owner = owner_of(c, name);
  qa_cvar_snapshot registered;
  if (!qa_app_q3_client_cvars_register(c, name, value, true, flags, &registered))
    qa_range_error("VM cvar registration failed");
  for (size_t i = 0; i < c->handle_count; i++) {
    if (c->options.current(c->options.ctx, c->handles[i].owner) == owner
        && strcmp(c->handles[i].name, registered.name) == 0) {
      qa_cvar_read_free(&registered);
      return (double)i;
    }
  }
  if (c->handle_count == 1024) {
    qa_cvar_read_free(&registered);
    qa_range_error("MAX_CVARS");
  }
  if (c->handle_count == c->handle_capacity) {
    size_t cap = c->handle_capacity == 0 ? 16 : c->handle_capacity * 2;
    handle_entry *next = realloc(c->handles, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    c->handles = next; c->handle_capacity = cap;
  }
  handle_entry *entry = &c->handles[c->handle_count];
  *entry = (handle_entry){0};
  entry->owner = owner;
  entry->name = xstrdup(registered.name);
  entry->previous_owner = owner;
  qa_cvar_read_free(&registered);
  return (double)c->handle_count++;
}
bool qa_app_q3_client_cvars_read_vm(qa_app_q3_client_cvars *c, double handle, qa_cvar_snapshot *out) {
  if (handle != trunc(handle) || handle < 0 || handle >= (double)c->handle_count)
    qa_range_error("Cvar_Update: handle out of range");
  handle_entry *entry = &c->handles[(size_t)handle];
  qa_cvar_registry *owner = c->options.current(c->options.ctx, entry->owner);
  qa_cvar_snapshot value;
  if (!qa_cvar_registry_get(owner, entry->name, &value)) return false;
  bool changed = owner != entry->previous_owner || !entry->has_previous
    || strcmp(value.value, entry->previous.value) != 0
    || value.numeric_value != entry->previous.numeric_value
    || value.integer_value != entry->previous.integer_value
    || value.modification_count != entry->previous.modification_count;
  if (changed) entry->revision++;
  entry->previous_owner = owner;
  if (entry->has_previous) qa_cvar_read_free(&entry->previous);
  /* Store a copy for the next comparison; hand the caller its own. */
  entry->previous.name = xstrdup(value.name);
  entry->previous.value = xstrdup(value.value);
  entry->previous.reset_value = xstrdup(value.reset_value);
  entry->previous.latched_value = value.latched_value != NULL ? xstrdup(value.latched_value) : NULL;
  entry->previous.flags = value.flags;
  entry->previous.modified = value.modified;
  entry->previous.modification_count = value.modification_count;
  entry->previous.numeric_value = value.numeric_value;
  entry->previous.integer_value = value.integer_value;
  entry->has_previous = true;
  *out = value;
  out->modification_count = entry->revision;
  return true;
}
static void print_bridge(void *ctx, const char *text) {
  qa_app_q3_client_cvars *c = ctx;
  c->options.print(c->options.ctx, text);
}
char *qa_app_q3_client_cvars_info_string(qa_app_q3_client_cvars *c, double flags, double maximum_length) {
  double max_len = maximum_length < 0 ? 1024 : maximum_length;
  char *result = xstrdup("");
  qa_cvar_registry **owners = NULL;
  size_t owner_count = c->options.visible(c->options.ctx, &owners);
  for (size_t i = 0; i < owner_count; i++) {
    qa_cvar_registry *owner = owners[i];
    qa_command_dialect dialect = qa_cvar_registry_dialect(owner);
    size_t count = 0;
    qa_cvar_snapshot *snapshots = qa_cvar_registry_canonical_snapshots(owner, 0, &count);
    for (size_t j = 0; j < count; j++) {
      qa_cvar_snapshot *value = &snapshots[j];
      int64_t projected = dialect == QA_COMMAND_DIALECT_Q3 ? value->flags
        : value->flags & (QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_SERVER_INFO);
      bool q2_private = (dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE)
        && (value->flags & QA_Q2_CVAR_FLAG_PRIVATE) != 0;
      if (owner_of(c, value->name) != owner || (projected & (int64_t)flags) == 0 || q2_private) continue;
      qa_info_options options = {QA_COMMAND_DIALECT_Q3, (int64_t)max_len,
        (qa_js_to_int32(flags) & QA_CVAR_FLAG_USER_INFO) != 0 ? QA_CVAR_INFO_CLIENT_USERINFO : QA_CVAR_INFO_SERVER_INFO,
        false, print_bridge, c};
      char *next = qa_set_info_value(result, value->name, value->value, &options);
      free(result);
      result = next != NULL ? next : xstrdup("");
    }
    for (size_t j = 0; j < count; j++) qa_cvar_read_free(&snapshots[j]);
    free(snapshots);
  }
  return result;
}
static bool svc_get(void *ctx, const char *n, qa_cvar_snapshot *o) { return qa_app_q3_client_cvars_get(ctx, n, o); }
static bool svc_set(void *ctx, const char *n, const char *v, bool f, qa_cvar_snapshot *o) { return qa_app_q3_client_cvars_set(ctx, n, v, f, o); }
static bool svc_set_value(void *ctx, const char *n, double v, qa_cvar_snapshot *o) { return qa_app_q3_client_cvars_set_value(ctx, n, v, o); }
static bool svc_reset(void *ctx, const char *n, bool f, qa_cvar_snapshot *o) { return qa_app_q3_client_cvars_reset(ctx, n, f, o); }
static bool svc_register(void *ctx, const char *n, const char *v, bool hf, double f, qa_cvar_snapshot *o) { return qa_app_q3_client_cvars_register(ctx, n, v, hf, f, o); }
static double svc_bind(void *ctx, const char *n, const char *v, double f) { return qa_app_q3_client_cvars_bind_vm(ctx, n, v, f); }
static bool svc_read(void *ctx, double h, qa_cvar_snapshot *o) { return qa_app_q3_client_cvars_read_vm(ctx, h, o); }
static char *svc_info(void *ctx, double f, double m) { return qa_app_q3_client_cvars_info_string(ctx, f, m); }
qa_app_qvm_cvar_services qa_app_q3_client_cvars_services(qa_app_q3_client_cvars *cvars) {
  return (qa_app_qvm_cvar_services){cvars, svc_get, svc_set, svc_set_value, svc_reset,
                                    svc_register, svc_bind, svc_read, svc_info};
}
