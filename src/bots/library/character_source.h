#ifndef QA_BOT_CHARACTER_SOURCE_H
#define QA_BOT_CHARACTER_SOURCE_H
#include "internal.h"
#include "qa/source_save.h"

enum { BOT_CHARACTER_BYTES = 716, BOT_CHARACTER_VALUE_OFFSET = 68 };
typedef struct bot_character_string {
    uint32_t pointer;
    qa_bot_memory_allocation allocation;
} bot_character_string;
typedef struct bot_character_store {
    size_t references;
    qa_bot_memory *memory;
    bool owns_memory;
    uint64_t next_pointer;
    bot_character_string *strings;
    size_t count, capacity;
} bot_character_store;
bool bot_character_store_create(qa_bot_memory *, bool, bot_character_store **, qa_error *);
void bot_character_store_release(bot_character_store *);
bool bot_character_create(bot_character_store *, const char *, float, qa_bot_character **, qa_error *);
bool bot_character_project(qa_bot_character *, qa_error *);
bool bot_character_write(qa_bot_character *, uint32_t, qa_bot_character_value, bool, qa_error *);
bool bot_character_skill(qa_bot_character *, float, qa_error *);
void bot_character_forget(qa_bot_library *, qa_bot_character *);
bool bot_character_store_fields(qa_source_save_io *, bot_character_store *);
bool bot_character_alias_fields(qa_source_save_io *, bot_character_store *, const qa_bot_character *,
                                const qa_script_services *, qa_bot_character **);
bool bot_character_standalone_fields(qa_source_save_io *, const qa_bot_character *, qa_bot_character **);
#endif
