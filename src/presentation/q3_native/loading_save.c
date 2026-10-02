#include "loading_internal.h"

bool q3nl_capture(q3n_loading *o,qa_error *e)
{
    qa_q3_product product; qa_q3_presentation_assets *a=o?o->options.assets:NULL;
    return q3n_loading_idle(o) && a && a->capturing && a->busy==1 && !a->codec_busy &&
        q3nl_checkpoint_basis(&o->options,&product,e) && product==o->product ? true :
        q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading continuation requires its actual source and parent asset capture lease");
}
static bool fields(qa_source_save_io *io,q3n_loading *o,q3n_loading_state *s)
{
    uint8_t magic[4]={'Q','3','L','D'}; uint32_t version=1,product=(uint32_t)o->product;
    uint32_t seat=o->options.seat,physical=o->options.presentation_seat;
    const char *expected=o->options.compiled_source?"Q3LC":o->options.remote_source?"Q3LR":"Q3LD";
    memcpy(magic,expected,4);
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,expected,4) || !qa_source_save_u32(io,&version) || version!=1 ||
       !qa_source_save_u32(io,&product) || product!=(uint32_t)o->product ||
       !qa_source_save_u32(io,&seat) || seat!=o->options.seat ||
       !qa_source_save_u32(io,&physical) || physical!=o->options.presentation_seat)return false;
    if(o->options.compiled_source && !q3n_compiled_source_fields(io,o->options.compiled_source))return false;
    if(o->options.remote_source) {
        q3n_remote_source_view source;
        if(!q3n_remote_source_read(o->options.remote_source,&source,io->error))return false;
        const qa_native_q3_remote_client_basis *b=&source.basis;
        uint64_t owner=b->connection.owner,generation=b->connection.generation,epoch=b->epoch;
        uint64_t restart=b->restart_generation,publication=b->publication_generation,configuration=b->configuration_generation;
        uint32_t slot=b->connection.slot,client=b->physical_client;
        int32_t message=b->initial_message,command=b->initial_command;
        if(!qa_source_save_u64(io,&owner) || owner!=b->connection.owner ||
           !qa_source_save_u64(io,&generation) || generation!=b->connection.generation ||
           !qa_source_save_u32(io,&slot) || slot!=b->connection.slot ||
           !qa_source_save_u64(io,&epoch) || epoch!=b->epoch ||
           !qa_source_save_u64(io,&restart) || restart!=b->restart_generation ||
           !qa_source_save_u64(io,&publication) || publication!=b->publication_generation ||
           !qa_source_save_u64(io,&configuration) || configuration!=b->configuration_generation ||
           !qa_source_save_u32(io,&client) || client!=b->physical_client ||
           !qa_source_save_i32(io,&message) || message!=b->initial_message ||
           !qa_source_save_i32(io,&command) || command!=b->initial_command || !q3n_remote_source_current(&source))return false;
    }
    if(
       !qa_source_save_bytes(io,s->text,sizeof(s->text)) || !memchr(s->text,0,sizeof(s->text)) ||
       !qa_source_save_u32(io,&s->player_count) || s->player_count>16 ||
       !qa_source_save_u32(io,&s->item_count) || s->item_count>26)return false;
    for(uint32_t i=0;i<s->player_count;++i) {
        const qa_material *material;
        if(!qa_source_save_i32(io,&s->player_icons[i]) || s->player_icons[i]<=0 ||
           !q3p_shader_get(o->options.assets,s->player_icons[i],&material,io->error) || !material)return false;
    }
    for(uint32_t i=0;i<s->item_count;++i) {
        const qa_material *material;
        if(!qa_source_save_i32(io,&s->item_icons[i]) ||
           !q3p_shader_get(o->options.assets,s->item_icons[i],&material,io->error))return false;
    }
    return true;
}
bool q3n_loading_checkpoint(const q3n_loading *borrowed,qa_buffer *out,qa_error *e)
{
    q3n_loading *o=(q3n_loading *)borrowed;
    if(!out || out->data || out->size || !q3nl_capture(o,e))return false;
    o->busy=true; q3n_loading_state copy=o->state; qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,o,&copy) && qa_source_save_finish(&io,out);
    if(!ok && e && e->code==QA_OK)q3nl_fail(e,QA_ERROR_FORMAT,"Native loading continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
bool q3n_loading_restore(q3n_loading *o,qa_bytes bytes,qa_error *e)
{
    if(!q3nl_capture(o,e))return false;
    o->busy=true; q3n_loading_state candidate={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,o,&candidate) && qa_source_save_finish(&io,NULL);
    if(ok)o->state=candidate;
    else if(e && e->code==QA_OK)q3nl_fail(e,QA_ERROR_FORMAT,"Saved native loading continuation is inconsistent");
    qa_source_save_dispose(&io); o->busy=false; return ok;
}
