#include "unified_q3_sources.h"
#include "remote_unified_save.h"
#include "qa/q3_abi.h"
#include "qa/source_save.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct received_source {
    frontend_unified_q3_source_view view;
    frontend_unified_q3_source_entity entities[QA_Q3_ENTITIES];
    frontend_unified_q3_source_player *players;
    qa_q3_gamestate game_state;
    uint64_t configstring_revisions[QA_Q3_CONFIGSTRINGS];
    uint16_t visible_entities[256];
    uint8_t area_mask[32];
    char *instance, *content;
    size_t references;
} received_source;
struct frontend_unified_q3_sources {
    frontend_remote_unified *replica;
    frontend_unified_media *media;
    qa_executable_recipe *recipe;
    uint32_t epoch;
    uint64_t revision;
    received_source **rows;
    size_t count;
    qa_unified_document *packet;
    frontend_unified_q3_source_frame *prepared;
    frontend_unified_q3_source_retirement *retirements;
};
struct frontend_unified_q3_source_retirement {
    frontend_unified_q3_source_retirement *next;
    frontend_unified_q3_sources *owner;
    received_source *row;
    qa_unified_document *packet;
    size_t index;
    const frontend_unified_q3_client *client;
};
struct frontend_unified_q3_source_frame {
    frontend_unified_q3_sources *owner;
    received_source **rows;
    size_t count;
    uint64_t revision;
    qa_unified_document *packet;
};
static bool fail(qa_error *e, qa_status code, const char *message)
{ qa_error_set(e, code, 0, "%s", message); return false; }
static qa_json_id field(const qa_unified_document *d, qa_json_id id, const char *name)
{ return qa_json_get(qa_unified_document_json(d), id, name); }
static bool natural(const qa_unified_document *d, qa_json_id id, uint64_t maximum,
    uint64_t *out, qa_error *e)
{
    double n;
    if (!qa_unified_document_number(d, id, &n, e)) return false;
    if (!isfinite(n) || n < 0 || n > (double)maximum || trunc(n) != n)
        return fail(e, QA_ERROR_FORMAT, "Compiled Q3 wire integer exceeds its declared domain");
    *out = (uint64_t)n; return true;
}
static bool integer(const qa_unified_document *d, qa_json_id id, int32_t *out, qa_error *e)
{
    double n;
    if (!qa_unified_document_number(d, id, &n, e)) return false;
    if (!isfinite(n) || n < INT32_MIN || n > INT32_MAX || trunc(n) != n)
        return fail(e, QA_ERROR_FORMAT, "Compiled Q3 signed Source word exceeds int32");
    *out = (int32_t)n; return true;
}
static bool text(const qa_unified_document *d, qa_json_id id, char **out, qa_error *e)
{
    qa_buffer value = {0};
    if (!qa_json_string(qa_unified_document_json(d), id, &value, e)) return false;
    if (memchr(value.data, 0, value.size)) {
        qa_buffer_free(&value); return fail(e, QA_ERROR_FORMAT, "Compiled Q3 Source text contains NUL");
    }
    *out = (char *)value.data; return true;
}
static bool vector(const qa_unified_document *d, qa_json_id id, qa_vec3 *out, qa_error *e)
{
    static const char *const names[] = {"x", "y", "z"};
    float values[3];
    for (size_t i = 0; i < 3; ++i) {
        double n;
        if (!qa_unified_document_number(d, field(d, id, names[i]), &n, e)) return false;
        if (!isfinite(n) || fabs(n) > FLT_MAX)
            return fail(e, QA_ERROR_FORMAT, "Compiled Q3 vector exceeds Source float storage");
        values[i] = (float)n;
    }
    *out = qa_v3(values[0], values[1], values[2]); return true;
}
static bool actor(frontend_unified_q3_sources *o, const qa_unified_document *d, qa_json_id id,
    bool restoring,qa_actor_id *out, qa_error *e)
{
    uint64_t slot, generation;
    return natural(d, field(d,id,"slot"), UINT32_MAX, &slot, e) &&
        natural(d, field(d,id,"generation"), QA_UNIFIED_SAFE_INTEGER, &generation, e) &&
        (restoring ? frontend_remote_unified_actor_retained(o->replica,(uint32_t)slot,generation,out,e) :
            frontend_remote_unified_actor(o->replica, (uint32_t)slot, generation, out, e));
}
static void source_free(received_source *r)
{ if (r && --r->references == 0) { free(r->instance); free(r->content); free(r->players); free(r); } }
static void rows_free(received_source **rows, size_t count)
{ if (rows) for (size_t i = 0; i < count; ++i) source_free(rows[i]); free(rows); }

bool frontend_unified_q3_sources_current(const frontend_unified_q3_sources *o)
{
    return o && o->recipe == frontend_remote_unified_recipe(o->replica) &&
        o->epoch == frontend_remote_unified_epoch(o->replica) &&
        frontend_unified_media_current(o->media) && frontend_remote_unified_current(o->replica, NULL);
}
bool frontend_unified_q3_sources_checkpoint_current(const frontend_unified_q3_sources *o)
{
    return o && o->recipe == frontend_remote_unified_recipe(o->replica) &&
        o->epoch == frontend_remote_unified_epoch(o->replica) && frontend_unified_media_current(o->media) &&
        frontend_remote_unified_checkpoint_current(o->replica,NULL);
}
static bool create(frontend_remote_unified *replica,frontend_unified_media *media,
    bool restoring,frontend_unified_q3_sources **out,qa_error *e)
{
    if (!replica || !media || !out || *out || !frontend_unified_media_current(media) ||
        !(restoring ? frontend_remote_unified_checkpoint_current(replica,e) : frontend_remote_unified_current(replica,e)) ||
        frontend_unified_media_recipe(media) != frontend_remote_unified_recipe(replica))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 CLIENT requires its actual replica and retained recipe media");
    frontend_unified_q3_sources *o = calloc(1,sizeof(*o));
    if (!o) return fail(e,QA_ERROR_MEMORY,"Retaining compiled Q3 received Source owner");
    o->replica = replica; o->media = media; o->recipe = frontend_remote_unified_recipe(replica);
    o->epoch = frontend_remote_unified_epoch(replica); *out = o; return true;
}
bool frontend_unified_q3_sources_create(frontend_remote_unified *replica, frontend_unified_media *media,
    frontend_unified_q3_sources **out, qa_error *e)
{
    return create(replica,media,false,out,e);
}

static bool read_record(const qa_unified_document *d, qa_json_id id, bool player,
    qa_q3_product product, void *out, qa_error *e)
{
    qa_buffer data = {0};
    size_t size = player ? qa_qvm_player_bytes(QA_QVM_Q3_MODERN) : qa_qvm_entity_bytes(QA_QVM_Q3_MODERN);
    bool ok = qa_unified_document_bytes(d, id, &data, e) && data.size == size;
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN, .bytes = {data.data, data.size}};
    if (ok && player) {
        qa_q3_player *ps = out;
        ok = qa_q3_abi_read_player(&record, 0, true, ps, e);
        if (ok) ps->product = product;
    } else if (ok) ok = qa_q3_abi_read_entity(&record, 0, true, out, e);
    qa_buffer_free(&data);
    return ok || (e && e->code ? false : fail(e, QA_ERROR_FORMAT, "Compiled Q3 record has no exact declared ABI bytes"));
}

static bool game_state(frontend_unified_q3_sources *o, const qa_unified_document *d,
    qa_json_id id, received_source *r, qa_error *e)
{
    (void)o;
    const qa_json_document *j = qa_unified_document_json(d);
    qa_json_id gs = field(d,id,"gameState"), offsets = field(d,gs,"stringOffsets");
    qa_buffer data = {0}; uint64_t count;
    bool ok = qa_json_string_equal(j, field(d,gs,"authority"), "compiled-source") &&
        natural(d, field(d,gs,"dataCount"), QA_Q3_GAMESTATE_CHARS, &count, e) && count &&
        qa_json_type(j, offsets) == QA_JSON_ARRAY && qa_json_size(j, offsets) == QA_Q3_CONFIGSTRINGS &&
        qa_unified_document_bytes(d, field(d,gs,"stringData"), &data, e) && data.size == count && !data.data[0];
    for (uint32_t i = 0; ok && i < QA_Q3_CONFIGSTRINGS; ++i) {
        uint64_t at;
        ok = natural(d, qa_json_at(j,offsets,i), count - 1, &at, e);
        if (ok && !memchr(data.data + at, 0, (size_t)count - (size_t)at)) ok = false;
        if (ok) r->game_state.config_offsets[i] = (uint16_t)at;
    }
    if (ok) {
        r->game_state.string_bytes = (size_t)count;
        memcpy(r->game_state.strings, data.data, data.size);
    }
    qa_buffer_free(&data);
    qa_json_id strings = field(d,id,"configstrings");
    if (ok) ok = qa_json_type(j, strings) == QA_JSON_ARRAY && qa_json_size(j,strings) == QA_Q3_CONFIGSTRINGS;
    for (uint32_t i = 0; ok && i < QA_Q3_CONFIGSTRINGS; ++i) {
        qa_json_id row = qa_json_at(j,strings,i); uint64_t index; char *value = NULL;
        ok = natural(d, field(d,row,"index"), QA_Q3_CONFIGSTRINGS-1, &index, e) && index == i &&
            natural(d, field(d,row,"revision"), QA_UNIFIED_SAFE_INTEGER, r->configstring_revisions+i, e) &&
            text(d, field(d,row,"value"), &value, e);
        if (ok) ok = !strcmp(value, qa_q3_configstring(&r->game_state,i));
        free(value);
    }
    return ok || (e && e->code ? false : fail(e, QA_ERROR_FORMAT, "Compiled Q3 GAMESTATE disagrees with its actual Source slots"));
}

static bool source_read(frontend_unified_q3_sources *o, const qa_unified_document *d,
    qa_json_id id, uint64_t revision, bool restoring, bool retired, received_source **out, qa_error *e)
{
    const qa_json_document *j = qa_unified_document_json(d);
    received_source *r = calloc(1,sizeof(*r));
    if (!r) return fail(e, QA_ERROR_MEMORY, "Retaining complete compiled Q3 Source records");
    r->references = 1;
    frontend_unified_q3_source_view *v = &r->view;
    v->owner = o; v->source = r; v->revision = revision; v->epoch = o->epoch;
    char *owner = NULL; uint64_t product, clients, snapshot_bit;
    qa_json_id activation = field(d,id,"activation");
    bool ok = qa_json_string_equal(j,field(d,id,"kind"),"compiled-q3") &&
        qa_json_string_equal(j,field(d,id,"abi"),"q3-modern") &&
        text(d,field(d,id,"owner"),&owner,e) && text(d,field(d,id,"instance"),&r->instance,e) &&
        !strcmp(owner,r->instance) && text(d,field(d,id,"content"),&r->content,e) &&
        natural(d,field(d,activation,"publication"),QA_UNIFIED_SAFE_INTEGER,&v->publication,e) && v->publication &&
        natural(d,field(d,activation,"mapRevision"),QA_UNIFIED_SAFE_INTEGER,&v->map_revision,e) &&
        natural(d,field(d,id,"product"),QA_Q3_TEAM_ARENA,&product,e) &&
        natural(d,field(d,id,"maxClients"),64,&clients,e) && clients &&
        natural(d,field(d,id,"snapshotBit"),4,&snapshot_bit,e) && (snapshot_bit == 0 || snapshot_bit == 4) &&
        integer(d,field(d,id,"serverTime"),&v->time,e) && integer(d,field(d,id,"levelStartTime"),&v->level_start,e) &&
        integer(d,field(d,id,"gameType"),&v->game_type,e) && actor(o,d,field(d,id,"viewer"),restoring,&v->viewer,e);
    free(owner);
    if (ok) {
        v->product = (qa_q3_product)product; v->max_clients = (uint32_t)clients; v->snapshot_bit = (uint8_t)snapshot_bit;
        v->instance = r->instance; v->content = r->content;
        for (size_t i = 0; i < qa_executable_recipe_provider_count(o->recipe); ++i) {
            const qa_recipe_provider *p = qa_executable_recipe_provider(o->recipe,i);
            if (!strcmp(p->selection.instance,r->instance)) { v->provider = p; break; }
        }
        ok = v->provider && v->provider->registered && v->provider->selection.runtime == QA_PROGRAM_BUILTIN &&
            v->provider->selection.clock.kind == QA_CLOCK_Q3 &&
            qa_executable_recipe_content_read(o->recipe,r->content,&v->files,&v->content_product) &&
            v->files == v->provider->content && v->content_product->family == QA_GAME_Q3 &&
            v->content_product->id == v->provider->selection.product;
        qa_actor_id viewer; uint32_t source_entity;
        if (ok && !retired) ok = frontend_remote_unified_player(o->replica,&viewer,&source_entity) && qa_actor_id_equal(viewer,v->viewer);
    }
    qa_json_id client = field(d,id,"clientNumber");
    if (ok && qa_json_type(j,client) != QA_JSON_NULL) {
        uint64_t n; ok = natural(d,client,v->max_clients-1,&n,e);
        if (ok) { v->has_client = true; v->client_number = (uint32_t)n; }
    }
    qa_json_id entities = field(d,id,"entities"), players = field(d,id,"clients");
    size_t entity_count = qa_json_size(j,entities), player_count = qa_json_size(j,players);
    if (ok) ok = qa_json_type(j,entities) == QA_JSON_ARRAY && entity_count <= QA_Q3_ENTITY_NONE &&
        qa_json_type(j,players) == QA_JSON_ARRAY && player_count <= entity_count;
    for (size_t i = 0; ok && i < entity_count; ++i) {
        qa_json_id row = qa_json_at(j,entities,i); uint64_t n, flags;
        ok = natural(d,field(d,row,"number"),QA_Q3_ENTITY_WORLD,&n,e);
        if (!ok) break;
        frontend_unified_q3_source_entity *entity = r->entities+n;
        if (entity->present) { ok = fail(e,QA_ERROR_FORMAT,"Compiled Q3 entity roster duplicates a physical Source row"); break; }
        ok = actor(o,d,field(d,row,"actor"),restoring,&entity->actor,e) &&
            read_record(d,field(d,row,"state"),false,v->product,&entity->state,e) && entity->state.number == (int32_t)n &&
            vector(d,field(d,row,"origin"),&entity->origin,e) &&
            qa_json_bool(j,field(d,row,"linked"),&entity->linked,e) &&
            natural(d,field(d,row,"serverFlags"),UINT32_MAX,&flags,e) && integer(d,field(d,row,"singleClient"),&entity->single_client,e);
        if (ok && entity->linked) {
            qa_json_id bounds = field(d,row,"linkBounds");
            ok = vector(d,field(d,bounds,"min"),&entity->link_bounds.mins,e) &&
                vector(d,field(d,bounds,"max"),&entity->link_bounds.maxs,e);
            if (ok) ok = entity->link_bounds.mins.x <= entity->link_bounds.maxs.x &&
                entity->link_bounds.mins.y <= entity->link_bounds.maxs.y && entity->link_bounds.mins.z <= entity->link_bounds.maxs.z;
        } else if (ok) ok = qa_json_type(j,field(d,row,"linkBounds")) == QA_JSON_NULL;
        if (ok) { entity->present = true; entity->server_flags = (uint32_t)flags;
            if (n + 1 > v->entity_count) v->entity_count = (uint32_t)n + 1; }
    }
    if (ok && player_count) r->players = calloc(player_count,sizeof(*r->players));
    if (ok && player_count && !r->players) ok = fail(e,QA_ERROR_MEMORY,"Retaining actual compiled Q3 player-pointer roster");
    bool viewer_found = false;
    for (size_t i = 0; ok && i < player_count; ++i) {
        qa_json_id row = qa_json_at(j,players,i); uint64_t n, slot; int32_t client_num;
        frontend_unified_q3_source_player *p = r->players+i;
        ok = natural(d,field(d,row,"number"),QA_Q3_ENTITY_WORLD,&n,e) &&
            natural(d,field(d,row,"clientSlot"),v->max_clients-1,&slot,e) &&
            integer(d,field(d,row,"clientNum"),&client_num,e) && actor(o,d,field(d,row,"actor"),restoring,&p->actor,e) &&
            read_record(d,field(d,row,"state"),true,v->product,&p->state,e) && p->state.clientNum == client_num &&
            r->entities[n].present && qa_actor_id_equal(r->entities[n].actor,p->actor);
        for (size_t k = 0; ok && k < i; ++k) if (r->players[k].source_number == n) ok = false;
        if (ok) { p->source_number = (uint32_t)n; p->client_slot = (uint32_t)slot;
            if (qa_actor_id_equal(p->actor,v->viewer) && n == v->client_number && slot == n) viewer_found = true; }
    }
    if (ok && v->has_client && !viewer_found) ok = fail(e,QA_ERROR_FORMAT,"Compiled Q3 recipient has no matching actual Source client PS");
    qa_json_id visibility = field(d,id,"visibility");
    if (ok && v->has_client) {
        qa_buffer mask = {0};
        qa_json_id visible = field(d,visibility,"entities");
        size_t count = qa_json_size(j,visible);
        ok = qa_json_type(j,visibility) == QA_JSON_OBJECT && qa_json_type(j,visible) == QA_JSON_ARRAY && count <= 256 &&
            qa_unified_document_bytes(d,field(d,visibility,"areaMask"),&mask,e) && mask.size == sizeof(r->area_mask);
        if (ok) memcpy(r->area_mask,mask.data,mask.size);
        qa_buffer_free(&mask);
        for (size_t i = 0; ok && i < count; ++i) {
            uint64_t number;
            ok = natural(d,qa_json_at(j,visible,i),QA_Q3_ENTITY_WORLD,&number,e) && r->entities[number].present &&
                (!i || number > r->visible_entities[i-1]);
            if (ok) r->visible_entities[i] = (uint16_t)number;
        }
        if (ok) { v->visible_count = count; v->visible_entities = r->visible_entities; v->area_mask = r->area_mask; }
    } else if (ok) ok = qa_json_type(j,visibility) == QA_JSON_NULL;
    if (ok) ok = game_state(o,d,id,r,e);
    if (ok && restoring) ok = frontend_unified_media_q3_assets_read(o->media,r->content,&v->assets);
    else if (ok) ok = frontend_unified_media_q3_assets(o->media,r->content,&v->assets,e);
    if (ok) { v->entities = r->entities; v->players = r->players; v->player_count = player_count;
        v->game_state = &r->game_state; v->configstring_revisions = r->configstring_revisions; }
    if (!ok) { source_free(r); return e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled Q3 received Source disagrees with its admitted recipe"); }
    *out = r; return true;
}

static bool prepare(frontend_unified_q3_sources *o, const qa_unified_document *d,
    qa_json_id array, bool restoring, frontend_unified_q3_source_frame **out, qa_error *e)
{
    if (!o || !out || *out || o->prepared || o->revision == UINT64_MAX ||
        !(restoring ? frontend_unified_q3_sources_checkpoint_current(o) : frontend_unified_q3_sources_current(o)))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 Source owner is not returned for its genuine frame");
    const qa_json_document *j = qa_unified_document_json(d);
    size_t count = qa_json_size(j,array);
    if (qa_json_type(j,array) != QA_JSON_ARRAY || count > qa_executable_recipe_provider_count(o->recipe))
        return fail(e,QA_ERROR_FORMAT,"Compiled Q3 Source list exceeds its admitted provider roster");
    frontend_unified_q3_source_frame *t = calloc(1,sizeof(*t));
    if (!t) return fail(e,QA_ERROR_MEMORY,"Retaining compiled Q3 Source frame admission");
    t->owner = o; t->revision = o->revision+1; t->count = count;
    t->rows = count ? calloc(count,sizeof(*t->rows)) : NULL;
    bool ok = (!count || t->rows) && qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,array),&t->packet,e);
    o->prepared = t;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = source_read(o,d,qa_json_at(j,array,i),t->revision,restoring,false,t->rows+i,e);
        for (size_t k = 0; ok && k < i; ++k) if (!strcmp(t->rows[k]->instance,t->rows[i]->instance)) ok = false;
        for (size_t k = 0; ok && k < o->count; ++k) {
            const received_source *old = o->rows[k], *next = t->rows[i];
            if (strcmp(old->instance,next->instance)) continue;
            if (old->view.publication == next->view.publication && old->view.map_revision == next->view.map_revision &&
                (old->view.product != next->view.product || old->view.max_clients != next->view.max_clients ||
                 old->view.has_client != next->view.has_client || old->view.client_number != next->view.client_number ||
                 (!qa_actor_id_equal(old->view.viewer,next->view.viewer) && old->view.snapshot_bit == next->view.snapshot_bit) || strcmp(old->content,next->content)))
                ok = fail(e,QA_ERROR_FORMAT,"Compiled Q3 retained activation changed its physical recipient or content");
        }
    }
    if (ok && restoring) {
        for (size_t i = 0; ok && i < count; ++i) ok = frontend_unified_q3_source_checkpoint_current(&t->rows[i]->view);
    } else if (ok) ok = frontend_unified_q3_sources_ready(t);
    if (!ok) { frontend_unified_q3_sources_abort(&t); return e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled Q3 frame admission lost its actual owner"); }
    *out = t; return true;
}
bool frontend_unified_q3_sources_prepare(frontend_unified_q3_sources *o, const qa_unified_document *d,
    frontend_unified_q3_source_frame **out, qa_error *e)
{
    if (!d || qa_unified_document_type(d) != QA_UNIFIED_FRAME_DOCUMENT)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 CLIENT requires the actual received FRAME document");
    return prepare(o,d,field(d,qa_unified_document_root(d),"compiledQ3Sources"),false,out,e);
}
bool frontend_unified_q3_sources_ready(const frontend_unified_q3_source_frame *t)
{
    if (!t || t->owner->prepared != t || t->revision != t->owner->revision+1 ||
        !frontend_unified_q3_sources_current(t->owner)) return false;
    for (size_t i = 0; i < t->count; ++i)
        if (!t->rows[i] || !frontend_unified_q3_source_current(&t->rows[i]->view)) return false;
    return true;
}
size_t frontend_unified_q3_source_frame_count(const frontend_unified_q3_source_frame *t)
{ return t && t->owner->prepared == t ? t->count : 0; }
bool frontend_unified_q3_source_frame_read(const frontend_unified_q3_source_frame *t, size_t index,
    frontend_unified_q3_source_view *out, qa_error *e)
{
    if (!t || !out || t->owner->prepared != t || index >= t->count || !t->rows[index] ||
        !frontend_unified_q3_sources_current(t->owner)) return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 staged Source requires its exact held ticket");
    *out = t->rows[index]->view;
    return frontend_unified_q3_source_current(out) || fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 staged Source has retired");
}
void frontend_unified_q3_sources_commit(frontend_unified_q3_source_frame **out)
{
    if (!out || !*out) return;
    frontend_unified_q3_source_frame *t = *out; frontend_unified_q3_sources *o = t->owner;
    rows_free(o->rows,o->count); qa_unified_document_destroy(o->packet);
    o->rows = t->rows; o->count = t->count; o->packet = t->packet; o->revision = t->revision; o->prepared = NULL;
    free(t); *out = NULL;
}
void frontend_unified_q3_sources_abort(frontend_unified_q3_source_frame **out)
{
    if (!out || !*out) return;
    frontend_unified_q3_source_frame *t = *out;
    if (t->owner->prepared == t) t->owner->prepared = NULL;
    rows_free(t->rows,t->count); qa_unified_document_destroy(t->packet); free(t); *out = NULL;
}
size_t frontend_unified_q3_sources_count(const frontend_unified_q3_sources *o)
{ return o ? (o->prepared ? o->prepared->count : o->count) : 0; }
static bool row_current(const frontend_unified_q3_source_view *v,bool cold)
{
    if (!v || !(cold ? frontend_unified_q3_sources_checkpoint_current(v->owner) : frontend_unified_q3_sources_current(v->owner))) return false;
    const frontend_unified_q3_sources *o = v->owner;
    received_source *found = NULL;
    for (size_t i = 0; i < o->count; ++i) if (o->rows[i] == v->source) found = o->rows[i];
    if (o->prepared) for (size_t i = 0; i < o->prepared->count; ++i)
        if (o->prepared->rows[i] == v->source) found = o->prepared->rows[i];
    if (found) {
        const frontend_unified_q3_source_view *r = &found->view;
        qa_q3_presentation_assets *assets;
        return r->revision == v->revision && r->epoch == v->epoch && r->provider == v->provider &&
            r->publication == v->publication && r->map_revision == v->map_revision &&
            r->entities == v->entities && r->players == v->players && r->game_state == v->game_state &&
            r->instance == v->instance && r->content == v->content && r->files == v->files &&
            r->content_product == v->content_product && r->product == v->product &&
            r->time == v->time && r->level_start == v->level_start && r->game_type == v->game_type &&
            r->max_clients == v->max_clients && r->entity_count == v->entity_count && r->player_count == v->player_count &&
            r->has_client == v->has_client && r->client_number == v->client_number &&
            r->snapshot_bit == v->snapshot_bit &&
            r->configstring_revisions == v->configstring_revisions &&
            r->visible_entities == v->visible_entities && r->visible_count == v->visible_count && r->area_mask == v->area_mask &&
            qa_actor_id_equal(r->viewer,v->viewer) &&
            frontend_unified_media_q3_assets_read(o->media,r->content,&assets) && assets == v->assets;
    }
    return false;
}
bool frontend_unified_q3_source_current(const frontend_unified_q3_source_view *v)
{ return row_current(v,false); }
bool frontend_unified_q3_source_checkpoint_current(const frontend_unified_q3_source_view *v)
{ return row_current(v,true); }
bool frontend_unified_q3_source_retirement_current(const frontend_unified_q3_source_retirement *t)
{
    const frontend_unified_q3_sources *o = t ? t->owner : NULL;
    if (!o || !t->row || !t->row->references || !t->packet || t->row->view.owner != o ||
        t->row->view.source != t->row || t->row->view.epoch != o->epoch ||
        frontend_unified_media_recipe(o->media) != o->recipe || !frontend_unified_media_current(o->media)) return false;
    bool linked = false;
    for (const frontend_unified_q3_source_retirement *p = o->retirements; p; p = p->next) if (p == t) linked = true;
    qa_q3_presentation_assets *assets;
    return linked && frontend_unified_media_q3_assets_read(o->media,t->row->content,&assets) && assets == t->row->view.assets;
}
bool frontend_unified_q3_source_retirement_departed(const frontend_unified_q3_source_retirement *t)
{
    if (!frontend_unified_q3_source_retirement_current(t)) return false;
    for (size_t i = 0; i < t->owner->count; ++i)
        if (t->owner->rows[i] == t->row) return false;
    return true;
}
bool frontend_unified_q3_source_retirement_prepare(const frontend_unified_q3_source_view *view,
    frontend_unified_q3_source_retirement **out,qa_error *e)
{
    if (!view || !out || *out || !frontend_unified_q3_source_current(view))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement custody requires its actual committed Source row");
    frontend_unified_q3_sources *o = (frontend_unified_q3_sources *)view->owner;
    size_t index = 0;
    while (index < o->count && o->rows[index] != view->source) ++index;
    if (index == o->count || !o->packet || o->rows[index]->references == SIZE_MAX)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement custody cannot borrow an unpublished or exhausted Source");
    frontend_unified_q3_source_retirement *t = calloc(1,sizeof(*t));
    if (!t) return fail(e,QA_ERROR_MEMORY,"Retaining actual compiled Source cleanup custody");
    const qa_json_document *j = qa_unified_document_json(o->packet);
    if (!qa_unified_document_create(QA_UNIFIED_CHECKPOINT,qa_json_source(j,qa_unified_document_root(o->packet)),&t->packet,e)) {
        free(t); return false;
    }
    t->owner = o; t->row = o->rows[index]; t->index = index; ++t->row->references;
    t->next = o->retirements; o->retirements = t; *out = t; return true;
}
bool frontend_unified_q3_source_retirement_checkpoint_current(const frontend_unified_q3_source_retirement *t)
{ return frontend_unified_q3_source_retirement_current(t) && frontend_unified_q3_sources_checkpoint_current(t->owner); }
bool frontend_unified_q3_source_retirement_client_hold(frontend_unified_q3_source_retirement *t,
    const frontend_unified_q3_client *client,qa_error *e)
{
    if (!client || !frontend_unified_q3_source_retirement_current(t) || t->client)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement custody already has a CLIENT or lost its parent");
    t->client = client; return true;
}
bool frontend_unified_q3_source_retirement_client_drop(frontend_unified_q3_source_retirement *t,
    const frontend_unified_q3_client *client,qa_error *e)
{
    if (!client || !frontend_unified_q3_source_retirement_current(t) || t->client != client)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement custody cannot release a different CLIENT");
    t->client = NULL; return true;
}
bool frontend_unified_q3_source_retirement_read(const frontend_unified_q3_source_retirement *t,
    frontend_unified_q3_source_view *out,qa_error *e)
{
    if (!out || !frontend_unified_q3_source_retirement_current(t))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement observation lost its actual structural custody");
    *out = t->row->view; return true;
}
bool frontend_unified_q3_source_retirement_return(frontend_unified_q3_source_retirement **out,qa_error *e)
{
    if (!out || !*out) return true;
    frontend_unified_q3_source_retirement *t = *out;
    if (!frontend_unified_q3_source_retirement_current(t) || t->client)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement release lost its retained resource owners");
    frontend_unified_q3_source_retirement **slot = &t->owner->retirements;
    while (*slot != t) slot = &(*slot)->next;
    *slot = t->next; source_free(t->row); qa_unified_document_destroy(t->packet); free(t); *out = NULL; return true;
}
bool frontend_unified_q3_source_retirement_checkpoint(const frontend_unified_q3_source_retirement *t,qa_buffer *out,qa_error *e)
{
    if (!out || out->data || !frontend_unified_q3_source_retirement_current(t) ||
        !frontend_unified_q3_sources_checkpoint_current(t->owner))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement capture requires its genuine cold parent");
    qa_source_save_io io = {0}; qa_buffer packet = {0}; uint32_t version = 1, epoch = t->row->view.epoch;
    uint64_t revision = t->row->view.revision; size_t index = t->index, size = 0;
    bool ok = qa_unified_document_encode(t->packet,&packet,e) && qa_source_save_writer(&io,NULL,e) &&
        qa_source_save_bytes(&io,(void *)"Q3SR",4) && qa_source_save_u32(&io,&version) && qa_source_save_u32(&io,&epoch) &&
        qa_source_save_u64(&io,&revision) && qa_source_save_count(&io,&index,qa_executable_recipe_provider_count(t->owner->recipe));
    size = packet.size;
    if (ok) ok = qa_source_save_count(&io,&size,32u*1024u*1024u) && qa_source_save_bytes(&io,packet.data,size) &&
        qa_source_save_finish(&io,out);
    qa_buffer_free(&packet); qa_source_save_dispose(&io); return ok;
}
bool frontend_unified_q3_source_retirement_restore(frontend_unified_q3_sources *o,qa_bytes bytes,
    frontend_unified_q3_source_retirement **out,qa_error *e)
{
    if (!o || !out || *out || o->prepared || !frontend_unified_q3_sources_checkpoint_current(o))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled retirement import requires its genuine isolated Source parent");
    frontend_unified_q3_source_retirement *t = calloc(1,sizeof(*t));
    if (!t) return fail(e,QA_ERROR_MEMORY,"Importing actual compiled Source cleanup custody");
    qa_source_save_io io = {0}; char magic[4]; uint32_t version, epoch; uint64_t revision; size_t size;
    bool ok = qa_source_save_reader(&io,NULL,bytes,e) && qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"Q3SR",4) &&
        qa_source_save_u32(&io,&version) && version == 1 && qa_source_save_u32(&io,&epoch) && epoch == o->epoch &&
        qa_source_save_u64(&io,&revision) && revision &&
        qa_source_save_count(&io,&t->index,qa_executable_recipe_provider_count(o->recipe)) &&
        qa_source_save_count(&io,&size,32u*1024u*1024u) && size && size <= io.input.size-io.offset &&
        qa_unified_document_decode(QA_UNIFIED_CHECKPOINT,(qa_bytes){io.input.data+io.offset,size},&t->packet,e);
    if (ok) {
        io.offset += size;
        const qa_json_document *j = qa_unified_document_json(t->packet); qa_json_id root = qa_unified_document_root(t->packet);
        ok = io.offset == io.input.size && qa_json_type(j,root) == QA_JSON_ARRAY &&
            qa_json_size(j,root) <= qa_executable_recipe_provider_count(o->recipe) && t->index < qa_json_size(j,root) &&
            source_read(o,t->packet,qa_json_at(j,root,t->index),revision,true,true,&t->row,e);
    }
    if (ok) { t->owner = o; t->next = o->retirements; o->retirements = t;
        ok = frontend_unified_q3_source_retirement_current(t); }
    if (ok) *out = t;
    else { if (t->owner) t->owner->retirements = t->next; source_free(t->row); qa_unified_document_destroy(t->packet); free(t); }
    qa_source_save_dispose(&io); return ok || (e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled retirement bytes lost their real Source provenance"));
}
bool frontend_unified_q3_sources_checkpoint_read(const frontend_unified_q3_sources *o,size_t index,
    frontend_unified_q3_source_view *out,qa_error *e)
{
    if (!out || !frontend_unified_q3_sources_checkpoint_current(o) || o->prepared || index >= o->count)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 cold observation lost its actual returned Source owner");
    *out = o->rows[index]->view;
    return frontend_unified_q3_source_checkpoint_current(out);
}
bool frontend_unified_q3_sources_read(const frontend_unified_q3_sources *o, size_t index,
    frontend_unified_q3_source_view *out, qa_error *e)
{
    if (!out || !frontend_unified_q3_sources_current(o) || index >= frontend_unified_q3_sources_count(o))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 Source observation has no actual retained row");
    received_source *r = o->prepared ? o->prepared->rows[index] : o->rows[index];
    *out = r->view;
    return frontend_unified_q3_source_current(out) || fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 Source row has retired");
}
size_t frontend_unified_q3_sources_committed_count(const frontend_unified_q3_sources *o)
{ return o ? o->count : 0; }
bool frontend_unified_q3_sources_committed_read(const frontend_unified_q3_sources *o,size_t index,
    frontend_unified_q3_source_view *out,qa_error *e)
{
    if (!out || !frontend_unified_q3_sources_current(o) || index >= o->count)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 published Source observation has retired");
    *out = o->rows[index]->view;
    return frontend_unified_q3_source_current(out) || fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 published Source row has retired");
}
bool frontend_unified_q3_sources_idle(const frontend_unified_q3_sources *o)
{ return !o || !o->prepared; }
bool frontend_unified_q3_sources_destroy(frontend_unified_q3_sources **out, qa_error *e)
{
    if (!out || !*out) return true;
    frontend_unified_q3_sources *o = *out;
    if (!frontend_unified_q3_sources_idle(o) || o->retirements)
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 Source retains its prepared frame or checked retirement custody");
    rows_free(o->rows,o->count); qa_unified_document_destroy(o->packet); free(o); *out = NULL; return true;
}

bool frontend_unified_q3_sources_checkpoint(const frontend_unified_q3_sources *o, qa_buffer *out, qa_error *e)
{
    if (!out || out->data || !frontend_unified_q3_sources_checkpoint_current(o) || !frontend_unified_q3_sources_idle(o))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 cold capture requires the returned actual Source owner");
    qa_source_save_io io = {0}; qa_buffer packet = {0}; uint32_t version = 1, epoch = o->epoch;
    uint64_t revision = o->revision; bool present = o->packet != NULL;
    bool ok = qa_source_save_writer(&io,NULL,e) && qa_source_save_bytes(&io,(void *)"Q3US",4) &&
        qa_source_save_u32(&io,&version) && qa_source_save_u32(&io,&epoch) && qa_source_save_u64(&io,&revision) &&
        qa_source_save_bool(&io,&present);
    if (ok && present) ok = qa_unified_document_encode(o->packet,&packet,e);
    size_t size = packet.size;
    if (ok) ok = qa_source_save_count(&io,&size,32u*1024u*1024u) && qa_source_save_bytes(&io,packet.data,size) &&
        qa_source_save_finish(&io,out);
    qa_buffer_free(&packet); qa_source_save_dispose(&io); return ok;
}
bool frontend_unified_q3_sources_restore(frontend_remote_unified *replica, frontend_unified_media *media,
    qa_bytes bytes, frontend_unified_q3_sources **out, qa_error *e)
{
    if (!replica || !media || !out || *out) return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 cold restore requires an empty owner");
    qa_source_save_io io = {0}; char magic[4]; uint32_t version, epoch; uint64_t revision; bool present; size_t size;
    bool ok = qa_source_save_reader(&io,NULL,bytes,e) && qa_source_save_bytes(&io,magic,4) && !memcmp(magic,"Q3US",4) &&
        qa_source_save_u32(&io,&version) && version == 1 && qa_source_save_u32(&io,&epoch) &&
        qa_source_save_u64(&io,&revision) && qa_source_save_bool(&io,&present) &&
        qa_source_save_count(&io,&size,32u*1024u*1024u) && size <= io.input.size-io.offset &&
        (present ? size != 0 && revision != 0 : size == 0 && revision == 0) && epoch == frontend_remote_unified_epoch(replica);
    qa_unified_document *packet = NULL; frontend_unified_q3_sources *o = NULL;
    if (ok && present) ok = qa_unified_document_decode(QA_UNIFIED_CHECKPOINT,(qa_bytes){io.input.data+io.offset,size},&packet,e);
    if (ok) { io.offset += size; ok = io.offset == io.input.size && create(replica,media,true,&o,e); }
    frontend_unified_q3_source_frame *t = NULL;
    if (ok && present) ok = prepare(o,packet,qa_unified_document_root(packet),true,&t,e);
    if (ok && t) frontend_unified_q3_sources_commit(&t);
    if (ok) { o->revision = revision;
        for (size_t i = 0; i < o->count; ++i) o->rows[i]->view.revision = revision;
        *out = o;
    } else { frontend_unified_q3_sources_abort(&t); frontend_unified_q3_sources_destroy(&o,NULL); }
    qa_unified_document_destroy(packet); qa_source_save_dispose(&io);
    return ok || (e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled Q3 cold Source bytes are malformed"));
}
