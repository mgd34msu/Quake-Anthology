#include "cvars_private.h"
#include <stdlib.h>
#include <string.h>

static uint64_t next_view_identity;

bool qa_cvars_retain(qa_cvars *registry, qa_error *error)
{
    if (!registry || registry->references == SIZE_MAX)
        return qac_fail(error, QA_ERROR_ARGUMENT, "retain requires a live cvar view");
    ++registry->references; return true;
}

qa_cvar_side qa_cvars_side(const qa_cvars *registry)
{ return registry ? registry->options.side : QA_CVAR_SIDE_UNSPECIFIED; }
qa_cvar_role qa_cvars_role(const qa_cvars *registry)
{ return registry ? registry->options.role : QA_CVAR_ROLE_ENGINE; }

bool qa_cvars_player_at(const qa_cvars *registry, size_t index, uint32_t *seat)
{
    const cvar_values *values=qac_cvars_current_values(registry);
    if (!values || !seat) return false;
    if (values->canonical_values) values=values->canonical_values;
    for (size_t i=0;i<values->row_capacity;++i) {
        const cvar *entry=values->rows[i];
        if (!entry || !entry->view.player_scoped || strcmp(entry->view.name,"name")) continue;
        if (!index--) { *seat=entry->player; return true; }
    }
    return false;
}

size_t qa_cvars_player_count(const qa_cvars *registry)
{
    const cvar_values *values=qac_cvars_current_values(registry);
    if (!values) return 0;
    if (values->canonical_values) values=values->canonical_values;
    size_t count=0;
    for (size_t i=0;i<values->row_capacity;++i) {
        const cvar *entry=values->rows[i];
        if (entry && entry->view.player_scoped && !strcmp(entry->view.name,"name")) ++count;
    }
    return count;
}

uint64_t qa_cvars_view_identity(const qa_cvars *registry)
{ return registry ? registry->view_identity : 0; }

qa_cvars_edit *qac_cvars_current_edit(const qa_cvars *registry)
{
    if (!registry) return NULL;
    if (registry->candidate_edit) return registry->candidate_edit;
    if (registry->entered_edit) return registry->entered_edit;
    return registry->store && registry->store->edit && registry->store->edit->owner_scope_depth
        ? registry->store->edit : NULL;
}

cvar_values *qac_cvars_current_values(const qa_cvars *registry)
{
    if (!registry) return NULL;
    qa_cvars_edit *edit = qac_cvars_current_edit(registry);
    if (edit) {
        for (cvar_edit_view *view = edit->views; view; view = view->next)
            if (view->registry == registry) return &view->values;
        return NULL;
    }
    return &((qa_cvars *)registry)->values;
}

qa_cvars_edit *qa_cvars_prepared_edit(const qa_cvars *registry)
{ return registry && registry->store ? registry->store->edit : NULL; }

cvar *qac_cvars_canonical(cvar *entry)
{ return entry && entry->canonical ? entry->canonical : entry; }

const char *qac_cvars_operand(void *user, uint16_t row)
{
    const cvar_projection_context *context = user;
    const qa_cvars *registry = context->registry;
    const char *name = qa_cvar_catalog_string(qa_cvar_catalog_rows[row].name);
    cvar *entry = qac_cvars_find_values(registry, context->values, name);
    cvar *canonical = qac_cvars_canonical(entry);
    return canonical ? (context->reset?canonical->view.reset_value:
        context->latched && canonical->view.latched_value?canonical->view.latched_value:canonical->view.value) : "";
}

bool qac_cvars_video(void *user, const qa_cvar_video_query *query,
    qa_cvar_video_mode *mode, qa_error *error)
{
    const cvar_projection_context *context=user;
    cvar_store *store=context->registry->store;
    if (!store->video_resolver)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar conversion requires its Source-owned video policy");
    qa_cvars *owner=store->video_owner;
    ++owner->notifying;
    bool okay=store->video_resolver(store->video_user,query,mode,error);
    --owner->notifying;
    return okay;
}

bool qa_cvars_set_video_resolver(qa_cvars *registry, qa_cvar_video_resolver resolver,
    void *user, qa_error *error)
{
    if (!registry || registry->options.role!=QA_CVAR_ROLE_ENGINE)
        return qac_fail(error,QA_ERROR_ARGUMENT,"video mode policy requires its ENGINE view");
    if (!qac_cvars_touch(registry,error)) return false;
    registry->store->video_owner=resolver?registry:NULL;
    registry->store->video_resolver=resolver;
    registry->store->video_user=resolver?user:NULL;
    return true;
}

qa_cvar_options qac_cvars_view_options(const qa_cvars *registry,const cvar_values *values)
{
    qa_cvar_options options=registry->options;
    if (registry->canonical_root && options.role==QA_CVAR_ROLE_ENGINE) {
        const qa_cvars_edit *edit=registry->store->edit;
        options.dialect=edit && values && values->canonical_values==&edit->values
            ? edit->active_dialect:registry->store->active_dialect;
    }
    return options;
}
const qa_cvar_catalog_conversion *qac_cvars_conversion(const qa_cvars *registry,
    const cvar_values *values,const qa_cvar_catalog_binding *binding)
{
    if (!binding) return NULL;
    const qa_cvar_catalog_row *row=&qa_cvar_catalog_rows[binding->row_index];
    if (row->not_stored) return &qa_cvar_catalog_conversions[row->rule_conversion];
    if (binding->canonical && registry->options.role==QA_CVAR_ROLE_ENGINE) return NULL;
    qa_cvar_options options=qac_cvars_view_options(registry,values);
    const qa_cvar_catalog_conversion *conversion=&qa_cvar_catalog_conversions[binding->conversion[options.dialect]];
    if (conversion->role_scope==QA_CATALOG_CGAME && options.role!=QA_CVAR_ROLE_CGAME) return NULL;
    return conversion;
}

void qac_cvars_refresh(qa_cvars *registry, cvar *entry)
{
    if (!entry) return;
    cvar *canonical = qac_cvars_canonical(entry);
    if (entry == canonical) return;
    entry->view.value = canonical->view.value;
    entry->view.reset_value = canonical->view.reset_value;
    entry->view.latched_value = canonical->view.latched_value;
    entry->view.explicit_value = canonical->view.explicit_value;
    entry->view.save_policy = canonical->view.save_policy;
    entry->view.number = qac_number(entry->view.value, qa_cvars_dialect(registry));
    entry->view.integer = qac_integer(entry->view.value);
}

static const char *project_text(const qa_cvars *registry, cvar *entry,
    cvar_values *values, const char *value, const char *detail, char buffer[64],bool reset,bool latched)
{
    if (!value) return NULL;
    cvar_projection_context context = {.registry=registry,.values=values,.reset=reset,.latched=latched};
    qa_cvar_options options=qac_cvars_view_options(registry,values);
    qac_cvar_conversion_input input = {.options = &options,
        .conversion = qac_cvars_conversion(registry, values, entry->catalog_binding), .binding=entry->catalog_binding,
        .value = value, .current = value, .detail = detail,
        .user = &context, .operand = qac_cvars_operand, .video=qac_cvars_video};
    qac_cvar_conversion_output output;
    if (!qac_cvar_read_conversion(&input, &output, NULL)) return NULL;
    if (output.value != output.text) return output.value;
    memcpy(buffer, output.text, strlen(output.text) + 1);
    return buffer;
}

const qa_cvar_view *qac_cvars_project(const qa_cvars *registry, cvar_values *values,
    cvar *entry)
{
    qac_cvars_refresh((qa_cvars *)registry, entry);
    entry->projection = entry->view;
    const cvar *canonical=qac_cvars_canonical(entry);
    if (registry->options.role==QA_CVAR_ROLE_ENGINE)
        entry->projection.flags=qac_cvars_flags(entry->view.flags,entry->flags_dialect,
            qac_cvars_view_options(registry,values).dialect)|canonical->view.flags|
            qac_cvars_catalog_flags(registry,values,entry->catalog_row,entry->view.name);
    const cvar_detail *detail=canonical->details;
    while (detail && (detail->binding!=entry->catalog_binding || detail->dialect!=qac_cvars_view_options(registry,values).dialect)) detail=detail->next;
    entry->projection.value = project_text(registry, entry, values, entry->view.value,
        detail?detail->value:NULL, entry->projected_value,false,false);
    entry->projection.reset_value = project_text(registry, entry, values,
        entry->view.reset_value, NULL, entry->projected_reset,true,false);
    entry->projection.latched_value = project_text(registry, entry, values,
        entry->view.latched_value, detail?detail->latched_value:NULL, entry->projected_latch,false,true);
    if (entry->catalog_row!=QA_CVAR_CATALOG_NO_ROW && qa_cvar_catalog_rows[entry->catalog_row].not_stored) {
        const qa_cvar_catalog_conversion *conversion=qac_cvars_conversion(registry,values,entry->catalog_binding);
        for (size_t i=0;conversion && i<conversion->operand_count;++i) {
            uint16_t operand=qa_cvar_catalog_operands[conversion->operand_first+i].row_index;
            const char *name=qa_cvar_catalog_string(qa_cvar_catalog_rows[operand].name);
            const cvar *record=qac_cvars_canonical(qac_cvars_find_values(registry,values,name));
            entry->projection.explicit_value |= record && record->view.explicit_value;
        }
    }
    if (!entry->projection.value || !entry->projection.reset_value) return NULL;
    qa_cvar_options options=qac_cvars_view_options(registry,values);
    entry->projection.number = qac_number(entry->projection.value, options.dialect);
    entry->projection.integer = qac_integer(entry->projection.value);
    return &entry->projection;
}

bool qa_cvars_same_store(const qa_cvars *a, const qa_cvars *b)
{ return a && b && a->store == b->store; }

bool qa_cvars_is_set(const qa_cvars *registry, const char *name)
{
    const qa_cvar_view *value = qa_cvars_find(registry, name);
    return value && value->explicit_value;
}

bool qa_cvars_effective_view(const qa_cvars *registry, const char *name,
    qa_cvar_view *out, qa_error *error)
{
    const qa_cvar_view *value = qa_cvars_find(registry, name);
    if (!out || !value)
        return qac_fail(error, QA_ERROR_NOT_FOUND, "effective cvar view needs its actual declared name");
    *out = *value;
    if (out->latched_value) out->value = out->latched_value;
    qa_cvar_options options=qac_cvars_view_options(registry,qac_cvars_current_values(registry));
    out->number = qac_number(out->value, options.dialect);
    out->integer = qac_integer(out->value);
    return true;
}

static bool condition_matches(uint8_t condition, const qa_cvar_options *options)
{
    switch ((qa_cvar_catalog_condition)condition) {
    case QA_CATALOG_CONDITION_ALWAYS: return true;
#ifdef __APPLE__
    case QA_CATALOG_CONDITION_MAC: return true;
    case QA_CATALOG_CONDITION_NOT_MAC: return false;
#else
    case QA_CATALOG_CONDITION_MAC: return false;
    case QA_CATALOG_CONDITION_NOT_MAC: return true;
#endif
#ifdef __linux__
    case QA_CATALOG_CONDITION_LINUX: return true;
    case QA_CATALOG_CONDITION_NOT_LINUX: return false;
#else
    case QA_CATALOG_CONDITION_LINUX: return false;
    case QA_CATALOG_CONDITION_NOT_LINUX: return true;
#endif
#ifdef _WIN32
    case QA_CATALOG_CONDITION_WINDOWS: return true;
    case QA_CATALOG_CONDITION_NOT_WINDOWS: return false;
#else
    case QA_CATALOG_CONDITION_WINDOWS: return false;
    case QA_CATALOG_CONDITION_NOT_WINDOWS: return true;
#endif
    case QA_CATALOG_CONDITION_ENGINE: return options->role == QA_CVAR_ROLE_ENGINE;
    case QA_CATALOG_CONDITION_GAME: return options->role == QA_CVAR_ROLE_GAME;
    case QA_CATALOG_CONDITION_CGAME: return options->role == QA_CVAR_ROLE_CGAME;
    case QA_CATALOG_CONDITION_CLIENT: return options->side == QA_CVAR_SIDE_CLIENT;
    case QA_CATALOG_CONDITION_DEDICATED:
    case QA_CATALOG_CONDITION_UNRESOLVED: return false;
    }
    return false;
}

static const qa_cvar_catalog_binding *catalog_name(const char *name, qa_cvar_side side)
{
    size_t lower = 0, upper = qa_cvar_catalog_binding_count;
    while (lower < upper) {
        size_t middle = lower + (upper - lower) / 2;
        const char *candidate = qa_cvar_catalog_string(qa_cvar_catalog_bindings[middle].name);
        const unsigned char *a = (const unsigned char *)name, *b = (const unsigned char *)candidate;
        int order = 0;
        while (*a || *b) {
            unsigned char aa = *a, bb = *b;
            if (aa >= 'A' && aa <= 'Z') aa = (unsigned char)(aa + ('a' - 'A'));
            if (bb >= 'A' && bb <= 'Z') bb = (unsigned char)(bb + ('a' - 'A'));
            if (aa != bb) { order = aa < bb ? -1 : 1; break; }
            if (*a) ++a;
            if (*b) ++b;
        }
        if (order > 0) lower = middle + 1;
        else upper = middle;
    }
    const qa_cvar_catalog_binding *fallback = NULL;
    for (size_t i = lower; i < qa_cvar_catalog_binding_count; ++i) {
        const qa_cvar_catalog_binding *binding = &qa_cvar_catalog_bindings[i];
        if (!qac_equal(name, qa_cvar_catalog_string(binding->name))) break;
        if (binding->side_scope == QA_CATALOG_ANY_SIDE) fallback = binding;
        else if ((binding->side_scope == QA_CATALOG_SERVER && side == QA_CVAR_SIDE_SERVER) ||
                 (binding->side_scope == QA_CATALOG_CLIENT && side != QA_CVAR_SIDE_SERVER)) return binding;
    }
    return fallback;
}

static qa_cvar_role stock_default_role(const cvar *canonical,qa_console_dialect dialect)
{
    if (canonical->catalog_row==QA_CVAR_CATALOG_NO_ROW) return QA_CVAR_ROLE_GAME;
    const qa_cvar_catalog_dialect *native=&qa_cvar_catalog_rows[canonical->catalog_row].dialect[dialect];
    qa_cvar_role role=QA_CVAR_ROLE_GAME;
    for (size_t i=0;i<native->default_count;++i) {
        const qa_cvar_catalog_default *clause=&qa_cvar_catalog_defaults[native->default_first+i];
        if (clause->issues) continue;
        if (clause->condition_kind==QA_CATALOG_CONDITION_ENGINE) role=QA_CVAR_ROLE_ENGINE;
        if (clause->condition_kind==QA_CATALOG_CONDITION_CGAME || clause->condition_kind==QA_CATALOG_CONDITION_ALWAYS) {
            const qa_cvar_catalog_binding *binding=catalog_name(qa_cvar_catalog_string(clause->member),QA_CVAR_SIDE_CLIENT);
            const qa_cvar_catalog_conversion *conversion=binding?&qa_cvar_catalog_conversions[binding->conversion[dialect]]:NULL;
            if (conversion && conversion->role_scope==QA_CATALOG_CGAME && conversion->kind==QA_CATALOG_RECIPROCAL) return QA_CVAR_ROLE_CGAME;
        }
    }
    return role;
}
static char *catalog_default(const qa_cvars *registry,const cvar *canonical,
    qa_console_dialect dialect,qa_error *error)
{
    const qa_cvar_catalog_row *row=&qa_cvar_catalog_rows[canonical->catalog_row];
    const qa_cvar_catalog_dialect *native=&row->dialect[dialect];
    qa_cvar_options options=registry->options;
    options.dialect=dialect; options.role=stock_default_role(canonical,dialect);
    char *selected=NULL;
    for (size_t i=0;i<native->default_count;++i) {
        const qa_cvar_catalog_default *candidate=&qa_cvar_catalog_defaults[native->default_first+i];
        if (candidate->issues || !condition_matches(candidate->condition_kind,&options)) continue;
        const char *value=qa_cvar_catalog_string(candidate->value);
        const char *member=qa_cvar_catalog_string(candidate->member);
        const qa_cvar_catalog_binding *binding=catalog_name(*member?member:canonical->view.name,options.side);
        const qa_cvar_catalog_conversion *conversion=binding?&qa_cvar_catalog_conversions[binding->conversion[dialect]]:NULL;
        cvar_projection_context context={.registry=registry,.values=&registry->store->values};
        bool joined_default=conversion && (conversion->operation==QA_CATALOG_OP_DEATHMATCH ||
            conversion->operation==QA_CATALOG_OP_COOP || conversion->operation==QA_CATALOG_OP_TEAMPLAY);
        qac_cvar_conversion_input input={.options=&options,.conversion=conversion,.binding=binding,
            .value=value,.current=joined_default && selected?selected:value,.user=&context,.operand=qac_cvars_operand,.video=qac_cvars_video};
        qac_cvar_conversion_output output; qa_error reason={0};
        if (!qac_cvar_write_conversion(&input,&output,&reason)) continue;
        if (selected && strcmp(selected,output.value)) {
            if (!joined_default) { free(output.allocated_value); free(selected); return NULL; }
            free(selected); selected=NULL;
        }
        if (!selected) selected=qac_copy(output.value,error);
        free(output.allocated_value);
        if (!selected) return NULL;
    }
    /* Ambiguous/unresolved clauses are supplied by the admitted Source
     * definition, never selected arbitrarily from competing registrations. */
    return selected;
}

uint32_t qac_cvars_flags(uint32_t flags, qa_console_dialect from, qa_console_dialect to)
{
    if (qac_q2(from)==qac_q2(to)) return flags;
    uint32_t normalized=flags&(QA_CVAR_ARCHIVE|QA_CVAR_USERINFO|QA_CVAR_SERVERINFO);
    if (qac_q2(from)) {
        if (flags&QA_Q2_CVAR_NOSET) normalized|=QA_CVAR_INIT;
        if (flags&QA_Q2_CVAR_LATCH) normalized|=QA_CVAR_LATCH;
        if (flags&QA_Q2_CVAR_CHEAT) normalized|=QA_CVAR_CHEAT;
        if (flags&QA_Q2_CVAR_READONLY) normalized|=QA_CVAR_READONLY;
        if (flags&QA_Q2_CVAR_CUSTOM) normalized|=QA_CVAR_USER_CREATED;
    } else {
        if (flags&QA_CVAR_INIT) normalized|=QA_Q2_CVAR_NOSET;
        if (flags&QA_CVAR_LATCH) normalized|=QA_Q2_CVAR_LATCH;
        if (flags&QA_CVAR_CHEAT) normalized|=QA_Q2_CVAR_CHEAT;
        if (flags&QA_CVAR_READONLY) normalized|=QA_Q2_CVAR_READONLY;
        if (flags&QA_CVAR_USER_CREATED) normalized|=QA_Q2_CVAR_CUSTOM;
    }
    return normalized;
}

uint32_t qac_cvars_catalog_flags(const qa_cvars *registry, const cvar_values *values, uint16_t index,
    const char *member)
{
    if (index == QA_CVAR_CATALOG_NO_ROW) return 0;
    const qa_cvar_catalog_row *row = &qa_cvar_catalog_rows[index];
    qa_console_dialect active=qac_cvars_view_options(registry,values).dialect;
    const qa_cvar_catalog_dialect *dialect = &row->dialect[active];
    uint32_t flags = row->archive_flags;
    for (size_t i = 0; i < dialect->flags_count; ++i) {
        const qa_cvar_catalog_flags *clause = &qa_cvar_catalog_flag_clauses[dialect->flags_first + i];
        const char *name = qa_cvar_catalog_string(clause->member);
        if (!clause->issues && (!*name || qac_equal(member, name) || qac_equal(member,qa_cvar_catalog_string(row->name)))) flags |= clause->flags;
    }
    if (row->policies & QA_CATALOG_POLICY_LATCH_ALL)
        flags |= qac_q2(active) ? (uint32_t)QA_Q2_CVAR_LATCH : (uint32_t)QA_CVAR_LATCH;
    if ((row->policies & QA_CATALOG_POLICY_PRIVATE) && qac_q2(active))
        flags |= QA_Q2_CVAR_PRIVATE;
    return flags;
}

bool qac_cvars_handles_reserve(cvar_values *values,size_t count,qa_error *error)
{
    if (count<=values->handle_capacity) return true;
    if (count>SIZE_MAX/sizeof(*values->handles)) return qac_fail(error,QA_ERROR_MEMORY,"Source cvar handles exceed address space");
    size_t capacity=values->handle_capacity?values->handle_capacity:16;
    while (capacity<count) { if (capacity>SIZE_MAX/2) { capacity=count; break; } capacity*=2; }
    if (capacity>SIZE_MAX/sizeof(*values->handles)) capacity=count;
    cvar_name_node **handles=realloc(values->handles,capacity*sizeof(*handles));
    if (!handles) return qac_fail(error,QA_ERROR_MEMORY,"retaining actual Source cvar handles");
    memset(handles+values->handle_capacity,0,(capacity-values->handle_capacity)*sizeof(*handles));
    values->handles=handles; values->handle_capacity=capacity; return true;
}

bool qac_cvars_rows_reserve(cvar_values *values, size_t rows, size_t aliases,
    qa_error *error)
{
    if (rows > SIZE_MAX / sizeof(*values->rows) || aliases > SIZE_MAX / sizeof(*values->alias_rows))
        return qac_fail(error, QA_ERROR_MEMORY, "cvar slots exceed address space");
    if (rows > values->row_capacity) {
        cvar **next = realloc(values->rows, rows * sizeof(*next));
        if (!next) return qac_fail(error, QA_ERROR_MEMORY, "allocating cvar slots");
        memset(next + values->row_capacity, 0, (rows - values->row_capacity) * sizeof(*next));
        values->rows = next; values->row_capacity = rows;
    }
    if (aliases > values->alias_capacity) {
        cvar_alias **next = realloc(values->alias_rows, aliases * sizeof(*next));
        if (!next) return qac_fail(error, QA_ERROR_MEMORY, "allocating cvar alias slots");
        memset(next + values->alias_capacity, 0, (aliases - values->alias_capacity) * sizeof(*next));
        values->alias_rows = next; values->alias_capacity = aliases;
    }
    return true;
}

static cvar *source_row(qa_cvars *registry, const cvar_values *values, cvar *canonical, const cvar *source,
    qa_error *error)
{
    cvar *entry = calloc(1, sizeof(*entry));
    if (!entry) { qac_fail(error, QA_ERROR_MEMORY, "allocating Source cvar declaration"); return NULL; }
    if (source) *entry = *source;
    entry->canonical = canonical;
    entry->next = NULL;
    entry->ordinal = canonical->ordinal;
    entry->catalog_row = canonical->catalog_row;
    entry->catalog_binding = canonical->catalog_binding;
    entry->player=canonical->player;
    entry->view.player_scoped=canonical->view.player_scoped;
    memset(entry->defaults, 0, sizeof(entry->defaults)); entry->details=NULL;
    entry->declaration_default = source && source->declaration_default
        ? qac_copy(source->declaration_default, error) : NULL;
    entry->view.name = canonical->view.name;
    entry->view.description = qac_copy(source ? source->view.description : "", error);
    entry->view.documentation = NULL;
    if (!entry->view.description || (source && source->declaration_default && !entry->declaration_default) ||
        !qac_document_replace(&entry->view.documentation, source ? source->view.documentation : NULL, error)) {
        qac_cvars_entry_free(entry); return NULL;
    }
    if (!source) {
        entry->view.handle = SIZE_MAX;
        entry->flags_dialect=qac_cvars_view_options(registry,values).dialect;
        entry->view.flags = qac_cvars_catalog_flags(registry,values,canonical->catalog_row,canonical->view.name);
        entry->view.save_policy = canonical->view.save_policy;
    }
    qac_cvars_refresh(registry, entry);
    return entry;
}

cvar *qac_cvars_source_row(qa_cvars *registry, cvar_values *values,
    cvar *canonical, qa_error *error)
{
    if (!canonical || canonical->ordinal >= values->row_capacity) return NULL;
    cvar *entry = values->rows[canonical->ordinal];
    if (entry) return entry;
    entry = source_row(registry, values, canonical, NULL, error);
    if (!entry) return NULL;
    entry->next = values->first; values->first = entry; ++values->count;
    qac_cvars_index_entry(registry, values, entry);
    return entry;
}

cvar_alias *qac_cvars_source_alias(qa_cvars *registry, cvar_values *values,
    cvar_alias *canonical, qa_error *error)
{
    if (!canonical || canonical->ordinal >= values->alias_capacity) return NULL;
    cvar_alias *alias = values->alias_rows[canonical->ordinal];
    if (alias) return alias;
    alias = qac_cvars_alias_copy(canonical, error);
    if (!alias) return NULL;
    alias->canonical = canonical;
    free(alias->name); free(alias->target);
    alias->name = canonical->name; alias->target = canonical->target;
    alias->handle = SIZE_MAX;
    alias->flags_dialect=qac_cvars_view_options(registry,values).dialect;
    alias->flags = qac_cvars_catalog_flags(registry,values,
        canonical->catalog_binding ? canonical->catalog_binding->row_index : QA_CVAR_CATALOG_NO_ROW,
        canonical->name);
    if (values->last_alias) values->last_alias->next = alias;
    else values->aliases = alias;
    values->last_alias = alias; ++values->alias_count;
    qac_cvars_index_alias(registry, values, alias);
    return alias;
}

bool qac_cvars_view_add(qa_cvars *registry, const cvar_values *canonical_values,
    cvar_values *values, qa_error *error)
{
    (void)registry;
    values->canonical_values = canonical_values;
    return qac_cvars_rows_reserve(values, canonical_values->row_capacity,
        canonical_values->alias_capacity, error);
}

bool qac_cvars_values_clone(qa_cvars *registry, const cvar_values *source,
    cvar_values *out, const cvar_values *canonical_values, qa_error *error)
{
    *out = (cvar_values){.canonical_values = canonical_values, .changed_flags = source->changed_flags,
        .userinfo_modified = source->userinfo_modified, .server_active = source->server_active,
        .high_characters = source->high_characters, .cheats = source->cheats,
        .next_handle = source->next_handle, .declared_count = source->declared_count};
    if (!qac_cvars_rows_reserve(out, canonical_values?canonical_values->row_capacity:source->row_capacity,
        canonical_values?canonical_values->alias_capacity:source->alias_capacity, error) ||
        (canonical_values && !qac_cvars_handles_reserve(out,source->next_handle,error))) goto failed;
    if (!canonical_values && !qac_cvars_index_reserve(registry, out, source->count + source->alias_count, error)) goto failed;
    cvar **tail = &out->first;
    for (const cvar *entry = source->first; entry; entry = entry->next) {
        cvar *copy;
        if (canonical_values) {
            copy = source_row(registry, out, canonical_values->rows[entry->ordinal], entry, error);
        } else {
            copy = calloc(1, sizeof(*copy));
            if (!copy) { qac_fail(error, QA_ERROR_MEMORY, "copying canonical cvar state"); goto failed; }
            *copy = *entry;
            copy->next = NULL; copy->canonical = NULL;
            copy->view.name = qac_copy(entry->view.name, error);
            copy->view.value = qac_copy(entry->view.value, error);
            copy->view.reset_value = qac_copy(entry->view.reset_value, error);
            copy->view.latched_value = entry->view.latched_value ? qac_copy(entry->view.latched_value, error) : NULL;
            copy->view.description = qac_copy(entry->view.description, error);
            copy->view.documentation = NULL; copy->declaration_default = NULL;
            memset(copy->defaults, 0, sizeof(copy->defaults)); copy->details=NULL;
            cvar_detail **detail_tail=&copy->details;
            for (const cvar_detail *detail=entry->details;detail;detail=detail->next) {
                cvar_detail *next=calloc(1,sizeof(*next));
                if (!next) { qac_cvars_entry_free(copy); qac_fail(error,QA_ERROR_MEMORY,"copying canonical enum detail"); goto failed; }
                next->binding=detail->binding; next->dialect=detail->dialect;
                next->value=detail->value?qac_copy(detail->value,error):NULL;
                next->latched_value=detail->latched_value?qac_copy(detail->latched_value,error):NULL;
                *detail_tail=next; detail_tail=&next->next;
                if ((detail->value && !next->value) || (detail->latched_value && !next->latched_value)) { qac_cvars_entry_free(copy); goto failed; }
            }
            bool okay = copy->view.name && copy->view.value && copy->view.reset_value && copy->view.description &&
                (!entry->view.latched_value || copy->view.latched_value) &&
                qac_document_replace(&copy->view.documentation, entry->view.documentation, error);
            if (!entry->definition)
                for (size_t d = 0; okay && d < QA_CVAR_CATALOG_DIALECTS; ++d)
                    if (entry->defaults[d] && !(copy->defaults[d] = qac_copy(entry->defaults[d], error))) okay = false;
            if (!okay) { qac_cvars_entry_free(copy); goto failed; }
        }
        if (!copy) goto failed;
        *tail = copy; tail = &copy->next; ++out->count;
        qac_cvars_index_entry(registry, out, copy);
        out->rows[copy->ordinal] = copy;
        if (canonical_values && copy->view.handle!=SIZE_MAX) out->handles[copy->view.handle]=&copy->indexed_name;
    }
    if (!canonical_values) {
        for (cvar *entry=out->first;entry;entry=entry->next) if (entry->definition) {
            entry->definition=out->rows[entry->definition->ordinal];
            memcpy(entry->defaults,entry->definition->defaults,sizeof(entry->defaults));
        }
    }
    for (const cvar_alias *alias = source->aliases; alias; alias = alias->next) {
        cvar_alias *copy = qac_cvars_alias_copy(alias, error);
        if (!copy) goto failed;
        if (canonical_values) {
            copy->canonical = canonical_values->alias_rows[alias->ordinal];
            free(copy->name); free(copy->target);
            copy->name = copy->canonical->name; copy->target = copy->canonical->target;
        } else copy->canonical = NULL;
        if (out->last_alias) out->last_alias->next = copy;
        else out->aliases = copy;
        out->last_alias = copy; ++out->alias_count;
        qac_cvars_index_alias(registry, out, copy);
        out->alias_rows[copy->ordinal] = copy;
        if (canonical_values && copy->vm_bound) out->handles[copy->handle]=&copy->indexed_name;
    }
    return true;
failed:
    qac_cvars_values_free(out); return false;
}

cvar_edit_view *qac_cvars_edit_view(qa_cvars_edit *edit, const qa_cvars *registry)
{
    for (cvar_edit_view *view = edit ? edit->views : NULL; view; view = view->next)
        if (view->registry == registry) return view;
    return NULL;
}

bool qac_cvars_edit_add_view(qa_cvars_edit *edit, qa_cvars *registry, qa_error *error)
{
    if (qac_cvars_edit_view(edit, registry)) return true;
    cvar_edit_view *view = calloc(1, sizeof(*view));
    if (!view) return qac_fail(error, QA_ERROR_MEMORY, "retaining prepared Source declarations");
    view->registry = registry;
    if (!qac_cvars_values_clone(registry, &registry->values, &view->values, &edit->values, error)) {
        free(view); return false;
    }
    if (edit->last_view) edit->last_view->next = view;
    else edit->views = view;
    edit->last_view = view;
    return true;
}

bool qac_cvars_default_declare(cvar_target target, cvar *entry,
    const char *canonical_default, qa_error *error)
{
    char *definition=qac_copy(canonical_default,error);
    if (!definition) return false;
    qa_cvars *authority=target.edit?target.edit->active_default_source:target.registry->store->active_default_source;
    cvar *canonical=qac_cvars_canonical(entry);
    qa_console_dialect active=target.edit?target.edit->active_dialect:target.registry->store->active_dialect;
    bool player_authority=canonical->view.player_scoped && target.registry->options.side==QA_CVAR_SIDE_CLIENT &&
        (!canonical->player_default_source || canonical->player_default_source==target.registry ||
         target.registry->options.role==QA_CVAR_ROLE_ENGINE);
    bool engine_default=target.registry->options.role==QA_CVAR_ROLE_ENGINE && !canonical->defaults[active];
    bool apply=engine_default || (player_authority && (target.registry->options.dialect==active ||
        (target.registry->options.role==QA_CVAR_ROLE_ENGINE && qac_equal(canonical->view.name,"name")))) ||
        (authority==target.registry &&
        (stock_default_role(canonical,target.registry->options.dialect)==QA_CVAR_ROLE_GAME ||
         !canonical->defaults[target.registry->options.dialect]));
    if (apply) {
        cvar_values *values=target.edit?&target.edit->values:&target.registry->store->values;
        const char **defaults=calloc(values->row_capacity,sizeof(*defaults));
        if (!defaults) { free(definition); return qac_fail(error,QA_ERROR_MEMORY,"admitting Source cvar default"); }
        defaults[canonical->ordinal]=definition;
        bool okay=qac_cvars_change_defaults(target,defaults,error);
        free(defaults);
        if (!okay) { free(definition); return false; }
    }
    if (player_authority) canonical->player_default_source=target.registry;
    free(entry->declaration_default); entry->declaration_default=definition;
    return true;
}

bool qa_cvars_select_dialect(qa_cvars *registry, qa_console_dialect dialect,
    qa_error *error)
{
    if (!registry || !qac_dialect_valid(dialect) || !qac_cvars_touch(registry, error)) return false;
    qa_cvars_edit *edit=qac_cvars_current_edit(registry);
    cvar_values *canonical=edit?&edit->values:&registry->store->values;
    cvar_values *values=qac_cvars_current_values(registry);
    const char **defaults=canonical->row_capacity?calloc(canonical->row_capacity,sizeof(*defaults)):NULL;
    if (canonical->row_capacity && !defaults) return qac_fail(error,QA_ERROR_MEMORY,"selecting active cvar defaults");
    qa_cvars *source=registry->options.role==QA_CVAR_ROLE_GAME?registry:NULL;
    for (cvar *entry=canonical->first;entry;entry=entry->next) {
        cvar *declaration=entry->ordinal<values->row_capacity?values->rows[entry->ordinal]:NULL;
        defaults[entry->ordinal]=source && declaration && declaration->declaration_default && stock_default_role(entry,dialect)==QA_CVAR_ROLE_GAME
            ? declaration->declaration_default:entry->defaults[dialect];
        qa_cvars *player_source=entry->player_default_source;
        if (entry->view.player_scoped && player_source && (player_source->options.dialect==dialect ||
            (player_source->options.role==QA_CVAR_ROLE_ENGINE && qac_equal(entry->view.name,"name")))) {
            cvar_values *player_values=edit?&qac_cvars_edit_view(edit,player_source)->values:&player_source->values;
            const cvar *player_default=entry->ordinal<player_values->row_capacity?player_values->rows[entry->ordinal]:NULL;
            if (player_default && player_default->declaration_default) defaults[entry->ordinal]=player_default->declaration_default;
        }
    }
    qa_console_dialect previous=edit?edit->active_dialect:registry->store->active_dialect;
    qa_cvars *authority=edit?edit->active_default_source:registry->store->active_default_source;
    if (edit) { edit->active_dialect=dialect; edit->active_default_source=source; }
    else { registry->store->active_dialect=dialect; registry->store->active_default_source=source; }
    bool okay=qac_cvars_change_defaults((cvar_target){registry,values,edit},defaults,error);
    free(defaults);
    if (!okay) {
        if (edit) { edit->active_dialect=previous; edit->active_default_source=authority; }
        else { registry->store->active_dialect=previous; registry->store->active_default_source=authority; }
    }
    return okay;
}

static bool catalog_composite_defaults(qa_cvars *registry,qa_error *error)
{
    cvar_values *values=&registry->store->values;
    for (const cvar *entry=values->first;entry;entry=entry->next) {
        const qa_cvar_catalog_row *row=&qa_cvar_catalog_rows[entry->catalog_row];
        if (row->not_stored) continue;
        for (size_t d=0;d<QA_CVAR_CATALOG_DIALECTS;++d) {
            qa_cvar_options options=registry->options; options.dialect=(qa_console_dialect)d;
            options.role=stock_default_role(entry,options.dialect);
            const qa_cvar_catalog_dialect *native=&row->dialect[d];
            for (size_t i=0;i<native->default_count;++i) {
                const qa_cvar_catalog_default *clause=&qa_cvar_catalog_defaults[native->default_first+i];
                if (clause->issues || clause->kind!=QA_CATALOG_NATIVE_DEFAULT || !condition_matches(clause->condition_kind,&options)) continue;
                const char *member=qa_cvar_catalog_string(clause->member);
                const qa_cvar_catalog_binding *binding=catalog_name(*member?member:entry->view.name,options.side);
                if (!binding) continue;
                const qa_cvar_catalog_conversion *conversion=&qa_cvar_catalog_conversions[binding->conversion[d]];
                if (conversion->kind!=QA_CATALOG_COMPOSITE || !conversion->operand_count) continue;
                cvar_projection_context context={.registry=registry,.values=values};
                const char *value=qa_cvar_catalog_string(clause->value);
                qac_cvar_conversion_input input={.options=&options,.conversion=conversion,.binding=binding,
                    .value=value,.current=value,.user=&context,.operand=qac_cvars_operand,.video=qac_cvars_video};
                qac_cvar_conversion_output output;
                if (!qac_cvar_write_conversion(&input,&output,error)) return false;
                for (size_t change=0;change<output.change_count;++change) {
                    const qac_cvar_change *contribution=&output.changes[change];
                    cvar *operand=NULL;
                    for (cvar *candidate=values->first;candidate;candidate=candidate->next)
                        if (candidate->catalog_row==contribution->row && !candidate->definition) { operand=candidate; break; }
                    if (!operand) { free(output.allocated_value); return qac_fail(error,QA_ERROR_FORMAT,"catalog default lost its composite operand"); }
                    const qa_cvar_catalog_dialect *own=&qa_cvar_catalog_rows[operand->catalog_row].dialect[d];
                    bool native_operand=false;
                    for (size_t clause_index=0;clause_index<own->default_count;++clause_index)
                        native_operand |= qa_cvar_catalog_defaults[own->default_first+clause_index].kind==QA_CATALOG_NATIVE_DEFAULT;
                    if (native_operand) continue;
                    char *resolved=qac_copy(contribution->value,error);
                    if (!resolved) { free(output.allocated_value); return false; }
                    free(operand->defaults[d]); operand->defaults[d]=resolved;
                }
                free(output.allocated_value);
            }
        }
    }
    for (cvar *entry=values->first;entry;entry=entry->next) {
        const char *initial=entry->defaults[registry->store->active_dialect];
        if (!initial) initial="";
        char *value=qac_copy(initial,error),*reset=qac_copy(initial,error);
        if (!value || !reset) { free(value); free(reset); return false; }
        free((char *)entry->view.value); free((char *)entry->view.reset_value);
        entry->view.value=value; entry->view.reset_value=reset;
    }
    return true;
}

static bool player_definition(const qa_cvar_catalog_binding *binding)
{
    static const char *const names[]={"name","model","headmodel","team_model",
        "team_headmodel","sex","color1","color2","team","handicap",
        "teamtask","spectator","password","hand"};
    const qa_cvar_catalog_row *row=&qa_cvar_catalog_rows[binding->row_index];
    if (!qac_equal(qa_cvar_catalog_string(row->owner),"client")) return false;
    bool identity=false, userinfo=false;
    for (size_t i=0;i<sizeof(names)/sizeof(names[0]);++i)
        if (qac_equal(qa_cvar_catalog_string(binding->name),names[i])) { identity=true; break; }
    for (size_t d=0;identity && d<QA_CVAR_CATALOG_DIALECTS;++d)
        for (size_t i=0;i<row->dialect[d].flags_count;++i)
            userinfo |= (qa_cvar_catalog_flag_clauses[row->dialect[d].flags_first+i].flags&QA_CVAR_USERINFO)!=0;
    return identity && userinfo;
}

/* The catalog definition remains singular. Only actual player identity/state
 * receives another scalar record, in the same canonical table. */
static bool admit_player(qa_cvars *registry,cvar_values *values,uint32_t player,
    qa_console_dialect dialect,qa_error *error)
{
    if (!player) return true;
    for (const cvar *entry=values->first;entry;entry=entry->next)
        if (entry->view.player_scoped && entry->player==player) return true;
    cvar *first=NULL, **tail=&first;
    size_t count=0;
    for (cvar *definition=values->first;definition;definition=definition->next) {
        if (!definition->view.player_scoped || definition->definition) continue;
        cvar *entry=calloc(1,sizeof(*entry));
        if (!entry) { qac_fail(error,QA_ERROR_MEMORY,"allocating actual player cvar record"); goto failed; }
        entry->definition=definition; entry->player=player;
        entry->catalog_row=definition->catalog_row; entry->catalog_binding=definition->catalog_binding;
        entry->ordinal=values->count+count;
        memcpy(entry->defaults,definition->defaults,sizeof(entry->defaults));
        const char *initial=entry->defaults[dialect]?entry->defaults[dialect]:"";
        entry->view=(qa_cvar_view){.name=qac_copy(definition->view.name,error),
            .value=qac_copy(initial,error),.reset_value=qac_copy(initial,error),
            .description=qac_copy("",error),.flags=definition->view.flags,
            .save_policy=QA_CVAR_SAVE_SETTING,.player_scoped=true,.handle=SIZE_MAX};
        *tail=entry; tail=&entry->next; ++count;
        if (!entry->view.name || !entry->view.value || !entry->view.reset_value || !entry->view.description) goto failed;
    }
    if (count>SIZE_MAX-values->count || !qac_cvars_index_reserve(registry,values,values->count+count+values->alias_count,error) ||
        !qac_cvars_rows_reserve(values,values->count+count,values->alias_count,error)) goto failed;
    for (qa_cvars *view=registry->store->views;view;view=view->next_view) {
        if (!qac_cvars_rows_reserve(&view->values,values->row_capacity,values->alias_capacity,error)) goto failed;
        cvar_edit_view *prepared=qac_cvars_edit_view(registry->store->edit,view);
        if (prepared && !qac_cvars_rows_reserve(&prepared->values,values->row_capacity,values->alias_capacity,error)) goto failed;
    }
    cvar **end=&values->first;
    while (*end) end=&(*end)->next;
    *end=first;
    for (cvar *entry=first;entry;entry=entry->next) qac_cvars_index_entry(registry,values,entry);
    values->count+=count;
    return true;
failed:
    while (first) { cvar *entry=first; first=entry->next; qac_cvars_entry_free(entry); }
    return false;
}

qa_cvars *qac_cvars_store_create(const qa_cvar_options *options, qa_error *error)
{
    qa_cvars *registry = calloc(1, sizeof(*registry));
    cvar_store *store = calloc(1, sizeof(*store));
    if (!registry || !store) {
        free(registry); free(store);
        qac_fail(error, QA_ERROR_MEMORY, "allocating canonical cvar owner"); return NULL;
    }
    if (next_view_identity == UINT64_MAX) {
        free(registry); free(store);
        qac_fail(error, QA_ERROR_MEMORY, "cvar view identities exhausted"); return NULL;
    }
    registry->view_identity = ++next_view_identity; registry->references = 1;
    registry->options = *options; registry->canonical_root=true;
    registry->store = store; registry->values.cheats = true;
    store->active_dialect = options->dialect; store->references = 1;
    store->views = store->last_view = registry;
    if (!qac_cvars_index_reserve(registry, &store->values, qa_cvar_catalog_binding_count, error) ||
        !qac_cvars_rows_reserve(&store->values, qa_cvar_catalog_binding_count, qa_cvar_catalog_binding_count, error)) goto failed;
    cvar **tail = &store->values.first;
    for (size_t i = 0; i < qa_cvar_catalog_binding_count; ++i) {
        const qa_cvar_catalog_binding *binding = &qa_cvar_catalog_bindings[i];
        if (!binding->canonical) continue;
        cvar *entry = calloc(1, sizeof(*entry));
        if (!entry) { qac_fail(error, QA_ERROR_MEMORY, "allocating canonical cvar row"); goto failed; }
        entry->catalog_row = binding->row_index; entry->catalog_binding = binding;
        entry->ordinal = store->values.count;
        entry->view.name = qac_copy(qa_cvar_catalog_string(binding->name), error);
        entry->view.description = qac_copy("", error); entry->view.handle = SIZE_MAX;
        entry->view.flags = qa_cvar_catalog_rows[binding->row_index].archive_flags;
        entry->view.player_scoped=player_definition(binding);
        if (entry->view.player_scoped) entry->view.save_policy=QA_CVAR_SAVE_SETTING;
        if (!entry->view.name || !entry->view.description) { qac_cvars_entry_free(entry); goto failed; }
        for (size_t d = 0; d < QA_CVAR_CATALOG_DIALECTS; ++d)
            entry->defaults[d] = catalog_default(registry, entry, (qa_console_dialect)d, error);
        const char *initial = entry->defaults[options->dialect];
        if (!initial) initial = "";
        entry->view.reset_value = qac_copy(initial, error); entry->view.value = qac_copy(initial, error);
        if (!entry->view.value || !entry->view.reset_value) { qac_cvars_entry_free(entry); goto failed; }
        *tail = entry; tail = &entry->next; ++store->values.count;
        qac_cvars_index_entry(registry, &store->values, entry);
        store->values.rows[entry->ordinal] = entry;
    }
    if (!catalog_composite_defaults(registry,error)) goto failed;
    for (size_t i = 0; i < qa_cvar_catalog_binding_count; ++i) {
        const qa_cvar_catalog_binding *binding = &qa_cvar_catalog_bindings[i];
        if (binding->canonical) continue;
        cvar *target = NULL;
        for (cvar *entry = store->values.first; entry; entry = entry->next)
            if (entry->catalog_row == binding->row_index && entry->catalog_binding->seat == binding->seat) { target = entry; break; }
        if (!target) { qac_fail(error, QA_ERROR_FORMAT, "catalog alias lost its canonical row"); goto failed; }
        cvar_alias prototype = {.name = (char *)qa_cvar_catalog_string(binding->name),
            .target = (char *)target->view.name, .description = "", .catalog_binding = binding,
            .ordinal = store->values.alias_count, .handle = SIZE_MAX};
        cvar_alias *alias = qac_cvars_alias_copy(&prototype, error);
        if (!alias) goto failed;
        if (store->values.last_alias) store->values.last_alias->next = alias;
        else store->values.aliases = alias;
        store->values.last_alias = alias; ++store->values.alias_count;
        qac_cvars_index_alias(registry, &store->values, alias);
        store->values.alias_rows[alias->ordinal] = alias;
    }
    if (!admit_player(registry,&store->values,options->seat,store->active_dialect,error) ||
        !qac_cvars_view_add(registry, &store->values, &registry->values, error)) goto failed;
    return registry;
failed:
    qac_cvars_values_free(&registry->values); qac_cvars_values_free(&store->values);
    free(store); free(registry); return NULL;
}

qa_cvars *qa_cvars_create_view(qa_cvars *shared, const qa_cvar_options *options,
    qa_error *error)
{
    if (!shared || !options || !qac_dialect_valid(options->dialect) ||
        options->side > QA_CVAR_SIDE_SERVER || options->role > QA_CVAR_ROLE_UI) {
        qac_fail(error, QA_ERROR_ARGUMENT, "cvar view needs its actual owner and Source context"); return NULL;
    }
    qa_cvars *registry = calloc(1, sizeof(*registry));
    if (!registry) { qac_fail(error, QA_ERROR_MEMORY, "allocating shared cvar view"); return NULL; }
    if (next_view_identity == UINT64_MAX) {
        free(registry); qac_fail(error, QA_ERROR_MEMORY, "cvar view identities exhausted"); return NULL;
    }
    registry->view_identity = ++next_view_identity; registry->references = 1;
    registry->options = *options; registry->store = shared->store; registry->values.cheats = true;
    qa_cvars_edit *edit=shared->store->edit;
    if (edit && edit->ready) { qac_fail(error,QA_ERROR_ARGUMENT,"new Source views require the unsealed canonical edit"); goto failed; }
    if (!admit_player(shared,&registry->store->values,options->seat,registry->store->active_dialect,error) ||
        (edit && !admit_player(shared,&edit->values,options->seat,edit->active_dialect,error)) ||
        !qac_cvars_view_add(registry, &registry->store->values, &registry->values, error)) goto failed;
    if (edit && !qac_cvars_edit_add_view(edit, registry, error)) goto failed;
    registry->candidate_edit = edit;
    registry->store->last_view->next_view = registry; registry->store->last_view = registry;
    ++registry->store->references;
    return registry;
failed:
    qac_cvars_values_free(&registry->values); free(registry); return NULL;
}
