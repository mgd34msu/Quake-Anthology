#include "qa/material_source_scratch.h"
#include "internal.h"
#include "campaign_cinematic.h"
#include "system_cinematic.h"
#include "qa/audio_save.h"
#include "campaign.h"
#include "config_store.h"
#include "cinematic_captions.h"
#include "ui_features.h"
#include "shared_render_controls.h"
#include "qa/source_save.h"
#include "qa/q3_cinematic_handles.h"
#include "cinematic_roles.h"
#include "material_movies.h"
#include <ctype.h>

typedef struct cinematic_request {
    qa_command_context command;
    qa_vfs *files;
    char *path, *script;
    uint32_t seat;
    uint64_t travel_revision;
    qa_actor_owner travel_provider;
    bool loop, hold, restore, focus_paused;
    qa_cinematic_checkpoint playback;
} cinematic_request;
struct frontend_cinematic {
    qa_frontend *frontend;
    cinematic_request request,pending_request;
    qa_media_library *library;
    qa_cinematic_asset *asset;
    qa_cinematic *movie;
    double clock_ms;
    bool pending, stopping, busy, focus_paused, complete;
};
struct frontend_cinematic_restore {
    cinematic_request command;
    qa_q3_cinematic_restore_slot slots[16];
    size_t count;
    bool present,travel,reserved,command_restored,published;
};
static char *copy_text(const char *text,qa_error *error)
{
    size_t length=strlen(text); char *copy=length<SIZE_MAX?malloc(length+1):NULL;
    if (!copy) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining cinematic request"); return NULL; }
    memcpy(copy,text,length+1); return copy;
}
static bool suffix(const char *text,const char *extension)
{
    size_t length=strlen(text),size=strlen(extension);
    if (length<size) return false;
    for (size_t i=0;i<size;++i)
        if (tolower((unsigned char)text[length-size+i])!=(unsigned char)extension[i]) return false;
    return true;
}
static bool equal_folded(const char *text,const char *expected)
{ return strlen(text)==strlen(expected) && suffix(text,expected); }
static char *resource_path(const char *name,qa_error *error)
{
    if (!name || !*name || name[0]=='/' || strchr(name,'\\')) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic resource path"); return NULL;
    }
    for (const char *part=name;*part;) {
        const char *end=strchr(part,'/'); size_t size=end?(size_t)(end-part):strlen(part);
        if (!size || (size==1 && part[0]=='.') || (size==2 && part[0]=='.' && part[1]=='.') ||
            (end && !end[1])) { frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic resource component"); return NULL; }
        part=end?end+1:part+size;
    }
    const char *leaf=strrchr(name,'/'); leaf=leaf?leaf+1:name;
    bool extension=strrchr(leaf,'.')!=NULL;
    const char *prefix=!strncmp(name,"video/",6) || !strncmp(name,"pics/",5)?"":suffix(name,".pcx")?"pics/":"video/";
    size_t size=strlen(name),head=strlen(prefix),tail=extension?0:4;
    if (size>SIZE_MAX-head-tail-1) { frontend_fail(error,QA_ERROR_MEMORY,"Cinematic path exceeds address space"); return NULL; }
    char *path=malloc(head+size+tail+1);
    if (!path) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining cinematic resource path"); return NULL; }
    memcpy(path,prefix,head); memcpy(path+head,name,size);
    if (tail) memcpy(path+head+size,".roq",4);
    path[head+size+tail]=0;
    if (!suffix(path,".cin") && !suffix(path,".roq") && !suffix(path,".ogv") && !suffix(path,".pcx")) {
        free(path); frontend_fail(error,QA_ERROR_UNSUPPORTED,"Unsupported cinematic format"); return NULL;
    }
    return path;
}
static void request_free(cinematic_request *request)
{
    qa_vfs_destroy(request->files); free(request->path); free(request->script);
    *request=(cinematic_request){0};
}
void frontend_cinematic_save_discard(qa_frontend *f)
{
    struct frontend_cinematic_restore *saved=f?f->cinematic_restore:NULL;
    if (!saved) return;
    request_free(&saved->command);
    for (size_t i=0;i<saved->count;++i) {
        free((char *)saved->slots[i].instance); free((char *)saved->slots[i].content);
        free((char *)saved->slots[i].parent_instance); free((char *)saved->slots[i].parent_content);
        free((char *)saved->slots[i].path); free((char *)saved->slots[i].system_state.nextmap);
    }
    free(saved); f->cinematic_restore=NULL;
}
static bool current(const frontend_cinematic *owner)
{
    uint32_t ordinal;
    if (!owner || !owner->frontend || !owner->frontend->application) return false;
    if (owner->request.travel_revision) {
        qa_frontend *f=owner->frontend;
        qa_application_travel_view travel;
        /* The returned Source route transfers its presentation to this actual
         * ENGINE child and its cloned media view. GAME can then shut down
         * without lending a retired command context to the decoder. */
        return !f->options.dedicated && owner->request.seat<f->options.seats &&
            f->seats && f->seats[owner->request.seat].ui && owner->request.files &&
            qa_application_travel_read(f->application,&travel) &&
            travel.revision==owner->request.travel_revision && travel.provider==owner->request.travel_provider &&
            (travel.target.kind==QA_TRAVEL_CINEMATIC || travel.target.kind==QA_TRAVEL_PICTURE);
    }
    return
        qa_application_command_context_active(owner->frontend->application,&owner->request.command) &&
        frontend_command_seat_read(owner->frontend,&owner->request.command,&ordinal) && ordinal==owner->request.seat;
}
static void release_playback(frontend_cinematic *owner)
{
    qa_cinematic_destroy(owner->movie); owner->movie=NULL;
    frontend_ui_cinematic_clear(owner->frontend,owner->request.seat);
    qa_cinematic_asset_release(owner->asset); owner->asset=NULL;
    qa_media_library_destroy(owner->library); owner->library=NULL;
    owner->clock_ms=0; owner->focus_paused=false; owner->complete=false;
}
static double sample(void *context)
{ return ((frontend_cinematic *)context)->clock_ms; }
static void completed(void *context,qa_cinematic_target target,qa_cinematic_end reason)
{
    frontend_cinematic *owner=context;
    if (target.kind==QA_CINEMATIC_SEAT && target.id.seat==owner->request.seat && reason!=QA_CINEMATIC_STOPPED)
        owner->complete=true;
}
static void diagnostic(void *context,const char *text)
{ frontend_print(((frontend_cinematic *)context)->frontend,text); }
bool frontend_cinematic_idle(const qa_frontend *f)
{ return f && frontend_ui_features_idle(f) && (!f->cinematic || !f->cinematic->busy); }
bool frontend_cinematic_running(const qa_frontend *f)
{ return frontend_system_cinematic_running(f) ||
    (f && f->cinematic && f->cinematic->movie && current(f->cinematic)); }
bool frontend_cinematic_capture_ready(const qa_frontend *f)
{
    const frontend_cinematic *owner=f?f->cinematic:NULL;
    return f && frontend_cinematic_idle(f) && frontend_system_cinematic_capture_ready(f) &&
        (!f->source_cinematics || qa_q3_cinematic_handles_idle(f->source_cinematics)) && (!owner ||
        (!owner->pending && !owner->stopping && (!owner->movie || current(owner))));
}
bool frontend_cinematic_saved_state(const qa_frontend *f)
{
    if (frontend_cinematic_running(f)) return true;
    if (f && f->cinematic_restore && f->cinematic_restore->count) return true;
    if (!f || !f->source_cinematics) return false;
    for (uint32_t i=0;i<16;++i) {
        qa_q3_cinematic_slot slot;
        if (qa_q3_cinematic_handles_at(f->source_cinematics,i,&slot) && slot.source && slot.path &&
            !(slot.flags&1u)) return true;
    }
    return false;
}
bool frontend_cinematic_destroy(qa_frontend *f,qa_error *error)
{
    if (!f || !f->cinematic) return true;
    frontend_cinematic *owner=f->cinematic;
    if (!frontend_cinematic_idle(f) || (f->audio && !qa_audio_engine_round_ready(f->audio,error)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic cleanup requires completed playback and audio callbacks");
    release_playback(owner); request_free(&owner->request); request_free(&owner->pending_request);
    f->cinematic=NULL; free(owner); return true;
}
static bool queue_request(qa_frontend *f,cinematic_request *request,qa_error *error)
{
    frontend_cinematic *owner=f->cinematic;
    if (!owner) {
        owner=calloc(1,sizeof(*owner));
        if (!owner) { request_free(request); return frontend_fail(error,QA_ERROR_MEMORY,"Allocating fullscreen cinematic owner"); }
        owner->frontend=f; f->cinematic=owner;
    }
    if (owner->pending) request_free(&owner->pending_request);
    owner->pending_request=*request; *request=(cinematic_request){0};
    owner->pending=true; owner->stopping=false;
    return true;
}
bool frontend_cinematic_travel(qa_frontend *f,const qa_application_travel_view *travel,qa_error *error)
{
    qa_application_travel_view actual;
    if (!f || !travel || !travel->revision || f->options.dedicated || f->capture || f->source_restoring ||
        !f->ui_images || !f->display || !frontend_cinematic_idle(f) ||
        (travel->target.kind!=QA_TRAVEL_CINEMATIC && travel->target.kind!=QA_TRAVEL_PICTURE) ||
        !qa_application_travel_read(f->application,&actual) || actual.revision!=travel->revision ||
        actual.provider!=travel->provider || actual.target.kind!=travel->target.kind ||
        strcmp(actual.target.name,travel->target.name))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic travel requires its actual returned media route");
    frontend_cinematic *owner=f->cinematic;
    if (owner && ((owner->pending && owner->pending_request.travel_revision==travel->revision) ||
        (owner->request.travel_revision==travel->revision && current(owner)))) return true;
    qa_application_startup_source authority; bool present=false;
    if (!frontend_config_store_primary_server_read(f->config_store,&authority,&present,error)) return false;
    bool parked=present && frontend_config_store_parked_current(f->config_store,&authority);
    if (!present || authority.scope.provider!=travel->provider ||
        (!parked && authority.scope.kind!=QA_APPLICATION_CONSOLE_Q2_GAME && authority.scope.kind!=QA_APPLICATION_CONSOLE_NATIVE_Q2))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic travel lost its actual Q2 GAME authority");
    cinematic_request request={.command=authority.command,.travel_revision=travel->revision,
        .travel_provider=travel->provider};
    if (!(parked?frontend_config_store_parked_recipient(f->config_store,&authority,&request.seat):
        frontend_command_seat_read(f,&request.command,&request.seat)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic travel has no physical local recipient");
    if (request.command.script) {
        request.script=copy_text(request.command.script,error);
        if (!request.script) return false;
        request.command.script=request.script;
    }
    request.path=resource_path(travel->target.name,error);
    qa_vfs *files=parked?authority.descriptor->content:
        qa_application_context_files(f->application,&request.command,NULL);
    if (!request.path || !files || !(request.files=qa_vfs_clone(files,error))) { request_free(&request); return false; }
    return queue_request(f,&request,error);
}
bool frontend_cinematic_command(qa_frontend *f,const qa_command_invocation *command,qa_error *error)
{
    if (!f || !f->application || !command || !command->argc || !command->argv || f->capture ||
        f->options.dedicated || !f->ui_images || !f->display)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic requires its current local presentation");
    frontend_cinematic *owner=f->cinematic;
    if ((owner && owner->busy) || !frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic callback or retained UI child is active");
    if (!strcmp(command->argv[0],"cinematicpause")) {
        if (!owner || !owner->movie || !current(owner)) return frontend_fail(error,QA_ERROR_ARGUMENT,"No cinematic is playing");
        return qa_cinematic_pause(owner->movie,qa_cinematic_status(owner->movie)!=QA_MEDIA_PAUSED,error);
    }
    if (!strcmp(command->argv[0],"stopcinematic")) {
        if (!owner || (!owner->movie && !owner->pending)) return frontend_fail(error,QA_ERROR_ARGUMENT,"No cinematic is playing");
        owner->stopping=true; return true;
    }
    if (strcmp(command->argv[0],"cinematic") || command->argc<2)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Usage: cinematic <name> [loop|hold]");
    cinematic_request request={0};
    if (!qa_application_capture_command_context(f->application,&command->context,&request.command,error)) return false;
    if (!frontend_command_seat_read(f,&request.command,&request.seat))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic request has no admitted physical local recipient");
    request.path=resource_path(command->argv[1],error);
    if (!request.path) return false;
    if (command->context.script) {
        request.script=copy_text(command->context.script,error);
        if (!request.script) { request_free(&request); return false; }
        request.command.script=request.script;
    }
    qa_vfs *files=qa_application_context_files(f->application,&request.command,NULL);
    if (!files || !(request.files=qa_vfs_clone(files,error))) { request_free(&request); return false; }
    const char *mode=command->argc>2?command->argv[2]:"";
    request.loop=mode[0]=='2' || !strcmp(mode,"loop");
    const char *leaf=strrchr(request.path,'/'); leaf=leaf?leaf+1:request.path;
    request.hold=mode[0]=='1' || !strcmp(mode,"hold") || equal_folded(leaf,"end.roq") || equal_folded(leaf,"demoend.roq");
    return queue_request(f,&request,error);
}
static bool prepare(frontend_cinematic *owner,qa_error *error)
{
    qa_frontend *f=owner->frontend;
    if (!current(owner)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic preparation belongs to a retired request");
    owner->library=qa_media_library_create(f->ui_images,error);
    if (!owner->library) return false;
    qa_error opened={0};
    bool ok=qa_media_library_load(owner->library,owner->request.files,owner->request.path,&owner->asset,&opened);
    if (!ok && opened.code==QA_ERROR_NOT_FOUND && suffix(owner->request.path,".cin")) {
        size_t size=strlen(owner->request.path);
        memcpy(owner->request.path+size-4,".ogv",4);
        ok=qa_media_library_load(owner->library,owner->request.files,owner->request.path,&owner->asset,&opened);
    }
    if (!ok) { if (error) *error=opened; return false; }
    qa_audio_engine *staging=NULL;
    if (f->audio) {
        qa_audio_engine_options options; frontend_audio_engine_options(f,&options);
        options.sample_rate=qa_audio_engine_rate(f->audio); options.observer=NULL; options.observer_user=NULL;
        if (!qa_audio_engine_create(&options,&staging,error)) return false;
    }
    qa_cinematic_options options={.clock={.context=owner,.sample=sample},
        .target={.kind=QA_CINEMATIC_SEAT,.id.seat=owner->request.seat},
        .loop=owner->request.loop,.hold=owner->request.hold,.silent=!f->audio,
        .audio=staging,.audio_bus=QA_FRONTEND_COMMAND_OWNER,.gain=1,
        .audio_audience={.kind=QA_CINEMATIC_AUDIO_WORLD},.context=owner,
        .complete=completed,.diagnostic=diagnostic};
    qa_cinematic_source source=qa_cinematic_asset_source(owner->asset);
    ok=owner->request.restore ? qa_cinematic_restore(&source,&options,
        &owner->request.playback,&owner->movie,error) :
        qa_cinematic_create(&source,&options,&owner->movie,error);
    if (ok && owner->request.restore) owner->focus_paused=owner->request.focus_paused;
    if (ok) ok=frontend_ui_cinematic_prepare(f,owner->request.files,owner->request.path,owner->request.seat,error);
    qa_audio_stream_cut *cut=NULL;
    if (ok) ok=qa_cinematic_audio_rebind_ready(owner->movie,staging,QA_FRONTEND_COMMAND_OWNER,error);
    if (ok) ok=current(owner) && (!f->audio || qa_audio_engine_round_ready(f->audio,error)) &&
        (!f->device || qa_audio_device_round_ready(f->device,error));
    qa_media_status status=owner->movie?qa_cinematic_status(owner->movie):QA_MEDIA_STOPPED;
    if (ok && f->audio && status!=QA_MEDIA_ENDED && status!=QA_MEDIA_STOPPED) {
        ok=qa_audio_engine_stream_cut_prepare(f->audio,staging,QA_FRONTEND_COMMAND_OWNER,QA_AUDIO_WORLD,1,&cut,error);
        if (ok && (!current(owner) || !qa_audio_engine_stream_cut_current(cut)))
            ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic activation lost its actual prepared owners");
        if (ok) {
            qa_audio_engine_stream_cut_publish(cut);
            qa_cinematic_audio_rebind(owner->movie,f->audio,QA_FRONTEND_COMMAND_OWNER);
            qa_audio_device_clear(f->device);
            frontend_source_audio_stopped(f); frontend_campaign_audio_stopped(f);
        }
    }
    if (ok && !cut) qa_cinematic_audio_rebind(owner->movie,f->audio,QA_FRONTEND_COMMAND_OWNER);
    if (!ok) {
        qa_cinematic_destroy(owner->movie); owner->movie=NULL;
    }
    qa_audio_engine_stream_cut_destroy(cut); qa_audio_engine_destroy(staging);
    return ok;
}
static bool saved_command(qa_source_save_io *io,cinematic_request *request,bool *present,bool *travel)
{
    return qa_source_save_bool(io,present) && (!*present ||
        (qa_source_save_owned_text(io,&request->path) &&
        qa_source_save_f64(io,&request->playback.source_ms) && qa_source_save_u64(io,&request->playback.loop) &&
        qa_source_save_bool(io,&request->playback.paused) && qa_source_save_u32(io,&request->seat) &&
        qa_source_save_bool(io,&request->loop) && qa_source_save_bool(io,&request->hold) &&
        qa_source_save_bool(io,&request->focus_paused) && qa_source_save_bool(io,travel)));
}
static bool saved_text(qa_source_save_io *io,const char **value)
{
    char *text=(char *)*value;
    if (!qa_source_save_owned_text(io,&text)) return false;
    *value=text; return true;
}
static bool saved_slot(qa_source_save_io *io,qa_q3_cinematic_restore_slot *slot)
{
    return qa_source_save_i32(io,&slot->handle) && saved_text(io,&slot->instance) &&
        saved_text(io,&slot->content) && saved_text(io,&slot->parent_instance) &&
        saved_text(io,&slot->parent_content) && qa_source_save_u32(io,&slot->parent_seat) &&
        qa_source_save_u32(io,&slot->source_group) && qa_source_save_bool(io,&slot->detached) &&
        qa_source_save_i32(io,&slot->role) && qa_source_save_u32(io,&slot->seat) && saved_text(io,&slot->path) &&
        qa_source_save_f32(io,&slot->rect.x) && qa_source_save_f32(io,&slot->rect.y) &&
        qa_source_save_f32(io,&slot->rect.width) && qa_source_save_f32(io,&slot->rect.height) &&
        qa_source_save_u32(io,&slot->flags) && qa_source_save_bool(io,&slot->has_playback) &&
        (!slot->has_playback || (qa_source_save_f64(io,&slot->playback.source_ms) &&
            qa_source_save_u64(io,&slot->playback.loop) && qa_source_save_bool(io,&slot->playback.paused))) &&
        qa_source_save_bool(io,&slot->occupied) && qa_source_save_bool(io,&slot->selected) &&
        qa_source_save_bool(io,&slot->decoder) && qa_source_save_i32(io,&slot->status) &&
        qa_source_save_i32(io,&slot->play_on_walls);
}
static bool saved_system_state(qa_source_save_io *io,qa_q3_system_movie_state *state)
{
    int32_t phase=(int32_t)state->phase,reason=(int32_t)state->ending_reason;
    bool ok=qa_source_save_i32(io,&phase) && qa_source_save_f64(io,&state->screen_ms) &&
        qa_source_save_i32(io,&reason) && qa_source_save_bool(io,&state->screen) &&
        qa_source_save_bool(io,&state->focus_paused) && qa_source_save_bool(io,&state->ending) &&
        qa_source_save_bool(io,&state->appended) && saved_text(io,&state->nextmap);
    if (ok && io->direction==QA_SOURCE_SAVE_READ) {
        state->phase=(qa_q3_system_movie_phase)phase; state->ending_reason=(qa_cinematic_end)reason;
    }
    return ok;
}
static bool saved_system_slots(qa_source_save_io *io,qa_q3_cinematic_restore_slot *slots,size_t count)
{
    size_t expected=0;
    for (size_t i=0;i<count;++i) if (slots[i].flags&1u) ++expected;
    bool writing=io->direction==QA_SOURCE_SAVE_WRITE;
    if ((writing && !expected) || (!writing && io->offset==io->input.size && !expected)) return true;
    size_t systems=expected;
    if (!qa_source_save_count(io,&systems,16) || systems!=expected)
        return frontend_fail(io->error,QA_ERROR_FORMAT,"Saved System movie count differs from its slots");
    bool seen[16]={0}; size_t next=0;
    for (size_t i=0;i<systems;++i) {
        if (writing) while (next<count && !(slots[next].flags&1u)) ++next;
        int32_t handle=writing?slots[next].handle:0;
        if (!qa_source_save_i32(io,&handle)) return false;
        size_t index=next;
        if (!writing) {
            index=0;
            while (index<count && (slots[index].handle!=handle || !(slots[index].flags&1u))) ++index;
        }
        if (index==count || seen[index])
            return frontend_fail(io->error,QA_ERROR_FORMAT,"Saved System movie has no unique numeric slot");
        seen[index]=true;
        if (!saved_system_state(io,&slots[index].system_state)) return false;
        next=index+1;
    }
    return true;
}
bool frontend_cinematic_save_capture(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    if (!out || !frontend_cinematic_capture_ready(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic save requires returned playback callbacks");
    frontend_cinematic *owner=f->cinematic;
    bool present=owner && owner->movie,travel=owner && owner->request.travel_revision!=0;
    cinematic_request request=owner?owner->request:(cinematic_request){0};
    if (present && !qa_cinematic_checkpoint_read(owner->movie,&request.playback,error)) return false;
    request.focus_paused=owner && owner->focus_paused;
    qa_source_save_io io;
    if (!qa_source_save_writer(&io,NULL,error)) return false;
    bool ok=saved_command(&io,&request,&present,&travel);
    qa_q3_cinematic_restore_slot slots[16]; size_t count=0;
    const qa_q3_cinematic_source *sources[16]={0};
    uint32_t groups[16]={0};
    for (uint32_t i=0;ok && i<16;++i) {
        qa_q3_cinematic_slot slot={0};
        if (f->source_cinematics) ok=qa_q3_cinematic_handles_at(f->source_cinematics,i,&slot);
        if (!ok) break;
        if (!slot.source || !slot.path || slot.restore_pending) {
            const struct frontend_cinematic_restore *pending=f->cinematic_restore;
            if (f->source_cinematics && pending && pending->reserved && !slot.restore_pending) continue;
            for (size_t j=0;pending && j<pending->count;++j) if (pending->slots[j].handle==(int32_t)i) {
                slots[count]=pending->slots[j]; groups[count]=slots[count].source_group;
                sources[count]=slot.source;
                if (f->source_cinematics) {
                    slots[count].selected=slot.selected; slots[count].decoder=slot.decoder;
                }
                ++count; break;
            }
            continue;
        }
        qa_q3_cinematic_restore_slot *saved=slots+count++;
        sources[count-1]=slot.source; groups[count-1]=slot.source_group;
        *saved=(qa_q3_cinematic_restore_slot){.handle=slot.handle,.instance=slot.instance,.content=slot.content,
            .parent_instance=slot.parent_instance,.parent_content=slot.parent_content,
            .parent_seat=slot.parent_seat,.source_group=slot.source_group,.detached=slot.detached,
            .role=slot.role,.seat=slot.seat,.path=slot.path,.rect=slot.rect,.flags=slot.flags,
            .has_playback=slot.playback!=NULL,.occupied=slot.occupied,.selected=slot.selected,
            .decoder=slot.decoder,.status=slot.status,.play_on_walls=slot.play_on_walls};
        if (slot.flags&1u) ok=slot.system && slot.system->state &&
            slot.system->state(slot.system->context,&saved->system_state,error);
        if (ok && saved->has_playback) ok=qa_cinematic_checkpoint_read((qa_cinematic *)slot.playback,&saved->playback,error);
    }
    if (ok && count) {
        /* One per-save association also covers owners not yet lazily created.
         * Runtime pointers qualify groups here but are never written. */
        for (size_t i=0;i<count;++i) {
            slots[i].source_group=(uint32_t)i;
            for (size_t j=0;j<i;++j) if ((sources[i] && sources[i]==sources[j]) ||
                (!sources[i] && !sources[j] && groups[i]==groups[j])) {
                slots[i].source_group=slots[j].source_group; break;
            }
        }
        ok=qa_source_save_count(&io,&count,16);
        for (size_t i=0;ok && i<count;++i) ok=saved_slot(&io,slots+i);
        if (ok) ok=saved_system_slots(&io,slots,count);
    }
    ok=ok && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_cinematic_save_prepare(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    struct frontend_cinematic_restore *saved=calloc(1,sizeof(*saved));
    if (!saved) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining saved cinematic state");
    saved->command.restore=true;
    qa_source_save_io io;
    if (!qa_source_save_reader(&io,NULL,bytes,error)) { free(saved); return false; }
    bool ok=saved_command(&io,&saved->command,&saved->present,&saved->travel);
    /* The original command-only record ends here. The optional continuation
     * names actual slots and resources; decoder and audio data are rebuilt. */
    if (ok && io.offset<io.input.size) {
        ok=qa_source_save_count(&io,&saved->count,16);
        for (size_t i=0;ok && i<saved->count;++i) ok=saved_slot(&io,saved->slots+i);
        if (ok) ok=saved_system_slots(&io,saved->slots,saved->count);
    }
    ok=ok && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    for (size_t i=0;i<saved->count;++i) saved->slots[i].playback.resource=saved->slots[i].path;
    if (!ok) {
        struct frontend_cinematic_restore *old=f->cinematic_restore;
        f->cinematic_restore=saved; frontend_cinematic_save_discard(f); f->cinematic_restore=old;
        if (!error || error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved cinematic state");
        return false;
    }
    frontend_cinematic_save_discard(f); f->cinematic_restore=saved;
    return true;
}
bool frontend_cinematic_save_reserve(qa_frontend *f,qa_error *error)
{
    struct frontend_cinematic_restore *saved=f?f->cinematic_restore:NULL;
    if (!saved || saved->reserved || !saved->count || !f->source_cinematics) return true;
    if (!qa_q3_cinematic_handles_restore_reserve(f->source_cinematics,saved->slots,saved->count,error)) return false;
    saved->reserved=true;
    if (saved->published) qa_q3_cinematic_handles_restore_published(f->source_cinematics);
    return true;
}
void frontend_cinematic_save_published(qa_frontend *f)
{
    if (!f || !f->cinematic_restore) return;
    f->cinematic_restore->published=true;
    if (f->cinematic_restore->reserved) qa_q3_cinematic_handles_restore_published(f->source_cinematics);
}
static bool saved_name_equal(const char *a,const char *b)
{ return a==b || (a && b && !strcmp(a,b)); }
bool frontend_cinematic_save_parent_bind(qa_frontend *f,frontend_material_movies *parent,
    const char *instance,const char *content,uint32_t seat,qa_error *error)
{
    struct frontend_cinematic_restore *saved=f?f->cinematic_restore:NULL;
    if (!saved || !saved->reserved) return true;
    for (size_t i=0;i<saved->count;++i) {
        const qa_q3_cinematic_restore_slot *row=saved->slots+i;
        if (!row->detached || row->parent_seat!=seat ||
            !saved_name_equal(row->parent_instance,instance) || !saved_name_equal(row->parent_content,content)) continue;
        qa_q3_cinematic_slot slot;
        if (!qa_q3_cinematic_handles_at(f->source_cinematics,(uint32_t)row->handle,&slot)) return false;
        if (slot.source) continue;
        frontend_material_movie_source source;
        if (!frontend_material_movies_source_read(parent,&source,error)) return false;
        size_t index=frontend_cinematic_roles_count(f);
        if (!frontend_cinematic_roles_restore_add(f,&source,seat,index,error) ||
            !frontend_cinematic_roles_restore_bind(f,index,parent,row->instance,row->content,row->role,
                row->seat,row->source_group,error)) return false;
    }
    return true;
}
static bool restore_command(qa_frontend *f,cinematic_request *request,bool travel,qa_error *error)
{
    if (!request->path || request->seat>=f->options.seats ||
        !isfinite(request->playback.source_ms) || request->playback.source_ms<0)
        return frontend_fail(error,QA_ERROR_FORMAT,"Invalid saved cinematic resource or playback position");
    char *path=resource_path(request->path,error);
    if (!path) return false;
    free(request->path); request->path=path;
    qa_vfs *files=NULL; bool ok;
    if (travel) {
        qa_application_travel_view route;
        qa_application_startup_source authority; bool found=false;
        ok=qa_application_travel_read(f->application,&route) &&
            frontend_config_store_primary_server_read(f->config_store,&authority,&found,error) && found;
        if (ok) {
            request->travel_revision=route.revision; request->travel_provider=route.provider;
            request->command=authority.command; request->command.script=NULL;
            files=frontend_config_store_parked_current(f->config_store,&authority) ?
                authority.descriptor->content : qa_application_context_files(f->application,&request->command,NULL);
        }
    } else {
        ok=qa_console_context_read(qa_application_console(f->application),&request->command,error);
        if (ok) {
            request->command.seat=request->seat; request->command.origin=QA_COMMAND_SEAT;
            (void)frontend_seat_launch_id_read(f,request->seat,&request->command.seat);
            request->command.script=NULL;
            ok=qa_application_capture_command_context(f->application,&request->command,&request->command,error);
            if (ok) files=qa_application_context_files(f->application,&request->command,NULL);
        }
    }
    if (ok && files) request->files=qa_vfs_clone(files,error);
    if (!ok || !request->files) return frontend_fail(error,QA_ERROR_FORMAT,"Saved cinematic content route is unavailable");
    request->playback.resource=request->path;
    return queue_request(f,request,error);
}
static bool restore_drain(qa_frontend *f,qa_error *error)
{
    struct frontend_cinematic_restore *saved=f?f->cinematic_restore:NULL;
    if (!saved || !saved->command_restored) return true;
    if (!saved->count) { frontend_cinematic_save_discard(f); return true; }
    if (!f->source_cinematics || !qa_q3_cinematic_handles_idle(f->source_cinematics)) return true;
    if (!frontend_cinematic_save_reserve(f,error)) return false;
    for (size_t i=0;i<saved->count;++i) {
        qa_q3_cinematic_slot slot;
        if (!qa_q3_cinematic_handles_at(f->source_cinematics,(uint32_t)saved->slots[i].handle,&slot)) return false;
        if (!slot.source && slot.restore_pending) return true;
    }
    bool complete=false;
    if (!qa_q3_cinematic_handles_restore_finish(f->source_cinematics,&complete,error)) return false;
    if (complete) frontend_cinematic_save_discard(f);
    return true;
}
bool frontend_cinematic_save_restore(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || f->options.dedicated || !frontend_cinematic_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic restore requires its local rebuilt frontend");
    if (!f->cinematic_restore && !frontend_cinematic_save_prepare(f,bytes,error)) return false;
    struct frontend_cinematic_restore *saved=f->cinematic_restore;
    if (!saved->command_restored) {
        if (saved->present && !restore_command(f,&saved->command,saved->travel,error)) return false;
        saved->command_restored=true;
    }
    return frontend_cinematic_drain(f,error);
}
static bool finish(frontend_cinematic *owner,qa_error *error)
{
    if (!owner->complete || !current(owner)) return true;
    if (owner->request.travel_revision) {
        if (!qa_application_complete_travel(owner->frontend->application,owner->request.travel_revision,error)) return false;
        owner->complete=false; return true;
    }
    owner->complete=false;
    qa_frontend *f=owner->frontend;
    if (frontend_network_remote(f)) return true;
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
    const qa_launch_binding *binding=choices?qa_launch_binding_for(choices,
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,""):NULL;
    const qa_launch_instance *instance=binding?qa_launch_snapshot_find(publication,binding->instance):NULL;
    const qa_product *product=instance?qa_catalog_product(qa_launch_snapshot_catalog(publication),instance->selection.product):NULL;
    if (!product || product->family!=QA_GAME_Q3) return true;
    qa_application_q3_campaign source;
    if (!qa_application_q3_campaign_read(f->application,0,&source,error)) return false;
    qa_application_startup_source authority; bool present=false;
    if (!frontend_config_store_primary_server_read(f->config_store,&authority,&present,error)) return false;
    if (!present || authority.scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME ||
        authority.scope.provider!=source.source_owner || authority.console!=source.console ||
        authority.cvars!=source.cvars)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic completion lost its actual GAME command authority");
    qa_command_context command=authority.command;
    command.script="cinematic";
    const qa_cvar_view *next=qa_cvars_find(source.cvars,"nextmap");
    if (!next || !next->value || !*next->value) return true;
    size_t size=strlen(next->value); char *text=size<=SIZE_MAX-2?malloc(size+2):NULL;
    if (!text) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining cinematic nextmap command");
    memcpy(text,next->value,size); text[size]='\n'; text[size+1]=0;
    bool ok=qa_application_q3_campaign_current(f->application,&source) &&
        qa_console_append(source.console,&command,text,error);
    free(text);
    return ok && qa_cvars_set(source.cvars,"nextmap","",true,error);
}
bool frontend_cinematic_drain(qa_frontend *f,qa_error *error)
{
    if (f && !frontend_system_cinematic_drain(f,error)) return false;
    if (!f || !f->cinematic) return restore_drain(f,error);
    frontend_cinematic *owner=f->cinematic;
    if (owner->busy || f->capture || f->preparing || f->round || !frontend_ui_features_idle(f) ||
        (f->audio && !qa_audio_engine_round_ready(f->audio,error)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic drain requires completed callbacks");
    if (owner->pending && !owner->stopping && !frontend_system_cinematic_stop_all(f,error)) return false;
    owner->busy=true; bool ok=true;
    if (owner->stopping) {
        release_playback(owner); owner->pending=false;
    } else if (owner->pending) {
        release_playback(owner); request_free(&owner->request);
        owner->request=owner->pending_request; owner->pending_request=(cinematic_request){0};
        owner->pending=false; ok=prepare(owner,error);
        if (!ok) release_playback(owner);
    } else if (!current(owner)) {
        release_playback(owner);
    } else if (owner->complete) { ok=finish(owner,error); if (ok) release_playback(owner); }
    owner->busy=false;
    if (ok && !owner->movie && !owner->pending) ok=frontend_cinematic_destroy(f,error);
    return ok && restore_drain(f,error);
}
bool frontend_cinematic_input(qa_frontend *f,uint32_t seat,qa_input_focus focus,
    const qa_input_event *event,bool *handled,qa_error *error)
{
    if (!f || !event || !handled) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic input event");
    if (!frontend_system_cinematic_input(f,seat,focus,event,handled,error)) return false;
    if (*handled) return true;
    *handled=false; frontend_cinematic *owner=f->cinematic;
    if (!owner || !owner->movie || owner->request.seat!=seat || !current(owner) || focus!=QA_INPUT_GAME) return true;
    if (!frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic input retains its actual UI child");
    *handled=true;
    bool press=event->down && ((event->kind==QA_INPUT_EVENT_KEY && !event->repeat) || event->kind==QA_INPUT_EVENT_BUTTON);
    if (!press) return true;
    double elapsed,source; uint64_t loop;
    if (!qa_cinematic_time(owner->movie,&elapsed,&source,&loop,error)) return false;
    if (elapsed>1000 || (qa_cinematic_status(owner->movie)==QA_MEDIA_HELD && owner->clock_ms>1000))
        return qa_cinematic_end_playback(owner->movie,QA_CINEMATIC_SKIPPED,error);
    return true;
}
bool frontend_cinematic_view_current(const qa_frontend *f,const qa_vfs *files,const char *path,uint32_t seat)
{
    const frontend_cinematic *owner=f?f->cinematic:NULL;
    return owner && owner->frontend==f && owner->movie && files && path &&
        owner->request.files==files && owner->request.seat==seat && owner->request.path &&
        !strcmp(owner->request.path,path) && current(owner);
}
bool frontend_cinematic_view_read(qa_frontend *f,frontend_cinematic_view *out,bool *found,qa_error *error)
{
    if (!f || !out || !found) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic view request");
    *found=false; frontend_cinematic *owner=f->cinematic;
    if (!owner || !owner->movie || !current(owner)) return true;
    frontend_cinematic_view view={.files=owner->request.files,.path=owner->request.path,
        .seat=owner->request.seat,.viewport={0,0,f->width,f->height},.status=qa_cinematic_status(owner->movie)};
    if (!qa_cinematic_time(owner->movie,&view.elapsed_ms,&view.source_ms,&view.loop,error)) return false;
    *out=view; *found=true; return true;
}
bool frontend_cinematic_focus(qa_frontend *f,qa_cinematic *movie,bool *paused,bool *obscured,qa_error *error)
{
    *obscured=false;
    for (uint32_t i=0;i<f->options.seats;++i) {
        qa_ui_state ui;
        if (!qa_ui_state_read(f->seats[i].ui,&ui,error)) return false;
        *obscured|=qa_input_seat_focus(f->seats[i].input)==QA_INPUT_CONSOLE || ui.depth!=0;
    }
    if (*obscured && qa_cinematic_status(movie)==QA_MEDIA_PLAYING) {
        if (!qa_cinematic_pause(movie,true,error)) return false;
        *paused=true;
    } else if (!*obscured && *paused) {
        if (!qa_cinematic_pause(movie,false,error)) return false;
        *paused=false;
    }
    return true;
}
bool frontend_cinematic_frame(qa_frontend *f,uint64_t elapsed_ns,bool *rendered,qa_error *error)
{
    if (!f || !rendered) return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid cinematic frame");
    if (frontend_system_cinematic_running(f))
        return frontend_system_cinematic_frame(f,elapsed_ns,rendered,error);
    *rendered=false; frontend_cinematic *owner=f->cinematic;
    if (!owner || !owner->movie || !current(owner)) return true;
    if (!frontend_ui_features_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic frame retains its actual UI child");
    double duration=(double)elapsed_ns/1000000.0;
    if (!isfinite(owner->clock_ms+duration)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Cinematic clock overflow");
    owner->clock_ms+=duration;
    bool obscured;
    if (!frontend_cinematic_focus(f,owner->movie,&owner->focus_paused,&obscured,error)) return false;
    if (obscured) return true;
    bool ready;
    if (!frontend_display_ready(f,&ready,error)) return false;
    if (!ready) return true;
    qa_scene_frame_reset(&f->frame,f->frame_number);
    bool blank;
    if (!qa_scene_frame_material_order(&f->frame,f->order,error) ||
        !qa_cinematic_fullscreen(owner->movie,QA_CINEMATIC_GAME,(qa_scene_rect){0,0,f->width,f->height},
            f->ui_images,&f->frame,&blank,error)) return false;
    if (!blank) {
        frontend_cinematic_view view; bool found=false;
        if (!frontend_cinematic_view_read(f,&view,&found,error) || !found ||
            !frontend_ui_cinematic_draw(f,view.files,view.path,view.seat,view.elapsed_ms,view.source_ms,
                view.loop,view.status,view.viewport,&f->frame,error)) return false;
    }
    if (!frontend_render_controls_live(f,error)) return false;
    if (f->frame.source_backend && f->frame.source_skip_backend) {
        if (f->frame.source_pending &&
            !qa_material_source_frame_end(f->frame.source_pending,&f->frame,false,error)) return false;
        *rendered=true; return true;
    }
    bool ok=frontend_frame_present(f,error);
    if (ok) *rendered=true;
    return ok;
}
