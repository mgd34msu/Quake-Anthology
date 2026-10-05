#include "host_menu.h"
#include "qa/network_q1.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <math.h>
#include <stdio.h>

struct frontend_host_menu {
    frontend_seat *seat;
    qa_ui *ui;
    qa_ui_library *library;
    qa_ui_id id;
    frontend_host_settings applied, draft;
    qa_ui_control controls[7];
    char port[6], status[256];
    bool busy, registered, retiring;
};
static bool fail(qa_error *error,qa_status status,const char *message)
{ return frontend_fail(error,status,message); }
static double now(const frontend_host_menu *menu)
{ return (double)menu->seat->frontend->time_ns/1000000.0; }
static bool bound(const frontend_host_menu *menu)
{
    const frontend_seat *seat=menu?menu->seat:NULL;
    const qa_frontend *f=seat?seat->frontend:NULL;
    return f && f->seats && seat->id<f->options.seats && seat==f->seats+seat->id &&
        seat->ui==menu->ui && seat->library==menu->library;
}
static const qa_product *product(const qa_launch_draft *draft)
{
    const qa_launch_choices *choices=qa_launch_draft_choices(draft);
    return choices?qa_catalog_product(qa_launch_draft_catalog(draft),choices->world.preset):NULL;
}
static bool netquake(const qa_product *game)
{ return game->family==QA_GAME_Q1 && game->edition!=QA_EDITION_QUAKEWORLD; }
static uint16_t default_port(const qa_product *game)
{
    return game->family==QA_GAME_Q3?27960:game->family==QA_GAME_Q2?
        game->edition==QA_EDITION_RERELEASE?5069:27910:
        game->edition==QA_EDITION_QUAKEWORLD?27500:26000;
}
static bool singleplayer(const qa_launch_choices *choices)
{
    for(size_t i=0;i<choices->mode_count;++i)
        if(choices->modes[i].primary_score)return choices->modes[i].rules.kind==QA_MODE_SINGLE_PLAYER;
    return !choices->mode_count || choices->modes[0].rules.kind==QA_MODE_SINGLE_PLAYER;
}
bool frontend_host_menu_read(const frontend_host_menu *menu,const qa_launch_draft *draft,
    frontend_host_settings *out,qa_error *error)
{
    if(!bound(menu) || !draft || !out || menu->retiring || !menu->registered)
        return fail(error,QA_ERROR_ARGUMENT,"Host settings require their returned physical selection owner");
    const qa_product *game=product(draft);
    if(!game)return fail(error,QA_ERROR_NOT_FOUND,"Select a game before choosing connections");
    *out=menu->applied;
    if(out->kind==FRONTEND_HOST_OFFLINE)out->port=default_port(game);
    if(!netquake(game))
        out->q1_protocol=(qa_net_protocol_id){.kind=QA_NET_NQ15};
    return true;
}
const char *frontend_host_menu_label(void *context)
{
    const frontend_host_menu *menu=context;
    return menu && menu->applied.kind!=FRONTEND_HOST_OFFLINE?"Network: hosting":"Network: local only";
}
static bool settings_fields(qa_source_save_io *io,frontend_host_settings *settings)
{
    uint32_t kind=settings->kind,protocol=settings->q1_protocol.kind;
    if(!qa_source_save_u32(io,&kind) || kind>FRONTEND_HOST_UNIFIED ||
       !qa_source_save_u16(io,&settings->port) || !settings->port ||
       !qa_source_save_u32(io,&protocol) || protocol>QA_NET_RMQ999 ||
       !qa_source_save_u32(io,&settings->q1_protocol.revision) ||
       !qa_source_save_u32(io,&settings->q1_protocol.flags))return false;
    settings->kind=(frontend_host_kind)kind;
    settings->q1_protocol.kind=(qa_net_protocol)protocol;
    return qa_net_protocol_valid(settings->q1_protocol,io->error);
}
static bool text_field(qa_source_save_io *io,char *text,size_t capacity)
{
    size_t count=io->direction==QA_SOURCE_SAVE_WRITE?strlen(text):0;
    if(!qa_source_save_count(io,&count,capacity-1) || !qa_source_save_bytes(io,text,count))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(memchr(text,0,count))return false;
        text[count]=0;
    }
    return qa_utf8_valid((qa_bytes){(const uint8_t *)text,count});
}
static bool fields(qa_source_save_io *io,const frontend_host_menu *menu,frontend_host_menu *state)
{
    uint32_t physical=menu->seat->id; uint64_t id=menu->id;
    return qa_source_save_u32(io,&physical) && physical==menu->seat->id &&
        qa_source_save_u64(io,&id) && id==menu->id &&
        settings_fields(io,&state->applied) && settings_fields(io,&state->draft) &&
        text_field(io,state->port,sizeof(state->port)) && text_field(io,state->status,sizeof(state->status));
}
bool frontend_host_menu_checkpoint(const frontend_host_menu *menu,qa_buffer *out,qa_error *error)
{
    if(!bound(menu) || menu->busy || menu->retiring || !menu->registered || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Host capture requires its returned registered physical child");
    frontend_host_menu state=*menu; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,error) && fields(&io,menu,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_host_menu_restore(frontend_host_menu *menu,qa_bytes bytes,qa_error *error)
{
    if(!bound(menu) || menu->busy || menu->retiring || !menu->registered ||
       !menu->seat->frontend->source_restoring || menu->seat->frontend->capture)
        return fail(error,QA_ERROR_ARGUMENT,"Host import requires its actual returned restoring seat");
    frontend_host_menu state={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,menu,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay) {
        menu->applied=state.applied; menu->draft=state.draft;
        memcpy(menu->port,state.port,sizeof(menu->port)); memcpy(menu->status,state.status,sizeof(menu->status));
    } else if(!error || error->code==QA_OK)fail(error,QA_ERROR_FORMAT,"Invalid host menu private choices");
    return okay;
}
static bool opened(void *context,uint32_t seat,qa_error *error)
{
    frontend_host_menu *menu=context;
    if(!bound(menu) || seat!=menu->seat->id || menu->busy || menu->retiring ||
       !frontend_host_menu_read(menu,qa_ui_library_draft(menu->library),&menu->draft,error))return false;
    snprintf(menu->port,sizeof(menu->port),"%u",(unsigned)menu->draft.port);
    menu->status[0]=0; return true;
}
static bool port_number(const char *text,uint16_t *out,qa_error *error)
{
    double value;
    if(!qa_parse_ecmascript_number((qa_bytes){(const uint8_t *)text,strlen(text)},&value,NULL) ||
       !isfinite(value) || value!=floor(value) || value<1 || value>65535)
        return fail(error,QA_ERROR_ARGUMENT,"Port must be a whole number from 1 to 65535.");
    *out=(uint16_t)value; return true;
}
static bool execute(frontend_host_menu *menu,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    if(control==1 && event->kind==QA_UI_SELECT) {
        if(event->value.row>FRONTEND_HOST_UNIFIED)return fail(error,QA_ERROR_ARGUMENT,"Unknown connection choice");
        menu->draft.kind=(frontend_host_kind)event->value.row; return true;
    }
    if(control==2 && event->kind==QA_UI_CHANGE_TEXT) {
        const char *text=event->value.text?event->value.text:"";
        if(strlen(text)>=sizeof(menu->port))return fail(error,QA_ERROR_ARGUMENT,"Port exceeds its five-character field");
        memcpy(menu->port,text,strlen(text)+1); return true;
    }
    if(control==3 && event->kind==QA_UI_SELECT) {
        static const qa_net_protocol kinds[]={QA_NET_NQ15,QA_NET_FITZ666,QA_NET_RMQ999};
        if(event->value.row>=3)return fail(error,QA_ERROR_ARGUMENT,"Unknown Quake protocol");
        menu->draft.q1_protocol=(qa_net_protocol_id){.kind=kinds[event->value.row],
            .flags=event->value.row==2?QA_Q1_INT32COORD|QA_Q1_SHORTANGLE:0};
        return true;
    }
    if(event->kind!=QA_UI_ACTIVATE)return true;
    if(control==6)return qa_ui_close(menu->ui,now(menu),error);
    if(control!=5)return true;
    frontend_host_settings applied=menu->draft;
    if(applied.kind!=FRONTEND_HOST_OFFLINE && !port_number(menu->port,&applied.port,error))return false;
    qa_launch_draft *draft=qa_ui_library_draft(menu->library);
    const qa_product *game=product(draft);
    if(!game)return fail(error,QA_ERROR_NOT_FOUND,"Select a game before choosing connections");
    if(applied.kind!=FRONTEND_HOST_OFFLINE && singleplayer(qa_launch_draft_choices(draft)) &&
       !qa_ui_library_select_mode(menu->library,game->family==QA_GAME_Q3?QA_MODE_FFA:QA_MODE_COOPERATIVE,error))return false;
    if(applied.kind!=FRONTEND_HOST_NATIVE || !netquake(game))
        applied.q1_protocol=(qa_net_protocol_id){.kind=QA_NET_NQ15};
    menu->applied=applied; menu->status[0]=0;
    return qa_ui_close(menu->ui,now(menu),error);
}
static bool action(void *context,uint32_t seat,qa_ui_id control,const qa_ui_action *event,qa_error *error)
{
    frontend_host_menu *menu=context;
    if(!bound(menu) || seat!=menu->seat->id || !event || menu->busy || menu->retiring)
        return fail(error,QA_ERROR_ARGUMENT,"Host action requires its returned physical selection owner");
    menu->busy=true; qa_error failure={0}; bool okay=execute(menu,control,event,&failure); menu->busy=false;
    if(!okay)snprintf(menu->status,sizeof(menu->status),"%s",failure.message[0]?failure.message:"Host settings rejected");
    return true;
}
static qa_ui_control button(frontend_host_menu *menu,qa_ui_id id,const char *label,unsigned row,bool enabled)
{
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect={64,118+(float)row*34,512,30},
        .enabled=enabled,.visible=true,.context=menu,.action=action};
}
static bool factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *error)
{
    frontend_host_menu *menu=context;
    if(!bound(menu) || !out || seat!=menu->seat->id || menu->retiring)
        return fail(error,QA_ERROR_ARGUMENT,"Host menu requires its actual shared selection owner");
    const qa_launch_draft *draft=qa_ui_library_draft(menu->library);
    const qa_product *game=product(draft);
    if(!game)return fail(error,QA_ERROR_NOT_FOUND,"Select a game before choosing connections");
    static const char *connections[]={"Local only","Native game clients","This client: mixed games"};
    static const char *protocols[]={"NetQuake (15)","FitzQuake (666)","RMQ (999)"};
    bool enabled=!menu->busy;
    menu->controls[0]=button(menu,1,"Connections",0,enabled);
    menu->controls[0].kind=QA_UI_CHOICE; menu->controls[0].rect=(qa_scene_rect_f){64,92,512,28};
    menu->controls[0].value.choice.labels=connections; menu->controls[0].value.choice.count=3;
    menu->controls[0].value.choice.selected=(size_t)menu->draft.kind;
    menu->controls[1]=button(menu,2,"Port",1,enabled && menu->draft.kind!=FRONTEND_HOST_OFFLINE);
    menu->controls[1].kind=QA_UI_FIELD; menu->controls[1].rect=(qa_scene_rect_f){64,120,512,28};
    menu->controls[1].value.field.text=menu->port; menu->controls[1].value.field.maximum=5;
    menu->controls[2]=button(menu,3,"Quake protocol",2,enabled);
    menu->controls[2].kind=QA_UI_CHOICE; menu->controls[2].rect=(qa_scene_rect_f){64,148,512,28};
    menu->controls[2].visible=menu->draft.kind==FRONTEND_HOST_NATIVE && netquake(game);
    menu->controls[2].value.choice.labels=protocols; menu->controls[2].value.choice.count=3;
    menu->controls[2].value.choice.selected=menu->draft.q1_protocol.kind==QA_NET_FITZ666?1:
        menu->draft.q1_protocol.kind==QA_NET_RMQ999?2:0;
    const char *notice=menu->draft.kind!=FRONTEND_HOST_OFFLINE && singleplayer(qa_launch_draft_choices(draft))?
        game->family==QA_GAME_Q3?"Uses Deathmatch. Change rules in Combat.":"Uses Co-op. Change rules in Combat.":
        "Choose game rules and difficulty in Combat.";
    menu->controls[3]=button(menu,4,notice,3,false);
    menu->controls[4]=button(menu,5,"Apply",4,enabled);
    menu->controls[5]=button(menu,6,"Back",9,enabled);
    menu->controls[6]=(qa_ui_control){.id=7,.kind=QA_UI_TEXT,.label=menu->status,
        .rect={64,450,512,0},.visible=menu->status[0]!=0,
        .value.text={.scale=1.5f,.fit_width=512,.accent=true,.source=true}};
    *out=(qa_ui_menu){.id=menu->id,.title="Host a game",.source_title=true,.controls=menu->controls,.count=7};
    return true;
}
bool frontend_host_menu_create(frontend_seat *seat,qa_ui_library *library,qa_ui_id id,
    frontend_host_menu **out,qa_error *error)
{
    qa_frontend *f=seat?seat->frontend:NULL;
    if(!f || !f->seats || seat->id>=f->options.seats || seat!=f->seats+seat->id ||
       !seat->ui || !library || seat->library!=library || !qa_ui_idle(seat->ui) || !id || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Host menu requires its actual physical seat and shared selection");
    frontend_host_menu *menu=calloc(1,sizeof(*menu));
    if(!menu)return fail(error,QA_ERROR_MEMORY,"Retaining host menu settings");
    menu->seat=seat; menu->ui=seat->ui; menu->library=library; menu->id=id;
    menu->applied=(frontend_host_settings){.kind=f->options.network_host?
        f->options.network_protocol.kind==QA_NET_UNIFIED_1?FRONTEND_HOST_UNIFIED:FRONTEND_HOST_NATIVE:FRONTEND_HOST_OFFLINE,
        .port=f->options.network_port,.q1_protocol={.kind=QA_NET_NQ15}};
    if(f->options.network_protocol.kind<=QA_NET_RMQ999)menu->applied.q1_protocol=f->options.network_protocol;
    menu->draft=menu->applied;
    *out=menu;
    qa_ui_menu_registration registration={.id=id,.context=menu,.factory=factory,.open=opened};
    if(!qa_ui_register(menu->ui,&registration,error))return false;
    menu->registered=true; return true;
}
bool frontend_host_menu_idle(const frontend_host_menu *menu)
{ return !menu || !menu->busy; }
bool frontend_host_menu_open(frontend_host_menu *menu,qa_error *error)
{
    if(!bound(menu) || menu->busy || menu->retiring || !menu->registered)
        return fail(error,QA_ERROR_ARGUMENT,"Host opening requires its registered physical owner");
    return qa_ui_open(menu->ui,menu->id,now(menu),error);
}
bool frontend_host_menu_destroy(frontend_host_menu **out,qa_error *error)
{
    if(!out)return fail(error,QA_ERROR_ARGUMENT,"Host retirement requires its retained child slot");
    frontend_host_menu *menu=*out;
    if(!menu)return true;
    if(!bound(menu) || menu->busy || !qa_ui_idle(menu->ui))
        return fail(error,QA_ERROR_ARGUMENT,"Host retirement requires its returned physical UI owner");
    menu->retiring=true;
    if(menu->registered && !qa_ui_unregister(menu->ui,menu->id,now(menu),error))return false;
    free(menu); *out=NULL; return true;
}
