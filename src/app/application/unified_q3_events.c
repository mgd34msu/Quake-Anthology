#include "unified_q3_events.h"
#include "unified_events.h"
#include "guest_q3_components.h"
#include "guest_q3_private.h"
#include "native_q3_wire_state.h"
#include "bots_private.h"
#include "bots_transport.h"
#include "event_stream.h"
#include <limits.h>

void application_q3_command_init(application_q3_command_cursor *cursor,qa_application *app)
{ *cursor=(application_q3_command_cursor){.application=app}; }
void application_q3_command_clear(application_q3_command_cursor *cursor)
{
    qa_application *app=cursor->application;
    qa_application_events_unsubscribe(&cursor->delivery);
    size_t kept=0;
    for(size_t i=0;app && i<app->unified_persistent_count;++i) {
        application_unified_persistent_event row=app->unified_persistent[i];
        if(row.command && row.command->stream==cursor->serial) qa_event_lease_release(row.lease);
        else app->unified_persistent[kept++]=row;
    }
    if(app) app->unified_persistent_count=kept;
    application_q3_command_init(cursor,app);
}
void application_q3_command_move(application_q3_command_cursor *to,application_q3_command_cursor *from)
{
    if(to==from) return;
    application_q3_command_clear(to);
    *to=*from;
    if(from->delivery.application) {
        qa_application_event_subscription **at=&from->delivery.application->event_subscribers;
        while(*at && *at!=&from->delivery) at=&(*at)->following;
        if(*at) *at=&to->delivery;
    }
    application_q3_command_init(from,from->application);
}
static const application_unified_persistent_event *command_retained(
    const application_q3_command_cursor *cursor,int32_t sequence)
{
    qa_application_q3_command_event command={.stream=cursor->serial,.sequence=sequence};
    application_persistent_key key;
    application_q3_command_key(&command,&key);
    for(size_t i=0;i<cursor->application->unified_persistent_count;++i) {
        const application_unified_persistent_event *row=&cursor->application->unified_persistent[i];
        if(row->command && row->command->sequence==sequence &&
            application_unified_persistent_key_equal(&row->key,&key)) return row;
    }
    return NULL;
}
static bool command_admit(application_q3_command_cursor *cursor,int32_t sequence,
    const char *text,qa_error *error)
{
    qa_application *app=cursor->application;
    if(!cursor->serial) cursor->serial=++app->q3_command_serial;
    bool pending=sequence>cursor->acknowledged;
    bool subscribed=cursor->delivery.application!=NULL;
    if(pending && !subscribed) qa_application_events_subscribe(app,&cursor->delivery);
    application_event_write write;
    if(!application_event_stream_begin(app,QA_APPLICATION_EVENT_Q3_COMMAND,&write,error)) goto blocked;
    size_t bytes=strlen(text)+1;
    char *copy=application_event_stream_alloc(app,bytes,1,error);
    if(!copy) { application_event_stream_abort(app,&write,error); goto blocked; }
    memcpy(copy,text,bytes);
    write.envelope->raw.q3_command=(qa_application_q3_command_event){cursor->serial,sequence,copy};
    if(application_event_stream_commit(app,&write,error)) return true;
blocked:
    if(pending && !subscribed) qa_application_events_unsubscribe(&cursor->delivery);
    return false;
}
bool application_q3_command_admit(application_q3_command_cursor *cursor,const char *text,uint32_t config_index,uint32_t fragment,bool *overflow,qa_error *error)
{
    *overflow=cursor->sequence==INT32_MAX || (int64_t)cursor->sequence-cursor->acknowledged>=QA_Q3_RELIABLE;
    if(*overflow) return true;
    if(config_index!=UINT32_MAX) {
        qa_q3_configstring_command(config_index,text,fragment,cursor->formatted);
        text=cursor->formatted;
    }
    if(!command_admit(cursor,cursor->sequence+1,text,error)) {
        if(error && error->code) return false;
        *overflow=true; return true;
    }
    ++cursor->sequence;
    return true;
}
bool application_q3_command_admit_received(application_q3_command_cursor *cursor,
    const qa_q3_command *command,qa_error *error)
{
    if(command->sequence<=cursor->sequence) return true;
    if(!command_admit(cursor,command->sequence,command->text,error)) return false;
    cursor->sequence=command->sequence; return true;
}
bool application_q3_command_lookup(application_q3_command_cursor *cursor,int32_t sequence,
    const char **text,qa_error *error)
{
    *text="";
    if(sequence>cursor->sequence)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 server command is in the future");
    if(sequence<=0) return true;
    if((int64_t)cursor->sequence-sequence>=QA_Q3_RELIABLE)
        return application_fail(error,QA_ERROR_FORMAT,"Q3 server command has left its retained window");
    if(cursor->delivery.application) {
        qa_application_event_range events={.next=cursor->delivery.next,
            .end=qa_application_events_next(cursor->application)};
        qa_application_event_view view;
        while(qa_application_event_raw_next(cursor->application,&events,
            UINT64_C(1)<<QA_APPLICATION_EVENT_Q3_COMMAND,&view)) {
            if(view.value.q3_command->stream==cursor->serial &&
                view.value.q3_command->sequence==sequence) {
                *text=view.value.q3_command->text; return true;
            }
        }
    }
    const application_unified_persistent_event *row=command_retained(cursor,sequence);
    if(row) *text=row->command->text;
    return true;
}
static int32_t command_slot_sequence(const application_q3_command_cursor *cursor,size_t slot)
{
    int64_t sequence=(int64_t)cursor->sequence-
        (((uint32_t)cursor->sequence-(uint32_t)slot)&(QA_Q3_RELIABLE-1));
    return sequence>0?(int32_t)sequence:0;
}
bool application_q3_command_lookup_slot(application_q3_command_cursor *cursor,size_t slot,
    const char **text,qa_error *error)
{
    return application_q3_command_lookup(cursor,command_slot_sequence(cursor,slot),text,error);
}
bool application_q3_command_restore_slot(application_q3_command_cursor *cursor,size_t slot,
    const char *text,qa_error *error)
{
    int32_t sequence=command_slot_sequence(cursor,slot);
    return sequence<=0 || !*text || command_admit(cursor,sequence,text,error);
}
bool application_q3_command_ack(application_q3_command_cursor *cursor,int32_t sequence,
    bool *clamped,qa_error *error)
{
    if(sequence<0 || sequence>cursor->sequence)
        return application_fail(error,QA_ERROR_FORMAT,"Invalid Q3 command acknowledgement");
    *clamped=(int64_t)sequence<(int64_t)cursor->sequence-QA_Q3_RELIABLE;
    if(*clamped) return application_fail(error,QA_ERROR_FORMAT,"Stale Q3 command acknowledgement");
    if(sequence>cursor->acknowledged) cursor->acknowledged=sequence;
    if(cursor->acknowledged==cursor->sequence) qa_application_events_unsubscribe(&cursor->delivery);
    else if(cursor->delivery.application) {
        const application_unified_persistent_event *row=command_retained(cursor,cursor->acknowledged);
        uint64_t id=row?qa_event_lease_id(row->lease):0;
        if(row && qa_event_ring_at(cursor->application->event_ring,id)==qa_event_lease_record(row->lease) &&
            id>=cursor->delivery.next) qa_application_events_consumed(&cursor->delivery,id+1);
    }
    return true;
}

static bool source(application_provider *p, qa_error *e)
{
    return (p && p->application && !p->application->destroy_requested &&
        !p->close_pending && p->launch && p->product &&
        ((p->kind == APPLICATION_PROVIDER_Q3 && p->state.q3) || q3g_engine(p))) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Q3 presentation lost its genuine GAME owner");
}

static bool source_time(application_provider *p, int32_t *time, qa_error *e)
{
    if (p->kind == APPLICATION_PROVIDER_Q3) return qa_q3_source_clock(p->state.q3, time, e);
    struct application_q3_guest *guest=q3g_engine(p);
    *time=guest ? guest->milliseconds : (int32_t)(uint32_t)(qa_session_elapsed(p->application->session)/UINT64_C(1000000));
    return true;
}

bool application_unified_q3_attack_providers(void *context, qa_actor_id attacker,
    qa_item_id weapon, qa_actor_owner *inventory, qa_actor_owner *movement, qa_error *e)
{
    application_provider *p = context;
    (void)weapon;
    if (!inventory || !movement || !source(p, e)) return false;
    application_provider *items = application_provider_for(p->application, attacker, QA_ROLE_INVENTORY, "");
    application_provider *motion = application_provider_for(p->application, attacker, QA_ROLE_MOVEMENT, "");
    if (!items || !motion || !items->constructed || !motion->constructed ||
        !items->attached || !motion->attached || items->close_pending || motion->close_pending)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 attack lost its selected inventory or movement source");
    *inventory = items->owner;
    *movement = motion->owner;
    return true;
}

static bool source_emit(application_provider *p, const qa_unified_q3_event *event,
    uint64_t ns, qa_error *e)
{
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_Q3, .value.q3 = *event};
    return application_unified_event_emit(p->application, p->owner, &payload, NULL,
        (qa_actor_id){0}, (qa_actor_id){0}, ns, 0, false, false, e);
}

static bool component_emit(qa_application *app, const application_q3_component_publication *p,
    const qa_unified_q3_event *event, qa_actor_id recipient, int32_t time, qa_error *e)
{
    if (!app || !p || !p->owner || !p->descriptor || !p->content || !p->game || !p->source ||
        !p->identity || !p->metadata || !p->generation)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 component event lost its admitted physical Source");
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_Q3, .value.q3 = *event};
    return application_unified_event_emit(app, p->owner, &payload, NULL, recipient,
        (qa_actor_id){0}, (uint64_t)(uint32_t)time * UINT64_C(1000000), 0, false, false, e);
}

bool application_unified_q3_component_player(qa_application *app, qa_actor_owner owner,
    const application_q3_scene_player_event *event, qa_error *e)
{
    application_q3_component_publication source;
    if (!application_q3_components_event_source_read(app, owner, &source, e)) return false;
    const application_q3_component_publication *p = &source;
    if (!app || !p || !event || event->time_ms < 0 || !event->actor.registry || !p->product || !p->metadata)
        return application_fail(e, QA_ERROR_ARGUMENT, "Original player event lost its actual component delivery");
    if (!p->module || !p->module->id || !p->module->artifact_path)
        return application_fail(e, QA_ERROR_ARGUMENT, "Original player event lost its admitted module identity owner");
    qa_unified_q3_event value = {.kind = QA_UNIFIED_Q3_PLAYER_EVENT, .actor = event->actor,
        .player = event->player, .event = event->event, .parameter = event->parameter,
        .external = event->external, .source_sequence = event->source_sequence,
        .origin = event->origin, .time_ms = event->time_ms, .abi = p->abi,
        .module = *p->module};
    return component_emit(app, p, &value, (qa_actor_id){0}, event->time_ms, e);
}

bool application_unified_q3_component_output(qa_application *app, qa_actor_owner owner,
    const qa_unified_q3_event *event, qa_actor_id recipient, int32_t time, qa_error *e)
{
    application_q3_component_publication source;
    return application_q3_components_event_source_read(app, owner, &source, e) &&
        component_emit(app, &source, event, recipient, time, e);
}

bool application_unified_q3_text(application_provider *p, application_unified_q3_text_kind kind,
    int32_t client, const char *value, qa_error *e)
{
    if(kind==APPLICATION_Q3_SOURCE_RESTART) value="map_restart\n";
    if ((unsigned)kind > APPLICATION_Q3_SOURCE_RESTART || !value || !p || !p->application)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 engine event lost its authored text");
    static const qa_unified_q3_event_kind kinds[] = {QA_UNIFIED_Q3_SERVER_COMMAND,
        QA_UNIFIED_Q3_DROP_CLIENT,QA_UNIFIED_Q3_SERVER_COMMAND};
    int32_t time;
    if (!source_time(p, &time, e)) return false;
    qa_unified_q3_event event = {.kind = kinds[kind], .client = client, .text = (char *)value,
        .round_restart=kind==APPLICATION_Q3_SOURCE_RESTART};
    return source_emit(p, &event,
        (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
}

bool application_unified_q3_configstring(application_provider *p, uint32_t index, const char *value, qa_error *e)
{
    if (index >= QA_Q3_CONFIGSTRINGS || !value || !source(p, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 source configstring has no authored slot");
    int32_t time;
    if (!source_time(p, &time, e)) return false;
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_CONFIGSTRING, .index = index, .text = (char *)value};
    return source_emit(p, &event,
        (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
}

bool application_unified_q3_participant(void *context, qa_actor_id actor, int32_t code,
    int32_t parameter, qa_vec3 origin, int32_t now, qa_error *e)
{
    application_provider *p = context;
    if (!source(p, e)) return false;
    if (!qa_actors_get(qa_session_actors(p->application->session), actor))
        return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 participant event lost its full live actor");
    (void)origin;
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_Q3_CHARACTER,
        .value.q3_character = {.actor = actor, .event = code, .parameter = parameter, .time_ms = now}};
    return application_unified_event_emit(p->application, p->owner, &payload, NULL,
        (qa_actor_id){0}, (qa_actor_id){0}, (uint64_t)(uint32_t)now * UINT64_C(1000000),
        0, false, false, e);
}

bool application_unified_q3_console(application_provider *p, bool execute_now, const char *value, qa_error *e)
{
    if (!value || !source(p, e)) return application_fail(e, QA_ERROR_ARGUMENT, "Q3 engine event lost its authored text");
    int32_t now;
    if (!source_time(p, &now, e)) return false;
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_CONSOLE_COMMAND, .execute_now = execute_now, .text = (char *)value};
    return source_emit(p, &event,
        (uint64_t)(uint32_t)now * UINT64_C(1000000), e);
}
typedef struct q3_output_delivery {
    const qa_unified_q3_event *event;
    qa_event_lease *lease;
    application_q3_output_recipient recipient;
    void *context;
    uint32_t config_index,fragment;
} q3_output_delivery;
static bool q3_output_fragment(void *context,const char *text,qa_error *error)
{
    q3_output_delivery *delivery=context;
    bool ok=delivery->recipient(delivery->context,-1,text,delivery->event,
        delivery->lease,delivery->config_index,delivery->fragment,error);
    if(ok) ++delivery->fragment;
    return ok;
}
bool application_q3_output_fanout(const qa_unified_q3_event *event,qa_event_lease *lease,
    application_q3_output_recipient recipient,void *context,qa_error *error)
{
    if(event->kind==QA_UNIFIED_Q3_SERVER_COMMAND)
        return recipient(context,event->client,event->text,event,lease,UINT32_MAX,0,error);
    if(event->kind==QA_UNIFIED_Q3_CONFIGSTRING) {
        q3_output_delivery delivery={event,lease,recipient,context,event->index,0};
        return qa_q3_configstring_commands(event->index,event->text,q3_output_fragment,&delivery,error);
    }
    return true;
}
bool qa_application_q3_output_drain(qa_application *app, qa_error *error)
{
    if (app->event_source_draining) return true;
    app->event_source_draining = true;
    bool ok=true;
    qa_application_event_range cursor={.next=app->event_source_cursor,.end=UINT64_MAX};
    const qa_application_event_filter filter={.presentation_kind=QA_UNIFIED_PRESENTATION_Q3};
    const application_unified_event_record *view;
    while(ok && (view=qa_application_event_range_next(app,&cursor,&filter))) {
        const qa_unified_q3_event *event=&view->presentation->value.q3;
        if(event->kind==QA_UNIFIED_Q3_SERVER_COMMAND || event->kind==QA_UNIFIED_Q3_CONFIGSTRING) {
            application_provider *provider=app->live_providers;
            while(provider && provider->owner!=view->provider) provider=provider->next_live;
            if(provider && !provider->close_pending) {
                qa_event_lease *lease=qa_event_ring_retain(app->event_ring,cursor.id);
                if(provider->kind==APPLICATION_PROVIDER_Q3 && provider->native_q3_wire)
                    ok=application_native_q3_output(provider,event,lease,error);
                else if(q3g_engine(provider)) ok=q3g_output(provider,event,lease,error);
                else if(app->bots && app->bots->transport && application_bot_source(app->bots)==provider)
                    ok=application_bot_transport_output(app->bots->transport,event,lease,error);
                qa_event_lease_release(lease);
            }
        }
        if(ok) app->event_source_cursor=cursor.view ? cursor.id : cursor.next;
    }
    if(ok) app->event_source_cursor=cursor.next;
    app->event_source_draining = false;
    application_events_retire(app);
    return ok;
}
