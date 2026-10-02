#include "internal.h"
#include "unified_q3_runtime_private.h"
#include "remote_unified_save.h"
#include "../../presentation/q3/internal.h"
#include "qa/q3_presentation_save.h"

static bool fail(qa_error *e,const char *message)
{ qa_error_set(e,QA_ERROR_FORMAT,0,"%s",message); return false; }
static bool ready(const frontend_unified_q3_runtime *o,bool reading,qa_error *e)
{
    q3n_compiled_source_view source; qa_q3_presentation_binding backend;
    const qa_q3_presentation_assets *assets=o?o->options.presentation.assets:NULL;
    if(!o || !o->complete || o->video || o->prepared ||
       (o->restoring!=reading && !(o->restoring && !reading && o->passive &&
           (frontend_remote_unified_retired(o->options.replica) ||
            frontend_unified_q3_client_retirement_departed(o->options.client)))) ||
       !frontend_unified_q3_runtime_idle(o) || !assets || !assets->capturing || assets->busy!=1 || assets->codec_busy ||
       !frontend_unified_q3_client_checkpoint_current(o->options.client))
        return fail(e,"Unified CG codec requires its actual imported/captured Source and asset graph");
    if(!q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,e) ||
       !qa_q3_presentation_binding_read(o->children.presentation,&backend,e))return false;
    return source.basis.assets==assets &&
        backend.options.assets==assets && backend.options.owner==o->options.presentation.owner &&
        backend.options.context==o->options.presentation.context && backend.options.seat==source.basis.physical_seat &&
        q3n_compiled_source_checkpoint_current(&source) ? true :
        fail(e,"Unified CG codec requires its actual imported/captured Source and asset graph");
}
static bool refdef_fields(qa_source_save_io *io,qa_q3_refdef *r)
{
    bool okay=qa_source_save_i32(io,&r->x) && qa_source_save_i32(io,&r->y) && qa_source_save_i32(io,&r->width) &&
        qa_source_save_i32(io,&r->height) && qa_source_save_i32(io,&r->time) && qa_source_save_i32(io,&r->flags) &&
        qa_source_save_f32(io,&r->fov_x) && qa_source_save_f32(io,&r->fov_y) && qa_source_save_vec3(io,&r->origin);
    for(unsigned i=0;okay && i<3;++i)okay=qa_source_save_vec3(io,r->axis+i);
    return okay && qa_source_save_bytes(io,r->area_mask,sizeof(r->area_mask)) &&
        qa_source_save_bytes(io,r->text,sizeof(r->text)) && qa_vec_finite(r->origin) &&
        qa_vec_finite(r->axis[0]) && qa_vec_finite(r->axis[1]) && qa_vec_finite(r->axis[2]);
}
static uint32_t child_presence(const frontend_unified_q3_runtime *o)
{
    const frontend_unified_q3_runtime_owners *c=&o->children;
    return (c->presentation?1u|2048u:0u)|(c->weapons?2u:0u)|(c->events?4u:0u)|(c->particles?8u:0u)|
        (c->view?16u:0u)|(c->player_state?32u:0u)|(c->hud?64u:0u)|(c->commands?128u:0u)|
        (c->loading?256u:0u)|(c->mission?512u:0u)|(c->snapshots?1024u:0u);
}
static bool fields(frontend_unified_q3_runtime *o,qa_source_save_io *io,uint32_t *presence)
{
    uint8_t magic[4]={'U','Q','3','R'}; uint32_t version=3;bool scene_only=o->options.scene_only;
    q3n_compiled_source_view source;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"UQ3R",4) &&
        qa_source_save_u32(io,&version) && version==3 &&
        qa_source_save_bool(io,&scene_only) && scene_only==o->options.scene_only &&
        q3n_compiled_source_fields(io,frontend_unified_q3_client_source(o->options.client)) &&
        q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,io->error) &&
        qa_source_save_bool(io,&o->retiring) && qa_source_save_u32(io,presence) &&
        (*presence==(source.basis.product==QA_Q3_TEAM_ARENA?4095u:3583u) ||
            (o->retiring && *presence==2049u)) &&
        qa_source_save_bool(io,&o->initialized) && o->initialized==source.basis.initialized &&
        qa_source_save_bool(io,&o->faulted) && qa_source_save_u64(io,&o->video_generation) &&
        qa_source_save_i32(io,&o->old_time) && qa_source_save_i32(io,&o->frame_milliseconds) && o->frame_milliseconds>=0 &&
        qa_source_save_i32(io,&o->presentation_time) &&
        qa_source_save_i32(io,&o->client_frame) && qa_source_save_u32(io,&o->stereo) && o->stereo<=2 &&
        refdef_fields(io,&o->refdef) && qa_source_save_vec3(io,&o->view_angles) && qa_vec_finite(o->view_angles) &&
        q3n_compiled_source_checkpoint_current(&source);
}
static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?bytes->size:0;
    if(!qa_source_save_count(io,&count,64u*1024u*1024u))return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE)return qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset)return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool child_write(const frontend_unified_q3_runtime *o,unsigned index,qa_buffer *out,qa_error *e)
{
    const frontend_unified_q3_runtime_owners *c=&o->children;
    switch(index) {
    case 0:return qa_q3_presentation_scene_checkpoint(c->presentation,out,e);
    case 1:return q3n_weapons_checkpoint(c->weapons,out,e);
    case 2:return q3n_events_checkpoint(c->events,out,e);
    case 3:return q3n_particles_checkpoint(c->particles,out,e);
    case 4:return q3n_view_checkpoint(c->view,out,e);
    case 5:return q3n_player_state_checkpoint(c->player_state,out,e);
    case 6:return q3n_hud_checkpoint(c->hud,out,e);
    case 7:return q3n_server_commands_checkpoint(c->commands,out,e);
    case 8:return q3n_loading_checkpoint(c->loading,out,e);
    case 9:return !c->mission || q3n_mission_hud_checkpoint(c->mission,out,e);
    case 10:return frontend_unified_q3_snapshots_checkpoint(c->snapshots,out,e);
    case 11:return o->options.backend_checkpoint(o->options.context,c->presentation,out,e);
    default:return false;
    }
}
static bool child_read(frontend_unified_q3_runtime *o,unsigned index,qa_bytes bytes,qa_error *e)
{
    frontend_unified_q3_runtime_owners *c=&o->children;
    switch(index) {
    case 0:return qa_q3_presentation_scene_restore(c->presentation,bytes,e);
    case 1:return q3n_weapons_restore(c->weapons,bytes,e);
    case 2:return q3n_events_restore(c->events,bytes,e);
    case 3:return q3n_particles_restore(c->particles,bytes,e);
    case 4:return q3n_view_restore(c->view,bytes,e);
    case 5:return q3n_player_state_restore(c->player_state,bytes,e);
    case 6:return q3n_hud_restore(c->hud,bytes,e);
    case 7:return q3n_server_commands_restore(c->commands,bytes,e);
    case 8:return q3n_loading_restore(c->loading,bytes,e);
    case 9:return c->mission?q3n_mission_hud_restore(c->mission,bytes,e):bytes.size==0;
    case 10: {
        frontend_unified_q3_snapshots_options options=frontend_unified_q3_runtime_cache_options(o);
        return frontend_unified_q3_snapshots_restore(&options,bytes,&c->snapshots,e);
    }
    case 11:return o->options.backend_restore(o->options.context,c->presentation,bytes,e);
    default:return false;
    }
}
bool frontend_unified_q3_runtime_checkpoint(const frontend_unified_q3_runtime *o,qa_buffer *out,qa_error *e)
{
    if(!out || out->data || out->size || !ready(o,false,e))return false;
    frontend_unified_q3_runtime copy=*o; qa_source_save_io io={0};
    frontend_unified_q3_runtime *held=(frontend_unified_q3_runtime *)o; held->busy=true;
    uint32_t presence=child_presence(o);
    bool okay=qa_source_save_writer(&io,NULL,e) && fields(&copy,&io,&presence);
    for(unsigned i=0;okay && i<12;++i) {
        qa_buffer saved={0}; okay=!(presence&(1u<<i)) || child_write(o,i,&saved,e);
        qa_bytes bytes={saved.data,saved.size}; if(okay)okay=blob(&io,&bytes); qa_buffer_free(&saved);
    }
    held->busy=false;
    if(okay)okay=ready(o,false,e) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
static void prune_closed_children(frontend_unified_q3_runtime *o,uint32_t presence)
{
    if(presence!=2049u)return;
    frontend_unified_q3_runtime_owners *c=&o->children;
    q3n_loading_destroy(c->loading);c->loading=NULL;q3n_mission_hud_destroy(c->mission);c->mission=NULL;
    q3n_server_commands_destroy(c->commands);c->commands=NULL;q3n_hud_destroy(c->hud);c->hud=NULL;
    q3n_player_state_destroy(c->player_state);c->player_state=NULL;q3n_view_destroy(c->view);c->view=NULL;
    q3n_particles_destroy(c->particles);c->particles=NULL;q3n_events_destroy(c->events);c->events=NULL;
    q3n_weapons_destroy(c->weapons);c->weapons=NULL;
}
bool frontend_unified_q3_runtime_restore(frontend_unified_q3_runtime *o,qa_bytes bytes,qa_error *e)
{
    if(!ready(o,true,e) || o->restored || (o->children.snapshots && o->imported_children<11))return false;
    if(o->import_bytes.data && (bytes.size!=o->import_bytes.size ||
       !bytes.data || memcmp(bytes.data,o->import_bytes.data,bytes.size)))
        return fail(e,"Unified CG retry differs from its retained import");
    if(o->import_bytes.data)bytes=(qa_bytes){o->import_bytes.data,o->import_bytes.size};
    frontend_unified_q3_runtime state=*o; qa_source_save_io io={0}; qa_bytes children[12]={{0}};
    o->busy=true;
    uint32_t presence=0;
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&state,&io,&presence);
    for(unsigned i=0;okay && i<12;++i)okay=blob(&io,children+i);
    if(okay)okay=io.offset==io.input.size;
    if(okay && !o->import_bytes.data) {
        o->import_bytes.data=malloc(bytes.size);
        if(!o->import_bytes.data){qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining Unified CG import");okay=false;}
        else {memcpy(o->import_bytes.data,bytes.data,bytes.size);o->import_bytes.size=bytes.size;}
    }
    if(okay)prune_closed_children(o,presence);
    while(okay && o->imported_children<12) {
        unsigned i=o->imported_children;
        okay=presence&(1u<<i)?child_read(o,i,children[i],e):children[i].size==0;
        if(okay)++o->imported_children;
    }
    if(okay) {
        o->initialized=state.initialized; o->faulted=state.faulted; o->retiring=state.retiring;o->video_generation=state.video_generation;
        o->old_time=state.old_time; o->frame_milliseconds=state.frame_milliseconds; o->client_frame=state.client_frame;
        o->presentation_time=state.presentation_time;
        o->stereo=state.stereo; o->refdef=state.refdef; o->view_angles=state.view_angles;
    }
    o->busy=false;
    if(okay)okay=ready(o,true,e);
    if(okay){o->restored=true;qa_buffer_free(&o->import_bytes);o->imported_children=0;}
    qa_source_save_dispose(&io);
    return okay || (e && e->code ? false : fail(e,"Unified CG continuation differs from its imported owners"));
}
bool frontend_unified_q3_runtime_restore_ready(const frontend_unified_q3_runtime *o,qa_error *e)
{
    q3n_compiled_source_view source;qa_q3_presentation_binding backend;
    if(!o || !o->restoring || !o->restored || !o->complete || (!o->retiring && !o->children.snapshots) || o->video || o->prepared ||
       !frontend_unified_q3_runtime_idle(o) || !frontend_unified_q3_client_checkpoint_current(o->options.client) ||
       !frontend_remote_unified_checkpoint_current(o->options.replica,e))
        return fail(e,"Unified CG import has not returned its genuine retained graph");
    if(!q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,e) ||
       !qa_q3_presentation_binding_read(o->children.presentation,&backend,e))return false;
    return o->initialized==source.basis.initialized && source.basis.assets==o->options.presentation.assets &&
        backend.options.assets==source.basis.assets && backend.options.owner==o->options.presentation.owner &&
        backend.options.context==o->options.presentation.context && backend.options.seat==source.basis.physical_seat &&
        q3n_compiled_source_checkpoint_current(&source) ? true :
        fail(e,"Unified CG imported graph differs from its actual Source and presentation owners");
}
bool frontend_unified_q3_runtime_restore_bind(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!o || o->retiring || !o->restoring || !o->restored || !o->children.snapshots || !frontend_unified_q3_runtime_idle(o) ||
       !frontend_remote_unified_current(o->options.replica,e) || !frontend_unified_q3_client_current(o->options.client) ||
       !o->options.current(o->options.context,&o->options))return fail(e,"Unified CG bind requires its actually installed live parents");
    o->restoring=false; return true;
}
bool frontend_unified_q3_runtime_restore_passive_finish(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!o || !o->options.frontend->source_restoring ||
       frontend_remote_unified_restore_pending(o->options.replica) ||
       !(frontend_remote_unified_retired(o->options.replica) ||
         frontend_unified_q3_client_retirement_departed(o->options.client)) ||
       !frontend_unified_q3_runtime_restore_ready(o,e))
        return fail(e,"Passive CG continuation requires its actually retired installed replica");
    o->passive=true;return true;
}
