#include "log_internal.h"
#include "../save_fields.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t magic[8]={'Q','A','B','L','O','G',0,0};
typedef struct log_image {
    const char *filename,*opened_filename;
    uint32_t phase;
    int32_t numwrites;
    uint64_t position;
} log_image;

static bool signature(qa_source_save_io *io)
{
    uint8_t bytes[8];memcpy(bytes,magic,sizeof(bytes));uint32_t version=1;
    return qa_source_save_bytes(io,bytes,sizeof(bytes)) && !memcmp(bytes,magic,sizeof(bytes)) &&
        qa_source_save_u32(io,&version) && version==1?true:
        bot_save_fail(io,QA_ERROR_FORMAT,"Unsupported genuine bot log continuation schema");
}

static bool fields(qa_source_save_io *io,log_image *image)
{
    bool okay=bot_save_text(io,&image->filename) && bot_save_text(io,&image->opened_filename) &&
        qa_source_save_i32(io,&image->numwrites) && image->numwrites>=0 &&
        qa_source_save_u32(io,&image->phase) && image->phase<=BOT_LOG_CLOSED_RETAINED &&
        qa_source_save_u64(io,&image->position) && image->position<=UINT64_C(9007199254740991) &&
        (image->phase==BOT_LOG_OPEN || image->position==0) &&
        image->filename && image->opened_filename && strlen(image->filename)<=1024;
    if(okay && image->phase==BOT_LOG_OPEN) {
        size_t length=strlen(image->opened_filename);if(length>1024) length=1024;
        okay=*image->opened_filename && strlen(image->filename)==length &&
            !memcmp(image->filename,image->opened_filename,length);
    }
    if(!okay && !io->failed) return bot_save_fail(io,QA_ERROR_FORMAT,"Invalid actual bot log source state");
    return okay;
}

bool qa_bot_log_capture(const qa_bot_log *log,qa_buffer *out,qa_error *error)
{
    if(!log || !out || log->busy) return bot_log_fail(error,"Bot log capture requires its idle genuine owner");
    char filename[1025];memcpy(filename,log->filename,log->filename_length);filename[log->filename_length]=0;
    log_image image={.filename=filename,.opened_filename=log->opened_filename?log->opened_filename:"",
        .numwrites=log->numwrites,.phase=log->current?(log->current->live?BOT_LOG_OPEN:BOT_LOG_CLOSED_RETAINED):BOT_LOG_CLOSED};
    bool okay=true;
    if(image.phase==BOT_LOG_OPEN) {
        qa_bot_log *borrowed=(qa_bot_log *)log;borrowed->busy=true;
        okay=log->current->stream.checkpoint(log->current->stream.context,&image.position,error);
        borrowed->busy=false;
    }
    qa_source_save_io io={0};
    if(okay) okay=qa_source_save_writer(&io,NULL,error) && signature(&io) && fields(&io,&image) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return okay;
}

bool qa_bot_log_restore(qa_bot_log *log,qa_bytes bytes,qa_error *error)
{
    if(!log || log->busy || log->current || log->entries || log->opened_filename ||
       log->filename_length || log->numwrites)
        return bot_log_fail(error,"Bot log restoration requires a fresh isolated owner");
    qa_source_save_io io={0};log_image image={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && signature(&io) && fields(&io,&image) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    qa_bot_log_file *entry=NULL;
    if(okay && image.phase==BOT_LOG_OPEN) {
        qa_bot_log_stream stream={0};log->busy=true;
        okay=log->services.open && log->services.open(log->services.context,image.opened_filename,true,image.position,&stream,error);
        if(okay) {
            if(!stream.write || !stream.flush || !stream.close || !stream.checkpoint) {
                qa_error ignored={0};if(stream.close) (void)stream.close(stream.context,&ignored);
                okay=bot_log_fail(error,"Resumed bot log lacks its actual complete stream");
            } else {
                entry=bot_log_entry(log,&stream,error);okay=entry!=NULL;
                if(!okay) {qa_error ignored={0};(void)stream.close(stream.context,&ignored);}
            }
        } else if(!error || error->code==QA_OK) bot_log_fail(error,"Active bot log continuation requires a real resumable file host");
        log->busy=false;
    } else if(okay && image.phase==BOT_LOG_CLOSED_RETAINED) {
        entry=bot_log_entry(log,NULL,error);okay=entry!=NULL;
    }
    if(okay) {
        log->filename_length=strlen(image.filename);memcpy(log->filename,image.filename,log->filename_length);
        if(log->filename_length<sizeof(log->filename)) log->filename[log->filename_length]=0;
        log->opened_filename=(char *)image.opened_filename;image.opened_filename=NULL;
        log->numwrites=image.numwrites;log->current=entry;
    }
    free((void *)image.filename);free((void *)image.opened_filename);return okay;
}
