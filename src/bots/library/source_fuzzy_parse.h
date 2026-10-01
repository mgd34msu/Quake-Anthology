#ifndef QA_BOT_SOURCE_FUZZY_PARSE_H
#define QA_BOT_SOURCE_FUZZY_PARSE_H
#include "source_fuzzy.h"
#include "qa/bot_library.h"
typedef struct bot_fuzzy_parser_host {
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*report)(void *,const qa_script_diagnostic *,qa_error *);
} bot_fuzzy_parser_host;

/* The caller owns the already opened source. Once allocated, out keeps its
 * true partial record even on failure; the store decides whether to free it. */
bool bot_fuzzy_parse(qa_bot_library *,qa_script *,bot_fuzzy_heap *,const char *,
    bot_fuzzy_config *,bool *source_failure,const bot_fuzzy_parser_host *,qa_error *);
#endif
