#include "guest_q3_component_private.h"
#include "qa/cvars_save.h"
#include <limits.h>

static bool blob(qa_source_save_io *io,qa_buffer *buffer)
{
    size_t size=buffer->size;
    if(!qa_source_save_count(io,&size,UINT32_MAX)) return false;
    if(io->direction==QA_SOURCE_SAVE_READ) {
        buffer->data=size?malloc(size):NULL; buffer->size=size;
        if(size&&!buffer->data) return q3records_fail(io->error,QA_ERROR_MEMORY,"Retaining component child continuation");
    }
    return qa_source_save_bytes(io,buffer->data,size);
}
static bool fields(application_q3_component *c,qa_source_save_io *io,qa_buffer children[7],qa_qvm_binding *ids,qa_qvm_binding *resolver)
{
    uint8_t magic[4]={'Q','G','C','M'},expected[4]={'Q','G','C','M'}; uint32_t version=2,abi=(uint32_t)c->options.abi;
    uint8_t program[32],declaration[32]; memcpy(program,&c->options.program_digest,32); memcpy(declaration,&c->options.declaration_digest,32);
    uint64_t owner=c->options.host.owner,services=c->options.host.service_owner,generation=c->options.generation;
    const char *path=c->options.program_path; size_t count=c->hook_count;
    if(!qa_source_save_bytes(io,magic,4)||memcmp(magic,expected,4)||!qa_source_save_u32(io,&version)||version!=2||
        !qa_source_save_u32(io,&abi)||abi!=(uint32_t)c->options.abi||!qa_source_save_bytes(io,program,32)||memcmp(program,&c->options.program_digest,32)||
        !qa_source_save_bytes(io,declaration,32)||memcmp(declaration,&c->options.declaration_digest,32)||!qa_source_save_text(io,&path)||!path||strcmp(path,c->options.program_path)||
        !qa_source_save_u64(io,&owner)||owner!=c->options.host.owner||!qa_source_save_u64(io,&services)||services!=c->options.host.service_owner||
        !qa_source_save_u64(io,&generation)||generation!=c->options.generation||!qa_source_save_i32(io,&c->milliseconds)||c->milliseconds<0||
        !qa_source_save_count(io,&count,c->hook_count)||count!=c->hook_count) return q3records_fail(io->error,QA_ERROR_FORMAT,"Component continuation differs from its actual source artifacts or namespaces");
    for(size_t i=0;i<count;++i) {
        uint32_t entry=c->hooks[i].entry;
        if(!qa_source_save_u32(io,&entry)||entry!=c->hooks[i].entry||!qa_source_save_u64(io,ids+i)||!ids[i])
            return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved function union differs from its actual declaration");
        for(size_t j=0;j<i;++j) if(ids[j]==ids[i]) return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved functions duplicate a binding");
    }
    if(!qa_source_save_u64(io,resolver)||!*resolver) return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved resolver is absent");
    for(size_t i=0;i<count;++i) if(ids[i]==*resolver) return q3records_fail(io->error,QA_ERROR_FORMAT,"Component saved resolver aliases a physical function");
    for(size_t i=0;i<7;++i) if(!blob(io,children+i)) return false;
    return true;
}
bool application_q3_component_checkpoint(application_q3_component *c,qa_buffer *out,qa_error *e)
{
    if(!c||!out||out->data||out->size||!c->initialized||c->restoring||c->closing||!application_q3_component_idle(c)||!q3component_current(c,e))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component capture requires its real completed idle executor");
    qa_qvm_saved_function *descriptors=c->hook_count?calloc(c->hook_count,sizeof(*descriptors)):NULL;
    qa_qvm_binding *ids=c->hook_count?calloc(c->hook_count,sizeof(*ids)):NULL;
    if(c->hook_count&&(!descriptors||!ids)) { free(descriptors); free(ids); return q3records_fail(e,QA_ERROR_MEMORY,"Capturing complete component function inventory"); }
    qa_buffer children[7]={{0}}; qa_source_save_io io={0};
    qa_qvm_saved_resolver resolver={c->actor_resolver,q3component_actor_resolve,c};
    bool ok=q3component_descriptors(c,descriptors,e)&&qa_qvm_checkpoint_callbacks(c->vm,descriptors,c->hook_count,&resolver,e)&&
        application_q3_component_records_checkpoint(c->records,children+0,e)&&application_q3_mod_checkpoint(c->mod,children+1,e)&&
        application_q3_component_source_checkpoint(c->source,children+2,e)&&qa_cvars_save_capture(c->cvars,children+3,e)&&
        qa_console_save_capture(c->console,c->options.host.session,children+4,e)&&qa_qvm_checkpoint(c->vm,children+5,e)&&
        application_q3_mod_actors_checkpoint(c->actor_semantics,children+6,e);
    for(size_t i=0;i<c->hook_count;++i) ids[i]=c->hooks[i].id;
    if(ok) ok=qa_source_save_writer(&io,c->options.host.session,e)&&fields(c,&io,children,ids,&resolver.binding)&&qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); for(size_t i=0;i<7;++i) qa_buffer_free(children+i); free(ids); free(descriptors); return ok;
}
bool application_q3_component_restore(application_q3_component *c,qa_bytes bytes,const qa_console_save_resolvers *console_resolvers,qa_error *e)
{
    if(!c||!c->restoring||c->initialized||!application_q3_component_idle(c)) return q3records_fail(e,QA_ERROR_ARGUMENT,"Component restore requires its unentered real candidate");
    qa_qvm_saved_function *descriptors=c->hook_count?calloc(c->hook_count,sizeof(*descriptors)):NULL;
    qa_qvm_binding *ids=c->hook_count?calloc(c->hook_count,sizeof(*ids)):NULL;
    if(c->hook_count&&(!descriptors||!ids)) { free(descriptors); free(ids); return q3records_fail(e,QA_ERROR_MEMORY,"Retaining restored physical function union"); }
    qa_buffer children[7]={{0}}; qa_source_save_io io={0}; qa_cvars_restore *cvars=NULL;
    qa_qvm_binding saved_resolver=0;
    qa_qvm_saved_resolver resolver={c->actor_resolver,q3component_actor_resolve,c};
    bool ok=qa_source_save_reader(&io,c->options.host.session,bytes,e)&&fields(c,&io,children,ids,&saved_resolver)&&qa_source_save_finish(&io,NULL)&&
        application_q3_component_records_restore(c->records,(qa_bytes){children[0].data,children[0].size},e)&&
        application_q3_mod_restore(c->mod,(qa_bytes){children[1].data,children[1].size},e)&&
        application_q3_component_source_restore(c->source,(qa_bytes){children[2].data,children[2].size},e)&&
        application_q3_mod_actors_restore(c->actor_semantics,(qa_bytes){children[6].data,children[6].size},e)&&
        qa_cvars_save_prepare(c->cvars,(qa_bytes){children[3].data,children[3].size},&cvars,e)&&qa_cvars_save_validate(cvars,e);
    if(ok) { ok=qa_cvars_save_commit(cvars,e); if(ok) cvars=NULL; }
    if(ok) ok=qa_console_save_restore(c->console,c->options.host.session,console_resolvers,(qa_bytes){children[4].data,children[4].size},e)&&
        q3component_descriptors(c,descriptors,e)&&qa_qvm_restore_candidate_callbacks(c->vm,(qa_bytes){children[5].data,children[5].size},descriptors,ids,c->hook_count,&resolver,saved_resolver,e);
    if(ok) {
        for(size_t i=0;i<c->hook_count;++i) c->hooks[i].id=ids[i];
        c->actor_resolver=saved_resolver;
        ok=qa_qvm_restore_candidate(c->vm,(qa_bytes){children[5].data,children[5].size},e);
    }
    if(ok) c->initialized=true;
    qa_cvars_save_abort(cvars); qa_source_save_dispose(&io); for(size_t i=0;i<7;++i) qa_buffer_free(children+i); free(ids); free(descriptors); return ok;
}
bool application_q3_component_finish_restore(application_q3_component *c,qa_error *e)
{
    if(!c||!c->initialized||!application_q3_component_idle(c)||!q3component_storage(c,e)||!c->options.current(c->options.context))
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component activation requires its installed restored source graph");
    if(!c->restoring&&!c->restored_storage)
        return q3records_fail(e,QA_ERROR_ARGUMENT,"Component activation has no imported continuation");
    if(!c->restored_storage) {
        if(!qa_q3_host_finish_restore(c->host,e)||!application_q3_component_records_validate(c->records,e)) return false;
        c->restoring=false;
        if(!application_q3_component_source_attach(c->source,c->vm,c->host,e)||!application_q3_component_source_validate(c->source,e)) { c->restoring=true; return false; }
        c->restored_storage=true;
    }
    if(!c->restored_actors) {
        if(!application_q3_mod_actors_finish_restore(c->actor_semantics,e)) return false;
        c->restored_actors=true;
    }
    if(!c->restored_mod) {
        if(!application_q3_mod_activate(c->mod,e)) return false;
        c->restored_mod=true;
    }
    if(!c->restored_callbacks) {
        if(!application_q3_mod_callbacks_register(c->mod,e)) return false;
        c->restored_callbacks=true;
    }
    return true;
}
