#include "internal.h"
#include "guest/internal.h"

bool qa_native_range_check(const qa_native_instance *instance, qa_native_address address,
    size_t bytes, uint32_t permissions, qa_error *error) {
    if (!instance || (instance->destroying && !qa_native_unloading_owner(instance)) || !permissions || permissions > 7)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native range proof requires its live owner and actual permission bits");
    if (instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS)
        return guest_ready(instance->guest, error) && guest_range(instance->guest, address, bytes, permissions, error);
    return instance->backend == QA_NATIVE_BACKEND_DIRECT ?
        native_direct_range_check(address, bytes, permissions, error) :
        native_runner_range_check((qa_native_instance *)instance, address, bytes, permissions, error);
}

bool qa_native_read(const qa_native_instance *instance, qa_native_address source, void *out,
                    size_t bytes, qa_error *error) {
    if (!instance || (instance->destroying && !qa_native_unloading_owner(instance)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "live native instance is required for memory reads");
    if (instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS)
        return qa_native_guest_read(instance->guest, source, out, bytes, error);
    return instance->backend == QA_NATIVE_BACKEND_DIRECT
               ? native_direct_read(source, out, bytes, error)
               : native_runner_read((qa_native_instance *)instance, source, out, bytes, error);
}

bool qa_native_write(qa_native_instance *instance, qa_native_address destination, qa_bytes bytes,
                     qa_error *error) {
    if (!instance || (instance->destroying && !qa_native_unloading_owner(instance)) || (!bytes.data && bytes.size))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "live native instance and bytes are required for memory writes");
    if (instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS)
        return qa_native_guest_write(instance->guest, destination, bytes, error);
    return instance->backend == QA_NATIVE_BACKEND_DIRECT
               ? native_direct_write(destination, bytes.data, bytes.size, error)
               : native_runner_write(instance, destination, bytes, error);
}

static bool grow_string(uint8_t **data, size_t *capacity, size_t required, qa_error *error) {
    if (required <= *capacity)
        return true;
    size_t next = *capacity ? *capacity : 256u;
    while (next < required) {
        if (next > SIZE_MAX / 2u) {
            next = required;
            break;
        }
        next *= 2u;
    }
    uint8_t *grown = realloc(*data, next);
    if (!grown)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native string copy");
    *data = grown;
    *capacity = next;
    return true;
}

bool qa_native_read_string(const qa_native_instance *instance, qa_native_address source,
                           size_t maximum, qa_buffer *out, qa_error *error) {
    if (!instance || !out || !source || !maximum)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native string address, limit and output are required");
    if (maximum > UINT64_MAX - source)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native string address range overflows");
    uint8_t *copy = NULL;
    size_t used = 0, capacity = 0;
    while (used < maximum) {
        uint8_t block[256];
        size_t amount = maximum - used;
        if (amount > sizeof(block))
            amount = sizeof(block);
        qa_error block_error = {0};
        if (qa_native_read(instance, source + used, block, amount, &block_error)) {
            const uint8_t *end = memchr(block, 0, amount);
            size_t content = end ? (size_t)(end - block) : amount;
            if (!grow_string(&copy, &capacity, used + content + 1u, error)) {
                free(copy);
                return false;
            }
            memcpy(copy + used, block, content);
            used += content;
            if (end) {
                copy[used] = 0;
                *out = (qa_buffer){copy, used};
                return true;
            }
            continue;
        }
        for (size_t index = 0; index < amount; ++index) {
            uint8_t byte;
            if (!qa_native_read(instance, source + used, &byte, 1, error)) {
                free(copy);
                return false;
            }
            if (!grow_string(&copy, &capacity, used + 2u, error)) {
                free(copy);
                return false;
            }
            if (!byte) {
                copy[used] = 0;
                *out = (qa_buffer){copy, used};
                return true;
            }
            copy[used++] = byte;
        }
    }
    free(copy);
    return native_fail(error, QA_ERROR_FORMAT, maximum,
                       "native string has no terminator within its limit");
}

bool qa_native_allocate(qa_native_instance *instance, size_t bytes, int32_t tag,
                        qa_native_address *out, qa_error *error) {
    if (!instance || !out || (instance->destroying && !qa_native_unloading_owner(instance)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "live native instance and allocation output are required");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER)
        return native_runner_allocate(instance, bytes, tag, out, error);
    size_t amount = bytes ? bytes : 1u;
    native_allocation *allocation = calloc(1, sizeof(*allocation));
    if (instance->backend == QA_NATIVE_BACKEND_OWNED_PROCESS) {
        if (!allocation) return native_fail(error, QA_ERROR_MEMORY, 0, "owning native tagged allocation receipt");
        if (!qa_native_guest_allocate(instance->guest, amount, tag, &allocation->guest_address, error)) {
            free(allocation); return false;
        }
        allocation->size = amount; allocation->tag = tag;
        allocation->next = instance->allocations; instance->allocations = allocation;
        *out = allocation->guest_address; return true;
    }
    void *memory = calloc(1, amount);
    if (!allocation || !memory) {
        free(allocation);
        free(memory);
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating tagged native module memory");
    }
    allocation->bytes = memory;
    allocation->size = amount;
    allocation->tag = tag;
    allocation->next = instance->allocations;
    instance->allocations = allocation;
    *out = (qa_native_address)(uintptr_t)memory;
    return true;
}

bool qa_native_allocation_query(const qa_native_instance *instance, qa_native_address address,
                                qa_native_allocation_info *out, qa_error *error) {
    if (!instance || !address || !out || instance->destroying || instance->unloading)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "live native allocation and output are required");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER)
        return native_runner_allocation_query((qa_native_instance *)instance, address, out, error);
    for (const native_allocation *allocation = instance->allocations; allocation; allocation = allocation->next) {
        qa_native_address base = allocation->guest_address ? allocation->guest_address :
            (qa_native_address)(uintptr_t)allocation->bytes;
        if (address >= base && address - base < allocation->size) {
            *out = (qa_native_allocation_info){base, allocation->size, allocation->tag};
            return true;
        }
    }
    return native_fail(error, QA_ERROR_UNSUPPORTED, 0, "native private pointer is not in an owned tagged allocation");
}

bool qa_native_free(qa_native_instance *instance, qa_native_address address, qa_error *error) {
    if (!instance || (instance->destroying && !qa_native_unloading_owner(instance)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "live native instance is required for tagged free");
    if (!address)
        return true;
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER)
        return native_runner_free(instance, address, error);
    native_allocation **link = &instance->allocations;
    while (*link && ((*link)->guest_address ? (*link)->guest_address :
        (qa_native_address)(uintptr_t)(*link)->bytes) != address)
        link = &(*link)->next;
    if (!*link)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native TagFree address is unowned or already freed");
    native_allocation *allocation = *link;
    if (allocation->guest_address && !qa_native_guest_free(instance->guest, address, error)) return false;
    *link = allocation->next;
    if (!allocation->guest_address) free(allocation->bytes);
    free(allocation);
    return true;
}

void qa_native_free_tag(qa_native_instance *instance, int32_t tag) {
    if (!instance || (instance->destroying && !qa_native_unloading_owner(instance)))
        return;
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER) {
        qa_error ignored = {0};
        native_runner_free_tag(instance, tag, &ignored);
        return;
    }
    native_allocation **link = &instance->allocations;
    while (*link) {
        native_allocation *allocation = *link;
        if (allocation->tag != tag) {
            link = &allocation->next;
            continue;
        }
        if (allocation->guest_address) {
            qa_error failure = {0};
            if (!qa_native_guest_free(instance->guest, allocation->guest_address, &failure)) {
                native_latch_error(instance, &failure); return;
            }
        }
        *link = allocation->next;
        if (!allocation->guest_address) free(allocation->bytes);
        free(allocation);
    }
}

bool qa_native_entity_table_get(const qa_native_instance *instance, qa_native_entity_table *out,
                                qa_error *error) {
    if (!instance || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance and entity-table output are required");
    if (!instance->entities.base && instance->entities.capacity)
        return native_fail(error, QA_ERROR_FORMAT, 0, "native entity table has no storage");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER) {
        qa_native_instance *mutable_instance = (qa_native_instance *)instance;
        if (!native_runner_entity_get(mutable_instance, out, error))
            return false;
        return native_entity_table_store(mutable_instance, *out, error);
    }
    *out = instance->entities;
    return true;
}

bool qa_native_entity_table_refresh(qa_native_instance *instance, qa_native_entity_table *out,
                                    qa_error *error)
{
    if (!instance || !out || instance->destroying || qa_native_terminal(instance))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "Native export refresh requires its live source instance");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER)
        return qa_native_entity_table_get(instance, out, error);
    if (!native_profile_refresh_entities(instance, error)) return false;
    return qa_native_entity_table_get(instance, out, error);
}

bool qa_native_terminal_entity_table(const qa_native_instance *instance,
                                      qa_native_entity_table *out, qa_error *error) {
    if (!out || !qa_native_terminal(instance) || !qa_native_can_destroy(instance))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "cached native table requires a drained terminal runner");
    if (instance->entities.capacity > instance->slot_capacity ||
        instance->entities.count > instance->entities.capacity ||
        (instance->entities.capacity && (!instance->entities.base || !instance->entities.stride)))
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "terminal native table differs from parent slot metadata");
    *out = instance->entities;
    return true;
}

bool native_entity_table_store(qa_native_instance *instance, qa_native_entity_table table,
                               qa_error *error) {
    if (!instance)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native instance is required for located game data");
    if ((!table.base && table.capacity) || (!table.stride && table.capacity) ||
        table.count > table.capacity || table.capacity > 1048576u)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "invalid native located entity table");
    if (table.capacity > instance->slot_capacity) {
        native_slot *slots = realloc(instance->slots, (size_t)table.capacity * sizeof(*slots));
        if (!slots)
            return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native located source slots");
        memset(slots + instance->slot_capacity, 0,
               (size_t)(table.capacity - instance->slot_capacity) * sizeof(*slots));
        instance->slots = slots;
        instance->slot_capacity = table.capacity;
    }
    if (table.capacity < instance->slot_capacity)
        memset(instance->slots + table.capacity, 0,
               (size_t)(instance->slot_capacity - table.capacity) * sizeof(*instance->slots));
    instance->entities = table;
    return true;
}

bool qa_native_set_entity_table(qa_native_instance *instance, qa_native_entity_table table,
                                qa_error *error) {
    if (!instance || instance->destroying ||
        (instance->module->info.profile != QA_NATIVE_Q3_VMMAIN &&
         instance->module->info.profile != QA_NATIVE_QUAKE_LIVE_GAME_API10))
        return native_fail(
            error, QA_ERROR_ARGUMENT, 0,
            "only a live native Q3 or Quake Live instance accepts located game data");
    if (instance->backend == QA_NATIVE_BACKEND_RUNNER &&
        !native_runner_entity_set(instance, table, error))
        return false;
    return native_entity_table_store(instance, table, error);
}

bool qa_native_entity_address(const qa_native_instance *instance, uint32_t slot,
                              qa_native_address *out, qa_error *error) {
    if (!instance || !out || slot >= instance->entities.capacity)
        return native_fail(error, QA_ERROR_ARGUMENT, slot,
                           "native entity slot is outside its source table");
    if (instance->entities.stride &&
        slot > (UINT64_MAX - instance->entities.base) / instance->entities.stride)
        return native_fail(error, QA_ERROR_ARGUMENT, slot, "native entity address overflows");
    *out = instance->entities.base + (uint64_t)slot * instance->entities.stride;
    return true;
}

bool qa_native_entity_slot(const qa_native_instance *instance, qa_native_address address,
                           uint32_t *out, qa_error *error) {
    if (!instance || !out || !instance->entities.stride || address < instance->entities.base)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native entity address and output are required");
    uint64_t relative = address - instance->entities.base;
    uint64_t slot = relative / instance->entities.stride;
    if (relative % instance->entities.stride || slot >= instance->entities.capacity)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "address is not the start of a native entity slot");
    *out = (uint32_t)slot;
    return true;
}

bool qa_native_bind_slot(qa_native_instance *instance, const qa_native_slot_binding *binding,
                         qa_error *error) {
    if (!instance || instance->destroying || !binding || binding->slot >= instance->entities.capacity ||
        binding->kind > QA_NATIVE_SLOT_BORROWED)
        return native_fail(error, QA_ERROR_ARGUMENT, binding ? binding->slot : 0,
                           "invalid native source slot binding");
    if (binding->kind == QA_NATIVE_SLOT_FREE &&
        (binding->owner || binding->source_slot || binding->actor.registry ||
         binding->actor.generation || binding->actor.slot))
        return native_fail(error, QA_ERROR_ARGUMENT, binding->slot,
                           "free native source slot carries actor identity");
    if (binding->kind != QA_NATIVE_SLOT_FREE && !binding->actor.registry)
        return native_fail(error, QA_ERROR_ARGUMENT, binding->slot,
                           "occupied native source slot requires an actor identity");
    instance->slots[binding->slot] =
        (native_slot){binding->kind, binding->actor, binding->owner, binding->source_slot};
    return true;
}

bool qa_native_slot(const qa_native_instance *instance, uint32_t slot, qa_native_slot_binding *out,
                    qa_error *error) {
    if (!instance || !out || slot >= instance->entities.capacity)
        return native_fail(error, QA_ERROR_ARGUMENT, slot,
                           "native source slot is outside its table");
    native_slot saved = instance->slots[slot];
    *out = (qa_native_slot_binding){saved.kind, slot, saved.actor, saved.owner, saved.source_slot};
    return true;
}
