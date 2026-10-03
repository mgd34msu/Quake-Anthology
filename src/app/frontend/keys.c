#include "keys.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct frontend_key_profile {
    frontend_key_profile *next;
    frontend_keys *owner;
    frontend_config_files *files;
    qa_q3_key *state;
    qa_cvars *cvars;
    char *saved_instance;
    uint8_t imported[34];
    uint64_t id, nonce;
    qa_application_console_scope registry;
    size_t references;
    bool demo, dedicated, imported_state, busy, detached;
};
struct frontend_keys {
    frontend_key_profile *profiles, *active;
    uint64_t next_id;
    frontend_keys_publication *publication;
    bool restoring;
};
static bool fail(qa_error *error,qa_status code,const char *text)
{ qa_error_set(error,code,0,"%s",text); return false; }
frontend_keys *frontend_keys_create(qa_error *error)
{
    frontend_keys *owner=calloc(1,sizeof(*owner));
    if (!owner) fail(error,QA_ERROR_MEMORY,"Allocating published Q3 key profile owner");
    return owner;
}
static void profile_free(frontend_key_profile *profile)
{
    qa_q3_key_destroy(profile->state); frontend_config_files_destroy(profile->files,NULL);
    volatile uint8_t *bytes=profile->imported;
    for (size_t i=0;i<sizeof(profile->imported);++i) bytes[i]=0;
    free(profile->saved_instance); free(profile);
}
bool frontend_key_profile_retain(frontend_key_profile *profile,qa_error *error)
{
    if (!profile || !profile->owner || profile->references==SIZE_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 key alias has no retained profile lifetime");
    ++profile->references; return true;
}
bool frontend_key_profile_release(frontend_key_profile *profile,qa_error *error)
{
    if (!profile) return true;
    if (!profile->owner || !profile->references || profile->busy ||
        (profile->references==1 && !frontend_config_files_idle(profile->files)) ||
        (profile->references==1 && profile->owner->active==profile))
        return fail(error,QA_ERROR_ARGUMENT,"Published or active Q3 key profile cannot retire");
    if (--profile->references) return true;
    frontend_key_profile **link=&profile->owner->profiles;
    while (*link && *link!=profile) link=&(*link)->next;
    if (!*link) return fail(error,QA_ERROR_ARGUMENT,"Q3 key profile leaves its actual roster");
    *link=profile->next; profile_free(profile); return true;
}
bool frontend_keys_destroy(frontend_keys *owner,qa_error *error)
{
    if (!owner) return true;
    if (owner->publication) return fail(error,QA_ERROR_ARGUMENT,"Q3 key publication still retains its actual candidate ticket");
    for (frontend_key_profile *profile=owner->profiles;profile;profile=profile->next)
        if (profile->busy || !frontend_config_files_idle(profile->files) ||
            profile->references>(size_t)(profile==owner->active)+(size_t)owner->restoring)
            return fail(error,QA_ERROR_ARGUMENT,"Q3 key profiles still have actual source aliases");
    while (owner->profiles) { frontend_key_profile *profile=owner->profiles; owner->profiles=profile->next; profile_free(profile); }
    free(owner); return true;
}
static bool load(frontend_key_profile *profile,bool base,qa_error *error)
{
    qa_fs_root *root=frontend_config_files_root(profile->files,base);
    qa_fs_root *fallback=frontend_config_files_loose_root(profile->files,base);
    return qa_q3_key_load_profile(profile->state,root,fallback,base?QA_Q3_KEY_BASE:QA_Q3_KEY_EXPANSION,error);
}
static bool stored(void *context,qa_q3_key_product product,qa_error *error)
{
    frontend_key_profile *profile=context;
    if (!profile || profile->busy || !profile->state || !profile->cvars || profile->nonce==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 key storage lost its actual idle profile registry");
    qa_fs_root *root=frontend_config_files_root(profile->files,product==QA_Q3_KEY_BASE);
    if (product==QA_Q3_KEY_EXPANSION && !*frontend_config_files_game_directory(profile->files))
        return fail(error,QA_ERROR_ARGUMENT,"Selected Q3 UI has no mod key write root");
    profile->busy=true;
    bool ok=qa_q3_key_save(profile->state,root,product,++profile->nonce,error);
    profile->busy=false; return ok;
}
bool frontend_keys_prepare(frontend_keys *owner,frontend_config_files *files,qa_cvars *cvars,
    const qa_q3_product_policy *policy,bool dedicated,frontend_key_profile **out,qa_error *error)
{
    const qa_product *product=files?qa_catalog_product(frontend_config_files_catalog(files),frontend_config_files_product(files)):NULL;
    if (!owner || owner->restoring || !product || product->family!=QA_GAME_Q3 || !cvars ||
        qa_cvars_dialect(cvars)!=QA_CONSOLE_Q3 || !policy || !policy->restriction_resolved || !out || *out || owner->next_id==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 key preparation requires its genuine source product, registry and frozen policy");
    frontend_key_profile *profile=calloc(1,sizeof(*profile));
    if (!profile) return fail(error,QA_ERROR_MEMORY,"Allocating selected Q3 key profile");
    profile->owner=owner; profile->files=files; profile->cvars=cvars; profile->demo=policy->filesystem_restricted;
    profile->dedicated=dedicated; profile->references=1;
    bool ok=qa_q3_key_create(cvars,dedicated,&profile->state,error) && load(profile,true,error);
    if (ok && *frontend_config_files_game_directory(files)) ok=load(profile,false,error);
    if (ok) ok=qa_q3_key_bind_storage(profile->state,stored,profile,error);
    if (!ok) { profile->files=NULL; profile_free(profile); return false; }
    profile->id=++owner->next_id; profile->next=owner->profiles; owner->profiles=profile; *out=profile; return true;
}
bool frontend_keys_carry(frontend_keys *owner,const frontend_key_profile *previous,
    frontend_config_files *files,qa_cvars *cvars,frontend_key_profile **out,qa_error *error)
{
    if (!owner || owner->restoring || !previous || previous->owner!=owner || previous->busy ||
        previous->imported_state || previous->detached || !previous->state || !previous->cvars ||
        !files || frontend_config_files_product(files)!=frontend_config_files_product(previous->files) ||
        !cvars || qa_cvars_dialect(cvars)!=QA_CONSOLE_Q3 || !out || *out || owner->next_id==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Key carry requires its genuine live previous source profile");
    frontend_key_profile *profile=calloc(1,sizeof(*profile));
    if (!profile) return fail(error,QA_ERROR_MEMORY,"Retaining actual carried source key profile");
    profile->owner=owner; profile->files=files; profile->cvars=cvars; profile->references=1;
    profile->demo=previous->demo; profile->dedicated=previous->dedicated; profile->nonce=previous->nonce;
    uint8_t bytes[34]; qa_q3_key_capture(previous->state,bytes);
    bool ok=qa_q3_key_create(cvars,profile->dedicated,&profile->state,error) &&
        qa_q3_key_restore(profile->state,(qa_bytes){bytes,sizeof(bytes)},error) &&
        qa_q3_key_bind_storage(profile->state,stored,profile,error);
    memset(bytes,0,sizeof(bytes));
    if (!ok) { profile->files=NULL; profile_free(profile); return false; }
    profile->id=++owner->next_id; profile->next=owner->profiles; owner->profiles=profile; *out=profile; return true;
}
qa_q3_key *frontend_key_profile_state(const frontend_key_profile *profile) { return profile?profile->state:NULL; }
qa_cvars *frontend_key_profile_registry(const frontend_key_profile *profile)
{ return profile && !profile->detached && !profile->imported_state && !profile->busy?profile->cvars:NULL; }
frontend_config_files *frontend_key_profile_files(const frontend_key_profile *profile) { return profile?profile->files:NULL; }
uint64_t frontend_key_profile_id(const frontend_key_profile *profile) { return profile?profile->id:0; }
const char *frontend_key_profile_game_directory(const frontend_key_profile *profile)
{ return profile?frontend_config_files_game_directory(profile->files):""; }
frontend_key_profile *frontend_keys_profile(const frontend_keys *owner,uint64_t id)
{
    for (frontend_key_profile *profile=owner?owner->profiles:NULL;profile;profile=profile->next) if (profile->id==id) return profile;
    return NULL;
}
const char *frontend_key_profile_saved_instance(const frontend_key_profile *profile)
{ return profile?profile->saved_instance:NULL; }
qa_application_console_scope frontend_key_profile_saved_scope(const frontend_key_profile *profile)
{ return profile?profile->registry:(qa_application_console_scope){0}; }
bool frontend_key_profile_bind(frontend_key_profile *profile,qa_cvars *cvars,const frontend_keys_cvar_refs *refs,qa_error *error)
{
    if (!profile || profile->busy || profile->detached || !cvars || qa_cvars_dialect(cvars)!=QA_CONSOLE_Q3)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 key binding requires its actual source registry");
    if (profile->imported_state) {
        qa_application_console_scope scope=profile->registry;
        if (!refs || !refs->resolve || !refs->qualify || !profile->saved_instance ||
            !refs->resolve(refs->context,profile->saved_instance,&scope.provider,error) || !scope.provider ||
            !refs->qualify(refs->context,&scope,cvars,error)) return false;
        profile->registry=scope;
    }
    if (!profile->state) {
        qa_q3_key *state=NULL;
        bool ok=qa_q3_key_create(cvars,profile->dedicated,&state,error) &&
            (!profile->imported_state || qa_q3_key_restore(state,(qa_bytes){profile->imported,sizeof(profile->imported)},error)) &&
            qa_q3_key_bind_storage(state,stored,profile,error);
        if (!ok) { qa_q3_key_destroy(state); return false; }
        profile->state=state;
    } else if (!qa_q3_key_rebind_cvars(profile->state,cvars,error)) return false;
    profile->cvars=cvars; profile->detached=false; profile->imported_state=false; memset(profile->imported,0,sizeof(profile->imported)); return true;
}
bool frontend_key_profile_scope(frontend_key_profile *profile,qa_application_console_scope scope,
    const qa_cvars *cvars,qa_error *error)
{
    if (!profile || profile->busy || profile->imported_state || profile->detached || profile->cvars!=cvars ||
        !scope.provider || (scope.kind!=QA_APPLICATION_CONSOLE_Q3_GAME && scope.kind!=QA_APPLICATION_CONSOLE_Q3_CGAME &&
            scope.kind!=QA_APPLICATION_CONSOLE_Q3_UI) ||
        (scope.kind==QA_APPLICATION_CONSOLE_Q3_GAME && scope.seat))
        return fail(error,QA_ERROR_ARGUMENT,"Q3 profile scope requires its actual live GAME or remote CGAME registry");
    profile->registry=scope; return true;
}
bool frontend_keys_publication_ready(frontend_keys *owner,frontend_key_profile *profile,qa_cvars *cvars,
    frontend_keys_publication *ticket,qa_error *error)
{
    if (!owner || owner->restoring || owner->publication || !ticket || ticket->owner ||
        (owner->active && owner->active->busy) ||
        (owner->active && owner->active!=profile && owner->active->references==1 && !frontend_config_files_idle(owner->active->files)) ||
        (profile && (profile->owner!=owner || profile->imported_state || !profile->state || profile->busy ||
            profile->detached || !cvars || profile->cvars!=cvars)))
        return fail(error,QA_ERROR_ARGUMENT,"Key publication requires its completely prepared source profile");
    if (profile && owner->active!=profile && !frontend_key_profile_retain(profile,error)) return false;
    *ticket=(frontend_keys_publication){owner,owner->active,profile,cvars}; owner->publication=ticket; return true;
}
bool frontend_keys_publication_current(const frontend_keys *owner,const frontend_keys_publication *ticket)
{
    return owner && ticket && !owner->restoring && owner->publication==ticket && ticket->owner==owner &&
        ticket->previous==owner->active && (!owner->active || !owner->active->busy) &&
        (!ticket->next?!ticket->cvars:
            ticket->next->owner==owner && ticket->next->references && ticket->next->state && !ticket->next->busy &&
            (ticket->next==owner->active || ticket->next->references>1) &&
            !ticket->next->imported_state && !ticket->next->detached && ticket->cvars && ticket->cvars==ticket->next->cvars);
}
void frontend_keys_publication_publish(frontend_keys_publication *ticket)
{
    frontend_keys *owner=ticket->owner;
    frontend_key_profile *previous=ticket->previous;
    owner->active=ticket->next; owner->publication=NULL;
    if (previous && previous!=ticket->next && !--previous->references) {
        frontend_key_profile **at=&owner->profiles;
        while (*at!=previous) at=&(*at)->next;
        *at=previous->next; profile_free(previous);
    }
    *ticket=(frontend_keys_publication){0};
}
void frontend_keys_publication_discard(frontend_keys_publication *ticket)
{
    if (!ticket || !ticket->owner) return;
    frontend_keys *owner=ticket->owner;
    if (ticket->next && ticket->next!=ticket->previous && !--ticket->next->references) {
        frontend_key_profile **at=&owner->profiles;
        while (*at!=ticket->next) at=&(*at)->next;
        *at=ticket->next->next; profile_free(ticket->next);
    }
    owner->publication=NULL; *ticket=(frontend_keys_publication){0};
}
bool frontend_keys_publish(frontend_keys *owner,frontend_key_profile *profile,qa_cvars *cvars,qa_error *error)
{
    frontend_keys_publication ticket={0};
    if (!frontend_keys_publication_ready(owner,profile,cvars,&ticket,error)) return false;
    frontend_keys_publication_publish(&ticket); return true;
}
bool frontend_key_profile_detach(frontend_key_profile *profile,const qa_cvars *cvars,qa_error *error)
{
    if (!profile || profile->busy || !cvars || profile->cvars!=cvars || !profile->state)
        return fail(error,QA_ERROR_ARGUMENT,"Key detachment requires the actual retiring source registry");
    if (!qa_q3_key_rebind_cvars(profile->state,NULL,error)) return false;
    profile->cvars=NULL; profile->registry=(qa_application_console_scope){0}; profile->detached=true;
    free(profile->saved_instance); profile->saved_instance=NULL; return true;
}
bool frontend_keys_authorization(const frontend_keys *owner,uint8_t out[33],bool *demo,qa_error *error)
{
    const frontend_key_profile *profile=owner?owner->active:NULL;
    return frontend_key_profile_authorization(profile,out,demo,error);
}
bool frontend_key_profile_authorization(const frontend_key_profile *profile,uint8_t out[33],bool *demo,qa_error *error)
{
    if (!profile || profile->owner->restoring || !profile->state || profile->imported_state || profile->busy || !profile->cvars || !out || !demo)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 authorization has no actual published source key profile");
    qa_q3_key_authorization(profile->state,out); *demo=profile->demo; return true;
}
bool frontend_key_profile_read(const frontend_key_profile *profile,frontend_key_profile_view *out)
{
    if (!profile || !profile->state || profile->imported_state || profile->busy || !profile->cvars || !out) return false;
    *out=(frontend_key_profile_view){profile,profile->state,profile->cvars,profile->id,
        frontend_config_files_product(profile->files),profile->demo}; return true;
}
bool frontend_keys_current(const frontend_keys *owner,frontend_key_profile_view *out)
{
    const frontend_key_profile *profile=owner?owner->active:NULL;
    if (!profile || owner->restoring || !profile->state || profile->imported_state || profile->busy || !profile->cvars || !out) return false;
    *out=(frontend_key_profile_view){profile,profile->state,profile->cvars,profile->id,
        frontend_config_files_product(profile->files),profile->demo}; return true;
}
bool frontend_keys_view_current(const frontend_keys *owner,const frontend_key_profile_view *view)
{
    frontend_key_profile_view current;
    return view && frontend_keys_current(owner,&current) && current.profile==view->profile && current.state==view->state &&
        current.cvars==view->cvars && current.identity==view->identity && current.product==view->product && current.demo==view->demo;
}
bool frontend_keys_save(frontend_keys *owner,qa_error *error)
{
    frontend_key_profile *profile=owner?owner->active:NULL;
    if (!owner || owner->restoring) return fail(error,QA_ERROR_ARGUMENT,"Q3 key save requires its published profile owner");
    if (!profile) return true;
    return frontend_key_profile_save(profile,error);
}
bool frontend_key_profile_save(frontend_key_profile *profile,qa_error *error)
{
    if (!profile || !profile->owner || profile->owner->restoring || profile->imported_state ||
        profile->detached || !profile->cvars || !profile->state)
        return fail(error,QA_ERROR_ARGUMENT,"Q3 key save requires its bound completed source profile");
    return stored(profile,QA_Q3_KEY_BASE,error) &&
        (!*frontend_config_files_game_directory(profile->files) || stored(profile,QA_Q3_KEY_EXPANSION,error));
}
bool frontend_keys_visit(const frontend_keys *owner,const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!owner || owner->restoring) return fail(error,QA_ERROR_ARGUMENT,"Key profile inventory requires its installed owner");
    for (const frontend_key_profile *profile=owner->profiles;profile;profile=profile->next)
        if (profile->busy || !frontend_config_files_visit(profile->files,visitor,error)) return false;
    return true;
}
static bool blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t count=bytes->size;
    if (!qa_source_save_count(io,&count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,count);
    if (count>io->input.size-io->offset) {
        io->failed=true;
        return fail(io->error,QA_ERROR_FORMAT,"Key profile blob exceeds its admitted section");
    }
    *bytes=(qa_bytes){io->input.data+io->offset,count}; io->offset+=count; return true;
}
static bool header(qa_source_save_io *io,uint64_t *next,uint64_t *active,size_t *count)
{
    uint8_t magic[4]={'Q','F','K','P'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFKP",4) && qa_source_save_u64(io,next) && qa_source_save_u64(io,active) && *active<=*next &&
        qa_source_save_count(io,count,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX);
}
static bool source_name(qa_source_save_io *io,char **name)
{
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE && *name?strlen(*name):0;
    size_t maximum=io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX-1;
    if (!qa_source_save_count(io,&length,maximum) || !length || length==SIZE_MAX) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,*name,length);
    char *copy=malloc(length+1);
    if (!copy) return fail(io->error,QA_ERROR_MEMORY,"Retaining actual key source instance identity");
    if (!qa_source_save_bytes(io,copy,length) || memchr(copy,0,length)) { free(copy); return false; }
    copy[length]=0; *name=copy; return true;
}
static bool row(qa_source_save_io *io,const frontend_keys_cvar_refs *refs,frontend_key_profile *profile,qa_bytes *files)
{
    uint32_t kind=profile->registry.kind;
    bool ok=qa_source_save_u64(io,&profile->id) && profile->id && qa_source_save_bool(io,&profile->detached);
    if (ok && !profile->detached) {
        if (io->direction==QA_SOURCE_SAVE_WRITE) profile->saved_instance=(char *)refs->instance(refs->context,profile->registry.provider);
        ok=source_name(io,&profile->saved_instance) &&
        qa_source_save_u32(io,&kind) &&
        (kind==QA_APPLICATION_CONSOLE_Q3_GAME || kind==QA_APPLICATION_CONSOLE_Q3_CGAME || kind==QA_APPLICATION_CONSOLE_Q3_UI) &&
        qa_source_save_u32(io,&profile->registry.seat) &&
        (kind!=QA_APPLICATION_CONSOLE_Q3_GAME || !profile->registry.seat);
    }
    if (ok) profile->registry=profile->detached?(qa_application_console_scope){0}:
        (qa_application_console_scope){profile->registry.provider,(qa_application_console_kind)kind,profile->registry.seat};
    return ok && qa_source_save_u64(io,&profile->nonce) && qa_source_save_bool(io,&profile->demo) &&
        qa_source_save_bool(io,&profile->dedicated) &&
        qa_source_save_bytes(io,profile->imported,sizeof(profile->imported)) &&
        !profile->imported[33] && blob(io,files) && files->size;
}
bool frontend_keys_checkpoint(const frontend_keys *owner,const qa_application_content_graph *graph,
    const frontend_keys_cvar_refs *refs,qa_buffer *out,qa_error *error)
{
    if (!owner || owner->restoring || owner->publication || !graph || !refs || !refs->encode || !refs->instance || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Key capture requires its actual profile and source registry inventory");
    size_t count=0; uint64_t next=owner->next_id,active=owner->active?owner->active->id:0;
    for (const frontend_key_profile *profile=owner->profiles;profile;profile=profile->next) ++count;
    qa_source_save_io io={0}; bool ok=qa_source_save_writer(&io,NULL,error) && header(&io,&next,&active,&count);
    for (const frontend_key_profile *profile=owner->profiles;ok && profile;profile=profile->next) {
        frontend_key_profile saved=*profile; qa_buffer files={0};
        ok=!profile->busy && profile->state && !profile->imported_state &&
            (profile->detached?!profile->cvars && !saved.registry.provider:
                profile->cvars && refs->encode(refs->context,profile->cvars,&saved.registry,error) && saved.registry.provider) &&
            frontend_config_files_checkpoint(profile->files,graph,&files,error);
        if (ok) {
            qa_q3_key_capture(profile->state,saved.imported); qa_bytes bytes={files.data,files.size}; ok=row(&io,refs,&saved,&bytes);
        }
        memset(saved.imported,0,sizeof(saved.imported)); qa_buffer_free(&files);
    }
    if (ok) ok=qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
bool frontend_keys_restore(qa_application_content_graph *graph,const qa_q3_product_policy *policy,
    const frontend_keys_cvar_refs *refs,qa_bytes bytes,frontend_keys **out,qa_error *error)
{
    if (!graph || !out || *out) return fail(error,QA_ERROR_ARGUMENT,"Key import requires its empty admitted profile owner");
    frontend_keys *owner=frontend_keys_create(error); if (!owner) return false;
    owner->restoring=true; uint64_t active=0; size_t count=0; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && header(&io,&owner->next_id,&active,&count);
    frontend_key_profile **tail=&owner->profiles;
    for (size_t i=0;ok && i<count;++i) {
        frontend_key_profile *profile=calloc(1,sizeof(*profile)); qa_bytes files={0};
        if (!profile) { ok=fail(error,QA_ERROR_MEMORY,"Importing Q3 profile roster"); break; }
        profile->owner=owner; profile->references=1; profile->imported_state=true;
        profile->registry.kind=QA_APPLICATION_CONSOLE_Q3_GAME;
        ok=row(&io,refs,profile,&files) && profile->id<=owner->next_id && !frontend_keys_profile(owner,profile->id);
        if (ok) ok=frontend_config_files_restore(graph,files,&profile->files,error);
        const qa_product *product=ok?qa_catalog_product(frontend_config_files_catalog(profile->files),frontend_config_files_product(profile->files)):NULL;
        if (ok) ok=product && product->family==QA_GAME_Q3 && policy && policy->restriction_resolved &&
            profile->demo==policy->filesystem_restricted;
        if (ok) ok=qa_q3_key_create_detached(profile->dedicated,&profile->state,error) &&
            qa_q3_key_restore(profile->state,(qa_bytes){profile->imported,sizeof(profile->imported)},error) &&
            qa_q3_key_bind_storage(profile->state,stored,profile,error);
        if (ok && profile->detached) { profile->imported_state=false; memset(profile->imported,0,sizeof(profile->imported)); }
        if (!ok) { profile_free(profile); break; }
        *tail=profile; tail=&profile->next;
        if (profile->id==active) { owner->active=profile; ++profile->references; }
    }
    if (ok) ok=(!active || owner->active) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) *out=owner;
    else { frontend_keys_destroy(owner,NULL); if (!error || error->code==QA_OK) fail(error,QA_ERROR_FORMAT,"Invalid genuine Q3 key profile continuation"); }
    return ok;
}
bool frontend_keys_finish_restore(frontend_keys *owner,qa_error *error)
{
    if (!owner || !owner->restoring) return fail(error,QA_ERROR_ARGUMENT,"Q3 key profiles have no pending pure admission");
    for (frontend_key_profile *profile=owner->profiles;profile;profile=profile->next)
        if (!profile->state || profile->imported_state || profile->busy || profile->references==1 ||
            (profile->detached?profile->cvars!=NULL:profile->cvars==NULL))
            return fail(error,QA_ERROR_FORMAT,"Restored key profile lacks its actual source or published alias");
    frontend_key_profile *profile=owner->profiles;
    while (profile) { frontend_key_profile *next=profile->next; if (!frontend_key_profile_release(profile,error)) return false; profile=next; }
    owner->restoring=false; return true;
}
