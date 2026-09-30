#ifndef QA_BOTS_CHAT_INTERNAL_H
#define QA_BOTS_CHAT_INTERNAL_H
#include "../library/internal.h"
#include "qa/bot_chat.h"

struct qa_bot_chat_asset {
    atomic_uint references;
    qa_arena arena;
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
    struct qa_bot_chat_asset *next;
};
typedef struct chat_console_cell {
    qa_bot_console_message message;
    uint32_t next, previous;
} chat_console_cell;
struct qa_bot_chat_system {
    qa_bot_chat_services services;
    qa_bot_chat_options options;
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
    int32_t client;
    uint32_t gender, last_handle, first_console, last_console;
    size_t console_count;
    size_t references;
    uint64_t initial_revision;
    bool retired;
    char name[32], message[256];
};
bool chat_asset_parse(qa_bot_library *, qa_bot_chat_asset *, qa_error *);
bool chat_asset_allocate(qa_bot_chat_asset_kind, const char *, const char *, qa_bot_chat_asset **,
                         qa_error *);
void chat_asset_view(qa_bot_chat_asset *);
bool chat_asset_finish(qa_bot_chat_asset *, qa_error *);
bool chat_asset_load(qa_bot_library *, qa_bot_chat_asset_kind, const char *, const char *,
                     qa_bot_chat_asset **, bool *cached, qa_error *);
bool chat_match_pieces(const qa_bot_chat_asset *, qa_bot_chat_range, qa_bot_chat_match *);
bool chat_construct(qa_bot_chat *, const char *, uint32_t, qa_bot_chat_match *, uint32_t, bool,
                    qa_error *);
void chat_match_clear(qa_bot_chat_match *, const char *);
int32_t chat_word(const char *, const char *, size_t);
bool chat_equal(const char *, const char *);
void chat_copy(char *, size_t, const char *);
void chat_strip_tildes(char *);
void chat_report(qa_bot_chat_system *, qa_script_severity, const char *);
void chat_retain(qa_bot_chat *);
void chat_release(qa_bot_chat *);
void chat_system_release(qa_bot_chat_system *);
const char *chat_random_string(qa_bot_chat_system *, const char *);
bool chat_reserve_console(qa_bot_chat_system *, size_t, qa_error *);
bool chat_parse_synonyms(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_randoms(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_matches(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_replies(qa_bot_library *, qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_parse_initial(qa_bot_chat_asset *, qa_script *, qa_error *);
bool chat_string(qa_bot_chat_asset *, qa_script *, const char **, qa_error *);
bool chat_message_parse(qa_bot_chat_asset *, qa_script *, const char **, qa_error *);
bool chat_pieces_parse(qa_bot_chat_asset *, qa_script *, const char *, qa_bot_chat_range *,
                       qa_error *);
bool chat_messages_parse(qa_bot_chat_asset *, qa_script *, qa_bot_chat_range *, qa_error *);
bool chat_append(void **, size_t *, size_t *, size_t, const void *, qa_error *);
#endif
