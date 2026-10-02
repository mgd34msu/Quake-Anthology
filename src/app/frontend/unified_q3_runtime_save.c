#include "unified_q3_runtime_private.h"
#include "../../presentation/q3/internal.h"
#include "qa/q3_presentation_save.h"

static bool fail(qa_error *e,const char *message)
{ qa_error_set(e,QA_ERROR_FORMAT,0,"%s",message); return false; }
static bool ready(const frontend_unified_q3_runtime *o,bool reading,qa_error *e)
{
    q3n_compiled_source_view source; qa_q3_presentation_binding backend;
    const qa_q3_presentation_assets *assets=o?o->options.presentation.assets:NULL;
    return o && o->complete && !o->video && !o->prepared && o->restoring==reading &&
        frontend_unified_q3_runtime_idle(o) && assets && assets->capturing && assets->busy==1 && !assets->codec_busy &&
        frontend_unified_q3_client_checkpoint_current(o->options.client) &&
        q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,e) &&
        source.basis.assets==assets && qa_q3_presentation_binding_read(o->children.presentation,&backend,e) &&
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
static bool fields(frontend_unified_q3_runtime *o,qa_source_save_io *io)
{
    uint8_t magic[4]={'U','Q','3','R'}; uint32_t version=1;
    q3n_compiled_source_view source;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"UQ3R",4) &&
        qa_source_save_u32(io,&version) && version==1 &&
        q3n_compiled_source_fields(io,frontend_unified_q3_client_source(o->options.client)) &&
        q3n_compiled_source_checkpoint_read(frontend_unified_q3_client_source(o->options.client),&source,io->error) &&
        qa_source_save_bool(io,&o->initialized) && o->initialized==source.basis.initialized &&
        qa_source_save_bool(io,&o->faulted) && qa_source_save_u64(io,&o->video_generation) &&
        qa_source_save_i32(io,&o->old_time) && qa_source_save_i32(io,&o->frame_milliseconds) && o->frame_milliseconds>=0 &&
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
    bool okay=qa_source_save_writer(&io,NULL,e) && fields(&copy,&io);
    for(unsigned i=0;okay && i<12;++i) {
        qa_buffer saved={0}; okay=child_write(o,i,&saved,e);
        qa_bytes bytes={saved.data,saved.size}; if(okay)okay=blob(&io,&bytes); qa_buffer_free(&saved);
    }
    if(okay)okay=ready(o,false,e) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_unified_q3_runtime_restore(frontend_unified_q3_runtime *o,qa_bytes bytes,qa_error *e)
{
    if(!ready(o,true,e) || o->children.snapshots)return false;
    frontend_unified_q3_runtime state=*o; qa_source_save_io io={0}; qa_bytes children[12]={{0}};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&state,&io);
    for(unsigned i=0;okay && i<12;++i)okay=blob(&io,children+i);
    if(okay)okay=io.offset==io.input.size;
    for(unsigned i=0;okay && i<12;++i)okay=child_read(o,i,children[i],e);
    if(okay) {
        o->initialized=state.initialized; o->faulted=state.faulted; o->video_generation=state.video_generation;
        o->old_time=state.old_time; o->frame_milliseconds=state.frame_milliseconds; o->client_frame=state.client_frame;
        o->stereo=state.stereo; o->refdef=state.refdef; o->view_angles=state.view_angles;
        okay=ready(o,true,e);
    }
    qa_source_save_dispose(&io);
    return okay || (e && e->code ? false : fail(e,"Unified CG continuation differs from its imported owners"));
}
bool frontend_unified_q3_runtime_restore_bind(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!o || !o->restoring || !o->children.snapshots || !frontend_unified_q3_runtime_idle(o) ||
       !frontend_remote_unified_current(o->options.replica,e) || !frontend_unified_q3_client_current(o->options.client) ||
       !o->options.current(o->options.context,&o->options))return fail(e,"Unified CG bind requires its actually installed live parents");
    o->restoring=false; return true;
}
