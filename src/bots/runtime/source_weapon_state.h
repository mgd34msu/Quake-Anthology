#ifndef QA_BOT_SOURCE_WEAPON_STATE_H
#define QA_BOT_SOURCE_WEAPON_STATE_H
#include "qa/bot_library.h"
#include "qa/bots_allocator.h"

enum {BOT_WEAPON_STATE_BYTES=8};
typedef enum bot_weapon_record_word {
    BOT_WEAPON_CONFIG_POINTER=0,BOT_WEAPON_INDEX_POINTER=4
} bot_weapon_record_word;
typedef struct bot_weapon_record {
    qa_bot_memory *memory;
    qa_bot_memory_allocation allocation;
} bot_weapon_record;
typedef struct bot_weapon_state {
    bool used;
    bot_weapon_record record;
    uint64_t revision;
} bot_weapon_state;
bool bot_weapon_record_bind(qa_bot_memory *,qa_bot_memory_allocation,bot_weapon_record *,qa_error *);
bool bot_weapon_record_allocate(qa_bot_memory *,bot_weapon_record *,qa_error *);
bool bot_weapon_record_read(const bot_weapon_record *,bot_weapon_record_word,uint32_t *,qa_error *);
bool bot_weapon_record_write(const bot_weapon_record *,bot_weapon_record_word,uint32_t,qa_error *);
bool bot_weapon_record_reset(const bot_weapon_record *,qa_error *);
bool bot_weapon_indexes_allocate(qa_bot_memory *,uint32_t,qa_bot_memory_allocation *,qa_error *);
bool bot_weapon_index_read(qa_bot_memory *,qa_bot_memory_allocation,uint32_t,int32_t *,qa_error *);
bool bot_weapon_index_write(qa_bot_memory *,qa_bot_memory_allocation,uint32_t,int32_t,qa_error *);

typedef enum bot_weapon_pointer_kind {BOT_WEAPON_POINTER_CONFIG,BOT_WEAPON_POINTER_INDEXES} bot_weapon_pointer_kind;
typedef struct bot_weapon_pointer {
    uint32_t pointer;
    bot_weapon_pointer_kind kind;
    qa_bot_weights *config;
    uint64_t references;
    qa_bot_memory_allocation indexes;
    struct bot_weapon_pointer *next;
} bot_weapon_pointer;
typedef struct bot_weapon_config_identity {
    qa_bot_weights *config;
    uint32_t pointer;
    struct bot_weapon_config_identity *next;
} bot_weapon_config_identity;
typedef struct bot_weapon_pointers {
    uint64_t next_pointer;
    bot_weapon_pointer *first,*last;
    bot_weapon_config_identity *configs,*last_config;
} bot_weapon_pointers;
void bot_weapon_pointers_init(bot_weapon_pointers *);
/* Native alias disposal has no source FreeWeightConfig/free-allocation effect. */
void bot_weapon_pointers_clear(bot_weapon_pointers *);
bool bot_weapon_config_get(const bot_weapon_pointers *,const bot_weapon_record *,qa_bot_weights **,qa_error *);
bool bot_weapon_config_set(bot_weapon_pointers *,const bot_weapon_record *,qa_bot_weights *,qa_error *);
bool bot_weapon_indexes_get(const bot_weapon_pointers *,const bot_weapon_record *,qa_bot_memory_allocation *,bool *,qa_error *);
bool bot_weapon_indexes_publish(bot_weapon_pointers *,const bot_weapon_record *,qa_bot_memory_allocation,qa_error *);
bool bot_weapon_indexes_forget(bot_weapon_pointers *,const bot_weapon_record *,qa_error *);
#endif
