#include "native_internal.h"
#include "../q3/internal.h"
#include "qa/game_q3_source.h"

#include <stdlib.h>
#include <string.h>

static bool capture(const q3n_native *o,qa_error *e)
{
    qa_native_q3_client_basis basis;
    qa_q3_presentation_binding backend;
    const qa_native_q3_client_services *services=o?qa_native_q3_client_services_read(o->options.client):NULL;
    if(!o || o->busy || !o->source_lease || !o->assets || !o->assets->capturing ||
        o->assets->busy!=1 || o->assets->codec_busy || !qa_native_q3_client_service_idle(o->options.client) ||
        !services || services->wire_reader!=o->options.reader ||
        !qa_native_q3_wire_reader_idle(o->options.reader) || !qa_native_q3_wire_reader_current(o->options.reader) ||
        !qa_native_q3_client_basis_read(o->options.client,&basis,e) ||
        !qa_q3_presentation_binding_read(o->options.presentation,&backend,e) ||
        backend.options.seat!=o->physical_presentation_seat ||
        basis.source_game!=o->source_game || basis.session!=o->session || basis.source_owner!=o->source_owner ||
        basis.source_launch->storage!=qa_launch_instance_lease_view(o->source_lease)->storage ||
        basis.map_revision!=o->map_revision || basis.seat!=o->seat || basis.physical_client!=o->physical_client ||
        !qa_actor_id_equal(basis.viewing_actor,o->viewing_actor) || !q3n_clients_idle(o->clients) ||
        !q3n_media_idle(o->media) || !q3n_weapons_idle(o->weapons) || !q3n_events_idle(o->events) ||
        !q3n_particles_idle(o->particles) || !q3n_view_idle(o->view) || !q3n_player_state_idle(o->player_state) ||
        !q3n_hud_idle(o->hud) || !q3n_server_commands_idle(o->commands))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME codec requires its actual source, idle children and asset capture lease");
    return true;
}
static bool finite_float(qa_source_save_io *io,float *value)
{ return qa_source_save_f32(io,value) && isfinite(*value); }
static bool vector(qa_source_save_io *io,qa_vec3 *value)
{ return qa_source_save_vec3(io,value) && qa_vec_finite(*value); }
static bool lerp(qa_source_save_io *io,q3n_lerp_frame *f)
{
    return qa_source_save_i32(io,&f->old_frame) && qa_source_save_i32(io,&f->old_frame_time) &&
        qa_source_save_i32(io,&f->frame) && qa_source_save_i32(io,&f->frame_time) && finite_float(io,&f->back_lerp) &&
        qa_source_save_i32(io,&f->animation_number) && qa_source_save_i32(io,&f->animation_time) &&
        qa_source_save_bool(io,&f->selected);
}
static bool pose(qa_source_save_io *io,q3n_pose_frame *p)
{
    return lerp(io,&p->animation) && finite_float(io,&p->yaw_angle) && finite_float(io,&p->pitch_angle) &&
        qa_source_save_bool(io,&p->yawing) && qa_source_save_bool(io,&p->pitching);
}
static bool entity(qa_source_save_io *io,q3n_native *o,uint32_t index)
{
    q3n_entity *s=&o->entities[index];
    if(!qa_source_save_actor(io,&s->actor) || !qa_source_save_u32(io,&s->physical) ||
        !qa_source_save_u64(io,&s->client_media_revision) || !qa_source_save_bool(io,&s->valid) ||
        !qa_source_save_bool(io,&s->event_only_fired) || !qa_source_save_bool(io,&s->teleport_bit) ||
        !qa_source_save_bool(io,&s->loop_stopped) || !qa_source_save_i32(io,&s->previous_event) ||
        !qa_source_save_i32(io,&s->snapshot_time) || !qa_source_save_i32(io,&s->trail_time) ||
        !qa_source_save_i32(io,&s->dust_trail_time) || !qa_source_save_i32(io,&s->misc_time) ||
        !qa_source_save_i32(io,&s->muzzle_flash_time) || !vector(io,&s->lerp_origin) || !vector(io,&s->lerp_angles) ||
        !pose(io,&s->player.legs) || !pose(io,&s->player.torso) || !qa_source_save_i32(io,&s->player.pain_time) ||
        !qa_source_save_bool(io,&s->player.pain_direction) || !q3n_player_fx_codec(io,&s->player_fx) ||
        !finite_float(io,&s->barrel_angle) || !qa_source_save_i32(io,&s->barrel_time) ||
        !qa_source_save_bool(io,&s->barrel_spinning) || !qa_source_save_bool(io,&s->lightning_firing) ||
        !qa_source_save_bool(io,&s->railgun_flash) || !vector(io,&s->rail_impact))return false;
    if(s->actor.registry && s->physical!=index)return false;
    /* Private poses can still name the previous completed presentation cut
     * after GAME has freed/reused a row. The typed actor codec preserves that
     * retired provenance; q3nn_entity admits a live observation before reuse. */
    if(s->valid && (!s->actor.registry || s->physical!=index))return false;
    return true;
}
static bool refdef(qa_source_save_io *io,qa_q3_refdef *r)
{
    if(!qa_source_save_i32(io,&r->x) || !qa_source_save_i32(io,&r->y) || !qa_source_save_i32(io,&r->width) ||
        !qa_source_save_i32(io,&r->height) || !qa_source_save_i32(io,&r->time) || !qa_source_save_i32(io,&r->flags) ||
        !finite_float(io,&r->fov_x) || !finite_float(io,&r->fov_y) || !vector(io,&r->origin))return false;
    for(unsigned i=0;i<3;++i)if(!vector(io,&r->axis[i]))return false;
    return qa_source_save_bytes(io,r->area_mask,sizeof(r->area_mask)) && qa_source_save_bytes(io,r->text,sizeof(r->text));
}
static bool fields(qa_source_save_io *io,q3n_native *o)
{
    uint8_t magic[4]={'Q','3','N','C'}; uint32_t version=1,product=(uint32_t)o->product,seat=o->seat,physical=o->physical_client;
    uint32_t presentation_seat=o->physical_presentation_seat;
    uint64_t owner=o->source_owner,map=o->map_revision;
    qa_actor_id actor=o->viewing_actor;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3NC",4) || !qa_source_save_u32(io,&version) || version!=1 ||
        !qa_source_save_u32(io,&product) || product!=(uint32_t)o->product || !qa_source_save_u64(io,&owner) || owner!=o->source_owner ||
        !qa_source_save_u64(io,&map) || map!=o->map_revision || !qa_source_save_u32(io,&seat) || seat!=o->seat ||
        !qa_source_save_u32(io,&presentation_seat) || presentation_seat!=o->physical_presentation_seat ||
        !qa_source_save_u32(io,&physical) || physical!=o->physical_client || !qa_source_save_actor(io,&actor) ||
        !qa_actor_id_equal(actor,o->viewing_actor) || !qa_source_save_u64(io,&o->source_frame_number) ||
        !qa_source_save_i32(io,&o->old_time) || !qa_source_save_i32(io,&o->frame_milliseconds) || o->frame_milliseconds<0 ||
        !qa_source_save_i32(io,&o->client_frame) || !qa_source_save_bool(io,&o->has_source_frame) ||
        !qa_source_save_bool(io,&o->initialized) || !qa_source_save_bool(io,&o->faulted) ||
        !refdef(io,&o->previous_refdef) || !vector(io,&o->previous_view_angles))return false;
    const qa_native_q3_client_services *services=qa_native_q3_client_services_read(o->options.client);
    if(!services || services->client.initialized!=o->initialized)return false;
    for(uint32_t i=0;i<QA_Q3_SOURCE_ENTITIES;++i)if(!entity(io,o,i))return false;
    return true;
}
static bool child_write(q3n_native *o,unsigned child,const q3n_client_refs *refs,qa_buffer *out,qa_error *e)
{
    switch(child) {
    case 0:return q3n_clients_checkpoint(o->clients,refs,out,e);
    case 1:return q3n_media_checkpoint(o->media,out,e);
    case 2:return q3n_weapons_checkpoint(o->weapons,out,e);
    case 3:return q3n_events_checkpoint(o->events,out,e);
    case 4:return q3n_particles_checkpoint(o->particles,out,e);
    case 5:return q3n_view_checkpoint(o->view,out,e);
    case 6:return q3n_player_state_checkpoint(o->player_state,out,e);
    case 7:return q3n_hud_checkpoint(o->hud,out,e);
    case 8:return q3n_server_commands_checkpoint(o->commands,out,e);
    default:return false;
    }
}
static bool child_read(q3n_native *o,unsigned child,const q3n_client_refs *refs,qa_bytes bytes,qa_error *e)
{
    switch(child) {
    case 0:return q3n_clients_restore(o->clients,refs,bytes,e);
    case 1:return q3n_media_restore(o->media,bytes,e);
    case 2:return q3n_weapons_restore(o->weapons,bytes,e);
    case 3:return q3n_events_restore(o->events,bytes,e);
    case 4:return q3n_particles_restore(o->particles,bytes,e);
    case 5:return q3n_view_restore(o->view,bytes,e);
    case 6:return q3n_player_state_restore(o->player_state,bytes,e);
    case 7:return q3n_hud_restore(o->hud,bytes,e);
    case 8:return q3n_server_commands_restore(o->commands,bytes,e);
    default:return false;
    }
}
bool q3n_native_checkpoint(const q3n_native *borrowed,const q3n_client_refs *refs,qa_buffer *out,qa_error *e)
{
    if(!out || out->data || out->size || !capture(borrowed,e))return false;
    q3n_native *o=(q3n_native *)borrowed; o->busy=true;
    q3n_native *copy=malloc(sizeof(*copy)); qa_source_save_io io={0};
    bool ok=copy!=NULL;
    if(!ok)q3nn_fail(e,QA_ERROR_MEMORY,"Copying native CGAME continuation");
    if(ok) { *copy=*o; ok=qa_source_save_writer(&io,o->session,e) && fields(&io,copy); }
    for(unsigned i=0;ok && i<9;++i) {
        qa_buffer child={0};
        ok=child_write(o,i,refs,&child,e);
        size_t count=child.size;
        if(ok)ok=qa_source_save_count(&io,&count,64u*1024u*1024u) && qa_source_save_bytes(&io,child.data,count);
        qa_buffer_free(&child);
    }
    if(ok)ok=qa_source_save_finish(&io,out);
    if(!ok && e && e->code==QA_OK)q3nn_fail(e,QA_ERROR_FORMAT,"Native CGAME continuation is inconsistent");
    qa_source_save_dispose(&io); free(copy); o->busy=false; return ok;
}
bool q3n_native_restore(const q3n_native_options *options,const q3n_client_refs *refs,
    qa_bytes bytes,q3n_native **out,qa_error *e)
{
    if(!out || *out)return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME import requires an empty candidate");
    q3n_native *o=NULL;
    if(!q3nn_allocate(options,true,&o,e))return false;
    bool ok=capture(o,e); qa_source_save_io io={0}; o->busy=true;
    if(ok)ok=qa_source_save_reader(&io,o->session,bytes,e) && fields(&io,o);
    for(unsigned i=0;ok && i<9;++i) {
        size_t count=0;
        ok=qa_source_save_count(&io,&count,64u*1024u*1024u);
        if(ok && (io.offset>io.input.size || count>io.input.size-io.offset))ok=false;
        if(ok) {
            qa_bytes child={io.input.data+io.offset,count}; io.offset+=count;
            ok=child_read(o,i,refs,child,e);
        }
    }
    if(ok)ok=qa_source_save_finish(&io,NULL);
    if(!ok && e && e->code==QA_OK)q3nn_fail(e,QA_ERROR_FORMAT,"Saved native CGAME continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false;
    if(!ok) { q3nn_free_children(o); free(o); return false; }
    *out=o; return true;
}
