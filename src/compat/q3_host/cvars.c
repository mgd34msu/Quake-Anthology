#include "internal.h"
#include "qa/console_cvars_prepare.h"
#include "qa/cvars_alias.h"
#include "qa/source_save.h"

/* Registry handles are nonnegative size_t values admitted through INT32_MAX.
 * This opaque signed source token cannot alias any real private handle. */
enum { ENGINE_CHEATS_HANDLE = -2 };
static bool status_name(const char *);

void q3_cvars_bindings_free(q3_cvar_binding *bindings,size_t count)
{
    for (size_t i=0;i<count;++i) { free(bindings[i].name); free(bindings[i].previous_value); }
    free(bindings);
}
static char *copy_text(const char *text,qa_error *error)
{
    size_t length=strlen(text)+1;
    char *copy=malloc(length);
    if (!copy) { q3_fail(error,QA_ERROR_MEMORY,0,"Retaining Q3 cvar mirror text"); return NULL; }
    memcpy(copy,text,length); return copy;
}
static bool binding_text(qa_source_save_io *io,char **out)
{
    if (io->direction==QA_SOURCE_SAVE_WRITE && !*out)
        return q3_fail(io->error,QA_ERROR_ARGUMENT,0,"Q3 cvar mirror text is absent");
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(*out):0;
    if (!qa_source_save_count(io,&length,SIZE_MAX-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (io->offset>io->input.size || length>io->input.size-io->offset ||
            memchr(io->input.data+io->offset,0,length))
            return q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid Q3 cvar mirror text");
        *out=malloc(length+1);
        if (!*out) return q3_fail(io->error,QA_ERROR_MEMORY,0,"Restoring Q3 cvar mirror text");
        (*out)[length]=0;
    }
    return qa_source_save_bytes(io,*out,length);
}
static bool binding_fields(qa_source_save_io *io,q3_cvar_binding *binding)
{
    uint32_t reference=binding->reference,dialect=binding->dialect;
    bool ok=qa_source_save_u32(io,&reference) && qa_source_save_u32(io,&dialect) &&
        qa_source_save_count(io,&binding->handle,SIZE_MAX-1) && binding_text(io,&binding->name) &&
        qa_source_save_bool(io,&binding->read) && qa_source_save_u64(io,&binding->revision);
    if (io->direction==QA_SOURCE_SAVE_READ) {
        binding->reference=(qa_q3_host_cvar_namespace)reference;
        binding->dialect=(qa_console_dialect)dialect;
    }
    if (!ok) return false;
    if (reference<QA_Q3_HOST_CVAR_ENGINE || reference>QA_Q3_HOST_CVAR_SELECTED_VIEW ||
        dialect>QA_CONSOLE_Q3 || !binding->name || !*binding->name ||
        (binding->read?!binding->revision:binding->revision!=0))
        return q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid Q3 cvar namespace or mirror identity");
    for (const unsigned char *p=(const unsigned char *)binding->name;*p;++p)
        if (*p<=32 || *p=='"' || *p==';')
            return q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid authored Q3 cvar binding name");
    return !binding->read || (qa_source_save_bool(io,&binding->previous_current) &&
        binding_text(io,&binding->previous_value) &&
        qa_source_save_u64(io,&binding->previous_modification) &&
        qa_source_save_f32(io,&binding->previous_number) &&
        qa_source_save_i32(io,&binding->previous_integer));
}
static bool status_fields(qa_source_save_io *io,q3_cvar_status *status)
{
    if (!qa_source_save_bool(io,&status->read)) return false;
    if (!status->read) return (!status->revision && !status->previous_value) ||
        q3_fail(io->error,QA_ERROR_FORMAT,0,"Unread status mirror retains an invalid continuation");
    uint32_t bits; memcpy(&bits,&status->previous_number,sizeof(bits));
    bool ok=qa_source_save_u32(io,&status->revision) &&
        binding_text(io,&status->previous_value) &&
        qa_source_save_u64(io,&status->previous_modification) &&
        qa_source_save_u32(io,&bits) && qa_source_save_i32(io,&status->previous_integer) &&
        qa_source_save_bool(io,&status->previous_visible);
    if (io->direction==QA_SOURCE_SAVE_READ) memcpy(&status->previous_number,&bits,sizeof(bits));
    return ok && ((status->revision && status->revision<=INT32_MAX) ||
        q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid CGAME status mirror revision"));
}
static bool binding_namespace(const qa_q3_host *host,const q3_cvar_binding *binding,
    qa_cvars **out,qa_error *error)
{
    const qa_q3_host_cvar_services *services=&host->options.cvar_namespaces;
    qa_cvars *registry=NULL; qa_q3_host_cvar_namespace reference=0;
    if (!services->resolve || !services->reference)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar binding lacks its actual namespace services");
    if (!services->resolve(services->context,binding->reference,&registry,error)) return false;
    if (!registry || qa_cvars_dialect(registry)!=binding->dialect ||
        !qa_cvars_observer_idle(registry))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar binding namespace is absent or active");
    if (!services->reference(services->context,registry,&reference,error)) return false;
    if (reference!=binding->reference)
        return q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cvar binding differs from its canonical namespace reference");
    *out=registry; return true;
}
static bool cache_fields(qa_source_save_io *io,q3_cvar_cache *cache)
{
    return qa_source_save_i32(io,&cache->pointer) && qa_source_save_i32(io,&cache->handle) &&
        ((cache->pointer!=0 && cache->handle!=-1 && cache->handle!=ENGINE_CHEATS_HANDLE) ||
            q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid original CGAME cvar cache identity"));
}
bool q3_cvars_bindings_capture(const qa_q3_host *host,qa_buffer *out,qa_error *error)
{
    qa_source_save_io io={0};
    uint8_t magic[4]={'Q','3','C','B'}; uint32_t version=3;
    size_t count=host->cvar_binding_count;
    bool ok=qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,sizeof(magic)) &&
        qa_source_save_u32(&io,&version) && qa_source_save_count(&io,&count,1024);
    for (size_t i=0;ok && i<count;++i) {
        q3_cvar_binding copy=host->cvar_bindings[i]; qa_cvars *current=NULL;
        ok=binding_namespace(host,&copy,&current,error) && copy.handle<qa_cvars_handle_count(current);
        if (!ok && (!error || error->code==QA_OK))
            q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar checkpoint lost its actual namespace");
        copy.previous_current=copy.registry==current;
        if (ok) ok=binding_fields(&io,&copy);
    }
    size_t caches=host->cvar_cache_count;
    if (ok) ok=qa_source_save_count(&io,&caches,SIZE_MAX/sizeof(q3_cvar_cache));
    for (size_t i=0;ok && i<caches;++i) {
        q3_cvar_cache copy=host->cvar_caches[i];
        if (!host->vm || copy.vm!=host->vm || copy.native || host->options.role!=QA_QVM_CGAME)
            ok=q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cache has no original executor owner");
        if (ok) ok=cache_fields(&io,&copy);
    }
    q3_cvar_status status=host->cvar_status;
    if (ok && status.read && host->options.role!=QA_QVM_CGAME)
        ok=q3_fail(error,QA_ERROR_ARGUMENT,0,"Status mirror has no actual CGAME owner");
    if (ok) ok=status_fields(&io,&status) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool q3_cvars_bindings_decode(qa_bytes bytes,q3_cvar_binding **out,size_t *out_count,
    q3_cvar_cache **out_caches,size_t *out_cache_count,q3_cvar_status *out_status,qa_error *error)
{
    qa_source_save_io io={0}; uint8_t magic[4]; uint32_t version=0; size_t count=0;
    if (!out || *out || !out_count || !out_caches || *out_caches || !out_cache_count ||
        !out_status || out_status->read || out_status->revision || out_status->previous_value ||
        !qa_source_save_reader(&io,NULL,bytes,error))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar decode requires empty actual candidate output");
    bool ok=qa_source_save_bytes(&io,magic,sizeof(magic)) && qa_source_save_u32(&io,&version) &&
        qa_source_save_count(&io,&count,1024);
    if (ok && (memcmp(magic,"Q3CB",4) || version!=3 || count>bytes.size-io.offset))
        ok=q3_fail(error,QA_ERROR_FORMAT,0,"Unsupported Q3 cvar binding continuation");
    q3_cvar_binding *bindings=ok && count?calloc(count,sizeof(*bindings)):NULL;
    if (ok && count && !bindings) ok=q3_fail(error,QA_ERROR_MEMORY,0,"Restoring routed Q3 cvar bindings");
    for (size_t i=0;ok && i<count;++i) {
        ok=binding_fields(&io,bindings+i);
        for (size_t j=0;ok && j<i;++j)
            if (bindings[j].reference==bindings[i].reference && bindings[j].handle==bindings[i].handle)
                ok=q3_fail(error,QA_ERROR_FORMAT,0,"Duplicate Q3 physical cvar binding");
    }
    size_t cache_count=0;
    if (ok) ok=qa_source_save_count(&io,&cache_count,SIZE_MAX/sizeof(q3_cvar_cache));
    if (ok && cache_count>(bytes.size-io.offset)/8)
        ok=q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cache inventory exceeds retained bytes");
    q3_cvar_cache *caches=ok && cache_count?calloc(cache_count,sizeof(*caches)):NULL;
    if (ok && cache_count && !caches) ok=q3_fail(error,QA_ERROR_MEMORY,0,"Restoring original Q3 cvar caches");
    for (size_t i=0;ok && i<cache_count;++i) {
        ok=cache_fields(&io,caches+i);
        if (ok && caches[i].handle<=-3 && (uint64_t)(-(int64_t)caches[i].handle-3)>=count)
            ok=q3_fail(error,QA_ERROR_FORMAT,0,"Original Q3 cache has no routed handle owner");
        for (size_t j=0;ok && j<i;++j)
            if (caches[j].pointer==caches[i].pointer)
                ok=q3_fail(error,QA_ERROR_FORMAT,0,"Duplicate original Q3 cvar cache pointer");
    }
    q3_cvar_status status={0};
    if (ok) ok=status_fields(&io,&status) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok) { q3_cvars_bindings_free(bindings,bindings?count:0); free(caches); free(status.previous_value); return false; }
    *out=bindings; *out_count=count; *out_caches=caches; *out_cache_count=cache_count;
    *out_status=status; return true;
}
bool q3_cvars_bindings_restore_ready(qa_q3_host *host,qa_error *error)
{
    if (host->cvar_status.read && host->options.role!=QA_QVM_CGAME)
        return q3_fail(error,QA_ERROR_FORMAT,0,"Restored status mirror has no actual CGAME owner");
    for (size_t i=0;i<host->cvar_binding_count;++i) {
        q3_cvar_binding *binding=host->cvar_bindings+i; qa_cvars *registry=NULL;
        if (!binding_namespace(host,binding,&registry,error)) return false;
        const qa_cvar_view *view=qa_cvars_handle(registry,binding->handle);
        if (binding->handle>=qa_cvars_handle_count(registry) || (view && strcmp(view->name,binding->name)))
            return q3_fail(error,QA_ERROR_FORMAT,0,"Restored Q3 binding differs from its actual canonical handle");
        binding->registry=binding->previous_current?registry:NULL;
    }
    if (host->cvar_cache_count && (!host->vm || host->options.role!=QA_QVM_CGAME))
        return q3_fail(error,QA_ERROR_FORMAT,0,"Restored cvar cache lacks its original CGAME executor");
    for (size_t i=0;i<host->cvar_cache_count;++i) {
        q3_cvar_cache *cache=host->cvar_caches+i; qa_bytes span;
        if (cache->handle>=0) {
            const qa_cvar_view *view=qa_cvars_handle(host->options.cvars,(size_t)cache->handle);
            if ((size_t)cache->handle>=qa_cvars_handle_count(host->options.cvars) ||
                (view && !status_name(view->name)))
                return q3_fail(error,QA_ERROR_FORMAT,0,"Restored Q3 cache differs from its physical status declaration");
        } else {
            size_t index=(size_t)(-(int64_t)cache->handle-3);
            if (index>=host->cvar_binding_count || !status_name(host->cvar_bindings[index].name))
                return q3_fail(error,QA_ERROR_FORMAT,0,"Restored Q3 cache differs from its routed status declaration");
        }
        if (!qa_qvm_span(host->vm,cache->pointer,0,272,&span,error)) return false;
        cache->vm=host->vm; cache->memory=host->memory;
        cache->address=(uint64_t)qa_qvm_memory_size(host->vm)+qa_qvm_mask_address(host->vm,cache->pointer);
    }
    return true;
}
size_t qa_q3_host_cvar_cache_count(const qa_q3_host *host)
{ return host && !host->retired && !host->restore_pending?host->cvar_cache_count:0; }
bool qa_q3_host_cvar_cache_read(const qa_q3_host *host,size_t ordinal,
    qa_q3_host_cvar_cache *out,bool *found,qa_error *error)
{
    if (!host || !out || !found || host->retired || host->restore_pending)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar cache read requires its actual live host");
    *found=false;
    if (ordinal>=host->cvar_cache_count) return true;
    const q3_cvar_cache *cache=host->cvar_caches+ordinal;
    if ((!cache->vm && !cache->native) || cache->vm!=host->vm || cache->native!=host->native ||
        cache->memory.context!=host->memory.context || cache->memory.read!=host->memory.read ||
        cache->memory.write!=host->memory.write || cache->memory.read_string!=host->memory.read_string ||
        cache->memory.pointer_bytes!=host->memory.pointer_bytes)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar cache lost its actual executor memory owner");
    uint8_t bytes[272];
    if (!cache->memory.read(cache->memory.context,cache->address,bytes,sizeof(bytes),error)) return false;
    if (qa_load_i32le(bytes)!=cache->handle) return true;
    if (!memchr(bytes+16,0,256))
        return q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cvar cache string is unterminated");
    qa_q3_host_cvar_cache value={.vm=cache->vm,.native=cache->native,
        .source_pointer=cache->pointer,.address=cache->address,.handle=cache->handle,
        .modification=qa_load_i32le(bytes+4),.number=qa_load_f32le(bytes+8),.integer=qa_load_i32le(bytes+12)};
    memcpy(value.value,bytes+16,sizeof(value.value));
    *out=value; *found=true; return true;
}

static bool engine_name(q3_call *call, const char *name)
{
    qa_q3_host_options *options = &call->host->options;
    const char *engine = "sv_cheats";
    const unsigned char *key = (const unsigned char *)name;
    while (*key && *engine) {
        unsigned char character = *key++;
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
        if (character != (unsigned char)*engine++) return false;
    }
    return !*key && !*engine && options->role == QA_QVM_GAME && options->engine_cvars;
}

static qa_cvars *named_owner(q3_call *call, const char *name)
{
    return engine_name(call, name) ? call->host->options.engine_cvars : call->host->options.cvars;
}
typedef struct q3_cvar_access {
    qa_cvars *registry;
    qa_cvars_edit *edit;
    qa_command_context command;
} q3_cvar_access;
static bool access_name(q3_call *call,const char *name,q3_cvar_access *out,qa_error *error)
{
    qa_q3_host_options *options=&call->host->options;
    *out=(q3_cvar_access){.registry=named_owner(call,name),.command=options->command_context};
    if (!options->console) return true;
    return qa_console_cvar_context(options->console,&options->command_context,&out->command,error) &&
        qa_console_cvar_access(options->console,&out->command,name,&out->registry,&out->edit,error);
}
static const qa_cvar_view *find(q3_cvar_access access,const char *name)
{ return access.edit?qa_cvars_edit_find(access.edit,name):qa_cvars_find(access.registry,name); }
static bool apply(q3_call *call,q3_cvar_access access,const qa_cvars_edit_command *command,qa_error *error)
{
    if (call->host->options.console)
        return qa_console_cvar_apply(call->host->options.console,&access.command,command,error);
    switch (command->kind) {
    case QA_CVARS_EDIT_SET: return qa_cvars_set(access.registry,command->name,command->value,command->force,error);
    case QA_CVARS_EDIT_RESET: return qa_cvars_reset(access.registry,command->name,command->force,error);
    case QA_CVARS_EDIT_SET_NUMBER: return qa_cvars_set_number(access.registry,command->name,command->number,error);
    case QA_CVARS_EDIT_REGISTER: return qa_cvars_register(access.registry,command->name,command->value,
        command->flags,command->owner,command->description,error);
    case QA_CVARS_EDIT_RETAIN_SHARED: return qa_cvars_retain_shared(access.registry,command->name,error);
    default: return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 named cvar operation is unbound");
    }
}
static bool declare(q3_call *call,q3_cvar_access access,const char *name,const char *value,
    uint32_t flags,qa_error *error)
{
    if (qa_cvars_dialect(access.registry)!=QA_CONSOLE_Q3) {
        const char *canonical=qa_cvars_canonical_name(access.registry,name);
        if (strcmp(canonical,name) && (flags&(QA_CVAR_USERINFO|QA_CVAR_SERVERINFO|QA_CVAR_SYSTEMINFO)))
            return q3_fail(error,QA_ERROR_ARGUMENT,0,"Guest alias requires its canonical protocol info-key mapping");
        return find(access,name)!=NULL ||
            q3_fail(error,QA_ERROR_NOT_FOUND,0,"Unknown guest cvar requires its actual Q3 seat owner");
    }
    return apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_REGISTER,.name=name,
        .value=value,.flags=flags,.owner=access.registry==call->host->options.cvars?
            call->host->options.service_owner:0},error);
}

static bool bind_routed(q3_call *call,q3_cvar_access access,size_t handle,
    const qa_cvar_view *view,int32_t *token,qa_error *error)
{
    qa_q3_host *host=call->host;
    qa_q3_host_cvar_services *services=&host->options.cvar_namespaces;
    qa_q3_host_cvar_namespace reference=0;
    qa_cvars *resolved=NULL;
    if (!services->reference || !services->resolve ||
        !services->reference(services->context,access.registry,&reference,error) ||
        reference<QA_Q3_HOST_CVAR_ENGINE || reference>QA_Q3_HOST_CVAR_SELECTED_VIEW ||
        !services->resolve(services->context,reference,&resolved,error) || resolved!=access.registry)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Routed Q3 cvar lacks its actual factory namespace reference");
    for (size_t i=0;i<host->cvar_binding_count;++i) {
        const q3_cvar_binding *binding=host->cvar_bindings+i;
        if (binding->reference==reference && binding->handle==handle && !strcmp(binding->name,view->name)) {
            *token=-3-(int32_t)i; return true;
        }
    }
    if (host->cvar_binding_count>=1024)
        return q3_fail(error,QA_ERROR_MEMORY,0,"MAX_CVARS");
    char *name=copy_text(view->name,error);
    if (!name) return false;
    q3_cvar_binding *grown=realloc(host->cvar_bindings,(host->cvar_binding_count+1)*sizeof(*grown));
    if (!grown) { free(name); return q3_fail(error,QA_ERROR_MEMORY,0,"Retaining Q3 routed cvar bindings"); }
    host->cvar_bindings=grown;
    grown[host->cvar_binding_count]=(q3_cvar_binding){.reference=reference,.handle=handle,
        .registry=access.registry,.name=name,.dialect=qa_cvars_dialect(access.registry)};
    *token=-3-(int32_t)host->cvar_binding_count++; return true;
}
static bool routed_view(q3_call *call,q3_cvar_binding *binding,const qa_cvar_view **out,
    uint64_t *modification,qa_error *error)
{
    qa_q3_host_cvar_services *services=&call->host->options.cvar_namespaces;
    qa_cvars *registry=NULL;
    if (!services->resolve || !services->resolve(services->context,binding->reference,&registry,error) ||
        !registry || qa_cvars_dialect(registry)!=binding->dialect)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar binding lost its actual physical namespace");
    q3_cvar_access access;
    if (!access_name(call,binding->name,&access,error)) return false;
    if (access.registry!=registry)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar binding changed its admitted namespace");
    const qa_cvar_view *view=access.edit?qa_cvars_edit_handle(access.edit,binding->handle):
        qa_cvars_handle(registry,binding->handle);
    size_t count=access.edit?qa_cvars_edit_handle_count(access.edit):qa_cvars_handle_count(registry);
    if (binding->handle>=count)
        return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
    *out=view;
    if (!view) return true;
    if (strcmp(view->name,binding->name))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar handle belongs to another authored declaration");
    if (!binding->read || binding->registry!=registry || strcmp(binding->previous_value,view->value) ||
        binding->previous_number!=view->number || binding->previous_integer!=view->integer ||
        binding->previous_modification!=view->modification_count) {
        if (binding->revision==UINT64_MAX)
            return q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cvar mirror revision is exhausted");
        char *value=copy_text(view->value,error);
        if (!value) return false;
        free(binding->previous_value); binding->previous_value=value;
        binding->registry=registry; binding->previous_number=view->number;
        binding->previous_integer=view->integer; binding->previous_modification=view->modification_count;
        binding->read=true; ++binding->revision;
    }
    *modification=binding->revision; return true;
}

static bool effective_status(q3_call *call,const qa_cvar_view *source,
    qa_cvar_view *out,qa_error *error)
{
    *out=*source;
    qa_q3_host *host=call->host;
    if (host->options.role!=QA_QVM_CGAME || !status_name(source->name)) return true;
    bool visible=true;
    const qa_q3_host_cvar_status_services *services=&host->options.cvar_status;
    if (services->visible && !services->visible(services->context,host,&visible,error)) return false;
    q3_cvar_status *status=&host->cvar_status;
    if (!status->read || status->previous_modification!=source->modification_count ||
        status->previous_visible!=visible || strcmp(status->previous_value,source->value) ||
        status->previous_number!=source->number || status->previous_integer!=source->integer) {
        if (status->revision==INT32_MAX)
            return q3_fail(error,QA_ERROR_FORMAT,0,"CGAME status cvar revision is exhausted");
        char *value=copy_text(source->value,error);
        if (!value) return false;
        free(status->previous_value); status->previous_value=value;
        status->previous_modification=source->modification_count; status->previous_visible=visible;
        status->previous_number=source->number; status->previous_integer=source->integer;
        status->read=true; ++status->revision;
    }
    out->modification_count=status->revision;
    if (!visible) { out->value="0"; out->number=0; out->integer=0; }
    return true;
}
static bool update_fields(q3_call *call,uint64_t pointer,const qa_cvar_view **resolved,qa_error *error)
{
    uint8_t input[272];
    if (!q3_read(call, pointer, input, sizeof(input), error)) return false;
    int32_t handle = qa_load_i32le(input);
    const qa_cvar_view *view=NULL;
    uint64_t modification=0;
    if (handle<=-3) {
        size_t index=(size_t)(-(int64_t)handle-3);
        if (index>=call->host->cvar_binding_count)
            return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
        if (!routed_view(call,call->host->cvar_bindings+index,&view,&modification,error)) return false;
    } else if (handle==ENGINE_CHEATS_HANDLE && call->host->options.role==QA_QVM_GAME &&
        call->host->options.engine_cvars) {
        q3_cvar_access access;
        if (!access_name(call,"sv_cheats",&access,error)) return false;
        if (access.registry!=call->host->options.engine_cvars)
            return q3_fail(error,QA_ERROR_ARGUMENT,0,"GAME cheats handle changed its actual ENGINE namespace");
        view=find(access,"sv_cheats");
        if (view) modification=(uint32_t)view->modification_count;
    } else if (handle>=0) {
        qa_q3_host_options *options=&call->host->options;
        if (options->console) {
            qa_command_context command;
            if (!qa_console_cvar_context(options->console,&options->command_context,&command,error) ||
                !qa_console_cvar_handle(options->console,&command,options->cvars,(size_t)handle,&view,error))
                return false;
        } else {
            if ((size_t)handle>=qa_cvars_handle_count(options->cvars))
                return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
            view=qa_cvars_handle(options->cvars,(size_t)handle);
        }
        if (view) modification=(uint32_t)view->modification_count;
    } else return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
    *resolved=view;
    qa_cvar_view effective;
    bool status=view && call->host->options.role==QA_QVM_CGAME && status_name(view->name);
    if (status) {
        if (!effective_status(call,view,&effective,error)) return false;
        view=&effective; modification=view->modification_count;
    }
    if (!view || (handle<=-3?
        qa_load_i32le(input+4)>=0 && modification==(uint64_t)qa_load_i32le(input+4):
        modification==qa_load_u32le(input+4))) return true;
    size_t length = strlen(view->value);
    int32_t integer = view->integer;
    uint32_t bits; memcpy(&bits, &view->number, sizeof(bits));
    uint8_t value[256] = {0};
    if (length <= 255) memcpy(value, view->value, length);
    if (!q3_write_word(call, pointer + 4, (uint32_t)modification, error)) return false;
    if (length > 255)
        return q3_fail(error, QA_ERROR_FORMAT, length, "Cvar_Update exceeds MAX_CVAR_VALUE_STRING");
    return q3_write(call, pointer + 16, (qa_bytes){value, sizeof(value)}, error) &&
           q3_write_word(call, pointer + 8, bits, error) &&
           q3_write_word(call, pointer + 12, (uint32_t)integer, error);
}
static bool status_name(const char *name)
{
    const char *status="cg_drawstatus";
    while (*name && *status) {
        unsigned char c=(unsigned char)*name++;
        if (c>='A' && c<='Z') c+='a'-'A';
        if (c!=(unsigned char)*status++) return false;
    }
    return !*name && !*status;
}
static bool cache_note(q3_call *call,uint64_t address,const qa_cvar_view *view,qa_error *error)
{
    qa_q3_host *host=call->host;
    if (host->options.role!=QA_QVM_CGAME) return true;
    int32_t pointer=0;
    if (call->vm && (!call->source_call ||
        !qa_qvm_call_argument(call->source_call,0,&pointer,error))) return false;
    size_t index=host->cvar_cache_count;
    for (size_t i=0;i<host->cvar_cache_count;++i) {
        const q3_cvar_cache *record=host->cvar_caches+i;
        if (record->vm==call->vm && record->native==call->native &&
            (call->vm?record->pointer==pointer:record->address==address)) { index=i; break; }
    }
    if (!view || !status_name(view->name)) {
        if (index<host->cvar_cache_count) {
            memmove(host->cvar_caches+index,host->cvar_caches+index+1,
                (host->cvar_cache_count-index-1)*sizeof(*host->cvar_caches));
            --host->cvar_cache_count;
        }
        return true;
    }
    uint8_t token[4];
    if (!q3_read(call,address,token,sizeof(token),error)) return false;
    if (index==host->cvar_cache_count) {
        if (host->cvar_cache_count==SIZE_MAX/sizeof(*host->cvar_caches))
            return q3_fail(error,QA_ERROR_MEMORY,0,"Q3 cvar cache inventory exceeds address space");
        q3_cvar_cache *grown=realloc(host->cvar_caches,(index+1)*sizeof(*grown));
        if (!grown) return q3_fail(error,QA_ERROR_MEMORY,0,"Retaining original CGAME cvar cache address");
        host->cvar_caches=grown; ++host->cvar_cache_count;
    }
    host->cvar_caches[index]=(q3_cvar_cache){.vm=call->vm,.native=call->native,
        .memory=call->memory,.pointer=pointer,.address=address,.handle=qa_load_i32le(token)};
    return true;
}
static bool update(q3_call *call,uint64_t pointer,qa_error *error)
{
    const qa_cvar_view *view=NULL;
    return update_fields(call,pointer,&view,error) && cache_note(call,pointer,view,error);
}
static bool refresh(void *context,const qa_command_context *command,qa_error *error)
{
    (void)command;
    qa_q3_host *host=context;
    bool visible;
    if (host->options.cvar_status.visible &&
        !host->options.cvar_status.visible(host->options.cvar_status.context,host,&visible,error)) return false;
    for (size_t i=0;i<host->cvar_cache_count;) {
        qa_q3_host_cvar_cache record; bool found=false;
        if (!qa_q3_host_cvar_cache_read(host,i,&record,&found,error)) return false;
        if (!found) {
            memmove(host->cvar_caches+i,host->cvar_caches+i+1,
                (host->cvar_cache_count-i-1)*sizeof(*host->cvar_caches));
            --host->cvar_cache_count; continue;
        }
        const q3_cvar_cache *cache=host->cvar_caches+i;
        q3_call call={.host=host,.memory=cache->memory,.vm=cache->vm,.native=cache->native,
            .native_profile=host->native_profile,.native_host=host->native_host};
        const qa_cvar_view *view=NULL;
        if (!update_fields(&call,cache->address,&view,error)) return false;
        ++i;
    }
    return true;
}
bool qa_q3_host_cvar_cache_refresh(qa_q3_host *host,qa_error *error)
{
    if (!host || host->retired || host->restore_pending || host->calls ||
        host->options.role!=QA_QVM_CGAME)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Status refresh requires its returned actual CGAME host");
    ++host->calls;
    bool ok=host->options.console?qa_console_cvar_enter(host->options.console,
        &host->options.command_context,NULL,NULL,refresh,host,error):refresh(host,NULL,error);
    --host->calls;
    if (!host->calls) qa_arena_reset(&host->scratch);
    return ok;
}

static bool register_vm(q3_call *call, qa_error *error)
{
    qa_buffer name = {0}, value = {0};
    bool ok = q3_string(call, call->arguments[1], &name, error) &&
              q3_string(call, call->arguments[2], &value, error);
    q3_cvar_access access={0}; size_t handle=0;
    const qa_cvar_view *view=NULL;
    if (ok) ok=access_name(call,(const char *)name.data,&access,error);
    bool engine=ok && engine_name(call,(const char *)name.data) &&
        access.registry==call->host->options.engine_cvars;
    uint64_t owner=!engine && access.registry==call->host->options.cvars?call->host->options.service_owner:0;
    if (ok) ok=access.edit?qa_cvars_edit_vm_bind(access.edit,(const char *)name.data,
        (const char *)value.data,(uint32_t)call->arguments[3],owner,&handle,error):
        qa_cvars_vm_bind(access.registry,(const char *)name.data,(const char *)value.data,
            (uint32_t)call->arguments[3],owner,&handle,error);
    if (ok) view=access.edit?qa_cvars_edit_handle(access.edit,handle):qa_cvars_handle(access.registry,handle);
    if (ok && !view) ok=q3_fail(error,QA_ERROR_NOT_FOUND,0,"Q3 VM registration lacks its actual admitted handle");
    if (ok && !engine && access.registry==call->host->options.cvars)
        ok=apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RETAIN_SHARED,.name=view->name},error);
    int32_t token=0;
    if (ok) {
        if (engine) token=ENGINE_CHEATS_HANDLE;
        else if (access.registry==call->host->options.cvars && handle<=INT32_MAX) token=(int32_t)handle;
        else ok=bind_routed(call,access,handle,view,&token,error);
    }
    if (ok && call->arguments[0]) {
        uint8_t admitted[272];
        ok = q3_read(call, call->arguments[0], admitted, sizeof(admitted), error) &&
                 q3_write_word(call, call->arguments[0], (uint32_t)token, error) &&
                 q3_write_word(call, call->arguments[0] + 4, UINT32_MAX, error) &&
                 update(call, call->arguments[0], error);
    }
    qa_buffer_free(&name); qa_buffer_free(&value);
    return ok;
}
static void info_print(q3_call *call,const qa_command_context *command,const char *text)
{ qa_console_emit(call->host->options.console,command,text); }
static bool info_pair(q3_call *call,const qa_command_context *command,char output[1024],
    const char *name,const char *value,qa_error *error)
{
    const char *message=strchr(name,'\\') || strchr(value,'\\')?"Can't use keys or values with a \\\n":
        strchr(name,';') || strchr(value,';')?"Can't use keys or values with a semicolon\n":
        strchr(name,'"') || strchr(value,'"')?"Can't use keys or values with a \"\n":NULL;
    if (message) { info_print(call,command,message); return true; }
    char removed[1024]; memcpy(removed,output,strlen(output)+1);
    if (!qa_q3_info_set(removed,sizeof(removed),name,"",error)) return false;
    if (!*value) { memcpy(output,removed,strlen(removed)+1); return true; }
    char pair[1024]; size_t pair_size=0;
    const char *parts[]={"\\",name,"\\",value};
    for (unsigned i=0;i<4;++i) {
        size_t remaining=sizeof(pair)-1-pair_size,length=strlen(parts[i]);
        size_t amount=length<remaining?length:remaining;
        memcpy(pair+pair_size,parts[i],amount); pair_size+=amount;
    }
    size_t removed_size=strlen(removed),total=removed_size+pair_size;
    if (total>1024) {
        memcpy(output,removed,strlen(removed)+1);
        info_print(call,command,"Info string length exceeded\n"); return true;
    }
    if (total==1024) return q3_fail(error,QA_ERROR_FORMAT,0,"Q3 info assignment overflows its source terminator");
    memcpy(output,pair,pair_size);
    memcpy(output+pair_size,removed,removed_size+1); return true;
}
static bool info_string(q3_call *call,uint32_t flags,qa_buffer *out,qa_error *error)
{
    qa_q3_host_options *options=&call->host->options;
    if (!options->console) return qa_cvars_info(options->cvars,flags,1024,out,error);
    qa_command_context command;
    if (!qa_console_cvar_context(options->console,&options->command_context,&command,error)) return false;
    char output[1024]={0};
    for (size_t ordinal=0;;++ordinal) {
        qa_cvars *registry=qa_console_visible_cvars(options->console,&command,ordinal);
        if (!registry) break;
        for (size_t index=0;;++index) {
            const qa_cvar_view *view=NULL;
            if (!qa_console_cvar_snapshot_at(options->console,&command,registry,index,&view,error)) return false;
            if (!view) break;
            if (strcmp(view->name,qa_cvars_canonical_name(registry,view->name))) continue;
            qa_console_dialect dialect=qa_cvars_dialect(registry);
            uint32_t projected=dialect==QA_CONSOLE_Q3?view->flags:
                view->flags&(QA_CVAR_USERINFO|QA_CVAR_SERVERINFO);
            if (!(projected&flags) || ((dialect==QA_CONSOLE_Q2 || dialect==QA_CONSOLE_Q2_RERELEASE) &&
                (view->flags&QA_Q2_CVAR_PRIVATE))) continue;
            q3_cvar_access access;
            if (!access_name(call,view->name,&access,error)) return false;
            if (access.registry!=registry) continue;
            if (!info_pair(call,&command,output,view->name,view->value,error)) return false;
        }
    }
    char *copy=copy_text(output,error);
    if (!copy) return false;
    *out=(qa_buffer){(uint8_t *)copy,strlen(copy)+1}; return true;
}

static q3_service_result cvars_selected(q3_call *call, int32_t *result, qa_error *error)
{
    bool ui = call->host->options.role == QA_QVM_UI;
    bool game = call->host->options.role == QA_QVM_GAME;
    int32_t trap = call->service;
    qa_cvars *cvars = call->host->options.cvars;
    if (!cvars) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 cvar owner is unbound");
        return Q3_FAILED;
    }
    if (trap == (ui ? 50 : 3)) return register_vm(call, error) ? Q3_COMPLETED : Q3_FAILED;
    if (trap == (ui ? 51 : 4)) return update(call, call->arguments[0], error) ? Q3_COMPLETED : Q3_FAILED;
    if (ui && trap == 9) {
        qa_buffer info = {0};
        bool ok = info_string(call, (uint32_t)call->arguments[0], &info, error) &&
                  q3_write_string(call, call->arguments[1], (const char *)info.data,
                                    q3_integer(call, 2), error);
        qa_buffer_free(&info);
        return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    qa_buffer name = {0}, value = {0};
    if (!q3_string(call, call->arguments[0], &name, error)) return Q3_FAILED;
    const char *key = (const char *)name.data;
    q3_cvar_access access;
    if (!access_name(call,key,&access,error)) { qa_buffer_free(&name); return Q3_FAILED; }
    cvars=access.registry;
    const qa_cvar_view *view=find(access,key);
    bool ok = true;
    if (trap == (ui ? 3 : 5)) {
        ok = call->arguments[1] ?
                 q3_string(call, call->arguments[1], &value, error) &&
                 apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,
                    .name=key,.value=(const char *)value.data,.force=true},error) :
                 apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RESET,.name=key,.force=true},error);
    } else if (trap == (ui ? 5 : game ? 7 : 6)) {
        qa_cvar_view effective;
        if (view && !effective_status(call,view,&effective,error)) ok=false;
        else if (view) view=&effective;
        if (ok && view) ok = q3_write_string(call, call->arguments[1], view->value, q3_integer(call, 2), error);
        else if (ok) {
            const uint8_t zero = 0;
            ok = q3_write(call, call->arguments[1], (qa_bytes){&zero, 1}, error);
        }
    } else if (ui && trap == 4) {
        float number = view ? view->number : 0;
        memcpy(result, &number, sizeof(number));
    } else if (game && trap == 6) {
        if (call->native && call->native_profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) {
            float number = view ? view->number : 0;
            memcpy(result, &number, sizeof(number));
        } else *result = view ? view->integer : 0;
    } else if (ui && trap == 6) {
        ok=apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_NUMBER,
            .name=key,.number=q3_float(call,1)},error);
    } else if (ui && trap == 7) {
        ok=apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RESET,.name=key},error);
    } else if (ui && trap == 8) {
        ok = q3_string(call, call->arguments[1], &value, error) &&
             declare(call,access,key,(const char *)value.data,(uint32_t)call->arguments[2],error) &&
             (cvars!=call->host->options.cvars || apply(call,access,
                &(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RETAIN_SHARED,.name=key},error));
    }
    qa_buffer_free(&name); qa_buffer_free(&value);
    return ok ? Q3_COMPLETED : Q3_FAILED;
}

typedef struct cvar_operation {
    q3_call *call;
    int32_t *result;
} cvar_operation;
static bool entered(void *context,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    const qa_q3_host *host=context;
    const qa_q3_host_cvar_entry_services *services=&host->options.cvar_entry;
    if (!host->calls || console!=host->options.console || !services->entered)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Cvar syscall lacks its actual entered host lifetime");
    return services->entered(services->context,host,console,command,error);
}
static bool operate(void *context,const qa_command_context *command,qa_error *error)
{
    (void)command;
    cvar_operation *operation=context;
    return cvars_selected(operation->call,operation->result,error)==Q3_COMPLETED;
}
q3_service_result q3_cvars(q3_call *call,int32_t *result,qa_error *error)
{
    bool ui=call->host->options.role==QA_QVM_UI;
    bool game=call->host->options.role==QA_QVM_GAME;
    int32_t trap=call->service;
    if (ui?!((trap>=3 && trap<=9) || trap==50 || trap==51):
        !(trap>=3 && trap<=(game?7:6))) return Q3_UNHANDLED;
    qa_q3_host_options *options=&call->host->options;
    if (!options->console) return cvars_selected(call,result,error);
    cvar_operation operation={call,result};
    return qa_console_cvar_enter(options->console,&options->command_context,
        entered,call->host,operate,&operation,error)?Q3_COMPLETED:Q3_FAILED;
}
