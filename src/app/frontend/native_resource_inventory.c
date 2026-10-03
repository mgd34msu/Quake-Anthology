#include "native_resource_inventory.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
typedef struct native_resource_row {
    struct native_resource_row *next;
    char *instance;
    uint64_t source,ordinal;
    qa_native_process_resources *resources;
    qa_buffer continuation;
    bool complete;
} native_resource_row;
struct qa_native_resource_inventory {
    native_resource_row *first,*last;
    uint64_t count;
    bool retiring;
};
static bool fail(qa_error *error,qa_status code,const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }
bool qa_native_resource_inventory_release(qa_native_resource_inventory **slot,qa_error *error)
{
    if (!slot) return fail(error,QA_ERROR_ARGUMENT,"Native inventory release needs its owner slot");
    qa_native_resource_inventory *owner=*slot;
    if (!owner) return true;
    owner->retiring=true;
    while (owner->first) {
        native_resource_row *row=owner->first;
        if (!qa_native_process_resources_release(&row->resources,error)) return false;
        owner->first=row->next;
        qa_buffer_free(&row->continuation); free(row->instance); free(row);
    }
    free(owner); *slot=NULL; return true;
}
typedef struct native_resource_recipe {
    uint64_t ordinal,source;
    qa_bytes instance,continuation;
} native_resource_recipe;
static bool recipe_bytes(qa_source_save_io *io,qa_bytes *bytes,size_t maximum)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,maximum)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (io->offset>io->input.size || size>io->input.size-io->offset) return false;
        *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
    }
    return qa_source_save_bytes(io,(void *)bytes->data,size);
}
static bool recipe_fields(qa_source_save_io *io,native_resource_recipe *recipe)
{
    uint8_t magic[4]={'Q','F','N','R'};
    return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFNR",4) &&
        qa_source_save_u64(io,&recipe->ordinal) && recipe->ordinal &&
        qa_source_save_u64(io,&recipe->source) && recipe->source &&
        recipe_bytes(io,&recipe->instance,QA_SAVE_NAME_LIMIT) && recipe->instance.size &&
        !memchr(recipe->instance.data,0,recipe->instance.size) &&
        recipe_bytes(io,&recipe->continuation,SIZE_MAX) && recipe->continuation.size;
}
static bool encode(native_resource_row *row,qa_buffer *out,qa_error *error)
{
    native_resource_recipe recipe={.ordinal=row->ordinal,.source=row->source,
        .instance={(const uint8_t *)row->instance,strlen(row->instance)},
        .continuation={row->continuation.data,row->continuation.size}};
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,NULL,error) && recipe_fields(&io,&recipe) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); return ok;
}
static bool capture(void *context,const char *instance,uint64_t source,
    const qa_native_process_resources *resources,qa_buffer *out,qa_error *error)
{
    frontend_native_resource_context *cut=context;
    if (!cut || cut->image || !instance || !instance[0] || strlen(instance)>QA_SAVE_NAME_LIMIT ||
        !source || !resources || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Native capture requires its actual provider and empty capsule");
    if (!cut->captured) {
        cut->captured=calloc(1,sizeof(*cut->captured));
        if (!cut->captured) return fail(error,QA_ERROR_MEMORY,"Allocating retained native capability graph");
    }
    qa_native_resource_inventory *owner=cut->captured;
    if (owner->retiring || owner->count==UINT64_MAX)
        return fail(error,QA_ERROR_ARGUMENT,"Native capability graph is retiring or exhausted");
    for (native_resource_row *row=owner->first;row;row=row->next)
        if (row->source==source && !strcmp(row->instance,instance))
            return fail(error,QA_ERROR_FORMAT,"Native provider capture appears twice in its graph");
    native_resource_row *row=calloc(1,sizeof(*row));
    if (!row) return fail(error,QA_ERROR_MEMORY,"Retaining native provider capability row");
    row->instance=malloc(strlen(instance)+1);
    if (!row->instance) { free(row); return fail(error,QA_ERROR_MEMORY,"Retaining native provider identity"); }
    strcpy(row->instance,instance); row->source=source; row->ordinal=++owner->count;
    if (owner->last) owner->last->next=row; else owner->first=row;
    owner->last=row;
    if (!qa_native_process_resources_capture(resources,&row->resources,&row->continuation,error)) return false;
    row->complete=true; return encode(row,out,error);
}
static bool resolve(void *context,const char *instance,uint64_t source,qa_bytes bytes,
    const qa_native_process_resources **out,qa_bytes *lower_recipe,qa_error *error)
{
    frontend_native_resource_context *cut=context;
    const qa_native_resource_inventory *owner=cut && cut->image?qa_save_image_native_read(cut->image):NULL;
    if (!cut || !cut->image || (owner && owner->retiring) || !instance || !source ||
        !out || !lower_recipe || (bytes.size && !bytes.data))
        return fail(error,QA_ERROR_ARGUMENT,"Native recipe resolution lacks its actual saved provider");
    native_resource_recipe recipe={0}; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,NULL,bytes,error) && recipe_fields(&io,&recipe) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (!ok || recipe.source!=source || recipe.instance.size!=strlen(instance) ||
        memcmp(recipe.instance.data,instance,recipe.instance.size))
        return fail(error,QA_ERROR_FORMAT,"Saved native capsule differs from its named provider");
    /* A file image carries the named recipe, not native pointers. Its caller
     * reconstructs those objects against the actual prepared content authority. */
    if (!owner) { *out=NULL; *lower_recipe=recipe.continuation; return true; }
    for (native_resource_row *row=owner->first;row;row=row->next) {
        if (row->source!=source || strcmp(row->instance,instance)) continue;
        if (!row->complete || !row->resources || recipe.ordinal!=row->ordinal ||
            recipe.continuation.size!=row->continuation.size ||
            memcmp(recipe.continuation.data,row->continuation.data,row->continuation.size))
            return fail(error,QA_ERROR_FORMAT,"Saved native capsule differs from its captured provider row");
        if (!qa_native_process_resources_validate(row->resources,
            (qa_bytes){row->continuation.data,row->continuation.size},error)) return false;
        *out=row->resources; *lower_recipe=recipe.continuation; return true;
    }
    return fail(error,QA_ERROR_FORMAT,"Saved native provider leaves its retained capability graph");
}

static bool attach(void *context,qa_save_image *image,qa_error *error)
{
    frontend_native_resource_context *cut=context;
    if (!cut || cut->image || !image) return fail(error,QA_ERROR_ARGUMENT,"Native graph attachment needs its actual capture image");
    if (!cut->captured) return true;
    for (native_resource_row *row=cut->captured->first;row;row=row->next)
        if (!row->complete || !row->resources) return fail(error,QA_ERROR_FORMAT,"Native graph contains an incomplete captured provider");
    if (!qa_save_image_native_attach(image,cut->captured,qa_native_resource_inventory_release,error)) return false;
    cut->captured=NULL; return true;
}
qa_application_native_resource_refs frontend_native_resource_refs(frontend_native_resource_context *context)
{ return (qa_application_native_resource_refs){.context=context,.capture=capture,.resolve=resolve,.attach=attach}; }
