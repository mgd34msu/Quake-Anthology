#include "unified_media_inventory.h"
#include "save_private.h"

bool frontend_unified_media_inventory_count(const qa_frontend *f,size_t *out,qa_error *error)
{
    size_t replicas=frontend_remote_unified_count(f);
    if(!f || !out || replicas>SIZE_MAX/2)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Unified media inventory requires its actual bounded replica roster");
    *out=replicas*2; return true;
}
bool frontend_unified_media_inventory_at(const qa_frontend *f,size_t ordinal,
    frontend_unified_media **out,qa_error *error)
{
    size_t count;
    if(!out || !frontend_unified_media_inventory_count(f,&count,error) || ordinal>=count)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Unified media ordinal leaves its actual replica roster");
    frontend_unified_media *installed=NULL,*pending=NULL;
    if(!frontend_remote_unified_presentation_media(frontend_remote_unified_at(f,ordinal/2),&installed,&pending,error))
        return false;
    if(installed && installed==pending)
        return frontend_fail(error,QA_ERROR_FORMAT,"Unified installed and pending media repeat destructor authority");
    *out=ordinal%2?pending:installed; return true;
}
bool frontend_unified_media_bank_key(size_t media,size_t bank,uint64_t *out)
{
    if(!out || media>=UINT32_MAX || bank>=UINT32_MAX) return false;
    *out=((uint64_t)(media+1)<<32)|(uint64_t)(bank+1); return true;
}
bool frontend_unified_media_bank_key_read(uint64_t key,size_t *media,size_t *bank)
{
    uint32_t upper=(uint32_t)(key>>32),lower=(uint32_t)key;
    if(!upper || !lower || !media || !bank) return false;
    *media=(size_t)(upper-1); *bank=(size_t)(lower-1); return true;
}
