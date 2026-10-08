#ifndef QA_CVARS_PRIVATE_H
#define QA_CVARS_PRIVATE_H
#include "internal.h"
#include "qa/console_cvar_observer.h"
#include "qa/console_cvars_prepare.h"
#include "qa/cvars_alias.h"
#include "cvar_catalog_generated.h"
#include "cvars_conversion.h"

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

typedef struct cvar cvar;
typedef struct cvar_alias cvar_alias;
typedef struct cvar_store cvar_store;
typedef struct cvar_edit_view cvar_edit_view;
typedef struct cvar_detail {
    struct cvar_detail *next;
    const qa_cvar_catalog_binding *binding;
    qa_console_dialect dialect;
    char *value, *latched_value;
} cvar_detail;
typedef struct cvar_name_node {
    struct cvar_name_node *next;
    const char *name;
    bool alias;
    uint8_t side_scope;
    union { cvar *entry; cvar_alias *alias; } owner;
} cvar_name_node;

struct cvar {
    /* Only the canonical store row owns scalar/default storage. Source rows
     * borrow it and retain their physical declarations/bindings/handles. */
    struct cvar *canonical;
    struct cvar *definition; /* same catalog definition for another actual player */
    qa_cvars *player_default_source; /* weak, retired with Source callbacks */
    uint32_t player;
    qa_console_dialect flags_dialect;
    size_t ordinal;
    uint16_t catalog_row;
    const qa_cvar_catalog_binding *catalog_binding;
    char *defaults[QA_CVAR_CATALOG_DIALECTS];
    char *declaration_default;
    cvar_detail *details;
    bool pending_explicit;
    qa_cvar_view projection;
    char projected_value[64], projected_reset[64], projected_latch[64];
    qa_cvar_view view;
    qa_cvar_binding binding;
    bool bound;
    uint64_t binding_order;
    struct cvar *next;
    cvar_name_node indexed_name;
};
struct cvar_alias {
    struct cvar_alias *next;
    struct cvar_alias *canonical;
    size_t ordinal, target_ordinal;
    char *name, *target, *description;
    const qa_console_documentation *documentation;
    const qa_cvar_catalog_binding *catalog_binding;
    qa_cvar_binding binding;
    bool bound, declared, console_created;
    uint64_t binding_order, owner, modification_count;
    bool modified;
    uint32_t flags;
    qa_console_dialect flags_dialect;
    size_t handle;
    bool vm_bound;
    qa_cvar_view projection;
    char value[64], reset[64], latched[64];
    cvar_name_node indexed_name;
};
typedef struct cvar_values {
    const struct cvar_values *canonical_values;
    cvar *first;
    cvar_alias *aliases, *last_alias;
    size_t alias_count;
    size_t count, declared_count, next_handle;
    /* The canonical owner alone owns name_buckets. Views address their
     * retained Source declarations by the resolved canonical/alias slot. */
    cvar **rows;
    cvar_alias **alias_rows;
    size_t row_capacity, alias_capacity;
    cvar_name_node **handles;
    size_t handle_capacity;
    uint32_t changed_flags;
    bool userinfo_modified, server_active, high_characters, cheats;
    cvar_name_node **name_buckets;
    size_t name_bucket_count;
} cvar_values;
struct cvar_store {
    cvar_values values;
    qa_console_dialect active_dialect;
    qa_cvars *active_default_source;
    qa_cvars *video_owner;
    qa_cvar_video_resolver video_resolver;
    void *video_user;
    qa_cvars *views, *last_view;
    uint64_t revision, next_binding;
    struct qa_cvars_edit *edit;
    size_t references;
};
struct cvar_edit_view {
    struct cvar_edit_view *next;
    qa_cvars *registry;
    cvar_values values;
    bool entered;
};
typedef struct cvar_edit_event cvar_edit_event;
typedef struct cvar_edit_binding {
    qa_cvars *registry;
    const cvar *actual;
    cvar *prepared;
    const cvar_alias *actual_alias;
    cvar_alias *prepared_alias;
} cvar_edit_binding;
struct qa_cvars_edit {
    qa_cvars *registry;
    cvar_values values; /* sole prepared canonical scalar snapshot */
    cvar_edit_view *views, *last_view;
    qa_console_dialect active_dialect;
    qa_cvars *active_default_source;
    size_t owner_scope_depth;
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
typedef struct cvar_projection_context {
    const qa_cvars *registry;
    const cvar_values *values;
    bool reset;
    bool latched;
} cvar_projection_context;
struct qa_cvars {
    qa_cvar_options options;
    bool canonical_root;
    cvar_store *store;
    struct qa_cvars *next_view;
    struct qa_cvars_edit *entered_edit, *candidate_edit;
    size_t edit_scope_depth;
    cvar_values values;
    uint64_t view_identity;
    size_t references;
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
void qac_cvars_alias_free(cvar_alias *);
cvar_alias *qac_cvars_alias_copy(const cvar_alias *, qa_error *);
bool qac_cvars_name_equal(const qa_cvars *, const char *, const char *);
cvar *qac_cvars_find_values(const qa_cvars *, const cvar_values *, const char *);
bool qac_cvars_index_reserve(const qa_cvars *, cvar_values *, size_t, qa_error *);
void qac_cvars_index_entry(const qa_cvars *, cvar_values *, cvar *);
void qac_cvars_index_alias(const qa_cvars *, cvar_values *, cvar_alias *);
void qac_cvars_values_free(cvar_values *);
qa_cvars *qac_cvars_store_create(const qa_cvar_options *, qa_error *);
qa_cvars_edit *qac_cvars_current_edit(const qa_cvars *);
cvar_values *qac_cvars_current_values(const qa_cvars *);
cvar *qac_cvars_canonical(cvar *);
void qac_cvars_refresh(qa_cvars *, cvar *);
const qa_cvar_view *qac_cvars_project(const qa_cvars *, cvar_values *, cvar *);
const qa_cvar_view *qac_cvars_values_find(const qa_cvars *, cvar_values *, const char *);
const qa_cvar_view *qac_cvars_values_at(const qa_cvars *, cvar_values *, size_t, bool aliases, bool whole_store);
const qa_cvar_view *qac_cvars_values_handle(const qa_cvars *, cvar_values *, size_t);
size_t qac_cvars_values_count(const qa_cvars *, const cvar_values *, bool aliases, bool whole_store);
const qa_cvar_catalog_conversion *qac_cvars_conversion(const qa_cvars *,
    const cvar_values *, const qa_cvar_catalog_binding *);
qa_cvar_options qac_cvars_view_options(const qa_cvars *, const cvar_values *);
cvar *qac_cvars_source_row(qa_cvars *, cvar_values *, cvar *, qa_error *);
cvar_alias *qac_cvars_source_alias(qa_cvars *, cvar_values *, cvar_alias *, qa_error *);
bool qac_cvars_view_add(qa_cvars *, const cvar_values *, cvar_values *, qa_error *);
bool qac_cvars_values_clone(qa_cvars *, const cvar_values *, cvar_values *,
    const cvar_values *, qa_error *);
cvar_edit_view *qac_cvars_edit_view(qa_cvars_edit *, const qa_cvars *);
bool qac_cvars_edit_add_view(qa_cvars_edit *, qa_cvars *, qa_error *);
bool qac_cvars_restore_row(qa_cvars_edit *, const char *, const char *, const char *, qa_console_dialect, bool, qa_error *);
bool qac_cvars_change_defaults(cvar_target, const char *const *, qa_error *);
bool qac_cvars_default_declare(cvar_target, cvar *, const char *, qa_error *);
bool qac_cvars_handles_reserve(cvar_values *, size_t, qa_error *);
bool qac_cvars_rows_reserve(cvar_values *, size_t, size_t, qa_error *);
uint32_t qac_cvars_flags(uint32_t, qa_console_dialect, qa_console_dialect);
uint32_t qac_cvars_catalog_flags(const qa_cvars *, const cvar_values *, uint16_t, const char *);
const char *qac_cvars_operand(void *, uint16_t);
bool qac_cvars_video(void *, const qa_cvar_video_query *, qa_cvar_video_mode *, qa_error *);
#endif
