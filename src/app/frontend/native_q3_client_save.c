#include "native_q3_client_internal.h"
#include "qa/audio_music_prepare.h"
#include "save_private.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_presentation_save.h"

static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?bytes->size:0;
    if(!qa_source_save_count(io,&count,64u*1024u*1024u))return false;
    if(io->direction==QA_SOURCE_SAVE_WRITE)return qa_source_save_bytes(io,(void *)bytes->data,count);
    if(io->offset>io->input.size || count>io->input.size-io->offset)return false;
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool envelope(qa_source_save_io *io,frontend_native_q3_import *state)
{
    uint8_t magic[4]={'Q','F','N','3'}; uint32_t version=1;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFN3",4) &&
        qa_source_save_u32(io,&version) && version==1 && blob(io,&state->reader) && blob(io,&state->client) &&
        blob(io,&state->core) && blob(io,&state->mission) && blob(io,&state->loading) &&
        blob(io,&state->commands) && blob(io,&state->music) && blob(io,&state->composition_state) && blob(io,&state->adapter);
}
bool frontend_native_q3_split(qa_bytes bytes,frontend_native_q3_import *out,qa_error *e)
{
    if(!out)return false;
    frontend_native_q3_import copy=*out; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && envelope(&io,&copy) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok)return frontend_fail(e,QA_ERROR_FORMAT,"Invalid native frontend child envelope");
    *out=copy; return true;
}
static bool adapter_fields(qa_source_save_io *io,frontend_native_q3 *row)
{
    uint8_t magic[4]={'Q','F','N','A'}; uint32_t version=1,product=row->view.product;
    uint64_t identity=row->view.identity; qa_string_id service=row->view.service_owner;
    qa_actor_owner source=row->view.source_owner; uint32_t seat=row->view.seat,launch=row->view.launch_seat,physical=row->view.physical_client;
    qa_actor_id actor=row->view.actor;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QFNA",4) || !qa_source_save_u32(io,&version) || version!=1 ||
        !qa_source_save_u64(io,&identity) || identity!=row->view.identity ||
        !qa_source_save_string(io,&service) || service!=row->view.service_owner ||
        !frontend_save_provider(io,row->frontend->application,&source) || source!=row->view.source_owner ||
        !qa_source_save_u32(io,&product) || product!=(uint32_t)row->view.product ||
        !qa_source_save_u32(io,&seat) || seat!=row->view.seat || !qa_source_save_u32(io,&launch) || launch!=row->view.launch_seat ||
        !qa_source_save_u32(io,&physical) || physical!=row->view.physical_client ||
        !qa_source_save_actor(io,&actor) || !qa_actor_id_equal(actor,row->view.actor))return false;
    qa_audio_listener *listener=&row->view.listener;
    if(!qa_source_save_bool(io,&row->view.has_listener) || !qa_source_save_u32(io,&listener->seat) ||
        !qa_source_save_u64(io,&listener->actor) || !qa_source_save_vec3(io,&listener->origin))return false;
    for(unsigned i=0;i<3;++i)if(!qa_source_save_vec3(io,listener->axis+i))return false;
    if(!qa_source_save_f32(io,&listener->gain) || !qa_source_save_bool(io,&listener->underwater) ||
        !qa_source_save_bool(io,&row->view.music_attached) || !qa_source_save_bool(io,&row->music_looping) ||
        !frontend_save_text(io,&row->music_intro) || !frontend_save_text(io,&row->music_loop) ||
        !frontend_save_text(io,&row->disconnect))return false;
    if(row->view.has_listener && (listener->seat!=row->view.seat || !qa_vec_finite(listener->origin) ||
        !qa_vec_finite(listener->axis[0]) || !qa_vec_finite(listener->axis[1]) || !qa_vec_finite(listener->axis[2]) ||
        !isfinite(listener->gain) || listener->gain<0 ||
        (listener->actor!=QA_AUDIO_NO_ACTOR && !frontend_audio_id_read(row->frontend,listener->actor,NULL,NULL))))return false;
    if(row->view.music_attached && !qa_audio_engine_music_ready(row->frontend->audio,row->view.identity,row->view.seat,1))return false;
    return !row->music_looping || (row->music_intro && row->music_loop);
}
bool frontend_native_q3_checkpoint(frontend_native_q3 *row,const q3n_client_refs *refs,qa_buffer *out,qa_error *e)
{
    if(!row || !out || out->data || out->size || !row->constructed || row->restoring || row->callbacks || row->frame_active ||
        !row->frontend->capture || !frontend_native_q3_current(row))return false;
    qa_buffer children[9]={0}; frontend_native_q3_import state={0};
    frontend_native_q3 copy=*row; qa_source_save_io private={0},io={0};
    qa_audio_music *attached=qa_audio_engine_bus_music(row->frontend->audio,row->view.identity);
    if (attached && attached!=row->view.music) return false;
    copy.view.music_attached=attached!=NULL;
    qa_audio_music *music=row->view.music;
    bool ok=qa_native_q3_wire_reader_checkpoint(row->view.reader,children+6,e) &&
        qa_native_q3_client_checkpoint(row->view.client,children,e) &&
        q3n_native_checkpoint(row->view.core,refs,children+1,e) &&
        (!row->view.mission || q3n_mission_hud_checkpoint(row->view.mission,children+2,e)) &&
        q3n_loading_checkpoint(row->view.loading,children+3,e) &&
        frontend_native_q3_commands_checkpoint(row->commands,children+4,e) &&
        (!music || qa_audio_music_checkpoint(music,children+7,e)) &&
        (!row->composition.checkpoint || row->composition.checkpoint(row->composition.context,children+8,e)) &&
        qa_source_save_writer(&private,qa_application_session(row->frontend->application),e) &&
        adapter_fields(&private,&copy) && qa_source_save_finish(&private,children+5);
    state.client=(qa_bytes){children[0].data,children[0].size}; state.core=(qa_bytes){children[1].data,children[1].size};
    state.mission=(qa_bytes){children[2].data,children[2].size}; state.loading=(qa_bytes){children[3].data,children[3].size};
    state.commands=(qa_bytes){children[4].data,children[4].size}; state.adapter=(qa_bytes){children[5].data,children[5].size};
    state.reader=(qa_bytes){children[6].data,children[6].size}; state.music=(qa_bytes){children[7].data,children[7].size};
    state.composition_state=(qa_bytes){children[8].data,children[8].size};
    if(ok)ok=qa_source_save_writer(&io,NULL,e) && envelope(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&private); qa_source_save_dispose(&io);
    for(unsigned i=0;i<9;++i)qa_buffer_free(children+i);
    if(!ok && e && e->code==QA_OK)frontend_fail(e,QA_ERROR_FORMAT,"Native frontend continuation lost a genuine child owner");
    return ok;
}
bool frontend_native_q3_restore(qa_frontend *f,frontend_native_q3_import *state,const q3n_client_refs *refs,
    frontend_native_q3 **out,qa_error *e)
{
    frontend_native_q3 *row=out?*out:NULL;
    if(!f || !state || !row || row->frontend!=f || !row->restoring || row->constructed ||
        !row->owns_media || !row->owns_services || row->view.client || row->view.core ||
        !frontend_native_q3_current(row) || !frontend_native_q3_import_bind(row,&state->owners,e))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Native continuation requires its adopted prepared physical graph");
    frontend_native_q3 candidate=*row;
    candidate.music_intro=candidate.music_loop=candidate.disconnect=NULL;
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(f->application),state->adapter,e) &&
        adapter_fields(&io,&candidate) && qa_source_save_finish(&io,NULL) &&
        candidate.view.music_attached==row->view.music_attached;
    qa_source_save_dispose(&io);
    qa_native_q3_client_services services={0}; qa_native_q3_client_basis basis;
    q3n_native_options options;
    qa_audio_music *music=NULL; bool music_owned=false;
    if(ok && candidate.view.music_attached) {
        qa_buffer bus={0};
        music=qa_audio_engine_bus_music(f->audio,row->view.identity);
        ok=state->music.size && music && qa_audio_music_checkpoint(music,&bus,e) &&
            bus.size==state->music.size && !memcmp(bus.data,state->music.data,bus.size);
        qa_buffer_free(&bus);
    } else if(ok && state->music.size)ok=qa_audio_music_restore(state->music,&music,e);
    if (ok && candidate.view.music_attached) ok=qa_audio_music_retain(music,e);
    music_owned=ok && music!=NULL;
    if(ok)ok=qa_native_q3_wire_reader_restore(row->view.reader,state->reader,e) &&
        frontend_native_q3_service_options(row,&services,e) &&
        qa_native_q3_client_source_basis_read(f->application,&services,&basis,e) &&
        basis.product==row->view.product &&
        qa_native_q3_client_restore(f->application,&basis,&services,&state->character,state->client,&row->view.client,e) &&
        frontend_native_q3_core_options(row,&options,e) && q3n_native_restore(&options,refs,state->core,&row->view.core,e) &&
        frontend_native_q3_make_children(row,NULL,true,e);
    if(ok)ok=(row->view.mission?q3n_mission_hud_restore(row->view.mission,state->mission,e):state->mission.size==0) &&
        q3n_loading_restore(row->view.loading,state->loading,e) &&
        frontend_native_q3_commands_restore(row->commands,state->commands,e);
    if(ok && row->composition.restore) {
        qa_application_q3_client_context installed;
        ok=frontend_native_q3_installed_context(row,&installed,e) &&
            row->composition.restore(row->composition.context,&installed,state->composition_state,e);
    } else if(ok)ok=state->composition_state.size==0;
    if(ok)ok=frontend_native_q3_current(row);
    if(ok) {
        row->view.listener=candidate.view.listener; row->view.has_listener=candidate.view.has_listener;
        row->view.music_attached=candidate.view.music_attached;
        qa_audio_music_release(row->view.music);
        row->view.music=music;
        free(row->music_intro); free(row->music_loop); free(row->disconnect);
        row->music_intro=candidate.music_intro; row->music_loop=candidate.music_loop; row->disconnect=candidate.disconnect;
        row->music_looping=candidate.music_looping; row->constructed=true; row->restoring=false;
        ok=frontend_native_q3_music_restore_bind(row,e);
    } else {
        if (music_owned) qa_audio_music_release(music);
        free(candidate.music_intro); free(candidate.music_loop); free(candidate.disconnect);
    }
    return ok;
}
