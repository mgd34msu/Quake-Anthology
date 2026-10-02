#include "../native/internal.h"

bool qa_native_terminal_retired(const qa_native_instance *instance, const qa_actor_registry *actors) {
    if (!qa_native_terminal(instance) || !qa_native_can_destroy(instance))
        return false;
    for (uint32_t i = 0; i < instance->slot_capacity; ++i) {
        const native_slot *slot = instance->slots + i;
        if (slot->kind != QA_NATIVE_SLOT_FREE && slot->actor.registry &&
            (!actors || slot->actor.registry != qa_actors_identity(actors) || qa_actors_get(actors, slot->actor)))
            return false;
    }
    return true;
}

