#include "q3_source_receipts.h"
#include <stdlib.h>
#include <string.h>

bool application_q3_source_receipts_prepare(application_q3_source_receipts *receipts, qa_error *error)
{
    if (receipts->snapshots) return true;
    receipts->snapshots=calloc(QA_Q3_PACKET_BACKUP,sizeof(*receipts->snapshots));
    if (receipts->snapshots) return true;
    qa_error_set(error,QA_ERROR_MEMORY,0,"Preparing Q3 snapshot actor receipts"); return false;
}
void application_q3_source_receipts_clear(application_q3_source_receipts *receipts)
{
    if (receipts->snapshots)
        memset(receipts->snapshots,0,QA_Q3_PACKET_BACKUP*sizeof(*receipts->snapshots));
}
void application_q3_source_receipts_destroy(application_q3_source_receipts *receipts)
{ free(receipts->snapshots); *receipts=(application_q3_source_receipts){0}; }
bool application_q3_source_receipts_capture(application_q3_source_receipts *receipts,
    const qa_q3_snapshot *snapshot, application_q3_receipt_actor actor, void *context, qa_error *error)
{
    if (!receipts->snapshots || snapshot->entity_count>QA_Q3_SNAPSHOT_ENTITIES) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Q3 snapshot actor receipts were not prepared"); return false;
    }
    application_q3_snapshot_receipt row={.message=snapshot->message_number,.time=snapshot->server_time,
        .player_number=(uint32_t)snapshot->player.clientNum,.count=snapshot->entity_count,.valid=true};
    if (!actor(context,row.player_number,&row.player,error)) return false;
    for (size_t i=0;i<row.count;++i) {
        row.entities[i].number=(uint32_t)snapshot->entities[i].number;
        if (!actor(context,row.entities[i].number,&row.entities[i].actor,error)) return false;
    }
    receipts->snapshots[(uint32_t)row.message&(QA_Q3_PACKET_BACKUP-1)]=row;
    return true;
}
void application_q3_source_receipts_delivered(application_q3_source_receipts *receipts,int32_t message)
{
    if (!receipts->snapshots) return;
    application_q3_snapshot_receipt *row=receipts->snapshots+((uint32_t)message&(QA_Q3_PACKET_BACKUP-1));
    if (row->valid && row->message==message) row->delivered=true;
}
static bool at(const application_q3_snapshot_receipt *row,uint32_t number,qa_actor_id *out)
{
    if (number==row->player_number) { *out=row->player; return true; }
    size_t first=0,last=row->count;
    while (first<last) {
        size_t middle=first+(last-first)/2;
        if (row->entities[middle].number<number) first=middle+1; else last=middle;
    }
    if (first==row->count || row->entities[first].number!=number) return false;
    *out=row->entities[first].actor; return true;
}

bool application_q3_source_receipts_read(const application_q3_source_receipts *receipts,
    uint32_t number,int32_t time,bool timed,qa_actor_id *out)
{
    *out=(qa_actor_id){0};
    if (!receipts->snapshots) return false;
    const application_q3_snapshot_receipt *selected=NULL,*initial=NULL;
    for (size_t i=0;i<QA_Q3_PACKET_BACKUP;++i) {
        const application_q3_snapshot_receipt *row=receipts->snapshots+i;
        if (!row->valid || !row->delivered) continue;
        if (!initial || row->message<initial->message) initial=row;
        if ((!timed || row->time<=time) && (!selected || row->message>selected->message)) selected=row;
    }
    /* Initial CG snapshots clamp cg.time forward to their actual server time. */
    if (!selected) selected=initial;
    if (!selected) return false;
    const application_q3_snapshot_receipt *matched=NULL;
    for (size_t i=0;i<QA_Q3_PACKET_BACKUP;++i) {
        const application_q3_snapshot_receipt *row=receipts->snapshots+i;
        if (!row->valid || !row->delivered || row->message>selected->message ||
            (matched && row->message<=matched->message)) continue;
        qa_actor_id actor;
        if (at(row,number,&actor)) { *out=actor; matched=row; }
    }
    return matched!=NULL && out->registry!=0;
}
void application_q3_source_receipts_visit(const application_q3_source_receipts *receipts,
    void (*visit)(void *,qa_actor_id),void *context)
{
    if (!receipts->snapshots) return;
    for (size_t i=0;i<QA_Q3_PACKET_BACKUP;++i) {
        const application_q3_snapshot_receipt *row=receipts->snapshots+i;
        if (!row->valid) continue;
        if (row->player.registry) visit(context,row->player);
        for (size_t j=0;j<row->count;++j)
            if (row->entities[j].actor.registry) visit(context,row->entities[j].actor);
    }
}
void application_q3_source_receipts_latest(const application_q3_source_receipts *receipts,
    qa_actor_id actors[QA_Q3_ENTITIES])
{
    memset(actors,0,QA_Q3_ENTITIES*sizeof(*actors));
    int32_t messages[QA_Q3_ENTITIES];
    for (size_t i=0;i<QA_Q3_ENTITIES;++i) messages[i]=-1;
    if (!receipts->snapshots) return;
    for (size_t i=0;i<QA_Q3_PACKET_BACKUP;++i) {
        const application_q3_snapshot_receipt *row=receipts->snapshots+i;
        if (!row->valid) continue;
        if (row->player.registry && row->player_number<QA_Q3_ENTITIES && messages[row->player_number]<row->message) {
            actors[row->player_number]=row->player; messages[row->player_number]=row->message;
        }
        for (size_t j=0;j<row->count;++j) {
            uint32_t number=row->entities[j].number;
            if (number<QA_Q3_ENTITIES && row->entities[j].actor.registry && messages[number]<row->message) {
                actors[number]=row->entities[j].actor; messages[number]=row->message;
            }
        }
    }
}
static bool legacy_actor(void *context,uint32_t number,qa_actor_id *out,qa_error *error)
{ (void)error; *out=number<QA_Q3_ENTITIES?((const qa_actor_id *)context)[number]:(qa_actor_id){0}; return true; }
bool application_q3_source_receipts_legacy(application_q3_source_receipts *receipts,
    const qa_q3_snapshot *snapshot,const qa_actor_id actors[QA_Q3_ENTITIES],qa_error *error)
{
    return application_q3_source_receipts_prepare(receipts,error) &&
        application_q3_source_receipts_capture(receipts,snapshot,legacy_actor,(void *)actors,error);
}
static bool receipt_fail(qa_source_save_io *io,const char *message)
{ io->failed=true; qa_error_set(io->error,QA_ERROR_FORMAT,io->offset,"%s",message); return false; }
bool application_q3_source_receipts_fields(qa_source_save_io *io,application_q3_source_receipts *receipts,
    bool (*actor_fields)(qa_source_save_io *,qa_actor_id *,bool),bool checkpoint)
{
    size_t count=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE && receipts->snapshots)
        for (size_t i=0;i<QA_Q3_PACKET_BACKUP;++i) count+=receipts->snapshots[i].valid;
    if (!qa_source_save_count(io,&count,QA_Q3_PACKET_BACKUP)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (count && !application_q3_source_receipts_prepare(receipts,io->error)) { io->failed=true; return false; }
        application_q3_source_receipts_clear(receipts);
    }
    for (size_t i=0,cursor=0;i<count;++i) {
        uint32_t index=0;
        if (io->direction==QA_SOURCE_SAVE_WRITE) {
            while (cursor<QA_Q3_PACKET_BACKUP && !receipts->snapshots[cursor].valid) ++cursor;
            index=(uint32_t)cursor++;
        }
        if (!qa_source_save_u32(io,&index) || index>=QA_Q3_PACKET_BACKUP) return receipt_fail(io,"Invalid Q3 snapshot receipt slot");
        application_q3_snapshot_receipt *row=receipts->snapshots+index;
        if (io->direction==QA_SOURCE_SAVE_READ && row->valid) return receipt_fail(io,"Repeated Q3 snapshot receipt slot");
        if (!qa_source_save_i32(io,&row->message) || row->message<0 ||
            ((uint32_t)row->message&(QA_Q3_PACKET_BACKUP-1))!=index ||
            !qa_source_save_i32(io,&row->time) || !qa_source_save_bool(io,&row->delivered) ||
            !qa_source_save_u32(io,&row->player_number) || row->player_number>=QA_Q3_ENTITIES ||
            !actor_fields(io,&row->player,checkpoint) ||
            !qa_source_save_count(io,&row->count,QA_Q3_SNAPSHOT_ENTITIES)) return receipt_fail(io,"Invalid Q3 snapshot actor receipt");
        for (size_t j=0;j<row->count;++j)
            if (!qa_source_save_u32(io,&row->entities[j].number) || row->entities[j].number>=QA_Q3_ENTITIES ||
                (j && row->entities[j].number<=row->entities[j-1].number) ||
                !actor_fields(io,&row->entities[j].actor,checkpoint)) return receipt_fail(io,"Invalid Q3 received actor generation");
        row->valid=true;
    }
    return true;
}
