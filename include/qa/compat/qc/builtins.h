/* Port of src/compat/qc/builtins.ts. GPL-2.0-or-later.
 * QcHostKind/QcHostBuiltinName are exact TS strings. Host map and
 * extension set become borrowed string arrays. createQcBuiltins
 * fills a caller-zeroed registry; its captured services live in
 * registry.state (destroyed by qa_compat_qc_registry_destroy). */
#ifndef QA_COMPAT_QC_BUILTINS_H
#define QA_COMPAT_QC_BUILTINS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/qc/machine.h"
#include "qa/contracts/numeric.h"

typedef const char *qa_compat_qc_host_kind;       /* "netquake"|"quakeworld"|"rerelease" */
typedef const char *qa_compat_qc_host_builtin_name;
typedef struct qa_compat_qc_host_binding { qa_compat_qc_host_builtin_name name; qa_compat_qc_builtin fn; } qa_compat_qc_host_binding;
typedef struct qa_compat_qc_builtin_services {
  qa_compat_qc_host_kind kind;
  const qa_random_source *random; /* nullable */
  void *ctx;
  bool (*is_free_entity)(void *ctx, int64_t slot); /* nullable */
  void (*prepare_entities)(void *ctx);             /* nullable */
  const qa_compat_qc_host_binding *host; size_t host_count;
  const char *const *extensions; size_t extension_count; bool has_extensions;
} qa_compat_qc_builtin_services;
typedef struct qa_compat_qc_builtin_requirement { bool has_number; int64_t number; qa_compat_qc_host_builtin_name name; } qa_compat_qc_builtin_requirement;

qa_compat_qc_host_kind qa_compat_qc_host_kind_for_program(const qa_compat_qc_program *program);
/* Writes up to capacity, returns total count. */
size_t qa_compat_qc_host_requirements(qa_compat_qc_host_kind kind, qa_compat_qc_builtin_requirement *out, size_t capacity);
/* False only on OOM. Registry must be zeroed; destroy after use. */
bool qa_compat_qc_create_builtins(qa_compat_qc_builtin_services services, qa_compat_qc_builtin_registry *out);
#endif
