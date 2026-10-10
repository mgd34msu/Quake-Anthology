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
#include "qa/application_network.h"
#include "qa/application_selected_effects.h"
#include "qa/game_q1_wire.h"
#include "qa/game_q2_wire.h"
#include "qa/game_q2_items.h"
#include <stdio.h>


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
    if (!product || !product->identity || !path || !r)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified resource lost its immutable Source provenance");
    return text(j, "{\"content\":", error) && string(j, product->identity, error) &&
        text(j, ",\"path\":", error) && string(j, path, error) &&
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

static const qa_unified_configuration_state *player_configuration(const qa_unified_frame_metadata *metadata,
    qa_actor_id actor)
{
    for (size_t i = 0; metadata && i < metadata->configuration_count; ++i)
        if (qa_actor_id_equal(metadata->configurations[i].actor, actor)) return metadata->configurations + i;
    return NULL;
}

static bool q2_configuration_same(const qa_unified_q2_hud_configuration *old,
    const qa_application_native_q2_hud_source *hud, bool found)
{
    return !found ? !old : old && old->data_provider == hud->data_provider &&
        old->revision == hud->config_revision && old->deathmatch == hud->deathmatch &&
        old->cooperative == hud->cooperative && old->protocol.kind ==
            (hud->edition == QA_Q2_CLASSIC ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023);
}

static int image_compare(const void *left, const void *right)
{ return strcmp(*(const char *const *)left, *(const char *const *)right); }
static int configstring_compare(const void *left, const void *right)
{
    uint32_t a = ((const qa_unified_q2_configstring *)left)->index;
    uint32_t b = ((const qa_unified_q2_configstring *)right)->index;
    return a < b ? -1 : a > b;
}
static bool q2_configstring(qa_unified_q2_hud_configuration *out, uint32_t index,
    const char *value, qa_error *error)
{
    qa_unified_q2_configstring *row = out->configstrings + out->configstring_count++;
    row->index = index;
    return application_unified_frame_string(NULL, &row->value, value, error);
}
static bool q2_configuration(qa_application *app, const application_unified_source *source,
    qa_unified_configuration_state *row, qa_error *error)
{
    qa_application_native_q2_hud_source hud; bool found;
    if (!qa_application_native_q2_hud_source_read(app, row->actor, &hud, &found, error)) return false;
    if (!found) return true;
    qa_unified_q2_hud_configuration *out = calloc(1, sizeof(*out));
    if (!out) return application_fail(error, QA_ERROR_MEMORY, "Retaining chosen Q2 HUD configuration");
    row->q2_hud = out;
    out->data_provider = hud.data_provider; out->revision = hud.config_revision;
    out->deathmatch = hud.deathmatch; out->cooperative = hud.cooperative;
    out->protocol = (qa_net_protocol_id){.kind = hud.edition == QA_Q2_CLASSIC ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023};
    if (hud.original) {
        out->configstrings = calloc(hud.configstring_count, sizeof(*out->configstrings));
        if (hud.configstring_count && !out->configstrings)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining changed Original Q2 configstrings");
        for (uint32_t i = 0; i < hud.configstring_count; ++i) {
            const char *text = qa_strings_cstr(hud.strings, hud.configstrings[i]);
            if (text && *text && !q2_configstring(out, i, text, error)) return false;
        }
        return true;
    }
    qa_q2_config_layout layout; qa_q2_codec codec = {.protocol = out->protocol};
    if (!qa_q2_config_layout_read(&codec, &layout, error)) return false;
    size_t count = qa_q2_item_count(hud.game), images = 2;
    const char **names = calloc(count + images, sizeof(*names));
    if (!names) return application_fail(error, QA_ERROR_MEMORY, "Retaining chosen Q2 HUD image indices");
    names[0] = "i_health"; names[1] = "i_help";
    for (size_t i = 0; i < count; ++i) {
        const qa_q2_item_definition *item = qa_q2_item_at(hud.game, i);
        if (item->icon && *item->icon) names[images++] = item->icon;
    }
    qsort(names, images, sizeof(*names), image_compare);
    size_t unique = 0;
    for (size_t i = 0; i < images; ++i)
        if (!unique || strcmp(names[i], names[unique - 1])) names[unique++] = names[i];
    out->configstrings = calloc(2 + unique + count, sizeof(*out->configstrings));
    if (!out->configstrings) { free(names); return application_fail(error, QA_ERROR_MEMORY, "Retaining chosen Q2 HUD configstrings"); }
    char clients[32]; snprintf(clients, sizeof(clients), "%u", source->max_clients);
    bool ok = q2_configstring(out, 5, hud.statusbar, error) &&
        q2_configstring(out, layout.max_clients, clients, error);
    for (size_t i = 0; ok && i < unique; ++i)
        ok = q2_configstring(out, layout.images + (uint32_t)i + 1, names[i], error);
    for (size_t i = 0; ok && i < count; ++i)
        ok = q2_configstring(out, layout.items + (uint32_t)i + 1, qa_q2_item_at(hud.game, i)->name, error);
    free(names);
    if (ok) qsort(out->configstrings, out->configstring_count, sizeof(*out->configstrings), configstring_compare);
    return ok;
}

typedef struct q2_stat_images {
    const qa_unified_q2_configstring *rows;
    size_t count;
    uint32_t base;
} q2_stat_images;
static bool q2_stat_image(void *context, const char *name, uint32_t *out, qa_error *error)
{
    (void)error;
    const q2_stat_images *images = context;
    size_t low = 0, high = images->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int order = strcmp(images->rows[middle].value, name);
        if (order < 0) low = middle + 1; else high = middle;
    }
    *out = low == images->count || strcmp(images->rows[low].value, name) ? 0 :
        images->rows[low].index - images->base;
    return true;
}
static size_t q2_config_lower_bound(const qa_unified_q2_hud_configuration *config, uint32_t index)
{
    size_t low = 0, high = config->configstring_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (config->configstrings[middle].index < index) low = middle + 1; else high = middle;
    }
    return low;
}
static bool q2_hud_state(const qa_application_native_q2_hud *hud,
    const qa_unified_configuration_state *configuration, qa_unified_frame_player *out, qa_error *error)
{
    qa_unified_q2_hud_state *state = &out->q2_hud;
    const qa_unified_q2_hud_configuration *config = configuration->q2_hud;
    if (hud->original) memcpy(state->frame.stats, hud->stats, sizeof(hud->stats));
    else {
        qa_q2_config_layout layout; qa_q2_codec codec = {.protocol = config->protocol};
        if (!qa_q2_config_layout_read(&codec, &layout, error)) return false;
        size_t first = q2_config_lower_bound(config, layout.images);
        q2_stat_images images = {.rows = config->configstrings + first,
            .count = q2_config_lower_bound(config, layout.images + layout.max_images) - first, .base = layout.images};
        if (!qa_q2_wire_stats(hud->game, &hud->view,
            &(qa_q2_wire_stat_resources){.context = &images, .items_base = layout.items, .image = q2_stat_image},
            state->frame.stats, error)) return false;
    }
    state->frame.stat_count = hud->edition == QA_Q2_CLASSIC ? 32 : 64;
    state->frame.server_frame = hud->server_frame;
    state->frame.time_ms = (double)hud->time_ns / 1000000;
    state->frame.has_frame_time = hud->frame_ns != 0;
    state->frame.frame_time_ms = (double)hud->frame_ns / 1000000;
    state->player_number = hud->player_number;
    out->has_q2_hud = true;
    return true;
}

static bool configurations(qa_application *app, const application_unified_source *source,
    qa_unified_frame_metadata *out, qa_error *error)
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
            !provider(&v->inventory, application_provider_for(app, row->actor, QA_ROLE_INVENTORY, ""), error) ||
            !provider(&v->hud, application_provider_for(app, row->actor, QA_ROLE_HUD, ""), error) ||
            !q2_configuration(app, source, v, error)) return false;
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
        struct application_qc_state *engine = application_network_q1_qc_observation(app, p->owner, error);
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
        application_network_q1_qc_observation(app, p->owner, error) : NULL;
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
        else if (native) pattern = qa_strings_cstr(qa_session_strings(native->provider->application->session), native->configstrings[base + i]);
        qa_unified_style_pattern *row = out->styles + out->style_count++;
        row->family = source->family; row->index = (uint32_t)i;
        if (!application_unified_frame_string(NULL, &row->pattern, pattern ? pattern : "", error)) return false;
    }
    return true;
}

static bool q1_world_equal(const qa_unified_q1_world_state *saved, const qa_application_network_q1_world *actual)
{
    return (!saved && !actual) || (saved && actual && saved->level && actual->level &&
        !strcmp(saved->level, actual->level) && saved->total_secrets == actual->total_secrets &&
        saved->total_monsters == actual->total_monsters && saved->found_secrets == actual->found_secrets &&
        saved->killed_monsters == actual->killed_monsters);
}

static bool q1_world_metadata(const qa_application_network_q1_world *world,
    qa_unified_frame_metadata *metadata, qa_error *error)
{
    if (!world) return true;
    metadata->q1 = calloc(1, sizeof(*metadata->q1));
    if (!metadata->q1) return application_fail(error, QA_ERROR_MEMORY, "Retaining changed Q1 world metadata");
    *metadata->q1 = (qa_unified_q1_world_state){.total_secrets = world->total_secrets,
        .total_monsters = world->total_monsters, .found_secrets = world->found_secrets,
        .killed_monsters = world->killed_monsters};
    return application_unified_frame_string(NULL, &metadata->q1->level, world->level, error);
}

bool application_unified_output_metadata(qa_application *app, const application_unified_source *source,
    const application_unified_q3_sources *q3_sources, qa_unified_frame *frame, uint32_t epoch, const application_unified_metadata_receipt *committed,
    const qa_unified_document *committed_source_metadata, application_unified_metadata_receipt *proposed, qa_unified_document **out, qa_error *error)
{
    if (!app || !source || !epoch || !proposed || !out || *out || !application_unified_source_current(app, source))
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified metadata requires its completed Source and recipient epoch");
    application_unified_metadata_receipt value;
    if (!metadata_revision(app, source, epoch, &value, error)) return false;
    qa_application_network_q1_world q1_world;
    const qa_application_network_q1_world *q1 = NULL;
    if (source->family == QA_GAME_Q1) {
        if (!qa_application_network_q1_world_read(app, source->owner, &q1_world, error)) return false;
        q1 = &q1_world;
    }
    const qa_unified_frame_metadata *previous = qa_unified_document_metadata(committed_source_metadata);
    bool initial = !committed || committed->epoch != epoch || committed->map_revision != value.map_revision;
    bool configuration_changed = initial || committed->publication_revision != value.publication_revision ||
        committed->roster_revision != value.roster_revision;
    if (!configuration_changed) for (size_t i = 0; i < previous->configuration_count; ++i) {
        const qa_unified_configuration_state *old = previous->configurations + i;
        qa_application_native_q2_hud_source hud; bool found;
        if (!qa_application_native_q2_hud_source_read(app, old->actor, &hud, &found, error)) return false;
        if (!q2_configuration_same(old->q2_hud, &hud, found)) { configuration_changed = true; break; }
    }
    qa_application_native_q2_hud hud; bool has_q2_hud;
    if (!qa_application_native_q2_hud_read(app, frame->player->actor, &hud, &has_q2_hud, error)) return false;
    bool styles_changed = initial || committed->style_source != value.style_source || committed->style_revision != value.style_revision;
    bool q3_changed = initial || !application_unified_q3_sources_metadata_current(q3_sources, committed_source_metadata);
    bool q1_changed = initial || !q1_world_equal(previous ? previous->q1 : NULL, q1);
    value.q1_revision = committed && committed->epoch == epoch ? committed->q1_revision : 0;
    if (q1_changed) {
        if (value.q1_revision == UINT64_MAX) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 world metadata revision exhausted");
        ++value.q1_revision;
    }
    if (!configuration_changed && !styles_changed && !q3_changed && !q1_changed) {
        if (has_q2_hud && !q2_hud_state(&hud, player_configuration(previous, frame->player->actor), frame->player, error)) return false;
        *proposed = value; return true;
    }
    qa_unified_frame_metadata *metadata = calloc(1, sizeof(*metadata));
    if (!metadata) return application_fail(error, QA_ERROR_MEMORY, "Retaining changed Unified Source metadata");
    metadata->epoch = epoch; metadata->frame = source->frame.number;
    metadata->configuration_revision = previous ? previous->configuration_revision : 0;
    if (configuration_changed) ++metadata->configuration_revision;
    metadata->roster_revision = value.roster_revision;
    metadata->style_revision = value.style_revision;
    metadata->q1_revision = value.q1_revision;
    metadata->replace_configurations = configuration_changed; metadata->replace_styles = styles_changed;
    metadata->replace_q3 = q3_changed;
    metadata->replace_q1 = q1_changed;
    bool ok = (!configuration_changed || configurations(app, source, metadata, error)) &&
        (!styles_changed || styles(app, source, metadata, error)) &&
        (!q3_changed || application_unified_q3_sources_metadata(q3_sources, metadata, error)) &&
        (!q1_changed || q1_world_metadata(q1, metadata, error));
    if (ok && has_q2_hud) ok = q2_hud_state(&hud,
        player_configuration(configuration_changed ? metadata : previous, frame->player->actor), frame->player, error);
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
    qa_unified_world_frame *v = qa_unified_world_frame_create(pool, qa_session_strings(source->session), error);
    if (!v) return false;
    v->source = source->frame;
    qa_application_selected_effects clock;
    if (!qa_application_effects_producer_read(app,source->owner,&clock,error)) {
        qa_unified_world_frame_destroy(v);return false;
    }
    if (source->family==QA_GAME_Q3) {
        int32_t milliseconds=clock.q3_time_ms;
        if (clock.kind!=QA_APPLICATION_EFFECTS_Q3) {
            uint32_t bits=(uint32_t)(clock.source_time_ns/UINT64_C(1000000));
            memcpy(&milliseconds,&bits,sizeof(milliseconds));
        }
        v->presentation_seconds=(double)milliseconds/1000;
    } else v->presentation_seconds=(double)clock.source_time_ns/1000000000.0;
    if (source->family==QA_GAME_Q1 && clock.launch->selection.clock.kind!=QA_RULESET_QUAKEWORLD)
        v->presentation_seconds=(float)v->presentation_seconds;
    v->actors = count ? application_unified_frame_alloc(v->lease, count, sizeof(*v->actors), error) : NULL;
    v->bodies = count ? application_unified_frame_alloc(v->lease, count, sizeof(*v->bodies), error) : NULL;
    v->collisions = count ? application_unified_frame_alloc(v->lease, count, sizeof(*v->collisions), error) : NULL;
    v->world = application_unified_frame_alloc(v->lease, 1, sizeof(*v->world), error);
    bool ok = (!count || (v->actors && v->bodies && v->collisions)) && v->world;
    if (!ok) application_fail(error, QA_ERROR_MEMORY, "Retaining actual Source world rows");
    uint32_t cursor = 0; const qa_actor_record *record;
    while (ok && qa_actors_next(registry, &cursor, &record)) {
        if (qa_actors_revision(registry) != revision) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified world actor roster changed during observation");
            break;
        }
        qa_actor_id id = record->id;
        qa_unified_actor_state *a = v->actors + v->actor_count++;
        a->actor = id;
        a->owner=record->owner; a->definition=record->definition;
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
    if (ok && (!product || !map.resource))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Unified world lost its installed resource provenance");
    if (ok) {
        v->world->byte_length = qa_resource_bytes(map.resource).size;
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
        qa_inventory_entry *row = out->inventories->entries + out->inventories->entry_count++;
        row->count = entries[i].count; row->capacity = entries[i].capacity; row->policy = entries[i].policy;
        row->item=entries[i].item;
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

bool application_unified_resource_key(uint64_t serial, const qa_product *product, const char *path,
    const qa_resource *r, qa_unified_document **out, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *error)
{
    if (!serial || !out || !id || !product || !product->identity || !path || !r)
        return application_fail(error, QA_ERROR_ARGUMENT, "Unified resource needs its actual retained acquisition");
    application_unified_json key = {0};
    bool ok = resource(&key, product, path, r, error);
    qa_unified_document *candidate = NULL;
    if (ok) ok = qa_unified_document_create(QA_UNIFIED_CHECKPOINT,
        (qa_bytes){key.bytes.data, key.bytes.size}, &candidate, error);
    if (ok) {
        char actual_id[QA_APPLICATION_RESOURCE_KEY_CAPACITY] = {0};
        snprintf(actual_id, sizeof(actual_id), "resource:unified:%llu", (unsigned long long)serial);
        memcpy(id, actual_id, sizeof(actual_id));
        *out = candidate;
    }
    application_unified_json_dispose(&key);
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
