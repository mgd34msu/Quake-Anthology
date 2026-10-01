#include "client_registry.h"
#include "qa/console_cvar_observer.h"
#include "qa/cvars_save.h"
#include "save_private.h"

typedef struct client_registry_saved {
    char *instance;
    qa_sha256_digest identity;
    uint64_t catalog,view;
    qa_product_id product;
    uint32_t seat,dialect;
    qa_buffer state;
    frontend_client_registry *owner;
} client_registry_saved;
struct frontend_client_registry_import {
    qa_application_content_graph *graph;
    client_registry_saved *rows;
    size_t count;
};

struct frontend_client_registry {
    frontend_client_registry *next;
    qa_frontend *frontend;
    qa_launch_instance_lease *metadata;
    frontend_client_registry_context callback;
    qa_cvars *cvars;
    uint32_t launch_seat;
    size_t references;
};
static bool linked(const frontend_client_registry *owner)
{
    if (!owner || !owner->frontend) return false;
    for (const frontend_client_registry *row=owner->frontend->client_registries;row;row=row->next)
        if (row==owner) return true;
    return false;
}
bool frontend_client_registry_matches(const frontend_client_registry *owner,
    const qa_launch_instance *source,uint32_t seat)
{
    const qa_launch_instance *retained=owner?qa_launch_instance_lease_view(owner->metadata):NULL;
    return linked(owner) && owner->cvars && source && source->storage && retained &&
        retained->storage==source->storage && owner->launch_seat==seat;
}
bool frontend_client_registry_create(qa_frontend *f,const qa_launch_instance *source,uint32_t seat,
    qa_cvars **owned,const frontend_client_registry_context *callback,
    frontend_client_registry **out,qa_error *error)
{
    if (!f || !source || !source->storage || !source->selection.instance || !*source->selection.instance ||
        !source->content || !qa_launch_instance_catalog(source) || !owned || !*owned || !out || *out ||
        !callback || !callback->context || !callback->retain || !callback->release || f->capture ||
        !qa_cvars_observer_idle(*owned))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry requires its actual prepared heap and callback lease");
    for (frontend_client_registry *row=f->client_registries;row;row=row->next)
        if (row->cvars==*owned || (qa_launch_instance_lease_view(row->metadata)->storage==source->storage && row->launch_seat==seat))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Client constructor already owns that registry or source seat");
    client_registry_saved *saved=NULL;
    if (f->client_registry_import) {
        frontend_client_registry_import *import=f->client_registry_import;
        for (size_t i=0;i<import->count;++i)
            if (import->rows[i].seat==seat && !strcmp(import->rows[i].instance,source->selection.instance)) saved=import->rows+i;
        if (!saved || saved->owner || saved->product!=source->selection.product ||
            qa_application_content_catalog(import->graph,saved->catalog)!=qa_launch_instance_catalog(source) ||
            qa_application_content_view(import->graph,saved->view)!=source->content ||
            memcmp(saved->identity.bytes,source->identity.bytes,sizeof(source->identity.bytes)) ||
            saved->dialect!=(uint32_t)qa_cvars_dialect(*owned))
            return frontend_fail(error,QA_ERROR_FORMAT,"Restored client registry differs from its actual source constructor");
        qa_cvars_restore *ticket=NULL;
        if (!qa_cvars_save_prepare(*owned,(qa_bytes){saved->state.data,saved->state.size},&ticket,error)) return false;
        if (!qa_cvars_save_commit(ticket,error)) { qa_cvars_save_abort(ticket); return false; }
    }
    frontend_client_registry *owner=calloc(1,sizeof(*owner));
    if (!owner) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating shared client registry owner");
    if (!qa_launch_instance_retain_metadata(source,&owner->metadata,error)) { free(owner); return false; }
    if (!callback->retain(callback->context,error)) {
        qa_launch_instance_lease_release(owner->metadata); free(owner); return false;
    }
    owner->frontend=f; owner->launch_seat=seat; owner->references=1;
    owner->callback=*callback; owner->cvars=*owned; *owned=NULL;
    frontend_client_registry **link=&f->client_registries;
    if (saved) {
        frontend_client_registry_import *import=f->client_registry_import;
        size_t ordinal=(size_t)(saved-import->rows);
        while (*link) {
            size_t previous=0;
            while (previous<import->count && import->rows[previous].owner!=*link) ++previous;
            if (previous>ordinal) break;
            link=&(*link)->next;
        }
        saved->owner=owner;
    }
    owner->next=*link; *link=owner; *out=owner; return true;
}
bool frontend_client_registry_retain(frontend_client_registry *owner,frontend_client_registry **out,qa_error *error)
{
    if (!out || *out || !linked(owner) || !owner->cvars || !owner->references || owner->references==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry retain requires its live actual owner");
    ++owner->references; *out=owner; return true;
}
bool frontend_client_registry_release(frontend_client_registry **slot,qa_error *error)
{
    if (!slot) return frontend_fail(error,QA_ERROR_ARGUMENT,"Missing client registry reference");
    frontend_client_registry *owner=*slot;
    if (!owner) return true;
    if (!linked(owner) || !owner->references || owner->frontend->capture)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry release requires its retained inactive owner");
    if (owner->references>1) { --owner->references; *slot=NULL; return true; }
    if (owner->cvars) {
        if (!qa_cvars_observer_idle(owner->cvars))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry still has an active publication callback");
        qa_cvars_destroy(owner->cvars); owner->cvars=NULL;
    }
    if (!owner->callback.release(owner->callback.context,error)) return false;
    frontend_client_registry **link=&owner->frontend->client_registries;
    while (*link!=owner) link=&(*link)->next;
    *link=owner->next; *slot=NULL;
    frontend_client_registry_import *import=owner->frontend->client_registry_import;
    for (size_t i=0;import && i<import->count;++i)
        if (import->rows[i].owner==owner) import->rows[i].owner=NULL;
    qa_launch_instance_lease_release(owner->metadata); free(owner); return true;
}
bool frontend_client_registry_release_ready(const frontend_client_registry *owner,qa_error *error)
{
    return !owner || (linked(owner) && owner->references==1 && !owner->frontend->capture &&
        (!owner->cvars || qa_cvars_observer_idle(owner->cvars))) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Prepared registry still has an actual client lease or publication callback");
}
bool frontend_client_registry_acquire(qa_frontend *f,const qa_launch_instance *source,uint32_t seat,
    frontend_client_registry **out,qa_error *error)
{
    if (!f || !out || *out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Invalid shared client registry acquisition");
    for (frontend_client_registry *row=f->client_registries;row;row=row->next)
        if (frontend_client_registry_matches(row,source,seat))
            return frontend_client_registry_retain(row,out,error);
    return frontend_fail(error,QA_ERROR_ARGUMENT,"No actual prepared client registry owns this source seat");
}
qa_cvars *frontend_client_registry_cvars(const frontend_client_registry *owner)
{ return linked(owner)?owner->cvars:NULL; }
const frontend_client_registry *frontend_client_registry_lookup(const qa_frontend *f,const qa_cvars *cvars)
{
    if (!cvars) return NULL;
    for (const frontend_client_registry *row=f?f->client_registries:NULL;row;row=row->next)
        if (row->cvars==cvars) return row;
    return NULL;
}
bool frontend_client_registry_source(const frontend_client_registry *owner,
    const qa_launch_instance **source,uint32_t *seat)
{
    if (!source || !seat || !linked(owner) || !owner->cvars) return false;
    *source=qa_launch_instance_lease_view(owner->metadata); *seat=owner->launch_seat; return *source!=NULL;
}
size_t frontend_client_registry_count(const qa_frontend *f)
{
    size_t count=0;
    for (const frontend_client_registry *row=f?f->client_registries:NULL;row;row=row->next) ++count;
    return count;
}
bool frontend_client_registry_read(const qa_frontend *f,size_t ordinal,
    frontend_client_registry_view *out,qa_error *error)
{
    const frontend_client_registry *owner=f?f->client_registries:NULL;
    while (owner && ordinal--) owner=owner->next;
    if (!f || !out || !owner || !owner->cvars || f->stepping ||
        !qa_cvars_observer_idle(owner->cvars))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry inventory requires an inactive physical owner");
    *out=(frontend_client_registry_view){owner,qa_launch_instance_lease_view(owner->metadata),
        owner->cvars,owner->launch_seat,owner->references}; return true;
}
bool frontend_client_registries_retired(const qa_frontend *f,qa_error *error)
{
    return !f || !f->client_registries ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Frontend still retains actual client registry leases");
}
static void import_free(frontend_client_registry_import *import)
{
    if (!import) return;
    for (size_t i=0;import->rows && i<import->count;++i) { free(import->rows[i].instance); qa_buffer_free(&import->rows[i].state); }
    free(import->rows); free(import);
}
static bool saved_fields(qa_source_save_io *io,client_registry_saved *row)
{
    if (!frontend_save_text(io,&row->instance) || !row->instance || !*row->instance ||
        !qa_source_save_bytes(io,row->identity.bytes,sizeof(row->identity.bytes)) ||
        !qa_source_save_u64(io,&row->catalog) || !row->catalog ||
        !qa_source_save_u64(io,&row->view) || !row->view ||
        !qa_source_save_u32(io,&row->product) || !row->product ||
        !qa_source_save_u32(io,&row->seat) || !qa_source_save_u32(io,&row->dialect) || row->dialect>QA_CONSOLE_Q3)
        return false;
    size_t size=row->state.size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX) || !size)
        return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        row->state.data=malloc(size);
        if (!row->state.data) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining canonical client registry bytes");
        row->state.size=size;
    }
    return qa_source_save_bytes(io,row->state.data,size);
}
static bool saved_header(qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','F','C','R'}; uint32_t version=1;
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFCR",4) &&
        qa_source_save_u32(io,&version) && version==1 &&
        qa_source_save_count(io,count,SIZE_MAX/sizeof(client_registry_saved));
}
bool frontend_client_registries_visit(const qa_frontend *f,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!f || !visitor || !visitor->catalog || !visitor->view || !visitor->pool)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry content requires its actual visitor");
    for (const frontend_client_registry *row=f->client_registries;row;row=row->next) {
        const qa_launch_instance *source=qa_launch_instance_lease_view(row->metadata);
        qa_catalog *catalog=source?qa_launch_instance_catalog(source):NULL;
        qa_resource_pool *pool=source && source->content?qa_vfs_resources(source->content):NULL;
        if (!catalog || !pool || !visitor->catalog(visitor->context,catalog,error) ||
            !visitor->view(visitor->context,source->content,error) || !visitor->pool(visitor->context,pool,error)) return false;
    }
    return true;
}
bool frontend_client_registries_checkpoint(const qa_frontend *f,
    const qa_application_content_graph *graph,qa_buffer *out,qa_error *error)
{
    if (!f || !graph || !out || out->data || out->size || f->stepping || f->client_registry_import)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry capture requires its complete physical roster");
    qa_source_save_io io={0}; size_t count=frontend_client_registry_count(f);
    bool ok=qa_source_save_writer(&io,NULL,error) && saved_header(&io,&count);
    for (const frontend_client_registry *row=f->client_registries;ok && row;row=row->next) {
        const qa_launch_instance *source=qa_launch_instance_lease_view(row->metadata);
        if (!source || !row->cvars || !row->references || !qa_cvars_observer_idle(row->cvars)) { ok=false; break; }
        client_registry_saved saved={.instance=(char *)source->selection.instance,.identity=source->identity,
            .catalog=qa_application_content_catalog_id(graph,qa_launch_instance_catalog(source)),
            .view=qa_application_content_view_id(graph,source->content),.product=source->selection.product,
            .seat=row->launch_seat,.dialect=(uint32_t)qa_cvars_dialect(row->cvars)};
        ok=qa_cvars_save_capture(row->cvars,&saved.state,error) && saved_fields(&io,&saved);
        qa_buffer_free(&saved.state);
    }
    ok=ok && qa_source_save_finish(&io,out); qa_source_save_dispose(&io);
    if (!ok && error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Client registry leaves its retained source content graph");
    return ok;
}
bool frontend_client_registries_prepare_restore(qa_frontend *f,
    qa_application_content_graph *graph,qa_bytes bytes,qa_error *error)
{
    if (!f || !graph || f->client_registries || f->client_registry_import || f->stepping || f->capture)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry prefix requires an empty isolated owner");
    frontend_client_registry_import *import=calloc(1,sizeof(*import)); qa_source_save_io io={0};
    if (!import) return frontend_fail(error,QA_ERROR_MEMORY,"Allocating client registry import roster");
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && saved_header(&io,&import->count);
    if (ok && import->count) {
        import->rows=calloc(import->count,sizeof(*import->rows));
        if (!import->rows) ok=frontend_fail(error,QA_ERROR_MEMORY,"Allocating saved client registry rows");
    }
    for (size_t i=0;ok && i<import->count;++i) ok=saved_fields(&io,import->rows+i);
    ok=ok && qa_source_save_finish(&io,NULL); qa_source_save_dispose(&io);
    for (size_t i=0;ok && i<import->count;++i) {
        client_registry_saved *row=import->rows+i;
        qa_catalog *catalog=qa_application_content_catalog(graph,row->catalog);
        ok=catalog && qa_catalog_product(catalog,row->product) && qa_application_content_view(graph,row->view);
        for (size_t j=0;ok && j<i;++j)
            if (row->seat==import->rows[j].seat && !strcmp(row->instance,import->rows[j].instance)) ok=false;
    }
    if (!ok) {
        import_free(import);
        if (error && error->code==QA_OK) frontend_fail(error,QA_ERROR_FORMAT,"Saved client registry prefix has invalid source authority");
        return false;
    }
    import->graph=graph; f->client_registry_import=import; return true;
}
bool frontend_client_registries_finish_restore(qa_frontend *f,qa_error *error)
{
    if (!f || !f->client_registry_import || f->capture || f->stepping)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry finish requires its real isolated prefix");
    frontend_client_registry_import *import=f->client_registry_import;
    if (frontend_client_registry_count(f)!=import->count)
        return frontend_fail(error,QA_ERROR_FORMAT,"Restored client registry physical roster is incomplete");
    for (size_t i=0;i<import->count;++i)
        if (!linked(import->rows[i].owner) || !import->rows[i].owner->cvars)
            return frontend_fail(error,QA_ERROR_FORMAT,"Restored client registry has no genuine constructor owner");
    import_free(import); f->client_registry_import=NULL; return true;
}
bool frontend_client_registries_discard_restore(qa_frontend *f,qa_error *error)
{
    if (!f || f->capture || f->stepping || f->client_registries)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry prefix discard requires returned client owners");
    import_free(f->client_registry_import); f->client_registry_import=NULL; return true;
}
bool frontend_client_registries_rebind_ready(const qa_frontend *owned,const qa_frontend *destination,qa_error *error)
{
    if (!owned || !destination || owned->client_registry_import || destination->client_registry_import ||
        (owned!=destination && destination->client_registries))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry rebind requires its actual owner and empty destination roster");
    for (const frontend_client_registry *row=owned->client_registries;row;row=row->next)
        if (row->frontend!=owned || !row->references || !row->cvars || !qa_cvars_observer_idle(row->cvars))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry rebind still has an active or retired physical heap");
    return true;
}
void frontend_client_registries_rebind(qa_frontend *owned,qa_frontend *destination)
{
    if (owned==destination) return;
    destination->client_registries=owned->client_registries; owned->client_registries=NULL;
    for (frontend_client_registry *row=destination->client_registries;row;row=row->next) row->frontend=destination;
}
