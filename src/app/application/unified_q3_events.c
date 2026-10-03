#include "unified_q3_events.h"
#include "unified_events.h"
#include "unified_output_json.h"
#include "native_q3_console.h"
#include "guest_q3_components.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/source_save.h"
#include "qa/q3_abi.h"

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

bool application_unified_q3_source_emit(application_provider *p, qa_bytes event,
    qa_actor_id recipient, int32_t slot, bool has_slot, uint64_t ns, qa_error *e)
{
    if (!source(p, e)) return false;
    application_unified_json j = {0};
    bool ok = application_unified_json_text(&j, "{\"kind\":\"q3-source\",\"event\":", e) &&
        application_unified_json_append(&j, event, e) && application_unified_json_text(&j, "}", e) &&
        application_unified_event_emit(p->application, p->owner,
            (qa_bytes){j.bytes.data, j.bytes.size}, (qa_bytes){0}, recipient,
            (qa_actor_id){0}, ns, slot, has_slot, false, e);
    application_unified_json_dispose(&j);
    return ok;
}

static bool component_emit(qa_application *app, const application_q3_component_publication *p,
    application_unified_json *event, qa_actor_id recipient, int32_t time, qa_error *e)
{
    if (!app || !p || !p->owner || !p->descriptor || !p->content || !p->game || !p->source ||
        !p->identity || !p->metadata || !p->generation)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 component event lost its admitted physical Source");
    application_unified_json payload = {0};
    bool ok = application_unified_json_text(&payload, "{\"kind\":\"q3-source\",\"event\":", e) &&
        application_unified_json_append(&payload, (qa_bytes){event->bytes.data,event->bytes.size}, e) &&
        application_unified_json_text(&payload, "}", e) && application_unified_event_emit(app, p->owner,
            (qa_bytes){payload.bytes.data,payload.bytes.size}, (qa_bytes){0}, recipient,
            (qa_actor_id){0}, (uint64_t)(uint32_t)time * UINT64_C(1000000), 0, false, false, e);
    application_unified_json_dispose(&payload);
    return ok;
}

static bool player_record_write(void *context,size_t offset,qa_bytes value,qa_error *e)
{
    qa_buffer *bytes=context;
    if(offset>bytes->size||value.size>bytes->size-offset) return application_fail(e,QA_ERROR_ARGUMENT,"Original player event leaves its Source ABI record");
    if(value.size) memcpy(bytes->data+offset,value.data,value.size);
    return true;
}
bool application_unified_q3_component_player(qa_application *app,const application_q3_component_publication *p,
    const application_q3_scene_player_event *event,qa_error *e)
{
    if(!app||!p||!event||event->time_ms<0||!event->actor.registry)
        return application_fail(e,QA_ERROR_ARGUMENT,"Original player event lost its actual component delivery");
    uint8_t player[468]={0}; qa_buffer bytes={player,qa_qvm_player_bytes(QA_QVM_Q3_MODERN)};
    qa_q3_abi_record record={.abi=QA_QVM_Q3_MODERN,.bytes={player,bytes.size},.context=&bytes,.write=player_record_write};
    application_unified_json j={0};
    const qa_json_document *identity=qa_unified_document_json(p->identity);
    qa_json_id module=qa_json_at(identity,qa_json_get(identity,qa_unified_document_root(p->identity),"modules"),0);
    qa_bytes raw=qa_json_source(identity,module);
    bool ok=module!=QA_JSON_NONE&&qa_q3_abi_write_player(&record,0,true,false,&event->player,e)&&
        application_unified_json_text(&j,"{\"kind\":\"player-event\",\"actor\":",e)&&application_unified_json_actor(&j,event->actor,e)&&
        application_unified_json_text(&j,",\"source\":{\"module\":",e)&&application_unified_json_append(&j,raw,e)&&
        application_unified_json_text(&j,",\"abiProfile\":",e)&&application_unified_json_string(&j,p->abi==QA_QVM_Q3_MODERN?"q3-modern":"q3-1.16n-base",e)&&
        application_unified_json_text(&j,"},\"playerState\":[",e);
    for(size_t i=0;ok&&i<bytes.size;++i) ok=(!i||application_unified_json_text(&j,",",e))&&application_unified_json_natural(&j,player[i],e);
    if(ok) ok=application_unified_json_text(&j,"]",e)&&
        application_unified_json_text(&j,",\"event\":",e)&&application_unified_json_number(&j,event->event,e)&&
        application_unified_json_text(&j,",\"parameter\":",e)&&application_unified_json_number(&j,event->parameter,e)&&
        application_unified_json_text(&j,event->external?",\"sequence\":{\"kind\":\"external\",\"time\":" : ",\"sequence\":{\"kind\":\"predictable\",\"sequence\":",e)&&
        application_unified_json_number(&j,event->source_sequence,e)&&application_unified_json_text(&j,"},\"origin\":",e)&&
        application_unified_json_vector(&j,event->origin,e)&&application_unified_json_text(&j,",\"time\":",e)&&
        application_unified_json_number(&j,event->time_ms,e)&&application_unified_json_text(&j,"}",e)&&
        component_emit(app,p,&j,(qa_actor_id){0},event->time_ms,e);
    application_unified_json_dispose(&j); return ok;
}

bool application_unified_q3_component_command(qa_application *app,
    const application_q3_component_publication *p, qa_actor_id recipient, const char *value,
    int32_t time, qa_error *e)
{
    if (!value)
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 component command lost its actual lexical client/text");
    application_unified_json j = {0};
    bool ok = application_unified_json_text(&j, "{\"kind\":\"server-command\",\"client\":-1,\"text\":", e) &&
        application_unified_json_string(&j, value, e) && application_unified_json_text(&j, "}", e) &&
        component_emit(app, p, &j, recipient, time, e);
    application_unified_json_dispose(&j);
    return ok;
}

static bool scalar(application_unified_json *j, const char *name, int32_t n, qa_error *e)
{
    return application_unified_json_text(j, ",", e) && application_unified_json_string(j, name, e) &&
        application_unified_json_text(j, ":", e) && application_unified_json_number(j, n, e);
}

bool application_unified_q3_text(application_provider *p, application_unified_q3_text_kind kind,
    int32_t client, const char *value, qa_error *e)
{
    static const char *const names[] = {"print", "log", "server-command", "drop-client"};
    if ((unsigned)kind >= sizeof(names) / sizeof(names[0]) || !value || !source(p, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 engine event lost its authored text");
    application_unified_json j = {0}; int32_t time;
    bool ok = qa_q3_source_clock(p->state.q3, &time, e) &&
        application_unified_json_text(&j, "{\"kind\":", e) && application_unified_json_string(&j, names[kind], e);
    if (ok && (kind == APPLICATION_Q3_SOURCE_COMMAND || kind == APPLICATION_Q3_SOURCE_DROP))
        ok = scalar(&j, "client", client, e);
    if (ok) ok = application_unified_json_text(&j,
        kind == APPLICATION_Q3_SOURCE_DROP ? ",\"reason\":" : ",\"text\":", e) &&
        application_unified_json_string(&j, value, e) && application_unified_json_text(&j, "}", e) &&
        application_unified_q3_source_emit(p, (qa_bytes){j.bytes.data, j.bytes.size}, (qa_actor_id){0},
            0, false, (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
    application_unified_json_dispose(&j);
    return ok;
}

bool application_unified_q3_configstring(application_provider *p, uint32_t index, const char *value, qa_error *e)
{
    if (index >= QA_Q3_CONFIGSTRINGS || !value || !source(p, e))
        return application_fail(e, QA_ERROR_ARGUMENT, "Q3 source configstring has no authored slot");
    application_unified_json j = {0}; int32_t time;
    bool ok = qa_q3_source_clock(p->state.q3, &time, e) &&
        application_unified_json_text(&j, "{\"kind\":\"configstring\",\"index\":", e) &&
        application_unified_json_natural(&j, index, e) && application_unified_json_text(&j, ",\"value\":", e) &&
        application_unified_json_string(&j, value, e) && application_unified_json_text(&j, "}", e) &&
        application_unified_q3_source_emit(p, (qa_bytes){j.bytes.data, j.bytes.size}, (qa_actor_id){0},
            0, false, (uint64_t)(uint32_t)time * UINT64_C(1000000), e);
    application_unified_json_dispose(&j);
    return ok;
}
static bool vector(application_unified_json *j, const char *name, const float v[3], qa_error *e)
{
    return application_unified_json_text(j, ",", e) && application_unified_json_string(j, name, e) &&
        application_unified_json_text(j, ":", e) && application_unified_json_vector(j, qa_v3(v[0], v[1], v[2]), e);
}
static bool trajectory(application_unified_json *j, const char *name, const qa_q3_trajectory *t, qa_error *e)
{
    return application_unified_json_text(j, ",", e) && application_unified_json_string(j, name, e) &&
        application_unified_json_text(j, ":{\"type\":", e) && application_unified_json_number(j, t->type, e) &&
        scalar(j, "time", t->time, e) && scalar(j, "duration", t->duration, e) &&
        vector(j, "base", t->base, e) && vector(j, "delta", t->delta, e) &&
        application_unified_json_text(j, "}", e);
}
static bool state(application_unified_json *j, const qa_q3_entity *s, qa_error *e)
{
    if (!application_unified_json_text(j, "{\"number\":", e) ||
        !application_unified_json_number(j, s->number, e) ||
        !scalar(j, "eType", s->eType, e) || !scalar(j, "eFlags", s->eFlags, e) ||
        !trajectory(j, "pos", &s->pos, e) || !trajectory(j, "apos", &s->apos, e) ||
        !scalar(j, "time", s->time, e) || !scalar(j, "time2", s->time2, e) ||
        !vector(j, "origin", s->origin, e) || !vector(j, "origin2", s->origin2, e) ||
        !vector(j, "angles", s->angles, e) || !vector(j, "angles2", s->angles2, e)) return false;
#define FIELD(name) if (!scalar(j, #name, s->name, e)) return false
    FIELD(otherEntityNum); FIELD(otherEntityNum2); FIELD(groundEntityNum); FIELD(constantLight);
    FIELD(loopSound); FIELD(modelindex); FIELD(modelindex2); FIELD(clientNum); FIELD(frame); FIELD(solid);
    FIELD(event); FIELD(eventParm); FIELD(powerups); FIELD(weapon); FIELD(legsAnim); FIELD(torsoAnim); FIELD(generic1);
#undef FIELD
    return application_unified_json_text(j, "}", e);
}

static bool entity_event(application_provider *p, uint32_t slot, qa_actor_id actor,
    bool physical, const qa_q3_entity *s, qa_vec3 origin, int32_t now, qa_error *e)
{
    application_unified_json j = {0};
    bool ok = application_unified_json_text(&j, "{\"kind\":\"entity-event\",\"actor\":", e) &&
        application_unified_json_actor(&j, actor, e) && application_unified_json_text(&j, ",\"state\":", e) &&
        state(&j, s, e) && application_unified_json_text(&j, ",\"origin\":", e) &&
        application_unified_json_vector(&j, origin, e) && scalar(&j, "time", now, e) &&
        application_unified_json_text(&j, "}", e) &&
        application_unified_q3_source_emit(p, (qa_bytes){j.bytes.data, j.bytes.size},
            (qa_actor_id){0}, (int32_t)slot, physical, (uint64_t)(uint32_t)now * UINT64_C(1000000), e);
    application_unified_json_dispose(&j);
    return ok;
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

bool application_unified_q3_console(application_provider *p, bool execute_now,
    const char *value, qa_error *e)
{
    if (!value || !source(p, e)) return false;
    application_unified_json j = {0}; int32_t now;
    bool ok = qa_q3_source_clock(p->state.q3, &now, e) &&
        application_unified_json_text(&j, "{\"kind\":\"console-command\",\"execution\":", e) &&
        application_unified_json_string(&j, execute_now ? "now" : "append", e) &&
        application_unified_json_text(&j, ",\"text\":", e) &&
        application_unified_json_string(&j, value, e) && application_unified_json_text(&j, "}", e) &&
        application_unified_q3_source_emit(p, (qa_bytes){j.bytes.data, j.bytes.size},
            (qa_actor_id){0}, 0, false, (uint64_t)(uint32_t)now * UINT64_C(1000000), e);
    application_unified_json_dispose(&j);
    return ok;
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
