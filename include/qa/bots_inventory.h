#ifndef QA_BOTS_INVENTORY_H
#define QA_BOTS_INVENTORY_H
#include "qa/bot_library.h"
#include <string.h>

/* A borrowed little-endian source array, without a native int32_t overlay. */
typedef struct qa_bot_inventory_bytes {
    const uint8_t *data;
    size_t count;
} qa_bot_inventory_bytes;

static inline int32_t qa_bot_inventory_value(qa_bot_inventory_bytes inventory,int32_t index) {
    if(index<0 || (size_t)index>=inventory.count) return 0;
    const uint8_t *bytes=inventory.data+(size_t)index*4;
    uint32_t word=(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8)|
        ((uint32_t)bytes[2]<<16)|((uint32_t)bytes[3]<<24);
    int32_t value;memcpy(&value,&word,sizeof(value));return value;
}

/* Each successful write changes the retained source array immediately. */
typedef struct qa_bot_inventory_target {
    void *context;
    size_t count;
    bool (*write)(void *,int32_t index,int32_t value,qa_error *);
} qa_bot_inventory_target;

static inline bool qa_bot_inventory_write(const qa_bot_inventory_target *target,int32_t index,
                                           int32_t value,qa_error *error) {
    if(!target || !target->write || index<0 || (size_t)index>=target->count) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Inventory write exceeds its admitted source target");
        return false;
    }
    return target->write(target->context,index,value,error);
}
#endif
