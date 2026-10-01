#ifndef QA_CVARS_PRIVATE_H
#define QA_CVARS_PRIVATE_H
#include "internal.h"
#include "qa/console_cvar_observer.h"
#include "qa/console_cvars_prepare.h"

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
    uint64_t binding_order;
    struct cvar *next;
} cvar;
typedef struct cvar_values {
    cvar *first;
    size_t count, next_handle;
    uint32_t changed_flags;
    bool userinfo_modified, server_active, high_characters, cheats;
} cvar_values;
typedef struct cvar_edit_event cvar_edit_event;
typedef struct cvar_edit_binding {
    const cvar *actual;
    cvar *prepared;
} cvar_edit_binding;
struct qa_cvars_edit {
    qa_cvars *registry;
    cvar_values values;
    uint64_t revision;
    cvar_edit_event *first, *last;
    qa_error fault;
    bool ready;
    cvar_edit_binding *bindings;
    size_t binding_count;
};
typedef struct cvar_target {
    qa_cvars *registry;
    cvar_values *values;
    qa_cvars_edit *edit;
} cvar_target;
struct qa_cvars {
    qa_cvar_options options;
    cvar_values values;
    uint64_t mutation_revision;
    cvar_observer *observers, *last_observer;
    cvar_post_event *post_first, *post_last;
    qa_cvar_observer_token next_observer;
    size_t mutation_depth, notifying;
    bool draining;
    qa_cvars_edit *ready_edit;
    cvar_edit_event *edit_first, *edit_last;
    bool edit_bindings_pending;
    uint64_t next_binding;
    cvar_edit_binding *edit_bindings;
    size_t edit_binding_count;
};
bool qac_cvars_touch(qa_cvars *, qa_error *);
void qac_cvars_entry_free(cvar *);
#endif
