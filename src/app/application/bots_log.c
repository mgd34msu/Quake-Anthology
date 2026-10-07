#include "bots_log.h"
#include "qa/filesystem.h"
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <pwd.h>
#include <unistd.h>
#endif

typedef struct application_bot_log_stream {
    qa_fs_root *root;
    qa_fs_stream *file;
    uint64_t position;
} application_bot_log_stream;

static bool write_stream(void *context,qa_bytes bytes,qa_error *error)
{
    application_bot_log_stream *stream=context;size_t offset=0;
    while(offset<bytes.size) {
        size_t written=0;uint64_t size;
        bool okay=qa_fs_stream_write(stream->file,(qa_bytes){bytes.data+offset,bytes.size-offset},
            stream->position,&written,&size,error);
        if(written>bytes.size-offset || written>UINT64_MAX-stream->position)
            return application_fail(error,QA_ERROR_IO,"Bot log write returned invalid actual progress");
        stream->position+=written;offset+=written;
        if(!okay) return false;
        if(!written) return application_fail(error,QA_ERROR_IO,"Bot log write made no progress");
    }
    return true;
}
static bool flush(void *context,qa_error *error)
{return qa_fs_stream_sync(((application_bot_log_stream *)context)->file,error);}
static bool close_stream(void *context,qa_error *error)
{
    application_bot_log_stream *stream=context;
    bool okay=qa_fs_stream_close_checked(stream->file,error);
    qa_fs_root_close(stream->root);free(stream);return okay;
}
static bool checkpoint(void *context,uint64_t *position,qa_error *error)
{
    application_bot_log_stream *stream=context;
    if(!stream || !position) return application_fail(error,QA_ERROR_ARGUMENT,"Bot log checkpoint has no retained stream");
    *position=stream->position;return true;
}

static char *directory(qa_error *error)
{
#if defined(_WIN32)
    const char *base=getenv("LOCALAPPDATA"),*suffix="/quake-anthology/bots";
    if(!base || !*base) {base=getenv("USERPROFILE");suffix="/.local/state/quake-anthology/bots";}
#else
    const char *base=getenv("XDG_STATE_HOME"),*suffix="/quake-anthology/bots";
    if(!base || !*base) {
        base=getenv("HOME");suffix="/.local/state/quake-anthology/bots";
        if(!base || !*base) {struct passwd *user=getpwuid(getuid());base=user?user->pw_dir:NULL;}
    }
#endif
    if(!base || !*base) {application_fail(error,QA_ERROR_NOT_FOUND,"Bot log has no actual user state directory");return NULL;}
    size_t a=strlen(base),b=strlen(suffix);
    if(a>SIZE_MAX-b-1) {application_fail(error,QA_ERROR_MEMORY,"Bot log state path length overflow");return NULL;}
    char *path=malloc(a+b+1);
    if(!path) {application_fail(error,QA_ERROR_MEMORY,"Allocating actual bot log state path");return NULL;}
    memcpy(path,base,a);memcpy(path+a,suffix,b+1);return path;
}

static bool open_stream(void *context,const char *filename,bool resume,uint64_t position,
    qa_bot_log_stream *out,qa_error *error)
{
    (void)context;
    if(!filename || !out || position>UINT64_C(9007199254740991))
        return application_fail(error,QA_ERROR_ARGUMENT,"Bot log open has invalid source filename or saved position");
    const char *name=filename;
    for(const char *cursor=filename;*cursor;++cursor)
        if(*cursor=='/'
#if defined(_WIN32)
           || *cursor=='\\'
#endif
        ) name=cursor+1;
    char *path=directory(error);if(!path) return false;
    application_bot_log_stream *stream=calloc(1,sizeof(*stream));
    if(!stream) {free(path);return application_fail(error,QA_ERROR_MEMORY,"Allocating retained bot log descriptor owner");}
    bool okay=(resume || qa_fs_path_create_directory(path,error)) && qa_fs_root_open(path,&stream->root,error);
    free(path);uint64_t size;
    if(okay) okay=qa_fs_root_stream_open(stream->root,name,QA_FS_STREAM_WRITE,resume,&stream->file,&size,error);
    if(okay && resume && size<position)
        okay=application_fail(error,QA_ERROR_FORMAT,"Saved bot log position exceeds the retained file");
    if(!okay) {qa_fs_stream_close(stream->file);qa_fs_root_close(stream->root);free(stream);return false;}
    stream->position=resume?position:0;
    *out=(qa_bot_log_stream){.context=stream,.write=write_stream,.flush=flush,.close=close_stream,.checkpoint=checkpoint};
    return true;
}

static bool print(void *context,qa_script_severity severity,const char *text,qa_error *error)
{
    (void)severity;application_bots *bots=context;
    if(!bots || !text) return application_fail(error,QA_ERROR_ARGUMENT,"Bot log diagnostic has no actual application owner");
    size_t length=strlen(text);
    application_console_print(bots->application,NULL,text);
    if(length && text[length-1]!='\n') application_console_print(bots->application,NULL,"\n");
    return true;
}

qa_bot_log_services application_bots_log_services(application_bots *bots)
{return (qa_bot_log_services){.context=bots,.open=open_stream,.print=print};}
