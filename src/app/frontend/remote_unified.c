#include "remote_unified_private.h"
#include "../application/unified_output_json.h"
#include "qa/network_unified_save.h"
#include "qa/network_unified_control.h"
#include "qa/unified_frame_prediction.h"
#include "qa/unified_frame_player.h"
#include "qa/unified_frame_events.h"
#include "qa/ruleset.h"
#include "remote_unified_save.h"
#include "remote_unified_presentation.h"
#include "remote_unified_metadata.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool frontend_unified_fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }

bool frontend_remote_unified_actor_retained(const frontend_remote_unified *owner,uint32_t slot,
    uint64_t generation,qa_actor_id *out,qa_error *error)
{
    if (!out || !frontend_remote_unified_checkpoint_current(owner,error)) return false;
    for (const frontend_unified_identity *row=owner->identities;row;row=row->next)
        if (row->wire.slot==slot && row->wire.generation==generation) {
            qa_saved_actor_id actual;
            if (!qa_actors_save_reference(owner->actors,row->actual,&actual,error)) return false;
            *out=row->actual; return true;
        }
    return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified import references an absent private identity receipt");
}

bool frontend_remote_unified_source_actor(frontend_remote_unified *owner,
    const qa_unified_frame *frame, qa_actor_id wire, bool retained,
    qa_actor_id *out, qa_error *error)
{
    if (!owner || !out || !owner->admitted || !owner->wire_player.registry ||
        (frame && (!frame->world || !frame->world->actor_count || frame->epoch != owner->epoch ||
            frame->world->actors[0].actor.registry != owner->wire_player.registry)))
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified actor lost its admitted Source frame");
    if (!wire.registry && !wire.generation && !wire.slot) { *out = (qa_actor_id){0}; return true; }
    if (wire.registry != owner->wire_player.registry)
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified actor belongs to a foreign Source registry");
    return retained ? frontend_remote_unified_actor_retained(owner, wire.slot, wire.generation, out, error) :
        frontend_remote_unified_actor(owner, wire.slot, wire.generation, out, error);
}

static bool linked(const frontend_remote_unified *owner)
{
    for (const frontend_remote_unified *row = owner && owner->frontend ? owner->frontend->remote_unified : NULL;
        row; row = row->next) if (row == owner) return true;
    return false;
}

bool frontend_remote_unified_current(const frontend_remote_unified *owner, qa_error *error)
{
    if (!owner || !linked(owner) || owner->retired || owner->frontend->application != owner->options.domain.application ||
        !owner->options.current(owner->options.context, &owner->options.domain, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified replica lost its actual CLIENT owner");
    if (owner->bound) {
        const qa_net_client *peer = qa_net_connections_get(qa_network_connections(owner->options.domain.runtime),
            owner->options.domain.client);
        if (!peer || peer->protocol.kind != QA_NET_UNIFIED_1 || peer->seat_count != 1 ||
            peer->seats[0].seat.owner != owner->options.domain.seat.owner ||
            peer->seats[0].seat.index != owner->options.domain.seat.index)
            return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified replica changed its real transport seat");
    }
    return !owner->recipe || qa_executable_recipe_current(owner->recipe, owner->options.domain.catalog) ||
        frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified replica changed its retained executable recipe");
}
bool frontend_remote_unified_retired(const frontend_remote_unified *owner)
{ return owner && owner->retired; }

bool frontend_remote_unified_create(qa_frontend *frontend, const frontend_remote_unified_options *options,
    frontend_remote_unified **out, qa_error *error)
{
    const frontend_remote_unified_domain *d = options ? &options->domain : NULL;
    const frontend_remote_unified_consumers *c = options ? &options->consumers : NULL;
    if (!frontend || !options || !out || *out || !d->application || frontend->application != d->application ||
        frontend->resource_inventory || !d->runtime || !d->catalog || !d->resources || !d->console || !d->cvars ||
        !d->seat.owner || d->client.owner || d->client.generation || d->client.slot ||
        d->physical_seat >= frontend->options.seats || !frontend->seats || !frontend->seats[d->physical_seat].input ||
        !options->identity_capacity || !options->current || !options->userinfo || !options->disconnected || !options->retirement || !options->transport_restart ||
        !c->prepare || !c->offer_publish || !c->offer_ready || !c->control || !c->frame || !c->publish || !c->input ||
        !c->begin_frame || !c->clock_read || !c->physical_ready || !c->physical_input ||
        !c->sample || !c->draw || !c->idle || !c->checkpoint_returned || !c->close || !c->content_visit ||
        !options->current(options->context, d, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified construction requires its actual CLIENT and presentation consumers");
    frontend_remote_unified *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Allocating unified readonly replica");
    owner->frontend = frontend; owner->options = *options;
    frontend_legacy_cvars_bind(d->cvars,&owner->legacy_cvars);
    frontend_remote_q2_effects_cvars_bind(d->cvars,&owner->q2_effect_cvars);
    frontend_q1_sky_controls_bind(qa_application_cvars(d->application),&owner->sky_controls);
    if (!qa_pool_prepare(&owner->identity_records, &owner->identity_storage,
            (size_t)options->identity_capacity * 4, sizeof(frontend_unified_identity),
            _Alignof(frontend_unified_identity), error) ||
        !qa_actors_create(options->identity_capacity, NULL, NULL, &owner->actors, error)) {
        qa_arena_destroy(&owner->identity_storage); free(owner); return false;
    }
    qa_arena_seal(&owner->identity_storage);
    owner->strings=qa_session_strings(qa_application_session(d->application)); qa_strings_retain(owner->strings);
    qa_catalog_retain(d->catalog);
    owner->next = frontend->remote_unified; frontend->remote_unified = owner; *out = owner; return true;
}

bool frontend_remote_unified_bind(frontend_remote_unified *owner, qa_net_client_id client,
    qa_unified_session *session, qa_error *error)
{
    qa_unified_session *installed = NULL;
    if (!owner || owner->bound || owner->busy || !session || !client.owner || !client.generation ||
        !qa_unified_session_idle(session) || !frontend_remote_unified_current(owner, error))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified binding requires its actual successful attach");
    frontend_remote_unified_domain candidate = owner->options.domain; candidate.client = client;
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(candidate.runtime), client);
    if (!peer || peer->protocol.kind != QA_NET_UNIFIED_1 || peer->seat_count != 1 ||
        peer->seats[0].seat.owner != candidate.seat.owner || peer->seats[0].seat.index != candidate.seat.index ||
        !owner->options.current(owner->options.context, &candidate, error) ||
        !qa_unified_session_find(candidate.runtime, client, &installed, error) || installed != session)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified attach changed its genuine CLIENT tuple");
    if (!owner->metadata_cuts.values) {
        const qa_unified_limits *limits=qa_unified_session_limits(session);
        size_t capacity=(limits->queued_reliable_bytes+limits->message_bytes)/(sizeof(frontend_unified_metadata_cut)+sizeof(qa_unified_frame_metadata));
        if (!qa_pool_prepare(&owner->metadata_cuts,&owner->metadata_storage,capacity,
            sizeof(frontend_unified_metadata_cut),_Alignof(frontend_unified_metadata_cut),error)) return false;
        qa_arena_seal(&owner->metadata_storage);
    }
    owner->options.domain = candidate; owner->session = session; owner->bound = true; return true;
}

qa_executable_recipe *frontend_remote_unified_recipe(const frontend_remote_unified *owner)
{ return owner ? owner->recipe : NULL; }
qa_actor_registry *frontend_remote_unified_registry(const frontend_remote_unified *owner)
{ return owner ? owner->actors : NULL; }
const qa_collision_geometry *frontend_remote_unified_geometry(const frontend_remote_unified *owner)
{ return qa_executable_recipe_geometry(frontend_remote_unified_recipe(owner)); }
const qa_unified_document *frontend_remote_unified_frame(const frontend_remote_unified *owner)
{ return owner ? owner->frame : NULL; }
const frontend_remote_unified_domain *frontend_remote_unified_domain_read(const frontend_remote_unified *owner)
{ return owner ? &owner->options.domain : NULL; }
uint32_t frontend_remote_unified_epoch(const frontend_remote_unified *owner)
{ return owner ? owner->epoch : 0; }

static const qa_recipe_provider *frame_provider(const frontend_remote_unified *owner,
    const qa_unified_document *document, qa_launch_role role, const char *selector)
{
    const qa_unified_frame *frame = qa_unified_document_frame(document);
    const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(
        frontend_remote_unified_metadata_document(owner,frame));
    if (!owner || !owner->admitted || !owner->recipe || (selector && *selector) || !frame || !frame->world ||
        !metadata || metadata->epoch!=frame->epoch || metadata->frame>frame->world->source.number) return NULL;
    for (size_t i = 0; i < metadata->configuration_count; ++i) {
        const qa_unified_configuration_state *row = metadata->configurations + i;
        if (!qa_actor_id_equal(row->actor,owner->wire_player)) continue;
        const qa_unified_provider_state *selected = role == QA_ROLE_MOVEMENT ? &row->movement :
            role == QA_ROLE_INVENTORY ? &row->inventory : role == QA_ROLE_CHARACTER ? &row->character :
            role == QA_ROLE_BODY ? &row->appearance : role == QA_ROLE_HUD ? &row->hud :
            role == QA_ROLE_ARSENAL && row->weapon_count ? row->weapons : NULL;
        if (!selected || !selected->provider || !selected->content) return NULL;
        for (size_t p = 0; p < qa_executable_recipe_provider_count(owner->recipe); ++p) {
            const qa_recipe_provider *provider = qa_executable_recipe_provider(owner->recipe, p);
            const qa_product *product = provider ? qa_catalog_product(owner->options.domain.catalog, provider->selection.product) : NULL;
            if (provider && product && (provider->roles & QA_ROLE_BIT(role)) &&
                !strcmp(selected->provider, provider->selection.instance) && !strcmp(selected->content, product->identity)) return provider;
        }
        return NULL;
    }
    return NULL;
}
const qa_unified_document *frontend_remote_unified_frame_prepared(const frontend_remote_unified *owner)
{ return owner?owner->prepared_frame:NULL; }

const qa_recipe_provider *frontend_remote_unified_provider(const frontend_remote_unified *owner,
    qa_launch_role role,const char *selector)
{
    return frame_provider(owner,owner?(owner->prepared_frame?owner->prepared_frame:owner->frame):NULL,role,selector);
}
const qa_recipe_provider *frontend_remote_unified_provider_published(const frontend_remote_unified *owner,
    qa_launch_role role,const char *selector)
{
    return frame_provider(owner,owner?owner->frame:NULL,role,selector);
}

static bool frame_actor_present(const qa_unified_document *document, uint32_t slot, uint64_t generation)
{
    const qa_unified_frame *frame = qa_unified_document_frame(document);
    if (!frame || !frame->world) return false;
    const qa_unified_world_frame *world = frame->world;
    size_t low = 0, high = world->actor_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2; qa_actor_id actor = world->actors[middle].actor;
        if (actor.slot < slot || (actor.slot == slot && actor.generation < generation)) low = middle + 1;
        else high = middle;
    }
    return low < world->actor_count && world->actors[low].actor.slot == slot && world->actors[low].actor.generation == generation;
}
bool frontend_remote_unified_actor_present(const frontend_remote_unified *owner,uint32_t slot,uint64_t generation)
{ return owner && frame_actor_present(owner->prepared_frame?owner->prepared_frame:owner->frame,slot,generation); }
bool frontend_remote_unified_actor_published(const frontend_remote_unified *owner,uint32_t slot,uint64_t generation)
{ return owner && frame_actor_present(owner->frame,slot,generation); }
bool frontend_remote_unified_actor(frontend_remote_unified *owner, uint32_t slot,
    uint64_t generation, qa_actor_id *out, qa_error *error)
{
    if (!owner || !out || !linked(owner) || owner->retired)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified identity needs its actual replica namespace");
    for (frontend_unified_identity *row = owner->identities; row; row = row->next)
        if (row->wire.slot == slot && row->wire.generation == generation) {
            if (frontend_remote_unified_actor_present(owner, slot, generation) && !qa_actors_get(owner->actors, row->actual))
                return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified frame resurrects a retired wire identity");
            *out = row->actual; return true;
        }
    bool historical = (owner->prepared_frame || owner->frame) &&
        !frontend_remote_unified_actor_present(owner, slot, generation);
    size_t identity_slot;
    frontend_unified_identity *row = qa_pool_take(&owner->identity_records, &identity_slot);
    if (!row) return frontend_unified_fail(error, QA_ERROR_MEMORY, "Retaining unified wire identity");
    if (!qa_actors_allocate(owner->actors, 0, 0, &row->actual, error)) {
        qa_pool_release(&owner->identity_records, identity_slot); return false;
    }
    if (historical && !qa_actors_release(owner->actors, row->actual, error)) {
        qa_pool_release(&owner->identity_records, identity_slot); return false;
    }
    row->wire = (qa_saved_actor_id){.slot = slot, .generation = generation};
    row->next = owner->identities; owner->identities = row; *out = row->actual; return true;
}
bool frontend_remote_unified_wire_actor(const frontend_remote_unified *owner, qa_actor_id actor,
    qa_saved_actor_id *out)
{
    if (!owner || !out) return false;
    for (const frontend_unified_identity *row = owner->identities; row; row = row->next)
        if (qa_actor_id_equal(row->actual, actor)) { *out = row->wire; return true; }
    return false;
}
bool frontend_remote_unified_player(const frontend_remote_unified *owner, qa_actor_id *actor, uint32_t *entity)
{
    if (!owner || !owner->admitted || !actor || !entity) return false;
    *actor = owner->player; *entity = owner->source_entity; return true;
}

static qa_json_id value(const qa_unified_document *document)
{ return qa_json_get(qa_unified_document_json(document), qa_unified_document_root(document), "value"); }
bool frontend_unified_document_equal(const qa_unified_document *a, const qa_unified_document *b)
{ return a && b && qa_unified_document_equal(a, b); }
bool frontend_unified_document_restore_bind(qa_unified_document **retained,
    const qa_unified_document *canonical, bool encoded, qa_error *error)
{
    if (!retained || !*retained || !canonical ||
        qa_unified_document_type(*retained)!=qa_unified_document_type(canonical))
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Restored document differs from its actual parent");
    if (*retained==canonical) return true;
    if (encoded) {
        qa_buffer actual={0},saved={0};
        bool okay=qa_unified_document_encode(*retained,&saved,error) &&
            qa_unified_document_encode(canonical,&actual,error);
        bool equal=okay && actual.size==saved.size &&
            (!actual.size || !memcmp(actual.data,saved.data,actual.size));
        qa_buffer_free(&actual); qa_buffer_free(&saved);
        if (!okay) return false;
        if (!equal) return frontend_unified_fail(error,QA_ERROR_FORMAT,"Restored document differs from its actual parent");
    } else if (!frontend_unified_document_equal(*retained,canonical))
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Restored document differs from its actual parent");
    qa_unified_document *bound=NULL;
    if (!qa_unified_document_retain(canonical,&bound,error)) return false;
    qa_unified_document_destroy(*retained); *retained=bound; return true;
}


static void staged_metadata_clear(frontend_remote_unified *owner)
{
    if (owner->metadata_lease) qa_unified_frame_lease_release(owner->metadata_lease);
    else free(owner->metadata);
    owner->metadata=NULL; owner->metadata_count=0; owner->metadata_lease=NULL;
}
static bool stage_metadata(frontend_remote_unified *owner, qa_error *error)
{
    const qa_unified_frame *frame = qa_unified_document_frame(owner->prepared_frame);
    if (!frame || !frame->world) return false;
    size_t count = frame->world->actor_count;
    qa_unified_frame_lease *lease=frame->lease;
    if (lease && !qa_unified_frame_lease_retain(lease,error)) return false;
    frontend_unified_metadata *metadata = count ? (lease ? qa_unified_frame_lease_alloc(lease,count,sizeof(*metadata),
        _Alignof(frontend_unified_metadata),error) : calloc(count,sizeof(*metadata))) : NULL;
    if (count && !metadata) { qa_unified_frame_lease_release(lease);
        return frontend_unified_fail(error, QA_ERROR_MEMORY, "Staging received actor metadata"); }
    for (size_t i = 0; i < count; ++i) {
        const qa_unified_actor_state *row = frame->world->actors + i;
        bool okay = frontend_remote_unified_source_actor(owner, frame, row->actor, false, &metadata[i].actor, error);
        metadata[i].owner=row->owner; metadata[i].definition=row->definition;
        if (!okay) { if (lease) qa_unified_frame_lease_release(lease); else free(metadata); return false; }
    }
    owner->metadata = metadata; owner->metadata_count = count; owner->metadata_lease=lease; return true;
}

static bool metadata_apply(frontend_remote_unified *owner, qa_error *error)
{
    uint64_t revision = qa_actors_revision(owner->actors);
    if (owner->metadata_count > (UINT64_MAX - revision) / 2)
        return frontend_unified_fail(error, QA_ERROR_MEMORY, "Actor metadata cannot retain a rollback revision");
    for (size_t i = 0; i < owner->metadata_count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        const qa_actor_record *actual = qa_actors_get(owner->actors, row->actor);
        if (!actual || actual->has_source)
            return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Received actor metadata lost its private identity");
        row->old_owner = actual->owner; row->old_definition = actual->definition;
    }
    for (size_t i = 0; i < owner->metadata_count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        if (!qa_actors_set_metadata(owner->actors, row->actor, row->owner, row->definition, error)) {
            for (size_t j = 0; j < i; ++j) {
                frontend_unified_metadata *old = owner->metadata + j;
                (void)qa_actors_set_metadata(owner->actors, old->actor, old->old_owner, old->old_definition, NULL);
            }
            return false;
        }
    }
    return true;
}

static void metadata_rollback(frontend_remote_unified *owner)
{
    for (size_t i = 0; i < owner->metadata_count; ++i) {
        frontend_unified_metadata *row = owner->metadata + i;
        (void)qa_actors_set_metadata(owner->actors, row->actor, row->old_owner, row->old_definition, NULL);
    }
}

static bool prepare_offer(frontend_remote_unified *owner, const qa_unified_document *document,
    bool *ready, qa_error *error)
{
    if (owner->offer && !frontend_unified_document_equal(owner->offer, document))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified preparation changed its retained offer");
    if (!owner->offer && !qa_unified_document_retain(document, &owner->offer, error)) return false;
    if (!owner->preparing_recipe && !qa_executable_recipe_prepare(document, owner->options.domain.catalog,
        owner->options.domain.resources, &owner->preparing_recipe, error)) return false;
    owner->preparing = true; owner->consumers_live = true;
    bool okay = owner->options.consumers.prepare(owner->options.consumers.context,
        owner, owner->preparing_recipe, ready, error);
    if (okay) okay = frontend_remote_unified_current(owner, error) &&
        qa_executable_recipe_current(owner->preparing_recipe, owner->options.domain.catalog);
    if (okay) owner->prepared = *ready;
    return okay;
}

static bool prepare_frame(frontend_remote_unified *owner, const qa_unified_document *document,
    bool *ready, qa_error *error)
{
    if (!owner->recipe || !owner->admitted || owner->preparing)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame precedes actual world and player admission");
    const qa_unified_frame *frame = qa_unified_document_frame(document);
    const qa_unified_frame *published = qa_unified_document_frame(owner->frame);
    if (!frame || !frame->world || !frame->player || !frame->prediction || frame->epoch != owner->epoch ||
        frame->player->actor.slot != owner->wire_player.slot || frame->player->actor.generation != owner->wire_player.generation ||
        !qa_actor_id_equal(frame->prediction->actor, frame->player->actor) ||
        frame->prediction->sequence != frame->acknowledged_input ||
        frame->player->actor.registry != owner->wire_player.registry ||
        (published && frame->player->actor.registry != published->player->actor.registry))
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified frame changes its actual Source player or acknowledgement");
    if (owner->prepared_frame && !frontend_unified_document_equal(owner->prepared_frame, document))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame preparation changed its retained Source state");
    if (!owner->prepared_frame && !qa_unified_document_retain(document, &owner->prepared_frame, error)) return false;
    uint64_t number = frame->world->source.number;
    if (owner->frame && number <= owner->frame_number) { *ready = true; return true; }
    if (!frontend_remote_unified_metadata_prepare(owner,frame,error)) return false;
    if (!owner->metadata && !stage_metadata(owner, error)) return false;
    frontend_unified_frame_preparation state = FRONTEND_UNIFIED_FRAME_WAIT;
    if (!owner->options.consumers.frame(owner->options.consumers.context, owner,
        owner->prepared_frame, &state, error) || !frontend_remote_unified_current(owner, error)) return false;
    if (state > FRONTEND_UNIFIED_FRAME_OBSOLETE)
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified frame consumer returned an unknown preparation disposition");
    owner->frame_obsolete = state == FRONTEND_UNIFIED_FRAME_OBSOLETE;
    *ready = state != FRONTEND_UNIFIED_FRAME_WAIT; return true;
}

static bool transport_continue(frontend_remote_unified *owner,const qa_unified_document *document,qa_error *error)
{
    if (!owner || !owner->transport_restarted || !owner->offer) return true;
    if (!frontend_unified_document_equal(owner->offer,document)) return true;
    const qa_json_document *json=qa_unified_document_json(document);
    uint64_t wire_epoch;
    if (!qa_json_u64(json,qa_json_get(json,value(document),"epoch"),&wire_epoch,error)) return false;
    if (wire_epoch!=owner->epoch) return true;
    if (!owner->recipe || !frontend_unified_document_equal(owner->offer,document) ||
        !qa_json_string_equal(json,qa_json_get(json,value(document),"kind"),"offer") ||
        owner->epoch!=qa_executable_recipe_epoch(owner->recipe) || !linked(owner) || owner->retired ||
        !owner->options.transport_restart)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified restart lost its retained CLIENT epoch continuation");
    uint64_t runtime_epoch=qa_network_epoch(owner->options.domain.runtime,owner->options.domain.client);
    if (!runtime_epoch) return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified restart lost its actual runtime epoch");
    return owner->options.transport_restart(owner->options.context,&owner->options.domain,runtime_epoch,error);
}

static bool prepare(void *context, qa_net_client_id client, const qa_unified_document *document,
    bool *ready, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (owner && !owner->busy && ready && qa_net_client_id_equal(client,owner->options.domain.client) &&
        frontend_remote_unified_presentation_video_held(owner) && frontend_remote_unified_current(owner,error)) {
        *ready=false; return true;
    }
    if (!owner || owner->busy || !ready || !qa_net_client_id_equal(client, owner->options.domain.client) ||
        !transport_continue(owner,document,error) ||
        !frontend_remote_unified_current(owner, error)) return false;
    *ready = true; owner->busy = true;
    bool okay = true;
    if (qa_unified_document_type(document) == QA_UNIFIED_FRAME_DOCUMENT)
        okay = prepare_frame(owner, document, ready, error);
    else if (qa_unified_document_control_type(document)==QA_UNIFIED_CONTROL_OFFER)
        okay = prepare_offer(owner, document, ready, error);
    owner->busy = false; return okay;
}

static bool ready_document(frontend_remote_unified *owner, qa_unified_document **out, qa_error *error)
{
    const char *userinfo=NULL;
    if (!owner->options.userinfo(owner->options.context,&owner->options.domain,&userinfo,error) || !userinfo) return false;
    qa_unified_control value={.kind=QA_UNIFIED_CONTROL_READY,.epoch=owner->epoch,
        .value.ready={.composition=*qa_executable_recipe_generation(owner->recipe),.userinfo=(char *)userinfo}};
    return qa_unified_document_create_control(&value,NULL,out,error);
}

static bool resources(frontend_remote_unified *owner, const qa_unified_document *document, qa_error *error)
{
    const qa_unified_control *control=qa_unified_document_control(document);
    if (!control || control->kind!=QA_UNIFIED_CONTROL_RESOURCES) return false;
    for (size_t i=0;i<control->value.resources.count;++i) {
        const qa_unified_resource_state *key=&control->value.resources.values[i].resource;
        qa_launch_resource actual; qa_vfs *view; const qa_vfs_acquisition *receipt;
        if (!qa_executable_recipe_acquire_resource(owner->recipe,key->content,key->path,
                key->byte_length,&actual,&view,&receipt,error)) return false;
    }
    return true;
}

static bool control(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    uint32_t epoch, const qa_unified_document *document, qa_unified_session_commit *commit, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || owner->busy || frontend_remote_unified_presentation_video_held(owner) ||
        runtime != owner->options.domain.runtime ||
        !qa_net_client_id_equal(client, owner->options.domain.client) || !commit) return false;
    if (qa_unified_document_events(document) || qa_unified_document_metadata(document)) {
        if (!frontend_remote_unified_current(owner, error)) return false;
        if (epoch != owner->epoch) return true;
        owner->busy = true;
        bool applied = owner->recipe && owner->options.consumers.control(
            owner->options.consumers.context, owner, document, error);
        owner->busy = false; commit->applied = applied; return applied;
    }
    qa_unified_control_kind type=qa_unified_document_control_type(document);
    const qa_unified_control *packet=qa_unified_document_control(document);
    bool pending_disconnect=false;
    if (epoch!=owner->epoch && owner->offer &&
        type==QA_UNIFIED_CONTROL_DISCONNECT) {
        if (!qa_unified_session_client_disconnect_pending(owner->session,runtime,client,epoch,
            document,owner->offer,error) || !transport_continue(owner,owner->offer,error)) return false;
        pending_disconnect=true;
    }
    if (!transport_continue(owner,document,error) || !frontend_remote_unified_current(owner,error)) return false;
    owner->busy = true; bool okay = true;
    if (type==QA_UNIFIED_CONTROL_OFFER) {
        if (owner->epoch != epoch) {
            okay = owner->prepared && owner->preparing_recipe && frontend_unified_document_equal(owner->offer, document) &&
                epoch == qa_executable_recipe_epoch(owner->preparing_recipe) &&
                owner->options.consumers.offer_publish(owner->options.consumers.context, owner, owner->preparing_recipe, error);
            if (okay) {
                owner->retiring_recipe = owner->recipe; owner->recipe = owner->preparing_recipe; owner->preparing_recipe = NULL;
                owner->epoch = epoch; owner->admitted = false; owner->player = (qa_actor_id){0}; owner->transport_restarted = false;
                owner->wire_player = (qa_actor_id){0};
                frontend_remote_unified_metadata_clear(owner);
                owner->frame_number = 0; owner->preparing = owner->prepared = false;
                qa_unified_document_destroy(owner->frame); owner->frame = NULL;
                qa_unified_document_destroy(owner->prepared_frame); owner->prepared_frame = NULL;
                staged_metadata_clear(owner);
                okay = qa_actors_clear(owner->actors, error);
                if (okay) {
                    owner->identities = NULL;
                    qa_pool_reset(&owner->identity_records);
                }
            }
        }
        if (okay && owner->retiring_recipe) { okay = qa_executable_recipe_close(owner->retiring_recipe, error);
            if (okay) owner->retiring_recipe = NULL; }
        if (okay) okay = owner->options.consumers.offer_ready(owner->options.consumers.context,
            owner, owner->recipe, error);
        if (okay && !owner->transport_restarted) {
            okay = qa_network_restart(runtime, client, qa_executable_recipe_generation(owner->recipe), error);
            if (okay) owner->transport_restarted = true;
        }
        if (okay) okay=transport_continue(owner,document,error);
        if (okay) okay = ready_document(owner, &commit->reply, error);
        if (okay) { qa_unified_document_destroy(owner->offer); owner->offer = NULL; commit->applied = true; }
    } else if (epoch != owner->epoch && !pending_disconnect) okay = true;
    else if (type==QA_UNIFIED_CONTROL_ADMITTED) {
        const qa_unified_admitted_control *admitted=packet?&packet->value.admitted:NULL;
        okay=admitted && (!owner->admitted || qa_actor_id_equal(owner->wire_player,admitted->actor)) &&
            frontend_remote_unified_actor(owner,admitted->actor.slot,admitted->actor.generation,&owner->player,error);
        if (okay) {
            owner->wire_player=admitted->actor;
            owner->wire_client=(qa_saved_actor_id){.slot=admitted->client.slot,.generation=admitted->client.generation};
            owner->source_entity=admitted->source_entity; owner->admitted=true; commit->applied=true;
        }
    } else if (type==QA_UNIFIED_CONTROL_RESOURCES) {
        okay=owner->recipe && resources(owner,document,error);
        if (okay) okay=owner->options.consumers.control(owner->options.consumers.context,owner,document,error);
        commit->applied=okay;
    } else if (type==QA_UNIFIED_CONTROL_DISCONNECT) {
        okay=packet && owner->options.disconnected(owner->options.context,&owner->options.domain,packet->value.disconnect,error);
        if (okay) owner->retired=true;
        commit->applied=okay;
    } else { okay = owner->recipe && owner->options.consumers.control(owner->options.consumers.context,
        owner, document, error); commit->applied = okay; }
    owner->busy = false; return okay;
}

static bool frame(void *context, qa_network_runtime *runtime, qa_net_client_id client,
    const qa_unified_document *document, qa_unified_session_commit *commit, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || owner->busy || runtime != owner->options.domain.runtime || !commit ||
        !qa_net_client_id_equal(client, owner->options.domain.client) || !frontend_remote_unified_current(owner, error) ||
        !frontend_unified_document_equal(owner->prepared_frame, document)) return false;
    const qa_unified_frame *received = qa_unified_document_frame(document);
    if (!received || !received->world) return false;
    uint64_t number = received->world->source.number;
    int64_t acknowledged = received->acknowledged_input;
    owner->busy = true; bool okay = true;
    if (owner->frame_obsolete) {
        qa_unified_document_destroy(owner->prepared_frame); owner->prepared_frame=NULL;
        frontend_remote_unified_metadata_abort(owner);
        staged_metadata_clear(owner);
        owner->frame_obsolete=false; commit->applied=false; owner->busy=false; return true;
    }
    if (!owner->frame || number > owner->frame_number) {
        okay = metadata_apply(owner, error);
        if (okay && !owner->options.consumers.publish(owner->options.consumers.context, owner, owner->prepared_frame, error)) {
            metadata_rollback(owner); okay = false;
        }
    }
    if (okay) {
        if (!owner->frame || number > owner->frame_number) {
            frontend_remote_unified_metadata_commit(owner,received);
            qa_unified_document_destroy(owner->frame);
            owner->frame = owner->prepared_frame; owner->prepared_frame = NULL; owner->frame_number = number; }
        else { qa_unified_document_destroy(owner->prepared_frame); owner->prepared_frame = NULL;
            frontend_remote_unified_metadata_abort(owner); }
        staged_metadata_clear(owner);
        commit->applied = true; commit->acknowledged_input = acknowledged;
    }
    owner->busy = false; return okay;
}
static void closed(void *context, qa_net_client_id client)
{
    frontend_remote_unified *owner = context;
    if (owner && qa_net_client_id_equal(client, owner->options.domain.client)) {
        owner->retired=true; owner->session=NULL; owner->retirement_pending=true;
        qa_error error={0};
        if (owner->options.retirement &&
            owner->options.retirement(owner->options.context,&owner->options.domain,&error)) owner->retirement_pending=false;
    }
}
static bool transport_player(void *context, qa_net_client_id client,
    qa_unified_session_player *out, qa_error *error)
{
    frontend_remote_unified *owner = context;
    if (!owner || !out || !owner->admitted || !owner->frame || owner->retired ||
        !qa_net_client_id_equal(client, owner->options.domain.client))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified player awaits its actual admitted frame");
    const qa_recipe_provider *movement = frontend_remote_unified_provider_published(owner, QA_ROLE_MOVEMENT, "");
    const qa_recipe_provider *arsenal = frontend_remote_unified_provider_published(owner, QA_ROLE_ARSENAL, "");
    if (!movement || !movement->source_owner || !arsenal || !arsenal->selection.instance ||
        !qa_actors_get(owner->actors, owner->player))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified player lacks its received selected providers");
    if ((unsigned)movement->selection.clock.kind > QA_RULESET_Q3)
        return frontend_unified_fail(error, QA_ERROR_FORMAT, "Unified player has an unknown movement clock");
    qa_ruleset_id kind=(movement->selection.clock.kind);
    *out = (qa_unified_session_player){.actor = owner->player, .seat = owner->options.domain.seat,
        .movement = kind, .arsenal = {(const uint8_t *)arsenal->selection.instance, strlen(arsenal->selection.instance)},
        .source_owner = movement->source_owner, .source_slot = owner->source_entity};
    return true;
}
static bool events_decode(void *context,qa_bytes bytes,qa_unified_held **out,bool *ready,qa_error *error)
{
    frontend_remote_unified *owner=context;
    return owner->options.consumers.events_decode(owner->options.consumers.context,bytes,out,ready,error);
}
qa_unified_session_hooks frontend_remote_unified_hooks(frontend_remote_unified *owner)
{ return (qa_unified_session_hooks){.strings=owner->strings,.context = owner, .player = transport_player,
    .prepare = prepare, .control = control, .events_decode = owner->options.consumers.events_decode?events_decode:NULL, .frame = frame, .closed = closed}; }

bool frontend_remote_unified_submit(frontend_remote_unified *owner, const qa_usercmd *input,
    double command_time_ms, qa_error *error)
{
    if (!owner || owner->busy || !input || !isfinite(command_time_ms) || !owner->admitted || !owner->frame || !owner->session ||
        !qa_unified_session_idle(owner->session) || !frontend_remote_unified_current(owner, error)) return false;
    owner->busy = true;
    bool okay = owner->options.consumers.input(owner->options.consumers.context, owner, input, command_time_ms, error);
    owner->busy = false;
    return okay && qa_unified_session_input(owner->session, input, error);
}

static bool command_document(frontend_remote_unified *owner, const char *name,
    const qa_source_owner *component, const char *const *args, size_t count, qa_error *error)
{
    if (!owner || owner->busy || !owner->admitted || !owner->session || (count && !args) || count > 128 ||
        !qa_unified_session_idle(owner->session) || !frontend_remote_unified_current(owner, error)) return false;
    qa_unified_control value={.epoch=owner->epoch};
    if (component) {
        value.kind=QA_UNIFIED_CONTROL_COMPONENT_COMMAND;
        value.value.component_command=(qa_unified_component_command_control){.owner=*component,
            .arguments={.values=(char **)args,.count=count}};
    } else {
        value.kind=QA_UNIFIED_CONTROL_COMMAND;
        value.value.command=(qa_unified_command_control){.name=(char *)name,
            .arguments={.values=(char **)args,.count=count}};
    }
    return qa_unified_session_control_value(owner->session,&value,error);
}
bool frontend_remote_unified_source_disconnect(frontend_remote_unified *owner,const char *reason,qa_error *error)
{
    if (!owner || !reason || !frontend_remote_unified_current(owner,error)) return false;
    if (!owner->options.disconnected(owner->options.context,&owner->options.domain,reason,error)) return false;
    owner->retired=true;
    return true;
}

bool frontend_remote_unified_command_text(frontend_remote_unified *owner,const char *text,qa_error *error)
{
    if (!owner || !text || !owner->options.command_text || !frontend_remote_unified_current(owner,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified command text lacks its actual CLIENT interpreter");
    return owner->options.command_text(owner->options.context,&owner->options.domain,text,error);
}

bool frontend_remote_unified_command(frontend_remote_unified *owner, const char *name,
    const char *const *args, size_t count, qa_error *error)
{ return command_document(owner, name, NULL, args, count, error); }
bool frontend_remote_unified_component_command(frontend_remote_unified *owner,
    const qa_source_owner *component, const char *const *args, size_t count, qa_error *error)
{ return component && command_document(owner, NULL, component, args, count, error); }

bool frontend_remote_unified_begin_frame(qa_frontend *frontend,uint64_t now,uint64_t elapsed,qa_error *error)
{
    if (!frontend || now!=frontend->wall_time_ns || elapsed>now) return false;
    for (frontend_remote_unified *owner=frontend->remote_unified;owner;owner=owner->next) {
        if (owner->retired) continue;
        if (owner->busy || !frontend_remote_unified_current(owner,error) ||
            !owner->options.consumers.begin_frame)
            return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified recipient clock lacks its actual frame owner");
        if (!owner->options.consumers.begin_frame(owner->options.consumers.context,owner,now,elapsed,error)) return false;
    }
    return true;
}
bool frontend_remote_unified_clock_read(const frontend_remote_unified *owner,
    frontend_unified_recipient_clock *out,qa_error *error)
{
    if (!owner || !out || !owner->options.consumers.clock_read)
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified recipient clock has no retained producer");
    return owner->options.consumers.clock_read(owner->options.consumers.context,owner,out,error);
}
bool frontend_remote_unified_sample(qa_frontend *frontend, uint64_t now, qa_error *error)
{
    if (!frontend) return false;
    for (frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next) {
        if (owner->retired) continue;
        if (owner->busy || !frontend_remote_unified_current(owner, error)) return false;
        owner->busy = true;
        bool okay = owner->options.consumers.sample(owner->options.consumers.context, owner, now, error);
        owner->busy = false; if (!okay) return false;
    }
    return true;
}
bool frontend_remote_unified_draw(qa_frontend *frontend, uint32_t seat, float stereo,
    qa_audio_listener *listener, bool *rendered, qa_error *error)
{
    if (!frontend || !listener || !rendered || !isfinite(stereo)) return false;
    *rendered = false; frontend_remote_unified *found = NULL;
    for (frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next)
        if (!owner->retired && owner->options.domain.physical_seat == seat) {
            if (found) return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Physical seat has multiple unified replicas");
            found = owner;
        }
    if (!found) return true;
    *rendered = true;
    if (!found->frame) { *listener = (qa_audio_listener){.seat = seat, .actor = QA_AUDIO_NO_ACTOR}; return true; }
    if (found->busy || !frontend_remote_unified_current(found, error)) return false;
    found->busy = true;
    bool okay = found->options.consumers.draw(found->options.consumers.context, found, stereo, listener, error);
    found->busy = false; return okay;
}
bool frontend_remote_unified_idle(const qa_frontend *frontend)
{
    if (!frontend) return true;
    for (const frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next)
        if (owner->busy || !owner->options.consumers.idle(owner->options.consumers.context, owner)) return false;
    return true;
}
bool frontend_remote_unified_checkpoint_returned(const qa_frontend *frontend)
{
    if(!frontend)return true;
    for(const frontend_remote_unified *owner=frontend->remote_unified;owner;owner=owner->next)
        if(owner->busy || !frontend_remote_unified_metadata_returned(owner,NULL) ||
            !owner->options.consumers.checkpoint_returned ||
            !owner->options.consumers.checkpoint_returned(owner->options.consumers.context,owner))return false;
    return true;
}
size_t frontend_remote_unified_count(const qa_frontend *frontend)
{
    size_t count=0;
    if (frontend) for (const frontend_remote_unified *owner=frontend->remote_unified;owner;owner=owner->next) ++count;
    return count;
}
frontend_remote_unified *frontend_remote_unified_at(const qa_frontend *frontend,size_t index)
{
    if (frontend) for (frontend_remote_unified *owner=frontend->remote_unified;owner;owner=owner->next)
        if (!index--) return owner;
    return NULL;
}
bool frontend_remote_unified_destroy(frontend_remote_unified **slot, qa_error *error)
{
    if (!slot || !*slot) return true;
    frontend_remote_unified *owner = *slot;
    if (!linked(owner) || owner->busy ||
        (owner->frontend->resource_inventory && !owner->frontend->source_restoring) || owner->session ||
        (owner->bound && !owner->retired))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified cleanup requires its actually retired returned session");
    if (owner->retirement_pending) {
        if (!owner->options.retirement ||
            !owner->options.retirement(owner->options.context,&owner->options.domain,error)) return false;
        owner->retirement_pending=false;
    }
    if (owner->consumers_live && !owner->options.consumers.close(owner->options.consumers.context, owner, error)) return false;
    owner->consumers_live = false;
    if (owner->retiring_recipe) { if (!qa_executable_recipe_close(owner->retiring_recipe, error)) return false; owner->retiring_recipe = NULL; }
    if (owner->preparing_recipe) { if (!qa_executable_recipe_close(owner->preparing_recipe, error)) return false; owner->preparing_recipe = NULL; }
    if (owner->recipe) { if (!qa_executable_recipe_close(owner->recipe, error)) return false; owner->recipe = NULL; }
    if (!qa_actors_destroy(owner->actors, error)) return false;
    qa_strings_destroy(owner->strings); staged_metadata_clear(owner);
    frontend_remote_unified **row = &owner->frontend->remote_unified;
    while (*row != owner) row = &(*row)->next;
    *row = owner->next;
    qa_arena_destroy(&owner->identity_storage);
    qa_unified_document_destroy(owner->offer); qa_unified_document_destroy(owner->frame);
    qa_unified_document_destroy(owner->prepared_frame);
    frontend_remote_unified_metadata_clear(owner);
    qa_arena_destroy(&owner->metadata_storage);
    qa_catalog_release(owner->options.domain.catalog);
    if (owner->options.consumers.dispose) owner->options.consumers.dispose(owner->options.consumers.context);
    free(owner); *slot = NULL; return true;
}
bool frontend_remote_unified_transport_retired(frontend_remote_unified *owner,
    const qa_unified_session *session, qa_error *error)
{
    if (!owner || !linked(owner) || owner->busy || owner->session != session ||
        !session || !qa_unified_session_source_retired(session))
        return frontend_unified_fail(error, QA_ERROR_ARGUMENT, "Unified retirement still owns physical transport callbacks");
    owner->session = NULL; owner->retired = true; owner->retirement_pending=false;
    return true;
}
bool frontend_remote_unified_destroy_all(qa_frontend *frontend, qa_error *error)
{
    if (!frontend) return true;
    while (frontend->remote_unified) { frontend_remote_unified *owner = frontend->remote_unified;
        if (!frontend_remote_unified_destroy(&owner, error)) return false; }
    return true;
}
bool frontend_remote_unified_content_visit(const qa_frontend *frontend,
    const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!frontend || !visitor || !visitor->pool || !visitor->catalog || !visitor->view) return false;
    for (const frontend_remote_unified *owner = frontend->remote_unified; owner; owner = owner->next) {
        if (!visitor->pool(visitor->context, owner->options.domain.resources, error) ||
            !visitor->catalog(visitor->context, owner->options.domain.catalog, error)) return false;
        const qa_executable_recipe *recipes[] = {owner->recipe, owner->preparing_recipe, owner->retiring_recipe};
        for (size_t i = 0; i < 3; ++i) if (recipes[i] &&
            !qa_executable_recipe_content_visit(recipes[i], visitor, error)) return false;
        if (!owner->options.consumers.content_visit(owner->options.consumers.context, owner, visitor, error)) return false;
    }
    return true;
}
