#include "log_internal.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool bot_log_fail(qa_error *error,const char *message)
{qa_error_set(error,QA_ERROR_ARGUMENT,0,"%s",message);return false;}

static bool idle(qa_bot_log *log,qa_error *error)
{return log && !log->busy?true:bot_log_fail(error,"Bot log owner is absent or executing a callback");}

static bool stored_name(qa_bot_log *log,qa_error *error)
{return log->filename_length<sizeof(log->filename)?true:bot_log_fail(error,"Log_Open: source stored filename is not NUL-terminated");}

static bool diagnostic(qa_bot_log *log,qa_script_severity severity,const char *prefix,
    const char *name,const char *suffix,qa_error *error)
{
    if(!log->services.print) return true;
    size_t a=strlen(prefix),b=strlen(name),c=strlen(suffix);
    if(a>SIZE_MAX-b || a+b>SIZE_MAX-c-1) return bot_log_fail(error,"Bot log diagnostic length overflow");
    char *text=malloc(a+b+c+1);
    if(!text) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating bot log diagnostic");return false;}
    memcpy(text,prefix,a);memcpy(text+a,name,b);memcpy(text+a+b,suffix,c+1);
    bool okay=log->services.print(log->services.context,severity,text,error);free(text);return okay;
}

qa_bot_log_file *bot_log_entry(qa_bot_log *log,const qa_bot_log_stream *stream,qa_error *error)
{
    qa_bot_log_file *entry=calloc(1,sizeof(*entry));
    if(!entry) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating genuine bot log FILE borrow");return NULL;}
    entry->owner=log;entry->next=log->entries;
    if(stream) {entry->stream=*stream;entry->live=true;}
    log->entries=entry;return entry;
}

bool qa_bot_log_create(const qa_bot_log_services *services,qa_bot_log **out,qa_error *error)
{
    if(!services || !out || *out) return bot_log_fail(error,"Bot log creation requires an empty owner");
    qa_bot_log *log=calloc(1,sizeof(*log));
    if(!log) {qa_error_set(error,QA_ERROR_MEMORY,0,"Allocating bot log owner");return false;}
    log->services=*services;*out=log;return true;
}
bool qa_bot_log_can_destroy(const qa_bot_log *log)
{return !log || !log->busy;}

bool qa_bot_log_print(qa_bot_log *log,qa_script_severity severity,const char *text,qa_error *error)
{
    if(!idle(log,error) || !text) return bot_log_fail(error,"Bot Print requires its retained owner and source text");
    log->busy=true;
    bool ok=!log->services.print || log->services.print(log->services.context,severity,text,error);
    log->busy=false;return ok;
}

void qa_bot_log_destroy(qa_bot_log *log)
{
    if(!log) return;
    if(log->busy) return;
    if(log->current && log->current->live) {
        qa_error ignored={0};log->busy=true;
        (void)log->current->stream.close(log->current->stream.context,&ignored);
        log->current->live=false;log->busy=false;
    }
    while(log->entries) {qa_bot_log_file *entry=log->entries;log->entries=entry->next;free(entry);}
    free(log->opened_filename);free(log);
}

bool qa_bot_log_open(qa_bot_log *log,qa_bot_library *library,const char *filename,bool *succeeded,qa_error *error)
{
    if(!idle(log,error) || !library || !succeeded) return false;
    *succeeded=true;
    const qa_bot_variable *enabled;
    if(!qa_bot_library_variable_default(library,"log","0",&enabled,error)) return false;
    if(enabled->value==0) return true;
    const char *name=filename?filename:"";log->busy=true;bool okay=true;
    if(!*name) okay=diagnostic(log,QA_SCRIPT_INFO,"openlog <filename>\n","","",error);
    else if(log->current) okay=stored_name(log,error) &&
        diagnostic(log,QA_SCRIPT_ERROR,"log file ",log->filename," is already opened\n",error);
    else {
        qa_bot_log_stream stream={0};qa_error io={0};
        bool opened=log->services.open && log->services.open(log->services.context,name,false,0,&stream,&io);
        if(!opened) {
            *succeeded=false;okay=diagnostic(log,QA_SCRIPT_ERROR,"can't open the log file ",name,"\n",error);
        } else if(!stream.write || !stream.flush || !stream.close || !stream.checkpoint) {
            if(stream.close) (void)stream.close(stream.context,&io);
            okay=bot_log_fail(error,"Bot log host returned an incomplete actual stream");
        } else {
            size_t length=strlen(name);
            char *opened_name=malloc(length+1);
            qa_bot_log_file *entry=opened_name?bot_log_entry(log,&stream,error):NULL;
            if(!entry) {
                free(opened_name);(void)stream.close(stream.context,&io);
                if(!error || error->code==QA_OK) qa_error_set(error,QA_ERROR_MEMORY,0,"Retaining opened bot log filename");
                okay=false;
            } else {
                memcpy(opened_name,name,length+1);free(log->opened_filename);log->opened_filename=opened_name;
                log->filename_length=length<sizeof(log->filename)?length:sizeof(log->filename);
                memcpy(log->filename,name,log->filename_length);
                if(log->filename_length<sizeof(log->filename)) log->filename[log->filename_length]=0;
                log->current=entry;
                okay=stored_name(log,error) && diagnostic(log,QA_SCRIPT_INFO,"Opened log ",log->filename,"\n",error);
            }
        }
    }
    log->busy=false;return okay;
}

bool qa_bot_log_close(qa_bot_log *log,bool *succeeded,qa_error *error)
{
    if(!idle(log,error) || !succeeded) return false;
    *succeeded=true;if(!log->current) return true;
    qa_bot_log_file *entry=log->current;
    if(!entry->live) return bot_log_fail(error,"Bot log source FILE is closed but its pointer is retained");
    log->busy=true;qa_error io={0};
    *succeeded=entry->stream.close(entry->stream.context,&io);entry->live=false;
    if(*succeeded) log->current=NULL;
    bool okay=stored_name(log,error) && diagnostic(log,*succeeded?QA_SCRIPT_INFO:QA_SCRIPT_ERROR,
        *succeeded?"Closed log ":"can't close log file ",log->filename,"\n",error);
    log->busy=false;return okay;
}

static bool current(qa_bot_log *log,qa_error *error)
{
    return !log->current || log->current->live?true:
        bot_log_fail(error,"Bot log source FILE is closed but its pointer is retained");
}

static bool output(qa_bot_log *log,const char *text,qa_error *error)
{
    if(!text) return bot_log_fail(error,"Bot log output text is absent");
    if(!log->current) return true;
    if(!current(log,error)) return false;
    qa_error ignored={0};qa_bot_log_file *entry=log->current;
    (void)entry->stream.write(entry->stream.context,(qa_bytes){(const uint8_t *)text,strlen(text)},&ignored);
    return true;
}

bool qa_bot_log_write(qa_bot_log *log,const char *text,qa_error *error)
{
    if(!idle(log,error)) return false;
    log->busy=true;bool okay=output(log,text,error);
    if(okay && log->current) {qa_error ignored={0};(void)log->current->stream.flush(log->current->stream.context,&ignored);}
    log->busy=false;return okay;
}

bool qa_bot_log_flush(qa_bot_log *log,qa_error *error)
{
    if(!idle(log,error) || !current(log,error)) return false;
    if(log->current) {qa_error ignored={0};log->busy=true;(void)log->current->stream.flush(log->current->stream.context,&ignored);log->busy=false;}
    return true;
}

static bool source_integer(float value,int32_t *out,qa_error *error)
{
    double integer=trunc((double)value);
    if(!isfinite(integer) || integer<INT32_MIN || integer>INT32_MAX)
        return bot_log_fail(error,"Log_WriteTimeStamped: source float-to-int conversion is undefined");
    *out=(int32_t)integer;return true;
}

bool qa_bot_log_write_timestamped(qa_bot_log *log,float time,const char *text,qa_error *error)
{
    if(!idle(log,error)) return false;
    if(!log->current) return true;
    int32_t hours,minutes,seconds,centiseconds;
    volatile float first=time/60.0f,second=first/60.0f,hundred=time*100.0f;
    if(!source_integer(second,&hours,error) || !source_integer(first,&minutes,error) ||
       !source_integer(time,&seconds,error) || !source_integer(hundred,&centiseconds,error)) return false;
    int64_t whole=(int64_t)seconds*100,fraction=(int64_t)centiseconds-whole;
    if(whole<INT32_MIN || whole>INT32_MAX || fraction<INT32_MIN || fraction>INT32_MAX)
        return bot_log_fail(error,"Log_WriteTimeStamped: source signed arithmetic would overflow");
    if(!current(log,error)) return false;
    char prefix[96];snprintf(prefix,sizeof(prefix),"%d   %02d:%02d:%02d:%02d   ",
        log->numwrites,hours,minutes,seconds,(int32_t)fraction);
    log->busy=true;bool okay=output(log,prefix,error) && output(log,text,error) && output(log,"\r\n",error);
    if(okay && log->numwrites==INT32_MAX) okay=bot_log_fail(error,"Log_WriteTimeStamped: source numwrites increment would overflow");
    if(okay) {++log->numwrites;qa_error ignored={0};(void)log->current->stream.flush(log->current->stream.context,&ignored);}
    log->busy=false;return okay;
}

qa_bot_log_file *qa_bot_log_file_pointer(qa_bot_log *log)
{return log?log->current:NULL;}

bool qa_bot_log_file_write(qa_bot_log_file *file,const char *text,int64_t *written,qa_error *error)
{
    qa_bot_log *log=file?file->owner:NULL;
    if(!idle(log,error) || !text || !written || log->current!=file)
        return bot_log_fail(error,"Cannot write a closed bot log borrow");
    if(!current(log,error)) return false;
    size_t size=strlen(text);
    if(size>INT64_MAX) return bot_log_fail(error,"Bot log FILE output exceeds its signed source result");
    log->busy=true;qa_error io={0};
    bool okay=file->stream.write(file->stream.context,(qa_bytes){(const uint8_t *)text,size},&io);
    *written=okay?(int64_t)size:-1;log->busy=false;return true;
}
