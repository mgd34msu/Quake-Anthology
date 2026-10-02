#ifndef QA_BOTS_CHAT_INTERNAL_H
#define QA_BOTS_CHAT_INTERNAL_H
#include "../library/internal.h"
#include "qa/bot_chat.h"
#include "qa/bot_log.h"
#include "source_initial_resource.h"
#include "source_state.h"
#include "source_packed.h"

struct qa_bot_chat_asset {
    atomic_uint references;
    qa_arena arena;
    qa_arena projection_arena;
    qa_bot_chat_asset_view view;
    const char **messages, **alternatives;
    qa_bot_chat_synonym *synonyms;
    qa_bot_chat_synonyms *groups;
    qa_bot_chat_list *lists;
    qa_bot_chat_piece *pieces;
    qa_bot_chat_template *templates;
    qa_bot_chat_key *keys;
    qa_bot_chat_reply *replies;
    size_t message_capacity, alternative_capacity, synonym_capacity, group_capacity, list_capacity,
        piece_capacity, template_capacity, key_capacity, reply_capacity;
    float *cooldowns;
    bot_chat_initial_resource *initial_source;
    bot_chat_packed *packed_source;
    qa_bot_library *source_library;
    qa_bot_chat *loading_state;
    uint64_t loading_revision;
    bool source_loaded;
    uint32_t *initial_types, *initial_messages;
    const qa_bot_chat_text_source *loading_path,*loading_name;
    struct qa_bot_chat_asset *next;
};
typedef struct chat_console_cell {
    qa_bot_memory_allocation allocation;
    uint32_t offset;
} chat_console_cell;
struct qa_bot_chat_system {
    qa_bot_chat_services services;
    qa_bot_chat_options options;
    qa_bot_log *log;
    qa_bot_library *library;
    qa_bot_memory *memory;
    qa_bot_memory_allocation console_heap;
    qa_bot_memory_allocation initial_cache[64];
    chat_console_cell *console;
    uint32_t free_console;
    size_t console_count, console_capacity;
    qa_bot_chat *states;
    size_t references;
    uint64_t revision;
    bool retired, restoring;
};
struct qa_bot_chat {
    qa_bot_chat_system *system;
    qa_bot_chat_asset *initial;
    qa_bot_chat *next, *previous;
    qa_bot_memory_allocation allocation;
    size_t references;
    uint64_t initial_revision;
    char name_projection[33],message_projection[257];
    bool retired;
};
bool chat_asset_parse(qa_bot_library *, qa_bot_chat_asset *, qa_error *);
bool chat_asset_allocate(qa_bot_chat_asset_kind, const char *, const char *, qa_bot_chat_asset **,
                         qa_error *);
void chat_asset_view(qa_bot_chat_asset *);
bool chat_asset_finish(qa_bot_chat_asset *, qa_error *);
bool chat_asset_load(qa_bot_library *, qa_bot_chat_asset_kind, const char *, const char *,
                     qa_bot_chat_asset **, bool *cached, qa_error *);
bool chat_asset_setup_load(qa_bot_library *,qa_bot_chat_asset_kind,const char *,
    qa_bot_chat_system *,uint64_t,qa_bot_chat_asset **,bool *,qa_error *);
bool chat_initial_asset_load(qa_bot_library *,const char *,const char *,qa_bot_chat *,uint64_t,
    qa_bot_chat_asset **,bool *,qa_error *);
bool chat_initial_asset_load_from(qa_bot_library *,const qa_bot_chat_text_source *,
    const qa_bot_chat_text_source *,qa_bot_chat *,uint64_t,qa_bot_chat_asset **,bool *,qa_error *);
bool chat_text_read(const qa_bot_chat_text_source *,size_t,qa_bytes *,qa_error *);
bool chat_text_copy(const qa_bot_chat_text_source *,size_t,char **,qa_error *);
qa_bot_chat_text_source chat_text_source(const char *);
bool chat_initial_asset_refresh(qa_bot_chat_asset *,qa_error *);
bool chat_initial_asset_owner(qa_bot_chat_asset *,qa_bot_library *,qa_bot_memory *,qa_error *);
bool chat_initial_asset_from_view(const qa_bot_chat_asset_view *,qa_bot_chat_asset **,qa_error *);
bool chat_initial_type_find(const qa_bot_chat_asset *,const char *,uint32_t *,qa_error *);
bool chat_initial_type_find_from(const qa_bot_chat_asset *,const qa_bot_chat_text_source *,uint32_t *,qa_error *);
bool chat_asset_message_time(qa_bot_chat_asset *,uint32_t,float *,bool,qa_error *);
bool chat_asset_message_text(const qa_bot_chat_asset *,uint32_t,const char **,qa_error *);
bool chat_match_pieces(const qa_bot_chat_asset *, qa_bot_chat_range, qa_bot_chat_match *);
bool chat_construct(qa_bot_chat *, const char *, uint32_t, qa_bot_chat_match *, uint32_t, bool,
                    qa_error *);
bool chat_replace_source(qa_bot_chat_system *,const char *,uint32_t,bool,bool,char **,qa_error *);
void chat_match_clear(qa_bot_chat_match *, const char *);
int32_t chat_word(const char *, const char *, size_t);
bool chat_equal(const char *, const char *);
void chat_copy(char *, size_t, const char *);
void chat_strip_tildes(char *);
void chat_report(qa_bot_chat_system *, qa_script_severity, const char *);
bool chat_print(qa_bot_chat_system *,qa_script_severity,const char *,qa_error *);
bool chat_source_time(qa_bot_chat_system *,float,float *,qa_error *);
bool chat_initial_free(qa_bot_chat *,qa_error *);
bool chat_state_message_strip(qa_bot_chat *,qa_error *);
bool chat_state_message_clear(qa_bot_chat *,qa_error *);
bool chat_initial_source_construct(qa_bot_chat *,const char *,uint32_t,
    const char *const[8],float,bool *,qa_error *);
bool chat_initial_source_construct_from(qa_bot_chat *,const qa_bot_chat_text_source *,uint32_t,
    const qa_bot_chat_text_source[8],float,bool *,qa_error *);
bool chat_append_variables(qa_bot_chat_match *,const char *const[8],qa_error *);
bool chat_append_variables_from(qa_bot_chat_match *,const qa_bot_chat_text_source[8],qa_error *);
bool bot_chat_system_library_bind(qa_bot_chat_system *,qa_bot_library *,qa_error *);
void chat_retain(qa_bot_chat *);
void chat_release(qa_bot_chat *);
void chat_system_release(qa_bot_chat_system *);
bool chat_random_string(qa_bot_chat_system *,const char *,const char **,qa_error *);
bool chat_reserve_console(qa_bot_chat_system *, size_t, qa_error *);
bool chat_parse_synonyms(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_randoms(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_matches(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_replies(qa_bot_library *, qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_string(qa_bot_chat_asset *, qa_script *, const char **, qa_error *);
bool chat_message_parse(qa_bot_chat_asset *, qa_script *, const char **, qa_error *);
bool chat_pieces_parse(qa_bot_chat_asset *, qa_script *, const char *, qa_bot_chat_range *,
                       qa_error *);
bool chat_messages_parse(qa_bot_chat_asset *, qa_script *, qa_bot_chat_range *, qa_error *);
bool chat_append(void **, size_t *, size_t *, size_t, const void *, qa_error *);
#endif
