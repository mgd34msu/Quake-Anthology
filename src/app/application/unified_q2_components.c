#include "unified_q2_components.h"
#include "unified_output_json.h"
#include "guest_native_q2_private.h"
#include "qa/native_host_q2_wire.h"

#include <limits.h>
#include <string.h>

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool vector(application_unified_json *j, const float v[3], qa_error *e)
{ return application_unified_json_vector(j, qa_v3(v[0], v[1], v[2]), e); }

static bool integers(application_unified_json *j, const int16_t *values, size_t count, qa_error *e)
{
    if (!text(j, "[", e)) return false;
    for (size_t i = 0; i < count; ++i)
        if ((i && !text(j, ",", e)) || !number(j, values[i], e)) return false;
    return text(j, "]", e);
}

static bool color(application_unified_json *j, const float v[4], qa_error *e)
{
    return text(j, "{\"x\":", e) && number(j, v[0], e) && text(j, ",\"y\":", e) &&
        number(j, v[1], e) && text(j, ",\"z\":", e) && number(j, v[2], e) &&
        text(j, ",\"w\":", e) && number(j, v[3], e) && text(j, "}", e);
}

static struct application_native_q2 *engine(qa_application *app,
    const application_unified_q2_source_documents *v)
{
    application_provider *provider = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!provider || provider->application != app || provider->kind != APPLICATION_PROVIDER_NATIVE ||
        provider->owner != v->source.owner || provider->launch != v->physical.launch ||
        provider->state.native.host != v->physical.source.original.host) return NULL;
    struct application_native_q2 *native = provider->state.native.q2_engine;
    return native && native->provider == provider && native->profile == v->physical.source.original.profile
        ? native : NULL;
}

static bool physical_player(qa_application *app, const application_unified_q2_source_documents *v,
    struct application_native_q2 **out, qa_q2_player *state, qa_error *e)
{
    struct application_native_q2 *native = engine(app, v);
    uint32_t slot = v->player.source_slot;
    if (!native || !slot || slot > 256 || slot > v->source.max_clients ||
        v->player.source_owner != v->source.owner)
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 HUD lost its physical Source client");
    const application_native_q2_client *client = native->clients + slot;
    if (!client->connected || !client->begun || client->bot || client->disconnect_started ||
        !qa_actor_id_equal(client->actor, v->player.actor))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 HUD lost its admitted remote full actor");
    for (uint32_t other = 1; other <= 256; ++other)
        if (other != slot && native->clients[other].connected &&
            qa_actor_id_equal(native->clients[other].actor, v->player.actor))
            return application_fail(e, QA_ERROR_FORMAT, "Unified Q2 HUD actor aliases physical client slots");
    qa_native_host_q2_entity entity;
    qa_native_host *host = (qa_native_host *)v->physical.source.original.host;
    if (!qa_native_host_q2_wire_entity(host, slot, &entity, e)) return false;
    if (!entity.in_use || !qa_actor_id_equal(entity.binding.actor, v->player.actor) ||
        entity.binding.owner != v->source.owner || entity.binding.source_slot != slot)
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 HUD lost its public edict/player-state binding");
    if (!qa_native_host_q2_wire_player(host, slot, v->player.actor, state, e)) return false;
    if (state->clientnum != (int32_t)(slot - 1) ||
        !memchr(client->layout, 0, sizeof(client->layout)))
        return application_fail(e, QA_ERROR_FORMAT, "Unified Q2 HUD exceeds its native client fields");
    *out = native;
    return true;
}

static bool state_equal(const qa_q2_player *a, const qa_q2_player *b)
{
    return a->clientnum == b->clientnum && a->pmove.type == b->pmove.type &&
        a->pmove.flags == b->pmove.flags && a->pmove.viewheight == b->pmove.viewheight &&
        !memcmp(a->pmove.origin, b->pmove.origin, sizeof(a->pmove.origin)) &&
        !memcmp(a->pmove.origin_f, b->pmove.origin_f, sizeof(a->pmove.origin_f)) &&
        !memcmp(a->viewangles, b->viewangles, sizeof(a->viewangles)) &&
        !memcmp(a->viewoffset, b->viewoffset, sizeof(a->viewoffset)) &&
        !memcmp(a->kick_angles, b->kick_angles, sizeof(a->kick_angles)) &&
        !memcmp(a->blend, b->blend, sizeof(a->blend)) &&
        !memcmp(a->damage_blend, b->damage_blend, sizeof(a->damage_blend)) &&
        !memcmp(a->stats, b->stats, sizeof(a->stats)) && a->fov == b->fov &&
        a->rdflags == b->rdflags && a->gunindex == b->gunindex;
}

bool application_unified_q2_source_documents_current(qa_application *app,
    const application_unified_q2_source_documents *v)
{
    if (!v || !application_unified_source_current(app, &v->source) ||
        !application_unified_player_current(app, v->recipient, &v->player) ||
        qa_actors_revision(qa_session_actors(v->source.session)) != v->actors_revision) return false;
    if (!v->present) return !v->hud_state && !v->hud_frame && !v->camera;
    struct application_native_q2 *native;
    qa_q2_player state;
    return v->hud_state && v->hud_frame && v->camera &&
        qa_application_native_q2_presentation_current(app, &v->physical) &&
        physical_player(app, v, &native, &state, NULL) && native->config_revision == v->config_revision &&
        !strcmp(native->clients[v->player.source_slot].layout, v->layout) &&
        !memcmp(native->clients[v->player.source_slot].inventory, v->inventory, sizeof(v->inventory)) &&
        state_equal(&state, &v->player_state);
}

static bool hud_state(application_unified_json *j, const struct application_native_q2 *native,
    const application_unified_q2_source_documents *v, qa_error *e)
{
    bool classic = v->physical.edition == QA_Q2_CLASSIC;
    if (!text(j, classic ? "{\"protocol\":{\"kind\":\"q2-classic\",\"version\":34},\"configstrings\":[" :
        "{\"protocol\":{\"kind\":\"q2-rerelease\",\"version\":1038},\"configstrings\":[", e)) return false;
    bool first = true;
    for (uint32_t i = 0; i < native->configstring_count; ++i) {
        if (!native->configstrings[i]) continue;
        if ((!first && !text(j, ",", e)) || !text(j, "{\"index\":", e) || !number(j, i, e) ||
            !text(j, ",\"value\":", e) || !application_unified_json_string(j, native->configstrings[i], e) ||
            !text(j, "}", e)) return false;
        first = false;
    }
    return text(j, "],\"layout\":", e) && application_unified_json_string(j, v->layout, e) &&
        text(j, ",\"inventory\":", e) && integers(j, v->inventory, 256, e) &&
        text(j, ",\"playerNumber\":", e) && number(j, v->player.source_slot - 1, e) && text(j, "}", e);
}

static bool hud_frame(application_unified_json *j, const application_unified_q2_source_documents *v,
    qa_error *e)
{
    const qa_source_frame *frame = &v->physical.clock.frame;
    if (frame->number > INT32_MAX)
        return application_fail(e, QA_ERROR_FORMAT, "Unified Q2 HUD frame exceeds its signed Source counter");
    bool ok = text(j, "{\"stats\":", e) && integers(j, v->player_state.stats,
        v->physical.edition == QA_Q2_CLASSIC ? 32 : 64, e) && text(j, ",\"serverFrame\":", e) &&
        application_unified_json_natural(j, frame->number, e) && text(j, ",\"timeMilliseconds\":", e) &&
        number(j, (double)v->physical.server_time_ns / 1000000.0, e);
    if (ok && frame->elapsed_ns)
        ok = text(j, ",\"frameTimeMilliseconds\":", e) && number(j, (double)frame->elapsed_ns / 1000000.0, e);
    return ok && text(j, "}", e);
}

static bool camera(application_unified_json *j, const application_unified_q2_source_documents *v,
    qa_error *e)
{
    const qa_q2_player *s = &v->player_state;
    bool classic = v->physical.edition == QA_Q2_CLASSIC;
    qa_vec3 movement = classic ? qa_v3((float)s->pmove.origin[0] / 8,
        (float)s->pmove.origin[1] / 8, (float)s->pmove.origin[2] / 8) :
        qa_v3(s->pmove.origin_f[0], s->pmove.origin_f[1], s->pmove.origin_f[2]);
    /* Classic preserves double sums; rerelease rounds each ABI float sum. */
    double origin[3] = {classic ? (double)movement.x + s->viewoffset[0] : (float)(movement.x + s->viewoffset[0]),
        classic ? (double)movement.y + s->viewoffset[1] : (float)(movement.y + s->viewoffset[1]),
        classic ? movement.z : (float)(movement.z + s->viewoffset[2])};
    bool ok = text(j, "{\"origin\":{\"x\":", e) && number(j, origin[0], e) && text(j, ",\"y\":", e) &&
        number(j, origin[1], e) && text(j, ",\"z\":", e) && number(j, origin[2], e) &&
        text(j, "},\"angles\":", e) && vector(j, s->viewangles, e) && text(j, ",\"viewHeight\":", e) &&
        number(j, classic ? s->viewoffset[2] : s->pmove.viewheight, e) &&
        text(j, ",\"kickAngles\":", e) && vector(j, s->kick_angles, e) &&
        text(j, ",\"fieldOfView\":", e) && number(j, s->fov, e) && text(j, ",\"blend\":", e) && color(j, s->blend, e);
    if (ok && !classic) ok = text(j, ",\"damageBlend\":", e) && color(j, s->damage_blend, e);
    return ok && text(j, classic ? ",\"native\":{\"edition\":\"classic\",\"movementOrigin\":" :
        ",\"native\":{\"edition\":\"rerelease\",\"movementOrigin\":", e) &&
        application_unified_json_vector(j, movement, e) && text(j, ",\"renderFlags\":", e) && number(j, s->rdflags, e) &&
        text(j, s->pmove.type != (classic ? 4 : 6) && !(s->pmove.flags & 64) ?
            ",\"positionPrediction\":true" : ",\"positionPrediction\":false", e) &&
        text(j, s->pmove.type < (classic ? 2 : 4) && (classic || !(s->pmove.flags & 256)) ?
            ",\"angularPrediction\":true" : ",\"angularPrediction\":false", e) &&
        text(j, s->gunindex && (!classic || s->fov <= 90) ?
            ",\"weaponVisible\":true}}" : ",\"weaponVisible\":false}}", e);
}

static bool document(application_unified_json *j, qa_unified_document **out, qa_error *e)
{ return qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){j->bytes.data, j->bytes.size}, out, e); }

bool application_unified_q2_source_documents_build(qa_application *app,
    const application_unified_source *source, qa_net_client_id recipient,
    const qa_unified_session_player *player, application_unified_q2_source_documents *out, qa_error *e)
{
    if (!source || !player || !out || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 documents require the actual Source and remote recipient");
    application_unified_q2_source_documents v = {.source = *source, .recipient = recipient, .player = *player,
        .actors_revision = qa_actors_revision(qa_session_actors(source->session))};
    bool found;
    if (!qa_application_native_q2_presentation_selected(app, &v.physical, &found, e)) return false;
    if (!found || v.physical.kind != QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        if (!application_unified_q2_source_documents_current(app, &v))
            return application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 absence changed its Source or recipient");
        *out = v;
        return true;
    }
    if (source->family != QA_GAME_Q2 || v.physical.source_owner != source->owner ||
        v.physical.publication != source->launch || v.physical.session != source->session)
        return application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 documents differ from their primary physical Source");
    struct application_native_q2 *native;
    if (!physical_player(app, &v, &native, &v.player_state, e)) return false;
    v.config_revision = native->config_revision;
    memcpy(v.layout, native->clients[player->source_slot].layout, sizeof(v.layout));
    memcpy(v.inventory, native->clients[player->source_slot].inventory, sizeof(v.inventory));
    application_unified_json state = {0}, frame = {0}, view = {0};
    bool ok = hud_state(&state, native, &v, e) && hud_frame(&frame, &v, e) && camera(&view, &v, e) &&
        document(&state, &v.hud_state, e) && document(&frame, &v.hud_frame, e) && document(&view, &v.camera, e);
    v.present = ok;
    if (ok && !application_unified_q2_source_documents_current(app, &v))
        ok = application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 documents changed their physical Source or recipient");
    application_unified_json_dispose(&state); application_unified_json_dispose(&frame);
    application_unified_json_dispose(&view);
    if (!ok) { application_unified_q2_source_documents_dispose(&v); return false; }
    *out = v;
    return true;
}

void application_unified_q2_source_documents_dispose(application_unified_q2_source_documents *v)
{
    if (!v) return;
    qa_unified_document_destroy(v->hud_state); qa_unified_document_destroy(v->hud_frame);
    qa_unified_document_destroy(v->camera);
    *v = (application_unified_q2_source_documents){0};
}
