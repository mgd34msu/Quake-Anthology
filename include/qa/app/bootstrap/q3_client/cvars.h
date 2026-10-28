/* Port of src/app/bootstrap/q3-client/cvars.ts. The TS
 * QvmCvarServices interface (Pick<CvarRegistry,...>) is an fn+ctx
 * vtable here; the landed compat header typedefs its
 * qa_qvm_cvar_services to a concrete registry, so this abstract
 * shape is local to the q3-client slice. Snapshot outs follow the
 * landed registry convention (bool found + owned qa_cvar_snapshot,
 * free with qa_cvar_read_free). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_CVARS_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_CVARS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/core/cvars/index.h"
/* TS QvmCvarServices. Optional TS args: value NULL / has_flags. */
typedef struct qa_app_qvm_cvar_services {
  void *ctx;
  bool (*get)(void *ctx, const char *name, qa_cvar_snapshot *out);
  bool (*set)(void *ctx, const char *name, const char *value, bool force, qa_cvar_snapshot *out);
  bool (*set_value)(void *ctx, const char *name, double value, qa_cvar_snapshot *out);
  bool (*reset)(void *ctx, const char *name, bool force, qa_cvar_snapshot *out);
  bool (*register_cvar)(void *ctx, const char *name, const char *value, bool has_flags,
                        double flags, qa_cvar_snapshot *out);
  double (*bind_vm)(void *ctx, const char *name, const char *value, double flags);
  bool (*read_vm)(void *ctx, double handle, qa_cvar_snapshot *out);
  /* maximum_length < 0 = TS default 1024. Malloc'd result. */
  char *(*info_string)(void *ctx, double flags, double maximum_length);
} qa_app_qvm_cvar_services;
/* TS Q3ClientCvars options. */
typedef struct qa_app_q3_client_cvars_options {
  void *ctx;
  qa_cvar_registry *(*owner)(void *ctx, const char *name);
  size_t (*visible)(void *ctx, qa_cvar_registry ***out); /* borrowed list */
  qa_cvar_registry *(*current)(void *ctx, qa_cvar_registry *owner);
  void (*print)(void *ctx, const char *text);
} qa_app_q3_client_cvars_options;
typedef struct qa_app_q3_client_cvars qa_app_q3_client_cvars;
qa_app_q3_client_cvars *qa_app_q3_client_cvars_create(qa_app_q3_client_cvars_options options);
void qa_app_q3_client_cvars_destroy(qa_app_q3_client_cvars *cvars);
/* The QvmCvarServices view (ctx = the object; valid while alive). */
qa_app_qvm_cvar_services qa_app_q3_client_cvars_services(qa_app_q3_client_cvars *cvars);
/* Direct method forms (the TS arrow properties). */
bool qa_app_q3_client_cvars_get(qa_app_q3_client_cvars *c, const char *name, qa_cvar_snapshot *out);
bool qa_app_q3_client_cvars_set(qa_app_q3_client_cvars *c, const char *name, const char *value,
                                bool force, qa_cvar_snapshot *out);
bool qa_app_q3_client_cvars_set_value(qa_app_q3_client_cvars *c, const char *name, double value, qa_cvar_snapshot *out);
bool qa_app_q3_client_cvars_reset(qa_app_q3_client_cvars *c, const char *name, bool force, qa_cvar_snapshot *out);
/* Aborts with the TS Error texts for alias/unknown guest cvars. */
bool qa_app_q3_client_cvars_register(qa_app_q3_client_cvars *c, const char *name, const char *value,
                                     bool has_flags, double flags, qa_cvar_snapshot *out);
/* Aborts "VM cvar registration failed" / RangeError "MAX_CVARS". */
double qa_app_q3_client_cvars_bind_vm(qa_app_q3_client_cvars *c, const char *name, const char *value, double flags);
/* Aborts RangeError "Cvar_Update: handle out of range". */
bool qa_app_q3_client_cvars_read_vm(qa_app_q3_client_cvars *c, double handle, qa_cvar_snapshot *out);
char *qa_app_q3_client_cvars_info_string(qa_app_q3_client_cvars *c, double flags, double maximum_length);
#endif
