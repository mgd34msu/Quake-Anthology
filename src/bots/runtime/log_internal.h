#ifndef QA_BOT_LOG_INTERNAL_H
#define QA_BOT_LOG_INTERNAL_H
#include "qa/bot_log.h"

typedef enum bot_log_phase {BOT_LOG_CLOSED,BOT_LOG_OPEN,BOT_LOG_CLOSED_RETAINED} bot_log_phase;
struct qa_bot_log_file {
    qa_bot_log *owner;
    qa_bot_log_stream stream;
    bool live;
    struct qa_bot_log_file *next;
};
struct qa_bot_log {
    qa_bot_log_services services;
    qa_bot_log_file *current,*entries;
    char filename[1024];
    size_t filename_length;
    char *opened_filename;
    int32_t numwrites;
    bool busy;
};
bool bot_log_fail(qa_error *,const char *);
qa_bot_log_file *bot_log_entry(qa_bot_log *,const qa_bot_log_stream *,qa_error *);
#endif
