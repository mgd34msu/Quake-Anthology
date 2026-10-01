#include "internal.h"
#include "qa/input_release.h"
#include "qa/console_release.h"

typedef struct release_record {
    qa_held_binding held;
    qa_console_release *program;
    bool action, complete;
} release_record;
struct qa_input_release {
    qa_input_seat *seat;
    qa_input_release_scope scope;
    int *keys;
    qa_held_binding *snapshot;
    size_t held_count;
    release_record *records;
    size_t count, cursor;
    double time_ms;
    bool advancing, entered, complete, metadata_entered;
    qa_error fault;
};
static bool fail(qa_error *error,const char *text)
{ qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",text); return false; }
bool qa_input_release_idle(const qa_input_seat *seat)
{ return !seat || !seat->release; }
const qa_console *qa_input_release_console(const qa_input_release *owner)
{ return owner ? owner->seat->options.console : NULL; }
static bool scope_valid(const qa_input_release_scope *scope)
{
    if (!scope || scope->controller<-1 || (scope->key_count && !scope->keys) ||
        scope->key_count>SIZE_MAX/sizeof(int)) return false;
    for (size_t i=0;i<scope->key_count;++i)
        if (scope->keys[i]<0 || scope->keys[i]>UINT16_MAX) return false;
    return true;
}
static bool key_selected(const qa_input_release_scope *scope,uint32_t key)
{
    for (size_t i=0;i<scope->key_count;++i) if ((uint32_t)scope->keys[i]==key) return true;
    return false;
}
static bool selected(const qa_input_release_scope *scope,qa_physical_input input)
{
    return scope->all || (input.kind==QA_PHYSICAL_KEY && key_selected(scope,input.code)) ||
        (input.kind>=QA_PHYSICAL_BUTTON && scope->controller>=0 && input.device==scope->controller);
}
bool qa_input_release_mutation_access(const qa_input_seat *seat,qa_error *error)
{ return !seat || !seat->release || fail(error,"input continuation is retained by its source release"); }
bool qa_input_release_action_access(const qa_input_seat *seat,qa_error *error)
{
    if (!seat || !seat->release) return true;
    const qa_input_release *owner=seat->release;
    if (owner->advancing && owner->cursor<owner->count &&
        qa_console_release_active(owner->records[owner->cursor].program)) return true;
    return fail(error,"input action is retained by its source release");
}
static void storage_free(qa_input_release *owner)
{
    free(owner->keys); free(owner->snapshot); free(owner->records); free(owner);
}
bool qa_input_release_prepare(qa_input_seat *seat,const qa_input_release_scope *scope,
    double time,qa_input_release **out,qa_error *error)
{
    if (!seat || seat->release || !scope_valid(scope) || !out || *out || !isfinite(time) || time<0 ||
        !qa_input_seat_context_ready(seat,&seat->options.context,error))
        return fail(error,"source release preparation requires its returned actual input seat and scope");
    if (seat->held_count>SIZE_MAX/sizeof(qa_held_binding) || seat->held_count>SIZE_MAX/sizeof(release_record))
        return fail(error,"source release held records exceed address space");
    qa_input_release *owner=calloc(1,sizeof(*owner));
    if (!owner) { qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating prepared input release"); return false; }
    owner->seat=seat; owner->scope=*scope; owner->time_ms=time; owner->held_count=seat->held_count;
    owner->keys=scope->key_count?malloc(scope->key_count*sizeof(*owner->keys)):NULL;
    owner->snapshot=seat->held_count?malloc(seat->held_count*sizeof(*owner->snapshot)):NULL;
    owner->records=seat->held_count?calloc(seat->held_count,sizeof(*owner->records)):NULL;
    if ((scope->key_count && !owner->keys) || (seat->held_count && (!owner->snapshot || !owner->records))) {
        storage_free(owner); qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining physical source release requirements"); return false;
    }
    if (scope->key_count) memcpy(owner->keys,scope->keys,scope->key_count*sizeof(*owner->keys));
    owner->scope.keys=owner->keys;
    if (seat->held_count) memcpy(owner->snapshot,seat->held,seat->held_count*sizeof(*owner->snapshot));
    bool ok=true;
    for (size_t i=0;ok && i<seat->held_count;++i) {
        const qa_held_binding *held=&seat->held[i];
        if (!selected(scope,held->input)) continue;
        release_record *record=&owner->records[owner->count++]; record->held=*held;
        const qa_input_binding *binding=held->binding?&held->binding->view:NULL;
        record->action=binding && binding->kind==QA_BIND_ACTION;
        if (!binding || record->action) continue;
        const char *text=NULL;
        ok=qa_input_binding_release_text(seat,held,time,&text,error);
        qa_command_context context=seat->options.context;
        context.script="key-binding"; context.direct=false;
        if (ok) ok=qa_console_release_prepare(seat->options.console,&context,text,&record->program,error);
    }
    if (!ok) {
        for (size_t i=0;i<owner->count;++i) if (owner->records[i].program) {
            qa_console_release_outcome outcome;
            qa_console_release_abort(owner->records[i].program,&outcome,NULL);
        }
        storage_free(owner); return false;
    }
    seat->release=owner; *out=owner; return true;
}
static qa_input_release_outcome outcome(const qa_input_release *owner)
{
    if (owner->complete) return owner->fault.code==QA_OK?QA_INPUT_RELEASE_COMPLETED:QA_INPUT_RELEASE_FAILED;
    return owner->entered?QA_INPUT_RELEASE_WAITING:QA_INPUT_RELEASE_UNENTERED;
}
static void enter_metadata(qa_input_release *owner)
{
    if (owner->metadata_entered) return;
    qa_input_seat *seat=owner->seat;
    for (size_t i=0;i<owner->count;++i) {
        release_record *record=&owner->records[i];
        if (record->program) continue;
        if (record->action) {
            const qa_input_binding *binding=&record->held.binding->view;
            qa_input_button_up(&seat->buttons[binding->action],
                qa_input_physical_source(record->held.input),owner->time_ms,10);
            owner->entered=true;
        }
        record->complete=true;
    }
    if (owner->scope.all) {
        for (size_t i=0;i<QA_INPUT_ACTION_COUNT;++i) qa_input_button_release(&seat->buttons[i],owner->time_ms);
        seat->mouse=(qa_input_pair){0}; seat->impulse=0;
    }
    if (owner->scope.all || owner->scope.clear_gamepad || owner->scope.controller>=0)
        qa_gamepad_clear(&seat->gamepad);
    if (owner->scope.clear_gamepad || owner->scope.controller>=0)
        qa_gamepad_calibration_reset(&seat->gamepad);
    owner->metadata_entered=true;
    if (owner->scope.all || owner->scope.clear_gamepad || owner->scope.controller>=0) owner->entered=true;
}
bool qa_input_release_advance(qa_input_release *owner,qa_input_release_outcome *out,qa_error *error)
{
    if (!owner || !out || owner->seat->release!=owner || owner->advancing ||
        !qa_console_idle(owner->seat->options.console)) return fail(error,"input release advance requires its returned actual owner");
    *out=outcome(owner);
    if (owner->complete) {
        if (owner->fault.code!=QA_OK) { if (error && error->code==QA_OK) *error=owner->fault; return false; }
        return true;
    }
    if (!qa_input_seat_context_ready(owner->seat,&owner->seat->options.context,error)) return false;
    owner->advancing=true;
    enter_metadata(owner);
    bool ok=true;
    while (owner->cursor<owner->count) {
        release_record *record=&owner->records[owner->cursor];
        if (record->complete) { ++owner->cursor; continue; }
        qa_error fault={0};
        if (record->program) {
            qa_console_release_outcome result=QA_CONSOLE_RELEASE_UNENTERED;
            bool advanced=qa_console_release_advance(record->program,&result,&fault);
            if (qa_console_release_entered(record->program)) owner->entered=true;
            if (result==QA_CONSOLE_RELEASE_COMPLETED || result==QA_CONSOLE_RELEASE_FAILED) record->complete=true;
            if (!advanced) {
                if (fault.code==QA_OK) qa_error_set(&fault,QA_ERROR_ARGUMENT,0,"Actual held source release failed");
                if (owner->fault.code==QA_OK) owner->fault=fault;
                ok=false;
            }
            if (!record->complete) break;
        }
        ++owner->cursor;
        if (!ok) break;
    }
    owner->advancing=false;
    owner->complete=owner->cursor==owner->count;
    *out=outcome(owner);
    if (!ok || owner->fault.code!=QA_OK) {
        if (error && error->code==QA_OK) *error=owner->fault;
        return false;
    }
    return true;
}
static bool physical_ready(const qa_input_release *owner,const qa_input_seat *seat,
    const qa_input_release_scope *required,qa_error *error)
{
    if (!owner || !seat || owner->seat!=seat || seat->release!=owner || owner->advancing ||
        !scope_valid(required) ||
        (!owner->scope.all && required->all) ||
        (required->clear_gamepad && !owner->scope.clear_gamepad && owner->scope.controller<0) ||
        (!owner->scope.all && required->controller>=0 && owner->scope.controller!=required->controller) ||
        seat->held_count!=owner->held_count) return fail(error,"input release has no completed matching physical proof");
    for (size_t i=0;i<required->key_count;++i)
        if (!owner->scope.all && !key_selected(&owner->scope,(uint32_t)required->keys[i]))
            return fail(error,"input release does not cover the retained native key");
    for (size_t i=0;i<owner->held_count;++i)
        if (!qa_input_physical_equal(owner->snapshot[i].input,seat->held[i].input) ||
            owner->snapshot[i].binding!=seat->held[i].binding)
            return fail(error,"actual held input changed during source release");
    return true;
}
bool qa_input_release_ready(const qa_input_release *owner,const qa_input_seat *seat,
    const qa_input_release_scope *required,qa_error *error)
{
    if (!physical_ready(owner,seat,required,error) || !owner->complete || owner->fault.code!=QA_OK)
        return fail(error,"input release has no completed matching physical proof");
    for (size_t i=0;i<owner->count;++i)
        if (owner->records[i].program &&
            !qa_console_release_ready(owner->records[i].program,seat->options.console,error)) return false;
    return qa_input_seat_context_ready(seat,&seat->options.context,error);
}
bool qa_input_release_scope_owned(const qa_input_release *owner,const qa_input_seat *seat,
    const qa_input_release_scope *required,qa_error *error)
{
    return physical_ready(owner,seat,required,error) &&
        (qa_console_idle(seat->options.console) || fail(error,"retained input source callback has not returned"));
}
bool qa_input_release_retirement_scope_ready(const qa_input_release *owner,const qa_input_seat *seat,
    const qa_input_release_scope *required,qa_console_release_disposition disposition,
    qa_console_release_retirement_fn qualify,void *context,qa_error *error)
{
    if (!qualify || !physical_ready(owner,seat,required,error) ||
        disposition<QA_CONSOLE_RELEASE_RETIRED_ACTOR || disposition>QA_CONSOLE_RELEASE_DETACHED_SOURCE ||
        !qa_console_idle(seat->options.console))
        return fail(error,"input retirement lacks its retained physical scope and source qualifier");
    for (size_t i=0;i<owner->count;++i)
        if (owner->records[i].program && !qa_console_release_retirement_ready(owner->records[i].program,
            disposition,qualify,context,error)) return false;
    return true;
}
void qa_input_release_publish(qa_input_release *owner)
{
    qa_input_seat *seat=owner->seat;
    for (size_t i=0;i<seat->held_count;) {
        qa_held_binding *held=&seat->held[i];
        if (!selected(&owner->scope,held->input)) { ++i; continue; }
        qa_input_binding_record_release(held->binding);
        memmove(held,held+1,(seat->held_count-i-1)*sizeof(*held)); --seat->held_count;
    }
    for (size_t i=0;i<owner->count;++i)
        if (owner->records[i].program) qa_console_release_publish(owner->records[i].program);
    seat->release=NULL; storage_free(owner);
}
bool qa_input_release_abort(qa_input_release *owner,qa_input_release_outcome *out,qa_error *error)
{
    if (!owner || !out || owner->seat->release!=owner || owner->advancing ||
        (owner->entered && !owner->complete)) return fail(error,"input release abort requires unentered or terminal continuation");
    *out=outcome(owner);
    if (owner->entered && owner->fault.code!=QA_OK) {
        if (error && error->code==QA_OK) *error=owner->fault;
        return false;
    }
    for (size_t i=0;i<owner->count;++i) if (owner->records[i].program) {
        qa_console_release_outcome result;
        if (!qa_console_release_abort(owner->records[i].program,&result,error)) return false;
        owner->records[i].program=NULL;
    }
    if (owner->entered) qa_input_release_publish(owner);
    else { owner->seat->release=NULL; storage_free(owner); }
    return true;
}
bool qa_input_release_retirement_ready(const qa_input_release *owner,qa_console_release_disposition disposition,
    qa_console_release_retirement_fn qualify,void *context,qa_error *error)
{
    if (!owner || !qualify || owner->seat->release!=owner || owner->advancing ||
        !qa_console_idle(owner->seat->options.console))
        return fail(error,"input release retirement requires its returned actual source parents");
    bool captured=false;
    for (size_t i=0;i<owner->count;++i) if (owner->records[i].program) {
        captured=true;
        if (!qa_console_release_retirement_ready(owner->records[i].program,disposition,qualify,context,error)) return false;
    }
    if (!captured) return fail(error,"input release has no captured source actor history");
    return true;
}
void qa_input_release_retirement_publish(qa_input_release *owner)
{
    enter_metadata(owner);
    for (size_t i=0;i<owner->count;++i) if (owner->records[i].program) {
        qa_console_release_retirement_publish(owner->records[i].program);
        owner->records[i].program=NULL;
    }
    qa_input_release_publish(owner);
}
bool qa_input_release_retire(qa_input_release *owner,qa_console_release_disposition disposition,
    qa_console_release_retirement_fn qualify,void *context,qa_error *error)
{
    if (!qa_input_release_retirement_ready(owner,disposition,qualify,context,error)) return false;
    qa_input_release_retirement_publish(owner); return true;
}
