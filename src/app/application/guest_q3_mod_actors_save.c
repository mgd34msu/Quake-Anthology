#include "guest_q3_mod_actors_private.h"

static bool prefix(application_q3_mod_actors *o,qa_source_save_io *io)
{
    uint8_t magic[4]={'Q','G','M','A'},expected[4]; memcpy(expected,magic,4);
    uint32_t abi=(uint32_t)o->options.profile->abi;
    uint8_t digest[32],original[32]; memcpy(digest,qa_qvm_image_digest(o->options.profile->image),32); memcpy(original,digest,32);
    size_t length=o->options.profile->declaration.size;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,expected,4)||
        !qa_source_save_u32(io,&abi)||abi!=(uint32_t)o->options.profile->abi||!qa_source_save_bytes(io,digest,32)||memcmp(digest,original,32)||
        !qa_source_save_count(io,&length,length)||length!=o->options.profile->declaration.size)
        return q3mod_fail(io->error,QA_ERROR_FORMAT,"Actor continuation differs from its genuine executable declaration");
    if(io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,o->options.profile->declaration.data,length);
    if(io->offset>io->input.size||length>io->input.size-io->offset||memcmp(io->input.data+io->offset,o->options.profile->declaration.data,length))
        return q3mod_fail(io->error,QA_ERROR_FORMAT,"Actor continuation declaration differs from its retained component");
    io->offset+=length; return true;
}
static bool row_valid(application_q3_mod_actors *o,mod_actor_row *row,bool installed,qa_error *e)
{
    uint32_t pointer;
    const qa_actor_record *actual=qa_actors_get(qa_session_actors(o->options.session),row->actor);
    if(!actual||actual->owner!=o->options.owner||row->retired||!o->options.owned(o->options.context,row->actor)||
        !o->options.pointer(o->options.context,row->actor,&pointer,e)||pointer!=row->pointer||
        row->bound!=o->combat||(row->bound&&!row->serial)||(!row->bound&&row->serial))
        return q3mod_fail(e,QA_ERROR_FORMAT,"Saved actor semantic row has no exact owned projection");
    if(installed&&row->bound&&!qa_combat_primary_current(o->options.combat,row->actor,row->serial,row))
        return q3mod_fail(e,QA_ERROR_FORMAT,"Actor continuation has no matching canonical primary storage");
    return true;
}
bool application_q3_mod_actors_checkpoint(application_q3_mod_actors *o,qa_buffer *out,qa_error *e)
{
    if(!out||out->data||out->size||!application_q3_mod_actors_idle(o)||!q3mod_actors_current(o,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor checkpoint requires its genuine idle component");
    size_t count=0;
    for(mod_actor_row *r=o->actors;r;r=r->next) { if(!row_valid(o,r,true,e)) return false; ++count; }
    qa_source_save_io io; if(!qa_source_save_writer(&io,o->options.session,e)) return false;
    uint64_t sequence=o->sequence;
    bool ok=prefix(o,&io)&&qa_source_save_u64(&io,&sequence)&&qa_source_save_count(&io,&count,UINT32_MAX);
    for(mod_actor_row *r=o->actors;ok&&r;r=r->next) {
        qa_actor_id actor=r->actor; uint32_t pointer=r->pointer; uint64_t serial=r->serial; bool bound=r->bound;
        ok=qa_source_save_actor(&io,&actor)&&qa_source_save_u32(&io,&pointer)&&qa_source_save_u64(&io,&serial)&&qa_source_save_bool(&io,&bound);
    }
    if(ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_mod_actors_restore(application_q3_mod_actors *o,qa_bytes bytes,qa_error *e)
{
    if(!o||!o->restoring||o->actors||!application_q3_mod_actors_idle(o))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor restore requires its genuine detached child");
    qa_source_save_io io; if(!qa_source_save_reader(&io,o->options.session,bytes,e)) return false;
    size_t count=0; uint64_t sequence=0;
    size_t limit=o->present?o->options.profile->records[o->entity_record].capacity:0;
    bool ok=prefix(o,&io)&&qa_source_save_u64(&io,&sequence)&&qa_source_save_count(&io,&count,limit);
    mod_actor_row *head=NULL,**tail=&head;
    for(size_t i=0;ok&&i<count;++i) {
        mod_actor_row *row=calloc(1,sizeof(*row)); if(!row) { ok=q3mod_fail(e,QA_ERROR_MEMORY,"Retaining detached actor semantic rows"); break; }
        row->owner=o;
        ok=qa_source_save_actor(&io,&row->actor)&&qa_source_save_u32(&io,&row->pointer)&&qa_source_save_u64(&io,&row->serial)&&qa_source_save_bool(&io,&row->bound)&&row_valid(o,row,false,e);
        for(mod_actor_row *prior=head;ok&&prior;prior=prior->next)
            if(qa_actor_id_equal(prior->actor,row->actor)||prior->pointer==row->pointer||(row->serial&&prior->serial==row->serial))
                ok=q3mod_fail(e,QA_ERROR_FORMAT,"Saved actor semantics duplicate physical ownership");
        if(!ok) { free(row); break; }
        *tail=row; tail=&row->next;
    }
    if(ok) ok=qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if(!ok) { while(head) { mod_actor_row *next=head->next; free(head); head=next; } return false; }
    o->actors=head; o->sequence=sequence; return true;
}
bool application_q3_mod_actors_finish_restore(application_q3_mod_actors *o,qa_error *e)
{
    if(!o||!o->restoring||!application_q3_mod_actors_idle(o)||!q3mod_storage_current(o->options.mod,e))
        return q3mod_fail(e,QA_ERROR_ARGUMENT,"Actor restore finish has no genuine idle retained child");
    for(mod_actor_row *r=o->actors;r;r=r->next) if(!row_valid(o,r,true,e)) return false;
    o->restoring=false;
    return true;
}
