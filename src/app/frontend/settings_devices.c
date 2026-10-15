#define _POSIX_C_SOURCE 200809L
#include "settings_devices.h"
#include "internal.h"
#include "capture.h"
#include "input_settings.h"
#include "shared_settings.h"
#include "config_store.h"
#include "global_settings_storage.h"
#include "qa/text.h"
#include <stdio.h>

enum { GYRO_SERIAL_BYTES=256, GYRO_PATH_BYTES=640 };
typedef struct frontend_gyro_profile {
    qa_input_seat *input;
    int32_t instance;
    qa_gyro_profile fallback, identity;
    char serial[GYRO_SERIAL_BYTES+1];
    qa_error failure;
    bool saved;
} frontend_gyro_profile;
struct frontend_gyro_profiles {
    frontend_gyro_profile seats[QA_INPUT_LOCAL_SEATS];
    unsigned dirty;
};

static qa_gyro_profile gyro_tuning(const qa_gamepad_tuning *pad)
{
    return (qa_gyro_profile){.enabled=pad->gyro_enabled,.yaw_axis=pad->gyro_yaw_axis,
        .yaw_sensitivity=pad->gyro_yaw_sensitivity,.pitch_sensitivity=pad->gyro_pitch_sensitivity};
}
bool frontend_settings_gyro_prepare(qa_frontend *f,qa_error *error)
{
    if (f->gyro_profiles) return true;
    struct frontend_gyro_profiles *profiles=calloc(1,sizeof(*profiles));
    if (!profiles) return frontend_fail(error,QA_ERROR_MEMORY,"Preparing controller gyro settings");
    for (unsigned slot=0;slot<f->options.seats;++slot) {
        profiles->seats[slot].instance=INT32_MIN;
        profiles->seats[slot].fallback=gyro_tuning(qa_input_seat_gamepad_tuning(f->seats[slot].input));
        profiles->dirty|=1u<<slot;
    }
    f->gyro_profiles=profiles; return true;
}
void frontend_settings_gyro_assignment_changed(void *context,unsigned slot,int32_t previous,int32_t instance)
{
    if (previous==instance) return;
    qa_frontend *f=context;
    if (f->gyro_profiles && slot<QA_INPUT_LOCAL_SEATS) f->gyro_profiles->dirty|=1u<<slot;
}
void frontend_settings_gyro_configuration_published(qa_frontend *f)
{
    if (!f->gyro_profiles) return;
    for (unsigned slot=0;slot<f->options.seats;++slot) {
        frontend_gyro_profile *row=f->gyro_profiles->seats+slot;
        row->fallback=gyro_tuning(qa_input_seat_gamepad_tuning(f->seats[slot].input));
        row->input=NULL;
        f->gyro_profiles->dirty|=1u<<slot;
    }
}
const char *frontend_settings_gyro_message(const qa_frontend *f,unsigned slot)
{
    if (!f->gyro_profiles || slot>=f->options.seats) return "Connect a controller with a gyroscope.";
    if (f->gyro_profiles->dirty&(1u<<slot)) return "Loading settings...";
    const frontend_gyro_profile *row=f->gyro_profiles->seats+slot;
    if (row->failure.code!=QA_OK) return row->failure.message;
    if (row->identity.device)
        return row->saved?"Saved for this controller and seat.":"Settings for this controller and seat.";
    return row->saved?"Saved for this seat.":"Settings for this seat.";
}
static void gyro_path(unsigned slot,const qa_gyro_profile *identity,char path[GYRO_PATH_BYTES])
{
    if (!identity->device) {
        snprintf(path,GYRO_PATH_BYTES,"controllers/seat-%u/seat.json",slot+1); return;
    }
    size_t at=(size_t)snprintf(path,GYRO_PATH_BYTES,"controllers/seat-%u/%s/serial",slot+1,identity->guid);
    static const char hex[]="0123456789abcdef";
    size_t index=0;
    for (const unsigned char *serial=(const unsigned char *)identity->serial;*serial;++serial,++index) {
        if (!(index%32u)) path[at++]='/';
        path[at++]=hex[*serial>>4]; path[at++]=hex[*serial&15u];
    }
    memcpy(path+at,"/gyro.json",sizeof("/gyro.json"));
}
static void gyro_failure(qa_frontend *f,frontend_gyro_profile *row,const qa_error *error)
{
    row->failure=*error;
    char message[sizeof(error->message)+2];
    snprintf(message,sizeof(message),"%s\n",error->message); frontend_print(f,message);
}
static void gyro_profiles_load(qa_frontend *f)
{
    struct frontend_gyro_profiles *profiles=f->gyro_profiles;
    if (!profiles || f->input_settings) return;
    unsigned dirty=profiles->dirty; profiles->dirty=0;
    qa_settings_store store=frontend_global_settings_storage_user_store(f->global_settings_storage);
    for (unsigned slot=0;slot<f->options.seats;++slot) if (dirty&(1u<<slot)) {
        frontend_gyro_profile *row=profiles->seats+slot;
        qa_input_seat *input=f->seats[slot].input;
        int32_t instance=qa_input_platform_controller(f->input,slot);
        if (row->input==input && row->instance==instance) continue;
        row->input=input; row->instance=instance; row->failure=(qa_error){0}; row->saved=false;
        row->identity=(qa_gyro_profile){0};
        qa_controller_info device={0}; bool connected=frontend_settings_controller_read(f,slot,&device);
        if (connected && *device.guid && device.serial && *device.serial && strlen(device.serial)<=GYRO_SERIAL_BYTES) {
            row->identity.device=true;
            for (size_t i=0;i<sizeof(row->identity.guid);++i)
                row->identity.guid[i]=(char)qa_text_ascii_lower((uint8_t)device.guid[i]);
            memcpy(row->serial,device.serial,strlen(device.serial)+1); row->identity.serial=row->serial;
        }
        char path[GYRO_PATH_BYTES]; gyro_path(slot,&row->identity,path);
        qa_gyro_profile saved={0}; bool found=false; qa_error error={0};
        bool device_profile=row->identity.device;
        bool ok=qa_settings_load_gyro(store,path,&saved,&found,&error);
        if (ok && !found && device_profile) {
            qa_gyro_profile seat_identity={0}; gyro_path(slot,&seat_identity,path);
            device_profile=false;
            ok=qa_settings_load_gyro(store,path,&saved,&found,&error);
        }
        if (ok && found && (saved.device!=device_profile ||
            (saved.device && (!qa_text_ascii_equal(saved.guid,row->identity.guid) || strcmp(saved.serial,row->identity.serial))))) {
            qa_error_set(&error,QA_ERROR_FORMAT,0,"Gyro settings belong to another controller"); ok=false;
        }
        qa_gyro_profile tuning=ok && found?saved:row->fallback;
        if (ok && found && !device_profile) row->fallback=tuning;
        qa_gamepad_tuning *pad=qa_input_seat_gamepad_tuning(input);
        pad->gyro_yaw_sensitivity=tuning.yaw_sensitivity; pad->gyro_pitch_sensitivity=tuning.pitch_sensitivity;
        pad->gyro_yaw_axis=tuning.yaw_axis; pad->gyro_enabled=false;
        if (connected && device.sensors[SDL_SENSOR_GYRO] &&
            !qa_input_platform_gyro(f->input,slot,tuning.enabled,&error)) ok=false;
        row->saved=ok && found && (!row->identity.device || device_profile);
        qa_gyro_profile_free(&saved);
        if (!ok) gyro_failure(f,row,&error);
    }
}
static void gyro_profiles_save(qa_frontend *f)
{
    if (!f->gyro_profiles) return;
    qa_settings_store store=frontend_global_settings_storage_user_store(f->global_settings_storage);
    for (unsigned slot=0;slot<f->options.seats;++slot) {
        frontend_gyro_profile *row=f->gyro_profiles->seats+slot;
        qa_controller_info device={0}; bool capable=frontend_settings_controller_read(f,slot,&device) &&
            device.instance==row->instance && device.sensors[SDL_SENSOR_GYRO];
        if (!capable) continue;
        qa_gyro_profile profile=gyro_tuning(qa_input_seat_gamepad_tuning(f->seats[slot].input));
        profile.device=row->identity.device; memcpy(profile.guid,row->identity.guid,sizeof(profile.guid));
        profile.serial=row->identity.serial;
        char path[GYRO_PATH_BYTES]; gyro_path(slot,&profile,path);
        qa_error error={0};
        if (!qa_settings_save_gyro(store,path,&profile,&error)) gyro_failure(f,row,&error);
        else {
            row->failure=(qa_error){0}; row->saved=true;
            if (!profile.device) row->fallback=gyro_tuning(qa_input_seat_gamepad_tuning(f->seats[slot].input));
        }
    }
}

enum device_request_kind { DEVICE_KEYBOARD, DEVICE_CONTROLLER, DEVICE_AUDIO, DEVICE_SAVE };
struct frontend_settings_devices {
    enum device_request_kind kind;
    int slot;
    qa_controller_selection selection;
    char *text;
    bool started;
    qa_audio_device_selection *audio;
    qa_error failure;
};

static bool fail(qa_error *error,const char *message)
{ return frontend_fail(error,QA_ERROR_ARGUMENT,message); }
static struct frontend_settings_devices *request(qa_frontend *f,
    enum device_request_kind kind,int slot,qa_error *error)
{
    if (!f || !f->application || f->options.dedicated || f->shutdown || f->capture ||
        f->resource_inventory || f->source_restoring || f->preparing ||
        f->settings_devices || f->input_settings) {
        fail(error,"Device selection requires the current frontend without a pending replacement");
        return NULL;
    }
    struct frontend_settings_devices *pending=calloc(1,sizeof(*pending));
    if (!pending) {
        frontend_fail(error,QA_ERROR_MEMORY,"Retaining native device selection"); return NULL;
    }
    pending->kind=kind; pending->slot=slot;
    return pending;
}
static bool retain_text(struct frontend_settings_devices *pending,const char *text,qa_error *error)
{
    if (text && !(pending->text=strdup(text))) {
        free(pending); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining device selection text");
    }
    return true;
}
bool frontend_settings_keyboard_read(const qa_frontend *f,int *slot)
{ return f && qa_input_platform_keyboard_read(f->input,slot); }
bool frontend_settings_keyboard_initial(const qa_frontend *f,int *slot,qa_error *error)
{
    if (!f || !slot || !f->options.seats || f->options.seats>QA_INPUT_LOCAL_SEATS)
        return fail(error,"Initial keyboard routing requires its actual local seats");
    qa_settings_store store=frontend_global_settings_storage_user_store(f->global_settings_storage);
    if (!store.vfs || !store.mount)
        return fail(error,"Initial keyboard routing lost its actual global settings directory");
    int saved=0; bool found=false;
    if (!qa_settings_load_routing(store,"input/routing.json",&saved,&found,error)) return false;
    *slot=found && saved<(int)f->options.seats?saved:0;
    return true;
}
bool frontend_settings_keyboard_select(qa_frontend *f,int slot,qa_error *error)
{
    if (!f || !f->input || slot < -1 || slot >= (int)f->options.seats ||
        (slot >= 0 && (!f->seats || !f->seats[slot].input)))
        return fail(error,"Keyboard selection requires an actual local input seat");
    struct frontend_settings_devices *pending=request(f,DEVICE_KEYBOARD,slot,error);
    if (!pending) return false;
    f->settings_devices=pending; return true;
}
bool frontend_settings_controller_select(qa_frontend *f,unsigned slot,
    const qa_controller_selection *selection,qa_error *error)
{
    if (!f || !f->input || !f->seats || slot >= f->options.seats ||
        !f->seats[slot].input || !selection ||
        selection->kind < QA_CONTROLLER_AUTO || selection->kind > QA_CONTROLLER_SERIAL ||
        ((selection->kind==QA_CONTROLLER_GUID || selection->kind==QA_CONTROLLER_SERIAL) &&
            selection->guid[32]) ||
        (selection->kind==QA_CONTROLLER_SERIAL && (!selection->serial || !*selection->serial)))
        return fail(error,"Controller selection requires its actual local seat and typed selector");
    struct frontend_settings_devices *pending=request(f,DEVICE_CONTROLLER,(int)slot,error);
    if (!pending) return false;
    pending->selection=*selection; pending->selection.serial=NULL;
    if (!retain_text(pending,selection->kind==QA_CONTROLLER_SERIAL?selection->serial:NULL,error)) return false;
    pending->selection.serial=pending->text;
    f->settings_devices=pending; return true;
}
bool frontend_settings_controller_read(const qa_frontend *f,unsigned slot,qa_controller_info *device)
{
    int32_t instance=qa_input_platform_controller(f->input,slot);
    for (size_t i=0;i<qa_input_platform_device_count(f->input);++i)
        if (qa_input_platform_device(f->input,i,device) && device->instance==instance) return true;
    return false;
}
bool frontend_settings_audio_select(qa_frontend *f,const char *name,qa_error *error)
{
    if (!f || !f->device) return fail(error,"Audio selection requires the actual native output");
    struct frontend_settings_devices *pending=request(f,DEVICE_AUDIO,-1,error);
    if (!pending || !retain_text(pending,name,error)) return false;
    f->settings_devices=pending; return true;
}
bool frontend_settings_devices_pending(const qa_frontend *f)
{ return f && (f->settings_devices || (f->gyro_profiles && f->gyro_profiles->dirty)); }
bool frontend_settings_devices_save(qa_frontend *f,qa_error *error)
{
    if (f && f->settings_devices) {
        if (f->settings_devices->failure.code!=QA_OK) {
            if (error) *error=f->settings_devices->failure;
            return false;
        }
        return true;
    }
    struct frontend_settings_devices *pending=request(f,DEVICE_SAVE,-1,error);
    if (!pending) return false;
    f->settings_devices=pending; return true;
}

static bool input_prepare(qa_frontend *f,struct frontend_settings_devices *pending,qa_error *error)
{
    qa_input_platform_settings desired;
    if (!frontend_shared_settings_input_project(qa_application_cvars(f->application),NULL,false,
        &desired,error)) return false;
    qa_input_seat *configuration[QA_INPUT_LOCAL_SEATS]={0};
    qa_controller_selection selections[QA_INPUT_LOCAL_SEATS]={0};
    for (unsigned slot=0;slot<f->options.seats;++slot) {
        configuration[slot]=f->seats[slot].input;
        if (!qa_input_platform_selection(f->input,slot,&selections[slot]))
            return fail(error,"Controller selection lost an actual platform route");
    }
    double now=(double)f->wall_time_ns/1000000.0;
    if (pending->kind==DEVICE_KEYBOARD)
        return frontend_input_settings_prepare(f,&desired,configuration,now,&f->input_settings,error) &&
            frontend_input_settings_release_all_prepare(f->input_settings,now,error);
    for (unsigned slot=0;slot<f->options.seats;++slot) if (slot!=(unsigned)pending->slot &&
        (pending->selection.kind==QA_CONTROLLER_GUID || pending->selection.kind==QA_CONTROLLER_SERIAL)) {
        bool steal=qa_controller_selection_equal(&pending->selection,&selections[slot]);
        int32_t instance=qa_input_platform_controller(f->input,slot);
        for (size_t i=0;!steal && i<qa_input_platform_device_count(f->input);++i) {
            qa_controller_info device;
            if (qa_input_platform_device(f->input,i,&device) && device.instance==instance)
                steal=qa_controller_selection_matches_device(&pending->selection,&device);
        }
        if (steal) selections[slot]=(qa_controller_selection){.kind=QA_CONTROLLER_NONE};
    }
    selections[pending->slot]=pending->selection;
    return frontend_input_settings_prepare_selected(f,&desired,configuration,selections,now,
        &f->input_settings,error);
}
static bool input_drain(qa_frontend *f,struct frontend_settings_devices *pending,bool *complete,qa_error *error)
{
    if (!pending->started) {
        if (!frontend_owners_idle(f))
            return fail(error,"Device selection requires returned native and frontend owners");
        pending->started=true;
        if (!input_prepare(f,pending,error) ||
            !frontend_input_settings_reserve_all(f->input_settings,error)) return false;
    }
    if (!frontend_input_settings_advance(f->input_settings,complete,error)) return false;
    if (!*complete) return true;
    *complete=false;
    if (!frontend_input_settings_ready_is(f->input_settings))
        return fail(error,"Device selection lost its completed native input publication");
    frontend_input_settings_publish(f->input_settings);
    const char *diagnostic=frontend_input_settings_diagnostic(f->input_settings);
    if (diagnostic && *diagnostic) frontend_print(f,diagnostic);
    if (!frontend_input_settings_destroy(&f->input_settings,error)) return false;
    if (pending->kind==DEVICE_KEYBOARD && !qa_input_platform_keyboard(f->input,pending->slot,
        (double)f->wall_time_ns/1000000.0,error)) return false;
    *complete=true; return true;
}
static bool audio_drain(qa_frontend *f,struct frontend_settings_devices *pending,qa_error *error)
{
    if (!frontend_owners_idle(f))
        return fail(error,"Audio selection requires returned native and frontend owners");
    qa_audio_device_options options=qa_audio_device_requested_configuration(f->device);
    options.name=pending->text;
    pending->started=true;
    bool ok=qa_audio_device_selection_prepare(f->device,&options,&pending->audio,error) &&
        qa_audio_device_selection_ready(pending->audio,error);
    if (ok && !qa_audio_device_selection_ready_is(pending->audio,f->device))
        ok=fail(error,"Audio selection lost its actual completed native output");
    if (!ok) {
        if (pending->audio && qa_audio_device_selection_abort(pending->audio,NULL)) pending->audio=NULL;
        return false;
    }
    qa_audio_device_selection_publish(pending->audio); pending->audio=NULL;
    return true;
}
bool frontend_settings_devices_drain(qa_frontend *f,bool *complete,qa_error *error)
{
    if (!f || !complete || f->stepping || f->preparing || f->capture || f->source_restoring ||
        f->shutdown || !frontend_seat_callbacks_returned(f))
        return fail(error,"Device selection must advance after actual frontend callbacks return");
    *complete=false;
    gyro_profiles_load(f);
    struct frontend_settings_devices *pending=f->settings_devices;
    if (!pending) { *complete=true; return true; }
    if (pending->failure.code!=QA_OK) {
        if (error) *error=pending->failure;
        return false;
    }
    qa_error failure={0};
    bool ok=true;
    if (pending->kind==DEVICE_SAVE) *complete=frontend_owners_idle(f);
    else if (pending->kind==DEVICE_AUDIO) {
        ok=audio_drain(f,pending,&failure); *complete=ok;
    } else ok=input_drain(f,pending,complete,&failure);
    if (ok && *complete) {
        int keyboard;
        qa_settings_store store=frontend_global_settings_storage_user_store(f->global_settings_storage);
        ok=frontend_settings_keyboard_read(f,&keyboard) &&
            qa_settings_save_routing(store,"input/routing.json",keyboard,&failure) &&
            frontend_config_store_save(f->config_store,&failure);
        if (!ok && failure.code==QA_OK)
            fail(&failure,"Device settings lost their actual keyboard route");
        if (ok && pending->kind==DEVICE_SAVE) gyro_profiles_save(f);
    }
    if (!ok) {
        *complete=false;
        pending->failure=failure;
        if (error) *error=failure;
        return false;
    }
    if (*complete) { free(pending->text); free(pending); f->settings_devices=NULL; }
    return true;
}
bool frontend_settings_devices_destroy(qa_frontend *f,qa_error *error)
{
    if (!f) return true;
    struct frontend_settings_devices *pending=f->settings_devices;
    if (!pending) { free(f->gyro_profiles); f->gyro_profiles=NULL; return true; }
    if (pending->started && (pending->kind==DEVICE_KEYBOARD || pending->kind==DEVICE_CONTROLLER) && f->input_settings)
        return fail(error,"Device selection still retains its actual input-release owner");
    if (pending->audio && !qa_audio_device_selection_abort(pending->audio,error)) return false;
    free(pending->text); free(pending); f->settings_devices=NULL;
    free(f->gyro_profiles); f->gyro_profiles=NULL;
    return true;
}
