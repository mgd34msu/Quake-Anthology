#include "internal.h"

bool qa_q3_host_source_input(const qa_q3_host *host, qa_input_seat **seat, uint64_t *owner)
{
    if (!host || host->retired || !seat || !owner || !host->options.seat) return false;
    *seat = host->options.seat;
    *owner = host->options.input_owner ? host->options.input_owner : host->options.service_owner;
    return true;
}

bool qa_q3_host_attach_bots(qa_q3_host *host, qa_bot_runtime *runtime,
                           uint32_t client_base, uint32_t entity_base,
                           bool remapped_namespace, bool shared_lifetime, qa_error *error)
{
    if (!host || host->retired || !runtime || host->calls ||
        (host->vm && qa_qvm_active(host->vm)) || (host->native && !qa_native_can_destroy(host->native)))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 bot library attach requires an idle live host");
    if (host->options.bots && host->options.bots != runtime)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 host already borrows another bot library");
    if(client_base>INT32_MAX-64 || entity_base>INT32_MAX-1024)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 bot namespace exceeds source signed range");
    qa_script_defines *globals=qa_bot_runtime_global_defines(runtime);
    if(!globals || (host->options.script_globals && host->options.script_globals!=globals))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 host global definitions differ from the actual bot library owner");
    host->options.bots = runtime;
    host->options.script_globals=globals;
    host->options.bot_client_base = client_base;
    host->options.bot_entity_base = entity_base;
    host->options.remapped_bot_namespace = remapped_namespace;
    host->options.shared_bot_lifetime = shared_lifetime;
    return true;
}

bool q3_bot_client_number(const q3_call *call,int32_t source,int32_t *out,qa_error *error)
{
    if(!call->host->options.remapped_bot_namespace) { *out=source;return true; }
    if(source<0 || (uint32_t)source>=call->host->options.server.maximum_clients)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 bot source client is outside its reserved range");
    *out=(int32_t)call->host->options.bot_client_base+source;return true;
}
qa_bot_runtime *q3_bot_runtime(const q3_call *call)
{ return call->host->bots_shutdown?NULL:call->host->options.bots; }
bool q3_bot_entity_number(const q3_call *call,int32_t source,int32_t *out,qa_error *error)
{
    if(!call->host->options.remapped_bot_namespace) { *out=source;return true; }
    if(source<0 || source>=1024)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 bot source entity is outside its reserved range");
    qa_actor_id actor=call->host->game?call->host->game->slots[source].actor:(qa_actor_id){0};
    if(actor.registry && qa_actors_get(qa_session_actors(call->host->options.session),actor))
        *out=(int32_t)actor.slot;
    else *out=(int32_t)call->host->options.bot_entity_base+source;
    return true;
}
bool q3_bot_source_entity(const q3_call *call,int32_t canonical,int32_t *out,qa_error *error)
{
    if(!call->host->options.remapped_bot_namespace || canonical<0) { *out=canonical;return true; }
    uint32_t base=call->host->options.bot_entity_base;
    if((uint32_t)canonical>=base && (uint32_t)canonical-base<1024) {
        *out=(int32_t)((uint32_t)canonical-base);return true;
    }
    if(call->host->game) for(uint32_t i=0;i<1022;++i) {
        qa_actor_id actor=call->host->game->slots[i].actor;
        if(actor.registry && actor.slot==(uint32_t)canonical &&
           qa_actors_get(qa_session_actors(call->host->options.session),actor)) { *out=(int32_t)i;return true; }
    }
    return q3_fail(error,QA_ERROR_NOT_FOUND,0,"canonical bot entity has no generation-matched source projection");
}

bool qa_q3_host_detach_actor(qa_q3_host *host, uint32_t number, qa_actor_id actor,
                            qa_error *error)
{
    if (!host || host->retired || !host->game || number >= 1022 || host->calls ||
        (host->vm && qa_qvm_active(host->vm)) ||
        (host->native && !qa_native_can_destroy(host->native)) || !qa_world_idle(host->options.world))
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 projection detach requires idle source and world callbacks");
    q3_entity_slot *slot = &host->game->slots[number];
    if (!slot->borrowed || !qa_actor_id_equal(slot->actor, actor) ||
        !qa_actors_get(qa_session_actors(host->options.session), actor))
        return q3_fail(error, QA_ERROR_NOT_FOUND, number, "Q3 borrowed projection generation changed");
    if (slot->input_motion)
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 borrowed projection still owns input motion");
    *slot = (q3_entity_slot){.host = host, .number = number};
    return true;
}

bool qa_q3_host_input_idle(const qa_q3_host *host, uint32_t slot)
{
    return host && host->game && slot < host->options.server.maximum_clients &&
        !host->game->slots[slot].input_motion;
}

bool qa_q3_host_source_player(qa_q3_host *host, uint32_t slot, qa_q3_player *out,
                              qa_error *error)
{
    if (!out) return q3_fail(error, QA_ERROR_ARGUMENT, slot, "Q3 source player output is absent");
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    q3_record record;
    bool ok = q3_game_player_record(&call, slot, &record, error) &&
        qa_q3_abi_read_player(&record.abi, 0, true, out, error);
    return q3_game_end(&call, ok);
}
