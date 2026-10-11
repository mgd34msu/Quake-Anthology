#include "unified_q3_events.h"
#include "unified_events.h"
#include "guest_q3_components.h"
#include "guest_q3_private.h"
#include "native_q3_wire_state.h"
#include "bots_private.h"
#include "bots_transport.h"

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
bool qa_application_q3_output_drain(qa_application *app, qa_error *error)
{
    if (app->event_source_draining) return true;
    app->event_source_draining = true;
    bool ok = true;
    while (ok && app->event_source_cursor < qa_application_events_next(app)) {
        const application_event_envelope *record =
            application_event_stream_at(app, app->event_source_cursor);
        for (const application_event_view *view = record ? record->views : NULL;
                ok && view; view = view->next) {
            const qa_unified_presentation_payload *payload = view->event.presentation;
            if (!payload || payload->kind != QA_UNIFIED_PRESENTATION_Q3 ||
                (payload->value.q3.kind != QA_UNIFIED_Q3_SERVER_COMMAND &&
                 payload->value.q3.kind != QA_UNIFIED_Q3_CONFIGSTRING)) continue;
            application_provider *provider = app->live_providers;
            while (provider && provider->owner != view->event.provider) provider = provider->next_live;
            if (!provider || provider->close_pending) continue;
            if (provider->kind == APPLICATION_PROVIDER_Q3 && provider->native_q3_wire)
                {
                    qa_event_lease *lease=qa_event_ring_retain(app->event_ring,record->id);
                    ok=application_native_q3_output(provider,&payload->value.q3,lease,error);
                    qa_event_lease_release(lease);
                }
            else if (q3g_engine(provider))
                {
                    qa_event_lease *lease=qa_event_ring_retain(app->event_ring,record->id);
                    ok=q3g_output(provider,&payload->value.q3,lease,error);
                    qa_event_lease_release(lease);
                }
            else if(app->bots && app->bots->transport && application_bot_source(app->bots)==provider &&
                payload->value.q3.kind==QA_UNIFIED_Q3_SERVER_COMMAND) {
                qa_event_lease *lease=qa_event_ring_retain(app->event_ring,record->id);
                ok=application_bot_transport_output(app->bots->transport,&payload->value.q3,lease,error);
                qa_event_lease_release(lease);
            }
        }
        if (ok) ++app->event_source_cursor;
    }
    app->event_source_draining = false;
    application_events_retire(app);
    return ok;
}
