#include "startup_rotation.h"
#include "qa/application_q2_rotation.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <stdio.h>

struct frontend_startup_rotation {
    frontend_seat *seat;
    qa_ui *ui;
    qa_ui_id menu;
    char draft[512],status[512];
    char **maps,**labels;
    size_t count,selected;
    qa_ui_control controls[11];
    bool registered,busy,retiring;
};
static bool fail(qa_error *e,qa_status status,const char *text)
{ return frontend_fail(e,status,text); }
static bool bound(const frontend_startup_rotation *o)
{
    const frontend_seat *s=o?o->seat:NULL; const qa_frontend *f=s?s->frontend:NULL;
    return f && f->seats && s->id<f->options.seats && s==f->seats+s->id && s->ui==o->ui;
}
static double now(const frontend_startup_rotation *o)
{ return (double)o->seat->frontend->time_ns/1000000.0; }
static void clear(frontend_startup_rotation *o)
{
    for(size_t i=0;i<o->count;++i) { free(o->maps[i]); free(o->labels[i]); }
    free(o->maps); free(o->labels); o->maps=o->labels=NULL; o->count=0;
}
static bool source_read(frontend_startup_rotation *o,qa_application_q2_rotation_view *out,bool *present,qa_error *e)
{
    if(!bound(o) || o->retiring)return fail(e,QA_ERROR_ARGUMENT,"Map rotation lost its actual physical UI owner");
    qa_application *app=o->seat->frontend->application;
    const qa_launch_snapshot *snapshot=qa_application_launch(app);
    const qa_launch_binding *binding=qa_launch_binding_for(qa_launch_snapshot_choices(snapshot),
        (qa_launch_scope){.kind=QA_SCOPE_WORLD},QA_ROLE_ENTITIES,"");
    const qa_launch_instance *descriptor=binding?qa_launch_snapshot_find(snapshot,binding->instance):NULL;
    const qa_product *product=descriptor?qa_catalog_product(qa_launch_instance_catalog(descriptor),descriptor->selection.product):NULL;
    *present=false;
    if(!product || product->family!=QA_GAME_Q2)return true;
    qa_actor_owner owner;
    if(!qa_application_provider_owner(app,binding->instance,&owner) ||
       !qa_application_q2_rotation_read(app,owner,out,e))return false;
    if(out->descriptor!=descriptor || !qa_application_q2_rotation_current(out))
        return fail(e,QA_ERROR_ARGUMENT,"Map rotation changed its actual published Q2 Source");
    *present=true; return true;
}
static bool append_map(frontend_startup_rotation *o,qa_bytes text,qa_error *e)
{
    if(!text.size)return true;
    if(text.size>SIZE_MAX-32 || o->count>=SIZE_MAX/sizeof(*o->maps)-1)
        return fail(e,QA_ERROR_MEMORY,"Source rotation exceeds its actual retained extent");
    char **maps=realloc(o->maps,(o->count+1)*sizeof(*maps));
    if(!maps)return fail(e,QA_ERROR_MEMORY,"Retaining actual Source rotation entries");
    o->maps=maps;
    char **labels=realloc(o->labels,(o->count+1)*sizeof(*labels));
    if(!labels)return fail(e,QA_ERROR_MEMORY,"Retaining actual Source rotation labels");
    o->labels=labels;
    char *map=malloc(text.size+1),*label=malloc(text.size+32);
    if(!map || !label) { free(map); free(label); return fail(e,QA_ERROR_MEMORY,"Retaining an actual Source map name"); }
    memcpy(map,text.data,text.size); map[text.size]=0;
    snprintf(label,text.size+32,"%zu. %s",o->count+1,map);
    o->maps[o->count]=map; o->labels[o->count++]=label; return true;
}
static bool parse(frontend_startup_rotation *o,const qa_application_q2_rotation_view *view,qa_error *e)
{
    clear(o);
    const char *text=view->desired_maps; qa_bytes bytes={(const uint8_t *)text,strlen(text)};
    size_t cursor=0,start=0; uint32_t code;
    while(cursor<bytes.size) {
        size_t before=cursor;
        if(!qa_utf8_next(bytes,&cursor,&code))break;
        if(code!=',' && !qa_unicode_whitespace(code))continue;
        if(!append_map(o,(qa_bytes){bytes.data+start,before-start},e))return false;
        start=cursor;
    }
    if(!append_map(o,(qa_bytes){bytes.data+start,bytes.size-start},e))return false;
    if(!o->count)o->selected=0;
    else if(o->selected>=o->count)o->selected=o->count-1;
    return qa_application_q2_rotation_current(view) || fail(e,QA_ERROR_ARGUMENT,"Rotation changed while reading its actual order");
}
static bool map_name(const char *text,char out[128],qa_error *e)
{
    qa_bytes bytes={(const uint8_t *)text,strlen(text)}; size_t at=0,first=bytes.size,last=0; uint32_t point;
    while(qa_utf8_next(bytes,&at,&point))if(!qa_unicode_whitespace(point)) {
        size_t width=point<128?1:point<2048?2:point<65536?3:4;
        if(first==bytes.size)first=at-width;
        last=at;
    }
    if(first==bytes.size)first=last=0;
    size_t length=last-first;
    if(length>=5 && !memcmp(text+first,"maps/",5)) { first+=5; length-=5; }
    if(length>=4 && text[first+length-4]=='.' &&
       (text[first+length-3]=='b'||text[first+length-3]=='B') &&
       (text[first+length-2]=='s'||text[first+length-2]=='S') &&
       (text[first+length-1]=='p'||text[first+length-1]=='P'))length-=4;
    bool segment=false,valid=length>0 && length<=127;
    for(size_t i=0;valid && i<length;++i) {
        unsigned char c=(unsigned char)text[first+i];
        if(c=='/') { valid=segment; segment=false; }
        else { valid=(c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-'; segment=true; }
    }
    if(!valid || !segment)return fail(e,QA_ERROR_ARGUMENT,"Enter a map name, such as q2dm1 or q64/outpost");
    memcpy(out,text+first,length); out[length]=0; return true;
}
static bool write(frontend_startup_rotation *o,const qa_application_q2_rotation_view *view,
    const char *append,size_t omit,bool empty,qa_error *e)
{
    size_t length=append?strlen(append):0;
    for(size_t i=0;!empty && i<o->count;++i)if(i!=omit) {
        size_t amount=strlen(o->maps[i]);
        if(amount>2048 || length>2048-amount || (length && length==2048))
            return fail(e,QA_ERROR_ARGUMENT,"Map rotation exceeds its authored 2048-character setting");
        length+=amount+(length!=0);
    }
    if(length>2048)return fail(e,QA_ERROR_ARGUMENT,"Map rotation exceeds its authored 2048-character setting");
    char *value=malloc(length+1);
    if(!value)return fail(e,QA_ERROR_MEMORY,"Retaining the requested Source map order");
    size_t used=0;
    for(size_t i=0;!empty && i<o->count;++i)if(i!=omit) {
        if(used)value[used++]=' ';
        size_t amount=strlen(o->maps[i]); memcpy(value+used,o->maps[i],amount); used+=amount;
    }
    if(append) { if(used)value[used++]=' '; size_t amount=strlen(append); memcpy(value+used,append,amount); used+=amount; }
    value[used]=0;
    bool okay=qa_application_q2_rotation_maps(o->seat->frontend->application,view,value,e);
    free(value); return okay;
}
static bool execute(frontend_startup_rotation *o,qa_ui_id id,const qa_ui_action *event,qa_error *e)
{
    if(id==1 && event->kind==QA_UI_CHANGE_TEXT) {
        const char *value=event->value.text?event->value.text:""; size_t length=strlen(value);
        if(length>=sizeof(o->draft) || !qa_utf8_valid((qa_bytes){(const uint8_t *)value,length}))
            return fail(e,QA_ERROR_ARGUMENT,"Map draft exceeds its actual field extent");
        memcpy(o->draft,value,length+1); return true;
    }
    if(id==10 && event->kind==QA_UI_ACTIVATE)return qa_ui_close(o->ui,now(o),e);
    qa_application_q2_rotation_view view; bool present;
    if(!source_read(o,&view,&present,e))return false;
    if(!present)return fail(e,QA_ERROR_ARGUMENT,"This Source has no Q2 map rotation setting");
    if(!parse(o,&view,e))return false;
    if(id==3 && event->kind==QA_UI_SELECT) {
        if(event->value.row<o->count)o->selected=event->value.row;
        return true;
    }
    if(id==8 && event->kind==QA_UI_CHANGE_NUMBER)
        return qa_application_q2_rotation_shuffle(o->seat->frontend->application,&view,event->value.number!=0,e);
    if((id==1 && event->kind==QA_UI_SUBMIT) || (id==2 && event->kind==QA_UI_ACTIVATE)) {
        char name[128];
        if(!map_name(o->draft,name,e) || !write(o,&view,name,SIZE_MAX,false,e))return false;
        o->selected=o->count; o->draft[0]=0; return true;
    }
    if(event->kind!=QA_UI_ACTIVATE)return true;
    if(id==4 || id==5) {
        if((id==4 && !o->selected) || (id==5 && o->selected+1>=o->count))return true;
        size_t other=id==4?o->selected-1:o->selected+1;
        char *map=o->maps[other]; o->maps[other]=o->maps[o->selected]; o->maps[o->selected]=map;
        if(!write(o,&view,NULL,SIZE_MAX,false,e))return false;
        o->selected=other; return true;
    }
    if(id==6)return !o->count || write(o,&view,NULL,o->selected,false,e);
    if(id==7)return write(o,&view,NULL,SIZE_MAX,true,e);
    return true;
}
static bool action(void *context,uint32_t seat,qa_ui_id id,const qa_ui_action *event,qa_error *e)
{
    frontend_startup_rotation *o=context;
    if(!bound(o) || !event || seat!=o->seat->id || o->busy || o->retiring)
        return fail(e,QA_ERROR_ARGUMENT,"Rotation action requires its returned actual seat");
    o->busy=true; qa_error failure={0}; bool okay=execute(o,id,event,&failure); o->busy=false;
    if(!bound(o))return fail(e,QA_ERROR_ARGUMENT,"Rotation action changed its physical UI owner");
    snprintf(o->status,sizeof(o->status),"%s",okay?"":failure.message[0]?failure.message:"Unable to edit Source rotation");
    return true;
}
static qa_ui_control button(frontend_startup_rotation *o,qa_ui_id id,const char *label,unsigned row,bool enabled)
{
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect={64,108+(float)row*32,512,30},
        .enabled=enabled,.visible=true,.context=o,.action=action};
}
static bool factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *e)
{
    frontend_startup_rotation *o=context; qa_application_q2_rotation_view view; bool present;
    if(!out || !bound(o) || seat!=o->seat->id || !source_read(o,&view,&present,e))return false;
    if(present && !parse(o,&view,e))return false;
    if(!present)clear(o);
    static const char *empty[]={"Empty: authored exits"};
    o->controls[0]=button(o,1,"Add map",0,present); o->controls[0].kind=QA_UI_FIELD;
    o->controls[0].value.field.text=o->draft; o->controls[0].value.field.maximum=127;
    o->controls[1]=button(o,2,"Add to end",1,present);
    o->controls[2]=button(o,3,"Rotation entry",2,present && o->count); o->controls[2].kind=QA_UI_CHOICE;
    o->controls[2].value.choice.labels=o->count?(const char *const *)o->labels:empty;
    o->controls[2].value.choice.count=o->count?o->count:1; o->controls[2].value.choice.selected=o->selected;
    o->controls[3]=button(o,4,"Move earlier",3,present && o->selected>0);
    o->controls[4]=button(o,5,"Move later",4,present && o->selected+1<o->count);
    o->controls[5]=button(o,6,"Remove entry",5,present && o->count);
    o->controls[6]=button(o,7,"Use authored exits",6,present && o->count);
    o->controls[7]=button(o,8,"Shuffle after last map",7,present && view.rerelease);
    o->controls[7].kind=QA_UI_TOGGLE; o->controls[7].visible=present && view.rerelease;
    o->controls[7].value.checked=present && view.desired_shuffle;
    o->controls[8]=button(o,9,o->status[0]?o->status:present?"Applies during play. Duplicate map entries keep their order.":
        "This Source has no Q2 map rotation setting.",8,false);
    o->controls[9]=button(o,10,"Back",10,true);
    *out=(qa_ui_menu){.id=o->menu,.title="Map rotation",.controls=o->controls,.count=10};
    return !present || qa_application_q2_rotation_current(&view) || fail(e,QA_ERROR_ARGUMENT,"Rotation menu changed its actual Source");
}
bool frontend_startup_rotation_idle(const frontend_startup_rotation *o)
{ return !o || !o->busy; }
bool frontend_startup_rotation_create(frontend_seat *seat,qa_ui_id id,frontend_startup_rotation **out,qa_error *e)
{
    if(!seat || !seat->frontend || !seat->frontend->seats || seat->id>=seat->frontend->options.seats ||
       seat!=seat->frontend->seats+seat->id || !seat->ui || !qa_ui_idle(seat->ui) || !id || !out || *out)
        return fail(e,QA_ERROR_ARGUMENT,"Rotation menu requires its actual retained physical UI");
    frontend_startup_rotation *o=calloc(1,sizeof(*o));
    if(!o)return fail(e,QA_ERROR_MEMORY,"Retaining Source rotation menu");
    o->seat=seat; o->ui=seat->ui; o->menu=id; *out=o;
    if(!qa_ui_register(o->ui,&(qa_ui_menu_registration){.id=id,.context=o,.factory=factory},e))return false;
    o->registered=true; return true;
}
bool frontend_startup_rotation_destroy(frontend_startup_rotation **out,qa_error *e)
{
    if(!out)return false;
    frontend_startup_rotation *o=*out; if(!o)return true;
    if(!bound(o) || o->busy || !qa_ui_idle(o->ui))return fail(e,QA_ERROR_ARGUMENT,"Rotation retirement retains its actual UI callback");
    o->retiring=true;
    if(o->registered && !qa_ui_unregister(o->ui,o->menu,now(o),e))return false;
    clear(o); free(o); *out=NULL; return true;
}
bool frontend_startup_rotation_open(frontend_startup_rotation *o,qa_error *e)
{ return bound(o) && o->registered && !o->busy && !o->retiring && qa_ui_open(o->ui,o->menu,now(o),e); }
static bool text_fields(qa_source_save_io *io,char *text,size_t capacity)
{
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(text):0;
    if(!qa_source_save_count(io,&length,capacity-1) || !qa_source_save_bytes(io,text,length))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { if(memchr(text,0,length))return false; text[length]=0; }
    return qa_utf8_valid((qa_bytes){(const uint8_t *)text,length});
}
static bool fields(qa_source_save_io *io,const frontend_startup_rotation *o,size_t *selected,char draft[512],char status[512])
{
    uint8_t magic[4]={'Q','R','O','M'}; uint32_t physical=o->seat->id; uint64_t menu=o->menu;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QROM",4) && qa_source_save_u32(io,&physical) && physical==o->seat->id && qa_source_save_u64(io,&menu) && menu==o->menu &&
        qa_source_save_count(io,selected,SIZE_MAX) && text_fields(io,draft,512) && text_fields(io,status,512);
}
bool frontend_startup_rotation_checkpoint(const frontend_startup_rotation *o,qa_buffer *out,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring || !out || out->data || out->size)return false;
    size_t selected=o->selected; char draft[512],status[512]; memcpy(draft,o->draft,512); memcpy(status,o->status,512);
    qa_source_save_io io={0}; bool okay=qa_source_save_writer(&io,NULL,e) && fields(&io,o,&selected,draft,status) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_startup_rotation_restore(frontend_startup_rotation *o,qa_bytes bytes,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring || !o->seat->frontend->source_restoring || o->seat->frontend->capture)return false;
    size_t selected=0; char draft[512]={0},status[512]={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,o,&selected,draft,status) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(okay) { o->selected=selected; memcpy(o->draft,draft,512); memcpy(o->status,status,512); }
    return okay;
}
