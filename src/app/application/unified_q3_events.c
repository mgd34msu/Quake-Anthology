#include "unified_q3_events.h"
#include "unified_events.h"
#include "native_q3_console.h"
#include "guest_q3_components.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/source_save.h"

#include <stdlib.h>
#include <string.h>

typedef struct published_event {
    qa_actor_id actor;
    int32_t event, time;
} published_event;

struct application_unified_q3_events {
    qa_q3_game *game;
    bool busy;
    published_event rows[QA_Q3_SOURCE_ENTITIES];
};

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

bool application_unified_q3_source_emit(application_provider *p, const qa_unified_q3_event *event,
    qa_actor_id recipient, int32_t slot, bool has_slot, uint64_t ns, qa_error *e)
{
    if (!event || !source(p, e)) return false;
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_Q3, .value.q3 = *event};
    return application_unified_event_emit(p->application, p->owner, &payload, NULL,
        recipient, (qa_actor_id){0}, ns, slot, has_slot, false, e);
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
    return application_unified_q3_source_emit(p, &event, (qa_actor_id){0}, 0, false,
        (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
}

bool application_unified_q3_configstring(application_provider *p, uint32_t index, const char *value, qa_error *e)
{
    if (index >= QA_Q3_CONFIGSTRINGS || !value || !source(p, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 source configstring has no authored slot");
    int32_t time;
    if (!qa_q3_source_clock(p->state.q3, &time, e)) return false;
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_CONFIGSTRING, .index = index, .text = (char *)value};
    return application_unified_q3_source_emit(p, &event, (qa_actor_id){0}, 0, false,
        (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
}

static bool entity_event(application_provider *p, uint32_t slot, qa_actor_id actor,
    bool physical, const qa_q3_entity *s, qa_vec3 origin, int32_t now, qa_error *e)
{
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_ENTITY_EVENT, .actor = actor,
        .entity = *s, .origin = origin, .time_ms = now};
    return application_unified_q3_source_emit(p, &event, (qa_actor_id){0}, (int32_t)slot,
        physical, (uint64_t)(uint32_t)now * UINT64_C(1000000), e);
}

bool application_unified_q3_participant(void *context, qa_actor_id actor, int32_t code,
    int32_t parameter, qa_vec3 origin, int32_t now, qa_error *e)
{
    application_provider *p = context;
    if (!source(p, e)) return false;
    if (!qa_actors_get(qa_session_actors(p->application->session), actor))
        return application_fail(e, QA_ERROR_NOT_FOUND, "Q3 participant event lost its full live actor");
    qa_q3_entity authored = {.event = code, .eventParm = parameter};
    return entity_event(p, 0, actor, false, &authored, origin, now, e);
}

bool application_unified_q3_console(application_provider *p, bool execute_now, const char *value, qa_error *e)
{
    if (!value || !source(p, e)) return application_fail(e, QA_ERROR_ARGUMENT, "Q3 engine event lost its authored text");
    int32_t now;
    if (!qa_q3_source_clock(p->state.q3, &now, e)) return false;
    qa_unified_q3_event event = {.kind = QA_UNIFIED_Q3_CONSOLE_COMMAND, .execute_now = execute_now, .text = (char *)value};
    return application_unified_q3_source_emit(p, &event, (qa_actor_id){0}, 0, false,
        (uint64_t)(uint32_t)now * UINT64_C(1000000), e);
}

bool application_unified_q3_events_publish(application_provider *p, const qa_source_frame *frame, qa_error *e)
{
    qa_source_frame active;
    if (!source(p, e)) return false;
    if (!frame || frame->provider != p->owner || frame->kind != QA_CLOCK_Q3 ||
        frame->phase != QA_CLIENT_END_FRAME || !p->attached ||
        p->application->operation != APPLICATION_ADVANCING ||
        !qa_session_active_frame(p->application->session, p->owner, &active) ||
        active.number != frame->number || active.time_ns != frame->time_ns || active.phase != frame->phase)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 event publication requires its actual END tail");
    if (!p->unified_q3_events) {
        p->unified_q3_events = calloc(1, sizeof(*p->unified_q3_events));
        if (!p->unified_q3_events) return application_fail(e, QA_ERROR_MEMORY, "Retaining actual Q3 published events");
        p->unified_q3_events->game = p->state.q3;
    }
    struct application_unified_q3_events *owner = p->unified_q3_events;
    if (owner->busy || owner->game != p->state.q3)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 published-event owner is entered or replaced");
    if (!application_native_q3_console_borrow(p, e)) return false;
    owner->busy = true;
    uint32_t count; int32_t now;
    bool ok = qa_q3_source_entity_count(owner->game, &count, e) && qa_q3_source_clock(owner->game, &now, e);
    for (uint32_t slot = 0; ok && slot < QA_Q3_SOURCE_ENTITIES; ++slot) {
        published_event *previous = owner->rows + slot;
        qa_q3_source_binding binding;
        ok = source(p, e) && p->state.q3 == owner->game &&
            qa_q3_source_binding_read(owner->game, slot, &binding, e);
        if (!ok) break;
        if (previous->actor.registry && (!qa_actor_id_equal(previous->actor, binding.actor) ||
            !qa_actors_get(qa_session_actors(p->application->session), previous->actor)))
            *previous = (published_event){0};
        if (slot >= count || !binding.in_use) continue;
        qa_q3_entity s; qa_q3_wire_visibility visibility; int32_t time;
        qa_vec3 origin;
        ok = qa_q3_wire_entity_read(owner->game, slot, &s, &visibility, e) &&
            qa_q3_wire_entity_event_time(owner->game, slot, &time, e) &&
            qa_q3_source_current_origin_read(owner->game, binding.actor, &origin, e);
        if (!ok) break;
        int32_t event = s.eType >= 13 ? s.eType - 13 : s.event;
        if (!event || (qa_actor_id_equal(previous->actor, binding.actor) &&
            previous->event == event && previous->time == time)) continue;
        /* The donor marks before entering emit; a failing sink is not replayed. */
        *previous = (published_event){binding.actor, event, time};
        ok = entity_event(p, slot, binding.actor, true, &s, origin, now, e);
    }
    owner->busy = false;
    application_native_q3_console_release(p);
    return ok;
}

bool application_unified_q3_events_idle(const application_provider *p)
{ return !p || !p->unified_q3_events || !p->unified_q3_events->busy; }

bool application_unified_q3_events_destroy(application_provider *p, qa_error *e)
{
    if (!application_unified_q3_events_idle(p))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 published events are entered");
    if (p) { free(p->unified_q3_events); p->unified_q3_events = NULL; }
    return true;
}

static bool fields(qa_source_save_io *io, published_event rows[QA_Q3_SOURCE_ENTITIES])
{
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_ENTITIES; ++slot)
        if (!qa_source_save_actor(io, &rows[slot].actor) || !qa_source_save_i32(io, &rows[slot].event) ||
            !qa_source_save_i32(io, &rows[slot].time)) return false;
    return true;
}

bool application_unified_q3_events_capture(application_provider *p, qa_buffer *out, qa_error *e)
{
    if (!p || !out || out->data || out->size || !application_unified_q3_events_idle(p))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 published events cannot be captured while entered");
    struct application_unified_q3_events *owner = p->unified_q3_events;
    if (!owner) return true;
    if (!source(p, e) || owner->game != p->state.q3) return false;
    published_event *rows = malloc(sizeof(owner->rows));
    if (!rows) return application_fail(e, QA_ERROR_MEMORY, "Capturing actual Q3 published events");
    memcpy(rows, owner->rows, sizeof(owner->rows));
    bool bindings_ok = true;
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i) {
        if (!rows[i].actor.registry) continue;
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(owner->game, i, &binding, e)) { bindings_ok = false; break; }
        if (!qa_actors_get(qa_session_actors(p->application->session), rows[i].actor) ||
            !qa_actor_id_equal(rows[i].actor, binding.actor)) rows[i] = (published_event){0};
    }
    qa_source_save_io io = {0}; uint8_t magic[4] = {'Q','3','P','E'};
    bool ok = bindings_ok && qa_source_save_writer(&io, p->application->session, e) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) &&
        fields(&io, rows) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); free(rows);
    return ok;
}

bool application_unified_q3_events_restore(application_provider *p, qa_bytes bytes, qa_error *e)
{
    if (!p || p->kind != APPLICATION_PROVIDER_Q3 || p->unified_q3_events)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 published-event restore requires its genuine empty candidate");
    if (!bytes.size) return true;
    if (!p->state.q3)
        return application_fail(e, QA_ERROR_FORMAT, "Saved Q3 published events require their actual GAME owner");
    struct application_unified_q3_events *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(e, QA_ERROR_MEMORY, "Restoring actual Q3 published events");
    qa_source_save_io io = {0}; uint8_t magic[4];
    bool ok = qa_source_save_reader(&io, p->application->session, bytes, e) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) &&
        !memcmp(magic, "Q3PE", 4) && fields(&io, owner->rows) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    for (uint32_t i = 0; ok && i < QA_Q3_SOURCE_ENTITIES; ++i) {
        const published_event *row = owner->rows + i;
        const qa_actor_record *actor = row->actor.registry ?
            qa_actors_get(qa_session_actors(p->application->session), row->actor) : NULL;
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(p->state.q3, i, &binding, e)) { ok = false; break; }
        if ((row->actor.registry && (!actor || !qa_actor_id_equal(binding.actor, row->actor))) ||
            (!row->actor.registry && (row->event || row->time)) || (row->actor.registry && !row->event))
            ok = application_fail(e, QA_ERROR_FORMAT, "Saved Q3 event lost its real physical full actor");
    }
    if (!ok) {
        if (e && e->code == QA_OK) application_fail(e, QA_ERROR_FORMAT, "Invalid Q3 published-event continuation");
        free(owner); return false;
    }
    owner->game = p->state.q3; p->unified_q3_events = owner;
    return true;
}
