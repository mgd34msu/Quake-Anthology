#include "unified_q3_sources.h"
#include "unified_frame_private.h"
#include "qa/unified_frame_q3.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/collision.h"

#include <stdlib.h>
#include <limits.h>
#include <string.h>

typedef struct compiled_source {
    application_provider *provider;
    const qa_launch_instance *launch;
    const qa_q3_game *game;
    const qa_product *content;
    qa_source_frame frame;
    int32_t time, level_start, game_type;
    qa_q3_product product;
    uint32_t clients, entities;
    uint8_t snapshot_bit;
    uint64_t configuration_revision;
} compiled_source;

struct application_unified_q3_sources {
    qa_application *application;
    application_unified_source source;
    qa_net_client_id recipient;
    qa_unified_session_player player;
    application_provider **providers;
    size_t provider_count, count;
    uint64_t actors_revision;
    compiled_source *rows;
    qa_unified_frame_q3 *value;
    qa_unified_frame_lease *lease;
};

static bool returned(const application_unified_q3_sources *v)
{
    return v && v->application->providers == v->providers &&
        v->application->provider_count == v->provider_count &&
        application_unified_source_current(v->application, &v->source) &&
        application_unified_player_current(v->application, v->recipient, &v->player) &&
        qa_actors_revision(qa_session_actors(v->source.session)) == v->actors_revision;
}

static bool frame_equal(const qa_source_frame *a, const qa_source_frame *b)
{
    return a->provider == b->provider && a->kind == b->kind && a->phase == b->phase &&
        a->number == b->number && a->start_ns == b->start_ns &&
        a->time_ns == b->time_ns && a->elapsed_ns == b->elapsed_ns;
}

static bool provider_current(const application_unified_q3_sources *v, const compiled_source *r)
{
    if (!returned(v)) return false;
    bool installed = false;
    for (size_t i = 0; i < v->provider_count; ++i)
        if (v->providers[i] == r->provider) { installed = true; break; }
    if (!installed) return false;
    const application_provider *p = r->provider;
    qa_clock_state clock;
    int32_t time, start, type;
    qa_q3_product product;
    uint32_t clients, entities; uint8_t snapshot_bit;
    uint64_t configuration_revision;
    return p->application == v->application && p->kind == APPLICATION_PROVIDER_Q3 &&
        p->constructed && p->attached && !p->close_pending && p->state.q3 == r->game &&
        p->launch == r->launch && p->product == r->content &&
        qa_session_clock(v->source.session, p->owner, &clock) && frame_equal(&clock.frame, &r->frame) &&
        qa_q3_source_clock(r->game, &time, NULL) && time == r->time &&
        qa_q3_source_match_context_read(r->game, &product, &start, NULL) &&
        product == r->product && start == r->level_start &&
        application_native_q3_settings_integer(p, "g_gametype", &type, NULL) && type == r->game_type &&
        qa_q3_source_max_clients(r->game, &clients, NULL) && clients == r->clients &&
        qa_q3_source_entity_count(r->game, &entities, NULL) && entities == r->entities &&
        application_native_q3_wire_snapshot_bit(r->provider,&snapshot_bit,NULL) && snapshot_bit == r->snapshot_bit &&
        qa_q3_configstring_table_revision(r->game, &configuration_revision, NULL) &&
        configuration_revision == r->configuration_revision;
}

bool application_unified_q3_sources_current(const application_unified_q3_sources *v)
{
    if (!returned(v)) return false;
    size_t count = 0;
    for (size_t i = 0; i < v->provider_count; ++i) {
        const application_provider *p = v->providers[i];
        if (p->kind == APPLICATION_PROVIDER_Q3 && p->constructed && p->attached && !p->close_pending) ++count;
    }
    if (count != v->count) return false;
    for (size_t i = 0; i < v->count; ++i)
        if (!provider_current(v, v->rows + i)) return false;
    return returned(v);
}

static bool stable(const application_unified_q3_sources *v, const compiled_source *r, qa_error *e)
{
    return provider_current(v, r) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 projection left its actual returned Source");
}

static bool binding_current(const application_unified_q3_sources *v, const compiled_source *r,
    uint32_t slot, const qa_q3_source_binding *b, qa_error *e)
{
    qa_q3_source_binding current;
    uint32_t actual;
    return stable(v, r, e) && qa_q3_source_binding_read(r->game, slot, &current, e) &&
        current.in_use == b->in_use && current.client_slot == b->client_slot &&
        qa_actor_id_equal(current.actor, b->actor) &&
        qa_actors_get(qa_session_actors(v->source.session), b->actor) &&
        qa_q3_source_actor_slot(r->game, b->actor, &actual, e) && actual == slot;
}

static bool game_state(qa_unified_q3_configuration *out, const application_unified_q3_sources *v,
    const compiled_source *r, qa_error *e)
{
    out->game_state = calloc(1, sizeof(*out->game_state));
    if (!out->game_state) return application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 configstrings");
    qa_q3_gamestate_init(out->game_state);
    for (uint32_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        const char *value;
        if (!stable(v, r, e) || !qa_q3_configstring_read(r->game, i, &value, e) ||
            !qa_q3_configstring_revision(r->game, i, out->config_revisions + i, e)) return false;
        size_t bytes = strlen(value) + 1;
        if (bytes > 1) {
            qa_q3_gamestate *state = out->game_state;
            if (bytes > sizeof(state->strings) - state->string_bytes)
                return application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 configstrings exceed the genuine GAMESTATE extent");
            state->config_offsets[i] = (uint16_t)state->string_bytes;
            memcpy(state->strings + state->string_bytes, value, bytes);
            state->string_bytes += bytes;
        }
    }
    return true;
}

typedef struct source_visibility_world {
    qa_collision_geometry *geometry;
    qa_error failure;
} source_visibility_world;
static bool visibility_point(void *context, const float point[3], int32_t *area,
    int32_t *cluster, qa_error *e)
{
    source_visibility_world *w = context;
    qa_collision_leaf leaf;
    if (!qa_collision_point_leaf(w->geometry, qa_v3(point[0],point[1],point[2]), &leaf, e)) return false;
    if (leaf.area < -1 || leaf.area > INT32_MAX || leaf.cluster < -1 || leaf.cluster > INT32_MAX)
        return application_fail(e,QA_ERROR_FORMAT,"Compiled Q3 visibility exceeds its Source indices");
    *area = (int32_t)leaf.area; *cluster = (int32_t)leaf.cluster; return true;
}
static bool visibility_bits(void *context, int32_t area, uint8_t accumulator[32],
    size_t *count, qa_error *e)
{
    source_visibility_world *w = context; uint8_t bits[32];
    if (!qa_collision_area_bits(w->geometry,area,bits,sizeof(bits),count,e)) return false;
    for (size_t i = 0; i < *count; ++i) accumulator[i] |= bits[i];
    return true;
}
static bool visibility_connected(void *context, int32_t a, int32_t b)
{
    source_visibility_world *w = context; bool connected = false;
    return !w->failure.code && qa_collision_areas_connected(w->geometry,a,b,&connected,&w->failure) && connected;
}
static bool visibility_cluster(void *context, int32_t a, int32_t b)
{
    source_visibility_world *w = context; bool visible = false;
    return !w->failure.code && qa_collision_cluster_visible(w->geometry,a,b,false,&visible,&w->failure) && visible;
}
static bool source_visibility(qa_unified_q3_source *out, application_unified_q3_sources *v,
    const compiled_source *r, bool recipient, uint32_t client_number, qa_error *e)
{
    if (!recipient) return true;
    qa_collision_geometry *geometry = qa_world_geometry(v->source.world);
    if (!geometry) return application_fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 visibility lost its actual shared geometry");
    qa_q3_entity *states = qa_unified_frame_lease_alloc(v->lease, r->entities, sizeof(*states),
        _Alignof(qa_q3_entity), e);
    qa_q3_visibility_entity *entities = qa_unified_frame_lease_alloc(v->lease, r->entities, sizeof(*entities),
        _Alignof(qa_q3_visibility_entity), e);
    int32_t (*clusters)[16] = qa_unified_frame_lease_alloc(v->lease, r->entities, sizeof(*clusters),
        _Alignof(int32_t[16]), e);
    bool ok = states && entities && clusters;
    if (!ok) application_fail(e,QA_ERROR_MEMORY,"Retaining actual compiled Q3 visibility observations");
    qa_q3_player player;
    if (ok) ok = qa_q3_wire_player_read(r->game,client_number,&player,e) && stable(v,r,e);
    for (uint32_t i = 0; ok && i < r->entities; ++i) {
        qa_q3_source_binding binding;
        ok = stable(v,r,e) && qa_q3_source_binding_read(r->game,i,&binding,e);
        if (!ok || !binding.in_use) continue;
        qa_q3_wire_visibility raw;
        ok = binding_current(v,r,i,&binding,e) && qa_q3_wire_entity_read(r->game,i,states+i,&raw,e) &&
            binding_current(v,r,i,&binding,e);
        if (!ok) break;
        qa_q3_visibility_entity *entity = entities+i;
        *entity = (qa_q3_visibility_entity){.state=states+i,.linked=raw.linked,.flags=raw.server_flags,
            .single_client=raw.single_client,.area=-1,.area2=-1,.clusters=clusters[i]};
        if (!raw.linked) continue;
        qa_world_leaf_membership membership;
        ok = qa_world_link_membership(v->source.world,binding.actor,NULL,
            QA_WORLD_LEAVES_BOX,&membership,e) && binding_current(v,r,i,&binding,e);
        size_t count = ok && membership.count < 128 ? membership.count : 128;
        for (size_t k = 0; ok && k < count; ++k) {
            qa_collision_leaf leaf = membership.leaves[k];
            if (leaf.area < -1 || leaf.area > INT32_MAX || leaf.cluster < -1 || leaf.cluster > INT32_MAX)
                ok = application_fail(e,QA_ERROR_FORMAT,"Compiled Q3 linked leaf exceeds its Source index");
            if (!ok) break;
            int32_t area = (int32_t)leaf.area;
            if (area != -1) {
                if (entity->area != -1 && entity->area != area) entity->area2 = area;
                else entity->area = area;
            }
        }
        for (size_t k = 0; ok && k < count; ++k) {
            qa_collision_leaf leaf = membership.leaves[k];
            if (leaf.cluster == -1) continue;
            clusters[i][entity->cluster_count++] = (int32_t)leaf.cluster;
            if (entity->cluster_count == 16) {
                leaf = membership.leaves[membership.count-1];
                if (leaf.cluster < -1 || leaf.cluster > INT32_MAX)
                    ok = application_fail(e,QA_ERROR_FORMAT,"Compiled Q3 last leaf exceeds its Source cluster");
                if (ok) entity->last_cluster = (int32_t)leaf.cluster;
                break;
            }
        }
    }
    source_visibility_world world = {.geometry=geometry};
    qa_q3_visibility_world queries = {.context=&world,.point=visibility_point,.area_bits=visibility_bits,
        .areas_connected=visibility_connected,.cluster_visible=visibility_cluster};
    qa_q3_visible_entities visible;
    if (ok) ok = qa_q3_select_snapshot_entities(&player,entities,r->entities,&queries,false,&visible,e);
    if (world.failure.code) { if (e) *e = world.failure; ok = false; }
    if (ok) ok = stable(v,r,e) && qa_world_geometry(v->source.world) == geometry;
    if (ok) {
        out->visibility = qa_unified_frame_lease_alloc(v->lease, 1, sizeof(*out->visibility),
            _Alignof(qa_unified_q3_visibility), e);
        if (!out->visibility) ok = application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 visibility");
    }
    if (ok) {
        qa_unified_q3_visibility *visibility = out->visibility;
        memcpy(visibility->area_mask, visible.area_mask, sizeof(visibility->area_mask));
        visibility->entities = qa_unified_frame_lease_alloc(v->lease, visible.count, sizeof(*visibility->entities),
            _Alignof(uint32_t), e);
        if (visible.count && !visibility->entities)
            ok = application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 visible entity rows");
        else {
            visibility->entity_count = visible.count;
            for (size_t i = 0; i < visible.count; ++i) visibility->entities[i] = visible.entities[i].number;
        }
    }
    return ok || (e && e->code ? false : application_fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 visibility lost its actual linked Source"));
}

static bool source_capture(qa_unified_q3_source *out, application_unified_q3_sources *v,
    compiled_source *r, qa_error *e)
{
    const application_provider *p = r->provider;
    if (!application_unified_frame_string(v->lease, &out->provider_name,
            qa_strings_cstr(qa_session_strings(v->source.session), p->owner), e) ||
        !application_unified_frame_string(v->lease, &out->instance, r->launch->selection.instance, e) ||
        !application_unified_frame_string(v->lease, &out->content, r->content->identity, e)) return false;
    out->publication = v->source.publication; out->map_revision = v->source.map_revision;
    out->configuration_revision = r->configuration_revision;
    out->product = r->product; out->server_time = r->time; out->level_start = r->level_start;
    out->game_type = r->game_type; out->max_clients = r->clients; out->snapshot_bit = r->snapshot_bit;
    out->viewer = v->player.actor;
    for (uint32_t i = 0; i < r->clients; ++i) {
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(r->game, i, &binding, e)) return false;
        if (binding.in_use && qa_actor_id_equal(binding.actor, v->player.actor)) {
            if (out->has_client) return application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 recipient has duplicate physical client bindings");
            out->has_client = true; out->client_number = i;
        }
    }
    out->entities = qa_unified_frame_lease_alloc(v->lease, r->entities, sizeof(*out->entities),
        _Alignof(qa_unified_q3_entity), e);
    out->clients = qa_unified_frame_lease_alloc(v->lease, r->entities, sizeof(*out->clients),
        _Alignof(qa_unified_q3_client), e);
    if (r->entities && (!out->entities || !out->clients))
        return application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 physical rows");
    for (uint32_t i = 0; i < r->entities; ++i) {
        qa_q3_source_binding binding;
        if (!stable(v, r, e) || !qa_q3_source_binding_read(r->game, i, &binding, e)) return false;
        if (!binding.in_use) continue;
        qa_unified_q3_entity *entity = out->entities + out->entity_count++;
        entity->number = i; entity->actor = binding.actor;
        qa_q3_wire_visibility visibility;
        if (!binding_current(v, r, i, &binding, e) ||
            !qa_q3_wire_entity_read(r->game, i, &entity->state, &visibility, e) ||
            !binding_current(v, r, i, &binding, e) ||
            !qa_q3_source_current_origin_read(r->game, binding.actor, &entity->origin, e) ||
            !binding_current(v, r, i, &binding, e)) return false;
        entity->linked = visibility.linked; entity->server_flags = visibility.server_flags;
        entity->single_client = visibility.single_client;
        if (visibility.linked) {
            qa_body_link_state link;
            if (!qa_world_link_state(v->source.world, binding.actor, &link) || !link.linked ||
                !binding_current(v, r, i, &binding, e)) return false;
            entity->link_bounds = link.absolute_bounds;
        }
    }
    for (uint32_t i = 0; i < r->entities; ++i) {
        qa_q3_source_binding binding;
        if (!stable(v, r, e) || !qa_q3_source_binding_read(r->game, i, &binding, e)) return false;
        if (!binding.in_use || binding.client_slot < 0) continue;
        if ((uint32_t)binding.client_slot >= r->clients)
            return application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 entity has an invalid actual client pointer");
        qa_unified_q3_client *client = out->clients + out->client_count++;
        client->source_number = i; client->client_slot = binding.client_slot; client->actor = binding.actor;
        if (!binding_current(v, r, i, &binding, e) ||
            !qa_q3_wire_player_read(r->game, (uint32_t)binding.client_slot, &client->state, e) ||
            !binding_current(v, r, i, &binding, e)) return false;
        client->state.product = r->product;
    }
    return source_visibility(out, v, r, out->has_client, out->client_number, e) && stable(v, r, e);
}

bool application_unified_q3_sources_build(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player,
    qa_unified_frame *target, application_unified_q3_sources **out, qa_error *e)
{
    if (!app || !source || !player || !target || !target->lease || !out || *out ||
        !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 projection requires its actual Unified recipient");
    if (!qa_unified_frame_lease_retain(target->lease, e)) return false;
    application_unified_q3_sources *v = qa_unified_frame_lease_alloc(target->lease, 1, sizeof(*v),
        _Alignof(application_unified_q3_sources), e);
    if (!v) { qa_unified_frame_lease_release(target->lease); return false; }
    v->lease = target->lease;
    v->application = app; v->source = *source; v->recipient = recipient; v->player = *player;
    v->providers = app->providers; v->provider_count = app->provider_count;
    v->actors_revision = qa_actors_revision(qa_session_actors(source->session));
    if (v->provider_count > SIZE_MAX / sizeof(*v->rows)) {
        application_unified_q3_sources_dispose(v);
        return application_fail(e, QA_ERROR_MEMORY, "Compiled Q3 Source roster overflows");
    }
    v->rows = qa_unified_frame_lease_alloc(v->lease, v->provider_count, sizeof(*v->rows),
        _Alignof(compiled_source), e);
    if (v->provider_count && !v->rows) {
        application_unified_q3_sources_dispose(v); return false;
    }
    v->value = qa_unified_frame_lease_alloc(v->lease, 1, sizeof(*v->value),
        _Alignof(qa_unified_frame_q3), e);
    bool ok = v->value != NULL;
    if (ok && v->provider_count) {
        v->value->sources = qa_unified_frame_lease_alloc(v->lease, v->provider_count, sizeof(*v->value->sources),
            _Alignof(qa_unified_q3_source), e);
        ok = v->value->sources != NULL;
    }
    if (!ok) application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 typed Source section");
    for (size_t i = 0; ok && i < v->provider_count; ++i) {
        application_provider *p = v->providers[i];
        if (p->kind != APPLICATION_PROVIDER_Q3 || !p->constructed || !p->attached || p->close_pending) continue;
        compiled_source *r = v->rows + v->count;
        *r = (compiled_source){.provider = p, .launch = p->launch, .game = p->state.q3, .content = p->product};
        qa_clock_state clock;
        ok = p->application == app && r->game && r->launch && r->launch->content && r->content &&
            qa_session_clock(source->session, p->owner, &clock) &&
            qa_q3_source_clock(r->game, &r->time, e) &&
            qa_q3_source_match_context_read(r->game, &r->product, &r->level_start, e) &&
            application_native_q3_settings_integer(p, "g_gametype", &r->game_type, e) &&
            qa_q3_source_max_clients(r->game, &r->clients, e) &&
            qa_q3_source_entity_count(r->game, &r->entities, e) &&
            application_native_q3_wire_snapshot_bit(p,&r->snapshot_bit,e) &&
            qa_q3_configstring_table_revision(r->game, &r->configuration_revision, e);
        if (!ok) { if (!e || !e->code) application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 source metadata is absent"); break; }
        r->frame = clock.frame;
        if (!r->clients || r->clients > QA_Q3_SOURCE_CLIENTS || r->entities > QA_Q3_SOURCE_NONE) {
            ok = application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 source physical roster exceeds its genuine extent"); break;
        }
        qa_unified_q3_source *value = v->value->sources + v->value->source_count++;
        ok = source_capture(value, v, r, e);
        if (ok) ++v->count;
    }
    if (ok) ok = application_unified_q3_sources_current(v);
    if (!ok) {
        if (!e || !e->code) application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 Source projection changed during capture");
        application_unified_q3_sources_dispose(v); return false;
    }
    *out = v; return true;
}

bool application_unified_q3_sources_metadata_current(const application_unified_q3_sources *v,
    const qa_unified_document *document)
{
    const qa_unified_frame_metadata *metadata = qa_unified_document_metadata(document);
    if (!v || !v->value || !application_unified_q3_sources_current(v) || !metadata || !metadata->replace_q3 ||
        metadata->q3_configuration_count != v->count) return false;
    for (size_t i = 0; i < v->count; ++i) {
        const qa_unified_q3_source *source = v->value->sources + i;
        const qa_unified_q3_configuration *saved = metadata->q3_configurations + i;
        if (source->publication != saved->publication || source->map_revision != saved->map_revision ||
            source->configuration_revision != saved->configuration_revision ||
            strcmp(source->provider_name, saved->provider_name) ||
            strcmp(source->instance, saved->instance) || strcmp(source->content, saved->content)) return false;
    }
    return true;
}

bool application_unified_q3_sources_metadata(const application_unified_q3_sources *v,
    qa_unified_frame_metadata *out, qa_error *e)
{
    if (!v || !v->value || !out || out->q3_configurations || out->q3_configuration_count ||
        !application_unified_q3_sources_current(v))
        return application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 metadata requires its captured Source table");
    out->q3_configurations = v->count ? calloc(v->count, sizeof(*out->q3_configurations)) : NULL;
    if (v->count && !out->q3_configurations)
        return application_fail(e, QA_ERROR_MEMORY, "Retaining changed compiled Q3 configuration tables");
    out->q3_configuration_count = v->count;
    for (size_t i = 0; i < v->count; ++i) {
        const qa_unified_q3_source *source = v->value->sources + i;
        qa_unified_q3_configuration *row = out->q3_configurations + i;
        row->publication = source->publication; row->map_revision = source->map_revision;
        row->configuration_revision = source->configuration_revision;
        if (!application_unified_frame_string(NULL, &row->provider_name, source->provider_name, e) ||
            !application_unified_frame_string(NULL, &row->instance, source->instance, e) ||
            !application_unified_frame_string(NULL, &row->content, source->content, e) ||
            !game_state(row, v, v->rows + i, e)) return false;
    }
    return application_unified_q3_sources_current(v) ||
        application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 configuration changed during reliable capture");
}

const qa_unified_frame_q3 *application_unified_q3_sources_value(const application_unified_q3_sources *v)
{ return v ? v->value : NULL; }
qa_unified_frame_q3 *application_unified_q3_sources_take(application_unified_q3_sources *v)
{ if (!v) return NULL; qa_unified_frame_q3 *out = v->value; v->value = NULL; return out; }
void application_unified_q3_sources_dispose(application_unified_q3_sources *v)
{ if (v) { qa_unified_frame_lease *lease = v->lease; qa_unified_frame_lease_release(lease); } }
