/* Team Arena CG_Text_* and the byte font contract, GPL-2.0-or-later. */
#include "mission_hud_internal.h"

static fontInfo_t *selected_font(float scale)
{
    q3n_mission_hud *o=q3nm_active();
    return scale<=q3nm_number(o,"cg_smallFont")?&o->display.Assets.smallFont:
        scale>q3nm_number(o,"cg_bigFont")?&o->display.Assets.bigFont:&o->display.Assets.textFont;
}
static bool escape(const unsigned char *p) { return p[0]=='^' && p[1] && p[1]!='^'; }
static float line_height(const fontInfo_t *font,float scale)
{ int height=0; for(unsigned i=0;i<256;++i)if(font->glyphs[i].height>height)height=font->glyphs[i].height;
    return height*(scale*font->glyphScale); }
static int metric(const char *text,float scale,int limit,bool height)
{
    q3n_mission_hud *o=q3nm_active(); fontInfo_t *font=selected_font(scale); float value=0; int count=0;
    float nominal=line_height(font,scale);
    if(o->draw.scene&&nominal>0) { float width,ink_height; bool handled;
        if(!q3nh_font_metric(&o->draw,text?text:"",nominal,limit,&width,&ink_height,&handled)) { q3nm_result(false); return 0; }
        if(handled)return (int)(height?ink_height:width); }
    for(const unsigned char *p=(const unsigned char *)text;p&&*p&&(limit<=0||count<limit);++p) {
        if(escape(p)) { ++p; continue; }
        glyphInfo_t *g=&font->glyphs[*p];
        value=height?fmaxf(value,(float)g->height):value+(float)g->xSkip; ++count;
    }
    return (int)(value*(scale*font->glyphScale)*(o->draw.scene?o->frame->preferences.text_scale:1));
}
int q3nm_width(const char *text,float scale,int limit) { return metric(text,scale,limit,false); }
int q3nm_height(const char *text,float scale,int limit) { return metric(text,scale,limit,true); }
static void glyph(glyphInfo_t *g,float x,float y,float scale)
{
    q3n_mission_hud *o=q3nm_active(); if(!g->glyph||o->menus->failed)return;
    qa_scene_rect_f r=q3nh_rect(&o->draw,(qa_scene_rect_f){x,y-scale*g->top,g->imageWidth*scale,g->imageHeight*scale});
    q3nm_result(q3nh_pixels(&o->draw,r.x,r.y,r.width,r.height,g->glyph,
        (qa_scene_vec4){g->s,g->t,g->s2,g->t2}));
}
static void colored(unsigned char code,float alpha,float out[4])
{
    static const float rgb[8][3]={{0,0,0},{1,0,0},{0,1,0},{1,1,0},{0,0,1},{0,1,1},{1,0,1},{1,1,1}};
    unsigned index=(code-'0')&7; memcpy(out,rgb[index],3*sizeof(float)); out[3]=alpha;
}
void q3nm_text(float x,float y,float scale,float input[4],const char *text,float adjust,int limit,int style)
{
    q3n_mission_hud *o=q3nm_active(); if(o->menus->failed)return;
    fontInfo_t *font=selected_font(scale); float nominal=line_height(font,scale),color[4]; memcpy(color,input,sizeof(color));
    bool force=o->frame->preferences.high_contrast||o->frame->preferences.color_mode!=QA_UI_COLOR_STANDARD;
    if(o->frame->preferences.high_contrast) { color[0]=color[1]=color[2]=1; style=6; }
    if(nominal>0) { bool handled; q3nm_result(q3nh_font_text(&o->draw,x,y,text?text:"",nominal,color,force,
        style==3||style==6,limit,QA_FONT_ALIGN_LEFT,true,&handled)); if(handled)o->text_policy_active=true;
        if(handled||o->menus->failed)return; }
    scale*=font->glyphScale*o->frame->preferences.text_scale; adjust*=o->frame->preferences.text_scale;
    int count=0; q3nm_result(q3nh_color(&o->draw,color));
    for(const unsigned char *p=(const unsigned char *)text;p&&*p&&!o->menus->failed&&(limit<=0||count<limit);++p) {
        if(escape(p)) { ++p; if(!force) { colored(*p,input[3],color); q3nm_result(q3nh_color(&o->draw,color)); } continue; }
        glyphInfo_t *g=&font->glyphs[*p];
        if((style==3||style==6)&&g->glyph) {
            float black[4]={0,0,0,color[3]},offset=style==3?1:2;
            q3nm_result(q3nh_color(&o->draw,black)); glyph(g,x+offset,y+offset,scale); q3nm_result(q3nh_color(&o->draw,color));
        }
        glyph(g,x,y,scale); x+=g->xSkip*scale+adjust; ++count;
    }
    q3nm_result(q3nh_color(&o->draw,NULL));
}
float q3nm_limit(const char *text,float x,float y,float scale,const float input[4],float max_x,int limit)
{
    q3n_mission_hud *o=q3nm_active(); if(o->menus->failed)return 0;
    if(!text)text="";
    fontInfo_t *font=selected_font(scale); float nominal=line_height(font,scale);
    if(nominal>0) { float width,height; bool handled;
        if(!q3nh_font_metric(&o->draw,text?text:"",nominal,limit,&width,&height,&handled)) { q3nm_result(false); return 0; }
        if(handled) {
            qa_font_selection selection=o->draw.typography.fonts;
            if(o->frame->preferences.typeface==QA_UI_TYPEFACE_STANDARD)selection.primary=NULL;
            qa_font_layout_options options={.text={(const uint8_t *)text,strlen(text)},
                .scale=nominal*o->draw.typography.text_scale/8,.color={1,1,1,1},
                .color_codes=QA_FONT_COLOR_Q3,.force_color=true,.max_glyphs=limit>0?(size_t)limit:0};
            qa_font_layout layout;
            if(!qa_font_layout_build(&selection,&options,&o->draw.scene->storage,&layout,o->menus->error)) { q3nm_result(false); return 0; }
            if(!layout.glyph_count)return max_x;
            const qa_font_line *line=layout.lines; int fit=0; float advance=0; bool clipped=false;
            for(size_t i=0;i<line->glyph_count;++i) {
                float end=line->width;
                if(i+1<line->glyph_count) { const qa_font_positioned_glyph *next=&layout.glyphs[line->first_glyph+i+1]; qa_font_info info;
                    if(!qa_font_describe(next->glyph.font,&info)) { q3nm_result(false); return 0; }
                    end=next->rect.x-next->glyph.bearing_x*layout.line_height/fmaxf(1,info.line_height); }
                if(x+end>max_x) { clipped=true; break; } fit=(int)(i+1); advance=end;
            }
            if(fit>0) { float color[4]; memcpy(color,input,sizeof(color)); q3nm_text(x,y,scale,color,text,0,fit,0); }
            return clipped?0:x+advance;
        }
    }
    scale*=font->glyphScale; float draw_scale=scale*o->frame->preferences.text_scale;
    float color[4]; memcpy(color,input,sizeof(color)); float result=max_x; int count=0;
    bool force=o->frame->preferences.high_contrast||o->frame->preferences.color_mode!=QA_UI_COLOR_STANDARD;
    if(o->frame->preferences.high_contrast)color[0]=color[1]=color[2]=1;
    q3nm_result(q3nh_color(&o->draw,color));
    for(const unsigned char *p=(const unsigned char *)text;p&&*p&&!o->menus->failed&&(limit<=0||count<limit);++p) {
        if(escape(p)) { ++p; if(!force) { colored(*p,input[3],color); q3nm_result(q3nh_color(&o->draw,color)); } continue; }
        if(x+q3nm_width((const char *)p,scale,1)>max_x) { result=0; break; }
        glyphInfo_t *g=&font->glyphs[*p]; glyph(g,x,y,draw_scale); x+=g->xSkip*draw_scale; result=x; ++count;
    }
    q3nm_result(q3nh_color(&o->draw,NULL)); return result;
}
bool q3n_mission_hud_text(q3n_mission_hud *o,const q3n_frame *f,const char *text,float y,float scale,
    const float input[4],int32_t style,bool integer_half,qa_error *e)
{
    q3menu_context *previous; if(!text||!input||!isfinite(y)||!isfinite(scale)||!q3nm_begin(o,f,e,&previous))return false;
    if(!q3nh_preferences(&o->draw))return q3nm_end(o,previous,false);
    int width=q3nm_width(text,scale,0); float color[4]; memcpy(color,input,sizeof(color));
    q3nm_text(320-(integer_half?(float)(width/2):width*0.5f),y,scale,color,text,0,0,style);
    return q3nm_end(o,previous,true);
}
bool q3n_mission_hud_center_line(q3n_mission_hud *o,const q3n_frame *f,const char *text,float y,
    const float input[4],float *height,qa_error *e)
{
    q3menu_context *previous; if(!text||!input||!height||!q3nm_begin(o,f,e,&previous))return false;
    if(!q3nh_preferences(&o->draw))return q3nm_end(o,previous,false);
    float color[4]; memcpy(color,input,sizeof(color)); *height=(float)q3nm_height(text,0.5f,0);
    q3nm_text((640-q3nm_width(text,0.5f,0))/2,y+*height,0.5f,color,text,0,0,6);
    return q3nm_end(o,previous,true);
}
