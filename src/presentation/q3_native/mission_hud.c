/* Team Arena cg_main/cg_newdraw, id Software 1999-2005, GPL-2.0-or-later. */
#include "qa/game_type.h"
#include "mission_hud_internal.h"
#include "qa/font_save.h"
#include "qa/text.h"

q3n_mission_hud *q3nm_active(void) { return q3menu_active()->owner; }
void q3nm_result(bool result)
{ q3menu_context *context=q3menu_active(); q3n_mission_hud *o=context->owner;
    if(!result || (o->frame&&!q3nm_current(o,o->frame,context->error)))context->failed=true; }
static const qa_application_q3_client_context *recipient(const q3n_mission_hud *o)
{ return o->native_recipient ? o->native_recipient : &o->options.recipient; }
bool q3nm_current(q3n_mission_hud *o, const q3n_frame *f, qa_error *e)
{
    if(o&&o->options.compiled_source) {
        if(o->rebind||!f||!f->compiled||f->remote||f->compiled->source.owner!=o->options.compiled_source||
           f->compiled->source.basis.product!=QA_Q3_TEAM_ARENA||
           f->compiled->source.basis.content!=o->options.content||f->application!=o->options.application||
           f->assets!=o->options.assets||f->presentation!=o->options.presentation||f->seat!=o->options.seat||
           !q3n_frame_current(f)||!o->options.compiled_current(o->options.context,f,
                o->options.compiled_cvars,&o->options.compiled_context))
            return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission HUD left its actual compiled CLIENT source and command domain");
        return true;
    }
    if(o&&o->options.remote_client) {
        const qa_application_q3_client_context *expected=recipient(o);
        const qa_application_q3_client_context *actual=f&&f->remote?&f->remote->source.basis.client:NULL;
        if(!f||!f->remote||f->compiled||!actual||f->application!=o->options.application||
           f->remote->client!=o->options.remote_client||f->remote->source.owner!=o->options.remote_source||
           f->remote->source.basis.product!=QA_Q3_TEAM_ARENA||f->remote->source.basis.content!=o->options.content||
           f->assets!=o->options.assets||f->presentation!=o->options.presentation||f->seat!=o->options.seat||
           actual->session!=expected->session||actual->receiver!=expected->receiver||
           actual->source_owner!=expected->source_owner||actual->service_owner!=expected->service_owner||
           actual->frontend_lifetime!=expected->frontend_lifetime||actual->console!=expected->console||
           actual->cvars!=expected->cvars||actual->source_cvars!=expected->source_cvars||
           actual->seat!=expected->seat||actual->source_client!=expected->source_client||
           !qa_actor_id_equal(actual->source_actor,expected->source_actor)||
           f->viewing_client!=expected->source_client||!qa_actor_id_equal(f->viewing_actor,expected->source_actor)||
           !q3n_frame_current(f))
            return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission HUD left its actual remote CLIENT or reached source");
        return true;
    }
    qa_application_q3_client_context context;
    if (!o || !f || f->remote || f->compiled || f->application != o->options.application || f->source.source_game != o->source_game ||
        f->source.product != QA_Q3_TEAM_ARENA || f->source.publication_generation != o->publication_generation ||
        f->source.map_revision != o->map_revision || f->assets != o->options.assets || f->presentation != o->options.presentation || f->seat != o->options.seat ||
        f->viewing_client != recipient(o)->source_client ||
        !qa_actor_id_equal(f->viewing_actor, recipient(o)->source_actor) ||
        !qa_application_native_q3_presentation_current(o->options.application, &f->source) ||
        !qa_native_q3_client_context_read(o->options.client, &context, e) ||
        context.session != recipient(o)->session || context.receiver != recipient(o)->receiver ||
        context.source_owner != recipient(o)->source_owner || context.seat != o->options.seat ||
        context.source_client != recipient(o)->source_client || !qa_actor_id_equal(context.source_actor,recipient(o)->source_actor) ||
        context.service_owner != recipient(o)->service_owner || context.console != recipient(o)->console ||
        context.frontend_lifetime != recipient(o)->frontend_lifetime || context.cvars != recipient(o)->cvars ||
        context.source_cvars != recipient(o)->source_cvars || !context.native_source ||
        f->client_service != o->options.client || f->reader!=o->options.reader || !qa_native_q3_wire_reader_current(o->options.reader))
        return q3ne_fail(e, QA_ERROR_ARGUMENT, "Mission HUD left its private CGAME recipient or native source");
    return true;
}
bool q3nm_cvar(q3n_mission_hud *o,const char *name,qa_native_q3_client_cvar *out,qa_error *e)
{
    if(o->options.compiled_source)return q3nm_current(o,o->frame,e)&&
        o->options.compiled_cvar_read(o->options.context,name,out,e)&&q3nm_current(o,o->frame,e);
    return o->options.remote_client?qa_native_q3_remote_client_cvar_read(o->options.remote_client,qa_native_q3_cvar_id_for_symbol(name),out,e):
        qa_native_q3_client_cvar_read(o->options.client,qa_native_q3_cvar_id_for_symbol(name),out,e);
}
bool q3nm_integer_set(q3n_mission_hud *o,const char *name,int32_t value,qa_error *e)
{
    if(o->options.compiled_source) { char text[32]; snprintf(text,sizeof(text),"%d",value);
        return q3nm_current(o,o->frame,e)&&qa_cvars_set(o->options.compiled_cvars,name,text,true,e)&&q3nm_current(o,o->frame,e); }
    return o->options.remote_client?qa_native_q3_remote_client_cvar_integer(o->options.remote_client,qa_native_q3_cvar_id_for_symbol(name),value,e):
        qa_native_q3_client_cvar_integer(o->options.client,qa_native_q3_cvar_id_for_symbol(name),value,e);
}
bool q3nm_console(q3n_mission_hud *o,const char *text,qa_error *e)
{
    if(o->options.compiled_source)return q3nm_current(o,o->frame,e)&&
        o->options.compiled_console(o->options.context,o->frame,text,e)&&q3nm_current(o,o->frame,e);
    return o->options.remote_client?qa_native_q3_remote_client_console(o->options.remote_client,text,e):
        qa_native_q3_client_console(o->options.client,text,e);
}
qa_cvars *q3nm_registry(const q3n_mission_hud *o)
{ return o->options.compiled_source?o->options.compiled_cvars:recipient(o)->cvars; }
const qa_q3_player *q3nm_player(const q3n_mission_hud *o)
{ return q3n_frame_snapshot_player(o->frame); }
const qa_q3_player *q3nm_require_player(q3n_mission_hud *o)
{
    const qa_q3_player *p=q3nm_player(o);
    if(!p) q3nm_result(q3ne_fail(o->menus->error,QA_ERROR_ARGUMENT,
        "Mission HUD player operation requires its actual reached snapshot"));
    return p;
}
int32_t q3nm_integer(q3n_mission_hud *o, const char *symbol)
{ qa_native_q3_client_cvar v = {0}; q3nm_result(q3nm_cvar(o,symbol,&v,o->menus->error)); return v.integer; }
float q3nm_number(q3n_mission_hud *o, const char *symbol)
{ qa_native_q3_client_cvar v = {0}; q3nm_result(q3nm_cvar(o,symbol,&v,o->menus->error)); return v.number; }
bool q3nm_set(q3n_mission_hud *o, const char *name, const char *value)
{ return !o->menus->failed && q3nm_current(o,o->frame,o->menus->error) &&
    qa_cvars_set(q3nm_registry(o), name, value, true, o->menus->error) && q3nm_current(o, o->frame, o->menus->error); }
bool q3nm_begin(q3n_mission_hud *o, const q3n_frame *f, qa_error *e, q3menu_context **previous)
{
    if (!o || o->busy || !o->hud || !q3nm_current(o, f, e)) return false;
    o->busy = true; o->frame = f; o->commands = f->server_commands ? q3n_server_commands_state(f->server_commands) : NULL;
    o->menus->error = e; o->menus->failed = false; *previous = q3menu_enter(o->menus);
    o->settings.draw_status = q3nm_integer(o, "cg_drawStatus") != 0;
    o->settings.draw_icons = q3nm_integer(o, "cg_drawIcons") != 0;
    o->settings.draw_3d_icons = q3nm_integer(o, "cg_draw3dIcons") != 0;
    o->draw = (q3n_hud_draw){.owner=o->hud, .frame=f, .settings=&o->settings,
        .commands=f->server_commands, .player=f->player_state, .viewport=f->presentation->options.viewport, .error=e};
    q3nh_anchor(&o->draw,320,240);
    o->display.xscale = (float)o->draw.viewport.width / 640;
    o->display.yscale = (float)o->draw.viewport.height / 480;
    o->display.whiteShader = q3n_media_read(f->media)->graphics[Q3N_G_WHITE];
    return true;
}
bool q3nm_end(q3n_mission_hud *o, q3menu_context *previous, bool result)
{
    result = result && !o->menus->failed && q3nm_current(o, o->frame, o->menus->error);
    if(!result&&o->menus->error&&o->menus->error->code==QA_OK)q3ne_fail(o->menus->error,QA_ERROR_FORMAT,"Mission HUD source operation failed");
    o->frame = NULL; o->commands = NULL; o->menus->error = NULL; o->busy = false; q3menu_leave(previous); return result;
}
bool q3nm_preferences(q3n_mission_hud *o)
{
    if(!q3nh_preferences(&o->draw))return false;
    bool requested=o->frame->preferences.text_scale!=1||o->frame->preferences.typeface!=QA_UI_TYPEFACE_STANDARD;
    if(requested||o->text_policy_active)for(uint32_t i=0;i<o->menus->allocation_count;++i)
        if(o->menus->allocations[i].kind==Q3MENU_ITEM) {
            itemDef_t *item=(void *)(o->menus->allocation.bytes+o->menus->allocations[i].offset); item->textRect.w=0;
        }
    o->text_policy_active=requested; return true;
}
static void print(const char *format, ...)
{ q3n_mission_hud *o=q3nm_active(); char text[4096]; va_list a; va_start(a,format); vsnprintf(text,sizeof(text),format,a); va_end(a); o->options.print(o->options.context,text); q3nm_result(q3nm_current(o,o->frame,o->menus->error)); }
static int shader(const char *path)
{ q3n_mission_hud *o=q3nm_active(); int h=0; if(o->menus->failed)return 0; q3nm_result(qa_q3_register_shader(o->options.assets,path,false,&h,o->menus->error)&&q3nm_current(o,o->frame,o->menus->error)); return h; }
static int model(const char *path)
{ q3n_mission_hud *o=q3nm_active(); int h=0; if(o->menus->failed)return 0; q3nm_result(qa_q3_register_model(o->options.assets,path,&h,o->menus->error)&&q3nm_current(o,o->frame,o->menus->error)); return h; }
static int sound(const char *path,qboolean compressed)
{ q3n_mission_hud *o=q3nm_active(); int h=0; if(o->menus->failed)return 0; q3nm_result(qa_q3_register_sound(o->options.assets,path,compressed!=0,&h,o->menus->error)&&q3nm_current(o,o->frame,o->menus->error)); return h; }
static void color(const float c[4]) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed)q3nm_result(q3nh_color(&o->draw,c)); }
static void picture(float x,float y,float w,float h,int asset) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed)q3nm_result(q3nh_picture(&o->draw,x,y,w,h,asset)); }
static void stretch(float x,float y,float w,float h,float s,float t,float s2,float t2,int asset)
{ q3n_mission_hud *o=q3nm_active(); if(o->menus->failed)return;
    qa_scene_rect_f r=q3nh_rect(&o->draw,(qa_scene_rect_f){x/o->display.xscale,y/o->display.yscale,w/o->display.xscale,h/o->display.yscale});
    q3nm_result(q3nh_pixels(&o->draw,r.x,r.y,r.width,r.height,asset,(qa_scene_vec4){s,t,s2,t2})); }
static void fill(float x,float y,float w,float h,const float c[4]) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed)q3nm_result(q3nh_fill(&o->draw,x,y,w,h,c)); }
static void sides(float x,float y,float w,float h,float size) { picture(x,y,size,h,q3nm_active()->display.whiteShader); picture(x+w-size,y,size,h,q3nm_active()->display.whiteShader); }
static void top_bottom(float x,float y,float w,float h,float size) { picture(x,y,w,size,q3nm_active()->display.whiteShader); picture(x,y+h-size,w,size,q3nm_active()->display.whiteShader); }
static void rectangle(float x,float y,float w,float h,float size,const float c[4]) { color(c); sides(x,y,w,h,size); top_bottom(x,y,w,h,size); color(NULL); }
static void model_bounds(int h,float mins[3],float maxs[3])
{ q3n_mission_hud *o=q3nm_active(); qa_bounds b={0}; if(!o->menus->failed)q3nm_result(qa_q3_presentation_model_bounds(o->options.assets,h,&b,o->menus->error)); q3ne_store(mins,b.mins); q3ne_store(maxs,b.maxs); }
static void clear_scene(void) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed)q3nm_result(qa_q3_presentation_clear(o->frame->presentation,o->menus->error)); }
static void entity(const refEntity_t *source)
{
    q3n_mission_hud *o=q3nm_active(); if(o->menus->failed)return; qa_q3_ref_entity r={.kind=QA_Q3_REF_MODEL,.model=source->hModel,.flags=source->renderfx,
        .origin=q3ne_array(source->origin),.old_origin=q3ne_array(source->oldorigin),.lighting_origin=q3ne_array(source->lightingOrigin)};
    memcpy(r.color,source->shaderRGBA,sizeof(r.color));
    for(unsigned i=0;i<3;++i)r.axis[i]=q3ne_array(source->axis[i]);
    q3nm_result(qa_q3_presentation_entity(o->frame->presentation,&r,o->menus->error));
}
static void render(const refdef_t *source)
{
    q3n_mission_hud *o=q3nm_active(); if(o->menus->failed)return; qa_q3_refdef r={.x=source->x,.y=source->y,.width=source->width,.height=source->height,
        .fov_x=source->fov_x,.fov_y=source->fov_y,.time=source->time,.flags=source->rdflags};
    if(o->frame->preferences.hud_scale!=1) { qa_scene_rect_f mapped=q3nh_rect(&o->draw,(qa_scene_rect_f){
        (float)source->x/o->display.xscale,(float)source->y/o->display.yscale,(float)source->width/o->display.xscale,(float)source->height/o->display.yscale});
        r.x=(int32_t)mapped.x; r.y=(int32_t)mapped.y; r.width=(int32_t)mapped.width; r.height=(int32_t)mapped.height; }
    for(unsigned i=0;i<3;++i)r.axis[i]=q3ne_array(source->viewaxis[i]);
    q3nm_result(qa_q3_presentation_render(o->frame->presentation,&r,o->menus->error));
}
void q3nm_font_record(const fontInfo_t *source,qa_q3_font_record *out)
{
    memset(out,0,sizeof(*out)); out->glyph_scale=source->glyphScale; memcpy(out->name,source->name,64);
    for(unsigned i=0;i<256;++i) { const glyphInfo_t *s=&source->glyphs[i]; qa_q3_glyph_record *d=&out->glyphs[i];
        *d=(qa_q3_glyph_record){.height=s->height,.top=s->top,.bottom=s->bottom,.pitch=s->pitch,.x_skip=s->xSkip,
            .image_width=s->imageWidth,.image_height=s->imageHeight,.s=s->s,.t=s->t,.s2=s->s2,.t2=s->t2,.handle=s->glyph}; memcpy(d->shader_name,s->shaderName,32); }
}
void q3nm_font_import(const qa_q3_font_record *source,fontInfo_t *out)
{
    memset(out,0,sizeof(*out)); out->glyphScale=source->glyph_scale; memcpy(out->name,source->name,64);
    for(unsigned i=0;i<256;++i) { const qa_q3_glyph_record *s=&source->glyphs[i]; glyphInfo_t *d=&out->glyphs[i];
        *d=(glyphInfo_t){.height=s->height,.top=s->top,.bottom=s->bottom,.pitch=s->pitch,.xSkip=s->x_skip,
            .imageWidth=s->image_width,.imageHeight=s->image_height,.s=s->s,.t=s->t,.s2=s->s2,.t2=s->t2,.glyph=s->handle}; memcpy(d->shaderName,s->shader_name,32); }
}
static int32_t font_image(void *context,const qa_scene_image *image)
{ q3n_mission_hud *o=context; int32_t h=0; if(!o->menus->failed)q3nm_result(qa_q3_register_picture_image(o->options.assets,image,&h,o->menus->error)); return h; }
static void font(const char *path,int point_size,fontInfo_t *out)
{
    q3n_mission_hud *o=q3nm_active(); const qa_font *registered=NULL;
    if(o->menus->failed||!q3nm_current(o,o->frame,o->menus->error)) { q3nm_result(false); return; }
    qa_font_q3_options options={.point_size=point_size,.truetype_path=path,.generate_if_missing=true};
    uint8_t bytes[QA_Q3_FONT_RECORD_BYTES]; qa_q3_font_record record;
    bool ok=qa_font_q3_register(o->options.fonts,&options,&registered,o->menus->error) &&
        qa_font_q3_export(registered,font_image,o,bytes,o->menus->error) &&
        qa_q3_font_record_decode((qa_bytes){bytes,sizeof(bytes)},&record,o->menus->error) && q3nm_current(o,o->frame,o->menus->error);
    if(ok) { q3nm_font_import(&record,out);
        unsigned slot=out==&o->display.Assets.smallFont?0:out==&o->display.Assets.bigFont?2:1; o->font_holders[slot]=registered; }
    q3nm_result(ok);
}
static void get_cvar(const char *name,char *out,int capacity)
{ q3n_mission_hud *o=q3nm_active(); const qa_cvar_view *v=qa_cvars_find(q3nm_registry(o),name); q3menu_strncpyz(out,v?v->value:"",capacity); }
static float cvar(const char *name) { char text[128]; double n=0; get_cvar(name,text,sizeof(text)); q3nm_result(qa_parse_atof(text,&n,q3menu_active()->error)); return (float)n; }
static void set_cvar(const char *name,const char *value) { q3nm_result(q3nm_set(q3nm_active(),name,value)); }
static void script(char **text) { (void)text; }
static void team_color(float (*out)[4]) { const qa_q3_player *p=q3nm_require_player(q3nm_active()); if(!p)return;
    int team=p->persistant[3]; (*out)[0]=team==1?1:0; (*out)[1]=team==1||team==2?0:0.17f; (*out)[2]=team==2?1:0; (*out)[3]=0.25f; }
static void cursor_text(float x,float y,float scale,float c[4],const char *text,int position,char cursor,int limit,int style)
{ (void)position; (void)cursor; q3nm_text(x,y,scale,c,text,0,limit,style); }
static void local_sound(int h,int channel) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed)q3nm_result(qa_q3_presentation_sound(o->frame->presentation,h,NULL,(int)o->frame->viewing_client,channel,true,o->menus->error)); }
static qboolean owner_key(int id,int flags,float *special,int key) { (void)id; (void)flags; (void)special; (void)key; return qfalse; }
static void music(const char *intro,const char *loop) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed)q3nm_result(qa_q3_presentation_music(o->frame->presentation,intro,loop,o->menus->error)); }
static void music_stop(void) { music("",""); }
static qa_scene_rect_f movie_rect(q3n_mission_hud *o,float x,float y,float w,float h)
{ float scale=o->draw.scene?o->frame->preferences.hud_scale:1;
    return (qa_scene_rect_f){320+(x-320)*scale,240+(y-240)*scale,w*scale,h*scale}; }
static int movie(const char *path,float x,float y,float w,float h) { q3n_mission_hud *o=q3nm_active(); int hnd=-1;
    if(!o->menus->failed)q3nm_result(qa_q3_presentation_movie_play(o->frame->presentation,path,movie_rect(o,x,y,w,h),2,&hnd,o->menus->error));
    return hnd; }
static void movie_stop(int h) { q3n_mission_hud *o=q3nm_active(); if(!o->menus->failed&&h>=0)q3nm_result(qa_q3_presentation_movie_stop(o->frame->presentation,h,false,o->menus->error)); }
static void movie_draw(int h,float x,float y,float w,float height) { q3n_mission_hud *o=q3nm_active(); if(o->menus->failed)return;
    qa_q3_presentation_movie_extents(o->frame->presentation,h,movie_rect(o,x,y,w,height)); q3nm_result(qa_q3_presentation_movie_draw(o->frame->presentation,h,o->menus->error)); }
static void movie_run(int h) { q3n_mission_hud *o=q3nm_active(); int status; if(!o->menus->failed)q3nm_result(qa_q3_presentation_movie_run(o->frame->presentation,h,&status,o->menus->error)); }
static int feeder_count(float id)
{ q3n_mission_hud *o=q3nm_active(); if(!o->commands)return 0; if(id==11)return o->commands->num_scores; int team=id==5?1:id==6?2:-1,count=0; for(int i=0;i<o->commands->num_scores;++i)if(o->commands->scores[i].team==team)++count; return count; }
static int score_index(q3n_mission_hud *o,int team,int index)
{ if(qa_game_type_is_team(o->commands->game_type)) { int count=0; for(int i=0;i<o->commands->num_scores;++i)if(o->commands->scores[i].team==team && count++==index)return i; } return index; }
static const char *feeder_text(float feeder,int index,int column,int *image)
{
    q3n_mission_hud *o=q3nm_active(); *image=-1; if(!o->commands)return "";
    int team=feeder==5?1:feeder==6?2:-1,slot=score_index(o,team,index);
    if(slot<0||slot>=o->commands->num_scores)return "";
    const q3n_command_score *s=&o->commands->scores[slot]; const q3n_client_info *ci=q3nm_client(o,s->client); if(!ci||!ci->info_valid)return "";
    switch(column) {
    case 0: { const q3n_media_view *m=q3n_media_read(o->frame->media); int pw=(ci->dynamic.powerups&(1<<9))?9:(ci->dynamic.powerups&(1<<7))?7:(ci->dynamic.powerups&(1<<8))?8:-1;
        if(pw>=0) { size_t count; const qa_q3_item *items=qa_q3_items(QA_Q3_TEAM_ARENA,&count); for(size_t i=0;i<count;++i)if(items[i].kind==QA_Q3_ITEM_TEAM&&items[i].tag==pw) { *image=m->items[i].icon; break; } }
        else if(ci->bot_skill>0&&ci->bot_skill<=5)*image=m->bot_skill_shaders[ci->bot_skill-1]; else if(ci->handicap<100)return q3menu_format("%d",ci->handicap);
        break; }
    case 1: if(team!=-1)*image=q3nm_status(o,ci->team_task); break;
    case 2: { const qa_q3_player *p=q3nm_require_player(o); if(!p)return "";
        if((uint32_t)p->stats[6]&(1u<<((unsigned)s->client&31u)))return "Ready";
        if(team==-1) { if(o->commands->game_type==1)return q3menu_format("%d/%d",ci->wins,ci->losses); if(ci->team==3)return "Spectator"; } else if(ci->team_leader)return "Leader"; break; }
    case 3:return ci->name; case 4:return q3menu_format("%d",ci->dynamic.score); case 5:return q3menu_format("%4d",s->time); case 6:return s->ping==-1?"connecting":q3menu_format("%4d",s->ping); }
    return "";
}
static int feeder_image(float id,int index) { (void)id; (void)index; return 0; }
static void feeder_select(float id,int index)
{ q3n_mission_hud *o=q3nm_active(); if(!o->commands)return;
    if(!qa_game_type_is_team(o->commands->game_type)) { o->selected_score=index; return; }
    int team=id==5?1:2,count=0;
    for(int i=0;i<o->commands->num_scores;++i)if(o->commands->scores[i].team==team&&count++==index) { o->selected_score=i; return; }
}
static bool source_read(void *context,const qa_script_include *request,qa_script_resource *out,bool *found,qa_error *e)
{
    q3n_mission_hud *o=context; char path[1024]; const char *name=request->requested_path; qa_resource *resource=NULL; qa_error local={0};
    if(!q3nm_current(o,o->frame,e))return false;
    if(request->kind!=QA_SCRIPT_ROOT && request->from_path) {
        const char *slash=strrchr(request->from_path,'/'); size_t prefix=slash?(size_t)(slash-request->from_path+1):0;
        if(prefix+strlen(name)<sizeof(path)) { memcpy(path,request->from_path,prefix); strcpy(path+prefix,name);
            if(qa_vfs_acquire(o->options.content,path,&resource,NULL,&local))name=path;
            else if(local.code!=QA_ERROR_NOT_FOUND) { if(e)*e=local; return false; } }
    }
    if(!resource&&!qa_vfs_acquire(o->options.content,name,&resource,NULL,&local)) { *found=false; if(local.code==QA_ERROR_NOT_FOUND)return true; if(e)*e=local; return false; }
    if(!q3nm_current(o,o->frame,e)) { qa_resource_release(resource); return false; }
    *out=(qa_script_resource){.path=qa_resource_path(resource),.bytes=qa_resource_bytes(resource),.lease=resource}; *found=true; return true;
}
static void source_diagnostic(void *context,const qa_script_diagnostic *diagnostic)
{ q3n_mission_hud *o=context; char text[4096]; snprintf(text,sizeof(text),"%s: %s, line %u: %s\n",
    diagnostic->severity==QA_SCRIPT_WARNING?"WARNING":"ERROR",diagnostic->location.path,diagnostic->location.line,diagnostic->message);
    o->options.print(o->options.context,text); q3nm_result(q3nm_current(o,o->frame,o->menus->error)); }
static void source_release(void *context,qa_script_resource *resource) { (void)context; qa_resource_release(resource->lease); memset(resource,0,sizeof(*resource)); }
static int32_t random_integer(void *context) { q3n_mission_hud *o=context; return q3n_events_rand(o->frame->events); }
static bool allocate(const q3n_mission_hud_options *,const qa_native_q3_client_basis *,q3n_mission_hud **,qa_error *);
static bool create(const q3n_mission_hud_options *options,const qa_native_q3_client_basis *basis,q3n_mission_hud **out,qa_error *e)
{
    const qa_native_q3_client_services *services=options?qa_native_q3_client_services_read(options->client):NULL;
    qa_native_q3_wire_basis wire={0};
    if(!options||options->remote_client||options->remote_source||options->compiled_source||!out||*out||!basis||basis->product!=QA_Q3_TEAM_ARENA||basis->application!=options->application||
        basis->seat!=options->seat||basis->receiver!=options->recipient.receiver||basis->source_owner!=options->recipient.source_owner||
        basis->physical_client!=options->recipient.source_client||!qa_actor_id_equal(basis->viewing_actor,options->recipient.source_actor)||!options->client||
        options->content!=basis->content||!options->assets||!options->presentation||!options->fonts||!options->milliseconds||!options->print||!options->key_catcher||
        qa_font_library_content(options->fonts)!=options->assets->options.provider.mounts||
        qa_font_library_resource_owner(options->fonts)!=options->assets->options.provider.images||
        options->presentation->options.assets!=options->assets||!services||services->client.session!=options->recipient.session||
        services->client.service_owner!=options->recipient.service_owner||services->client.frontend_lifetime!=options->recipient.frontend_lifetime||
        services->client.console!=options->recipient.console||services->client.cvars!=options->recipient.cvars||
        services->client.source_cvars!=options->recipient.source_cvars||!services->client.native_source||
        !qa_native_q3_wire_reader_basis(options->reader,&wire,e)||wire.application!=options->application||wire.session!=basis->session||
        wire.source_game!=basis->source_game||wire.source_owner!=basis->source_owner||wire.receiver!=basis->receiver||
        wire.seat!=basis->seat||wire.physical_client!=basis->physical_client||!qa_actor_id_equal(wire.actor,basis->viewing_actor)||
        wire.publication_generation!=basis->publication_generation||wire.map_revision!=basis->map_revision)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission HUD requires actual selected CGAME content and private seat services");
    return allocate(options,basis,out,e);
}
static bool allocate(const q3n_mission_hud_options *options,const qa_native_q3_client_basis *basis,q3n_mission_hud **out,qa_error *e)
{
    q3n_mission_hud *o=calloc(1,sizeof(*o)); if(!o)return q3ne_fail(e,QA_ERROR_MEMORY,"Allocating native mission HUD");
    const qa_native_q3_client_services *services=options->client ? qa_native_q3_client_services_read(options->client) : NULL;
    o->options=*options; o->native_recipient=services ? &services->client : NULL; o->options.source=NULL;
    if(basis) { o->source_game=basis->source_game;
        o->publication_generation=basis->publication_generation; o->map_revision=basis->map_revision; }
    o->scoreboard_menu=o->captured_menu=-1; o->spectator_width=-1; o->spectator_paint_x2=-1;
    o->display=(displayContextDef_t){.registerShaderNoMip=shader,.setColor=color,.drawHandlePic=picture,.drawStretchPic=stretch,
        .drawText=q3nm_text,.textWidth=q3nm_width,.textHeight=q3nm_height,.registerModel=model,.modelBounds=model_bounds,
        .fillRect=fill,.drawRect=rectangle,.drawSides=sides,.drawTopBottom=top_bottom,.clearScene=clear_scene,.addRefEntityToScene=entity,
        .renderScene=render,.registerFont=font,.ownerDrawItem=q3nm_owner,.getValue=q3nm_value,.ownerDrawVisible=q3nm_visible,
        .runScript=script,.getTeamColor=team_color,.getCVarString=get_cvar,.getCVarValue=cvar,.setCVar=set_cvar,
        .drawTextWithCursor=cursor_text,.startLocalSound=local_sound,.ownerDrawHandleKey=owner_key,.feederCount=feeder_count,
        .feederItemText=feeder_text,.feederItemImage=feeder_image,.feederSelection=feeder_select,.Error=q3menu_error,.Print=print,
        .ownerDrawWidth=q3nm_owner_width,.registerSound=sound,.startBackgroundTrack=music,.stopBackgroundTrack=music_stop,
        .playCinematic=movie,.stopCinematic=movie_stop,.drawCinematic=movie_draw,.runCinematicFrame=movie_run};
    o->menus=q3menu_create(&o->display,o,e); if(!o->menus) { free(o); return false; }
    o->menus->scripts=(qa_script_services){.context=o,.read=source_read,.release=source_release,.diagnostic=source_diagnostic};
    if(!qa_script_defines_create(&o->menus->global_defines,e)) { q3menu_destroy(o->menus); free(o); return false; }
    o->menus->script_options=(qa_script_options){.lexer_flags=QA_SCRIPT_NO_STRING_ESCAPES,.builtins=true,.globals=o->menus->global_defines};
    o->menus->random_integer=random_integer;
    *out=o; return true;
}
bool q3n_mission_hud_create(const q3n_mission_hud_options *options,q3n_mission_hud **out,qa_error *e)
{
    qa_native_q3_client_basis basis;
    if(!options||!options->source||!qa_application_native_q3_presentation_current(options->application,options->source)||
        !qa_native_q3_client_basis_read(options->client,&basis,e)||options->source->source_game!=basis.source_game||
        options->source->publication_generation!=basis.publication_generation||options->source->map_revision!=basis.map_revision)return false;
    return create(options,&basis,out,e);
}
bool q3n_mission_hud_create_restored(const q3n_mission_hud_options *options,q3n_mission_hud **out,qa_error *e)
{ qa_native_q3_client_basis basis; if(!options||!qa_native_q3_client_basis_read(options->client,&basis,e))return false;
    return create(options,&basis,out,e); }
bool q3n_mission_hud_create_remote(const q3n_mission_hud_options *options,q3n_mission_hud **out,qa_error *e)
{
    q3n_remote_source_view view;
    if(!options||!out||*out||options->source||options->client||options->reader||options->compiled_source||!options->remote_client||!options->remote_source||
       !q3n_remote_source_read(options->remote_source,&view,e)||!q3n_remote_source_current(&view)||
       q3n_remote_source_client(options->remote_source)!=options->remote_client||view.basis.product!=QA_Q3_TEAM_ARENA||
       view.basis.application!=options->application||view.basis.content!=options->content||
       view.basis.client.seat!=options->seat||view.basis.client.session!=options->recipient.session||
       view.basis.client.receiver!=options->recipient.receiver||view.basis.client.source_owner!=options->recipient.source_owner||
       view.basis.client.service_owner!=options->recipient.service_owner||
       view.basis.client.frontend_lifetime!=options->recipient.frontend_lifetime||
       view.basis.client.console!=options->recipient.console||view.basis.client.cvars!=options->recipient.cvars||
       view.basis.client.source_cvars!=options->recipient.source_cvars||
       view.basis.physical_client!=options->recipient.source_client||
       !qa_actor_id_equal(view.basis.client.source_actor,options->recipient.source_actor)||
       !options->assets||!options->presentation||!options->fonts||!options->milliseconds||!options->print||!options->key_catcher||
       options->presentation->options.assets!=options->assets||
       qa_font_library_content(options->fonts)!=options->assets->options.provider.mounts||
       qa_font_library_resource_owner(options->fonts)!=options->assets->options.provider.images)
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Remote Mission HUD requires its actual private CLIENT/menu/font resource tuple");
    return allocate(options,NULL,out,e);
}
bool q3n_mission_hud_create_compiled(const q3n_mission_hud_options *options,q3n_mission_hud **out,qa_error *e)
{
    q3n_compiled_source_view view;
    if(!options||!out||*out||options->source||options->client||options->reader||options->remote_client||options->remote_source||
       !options->compiled_source||!options->compiled_cvars||!options->compiled_current||!options->compiled_cvar_read||
       !options->compiled_console||!options->context||!q3n_compiled_source_checkpoint_read(options->compiled_source,&view,e)||
       view.basis.product!=QA_Q3_TEAM_ARENA||view.basis.application!=options->application||view.basis.content!=options->content||
       view.basis.seat!=options->seat||view.basis.assets!=options->assets||!options->presentation||!options->fonts||
       !options->milliseconds||!options->print||!options->key_catcher||options->presentation->options.assets!=options->assets||
       qa_font_library_content(options->fonts)!=options->assets->options.provider.mounts||
       qa_font_library_resource_owner(options->fonts)!=options->assets->options.provider.images||!q3n_compiled_source_checkpoint_current(&view))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Compiled Mission HUD requires its actual CLIENT command and menu/font resource tuple");
    return allocate(options,NULL,out,e);
}
bool q3n_mission_hud_bind(q3n_mission_hud *o,q3n_hud *hud,qa_error *e)
{ if(!q3n_mission_hud_idle(o)||!hud||hud->options.application!=o->options.application||hud->options.assets!=o->options.assets||
    hud->options.client!=o->options.client||hud->options.remote_client!=o->options.remote_client||
    hud->options.compiled_source!=o->options.compiled_source||
    hud->options.seat!=o->options.seat||hud->source_game!=o->source_game||hud->product!=QA_Q3_TEAM_ARENA)
    return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission HUD binding requires its actual native HUD owner");
  o->hud=hud; return true; }
void q3n_mission_hud_destroy(q3n_mission_hud *o) { if(q3n_mission_hud_idle(o)) { q3menu_destroy(o->menus); free(o); } }
bool q3n_mission_hud_idle(const q3n_mission_hud *o) { return o&&!o->busy&&!o->rebind; }
bool q3n_mission_hud_rebind_prepare(q3n_mission_hud *o,const q3n_compiled_source_rebind_ticket *t,
    const qa_command_context *context,qa_error *e)
{
    if(!q3n_mission_hud_idle(o)||!o->options.compiled_source||
       !q3n_compiled_source_rebind_context_is(t,o->options.compiled_source,&o->options.compiled_context,context))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission round rebind requires its actual retained and staged CLIENT context");
    o->rebound_context=*context; o->rebind=t; return true;
}
bool q3n_mission_hud_rebind_ready(const q3n_mission_hud *o,const q3n_compiled_source_rebind_ticket *t)
{ return o && !o->busy && o->rebind==t && t &&
    q3n_compiled_source_rebind_context_is(t,o->options.compiled_source,&o->options.compiled_context,&o->rebound_context); }
bool q3n_mission_hud_rebind_checkpoint_current(const q3n_mission_hud *o,const q3n_compiled_source_rebind_ticket *t)
{ return o && !o->busy && t && o->rebind==t && q3n_compiled_source_rebind_checkpoint_context_is(
    t,o->options.compiled_source,&o->options.compiled_context,&o->rebound_context); }
bool q3n_mission_hud_rebind_restore(q3n_mission_hud *o,const q3n_compiled_source_rebind_ticket *t,
    const qa_command_context *context,qa_error *e)
{
    if(!q3n_mission_hud_idle(o) || !context || !q3n_compiled_source_rebind_checkpoint_context_is(
        t,o->options.compiled_source,&o->options.compiled_context,context))
        return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission imported round requires its actual CLIENT checkpoint context");
    o->rebound_context=*context;o->rebind=t;return true;
}
void q3n_mission_hud_rebind_commit(q3n_mission_hud *o,const q3n_compiled_source_rebind_ticket *t)
{ if(o && t && o->rebind==t) { o->options.compiled_context=o->rebound_context;
    o->rebound_context=(qa_command_context){0}; o->rebind=NULL; } }
void q3n_mission_hud_rebind_abort(q3n_mission_hud *o,const q3n_compiled_source_rebind_ticket *t)
{ if(o && t && o->rebind==t) { o->rebound_context=(qa_command_context){0}; o->rebind=NULL; } }
