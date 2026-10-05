#include "remote_unified_prediction_save.h"
#include "remote_unified_prediction_private.h"
#include "remote_unified_private.h"
#include "remote_unified_save.h"
#include "qa/source_save.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct saved_prediction {
    uint32_t epoch;
    bool received;
    int64_t discarded;
    uint64_t authoritative_frame;
    qa_unified_document *snapshot;
    qa_unified_input_batch commands;
    double times[64];
} saved_prediction;
static bool document(qa_source_save_io *io,qa_unified_document_kind kind,qa_unified_document **doc)
{
    qa_buffer bytes={0};
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    bool ok=read || qa_unified_document_encode(*doc,&bytes,io->error);
    size_t size=bytes.size;
    if(ok) ok=qa_source_save_count(io,&size,SIZE_MAX);
    if(ok && read) {
        ok=size && size<=io->input.size-io->offset &&
            qa_unified_document_decode(kind,(qa_bytes){io->input.data+io->offset,size},doc,io->error);
        if(ok) io->offset+=size;
    } else if(ok) ok=qa_source_save_bytes(io,bytes.data,size);
    qa_buffer_free(&bytes);return ok;
}
static bool fields(qa_source_save_io *io,saved_prediction *s)
{
    uint8_t tag[8]={'Q','U','P','R',2,0,0,0};
    const uint8_t expected[8]={'Q','U','P','R',2,0,0,0};
    if(!qa_source_save_bytes(io,tag,8) || memcmp(tag,expected,8) ||
        !qa_source_save_u32(io,&s->epoch) || !s->epoch ||
        !qa_source_save_bool(io,&s->received) || !qa_source_save_i64(io,&s->discarded) ||
        s->discarded < -1 || s->discarded>(int64_t)QA_UNIFIED_SAFE_INTEGER) return false;
    if(!s->received) return s->discarded==-1;
    if(!qa_source_save_u64(io,&s->authoritative_frame)||s->authoritative_frame>QA_UNIFIED_SAFE_INTEGER) return false;
    if(!document(io,QA_UNIFIED_FRAME_DOCUMENT,&s->snapshot)) return false;
    qa_unified_document *commands=NULL;
    bool read=io->direction==QA_SOURCE_SAVE_READ;
    bool ok=read || qa_unified_inputs_document(s->epoch,s->commands.commands,s->commands.count,&commands,io->error);
    if(ok) ok=document(io,QA_UNIFIED_INPUT_DOCUMENT,&commands);
    if(ok && read) ok=qa_unified_inputs_read(commands,&s->commands,io->error) && s->commands.epoch==s->epoch;
    qa_unified_document_destroy(commands);
    for(size_t i=0;ok && i<s->commands.count;++i) {
        ok=!s->commands.commands[i].has_arsenal && qa_source_save_f64(io,&s->times[i]) && isfinite(s->times[i]);
        if(i && s->commands.commands[i].sequence<=s->commands.commands[i-1].sequence) ok=false;
    }
    if(ok && s->commands.count && s->discarded >= 0 &&
        (uint64_t)s->discarded>=s->commands.commands[0].sequence) ok=false;
    return ok;
}
bool frontend_remote_unified_prediction_checkpoint(const frontend_remote_unified_prediction *p,
    qa_buffer *out,qa_error *e)
{
    if(!p || !out || out->data || !frontend_remote_unified_prediction_idle(p) ||
        p->importing || !frontend_remote_unified_checkpoint_current(p->replica,e) ||
        p->epoch!=frontend_remote_unified_epoch(p->replica) || (p->received && !p->snapshot_document)) return false;
    saved_prediction s={.epoch=p->epoch,.received=p->received,.discarded=p->discarded,.snapshot=p->snapshot_document,
        .authoritative_frame=p->authoritative_frame};
    s.commands.epoch=p->epoch;s.commands.count=p->command_count;
    for(size_t i=0;i<p->command_count;++i) {
        s.commands.commands[i]=(qa_unified_input){.sequence=p->commands[i].sequence,.command=p->commands[i].raw};
        s.times[i]=p->commands[i].time_ms;
    }
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,e) && fields(&io,&s) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);return ok;
}
bool frontend_remote_unified_prediction_restore(frontend_remote_unified *replica,qa_bytes bytes,
    frontend_remote_unified_prediction **out,qa_error *e)
{
    if(!replica || !out || *out) return false;
    saved_prediction s={0};qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,e) && fields(&io,&s) && qa_source_save_finish(&io,NULL) &&
        s.epoch==frontend_remote_unified_epoch(replica);
    qa_source_save_dispose(&io);
    frontend_remote_unified_prediction *p=NULL;
    if(ok) ok=frontend_prediction_import_create(replica,&p,e);
    if(ok && s.received) ok=frontend_remote_unified_prediction_receive(p,s.snapshot,e);
    if(ok&&s.received) ok=p->authoritative_frame==s.authoritative_frame;
    for(size_t i=0;ok && i<s.commands.count;++i) {
        ok=(int64_t)s.commands.commands[i].sequence>p->snapshot.sequence &&
            frontend_remote_unified_prediction_input(p,&s.commands.commands[i],s.times[i],e);
    }
    if(ok) {p->discarded=s.discarded;p->importing=false;*out=p;p=NULL;}
    if(p) (void)frontend_remote_unified_prediction_destroy(&p,NULL);
    qa_unified_document_destroy(s.snapshot);qa_unified_inputs_free(&s.commands);
    if(!ok && (!e || e->code==QA_OK)) frontend_unified_fail(e,QA_ERROR_FORMAT,"Invalid retained Unified prediction history");
    return ok;
}
