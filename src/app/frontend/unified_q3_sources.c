#include "unified_q3_sources.h"
#include "remote_unified_save.h"
#include "remote_unified_metadata.h"
#include "qa/unified_frame_q3.h"
#include "remote_unified_private.h"

#include <stdlib.h>
#include <string.h>

typedef struct received_source {
    frontend_unified_q3_source_view view;
    frontend_unified_q3_source_entity entities[QA_Q3_ENTITIES];
    frontend_unified_q3_source_player *players;
    uint16_t visible_entities[256];
    const char *provider_name, *instance, *content;
    qa_unified_document *metadata;
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
static void source_free(received_source *r)
{ if (r && --r->references == 0) { qa_unified_document_destroy(r->metadata); free(r->players); free(r); } }
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

static bool source_configuration(frontend_unified_q3_sources *o, const qa_unified_frame *frame,
    const qa_unified_q3_source *source, received_source *r, qa_error *e)
{
    const qa_unified_document *document=frontend_remote_unified_metadata_document(o->replica,frame);
    const qa_unified_frame_metadata *metadata = qa_unified_document_metadata(document);
    if (!metadata || metadata->epoch != frame->epoch || metadata->frame > frame->world->source.number)
        return fail(e,QA_ERROR_FORMAT,"Compiled Q3 Source precedes its actual reliable configuration");
    for (size_t i = 0; i < metadata->q3_configuration_count; ++i) {
        const qa_unified_q3_configuration *configuration = metadata->q3_configurations+i;
        if (configuration->publication != source->publication || configuration->map_revision != source->map_revision ||
            configuration->configuration_revision != source->configuration_revision ||
            strcmp(configuration->provider_name,source->provider_name) || strcmp(configuration->instance,source->instance) ||
            strcmp(configuration->content,source->content)) continue;
        if (!qa_unified_document_retain(document,&r->metadata,e)) return false;
        r->view.game_state = configuration->game_state;
        r->view.configstring_revisions = configuration->config_revisions;
        return true;
    }
    return fail(e,QA_ERROR_FORMAT,"Compiled Q3 Source lost its admitted reliable configuration owner");
}

static bool source_read(frontend_unified_q3_sources *o, const qa_unified_frame *frame, const qa_unified_q3_source *source,
    uint64_t revision, bool restoring, bool retired, received_source **out, qa_error *e)
{
    received_source *r = calloc(1,sizeof(*r));
    if (!r) return fail(e, QA_ERROR_MEMORY, "Retaining complete compiled Q3 Source records");
    r->references = 1;
    r->provider_name = source->provider_name; r->instance = source->instance; r->content = source->content;
    frontend_unified_q3_source_view *v = &r->view;
    *v = (frontend_unified_q3_source_view){.owner=o,.source=r,.revision=revision,.epoch=o->epoch,
        .publication=source->publication,.map_revision=source->map_revision,.product=source->product,
        .max_clients=source->max_clients,.snapshot_bit=source->snapshot_bit,.time=source->server_time,
        .level_start=source->level_start,.game_type=source->game_type,.provider_name=r->provider_name,
        .instance=r->instance,.content=r->content,.has_client=source->has_client,.client_number=source->client_number};
    bool ok = source_configuration(o,frame,source,r,e) &&
        frontend_remote_unified_source_actor(o->replica,frame,source->viewer,restoring,&v->viewer,e);
    for (size_t i = 0; ok && i < qa_executable_recipe_provider_count(o->recipe); ++i) {
        const qa_recipe_provider *p = qa_executable_recipe_provider(o->recipe,i);
        if (!strcmp(p->selection.instance,r->instance)) { v->provider = p; break; }
    }
    if (ok) ok = v->provider && v->provider->registered && v->provider->selection.runtime == QA_PROGRAM_BUILTIN &&
        v->provider->selection.clock.kind == QA_RULESET_Q3 &&
        (restoring ? qa_executable_recipe_content_read(o->recipe,r->content,&v->files,&v->content_product) :
            qa_executable_recipe_content(o->recipe,r->content,&v->files,&v->content_product,e)) &&
        qa_vfs_lookup_equal(v->files,v->provider->content) && v->content_product->family == QA_GAME_Q3 &&
        v->content_product->id == v->provider->selection.product;
    qa_actor_id viewer; uint32_t source_entity;
    if (ok && !retired) ok = frontend_remote_unified_player(o->replica,&viewer,&source_entity) && qa_actor_id_equal(viewer,v->viewer);
    for (size_t i = 0; ok && i < source->entity_count; ++i) {
        const qa_unified_q3_entity *row = source->entities+i;
        frontend_unified_q3_source_entity *entity = r->entities+row->number;
        *entity = (frontend_unified_q3_source_entity){.state=row->state,.origin=row->origin,.link_bounds=row->link_bounds,
            .server_flags=row->server_flags,.single_client=row->single_client,.present=true,.linked=row->linked};
        ok = frontend_remote_unified_source_actor(o->replica,frame,row->actor,restoring,&entity->actor,e);
        if (ok && row->number + 1 > v->entity_count) v->entity_count = row->number + 1;
    }
    if (ok && source->client_count) r->players = calloc(source->client_count,sizeof(*r->players));
    if (ok && source->client_count && !r->players) ok = fail(e,QA_ERROR_MEMORY,"Retaining actual compiled Q3 player-pointer roster");
    bool viewer_found = false;
    for (size_t i = 0; ok && i < source->client_count; ++i) {
        const qa_unified_q3_client *row = source->clients+i;
        frontend_unified_q3_source_player *p = r->players+i;
        *p = (frontend_unified_q3_source_player){.state=row->state,.source_number=row->source_number,.client_slot=(uint32_t)row->client_slot};
        ok = frontend_remote_unified_source_actor(o->replica,frame,row->actor,restoring,&p->actor,e) && r->entities[row->source_number].present &&
            qa_actor_id_equal(r->entities[row->source_number].actor,p->actor);
        if (ok && qa_actor_id_equal(p->actor,v->viewer) && row->source_number == v->client_number &&
            p->client_slot == row->source_number) viewer_found = true;
    }
    if (ok && v->has_client && !viewer_found) ok = fail(e,QA_ERROR_FORMAT,"Compiled Q3 recipient has no matching actual Source client PS");
    if (ok && source->visibility) {
        for (size_t i = 0; i < source->visibility->entity_count; ++i)
            r->visible_entities[i] = (uint16_t)source->visibility->entities[i];
        v->visible_count = source->visibility->entity_count;
        v->visible_entities = r->visible_entities; v->area_mask = source->visibility->area_mask;
    }
    if (ok && restoring) ok = frontend_unified_media_q3_assets_read(o->media,v->content_product,&v->assets);
    else if (ok) ok = frontend_unified_media_q3_assets(o->media,r->content,&v->assets,e);
    if (ok) { v->entities = r->entities; v->players = r->players; v->player_count = source->client_count; }
    if (!ok) { source_free(r); return e && e->code ? false : fail(e,QA_ERROR_FORMAT,"Compiled Q3 received Source disagrees with its admitted recipe"); }
    *out = r; return true;
}

static bool prepare(frontend_unified_q3_sources *o, const qa_unified_document *d,
    const qa_unified_frame_q3 *sources, bool restoring, frontend_unified_q3_source_frame **out, qa_error *e)
{
    if (!o || !out || *out || o->prepared || o->revision == UINT64_MAX ||
        !(restoring ? frontend_unified_q3_sources_checkpoint_current(o) : frontend_unified_q3_sources_current(o)))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 Source owner is not returned for its genuine frame");
    const qa_unified_frame *frame = qa_unified_document_frame(d);
    size_t count = sources ? sources->source_count : 0;
    if (count > qa_executable_recipe_provider_count(o->recipe))
        return fail(e,QA_ERROR_FORMAT,"Compiled Q3 Source list exceeds its admitted provider roster");
    frontend_unified_q3_source_frame *t = calloc(1,sizeof(*t));
    if (!t) return fail(e,QA_ERROR_MEMORY,"Retaining compiled Q3 Source frame admission");
    t->owner = o; t->revision = o->revision+1; t->count = count;
    t->rows = count ? calloc(count,sizeof(*t->rows)) : NULL;
    bool ok = (!count || t->rows) && qa_unified_document_retain(d,&t->packet,e);
    o->prepared = t;
    for (size_t i = 0; ok && i < count; ++i) {
        ok = source_read(o,frame,sources->sources+i,t->revision,restoring,false,t->rows+i,e);
        for (size_t k = 0; ok && k < i; ++k)
            if (!strcmp(t->rows[k]->instance,t->rows[i]->instance) ||
                !strcmp(t->rows[k]->provider_name,t->rows[i]->provider_name))
                ok = fail(e,QA_ERROR_FORMAT,"Compiled Q3 Source roster repeats its descriptor or event owner");
        for (size_t k = 0; ok && k < o->count; ++k) {
            const received_source *old = o->rows[k], *next = t->rows[i];
            if (strcmp(old->instance,next->instance)) continue;
            if (old->view.publication == next->view.publication && old->view.map_revision == next->view.map_revision &&
                (strcmp(old->provider_name,next->provider_name) || old->view.product != next->view.product || old->view.max_clients != next->view.max_clients ||
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
    const qa_unified_frame *frame = qa_unified_document_frame(d);
    if (!frame) return fail(e,QA_ERROR_ARGUMENT,"Compiled Q3 CLIENT requires a typed FRAME");
    return prepare(o,d,frame->q3,false,out,e);
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
            r->provider_name == v->provider_name && r->instance == v->instance && r->content == v->content && r->files == v->files &&
            r->content_product == v->content_product && r->product == v->product &&
            r->time == v->time && r->level_start == v->level_start && r->game_type == v->game_type &&
            r->max_clients == v->max_clients && r->entity_count == v->entity_count && r->player_count == v->player_count &&
            r->has_client == v->has_client && r->client_number == v->client_number &&
            r->snapshot_bit == v->snapshot_bit &&
            r->configstring_revisions == v->configstring_revisions &&
            r->visible_entities == v->visible_entities && r->visible_count == v->visible_count && r->area_mask == v->area_mask &&
            qa_actor_id_equal(r->viewer,v->viewer) &&
            frontend_unified_media_q3_assets_read(o->media,r->content_product,&assets) && assets == v->assets;
    }
    return false;
}
bool frontend_unified_q3_source_current(const frontend_unified_q3_source_view *v)
{ return row_current(v,false); }
bool frontend_unified_q3_source_checkpoint_current(const frontend_unified_q3_source_view *v)
{ return row_current(v,true); }
bool frontend_unified_q3_source_staged_checkpoint_current(const frontend_unified_q3_source_view *v)
{
    if (!frontend_unified_q3_source_checkpoint_current(v)) return false;
    const frontend_unified_q3_source_frame *t = v->owner->prepared;
    if (!t) return false;
    for (size_t i = 0; i < t->count; ++i) if (t->rows[i] == v->source) return true;
    return false;
}
bool frontend_unified_q3_source_retirement_current(const frontend_unified_q3_source_retirement *t)
{
    const frontend_unified_q3_sources *o = t ? t->owner : NULL;
    if (!o || !t->row || !t->row->references || !t->packet || t->row->view.owner != o ||
        t->row->view.source != t->row || t->row->view.epoch != o->epoch ||
        frontend_unified_media_recipe(o->media) != o->recipe || !frontend_unified_media_current(o->media)) return false;
    bool linked = false;
    for (const frontend_unified_q3_source_retirement *p = o->retirements; p; p = p->next) if (p == t) linked = true;
    qa_q3_presentation_assets *assets;
    return linked && frontend_unified_media_q3_assets_read(o->media,t->row->view.content_product,&assets) && assets == t->row->view.assets;
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
    if (!qa_unified_document_retain(o->packet,&t->packet,e)) {
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

bool frontend_unified_q3_sources_checkpoint_read(const frontend_unified_q3_sources *o,size_t index,
    frontend_unified_q3_source_view *out,qa_error *e)
{
    if (!out || !frontend_unified_q3_sources_checkpoint_current(o) || index >= o->count)
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

bool frontend_unified_q3_sources_checkpoint_stage_read(const frontend_unified_q3_sources *o,
    const frontend_unified_q3_source_frame *t,bool staged,size_t index,
    frontend_unified_q3_source_view *out,qa_error *e)
{
    if (!out || !o || (t && t->owner != o) || o->prepared != t || (staged && !t) ||
        !frontend_unified_q3_sources_checkpoint_current(o) || index >= (staged ? t->count : o->count))
        return fail(e,QA_ERROR_ARGUMENT,"Compiled Source cold row requires its exact published or staged owner");
    *out = (staged ? t->rows[index] : o->rows[index])->view;
    return frontend_unified_q3_source_checkpoint_current(out);
}
bool frontend_unified_q3_source_frame_checkpoint_ready(const frontend_unified_q3_source_frame *t)
{
    if (!t || !t->owner || t->owner->prepared != t || t->owner->revision == UINT64_MAX ||
        t->revision != t->owner->revision+1 || !frontend_unified_q3_sources_checkpoint_current(t->owner)) return false;
    for (size_t i = 0; i < t->count; ++i)
        if (!t->rows[i] || !frontend_unified_q3_source_checkpoint_current(&t->rows[i]->view)) return false;
    return true;
}
