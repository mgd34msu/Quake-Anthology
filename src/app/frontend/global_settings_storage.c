#include "global_settings_storage.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>

struct frontend_global_settings_storage {
    qa_resource_pool *resources;
    qa_vfs *files;
    qa_mount_id user,devices;
};
static bool fail(qa_error *error,qa_status code,const char *message)
{ qa_error_set(error,code,0,"%s",message); return false; }
static bool stores_valid(const qa_vfs *files,qa_mount_id user,qa_mount_id devices)
{
    /* This private view issues the user mount first and its console child
     * second. Preserve those roles independently of mapped root identities. */
    if (!files || user!=1 || devices!=2 || qa_vfs_mount_count(files)!=2) return false;
    bool has_user=false,has_devices=false;
    for (size_t i=0;i<2;++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(files,i,&mount) || !mount.writable || mount.is_archive ||
            mount.comparison!=QA_ARCHIVE_CASE_INSENSITIVE || !qa_vfs_mount_root(files,mount.id)) return false;
        if (mount.id==user) has_user=true;
        else if (mount.id==devices) has_devices=true;
        else return false;
    }
    return has_user && has_devices && !qa_fs_root_same_object(qa_vfs_mount_root(files,user),
        qa_vfs_mount_root(files,devices));
}
bool frontend_global_settings_storage_idle(const frontend_global_settings_storage *owner)
{ return owner && owner->resources && qa_vfs_resources(owner->files)==owner->resources &&
    stores_valid(owner->files,owner->user,owner->devices); }
qa_settings_store frontend_global_settings_storage_user_store(const frontend_global_settings_storage *owner)
{
    return frontend_global_settings_storage_idle(owner)?
        (qa_settings_store){owner->files,owner->user}:(qa_settings_store){0};
}
qa_settings_store frontend_global_settings_storage_device_store(const frontend_global_settings_storage *owner)
{
    return frontend_global_settings_storage_idle(owner)?
        (qa_settings_store){owner->files,owner->devices}:(qa_settings_store){0};
}
bool frontend_global_settings_storage_create(const char *user_root,
    frontend_global_settings_storage **out,qa_error *error)
{
    if (!user_root || !*user_root || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Global settings require the configured user directory and an empty owner");
    frontend_global_settings_storage *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Retaining global settings directories");
    owner->resources=qa_resource_pool_create(error);
    owner->files=owner->resources?qa_vfs_create(owner->resources,error):NULL;
    char *console_path=NULL;
    bool okay=owner->files && qa_fs_path_create_directory(user_root,error) &&
        qa_vfs_mount_directory(owner->files,user_root,QA_ARCHIVE_CASE_INSENSITIVE,true,&owner->user,error);
    qa_fs_root *root=okay?qa_vfs_mount_root(owner->files,owner->user):NULL;
    if (okay) okay=qa_fs_root_create_directory(root,"console",error) &&
        qa_fs_root_join(root,"console",&console_path,error) &&
        qa_vfs_mount_directory(owner->files,console_path,QA_ARCHIVE_CASE_INSENSITIVE,true,&owner->devices,error) &&
        frontend_global_settings_storage_idle(owner);
    free(console_path);
    if (!okay) { qa_vfs_destroy(owner->files); qa_resource_pool_destroy(owner->resources); free(owner); return false; }
    *out=owner; return true;
}
bool frontend_global_settings_storage_visit(const frontend_global_settings_storage *owner,
    const qa_application_content_visitor *visitor,qa_error *error)
{
    if (!frontend_global_settings_storage_idle(owner) || !visitor || !visitor->view)
        return fail(error,QA_ERROR_ARGUMENT,"Global settings inventory lost its real retained directory view");
    return (!visitor->pool || visitor->pool(visitor->context,qa_vfs_resources(owner->files),error)) &&
        visitor->view(visitor->context,owner->files,error);
}
typedef struct storage_state {
    uint64_t pool,view,user,devices;
    qa_fs_object_reference user_root,device_root;
} storage_state;
static bool root_fields(qa_source_save_io *io,qa_fs_object_reference *root)
{
    if (!qa_source_save_u32(io,&root->platform) || (root->platform!=1 && root->platform!=2)) return false;
    for (size_t i=0;i<3;++i) if (!qa_source_save_u64(io,root->words+i)) return false;
    return true;
}
static bool root_matches(const qa_vfs *files,qa_mount_id mount,const qa_fs_object_reference *saved)
{
    const qa_fs_object_reference *roots=NULL; size_t count=0;
    if (!qa_vfs_mount_root_references(files,mount,&roots,&count)) return false;
    for (size_t i=0;i<count;++i)
        if (roots[i].platform==saved->platform && !memcmp(roots[i].words,saved->words,sizeof(saved->words))) return true;
    return false;
}
static bool fields(qa_source_save_io *io,storage_state *state)
{
    uint8_t magic[4]={'Q','F','G','S'}; return qa_source_save_bytes(io,magic,4) && !memcmp(magic,"QFGS",4) &&
        qa_source_save_u64(io,&state->pool) &&
        qa_source_save_u64(io,&state->view) &&
        qa_source_save_u64(io,&state->user) && qa_source_save_u64(io,&state->devices) &&
        root_fields(io,&state->user_root) && root_fields(io,&state->device_root) &&
        state->pool && state->view && state->user && state->devices && state->user!=state->devices;
}
bool frontend_global_settings_storage_checkpoint(const frontend_global_settings_storage *owner,
    const qa_application_content_graph *graph,qa_buffer *out,qa_error *error)
{
    if (!frontend_global_settings_storage_idle(owner) || !graph || !out || out->data || out->size)
        return fail(error,QA_ERROR_ARGUMENT,"Global settings capture requires its actual content graph");
    storage_state state={.pool=qa_application_content_pool_id(graph,owner->resources),
        .view=qa_application_content_view_id(graph,owner->files),.user=owner->user,.devices=owner->devices};
    if (!qa_fs_root_reference_read(qa_vfs_mount_root(owner->files,owner->user),&state.user_root) ||
        !qa_fs_root_reference_read(qa_vfs_mount_root(owner->files,owner->devices),&state.device_root))
        return fail(error,QA_ERROR_FORMAT,"Global settings capture lost its actual root capabilities");
    qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,NULL,error) && fields(&io,&state) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!okay && (!error || error->code==QA_OK)) fail(error,QA_ERROR_FORMAT,"Global settings view is absent from its captured graph");
    return okay;
}
bool frontend_global_settings_storage_restore(qa_application_content_graph *graph,qa_bytes bytes,
    frontend_global_settings_storage **out,qa_error *error)
{
    if (!graph || !out || *out)
        return fail(error,QA_ERROR_ARGUMENT,"Global settings import requires an actual decoded graph and an empty owner");
    storage_state state={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,NULL,bytes,error) && fields(&io,&state) && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    qa_vfs *files=okay?qa_application_content_view(graph,state.view):NULL;
    qa_resource_pool *pool=okay?qa_application_content_pool(graph,state.pool):NULL;
    if (okay) okay=pool && qa_vfs_resources(files)==pool && stores_valid(files,state.user,state.devices) &&
        root_matches(files,state.user,&state.user_root) && root_matches(files,state.devices,&state.device_root);
    if (!okay) return fail(error,QA_ERROR_FORMAT,"Invalid global settings directory continuation");
    frontend_global_settings_storage *owner=calloc(1,sizeof(*owner));
    if (!owner) return fail(error,QA_ERROR_MEMORY,"Importing global settings directories");
    owner->user=state.user; owner->devices=state.devices;
    okay=qa_application_content_claim_pool(graph,state.pool,&owner->resources,error) &&
        qa_application_content_claim_view(graph,state.view,&owner->files,error);
    if (!okay) { qa_vfs_destroy(owner->files); qa_resource_pool_destroy(owner->resources); free(owner); return false; }
    *out=owner; return true;
}
bool frontend_global_settings_storage_destroy(frontend_global_settings_storage **out,qa_error *error)
{
    if (!out) return fail(error,QA_ERROR_ARGUMENT,"Global settings retirement requires its owning slot");
    if (!*out) return true;
    qa_vfs_destroy((*out)->files); qa_resource_pool_destroy((*out)->resources);
    free(*out); *out=NULL; return true;
}
