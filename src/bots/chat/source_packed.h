#ifndef QA_BOT_CHAT_SOURCE_PACKED_H
#define QA_BOT_CHAT_SOURCE_PACKED_H
#include "qa/bot_chat.h"
#include "qa/bots_allocator.h"
#include "qa/bots_allocator_checkpoint.h"
#include "qa/source_save.h"

typedef struct bot_chat_packed_member {
    uint32_t offset,text,first,count;
    /* A zero-byte sizing pass leaves pass two without an allocation. The
     * donor then retains actual parser values in getter closures. */
    uint32_t word;
    float number;
    char *value;
} bot_chat_packed_member;
typedef struct bot_chat_packed {
    qa_bot_memory *memory;
    qa_bot_memory_allocation allocation;
    qa_script_services services;
    qa_script_options options;
    char *include_path,*date,*time;
    qa_bot_chat_asset *asset;
    qa_bot_chat_system *loading_system;
    uint64_t loading_revision;
    qa_script *reader;
    struct bot_chat_initial_acquired *pending;
    bot_chat_packed_member *groups,*entries;
    size_t group_count,group_capacity,entry_count,entry_capacity;
    uint32_t size,pass;
    bool active,attempted,loaded,missing_root,source_failure,own_failure;
    bool retired_abort,service_failed,report_failed;
    qa_error report_error;
} bot_chat_packed;

bool bot_chat_packed_owner(qa_bot_chat_asset *,qa_bot_library *,qa_bot_memory *,qa_error *);
void bot_chat_packed_destroy(bot_chat_packed *);
bool bot_chat_packed_load(qa_bot_chat_asset *,qa_bot_chat_system *,uint64_t,bool *,qa_error *);
bool bot_chat_packed_refresh(qa_bot_chat_asset *,qa_error *);
bool bot_chat_packed_group(const qa_bot_chat_asset *,uint32_t,qa_bot_chat_synonyms *,qa_error *);
bool bot_chat_packed_entry(const qa_bot_chat_asset *,uint32_t,qa_bot_chat_synonym *,qa_error *);
bool bot_chat_packed_weight(const qa_bot_chat_asset *,uint32_t,float *,qa_error *);
bool bot_chat_packed_list(const qa_bot_chat_asset *,uint32_t,const char **,int32_t *,qa_error *);
bool bot_chat_packed_count(const qa_bot_chat_asset *,uint32_t,int32_t *,qa_error *);
bool bot_chat_packed_message(const qa_bot_chat_asset *,uint32_t,const char **,qa_error *);
bool bot_chat_packed_copy(const bot_chat_packed *,const qa_bot_memory_prepared *,bot_chat_packed **,qa_error *);
bool bot_chat_packed_fields(qa_source_save_io *,const qa_bot_chat_asset *,qa_bot_library *,qa_bot_memory *,qa_bot_chat_asset **);
#endif
