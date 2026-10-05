#include "seats_resize.h"
#include "internal.h"
#include "capture.h"
#include "input_settings.h"
#include "shared_settings.h"
#include "settings_devices.h"

static bool fail(qa_error *error,const char *message)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,message); }
static bool release_prepare(qa_frontend *f,qa_error *error)
{
    qa_input_platform_settings desired;
    if (!frontend_shared_settings_input_project(qa_application_cvars(f->application),NULL,false,
        &desired,error)) return false;
    qa_input_seat *configuration[QA_INPUT_LOCAL_SEATS]={0};
    for (unsigned slot=0;slot<f->options.seats;++slot) configuration[slot]=f->seats[slot].input;
    double now=(double)f->wall_time_ns/1000000.0;
    return frontend_input_settings_prepare(f,&desired,configuration,now,&f->input_settings,error) &&
        frontend_input_settings_release_all_prepare(f->input_settings,now,error) &&
        frontend_input_settings_reserve_all(f->input_settings,error);
}
static bool release_advance(qa_frontend *f,bool *complete,qa_error *error)
{
    if (!frontend_input_settings_release_advance(f->input_settings,complete,error)) return false;
    if (!*complete) return true;
    *complete=false;
    if (!frontend_input_settings_release_all_ready(f->input_settings,error) ||
        !frontend_input_settings_abort(f->input_settings,error) ||
        !frontend_input_settings_destroy(&f->input_settings,error)) return false;
    *complete=true; return true;
}
static void cleanup_error(qa_error *error,const qa_error *cleanup)
{
    qa_error original=*error;
    qa_error_set(error,cleanup->code,0,"%s; local seat cleanup: %s",original.message,cleanup->message);
}
static void grow_discard(qa_frontend *f,unsigned previous,unsigned next,qa_error *error)
{
    qa_error cleanup={0};
    if (!frontend_seats_destroy_range(f,previous,next,&cleanup)) cleanup_error(error,&cleanup);
    /* Native routes still contain the previous active prefix. Refused new
     * children remain in stable capacity slots for checked final destruction. */
    f->options.seats=previous;
}
static bool routes_resize(qa_frontend *f,unsigned previous,unsigned next,qa_error *error)
{
    qa_controller_selection controllers[QA_INPUT_LOCAL_SEATS]={0};
    int keyboard;
    if (!frontend_settings_keyboard_read(f,&keyboard))
        return fail(error,"Seat resize lost its actual keyboard route");
    for (unsigned slot=0;slot<previous && slot<next;++slot)
        if (!qa_input_platform_selection(f->input,slot,&controllers[slot]))
            return fail(error,"Seat resize lost its actual controller selector");
    if (keyboard>=(int)next) keyboard=0;
    if (next>previous) {
        f->options.seats=next;
        if (!frontend_seats_create_range(f,previous,error)) {
            grow_discard(f,previous,next,error);
            return false;
        }
    }
    qa_input_seat *routes[QA_INPUT_LOCAL_SEATS]={0};
    for (unsigned slot=0;slot<next;++slot) routes[slot]=f->seats[slot].input;
    double now=(double)f->wall_time_ns/1000000.0;
    bool routed=next<previous ? qa_input_platform_retain(f->input,(1u<<next)-1,keyboard,now,error) :
        qa_input_platform_routes(f->input,routes,controllers,keyboard,now,error);
    if (!routed) {
        qa_controller_selection actual;
        bool present=qa_input_platform_selection(f->input,next>previous?previous:next,&actual);
        bool published=next>previous?present:!present;
        if (!published) {
            if (next>previous) grow_discard(f,previous,next,error);
            return false;
        }
    }
    f->options.seats=next;
    if (next<previous) {
        qa_error cleanup={0};
        if (!frontend_seats_destroy_range(f,next,previous,&cleanup)) {
            if (routed) *error=cleanup;
            else cleanup_error(error,&cleanup);
            return false;
        }
    }
    return routed;
}
bool frontend_seats_resize(qa_frontend *f,unsigned next,bool *complete,qa_error *error)
{
    if (!f || !complete || !f->application || !f->input || !f->seats ||
        f->options.dedicated || !next || next>QA_INPUT_LOCAL_SEATS ||
        !f->options.seats || f->options.seats>QA_INPUT_LOCAL_SEATS ||
        f->stepping || f->preparing || f->round || f->shutdown || f->capture ||
        f->resource_inventory || f->source_restoring || frontend_settings_devices_pending(f) ||
        !frontend_seat_callbacks_returned(f))
        return fail(error,"Seat resize requires its returned actual startup and physical owners");
    *complete=false;
    frontend_seats_resize_state *state=frontend_startup_launch_resize_state(f,next);
    if (!state) return fail(error,"Seat resize does not belong to the current startup request");
    if (state->failure.code!=QA_OK) {
        if (error) *error=state->failure;
        return false;
    }
    if (next==f->options.seats && !f->input_settings) { *complete=true; return true; }
    qa_error failure={0};
    if (!state->started) {
        if (f->input_settings || !frontend_owners_idle(f))
            return fail(error,"Seat resize cannot adopt another physical release owner");
        state->previous=f->options.seats; state->started=true;
        if (!release_prepare(f,&failure)) goto failed;
    }
    bool released=false;
    if (!release_advance(f,&released,&failure)) goto failed;
    if (!released) return true;
    if (!routes_resize(f,state->previous,next,&failure)) goto failed;
    *complete=true; return true;
failed:
    if (failure.code==QA_OK) fail(&failure,"Physical seat resize failed before completion");
    state->failure=failure;
    if (error) *error=failure;
    return false;
}
