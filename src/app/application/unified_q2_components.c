#include "unified_q2_components.h"
#include "unified_frame_private.h"
#include "unified_output_json.h"
#include "guest_native_q2_private.h"
#include "qa/native_host_q2_wire.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool integers(application_unified_json *j, const int16_t *values, size_t count, qa_error *e)
{
    if (!text(j, "[", e)) return false;
    for (size_t i = 0; i < count; ++i)
        if ((i && !text(j, ",", e)) || !number(j, values[i], e)) return false;
    return text(j, "]", e);
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
    if (!v->present) return !v->hud_state;
    struct application_native_q2 *native;
    qa_q2_player state;
    return v->hud_state &&
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

static bool hud_frame(application_unified_q2_source_documents *v, qa_error *e)
{
    const qa_source_frame *frame = &v->physical.clock.frame;
    if (frame->number > INT32_MAX)
        return application_fail(e, QA_ERROR_FORMAT, "Unified Q2 HUD frame exceeds its signed Source counter");
    qa_unified_native_hud *out = &v->hud_frame;
    out->stat_count = v->physical.edition == QA_Q2_CLASSIC ? 32 : 64;
    memcpy(out->stats, v->player_state.stats, out->stat_count * sizeof(*out->stats));
    out->server_frame = (int32_t)frame->number;
    out->time_ms = (double)v->physical.server_time_ns / 1000000.0;
    out->has_frame_time = frame->elapsed_ns != 0;
    out->frame_time_ms = (double)frame->elapsed_ns / 1000000.0;
    return true;
}

static void camera(application_unified_q2_source_documents *v)
{
    const qa_q2_player *s = &v->player_state;
    bool classic = v->physical.edition == QA_Q2_CLASSIC;
    qa_unified_native_camera *out = &v->camera;
    qa_vec3 movement = classic ? qa_v3((float)s->pmove.origin[0] / 8,
        (float)s->pmove.origin[1] / 8, (float)s->pmove.origin[2] / 8) :
        qa_v3(s->pmove.origin_f[0], s->pmove.origin_f[1], s->pmove.origin_f[2]);
    /* Classic preserves double sums; rerelease rounds each ABI float sum. */
    out->origin[0] = classic ? (double)movement.x + s->viewoffset[0] : (float)(movement.x + s->viewoffset[0]);
    out->origin[1] = classic ? (double)movement.y + s->viewoffset[1] : (float)(movement.y + s->viewoffset[1]);
    out->origin[2] = classic ? movement.z : (float)(movement.z + s->viewoffset[2]);
    for (size_t i = 0; i < 3; ++i) { out->angles[i] = s->viewangles[i]; out->kick_angles[i] = s->kick_angles[i]; }
    out->view_height = classic ? (double)s->viewoffset[2] : (double)s->pmove.viewheight;
    out->field_of_view = s->fov;
    for (size_t i = 0; i < 4; ++i) { out->blend[i] = s->blend[i]; if (!classic) out->damage_blend[i] = s->damage_blend[i]; }
    out->rerelease = !classic; out->movement_origin = movement; out->render_flags = s->rdflags;
    out->position_prediction = s->pmove.type != (classic ? 4 : 6) && !(s->pmove.flags & 64);
    out->angular_prediction = s->pmove.type < (classic ? 2 : 4) && (classic || !(s->pmove.flags & 256));
    out->weapon_visible = s->gunindex && (!classic || s->fov <= 90);
}

static bool document(application_unified_json *j, qa_unified_document **out, qa_error *e)
{ return qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){j->bytes.data, j->bytes.size}, out, e); }

bool application_unified_q2_source_documents_build(qa_application *app,
    const application_unified_source *source, qa_net_client_id recipient,
    const qa_unified_session_player *player, qa_unified_frame *target,
    application_unified_q2_source_documents *out, qa_error *e)
{
    if (!source || !player || !target || !target->lease || !out || !application_unified_source_current(app, source) ||
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
    application_unified_json state = {0};
    bool ok = hud_state(&state, native, &v, e) && hud_frame(&v, e) && document(&state, &v.hud_state, e);
    if (ok) camera(&v);
    v.present = ok;
    if (ok && !application_unified_q2_source_documents_current(app, &v))
        ok = application_fail(e, QA_ERROR_ARGUMENT, "Unified Q2 documents changed their physical Source or recipient");
    application_unified_json_dispose(&state);
    if (!ok) { application_unified_q2_source_documents_dispose(&v); return false; }
    *out = v;
    return true;
}

void application_unified_q2_source_documents_dispose(application_unified_q2_source_documents *v)
{
    if (!v) return;
    qa_unified_document_destroy(v->hud_state);
    *v = (application_unified_q2_source_documents){0};
}

static bool publication_owner(application_unified_json *j,
    const application_unified_q2_component_documents *v, qa_error *e)
{
    const char *name = qa_strings_cstr(qa_session_strings(v->source.source.session), v->publication.owner);
    return name && text(j, "{\"provider\":", e) && application_unified_json_string(j, name, e) &&
        text(j, ",\"generation\":", e) && application_unified_json_natural(j, v->publication.activation_generation, e) &&
        text(j, "}", e);
}

bool application_unified_q2_component_documents_build(qa_application *app,
    const application_unified_source *source, qa_net_client_id recipient,
    const qa_unified_session_player *player, qa_unified_frame *target,
    application_unified_q2_component_documents *out, qa_error *e)
{
    if (!out || !source || !player || !target || !target->lease || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Native component documents require their actual Source recipient");
    application_unified_q2_component_documents v = {0}; bool registered;
    if (!application_native_q2_publication_read(app, source, &v.publication, &registered, e)) return false;
    if (!registered) {
        v.source.source = *source; v.source.recipient = recipient; v.source.player = *player;
        v.source.actors_revision = qa_actors_revision(qa_session_actors(source->session));
        if (!application_unified_q2_component_documents_current(app, &v))
            return application_fail(e, QA_ERROR_ARGUMENT, "Native component absence changed its actual Source recipient");
        *out = v;
        return true;
    }
    if (!application_unified_q2_source_documents_build(app, source, recipient, player, target, &v.source, e)) return false;
    if (!v.source.present) {
        application_unified_q2_component_documents_dispose(&v);
        return application_fail(e, QA_ERROR_ARGUMENT, "Registered native component has no admitted physical HUD source");
    }
    application_unified_json state = {0};
    bool ok = text(&state, "{\"owner\":", e) && publication_owner(&state, &v, e) &&
        text(&state, ",\"identity\":", e) && application_unified_json_document(&state, v.publication.identity, e) &&
        text(&state, ",\"generation\":", e) && application_unified_json_natural(&state, v.publication.generation, e) &&
        text(&state, ",\"hud\":", e);
    if (ok && v.publication.hud == APPLICATION_NATIVE_Q2_HUD_NONE) ok = text(&state, "null", e);
    else if (ok) ok = text(&state, v.publication.hud == APPLICATION_NATIVE_Q2_HUD_OVERLAY ?
        "{\"mode\":\"layout-overlay\",\"frame\":" : "{\"mode\":\"replace-status\",\"frame\":", e) &&
        application_unified_json_document(&state, v.source.hud_state, e) && text(&state, "}", e);
    if (ok) ok = text(&state, "}", e) && document(&state, &v.state, e);
    if (ok) {
        v.frame = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*v.frame),
            _Alignof(qa_unified_frame_components), e);
        if (v.frame) v.frame->native = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*v.frame->native),
            _Alignof(qa_unified_native_component), e);
        if (!v.frame || !v.frame->native) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining native component frame");
    }
    if (ok) {
        v.frame->native_count = 1;
        qa_unified_native_component *row = v.frame->native;
        const char *provider = qa_strings_cstr(qa_session_strings(v.source.source.session), v.publication.owner);
        ok = application_unified_frame_string(target->lease, &row->owner.provider, provider, e);
        row->owner.generation = v.publication.activation_generation;
        row->generation = v.publication.generation; row->viewer = player->actor;
        if (ok && v.publication.hud != APPLICATION_NATIVE_Q2_HUD_NONE) {
            row->hud = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*row->hud),
                _Alignof(qa_unified_native_hud), e);
            if (!row->hud) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining native component HUD");
            else *row->hud = v.source.hud_frame;
        }
        if (ok && v.publication.camera) {
            row->view = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*row->view),
                _Alignof(qa_unified_native_camera), e);
            if (!row->view) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining native component camera");
            else *row->view = v.source.camera;
        }
    }
    v.present = ok;
    if (ok && !application_unified_q2_component_documents_current(app, &v))
        ok = application_fail(e, QA_ERROR_ARGUMENT, "Native component changed its registration or receiver during publication");
    application_unified_json_dispose(&state);
    if (!ok) { application_unified_q2_component_documents_dispose(&v); return false; }
    *out = v;
    return true;
}

bool application_unified_q2_component_documents_current(qa_application *app,
    const application_unified_q2_component_documents *v)
{
    if (!v || !application_unified_q2_source_documents_current(app, &v->source)) return false;
    if (!v->present) {
        application_native_q2_publication_view actual; bool found;
        return !v->state && !v->frame &&
            application_native_q2_publication_read(app, &v->source.source, &actual, &found, NULL) && !found;
    }
    return v->state && v->frame && application_native_q2_publication_current(app,
        &v->source.source, &v->publication);
}

void application_unified_q2_component_documents_dispose(application_unified_q2_component_documents *v)
{
    if (!v) return;
    qa_unified_document_destroy(v->state);
    application_unified_q2_source_documents_dispose(&v->source);
    *v = (application_unified_q2_component_documents){0};
}
