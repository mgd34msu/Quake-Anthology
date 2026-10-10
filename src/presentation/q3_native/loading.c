/* CG loading information, id Software 1999-2005, GPL-2.0-or-later. */
#include "qa/game_type.h"
#include "loading_internal.h"

bool q3nl_fail(qa_error *e,qa_status status,const char *text)
{ qa_error_set(e,status,0,"%s",text); return false; }
static bool basis(const q3n_loading_options *o,qa_q3_product *product,bool retained,qa_error *e)
{
    if(o && o->compiled_source) {
        q3n_compiled_source_view source; qa_q3_presentation_binding backend; q3n_loading_media media;
        if(!product || o->client || o->reader || o->remote_client || o->remote_source || !o->compiled_cvars ||
           !o->assets || !o->presentation || !o->media || !o->ui || !o->update_screen ||
           !(retained?q3n_compiled_source_checkpoint_read(o->compiled_source,&source,e):
                q3n_compiled_source_read(o->compiled_source,&source,e)) ||
           !qa_q3_presentation_binding_read(o->presentation,&backend,e) ||
           !q3n_media_loading_read(o->media,&media,e))return false;
        if(source.basis.application!=o->application || source.basis.seat!=o->seat ||
           source.basis.assets!=o->assets || backend.options.assets!=o->assets ||
           backend.options.seat!=o->presentation_seat || media.assets!=o->assets ||
           media.product!=source.basis.product || media.compiled_source!=o->compiled_source ||
           !(retained?q3n_compiled_source_checkpoint_current(&source):q3n_compiled_source_current(&source)))
            return q3nl_fail(e,QA_ERROR_ARGUMENT,"Compiled loading requires its actual reached CLIENT and physical renderer seat");
        *product=source.basis.product; return true;
    }
    if(o && o->remote_client) {
        qa_native_q3_remote_client_basis client; q3n_remote_source_view source;
        qa_q3_presentation_binding backend; q3n_loading_media media;
        if(!product || !o->application || o->client || o->reader || !o->remote_source || !o->assets ||
           !o->presentation || !o->media || !o->ui || !o->update_screen ||
           !qa_native_q3_remote_client_basis_read(o->remote_client,&client,e) ||
           !q3n_remote_source_read(o->remote_source,&source,e) ||
           !qa_q3_presentation_binding_read(o->presentation,&backend,e) ||
           !q3n_media_loading_read(o->media,&media,e))return false;
        if(client.application!=o->application || client.client.seat!=o->seat ||
           source.basis.client.service_owner!=client.client.service_owner ||
           source.basis.client.frontend_lifetime!=client.client.frontend_lifetime ||
           !qa_net_client_id_equal(source.basis.connection,client.connection) ||
           backend.options.assets!=o->assets || backend.options.seat!=o->presentation_seat ||
           media.assets!=o->assets || media.product!=client.product || media.remote_source!=o->remote_source)
            return q3nl_fail(e,QA_ERROR_ARGUMENT,"Remote loading requires its actual CLIENT, reached source and physical renderer seat");
        *product=client.product; return true;
    }
    qa_native_q3_client_basis client; qa_native_q3_wire_basis wire;
    qa_q3_presentation_binding backend;
    q3n_loading_media media;
    const qa_native_q3_client_services *services=o?qa_native_q3_client_services_read(o->client):NULL;
    if(!o || o->remote_source || o->compiled_source || !o->application || !o->client || !o->reader || !o->assets || !o->presentation || !o->media ||
       !o->ui || !o->update_screen || !services || services->wire_reader!=o->reader ||
       !qa_native_q3_client_basis_read(o->client,&client,e) ||
       !qa_native_q3_wire_reader_basis(o->reader,&wire,e) ||
       !qa_q3_presentation_binding_read(o->presentation,&backend,e) ||
       !q3n_media_loading_read(o->media,&media,e))return false;
    if(client.application!=o->application || client.seat!=o->seat || wire.application!=client.application ||
       wire.session!=client.session || wire.source_game!=client.source_game || wire.source_owner!=client.source_owner ||
       wire.receiver!=client.receiver || wire.product!=client.product || wire.seat!=client.seat ||
       wire.physical_client!=client.physical_client || wire.publication_generation!=client.publication_generation ||
       wire.map_revision!=client.map_revision || !qa_actor_id_equal(wire.actor,client.viewing_actor) ||
       backend.options.assets!=o->assets || backend.options.seat!=o->presentation_seat ||
       media.assets!=o->assets || media.product!=client.product)
        return q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading owner requires its actual source, reached reader and physical renderer seat");
    *product=client.product; return true;
}
bool q3nl_basis(const q3n_loading_options *o,qa_q3_product *product,qa_error *e)
{ return basis(o,product,false,e); }
bool q3nl_checkpoint_basis(const q3n_loading_options *o,qa_q3_product *product,qa_error *e)
{ return basis(o,product,true,e); }
bool q3n_loading_create_restored(const q3n_loading_options *options,q3n_loading **out,qa_error *e)
{
    qa_q3_product product;
    if(!out || *out || !q3nl_checkpoint_basis(options,&product,e))return false;
    q3n_loading *o=calloc(1,sizeof(*o));
    if(!o)return q3nl_fail(e,QA_ERROR_MEMORY,"Allocating native Q3 loading information");
    o->options=*options; o->product=product;
    const qa_native_q3_client_services *local=qa_native_q3_client_services_read(options->client);
    const qa_native_q3_remote_client_services *remote=qa_native_q3_remote_client_services_read(options->remote_client);
    o->cvars=options->compiled_source?options->compiled_cvars:remote?remote->basis.client.cvars:local?local->client.cvars:NULL;
    o->sv_running=qa_cvars_resolve(o->cvars,"sv_running");
    *out=o; return true;
}
bool q3n_loading_create(const q3n_loading_options *options,q3n_loading **out,qa_error *e)
{
    return options && !options->remote_client && !options->compiled_source ? q3n_loading_create_restored(options,out,e) :
        q3nl_fail(e,QA_ERROR_ARGUMENT,"Local loading requires its actual local CLIENT services");
}
bool q3n_loading_create_remote(const q3n_loading_options *options,q3n_loading **out,qa_error *e)
{
    return options && options->remote_client && !options->compiled_source ? q3n_loading_create_restored(options,out,e) :
        q3nl_fail(e,QA_ERROR_ARGUMENT,"Remote loading requires its actual remote CLIENT services");
}
bool q3n_loading_create_compiled(const q3n_loading_options *options,q3n_loading **out,qa_error *e)
{
    return options && options->compiled_source ? q3n_loading_create_restored(options,out,e) :
        q3nl_fail(e,QA_ERROR_ARGUMENT,"Compiled loading requires its actual compiled CLIENT source");
}
bool q3n_loading_idle(const q3n_loading *o) { return o && !o->busy && !o->painting; }
const char *q3n_loading_text(const q3n_loading *o) { return o ? o->state.text : NULL; }
void q3n_loading_destroy(q3n_loading *o) { if(q3n_loading_idle(o))free(o); }
static bool current(q3n_loading *o,const q3n_frame *f,qa_error *e)
{
    if(o && o->options.compiled_source) {
        qa_q3_product product;
        if(!f || !f->compiled || !q3nl_basis(&o->options,&product,e) || product!=o->product)return false;
        const q3n_compiled_source_basis *b=&f->compiled->source.basis;
        return f->compiled->source.owner==o->options.compiled_source && f->application==o->options.application &&
            f->assets==o->options.assets && f->presentation==o->options.presentation && f->media==o->options.media &&
            f->seat==o->options.seat && f->physical_presentation_seat==o->options.presentation_seat &&
            (f->compiled->stage==Q3N_COMPILED_INITIALIZATION || f->compiled->stage==Q3N_COMPILED_AWAITING_SNAPSHOT ||
                f->compiled->stage==Q3N_COMPILED_LOADING_INFORMATION) &&
            (!o->busy || (o->active_frame==f && o->active_sequence==b->reached_command)) && q3n_frame_current(f) ? true :
            q3nl_fail(e,QA_ERROR_ARGUMENT,"Compiled loading left its actual constructor or information scope");
    }
    if(o && o->options.remote_client) {
        qa_q3_product product; qa_native_q3_remote_client_basis basis; q3n_remote_source_view source;
        if(!f || !f->remote || !q3nl_basis(&o->options,&product,e) || product!=o->product ||
           !qa_native_q3_remote_client_basis_read(o->options.remote_client,&basis,e) ||
           !q3n_remote_source_read(o->options.remote_source,&source,e))return false;
        return f->application==o->options.application && !f->reader && !f->client_service &&
            f->remote->client==o->options.remote_client && f->remote->source.owner==o->options.remote_source &&
            f->assets==o->options.assets && f->presentation==o->options.presentation && f->media==o->options.media &&
            f->seat==o->options.seat && f->physical_presentation_seat==o->options.presentation_seat &&
            f->viewing_client==basis.physical_client && qa_actor_id_equal(f->viewing_actor,f->remote->source.publication.viewer) &&
            (!o->busy || (o->active_frame==f && o->active_sequence==source.reached_command)) && q3n_frame_current(f) ? true :
            q3nl_fail(e,QA_ERROR_ARGUMENT,"Remote loading left its actual constructor cut or reached gamestate");
    }
    qa_q3_product product; qa_native_q3_client_basis basis; qa_native_q3_wire_publication publication;
    if(!o || !f || f->remote || f->compiled || !q3nl_basis(&o->options,&product,e) || product!=o->product ||
       !qa_native_q3_client_basis_read(o->options.client,&basis,e) ||
       !qa_native_q3_wire_reader_publication(o->options.reader,&publication,e))return false;
    return f->application==o->options.application && f->client_service==o->options.client &&
        f->reader==o->options.reader && f->assets==o->options.assets && f->presentation==o->options.presentation &&
        f->media==o->options.media && f->seat==o->options.seat &&
        f->physical_presentation_seat==o->options.presentation_seat && f->viewing_client==basis.physical_client &&
        qa_actor_id_equal(f->viewing_actor,basis.viewing_actor) && f->source.source_game==basis.source_game &&
        f->source.session==basis.session && f->source.source_owner==basis.source_owner &&
        f->source.content==basis.content && f->source.content_product==basis.content_product &&
        f->source.publication_generation==basis.publication_generation &&
        f->source.product==product && f->source.map_revision==basis.map_revision &&
        f->time==f->source.source_time_ms && publication.has_gamestate &&
        (!o->busy || (o->active_frame==f && o->active_sequence==publication.reached_command_sequence)) &&
        qa_application_native_q3_presentation_current(f->application,&f->source) ? true :
        q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading information left its actual constructor cut or reached gamestate");
}
static bool begin(q3n_loading *o,const q3n_frame *f,qa_error *e)
{
    qa_native_q3_wire_publication publication;
    if(!q3n_loading_idle(o) || !current(o,f,e))return false;
    if(f->compiled) {
        o->active_sequence=f->compiled->source.basis.reached_command;
    } else if(f->remote) {
        q3n_remote_source_view source;
        if(!q3n_remote_source_read(o->options.remote_source,&source,e))return false;
        o->active_sequence=source.reached_command;
    } else {
        if(!qa_native_q3_wire_reader_publication(o->options.reader,&publication,e))return false;
        o->active_sequence=publication.reached_command_sequence;
    }
    o->active_frame=f;
    o->busy=true; o->painted=false; return true;
}
static bool end(q3n_loading *o,bool ok)
{ o->active_frame=NULL; o->busy=o->painting=o->painted=false; return ok; }
static bool config(q3n_loading *o,const q3n_frame *f,uint32_t index,const char **text,qa_error *e)
{
    uint64_t revision;
    return current(o,f,e) && q3n_frame_configstring(f,index,text,&revision,e);
}
static bool shader(q3n_loading *o,const q3n_frame *f,const char *name,bool mipmap,int32_t *out,qa_error *e)
{ return current(o,f,e) && qa_q3_register_shader(o->options.assets,name,mipmap,out,e) && current(o,f,e); }
static bool loading_string(q3n_loading *o,const q3n_frame *f,const char *text,qa_error *e)
{
    snprintf(o->state.text,sizeof(o->state.text),"%s",text);
    if(!o->options.update_screen(o->options.context,o,f,e))return false;
    if(!o->painted)return q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading UpdateScreen did not paint the actual information frame");
    return current(o,f,e);
}
bool q3n_loading_string(q3n_loading *o,const q3n_frame *f,const char *text,qa_error *e)
{
    if(!text || !begin(o,f,e))return false;
    return end(o,loading_string(o,f,text,e));
}
bool q3n_loading_item(q3n_loading *o,const q3n_frame *f,uint32_t number,qa_error *e)
{
    if(!begin(o,f,e))return false;
    size_t count; const qa_q3_item *items=qa_q3_items(o->product,&count);
    if(!number || number>=count || !items[number].name)
        return end(o,q3nl_fail(e,QA_ERROR_ARGUMENT,"CG_LoadingItem requires its actual named source item"));
    const qa_q3_item *item=items+number; bool ok=true;
    if(item->icon && o->state.item_count<26) {
        int32_t handle;
        ok=shader(o,f,item->icon,false,&handle,e);
        if(ok)o->state.item_icons[o->state.item_count++]=handle;
    }
    return end(o,ok && loading_string(o,f,item->name,e));
}
static void clean(char *text)
{
    unsigned char *out=(unsigned char *)text;
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if(p[0]=='^' && p[1] && p[1]!='^')++p;
        else if(*p>=32 && *p<=126)*out++=*p;
    }
    *out=0;
}
static int32_t integer(const char *text)
{
    while(*text==' ' || (*text>='\t' && *text<='\r'))++text;
    bool negative=*text=='-'; if(*text=='-' || *text=='+')++text;
    uint32_t value=0; while(*text>='0' && *text<='9')value=value*10u+(uint32_t)(*text++-'0');
    if(negative)value=0u-value;
    int32_t result; memcpy(&result,&value,sizeof(result)); return result;
}
static bool info_text(const char *info,const char *key,char *out,size_t capacity,qa_error *e)
{
    char value[8192];
    if(!qa_q3_info_value(info,key,value,sizeof(value),e))return false;
    size_t length=strlen(value);
    if(length>=capacity)length=capacity-1;
    memcpy(out,value,length); out[length]=0; return true;
}
static bool info_integer(const char *info,const char *key,int32_t *out,qa_error *e)
{
    char value[8192];
    if(!qa_q3_info_value(info,key,value,sizeof(value),e))return false;
    *out=integer(value); return true;
}
bool q3n_loading_client(q3n_loading *o,const q3n_frame *f,uint32_t number,qa_error *e)
{
    if(number>=64 || !begin(o,f,e))return false;
    const char *info; char model[64],personality[64]; bool ok=config(o,f,544u+number,&info,e) &&
        info_text(info,"model",model,sizeof(model),e) && info_text(info,"n",personality,sizeof(personality),e);
    if(ok && o->state.player_count<16) {
        char *skin=strrchr(model,'/'); if(skin)*skin++=0; else skin="default";
        char path[sizeof(model) * 2 + sizeof("models/players/characters//icon_.tga")]; int32_t handle=0;
        if(*model) {
            snprintf(path,sizeof(path),"models/players/%s/icon_%s.tga",model,skin);
            ok=shader(o,f,path,false,&handle,e);
            if(ok && !handle) { snprintf(path,sizeof(path),"models/players/characters/%s/icon_%s.tga",model,skin);
                ok=shader(o,f,path,false,&handle,e); }
        }
        if(ok && !handle)ok=shader(o,f,"models/players/sarge/icon_default.tga",false,&handle,e);
        if(ok && handle)o->state.player_icons[o->state.player_count++]=handle;
    }
    if(ok)clean(personality);
    const char *server; int32_t mode;
    if(ok)ok=config(o,f,0,&server,e) && info_integer(server,"g_gametype",&mode,e);
    if(ok && mode==2) {
        char path[128]; int32_t sound;
        snprintf(path,sizeof(path),"sound/player/announce/%s.wav",personality);
        ok=qa_q3_register_sound(o->options.assets,path,true,&sound,e) && current(o,f,e);
    }
    return end(o,ok && loading_string(o,f,personality,e));
}

/* Authored UI proportional atlas. Lowercase uses the donor's uppercase image;
 * invalid bytes retain the previous draw advance in the UI profile. */
static const int16_t prop[69][3]={
    {0,0,8},{11,122,7},{154,181,14},{55,122,17},{79,122,18},{101,122,23},{153,122,18},{9,93,7},
    {207,122,8},{230,122,9},{177,122,18},{30,152,18},{85,181,7},{34,93,11},{110,181,6},{130,152,14},
    {22,64,17},{41,64,12},{58,64,17},{78,64,18},{98,64,19},{120,64,18},{141,64,18},{204,64,16},
    {162,64,17},{182,64,18},{59,181,7},{35,181,7},{203,152,14},{56,93,14},{228,152,14},{177,181,18},
    {28,122,22},{5,4,18},{27,4,18},{48,4,18},{69,4,17},{90,4,13},{106,4,13},{121,4,18},
    {143,4,17},{164,4,8},{175,4,16},{195,4,18},{216,4,12},{230,4,23},{6,34,18},{27,34,18},
    {48,34,18},{68,34,18},{90,34,17},{110,34,18},{130,34,14},{146,34,18},{166,34,19},{185,34,29},
    {215,34,18},{234,34,18},{5,64,14},{60,152,7},{106,151,13},{83,152,7},{128,122,17},{4,152,21},
    {134,181,5},{153,152,13},{11,181,5},{180,152,13},{79,93,17}
};
static const int16_t *metric(unsigned code)
{
    code&=127; if(code<32 || code==127)return NULL;
    if(code>=97 && code<=122)code-=32;
    return prop[code>=123?65+code-123:code-32];
}
typedef struct loading_draw {
    q3n_loading *owner; const q3n_frame *frame; qa_q3_presentation_binding backend;
    qa_ui_presentation typography; qa_ui_preferences preferences; qa_error *error;
    q3n_loading_media media;
    float sx,sy;
} loading_draw;
static bool picture(loading_draw *d,float x,float y,float w,float h,int32_t handle,qa_vec4 uv,bool pixels)
{
    if(!pixels) { x=(x * d->sx); y=(y * d->sy); w=(w * d->sx); h=(h * d->sy); }
    return qa_q3_presentation_picture(d->owner->options.presentation,handle,(qa_scene_rect_f){x,y,w,h},uv,d->error) &&
        current(d->owner,d->frame,d->error);
}
static bool alternate(loading_draw *d,const char *text)
{
    if(d->preferences.typeface==QA_UI_TYPEFACE_BOLD)
        return qa_utf8_valid((qa_bytes){(const uint8_t *)text,strlen(text)});
    for(const unsigned char *p=(const unsigned char *)text;*p;++p)if(*p>=128)
        return qa_utf8_valid((qa_bytes){(const uint8_t *)text,strlen(text)});
    return false;
}
static bool text(loading_draw *d,int32_t y,const char *value)
{
    if(alternate(d,value)) {
        qa_font_selection fonts=d->typography.fonts;
        if(d->preferences.typeface==QA_UI_TYPEFACE_STANDARD)fonts.primary=NULL;
        qa_font_layout_options options={.text={(const uint8_t *)value,strlen(value)},
            .scale=27*0.75f*d->typography.text_scale/8,.color={1,1,1,1},
            .color_codes=QA_FONT_COLOR_LITERAL,.force_color=true};
        qa_font_layout layout;
        if(!qa_font_layout_build(&fonts,&options,&d->backend.frame->storage,&layout,d->error))return false;
        qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)layout.glyphs;
        for(size_t row=0;row<layout.line_count;++row) {
            const qa_font_line *line=layout.lines+row;
            for(size_t i=0;i<line->glyph_count;++i) {
                qa_scene_rect_f *r=&glyphs[line->first_glyph+i].rect;
                r->x=(320+r->x-line->width*0.5f)*d->sx; r->y=((float)y+r->y)*d->sy;
                r->width*=d->sx; r->height*=d->sy;
            }
        }
        qa_font_draw_options draw={.seat=d->owner->options.presentation_seat,.target=d->backend.options.viewport,
            .space=QA_FONT_PIXELS,.shadow_offset=2*fminf(d->sx,d->sy)};
        return qa_font_draw_layout(d->backend.frame,&layout,&draw,d->error) && current(d->owner,d->frame,d->error);
    }
    int32_t width=0;
    for(const unsigned char *p=(const unsigned char *)value;*p;++p) { const int16_t *m=metric(*p); if(m)width+=m[2]+3; }
    width-=3; float size=(0.75f * d->preferences.text_scale);
    int32_t scaled=(int32_t)((float)width * size),x=320-scaled/2;
    int32_t handle=d->media.proportional;
    for(unsigned pass=0;pass<2;++pass) {
        qa_vec4 color=pass?(qa_vec4){1,1,1,1}:(qa_vec4){0,0,0,1};
        qa_q3_presentation_color(d->owner->options.presentation,&color);
        float ax=((float)(x+(pass?0:2)) * d->sx),ay=((float)(y+(pass?0:2)) * d->sy),aw=0;
        float gap=((3 * d->sx) * size),height=((27 * d->sy) * size);
        for(const unsigned char *p=(const unsigned char *)value;*p;++p) {
            unsigned code=*p&127u; const int16_t *m=metric(code);
            if(code==32)aw=((8 * d->sx) * size);
            else if(m) {
                aw=(((float)m[2] * d->sx) * size);
                if(!picture(d,ax,ay,aw,height,handle,(qa_vec4){(float)m[0]/256,(float)m[1]/256,
                    (float)(m[0]+m[2])/256,(float)(m[1]+27)/256},true))return false;
            }
            ax=(ax + (aw + gap));
        }
        qa_q3_presentation_color(d->owner->options.presentation,NULL);
    }
    return current(d->owner,d->frame,d->error);
}
static const char *game_name(qa_q3_product product,int32_t mode)
{
    switch(mode) {
    case 0:return "Free For All"; case 1:return "Tournament"; case 2:return "Single Player";
    case 3:return "Team Deathmatch"; case 4:return "Capture The Flag";
    case 5:if(product==QA_Q3_TEAM_ARENA)return "One Flag CTF"; break;
    case 6:if(product==QA_Q3_TEAM_ARENA)return "Overload"; break;
    case 7:if(product==QA_Q3_TEAM_ARENA)return "Harvester"; break;
    case QA_GAME_TYPE_CAMPAIGN:return "Campaign";
    case QA_GAME_TYPE_COOPERATIVE:return "Cooperative";
    }
    return "Unknown Gametype";
}
static bool draw_information(loading_draw *d)
{
    q3n_loading *o=d->owner; const q3n_frame *f=d->frame; const char *info,*system,*message,*motd;
    char map[8192],path[8208],buffer[1024],pure[64],cheats[64];
    int32_t game_type,time_limit,frag_limit,capture_limit;
    if(!config(o,f,0,&info,d->error) || !config(o,f,1,&system,d->error) || !config(o,f,3,&message,d->error) ||
       !config(o,f,4,&motd,d->error) || !qa_q3_info_value(info,"mapname",map,sizeof(map),d->error) ||
       !info_integer(info,"g_gametype",&game_type,d->error) ||
       !info_text(system,"sv_pure",pure,sizeof(pure),d->error) ||
       !info_text(system,"sv_cheats",cheats,sizeof(cheats),d->error) ||
       !info_integer(info,"timelimit",&time_limit,d->error) ||
       !info_integer(info,"fraglimit",&frag_limit,d->error) ||
       !info_integer(info,"capturelimit",&capture_limit,d->error))return false;
    snprintf(path,sizeof(path),"levelshots/%s.tga",map); int32_t levelshot,detail;
    if(!shader(o,f,path,false,&levelshot,d->error) ||
       (!levelshot && !shader(o,f,"menu/art/unknownmap",false,&levelshot,d->error)))return false;
    qa_q3_presentation_color(o->options.presentation,NULL);
    if(!picture(d,0,0,640,480,levelshot,(qa_vec4){0,0,1,1},false) ||
       !shader(o,f,"levelShotDetail",true,&detail,d->error) ||
       !picture(d,0,0,(float)d->backend.options.viewport.width,(float)d->backend.options.viewport.height,detail,
        (qa_vec4){0,0,2.5f,2},true))return false;
    for(uint32_t i=0;i<o->state.player_count;++i)
        if(!picture(d,(float)(16+i*78),284,64,64,o->state.player_icons[i],(qa_vec4){0,0,1,1},false))return false;
    for(uint32_t i=0;i<o->state.item_count;++i)
        if(!picture(d,(float)(16+i%13*48),i>=13?400:360,32,32,o->state.item_icons[i],(qa_vec4){0,0,1,1},false))return false;
    char loading[1100];
    if(o->state.text[0])snprintf(loading,sizeof(loading),"Loading... %s",o->state.text);
    else snprintf(loading,sizeof(loading),"Awaiting snapshot...");
    if(!text(d,96,loading))return false;
    const qa_cvar_view *running=qa_cvars_read(o->cvars,o->sv_running);
    int32_t y=148; char running_text[1024]; snprintf(running_text,sizeof(running_text),"%s",running?running->value:"");
    if(integer(running_text)==0) {
        if(!info_text(info,"sv_hostname",buffer,sizeof(buffer),d->error))return false;
        clean(buffer); if(!text(d,y,buffer))return false; y+=27;
        if(pure[0]=='1') { if(!text(d,y,"Pure Server"))return false; y+=27; }
        if(*motd) { if(!text(d,y,motd))return false; y+=27; }
        y+=10;
    }
    if(*message) { if(!text(d,y,message))return false; y+=27; }
    if(cheats[0]=='1') { if(!text(d,y,"CHEATS ARE ENABLED"))return false; y+=27; }
    if(!text(d,y,game_name(o->product,game_type)))return false;
    y+=27;
    int32_t limit=time_limit;
    if(limit) { snprintf(buffer,sizeof(buffer),"timelimit %i",limit); if(!text(d,y,buffer))return false; y+=27; }
    limit=!qa_game_type_is_objective(game_type)?frag_limit:capture_limit;
    if(limit) { snprintf(buffer,sizeof(buffer),"%s %i",!qa_game_type_is_objective(game_type)?"fraglimit":"capturelimit",limit); if(!text(d,y,buffer))return false; }
    return true;
}
bool q3n_loading_draw_information(q3n_loading *o,const q3n_frame *f,qa_error *e)
{
    if(!o || o->painting)return false;
    bool owned=!o->busy;
    if(owned ? !begin(o,f,e) : !current(o,f,e))return false;
    o->painting=true;
    loading_draw d={.owner=o,.frame=f,.error=e};
    bool ok=qa_q3_presentation_binding_read(o->options.presentation,&d.backend,e) && d.backend.frame &&
        d.backend.options.viewport.width && d.backend.options.viewport.height &&
        q3n_media_loading_read(o->options.media,&d.media,e) &&
        qa_ui_presentation_read(o->options.ui,&d.typography,e);
    if(ok) {
        if(f->compiled)d.preferences=f->preferences;
        else ok=qa_ui_preferences_read(qa_application_cvars(o->options.application),qa_application_ui_preference_handles(o->options.application),o->options.presentation_seat,&d.preferences,e);
    }
    if(ok && !d.media.initialized)
        ok=q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading information requires its real completed loading-font registration stage");
    if(ok && (d.typography.fonts.seat!=o->options.presentation_seat || d.typography.text_scale!=d.preferences.text_scale ||
       d.typography.color_mode!=d.preferences.color_mode))
        ok=q3nl_fail(e,QA_ERROR_ARGUMENT,"Loading typography requires its actual synchronized physical UI seat");
    if(ok) { d.sx=(float)d.backend.options.viewport.width/640; d.sy=(float)d.backend.options.viewport.height/480;
        ok=draw_information(&d) && current(o,f,e); }
    qa_q3_presentation_color(o->options.presentation,NULL);
    o->painting=false; if(ok)o->painted=true;
    return owned?end(o,ok):ok;
}
