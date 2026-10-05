#include "remote_unified_save.h"
#include "remote_unified_private.h"
#include "remote_unified_metadata.h"
#include "../application/unified_save_internal.h"
#include "qa/network_unified_save.h"
#include "qa/unified_frame_player.h"
#include "qa/unified_frame_prediction.h"

#include <stdlib.h>
#include <string.h>

static bool bad(qa_error *e, const char *message)
{ return frontend_unified_fail(e, QA_ERROR_FORMAT, message); }

static bool frame_valid(const frontend_remote_unified *owner,const qa_unified_document *document,
    uint64_t *number,qa_error *e)
{
    const qa_unified_frame *frame=qa_unified_document_frame(document);
    qa_actor_id actual;
    if(!frame||!frame->world||!frame->player||frame->epoch!=owner->epoch||
        !frontend_remote_unified_source_actor((frontend_remote_unified *)owner,frame,frame->player->actor,true,&actual,e)||
        !qa_actor_id_equal(actual,owner->player))return false;
    *number=frame->world->source.number;return true;
}
static bool retained_valid(const frontend_remote_unified *owner, const qa_net_client *peer, qa_error *e)
{
    if((owner->frontend->capture||owner->frontend->source_restoring)&&
        !frontend_remote_unified_metadata_returned(owner,e))return false;
    if (!owner->bound || !peer || peer->protocol.kind != QA_NET_UNIFIED_1 || peer->protocol.flags || peer->protocol.revision ||
        peer->seat_count != 1 || !peer->seats || !qa_net_client_id_equal(peer->id, owner->options.domain.client) ||
        !qa_net_client_owns_seat(peer, owner->options.domain.seat) || peer->seats[0].remote_index ||
        owner->busy || !owner->actors || !owner->strings || (owner->retirement_pending && !owner->retired) ||
        (owner->prepared && !owner->preparing) || (owner->preparing && (!owner->preparing_recipe || !owner->offer)) ||
        (owner->preparing_recipe && !owner->offer) || (owner->frame_obsolete && !owner->prepared_frame) ||
        (owner->admitted && (!owner->recipe || !owner->epoch || !qa_actors_get(owner->actors, owner->player))) ||
        (!owner->recipe && (owner->epoch || owner->frame || owner->admitted)) ||
        (owner->frame && !owner->admitted) || (owner->prepared_frame && !owner->admitted) ||
        (!owner->prepared_frame && (owner->metadata || owner->metadata_count)) ||
        (!owner->frame && owner->frame_number)) return bad(e, "Invalid retained Unified replica lifecycle");
    const qa_unified_frame_metadata *retained=frontend_remote_unified_metadata(owner);
    if(owner->source_metadata&&(!owner->admitted||!retained))
        return bad(e,"Unified replica metadata differs from its admitted Source epoch");
    for(size_t i=0;retained&&i<retained->configuration_count;++i)
        if(retained->configurations[i].actor.registry!=owner->wire_player.registry)
            return bad(e,"Unified replica metadata changed its admitted Source registry");
    const qa_executable_recipe *recipes[] = {owner->recipe, owner->preparing_recipe, owner->retiring_recipe};
    for (size_t i = 0; i < 3; ++i) if (recipes[i] && !qa_executable_recipe_current(recipes[i], owner->options.domain.catalog))
        return bad(e, "Unified replica recipe belongs to another catalog graph");
    if (owner->recipe && (qa_executable_recipe_epoch(owner->recipe) != owner->epoch ||
        (owner->transport_restarted && !qa_sha256_equal(qa_executable_recipe_digest(owner->recipe), &peer->composition))))
        return bad(e, "Unified replica installed recipe differs from its actual publication");
    if (owner->offer) {
        const qa_json_document *json = qa_unified_document_json(owner->offer);
        qa_json_id value = qa_json_get(json, qa_unified_document_root(owner->offer), "value");
        uint64_t epoch;
        if (!qa_json_string_equal(json, qa_json_get(json, value, "kind"), "offer") ||
            !qa_json_u64(json, qa_json_get(json, value, "epoch"), &epoch, e) || epoch < owner->epoch)
            return bad(e, "Unified replica lost its exact pending offer");
        const qa_executable_recipe *offered = owner->preparing_recipe ? owner->preparing_recipe :
            epoch == owner->epoch ? owner->recipe : NULL;
        if (offered) {
            qa_unified_composition canonical = {0};
            bool okay = qa_executable_recipe_epoch(offered) == epoch &&
                qa_unified_composition_create(qa_json_source(json, qa_json_get(json,
                    qa_json_get(json, value, "composition"), "composition")), &canonical, e) &&
                qa_sha256_equal(&canonical.digest, qa_executable_recipe_digest(offered));
            qa_unified_composition_free(&canonical);
            if (!okay) return bad(e, "Unified replica pending recipe changes its retained offer");
        }
    }
    if (owner->admitted) {
        qa_saved_actor_id wire;
        if (!owner->wire_player.registry || !frontend_remote_unified_wire_actor(owner, owner->player, &wire) ||
            wire.slot != owner->wire_player.slot || wire.generation != owner->wire_player.generation)
            return bad(e, "Unified replica player lost its exact private identity mapping");
    }
    uint64_t number;
    if (owner->frame && (!frame_valid(owner, owner->frame, &number, e) || number != owner->frame_number))
        return bad(e, "Unified replica published frame changes its Source clock or player");
    if (owner->prepared_frame) {
        if (!frame_valid(owner, owner->prepared_frame, &number, e)) return false;
        const qa_unified_frame *frame=qa_unified_document_frame(owner->prepared_frame);
        if(!frame->prediction||!qa_actor_id_equal(frame->prediction->actor,frame->player->actor)||
            frame->prediction->sequence!=frame->acknowledged_input)
            return bad(e,"Unified replica pending prediction differs from its received frame");
        if(owner->metadata){
            if(frame->world->actor_count!=owner->metadata_count)return false;
            for(size_t i=0;i<owner->metadata_count;++i){
                const frontend_unified_metadata *pending=owner->metadata+i;
                const qa_unified_actor_state *row=frame->world->actors+i;qa_actor_id actual;
                const char *source=qa_strings_cstr(owner->strings,pending->owner);
                const char *definition=qa_strings_cstr(owner->strings,pending->definition);
                if(!frontend_remote_unified_source_actor((frontend_remote_unified *)owner,frame,row->actor,true,&actual,e)||
                    !qa_actor_id_equal(actual,pending->actor)||!source||!definition||
                    strcmp(row->owner,source)||strcmp(row->definition,definition))return false;
            }
        }
    }
    return true;
}

bool frontend_remote_unified_restore_pending(const frontend_remote_unified *owner)
{ return owner && owner->restore_pending && !owner->busy; }
bool frontend_remote_unified_checkpoint_current(const frontend_remote_unified *owner, qa_error *e)
{
    bool linked = false;
    for (const frontend_remote_unified *row = owner && owner->frontend ? owner->frontend->remote_unified : NULL;
        row; row = row->next) if (row == owner) { linked = true; break; }
    if (!owner || !linked || owner->busy || owner->frontend->application != owner->options.domain.application ||
        (!owner->frontend->capture && !owner->frontend->source_restoring) ||
        !owner->options.current(owner->options.context, &owner->options.domain, e)) return false;
    const qa_net_client *peer = qa_net_connections_get(qa_network_connections(owner->options.domain.runtime),
        owner->options.domain.client);
    if (!retained_valid(owner, peer, e)) return false;
    if (owner->frontend->source_restoring && !owner->restore_pending) {
        qa_unified_session *installed = NULL;
        return owner->session && qa_unified_session_source_retired(owner->session) &&
            qa_unified_session_find(owner->options.domain.runtime, owner->options.domain.client, &installed, e) &&
            installed == owner->session && qa_unified_session_qualified(installed, peer, e) &&
            qa_unified_session_client_receipt(installed, owner->epoch, owner->admitted, owner->retired,
                owner->offer, owner->frame, owner->prepared_frame, e);
    }
    return !owner->restore_pending || owner->frontend->source_restoring;
}
bool frontend_remote_unified_qualified(const frontend_remote_unified *owner, qa_network_runtime *runtime,
    const qa_net_client *peer, qa_error *e)
{
    qa_unified_session *installed = NULL;
    bool linked = false;
    for (const frontend_remote_unified *row = owner && owner->frontend ? owner->frontend->remote_unified : NULL;
        row; row = row->next) if (row == owner) { linked = true; break; }
    return owner && linked && !owner->restore_pending && owner->options.domain.runtime == runtime &&
        owner->frontend->application == owner->options.domain.application &&
        owner->options.current(owner->options.context, &owner->options.domain, e) &&
        ((owner->frontend->capture || owner->frontend->source_restoring)?
            (owner->options.consumers.checkpoint_returned && owner->options.consumers.checkpoint_returned(owner->options.consumers.context, owner)):
            owner->options.consumers.idle(owner->options.consumers.context, owner)) && retained_valid(owner, peer, e) &&
        qa_unified_session_find(runtime, owner->options.domain.client, &installed, e) && installed == owner->session &&
        qa_unified_session_qualified(installed, peer, e) &&
        qa_unified_session_client_receipt(installed, owner->epoch, owner->admitted, owner->retired,
            owner->offer, owner->frame, owner->prepared_frame, e);
}
bool frontend_remote_unified_restore_dispose(frontend_remote_unified **owned, qa_error *e)
{
    frontend_remote_unified *owner = owned ? *owned : NULL;
    if (!owner) return true;
    if (!owner->restore_pending || owner->busy || (owner->session && !qa_unified_session_source_retired(owner->session)))
        return frontend_unified_fail(e, QA_ERROR_ARGUMENT, "Unified prefix cleanup still owns physical callback custody");
    if (!owner->options.consumers.close(owner->options.consumers.context, owner, e)) return false;
    owner->consumers_live = false;
    owner->session = NULL; owner->retired = true; owner->retirement_pending = false;
    return frontend_remote_unified_destroy(owned, e);
}
