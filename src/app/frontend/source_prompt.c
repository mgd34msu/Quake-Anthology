#include "source_prompt.h"
#include "ui_features.h"
#include "save_private.h"
#include "qa/application_q1_composition.h"
#include "qa/ui_preferences.h"
#include "qa/text.h"
#include <stdio.h>

typedef struct prompt_choice {
    qa_builtin_prompt_choice source;
    char *label;
} prompt_choice;
struct frontend_source_prompt {
    frontend_seat *seat;
    qa_ui *ui;
    qa_ui_id menu;
    qa_actor_owner provider;
    qa_actor_id actor;
    qa_string_id title;
    uint64_t time_ns;
    prompt_choice *choices;
    size_t count, page;
    char *shown_title, *language;
    char next_label[80];
    qa_ui_control controls[6];
    bool registered, busy, retiring, prepared, importing;
};
static bool bound(const frontend_source_prompt *o)
{
    const frontend_seat *s=o?o->seat:NULL; const qa_frontend *f=s?s->frontend:NULL;
    return f && f->application && f->seats && s->id<f->options.seats &&
        s==f->seats+s->id && s->ui==o->ui && s->input;
}
static double now(const frontend_source_prompt *o)
{ return (double)o->seat->frontend->time_ns/1000000.0; }
static char *copy(const char *text)
{
    size_t length=strlen(text); char *value=malloc(length+1);
    if(value)memcpy(value,text,length+1);
    return value;
}
static void dispose(frontend_source_prompt *o)
{
    if(o->choices)for(size_t i=0;i<o->count;++i)free(o->choices[i].label);
    free(o->choices); free(o->shown_title); free(o->language);
    o->choices=NULL; o->shown_title=o->language=NULL; o->count=o->page=0;
    o->provider=0; o->actor=(qa_actor_id){0}; o->title=0; o->time_ns=0; o->prepared=false;
}
static bool current(const frontend_source_prompt *o, bool *found, qa_error *e)
{
    *found=false;
    if(!bound(o))return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt lost its physical UI owner");
    qa_actor_id actor;
    if(!o->provider || !qa_application_provider_instance(o->seat->frontend->application,o->provider) ||
        !frontend_seat_actor_read(o->seat->frontend,o->seat->id,&actor) || !qa_actor_id_equal(actor,o->actor))return true;
    uint64_t source_time;
    return qa_application_q1_ctf_recipient_read(o->seat->frontend->application,o->provider,
        actor,&source_time,found,e);
}
bool frontend_source_prompt_supported(const frontend_source_prompt *o,qa_actor_id actor)
{
    qa_actor_id actual;
    return bound(o) && o->registered && !o->retiring && !o->importing &&
        (o->seat->builder.kind==QA_MOVEMENT_NETQUAKE || o->seat->builder.kind==QA_MOVEMENT_QUAKEWORLD) &&
        frontend_seat_actor_read(o->seat->frontend,o->seat->id,&actual) && qa_actor_id_equal(actual,actor);
}
static bool close_visible(frontend_source_prompt *o,qa_error *e)
{
    qa_ui_state state;
    return qa_ui_state_read(o->ui,&state,e) &&
        (state.menu!=o->menu || qa_ui_close(o->ui,now(o),e));
}
static bool clear(frontend_source_prompt *o,qa_error *e)
{ dispose(o); return close_visible(o,e); }
static bool choose(frontend_source_prompt *o,size_t index,qa_error *e)
{
    bool found;
    if(!current(o,&found,e))return false;
    if(!found)return clear(o,e);
    if(!o->prepared || index>=o->count)return true;
    int32_t impulse=o->choices[index].source.impulse;
    qa_actor_id actor=o->actor; qa_actor_owner provider=o->provider;
    if(!clear(o,e))return false;
    qa_actor_id actual; uint64_t source_time;
    if(!frontend_seat_actor_read(o->seat->frontend,o->seat->id,&actual) || !qa_actor_id_equal(actual,actor))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt recipient changed while closing its menu");
    if(!qa_application_q1_ctf_recipient_read(o->seat->frontend->application,provider,actor,&source_time,&found,e))return false;
    if(!found)return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt lost its source client before its impulse");
    return qa_input_command_impulse(&o->seat->builder,impulse,e);
}
static bool action(void *context,uint32_t seat,qa_ui_id id,const qa_ui_action *event,qa_error *e)
{
    frontend_source_prompt *o=context;
    if(!bound(o) || !event || seat!=o->seat->id || o->busy || o->retiring || o->importing)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt action requires its actual recipient");
    if(event->kind!=QA_UI_ACTIVATE)return true;
    o->busy=true; bool okay=true;
    if(id>=1 && id<=4)okay=choose(o,o->page*4+(size_t)id-1,e);
    else if(id==5 && o->page)o->page--;
    else if(id==6 && o->page<o->count/4 && o->page*4+4<o->count)o->page++;
    o->busy=false; return okay;
}
static bool factory(void *context,uint32_t seat,qa_ui_menu *out,qa_error *e)
{
    frontend_source_prompt *o=context;
    if(!bound(o) || !out || seat!=o->seat->id || o->retiring)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt menu lost its actual UI");
    size_t first=o->page*4, count=0;
    for(size_t i=0;i<4 && first+i<o->count && o->prepared;++i)
        o->controls[count++]=(qa_ui_control){.id=i+1,.kind=QA_UI_BUTTON,.label=o->choices[first+i].label,
            .rect={64,220+(float)i*44,512,42},.enabled=true,.visible=true,.context=o,.action=action};
    if(o->count>4 && o->prepared) {
        snprintf(o->next_label,sizeof(o->next_label),"Next (%zu/%zu)",o->page+1,(o->count-1)/4+1);
        o->controls[count++]=(qa_ui_control){.id=5,.kind=QA_UI_BUTTON,.label="Previous",.rect={64,408,248,42},
            .enabled=o->page>0,.visible=true,.context=o,.action=action};
        o->controls[count++]=(qa_ui_control){.id=6,.kind=QA_UI_BUTTON,.label=o->next_label,.rect={328,408,248,42},
            .enabled=first+4<o->count,.visible=true,.context=o,.action=action};
    }
    *out=(qa_ui_menu){.id=o->menu,.title=o->shown_title?o->shown_title:"",.source_title=true,.controls=o->controls,.count=count};
    return true;
}
bool frontend_source_prompt_idle(const frontend_source_prompt *o)
{ return !o || !o->busy; }
bool frontend_source_prompt_create(frontend_seat *seat,qa_ui_id menu,frontend_source_prompt **out,qa_error *e)
{
    if(!seat || !seat->ui || !menu || !out || *out || !qa_ui_idle(seat->ui))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt requires its retained physical UI");
    frontend_source_prompt *o=calloc(1,sizeof(*o));
    if(!o)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Source prompt menu");
    o->seat=seat; o->ui=seat->ui; o->menu=menu;
    if(!bound(o) || !qa_ui_register(o->ui,&(qa_ui_menu_registration){.id=menu,.context=o,.factory=factory},e)) { free(o); return false; }
    o->registered=true; *out=o; return true;
}
bool frontend_source_prompt_destroy(frontend_source_prompt **out,qa_error *e)
{
    if(!out)return false;
    frontend_source_prompt *o=*out; if(!o)return true;
    if(!bound(o) || o->busy || !qa_ui_idle(o->ui))return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt retains an actual UI callback");
    o->retiring=true;
    if(o->registered && !qa_ui_unregister(o->ui,o->menu,now(o),e))return false;
    dispose(o); free(o); *out=NULL; return true;
}
bool frontend_source_prompt_receive(frontend_source_prompt *o,const qa_builtin_event *event,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring || o->importing || !event)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt delivery requires its actual idle seat");
    if(event->kind!=QA_BUILTIN_SOURCE_PROMPT && event->kind!=QA_BUILTIN_CLEAR_PROMPT)return true;
    if(!frontend_source_prompt_supported(o,event->actor))return true;
    bool found=false; uint64_t source_time;
    if(event->family!=QA_GAME_Q1 || !event->provider ||
        !qa_application_q1_ctf_recipient_read(o->seat->frontend->application,event->provider,event->actor,&source_time,&found,e))return false;
    if(!found)return true;
    if(event->kind==QA_BUILTIN_CLEAR_PROMPT)return clear(o,e);
    qa_strings *strings=qa_session_strings(qa_application_session(o->seat->frontend->application));
    if(!event->text || !qa_strings_cstr(strings,event->text) ||
        (event->prompt_choice_count && !event->prompt_choices) || event->prompt_choice_count>SIZE_MAX/sizeof(prompt_choice))
        return frontend_fail(e,QA_ERROR_FORMAT,"Source prompt lost its actual title or ordered choices");
    prompt_choice *choices=event->prompt_choice_count?calloc(event->prompt_choice_count,sizeof(*choices)):NULL;
    if(event->prompt_choice_count && !choices)return frontend_fail(e,QA_ERROR_MEMORY,"Retaining Source prompt choices");
    for(size_t i=0;i<event->prompt_choice_count;++i) {
        if(!event->prompt_choices[i].label || !qa_strings_cstr(strings,event->prompt_choices[i].label) ||
            event->prompt_choices[i].impulse<1 || event->prompt_choices[i].impulse>255) {
            free(choices); return frontend_fail(e,QA_ERROR_FORMAT,"Source prompt choice has no genuine label or byte impulse");
        }
        choices[i].source=event->prompt_choices[i];
    }
    dispose(o); o->choices=choices; o->count=event->prompt_choice_count; o->provider=event->provider;
    o->actor=event->actor; o->title=event->text; o->time_ns=event->time_ns; return true;
}
static bool prepare(frontend_source_prompt *o,qa_error *e)
{
    bool found;
    if(!current(o,&found,e))return false;
    if(!found)return clear(o,e);
    qa_ui_preferences preferences;
    if(!qa_ui_preferences_read(qa_application_cvars(o->seat->frontend->application),o->seat->id,&preferences,e))return false;
    if(!o->prepared || !o->language || strcmp(o->language,preferences.language)) {
        qa_builtin_event event={.kind=QA_BUILTIN_SOURCE_PROMPT,.family=QA_GAME_Q1,.provider=o->provider,
            .actor=o->actor,.text=o->title,.time_ns=o->time_ns};
        qa_builtin_prompt_choice *source=o->count?malloc(o->count*sizeof(*source)):NULL;
        if(o->count && !source)return frontend_fail(e,QA_ERROR_MEMORY,"Reading actual Source prompt labels");
        for(size_t i=0;i<o->count;++i)source[i]=o->choices[i].source;
        event.prompt_choices=source; event.prompt_choice_count=o->count;
        char text[1024]; const char *localized=NULL;
        char *language=copy(preferences.language), *title=NULL;
        bool okay=language!=NULL || frontend_fail(e,QA_ERROR_MEMORY,"Retaining Source prompt language receipt");
        if(okay)okay=frontend_ui_source_prompt_text(o->seat->frontend,o->seat->id,&event,o->title,text,&localized,e);
        if(okay)title=copy(localized);
        char **labels=o->count?calloc(o->count,sizeof(*labels)):NULL;
        if(okay && (!title || !language || (o->count && !labels)))okay=frontend_fail(e,QA_ERROR_MEMORY,"Retaining actual Source prompt localization");
        for(size_t i=0;okay && i<o->count;++i) {
            okay=frontend_ui_source_prompt_text(o->seat->frontend,o->seat->id,&event,source[i].label,text,&localized,e);
            size_t size=okay?strlen(localized):0;
            if(okay && size>SIZE_MAX-32)okay=frontend_fail(e,QA_ERROR_MEMORY,"Source prompt label extent overflows");
            if(okay && !(labels[i]=malloc(size+32)))okay=frontend_fail(e,QA_ERROR_MEMORY,"Retaining Source prompt label");
            if(okay)snprintf(labels[i],size+32,"%zu. %s",i+1,localized);
        }
        free(source);
        if(okay)okay=current(o,&found,e);
        if(okay && !found)okay=frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt recipient changed during preparation");
        if(okay)okay=qa_ui_preferences_read(qa_application_cvars(o->seat->frontend->application),o->seat->id,&preferences,e);
        if(okay && strcmp(language,preferences.language))okay=frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt language changed during preparation");
        if(!okay) { if(labels)for(size_t i=0;i<o->count;++i)free(labels[i]); free(labels); free(title); free(language); return false; }
        free(o->shown_title); free(o->language); o->shown_title=title; o->language=language;
        for(size_t i=0;i<o->count;++i) { free(o->choices[i].label); o->choices[i].label=labels[i]; }
        free(labels); o->page=0; o->prepared=true;
    }
    return qa_input_seat_focus(o->seat->input)!=QA_INPUT_GAME || qa_ui_open(o->ui,o->menu,now(o),e);
}
bool frontend_source_prompt_prepare(frontend_source_prompt *o,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring || o->importing)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt preparation requires its returned physical seat");
    o->busy=true; bool okay=prepare(o,e); o->busy=false; return okay;
}
bool frontend_source_prompt_input(frontend_source_prompt *o,const qa_input_event *event,bool *consumed,qa_error *e)
{
    if(!consumed || !event)return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt input requires its actual event and result");
    *consumed=false;
    if(!o || o->retiring || o->importing)return true;
    if(!bound(o) || o->busy)return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt input requires its returned physical owner");
    qa_ui_state state;
    if(!qa_ui_state_read(o->ui,&state,e))return false;
    if(state.menu!=o->menu || event->kind!=QA_INPUT_EVENT_KEY || event->input.kind!=QA_PHYSICAL_KEY ||
        event->input.code<49 || event->input.code>57)return true;
    *consumed=true;
    if(!event->down || event->repeat)return true;
    o->busy=true; bool okay=choose(o,(size_t)event->input.code-49,e); o->busy=false; return okay;
}
static bool saved_text(qa_source_save_io *io,char **text)
{
    return qa_source_save_owned_text(io,text) && (!*text || qa_utf8_valid((qa_bytes){(const uint8_t *)*text,strlen(*text)}));
}
static bool fields(qa_source_save_io *io,frontend_source_prompt *o)
{
    uint8_t magic[4]={'Q','S','P','M'}; uint32_t physical=o->seat->id; uint64_t menu=o->menu;
    bool pending=io->direction==QA_SOURCE_SAVE_WRITE && o->provider!=0;
    if(!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QSPM",4) ||
        !qa_source_save_u32(io,&physical) || physical!=o->seat->id ||
        !qa_source_save_u64(io,&menu) || menu!=o->menu || !qa_source_save_bool(io,&pending))return false;
    if(!pending)return true;
    if(!frontend_save_provider(io,o->seat->frontend->application,&o->provider) || !o->provider ||
        !qa_source_save_actor(io,&o->actor) || !o->actor.registry ||
        !qa_source_save_string(io,&o->title) || !o->title || !qa_source_save_u64(io,&o->time_ns) ||
        !qa_source_save_bool(io,&o->prepared) ||
        !qa_source_save_count(io,&o->page,SIZE_MAX/4) ||
        !qa_source_save_count(io,&o->count,io->direction==QA_SOURCE_SAVE_READ?
            (io->input.size-io->offset)/6:SIZE_MAX/sizeof(*o->choices)) ||
        o->count>SIZE_MAX/sizeof(*o->choices) || (o->count?o->page>(o->count-1)/4:o->page!=0))return false;
    if(io->direction==QA_SOURCE_SAVE_READ && o->count) {
        o->choices=calloc(o->count,sizeof(*o->choices));
        if(!o->choices)return frontend_fail(io->error,QA_ERROR_MEMORY,"Restoring actual Source prompt choices");
    }
    if(!saved_text(io,&o->shown_title) || !saved_text(io,&o->language) ||
        (o->prepared?(!o->shown_title || !o->language):(o->shown_title || o->language || o->page)))return false;
    for(size_t i=0;i<o->count;++i) {
        prompt_choice *choice=o->choices+i;
        if(!qa_source_save_string(io,&choice->source.label) || !choice->source.label ||
            !qa_source_save_i32(io,&choice->source.impulse) || choice->source.impulse<1 || choice->source.impulse>255 ||
            !saved_text(io,&choice->label) || (o->prepared?!choice->label:choice->label!=NULL))return false;
    }
    return true;
}
bool frontend_source_prompt_checkpoint(const frontend_source_prompt *o,qa_buffer *out,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring || o->importing ||
        !out || out->data || out->size)return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt capture requires its actual idle owner");
    frontend_source_prompt saved=*o; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,qa_application_session(o->seat->frontend->application),e) &&
        fields(&io,&saved) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if(!okay && e && e->code==QA_OK)frontend_fail(e,QA_ERROR_FORMAT,"Invalid actual Source prompt continuation");
    return okay;
}
bool frontend_source_prompt_restore(frontend_source_prompt *o,qa_bytes bytes,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring ||
        !o->seat->frontend->source_restoring || o->seat->frontend->capture)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt import requires its isolated prepared seat");
    frontend_source_prompt saved={.seat=o->seat,.ui=o->ui,.menu=o->menu}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,qa_application_session(o->seat->frontend->application),bytes,e) &&
        fields(&io,&saved) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!okay) { dispose(&saved); if(e && e->code==QA_OK)frontend_fail(e,QA_ERROR_FORMAT,"Invalid saved Source prompt continuation"); return false; }
    dispose(o);
    o->provider=saved.provider; o->actor=saved.actor; o->title=saved.title; o->time_ns=saved.time_ns;
    o->choices=saved.choices; o->count=saved.count; o->page=saved.page; o->shown_title=saved.shown_title;
    o->language=saved.language; o->prepared=saved.prepared; o->importing=true; return true;
}
bool frontend_source_prompt_restore_finish(frontend_source_prompt *o,qa_error *e)
{
    if(!bound(o) || !o->registered || o->busy || o->retiring || !o->importing)
        return frontend_fail(e,QA_ERROR_ARGUMENT,"Source prompt finish requires its imported physical owner");
    bool found=false;
    if(o->provider) {
        if(!current(o,&found,e))return false;
        if(!found)return frontend_fail(e,QA_ERROR_FORMAT,"Saved Source prompt has no exact published source recipient");
    }
    o->importing=false; return true;
}
