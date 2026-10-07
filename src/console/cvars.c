#include "cvars_private.h"
#include "qa/text.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qac_cvars_touch(qa_cvars *registry, qa_error *error)
{
    if (!registry || registry->edit_first || registry->edit_bindings_pending)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar mutation requires its available owner");
    for (const qa_cvars *view = registry->store->views; view; view = view->next_view)
        if (view->notifying)
            return qac_fail(error, QA_ERROR_ARGUMENT, "cvar callback cannot mutate its shared owner");
    qa_cvars_edit *edit = qac_cvars_current_edit(registry);
    if (registry->store->edit && edit != registry->store->edit)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar mutation requires its entered prepared values");
    if (edit) return !edit->ready && edit->fault.code == QA_OK &&
        qac_cvars_edit_add_view(edit, registry, error);
    if (registry->store->revision == UINT64_MAX)
        return qac_fail(error, QA_ERROR_MEMORY, "cvar mutation identity is exhausted");
    ++registry->store->revision; ++registry->mutation_revision;
    return true;
}

bool qa_cvars_capture_metadata(const qa_cvars *registry,qa_cvar_registry_state *out,
    qa_cvar_record_state *records,size_t capacity,qa_error *error)
{
    size_t count=qa_cvars_count(registry);
    cvar_values *values=qac_cvars_current_values(registry);
    if (!qa_cvars_observer_idle(registry) || !out || capacity<count || (count && !records))
        return qac_fail(error,QA_ERROR_ARGUMENT,"metadata capture needs returned Source declarations and complete storage");
    *out=(qa_cvar_registry_state){values->next_handle,values->changed_flags,values->userinfo_modified,
        values->server_active,values->high_characters,values->cheats};
    for (size_t i=0;i<count;++i) {
        const qa_cvar_view *view=qa_cvars_at(registry,i);
        records[i]=(qa_cvar_record_state){view->name,view->handle,view->owner,
            view->modification_count,view->modified,view->console_created};
    }
    return true;
}

bool qac_cvars_name_equal(const qa_cvars *registry, const char *a, const char *b)
{
    (void)registry;
    return qac_equal(a, b);
}

static size_t name_hash(const qa_cvars *registry, const char *name)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    (void)registry;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        unsigned char byte = *p;
        if (byte >= 'A' && byte <= 'Z')
            byte = (unsigned char)(byte + ('a' - 'A'));
        hash = (hash ^ byte) * UINT64_C(1099511628211);
    }
    return (size_t)hash;
}
bool qac_cvars_index_reserve(const qa_cvars *registry, cvar_values *values,
                            size_t count, qa_error *error)
{
    if (count <= values->name_bucket_count) return true;
    size_t capacity = values->name_bucket_count ? values->name_bucket_count : 16;
    while (capacity < count) {
        if (capacity > SIZE_MAX / 2)
            return qac_fail(error, QA_ERROR_MEMORY, "cvar name index exceeds address space");
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(*values->name_buckets))
        return qac_fail(error, QA_ERROR_MEMORY, "cvar name index exceeds address space");
    cvar_name_node **buckets = calloc(capacity, sizeof(*buckets));
    if (!buckets) return qac_fail(error, QA_ERROR_MEMORY, "allocating cvar name index");
    for (size_t i = 0; i < values->name_bucket_count; ++i) {
        cvar_name_node *node = values->name_buckets[i];
        while (node) {
            cvar_name_node *next = node->next;
            size_t slot = name_hash(registry, node->name) & (capacity - 1);
            node->next = buckets[slot]; buckets[slot] = node;
            node = next;
        }
    }
    free(values->name_buckets);
    values->name_buckets = buckets; values->name_bucket_count = capacity;
    return true;
}
static void index_add(const qa_cvars *registry, cvar_values *values,
                       cvar_name_node *node)
{
    size_t slot = name_hash(registry, node->name) & (values->name_bucket_count - 1);
    node->next = values->name_buckets[slot]; values->name_buckets[slot] = node;
}
void qac_cvars_index_entry(const qa_cvars *registry, cvar_values *values, cvar *entry)
{
    entry->indexed_name = (cvar_name_node){.name=entry->view.name, .owner.entry=entry};
    values->rows[entry->ordinal] = entry;
    if (!entry->canonical) index_add(registry, values, &entry->indexed_name);
}
void qac_cvars_index_alias(const qa_cvars *registry, cvar_values *values, cvar_alias *alias)
{
    alias->indexed_name = (cvar_name_node){.name=alias->name, .alias=true,
        .side_scope=alias->catalog_binding?alias->catalog_binding->side_scope:QA_CATALOG_ANY_SIDE,
        .owner.alias=alias};
    values->alias_rows[alias->ordinal] = alias;
    if (!alias->canonical) index_add(registry, values, &alias->indexed_name);
}
static cvar_name_node *find_name(const qa_cvars *registry,
                                const cvar_values *values, const char *name)
{
    if (!registry || !values || !name) return NULL;
    const cvar_values *index = values;
    if (!index->name_bucket_count) index = values->canonical_values;
    if (!index || !index->name_bucket_count) return NULL;
    size_t slot = name_hash(registry, name) & (index->name_bucket_count - 1);
    cvar_name_node *selected = NULL;
    uint8_t side = registry->options.side == QA_CVAR_SIDE_SERVER
        ? QA_CATALOG_SERVER : QA_CATALOG_CLIENT;
    for (cvar_name_node *node = index->name_buckets[slot]; node; node = node->next) {
        if (!qac_cvars_name_equal(registry, name, node->name)) continue;
        if (!node->alias && node->owner.entry->view.player_scoped &&
            node->owner.entry->player != registry->options.seat) continue;
        if (node->side_scope == side) { selected = node; break; }
        if (node->side_scope == QA_CATALOG_ANY_SIDE) selected = node;
    }
    if (!selected || index == values) return selected;
    if (selected->alias) {
        size_t ordinal = selected->owner.alias->ordinal;
        cvar_alias *alias = ordinal < values->alias_capacity
            ? qac_cvars_source_alias((qa_cvars *)registry, (cvar_values *)values, selected->owner.alias, NULL) : NULL;
        return alias ? &alias->indexed_name : NULL;
    }
    size_t ordinal = selected->owner.entry->ordinal;
    cvar *entry = ordinal < values->row_capacity
        ? qac_cvars_source_row((qa_cvars *)registry, (cvar_values *)values, selected->owner.entry, NULL) : NULL;
    return entry ? &entry->indexed_name : NULL;
}
cvar *qac_cvars_find_values(const qa_cvars *registry, const cvar_values *values, const char *name)
{
    const cvar_name_node *node = find_name(registry, values, name);
    return node && !node->alias ? node->owner.entry : NULL;
}
static cvar *find_variable(const qa_cvars *registry, const char *name)
{ return qac_cvars_find_values(registry, registry ? qac_cvars_current_values(registry) : NULL, name); }
static cvar_alias *find_alias(const qa_cvars *registry, const cvar_values *values, const char *name)
{
    const cvar_name_node *node = find_name(registry, values, name);
    return node && node->alias ? node->owner.alias : NULL;
}
static const char *canonical_name(const qa_cvars *registry, const cvar_values *values, const char *name)
{
    const cvar_alias *alias=find_alias(registry,values,name);
    return alias?alias->target:name;
}
const char *qa_cvars_canonical_name(const qa_cvars *registry, const char *name)
{ return canonical_name(registry,qac_cvars_current_values(registry),name); }
static bool format_number(qa_console_dialect dialect,float value,char out[32],
    bool *truncated,qa_error *error)
{
    char text[64];
    if (!qac_q1(dialect) && value>=-2147483648.0f &&
        value<2147483648.0f && truncf(value)==value)
        (void)snprintf(text,sizeof(text),"%d",(int32_t)value);
    else if (!qa_format_fixed(value,6,text,sizeof(text),error)) return false;
    size_t length=strlen(text);
    if (truncated) *truncated=length>=32;
    if (length>=32) {
        if (qac_q1(dialect))
            return qac_fail(error,QA_ERROR_FORMAT,"numeric cvar exceeds source value buffer");
        length=31;
    }
    memcpy(out,text,length); out[length]='\0'; return true;
}
static const qa_cvar_view *alias_view(const qa_cvars *registry,const cvar_values *values,cvar_alias *alias)
{
    cvar *target=qac_cvars_find_values(registry,values,alias->target);
    if (!target) return NULL;
    qac_cvars_refresh((qa_cvars *)registry,target);
    alias->projection=target->view;
    alias->projection.name=alias->name;
    alias->projection.description=alias->description;
    alias->projection.documentation=alias->documentation;
    alias->projection.flags=registry->options.role==QA_CVAR_ROLE_ENGINE
        ?qac_cvars_flags(alias->flags,alias->flags_dialect,qac_cvars_view_options(registry,values).dialect)|
            qac_cvars_canonical(target)->view.flags|
            qac_cvars_catalog_flags(registry,values,target->catalog_row,alias->name)
        :alias->flags;
    alias->projection.owner=alias->owner;
    alias->projection.declared=alias->declared;
    alias->projection.console_created=alias->console_created;
    alias->projection.handle=alias->handle;
    alias->projection.modification_count=alias->modification_count;
    alias->projection.modified=alias->modified;
    const cvar *canonical=qac_cvars_canonical(target);
    const cvar_detail *detail=canonical->details;
    while (detail && (detail->binding!=alias->catalog_binding || detail->dialect!=qac_cvars_view_options(registry,values).dialect)) detail=detail->next;
    if (alias->catalog_binding) {
        qa_cvar_options options=qac_cvars_view_options(registry,values);
        qac_cvar_conversion_input input={.options=&options,
            .conversion=qac_cvars_conversion(registry,values,alias->catalog_binding),.binding=alias->catalog_binding,
            .current=target->view.value,.value=target->view.value,.detail=detail?detail->value:NULL};
        qac_cvar_conversion_output output;
        cvar_projection_context context={.registry=registry,.values=values};
        input.user=&context; input.operand=qac_cvars_operand; input.video=qac_cvars_video;
        if (!qac_cvar_read_conversion(&input,&output,NULL)) return NULL;
        if (output.value==output.text) { memcpy(alias->value,output.text,strlen(output.text)+1); alias->projection.value=alias->value; }
        else alias->projection.value=output.value;
        input.value=target->view.reset_value; input.detail=NULL; context.reset=true;
        if (!qac_cvar_read_conversion(&input,&output,NULL)) return NULL;
        if (output.value==output.text) { memcpy(alias->reset,output.text,strlen(output.text)+1); alias->projection.reset_value=alias->reset; }
        else alias->projection.reset_value=output.value;
        context.reset=false;
        if (target->view.latched_value) {
            context.latched=true;
            input.value=target->view.latched_value; input.detail=detail?detail->latched_value:NULL;
            if (!qac_cvar_read_conversion(&input,&output,NULL)) return NULL;
            if (output.value==output.text) { memcpy(alias->latched,output.text,strlen(output.text)+1); alias->projection.latched_value=alias->latched; }
            else alias->projection.latched_value=output.value;
        }
    }
    if (!alias->projection.value || !alias->projection.reset_value) return NULL;
    alias->projection.number=qac_number(alias->projection.value,qac_cvars_view_options(registry,values).dialect);
    alias->projection.integer=qac_integer(alias->projection.value);
    return &alias->projection;
}

static cvar_target live_target(qa_cvars *registry)
{ return (cvar_target){registry,qac_cvars_current_values(registry),qac_cvars_current_edit(registry)}; }
static bool target_touch(cvar_target target, qa_error *error)
{
    if (!target.edit) return qac_cvars_touch(target.registry,error);
    if (!target.registry || target.registry->store->edit!=target.edit || target.edit->ready ||
        target.edit->fault.code!=QA_OK || target.registry->store->revision!=target.edit->revision ||
        target.registry->notifying || target.registry->draining || target.registry->post_first ||
        target.registry->edit_first || target.registry->edit_bindings_pending)
        return qac_fail(error,QA_ERROR_ARGUMENT,"prepared cvar values are unavailable or stale");
    return true;
}

static const char *source_name(const qa_cvars *, const char *);

bool qa_cvars_observer_idle(const qa_cvars *registry)
{
    return registry && !registry->mutation_depth && !registry->notifying &&
        !registry->draining && !registry->post_first && !registry->ready_edit && !registry->edit_scope_depth &&
        !registry->edit_first && !registry->edit_bindings_pending;
}
uint64_t qa_cvars_revision(const qa_cvars *registry)
{
    return qa_cvars_observer_idle(registry) ? registry->store->revision : 0;
}
static void observer_release(qa_cvars *registry, cvar_observer *observer)
{
    if (observer->active || observer->references) return;
    cvar_observer **link=&registry->observers;
    while (*link!=observer) link=&(*link)->next;
    *link=observer->next;
    if (registry->last_observer==observer) {
        registry->last_observer=NULL;
        for (cvar_observer *row=registry->observers;row;row=row->next) registry->last_observer=row;
    }
    free(observer->name); free(observer);
}
static void post_release(qa_cvars *registry, cvar_post_event *event)
{
    if (!event) return;
    for (size_t i=0;i<event->count;++i) {
        cvar_observer *row=event->observers[i];
        --row->references; observer_release(registry,row);
    }
    free(event);
}
static bool post_prepare(qa_cvars *registry, const cvar *entry,
    cvar_post_event **out, qa_error *error)
{
    size_t count=0;
    for (cvar_observer *row=registry->observers;row;row=row->next)
        if (row->active && !row->suppressed && qac_cvars_name_equal(registry,canonical_name(registry,qac_cvars_current_values(registry),row->name),
            canonical_name(registry,qac_cvars_current_values(registry),entry->view.name))) {
            if (count==SIZE_MAX || row->references==SIZE_MAX)
                return qac_fail(error,QA_ERROR_MEMORY,"cvar observer inventory is exhausted");
            ++count;
        }
    *out=NULL;
    if (!count) return true;
    if (count>(SIZE_MAX-sizeof(cvar_post_event))/sizeof(cvar_observer *))
        return qac_fail(error,QA_ERROR_MEMORY,"cvar observer event exceeds address space");
    cvar_post_event *event=malloc(sizeof(*event)+count*sizeof(*event->observers));
    if (!event) return qac_fail(error,QA_ERROR_MEMORY,"retaining cvar publication observers");
    event->next=NULL; event->count=0;
    for (cvar_observer *row=registry->observers;row;row=row->next)
        if (row->active && !row->suppressed && qac_cvars_name_equal(registry,canonical_name(registry,qac_cvars_current_values(registry),row->name),
            canonical_name(registry,qac_cvars_current_values(registry),entry->view.name))) {
            ++row->references; event->observers[event->count++]=row;
        }
    *out=event; return true;
}
static void post_enqueue(qa_cvars *registry,cvar_post_event *event)
{
    if (!event) return;
    if (registry->post_last) registry->post_last->next=event;
    else registry->post_first=event;
    registry->post_last=event;
}
static bool post_drain(qa_cvars *registry, qa_error *error)
{
    if (registry->mutation_depth || registry->draining) return true;
    registry->draining=true; bool ok=true;
    while (registry->post_first) {
        cvar_post_event *event=registry->post_first;
        registry->post_first=event->next;
        if (!registry->post_first) registry->post_last=NULL;
        for (size_t i=0;i<event->count;++i) {
            cvar_observer *row=event->observers[i];
            if (!row->active) continue;
            qa_error fault={0};
            if (!row->callback(row->user,registry,row->name,&fault)) {
                if (fault.code==QA_OK) qac_fail(&fault,QA_ERROR_ARGUMENT,"cvar publication observer failed");
                if (ok && error && error->code==QA_OK) *error=fault;
                ok=false;
            }
        }
        post_release(registry,event);
    }
    registry->draining=false; return ok;
}
static bool mutation_begin(qa_cvars *registry,qa_error *error)
{
    if (!registry || registry->mutation_depth==SIZE_MAX || !qac_cvars_touch(registry,error)) return false;
    ++registry->mutation_depth; return true;
}
static bool mutation_end(qa_cvars *registry,bool ok,qa_error *error)
{
    --registry->mutation_depth;
    if (qac_cvars_current_edit(registry)) return ok;
    for (qa_cvars *view=registry->store->views;view;view=view->next_view)
        if (!post_drain(view,error)) ok=false;
    return ok;
}

bool qa_cvars_observe(qa_cvars *registry,const char *name,uint64_t owner,
    qa_cvar_observer_fn callback,void *user,qa_cvar_observer_token *out,qa_error *error)
{
    if (!registry || registry->notifying || !name || !owner || !callback || !out || *out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar observer needs a declared owner and empty token");
    const qa_cvar_view *entry=qa_cvars_find(registry,source_name(registry,name));
    if (!entry) return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar observer name is not registered");
    if (registry->next_observer==UINT64_MAX)
        return qac_fail(error,QA_ERROR_MEMORY,"cvar observer tokens are exhausted");
    cvar_observer *row=calloc(1,sizeof(*row));
    if (!row) return qac_fail(error,QA_ERROR_MEMORY,"allocating cvar observer");
    row->name=qac_copy(source_name(registry,name),error);
    if (!row->name || !qac_cvars_touch(registry,error)) { free(row->name); free(row); return false; }
    row->token=++registry->next_observer; row->owner=owner;
    row->callback=callback; row->user=user; row->active=true;
    if (registry->last_observer) registry->last_observer->next=row;
    else registry->observers=row;
    registry->last_observer=row; *out=row->token; return true;
}
void qa_cvars_unobserve(qa_cvars *registry,qa_cvar_observer_token token)
{
    if (!registry || registry->notifying || !token) return;
    for (cvar_observer *row=registry->observers;row;row=row->next)
        if (row->token==token && row->active) {
            if (!qac_cvars_touch(registry,NULL)) return;
            row->active=false; observer_release(registry,row); return;
        }
}
bool qa_cvars_observer_suppress(qa_cvars *registry,qa_cvar_observer_token token,
    bool suppressed,qa_error *error)
{
    if (!registry || registry->notifying || !token)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar observer suppression needs its active token");
    for (cvar_observer *row=registry->observers;row;row=row->next)
        if (row->token==token && row->active) {
            if (!qac_cvars_touch(registry,error)) return false;
            row->suppressed=suppressed; return true;
        }
    return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar observer token is retired");
}
bool qa_cvars_restore_metadata(qa_cvars *registry,const qa_cvar_registry_state *state,
    const qa_cvar_record_state *records,size_t count,qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || !state || count!=qa_cvars_count(registry) ||
        (count && !records) || !qac_cvars_touch(registry,error))
        return qac_fail(error,QA_ERROR_ARGUMENT,"metadata restore needs this returned Source declaration inventory");
    cvar_values *values=qac_cvars_current_values(registry);
    for (size_t i=0;i<count;++i) {
        const cvar_name_node *node=find_name(registry,values,records[i].name);
        const qa_cvar_view *view=qa_cvars_find(registry,records[i].name);
        bool bound=node && (node->alias?node->owner.alias->bound:node->owner.entry->bound);
        if (!view || (records[i].handle!=SIZE_MAX && records[i].handle>=state->next_handle) ||
            (bound && (view->owner!=records[i].owner || view->handle!=records[i].handle)))
            return qac_fail(error,QA_ERROR_FORMAT,"saved Source metadata lost its actual declaration/handle");
        for (size_t j=0;j<i;++j)
            if ((records[i].handle!=SIZE_MAX && records[i].handle==records[j].handle) || qac_equal(records[i].name,records[j].name))
                return qac_fail(error,QA_ERROR_FORMAT,"duplicate saved Source metadata");
    }
    if ((qa_cvars_dialect(registry)==QA_CONSOLE_Q3 && state->next_handle>1024) ||
        !qac_cvars_handles_reserve(values,state->next_handle,error)) return false;
    memset(values->handles,0,values->handle_capacity*sizeof(*values->handles));
    for (size_t i=0;i<count;++i) {
        cvar_name_node *node=find_name(registry,values,records[i].name);
        if (node->alias) {
            cvar_alias *alias=node->owner.alias;
            alias->handle=records[i].handle; alias->vm_bound=alias->handle!=SIZE_MAX;
            alias->owner=records[i].owner; alias->console_created=records[i].console_created;
            alias->modification_count=records[i].modification_count; alias->modified=records[i].modified;
        } else {
            cvar *entry=node->owner.entry;
            entry->view.handle=records[i].handle; entry->view.owner=records[i].owner;
            entry->view.modification_count=records[i].modification_count;
            entry->view.modified=records[i].modified; entry->view.console_created=records[i].console_created;
        }
        if (records[i].handle!=SIZE_MAX) values->handles[records[i].handle]=node;
    }
    values->next_handle=state->next_handle; values->changed_flags=state->modified_flags;
    values->userinfo_modified=state->userinfo_modified; values->server_active=state->server_active;
    values->high_characters=state->high_characters; values->cheats=state->cheats;
    return true;
}
static bool retain_shared_variable(cvar_target target,const char *name,qa_error *error)
{
    if (!target_touch(target,error)) return false;
    cvar *entry=qac_cvars_find_values(target.registry,target.values,
        canonical_name(target.registry,target.values,name));
    if (entry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "shared cvar is not registered");
    entry->view.owner = 0;
    return true;
}
bool qa_cvars_retain_shared(qa_cvars *registry,const char *name,qa_error *error)
{ return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RETAIN_SHARED,.name=name},error); }

typedef enum cvar_edit_event_kind {
    CVAR_EDIT_NOTIFY, CVAR_EDIT_EFFECT, CVAR_EDIT_PRINT
} cvar_edit_event_kind;
struct cvar_edit_event {
    struct cvar_edit_event *next;
    cvar_edit_event_kind kind;
    qa_cvars *registry;
    cvar *snapshot;
    cvar_post_event *post;
    bool changed;
    qa_cvar_effect_kind effect;
    char *print_name, *print_message;
};
static cvar *entry_copy(const cvar *source, qa_error *error)
{
    cvar *copy=calloc(1,sizeof(*copy));
    if (!copy) { qac_fail(error,QA_ERROR_MEMORY,"copying prepared cvar record"); return NULL; }
    *copy=*source;
    copy->next=NULL; copy->canonical=NULL; copy->declaration_default=NULL;
    memset(copy->defaults,0,sizeof(copy->defaults)); copy->details=NULL;
    copy->indexed_name=(cvar_name_node){0};
    copy->view.name=qac_copy(source->view.name,error);
    copy->view.value=qac_copy(source->view.value,error);
    copy->view.reset_value=qac_copy(source->view.reset_value,error);
    copy->view.latched_value=source->view.latched_value?qac_copy(source->view.latched_value,error):NULL;
    copy->view.description=qac_copy(source->view.description,error);
    copy->view.documentation=NULL;
    if (!copy->view.name || !copy->view.value || !copy->view.reset_value ||
        (source->view.latched_value && !copy->view.latched_value) || !copy->view.description ||
        !qac_document_replace(&copy->view.documentation,source->view.documentation,error)) {
        qac_cvars_entry_free(copy); return NULL;
    }
    return copy;
}
static void edit_event_free(qa_cvars *registry,cvar_edit_event *event)
{
    if (!event) return;
    post_release(registry,event->post);
    if (event->snapshot) qac_cvars_entry_free(event->snapshot);
    free(event->print_name); free(event->print_message); free(event);
}
static cvar_edit_event *edit_event(qa_cvars_edit *edit,cvar_edit_event_kind kind,
    const cvar *entry,qa_error *error)
{
    cvar_edit_event *event=calloc(1,sizeof(*event));
    if (!event) { qac_fail(error,QA_ERROR_MEMORY,"retaining prepared cvar notification"); return NULL; }
    event->kind=kind; event->registry=edit->registry;
    if (entry && !(event->snapshot=entry_copy(entry,error))) { free(event); return NULL; }
    if (edit->last) edit->last->next=event;
    else edit->first=event;
    edit->last=event;
    return event;
}
static void print_message(cvar_target target, const char *name, const char *message)
{
    qa_cvars *registry=target.registry;
    if (registry->options.print == NULL) return;
    if (target.edit) {
        cvar_edit_event *event=edit_event(target.edit,CVAR_EDIT_PRINT,NULL,&target.edit->fault);
        if (!event) return;
        event->registry=registry;
        event->print_name=name?qac_copy(name,&target.edit->fault):NULL;
        event->print_message=qac_copy(message,&target.edit->fault);
        return;
    }
    ++registry->notifying;
    if (name != NULL) registry->options.print(registry->options.user, name);
    registry->options.print(registry->options.user, message);
    --registry->notifying;
}

static bool valid_info(const char *text)
{
    return strpbrk(text, "\\\";") == NULL;
}

bool qa_cvars_name_valid(qa_console_dialect dialect, const char *text)
{
    if (!text) return false;
    if (dialect == QA_CONSOLE_Q3) return valid_info(text);
    if (*text == '\0') return false;
    for (; *text != '\0'; ++text)
        if ((unsigned char)*text <= 32 || *text == '"' || *text == ';') return false;
    return true;
}

static const char *source_name(const qa_cvars *registry, const char *name)
{
    return qa_cvars_dialect(registry) == QA_CONSOLE_Q3 &&
        !qa_cvars_name_valid(qa_cvars_dialect(registry), name) ? "BADNAME" : name;
}

void qac_cvars_entry_free(cvar *entry)
{
    if (!entry->canonical) {
        free((char *)entry->view.name);
        free((char *)entry->view.value);
        free((char *)entry->view.reset_value);
        free((char *)entry->view.latched_value);
        if (!entry->definition)
            for (size_t d = 0; d < QA_CVAR_CATALOG_DIALECTS; ++d) free(entry->defaults[d]);
        while (entry->details) { cvar_detail *detail=entry->details; entry->details=detail->next;
            free(detail->value); free(detail->latched_value); free(detail); }
    }
    free(entry->declaration_default);
    free((char *)entry->view.description);
    qac_document_free(entry->view.documentation);
    free(entry);
}
void qac_cvars_alias_free(cvar_alias *alias)
{
    if (!alias) return;
    if (!alias->canonical) { free(alias->name); free(alias->target); }
    free(alias->description);
    qac_document_free(alias->documentation); free(alias);
}
cvar_alias *qac_cvars_alias_copy(const cvar_alias *source,qa_error *error)
{
    cvar_alias *alias=calloc(1,sizeof(*alias));
    if (!alias) { qac_fail(error,QA_ERROR_MEMORY,"retaining canonical cvar alias"); return NULL; }
    alias->ordinal=source->ordinal;
    alias->name=qac_copy(source->name,error); alias->target=qac_copy(source->target,error);
    alias->description=qac_copy(source->description,error);
    alias->handle=source->handle; alias->vm_bound=source->vm_bound;
    alias->catalog_binding=source->catalog_binding;
    alias->binding=source->binding; alias->bound=source->bound;
    alias->declared=source->declared; alias->console_created=source->console_created; alias->binding_order=source->binding_order;
    alias->owner=source->owner; alias->flags=source->flags; alias->flags_dialect=source->flags_dialect;
    alias->modification_count=source->modification_count; alias->modified=source->modified;

    if (!alias->name || !alias->target || !alias->description ||
        !qac_document_replace(&alias->documentation,source->documentation,error)) {
        qac_cvars_alias_free(alias); return NULL;
    }
    return alias;
}
bool qa_cvars_document(qa_cvars *registry, const char *name, uint64_t owner,
                        const qa_console_documentation *doc, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    cvar_alias *alias=find_alias(registry,qac_cvars_current_values(registry),name);
    if (alias) {
        const cvar *target=find_variable(registry,alias->target);
        if (!target || alias->owner!=owner)
            return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar alias documentation owner not found");
        return qac_document_replace(&alias->documentation,doc,error);
    }
    cvar *entry = find_variable(registry, name);
    if (!entry || entry->view.owner != owner)
        return qac_fail(error, QA_ERROR_NOT_FOUND, "cvar documentation owner not found");
    return qac_document_replace(&entry->view.documentation, doc, error);
}

static void numbers(qa_cvars *registry, cvar *entry)
{
    entry->view.number = qac_number(entry->view.value, qa_cvars_dialect(registry));
    entry->view.integer = qac_integer(entry->view.value);
}

static bool replace_text(const char **target, const char *value, qa_error *error)
{
    char *copy = qac_copy(value, error);
    if (copy == NULL) return false;
    free((char *)*target);
    *target = copy;
    return true;
}

static void effect(cvar_target target, qa_cvar_effect_kind kind, const cvar *entry)
{
    qa_cvars *registry=target.registry;
    if (registry->options.effect != NULL) {
        if (target.edit) {
            cvar_edit_event *event=edit_event(target.edit,CVAR_EDIT_EFFECT,entry,&target.edit->fault);
            if (event) { event->effect=kind; event->registry=registry; }
            return;
        }
        ++registry->notifying;
        registry->options.effect(registry->options.user, kind, &entry->view);
        --registry->notifying;
    }
}
static bool validate_value(qa_cvars *registry,const qa_cvar_binding *binding,
    const char *value,qa_error *error)
{
    if (!binding->validate) return true;
    ++registry->notifying;
    bool ok=binding->validate(binding->user,value,error);
    --registry->notifying; return ok;
}
static void changed_value(qa_cvars *registry,const cvar *entry,const char *value)
{
    if (!entry->bound || !entry->binding.changed) return;
    ++registry->notifying;
    entry->binding.changed(entry->binding.user,value);
    --registry->notifying;
}
static void propagate(cvar_target target, const cvar *entry, bool changed)
{
    qa_cvars *registry=target.registry;
    if (qa_cvars_dialect(registry) == QA_CONSOLE_Q1 && changed && target.values->server_active &&
        (entry->view.flags & QA_CVAR_SERVERINFO) != 0)
        effect(target, QA_CVAR_EFFECT_BROADCAST, entry);
    else if (qa_cvars_dialect(registry) == QA_CONSOLE_QW) {
        if ((entry->view.flags & QA_CVAR_USERINFO) != 0) effect(target, QA_CVAR_EFFECT_USERINFO, entry);
        if ((entry->view.flags & QA_CVAR_SERVERINFO) != 0) effect(target, QA_CVAR_EFFECT_SERVERINFO, entry);
    }
}

typedef struct cvar_write {
    cvar *entry;
    const char *value, *reset_value, *latch_reset;
    const qa_cvar_catalog_binding *detail_binding;
    qa_console_dialect detail_dialect;
    const char *detail_value;
    bool clear_details, clear_latch, promote_details, pending, explicit_value, mark, silent, reuse_value;
    char *owned_value, *owned_detail, *owned_reset, *owned_latch;
    cvar_detail *projected_details;
    cvar_detail *detail, *allocated_detail;
    cvar_edit_event *events;
} cvar_write;

static bool alias_affected(const qa_cvars *registry,const cvar_values *values,const cvar_alias *alias,const cvar *canonical)
{
    if (canonical->view.player_scoped && canonical->player != registry->options.seat) return false;
    if (qac_cvars_name_equal(registry,alias->target,canonical->view.name)) return true;
    const qa_cvar_catalog_conversion *conversion=qac_cvars_conversion(registry,values,alias->catalog_binding);
    if (conversion && (conversion->operation==QA_CATALOG_OP_VIDEO_MODE ||
        conversion->operation==QA_CATALOG_OP_FULLSCREEN)) {
        if (qac_equal(canonical->view.name,"r_customwidth") ||
            qac_equal(canonical->view.name,"r_customheight")) return true;
        if (conversion->operation==QA_CATALOG_OP_FULLSCREEN &&
            (qac_equal(canonical->view.name,"r_mode") || qac_equal(canonical->view.name,"vid_modelist"))) return true;
    }
    for (size_t i=0;conversion && i<conversion->operand_count;++i)
        if (qa_cvar_catalog_operands[conversion->operand_first+i].row_index==canonical->catalog_row) return true;
    return false;
}
static bool record_affected(const qa_cvars *registry,const cvar_values *values,const cvar *record,const cvar *canonical)
{
    if (record->catalog_row==QA_CVAR_CATALOG_NO_ROW || !qa_cvar_catalog_rows[record->catalog_row].not_stored) return false;
    const qa_cvar_catalog_conversion *conversion=qac_cvars_conversion(registry,values,record->catalog_binding);
    for (size_t i=0;conversion && i<conversion->operand_count;++i)
        if (qa_cvar_catalog_operands[conversion->operand_first+i].row_index==canonical->catalog_row) return true;
    return false;
}
static bool receives_value(qa_cvars *registry,const cvar_values *values,const qa_cvar_view *view,bool bound)
{
    if (bound) return true;
    for (const cvar_observer *observer=registry->observers;observer;observer=observer->next)
        if (observer->active && !observer->suppressed && qac_equal(
            canonical_name(registry,values,observer->name),canonical_name(registry,values,view->name))) return true;
    if (!registry->options.effect) return false;
    if (qa_cvars_dialect(registry)==QA_CONSOLE_QW) return (view->flags&(QA_CVAR_USERINFO|QA_CVAR_SERVERINFO))!=0;
    return qa_cvars_dialect(registry)==QA_CONSOLE_Q1 && values->server_active && (view->flags&QA_CVAR_SERVERINFO)!=0;
}
static bool retain_fanout(cvar_target target,qa_cvars *receiver,const qa_cvar_view *view,
    const qa_cvar_binding *binding,bool bound,uint64_t order,bool observers,
    cvar_edit_event **events,qa_error *error)
{
    cvar snapshot={.view=*view,.binding=*binding,.bound=bound,.binding_order=order};
    cvar_edit_event *event=calloc(1,sizeof(*event));
    if (!event) return qac_fail(error,QA_ERROR_MEMORY,"retaining shared value notifications");
    event->kind=CVAR_EDIT_NOTIFY; event->registry=receiver;
    event->snapshot=entry_copy(&snapshot,error);
    if (!event->snapshot || (observers && !post_prepare(receiver,&snapshot,&event->post,error))) {
        edit_event_free(receiver,event); return false;
    }
    cvar_edit_event **link=events;
    while (*link && (*link)->snapshot->binding_order<=order) link=&(*link)->next;
    event->next=*link; *link=event;
    (void)target;
    return true;
}
static void write_dispose(cvar_write *write)
{
    free(write->owned_value); free(write->owned_detail); free(write->owned_reset); free(write->owned_latch); free(write->allocated_detail);
    while (write->projected_details) { cvar_detail *detail=write->projected_details; write->projected_details=detail->next; free(detail); }
    while (write->events) {
        cvar_edit_event *event=write->events; write->events=event->next;
        edit_event_free(event->registry,event);
    }
    write->owned_value=write->owned_detail=write->owned_reset=NULL; write->allocated_detail=NULL;
}
static bool write_prepare(cvar_target target,cvar_write *write,qa_error *error)
{
    cvar *canonical=qac_cvars_canonical(write->entry);
    const char *current=write->pending?canonical->view.latched_value:canonical->view.value;
    write->reuse_value=current && !strcmp(current,write->value);
    if (!write->reuse_value && !(write->owned_value=qac_copy(write->value,error))) return false;
    if (write->reset_value && !(write->owned_reset=qac_copy(write->reset_value,error))) goto failed;
    if (write->latch_reset && !(write->owned_latch=qac_copy(write->latch_reset,error))) goto failed;
    cvar shadow=*canonical;
    shadow.view.value=write->reuse_value?current:write->owned_value;
    if (write->owned_reset) shadow.view.reset_value=write->owned_reset;
    if (write->clear_latch) shadow.view.latched_value=NULL;
    if (write->clear_details) shadow.details=NULL;
    if (write->promote_details) {
        cvar_detail **tail=&write->projected_details;
        for (const cvar_detail *detail=canonical->details;detail;detail=detail->next) {
            cvar_detail *copy=malloc(sizeof(*copy));
            if (!copy) { qac_fail(error,QA_ERROR_MEMORY,"admitting promoted cvar details"); goto failed; }
            *copy=*detail; copy->value=detail->latched_value; copy->latched_value=NULL; copy->next=NULL;
            *tail=copy; tail=&copy->next;
        }
        shadow.details=write->projected_details;
    }
    if (write->owned_latch) shadow.view.latched_value=write->owned_latch;
    cvar_detail proposed={0};
    if (write->detail_binding && write->detail_value) {
        write->owned_detail=qac_copy(write->detail_value,error);
        if (!write->owned_detail) goto failed;
        for (cvar_detail *detail=canonical->details;detail;detail=detail->next)
            if (detail->binding==write->detail_binding && detail->dialect==write->detail_dialect) { write->detail=detail; break; }
        if (!write->detail) {
            write->allocated_detail=calloc(1,sizeof(*write->allocated_detail));
            if (!write->allocated_detail) { qac_fail(error,QA_ERROR_MEMORY,"retaining canonical enum metadata"); goto failed; }
            write->allocated_detail->binding=write->detail_binding;
            write->allocated_detail->dialect=write->detail_dialect;
        }
        proposed=write->detail?*write->detail:*write->allocated_detail;
        proposed.value=write->owned_detail; proposed.next=shadow.details; shadow.details=&proposed;
    }
    if (write->silent) return true;
    for (qa_cvars *receiver=target.registry->store->views;receiver;receiver=receiver->next_view) {
        cvar_values *values=target.edit?&qac_cvars_edit_view(target.edit,receiver)->values:&receiver->values;
        cvar *entry=values->rows[canonical->ordinal];
        if (!entry) continue;
        cvar *original=entry->canonical; entry->canonical=&shadow;
        bool okay=true;
        if (receives_value(receiver,values,&entry->view,entry->bound)) {
            const qa_cvar_view *view=qac_cvars_project(receiver,values,entry);
            okay=view && retain_fanout(target,receiver,view,&entry->binding,entry->bound,
                entry->binding_order,true,&write->events,error);
        }
        for (cvar *derived=values->first;okay && derived;derived=derived->next) {
            if (!record_affected(receiver,values,derived,canonical) || !receives_value(receiver,values,&derived->view,derived->bound)) continue;
            const qa_cvar_view *view=qac_cvars_project(receiver,values,derived);
            okay=view && retain_fanout(target,receiver,view,&derived->binding,derived->bound,
                derived->binding_order,true,&write->events,error);
        }
        for (cvar_alias *alias=values->aliases;okay && alias;alias=alias->next) {
            if (!(alias->declared || alias->bound) || !alias_affected(receiver,values,alias,canonical)) continue;
            const qa_cvar_view *view=alias_view(receiver,values,alias);
            if (view && !receives_value(receiver,values,view,alias->bound)) continue;
            bool observers=!qac_cvars_name_equal(receiver,alias->target,canonical->view.name);
            okay=view && retain_fanout(target,receiver,view,&alias->binding,alias->bound,
                alias->binding_order,observers,&write->events,error);
        }
        entry->canonical=original; qac_cvars_refresh(receiver,entry);
        if (!okay) goto failed;
    }
    return true;
failed:
    write_dispose(write); return false;
}
static void write_commit(cvar_target target,cvar_write *write)
{
    cvar *canonical=qac_cvars_canonical(write->entry);
    const char **value=write->pending?&canonical->view.latched_value:&canonical->view.value;
    if (!write->reuse_value) { free((char *)*value); *value=write->owned_value; write->owned_value=NULL; }
    if (write->owned_reset) {
        free((char *)canonical->view.reset_value); canonical->view.reset_value=write->owned_reset; write->owned_reset=NULL;
    }
    if (write->clear_latch && !write->pending) {
        free((char *)canonical->view.latched_value); canonical->view.latched_value=NULL; canonical->pending_explicit=false;
        for (cvar_detail *detail=canonical->details;detail;detail=detail->next) {
            if (write->promote_details) { free(detail->value); detail->value=detail->latched_value; }
            else free(detail->latched_value);
            detail->latched_value=NULL;
        }
    }
    if (write->owned_latch) {
        free((char *)canonical->view.latched_value); canonical->view.latched_value=write->owned_latch; write->owned_latch=NULL;
        for (cvar_detail *detail=canonical->details;detail;detail=detail->next) { free(detail->latched_value); detail->latched_value=NULL; }
    }
    numbers(target.registry,canonical);
    if (write->pending) canonical->pending_explicit=write->explicit_value;
    else canonical->view.explicit_value=write->explicit_value;
    if (write->clear_details) {
        for (cvar_detail *detail=canonical->details;detail;detail=detail->next) {
            char **slot=write->pending?&detail->latched_value:&detail->value;
            free(*slot); *slot=NULL;
        }
    }
    if (write->owned_detail) {
        cvar_detail *detail=write->detail;
        if (!detail) {
            detail=write->allocated_detail; write->allocated_detail=NULL;
            detail->next=canonical->details; canonical->details=detail;
        }
        char **slot=write->pending?&detail->latched_value:&detail->value;
        free(*slot); *slot=write->owned_detail; write->owned_detail=NULL;
    }
    for (qa_cvars *receiver=target.registry->store->views;receiver;receiver=receiver->next_view) {
        cvar_values *values=target.edit?&qac_cvars_edit_view(target.edit,receiver)->values:&receiver->values;
        cvar *entry=values->rows[canonical->ordinal]; qac_cvars_refresh(receiver,entry);
        if (!entry) continue;
        if (write->mark && !qac_q1(qac_cvars_view_options(receiver,values).dialect)) {
            entry->view.modified=true; ++entry->view.modification_count;
            values->changed_flags|=entry->view.flags;
        }
        if (!write->silent && !write->pending && write->mark && (entry->view.flags&QA_CVAR_USERINFO)) values->userinfo_modified=true;

    }
}
static void write_projection_metadata(cvar_target target,const cvar_write *writes,size_t count)
{
    bool changed_group=false;
    for (size_t i=0;i<count;++i) changed_group |= writes[i].mark;
    if (!changed_group) return;
    for (qa_cvars *receiver=target.registry->store->views;receiver;receiver=receiver->next_view) {
        cvar_values *values=target.edit?&qac_cvars_edit_view(target.edit,receiver)->values:&receiver->values;
        for (cvar *entry=values->first;entry;entry=entry->next) {
            if (entry->catalog_row==QA_CVAR_CATALOG_NO_ROW || !qa_cvar_catalog_rows[entry->catalog_row].not_stored) continue;
            bool changed=false,info=false;
            for (size_t i=0;i<count;++i) {
                if (!writes[i].mark || !record_affected(receiver,values,entry,qac_cvars_canonical(writes[i].entry))) continue;
                changed=true; info |= !writes[i].silent && !writes[i].pending;
            }
            if (changed && !qac_q1(qac_cvars_view_options(receiver,values).dialect)) {
                entry->view.modified=true; ++entry->view.modification_count; values->changed_flags|=entry->view.flags;
            }
            if (info && (entry->view.flags&QA_CVAR_USERINFO)) values->userinfo_modified=true;
        }
        for (cvar_alias *alias=values->aliases;alias;alias=alias->next) {
            bool changed=false,info=false;
            for (size_t i=0;i<count;++i) {
                if (!writes[i].mark || !alias_affected(receiver,values,alias,qac_cvars_canonical(writes[i].entry))) continue;
                changed=true; info |= !writes[i].silent && !writes[i].pending;
            }
            if (changed && !qac_q1(qac_cvars_view_options(receiver,values).dialect)) {
                alias->modified=true; ++alias->modification_count; values->changed_flags|=alias->flags;
            }
            if (info && (alias->flags&QA_CVAR_USERINFO)) values->userinfo_modified=true;
        }
    }
}
static void write_deliver(cvar_target target,cvar_write *write)
{
    while (write->events) {
        cvar_edit_event *event=write->events; write->events=event->next; event->next=NULL;
        if (write->pending) { edit_event_free(event->registry,event); continue; }
        cvar_target receiver={event->registry,target.edit?&qac_cvars_edit_view(target.edit,event->registry)->values:&event->registry->values,target.edit};
        if (target.edit) {
            if (target.edit->last) target.edit->last->next=event; else target.edit->first=event;
            target.edit->last=event;
            propagate(receiver,event->snapshot,event->changed);
        } else {
            post_enqueue(event->registry,event->post); event->post=NULL;
            propagate(receiver,event->snapshot,event->changed);
            edit_event_free(event->registry,event);
        }
    }
}

bool qa_cvars_cheats_policy(const qa_cvars *registry, bool *callback_backed,
                             bool *fallback_allowed)
{
    if (!registry || !callback_backed || !fallback_allowed)
        return false;
    *callback_backed = registry->options.cheats_allowed != NULL;
    *fallback_allowed = registry->values.cheats;
    return true;
}

static bool cheats_allowed(cvar_target target)
{
    qa_cvars *registry=target.registry;
    if (registry->options.cheats_allowed != NULL) {
        qa_cvars *owned=(qa_cvars *)registry;
        ++owned->notifying;
        bool allowed=registry->options.cheats_allowed(registry->options.user);
        --owned->notifying; return allowed;
    }
    const cvar *cheats = qac_cvars_find_values(registry, target.values, "sv_cheats");
    return cheats == NULL ? target.values->cheats : cheats->view.integer != 0;
}

qa_cvars *qa_cvars_create(const qa_cvar_options *options, qa_error *error)
{
    if (options == NULL || !qac_dialect_valid(options->dialect) ||
        options->default_save_policy < QA_CVAR_SAVE_UNCLASSIFIED ||
        options->default_save_policy > QA_CVAR_SAVE_SETTING) {
        qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar registry options");
        return NULL;
    }
    return qac_cvars_store_create(options, error);
}

static void retire_view_events(qa_cvars *registry)
{
    qa_cvars_edit *edit=registry->store->edit;
    if (edit) {
        cvar_edit_event **link=&edit->first;
        while (*link) {
            cvar_edit_event *event=*link;
            if (event->registry!=registry) { link=&event->next; continue; }
            *link=event->next; edit_event_free(registry,event);
        }
        edit->last=edit->first;
        while (edit->last && edit->last->next) edit->last=edit->last->next;
    }
    for (qa_cvars *owner=registry->store->views;owner;owner=owner->next_view) {
        cvar_edit_event **link=&owner->edit_first;
        while (*link) {
            cvar_edit_event *event=*link;
            if (event->registry!=registry) { link=&event->next; continue; }
            *link=event->next; edit_event_free(registry,event);
        }
        owner->edit_last=owner->edit_first;
        while (owner->edit_last && owner->edit_last->next) owner->edit_last=owner->edit_last->next;
        size_t retained=0;
        for (size_t i=0;i<owner->edit_binding_count;++i)
            if (owner->edit_bindings[i].registry!=registry) owner->edit_bindings[retained++]=owner->edit_bindings[i];
        owner->edit_binding_count=retained;
    }
}

static void retire_player_defaults(qa_cvars *registry)
{
    cvar_values *sets[2]={&registry->store->values,
        registry->store->edit?&registry->store->edit->values:NULL};
    for (size_t i=0;i<2;++i)
        for (cvar *entry=sets[i]?sets[i]->first:NULL;entry;entry=entry->next)
            if (entry->player_default_source==registry) entry->player_default_source=NULL;
}

void qa_cvars_detach_callbacks(qa_cvars *registry)
{
    if (!registry || registry->notifying) return;
    if (registry->store->video_owner==registry) {
        registry->store->video_owner=NULL;
        registry->store->video_resolver=NULL;
        registry->store->video_user=NULL;
    }
    registry->options.user=NULL; registry->options.print=NULL;
    registry->options.command_exists=NULL; registry->options.cheats_allowed=NULL;
    registry->options.effect=NULL; registry->options.declaration_save_policy=NULL;
    cvar_edit_view *prepared=qac_cvars_edit_view(registry->store->edit,registry);
    cvar_values *sets[2]={&registry->values,prepared?&prepared->values:NULL};
    retire_player_defaults(registry);
    for (size_t i=0;i<2;++i) {
        if (!sets[i] || (i && sets[i]==sets[0])) continue;
        for (cvar *entry=sets[i]->first;entry;entry=entry->next) {
            entry->bound=false; entry->binding=(qa_cvar_binding){0}; entry->binding_order=0;
        }
        for (cvar_alias *alias=sets[i]->aliases;alias;alias=alias->next) {
            alias->bound=false; alias->binding=(qa_cvar_binding){0}; alias->binding_order=0;
        }
    }
    cvar_observer *observer=registry->observers;
    while (observer) {
        cvar_observer *next=observer->next;
        observer->active=false; observer_release(registry,observer); observer=next;
    }
    retire_view_events(registry);
}

void qa_cvars_destroy(qa_cvars *registry)
{
    if (registry && registry->references > 1) { --registry->references; return; }
    if (!registry || registry->mutation_depth || registry->notifying || registry->draining ||
        registry->post_first || registry->edit_first || registry->edit_bindings_pending || registry->edit_scope_depth) return;
    cvar_store *store=registry->store;
    qa_cvars_edit *edit=store->edit;
    if (edit && edit->registry==registry) return;
    if (store->video_owner==registry) {
        store->video_owner=NULL; store->video_resolver=NULL; store->video_user=NULL;
    }
    retire_view_events(registry);
    retire_player_defaults(registry);
    if (store->active_default_source==registry) store->active_default_source=NULL;
    if (edit) {
        if (edit->active_default_source==registry) edit->active_default_source=NULL;
        cvar_edit_view **link=&edit->views;
        while (*link && (*link)->registry!=registry) link=&(*link)->next;
        if (*link) {
            cvar_edit_view *view=*link; *link=view->next;
            if (edit->last_view==view) {
                edit->last_view=edit->views;
                while (edit->last_view && edit->last_view->next) edit->last_view=edit->last_view->next;
            }
            qac_cvars_values_free(&view->values); free(view);
        }
        cvar_edit_event **event=&edit->first;
        while (*event) {
            if ((*event)->registry!=registry) { event=&(*event)->next; continue; }
            cvar_edit_event *retired=*event; *event=retired->next; edit_event_free(registry,retired);
        }
        edit->last=edit->first; while (edit->last && edit->last->next) edit->last=edit->last->next;
    }
    qa_cvars **link=&store->views;
    while (*link!=registry) link=&(*link)->next_view;
    *link=registry->next_view;
    if (store->last_view==registry) {
        store->last_view=store->views;
        while (store->last_view && store->last_view->next_view) store->last_view=store->last_view->next_view;
    }
    qac_cvars_values_free(&registry->values);
    while (registry->observers) {
        cvar_observer *row=registry->observers; registry->observers=row->next;
        free(row->name); free(row);
    }
    free(registry);
    if (!--store->references) { qac_cvars_values_free(&store->values); free(store); }
}

qa_console_dialect qa_cvars_dialect(const qa_cvars *registry)
{
    if (!registry->canonical_root || registry->options.role!=QA_CVAR_ROLE_ENGINE) return registry->options.dialect;
    qa_cvars_edit *edit=qac_cvars_current_edit(registry);
    return edit?edit->active_dialect:registry->store->active_dialect;
}

const qa_cvar_view *qac_cvars_values_find(const qa_cvars *registry,cvar_values *values,const char *name)
{
    const cvar_name_node *node=find_name(registry,values,name);
    if (!node) return NULL;
    return node->alias?alias_view(registry,values,node->owner.alias):qac_cvars_project(registry,values,node->owner.entry);
}
const qa_cvar_view *qa_cvars_find(const qa_cvars *registry,const char *name)
{ return qac_cvars_values_find(registry,qac_cvars_current_values(registry),name); }
static bool source_visible(const qa_cvars *registry,const cvar *entry,bool whole_store)
{
    const cvar *canonical=qac_cvars_canonical((cvar *)entry);
    if (canonical->view.player_scoped && canonical->player != registry->options.seat) return false;
    return whole_store || registry->options.role==QA_CVAR_ROLE_ENGINE || entry->view.declared || entry->view.console_created;
}
const qa_cvar_view *qac_cvars_values_at(const qa_cvars *registry,cvar_values *values,size_t ordinal,bool aliases,bool whole_store)
{
    if (!registry || !values) return NULL;
    bool engine=registry->options.role==QA_CVAR_ROLE_ENGINE;
    bool common=engine || whole_store;
    const cvar_values *entries=common && values->canonical_values?values->canonical_values:values;
    for (cvar *entry=entries?entries->first:NULL;entry;entry=entry->next) {
        if (!source_visible(registry,entry,whole_store) || ordinal--) continue;
        cvar *projection=common?qac_cvars_source_row((qa_cvars *)registry,values,entry,NULL):entry;
        return projection?qac_cvars_project(registry,values,projection):NULL;
    }
    for (cvar_alias *alias=entries?entries->aliases:NULL;alias;alias=alias->next) {
        if (!(aliases || (!common && (alias->declared || alias->console_created))) || ordinal--) continue;
        cvar_alias *projection=common?qac_cvars_source_alias((qa_cvars *)registry,values,alias,NULL):alias;
        return projection?alias_view(registry,values,projection):NULL;
    }
    return NULL;
}
size_t qac_cvars_values_count(const qa_cvars *registry,const cvar_values *values,bool aliases,bool whole_store)
{
    if (!registry || !values) return 0;
    if (registry->options.role==QA_CVAR_ROLE_ENGINE || whole_store) {
        const cvar_values *canonical=values->canonical_values?values->canonical_values:values;
        size_t count=aliases?canonical->alias_count:0;
        for (const cvar *entry=canonical->first;entry;entry=entry->next) count+=source_visible(registry,entry,whole_store);
        return count;
    }
    size_t count=0;
    for (cvar *entry=values->first;entry;entry=entry->next) count+=source_visible(registry,entry,false);
    for (cvar_alias *alias=values->aliases;alias;alias=alias->next) count+=alias->declared || alias->console_created;
    return count;
}
const qa_cvar_view *qa_cvars_at(const qa_cvars *registry,size_t ordinal)
{ return qac_cvars_values_at(registry,qac_cvars_current_values(registry),ordinal,false,false); }
const qa_cvar_view *qa_cvars_next(const qa_cvars *registry,const qa_cvar_view *previous)
{
    if (!registry) return NULL;
    cvar_values *values=qac_cvars_current_values(registry);
    if (!values) return NULL;
    bool engine=registry->options.role==QA_CVAR_ROLE_ENGINE;
    const cvar_values *entries=engine?values->canonical_values:values;
    cvar *entry=entries->first;
    cvar_alias *alias=entries->aliases;
    if (previous) {
        const cvar_name_node *node=find_name(registry,values,previous->name);
        if (!node) return NULL;
        if (node->alias) {
            if (&node->owner.alias->projection!=previous) return NULL;
            entry=NULL; alias=node->owner.alias->next;
        } else {
            if (&node->owner.entry->projection!=previous) return NULL;
            entry=(engine?qac_cvars_canonical(node->owner.entry):node->owner.entry)->next;
        }
    }
    for (;entry;entry=entry->next) {
        if (!source_visible(registry,entry,false)) continue;
        cvar *projection=engine?qac_cvars_source_row((qa_cvars *)registry,values,entry,NULL):entry;
        return projection?qac_cvars_project(registry,values,projection):NULL;
    }
    for (;alias;alias=alias->next)
        if (!engine && (alias->declared || alias->console_created)) return alias_view(registry,values,alias);
    return NULL;
}
const qa_cvar_view *qac_cvars_values_handle(const qa_cvars *registry,cvar_values *values,size_t handle)
{
    const cvar_name_node *node=values && handle<values->next_handle?values->handles[handle]:NULL;
    return !node?NULL:node->alias?alias_view(registry,values,node->owner.alias):qac_cvars_project(registry,values,node->owner.entry);
}
const qa_cvar_view *qa_cvars_handle(const qa_cvars *registry,size_t handle)
{ return qac_cvars_values_handle(registry,qac_cvars_current_values(registry),handle); }

size_t qa_cvars_count(const qa_cvars *registry)
{ return qac_cvars_values_count(registry,qac_cvars_current_values(registry),false,false); }
size_t qa_cvars_handle_count(const qa_cvars *registry)
{ cvar_values *values=qac_cvars_current_values(registry); return values?values->next_handle:0; }
size_t qa_cvars_visible_count(const qa_cvars *registry)
{ return qac_cvars_values_count(registry,qac_cvars_current_values(registry),registry && registry->options.role==QA_CVAR_ROLE_ENGINE,false); }
const qa_cvar_view *qa_cvars_visible_at(const qa_cvars *registry,size_t ordinal)
{ return qac_cvars_values_at(registry,qac_cvars_current_values(registry),ordinal,registry && registry->options.role==QA_CVAR_ROLE_ENGINE,false); }

bool qa_cvars_bind(qa_cvars *registry,const char *name,const qa_cvar_binding *binding,qa_error *error)
{
    if (!qac_cvars_touch(registry,error) || !binding) return false;
    cvar_values *values=qac_cvars_current_values(registry);
    cvar_alias *alias=find_alias(registry,values,name);
    cvar *entry=alias?NULL:qac_cvars_find_values(registry,values,name);
    if (!alias && !entry) return qac_fail(error,QA_ERROR_NOT_FOUND,"binding requires its actual Source name");
    bool *bound=alias?&alias->bound:&entry->bound;
    qa_cvar_binding *actual=alias?&alias->binding:&entry->binding;
    uint64_t *order=alias?&alias->binding_order:&entry->binding_order;
    if (*bound) return qac_fail(error,QA_ERROR_ARGUMENT,"Source name already has a value binding");
    const qa_cvar_view *view=alias?alias_view(registry,values,alias):qac_cvars_project(registry,values,entry);
    if (!view || !validate_value(registry,binding,view->value,error) || !validate_value(registry,binding,view->reset_value,error) ||
        (view->latched_value && !validate_value(registry,binding,view->latched_value,error))) return false;
    if (registry->store->next_binding==UINT64_MAX) return qac_fail(error,QA_ERROR_MEMORY,"canonical binding order exhausted");
    *actual=*binding; *bound=true; *order=++registry->store->next_binding;
    return true;
}
void qa_cvars_unbind(qa_cvars *registry,const char *name,uint64_t owner)
{
    if (!qac_cvars_touch(registry,NULL)) return;
    cvar_values *values=qac_cvars_current_values(registry);
    cvar_alias *alias=find_alias(registry,values,name);
    cvar *entry=alias?NULL:qac_cvars_find_values(registry,values,name);
    if (!alias && !entry) return;
    qa_cvar_binding *binding=alias?&alias->binding:&entry->binding;
    bool *bound=alias?&alias->bound:&entry->bound;
    uint64_t *order=alias?&alias->binding_order:&entry->binding_order;
    if (*bound && binding->owner==owner) { *binding=(qa_cvar_binding){0}; *bound=false; *order=0; }
}

static bool set_variable(cvar_target, const char *, const char *, bool, qa_error *);
static bool apply_latched_variables(cvar_target, const char *, qa_error *);
static cvar_values *canonical_values(cvar_target target)
{ return target.edit?&target.edit->values:&target.registry->store->values; }
static bool convert_input_as(cvar_target target,const qa_cvar_catalog_binding *binding,
    const char *value,const char *current,const qa_cvar_options *grammar,qac_cvar_conversion_output *out,qa_error *error)
{
    cvar_projection_context context={.registry=target.registry,.values=target.values};
    qa_cvar_options options=grammar?*grammar:qac_cvars_view_options(target.registry,target.values);
    const qa_cvar_catalog_conversion *conversion=grammar && binding
        ? &qa_cvar_catalog_conversions[binding->conversion[options.dialect]]
        : qac_cvars_conversion(target.registry,target.values,binding);
    qac_cvar_conversion_input input={.options=&options,
        .conversion=conversion,.binding=binding,.value=value,.current=current,
        .operand=qac_cvars_operand,.video=qac_cvars_video,.user=&context};
    if (binding) {
        cvar *entry=qac_cvars_find_values(target.registry,target.values,
            qa_cvar_catalog_string(qa_cvar_catalog_rows[binding->row_index].name));
        const cvar *canonical=qac_cvars_canonical(entry);
        for (const cvar_detail *detail=canonical?canonical->details:NULL;detail;detail=detail->next)
            if (detail->binding==binding && detail->dialect==options.dialect) { input.detail=detail->value; break; }
    }
    return qac_cvar_write_conversion(&input,out,error);
}
static bool convert_input(cvar_target target,const qa_cvar_catalog_binding *binding,
    const char *value,const char *current,qac_cvar_conversion_output *out,qa_error *error)
{ return convert_input_as(target,binding,value,current,NULL,out,error); }
static bool sync_store_rows(cvar_target target,qa_error *error)
{
    const cvar_values *canonical=canonical_values(target);
    if (target.edit) {
        for (cvar_edit_view *view=target.edit->views;view;view=view->next)
            if (!qac_cvars_view_add(view->registry,canonical,&view->values,error)) return false;
    } else {
        for (qa_cvars *view=target.registry->store->views;view;view=view->next_view)
            if (!qac_cvars_view_add(view,canonical,&view->values,error)) return false;
    }
    return true;
}
static cvar *create_variable(cvar_target target,const char *name,const char *value,
    qa_error *error)
{
    cvar_values *values=canonical_values(target);
    if (!qac_cvars_rows_reserve(values,values->count+1,values->alias_count,error) ||
        !qac_cvars_index_reserve(target.registry,values,values->count+values->alias_count+1,error) ||
        !sync_store_rows(target,error)) return NULL;
    cvar *entry=calloc(1,sizeof(*entry));
    if (!entry) { qac_fail(error,QA_ERROR_MEMORY,"allocating canonical guest cvar"); return NULL; }
    entry->catalog_row=QA_CVAR_CATALOG_NO_ROW; entry->ordinal=values->count;
    entry->view.name=qac_copy(name,error); entry->view.value=qac_copy(value,error);
    entry->view.reset_value=qac_copy(value,error); entry->view.description=qac_copy("",error);
    entry->view.handle=SIZE_MAX;
    qa_cvar_save_policy policy=target.registry->options.declaration_save_policy
        ?target.registry->options.declaration_save_policy(name):QA_CVAR_SAVE_UNCLASSIFIED;
    entry->view.save_policy=policy==QA_CVAR_SAVE_UNCLASSIFIED?target.registry->options.default_save_policy:policy;
    if (!entry->view.name || !entry->view.value || !entry->view.reset_value || !entry->view.description) {
        qac_cvars_entry_free(entry); return NULL;
    }
    cvar *projection=qac_cvars_source_row(target.registry,target.values,entry,error);
    if (!projection) { qac_cvars_entry_free(entry); return NULL; }
    entry->next=values->first; values->first=entry; ++values->count;
    qac_cvars_index_entry(target.registry,values,entry);
    return projection;
}
static bool source_handle(cvar_target target,cvar *entry,cvar_alias *alias,qa_error *error)
{
    size_t *handle=alias?&alias->handle:&entry->view.handle;
    if (*handle!=SIZE_MAX) return true;
    if (target.values->next_handle==SIZE_MAX ||
        (qac_cvars_view_options(target.registry,target.values).dialect==QA_CONSOLE_Q3 && target.values->next_handle>=1024))
        return qac_fail(error,QA_ERROR_MEMORY,"Source cvar handles exhausted");
    if (!qac_cvars_handles_reserve(target.values,target.values->next_handle+1,error)) return false;
    *handle=target.values->next_handle++;
    target.values->handles[*handle]=alias?&alias->indexed_name:&entry->indexed_name;
    if (alias) alias->vm_bound=true;
    return true;
}
static bool register_variable(cvar_target target,const char *name,const char *default_value,
    uint32_t flags,uint64_t owner,const char *description,qa_error *error)
{
    if (!target_touch(target,error) || !name || !default_value) return false;
    qa_cvars *registry=target.registry;
    name=source_name(registry,name);
    if (!qa_cvars_name_valid(qa_cvars_dialect(registry),name)) return qac_fail(error,QA_ERROR_ARGUMENT,"invalid Source cvar name");
    cvar_alias *alias=find_alias(registry,target.values,name);
    cvar *entry=qac_cvars_find_values(registry,target.values,alias?alias->target:name);
    if (!entry) {
        if (registry->options.command_exists) {
            ++registry->notifying; bool exists=registry->options.command_exists(registry->options.user,name); --registry->notifying;
            if (exists) return qac_fail(error,QA_ERROR_ARGUMENT,"cvar name is already a command");
        }
        entry=create_variable(target,name,default_value,error);
        if (!entry) return false;
    }
    const qa_cvar_catalog_binding *binding=alias?alias->catalog_binding:entry->catalog_binding;
    qac_cvar_conversion_output converted;
    if (!convert_input(target,binding,default_value,qac_cvars_canonical(entry)->view.value,&converted,error)) return false;
    qa_console_dialect dialect=qac_cvars_view_options(registry,target.values).dialect;
    uint32_t native_flags=flags|qac_cvars_catalog_flags(registry,target.values,entry->catalog_row,name);
    if (qac_q2(qa_cvars_dialect(registry)) && (native_flags&6u) && (!valid_info(name) || !valid_info(default_value)))
        { free(converted.allocated_value); return qac_fail(error,QA_ERROR_FORMAT,"invalid Source info declaration"); }
    if (!source_handle(target,entry,alias,error)) { free(converted.allocated_value); return false; }
    bool declared=alias?alias->declared:entry->view.declared;
    if (!declared) {
        if (!qac_cvars_default_declare(target,entry,converted.value,error)) { free(converted.allocated_value); return false; }
        if (alias) { alias->declared=true; alias->owner=owner; }
        else { entry->view.declared=true; entry->view.owner=owner; ++target.values->declared_count; }
    }
    if (alias) {
        alias->flags=qac_cvars_flags(alias->flags,alias->flags_dialect,dialect)|native_flags;
        alias->flags_dialect=dialect;
        if (!replace_text((const char **)&alias->description,description?description:"",error)) { free(converted.allocated_value); return false; }
    } else {
        entry->view.flags=qac_cvars_flags(entry->view.flags,entry->flags_dialect,dialect)|native_flags;
        entry->flags_dialect=dialect;
        entry->view.console_created=false;
        if (!replace_text(&entry->view.description,description?description:"",error)) { free(converted.allocated_value); return false; }
    }
    cvar *canonical=qac_cvars_canonical(entry);
    canonical->view.flags|=native_flags&QA_CVAR_ARCHIVE;
    qa_cvar_save_policy policy=registry->options.declaration_save_policy?registry->options.declaration_save_policy(name):QA_CVAR_SAVE_UNCLASSIFIED;
    if (policy==QA_CVAR_SAVE_UNCLASSIFIED) policy=registry->options.default_save_policy;
    if (canonical->view.player_scoped) policy=QA_CVAR_SAVE_SETTING;
    if (policy==QA_CVAR_SAVE_SETTING || canonical->view.save_policy==QA_CVAR_SAVE_UNCLASSIFIED) canonical->view.save_policy=policy;
    entry->view.save_policy=canonical->view.save_policy;
    qac_cvars_refresh(registry,entry);
    free(converted.allocated_value);
    return true;
}
static bool write_policy(cvar_target target,cvar_write *write,const char *name,
    uint32_t flags,bool force,bool *accepted,qa_error *error)
{
    qa_cvars *registry=target.registry;
    cvar *canonical=qac_cvars_canonical(write->entry);
    qa_console_dialect dialect=qac_cvars_view_options(registry,target.values).dialect;
    bool q2=qac_q2(dialect);
    if (registry->options.role==QA_CVAR_ROLE_ENGINE) {
        const cvar_alias *alias=find_alias(registry,target.values,name);
        const cvar *entry=qac_cvars_find_values(registry,target.values,alias?alias->target:name);
        if (entry) flags=qac_cvars_flags(flags,alias?alias->flags_dialect:entry->flags_dialect,dialect)|
            canonical->view.flags|qac_cvars_catalog_flags(registry,target.values,entry->catalog_row,name);
    }
    *accepted=false;
    if (!force) {
        uint32_t readonly=q2?(uint32_t)QA_Q2_CVAR_READONLY:(uint32_t)QA_CVAR_READONLY;
        uint32_t init=q2?(uint32_t)QA_Q2_CVAR_NOSET:(uint32_t)QA_CVAR_INIT;
        uint32_t cheat=q2?(uint32_t)QA_Q2_CVAR_CHEAT:(uint32_t)QA_CVAR_CHEAT;
        uint32_t latch=q2?(uint32_t)QA_Q2_CVAR_LATCH:(uint32_t)QA_CVAR_LATCH;
        if (flags&readonly) { print_message(target,name," is read only.\n"); return true; }
        if (flags&init) { print_message(target,name," is write protected.\n"); return true; }
        if ((flags&cheat) && !cheats_allowed(target)) { print_message(target,name," is cheat protected.\n"); return true; }
        write->pending|=(flags&latch) && (!q2 || target.values->server_active ||
            (canonical->catalog_row!=QA_CVAR_CATALOG_NO_ROW &&
             (qa_cvar_catalog_rows[canonical->catalog_row].policies&QA_CATALOG_POLICY_LATCH_ALL)));
    }
    if (q2 && (flags&6u) && !valid_info(write->value)) return qac_fail(error,QA_ERROR_FORMAT,"invalid Source info value");
    write->mark=strcmp(write->pending && canonical->view.latched_value?canonical->view.latched_value:canonical->view.value,write->value)!=0;
    if (write->detail_binding) {
        const cvar_detail *detail=canonical->details;
        while (detail && (detail->binding!=write->detail_binding || detail->dialect!=write->detail_dialect)) detail=detail->next;
        const char *previous=detail?(write->pending?detail->latched_value:detail->value):NULL;
        if ((!previous)!=(!write->detail_value) || (previous && strcmp(previous,write->detail_value))) write->mark=true;
    }
    write->clear_latch=force && !write->pending;
    *accepted=true;
    return true;
}
static bool write_group(cvar_target target,cvar_write *writes,size_t count,qa_error *error)
{
    /* All publications and noninvertible details are admitted before changing
     * the one scalar owner. Source callbacks run after the whole group commits. */
    size_t prepared=0;
    for (;prepared<count;++prepared) if (!write_prepare(target,&writes[prepared],error)) goto failed;
    typedef struct write_previous { const char *value, *reset, *latch; cvar_detail *details; cvar_detail proposed; } write_previous;
    write_previous retained[33];
    write_previous *previous=count<=33?retained:calloc(count,sizeof(*previous));
    if (!previous) { qac_fail(error,QA_ERROR_MEMORY,"admitting canonical value group"); goto failed; }
    for (size_t i=0;i<count;++i) {
        cvar *canonical=qac_cvars_canonical(writes[i].entry);
        previous[i].value=canonical->view.value; previous[i].reset=canonical->view.reset_value; previous[i].latch=canonical->view.latched_value; previous[i].details=canonical->details;
        canonical->view.value=writes[i].reuse_value?writes[i].value:writes[i].owned_value;
        if (writes[i].owned_reset) canonical->view.reset_value=writes[i].owned_reset;
        if (writes[i].clear_latch) canonical->view.latched_value=NULL;
        if (writes[i].owned_latch) canonical->view.latched_value=writes[i].owned_latch;
        if (writes[i].clear_details) canonical->details=NULL;
        if (writes[i].promote_details) canonical->details=writes[i].projected_details;
        if (writes[i].owned_detail) {
            previous[i].proposed=writes[i].detail?*writes[i].detail:*writes[i].allocated_detail;
            previous[i].proposed.value=writes[i].owned_detail; previous[i].proposed.next=canonical->details; canonical->details=&previous[i].proposed;
        }
    }
    bool okay=true;
    for (size_t i=0;okay && i<count;++i) {
        for (cvar_edit_event *event=writes[i].events;event;event=event->next) {
            cvar_values *values=target.edit?&qac_cvars_edit_view(target.edit,event->registry)->values:&event->registry->values;
            cvar_alias *alias=find_alias(event->registry,values,event->snapshot->view.name);
            cvar *entry=alias?NULL:qac_cvars_find_values(event->registry,values,event->snapshot->view.name);
            const qa_cvar_view *projected=alias?alias_view(event->registry,values,alias):qac_cvars_project(event->registry,values,entry);
            if (!projected || !replace_text(&event->snapshot->view.value,projected->value,error) ||
                !replace_text(&event->snapshot->view.reset_value,projected->reset_value,error)) { okay=false; break; }
            numbers(event->registry,event->snapshot);
        }
    }
    for (size_t i=0;i<count;++i) {
        cvar *canonical=qac_cvars_canonical(writes[i].entry);
        canonical->view.value=previous[i].value; canonical->view.reset_value=previous[i].reset; canonical->view.latched_value=previous[i].latch; canonical->details=previous[i].details;
        for (qa_cvars *view=target.registry->store->views;view;view=view->next_view) {
            cvar_values *values=target.edit?&qac_cvars_edit_view(target.edit,view)->values:&view->values;
            qac_cvars_refresh(view,values->rows[canonical->ordinal]);
        }
    }
    if (previous!=retained) free(previous);
    if (!okay) goto failed;
    for (size_t i=0;i<count;++i)
        for (cvar_edit_event *event=writes[i].events;event;event=event->next)
            if (event->snapshot->bound && (!validate_value(event->registry,&event->snapshot->binding,event->snapshot->view.value,error) ||
                (writes[i].reset_value && !validate_value(event->registry,&event->snapshot->binding,event->snapshot->view.reset_value,error)))) goto failed;
    for (size_t i=0;i<count;++i) write_commit(target,&writes[i]);
    write_projection_metadata(target,writes,count);
    cvar_write published={0};
    for (size_t i=0;i<count;++i) {
        while (writes[i].events) {
            cvar_edit_event *event=writes[i].events; writes[i].events=event->next;
            event->changed=writes[i].mark;
            if (writes[i].pending) { edit_event_free(event->registry,event); continue; }
            cvar_edit_event **link=&published.events;
            bool duplicate=false;
            for (cvar_edit_event *kept=published.events;kept;kept=kept->next)
                if (kept->registry==event->registry && qac_equal(kept->snapshot->view.name,event->snapshot->view.name)) { kept->changed|=event->changed; duplicate=true; break; }
            if (duplicate) { edit_event_free(event->registry,event); continue; }
            while (*link && (*link)->snapshot->binding_order<=event->snapshot->binding_order) link=&(*link)->next;
            event->next=*link; *link=event;
        }
        write_dispose(&writes[i]);
    }
    if (!target.edit)
        for (cvar_edit_event *event=published.events;event;event=event->next)
            changed_value(event->registry,event->snapshot,event->snapshot->view.value);
    write_deliver(target,&published);
    return true;
failed:
    for (size_t i=0;i<prepared;++i) write_dispose(&writes[i]);
    return false;
}
bool qac_cvars_change_defaults(cvar_target target,const char *const *defaults,qa_error *error)
{
    cvar_values *canonical=canonical_values(target);
    cvar_write *writes=canonical->count?calloc(canonical->count,sizeof(*writes)):NULL;
    if (canonical->count && !writes) return qac_fail(error,QA_ERROR_MEMORY,"admitting active cvar defaults");
    size_t count=0;
    for (cvar *entry=canonical->first;entry;entry=entry->next) {
        const char *reset=defaults[entry->ordinal];
        if (!reset || (entry->catalog_row!=QA_CVAR_CATALOG_NO_ROW && qa_cvar_catalog_rows[entry->catalog_row].not_stored)) continue;
        const char *value=entry->view.explicit_value?entry->view.value:reset;
        if (!strcmp(reset,entry->view.reset_value) && !strcmp(value,entry->view.value)) continue;
        writes[count++]=(cvar_write){.entry=entry,.value=value,.reset_value=reset,
            .latch_reset=entry->view.latched_value && !entry->pending_explicit?reset:NULL,
            .explicit_value=entry->view.explicit_value,.mark=strcmp(value,entry->view.value)!=0};
    }
    if (target.registry->mutation_depth==SIZE_MAX) { free(writes); return qac_fail(error,QA_ERROR_MEMORY,"cvar default mutation depth exhausted"); }
    ++target.registry->mutation_depth;
    bool okay=write_group(target,writes,count,error);
    --target.registry->mutation_depth;
    free(writes);
    if (!target.edit)
        for (qa_cvars *view=target.registry->store->views;view;view=view->next_view)
            if (!post_drain(view,error)) okay=false;
    return okay;
}

static bool set_canonical(cvar_target target,cvar *entry,const char *name,const char *value,
    uint32_t flags,bool force,bool explicit_value,qa_error *error)
{
    cvar_write write={.entry=entry,.value=value,.clear_details=true,.explicit_value=explicit_value};
    bool accepted=false;
    if (!write_policy(target,&write,name,flags,force,&accepted,error)) return false;
    return !accepted || write_group(target,&write,1,error);
}
static bool apply_converted_as(cvar_target target,const char *name,const char *value,
    bool force,bool staged,bool silent,const qa_cvar_options *grammar,bool canonical_value,qa_error *error)
{
    qa_cvars *registry=target.registry;
    cvar_alias *alias=find_alias(registry,target.values,name);
    cvar *entry=qac_cvars_find_values(registry,target.values,alias?alias->target:name);
    if (!entry) {
        if (qac_q1(qa_cvars_dialect(registry)) && registry->options.role!=QA_CVAR_ROLE_ENGINE)
            return qac_fail(error,QA_ERROR_NOT_FOUND,"Q1 Source cvar is not registered");
        if (!qa_cvars_name_valid(qa_cvars_dialect(registry),name)) return qac_fail(error,QA_ERROR_ARGUMENT,"invalid cvar name");
        entry=create_variable(target,name,"",error);
        if (!entry) return false;
        entry->view.console_created=true;
        entry->view.flags=qa_cvars_dialect(registry)==QA_CONSOLE_Q3?QA_CVAR_USER_CREATED:qac_q2(qa_cvars_dialect(registry))?QA_Q2_CVAR_CUSTOM:0;
        entry->flags_dialect=qac_cvars_view_options(registry,target.values).dialect;
    }
    cvar *canonical=qac_cvars_canonical(entry);
    const qa_cvar_catalog_binding *binding=alias?alias->catalog_binding:entry->catalog_binding;
    qac_cvar_conversion_output converted;
    if (canonical_value) converted=(qac_cvar_conversion_output){.value=value};
    else if (!convert_input_as(target,binding,value,canonical->view.value,grammar,&converted,error)) return false;
    cvar_write writes[33]={0}; size_t count=0;
    uint32_t flags=alias?alias->flags:entry->view.flags;
    for (size_t i=0;i<converted.change_count;++i) {
        const qac_cvar_change *change=&converted.changes[i];
        const char *operand_name=qa_cvar_catalog_string(qa_cvar_catalog_rows[change->row].name);
        cvar *operand=qac_cvars_find_values(registry,target.values,operand_name);
        if (!operand) { free(converted.allocated_value); return qac_fail(error,QA_ERROR_FORMAT,"composite cvar lost its canonical operand"); }
        writes[count++]=(cvar_write){.entry=operand,.value=change->value,.pending=staged,.clear_details=true,.explicit_value=true};
    }
    if (canonical->catalog_row==QA_CVAR_CATALOG_NO_ROW || !qa_cvar_catalog_rows[canonical->catalog_row].not_stored) {
        writes[count++]=(cvar_write){.entry=entry,.value=converted.value,.pending=staged,
            .clear_details=!alias || !converted.detail,.explicit_value=true,
            .detail_binding=converted.detail?binding:NULL,
            .detail_dialect=grammar?grammar->dialect:qac_cvars_view_options(registry,target.values).dialect,
            .detail_value=converted.detail?(converted.detail_value?converted.detail_value:value):NULL};
    }
    for (size_t i=0;i<count;++i) {
        bool accepted=false;
        if (!write_policy(target,&writes[i],name,flags,force,&accepted,error)) { free(converted.allocated_value); return false; }
        if (!accepted) { free(converted.allocated_value); return true; }
        writes[i].silent=silent;
        if (silent) { writes[i].clear_latch=false; writes[i].mark=!qac_q1(qa_cvars_dialect(registry)); }
    }
    bool okay=write_group(target,writes,count,error);
    if (okay && count && writes[0].pending) print_message(target,name," will be changed upon restarting.\n");
    free(converted.allocated_value);
    return okay;
}
static bool apply_converted(cvar_target target,const char *name,const char *value,
    bool force,bool staged,qa_error *error)
{ return apply_converted_as(target,name,value,force,staged,false,NULL,false,error); }

bool qac_cvars_restore_row(qa_cvars_edit *edit,const char *name,const char *value,
    const char *latch,qa_console_dialect dialect,bool canonical_value,qa_error *error)
{
    cvar_edit_view *view=qac_cvars_edit_view(edit,edit->registry);
    cvar_target target={edit->registry,&view->values,edit};
    qa_cvar_options grammar=edit->registry->options;
    grammar.dialect=dialect; grammar.role=QA_CVAR_ROLE_GAME;
    if (!target_touch(target,error) ||
        !apply_converted_as(target,name,value,true,false,false,canonical_value?NULL:&grammar,canonical_value,error)) return false;
    if (latch && !apply_converted_as(target,name,latch,true,true,false,canonical_value?NULL:&grammar,canonical_value,error)) return false;
    return true;
}

static bool set_variable(cvar_target target,const char *name,const char *value,bool force,qa_error *error)
{
    if (!target_touch(target,error) || !name || !value) return false;
    return apply_converted(target,source_name(target.registry,name),value,force,false,error);
}
static bool set_console_variable(cvar_target target,const char *name,const char *value,qa_error *error)
{ return set_variable(target,name,value,false,error); }

static bool set_number_variable(cvar_target target, const char *name, float value, qa_error *error)
{
    qa_cvars *registry=target.registry;
    if (!target_touch(target, error)) return false;
    if (registry == NULL || !isfinite(value))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar numeric set requires a finite float");
    char text[32]; bool truncated=false;
    if (!format_number(qa_cvars_dialect(registry),value,text,&truncated,error)) return false;
    if (truncated)
        print_message(target, NULL, "numeric cvar truncated to source value buffer\n");
    return set_variable(target, name, text, qa_cvars_dialect(registry) == QA_CONSOLE_Q3, error);
}

static bool full_set_variable(cvar_target target,const char *name,const char *value,
    uint32_t flags,qa_error *error)
{
    if (!target_touch(target,error) || !qac_q2(qac_cvars_view_options(target.registry,target.values).dialect) || !name || !value)
        return qac_fail(error,QA_ERROR_ARGUMENT,"full set requires its actual Q2 Source view");
    if (!apply_converted(target,name,value,true,false,error)) return false;
    cvar_alias *alias=find_alias(target.registry,target.values,name);
    cvar *entry=qac_cvars_find_values(target.registry,target.values,alias?alias->target:name);
    qa_console_dialect dialect=qac_cvars_view_options(target.registry,target.values).dialect;
    if (alias) {
        alias->flags=flags|qac_cvars_catalog_flags(target.registry,target.values,entry->catalog_row,name);
        alias->flags_dialect=dialect;
    } else {
        entry->view.flags=flags|qac_cvars_catalog_flags(target.registry,target.values,entry->catalog_row,name);
        entry->flags_dialect=dialect;
    }
    qac_cvars_canonical(entry)->view.flags|=flags&QA_CVAR_ARCHIVE;
    return true;
}
static bool set_flags_variable(cvar_target target,const char *name,const char *value,
    uint32_t flag,qa_error *error)
{
    if (!target_touch(target,error) || !name || !value ||
        (flag!=QA_CVAR_ARCHIVE && flag!=QA_CVAR_USERINFO && flag!=QA_CVAR_SERVERINFO))
        return qac_fail(error,QA_ERROR_ARGUMENT,"invalid Source cvar command flag");
    if (flag!=QA_CVAR_ARCHIVE && !valid_info(value)) return qac_fail(error,QA_ERROR_FORMAT,"invalid Source info value");
    if (!set_console_variable(target,name,value,error)) return false;
    cvar_alias *alias=find_alias(target.registry,target.values,name);
    cvar *entry=qac_cvars_find_values(target.registry,target.values,alias?alias->target:name);
    if (alias) alias->flags|=flag;
    else entry->view.flags|=flag;
    qac_cvars_canonical(entry)->view.flags|=flag&QA_CVAR_ARCHIVE;
    if (flag&QA_CVAR_USERINFO) target.values->userinfo_modified=true;
    return true;
}
static bool stage_variable(cvar_target target,const char *name,const char *value,qa_error *error)
{
    if (!target_touch(target,error) || !name || !value) return false;
    return apply_converted(target,name,value,false,true,error);
}
static bool apply_latched_variables(cvar_target target,const char *name,qa_error *error)
{
    if (!target_touch(target,error)) return false;
    name=canonical_name(target.registry,target.values,name);
    cvar *entries=target.registry->options.role==QA_CVAR_ROLE_ENGINE?canonical_values(target)->first:target.values->first;
    for (cvar *entry=entries;entry;entry=entry->next) {
        cvar *canonical=qac_cvars_canonical(entry);
        if ((canonical->view.player_scoped && canonical->player!=target.registry->options.seat) ||
            !canonical->view.latched_value || (name && !qac_cvars_name_equal(target.registry,name,entry->view.name))) continue;
        cvar_write write={.entry=entry,.value=canonical->view.latched_value,.clear_latch=true,.promote_details=true,
            .explicit_value=canonical->pending_explicit,.mark=strcmp(canonical->view.value,canonical->view.latched_value)!=0};
        if (!write_group(target,&write,1,error)) return false;
    }
    return true;
}
static bool reset_variable(cvar_target target,const char *name,bool force,qa_error *error)
{
    if (!target_touch(target,error)) return false;
    cvar_alias *alias=find_alias(target.registry,target.values,name);
    cvar *entry=qac_cvars_find_values(target.registry,target.values,alias?alias->target:name);
    if (!entry) return qac_fail(error,QA_ERROR_NOT_FOUND,"reset requires its canonical row");
    cvar *canonical=qac_cvars_canonical(entry);
    if (canonical->catalog_row!=QA_CVAR_CATALOG_NO_ROW && qa_cvar_catalog_rows[canonical->catalog_row].not_stored) {
        const qa_cvar_catalog_conversion *conversion=qac_cvars_conversion(target.registry,target.values,alias?alias->catalog_binding:entry->catalog_binding);
        cvar_write writes[33]={0}; size_t count=0;
        for (size_t i=0;conversion && i<conversion->operand_count;++i) {
            const char *operand_name=qa_cvar_catalog_string(qa_cvar_catalog_rows[qa_cvar_catalog_operands[conversion->operand_first+i].row_index].name);
            cvar *operand=qac_cvars_find_values(target.registry,target.values,operand_name);
            if (!operand) return qac_fail(error,QA_ERROR_FORMAT,"reset lost its composite operand");
            writes[count++]=(cvar_write){.entry=operand,.value=qac_cvars_canonical(operand)->view.reset_value,.clear_details=true};
            bool accepted=false;
            if (!write_policy(target,&writes[count-1],name,alias?alias->flags:entry->view.flags,force,&accepted,error)) return false;
            if (!accepted) return true;
        }
        return write_group(target,writes,count,error);
    }
    return set_canonical(target,entry,name,canonical->view.reset_value,alias?alias->flags:entry->view.flags,force,false,error);
}
static bool restart_variables(cvar_target target,qa_error *error)
{
    if (!target_touch(target,error)) return false;
    for (cvar *entry=target.values->first;entry;entry=entry->next) {
        const qa_cvar_view *projection=qac_cvars_project(target.registry,target.values,entry);
        if (!source_visible(target.registry,entry,false) || !projection ||
            (qac_cvars_flags(projection->flags,qac_cvars_view_options(target.registry,target.values).dialect,QA_CONSOLE_Q3)&
             (QA_CVAR_READONLY|QA_CVAR_INIT|QA_CVAR_NO_RESTART))) continue;
        if (!reset_variable(target,entry->view.name,true,error)) return false;
    }
    return true;
}
static bool set_cheats_variables(cvar_target target,bool allowed,qa_error *error)
{
    if (!target_touch(target,error)) return false;
    target.values->cheats=allowed;
    if (allowed) return true;
    uint32_t cheat=qac_q2(qac_cvars_view_options(target.registry,target.values).dialect)?(uint32_t)QA_Q2_CVAR_CHEAT:(uint32_t)QA_CVAR_CHEAT;
    for (cvar *entry=target.values->first;entry;entry=entry->next) {
        const qa_cvar_view *projection=qac_cvars_project(target.registry,target.values,entry);
        if (projection && (projection->flags&cheat) && !reset_variable(target,entry->view.name,true,error)) return false;
    }
    return true;
}

bool qa_cvars_register(qa_cvars *registry, const char *name, const char *value,
    uint32_t flags, uint64_t owner, const char *description, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_REGISTER,
        .name=name,.value=value,.flags=flags,.owner=owner,.description=description},error);
}
static bool add_flags_variable(cvar_target target,const char *name,uint32_t flags,qa_error *error)
{
    if (!target_touch(target,error)) return false;
    cvar_alias *alias=find_alias(target.registry,target.values,name);
    cvar *entry=qac_cvars_find_values(target.registry,target.values,alias?alias->target:name);
    if (!entry) return qac_fail(error,QA_ERROR_NOT_FOUND,"flags need their actual Source declaration");
    qa_console_dialect dialect=qac_cvars_view_options(target.registry,target.values).dialect;
    if (alias) {
        alias->flags=qac_cvars_flags(alias->flags,alias->flags_dialect,dialect)|flags;
        alias->flags_dialect=dialect;
    } else {
        entry->view.flags=qac_cvars_flags(entry->view.flags,entry->flags_dialect,dialect)|flags;
        entry->flags_dialect=dialect;
    }
    qac_cvars_canonical(entry)->view.flags|=flags&QA_CVAR_ARCHIVE;
    return true;
}

bool qa_cvars_add_flags(qa_cvars *registry,const char *name,uint32_t flags,qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_ADD_FLAGS,
        .name=name,.flags=flags},error);
}
static bool declare_save_policy(cvar_target target,const char *name,
    qa_cvar_save_policy policy,qa_error *error)
{
    if (policy!=QA_CVAR_SAVE_GAMEPLAY && policy!=QA_CVAR_SAVE_SETTING)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar Source save policy is not declared");
    if (!target_touch(target,error)) return false;
    cvar *entry=name?qac_cvars_find_values(target.registry,target.values,
        canonical_name(target.registry,target.values,name)):NULL;
    if (!entry) return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar save policy needs its actual Source declaration");
    cvar *canonical=qac_cvars_canonical(entry);
    if (canonical->view.player_scoped) policy=QA_CVAR_SAVE_SETTING;
    if (policy==QA_CVAR_SAVE_SETTING || canonical->view.save_policy!=QA_CVAR_SAVE_SETTING) canonical->view.save_policy=policy;
    entry->view.save_policy=canonical->view.save_policy;
    return true;
}
bool qa_cvars_declare_save_policy(qa_cvars *registry,const char *name,
    qa_cvar_save_policy policy,qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SAVE_POLICY,
        .name=name,.save_policy=policy},error);
}
static bool vm_handle_variable(cvar_target target,const char *name,size_t *handle,qa_error *error)
{
    cvar_alias *alias=find_alias(target.registry,target.values,name);
    cvar *entry=qac_cvars_find_values(target.registry,target.values,alias?alias->target:name);
    if (!entry || !handle) return qac_fail(error,QA_ERROR_NOT_FOUND,"VM Source name is absent");
    if (!source_handle(target,entry,alias,error)) return false;
    *handle=alias?alias->handle:entry->view.handle;
    return true;
}
static bool vm_bind_variable(cvar_target target,const char *name,const char *default_value,
    uint32_t flags,uint64_t owner,size_t *handle,qa_error *error)
{
    return register_variable(target,name,default_value,flags,owner,NULL,error) && vm_handle_variable(target,name,handle,error);
}

bool qa_cvars_vm_rebind(qa_cvars *registry,const char *name,size_t *handle,qa_error *error)
{
    if (!registry || !name || !handle)
        return qac_fail(error,QA_ERROR_ARGUMENT,"VM cvar rebind requires its actual current registry and name");
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,vm_handle_variable(live_target(registry),source_name(registry,name),
        handle,error),error);
}
bool qa_cvars_vm_bind(qa_cvars *registry,const char *name,const char *default_value,
    uint32_t flags,uint64_t owner,size_t *handle,qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,vm_bind_variable(live_target(registry),name,default_value,
        flags,owner,handle,error),error);
}
bool qa_cvars_edit_vm_bind(qa_cvars_edit *edit,const char *name,const char *default_value,
    uint32_t flags,uint64_t owner,size_t *handle,qa_error *error)
{
    if (!edit) return qac_fail(error,QA_ERROR_ARGUMENT,"VM cvar binding requires its prepared ticket");
    qa_error fault={0};
    bool ok=vm_bind_variable((cvar_target){edit->registry,&qac_cvars_edit_view(edit,edit->registry)->values,edit},name,default_value,
        flags,owner,handle,&fault);
    if (!ok) {
        if (edit->fault.code==QA_OK) edit->fault=fault;
        if (error && error->code==QA_OK) *error=fault;
    }
    return ok;
}
static bool assign_variable(cvar_target target,const char *name,const char *value,
    qa_console_dialect source_dialect,qa_error *error)
{
    if (!name || !value || !qac_dialect_valid(source_dialect) || !target_touch(target,error)) return false;
    name=source_name(target.registry,name);
    if (!qa_cvars_find(target.registry,name)) return qac_fail(error,QA_ERROR_NOT_FOUND,"direct cvar assignment requires an existing registration");
    qa_cvar_options grammar=target.registry->options; grammar.dialect=source_dialect;
    return apply_converted_as(target,name,value,true,false,true,&grammar,false,error);
}

bool qa_cvars_assign(qa_cvars *registry, const char *name, const char *value,
    qa_console_dialect source_dialect, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_ASSIGN,
        .name=name,.value=value,.source_dialect=source_dialect,.force=true},error);
}
bool qa_cvars_set(qa_cvars *registry, const char *name, const char *value, bool force, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,.name=name,.value=value,.force=force},error);
}
bool qa_cvars_set_console(qa_cvars *registry, const char *name, const char *value, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,.name=name,.value=value},error);
}
bool qa_cvars_set_number(qa_cvars *registry, const char *name, float value, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_NUMBER,.name=name,.number=value},error);
}
bool qa_cvars_full_set(qa_cvars *registry, const char *name, const char *value, uint32_t flags, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_FULL_SET,.name=name,.value=value,.flags=flags},error);
}
bool qa_cvars_set_flags(qa_cvars *registry, const char *name, const char *value, uint32_t flags, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_FLAGS,.name=name,.value=value,.flags=flags},error);
}
bool qa_cvars_stage(qa_cvars *registry, const char *name, const char *value, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_STAGE,.name=name,.value=value},error);
}
bool qa_cvars_apply_latched(qa_cvars *registry, const char *name, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_APPLY_LATCHED,.name=name},error);
}
bool qa_cvars_reset(qa_cvars *registry, const char *name, bool force, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RESET,.name=name,.force=force},error);
}
bool qa_cvars_restart(qa_cvars *registry, qa_error *error)
{
    return qa_cvars_apply(registry,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RESTART},error);
}
bool qa_cvars_set_cheats(qa_cvars *registry, bool allowed, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,set_cheats_variables(live_target(registry),allowed,error),error);
}

void qac_cvars_values_free(cvar_values *values)
{
    free(values->rows); free(values->alias_rows); free(values->handles);
    free(values->name_buckets);
    while (values->first) {
        cvar *entry=values->first;
        values->first=entry->next; qac_cvars_entry_free(entry);
    }
    while (values->aliases) {
        cvar_alias *alias=values->aliases;
        values->aliases=alias->next; qac_cvars_alias_free(alias);
    }
    *values=(cvar_values){0};
}
bool qa_cvars_edit_prepare(qa_cvars *registry,qa_cvars_edit **out,qa_error *error)
{
    if (!registry || !out || *out || registry->store->edit || registry->store->revision==UINT64_MAX)
        return qac_fail(error,QA_ERROR_ARGUMENT,"preparing cvar values requires its available owner");
    for (qa_cvars *view=registry->store->views;view;view=view->next_view)
        if (!qa_cvars_observer_idle(view)) return qac_fail(error,QA_ERROR_ARGUMENT,"prepared values require returned Source views");
    qa_cvars_edit *edit=calloc(1,sizeof(*edit));
    if (!edit) return qac_fail(error,QA_ERROR_MEMORY,"allocating prepared canonical values");
    edit->registry=registry; edit->revision=registry->store->revision;
    edit->active_dialect=registry->store->active_dialect;
    edit->active_default_source=registry->store->active_default_source;
    if (!qac_cvars_values_clone(registry,&registry->store->values,&edit->values,NULL,error)) { free(edit); return false; }
    for (qa_cvars *view=registry->store->views;view;view=view->next_view)
        if (!qac_cvars_edit_add_view(edit,view,error)) { qa_cvars_edit_abort(edit); return false; }
    registry->store->edit=edit; registry->ready_edit=edit; *out=edit; return true;
}
qa_cvars *qa_cvars_edit_registry(const qa_cvars_edit *edit)
{ return edit?edit->registry:NULL; }
bool qa_cvars_edit_enter(qa_cvars_edit *edit,qa_cvars *registry,qa_error *error)
{
    if (!edit || !registry || registry->store!=edit->registry->store || registry->store->edit!=edit ||
        edit->ready || edit->fault.code!=QA_OK || registry->edit_scope_depth==SIZE_MAX ||
        registry->store->revision!=edit->revision)
        return qac_fail(error,QA_ERROR_ARGUMENT,"enter cvar values with their actual prepared owner");
    for (qa_cvars *view=registry->store->views;view;view=view->next_view)
        if (view->notifying || view->draining) return qac_fail(error,QA_ERROR_ARGUMENT,"cvar callbacks cannot enter prepared values");
    if (!qac_cvars_edit_add_view(edit,registry,error)) return false;
    if (registry==edit->registry) {
        if (edit->owner_scope_depth==SIZE_MAX) return qac_fail(error,QA_ERROR_MEMORY,"prepared owner scope exhausted");
        ++edit->owner_scope_depth;
    }
    ++registry->edit_scope_depth; registry->entered_edit=edit;
    return true;
}
bool qa_cvars_edit_leave(qa_cvars_edit *edit,qa_cvars *registry,qa_error *error)
{
    if (!edit || !registry || registry->entered_edit!=edit || !registry->edit_scope_depth ||
        registry->mutation_depth || registry->notifying || registry->draining || registry->post_first)
        return qac_fail(error,QA_ERROR_ARGUMENT,"leave prepared values after the entered Source callbacks return");
    if (registry==edit->registry) --edit->owner_scope_depth;
    if (!--registry->edit_scope_depth) registry->entered_edit=NULL;
    return true;
}
static cvar_values *edit_source_values(const qa_cvars_edit *edit)
{ cvar_edit_view *view=qac_cvars_edit_view((qa_cvars_edit *)edit,edit?edit->registry:NULL); return view?&view->values:NULL; }
const qa_cvar_view *qa_cvars_edit_find(const qa_cvars_edit *edit,const char *name)
{ return qac_cvars_values_find(edit?edit->registry:NULL,edit_source_values(edit),name); }
const qa_cvar_view *qa_cvars_edit_at(const qa_cvars_edit *edit,size_t ordinal)
{ return qac_cvars_values_at(edit?edit->registry:NULL,edit_source_values(edit),ordinal,false,false); }
const qa_cvar_view *qa_cvars_edit_handle(const qa_cvars_edit *edit,size_t handle)
{ return qac_cvars_values_handle(edit?edit->registry:NULL,edit_source_values(edit),handle); }

size_t qa_cvars_edit_count(const qa_cvars_edit *edit)
{ return qac_cvars_values_count(edit?edit->registry:NULL,edit_source_values(edit),false,false); }
size_t qa_cvars_edit_handle_count(const qa_cvars_edit *edit)
{ cvar_values *values=edit_source_values(edit); return values?values->next_handle:0; }
size_t qa_cvars_edit_visible_count(const qa_cvars_edit *edit)
{ return qac_cvars_values_count(edit?edit->registry:NULL,edit_source_values(edit),edit && edit->registry->options.role==QA_CVAR_ROLE_ENGINE,false); }
const qa_cvar_view *qa_cvars_edit_visible_at(const qa_cvars_edit *edit,size_t ordinal)
{ return qac_cvars_values_at(edit?edit->registry:NULL,edit_source_values(edit),ordinal,edit && edit->registry->options.role==QA_CVAR_ROLE_ENGINE,false); }
static bool apply_operation(cvar_target target,const qa_cvars_edit_command *command,qa_error *error)
{
    switch (command->kind) {
    case QA_CVARS_EDIT_REGISTER:
        return register_variable(target,command->name,command->value,command->flags,
            command->owner,command->description,error) &&
            (command->save_policy == QA_CVAR_SAVE_UNCLASSIFIED ||
             declare_save_policy(target,command->name,command->save_policy,error));
    case QA_CVARS_EDIT_SET:
        return set_variable(target,command->name,command->value,command->force,error);
    case QA_CVARS_EDIT_ASSIGN:
        return command->force ? assign_variable(target,command->name,command->value,command->source_dialect,error) :
            qac_fail(error,QA_ERROR_ARGUMENT,"direct cvar assignment requires explicit force");
    case QA_CVARS_EDIT_SAVE_POLICY:
        return declare_save_policy(target,command->name,command->save_policy,error);
    case QA_CVARS_EDIT_SET_CONSOLE:
        return set_console_variable(target,command->name,command->value,error);
    case QA_CVARS_EDIT_SET_FLAGS:
        return set_flags_variable(target,command->name,command->value,command->flags,error);
    case QA_CVARS_EDIT_ADD_FLAGS:
        return add_flags_variable(target,command->name,command->flags,error);
    case QA_CVARS_EDIT_FULL_SET:
        return full_set_variable(target,command->name,command->value,command->flags,error);
    case QA_CVARS_EDIT_STAGE:
        return stage_variable(target,command->name,command->value,error);
    case QA_CVARS_EDIT_APPLY_LATCHED:
        return apply_latched_variables(target,command->name,error);
    case QA_CVARS_EDIT_RESET:
        return reset_variable(target,command->name,command->force,error);
    case QA_CVARS_EDIT_RESTART:
        return restart_variables(target,error);
    case QA_CVARS_EDIT_SET_NUMBER:
        return set_number_variable(target,command->name,command->number,error);
    case QA_CVARS_EDIT_RETAIN_SHARED:
        return retain_shared_variable(target,command->name,error);
    }
    return qac_fail(error,QA_ERROR_ARGUMENT,target.edit ?
        "unknown prepared cvar operation" : "unknown routed cvar operation");
}
bool qa_cvars_apply(qa_cvars *registry,const qa_cvars_edit_command *command,qa_error *error)
{
    if (!command) return qac_fail(error,QA_ERROR_ARGUMENT,"live cvar mutation requires its operation");
    /* Forced assignment admission and unknown packets precede live mutation;
     * shared-owner retention has no notification drain. */
    if ((unsigned)command->kind>(unsigned)QA_CVARS_EDIT_ASSIGN ||
        (command->kind==QA_CVARS_EDIT_ASSIGN && !command->force) ||
        command->kind==QA_CVARS_EDIT_RETAIN_SHARED)
        return apply_operation(live_target(registry),command,error);
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,apply_operation(live_target(registry),command,error),error);
}
bool qa_cvars_edit_apply(qa_cvars_edit *edit,const qa_cvars_edit_command *command,qa_error *error)
{
    if (!edit || !command)
        return qac_fail(error,QA_ERROR_ARGUMENT,"prepared cvar mutation requires its ticket and operation");
    cvar_target target={edit->registry,edit_source_values(edit),edit};
    qa_error fault={0};
    bool ok=target_touch(target,&fault);
    if (ok) ok=apply_operation(target,command,&fault);
    if (edit->fault.code!=QA_OK) { if (fault.code==QA_OK) fault=edit->fault; ok=false; }
    if (!ok) {
        if (fault.code==QA_OK) qac_fail(&fault,QA_ERROR_ARGUMENT,"prepared cvar operation failed");
        if (edit->fault.code==QA_OK) edit->fault=fault;
        if (error && error->code==QA_OK) *error=fault;
    }
    return ok;
}
static bool edit_current(const qa_cvars_edit *edit)
{
    if (!edit || !edit->registry || edit->registry->store->edit!=edit ||
        edit->registry->store->revision!=edit->revision || edit->revision==UINT64_MAX ||
        edit->owner_scope_depth) return false;
    for (const cvar_edit_view *view=edit->views;view;view=view->next) {
        const qa_cvars *registry=view->registry;
        if (registry->edit_scope_depth || registry->notifying || registry->mutation_depth ||
            registry->draining || registry->post_first || registry->edit_first || registry->edit_bindings_pending) return false;
    }
    return true;
}
bool qa_cvars_edit_returned_is(const qa_cvars_edit *edit,const qa_cvars *registry)
{ return registry && edit && edit->registry==registry && edit_current(edit) && edit->fault.code==QA_OK; }
bool qa_cvars_edit_abort_is(const qa_cvars_edit *edit,const qa_cvars *registry)
{ return registry && edit && edit->registry==registry && edit_current(edit); }
bool qa_cvars_edit_ready_is(const qa_cvars_edit *edit)
{ return edit_current(edit) && edit->fault.code==QA_OK && edit->ready; }
static uint64_t prepared_binding_order(const cvar_edit_binding *binding)
{ return binding->prepared?binding->prepared->binding_order:binding->prepared_alias->binding_order; }
bool qa_cvars_edit_ready(qa_cvars_edit *edit,qa_error *error)
{
    if (!edit_current(edit) || edit->fault.code!=QA_OK)
        return qac_fail(error,QA_ERROR_ARGUMENT,"prepared canonical publication is unavailable");
    if (edit->ready) return true;
    size_t count=0;
    for (cvar_edit_view *view=edit->views;view;view=view->next) {
        for (cvar *row=view->values.first;row;row=row->next) count+=row->bound;
        for (cvar_alias *alias=view->values.aliases;alias;alias=alias->next) count+=alias->bound;
    }
    if (count>SIZE_MAX/sizeof(*edit->bindings)) return qac_fail(error,QA_ERROR_MEMORY,"prepared bindings exceed address space");
    cvar_edit_binding *bindings=count?calloc(count,sizeof(*bindings)):NULL;
    if (count && !bindings) return qac_fail(error,QA_ERROR_MEMORY,"retaining canonical binding order");
    size_t used=0;
    for (cvar_edit_view *view=edit->views;view;view=view->next) {
        for (cvar *row=view->values.first;row;row=row->next) {
            if (!row->bound) continue;
            const qa_cvar_view *projected=qac_cvars_project(view->registry,&view->values,row);
            if (!projected || !validate_value(view->registry,&row->binding,projected->value,error) ||
                !validate_value(view->registry,&row->binding,projected->reset_value,error) ||
                (projected->latched_value && !validate_value(view->registry,&row->binding,projected->latched_value,error))) goto failed;
            cvar_edit_binding binding={.registry=view->registry,.prepared=row};
            size_t at=used++;
            while (at && prepared_binding_order(&bindings[at-1])>row->binding_order) { bindings[at]=bindings[at-1]; --at; }
            bindings[at]=binding;
        }
        for (cvar_alias *alias=view->values.aliases;alias;alias=alias->next) {
            if (!alias->bound) continue;
            const qa_cvar_view *projected=alias_view(view->registry,&view->values,alias);
            if (!projected || !validate_value(view->registry,&alias->binding,projected->value,error) ||
                !validate_value(view->registry,&alias->binding,projected->reset_value,error) ||
                (projected->latched_value && !validate_value(view->registry,&alias->binding,projected->latched_value,error))) goto failed;
            cvar_edit_binding binding={.registry=view->registry,.prepared_alias=alias};
            size_t at=used++;
            while (at && prepared_binding_order(&bindings[at-1])>alias->binding_order) { bindings[at]=bindings[at-1]; --at; }
            bindings[at]=binding;
        }
    }
    free(edit->bindings); edit->bindings=bindings; edit->binding_count=count; edit->ready=true;
    return true;
failed:
    free(bindings); return false;
}
void qa_cvars_edit_publish(qa_cvars_edit *edit)
{
    qa_cvars *registry=edit->registry;
    cvar_store *store=registry->store;
    cvar_values previous=store->values;
    store->values=edit->values; edit->values=(cvar_values){0};
    store->active_dialect=edit->active_dialect; store->active_default_source=edit->active_default_source;
    for (cvar_edit_view *view=edit->views;view;view=view->next) {
        cvar_values old=view->registry->values;
        view->registry->values=view->values; view->values=(cvar_values){0};
        view->registry->values.canonical_values=&store->values;
        view->registry->candidate_edit=NULL; view->registry->entered_edit=NULL;
        ++view->registry->mutation_revision;
        qac_cvars_values_free(&old);
    }
    registry->edit_first=edit->first; registry->edit_last=edit->last;
    registry->edit_bindings_pending=true;
    registry->edit_bindings=edit->bindings; registry->edit_binding_count=edit->binding_count;
    registry->ready_edit=NULL; store->edit=NULL; ++store->revision;
    qac_cvars_values_free(&previous);
    while (edit->views) { cvar_edit_view *view=edit->views; edit->views=view->next; free(view); }
    free(edit);
}
bool qa_cvars_edit_finish(qa_cvars *registry,qa_error *error)
{
    if (!registry || registry->ready_edit || registry->notifying || registry->mutation_depth || registry->draining)
        return qac_fail(error,QA_ERROR_ARGUMENT,"prepared notifications require returned canonical publication");
    if (registry->edit_bindings_pending) {
        for (size_t i=0;i<registry->edit_binding_count;++i) {
            cvar_edit_binding *binding=&registry->edit_bindings[i];
            const qa_cvar_view *view=binding->prepared
                ? qac_cvars_project(binding->registry,&binding->registry->values,binding->prepared)
                : alias_view(binding->registry,&binding->registry->values,binding->prepared_alias);
            const qa_cvar_binding *callback=binding->prepared?&binding->prepared->binding:&binding->prepared_alias->binding;
            if (callback->changed && view) {
                ++binding->registry->notifying; callback->changed(callback->user,view->value); --binding->registry->notifying;
            }
        }
        free(registry->edit_bindings); registry->edit_bindings=NULL; registry->edit_binding_count=0;
        registry->edit_bindings_pending=false;
    }
    while (registry->edit_first) {
        cvar_edit_event *event=registry->edit_first;
        qa_cvars *receiver=event->registry;
        if (event->kind==CVAR_EDIT_NOTIFY) { post_enqueue(receiver,event->post); event->post=NULL; }
        else if (event->kind==CVAR_EDIT_EFFECT) effect((cvar_target){receiver,&receiver->values,NULL},event->effect,event->snapshot);
        else print_message((cvar_target){receiver,&receiver->values,NULL},event->print_name,event->print_message);
        registry->edit_first=event->next; edit_event_free(receiver,event);
    }
    registry->edit_last=NULL;
    bool okay=true;
    for (qa_cvars *view=registry->store->views;view;view=view->next_view)
        if (!post_drain(view,error)) okay=false;
    return okay;
}
void qa_cvars_edit_abort(qa_cvars_edit *edit)
{
    if (!edit) return;
    qa_cvars *registry=edit->registry;
    while (edit->first) {
        cvar_edit_event *event=edit->first; edit->first=event->next; edit_event_free(event->registry,event);
    }
    while (edit->views) {
        cvar_edit_view *view=edit->views; edit->views=view->next;
        view->registry->candidate_edit=NULL; view->registry->entered_edit=NULL;
        view->registry->edit_scope_depth=0;
        qac_cvars_values_free(&view->values); free(view);
    }
    qac_cvars_values_free(&edit->values);
    if (registry->ready_edit==edit) registry->ready_edit=NULL;
    if (registry->store->edit==edit) registry->store->edit=NULL;
    free(edit->bindings); free(edit);
}

static bool copy_variables(qa_cvars *destination,const qa_cvars *source,
    bool declarations_only,qa_error *error)
{
    if (!source || !destination || !qa_cvars_observer_idle(source) ||
        qa_cvars_dialect(source)!=qa_cvars_dialect(destination))
        return qac_fail(error,QA_ERROR_ARGUMENT,"Source carry needs its returned same-dialect views");
    if (destination==source || (!declarations_only && qa_cvars_same_store(destination,source))) return true;
    qa_cvars_edit *edit=qac_cvars_current_edit(destination);
    bool owned=edit==NULL;
    if (owned && !qa_cvars_edit_prepare(destination,&edit,error)) return false;
    cvar_target target={destination,&qac_cvars_edit_view(edit,destination)->values,edit};
    bool okay=true;
    for (const qa_cvar_view *row=qa_cvars_next(source,NULL);okay && row;row=qa_cvars_next(source,row)) {
        if (!row->declared && !row->console_created) continue;
        const qa_cvar_view *actual=qa_cvars_find(destination,row->name);
        if (!actual || !actual->declared) {
            okay=register_variable(target,row->name,row->reset_value,row->flags,row->owner,row->description,error);
            if (!okay) break;
        }
        if (!declarations_only && row->explicit_value) okay=set_variable(target,row->name,row->value,true,error);
        if (okay && !declarations_only && row->latched_value) okay=stage_variable(target,row->name,row->latched_value,error);
    }
    if (!owned) return okay;
    if (okay) okay=qa_cvars_edit_ready(edit,error);
    if (!okay) { qa_cvars_edit_abort(edit); return false; }
    qa_cvars_edit_publish(edit); return qa_cvars_edit_finish(destination,error);
}

bool qa_cvars_copy(qa_cvars *destination,const qa_cvars *source,qa_error *error)
{ return copy_variables(destination,source,false,error); }

bool qa_cvars_copy_declarations(qa_cvars *destination,const qa_cvars *source,qa_error *error)
{ return copy_variables(destination,source,true,error); }

void qa_cvars_set_server_active(qa_cvars *registry,bool active)
{ if (qac_cvars_touch(registry,NULL)) qac_cvars_current_values(registry)->server_active=active; }
void qa_cvars_set_high_characters(qa_cvars *registry,bool enabled)
{ if (qac_cvars_touch(registry,NULL)) qac_cvars_current_values(registry)->high_characters=enabled; }
void qa_cvars_remove_owner(qa_cvars *registry,uint64_t owner)
{
    if (!qac_cvars_touch(registry,NULL)) return;
    cvar_observer *observer=registry->observers;
    while (observer) {
        cvar_observer *next=observer->next;
        if (observer->owner==owner) { observer->active=false; observer_release(registry,observer); }
        observer=next;
    }
    qa_cvars_edit *edit=registry->store->edit;
    for (cvar_edit_event *event=edit?edit->first:NULL;event;event=event->next)
        if (event->registry==registry && event->snapshot && event->snapshot->binding.owner==owner) {
            event->snapshot->bound=false; event->snapshot->binding=(qa_cvar_binding){0};
        }
    cvar_values *values=qac_cvars_current_values(registry);
    for (cvar *entry=values->first;entry;entry=entry->next) {
        if (entry->bound && entry->binding.owner==owner) { entry->binding=(qa_cvar_binding){0}; entry->bound=false; entry->binding_order=0; }
        if (!entry->view.declared || entry->view.owner!=owner) continue;
        entry->view.declared=false; entry->view.owner=0; --values->declared_count;
        values->changed_flags|=entry->view.flags;
        if (entry->view.flags&QA_CVAR_USERINFO) values->userinfo_modified=true;
    }
    for (cvar_alias *alias=values->aliases;alias;alias=alias->next) {
        if (alias->bound && alias->binding.owner==owner) { alias->binding=(qa_cvar_binding){0}; alias->bound=false; alias->binding_order=0; }
        if (alias->declared && alias->owner==owner) { alias->declared=false; alias->owner=0; }
    }
}
uint32_t qa_cvars_take_modified_flags(qa_cvars *registry)
{
    if (!qac_cvars_touch(registry,NULL)) return 0;
    cvar_values *values=qac_cvars_current_values(registry); uint32_t flags=values->changed_flags; values->changed_flags=0; return flags;
}
void qa_cvars_mark_modified_flags(qa_cvars *registry,uint32_t flags)
{ if (qac_cvars_touch(registry,NULL)) qac_cvars_current_values(registry)->changed_flags|=flags; }
void qa_cvars_clear_modified(qa_cvars *registry,const char *name)
{
    if (!qac_cvars_touch(registry,NULL)) return;
    cvar_values *values=qac_cvars_current_values(registry);
    cvar *entry=qac_cvars_find_values(registry,values,canonical_name(registry,values,name));
    if (entry) entry->view.modified=false;
    cvar_alias *alias=find_alias(registry,values,name);
    if (alias) alias->modified=false;
}
bool qa_cvars_take_userinfo_modified(qa_cvars *registry)
{
    if (!qac_cvars_touch(registry,NULL)) return false;
    cvar_values *values=qac_cvars_current_values(registry); bool modified=values->userinfo_modified; values->userinfo_modified=false; return modified;
}

bool qa_cvars_info(const qa_cvars *registry, uint32_t flags, size_t maximum_length,
                    qa_buffer *out, qa_error *error)
{
    if (registry == NULL || out == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar info arguments");
    qa_console_dialect dialect = qa_cvars_dialect(registry);
    if (maximum_length == 0) maximum_length = dialect == QA_CONSOLE_Q3 ? 1024 : 512;
    qac_text result = {0};
    cvar_values *values=qac_cvars_current_values(registry);
    for (const qa_cvar_view *value=qa_cvars_next(registry,NULL); value; value=qa_cvars_next(registry,value)) {
        if ((value->flags & flags) == 0 || (qac_q2(dialect) && (value->flags & QA_Q2_CVAR_PRIVATE) != 0)) continue;
        size_t key_length = strlen(value->name);
        size_t value_length = strlen(value->value);
        if (*value->value == '\0' || strchr(value->name, '\\') != NULL || strchr(value->value, '\\') != NULL ||
            strchr(value->name, '"') != NULL || strchr(value->value, '"') != NULL ||
            (dialect != QA_CONSOLE_QW && strchr(value->name, ';') != NULL) ||
            (dialect == QA_CONSOLE_Q3 && strchr(value->value, ';') != NULL) ||
            (dialect == QA_CONSOLE_QW && *value->name == '*') ||
            (dialect != QA_CONSOLE_Q3 && (key_length >= 64 || value_length >= 64))) continue;
        qac_text pair = {0};
        if (!qac_text_add(&pair, "\\", 1, error) || !qac_text_string(&pair, value->name, error) ||
            !qac_text_add(&pair, "\\", 1, error) || !qac_text_string(&pair, value->value, error)) {
            free(pair.data);
            free(result.data);
            return false;
        }
        if (dialect != QA_CONSOLE_Q3) {
            size_t count = 0;
            bool userinfo = (flags & QA_CVAR_USERINFO) != 0;
            bool strip = dialect != QA_CONSOLE_QW || (userinfo ? !qac_equal(value->name, "name") : !values->high_characters);
            for (size_t i = 0; i < pair.size; ++i) {
                unsigned char c = (unsigned char)pair.data[i];
                if (strip) {
                    c &= 127;
                    if (c < 32 || (dialect != QA_CONSOLE_QW && c == 127)) continue;
                    if (dialect == QA_CONSOLE_QW && userinfo && qac_equal(value->name, "team") && c >= 'A' && c <= 'Z') c += 32;
                }
                if (dialect == QA_CONSOLE_QW && c <= 13) continue;
                pair.data[count++] = (char)c;
            }
            pair.size = count;
            pair.data[count] = '\0';
        }
        if (pair.size >= maximum_length || result.size >= maximum_length - pair.size) {
            print_message(live_target((qa_cvars *)registry), NULL, "Info string length exceeded\n");
            free(pair.data);
            continue;
        }
        if (dialect == QA_CONSOLE_Q3 && maximum_length != 8192) {
            if (!qac_text_add(&pair, result.data, result.size, error)) {
                free(pair.data); free(result.data); return false;
            }
            free(result.data);
            result = pair;
        } else {
            bool ok = qac_text_add(&result, pair.data, pair.size, error);
            free(pair.data);
            if (!ok) { free(result.data); return false; }
        }
    }
    if (!qac_text_finish(&result, out, error)) { free(result.data); return false; }
    return true;
}

static const char *archive_value(const qa_cvars *registry,const cvar_values *values,
    const qa_cvar_view *variable)
{
    if (!registry || !values || !variable) return NULL;
    const cvar_values *canonical=values->canonical_values?values->canonical_values:values;
    if (find_alias(registry,canonical,variable->name)) return NULL;
    const cvar *entry=qac_cvars_find_values(registry,canonical,variable->name);
    if (!entry || !(entry->view.flags&QA_CVAR_ARCHIVE) ||
        (!entry->view.explicit_value && !entry->pending_explicit) ||
        (entry->catalog_row!=QA_CVAR_CATALOG_NO_ROW &&
            (qa_cvar_catalog_rows[entry->catalog_row].not_stored ||
             (qa_cvar_catalog_rows[entry->catalog_row].policies&QA_CATALOG_POLICY_PRIVATE)))) return NULL;
    if (qac_equal(entry->view.name,"cl_cdkey")) return NULL;
    const qa_cvars_edit *edit=registry->store->edit;
    for (const qa_cvars *source=registry->store->views;source;source=source->next_view) {
        const cvar_edit_view *prepared=edit && canonical==&edit->values?qac_cvars_edit_view((qa_cvars_edit *)edit,source):NULL;
        const cvar_values *metadata=prepared?&prepared->values:&source->values;
        const cvar *declaration=entry->ordinal<metadata->row_capacity?metadata->rows[entry->ordinal]:NULL;
        if (declaration && qac_q2(declaration->flags_dialect) &&
            (declaration->view.flags&(QA_Q2_CVAR_PRIVATE|QA_Q2_CVAR_NO_ARCHIVE))) return NULL;
        for (const cvar_alias *alias=metadata->aliases;alias;alias=alias->next)
            if (qac_q2(alias->flags_dialect) && (alias->flags&(QA_Q2_CVAR_PRIVATE|QA_Q2_CVAR_NO_ARCHIVE)) &&
                (!entry->view.player_scoped || source->options.seat==entry->player) && qac_equal(alias->target,entry->view.name)) return NULL;
    }
    return entry->view.latched_value?entry->view.latched_value:entry->view.value;
}

const char *qa_cvars_archive_value(const qa_cvars *registry,const qa_cvar_view *variable)
{ return archive_value(registry,qac_cvars_current_values(registry),variable); }

const char *qa_cvars_edit_archive_value(const qa_cvars_edit *edit,const qa_cvar_view *variable)
{ return edit_current(edit)?archive_value(edit->registry,&edit->values,variable):NULL; }

const qa_cvar_view *qa_cvars_edit_canonical_record(const qa_cvars_edit *edit,const char *name)
{
    if (!edit_current(edit) || !name) return NULL;
    const cvar *row=qac_cvars_find_values(edit->registry,&edit->values,
        canonical_name(edit->registry,&edit->values,name));
    return row?&row->view:NULL;
}

static bool config_filtered(const qa_cvars *registry,const cvar_values *values,
    qa_cvar_config_filter filter,void *context,qa_buffer *out,qa_error *error)
{
    if (registry == NULL || out == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar config arguments");
    qac_text result = {0};
    qa_console_dialect dialect = qa_cvars_dialect(registry);
    if (values->canonical_values) values=values->canonical_values;
    for (const cvar *entry = values->first; entry != NULL; entry = entry->next) {
        if (entry->view.player_scoped && entry->player != registry->options.seat) continue;
        const qa_cvar_view *variable = &entry->view;
        const char *value = archive_value(registry,values,variable);
        if (!value || (filter && !filter(context, registry, variable))) continue;
        if (strpbrk(value, "\"\r\n") != NULL) {
            free(result.data);
            return qac_fail(error, QA_ERROR_FORMAT, "cvar value cannot be represented by source config quoting");
        }
        const char *prefix = dialect == QA_CONSOLE_Q3 || variable->console_created ||
            (qac_q2(dialect) && (variable->flags & QA_Q2_CVAR_CUSTOM) != 0) ? "seta " : qac_q2(dialect) ? "set " : "";
        if (!qac_text_string(&result, prefix, error) || !qac_text_string(&result, variable->name, error) ||
            !qac_text_add(&result, " \"", 2, error) || !qac_text_string(&result, value, error) ||
            !qac_text_add(&result, "\"\n", 2, error)) {
            free(result.data);
            return false;
        }
    }
    if (!qac_text_finish(&result, out, error)) { free(result.data); return false; }
    return true;
}

bool qa_cvars_config_filtered(const qa_cvars *registry,qa_cvar_config_filter filter,
    void *context,qa_buffer *out,qa_error *error)
{ return config_filtered(registry,qac_cvars_current_values(registry),filter,context,out,error); }

bool qa_cvars_edit_config_filtered(const qa_cvars_edit *edit,qa_cvar_config_filter filter,
    void *context,qa_buffer *out,qa_error *error)
{
    if (!edit_current(edit))
        return qac_fail(error,QA_ERROR_ARGUMENT,"prepared config requires its returned current scalar ticket");
    return config_filtered(edit->registry,&edit->values,filter,context,out,error);
}

bool qa_cvars_config(const qa_cvars *registry, qa_buffer *out, qa_error *error)
{
    return qa_cvars_config_filtered(registry, NULL, NULL, out, error);
}
