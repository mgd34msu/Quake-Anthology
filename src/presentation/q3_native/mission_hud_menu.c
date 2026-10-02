/* CG_LoadMenus and Team Arena menu/input lifetime, GPL-2.0-or-later. */
#include "mission_hud_internal.h"

static bool assets(int handle)
{
    q3n_mission_hud *o=q3nm_active(); cachedAssets_t *a=&o->display.Assets; pc_token_t t; const char *path;
    if(!q3menu_source_token(handle,&t)||q3menu_stricmp(t.string,"{"))return false;
    while(q3menu_source_token(handle,&t)) {
        if(!q3menu_stricmp(t.string,"}"))return true;
        fontInfo_t *font=!q3menu_stricmp(t.string,"font")?&a->textFont:
            !q3menu_stricmp(t.string,"smallFont")?&a->smallFont:!q3menu_stricmp(t.string,"bigfont")?&a->bigFont:NULL;
        if(font) { int point; if(!PC_String_Parse(handle,&path)||!PC_Int_Parse(handle,&point))return false;
            o->display.registerFont(path,point,font); continue; }
        if(!q3menu_stricmp(t.string,"gradientbar")) { if(!PC_String_Parse(handle,&path))return false;
            a->gradientBar=o->display.registerShaderNoMip(path); continue; }
        int *sound=!q3menu_stricmp(t.string,"menuEnterSound")?&a->menuEnterSound:
            !q3menu_stricmp(t.string,"menuExitSound")?&a->menuExitSound:
            !q3menu_stricmp(t.string,"itemFocusSound")?&a->itemFocusSound:
            !q3menu_stricmp(t.string,"menuBuzzSound")?&a->menuBuzzSound:NULL;
        if(sound) { if(!PC_String_Parse(handle,&path))return false; *sound=o->display.registerSound(path,qfalse); continue; }
        if(!q3menu_stricmp(t.string,"cursor")) { if(!PC_String_Parse(handle,&a->cursorStr))return false;
            a->cursor=o->display.registerShaderNoMip(a->cursorStr); continue; }
        float *number=!q3menu_stricmp(t.string,"fadeClamp")?&a->fadeClamp:
            !q3menu_stricmp(t.string,"fadeAmount")?&a->fadeAmount:
            !q3menu_stricmp(t.string,"shadowX")?&a->shadowX:!q3menu_stricmp(t.string,"shadowY")?&a->shadowY:NULL;
        if(number) { if(!PC_Float_Parse(handle,number))return false; continue; }
        if(!q3menu_stricmp(t.string,"fadeCycle")) { if(!PC_Int_Parse(handle,&a->fadeCycle))return false; continue; }
        if(!q3menu_stricmp(t.string,"shadowColor")) { if(!PC_Color_Parse(handle,&a->shadowColor))return false;
            a->shadowFadeClamp=a->shadowColor[3]; continue; }
    }
    return false;
}
static bool parse_menu(const char *path)
{
    int handle=q3menu_source_open(path); if(!handle&&!q3menu_active()->failed)handle=q3menu_source_open("ui/testhud.menu");
    if(!handle)return !q3menu_active()->failed;
    pc_token_t token;
    while(q3menu_source_token(handle,&token)) {
        if(token.string[0]=='}')break;
        if(!q3menu_stricmp(token.string,"assetGlobalDef")) { if(!assets(handle))break; continue; }
        if(!q3menu_stricmp(token.string,"menudef"))Menu_New(handle);
    }
    q3menu_source_close(handle); return !q3menu_active()->failed;
}
static bool load_menus(q3n_mission_hud *o,const char *requested)
{
    qa_resource *root=NULL; qa_buffer compressed={0};
    const qa_cvar_view *selected=qa_cvars_find(q3nm_registry(o),"cg_hudFiles");
    char path[1024]; q3menu_strncpyz(path,requested?requested:selected?selected->value:"",sizeof(path)); if(!path[0])strcpy(path,"ui/hud.txt");
    int start=o->options.milliseconds(o->options.context);
    if(!q3nm_current(o,o->frame,o->menus->error)||!qa_vfs_acquire(o->options.content,path,&root,NULL,o->menus->error))return false;
    qa_bytes bytes=qa_resource_bytes(root);
    if(bytes.size>=MAX_MENUDEFFILE) { qa_resource_release(root); return q3ne_fail(o->menus->error,QA_ERROR_FORMAT,"Mission HUD menu root exceeds 4095 source bytes"); }
    bool ok=qa_common_compress(bytes,QA_COMMON_TERMINATED,&compressed,o->menus->error); qa_resource_release(root);
    if(!ok)return false;
    q3menu_reset(o->menus,false); o->scoreboard_menu=o->captured_menu=-1;
    char *cursor=(char *)compressed.data;
    o->menus->allocation_guard=true;
    if(setjmp(o->menus->allocation_failure))ok=false;
    else for(;;) {
        char *token=q3menu_parse(&cursor,qtrue); if(!*token||token[0]=='}')break;
        if(q3menu_stricmp(token,"loadmenu"))continue;
        token=q3menu_parse(&cursor,qtrue); if(*token!='{')break;
        while(*(token=q3menu_parse(&cursor,qtrue))&&q3menu_stricmp(token,"}"))if(!parse_menu(token)) { ok=false; break; }
        if(!ok||!*token)break;
    }
    o->menus->allocation_guard=false;
    for(int i=1;i<=64;++i)q3menu_source_close(i);
    qa_buffer_free(&compressed);
    if(ok&&!o->menus->failed) { o->loaded=true; q3menu_print("UI menu load time = %d milli seconds\n",q3ne_sub(o->options.milliseconds(o->options.context),start)); }
    return ok&&!o->menus->failed;
}
static void cache_assets(q3n_mission_hud *o)
{
    cachedAssets_t *a=&o->display.Assets; int (*shader)(const char *)=o->display.registerShaderNoMip;
    a->gradientBar=shader(ASSET_GRADIENTBAR); a->fxBasePic=shader(ART_FX_BASE);
    const char *fx[]={ART_FX_RED,ART_FX_YELLOW,ART_FX_GREEN,ART_FX_TEAL,ART_FX_BLUE,ART_FX_CYAN,ART_FX_WHITE};
    for(unsigned i=0;i<7;++i)a->fxPic[i]=shader(fx[i]);
    a->scrollBar=shader(ASSET_SCROLLBAR); a->scrollBarArrowDown=shader(ASSET_SCROLLBAR_ARROWDOWN);
    a->scrollBarArrowUp=shader(ASSET_SCROLLBAR_ARROWUP); a->scrollBarArrowLeft=shader(ASSET_SCROLLBAR_ARROWLEFT);
    a->scrollBarArrowRight=shader(ASSET_SCROLLBAR_ARROWRIGHT); a->scrollBarThumb=shader(ASSET_SCROLL_THUMB);
    a->sliderBar=shader(ASSET_SLIDER_BAR); a->sliderThumb=shader(ASSET_SLIDER_THUMB);
}
bool q3n_mission_hud_initialize(q3n_mission_hud *o,const q3n_frame *f,q3n_command_init_stage stage,qa_error *e)
{
    q3menu_context *previous; if(!q3nm_begin(o,f,e,&previous))return false; bool ok=true;
    switch(stage) {
    case Q3N_INIT_STRING_TABLE:q3menu_reset(o->menus,true); o->scoreboard_menu=o->captured_menu=-1; break;
    case Q3N_INIT_MISSION_ASSETS:cache_assets(o); break;
    case Q3N_INIT_HUD_MENU:ok=load_menus(o,NULL); break;
    case Q3N_INIT_TEAM_CHAT:o->system_chat[0]=o->team_chat[0][0]=o->team_chat[1][0]=0; break;
    default:ok=q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission HUD received another child's constructor stage"); break;
    }
    return q3nm_end(o,previous,ok);
}
static void score_selection(q3n_mission_hud *o,menuDef_t *menu)
{
    if(!o->commands)return;
    int red=0,blue=0;
    const qa_q3_player *p=q3nm_require_player(o); if(!p)return;
    for(int i=0;i<o->commands->num_scores;++i) { const q3n_command_score *s=&o->commands->scores[i];
        if(s->team==1)++red; else if(s->team==2)++blue; if(s->client==p->clientNum)o->selected_score=i; }
    if(!menu||o->selected_score<0||o->selected_score>=64)return;
    if(o->commands->game_type>=3) { bool b=o->commands->scores[o->selected_score].team==2; Menu_SetFeederSelection(menu,b?6:5,b?blue:red,NULL); }
    else Menu_SetFeederSelection(menu,11,o->selected_score,NULL);
}
bool q3n_mission_hud_score_selection(q3n_mission_hud *o,const q3n_frame *f,const q3n_command_state *state,qa_error *e)
{ q3menu_context *p; if(!state||!q3nm_begin(o,f,e,&p))return false; o->commands=state; score_selection(o,NULL); return q3nm_end(o,p,true); }
bool q3n_mission_hud_paint(q3n_mission_hud *o,const q3n_frame *f,bool scoreboard,bool first,qa_error *e)
{
    q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false;
    if(!q3nm_require_player(o))return q3nm_end(o,p,false);
    if(!q3nm_preferences(o)||!q3n_hud_weapon_read(o->hud,f,&o->draw.weapon_hud,e))return q3nm_end(o,p,false);
    if(scoreboard) {
        if(o->scoreboard_menu>=0)o->menus->menus[o->scoreboard_menu].window.flags&=~WINDOW_FORCED;
        if(o->scoreboard_menu<0&&o->commands) { menuDef_t *m=Menus_FindByName(o->commands->game_type>=3?"teamscore_menu":"score_menu");
            if(m)o->scoreboard_menu=(int)(m-o->menus->menus); }
        if(o->scoreboard_menu>=0) { menuDef_t *m=&o->menus->menus[o->scoreboard_menu]; if(first)score_selection(o,m); Menu_Paint(m,qtrue); }
    } else Menu_PaintAll();
    return q3nm_end(o,p,true);
}
bool q3n_mission_hud_response(q3n_mission_hud *o,const q3n_frame *f,const q3n_command_state *state,qa_error *e)
{ q3menu_context *p; if(!state||!q3nm_begin(o,f,e,&p))return false; o->commands=state;
    Menus_ShowByName("voiceMenu"); bool ok=q3nm_set(o,"cl_conXOffset","72"); o->voice_time=f->time; return q3nm_end(o,p,ok); }
bool q3n_mission_hud_timed(q3n_mission_hud *o,const q3n_frame *f,qa_error *e)
{ q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false; bool ok=true;
    if(o->voice_time&&q3ne_sub(f->time,o->voice_time)>2500) { Menus_CloseByName("voiceMenu"); ok=q3nm_set(o,"cl_conXOffset","0"); o->voice_time=0; }
    return q3nm_end(o,p,ok); }
bool q3n_mission_hud_message(q3n_mission_hud *o,const q3n_frame *f,int32_t type,const char *text,qa_error *e)
{ q3menu_context *p; if(!text)return q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission HUD chat requires source text");
    if(!q3nm_begin(o,f,e,&p))return false;
    if(type==0)q3menu_strncpyz(o->system_chat,text,256);
    else { memcpy(o->team_chat[1],o->team_chat[0],256); q3menu_strncpyz(o->team_chat[0],text,256); } return q3nm_end(o,p,true); }
static void event(q3n_mission_hud *o,int type)
{ o->event_handling=type; if(!type) { Menus_CloseByName("teamMenu"); Menus_CloseByName("getMenu"); } }
bool q3n_mission_hud_event(q3n_mission_hud *o,const q3n_frame *f,int32_t type,qa_error *e)
{ q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false; event(o,type); return q3nm_end(o,p,true); }
bool q3n_mission_hud_team_menu(q3n_mission_hud *o,const q3n_frame *f,bool show,qa_error *e)
{ q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false;
    if(show)Menus_ShowByName("teamMenu"); else { Menus_CloseByName("teamMenu"); Menus_CloseByName("getMenu"); }
    return q3nm_end(o,p,true); }
bool q3n_mission_hud_client_number(q3n_mission_hud *o,const q3n_frame *f,const char *name,int32_t *out,qa_error *e)
{ q3menu_context *p; if(!name||!out||!q3nm_begin(o,f,e,&p))return false; *out=-1;
    if(o->commands)for(int i=0;i<o->commands->max_clients;++i) { const q3n_client_info *ci=q3nm_client(o,i);
        if(ci&&ci->info_valid&&!q3menu_stricmp(ci->name,name)) { *out=i; break; } }
    return q3nm_end(o,p,true); }
bool q3n_mission_hud_reset(q3n_mission_hud *o,const q3n_frame *f,bool strings,qa_error *e)
{ q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false; q3menu_reset(o->menus,strings);
    o->scoreboard_menu=o->captured_menu=-1; return q3nm_end(o,p,true); }
bool q3n_mission_hud_load_menus(q3n_mission_hud *o,const q3n_frame *f,const char *path,qa_error *e)
{ q3menu_context *p; if(!path||!q3nm_begin(o,f,e,&p))return false; bool ok=load_menus(o,path); return q3nm_end(o,p,ok); }
bool q3n_mission_hud_menu_buffer(q3n_mission_hud *o,const q3n_frame *f,const char *path,qa_buffer *out,bool *found,qa_error *e)
{
    q3menu_context *p; if(!path||!out||out->data||out->size||!found||!q3nm_begin(o,f,e,&p))return false;
    qa_resource *resource=NULL; qa_error local={0}; bool ok=true; *found=false;
    if(!qa_vfs_acquire(o->options.content,path,&resource,NULL,&local)) {
        if(local.code==QA_ERROR_NOT_FOUND)q3menu_print("^1menu file not found: %s, using default\n",path);
        else { if(e)*e=local; ok=false; }
    } else {
        qa_bytes bytes=qa_resource_bytes(resource);
        if(bytes.size>=MAX_MENUFILE)q3menu_print("^1menu file too large: %s is %zu, max allowed is %i",path,bytes.size,MAX_MENUFILE);
        else { uint8_t *copy=malloc(bytes.size+1); if(!copy)ok=q3ne_fail(e,QA_ERROR_MEMORY,"Retaining Mission HUD menu buffer");
            else { if(bytes.size)memcpy(copy,bytes.data,bytes.size); copy[bytes.size]=0; *out=(qa_buffer){copy,bytes.size}; *found=true; } }
        qa_resource_release(resource);
    }
    ok=q3nm_end(o,p,ok); if(!ok) { qa_buffer_free(out); *found=false; } return ok;
}
bool q3n_mission_hud_mouse(q3n_mission_hud *o,const q3n_frame *f,int32_t dx,int32_t dy,qa_error *e)
{
    q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false; o->display.cursorx=o->cursor_x; o->display.cursory=o->cursor_y;
    const qa_q3_player *player=q3n_frame_predicted_player(f); if(!player)return q3nm_end(o,p,q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission mouse input requires its actual predicted player"));
    int type=player->pmType; bool ok=true;
    if((type==0||type==2)&&!o->hud->state.show_scores)ok=o->options.key_catcher(o->options.context,0,e);
    else { o->cursor_x=q3ne_plus(o->cursor_x,dx); o->cursor_y=q3ne_plus(o->cursor_y,dy);
        if(o->cursor_x<0)o->cursor_x=0;
        if(o->cursor_x>640)o->cursor_x=640;
        if(o->cursor_y<0)o->cursor_y=0;
        if(o->cursor_y>480)o->cursor_y=480;
        int cursor=Display_CursorType(o->cursor_x,o->cursor_y); const q3n_media_view *m=q3n_media_read(f->media);
        o->active_cursor=m->graphics[cursor==CURSOR_ARROW?Q3N_G_SELECT_CURSOR:Q3N_G_SIZE_CURSOR];
        Display_MouseMove(o->captured_menu>=0?&o->menus->menus[o->captured_menu]:NULL,
            o->captured_menu>=0?dx:o->cursor_x,o->captured_menu>=0?dy:o->cursor_y); }
    return q3nm_end(o,p,ok);
}
bool q3n_mission_hud_key(q3n_mission_hud *o,const q3n_frame *f,int32_t key,bool down,qa_error *e)
{
    q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false; bool ok=true;
    if(down) { const qa_q3_player *player=q3n_frame_predicted_player(f); if(!player)return q3nm_end(o,p,q3ne_fail(e,QA_ERROR_ARGUMENT,"Mission key input requires its actual predicted player"));
        int type=player->pmType;
        if(type==0||(type==2&&!o->hud->state.show_scores)) { event(o,0); ok=o->options.key_catcher(o->options.context,0,e); }
        else { Display_HandleKey(key,qtrue,o->cursor_x,o->cursor_y);
            if(o->captured_menu>=0)o->captured_menu=-1; else if(key==K_MOUSE2) { menuDef_t *m=Display_CaptureItem(o->cursor_x,o->cursor_y);
                if(m)o->captured_menu=(int)(m-o->menus->menus); } } }
    return q3nm_end(o,p,ok);
}
bool q3n_mission_hud_scroll(q3n_mission_hud *o,const q3n_frame *f,bool down,qa_error *e)
{ q3menu_context *p; if(!q3nm_begin(o,f,e,&p))return false;
    if(o->scoreboard_menu>=0&&o->hud->state.scoreboard_showing) { menuDef_t *m=&o->menus->menus[o->scoreboard_menu];
        Menu_ScrollFeeder(m,11,down); Menu_ScrollFeeder(m,5,down); Menu_ScrollFeeder(m,6,down); }
    return q3nm_end(o,p,true); }
