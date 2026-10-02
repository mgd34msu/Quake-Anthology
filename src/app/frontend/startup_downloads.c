#include "startup_downloads.h"
#include "network_menu.h"
#include "qa/source_save.h"
#include "qa/text.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct download_row {
    qa_download_view view;
    char *path;
    char key[32],detail[128];
} download_row;
typedef struct download_context { frontend_startup_downloads *owner; qa_ui_id menu; } download_context;
typedef struct download_draft {
    char path[512],url[2049],digest[65],bytes[21],status[256];
    size_t selected;
} download_draft;
struct frontend_startup_downloads {
    frontend_seat *seat;
    qa_ui *ui;
    qa_input_seat *input;
    qa_seat_console *console;
    download_context contexts[2];
    download_draft draft;
    download_row *jobs;
    qa_ui_row *rows;
    size_t count;
    qa_download_id selected_id;
    uint64_t revision;
    char receipt[256];
    qa_ui_control controls[9];
    unsigned registered;
    bool busy,retiring;
};
static bool fail(qa_error *e,qa_status code,const char *message)
{ frontend_fail(e,code,message); return false; }
static bool bound(const frontend_startup_downloads *o)
{
    const frontend_seat *s=o?o->seat:NULL; const qa_frontend *f=s?s->frontend:NULL;
    return f && f->seats && s->id<f->options.seats && s==f->seats+s->id &&
        s->ui==o->ui && s->input==o->input && s->console==o->console;
}
static double now(const frontend_startup_downloads *o)
{ return (double)o->seat->frontend->time_ns/1000000.0; }
static bool read(frontend_startup_downloads *o,frontend_network_menu_view *out,qa_error *e)
{
    if(!bound(o) || o->retiring)return fail(e,QA_ERROR_ARGUMENT,"Downloads lost their physical menu owner");
    return frontend_network_menu_read(o->seat->frontend,o->seat->id,out,e);
}
static void clear(frontend_startup_downloads *o)
{
    for(size_t i=0;i<o->count;++i)free(o->jobs[i].path);
    free(o->jobs); free(o->rows); o->jobs=NULL; o->rows=NULL; o->count=0;
}
static const char *state(const qa_download_view *v)
{
    switch(v->state) {
    case QA_DOWNLOAD_RECEIVING:return "Downloading";
    case QA_DOWNLOAD_INSTALLING:return v->publication_pending?"Waiting to install":"Verifying";
    case QA_DOWNLOAD_COMPLETE:return v->mounted?"Installed":"Downloaded";
    case QA_DOWNLOAD_FAILED:return v->published?"Installed, activation failed":"Failed";
    case QA_DOWNLOAD_CANCELED:return "Canceled";
    }
    return "Unknown";
}
static bool refresh(frontend_startup_downloads *o,const frontend_network_menu_view *view,qa_error *e)
{
    size_t count=0;
    if(!frontend_network_menu_download_rows(o->seat->frontend,view,NULL,0,&count,e))return false;
    if(count>SIZE_MAX/sizeof(download_row) || count>SIZE_MAX/sizeof(qa_ui_row) || count>SIZE_MAX/sizeof(qa_download_view))
        return fail(e,QA_ERROR_MEMORY,"Download list exceeds its retained extent");
    download_row *jobs=count?calloc(count,sizeof(*jobs)):NULL;
    qa_ui_row *rows=count?calloc(count,sizeof(*rows)):NULL;
    qa_download_view *values=count?calloc(count,sizeof(*values)):NULL;
    bool okay=!count || (jobs && rows && values);
    if(!okay)fail(e,QA_ERROR_MEMORY,"Retaining the download list");
    size_t actual=0;
    if(okay)okay=frontend_network_menu_download_rows(o->seat->frontend,view,values,count,&actual,e) && actual==count;
    for(size_t i=0;okay && i<count;++i) {
        jobs[i].view=values[i]; size_t length=strlen(values[i].path);
        jobs[i].path=malloc(length+1);
        if(jobs[i].path)memcpy(jobs[i].path,values[i].path,length+1);
        if(!jobs[i].path) { okay=fail(e,QA_ERROR_MEMORY,"Retaining a download path"); break; }
        jobs[i].view.path=jobs[i].path;
        snprintf(jobs[i].key,sizeof(jobs[i].key),"%" PRIu64,values[i].id);
        snprintf(jobs[i].detail,sizeof(jobs[i].detail),"%s  %" PRIu64 " / %" PRIu64 " bytes",
            state(values+i),values[i].received,values[i].limit);
        rows[i]=(qa_ui_row){.key=jobs[i].key,.label=jobs[i].path,.detail=jobs[i].detail,.enabled=true};
    }
    free(values);
    if(!okay) { for(size_t i=0;jobs && i<count;++i)free(jobs[i].path); free(jobs); free(rows); return false; }
    bool changed=count!=o->count;
    for(size_t i=0;!changed && i<count;++i)changed=jobs[i].view.id!=o->jobs[i].view.id;
    if(changed)++o->revision;
    size_t selected=o->draft.selected;
    for(size_t i=0;i<count;++i)if(jobs[i].view.id==o->selected_id)selected=i;
    if(selected>=count)selected=count?count-1:0;
    clear(o); o->jobs=jobs; o->rows=rows; o->count=count; o->draft.selected=selected;
    o->selected_id=count?jobs[selected].view.id:0;
    return frontend_network_menu_current(o->seat->frontend,view) || fail(e,QA_ERROR_ARGUMENT,"Download list changed its Network owner");
}
static bool field_copy(char *destination,size_t capacity,const qa_ui_action *event,qa_error *e)
{
    const char *text=event->value.text?event->value.text:""; size_t length=strlen(text);
    if(length>=capacity || !qa_utf8_valid((qa_bytes){(const uint8_t *)text,length}))
        return fail(e,QA_ERROR_ARGUMENT,"Download field is too long or contains invalid text");
    memcpy(destination,text,length+1); return true;
}
static bool byte_count(const char *text,uint64_t *out,qa_error *e)
{
    uint64_t count=0;
    if(!*text)return fail(e,QA_ERROR_ARGUMENT,"Enter the file size in bytes");
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if(*p<'0' || *p>'9' || count>(UINT64_MAX-(uint64_t)(*p-'0'))/10)
            return fail(e,QA_ERROR_ARGUMENT,"File size must be a positive whole number of bytes");
        count=count*10+(uint64_t)(*p-'0');
    }
    if(!count || count>INT64_MAX)return fail(e,QA_ERROR_ARGUMENT,"File size is outside the supported range");
    *out=count; return true;
}
static bool execute(download_context *context,qa_ui_id id,const qa_ui_action *event,qa_error *e)
{
    frontend_startup_downloads *o=context->owner; qa_frontend *f=o->seat->frontend;
    if(context->menu==o->contexts[1].menu) {
        if(event->kind==QA_UI_CHANGE_TEXT) {
            switch(id) {
            case 1:return field_copy(o->draft.path,sizeof(o->draft.path),event,e);
            case 2:return field_copy(o->draft.url,sizeof(o->draft.url),event,e);
            case 3:return field_copy(o->draft.digest,sizeof(o->draft.digest),event,e);
            case 4:return field_copy(o->draft.bytes,sizeof(o->draft.bytes),event,e);
            default:return true;
            }
        }
        if(id==6 && event->kind==QA_UI_ACTIVATE)return qa_ui_close(o->ui,now(o),e);
        if(id!=5 || event->kind!=QA_UI_ACTIVATE)return true;
        qa_download_request request={.path=o->draft.path,.exact_identity=true};
        if(!o->draft.path[0] || !o->draft.url[0])return fail(e,QA_ERROR_ARGUMENT,"Enter a destination path and download URL");
        if(!byte_count(o->draft.bytes,&request.expected_bytes,e) || !qa_sha256_parse(o->draft.digest,&request.digest,e))return false;
        request.maximum_bytes=request.expected_bytes;
        frontend_network_menu_view view; qa_download_id job;
        if(!read(o,&view,e) || !frontend_network_menu_download_begin(f,&view,&request,o->draft.url,&job,e))return false;
        o->selected_id=job;
        if(!read(o,&view,e) || !refresh(o,&view,e))return false;
        snprintf(o->draft.status,sizeof(o->draft.status),"Download started");
        return qa_ui_close(o->ui,now(o),e);
    }
    if(id==6 && event->kind==QA_UI_ACTIVATE)return qa_ui_close(o->ui,now(o),e);
    if(id==3 && event->kind==QA_UI_ACTIVATE)return qa_ui_open(o->ui,o->contexts[1].menu,now(o),e);
    if(id==2 && event->kind==QA_UI_SELECT) {
        if(event->value.row<o->count) { o->draft.selected=event->value.row; o->selected_id=o->jobs[event->value.row].view.id; }
        return true;
    }
    frontend_network_menu_view view;
    if(!read(o,&view,e))return false;
    if(id==1 && event->kind==QA_UI_CHANGE_NUMBER) {
        frontend_network_menu_download_policy policy; bool present;
        if(!frontend_network_menu_download_policy_read(f,&view,&policy,&present,e))return false;
        if(!present)return fail(e,QA_ERROR_ARGUMENT,"No connected client owns automatic downloads");
        return frontend_network_menu_download_policy_set(f,&view,&policy,event->value.number!=0,e);
    }
    if(id==5 && event->kind==QA_UI_ACTIVATE) {
        if(!o->selected_id || !frontend_network_menu_download_release(f,&view,o->selected_id,e))return false;
        o->selected_id=0; snprintf(o->draft.status,sizeof(o->draft.status),"Finished download removed from the list");
    }
    if(id==4 && event->kind==QA_UI_ACTIVATE) {
        qa_download_view job;
        if(!o->selected_id || !frontend_network_menu_download_read(f,&view,o->selected_id,&job,e))return false;
        if(job.published || (job.state!=QA_DOWNLOAD_RECEIVING && job.state!=QA_DOWNLOAD_INSTALLING))
            return fail(e,QA_ERROR_ARGUMENT,"This download can no longer be canceled");
        if(!frontend_network_menu_download_stop(f,&view,job.id,false,e))return false;
        snprintf(o->draft.status,sizeof(o->draft.status),"Download canceled");
    }
    return true;
}
static bool action(void *context,uint32_t seat,qa_ui_id id,const qa_ui_action *event,qa_error *e)
{
    download_context *c=context; frontend_startup_downloads *o=c?c->owner:NULL;
    if(!bound(o) || !event || seat!=o->seat->id || o->busy || o->retiring)
        return fail(e,QA_ERROR_ARGUMENT,"Download action requires its physical UI owner");
    o->busy=true; qa_error failure={0}; bool okay=execute(c,id,event,&failure); o->busy=false;
    if(!bound(o))return fail(e,QA_ERROR_ARGUMENT,"Download action changed its physical UI owner");
    if(!okay)snprintf(o->draft.status,sizeof(o->draft.status),"%.*s",(int)sizeof(o->draft.status)-1,
        failure.message[0]?failure.message:"Unable to update downloads");
    return true;
}
static qa_ui_control control(download_context *c,qa_ui_id id,const char *label,float y,bool enabled)
{
    return (qa_ui_control){.id=id,.kind=QA_UI_BUTTON,.label=label,.rect={64,y,512,30},
        .enabled=enabled,.visible=true,.context=c,.action=action};
}
static bool factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *e)
{
    download_context *c=context; frontend_startup_downloads *o=c?c->owner:NULL;
    if(!out || !bound(o) || seat!=o->seat->id || o->retiring)return fail(e,QA_ERROR_ARGUMENT,"Downloads lost their physical UI owner");
    if(c->menu==o->contexts[1].menu) {
        static const char *labels[]={"Destination path under user content","HTTP or HTTPS URL","Expected SHA-256","Expected file size in bytes"};
        const char *texts[]={o->draft.path,o->draft.url,o->draft.digest,o->draft.bytes};
        const size_t limits[]={sizeof(o->draft.path)-1,sizeof(o->draft.url)-1,sizeof(o->draft.digest)-1,sizeof(o->draft.bytes)-1};
        for(unsigned i=0;i<4;++i) {
            o->controls[i]=control(c,i+1,labels[i],112+(float)i*56,true); o->controls[i].kind=QA_UI_FIELD;
            o->controls[i].value.field.text=texts[i]; o->controls[i].value.field.maximum=limits[i];
        }
        o->controls[4]=control(c,5,"Download and verify",352,true);
        o->controls[5]=control(c,6,"Back",388,true);
        o->controls[6]=control(c,7,o->draft.status[0]?o->draft.status:"Use a publisher's file size and SHA-256. Existing files are preserved.",432,false);
        *out=(qa_ui_menu){.id=c->menu,.title="Add verified download",.controls=o->controls,.count=7}; return true;
    }
    frontend_network_menu_view view; frontend_network_menu_download_policy policy; bool present;
    if(!read(o,&view,e) || !refresh(o,&view,e) ||
       !frontend_network_menu_download_policy_read(o->seat->frontend,&view,&policy,&present,e))return false;
    const qa_download_view *job=o->count?&o->jobs[o->draft.selected].view:NULL;
    o->controls[0]=control(c,1,"Allow automatic client downloads",108,present); o->controls[0].kind=QA_UI_TOGGLE;
    o->controls[0].value.checked=present && policy.allowed;
    o->controls[1]=control(c,2,o->count?"Verified package downloads":"No verified package downloads",148,o->count!=0);
    o->controls[1].kind=QA_UI_LIST; o->controls[1].rect.height=154;
    o->controls[1].value.list.rows=o->rows; o->controls[1].value.list.count=o->count;
    o->controls[1].value.list.selected=o->draft.selected; o->controls[1].value.list.row_height=38;
    o->controls[1].value.list.revision=o->revision;
    o->controls[2]=control(c,3,"Add verified download",310,true);
    o->controls[3]=control(c,4,"Cancel selected",344,job && !job->published &&
        (job->state==QA_DOWNLOAD_RECEIVING || job->state==QA_DOWNLOAD_INSTALLING));
    o->controls[3].rect.width=248;
    o->controls[4]=control(c,5,"Remove finished",344,job && !job->publication_pending &&
        (job->state==QA_DOWNLOAD_COMPLETE || job->state==QA_DOWNLOAD_FAILED || job->state==QA_DOWNLOAD_CANCELED));
    o->controls[4].rect.x=328; o->controls[4].rect.width=248;
    o->controls[5]=control(c,6,"Back",378,true);
    snprintf(o->receipt,sizeof(o->receipt),"%.*s",(int)sizeof(o->receipt)-1,
        job?job->failure.message[0]?job->failure.message:job->mounted?"Verified and installed":job->published?
        "Verified file installed; content activation is pending":state(job):present?"Select a download to see its result":
        "Connect a client to change automatic downloads");
    o->controls[6]=control(c,7,o->receipt,416,false);
    o->controls[7]=control(c,8,o->draft.status,446,false);
    *out=(qa_ui_menu){.id=c->menu,.title="Downloads",.controls=o->controls,.count=8};
    return frontend_network_menu_current(o->seat->frontend,&view) || fail(e,QA_ERROR_ARGUMENT,"Downloads changed their Network owner");
}
bool frontend_startup_downloads_idle(const frontend_startup_downloads *o)
{ return !o || !o->busy; }
bool frontend_startup_downloads_create(frontend_seat *seat,qa_ui_id jobs,qa_ui_id add,frontend_startup_downloads **out,qa_error *e)
{
    if(!seat || !seat->frontend || !seat->frontend->seats || seat->id>=seat->frontend->options.seats ||
       seat!=seat->frontend->seats+seat->id || !seat->ui || !seat->input || !seat->console || !qa_ui_idle(seat->ui) ||
       !jobs || !add || jobs==add || !out || *out)return fail(e,QA_ERROR_ARGUMENT,"Downloads require a retained physical UI");
    frontend_startup_downloads *o=calloc(1,sizeof(*o));
    if(!o)return fail(e,QA_ERROR_MEMORY,"Retaining the downloads menu");
    o->seat=seat; o->ui=seat->ui; o->input=seat->input; o->console=seat->console;
    o->contexts[0]=(download_context){o,jobs}; o->contexts[1]=(download_context){o,add}; *out=o;
    for(unsigned i=0;i<2;++i) {
        if(!qa_ui_register(o->ui,&(qa_ui_menu_registration){.id=o->contexts[i].menu,.context=o->contexts+i,.factory=factory},e))return false;
        ++o->registered;
    }
    return true;
}
bool frontend_startup_downloads_destroy(frontend_startup_downloads **out,qa_error *e)
{
    if(!out)return fail(e,QA_ERROR_ARGUMENT,"Missing downloads owner slot");
    frontend_startup_downloads *o=*out; if(!o)return true;
    if(!bound(o) || o->busy || !qa_ui_idle(o->ui))return fail(e,QA_ERROR_ARGUMENT,"Downloads retain an entered UI callback");
    o->retiring=true;
    while(o->registered) {
        if(!qa_ui_unregister(o->ui,o->contexts[o->registered-1].menu,now(o),e))return false;
        --o->registered;
    }
    clear(o); free(o); *out=NULL; return true;
}
bool frontend_startup_downloads_open(frontend_startup_downloads *o,qa_error *e)
{
    if(!bound(o) || o->registered!=2 || o->busy || o->retiring)return fail(e,QA_ERROR_ARGUMENT,"Downloads are not ready for their physical seat");
    return qa_ui_open(o->ui,o->contexts[0].menu,now(o),e);
}
static bool text_field(qa_source_save_io *io,char *text,size_t capacity)
{
    size_t size=io->direction==QA_SOURCE_SAVE_WRITE?strlen(text):0;
    if(!qa_source_save_count(io,&size,capacity-1) || !qa_source_save_bytes(io,text,size))return false;
    if(io->direction==QA_SOURCE_SAVE_READ) { if(memchr(text,0,size))return false; text[size]=0; }
    return qa_utf8_valid((qa_bytes){(const uint8_t *)text,size});
}
static bool fields(qa_source_save_io *io,const frontend_startup_downloads *o,download_draft *draft)
{
    uint8_t magic[4]={'Q','D','M','N'}; uint32_t version=1,physical=o->seat->id;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QDMN",4) || !qa_source_save_u32(io,&version) || version!=1 ||
       !qa_source_save_u32(io,&physical) || physical!=o->seat->id)return false;
    for(unsigned i=0;i<2;++i) { uint64_t menu=o->contexts[i].menu; if(!qa_source_save_u64(io,&menu) || menu!=o->contexts[i].menu)return false; }
    return qa_source_save_count(io,&draft->selected,SIZE_MAX) && text_field(io,draft->path,sizeof(draft->path)) &&
        text_field(io,draft->url,sizeof(draft->url)) && text_field(io,draft->digest,sizeof(draft->digest)) &&
        text_field(io,draft->bytes,sizeof(draft->bytes)) && text_field(io,draft->status,sizeof(draft->status));
}
bool frontend_startup_downloads_checkpoint(const frontend_startup_downloads *o,qa_buffer *out,qa_error *e)
{
    if(!bound(o) || o->registered!=2 || o->busy || o->retiring || !out || out->data || out->size)
        return fail(e,QA_ERROR_ARGUMENT,"Downloads checkpoint requires its returned UI owner");
    download_draft draft=o->draft; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,e) && fields(&io,o,&draft) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_startup_downloads_restore(frontend_startup_downloads *o,qa_bytes bytes,qa_error *e)
{
    if(!bound(o) || o->registered!=2 || o->busy || o->retiring || !o->seat->frontend->source_restoring || o->seat->frontend->capture)
        return fail(e,QA_ERROR_ARGUMENT,"Downloads import requires its isolated UI owner");
    download_draft draft={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,o,&draft) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); if(okay) { clear(o); o->draft=draft; o->selected_id=0; } return okay;
}
