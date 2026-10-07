#ifndef QA_CVARS_ALIAS_H
#define QA_CVARS_ALIAS_H
#include "qa/console_cvars_prepare.h"

const char *qa_cvars_canonical_name(const qa_cvars *, const char *name);
/* Engine listings are canonical; expanded listings include generated aliases.
 * Source view iteration retains actual declared Source names and handles. */
size_t qa_cvars_visible_count(const qa_cvars *);
const qa_cvar_view *qa_cvars_visible_at(const qa_cvars *, size_t);
size_t qa_cvars_edit_visible_count(const qa_cvars_edit *);
const qa_cvar_view *qa_cvars_edit_visible_at(const qa_cvars_edit *, size_t);
/* Each physical Source declaration receives its own handle on first bind.
 * Lookup does not allocate a handle; projections share the canonical value. */
bool qa_cvars_vm_bind(qa_cvars *, const char *name, const char *default_value,
                      uint32_t flags, uint64_t owner, size_t *handle, qa_error *);
/* Resolve a current declaration or converted alias without registering it or
 * applying pending Source latches. */
bool qa_cvars_vm_rebind(qa_cvars *, const char *name, size_t *handle, qa_error *);
bool qa_cvars_edit_vm_bind(qa_cvars_edit *, const char *name, const char *default_value,
                           uint32_t flags, uint64_t owner, size_t *handle, qa_error *);
const qa_cvar_view *qa_cvars_edit_handle(const qa_cvars_edit *, size_t handle);
#endif
