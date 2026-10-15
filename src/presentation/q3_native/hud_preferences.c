#include "hud_internal.h"
#include "qa/q3_presentation_save.h"
#include "qa/text.h"

bool q3nh_preferences(q3n_hud_draw *d)
{
    qa_q3_presentation_binding backend;
    if(!d || !d->owner || !d->frame || !qa_ui_presentation_read(d->owner->options.ui,&d->typography,d->error) ||
       !qa_q3_presentation_binding_read(d->frame->presentation,&backend,d->error))return false;
    if(!backend.frame || backend.options.seat!=d->owner->options.presentation_seat ||
       d->typography.fonts.seat!=backend.options.seat ||
       d->typography.text_scale!=d->frame->preferences.text_scale ||
       d->typography.color_mode!=d->frame->preferences.color_mode ||
       backend.options.viewport.x!=d->viewport.x || backend.options.viewport.y!=d->viewport.y ||
       backend.options.viewport.width!=d->viewport.width || backend.options.viewport.height!=d->viewport.height)
        return q3ne_fail(d->error,QA_ERROR_ARGUMENT,"Native HUD typography requires its actual physical seat and synchronized preferences");
    d->scene=backend.frame; return q3nh_current(d->owner,d->frame,d->error);
}
void q3nh_anchor(q3n_hud_draw *d,float x,float y) { d->anchor_x=x; d->anchor_y=y; }
qa_scene_rect_f q3nh_rect(const q3n_hud_draw *d,qa_scene_rect_f r)
{
    float scale=d->frame->preferences.hud_scale;
    if(scale!=1) {
        r.x=d->anchor_x+(r.x-d->anchor_x)*scale;
        r.y=d->anchor_y+(r.y-d->anchor_y)*scale;
        r.width*=scale; r.height*=scale;
    }
    float sx=((float)d->viewport.width / 640),sy=((float)d->viewport.height / 480);
    return (qa_scene_rect_f){(r.x * sx),(r.y * sy),(r.width * sx),(r.height * sy)};
}
void q3nh_palette(const q3n_hud_draw *d,const float input[4],float output[4])
{
    memcpy(output,input,4*sizeof(float));
    switch(d->frame->preferences.color_mode) {
    case QA_UI_COLOR_MONOCHROME: {
        float value=input[0]*0.299f+input[1]*0.587f+input[2]*0.114f;
        output[0]=output[1]=output[2]=value; break;
    }
    case QA_UI_COLOR_BLUE_YELLOW:
        if(input[0]>input[1] && input[0]>input[2]) {
            output[1]=fmaxf(input[1],input[0]*0.75f); output[2]=fminf(input[2],input[0]*0.1f);
        } else if(input[2]>input[0] && input[2]>input[1]) {
            output[0]=fminf(input[0],input[2]*0.1f); output[1]=fmaxf(input[1],input[2]*0.55f);
        }
        break;
    case QA_UI_COLOR_STANDARD:break;
    }
}
static bool alternate(const q3n_hud_draw *d,const char *text)
{
    if(d->frame->preferences.typeface==QA_UI_TYPEFACE_BOLD)
        return qa_utf8_valid((qa_bytes){(const uint8_t *)text,strlen(text)});
    for(const unsigned char *p=(const unsigned char *)text;*p;++p)if(*p>=128)
        return qa_utf8_valid((qa_bytes){(const uint8_t *)text,strlen(text)});
    return false;
}
static qa_font_selection selected_fonts(const q3n_hud_draw *d)
{
    qa_font_selection fonts=d->typography.fonts;
    if(d->frame->preferences.typeface==QA_UI_TYPEFACE_STANDARD)fonts.primary=NULL;
    return fonts;
}
static bool layout(q3n_hud_draw *d,const char *text,float height,const float color[4],bool force,int32_t limit,
    qa_font_layout *out)
{
    qa_font_selection fonts=selected_fonts(d);
    qa_font_layout_options options={.text={(const uint8_t *)text,strlen(text)},
        .scale=height*d->typography.text_scale/8,.color={color[0],color[1],color[2],color[3]},
        .color_codes=QA_FONT_COLOR_Q3,.force_color=force || d->frame->preferences.color_mode!=QA_UI_COLOR_STANDARD,
        .max_glyphs=limit>0?(size_t)limit:0};
    return d->scene && qa_font_layout_build(&fonts,&options,&d->scene->storage,out,d->error);
}
bool q3nh_font_metric(q3n_hud_draw *d,const char *text,float height,int32_t limit,
    float *width,float *out_height,bool *handled)
{
    if(!text || !width || !out_height || !handled)return false;
    *handled=alternate(d,text); if(!*handled)return true;
    qa_font_layout value;
    if(!layout(d,text,height,q3nh_white,true,limit,&value))return false;
    *width=value.width; *out_height=0;
    for(size_t i=0;i<value.glyph_count;++i)*out_height=fmaxf(*out_height,value.glyphs[i].rect.height);
    return true;
}
bool q3nh_font_text(q3n_hud_draw *d,float x,float y,const char *text,float height,const float color[4],
    bool force,bool shadow,int32_t limit,qa_font_alignment alignment,bool baseline,
    const q3n_hud_text_cursor *cursor,bool *handled)
{
    if(!text || !color || !handled)return false;
    *handled=alternate(d,text); if(!*handled)return true;
    float selected[4]; q3nh_palette(d,color,selected);
    if(d->frame->preferences.high_contrast) { selected[0]=selected[1]=selected[2]=1; force=true; shadow=true; }
    bool with_cursor=cursor && cursor->position>=0;
    const char *painted=text;
    size_t maximum=0;
    if(with_cursor && d->scene) {
        maximum=strlen(text);
        char *extended=qa_arena_alloc(&d->scene->storage,maximum+2,1,d->error);
        if(!extended)return false;
        memcpy(extended,text,maximum);
        /* A final literal caret cannot form a new color escape, including
         * when the authored text itself ends in a caret. */
        extended[maximum]='^'; extended[maximum+1]=0; painted=extended;
    }
    qa_font_layout value;
    if(!layout(d,painted,height,selected,force,with_cursor?0:limit,&value))return false;
    qa_font_positioned_glyph *glyphs=(qa_font_positioned_glyph *)value.glyphs;
    qa_font_positioned_glyph *caret=NULL;
    size_t before_cursor=0;
    if(with_cursor) {
        bool limited=limit>0 && maximum>(size_t)limit;
        if(limit>0 && maximum>(size_t)limit)maximum=(size_t)limit;
        size_t available=value.glyph_count-1,visible=available<maximum?available:maximum;
        size_t position=(size_t)cursor->position;
        bool inside=position<visible,end=position==maximum;
        if(inside || end) {
            const qa_font_positioned_glyph *anchor=glyphs+(inside?position:visible);
            qa_font_info from,to;
            qa_font_glyph cursor_glyph;
            qa_font_selection fonts=selected_fonts(d);
            if(!qa_font_describe(anchor->glyph.font,&from) ||
               !qa_font_resolve(&fonts,cursor->character,false,&cursor_glyph) ||
               !qa_font_describe(cursor_glyph.font,&to))return false;
            float from_scale=value.line_height/fmaxf(1,from.line_height);
            float to_scale=value.line_height/fmaxf(1,to.line_height);
            float pen=anchor->rect.x-anchor->glyph.bearing_x*from_scale;
            float row=anchor->rect.y-(from.ascent-anchor->glyph.bearing_y)*from_scale;
            qa_vec4 tint=anchor->color;
            if(end && limited && visible==maximum && visible)tint=glyphs[visible-1].color;
            if(cursor_glyph.baked_color)tint=(qa_vec4){1,1,1,tint.w};
            caret=glyphs+available;
            *caret=(qa_font_positioned_glyph){.glyph=cursor_glyph,.color=tint,
                .rect={pen+cursor_glyph.bearing_x*to_scale,row+(to.ascent-cursor_glyph.bearing_y)*to_scale,
                    cursor_glyph.width*to_scale,cursor_glyph.height*to_scale}};
            before_cursor=inside?position+1:visible;
        }
        value.glyph_count=visible;
    }
    for(size_t row=0;row<value.line_count;++row) {
        const qa_font_line *line=value.lines+row;
        float offset=alignment==QA_FONT_ALIGN_CENTER?line->width*0.5f:alignment==QA_FONT_ALIGN_RIGHT?line->width:0;
        for(size_t i=0;i<line->glyph_count;++i) {
            qa_font_positioned_glyph *g=&glyphs[line->first_glyph+i];
            if(with_cursor && line->first_glyph+i>=value.glyph_count && g!=caret)continue;
            float ascent=0;
            if(baseline) {
                qa_font_info info;
                if(!qa_font_describe(g->glyph.font,&info))return q3ne_fail(d->error,QA_ERROR_ARGUMENT,"Native HUD glyph lost its actual font metrics");
                ascent=info.ascent*value.line_height/fmaxf(1,info.line_height);
            }
            g->rect=q3nh_rect(d,(qa_scene_rect_f){x+g->rect.x-offset,y+g->rect.y-ascent,g->rect.width,g->rect.height});
            float raw[4]={g->color.x,g->color.y,g->color.z,g->color.w},mapped[4]; q3nh_palette(d,raw,mapped);
            g->color=(qa_vec4){mapped[0],mapped[1],mapped[2],mapped[3]};
        }
    }
    qa_font_draw_options options={.seat=d->owner->options.presentation_seat,.target=d->viewport,.space=QA_FONT_PIXELS,
        .shadow_offset=shadow?2*d->frame->preferences.hud_scale*fminf((float)d->viewport.width/640,(float)d->viewport.height/480):0};
    bool ok;
    if(caret) {
        qa_font_layout prefix=value,suffix=value,mark=value;
        prefix.glyph_count=before_cursor;
        suffix.glyphs+=before_cursor; suffix.glyph_count-=before_cursor;
        mark.glyphs=caret; mark.glyph_count=1; mark.lines=NULL; mark.line_count=0;
        qa_font_draw_options cursor_options=options; cursor_options.shadow_offset=0;
        ok=qa_font_draw_layout(d->scene,&prefix,&options,d->error) &&
            qa_font_draw_layout(d->scene,&mark,&cursor_options,d->error) &&
            qa_font_draw_layout(d->scene,&suffix,&options,d->error);
    } else ok=qa_font_draw_layout(d->scene,&value,&options,d->error);
    return ok && q3nh_current(d->owner,d->frame,d->error);
}
bool q3nh_width(q3n_hud_draw *d,const char *text,float width,float height,int32_t limit,float *out)
{
    float measured_height; bool handled;
    if(!q3nh_font_metric(d,text,height,limit,out,&measured_height,&handled))return false;
    if(!handled) { size_t count=q3nh_strlen(text); if(limit>0 && count>(size_t)limit)count=(size_t)limit;
        *out=(float)count*width*d->frame->preferences.text_scale; }
    return true;
}
