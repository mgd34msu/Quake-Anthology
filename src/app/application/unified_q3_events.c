#include "unified_q3_events.h"
#include "unified_events.h"
#include "guest_q3_components.h"

static bool source(application_provider *p, qa_error *e)
{
    return (p && p->application && !p->application->destroy_requested &&
        p->kind == APPLICATION_PROVIDER_Q3 && p->state.q3 && p->constructed &&
        !p->close_pending && p->launch && p->product) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Q3 presentation lost its genuine GAME owner");
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
    if (!event || !source(p, e)) return false;
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

bool application_unified_q3_component_player(qa_application *app, const application_q3_component_publication *p,
    const application_q3_scene_player_event *event, qa_error *e)
{
    if (!app || !p || !event || event->time_ms < 0 || !event->actor.registry || !p->product || !p->metadata)
        return application_fail(e, QA_ERROR_ARGUMENT, "Original player event lost its actual component delivery");
    if (!p->module || !p->module->id || !p->module->artifact_path || !p->module->digest || !p->module->revision)
        return application_fail(e, QA_ERROR_ARGUMENT, "Original player event lost its admitted module identity owner");
    qa_unified_q3_event value = {.kind = QA_UNIFIED_Q3_PLAYER_EVENT, .actor = event->actor,
        .player = event->player, .event = event->event, .parameter = event->parameter,
        .external = event->external, .source_sequence = event->source_sequence,
        .origin = event->origin, .time_ms = event->time_ms, .abi = p->abi,
        .module = *p->module};
    return component_emit(app, p, &value, (qa_actor_id){0}, event->time_ms, e);
}

bool application_unified_q3_component_command(qa_application *app,
    const application_q3_component_publication *p, qa_actor_id recipient, const char *value,
    int32_t time, qa_error *e)
{
    if (!value) return application_fail(e, QA_ERROR_ARGUMENT, "Q3 component command lost its actual lexical client/text");
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_SERVER_COMMAND, .client = -1, .text = (char *)value};
    return component_emit(app, p, &event, recipient, time, e);
}

bool application_unified_q3_text(application_provider *p, application_unified_q3_text_kind kind,
    int32_t client, const char *value, qa_error *e)
{
    if ((unsigned)kind > APPLICATION_Q3_SOURCE_DROP || !value || !source(p, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 engine event lost its authored text");
    static const qa_unified_q3_event_kind kinds[] = {QA_UNIFIED_Q3_PRINT, QA_UNIFIED_Q3_LOG,
        QA_UNIFIED_Q3_SERVER_COMMAND, QA_UNIFIED_Q3_DROP_CLIENT};
    int32_t time;
    if (!qa_q3_source_clock(p->state.q3, &time, e)) return false;
    qa_unified_q3_event event = {.kind = kinds[kind], .client = client, .text = (char *)value};
    return source_emit(p, &event,
        (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
}

bool application_unified_q3_configstring(application_provider *p, uint32_t index, const char *value, qa_error *e)
{
    if (index >= QA_Q3_CONFIGSTRINGS || !value || !source(p, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 source configstring has no authored slot");
    int32_t time;
    if (!qa_q3_source_clock(p->state.q3, &time, e)) return false;
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
    if (!qa_q3_source_clock(p->state.q3, &now, e)) return false;
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_CONSOLE_COMMAND, .execute_now = execute_now, .text = (char *)value};
    return source_emit(p, &event,
        (uint64_t)(uint32_t)now * UINT64_C(1000000), e);
}
