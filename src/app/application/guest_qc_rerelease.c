#include "guest_qc_rerelease.h"
#include "guest_qc_bot_orders.h"
#include "bots_npc.h"
#include "map_players_private.h"
#include "qa/network_q1_qw.h"
#include "qa/source_save.h"
#include "qa/text.h"

typedef struct qc_finale_held { qa_actor_id actor; bool held; } qc_finale_held;
typedef struct qc_prompt_choice { char *text; int32_t impulse; } qc_prompt_choice;
typedef struct qc_prompt {
    qa_actor_id actor;
    char *header;
    qc_prompt_choice *choices;
    size_t count,capacity,selected;
    uint64_t next_send;
    float old_forward,old_side;
    bool active;
} qc_prompt;
struct application_qc_rerelease {
    qc_finale_held *held;
    size_t count;
    double last_poll;
    bool has_poll,acknowledged;
    qc_prompt *prompts;
};
static bool owner_get(struct application_qc_state *engine,qa_error *error)
{
    if (engine->rerelease) return true;
    engine->rerelease=calloc(1,sizeof(*engine->rerelease));
    if (!engine->rerelease) return application_fail(error,QA_ERROR_MEMORY,"Allocating actual QC rerelease continuation");
    return true;
}
static void prompt_clear(qc_prompt *prompt,uint64_t now)
{
    free(prompt->header); prompt->header=NULL;
    if (prompt->choices)
        for (size_t i=0;i<prompt->count;++i) free(prompt->choices[i].text);
    free(prompt->choices); prompt->choices=NULL;
    prompt->active=false; prompt->count=prompt->capacity=prompt->selected=0; prompt->next_send=now;
}
static char *text_copy(const char *text,qa_error *error)
{
    size_t size=strlen(text)+1; char *copy=malloc(size);
    if (!copy) application_fail(error,QA_ERROR_MEMORY,"Retaining actual source prompt text");
    else memcpy(copy,text,size);
    return copy;
}
static bool byte_argument(qa_qc_instance *vm,uint32_t arg,uint8_t *out,qa_error *error)
{
    float value;
    if (!qa_qc_arg_float(vm,arg,&value,error) || !isfinite(value)) {
        (void)application_fail(error,QA_ERROR_FORMAT,"Rerelease byte argument is nonfinite");
        return false;
    }
    *out=(uint8_t)(uint32_t)qa_source_float_to_i32(value); return true;
}
static bool target(struct application_qc_state *engine,qa_qc_instance *vm,int32_t reference,
    bool connected,uint32_t *out,qa_error *error)
{
    qa_qc_entity_layout layout=qa_qc_default_entity_layout(engine->provider->state.qc.program,engine->profile);
    if (reference<=0 || !layout.stride_bytes || (uint32_t)reference%layout.stride_bytes)
        return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease target is not a physical source client");
    uint32_t slot=(uint32_t)reference/layout.stride_bytes;
    int32_t actual; qa_qc_slot_binding binding;
    if (!slot || slot>engine->max_clients || !qa_qc_slot_reference(vm,slot,&actual,error) || actual!=reference ||
        !qa_qc_slot(vm,slot,&binding))
        return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease target has no reserved client slot");
    const application_qc_client *client=engine->clients+slot;
    if (connected && !client->connected)
        return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease target is not a connected source client");
    if (client->connected) {
        const qa_actor_record *record=qa_actors_get(qa_session_actors(engine->services.session),client->actor);
        if (!record || (binding.kind!=QA_QC_SLOT_BORROWED && binding.kind!=QA_QC_SLOT_OWNED) ||
            !qa_actor_id_equal(binding.actor,client->actor) || binding.owner!=record->owner ||
            binding.source_slot!=(record->has_source?record->source_slot:0))
            return application_fail(error,QA_ERROR_NOT_FOUND,"Rerelease target changed its full source actor binding");
    } else if (binding.kind!=QA_QC_SLOT_FREE || client->actor.registry)
        return application_fail(error,QA_ERROR_FORMAT,"Disconnected rerelease client differs from its reserved slot");
    *out=slot; return true;
}
static bool walk_path(struct application_qc_state *engine,qa_qc_instance *vm,qa_error *error)
{
    const qa_qc_definition *self=qa_qc_program_find_global(engine->provider->state.qc.program,"self");
    int32_t reference; qa_actor_id actor; float distance; qa_vec3 goal;
    if(!self||self->type!=QA_QC_ENTITY)
        return application_fail(error,QA_ERROR_FORMAT,"QC monster path requires its actual entity self global");
    if(!qa_qc_global_int(vm,self->offset,&reference,error)||
        !qa_qc_reference_actor(vm,reference,&actor,error)||
        !qa_qc_arg_float(vm,0,&distance,error)||!qa_qc_arg_vector(vm,1,&goal,error)) return false;
    qa_q1_path_result result=QA_Q1_PATH_ERROR;
    return application_bots_npc_walk(engine->provider,actor,goal,distance,&result,error)&&
        qa_qc_return_float(vm,(float)result,error);
}
static bool set_color(struct application_qc_state *engine,qa_qc_instance *vm,qa_error *error)
{
    int32_t reference; uint8_t value; uint32_t slot;
    if (!qa_qc_arg_int(vm,0,&reference,error) || !byte_argument(vm,1,&value,error)) return false;
    qa_error local={0};
    if (!target(engine,vm,reference,false,&slot,&local)) {
        if (local.code==QA_ERROR_NOT_FOUND || local.code==QA_ERROR_FORMAT) { if (error) *error=local; return false; }
        qa_console_emit(engine->console,&engine->command_context,"tried to setcolor a non-client\n"); return true;
    }
    uint8_t top=value>>4,bottom=value&15u;
    if (top>13) top=13;
    if (bottom>13) bottom=13;
    uint8_t colors=(uint8_t)((top<<4)|bottom);
    if (!application_qc_set_float(engine,reference,"team",(float)((value&15u)+1u),error)) return false;
    application_qc_client *client=engine->clients+slot;
    if (client->colors==colors) return true;
    if (slot>256) return application_fail(error,QA_ERROR_FORMAT,"Rerelease color update exceeds actual source protocol slot");
    uint8_t bytes[3]={QA_NQ_COLORS,(uint8_t)(slot-1),colors};
    qa_application_protocol_event event={.payload={bytes,sizeof(bytes)},.destination=engine->loading?3:2,
        .reliable=!engine->loading,.signon=engine->loading};
    if (!application_emit_protocol(engine->provider,&event,error)) return false;
    client->colors=colors; return true;
}
static bool check_player_flags(struct application_qc_state *engine,qa_qc_instance *vm,qa_error *error)
{
    int32_t reference; uint32_t slot;
    if (!qa_qc_arg_int(vm,0,&reference,error)) return false;
    qa_error local={0};
    if (!target(engine,vm,reference,false,&slot,&local)) {
        if (local.code!=QA_ERROR_ARGUMENT) { if (error) *error=local; return false; }
        return qa_qc_return_float(vm,0,error);
    }
    const application_qc_client *client=engine->clients+slot;
    const char *raw="";
    if (client->connected) {
        const struct application_player_roster *roster=engine->provider->application->players;
        const application_player_record *actual=NULL;
        for (size_t i=0;roster && i<roster->count;++i)
            if (qa_actor_id_equal(roster->records[i].actor,client->actor)) {
                if (actual) return application_fail(error,QA_ERROR_FORMAT,"QC preference has duplicate canonical player owners");
                actual=roster->records+i;
            }
        if (!actual) return application_fail(error,QA_ERROR_NOT_FOUND,"QC preference has no actual canonical player owner");
        if (actual->userinfo) raw=actual->userinfo;
    }
    qa_qw_info info={0};
    if (!qa_qw_info_parse(raw,&info,error)) return false;
    const char *value=qa_qw_info_get(&info,"w_switch");
    if (!value || !*value) value=qa_qw_info_get(&info,"b_switch");
    const unsigned char *p=(const unsigned char *)(value?value:"");
    while (*p==' ' || (*p>='\t' && *p<='\r')) ++p;
    bool negative=*p=='-';
    if (*p=='+' || *p=='-') ++p;
    uint32_t number=0;
    while (*p>='0' && *p<='9') number=number*10u+(uint32_t)(*p++-'0');
    if (negative) number=0u-number;
    qa_qw_info_free(&info);
    return qa_qc_return_float(vm,!number?1:number==1?2:0,error);
}
static bool formatted_print(struct application_qc_state *engine,qa_qc_instance *vm,qa_qc_builtin builtin,qa_error *error)
{
    uint32_t first=0; const char *format; qa_actor_id actor={0};
    if (builtin!=QA_QC_BUILTIN_EX_BPRINT) {
        int32_t reference; uint32_t slot;
        if (!qa_qc_arg_int(vm,0,&reference,error) || !target(engine,vm,reference,true,&slot,error)) return false;
        actor=engine->clients[slot].actor; first=1;
    }
    uint32_t argc=qa_qc_argument_count(vm);
    if (argc<=first || !qa_qc_arg_string(vm,first,&format,error)) return false;
    qa_builtin_message_arg arguments[8]; size_t count=argc-first-1;
    if (count>sizeof(arguments)/sizeof(arguments[0]))
        return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease formatted print exceeds actual source arguments");
    for (size_t i=0;i<count;++i) {
        const char *text; arguments[i].kind=QA_BUILTIN_MESSAGE_STRING;
        if (!qa_qc_arg_string(vm,first+1+(uint32_t)i,&text,error) ||
            !qa_builtin_resource(&engine->services,text,&arguments[i].value.text,error)) return false;
    }
    qa_builtin_event event={.kind=builtin==QA_QC_BUILTIN_EX_CENTERPRINT?QA_BUILTIN_CENTERPRINT:QA_BUILTIN_MESSAGE,
        .family=QA_GAME_Q1,.provider=engine->provider->owner,.actor=actor,.flags=2,.code=2,
        .time_ns=engine->source_time_ns,.arguments=arguments,.argument_count=count};
    return qa_builtin_resource(&engine->services,format,&event.text,error) && qa_builtin_emit(&engine->services,&event,error);
}
static bool local_sound(struct application_qc_state *engine,qa_qc_instance *vm,qa_error *error)
{
    int32_t reference; uint32_t slot; const char *name; qa_qc_game_resource cached;
    if (!qa_qc_arg_int(vm,0,&reference,error) || !target(engine,vm,reference,true,&slot,error) ||
        !qa_qc_arg_string(vm,1,&name,error) ||
        !application_qc_resource_lookup(engine,QA_QC_RESOURCE_SOUND,name,false,&cached,error)) return false;
    qa_builtin_event event={.kind=QA_BUILTIN_SOUND,.family=QA_GAME_Q1,.provider=engine->provider->owner,
        .actor=engine->clients[slot].actor,.time_ns=engine->source_time_ns,.flags=2,.volume=1,.attenuation=0};
    return qa_builtin_resource(&engine->services,name,&event.resource,error) && qa_builtin_emit(&engine->services,&event,error);
}
static bool prompt_get(struct application_qc_state *engine,uint32_t slot,qc_prompt **out,qa_error *error)
{
    if (!owner_get(engine,error)) return false;
    if (!engine->rerelease->prompts) {
        engine->rerelease->prompts=calloc((size_t)engine->max_clients+1,sizeof(*engine->rerelease->prompts));
        if (!engine->rerelease->prompts) return application_fail(error,QA_ERROR_MEMORY,"Allocating physical source prompt rows");
    }
    qc_prompt *prompt=engine->rerelease->prompts+slot;
    qa_actor_id actor=engine->clients[slot].actor;
    if (!qa_actor_id_equal(prompt->actor,actor)) {
        prompt_clear(prompt,qa_session_elapsed(engine->services.session));
        prompt->actor=actor; prompt->old_forward=prompt->old_side=0;
    }
    *out=prompt; return true;
}
static bool prompt_send(struct application_qc_state *engine,qc_prompt *prompt,qa_error *error)
{
    const char *texts[4]={prompt->header?prompt->header:""," "," "," "};
    size_t count=1; const char *format="{0}";
    if (prompt->count) {
        size_t selected=prompt->selected;
        if (selected) texts[1]=prompt->choices[selected-1].text;
        texts[2]=prompt->choices[selected].text;
        if (selected+1<prompt->count) texts[3]=prompt->choices[selected+1].text;
        format="{0}\n{1}\n[[ {2} ]]\n{3}"; count=4;
    }
    qa_builtin_message_arg arguments[4];
    for (size_t i=0;i<count;++i) {
        arguments[i].kind=QA_BUILTIN_MESSAGE_STRING;
        if (!qa_builtin_resource(&engine->services,texts[i],&arguments[i].value.text,error)) return false;
    }
    qa_builtin_event event={.kind=QA_BUILTIN_CENTERPRINT,.family=QA_GAME_Q1,.provider=engine->provider->owner,
        .actor=prompt->actor,.flags=2,.time_ns=engine->source_time_ns,.arguments=arguments,.argument_count=count};
    if (!qa_builtin_resource(&engine->services,format,&event.text,error) || !qa_builtin_emit(&engine->services,&event,error)) return false;
    uint64_t now=qa_session_elapsed(engine->services.session);
    prompt->next_send=UINT64_MAX-now<UINT64_C(1000000000)?UINT64_MAX:now+UINT64_C(1000000000);
    return true;
}
static bool prompt_import(struct application_qc_state *engine,qa_qc_instance *vm,qa_qc_builtin builtin,qa_error *error)
{
    int32_t reference; uint32_t slot; qc_prompt *prompt;
    if (!qa_qc_arg_int(vm,0,&reference,error) || !target(engine,vm,reference,true,&slot,error) ||
        !prompt_get(engine,slot,&prompt,error)) return false;
    uint64_t now=qa_session_elapsed(engine->services.session);
    uint32_t argc=qa_qc_argument_count(vm);
    if (builtin==QA_QC_BUILTIN_EX_CLEARPROMPT || (builtin==QA_QC_BUILTIN_EX_PROMPT && argc<2)) {
        prompt_clear(prompt,now); return prompt_send(engine,prompt,error);
    }
    const char *text;
    if (!qa_qc_arg_string(vm,1,&text,error)) return false;
    char *copy=text_copy(text,error); if (!copy) return false;
    if (builtin==QA_QC_BUILTIN_EX_PROMPT) {
        float count=0;
        if ((argc>=3 && !qa_qc_arg_float(vm,2,&count,error)) || !isfinite(count) || count<0 || (double)count>UINT32_MAX ||
            trunc((double)count)>(double)(SIZE_MAX/sizeof(qc_prompt_choice))) {
            free(copy); return application_fail(error,QA_ERROR_ARGUMENT,"Source prompt reservation exceeds actual choices");
        }
        size_t capacity=(size_t)count;
        qc_prompt_choice *choices=capacity?calloc(capacity,sizeof(*choices)):NULL;
        if (capacity && !choices) { free(copy); return application_fail(error,QA_ERROR_MEMORY,"Reserving actual source prompt choices"); }
        prompt_clear(prompt,now); prompt->header=copy; prompt->choices=choices;
        prompt->capacity=capacity; prompt->active=true; return true;
    }
    float value;
    if (!prompt->active || !qa_qc_arg_float(vm,2,&value,error) || !isfinite(value) ||
        trunc((double)value)<INT32_MIN || trunc((double)value)>INT32_MAX) {
        free(copy); return application_fail(error,QA_ERROR_ARGUMENT,"Source prompt choice has no active prompt");
    }
    if (prompt->count==prompt->capacity) {
        size_t capacity=prompt->capacity?prompt->capacity*2:1;
        if (capacity<prompt->capacity || capacity>SIZE_MAX/sizeof(*prompt->choices)) {
            free(copy); return application_fail(error,QA_ERROR_MEMORY,"Source prompt choice count overflows");
        }
        qc_prompt_choice *choices=realloc(prompt->choices,capacity*sizeof(*choices));
        if (!choices) { free(copy); return application_fail(error,QA_ERROR_MEMORY,"Growing actual source prompt choices"); }
        prompt->choices=choices; prompt->capacity=capacity;
    }
    prompt->choices[prompt->count++]=(qc_prompt_choice){copy,(int32_t)trunc((double)value)};
    return true;
}
bool application_qc_rerelease_command(application_provider *provider,qa_actor_id actor,
    qa_usercmd *command,qa_error *error)
{
    struct application_qc_state *engine=provider && provider->kind==APPLICATION_PROVIDER_QC?provider->state.qc.engine:NULL;
    if (!engine || !command) return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease command has no actual source owner");
    if (engine->profile!=QA_QC_RERELEASE) return true;
    for (uint32_t slot=1;slot<=engine->max_clients;++slot) {
        if (!engine->clients[slot].connected || !qa_actor_id_equal(engine->clients[slot].actor,actor)) continue;
        qc_prompt *prompt; int32_t reference; uint32_t actual;
        if (!qa_qc_slot_reference(provider->state.qc.instance,slot,&reference,error) ||
            !target(engine,provider->state.qc.instance,reference,true,&actual,error) ||
            !prompt_get(engine,slot,&prompt,error)) return false;
        float forward=command->forward_move,side=command->side_move;
        if (command->kind==QA_RULESET_Q3) { forward*=320.f/127; side*=320.f/127; }
        else if (command->kind==QA_RULESET_Q2_CLASSIC || command->kind==QA_RULESET_Q2_RERELEASE) { forward*=320.f/200; side*=320.f/200; }
        if (!isfinite(forward) || !isfinite(side)) return application_fail(error,QA_ERROR_ARGUMENT,"Source prompt movement is nonfinite");
        if (prompt->active) {
            if (forward>=100 && prompt->old_forward<100) {
                prompt->selected=prompt->selected?prompt->selected-1:prompt->count?prompt->count-1:0;
                prompt->next_send=qa_session_elapsed(engine->services.session);
            } else if (forward<=-100 && prompt->old_forward>-100) {
                prompt->selected=prompt->count?(prompt->selected+1)%prompt->count:0;
                prompt->next_send=qa_session_elapsed(engine->services.session);
            } else if (side>=100 && prompt->old_side<100 && prompt->selected<prompt->count) {
                int32_t impulse=prompt->choices[prompt->selected].impulse;
                command->impulse=(uint8_t)impulse;
                if (!application_qc_set_float(engine,reference,"impulse",(float)impulse,error)) return false;
            }
            command->forward_move=command->side_move=0;
        }
        prompt->old_forward=forward; prompt->old_side=side; return true;
    }
    return application_fail(error,QA_ERROR_NOT_FOUND,"Source prompt command lost its actual connected client");
}
bool application_qc_rerelease_frame(struct application_qc_state *engine,qa_error *error)
{
    if (!engine || engine->profile!=QA_QC_RERELEASE || !engine->rerelease || !engine->rerelease->prompts) return true;
    uint64_t now=qa_session_elapsed(engine->services.session);
    for (uint32_t slot=1;slot<=engine->max_clients;++slot) {
        qc_prompt *prompt=engine->rerelease->prompts+slot;
        if (!prompt->active || !engine->clients[slot].connected || !qa_actor_id_equal(prompt->actor,engine->clients[slot].actor) || prompt->next_send>now) continue;
        int32_t reference; uint32_t actual;
        if (!qa_qc_slot_reference(engine->provider->state.qc.instance,slot,&reference,error) ||
            !target(engine,engine->provider->state.qc.instance,reference,true,&actual,error) || !prompt_send(engine,prompt,error)) return false;
    }
    return true;
}
void application_qc_rerelease_released(struct application_qc_state *engine,qa_actor_id actor)
{
    if (!engine || !engine->rerelease || !engine->rerelease->prompts) return;
    for (uint32_t slot=1;slot<=engine->max_clients;++slot) {
        qc_prompt *prompt=engine->rerelease->prompts+slot;
        if (qa_actor_id_equal(prompt->actor,actor)) { prompt_clear(prompt,qa_session_elapsed(engine->services.session)); prompt->actor=(qa_actor_id){0}; }
    }
}
static bool debug_draw(struct application_qc_state *engine,qa_qc_instance *vm,qa_qc_builtin builtin,qa_error *error)
{
    qa_builtin_event event={.kind=QA_BUILTIN_EFFECT,.family=QA_GAME_Q1,.provider=engine->provider->owner,
        .time_ns=engine->source_time_ns,.code=(int32_t)builtin};
    uint32_t color_arg=0,lifetime_arg=0,depth_arg=0; uint8_t color=0; float lifetime,depth;
    bool ok=false;
    switch (builtin) {
    case QA_QC_BUILTIN_EX_DRAW_POINT:
        ok=qa_qc_arg_vector(vm,0,&event.origin,error); event.value=4;
        color_arg=1; lifetime_arg=2; depth_arg=3; break;
    case QA_QC_BUILTIN_EX_DRAW_LINE: case QA_QC_BUILTIN_EX_DRAW_BOUNDS:
        ok=qa_qc_arg_vector(vm,0,&event.origin,error) && qa_qc_arg_vector(vm,1,&event.end,error);
        color_arg=2; lifetime_arg=3; depth_arg=4; break;
    case QA_QC_BUILTIN_EX_DRAW_ARROW:
        ok=qa_qc_arg_vector(vm,0,&event.origin,error) && qa_qc_arg_vector(vm,1,&event.end,error) &&
            qa_qc_arg_float(vm,3,&event.value,error);
        color_arg=2; lifetime_arg=4; depth_arg=5; break;
    case QA_QC_BUILTIN_EX_DRAW_RAY:
        ok=qa_qc_arg_vector(vm,0,&event.origin,error) && qa_qc_arg_vector(vm,1,&event.direction,error) &&
            qa_qc_arg_float(vm,2,&event.volume,error) && qa_qc_arg_float(vm,4,&event.value,error);
        color_arg=3; lifetime_arg=5; depth_arg=6; break;
    case QA_QC_BUILTIN_EX_DRAW_CIRCLE: case QA_QC_BUILTIN_EX_DRAW_SPHERE:
        ok=qa_qc_arg_vector(vm,0,&event.origin,error) && qa_qc_arg_float(vm,1,&event.volume,error);
        color_arg=2; lifetime_arg=3; depth_arg=4; break;
    case QA_QC_BUILTIN_EX_DRAW_CYLINDER:
        ok=qa_qc_arg_vector(vm,0,&event.origin,error) && qa_qc_arg_float(vm,1,&event.attenuation,error) &&
            qa_qc_arg_float(vm,2,&event.volume,error);
        color_arg=3; lifetime_arg=4; depth_arg=5; break;
    case QA_QC_BUILTIN_EX_DRAW_WORLDTEXT: {
        const char *text;
        ok=qa_qc_arg_string(vm,0,&text,error) && qa_builtin_resource(&engine->services,text,&event.text,error) &&
            qa_qc_arg_vector(vm,1,&event.origin,error) && qa_qc_arg_float(vm,2,&event.value,error);
        lifetime_arg=3; depth_arg=4; break;
    }
    default: return application_fail(error,QA_ERROR_ARGUMENT,"Unknown rerelease debug source import");
    }
    if (!ok || (builtin!=QA_QC_BUILTIN_EX_DRAW_WORLDTEXT && !byte_argument(vm,color_arg,&color,error)) ||
        !qa_qc_arg_float(vm,lifetime_arg,&lifetime,error) || !qa_qc_arg_float(vm,depth_arg,&depth,error)) return false;
    if (!qa_vec_finite(event.origin) || !qa_vec_finite(event.end) || !qa_vec_finite(event.direction) ||
        !isfinite(event.value) || !isfinite(event.volume) || !isfinite(event.attenuation) ||
        !isfinite(lifetime) || lifetime<0 || !isfinite(depth))
        return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease debug source arguments are nonfinite or negative");
    qa_builtin_message_arg arguments[3]={
        {.kind=QA_BUILTIN_MESSAGE_NUMBER,.value.number=color},
        {.kind=QA_BUILTIN_MESSAGE_NUMBER,.value.number=lifetime},
        {.kind=QA_BUILTIN_MESSAGE_NUMBER,.value.number=depth}};
    event.arguments=arguments; event.argument_count=3;
    return qa_builtin_resource(&engine->services,"q1:rerelease-debug",&event.resource,error) &&
        qa_builtin_emit(&engine->services,&event,error);
}
static bool finale(struct application_qc_state *engine,qa_qc_instance *vm,qa_error *error)
{
    if (!owner_get(engine,error)) return false;
    struct application_qc_rerelease *owner=engine->rerelease;
    const qa_qc_definition *time=qa_qc_program_find_global(engine->provider->state.qc.program,"time");
    float seconds;
    if (!time || time->type!=QA_QC_FLOAT || !qa_qc_global_float(vm,time->offset,&seconds,error) || !isfinite(seconds))
        return application_fail(error,QA_ERROR_FORMAT,"QC finale has no genuine source clock");
    qc_finale_held *current=calloc(engine->max_clients,sizeof(*current));
    if (!current) return application_fail(error,QA_ERROR_MEMORY,"Observing actual QC finale source clients");
    size_t count=0; bool ok=true;
    for (uint32_t slot=1;ok && slot<=engine->max_clients;++slot) {
        if (!engine->clients[slot].connected) continue;
        int32_t reference; uint32_t actual; float down;
        ok=qa_qc_slot_reference(vm,slot,&reference,error) && target(engine,vm,reference,true,&actual,error) &&
            application_qc_float(engine,reference,"button0",&down,error);
        if (ok) current[count++]=(qc_finale_held){engine->clients[slot].actor,down!=0};
    }
    if (ok) {
        bool reset=!owner->has_poll || seconds<owner->last_poll || (double)seconds-owner->last_poll>1;
        if (reset) owner->acknowledged=false;
        for (size_t i=0;i<count && !reset;++i) {
            bool held=false;
            for (size_t j=0;j<owner->count;++j)
                if (qa_actor_id_equal(current[i].actor,owner->held[j].actor)) { held=owner->held[j].held; break; }
            if (current[i].held && !held) owner->acknowledged=true;
        }
        free(owner->held); owner->held=current; owner->count=count; current=NULL;
        owner->has_poll=true; owner->last_poll=seconds;
        ok=qa_qc_return_float(vm,owner->acknowledged?1:0,error);
    }
    free(current); return ok;
}
bool application_qc_rerelease_import(struct application_qc_state *engine,qa_qc_instance *vm,qa_qc_builtin builtin,qa_error *error)
{
    if (!engine || engine->profile!=QA_QC_RERELEASE || vm!=engine->provider->state.qc.instance)
        return application_fail(error,QA_ERROR_ARGUMENT,"Rerelease import lacks its actual source owner");
    if (builtin>=QA_QC_BUILTIN_EX_DRAW_POINT && builtin<=QA_QC_BUILTIN_EX_DRAW_CYLINDER)
        return debug_draw(engine,vm,builtin,error);
    switch (builtin) {
    case QA_QC_BUILTIN_SETCOLOR: return set_color(engine,vm,error);
    case QA_QC_BUILTIN_EX_BPRINT: case QA_QC_BUILTIN_EX_SPRINT: case QA_QC_BUILTIN_EX_CENTERPRINT:
        return formatted_print(engine,vm,builtin,error);
    case QA_QC_BUILTIN_EX_LOCALSOUND: return local_sound(engine,vm,error);
    case QA_QC_BUILTIN_EX_FINALE_FINISHED: return finale(engine,vm,error);
    case QA_QC_BUILTIN_EX_CHECK_PLAYER_FLAGS: return check_player_flags(engine,vm,error);
    case QA_QC_BUILTIN_EX_WALKPATHTOGOAL: return walk_path(engine,vm,error);
    case QA_QC_BUILTIN_EX_BOT_MOVETOPOINT: return application_qc_bot_order(engine,vm,false,error);
    case QA_QC_BUILTIN_EX_BOT_FOLLOWENTITY: return application_qc_bot_order(engine,vm,true,error);
    case QA_QC_BUILTIN_EX_PROMPT: case QA_QC_BUILTIN_EX_PROMPTCHOICE: case QA_QC_BUILTIN_EX_CLEARPROMPT:
        return prompt_import(engine,vm,builtin,error);
    default: return application_fail(error,QA_ERROR_UNSUPPORTED,"Rerelease import has no installed concrete capability");
    }
}
void application_qc_rerelease_destroy(struct application_qc_state *engine)
{
    if(!engine) return;
    qa_buffer_free(&engine->npc_restore);
    if (!engine->rerelease) return;
    if (engine->rerelease->prompts) {
        for (uint32_t slot=1;slot<=engine->max_clients;++slot) prompt_clear(engine->rerelease->prompts+slot,0);
        free(engine->rerelease->prompts);
    }
    free(engine->rerelease->held); free(engine->rerelease); engine->rerelease=NULL;
}
void application_qc_rerelease_reset(struct application_qc_state *engine)
{ application_qc_rerelease_destroy(engine); }
static bool prompt_text(qa_source_save_io *io,char **owned)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && *owned;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    size_t length=reading?0:strlen(*owned);
    if (!qa_source_save_count(io,&length,reading?io->input.size-io->offset:SIZE_MAX-1)) return false;
    if (reading) {
        if (length==SIZE_MAX) return false;
        *owned=calloc(length+1,1);
        if (!*owned) return application_fail(io->error,QA_ERROR_MEMORY,"Restoring actual owned prompt text");
    }
    return qa_source_save_bytes(io,*owned,length) && !memchr(*owned,0,length);
}
static bool prompt_fields(qa_source_save_io *io,struct application_qc_state *engine,
    struct application_qc_rerelease *owner)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=owner->prompts!=NULL;
    if (!qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    uint32_t slots=engine->max_clients;
    if (!qa_source_save_u32(io,&slots) || slots!=engine->max_clients) return false;
    if (reading) {
        owner->prompts=calloc((size_t)slots+1,sizeof(*owner->prompts));
        if (!owner->prompts) return application_fail(io->error,QA_ERROR_MEMORY,"Restoring physical source prompt rows");
    }
    for (uint32_t slot=1;slot<=slots;++slot) {
        qc_prompt *prompt=owner->prompts+slot;
        if (!qa_source_save_actor(io,&prompt->actor) || !qa_source_save_bool(io,&prompt->active) ||
            !prompt_text(io,&prompt->header) || !qa_source_save_u64(io,&prompt->next_send) ||
            !qa_source_save_f32(io,&prompt->old_forward) || !qa_source_save_f32(io,&prompt->old_side) ||
            !isfinite(prompt->old_forward) || !isfinite(prompt->old_side) ||
            !qa_source_save_count(io,&prompt->capacity,SIZE_MAX/sizeof(*prompt->choices)) ||
            !qa_source_save_count(io,&prompt->count,prompt->capacity) ||
            !qa_source_save_count(io,&prompt->selected,prompt->count?prompt->count-1:0)) return false;
        if ((prompt->actor.registry && (!engine->clients[slot].connected ||
                !qa_actor_id_equal(prompt->actor,engine->clients[slot].actor))) ||
            (prompt->active && (!prompt->actor.registry || !prompt->header)) ||
            (!prompt->active && (prompt->header || prompt->count || prompt->capacity || prompt->selected)))
            return application_fail(io->error,QA_ERROR_FORMAT,"Saved prompt differs from its physical source client");
        if (reading && prompt->capacity) {
            prompt->choices=calloc(prompt->capacity,sizeof(*prompt->choices));
            if (!prompt->choices) return application_fail(io->error,QA_ERROR_MEMORY,"Restoring actual reserved prompt choices");
        }
        if (prompt->capacity && !prompt->choices) return false;
        for (size_t i=0;i<prompt->count;++i)
            if (!prompt_text(io,&prompt->choices[i].text) || !prompt->choices[i].text ||
                !qa_source_save_i32(io,&prompt->choices[i].impulse)) return false;
    }
    return true;
}
static bool fields(qa_source_save_io *io,struct application_qc_state *engine,
    struct application_qc_rerelease **out)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ,present=!reading && *out;
    uint8_t magic[4]={'Q','Q','E','X'};
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"QQEX",4) ||
        !qa_source_save_bool(io,&present)) return false;
    if (!present) return true;
    if (engine->profile!=QA_QC_RERELEASE) return false;
    if (reading) {
        *out=calloc(1,sizeof(**out));
        if (!*out) return application_fail(io->error,QA_ERROR_MEMORY,"Restoring QC finale owner");
    }
    struct application_qc_rerelease *owner=*out;
    if (!qa_source_save_bool(io,&owner->has_poll) || !qa_source_save_bool(io,&owner->acknowledged) ||
        !qa_source_save_f64(io,&owner->last_poll) || !isfinite(owner->last_poll) ||
        !qa_source_save_count(io,&owner->count,engine->max_clients) ||
        (!owner->has_poll && (owner->last_poll != 0.0 || owner->acknowledged || owner->count))) return false;
    if (reading && owner->count) {
        owner->held=calloc(owner->count,sizeof(*owner->held));
        if (!owner->held) return application_fail(io->error,QA_ERROR_MEMORY,"Restoring actual QC finale held actors");
    }
    if (owner->count && !owner->held) return false;
    for (size_t i=0;i<owner->count;++i) {
        if (!qa_source_save_actor(io,&owner->held[i].actor) || !owner->held[i].actor.registry ||
            !qa_source_save_bool(io,&owner->held[i].held)) return false;
        for (size_t j=0;j<i;++j) if (qa_actor_id_equal(owner->held[j].actor,owner->held[i].actor)) return false;
    }
    return prompt_fields(io,engine,owner);
}
bool application_qc_rerelease_checkpoint(struct application_qc_state *engine,qa_buffer *out,qa_error *error)
{
    if (!engine || !out || out->data || out->size)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC finale capture needs its actual owner and empty output");
    if(engine->npc_restore.size) return application_fail(error,QA_ERROR_ARGUMENT,"QC navigation is awaiting its actual restored source world");
    qa_source_save_io io={0}; qa_buffer npc={0};
    bool ok=application_bots_npc_capture(engine->provider,&npc,error);
    size_t length=npc.size;
    if(ok) ok=qa_source_save_writer(&io,engine->services.session,error) && fields(&io,engine,&engine->rerelease) &&
        qa_source_save_count(&io,&length,SIZE_MAX)&&qa_source_save_bytes(&io,npc.data,length)&&qa_source_save_finish(&io,out);
    qa_buffer_free(&npc);
    qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Invalid actual rerelease source continuation");
    return ok;
}
bool application_qc_rerelease_restore(struct application_qc_state *engine,qa_bytes bytes,qa_error *error)
{
    if (!engine || engine->rerelease || engine->npc_restore.data || engine->npc_restore.size)
        return application_fail(error,QA_ERROR_ARGUMENT,"QC finale restore needs its empty isolated source owner");
    struct application_qc_rerelease *owner=NULL; qa_source_save_io io={0};
    size_t length=0;
    bool ok=qa_source_save_reader(&io,engine->services.session,bytes,error) && fields(&io,engine,&owner) &&
        qa_source_save_count(&io,&length,bytes.size-io.offset);
    if(ok&&length) {
        engine->npc_restore.data=malloc(length);
        if(!engine->npc_restore.data) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining QC navigation until actual source reconnect");
        else { engine->npc_restore.size=length; ok=qa_source_save_bytes(&io,engine->npc_restore.data,length); }
    }
    if(ok) ok=length!=0&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); engine->rerelease=owner;
    if (!ok) application_qc_rerelease_destroy(engine);
    if (!ok && error && error->code==QA_OK) application_fail(error,QA_ERROR_FORMAT,"Invalid saved rerelease source continuation");
    return ok;
}
bool application_qc_npc_restore_finish(application_provider *provider,qa_error *error)
{
    struct application_qc_state *engine=provider?provider->state.qc.engine:NULL;
    if(!engine) return application_fail(error,QA_ERROR_ARGUMENT,"QC navigation reconnect requires its actual source engine");
    if(!engine->npc_restore.size) return true;
    if(!application_bots_npc_restore(provider,(qa_bytes){engine->npc_restore.data,engine->npc_restore.size},error)) return false;
    qa_buffer_free(&engine->npc_restore); return true;
}
