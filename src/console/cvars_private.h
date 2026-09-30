#ifndef QA_CVARS_PRIVATE_H
#define QA_CVARS_PRIVATE_H
#include "internal.h"

typedef struct cvar {
    qa_cvar_view view;
    qa_cvar_binding binding;
    bool bound;
    struct cvar *next;
} cvar;
struct qa_cvars {
    qa_cvar_options options;
    cvar *first;
    size_t count, next_handle;
    uint32_t changed_flags;
    bool userinfo_modified, server_active, high_characters, cheats;
    uint64_t mutation_revision;
};
bool qac_cvars_touch(qa_cvars *, qa_error *);
void qac_cvars_entry_free(cvar *);
#endif
