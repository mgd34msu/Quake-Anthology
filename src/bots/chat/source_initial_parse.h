#ifndef QA_BOT_CHAT_SOURCE_INITIAL_PARSE_H
#define QA_BOT_CHAT_SOURCE_INITIAL_PARSE_H
#include "source_initial.h"

typedef struct bot_chat_initial_parser_host {
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*report)(void *,const qa_script_diagnostic *,qa_error *);
    bool (*name_equal)(void *,qa_bytes,bool *,qa_error *);
    /* The source closes PC at EOF before the requested-name outcome. */
    bool (*complete)(void *,qa_error *);
} bot_chat_initial_parser_host;
/* A null stored owner is the sizing pass. The second pass uses the reached
 * heap retained by the enclosing resource before resolver callbacks. */
bool bot_chat_initial_parse(qa_script *,const char *,const bot_chat_initial_parser_host *,
    bot_chat_initial *,bool,uint32_t *,bool *,bool *,bool *,qa_error *);
#endif
