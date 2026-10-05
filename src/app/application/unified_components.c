#include "unified_components_internal.h"
#include "unified_frame_private.h"
#include "unified_output_json.h"
#include "guest_q3_components.h"
#include "native_q2_publication.h"
#include "unified_q2_components.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool string(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_string(j, s, e); }

static bool bytes(application_unified_json *j, qa_bytes value, qa_error *e)
{
    qa_buffer encoded = {0};
    bool ok = qa_unified_checkpoint_bytes(value, &encoded, e) &&
        application_unified_json_append(j, (qa_bytes){encoded.data, encoded.size}, e);
    qa_buffer_free(&encoded);
    return ok;
}

static bool owner_write(application_unified_json *j, const application_unified_component_capture *v,
    const component_cursor *row, qa_error *e)
{
    const char *id = qa_strings_cstr(qa_session_strings(v->source.session), row->owner);
    return text(j, "{\"provider\":", e) && string(j, id, e) && text(j, ",\"generation\":", e) &&
        application_unified_json_natural(j, row->generation, e) && text(j, "}", e);
}

static bool identity_hash(const qa_unified_document *identity, qa_sha256_digest *out, qa_error *e)
{
    if (!identity || qa_unified_document_type(identity) != QA_UNIFIED_CHECKPOINT)
        return application_fail(e, QA_ERROR_ARGUMENT, "Component has no admitted immutable ModIdentity");
    qa_buffer canonical = {0};
    bool ok = qa_unified_value_canonical(qa_json_source(qa_unified_document_json(identity),
        qa_unified_document_root(identity)), &canonical, e);
    if (ok) qa_sha256((qa_bytes){canonical.data, canonical.size}, out);
    qa_buffer_free(&canonical);
    return ok;
}

static bool json_hash(const qa_json_document *j, qa_json_id value, qa_sha256_digest *out, qa_error *e)
{
    qa_buffer canonical = {0};
    bool ok = qa_unified_value_canonical(qa_json_source(j, value), &canonical, e);
    if (ok) qa_sha256((qa_bytes){canonical.data, canonical.size}, out);
    qa_buffer_free(&canonical);
    return ok;
}

static bool native_states(application_unified_json *j, const application_unified_component_capture *v,
    bool configs, qa_error *e)
{
    if (configs || v->native_documents.publication.hud == APPLICATION_NATIVE_Q2_HUD_NONE)
        return application_unified_json_document(j, v->native_documents.state, e);
    const qa_json_document *json = qa_unified_document_json(v->native_documents.state);
    qa_json_id root = qa_unified_document_root(v->native_documents.state);
    qa_json_id hud = qa_json_get(json, root, "hud"), frame = qa_json_get(json, hud, "frame");
    static const char *const fields[] = {"owner", "identity", "generation"};
    bool ok = text(j, "{", e);
    for (size_t i = 0; ok && i < sizeof(fields) / sizeof(fields[0]); ++i)
        ok = (!i || text(j, ",", e)) && string(j, fields[i], e) && text(j, ":", e) &&
            application_unified_json_append(j, qa_json_source(json, qa_json_get(json, root, fields[i])), e);
    if (ok) ok = text(j, ",\"hud\":{\"mode\":", e) && application_unified_json_append(j,
        qa_json_source(json, qa_json_get(json, hud, "mode")), e) && text(j, ",\"frame\":{\"configstrings\":null", e);
    static const char *const frame_fields[] = {"protocol", "layout", "inventory", "playerNumber"};
    for (size_t i = 0; ok && i < sizeof(frame_fields) / sizeof(frame_fields[0]); ++i)
        ok = text(j, ",", e) && string(j, frame_fields[i], e) && text(j, ":", e) &&
            application_unified_json_append(j, qa_json_source(json, qa_json_get(json, frame, frame_fields[i])), e);
    return ok && text(j, "}}}", e);
}

static bool native_prepare(application_unified_component_capture *v, application_unified_json *states, bool *changed, qa_error *e)
{
    qa_application *app = v->owner->application;
    if (!application_unified_q2_component_documents_build(app, &v->source, v->owner->recipient,
        &v->player, v->target, &v->native_documents, e)) return false;
    const native_cursor *old = v->owner->epoch == v->epoch ? &v->owner->native : NULL;
    if (!v->native_documents.present) { *changed |= v->owner->native.present; return text(states, "[]", e); }
    const application_native_q2_publication_view *p = &v->native_documents.publication;
    v->native = (native_cursor){.owner=p->owner, .activation=p->activation_generation,
        .generation=p->generation, .present=true};
    for (size_t i = 0; i < v->count; ++i)
        if (v->rows[i].owner == p->owner)
            return application_fail(e, QA_ERROR_FORMAT, "Native and original components alias one presentation owner");
    if (v->count >= 256)
        return application_fail(e, QA_ERROR_FORMAT, "Combined components exceed their wire owner extent");
    if (!identity_hash(p->identity, &v->native.identity, e)) return false;
    bool same = old && old->present && old->owner == p->owner && old->activation == p->activation_generation &&
        old->generation == p->generation;
    if (same && !qa_sha256_equal(&old->identity, &v->native.identity))
        return application_fail(e, QA_ERROR_FORMAT, "Native component activation changed its actual identity");
    const qa_json_document *json = qa_unified_document_json(v->native_documents.source.hud_state);
    qa_json_id root = qa_unified_document_root(v->native_documents.source.hud_state);
    if (!json_hash(json, qa_json_get(json, root, "configstrings"), &v->native.configs, e)) return false;
    if (!identity_hash(v->native_documents.state, &v->native.state, e)) return false;
    *changed |= !same || !qa_sha256_equal(&old->state, &v->native.state);
    bool ok = text(states, "[", e) && native_states(states, v,
        !same || !qa_sha256_equal(&old->configs, &v->native.configs), e) && text(states, "]", e);
    if (ok) {
        v->frame->native = v->native_documents.frame->native;
        v->frame->native_count = v->native_documents.frame->native_count;
        v->native_documents.frame->native = NULL;
        v->native_documents.frame->native_count = 0;
    }
    return ok;
}

static bool game_state(application_unified_json *j, const qa_q3_gamestate *gs, qa_error *e)
{
    if (!gs || gs->string_bytes > QA_Q3_GAMESTATE_CHARS || gs->strings[0])
        return application_fail(e, QA_ERROR_FORMAT, "Component gameState lost its actual source extent");
    if (!text(j, "{\"stringOffsets\":[", e)) return false;
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        if ((i && !text(j, ",", e)) || !number(j, gs->config_offsets[i], e)) return false;
    return text(j, "],\"stringData\":", e) &&
        bytes(j, (qa_bytes){(const uint8_t *)gs->strings, QA_Q3_GAMESTATE_CHARS}, e) &&
        text(j, ",\"dataCount\":", e) && application_unified_json_natural(j, gs->string_bytes, e) && text(j, "}", e);
}

static bool commands(application_unified_json *j, const application_q3_scene_context *context,
    int32_t base, int32_t through, qa_error *e)
{
    int32_t previous = base;
    bool first = true;
    if (!text(j, "[", e)) return false;
    for (size_t i = 0; i < context->command_count; ++i) {
        const application_q3_scene_command *row = context->commands + i;
        if (row->sequence <= base) continue;
        if (previous == INT32_MAX || row->sequence != previous + 1 || row->sequence > through)
            return application_fail(e, QA_ERROR_FORMAT, "Component reliable commands exceeded their retained window");
        qa_q3_tokens tokens = {0};
        if (row->addressed && !qa_q3_tokenize(row->text, &tokens, e)) return false;
        if (tokens.truncated || tokens.count > 128)
            return application_fail(e, QA_ERROR_FORMAT, "Component command exceeds its authentic argument extent");
        if ((!first && !text(j, ",", e)) || !text(j, "{\"sequence\":", e) || !number(j, row->sequence, e) ||
            !text(j, ",\"arguments\":[", e)) return false;
        for (size_t a = 0; a < tokens.count; ++a) {
            const char *argument = qa_q3_token(&tokens, a);
            if (strlen(argument) > 8192)
                return application_fail(e, QA_ERROR_FORMAT, "Component command argument exceeds its wire extent");
            if ((a && !text(j, ",", e)) || !string(j, argument, e)) return false;
        }
        if (!text(j, "]}", e)) return false;
        previous = row->sequence; first = false;
    }
    if (previous != through)
        return application_fail(e, QA_ERROR_FORMAT, "Component reliable command tail is not retained");
    return text(j, "]", e);
}

static bool frame_source(qa_unified_component_source *out, const application_unified_component_capture *v,
    const component_cursor *row, const application_q3_scene_context *context, qa_error *e)
{
    const qa_q3_snapshot *snap = context->snapshot;
    if (!snap || !context->has_weapon_presented || context->actor_count > QA_Q3_ENTITIES ||
        snap->entity_count > 256 || snap->server_time < 0 || context->revision < 0 ||
        context->revision > (int64_t)QA_UNIFIED_SAFE_INTEGER ||
        context->client_number < 0 || context->client_number >= QA_Q3_ENTITIES)
        return application_fail(e, QA_ERROR_FORMAT, "Component frame lacks its real receiver snapshot or weapon policy");
    const char *provider = qa_strings_cstr(qa_session_strings(v->source.session), row->owner);
    if (!application_unified_frame_string(v->lease, &out->owner.provider, provider, e)) return false;
    out->owner.generation = row->generation;
    out->abi = row->abi; out->viewer = v->player.actor; out->client_number = context->client_number;
    out->game_state_revision = row->game_state_revision; out->weapon_presented = context->weapon_presented;
    out->scene = row->scene;
    out->snapshot = (qa_q3_snapshot){.valid=true, .server_time=snap->server_time,
        .player=snap->player, .server_command_number=row->sequence};
    out->bindings = qa_unified_frame_lease_alloc(v->lease, context->actor_count, sizeof(*out->bindings),
        _Alignof(qa_unified_component_binding), e);
    if (context->actor_count && !out->bindings)
        return application_fail(e, QA_ERROR_MEMORY, "Retaining component Source bindings");
    out->binding_count = context->actor_count;
    for (size_t i = 0; i < context->actor_count; ++i) {
        const application_q3_scene_actor *binding = context->actors + i;
        out->bindings[i] = (qa_unified_component_binding){binding->slot, binding->actor, binding->owned};
    }
    if (row->scene) {
        out->scene_revision = context->revision; out->snapshot.flags = snap->flags; out->snapshot.area_bytes = 32;
        memcpy(out->snapshot.area_mask, snap->area_mask, sizeof(out->snapshot.area_mask));
        qa_q3_entity *entities = qa_unified_frame_lease_alloc(v->lease, snap->entity_count, sizeof(*entities),
            _Alignof(qa_q3_entity), e);
        if (snap->entity_count && !entities)
            return application_fail(e, QA_ERROR_MEMORY, "Retaining component Source snapshot entities");
        out->snapshot.entities = entities; out->snapshot.entity_count = snap->entity_count;
        if (snap->entity_count) memcpy(entities, snap->entities, snap->entity_count * sizeof(*entities));
    }
    return true;
}

static bool camera(const qa_unified_frame_player *player, qa_vec3 *origin, qa_vec3 axis[3], qa_error *e)
{
    const qa_unified_player_view *view = &player->view;
    if (!qa_vec_finite(view->origin) || !qa_vec_finite(view->angles) || !isfinite(view->view_height))
        return application_fail(e, QA_ERROR_FORMAT, "Component receiver camera contains a nonfinite scalar");
    *origin = view->origin; origin->z += view->view_height;
    qa_builtin_angle_vectors(view->angles, axis, axis + 1, axis + 2);
    axis[1] = qa_vec_scale(axis[1], -1);
    return qa_vec_finite(*origin) || application_fail(e, QA_ERROR_FORMAT, "Component receiver camera leaves its Source scalar extent");
}

bool application_unified_components_create(qa_application *app, qa_net_client_id recipient,
    qa_actor_id actor, application_unified_component_publisher **out, qa_error *e)
{
    if (!app || !out || *out || !recipient.owner || !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component publisher requires its actual admitted recipient");
    application_unified_component_publisher *p = calloc(1, sizeof(*p));
    if (!p) return application_fail(e, QA_ERROR_MEMORY, "Retaining recipient component publication");
    p->application = app; p->recipient = recipient; p->actor = actor; *out = p;
    return true;
}
bool application_unified_components_reserve(application_unified_component_publisher *p,
    size_t count, qa_error *e)
{
    if (count <= p->capacity) return true;
    component_cursor *rows = realloc(p->rows, count * sizeof(*rows));
    if (!rows) return application_fail(e, QA_ERROR_MEMORY, "Growing recipient component cursors");
    p->rows = rows; p->capacity = count; return true;
}
bool application_unified_components_idle(const application_unified_component_publisher *p)
{ return !p || !p->pending; }
bool application_unified_components_destroy(application_unified_component_publisher **out, qa_error *e)
{
    if (!out || !*out) return true;
    application_unified_component_publisher *p = *out;
    if (!application_unified_components_idle(p))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component publisher retains its prepared reliable update");
    free(p->rows); free(p); *out = NULL; return true;
}

bool application_unified_components_current(const application_unified_component_capture *v)
{
    if (!v || v->committed || v->owner->pending != v || v->owner->serial != v->serial) return false;
    if (v->sealed) return true;
    qa_application *app = v->owner->application;
    if (!application_unified_source_current(app, &v->source) ||
        !application_unified_player_current(app, v->owner->recipient, &v->player) || app->components != v->roster) return false;
    for (size_t i = 0; i < v->count; ++i)
        if (!application_q3_components_publication_current(v->leases[i])) return false;
    return application_unified_q2_component_documents_current(app, &v->native_documents);
}

bool application_unified_components_prepare(application_unified_component_publisher *p,
    const application_unified_source *source, const qa_unified_session_player *player, uint32_t epoch,
    qa_unified_frame *target, const qa_unified_frame_player *values,
    application_unified_component_capture **out, qa_error *e)
{
    if (!p || !source || !player || !target || !target->lease || !values || !epoch ||
        !out || *out || p->pending || p->serial == UINT64_MAX ||
        !qa_actor_id_equal(player->actor, p->actor) || !application_unified_source_current(p->application, source) ||
        !application_unified_player_current(p->application, p->recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component capture requires its actual returned Source and recipient");
    if (!qa_unified_frame_lease_retain(target->lease, e)) return false;
    application_unified_component_capture *v = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*v),
        _Alignof(application_unified_component_capture), e);
    if (!v) { qa_unified_frame_lease_release(target->lease); return false; }
    v->lease = target->lease; v->target = target;
    v->owner = p; v->source = *source; v->player = *player; v->roster = p->application->components;
    v->epoch = epoch; v->serial = p->serial; v->revision = p->epoch == epoch ? p->revision : 0; p->pending = v;
    size_t capacity = application_q3_components_publication_count(v->roster);
    if (capacity > 256) { application_unified_components_dispose(v); return application_fail(e, QA_ERROR_FORMAT, "Component roster exceeds its wire owner extent"); }
    v->rows = qa_unified_frame_lease_alloc(v->lease, capacity, sizeof(*v->rows),
        _Alignof(component_cursor), e);
    v->leases = qa_unified_frame_lease_alloc(v->lease, capacity, sizeof(*v->leases),
        _Alignof(application_q3_component_publication_lease *), e);
    bool ok = (!capacity || (v->rows && v->leases)) &&
        application_unified_components_reserve(p, capacity, e);
    if (!ok) application_fail(e, QA_ERROR_MEMORY, "Retaining actual component rows and Source leases");
    qa_vec3 origin, axis[3];
    if (ok && capacity) ok = camera(values, &origin, axis, e);
    v->frame = qa_unified_frame_lease_alloc(v->lease, 1, sizeof(*v->frame),
        _Alignof(qa_unified_frame_components), e);
    if (v->frame) v->frame->sources = qa_unified_frame_lease_alloc(v->lease, capacity, sizeof(*v->frame->sources),
        _Alignof(qa_unified_component_source), e);
    if (!v->frame || (capacity && !v->frame->sources))
        ok = application_fail(e, QA_ERROR_MEMORY, "Retaining typed component frame rows");
    application_unified_json states = {0};
    if (ok) ok = text(&states, "[", e);
    bool changed = p->epoch != epoch && p->count != 0;
    for (size_t index = 0; ok && index < capacity; ++index) {
        application_q3_component_publication publication;
        ok = application_q3_components_publication_at(v->roster, index, &publication, e);
        if (!ok || !publication.presentation_runtime) continue;
        bool scene = !strcmp(publication.presentation_runtime, "qvm-scene");
        if (!scene && strcmp(publication.presentation_runtime, "qvm-player-events")) {
            ok = application_fail(e, QA_ERROR_FORMAT, "Component presentation has no actual admitted runtime"); break;
        }
        qa_clock_state clock;
        if (!qa_session_clock(source->session, publication.owner, &clock) || clock.frame.kind != QA_CLOCK_Q3) {
            ok = application_fail(e, QA_ERROR_ARGUMENT, "Component publication lost its real Source clock"); break;
        }
        uint32_t word = (uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
        int32_t time; memcpy(&time, &word, sizeof(time));
        if (clock.frame.elapsed_ns / UINT64_C(1000000) > INT32_MAX) {
            ok = application_fail(e, QA_ERROR_FORMAT, "Component Source interval leaves its actual millisecond extent"); break;
        }
        application_q3_scene_context context = {0};
        size_t at = v->count++;
        ok = application_q3_components_publication_borrow(v->roster, index, player->actor, &origin, axis,
            time, (int32_t)(clock.frame.elapsed_ns / UINT64_C(1000000)), v->leases + at, &context, e);
        component_cursor *row = v->rows + at;
        *row = (component_cursor){.owner = publication.owner, .generation = publication.generation,
            .game_state_revision = context.game_state_revision, .sequence = scene && ok && context.snapshot ? context.snapshot->server_command_number : 0,
            .abi = publication.abi, .scene = scene};
        for (size_t i = 0; ok && i < at; ++i)
            if (v->rows[i].owner == row->owner)
                ok = application_fail(e, QA_ERROR_FORMAT, "Component publication aliases an actual owner");
        if (ok) ok = identity_hash(publication.identity, &row->identity, e);
        const component_cursor *old = NULL;
        if (p->epoch == epoch) for (size_t i = 0; i < p->count; ++i)
            if (p->rows[i].owner == row->owner && p->rows[i].generation == row->generation) old = p->rows + i;
        if (ok && old && (old->abi != row->abi || old->scene != row->scene || !qa_sha256_equal(&old->identity, &row->identity)))
            ok = application_fail(e, QA_ERROR_FORMAT, "Component activation changed its actual identity or runtime");
        if (ok && (row->game_state_revision < 0 || row->game_state_revision > (int64_t)QA_UNIFIED_SAFE_INTEGER ||
            row->sequence < 0 || (old && (row->game_state_revision < old->game_state_revision || row->sequence < old->sequence))))
            ok = application_fail(e, QA_ERROR_FORMAT, "Component Source continuation moved backward");
        bool game_changed = !old || old->game_state_revision != row->game_state_revision;
        int32_t base = old ? old->sequence : row->sequence;
        changed |= !old || game_changed || base != row->sequence;
        if (ok) {
            v->frame->source_count = at + 1;
            ok = frame_source(v->frame->sources + at, v, row, &context, e);
        }
        if (ok) ok = (!at || text(&states, ",", e)) && text(&states, "{\"owner\":", e) && owner_write(&states, v, row, e) &&
            text(&states, ",\"identity\":", e) && application_unified_json_document(&states, publication.identity, e) &&
            text(&states, ",\"generation\":", e) && application_unified_json_natural(&states, row->generation, e) &&
            text(&states, ",\"abi\":", e) && string(&states, row->abi == QA_QVM_Q3_MODERN ? "q3-modern" : "q3-1.16n-base", e) &&
            text(&states, ",\"runtime\":", e) && string(&states, publication.presentation_runtime, e) &&
            text(&states, ",\"gameStateRevision\":", e) && number(&states, (double)row->game_state_revision, e) &&
            text(&states, ",\"gameState\":", e) && (game_changed ? game_state(&states, context.game_state, e) : text(&states, "null", e)) &&
            text(&states, ",\"commandBase\":", e) && number(&states, base, e) && text(&states, ",\"commands\":", e) &&
            (scene ? commands(&states, &context, base, row->sequence, e) : text(&states, "[]", e)) && text(&states, "}", e);
    }
    application_unified_json native_states_json = {0};
    if (ok) ok = native_prepare(v, &native_states_json, &changed, e);
    changed |= v->count != p->count;
    for (size_t i = 0; i < v->count && !changed; ++i)
        changed = v->rows[i].owner != p->rows[i].owner || v->rows[i].generation != p->rows[i].generation;
    if (ok && changed) {
        if (v->revision == QA_UNIFIED_SAFE_INTEGER) ok = application_fail(e, QA_ERROR_FORMAT, "Component publication revision is exhausted");
        else ++v->revision;
    }
    if (ok) { v->frame->revision = v->revision; ok = text(&states, "]", e); }
    application_unified_json document = {0};
    if (ok && changed) ok = text(&document, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"components\",\"epoch\":", e) &&
        number(&document, epoch, e) && text(&document, ",\"update\":{\"revision\":", e) &&
        application_unified_json_natural(&document, v->revision, e) && text(&document, ",\"native\":", e) &&
        application_unified_json_append(&document, (qa_bytes){native_states_json.bytes.data, native_states_json.bytes.size}, e) &&
        text(&document, ",\"sources\":", e) &&
        application_unified_json_append(&document, (qa_bytes){states.bytes.data, states.bytes.size}, e) && text(&document, "}}}", e) &&
        qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT, (qa_bytes){document.bytes.data, document.bytes.size}, &v->control, e);
    application_unified_json_dispose(&document); application_unified_json_dispose(&states);
    application_unified_json_dispose(&native_states_json);
    if (ok && !application_unified_components_current(v)) ok = application_fail(e, QA_ERROR_ARGUMENT, "Component Source retired while its output was assembled");
    if (!ok) { application_unified_components_dispose(v); return false; }
    *out = v; return true;
}

const qa_unified_frame_components *application_unified_components_frame(const application_unified_component_capture *v)
{ return v ? v->frame : NULL; }
const qa_unified_document *application_unified_components_control(const application_unified_component_capture *v)
{ return v ? v->control : NULL; }
qa_unified_frame_components *application_unified_components_take(application_unified_component_capture *v)
{ if (!v || v->frame_document) return NULL; qa_unified_frame_components *out = v->frame; v->frame = NULL; return out; }
bool application_unified_components_bind_frame(application_unified_component_capture *v,
    const qa_unified_document *document, qa_error *e)
{
    const qa_unified_frame *frame = qa_unified_document_frame(document);
    if (!v || !frame || frame->lease != v->lease || !frame->components || frame->epoch != v->epoch ||
        frame->components->revision != v->revision || frame->components->source_count != v->count ||
        frame->components->native_count != (v->native.present ? 1u : 0u) ||
        (v->frame_document && v->frame_document != document) ||
        (v->frame && v->frame != frame->components))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component queue token requires its canonical completed FRAME");
    if (!v->frame_document && !qa_unified_document_retain(document, &v->frame_document, e)) return false;
    v->frame = frame->components; return true;
}
bool application_unified_components_seal(application_unified_component_capture *v,
    const qa_unified_document *frame, qa_error *e)
{
    if (!application_unified_components_current(v))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component queue token lost its actual prepared publisher");
    if (!application_unified_components_bind_frame(v, frame, e)) return false;
    if (!v->sealed) {
        for (size_t i = 0; i < v->count; ++i) application_q3_components_publication_return(v->leases + i);
        v->leases = NULL; v->sealed = true;
    }
    return true;
}
void application_unified_components_commit(application_unified_component_capture *v)
{
    if (!v || !v->sealed || v->committed || v->owner->pending != v || v->owner->serial != v->serial) return;
    application_unified_component_publisher *p = v->owner;
    if (v->count) memcpy(p->rows, v->rows, v->count * sizeof(*p->rows));
    p->count = v->count;
    p->epoch = v->epoch; p->revision = v->revision; ++p->serial; p->pending = NULL; v->committed = true;
    p->native = v->native;
}
void application_unified_components_dispose(application_unified_component_capture *v)
{
    if (!v) return;
    if (v->leases) for (size_t i = 0; i < v->count; ++i) application_q3_components_publication_return(v->leases + i);
    if (v->owner->pending == v) v->owner->pending = NULL;
    qa_unified_frame_lease *lease = v->lease;
    qa_unified_document_destroy(v->frame_document);
    qa_unified_document_destroy(v->control);
    application_unified_q2_component_documents_dispose(&v->native_documents);
    qa_unified_frame_lease_release(lease);
}
