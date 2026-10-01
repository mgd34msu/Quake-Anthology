#ifndef QA_CVARS_ALIAS_H
#define QA_CVARS_ALIAS_H
#include "qa/console_cvars_prepare.h"

typedef enum qa_cvar_alias_conversion {
    QA_CVAR_ALIAS_IDENTITY,
    QA_CVAR_ALIAS_RECIPROCAL_GAMMA,
    QA_CVAR_ALIAS_KILOHERTZ
} qa_cvar_alias_conversion;
/* Aliases retain their declarations in the canonical registry. They cannot
 * target aliases or protocol info keys. Projection does not create a scalar,
 * binding or archive row. Documentation is copied from this declaration. */
bool qa_cvars_alias_register(qa_cvars *, const char *name, const char *target,
    qa_cvar_alias_conversion, const char *description,
    const qa_console_documentation *, qa_error *);
const char *qa_cvars_canonical_name(const qa_cvars *, const char *name);
/* Canonical count/at retain their physical-record contract. Visible snapshots
 * append declared aliases in insertion order after physical records. */
size_t qa_cvars_visible_count(const qa_cvars *);
const qa_cvar_view *qa_cvars_visible_at(const qa_cvars *, size_t);
size_t qa_cvars_edit_visible_count(const qa_cvars_edit *);
const qa_cvar_view *qa_cvars_edit_visible_at(const qa_cvars_edit *, size_t);
/* Converted aliases allocate a genuine separate Q3 VM handle on first bind;
 * identity aliases use the canonical handle. Find never allocates a handle. */
bool qa_cvars_vm_bind(qa_cvars *, const char *name, const char *default_value,
                      uint32_t flags, uint64_t owner, size_t *handle, qa_error *);
bool qa_cvars_edit_vm_bind(qa_cvars_edit *, const char *name, const char *default_value,
                           uint32_t flags, uint64_t owner, size_t *handle, qa_error *);
const qa_cvar_view *qa_cvars_edit_handle(const qa_cvars_edit *, size_t handle);
#endif
