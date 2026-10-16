#include "internal.h"
#include "qa/console_cvars_prepare.h"
#include "qa/console_cvar_observer.h"
#include "qa/cvars_alias.h"
#include "qa/source_save.h"

/* Registry handles are nonnegative size_t values admitted through INT32_MAX.
 * This opaque signed source token cannot alias any real private handle. */
enum { ENGINE_CHEATS_HANDLE = -2 };
enum { CACHE_QVM_ADDRESS = 1, CACHE_NATIVE_ADDRESS = 2 };
static bool status_name(const char *);

static qa_q3_host_cvar_namespace private_namespace(const qa_q3_host *host)
{ return host->options.role == QA_QVM_GAME ? QA_Q3_HOST_CVAR_GAME : QA_Q3_HOST_CVAR_CLIENT; }

static bool namespace_resolve(const qa_q3_host *host, qa_q3_host_cvar_namespace reference,
    qa_cvars **out, qa_error *error)
{
    const qa_q3_host_cvar_services *services = &host->options.cvar_namespaces;
    if (services->resolve) return services->resolve(services->context, reference, out, error);
    if (services->reference || reference != private_namespace(host) || !host->options.cvars)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 cvar namespace has no actual registry owner");
    *out = host->options.cvars; return true;
}

static bool namespace_reference(const qa_q3_host *host, const qa_cvars *registry,
    qa_q3_host_cvar_namespace *out, qa_error *error)
{
    const qa_q3_host_cvar_services *services = &host->options.cvar_namespaces;
    if (services->reference) return services->reference(services->context, registry, out, error);
    if (services->resolve || !registry || registry != host->options.cvars)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 cvar registry has no actual namespace owner");
    *out = private_namespace(host); return true;
}

void q3_cvars_bindings_free(q3_cvar_binding *bindings,size_t count)
{
    for (size_t i=0;i<count;++i) { free(bindings[i].name); free(bindings[i].previous_value.overflow); }
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
static const char *mirror_text(const q3_cvar_text *text)
{ return text->overflow ? text->overflow : text->value; }
static bool mirror_changed(const q3_cvar_text *text,const char *value)
{ return strcmp(mirror_text(text),value)!=0; }
static bool mirror_store(q3_cvar_text *text,const char *value,qa_error *error)
{
    size_t length=strlen(value);
    char *overflow=NULL;
    /* Overlength source observations keep their exact checkpoint continuation. */
    if (length>=sizeof(text->value)) {
        overflow=copy_text(value,error);
        if (!overflow) return false;
    }
    if (text->overflow) free(text->overflow);
    text->overflow=overflow; text->length=length;
    size_t prefix=length<sizeof(text->value)?length:sizeof(text->value)-1;
    memcpy(text->value,value,prefix); text->value[prefix]=0;
    return true;
}
static bool mirror_fields(qa_source_save_io *io,q3_cvar_text *text)
{
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?text->length:0;
    if (!qa_source_save_count(io,&length,SIZE_MAX-1)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io,(void *)mirror_text(text),length);
    if (io->offset>io->input.size || length>io->input.size-io->offset ||
        memchr(io->input.data+io->offset,0,length))
        return q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid Q3 cvar mirror text");
    char *value=text->value;
    if (length>=sizeof(text->value)) {
        text->overflow=malloc(length+1);
        if (!text->overflow) return q3_fail(io->error,QA_ERROR_MEMORY,0,"Restoring Q3 cvar mirror text");
        value=text->overflow;
    }
    text->length=length; value[length]=0;
    if (!qa_source_save_bytes(io,value,length)) return false;
    if (text->overflow) {
        memcpy(text->value,value,sizeof(text->value)-1);
        text->value[sizeof(text->value)-1]=0;
    }
    return true;
}
static bool binding_fields(qa_source_save_io *io,q3_cvar_binding *binding)
{
    uint32_t reference=binding->reference,dialect=binding->dialect;
    bool ok=qa_source_save_u32(io,&reference) && qa_source_save_u32(io,&dialect) &&
        binding_text(io,&binding->name) &&
        qa_source_save_bool(io,&binding->read) && qa_source_save_u64(io,&binding->revision);
    if (io->direction==QA_SOURCE_SAVE_READ) {
        binding->reference=(qa_q3_host_cvar_namespace)reference;
        binding->dialect=(qa_ruleset_id)dialect;
    }
    if (!ok) return false;
    if (reference<QA_Q3_HOST_CVAR_ENGINE || reference>QA_Q3_HOST_CVAR_SELECTED_VIEW ||
        dialect>QA_RULESET_Q3 ||
        (binding->read?!binding->revision:binding->revision!=0))
        return q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid Q3 cvar namespace or mirror identity");
    if (!qa_cvars_name_valid(binding->dialect,binding->name))
        return q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid authored Q3 cvar binding name");
    return !binding->read || (qa_source_save_bool(io,&binding->previous_current) &&
        mirror_fields(io,&binding->previous_value) &&
        qa_source_save_f32(io,&binding->previous_number) &&
        qa_source_save_i32(io,&binding->previous_integer));
}
static bool status_fields(qa_source_save_io *io,q3_cvar_status *status)
{
    if (!qa_source_save_bool(io,&status->read)) return false;
    if (!status->read) return (!status->revision && !status->previous_value.length && !status->previous_value.overflow) ||
        q3_fail(io->error,QA_ERROR_FORMAT,0,"Unread status mirror retains an invalid continuation");
    uint32_t bits; memcpy(&bits,&status->previous_number,sizeof(bits));
    bool ok=qa_source_save_u32(io,&status->revision) &&
        mirror_fields(io,&status->previous_value) &&
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
    qa_cvars *registry=NULL; qa_q3_host_cvar_namespace reference=0;
    if (!namespace_resolve(host,binding->reference,&registry,error)) return false;
    if (!registry || qa_cvars_dialect(registry)!=binding->dialect)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar binding namespace is absent");
    if (!namespace_reference(host,registry,&reference,error)) return false;
    if (reference!=binding->reference)
        return q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cvar binding differs from its canonical namespace reference");
    *out=registry; return true;
}
static bool cache_fields(qa_source_save_io *io,q3_cvar_cache *cache)
{
    uint64_t address = cache->native_address ? cache->address : 0;
    bool ok = qa_source_save_bool(io,&cache->native_address) &&
        qa_source_save_i32(io,&cache->pointer) && qa_source_save_u64(io,&address) &&
        qa_source_save_i32(io,&cache->handle) &&
        (((cache->native_address ? cache->pointer==0 && address!=0 : cache->pointer!=0 && address==0) &&
            cache->handle!=-1 && cache->handle!=ENGINE_CHEATS_HANDLE) ||
            q3_fail(io->error,QA_ERROR_FORMAT,0,"Invalid original CGAME cvar cache identity"));
    if (ok && io->direction==QA_SOURCE_SAVE_READ) cache->address=address;
    return ok;
}
bool q3_cvars_cache_reserve(qa_q3_host *host,size_t count,qa_error *error)
{
    size_t maximum=SIZE_MAX/sizeof(*host->cvar_caches);
    if (count>maximum)
        return q3_fail(error,QA_ERROR_MEMORY,0,"Q3 cvar cache inventory exceeds address space");
    if (!qa_id_index_reserve(&host->cvar_cache_addresses,
        count>host->cvar_cache_count?count-host->cvar_cache_count:0,error)) return false;
    if (count<=host->cvar_cache_capacity) return true;
    size_t capacity=host->cvar_cache_capacity?host->cvar_cache_capacity:32;
    while (capacity<count) {
        if (capacity>maximum/2) { capacity=count; break; }
        capacity*=2;
    }
    q3_cvar_cache *rows=qa_arena_grow(&host->cvar_cache_storage,host->cvar_caches,
        host->cvar_cache_count*sizeof(*rows),capacity*sizeof(*rows),_Alignof(q3_cvar_cache),error);
    if (!rows) return false;
    host->cvar_caches=rows; host->cvar_cache_capacity=capacity;
    return true;
}
void q3_cvars_cache_install(qa_q3_host *host,const q3_cvar_cache *rows,size_t count)
{
    qa_id_index_clear(&host->cvar_cache_addresses);
    if (count) memcpy(host->cvar_caches,rows,count*sizeof(*rows));
    host->cvar_cache_count=count;
    for (size_t i=0;i<count;++i) {
        const q3_cvar_cache *cache=host->cvar_caches+i;
        qa_id_index_insert(&host->cvar_cache_addresses,cache->native_address?CACHE_NATIVE_ADDRESS:CACHE_QVM_ADDRESS,
            cache->native_address?cache->address:(uint32_t)cache->pointer,i);
    }
}
void q3_cvars_cache_clear(qa_q3_host *host)
{
    qa_id_index_destroy(&host->cvar_cache_addresses);
    qa_arena_reset(&host->cvar_cache_storage);
    host->cvar_cache_addresses=(qa_id_index){.storage=&host->cvar_cache_storage};
    host->cvar_caches=NULL; host->cvar_cache_count=host->cvar_cache_capacity=0;
}
static void cache_remove(qa_q3_host *host,size_t index)
{
    const q3_cvar_cache *cache=host->cvar_caches+index;
    qa_id_index_remove(&host->cvar_cache_addresses,cache->native_address?CACHE_NATIVE_ADDRESS:CACHE_QVM_ADDRESS,
        cache->native_address?cache->address:(uint32_t)cache->pointer);
    memmove(host->cvar_caches+index,host->cvar_caches+index+1,
        (host->cvar_cache_count-index-1)*sizeof(*host->cvar_caches));
    --host->cvar_cache_count;
    for (size_t i=index;i<host->cvar_cache_count;++i) {
        cache=host->cvar_caches+i;
        qa_id_index_find(&host->cvar_cache_addresses,cache->native_address?CACHE_NATIVE_ADDRESS:CACHE_QVM_ADDRESS,
            cache->native_address?cache->address:(uint32_t)cache->pointer)->value=i;
    }
}
bool q3_cvars_bindings_capture(const qa_q3_host *host,qa_buffer *out,qa_error *error)
{
    qa_source_save_io io={0};
    uint8_t magic[4]={'Q','3','C','B'};
    size_t count=host->cvar_binding_count;
    bool ok=qa_source_save_writer(&io,NULL,error) && qa_source_save_bytes(&io,magic,sizeof(magic)) &&
        qa_source_save_count(&io,&count,Q3_MAX_CVARS);
    for (size_t i=0;ok && i<count;++i) {
        q3_cvar_binding copy=host->cvar_bindings[i]; qa_cvars *current=NULL;
        ok=binding_namespace(host,&copy,&current,error) && copy.handle<qa_cvars_handle_count(current);
        if (!ok && (!error || error->code==QA_OK))
            q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar checkpoint lost its actual namespace");
        const qa_cvar_view *view=ok?qa_cvars_handle(current,copy.handle):NULL;
        if (view && strcmp(view->name,copy.name))
            ok=q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar checkpoint changed its actual named binding");
        copy.previous_current=copy.registry==current && view &&
            copy.previous_modification==view->modification_count;
        if (ok) ok=binding_fields(&io,&copy);
    }
    size_t caches=host->cvar_cache_count;
    if (ok) ok=qa_source_save_count(&io,&caches,SIZE_MAX/sizeof(q3_cvar_cache));
    for (size_t i=0;ok && i<caches;++i) {
        q3_cvar_cache copy=host->cvar_caches[i];
        if (host->options.role!=QA_QVM_CGAME ||
            (host->vm ? copy.vm!=host->vm || copy.native || copy.native_address :
                !host->native || copy.vm || copy.native!=host->native || !copy.native_address ||
                qa_native_get_backend(host->native)!=QA_NATIVE_BACKEND_OWNED_PROCESS))
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
    qa_source_save_io io={0}; uint8_t magic[4]; size_t count=0;
    if (!out || *out || !out_count || !out_caches || *out_caches || !out_cache_count ||
        !out_status || out_status->read || out_status->revision ||
        out_status->previous_value.length || out_status->previous_value.overflow ||
        !qa_source_save_reader(&io,NULL,bytes,error))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 cvar decode requires empty actual candidate output");
    bool ok=qa_source_save_bytes(&io,magic,sizeof(magic)) &&
        qa_source_save_count(&io,&count,Q3_MAX_CVARS);
    if (ok && (memcmp(magic,"Q3CB",4) || count>bytes.size-io.offset))
        ok=q3_fail(error,QA_ERROR_FORMAT,0,"Unsupported Q3 cvar binding continuation");
    q3_cvar_binding *bindings=ok?calloc(Q3_MAX_CVARS,sizeof(*bindings)):NULL;
    if (ok && !bindings) ok=q3_fail(error,QA_ERROR_MEMORY,0,"Restoring routed Q3 cvar bindings");
    for (size_t i=0;ok && i<count;++i) {
        ok=binding_fields(&io,bindings+i);
        for (size_t j=0;ok && j<i;++j)
            if (bindings[j].reference==bindings[i].reference && !strcmp(bindings[j].name,bindings[i].name))
                ok=q3_fail(error,QA_ERROR_FORMAT,0,"Duplicate Q3 named cvar binding");
    }
    size_t cache_count=0;
    if (ok) ok=qa_source_save_count(&io,&cache_count,SIZE_MAX/sizeof(q3_cvar_cache));
    if (ok && cache_count>(bytes.size-io.offset)/17)
        ok=q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cache inventory exceeds retained bytes");
    q3_cvar_cache *caches=ok && cache_count?calloc(cache_count,sizeof(*caches)):NULL;
    if (ok && cache_count && !caches) ok=q3_fail(error,QA_ERROR_MEMORY,0,"Restoring original Q3 cvar caches");
    qa_id_index cache_addresses={0};
    if (ok) ok=qa_id_index_reserve(&cache_addresses,cache_count,error);
    for (size_t i=0;ok && i<cache_count;++i) {
        ok=cache_fields(&io,caches+i);
        if (ok && caches[i].handle<=-3 && (uint64_t)(-(int64_t)caches[i].handle-3)>=count)
            ok=q3_fail(error,QA_ERROR_FORMAT,0,"Original Q3 cache has no routed handle owner");
        if (ok) {
            qa_string_id key=caches[i].native_address?CACHE_NATIVE_ADDRESS:CACHE_QVM_ADDRESS;
            uint64_t address=caches[i].native_address?caches[i].address:(uint32_t)caches[i].pointer;
            if (qa_id_index_find(&cache_addresses,key,address))
                ok=q3_fail(error,QA_ERROR_FORMAT,0,"Duplicate original Q3 cvar cache pointer");
            else qa_id_index_insert(&cache_addresses,key,address,i);
        }
    }
    q3_cvar_status status={0};
    if (ok) ok=status_fields(&io,&status) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    qa_id_index_destroy(&cache_addresses);
    if (!ok) { q3_cvars_bindings_free(bindings,bindings?count:0); free(caches); free(status.previous_value.overflow); return false; }
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
        const qa_cvar_view *view=qa_cvars_find(registry,binding->name);
        if (!view || strcmp(view->name,binding->name))
            return q3_fail(error,QA_ERROR_FORMAT,0,"Restored Q3 binding lacks its current authored declaration");
        size_t handle=0;
        uint64_t modification=view->modification_count;
        if (!qa_cvars_vm_rebind(registry,binding->name,&handle,error)) return false;
        binding->handle=handle;
        binding->registry=registry;
        binding->value_handle=qa_cvars_resolve(registry,binding->name);
        binding->status=status_name(binding->name);
        binding->previous_modification=binding->previous_current?modification:0;
    }
    host->draw_status=qa_cvars_resolve(host->options.cvars,"cg_drawStatus");
    if (host->cvar_cache_count && ((!host->vm && !host->native) || host->options.role!=QA_QVM_CGAME))
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
        if (host->vm) {
            if (cache->native_address || !qa_qvm_span(host->vm,cache->pointer,0,272,&span,error))
                return q3_fail(error,QA_ERROR_FORMAT,0,"Restored QVM cvar cache changed its memory backend");
            cache->address=(uint64_t)qa_qvm_memory_size(host->vm)+qa_qvm_mask_address(host->vm,cache->pointer);
        } else if (!cache->native_address || qa_native_get_backend(host->native)!=QA_NATIVE_BACKEND_OWNED_PROCESS ||
            !qa_native_range_check(host->native,cache->address,272,QA_NATIVE_MEMORY_READ|QA_NATIVE_MEMORY_WRITE,error))
            return q3_fail(error,QA_ERROR_FORMAT,0,"Restored native cvar cache changed its actual process memory");
        cache->vm=host->vm; cache->native=host->native; cache->memory=host->memory;
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
    if (!options->cvar_entry.entered) return true;
    return qa_console_cvar_context(options->console,&options->command_context,&out->command,error) &&
        qa_console_cvar_access(options->console,&out->command,name,&out->registry,&out->edit,error);
}
static bool access_enter(q3_cvar_access access,qa_error *error)
{ return !access.edit || qa_cvars_edit_enter(access.edit,access.registry,error); }
static bool access_leave(q3_cvar_access access,bool okay,qa_error *error)
{
    qa_error returned={0};
    bool left=!access.edit || qa_cvars_edit_leave(access.edit,access.registry,&returned);
    if (!left && okay && error) *error=returned;
    return okay && left;
}
static bool find(q3_cvar_access access,const char *name,const qa_cvar_view **out,qa_error *error)
{
    if (!access_enter(access,error)) return false;
    *out=qa_cvars_find(access.registry,name);
    return access_leave(access,true,error);
}
static bool apply(q3_call *call,q3_cvar_access access,const qa_cvars_edit_command *command,qa_error *error)
{
    if (call->host->options.cvar_entry.entered)
        return qa_console_cvar_apply(call->host->options.console,&access.command,command,error);
    switch (command->kind) {
    case QA_CVARS_EDIT_SET: return qa_cvars_set(access.registry,command->name,command->value,command->force,error);
    case QA_CVARS_EDIT_RESET: return qa_cvars_reset(access.registry,command->name,command->force,error);
    case QA_CVARS_EDIT_SET_NUMBER: return qa_cvars_set_number(access.registry,command->name,command->number,error);
    case QA_CVARS_EDIT_REGISTER: return qa_cvars_register(access.registry,command->name,command->value,
        command->flags,command->owner,command->description,error) &&
        (command->save_policy == QA_CVAR_SAVE_UNCLASSIFIED ||
         qa_cvars_declare_save_policy(access.registry,command->name,command->save_policy,error));
    case QA_CVARS_EDIT_SAVE_POLICY:
        return qa_cvars_declare_save_policy(access.registry,command->name,command->save_policy,error);
    case QA_CVARS_EDIT_RETAIN_SHARED: return qa_cvars_retain_shared(access.registry,command->name,error);
    default: return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 named cvar operation is unbound");
    }
}
static bool classify(q3_call *call,q3_cvar_access access,const char *name,
    const qa_cvar_view **actual,qa_error *error)
{
    if (!*actual || (*actual)->save_policy!=QA_CVAR_SAVE_UNCLASSIFIED) return true;
    if (!apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SAVE_POLICY,.name=name,
        .save_policy=call->host->options.role==QA_QVM_GAME?
            QA_CVAR_SAVE_GAMEPLAY:QA_CVAR_SAVE_SETTING},error)) return false;
    return find(access,name,actual,error);
}
static bool declare(q3_call *call,q3_cvar_access access,const char *name,const char *value,
    uint32_t flags,qa_error *error)
{
    if (qa_cvars_dialect(access.registry)!=QA_RULESET_Q3) {
        const char *canonical=qa_cvars_canonical_name(access.registry,name);
        if (strcmp(canonical,name) && (flags&(QA_CVAR_USERINFO|QA_CVAR_SERVERINFO|QA_CVAR_SYSTEMINFO)))
            return q3_fail(error,QA_ERROR_ARGUMENT,0,"Guest alias requires its canonical protocol info-key mapping");
        const qa_cvar_view *actual=NULL;
        if (!find(access,name,&actual,error)) return false;
        return (actual!=NULL ||
            q3_fail(error,QA_ERROR_NOT_FOUND,0,"Unknown guest cvar requires its actual Q3 seat owner")) &&
            classify(call,access,name,&actual,error);
    }
    if (!apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_REGISTER,.name=name,
        .value=value,.flags=flags,.owner=access.registry==call->host->options.cvars?
            call->host->options.service_owner:0},error)) return false;
    const qa_cvar_view *actual=NULL;
    return find(access,name,&actual,error) && classify(call,access,name,&actual,error);
}

static bool bind_routed(q3_call *call,q3_cvar_access access,size_t handle,
    const qa_cvar_view *view,int32_t *token,qa_error *error)
{
    qa_q3_host *host=call->host;
    qa_q3_host_cvar_namespace reference=0;
    qa_cvars *resolved=NULL;
    if (!namespace_reference(host,access.registry,&reference,error) ||
        reference<QA_Q3_HOST_CVAR_ENGINE || reference>QA_Q3_HOST_CVAR_SELECTED_VIEW ||
        !namespace_resolve(host,reference,&resolved,error) || resolved!=access.registry)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Routed Q3 cvar lacks its actual factory namespace reference");
    qa_cvar_handle value_handle=qa_cvars_resolve(access.registry,view->name);
    bool status=status_name(view->name);
    if (status && access.registry==host->options.cvars) host->draw_status=value_handle;
    for (size_t i=0;i<host->cvar_binding_count;++i) {
        const q3_cvar_binding *binding=host->cvar_bindings+i;
        if (binding->reference==reference && binding->handle==handle && !strcmp(binding->name,view->name)) {
            *token=-3-(int32_t)i; return true;
        }
    }
    if (host->cvar_binding_count>=Q3_MAX_CVARS)
        return q3_fail(error,QA_ERROR_MEMORY,0,"MAX_CVARS");
    char *name=copy_text(view->name,error);
    if (!name) return false;
    host->cvar_bindings[host->cvar_binding_count]=(q3_cvar_binding){.reference=reference,.handle=handle,
        .registry=access.registry,.value_handle=value_handle,
        .name=name,.dialect=qa_cvars_dialect(access.registry),.status=status};
    *token=-3-(int32_t)host->cvar_binding_count++; return true;
}
static bool routed_view(q3_cvar_binding *binding,const qa_cvar_view **out,
    uint64_t *modification,qa_error *error)
{
    const qa_cvar_view *view=qa_cvars_read(binding->registry,binding->value_handle);
    *out=view;
    if (!view) return true;
    if (!binding->read || !binding->previous_current || mirror_changed(&binding->previous_value,view->value) ||
        binding->previous_number!=view->number || binding->previous_integer!=view->integer ||
        binding->previous_modification!=view->modification_count) {
        if (binding->revision==UINT64_MAX)
            return q3_fail(error,QA_ERROR_FORMAT,0,"Q3 cvar mirror revision is exhausted");
        if (!mirror_store(&binding->previous_value,view->value,error)) return false;
        binding->previous_number=view->number;
        binding->previous_integer=view->integer; binding->previous_modification=view->modification_count;
        binding->read=true; binding->previous_current=true; ++binding->revision;
    }
    *modification=binding->revision; return true;
}

static bool effective_status(q3_call *call,const qa_cvar_view *source,bool is_status,
    qa_cvar_view *out,qa_error *error)
{
    *out=*source;
    qa_q3_host *host=call->host;
    if (host->options.role!=QA_QVM_CGAME || !is_status) return true;
    bool visible=true;
    const qa_q3_host_cvar_status_services *services=&host->options.cvar_status;
    if (services->visible && !services->visible(services->context,host,&visible,error)) return false;
    q3_cvar_status *status=&host->cvar_status;
    if (!status->read || status->previous_modification!=source->modification_count ||
        status->previous_visible!=visible || mirror_changed(&status->previous_value,source->value) ||
        status->previous_number!=source->number || status->previous_integer!=source->integer) {
        if (status->revision==INT32_MAX)
            return q3_fail(error,QA_ERROR_FORMAT,0,"CGAME status cvar revision is exhausted");
        if (!mirror_store(&status->previous_value,source->value,error)) return false;
        status->previous_modification=source->modification_count; status->previous_visible=visible;
        status->previous_number=source->number; status->previous_integer=source->integer;
        status->read=true; ++status->revision;
    }
    out->modification_count=status->revision;
    if (!visible) { out->value="0"; out->number=0; out->integer=0; }
    return true;
}
static bool update_fields(q3_call *call,uint64_t pointer,bool *resolved_status,qa_error *error)
{
    uint8_t input[272];
    if (!q3_read(call, pointer, input, sizeof(input), error)) return false;
    int32_t handle = qa_load_i32le(input);
    const qa_cvar_view *view=NULL;
    uint64_t modification=0;
    bool status=false;
    if (handle<=-3) {
        size_t index=(size_t)(-(int64_t)handle-3);
        if (index>=call->host->cvar_binding_count)
            return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
        q3_cvar_binding *binding=call->host->cvar_bindings+index;
        if (!routed_view(binding,&view,&modification,error)) return false;
        status=binding->status;
    } else if (handle==ENGINE_CHEATS_HANDLE && call->host->options.role==QA_QVM_GAME &&
        call->host->options.engine_cvars) {
        view=qa_cvars_read(call->host->options.engine_cvars,call->host->engine_cheats);
        if (view) modification=(uint32_t)view->modification_count;
    } else if (handle>=0) {
        qa_q3_host_options *options=&call->host->options;
        if ((size_t)handle>=qa_cvars_handle_count(options->cvars))
            return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
        view=qa_cvars_handle(options->cvars,(size_t)handle);
        status=view && view==qa_cvars_read(options->cvars,call->host->draw_status);
        if (view) modification=(uint32_t)view->modification_count;
    } else return q3_fail(error,QA_ERROR_FORMAT,0,"Cvar_Update: handle out of range");
    status=view && call->host->options.role==QA_QVM_CGAME && status;
    *resolved_status=status;
    qa_cvar_view effective;
    if (status) {
        if (!effective_status(call,view,status,&effective,error)) return false;
        view=&effective; modification=view->modification_count;
    }
    if (!view || (handle<=-3?
        qa_load_i32le(input+4)>=0 && modification==(uint64_t)qa_load_i32le(input+4):
        modification==qa_load_u32le(input+4))) return true;
    size_t length = strlen(view->value);
    int32_t integer = view->integer;
    uint32_t bits; memcpy(&bits, &view->number, sizeof(bits));
    uint8_t value[Q3_CVAR_VALUE_BYTES] = {0};
    if (length < sizeof(value)) memcpy(value, view->value, length);
    if (!q3_write_word(call, pointer + 4, (uint32_t)modification, error)) return false;
    if (length >= sizeof(value))
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
static bool cache_note(q3_call *call,uint64_t address,bool status,qa_error *error)
{
    qa_q3_host *host=call->host;
    if (host->options.role!=QA_QVM_CGAME) return true;
    int32_t pointer=0;
    if (call->vm && (!call->source_call ||
        !qa_qvm_call_argument(call->source_call,0,&pointer,error))) return false;
    qa_string_id key=call->native?CACHE_NATIVE_ADDRESS:CACHE_QVM_ADDRESS;
    uint64_t source_address=call->vm?(uint32_t)pointer:address;
    qa_id_index_entry *entry=qa_id_index_find(&host->cvar_cache_addresses,key,source_address);
    size_t index=entry?entry->value:host->cvar_cache_count;
    if (!status) {
        if (entry) cache_remove(host,index);
        return true;
    }
    uint8_t token[4];
    if (!q3_read(call,address,token,sizeof(token),error)) return false;
    if (index==host->cvar_cache_count) {
        if (host->cvar_cache_count==SIZE_MAX/sizeof(*host->cvar_caches))
            return q3_fail(error,QA_ERROR_MEMORY,0,"Q3 cvar cache inventory exceeds address space");
        if (!q3_cvars_cache_reserve(host,index+1,error)) return false;
        qa_id_index_insert(&host->cvar_cache_addresses,key,source_address,index);
        ++host->cvar_cache_count;
    }
    host->cvar_caches[index]=(q3_cvar_cache){.vm=call->vm,.native=call->native,
        .memory=call->memory,.pointer=pointer,.address=address,.native_address=call->native!=NULL,
        .handle=qa_load_i32le(token)};
    return true;
}
static bool update(q3_call *call,uint64_t pointer,qa_error *error)
{
    bool status=false;
    return update_fields(call,pointer,&status,error) && cache_note(call,pointer,status,error);
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
            cache_remove(host,i); continue;
        }
        const q3_cvar_cache *cache=host->cvar_caches+i;
        q3_call call={.host=host,.memory=cache->memory,.vm=cache->vm,.native=cache->native,
            .native_profile=host->native_profile,.native_host=host->native_host};
        bool status=false;
        if (!update_fields(&call,cache->address,&status,error)) return false;
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
    bool ok=host->options.cvar_entry.entered?qa_console_cvar_enter(host->options.console,
        &host->options.command_context,NULL,NULL,refresh,host,error):refresh(host,NULL,error);
    --host->calls;
    if (!host->calls) qa_arena_reset(&host->scratch);
    return ok;
}

static bool register_vm(q3_call *call, qa_error *error)
{
    qa_bytes name = {0}, value = {0};
    bool ok = q3_string(call, call->arguments[1], &name, error) &&
              q3_string(call, call->arguments[2], &value, error);
    q3_cvar_access access={0}; size_t handle=0;
    const qa_cvar_view *view=NULL;
    if (ok) ok=access_name(call,(const char *)name.data,&access,error);
    bool engine=ok && engine_name(call,(const char *)name.data) &&
        access.registry==call->host->options.engine_cvars;
    uint64_t owner=!engine && access.registry==call->host->options.cvars?call->host->options.service_owner:0;
    if (ok) {
        ok=access_enter(access,error);
        if (ok) {
            ok=qa_cvars_vm_bind(access.registry,(const char *)name.data,(const char *)value.data,
                (uint32_t)call->arguments[3],owner,&handle,error);
            if (ok) view=qa_cvars_handle(access.registry,handle);
            if (ok && engine) call->host->engine_cheats=qa_cvars_resolve(access.registry,"sv_cheats");
            ok=access_leave(access,ok,error);
        }
    }
    if (ok) ok=classify(call,access,(const char *)name.data,&view,error);
    if (ok && !view) ok=q3_fail(error,QA_ERROR_NOT_FOUND,0,"Q3 VM registration lacks its actual admitted handle");
    if (ok && !engine && access.registry==call->host->options.cvars)
        ok=apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RETAIN_SHARED,.name=view->name},error);
    int32_t token=0;
    if (ok) {
        if (engine) token=ENGINE_CHEATS_HANDLE;
        else ok=bind_routed(call,access,handle,view,&token,error);
    }
    if (ok && call->arguments[0]) {
        uint8_t admitted[272];
        ok = q3_read(call, call->arguments[0], admitted, sizeof(admitted), error) &&
                 q3_write_word(call, call->arguments[0], (uint32_t)token, error) &&
                 q3_write_word(call, call->arguments[0] + 4, UINT32_MAX, error) &&
                 update(call, call->arguments[0], error);
    }

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
    if (!qa_q3_info_set(removed,sizeof(removed),name,"", QA_Q3_INFO_STRICT, NULL,error)) return false;
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
    if (!options->cvar_entry.entered) return qa_cvars_info(options->cvars,flags,1024,out,error);
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
            qa_ruleset_id dialect=qa_cvars_dialect(registry);
            uint32_t projected=dialect==QA_RULESET_Q3?view->flags:
                view->flags&(QA_CVAR_USERINFO|QA_CVAR_SERVERINFO);
            if (!(projected&flags) || ((dialect==QA_RULESET_Q2_CLASSIC || dialect==QA_RULESET_Q2_RERELEASE) &&
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
    qa_bytes name = {0}, value = {0};
    if (!q3_string(call, call->arguments[0], &name, error)) return Q3_FAILED;
    const char *key = (const char *)name.data;
    q3_cvar_access access;
    if (!access_name(call,key,&access,error)) { return Q3_FAILED; }
    cvars=access.registry;
    const qa_cvar_view *view=NULL;
    bool ok = find(access,key,&view,error) && classify(call,access,key,&view,error);
    if (!ok) { return Q3_FAILED; }
    if (trap == (ui ? 3 : 5)) {
        ok = call->arguments[1] ?
                 q3_string(call, call->arguments[1], &value, error) &&
                 apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,
                    .name=key,.value=(const char *)value.data,.force=true},error) :
                 apply(call,access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RESET,.name=key,.force=true},error);
        if (ok) ok=find(access,key,&view,error) && classify(call,access,key,&view,error);
    } else if (trap == (ui ? 5 : game ? 7 : 6)) {
        qa_cvar_view effective;
        bool status=view && call->host->options.role==QA_QVM_CGAME && status_name(view->name);
        if (view && !effective_status(call,view,status,&effective,error)) ok=false;
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
    if (!options->cvar_entry.entered) return cvars_selected(call,result,error);
    cvar_operation operation={call,result};
    return qa_console_cvar_enter(options->console,&options->command_context,
        entered,call->host,operate,&operation,error)?Q3_COMPLETED:Q3_FAILED;
}
