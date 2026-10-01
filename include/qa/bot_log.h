#ifndef QA_BOT_LOG_H
#define QA_BOT_LOG_H
#include "qa/bot_library.h"

typedef struct qa_bot_log qa_bot_log;
typedef struct qa_bot_log_file qa_bot_log_file;
typedef struct qa_bot_log_stream {
    void *context;
    bool (*write)(void *,qa_bytes,qa_error *);
    bool (*flush)(void *,qa_error *);
    /* Consumes the retained stream context even when native close fails. */
    bool (*close)(void *,qa_error *);
    bool (*checkpoint)(void *,uint64_t *,qa_error *);
} qa_bot_log_stream;
typedef struct qa_bot_log_services {
    void *context;
    bool (*open)(void *,const char *,bool resume,uint64_t position,qa_bot_log_stream *,qa_error *);
    bool (*print)(void *,qa_script_severity,const char *,qa_error *);
} qa_bot_log_services;

bool qa_bot_log_create(const qa_bot_log_services *,qa_bot_log **,qa_error *);
bool qa_bot_log_can_destroy(const qa_bot_log *);
void qa_bot_log_destroy(qa_bot_log *);
bool qa_bot_log_open(qa_bot_log *,qa_bot_library *,const char *,bool *succeeded,qa_error *);
bool qa_bot_log_close(qa_bot_log *,bool *succeeded,qa_error *);
bool qa_bot_log_write(qa_bot_log *,const char *,qa_error *);
bool qa_bot_log_write_timestamped(qa_bot_log *,float,const char *,qa_error *);
bool qa_bot_log_flush(qa_bot_log *,qa_error *);
qa_bot_log_file *qa_bot_log_file_pointer(qa_bot_log *);
bool qa_bot_log_file_write(qa_bot_log_file *,const char *,int64_t *written,qa_error *);
bool qa_bot_log_capture(const qa_bot_log *,qa_buffer *,qa_error *);
bool qa_bot_log_restore(qa_bot_log *,qa_bytes,qa_error *);
#endif
