#include "content_library_menu.h"
#include "config_store.h"
#include "config_scripts.h"
#include "qa/text.h"
#include "qa/network_q3.h"
#include <stdio.h>

#define LIBRARY_PAGE_FIRST 250

typedef struct library_view {
    struct library_view *next;
    char *scope,*selected;
    char query[81];
} library_view;
typedef struct library_page {
    struct frontend_content_library_menu *owner;
    frontend_library_kind kind;
    frontend_library_service service;
    library_view *views,*view;
    qa_ui_row *rows;
    size_t capacity,count,selected;
    uint64_t revision,source_revision,filter_revision;
    bool rows_ready;
    char name[129],error[256];
    qa_ui_control controls[9];
} library_page;
typedef struct local_library {
    struct frontend_content_library_menu *owner;
    frontend_library_kind kind;
    qa_ui_row *rows;
    qa_vfs_listing listing;
    char **keys;
    size_t count;
    uint64_t revision;
    char status[256];
} local_library;
struct frontend_content_library_menu {
    frontend_seat *seat;
    library_page pages[FRONTEND_LIBRARY_COUNT];
    local_library locals[2];
    qa_ui_control controls[FRONTEND_LIBRARY_COUNT+1];
    bool registered[FRONTEND_LIBRARY_COUNT],root_registered;
};
static const char *const titles[FRONTEND_LIBRARY_COUNT]={"Add-ons — Quaddicted","Demos","Movies","Configurations","Server profiles","Achievements and progress"};
static double now(const frontend_content_library_menu *o) { return (double)o->seat->frontend->time_ns/1000000.0; }
static char *copy(const char *value,qa_error *e) {
    size_t size=strlen(value)+1;char *result=malloc(size);if(!result) { frontend_fail(e,QA_ERROR_MEMORY,"Retaining content library view");return NULL; }memcpy(result,value,size);return result;
}
bool frontend_content_library_source_read(frontend_seat *seat,frontend_config_files **scripts,
    qa_command_context *command,qa_vfs **files,qa_cvars **cvars,qa_console **console,qa_error *e)
{
    if(!seat || !scripts || !command || !files || !seat->frontend->application || !seat->input)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Library source requires its actual physical seat");
    qa_frontend *f=seat->frontend;qa_application *app=f->application;
    const qa_launch_snapshot *launch=qa_application_launch(app);
    qa_command_context input=qa_input_seat_context(seat->input);
    frontend_config_source *source=NULL;
    for(size_t i=0;i<qa_launch_snapshot_instance_count(launch);++i) {
        const qa_launch_instance *instance=qa_launch_snapshot_instance(launch,i);
        frontend_config_source *candidate=frontend_config_store_named_source(f->config_store,instance->selection.instance);
        if(!candidate || !frontend_config_source_published(candidate) ||
            frontend_config_source_input(candidate,input.seat)!=seat->input)continue;
        if(!source || frontend_config_source_primary(candidate))source=candidate;
        if(frontend_config_source_primary(candidate))break;
    }
    *scripts=source?frontend_config_source_files(source):NULL;
    if(cvars)*cvars=source?frontend_config_source_cvars(source):qa_application_cvars(app);
    if(console)*console=source?frontend_config_source_console(source):qa_application_console(app);
    if(!qa_application_capture_command_context(app,&input,command,e))return false;
    *files=*scripts?frontend_config_files_source_content(*scripts):NULL;
    if(!*files)*files=qa_application_context_files(app,command,NULL);
    if(!*files) { qa_settings_store store=frontend_config_store_input_store(f->config_store);*files=store.vfs; }
    return *files!=NULL || frontend_fail(e,QA_ERROR_NOT_FOUND,"Library has no admitted source content");
}
static bool invoke(local_library *o,const char *name,const char *argument,qa_error *e)
{
    frontend_config_files *scripts;qa_command_context command;qa_vfs *files;qa_console *console;
    if(!frontend_content_library_source_read(o->owner->seat,&scripts,&command,&files,NULL,&console,e))return false;
    (void)scripts;(void)files;
    if(argument && (strpbrk(argument,"\"\n\r") || !*argument))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Library command argument contains a separator");
    size_t size=strlen(name)+(argument?strlen(argument):0)+6;char *text=malloc(size);
    if(!text)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual library command");
    if(argument)snprintf(text,size,"%s \"%s\"\n",name,argument);else snprintf(text,size,"%s\n",name);
    if(o->kind==FRONTEND_LIBRARY_MOVIES)console=qa_application_console(o->owner->seat->frontend->application);
    if(!scripts && o->kind==FRONTEND_LIBRARY_CONFIGURATIONS) {
        free(text);return frontend_fail(e,QA_ERROR_NOT_FOUND,"Select a game before using its configuration files");
    }
    bool ok=qa_console_append(console,&command,text,e);
    if(ok)snprintf(o->status,sizeof(o->status),"Queued %s",argument?argument:name);
    free(text);return ok;
}
static void local_clear(local_library *o) {
    for(size_t i=0;i<o->count;++i)free(o->keys?o->keys[i]:NULL);
    free(o->keys);free(o->rows);qa_vfs_listing_free(&o->listing);o->keys=NULL;o->rows=NULL;o->count=0;
}
static const char *local_status(void *context) { return ((local_library *)context)->status; }
static bool local_entries(void *context,const qa_ui_row **rows,size_t *count,uint64_t *revision,qa_error *e) {
    local_library *o=context;(void)e;*rows=o->rows;*count=o->count;*revision=o->revision;return true;
}
static bool append_listing(qa_vfs_listing *all,qa_vfs_listing *part,qa_error *e) {
    if(part->count>SIZE_MAX/sizeof(*all->names)-all->count)return frontend_fail(e,QA_ERROR_MEMORY,"Library listing exceeds native extent");
    char **names=realloc(all->names,(all->count+part->count)*sizeof(*names));
    if(!names && part->count)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual library listing");
    all->names=names;
    for(size_t i=0;i<part->count;++i) { size_t found=0;while(found<all->count && strcmp(all->names[found],part->names[i]))++found;
        if(found==all->count)all->names[all->count++]=part->names[i];else free(part->names[i]);part->names[i]=NULL; }
    qa_vfs_listing_free(part);return true;
}
static int by_name(const void *a,const void *b) { return strcmp(*(const char *const *)a,*(const char *const *)b); }
static bool local_refresh(void *context,qa_error *e)
{
    local_library *o=context;frontend_config_files *scripts;qa_command_context command;qa_vfs *files;qa_cvars *cvars;qa_vfs_listing listing={0};
    if(!frontend_content_library_source_read(o->owner->seat,&scripts,&command,&files,&cvars,NULL,e))return false;
    bool movie=o->kind==FRONTEND_LIBRARY_MOVIES,ok=true;
    if(movie) {
        const char *extensions[]={".roq",".cin",".ogv"};
        for(size_t i=0;ok && i<3;++i) { qa_vfs_listing part={0};ok=qa_vfs_list(files,"video",extensions[i],&part,e) && append_listing(&listing,&part,e);qa_vfs_listing_free(&part); }
    } else ok=scripts?frontend_config_files_list(scripts,&command,&listing,e):qa_vfs_list(files,"",".cfg",&listing,e);
    if(!ok) { qa_vfs_listing_free(&listing);return false; }
    qsort(listing.names,listing.count,sizeof(*listing.names),by_name);
    qa_ui_row *rows=listing.count?calloc(listing.count,sizeof(*rows)):NULL;char **keys=listing.count?calloc(listing.count,sizeof(*keys)):NULL;
    if(listing.count && (!rows || !keys)) { free(rows);free(keys);qa_vfs_listing_free(&listing);return frontend_fail(e,QA_ERROR_MEMORY,"Retaining content library rows"); }
    size_t count=0;
    for(size_t i=0;ok && i<listing.count;++i) {
        const char *name=listing.names[i];size_t length=strlen(name);keys[i]=malloc(length+(movie?7:1));
        if(!keys[i]) { ok=frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual library entry");break; }
        if(movie)snprintf(keys[i],length+7,"video/%s",name);else memcpy(keys[i],name,length+1);
        rows[i]=(qa_ui_row){.key=keys[i],.label=name,.detail=movie?"":"Selected game",.enabled=true};++count;
        if(!movie && scripts && command.origin==QA_COMMAND_SEAT) {
            qa_settings_store devices=frontend_config_files_device_store(scripts);
            char *path=malloc(length+64);
            if(!path) { ok=frontend_fail(e,QA_ERROR_MEMORY,"Reading configuration row origin");break; }
            snprintf(path,length+64,"settings/seat-%u/%s",command.seat,name);
            qa_fs_entry_kind kind;
            ok=qa_fs_root_status(qa_vfs_mount_root(devices.vfs,devices.mount),path,&kind,NULL,e);
            free(path);
            if(!ok)break;
            if(kind==QA_FS_REGULAR)rows[i].detail="Current player";
        }
        if(movie && qa_cvars_dialect(cvars)==QA_RULESET_Q3) {
            const char *leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;unsigned tier=0;
            char folded[12];size_t leaf_length=strlen(leaf);
            if(leaf_length<sizeof(folded)) {
                for(size_t j=0;j<=leaf_length;++j)folded[j]=leaf[j]>='A' && leaf[j]<='Z'?(char)(leaf[j]+('a'-'A')):leaf[j];
                if(leaf_length==9 && !strncmp(folded,"tier",4) && folded[4]>='1' && folded[4]<='7' && !strcmp(folded+5,".roq"))tier=(unsigned)(folded[4]-'0');
                else if(!strcmp(folded,"end.roq") || !strcmp(folded,"demoend.roq"))tier=8;
            }
            if(tier) {
                const qa_cvar_view *videos=qa_cvars_find(cvars,"g_spVideos");char key[16],unlocked[32]={0};qa_error info_error={0};
                snprintf(key,sizeof(key),"tier%u",tier);
                if(!videos || !qa_q3_info_value(videos->value,key,unlocked,sizeof(unlocked),&info_error) || !strtol(unlocked,NULL,10)) {
                    rows[i].enabled=false;rows[i].detail="Complete the single-player tier to unlock";
                }
            }
        }
    }
    if(!ok) { for(size_t i=0;i<count;++i)free(keys[i]);free(keys);free(rows);qa_vfs_listing_free(&listing);return false; }
    local_clear(o);o->listing=listing;o->rows=rows;o->keys=keys;o->count=count;++o->revision;
    snprintf(o->status,sizeof(o->status),"%zu %s",count,movie?"movies":"configuration files");return true;
}
static bool local_activate(void *context,const char *id,qa_error *e) {
    local_library *o=context;size_t i=0;while(i<o->count && strcmp(o->rows[i].key,id))++i;
    if(i==o->count || !o->rows[i].enabled)return frontend_fail(e,QA_ERROR_ARGUMENT,"Library entry is no longer available");
    return invoke(o,o->kind==FRONTEND_LIBRARY_MOVIES?"cinematic":"exec",id,e);
}
static bool local_create(void *context,const char *name,qa_error *e) { return invoke(context,"writeconfig",name,e); }
static bool local_stop(void *context,qa_error *e) { return invoke(context,"stopcinematic",NULL,e); }
static bool view(library_page *p,qa_error *e) {
    const char *scope=p->service.scope?p->service.scope(p->service.context):"";if(!scope)scope="";
    if(p->view && !strcmp(p->view->scope,scope))return true;
    library_view *v=p->views;while(v && strcmp(v->scope,scope))v=v->next;
    if(!v) { v=calloc(1,sizeof(*v));if(!v)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining scoped library search");v->scope=copy(scope,e);if(!v->scope) { free(v);return false; }v->next=p->views;p->views=v; }
    p->view=v;++p->revision;return true;
}
static bool matching(const qa_ui_row *row,const char *query,bool *out,qa_error *e) {
    qa_buffer lower={0},needle={0};const char *label=row->label?row->label:"",*detail=row->detail?row->detail:"";
    size_t a=strlen(label),b=strlen(detail);if(a>SIZE_MAX-b-2)return frontend_fail(e,QA_ERROR_MEMORY,"Library search extent overflows");
    char *text=malloc(a+b+2);if(!text)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining library search text");
    memcpy(text,label,a);text[a]=' ';memcpy(text+a+1,detail,b+1);
    bool ok=qa_utf8_lower((qa_bytes){(const uint8_t *)text,a+b+1},&lower,e) && qa_utf8_lower((qa_bytes){(const uint8_t *)query,strlen(query)},&needle,e);
    if(ok)*out=strstr((const char *)lower.data,(const char *)needle.data)!=NULL;
    qa_buffer_free(&lower);qa_buffer_free(&needle);free(text);return ok;
}
static bool rows(library_page *p,qa_error *e) {
    if(!view(p,e))return false;
    const qa_ui_row *entries=NULL;size_t count=0;uint64_t revision=0;
    if(!p->service.entries(p->service.context,&entries,&count,&revision,e) || (count && !entries))return false;
    if(p->rows_ready && p->source_revision==revision && p->filter_revision==p->revision)return true;
    if(count>p->capacity) { if(count>SIZE_MAX/sizeof(*p->rows))return frontend_fail(e,QA_ERROR_MEMORY,"Library rows exceed native extent");
        qa_ui_row *grown=realloc(p->rows,count*sizeof(*grown));if(!grown)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining filtered library rows");p->rows=grown;p->capacity=count; }
    p->count=0;p->selected=SIZE_MAX;
    for(size_t i=0;i<count;++i) { bool match=true;if(p->view->query[0] && !matching(entries+i,p->view->query,&match,e))return false;
        if(!match)continue;
        p->rows[p->count]=entries[i];if(p->view->selected && !strcmp(p->view->selected,entries[i].key))p->selected=p->count;++p->count; }
    if(p->selected==SIZE_MAX && p->count) { char *selected=copy(p->rows[0].key,e);if(!selected)return false;free(p->view->selected);p->view->selected=selected;p->selected=0; }
    if(!p->count) { free(p->view->selected);p->view->selected=NULL; }
    if(p->source_revision!=revision) { p->source_revision=revision;++p->revision; }
    p->filter_revision=p->revision;p->rows_ready=true;return true;
}
static bool library_action(void *context,uint32_t seat,qa_ui_id control,const qa_ui_action *event,qa_error *e) {
    library_page *p=context;(void)seat;
    if(!view(p,e))return false;
    if(control==1 && (event->kind==QA_UI_CHANGE_TEXT || event->kind==QA_UI_SUBMIT)) { snprintf(p->view->query,sizeof(p->view->query),"%s",event->value.text?event->value.text:"");++p->revision;return true; }
    if(control==6 && (event->kind==QA_UI_CHANGE_TEXT || event->kind==QA_UI_SUBMIT)) { snprintf(p->name,sizeof(p->name),"%s",event->value.text?event->value.text:"");return true; }
    if(control==2 && (event->kind==QA_UI_SELECT || event->kind==QA_UI_ROW_ACTIVATE)) {
        if(!rows(p,e) || event->value.row>=p->count)return frontend_fail(e,QA_ERROR_ARGUMENT,"Library selection left its filtered scope");
        char *selected=copy(p->rows[event->value.row].key,e);if(!selected)return false;free(p->view->selected);p->view->selected=selected;p->selected=event->value.row;
        if(event->kind==QA_UI_SELECT)return true;
        control=3;
    } else if(event->kind!=QA_UI_ACTIVATE)return true;
    if(control==9)return qa_ui_close(p->owner->seat->ui,now(p->owner),e);
    qa_error local={0};bool ok=true;
    if(control==3) { if(!rows(p,e))return false;if(p->selected<p->count && p->rows[p->selected].enabled)ok=p->service.activate(p->service.context,p->rows[p->selected].key,&local); }
    else if(control==4)ok=p->service.refresh(p->service.context,&local);
    else if(control==5 && p->service.stop)ok=p->service.stop(p->service.context,&local);
    else if(control==7 && p->service.create)ok=p->service.create(p->service.context,p->name,&local);
    snprintf(p->error,sizeof(p->error),"%s",ok?"":local.message);
    if(!ok && local.code==QA_ERROR_MEMORY) { if(e)*e=local;return false; }return true;
}
static qa_ui_control control(library_page *p,qa_ui_id id,qa_ui_control_kind kind,const char *label,qa_scene_rect_f rect,bool enabled) {
    return (qa_ui_control){.id=id,.kind=kind,.label=label,.rect=rect,.enabled=enabled,.visible=true,.context=p,.action=library_action};
}
static bool library_factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *e) {
    library_page *p=context;(void)seat;if(!rows(p,e))return false;size_t n=0;
    p->controls[n]=control(p,1,QA_UI_FIELD,"Search",(qa_scene_rect_f){48,94,544,30},true);
    p->controls[n].value.field.text=p->view->query;p->controls[n++].value.field.maximum=80;
    p->controls[n]=control(p,2,QA_UI_LIST,titles[p->kind],(qa_scene_rect_f){48,132,544,198},true);
    p->controls[n].value.list.rows=p->rows;p->controls[n].value.list.count=p->count;p->controls[n].value.list.selected=p->selected;
    p->controls[n].value.list.row_height=33;p->controls[n++].value.list.revision=p->revision;
    p->controls[n++]=control(p,3,QA_UI_BUTTON,"Open",(qa_scene_rect_f){48,338,164,30},p->selected<p->count && p->rows[p->selected].enabled);
    p->controls[n++]=control(p,4,QA_UI_BUTTON,"Refresh",(qa_scene_rect_f){222,338,164,30},true);
    if(p->service.stop)p->controls[n++]=control(p,5,QA_UI_BUTTON,p->service.stop_label,(qa_scene_rect_f){396,338,196,30},true);
    if(p->service.create) {
        p->controls[n]=control(p,6,QA_UI_FIELD,"Name",(qa_scene_rect_f){48,376,354,30},true);
        p->controls[n].value.field.text=p->name;p->controls[n++].value.field.maximum=128;
        const char *name=p->name;while(*name==' ' || *name=='\t')++name;
        p->controls[n++]=control(p,7,QA_UI_BUTTON,p->service.create_label,(qa_scene_rect_f){414,376,178,30},*name!=0);
    }
    const char *status=p->error[0]?p->error:p->service.status?p->service.status(p->service.context):"";
    if(!status || !*status)status=p->count?"":"No matching entries";
    p->controls[n++]=control(p,8,QA_UI_BUTTON,status,(qa_scene_rect_f){48,410,544,22},false);
    p->controls[n++]=control(p,9,QA_UI_BUTTON,"Back",(qa_scene_rect_f){48,438,544,30},true);
    *out=(qa_ui_menu){.id=LIBRARY_PAGE_FIRST+(qa_ui_id)p->kind,.title=titles[p->kind],.controls=p->controls,.count=n,.fullscreen=true};return true;
}
static bool library_open(void *context,uint32_t seat,qa_error *e) {
    library_page *p=context;(void)seat;qa_error local={0};bool ok=p->service.refresh(p->service.context,&local);
    snprintf(p->error,sizeof(p->error),"%s",ok?"":local.message);if(!ok && local.code==QA_ERROR_MEMORY) { if(e)*e=local;return false; }return true;
}
static bool root_action(void *context,uint32_t seat,qa_ui_id id,const qa_ui_action *event,qa_error *e) {
    frontend_content_library_menu *o=context;(void)seat;if(event->kind!=QA_UI_ACTIVATE)return true;
    return id==FRONTEND_LIBRARY_COUNT+1?qa_ui_close(o->seat->ui,now(o),e):
        qa_ui_open(o->seat->ui,LIBRARY_PAGE_FIRST+id-1,now(o),e);
}
static bool root_factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *e) {
    frontend_content_library_menu *o=context;(void)seat;(void)e;size_t n=0;
    for(size_t i=0;i<FRONTEND_LIBRARY_COUNT;++i)if(o->registered[i]) {
        o->controls[n]=(qa_ui_control){.id=i+1,.kind=QA_UI_BUTTON,.label=titles[i],.rect={64,118+(float)n*34,512,30},.enabled=true,.visible=true,.context=o,.action=root_action};++n;
    }
    o->controls[n++]=(qa_ui_control){.id=FRONTEND_LIBRARY_COUNT+1,.kind=QA_UI_BUTTON,.label="Back",.rect={64,424,512,30},.enabled=true,.visible=true,.context=o,.action=root_action};
    *out=(qa_ui_menu){.id=FRONTEND_CONTENT_LIBRARY,.title="Library",.controls=o->controls,.count=n,.fullscreen=true};return true;
}
bool frontend_content_library_menu_create(frontend_seat *seat,const frontend_library_service services[FRONTEND_LIBRARY_COUNT],frontend_content_library_menu **out,qa_error *e) {
    if(!seat || !services || !out || *out)return frontend_fail(e,QA_ERROR_ARGUMENT,"Content Library requires its actual seat and service owners");
    frontend_content_library_menu *o=calloc(1,sizeof(*o));if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining content Library menus");o->seat=seat;
    for(size_t i=0;i<FRONTEND_LIBRARY_COUNT;++i) { o->pages[i].owner=o;o->pages[i].kind=(frontend_library_kind)i;o->pages[i].service=services[i]; }
    for(size_t i=0;i<2;++i) {
        local_library *local=o->locals+i;local->owner=o;local->kind=i?FRONTEND_LIBRARY_CONFIGURATIONS:FRONTEND_LIBRARY_MOVIES;
        o->pages[local->kind].service=(frontend_library_service){.context=local,.entries=local_entries,.status=local_status,
            .refresh=local_refresh,.activate=local_activate,.create_label=i?"Save config":NULL,.create=i?local_create:NULL,.stop_label=i?NULL:"Stop movie",.stop=i?NULL:local_stop};
    }
    for(size_t i=0;i<FRONTEND_LIBRARY_COUNT;++i)if(o->pages[i].service.entries && o->pages[i].service.refresh && o->pages[i].service.activate) {
        if(!qa_ui_register(seat->ui,&(qa_ui_menu_registration){.id=LIBRARY_PAGE_FIRST+i,.context=o->pages+i,.factory=library_factory,.open=library_open},e))goto failed;
        o->registered[i]=true;
    }
    if(!qa_ui_register(seat->ui,&(qa_ui_menu_registration){.id=FRONTEND_CONTENT_LIBRARY,.context=o,.factory=root_factory},e))goto failed;
    o->root_registered=true;*out=o;return true;
failed: { qa_error cleanup={0};frontend_content_library_menu_destroy(&o,&cleanup);return false; }
}
bool frontend_content_library_menu_destroy(frontend_content_library_menu **slot,qa_error *e) {
    frontend_content_library_menu *o=slot?*slot:NULL;if(!o)return true;
    if(o->root_registered) { if(!qa_ui_unregister(o->seat->ui,FRONTEND_CONTENT_LIBRARY,now(o),e))return false;o->root_registered=false; }
    for(size_t i=0;i<FRONTEND_LIBRARY_COUNT;++i) {
        if(o->registered[i]) { if(!qa_ui_unregister(o->seat->ui,LIBRARY_PAGE_FIRST+i,now(o),e))return false;o->registered[i]=false; }
        library_view *v=o->pages[i].views;while(v) { library_view *next=v->next;free(v->scope);free(v->selected);free(v);v=next; }o->pages[i].views=o->pages[i].view=NULL;free(o->pages[i].rows);o->pages[i].rows=NULL;
    }
    for(size_t i=0;i<2;++i)local_clear(o->locals+i);
    free(o);*slot=NULL;return true;
}
