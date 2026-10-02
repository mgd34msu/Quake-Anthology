#include "unified_output.h"
#include "unified_output_json.h"
#include "map_players_private.h"
#include "network_q1_source.h"
#include "guest_native_q2_private.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q2_wire.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool string(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_string(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool actor(application_unified_json *j, qa_actor_id a, qa_error *e)
{ return application_unified_json_actor(j, a, e); }

static bool current(qa_application *app, const application_unified_source *source,
    uint64_t revision, qa_error *error)
{
    return (application_unified_source_current(app, source) &&
        qa_actors_revision(qa_session_actors(source->session)) == revision) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified output changed its Source or full actor roster");
}

static bool provider(application_unified_json *j, const application_provider *p, qa_error *error)
{
    if (!p || !p->constructed || !p->attached || p->close_pending || !p->launch ||
        !p->launch->selection.instance || !p->product || !p->product->identity)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified configuration lost its published provider");
    return text(j, "{\"provider\":", error) && string(j, p->launch->selection.instance, error) &&
        text(j, ",\"content\":", error) && string(j, p->product->identity, error) && text(j, "}", error);
}

static bool time_value(application_unified_json *j, qa_clock_kind kind, uint64_t ns, qa_error *error)
{
    bool ms = kind == QA_CLOCK_Q2_RERELEASE || kind == QA_CLOCK_Q3;
    return text(j, ms ? "{\"kind\":\"milliseconds\",\"value\":" : "{\"kind\":\"seconds\",\"value\":", error) &&
        number(j, (double)ns / (ms ? 1e6 : 1e9), error) && text(j, "}", error);
}

static bool frame(application_unified_json *j, const qa_source_frame *f, qa_error *error)
{
    static const char *const phases[] = {"frame-entry", "client-command", "entity-prethink",
        "entity-physics", "entity-think", "client-end-frame", "frame-exit"};
    if ((unsigned)f->phase >= sizeof(phases) / sizeof(phases[0]))
        return application_fail(error, QA_ERROR_FORMAT, "Unified Source has an unknown frame phase");
    return text(j, "{\"frame\":", error) && application_unified_json_natural(j, f->number, error) &&
        text(j, ",\"time\":", error) && time_value(j, f->kind, f->time_ns, error) &&
        text(j, ",\"elapsed\":", error) && time_value(j, f->kind, f->elapsed_ns, error) &&
        text(j, ",\"phase\":", error) && string(j, phases[f->phase], error) && text(j, "}", error);
}

static bool resource(application_unified_json *j, const qa_product *product, const char *path,
    const qa_resource *r, qa_error *error)
{
    if (!product || !product->identity || !path || !r || !qa_resource_digest(r))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified resource lost its immutable Source provenance");
    char digest[72] = "sha256:";
    qa_sha256_hex(qa_resource_digest(r), digest + 7);
    return text(j, "{\"content\":", error) && string(j, product->identity, error) &&
        text(j, ",\"path\":", error) && string(j, path, error) &&
        text(j, ",\"digest\":", error) && string(j, digest, error) &&
        text(j, ",\"byteLength\":", error) &&
        application_unified_json_natural(j, qa_resource_bytes(r).size, error) && text(j, "}", error);
}

static bool actor_rows(qa_application *app, application_unified_json *j, qa_error *error)
{
    const qa_actor_registry *registry = qa_session_actors(app->session);
    qa_strings *strings = qa_session_strings(app->session);
    uint32_t cursor = 0;
    const qa_actor_record *row;
    bool first = true;
    while (qa_actors_next(registry, &cursor, &row)) {
        qa_actor_record observed = *row;
        if ((!first && !text(j, ",", error)) || !text(j, "{\"id\":", error) ||
            !actor(j, observed.id, error) || !text(j, ",\"owner\":", error) ||
            !string(j, qa_strings_cstr(strings, observed.owner), error) ||
            !text(j, ",\"definition\":", error) ||
            !string(j, qa_strings_cstr(strings, observed.definition), error) || !text(j, "}", error)) return false;
        first = false;
    }
    return true;
}

static bool body_rows(qa_application *app, const application_unified_source *source,
    uint64_t revision, application_unified_json *j, qa_error *error)
{
    uint32_t cursor = 0;
    const qa_actor_record *row;
    bool first = true;
    while (qa_actors_next(qa_session_actors(source->session), &cursor, &row)) {
        qa_actor_id id = row->id;
        /* This is the real body's storage membership, independently of its
         * collision link or currently selected appearance. */
        if (!qa_world_body_storage_serial(source->world, id)) continue;
        qa_body_state body;
        if (!qa_world_body_read(source->world, id, &body, error) ||
            !current(app, source, revision, error)) return false;
        if (body.ground.registry && body.ground.registry != qa_actors_identity(qa_session_actors(source->session)))
            return application_fail(error, QA_ERROR_FORMAT, "Unified body ground belongs to another registry");
        if ((!first && !text(j, ",", error)) || !text(j, "{\"actor\":", error) ||
            !actor(j, id, error) || !text(j, ",\"body\":", error) ||
            !application_unified_json_body(j, &body, error) || !text(j, "}", error)) return false;
        first = false;
    }
    return true;
}

static bool inventory_entry(qa_application *app, application_unified_json *j,
    const qa_inventory_entry *entry, qa_error *error)
{
    const char *policy = entry->policy == QA_COUNT_STACK ? "{\"kind\":\"stack\"}" :
        entry->policy == QA_COUNT_SOURCE_FLOAT ? "{\"kind\":\"source-counter\",\"arithmetic\":\"binary32\"}" :
        entry->policy == QA_COUNT_SOURCE_INT32 ? "{\"kind\":\"source-counter\",\"arithmetic\":\"int32\"}" : NULL;
    if (!policy) return application_fail(error, QA_ERROR_FORMAT, "Unified inventory has an unknown arithmetic owner");
    return text(j, "{\"item\":", error) &&
        string(j, qa_strings_cstr(qa_session_strings(app->session), entry->item), error) &&
        text(j, ",\"count\":", error) && number(j, entry->count, error) &&
        text(j, ",\"capacity\":", error) && number(j, entry->capacity, error) &&
        text(j, ",\"countPolicy\":", error) && text(j, policy, error) && text(j, "}", error);
}

static bool inventory_rows(qa_application *app, const application_unified_source *source,
    uint64_t revision, qa_actor_id recipient, application_unified_json *j, qa_error *error)
{
    size_t count = 0, actual = 0;
    if (!qa_inventory_entries(app->inventory, recipient, NULL, 0, &count, error)) return false;
    if (count > SIZE_MAX / sizeof(qa_inventory_entry))
        return application_fail(error, QA_ERROR_MEMORY, "Unified recipient inventory extent is too large");
    qa_inventory_entry *entries = count ? calloc(count, sizeof(*entries)) : NULL;
    if (count && !entries) return application_fail(error, QA_ERROR_MEMORY, "Allocating actual Unified recipient inventory");
    bool ok = qa_inventory_entries(app->inventory, recipient, entries, count, &actual, error);
    if (ok && actual != count)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified recipient inventory changed between reads");
    ok = ok && current(app, source, revision, error) && text(j, "{\"actor\":", error) &&
        actor(j, recipient, error) && text(j, ",\"entries\":[", error);
    for (size_t i = 0; ok && i < actual; ++i)
        ok = (!i || text(j, ",", error)) && inventory_entry(app, j, entries + i, error);
    if (ok) ok = text(j, "]}", error);
    free(entries);
    return ok;
}

static bool configurations(qa_application *app, application_unified_json *j, qa_error *error)
{
    bool first = true;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = app->players->records + i;
        if (row->retiring || row->deferred || row->source_begin_pending) continue;
        if (!qa_actors_get(qa_session_actors(app->session), row->actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified configuration lost its admitted player");
        application_provider *movement = application_provider_for(app, row->actor, QA_ROLE_MOVEMENT, "");
        application_provider *character = application_provider_for(app, row->actor, QA_ROLE_CHARACTER, "");
        application_provider *body = application_provider_for(app, row->actor, QA_ROLE_BODY, "");
        application_provider *arsenal = application_provider_for(app, row->actor, QA_ROLE_ARSENAL, "");
        application_provider *inventory = application_provider_for(app, row->actor, QA_ROLE_INVENTORY, "");
        if ((!first && !text(j, ",", error)) || !text(j, "{\"actor\":", error) ||
            !actor(j, row->actor, error) || !text(j, ",\"movement\":", error) || !provider(j, movement, error) ||
            !text(j, ",\"character\":{\"definition\":", error) || !provider(j, character, error) ||
            !text(j, ",\"appearance\":", error) || !provider(j, body, error) ||
            !text(j, "},\"weapons\":[", error) || !provider(j, arsenal, error) ||
            !text(j, "],\"inventory\":", error) || !provider(j, inventory, error) || !text(j, "}", error)) return false;
        first = false;
    }
    return true;
}

static bool style(application_unified_json *j, unsigned index, const char *pattern,
    bool q1, double seconds, bool *first, qa_error *error)
{
    if (!pattern) return true;
    size_t length = strlen(pattern);
    double letter = length ? (unsigned char)pattern[(size_t)fmod(floor(seconds * 10), (double)length)] - 97.0 : 12;
    if ((!*first && !text(j, ",", error)) || !text(j, q1 ? "{\"kind\":\"q1\",\"style\":" :
        "{\"kind\":\"q2\",\"style\":", error) || !number(j, index, error)) return false;
    bool ok;
    if (q1) ok = text(j, ",\"value\":", error) && number(j, length ? letter * 22 : 256, error);
    else {
        double value = letter / 12;
        ok = text(j, ",\"rgb\":{\"x\":", error) && number(j, value, error) &&
            text(j, ",\"y\":", error) && number(j, value, error) &&
            text(j, ",\"z\":", error) && number(j, value, error) &&
            text(j, "},\"white\":", error) && number(j, value * 3, error);
    }
    if (ok) ok = text(j, "}", error);
    *first = false;
    return ok;
}

static bool styles(qa_application *app, const application_unified_source *source,
    application_unified_json *j, qa_error *error)
{
    application_provider *p = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_strings *strings = qa_session_strings(source->session);
    double seconds = (double)source->frame.time_ns / 1e9;
    bool first = true, ok = true;
    if (p->kind == APPLICATION_PROVIDER_Q1) {
        for (unsigned i = 0; ok && i < 64; ++i) {
            qa_string_id pattern;
            ok = qa_q1_source_lightstyle_read(p->state.q1, i, &pattern, error);
            if (ok && pattern) ok = style(j, i, qa_strings_cstr(strings, pattern), true, seconds, &first, error);
        }
    } else if (p->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = application_network_q1_qc_source(app, p->owner, error);
        if (!engine) return false;
        for (unsigned i = 0; ok && i < 64; ++i) ok = style(j, i, engine->lightstyles[i], true, seconds, &first, error);
    } else if (p->kind == APPLICATION_PROVIDER_Q2) {
        for (unsigned i = 0; ok && i < 256; ++i) {
            qa_string_id pattern;
            ok = qa_q2_wire_lightstyle_read(p->state.q2, i, &pattern, error);
            if (ok && pattern) ok = style(j, i, qa_strings_cstr(strings, pattern), false, seconds, &first, error);
        }
    } else if (p->kind == APPLICATION_PROVIDER_NATIVE && source->family == QA_GAME_Q2) {
        qa_application_native_q2_presentation cut;
        bool found;
        if (!qa_application_native_q2_presentation_selected(app, &cut, &found, error)) return false;
        struct application_native_q2 *engine = p->state.native.q2_engine;
        if (!found || cut.kind != QA_APPLICATION_NATIVE_Q2_ORIGINAL || !engine ||
            engine->provider != p || engine->calls || !engine->configstrings)
            return application_fail(error, QA_ERROR_ARGUMENT, "Unified lightstyles lost their original Q2 Source owner");
        uint64_t revision = engine->config_revision;
        uint32_t base = engine->resource_base[2] + engine->resource_limit[2];
        if (base > engine->configstring_count || engine->configstring_count - base < 256)
            return application_fail(error, QA_ERROR_FORMAT, "Original Q2 lightstyle table exceeds its real configstrings");
        for (unsigned i = 0; ok && i < 256; ++i)
            if (engine->configstrings[base + i])
                ok = style(j, i, engine->configstrings[base + i], false, seconds, &first, error);
        if (ok && (revision != engine->config_revision ||
            !qa_application_native_q2_presentation_current(app, &cut)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 lightstyles changed their actual Source receipt");
    }
    return ok;
}

bool application_unified_output_snapshot(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    application_unified_snapshot *out, qa_error *error)
{
    if (!out || out->snapshot || !epoch || !player || !source ||
        !application_unified_source_current(app, source) || player->source_owner != source->owner ||
        !qa_actors_get(qa_session_actors(source->session), player->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified output requires its actual admitted Source player and frame");
    if (!application_unified_player_current(app, recipient, player))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified output differs from its authentic connection player");
    qa_application_map_view map;
    if (!qa_application_map_read(app, &map) || map.revision != source->map_revision || !map.resource)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified output lost its published map resource");
    uint64_t revision = qa_actors_revision(qa_session_actors(source->session));
    application_unified_snapshot candidate = {0};
    application_unified_json j = {0};
    bool ok = text(&j, "{\"frame\":", error) && frame(&j, &source->frame, error) &&
        text(&j, ",\"actors\":[", error) && actor_rows(app, &j, error) &&
        text(&j, "],\"bodies\":[", error) && body_rows(app, source, revision, &j, error) &&
        text(&j, "],\"inventories\":[", error) && inventory_rows(app, source, revision, player->actor, &j, error) &&
        text(&j, "],\"configurations\":[", error) && configurations(app, &j, error) &&
        text(&j, "],\"scene\":{\"time\":", error) && time_value(&j, source->frame.kind, source->frame.time_ns, error) &&
        text(&j, ",\"world\":", error) && resource(&j, qa_catalog_product(qa_launch_snapshot_catalog(source->launch), map.geometry),
            qa_resource_path(map.resource), map.resource, error) &&
        text(&j, ",\"entities\":[],\"lights\":[],\"particles\":[],\"lightStyles\":[", error) && styles(app, source, &j, error) &&
        text(&j, "],\"areaBits\":null}}", error) &&
        current(app, source, revision, error) &&
        (application_unified_player_current(app, recipient, player) ||
            application_fail(error, QA_ERROR_ARGUMENT, "Unified recipient changed during Source output")) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){j.bytes.data, j.bytes.size}, &candidate.snapshot, error);
    application_unified_json_dispose(&j);
    if (!ok) { application_unified_snapshot_dispose(&candidate); return false; }
    *out = candidate;
    return true;
}

void application_unified_snapshot_dispose(application_unified_snapshot *out)
{
    if (!out) return;
    qa_unified_document_destroy(out->snapshot);
    *out = (application_unified_snapshot){0};
}

static bool child_field(application_unified_json *out, const qa_unified_document *document,
    qa_json_id parent, const char *name, qa_json_kind kind, qa_error *error)
{
    const qa_json_document *json = qa_unified_document_json(document);
    qa_json_id value = qa_json_get(json, parent, name);
    if (qa_json_type(json, value) != kind)
        return application_fail(error, QA_ERROR_FORMAT, "Unified presentation lacks its actual typed projection");
    return application_unified_json_append(out, qa_json_source(json, value), error);
}

static bool children_current(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player,
    const application_unified_frame_children *children, qa_error *error)
{
    return (application_unified_source_current(app, source) &&
        application_unified_player_current(app, recipient, player) &&
        children->current(children->context, app, source, recipient, player) &&
        application_unified_source_current(app, source) &&
        application_unified_player_current(app, recipient, player)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified frame lost an actual Source projection owner");
}

bool application_unified_output_build(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player, uint32_t epoch,
    int64_t acknowledged, const application_unified_frame_children *children,
    application_unified_output *out, qa_error *error)
{
    if (!out || out->frame || out->controls || out->control_count || !source || !player ||
        !children || !children->current || !children->prediction || !children->presentation ||
        !children->simulation_events || (children->control_count && !children->controls) ||
        children->control_count > SIZE_MAX / sizeof(*out->controls) ||
        qa_unified_document_type(children->prediction) != QA_UNIFIED_PREDICTION_DOCUMENT ||
        qa_unified_document_type(children->presentation) != QA_UNIFIED_CHECKPOINT ||
        qa_unified_document_type(children->simulation_events) != QA_UNIFIED_CHECKPOINT ||
        acknowledged < -1 || acknowledged > (int64_t)QA_UNIFIED_SAFE_INTEGER || !epoch)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified frame requires its real typed Source children");
    if (!children_current(app, source, recipient, player, children, error)) return false;
    const qa_json_document *prediction = qa_unified_document_json(children->prediction);
    qa_json_id root = qa_unified_document_root(children->prediction);
    uint64_t slot, generation;
    int64_t sequence;
    qa_json_id predicted_actor = qa_json_get(prediction, root, "actor");
    if (!qa_json_u64(prediction, qa_json_get(prediction, predicted_actor, "slot"), &slot, error) ||
        !qa_json_u64(prediction, qa_json_get(prediction, predicted_actor, "generation"), &generation, error) ||
        !qa_json_i64(prediction, qa_json_get(prediction, root, "sequence"), &sequence, error)) return false;
    if (slot != player->actor.slot || generation != player->actor.generation || sequence != acknowledged)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified prediction differs from its full recipient and input acknowledgement");
    if (qa_json_type(qa_unified_document_json(children->simulation_events),
        qa_unified_document_root(children->simulation_events)) != QA_JSON_ARRAY)
        return application_fail(error, QA_ERROR_FORMAT, "Unified Source events are not their actual output array");

    application_unified_snapshot snapshot = {0};
    if (!application_unified_output_snapshot(app, source, recipient, player, epoch, &snapshot, error)) return false;
    application_unified_output candidate = {0};
    application_unified_json j = {0};
    qa_buffer encoded_prediction = {0}, tagged_prediction = {0};
    const qa_json_document *presentation = qa_unified_document_json(children->presentation);
    qa_json_id p = qa_unified_document_root(children->presentation);
    qa_json_id presented_player = qa_json_get(presentation, p, "player");
    bool ok = qa_unified_document_encode(children->prediction, &encoded_prediction, error) &&
        qa_unified_checkpoint_bytes((qa_bytes){encoded_prediction.data, encoded_prediction.size}, &tagged_prediction, error) &&
        text(&j, "{\"schema\":\"qts-unified-frame\",\"version\":9,\"epoch\":", error) &&
        application_unified_json_natural(&j, epoch, error) && text(&j, ",\"acknowledgedInput\":", error) &&
        number(&j, (double)acknowledged, error) && text(&j, ",\"prediction\":", error) &&
        application_unified_json_append(&j, (qa_bytes){tagged_prediction.data, tagged_prediction.size}, error) &&
        text(&j, ",\"output\":{\"snapshot\":", error) && application_unified_json_document(&j, snapshot.snapshot, error) &&
        text(&j, ",\"events\":", error) && application_unified_json_document(&j, children->simulation_events, error) &&
        text(&j, "},\"models\":", error) && child_field(&j, children->presentation, p, "models", QA_JSON_ARRAY, error) &&
        text(&j, ",\"characters\":", error) && child_field(&j, children->presentation, p, "characters", QA_JSON_ARRAY, error) &&
        text(&j, ",\"worldText\":", error) && child_field(&j, children->presentation, p, "worldText", QA_JSON_ARRAY, error) &&
        text(&j, ",\"player\":{\"actor\":", error) && actor(&j, player->actor, error) &&
        text(&j, ",\"view\":", error) && child_field(&j, children->presentation, presented_player, "view", QA_JSON_OBJECT, error) &&
        text(&j, ",\"ui\":", error) && child_field(&j, children->presentation, presented_player, "ui", QA_JSON_OBJECT, error) &&
        text(&j, "}", error);
    static const char *const optional[] = {"nativeCamera", "components"};
    for (size_t i = 0; ok && i < sizeof(optional) / sizeof(optional[0]); ++i) {
        qa_json_id field = qa_json_get(presentation, p, optional[i]);
        if (field != QA_JSON_NONE)
            ok = text(&j, ",", error) && string(&j, optional[i], error) && text(&j, ":", error) &&
                child_field(&j, children->presentation, p, optional[i], QA_JSON_OBJECT, error);
    }
    if (ok && qa_json_get(presentation, p, "compiledQ3Sources") != QA_JSON_NONE)
        ok = text(&j, ",\"compiledQ3Sources\":", error) &&
            child_field(&j, children->presentation, p, "compiledQ3Sources", QA_JSON_ARRAY, error);
    if (ok) ok = text(&j, "}", error) && children_current(app, source, recipient, player, children, error) &&
        qa_unified_document_create(QA_UNIFIED_FRAME_DOCUMENT, (qa_bytes){j.bytes.data, j.bytes.size}, &candidate.frame, error);
    if (ok && children->control_count) {
        candidate.controls = calloc(children->control_count, sizeof(*candidate.controls));
        if (!candidate.controls) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating actual Unified reliable prerequisites");
    }
    for (size_t i = 0; ok && i < children->control_count; ++i) {
        const qa_unified_document *control = children->controls[i];
        if (!control || qa_unified_document_type(control) != QA_UNIFIED_CONTROL_DOCUMENT) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified prerequisite is not its actual reliable control");
            break;
        }
        const qa_json_document *json = qa_unified_document_json(control);
        uint64_t control_epoch;
        qa_json_id value = qa_json_get(json, qa_unified_document_root(control), "value");
        if (!qa_json_u64(json, qa_json_get(json, value, "epoch"), &control_epoch, error)) { ok = false; break; }
        if (control_epoch != epoch) { ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified prerequisite belongs to another epoch"); break; }
        qa_bytes bytes = qa_json_source(json, qa_unified_document_root(control));
        ok = qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT, bytes,
            candidate.controls + candidate.control_count, error);
        if (ok) ++candidate.control_count;
    }
    if (ok) ok = children_current(app, source, recipient, player, children, error);
    qa_buffer_free(&encoded_prediction);
    qa_buffer_free(&tagged_prediction);
    application_unified_json_dispose(&j);
    application_unified_snapshot_dispose(&snapshot);
    if (!ok) { application_unified_output_dispose(&candidate); return false; }
    *out = candidate;
    return true;
}

void application_unified_output_dispose(application_unified_output *out)
{
    if (!out) return;
    qa_unified_document_destroy(out->frame);
    for (size_t i = 0; i < out->control_count; ++i) qa_unified_document_destroy(out->controls[i]);
    free(out->controls);
    *out = (application_unified_output){0};
}

bool application_unified_resource_key(const qa_product *product, const char *path,
    const qa_resource *r, qa_unified_document **out, char id[81], qa_error *error)
{
    if (!out || !id || !product || !product->identity || !path || !r || !qa_resource_digest(r))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified resource needs its actual retained acquisition");
    application_unified_json key = {0}, tuple = {0};
    qa_buffer canonical = {0};
    char digest[72] = "sha256:";
    qa_sha256_hex(qa_resource_digest(r), digest + 7);
    bool ok = resource(&key, product, path, r, error) && text(&tuple, "[", error) &&
        string(&tuple, product->identity, error) && text(&tuple, ",", error) && string(&tuple, path, error) &&
        text(&tuple, ",", error) && string(&tuple, digest, error) && text(&tuple, ",", error) &&
        application_unified_json_natural(&tuple, qa_resource_bytes(r).size, error) && text(&tuple, "]", error);
    qa_unified_document *candidate = NULL;
    if (ok) ok = qa_unified_value_canonical((qa_bytes){tuple.bytes.data, tuple.bytes.size}, &canonical, error) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){key.bytes.data, key.bytes.size}, &candidate, error);
    if (ok) {
        qa_sha256_digest hash;
        char actual_id[81] = "resource:unified:";
        qa_sha256((qa_bytes){canonical.data, canonical.size}, &hash);
        qa_sha256_hex(&hash, actual_id + sizeof("resource:unified:") - 1);
        memcpy(id, actual_id, sizeof(actual_id));
        *out = candidate;
    }
    application_unified_json_dispose(&key);
    application_unified_json_dispose(&tuple);
    qa_buffer_free(&canonical);
    return ok;
}

bool application_unified_resource_control(uint32_t epoch,
    const qa_unified_document *const *keys, size_t count, qa_unified_document **out, qa_error *error)
{
    if (!out || !epoch || (count && !keys) || count > 32768)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified resource control has an invalid actual dictionary extent");
    application_unified_json j = {0};
    bool ok = text(&j, "{\"schema\":\"qts-control\",\"version\":1,\"value\":{\"kind\":\"resources\",\"epoch\":", error) &&
        application_unified_json_natural(&j, epoch, error) && text(&j, ",\"resources\":[", error);
    for (size_t i = 0; ok && i < count; ++i) {
        if (!keys[i] || qa_unified_document_type(keys[i]) != QA_UNIFIED_CHECKPOINT)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified dictionary member lacks its actual key projection");
        else ok = (!i || text(&j, ",", error)) && application_unified_json_document(&j, keys[i], error);
    }
    if (ok) ok = text(&j, "]}}", error) && qa_unified_document_create(QA_UNIFIED_CONTROL_DOCUMENT,
        (qa_bytes){j.bytes.data, j.bytes.size}, out, error);
    application_unified_json_dispose(&j);
    return ok;
}
