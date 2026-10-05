#include "unified_output.h"
#include "unified_output_json.h"
#include "unified_frame_private.h"
#include "unified_q3_sources.h"
#include "map_players_private.h"
#include "network_q1_source.h"
#include "guest_native_q2_private.h"
#include "qa/unified_frame_prediction.h"
#include "qa/unified_frame_player.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q2_wire.h"


static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool string(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_string(j, s, e); }

static bool current(qa_application *app, const application_unified_source *source,
    uint64_t revision, qa_error *error)
{
    return (application_unified_source_current(app, source) &&
        qa_actors_revision(qa_session_actors(source->session)) == revision) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified output changed its Source or full actor roster");
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

static bool provider(qa_unified_provider_state *out, const application_provider *p, qa_error *error)
{
    if (!p || !p->constructed || !p->attached || p->close_pending || !p->launch || !p->product)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified configuration lost its published provider");
    return application_unified_frame_string(NULL, &out->provider, p->launch->selection.instance, error) &&
        application_unified_frame_string(NULL, &out->content, p->product->identity, error);
}

static int configuration_compare(const void *left, const void *right)
{
    qa_actor_id a = ((const qa_unified_configuration_state *)left)->actor;
    qa_actor_id b = ((const qa_unified_configuration_state *)right)->actor;
    if (a.registry != b.registry) return a.registry < b.registry ? -1 : 1;
    if (a.slot != b.slot) return a.slot < b.slot ? -1 : 1;
    return a.generation == b.generation ? 0 : a.generation < b.generation ? -1 : 1;
}

static bool configurations(qa_application *app, qa_unified_frame_metadata *out, qa_error *error)
{
    size_t count = app->players->count;
    out->configurations = count ? calloc(count, sizeof(*out->configurations)) : NULL;
    if (count && !out->configurations) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual player configurations");
    for (size_t i = 0; i < count; ++i) {
        const application_player_record *row = app->players->records + i;
        if (row->retiring || row->deferred || row->source_begin_pending) continue;
        qa_unified_configuration_state *v = out->configurations + out->configuration_count++;
        v->actor = row->actor;
        v->weapons = calloc(1, sizeof(*v->weapons));
        if (!v->weapons) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual arsenal configuration");
        v->weapon_count = 1;
        if (!provider(&v->movement, application_provider_for(app, row->actor, QA_ROLE_MOVEMENT, ""), error) ||
            !provider(&v->character, application_provider_for(app, row->actor, QA_ROLE_CHARACTER, ""), error) ||
            !provider(&v->appearance, application_provider_for(app, row->actor, QA_ROLE_BODY, ""), error) ||
            !provider(v->weapons, application_provider_for(app, row->actor, QA_ROLE_ARSENAL, ""), error) ||
            !provider(&v->inventory, application_provider_for(app, row->actor, QA_ROLE_INVENTORY, ""), error)) return false;
    }
    if (out->configuration_count > 1)
        qsort(out->configurations, out->configuration_count, sizeof(*out->configurations), configuration_compare);
    return true;
}

static bool metadata_revision(qa_application *app, const application_unified_source *source,
    uint32_t epoch, application_unified_metadata_receipt *out, qa_error *error)
{
    *out = (application_unified_metadata_receipt){.epoch = epoch, .style_source = source->owner,
        .publication_revision = source->publication, .roster_revision = app->players ? app->players->revision : 0,
        .map_revision = source->map_revision};
    application_provider *p = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!p || p->owner != source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified metadata lost its actual primary Source");
    if (p->kind == APPLICATION_PROVIDER_Q1)
        return qa_q1_source_lightstyle_revision(p->state.q1, &out->style_revision, error);
    if (p->kind == APPLICATION_PROVIDER_Q2)
        return qa_q2_wire_lightstyle_revision(p->state.q2, &out->style_revision, error);
    if (p->kind == APPLICATION_PROVIDER_QC) {
        struct application_qc_state *engine = application_network_q1_qc_source(app, p->owner, error);
        if (!engine) return false;
        out->style_revision = engine->lightstyle_revision;
    } else if (p->kind == APPLICATION_PROVIDER_NATIVE && source->family == QA_GAME_Q2) {
        if (!p->state.native.q2_engine) return application_fail(error, QA_ERROR_ARGUMENT, "Unified metadata lost its original Q2 Source");
        out->style_revision = p->state.native.q2_engine->lightstyle_revision;
    }
    return true;
}

static bool styles(qa_application *app, const application_unified_source *source,
    qa_unified_frame_metadata *out, qa_error *error)
{
    application_provider *p = application_world_provider(app, QA_ROLE_ENTITIES, "");
    qa_strings *strings = qa_session_strings(source->session);
    size_t extent = source->family == QA_GAME_Q1 ? 64 : source->family == QA_GAME_Q2 ? 256 : 0;
    out->styles = extent ? calloc(extent, sizeof(*out->styles)) : NULL;
    if (extent && !out->styles) return application_fail(error, QA_ERROR_MEMORY, "Retaining changed Source lightstyle patterns");
    struct application_qc_state *qc = p->kind == APPLICATION_PROVIDER_QC ?
        application_network_q1_qc_source(app, p->owner, error) : NULL;
    if (p->kind == APPLICATION_PROVIDER_QC && !qc) return false;
    struct application_native_q2 *native = p->kind == APPLICATION_PROVIDER_NATIVE && source->family == QA_GAME_Q2 ?
        p->state.native.q2_engine : NULL;
    uint32_t base = native ? native->resource_base[QA_NATIVE_HOST_IMAGE] + native->resource_limit[QA_NATIVE_HOST_IMAGE] : 0;
    if (native && (!native->configstrings || base > native->configstring_count || native->configstring_count - base < 256))
        return application_fail(error, QA_ERROR_FORMAT, "Original Q2 lightstyles leave actual configstrings");
    for (size_t i = 0; i < extent; ++i) {
        const char *pattern = NULL;
        qa_string_id id = 0;
        if (p->kind == APPLICATION_PROVIDER_Q1) {
            if (!qa_q1_source_lightstyle_read(p->state.q1, (uint32_t)i, &id, error)) return false;
            if (id) pattern = qa_strings_cstr(strings, id);
        } else if (p->kind == APPLICATION_PROVIDER_Q2) {
            if (!qa_q2_wire_lightstyle_read(p->state.q2, (uint32_t)i, &id, error)) return false;
            if (id) pattern = qa_strings_cstr(strings, id);
        } else if (qc) pattern = qc->lightstyles[i];
        else if (native) pattern = native->configstrings[base + i];
        qa_unified_style_pattern *row = out->styles + out->style_count++;
        row->family = source->family; row->index = (uint32_t)i;
        if (!application_unified_frame_string(NULL, &row->pattern, pattern ? pattern : "", error)) return false;
    }
    return true;
}

bool application_unified_output_metadata(qa_application *app, const application_unified_source *source,
    const application_unified_q3_sources *q3_sources, uint32_t epoch, const application_unified_metadata_receipt *committed,
    const qa_unified_document *committed_q3_metadata, application_unified_metadata_receipt *proposed, qa_unified_document **out, qa_error *error)
{
    if (!app || !source || !epoch || !proposed || !out || *out || !application_unified_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified metadata requires its completed Source and recipient epoch");
    application_unified_metadata_receipt value;
    if (!metadata_revision(app, source, epoch, &value, error)) return false;
    bool initial = !committed || committed->epoch != epoch || committed->map_revision != value.map_revision;
    bool configuration_changed = initial || committed->publication_revision != value.publication_revision ||
        committed->roster_revision != value.roster_revision;
    bool styles_changed = initial || committed->style_source != value.style_source || committed->style_revision != value.style_revision;
    bool q3_changed = initial || !application_unified_q3_sources_metadata_current(q3_sources, committed_q3_metadata);
    if (!configuration_changed && !styles_changed && !q3_changed) { *proposed = value; return true; }
    qa_unified_frame_metadata *metadata = calloc(1, sizeof(*metadata));
    if (!metadata) return application_fail(error, QA_ERROR_MEMORY, "Retaining changed Unified Source metadata");
    metadata->epoch = epoch; metadata->frame = source->frame.number;
    metadata->configuration_revision = value.publication_revision; metadata->roster_revision = value.roster_revision;
    metadata->style_revision = value.style_revision;
    metadata->replace_configurations = configuration_changed; metadata->replace_styles = styles_changed;
    metadata->replace_q3 = q3_changed;
    bool ok = (!configuration_changed || configurations(app, metadata, error)) &&
        (!styles_changed || styles(app, source, metadata, error)) &&
        (!q3_changed || application_unified_q3_sources_metadata(q3_sources, metadata, error));
    application_unified_metadata_receipt after;
    if (ok) ok = metadata_revision(app, source, epoch, &after, error) &&
        after.publication_revision == value.publication_revision && after.roster_revision == value.roster_revision &&
        after.style_revision == value.style_revision && application_unified_q3_sources_current(q3_sources) &&
        application_unified_source_current(app, source);
    if (ok) ok = qa_unified_document_create_metadata(&metadata, out, error);
    if (!ok) { qa_unified_frame_metadata_destroy(metadata); return false; }
    *proposed = value;
    return true;
}

bool application_unified_output_world(qa_application *app, const application_unified_source *source,
    qa_unified_frame_pool *pool, qa_unified_world_frame **out, qa_error *error)
{
    if (!out || *out || !source || !application_unified_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified world requires its actual completed Source");
    qa_application_map_view map;
    if (!qa_application_map_read(app, &map) || map.revision != source->map_revision || !map.resource)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified output lost its published map resource");
    const qa_actor_registry *registry = qa_session_actors(source->session);
    uint64_t revision = qa_actors_revision(registry);
    size_t count = qa_actors_count(registry);
    qa_unified_world_frame *v = qa_unified_world_frame_create(pool, error);
    if (!v) return false;
    v->source = source->frame;
    v->actors = count ? application_unified_frame_alloc(v->lease, count, sizeof(*v->actors), error) : NULL;
    v->bodies = count ? application_unified_frame_alloc(v->lease, count, sizeof(*v->bodies), error) : NULL;
    v->collisions = count ? application_unified_frame_alloc(v->lease, count, sizeof(*v->collisions), error) : NULL;
    v->world = application_unified_frame_alloc(v->lease, 1, sizeof(*v->world), error);
    bool ok = (!count || (v->actors && v->bodies && v->collisions)) && v->world;
    if (!ok) application_fail(error, QA_ERROR_MEMORY, "Retaining actual Source world rows");
    uint32_t cursor = 0; const qa_actor_record *record;
    qa_strings *strings = qa_session_strings(source->session);
    while (ok && qa_actors_next(registry, &cursor, &record)) {
        if (qa_actors_revision(registry) != revision) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified world actor roster changed during observation");
            break;
        }
        qa_actor_id id = record->id;
        qa_unified_actor_state *a = v->actors + v->actor_count++;
        a->actor = id;
        ok = application_unified_frame_string(v->lease, &a->owner, qa_strings_cstr(strings, record->owner), error) &&
            application_unified_frame_string(v->lease, &a->definition, qa_strings_cstr(strings, record->definition), error);
        if (ok && qa_world_body_storage_serial(source->world, id)) {
            qa_unified_body_state *b = v->bodies + v->body_count++;
            b->actor = id; ok = qa_world_body_read(source->world, id, &b->body, error);
            if (ok && qa_actors_revision(registry) != revision)
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified world actor roster changed during observation");
            qa_spatial_actor row = {0};
            if (ok && qa_world_linked(source->world, id, &row.body)) {
                qa_error observed = {0};
                if (qa_world_get_collision(source->world, id, &row.collision, &observed)) {
                    row.body.state = b->body;
                    v->collisions[v->collision_count++] = row;
                } else if (observed.code != QA_OK) {
                    if (error) *error = observed;
                    ok = false;
                }
            }
            if (ok && qa_actors_revision(registry) != revision)
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified world actor roster changed during observation");
        }
    }
    const qa_product *product = qa_catalog_product(qa_launch_snapshot_catalog(source->launch), map.geometry);
    if (ok && (!product || !qa_resource_digest(map.resource)))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified world lost its installed resource provenance");
    if (ok) {
        v->world->digest = *qa_resource_digest(map.resource); v->world->byte_length = qa_resource_bytes(map.resource).size;
        ok = application_unified_frame_string(v->lease, &v->world->content, product->identity, error) &&
            application_unified_frame_string(v->lease, &v->world->path, qa_resource_path(map.resource), error) &&
            current(app, source, revision, error);
    }
    if (!ok) { qa_unified_world_frame_destroy(v); return false; }
    *out = v;
    return true;
}

bool application_unified_output_inventory(qa_application *app, qa_actor_id actor,
    qa_unified_frame *out, const qa_inventory_entry **raw, size_t *raw_count, qa_error *error)
{
    size_t count = 0, actual = 0;
    if (!qa_inventory_entries(app->inventory, actor, NULL, 0, &count, error)) return false;
    qa_inventory_entry *entries = count ? application_unified_frame_alloc(out->lease, count, sizeof(*entries), error) : NULL;
    if (count && !entries) return application_fail(error, QA_ERROR_MEMORY, "Reading actual Unified recipient inventory");
    bool ok = qa_inventory_entries(app->inventory, actor, entries, count, &actual, error);
    if (ok && actual != count) ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified recipient inventory changed between reads");
    if (ok) {
        out->inventories = application_unified_frame_alloc(out->lease, 1, sizeof(*out->inventories), error);
        if (!out->inventories) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual Unified recipient inventory");
    }
    if (ok) {
        out->inventory_count = 1; out->inventories->actor = actor;
        out->inventories->entries = count ? application_unified_frame_alloc(out->lease, count, sizeof(*out->inventories->entries), error) : NULL;
        if (count && !out->inventories->entries) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining actual inventory rows");
    }
    for (size_t i = 0; ok && i < count; ++i) {
        qa_unified_inventory_entry *row = out->inventories->entries + out->inventories->entry_count++;
        row->count = entries[i].count; row->capacity = entries[i].capacity; row->policy = entries[i].policy;
        ok = application_unified_frame_string(out->lease, &row->item,
            qa_strings_cstr(qa_session_strings(app->session), entries[i].item), error);
    }
    if (ok) { *raw = entries; *raw_count = count; }
    return ok;
}

static bool children_current(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player,
    const application_unified_frame_children *children, qa_error *error)
{
    return (application_unified_source_current(app, source) &&
        application_unified_player_current(app, recipient, player) &&
        children->current(children->context, app, source, recipient, player)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Unified frame lost an actual Source projection owner");
}

bool application_unified_output_build(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player,
    qa_unified_frame **owned, const application_unified_frame_children *children,
    application_unified_output *out, qa_error *error)
{
    if (!out || out->frame || out->controls || out->control_count || !owned || !*owned ||
        !source || !player || !children || !children->current ||
        (children->control_count && !children->controls))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified frame requires its real typed Source children");
    qa_unified_frame *frame = *owned;
    if (!frame->world || !frame->prediction || !frame->player || !frame->visuals || !frame->epoch ||
        !qa_actor_id_equal(frame->prediction->actor, player->actor) ||
        !qa_actor_id_equal(frame->player->actor, player->actor) || frame->prediction->sequence != frame->acknowledged_input)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified frame differs from its actual player acknowledgement");
    if (!children_current(app, source, recipient, player, children, error)) return false;
    application_unified_output candidate = {.controls_pooled = frame->lease != NULL};
    bool ok = true;
    if (ok && children->control_count) {
        candidate.controls = application_unified_frame_alloc(frame->lease, children->control_count, sizeof(*candidate.controls), error);
        if (!candidate.controls) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining Unified reliable prerequisites");
    }
    for (size_t i = 0; ok && i < children->control_count; ++i) {
        const qa_unified_document *control = children->controls[i];
        if (!control || qa_unified_document_type(control) != QA_UNIFIED_CONTROL_DOCUMENT) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified prerequisite is not its actual reliable control"); break;
        }
        ok = qa_unified_document_retain(control, candidate.controls + candidate.control_count, error);
        if (ok) ++candidate.control_count;
    }
    if (ok) ok = children_current(app, source, recipient, player, children, error) &&
        qa_unified_document_create_frame(owned, &candidate.frame, error);
    if (!ok) { application_unified_output_dispose(&candidate); return false; }
    *out = candidate;
    return true;
}

void application_unified_output_dispose(application_unified_output *out)
{
    if (!out) return;
    for (size_t i = 0; i < out->control_count; ++i) qa_unified_document_destroy(out->controls[i]);
    if (!out->controls_pooled) free(out->controls);
    qa_unified_document_destroy(out->frame);
    *out = (application_unified_output){0};
}

bool application_unified_resource_key(const qa_product *product, const char *path,
    const qa_resource *r, qa_unified_document **out, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *error)
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
        char actual_id[QA_APPLICATION_RESOURCE_KEY_CAPACITY] = "resource:unified:";
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
    const qa_unified_resource_declaration *resources, size_t count, qa_unified_document **out, qa_error *error)
{
    if (!out || !epoch || (count && !resources) || count>32768)
        return application_fail(error,QA_ERROR_ARGUMENT,"Unified resource control has an invalid actual dictionary extent");
    qa_unified_control value={.kind=QA_UNIFIED_CONTROL_RESOURCES,.epoch=epoch,
        .value.resources={.values=(qa_unified_resource_declaration *)resources,.count=count}};
    return qa_unified_document_create_control(&value,out,error);
}
