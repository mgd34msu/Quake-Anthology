#include "unified_q2_native_events.h"
#include "unified_events.h"
#include "unified_q2_events.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_combat.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static char *alias(qa_application *app, qa_string_id id)
{ return id ? (char *)qa_strings_cstr(qa_session_strings(app->session), id) : NULL; }
static bool source(application_provider *p, qa_clock_state *clock, qa_error *e)
{
    return (p && p->application && p->constructed && !p->close_pending &&
        p->application->session && qa_session_clock(p->application->session, p->owner, clock)) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Q2 event lost its actual Source clock owner");
}
static application_provider *provider(qa_application *app, qa_actor_owner owner)
{
    for (application_provider *p = app->live_providers; p; p = p->next_live)
        if (p->owner == owner) return p;
    return NULL;
}
static bool emit(application_provider *p, const qa_unified_presentation_payload *presentation,
    const qa_unified_simulation_payload *simulation, qa_actor_id actor, qa_actor_id recipient,
    uint64_t time, const qa_application_q2_audience *audience, qa_error *e)
{
    qa_q2_wire_binding binding = {0}; qa_error missing = {0};
    bool has_slot = p->kind == APPLICATION_PROVIDER_Q2 &&
        qa_q2_wire_actor(p->state.q2, actor, &binding, &missing) && binding.in_use &&
        binding.source_owner == p->owner && binding.source_slot <= INT32_MAX;
    int32_t slot = has_slot ? (int32_t)binding.source_slot : 0;
    if (audience && audience->captured) {
        if (audience->source != p->owner || (audience->count && !audience->recipients))
            return application_fail(e, QA_ERROR_ARGUMENT, "Q2 event audience belongs to another Source");
        for (size_t i = 0; i < audience->count; ++i)
            if (!application_unified_event_emit(p->application, p->owner, presentation, simulation,
                audience->recipients[i].actor, audience->recipients[i].actor,
                audience->source_time_ns, slot, has_slot, false, e)) return false;
        return true;
    }
    return application_unified_event_emit(p->application, p->owner, presentation, simulation,
        recipient, recipient, time, slot, has_slot, false, e);
}
bool application_unified_q2_native_player(application_provider *p, const qa_q2_player_event *v, qa_error *e)
{
    qa_clock_state clock;
    if (!v || !source(p, &clock, e)) return false;
    if (v->kind == QA_Q2_PLAYER_TRAIL) return true;
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_Q2_PLAYER};
    payload.value.q2_player = *v;
    bool ok = true;
    qa_actor_id recipient = v->kind == QA_Q2_PLAYER_USERINFO ? (qa_actor_id){0} : v->actor;
    qa_unified_simulation_payload simulation = {.kind = QA_UNIFIED_SIMULATION_MESSAGE};
    int16_t counts[256] = {0};
    const qa_unified_simulation_payload *inventory = NULL;
    if (ok && v->kind == QA_Q2_PLAYER_INVENTORY && v->visible) {
        size_t items = qa_q2_item_count(p->state.q2);
        for (size_t i = 0; i < items && i + 1 < 256; ++i) {
            const qa_q2_item_definition *item = qa_q2_item_at(p->state.q2, i);
            for (size_t n = 0; n < v->inventory_count; ++n) if (v->inventory[n].item == item->item) {
                double count = v->inventory[n].count;
                int32_t value = count >= INT32_MIN && count < 2147483648.0 ? (int32_t)count : INT32_MIN;
                uint16_t bits = (uint16_t)(uint32_t)value;
                memcpy(counts + i + 1, &bits, sizeof(bits));
                break;
            }
        }
        simulation.value.message = (qa_unified_message_event){.kind = QA_UNIFIED_MESSAGE_Q2_INVENTORY,
            .counts = counts, .count = 256};
        inventory = &simulation;
    }
    if (ok) ok = emit(p, &payload, inventory, v->actor, recipient, clock.frame.time_ns, NULL, e);
    return ok;
}

bool application_unified_q2_native_map(application_provider *p, const qa_q2_map_event *v,
    const qa_application_q2_audience *audience, qa_error *e)
{
    qa_clock_state clock;
    if (!v || !source(p, &clock, e)) return false;
    if (v->kind == QA_Q2_MAP_WORLD_TEXT || v->kind == QA_Q2_MAP_GOAL || v->kind == QA_Q2_MAP_SECRET) return true;
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_Q2_MAP,
        .value.q2_map = *v};
    return emit(p, &payload, NULL, v->actor, v->recipient, clock.frame.time_ns, audience, e);
}

static bool model(application_provider *p, qa_actor_id id, const qa_entity_visual *v,
    uint64_t time, const qa_application_q2_audience *audience, qa_error *e)
{
    qa_unified_model_attachment attachments[3] = {0}; size_t count = 0;
    for (size_t i = 1; i < 4; ++i)
        if (v->models[i]) attachments[count++].path = alias(p->application, v->models[i]);
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_MODEL};
    payload.value.model = (qa_unified_model_state){.actor = id, .family = QA_GAME_Q2, .visual = *v,
        .path = v->models[0], .has_alpha = true,
        .attachments = attachments, .attachment_count = count};
    return emit(p, &payload, NULL, id, (qa_actor_id){0}, time, audience, e);
}
bool application_unified_q2_native_visual(application_provider *p, qa_actor_id id,
    const qa_entity_visual *v, qa_error *e)
{
    qa_clock_state clock;
    if (!v || !source(p, &clock, e)) return false;
    if (!model(p, id, v, clock.frame.time_ns, NULL, e)) return false;
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_VISIBILITY,
        .value.visibility = {.actor = id, .visible = v->visible}};
    return emit(p, &payload, NULL, id, (qa_actor_id){0}, clock.frame.time_ns, NULL, e);
}
bool application_unified_q2_native_item_visibility(void *context, qa_actor_id player,
    qa_actor_id pickup, bool visible, qa_error *e)
{
    application_provider *p = context; qa_clock_state clock;
    if (!source(p, &clock, e)) return false;
    qa_unified_presentation_payload payload = {.kind = QA_UNIFIED_PRESENTATION_VISIBILITY,
        .value.visibility = {.actor = pickup, .visible = visible}};
    return emit(p, &payload, NULL, pickup, player, clock.frame.time_ns, NULL, e);
}

static bool damage_effect(int code)
{
    return code == 0 || code == 1 || code == 4 || code == 9 || code == 12 || code == 13 ||
        code == 14 || code == 26 || code == 42 || code == 46;
}
bool application_unified_q2_native_builtin(qa_application *app, const qa_builtin_event *v,
    const qa_application_q2_audience *audience, qa_error *e)
{
    if (!v || v->family != QA_GAME_Q2) return true;
    application_provider *p = provider(app, v->provider); qa_clock_state clock;
    if (!source(p, &clock, e)) return false;
    if (v->kind == QA_BUILTIN_ANIMATION) {
        qa_entity_visual visual;
        return p->kind != APPLICATION_PROVIDER_Q2 || !qa_q2_presentation_read(p->state.q2, v->actor, &visual) ||
            model(p, v->actor, &visual, v->time_ns, audience, e);
    }
    bool reached = false;
    switch (v->kind) {
    case QA_BUILTIN_SOUND: case QA_BUILTIN_STOP_SOUND: case QA_BUILTIN_CENTERPRINT:
    case QA_BUILTIN_MESSAGE: case QA_BUILTIN_MUZZLE: case QA_BUILTIN_Q2_PLAYER_ANIMATION:
    case QA_BUILTIN_Q2_ENTITY_EVENT: reached = true; break;
    case QA_BUILTIN_ITEM: reached = v->code == 0 || v->code == 1; break;
    case QA_BUILTIN_BEAM: reached = true; break;
    case QA_BUILTIN_PARTICLES: reached = v->resource || damage_effect(v->code); break;
    case QA_BUILTIN_IMPACT: case QA_BUILTIN_EXPLOSION: case QA_BUILTIN_EFFECT:
    case QA_BUILTIN_TELEPORT: reached = v->resource != 0; break;
    default: break;
    }
    if (!reached) return true;
    if (v->kind == QA_BUILTIN_MESSAGE || v->kind == QA_BUILTIN_CENTERPRINT)
        for (size_t i = 0; i < v->argument_count; ++i)
            if (v->arguments[i].kind != QA_BUILTIN_MESSAGE_STRING)
                return application_fail(e, QA_ERROR_UNSUPPORTED, "Q2 localized Source event requires its authored string argument");
    qa_unified_presentation_payload presentation = {.kind = QA_UNIFIED_PRESENTATION_BUILTIN};
    presentation.value.builtin = *v;
    qa_unified_simulation_payload simulation = {0}; const qa_unified_simulation_payload *sim = NULL;
    char key[QA_APPLICATION_RESOURCE_KEY_CAPACITY]; bool ok = true;
    if (v->kind == QA_BUILTIN_SOUND && !(v->flags & 1u)) {
        bool found;
        ok = application_unified_event_resource_lookup(app, v->provider, alias(app, v->resource), key, &found, e);
        if (ok && found) {
            simulation.kind = QA_UNIFIED_SIMULATION_SOUND;
            simulation.value.sound = (qa_unified_sound_event){.resource = key, .actor = v->actor,
                .origin = v->origin, .channel = v->channel, .volume = v->volume, .attenuation = v->attenuation};
            sim = &simulation;
        }
    }
    if (ok && v->kind == QA_BUILTIN_BEAM && presentation.value.builtin.resource &&
        v->resource==app->ui_names.q2_lightning) {
        qa_q2_wire_binding from = {0}, to = {0}; qa_q2_combat_rules rules;
        ok = qa_q2_combat_rules_read(p->state.q2, &rules) &&
            qa_q2_wire_actor(p->state.q2, v->actor, &from, e) && qa_q2_wire_actor(p->state.q2, v->other, &to, e);
        if (ok) {
            qa_unified_q2_temp_field fields[4] = {
                {.name = QA_Q2_TEMP_ENTITY1, .kind = QA_Q2_TEMP_INTEGER, .integer = (int32_t)from.source_slot, .actor = v->actor},
                {.name = QA_Q2_TEMP_ENTITY2, .kind = QA_Q2_TEMP_INTEGER, .integer = (int32_t)to.source_slot, .actor = v->other},
                {.name = QA_Q2_TEMP_POSITION1, .kind = QA_Q2_TEMP_VECTOR, .vector = v->origin},
                {.name = QA_Q2_TEMP_POSITION2, .kind = QA_Q2_TEMP_VECTOR, .vector = v->end}};
            qa_unified_presentation_payload temporary = {.kind = QA_UNIFIED_PRESENTATION_Q2_TEMPORARY,
                .value.q2_temporary = {.type = QA_Q2_TE_LIGHTNING, .rerelease = rules.edition == QA_Q2_RERELEASE,
                    .fields = fields, .field_count = 4}};
            ok = emit(p, &temporary, sim, v->actor, (qa_actor_id){0}, v->time_ns, audience, e);
        } else if (!e || e->code == QA_OK) application_fail(e, QA_ERROR_FORMAT, "Q2 lightning lost its actual GAME rules");
    } else if (ok) {
        qa_actor_id recipient = v->kind == QA_BUILTIN_MESSAGE || v->kind == QA_BUILTIN_CENTERPRINT ||
            (v->kind == QA_BUILTIN_ITEM && v->code == 0) ? v->actor : (qa_actor_id){0};
        ok = emit(p, &presentation, sim, v->actor, recipient, v->time_ns, audience, e);
    }
    return ok;
}
