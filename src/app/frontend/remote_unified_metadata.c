#include "remote_unified_metadata.h"
#include "remote_unified_private.h"
#include "qa/unified_frame_metadata.h"
#include <stdlib.h>

bool frontend_remote_unified_metadata_control(frontend_remote_unified *owner,
    const qa_unified_document *document,qa_error *error)
{
    const qa_unified_frame_metadata *update=qa_unified_document_metadata(document);
    if(!owner||!update||!owner->admitted||!frontend_remote_unified_current(owner,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified metadata requires its actual admitted CLIENT");
    const qa_unified_document *prior=owner->metadata_tail?owner->metadata_tail->document:owner->source_metadata;
    const qa_unified_frame_metadata *previous=qa_unified_document_metadata(prior);
    if(update->epoch!=owner->epoch||(previous&&update->frame<previous->frame))
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified metadata changed its actual Source epoch or frame order");
    for(size_t i=0;i<update->configuration_count;++i)
        if(update->configurations[i].actor.registry!=owner->wire_player.registry)
            return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified metadata belongs to another admitted Source registry");
    qa_unified_document *retained=NULL;
    if(!qa_unified_metadata_apply(prior,document,&retained,error))return false;
    size_t bytes=qa_unified_document_memory(retained);
    const qa_unified_limits *limits=qa_unified_session_limits(owner->session);
    if(!limits||limits->queued_reliable_bytes>SIZE_MAX-limits->message_bytes) {
        qa_unified_document_destroy(retained);
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified metadata lost its actual session custody budget");
    }
    size_t budget=limits->queued_reliable_bytes+limits->message_bytes;
    if(bytes>SIZE_MAX-sizeof(frontend_unified_metadata_cut)||owner->pending_metadata_bytes>budget||
        bytes+sizeof(frontend_unified_metadata_cut)>budget-owner->pending_metadata_bytes) {
        qa_unified_document_destroy(retained);
        return frontend_unified_fail(error,QA_ERROR_MEMORY,"Unified pending metadata exceeds its actual session custody budget");
    }
    frontend_unified_metadata_cut *cut=malloc(sizeof(*cut));
    if(!cut) {
        qa_unified_document_destroy(retained);
        return frontend_unified_fail(error,QA_ERROR_MEMORY,"Retaining ordered Unified metadata publication");
    }
    *cut=(frontend_unified_metadata_cut){.document=retained};
    if(owner->metadata_tail)owner->metadata_tail->next=cut;
    else owner->metadata_head=cut;
    owner->metadata_tail=cut;owner->pending_metadata_bytes+=bytes+sizeof(*cut);return true;
}
const qa_unified_frame_metadata *frontend_remote_unified_metadata(const frontend_remote_unified *owner)
{
    const qa_unified_frame_metadata *metadata=owner?qa_unified_document_metadata(owner->source_metadata):NULL;
    return metadata&&metadata->epoch==owner->epoch?metadata:NULL;
}
const qa_unified_document *frontend_remote_unified_metadata_document(const frontend_remote_unified *owner,
    const qa_unified_frame *frame)
{
    if(!owner)return NULL;
    const qa_unified_document *document=owner->prepared_source_metadata&&frame&&
        frame==qa_unified_document_frame(owner->prepared_frame)?
        owner->prepared_source_metadata:owner->source_metadata;
    const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(document);
    return metadata&&metadata->epoch==owner->epoch&&(!frame||(metadata->epoch==frame->epoch&&frame->world&&
        metadata->frame<=frame->world->source.number))?document:NULL;
}
bool frontend_remote_unified_metadata_prepare(frontend_remote_unified *owner,const qa_unified_frame *frame,
    qa_error *error)
{
    if(!owner||!frame||!frame->world||frame->epoch!=owner->epoch)
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified metadata selection lost its actual Source frame");
    if(owner->prepared_source_metadata)
        return frontend_remote_unified_metadata_document(owner,frame)!=NULL||
            frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified prepared metadata changed its Source frame fence");
    const qa_unified_document *selected=owner->source_metadata;
    for(const frontend_unified_metadata_cut *cut=owner->metadata_head;cut;cut=cut->next) {
        const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(cut->document);
        if(metadata->frame>frame->world->source.number)break;
        selected=cut->document;
    }
    const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(selected);
    if(!metadata||metadata->epoch!=frame->epoch||metadata->frame>frame->world->source.number)
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified frame precedes its eligible reliable metadata");
    return qa_unified_document_retain(selected,&owner->prepared_source_metadata,error);
}
void frontend_remote_unified_metadata_abort(frontend_remote_unified *owner)
{
    if(!owner)return;
    qa_unified_document_destroy(owner->prepared_source_metadata);owner->prepared_source_metadata=NULL;
}
void frontend_remote_unified_metadata_commit(frontend_remote_unified *owner,const qa_unified_frame *frame)
{
    qa_unified_document_destroy(owner->source_metadata);
    owner->source_metadata=owner->prepared_source_metadata;owner->prepared_source_metadata=NULL;
    while(owner->metadata_head) {
        frontend_unified_metadata_cut *cut=owner->metadata_head;
        const qa_unified_frame_metadata *metadata=qa_unified_document_metadata(cut->document);
        if(metadata->frame>frame->world->source.number)break;
        owner->metadata_head=cut->next;
        owner->pending_metadata_bytes-=qa_unified_document_memory(cut->document)+sizeof(*cut);
        qa_unified_document_destroy(cut->document);free(cut);
    }
    if(!owner->metadata_head)owner->metadata_tail=NULL;
}
bool frontend_remote_unified_metadata_returned(const frontend_remote_unified *owner,qa_error *error)
{
    return (owner&&!owner->metadata_head&&(!owner->prepared_source_metadata||
        owner->prepared_source_metadata==owner->source_metadata))||
        frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified checkpoint retains unpublished reliable metadata");
}
void frontend_remote_unified_metadata_clear(frontend_remote_unified *owner)
{
    if(!owner)return;
    frontend_remote_unified_metadata_abort(owner);
    while(owner->metadata_head) {
        frontend_unified_metadata_cut *cut=owner->metadata_head;owner->metadata_head=cut->next;
        qa_unified_document_destroy(cut->document);free(cut);
    }
    owner->metadata_tail=NULL;owner->pending_metadata_bytes=0;
    qa_unified_document_destroy(owner->source_metadata);owner->source_metadata=NULL;
}
