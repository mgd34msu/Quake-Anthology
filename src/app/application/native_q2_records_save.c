#include "native_q2_records_private.h"

typedef struct nqr_range { qa_native_address base; uint64_t bytes; } nqr_range;
bool application_native_q2_records_validate(application_native_q2_records *o,qa_error *e)
{
    if(!nqr_current(o,e)||!application_native_q2_records_idle(o)) return false;
    nqr_range *ranges=NULL; size_t count=0; bool ok=true;
    for(size_t i=0;ok&&i<o->record_count;++i) {
        nqr_record *record=o->records+i;
        qa_json_id row=qa_json_at(o->document,qa_json_get(o->document,qa_json_root(o->document),"actorRecords"),i);
        bool clients=qa_json_string_equal(o->document,qa_json_get(o->document,qa_json_get(o->document,row,"base"),"kind"),"clients");
        qa_native_address array_base=0; uint64_t array_bytes=0;
        ok=o->options.record_validate(o->options.context,(size_t)(record-o->records)+1,&array_base,&array_bytes,e);
        if(ok&&(clients?(array_base||array_bytes):(!array_base||array_bytes!=(uint64_t)record->stride*record->capacity)))
            ok=nqr_fail(e,QA_ERROR_FORMAT,"Native record validation differs from its actual declared array");
        uint32_t rows=clients?record->capacity:1;
        for(uint32_t j=0;ok&&j<rows;++j) {
            qa_native_address base=array_base;
            if(clients) ok=o->options.record_source(o->options.context,(size_t)(record-o->records)+1,j,&base,e);
            uint64_t length=clients?record->stride:array_bytes;
            if(ok&&(!base||base>UINT64_MAX-length)) ok=nqr_fail(e,QA_ERROR_FORMAT,"Native declared source array extent overflows");
            for(size_t k=0;ok&&k<count;++k) if(base<ranges[k].base+ranges[k].bytes&&ranges[k].base<base+length)
                ok=nqr_fail(e,QA_ERROR_FORMAT,"Native declared actor arrays overlap actual source storage");
            if(ok&&length&&(uint64_t)SIZE_MAX/length==0) ok=nqr_fail(e,QA_ERROR_FORMAT,"Native declared array exceeds the host extent");
            if(ok) ok=qa_native_range_check(o->options.instance,base,(size_t)length,QA_NATIVE_MEMORY_WRITE,e);
            if(!ok) break;
            if(count==SIZE_MAX/sizeof(*ranges)) { ok=nqr_fail(e,QA_ERROR_MEMORY,"Native array range roster overflows"); break; }
            nqr_range *next=realloc(ranges,(count+1)*sizeof(*ranges));
            if(!next) { ok=nqr_fail(e,QA_ERROR_MEMORY,"Qualifying actual native actor arrays"); break; }
            ranges=next; ranges[count++]=(nqr_range){base,length};
        }
    }
    free(ranges); return ok&&nqr_current(o,e);
}
static bool fields(application_native_q2_records *o,qa_source_save_io *io,nqr_actor **candidate)
{
    uint8_t magic[4]={'Q','N','R','B'};
    qa_native_module_info info=qa_native_module_describe(qa_native_get_module(o->options.instance));
    uint32_t profile=info.profile,abi=info.image.target.abi;
    uint8_t pointer_bytes=info.image.target.pointer_bytes;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,"QNRB",4)||
        !qa_source_save_u32(io,&profile)||profile!=(uint32_t)info.profile||!qa_source_save_u32(io,&abi)||abi!=(uint32_t)info.image.target.abi||
        !qa_source_save_u8(io,&pointer_bytes)||pointer_bytes!=info.image.target.pointer_bytes)
        return nqr_fail(io->error,QA_ERROR_FORMAT,"Native borrowed actor continuation differs from its actual module ABI");
    qa_bytes declaration=qa_json_source(o->document,qa_json_root(o->document));
    size_t bytes=declaration.size;
    if(!qa_source_save_count(io,&bytes,declaration.size)||bytes!=declaration.size) return nqr_fail(io->error,QA_ERROR_FORMAT,"Native borrowed actor declaration extent changed");
    if(io->direction==QA_SOURCE_SAVE_READ) {
        if(bytes>io->input.size-io->offset||memcmp(io->input.data+io->offset,declaration.data,bytes)) return nqr_fail(io->error,QA_ERROR_FORMAT,"Native borrowed actors belong to another retained declaration");
        io->offset+=bytes;
    } else if(!qa_source_save_bytes(io,(void *)declaration.data,bytes)) return false;
    size_t count=0;
    if(io->direction==QA_SOURCE_SAVE_WRITE) for(nqr_actor *r=o->actors;r;r=r->next) ++count;
    if(!qa_source_save_count(io,&count,65536)) return false;
    nqr_actor *read=NULL,**tail=&read;
    nqr_actor *source=o->actors;
    for(size_t i=0;i<count;++i) {
        nqr_actor row={0};
        if(io->direction==QA_SOURCE_SAVE_WRITE) { row=*source; source=source->next; }
        if(!qa_source_save_actor(io,&row.actor)||!row.actor.registry||!qa_source_save_u32(io,&row.index)||!qa_source_save_bool(io,&row.client)) goto failed;
        if(row.retired||row.failed||row.index>=65536||(row.client?row.index>=o->client_maximum:row.index<o->client_maximum)) {
            nqr_fail(io->error,QA_ERROR_FORMAT,"Native saved projection has no declared actor/client index"); goto failed;
        }
        for(size_t j=0;j<o->record_count;++j) if((!o->records[j].client||row.client)&&row.index>=o->records[j].capacity) {
            nqr_fail(io->error,QA_ERROR_FORMAT,"Native saved actor exceeds its actual auxiliary declaration"); goto failed;
        }
        if(o->entity_record!=SIZE_MAX&&(uint64_t)o->records[o->entity_record].first+row.index>UINT32_MAX) {
            nqr_fail(io->error,QA_ERROR_FORMAT,"Native saved entity projection slot overflows"); goto failed;
        }
        nqr_actor *previous=io->direction==QA_SOURCE_SAVE_READ?read:o->actors;
        size_t preceding=0;
        for(nqr_actor *r=previous;r&&preceding<i;r=r->next,++preceding) if(r->index==row.index||qa_actor_id_equal(r->actor,row.actor)) {
            nqr_fail(io->error,QA_ERROR_FORMAT,"Native borrowed actor continuation duplicates a full actor or private index"); goto failed;
        }
        if(io->direction==QA_SOURCE_SAVE_READ) {
            nqr_actor *next=calloc(1,sizeof(*next)); if(!next) { nqr_fail(io->error,QA_ERROR_MEMORY,"Retaining decoded native full actor mapping"); goto failed; }
            *next=row; next->next=NULL; *tail=next; tail=&next->next;
        } else if(!nqr_actor_current(o,&row,io->error)||!nqr_capacity(o,&row,io->error)) goto failed;
    }
    *candidate=read; return true;
failed:
    while(read) { nqr_actor *row=read; read=row->next; free(row); }
    return false;
}
bool application_native_q2_records_checkpoint(application_native_q2_records *o,qa_buffer *out,qa_error *e)
{
    if(!out||out->data||out->size||!nqr_current(o,e)||!application_native_q2_records_idle(o)||o->restoring||o->closing)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native record capture requires its actual returned source owner");
    if(!nqr_releases(o,e)||!application_native_q2_records_refresh(o,e)) return false;
    qa_source_save_io io={0}; nqr_actor *unused=NULL;
    bool ok=qa_source_save_writer(&io,o->options.session,e)&&fields(o,&io,&unused)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_native_q2_records_restore(application_native_q2_records *o,qa_bytes bytes,qa_error *e)
{
    if(!nqr_current(o,e)||!application_native_q2_records_idle(o)||o->actors||o->restoring||o->closing)
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native record decode requires its empty actual module owner");
    qa_source_save_io io={0}; nqr_actor *candidate=NULL;
    bool ok=qa_source_save_reader(&io,o->options.session,bytes,e)&&fields(o,&io,&candidate)&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(ok) { o->actors=candidate; o->restoring=true; }
    else while(candidate) { nqr_actor *row=candidate; candidate=row->next; free(row); }
    return ok;
}
bool application_native_q2_records_finish_restore(application_native_q2_records *o,qa_error *e)
{
    if(!o||!o->restoring||!application_native_q2_records_idle(o)||!application_native_q2_records_validate(o,e))
        return nqr_fail(e,QA_ERROR_ARGUMENT,"Native restored projection requires its completed original RAM restore");
    for(nqr_actor *r=o->actors;r;r=r->next) {
        uint32_t index=0; bool client=false;
        if(!nqr_live(o,r->actor)||!o->options.client_slot(o->options.context,r->actor,&index,&client,e)||r->client!=client||(client&&index!=r->index)||!nqr_capacity(o,r,e)) return false;
    }
    /* Publish links only after the complete mapping passed qualification. The
     * original module owns all other bytes, including private constants. */
    ++o->lifecycle_depth;
    bool ok=true;
    for(nqr_actor *r=o->actors;ok&&r;r=r->next) {
        if(o->entity_record!=SIZE_MAX&&!r->bound) {
            uint64_t slot=(uint64_t)o->records[o->entity_record].first+r->index;
            ok=slot<=UINT32_MAX&&o->options.bound(o->options.context,r->actor,(uint32_t)slot,e);
            if(ok) r->bound=true;
        }
        if(ok) ok=nqr_seed(o,r,false,e);
    }
    --o->lifecycle_depth;
    if(ok) ok=application_native_q2_records_refresh(o,e);
    if(ok) o->restoring=false;
    return ok;
}
