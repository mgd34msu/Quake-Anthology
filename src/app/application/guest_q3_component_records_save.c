#include "guest_q3_component_records_private.h"
#include "qa/source_save.h"

static bool fields(application_q3_component_records *r,qa_source_save_io *io)
{
    uint8_t magic[4]={'Q','G','C','R'},expected[4]={'Q','G','C','R'};
    uint32_t version=1; size_t count=r->record_count;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,expected,4)||!qa_source_save_u32(io,&version)||version!=1||
        !qa_source_save_bool(io,&r->defaults_ready)||!r->defaults_ready||
        !qa_source_save_count(io,&count,r->record_count)||count!=r->record_count)
        return q3records_fail(io->error,QA_ERROR_FORMAT,"Component record continuation differs from its actual roster");
    for(size_t i=0;i<count;++i) {
        component_record *record=r->records+i;
        const char *id=record->id; uint32_t address=record->address,stride=record->stride,capacity=record->capacity;
        size_t bytes=(size_t)stride*capacity;
        if(!qa_source_save_text(io,&id)||!id||strcmp(id,record->id)||!qa_source_save_u32(io,&address)||address!=record->address||
            !qa_source_save_u32(io,&stride)||stride!=record->stride||!qa_source_save_u32(io,&capacity)||capacity!=record->capacity||
            !qa_source_save_count(io,&bytes,(size_t)record->stride*record->capacity)||bytes!=(size_t)record->stride*record->capacity)
            return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved defaults do not name the retained source array");
        if(io->direction==QA_SOURCE_SAVE_READ) {
            record->defaults.data=bytes?malloc(bytes):NULL; record->defaults.size=bytes;
            if(bytes&&!record->defaults.data) return q3records_fail(io->error,QA_ERROR_MEMORY,"Retaining actual component defaults");
        }
        if(record->defaults.size!=bytes||!qa_source_save_bytes(io,record->defaults.data,bytes)) return false;
    }
    size_t actors=r->actor_count;
    if(!qa_source_save_count(io,&actors,1022)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        r->actors=actors?calloc(actors,sizeof(*r->actors)):NULL; r->actor_count=r->actor_capacity=actors;
        if(actors&&!r->actors) return q3records_fail(io->error,QA_ERROR_MEMORY,"Retaining actual component actor continuation");
    }
    for(size_t i=0;i<actors;++i) {
        component_actor *row=r->actors+i;
        if(!qa_source_save_actor(io,&row->actor)||!qa_source_save_u32(io,&row->slot)||
            !qa_source_save_bool(io,&row->owned)||!qa_source_save_bool(io,&row->client)||
            !qa_source_save_bool(io,&row->admitted)||!qa_source_save_bool(io,&row->projected)||
            !qa_source_save_bool(io,&row->disconnected)) return false;
        const qa_actor_record *actual=qa_actors_get(qa_session_actors(r->options.session),row->actor);
        if(!actual||row->retired||row->slot>=1022||
            (row->client?row->slot>=r->client_maximum:row->slot<r->client_maximum)||
            (row->owned&&(row->client||!row->projected||actual->owner==0||!actual->has_source||actual->source_slot!=row->slot))||
            (row->admitted&&(!row->client||!row->projected))||(!row->client&&row->disconnected))
            return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved projection lost its full actor or source admission");
        for(size_t j=0;j<i;++j) if(r->actors[j].slot==row->slot||qa_actor_id_equal(r->actors[j].actor,row->actor))
            return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved projections duplicate an actor or slot");
        for(size_t j=0;j<r->record_count;++j) if((!r->records[j].client||row->client)&&row->slot>=r->records[j].capacity)
            return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved projection exceeds an actual auxiliary array");
    }
    return true;
}
bool application_q3_component_records_checkpoint(application_q3_component_records *r,qa_buffer *out,qa_error *e)
{
    if(!r||!out||out->data||out->size||!application_q3_component_records_idle(r)||!r->defaults_ready)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component record capture requires its reached idle owner");
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,r->options.session,e)&&fields(r,&io)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_component_records_restore(application_q3_component_records *r,qa_bytes bytes,qa_error *e)
{
    if(!r||r->defaults_ready||r->actors||!application_q3_component_records_idle(r))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component record import requires its unentered actual constructor");
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,r->options.session,bytes,e)&&fields(r,&io)&&qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io); return ok;
}
bool application_q3_component_records_validate(application_q3_component_records *r,qa_error *e)
{
    if(!r||!application_q3_component_records_idle(r)||!r->options.storage_current(r->options.context,e)) return false;
    for(size_t i=0;i<r->actor_count;++i) {
        component_actor row=r->actors[i];
        if(row.retired||!q3records_live(r,row.actor)) return q3records_fail(e,QA_ERROR_FORMAT,"Component restored projection is retired");
    }
    return true;
}
