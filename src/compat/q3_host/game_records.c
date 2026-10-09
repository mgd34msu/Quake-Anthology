#include "internal.h"

size_t q3_shared_offset(qa_qvm_abi abi, size_t modern)
{
    return abi == QA_QVM_Q3_MODERN ? modern : modern - (modern < 428 ? 8u : 12u);
}

bool q3_game_begin(qa_q3_host *host, q3_call *call, qa_error *error)
{
    if (!host || host->retired || !host->game || !host->memory.read)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 game records require an attached live game host");
    if (host->native && host->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Quake Live entity/player layouts are not established");
    *call = (q3_call){.host = host, .memory = host->memory, .vm = host->vm,
        .native = host->native, .native_host = host->native_host, .native_profile = host->native_profile};
    ++host->calls; return true;
}

bool q3_game_end(q3_call *call, bool ok)
{
    if (!--call->host->calls) qa_arena_reset(&call->host->scratch);
    return ok;
}

static bool indexed(uint64_t base, uint32_t stride, uint32_t number, uint64_t *out,
                       qa_error *error)
{
    uint64_t displacement = (uint64_t)stride * number;
    if (!base || displacement > INT32_MAX || base > UINT64_MAX - displacement)
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 game-data index exceeds its source table");
    *out = base + displacement; return true;
}

bool q3_game_entity_record(q3_call *call, uint32_t number, q3_record *record, qa_error *error)
{
    q3_game_data *game = call->host->game; uint64_t address;
    if (!game || number >= game->entity_count)
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 entity slot is outside located game data");
    return indexed(game->entities, game->entity_stride, number, &address, error) &&
        q3_record_open(call, address, qa_qvm_shared_entity_bytes(call->host->options.abi), record, error);
}

bool q3_game_player_record(q3_call *call, uint32_t number, q3_record *record, qa_error *error)
{
    q3_game_data *game = call->host->game; uint64_t address;
    if (!game || number >= call->host->options.server.maximum_clients)
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 player slot is outside configured game data");
    return indexed(game->clients, game->client_stride, number, &address, error) &&
        q3_record_open(call, address, game->client_stride, record, error);
}

bool q3_game_pointer_slot(q3_call *call, uint64_t address, uint32_t *out, qa_error *error)
{
    q3_game_data *game = call->host->game;
    if (!game || !game->entities || !game->entity_stride || address < game->entities ||
        (address - game->entities) % game->entity_stride)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 entity pointer is not a located source slot");
    uint64_t number = (address - game->entities) / game->entity_stride;
    if (number >= game->entity_count)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 entity pointer leaves located game data");
    *out = (uint32_t)number; return true;
}

q3_service_result q3_game_records(q3_call *call, int32_t *result, qa_error *error)
{
    (void)result;
    if (call->host->options.role != QA_QVM_GAME || call->service != 15) return Q3_UNHANDLED;
    if (call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Quake Live entity/player layouts are not established"); return Q3_FAILED;
    }
    int32_t count = q3_integer(call, 1), entity_stride = q3_integer(call, 2), client_stride = q3_integer(call, 4);
    qa_qvm_abi abi = call->host->options.abi;
    if (count < 0 || count > 1024 || entity_stride < (int32_t)qa_qvm_shared_entity_bytes(abi) ||
        client_stride < (int32_t)qa_qvm_player_bytes(abi) ||
        ((uint32_t)entity_stride & 3u) || ((uint32_t)client_stride & 3u) ||
        !call->arguments[0] || !call->arguments[3] || (call->arguments[0] & 3u) || (call->arguments[3] & 3u)) {
        q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 located game-data descriptor is invalid"); return Q3_FAILED;
    }
    uint64_t total = (uint64_t)(uint32_t)count * (uint32_t)entity_stride;
    if (total > SIZE_MAX) { q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 entity table is too large"); return Q3_FAILED; }
    q3_record entities, clients;
    if (!q3_record_open(call, call->arguments[0], (size_t)total, &entities, error) ||
        !q3_record_open(call, call->arguments[3], (size_t)client_stride, &clients, error)) return Q3_FAILED;
    q3_game_data *game = call->host->game;
    game->entities = call->arguments[0]; game->clients = call->arguments[3];
    game->entity_count = (uint32_t)count; game->entity_stride = (uint32_t)entity_stride;
    game->client_stride = (uint32_t)client_stride;
    return q3_game_fields_refresh(call->host, error) ? Q3_COMPLETED : Q3_FAILED;
}

bool qa_q3_host_game_data_read(const qa_q3_host *host, qa_q3_host_game_data *out)
{
    if (!host || host->retired || !host->game || !out) return false;
    uint64_t entities = host->game->entities, clients = host->game->clients;
    if (host->vm) {
        uint64_t base = qa_qvm_memory_size(host->vm);
        /* Host service addresses tag non-null QVM offsets with its memory size.
         * Input hooks compare against pointers stored inside the source VM. */
        if (entities) entities -= base;
        if (clients) clients -= base;
    }
    *out = (qa_q3_host_game_data){host->game->entity_count, host->game->entity_stride,
        host->options.server.maximum_clients, host->game->client_stride,
        entities, clients}; return true;
}

bool qa_q3_host_game_data_bind(qa_q3_host *host,qa_qvm *vm,const qa_q3_host_game_data *data,qa_error *error)
{
    if(!host||!vm||host->vm!=vm||host->native||host->retired||host->calls||!host->game||!data||
        data->entity_count>1024||!data->entity_count||data->entity_stride<qa_qvm_shared_entity_bytes(host->options.abi)||
        (data->entity_stride&3)||data->entities_address>UINT32_MAX||(data->entities_address&3)||
        data->client_count>64||data->client_count>host->options.server.maximum_clients||
        (data->client_count&&(data->client_stride<qa_qvm_player_bytes(host->options.abi)||(data->client_stride&3)||
            data->clients_address>UINT32_MAX||(data->clients_address&3))))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Declared component game-data layout is not its retained QVM tables");
    uint64_t entities=(uint64_t)data->entity_count*data->entity_stride,clients=(uint64_t)data->client_count*data->client_stride;
    qa_bytes span;
    if(entities>SIZE_MAX||clients>SIZE_MAX||data->entities_address>qa_qvm_memory_size(vm)||entities>qa_qvm_memory_size(vm)-data->entities_address||
        (data->client_count&&(data->clients_address>qa_qvm_memory_size(vm)||clients>qa_qvm_memory_size(vm)-data->clients_address))||
        !qa_qvm_span(vm,(int32_t)(uint32_t)data->entities_address,0,(size_t)entities,&span,error)||
        (data->client_count&&!qa_qvm_span(vm,(int32_t)(uint32_t)data->clients_address,0,(size_t)clients,&span,error))) return false;
    uint64_t tag=qa_qvm_memory_size(vm);
    host->game->entities=tag+data->entities_address;
    host->game->clients=data->client_count?tag+data->clients_address:0;
    host->game->entity_count=data->entity_count; host->game->entity_stride=data->entity_stride;
    host->game->client_stride=data->client_stride;
    return q3_game_fields_refresh(host,error);
}

bool qa_q3_host_entity(qa_q3_host *host, uint32_t number, qa_q3_entity *entity,
                         qa_qvm_entity_shared *shared, qa_error *error)
{
    q3_call call;
    if ((!entity && !shared) || !q3_game_begin(host, &call, error)) return false;
    q3_record record;
    bool ok = q3_game_entity_record(&call, number, &record, error) &&
        (!entity || qa_q3_abi_read_entity(&record.abi, 0, false, entity, error)) &&
        (!shared || qa_q3_abi_read_shared_entity(&record.abi, 0, shared, error));
    return q3_game_end(&call, ok);
}

bool qa_q3_host_player(qa_q3_host *host, uint32_t number, qa_q3_player *out, qa_error *error)
{
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    q3_record record;
    bool ok = q3_game_player_record(&call, number, &record, error) &&
              qa_q3_abi_read_player(&record.abi, 0, false, out, error);
    return q3_game_end(&call, ok);
}

bool qa_q3_host_write_player(qa_q3_host *host, uint32_t number, const qa_q3_player *value, qa_error *error)
{
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    q3_record record;
    bool ok = q3_game_player_record(&call, number, &record, error) &&
              qa_q3_abi_write_player(&record.abi, 0, false, true, value, error);
    return q3_game_end(&call, ok);
}

typedef struct player_control_write {
    q3_call *call;
    qa_actor_id actor;
    uint32_t number, stride;
    uint64_t clients, address;
} player_control_write;

static bool control_current(const player_control_write *write, qa_error *error)
{
    const qa_q3_host *host = write->call->host;
    return (!host->retired && host->game->clients == write->clients &&
        host->game->client_stride == write->stride &&
        qa_actor_id_equal(host->game->slots[write->number].actor, write->actor) &&
        qa_actors_get(qa_session_actors(host->options.session), write->actor)) ||
        q3_fail(error, QA_ERROR_NOT_FOUND, write->number, "Q3 controlled player changed during source access");
}

static bool control_word(const player_control_write *write, size_t offset, uint32_t value,
                           qa_error *error)
{
    return control_current(write, error) &&
        q3_write_word(write->call, write->address + offset, value, error) &&
        control_current(write, error);
}

static bool control_vector(const player_control_write *write, size_t offset, qa_vec3 value,
                             qa_error *error)
{
    return control_current(write, error) &&
        q3_write_vector(write->call, write->address + offset, value, error) &&
        control_current(write, error);
}

bool qa_q3_host_player_cutscene(qa_q3_host *host, qa_actor_id actor, qa_vec3 origin,
                                qa_vec3 angles, int32_t height, qa_error *error)
{
    q3_call call;
    if (!qa_vec_finite(origin) || !qa_vec_finite(angles))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 controlled view must be finite");
    if (!q3_game_begin(host, &call, error)) return false;
    uint32_t number;
    bool ok = qa_q3_host_actor_slot(host, actor, &number, error);
    if (ok && number >= host->options.server.maximum_clients)
        ok = q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 controlled actor has no client playerState");
    if (!ok) return q3_game_end(&call, false);
    player_control_write write = {.call = &call, .actor = actor, .number = number,
        .clients = host->game->clients, .stride = host->game->client_stride};
    qa_q3_usercmd command; q3_record record;
    ok = host->options.server.user_command ?
        host->options.server.user_command(host->options.server.context, number, &command, error) :
        q3_fail(error, QA_ERROR_UNSUPPORTED, number, "Q3 controlled view requires the current source command");
    if (ok) ok = control_current(&write, error) && q3_game_player_record(&call, number, &record, error);
    if (ok) {
        write.address = record.address;
        ok = control_word(&write, 4, 4, error) && control_vector(&write, 20, origin, error) &&
             control_vector(&write, 32, (qa_vec3){0}, error) &&
             control_word(&write, 68, 1023, error);
    }
    const float components[] = {angles.x, angles.y, angles.z};
    for (size_t i = 0; ok && i < 3; ++i) {
        uint32_t delta = (uint32_t)qa_angle_to_word(components[i]) - (uint32_t)command.angles[i];
        ok = control_word(&write, 56 + i * 4u, delta, error);
    }
    if (ok) ok = control_vector(&write, 152, angles, error) &&
                 control_word(&write, 164, (uint32_t)height, error);
    return q3_game_end(&call, ok);
}

bool qa_q3_host_player_cutscene_clear(qa_q3_host *host, qa_actor_id actor,
                                      int32_t prior_type, qa_error *error)
{
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    uint32_t number;
    bool ok = qa_q3_host_actor_slot(host, actor, &number, error);
    if (ok && number >= host->options.server.maximum_clients)
        ok = q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 controlled actor has no client playerState");
    if (!ok) return q3_game_end(&call, false);
    player_control_write write = {.call = &call, .actor = actor, .number = number,
        .clients = host->game->clients, .stride = host->game->client_stride};
    q3_record record;
    ok = q3_game_player_record(&call, number, &record, error);
    if (ok) { write.address = record.address; ok = control_word(&write, 4, (uint32_t)prior_type, error); }
    return q3_game_end(&call, ok);
}

static bool current(const q3_entity_slot *slot, qa_error *error)
{
    return (slot->actor.registry && qa_actors_get(qa_session_actors(slot->host->options.session), slot->actor)) ||
        q3_fail(error, QA_ERROR_ARGUMENT, slot->number, "Q3 source actor generation is retired");
}

static void reference_fields(qa_q3_host *host)
{
    qa_actor_id world = host->options.server.world_actor ?
        host->options.server.world_actor(host->options.server.context) : (qa_actor_id){0};
    const qa_actor_record *record = qa_actors_get(qa_session_actors(host->options.session), world);
    host->game->references = (qa_entity_references){
        .actors = qa_session_actor_registry(host->options.session), .owner = host->options.owner,
        .base = 0, .stride = 1, .slots = (const uint8_t *)host->game->slots,
        .capacity = 1022, .slot_stride = sizeof(q3_entity_slot),
        .actor_offset = offsetof(q3_entity_slot, actor), .foreign_owner = true,
        .invalid_is_none = true, .has_world_number = true, .world_number = 1022,
        .has_none_number = true, .none_number = 1023,
        .world = record && record->owner == host->options.owner && record->has_source ?
            qa_actor_reference_source(record->owner, record->source_slot) : qa_actor_reference_lifetime(world)};
}

static bool live_word(qa_q3_host *host, uint64_t address,
                      qa_entity_scalar_encoding encoding, qa_entity_scalar_field *out,
                      qa_error *error)
{
    qa_bytes bytes;
    bool ok = host->vm ? q3_vm_span(host->vm, address, 4, &bytes, error) :
        qa_native_borrow(host->native, address, 4, &bytes, error);
    if (ok) *out = (qa_entity_scalar_field){bytes.data, encoding};
    return ok;
}

static bool live_vector(qa_q3_host *host, uint64_t address,
                        qa_entity_vector_field *out, qa_error *error)
{
    if (host->vm) {
        qa_bytes bytes;
        if (!q3_vm_span(host->vm, address, 12, &bytes, error)) return false;
        *out = qa_entity_vector_bytes(bytes.data);
        return true;
    }
    for (size_t i = 0; i < 3; ++i) {
        qa_entity_scalar_field word;
        if (!live_word(host, address + i * 4u, QA_ENTITY_F32_LE, &word, error)) return false;
        out->word[i] = word.bytes;
    }
    return true;
}

static bool slot_fields(q3_entity_slot *slot, qa_error *error)
{
    qa_q3_host *host = slot->host; q3_game_data *game = host->game;
    qa_qvm_abi abi = host->options.abi; uint64_t entity, player;
    if (!indexed(game->entities, game->entity_stride, slot->number, &entity, error)) return false;
    qa_entity_body_fields body = {.references = &game->references};
    qa_entity_collision_fields collision = {.family = QA_COLLISION_Q3,
        .entity_number = (int32_t)slot->number, .references = &game->references};
    qa_entity_vector_field player_origin = {0};
    if (!live_vector(host, entity + q3_shared_offset(abi, 488), &body.pose[QA_ENTITY_CLIP_POSE].origin, error) ||
        !live_vector(host, entity + q3_shared_offset(abi, 500), &body.pose[QA_ENTITY_CLIP_POSE].angles, error) ||
        !live_vector(host, entity + 92, &body.pose[QA_ENTITY_CONTENTS_POSE].origin, error) ||
        !live_vector(host, entity + 116, &body.pose[QA_ENTITY_CONTENTS_POSE].angles, error) ||
        !live_vector(host, entity + q3_shared_offset(abi, 436), &body.minimum, error) ||
        !live_vector(host, entity + q3_shared_offset(abi, 448), &body.maximum, error) ||
        !live_word(host, entity + 148, QA_ENTITY_I32_LE, &body.ground, error) ||
        !live_word(host, entity + q3_shared_offset(abi, 424), QA_ENTITY_I32_LE, &collision.flags, error) ||
        !live_word(host, entity + q3_shared_offset(abi, 432), QA_ENTITY_I32_LE, &collision.brush_model, error) ||
        !live_word(host, entity + q3_shared_offset(abi, 460), QA_ENTITY_I32_LE, &collision.contents, error) ||
        !live_word(host, entity + 160, QA_ENTITY_U32_LE, &collision.model, error) ||
        !live_word(host, entity + q3_shared_offset(abi, 512), QA_ENTITY_I32_LE, &collision.owner, error)) return false;
    body.pose[QA_ENTITY_CONTROL_POSE] = body.pose[QA_ENTITY_CLIP_POSE];
    if (slot->number < host->options.server.maximum_clients) {
        if (!indexed(game->clients, game->client_stride, slot->number, &player, error) ||
            !live_vector(host, player + 20, &player_origin, error) ||
            !live_vector(host, player + 32, &body.velocity, error)) return false;
        if (slot->input_motion) body.pose[QA_ENTITY_CONTROL_POSE].origin = player_origin;
    } else if (!live_vector(host, entity + 36, &body.velocity, error)) return false;
    slot->body_fields = body; slot->collision_fields = collision;
    slot->player_origin = player_origin;
    return true;
}

static void slot_fields_clear(q3_entity_slot *slot)
{
    slot->body_fields = (qa_entity_body_fields){0};
    slot->collision_fields = (qa_entity_collision_fields){0};
    slot->player_origin = (qa_entity_vector_field){0};
}

void q3_game_fields_clear(qa_q3_host *host)
{
    if (!host->game) return;
    host->game->references = (qa_entity_references){0};
    for (size_t i = 0; i < 1024; ++i) slot_fields_clear(&host->game->slots[i]);
}

static bool storage_overlaps(uint64_t address, uint64_t bytes,
                             uint64_t base, uint64_t length)
{
    return bytes && length && (address < base ? base - address < bytes : address - base < length);
}

void q3_game_fields_changed(void *context, const qa_native_entity_event *event)
{
    qa_q3_host *host = context;
    if (!host || !host->game || host->options.role != QA_QVM_GAME ||
        event->change != QA_NATIVE_ENTITIES_INVALIDATE) return;
    if (!event->address) { q3_game_fields_clear(host); return; }
    q3_game_data *game = host->game;
    for (uint32_t i = 0; i < game->entity_count && i < 1022; ++i) {
        bool retired = storage_overlaps(event->address, event->bytes,
            game->entities + (uint64_t)i * game->entity_stride, game->entity_stride);
        if (i < host->options.server.maximum_clients)
            retired = retired || storage_overlaps(event->address, event->bytes,
                game->clients + (uint64_t)i * game->client_stride, game->client_stride);
        if (retired) slot_fields_clear(&game->slots[i]);
    }
}

bool q3_game_fields_refresh(qa_q3_host *host, qa_error *error)
{
    q3_game_fields_clear(host);
    if (!host->game || !host->game->entities) return true;
    reference_fields(host);
    for (uint32_t i = 0; i < host->game->entity_count && i < 1022; ++i) {
        q3_entity_slot *slot = &host->game->slots[i];
        if (slot->actor.registry && !slot->borrowed && !slot_fields(slot, error)) return false;
    }
    return true;
}

typedef struct body_write_scope {
    q3_call *call;
    qa_actor_id actor;
    uint32_t number, entity_stride, client_stride;
    uint64_t entities, clients, record_address;
} body_write_scope;

static bool body_write_current(const body_write_scope *scope, qa_error *error)
{
    qa_q3_host *host = scope->call->host;
    return (host->game->entities == scope->entities && host->game->clients == scope->clients &&
        host->game->entity_stride == scope->entity_stride && host->game->client_stride == scope->client_stride &&
        scope->number < host->game->entity_count &&
        qa_actor_id_equal(host->game->slots[scope->number].actor, scope->actor) &&
        qa_actors_get(qa_session_actors(host->options.session), scope->actor)) ||
        q3_fail(error, QA_ERROR_NOT_FOUND, scope->number, "Q3 body actor or table changed during source writes");
}

static bool body_write_bytes(const body_write_scope *scope, uint64_t address,
                               qa_bytes bytes, qa_error *error)
{
    return body_write_current(scope, error) && q3_write(scope->call, address, bytes, error) &&
        body_write_current(scope, error);
}

static bool body_record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    body_write_scope *scope = context;
    return body_write_bytes(scope, scope->record_address + offset, bytes, error);
}

static bool body_vector_write(const body_write_scope *scope, uint64_t address,
                                qa_vec3 vector, qa_error *error)
{
    return body_write_current(scope, error) && q3_write_vector(scope->call, address, vector, error) &&
        body_write_current(scope, error);
}

static bool body_write(void *context, const qa_body_state *body, qa_error *error)
{
    q3_entity_slot *slot = context; qa_q3_host *host = slot->host; q3_call call;
    if (!current(slot, error) || !q3_game_begin(host, &call, error)) return false;
    body_write_scope scope = {.call = &call, .actor = slot->actor, .number = slot->number,
        .entities = host->game->entities, .clients = host->game->clients,
        .entity_stride = host->game->entity_stride, .client_stride = host->game->client_stride};
    q3_record entity; bool ok = q3_game_entity_record(&call, slot->number, &entity, error) &&
        body_write_current(&scope, error);
    if (ok && slot->number < host->options.server.maximum_clients) {
        qa_q3_player player; q3_record record;
        ok = q3_game_player_record(&call, slot->number, &record, error) && body_write_current(&scope, error) &&
             qa_q3_abi_read_player(&record.abi, 0, true, &player, error);
        if (ok) {
            const float origin[] = {body->origin.x, body->origin.y, body->origin.z};
            const float velocity[] = {body->velocity.x, body->velocity.y, body->velocity.z};
            if (slot->input_motion) memcpy(player.origin, origin, sizeof(origin));
            memcpy(player.velocity, velocity, sizeof(velocity));
            scope.record_address = record.address;
            record.abi.context = &scope; record.abi.write = body_record_write;
            ok = qa_q3_abi_write_player(&record.abi, 0, true, true, &player, error);
        }
    }
    uint64_t address = ok ? entity.address : 0;
    const qa_vec3 vectors[] = {body->origin, body->angles, body->bounds.mins, body->bounds.maxs, body->velocity};
    const size_t fields[] = {q3_shared_offset(host->options.abi, 488), q3_shared_offset(host->options.abi, 500),
        q3_shared_offset(host->options.abi, 436), q3_shared_offset(host->options.abi, 448), 36};
    for (size_t i = 0; ok && i < 5; ++i)
        ok = body_vector_write(&scope, address + fields[i], vectors[i], error);
    if (ok && !slot->input_motion) {
        uint32_t ground = qa_load_i32le(entity.abi.bytes.data + 148) == -1 ? UINT32_MAX : 1023;
        if (body->ground.kind == QA_ACTOR_REFERENCE_SOURCE && body->ground.value.source.owner == host->options.owner) {
            ground = body->ground.value.source.slot;
            if (ground >= 1023) ok = q3_fail(error, QA_ERROR_ARGUMENT, ground, "Invalid physical Q3 ground number");
        } else if (qa_actor_reference_present(body->ground)) {
            qa_actor_id target = qa_actor_reference_resolve(qa_session_actors(host->options.session), body->ground);
            qa_actor_id world = host->options.server.world_actor ?
                host->options.server.world_actor(host->options.server.context) : (qa_actor_id){0};
            ok = body_write_current(&scope, error);
            if (ok && world.registry && qa_actor_id_equal(world, target)) ground = 1022;
            else if (ok) ok = qa_q3_host_actor_slot(host, target, &ground, error);
        }
        uint8_t bytes[4]; qa_store_u32le(bytes, ground);
        if (ok) ok = body_write_bytes(&scope, address + 148, (qa_bytes){bytes, sizeof(bytes)}, error);
    }
    if (ok && !slot->input_motion && slot->number < host->options.server.maximum_clients &&
        host->options.server.player_velocity)
        ok = host->options.server.player_velocity(host->options.server.context, scope.actor, body->velocity, error) &&
            body_write_current(&scope, error);
    return q3_game_end(&call, ok);
}

bool qa_q3_host_bind_actor(qa_q3_host *host, uint32_t number, qa_actor_id actor,
                             bool borrowed, qa_error *error)
{
    if (host && host->native && qa_native_unloading_owner(host->native))
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 actor projection cannot survive native module unloading");
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    const qa_actor_record *owned = qa_actors_get(qa_session_actors(host->options.session), actor);
    if (number >= 1022 || !owned || (!borrowed &&
        (owned->owner != host->options.owner || !owned->has_source || owned->source_slot != number)))
        return q3_game_end(&call, q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 actor binding ownership mismatch"));
    q3_record record;
    if (!q3_game_entity_record(&call, number, &record, error)) return q3_game_end(&call, false);
    q3_entity_slot *slot = &host->game->slots[number];
    if (slot->input_retired)
        return q3_game_end(&call, q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 source slot has a retiring input scope"));
    if (slot->actor.registry) {
        bool same = qa_actor_id_equal(slot->actor, actor) && slot->borrowed == borrowed;
        return q3_game_end(&call, same || q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 source slot is already bound"));
    }
    for (size_t i = 0; i < 1022; ++i)
        if (qa_actor_id_equal(host->game->slots[i].actor, actor))
            return q3_game_end(&call, q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 actor already has a projection"));
    slot->actor = actor; slot->borrowed = borrowed;
    bool ok = true;
    if (!borrowed) {
        qa_body_binding body = {.context = slot, .fields = &slot->body_fields, .write = body_write};
        qa_collision_binding collision = {.context = slot, .fields = &slot->collision_fields};
        ok = slot_fields(slot, error) && qa_world_body_bind(host->options.world, actor, &body, false, error);
        if (!ok) {
            slot->actor = (qa_actor_id){0}; slot->borrowed = false;
            return q3_game_end(&call, false);
        }
        ok = qa_world_collision_bind(host->options.world, actor, &collision, error);
        if (ok && host->options.server.admit_actor)
            ok = host->options.server.admit_actor(host->options.server.context, actor, error);
        if (ok && (!qa_actor_id_equal(slot->actor, actor) || !qa_actors_get(qa_session_actors(host->options.session), actor)))
            ok = q3_fail(error, QA_ERROR_NOT_FOUND, number, "Q3 actor retired during admission");
    }
    return q3_game_end(&call, ok);
}

bool q3_game_bind_restored(qa_q3_host *host, qa_error *error)
{
    if (!host->game) return true;
    if (!q3_game_fields_refresh(host, error)) return false;
    for (uint32_t i = 0; i < 1022; ++i) {
        q3_entity_slot *slot = &host->game->slots[i];
        if (!slot->actor.registry || slot->borrowed) continue;
        qa_body_binding body = {.context = slot, .fields = &slot->body_fields, .write = body_write};
        qa_collision_binding collision = {.context = slot, .fields = &slot->collision_fields};
        if (!qa_world_body_bind(host->options.world, slot->actor, &body, true, error) ||
            !qa_world_collision_bind(host->options.world, slot->actor, &collision, error)) return false;
    }
    return true;
}

bool qa_q3_host_actor(qa_q3_host *host, uint32_t number, bool create, qa_actor_id *out, qa_error *error)
{
    q3_call call;
    if (!out || !q3_game_begin(host, &call, error)) return false;
    if (number >= 1022 || number >= host->game->entity_count || host->game->slots[number].input_retired)
        return q3_game_end(&call, q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 actor slot is unavailable"));
    qa_actor_id actor = host->game->slots[number].actor;
    bool ok = true;
    if (actor.registry) ok = current(&host->game->slots[number], error);
    else if (create) {
        ok = qa_session_allocate(host->options.session, host->options.owner, host->slot_definition, true,
                                   number, &actor, error) && qa_q3_host_bind_actor(host, number, actor, false, error);
        if (!ok && actor.registry && qa_actors_get(qa_session_actors(host->options.session), actor)) {
            qa_error ignored = {0};
            qa_session_release(host->options.session, actor, &ignored);
        }
    }
    if (ok) *out = actor;
    return q3_game_end(&call, ok);
}

bool qa_q3_host_actor_slot(const qa_q3_host *host, qa_actor_id actor, uint32_t *out, qa_error *error)
{
    if (host && !host->retired && host->game && out &&
        qa_actors_get(qa_session_actors(host->options.session), actor)) {
        const qa_actor_record *source = qa_actors_get(qa_session_actors(host->options.session), actor);
        if (source->owner == host->options.owner && source->has_source && source->source_slot < 1022 &&
            qa_actor_id_equal(host->game->slots[source->source_slot].actor, actor)) {
            *out = source->source_slot; return true;
        }
        for (uint32_t i = 0; i < 1022; ++i) if (qa_actor_id_equal(host->game->slots[i].actor, actor)) {
            *out = i; return true;
        }
    }
    return q3_fail(error, QA_ERROR_ARGUMENT, 0, "shared actor has no current Q3 source projection");
}

bool qa_q3_host_actor_released(qa_q3_host *host, qa_actor_record released, qa_error *error)
{
    if (!host || host->retired) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 release requires a live host");
    if (!host->game) return true;
    if (qa_actors_get(qa_session_actors(host->options.session), released.id))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 reverse release requires an invalidated actor");
    for (uint32_t i = 0; i < 1022; ++i) {
        q3_entity_slot *slot = &host->game->slots[i];
        if (qa_actor_id_equal(slot->actor, released.id)) {
            uint32_t motion = slot->input_motion;
            *slot = (q3_entity_slot){.host = host, .number = i,
                .input_motion = motion, .input_retired = motion != 0};
        }
    }
    return true;
}

bool qa_q3_host_player_motion(qa_q3_host *host, uint32_t number, bool begin, qa_error *error)
{
    if (!host || host->retired || !host->game || number >= host->options.server.maximum_clients)
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "invalid Q3 input motion slot");
    q3_entity_slot *slot = &host->game->slots[number];
    if (begin ? slot->input_motion == UINT32_MAX || slot->input_retired : !slot->input_motion)
        return q3_fail(error, QA_ERROR_ARGUMENT, number, "unbalanced Q3 input motion scope");
    if (begin) {
        if (!slot->input_motion++ && slot->actor.registry && !slot->borrowed)
            slot->body_fields.pose[QA_ENTITY_CONTROL_POSE].origin = slot->player_origin;
    } else if (!--slot->input_motion) {
        if (slot->actor.registry && !slot->borrowed)
            slot->body_fields.pose[QA_ENTITY_CONTROL_POSE].origin = slot->body_fields.pose[QA_ENTITY_CLIP_POSE].origin;
        if (slot->input_retired && !slot->actor.registry) slot->input_retired = false;
    }
    return true;
}

bool qa_q3_host_retire_input(qa_q3_host *host, uint32_t number, bool retired, qa_error *error)
{
    q3_call call;
    if (!q3_game_begin(host, &call, error)) return false;
    if (number >= host->options.server.maximum_clients)
        return q3_game_end(&call, q3_fail(error, QA_ERROR_ARGUMENT, number, "invalid Q3 input retirement slot"));
    q3_entity_slot *slot = &host->game->slots[number];
    if (!retired && slot->input_retired && slot->input_motion && !slot->actor.registry)
        return q3_game_end(&call, q3_fail(error, QA_ERROR_ARGUMENT, number, "Q3 released input scope must drain before reuse"));
    slot->input_retired = retired;
    bool ok = true;
    if (retired && number < host->game->entity_count) {
        q3_record record;
        ok = q3_game_entity_record(&call, number, &record, error) &&
             q3_write_word(&call, record.address + q3_shared_offset(host->options.abi, 416), 0, error);
        if (ok && slot->actor.registry && !slot->borrowed)
            ok = qa_world_unlink(host->options.world, slot->actor, error);
    }
    return q3_game_end(&call, ok);
}
