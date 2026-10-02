#include "unified_q3_sources.h"
#include "unified_output_json.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/q3_abi.h"
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
    uint64_t strings[QA_Q3_CONFIGSTRINGS];
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
    qa_unified_document *value;
};

static bool text(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_text(j, s, e); }
static bool number(application_unified_json *j, double n, qa_error *e)
{ return application_unified_json_number(j, n, e); }
static bool string(application_unified_json *j, const char *s, qa_error *e)
{ return application_unified_json_string(j, s, e); }

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
        application_native_q3_wire_snapshot_bit(r->provider,&snapshot_bit,NULL) && snapshot_bit == r->snapshot_bit;
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
    for (size_t i = 0; i < v->count; ++i) {
        const compiled_source *r = v->rows + i;
        if (!provider_current(v, r)) return false;
        for (uint32_t slot = 0; slot < QA_Q3_CONFIGSTRINGS; ++slot) {
            uint64_t revision;
            if (!qa_q3_configstring_revision(r->game, slot, &revision, NULL) ||
                revision != r->strings[slot]) return false;
        }
    }
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

typedef struct record_writer { uint8_t *data; size_t size; } record_writer;
static bool record_write(void *context, size_t offset, qa_bytes bytes, qa_error *e)
{
    record_writer *w = context;
    if (offset > w->size || bytes.size > w->size - offset)
        return application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 record write exceeds its declared ABI extent");
    memcpy(w->data + offset, bytes.data, bytes.size);
    return true;
}
static bool bytes(application_unified_json *j, qa_bytes value, qa_error *e)
{
    qa_buffer encoded = {0};
    bool ok = qa_unified_checkpoint_bytes(value, &encoded, e) &&
        application_unified_json_append(j, (qa_bytes){encoded.data, encoded.size}, e);
    qa_buffer_free(&encoded);
    return ok;
}
static bool entity_record(application_unified_json *j, const qa_q3_entity *es, qa_error *e)
{
    uint8_t data[208] = {0};
    record_writer w = {data, qa_qvm_entity_bytes(QA_QVM_Q3_MODERN)};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN, .bytes = {data, w.size},
        .context = &w, .write = record_write};
    return qa_q3_abi_write_entity(&record, 0, true, es, e) && bytes(j, (qa_bytes){data, w.size}, e);
}
static bool player_record(application_unified_json *j, const qa_q3_player *ps, qa_error *e)
{
    uint8_t data[468] = {0};
    record_writer w = {data, qa_qvm_player_bytes(QA_QVM_Q3_MODERN)};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN, .bytes = {data, w.size},
        .context = &w, .write = record_write};
    return qa_q3_abi_write_player(&record, 0, true, false, ps, e) && bytes(j, (qa_bytes){data, w.size}, e);
}

static bool game_state(application_unified_json *j, application_unified_q3_sources *v,
    compiled_source *r, qa_error *e)
{
    char strings[QA_Q3_GAMESTATE_CHARS] = {0};
    uint16_t offsets[QA_Q3_CONFIGSTRINGS] = {0};
    size_t string_bytes = 1;
    bool ok = text(j, ",\"configstrings\":[", e);
    for (uint32_t i = 0; ok && i < QA_Q3_CONFIGSTRINGS; ++i) {
        const char *value;
        ok = stable(v, r, e) && qa_q3_configstring_read(r->game, i, &value, e) &&
            qa_q3_configstring_revision(r->game, i, r->strings + i, e);
        if (!ok) break;
        size_t size = strlen(value);
        if (size) {
            if (size + 1 > sizeof(strings) - string_bytes)
                return application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 configstrings exceed the genuine GAMESTATE extent");
            offsets[i] = (uint16_t)string_bytes;
            memcpy(strings + string_bytes, value, size + 1);
            string_bytes += size + 1;
        }
        ok = (!i || text(j, ",", e)) && text(j, "{\"index\":", e) && number(j, i, e) &&
            text(j, ",\"value\":", e) && string(j, value, e) && text(j, ",\"revision\":", e) &&
            application_unified_json_natural(j, r->strings[i], e) && text(j, "}", e);
    }
    if (ok) ok = text(j, "],\"gameState\":{\"authority\":\"compiled-source\",\"dataCount\":", e) &&
        application_unified_json_natural(j, string_bytes, e) && text(j, ",\"stringOffsets\":[", e);
    for (uint32_t i = 0; ok && i < QA_Q3_CONFIGSTRINGS; ++i)
        ok = (!i || text(j, ",", e)) && number(j, offsets[i], e);
    return ok && text(j, "],\"stringData\":", e) &&
        bytes(j, (qa_bytes){(const uint8_t *)strings, string_bytes}, e) && text(j, "}", e);
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
static bool source_visibility(application_unified_json *j, application_unified_q3_sources *v,
    const compiled_source *r, bool recipient, uint32_t client_number, qa_error *e)
{
    if (!text(j,",\"visibility\":",e)) return false;
    if (!recipient) return text(j,"null",e);
    qa_collision_geometry *geometry = qa_world_geometry(v->source.world);
    if (!geometry) return application_fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 visibility lost its actual shared geometry");
    size_t leaf_capacity = qa_bsp_record_count(qa_collision_bsp(geometry),QA_BSP_LEAVES);
    if (leaf_capacity > SIZE_MAX / sizeof(uint32_t))
        return application_fail(e,QA_ERROR_MEMORY,"Compiled Q3 visibility leaf roster overflows");
    uint32_t *leaves = leaf_capacity ? malloc(leaf_capacity * sizeof(*leaves)) : NULL;
    qa_q3_entity *states = calloc(r->entities,sizeof(*states));
    qa_q3_visibility_entity *entities = calloc(r->entities,sizeof(*entities));
    int32_t (*clusters)[16] = calloc(r->entities,sizeof(*clusters));
    bool ok = (!leaf_capacity || leaves) && states && entities && clusters;
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
        qa_body_link_state link; qa_leaf_list list;
        ok = qa_world_link_state(v->source.world,binding.actor,&link) && link.linked &&
            qa_collision_box_leaves(geometry,link.absolute_bounds,leaves,leaf_capacity,&list,e) &&
            !list.overflow && binding_current(v,r,i,&binding,e);
        size_t count = ok && list.count < 128 ? list.count : 128;
        for (size_t k = 0; ok && k < count; ++k) {
            qa_collision_leaf leaf;
            ok = qa_collision_leaf_at(geometry,leaves[k],&leaf,e);
            if (ok && (leaf.area < -1 || leaf.area > INT32_MAX || leaf.cluster < -1 || leaf.cluster > INT32_MAX))
                ok = application_fail(e,QA_ERROR_FORMAT,"Compiled Q3 linked leaf exceeds its Source index");
            if (!ok) break;
            int32_t area = (int32_t)leaf.area;
            if (area != -1) {
                if (entity->area != -1 && entity->area != area) entity->area2 = area;
                else entity->area = area;
            }
        }
        for (size_t k = 0; ok && k < count; ++k) {
            qa_collision_leaf leaf;
            ok = qa_collision_leaf_at(geometry,leaves[k],&leaf,e);
            if (!ok || leaf.cluster == -1) continue;
            clusters[i][entity->cluster_count++] = (int32_t)leaf.cluster;
            if (entity->cluster_count == 16) {
                ok = list.count && qa_collision_leaf_at(geometry,leaves[list.count-1],&leaf,e);
                if (ok && (leaf.cluster < -1 || leaf.cluster > INT32_MAX))
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
    if (ok) ok = stable(v,r,e) && qa_world_geometry(v->source.world) == geometry &&
        text(j,"{\"areaMask\":",e) && bytes(j,(qa_bytes){visible.area_mask,sizeof(visible.area_mask)},e) &&
        text(j,",\"entities\":[",e);
    for (size_t i = 0; ok && i < visible.count; ++i)
        ok = (!i || text(j,",",e)) && number(j,visible.entities[i].number,e);
    if (ok) ok = text(j,"]}",e);
    free(leaves); free(states); free(entities); free(clusters);
    return ok || (e && e->code ? false : application_fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 visibility lost its actual linked Source"));
}

static bool source_json(application_unified_json *j, application_unified_q3_sources *v,
    compiled_source *r, qa_error *e)
{
    const application_provider *p = r->provider;
    const char *owner = qa_strings_cstr(qa_session_strings(v->source.session), p->owner);
    bool ok = text(j, "{\"kind\":\"compiled-q3\",\"abi\":\"q3-modern\",\"owner\":", e) &&
        string(j, owner, e) && text(j, ",\"instance\":", e) && string(j, r->launch->selection.instance, e) &&
        text(j, ",\"content\":", e) && string(j, r->content->identity, e) &&
        text(j, ",\"activation\":{\"publication\":", e) && application_unified_json_natural(j, v->source.publication, e) &&
        text(j, ",\"mapRevision\":", e) && application_unified_json_natural(j, v->source.map_revision, e) &&
        text(j, "},\"product\":", e) && number(j, r->product, e) &&
        text(j, ",\"serverTime\":", e) && number(j, r->time, e) &&
        text(j, ",\"levelStartTime\":", e) && number(j, r->level_start, e) &&
        text(j, ",\"gameType\":", e) && number(j, r->game_type, e) &&
        text(j, ",\"maxClients\":", e) && number(j, r->clients, e) &&
        text(j, ",\"snapshotBit\":", e) && number(j,r->snapshot_bit,e) &&
        text(j, ",\"viewer\":", e) && application_unified_json_actor(j, v->player.actor, e) &&
        text(j, ",\"clientNumber\":", e);
    bool recipient = false;
    uint32_t client_number = 0;
    for (uint32_t i = 0; ok && i < r->clients; ++i) {
        qa_q3_source_binding b;
        ok = qa_q3_source_binding_read(r->game, i, &b, e);
        if (ok && b.in_use && qa_actor_id_equal(b.actor, v->player.actor)) {
            if (recipient) return application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 recipient has duplicate physical client bindings");
            recipient = true; client_number = i;
        }
    }
    if (ok) ok = recipient ? number(j, client_number, e) : text(j, "null", e);
    if (ok) ok = text(j, ",\"entities\":[", e);
    bool first = true;
    for (uint32_t i = 0; ok && i < r->entities; ++i) {
        qa_q3_source_binding b;
        ok = stable(v, r, e) && qa_q3_source_binding_read(r->game, i, &b, e);
        if (!ok || !b.in_use) continue;
        qa_q3_entity es; qa_q3_wire_visibility visibility; qa_vec3 origin;
        qa_body_link_state link = {0};
        ok = binding_current(v, r, i, &b, e) &&
            qa_q3_wire_entity_read(r->game, i, &es, &visibility, e) && binding_current(v, r, i, &b, e) &&
            qa_q3_source_current_origin_read(r->game, b.actor, &origin, e) && binding_current(v, r, i, &b, e);
        if (ok && visibility.linked)
            ok = qa_world_link_state(v->source.world, b.actor, &link) && link.linked && binding_current(v, r, i, &b, e);
        if (ok) ok = (first || text(j, ",", e)) && text(j, "{\"number\":", e) && number(j, i, e) &&
            text(j, ",\"actor\":", e) && application_unified_json_actor(j, b.actor, e) &&
            text(j, ",\"state\":", e) && entity_record(j, &es, e) && text(j, ",\"origin\":", e) &&
            application_unified_json_vector(j, origin, e) && text(j, ",\"linked\":", e) &&
            text(j, visibility.linked ? "true" : "false", e) && text(j, ",\"serverFlags\":", e) &&
            number(j, visibility.server_flags, e) && text(j, ",\"singleClient\":", e) &&
            number(j, visibility.single_client, e) && text(j, ",\"linkBounds\":", e) &&
            (visibility.linked ? application_unified_json_bounds(j, link.absolute_bounds, e) : text(j, "null", e)) &&
            text(j, "}", e);
        first = false;
    }
    if (ok) ok = text(j, "],\"clients\":[", e);
    first = true;
    for (uint32_t i = 0; ok && i < r->entities; ++i) {
        qa_q3_source_binding b;
        ok = stable(v, r, e) && qa_q3_source_binding_read(r->game, i, &b, e);
        if (!ok || !b.in_use || b.client_slot < 0) continue;
        if ((uint32_t)b.client_slot >= r->clients)
            return application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 entity has an invalid actual client pointer");
        qa_q3_player ps;
        ok = binding_current(v, r, i, &b, e) &&
            qa_q3_wire_player_read(r->game, (uint32_t)b.client_slot, &ps, e) && binding_current(v, r, i, &b, e);
        if (ok) ok = (first || text(j, ",", e)) && text(j, "{\"number\":", e) && number(j, i, e) &&
            text(j, ",\"clientSlot\":", e) && number(j, b.client_slot, e) &&
            text(j, ",\"clientNum\":", e) && number(j, ps.clientNum, e) &&
            text(j, ",\"actor\":", e) && application_unified_json_actor(j, b.actor, e) &&
            text(j, ",\"state\":", e) && player_record(j, &ps, e) && text(j, "}", e);
        first = false;
    }
    return ok && text(j, "]", e) && source_visibility(j,v,r,recipient,client_number,e) &&
        game_state(j, v, r, e) && stable(v, r, e) && text(j, "}", e);
}

bool application_unified_q3_sources_build(qa_application *app, const application_unified_source *source,
    qa_net_client_id recipient, const qa_unified_session_player *player,
    application_unified_q3_sources **out, qa_error *e)
{
    if (!app || !source || !player || !out || *out || !application_unified_source_current(app, source) ||
        !application_unified_player_current(app, recipient, player))
        return application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 projection requires its actual Unified recipient");
    application_unified_q3_sources *v = calloc(1, sizeof(*v));
    if (!v) return application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 Source projection");
    v->application = app; v->source = *source; v->recipient = recipient; v->player = *player;
    v->providers = app->providers; v->provider_count = app->provider_count;
    v->actors_revision = qa_actors_revision(qa_session_actors(source->session));
    if (v->provider_count > SIZE_MAX / sizeof(*v->rows)) {
        free(v); return application_fail(e, QA_ERROR_MEMORY, "Compiled Q3 Source roster overflows");
    }
    if (v->provider_count) v->rows = calloc(v->provider_count, sizeof(*v->rows));
    if (v->provider_count && !v->rows) {
        free(v); return application_fail(e, QA_ERROR_MEMORY, "Retaining compiled Q3 Source roster");
    }
    application_unified_json j = {0};
    bool ok = text(&j, "[", e);
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
            application_native_q3_wire_snapshot_bit(p,&r->snapshot_bit,e);
        if (!ok) { if (!e || !e->code) application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 source metadata is absent"); break; }
        r->frame = clock.frame;
        if (!r->clients || r->clients > QA_Q3_SOURCE_CLIENTS || r->entities > QA_Q3_SOURCE_NONE) {
            ok = application_fail(e, QA_ERROR_FORMAT, "Compiled Q3 source physical roster exceeds its genuine extent"); break;
        }
        ok = (!v->count || text(&j, ",", e)) && source_json(&j, v, r, e);
        if (ok) ++v->count;
    }
    if (ok) ok = text(&j, "]", e) && application_unified_q3_sources_current(v) &&
        qa_unified_document_create(QA_UNIFIED_CHECKPOINT, (qa_bytes){j.bytes.data, j.bytes.size}, &v->value, e);
    application_unified_json_dispose(&j);
    if (!ok) {
        if (!e || !e->code) application_fail(e, QA_ERROR_ARGUMENT, "Compiled Q3 Source projection changed during capture");
        application_unified_q3_sources_dispose(v); return false;
    }
    *out = v; return true;
}

const qa_unified_document *application_unified_q3_sources_value(const application_unified_q3_sources *v)
{ return v ? v->value : NULL; }
void application_unified_q3_sources_dispose(application_unified_q3_sources *v)
{ if (v) { qa_unified_document_destroy(v->value); free(v->rows); free(v); } }
