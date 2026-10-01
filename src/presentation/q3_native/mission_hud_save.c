#include "mission_hud_internal.h"
#include "qa/font_save.h"

static bool number(qa_source_save_io *io,int *v) { int32_t n=*v; if(!qa_source_save_i32(io,&n))return false; *v=n; return true; }
static bool flag(qa_source_save_io *io,qboolean *v) { bool b=*v!=0; if(!qa_source_save_bool(io,&b))return false; *v=b?qtrue:qfalse; return true; }
static bool vector(qa_source_save_io *io,float *v,unsigned n) { for(unsigned i=0;i<n;++i)if(!q3nh_float(io,v+i))return false; return true; }
static bool shader(qa_source_save_io *io,int *v,q3n_mission_hud *o)
{ return number(io,v)&&q3nh_handle(o->options.assets,*v,Q3P_SHADER); }
static bool sound(qa_source_save_io *io,int *v,q3n_mission_hud *o)
{ return number(io,v)&&q3nh_handle(o->options.assets,*v,Q3P_SOUND); }
static bool font(qa_source_save_io *io,q3n_mission_hud *o,fontInfo_t *font,unsigned slot)
{
    uint64_t ordinal=UINT64_MAX; size_t count=qa_font_library_record_count(o->options.fonts);
    if(io->direction==QA_SOURCE_SAVE_WRITE&&o->font_holders[slot]) {
        for(size_t i=0;i<count;++i)if(qa_font_library_record_at(o->options.fonts,i)==o->font_holders[slot]) { ordinal=i; break; }
        if(ordinal==UINT64_MAX)return false;
    }
    if(!qa_source_save_u64(io,&ordinal))return false;
    if(ordinal==UINT64_MAX) {
        fontInfo_t zero={0}; if(io->direction==QA_SOURCE_SAVE_WRITE&&memcmp(font,&zero,sizeof(zero)))return false;
        if(io->direction==QA_SOURCE_SAVE_READ) { *font=zero; o->font_holders[slot]=NULL; } return true;
    }
    if(ordinal>=count)return false;
    if(io->direction==QA_SOURCE_SAVE_READ)o->font_holders[slot]=qa_font_library_record_at(o->options.fonts,(size_t)ordinal);
    qa_font_info info; if(!qa_font_describe(o->font_holders[slot],&info)||info.kind!=QA_FONT_Q3)return false;
    uint8_t bytes[QA_Q3_FONT_RECORD_BYTES]; qa_q3_font_record record={0};
    if(io->direction==QA_SOURCE_SAVE_WRITE) { q3nm_font_record(font,&record); if(!qa_q3_font_record_encode(&record,bytes,io->error))return false; }
    if(!qa_source_save_bytes(io,bytes,sizeof(bytes))||!qa_q3_font_record_decode((qa_bytes){bytes,sizeof(bytes)},&record,io->error))return false;
    for(unsigned i=0;i<256;++i) {
        const qa_q3_glyph_record *g=&record.glyphs[i]; qa_font_glyph source;
        if(!q3nh_handle(o->options.assets,g->handle,Q3P_SHADER)||!qa_font_find_glyph(o->font_holders[slot],i,&source)||
            source.width!=g->image_width*record.glyph_scale||source.height!=g->image_height*record.glyph_scale||
            source.advance!=g->x_skip*record.glyph_scale||source.bearing_y!=g->top*record.glyph_scale||
            source.uv.x!=g->s||source.uv.y!=g->t||source.uv.z!=g->s2||source.uv.w!=g->t2||
            ((source.image!=NULL)!=(g->handle!=0)))return false;
        if(source.image) {
            const qa_material *material=o->options.assets->shaders[g->handle-1]; bool retained=false;
            for(size_t stage=0;stage<material->stage_count&&!retained;++stage)
                for(size_t image=0;image<material->stages[stage].image_count;++image)
                    if(material->stages[stage].images[image]==source.image) { retained=true; break; }
            if(!retained)return false;
        }
    }
    if(io->direction==QA_SOURCE_SAVE_READ)q3nm_font_import(&record,font); return true;
}
static bool assets(qa_source_save_io *io,q3n_mission_hud *o)
{
    cachedAssets_t *a=&o->display.Assets; q3menu_context *c=o->menus;
    if(!q3menu_save_string(io,c,&a->fontStr)||!q3menu_save_string(io,c,&a->cursorStr)||!q3menu_save_string(io,c,&a->gradientStr)||
        !font(io,o,&a->smallFont,0)||!font(io,o,&a->textFont,1)||!font(io,o,&a->bigFont,2)||
        !shader(io,&a->cursor,o)||!shader(io,&a->gradientBar,o)||!shader(io,&a->scrollBarArrowUp,o)||
        !shader(io,&a->scrollBarArrowDown,o)||!shader(io,&a->scrollBarArrowLeft,o)||!shader(io,&a->scrollBarArrowRight,o)||
        !shader(io,&a->scrollBar,o)||!shader(io,&a->scrollBarThumb,o)||!shader(io,&a->buttonMiddle,o)||
        !shader(io,&a->buttonInside,o)||!shader(io,&a->solidBox,o)||!shader(io,&a->sliderBar,o)||!shader(io,&a->sliderThumb,o)||
        !sound(io,&a->menuEnterSound,o)||!sound(io,&a->menuExitSound,o)||!sound(io,&a->menuBuzzSound,o)||!sound(io,&a->itemFocusSound,o)||
        !q3nh_float(io,&a->fadeClamp)||!number(io,&a->fadeCycle)||!q3nh_float(io,&a->fadeAmount)||!q3nh_float(io,&a->shadowX)||
        !q3nh_float(io,&a->shadowY)||!vector(io,a->shadowColor,4)||!q3nh_float(io,&a->shadowFadeClamp)||!flag(io,&a->fontRegistered)||
        !shader(io,&a->fxBasePic,o))return false;
    for(unsigned i=0;i<7;++i)if(!shader(io,&a->fxPic[i],o))return false;
    for(unsigned i=0;i<NUM_CROSSHAIRS;++i)if(!shader(io,&a->crosshairShader[i],o))return false;
    return true;
}
static bool blob(qa_source_save_io *io,qa_buffer *b)
{
    size_t size=b->size; if(!qa_source_save_count(io,&size,SIZE_MAX))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { if(size>io->input.size-io->offset)return false; b->data=size?malloc(size):NULL; b->size=size; if(size&&!b->data)return false; }
    return qa_source_save_bytes(io,b->data,size);
}
static bool fields(qa_source_save_io *io,q3n_mission_hud *o)
{
    uint8_t magic[4]={'Q','3','M','H'}; uint32_t version=1,seat=o->options.seat; bool shared_weapon_hud=o->options.shared_weapon_hud;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,"Q3MH",4)||!qa_source_save_u32(io,&version)||version!=1||
        !qa_source_save_u32(io,&seat)||seat!=o->options.seat||!qa_source_save_bool(io,&shared_weapon_hud)||shared_weapon_hud!=o->options.shared_weapon_hud)return false;
    qa_buffer menus={0}; bool ok=true;
    if(io->direction==QA_SOURCE_SAVE_WRITE)ok=q3menu_checkpoint(o->menus,&menus,io->error);
    if(ok)ok=blob(io,&menus);
    if(ok&&io->direction==QA_SOURCE_SAVE_READ) { q3menu_context *restored=NULL; ok=q3menu_restore(o->menus,(qa_bytes){menus.data,menus.size},&restored,io->error);
        if(ok) { o->menus=restored; restored->owner=o; restored->display=&o->display; } }
    qa_buffer_free(&menus); if(!ok||!assets(io,o))return false;
    if(!qa_source_save_bytes(io,o->system_chat,256)||!qa_source_save_bytes(io,o->team_chat,sizeof(o->team_chat))||
        !memchr(o->system_chat,0,256)||!memchr(o->team_chat[0],0,256)||!memchr(o->team_chat[1],0,256)||
        !qa_source_save_i32(io,&o->selected_score)||o->selected_score< -1||o->selected_score>=64||
        !qa_source_save_i32(io,&o->cursor_x)||o->cursor_x<0||o->cursor_x>640||
        !qa_source_save_i32(io,&o->cursor_y)||o->cursor_y<0||o->cursor_y>480||
        !qa_source_save_i32(io,&o->active_cursor)||!q3nh_handle(o->options.assets,o->active_cursor,Q3P_SHADER)||
        !qa_source_save_i32(io,&o->event_handling)||!qa_source_save_i32(io,&o->voice_time)||
        !qa_source_save_i32(io,&o->order_time)||!qa_source_save_i32(io,&o->current_order)||!qa_source_save_bool(io,&o->order_pending)||
        !qa_source_save_bool(io,&o->loaded)||!qa_source_save_bool(io,&o->text_policy_active)||!qa_source_save_i32(io,&o->scoreboard_menu)||
        o->scoreboard_menu< -1||o->scoreboard_menu>=o->menus->menu_count||!qa_source_save_i32(io,&o->captured_menu)||
        o->captured_menu< -1||o->captured_menu>=o->menus->menu_count||!qa_source_save_i32(io,&o->spectator_offset)||
        o->spectator_offset<0||o->spectator_offset>1023||!qa_source_save_i32(io,&o->spectator_time)||
        !qa_source_save_i32(io,&o->spectator_paint_x)||!qa_source_save_i32(io,&o->spectator_paint_x2)||
        !q3nh_float(io,&o->spectator_width)||!qa_source_save_i32(io,&o->spectator_length)||o->spectator_length<0||o->spectator_length>1023)return false;
    displayContextDef_t *d=&o->display;
    return q3nh_float(io,&d->xscale)&&q3nh_float(io,&d->yscale)&&q3nh_float(io,&d->bias)&&number(io,&d->realTime)&&
        number(io,&d->frameTime)&&number(io,&d->cursorx)&&number(io,&d->cursory)&&flag(io,&d->debug)&&
        shader(io,&d->whiteShader,o)&&shader(io,&d->gradientImage,o)&&shader(io,&d->cursor,o)&&q3nh_float(io,&d->FPS)&&
        qa_source_save_i32(io,&d->glconfig.vidWidth)&&qa_source_save_i32(io,&d->glconfig.vidHeight)&&q3nh_float(io,&d->glconfig.windowAspect);
}
bool q3n_mission_hud_checkpoint(const q3n_mission_hud *borrowed,qa_buffer *out,qa_error *e)
{
    if(!borrowed||!out||out->data||out->size||!q3nh_capture(borrowed->options.assets,borrowed->busy,e))return false;
    q3n_mission_hud *o=(q3n_mission_hud *)borrowed; o->busy=true; q3n_mission_hud copy=*o;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,e)&&fields(&io,&copy)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); o->busy=false;
    if(!ok&&e&&e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Mission HUD continuation has invalid authored menu or registered media references"); return ok;
}
bool q3n_mission_hud_restore(q3n_mission_hud *o,qa_bytes bytes,qa_error *e)
{
    if(!o||!q3nh_capture(o->options.assets,o->busy,e))return false;
    o->busy=true; q3n_mission_hud candidate=*o; q3menu_context *old=o->menus;
    qa_source_save_io io={0}; bool ok=qa_source_save_reader(&io,NULL,bytes,e)&&fields(&io,&candidate)&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(ok) { *o=candidate; o->menus->owner=o; o->menus->display=&o->display; o->menus->scripts.context=o; q3menu_destroy(old); }
    else if(candidate.menus!=old)q3menu_destroy(candidate.menus);
    o->busy=false; if(!ok&&e&&e->code==QA_OK)q3ne_fail(e,QA_ERROR_FORMAT,"Saved mission HUD continuation is inconsistent with its imported owners"); return ok;
}
