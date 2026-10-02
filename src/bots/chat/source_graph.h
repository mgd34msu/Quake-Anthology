#ifndef QA_BOT_CHAT_SOURCE_GRAPH_H
#define QA_BOT_CHAT_SOURCE_GRAPH_H
#include "qa/bot_chat.h"
#include "qa/bots_allocator_checkpoint.h"
#include "qa/source_save.h"

typedef enum bot_chat_graph_kind {
    BOT_CHAT_GRAPH_STRING,BOT_CHAT_GRAPH_MATCH_STRING,BOT_CHAT_GRAPH_PIECE,
    BOT_CHAT_GRAPH_TEMPLATE,BOT_CHAT_GRAPH_KEY,BOT_CHAT_GRAPH_MESSAGE,BOT_CHAT_GRAPH_REPLY
} bot_chat_graph_kind;
typedef struct bot_chat_graph_pointer {
    qa_bot_memory_allocation allocation;
    uint32_t offset;
    bot_chat_graph_kind kind;
} bot_chat_graph_pointer;
typedef enum bot_chat_graph_cleanup {
    BOT_CHAT_GRAPH_OUTER,BOT_CHAT_GRAPH_PIECES,BOT_CHAT_GRAPH_GRAPHS
} bot_chat_graph_cleanup;
typedef struct bot_chat_graph {
    qa_bot_memory *memory;
    bot_chat_graph_pointer *pointers;
    size_t count,capacity;
    uint32_t root,unfinished;
    bot_chat_graph_cleanup cleanup;
} bot_chat_graph;
typedef struct bot_chat_graph_messages {
    uint32_t reply,message;
    size_t replies,messages;
    bool started;
} bot_chat_graph_messages;
void bot_chat_graph_dispose(bot_chat_graph *);
bool bot_chat_graph_new(bot_chat_graph *,bot_chat_graph_kind,qa_bytes,uint32_t *,qa_error *);
bool bot_chat_graph_word(const bot_chat_graph *,uint32_t,bot_chat_graph_kind,uint32_t,uint32_t *,qa_error *);
bool bot_chat_graph_write(bot_chat_graph *,uint32_t,bot_chat_graph_kind,uint32_t,uint32_t,qa_error *);
bool bot_chat_graph_link(const bot_chat_graph *,uint32_t,bot_chat_graph_kind,uint32_t,bot_chat_graph_kind,uint32_t *,qa_error *);
bool bot_chat_graph_text(const bot_chat_graph *,uint32_t,const char **,qa_error *);
bool bot_chat_graph_free_pieces(bot_chat_graph *,uint32_t,qa_error *);
bool bot_chat_graph_free_root(bot_chat_graph *,bot_chat_graph_kind,qa_error *);
bool bot_chat_graph_copy(const bot_chat_graph *,const qa_bot_memory_prepared *,bot_chat_graph *,qa_error *);
bool bot_chat_graph_fields(qa_source_save_io *,bot_chat_graph *);
bool bot_chat_graph_project(qa_bot_chat_asset *,qa_error *);
bool bot_chat_graph_message_text(const qa_bot_chat_asset *,uint32_t,const char **,qa_error *);
bool bot_chat_graph_message_time(qa_bot_chat_asset *,uint32_t,float *,bool,qa_error *);
bool bot_chat_graph_match(const qa_bot_chat_asset *,uint32_t,qa_bot_chat_match *,bool *,qa_error *);
bool bot_chat_graph_message_next(const qa_bot_chat_asset *,bot_chat_graph_messages *,qa_error *);
#endif
