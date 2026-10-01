#include "input_settings.h"
#include "capture.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_engine_shutdown.h"

struct frontend_input_settings {
    qa_frontend *frontend;
    qa_application *application;
    qa_input_platform *platform;
    frontend_seat *seats;
    unsigned seat_count;
    qa_input_seat *physical[QA_INPUT_LOCAL_SEATS];
    qa_input_platform_settings_ticket *native;
    qa_input_release *release[QA_INPUT_LOCAL_SEATS];
    qa_input_release_outcome source[QA_INPUT_LOCAL_SEATS];
    double now_ms;
    bool prepared, aborting, terminal;
    qa_error failure;
};
static bool fail(qa_error *error,const char *text)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,text); }
static void retain_failure(frontend_input_settings *owner,const qa_error *error)
{
    if (owner->failure.code==QA_OK && error && error->code!=QA_OK) owner->failure=*error;
}
static bool parents_returned(const frontend_input_settings *owner,
    const qa_frontend *frontend,qa_error *error)
{
    if (!owner || !frontend || owner->frontend!=frontend ||
        owner->application!=frontend->application || owner->platform!=frontend->input ||
        owner->seats!=frontend->seats || owner->seat_count!=frontend->options.seats ||
        !frontend_seat_callbacks_returned(frontend))
        return fail(error,"Input settings lost their returned actual frontend parents");
    for (unsigned slot=0;slot<owner->seat_count;++slot)
        if (frontend->seats[slot].frontend!=frontend || frontend->seats[slot].id!=slot ||
            frontend->seats[slot].input!=owner->physical[slot])
            return fail(error,"Input settings lost their retained physical input route");
    return true;
}
bool frontend_input_settings_current(const frontend_input_settings *owner,
    const qa_frontend *frontend,qa_error *error)
{
    return parents_returned(owner,frontend,error) &&
        (!frontend->stepping || fail(error,"Input settings require a returned frontend driver"));
}
static frontend_input_settings *create(qa_frontend *frontend,double now,
    frontend_input_settings **out,qa_error *error)
{
    if (!frontend || !out || *out || !frontend->application || !frontend->input ||
        frontend->options.dedicated || !frontend->seats ||
        !frontend->options.seats || frontend->options.seats>QA_INPUT_LOCAL_SEATS ||
        frontend->stepping || !frontend_seat_callbacks_idle(frontend) ||
        !qa_input_platform_settings_idle(frontend->input) || !isfinite(now) || now<0) {
        fail(error,"Input settings require their returned actual platform and physical seats"); return NULL;
    }
    frontend_input_settings *owner=calloc(1,sizeof(*owner));
    if (!owner) { frontend_fail(error,QA_ERROR_MEMORY,"Allocating retained input settings"); return NULL; }
    owner->frontend=frontend; owner->application=frontend->application;
    owner->platform=frontend->input; owner->seats=frontend->seats;
    owner->seat_count=frontend->options.seats; owner->now_ms=now;
    for (unsigned slot=0;slot<owner->seat_count;++slot) {
        qa_input_seat *seat=frontend->seats[slot].input;
        if (!seat || frontend->seats[slot].frontend!=frontend || frontend->seats[slot].id!=slot ||
            qa_input_seat_ordinal(seat)!=slot || !qa_input_release_idle(seat)) {
            free(owner); fail(error,"Input settings require idle actual physical input seats"); return NULL;
        }
        owner->physical[slot]=seat;
    }
    *out=owner; return owner;
}
static bool prepare_releases(frontend_input_settings *owner,qa_error *error)
{
    if (!owner->native || qa_input_platform_settings_owner(owner->native)!=owner->platform)
        return fail(error,"Input settings have no prepared actual native ticket");
    qa_input_platform_settings_requirements requirements;
    if (!qa_input_platform_settings_requirements_read(owner->native,&requirements,error)) return false;
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) {
        qa_input_seat *seat=qa_input_platform_settings_seat(owner->native,slot);
        if (seat!=(slot<owner->seat_count?owner->physical[slot]:NULL))
            return fail(error,"Native settings do not retain the frontend physical input seat");
        int keys[528]; qa_input_release_scope scope;
        if (!qa_input_platform_settings_release_scope(owner->native,slot,&scope,keys,528,error)) return false;
        bool source=seat && requirements.source_changed && requirements.source_slot==(int)slot;
        bool midi=seat && requirements.midi_changed && requirements.midi_slot==(int)slot;
        if (scope.all || scope.clear_gamepad || scope.controller>=0 || scope.key_count || source || midi)
            if (!qa_input_release_prepare(seat,&scope,owner->now_ms,&owner->release[slot],error)) return false;
    }
    owner->prepared=true; return true;
}
bool frontend_input_settings_prepare(qa_frontend *frontend,const qa_input_platform_settings *desired,
    qa_input_seat *const configuration[QA_INPUT_LOCAL_SEATS],double now,
    frontend_input_settings **out,qa_error *error)
{
    frontend_input_settings *owner=create(frontend,now,out,error);
    if (!owner) return false;
    bool ok=qa_input_platform_settings_prepare(owner->platform,desired,configuration,now,&owner->native,error) &&
        prepare_releases(owner,error);
    if (!ok) retain_failure(owner,error);
    return ok;
}
bool frontend_input_settings_reconnect_prepare(qa_frontend *frontend,double now,
    frontend_input_settings **out,qa_error *error)
{
    frontend_input_settings *owner=create(frontend,now,out,error);
    if (!owner) return false;
    bool ok=qa_input_platform_reconnect_prepare(owner->platform,now,&owner->native,error);
    if (ok && !owner->native) { *out=NULL; free(owner); return true; }
    if (ok) ok=prepare_releases(owner,error);
    if (!ok) retain_failure(owner,error);
    return ok;
}
bool frontend_input_settings_read(const frontend_input_settings *owner,
    frontend_input_settings_view *out,qa_error *error)
{
    if (!out || !owner || !frontend_input_settings_current(owner,owner->frontend,error)) return false;
    frontend_input_settings_view view={.prepared=owner->prepared,.aborting=owner->aborting,
        .terminal=owner->terminal,.failure=owner->failure,
        .native=qa_input_platform_settings_result(owner->native)};
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) {
        view.source[slot]=owner->source[slot];
        if (owner->release[slot]) view.retained_sources|=1u<<slot;
    }
    *out=view; return true;
}
static void proofs(const frontend_input_settings *owner,const qa_input_release *out[QA_INPUT_LOCAL_SEATS])
{ for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) out[slot]=owner->release[slot]; }
bool frontend_input_settings_advance(frontend_input_settings *owner,bool *complete,qa_error *error)
{
    if (!complete || !owner || !owner->prepared || owner->terminal ||
        !frontend_input_settings_current(owner,owner->frontend,error))
        return fail(error,"Input settings cannot advance outside their retained returned boundary");
    *complete=false;
    if (owner->failure.code!=QA_OK && !owner->aborting) {
        if (error && error->code==QA_OK) *error=owner->failure;
        return false;
    }
    bool waiting=false;
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
        qa_error fault={0};
        bool ok=qa_input_release_advance(owner->release[slot],&owner->source[slot],&fault);
        if (!ok) { retain_failure(owner,&fault); if (error && error->code==QA_OK) *error=fault; return false; }
        waiting|=owner->source[slot]!=QA_INPUT_RELEASE_COMPLETED;
    }
    if (waiting) return true;
    if (owner->aborting) {
        bool ok=frontend_input_settings_abort(owner,error);
        *complete=owner->terminal; return ok;
    }
    const qa_input_release *release[QA_INPUT_LOCAL_SEATS]; proofs(owner,release);
    qa_input_platform_settings_outcome result;
    bool ok=qa_input_platform_settings_enter(owner->native,release,&result,error) &&
        qa_input_platform_settings_ready(owner->native,release,error);
    if (!ok) retain_failure(owner,error);
    *complete=ok; return ok;
}
bool frontend_input_settings_ready(const frontend_input_settings *owner,qa_error *error)
{
    if (!owner || !owner->prepared || owner->aborting || owner->terminal ||
        !frontend_input_settings_current(owner,owner->frontend,error))
        return fail(error,"Input settings have no complete retained publication");
    const qa_input_release *release[QA_INPUT_LOCAL_SEATS]; proofs(owner,release);
    return qa_input_platform_settings_ready(owner->native,release,error);
}
void frontend_input_settings_publish(frontend_input_settings *owner)
{
    qa_input_platform_settings_publish(owner->native);
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
        qa_input_release_publish(owner->release[slot]); owner->release[slot]=NULL;
    }
    owner->terminal=true;
}
bool frontend_input_settings_abort(frontend_input_settings *owner,qa_error *error)
{
    if (!owner || owner->terminal || !frontend_input_settings_current(owner,owner->frontend,error))
        return fail(error,"Input settings abort requires its retained actual parents");
    if (qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_ENTERED)
        return frontend_input_settings_retire_entered(owner,error);
    owner->aborting=true;
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
        if (!qa_input_release_abort(owner->release[slot],&owner->source[slot],error)) return false;
        owner->release[slot]=NULL;
    }
    bool ok=!owner->native || qa_input_platform_settings_abort(owner->native,error);
    if (!owner->native || qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_ABORTED)
        owner->terminal=true;
    if (!ok) retain_failure(owner,error);
    return ok;
}
bool frontend_input_settings_retire_entered(frontend_input_settings *owner,qa_error *error)
{
    if (!owner || owner->terminal || !owner->prepared ||
        !frontend_input_settings_current(owner,owner->frontend,error) ||
        qa_input_platform_settings_result(owner->native)!=QA_INPUT_PLATFORM_SETTINGS_ENTERED)
        return fail(error,"Entered input retirement requires its retained native and completed source parents");
    owner->aborting=true;
    const qa_input_release *release[QA_INPUT_LOCAL_SEATS]; proofs(owner,release);
    bool ok=qa_input_platform_settings_retire_entered(owner->native,release,error);
    if (qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_RETIRED) {
        for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
            qa_input_release_publish(owner->release[slot]); owner->release[slot]=NULL;
        }
        owner->terminal=true;
    }
    if (!ok) retain_failure(owner,error);
    return ok;
}
static bool retirement(void *user,const qa_console *console,const qa_command_context *command,
    qa_console_release_disposition disposition,qa_error *error)
{
    frontend_input_settings *owner=user;
    if (!parents_returned(owner,owner->frontend,error)) return false;
    if (disposition==QA_CONSOLE_RELEASE_DETACHED_SOURCE)
        return qa_application_startup_source_retiring(owner->application,console,command) ||
            qa_application_engine_shutdown_retiring(owner->application,console,command) ||
            fail(error,"Source release has no entered detached physical source authority");
    if (!frontend_input_settings_current(owner,owner->frontend,error)) return false;
    if (disposition!=QA_CONSOLE_RELEASE_RETIRED_ACTOR || !command->actor.registry ||
        command->actor.registry!=command->registry)
        return fail(error,"Source release has no captured actor retirement authority");
    const qa_actor_registry *registry=qa_session_actors(qa_application_session(owner->application));
    return (registry && qa_actors_identity(registry)==command->registry &&
        !qa_actors_get(registry,command->actor)) || fail(error,"Captured source actor remains live or belongs to another registry");
}
bool frontend_input_settings_engine_shutdown(frontend_input_settings *owner,
    const qa_application_engine_shutdown *loan,bool *complete,qa_error *error)
{
    if (complete) *complete=false;
    if (!complete || !owner || !parents_returned(owner,owner->frontend,error) ||
        owner->frontend->stepping || qa_application_engine_shutdown_owner(loan)!=owner->application)
        return fail(error,"Input shutdown requires its actual detached ENGINE and returned physical parents");
    qa_console *console=NULL; qa_cvars *cvars=NULL;
    if (!qa_application_engine_shutdown_read(loan,&console,&cvars,error)) return false;
    (void)cvars;
    if (owner->terminal) { *complete=true; return true; }
    /* Admit every retained history before native cleanup or the first consume.
     * The physical scope comes from the same native owner that prepared it. */
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
        int keys[528]; qa_input_release_scope scope;
        if (qa_input_release_console(owner->release[slot])!=console ||
            !qa_input_platform_settings_release_scope(owner->native,slot,&scope,keys,528,error) ||
            !qa_input_release_retirement_scope_ready(owner->release[slot],owner->physical[slot],&scope,
                QA_CONSOLE_RELEASE_DETACHED_SOURCE,retirement,owner,error)) return false;
    }
    owner->aborting=true;
    const qa_input_release *release[QA_INPUT_LOCAL_SEATS]; proofs(owner,release);
    bool entered=qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_ENTERED;
    bool ok=!owner->native || (entered?
        qa_input_platform_settings_retire_entered_disposition(owner->native,release,
            QA_CONSOLE_RELEASE_DETACHED_SOURCE,retirement,owner,error):
        qa_input_platform_settings_abort(owner->native,error));
    qa_input_platform_settings_outcome result=qa_input_platform_settings_result(owner->native);
    if (!owner->native || result==QA_INPUT_PLATFORM_SETTINGS_ABORTED || result==QA_INPUT_PLATFORM_SETTINGS_RETIRED) {
        for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
            qa_input_release_retirement_publish(owner->release[slot]); owner->release[slot]=NULL;
        }
        owner->terminal=true; *complete=true;
    }
    if (!ok) retain_failure(owner,error);
    return ok;
}
bool frontend_input_settings_retire_actor(frontend_input_settings *owner,qa_error *error)
{
    if (!owner || owner->terminal || !frontend_input_settings_current(owner,owner->frontend,error)) return false;
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
        int keys[528]; qa_input_release_scope scope;
        if (!qa_input_platform_settings_release_scope(owner->native,slot,&scope,keys,528,error) ||
            !qa_input_release_retirement_scope_ready(owner->release[slot],owner->physical[slot],&scope,
                QA_CONSOLE_RELEASE_RETIRED_ACTOR,retirement,owner,error)) return false;
    }
    owner->aborting=true;
    const qa_input_release *release[QA_INPUT_LOCAL_SEATS]; proofs(owner,release);
    bool entered=qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_ENTERED;
    bool ok=!owner->native || (entered?
        qa_input_platform_settings_retire_entered_disposition(owner->native,release,
            QA_CONSOLE_RELEASE_RETIRED_ACTOR,retirement,owner,error):
        qa_input_platform_settings_abort(owner->native,error));
    qa_input_platform_settings_outcome result=qa_input_platform_settings_result(owner->native);
    if (!owner->native || result==QA_INPUT_PLATFORM_SETTINGS_ABORTED || result==QA_INPUT_PLATFORM_SETTINGS_RETIRED) {
        for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
            qa_input_release_retirement_publish(owner->release[slot]); owner->release[slot]=NULL;
        }
        owner->terminal=true;
    }
    if (!ok) retain_failure(owner,error);
    return ok;
}
bool frontend_input_settings_retire_source(frontend_input_settings *owner,qa_application *application,
    const qa_console *console,qa_error *error)
{
    if (!owner || !console || owner->application!=application || owner->terminal ||
        !parents_returned(owner,owner->frontend,error)) return false;
    if (qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_ENTERED) {
        /* A detached source cannot supply current-command completion. Admit
         * every retained native scope through the real retirement loan before
         * disposing an endpoint or consuming any captured programme. */
        for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
            int keys[528]; qa_input_release_scope scope;
            if (!qa_input_platform_settings_release_scope(owner->native,slot,&scope,keys,528,error) ||
                !qa_input_release_retirement_scope_ready(owner->release[slot],owner->physical[slot],&scope,
                    QA_CONSOLE_RELEASE_DETACHED_SOURCE,retirement,owner,error)) return false;
        }
        owner->aborting=true;
        const qa_input_release *release[QA_INPUT_LOCAL_SEATS]; proofs(owner,release);
        bool ok=qa_input_platform_settings_retire_entered_disposition(owner->native,release,
            QA_CONSOLE_RELEASE_DETACHED_SOURCE,retirement,owner,error);
        if (qa_input_platform_settings_result(owner->native)==QA_INPUT_PLATFORM_SETTINGS_RETIRED) {
            for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot]) {
                qa_input_release_retirement_publish(owner->release[slot]); owner->release[slot]=NULL;
            }
            owner->terminal=true;
        }
        if (!ok) retain_failure(owner,error);
        return ok;
    }
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot] &&
        qa_input_release_console(owner->release[slot])==console) {
        if (!qa_input_release_retirement_ready(owner->release[slot],QA_CONSOLE_RELEASE_DETACHED_SOURCE,
            retirement,owner,error)) return false;
    }
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot) if (owner->release[slot] &&
        qa_input_release_console(owner->release[slot])==console) {
        owner->aborting=true;
        qa_input_release_retirement_publish(owner->release[slot]);
        owner->release[slot]=NULL;
    }
    return true;
}
const char *frontend_input_settings_diagnostic(const frontend_input_settings *owner)
{ return owner ? qa_input_platform_settings_diagnostic(owner->native) : NULL; }
bool frontend_input_settings_destroy(frontend_input_settings **in,qa_error *error)
{
    if (!in || !*in) return true;
    frontend_input_settings *owner=*in;
    if (!owner->terminal || !frontend_input_settings_current(owner,owner->frontend,error))
        return fail(error,"Input settings destruction retains a nonterminal actual owner");
    for (unsigned slot=0;slot<QA_INPUT_LOCAL_SEATS;++slot)
        if (owner->release[slot]) return fail(error,"Input settings destruction still retains source history");
    bool ok=qa_input_platform_settings_ticket_destroy(owner->native,error);
    *in=NULL; free(owner); return ok;
}
