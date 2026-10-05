#include "global_settings_storage.h"
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
bool frontend_global_settings_storage_destroy(frontend_global_settings_storage **out,qa_error *error)
{
    if (!out) return fail(error,QA_ERROR_ARGUMENT,"Global settings retirement requires its owning slot");
    if (!*out) return true;
    qa_vfs_destroy((*out)->files); qa_resource_pool_destroy((*out)->resources);
    free(*out); *out=NULL; return true;
}
