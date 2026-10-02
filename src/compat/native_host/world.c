#include "internal.h"

#include <math.h>
#include "../../world/collision/internal.h"

static void store_f32(uint8_t *out, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    qa_store_u32le(out, bits);
}

static bool actor_live(qa_native_host *host, qa_actor_id actor)
{
    return host->world.session &&
           qa_actors_get(qa_session_actor_registry(host->world.session), actor) != NULL;
}

static size_t inuse_offset(const qa_native_host *host)
{
    return host->edict->inuse;
}

static size_t minimum_edict_size(const qa_native_host *host)
{
    return host->edict->bytes;
}

static bool source_inuse(qa_native_host *host, qa_native_address address, bool *out,
                         qa_error *error)
{
    if (host->profile == QA_NATIVE_Q2_GAME_API3) {
        int32_t value;
        if (!native_host_read_i32(host, address + inuse_offset(host), &value, error))
            return false;
        *out = value != 0;
        return true;
    }
    uint8_t value;
    if (!native_host_read_u8(host, address + inuse_offset(host), &value, error))
        return false;
    *out = value != 0;
    return true;
}

static bool release_binding(qa_native_host *host, qa_native_slot_binding binding,
                            qa_error *error)
{
    if (binding.kind == QA_NATIVE_SLOT_FREE || binding.kind == QA_NATIVE_SLOT_WORLD)
        return true;
    qa_native_slot_binding cleared = {.kind = QA_NATIVE_SLOT_FREE, .slot = binding.slot};
    if (!qa_native_bind_slot(host->instance, &cleared, error))
        return false;
    if (binding.slot < host->q2_lifetime_capacity)
        host->q2_lifetimes[binding.slot] = (native_host_q2_lifetime){0};
    if (binding.slot < host->retained_capacity)
        host->retained_clients[binding.slot] = false;
    if (binding.kind == QA_NATIVE_SLOT_OWNED && actor_live(host, binding.actor)) {
        ++host->callback_depth;
        if (host->world.release_actor) {
            host->world.release_actor(host->world.binding_context, host, binding.slot,
                                      binding.actor);
        }
        bool ok = !actor_live(host, binding.actor) ||
                  qa_session_release(host->world.session, binding.actor, error);
        --host->callback_depth;
        if (!ok) return false;
    }
    return true;
}

bool qa_native_host_actor_released(qa_native_host *host, qa_actor_record released,
                                    qa_error *error)
{
    if (!host || !host->instance || !released.id.registry || actor_live(host, released.id))
        return native_host_fail(error, QA_ERROR_ARGUMENT, released.id.slot,
                                "native release notification requires an invalidated actor ID");
    qa_native_entity_table table;
    bool terminal = qa_native_terminal(host->instance);
    if (terminal && (!host->world.session || released.id.registry !=
                    qa_actors_identity(qa_session_actors(host->world.session))))
        return native_host_fail(error, QA_ERROR_ARGUMENT, released.id.slot,
                                "terminal release belongs to another canonical registry");
    if (!(terminal ? qa_native_terminal_entity_table(host->instance, &table, error)
                   : qa_native_entity_table_get(host->instance, &table, error))) return false;
    ++host->callback_depth;
    bool ok = true;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(host->instance, slot, &binding, error)) { ok = false; break; }
        if ((binding.kind != QA_NATIVE_SLOT_OWNED && binding.kind != QA_NATIVE_SLOT_BORROWED &&
             !(terminal && binding.kind == QA_NATIVE_SLOT_WORLD)) ||
            !qa_actor_id_equal(binding.actor, released.id)) continue;
        qa_native_slot_binding cleared = {.kind = QA_NATIVE_SLOT_FREE, .slot = slot};
        if (!qa_native_bind_slot(host->instance, &cleared, error)) { ok = false; break; }
        if (slot < host->q2_lifetime_capacity) host->q2_lifetimes[slot] = (native_host_q2_lifetime){0};
        if (slot < host->retained_capacity) host->retained_clients[slot] = false;
        if (binding.kind == QA_NATIVE_SLOT_OWNED && host->world.release_actor) {
            host->world.release_actor(host->world.binding_context, host, slot, released.id);
            if (!(terminal ? qa_native_terminal_entity_table(host->instance, &table, error)
                           : qa_native_entity_table_get(host->instance, &table, error))) { ok = false; break; }
        }
    }
    --host->callback_depth;
    return ok;
}

static bool source_body_address(qa_native_host *host,uint32_t slot,uint32_t velocity,
    uint32_t ground,qa_native_address *address,qa_native_slot_binding *binding,qa_error *error)
{
    qa_native_entity_table table;
    if(!host||host->kind!=NATIVE_HOST_Q2_GAME||!qa_native_entity_table_get(host->instance,&table,error)) return false;
    if(slot>=table.count||velocity>table.stride||table.stride-velocity<12||
        ground>table.stride||table.stride-ground<host->pointer_bytes||table.stride<minimum_edict_size(host))
        return native_host_fail(error,QA_ERROR_FORMAT,slot,"Native owned body fields exceed its real source row");
    if(!qa_native_slot(host->instance,slot,binding,error)||!qa_native_entity_address(host->instance,slot,address,error)) return false;
    if(binding->kind!=QA_NATIVE_SLOT_OWNED||binding->owner!=host->world.owner||binding->source_slot!=slot||!actor_live(host,binding->actor))
        return native_host_fail(error,QA_ERROR_ARGUMENT,slot,"Native body requires its actual owned full actor binding");
    return true;
}

bool qa_native_host_source_body_read(qa_native_host *host,uint32_t slot,uint32_t velocity,
    uint32_t ground,qa_body_state *out,qa_error *error)
{
    qa_native_address address;qa_native_slot_binding binding;qa_body_state body={0};uint8_t pointer[8];
    if(!out||!source_body_address(host,slot,velocity,ground,&address,&binding,error)) return false;
    if(!native_host_read_vec3(host,address+4,&body.origin,error)||
        !native_host_read_vec3(host,address+16,&body.angles,error)||
        !native_host_read_vec3(host,address+host->edict->mins,&body.bounds.mins,error)||
        !native_host_read_vec3(host,address+host->edict->maxs,&body.bounds.maxs,error)||
        !native_host_read_vec3(host,address+velocity,&body.velocity,error)||
        !native_host_read(host,address+ground,pointer,host->pointer_bytes,error)) return false;
    if(!qa_vec_finite(body.origin)||!qa_vec_finite(body.angles)||!qa_vec_finite(body.velocity)||!qa_collision_bounds_valid(body.bounds))
        return native_host_fail(error,QA_ERROR_FORMAT,slot,"Native source body has invalid authored vectors or bounds");
    qa_native_address at=host->pointer_bytes==4?qa_load_u32le(pointer):qa_load_u64le(pointer);
    if(at) {
        uint32_t other;qa_native_slot_binding target;qa_native_entity_table table;
        if(!qa_native_entity_table_get(host->instance,&table,error)||!qa_native_entity_slot(host->instance,at,&other,error)||
            !qa_native_slot(host->instance,other,&target,error)) return false;
        if(other>=table.count||target.kind==QA_NATIVE_SLOT_FREE||!actor_live(host,target.actor))
            return native_host_fail(error,QA_ERROR_ARGUMENT,other,"Native source ground has no actual bound full actor");
        body.ground=target.actor;
    }
    *out=body;return true;
}

bool qa_native_host_source_body_write(qa_native_host *host,uint32_t slot,uint32_t velocity,
    uint32_t ground,const qa_body_state *body,qa_error *error)
{
    qa_native_address address,target=0;qa_native_slot_binding binding;
    if(!body||!qa_vec_finite(body->origin)||!qa_vec_finite(body->angles)||!qa_vec_finite(body->velocity)||
        !qa_collision_bounds_valid(body->bounds)) return native_host_fail(error,QA_ERROR_ARGUMENT,slot,"Native body write has invalid canonical vectors or bounds");
    if(!source_body_address(host,slot,velocity,ground,&address,&binding,error)) return false;
    if(body->ground.registry&&!native_host_address_for_actor(host,body->ground,&target,error)) return false;
    qa_native_address current_address;qa_native_slot_binding current_binding;
    if(!source_body_address(host,slot,velocity,ground,&current_address,&current_binding,error)) return false;
    if(current_address!=address||!qa_actor_id_equal(current_binding.actor,binding.actor))
        return native_host_fail(error,QA_ERROR_ARGUMENT,slot,"Native body source changed during ground projection");
    if(host->pointer_bytes==4&&target>UINT32_MAX) return native_host_fail(error,QA_ERROR_FORMAT,slot,"Native ground exceeds its source pointer width");
    const size_t offsets[]={4,16,host->edict->mins,host->edict->maxs,velocity,ground};
    for(size_t i=0;i<sizeof(offsets)/sizeof(*offsets);++i)
        if(!qa_native_range_check(host->instance,address+offsets[i],i==5?host->pointer_bytes:12,QA_NATIVE_MEMORY_WRITE,error)) return false;
    uint8_t pointer[8];
    if(host->pointer_bytes==4) qa_store_u32le(pointer,(uint32_t)target);else qa_store_u64le(pointer,target);
    return native_host_write_vec3(host,address+4,body->origin,error)&&
        native_host_write_vec3(host,address+16,body->angles,error)&&
        native_host_write_vec3(host,address+host->edict->mins,body->bounds.mins,error)&&
        native_host_write_vec3(host,address+host->edict->maxs,body->bounds.maxs,error)&&
        native_host_write_vec3(host,address+velocity,body->velocity,error)&&
        native_host_write(host,address+ground,pointer,host->pointer_bytes,error);
}

static bool bind_world(qa_native_host *host, uint32_t slot, qa_error *error)
{
    if (!actor_live(host, host->world.world_actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "native host world actor is not live");
    qa_native_slot_binding binding = {
        .kind = QA_NATIVE_SLOT_WORLD,
        .slot = slot,
        .actor = host->world.world_actor,
        .owner = host->world.owner,
        .source_slot = slot};
    return qa_native_bind_slot(host->instance, &binding, error);
}

bool qa_native_host_detach_actor(qa_native_host *host, uint32_t slot,
                                  qa_actor_id actor, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !qa_native_host_destroy_ready(host) ||
        !host->world.world || !qa_world_idle(host->world.world))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Native actor detach requires drained source/world callbacks");
    qa_native_slot_binding binding;
    if (!qa_native_slot(host->instance, slot, &binding, error)) return false;
    if (binding.kind == QA_NATIVE_SLOT_FREE) return true;
    if (binding.kind != QA_NATIVE_SLOT_BORROWED || !qa_actor_id_equal(binding.actor, actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot, "Native actor detach differs from its borrowed full generation");
    qa_native_slot_binding cleared = {.kind = QA_NATIVE_SLOT_FREE, .slot = slot};
    if (!qa_native_bind_slot(host->instance, &cleared, error)) return false;
    if (slot < host->q2_lifetime_capacity) host->q2_lifetimes[slot] = (native_host_q2_lifetime){0};
    if (slot < host->retained_capacity) host->retained_clients[slot] = false;
    return true;
}

bool qa_native_host_world_actor_bind(qa_native_host *host, qa_actor_id actor, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !qa_native_host_destroy_ready(host) ||
        !qa_world_idle(host->world.world) || !actor_live(host, actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native world binding requires an idle live canonical actor");
    const qa_actor_record *record = qa_actors_get(qa_session_actors(host->world.session), actor);
    if (record->owner != host->world.owner || !record->has_source || record->source_slot)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native world actor differs from its owner/source world slot");
    host->world.world_actor = actor;
    if (qa_native_get_lifecycle(host->instance) == QA_NATIVE_LOADED) return true;
    return bind_world(host, 0, error);
}

bool native_host_actor_for_address(qa_native_host *host, qa_native_address address,
                                   bool observe, qa_actor_id *out, uint32_t *out_slot,
                                   qa_error *error)
{
    if (!host || !address || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native entity address and actor output are required");
    uint32_t slot;
    if (!qa_native_entity_slot(host->instance, address, &slot, error))
        return false;
    if (out_slot)
        *out_slot = slot;
    qa_native_slot_binding binding;
    if (!qa_native_slot(host->instance, slot, &binding, error))
        return false;
    if (binding.kind != QA_NATIVE_SLOT_FREE && actor_live(host, binding.actor)) {
        *out = binding.actor;
        return true;
    }
    if (binding.kind != QA_NATIVE_SLOT_FREE && !release_binding(host, binding, error))
        return false;
    if (!observe) {
        *out = (qa_actor_id){0};
        return true;
    }
    if (slot == 0) {
        if (!bind_world(host, slot, error))
            return false;
        *out = host->world.world_actor;
        return true;
    }
    bool inuse;
    if (!source_inuse(host, address, &inuse, error))
        return false;
    bool retained = slot < host->retained_capacity && host->retained_clients[slot];
    if (!inuse && !retained) {
        *out = (qa_actor_id){0};
        return true;
    }
    if (host->world.project_actor) {
        qa_actor_id projected = {0};
        bool present = false;
        if (!host->world.project_actor(host->world.binding_context, host, slot, address,
                                       &projected, &present, error))
            return false;
        if (present) {
            if (!actor_live(host, projected))
                return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                        "native projection returned a stale actor");
            qa_native_slot_binding projected_binding = {
                .kind = QA_NATIVE_SLOT_BORROWED,
                .slot = slot,
                .actor = projected,
                .owner = host->world.owner,
                .source_slot = slot};
            if (!qa_native_bind_slot(host->instance, &projected_binding, error))
                return false;
            *out = projected;
            return true;
        }
    }
    if(host->world.reserved_source_slot) {
        bool reserved=false;
        if(!host->world.reserved_source_slot(host->world.binding_context,slot,&reserved,error)) return false;
        if(reserved) { *out=(qa_actor_id){0}; return true; }
    }
    qa_actor_id actor;
    if (!qa_session_allocate(host->world.session, host->world.owner, host->world.definition,
                             true, slot, &actor, error))
        return false;
    qa_native_slot_binding owned = {
        .kind = QA_NATIVE_SLOT_OWNED,
        .slot = slot,
        .actor = actor,
        .owner = host->world.owner,
        .source_slot = slot};
    if (!qa_native_bind_slot(host->instance, &owned, error)) {
        qa_error ignored = {0};
        qa_session_release(host->world.session, actor, &ignored);
        return false;
    }
    if (host->world.bind_actor && !host->reconstruction &&
        !host->world.bind_actor(host->world.binding_context, host, slot, actor, error)) {
        qa_error ignored = {0};
        qa_session_release(host->world.session, actor, &ignored);
        return false;
    }
    qa_native_slot_binding admitted;
    if (!actor_live(host, actor) || !qa_native_slot(host->instance, slot, &admitted, error) ||
        admitted.kind != QA_NATIVE_SLOT_OWNED || !qa_actor_id_equal(admitted.actor, actor) ||
        admitted.owner != host->world.owner || admitted.source_slot != slot)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native actor changed during its actual source binding");
    *out = actor;
    return true;
}

bool native_host_address_for_actor(qa_native_host *host, qa_actor_id actor,
                                   qa_native_address *out, qa_error *error)
{
    if (!host || !out || !actor_live(host, actor))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "live actor and native address output are required");
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(host->instance, &table, error))
        return false;
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(host->instance, slot, &binding, error))
            return false;
        if (binding.kind != QA_NATIVE_SLOT_FREE && qa_actor_id_equal(binding.actor, actor))
            return qa_native_entity_address(host->instance, slot, out, error);
    }
    if (host->world.address_for_actor) {
        bool present = false;
        if (!host->world.address_for_actor(host->world.binding_context, host, actor, out,
                                           &present, error))
            return false;
        if (present)
            return true;
    }
    return native_host_fail(error, QA_ERROR_NOT_FOUND, actor.slot,
                            "actor has no native source projection");
}

bool native_host_reconcile(qa_native_host *host, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !host->world.session)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Q2 game host is required for source reconciliation");
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(host->instance, &table, error))
        return false;
    if (table.stride < minimum_edict_size(host))
        return native_host_fail(error, QA_ERROR_FORMAT, table.stride,
                                "native Q2 entity stride is smaller than its public prefix");
    for (uint32_t slot = 0; slot < table.capacity; ++slot) {
        qa_native_slot_binding binding;
        if (!qa_native_slot(host->instance, slot, &binding, error))
            return false;
        if (slot == 0) {
            if (!bind_world(host, slot, error))
                return false;
            continue;
        }
        bool in_range = slot < table.count;
        bool retained = slot < host->retained_capacity && host->retained_clients[slot];
        bool inuse = false;
        qa_native_address address;
        if (in_range &&
            (!qa_native_entity_address(host->instance, slot, &address, error) ||
             !source_inuse(host, address, &inuse, error)))
            return false;
        if (!in_range || (!inuse && !retained)) {
            if (!release_binding(host, binding, error))
                return false;
            continue;
        }
        qa_actor_id actor;
        if (!native_host_actor_for_address(host, address, true, &actor, NULL, error))
            return false;
    }
    return true;
}

static bool read_pointer(qa_native_host *host, qa_native_address address,
                         qa_native_address *out, qa_error *error)
{
    uint8_t bytes[8] = {0};
    if (!native_host_read(host, address, bytes, host->pointer_bytes, error))
        return false;
    *out = host->pointer_bytes == 4 ? qa_load_u32le(bytes) : qa_load_u64le(bytes);
    return true;
}

bool qa_native_host_source_actor(qa_native_host *host, qa_native_address address,
                                  bool observe, qa_actor_id *out, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native source actor requires an original Q2 game host");
    ++host->callback_depth;
    bool ok = native_host_actor_for_address(host, address, observe, out, NULL, error);
    --host->callback_depth;
    return ok;
}

bool qa_native_host_source_reconcile(qa_native_host *host, qa_error *error)
{
    if (!host || host->kind != NATIVE_HOST_Q2_GAME)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native source reconciliation requires an original Q2 game host");
    ++host->callback_depth;
    bool ok = native_host_reconcile(host, error);
    --host->callback_depth;
    return ok;
}

bool qa_native_host_source_active(qa_native_host *host, uint32_t slot, bool *out,
                                   qa_error *error)
{
    qa_native_entity_table table;
    qa_native_address address;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native source activity requires its actual Q2 entity table");
    if (!qa_native_entity_table_get(host->instance, &table, error)) return false;
    if (slot >= table.count)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native source activity exceeds the allocated entity rows");
    return qa_native_entity_address(host->instance, slot, &address, error) &&
           source_inuse(host, address, out, error);
}

bool qa_native_host_source_frame_begin(qa_native_host *host, qa_error *error)
{
    qa_native_entity_table table;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || host->destroying || host->restoring)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "Native source frame requires its returned original Q2 owner");
    if (!qa_native_entity_table_refresh(host->instance, &table, error)) return false;
    size_t offset = host->profile == QA_NATIVE_Q2_GAME_API3 ? 80u : 84u;
    size_t width = host->profile == QA_NATIVE_Q2_GAME_API3 ? 4u : 1u;
    if (table.stride < offset + width)
        return native_host_fail(error, QA_ERROR_FORMAT, table.stride,
                                "Native source event exceeds its actual public edict prefix");
    const uint8_t zero[4] = {0};
    ++host->callback_depth;
    bool ok = true;
    for (uint32_t slot = 1; ok && slot < table.count; ++slot) {
        qa_native_slot_binding binding;
        qa_native_address address;
        ok = qa_native_slot(host->instance, slot, &binding, error);
        if (!ok || (binding.kind != QA_NATIVE_SLOT_OWNED && binding.kind != QA_NATIVE_SLOT_BORROWED)) continue;
        if (binding.owner != host->world.owner || binding.source_slot != slot || !actor_live(host, binding.actor)) continue;
        ok = qa_native_entity_address(host->instance, slot, &address, error) &&
             native_host_write(host, address + offset, zero, width, error);
    }
    --host->callback_depth;
    return ok;
}

bool qa_native_host_source_frame_end(qa_native_host *host, qa_error *error)
{
    qa_native_entity_table table;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME ||
        !qa_native_entity_table_refresh(host->instance, &table, error)) return false;
    return qa_native_host_source_reconcile(host, error);
}

bool qa_native_host_source_birth(qa_native_host *host, qa_native_address address,
                                  qa_actor_id *out, qa_error *error)
{
    qa_native_entity_table table;
    uint32_t slot;
    qa_native_slot_binding binding;
    if (!host || host->kind != NATIVE_HOST_Q2_GAME || !out || !address ||
        !qa_native_entity_table_refresh(host->instance, &table, error) ||
        !qa_native_entity_slot(host->instance, address, &slot, error)) return false;
    if (slot >= table.count)
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native allocator returned an unallocated source row");
    if (!qa_native_slot(host->instance, slot, &binding, error)) return false;
    *out = (qa_actor_id){0};
    bool reserved = slot == 0;
    if (!reserved && host->world.reserved_source_slot &&
        !host->world.reserved_source_slot(host->world.binding_context, slot, &reserved, error)) return false;
    if (reserved || binding.kind == QA_NATIVE_SLOT_BORROWED || binding.kind == QA_NATIVE_SLOT_WORLD) return true;
    if (binding.kind == QA_NATIVE_SLOT_OWNED &&
        (binding.owner != host->world.owner || binding.source_slot != slot))
        return native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                                "Native allocation replaced another owned source namespace");
    ++host->callback_depth;
    bool ok = release_binding(host, binding, error);
    qa_native_entity_table after;
    qa_native_slot_binding vacant;
    if (ok) ok = qa_native_entity_table_refresh(host->instance, &after, error) &&
                 qa_native_slot(host->instance, slot, &vacant, error);
    if (ok && (after.base != table.base || after.stride != table.stride || slot >= after.count || vacant.kind != QA_NATIVE_SLOT_FREE))
        ok = native_host_fail(error, QA_ERROR_ARGUMENT, slot,
                              "Native allocation source changed during prior actor retirement");
    if (ok) ok = native_host_actor_for_address(host, address, true, out, NULL, error);
    --host->callback_depth;
    return ok;
}

static uint32_t pack_classic_solid(qa_bounds bounds)
{
    int32_t x = (int32_t)(bounds.maxs.x / 8.0f);
    int32_t zd = (int32_t)(-bounds.mins.z / 8.0f);
    int32_t zu = (int32_t)((bounds.maxs.z + 32.0f) / 8.0f);
    if (x < 1)
        x = 1;
    if (x > 31)
        x = 31;
    if (zd < 1)
        zd = 1;
    if (zd > 31)
        zd = 31;
    if (zu < 1)
        zu = 1;
    if (zu > 63)
        zu = 63;
    return (uint32_t)(x | (zd << 5) | (zu << 10));
}

static uint32_t solid_byte(float value, uint32_t minimum)
{
    if (value <= (float)minimum)
        return minimum;
    if (value >= 255.0f)
        return 255u;
    return (uint32_t)value;
}

static uint32_t pack_rerelease_solid(qa_bounds bounds)
{
    if (bounds.mins.x == bounds.maxs.x && bounds.mins.y == bounds.maxs.y &&
        bounds.mins.z == bounds.maxs.z)
        return 0;
    uint32_t x = solid_byte(bounds.maxs.x, 1);
    uint32_t y = solid_byte(bounds.maxs.y, 1);
    uint32_t zd = solid_byte(-bounds.mins.z, 0);
    uint32_t zu = solid_byte(bounds.maxs.z + 32.0f, 0);
    uint32_t packed = x | (y << 8) | (zd << 16) | (zu << 24);
    return packed == 31u ? 0u : packed;
}

static qa_bounds source_absolute_bounds(qa_vec3 origin, qa_vec3 angles, qa_bounds bounds,
                                        bool brush)
{
    qa_bounds absolute;
    if (brush && (angles.x != 0.0f || angles.y != 0.0f || angles.z != 0.0f)) {
        float radius = fabsf(bounds.mins.x);
        float values[] = {fabsf(bounds.mins.y), fabsf(bounds.mins.z), fabsf(bounds.maxs.x),
                          fabsf(bounds.maxs.y), fabsf(bounds.maxs.z)};
        for (size_t index = 0; index < sizeof(values) / sizeof(values[0]); ++index)
            if (values[index] > radius)
                radius = values[index];
        absolute.mins = qa_vec_sub(origin, (qa_vec3){radius, radius, radius});
        absolute.maxs = qa_vec_add(origin, (qa_vec3){radius, radius, radius});
    } else {
        absolute.mins = qa_vec_add(origin, bounds.mins);
        absolute.maxs = qa_vec_add(origin, bounds.maxs);
    }
    absolute.mins = qa_vec_sub(absolute.mins, (qa_vec3){1.0f, 1.0f, 1.0f});
    absolute.maxs = qa_vec_add(absolute.maxs, (qa_vec3){1.0f, 1.0f, 1.0f});
    return absolute;
}

static bool source_link_metadata(qa_native_host *host, qa_bounds bounds,
                                 qa_native_host_link_metadata *metadata,
                                 qa_error *error)
{
    uint32_t leaves[128];
    qa_leaf_list list;
    qa_collision_geometry *geometry = qa_world_geometry(host->world.world);
    if (!qa_collision_box_leaves(geometry, bounds, leaves,
                                 sizeof(leaves) / sizeof(leaves[0]), &list, error))
        return false;
    metadata->headnode = list.topnode;
    metadata->cluster_count = list.overflow || list.count >= 128 ? -1 : 0;
    for (size_t index = 0; index < list.count; ++index) {
        qa_collision_leaf leaf;
        if (!qa_collision_leaf_at(geometry, leaves[index], &leaf, error))
            return false;
        if (leaf.area != 0) {
            if (metadata->area != 0 && leaf.area != metadata->area)
                metadata->secondary_area = (int32_t)leaf.area;
            else
                metadata->area = (int32_t)leaf.area;
        }
        if (metadata->cluster_count < 0 || leaf.cluster < 0)
            continue;
        bool duplicate = false;
        for (int32_t cluster = 0; cluster < metadata->cluster_count; ++cluster)
            if (metadata->clusters[cluster] == (int32_t)leaf.cluster) {
                duplicate = true;
                break;
            }
        if (!duplicate) {
            if (metadata->cluster_count == 16)
                metadata->cluster_count = -1;
            else
                metadata->clusters[metadata->cluster_count++] = (int32_t)leaf.cluster;
        }
    }
    return true;
}

static bool lifetime_capacity(qa_native_host *host, uint32_t slot, qa_error *error)
{
    if (slot < host->q2_lifetime_capacity) return true;
    qa_native_entity_table table;
    if (!qa_native_entity_table_get(host->instance, &table, error)) return false;
    size_t bytes = (size_t)table.capacity * sizeof(*host->q2_lifetimes);
    if (slot >= table.capacity || bytes / sizeof(*host->q2_lifetimes) != table.capacity)
        return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 link leaves its real Source table");
    native_host_q2_lifetime *values = calloc(table.capacity, sizeof(*values));
    if (!values) return native_host_fail(error, QA_ERROR_MEMORY, slot, "Retaining native Q2 Source creation metadata");
    if (host->q2_lifetime_capacity)
        memcpy(values, host->q2_lifetimes, host->q2_lifetime_capacity * sizeof(*values));
    free(host->q2_lifetimes); host->q2_lifetimes = values; host->q2_lifetime_capacity = table.capacity;
    return true;
}

bool native_host_link(qa_native_host *host, qa_native_address address, qa_error *error)
{
    if (host && host->filter_depth)
        return native_host_fail(error, QA_ERROR_ARGUMENT, host->filter_depth,
                                "Q2 BoxEdicts filter cannot modify world links");
    qa_actor_id actor;
    uint32_t slot;
    if (!native_host_actor_for_address(host, address, true, &actor, &slot, error))
        return false;
    if (!actor.registry || slot == 0)
        return true;
    qa_native_slot_binding binding;
    if (!qa_native_slot(host->instance, slot, &binding, error))
        return false;
    bool borrowed = binding.kind == QA_NATIVE_SLOT_BORROWED;
    const native_host_edict_layout *layout = host->edict;
    qa_vec3 origin, angles, minimum, maximum;
    if (!native_host_read_vec3(host, address + 4, &origin, error) ||
        !native_host_read_vec3(host, address + 16, &angles, error) ||
        !native_host_read_vec3(host, address + layout->mins, &minimum, error) ||
        !native_host_read_vec3(host, address + layout->maxs, &maximum, error))
        return false;
    if (!qa_vec_finite(origin) || !qa_vec_finite(angles) ||
        !qa_collision_bounds_valid((qa_bounds){minimum, maximum}))
        return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 link has invalid Source origin or bounds");
    uint32_t flags, clipmask;
    int32_t link_count;
    if (!native_host_read_u32(host, address + layout->flags, &flags, error) ||
        !native_host_read_u32(host, address + layout->clipmask, &clipmask, error) ||
        !native_host_read_i32(host, address + layout->linkcount, &link_count, error))
        return false;
    if (!lifetime_capacity(host, slot, error)) return false;
    uint32_t solid;
    if (host->profile == QA_NATIVE_Q2_GAME_API3) {
        int32_t source_solid;
        if (!native_host_read_i32(host, address + layout->solid, &source_solid, error))
            return false;
        if (source_solid < 0 || source_solid > 3)
            return native_host_fail(error, QA_ERROR_FORMAT, slot,
                                    "API 3 entity has an invalid solid value");
        solid = (uint32_t)source_solid;
    } else {
        uint8_t source_solid;
        if (!native_host_read_u8(host, address + layout->solid, &source_solid, error))
            return false;
        if (source_solid > 3)
            return native_host_fail(error, QA_ERROR_FORMAT, slot,
                                    "API 2023 entity has an invalid solid value");
        solid = source_solid;
    }
    qa_body_state body = {.origin = origin, .angles = angles, .bounds = {minimum, maximum}};
    qa_error body_error = {0};
    qa_body_state existing;
    if (!borrowed && qa_world_body_read(host->world.world, actor, &existing, &body_error)) {
        body.velocity = existing.velocity;
        body.ground = existing.ground;
        if (!qa_world_body_write(host->world.world, actor, &body, error))
            return false;
    } else if (!borrowed && !qa_world_body_create(host->world.world, actor, &body, error)) {
        return false;
    }
    qa_native_address owner_pointer = 0;
    qa_actor_id owner_actor = {0};
    if (!read_pointer(host, address + layout->owner, &owner_pointer, error))
        return false;
    if (owner_pointer &&
        !native_host_actor_for_address(host, owner_pointer, false, &owner_actor, NULL, error))
        return false;
    int32_t model_index;
    if (!native_host_read_i32(host, address + 40, &model_index, error))
        return false;
    uint32_t inline_model = 0;
    if (solid == 3) {
        for (native_host_model *model = host->models; model; model = model->next)
            if (model->resource == model_index) {
                inline_model = model->inline_model;
                break;
            }
        if (!inline_model)
            return native_host_fail(error, QA_ERROR_FORMAT, slot,
                                    "native solid brush has no registered inline model");
    }
    qa_actor_collision collision = {
        .family = QA_COLLISION_Q2,
        .shape = QA_SHAPE_BOX,
        .inline_model = solid == 3,
        .model = inline_model,
        .contents = solid == 3 ? 1 : solid == 0 ||
            (solid == 1 && host->profile == QA_NATIVE_Q2_GAME_API3) ? 0
            : (flags & 2u) ? 0x04000000
            : host->profile == QA_NATIVE_Q2_GAME_API2023 && (flags & 8u) ? 0x40000000
            : host->profile == QA_NATIVE_Q2_GAME_API2023 && (flags & 128u) ? INT32_MIN
            : 0x02000000,
        .owner = owner_actor,
        .role = solid == 1 ? QA_COLLISION_TRIGGER : QA_COLLISION_SOLID,
        .monster = (flags & 4u) != 0,
        .dead_monster = (flags & 2u) != 0};
    if (!borrowed && !qa_world_set_collision(host->world.world, actor, solid ? &collision : NULL, error))
        return false;
    if (!borrowed && solid && !qa_world_link(host->world.world, actor, NULL, error))
        return false;
    if (!borrowed && !solid && !qa_world_unlink(host->world.world, actor, error))
        return false;
    qa_linked_body linked;
    if (!borrowed && solid && !qa_world_linked(host->world.world, actor, &linked))
        return native_host_fail(error, QA_ERROR_NOT_FOUND, slot,
                                "shared world did not retain a native link");
    qa_bounds absolute = !borrowed && solid ? linked.absolute_bounds
                               : source_absolute_bounds(origin, angles,
                                                        (qa_bounds){minimum, maximum}, solid == 3);
    qa_vec3 dimensions = {maximum.x - minimum.x, maximum.y - minimum.y,
                          maximum.z - minimum.z};
    qa_native_host_link_metadata metadata = {0};
    if (!source_link_metadata(host, absolute, &metadata, error))
        return false;
    metadata.network_solid = solid == 3 ? 31u
                                        : solid == 2 && !(flags & 2u)
                                              ? host->profile == QA_NATIVE_Q2_GAME_API3
                                                    ? pack_classic_solid(body.bounds)
                                                    : pack_rerelease_solid(body.bounds)
                                              : 0u;
    if (host->engine.link_metadata &&
        !host->engine.link_metadata(host->engine.context, actor, &metadata, error))
        return false;
    if (metadata.cluster_count < -1 || metadata.cluster_count > 16)
        return native_host_fail(error, QA_ERROR_FORMAT, slot,
                                "native link metadata has an invalid cluster count");
    uint32_t next_bits = (uint32_t)link_count + 1;
    int32_t next_link_count;
    memcpy(&next_link_count, &next_bits, sizeof(next_link_count));
    if (!native_host_write_vec3(host, address + layout->absmin, absolute.mins, error) ||
        !native_host_write_vec3(host, address + layout->absmax, absolute.maxs, error) ||
        !native_host_write_vec3(host, address + layout->size, dimensions, error) ||
        !native_host_write_i32(host, address + layout->area, metadata.area, error) ||
        !native_host_write_i32(host, address + layout->area2, metadata.secondary_area, error) ||
        !native_host_write_i32(host, address + layout->linkcount, next_link_count, error) ||
        !native_host_write_u32(host, address +
                                        (host->profile == QA_NATIVE_Q2_GAME_API3 ? 72u : 76u),
                               metadata.network_solid, error))
        return false;
    if (host->profile == QA_NATIVE_Q2_GAME_API3) {
        uint8_t clusters[64] = {0};
        for (int32_t index = 0; index < metadata.cluster_count; ++index)
            qa_store_u32le(clusters + (size_t)index * 4u,
                           (uint32_t)metadata.clusters[index]);
        if (!native_host_write_i32(host, address + layout->cluster_count, metadata.cluster_count, error) ||
            !native_host_write(host, address + layout->clusters, clusters, sizeof(clusters), error) ||
            !native_host_write_i32(host, address + layout->headnode, metadata.headnode, error))
            return false;
    }
    if (host->profile == QA_NATIVE_Q2_GAME_API2023 &&
        !native_host_write_u8(host, address + NATIVE_Q2_RR_LINKED, 1u, error))
        return false;
    if (link_count == 0) {
        host->q2_lifetimes[slot] = (native_host_q2_lifetime){0};
        if (host->engine.source_frame)
            host->q2_lifetimes[slot] = (native_host_q2_lifetime){.actor = actor,
                .creation_origin = origin, .creation_frame = host->engine.source_frame(host->engine.context), .present = true};
        bool copy_origin = host->profile == QA_NATIVE_Q2_GAME_API3;
        if (!copy_origin) {
            uint32_t render_effects;
            if (!native_host_read_u32(host, address + 72, &render_effects, error))
                return false;
            copy_origin = (render_effects & 128u) == 0;
        }
        if (copy_origin && !native_host_write_vec3(host, address + 28, origin, error))
            return false;
    }
    if (host->engine.source_frame) {
        native_host_q2_lifetime *lifetime = &host->q2_lifetimes[slot];
        if (!lifetime->present || !qa_actor_id_equal(lifetime->actor, actor))
            return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 link lost its physical Source creation owner");
        uint64_t frame = host->engine.source_frame(host->engine.context);
        if (frame < lifetime->creation_frame)
            return native_host_fail(error, QA_ERROR_FORMAT, slot, "Native Q2 link moved before its Source creation frame");
        lifetime->origins[frame & 7u] = (qa_native_host_q2_origin){
            .source_frame = frame, .origin = origin, .present = true};
    }
    host->q2_lifetimes[slot].actor = actor;
    host->q2_lifetimes[slot].linked = true;
    return true;
}

bool native_host_unlink(qa_native_host *host, qa_native_address address, qa_error *error)
{
    if (host && host->filter_depth)
        return native_host_fail(error, QA_ERROR_ARGUMENT, host->filter_depth,
                                "Q2 BoxEdicts filter cannot modify world links");
    qa_actor_id actor;
    uint32_t slot;
    if (!native_host_actor_for_address(host, address, false, &actor, &slot, error))
        return false;
    qa_native_slot_binding binding;
    if (!qa_native_slot(host->instance, slot, &binding, error))
        return false;
    if (binding.kind != QA_NATIVE_SLOT_BORROWED && actor.registry && slot != 0 &&
        !qa_world_unlink(host->world.world, actor, error))
        return false;
    if (host->profile == QA_NATIVE_Q2_GAME_API2023 && slot != 0 &&
        !native_host_write_u8(host, address + NATIVE_Q2_RR_LINKED, 0, error)) return false;
    if (slot < host->q2_lifetime_capacity && qa_actor_id_equal(host->q2_lifetimes[slot].actor, actor))
        host->q2_lifetimes[slot].linked = false;
    return true;
}

static native_host_surface *surface_address(qa_native_host *host,
                                            const qa_collision_surface *surface,
                                            qa_error *error)
{
    for (native_host_surface *entry = host->surfaces; entry; entry = entry->next)
        if (!memcmp(&entry->surface, surface, sizeof(*surface)))
            return entry;
    native_host_surface *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        native_host_fail(error, QA_ERROR_MEMORY, 0, "allocating native surface record");
        return NULL;
    }
    entry->surface = *surface;
    size_t size = host->profile == QA_NATIVE_Q2_GAME_API3 ? 24u : 60u;
    uint8_t bytes[60] = {0};
    size_t name_bytes = host->profile == QA_NATIVE_Q2_GAME_API3 ? 16u : 32u;
    size_t length = strlen(surface->name);
    if (length > name_bytes - 1u)
        length = name_bytes - 1u;
    memcpy(bytes, surface->name, length);
    qa_store_u32le(bytes + name_bytes, (uint32_t)surface->flags);
    qa_store_u32le(bytes + name_bytes + 4u, (uint32_t)surface->value);
    if (host->profile != QA_NATIVE_Q2_GAME_API3) {
        size_t material = strlen(surface->material);
        if (material > 15u)
            material = 15u;
        memcpy(bytes + 44, surface->material, material);
    }
    if (!qa_native_allocate(host->instance, size, INT32_MIN + 6, &entry->address, error) ||
        !native_host_write(host, entry->address, bytes, size, error)) {
        if (entry->address) {
            qa_error ignored = {0};
            qa_native_free(host->instance, entry->address, &ignored);
        }
        free(entry);
        return NULL;
    }
    entry->next = host->surfaces;
    host->surfaces = entry;
    return entry;
}

static bool trace_entity_address(qa_native_host *host, const qa_trace_result *trace,
                                 qa_native_address forced, qa_native_address *out,
                                 qa_error *error)
{
    if (forced) {
        *out = forced;
        return true;
    }
    if (trace->hit == QA_TRACE_HIT_NONE) {
        if (host->profile == QA_NATIVE_Q2_GAME_API3) {
            *out = 0;
            return true;
        }
        return qa_native_entity_address(host->instance, 0, out, error);
    }
    if (trace->hit == QA_TRACE_HIT_WORLD)
        return qa_native_entity_address(host->instance, 0, out, error);
    return native_host_address_for_actor(host, trace->actor, out, error);
}

static bool encode_trace(qa_native_host *host, const qa_trace_result *trace,
                         qa_native_address forced_entity, qa_native_value *result,
                         qa_error *error)
{
    size_t size = host->classic ? host->classic->trace.bytes : 96u;
    if (!result || result->type != QA_NATIVE_BYTES || !result->as.bytes.data ||
        result->as.bytes.size < size)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native trace result storage is invalid");
    uint8_t bytes[96] = {0};
    qa_native_address entity = 0, surface = 0, secondary = 0;
    if (!trace_entity_address(host, trace, forced_entity, &entity, error))
        return false;
    if (trace->has_surface) {
        native_host_surface *record = surface_address(host, &trace->surface, error);
        if (!record)
            return false;
        surface = record->address;
    }
    if (trace->has_secondary && trace->secondary_has_surface) {
        native_host_surface *record = surface_address(host, &trace->secondary_surface, error);
        if (!record)
            return false;
        secondary = record->address;
    }
    if (host->profile == QA_NATIVE_Q2_GAME_API3) {
        qa_store_u32le(bytes, trace->all_solid ? 1u : 0u);
        qa_store_u32le(bytes + 4, trace->start_solid ? 1u : 0u);
        store_f32(bytes + 8, trace->fraction);
        store_f32(bytes + 12, trace->end.x);
        store_f32(bytes + 16, trace->end.y);
        store_f32(bytes + 20, trace->end.z);
        store_f32(bytes + 24, trace->plane.normal.x);
        store_f32(bytes + 28, trace->plane.normal.y);
        store_f32(bytes + 32, trace->plane.normal.z);
        store_f32(bytes + 36, trace->plane.distance);
        bytes[40] = (uint8_t)trace->plane.type;
        bytes[41] = trace->plane.signbits;
        if (!native_host_store_pointer(host, bytes + host->classic->trace.surface,
                                       surface, error) ||
            !native_host_store_pointer(host, bytes + host->classic->trace.entity,
                                       entity, error))
            return false;
        qa_store_u32le(bytes + host->classic->trace.contents, (uint32_t)trace->contents);
    } else {
        bytes[0] = trace->all_solid ? 1u : 0u;
        bytes[1] = trace->start_solid ? 1u : 0u;
        store_f32(bytes + 4, trace->fraction);
        store_f32(bytes + 8, trace->end.x);
        store_f32(bytes + 12, trace->end.y);
        store_f32(bytes + 16, trace->end.z);
        store_f32(bytes + 20, trace->plane.normal.x);
        store_f32(bytes + 24, trace->plane.normal.y);
        store_f32(bytes + 28, trace->plane.normal.z);
        store_f32(bytes + 32, trace->plane.distance);
        bytes[36] = (uint8_t)trace->plane.type;
        bytes[37] = trace->plane.signbits;
        qa_store_u64le(bytes + 40, surface);
        qa_store_u32le(bytes + 48, (uint32_t)trace->contents);
        qa_store_u64le(bytes + 56, entity);
        if (trace->has_secondary) {
            store_f32(bytes + 64, trace->secondary_plane.normal.x);
            store_f32(bytes + 68, trace->secondary_plane.normal.y);
            store_f32(bytes + 72, trace->secondary_plane.normal.z);
            store_f32(bytes + 76, trace->secondary_plane.distance);
            bytes[80] = (uint8_t)trace->secondary_plane.type;
            bytes[81] = trace->secondary_plane.signbits;
            qa_store_u64le(bytes + 88, secondary);
        }
    }
    memcpy(result->as.bytes.data, bytes, size);
    result->as.bytes.size = size;
    return true;
}

bool qa_native_host_q2_trace_encode(qa_native_host *host,const qa_trace_result *trace,
    qa_buffer bytes,qa_error *error)
{
    if(!host||host->kind!=NATIVE_HOST_Q2_GAME||!host->instance||host->destroying||host->restoring||
        host->reconstruction||qa_native_terminal(host->instance)||!trace||!bytes.data)
        return native_host_fail(error,QA_ERROR_ARGUMENT,0,"Native trace encoding requires its installed live GAME owner");
    qa_native_value result={.type=QA_NATIVE_BYTES,.as.bytes={bytes.data,bytes.size}};
    return encode_trace(host,trace,0,&result,error);
}

struct qa_native_host_source_touch {
    qa_native_host *host;
    qa_native_address scratch;
    qa_native_value arguments[4];
    bool prepared;
};

bool qa_native_host_source_touch_close(qa_native_host_source_touch **slot,qa_error *error)
{
    if(!slot) return native_host_fail(error,QA_ERROR_ARGUMENT,0,"Native touch requires its retained ticket slot");
    qa_native_host_source_touch *touch=*slot;
    if(!touch) return true;
    if(touch->scratch&&!qa_native_free(touch->host->instance,touch->scratch,error)) return false;
    free(touch);*slot=NULL;return true;
}

bool qa_native_host_source_touch_prepare(qa_native_host *host,bool rerelease,
    const qa_touch_contact *contact,qa_native_host_source_touch **out,qa_error *error)
{
    if(!host||host->kind!=NATIVE_HOST_Q2_GAME||!contact||!out||*out||
        rerelease!=(host->profile==QA_NATIVE_Q2_GAME_API2023))
        return native_host_fail(error,QA_ERROR_ARGUMENT,0,"Native touch requires its actual declared Q2 ABI and empty ticket slot");
    qa_native_address self,other=0;
    if(!native_host_address_for_actor(host,contact->self,&self,error)||
        (contact->other.registry&&!native_host_address_for_actor(host,contact->other,&other,error))) return false;
    if(rerelease&&!contact->has_source_trace)
        return native_host_fail(error,QA_ERROR_ARGUMENT,0,"Rerelease source touch requires its complete shared trace");
    uint8_t bytes[96]={0};size_t size=rerelease?96u:44u;
    if(rerelease) {
        qa_native_value trace={.type=QA_NATIVE_BYTES,.as.bytes={bytes,sizeof(bytes)}};
        if(!encode_trace(host,&contact->source_trace,0,&trace,error)) return false;
    } else {
        if(contact->has_plane) {
            qa_vec3 normal=contact->plane.normal;
            if(!qa_vec_finite(normal)||!isfinite(contact->plane.distance))
                return native_host_fail(error,QA_ERROR_FORMAT,0,"Native touch plane is nonfinite");
            store_f32(bytes,normal.x);store_f32(bytes+4,normal.y);store_f32(bytes+8,normal.z);
            store_f32(bytes+12,contact->plane.distance);
            bytes[16]=normal.x==1?0:normal.y==1?1:normal.z==1?2:3;
            bytes[17]=(uint8_t)((normal.x<0?1:0)|(normal.y<0?2:0)|(normal.z<0?4:0));
        }
        if(contact->has_surface) {
            size_t length=0;
            while(length<16&&contact->surface.name[length]) ++length;
            memcpy(bytes+20,contact->surface.name,length);
            qa_store_u32le(bytes+36,(uint32_t)contact->surface.flags);
            qa_store_u32le(bytes+40,(uint32_t)contact->surface.value);
        }
    }
    qa_native_host_source_touch *touch=calloc(1,sizeof(*touch));
    if(!touch) return native_host_fail(error,QA_ERROR_MEMORY,0,"Retaining native source touch arguments");
    touch->host=host;*out=touch;
    if(!qa_native_allocate(host->instance,size,INT32_MIN+10,&touch->scratch,error)||
        !native_host_write(host,touch->scratch,bytes,size,error)) return false;
    touch->arguments[0]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=self};
    touch->arguments[1]=(qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=other};
    touch->arguments[2]=(qa_native_value){.type=QA_NATIVE_ADDRESS,
        .as.address=rerelease||contact->has_plane?touch->scratch:0};
    touch->arguments[3]=rerelease?
        (qa_native_value){.type=QA_NATIVE_U8,.as.u8=contact->inverted?1:0}:
        (qa_native_value){.type=QA_NATIVE_ADDRESS,.as.address=contact->has_surface?touch->scratch+20:0};
    touch->prepared=true;return true;
}

bool qa_native_host_source_touch_arguments(const qa_native_host_source_touch *touch,
    qa_native_value arguments[4],qa_error *error)
{
    if(!touch||!touch->prepared||!arguments)
        return native_host_fail(error,QA_ERROR_ARGUMENT,0,"Native touch arguments are not completely prepared");
    memcpy(arguments,touch->arguments,sizeof(touch->arguments));return true;
}

bool native_host_trace(qa_native_host *host, const qa_native_import_call *call,
                       qa_native_value *result, bool clip, qa_error *error)
{
    qa_vec3 start, end, mins = {0}, maxs = {0};
    size_t start_index = clip ? 1u : 0u;
    size_t mins_index = clip ? 2u : 1u;
    size_t maxs_index = clip ? 3u : 2u;
    size_t end_index = clip ? 4u : 3u;
    qa_native_address mins_address = native_argument_address(call, mins_index);
    qa_native_address maxs_address = native_argument_address(call, maxs_index);
    if (!native_host_read_vec3(host, native_argument_address(call, start_index), &start,
                               error) ||
        !native_host_read_vec3(host, native_argument_address(call, end_index), &end, error))
        return false;
    if ((mins_address == 0) != (maxs_address == 0))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native Q2 trace requires both bounds or neither");
    if (mins_address &&
        (!native_host_read_vec3(host, mins_address, &mins, error) ||
         !native_host_read_vec3(host, maxs_address, &maxs, error)))
        return false;
    qa_actor_id pass = {0};
    qa_native_address pass_address = clip ? 0 : native_argument_address(call, 4);
    if (pass_address &&
        !native_host_actor_for_address(host, pass_address, false, &pass, NULL, error))
        return false;
    uint32_t mask = host->profile == QA_NATIVE_Q2_GAME_API3
                        ? (uint32_t)native_argument_i32(call, 5)
                        : native_argument_u32(call, 5);
    qa_trace_query query = {
        .start = start,
        .end = end,
        .shape = {.kind = mins_address ? QA_SHAPE_BOX : QA_SHAPE_POINT,
                  .bounds = {mins, maxs}},
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
        .pass_actor = pass};
    query.policy.contents_mask = mask;
    qa_trace_result trace;
    qa_native_address forced_entity = 0;
    if (!clip) {
        if (!qa_world_trace(host->world.world, &query, &trace, error))
            return false;
    } else {
        forced_entity = native_argument_address(call, 0);
        qa_actor_id actor;
        uint32_t slot;
        if (!forced_entity ||
            !native_host_actor_for_address(host, forced_entity, true, &actor, &slot, error))
            return false;
        if (slot == 0) {
            if (!qa_collision_trace(qa_world_geometry(host->world.world), &query, &trace,
                                    error))
                return false;
        } else {
            qa_body_state body;
            qa_actor_collision collision;
            if (!qa_world_body_read(host->world.world, actor, &body, error)) return false;
            qa_error local = {0};
            if (!qa_world_get_collision(host->world.world, actor, &collision, &local)) {
                if (local.code != QA_OK) { if (error) *error = local; return false; }
                return native_host_fail(error, QA_ERROR_NOT_FOUND, slot,
                                        "native clip target has no shared body collision");
            }
            if (collision.inline_model) {
                query.target = (qa_collision_target){true, collision.model, body.origin,
                                                      body.angles};
                if (!qa_collision_trace(qa_world_geometry(host->world.world), &query,
                                        &trace, error))
                    return false;
            } else if (!qa_collision_trace_body(&query, collision.family,
                                                 collision.shape, body.bounds, body.origin,
                                                 qa_world_actor_contents(&collision,
                                                                         QA_COLLISION_Q2),
                                                 &trace, error)) {
                return false;
            }
        }
    }
    return encode_trace(host, &trace, forced_entity, result, error);
}

typedef struct box_visit_context {
    qa_actor_id *actors;
    size_t count, capacity;
} box_visit_context;

static qa_spatial_visit box_visit(void *context, const qa_spatial_actor *actor)
{
    box_visit_context *visit = context;
    if (visit->count < visit->capacity)
        visit->actors[visit->count] = actor->body.actor;
    ++visit->count;
    return QA_SPATIAL_CONTINUE;
}

bool native_host_box_edicts(qa_native_host *host, const qa_native_import_call *call,
                            qa_native_value *result, qa_error *error)
{
    qa_vec3 minimum, maximum;
    if (!native_host_read_vec3(host, native_argument_address(call, 0), &minimum, error) ||
        !native_host_read_vec3(host, native_argument_address(call, 1), &maximum, error))
        return false;
    int32_t classic_requested = host->profile == QA_NATIVE_Q2_GAME_API3
                                    ? native_argument_i32(call, 3)
                                    : 0;
    if (classic_requested < 0)
        return native_host_fail(error, QA_ERROR_ARGUMENT, (size_t)classic_requested,
                                "native BoxEdicts capacity is negative");
    uint64_t requested = host->profile == QA_NATIVE_Q2_GAME_API3
                             ? (uint32_t)classic_requested
                             : call->arguments[3].as.u64;
    if (requested > 1048576u)
        return native_host_fail(error, QA_ERROR_ARGUMENT, (size_t)requested,
                                "native BoxEdicts capacity is unreasonable");
    qa_native_address output = native_argument_address(call, 2);
    if (requested && !output)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native BoxEdicts output is null with nonzero capacity");
    size_t candidate_capacity = qa_actors_count(qa_world_actors(host->world.world));
    qa_actor_id *actors = candidate_capacity
                              ? calloc(candidate_capacity, sizeof(*actors))
                              : NULL;
    if (candidate_capacity && !actors)
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating native BoxEdicts candidates");
    int32_t area = native_argument_i32(call, 4);
    if (area != 1 && area != 2) {
        free(actors);
        return native_host_fail(error, QA_ERROR_ARGUMENT, (size_t)(uint32_t)area,
                                "native BoxEdicts area is invalid");
    }
    box_visit_context visit = {.actors = actors, .capacity = candidate_capacity};
    bool ok = qa_world_visit(host->world.world, (qa_bounds){minimum, maximum},
                             area == 1 ? QA_COLLISION_SOLID : QA_COLLISION_TRIGGER,
                             box_visit, &visit, error);
    size_t accepted = 0;
    qa_native_address filter = host->profile == QA_NATIVE_Q2_GAME_API2023
                                   ? native_argument_address(call, 5)
                                   : 0;
    qa_native_address filter_data = host->profile == QA_NATIVE_Q2_GAME_API2023
                                        ? native_argument_address(call, 6)
                                        : 0;
    qa_native_module_info info = qa_native_module_describe(qa_native_get_module(host->instance));
    qa_native_type parameters[] = {{.kind = QA_NATIVE_ADDRESS, .count = 1},
                                   {.kind = QA_NATIVE_ADDRESS, .count = 1}};
    qa_native_signature signature = {
        .abi = info.image.target.abi,
        .parameters = parameters,
        .parameter_count = 2,
        .result = {.kind = QA_NATIVE_I32, .count = 1}};
    if (ok) {
        size_t count = visit.count < candidate_capacity ? visit.count : candidate_capacity;
        for (size_t index = 0;
             index < count && !(host->profile == QA_NATIVE_Q2_GAME_API3 && !requested);
             ++index) {
            qa_native_address address;
            if (!native_host_address_for_actor(host, actors[index], &address, error)) {
                ok = false;
                break;
            }
            int32_t decision = 0;
            if (filter) {
                qa_native_value arguments[] = {
                    {.type = QA_NATIVE_ADDRESS, .as.address = address},
                    {.type = QA_NATIVE_ADDRESS, .as.address = filter_data}};
                qa_native_value filter_result = {.type = QA_NATIVE_I32};
                ++host->filter_depth;
                bool invoked = qa_native_invoke(host->instance, filter, &signature,
                                                arguments, 2, &filter_result, error);
                --host->filter_depth;
                if (!invoked) {
                    ok = false;
                    break;
                }
                decision = filter_result.as.i32;
                if (decision & ~65) {
                    ok = native_host_fail(error, QA_ERROR_FORMAT, (size_t)(uint32_t)decision,
                                          "native BoxEdicts filter returned invalid flags");
                    break;
                }
            }
            if (!(decision & 1)) {
                uint8_t pointer[8] = {0};
                size_t width = info.image.target.pointer_bytes;
                if (width == 4) {
                    if (address > UINT32_MAX) {
                        ok = native_host_fail(error, QA_ERROR_FORMAT, 0,
                                              "BoxEdicts pointer exceeds guest width");
                        break;
                    }
                    qa_store_u32le(pointer, (uint32_t)address);
                } else {
                    qa_store_u64le(pointer, address);
                }
                if (!requested && host->profile == QA_NATIVE_Q2_GAME_API2023) {
                    ++accepted;
                } else if (accepted < requested) {
                    if (!native_host_write(host, output + accepted * width, pointer, width,
                                           error)) {
                        ok = false;
                        break;
                    }
                    ++accepted;
                }
            }
            if (decision & 64)
                break;
            if (host->profile == QA_NATIVE_Q2_GAME_API3 && accepted == requested)
                break;
        }
    }
    free(actors);
    if (!ok)
        return false;
    if (host->profile == QA_NATIVE_Q2_GAME_API3)
        result->as.i32 = (int32_t)accepted;
    else
        result->as.u64 = accepted;
    return true;
}
