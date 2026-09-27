#ifndef QA_BOT_CHAT_H
#define QA_BOT_CHAT_H

#include "qa/bot_library.h"

#define QA_BOT_CHAT_MESSAGE_SIZE 256
#define QA_BOT_CHAT_VARIABLES 8
typedef enum qa_bot_chat_asset_kind {
    QA_BOT_CHAT_SYNONYMS,
    QA_BOT_CHAT_RANDOMS,
    QA_BOT_CHAT_MATCHES,
    QA_BOT_CHAT_REPLIES,
    QA_BOT_CHAT_INITIAL
} qa_bot_chat_asset_kind;
typedef struct qa_bot_chat_range {
    uint32_t first, count;
} qa_bot_chat_range;
typedef struct qa_bot_chat_synonym {
    const char *text;
    float weight;
} qa_bot_chat_synonym;
typedef struct qa_bot_chat_synonyms {
    uint32_t context;
    float total_weight;
    qa_bot_chat_range entries;
} qa_bot_chat_synonyms;
typedef struct qa_bot_chat_list {
    const char *name;
    qa_bot_chat_range messages;
} qa_bot_chat_list;
typedef enum qa_bot_chat_piece_kind {
    QA_BOT_CHAT_VARIABLE,
    QA_BOT_CHAT_ALTERNATIVES
} qa_bot_chat_piece_kind;
typedef struct qa_bot_chat_piece {
    qa_bot_chat_piece_kind kind;
    union {
        uint32_t variable;
        qa_bot_chat_range alternatives;
    } data;
} qa_bot_chat_piece;
typedef struct qa_bot_chat_template {
    uint32_t context;
    int32_t type, subtype;
    qa_bot_chat_range pieces;
} qa_bot_chat_template;
typedef enum qa_bot_chat_key_mode {
    QA_BOT_CHAT_ANY,
    QA_BOT_CHAT_AND,
    QA_BOT_CHAT_NOT
} qa_bot_chat_key_mode;
typedef enum qa_bot_chat_key_kind {
    QA_BOT_CHAT_NAME,
    QA_BOT_CHAT_GENDER,
    QA_BOT_CHAT_BOT_NAMES,
    QA_BOT_CHAT_WORD,
    QA_BOT_CHAT_PATTERN
} qa_bot_chat_key_kind;
typedef struct qa_bot_chat_key {
    qa_bot_chat_key_mode mode;
    qa_bot_chat_key_kind kind;
    union {
        uint32_t gender;
        const char *text;
        qa_bot_chat_range pieces;
    } data;
} qa_bot_chat_key;
typedef struct qa_bot_chat_reply {
    float priority;
    qa_bot_chat_range keys, messages;
} qa_bot_chat_reply;
typedef struct qa_bot_chat_asset_view {
    qa_bot_chat_asset_kind kind;
    const char *path, *name;
    const char *const *messages, *const *alternatives;
    const qa_bot_chat_synonym *synonyms;
    const qa_bot_chat_synonyms *groups;
    const qa_bot_chat_list *lists;
    const qa_bot_chat_piece *pieces;
    const qa_bot_chat_template *templates;
    const qa_bot_chat_key *keys;
    const qa_bot_chat_reply *replies;
    size_t message_count, alternative_count, synonym_count, group_count, list_count, piece_count,
        template_count, key_count, reply_count;
} qa_bot_chat_asset_view;
typedef struct qa_bot_chat_asset qa_bot_chat_asset;
bool qa_bot_chat_asset_load(qa_bot_library *, qa_bot_chat_asset_kind, const char *path,
                            const char *name, qa_bot_chat_asset **, qa_error *);
void qa_bot_chat_asset_retain(qa_bot_chat_asset *);
void qa_bot_chat_asset_release(qa_bot_chat_asset *);
const qa_bot_chat_asset_view *qa_bot_chat_asset_read(const qa_bot_chat_asset *);
bool qa_bot_chat_asset_restore(const qa_bot_chat_asset_view *, qa_bot_chat_asset **, qa_error *);
/* Cached initial/reply definitions share cooldowns across bots, as the source
 * does. Capture these once per shared asset, separately from each bot state. */
const float *qa_bot_chat_cooldowns(const qa_bot_chat_asset *, size_t *count);
bool qa_bot_chat_cooldowns_restore(qa_bot_chat_asset *, const float *, size_t, qa_error *);
typedef struct qa_bot_chat_capture {
    int16_t offset;
    uint16_t length;
} qa_bot_chat_capture;
typedef struct qa_bot_chat_match {
    char text[QA_BOT_CHAT_MESSAGE_SIZE];
    int32_t type, subtype;
    qa_bot_chat_capture variables[QA_BOT_CHAT_VARIABLES];
} qa_bot_chat_match;
typedef enum qa_bot_chat_destination {
    QA_BOT_CHAT_ALL,
    QA_BOT_CHAT_TEAM,
    QA_BOT_CHAT_TELL
} qa_bot_chat_destination;
typedef struct qa_bot_chat_services {
    void *context;
    bool (*command)(void *, int32_t client, const char *, qa_error *);
    void (*diagnostic)(void *, qa_script_severity, const char *);
    bool (*test_initial)(void *);
    bool (*test_reply)(void *);
    qa_bot_random_source random;
} qa_bot_chat_services;
typedef struct qa_bot_chat_options {
    qa_bot_chat_asset *synonyms, *randoms, *matches, *replies;
    size_t console_capacity;
    bool debug;
    bool console_unavailable;
} qa_bot_chat_options;
/* One session thread owns states and shared cooldowns. RNG and test queries
 * only read their source owner. Command and diagnostic callbacks may retire a
 * chat state; active calls retain it through the callback. Zero console
 * capacity selects the native unbounded pool; source runtime supplies >=2. */
typedef struct qa_bot_chat_system qa_bot_chat_system;
typedef struct qa_bot_chat qa_bot_chat;
bool qa_bot_chat_system_create(const qa_bot_chat_services *, const qa_bot_chat_options *,
                               qa_bot_chat_system **, qa_error *);
void qa_bot_chat_system_destroy(qa_bot_chat_system *);
bool qa_bot_chat_system_configure(qa_bot_chat_system *, const qa_bot_chat_options *, qa_error *);
bool qa_bot_chat_create(qa_bot_chat_system *, int32_t client, qa_bot_chat **, qa_error *);
void qa_bot_chat_destroy(qa_bot_chat *);
bool qa_bot_chat_set_initial(qa_bot_chat *, qa_bot_chat_asset *, qa_error *);
bool qa_bot_chat_load_initial(qa_bot_chat *, qa_bot_library *, const char *path, const char *name,
                              bool developer, int32_t *source_result, qa_error *);
bool qa_bot_chat_check_integrity(qa_bot_chat_system *, qa_bot_chat_asset *, qa_error *);
void qa_bot_chat_set_name(qa_bot_chat *, const char *, int32_t client);
void qa_bot_chat_set_identity(qa_bot_chat *, const char *, const int32_t *client);
void qa_bot_chat_set_gender(qa_bot_chat *, uint32_t);
size_t qa_bot_chat_initial_count(const qa_bot_chat *, const char *);
bool qa_bot_chat_initial(qa_bot_chat *, const char *, uint32_t context,
                         const char *const variables[8], float time, bool *found, qa_error *);
bool qa_bot_chat_reply_message(qa_bot_chat *, const char *, uint32_t message_context,
                               uint32_t variable_context, const char *const variables[8],
                               float time, bool *found, qa_error *);
bool qa_bot_chat_find_match(const qa_bot_chat_system *, const char *, uint32_t, qa_bot_chat_match *,
                            bool *, qa_error *);
bool qa_bot_chat_match_variable(const qa_bot_chat_match *, uint32_t, char *, size_t, qa_error *);
bool qa_bot_chat_replace_synonyms(qa_bot_chat_system *, char *, size_t, uint32_t, bool weighted,
                                  bool reply, qa_error *);
void qa_bot_chat_unify_whitespace(char *);
int32_t qa_bot_chat_contains(const char *, const char *, bool case_sensitive);
/* External buffers borrow the same algorithms. The boundary owns address
 * validation, byte widths and write observations. snapshot excludes the NUL
 * and remains readable until the next callback; callers copy before writing.
 * copy has memmove semantics and publishes one write. The external match and
 * synonym entrypoints accept NULL system as the source's empty configuration. */
typedef struct qa_bot_chat_text_io {
    void *context;
    bool (*admit)(void *, size_t offset, size_t size, qa_error *);
    bool (*read)(void *, size_t offset, void *, size_t, qa_error *);
    bool (*write)(void *, size_t offset, qa_bytes, qa_error *);
    bool (*copy)(void *, size_t destination, size_t source, size_t size, qa_error *);
    bool (*clear)(void *, size_t offset, size_t size, qa_error *);
    bool (*snapshot)(void *, qa_bytes *, qa_error *);
} qa_bot_chat_text_io;
typedef struct qa_bot_chat_match_io {
    qa_bot_chat_text_io text;
    bool (*read_offset)(void *, uint32_t index, int32_t *, qa_error *);
    bool (*write_offset)(void *, uint32_t index, int32_t, qa_error *);
    bool (*write_length)(void *, uint32_t index, int32_t, qa_error *);
    bool (*write_type)(void *, bool subtype, int32_t, qa_error *);
} qa_bot_chat_match_io;
bool qa_bot_chat_find_match_into(const qa_bot_chat_system *, const char *, uint32_t,
                                 const qa_bot_chat_match_io *, bool *, qa_error *);
bool qa_bot_chat_unify_whitespace_into(const qa_bot_chat_text_io *, qa_error *);
bool qa_bot_chat_replace_synonyms_into(qa_bot_chat_system *, const qa_bot_chat_text_io *,
                                       uint32_t context, qa_error *);
const char *qa_bot_chat_message(const qa_bot_chat *);
bool qa_bot_chat_take_message(qa_bot_chat *, char *, size_t, qa_error *);
bool qa_bot_chat_write_message(qa_bot_chat *, void *context,
                               bool (*write)(void *, const char *, qa_error *), qa_error *);
bool qa_bot_chat_enter(qa_bot_chat *, int32_t recipient, qa_bot_chat_destination, qa_error *);
bool qa_bot_chat_enter_from(qa_bot_chat *, const int32_t *source_client, int32_t recipient,
                            qa_bot_chat_destination, qa_error *);
typedef struct qa_bot_console_message {
    uint32_t handle;
    float time;
    int32_t type;
    char text[256];
} qa_bot_console_message;
bool qa_bot_chat_console_queue(qa_bot_chat *, int32_t, const char *, float, uint32_t *, qa_error *);
bool qa_bot_chat_console_first(const qa_bot_chat *, qa_bot_console_message *);
bool qa_bot_chat_console_remove(qa_bot_chat *, uint32_t);
size_t qa_bot_chat_console_count(const qa_bot_chat *);
typedef struct qa_bot_chat_state {
    int32_t client;
    uint32_t gender, last_handle;
    char name[32], message[256];
    qa_bot_console_message *console;
    size_t console_count;
} qa_bot_chat_state;
bool qa_bot_chat_capture_state(const qa_bot_chat *, qa_bot_chat_state *, qa_error *);
bool qa_bot_chat_restore_state(qa_bot_chat *, const qa_bot_chat_state *, qa_error *);
void qa_bot_chat_state_free(qa_bot_chat_state *);

#endif
