#ifndef QA_BOT_VARIABLES_PRIVATE_H
#define QA_BOT_VARIABLES_PRIVATE_H

#include "internal.h"

typedef struct bot_variable {
    struct bot_variable *next, *bucket_next;
    qa_bot_variable view;
    char *text;
    size_t capacity;
    uint32_t hash;
    char name[];
} bot_variable;

uint32_t bot_variable_name_hash(const char *);
bool bot_variable_name_equal(const char *, const char *);

#endif
