#ifndef QA_Q3_PEER_INTERNAL_H
#define QA_Q3_PEER_INTERNAL_H
#include "qa/network_q3.h"
#include <stdlib.h>
#include <string.h>

typedef struct qa_q3_snapshot_slot {
    qa_q3_snapshot value;
    qa_q3_entity *entities;
    size_t capacity;
    int32_t sent_time, ack_time;
    size_t message_size;
} qa_q3_snapshot_slot;
static inline bool qa_q3_slot_store(qa_q3_snapshot_slot *slot, const qa_q3_snapshot *value, qa_error *error) {
    if (value->entity_count > SIZE_MAX / sizeof(*slot->entities) || (value->entity_count && !value->entities)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid retained Q3 snapshot entities"); return false;
    }
    if (slot->capacity < value->entity_count) {
        qa_q3_entity *entities = realloc(slot->entities, value->entity_count * sizeof(*entities));
        if (!entities) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 snapshot history"); return false; }
        slot->entities = entities; slot->capacity = value->entity_count;
    }
    if (value->entity_count) memcpy(slot->entities, value->entities, value->entity_count * sizeof(*slot->entities));
    slot->value = *value; slot->value.entities = slot->entities;
    return true;
}
static inline void qa_q3_slot_clear(qa_q3_snapshot_slot *slot, qa_q3_product product) {
    memset(&slot->value, 0, sizeof(slot->value));
    slot->value.entities = slot->entities; slot->value.player.product = product;
    slot->sent_time = 0; slot->ack_time = -1; slot->message_size = 0;
}
#endif
