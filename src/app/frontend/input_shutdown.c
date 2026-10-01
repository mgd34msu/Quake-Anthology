#include "input_shutdown.h"
#include "capture.h"
#include "qa/input_release.h"

struct frontend_input_shutdown {
    qa_frontend *frontend;
    qa_application *application;
    frontend_seat *seats;
    unsigned count;
    qa_console *console;
    qa_input_seat *physical[QA_INPUT_LOCAL_SEATS];
    qa_input_release *release[QA_INPUT_LOCAL_SEATS];
    bool prepared,terminal;
    qa_error failure;
};
static const qa_input_release_scope all={.all=true,.controller=-1};
static bool fail(qa_error *error,const char *message)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,message); }
static bool returned(const frontend_input_shutdown *owner,qa_error *error)
{
    const qa_frontend *f=owner?owner->frontend:NULL;
    if (!f || f->application!=owner->application || f->seats!=owner->seats ||
        f->options.seats!=owner->count || f->stepping || f->preparing ||
        !frontend_seat_callbacks_returned(f))
        return fail(error,"Input shutdown lost its returned actual frontend parents");
    if (f->seats) for (unsigned slot=0;slot<owner->count;++slot)
        if (f->seats[slot].input!=owner->physical[slot] ||
            (owner->physical[slot] && (f->seats[slot].frontend!=f || f->seats[slot].id!=slot)))
            return fail(error,"Input shutdown lost its retained physical seat");
    return true;
}
bool frontend_input_shutdown_prepare(qa_frontend *f,double now,
    frontend_input_shutdown **out,qa_error *error)
{
    if (!f || !out || *out || !f->application || f->stepping || f->preparing ||
        f->options.seats>QA_INPUT_LOCAL_SEATS || !isfinite(now) || now<0 ||
        !frontend_seat_callbacks_idle(f))
        return fail(error,"Input shutdown requires returned actual physical seats");
    qa_console *console=qa_application_console(f->application);
    if (!console || !qa_console_idle(console))
        return fail(error,"Input shutdown requires the published physical ENGINE console");
    if (f->seats) for (unsigned slot=0;slot<f->options.seats;++slot)
        if (f->seats[slot].input && (f->seats[slot].frontend!=f || f->seats[slot].id!=slot ||
            qa_input_seat_ordinal(f->seats[slot].input)!=slot))
            return fail(error,"Final input preparation has an unqualified constructed physical seat");
    frontend_input_shutdown *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining final physical input release");
    owner->frontend=f; owner->application=f->application; owner->seats=f->seats;
    owner->count=f->options.seats; owner->console=console; *out=owner;
    if (f->seats) for (unsigned slot=0;slot<owner->count;++slot)
        owner->physical[slot]=f->seats[slot].input;
    bool ok=returned(owner,error);
    for (unsigned slot=0;ok && slot<owner->count;++slot) if (owner->physical[slot]) {
        ok=qa_input_seat_ordinal(owner->physical[slot])==slot ||
            fail(error,"Final input release has another physical ordinal");
        if (ok) ok=qa_input_release_prepare(owner->physical[slot],&all,now,&owner->release[slot],error);
        if (ok && qa_input_release_console(owner->release[slot])!=console)
            ok=fail(error,"Final input release does not borrow the actual ENGINE console");
    }
    owner->prepared=ok;
    if (!ok && error) owner->failure=*error;
    return ok;
}
bool frontend_input_shutdown_advance(frontend_input_shutdown *owner,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!complete || !returned(owner,error)) return false;
    if (owner->terminal) { *complete=true; return true; }
    if (owner->failure.code!=QA_OK) {
        if (error && error->code==QA_OK) *error=owner->failure;
        return false;
    }
    if (!owner->prepared || qa_application_console(owner->application)!=owner->console)
        return fail(error,"Final input release cannot replay failed or detached source history");
    bool waiting=false;
    for (unsigned slot=0;slot<owner->count;++slot) if (owner->release[slot]) {
        qa_input_release_outcome result; qa_error fault={0};
        if (!qa_input_release_advance(owner->release[slot],&result,&fault)) {
            if (fault.code==QA_OK) fail(&fault,"Actual final input release failed");
            owner->failure=fault; if (error && error->code==QA_OK) *error=fault;
            return false;
        }
        waiting|=result!=QA_INPUT_RELEASE_COMPLETED;
    }
    if (waiting) return true;
    for (unsigned slot=0;slot<owner->count;++slot) if (owner->release[slot] &&
        !qa_input_release_ready(owner->release[slot],owner->physical[slot],&all,error)) return false;
    for (unsigned slot=0;slot<owner->count;++slot) if (owner->release[slot]) {
        qa_input_release_publish(owner->release[slot]); owner->release[slot]=NULL;
    }
    owner->terminal=true; *complete=true; return true;
}
static bool retiring(void *user,const qa_console *console,const qa_command_context *command,
    qa_console_release_disposition disposition,qa_error *error)
{
    const frontend_input_shutdown *owner=user;
    return (returned(owner,error) && disposition==QA_CONSOLE_RELEASE_DETACHED_SOURCE &&
        console==owner->console && qa_application_engine_shutdown_retiring(owner->application,console,command)) ||
        fail(error,"Final input history has no actual detached ENGINE authority");
}
bool frontend_input_shutdown_retire(frontend_input_shutdown *owner,
    const qa_application_engine_shutdown *loan,qa_error *error)
{
    if (!returned(owner,error) || qa_application_engine_shutdown_owner(loan)!=owner->application)
        return fail(error,"Final input retirement requires its retained actual ENGINE loan");
    qa_console *console=NULL; qa_cvars *cvars=NULL;
    if (!qa_application_engine_shutdown_read(loan,&console,&cvars,error) || console!=owner->console)
        return fail(error,"Final input retirement lost its physical ENGINE console");
    (void)cvars;
    if (owner->terminal) return true;
    for (unsigned slot=0;slot<owner->count;++slot) if (owner->release[slot] &&
        !qa_input_release_retirement_scope_ready(owner->release[slot],owner->physical[slot],&all,
            QA_CONSOLE_RELEASE_DETACHED_SOURCE,retiring,owner,error)) return false;
    for (unsigned slot=0;slot<owner->count;++slot) if (owner->release[slot]) {
        qa_input_release_retirement_publish(owner->release[slot]); owner->release[slot]=NULL;
    }
    owner->terminal=true; return true;
}
bool frontend_input_shutdown_abort(frontend_input_shutdown *owner,qa_error *error)
{
    if (!returned(owner,error)) return false;
    if (owner->terminal) return true;
    for (unsigned slot=0;slot<owner->count;++slot) if (owner->release[slot]) {
        qa_input_release_outcome result;
        if (!qa_input_release_abort(owner->release[slot],&result,error)) return false;
        owner->release[slot]=NULL;
    }
    owner->terminal=true; return true;
}
bool frontend_input_shutdown_destroy(frontend_input_shutdown **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_input_shutdown *owner=*in;
    if (!returned(owner,error) || !owner->terminal)
        return fail(error,"Final input shutdown retains actual source history");
    for (unsigned slot=0;slot<owner->count;++slot)
        if (owner->release[slot]) return fail(error,"Final input shutdown still owns a physical release");
    free(owner); *in=NULL; return true;
}
