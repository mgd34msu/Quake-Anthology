#ifndef QA_BOT_CHAT_SOURCE_INITIAL_RESOURCE_H
#define QA_BOT_CHAT_SOURCE_INITIAL_RESOURCE_H
#include "source_initial_parse.h"

typedef enum bot_chat_initial_diagnostic_origin {
    BOT_CHAT_INITIAL_SOURCE_DIAGNOSTIC,BOT_CHAT_INITIAL_PRINT_DIAGNOSTIC
} bot_chat_initial_diagnostic_origin;
typedef struct bot_chat_initial_resource_host {
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*report)(void *,bot_chat_initial_diagnostic_origin,const qa_script_diagnostic *,qa_error *);
    bool (*name_equal)(void *,qa_bytes,bool *,qa_error *);
    bool (*path)(void *,const char **,qa_error *);
    /* The true map ordinal is read after allocation and owner qualification;
     * callbacks in the first parser pass may have registered other chats. */
    bool (*identity)(void *,uint32_t *,qa_error *);
    /* Called immediately after the real second-pass allocation, before PC
     * root resolution. The enclosing owner installs the true pointer view. */
    bool (*publish)(void *,bot_chat_initial *,qa_error *);
} bot_chat_initial_resource_host;
typedef struct bot_chat_initial_acquired {
    qa_script_resource resource;
    bool owned;
    struct bot_chat_initial_acquired *next;
} bot_chat_initial_acquired;
typedef enum bot_chat_initial_failure {
    BOT_CHAT_INITIAL_NO_FAILURE,BOT_CHAT_INITIAL_ROOT_MISSING,
    BOT_CHAT_INITIAL_GRAMMAR_FAILURE,BOT_CHAT_INITIAL_NAME_MISSING
} bot_chat_initial_failure;
typedef struct bot_chat_initial_resource {
    qa_bot_memory *memory;
    qa_script_services services;
    qa_script_options options;
    bot_chat_initial_resource_host host;
    bot_chat_initial initial;
    qa_script *reader;
    bot_chat_initial_acquired *pending;
    char *path,*include_path,*date,*time;
    uint32_t size,pass;
    bot_chat_initial_failure failure;
    bool active,attempted,published,loaded,missing_root,own_failure,report_failed;
    bool retired_abort,service_failed;
    qa_error report_error;
} bot_chat_initial_resource;
bool bot_chat_initial_resource_create(qa_bot_memory *,const qa_script_services *,
    const qa_script_options *,const bot_chat_initial_resource_host *,
    bot_chat_initial_resource **,qa_error *);
bool bot_chat_initial_resource_pure(qa_bot_memory *,bot_chat_initial_resource **,qa_error *);
void bot_chat_initial_resource_destroy(bot_chat_initial_resource *);
bool bot_chat_initial_resource_current(const bot_chat_initial_resource *,qa_error *);
qa_script_services bot_chat_initial_resource_services(bot_chat_initial_resource *);
bool bot_chat_initial_resource_load(bot_chat_initial_resource *,bool *,qa_error *);
#endif
