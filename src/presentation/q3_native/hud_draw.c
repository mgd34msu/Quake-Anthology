/* CG drawing tools and model icons, id Software 1999-2005, GPL-2.0-or-later. */
#include "hud_internal.h"

const float q3nh_white[4]={1,1,1,1},q3nh_normal[4]={1,0.69f,0,1},q3nh_red[4]={1,0,0,1};
static const float colors[8][4]={{0,0,0,1},{1,0,0,1},{0,1,0,1},{1,1,0,1},{0,0,1,1},{0,1,1,1},{1,0,1,1},{1,1,1,1}};
bool q3nh_current(q3n_hud *o,const q3n_frame *f,qa_error *e)
{
    return o && f && o->options.application==f->application && o->source_game==f->source.source_game &&
        o->options.assets==f->assets && o->options.seat==f->seat && o->product==f->source.product &&
        f->has_local_player && f->time==f->source.source_time_ms && q3ne_current(f,e)?true:
        q3ne_fail(e,QA_ERROR_ARGUMENT,"Native Q3 HUD requires the actual completed GAME and physical viewing seat");
}
size_t q3nh_strlen(const char *s)
{ size_t count=0; for(;*s;++s) { if(s[0]=='^' && s[1] && s[1]!='^')++s; else ++count; } return count; }
bool q3nh_fade(int32_t time,int32_t start,int32_t duration,float color[4])
{
    int32_t elapsed=q3ne_sub(time,start); if(!start || elapsed>=duration)return false;
    color[0]=color[1]=color[2]=1; int32_t remaining=q3ne_sub(duration,elapsed);
    color[3]=remaining<200?q3ne_div((float)remaining,200):1; return true;
}
void q3nh_health(int32_t health,int32_t armor,float color[4])
{
    color[3]=1;
    if(health<=0) { color[0]=color[1]=color[2]=0; return; }
    int32_t maximum=q3ne_int(q3ne_div(q3ne_mul((float)health,0.66f),q3ne_add(1,-0.66f)));
    health=q3ne_plus(health,armor<maximum?armor:maximum); color[0]=1;
    color[1]=health>60?1:health<30?0:q3ne_div((float)q3ne_sub(health,30),30);
    color[2]=health>=100?1:health<66?0:q3ne_div((float)q3ne_sub(health,66),33);
}
bool q3nh_color(q3n_hud_draw *d,const float c[4])
{
    qa_scene_vec4 color;
    if(c) { float mapped[4]; q3nh_palette(d,c,mapped); color=(qa_scene_vec4){mapped[0],mapped[1],mapped[2],mapped[3]}; }
    qa_q3_presentation_color(d->frame->presentation,c?&color:NULL); return q3nh_current(d->owner,d->frame,d->error);
}
bool q3nh_pixels(q3n_hud_draw *d,float x,float y,float w,float h,int32_t shader,qa_scene_vec4 uv)
{ return qa_q3_presentation_picture(d->frame->presentation,shader,(qa_scene_rect_f){x,y,w,h},uv,d->error) &&
    q3nh_current(d->owner,d->frame,d->error); }
bool q3nh_picture(q3n_hud_draw *d,float x,float y,float w,float h,int32_t shader)
{ qa_scene_rect_f r=q3nh_rect(d,(qa_scene_rect_f){x,y,w,h});
  return q3nh_pixels(d,r.x,r.y,r.width,r.height,shader,(qa_scene_vec4){0,0,1,1}); }
bool q3nh_fill(q3n_hud_draw *d,float x,float y,float w,float h,const float c[4])
{
    if(!q3nh_color(d,c))return false;
    qa_scene_rect_f r=q3nh_rect(d,(qa_scene_rect_f){x,y,w,h});
    bool ok=q3nh_pixels(d,r.x,r.y,r.width,r.height,
        q3n_media_read(d->frame->media)->graphics[Q3N_G_WHITE],(qa_scene_vec4){0,0,0,0});
    return q3nh_color(d,NULL) && ok;
}
static bool glyph(q3n_hud_draw *d,float x,float y,float w,float h,uint8_t code)
{
    if(code==32)return true;
    float row=(float)(code>>4)*0.0625f,column=(float)(code&15)*0.0625f;
    qa_scene_rect_f r=q3nh_rect(d,(qa_scene_rect_f){x,y,w,h});
    return q3nh_pixels(d,r.x,r.y,r.width,r.height,
        q3n_media_read(d->frame->media)->graphics[Q3N_G_CHARSET],(qa_scene_vec4){column,row,column+0.0625f,row+0.0625f});
}
bool q3nh_text(q3n_hud_draw *d,float x,float y,const char *text,float w,float h,const float c[4],bool force,bool shadow,int32_t limit)
{
    if(!text || !c)return false;
    bool handled;
    if(!q3nh_font_text(d,x,y,text,h,c,force,shadow,limit,QA_FONT_ALIGN_LEFT,false,&handled))return false;
    if(handled)return true;
    w=q3ne_mul(w,d->frame->preferences.text_scale); h=q3ne_mul(h,d->frame->preferences.text_scale);
    float selected[4]; memcpy(selected,c,sizeof(selected));
    if(d->frame->preferences.high_contrast) { selected[0]=selected[1]=selected[2]=1; force=true; shadow=true; }
    force=force || d->frame->preferences.color_mode!=QA_UI_COLOR_STANDARD;
    if(shadow) {
        float black[4]={0,0,0,c[3]}; if(!q3nh_color(d,black))return false;
        float xx=x; int32_t count=0;
        for(const unsigned char *p=(const unsigned char *)text;*p && (!limit || count<limit);++p) {
            if(p[0]=='^' && p[1] && p[1]!='^') { ++p; continue; }
            if(!glyph(d,xx+2,y+2,w,h,*p))return false; xx=q3ne_add(xx,w); ++count;
        }
    }
    if(!q3nh_color(d,selected))return false;
    int32_t count=0;
    for(const unsigned char *p=(const unsigned char *)text;*p && (!limit || count<limit);++p) {
        if(p[0]=='^' && p[1] && p[1]!='^') {
            if(!force) { float color[4]; memcpy(color,colors[(p[1]-'0')&7],sizeof(color)); color[3]=c[3]; if(!q3nh_color(d,color))return false; }
            ++p; continue;
        }
        if(!glyph(d,x,y,w,h,*p))return false; x=q3ne_add(x,w); ++count;
    }
    return q3nh_color(d,NULL);
}
bool q3nh_big(q3n_hud_draw *d,float x,float y,const char *s,float alpha)
{ float c[4]={1,1,1,alpha}; return q3nh_text(d,x,y,s,16,16,c,false,true,0); }
bool q3nh_center(q3n_hud_draw *d,float y,const char *s,float alpha)
{ float width; return q3nh_width(d,s,16,16,0,&width) && q3nh_big(d,320-width*0.5f,y,s,alpha); }
bool q3nh_right(q3n_hud_draw *d,float x,float y,const char *s,float alpha)
{ float width; return q3nh_width(d,s,16,16,0,&width) && q3nh_big(d,x-width,y,s,alpha); }
bool q3nh_field(q3n_hud_draw *d,float x,float y,int32_t width,int32_t value)
{
    if(width<1)return true; if(width>5)width=5;
    static const int32_t max[4]={9,99,999,9999},min[4]={0,-9,-99,-999};
    if(width<=4) { if(value>max[width-1])value=max[width-1]; if(value<min[width-1])value=min[width-1]; }
    char text[16]; snprintf(text,sizeof(text),"%i",value); size_t count=strlen(text); if(count>(size_t)width)count=(size_t)width;
    y=q3ne_add(y,48-48*d->frame->preferences.text_scale);
    float measured;
    if(!q3nh_width(d,text,32,48,(int32_t)count,&measured))return false;
    x=q3ne_add(x,(float)(2+32*width)-measured);
    if(d->frame->preferences.typeface==QA_UI_TYPEFACE_BOLD) {
        /* The actual selected font replaces the authored numeral shaders only
         * when the user requests that typography. */
        qa_scene_vec4 color=d->frame->presentation->color;
        float tint[4]={color.x,color.y,color.z,color.w};
        return q3nh_text(d,x,y,text,32,48,tint,true,false,(int32_t)count);
    }
    if(d->frame->preferences.high_contrast && !q3nh_color(d,q3nh_white))return false;
    const q3n_media_view *m=q3n_media_read(d->frame->media);
    for(size_t i=0;i<count;++i) { unsigned frame=text[i]=='-'?10u:(unsigned)(text[i]-'0');
        if(!q3nh_picture(d,x,y,32*d->frame->preferences.text_scale,48*d->frame->preferences.text_scale,m->number_shaders[frame]))return false;
        x=q3ne_add(x,32*d->frame->preferences.text_scale); }
    return true;
}
bool q3nh_model(q3n_hud_draw *d,float x,float y,float w,float h,int32_t model,int32_t skin,qa_vec3 origin,qa_vec3 angles)
{
    if(!d->settings->draw_3d_icons || !d->settings->draw_icons)return true;
    qa_q3_ref_entity r={.kind=QA_Q3_REF_MODEL,.model=model,.custom_skin=skin,.flags=64,.origin=origin};
    q3nh_axis(angles,r.axis);
    qa_q3_refdef view={.flags=1,.fov_x=30,.fov_y=30,.time=d->frame->time};
    qa_scene_rect_f rect=q3nh_rect(d,(qa_scene_rect_f){x,y,w,h});
    view.x=q3ne_int(rect.x); view.y=q3ne_int(rect.y);
    view.width=q3ne_int(rect.width); view.height=q3ne_int(rect.height); q3ne_identity(view.axis);
    return qa_q3_presentation_clear(d->frame->presentation,d->error) &&
        qa_q3_presentation_entity(d->frame->presentation,&r,d->error) &&
        qa_q3_presentation_render(d->frame->presentation,&view,d->error) && q3nh_current(d->owner,d->frame,d->error);
}
static qa_vec3 icon_origin(qa_bounds b,float fraction)
{ return qa_v3(q3ne_div(q3ne_mul(fraction,q3ne_add(b.maxs.z,-b.mins.z)),0.268f),
    q3ne_mul(0.5f,q3ne_add(b.mins.y,b.maxs.y)),-q3ne_mul(0.5f,q3ne_add(b.mins.z,b.maxs.z))); }
bool q3nh_head(q3n_hud_draw *d,float x,float y,float w,float h,int32_t client,qa_vec3 angles)
{
    if(client<0 || client>=64)return q3ne_fail(d->error,QA_ERROR_FORMAT,"HUD head client is outside physical source array");
    const q3n_client_info *ci=q3n_clients_get(d->frame->clients,(uint32_t)client); if(!ci)return false;
    if(d->settings->draw_3d_icons) {
        if(!ci->models[2])return true;
        qa_bounds b; if(!qa_q3_presentation_model_bounds(d->frame->assets,ci->models[2],&b,d->error))return false;
        qa_vec3 origin=q3ne_sum(icon_origin(b,0.7f),q3ne_array(ci->animations.head_offset));
        if(!q3nh_model(d,x,y,w,h,ci->models[2],ci->skins[2],origin,angles))return false;
    } else if(d->settings->draw_icons && !q3nh_picture(d,x,y,w,h,ci->icon))return false;
    return !ci->deferred || q3nh_picture(d,x,y,w,h,q3n_media_read(d->frame->media)->graphics[Q3N_G_DEFER]);
}
bool q3nh_flag(q3n_hud_draw *d,float x,float y,float w,float h,int32_t team,bool force_2d)
{
    if(team<0 || team>2)return true;
    const q3n_media_view *m=q3n_media_read(d->frame->media); unsigned powerup=team==1?7:team==2?8:9;
    if(!force_2d && d->settings->draw_3d_icons) {
        qa_bounds b; if(!qa_q3_presentation_model_bounds(d->frame->assets,m->graphics[Q3N_G_RED_FLAG],&b,d->error))return false;
        qa_vec3 angles=qa_v3(0,q3ne_mul(60,(float)sin((double)q3ne_div((float)d->frame->time,2000))),0);
        return q3nh_model(d,x,y,w,h,m->graphics[team==1?Q3N_G_RED_FLAG:team==2?Q3N_G_BLUE_FLAG:Q3N_G_NEUTRAL_FLAG],0,icon_origin(b,0.5f),angles);
    }
    if(d->settings->draw_icons) {
        size_t count; const qa_q3_item *items=qa_q3_items(d->frame->source.product,&count);
        for(size_t i=1;i<count;++i)if(items[i].kind==QA_Q3_ITEM_TEAM && items[i].tag==(int32_t)powerup)
            return q3nh_picture(d,x,y,w,h,m->items[i].icon);
    }
    return true;
}
bool q3nh_team_background(q3n_hud_draw *d,float x,float y,float w,float h,float alpha,int32_t team)
{
    if(team!=1 && team!=2)return true;
    float color[4]={team==1?1:0,0,team==2?1:0,alpha};
    bool ok=q3nh_color(d,color) && q3nh_picture(d,x,y,w,h,q3n_media_read(d->frame->media)->graphics[Q3N_G_TEAM_STATUS_BAR]);
    return q3nh_color(d,NULL) && ok;
}
bool q3nh_sound(q3n_hud_draw *d,q3n_sound sound,int32_t channel)
{ return q3ne_sound(d->frame,q3n_media_read(d->frame->media)->sounds[sound],NULL,d->frame->local_player.clientNum,channel,true,d->error); }
bool q3nh_location(q3n_hud_draw *d,int32_t index,const char **out)
{
    if(index<0 || index>=64)return q3ne_fail(d->error,QA_ERROR_FORMAT,"HUD source location index is invalid");
    uint64_t revision; return qa_application_native_q3_presentation_configstring(d->frame->application,&d->frame->source,
        608u+(uint32_t)index,out,&revision,d->error);
}
