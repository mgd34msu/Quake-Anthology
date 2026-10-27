/* Port of src/compat/qc/profile.ts. GPL-2.0-or-later.
 * loadMountedQcProgram: async open becomes a synchronous fn+ctx
 * (NULL/0 return = not found). describeQcHost fills caller storage;
 * layout fields point into out's arrays (free via destroy helper).
 * MountedContent/ResolvedResourceReference owners:
 * src/content/mounts, src/contracts/content.ts (landed types). */
#ifndef QA_COMPAT_QC_PROFILE_H
#define QA_COMPAT_QC_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/builtins.h"
#include "qa/contracts/content.h"
typedef struct qa_compat_qc_mounted_open {
  void *ctx;
  /* Returns malloc'd bytes (caller frees) or NULL when absent; fills *out_reference (borrowed storage in *storage) when found. */
  uint8_t *(*open)(void *ctx, const char *path, size_t *out_length, qa_resolved_resource_reference *out_reference, void *storage);
  void *storage;
} qa_compat_qc_mounted_open;
size_t qa_compat_qc_program_search_order(qa_compat_qc_host_kind kind, const char **out, size_t capacity);
bool qa_compat_qc_load_mounted_program(qa_compat_qc_mounted_open content, qa_compat_qc_host_kind kind, qa_compat_qc_program *out_program, qa_resolved_resource_reference *out_resource);
qa_compat_qc_entity_layout qa_compat_qc_classic_entity_layout(const qa_compat_qc_program *program);
typedef struct qa_compat_qc_host_description {
  qa_quakec_host_profile profile;
  qa_guest_layout globals_layout, entity_layout;
  qa_guest_field_layout *global_fields, *entity_fields;
  qa_quakec_builtin_binding *bindings; char **binding_names, **binding_callbacks; char *globals_id, *entity_id;
  const char *search_order[2];
} qa_compat_qc_host_description;
bool qa_compat_qc_describe_host(const qa_compat_qc_program *program, qa_compat_qc_host_kind kind, qa_compat_qc_entity_layout entities, const qa_compat_qc_builtin_registry *registry, const char *const *extensions, size_t extension_count, qa_compat_qc_host_description *out);
void qa_compat_qc_host_description_destroy(qa_compat_qc_host_description *d);
#endif
