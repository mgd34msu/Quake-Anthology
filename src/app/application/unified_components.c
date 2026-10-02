#include "unified_components.h"
#include "unified_output_json.h"
#include "guest_q3_components.h"
#include "qa/q3_abi.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct component_cursor {
    qa_actor_owner owner;
    uint64_t generation;
    int64_t game_state_revision;
    int32_t sequence;
    qa_qvm_abi abi;
    bool scene;
    qa_sha256_digest identity;
} component_cursor;

struct application_unified_component_publisher {
    qa_application *application;
    qa_net_client_id recipient;
    qa_actor_id actor;
    uint64_t revision, serial;
    uint32_t epoch;
    component_cursor *rows;
    size_t count;
    application_unified_component_capture *pending;
};

struct application_unified_component_capture {
    application_unified_component_publisher *owner;
    application_unified_source source;
    qa_unified_session_player player;
    application_q3_components *roster;
    application_q3_component_publication_lease **leases;
    component_cursor *rows;
    size_t count;
    uint64_t revision, serial;
    uint32_t epoch;
    qa_unified_document *frame, *control;
    bool sealed, committed;
};

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

typedef struct record_writer { uint8_t *data; size_t size; } record_writer;
static bool record_write(void *context, size_t offset, qa_bytes value, qa_error *e)
{
    record_writer *w = context;
    if (offset > w->size || value.size > w->size - offset)
        return application_fail(e, QA_ERROR_ARGUMENT, "Component ABI write leaves its actual record");
    memcpy(w->data + offset, value.data, value.size);
    return true;
}
static bool player_record(application_unified_json *j, qa_qvm_abi abi, const qa_q3_player *ps, qa_error *e)
{
    uint8_t data[468] = {0};
    record_writer w = {data, qa_qvm_player_bytes(abi)};
    qa_q3_abi_record r = {.abi = abi, .bytes = {data, w.size}, .context = &w, .write = record_write};
    return qa_q3_abi_write_player(&r, 0, true, false, ps, e) && bytes(j, (qa_bytes){data, w.size}, e);
}
static bool entity_record(application_unified_json *j, qa_qvm_abi abi, const qa_q3_entity *es, qa_error *e)
{
    uint8_t data[208] = {0};
    record_writer w = {data, qa_qvm_entity_bytes(abi)};
    qa_q3_abi_record r = {.abi = abi, .bytes = {data, w.size}, .context = &w, .write = record_write};
    return qa_q3_abi_write_entity(&r, 0, true, es, e) && bytes(j, (qa_bytes){data, w.size}, e);
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
        for (size_t a = 0; a < tokens.count; ++a)
            if ((a && !text(j, ",", e)) || !string(j, qa_q3_token(&tokens, a), e)) return false;
        if (!text(j, "]}", e)) return false;
        previous = row->sequence; first = false;
    }
    if (previous != through)
        return application_fail(e, QA_ERROR_FORMAT, "Component reliable command tail is not retained");
    return text(j, "]", e);
}

static bool frame_source(application_unified_json *j, const application_unified_component_capture *v,
    const component_cursor *row, const application_q3_scene_context *context, qa_error *e)
{
    const qa_q3_snapshot *snap = context->snapshot;
    if (!snap || !context->has_weapon_presented || context->actor_count > QA_Q3_ENTITIES ||
        snap->entity_count > 256 || snap->server_time < 0 || context->revision < 0 ||
        context->client_number < 0 || context->client_number >= QA_Q3_ENTITIES)
        return application_fail(e, QA_ERROR_FORMAT, "Component frame lacks its real receiver snapshot or weapon policy");
    bool ok = text(j, "{\"owner\":", e) && owner_write(j, v, row, e) &&
        text(j, ",\"generation\":", e) && application_unified_json_natural(j, row->generation, e) &&
        text(j, ",\"abi\":", e) && string(j, row->abi == QA_QVM_Q3_MODERN ? "q3-modern" : "q3-1.16n-base", e) &&
        text(j, ",\"viewer\":", e) && application_unified_json_actor(j, v->player.actor, e) &&
        text(j, ",\"clientNumber\":", e) && number(j, context->client_number, e) &&
        text(j, ",\"gameStateRevision\":", e) && number(j, (double)row->game_state_revision, e) &&
        text(j, ",\"snapshot\":{\"serverTime\":", e) && number(j, snap->server_time, e) &&
        text(j, ",\"playerState\":", e) && player_record(j, row->abi, &snap->player, e) &&
        text(j, context->weapon_presented ? "},\"weaponPresented\":true,\"bindings\":[" :
            "},\"weaponPresented\":false,\"bindings\":[", e);
    for (size_t i = 0; ok && i < context->actor_count; ++i) {
        const application_q3_scene_actor *binding = context->actors + i;
        ok = (!i || text(j, ",", e)) && text(j, "{\"slot\":", e) && number(j, binding->slot, e) &&
            text(j, ",\"actor\":", e) && application_unified_json_actor(j, binding->actor, e) &&
            text(j, binding->owned ? ",\"owned\":true}" : ",\"owned\":false}", e);
    }
    if (ok) ok = text(j, "],\"scene\":", e);
    if (ok && !row->scene) ok = text(j, "null", e);
    else if (ok) {
        ok = text(j, "{\"revision\":", e) && number(j, (double)context->revision, e) &&
            text(j, ",\"snapshot\":{\"serverTime\":", e) && number(j, snap->server_time, e) &&
            text(j, ",\"flags\":", e) && number(j, snap->flags, e) && text(j, ",\"areaMask\":", e) &&
            bytes(j, (qa_bytes){snap->area_mask, 32}, e) && text(j, ",\"playerState\":", e) &&
            player_record(j, row->abi, &snap->player, e) && text(j, ",\"entities\":[", e);
        for (size_t i = 0; ok && i < snap->entity_count; ++i)
            ok = (!i || text(j, ",", e)) && entity_record(j, row->abi, snap->entities + i, e);
        if (ok) ok = text(j, "],\"serverCommandSequence\":", e) && number(j, snap->server_command_number, e) && text(j, "}}", e);
    }
    return ok && text(j, "}", e);
}

static bool camera(const qa_unified_document *player, qa_vec3 *origin, qa_vec3 axis[3], qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(player);
    qa_json_id view = qa_json_get(j, qa_unified_document_root(player), "view");
    qa_vec3 angles;
    qa_vec3 *vectors[] = {origin, &angles};
    const char *const fields[] = {"origin", "angles"};
    const char *const components[] = {"x", "y", "z"};
    for (size_t v = 0; v < 2; ++v) {
        qa_json_id value = qa_json_get(j, view, fields[v]);
        double values[3];
        for (size_t c = 0; c < 3; ++c)
            if (!qa_json_number(j, qa_json_get(j, value, components[c]), values + c, e) || !isfinite(values[c])) return false;
        *vectors[v] = qa_v3((float)values[0], (float)values[1], (float)values[2]);
    }
    double height;
    if (!qa_json_number(j, qa_json_get(j, view, "viewHeight"), &height, e) || !isfinite(height)) return false;
    origin->z += (float)height;
    qa_builtin_angle_vectors(angles, axis, axis + 1, axis + 2);
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
    return true;
}

bool application_unified_components_prepare(application_unified_component_publisher *p,
    const application_unified_source *source, const qa_unified_session_player *player, uint32_t epoch,
    const qa_unified_document *values, application_unified_component_capture **out, qa_error *e)
{
    if (!p || !source || !player || !values || !epoch || !out || *out || p->pending || p->serial == UINT64_MAX ||
        !qa_actor_id_equal(player->actor, p->actor) || !application_unified_source_current(p->application, source) ||
        !application_unified_player_current(p->application, p->recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component capture requires its actual returned Source and recipient");
    application_unified_component_capture *v = calloc(1, sizeof(*v));
    if (!v) return application_fail(e, QA_ERROR_MEMORY, "Retaining component publication cursor candidate");
    v->owner = p; v->source = *source; v->player = *player; v->roster = p->application->components;
    v->epoch = epoch; v->serial = p->serial; v->revision = p->epoch == epoch ? p->revision : 0; p->pending = v;
    size_t capacity = application_q3_components_publication_count(v->roster);
    if (capacity > 256) { application_unified_components_dispose(v); return application_fail(e, QA_ERROR_FORMAT, "Component roster exceeds its wire owner extent"); }
    v->rows = capacity ? calloc(capacity, sizeof(*v->rows)) : NULL;
    v->leases = capacity ? calloc(capacity, sizeof(*v->leases)) : NULL;
    bool ok = !capacity || (v->rows && v->leases);
    if (!ok) application_fail(e, QA_ERROR_MEMORY, "Retaining actual component rows and Source leases");
    qa_vec3 origin, axis[3];
    if (ok && capacity) ok = camera(values, &origin, axis, e);
    application_unified_json frames = {0}, states = {0};
    if (ok) ok = text(&frames, "[", e) && text(&states, "[", e);
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
        if (ok) ok = (!at || text(&frames, ",", e)) && frame_source(&frames, v, row, &context, e) &&
            (!at || text(&states, ",", e)) && text(&states, "{\"owner\":", e) && owner_write(&states, v, row, e) &&
            text(&states, ",\"identity\":", e) && application_unified_json_document(&states, publication.identity, e) &&
            text(&states, ",\"generation\":", e) && application_unified_json_natural(&states, row->generation, e) &&
            text(&states, ",\"abi\":", e) && string(&states, row->abi == QA_QVM_Q3_MODERN ? "q3-modern" : "q3-1.16n-base", e) &&
            text(&states, ",\"runtime\":", e) && string(&states, publication.presentation_runtime, e) &&
            text(&states, ",\"gameStateRevision\":", e) && number(&states, (double)row->game_state_revision, e) &&
            text(&states, ",\"gameState\":", e) && (game_changed ? game_state(&states, context.game_state, e) : text(&states, "null", e)) &&
            text(&states, ",\"commandBase\":", e) && number(&states, base, e) && text(&states, ",\"commands\":", e) &&
            (scene ? commands(&states, &context, base, row->sequence, e) : text(&states, "[]", e)) && text(&states, "}", e);
    }
    changed |= v->count != p->count;
    for (size_t i = 0; i < v->count && !changed; ++i)
        changed = v->rows[i].owner != p->rows[i].owner || v->rows[i].generation != p->rows[i].generation;
    if (ok && changed) {
        if (v->revision == QA_UNIFIED_SAFE_INTEGER) ok = application_fail(e, QA_ERROR_FORMAT, "Component publication revision is exhausted");
        else ++v->revision;
    }
    if (ok) ok = text(&frames, "]", e) && text(&states, "]", e);
    application_unified_json document = {0};
    if (ok) ok = text(&document, "{\"revision\":", e) && application_unified_json_natural(&document, v->revision, e) &&
        text(&document, ",\"native\":[],\"sources\":", e) && application_unified_json_append(&document,
            (qa_bytes){frames.bytes.data, frames.bytes.size}, e) && text(&document, "}", e) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){document.bytes.data, document.bytes.size}, &v->frame, e);
    application_unified_json_dispose(&document);
    if (ok && changed) ok = text(&document, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"components\",\"epoch\":", e) &&
        number(&document, epoch, e) && text(&document, ",\"update\":{\"revision\":", e) &&
        application_unified_json_natural(&document, v->revision, e) && text(&document, ",\"native\":[],\"sources\":", e) &&
        application_unified_json_append(&document, (qa_bytes){states.bytes.data, states.bytes.size}, e) && text(&document, "}}}", e) &&
        qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT, (qa_bytes){document.bytes.data, document.bytes.size}, &v->control, e);
    application_unified_json_dispose(&document); application_unified_json_dispose(&frames); application_unified_json_dispose(&states);
    if (ok && !application_unified_components_current(v)) ok = application_fail(e, QA_ERROR_ARGUMENT, "Component Source retired while its output was assembled");
    if (!ok) { application_unified_components_dispose(v); return false; }
    *out = v; return true;
}

const qa_unified_document *application_unified_components_frame(const application_unified_component_capture *v)
{ return v ? v->frame : NULL; }
const qa_unified_document *application_unified_components_control(const application_unified_component_capture *v)
{ return v ? v->control : NULL; }
bool application_unified_components_seal(application_unified_component_capture *v, qa_error *e)
{
    if (!application_unified_components_current(v))
        return application_fail(e, QA_ERROR_ARGUMENT, "Component queue token lost its actual prepared publisher");
    if (!v->sealed) {
        for (size_t i = 0; i < v->count; ++i) application_q3_components_publication_return(v->leases + i);
        free(v->leases); v->leases = NULL; v->sealed = true;
    }
    return true;
}
void application_unified_components_commit(application_unified_component_capture *v)
{
    if (!v || !v->sealed || v->committed || v->owner->pending != v || v->owner->serial != v->serial) return;
    application_unified_component_publisher *p = v->owner;
    free(p->rows); p->rows = v->rows; v->rows = NULL; p->count = v->count;
    p->epoch = v->epoch; p->revision = v->revision; ++p->serial; p->pending = NULL; v->committed = true;
}
void application_unified_components_dispose(application_unified_component_capture *v)
{
    if (!v) return;
    if (v->leases) for (size_t i = 0; i < v->count; ++i) application_q3_components_publication_return(v->leases + i);
    if (v->owner->pending == v) v->owner->pending = NULL;
    free(v->rows); free(v->leases); qa_unified_document_destroy(v->frame); qa_unified_document_destroy(v->control); free(v);
}
