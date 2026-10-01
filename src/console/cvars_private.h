#ifndef QA_CVARS_PRIVATE_H
#define QA_CVARS_PRIVATE_H
#include "internal.h"
#include "qa/console_cvar_observer.h"

typedef struct cvar_observer {
    struct cvar_observer *next;
    char *name;
    qa_cvar_observer_token token;
    uint64_t owner;
    qa_cvar_observer_fn callback;
    void *user;
    size_t references;
    bool active, suppressed;
} cvar_observer;
typedef struct cvar_post_event {
    struct cvar_post_event *next;
    size_t count;
    cvar_observer *observers[];
} cvar_post_event;

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
    cvar_observer *observers, *last_observer;
    cvar_post_event *post_first, *post_last;
    qa_cvar_observer_token next_observer;
    size_t mutation_depth, notifying;
    bool draining;
};
bool qac_cvars_touch(qa_cvars *, qa_error *);
void qac_cvars_entry_free(cvar *);
#endif
