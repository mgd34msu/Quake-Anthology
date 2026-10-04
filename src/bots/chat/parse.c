#include "internal.h"

bool chat_append(void **data, size_t *capacity, size_t *count, size_t stride, const void *value,
                 qa_error *e) {
    if (*count >= UINT32_MAX) {
        qa_error_set(e, QA_ERROR_MEMORY, *count, "Bot chat resource index overflow");
        return false;
    }
    if (!bot_grow(data, capacity, *count + 1, stride, e))
        return false;
    memcpy((uint8_t *)*data + (*count)++ * stride, value, stride);
    return true;
}
