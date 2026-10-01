#include "cvars_private.h"
#include "qa/text.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qac_cvars_touch(qa_cvars *registry, qa_error *error)
{
    if (!registry) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar mutation requires its registry");
    if (registry->notifying) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar notification cannot mutate its registry");
    if (registry->mutation_revision == UINT64_MAX)
        return qac_fail(error, QA_ERROR_MEMORY, "cvar mutation identity is exhausted");
    ++registry->mutation_revision;
    return true;
}

bool qa_cvars_capture_metadata(const qa_cvars *registry, qa_cvar_registry_state *out,
                                qa_cvar_record_state *records, size_t capacity, qa_error *error)
{
    if (!qa_cvars_observer_idle(registry) || out == NULL || capacity < registry->count || (registry->count && records == NULL))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar metadata capture needs complete output storage");
    *out = (qa_cvar_registry_state){registry->next_handle, registry->changed_flags,
        registry->userinfo_modified, registry->server_active, registry->high_characters, registry->cheats};
    size_t index = 0;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        records[index++] = (qa_cvar_record_state){entry->view.name, entry->view.handle,
            entry->view.owner, entry->view.modification_count, entry->view.modified, entry->view.console_created};
    }
    return true;
}

static const uint32_t q2_no_archive = QA_Q2_CVAR_NOSET | QA_Q2_CVAR_CHEAT |
    QA_Q2_CVAR_PRIVATE | QA_Q2_CVAR_READONLY | QA_Q2_CVAR_NO_ARCHIVE;

static bool name_equal(const qa_cvars *registry, const char *a, const char *b)
{
    return registry->options.dialect == QA_CONSOLE_Q3 ? qac_equal(a, b) : strcmp(a, b) == 0;
}

static cvar *find_variable(const qa_cvars *registry, const char *name)
{
    if (registry == NULL || name == NULL) return NULL;
    for (cvar *entry = registry->first; entry != NULL; entry = entry->next)
        if (name_equal(registry, name, entry->view.name)) return entry;
    return NULL;
}
static const char *source_name(const qa_cvars *, const char *);

bool qa_cvars_observer_idle(const qa_cvars *registry)
{
    return registry && !registry->mutation_depth && !registry->notifying &&
        !registry->draining && !registry->post_first;
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
        if (row->active && !row->suppressed && name_equal(registry,row->name,entry->view.name)) {
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
        if (row->active && !row->suppressed && name_equal(registry,row->name,entry->view.name)) {
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
    if (!registry || registry->notifying || registry->mutation_depth==SIZE_MAX)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar mutation requires an available registry");
    ++registry->mutation_depth; return true;
}
static bool mutation_end(qa_cvars *registry,bool ok,qa_error *error)
{
    --registry->mutation_depth;
    bool observed=post_drain(registry,error); return ok && observed;
}
bool qa_cvars_observe(qa_cvars *registry,const char *name,uint64_t owner,
    qa_cvar_observer_fn callback,void *user,qa_cvar_observer_token *out,qa_error *error)
{
    if (!registry || registry->notifying || !name || !owner || !callback || !out || *out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar observer needs a declared owner and empty token");
    const cvar *entry=find_variable(registry,source_name(registry,name));
    if (!entry) return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar observer name is not registered");
    if (registry->next_observer==UINT64_MAX)
        return qac_fail(error,QA_ERROR_MEMORY,"cvar observer tokens are exhausted");
    cvar_observer *row=calloc(1,sizeof(*row));
    if (!row) return qac_fail(error,QA_ERROR_MEMORY,"allocating cvar observer");
    row->name=qac_copy(entry->view.name,error);
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
        if (row->token==token && row->active) { row->suppressed=suppressed; return true; }
    return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar observer token is retired");
}
bool qa_cvars_restore_metadata(qa_cvars *registry, const qa_cvar_registry_state *state,
                                const qa_cvar_record_state *records, size_t count, qa_error *error)
{
    if (!qa_cvars_observer_idle(registry))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar metadata restore requires a complete publication drain");
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || state == NULL || count != registry->count || (count && records == NULL))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar metadata does not cover this registry");
    for (size_t i = 0; i < count; ++i) {
        cvar *entry = find_variable(registry, records[i].name);
        if (entry == NULL || records[i].handle >= state->next_handle ||
            (entry->bound && (entry->view.owner != records[i].owner || entry->view.handle != records[i].handle)))
            return qac_fail(error, QA_ERROR_FORMAT, "invalid saved cvar metadata identity");
        for (size_t earlier = 0; earlier < i; ++earlier)
            if (records[earlier].handle == records[i].handle || name_equal(registry, records[earlier].name, records[i].name))
                return qac_fail(error, QA_ERROR_FORMAT, "duplicate saved cvar metadata identity");
    }
    for (size_t i = 0; i < count; ++i) {
        cvar *entry = find_variable(registry, records[i].name);
        entry->view.handle = records[i].handle; entry->view.owner = records[i].owner;
        entry->view.modification_count = records[i].modification_count;
        entry->view.modified = records[i].modified; entry->view.console_created = records[i].console_created;
    }
    registry->next_handle = state->next_handle; registry->changed_flags = state->modified_flags;
    registry->userinfo_modified = state->userinfo_modified; registry->server_active = state->server_active;
    registry->high_characters = state->high_characters; registry->cheats = state->cheats;
    return true;
}
bool qa_cvars_retain_shared(qa_cvars *registry, const char *name, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "shared cvar is not registered");
    entry->view.owner = 0;
    return true;
}

static void print_message(const qa_cvars *registry, const char *name, const char *message)
{
    if (registry->options.print == NULL) return;
    qa_cvars *owned=(qa_cvars *)registry;
    ++owned->notifying;
    if (name != NULL) registry->options.print(registry->options.user, name);
    registry->options.print(registry->options.user, message);
    --owned->notifying;
}

static bool valid_info(const char *text)
{
    return strpbrk(text, "\\\";") == NULL;
}

static bool valid_name(const char *text)
{
    if (*text == '\0') return false;
    for (; *text != '\0'; ++text)
        if ((unsigned char)*text <= 32 || *text == '"' || *text == ';') return false;
    return true;
}

static const char *source_name(const qa_cvars *registry, const char *name)
{
    return registry->options.dialect == QA_CONSOLE_Q3 && !valid_info(name) ? "BADNAME" : name;
}

void qac_cvars_entry_free(cvar *entry)
{
    free((char *)entry->view.name);
    free((char *)entry->view.value);
    free((char *)entry->view.reset_value);
    free((char *)entry->view.latched_value);
    free((char *)entry->view.description);
    qac_document_free(entry->view.documentation);
    free(entry);
}

bool qa_cvars_document(qa_cvars *registry, const char *name, uint64_t owner,
                        const qa_console_documentation *doc, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    cvar *entry = find_variable(registry, name);
    if (!entry || entry->view.owner != owner)
        return qac_fail(error, QA_ERROR_NOT_FOUND, "cvar documentation owner not found");
    return qac_document_replace(&entry->view.documentation, doc, error);
}

static void numbers(qa_cvars *registry, cvar *entry)
{
    entry->view.number = qac_number(entry->view.value, registry->options.dialect);
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

static void effect(qa_cvars *registry, qa_cvar_effect_kind kind, const cvar *entry)
{
    if (registry->options.effect != NULL) {
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
static bool notify_value(qa_cvars *registry,const cvar *entry,const char *value,qa_error *error)
{
    cvar_post_event *event=NULL;
    if (!post_prepare(registry,entry,&event,error)) return false;
    changed_value(registry,entry,value); post_enqueue(registry,event); return true;
}

static void propagate(qa_cvars *registry, const cvar *entry, bool changed)
{
    if (registry->options.dialect == QA_CONSOLE_Q1 && changed && registry->server_active &&
        (entry->view.flags & QA_CVAR_SERVERINFO) != 0)
        effect(registry, QA_CVAR_EFFECT_BROADCAST, entry);
    else if (registry->options.dialect == QA_CONSOLE_QW) {
        if ((entry->view.flags & QA_CVAR_USERINFO) != 0) effect(registry, QA_CVAR_EFFECT_USERINFO, entry);
        if ((entry->view.flags & QA_CVAR_SERVERINFO) != 0) effect(registry, QA_CVAR_EFFECT_SERVERINFO, entry);
    }
}

static bool apply_value(qa_cvars *registry, cvar *entry, const char *value,
                        bool mark, qa_error *error)
{
    cvar_post_event *event=NULL;
    if (!post_prepare(registry,entry,&event,error)) return false;
    if (!replace_text(&entry->view.value, value, error)) { post_release(registry,event); return false; }
    numbers(registry, entry);
    if (mark) {
        entry->view.modified = true;
        ++entry->view.modification_count;
    }
    changed_value(registry,entry,entry->view.value);
    post_enqueue(registry,event);
    return true;
}

bool qa_cvars_cheats_policy(const qa_cvars *registry, bool *callback_backed,
                             bool *fallback_allowed)
{
    if (!registry || !callback_backed || !fallback_allowed)
        return false;
    *callback_backed = registry->options.cheats_allowed != NULL;
    *fallback_allowed = registry->cheats;
    return true;
}

static bool cheats_allowed(const qa_cvars *registry)
{
    if (registry->options.cheats_allowed != NULL) {
        qa_cvars *owned=(qa_cvars *)registry;
        ++owned->notifying;
        bool allowed=registry->options.cheats_allowed(registry->options.user);
        --owned->notifying; return allowed;
    }
    const cvar *cheats = find_variable(registry, "sv_cheats");
    return cheats == NULL ? registry->cheats : cheats->view.integer != 0;
}

qa_cvars *qa_cvars_create(const qa_cvar_options *options, qa_error *error)
{
    if (options == NULL || !qac_dialect_valid(options->dialect)) {
        qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar registry options");
        return NULL;
    }
    qa_cvars *registry = calloc(1, sizeof(*registry));
    if (registry == NULL) {
        qac_fail(error, QA_ERROR_MEMORY, "allocating cvar registry");
        return NULL;
    }
    registry->options = *options;
    registry->cheats = true;
    return registry;
}

void qa_cvars_destroy(qa_cvars *registry)
{
    if (registry == NULL) return;
    if (!qa_cvars_observer_idle(registry)) return;
    cvar *entry = registry->first;
    while (entry != NULL) {
        cvar *next = entry->next;
        qac_cvars_entry_free(entry);
        entry = next;
    }
    while (registry->observers) {
        cvar_observer *row=registry->observers;
        registry->observers=row->next; free(row->name); free(row);
    }
    free(registry);
}

qa_console_dialect qa_cvars_dialect(const qa_cvars *registry)
{
    return registry->options.dialect;
}

const qa_cvar_view *qa_cvars_find(const qa_cvars *registry, const char *name)
{
    const cvar *entry = find_variable(registry, name);
    return entry == NULL ? NULL : &entry->view;
}

const qa_cvar_view *qa_cvars_at(const qa_cvars *registry, size_t ordinal)
{
    if (registry == NULL) return NULL;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next)
        if (ordinal-- == 0) return &entry->view;
    return NULL;
}

const qa_cvar_view *qa_cvars_handle(const qa_cvars *registry, size_t handle)
{
    if (registry == NULL) return NULL;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next)
        if (entry->view.handle == handle) return &entry->view;
    return NULL;
}

size_t qa_cvars_count(const qa_cvars *registry)
{
    return registry == NULL ? 0 : registry->count;
}

size_t qa_cvars_handle_count(const qa_cvars *registry)
{
    return registry == NULL ? 0 : registry->next_handle;
}

bool qa_cvars_bind(qa_cvars *registry, const char *name, const qa_cvar_binding *binding,
                    qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    cvar *entry = find_variable(registry, name);
    if (entry == NULL || binding == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "binding requires a registered cvar");
    if (entry->bound) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar already has a value binding");
    if (binding->validate != NULL &&
        (!validate_value(registry,binding,entry->view.value,error) ||
         !validate_value(registry,binding,entry->view.reset_value,error) ||
         (entry->view.latched_value != NULL && !validate_value(registry,binding,entry->view.latched_value,error)))) return false;
    entry->binding = *binding;
    entry->bound = true;
    return true;
}

void qa_cvars_unbind(qa_cvars *registry, const char *name, uint64_t owner)
{
    if (!qac_cvars_touch(registry, NULL)) return;
    cvar *entry = find_variable(registry, name);
    if (entry != NULL && entry->bound && entry->binding.owner == owner) {
        entry->binding = (qa_cvar_binding){0};
        entry->bound = false;
    }
}

static bool register_variable(qa_cvars *registry, const char *name, const char *default_value,
                        uint32_t flags, uint64_t owner, const char *description,
                        qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || name == NULL || default_value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar registration requires name and default");
    name = source_name(registry, name);
    if (!valid_name(name)) return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar name");
    const qa_console_dialect dialect = registry->options.dialect;
    bool q2 = qac_q2(dialect);
    if (q2 && (flags & (QA_CVAR_USERINFO | QA_CVAR_SERVERINFO)) != 0 &&
        (!valid_info(name) || !valid_info(default_value)))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid info cvar name or default");
    cvar *entry = find_variable(registry, name);
    if (entry != NULL) {
        if (entry->bound && entry->binding.validate != NULL &&
            !validate_value(registry,&entry->binding,default_value,error)) return false;
        if (qac_q1(dialect)) {
            if (!entry->view.console_created) {
                print_message(registry, name, " is already registered\n");
                return true;
            }
            if (!replace_text(&entry->view.reset_value, default_value, error)) return false;
            entry->view.console_created = false;
            entry->view.flags |= flags;
            entry->view.owner = owner;
            propagate(registry, entry, true);
            return true;
        }
        uint32_t created = q2 ? QA_Q2_CVAR_CUSTOM : QA_CVAR_USER_CREATED;
        bool promoted = q2 && (entry->view.flags & created) != 0 && (flags & created) == 0;
        if (((entry->view.flags & created) != 0 && (flags & created) == 0 &&
             (q2 || default_value[0] != '\0')) || entry->view.reset_value[0] == '\0') {
            if (!replace_text(&entry->view.reset_value, default_value, error)) return false;
            entry->view.flags &= ~created;
            entry->view.console_created = false;
            entry->view.owner = owner;
            if (!q2) registry->changed_flags |= flags;
        }
        if (promoted && (((flags & (QA_Q2_CVAR_READONLY | QA_Q2_CVAR_NOSET)) != 0) ||
                   ((flags & QA_Q2_CVAR_CHEAT) != 0 && !cheats_allowed(registry)) ||
                   ((flags & 6u) != 0 && !valid_info(entry->view.value)))) {
            if (!qa_cvars_set(registry, name, default_value, true, error)) return false;
        }
        entry->view.flags |= flags;
        if (q2 && (entry->view.flags & q2_no_archive) != 0) entry->view.flags &= ~UINT32_C(1);
        if (!q2 && entry->view.latched_value != NULL)
            return qa_cvars_apply_latched(registry, name, error);
        return true;
    }
    if (qac_q1(dialect) && registry->options.command_exists != NULL) {
        ++registry->notifying;
        bool exists=registry->options.command_exists(registry->options.user,name);
        --registry->notifying;
        if (exists) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar name is already a command");
    }
    if (registry->next_handle == SIZE_MAX)
        return qac_fail(error, QA_ERROR_MEMORY, "cvar handles exhausted");
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) return qac_fail(error, QA_ERROR_MEMORY, "allocating cvar");
    entry->view.name = qac_copy(name, error);
    entry->view.value = qac_copy(default_value, error);
    entry->view.reset_value = qac_copy(default_value, error);
    entry->view.description = qac_copy(description == NULL ? "" : description, error);
    if (entry->view.name == NULL || entry->view.value == NULL ||
        entry->view.reset_value == NULL || entry->view.description == NULL) {
        qac_cvars_entry_free(entry);
        return false;
    }
    entry->view.flags = q2 && (flags & q2_no_archive) != 0 ? flags & ~UINT32_C(1) : flags;
    entry->view.owner = owner;
    entry->view.modified = true;
    entry->view.modification_count = 1;
    entry->view.handle = registry->next_handle++;
    numbers(registry, entry);
    entry->next = registry->first;
    registry->first = entry;
    ++registry->count;
    if (dialect == QA_CONSOLE_QW) propagate(registry, entry, true);
    return true;
}

static bool set_variable(qa_cvars *registry, const char *name, const char *value,
                   bool force, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar set requires name and value");
    name = source_name(registry, name);
    cvar *entry = find_variable(registry, name);
    const qa_console_dialect dialect = registry->options.dialect;
    bool q2 = qac_q2(dialect);
    if (entry == NULL) {
        if (qac_q1(dialect)) return qac_fail(error, QA_ERROR_NOT_FOUND, "Q1 cvar is not registered");
        uint32_t flags = dialect == QA_CONSOLE_Q3 && !force ? QA_CVAR_USER_CREATED : 0;
        return qa_cvars_register(registry, name, value, flags, 0, NULL, error);
    }
    bool changed = strcmp(entry->view.value, value) != 0;
    if (entry->bound && entry->binding.validate != NULL &&
        !validate_value(registry,&entry->binding,value,error)) return false;
    if (qac_q1(dialect)) {
        if (!apply_value(registry, entry, value, false, error)) return false;
        propagate(registry, entry, changed);
        return true;
    }
    if (q2 && (entry->view.flags & 6u) != 0 && !valid_info(value))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid info cvar value");
    if (!q2 && !changed) {
        return notify_value(registry,entry,value,error);
    }
    if (!q2) registry->changed_flags |= entry->view.flags;
    if (!force) {
        uint32_t readonly = q2 ? QA_Q2_CVAR_READONLY : QA_CVAR_READONLY;
        uint32_t init = q2 ? QA_Q2_CVAR_NOSET : QA_CVAR_INIT;
        uint32_t cheat = q2 ? QA_Q2_CVAR_CHEAT : QA_CVAR_CHEAT;
        uint32_t latch = q2 ? QA_Q2_CVAR_LATCH : QA_CVAR_LATCH;
        if ((entry->view.flags & readonly) != 0) {
            print_message(registry, name, " is read only.\n");
            return true;
        }
        if (q2 && (entry->view.flags & cheat) != 0 && !cheats_allowed(registry)) {
            print_message(registry, name, " is cheat protected.\n");
            return true;
        }
        if ((entry->view.flags & init) != 0) {
            print_message(registry, name, " is write protected.\n");
            return true;
        }
        if ((entry->view.flags & latch) != 0) {
            const char *pending = entry->view.latched_value;
            if (pending != NULL && strcmp(pending, value) == 0) return true;
            if (q2 && pending == NULL && !changed) return true;
            if (q2 && !registry->server_active) {
                const char *previous = entry->view.latched_value;
                entry->view.latched_value = NULL;
                if (!apply_value(registry, entry, value, false, error)) {
                    entry->view.latched_value = previous;
                    return false;
                }
                free((char *)previous);
                if (strcmp(entry->view.name, "game") == 0) effect(registry, QA_CVAR_EFFECT_GAME_DIRECTORY, entry);
                return true;
            }
            if (!replace_text(&entry->view.latched_value, value, error)) return false;
            if (!q2) { entry->view.modified = true; ++entry->view.modification_count; }
            print_message(registry, name, q2 ? " will be changed for next game.\n" : " will be changed upon restarting.\n");
            return true;
        }
        if (!q2 && (entry->view.flags & cheat) != 0 && !cheats_allowed(registry)) {
            print_message(registry, name, " is cheat protected.\n");
            return true;
        }
    }
    /* Detach pending storage until the value has been copied successfully. */
    cvar_post_event *equal_event=NULL;
    if (!changed && !post_prepare(registry,entry,&equal_event,error)) return false;
    const char *previous = force ? entry->view.latched_value : NULL;
    if (force) entry->view.latched_value = NULL;
    if (changed && !apply_value(registry, entry, value, true, error)) {
        if (force) entry->view.latched_value = previous;
        return false;
    }
    free((char *)previous);
    if (!changed) {
        changed_value(registry,entry,entry->view.value);
        post_enqueue(registry,equal_event);
    }
    if (changed && q2 && (entry->view.flags & QA_CVAR_USERINFO) != 0) registry->userinfo_modified = true;
    return true;
}

static bool set_console_variable(qa_cvars *registry, const char *name, const char *value,
                           qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console cvar arguments");
    cvar *entry = find_variable(registry, name);
    if (qac_q2(registry->options.dialect) && entry != NULL && strcmp(entry->view.value, value) == 0) {
        cvar_post_event *event=NULL;
        if (!post_prepare(registry,entry,&event,error)) return false;
        free((char *)entry->view.latched_value);
        entry->view.latched_value = NULL;
        changed_value(registry,entry,entry->view.value); post_enqueue(registry,event);
        return true;
    }
    return qa_cvars_set(registry, name, value, false, error);
}

static bool set_number_variable(qa_cvars *registry, const char *name, float value, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || !isfinite(value))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar numeric set requires a finite float");
    char text[64];
    if (!qac_q1(registry->options.dialect) && value >= -2147483648.0f &&
        value < 2147483648.0f && truncf(value) == value)
        (void)snprintf(text, sizeof(text), "%d", (int32_t)value);
    else if (!qa_format_fixed(value, 6, text, sizeof(text), error)) return false;
    if (strlen(text) >= 32) {
        if (qac_q1(registry->options.dialect))
            return qac_fail(error, QA_ERROR_FORMAT, "numeric cvar exceeds source value buffer");
        print_message(registry, NULL, "numeric cvar truncated to source value buffer\n");
        text[31] = '\0';
    }
    return qa_cvars_set(registry, name, text, registry->options.dialect == QA_CONSOLE_Q3, error);
}

static bool full_set_variable(qa_cvars *registry, const char *name, const char *value,
                        uint32_t flags, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || !qac_q2(registry->options.dialect) || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "full cvar set requires a Q2 registry");
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qa_cvars_register(registry, name, value, flags, 0, NULL, error);
    if (entry->bound && entry->binding.validate != NULL &&
        !validate_value(registry,&entry->binding,value,error)) return false;
    if (!apply_value(registry, entry, value, true, error)) return false;
    if ((entry->view.flags & QA_CVAR_USERINFO) != 0) registry->userinfo_modified = true;
    entry->view.flags = flags;
    return true;
}

static bool set_flags_variable(qa_cvars *registry, const char *name, const char *value,
                         uint32_t flag, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || name == NULL || value == NULL ||
        (flag != QA_CVAR_ARCHIVE && flag != QA_CVAR_USERINFO && flag != QA_CVAR_SERVERINFO))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar command flag");
    bool q2 = qac_q2(registry->options.dialect);
    bool q3 = registry->options.dialect == QA_CONSOLE_Q3;
    name = source_name(registry, name);
    if (!q3 && flag != QA_CVAR_ARCHIVE && (!valid_info(name) || !valid_info(value) ||
        (q2 && (strlen(name) >= 64 || strlen(value) >= 64))))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid info cvar name or value");
    cvar *entry = find_variable(registry, name);
    uint32_t old_flags = entry == NULL ? 0 : entry->view.flags;
    if (entry == NULL) {
        uint32_t created = q2 ? QA_Q2_CVAR_CUSTOM : registry->options.dialect == QA_CONSOLE_Q3 ? QA_CVAR_USER_CREATED : 0;
        if (!qa_cvars_register(registry, name, value, flag | created, 0, NULL, error)) return false;
        entry = find_variable(registry, source_name(registry, name));
        entry->view.console_created = true;
    } else {
        if (!qa_cvars_set_console(registry, name, value, error)) return false;
        if (!q3 && flag != QA_CVAR_ARCHIVE && (!valid_info(entry->view.value) || (q2 && strlen(entry->view.value) >= 64)))
            return qac_fail(error, QA_ERROR_FORMAT, "invalid retained info cvar value");
        if (q2 && flag != QA_CVAR_ARCHIVE) entry->view.flags &= ~UINT32_C(6);
        entry->view.flags |= flag;
    }
    if (q2 && (entry->view.flags & q2_no_archive) != 0) entry->view.flags &= ~UINT32_C(1);
    if (flag != QA_CVAR_ARCHIVE) {
        if (q2) registry->userinfo_modified |= ((old_flags | entry->view.flags) & QA_CVAR_USERINFO) != 0;
        else propagate(registry, entry, true);
    }
    return true;
}

static bool stage_variable(qa_cvars *registry, const char *name, const char *value, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || name == NULL || value == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid staged cvar arguments");
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "cannot stage unregistered cvar");
    if (entry->bound && entry->binding.validate != NULL &&
        !validate_value(registry,&entry->binding,value,error)) return false;
    qa_console_dialect dialect = registry->options.dialect;
    if ((dialect == QA_CONSOLE_Q3 && (entry->view.flags & (QA_CVAR_READONLY | QA_CVAR_INIT)) != 0) ||
        (qac_q2(dialect) && (entry->view.flags & QA_Q2_CVAR_NOSET) != 0))
        return qac_fail(error, QA_ERROR_ARGUMENT, "cannot stage protected cvar");
    if (qac_q2(dialect) && (entry->view.flags & 6u) != 0 && !valid_info(value))
        return qac_fail(error, QA_ERROR_FORMAT, "invalid staged info cvar");
    bool same = strcmp(value, entry->view.value) == 0;
    if ((same && entry->view.latched_value == NULL) ||
        (!same && entry->view.latched_value != NULL && strcmp(value, entry->view.latched_value) == 0)) return true;
    if (same) { free((char *)entry->view.latched_value); entry->view.latched_value = NULL; }
    else if (!replace_text(&entry->view.latched_value, value, error)) return false;
    entry->view.modified = true;
    ++entry->view.modification_count;
    if (dialect == QA_CONSOLE_Q3) registry->changed_flags |= entry->view.flags;
    return true;
}

static bool apply_latched_variables(qa_cvars *registry, const char *name, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar registry is NULL");
    for (cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        if ((name != NULL && !name_equal(registry, name, entry->view.name)) || entry->view.latched_value == NULL) continue;
        const char *pending = entry->view.latched_value;
        entry->view.latched_value = NULL;
        if (!apply_value(registry, entry, pending, registry->options.dialect == QA_CONSOLE_Q3, error)) {
            entry->view.latched_value = pending;
            return false;
        }
        free((char *)pending);
        if (qac_q2(registry->options.dialect) && strcmp(entry->view.name, "game") == 0)
            effect(registry, QA_CVAR_EFFECT_GAME_DIRECTORY, entry);
    }
    return true;
}

static bool reset_variable(qa_cvars *registry, const char *name, bool force, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    cvar *entry = find_variable(registry, name);
    if (entry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "cannot reset unregistered cvar");
    return qa_cvars_set(registry, name, entry->view.reset_value, force, error);
}

static bool restart_variables(qa_cvars *registry, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL || registry->options.dialect != QA_CONSOLE_Q3)
        return qac_fail(error, QA_ERROR_ARGUMENT, "cvar restart requires a Q3 registry");
    cvar **link = &registry->first;
    while (*link != NULL) {
        cvar *entry = *link;
        if ((entry->view.flags & (QA_CVAR_READONLY | QA_CVAR_INIT | QA_CVAR_NO_RESTART)) != 0) {
            link = &entry->next;
        } else if ((entry->view.flags & QA_CVAR_USER_CREATED) != 0) {
            *link = entry->next;
            qac_cvars_entry_free(entry);
            --registry->count;
        } else {
            if (!qa_cvars_set(registry, entry->view.name, entry->view.reset_value, true, error)) return false;
            link = &entry->next;
        }
    }
    return true;
}

static bool set_cheats_variables(qa_cvars *registry, bool allowed, qa_error *error)
{
    if (!qac_cvars_touch(registry, error)) return false;
    if (registry == NULL) return qac_fail(error, QA_ERROR_ARGUMENT, "cvar registry is NULL");
    registry->cheats = allowed;
    if (allowed || registry->options.dialect != QA_CONSOLE_Q3) return true;
    for (cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        if ((entry->view.flags & QA_CVAR_CHEAT) == 0) continue;
        free((char *)entry->view.latched_value);
        entry->view.latched_value = NULL;
        if (!qa_cvars_set(registry, entry->view.name, entry->view.reset_value, true, error)) return false;
    }
    return true;
}

bool qa_cvars_register(qa_cvars *registry, const char *name, const char *value,
    uint32_t flags, uint64_t owner, const char *description, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,register_variable(registry,name,value,flags,owner,description,error),error);
}
bool qa_cvars_add_flags(qa_cvars *registry,const char *name,uint32_t flags,qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    bool ok=qac_cvars_touch(registry,error);
    cvar *entry=ok && name?find_variable(registry,source_name(registry,name)):NULL;
    if (ok && !entry) ok=qac_fail(error,QA_ERROR_NOT_FOUND,"Flag declaration requires its registered cvar");
    if (ok) entry->view.flags|=flags;
    return mutation_end(registry,ok,error);
}
bool qa_cvars_set(qa_cvars *registry, const char *name, const char *value, bool force, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,set_variable(registry,name,value,force,error),error);
}
bool qa_cvars_set_console(qa_cvars *registry, const char *name, const char *value, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,set_console_variable(registry,name,value,error),error);
}
bool qa_cvars_set_number(qa_cvars *registry, const char *name, float value, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,set_number_variable(registry,name,value,error),error);
}
bool qa_cvars_full_set(qa_cvars *registry, const char *name, const char *value, uint32_t flags, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,full_set_variable(registry,name,value,flags,error),error);
}
bool qa_cvars_set_flags(qa_cvars *registry, const char *name, const char *value, uint32_t flags, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,set_flags_variable(registry,name,value,flags,error),error);
}
bool qa_cvars_stage(qa_cvars *registry, const char *name, const char *value, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,stage_variable(registry,name,value,error),error);
}
bool qa_cvars_apply_latched(qa_cvars *registry, const char *name, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,apply_latched_variables(registry,name,error),error);
}
bool qa_cvars_reset(qa_cvars *registry, const char *name, bool force, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,reset_variable(registry,name,force,error),error);
}
bool qa_cvars_restart(qa_cvars *registry, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,restart_variables(registry,error),error);
}
bool qa_cvars_set_cheats(qa_cvars *registry, bool allowed, qa_error *error)
{
    if (!mutation_begin(registry,error)) return false;
    return mutation_end(registry,set_cheats_variables(registry,allowed,error),error);
}

void qa_cvars_set_server_active(qa_cvars *registry, bool active) { if (qac_cvars_touch(registry, NULL)) registry->server_active = active; }
void qa_cvars_set_high_characters(qa_cvars *registry, bool enabled) { if (qac_cvars_touch(registry, NULL)) registry->high_characters = enabled; }

void qa_cvars_remove_owner(qa_cvars *registry, uint64_t owner)
{
    if (!qac_cvars_touch(registry, NULL)) return;
    if (registry == NULL) return;
    cvar_observer *observer=registry->observers;
    while (observer) {
        cvar_observer *next=observer->next;
        if (observer->owner==owner) { observer->active=false; observer_release(registry,observer); }
        observer=next;
    }
    cvar **link = &registry->first;
    while (*link != NULL) {
        cvar *entry = *link;
        if (entry->bound && entry->binding.owner == owner) {
            entry->binding = (qa_cvar_binding){0};
            entry->bound = false;
        }
        if (entry->view.owner != owner) { link = &entry->next; continue; }
        *link = entry->next;
        registry->changed_flags |= entry->view.flags;
        if ((entry->view.flags & QA_CVAR_USERINFO) != 0) registry->userinfo_modified = true;
        --registry->count;
        qac_cvars_entry_free(entry);
    }
}

uint32_t qa_cvars_take_modified_flags(qa_cvars *registry)
{
    if (!qac_cvars_touch(registry, NULL)) return 0;
    uint32_t flags = registry->changed_flags;
    registry->changed_flags = 0;
    return flags;
}

void qa_cvars_mark_modified_flags(qa_cvars *registry, uint32_t flags)
{
    if (!qac_cvars_touch(registry, NULL)) return;
    registry->changed_flags |= flags;
}

void qa_cvars_clear_modified(qa_cvars *registry, const char *name)
{
    if (!qac_cvars_touch(registry, NULL)) return;
    cvar *entry = find_variable(registry, name);
    if (entry != NULL) entry->view.modified = false;
}

bool qa_cvars_take_userinfo_modified(qa_cvars *registry)
{
    if (!qac_cvars_touch(registry, NULL)) return false;
    bool modified = registry->userinfo_modified;
    registry->userinfo_modified = false;
    return modified;
}

bool qa_cvars_info(const qa_cvars *registry, uint32_t flags, size_t maximum_length,
                    qa_buffer *out, qa_error *error)
{
    if (registry == NULL || out == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar info arguments");
    qa_console_dialect dialect = registry->options.dialect;
    if (maximum_length == 0) maximum_length = dialect == QA_CONSOLE_Q3 ? 1024 : 512;
    qac_text result = {0};
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        const qa_cvar_view *value = &entry->view;
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
            bool strip = dialect != QA_CONSOLE_QW || (userinfo ? !qac_equal(value->name, "name") : !registry->high_characters);
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
            print_message(registry, NULL, "Info string length exceeded\n");
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

const char *qa_cvars_archive_value(const qa_cvars *registry, const qa_cvar_view *variable)
{
    qa_console_dialect dialect = registry->options.dialect;
    if ((variable->flags & QA_CVAR_ARCHIVE) == 0 ||
        (qac_q2(dialect) && (variable->flags & q2_no_archive) != 0) ||
        (dialect == QA_CONSOLE_Q3 && qac_equal(variable->name, "cl_cdkey"))) return NULL;
    return dialect == QA_CONSOLE_Q3 && variable->latched_value != NULL
        ? variable->latched_value : variable->value;
}

bool qa_cvars_config_filtered(const qa_cvars *registry, qa_cvar_config_filter filter,
                               void *context, qa_buffer *out, qa_error *error)
{
    if (registry == NULL || out == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid cvar config arguments");
    qac_text result = {0};
    qa_console_dialect dialect = registry->options.dialect;
    for (const cvar *entry = registry->first; entry != NULL; entry = entry->next) {
        const qa_cvar_view *variable = &entry->view;
        const char *value = qa_cvars_archive_value(registry, variable);
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

bool qa_cvars_config(const qa_cvars *registry, qa_buffer *out, qa_error *error)
{
    return qa_cvars_config_filtered(registry, NULL, NULL, out, error);
}
