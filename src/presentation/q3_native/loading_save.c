#include "loading_internal.h"

bool q3nl_capture(q3n_loading *o,qa_error *e)
{
    qa_q3_product product; qa_q3_presentation_assets *a=o?o->options.assets:NULL;
    return q3n_loading_idle(o) && a && a->capturing && a->busy==1 && !a->codec_busy &&
        q3nl_basis(&o->options,&product,e) && product==o->product ? true :
        q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading continuation requires its actual source and parent asset capture lease");
}
static bool fields(qa_source_save_io *io,q3n_loading *o,q3n_loading_state *s)
{
    uint8_t magic[4]={'Q','3','L','D'}; uint32_t version=1,product=(uint32_t)o->product;
    uint32_t seat=o->options.seat,physical=o->options.presentation_seat;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3LD",4) || !qa_source_save_u32(io,&version) || version!=1 ||
       !qa_source_save_u32(io,&product) || product!=(uint32_t)o->product ||
       !qa_source_save_u32(io,&seat) || seat!=o->options.seat ||
       !qa_source_save_u32(io,&physical) || physical!=o->options.presentation_seat ||
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
