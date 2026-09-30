#ifndef QA_BOT_BSP_PRIVATE_H
#define QA_BOT_BSP_PRIVATE_H

#include "qa/bot_bsp.h"

struct qa_bot_bsp {
    qa_entities entities;
    size_t record_capacity, property_capacity;
};

#endif
