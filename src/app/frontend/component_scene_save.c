#include "component_scene_save.h"
#include "music_sources.h"
#include "capture.h"
#include "qa/source_save.h"
#include "qa/q3_presentation_save.h"
#include "qa/q3_scene_packet_save.h"
#include "qa/audio_music_prepare.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct component_saved {
    uint64_t identity,view,pool,sequence;
    double clock_ms;
    qa_q3_presentation_options policy;
    bool begun,music,attached,looping,pending;
    const char *intro,*loop;
    qa_bytes frame,scene,movies,player;
    qa_bytes *packets;
    size_t packet_count;
    qa_q3_picture_receipt *pictures;
    uint64_t *picture_keys;
    bool *picture_images;
    size_t picture_count;
    uint64_t picture_frame;
    size_t picture_cursor;
    bool picture_frame_valid;
} component_saved;
struct frontend_component_scene_restore_set {
    qa_frontend *frontend;
    component_saved *rows;
    size_t count;
    bool prepared,attempted,restored;
};
static bool span(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=bytes->size;
    if(!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool policy_fields(qa_source_save_io *io,qa_q3_presentation_options *p)
{
    return qa_source_save_i32(io,&p->viewport.x) && qa_source_save_i32(io,&p->viewport.y) &&
        qa_source_save_u32(io,&p->viewport.width) && qa_source_save_u32(io,&p->viewport.height) &&
        qa_source_save_f32(io,&p->near_clip) && qa_source_save_f32(io,&p->far_clip) &&
        qa_source_save_f32(io,&p->identity_light) && qa_source_save_f32(io,&p->lod_scale) &&
        qa_source_save_f32(io,&p->lod_bias) && qa_source_save_u32(io,&p->shadow_mode) &&
        qa_source_save_i32(io,&p->rail_core_width) && qa_source_save_i32(io,&p->rail_ring_width) &&
        qa_source_save_f32(io,&p->rail_segment_length);
}
static bool picture_fields(qa_source_save_io *io,qa_q3_picture_receipt *p,uint64_t *key,bool *image)
{
    return qa_source_save_bool(io,image) && qa_source_save_bool(io,&p->source_raw) &&
        (!p->source_raw || *image) && qa_source_save_u64(io,key) && *key &&
        qa_source_save_i32(io,&p->shader) && qa_source_save_i32(io,&p->milliseconds) &&
        qa_source_save_f32(io,&p->rect.x) && qa_source_save_f32(io,&p->rect.y) &&
        qa_source_save_f32(io,&p->rect.width) && qa_source_save_f32(io,&p->rect.height) &&
        qa_source_save_f32(io,&p->uv.x) && qa_source_save_f32(io,&p->uv.y) &&
        qa_source_save_f32(io,&p->uv.z) && qa_source_save_f32(io,&p->uv.w) &&
        qa_source_save_f32(io,&p->color.x) && qa_source_save_f32(io,&p->color.y) &&
        qa_source_save_f32(io,&p->color.z) && qa_source_save_f32(io,&p->color.w) &&
        qa_source_save_i32(io,&p->viewport.x) && qa_source_save_i32(io,&p->viewport.y) &&
        qa_source_save_u32(io,&p->viewport.width) && qa_source_save_u32(io,&p->viewport.height) &&
        qa_source_save_u32(io,&p->seat) && qa_source_save_f32(io,&p->identity_light);
}
static bool row_fields(qa_source_save_io *io,component_saved *row)
{
    if(!qa_source_save_u64(io,&row->identity) || !row->identity ||
        !qa_source_save_u64(io,&row->view) || !row->view ||
        !qa_source_save_u64(io,&row->pool) || !row->pool ||
        !qa_source_save_u64(io,&row->sequence) || !qa_source_save_f64(io,&row->clock_ms) ||
        !isfinite(row->clock_ms) || row->clock_ms<0 || !policy_fields(io,&row->policy) ||
        !qa_source_save_bool(io,&row->begun) || !qa_source_save_bool(io,&row->music) ||
        !qa_source_save_bool(io,&row->attached) || !qa_source_save_bool(io,&row->looping) ||
        !qa_source_save_bool(io,&row->pending) || !qa_source_save_text(io,&row->intro) ||
        !qa_source_save_text(io,&row->loop) || !span(io,&row->frame) || !row->frame.size ||
        !span(io,&row->scene) || !row->scene.size || !span(io,&row->movies) || !row->movies.size ||
        !span(io,&row->player) || ((!row->music || row->attached) && row->player.size) ||
        (row->music && !row->attached && !row->player.size) ||
        (!row->music && (row->attached || row->looping || row->pending || row->intro || row->loop))) return false;
    if(!qa_source_save_count(io,&row->packet_count,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(io->offset>io->input.size || row->packet_count>(io->input.size-io->offset)/8 ||
            row->packet_count>SIZE_MAX/sizeof(*row->packets)) return false;
        row->packets=row->packet_count?calloc(row->packet_count,sizeof(*row->packets)):NULL;
        if(row->packet_count && !row->packets) return false;
    }
    if(!row->begun && row->packet_count) return false;
    for(size_t i=0;i<row->packet_count;++i) if(!span(io,row->packets+i) || !row->packets[i].size) return false;
    if(!qa_source_save_count(io,&row->picture_count,SIZE_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(io->offset>io->input.size || row->picture_count>(io->input.size-io->offset)/89 ||
            row->picture_count>SIZE_MAX/sizeof(*row->pictures) || row->picture_count>SIZE_MAX/sizeof(*row->picture_keys)) return false;
        row->pictures=row->picture_count?calloc(row->picture_count,sizeof(*row->pictures)):NULL;
        row->picture_keys=row->picture_count?calloc(row->picture_count,sizeof(*row->picture_keys)):NULL;
        row->picture_images=row->picture_count?calloc(row->picture_count,sizeof(*row->picture_images)):NULL;
        if(row->picture_count && (!row->pictures || !row->picture_keys || !row->picture_images)) return false;
    }
    if(!row->begun && row->picture_count) return false;
    for(size_t i=0;i<row->picture_count;++i) if(!picture_fields(io,row->pictures+i,row->picture_keys+i,row->picture_images+i)) return false;
    return qa_source_save_bool(io,&row->picture_frame_valid) && qa_source_save_u64(io,&row->picture_frame) &&
        qa_source_save_count(io,&row->picture_cursor,row->picture_count) &&
        (row->picture_frame_valid || (!row->picture_frame && !row->picture_cursor));
}
static bool prefix(qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','F','C','P'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFCP",4) &&
        qa_source_save_count(io,count,SIZE_MAX);
}
static void row_free(component_saved *row)
{ free(row->packets); free(row->pictures); free(row->picture_keys); free(row->picture_images); }
void frontend_component_scene_restore_set_destroy(frontend_component_scene_restore_set *set)
{
    if(!set) return;
    for(size_t i=0;set->rows && i<set->count;++i) row_free(set->rows+i);
    free(set->rows); free(set);
}
bool frontend_component_scenes_capture_frames(qa_frontend *f,frontend_scene_namespace *space,qa_error *e)
{
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,e) ||
            !frontend_scene_namespace_capture_frame(space,i+2,row.frame,e)) return false;
    }
    return f && space;
}
bool frontend_component_scenes_bind_frames(qa_frontend *f,frontend_scene_namespace *space,qa_error *e)
{
    if(!f || !f->source_restoring || !space) return false;
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if(!frontend_component_scene_metadata_read(f,i,&row,e) ||
            !frontend_component_scene_restore_frame_bind(f,row.identity,space,i+2,e)) return false;
    }
    return true;
}
static bool write_state(qa_source_save_io *io,component_saved *row,const frontend_component_scene_view *view,
    qa_frontend *f,const frontend_component_scene_save_refs *refs,qa_error *e)
{
    qa_buffer frame={0},scene={0},movies={0},player={0};
    qa_buffer *packets=NULL;
    qa_q3_movie_checkpoint_refs movie_refs={0};
    *row=(component_saved){.identity=view->identity,.view=qa_application_content_view_id(refs->content,view->files),
        .pool=qa_application_content_pool_id(refs->content,qa_vfs_resources(view->files)),.sequence=view->sequence,
        .policy=view->policy,.clock_ms=view->clock_ms,
        .begun=view->begun,.music=view->music!=NULL,.attached=view->music_attached,
        .looping=view->music_looping,.pending=view->music_pending,.intro=view->music_intro,.loop=view->music_loop,
        .packet_count=view->packet_count,.picture_count=view->picture_count,.picture_frame=view->picture_frame,
        .picture_cursor=view->picture_cursor,.picture_frame_valid=view->picture_frame_valid};
    bool ok=frontend_q3_component_movie_refs(refs->q3,view->identity,&movie_refs,e) &&
        qa_scene_frame_checkpoint(view->frame,refs->frame,&frame,e) &&
        qa_q3_presentation_scene_checkpoint(view->presentation,&scene,e) &&
        qa_q3_presentation_media_checkpoint(view->presentation,&movie_refs,&movies,e) &&
        (!view->music || view->music_attached || qa_audio_music_checkpoint(view->music,refs->audio,&player,e));
    if(ok && row->packet_count) {
        row->packets=calloc(row->packet_count,sizeof(*row->packets)); packets=calloc(row->packet_count,sizeof(*packets));
        ok=row->packets && packets;
    }
    for(size_t i=0;ok && i<row->packet_count;++i) {
        frontend_component_scene_packet p;
        ok=frontend_component_scene_packet_read(f,view->identity,view->sequence,i,&p,e);
        if(ok) {
            qa_q3_scene_packet_view packet={.definition=p.definition,.options=p.options,.entities=p.entities,
                .entity_count=p.entity_count,.polygons=p.polygons,.polygon_count=p.polygon_count,
                .vertices=p.vertices,.vertex_count=p.vertex_count,.lights=p.lights,.light_count=p.light_count};
            ok=qa_q3_scene_packet_checkpoint(&packet,refs->frame,packets+i,e);
            row->packets[i]=(qa_bytes){packets[i].data,packets[i].size};
        }
    }
    if(ok && row->picture_count) {
        row->pictures=calloc(row->picture_count,sizeof(*row->pictures)); row->picture_keys=calloc(row->picture_count,sizeof(*row->picture_keys));
        row->picture_images=calloc(row->picture_count,sizeof(*row->picture_images));
        ok=row->pictures && row->picture_keys && row->picture_images;
    }
    for(size_t i=0;ok && i<row->picture_count;++i) {
        ok=frontend_component_scene_picture_read(f,view->identity,view->sequence,i,row->pictures+i,e);
        row->picture_images[i]=row->pictures[i].image!=NULL;
        if(ok) ok=row->picture_images[i]?
            frontend_scene_image_encode(refs->scene,row->pictures[i].image,row->picture_keys+i,e):
            frontend_scene_material_encode(refs->scene,row->pictures[i].material,row->picture_keys+i,e);
    }
    row->frame=(qa_bytes){frame.data,frame.size}; row->scene=(qa_bytes){scene.data,scene.size};
    row->movies=(qa_bytes){movies.data,movies.size}; row->player=(qa_bytes){player.data,player.size};
    ok=ok && row_fields(io,row) && frontend_component_scene_metadata_current(f,view,e);
    for(size_t i=0;packets && i<row->packet_count;++i) qa_buffer_free(packets+i);
    free(packets); row_free(row);
    qa_buffer_free(&frame); qa_buffer_free(&scene); qa_buffer_free(&movies); qa_buffer_free(&player); return ok;
}
bool frontend_component_scenes_checkpoint(qa_frontend *f,const frontend_component_scene_save_refs *refs,qa_buffer *out,qa_error *e)
{
    if(!f || !f->capture || !refs || !refs->content || !refs->scene || !refs->frame || !refs->q3) return false;
    qa_source_save_io io={0}; size_t count=frontend_component_scene_count(f);
    bool ok=qa_source_save_writer(&io,qa_application_session(f->application),e) && prefix(&io,&count);
    for(size_t i=0;ok && i<count;++i) {
        frontend_component_scene_view view; component_saved row={0};
        ok=frontend_component_scene_metadata_read(f,i,&view,e) && write_state(&io,&row,&view,f,refs,e);
    }
    ok=ok && qa_source_save_finish(&io,out); qa_source_save_dispose(&io); return ok;
}
bool frontend_component_scenes_prepare_restored(qa_frontend *f,qa_application_content_graph *graph,qa_bytes bytes,
    frontend_component_scene_restore_set **out,qa_error *e)
{
    if(!f || !f->source_restoring || f->capture || f->resource_inventory || !graph || !out || *out) return false;
    frontend_component_scene_restore_set *set=calloc(1,sizeof(*set));
    if(!set) return frontend_fail(e,QA_ERROR_MEMORY,"Decoding component private topology");
    set->frontend=f; *out=set;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(f->application),bytes,e) && prefix(&io,&set->count);
    if(ok) {
        ok=io.offset<=io.input.size && set->count<=(io.input.size-io.offset)/128 && set->count<=SIZE_MAX/sizeof(*set->rows);
        if(ok) { set->rows=set->count?calloc(set->count,sizeof(*set->rows)):NULL; ok=!set->count || set->rows; }
    }
    /* All portable rows and opaque extents qualify before any graph claim. */
    for(size_t i=0;ok && i<set->count;++i) {
        component_saved *row=set->rows+i; ok=row_fields(&io,row);
        for(size_t j=0;ok && j<i;++j) ok=set->rows[j].identity!=row->identity;
        const qa_vfs *files=ok?qa_application_content_view(graph,row->view):NULL;
        ok=ok && files && qa_vfs_resources(files)==qa_application_content_pool(graph,row->pool);
    }
    ok=ok && qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    for(size_t i=0;ok && i<set->count;++i) {
        qa_vfs *files=NULL; component_saved *row=set->rows+i;
        ok=qa_application_content_retain_view(graph,row->view,&files,e) &&
            frontend_component_scene_restore_prepare(f,row->identity,&files,&row->policy,e);
        qa_vfs_destroy(files);
    }
    set->prepared=ok;
    return ok || frontend_fail(e,QA_ERROR_FORMAT,"Component private topology leaves its actual graph namespace");
}
bool frontend_component_scene_restore_set_order(frontend_component_scene_restore_set *set,qa_error *e)
{
    if(!set || !set->prepared || set->attempted || !set->frontend->source_restoring) return false;
    uint64_t *ids=set->count?malloc(set->count*sizeof(*ids)):NULL;
    if(set->count && !ids) return frontend_fail(e,QA_ERROR_MEMORY,"Ordering restored component renderer owners");
    for(size_t i=0;i<set->count;++i) ids[i]=set->rows[i].identity;
    bool ok=frontend_component_scenes_restore_order(set->frontend,ids,set->count,e);
    free(ids); return ok;
}
bool frontend_component_scenes_restore_continuation(frontend_component_scene_restore_set *set,
    const frontend_component_scene_save_refs *refs,qa_error *e)
{
    if(!set || !set->prepared || set->attempted || set->restored || !refs || !refs->q3 || !refs->frame || !refs->scene ||
        !set->frontend->source_restoring || frontend_component_scene_count(set->frontend)!=set->count) return false;
    qa_frontend *f=set->frontend;
    set->attempted=true;
    for(size_t i=0;i<set->count;++i) {
        component_saved *row=set->rows+i; frontend_component_scene_view current; qa_q3_movie_checkpoint_refs movies={0};
        if(!frontend_component_scene_metadata_read(f,i,&current,e) || current.identity!=row->identity ||
            qa_application_content_view_id(refs->content,current.files)!=row->view ||
            !frontend_q3_component_movie_refs(refs->q3,row->identity,&movies,e) ||
            !frontend_component_scene_restore_output(f,row->identity,row->sequence,row->begun,row->frame,refs->frame,e) ||
            !frontend_scene_namespace_qualify_frame(refs->scene,i+2,current.frame,e) ||
            !qa_q3_presentation_scene_restore(current.presentation,row->scene,e) ||
            !qa_q3_presentation_media_restore(current.presentation,&movies,row->identity,row->clock_ms,row->movies,e)) return false;
        for(size_t j=0;j<row->packet_count;++j) {
            qa_q3_scene_packet_owner *owner=NULL; qa_q3_scene_packet_view p;
            bool ok=qa_q3_scene_packet_restore(row->packets[j],refs->frame,&owner,e) && qa_q3_scene_packet_read(owner,&p);
            if(ok) {
                frontend_component_scene_packet packet={.definition=p.definition,.options=p.options,.entities=p.entities,
                    .entity_count=p.entity_count,.polygons=p.polygons,.polygon_count=p.polygon_count,
                    .vertices=p.vertices,.vertex_count=p.vertex_count,.lights=p.lights,.light_count=p.light_count};
                ok=frontend_component_scene_restore_packet(f,row->identity,&packet,e);
            }
            qa_q3_scene_packet_destroy(owner); if(!ok) return false;
        }
        for(size_t j=0;j<row->picture_count;++j) {
            qa_q3_picture_receipt picture=row->pictures[j];
            picture.assets=current.assets;
            bool decoded=row->picture_images[j]?
                frontend_scene_image_decode(refs->scene,row->picture_keys[j],&picture.image,e):
                frontend_scene_material_decode(refs->scene,row->picture_keys[j],&picture.material,e);
            if(!decoded ||
                !frontend_component_scene_restore_picture(f,row->identity,&picture,e)) return false;
        }
        if(!frontend_component_scene_restore_picture_cursor(f,row->identity,row->picture_frame_valid,
            row->picture_frame,row->picture_cursor,e)) return false;
        qa_audio_music *player=NULL;
        bool ok=true;
        if(row->music && row->attached) {
            player=qa_audio_engine_bus_music(f->audio,row->identity); ok=player && qa_audio_music_retain(player,e);
            if(!ok) player=NULL;
        } else if(row->music) ok=qa_audio_music_restore(row->player,refs->audio,&player,e);
        if(ok) ok=frontend_component_scene_restore_music(f,row->identity,&player,row->attached,
            row->intro,row->loop,row->looping,row->pending,e);
        qa_audio_music_release(player); if(!ok) return false;
    }
    set->restored=true; return true;
}
static bool same_text(const char *a,const char *b)
{ return (!a && !b) || (a && b && !strcmp(a,b)); }
bool frontend_component_scene_save_current(const qa_frontend *f,const frontend_component_scene_restore_set *set,
    const void *owner,uint64_t identity,qa_error *e)
{
    if(!f || !owner || !identity || (f->source_restoring?(!set || set->frontend!=f || !set->restored):!f->capture)) return false;
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view actual;
        if(!frontend_component_scene_metadata_read(f,i,&actual,e)) return false;
        if(actual.identity!=identity) continue;
        if(actual.owner!=owner || !frontend_component_scene_metadata_current(f,&actual,e)) return false;
        if(!f->source_restoring) {
            bool assets=false,images=false,library=false;
            for(size_t j=0;frontend_capture_assets_at(f->capture,j);++j)
                if(frontend_capture_assets_at(f->capture,j)==actual.assets) assets=true;
            for(size_t j=0;frontend_capture_images_at(f->capture,j);++j)
                if(frontend_capture_images_at(f->capture,j)==actual.images) images=true;
            for(size_t j=0;frontend_capture_library_at(f->capture,j);++j)
                if(frontend_capture_library_at(f->capture,j)==actual.materials) library=true;
            return assets && images && library;
        }
        if(i>=set->count) return false;
        const component_saved *saved=set->rows+i;
        return saved->identity==identity && actual.clock_ms==saved->clock_ms &&
            actual.sequence==saved->sequence && actual.begun==saved->begun &&
            actual.frame && actual.frame->owner==identity && actual.frame->sequence==saved->sequence &&
            actual.packet_count==saved->packet_count && actual.picture_count==saved->picture_count &&
            actual.picture_frame_valid==saved->picture_frame_valid && actual.picture_frame==saved->picture_frame &&
            actual.picture_cursor==saved->picture_cursor &&
            (actual.music!=NULL)==saved->music && actual.music_attached==saved->attached &&
            actual.music_looping==saved->looping && actual.music_pending==saved->pending &&
            same_text(actual.music_intro,saved->intro) && same_text(actual.music_loop,saved->loop) &&
            qa_application_content_view_id(qa_application_content_graph_read(f->application),actual.files)==saved->view;
    }
    return frontend_fail(e,QA_ERROR_FORMAT,"Component child lacks its actual shared private scene continuation");
}
