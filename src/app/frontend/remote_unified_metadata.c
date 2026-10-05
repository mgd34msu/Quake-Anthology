#include "remote_unified_metadata.h"
#include "remote_unified_private.h"
#include "qa/unified_frame_metadata.h"

bool frontend_remote_unified_metadata_control(frontend_remote_unified *owner,
    const qa_unified_document *document,qa_error *error)
{
    const qa_unified_frame_metadata *update=qa_unified_document_metadata(document);
    if(!owner||!update||!owner->admitted||!frontend_remote_unified_current(owner,error))
        return frontend_unified_fail(error,QA_ERROR_ARGUMENT,"Unified metadata requires its actual admitted CLIENT");
    const qa_unified_frame_metadata *previous=qa_unified_document_metadata(owner->source_metadata);
    if(update->epoch!=owner->epoch||(previous&&update->frame<previous->frame))
        return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified metadata changed its actual Source epoch or frame order");
    for(size_t i=0;i<update->configuration_count;++i)
        if(update->configurations[i].actor.registry!=owner->wire_player.registry)
            return frontend_unified_fail(error,QA_ERROR_FORMAT,"Unified metadata belongs to another admitted Source registry");
    qa_unified_document *retained=NULL;
    if(!qa_unified_metadata_apply(owner->source_metadata,document,&retained,error))return false;
    qa_unified_document_destroy(owner->source_metadata);owner->source_metadata=retained;return true;
}
const qa_unified_frame_metadata *frontend_remote_unified_metadata(const frontend_remote_unified *owner)
{
    const qa_unified_frame_metadata *metadata=owner?qa_unified_document_metadata(owner->source_metadata):NULL;
    return metadata&&metadata->epoch==owner->epoch?metadata:NULL;
}
void frontend_remote_unified_metadata_clear(frontend_remote_unified *owner)
{
    if(!owner)return;
    qa_unified_document_destroy(owner->source_metadata);owner->source_metadata=NULL;
}
