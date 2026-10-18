#include "internal.h"
#include "qa/bot_runtime.h"

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
    if (!q3_script_namespace_bind(host, globals, error)) return false;
    host->options.bots = runtime;
    if (!q3_script_namespace_library(host, error)) return false;
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
    *out=(int32_t)call->host->options.bot_entity_base+source;
    return true;
}
bool q3_bot_source_entity(const q3_call *call,int32_t canonical,int32_t *out,qa_error *error)
{
    (void)error;
    if(!call->host->options.remapped_bot_namespace || canonical<0 || canonical==1022 || canonical==1023)
        { *out=canonical;return true; }
    uint32_t base=call->host->options.bot_entity_base;
    if((uint32_t)canonical>=base && (uint32_t)canonical-base<1024) {
        *out=(int32_t)((uint32_t)canonical-base);return true;
    }
    if(call->host->game) for(uint32_t i=0;i<1022;++i) {
        qa_actor_id actor=call->host->game->slots[i].actor;
        if(actor.registry && actor.slot==(uint32_t)canonical &&
           qa_actors_get(qa_session_actors(call->host->options.session),actor)) { *out=(int32_t)i;return true; }
    }
    *out=1023;return true;
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
    qa_actor_index_remove(&host->game->actor_slots, actor);
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

static bool source_argument_word(const qa_native_value *value, int32_t *out, qa_error *error)
{
    switch (value->type) {
    case QA_NATIVE_I8: *out = value->as.i8; return true;
    case QA_NATIVE_U8: *out = value->as.u8; return true;
    case QA_NATIVE_I16: *out = value->as.i16; return true;
    case QA_NATIVE_U16: *out = value->as.u16; return true;
    case QA_NATIVE_I32: *out = value->as.i32; return true;
    case QA_NATIVE_U32: memcpy(out, &value->as.u32, sizeof(*out)); return true;
    case QA_NATIVE_F32: memcpy(out, &value->as.f32, sizeof(*out)); return true;
    case QA_NATIVE_ADDRESS:
        if (value->as.address <= UINT32_MAX) {
            uint32_t word = (uint32_t)value->as.address;
            memcpy(out, &word, sizeof(*out)); return true;
        }
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Source pointer exceeds its QVM argument width");
    default:
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Source argument has no QVM word representation");
    }
}

static bool source_result_word(qa_native_value *out, int32_t word, qa_error *error)
{
    switch (out->type) {
    case QA_NATIVE_VOID: return true;
    case QA_NATIVE_I8: out->as.i8 = (int8_t)word; return true;
    case QA_NATIVE_U8: out->as.u8 = (uint8_t)word; return true;
    case QA_NATIVE_I16: out->as.i16 = (int16_t)word; return true;
    case QA_NATIVE_U16: out->as.u16 = (uint16_t)word; return true;
    case QA_NATIVE_I32: out->as.i32 = word; return true;
    case QA_NATIVE_U32: out->as.u32 = (uint32_t)word; return true;
    case QA_NATIVE_F32: memcpy(&out->as.f32, &word, sizeof(word)); return true;
    case QA_NATIVE_ADDRESS: out->as.address = (uint32_t)word; return true;
    default:
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Source result has no QVM word representation");
    }
}

bool qa_q3_host_source_call(qa_q3_host *host, const qa_qvm_call *parent, uint64_t entry,
    const qa_native_value *arguments, size_t count, qa_native_value *result, qa_error *error)
{
    if (!host || host->retired || (!host->vm && !host->native) || (count && !arguments) ||
        (parent && parent->vm != host->vm) || count > SIZE_MAX / sizeof(qa_native_type))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Source callback requires its attached executor and arguments");
    qa_native_value discarded = {.type = QA_NATIVE_VOID};
    if (!result) result = &discarded;
    q3_call call = {.host = host};
    ++host->calls;
    bool ok = true;
    if (host->vm) {
        int32_t *words = count ? qa_arena_alloc(&host->scratch, count * sizeof(*words),
            _Alignof(int32_t), error) : NULL;
        qa_native_value checked = {.type = result->type};
        ok = (!count || words) && source_result_word(&checked, 0, error) && entry <= INT32_MAX;
        if (entry > INT32_MAX)
            q3_fail(error, QA_ERROR_ARGUMENT, 0, "Source entry exceeds its QVM instruction width");
        for (size_t i = 0; ok && i < count; ++i)
            ok = source_argument_word(arguments + i, words + i, error);
        int32_t word;
        if (ok) ok = parent ? qa_qvm_invoke_source_callback(parent, qa_qvm_image_of(host->vm),
                (int32_t)(uint32_t)entry, words, count, &word, error) :
            qa_qvm_invoke(host->vm, (uint32_t)entry, words, count, &word, error);
        if (ok) ok = source_result_word(result, word, error);
    } else {
        qa_native_type *parameters = count ? qa_arena_alloc(&host->scratch,
            count * sizeof(*parameters), _Alignof(qa_native_type), error) : NULL;
        ok = (!count || parameters) && (unsigned)result->type < QA_NATIVE_BYTES;
        if ((unsigned)result->type >= QA_NATIVE_BYTES)
            q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Source scalar callback has an aggregate result");
        for (size_t i = 0; ok && i < count; ++i) {
            ok = arguments[i].type > QA_NATIVE_VOID && arguments[i].type < QA_NATIVE_BYTES;
            if (!ok) q3_fail(error, QA_ERROR_UNSUPPORTED, i, "Source scalar callback has an aggregate argument");
            else parameters[i] = (qa_native_type){.kind = arguments[i].type, .count = 1};
        }
        if (ok) {
            qa_native_signature signature = {
                .abi = qa_native_module_describe(qa_native_get_module(host->native)).image.target.abi,
                .parameters = parameters, .parameter_count = count,
                .result = {.kind = result->type, .count = 1}};
            ok = qa_native_invoke(host->native, entry, &signature, arguments, count, result, error);
        }
    }
    return q3_game_end(&call, ok);
}
